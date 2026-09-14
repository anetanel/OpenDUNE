#!/bin/sh

# Makes the compiled executable inside an OSX .app bundle self-contained
# by copying every non-system dylib it (transitively) links against into
# Contents/Frameworks and rewriting the load commands to reference that
# copy via @executable_path instead of the build machine's absolute path
# (e.g. Homebrew's /opt/homebrew/... or /usr/local/...) -- otherwise the
# app only runs on a machine with that exact library installed at that
# exact path, which in practice means it only ever runs on the machine
# that built it. Re-signs afterwards: install_name_tool invalidates
# whatever signature was there, and an unsigned (not even ad-hoc signed)
# binary won't launch at all on Apple Silicon.
#
# Usage: bundle_dylibs.sh <path-to-executable-inside-Contents/MacOS>

set -e

EXE="$1"
MACOS_DIR=$(dirname "$EXE")
FRAMEWORKS_DIR="$MACOS_DIR/../Frameworks"

bundle_one() {
	target="$1"

	otool -L "$target" | tail -n +2 | awk '{print $1}' | while read -r dep; do
		case "$dep" in
			/usr/lib/*|/System/*|@executable_path/*|@rpath/*|@loader_path/*)
				continue
				;;
		esac

		libname=$(basename "$dep")
		dest="$FRAMEWORKS_DIR/$libname"

		if [ ! -f "$dest" ]; then
			mkdir -p "$FRAMEWORKS_DIR"
			cp "$dep" "$dest"
			chmod u+w "$dest"
			install_name_tool -id "@executable_path/../Frameworks/$libname" "$dest"
			bundle_one "$dest"
		fi

		install_name_tool -change "$dep" "@executable_path/../Frameworks/$libname" "$target"
	done

	codesign --force -s - "$target" 2>/dev/null || true
}

bundle_one "$EXE"
