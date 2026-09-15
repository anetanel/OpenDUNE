"""
Builds the strategic-map narration text shown between missions (e.g. "The
Atreides claimed strategic regions.") as a standalone, Hebrew-only .INI.

The original REGIONA.INI/REGIONH.INI/REGIONO.INI (one per starting house,
packed inside SCENARIO.PAK) already hold every officially-supported
language's text side-by-side per scenario group, keyed by prefix+region
number (ENGTXT13, FRETXT13, GERTXT13, ...) -- see
GUI_StrategicMap_ShowProgression(), src/gui/gui.c, which builds the key as
"%sTXT%d" % (g_languageSuffixes[g_config.language], region). Hebrew has no
such keys there.

Rather than splicing new HEBTXTn lines into a copy of that original file
(which would mean redistributing its existing, copyrighted English/French/
German text and region layout data under a new name), this instead builds
a small standalone file holding *only* the new HEBTXTn lines, nothing else
-- e.g.:

    [GROUP1]
    HEBTXT13	= ...
    [GROUP2]
    HEBTXT8	= ...

Sprites_CPS_LoadRegionClick() (src/sprites.c) loads this as a loose
"REGION<H>.HEB"-style file (via String_GenerateFilename(), same per-
language-suffix convention as every other Hebrew asset) *in addition to*
the real REGION<H>.INI, into a separate buffer (g_fileRegionINI_lang).
GUI_StrategicMap_ShowProgression() checks that buffer first and falls back
to the original file's own (English-only, for Hebrew) text if a key isn't
there -- see its comment in src/gui/gui.c. Since this file only ever needs
to exist on its own, it needs no pristine/copyrighted original input at
all, unlike build_intro1_animation.py.

Hebrew text is encoded with 'cp862' (see eng.py's docstring) -- same
codepage the Hebrew font's glyph table expects everywhere else in this
project. Deliberately does NOT mirror the Hebrew text --
GUI_DrawText_WrapperBox() (src/gui/gui.c, the function
GUI_StrategicMap_DrawText() draws through) already mirrors RTL lines
itself at draw time, so this should hold plain, normal-reading-order
Hebrew, same convention as build_heb.py.
"""

ENCODING = "cp862"


def encode_file(entries):
    """entries: list of {"group": "GROUP1", "key": 13, "he": "..."} dicts
    (regions.json's per-file list). Returns the standalone file's bytes,
    one [GROUPn] section per distinct group (in first-seen order), holding
    only that group's HEBTXTn lines."""
    groups = []
    by_group = {}

    for e in entries:
        group = e["group"]
        if group not in by_group:
            by_group[group] = []
            groups.append(group)
        by_group[group].append(e)

    out = []
    for group in groups:
        out.append(f"[{group}]".encode("ascii"))
        for e in by_group[group]:
            out.append(f"HEBTXT{e['key']}\t= ".encode("ascii") + e["he"].encode(ENCODING))

    return b"\r\n".join(out) + b"\r\n"
