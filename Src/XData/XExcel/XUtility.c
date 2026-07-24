#include "XUtility.h"
#include <string.h>

#include <math.h>

#include <ctype.h>

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
void XUtility_safeSheetName(const char* src, char* dst, size_t dstSize) {
    if (!src || !dst || dstSize == 0) return;
    size_t i, j = 0;
    for (i = 0; src[i] != '\0' && j < dstSize - 1; ++i) {
        if (src[i] == '\\' || src[i] == '/' || src[i] == '?' || src[i] == '*' || src[i] == '[' || src[i] == ']' || src[i] == ':')
            dst[j++] = ' ';
        else
            dst[j++] = src[i];
    }
    dst[j] = '\0';
}
bool XUtility_isValidSheetName(const char* name) {
    if (!name || strlen(name) == 0 || strlen(name) > 31) return false;
    for (const char* p = name; *p; ++p) {
        if (*p == '\\' || *p == '/' || *p == '?' || *p == '*' || *p == '[' || *p == ']' || *p == ':')
            return false;
    }
    return true;
}
XString* XUtility_createSheetName(const char* baseName, bool (*exists)(const char*)) {
    (void)exists;
    XString* name = XString_create();
    if (name) XString_append_utf8(name, baseName ? baseName : "Sheet");
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
    /* 累加完整年份的天数 */
    for (int y = 1; y < year; y++) {
        days += is_leap_year(y) ? 366 : 365;
    }
    /* 累加当前年的月份 */
    for (int m = 1; m < month; m++) {
        days += days_in_months[m - 1];
        if (m == 2 && is_leap_year(year)) days++;
    }
    days += day - 1; /* 第1天偏移为0 */
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

/* Excel 1900 系统的基准日期：1899-12-30 = 天数 0 */
/* 1899-12-30 距离 0001-01-01 的天数 */
#define EXCEL_EPOCH_OFFSET_1900 693594LL

double XUtility_epochToExcel(int year, int month, int day, int hour, int min, int sec) {
    int64_t total_days = date_to_days(year, month, day);
    int64_t excel_serial = total_days - EXCEL_EPOCH_OFFSET_1900;
    /* Excel 的 1900 年闰年 bug：serial 60 对应 1900-02-29，但实际上 1900 不是闰年 */
    if (excel_serial > 59) excel_serial += 1; /* 跳过不存在的 1900-02-29 */
    double time_fraction = (double)(hour * 3600 + min * 60 + sec) / 86400.0;
    return (double)excel_serial + time_fraction;
}

void XUtility_excelToEpoch(double serial, int* year, int* month, int* day, int* hour, int* min, int* sec) {
    int64_t days = (int64_t)serial;
    /* 补偿 Excel 的 1900 年闰年 bug */
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

bool XUtility_parseXsdBoolean(const char* value, bool defaultValue)
{
    if (!value || !value[0]) return defaultValue;
    if (strcmp(value, "true") == 0 || strcmp(value, "1") == 0) return true;
    if (strcmp(value, "false") == 0 || strcmp(value, "0") == 0) return false;
    return defaultValue;
}

const char* XUtility_xsdBoolean(bool value)
{
    return value ? "true" : "false";
}

/* ========== 路径工具 ========== */

void XUtility_splitPath(const char* path, char* dir, size_t dirSize, char* fileName, size_t nameSize)
{
    if (!path) {
        if (dir && dirSize) dir[0] = '\0';
        if (fileName && nameSize) fileName[0] = '\0';
        return;
    }
    const char* lastSlash = strrchr(path, '/');
    if (!lastSlash) lastSlash = strrchr(path, '\\');
    if (lastSlash) {
        if (dir && dirSize) {
            size_t len = (size_t)(lastSlash - path);
            if (len >= dirSize) len = dirSize - 1;
            memcpy(dir, path, len);
            dir[len] = '\0';
        }
        if (fileName && nameSize) {
            strncpy(fileName, lastSlash + 1, nameSize - 1);
            fileName[nameSize - 1] = '\0';
        }
    } else {
        if (dir && dirSize) dir[0] = '\0';
        if (fileName && nameSize) {
            strncpy(fileName, path, nameSize - 1);
            fileName[nameSize - 1] = '\0';
        }
    }
}

XString XUtility_getRelFilePath(const char* filePath)
{
    XString result;
    XString_init(&result);
    if (!filePath) return result;
    const char* lastSlash = strrchr(filePath, '/');
    if (!lastSlash) lastSlash = strrchr(filePath, '\\');
    if (lastSlash)
        XString_append_utf8(&result, lastSlash + 1);
    else
        XString_append_utf8(&result, filePath);
    return result;
}

/* ========== 工作表名称工具 ========== */

XString XUtility_escapeSheetName(const char* sheetName)
{
    XString result;
    XString_init(&result);
    if (!sheetName) return result;
    /* 如果名称包含特殊字符或空格，用单引号包裹 */
    bool needQuote = false;
    for (const char* p = sheetName; *p; ++p) {
        if (*p == ' ' || *p == '\'' || *p == '-' || *p == '+' || *p == '(' || *p == ')' ||
            *p == '[' || *p == ']' || *p == '{' || *p == '}' || *p == ',' || *p == ';' ||
            *p == '!' || *p == '^' || *p == '&' || *p == '~' || *p == '`' || *p == '@' ||
            *p == '#' || *p == '$' || *p == '%' || *p == '=') {
            needQuote = true;
            break;
        }
    }
    /* 如果以数字开头也需要引号 */
    if (sheetName[0] >= '0' && sheetName[0] <= '9') needQuote = true;

    if (needQuote) {
        XString_append_utf8(&result, "'");
        /* 内部的单引号需要转义为两个单引号 */
        for (const char* p = sheetName; *p; ++p) {
            if (*p == '\'') XString_append_utf8(&result, "''");
            else {
                char c[2] = {*p, '\0'};
                XString_append_utf8(&result, c);
            }
        }
        XString_append_utf8(&result, "'");
    } else {
        XString_append_utf8(&result, sheetName);
    }
    return result;
}

XString XUtility_unescapeSheetName(const char* sheetName)
{
    XString result;
    XString_init(&result);
    if (!sheetName) return result;
    size_t len = strlen(sheetName);
    if (len >= 2 && sheetName[0] == '\'' && sheetName[len - 1] == '\'') {
        /* 去掉首尾单引号，内部的 '' 还原为 ' */
        for (size_t i = 1; i < len - 1; ++i) {
            if (sheetName[i] == '\'' && i + 1 < len - 1 && sheetName[i + 1] == '\'') {
                XString_append_utf8(&result, "'");
                ++i;
            } else {
                char c[2] = {sheetName[i], '\0'};
                XString_append_utf8(&result, c);
            }
        }
    } else {
        XString_append_utf8(&result, sheetName);
    }
    return result;
}

bool XUtility_isSpaceReserveNeeded(const char* str)
{
    if (!str || !str[0]) return false;
    size_t len = strlen(str);
    return (str[0] == ' ' || str[len - 1] == ' ');
}

/* ========== 共享公式转换 ========== */

/* 辅助：解析单元格引用（如 "A1"、"$B$2"），返回是否成功 */
static bool parseCellRef(const char* s, size_t* pos, bool* colAbs, int* col, bool* rowAbs, int* row)
{
    size_t i = *pos;
    *colAbs = false;
    *rowAbs = false;
    *col = 0;
    *row = 0;

    if (s[i] == '$') { *colAbs = true; i++; }
    /* 读取列字母 */
    bool hasCol = false;
    while (s[i] >= 'A' && s[i] <= 'Z') { *col = *col * 26 + (s[i] - 'A' + 1); hasCol = true; i++; }
    while (s[i] >= 'a' && s[i] <= 'z') { *col = *col * 26 + (s[i] - 'a' + 1); hasCol = true; i++; }
    if (!hasCol) return false;

    if (s[i] == '$') { *rowAbs = true; i++; }
    /* 读取行数字 */
    bool hasRow = false;
    while (s[i] >= '0' && s[i] <= '9') { *row = *row * 10 + (s[i] - '0'); hasRow = true; i++; }
    if (!hasRow) return false;

    *pos = i;
    return true;
}

/* 辅助：将列号转为字母 */
static void colToLetters(int col, char* buf)
{
    char tmp[16];
    int idx = 0;
    while (col > 0) {
        int rem = (col - 1) % 26;
        tmp[idx++] = 'A' + rem;
        col = (col - 1) / 26;
    }
    /* 反转 */
    for (int i = 0; i < idx; i++) buf[i] = tmp[idx - 1 - i];
    buf[idx] = '\0';
}

XString XUtility_convertSharedFormula(const char* rootFormula, const XCellReference* rootCell, const XCellReference* cell)
{
    XString result;
    XString_init(&result);
    if (!rootFormula || !rootCell || !cell) return result;

    int rowDelta = XCellReference_row(cell) - XCellReference_row(rootCell);
    int colDelta = XCellReference_column(cell) - XCellReference_column(rootCell);

    size_t len = strlen(rootFormula);
    size_t i = 0;
    char buf[64];

    while (i < len) {
        /* 检测是否是单元格引用（前面不能是字母/数字/下划线） */
        bool canBeRef = (i == 0) || (!isalnum((unsigned char)rootFormula[i-1]) && rootFormula[i-1] != '_' && rootFormula[i-1] != '$');
        /* 也允许 $ 开头 */
        if (rootFormula[i] == '$') canBeRef = true;

        if (canBeRef) {
            size_t savedPos = i;
            bool colAbs, rowAbs;
            int col, row;
            if (parseCellRef(rootFormula, &i, &colAbs, &col, &rowAbs, &row)) {
                /* 检查后面不能紧跟字母（否则是函数名的一部分） */
                if (i < len && (isalpha((unsigned char)rootFormula[i]) || rootFormula[i] == '_')) {
                    /* 不是单元格引用，原样输出 */
                    for (size_t j = savedPos; j < i; j++) {
                        char c[2] = {rootFormula[j], '\0'};
                        XString_append_utf8(&result, c);
                    }
                } else {
                    /* 是单元格引用，进行偏移 */
                    int newCol = colAbs ? col : col + colDelta;
                    int newRow = rowAbs ? row : row + rowDelta;
                    if (newCol < 1) newCol = 1;
                    if (newRow < 1) newRow = 1;
                    /* 构建新引用 */
                    char ref[64];
                    int pos = 0;
                    if (colAbs) ref[pos++] = '$';
                    colToLetters(newCol, buf);
                    strcpy(ref + pos, buf);
                    pos += strlen(buf);
                    if (rowAbs) ref[pos++] = '$';
                    snprintf(ref + pos, sizeof(ref) - pos, "%d", newRow);
                    XString_append_utf8(&result, ref);
                }
            } else {
                /* 解析失败，输出当前字符 */
                char c[2] = {rootFormula[i], '\0'};
                XString_append_utf8(&result, c);
                i++;
            }
        } else {
            char c[2] = {rootFormula[i], '\0'};
            XString_append_utf8(&result, c);
            i++;
        }
    }
    return result;
}
