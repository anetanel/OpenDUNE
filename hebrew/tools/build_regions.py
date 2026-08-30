#!/usr/bin/env python3
"""
Build bin/data/REGIONA.INI, REGIONH.INI, REGIONO.INI with Hebrew strategic-
map narration text added (see region_ini.py's docstring for why this needs
its own script instead of a build_heb.py STRING_JOBS entry).

Needs the pristine original REGION*.INI files as input (copyrighted, not
committed here) -- expected at hebrew/extracted/dune2_eu_1.07/REGION*.INI,
extracted from your own legally-owned copy of the game (e.g. via dunepak
against bin/data/SCENARIO.PAK -- same convention as
build_intro1_animation.py/pack_introvoc.py's inputs).

Usage: python3 hebrew/tools/build_regions.py
"""
import json
import sys
from pathlib import Path

HEBREW_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = HEBREW_DIR.parent
TRANSLATIONS_DIR = HEBREW_DIR / "translations"
SOURCE_DIR = HEBREW_DIR / "extracted" / "dune2_eu_1.07"
DEST_DIR = REPO_ROOT / "bin" / "data"

sys.path.insert(0, str(HEBREW_DIR / "tools"))
import region_ini  # noqa: E402

FILES = ["REGIONA.INI", "REGIONH.INI", "REGIONO.INI"]


def main():
    missing = [name for name in FILES if not (SOURCE_DIR / name).is_file()]
    if missing:
        print(f"missing pristine source(s) in {SOURCE_DIR}: {missing}")
        print("extract them from your own legally-owned copy of the game "
              "(e.g. dunepak unpak bin/data/SCENARIO.PAK) and copy them there first")
        sys.exit(1)
    if not DEST_DIR.is_dir():
        print(f"no destination directory {DEST_DIR} -- run configure/make and "
              "install your Dune 2 data files there first")
        sys.exit(1)

    translations = json.loads((TRANSLATIONS_DIR / "regions.json").read_text(encoding="utf-8"))

    for name in FILES:
        original = (SOURCE_DIR / name).read_bytes()
        entries = translations[name]
        translated = sum(1 for e in entries if e["he"] != e["en"])

        patched = region_ini.encode_file(original, entries)
        (DEST_DIR / name).write_bytes(patched)

        print(f"{name:12s} ({translated}/{len(entries)} translated) -> {DEST_DIR / name}")


if __name__ == "__main__":
    main()
