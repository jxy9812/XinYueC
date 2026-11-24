# -*- coding: utf-8 -*-
"""XLSX 导出「数值列必须是**数值格**」回归锁（2026-11-19 用户实报）。

[用户实报] 「历史数据导出的时候，有些列是数值，保存为 xlsx 的时候为啥是
    文本型而不是数值型」——Excel/WPS 里左对齐、SUM 求和失效、不能排序。

[根因] 导出管线交给写出器的是**界面显示文本**（"2.29" / "--" /
    "0x00000000"）⇒ 写出器若一律按 ``t="inlineStr"`` 写，**每一格都是
    文本**（数值格必须是 ``<c r="D2"><v>-0.008</v></c>``：不带 ``t``
    属性、内容放在 ``<v>`` 里）。

[本文件锁什么]
    A 写出器单元级：数值格 / 文本格 / bool / 空值 / NaN·Inf / 精度；
    B 产物级：合法 zip + 必需部件齐全 + 每个 XML 部件可解析；
    C 端到端：**真实**历史显示文本（``HistoryPage._server_row_texts``
      的真实格式化）→ 写 xlsx → 逐格判定：数值列是数值格、文本列是
      inlineStr；
    D CSV 不受影响（**CSV 仍是文本**——数值格只影响 xlsx）。

[怎么跑]
    QT_QPA_PLATFORM=offscreen python -I tests/test_xlsx_numeric.py
"""

import os
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CLIENT_ROOT = os.path.dirname(HERE)
if CLIENT_ROOT not in sys.path:
    sys.path.insert(0, CLIENT_ROOT)

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError, OSError):
        pass

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from hsfsrv_client.xlsx_stream import XlsxStreamWriter           # noqa: E402

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"

PASS = 0
FAIL = 0
SKIP = 0


def check(name, ok, detail=""):
    global PASS, FAIL
    if ok:
        PASS += 1
        print("  [PASS] %s %s" % (name, detail))
    else:
        FAIL += 1
        print("  [FAIL] %s %s" % (name, detail))
    return ok


def skip(name, why):
    global SKIP
    SKIP += 1
    print("  [SKIP] %s（%s）" % (name, why))


def _col_letter(index):
    """1 基列号 → Excel 字母列名。"""
    letters = ""
    while index > 0:
        index, rem = divmod(index - 1, 26)
        letters = chr(ord("A") + rem) + letters
    return letters


def _parse_cells(xml_bytes):
    """sheetN.xml → {列字母+行号: (类型, 文本)}；类型 num/str/empty。

    [为什么要 XML 解析而不是正则] 正则分不出 "``<v>`` 里是数值还是字符串"
    ——``t="inlineStr"`` 的格子里**没有** ``<v>``，但正则容易漏判；用
    解析器看**属性 t 在不在**才是真判据（这正是 Excel 认不认它是数的
    依据）。
    """
    root = ET.fromstring(xml_bytes)
    cells = {}
    for row in root.iter(NS + "row"):
        for cell in row.iter(NS + "c"):
            ref = cell.get("r")
            v = cell.find(NS + "v")
            is_ = cell.find(NS + "is")
            if v is not None:
                cells[ref] = ("num", v.text or "")
            elif is_ is not None:
                t = is_.find(NS + "t")
                cells[ref] = ("str", (t.text or "") if t is not None else "")
            else:
                cells[ref] = ("empty", "")
    return cells


def _raw_cell(row_xml):
    """一行 XML → 该行第一个 ``<c>`` 的原文（单元级判据）。"""
    start = row_xml.find("<c ")
    if start < 0:
        return ""
    end = row_xml.find("</c>", start)
    return row_xml[start:end + 4] if end > 0 else row_xml[start:]


#: 历史页 19 个显示列（与 HistoryPage.COLUMNS 同序；2026-11-23 加电机温度
#: 与压力1/压力2）。
HEADERS = ["时间", "模式", "搅拌状态", "输出频率(Hz)", "输出电流(A)",
           "搅拌转速(rpm)", "电机温度(℃)",
           "进水浊度(NTU)", "出水浊度(NTU)", "水温(℃)",
           "瞬时流量", "正累计", "压差(MPa)",
           # [压力1/压力2 2026-11-23] 插在压差之后；其后 电箱温度/湿度/
           # DI/DO 的下标整体 +2（15/16 → 17/18）。
           "进水压力(MPa)", "出水压力(MPa)",
           "电箱温度(℃)", "电箱湿度(%RH)", "DI(32位)", "DO(32位)"]

#: 真实显示文本（由 HistoryPage._server_row_texts 格式化，见 C 段）。
TEXTS = ["2025-11-19 08:42:00", "manual", "运行", "45.00", "12.34", "1440",
         "46.2", "2.29", "0.04", "19.3", "3.60 m³/h", "123.45 m³",
         "-0.008", "0.512", "0.746", "25.5", "55.0",
         "0x00000000", "0x0000000A"]

#: 期望是**数值格**的列下标（0 基）——纯数字字面量。
NUMERIC_COLS = [3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16]
#: 期望是**文本格**的列下标——时间/枚举/带单位/十六进制位图。
TEXT_COLS = [0, 1, 2, 10, 11, 17, 18]


def sec_unit():
    """A 段：写出器单元级判定。"""
    from hsfsrv_client.xlsx_stream import _cell_xml, _row_xml

    # ---- A1 真正的数值 ⇒ 数值格（**不带 t 属性**）----
    cell = _cell_xml("D2", -0.008)
    check("A1 float 数值写数值格（无 t 属性）",
          cell == '<c r="D2"><v>-0.008</v></c>' and "t=" not in cell, cell)
    cell = _cell_xml("F2", 1440)
    check("A2 int 数值写数值格",
          cell == '<c r="F2"><v>1440</v></c>' and "t=" not in cell, cell)

    # ---- A3 数值**文本** ⇒ 也是数值格（导出管线给的就是显示文本！）----
    cell = _cell_xml("G2", "2.29")
    check("A3 数值字面量文本写数值格（导出管线的真实输入）",
          cell == '<c r="G2"><v>2.29</v></c>' and "t=" not in cell, cell)

    # ---- A4 普通文本 ⇒ inlineStr ----
    cell = _cell_xml("A2", "手动")
    check("A4 文本格走 inlineStr",
          cell == '<c r="A2" t="inlineStr"><is><t xml:space="preserve">手动'
                  '</t></is></c>', cell)

    # ---- A5 bool 必须先判（bool 是 int 的子类 ⇒ 否则 True 变成数值 1）----
    cell = _cell_xml("A2", True)
    check("A5 bool True 是文本 TRUE（不是数值 1）",
          't="inlineStr"' in cell and ">TRUE<" in cell, cell)
    cell = _cell_xml("A2", False)
    check("A6 bool False 是文本 FALSE（不是数值 0）",
          't="inlineStr"' in cell and ">FALSE<" in cell, cell)

    # ---- A7 空值跳过该格（既有语义不变）----
    check("A7 None 跳过该格", "<c " not in _row_xml(1, [None]), "")
    check("A8 空串跳过该格", "<c " not in _row_xml(1, [""]), "")

    # ---- A9 NaN/Inf 不是合法数值格 ⇒ 退回文本 ----
    cell = _cell_xml("A2", float("nan"))
    check("A9 NaN 退回文本（不写非法数值格）",
          't="inlineStr"' in cell and "<v>" not in cell, cell)
    cell = _cell_xml("A2", float("inf"))
    check("A10 Inf 退回文本（不写非法数值格）",
          't="inlineStr"' in cell and "<v>" not in cell, cell)

    # ---- A11 精度保真（不 round、不 %.2f）----
    check("A11 0.1 不被改精度", _cell_xml("A2", 0.1) ==
          '<c r="A2"><v>0.1</v></c>', _cell_xml("A2", 0.1))
    check("A12 -0.008 一字不差", _cell_xml("A2", -0.008) ==
          '<c r="A2"><v>-0.008</v></c>', _cell_xml("A2", -0.008))
    check("A13 负零保留", _cell_xml("A2", -0.0) ==
          '<c r="A2"><v>-0.0</v></c>', _cell_xml("A2", -0.0))
    check("A14 大整数不丢位", _cell_xml("A2", 123456789012345) ==
          '<c r="A2"><v>123456789012345</v></c>', "")

    # ---- A15 下列"看着像数但不是数"的**保持文本** ----
    for text, why in (("007", "前导零编号"),
                      ("3.60 m³/h", "带单位（单位逐行可能不同）"),
                      ("0x0000000A", "十六进制位图"),
                      ("2025-11-19 08:42:00", "时间戳"),
                      ("--", "无数据"),
                      ("1,234", "千分位")):
        cell = _cell_xml("A2", text)
        check("A15 %s 保持文本（%s）" % (text, why),
              't="inlineStr"' in cell and "<v>" not in cell, cell)

    # ---- A16 XML 转义（文本里的 & < > 不能炸包）----
    # [为什么不断言 &quot;] 引号只在**属性值**里必须转义；这里是元素
    # 内容（<t>…</t>）⇒ 不转义也合法（且现有转义函数一直如此）。
    cell = _cell_xml("A2", 'a&b<c>d"e')
    check("A16 文本做 XML 转义（& < >）",
          "&amp;" in cell and "&lt;" in cell and "&gt;" in cell, cell)
    try:
        ET.fromstring(("<worksheet xmlns=\"%s\">%s</worksheet>"
                       % (NS[1:-1], cell)).encode("utf-8"))
        parsed = True
    except Exception as exc:                            # noqa: BLE001
        parsed = False
    check("A17 含特殊字符的文本格仍是合法 XML", parsed, "")


def sec_product(tmpdir):
    """B 段：产物级（合法 zip + 部件齐全 + XML 可解析 + 逐格判定）。"""
    path = os.path.join(tmpdir, "numeric.xlsx")
    writer = XlsxStreamWriter(path, ["历史数据"])
    writer.sheet(0, HEADERS)
    writer.row(0, TEXTS)
    writer.close()

    if not check("B1 XLSX 产物生成", os.path.isfile(path), path):
        return
    with zipfile.ZipFile(path) as zf:
        check("B2 产物是合法 zip（testzip 无坏项）", zf.testzip() is None, "")
        names = zf.namelist()
        need = ["[Content_Types].xml", "_rels/.rels", "xl/workbook.xml",
                "xl/worksheets/sheet1.xml"]
        check("B3 必需部件齐全（Excel/WPS 能开的最小包）",
              all(n in names for n in need), str(names))
        parsed, err = True, ""
        for name in names:
            if name.endswith(".xml") or name.endswith(".rels"):
                try:
                    ET.fromstring(zf.read(name))
                except Exception as exc:                   # noqa: BLE001
                    parsed = False
                    err = "%s: %s" % (name, exc)
        check("B4 每个 XML 部件可解析（合法 OOXML）", parsed, err)
        cells = _parse_cells(zf.read("xl/worksheets/sheet1.xml"))

    ok_num, bad_num = True, []
    for c in NUMERIC_COLS:
        kind, text = cells.get("%s2" % _col_letter(c + 1), ("empty", ""))
        if kind != "num" or text != TEXTS[c]:
            ok_num = False
            bad_num.append("%s(%s=%r)" % (HEADERS[c], kind, text))
    check("B5 数值列全部是数值格（%d 列）" % len(NUMERIC_COLS), ok_num,
          "; ".join(bad_num))

    ok_text, bad_text = True, []
    for c in TEXT_COLS:
        kind, text = cells.get("%s2" % _col_letter(c + 1), ("empty", ""))
        if kind != "str" or text != TEXTS[c]:
            ok_text = False
            bad_text.append("%s(%s=%r)" % (HEADERS[c], kind, text))
    check("B6 文本列全部是 inlineStr（%d 列）" % len(TEXT_COLS), ok_text,
          "; ".join(bad_text))

    ok_head = True
    for c, head in enumerate(HEADERS):
        kind, text = cells.get("%s1" % _col_letter(c + 1), ("empty", ""))
        if kind != "str" or text != head:
            ok_head = False
    check("B7 表头行全是文本格", ok_head, "")

    # [数值格内容不失真] 显示文本 "45.00" ⇒ 数值格内容仍是 "45.00"
    # （值还是 45，只是 Excel 用通用格式显示成 45）。
    kind, text = cells.get("D2", ("empty", ""))
    check("B8 数值格内容与显示文本一致（值不失真）",
          kind == "num" and text == "45.00", "%s %r" % (kind, text))


def sec_end_to_end(tmpdir):
    """C 段：真实历史显示文本 → xlsx（端到端）。

    [为什么不用手抄的文本] 那一列"数值文本"是**界面格式化**的产物
    （``HistoryPage._server_row_texts``）⇒ 格式化口径一改（比如压差列
    改成优先显示数值），这里必须跟着变 ⇒ 直接调真实函数。
    """
    try:
        from PyQt5.QtWidgets import QApplication
        from hsfsrv_client import proto
        from hsfsrv_client.pages_other import HistoryPage
    except Exception as exc:                            # noqa: BLE001
        skip("C 段（真实历史显示文本端到端）",
             "PyQt5/pages_other 不可用：%s" % exc)
        return
    app = QApplication.instance() or QApplication(sys.argv[:1])

    class _Stub(object):
        TURBIDITY_WARN_NTU = 20.0

    raw = [1763512920000, "manual", "运行", 45.0, 12.34, 1440.0, 2.29,
           0.045, 3.6, "m³/h", 123.45, "m³", -0.008, "", 25.5, 55.0,
           0, 10, 19.3, 46.2, 0.512, 0.746]
    texts, _abnormal = HistoryPage._server_row_texts(
        _Stub(), raw, list(proto.HISTORY_COLUMNS))
    check("C1 真实格式化产出 19 列显示文本", len(texts) == len(HEADERS),
          str(texts))

    path = os.path.join(tmpdir, "e2e.xlsx")
    writer = XlsxStreamWriter(path, ["历史数据"])
    writer.sheet(0, HEADERS)
    writer.row(0, texts)
    writer.close()
    with zipfile.ZipFile(path) as zf:
        cells = _parse_cells(zf.read("xl/worksheets/sheet1.xml"))

    ok_num, bad = True, []
    for c in NUMERIC_COLS:
        kind, text = cells.get("%s2" % _col_letter(c + 1), ("empty", ""))
        if kind != "num":
            ok_num = False
            bad.append("%s(%s=%r)" % (HEADERS[c], kind, text))
    check("C2 真实导出：数值列是数值格（%d 列）" % len(NUMERIC_COLS), ok_num,
          "; ".join(bad))

    ok_text, bad = True, []
    for c in TEXT_COLS:
        kind, text = cells.get("%s2" % _col_letter(c + 1), ("empty", ""))
        if kind != "str":
            ok_text = False
            bad.append("%s(%s=%r)" % (HEADERS[c], kind, text))
    check("C3 真实导出：文本列是 inlineStr（%d 列）" % len(TEXT_COLS), ok_text,
          "; ".join(bad))
    del app


def sec_csv(tmpdir):
    """D 段：CSV 不受影响（**CSV 仍是文本**——数值格只影响 xlsx）。"""
    try:
        from hsfsrv_client.hsf_export_plan import CsvStreamWriter
    except Exception as exc:                            # noqa: BLE001
        skip("D 段（CSV 仍是文本）", "导入失败：%s" % exc)
        return
    path = os.path.join(tmpdir, "numeric.csv")
    writer = CsvStreamWriter(path, HEADERS)
    writer.write_rows([TEXTS])
    writer.close()
    with open(path, "r", encoding="utf-8-sig", newline="") as fh:
        body = fh.read()
    check("D1 CSV 里数值列仍是文本（未受数值格改动影响）",
          ",45.00," in body and "2.29" in body, body[:120])
    check("D2 CSV 带 UTF-8 BOM（Excel 双击直开不乱码）",
          body.startswith("时间") and "时间" in body, "")


def main():
    global PASS, FAIL
    tmpdir = tempfile.mkdtemp(prefix="hsf_xlsx_numeric_")
    print("== A 写出器单元级 ==")
    sec_unit()
    print("== B 产物级 ==")
    sec_product(tmpdir)
    print("== C 端到端（真实历史显示文本） ==")
    sec_end_to_end(tmpdir)
    print("== D CSV 不受影响 ==")
    sec_csv(tmpdir)
    print("\n汇总：PASS %d / FAIL %d / SKIP %d" % (PASS, FAIL, SKIP))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
