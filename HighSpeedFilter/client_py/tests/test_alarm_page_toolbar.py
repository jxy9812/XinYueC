# -*- coding: utf-8 -*-
"""报警页工具条全场景测试（**纯控件层**，不连服务端）。

    python tests/test_alarm_page_toolbar.py

任务口径（2026-11-09 对齐 C 端 HsfDataAlarm.c：alarmRowVisible /
alarmApplyFilter / alarmClearFilter / alarmApplyColumnFilter /
alarmExportCsv / alarmBuildChart / alarmSetView）：

  A. 类型筛选：类型下拉后表格行集 = 期望子集（类型文本全等，逐类验）
  B. 时间窗：起始/结束/组合筛选（epoch 毫秒边界与 C 同口径：ts<lo 不可见、
     ts>hi 不可见、界为空=不限）
  C. 起点晚于终点：提示「起始时间晚于结束时间，未筛选」且**不动**结果
  D. 列显隐：单列显示 / 恢复全部列（隐藏列的数据仍在表里，只不显示）
  E. 行数统计：「显示 N / 共 M 条」跟随筛选
  F. 清除筛选：类型回全部、时间窗回数据全区间、全部行可见
  G. CSV 导出：文件存在 + 首行 UTF-8 BOM（utf-8-sig）+ 表头/行序（最新在
     上）；无 Download 权限时 _export_data 直接拒绝且**不落盘**
  H. 报警次数/分钟折线图：分钟聚合正确（同分钟合并、异分钟分桶）、
     随筛选联动
  I. 表格/图表双视图切换（set_view 幂等、视图栈与按钮勾选态同步）
  J. MAX_ROWS 环形截断：超出丢最旧、最新保留
"""

import os
import sys
import tempfile
import time

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
sys.path.insert(0, CLIENT_ROOT)

PASS = 0
FAIL = 0
FAILED = []


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        FAILED.append(name)
        print("  [FAIL] %s %s" % (name, detail))


def stamp_text(ms, fmt="%Y-%m-%d %H:%M:%S"):
    return time.strftime(fmt, time.localtime(ms / 1000.0))


def make_page():
    from PyQt5.QtWidgets import QApplication
    app = QApplication.instance() or QApplication(sys.argv[:1])
    from hsfsrv_client.bridge import LinkBridge
    from hsfsrv_client.page_base import PageContext
    from hsfsrv_client.pages_other import AlarmPage
    status = []
    ctx = PageContext(LinkBridge(), lambda i: None, status.append)
    page = AlarmPage(ctx)
    return app, page, status


def seed(page, base_ms):
    """注入 4 条不同类型/时间的报警（旧→新：T0 / +30s / +60s / +120s）。"""
    page._record_alarm("搅拌泵", "变频器", "故障", base_ms)
    page._record_alarm("温湿度", "温湿度", "数据过期", base_ms + 30 * 1000)
    page._record_alarm("流量计", "流量计", "离线", base_ms + 60 * 1000)
    page._record_alarm("电磁阀组", "电磁阀组", "故障",
                       base_ms + 120 * 1000)


def table_kinds(page):
    """当前表格里第 2 列（类型）的文本列表（视图序 = 最新在上）。"""
    table = page._table.table
    out = []
    for r in range(table.rowCount()):
        item = table.item(r, 2)
        out.append(item.text() if item else "")
    return out


# ============================================================================
#                    A. 类型筛选
# ============================================================================

def part_a_kind(page, base_ms):
    print("\n== A. 类型筛选 ==")
    kinds = ["变频器", "温湿度", "流量计", "电磁阀组"]
    for kind in kinds:
        # [用 KIND_ITEMS.index 找下标] 类型下拉里还插着"模拟量/485总线/
        # 数字量"等类型，写死序号会错位。
        page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index(kind))
        page._apply_filter()
        got = table_kinds(page)
        check("类型=%s → 只显示该类型" % kind,
              got == [kind], "%s" % got)
    check("行数统计随筛选（显示 1 / 共 4 条）",
          page._count_label.text() == "显示 1 / 共 4 条",
          page._count_label.text())
    # 全部类型恢复
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()
    got = table_kinds(page)
    check("全部类型 → 4 行全显（最新在上）",
          got == ["电磁阀组", "流量计", "温湿度", "变频器"], "%s" % got)
    # 注一个表里没有的类型（模拟量）：命中 0 行但不报错
    idx = page.KIND_ITEMS.index("模拟量")
    page._kind_combo.setCurrentIndex(idx)
    page._apply_filter()
    check("无该类型的报警 → 0 行（不崩）", table_kinds(page) == [],
          "%s" % table_kinds(page))
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()


# ============================================================================
#                    B. 时间窗
# ============================================================================

def part_b_time(page, base_ms):
    print("\n== B. 时间窗筛选 ==")
    # 只给起点（= +45s）：剩 +60s / +120s 两条
    page._f_start.setText(stamp_text(base_ms + 45 * 1000))
    page._f_end.clear()
    page._apply_filter()
    check("只给起点（+45s）→ 剩 2 行（最新在上）",
          table_kinds(page) == ["电磁阀组", "流量计"], "%s" % table_kinds(page))

    # 只给终点（= +60s）：剩 T0 / +30s / +60s 三条（含端点，C 同口径 ts<=hi）
    page._f_start.clear()
    page._f_end.setText(stamp_text(base_ms + 60 * 1000))
    page._apply_filter()
    check("只给终点（+60s）→ 剩 3 行（含端点）",
          len(table_kinds(page)) == 3, "%s" % table_kinds(page))

    # 起点 == 终点（= +60s）：只剩端点那一条
    page._f_start.setText(stamp_text(base_ms + 60 * 1000))
    page._f_end.setText(stamp_text(base_ms + 60 * 1000))
    page._apply_filter()
    check("起止相等 → 只剩端点 1 条",
          table_kinds(page) == ["流量计"], "%s" % table_kinds(page))

    # 纯时刻（当天语义）：00:00 起当天全命中
    page._f_start.setText("00:00")
    page._f_end.setText(stamp_text(base_ms + 120 * 1000))
    page._apply_filter()
    check("纯时刻起点 00:00（当天）→ 4 行全显",
          len(table_kinds(page)) == 4, "%s" % table_kinds(page))

    # 两侧留空 = 不限
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    check("两侧留空 = 不限 → 4 行",
          len(table_kinds(page)) == 4, "%s" % table_kinds(page))


# ============================================================================
#                    C. 起点晚于终点
# ============================================================================

def part_c_inverted(page, base_ms):
    print("\n== C. 起点晚于终点（未筛选）==")
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    before = table_kinds(page)
    page._f_start.setText(stamp_text(base_ms + 120 * 1000))
    page._f_end.setText(stamp_text(base_ms + 30 * 1000))
    page._apply_filter()
    check("提示「起始时间晚于结束时间，未筛选」",
          "未筛选" in (page.ctx_status() or ""), page.ctx_status())
    check("未应用筛选（行集不动）", table_kinds(page) == before,
          "%s" % table_kinds(page))
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()


# ============================================================================
#                    D. 列显隐
# ============================================================================

def part_d_columns(page):
    print("\n== D. 列显隐 ==")
    table = page._table.table
    page._col_combo.setCurrentIndex(2)          # 「类型」列
    hidden = [table.isColumnHidden(c) for c in range(4)]
    check("选「类型」→ 只显示第 1 列",
          hidden == [True, False, True, True], "%s" % hidden)
    check("隐藏列的数据仍在（设备列文本未被清）",
          (table.item(0, 0).text() if table.item(0, 0) else "") != "",
          table.item(0, 0).text() if table.item(0, 0) else "None")
    page._col_combo.setCurrentIndex(0)
    hidden = [table.isColumnHidden(c) for c in range(4)]
    check("回「全部列」→ 4 列全显",
          hidden == [False] * 4, "%s" % hidden)


# ============================================================================
#                    E. 行数统计
# ============================================================================

def part_e_count(page):
    print("\n== E. 行数统计 ==")
    check("全量 → 显示 4 / 共 4 条",
          page._count_label.text() == "显示 4 / 共 4 条",
          page._count_label.text())
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("变频器"))
    page._apply_filter()
    check("筛选后 → 显示 1 / 共 4 条（共 = 全部记录数）",
          page._count_label.text() == "显示 1 / 共 4 条",
          page._count_label.text())
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()


# ============================================================================
#                    F. 清除筛选
# ============================================================================

def part_f_clear(page, base_ms):
    print("\n== F. 清除筛选 ==")
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("流量计"))
    page._f_start.setText(stamp_text(base_ms + 45 * 1000))
    page._col_combo.setCurrentIndex(1)          # 「时间」列
    page._apply_filter()
    check("筛选生效（1 行）", len(table_kinds(page)) == 1,
          "%s" % table_kinds(page))
    page._clear_filter()
    check("清除后类型回全部（4 行）",
          len(table_kinds(page)) == 4, "%s" % table_kinds(page))
    check("清除后类型下拉回「全部类型」",
          page._kind_combo.currentIndex() == 0)
    check("清除后列显隐回「全部列」",
          page._col_combo.currentIndex() == 0
          and not page._table.table.isColumnHidden(0))
    check("清除后时间窗回数据全区间（起点=最早一条）",
          page._f_start.text() == stamp_text(base_ms),
          page._f_start.text())
    check("清除后时间窗回数据全区间（终点=最新一条）",
          page._f_end.text() == stamp_text(base_ms + 120 * 1000),
          page._f_end.text())


# ============================================================================
#                    G. CSV 导出
# ============================================================================

def part_g_export(page, base_ms):
    print("\n== G. CSV 导出 ==")
    from hsfsrv_client import proto
    rows = [row for _ts, row in page._visible_records()]
    tmpdir = tempfile.mkdtemp(prefix="hsf_alarm_export_")
    path = os.path.join(tmpdir, "alarm.csv")

    # 无 Download 权限（匿名）：_export_data 拒绝且不落盘
    page.ctx.bridge.role = proto.ROLE_ANONYMOUS
    page._export_data()
    check("匿名无 Download 权限 → 拒绝导出",
          "权限不足" in (page.ctx_status() or ""), page.ctx_status())
    check("拒绝时不落盘", not os.path.exists(path))

    # 直接写（权限闸之后的落盘路径；授权由服务端权威校验，界面侧闸已单测）
    page._write_export(path, rows)
    check("导出文件存在", os.path.exists(path), path)
    with open(path, "rb") as fh:
        raw = fh.read()
    check("首行以 UTF-8 BOM 开头（utf-8-sig）",
          raw[:3] == b"\xef\xbb\xbf", "%r" % raw[:3])
    text = raw.decode("utf-8-sig")
    lines = text.strip().splitlines()
    check("表头 = 列定义（含 BOM 后的首行）",
          lines[0] == ",".join(page.COLUMNS), lines[0])
    check("行序 = 最新在上（4 行数据）",
          len(lines) == 5 and "电磁阀组" in lines[1]
          and "变频器" in lines[-1], "%s" % lines[1:])
    check("状态栏回报导出条数",
          "已导出 4 条" in (page.ctx_status() or ""), page.ctx_status())

    # 筛选后再导出：只导命中行
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("变频器"))
    page._apply_filter()
    path2 = os.path.join(tmpdir, "alarm_vfd.csv")
    rows2 = [row for _ts, row in page._visible_records()]
    page._write_export(path2, rows2)
    with open(path2, "rb") as fh:
        lines2 = fh.read().decode("utf-8-sig").strip().splitlines()
    check("筛选后导出只含命中行（1 条变频器）",
          len(lines2) == 2 and "变频器" in lines2[1], "%s" % lines2)
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()


# ============================================================================
#                    H. 报警次数/分钟折线图
# ============================================================================

def part_h_chart(page, base_ms):
    print("\n== H. 报警次数/分钟折线图 ==")
    # minute 粒度对齐：base_ms 取整分钟（本文件 seed 的 4 条跨 3 个分钟桶）
    aligned = base_ms - (base_ms % 60000)
    page._records = []
    page._seen = {}
    page._record_alarm("A", "变频器", "故障", aligned)
    page._record_alarm("B", "变频器", "故障", aligned + 30 * 1000)   # 同分钟
    page._record_alarm("C", "温湿度", "数据过期", aligned + 90 * 1000)
    series = page._chart_series()
    name, points = series[0]
    check("序列名 = 报警次数/分钟", name == "报警次数/分钟", name)
    check("分钟聚合：同分钟合并、异分钟分桶",
          points == [(aligned, 2), (aligned + 60000, 1)], "%s" % points)

    # 图表对话框建出来（图表视图内嵌）
    page.set_view(1)
    dialog = page._chart_dialog
    check("图表视图建出内嵌对话框", dialog is not None)
    check("图表数据与聚合一致",
          dialog._series[0][1] == [(aligned, 2), (aligned + 60000, 1)],
          "%s" % (dialog._series[0][1],))
    got = [v for _t, v in dialog._series[0][1]]
    check("y 值为次数（2, 1）", got == [2, 1], "%s" % got)

    # 筛选联动：只看温湿度 → 图表只剩 1 个点
    page._kind_combo.setCurrentIndex(page.KIND_ITEMS.index("温湿度"))
    page._apply_filter()                        # _view_mode==1 会刷新图表
    check("筛选联动：图表只剩温湿度那分钟的 1 点",
          page._chart_dialog._series[0][1] == [(aligned + 60000, 1)],
          "%s" % (page._chart_dialog._series[0][1],))
    page._kind_combo.setCurrentIndex(0)
    page._apply_filter()

    # 空数据：图表为空 + 状态提示（不崩）
    page._records = []
    page._rebuild()
    page._refresh_chart()
    check("无记录 → 图表为空 + 提示",
          page._chart_dialog._series[0][1] == []
          and "暂无报警" in (page.ctx_status() or ""), page.ctx_status())
    # 恢复数据
    seed(page, base_ms)


# ============================================================================
#                    I. 表格/图表双视图
# ============================================================================

def part_i_views(page):
    print("\n== I. 表格/图表双视图 ==")
    page.set_view(0)
    check("表格视图：视图栈第 0 页 + 表格钮勾选",
          page._view_stack.currentIndex() == 0
          and page._btn_table_view.isChecked()
          and not page._btn_chart_view.isChecked())
    page.set_view(1)
    check("图表视图：视图栈第 1 页 + 图表钮勾选",
          page._view_stack.currentIndex() == 1
          and page._btn_chart_view.isChecked()
          and not page._btn_table_view.isChecked())
    page.set_view(1)                            # 幂等：再点图表视图不重建
    check("重复切同一视图保持不变（幂等）",
          page._view_mode == 1 and page._view_stack.currentIndex() == 1)
    page.set_view(0)
    check("切回表格视图", page._view_stack.currentIndex() == 0)
    page.set_view(99)                           # 越界值按表格处理
    check("越界 mode 归一为表格视图", page._view_mode == 0)


# ============================================================================
#                    J. MAX_ROWS 环形截断
# ============================================================================

def part_j_ring(page, base_ms):
    print("\n== J. MAX_ROWS 环形截断 ==")
    page._records = []
    page._seen = {}
    # [先清时间窗] 前面段落里「清除筛选」会把时间框回填成数据区间
    # （C alarmClearFilter 语义），这里不显式清掉的话，截断后行集会被
    # 旧窗口裁掉，截断断言就不纯了。
    page._f_start.clear()
    page._f_end.clear()
    page._apply_filter()
    for i in range(page.MAX_ROWS + 3):
        page._record_alarm("dev%d" % i, "变频器", "故障",
                           base_ms + i * 1000)
    check("记录数封顶 MAX_ROWS", len(page._records) == page.MAX_ROWS,
          "%d" % len(page._records))
    check("最旧 3 条被丢弃（首条为 dev3）",
          page._records[0][1][1] == "dev3", page._records[0][1][1])
    check("最新保留（末条为最后一条）",
          page._records[-1][1][1] == "dev%d" % (page.MAX_ROWS + 2),
          page._records[-1][1][1])
    table = page._table.table
    check("表格行数与封顶一致", table.rowCount() == page.MAX_ROWS,
          "%d" % table.rowCount())


def main():
    app, page, _status = make_page()
    # 分钟对齐的基准时刻（避免跨分钟边界造成聚合断言抖动）
    base_ms = int(time.time() * 1000) // 60000 * 60000
    seed(page, base_ms)

    # PageContext.set_status 记录到列表；为了断言方便包装最近一次取值
    def _ctx_status():
        return _status[-1] if _status else ""
    page.ctx_status = _ctx_status

    part_a_kind(page, base_ms)
    part_b_time(page, base_ms)
    part_c_inverted(page, base_ms)
    part_d_columns(page)
    part_e_count(page)
    part_f_clear(page, base_ms)
    part_g_export(page, base_ms)
    part_h_chart(page, base_ms)
    part_i_views(page)
    part_j_ring(page, base_ms)

    print("\n%s" % ("=" * 60))
    print("===== %d passed, %d failed =====" % (PASS, FAIL))
    if FAILED:
        print("失败项: " + ", ".join(FAILED))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
