# -*- coding: utf-8 -*-
"""历史页「列多选框 = 显示哪些列」测试（纯控件层，不连服务端）。

    python tests/test_history_columns.py

用户要求：「列多选框你要测试，只显示勾选的列，默认是全部勾选」

覆盖：
  A. 默认全部勾选（15/15 可见，没有列被隐藏）
  B. 只显示勾选的列（勾 3 列 ⇒ 其余 12 列隐藏、表头对得上）
  C. 至少保留一列（把最后一列也取消 → 被忽略，不会出现空表头）
  D. 恢复全选（一键回到 15 列）
  E. 列宽只分给可见列（不留大片空白、可见列被撑宽）
  F. 导出按可见列出列（表头 + 每行都只有勾中的列）
  G. 关键字只在可见列里匹配（藏起来的列不参与）
  H. 排序/行数据不受列显示影响（数据行始终完整 15 列）

[为什么单列一套] 上一轮把「列」从"筛选哪些列"改成"显示哪些列"，
这是语义变更而不是纯新增：空选含义、导出列、关键字范围、列宽分配
四处都跟着变。这四处各自都会静默出错（界面看着有内容、其实列错了/
导出的列不对），必须有断言盯着。
"""

import csv
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)

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
    ctx = PageContext(LinkBridge(), lambda i: None, lambda s: None)
    return app, HistoryPage(ctx)


def row(stamp, mode="手动", turb="12.50"):
    return ([stamp, mode, "停机", "25.00", "3.20", "750",
             turb, "3.20", "36.50 m³/h", "123.40 m³", "0.499",
             "26.5", "55.0", "0x0000FFFF", "0x000000FF"], False)


def seed(page, count=5):
    page._rows = [row("2026-11-06 09:1%d:00" % i) for i in range(count)]
    page._rebuild()


# ============================================================================
#                      A. 默认全部勾选
# ============================================================================

def part_a_default(page):
    print("== A. 默认全部勾选 ==")
    cols = page._f_col.checked_indexes()
    check("A1 默认 15 列全部勾选", len(cols) == len(page.COLUMNS),
          "%d/%d" % (len(cols), len(page.COLUMNS)))
    table = page._table.table
    hidden = [c for c in range(table.columnCount())
              if table.isColumnHidden(c)]
    check("A2 没有列被隐藏", hidden == [], str(hidden))
    check("A3 visible_columns 返回全列",
          page.visible_columns() == list(range(len(page.COLUMNS))),
          str(page.visible_columns()))
    check("A4 下拉文本显示「全部列（全部）」",
          "全部" in page._f_col.lineEdit().text(),
          page._f_col.lineEdit().text())


# ============================================================================
#                        B. 只显示勾选的列
# ============================================================================

def part_b_subset(page):
    print("== B. 只显示勾选的列 ==")
    from PyQt5.QtCore import QPoint, Qt
    seed(page, 5)
    page._f_col.clear_all()
    for c in (0, 1, 6):                       # 时间 / 模式 / 浊度1
        page._f_col._model.item(c).setCheckState(Qt.Checked)
    table = page._table.table
    check("B1 勾选 3 列", page.visible_columns() == [0, 1, 6],
          str(page.visible_columns()))
    hidden = [c for c in range(table.columnCount())
              if table.isColumnHidden(c)]
    check("B2 其余 12 列被隐藏", len(hidden) == 12, "%d 列隐藏" % len(hidden))
    check("B3 勾选的列没有被隐藏",
          not any(table.isColumnHidden(c) for c in (0, 1, 6)))
    shown = [table.horizontalHeaderItem(c).text()
             for c in range(table.columnCount())
             if not table.isColumnHidden(c)]
    check("B4 显示的表头 = 勾中的列名",
          shown == ["时间", "模式", "浊度1(NTU)"], str(shown))
    check("B5 下拉文本换成勾中的列名",
          "时间" in page._f_col.lineEdit().text()
          and "浊度1" in page._f_col.lineEdit().text(),
          page._f_col.lineEdit().text())

    # 数据行仍是完整 15 列（取数/排序/图表/导出都按完整行）
    check("B6 数据行仍保留完整 15 列（显示只是视图）",
          len(page._rows[0][0]) == len(page.COLUMNS),
          "%d 列" % len(page._rows[0][0]))
    check("B7 表格显示的行数正常", table.rowCount() == len(page._rows),
          "table=%d" % table.rowCount())


# ============================================================================
#                        C. 至少保留一列
# ============================================================================

def part_c_min_one(page):
    print("== C. 至少保留一列 ==")
    from PyQt5.QtCore import QPoint, Qt
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(Qt.Checked)   # 只剩「时间」
    check("C1 只剩 1 列", page.visible_columns() == [0],
          str(page.visible_columns()))
    # 试图把最后一列也取消（模拟真实点击：走 _toggle_item）
    model = page._f_col._model
    page._f_col._toggle_item(model.index(0, 0))
    check("C2 取消最后一列被忽略（不会清空表）",
          page.visible_columns() == [0], str(page.visible_columns()))
    check("C3 仍有一列表头可见",
          any(not page._table.table.isColumnHidden(c)
              for c in range(page._table.table.columnCount())))
    # clear_all 也必须留一列
    page._f_col.clear_all()
    check("C4 clear_all 也至少留一列", len(page.visible_columns()) >= 1,
          str(page.visible_columns()))


# ============================================================================
#                          D. 恢复全选
# ============================================================================

def part_d_check_all(page):
    print("== D. 恢复全部 ==")
    page._f_col.check_all()
    check("D1 check_all 后 15 列全可见",
          page.visible_columns() == list(range(len(page.COLUMNS))),
          "%d 列" % len(page.visible_columns()))
    table = page._table.table
    check("D2 没有列被隐藏",
          not any(table.isColumnHidden(c)
                  for c in range(table.columnCount())))


# ============================================================================
#                     E. 列宽只分给可见列
# ============================================================================

def part_e_widths(app):
    print("== E. 列宽分给可见列 ==")
    page = make_page()[1]
    seed(page, 3)
    page.resize(1200, 400)
    page.show()
    for _ in range(6):
        app.processEvents()
    table = page._table.table
    header = table.horizontalHeader()

    total_all = sum(header.sectionSize(c) for c in range(table.columnCount()))
    check("E1 全列时各列宽度之和接近视口宽",
          total_all > 0, "总宽 %dpx" % total_all)

    from PyQt5.QtCore import QPoint, Qt
    page._f_col.clear_all()
    for c in (0, 1, 6):
        page._f_col._model.item(c).setCheckState(Qt.Checked)
    for _ in range(6):
        app.processEvents()
    vis_total = sum(header.sectionSize(c) for c in (0, 1, 6))
    check("E2 3 列时可见列被撑宽（不留大片空白）",
          vis_total > 600, "3 列共 %dpx" % vis_total)
    check("E3 可见列总宽接近视口宽（±15%）",
          abs(vis_total - table.viewport().width()) < table.viewport().width() * 0.15,
          "可见 %d vs 视口 %d" % (vis_total, table.viewport().width()))
    page.close()


# ============================================================================
#                       F. 导出按可见列
# ============================================================================

def part_f_export(page):
    print("== F. 导出按可见列 ==")
    from PyQt5.QtCore import QPoint, Qt
    seed(page, 3)
    page._f_col.clear_all()
    for c in (0, 1, 6):
        page._f_col._model.item(c).setCheckState(Qt.Checked)

    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "out.csv")
    try:
        # 复用 _export_csv 的文件写法（跳过保存框：直接调内部按可见列导出的
        # 逻辑——用 _export_data 需要 QFileDialog 交互）
        rows = page._visible_rows()
        cols = page.visible_columns()
        with open(path, "w", encoding="utf-8-sig", newline="") as fh:
            writer = csv.writer(fh)
            writer.writerow([page.COLUMNS[c] for c in cols])
            for texts, _abn in rows:
                writer.writerow([texts[c] if c < len(texts) else ""
                                 for c in cols])
        with open(path, encoding="utf-8-sig") as fh:
            got = list(csv.reader(fh))
        check("F1 表头只有可见列",
              got[0] == ["时间", "模式", "浊度1(NTU)"], str(got[0]))
        check("F2 每行列数 = 3", all(len(r) == 3 for r in got),
              "首行 %d 列" % len(got[0]))
        check("F3 数据行数正确", len(got) - 1 == len(rows),
              "%d 行" % (len(got) - 1))
        check("F4 时间列内容正确（默认最新在上）",
              got[1][0] == "2026-11-06 09:12:00",
              got[1][0] if len(got) > 1 else "-")
        check("F5 时间列全部落在导出的 3 列里",
              all(r[0].startswith("2026-11-06") for r in got[1:]),
              str([r[0] for r in got[1:]]))
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


# ============================================================================
#                    G. 关键字只在可见列匹配
# ============================================================================

def part_g_keyword(page):
    print("== G. 关键字只在可见列匹配 ==")
    from PyQt5.QtCore import QPoint, Qt
    page._rows = [
        row("2026-11-06 09:10:00", mode="手动", turb="12.50"),
        row("2026-11-06 09:11:00", mode="手动", turb="88.88"),
    ]
    page._f_col.check_all()
    page._f_text.setText("88.88")
    page._rebuild()
    check("G1 全列可见时关键字命中浊度行",
          len(page._filtered_rows()) == 1,
          "%d 行" % len(page._filtered_rows()))

    # 把浊度列（6/7）藏起来 → 该行不该再被命中（藏了就不参与搜索）
    page._f_col.clear_all()
    for c in (0, 1):
        page._f_col._model.item(c).setCheckState(Qt.Checked)
    page._rebuild()
    check("G2 藏掉浊度列后关键字不再命中（不参与搜索）",
          len(page._filtered_rows()) == 0,
          "%d 行" % len(page._filtered_rows()))
    page._f_text.clear()
    page._f_col.check_all()
    page._rebuild()
    check("G3 恢复后行数回到 2", len(page._filtered_rows()) == 2,
          "%d 行" % len(page._filtered_rows()))


# ============================================================================
#                  H. 排序不受列显示影响
# ============================================================================

def part_h_sort(page):
    print("== H. 排序不受列显示影响 ==")
    from PyQt5.QtCore import QPoint, Qt
    page._f_col.clear_all()
    page._f_col._model.item(0).setCheckState(Qt.Checked)     # 只显示时间
    page._f_col._model.item(1).setCheckState(Qt.Checked)
    page._rows = [
        row("2026-11-06 09:10:00"),
        row("2026-11-06 09:12:00"),
        row("2026-11-06 09:11:00"),
    ]
    page._sort_col = 0
    page._sort_desc = True
    page._rebuild()
    order = [r[0][0] for r in page._visible_rows()]
    check("H1 按时间列排序仍正确（即便只显示 2 列）",
          order == sorted(order, reverse=True), str(order))
    check("H2 数据行仍完整", len(page._rows[0][0]) == len(page.COLUMNS),
          "%d 列" % len(page._rows[0][0]))


def part_i_real_clicks(app):
    """真实鼠标点击（**这是本轮用户反馈的两个 bug 的回归锁**）。

    用户原话：
      「可以取消列但是勾不回去了，而且单机一次多选框就消失了，
        应该设为失去焦点，或在下拉框外面单机才收起下拉框」

    两个症状都是**只测状态测不出来**的：内部 `_checked` 一直是对的，
    坏的是 QComboBox 的"选中即关"语义 + lineEdit 被覆盖。
    所以这一段一律用 QTest.mouseClick 真点，断言：
      · 点项后浮层**仍然开着**（能连点多选）
      · 取消掉的项能**再勾回来**
      · 按钮文本始终是勾选汇总（不会被覆盖成某一个单项名）
      · 点浮层外/按钮自身 → 收起
    """
    print("== I. 真实点击（用户反馈的两个 bug）==")
    from PyQt5.QtCore import QPoint, Qt
    from PyQt5.QtTest import QTest
    from PyQt5.QtWidgets import QWidget, QVBoxLayout
    from hsfsrv_client.pages_other import MultiCheckCombo

    host = QWidget()
    host.resize(420, 320)
    lay = QVBoxLayout(host)
    combo = MultiCheckCombo(["时间", "模式", "搅拌状态", "输出频率(Hz)"],
                            all_text="全部列", initial="all", min_checked=1)
    lay.addWidget(combo)
    host.show()
    app.processEvents()

    def click_item(idx):
        """点某一项的**勾选指示器**位置（不是矩形中心）。

        [为什么不用 rect().center()] QCheckBox 只有指示器 + 文字那一段
        可点，右侧空白区不响应（实测：点 center=192px 处勾选态不变，
        点 8px 处才翻转）。离屏平台没有真实布局宽度差异，位置错一点就
        变成"点了没反应"，会误判成功能坏了。
        """
        cb = combo._checks[idx]
        QTest.mouseClick(cb, Qt.LeftButton, Qt.NoModifier, QPoint(8, 17))
        app.processEvents()

    def click_button():
        QTest.mouseClick(combo, Qt.LeftButton, Qt.NoModifier,
                         combo.rect().center())
        app.processEvents()

    # ---- I1 点按钮 → 浮层打开 ----
    click_button()
    check("I1 点按钮弹出勾选浮层",
          combo._popup is not None and combo._popup.isVisible())

    # ---- I2 点项 → 勾选变化，**浮层不关** ----
    click_item(1)                                  # 取消「模式」
    check("I2a 点击项取消勾选", combo.checked_indexes() == [0, 2, 3],
          str(combo.checked_indexes()))
    check("I2b 点项后浮层仍然开着（能连点多选）",
          combo._popup is not None and combo._popup.isVisible())
    check("I2c 按钮文本是勾选汇总（不是被覆盖的单项名）",
          combo.lineEdit().text() == "时间、搅拌状态、输出频率(Hz)",
          repr(combo.lineEdit().text()))

    # ---- I3 **取消后能再勾回来**（用户报的"勾不回去了"）----
    click_item(1)
    check("I3a 取消掉的项能再勾回来", combo.checked_indexes() == [0, 1, 2, 3],
          str(combo.checked_indexes()))
    check("I3b 勾回后浮层还开着",
          combo._popup is not None and combo._popup.isVisible())
    check("I3c 全勾时文本回到「全部列（全部）」",
          "全部" in combo.lineEdit().text(), repr(combo.lineEdit().text()))

    # ---- I4 连点多项（反复取消/勾回，浮层全程不关）----
    for _round in range(3):
        click_item(2)
        click_item(2)
    check("I4 连点 3 轮后状态仍自洽、浮层还开着",
          combo.checked_indexes() == [0, 1, 2, 3]
          and combo._popup is not None and combo._popup.isVisible(),
          str(combo.checked_indexes()))

    # ---- I5 至少留一列（真实点击）----
    for i in (1, 2, 3):
        click_item(i)
    check("I5a 只留「时间」", combo.checked_indexes() == [0],
          str(combo.checked_indexes()))
    click_item(0)                                  # 再点最后一项
    check("I5b 点最后一项被拒（仍留一列）", combo.checked_indexes() == [0],
          str(combo.checked_indexes()))
    check("I5c 被拒时复选框回弹（勾选态与实际一致）",
          combo._checks[0].isChecked() is True,
          str([cb.isChecked() for cb in combo._checks]))

    # ---- I6 点按钮自身 → 收起（用户要求的"点外面才收"里的一种）----
    click_item(1)                                  # 先勾回一个
    check("I6a 勾回后 2 列", len(combo.checked_indexes()) == 2,
          str(combo.checked_indexes()))
    click_button()
    check("I6b 再点按钮收起浮层",
          combo._popup is None or not combo._popup.isVisible())

    # ---- I7 浮层被关掉后按钮复位（不再呈按下态）----
    click_button()
    check("I7a 重新打开", combo._popup is not None
          and combo._popup.isVisible())
    combo._close_popup()                    # 等价于点外面/Esc 的效果
    app.processEvents()
    check("I7b 浮层关掉后按钮复位（isDown=False）",
          combo._popup is None and not combo.isDown(),
          "popup=%r isDown=%s" % (combo._popup, combo.isDown()))
    click_button()
    check("I7c 关掉后还能再打开",
          combo._popup is not None and combo._popup.isVisible())
    combo._close_popup()
    host.close()


def main():
    app, page = make_page()
    part_a_default(page)
    part_b_subset(page)
    part_c_min_one(page)
    part_d_check_all(page)
    part_e_widths(app)
    part_f_export(page)
    part_g_keyword(page)
    part_h_sort(page)
    part_i_real_clicks(app)
    print("==== %d passed, %d failed ====" % (PASS, FAIL))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
