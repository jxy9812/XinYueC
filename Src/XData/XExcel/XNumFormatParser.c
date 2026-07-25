#include "XNumFormatParser.h"
#include <string.h>

#include <ctype.h>

static const char* s_dateTokens[] = {
    "yyyy", "yy", "mmmm", "mmm", "mm", "m", "dddd", "ddd", "dd", "d",
    "hh", "h", "ss", "s", "AM/PM", "A/P", "am/pm", "a/p",
    NULL
};
bool XNumFormatParser_isDateTime(const XString* formatCode) {
    const char* fc = formatCode ? XString_toUtf8(formatCode) : NULL;
    if (!fc || strlen(fc) == 0) return false;
    /* 检查内置日期格式 */
    int code = atoi(fc);
    if ((code >= 14 && code <= 22) || (code >= 27 && code <= 36) || (code >= 45 && code <= 47) ||
        (code >= 50 && code <= 58) || (code >= 71 && code <= 81) || code == 164 || code == 165 ||
        code == 166 || code == 167 || code == 168 || code == 169 || code == 170 ||
        code == 171 || code == 172 || code == 173 || code == 174 || code == 175 ||
        code == 176 || code == 177 || code == 178 || code == 179 || code == 180 ||
        code == 181 || code == 182 || code == 183 || code == 184 || code == 185 ||
        code == 186 || code == 187 || code == 188 || code == 189 || code == 190 ||
        code == 191 || code == 192 || code == 193 || code == 194 || code == 195 ||
        code == 196 || code == 197 || code == 198 || code == 199 || code == 200 ||
        code == 201 || code == 202 || code == 203 || code == 204 || code == 205 ||
        code == 206 || code == 207 || code == 208 || code == 209 || code == 210 ||
        code == 211 || code == 212 || code == 213 || code == 214 || code == 215 ||
        code == 216 || code == 217 || code == 218 || code == 219 || code == 220) {
        return true;
    }
    /* 检查自定义格式中的日期/时间标记 */
    for (int i = 0; s_dateTokens[i] != NULL; ++i) {
        if (strstr(fc, s_dateTokens[i]) != NULL) return true;
    }
    return false;
}
