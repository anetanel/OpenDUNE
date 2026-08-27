"""
Reader/writer for Dune II's ".ENG" string table format (INTRO.ENG and
friends, from ENGLISH.PAK): a header of little-endian uint16 offsets
(one per string, count implied by the first offset == count*2), followed
by the null-terminated strings themselves. A literal \\r (0x0D) inside a
string is a manual line break within one subtitle/dialogue line.

Decoded/encoded with the 'cp862' codepage rather than ASCII: bytes 0x00-
0x7F are identical to ASCII in cp862 (so the original English text is
unaffected), but 0x80-0x9A decode to the same Hebrew letters used for the
font's glyph slots 128-154 -- so a Hebrew translation typed with those
code points round-trips correctly through this same encoder.

DUNE2.EXE draws this text LTR with no bidi awareness, so any Hebrew needs
mirroring before it'll read correctly on screen -- see mirror_rtl(). That
is NOT part of decode()/encode() (which stay a pure, symmetric format
round-trip -- decode(encode(x)) == x, including for Hebrew content) since
mirroring is a one-way display transform, not a property of the file
format. Apply it once, right before the final encode -- see
build_translation.py's build_intro_eng().
"""
import json
import struct
import sys
from pathlib import Path

ENCODING = "cp862"

# Digram table used by DUNE2.EXE's String_DecompressAndTranslate for the
# "compressed" language files (TEXTH/TEXTA/TEXTO/PROTECT -- the Mentat
# per-house win/lose/advice/briefing text, one file per house). A byte with
# the high bit set packs two characters: the top 4 bits (after masking off
# the high bit) index the first row below for the 1st char, and the full
# 7-bit value indexes 16 bytes into the flattened table for the 2nd char
# (an 8-entry "what usually follows this letter" sub-table). Reverse-
# engineered from OpenDUNE's src/string.c (String_DecompressAndTranslate),
# confirmed against extracted/dune2_eu_1.07/ENGLISH/TEXTA.ENG.
#
# Byte 0x1B is an escape: the following byte X is untranslated and decodes
# to the literal char 0x7F + X. This is how bytes >=0x80 that are NOT one
# of the digram codes (e.g. our cp862 Hebrew glyphs at 0x80-0x9A) get
# represented -- a plain literal byte >=0x80 would otherwise be consumed by
# the digram logic above instead of standing for itself.
_COUPLES = (
    " etainosrlhcdupm"  # 1st char, indexed by (byte & 0x7F) >> 3
    "tasio wb"  # after <SPACE>
    " rnsdalm"  # after e
    "h ieoras"  # after t
    "nrtlc sy"  # after a
    "nstcloer"  # after i
    " dtgesio"  # after n
    "nr ufmsw"  # after o
    " tep.ica"  # after s
    "e oiadur"  # after r
    " laeiyod"  # after l
    "eia otru"  # after h
    "etoakhlr"  # after c
    " eiu,.oa"  # after d
    "nsrctlai"  # after u
    "leoiratp"  # after p
    "eaoip bm"  # after m
)


def _decompress(raw):
    """Reverse DUNE2.EXE's String_DecompressAndTranslate: turn the raw
    (possibly digram-packed) bytes of one string (no NUL terminator) back
    into plain bytes, ready to decode() with ENCODING."""
    out = bytearray()
    it = iter(raw)
    for c in it:
        if c & 0x80:
            c &= 0x7F
            out.append(ord(_COUPLES[c >> 3]))
            c = ord(_COUPLES[c + 16])
        elif c == 0x1B:
            c = (0x7F + next(it)) & 0xFF
        out.append(c)
    return bytes(out)


def _escape_compressed(raw):
    """Encode plain bytes into a form String_DecompressAndTranslate will
    turn back into exactly `raw`. This does NOT reproduce the original
    game's digram packing (that would require a greedy/optimal matcher
    against _COUPLES, and there's no need -- the game only cares that
    decompression reproduces the right bytes, not that the input was
    maximally packed). Bytes <0x80 other than 0x1B pass through as
    literals (decompression treats them as literals too); 0x1B is escaped
    as itself so it isn't mistaken for the escape prefix; bytes >=0x80
    (e.g. Hebrew cp862 glyphs) are written via the 0x1B escape so they
    aren't misread as digram codes."""
    out = bytearray()
    for c in raw:
        if c == 0x1B:
            out += bytes((0x1B, (0x1B - 0x7F) % 256))
        elif c >= 0x80:
            out += bytes((0x1B, c - 0x7F))
        else:
            out.append(c)
    return bytes(out)


def decode(data, compressed=False):
    if len(data) < 2:
        raise ValueError("file too short to contain an offsets header")
    first_offset = struct.unpack_from("<H", data, 0)[0]
    if first_offset % 2 != 0 or first_offset == 0:
        raise ValueError(f"implausible first offset {first_offset} (should be a positive even header length)")
    count = first_offset // 2
    if count * 2 > len(data):
        raise ValueError("header claims more strings than the file can hold")
    offsets = struct.unpack_from(f"<{count}H", data, 0)
    strings = []
    for off in offsets:
        if off > len(data):
            raise ValueError(f"string offset {off} exceeds file length {len(data)}")
        end = data.find(b"\x00", off)
        if end == -1:
            raise ValueError(f"string at offset {off} is not null-terminated")
        raw = data[off:end]
        if compressed:
            raw = _decompress(raw)
        strings.append(raw.decode(ENCODING))
    return strings


HEBREW_LO, HEBREW_HI = 0x05D0, 0x05EA  # Aleph..Tav


def _has_hebrew(s):
    return any(HEBREW_LO <= ord(c) <= HEBREW_HI for c in s)


def _reverse_keep_digit_runs(s):
    """Reverse character order, but keep any run of ASCII digits in its
    original left-to-right order (only its position within the line
    moves) -- numbers always read most-significant-digit-first, even
    embedded in RTL text."""
    units = []
    i, n = 0, len(s)
    while i < n:
        if s[i].isdigit():
            j = i
            while j < n and s[j].isdigit():
                j += 1
            units.append(s[i:j])
            i = j
        else:
            units.append(s[i])
            i += 1
    return "".join(reversed(units))


def mirror_rtl(strings):
    """Mirror every string containing Hebrew for display by this engine.

    DUNE2.EXE draws subtitle text as a plain byte run, left to right, with
    no bidi awareness (there's no RTL-native engine patch here, unlike the
    sibling Dune-Heb project). So a Hebrew string typed/stored in normal
    reading order comes out on screen mirrored letter-by-letter -- confirmed
    live, "מציגים" rendered backwards. The fix is the standard one for this
    class of engine: store the string already reversed, so the LTR drawer
    paints it out in the correct visual order.

    Each \\r-delimited line (a manual line break within one string) is
    mirrored independently -- the line order itself is not reading order
    and must not flip. Strings with no Hebrew (still-untranslated English)
    are left untouched, so this is safe to run over a partially-translated
    file.
    """
    out = []
    for s in strings:
        lines = s.split("\r")
        out.append("\r".join(_reverse_keep_digit_runs(line) if _has_hebrew(line) else line for line in lines))
    return out


def encode(strings, compressed=False):
    count = len(strings)
    header_len = count * 2
    encoded = [s.encode(ENCODING) + b"\x00" for s in strings]
    if compressed:
        encoded = [_escape_compressed(chunk[:-1]) + b"\x00" for chunk in encoded]
    offsets = []
    cursor = header_len
    for chunk in encoded:
        offsets.append(cursor)
        cursor += len(chunk)
    out = bytearray()
    for off in offsets:
        out += struct.pack("<H", off)
    for chunk in encoded:
        out += chunk
    return bytes(out)


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if a != "--compressed"]
    compressed = "--compressed" in sys.argv
    if len(args) < 2:
        print("usage:")
        print("  eng.py decode [--compressed] <in.eng> <out.json>")
        print("  eng.py encode [--compressed] <in.json> <out.eng>")
        print("  eng.py roundtrip-check [--compressed] <in.eng>")
        print()
        print("  --compressed is for TEXTH/TEXTA/TEXTO/PROTECT (digram-packed by")
        print("  DUNE2.EXE's String_DecompressAndTranslate); INTRO.ENG and DUNE.ENG")
        print("  are not compressed.")
        sys.exit(1)
    cmd = args[0]
    if cmd == "decode":
        # Emits the translations/*.json schema: a list of {"en", "he"}
        # pairs, so the English original stays visible next to the
        # (initially untranslated, i.e. he == en) Hebrew for reference.
        strings = decode(Path(args[1]).read_bytes(), compressed=compressed)
        pairs = [{"en": s, "he": s} for s in strings]
        Path(args[2]).write_text(json.dumps(pairs, ensure_ascii=False, indent=2) + "\n")
        print(f"{len(strings)} strings -> {args[2]}")
    elif cmd == "encode":
        pairs = json.loads(Path(args[1]).read_text())
        strings = [pair["he"] for pair in pairs]
        Path(args[2]).write_bytes(encode(mirror_rtl(strings), compressed=compressed))
        print(f"{len(strings)} strings -> {args[2]} (Hebrew lines mirrored for this LTR-only engine)")
    elif cmd == "roundtrip-check":
        orig = Path(args[1]).read_bytes()
        if compressed:
            # encode() doesn't reproduce the game's own digram packing (see
            # _escape_compressed's docstring), so bytes won't match -- only
            # check that decompressing what we'd write reproduces the same
            # content as decompressing the original.
            content = decode(orig, compressed=True)
            again_content = decode(encode(content, compressed=True), compressed=True)
            if again_content == content:
                print("OK: content round trip matches (bytes differ -- expected, we don't re-pack digrams)")
            else:
                print("MISMATCH: decompressed content differs after re-encoding")
                sys.exit(1)
        else:
            again = encode(decode(orig))
            if again == orig:
                print("OK: byte-identical round trip")
            else:
                print(f"MISMATCH: {len(orig)} bytes in, {len(again)} bytes out")
                sys.exit(1)
    else:
        print(f"unknown command: {cmd}")
        sys.exit(1)
