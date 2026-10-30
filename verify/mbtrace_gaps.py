import re, sys
from collections import Counter

path = r'C:/Users/Administrator/AppData/Local/Temp/mfin.log'
L = [l.strip() for l in open(path, encoding='utf-8', errors='ignore') if 'MBTRACE' in l]


def ts(l):
    m = re.search(r'MBTRACE (\d+)\.(\d+)', l)
    return float(m.group(1)) + float(m.group(2)) / 1e6 if m else None


ev = [(ts(l), l) for l in L]
ev = [(t, l) for t, l in ev if t is not None]
sends = [(t, l) for t, l in ev if ' SEND ' in l]

hist = Counter()
big = []
for i in range(1, len(sends)):
    g = (sends[i][0] - sends[i - 1][0]) * 1000.0
    if g <= 150:
        hist['<=150'] += 1
    elif g <= 200:
        hist['150-200'] += 1
    elif g <= 300:
        hist['200-300'] += 1
    elif g <= 600:
        hist['300-600'] += 1
    elif g <= 950:
        hist['600-950'] += 1
    else:
        hist['>=950'] += 1
    if g > 150:
        big.append((g, i))
print('SEND 间隔分布:', dict(hist))

# 找第一处 >=950ms 的间隔，打印上下文
for g, i in big:
    if g < 950:
        continue
    print('\n--- 上下文 (gap=%.1fms) ---' % g)
    lo = max(0, i - 4)
    hi = min(len(ev), i + 4)
    for t, l in ev[lo:hi]:
        print('   ', l)
    break

# SKIP 与 dt 关系
skips = [l for t, l in ev if 'SKIP' in l]
print('\nSKIP 行数:', len(skips))
dts = [float(re.search(r'dt=([0-9.]+)', l).group(1)) * 1000 for t, l in ev if 'dt=' in l]
slow = [d for d in dts if d > 50]
print('dt>50ms: %d (%.1f%%)' % (len(slow), 100.0 * len(slow) / len(dts)))
print('dt 慢样本(前20):', sorted(slow)[:20])
