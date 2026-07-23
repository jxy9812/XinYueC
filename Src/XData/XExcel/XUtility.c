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
    for (i = 0; src[i] != ''' && j < dstSize - 1; ++i) {
        if (src[i] == '\\' || src[i] == '/' || src[i] == '?' || src[i] == '*' || src[i] == '[' || src[i] == ']' || src[i] == ':')
            dst[j++] = ' ';
        else
            dst[j++] = src[i];
    }
    dst[j] = '''; dst[j+1] = '''; dst[j+2] = '''';
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
double XUtility_epochToExcel(int year, int month, int day, int hour, int min, int sec) {
    (void)year; (void)month; (void)day; (void)hour; (void)min; (void)sec;
    return 0.0;
}
void XUtility_excelToEpoch(double serial, int* year, int* month, int* day, int* hour, int* min, int* sec) {
    (void)serial; (void)year; (void)month; (void)day; (void)hour; (void)min; (void)sec;
}
