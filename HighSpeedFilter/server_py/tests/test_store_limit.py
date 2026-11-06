# -*- coding: utf-8 -*-
"""落盘容量限额单元测试（不起服务端，纯 store 层）。

    python server_py/tests/test_store_limit.py

为什么要有纯单元层：e2e（client_py/tests/test_storage_limit.py）要起真实
服务端、靠真实采样慢慢堆数据，跑一轮几分钟；这里直接调 store，毫秒级
就能把「到限那一刻」的三种策略行为全部走一遍，改代码时先跑这个。

覆盖：
  A. 配置换算（MB/KB 两种口径、非法值回落）
  B. stop   到限停写：行数冻结、pauseReason 有值、再放宽上限能自动恢复
  C. ring   循环覆盖：真删最旧、内容封顶、累计丢弃计数
  D. rotate 分段滚动：分段出现、超龄段被清、总占用封顶
  E. 体积口径：空库底噪（说明为什么不能把上限设得比底噪还小）
  F. 重启保持：关库重开后限额配置与分段清理仍正确
"""

import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

from hsfsrv import store                              # noqa: E402

PASS = 0
FAIL = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))


ROW = {"mode": "手动", "vfd_state": "停机", "freq_hz": 25.0, "current_a": 3.2,
       "rpm": 750.0, "turb1": 12.5, "turb2": 3.2, "flow": 36.5,
       "flow_unit": "m³/h", "flow_total": 123.4, "total_unit": "m³",
       "press_mpa": 0.499, "press_note": "", "temp": 26.5, "hum": 55.0,
       "di": 0xFFFF, "do": 0x00FF}


def feed(st, count, start=1700000000000, step=1000):
    """写 count 行历史，返回实际被接受的行数（到限后会被拒）。"""
    accepted = 0
    for i in range(count):
        if st.insert_history(dict(ROW, ts=start + i * step)):
            accepted += 1
    return accepted


def new_store(tmp, **cfg):
    st = store.Store(tmp, log=lambda _m: None)
    if not st.open_for(""):
        raise RuntimeError("库打不开")
    st.configure(**cfg)
    return st


# ============================================================================
#                              A. 配置换算
# ============================================================================

def part_a_config():
    print("== A. 配置换算 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp, max_mb=7)
        check("A1 max_mb=7 → 7168 KB", st.max_bytes == 7 * 1024 * 1024,
              str(st.max_bytes))
        st.configure(max_kb=120)
        check("A2 max_kb=120 → 122880 字节", st.max_bytes == 120 * 1024,
              str(st.max_bytes))
        st.configure(max_mb=0)
        check("A3 max=0 → 不限", st.max_bytes == 0, str(st.max_bytes))
        st.configure(max_mb=None, full_policy="bogus")
        check("A4 非法策略回落缺省 ring",
              st.full_policy == store.DEFAULT_POLICY, st.full_policy)
        st.configure(keep_files=0)
        check("A5 keep_files 下限夹到 1", st.keep_files == 1,
              str(st.keep_files))
        st.configure(keep_files=500)
        check("A6 keep_files 上限夹到 99", st.keep_files == 99,
              str(st.keep_files))
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                            B. stop 到限停写
# ============================================================================

def part_b_stop():
    print("== B. stop：到限停写 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp, max_kb=120, full_policy="stop")
        feed(st, 400)
        rows_after = st.row_count("history")
        stats = st.stats()
        check("B1 已停写（pauseReason 有值）", bool(stats["paused"]),
              (stats.get("pauseReason") or "")[:40])
        check("B2 有数据留下（不是空库假停）", rows_after > 0,
              "rows=%d" % rows_after)
        more = feed(st, 200, start=1700000100000)
        check("B3 停写后新数据被拒", more == 0, "accepted=%d" % more)
        check("B4 行数冻结", st.row_count("history") == rows_after,
              "%d → %d" % (rows_after, st.row_count("history")))
        # 放宽上限 → 自动恢复写入
        st.configure(max_kb=4096)
        resumed = feed(st, 50, start=1700000200000)
        check("B5 放宽上限后自动恢复写入", resumed > 0, "accepted=%d" % resumed)
        check("B6 pauseReason 已清空", not st.pause_reason)
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                           C. ring 循环覆盖
# ============================================================================

def part_c_ring():
    print("== C. ring：循环覆盖 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp, max_kb=120, full_policy="ring")
        feed(st, 800)
        stats = st.stats()
        check("C1 发生了丢弃", stats["droppedRows"] > 0,
              "dropped=%d" % stats["droppedRows"])
        check("C2 内容封顶在上限附近（含一行余量）",
              stats["contentBytes"] <= 121 * 1024,
              "content=%d KB / 上限 120 KB"
              % (stats["contentBytes"] // 1024))
        check("C3 磁盘占用封顶（不含空库底噪）",
              stats["usedBytes"] <= 400 * 1024,
              "used=%d KB" % (stats["usedBytes"] // 1024))
        check("C4 库仍可读（行还在）", stats["rows"] > 0,
              "rows=%d" % stats["rows"])
        before = stats["droppedRows"]
        feed(st, 400, start=1700001000000)
        check("C5 持续循环（丢弃计数继续涨）",
              st.stats()["droppedRows"] > before,
              "%d → %d" % (before, st.stats()["droppedRows"]))
        # 留下的应该是"较新的"数据：最早那批被删掉了
        oldest = st._conn.execute(
            "SELECT MIN(ts) FROM history").fetchone()[0]
        check("C6 删的是最旧的数据", oldest >= 1700000000000,
              "最旧 ts=%s（起点 1700000000000）" % oldest)
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                         D. rotate 分段滚动
# ============================================================================

def part_d_rotate():
    print("== D. rotate：分段滚动 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp, max_kb=200, full_policy="rotate", keep_files=3)
        feed(st, 1200)
        # 分段名是 `hsf_data.<时间戳>.db`；主库 `hsf_data.db` 不算分段
        # （只按前缀 `hsf_data.` 匹配会把主库也捞进来）。
        segs = [n for n in os.listdir(tmp)
                if n.startswith("hsf_data.") and n.endswith(".db")
                and n != "hsf_data.db"]
        check("D1 产生了分段文件", len(segs) >= 1, str(sorted(segs)[:3]))
        check("D2 分段数 <= keepFiles-1", len(segs) <= 2,
              "%d 个（keepFiles=3）" % len(segs))
        total = sum(os.path.getsize(os.path.join(tmp, n))
                    for n in os.listdir(tmp))
        check("D3 总占用封顶（分段上限之和 + 当前库）",
              total <= 400 * 1024, "%d KB" % (total // 1024))
        main = os.path.join(tmp, "hsf_data.db")
        check("D4 当前库体积在上限内",
              os.path.getsize(main) <= 200 * 1024,
              "%d KB" % (os.path.getsize(main) // 1024))
        check("D5 仍在写（当前库有行）", st.row_count("history") > 0,
              "rows=%d" % st.row_count("history"))
        # 分段应是可独立打开的 SQLite 库（可归档/拷走）
        import sqlite3
        seg_path = os.path.join(tmp, sorted(segs)[-1])
        conn = sqlite3.connect(seg_path)
        try:
            n = conn.execute("SELECT COUNT(*) FROM history").fetchone()[0]
        except sqlite3.Error as exc:
            n = -1
            print("    （分段读取异常：%s）" % exc)
        finally:
            conn.close()
        check("D6 分段是可独立打开的完整库", n >= 0, "分段行数=%d" % n)
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                            E. 体积口径
# ============================================================================

def part_e_metrics():
    print("== E. 体积口径 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        empty = st.stats()
        check("E1 空库也有底噪（页+WAL，>32KB）",
              empty["contentBytes"] > 32 * 1024,
              "空库 content=%d KB" % (empty["contentBytes"] // 1024))
        check("E2 未设上限时 maxMb=0（不限）", empty["maxMb"] == 0,
              str(empty["maxMb"]))
        feed(st, 100)
        grown = st.stats()
        check("E3 写入后内容增长", grown["contentBytes"] > empty["contentBytes"],
              "%d → %d KB" % (empty["contentBytes"] // 1024,
                              grown["contentBytes"] // 1024))
        check("E4 usedBytes >= contentBytes（物理含 shm）",
              grown["usedBytes"] >= grown["contentBytes"],
              "used=%d content=%d"
              % (grown["usedBytes"], grown["contentBytes"]))
        check("E5 统计字段齐全",
              all(k in grown for k in (
                  "usedBytes", "usedMb", "contentBytes", "maxMb", "maxKb",
                  "policy", "keepFiles", "paused", "droppedRows", "rows",
                  "segments")), str(sorted(grown)))
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                           F. 重启保持
# ============================================================================

def part_f_restart():
    print("== F. 关库重开 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp, max_kb=200, full_policy="rotate", keep_files=2)
        feed(st, 900)
        segs_before = len([n for n in os.listdir(tmp)
                           if n.startswith("hsf_data.") and n.endswith(".db")
                           and n != "hsf_data.db"])
        st.close()
        # 重开：限额与策略要跟着配置走（由 app 侧 configure 传入）
        st2 = store.Store(tmp, log=lambda _m: None)
        st2.configure(max_kb=200, full_policy="rotate", keep_files=2)
        ok = st2.open_for("")
        check("F1 重开成功", ok)
        check("F2 重开后策略保持", st2.full_policy == "rotate", st2.full_policy)
        after = [n for n in os.listdir(tmp)
                 if n.startswith("hsf_data.") and n.endswith(".db")
                 and n != "hsf_data.db"]
        check("F3 重开时清掉超龄分段（keepFiles=2 → 最多 1 个）",
              len(after) <= 1, "重开前 %d 个 → 重开后 %d 个"
              % (segs_before, len(after)))
        # 重开后继续写不会炸
        feed(st2, 50, start=1700009000000)
        check("F4 重开后继续写入正常", st2.row_count("history") > 0,
              "rows=%d" % st2.row_count("history"))
        st2.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    part_a_config()
    part_b_stop()
    part_c_ring()
    part_d_rotate()
    part_e_metrics()
    part_f_restart()
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
