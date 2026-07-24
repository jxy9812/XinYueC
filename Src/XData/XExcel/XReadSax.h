/******************************************************************************
 * @file       XReadSax.h
 * @brief      XReadSax SAX 流式单元格读取（对标 QXlsx::read_sax）
 * @author     XinYueC 团队
 * @note       提供基于 SAX 的流式单元格读取功能，避免加载整个文档到内存。
 *             对齐 QXlsx::read_sax 全部功能
 ******************************************************************************/
#ifndef XREADSAX_H
#define XREADSAX_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>

#include "XString.h"
#include "XCellLocation.h"

/* ========== 前向声明 ========== */
typedef struct XStringList XStringList;
typedef struct XReadSax_Options XReadSax_Options;
typedef bool (*XReadSax_CellCallback)(int row, int col, const char* value, const char* type, void* userData);

/* ========== 选项结构体 ========== */
struct XReadSax_Options {
    int m_minRow;              /**< 最小行号，0表示不限制 */
    int m_maxRow;              /**< 最大行号，0表示不限制 */
    int m_minCol;              /**< 最小列号，0表示不限制 */
    int m_maxCol;              /**< 最大列号，0表示不限制 */
    bool m_skipEmptyRows;      /**< 跳过空行 */
    bool m_skipEmptyColumns;   /**< 跳过空列（暂未实现）*/
};

/** @brief 默认 SAX 选项 */
extern const XReadSax_Options XReadSax_DefaultOptions;

/* ========== 公开 API ========== */

/**
 * @brief     将列字母转换为列号（A=1, Z=26, AA=27, ...）
 * @param letters   列字母字符串（如 "AB"）
 * @param lettersLen 输出参数，接收消耗的字符数（可为NULL）
 * @return    列号（从1开始），失败返回0
 */
int XReadSax_parseColLetters(const char* letters, int* lettersLen);

/**
 * @brief     解析单元格引用字符串
 * @param ref     单元格引用（如 "C12"、"$A$1"）
 * @param outRow  输出行号（1索引）
 * @param outCol  输出列号（1索引）
 * @return    成功返回true
 */
bool XReadSax_parseCellRef(const char* ref, int* outRow, int* outCol);

/**
 * @brief     从 XLSX ZIP 文件加载共享字符串表
 * @param zipPath   XLSX 文件路径
 * @param outList   输出共享字符串列表（调用者负责释放每个XString*）
 * @return    成功返回true
 */
bool XReadSax_loadSharedStringsFromZip(const char* zipPath, XStringList* outList);

/**
 * @brief     SAX 方式解析工作表 XML 数据
 * @param sheetXml      sheet XML 数据
 * @param sheetLen      XML 长度
 * @param sharedStrings 共享字符串列表（可为空）
 * @param opt           选项（可为NULL）
 * @param onCell        单元格回调（不能为NULL）
 * @param userData      用户数据
 * @return    成功返回true
 */
bool XReadSax_readSheetXml(const uint8_t* sheetXml, size_t sheetLen,
                            const XStringList* sharedStrings,
                            const XReadSax_Options* opt,
                            XReadSax_CellCallback onCell,
                            void* userData);

/**
 * @brief     从 XLSX ZIP 中 SAX 读取指定工作表
 * @param zipPath       XLSX 文件路径
 * @param sheetPath     工作表 XML 路径，如 "xl/worksheets/sheet1.xml"
 * @param sharedStrings 可选共享字符串列表（传NULL则不解析）
 * @param opt           选项（可为NULL）
 * @param onCell        单元格回调
 * @param userData      用户数据
 * @return    成功返回true
 */
bool XReadSax_readSheetFromZip(const char* zipPath,
                                const char* sheetPath,
                                const XStringList* sharedStrings,
                                const XReadSax_Options* opt,
                                XReadSax_CellCallback onCell,
                                void* userData);

#ifdef __cplusplus
}
#endif
#endif
