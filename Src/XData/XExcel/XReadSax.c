/******************************************************************************
 * @file       XReadSax.c
 * @brief      XReadSax SAX 流式单元格读取实现
 *             对标 QXlsx::read_sax 全部功能
 *
 * 公开函数：
 *   XReadSax_parseColLetters()         - 列字母转列号
 *   XReadSax_parseCellRef()            - 单元格引用解析
 *   XReadSax_loadSharedStringsFromZip() - 从ZIP加载共享字符串
 *   XReadSax_readSheetXml()             - SAX解析sheet XML数据
 *   XReadSax_readSheetFromZip()         - 从ZIP读取指定sheet
 ******************************************************************************/
#include "XReadSax.h"
#include "XZipReader.h"
#include "XXmlStreamReader.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

const XReadSax_Options XReadSax_DefaultOptions = { 0, 0, 0, 0, true, true };

/* ========== 1. 列字母解析 ========== */
/**
 * @brief     将列字母转换为列号（A=1, Z=26, AA=27, AZ=52, ...）
 * @param letters   列字母字符串（如 "AB"）
 * @param lettersLen 输出参数，接收消耗的字符数（可为NULL）
 * @return    列号（从1开始），解析失败返回0
 */
int XReadSax_parseColLetters(const XString* letters, int* lettersLen)
{
    const char* lettersUtf8 = letters ? XString_toUtf8(letters) : NULL;
    if (!lettersUtf8 || !lettersUtf8[0]) return 0;
    int col = 0;
    int len = 0;
    const char* p = lettersUtf8;
    while (*p && isalpha((unsigned char)*p)) {
        col = col * 26 + (toupper((unsigned char)*p) - 'A' + 1);
        ++p;
        ++len;
    }
    if (lettersLen) *lettersLen = len;
    return col;
}

/**
 * @brief     解析单元格引用字符串
 * @param ref     单元格引用字符串（如 "C12"、"$A$1"、"AB123"）
 * @param outRow  输出参数，接收行号（1索引）
 * @param outCol  输出参数，接收列号（1索引）
 * @return    成功返回true，失败返回false
 */
bool XReadSax_parseCellRef(const XString* ref, int* outRow, int* outCol)
{
    const char* refUtf8 = ref ? XString_toUtf8(ref) : NULL;
    if (!refUtf8 || !outRow || !outCol) return false;
    int lettersLen = 0;
    int col = XReadSax_parseColLetters(ref, &lettersLen);
    if (col <= 0 || lettersLen == 0) return false;
    const char* rowPart = refUtf8 + lettersLen;
    char* end = NULL;
    errno = 0;
    long row = strtol(rowPart, &end, 10);
    if (end == rowPart || *end != '\0' || row <= 0 || errno == ERANGE) return false;
    *outRow = (int)row;
    *outCol = col;
    return true;
}

/* ========== 2. 从ZIP加载共享字符串 ========== */
/**
 * @brief     从 XLSX ZIP 文件加载共享字符串表
 * @param zipPath   XLSX 文件路径
 * @param outList   输出：共享字符串列表（调用者负责释放每个XString*）
 * @return    成功返回true（即使文件不存在也返回true，列表为空）
 * @note      调用者负责释放 outList 中每个 XString* 并销毁列表
 */
bool XReadSax_loadSharedStringsFromZip(const XString* zipPath, XStringList* outList)
{
    if (!zipPath || !outList) return false;

    XZipReader* zip = XZipReader_create(zipPath);
    if (!zip) return false;
    XString* ssPath = XString_create_utf8("xl/sharedStrings.xml");
    XByteArray* xmlData = XZipReader_fileData(zip, ssPath);
    XString_delete_base(ssPath);
    XZipReader_delete(zip);
    if (!xmlData) return true;  /* 文件不存在，视为成功（空列表）*/

    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamReader_addData(reader, xmlData);
    XByteArray_delete_base(xmlData);

    bool in_si = false;
    XString* acc = XString_create();

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (XXmlStreamReader_hasError(reader)) break;

        if (tt == XXmlStream_StartElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (strcmp(name, "si") == 0) {
                in_si = true;
                XString_clear_base(acc);
            }
        } else if (tt == XXmlStream_Characters ) {
            if (in_si) {
                const char* txt = XXmlStreamReader_text(reader);
                if (txt) XString_append_utf8(acc, txt);
            }
        } else if (tt == XXmlStream_EndElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (strcmp(name, "si") == 0) {
                in_si = false;
                const char* str = XString_toUtf8(acc);
                XString* s = XString_create_utf8(str ? str : "");
                XStringList_push_back_base((XVector*)outList, &s);
            }
        }
    }

    XString_delete_base(acc);
    XXmlStreamReader_delete(reader);
    return true;
}

/* ========== 3. SAX 解析工作表 XML ========== */
/**
 * @brief     SAX 方式解析工作表 XML 数据
 * @param sheetXml      sheet XML 原始字节数据
 * @param sheetLen      XML 数据长度
 * @param sharedStrings 共享字符串列表（可为空）
 * @param opt           读取选项（传 NULL 使用默认选项）
 * @param onCell        单元格回调函数（不能为NULL）
 * @param userData      用户数据，透传给回调
 * @return    成功返回true（即使回调提前终止也返回true）
 */
bool XReadSax_readSheetXml(const uint8_t* sheetXml, size_t sheetLen,
                            const XStringList* sharedStrings,
                            const XReadSax_Options* opt,
                            XReadSax_CellCallback onCell,
                            void* userData)
{
    if (!sheetXml || !onCell) return false;
    static const XReadSax_Options defaultOpt = { 0, 0, 0, 0, true, true };
    if (!opt) opt = &defaultOpt;

    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(reader, (const char*)sheetXml);

    typedef enum {
        ST_ROOT,       /* 根元素之外 */
        ST_SHEETDATA,  /* 在 <sheetData> 内 */
        ST_CELL        /* 在 <c>（单元格）内 */
    } ParserState;

    ParserState state = ST_ROOT;
    int cell_row = 0, cell_col = 0;
    char cell_ref[32] = {0};
    char cell_type[16] = {0};   /* s=shared, b=bool, e=error, str=formula, inlineStr */
    char value_buf[512] = {0};
    size_t value_len = 0;
    bool has_value = false;
    bool in_cell_child = false;

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (XXmlStreamReader_hasError(reader)) break;

        if (tt == XXmlStream_StartElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (state == ST_ROOT && strcmp(name, "sheetData") == 0) {
                state = ST_SHEETDATA;
            } else if (state == ST_SHEETDATA && strcmp(name, "c") == 0) {
                /* 单元格开始 */
                state = ST_CELL;
                cell_ref[0] = '\0';
                cell_type[0] = '\0';
                value_buf[0] = '\0';
                value_len = 0;
                has_value = false;
                in_cell_child = false;

                const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
                if (attrs) {
                    const char* r = XXmlStreamAttributes_value_ex(attrs, NULL, "r");
                    const char* t = XXmlStreamAttributes_value_ex(attrs, NULL, "t");
                    if (r && strlen(r) < sizeof(cell_ref)) strcpy(cell_ref, r);
                    if (t && strlen(t) < sizeof(cell_type)) strcpy(cell_type, t);
                }
                if (cell_ref[0]) {
                    XReadSax_parseCellRef(cell_ref, &cell_row, &cell_col);
                }
            } else if (state == ST_CELL) {
                in_cell_child = true;
                if (strcmp(name, "v") == 0 || strcmp(name, "f") == 0) {
                    const char* txt = XXmlStreamReader_readElementText(reader,
                        XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                    if (txt) {
                        size_t len = strlen(txt);
                        if (len < sizeof(value_buf) - value_len) {
                            memcpy(value_buf + value_len, txt, len + 1);
                            value_len += len;
                            if (strcmp(name, "v") == 0) has_value = true;
                        }
                    }
                    in_cell_child = false;
                }
            }
        } else if (tt == XXmlStream_Characters ) {
            if (state == ST_CELL && !in_cell_child && !has_value) {
                const char* txt = XXmlStreamReader_text(reader);
                if (txt) {
                    size_t len = strlen(txt);
                    if (len < sizeof(value_buf) - value_len) {
                        memcpy(value_buf + value_len, txt, len);
                        value_len += len;
                        value_buf[value_len] = '\0';
                    }
                }
            }
        } else if (tt == XXmlStream_EndElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (state == ST_CELL && strcmp(name, "c") == 0) {
                /* 行列过滤 */
                if ((opt->m_minRow > 0 && cell_row < opt->m_minRow) ||
                    (opt->m_maxRow > 0 && cell_row > opt->m_maxRow) ||
                    (opt->m_minCol > 0 && cell_col < opt->m_minCol) ||
                    (opt->m_maxCol > 0 && cell_col > opt->m_maxCol)) {
                    state = ST_SHEETDATA;
                    continue;
                }

                /* 解析值 */
                const char* type_str = cell_type;
                const char* value_str = value_buf;
                char resolved[512] = {0};

                if (cell_type[0] == 's' && has_value && sharedStrings) {
                    errno = 0;
                    long idx = strtol(value_buf, NULL, 10);
                    if (errno == 0 && idx >= 0 && idx < (long)XStringList_size_base((const XVector*)sharedStrings)) {
                        XString** arr = (XString**)XStringList_at_base((XVector*)sharedStrings, (size_t)idx);
                        if (arr && *arr) {
                            const char* s = XString_toUtf8(*arr);
                            if (s) {
                                strncpy(resolved, s, sizeof(resolved) - 1);
                                resolved[sizeof(resolved) - 1] = '\0';
                                value_str = resolved;
                            }
                        }
                    }
                    type_str = "s";
                } else if (cell_type[0] == '\0' && has_value) {
                    type_str = "n";  /* 默认数字类型 */
                } else if (cell_type[0] == '\0' && !has_value) {
                    /* 空单元格，跳过 */
                    state = ST_SHEETDATA;
                    continue;
                }

                if (!onCell(cell_row, cell_col, value_str, type_str, userData)) {
                    XXmlStreamReader_delete(reader);
                    return true;  /* 回调返回 false，停止解析 */
                }
                state = ST_SHEETDATA;
            } else if (state == ST_SHEETDATA && strcmp(name, "sheetData") == 0) {
                state = ST_ROOT;
            }
        }
    }

    XXmlStreamReader_delete(reader);
    return !XXmlStreamReader_hasError(reader);
}

/* ========== 4. 从ZIP读取指定sheet ========== */
/**
 * @brief     从 XLSX ZIP 文件中 SAX 读取指定工作表
 * @param zipPath       XLSX ZIP 文件路径
 * @param sheetPath     工作表 XML 路径，如 "xl/worksheets/sheet1.xml"
 * @param sharedStrings 可选的共享字符串列表（传 NULL 则不解析共享字符串）
 * @param opt           读取选项（传 NULL 使用默认）
 * @param onCell        单元格回调函数
 * @param userData      用户数据
 * @return    成功返回true
 */
bool XReadSax_readSheetFromZip(const XString* zipPath,
                                const XString* sheetPath,
                                const XStringList* sharedStrings,
                                const XReadSax_Options* opt,
                                XReadSax_CellCallback onCell,
                                void* userData)
{
    if (!zipPath || !sheetPath || !onCell) return false;

    XZipReader* zip = XZipReader_create(zipPath);
    if (!zip) return false;
    XByteArray* xmlData = XZipReader_fileData(zip, sheetPath);
    XZipReader_delete(zip);
    if (!xmlData) return false;

    uint8_t* data = XByteArray_data(xmlData);
    size_t len = XByteArray_size_base(xmlData);
    bool ok = XReadSax_readSheetXml(data, len, sharedStrings, opt, onCell, userData);
    XByteArray_delete_base(xmlData);
    return ok;
}
