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
    if (lettersLen) *lettersLen = 0;
    if (!lettersUtf8 || !lettersUtf8[0]) return 0;
    int col = 0;
    int len = 0;
    const char* p = lettersUtf8;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
        int digit = (*p >= 'a' && *p <= 'z') ? *p - 'a' + 1 : *p - 'A' + 1;
        if (col > (INT32_MAX - digit) / 26) return 0;
        col = col * 26 + digit;
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
    const char* p = refUtf8;
    if (*p == '$') ++p;
    int col = 0;
    int lettersLen = 0;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
        int digit = (*p >= 'a' && *p <= 'z') ? *p - 'a' + 1 : *p - 'A' + 1;
        if (col > (INT32_MAX - digit) / 26) return false;
        col = col * 26 + digit;
        ++p;
        ++lettersLen;
    }
    if (col <= 0 || col > 16384 || lettersLen == 0) return false;
    if (*p == '$') ++p;
    const char* rowPart = p;
    char* end = NULL;
    errno = 0;
    long row = strtol(rowPart, &end, 10);
    if (end == rowPart || *end != '\0' || row <= 0 || row > 1048576 || errno == ERANGE) return false;
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

    bool result = XReadSax_loadSharedStringsXml(XByteArray_data(xmlData),
        XByteArray_size_base(xmlData), outList);
    XByteArray_delete_base(xmlData);
    return result;
}

bool XReadSax_loadSharedStringsXml(const uint8_t* xmlData, size_t xmlLen, XStringList* outList)
{
    if (!xmlData || xmlLen == 0 || !outList) return false;
    XByteArray* xml = XByteArray_create_with_data((const char*)xmlData, xmlLen);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!xml || !reader) {
        if (xml) XByteArray_delete_base(xml);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    XXmlStreamReader_addData(reader, xml);

    bool in_si = false;
    XString* acc = XString_create();

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (XXmlStreamReader_hasError(reader)) break;

        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (XString_equals_utf8(name, "si", XChar_CaseSensitive)) {
                in_si = true;
                XString_clear_base(acc);
            } else if (in_si && XString_equals_utf8(name, "t", XChar_CaseSensitive)) {
                const XString* text = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (text) XString_append(acc, text);
            }
        } else if (tt == XXmlStream_EndElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (XString_equals_utf8(name, "si", XChar_CaseSensitive)) {
                in_si = false;
                XString* s = XString_create_copy(acc);
                XStringList_push_back_base((XVector*)outList, s);
                XString_delete_base(s);
            }
        }
    }

    XString_delete_base(acc);
    bool ok = !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    XByteArray_delete_base(xml);
    return ok;
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
    if (!sheetXml || sheetLen == 0 || !onCell) return false;
    static const XReadSax_Options defaultOpt = { 0, 0, 0, 0, true, true };
    if (!opt) opt = &defaultOpt;

    XByteArray* xml = XByteArray_create_with_data((const char*)sheetXml, sheetLen);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!xml || !reader) {
        if (xml) XByteArray_delete_base(xml);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    XXmlStreamReader_addData(reader, xml);

    typedef enum {
        ST_ROOT,       /* 根元素之外 */
        ST_SHEETDATA,  /* 在 <sheetData> 内 */
        ST_CELL        /* 在 <c>（单元格）内 */
    } ParserState;

    ParserState state = ST_ROOT;
    int cell_row = 0, cell_col = 0;
    char cell_ref[32] = {0};
    char cell_type[16] = {0};   /* s=shared, b=bool, e=error, str=formula, inlineStr */
    XString* value = XString_create();
    if (!value) {
        XXmlStreamReader_delete_base(reader);
        XByteArray_delete_base(xml);
        return false;
    }
    bool has_value = false;

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (XXmlStreamReader_hasError(reader)) break;

        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (state == ST_ROOT && XString_equals_utf8(name, "sheetData", XChar_CaseSensitive)) {
                state = ST_SHEETDATA;
            } else if (state == ST_SHEETDATA && XString_equals_utf8(name, "c", XChar_CaseSensitive)) {
                /* 单元格开始 */
                state = ST_CELL;
                cell_ref[0] = '\0';
                cell_type[0] = '\0';
                XString_clear_base(value);
                has_value = false;

                const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
                if (attrs) {
                    XString_Init_Utf8(rName, "r");
                    XString_Init_Utf8(tName, "t");
                    const XString* r = XXmlStreamAttributes_value_ex(attrs, NULL, rName);
                    const XString* t = XXmlStreamAttributes_value_ex(attrs, NULL, tName);
                    if (r) {
                        const char* rStr = XString_toUtf8(r);
                        if (rStr && strlen(rStr) < sizeof(cell_ref)) strcpy(cell_ref, rStr);
                    }
                    if (t) {
                        const char* tStr = XString_toUtf8(t);
                        if (tStr && strlen(tStr) < sizeof(cell_type)) strcpy(cell_type, tStr);
                    }
                    XString_deinit_base(rName);
                    XString_deinit_base(tName);
                }
                if (cell_ref[0]) {
                    XString_Init_Utf8(cellRef, cell_ref);
                    if (!XReadSax_parseCellRef(cellRef, &cell_row, &cell_col)) {
                        cell_row = 0;
                        cell_col = 0;
                    }
                    XString_deinit_base(cellRef);
                }
            } else if (state == ST_CELL) {
                if (XString_equals_utf8(name, "v", XChar_CaseSensitive) ||
                    XString_equals_utf8(name, "t", XChar_CaseSensitive)) {
                    const XString* txt = XXmlStreamReader_readElementText(reader,
                        XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                    if (txt) {
                        XString_append(value, txt);
                        has_value = true;
                    }
                }
            }
        } else if (tt == XXmlStream_EndElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (state == ST_CELL && XString_equals_utf8(name, "c", XChar_CaseSensitive)) {
                /* 行列过滤 */
                if ((opt->m_minRow > 0 && cell_row < opt->m_minRow) ||
                    (opt->m_maxRow > 0 && cell_row > opt->m_maxRow) ||
                    (opt->m_minCol > 0 && cell_col < opt->m_minCol) ||
                    (opt->m_maxCol > 0 && cell_col > opt->m_maxCol)) {
                    state = ST_SHEETDATA;
                    continue;
                }

                /* 解析值 */
                if (cell_row <= 0 || cell_col <= 0) {
                    state = ST_SHEETDATA;
                    continue;
                }
                const char* type_str = cell_type;
                const XString* callbackValue = value;

                if (cell_type[0] == 's' && has_value && sharedStrings) {
                    const char* valueUtf8 = XString_toUtf8(value);
                    char* end = NULL;
                    errno = 0;
                    long idx = strtol(valueUtf8 ? valueUtf8 : "", &end, 10);
                    if (errno == 0 && idx >= 0 && idx < (long)XStringList_size_base((const XVector*)sharedStrings)) {
                        XString* item = (XString*)XStringList_at_base((XVector*)sharedStrings, (size_t)idx);
                        if (item && end && *end == '\0') callbackValue = item;
                    }
                    type_str = "s";
                } else if (cell_type[0] == '\0' && has_value) {
                    type_str = "n";  /* 默认数字类型 */
                } else if (cell_type[0] == '\0' && !has_value) {
                    /* 空单元格，跳过 */
                    state = ST_SHEETDATA;
                    continue;
                }

                XString_Init_Utf8(tmpType, type_str);
                if (!onCell(cell_row, cell_col, callbackValue, tmpType, userData)) {
                    XString_deinit_base(tmpType);
                    XString_delete_base(value);
                    XXmlStreamReader_delete_base(reader);
                    XByteArray_delete_base(xml);
                    return true;  /* 回调返回 false，停止解析 */
                }
                XString_deinit_base(tmpType);
                state = ST_SHEETDATA;
            } else if (state == ST_SHEETDATA && XString_equals_utf8(name, "sheetData", XChar_CaseSensitive)) {
                state = ST_ROOT;
            }
        }
    }

    bool hasError = XXmlStreamReader_hasError(reader);
    XString_delete_base(value);
    XXmlStreamReader_delete_base(reader);
    XByteArray_delete_base(xml);
    return !hasError;
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
