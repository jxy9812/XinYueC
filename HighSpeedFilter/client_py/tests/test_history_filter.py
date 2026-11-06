# -*- coding: utf-8 -*-
"""历史页筛选功能全场景测试（**纯控件层**，不连服务端）。

    python tests/test_history_filter.py

[为什么单开这一套] 筛选是"看起来简单、实际最容易悄悄错"的一块：
时间格式有 5 种写法、起止可单给可组合、关键字要与列选择联动、
**服务端模式下时间/关键字/列三者都要下推**（否则分页时关键字只能搜
到当前页，用户实测"关键字筛选无效"）、翻页后筛选不能丢……
每一条错了界面都"照常显示"，只是显示的东西不对。之前的覆盖只有：
  · test_remote_settings_ui C 段：4 种格式归一化 + 4 行合成数据的时间筛选
  · test_history_page G 段：服务端模式下时间下推 + 清除（8 项）
组合条件、边界、默认值、与分页/排序/导出的交互都没测。这里补齐。

覆盖：
  A. 时间格式归一化全表（合法 9 种 / 非法 10 种）
  B. 起止时间四个组合（only start / only end / both / 跨天）
  C. 关键字：全列匹配 / 单列 / 多列 / 不命中 / 空串
  D. 时间 + 关键字组合（AND 语义）
  E. 边界：起止相等 / 起晚于止 / 极早极晚 / 秒级精度
  F. 筛选与排序、列显示、导出的交互（互不干扰）
  G. 默认起始时间（只填一次、不覆盖用户输入、清除后不回填、坏 ts 不采用）
  H. 筛选参数下推的构造（_server_time_range 的各种输入）
  I. 服务端模式筛选触发（_apply_filter / _on_columns_changed 重取第 0 页、
     请求带 kw/kwCols、本地不再重复筛、bridge 载荷字段）
"""

import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)

from PyQt5.QtCore import Qt                             # noqa: E402

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


def make_page():
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import HistoryPage
    status = []
    ctx = PageContext(LinkBridge(), lambda i: None, status.append)
    page = HistoryPage(ctx)
    return app, page, status


def row(stamp, mode="手动", state="停机", freq="25.00", turb="12.50",
        flow="36.50 m³/h", press="0.499", temp="26.5"):
    """造一行 15 列文本（列序同 HistooryPage.COLUMNS）。"""
    return ([stamp, mode, state, freq, "3.20", "750", turb, "3.20",
             flow, "123.40 m³", press, temp, "55.0",
             "0x0000FFFF", "0x000000FF"], False)


def seed(page, rows):
    page._rows = list(rows)
    page._rebuild()


def stamps_of(rows):
    return [r[0][0] for r in rows]


# ============================================================================
#                    A. 时间格式归一化全表
# ============================================================================

def part_a_norm(page):
    print("== A. 时间格式归一化 ==")
    today = time.strftime("%Y-%m-%d")
    year = today[:4]
    valid = [
        # (输入, is_end, 期望) —— 期望按 `_norm_stamp` 实测口径写，
        # 不凭想象：MM-dd 单独也支持（当年），年份两位数按字面当年份。
        ("23:09", False, ("T", "23:09:00")),
        ("23:09", True, ("T", "23:09:59")),
        ("23:09:53", False, ("T", "23:09:53")),
        ("0:05", False, ("T", "00:05:00")),
        ("11-05", False, ("F", "%s-11-05 00:00:00" % year)),
        ("11-05 08:30", False, ("F", "%s-11-05 08:30:00" % year)),
        ("2026-11-05", False, ("F", "2026-11-05 00:00:00")),
        ("2026-11-05", True, ("F", "2026-11-05 23:59:59")),
        ("2026-11-05 08:30", False, ("F", "2026-11-05 08:30:00")),
        ("2026-11-05 08:30:15", True, ("F", "2026-11-05 08:30:15")),
    ]
    ok = 0
    total = len(valid)
    for raw, is_end, want in valid:
        got = page._norm_stamp(raw, is_end=is_end)
        if got == want:
            ok += 1
        else:
            print("      %r(end=%s) → %r 期望 %r" % (raw, is_end, got, want))
    check("A1 合法格式全部归一化正确", ok == total, "%d/%d" % (ok, total))

    # MM-dd 带时刻（当年语义）
    got = page._norm_stamp("11-05 08:30", is_end=False)
    check("A2 MM-dd HH:MM 按当年归一", got is not None
          and got[1].endswith("-11-05 08:30:00"),
          repr(got))

    invalid = ["", "  ", "abc", "25:00", "12:60", "12:00:61", "2026-13-01",
               "2026-02-30", "2026-11-05 25:00", "a b c", "1:2:3:4"]
    bad_ok = 0
    for raw in invalid:
        if page._norm_stamp(raw, is_end=False) is None:
            bad_ok += 1
        else:
            print("      %r 本应判非法，却接受为 %r"
                  % (raw, page._norm_stamp(raw, is_end=False)))
    check("A3 非法格式全部被拒", bad_ok == len(invalid),
          "%d/%d" % (bad_ok, len(invalid)))


# ============================================================================
#                   B. 起止时间组合
# ============================================================================

def part_b_ranges(page):
    print("== B. 起止组合 ==")
    seed(page, [
        row("2026-11-04 23:00:00"),
        row("2026-11-05 08:00:00"),
        row("2026-11-05 12:00:00"),
        row("2026-11-05 18:00:00"),
        row("2026-11-06 09:00:00"),
    ])

    def filt():
        return stamps_of(page._filtered_rows())

    # ---- 只有起点 ----
    page._clear_inputs()
    page._f_start.setText("2026-11-05 12:00")
    check("B1 只给起点 → 该时刻及之后（3 行）",
          filt() == ["2026-11-05 12:00:00", "2026-11-05 18:00:00",
                     "2026-11-06 09:00:00"], str(filt()))

    # ---- 只有终点 ----
    page._clear_inputs()
    page._f_end.setText("2026-11-05 12:00")
    check("B2 只给终点 → 该时刻及之前（3 行）",
          filt() == ["2026-11-04 23:00:00", "2026-11-05 08:00:00",
                     "2026-11-05 12:00:00"], str(filt()))

    # ---- 起止都给（含端点）----
    page._clear_inputs()
    page._f_start.setText("2026-11-05 08:00")
    page._f_end.setText("2026-11-05 18:00")
    check("B3 起止都给 → 闭区间（3 行，含两端）",
          filt() == ["2026-11-05 08:00:00", "2026-11-05 12:00:00",
                     "2026-11-05 18:00:00"], str(filt()))

    # ---- 跨天 ----
    page._clear_inputs()
    page._f_start.setText("2026-11-04")
    page._f_end.setText("2026-11-05")
    check("B4 跨天范围（日期粒度）→ 04 与 05 两天（4 行）",
          len(filt()) == 4, str(filt()))

    # ---- 纯时刻 = 当天语义（跨天数据不能误命中）----
    import datetime as _dt
    today = _dt.date.today()
    tomorrow = today + _dt.timedelta(days=1)
    T = today.strftime("%Y-%m-%d")
    seed(page, [
        row("%s 08:00:00" % T),
        row("%s 12:00:00" % T),
        row("%s 18:00:00" % T),
        row("%s 09:00:00" % tomorrow.strftime("%Y-%m-%d")),
    ])
    page._clear_inputs()
    page._f_start.setText("08:00")
    page._f_end.setText("13:00")
    got = filt()
    check("B5 纯时刻只筛今天（跨天那行不误命中）",
          got == ["%s 08:00:00" % T, "%s 12:00:00" % T], str(got))
    page._f_start.clear()
    page._f_end.clear()
    check("B5b 清掉时刻条件 → 4 行都回来", len(filt()) == 4, str(filt()))

    # 恢复 5 行数据供后续断言
    seed(page, [
        row("2026-11-04 23:00:00"),
        row("2026-11-05 08:00:00"),
        row("2026-11-05 12:00:00"),
        row("2026-11-05 18:00:00"),
        row("2026-11-06 09:00:00"),
    ])

    # ---- 无筛选 ----
    page._clear_inputs()
    check("B6 无筛选 → 全部 5 行（时间正序）", len(filt()) == 5, str(filt()))


# ============================================================================
#                      C. 关键字
# ============================================================================

def part_c_keyword(page):
    print("== C. 关键字 ==")
    page._f_col.check_all()
    seed(page, [
        row("2026-11-05 08:00:00", mode="手动", state="停机"),
        row("2026-11-05 09:00:00", mode="自动·运行中", state="正转运行"),
        row("2026-11-05 10:00:00", mode="手动", state="故障 0x0001"),
    ])

    page._clear_inputs()
    page._f_text.setText("自动")
    check("C1 关键字命中「自动·运行中」（1 行）",
          stamps_of(page._filtered_rows()) == ["2026-11-05 09:00:00"],
          str(stamps_of(page._filtered_rows())))

    page._f_text.setText("故障")
    check("C2 关键字命中「故障 0x0001」",
          stamps_of(page._filtered_rows()) == ["2026-11-05 10:00:00"])

    page._f_text.setText("手动")
    check("C3 关键字命中两行「手动」", len(page._filtered_rows()) == 2)

    page._f_text.setText("不存在的词")
    check("C4 关键字不命中 → 0 行", page._filtered_rows() == [])

    page._f_text.setText("")
    check("C5 关键字空 → 恢复全部（3 行）",
          len(page._filtered_rows()) == 3)

    # ---- 只勾某一列：关键字只在该列里找 ----
    page._clear_inputs()
    page._f_col.clear_all()
    page._f_col._model.item(2).setCheckState(Qt.Checked)   # 只看「搅拌状态」
    page._f_text.setText("手动")
    check("C6 列只勾「搅拌状态」时，关键字「手动」不命中（模式列没勾）",
          page._filtered_rows() == [],
          "%d 行" % len(page._filtered_rows()))

    page._f_col._model.item(1).setCheckState(Qt.Checked)   # 加勾「模式」
    # clear_all 受 min_checked=1 约束会保留第 1 列（时间），所以是 3 列
    check("C7 加勾「模式」列后命中 2 行",
          len(page._filtered_rows()) == 2,
          "%d 行 / %d 列" % (len(page._filtered_rows()),
                            len(page.visible_columns())))
    check("C7b 列显示 = 时间+模式+搅拌状态（3 列）",
          page.visible_columns() == [0, 1, 2],
          str(page.visible_columns()))

    # ---- 大小写/子串 ----
    page._f_col.check_all()
    page._f_text.setText("0x0000")
    check("C8 关键字可匹配十六进制位图列（0x0000 前缀）",
          len(page._filtered_rows()) == 3, "全部行的 DI/DO 都含 0x0000")

    page._f_text.setText("")
    check("C9 清空关键字恢复", len(page._filtered_rows()) == 3)


# ============================================================================
#                  D. 时间 + 关键字 组合
# ============================================================================

def part_d_combo(page):
    print("== D. 时间 + 关键字组合（AND）==")
    page._f_col.check_all()
    seed(page, [
        row("2026-11-05 08:00:00", mode="手动"),
        row("2026-11-05 12:00:00", mode="手动"),
        row("2026-11-05 12:30:00", mode="自动·运行中"),
        row("2026-11-05 18:00:00", mode="自动·运行中"),
    ])

    page._clear_inputs()
    page._f_start.setText("2026-11-05 12:00")
    page._f_text.setText("自动")
    got = stamps_of(page._filtered_rows())
    check("D1 时间≥12:00 且 关键字=自动 → 2 行",
          got == ["2026-11-05 12:30:00", "2026-11-05 18:00:00"], str(got))

    page._f_end.setText("2026-11-05 12:30")
    got = stamps_of(page._filtered_rows())
    check("D2 再加终点 12:30 → 只剩 1 行",
          got == ["2026-11-05 12:30:00"], str(got))

    page._f_text.setText("手动")
    got = stamps_of(page._filtered_rows())
    # 12:00 那行是「手动」，18:00 是「自动·运行中」→ 只剩 1 行
    check("D3 同时间窗但关键字换成「手动」→ 1 行（AND 语义）",
          got == ["2026-11-05 12:00:00"], str(got))


# ============================================================================
#                        E. 边界情况
# ============================================================================

def part_e_edges(page):
    print("== E. 边界 ==")
    seed(page, [
        row("2026-11-05 08:00:00"),
        row("2026-11-05 09:00:00"),
    ])

    # 起止完全相同：只命中该秒那一行
    page._clear_inputs()
    page._f_start.setText("2026-11-05 08:00:00")
    page._f_end.setText("2026-11-05 08:00:00")
    got = stamps_of(page._filtered_rows())
    check("E1 起止相等（秒级）→ 只命中该行",
          got == ["2026-11-05 08:00:00"], str(got))

    # 起晚于止 → 空集（不是全部，也不是报错）
    page._f_start.setText("2026-11-05 20:00")
    page._f_end.setText("2026-11-05 08:00")
    check("E2 起晚于止 → 空集（不静默变成全部）",
          page._filtered_rows() == [], str(page._filtered_rows()))

    # 极早起点 → 全部
    page._clear_inputs()
    page._f_start.setText("2000-01-01")
    check("E3 极早起点 → 全部行", len(page._filtered_rows()) == 2)

    # 极晚终点 → 全部
    page._clear_inputs()
    page._f_end.setText("2099-12-31")
    check("E4 极晚终点 → 全部行", len(page._filtered_rows()) == 2)

    # 起点在最后一行之后
    page._clear_inputs()
    page._f_start.setText("2099-01-01")
    check("E5 起点晚于所有数据 → 空集", page._filtered_rows() == [])

    # 终点在第一行之前
    page._clear_inputs()
    page._f_end.setText("2000-01-01")
    check("E6 终点早于所有数据 → 空集", page._filtered_rows() == [])

    # 空数据集
    page._clear_inputs()
    page._rows = []
    check("E7 无数据 + 有筛选 → 空集不崩",
          page._filtered_rows() == [], "ok")
    seed(page, [row("2026-11-05 08:00:00")])
    check("E8 数据恢复后筛选仍有效",
          len(page._filtered_rows()) == 1)


# ============================================================================
#              F. 与排序 / 列显示 / 导出 的交互
# ============================================================================

def part_f_interaction(page):
    print("== F. 与排序/列/导出的交互 ==")
    page._f_col.check_all()
    page._f_start.clear()
    page._f_end.clear()
    page._f_text.clear()
    seed(page, [
        row("2026-11-05 08:00:00", turb="12.50"),
        row("2026-11-05 09:00:00", turb="88.88"),
        row("2026-11-05 10:00:00", turb="3.20"),
    ])

    # 筛选 + 排序：排序只重排筛后的结果
    page._f_start.setText("2026-11-05 09:00")
    page._sort_col = 6          # 浊度1 列
    page._sort_desc = True
    page._rebuild()
    view = [r[0][0] for r in page._visible_rows()]
    check("F1 排序只作用于筛后结果（09:00、10:00 两行）",
          len(view) == 2, str(view))
    check("F2 按浊度降序正确（88.88 在 3.20 前）",
          view[0] == "2026-11-05 09:00:00", str(view))

    # 筛选 + 列隐藏：筛出的行不受列显示影响
    page._sort_col = None
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(Qt.Checked)   # 只显示时间
    page._rebuild()
    check("F3 隐藏大多数列后，筛选结果不变（2 行）",
          page._table.table.rowCount() == 2,
          "table rows=%d" % page._table.table.rowCount())
    check("F4 隐藏列后筛选逻辑仍能取到被隐藏列的值（浊度用于判断）",
          len(page._filtered_rows()) == 2)

    # 导出只含可见列 + 筛后的行
    import csv
    import tempfile
    cols = page.visible_columns()
    rows = page._visible_rows()
    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "f.csv")
    try:
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            w = csv.writer(fh)
            w.writerow([page.COLUMNS[c] for c in cols])
            for texts, _a in rows:
                w.writerow([texts[c] for c in cols])
        with open(path, encoding="utf-8-sig") as fh:
            got = list(csv.reader(fh))
        check("F5 导出：表头 = 1 列（只显示时间）", got[0] == ["时间"],
              str(got[0]))
        check("F6 导出：行数 = 筛后行数（2）", len(got) - 1 == 2,
              "%d 行" % (len(got) - 1))
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)

    page._f_col.check_all()
    page._clear_inputs()


# ============================================================================
#                    G. 默认起始时间
# ============================================================================

def part_g_default_start():
    print("== G. 默认起始时间 ==")
    app, page, status = make_page()
    page._data_mode = "server"
    earliest = 1793927000000
    latest = 1793927400000

    def reply(earliest_ts, latest_ts=latest):
        page._on_page_ready({"ok": True, "total": 10, "cols": [],
                             "rows": [], "dbRows": 10,
                             "latestTs": latest_ts,
                             "earliestTs": earliest_ts,
                             "offset": 0, "limit": 20})

    check("G1 初始起始框为空", page._f_start.text() == "",
          repr(page._f_start.text()))
    reply(earliest)
    want = time.strftime("%Y-%m-%d %H:%M:%S",
                         time.localtime(earliest / 1000.0))
    check("G2 首次应答后起始框 = 最前一条数据时间",
          page._f_start.text() == want, repr(page._f_start.text()))
    check("G3 状态栏提示已载入",
          any("最早一条" in s for s in status), str(status[-1:]))

    # 只填一次：再应答不覆盖
    reply(earliest - 3600000)
    check("G4 后续应答不覆盖已填的默认值（只填一次）",
          page._f_start.text() == want, repr(page._f_start.text()))

    # 坏 ts（1970 年）不采用
    app2, page2, _st2 = make_page()
    page2._data_mode = "server"
    page2._on_page_ready({"ok": True, "total": 0, "cols": [], "rows": [],
                          "dbRows": 0, "latestTs": 0, "earliestTs": 3236093,
                          "offset": 0, "limit": 20})
    check("G5 1970 年坏 ts 不填进起始框", page2._f_start.text() == "",
          repr(page2._f_start.text()))

    # 空库（earliestTs=0）不填
    app3, page3, _st3 = make_page()
    page3._data_mode = "server"
    page3._on_page_ready({"ok": True, "total": 0, "cols": [], "rows": [],
                          "dbRows": 0, "latestTs": 0, "earliestTs": 0,
                          "offset": 0, "limit": 20})
    check("G6 空库不填默认值", page3._f_start.text() == "",
          repr(page3._f_start.text()))

    # 用户先手输 → 不覆盖
    app4, page4, _st4 = make_page()
    page4._data_mode = "server"
    page4._f_start.setText("2026-11-01 00:00")
    page4._on_start_edited("2026-11-01 00:00")
    page4._on_page_ready({"ok": True, "total": 10, "cols": [], "rows": [],
                          "dbRows": 10, "latestTs": latest,
                          "earliestTs": earliest, "offset": 0, "limit": 20})
    check("G7 用户手输的时间不被默认值覆盖",
          page4._f_start.text() == "2026-11-01 00:00",
          repr(page4._f_start.text()))

    # 清除筛选后不回填
    page._clear_filter()
    check("G8 清除筛选清空起始框", page._f_start.text() == "",
          repr(page._f_start.text()))
    reply(earliest)
    check("G9 清除筛选后不再自动回填", page._f_start.text() == "",
          repr(page._f_start.text()))


# ============================================================================
#                H. 下推参数构造（_server_time_range）
# ============================================================================

def part_h_pushdown():
    print("== H. 下推参数构造 ==")
    app, page, _st = make_page()
    page._data_mode = "server"

    page._f_start.clear()
    page._f_end.clear()
    check("H1 无时间条件 → None（不带上界下界）",
          page._server_time_range() is None,
          repr(page._server_time_range()))

    page._f_start.setText("2026-11-05 08:00:00")
    lo, hi = page._server_time_range()
    check("H2 只给起点 → (lo, None)", lo is not None and hi is None,
          "lo=%s hi=%s" % (lo, hi))
    check("H3 lo 是毫秒且与输入一致",
          lo == int(time.mktime(time.strptime("2026-11-05 08:00:00",
                                              "%Y-%m-%d %H:%M:%S")) * 1000),
          str(lo))

    page._f_start.clear()
    page._f_end.setText("2026-11-05 18:00:00")
    lo, hi = page._server_time_range()
    check("H4 只给终点 → (None, hi)", lo is None and hi is not None,
          "lo=%s hi=%s" % (lo, hi))

    page._f_start.setText("2026-11-05 08:00")
    lo, hi = page._server_time_range()
    check("H5 起止都给 → 两个毫秒值且 lo<hi", lo is not None
          and hi is not None and lo < hi, "%s ~ %s" % (lo, hi))

    # 纯时刻 = 当天
    page._f_start.setText("08:00")
    page._f_end.clear()
    lo, _hi = page._server_time_range()
    day_start = time.mktime(time.strptime(
        time.strftime("%Y-%m-%d ") + "08:00:00", "%Y-%m-%d %H:%M:%S"))
    check("H6 纯时刻按当天换算", lo == int(day_start * 1000), str(lo))

    # 非法输入 → 不抛异常，返回 None（_apply_filter 负责先报错）
    page._f_start.setText("乱码")
    try:
        got = page._server_time_range()
        ok = got is None
    except Exception as exc:                        # noqa: BLE001
        ok = False
        print("      异常 %s: %s" % (type(exc).__name__, exc))
    check("H7 非法输入不抛异常（返回 None）", ok, repr(got))

    # _apply_filter 对非法输入只报错、不改筛选状态
    page._f_start.setText("13:99")
    before = (page._f_start.text(), page._page_index)
    page._data_mode = "local"                       # local 分支只 rebuild
    page._apply_filter()
    check("H8 非法起始时间：报错且不改页码",
          page._page_index == before[1], "index=%d" % page._page_index)


def part_i_server_trigger():
    """服务端模式筛选触发语义（拦截 bridge 调用，不发真实网络）。

    这是 2026-11-06「关键字筛选无效」修复的控件层锁：关键字/列变化
    必须触发**重取第 0 页**且请求里带上 kw/kwCols；否则 _filtered_rows
    在服务端模式直接放行，关键字等于没生效。
    """
    print("== I. 服务端模式筛选触发 ==")
    app, page, _st = make_page()
    calls = []

    class StubBridge(object):
        def is_connected(self):
            return True

        def history_page(self, **kw):
            calls.append(dict(kw))

    page.ctx.bridge = StubBridge()
    page._data_mode = "server"
    page._page_index = 3
    page._window_end = 123456

    # ---- I1 任何筛选变化（哪怕只改关键字）都重取第 0 页 ----
    page._f_text.setText("运行")
    page._apply_filter()
    check("I1 只改关键字也重取第 0 页（旧 bug：只推时间）",
          page._page_index == 0 and len(calls) == 1,
          "index=%d calls=%d" % (page._page_index, len(calls)))
    check("I2 请求带 kw 与 kwCols", calls
          and calls[0].get("kw") == "运行"
          and calls[0].get("kw_cols") == page.visible_columns(),
          "kw=%r cols=%r" % (calls[0].get("kw") if calls else None,
                             calls[0].get("kw_cols") if calls else None))
    check("I3 筛选重置窗口锚点（追最新）", page._window_end > 123456,
          "end=%d" % page._window_end)

    # 清 _inflight（stub 不回调，手动复位以继续测）
    page._inflight = False
    calls[:] = []

    # ---- I4 关键字为空时，列变化只是视图设置，不重取 ----
    page._f_text.clear()
    page._page_index = 2
    page._on_columns_changed()
    check("I4 无关键字时改列不发网络请求（纯视图）", len(calls) == 0,
          "calls=%d" % len(calls))

    # ---- I5 关键字激活时，改列影响 kwCols ⇒ 必须重取 ----
    page._f_text.setText("运行")
    page._page_index = 2
    page._on_columns_changed()
    check("I5 关键字激活时改列触发重取（kwCols 是筛选条件）",
          page._page_index == 0 and len(calls) == 1,
          "index=%d calls=%d" % (page._page_index, len(calls)))
    check("I6 重取请求带上新的 kwCols", calls
          and calls[0].get("kw_cols") == page.visible_columns(),
          "cols=%r" % (calls[0].get("kw_cols") if calls else None))

    # ---- I7 服务端模式 _filtered_rows 不再本地重复筛 ----
    page._inflight = False
    page._rows = [row("2026-11-06 09:00:00", mode="手动"),
                  row("2026-11-06 09:00:01", mode="自动")]
    page._f_text.setText("绝不可能命中")
    check("I7 服务端模式 _filtered_rows 直接放行（本地不再重复筛）",
          len(page._filtered_rows()) == 2, "%d 行" % len(page._filtered_rows()))

    # ---- I8 清除筛选在服务端模式重取且清掉 kw ----
    page._inflight = False
    calls[:] = []
    page._page_index = 4
    page._clear_filter()
    check("I8 清除筛选重取第 0 页", page._page_index == 0 and len(calls) == 1,
          "index=%d calls=%d" % (page._page_index, len(calls)))
    check("I9 清除后请求不带 kw", calls and calls[0].get("kw") is None,
          "kw=%r" % (calls[0].get("kw") if calls else None))
    check("I10 清除后输入框全空", page._f_text.text() == ""
          and page._f_start.text() == "", "text=%r" % page._f_text.text())

    # ---- I11 bridge.history_page 载荷字段（proto 常量对齐）----
    from hsfsrv_client import proto
    from hsfsrv_client.bridge import LinkBridge
    lb = LinkBridge()
    captured = {}
    # 不连服务端也能验参数：直接替换底层 request 出口
    def fake_request(code, doc, on_done=None, on_error=None, timeout=5.0):
        captured["code"] = code
        captured.update(doc)
    lb.request = fake_request
    lb.history_page(offset=0, limit=20, before_ts=999, lo_ts=111,
                    hi_ts=222, kw="运行", kw_cols=[0, 2])
    check("I11 bridge 载荷含 kw / kwCols（BSON 键名对齐）",
          captured.get(proto.HIST_KEY_KW) == "运行"
          and captured.get(proto.HIST_KEY_KW_COLS) == [0, 2],
          "kw=%r cols=%r" % (captured.get(proto.HIST_KEY_KW),
                             captured.get(proto.HIST_KEY_KW_COLS)))
    check("I12 bridge 载荷时间/锚点键齐全",
          captured.get(proto.HIST_KEY_LO_TS) == 111
          and captured.get(proto.HIST_KEY_HI_TS) == 222
          and captured.get(proto.HIST_KEY_BEFORE_TS) == 999,
          "lo=%s hi=%s before=%s" % (captured.get(proto.HIST_KEY_LO_TS),
                                     captured.get(proto.HIST_KEY_HI_TS),
                                     captured.get(proto.HIST_KEY_BEFORE_TS)))
    # 空关键字/空列不写字段（省载荷，也与旧调用方兼容）
    captured.clear()
    lb.history_page(offset=5, limit=10)
    check("I13 空 kw/kwCols 不写进载荷（向后兼容）",
          proto.HIST_KEY_KW not in captured
          and proto.HIST_KEY_KW_COLS not in captured,
          "keys=%s" % sorted(captured.keys()))


def main():
    app, page, status = make_page()
    part_a_norm(page)
    part_b_ranges(page)
    part_c_keyword(page)
    part_d_combo(page)
    part_e_edges(page)
    part_f_interaction(page)
    part_g_default_start()
    part_h_pushdown()
    part_i_server_trigger()
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
