#!/usr/bin/env python3
"""Compile a TTF/OTF font into the compact XFO1 outline format.

The runtime deliberately has no TTF parser.  This tool uses fontTools offline
and writes only cmap, metrics, and flattened glyph outlines.  Example:

    python Tools/xfont_compile.py Arial.ttf Arial.xfo --text "Hello 123"

Use --text (or --unicodes) to keep an embedded font small.  Without either
option all cmap entries are emitted.
"""

import argparse
import struct
import sys
from pathlib import Path


HEADER_SIZE = 36
CMAP_ENTRY_SIZE = 8
GLYPH_ENTRY_SIZE = 20
MOVE = 0
LINE = 1
QUAD = 2
CUBIC = 3
CLOSE = 4


def delta(value, previous):
    value = int(round(value))
    d = value - previous
    if d < -32768 or d > 32767:
        raise ValueError("outline delta does not fit int16: %d" % d)
    return d, value


def put_delta(out, x, y, current):
    dx, x = delta(x, current[0])
    dy, y = delta(y, current[1])
    out.extend(struct.pack("<hh", dx, dy))
    current[0], current[1] = x, y


class RecordingXfoPen:
    def __init__(self, glyph_set=None):
        self.commands = bytearray()
        self.current = [0, 0]
        self.command_count = 0
        self.glyph_set = glyph_set

    def moveTo(self, pt):
        self.commands.append(MOVE)
        self.command_count += 1
        put_delta(self.commands, pt[0], pt[1], self.current)

    def lineTo(self, *points):
        for pt in points:
            self.commands.append(LINE)
            self.command_count += 1
            put_delta(self.commands, pt[0], pt[1], self.current)

    def qCurveTo(self, *points):
        # TrueType may omit the final on-curve point (None) and may provide
        # several consecutive off-curve points.  Insert implied midpoints.
        points = list(points)
        if points and points[-1] is None:
            if len(points) < 2:
                raise ValueError("quadratic command has no off-curve point")
            # A contour with no explicit on-curve point starts at the implied
            # midpoint between its last and first off-curve points.  This is
            # the same convention used by fontTools BasePen.qCurveTo().
            first = points[0]
            last = points[-2]
            implied = ((first[0] + last[0]) / 2.0,
                       (first[1] + last[1]) / 2.0)
            self.moveTo(implied)
            points[-1] = implied
        if not points:
            return
        controls = points[:-1]
        end = points[-1]
        if not controls:
            self.lineTo(end)
            return
        for index, control in enumerate(controls):
            if index + 1 < len(controls):
                next_control = controls[index + 1]
                target = ((control[0] + next_control[0]) / 2.0,
                          (control[1] + next_control[1]) / 2.0)
            else:
                target = end
            self.commands.append(QUAD)
            self.command_count += 1
            cx = int(round(control[0]))
            cy = int(round(control[1]))
            tx = int(round(target[0]))
            ty = int(round(target[1]))
            cdx, _ = delta(cx, self.current[0])
            cdy, _ = delta(cy, self.current[1])
            edx, _ = delta(tx, cx)
            edy, _ = delta(ty, cy)
            if any(v < -32768 or v > 32767 for v in (cdx, cdy, edx, edy)):
                raise ValueError("quadratic delta does not fit int16")
            self.commands.extend(struct.pack("<hhhh", cdx, cdy, edx, edy))
            self.current[0], self.current[1] = tx, ty

    def curveTo(self, *points):
        points = list(points)
        if len(points) % 3:
            raise ValueError("invalid cubic command")
        for index in range(0, len(points), 3):
            c1, c2, end = points[index:index + 3]
            self.commands.append(CUBIC)
            self.command_count += 1
            c1x, c1y = int(round(c1[0])), int(round(c1[1]))
            c2x, c2y = int(round(c2[0])), int(round(c2[1]))
            ex, ey = int(round(end[0])), int(round(end[1]))
            values = [c1x - self.current[0], c1y - self.current[1],
                      c2x - c1x, c2y - c1y, ex - c2x, ey - c2y]
            if any(v < -32768 or v > 32767 for v in values):
                raise ValueError("cubic delta does not fit int16")
            self.commands.extend(struct.pack("<hhhhhh", *values))
            self.current[0], self.current[1] = ex, ey

    def closePath(self):
        self.commands.append(CLOSE)
        self.command_count += 1

    def endPath(self):
        self.closePath()

    def addComponent(self, glyphName, transformation):
        # Decompose composite glyphs while applying each component's affine
        # transform.  XFO1 intentionally stores only primitive commands.
        from fontTools.pens.transformPen import TransformPen

        if self.glyph_set is None or glyphName not in self.glyph_set:
            raise ValueError("missing composite glyph: %s" % glyphName)
        self.glyph_set[glyphName].draw(TransformPen(self, transformation))


def glyph_record(glyph_set, glyph_name):
    from fontTools.pens.boundsPen import BoundsPen

    bounds = BoundsPen(glyph_set)
    glyph_set[glyph_name].draw(bounds)
    if bounds.bounds is None:
        x_min = y_min = x_max = y_max = 0
    else:
        x_min, y_min, x_max, y_max = [int(round(v)) for v in bounds.bounds]
    pen = RecordingXfoPen(glyph_set)
    # Resolve composite glyphs offline so the runtime format only needs the
    # primitive outline commands and remains independent of fontTools.
    glyph_set[glyph_name].draw(pen)
    return pen.commands, (x_min, y_min, x_max, y_max), pen.command_count


def parse_unicodes(value):
    result = set()
    for item in value.split(","):
        item = item.strip()
        if not item:
            continue
        if "-" in item:
            start, end = item.split("-", 1)
            result.update(range(int(start, 0), int(end, 0) + 1))
        else:
            result.add(int(item, 0))
    return result


def compile_font(source, output, text=None, unicodes=None):
    try:
        from fontTools.ttLib import TTFont
    except ImportError as exc:
        raise RuntimeError("fontTools is required: python -m pip install fonttools") from exc

    font = TTFont(str(source), recalcBBoxes=False, recalcTimestamp=False)
    glyph_set = font.getGlyphSet()
    cmap = {}
    for table in font["cmap"].tables:
        cmap.update(table.cmap)
    selected = None
    if text is not None:
        selected = {ord(char) for char in text}
    if unicodes:
        selected = parse_unicodes(unicodes) if selected is None else selected | parse_unicodes(unicodes)
    if selected is not None:
        cmap = {cp: name for cp, name in cmap.items() if cp in selected}
    if not cmap:
        raise ValueError("the selected text/unicode set has no cmap entries")

    names = [".notdef"]
    for name in cmap.values():
        if name not in names:
            names.append(name)
    if len(cmap) > 65535:
        raise ValueError("too many cmap entries for XFO1")
    if len(names) > 65535:
        raise ValueError("too many glyphs for XFO1")
    glyph_ids = {name: index for index, name in enumerate(names)}
    hmtx = font["hmtx"].metrics
    units_per_em = int(font["head"].unitsPerEm)
    ascent = int(font["hhea"].ascent)
    descent = int(font["hhea"].descent)
    line_gap = int(font["hhea"].lineGap)
    if not 1 <= units_per_em <= 65535:
        raise ValueError("unitsPerEm does not fit uint16")
    if not 0 <= ascent <= 32767 or descent < -32767:
        raise ValueError("font ascent/descent does not fit XFO1")
    if not 0 <= line_gap <= 32767:
        raise ValueError("font lineGap does not fit XFO1")
    descent_metric = max(0, -descent)
    glyph_data = []
    command_stream = bytearray()
    for name in names:
        commands, bounds, command_count = glyph_record(glyph_set, name)
        command_offset = len(command_stream)
        command_stream.extend(commands)
        advance = int(round(hmtx.get(name, (0, 0))[0]))
        if not 0 <= advance <= 0xFFFFFFFF:
            raise ValueError("glyph advance does not fit uint32")
        if any(value < -32768 or value > 32767 for value in bounds):
            raise ValueError("glyph bounds do not fit int16")
        glyph_data.append((advance, *bounds, command_offset, command_count))

    cmap_entries = bytearray()
    for cp, name in sorted(cmap.items()):
        cmap_entries.extend(struct.pack("<IHH", cp, glyph_ids[name], 0))
    glyph_entries = bytearray()
    cmap_offset = HEADER_SIZE
    glyph_offset = cmap_offset + len(cmap_entries)
    command_offset = glyph_offset + len(glyph_data) * GLYPH_ENTRY_SIZE
    for advance, x_min, y_min, x_max, y_max, offset, count in glyph_data:
        if count > 65535:
            raise ValueError("glyph has too many outline commands")
        glyph_entries.extend(struct.pack("<IhhhhIHH", advance, x_min, y_min,
                                         x_max, y_max, offset, count, 0))

    head = struct.pack("<4sHHHhhhHHIIII", b"XFO1", 1, 0,
                       units_per_em, ascent, descent_metric, line_gap,
                       len(cmap_entries) // CMAP_ENTRY_SIZE, len(glyph_data),
                       cmap_offset, glyph_offset,
                       command_offset, len(command_stream))
    if len(head) != HEADER_SIZE:
        raise AssertionError("XFO1 header size mismatch")
    output.write_bytes(head + cmap_entries + glyph_entries + command_stream)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="input TTF/OTF")
    parser.add_argument("output", type=Path, help="output XFO1 file")
    parser.add_argument("--text", help="only include codepoints appearing in this text")
    parser.add_argument("--unicodes", help="comma-separated codepoints/ranges, e.g. 0x20-0x7e,0x4e00")
    args = parser.parse_args(argv)
    try:
        compile_font(args.source, args.output, args.text, args.unicodes)
    except (OSError, ValueError, RuntimeError) as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
