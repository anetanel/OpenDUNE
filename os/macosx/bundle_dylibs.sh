#!/bin/sh

# Makes the compiled executable inside an OSX .app bundle self-contained
# by copying every non-system dylib it (transitively) links against into
# Contents/Frameworks and rewriting the load commands to reference that
# copy via @executable_path instead of the build machine's absolute path
# -- otherwise the app only runs on a machine with that exact library
# installed at that exact path, which in practice means it only ever
# runs on the machine that built it. Re-signs afterwards: install_name_tool
# invalidates whatever signature was there, and an unsigned (not even
# ad-hoc signed) binary won't launch at all on Apple Silicon.
#
# Homebrew's dylibs (SDL2 included) are built with an install name of
# @rpath/libFoo.dylib plus an LC_RPATH load command pointing at Homebrew's
# absolute lib directory -- not a plain absolute path directly -- so an
# @rpath/ dependency still has to be resolved against the target's own
# LC_RPATH entries and copied/rewritten just like any other non-system
# one; it is not already portable on its own (confirmed the hard way: a
# first version of this script skipped @rpath/ deps outright on the
# assumption they were already relocatable, and the resulting .app still
# crashed at launch with dyld trying several absolute-Homebrew-path
# variants -- the signature of an unresolved @rpath lookup).
#
# Usage: bundle_dylibs.sh <path-to-executable-inside-Contents/MacOS>

set -e

EXE="$1"
MACOS_DIR=$(dirname "$EXE")
FRAMEWORKS_DIR="$MACOS_DIR/../Frameworks"

# Print this target's LC_RPATH entries, one per line.
rpaths_of() {
	otool -l "$1" | awk '
		/cmd LC_RPATH/ { getline; getline; sub(/^ *path /, ""); sub(/ \(offset [0-9]+\)$/, ""); print }
	'
}

# Resolve "@rpath/libFoo.dylib" to a real file on disk, trying in order:
# 1) Contents/Frameworks (a sibling branch of the dependency tree may
#    have already bundled it there);
# 2) target's own LC_RPATH entries;
# 3) a broad search under the standard Homebrew roots, since a
#    dependency's recorded LC_RPATH doesn't always actually point at
#    wherever the real copy on this machine lives (seen in practice with
#    some of libjxl's/libwebp's own sub-dependencies).
# Prints the resolved path, or nothing if none of these find it.
resolve_rpath() {
	target="$1"
	leaf="${2#@rpath/}"

	if [ -f "$FRAMEWORKS_DIR/$leaf" ]; then
		echo "$FRAMEWORKS_DIR/$leaf"
		return
	fi

	found=$(rpaths_of "$target" | while read -r rp; do
		candidate="$rp/$leaf"
		if [ -f "$candidate" ]; then
			echo "$candidate"
			break
		fi
	done)
	if [ -n "$found" ]; then
		echo "$found"
		return
	fi

	find /opt/homebrew /usr/local -name "$leaf" -print 2>/dev/null | head -n 1
}

bundle_one() {
	target="$1"

	otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
		case "$dep" in
			/usr/lib/*|/System/*|@executable_path/*)
				continue
				;;
			@rpath/*)
				src=$(resolve_rpath "$target" "$dep")
				if [ -z "$src" ]; then
					echo "bundle_dylibs.sh: WARNING: could not resolve $dep (referenced from $target) against any LC_RPATH entry -- left as-is" >&2
					continue
				fi
				;;
			@loader_path/*)
				echo "bundle_dylibs.sh: WARNING: $dep (referenced from $target) uses @loader_path, not handled -- left as-is" >&2
				continue
				;;
			*)
				src="$dep"
				;;
		esac

		libname=$(basename "$src")
		dest="$FRAMEWORKS_DIR/$libname"

		if [ ! -f "$dest" ]; then
			mkdir -p "$FRAMEWORKS_DIR"
			cp "$src" "$dest"
			chmod u+w "$dest"
			install_name_tool -id "@executable_path/../Frameworks/$libname" "$dest"
			# In a subshell: none of target/dep/src/libname/dest above are
			# scoped to this function (no portable "local" in POSIX sh), so
			# a direct recursive call would clobber them on return and the
			# -change call below would end up firing against whatever the
			# innermost recursion last left in $target instead of this
			# frame's actual target -- confirmed happening via a CI build
			# log: the second install_name_tool invocation for a freshly
			# bundled dylib kept re-targeting that same dylib instead of
			# the one that actually depended on it.
			( bundle_one "$dest" )
		fi

		install_name_tool -change "$dep" "@executable_path/../Frameworks/$libname" "$target"
	done

	codesign --force -s - "$target" 2>/dev/null || true
}

bundle_one "$EXE"

# Homebrew's "sdl2" formula is actually sdl2-compat, a shim that
# implements the SDL2 ABI by translating calls into a separate SDL3
# library it dlopen()s *by name* ("libSDL3.dylib", via @loader_path) in
# its dllinit() constructor at load time -- not a normal link-time
# dependency, so it never shows up in otool -L and the loop above never
# bundles it. Without it, the shim aborts at launch ("Failed loading
# SDL3 library"), confirmed via a real crash report (abort() inside
# libSDL2-2.0.0.dylib's own dllinit). See
# https://discourse.libsdl.org/t/sdl2-compat-mac-fixed-sdl3-libname-to-be-libsdl3-dylib/40802
if [ -f "$FRAMEWORKS_DIR/libSDL2-2.0.0.dylib" ] && [ ! -f "$FRAMEWORKS_DIR/libSDL3.dylib" ]; then
	sdl3=$(find /opt/homebrew /usr/local -name "libSDL3.dylib" -print 2>/dev/null | head -n 1)
	if [ -z "$sdl3" ]; then
		sdl3=$(find /opt/homebrew /usr/local -name "libSDL3.*.dylib" -print 2>/dev/null | head -n 1)
	fi

	if [ -n "$sdl3" ]; then
		cp -L "$sdl3" "$FRAMEWORKS_DIR/libSDL3.dylib"
		chmod u+w "$FRAMEWORKS_DIR/libSDL3.dylib"
		install_name_tool -id "@executable_path/../Frameworks/libSDL3.dylib" "$FRAMEWORKS_DIR/libSDL3.dylib"
		( bundle_one "$FRAMEWORKS_DIR/libSDL3.dylib" )
		codesign --force -s - "$FRAMEWORKS_DIR/libSDL3.dylib" 2>/dev/null || true
	else
		echo "bundle_dylibs.sh: WARNING: libSDL2-2.0.0.dylib (sdl2-compat) bundled but no libSDL3*.dylib found under /opt/homebrew or /usr/local -- the shim will fail to load SDL3 at runtime" >&2
	fi
fi
