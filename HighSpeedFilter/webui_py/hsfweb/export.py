# -*- coding: utf-8 -*-
"""数据导出：**按筛选全量**批取 + CSV/XLSX 落盘（零第三方依赖）。

[为什么导出不能只导"当前页"] 表格是分页的：界面只显示当前页那几十行；
导出必须是**筛选命中的全部行**（用户口径 2026-11-11）。直接导当前页 ⇒
导出的行数比用户以为的少一个数量级，而且**看不出少了**。

[口径与两端一致（client hsf_export_plan.py / C HsfExportPlan 同款）]
    · 批行数 40（EXPORT_CHUNK）：与页大小档 10/20/50、图表批 50
      **两两不相交** ⇒ 在途应答不会被认错来源；
    · 逐批**连续**取（步长=本批实到行数）⇒ 跨页不重不漏，不抽样；
    · 整轮钉**同一个 beforeTs 锚点**（每批各取"当下"会让窗口上界逐批
      增长 ⇒ 顺序取必重复/漏行）；
    · 结束判据 = 短批 / 空批 / 写满服务端报的总行数；
    · **不限行数**（2026-11-14 用户口径「不管多大的数据都可以导出」）：
      CSV 与 XLSX 都**流式写**（边取边写）⇒ 内存恒定 ⇒ 没有内存闸，
      也就没有截断。

[CSV 字节口径（与 C 端 HsfExport 字节级兼容）]
    UTF-8 BOM (EF BB BF) + CRLF 行尾 + 最小引号转义：
    仅当字段含 `"` `,` CR LF 之一才加引号，内部 `"` 翻倍。

[XLSX 产出路径]
    · **导出端点走流式写出器**（`xlsx_stream.XlsxStreamWriter`）：每批行直接
      写进 zip 的 sheet part ⇒ 行数不限、内存恒定、每批只花几毫秒 ⇒
      **事件循环不被卡住**（这是硬要求：旧的「攒齐再一次性拼」要 40+ 秒纯
      CPU，卡住事件循环 ⇒ 心跳停发 ⇒ 上游判线断链 ⇒ 导出完主链路已死、
      后续点动/配置全 503，2026-11-13 实锅）。
    · **一次性小批量**仍可用 `write_xlsx(sheets, path)`（统一入口，内部按
      「C 动态库优先、纯 Python 回落」两条路产出合法 OOXML）。
      "C 路径实测成本模型 + 预算"择优：够快就用 C 路径，否则用纯 Python；
      缺库/写失败/探测失败一律**静默回落**（网页端不会 500、也不会因为
      "没 DLL"或"C 路径太慢"而导不出 Excel）。`last_writer()` /
      `last_writer_reason()` 报告本次走的哪条路与理由（供自检/日志）。
      ⚠️ **[2026-11-21 起：导出端点已不再用这条]** 历史/日志/报警的导出
      全部改走 `xlsx_stream.XlsxStreamWriter`（**流式**：行数不限、内存
      恒定、每批几毫秒）。本函数与 `build_xlsx_auto` 现在**全仓零调用方**
      （保留供一次性小批量/外部脚本用）。**别把导出改回这里**：它是
      「攒齐所有行再一次性拼」的路子 ⇒ 大表 40+ 秒纯 CPU 卡住事件循环 ⇒
      心跳停发 ⇒ 上游 6s 静默判线断链 ⇒ 导出完主链路已死（2026-11-13 实锅）。

    [为什么要"按成本择优"而不是"能用就用"]（2026-11-13 实测）C 路径的
    `xlsx_save` 成本随**格数平方**增长：3.2 万格要 1~2 分钟（HTTP 端到端
    实测 137 s ⇒ 浏览器超时），而纯 Python 路径同数据 0.05 s。写死
    "优先 C 路径"会让网页端的 Excel 导出在大表上直接卡死。
    成本模型见 `xlsx_native.estimate_save_seconds()`。
    **显式关掉 C 路径**：环境变量 `HSF_XLSX_NATIVE=0` ⇒ 恒走纯 Python
    （板端无 .so、或要复现纯 Python 路径时用；网页端随即静默回落，不 500）。
"""

import asyncio
import zipfile

# [数值格口径 2026-11-19] 单元格类型判定与流式写出器**同一份实现**（免得
# 网页端两条产出路径（C 动态库 / 纯 Python）出来的列类型不一样）。
from .xlsx_stream import _cell_xml

#: 导出批行数（行）。取服务端 HISTORY_PAGE_MAX=50 上限：
#: [2026-11-22 40→50] 35 万行时批数 ≈ 7,000（40 行/批则 ≈8,800）⇒
#: 每批 6~7ms 的往返成本直接省 20%。[旧注「与页大小档两两不相交」已不
#: 成立] 请求按 requestId 归属（XLink 层），应答不会认错来源 —— 那是
#: 无 requestId 的老协议顾虑。
EXPORT_CHUNK = 50

#: C 动态库路径的单次 save 预算（秒）。**超预算就不用 C 路径**——
#: C 侧 XExcel 的 save 成本随格数平方增长（实测 3.2 万格 1~2 分钟），
#: 纯 Python 路径同数据 0.05 s 且同样是合法 OOXML。
NATIVE_BUDGET_S = 12.0

#: 成本预测的安全倍数（实测 C 路径在大表上比二次模型更陡 ⇒ 宁可保守）。
NATIVE_SAFETY = 2.0

# [2026-11-18 删死代码] 原先这里有 `EXPORT_MAX_ROWS = 12000` 与
# `plan_batches()`（带"超上限截断"的口径）。**网页端早已改成流式导出、
# 不限行数**（用户 2026-11-14 口径「不管多大的数据都可以导出」），这两个
# 符号在本包里**无人调用** ⇒ 留着只会让人以为还有 12000 行上限
# （界面文案也照抄过这个假上限）。⇒ 一并删除，别复活。


# ============================================================================
#                              CSV（字节口径）
# ============================================================================

#: UTF-8 BOM：Excel 靠它认出 UTF-8（双击直开不乱码）。
CSV_BOM = b"\xef\xbb\xbf"


def csv_field(value):
    """单字段 → CSV 文本（None/缺 → 空串；最小引号转义）。"""
    if value is None:
        return ""
    text = value if isinstance(value, str) else str(value)
    if any(ch in text for ch in ('"', ",", "\r", "\n")):
        return '"%s"' % text.replace('"', '""')
    return text


def csv_line(fields):
    """一行字段 → 按 CRLF 结尾的 UTF-8 字节（含行尾）。"""
    text = ",".join(csv_field(f) for f in fields)
    return (text + "\r\n").encode("utf-8")


# ============================================================================
#                     历史行格式化（与客户端 _server_row_texts 同口径）
# ============================================================================

#: 历史表**显示列** 16（与 server store.DISPLAY_COLUMNS /
#: client HistoryPage.COLUMNS / C 端 kHistoryHeaders 三处同序）。
#: 线上列序另有一套（proto.HISTORY_COLUMNS 19 列，water_temp 在末尾）。
HISTORY_HEADERS = (
    "时间", "模式", "搅拌状态", "输出频率(Hz)", "输出电流(A)",
    "搅拌转速(rpm)", "浊度1(NTU)", "浊度2(NTU)", "水温(℃)",
    "瞬时流量", "正累计", "压差(MPa)",
    "电箱温度(℃)", "电箱湿度(%RH)", "DI(32位)", "DO(32位)")

#: 浊度越限阈值（与历史/水质页同一口径；仅异常行判定用）。
TURBIDITY_WARN_NTU = 20.0

#: 合法墙钟毫秒下界（2020-01-01），与服务端 store.VALID_TS_MIN 同值。
VALID_TS_MIN = 1577836800000


def _fmt_num(rec, key, fmt="%.2f"):
    value = rec.get(key)
    return "--" if value is None else fmt % float(value)


def _fmt_stamp(ms):
    """毫秒 → "YYYY-MM-DD HH:MM:SS" 本地时间（坏数据闸内才格式化）。"""
    import time as _time
    try:
        ms = int(ms)
    except (TypeError, ValueError):
        return "--"
    if ms < VALID_TS_MIN:
        return "--"
    return _time.strftime("%Y-%m-%d %H:%M:%S", _time.localtime(ms / 1000.0))


def history_row_texts(raw, cols):
    """服务端一行（按 cols 顺序的值数组）→ (显示文本列表, 是否异常)。

    [为什么在服务端（web）做格式化] 库里存的是原始值，导出文件要给
    人/Excel 看，格式化口径必须与界面一致（数值列两位小数、转速零位、
    位图 0x%08X、流量带单位、压差优先说明文字、NULL → "--"）。
    与 client pages_other.HistoryPage._server_row_texts 逐条同口径。
    """
    rec = dict(zip(cols, raw))
    ts = rec.get("ts")
    stamp = _fmt_stamp(ts) if ts else "--"
    flow = rec.get("flow")
    flow_unit = rec.get("flow_unit") or ""
    total = rec.get("flow_total")
    total_unit = rec.get("total_unit") or ""
    press_note = rec.get("press_note") or ""
    di = rec.get("di")
    do = rec.get("do")
    vfd_state = rec.get("vfd_state") or "--"

    press_text = press_note or _fmt_num(rec, "press_mpa", "%.3f")
    texts = [
        stamp,
        rec.get("mode") or "--",
        vfd_state,
        _fmt_num(rec, "freq_hz"),
        _fmt_num(rec, "current_a"),
        _fmt_num(rec, "rpm", "%.0f"),
        _fmt_num(rec, "turb1"), _fmt_num(rec, "turb2"),
        # [水温] 显示在第 8 位（紧跟浊度2）；线上列序里它在末尾，
        # 此处按**列名**取值，位置无关。无温度机型该列入库 NULL ⇒ "--"。
        _fmt_num(rec, "water_temp", "%.1f"),
        "--" if flow is None else "%.2f %s" % (float(flow), flow_unit),
        "--" if total is None else "%.2f %s" % (float(total), total_unit),
        press_text,
        _fmt_num(rec, "temp", "%.1f"), _fmt_num(rec, "hum", "%.1f"),
        "--" if di is None else "0x%08X" % (int(di) & 0xFFFFFFFF),
        "--" if do is None else "0x%08X" % (int(do) & 0xFFFFFFFF),
    ]
    abnormal = ("故障" in vfd_state
                or bool(press_note)
                or (rec.get("turb1") is not None
                    and float(rec["turb1"]) >= TURBIDITY_WARN_NTU)
                or (rec.get("turb2") is not None
                    and float(rec["turb2"]) >= TURBIDITY_WARN_NTU))
    return texts, abnormal


def log_row_texts(row):
    """日志一条（{ts,category,text}）→ 显示文本列表。"""
    return [_fmt_stamp(row.get("ts")),
            str(row.get("category", "") or ""),
            str(row.get("text", "") or "")]


#: 日志导出表头。
#:
#: [类别列导原文] conn/ctrl/alarm/auto/auth(/sys) 与界面下拉、库内取值
#: 同一字面，可与库对账；中文映射只用于界面显示，不进导出文件。
LOG_HEADERS = ("时间", "类别", "事件")


# ============================================================================
#                          导出流（async 生成器）
# ============================================================================

def export_filename(base, ext):
    """导出文件名：hsf_<what>_<YYYYMMDD-HHMMSS>.<ext>。"""
    import time as _time
    return "%s_%s.%s" % (base, _time.strftime("%Y%m%d-%H%M%S"), ext)


async def export_stream(fetch_batch, headers, rows_to_texts, fmt,
                        sheet_name="数据", first_doc=None):
    """按筛选全量导出（async 生成器，逐块 yield 字节）。

    :param fetch_batch: ``async fetch_batch(offset, limit) -> dict``——发一批
        请求（**必须已钉同一 beforeTs 锚点、带 export=true**，由调用方保证），
        返回服务端应答 doc。
    :param headers: 表头（元组/列表）。
    :param rows_to_texts: ``(row) -> list[str]``——一行原始数据 → 显示文本。
        历史行是 (texts, abnormal) 二元组时只取 texts。
    :param fmt: "csv" | "xlsx"。
    :param first_doc: 探测批应答（api 层先取的第一批；None 则本函数自取）。
        [为什么有探测批] 链路断/权限被拒要在 HTTP 响应头之前暴露成 JSON
        错误，不让浏览器收到半个坏文件才发现失败；第一批数据在这里消费，
        不会重复取。
    :returns: yields (bytes chunk)；调用方把它包成 HTTP 流式应答。

    [结束判据] 短批（got<limit）/ 空批 / 写满服务端 total（total=-1 表示
    未知，靠短批收口）。
    [2026-11-22 订正] XLSX 走 `xlsx_stream.XlsxStreamWriter` **流式写出、
    行数不限、不截断**（旧的 12000 上限已删）；「超上限截断并在导出说明表
    写明」是过期口径，别再按它理解本函数。
    """
    fmt = str(fmt or "csv").lower()
    if fmt not in ("csv", "xlsx"):
        raise ValueError("不支持的导出格式：%s" % fmt)

    count = 0
    total = 0
    got = 0
    doc = first_doc

    if fmt == "csv":
        yield CSV_BOM + csv_line(headers)
        offset = 0
        while True:
            if doc is None:
                doc = await fetch_batch(offset, EXPORT_CHUNK)
            _require_ok(doc)
            rows = doc.get("rows") or []
            if count == 0:
                total = _int_of(doc.get("total"))
            for row in rows:
                texts = rows_to_texts(row)
                if isinstance(texts, tuple):
                    texts = texts[0]
                yield csv_line(texts)
                count += 1
            got = len(rows)
            if (got <= 0 or got < EXPORT_CHUNK
                    or (total > 0 and count >= total)):
                return
            # [连续取] 步长 = 本批实到行数 ⇒ 服务端少给几行时也不跳行。
            offset += got
            doc = None

    # ---- XLSX：**流式写出**（行数不限 + 不卡事件循环）----
    # [为什么不用「攒齐再一次性拼」] 纯 Python 拼 7000 行 × 19 列要 40+ 秒
    # 纯 CPU；直接跑在事件循环里 ⇒ 心跳停发 ⇒ 上游按「静默 6s」判线断链 ⇒
    # 导出刚结束主链路已死，后续点动/配置全 503（2026-11-13 实锅：smoke
    # backend 三条红）。流式写法每批只花几毫秒 ⇒ 事件循环不卡 ⇒ 心跳照发。
    # [为什么不限行数] 用户 2026-11-14 口径「不管多大的数据都可以导出」：
    # 流式写内存恒定（只有 zip 压缩缓冲）⇒ 没有内存闸，也就不必截断。
    import os
    import tempfile
    from .xlsx_stream import XlsxStreamWriter

    tmp_path = None
    writer = None
    offset = 0
    try:
        fd, tmp_path = tempfile.mkstemp(prefix="hsf_export_", suffix=".xlsx")
        os.close(fd)
        writer = XlsxStreamWriter(tmp_path, [sheet_name])
        writer.sheet(0, list(headers))
        while True:
            if doc is None:
                doc = await fetch_batch(offset, EXPORT_CHUNK)
            _require_ok(doc)
            rows = doc.get("rows") or []
            if count == 0:
                total = _int_of(doc.get("total"))
            for row in rows:
                texts = rows_to_texts(row)
                if isinstance(texts, tuple):
                    texts = texts[0]
                writer.row(0, texts)
                count += 1
            got = len(rows)
            if (got <= 0 or got < EXPORT_CHUNK
                    or (total > 0 and count >= total)):
                break
            # [连续取] 步长 = 本批实到行数 ⇒ 服务端少给几行时也不跳行。
            offset += got
            doc = None
        writer.close()
        writer = None
    except Exception:
        if writer is not None:
            try:
                writer.abort()
            except Exception:                           # noqa: BLE001
                pass
        if tmp_path and os.path.exists(tmp_path):
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
        raise
    # [分块回读] 不把整个文件塞进内存（几十 MB 的工作簿也能流给浏览器）。
    try:
        with open(tmp_path, "rb") as fh:
            while True:
                chunk = fh.read(256 * 1024)
                if not chunk:
                    break
                yield chunk
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass


def _require_ok(doc):
    """服务端应答校验（否定应答显式报错——不让半截文件装成功）。"""
    if not isinstance(doc, dict) or not doc.get("ok"):
        err = doc.get("err") if isinstance(doc, dict) else None
        raise RuntimeError("导出失败：%s" % (err or "服务端应答为空"))


def _int_of(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return 0


# ============================================================================
#                          XLSX（zipfile + SpreadsheetML 最小集）
# ============================================================================

def _xml_escape(text):
    """XML 文本转义 + 剔除 XML 1.0 不允许的控制字符（0x00-0x08 等会炸包）。"""
    out = []
    for ch in str(text):
        code = ord(ch)
        if code < 0x20 and ch not in ("\t", "\n", "\r"):
            continue
        out.append(ch)
    text = "".join(out)
    return (text.replace("&", "&amp;").replace("<", "&lt;")
            .replace(">", "&gt;"))


def _sheet_xml(headers, rows):
    """一张工作表的 sheetData（表头第 1 行 + 数据行；列号 A..）."""
    lines = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
             '<worksheet xmlns="http://schemas.openxmlformats.org/'
             'spreadsheetml/2006/main">',
             "<sheetData>"]
    all_rows = [list(headers)] + list(rows)
    for r, row in enumerate(all_rows, start=1):
        cells = []
        for c, value in enumerate(row, start=1):
            ref = "%s%d" % (_col_letter(c), r)
            # None/空串 → 空单元格（跳过，视觉即空）；数值列写数值格。
            if value is None or value == "":
                continue
            cells.append(_cell_xml(ref, value))
        lines.append('<row r="%d">%s</row>' % (r, "".join(cells)))
    lines.append("</sheetData></worksheet>")
    return "".join(lines).encode("utf-8")


def _col_letter(index):
    """1 基列号 → Excel 字母列名（1=A, 26=Z, 27=AA）。"""
    letters = ""
    while index > 0:
        index, rem = divmod(index - 1, 26)
        letters = chr(ord("A") + rem) + letters
    return letters


_CONTENT_TYPES = (
    '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/'
    'content-types">'
    '<Default Extension="rels" ContentType="application/vnd.'
    'openxmlformats-package.relationships+xml"/>'
    '<Default Extension="xml" ContentType="application/xml"/>'
    '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.'
    'openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
    '<Override PartName="/xl/styles.xml" ContentType="application/vnd.'
    'openxmlformats-officedocument.spreadsheetml.styles+xml"/>'
    "%s</Types>")

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


def build_xlsx(sheets):
    """sheets = [(name, headers, rows)] → XLSX 字节（ZIP_DEFLATED）。

    [最小可开] Excel/WPS 都认的最小包：ContentTypes + 根 rels + workbook +
    workbook rels + styles（最小）+ 每表一个 sheetN.xml（**数值格不带
    t 属性**：``<c r="D2"><v>-0.008</v></c>``；文本格走 inlineStr）。

    [数值格口径 2026-11-19] 与 ``xlsx_stream`` 同一份判定：纯数字字面量的
    显示文本（"2.29"）也写数值格 ⇒ 导出后 Excel 里能直接求和/排序
    （用户实报「数值列保存成 xlsx 后是文本型」）。带单位/十六进制/时间戳/
    "--" 的仍是文本；``bool`` 是文本 "TRUE"/"FALSE"；空值跳过。
    """
    sheet_overrides = []
    sheet_entries = []
    sheet_rels = []
    for i, (name, headers, rows) in enumerate(sheets, start=1):
        sheet_overrides.append(
            '<Override PartName="/xl/worksheets/sheet%d.xml" '
            'ContentType="application/vnd.openxmlformats-officedocument.'
            'spreadsheetml.worksheet+xml"/>' % i)
        sheet_entries.append(
            '<sheet name="%s" sheetId="%d" r:id="rId%d"/>'
            % (_xml_escape(name), i, i))
        sheet_rels.append(
            '<Relationship Id="rId%d" Type="http://schemas.openxmlformats.'
            'org/officeDocument/2006/relationships/worksheet" '
            'Target="worksheets/sheet%d.xml"/>' % (i, i))

    workbook = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/'
        '2006/main" xmlns:r="http://schemas.openxmlformats.org/'
        'officeDocument/2006/relationships"><sheets>%s</sheets></workbook>'
        % ("".join(sheet_entries)))

    workbook_rels = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/'
        '2006/relationships">%s'
        '<Relationship Id="rId99" Type="http://schemas.openxmlformats.org/'
        'officeDocument/2006/relationships/styles" Target="styles.xml"/>'
        "</Relationships>" % ("".join(sheet_rels)))

    import io as _io
    buf = _io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("[Content_Types].xml",
                    (_CONTENT_TYPES % "".join(sheet_overrides))
                    .encode("utf-8"))
        zf.writestr("_rels/.rels", _ROOT_RELS.encode("utf-8"))
        zf.writestr("xl/workbook.xml", workbook.encode("utf-8"))
        zf.writestr("xl/_rels/workbook.xml.rels", workbook_rels.encode("utf-8"))
        zf.writestr("xl/styles.xml", _STYLES.encode("utf-8"))
        for i, (_name, headers, rows) in enumerate(sheets, start=1):
            zf.writestr("xl/worksheets/sheet%d.xml" % i,
                        _sheet_xml(headers, rows))
    return buf.getvalue()


#: 上一次 `write_xlsx` 走的产出路径（"native" / "python"）。
_LAST_WRITER = "python"

#: 上一次走这条路的理由（自检/日志用）。
_LAST_REASON = ""


def last_writer():
    """上一次导出走的产出路径（"native"=C 动态库；"python"=纯 Python）。"""
    return _LAST_WRITER


def last_writer_reason():
    """上一次走这条路的理由（为什么回落/为什么用 C 路径）。"""
    return _LAST_REASON


def write_xlsx(sheets, path=None, prefer="auto"):
    """**统一入口**：网页端 XLSX 产出的唯一出口（两条路径在此汇合）。

    ⚠️ **[当前导出端点不用它]（2026-11-21 审计确认：全仓零调用方）**
       历史/日志/报警的导出走 `xlsx_stream.XlsxStreamWriter`（**流式**：
       行数不限、内存恒定）。本函数是「**攒齐所有行再一次性拼**」的路子
       ⇒ 大表 40+ 秒纯 CPU 卡住事件循环 ⇒ 心跳停发 ⇒ 上游 6s 静默判线
       断链 ⇒ 导出完主链路已死（2026-11-13 实锅）。
       ⇒ **只用于一次性小批量**；**别把导出端点改回这里**。

    :param sheets: ``[(表名, 表头, 数据行), ...]``。
    :param path: 落盘路径（None = 只返回字节）。
    :param prefer: "auto"（默认：C 路径**够快**就用，否则纯 Python）/
                   "native"（强制 C 路径；失败也静默回落）/
                   "python"（强制纯 Python）。
    :returns: XLSX 字节（合法 OOXML：zip 内含 [Content_Types].xml 与
              xl/workbook.xml）。

    [回落口径] 缺库/探测失败/写入失败 ⇒ **静默**落纯 Python 路径，
    不抛异常、不 500（网页端必须永远能导出 Excel）。`last_writer()` 与
    `last_writer_reason()` 报告本次实际走的路与理由。
    """
    global _LAST_WRITER, _LAST_REASON
    data, writer, reason = _produce_xlsx(sheets, prefer)
    _LAST_WRITER = writer
    _LAST_REASON = reason
    if path:
        with open(path, "wb") as fh:
            fh.write(data)
    return data


def _produce_xlsx(sheets, prefer):
    """择优产出 XLSX 字节 → (字节, 路径标记, 理由)。"""
    prefer = str(prefer or "auto").lower()
    if prefer == "python":
        return build_xlsx(sheets), "python", "纯 Python 路径（指定）"
    try:
        from . import xlsx_native
        if not xlsx_native.available():
            raise xlsx_native.XlsxNativeError("未找到 xlsx_bridge 动态库")
        estimate = xlsx_native.estimate_save_seconds(sheets)
        if prefer != "native" and \
                estimate * NATIVE_SAFETY > NATIVE_BUDGET_S:
            # [成本择优] C 路径预测超预算 ⇒ 走 O(n) 的纯 Python 路径
            #（同数据 0.05 s vs C 路径 1~2 分钟）。
            return (build_xlsx(sheets), "python",
                    "C 路径预测 save 耗时 %.1fs（预算 %.1fs，含 %.1f 倍安全系数）"
                    "⇒ 走纯 Python 路径"
                    % (estimate, NATIVE_BUDGET_S, NATIVE_SAFETY))
        data = xlsx_native.build_xlsx(sheets)
        if not data:
            raise xlsx_native.XlsxNativeError("C 路径产物为空")
        return data, "native", "C 动态库路径（save 预测 %.2fs）" % estimate
    except Exception as exc:                            # noqa: BLE001
        # C 路径不可用/出错/太慢 ⇒ 落纯 Python（网页端必须仍能导出 Excel）。
        return (build_xlsx(sheets), "python",
                "C 路径不可用（%s: %s）⇒ 回落纯 Python 路径"
                % (type(exc).__name__, exc))


def build_xlsx_auto(sheets):
    """产出 XLSX 字节（兼容旧调用名）——**统一入口 `write_xlsx` 的别名**。

    :param sheets: ``[(表名, 表头, 数据行), ...]``。
    :return: XLSX 字节（合法 OOXML：zip 内含 [Content_Types].xml 与
             xl/workbook.xml）。
    """
    return write_xlsx(sheets)
