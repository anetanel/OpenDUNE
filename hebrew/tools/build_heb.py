#!/usr/bin/env python3
"""
Build the Hebrew-language data files OpenDUNE needs (string tables, fonts,
graphics) from the translation source in hebrew/, and drop them into
bin/data/ -- the directory OpenDUNE's File_Init() resolves at runtime
(DUNE_DATA_DIR defaults to "./data", and the compiled `opendune` binary
lives in bin/ alongside its data/ dir).

Deliberately does NOT mirror Hebrew text here -- OpenDUNE mirrors Hebrew
lines itself at draw time (see GUI_MirrorRTLText() in src/gui/gui.c), so
the .HEB files should hold plain, normal-reading-order Hebrew, same as
you'd type it anywhere.

Usage:
    python3 hebrew/tools/build_heb.py            # build everything
    python3 hebrew/tools/build_heb.py dune intro  # just these
    python3 hebrew/tools/build_heb.py list        # show valid names
"""
import json
import sys
from pathlib import Path

HEBREW_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = HEBREW_DIR.parent
TRANSLATIONS_DIR = HEBREW_DIR / "translations"
FONTS_DIR = HEBREW_DIR / "fonts"
GRAPHICS_DIR = HEBREW_DIR / "graphics"

DEST_DIR = REPO_ROOT / "bin" / "data"

sys.path.insert(0, str(HEBREW_DIR / "tools"))
import eng  # noqa: E402


def _write(name, data):
    if not DEST_DIR.is_dir():
        return []
    (DEST_DIR / name).write_bytes(data)
    return [DEST_DIR]


def _build_simple_list(name, compressed):
    pairs = json.loads((TRANSLATIONS_DIR / f"{name}.json").read_text())
    strings = [p["he"] if p["he"] != p["en"] else p["en"] for p in pairs]
    translated = sum(1 for p in pairs if p["he"] != p["en"])
    return eng.encode(strings, compressed=compressed), len(pairs), translated


# name -> (output filename, builder). compressed flags mirror exactly the
# `compressed` argument String_Init() passes to String_Load() for each
# table in src/string.c.
STRING_JOBS = {
    "dune": ("DUNE.HEB", lambda: _build_simple_list("dune", False)),
    "message": ("MESSAGE.HEB", lambda: _build_simple_list("message", False)),
    "intro": ("INTRO.HEB", lambda: _build_simple_list("intro", False)),
    "texth": ("TEXTH.HEB", lambda: _build_simple_list("texth", True)),
    "texta": ("TEXTA.HEB", lambda: _build_simple_list("texta", True)),
    "texto": ("TEXTO.HEB", lambda: _build_simple_list("texto", True)),
    "protect": ("PROTECT.HEB", lambda: _build_simple_list("protect", True)),
}

# source path (relative to hebrew/) -> dest filename, for assets that are
# installed as-is (no encoding step).
# Fonts: lowercase "*h.fnt" is the suffix Font_Init() looks for when
# language==Hebrew (modeled on the existing "new6pg.fnt" German precedent).
# Graphics: plain ".HEB" language suffix, same convention String_GenerateFilename()
# uses for string tables (confirmed against AND/BTTN/HERALD/MENTAT/MISC/TITLE
# call sites in opendune.c, cutscene.c, sprites.c, gui.c).
#
# choam.eng-fallback.cps -> CHOAM.HEB is NOT translated -- it's a byte-for-byte
# copy of the original English CHOAM.ENG. Every officially supported language
# ships its own CHOAM.<suffix> (it holds the "BUILD THIS"/"RESUME GAME"/
# "UPGRADE"/scroll-arrow button graphics used by the Construction Yard/Starport
# full-screen build modal, GUI_DisplayFactoryWindow) -- there's no working
# generic fallback if it's missing (Sprites_Load's fallback filename argument
# points at a file that isn't actually shipped), so without *some* CHOAM.HEB
# those buttons silently fail to draw (found by testing the actual modal).
# Shipping the English graphic keeps the buttons functional, with English
# labels, until someone draws a proper Hebrew version.
ASSET_JOBS = {
    "intro.fnt": ("fonts", "introh.fnt"),
    "new8p.fnt": ("fonts", "new8ph.fnt"),
    "new6p.fnt": ("fonts", "new6ph.fnt"),
    "and.heb.cps": ("graphics", "AND.HEB"),
    "bttn.heb.shp": ("graphics", "BTTN.HEB"),
    "choam.eng-fallback.cps": ("graphics", "CHOAM.HEB"),
    "herald.heb.cps": ("graphics", "HERALD.HEB"),
    "mentat.heb.shp": ("graphics", "MENTAT.HEB"),
    "misc.heb.cps": ("graphics", "MISC.HEB"),
    "title.heb.cps": ("graphics", "TITLE.HEB"),
}


def main():
    args = sys.argv[1:]
    if args == ["list"]:
        print("\n".join(sorted(STRING_JOBS)))
        return
    if not DEST_DIR.is_dir():
        print(f"no destination directory {DEST_DIR} -- run configure/make and "
              "install your Dune 2 data files there first")
        sys.exit(1)

    names = args if args else sorted(STRING_JOBS)
    for name in names:
        if name not in STRING_JOBS:
            print(f"unknown name: {name} (see: build_heb.py list)")
            sys.exit(1)
        outname, builder = STRING_JOBS[name]
        data, count, translated = builder()
        dests = _write(outname, data)
        print(f"{name:10s} -> {outname:14s} ({translated}/{count} translated) -> {', '.join(str(d) for d in dests)}")

    if not args:
        for src, (subdir, outname) in ASSET_JOBS.items():
            srcpath = HEBREW_DIR / subdir / src
            data = srcpath.read_bytes()
            dests = _write(outname, data)
            print(f"{'asset':10s} -> {outname:14s} (copied from {src}) -> {', '.join(str(d) for d in dests)}")


if __name__ == "__main__":
    main()
