# -*- coding: utf-8 -*-
"""历史分页查询的存储层单元测试（不起服务端，纯 store）。

    python server_py/tests/test_store_page.py

为什么单独一套：分页的正确性全在 SQL 这一层（offset/limit 的边界、
窗口锚定、范围过滤与总数同口径），而这些在 e2e 里要堆够数据才看得
出来，跑一轮几十秒。这里毫秒级把边界全走一遍，改代码先跑这个。

覆盖：
  A. 分页基本语义（行数 / offset / 顺序 / 无重叠无遗漏）
  B. 窗口锚定（beforeTs 固定后，新入库不改变窗口内总数与页内容）
  C. 时间范围过滤（总数与页内容同口径）
  D. 边界（offset 越界 / limit 0 与负数 / 空库 / 未开库）
  E. 列序（HISTORY_COLUMNS 与 SELECT 一致，防"列错位"）
  F. 有效墙钟边界 / lo+hi+before 组合
  K. 关键字下推：显示文本同口径 / 列限定 / %_\\ 转义 / NULL→"--"
     / 时间 AND / 翻页不重不漏 / count 与 query 同口径
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


T0 = 1750000000000
STEP = 5000


def make_row(i, **over):
    row = {"ts": T0 + i * STEP, "mode": "手动", "vfd_state": "停机",
           "freq_hz": 25.0, "current_a": 3.2, "rpm": 750.0,
           "turb1": 12.5, "turb2": 3.2, "flow": 36.5, "flow_unit": "m³/h",
           "flow_total": 123.4, "total_unit": "m³", "press_mpa": 0.499,
           "press_note": "", "temp": 26.5, "hum": 55.0,
           "di": 0xFFFF, "do": 0x00FF}
    row.update(over)
    return row


def new_store(tmp):
    st = store.Store(tmp, log=lambda _m: None)
    if not st.open_for(""):
        raise RuntimeError("库打不开")
    return st


# ============================================================================
#                        A. 分页基本语义
# ============================================================================

def part_a_basic():
    print("== A. 分页基本语义 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        for i in range(95):
            st.insert_history(make_row(i))

        rows, total = st.query_history_page(offset=0, limit=20)
        check("A1 第 1 页 20 行", len(rows) == 20, "rows=%d" % len(rows))
        check("A2 总数 95", total == 95, "total=%d" % total)
        check("A3 页内时间正序（旧→新）",
              [r[0] for r in rows] == sorted(r[0] for r in rows))
        check("A4 第 1 页取的是**最新**那段",
              rows[-1][0] == T0 + 94 * STEP,
              "末行 ts=%d 期望 %d" % (rows[-1][0], T0 + 94 * STEP))

        page2, _ = st.query_history_page(offset=20, limit=20)
        ids1 = set(r[0] for r in rows)
        ids2 = set(r[0] for r in page2)
        check("A5 相邻两页无重叠", ids1.isdisjoint(ids2),
              "重叠 %d 行" % len(ids1 & ids2))
        check("A6 相邻两页连续（第2页末行 = 第1页首行 - 1 步）",
              page2[-1][0] == rows[0][0] - STEP,
              "%d vs %d" % (page2[-1][0], rows[0][0] - STEP))

        # 全部翻完：并集 = 全表、无重复
        seen, got = [], 0
        offset = 0
        while True:
            chunk, _t = st.query_history_page(offset=offset, limit=20)
            if not chunk:
                break
            got += len(chunk)
            seen.extend(r[0] for r in chunk)
            offset += 20
        check("A7 翻完所有页共 95 行", got == 95, "got=%d" % got)
        check("A8 无重复行", len(set(seen)) == len(seen),
              "unique=%d/%d" % (len(set(seen)), len(seen)))
        check("A9 无遗漏（并集 = 库内全部 ts）",
              set(seen) == set(T0 + i * STEP for i in range(95)))

        # 末页不满
        last, _t = st.query_history_page(offset=80, limit=20)
        check("A10 末页只剩 15 行", len(last) == 15, "rows=%d" % len(last))
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                          B. 窗口锚定
# ============================================================================

def part_b_window():
    print("== B. 窗口锚定（beforeTs）==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        for i in range(40):
            st.insert_history(make_row(i))
        anchor = T0 + 39 * STEP          # 客户端首屏的"现在"
        _rows, total0 = st.query_history_page(offset=0, limit=10,
                                              before_ms=anchor)
        check("B1 锚点内总数 40", total0 == 40, "total=%d" % total0)

        # 服务端又采了 5 行（模拟分页过程中持续入库）
        for i in range(40, 45):
            st.insert_history(make_row(i))

        rows_after, total_after = st.query_history_page(
            offset=0, limit=10, before_ms=anchor)
        check("B2 新入库不改变窗口内总数（锚点生效）", total_after == 40,
              "%d → %d" % (total0, total_after))
        check("B3 锚点内第 1 页内容不变（最新那行还是旧锚点时刻）",
              rows_after[-1][0] == anchor,
              "末行 ts=%d 期望 %d" % (rows_after[-1][0], anchor))

        # 不传锚点（服务端按 now）：总数应包含新行
        _r2, total_now = st.query_history_page(offset=0, limit=10)
        check("B4 不传锚点时按全库计数（45）", total_now == 45,
              "total=%d" % total_now)

        # 翻第 2 页仍用同一锚点 → 与第 1 页无重叠
        page2, _t = st.query_history_page(offset=10, limit=10,
                                          before_ms=anchor)
        check("B5 翻页沿用锚点：第 2 页不混入新行",
              all(r[0] <= anchor for r in page2),
              "最大 ts=%d 锚点=%d" % (max(r[0] for r in page2), anchor))
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                         C. 时间范围过滤
# ============================================================================

def part_c_range():
    print("== C. 时间范围过滤 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        for i in range(60):
            st.insert_history(make_row(i))
        lo = T0 + 10 * STEP
        hi = T0 + 29 * STEP              # 含两端共 20 行

        rows, total = st.query_history_page(offset=0, limit=100,
                                            lo_ms=lo, hi_ms=hi)
        check("C1 范围总数 20", total == 20, "total=%d" % total)
        check("C2 范围内行数 = 总数", len(rows) == 20, "rows=%d" % len(rows))
        check("C3 首行 = 下界", rows[0][0] == lo, "ts=%d" % rows[0][0])
        check("C4 末行 = 上界", rows[-1][0] == hi, "ts=%d" % rows[-1][0])

        # 范围内分页：两页拼起来 20 行、无重叠
        p1, t1 = st.query_history_page(offset=0, limit=12, lo_ms=lo, hi_ms=hi)
        p2, t2 = st.query_history_page(offset=12, limit=12,
                                       lo_ms=lo, hi_ms=hi)
        check("C5 范围分页第 1 页 12 行", len(p1) == 12, "rows=%d" % len(p1))
        check("C6 范围分页第 2 页 8 行", len(p2) == 8, "rows=%d" % len(p2))
        check("C7 范围分页两页总计 20 行",
              len(set(r[0] for r in p1 + p2)) == 20,
              "unique=%d" % len(set(r[0] for r in p1 + p2)))
        check("C8 count_history 与分页 total 同口径",
              st.count_history(lo_ms=lo, hi_ms=hi) == total,
              "count=%d total=%d" % (st.count_history(lo_ms=lo, hi_ms=hi),
                                     total))

        # 只有下界
        _r, t_lo = st.query_history_page(offset=0, limit=5, lo_ms=lo)
        check("C9 只给下界：剩 50 行", t_lo == 50, "total=%d" % t_lo)
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                             D. 边界
# ============================================================================

def part_d_edges():
    print("== D. 边界 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        # 空库
        rows, total = st.query_history_page(offset=0, limit=10)
        check("D1 空库返回空页", rows == [], str(rows))
        check("D2 空库总数 0", total == 0, "total=%d" % total)
        check("D3 空库 count_history = 0", st.count_history() == 0)

        for i in range(5):
            st.insert_history(make_row(i))
        rows, total = st.query_history_page(offset=999, limit=10)
        check("D4 offset 越界返回空页（不报错）", rows == [], str(rows))
        check("D5 越界时总数仍正确", total == 5, "total=%d" % total)

        rows, _t = st.query_history_page(offset=0, limit=0)
        check("D6 limit=0 返回空页（夹取在上层做）", rows == [])

        # 负数 offset 在 SQL 里等同 0（上层也 clamp，双保险）
        rows, _t = st.query_history_page(offset=-5, limit=3)
        check("D7 负 offset 按 0 处理", len(rows) == 3, "rows=%d" % len(rows))
        st.close()

        # 未开库
        st2 = store.Store(tmp, log=lambda _m: None)
        rows, total = st2.query_history_page(offset=0, limit=10)
        check("D8 未开库返回空页", rows == [] and total == 0)
        check("D9 未开库 count_history = 0", st2.count_history() == 0)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                             E. 列序
# ============================================================================

def part_e_columns():
    print("== E. 列序与空值 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        st.insert_history(make_row(0, mode="自动·运行中", vfd_state="故障 0x0001",
                                   press_note="断线", turb1=25.0))
        # 只有 ts 的行：其余列必须是 None（不是 0、不是空串）
        st.insert_history({"ts": T0 + STEP})
        rows, _t = st.query_history_page(offset=0, limit=10)

        check("E1 列数与 HISTORY_COLUMNS 一致",
              len(rows[0]) == len(store.Store.HISTORY_COLUMNS),
              "%d vs %d" % (len(rows[0]), len(store.Store.HISTORY_COLUMNS)))
        # 行按时间**正序**返回：rows[0] 是完整行（T0），rows[1] 是只有 ts 的行。
        rec = dict(zip(store.Store.HISTORY_COLUMNS, rows[0]))
        bare = dict(zip(store.Store.HISTORY_COLUMNS, rows[1]))
        check("E2 缺列入库的行取出来是 NULL（None）",
              bare["freq_hz"] is None and bare["di"] is None,
              "freq=%r di=%r" % (bare["freq_hz"], bare["di"]))
        check("E3 中文文本原样往返", rec["mode"] == "自动·运行中",
              repr(rec["mode"]))
        check("E4 带空格的单位串原样往返", rec["flow_unit"] == "m³/h",
              repr(rec["flow_unit"]))
        check("E5 位图整数原样往返", rec["di"] == 0xFFFF, str(rec["di"]))
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def part_f_bounds_and_filter():
    """时间边界（过滤坏 ts）+ lo/hi/before 三条件组合的 SQL 语义。

    [为什么单列一段] 客户端筛选的时间条件最终落到这里；组合条件错一点
    （比如把 hi 写成 before 的别名、或漏掉某个 AND）界面上看不出来，
    只会"多几行/少几行"。用真实 SQLite 逐组合断言才靠得住。
    """
    print("== F. 时间边界与筛选组合 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        # 正常墙钟数据：T0 起 10 行，每步 STEP
        for i in range(10):
            st.insert_history(make_row(i))
        # 一条 1970 年的坏数据（旧版本误写 monotonic 的行）
        st.insert_history(make_row(0, ts=3236093))

        lo, hi = st.history_bounds()
        check("F1 history_bounds 起点 = 最早有效数据", lo == T0,
              "lo=%d 期望 %d" % (lo, T0))
        check("F2 history_bounds 终点 = 最新有效数据",
              hi == T0 + 9 * STEP, "hi=%d" % hi)
        check("F3 1970 年坏 ts 未污染边界",
              lo > store.Store.VALID_TS_MIN,
              "lo=%d 下界=%d" % (lo, store.Store.VALID_TS_MIN))

        # 单个条件
        check("F4 只 lo：9 行（T0+2*STEP 起）",
              st.count_history(lo_ms=T0 + 2 * STEP) == 8,
              str(st.count_history(lo_ms=T0 + 2 * STEP)))
        check("F5 只 hi：T0+3*STEP 及之前 = 4 行 + 坏行 = 5",
              st.count_history(hi_ms=T0 + 3 * STEP) == 5,
              str(st.count_history(hi_ms=T0 + 3 * STEP)))
        check("F6 只 before（窗口）",
              st.count_history(before_ms=T0 + 5 * STEP) == 7,
              str(st.count_history(before_ms=T0 + 5 * STEP)))

        # 组合：lo + hi
        rows, total = st.query_history_page(
            offset=0, limit=50, lo_ms=T0 + 2 * STEP, hi_ms=T0 + 5 * STEP)
        check("F7 lo+hi 组合总数 4", total == 4, "total=%d" % total)
        check("F8 lo+hi 行数与总数一致", len(rows) == 4,
              "%d 行" % len(rows))
        check("F9 所有行都在区间内",
              all(T0 + 2 * STEP <= r[0] <= T0 + 5 * STEP for r in rows),
              str([r[0] for r in rows]))

        # 组合：lo + hi + before（三个条件 AND）
        rows, total = st.query_history_page(
            offset=0, limit=50, lo_ms=T0 + 2 * STEP, hi_ms=T0 + 8 * STEP,
            before_ms=T0 + 5 * STEP)
        check("F10 三个条件同时生效（window 优先收窄到 4 行）",
              total == 4, "total=%d" % total)
        check("F11 最晚一行不超过 before",
              max(r[0] for r in rows) <= T0 + 5 * STEP,
              "max=%s" % max(r[0] for r in rows))

        # lo 晚于 hi → 空集（不静默变成全部）
        rows, total = st.query_history_page(
            offset=0, limit=50, lo_ms=T0 + 8 * STEP, hi_ms=T0 + 2 * STEP)
        check("F12 lo>hi → 空集", total == 0 and rows == [],
              "total=%d rows=%d" % (total, len(rows)))

        # 边界相等（含端点）
        rows, total = st.query_history_page(
            offset=0, limit=50, lo_ms=T0 + 4 * STEP, hi_ms=T0 + 4 * STEP)
        check("F13 lo=hi → 只命中该时刻 1 行",
              total == 1 and rows[0][0] == T0 + 4 * STEP,
              "total=%d" % total)
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#      K. 关键字下推（服务端筛选 = 分页 total 与 rows 共用同一条 WHERE）
# ============================================================================

def _keyword_rows(st):
    """造一批可确定性断言的行：4 行'正转运行' / 8 行'停机' / 1 行特殊字符。

    另单独用 insert 造全 NULL 行，验证离线列显示 '--' 而非 '0.00'。
    """
    for i in range(12):
        st.insert_history(make_row(
            i, vfd_state="正转运行" if i % 3 == 0 else "停机",
            mode="手动", freq_hz=25.0, di=0xFFFF))
    # 含 % _ 反斜杠字面量的行（测 LIKE 通配符转义）
    st.insert_history(make_row(
        20, mode="50%_A" + chr(92) + "B", vfd_state="停机",
        freq_hz=None, di=None, turb1=None, turb2=None, flow=None,
        flow_total=None, press_mpa=None, press_note=None, temp=None,
        hum=None, current_a=None, rpm=None, do=None))
    # 全 NULL 行：显示文本应全是 "--"
    st.insert_history(make_row(
        21, mode=None, vfd_state=None, freq_hz=None, current_a=None,
        rpm=None, turb1=None, turb2=None, flow=None, flow_unit=None,
        flow_total=None, total_unit=None, press_mpa=None, press_note=None,
        temp=None, hum=None, di=None, do=None))


def part_k_keyword():
    print("== K. 关键字下推 ==")
    tmp = tempfile.mkdtemp()
    try:
        st = new_store(tmp)
        _keyword_rows(st)
        # 时间基准（make_row(i) → ts = T0 + i*STEP）
        t0 = T0

        # ---- K1~K3 全列 / 列限定 / 列排除 ----
        rows, total = st.query_history_page(kw="运行")
        check("K1 全列搜'运行'命中 4 行（总数=行数）",
              total == 4 and len(rows) == 4, "total=%d rows=%d" % (total, len(rows)))
        rows, total = st.query_history_page(kw="运行", kw_cols=[2])
        check("K2 只在'搅拌状态'列(2)搜'运行'仍命中 4 行",
              total == 4, "total=%d" % total)
        rows, total = st.query_history_page(kw="运行", kw_cols=[1])
        check("K3 只在'模式'列(1)搜'运行'⇒0 行（列限定真生效）",
              total == 0 and rows == [], "total=%d" % total)

        # ---- K4~K6 LIKE 特殊字符按字面量转义 ----
        rows, total = st.query_history_page(kw="50%")
        check("K4 搜字面'50%'只中 1 行（% 不是通配符）",
              total == 1, "total=%d" % total)
        rows, total = st.query_history_page(kw="50%_A")
        check("K5 搜字面'50%_A'只中 1 行（_ 不是通配符）",
              total == 1, "total=%d" % total)
        rows, total = st.query_history_page(kw="A" + chr(92) + "B")
        check("K6 搜含反斜杠'A\\B'命中 1 行", total == 1, "total=%d" % total)

        # ---- K7~K8 NULL 显示 '--' 而非 '0.00' ----
        rows, total = st.query_history_page(kw="0.00")
        check("K7 搜'0.00'不误命中全 NULL 行", total == 0, "total=%d" % total)
        rows, total = st.query_history_page(kw="--")
        check("K8 搜'--'命中 NULL 行（显示同口径）", total >= 1,
              "total=%d" % total)

        # ---- K9~K10 时间 AND 关键字 / 翻页不重不漏 ----
        rows, total = st.query_history_page(kw="运行", lo_ms=t0 + 3 * STEP)
        check("K9 时间 AND 关键字：命中 3 行", total == 3, "total=%d" % total)
        seen = []
        for off in (0, 2, 4):
            r, _t = st.query_history_page(offset=off, limit=2, kw="运行")
            seen += [x[0] for x in r]
        check("K10 关键字下翻页 4 行不重不漏", len(seen) == len(set(seen)) == 4,
              "ts=%s" % seen)

        # ---- K11~K14 越界 / 空关键字 / count 同口径 / 列集合 = 全部 ----
        rows, total = st.query_history_page(kw="运行", kw_cols=[99, 100])
        check("K11 全越界列下标退回全列（不变成空条件）",
              total == 4, "total=%d" % total)
        rows, total = st.query_history_page(kw="")
        check("K12 空关键字不过滤", total == 14, "total=%d" % total)
        c = st.count_history(kw="运行", lo_ms=t0 + 3 * STEP)
        check("K13 count_history 与 query total 同口径", c == 3,
              "count=%d" % c)
        all_cols = list(range(len(st.DISPLAY_EXPR)))
        rows, total = st.query_history_page(kw="运行", kw_cols=all_cols)
        check("K14 显式全列 == 省略 kwCols", total == 4, "total=%d" % total)

        # ---- K15 显示表达式 == 客户端 _server_row_texts（同口径锁）----
        # 从 server_py 测试里 import 客户端模块：把 client_py 加进 sys.path
        client_root = os.path.join(os.path.dirname(os.path.dirname(HERE)),
                                   "client_py")
        if client_root not in sys.path:
            sys.path.insert(0, client_root)
        from hsfsrv_client.pages_other import HistoryPage  # noqa: E402
        sel = ("SELECT " + ", ".join("(%s)" % e for e in st.DISPLAY_EXPR)
               + " FROM history ORDER BY ts")
        sql_texts = [list(r) for r in st._conn.execute(sel).fetchall()]
        raw = st._conn.execute(
            "SELECT " + ",".join(st.HISTORY_COLUMNS)
            + " FROM history ORDER BY ts").fetchall()
        fake = type("Fake", (), {
            "TURBIDITY_WARN_NTU": HistoryPage.TURBIDITY_WARN_NTU})()
        cli_texts = [
            HistoryPage._server_row_texts(fake, list(r),
                                          list(st.HISTORY_COLUMNS))[0]
            for r in raw]
        mismatch = []
        for ri, (srow, crow) in enumerate(zip(sql_texts, cli_texts)):
            for ci, (a, b) in enumerate(zip(srow, crow)):
                if a != b:
                    mismatch.append((ri, ci, a, b))
        check("K15 SQL 显示文本与客户端格式化逐格一致（%d 行 x %d 列）"
              % (len(sql_texts), len(st.DISPLAY_EXPR)),
              not mismatch, "分歧=%s" % (mismatch[:3],))
        check("K16 显示列数 == 客户端 COLUMNS 列数",
              len(st.DISPLAY_EXPR) == len(HistoryPage.COLUMNS),
              "%d vs %d" % (len(st.DISPLAY_EXPR), len(HistoryPage.COLUMNS)))
        st.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    part_a_basic()
    part_b_window()
    part_c_range()
    part_d_edges()
    part_e_columns()
    part_f_bounds_and_filter()
    part_k_keyword()
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
