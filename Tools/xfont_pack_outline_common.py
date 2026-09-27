#!/usr/bin/env python3
"""Pack the split builtin outline fonts into a single external-load file.

XFontOutlineCommon 的外挂加载按家族名找同名文件（XFONT_EXTERNAL_OUTLINE_FONT_DIR
下的 XFontOutlineCommon.xfo / .inc），而内嵌形态是 Latin + Cjk 两个分体 .inc。
本工具把两个 XFO1 合并为一个，供外挂模式使用：

    python Tools/xfont_pack_outline_common.py

输出：
    Library/XFont/XFontOutlineCommon.inc  （十六进制 C 数组文本，运行时可解析）
    Library/XFont/XFontOutlineCommon.xfo  （同数据二进制，加载更快）

合并规则：度量必须一致；cmap 不得重叠；glyph 记录与命令流按
Cjk + Latin 顺序拼接，Latin 的 glyph id 平移、命令相对偏移加 Cjk 流长。
"""
import argparse
import re
import struct
import sys
from pathlib import Path

HEADER_SIZE = 36
CMAP_ENTRY = 8
GLYPH_ENTRY = 20

REPO = Path(__file__).resolve().parent.parent
FONT_DIR = REPO / "Library" / "XFont"
DEFAULT_CJK = FONT_DIR / "XFontOutlineCommonCjk.inc"
DEFAULT_LATIN = FONT_DIR / "XFontOutlineCommonLatin.inc"
DEFAULT_OUT_STEM = FONT_DIR / "XFontOutlineCommon"


def load_inc(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", text))


def format_inc(blob: bytes) -> bytes:
    lines = []
    for i in range(0, len(blob), 16):
        items = "".join("0x%02X, " % b for b in blob[i:i + 16]).rstrip(" ")
        lines.append("    " + items)
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def parse_xfo(d: bytes):
    if d[:4] != b"XFO1" or struct.unpack_from("<H", d, 4)[0] != 1:
        raise SystemExit("not an XFO1 blob")
    upm, asc, des, lg = struct.unpack_from("<Hhhh", d, 8)
    cc, gc = struct.unpack_from("<HH", d, 16)
    cm, go, co, clen = struct.unpack_from("<IIII", d, 20)
    if co + clen != len(d):
        raise SystemExit("command stream length mismatch")
    cmap = [struct.unpack_from("<IHH", d, cm + i * CMAP_ENTRY)
            for i in range(cc)]
    glyphs = [struct.unpack_from("<IhhhhIHH", d, go + i * GLYPH_ENTRY)
              for i in range(gc)]
    cmds = d[co:]
    return {
        "upm": upm, "asc": asc, "des": des, "lg": lg,
        "cmap": cmap, "glyphs": glyphs, "cmds": cmds,
    }


def merge(cjk, latin, notdef_gid=0):
    for key, label in (("upm", "unitsPerEm"), ("asc", "ascent"),
                       ("des", "descent"), ("lg", "lineGap")):
        if cjk[key] != latin[key]:
            raise SystemExit("metric mismatch: %s %d != %d"
                             % (label, cjk[key], latin[key]))
    cjk_cps = {cp for cp, _, _ in cjk["cmap"]}
    latin_cps = {cp for cp, _, _ in latin["cmap"]}
    both = cjk_cps & latin_cps
    if both:
        raise SystemExit("cmap overlap: %r" % sorted(both)[:5])

    cjk_gc = len(cjk["glyphs"])
    # Latin glyph ids shifted after the Cjk table (keep every Latin glyph,
    # including its .notdef, so command offsets stay a pure concatenation).
    glyphs = list(cjk["glyphs"])
    for adv, x0, y0, x1, y1, rel, cnt, pad in latin["glyphs"]:
        glyphs.append((adv, x0, y0, x1, y1,
                       rel + len(cjk["cmds"]), cnt, pad))
    cmds = cjk["cmds"] + latin["cmds"]
    cmap = [(cp, gid, res) for cp, gid, res in cjk["cmap"]]
    cmap += [(cp, gid + cjk_gc, res) for cp, gid, res in latin["cmap"]]
    cmap.sort(key=lambda e: e[0])
    return {
        "upm": cjk["upm"], "asc": cjk["asc"], "des": cjk["des"],
        "lg": cjk["lg"], "cmap": cmap, "glyphs": glyphs, "cmds": cmds,
        "notdef_gid": notdef_gid,
    }


def build_blob(font) -> bytes:
    cc = len(font["cmap"])
    gc = len(font["glyphs"])
    cmap_blob = bytearray()
    for cp, gid, res in font["cmap"]:
        cmap_blob.extend(struct.pack("<IHH", cp, gid, res))
    glyph_blob = bytearray()
    for adv, x0, y0, x1, y1, rel, cnt, pad in font["glyphs"]:
        glyph_blob.extend(struct.pack("<IhhhhIHH", adv, x0, y0, x1, y1,
                                      rel, cnt, pad))
    cmap_off = HEADER_SIZE
    glyph_off = cmap_off + len(cmap_blob)
    cmd_off = glyph_off + len(glyph_blob)
    head = struct.pack("<4sHHHhhhHHIIII", b"XFO1", 1, 0, font["upm"],
                       font["asc"], font["des"], font["lg"], cc, gc,
                       cmap_off, glyph_off, cmd_off, len(font["cmds"]))
    if len(head) != HEADER_SIZE:
        raise AssertionError("header size")
    return bytes(head) + bytes(cmap_blob) + bytes(glyph_blob) + font["cmds"]


def pack(cjk_path=DEFAULT_CJK, latin_path=DEFAULT_LATIN,
         out_stem=DEFAULT_OUT_STEM):
    cjk = parse_xfo(load_inc(cjk_path))
    latin = parse_xfo(load_inc(latin_path))
    blob = build_blob(merge(cjk, latin))
    # structural round-trip: header must accept the blob like the runtime
    parsed = parse_xfo(blob)
    assert len(parsed["cmap"]) == len(cjk["cmap"]) + len(latin["cmap"])
    assert {cp for cp, _, _ in parsed["cmap"]} == \
           {cp for cp, _, _ in cjk["cmap"]} | {cp for cp, _, _ in latin["cmap"]}
    inc_path = Path(str(out_stem) + ".inc")
    xfo_path = Path(str(out_stem) + ".xfo")
    inc_path.write_bytes(format_inc(blob))
    xfo_path.write_bytes(blob)
    print("packed %s (%d cmap / %d glyphs, %d bytes binary)" %
          (inc_path.name, len(parsed["cmap"]), len(parsed["glyphs"]),
           len(blob)))
    print("packed %s" % xfo_path.name)
    return inc_path, xfo_path


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--cjk", type=Path, default=DEFAULT_CJK)
    ap.add_argument("--latin", type=Path, default=DEFAULT_LATIN)
    ap.add_argument("--out-stem", type=Path, default=DEFAULT_OUT_STEM,
                    help="output path without extension")
    args = ap.parse_args(argv)
    pack(args.cjk, args.latin, args.out_stem)
    return 0


if __name__ == "__main__":
    sys.exit(main())
