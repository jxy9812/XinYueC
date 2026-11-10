/**
 * @file       HsfExport.c
 * @brief      HSF 客户端表格导出模块实现（CSV / 真 XLSX）。
 * @details    取数与落盘解耦：页面给一张 grid（表头 + 取格回调），模块负责
 *              按格式写出文件。
 *              - CSV：UTF-8 BOM + CRLF 行分隔 + 最小化引号（与三页既有
 *                histWriteCsv / logWriteCsv 同口径；引号加倍规则同 Python
 *                端 csv.writer 的 QUOTE_MINIMAL）。
 *              - XLSX：框架 XExcel 原生 API（行列 1 基，见 XExcelTest.c）。
 */
/* [必须最先包含] XExcel 全体头（XDocument.h / XWorksheet.h / XAbstractSheet.h
 *  等）的正文都包在 `#if XEXCEL_ON` 里，而该宏由 CXinYueConfig.h 定义；而
 *  这些头的**包含守卫又在宏判断之外** —— 若在 XEXCEL_ON 尚未定义时先包含
 *  XAbstractSheet.h，守卫会被定死而正文被裁空，之后再包含 XWorksheet.h 就
 *  报「struct XAbstractSheet 未定义」。故配置宏必须先引进来。
 *  （同款坑与修法见 client_py/tools/xlsx_bridge/xlsx_bridge.c 头部注释。） */
#include "CXinYueConfig.h"
#include "XMemory.h"

#include "HsfExport.h"

#include "XAbstractSheet.h"
#include "XByteArray.h"
#include "XClass.h"
#include "XDevice.h"
#include "XDocument.h"
#include "XIODevice.h"
#include "XString.h"
#include "XWorksheet.h"

#include <stdio.h>
#include <string.h>

/* ==================== 路径 ==================== */

/** @brief path 前缀 + 扩展名 → 最终落地路径。 */
static bool exportJoinExt(const char* prefix, const char* ext,
                          char* out, int cap)
{
    int n;
    if (!prefix || !prefix[0] || !out || cap <= 0) return false;
    n = snprintf(out, (size_t)cap, "%s%s", prefix, ext);
    return n > 0 && n < cap;
}

bool HsfExport_outputPath(const HsfExportGrid* grid, char* out, int cap)
{
    if (!grid || !out || cap <= 0) return false;
    return exportJoinExt(grid->path,
                         (grid->format == HsfExportFormat_Xlsx)
                             ? ".xlsx" : ".csv",
                         out, cap);
}

HsfExportFormat HsfExport_formatFromBarIndex(int index)
{
    /* HsfDataBar 下拉项顺序：[Excel (.xlsx), CSV (.csv)] ⇒ 0=XLSX、1=CSV，
     * 与 HsfExportFormat 的数值正好相反，接线处必须走本函数映射。 */
    if (index == 0) return HsfExportFormat_Xlsx;
    return HsfExportFormat_Csv;
}

/* ==================== CSV ==================== */

/** @brief 该格是否需要加引号（含逗号 / 双引号 / 换行之一即需要）。 */
static bool csvNeedsQuote(const char* text)
{
    const char* p;
    if (!text) return false;
    for (p = text; *p; ++p)
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r')
            return true;
    return false;
}

/** @brief 追加一个 CSV 单元格（按需加引号；格内双引号写成两个）。 */
static bool csvAppendCell(XByteArray* row, const char* text)
{
    const char* seg;
    const char* p;
    if (!row || !text || !*text) return true;  /* 空串：无内容可写 */
    if (!csvNeedsQuote(text))
        return XByteArray_append_2((XVector*)row, text, strlen(text));
    if (!XByteArray_append_2((XVector*)row, "\"", 1)) return false;
    seg = text;
    for (p = text; *p; ++p) {
        if (*p != '"') continue;
        if (p > seg &&
            !XByteArray_append_2((XVector*)row, seg, (size_t)(p - seg)))
            return false;
        if (!XByteArray_append_2((XVector*)row, "\"\"", 2)) return false;
        seg = p + 1;
    }
    if (p > seg &&
        !XByteArray_append_2((XVector*)row, seg, (size_t)(p - seg)))
        return false;
    return XByteArray_append_2((XVector*)row, "\"", 1);
}

/** @brief 行缓冲写盘并清空（清空保留容量）。 */
static bool csvFlushRow(XFd fd, XByteArray* row)
{
    size_t n = XByteArray_size_base((const XContainer*)row);
    bool ok = true;
    if (n > 0)
        ok = (XDevice_write(fd, XByteArray_data(row), (int64_t)n) == (int64_t)n);
    XByteArray_clear_base((XContainer*)row);
    return ok;
}

/** @brief 写 CSV 表（逐行拼好再落盘，行内不多次写文件）。 */
static bool csvWriteGrid(const HsfExportGrid* grid, const char* path)
{
    XDeviceOpenOptions opts;
    XString* target;
    XFd fd;
    XByteArray* row;
    int err = 0;
    int r;
    int c;
    char cell[HSF_EXPORT_CELL_CAP];
    bool ok = true;

    target = XString_create_utf8(path);
    if (!target) return false;
    memset(&opts, 0, sizeof(opts));
    opts.m_openMode = XIODevice_WriteOnly | XIODevice_Truncate;
    opts.m_target = target;
    fd = XDevice_open(XDeviceType_File, &opts, &err);
    XClassDelete(target);
    if (fd == XFD_INVALID) return false;

    row = XByteArray_create();
    if (!row) {
        XDevice_close(fd);
        return false;
    }

    /* UTF-8 BOM：Excel 直接双击打开不乱码（与 Python 端 utf-8-sig 同口径）。 */
    if (XDevice_write(fd, "\xEF\xBB\xBF", 3) != 3) ok = false;

    /* 表头行 */
    if (ok && grid->header) {
        for (c = 0; c < grid->colCount; ++c) {
            if (c > 0 && !XByteArray_append_2((XVector*)row, ",", 1)) {
                ok = false;
                break;
            }
            if (!csvAppendCell(row, grid->header[c])) {
                ok = false;
                break;
            }
        }
        if (ok && !XByteArray_append_2((XVector*)row, "\r\n", 2)) ok = false;
        if (ok) ok = csvFlushRow(fd, row);
    }

    /* 数据行 */
    for (r = 0; ok && r < grid->rowCount; ++r) {
        for (c = 0; c < grid->colCount; ++c) {
            if (c > 0 && !XByteArray_append_2((XVector*)row, ",", 1)) {
                ok = false;
                break;
            }
            cell[0] = '\0';
            if (grid->cell &&
                grid->cell(grid->user, r, c, cell, (int)sizeof(cell))) {
                cell[sizeof(cell) - 1] = '\0';
                if (!csvAppendCell(row, cell)) {
                    ok = false;
                    break;
                }
            }
        }
        if (!ok) break;
        if (!XByteArray_append_2((XVector*)row, "\r\n", 2)) {
            ok = false;
            break;
        }
        ok = csvFlushRow(fd, row);
    }

    XClassDelete(row);
    XDevice_close(fd);
    return ok;
}

/* ==================== XLSX ==================== */

/** @brief 取一格文本（回调无内容 ⇒ 空串）。 */
static void xlsxCellText(const HsfExportGrid* grid, int r, int c,
                          char* buf, int cap)
{
    buf[0] = '\0';
    if (grid->cell && grid->cell(grid->user, r, c, buf, cap)) {
        buf[cap - 1] = '\0';
        return;
    }
    buf[0] = '\0';
}

/** @brief 写 XLSX 表（框架 XExcel 原生 API，行列 1 基）。 */
static bool xlsxWriteGrid(const HsfExportGrid* grid, const char* path)
{
    XDocument* doc;
    XWorksheet* ws;
    const char* sheetName;
    char cell[HSF_EXPORT_CELL_CAP];
    int row;
    int r;
    int c;
    bool ok = true;

    doc = XDocument_create();
    if (!doc) return false;

    /* [单表口径] XDocument_create() 自带一张 "Sheet1"：改名比另加一张干净
     * —— 成品里就一张表，包内是 xl/worksheets/sheet1.xml，数据就在它里面。
     * 改名失败（重名 / 非法表名字符）才另加一张；都失败就退回默认表名。 */
    sheetName = (grid->sheetName && grid->sheetName[0]) ? grid->sheetName
                                                        : "Sheet1";
    if (strcmp(sheetName, "Sheet1") != 0) {
        if (!XDocument_renameSheet_utf8(doc, "Sheet1", sheetName) &&
            !XDocument_addSheet_utf8(doc, sheetName,
                                     XAbstractSheet_ST_WorkSheet))
            sheetName = "Sheet1";
    }
    if (!XDocument_selectSheet_utf8(doc, sheetName)) {
        XDocument_delete(doc);
        return false;
    }
    ws = XDocument_currentWorksheet(doc);
    if (!ws) {
        XDocument_delete(doc);
        return false;
    }

    /* [行列 1 基] XWorksheet 原生口径（见 Test/XDataTest/XExcelTest.c：
     * writeString_utf8(summary, 1, 1, ...)）。 */
    row = 1;
    if (grid->header) {
        for (c = 0; c < grid->colCount; ++c) {
            const char* h = grid->header[c] ? grid->header[c] : "";
            if (!XWorksheet_writeString_utf8(ws, row, c + 1, h, NULL))
                ok = false;
        }
        row++;
    }
    for (r = 0; ok && r < grid->rowCount; ++r) {
        for (c = 0; c < grid->colCount; ++c) {
            xlsxCellText(grid, r, c, cell, (int)sizeof(cell));
            /* 空格跳过（与 Python 端 xlsx_writer.write_rows 同口径：空值不
             * 建格，视觉即空单元）。 */
            if (cell[0] == '\0') continue;
            if (!XWorksheet_writeString_utf8(ws, row, c + 1, cell, NULL))
                ok = false;
        }
        row++;
    }

    if (ok) ok = XDocument_saveAs_utf8(doc, path);
    XDocument_delete(doc);   /* 失败路径也要释放，别泄漏。 */
    return ok;
}

/* ==================== 对外入口 ==================== */

bool HsfExport_write(const HsfExportGrid* grid)
{
    char path[512];
    if (!grid) return false;
    if (grid->colCount <= 0) return false;
    if (grid->rowCount < 0) return false;
    if (!HsfExport_outputPath(grid, path, (int)sizeof(path))) return false;
    if (grid->format == HsfExportFormat_Xlsx)
        return xlsxWriteGrid(grid, path);
    return csvWriteGrid(grid, path);
}
