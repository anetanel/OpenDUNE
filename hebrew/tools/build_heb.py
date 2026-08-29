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

If you edited hebrew/INTRO1-00049.png (the flying-logo intro animation's
source frame), regenerate hebrew/intro1.wsa first, before running this
script -- see hebrew/tools/build_intro1_animation.py (separate from this
job list: it needs heavier dependencies (opencv/numpy/Pillow) and your own
locally-extracted copy of the original game files, so it isn't run as part
of "build everything" above).
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
import mentat_eng  # noqa: E402


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


def _build_mentat(name):
    doc = json.loads((TRANSLATIONS_DIR / f"{name}.json").read_text())
    entries = doc["entries"]
    translated = sum(1 for e in entries if e["name_he"] != e["name_en"] or e["body_he"] != e["body_en"])
    return mentat_eng.encode(doc), len(entries), translated


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
    # Mentat per-house "encyclopedia" (help subject list) -- a separate IFF
    # FORM container, not eng.py's offset-table format. See mentat_eng.py.
    "mentata": ("MENTATA.HEB", lambda: _build_mentat("mentata")),
    "mentath": ("MENTATH.HEB", lambda: _build_mentat("mentath")),
    "mentato": ("MENTATO.HEB", lambda: _build_mentat("mentato")),
    # Text this engine's own C code hardcodes as a plain English string
    # literal (house names, a couple of modal error messages) rather than
    # looking up by STR_* index -- see EngineStringID in src/string.h.
    # Entry order here must match that enum exactly; String_LoadEngineStrings()
    # (src/string.c) only loads this file if it exists for the active
    # language, so English (and any language without one) is unaffected.
    "engine": ("ENGINE.HEB", lambda: _build_simple_list("engine_strings", False)),
}

# source path (relative to hebrew/) -> dest filename, for assets that are
# installed as-is (no encoding step).
# Fonts: lowercase "*h.fnt" is the suffix Font_Init() looks for when
# language==Hebrew (modeled on the existing "new6pg.fnt" German precedent).
# Graphics: plain ".HEB" language suffix, same convention String_GenerateFilename()
# uses for string tables (confirmed against AND/BTTN/HERALD/MENTAT/MISC/TITLE
# call sites in opendune.c, cutscene.c, sprites.c, gui.c).
#
# choam.heb.shp -> CHOAM.HEB holds the "BUILD THIS"/"RESUME GAME"/"UPGRADE"/
# scroll-arrow button graphics for the Construction Yard/Starport full-screen
# build modal (GUI_DisplayFactoryWindow). Every officially supported language
# ships its own CHOAM.<suffix> -- there's no working generic fallback if it's
# missing (Sprites_Load's fallback filename argument points at a file that
# isn't actually shipped), so without *some* CHOAM.HEB those buttons silently
# fail to draw (found by testing the actual modal). This used to ship as a
# byte-for-byte copy of the original English CHOAM.ENG (untranslated, just to
# keep the buttons functional); now a real hand-drawn Hebrew version.
#
# intro1.wsa -> INTRO1H.WSA is a special case: the flying-logo intro
# animation. cutscene.c/houseanimation.c hardcode the base "INTRO1.WSA"
# lookup with no per-language suffix at all (confirmed against both this
# repo and dunedynasty's identical mechanism), so this can't use the usual
# ".HEB"/"...h.fnt" naming conventions -- instead it's installed under its
# own non-clobbering name, and GameLoop_PlayAnimation() (src/cutscene.c)
# picks "INTRO1H.WSA" over the stock "INTRO1.WSA" only when
# g_config.language == LANGUAGE_HEBREW and the file exists.
#
# Note: BLDING.VOC/DYNASTY.VOC (the "battle for Arrakis" title-correction
# audio -- see hebrew/README.md) are NOT installed as loose ASSET_JOBS
# entries like INTRO1.WSA. The loose-file-overrides-PAK lookup that works
# for INTRO1.WSA turned out not to apply to VOC playback (confirmed by
# testing in-game -- only a fragment of "for" played), so those two files
# have to be packed directly into INTROVOC.PAK instead. See
# hebrew/tools/pack_introvoc.py (a separate script, like
# build_intro1_animation.py, since it needs the pristine original PAK as
# input and isn't part of "build everything" below).
ASSET_JOBS = {
    "intro.fnt": ("fonts", "introh.fnt"),
    "new8p.fnt": ("fonts", "new8ph.fnt"),
    "new6p.fnt": ("fonts", "new6ph.fnt"),
    "and.heb.cps": ("graphics", "AND.HEB"),
    "bttn.heb.shp": ("graphics", "BTTN.HEB"),
    "choam.heb.shp": ("graphics", "CHOAM.HEB"),
    "herald.heb.cps": ("graphics", "HERALD.HEB"),
    "mentat.heb.shp": ("graphics", "MENTAT.HEB"),
    "misc.heb.cps": ("graphics", "MISC.HEB"),
    "title.heb.cps": ("graphics", "TITLE.HEB"),
    "intro1.wsa": (".", "INTRO1H.WSA"),
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
