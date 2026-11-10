/**
 * @file       HsfExport.h
 * @brief      HSF 客户端表格导出模块（CSV / 真 XLSX 共用一套取数口径）。
 * @details    历史页 / 日志页 / 报警页三页的「导出」共用本模块：
 *              - CSV：与三页既有 histWriteCsv / logWriteCsv / alarmExportCsv
 *                同口径（UTF-8 BOM + CRLF 行分隔 + 最小化引号 + 引号加倍）。
 *              - XLSX：走框架 XExcel（Src/XCore/XData/XExcel）原生 API ——
 *                Python 端 xlsx_bridge.c 封的正是同一套 API，故 C 端不需要
 *                任何桥接动态库。
 *             取数与落盘解耦：模块只管「怎么写文件」，数据从哪来由页面经
 *             回调喂进来。
 */
#ifndef HSF_EXPORT_H
#define HSF_EXPORT_H

#include "CXinYueConfig.h"  /* 模块开关（XEXCEL_ON 等）统一在此引入。 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单元格文本缓冲区容量（字节，含结尾 NUL）。 */
#define HSF_EXPORT_CELL_CAP 1024

/** @brief 导出格式。 */
typedef enum HsfExportFormat {
    HsfExportFormat_Csv  = 0,  /**< CSV（UTF-8 BOM；Excel 双击直开不乱码）。 */
    HsfExportFormat_Xlsx = 1   /**< Excel 工作簿（走框架 XExcel）。 */
} HsfExportFormat;

/**
 * @brief 取单元格文本（UTF-8；写入 buf）。
 * @param user 回调上下文（grid->user 原样回传）。
 * @param row  数据行号（0 基；不含表头行）。
 * @param col  列号（0 基）。
 * @param buf  文本缓冲（容量 cap 字节；模块已清零，回调只管写）。
 * @param cap  buf 容量（字节）。
 * @return true=取到；false=该格无内容（导出时写空串）。
 */
typedef bool (*HsfExportCellFn)(void* user, int row, int col,
                                char* buf, int cap);

/**
 * @brief 一张待导出的表（表头 + 数据格）。
 */
typedef struct HsfExportGrid {
    const char*       path;      /**< 输出路径前缀（UTF-8，无扩展名）。 */
    const char*       sheetName; /**< XLSX 工作表名（CSV 忽略；NULL=默认名）。 */
    HsfExportFormat   format;    /**< 输出格式。 */
    int               colCount;  /**< 列数（>=1）。 */
    int               rowCount;  /**< 数据行数（不含表头行；>=0）。 */
    const char* const* header;   /**< 表头（可为 NULL ⇒ 不写表头行）。 */
    HsfExportCellFn   cell;      /**< 取单元格文本（可为 NULL ⇒ 全空）。 */
    void*             user;      /**< 回调上下文。 */
} HsfExportGrid;

/**
 * @brief 按 grid 导出（CSV 或 XLSX）。
 * @param grid 待导出的表。
 * @return 成功返回 true；失败返回 false（调用方负责提示）。
 * @note 落地文件名 = grid->path 前缀 + 扩展名（.csv / .xlsx）。
 */
bool HsfExport_write(const HsfExportGrid* grid);

/**
 * @brief 拼出本次导出的最终落地路径（path 前缀 + 扩展名）。
 * @param grid 待导出的表。
 * @param out  输出缓冲。
 * @param cap  out 容量（字节）。
 * @return 成功返回 true；失败返回 false（out 未被改写）。
 */
bool HsfExport_outputPath(const HsfExportGrid* grid, char* out, int cap);

/**
 * @brief 工具条「导出格式」下拉索引 → 导出格式。
 * @param index HsfDataBar_exportFormatIndex() 的返回值。
 * @return 对应的导出格式；索引越界按 CSV 处理。
 * @details HsfDataBar 的下拉项顺序是 [Excel (.xlsx), CSV (.csv)]，即索引
 *          0 = XLSX、1 = CSV —— 与 HsfExportFormat 的数值正好相反。接线处
 *          务必走本函数映射，别把下拉索引直接当枚举值用。
 */
HsfExportFormat HsfExport_formatFromBarIndex(int index);

#ifdef __cplusplus
}
#endif

#endif /* HSF_EXPORT_H */
