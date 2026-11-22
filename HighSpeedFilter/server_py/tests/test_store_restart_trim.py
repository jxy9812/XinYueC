# -*- coding: utf-8 -*-
"""回归锁：close→open_for 重建后的「残留维护唤醒」不得删历史行。

[2026-11-19 实锅] `_stop_maintenance` 无条件 `wake.set()`（去阻塞在等的
老线程），而 `_maint_wake` 是**跨代共用**的 Event：close() → open_for()
重建（服务端每次启动、远端改库路径、停用再启用落盘都会走）时，新维护
线程第一拍直接被上一代留下的 wake 放行 ⇒ `woke=True` 且 rotate=False ⇒
走 `_ring_trim` 的「首轮必删」⇒ 库里有 ≥4000 行历史时**每次启动都静默
删掉最旧的 ~1000 行**，日志还谎称「已达上限」。E2E 实测：一次重启丢
999 行，三次重开丢 2997 行。

[修法] `_start_maintenance` 起线程前 clear wake；`_stop_maintenance`
只在确有活线程在等时才 set。本锁两道：
  · A：4500 行的库 close→open_for 重建，行数必须分毫不动（旧代码在此
    删 999~1000 行）；
  · B：真超限（把上限压到极小）时裁剪仍会动手——门没修死。

[怎么跑]
    python -I tests/test_store_restart_trim.py     # 独立跑
    pytest tests/test_store_restart_trim.py -q     # pytest 收集
"""

import os
import shutil
import sys
import tempfile
import time

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv.store import Store            # noqa: E402

PASS = 0
FAIL = 0

#: 行数必须 > RING_TRIM_CHUNK_ROWS(1000) 且 > 4000（旧 bug 的触发门槛：
#: 25% 比例取块后 ≥1000 行才会吃满一个删除块）。
SEED_ROWS = 4500
#: 给后台维护线程留出「旧代码会在这里动手」的时间窗。
SETTLE_S = 1.2


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


def _seed(store, rows=SEED_ROWS):
    """经公开入口插 N 行历史（ts 铺过去 3 天，绝不与「现在」重叠）。"""
    now = int(time.time() * 1000)
    base = now - 180 * 1000 - rows * 20 * 1000
    for i in range(rows):
        store.insert_history({
            "ts": base + i * 20 * 1000,
            "mode": "手动", "vfd_state": "停机",
            "freq_hz": 25.0, "current_a": 12.3, "rpm": 720.0,
            "turb1": 2.29, "turb2": 0.045,
            "flow": 3.6, "flow_unit": "m³/h",
            "flow_total": 1.0, "total_unit": "m³",
            "press_mpa": 0.4, "temp": 26.5, "hum": 55.0,
            "di": 0, "do": 0,
        })


def test_reopen_does_not_trim():
    """A：4500 行的库 close→open_for 重建，行数分毫不动。"""
    base = tempfile.mkdtemp(prefix="hsf_trim_regress_")
    try:
        s1 = Store(base)
        # [max_kb=0 = 不限] 闸门在合法路径上永不触发 ⇒ 之后少掉的行只能
        # 来自「残留唤醒的假维护」，判据干净。
        s1.configure(max_kb=0)
        s1.open_for("")
        _seed(s1)
        n1 = s1.row_count("history")
        check("A1 预置 %d 行" % SEED_ROWS, n1 == SEED_ROWS, "n=%d" % n1)
        s1.close()
        # 同一代 Store 重建（open_for 内部先 close 再起维护线程）。
        s1.configure(max_kb=0)
        s1.open_for("")
        time.sleep(SETTLE_S)
        n2 = s1.row_count("history")
        check("A2 close→open_for 重建后行数不变（残留唤醒不得删行）",
              n2 == n1, "before=%d after=%d" % (n1, n2))
        s1.close()

        # 再走一遍「新建 Store 实例」的路径（= 服务端进程重启的等价形态）。
        s2 = Store(base)
        s2.configure(max_kb=0)
        s2.open_for("")
        time.sleep(SETTLE_S)
        n3 = s2.row_count("history")
        check("A3 新实例 open_for（等价进程重启）行数不变",
              n3 == n1, "before=%d after=%d" % (n1, n3))
        s2.close()
    finally:
        shutil.rmtree(base, ignore_errors=True)


def test_over_limit_still_trims():
    """B：真超限时裁剪仍会动手（门没修死）。"""
    base = tempfile.mkdtemp(prefix="hsf_trim_over_")
    try:
        s = Store(base)
        # 上限压到 64KB、塞几千行 ⇒ 闸门真实触发 ⇒ 裁剪必须删到水位以下。
        s.configure(max_kb=64, full_policy="ring")
        s.open_for("")
        _seed(s, SEED_ROWS)
        time.sleep(SETTLE_S + 1.0)          # 等后台裁剪分块跑完
        n = s.row_count("history")
        check("B1 真超限仍会循环裁剪（行数 < 预置数）",
              0 < n < SEED_ROWS, "rows=%d" % n)
        check("B2 裁剪后占用收敛（dropped 计数 > 0）",
              getattr(s, "_dropped_rows", 0) > 0,
              "dropped=%d" % getattr(s, "_dropped_rows", 0))
        s.close()
    finally:
        shutil.rmtree(base, ignore_errors=True)


def main():
    test_reopen_does_not_trim()
    test_over_limit_still_trims()
    print("\n===== 重启裁剪回归：%d failed（PASS %d / FAIL %d）====="
          % (FAIL, PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
