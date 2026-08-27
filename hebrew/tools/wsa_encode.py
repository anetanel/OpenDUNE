import struct

def format40_encode(prev, cur):
    """Return format40 XOR-delta opcode stream turning prev into cur."""
    assert len(prev) == len(cur)
    n = len(cur)
    diff = bytes(a ^ b for a, b in zip(prev, cur))
    out = bytearray()
    i = 0
    while i < n:
        if diff[i] == 0:
            j = i
            while j < n and diff[j] == 0:
                j += 1
            run = j - i
            while run > 0:
                chunk = min(run, 0x7FFF)
                if chunk <= 127:
                    out.append(0x80 | chunk)
                else:
                    out.append(0x80)
                    out += struct.pack('<H', chunk)  # bit15=0 -> skip
                run -= chunk
            i = j
        else:
            j = i
            while j < n and diff[j] != 0:
                j += 1
            run = j - i
            while run > 0:
                chunk = min(run, 0x3FFF)
                if chunk <= 127:
                    out.append(chunk)
                else:
                    out.append(0x80)
                    out += struct.pack('<H', 0x8000 | chunk)
                out += diff[i:i+chunk]
                i += chunk
                run -= chunk
    out += b'\x80\x00\x00'
    return bytes(out)

def format80_encode_passthrough(data):
    """Wrap raw bytes as literal 'short copy' format80 opcodes (no real LZ compression)."""
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        chunk = min(n - i, 0x3F)
        out.append(0x80 | chunk)
        out += data[i:i+chunk]
        i += chunk
    out.append(0x80)  # exit
    return bytes(out)

def format80_encode_lz(data, max_candidates=64, allow_match=True):
    """Real LCW (format80) compression: back-references + run-length + literals."""
    n = len(data)
    out = bytearray()
    literal_buf = bytearray()

    def flush_literals():
        j = 0
        while j < len(literal_buf):
            chunk = min(len(literal_buf) - j, 63)
            out.append(0x80 | chunk)
            out.extend(literal_buf[j:j+chunk])
            j += chunk
        literal_buf.clear()

    index = {}  # 3-byte key -> list of positions (recent-last)
    K = 3

    def add_hash(pos):
        if pos + K <= n:
            key = bytes(data[pos:pos+K])
            lst = index.setdefault(key, [])
            lst.append(pos)
            if len(lst) > max_candidates:
                del lst[0]

    i = 0
    while i < n:
        # run-length-of-same-byte check (covers long constant runs cheaply)
        run_len = 1
        while i + run_len < n and data[i + run_len] == data[i] and run_len < 0xFFFF:
            run_len += 1

        best_len, best_pos = 0, -1
        if allow_match and i + K <= n:
            key = bytes(data[i:i+K])
            for pos in reversed(index.get(key, [])):
                # cap at (i - pos) so the source region is never still "in flight" --
                # avoids relying on decoder doing a byte-at-a-time overlapping copy
                maxl = min(n - i, 0xFFFF, i - pos)
                l = 0
                while l < maxl and data[pos + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_pos = l, pos

        if run_len >= 3 and run_len >= best_len:
            flush_literals()
            size = run_len
            while size > 0:
                chunk = min(size, 0xFFFF)
                out.append(0xFE)
                out += struct.pack('<H', chunk)
                out.append(data[i])
                size -= chunk
            for p in range(i, i + run_len):
                add_hash(p)
            i += run_len
        elif best_len >= 3:
            flush_literals()
            offset, size = best_pos, best_len
            if size <= 64:
                out.append(0xC0 | (size - 3))
                out += struct.pack('<H', offset)
            else:
                out.append(0xFF)
                out += struct.pack('<H', size)
                out += struct.pack('<H', offset)
            for p in range(i, i + best_len):
                add_hash(p)
            i += best_len
        else:
            literal_buf.append(data[i])
            add_hash(i)
            i += 1

    flush_literals()
    out.append(0x80)  # exit
    return bytes(out)

def build_wsa(frames_pixels, width, height, use_lz=True, allow_match=True):
    """frames_pixels: list of bytes (len width*height each), frame 0..N-1."""
    n = len(frames_pixels)
    prev = bytearray(width * height)
    compressed_frames = []
    for cur in frames_pixels:
        f40 = format40_encode(bytes(prev), cur)
        f80 = format80_encode_lz(f40, allow_match=allow_match) if use_lz else format80_encode_passthrough(f40)
        compressed_frames.append((f80, len(f40)))
        prev = bytearray(cur)

    header_len = 10
    table_len = 4 * (n + 2)
    offsets = [header_len + table_len]
    for f80, _ in compressed_frames:
        offsets.append(offsets[-1] + len(f80))
    offsets.append(0)  # sentinel, matches original file convention

    max_f40_len = max(l for _, l in compressed_frames)
    required_buffer_size = max_f40_len + 35

    out = bytearray()
    out += struct.pack('<HHHHH', n, width, height, required_buffer_size, 0)
    out += struct.pack(f'<{n+2}I', *offsets)
    for f80, _ in compressed_frames:
        out += f80
    return bytes(out)
