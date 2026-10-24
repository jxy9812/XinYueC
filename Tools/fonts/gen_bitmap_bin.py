#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 LVGL8 风格外挂位图字库（.bin），供 XGui 的 XFont.c 外挂位图路径使用。

产出格式与 Src/XData/XFont/XFont.c 的解析器逐字段对齐：

    [u32 len][4B 'head'][40B head 数据]        len 含 8 字节节头
    [u32 len][4B 'cmap'][u32 count][count×16B 表][稀疏偏移表...]
    [u32 len][4B 'loca'][u32 count][count×u32 偏移]
    [u32 len][4B 'glyf'][4B 保留][字形记录...]

head 关键字段（绝对偏移）：
    14 u16 font_size   16 u16 ascent   18 i16 descent(负)
    34 u8  loca 索引位宽（0=2 字节，1=4 字节）——**注意是文件绝对偏移**，
       不是 loca 段内偏移
    36 u8  advance 单位标志（0 → adv_w 再乘 16；1 → adv_w 已是 1/16 px）
    37 u8  bpp         38 u8 xy_bits   39 u8 wh_bits
    40 u8  advance_bits  41 u8 compression(0=裸位图)

cmap 子表项（16 字节）：
    0 u32 data_offset（相对 cmap 段首）  4 u32 range_start
    8 u16 range_length                   10 u16 glyph_id_start
    12 u16 entries（稀疏表长度）         14 u8 format
    format: 0=Format0Full 1=SparseFull 2=Format0Tiny(连续) 3=SparseTiny(稀疏)

字形记录（位流，MSB first，逐字节对齐）：
    advance_bits(u) | xy_bits(i) ofs_x | xy_bits(i) ofs_y |
    wh_bits(u) box_w | wh_bits(u) box_h | box_h 行 × box_w 像素 × bpp

定位约定（本仓库 XPainter.c 实际用法）：
    originX = pen_x + ofs_x                  （像素）
    originY = baselineY - box_h - ofs_y       （像素）
    baselineY = line_top + ascent
  故 ofs_y = ascent - 墨迹下边缘（画布坐标，基线在 ascent 处）
"""
import argparse
import os
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.stderr.write('需要 Pillow：pip install Pillow\n')
    raise

# 字符集分段：每段 (起始码点, 结束码点, 说明)
CHARSET_RANGES = [
    (0x0020, 0x007E, 'ASCII'),
    (0x00A0, 0x00FF, 'Latin-1'),
    (0x0100, 0x017F, 'Latin Extended-A'),
    (0x2000, 0x206F, 'General Punctuation'),
    (0x3000, 0x303F, 'CJK Symbols and Punctuation'),
    (0x4E00, 0x9FFF, 'CJK Unified Ideographs'),
    (0xFF00, 0xFFEF, 'Halfwidth and Fullwidth Forms'),
]


def collect_charset(font_path, extra_ranges=None):
    """返回 (有序码点列表, 段落统计)。只收字体里真有的字形。"""
    from fontTools.ttLib import TTFont
    tt = TTFont(font_path, fontNumber=0, lazy=True)
    cmap = tt.getBestCmap()
    ranges = list(CHARSET_RANGES)
    if extra_ranges:
        ranges += [tuple(r) for r in extra_ranges]
    chars = []
    stats = []
    for lo, hi, name in ranges:
        got = [c for c in range(lo, hi + 1) if c in cmap]
        chars.extend(got)
        stats.append((name, lo, hi, len(got)))
    tt.close()
    return sorted(chars), stats


class BitWriter:
    def __init__(self):
        self.buf = bytearray()
        self.acc = 0
        self.nbits = 0

    def write(self, value, bits):
        for i in range(bits - 1, -1, -1):
            self.acc = (self.acc << 1) | ((value >> i) & 1)
            self.nbits += 1
            if self.nbits == 8:
                self.buf.append(self.acc)
                self.acc = 0
                self.nbits = 0

    def align(self):
        if self.nbits:
            self.buf.append(self.acc << (8 - self.nbits))
            self.acc = 0
            self.nbits = 0

    def bytes(self):
        return bytes(self.buf)


def render_glyph(font, ss_font, ch, size, ascent, ss, bpp_levels,
                 canvas=None, draw=None, lut=None, bbox_lut=None):
    """渲染单字，返回 (adv_px, ofs_x, ofs_y, box_w, box_h, pixels) 或 None。

    pixels 为 list[list[int]]，取值 0..(2^bpp-1)。
    量化与包围盒都走 PIL 的 LUT/point，避免逐像素 Python 循环
    （7000 字 × 每字上万像素，纯 Python 循环要跑几十分钟）。
    """
    ss_size = size * ss
    ss_ascent = ascent * ss
    pen = size * ss                    # 最终坐标：pen 在 size 处
    base = ss_ascent                   # 最终坐标：基线在 ascent 处
    W = size * 4 * ss                  # 画布按最终像素的 4 倍留边
    H = W
    if canvas is None:
        canvas = Image.new('L', (W, H), 0)
        draw = ImageDraw.Draw(canvas)
    if lut is None:
        lut = [min(bpp_levels - 1, (v * (bpp_levels - 1) + 127) // 255)
               for v in range(256)]
    if bbox_lut is None:
        # 包围盒阈值：极淡的抗锯齿边缘（<24/255）不算墨迹
        bbox_lut = [255 if v >= 24 else 0 for v in range(256)]
    draw.rectangle([0, 0, W, H], fill=0)
    draw.text((pen, base), ch, font=ss_font, fill=255, anchor='ls')
    # 超采样后 box 降采样 → 抗锯齿灰度
    small = canvas.resize((W // ss, H // ss), Image.BOX)
    bbox = small.point(bbox_lut).getbbox()
    adv = font.getlength(ch)
    if bbox is None:
        # 空白字符（如空格）：只保留 advance，无位图
        return (adv, 0, 0, 0, 0, [])
    x0, y0, x1, y1 = bbox
    box_w = x1 - x0
    box_h = y1 - y0
    ofs_x = x0 - size
    ofs_y = ascent - y1
    quant = small.crop((x0, y0, x1, y1)).point(lut)
    flat = list(quant.getdata())
    pixels = [flat[i * box_w:(i + 1) * box_w] for i in range(box_h)]
    return (adv, ofs_x, ofs_y, box_w, box_h, pixels)


def encode_glyph(adv_px, ofs_x, ofs_y, box_w, box_h, pixels,
                 bpp, xy_bits, wh_bits, advance_bits):
    """位流编码一个字形记录。"""
    w = BitWriter()
    adv = int(round(adv_px * 16.0))
    max_adv = (1 << advance_bits) - 1
    if adv > max_adv:
        adv = max_adv
    w.write(adv, advance_bits)
    w.write(ofs_x & ((1 << xy_bits) - 1), xy_bits)
    w.write(ofs_y & ((1 << xy_bits) - 1), xy_bits)
    w.write(box_w, wh_bits)
    w.write(box_h, wh_bits)
    if box_w and box_h:
        for row in pixels:
            for v in row:
                w.write(v & ((1 << bpp) - 1), bpp)
    w.align()
    return w.bytes()


def build_bin(chars, glyphs, size, ascent, descent, bpp,
              xy_bits=8, wh_bits=8, advance_bits=12):
    """组装完整 .bin 字节流。glyphs: {codepoint: (adv, ofs_x, ofs_y, w, h, pixels)}"""
    # ---- 码点 → 字形 id 布局 ----
    #  id 0          : .notdef
    #  id 1..95      : ASCII 0x20..0x7E（连续段，format 2）
    #  id 96..       : 其余字符（稀疏段，format 3，按码点升序）
    ascii_cps = [c for c in chars if 0x20 <= c <= 0x7E]
    other_cps = [c for c in chars if not (0x20 <= c <= 0x7E)]
    gid = {}
    for i, cp in enumerate(ascii_cps):
        gid[cp] = 1 + i
    sparse_base = 1 + len(ascii_cps)
    for i, cp in enumerate(other_cps):
        gid[cp] = sparse_base + i
    total_glyphs = sparse_base + len(other_cps)

    # ---- glyf 段 ----
    # 段头 [u32 len][4B 'glyf'] 共 8 字节；首条字形记录紧跟其后，
    # 故记录偏移从 8 起算（解析器要求 glyphOffset >= 8）。
    glyf_records = bytearray()
    offsets = []
    for g in range(total_glyphs):
        offsets.append(8 + len(glyf_records))
        if g == 0:
            rec = BitWriter()
            rec.write(0, advance_bits)
            rec.write(0, xy_bits)
            rec.write(0, xy_bits)
            rec.write(0, wh_bits)
            rec.write(0, wh_bits)
            rec.align()
            glyf_records += rec.bytes()
            continue
        cp = ascii_cps[g - 1] if g <= len(ascii_cps) else other_cps[g - 1 - len(ascii_cps)]
        info = glyphs.get(cp)
        if info is None:
            rec = BitWriter()
            rec.write(0, advance_bits)
            rec.write(0, xy_bits)
            rec.write(0, xy_bits)
            rec.write(0, wh_bits)
            rec.write(0, wh_bits)
            rec.align()
            glyf_records += rec.bytes()
            continue
        glyf_records += encode_glyph(info[0], info[1], info[2], info[3], info[4], info[5],
                                    bpp, xy_bits, wh_bits, advance_bits)
    glyf = bytearray()
    glyf += b'\x00\x00\x00\x00' + b'glyf'
    glyf += glyf_records

    # ---- cmap 段 ----
    # 子表 0：ASCII 连续段；子表 1：其余稀疏段
    sparse_rel = [cp - other_cps[0] for cp in other_cps] if other_cps else []
    tables = []
    if ascii_cps:
        tables.append(dict(data_off=0, start=ascii_cps[0], length=len(ascii_cps),
                           glyph_start=1, entries=0, fmt=2))
    if other_cps:
        tables.append(dict(data_off=0, start=other_cps[0],
                           length=other_cps[-1] - other_cps[0] + 1,
                           glyph_start=sparse_base, entries=len(other_cps), fmt=3))
    list_start = 12 + 16 * len(tables)
    tbl_bytes = bytearray()
    for t in tables:
        # 16 字节子表项：data_off(4) range_start(4) range_len(2) glyph_start(2)
        #                entries(2) format(1) + 1 字节填充
        tbl_bytes += struct.pack('<IIHHHB', t['data_off'], t['start'], t['length'],
                                 t['glyph_start'], t['entries'], t['fmt'])
        tbl_bytes += b'\x00'
    list_bytes = bytearray()
    for t in tables:
        if t['fmt'] == 3:
            t['data_off'] = list_start + len(list_bytes)
            # 稀疏表的数据偏移必须在表描述符写盘前确定 → 回填
            for i, tt in enumerate(tables):
                if tt is t:
                    struct.pack_into('<I', tbl_bytes, i * 16, t['data_off'])
            for rel in sparse_rel:
                list_bytes += struct.pack('<H', rel)
    cmap = bytearray()
    cmap += b'\x00\x00\x00\x00' + b'cmap'
    cmap += struct.pack('<I', len(tables))
    cmap += tbl_bytes
    cmap += list_bytes

    # ---- loca 段 ----
    loca = bytearray()
    loca += b'\x00\x00\x00\x00' + b'loca'
    loca += struct.pack('<I', total_glyphs)
    for off in offsets:
        loca += struct.pack('<I', off)

    # ---- head 段 ----
    head = bytearray(48)
    struct.pack_into('<I', head, 0, 48)
    head[4:8] = b'head'
    struct.pack_into('<I', head, 8, 1)            # 版本
    struct.pack_into('<H', head, 12, 3)
    struct.pack_into('<H', head, 14, size)        # font_size
    struct.pack_into('<H', head, 16, ascent)
    struct.pack_into('<h', head, 18, descent)
    struct.pack_into('<H', head, 20, ascent)      # line_height
    struct.pack_into('<h', head, 22, descent)
    struct.pack_into('<H', head, 24, 0)
    struct.pack_into('<h', head, 26, descent)      # base_line
    struct.pack_into('<H', head, 28, ascent - 1)  # underline_position
    struct.pack_into('<H', head, 30, size * 16)   # 默认 advance（1/16 px）
    struct.pack_into('<H', head, 32, 16)
    head[34] = 1                                 # loca 索引 = 4 字节
    head[36] = 1                                 # adv 已是 1/16 px
    head[37] = bpp
    head[38] = xy_bits
    head[39] = wh_bits
    head[40] = advance_bits
    head[41] = 0                                 # compression = 裸位图

    out = bytearray()
    for sec, label in ((head, 'head'), (cmap, 'cmap'), (loca, 'loca'), (glyf, 'glyf')):
        struct.pack_into('<I', sec, 0, len(sec))
        sec[4:8] = label.encode('ascii')
        out += sec
    return bytes(out), gid, total_glyphs


def main():
    ap = argparse.ArgumentParser(description='生成 XGui 外挂位图字库 (.bin)')
    ap.add_argument('--otf', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--size', type=int, required=True)
    ap.add_argument('--ascent', type=int, required=True)
    ap.add_argument('--descent', type=int, required=True)
    ap.add_argument('--bpp', type=int, default=2)
    ap.add_argument('--supersample', type=int, default=4)
    ap.add_argument('--report', default=None)
    args = ap.parse_args()

    chars, stats = collect_charset(args.otf)
    sys.stderr.write('字符集：%d 个码点\n' % len(chars))
    for name, lo, hi, n in stats:
        sys.stderr.write('  %-32s U+%04X..U+%04X  %d 字\n' % (name, lo, hi, n))

    font = ImageFont.truetype(args.otf, args.size)
    ss_font = ImageFont.truetype(args.otf, args.size * args.supersample)
    levels = 1 << args.bpp

    # 复用画布 / LUT：逐字新建 Image 与逐像素 Python 循环都太慢
    W = H = args.size * 4 * args.supersample
    canvas = Image.new('L', (W, H), 0)
    draw = ImageDraw.Draw(canvas)
    lut = [min(levels - 1, (v * (levels - 1) + 127) // 255) for v in range(256)]
    bbox_lut = [255 if v >= 24 else 0 for v in range(256)]

    glyphs = {}
    empty = 0
    for i, cp in enumerate(chars):
        ch = chr(cp)
        g = render_glyph(font, ss_font, ch, args.size, args.ascent,
                         args.supersample, levels, canvas, draw, lut, bbox_lut)
        if g is None:
            continue
        if g[3] == 0 or g[4] == 0:
            empty += 1
        glyphs[cp] = g
        if (i + 1) % 1000 == 0:
            sys.stderr.write('  已栅格化 %d/%d\n' % (i + 1, len(chars)))
            sys.stderr.flush()

    data, gid, total = build_bin(chars, glyphs, args.size, args.ascent,
                                  args.descent, args.bpp)
    with open(args.out, 'wb') as f:
        f.write(data)
    sys.stderr.write('写出 %s：%d 字节（%d 字形，含 %d 个空白）\n'
                     % (args.out, len(data), total, empty))

    if args.report:
        with open(args.report, 'w', encoding='utf-8') as f:
            f.write('源字体: %s\n' % args.otf)
            f.write('输出: %s\n' % args.out)
            f.write('size=%d ascent=%d descent=%d bpp=%d 字形数=%d 空白=%d\n'
                    % (args.size, args.ascent, args.descent, args.bpp, total, empty))
            f.write('码点数=%d 文件=%d 字节\n' % (len(chars), len(data)))
            for name, lo, hi, n in stats:
                f.write('  %-32s U+%04X..U+%04X %d\n' % (name, lo, hi, n))


if __name__ == '__main__':
    main()
