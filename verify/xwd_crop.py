#!/usr/bin/env python3
"""XWD 裁剪 + 最近邻放大 → PNG（无 PIL）。用于逐像素差异目视核对。
用法: xwd_crop.py <in.xwd> <out.png> <x> <y> <w> <h> [scale]
"""
import struct
import sys
import zlib


def load(path):
    with open(path, "rb") as fh:
        data = fh.read()
    (header_size,) = struct.unpack_from(">I", data, 0)
    (width,) = struct.unpack_from(">I", data, 4 * 4)
    (height,) = struct.unpack_from(">I", data, 5 * 4)
    bpl = struct.unpack_from(">I", data, 12 * 4)[0]
    ncolors = struct.unpack_from(">I", data, 19 * 4)[0]
    off = header_size + ncolors * 12
    return data, off, width, height, bpl


def main():
    src, dst = sys.argv[1], sys.argv[2]
    x0, y0, cw, ch = (int(v) for v in sys.argv[3:7])
    scale = int(sys.argv[7]) if len(sys.argv) > 7 else 1
    data, off, W, H, bpl = load(src)
    rows = bytearray()
    for y in range(y0, min(y0 + ch, H)):
        line = bytearray()
        base = off + y * bpl
        for x in range(x0, min(x0 + cw, W)):
            p = base + x * 4
            line += bytes((data[p + 2], data[p + 1], data[p]))
        line = bytes(line)
        for _ in range(scale):
            rows.append(0)
            scaled = bytearray()
            for x in range(0, len(line), 3):
                scaled += line[x:x + 3] * scale
            rows += scaled

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    ow = min(cw, W - x0) * scale
    oh = min(ch, H - y0) * scale
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", ow, oh, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    with open(dst, "wb") as fh:
        fh.write(png)
    print("wrote %s (%dx%d)" % (dst, ow, oh))


main()
