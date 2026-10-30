#!/usr/bin/env python3
"""XWD -> PNG 纯标准库转换（RK3568 板子 xwd 截图用，无 PIL/ImageMagick）。

用法: python3 xwd2png.py <in.xwd> <out.png>

XWD 格式（本板 xwd 输出，字段全为大端 u32）：
  固定头 25 个 u32（100 字节）；header_size=[0]（107，含 7 字节窗口名
  "xwdump\\0" 位于偏移 100，无长度字）、width=[4]、height=[5]、
  bytes_per_line=[12]、ncolors=[19]。
  调色板：ncolors × 12 字节，紧随头部之后。
  像素数据起点 = header_size + ncolors*12，32bpp ZPixmap、LSBFirst，
  每像素 4 字节 B,G,R,X。
"""
import struct
import sys
import zlib


def read_words(data, count, offset=0):
    return struct.unpack_from(">%dI" % count, data, offset)


def convert(src, dst):
    with open(src, "rb") as fh:
        data = fh.read()
    (header_size,) = struct.unpack_from(">I", data, 0)
    (width,) = struct.unpack_from(">I", data, 4 * 4)
    (height,) = struct.unpack_from(">I", data, 5 * 4)
    bytes_per_line = struct.unpack_from(">I", data, 12 * 4)[0]
    ncolors = struct.unpack_from(">I", data, 19 * 4)[0]
    pix_off = header_size + ncolors * 12
    need = pix_off + bytes_per_line * height
    if len(data) < need:
        raise SystemExit("XWD 截断: 需要 %d 字节, 实际 %d" % (need, len(data)))

    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter type 0 (None)
        base = pix_off + y * bytes_per_line
        row = data[base:base + width * 4]
        # B,G,R,X -> R,G,B
        out = bytearray(width * 3)
        for x in range(width):
            s = x * 4
            d = x * 3
            out[d] = row[s + 2]
            out[d + 1] = row[s + 1]
            out[d + 2] = row[s]
        rows += out

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    with open(dst, "wb") as fh:
        fh.write(png)
    return width, height


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("用法: xwd2png.py <in.xwd> <out.png>")
    w, h = convert(sys.argv[1], sys.argv[2])
    print("wrote %s (%dx%d)" % (sys.argv[2], w, h))
