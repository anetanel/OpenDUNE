"""
Reader/writer for MENTATA.ENG / MENTATH.ENG / MENTATO.ENG (from
ENGLISH.PAK): the per-house Mentat "encyclopedia" data -- the topic list
you browse in the Mentat help screen (Houses/Structures/Vehicles/Specials,
each with sub-entries like "Barracks" or "Trike") plus, for each topic,
the WSA picture to show and the descriptive text.

This is a completely different container from eng.py's offset-table
format: a Westwood/EA-IFF FORM file (formType "MENT") with three chunks:

  NAME  a packed list of entries, each:
          size:u8       total entry length, including this byte
          offset:u32be  ABSOLUTE file offset of this entry's compressed
                        text record (see DESC below)
          code:2 bytes  ASCII digit pair, e.g. "11" -- tens digit is a
                        top category (0=briefing/advice/orders, 1=houses,
                        2=structures, 3=vehicles, 4=specials), units digit
                        distinguishes a category header ("0", e.g.
                        "10Houses") from a leaf entry ("1"/"2"/...); used
                        by OpenDUNE's GUI_Mentat_Draw for indent/colour
                        and by GUI_Mentat_ShowHelp's noDesc check
                        (tens digit '0' -> ignore this entry's DESC text
                        and show campaign narrative from DUNE.ENG instead)
          name          NUL-terminated menu label, e.g. "Barracks"
          threshold:u8  last byte of the entry: minimum campaign/mission
                        ID required for this entry to appear in the list

  DESC  a pool of variable-length compressed text records, one per NAME
        entry, digram-compressed the same way as TEXTA/TEXTH/TEXTO (see
        eng.py's _decompress/_escape_compressed) -- BUT packed in some
        unrelated original order, not the NAME list's order, and not
        aligned with any padding: each record's true length is only
        recoverable by sorting all entries by `offset` and taking
        consecutive differences (confirmed empirically: every record's
        decompressed bytes end with exactly one NUL at the computed
        boundary, never bleeding into a neighbour). Each record decodes
        to  <picture-ref><delim><rest>\\0  where:
          picture-ref  a WSA filename (e.g. "Trike.wsa") for a normal
                       entry, or a bare digit for the noDesc entries
                       (unused filler content the game ignores)
          delim        '*' (loop the WSA animation) or '?' (single frame)
          rest         either a plain filler line (noDesc entries, e.g.
                       "No desc ") or  <caption>\\x0c<description>  where
                       \\x0c marks where the picture's caption text ends
                       and the paragraph body begins (some captions also
                       contain \\r manual line breaks, same convention as
                       eng.py's format)

  INFO  12 bytes: an 8-byte prefix the game's own code labels `notused`
        (never read -- see OpenDUNE's GUI_Mentat_ShowHelp) which we
        preserve byte-for-byte rather than guess at, followed by a
        u32be `length`: how many bytes GUI_Mentat_ShowHelp reads (from
        the clicked entry's own offset) before decompressing -- always
        exactly the length of the single largest DESC record in the file
        in the original data, i.e. a fixed generous window sized to
        whichever entry needs the most room. We recompute this on encode
        the same way, sized to the new largest record, so a longer
        Hebrew description is never truncated.

Text is decoded/encoded with cp862 like eng.py's format (see eng.py's
module docstring). Unlike dunedynasty (where this codec was ported from),
this engine mirrors Hebrew lines itself at *draw time*
(GUI_MirrorRTLText(), src/gui/gui.c -- the single shared entry point for
every text-drawing path, including the Mentat list widgets and
GUI_DrawText_WrapperBox()'s per-line rendering), so encode() here must
NOT pre-mirror -- store name_he/body_he in plain, normal-reading-order
Hebrew, same convention build_heb.py's own top-of-file docstring states
for every other translation job. Pre-mirroring here would double-mirror
on top of the draw-time pass and come out reversed/interleaved wrong.
"""
import json
import struct
import sys
from pathlib import Path

UTILS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(UTILS_DIR))
import eng  # noqa: E402

ENCODING = eng.ENCODING
FORM_TYPE = b"MENT"


def _read_chunks(data):
    if data[0:4] != b"FORM":
        raise ValueError("not an IFF FORM file")
    formtype = data[8:12]
    if formtype != FORM_TYPE:
        raise ValueError(f"unexpected form type {formtype!r} (expected {FORM_TYPE!r})")
    pos = 12
    chunks = {}
    while pos < len(data):
        tag = data[pos : pos + 4].decode("ascii")
        size = struct.unpack_from(">I", data, pos + 4)[0]
        chunks[tag] = (pos + 8, data[pos + 8 : pos + 8 + size])
        pos += 8 + size + (size % 2)
    for required in ("NAME", "DESC", "INFO"):
        if required not in chunks:
            raise ValueError(f"missing {required} chunk")
    return chunks


def _parse_name_entries(name_data):
    pos = 0
    entries = []
    while pos < len(name_data):
        size = name_data[pos]
        if size == 0:
            raise ValueError(f"zero-size NAME entry at {pos}")
        entry = name_data[pos : pos + size]
        offset = struct.unpack_from(">I", entry, 1)[0]
        code = entry[5:7].decode("ascii")
        nul = entry.index(0, 7)
        name = entry[7:nul].decode(ENCODING)
        threshold = entry[-1]
        if nul != size - 2:
            raise ValueError(f"NAME entry at {pos}: name/threshold layout mismatch")
        entries.append({"offset": offset, "code": code, "name": name, "threshold": threshold})
        pos += size
    if pos != len(name_data):
        raise ValueError("NAME chunk not fully consumed")
    return entries


def decode(data):
    chunks = _read_chunks(data)
    name_entries = _parse_name_entries(chunks["NAME"][1])
    desc_abs, desc_bytes = chunks["DESC"]
    info_abs, info_bytes = chunks["INFO"]
    if len(info_bytes) != 12:
        raise ValueError(f"INFO chunk should be 12 bytes, got {len(info_bytes)}")
    info_prefix = info_bytes[:8]

    desc_end = desc_abs + len(desc_bytes)
    order = sorted(range(len(name_entries)), key=lambda i: name_entries[i]["offset"])
    bounds = {}
    for pos, idx in enumerate(order):
        start = name_entries[idx]["offset"]
        end = name_entries[order[pos + 1]]["offset"] if pos + 1 < len(order) else desc_end
        if not (desc_abs <= start < end <= desc_end):
            raise ValueError(f"entry {idx} ({name_entries[idx]['name']!r}): implausible DESC bounds {start}..{end}")
        bounds[idx] = (start, end)

    entries = []
    for idx, ne in enumerate(name_entries):
        start, end = bounds[idx]
        decompressed = eng._decompress(data[start:end])
        if not decompressed.endswith(b"\x00") or b"\x00" in decompressed[:-1]:
            raise ValueError(f"entry {idx} ({ne['name']!r}): decompressed record doesn't end in exactly one NUL")
        text = decompressed[:-1].decode(ENCODING)
        delim_pos = next((i for i, c in enumerate(text) if c in "*?"), None)
        if delim_pos is None:
            raise ValueError(f"entry {idx} ({ne['name']!r}): no '*'/'?' delimiter in {text!r}")
        entries.append(
            {
                "code": ne["code"],
                "threshold": ne["threshold"],
                "name_en": ne["name"],
                "name_he": ne["name"],
                "prefix": text[:delim_pos],
                "delim": text[delim_pos],
                "body_en": text[delim_pos + 1 :],
                "body_he": text[delim_pos + 1 :],
            }
        )

    return {"info_prefix_hex": info_prefix.hex(), "entries": entries}


def _encode_record(prefix, delim, body_he):
    # Not mirrored: OpenDUNE mirrors Hebrew lines itself at draw time
    # (GUI_MirrorRTLText()) -- see module docstring.
    text = prefix + delim + body_he
    plain = text.encode(ENCODING)
    return eng._escape_compressed(plain) + b"\x00"


def encode(doc):
    info_prefix = bytes.fromhex(doc["info_prefix_hex"])
    if len(info_prefix) != 8:
        raise ValueError("info_prefix_hex must decode to 8 bytes")

    name_entries = []
    desc_blobs = []
    for e in doc["entries"]:
        # Not mirrored: OpenDUNE mirrors Hebrew lines itself at draw time
        # (GUI_MirrorRTLText()) -- see module docstring.
        name_bytes = e["name_he"].encode(ENCODING)
        code_bytes = e["code"].encode("ascii")
        if len(code_bytes) != 2:
            raise ValueError(f"code must be 2 ASCII bytes, got {e['code']!r}")
        size = 1 + 4 + 2 + len(name_bytes) + 1 + 1
        if size > 255:
            raise ValueError(f"entry {e['name_en']!r}: translated name too long ({size} byte entry)")
        name_entries.append((size, code_bytes, name_bytes, e["threshold"]))
        desc_blobs.append(_encode_record(e["prefix"], e["delim"], e["body_he"]))

    name_chunk = bytearray()
    for size, code_bytes, name_bytes, threshold in name_entries:
        name_chunk.append(size)
        # offset filled in below, once we know where DESC starts
        name_chunk += b"\x00\x00\x00\x00"
        name_chunk += code_bytes
        name_chunk += name_bytes
        name_chunk.append(0)
        name_chunk.append(threshold)
    name_pad = len(name_chunk) % 2

    desc_start = 12 + 8 + len(name_chunk) + name_pad + 8
    offsets = []
    cursor = desc_start
    for blob in desc_blobs:
        offsets.append(cursor)
        cursor += len(blob)
    desc_chunk = b"".join(desc_blobs)
    desc_pad = len(desc_chunk) % 2

    # patch offsets into name_chunk now that they're known
    pos = 0
    for i, (size, code_bytes, name_bytes, threshold) in enumerate(name_entries):
        struct.pack_into(">I", name_chunk, pos + 1, offsets[i])
        pos += size

    max_len = max(len(b) for b in desc_blobs)
    info_chunk = info_prefix + struct.pack(">I", max_len)

    out = bytearray()
    out += b"NAME" + struct.pack(">I", len(name_chunk)) + bytes(name_chunk) + b"\x00" * name_pad
    out += b"DESC" + struct.pack(">I", len(desc_chunk)) + desc_chunk + b"\x00" * desc_pad
    out += b"INFO" + struct.pack(">I", len(info_chunk)) + info_chunk

    formsize = 4 + len(out)  # formType (4 bytes) + all chunks
    return b"FORM" + struct.pack(">I", formsize) + FORM_TYPE + bytes(out)


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) < 2:
        print("usage:")
        print("  mentat_eng.py decode <in.eng> <out.json>")
        print("  mentat_eng.py encode <in.json> <out.eng>")
        print("  mentat_eng.py roundtrip-check <in.eng>")
        sys.exit(1)
    cmd = args[0]
    if cmd == "decode":
        doc = decode(Path(args[1]).read_bytes())
        Path(args[2]).write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n")
        print(f"{len(doc['entries'])} entries -> {args[2]}")
    elif cmd == "encode":
        doc = json.loads(Path(args[1]).read_text())
        Path(args[2]).write_bytes(encode(doc))
        print(f"{len(doc['entries'])} entries -> {args[2]} (not mirrored -- OpenDUNE mirrors Hebrew at draw time)")
    elif cmd == "roundtrip-check":
        # encode() doesn't reproduce the game's own digram packing (see
        # eng.py's _escape_compressed docstring), so bytes won't match --
        # only check that decoding what we'd write reproduces the same
        # content as decoding the original.
        orig = Path(args[1]).read_bytes()
        doc = decode(orig)
        again_doc = decode(encode(doc))
        if again_doc == doc:
            print("OK: content round trip matches (bytes differ -- expected, we don't re-pack digrams)")
        else:
            print("MISMATCH: decoded content differs after re-encoding")
            sys.exit(1)
    else:
        print(f"unknown command: {cmd}")
        sys.exit(1)
