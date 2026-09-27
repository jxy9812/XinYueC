#!/usr/bin/env python3
"""Merge missing GB2312 Han glyphs into the built-in XFO1 CJK font.

The built-in library (Library/XFont/XFontOutlineCommonCjk.inc) is a C byte
array of XFO1 data included directly by XFontOutlineCommon.c (compiled in,
not read at runtime).  This tool appends glyphs for every GB2312 Han
character (zones 0xB0-0xF7, 6763 chars) that the existing data lacks, so
the coverage can be topped up without regenerating existing glyph bytes.

Source font must match the existing XFO1 metrics (unitsPerEm / ascent /
descent / lineGap) and design; the library was originally produced from
Noto Sans SC Regular (verified byte-identical glyph outlines).  Example:

    python Tools/xfont_merge_gb2312.py

Requires fontTools (python -m pip install fonttools).
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
DEFAULT_INC = REPO / "Library/XFont/XFontOutlineCommonCjk.inc"
DEFAULT_FONT = Path(r"C:\Windows\Fonts\Noto Sans SC (TrueType).otf")


def load_inc(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", text))


def format_inc(blob: bytes) -> bytes:
    lines = []
    for i in range(0, len(blob), 16):
        items = "".join("0x%02X, " % b for b in blob[i:i + 16]).rstrip(" ")
        lines.append("    " + items)
    return ("\r\n".join(lines) + "\r\n").encode("ascii")


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def s16(d, o):
    return struct.unpack_from("<h", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def gb2312_han() -> set:
    out = set()
    for hi in range(0xB0, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                out.add(ord(ch))
    return out


def compile_font_source(path: Path, target_cps, old_upm):
    """Compile glyph records for target codepoints from a TTF/OTF source."""
    sys.path.insert(0, str(REPO / "Tools"))
    from fontTools.ttLib import TTFont
    import xfont_compile as xc

    font = TTFont(str(path), recalcBBoxes=False, recalcTimestamp=False)
    upm = int(font["head"].unitsPerEm)
    if upm != old_upm:
        raise SystemExit("source font unitsPerEm %d != existing %d"
                         % (upm, old_upm))
    gset = font.getGlyphSet()
    cmap_t = {}
    for t in font["cmap"].tables:
        cmap_t.update(t.cmap)
    hmtx = font["hmtx"].metrics

    records = []
    for cp in sorted(target_cps):
        name = cmap_t.get(cp)
        if name is None:
            records.append((cp, None))
            continue
        commands, bounds, count = xc.glyph_record(gset, name)
        advance = int(round(hmtx[name][0]))
        records.append((cp, (advance, bounds, count, bytes(commands))))
    return records


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--inc", type=Path, default=DEFAULT_INC,
                    help="existing XFO1 .inc file (updated in place)")
    ap.add_argument("--font", type=Path, default=DEFAULT_FONT,
                    help="TTF/OTF source with matching metrics")
    ap.add_argument("--dry-run", action="store_true",
                    help="report missing chars without writing")
    args = ap.parse_args(argv)

    old = load_inc(args.inc)
    if old[:4] != b"XFO1" or u16(old, 4) != 1:
        raise SystemExit("not an XFO1 blob: %s" % args.inc)
    upm, ascent, descent = u16(old, 8), s16(old, 10), s16(old, 12)
    linegap = s16(old, 14)
    cc, gc = u16(old, 16), u16(old, 18)
    cm, go, co = u32(old, 20), u32(old, 24), u32(old, 28)
    if co + u32(old, 32) != len(old):
        raise SystemExit("command stream length mismatch")
    old_cmap = old[cm:go]
    old_glyphs = old[go:co]
    old_cmds = old[co:]
    existing = {u32(old_cmap, i * CMAP_ENTRY)
                for i in range(cc)}
    if [u32(old_cmap, i * CMAP_ENTRY) for i in range(cc)] != \
            sorted(u32(old_cmap, i * CMAP_ENTRY) for i in range(cc)):
        raise SystemExit("existing cmap is not sorted")

    missing = sorted(gb2312_han() - existing)
    print("existing: %d cmap / %d glyphs; missing GB2312 Han: %d"
          % (cc, gc, len(missing)))
    if not missing:
        print("nothing to do")
        return 0
    if args.dry_run:
        print(" ".join("%s(U+%04X)" % (chr(c), c) for c in missing[:40]))
        return 0

    records = compile_font_source(args.font, missing, upm)
    absent = [cp for cp, r in records if r is None]
    if absent:
        raise SystemExit("source font lacks %d chars: %s"
                         % (len(absent),
                            " ".join(chr(c) for c in absent[:20])))

    new_cmds = bytearray()
    new_glyphs = bytearray()
    pairs = [(u32(old_cmap, i * CMAP_ENTRY),
              u16(old_cmap, i * CMAP_ENTRY + 4)) for i in range(cc)]
    for i, (cp, rec) in enumerate(records):
        advance, bounds, count, commands = rec
        rel = len(old_cmds) + len(new_cmds)
        new_cmds.extend(commands)
        new_glyphs.extend(struct.pack("<IhhhhIHH", advance, *bounds,
                                      rel, count, 0))
        pairs.append((cp, gc + i))
    pairs.sort(key=lambda p: p[0])

    cmap_blob = bytearray()
    for cp, gid in pairs:
        cmap_blob.extend(struct.pack("<IHH", cp, gid, 0))
    new_cc, new_gc = len(pairs), gc + len(records)
    new_go = HEADER_SIZE + new_cc * CMAP_ENTRY
    new_co = new_go + new_gc * GLYPH_ENTRY
    stream = bytes(old_cmds) + bytes(new_cmds)
    head = struct.pack("<4sHHHhhhHHIIII", b"XFO1", 1, 0, upm, ascent,
                       descent, linegap, new_cc, new_gc, HEADER_SIZE,
                       new_go, new_co, len(stream))
    blob = head + bytes(cmap_blob) + old_glyphs + bytes(new_glyphs) + stream

    # invariants: header offsets consistent, old glyph records / command
    # stream are exact prefixes, cmap sorted, every target char resolvable
    assert u32(blob, 20) == HEADER_SIZE
    assert u32(blob, 24) == HEADER_SIZE + new_cc * CMAP_ENTRY
    assert u32(blob, 28) == u32(blob, 24) + new_gc * GLYPH_ENTRY
    assert u32(blob, 32) == len(stream)
    assert blob[u32(blob, 24):u32(blob, 24) + len(old_glyphs)] == old_glyphs
    assert blob[u32(blob, 28):u32(blob, 28) + len(old_cmds)] == old_cmds
    cps = [u32(blob, HEADER_SIZE + i * CMAP_ENTRY) for i in range(new_cc)]
    assert cps == sorted(cps) and len(set(cps)) == len(cps)
    for cp in gb2312_han():
        lo, hi = 0, new_cc - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            v = u32(blob, HEADER_SIZE + mid * CMAP_ENTRY)
            if v == cp:
                break
            lo, hi = (mid + 1, hi) if v < cp else (lo, mid - 1)
        else:
            raise SystemExit("U+%04X still missing" % cp)

    args.inc.write_bytes(format_inc(blob))
    print("wrote %s: %d cmap / %d glyphs (+%d), %d bytes binary"
          % (args.inc, new_cc, new_gc, len(records), len(blob)))

    # 分体数据变了，重新打包外挂合并文件（XFontOutlineCommon.inc/.xfo），
    # 否则运行时外挂家族名文件与分体字库脱节。
    import xfont_pack_outline_common
    xfont_pack_outline_common.pack()
    return 0


if __name__ == "__main__":
    sys.exit(main())
