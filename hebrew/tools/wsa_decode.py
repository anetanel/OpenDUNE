import struct, sys

def format80_decode(source, dest_length):
    dest = bytearray(dest_length)
    start = 0
    di = 0
    si = 0
    n = len(source)
    while di < dest_length:
        cmd = source[si]; si += 1
        if cmd == 0x80:
            break
        elif (cmd & 0x80) == 0:
            size = (cmd >> 4) + 3
            size = min(size, dest_length - di)
            offset = ((cmd & 0xF) << 8) + source[si]; si += 1
            for _ in range(size):
                dest[di] = dest[di - offset]
                di += 1
        elif cmd == 0xFE:
            size = source[si] + (source[si+1] << 8); si += 2
            size = min(size, dest_length - di)
            val = source[si]; si += 1
            for _ in range(size):
                dest[di] = val; di += 1
        elif cmd == 0xFF:
            size = source[si] + (source[si+1] << 8); si += 2
            size = min(size, dest_length - di)
            offset = source[si] + (source[si+1] << 8); si += 2
            for _ in range(size):
                dest[di] = dest[offset]; offset += 1; di += 1
        elif (cmd & 0x40) != 0:
            size = (cmd & 0x3F) + 3
            size = min(size, dest_length - di)
            offset = source[si] + (source[si+1] << 8); si += 2
            for _ in range(size):
                dest[di] = dest[offset]; offset += 1; di += 1
        else:
            size = cmd & 0x3F
            size = min(size, dest_length - di)
            for _ in range(size):
                dest[di] = source[si]; si += 1; di += 1
    return bytes(dest[:di]), si

def format40_decode_xor(dst, src):
    """dst: bytearray (modified in place, XORed). src: bytes."""
    si = 0
    di = 0
    n = len(dst)
    slen = len(src)
    while si < slen:
        cmd = src[si]; si += 1
        if cmd == 0:
            count = src[si]; si += 1
            val = src[si]; si += 1
            for _ in range(count):
                if di < n: dst[di] ^= val
                di += 1
        elif (cmd & 0x80) == 0:
            count = cmd
            for _ in range(count):
                if di < n: dst[di] ^= src[si]
                si += 1; di += 1
        elif cmd != 0x80:
            di += (cmd & 0x7F)
        else:
            cmd2 = src[si] + (src[si+1] << 8); si += 2
            if cmd2 == 0:
                break
            if (cmd2 & 0x8000) == 0:
                di += cmd2
            elif (cmd2 & 0x4000) == 0:
                count = cmd2 & 0x3FFF
                for _ in range(count):
                    if di < n: dst[di] ^= src[si]
                    si += 1; di += 1
            else:
                count = cmd2 & 0x3FFF
                val = src[si]; si += 1
                for _ in range(count):
                    if di < n: dst[di] ^= val
                    di += 1
    return di

class WSA:
    def __init__(self, path):
        data = open(path, 'rb').read()
        self.data = data
        frames, width, height, reqBuf, hasPalette = struct.unpack_from('<HHHHH', data, 0)
        header_len = 10
        offsets = struct.unpack_from(f'<{frames+2}I', data, header_len)
        if offsets[0] != header_len + 8 + 4*frames and offsets[1] != header_len + 8 + 4*frames:
            header_len = 8
            hasPalette = 0
            frames, width, height, reqBuf = struct.unpack_from('<HHHH', data, 0)
            offsets = struct.unpack_from(f'<{frames+2}I', data, 8)
        self.frames = frames
        self.width = width
        self.height = height
        self.reqBuf = reqBuf
        self.hasPalette = hasPalette
        self.header_len = header_len
        self.offsets = offsets
        pal_off = header_len + 4*(frames+2)
        if hasPalette:
            self.palette = data[pal_off:pal_off+0x300]
            pal_off += 0x300
        else:
            self.palette = None

    def decode_all_frames(self):
        buf = bytearray(self.width * self.height)
        out = []
        for i in range(self.frames):
            start, end = self.offsets[i], self.offsets[i+1]
            chunk = self.data[start:end]
            decoded40, used = format80_decode(chunk, self.reqBuf)
            format40_decode_xor(buf, decoded40)
            out.append(bytes(buf))
        return out

if __name__ == '__main__':
    w = WSA(sys.argv[1])
    print(f"frames={w.frames} {w.width}x{w.height} reqBuf={w.reqBuf} hasPalette={w.hasPalette} header_len={w.header_len}")
