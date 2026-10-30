import re, statistics, sys

path = sys.argv[1] if len(sys.argv) > 1 else r'C:/Users/Administrator/AppData/Local/Temp/mfin.log'
L = []
for line in open(path, encoding='utf-8', errors='ignore'):
    if 'MBTRACE' in line:
        L.append(line.strip())


def ts(l):
    m = re.search(r'MBTRACE (\d+)\.(\d+)', l)
    return float(m.group(1)) + float(m.group(2)) / 1e6 if m else None


sends, recvs = [], []
for l in L:
    t = ts(l)
    if t is None:
        continue
    if 'SEND' in l:
        sends.append((t, l))
    elif 'RECV' in l:
        recvs.append((t, l))

print('SEND=%d RECV=%d' % (len(sends), len(recvs)))

gaps = sorted(((sends[i][0] - sends[i - 1][0]) * 1000.0, i) for i in range(1, len(sends)))
ds = [g for g, _ in gaps]
if ds:
    sds = sorted(ds)
    print('SEND间隔ms: p50=%.1f p90=%.1f p99=%.1f max=%.1f' % (
        statistics.median(ds), sds[int(len(ds) * .9)], sds[int(len(ds) * .99)], max(ds)))
    big = [g for g in ds if g > 150]
    print('gap>150ms: %d / %d = %.1f%%' % (len(big), len(ds), 100.0 * len(big) / len(ds)))
    print('top10:', [round(g, 1) for g in ds[-10:]])

dts = []
for t, l in recvs:
    m = re.search(r'dt=([0-9.]+)', l)
    if m:
        dts.append(float(m.group(1)) * 1000.0)
if dts:
    sds = sorted(dts)
    print('dt(ms): p50=%.1f p90=%.1f p99=%.1f max=%.1f' % (
        statistics.median(dts), sds[int(len(dts) * .9)], sds[int(len(dts) * .99)], max(dts)))
    print('dt>50ms: %d / %d = %.1f%%' % (
        sum(1 for d in dts if d > 50), len(dts),
        100.0 * sum(1 for d in dts if d > 50) / len(dts)))
