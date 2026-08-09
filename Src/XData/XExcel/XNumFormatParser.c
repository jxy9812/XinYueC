#include "XNumFormatParser.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_builtin_date_format(int code)
{
    return (code >= 14 && code <= 22) || (code >= 27 && code <= 36) ||
           (code >= 45 && code <= 47) || (code >= 50 && code <= 58) ||
           (code >= 71 && code <= 81);
}

bool XNumFormatParser_isDateTime(const XString* formatCode)
{
    const char* fc = formatCode ? XString_toUtf8(formatCode) : NULL;
    if (!fc || !*fc) return false;

    /* 仅当整个字符串是十进制编号时，才按 Excel 内置 numFmtId 判断。 */
    char* end = NULL;
    long code = strtol(fc, &end, 10);
    if (end != fc && *end == '\0') return is_builtin_date_format((int)code);

    bool quoted = false;
    for (size_t i = 0; fc[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)fc[i];
        if (ch == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted) continue;
        if (ch == '\\' || ch == '_' || ch == '*') {
            if (fc[i + 1] != '\0') ++i;
            continue;
        }
        if (ch == '[') {
            size_t close = i + 1;
            while (fc[close] != '\0' && fc[close] != ']') ++close;
            if (fc[close] == ']') {
                size_t token = i + 1;
                while (token < close && isspace((unsigned char)fc[token])) ++token;
                if (token < close) {
                    int lower = tolower((unsigned char)fc[token]);
                    if (lower == 'h' || lower == 'm' || lower == 's') return true;
                }
                i = close;
                continue;
            }
        }
        ch = (unsigned char)tolower(ch);
        if (ch == 'y' || ch == 'm' || ch == 'd' || ch == 'h' || ch == 's')
            return true;
    }
    return false;
}
