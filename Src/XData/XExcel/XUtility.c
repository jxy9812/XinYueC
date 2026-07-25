#include "XUtility.h"
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdio.h>

double XUtility_dateTimeToExcelSerial(int64_t timestampMs, bool date1904) {
    double offset = date1904 ? 24107.0 : 25569.0;
    double serial = (double)timestampMs / 86400000.0 + offset;
    if (!date1904 && serial < 60.0) serial -= 1.0;
    return serial;
}

int64_t XUtility_excelSerialToDateTime(double serial, bool date1904) {
    double offset = date1904 ? 24107.0 : 25569.0;
    if (!date1904 && serial < 60.0) serial += 1.0;
    return (int64_t)((serial - offset) * 86400000.0);
}

XString XUtility_safeSheetName(const XString* src) {
    XString result;
    XString_init(&result);
    if (!src) return result;
    const char* cstr = XString_toUtf8(src);
    if (!cstr) return result;
    for (const char* p = cstr; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == '?' || *p == '*' || *p == '[' || *p == ']' || *p == ':')
            XString_append_utf8(&result, " ");
        else {
            char c[2] = {*p, '\0'};
            XString_append_utf8(&result, c);
        }
    }
    /* 截断到31个字符 */
    if (XString_size_base(&result) > 31) {
        XString* tmp = XString_left(&result, 31);
        XString_deinit_base(&result);
        if (tmp) { result = *tmp; XString_delete_base(tmp); }
    }
    return result;
}

bool XUtility_isValidSheetName(const XString* name) {
    if (!name) return false;
    const char* cstr = XString_toUtf8(name);
    if (!cstr || strlen(cstr) == 0 || strlen(cstr) > 31) return false;
    for (const char* p = cstr; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == '?' || *p == '*' || *p == '[' || *p == ']' || *p == ':')
            return false;
    }
    return true;
}

XString* XUtility_createSheetName(const XString* baseName, bool (*exists)(const XString*)) {
    XString* name = XString_create();
    if (!name) return NULL;
    if (baseName && XString_size_base((XString*)baseName) > 0)
        XString_append(name, baseName);
    else
        XString_append_utf8(name, "Sheet");
    /* 如果提供了 exists 回调，则尝试生成唯一名称 */
    if (exists) {
        int suffix = 1;
        while (exists(name)) {
            XString_clear_base(name);
            if (baseName && XString_size_base((XString*)baseName) > 0)
                XString_append(name, baseName);
            else
                XString_append_utf8(name, "Sheet");
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", suffix++);
            XString_append_utf8(name, buf);
        }
    }
    return name;
}

/* 辅助：判断闰年 */
static int is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/* 辅助：月份天数表 */
static const int days_in_months[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

/* 辅助：计算从公元1年1月1日到给定日期的天数 */
static int64_t date_to_days(int year, int month, int day) {
    int64_t days = 0;
    for (int y = 1; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    for (int m = 1; m < month; m++) {
        days += days_in_months[m - 1];
        if (m == 2 && is_leap_year(year)) days++;
    }
    days += day - 1;
    return days;
}

/* 辅助：从天数反算日期 */
static void days_to_date(int64_t days, int* year, int* month, int* day) {
    int y = 1;
    while (1) {
        int yd = is_leap_year(y) ? 366 : 365;
        if (days < yd) break;
        days -= yd;
        y++;
    }
    *year = y;
    int leap = is_leap_year(y);
    for (int m = 1; m <= 12; m++) {
        int md = days_in_months[m - 1];
        if (m == 2 && leap) md++;
        if (days < md) {
            *month = m;
            *day = (int)(days + 1);
            return;
        }
        days -= md;
    }
    *month = 12;
    *day = 31;
}

/* Excel 1900 系统的基准日期：1899-12-30 距离 0001-01-01 的天数 */
#define EXCEL_EPOCH_OFFSET_1900 693594LL

double XUtility_epochToExcel(int year, int month, int day, int hour, int min, int sec) {
    int64_t total_days = date_to_days(year, month, day);
    int64_t excel_serial = total_days - EXCEL_EPOCH_OFFSET_1900;
    if (excel_serial > 59) excel_serial += 1;
    double time_fraction = (double)(hour * 3600 + min * 60 + sec) / 86400.0;
    return (double)excel_serial + time_fraction;
}

void XUtility_excelToEpoch(double serial, int* year, int* month, int* day, int* hour, int* min, int* sec) {
    int64_t days = (int64_t)serial;
    if (days > 59) days -= 1;
    double frac = serial - (int64_t)serial;
    if (frac < 0) { frac += 1.0; days -= 1; }
    int64_t total_seconds = (int64_t)(frac * 86400.0 + 0.5);
    days_to_date(days + EXCEL_EPOCH_OFFSET_1900, year, month, day);
    *hour = (int)(total_seconds / 3600);
    *min = (int)((total_seconds % 3600) / 60);
    *sec = (int)(total_seconds % 60);
}

/* ========== XSD 布尔值解析 ========== */

bool XUtility_parseXsdBoolean(const XString* value, bool defaultValue)
{
    if (!value) return defaultValue;
    const char* cstr = XString_toUtf8(value);
    if (!cstr || !cstr[0]) return defaultValue;
    if (strcmp(cstr, "true") == 0 || strcmp(cstr, "1") == 0) return true;
    if (strcmp(cstr, "false") == 0 || strcmp(cstr, "0") == 0) return false;
    return defaultValue;
}

const XString* XUtility_xsdBoolean(bool value)
{
    static XString s_true;
    static XString s_false;
    static bool inited = false;
    if (!inited) {
        XString_init(&s_true);
        XString_init(&s_false);
        XString_append_utf8(&s_true, "true");
        XString_append_utf8(&s_false, "false");
        inited = true;
    }
    return value ? &s_true : &s_false;
}

/* ========== 路径工具 ========== */

void XUtility_splitPath(const XString* path, XString* dir, XString* fileName)
{
    if (dir) XString_clear_base(dir);
    if (fileName) XString_clear_base(fileName);
    if (!path) return;
    const char* cstr = XString_toUtf8(path);
    if (!cstr) return;
    const char* lastSlash = strrchr(cstr, '/');
    if (!lastSlash) lastSlash = strrchr(cstr, '\\');
    if (lastSlash) {
        if (dir) XString_append_with_length_utf8(dir, cstr, (size_t)(lastSlash - cstr));
        if (fileName) XString_append_utf8(fileName, lastSlash + 1);
    } else {
        if (fileName) XString_append_utf8(fileName, cstr);
    }
}

XString XUtility_getRelFilePath(const XString* filePath)
{
    XString result;
    XString_init(&result);
    if (!filePath) return result;
    const char* cstr = XString_toUtf8(filePath);
    if (!cstr) return result;
    const char* lastSlash = strrchr(cstr, '/');
    if (!lastSlash) lastSlash = strrchr(cstr, '\\');
    if (lastSlash)
        XString_append_utf8(&result, lastSlash + 1);
    else
        XString_append_utf8(&result, cstr);
    return result;
}

/* ========== 工作表名称工具 ========== */

XString XUtility_escapeSheetName(const XString* sheetName)
{
    XString result;
    XString_init(&result);
    if (!sheetName) return result;
    const char* cstr = XString_toUtf8(sheetName);
    if (!cstr) return result;
    bool needQuote = false;
    for (const char* p = cstr; *p; ++p) {
        if (*p == ' ' || *p == '\'' || *p == '-' || *p == '+' || *p == '(' || *p == ')' ||
            *p == '[' || *p == ']' || *p == '{' || *p == '}' || *p == ',' || *p == ';' ||
            *p == '!' || *p == '^' || *p == '&' || *p == '~' || *p == '`' || *p == '@' ||
            *p == '#' || *p == '$' || *p == '%' || *p == '=') {
            needQuote = true;
            break;
        }
    }
    if (cstr[0] >= '0' && cstr[0] <= '9') needQuote = true;

    if (needQuote) {
        XString_append_utf8(&result, "'");
        for (const char* p = cstr; *p; ++p) {
            if (*p == '\'') XString_append_utf8(&result, "''");
            else {
                char c[2] = {*p, '\0'};
                XString_append_utf8(&result, c);
            }
        }
        XString_append_utf8(&result, "'");
    } else {
        XString_append_utf8(&result, cstr);
    }
    return result;
}

XString XUtility_unescapeSheetName(const XString* sheetName)
{
    XString result;
    XString_init(&result);
    if (!sheetName) return result;
    const char* cstr = XString_toUtf8(sheetName);
    if (!cstr) return result;
    size_t len = strlen(cstr);
    if (len >= 2 && cstr[0] == '\'' && cstr[len - 1] == '\'') {
        for (size_t i = 1; i < len - 1; ++i) {
            if (cstr[i] == '\'' && i + 1 < len - 1 && cstr[i + 1] == '\'') {
                XString_append_utf8(&result, "'");
                ++i;
            } else {
                char c[2] = {cstr[i], '\0'};
                XString_append_utf8(&result, c);
            }
        }
    } else {
        XString_append_utf8(&result, cstr);
    }
    return result;
}

bool XUtility_isSpaceReserveNeeded(const XString* str)
{
    if (!str) return false;
    const char* cstr = XString_toUtf8(str);
    if (!cstr || !cstr[0]) return false;
    size_t len = strlen(cstr);
    return (cstr[0] == ' ' || cstr[len - 1] == ' ');
}

/* ========== 共享公式转换 ========== */

static bool parseCellRef(const char* s, size_t* pos, bool* colAbs, int* col, bool* rowAbs, int* row)
{
    size_t i = *pos;
    *colAbs = false;
    *rowAbs = false;
    *col = 0;
    *row = 0;

    if (s[i] == '$') { *colAbs = true; i++; }
    bool hasCol = false;
    while (s[i] >= 'A' && s[i] <= 'Z') { *col = *col * 26 + (s[i] - 'A' + 1); hasCol = true; i++; }
    while (s[i] >= 'a' && s[i] <= 'z') { *col = *col * 26 + (s[i] - 'a' + 1); hasCol = true; i++; }
    if (!hasCol) return false;

    if (s[i] == '$') { *rowAbs = true; i++; }
    bool hasRow = false;
    while (s[i] >= '0' && s[i] <= '9') { *row = *row * 10 + (s[i] - '0'); hasRow = true; i++; }
    if (!hasRow) return false;

    *pos = i;
    return true;
}

static void colToLetters(int col, char* buf)
{
    char tmp[16];
    int idx = 0;
    while (col > 0) {
        int rem = (col - 1) % 26;
        tmp[idx++] = 'A' + rem;
        col = (col - 1) / 26;
    }
    for (int i = 0; i < idx; i++) buf[i] = tmp[idx - 1 - i];
    buf[idx] = '\0';
}

XString XUtility_convertSharedFormula(const XString* rootFormula, const XCellReference* rootCell, const XCellReference* cell)
{
    XString result;
    XString_init(&result);
    if (!rootFormula || !rootCell || !cell) return result;
    const char* formulaCstr = XString_toUtf8(rootFormula);
    if (!formulaCstr) return result;

    int rowDelta = XCellReference_row(cell) - XCellReference_row(rootCell);
    int colDelta = XCellReference_column(cell) - XCellReference_column(rootCell);

    size_t len = strlen(formulaCstr);
    size_t i = 0;
    char buf[64];

    while (i < len) {
        bool canBeRef = (i == 0) || (!isalnum((unsigned char)formulaCstr[i-1]) && formulaCstr[i-1] != '_' && formulaCstr[i-1] != '$');
        if (formulaCstr[i] == '$') canBeRef = true;

        if (canBeRef) {
            size_t savedPos = i;
            bool colAbs, rowAbs;
            int col, row;
            if (parseCellRef(formulaCstr, &i, &colAbs, &col, &rowAbs, &row)) {
                if (i < len && (isalpha((unsigned char)formulaCstr[i]) || formulaCstr[i] == '_')) {
                    for (size_t j = savedPos; j < i; j++) {
                        char c[2] = {formulaCstr[j], '\0'};
                        XString_append_utf8(&result, c);
                    }
                } else {
                    int newCol = colAbs ? col : col + colDelta;
                    int newRow = rowAbs ? row : row + rowDelta;
                    if (newCol < 1) newCol = 1;
                    if (newRow < 1) newRow = 1;
                    char ref[64];
                    int pos = 0;
                    if (colAbs) ref[pos++] = '$';
                    colToLetters(newCol, buf);
                    strcpy(ref + pos, buf);
                    pos += (int)strlen(buf);
                    if (rowAbs) ref[pos++] = '$';
                    snprintf(ref + pos, sizeof(ref) - (size_t)pos, "%d", newRow);
                    XString_append_utf8(&result, ref);
                }
            } else {
                char c[2] = {formulaCstr[i], '\0'};
                XString_append_utf8(&result, c);
                i++;
            }
        } else {
            char c[2] = {formulaCstr[i], '\0'};
            XString_append_utf8(&result, c);
            i++;
        }
    }
    return result;
}
