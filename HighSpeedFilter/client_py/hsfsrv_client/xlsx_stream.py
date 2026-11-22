# -*- coding: utf-8 -*-
"""流式 XLSX 写出器（纯标准库：zipfile + SpreadsheetML，**边收边写**）。

[为什么不用 xlsx_bridge（C 端 XExcel）]
    XExcel 的工作簿在 save 之前**全在内存**里 ⇒
      ① 有行数上限（客户端与网页端都锁 12000 行，超出显式截断）——
         用户要「不管多大的数据都能导出」就卡在这条上；
      ② 收尾那一次大写入落在 UI 线程 ⇒ 导出期间界面无响应，**自动控制
         的按钮点不动**（用户 2026-11-14 口径：导出绝不能影响自动控制）。
    本写出器**边收边写**：每批行直接写进 zip 的 sheet part ⇒
      · 行数不限（内存恒定，只有 zip 的压缩缓冲）；
      · 每批只写几十行 ⇒ UI 线程单次占用毫秒级，不卡界面；
      · 纯标准库（板端没有 openpyxl 也能用）。

[格式] 最小可开包（Excel/WPS 都认）：``[Content_Types].xml`` +
    ``_rels/.rels`` + ``xl/workbook.xml`` + ``xl/_rels/workbook.xml.rels``
    + ``xl/styles.xml`` + 每表一个 ``xl/worksheets/sheetN.xml``
    （**数值格不带 t 属性**：``<c r="D2"><v>-0.008</v></c>``；文本格走
    inlineStr）——与网页端已验证的纯 Python 写路
    （``hsfweb/export.py`` 的 ``build_xlsx``）同款结构。

[数值格口径（2026-11-19 用户实报「导出成 xlsx 后数值列是文本型」修）]
    Excel/WPS 里**左对齐 + 求和失效 + 不能排序** = 那一格是文本
    （``t="inlineStr"``）。本写出器的契约：
      · 真正的 ``int``/``float`` ⇒ **数值格**（不带 ``t``，内容在 ``<v>``）；
      · **纯数字字面量的显示文本**（"2.29" / "-0.008" / "1440"）⇒ 同样
        数值格——导出管线给的是**界面显示文本**（表格里显示什么，导出文件
        里就是什么），不转的话**每一格都是文本** ⇒ 用户在 Excel 里求和/
        排序/做公式全都不对；
      · 带单位（"3.60 m³/h"）、十六进制位图（"0x00000000"）、时间戳
        （"2026-11-19 08:22:01"）、无数据（"--"）⇒ **保持文本**
        （单位逐行可能不同、位图不是数、时间不是数、无数据不是数）；
      · ``bool`` ⇒ 文本 "TRUE"/"FALSE"（**bool 是 int 的子类**，不先判
        就会变成数值 1/0）；
      · ``None``/空串 ⇒ 跳过该格（空单元格）。

[用法]
    w = XlsxStreamWriter(path, ["历史数据"])
    w.sheet(0, ["时间", "浊度"])
    for batch in batches:                    # 每批几十行
        for row in batch:
            w.row(0, row)
    w.close()
"""

import math
import re
import zipfile

__all__ = ["XlsxStreamWriter", "XlsxStreamError"]


class XlsxStreamError(RuntimeError):
    """流式写出失败（磁盘满/路径不可写等）。"""


def _xml_escape(text):
    """XML 文本转义 + 剔除 XML 1.0 不允许的控制字符（0x00-0x08 会炸包）。"""
    out = []
    for ch in str(text):
        code = ord(ch)
        if code < 0x20 and ch not in ("\t", "\n", "\r"):
            continue
        out.append(ch)
    text = "".join(out)
    return (text.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;"))


def _col_letter(index):
    """1 基列号 → Excel 字母列名（1=A, 26=Z, 27=AA）。"""
    letters = ""
    while index > 0:
        index, rem = divmod(index - 1, 26)
        letters = chr(ord("A") + rem) + letters
    return letters


#: 纯数字字面量（十进制，可带符号/小数/指数）——**没有单位、没有空格、
#: 不是十六进制**。前导零（"007" / "00"）**不匹配**：编号/补零串转成
#: 数字会悄悄改值。
_NUMERIC_TEXT_RE = re.compile(
    r"^[+-]?(?:0|[1-9]\d*)(?:\.\d+)?(?:[eE][+-]?\d+)?$")

#: 数值文本的长度上限（超长一律当文本——那么长的东西不是数）。
_NUMERIC_TEXT_MAX_LEN = 32


def _numeric_text(value):
    """显示文本 → 数值字面量文本（严格纯数字才转，否则 None）。

    [为什么不直接 float()] ``float("nan")``/``float("inf")``/``float("1_0")``
    都能过 float()，但它们**不是合法数值格** => 用正则严格筛。
    """
    if not isinstance(value, str):
        return None
    if not value or len(value) > _NUMERIC_TEXT_MAX_LEN:
        return None
    if not _NUMERIC_TEXT_RE.match(value):
        return None
    return value.replace("e", "E")


def _number_text(value):
    """数值（int/float）→ 写入 ``<v>`` 的文本（非数值/非有限值 → None）。

    [精度] float 用 ``repr``（最短往返表示：0.1 还是 0.1、-0.008 一字
    不差、负零保留），**不用** ``round``/``%.2f``（那会改精度）。
    """
    if isinstance(value, bool):
        return None                     # bool 是 int 的子类 => 调用方先判
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if not math.isfinite(value):
            return None                 # NaN/Inf 不是合法数值格
        return repr(value).replace("e", "E")
    return None


def _cell_xml(ref, value):
    """一格 → ``<c ...></c>``（**数值格不带 t 属性**；文本格走 inlineStr）。"""
    # [bool 必须先判] bool 是 int 的子类 => 不先判，True 会变成数值 1。
    if isinstance(value, bool):
        return ('<c r="%s" t="inlineStr"><is><t xml:space="preserve">%s</t>'
                '</is></c>' % (ref, "TRUE" if value else "FALSE"))
    num = _number_text(value)
    if num is None and isinstance(value, str):
        num = _numeric_text(value)
    if num is not None:
        return '<c r="%s"><v>%s</v></c>' % (ref, num)
    return ('<c r="%s" t="inlineStr"><is><t xml:space="preserve">%s</t>'
            '</is></c>' % (ref, _xml_escape(value)))


def _row_xml(row_no, values):
    """一行 → ``<row>…</row>``（空值跳过；数值列写数值格，其余 inlineStr）。"""
    cells = []
    for c, value in enumerate(values, start=1):
        ref = "%s%d" % (_col_letter(c), row_no)
        if value is None or value == "":
            continue
        cells.append(_cell_xml(ref, value))
    return '<row r="%d">%s</row>' % (row_no, "".join(cells))


_CONTENT_TYPES_HEAD = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/'
    'content-types">'
    '<Default Extension="rels" ContentType="application/vnd.'
    'openxmlformats-package.relationships+xml"/>'
    '<Default Extension="xml" ContentType="application/xml"/>'
    '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.'
    'openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
    '<Override PartName="/xl/styles.xml" ContentType="application/vnd.'
    'openxmlformats-officedocument.spreadsheetml.styles+xml"/>')

_ROOT_RELS = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/'
    'relationships">'
    '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/'
    'officeDocument/2006/relationships/officeDocument" '
    'Target="xl/workbook.xml"/></Relationships>')

_STYLES = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/'
    '2006/main">'
    '<fonts count="1"><font><sz val="11"/><name val="Calibri"/></font></fonts>'
    '<fills count="2"><fill><patternFill patternType="none"/></fill>'
    '<fill><patternFill patternType="gray125"/></fill></fills>'
    '<borders count="1"><border><left/><right/><top/><bottom/><diagonal/>'
    '</border></borders>'
    '<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" '
    'borderId="0"/></cellStyleXfs>'
    '<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" '
    'xfId="0"/></cellXfs>'
    "</styleSheet>")


class XlsxStreamWriter(object):
    """流式 XLSX 写出器（一张或多张表，行数不限、内存恒定）。

    :param path: 输出文件路径。
    :param sheet_names: 各表名（**顺序即表序**；数量须在开写前定下来——
        ``[Content_Types].xml`` 要按表数逐个声明 Override）。
    """

    def __init__(self, path, sheet_names):
        self._path = path
        self._names = [str(n) for n in sheet_names]
        if not self._names:
            raise XlsxStreamError("至少要有一张表")
        self._zf = None
        self._writers = {}
        self._row_no = {}
        self._closed = False
        try:
            self._zf = zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED)
            self._zf.writestr("[Content_Types].xml",
                              self._content_types())
            self._zf.writestr("_rels/.rels", _ROOT_RELS)
            self._zf.writestr("xl/workbook.xml", self._workbook())
            self._zf.writestr("xl/_rels/workbook.xml.rels",
                              self._workbook_rels())
            self._zf.writestr("xl/styles.xml", _STYLES)
        except Exception as exc:                        # noqa: BLE001
            raise XlsxStreamError("XLSX 写出失败：%s" % exc)

    # ---- 包内部件 ----

    def _content_types(self):
        overrides = []
        for i in range(1, len(self._names) + 1):
            overrides.append(
                '<Override PartName="/xl/worksheets/sheet%d.xml" '
                'ContentType="application/vnd.openxmlformats-officedocument.'
                'spreadsheetml.worksheet+xml"/>' % i)
        return _CONTENT_TYPES_HEAD + "".join(overrides) + "</Types>"

    def _workbook(self):
        sheets = []
        for i, name in enumerate(self._names, start=1):
            # [表名截断 2026-11-16 审计] Excel 表名上限 **31 字节**，且
            # 原来先转义再截 ⇒ 截断点落在 `&amp;`/`&lt;` 中间会把实体截成
            # 半截 ⇒ 非法 XML、Excel 打不开包。⇒ 先转义，再从 31 字节往
            # 回找到**不在实体中间**的安全长度（每回退一个原字符重转义，
            # 名字 ≤31 字符时零开销——当前表名固定"历史数据"）。
            escaped = _xml_escape(name[:31])
            if len(escaped) > 31:
                cut = min(len(name), 31)
                while cut > 0:
                    candidate = _xml_escape(name[:cut])
                    if len(candidate) <= 31:
                        escaped = candidate
                        break
                    cut -= 1
            sheets.append('<sheet name="%s" sheetId="%d" r:id="rId%d"/>'
                          % (escaped, i, i))
        return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                '<workbook xmlns="http://schemas.openxmlformats.org/'
                'spreadsheetml/2006/main" xmlns:r="http://schemas.'
                'openxmlformats.org/officeDocument/2006/relationships">'
                "<sheets>%s</sheets></workbook>" % "".join(sheets))

    def _workbook_rels(self):
        rels = []
        for i in range(1, len(self._names) + 1):
            rels.append('<Relationship Id="rId%d" Type="http://schemas.'
                        'openxmlformats.org/officeDocument/2006/relationships/'
                        'worksheet" Target="worksheets/sheet%d.xml"/>' % (i, i))
        rels.append('<Relationship Id="rId99" Type="http://schemas.'
                    'openxmlformats.org/officeDocument/2006/relationships/'
                    'styles" Target="styles.xml"/>')
        return ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                '<Relationships xmlns="http://schemas.openxmlformats.org/'
                'package/2006/relationships">%s</Relationships>'
                % "".join(rels))

    # ---- 写数据 ----

    def sheet(self, index, headers):
        """开一张表并写表头行（每张表只能开一次）。"""
        self._check_open()
        if index in self._writers:
            raise XlsxStreamError("第 %d 张表已开过" % (index + 1))
        if index >= len(self._names):
            raise XlsxStreamError("表序号越界：%d" % index)
        try:
            writer = self._zf.open("xl/worksheets/sheet%d.xml" % (index + 1),
                                   "w")
            writer.write('<?xml version="1.0" encoding="UTF-8" standalone='
                         '"yes"?><worksheet xmlns="http://schemas.'
                         'openxmlformats.org/spreadsheetml/2006/main">'
                         "<sheetData>".encode("utf-8"))
            self._writers[index] = writer
            self._row_no[index] = 0
            if headers:
                self.row(index, headers)
        except Exception as exc:                        # noqa: BLE001
            raise XlsxStreamError("写表头失败：%s" % exc)

    def row(self, index, values):
        """追加一行（**不缓存** ⇒ 行数不限、内存恒定）。"""
        self._check_open()
        writer = self._writers.get(index)
        if writer is None:
            raise XlsxStreamError("第 %d 张表还没开" % (index + 1))
        self._row_no[index] += 1
        payload = _row_xml(self._row_no[index], values)
        try:
            writer.write(payload.encode("utf-8"))
        except Exception as exc:                        # noqa: BLE001
            raise XlsxStreamError("写行失败：%s" % exc)

    def rows(self, index, batch):
        """追加一批行（list[list]）。"""
        for values in batch:
            self.row(index, values)

    def row_count(self, index):
        """该表已写行数（含表头行）。"""
        return self._row_no.get(index, 0)

    # ---- 收尾 ----

    def close(self):
        """收尾（写各表尾部 + 关闭 zip）。可重复调用。"""
        if self._closed:
            return
        self._closed = True
        if self._zf is None:
            return
        try:
            for index, writer in list(self._writers.items()):
                writer.write(b"</sheetData></worksheet>")
                writer.close()
            self._writers.clear()
            self._zf.close()
        except Exception as exc:                        # noqa: BLE001
            raise XlsxStreamError("收尾失败：%s" % exc)

    def abort(self):
        """放弃（出错时调用）：关闭 zip，不写各表尾部 ⇒ 文件不完整。"""
        if self._closed:
            return
        self._closed = True
        for writer in list(self._writers.values()):
            try:
                writer.close()
            except Exception:                           # noqa: BLE001
                pass
        self._writers.clear()
        if self._zf is not None:
            try:
                self._zf.close()
            except Exception:                           # noqa: BLE001
                pass

    def _check_open(self):
        if self._closed:
            raise XlsxStreamError("写出器已关闭")
