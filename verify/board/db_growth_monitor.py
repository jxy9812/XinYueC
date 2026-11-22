#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""verify/board/db_growth_monitor.py —— 长压测期间的「堆 vs 库体量」对照采样。

用法: python db_growth_monitor.py <服务端堆日志> <db路径> <输出CSV> [间隔秒]

每行记: ts, heap_bytes, sessions, db_bytes, wal_bytes, page_count, freelist_count

用途：区分两种解释——
  ① 「框架按 INSERT 泄漏」   ⇒ 堆随**会话/INSERT 数**线性涨，与库体量无关；
  ② 「SQLite 页缓存长到上限」 ⇒ 堆随**库页数**涨，到 cache_size 上限后**平台化**。
两者都能解释「落盘开就涨、落盘关就平」，必须把堆和 page_count 并排看。
"""
from __future__ import print_function

import os
import re
import sqlite3
import sys
import time

HEAP = re.compile(r"\[heap\]\s+sys=(\d+)\s+sessions=(\d+)\s+t=(\d+)")


def heap_samples(path):
    """返回 [(t_ms, heap, sessions)]（按日志顺序，最后一条为当前值）。"""
    out = []
    if not path or not os.path.exists(path):
        return out
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = HEAP.search(line)
            if m:
                out.append((int(m.group(3)), int(m.group(1)), int(m.group(2))))
    return out


def db_stats(db):
    size = os.path.getsize(db) if os.path.exists(db) else 0
    wal = db + "-wal"
    walsz = os.path.getsize(wal) if os.path.exists(wal) else 0
    pages = freelist = 0
    try:
        con = sqlite3.connect("file:%s?mode=ro" % db.replace("\\", "/"), uri=True)
        cur = con.execute("PRAGMA page_count")
        pages = cur.fetchone()[0]
        cur = con.execute("PRAGMA freelist_count")
        freelist = cur.fetchone()[0]
        con.close()
    except Exception:                                    # noqa: BLE001
        pass
    return size, walsz, pages, freelist


def main():
    heap_log = sys.argv[1]
    db = sys.argv[2]
    out = sys.argv[3]
    interval = float(sys.argv[4]) if len(sys.argv) > 4 else 30.0
    if not os.path.exists(out):
        with open(out, "w", encoding="utf-8") as f:
            f.write("ts,heap,sessions,db_bytes,wal_bytes,page_count,freelist_count\n")
    while True:
        samples = heap_samples(heap_log)
        if samples:
            t, h, s = samples[-1]
            size, walsz, pages, free = db_stats(db)
            with open(out, "a", encoding="utf-8") as f:
                f.write("%d,%d,%d,%d,%d,%d,%d\n" % (t, h, s, size, walsz, pages, free))
            print("[dbmon] heap=%d sessions=%d db=%d wal=%d pages=%d free=%d"
                  % (h, s, size, walsz, pages, free), flush=True)
        time.sleep(interval)


if __name__ == "__main__":
    main()
