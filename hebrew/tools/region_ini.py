"""
Injects Hebrew strategic-map narration text into REGIONA.INI/REGIONH.INI/
REGIONO.INI (the per-starting-house "flavor text" shown between missions on
the strategic map, e.g. "The Atreides claimed strategic regions.").

Unlike DUNE.HEB and friends (eng.py), these are plain-text .INI files (see
Ini_GetString(), src/ini.c) with no compression/offset-table framing, read
via the normal loose-file-overrides-PAK lookup (File_ReadFile() ->
File_Open(), src/file.c) -- so no C code or new filename convention is
needed, just a loose REGION*.INI in bin/data/ that shadows the copy packed
inside SCENARIO.PAK.

Each file already holds every officially-supported language's text
side-by-side per scenario group, keyed by prefix+region number (ENGTXT13,
FRETXT13, GERTXT13, ...) -- see GUI_StrategicMap_ShowProgression(),
src/gui/gui.c, which builds the key as "%sTXT%d" % (g_languageSuffixes
[g_config.language], region). Hebrew is missing (no HEBTXT* keys), which is
why some strategic-map lines silently don't appear at all in Hebrew --
Ini_GetString() returns NULL and the draw call is just skipped, no error.

encode_file() works purely on the original bytes, splicing in new
"HEBTXTn = <hebrew>\\r\\n" lines right after each matching "ENGTXTn" line,
inside the same [GROUPn] block -- it never decodes/re-encodes the existing
French/German text, so their bytes (and whatever DOS codepage they're
actually in) pass through untouched. Hebrew text itself is encoded with
'cp862' (see eng.py's docstring) -- same codepage the Hebrew font's glyph
table expects everywhere else in this project. Deliberately does NOT
mirror the Hebrew text -- GUI_DrawText_WrapperBox() (src/gui/gui.c, the
function GUI_StrategicMap_DrawText() draws through) already mirrors RTL
lines itself at draw time, so this should hold plain, normal-reading-order
Hebrew, same convention as build_heb.py.

The pristine original REGION*.INI files are copyrighted game data, not
committed here -- see build_regions.py, which is the actual entry point
(reads hebrew/extracted/dune2_eu_1.07/REGION*.INI + this module's
translations, writes bin/data/REGION*.INI).
"""
import re

ENCODING = "cp862"

_GROUP_RE = re.compile(rb"^\[(\w+)\]\s*$")
_TXT_RE = re.compile(rb"^ENGTXT(\d+)\s*=")


def encode_file(original, entries):
    """original: bytes of the pristine REGION*.INI. entries: list of
    {"group": "GROUP1", "key": 13, "he": "..."} dicts (regions.json's
    per-file list). Returns the patched bytes, original content byte-for-
    byte unchanged except for the newly-inserted HEBTXT lines."""
    by_group_key = {(e["group"], e["key"]): e["he"] for e in entries}
    seen = set()

    lines = original.split(b"\r\n")
    out = []
    current_group = None

    for line in lines:
        out.append(line)

        m = _GROUP_RE.match(line.strip())
        if m is not None:
            current_group = m.group(1).decode("ascii").upper()
            continue

        m = _TXT_RE.match(line.strip())
        if m is None or current_group is None:
            continue

        key = int(m.group(1))
        he = by_group_key.get((current_group, key))
        if he is None:
            continue

        seen.add((current_group, key))
        out.append(f"HEBTXT{key}\t= ".encode("ascii") + he.encode(ENCODING))

    missing = set(by_group_key) - seen
    if missing:
        raise ValueError(f"translations with no matching ENGTXT line: {sorted(missing)}")

    return b"\r\n".join(out)
