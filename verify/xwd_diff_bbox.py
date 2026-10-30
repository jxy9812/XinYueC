import struct
import sys


def load(path):
    with open(path, "rb") as fh:
        data = fh.read()
    header_size = struct.unpack_from(">I", data, 0)[0]
    width = struct.unpack_from(">I", data, 4 * 4)[0]
    height = struct.unpack_from(">I", data, 5 * 4)[0]
    bpl = struct.unpack_from(">I", data, 12 * 4)[0]
    ncolors = struct.unpack_from(">I", data, 19 * 4)[0]
    return data, header_size + ncolors * 12, bpl, width, height


a_path, b_path = sys.argv[1], sys.argv[2]
skips = []
for arg in sys.argv[3:]:
    sx, sy, sw, sh = (int(v) for v in arg.split(","))
    skips.append((sx, sy, sw, sh))


def skipped(x, y):
    for sx, sy, sw, sh in skips:
        if sx <= x < sx + sw and sy <= y < sy + sh:
            return True
    return False


a, a_off, a_bpl, w, h = load(a_path)
b, b_off, b_bpl, _, _ = load(b_path)

minx, miny, maxx, maxy = w, h, -1, -1
rows = {}
count = 0
for y in range(h):
    ra = a_off + y * a_bpl
    rb = b_off + y * b_bpl
    for x in range(w):
        if skipped(x, y):
            continue
        pa = a[ra + x * 4: ra + x * 4 + 4]
        pb = b[rb + x * 4: rb + x * 4 + 4]
        if pa != pb:
            count += 1
            rows[y] = rows.get(y, 0) + 1
            if x < minx: minx = x
            if x > maxx: maxx = x
            if y < miny: miny = y
            if y > maxy: maxy = y

print("total diff px =", count)
print("bbox = x %d..%d  y %d..%d" % (minx, maxx, miny, maxy))
ys = sorted(rows)
print("distinct rows =", len(ys))
if ys:
    # 找出连续的差异行簇
    clusters = []
    start = prev = ys[0]
    for y in ys[1:]:
        if y - prev > 3:
            clusters.append((start, prev))
            start = y
        prev = y
    clusters.append((start, prev))
    for c in clusters:
        px = sum(rows[y] for y in ys if c[0] <= y <= c[1])
        print("  cluster y %d..%d : %d px" % (c[0], c[1], px))
