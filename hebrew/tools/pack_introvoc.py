#!/usr/bin/env python3
"""
Patch INTROVOC.PAK, replacing BLDING.VOC and DYNASTY.VOC with the
title-corrected audio in hebrew/audio/ (see hebrew/README.md for why).

Unlike the rest of hebrew/tools/build_heb.py's ASSET_JOBS, this can't be a
simple "copy this file into bin/data/" job: BLDING.VOC/DYNASTY.VOC aren't
loose files the engine looks for on their own (the loose-file-overrides-PAK
lookup that INTRO1.WSA relies on turned out not to apply to VOC playback --
confirmed by testing in-game, only a fragment of "for" played) -- the audio
has to actually live inside INTROVOC.PAK's own archive, at the entries the
game already reads.

INTROVOC.PAK format (reverse-engineered from this repo's own
File_ReadBlockFile(), src/file.c): a header of repeated
(uint32 LE absolute-offset, nul-terminated filename) pairs, terminated by
one final offset of 0; then the concatenated file data itself, starting at
the offsets named in the header. A file's size isn't stored -- it's implied
by (next entry's offset - this entry's offset), with the last entry running
to EOF. Since we're replacing entries in place (same names, same count,
just different byte lengths), the header's own size never changes -- only
the offsets from the replaced entries onward shift by the size delta.

Needs the pristine original INTROVOC.PAK as input (copyrighted, not
committed here) -- expected at
hebrew/extracted/dune2_eu_1.07/INTROVOC.PAK, extracted from your own
legally-owned copy of the game (same convention as
build_intro1_animation.py's INTRO1.WSA/INTRO.PAL). Confirmed identical
(same MD5) across the US 1.0/1.07, EU 1.07, and HitSquad 1.07 releases, so
any of those works as the source.

Usage: python3 hebrew/tools/pack_introvoc.py
Writes bin/data/INTROVOC.PAK (where File_Init() looks by default).
"""
import struct
import sys
from pathlib import Path

HEBREW_DIR = Path(__file__).resolve().parent.parent
REPO_ROOT = HEBREW_DIR.parent

SOURCE_PAK = HEBREW_DIR / "extracted" / "dune2_eu_1.07" / "INTROVOC.PAK"
AUDIO_DIR = HEBREW_DIR / "audio"
DEST_PAK = REPO_ROOT / "bin" / "data" / "INTROVOC.PAK"

REPLACEMENTS = {
    "BLDING.VOC": AUDIO_DIR / "BLDING.VOC",
    "DYNASTY.VOC": AUDIO_DIR / "DYNASTY.VOC",
}


def parse_pak(data):
    """Return [(name, offset, size), ...] in on-disk order, plus the header
    length (offset of the first file's data, i.e. the terminator's end)."""
    entries = []
    pos = 0
    while True:
        (offset,) = struct.unpack_from("<I", data, pos)
        pos += 4
        if offset == 0:
            break
        end = data.index(b"\x00", pos)
        name = data[pos:end].decode("ascii")
        pos = end + 1
        entries.append([name, offset])
    header_len = pos
    for i in range(len(entries) - 1):
        entries[i].append(entries[i + 1][1] - entries[i][1])
    entries[-1].append(len(data) - entries[-1][1])
    return entries, header_len


def main():
    if not SOURCE_PAK.is_file():
        print(f"missing pristine source: {SOURCE_PAK}")
        print("copy your own legally-owned copy of INTROVOC.PAK there first "
              "(US 1.0/1.07, EU 1.07, and HitSquad 1.07 are all identical)")
        sys.exit(1)
    if not DEST_PAK.parent.is_dir():
        print(f"no destination directory {DEST_PAK.parent} -- run configure/make "
              "and install your Dune 2 data files there first")
        sys.exit(1)

    data = SOURCE_PAK.read_bytes()
    entries, header_len = parse_pak(data)

    replacement_bytes = {}
    for name, path in REPLACEMENTS.items():
        if not path.is_file():
            print(f"missing replacement source: {path}")
            sys.exit(1)
        replacement_bytes[name] = path.read_bytes()

    names_found = {name for name, _, _ in entries} & set(REPLACEMENTS)
    missing = set(REPLACEMENTS) - names_found
    if missing:
        print(f"names not found in {SOURCE_PAK.name}: {sorted(missing)}")
        sys.exit(1)

    # rebuild the data region, substituting replacement bytes in place,
    # and recompute each entry's offset as we go
    body = bytearray()
    new_entries = []
    for name, _old_offset, old_size in entries:
        chunk = replacement_bytes.get(name, data[_old_offset:_old_offset + old_size])
        new_offset = header_len + len(body)
        new_entries.append((name, new_offset))
        body += chunk

    header = bytearray()
    for name, offset in new_entries:
        header += struct.pack("<I", offset)
        header += name.encode("ascii") + b"\x00"
    header += struct.pack("<I", 0)
    assert len(header) == header_len, f"header size changed: {len(header)} != {header_len}"

    DEST_PAK.write_bytes(bytes(header) + bytes(body))

    print(f"wrote {DEST_PAK} ({len(header) + len(body)} bytes)")
    for name in REPLACEMENTS:
        old_size = next(s for n, _, s in entries if n == name)
        new_size = len(replacement_bytes[name])
        print(f"  {name}: {old_size} -> {new_size} bytes")


if __name__ == "__main__":
    main()
