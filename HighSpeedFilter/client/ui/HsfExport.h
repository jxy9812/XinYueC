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
 *             两种用法：
 *              - 一次性（HsfExport_write）：数据已齐（一张 grid 全量给出，
 *                报警页 ≤200 行的本地数据用这个）。
 *              - 流式（HsfExportWriter_*）：数据按批到达（历史/日志页的分页
 *                全量导出），open 时写表头、每批 writeRow、finish 收尾——
 *                CSV 常驻内存 O(1)，XLSX 文档在 finish 时才 saveAs。
 */
#ifndef HSF_EXPORT_H
#define HSF_EXPORT_H

#include "CXinYueConfig.h"  /* 模块开关（XEXCEL_ON 等）统一在此引入。 */
#include "XTypes.h"         /* XFd（写出器的 CSV 文件句柄字段）。 */

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
 * @brief 文件对话框过滤器索引 → 导出格式。
 * @param index XFileDialog_getSaveFileName_2 的 selectedFilterIndex 输出值。
 * @return 对应的导出格式；索引越界按 CSV 处理。
 * @details 三页导出走框架文件对话框（XFileDialog_getSaveFileName_2），
 *          过滤器串是 "Excel 工作簿 (*.xlsx);;CSV (*.csv)"（与 Python
 *          客户端 pages_other.py 同串同序）——即索引 0 = XLSX、1 = CSV，
 *          与 HsfExportFormat 的数值正好相反。接线处务必走本函数映射，
 *          别把过滤器索引直接当枚举值用。
 */
HsfExportFormat HsfExport_formatFromFilterIndex(int index);

/**
 * @brief 就地去掉路径尾部已知的导出扩展名（.csv / .xlsx，不分大小写）。
 * @param path 路径缓冲（调用方提供、可改写；NULL 忽略）。
 * @note  对话框回传的路径可能带用户选/输入的扩展名；写盘前剥掉它，
 *        让写出器按**定格格式**追加规范扩展名——否则选了 Excel 过滤器
 *        却输入 "x.csv" 会落成 "x.csv.xlsx"。无尾缀时原样不动。
 */
void HsfExport_stripKnownExt(char* path);

/** @brief 写出器的落地路径缓冲容量（字节，含结尾 NUL）。 */
#define HSF_EXPORT_PATH_CAP 512

/* XExcel 不透明类型前向声明（完整定义在 XDocument.h / XWorksheet.h；
 * 同款模式见 XWorkbook.h:33——本头文件只存指针，不展开其定义）。 */
typedef struct XDocument  XDocument;
typedef struct XWorksheet XWorksheet;

/**
 * @brief 行式导出写出器（增量写：一批一行地追加，落盘与取数解耦）。
 * @warning 用法是固定时序 open → writeRow × N → finish/abort：
 *          - open 写 BOM（CSV）/建文档与表（XLSX）+ 表头行；
 *          - writeRow 逐行追加（cell 回调口径与 HsfExportGrid 同款）；
 *          - finish 收尾（XLSX 此刻才 saveAs；CSV 关闭句柄）或
 *            abort 中途放弃（关闭并尽力删除半成品 CSV；XLSX 直接丢弃，
 *            盘上不留任何东西）。
 *          结构体由调用方分配（栈/成员均可，先整体清零）；finish 或
 *          abort 之后方可再次 open（复用同一份存储）。
 */
typedef struct HsfExportWriter {
    HsfExportFormat m_format;   /**< 输出格式（open 时定格）。 */
    int             m_colCount; /**< 列数（open 时定格）。 */
    bool            m_open;     /**< open 成功且未 finish/abort。 */
    bool            m_failed;   /**< 任一次写失败（finish 返回 false）。 */
    XFd             m_fd;       /**< CSV 文件句柄（XLSX 忽略）。 */
    XByteArray*     m_row;      /**< CSV 行缓冲（XLSX 忽略）。 */
    XDocument*      m_doc;      /**< XLSX 文档（CSV 忽略）。 */
    XWorksheet*     m_ws;       /**< XLSX 当前工作表（CSV 忽略）。 */
    int             m_nextRow;  /**< XLSX 下一数据行（1 基；CSV 忽略）。 */
    char            m_path[HSF_EXPORT_PATH_CAP]; /**< 最终落地路径。 */
} HsfExportWriter;

/**
 * @brief 打开写出器并写表头行。
 * @param w         写出器（调用方先 memset 清零）。
 * @param format    输出格式。
 * @param pathPrefix 落地路径前缀（**不含扩展名**；可先用
 *                  HsfExport_stripKnownExt 剥掉对话框回传的尾缀）。
 * @param sheetName XLSX 工作表名（CSV 忽略；NULL/空=默认名）。
 * @param header    表头（NULL=不写表头行）。
 * @param colCount  列数（>=1）。
 * @return 成功返回 true；失败返回 false（无需/不可再 abort）。
 */
bool HsfExportWriter_open(HsfExportWriter* w, HsfExportFormat format,
                          const char* pathPrefix, const char* sheetName,
                          const char* const* header, int colCount);

/**
 * @brief 追加一行（行内各格经 cell 回调取文本；返回 false 的格写空串）。
 * @param w    已 open 的写出器。
 * @param row  本批内的数据行号（0 基，原样回传给 cell；页按自己那批
 *             应答的行下标传）。
 * @param cell 取格回调（同 HsfExportCellFn；可为 NULL ⇒ 整行空串）。
 * @param user 回调上下文。
 * @return 本行写成功返回 true；失败返回 false（m_failed 置真，可继续
 *         追加但 finish 必返回 false）。
 */
bool HsfExportWriter_writeRow(HsfExportWriter* w, int row,
                              HsfExportCellFn cell, void* user);

/**
 * @brief 成功收尾：XLSX 此刻 saveAs 落盘；CSV 关闭句柄。
 * @return 全程无写失败返回 true；失败返回 false（半成品留在盘上，
 *         调用方可再调 abort 清理）。
 */
bool HsfExportWriter_finish(HsfExportWriter* w);

/**
 * @brief 中途放弃：关闭句柄/丢弃文档，并尽力删除半成品 CSV；
 *        XLSX 未 saveAs 盘上本就无文件。之后写出器回到未打开态。
 */
void HsfExportWriter_abort(HsfExportWriter* w);

/** @brief 最终落地路径（借用；未 open 时返回空串）。 */
const char* HsfExportWriter_path_const(const HsfExportWriter* w);

#ifdef __cplusplus
}
#endif

#endif /* HSF_EXPORT_H */
