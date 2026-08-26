#include "XChar.h"
#include "XMemory.h"
#include <float.h>
#include <math.h> 
#include <limits.h>
/* ========================================================================== */
/*                      平台抽象函数（在对应平台目录实现）                         */
/* ========================================================================== */

/**
 * @brief 从GBK编码字符串转换为XChar数组（平台实现）
 * @param gbk GBK编码字符串
 * @param input_size 输入数据大小（字节），0则自动检测NULL结尾
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XCharPlatform_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为GBK编码字符串（平台实现）
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param gbk 输出的GBK编码字符串缓冲区
 * @param max_gbk 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XCharPlatform_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk);

/**
 * @brief UTF-8转GBK编码（平台实现）
 * @param utf8_str 输入UTF-8字符串
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param gbk_buf 输出GBK缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回GBK字节数（不含终止符），失败返回-1
 */
int64_t XCharPlatform_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len);

/**
 * @brief GBK转UTF-8编码（平台实现）
 * @param gbk_str 输入GBK字符串
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param utf8_buf 输出UTF-8缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回UTF-8字节数（不含终止符），失败返回-1
 */
int64_t XCharPlatform_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len);
/* ========================================================================== */
/*                              内部常量                                        */
/* ========================================================================== */
#define SURROGATE_OFFSET                    0x10000
#define UTF16_HIGH_SURROGATE_START          0xD800
#define UTF16_HIGH_SURROGATE_END            0xDBFF
#define UTF16_LOW_SURROGATE_START           0xDC00
#define UTF16_LOW_SURROGATE_END             0xDFFF
#define UNICODE_MAX_CODEPOINT               0x10FFFF

/* ========================================================================== */
/*                       大小写转换查找表（统一替代重复switch）                    */
/* 每个条目 {from, to}，查找时按 from 匹配                                     */
/* ========================================================================== */
typedef struct { uint16_t from; uint16_t to; } XCharCasePair;

static const XCharCasePair g_caseMap[] = {
    /* Latin */
    {0x00DF, 0x1E9E}, {0x00E0, 0x00C0}, {0x00E1, 0x00C1}, {0x00E2, 0x00C2},
    {0x00E3, 0x00C3}, {0x00E4, 0x00C4}, {0x00E5, 0x00C5}, {0x00E6, 0x00C6},
    {0x00E7, 0x00C7}, {0x00E8, 0x00C8}, {0x00E9, 0x00C9}, {0x00EA, 0x00CA},
    {0x00EB, 0x00CB}, {0x00EC, 0x00CC}, {0x00ED, 0x00CD}, {0x00EE, 0x00CE},
    {0x00EF, 0x00CF}, {0x00F0, 0x00D0}, {0x00F1, 0x00D1}, {0x00F2, 0x00D2},
    {0x00F3, 0x00D3}, {0x00F4, 0x00D4}, {0x00F5, 0x00D5}, {0x00F6, 0x00D6},
    {0x00F8, 0x00D8}, {0x00F9, 0x00D9}, {0x00FA, 0x00DA}, {0x00FB, 0x00DB},
    {0x00FC, 0x00DC}, {0x00FD, 0x00DD}, {0x00FE, 0x00DE},
    /* Greek */
    {0x03B1, 0x0391}, {0x03B2, 0x0392}, {0x03B3, 0x0393}, {0x03B4, 0x0394},
    {0x03B5, 0x0395}, {0x03B6, 0x0396}, {0x03B7, 0x0397}, {0x03B8, 0x0398},
    {0x03B9, 0x0399}, {0x03BA, 0x039A}, {0x03BB, 0x039B}, {0x03BC, 0x039C},
    {0x03BD, 0x039D}, {0x03BE, 0x039E}, {0x03BF, 0x039F}, {0x03C0, 0x03A0},
    {0x03C1, 0x03A1}, {0x03C2, 0x03A3}, {0x03C3, 0x03A3}, {0x03C4, 0x03A4},
    {0x03C5, 0x03A5}, {0x03C6, 0x03A6}, {0x03C7, 0x03A7}, {0x03C8, 0x03A8},
    {0x03C9, 0x03A9},
    /* Cyrillic */
    {0x0430, 0x0410}, {0x0431, 0x0411}, {0x0432, 0x0412}, {0x0433, 0x0413},
    {0x0434, 0x0414}, {0x0435, 0x0415}, {0x0436, 0x0416}, {0x0437, 0x0417},
    {0x0438, 0x0418}, {0x0439, 0x0419}, {0x043A, 0x041A}, {0x043B, 0x041B},
    {0x043C, 0x041C}, {0x043D, 0x041D}, {0x043E, 0x041E}, {0x043F, 0x041F},
    {0x0440, 0x0420}, {0x0441, 0x0421}, {0x0442, 0x0422}, {0x0443, 0x0423},
    {0x0444, 0x0424}, {0x0445, 0x0425}, {0x0446, 0x0426}, {0x0447, 0x0427},
    {0x0448, 0x0428}, {0x0449, 0x0429}, {0x044A, 0x042A}, {0x044B, 0x042B},
    {0x044C, 0x042C}, {0x044D, 0x042D}, {0x044E, 0x042E}, {0x044F, 0x042F},
};
#define CASE_MAP_SIZE (sizeof(g_caseMap) / sizeof(g_caseMap[0]))

/* 在查找表中查找：from→to（小写→大写），返回是否找到 */
static bool caseMap_lookupLowerToUpper(uint16_t code, uint16_t* out)
{
    for (size_t i = 0; i < CASE_MAP_SIZE; i++) {
        if (g_caseMap[i].from == code) { *out = g_caseMap[i].to; return true; }
    }
    return false;
}

/* 在查找表中查找：to→from（大写→小写），返回是否找到 */
static bool caseMap_lookupUpperToLower(uint16_t code, uint16_t* out)
{
    for (size_t i = 0; i < CASE_MAP_SIZE; i++) {
        if (g_caseMap[i].to == code) { *out = g_caseMap[i].from; return true; }
    }
    return false;
}

/* ========================================================================== */
/*               _2 后缀函数统一宏（消除20+个重复函数模板）                        */
/* ========================================================================== */
#define DEFINE_XCHAR_FUNC2_RET0(name, rettype, call_expr) \
    rettype name(uint32_t ucs4) { \
        if (ucs4 <= 0xFFFF) return (rettype)(call_expr); \
        return (rettype)0; \
    }

#define DEFINE_XCHAR_FUNC2_RET_U32(name, call_expr) \
    uint32_t name(uint32_t ucs4) { \
        if (ucs4 <= 0xFFFF) return (uint32_t)(call_expr); \
        return ucs4; \
    }

/* ========================================================================== */
/*               流式转换公共辅助：输入长度计算 + 终止符处理                       */
/* ========================================================================== */
/* 计算以0结尾的uint16_t数组长度 */
static size_t stream_input_len_u16(const uint16_t* data, size_t input_size)
{
    if (input_size == 0) {
        size_t len = 0;
        while (data[len] != 0) len++;
        return len;
    }
    for (size_t i = 0; i < input_size; i++) {
        if (data[i] == 0) return i;
    }
    return input_size;
}

/* 计算以0结尾的uint32_t数组长度 */
static size_t stream_input_len_u32(const uint32_t* data, size_t input_count)
{
    if (input_count == 0) {
        size_t len = 0;
        while (data[len] != 0) len++;
        return len;
    }
    for (size_t i = 0; i < input_count; i++) {
        if (data[i] == 0) return i;
    }
    return input_count;
}

/* 计算XChar数组长度（复用已有 XChar_getInputLength，但内部版不依赖公共API） */
static size_t stream_input_len_xchar(const XChar* data, size_t input_count)
{
    if (input_count == 0) {
        size_t len = 0;
        while (data[len] != 0) len++;
        return len;
    }
    for (size_t i = 0; i < input_count; i++) {
        if (data[i] == 0) return i;
    }
    return input_count;
}

/* 计算以'\0'结尾的字节串长度 */
static size_t stream_input_len_u8(const uint8_t* data, size_t input_size)
{
    if (input_size == 0) {
        size_t len = 0;
        while (data[len] != '\0') len++;
        return len;
    }
    for (size_t i = 0; i < input_size; i++) {
        if (data[i] == '\0') return i;
    }
    return input_size;
}

static size_t stream_input_len_char(const char* data, size_t input_size)
{
    if (input_size == 0) {
        size_t len = 0;
        while (data[len] != '\0') len++;
        return len;
    }
    for (size_t i = 0; i < input_size; i++) {
        if (data[i] == '\0') return i;
    }
    return input_size;
}

/* ========================================================================== */
/*                          构造与创建函数                                       */
/* ========================================================================== */

XChar XChar_null(void) { return 0; }
XChar XChar_from(uint16_t code) { return code; }
XChar XChar_fromSpecial(XChar_SpecialCharacter ch) { return (XChar)(uint16_t)ch; }
XChar XChar_fromLatin1(char c) { return (uint16_t)(uint8_t)c; }
XChar XChar_fromUcs2(uint16_t c) { return c; }

XChar XChar_fromUnicode(uint32_t unicode)
{
    if (unicode > UNICODE_MAX_CODEPOINT) return 0;
    if (unicode <= 0xFFFF) {
        if ((unicode >= 0xD800 && unicode <= 0xDBFF) ||
            (unicode >= 0xDC00 && unicode <= 0xDFFF)) return 0;
        return (uint16_t)unicode;
    }
    unicode -= SURROGATE_OFFSET;
    return (uint16_t)(UTF16_HIGH_SURROGATE_START + (unicode >> 10));
}

static XChar XChar_from_unicode_low(uint32_t unicode)
{
    if (unicode < 0x10000 || unicode > UNICODE_MAX_CODEPOINT) return 0;
    unicode -= SURROGATE_OFFSET;
    return (uint16_t)(UTF16_LOW_SURROGATE_START + (unicode & 0x3FF));
}

XChar XChar_highSurrogate(uint32_t ucs4) { return XChar_fromUnicode(ucs4); }
XChar XChar_lowSurrogate(uint32_t ucs4) { return XChar_from_unicode_low(ucs4); }

/* ========================================================================== */
/*                         Unicode码点获取函数                                   */
/* ========================================================================== */

uint32_t XChar_unicode(XChar ch) { return ch; }
char XChar_toLatin1(XChar ch) { return (ch > 0xFF) ? 0 : (char)(uint8_t)ch; }
uint8_t XChar_row(XChar ch) { return (uint8_t)(ch >> 8); }
uint8_t XChar_cell(XChar ch) { return (uint8_t)(ch & 0xFF); }

/* ========================================================================== */
/*                    字符分类函数（基于范围判断）                                  */
/* ========================================================================== */

XChar_Category XChar_category(XChar ch)
{
    if (!ch) return XChar_Other_Control;
    uint16_t c = ch;
    if (c <= 0x001F || c == 0x007F || (c >= 0x0080 && c <= 0x009F))
        return XChar_Other_Control;
    if (c == 0x0020 || c == 0x00A0 || c == 0x3000 ||
        (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F)
        return XChar_Separator_Space;
    if (c == 0x2028) return XChar_Separator_Line;
    if (c == 0x2029) return XChar_Separator_Paragraph;
    if ((c >= 'A' && c <= 'Z') || (c >= 0x00C0 && c <= 0x00D6) ||
        (c >= 0x00D8 && c <= 0x00DE) || (c >= 0x0391 && c <= 0x03A9) ||
        (c >= 0x0410 && c <= 0x042F))
        return XChar_Letter_Uppercase;
    if ((c >= 'a' && c <= 'z') || (c >= 0x00DF && c <= 0x00F6) ||
        (c >= 0x00F8 && c <= 0x00FF) || (c >= 0x03B1 && c <= 0x03C9) ||
        (c >= 0x0430 && c <= 0x044F))
        return XChar_Letter_Lowercase;
    if (c >= '0' && c <= '9') return XChar_Number_DecimalDigit;
    if ((c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
        (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E))
        return XChar_Punctuation_Other;
    if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) ||
        (c >= 0xAC00 && c <= 0xD7AF))
        return XChar_Letter_Other;
    return XChar_Other_NotAssigned;
}

uint8_t XChar_combiningClass(XChar ch)
{
    uint16_t c = ch;
    if (c == 0) return 0;
    /* class 230: above */
    if ((c >= 0x0300 && c <= 0x0314) || c == 0x033D || c == 0x0342 ||
        c == 0x0344 || c == 0x0346 || c == 0x034A || c == 0x034B ||
        c == 0x034C || c == 0x0350 || c == 0x0351 || c == 0x0352 ||
        c == 0x0357 || (c >= 0x035B && c <= 0x035C) || c == 0x0363 ||
        (c >= 0x0364 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0487) ||
        (c >= 0x05A3 && c <= 0x05A7) || c == 0x05A8 || c == 0x05AB ||
        c == 0x05AC || (c >= 0x05AF && c <= 0x05B2) || c == 0x05B4 ||
        c == 0x05B7 || c == 0x05B8 || c == 0x05BB ||
        (c >= 0x05BF && c <= 0x05C4) || (c >= 0x0610 && c <= 0x0617) ||
        (c >= 0x0657 && c <= 0x0659) || c == 0x065D || c == 0x065E ||
        (c >= 0x06D6 && c <= 0x06DC) || (c >= 0x06DF && c <= 0x06E2) ||
        c == 0x06E4 || (c >= 0x06E7 && c <= 0x06E8) || c == 0x06EB ||
        c == 0x06EC || (c >= 0x0730 && c <= 0x074A) ||
        (c >= 0x07EB && c <= 0x07F3) || (c >= 0x0816 && c <= 0x0819) ||
        (c >= 0x081B && c <= 0x0823) || (c >= 0x0825 && c <= 0x0827) ||
        (c >= 0x0829 && c <= 0x082D) || (c >= 0x0859 && c <= 0x085B) ||
        (c >= 0x08D4 && c <= 0x08E1) || (c >= 0x08E3 && c <= 0x08FE))
        return 230;
    /* class 220: below */
    if ((c >= 0x0315 && c <= 0x0319) || c == 0x031C ||
        (c >= 0x0321 && c <= 0x0322) || (c >= 0x0327 && c <= 0x0328) ||
        (c >= 0x032D && c <= 0x0333) || c == 0x0339 ||
        (c >= 0x033A && c <= 0x033C) || (c >= 0x0347 && c <= 0x0349) ||
        c == 0x034D || c == 0x034E || c == 0x0353 || c == 0x0354 ||
        c == 0x0355 || c == 0x0356 || c == 0x0359 || c == 0x035A ||
        (c >= 0x035D && c <= 0x0362) || (c >= 0x0591 && c <= 0x0592) ||
        c == 0x0596 || c == 0x059B || c == 0x05A0 || c == 0x05A1 ||
        c == 0x05A4 || c == 0x05A9 || c == 0x05AE ||
        (c >= 0x05B3 && c <= 0x05B6) || c == 0x05B9 || c == 0x05BA ||
        c == 0x05BC || c == 0x05BD || (c >= 0x05C5 && c <= 0x05C7) ||
        (c >= 0x0618 && c <= 0x061A) || (c >= 0x0653 && c <= 0x0655) ||
        c == 0x0656 || (c >= 0x0816 && c <= 0x0817) ||
        (c >= 0x08E4 && c <= 0x08FE))
        return 220;
    /* class 9: Virama */
    if (c == 0x094D || c == 0x09CD || c == 0x0A4D || c == 0x0ACD ||
        c == 0x0B4D || c == 0x0BCD || c == 0x0C4D || c == 0x0CCD ||
        c == 0x0D4D || c == 0x0DCA || c == 0x1B44 || c == 0xA806 ||
        c == 0xA8C4 || c == 0xA9C0)
        return 9;
    /* class 7: Nukta */
    if (c == 0x093C || c == 0x09BC || c == 0x0A3C || c == 0x0ABC ||
        c == 0x0B3C || c == 0x0CBC || c == 0x0F39 || c == 0x1B34 ||
        c == 0x1C37 || c == 0xA9B3)
        return 7;
    /* class 8: Kana voiced */
    if (c == 0x3099 || c == 0x309A) return 8;
    /* class 10: Cyrillic */
    if (c >= 0x0488 && c <= 0x0489) return 10;
    /* class 200: attached below */
    if (c == 0x0320 || c == 0x031A || c == 0x031B || c == 0x031D ||
        c == 0x031E || c == 0x031F || c == 0x0330 || c == 0x0331 ||
        c == 0x0334 || c == 0x0335 || c == 0x0336)
        return 200;
    /* class 216: attached above right */
    if (c == 0x0358) return 216;
    /* class 233: double above */
    if (c == 0x035F || c == 0x0360) return 233;
    /* class 234: double below */
    if (c == 0x035C || c == 0x035E || c == 0x0361) return 234;
    return 0;
}

XChar_Decomposition XChar_decompositionTag(XChar ch)
{
    uint16_t c = ch;
    if (c == 0) return XChar_NoDecomposition;
    if (c >= 0xF870 && c <= 0xF8FF) return XChar_Font;
    if (c == 0x00A0 || c == 0x2011) return XChar_NoBreak;
    /* Arabic presentation forms - Initial/Medial/Final/Isolated */
    if ((c >= 0xFB50 && c <= 0xFB58) || (c >= 0xFB72 && c <= 0xFB77) ||
        (c >= 0xFB80 && c <= 0xFB84) || (c >= 0xFBE4 && c <= 0xFBE7) ||
        (c >= 0xFBF6 && c <= 0xFBF8) || (c >= 0xFC97 && c <= 0xFC9F) ||
        (c >= 0xFD2C && c <= 0xFD2D) || (c >= 0xFDFB && c <= 0xFDFD))
        return XChar_Initial;
    if ((c >= 0xFB5D && c <= 0xFB62) || (c >= 0xFBA0 && c <= 0xFBA3) ||
        (c >= 0xFBA7 && c <= 0xFBA8) || (c >= 0xFBE8 && c <= 0xFBE9) ||
        (c >= 0xFC00 && c <= 0xFC1F) || (c >= 0xFC5E && c <= 0xFC63) ||
        (c >= 0xFCE0 && c <= 0xFCE1) || (c >= 0xFCF2 && c <= 0xFCF3) ||
        (c >= 0xFD31 && c <= 0xFD32))
        return XChar_Medial;
    if ((c >= 0xFB63 && c <= 0xFB6C) || (c >= 0xFBA9 && c <= 0xFBAF) ||
        (c >= 0xFBB1 && c <= 0xFBB2) || (c >= 0xFBF9 && c <= 0xFBFC) ||
        (c >= 0xFC20 && c <= 0xFC5D) || (c >= 0xFC64 && c <= 0xFC96) ||
        (c >= 0xFCA0 && c <= 0xFCA9) || (c >= 0xFCAB && c <= 0xFCBB) ||
        (c >= 0xFCBE && c <= 0xFCC5) || (c >= 0xFCCD && c <= 0xFCDF) ||
        (c >= 0xFCE2 && c <= 0xFCE5) || (c >= 0xFCEA && c <= 0xFCED) ||
        c == 0xFCEF || c == 0xFCF1 || (c >= 0xFD20 && c <= 0xFD2B) ||
        (c >= 0xFD2E && c <= 0xFD2F) || (c >= 0xFD33 && c <= 0xFD38))
        return XChar_Final;
    if ((c >= 0xFB50 && c <= 0xFBC1) || (c >= 0xFBF0 && c <= 0xFBF8) ||
        (c >= 0xFC00 && c <= 0xFC63) || (c >= 0xFC6D && c <= 0xFCCF) ||
        (c >= 0xFCD2 && c <= 0xFCE5) || (c >= 0xFCEA && c <= 0xFDF0) ||
        c == 0xFDF4 || c == 0xFDFC)
        return XChar_Isolated;
    if ((c >= 0x2460 && c <= 0x2473) || (c >= 0x24B6 && c <= 0x24E9) ||
        (c >= 0x3251 && c <= 0x327E) || (c >= 0x3280 && c <= 0x32BF) ||
        (c >= 0x32D0 && c <= 0x32FE) || (c >= 0x3300 && c <= 0x3357))
        return XChar_Circle;
    if (c == 0x00AA || c == 0x00B2 || c == 0x00B3 || c == 0x00B9 ||
        c == 0x00BA || (c >= 0x02B0 && c <= 0x02B8) ||
        (c >= 0x02E0 && c <= 0x02E4) || c == 0x1D2C || c == 0x1D2E ||
        (c >= 0x1D30 && c <= 0x1D3A) || (c >= 0x1D3C && c <= 0x1D4D) ||
        (c >= 0x1D4F && c <= 0x1D6A) || (c >= 0x2070 && c <= 0x207E))
        return XChar_Super;
    if ((c >= 0x2080 && c <= 0x208E) || (c >= 0x2090 && c <= 0x209C) ||
        (c >= 0x1D62 && c <= 0x1D6A))
        return XChar_Sub;
    if ((c >= 0xFE10 && c <= 0xFE19) || (c >= 0xFE30 && c <= 0xFE44))
        return XChar_Vertical;
    if ((c >= 0xFF01 && c <= 0xFF5E) || (c >= 0xFFE0 && c <= 0xFFE6))
        return XChar_Wide;
    if (c >= 0xFF61 && c <= 0xFF9F) return XChar_Narrow;
    if ((c >= 0x3300 && c <= 0x33FF) || (c >= 0xFE30 && c <= 0xFE4F) ||
        (c >= 0xFE50 && c <= 0xFE6B) || (c >= 0xFF01 && c <= 0xFFEF))
        return XChar_Compat;
    if (c >= 0x0300 && c <= 0x036F) return XChar_Canonical;
    return XChar_NoDecomposition;
}

XChar_Direction XChar_direction(XChar ch)
{
    uint16_t c = ch;
    if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) ||
        (c >= 0x08A0 && c <= 0x08FF) || (c >= 0xFB50 && c <= 0xFDFF) ||
        (c >= 0xFE70 && c <= 0xFEFF))
        return XChar_DirAL;
    if (c >= 0x0590 && c <= 0x05FF) return XChar_DirR;
    return XChar_DirL;
}

XChar_JoiningType XChar_joiningType(XChar ch)
{
    uint16_t c = ch;
    /* Dual joining */
    if ((c >= 0x0622 && c <= 0x0625) || c == 0x0627 || c == 0x0629 ||
        (c >= 0x062F && c <= 0x0632) || c == 0x0648 ||
        (c >= 0x0671 && c <= 0x0673) || (c >= 0x0675 && c <= 0x0677) ||
        (c >= 0x0688 && c <= 0x0699) || c == 0x06C0 ||
        (c >= 0x06C2 && c <= 0x06CB) || c == 0x06CD || c == 0x06CF ||
        (c >= 0x06D2 && c <= 0x06D3) || c == 0x06EE || c == 0x06EF ||
        c == 0x0710 || (c >= 0x0715 && c <= 0x0719) ||
        (c >= 0x071E && c <= 0x071F) || c == 0x0728 || c == 0x072A ||
        c == 0x072C || c == 0x072F)
        return XChar_Joining_Right;
    /* Left joining */
    if (c == 0x0621 || c == 0x0626 || c == 0x0628 ||
        (c >= 0x062A && c <= 0x062E) || (c >= 0x0633 && c <= 0x063A) ||
        (c >= 0x0641 && c <= 0x0647) || (c >= 0x0649 && c <= 0x064A) ||
        (c >= 0x066E && c <= 0x066F) || (c >= 0x0678 && c <= 0x0687) ||
        (c >= 0x069A && c <= 0x06BF) || c == 0x06C1 ||
        (c >= 0x06CC && c <= 0x06CE) || (c >= 0x06D0 && c <= 0x06D1) ||
        (c >= 0x06FA && c <= 0x06FC) || c == 0x06FF)
        return XChar_Joining_Dual;
    /* Causing */
    if (c == 0x0640 || c == 0x0620 || c == 0x0712 ||
        (c >= 0x071A && c <= 0x071D) || (c >= 0x071E && c <= 0x0727) ||
        (c >= 0x0729 && c <= 0x072B) || (c >= 0x072D && c <= 0x072E))
        return XChar_Joining_Causing;
    /* Transparent */
    if ((c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) ||
        c == 0x0670 || (c >= 0x06D6 && c <= 0x06DC) ||
        (c >= 0x06DF && c <= 0x06E4) || c == 0x06E7 || c == 0x06E8 ||
        c == 0x06EA || c == 0x06EB || (c >= 0x06EC && c <= 0x06ED))
        return XChar_Joining_Transparent;
    return XChar_Joining_None;
}

XChar_Script XChar_script(XChar ch)
{
    uint16_t c = ch;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= 0x00C0 && c <= 0x02AF)) return XChar_Script_Latin;
    if (c >= 0x0370 && c <= 0x03FF) return XChar_Script_Greek;
    if (c >= 0x0400 && c <= 0x052F) return XChar_Script_Cyrillic;
    if (c >= 0x0590 && c <= 0x05FF) return XChar_Script_Hebrew;
    if ((c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F) ||
        (c >= 0xFB50 && c <= 0xFEFF)) return XChar_Script_Arabic;
    if (c >= 0x3040 && c <= 0x309F) return XChar_Script_Hiragana;
    if (c >= 0x30A0 && c <= 0x30FF) return XChar_Script_Katakana;
    if (c >= 0x4E00 && c <= 0x9FFF) return XChar_Script_Han;
    if (c >= 0xAC00 && c <= 0xD7AF) return XChar_Script_Hangul;
    return XChar_Script_Common;
}

XChar_UnicodeVersion XChar_unicodeVersion(XChar ch)
{
    uint16_t c = ch;
    if (c <= 0x058F) return XChar_Unicode_1_1;
    if (c >= 0x0F00 && c <= 0x0FFF) return XChar_Unicode_2_0;
    if ((c >= 0x1000 && c <= 0x10CF) || (c >= 0x1200 && c <= 0x137F) ||
        (c >= 0x13A0 && c <= 0x167F) || (c >= 0x1680 && c <= 0x16FF) ||
        (c >= 0x20D0 && c <= 0x20FF)) return XChar_Unicode_3_0;
    if ((c >= 0x2C00 && c <= 0x2C5F) || (c >= 0x20B0 && c <= 0x20CF))
        return XChar_Unicode_4_1;
    if (c >= 0x07C0 && c <= 0x07FF) return XChar_Unicode_5_0;
    if ((c >= 0x08A0 && c <= 0x08FF)) return XChar_Unicode_6_1;
    if (c >= 0x2066 && c <= 0x2069) return XChar_Unicode_6_3;
    /* BMP common ranges */
    if ((c >= 0x1100 && c <= 0x11FF) || (c >= 0x3040 && c <= 0x30FF) ||
        (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) ||
        (c >= 0xE000 && c <= 0xF8FF) || (c >= 0xF900 && c <= 0xFAFF) ||
        (c >= 0x1E00 && c <= 0x1EFF) || (c >= 0x2000 && c <= 0x206F))
        return XChar_Unicode_1_1;
    return XChar_Unicode_1_1;
}

XChar_UnicodeVersion XChar_currentUnicodeVersion(void) { return XChar_Unicode_16_0; }

int XChar_digitValue(XChar ch)
{
    if (!ch || !XChar_isNumber(ch)) return -1;
    uint16_t c = ch;
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 0x0660 && c <= 0x0669) return c - 0x0660;
    if (c >= 0x06F0 && c <= 0x06F9) return c - 0x06F0;
    if (c >= 0x0966 && c <= 0x096F) return c - 0x0966;
    if (c >= 0x09E6 && c <= 0x09EF) return c - 0x09E6;
    if (c >= 0x0AE6 && c <= 0x0AEF) return c - 0x0AE6;
    if (c >= 0x0BE7 && c <= 0x0BEF) return c - 0x0BE7;
    if (c >= 0x0CE6 && c <= 0x0CEF) return c - 0x0CE6;
    if (c >= 0x0DE6 && c <= 0x0DEF) return c - 0x0DE6;
    if (c >= 0x0EE6 && c <= 0x0EEF) return c - 0x0EE6;
    if (c >= 0x0FE6 && c <= 0x0FEF) return c - 0x0FE6;
    if (c >= 0xFF10 && c <= 0xFF19) return c - 0xFF10;
    if (c >= 0x1040 && c <= 0x1049) return c - 0x1040;
    if (c >= 0x17E0 && c <= 0x17E9) return c - 0x17E0;
    if (c >= 0x1810 && c <= 0x1819) return c - 0x1810;
    if (c >= 0x2D30 && c <= 0x2D39) return c - 0x2D30;
    if (c >= 0xA620 && c <= 0xA629) return c - 0xA620;
    if (c >= 0xA830 && c <= 0xA839) return c - 0xA830;
    if (c >= 0x1D7CE && c <= 0x1D7FF) return c - 0x1D7CE;
    if (c >= 0x2070 && c <= 0x2079) return (c == 0x2070) ? 0 : (c - 0x2071 + 1);
    if (c >= 0x2080 && c <= 0x2089) return c - 0x2080;
    switch (c) {
    case 0x3007: return 0; case 0x4E00: case 0x58F9: return 1;
    case 0x4E8C: case 0x8D30: return 2; case 0x4E09: case 0x53C1: return 3;
    case 0x56DB: case 0x8086: return 4; case 0x4E95: case 0x4F0D: return 5;
    case 0x516D: case 0x9678: return 6; case 0x4E03: case 0x67D2: return 7;
    case 0x516B: case 0x634C: return 8; case 0x4E5D: case 0x7396: return 9;
    }
    return -1;
}

bool XChar_hasMirrored(XChar ch)
{
    uint16_t c = ch;
    return (c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '<' || c == '>' ||
            (c >= 0x2329 && c <= 0x232A) || (c >= 0x2039 && c <= 0x203A));
}

XChar XChar_mirroredChar(XChar ch)
{
    switch (ch) {
    case '(': return ')'; case ')': return '(';
    case '[': return ']'; case ']': return '[';
    case '{': return '}'; case '}': return '{';
    case '<': return '>'; case '>': return '<';
    }
    return ch;
}

/* ========================================================================== */
/*                    isXxx 系列判断函数                                         */
/* ========================================================================== */

bool XChar_isNull(XChar ch) { return (ch == 0); }

bool XChar_isPrint(XChar ch)
{
    if (!ch) return false;
    uint16_t c = ch;
    if (c <= 0x001F || c == 0x007F || (c >= 0x0080 && c <= 0x009F)) return false;
    if (c == 0x00AD) return false;
    if (c == 0x200B || (c >= 0x200C && c <= 0x200F)) return false;
    if (c >= 0x2028 && c <= 0x202E) return false;
    if (c >= 0x2060 && c <= 0x2069) return false;
    if (c >= 0xFFF0 && c <= 0xFFF8) return false;
    if (c == 0xFFFE || c == 0xFFFF) return false;
    return true;
}

bool XChar_isSpace(XChar ch)
{
    uint16_t c = ch;
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' ||
            c == 0x00A0 || (c >= 0x2000 && c <= 0x200A) ||
            c == 0x202F || c == 0x205F || c == 0x3000);
}

bool XChar_isPunct(XChar ch)
{
    uint16_t c = ch;
    if ((c >= 0x21 && c <= 0x2F) || (c >= 0x3A && c <= 0x40) ||
        (c >= 0x5B && c <= 0x60) || (c >= 0x7B && c <= 0x7E))
        return true;
    if ((c >= 0x2010 && c <= 0x2027) || (c >= 0x2030 && c <= 0x2043) ||
        (c >= 0x2045 && c <= 0x205E))
        return true;
    if ((c >= 0x3001 && c <= 0x3002) || (c >= 0xFF01 && c <= 0xFF0F) ||
        (c >= 0xFF1A && c <= 0xFF1F))
        return true;
    return false;
}

bool XChar_isLetter(XChar ch)
{
    uint16_t c = ch;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) return true;
    if ((c >= 0x00C0 && c <= 0x00D6) || (c >= 0x00D8 && c <= 0x00F6) ||
        (c >= 0x00F8 && c <= 0x02AF)) return true;
    if ((c >= 0x3040 && c <= 0x309F) || (c >= 0x30A0 && c <= 0x30FF)) return true;
    if ((c >= 0xAC00 && c <= 0xD7AF) || (c >= 0x1100 && c <= 0x11FF) ||
        (c >= 0x3130 && c <= 0x318F)) return true;
    if ((c >= 0x0400 && c <= 0x04FF) || (c >= 0x0500 && c <= 0x052F)) return true;
    return false;
}

bool XChar_isNumber(XChar ch)
{
    uint16_t c = ch;
    if (c >= '0' && c <= '9') return true;
    if ((c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9)) return true;
    if ((c >= 0x0966 && c <= 0x096F) || (c >= 0x09E6 && c <= 0x09EF) ||
        (c >= 0x0AE6 && c <= 0x0AEF) || (c >= 0x0BE7 && c <= 0x0BEF) ||
        (c >= 0x0CE6 && c <= 0x0CEF) || (c >= 0x0DE6 && c <= 0x0DEF) ||
        (c >= 0x0EE6 && c <= 0x0EEF) || (c >= 0x0FE6 && c <= 0x0FEF))
        return true;
    if (c >= 0xFF10 && c <= 0xFF19) return true;
    if ((c >= 0x1040 && c <= 0x1049) || (c >= 0x17E0 && c <= 0x17E9) ||
        (c >= 0x1810 && c <= 0x1819) || (c >= 0x2D30 && c <= 0x2D39) ||
        (c >= 0xA620 && c <= 0xA629) || (c >= 0xA830 && c <= 0xA839) ||
        (c >= 0x1D7CE && c <= 0x1D7FF))
        return true;
    if ((c >= 0x2070 && c <= 0x2079) || (c >= 0x2080 && c <= 0x2089)) return true;
    return false;
}

bool XChar_isLetterOrNumber(XChar ch) { return XChar_isLetter(ch) || XChar_isNumber(ch); }
bool XChar_isDigit(XChar ch) { return (ch >= '0' && ch <= '9'); }

bool XChar_isMark(XChar ch)
{
    uint16_t c = ch;
    if (c >= 0x0300 && c <= 0x036F) return true;
    if (c >= 0x1AB0 && c <= 0x1AFF) return true;
    if (c >= 0x1DC0 && c <= 0x1DFF) return true;
    if (c >= 0x20D0 && c <= 0x20FF) return true;
    if (c >= 0xFE20 && c <= 0xFE2F) return true;
    if (c >= 0x0591 && c <= 0x05BD) return true;
    if (c == 0x05BF || c == 0x05C1 || c == 0x05C2) return true;
    if (c >= 0x05C4 && c <= 0x05C7) return true;
    if (c >= 0x0610 && c <= 0x061A) return true;
    if (c >= 0x064B && c <= 0x065F) return true;
    if (c >= 0x0670 && c <= 0x0670) return true;
    if (c >= 0x06D6 && c <= 0x06DC) return true;
    if (c >= 0x06DF && c <= 0x06E4) return true;
    if (c >= 0x06E7 && c <= 0x06E8) return true;
    if (c >= 0x06EA && c <= 0x06ED) return true;
    if (c >= 0x0900 && c <= 0x0903) return true;
    if (c >= 0x093A && c <= 0x094D) return true;
    if (c >= 0x0951 && c <= 0x0957) return true;
    if (c >= 0x0962 && c <= 0x0963) return true;
    if (c >= 0x0E31 && c <= 0x0E3A) return true;
    if (c >= 0x0E47 && c <= 0x0E4E) return true;
    if (c >= 0x0F18 && c <= 0x0F19) return true;
    if (c == 0x0F35 || c == 0x0F37 || c == 0x0F39) return true;
    if (c >= 0x0F71 && c <= 0x0F84) return true;
    if (c >= 0x0F86 && c <= 0x0F87) return true;
    if (c >= 0x0F90 && c <= 0x0FBC) return true;
    return false;
}

bool XChar_isSymbol(XChar ch)
{
    uint16_t c = ch;
    if ((c >= 0x2200 && c <= 0x22FF) || (c >= 0x27C0 && c <= 0x27EF)) return true;
    if ((c >= 0x20A0 && c <= 0x20CF) || c == 0x0024 || c == 0x00A2 || c == 0x00A3)
        return true;
    if ((c >= 0x3300 && c <= 0x33FF) || (c >= 0x2100 && c <= 0x214F)) return true;
    return false;
}

bool XChar_isLower(XChar ch)
{
    uint16_t c = ch;
    if (c >= 'a' && c <= 'z') return true;
    if ((c >= 0x00DF && c <= 0x00F6) || (c >= 0x00F8 && c <= 0x00FF)) return true;
    if ((c >= 0x0100 && c <= 0x0137) || (c >= 0x013A && c <= 0x0148) ||
        (c >= 0x014B && c <= 0x017E)) return true;
    if ((c >= 0x03B1 && c <= 0x03C9) || (c >= 0x03CA && c <= 0x03CE)) return true;
    if ((c >= 0x0430 && c <= 0x044F) || (c >= 0x0451 && c <= 0x045F)) return true;
    return false;
}

bool XChar_isUpper(XChar ch)
{
    if (!ch) return false;
    uint16_t c = ch;
    if (c >= 'A' && c <= 'Z') return true;
    if ((c >= 0x00C0 && c <= 0x00D6) || (c >= 0x00D8 && c <= 0x00DE)) return true;
    if ((c >= 0x0100 && c <= 0x0137) || (c >= 0x0139 && c <= 0x0147) ||
        (c >= 0x014A && c <= 0x017D)) return true;
    if ((c >= 0x0391 && c <= 0x03A9) || (c >= 0x03AA && c <= 0x03AF)) return true;
    if ((c >= 0x0410 && c <= 0x042F) || (c >= 0x0401 && c <= 0x040F)) return true;
    return false;
}

bool XChar_isTitleCase(XChar ch)
{
    uint16_t c = ch;
    return (c == 0x01C5 || c == 0x01C8 || c == 0x01CB || c == 0x01F2 ||
            c == 0x0386 || c == 0x0388 || c == 0x0389 || c == 0x038A ||
            c == 0x038C || c == 0x038E || c == 0x038F);
}

bool XChar_isNonCharacter(XChar ch)
{
    uint16_t c = ch;
    return (c >= 0xFDD0 && c <= 0xFDEF) || c == 0xFFFE || c == 0xFFFF;
}

bool XChar_isControl(XChar ch)
{
    uint16_t c = ch;
    return (c <= 0x001F || (c >= 0x007F && c <= 0x009F));
}

bool XChar_isEmoji(XChar ch)
{
    uint16_t c = ch;
    return ((c >= 0x1F600 && c <= 0x1F64F) || (c >= 0x1F300 && c <= 0x1F5FF) ||
            (c >= 0x1F680 && c <= 0x1F6FF) || (c >= 0x1F1E0 && c <= 0x1F1FF));
}

bool XChar_isFullwidth(XChar ch)
{
    uint16_t c = ch;
    return ((c >= 0xFF01 && c <= 0xFFEF) || (c >= 0x4E00 && c <= 0x9FFF) ||
            (c >= 0x3040 && c <= 0x30FF) || (c >= 0xAC00 && c <= 0xD7AF));
}

bool XChar_isHalfwidth(XChar ch)
{
    uint16_t c = ch;
    return ((c >= 0x0020 && c <= 0x007E) || (c >= 0xFF61 && c <= 0xFF9F));
}

/* ========================================================================== */
/*                    代理对相关函数                                             */
/* ========================================================================== */

bool XChar_isHighSurrogate(XChar ch) { return (ch >= 0xD800 && ch <= 0xDBFF); }
bool XChar_isLowSurrogate(XChar ch) { return (ch >= 0xDC00 && ch <= 0xDFFF); }
bool XChar_isSurrogate(XChar ch) { return XChar_isHighSurrogate(ch) || XChar_isLowSurrogate(ch); }
bool XChar_requiresSurrogates(uint32_t ucs4) { return (ucs4 >= SURROGATE_OFFSET); }

uint32_t XChar_surrogateToUcs4(XChar high, XChar low)
{
    if (!XChar_isHighSurrogate(high) || !XChar_isLowSurrogate(low)) return 0;
    return ((uint32_t)(high - 0xD800) << 10) + (low - 0xDC00) + SURROGATE_OFFSET;
}

/* ========================================================================== */
/*                    大小写转换（使用统一查找表）                                 */
/* ========================================================================== */

XChar XChar_toUpper(XChar ch)
{
    if (!XChar_isLower(ch)) return ch;
    if (ch >= 'a' && ch <= 'z') return ch - 32;
    uint16_t mapped;
    if (caseMap_lookupLowerToUpper(ch, &mapped)) return mapped;
    return ch;
}

XChar XChar_toLower(XChar ch)
{
    if (!XChar_isUpper(ch)) return ch;
    if (ch >= 'A' && ch <= 'Z') return ch + 32;
    uint16_t mapped;
    if (caseMap_lookupUpperToLower(ch, &mapped)) return mapped;
    return ch;
}

XChar XChar_toCaseFolded(XChar ch) { return XChar_toLower(ch); }
XChar XChar_toTitleCase(XChar ch) { return XChar_toUpper(ch); }

XChar XChar_toFullwidth(XChar ch)
{
    uint16_t c = ch;
    if (c >= 0x20 && c <= 0x7E) return c + 0xFEE0;
    if (c >= 0xFF61 && c <= 0xFF9F) return c + 0x0040;
    return ch;
}

XChar XChar_toHalfwidth(XChar ch)
{
    uint16_t c = ch;
    if (c >= 0xFF01 && c <= 0xFF5E) return c - 0xFEE0;
    if (c >= 0xFFA1 && c <= 0xFFDF) return c - 0x0040;
    return ch;
}

/* ========================================================================== */
/*                    比较函数                                                   */
/* ========================================================================== */

bool XChar_equals(XChar a, XChar b, XChar_CaseSensitivity cs)
{
    return (cs == XChar_CaseSensitive) ? (a == b) : (XChar_toLower(a) == XChar_toLower(b));
}

int32_t XChar_compare(XChar a, XChar b) { return (a > b) ? 1 : (a < b ? -1 : 0); }

/* ========================================================================== */
/*                    扩展功能                                                   */
/* ========================================================================== */

uint8_t XChar_toUtf8Size(XChar ch)
{
    if (ch <= 0x7F) return 1;
    if (ch <= 0x7FF) return 2;
    return 3;
}

size_t XChar_getInputLength(const XChar* xchars, size_t input_count)
{
    if (!xchars) return 0;
    size_t len = 0;
    if (input_count == 0) { while (xchars[len] != 0) len++; }
    else { while (len < input_count && xchars[len] != 0) len++; }
    return len;
}

/* ========================================================================== */
/*              _2 后缀函数（统一宏展开）                                         */
/* ========================================================================== */

DEFINE_XCHAR_FUNC2_RET0(XChar_category_2, XChar_Category, XChar_category((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_combiningClass_2, uint8_t, XChar_combiningClass((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_decompositionTag_2, XChar_Decomposition, XChar_decompositionTag((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_direction_2, XChar_Direction, XChar_direction((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_joiningType_2, XChar_JoiningType, XChar_joiningType((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_script_2, XChar_Script, XChar_script((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_unicodeVersion_2, XChar_UnicodeVersion, XChar_unicodeVersion((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_digitValue_2, int, XChar_digitValue((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_hasMirrored_2, bool, XChar_hasMirrored((XChar)ucs4))

uint32_t XChar_mirroredChar_2(uint32_t ucs4)
{
    if (ucs4 <= 0xFFFF) return (uint32_t)XChar_mirroredChar((XChar)ucs4);
    return ucs4;
}

bool XChar_isNull_2(uint32_t ucs4) { return (ucs4 == 0); }

DEFINE_XCHAR_FUNC2_RET0(XChar_isPrint_2, bool, XChar_isPrint((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isSpace_2, bool, XChar_isSpace((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isPunct_2, bool, XChar_isPunct((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isLetter_2, bool, XChar_isLetter((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isNumber_2, bool, XChar_isNumber((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isLetterOrNumber_2, bool, XChar_isLetterOrNumber((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isDigit_2, bool, XChar_isDigit((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isMark_2, bool, XChar_isMark((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isSymbol_2, bool, XChar_isSymbol((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isUpper_2, bool, XChar_isUpper((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isLower_2, bool, XChar_isLower((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isTitleCase_2, bool, XChar_isTitleCase((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isHighSurrogate_2, bool, XChar_isHighSurrogate((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isLowSurrogate_2, bool, XChar_isLowSurrogate((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET0(XChar_isSurrogate_2, bool, XChar_isSurrogate((XChar)ucs4))

bool XChar_isNonCharacter_2(uint32_t ucs4)
{
    if (ucs4 > 0x10FFFF) return true;
    if (ucs4 <= 0xFFFF) return XChar_isNonCharacter((XChar)ucs4);
    return ((ucs4 & 0xFFFF) == 0xFFFE || (ucs4 & 0xFFFF) == 0xFFFF);
}

DEFINE_XCHAR_FUNC2_RET_U32(XChar_toUpper_2, XChar_toUpper((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET_U32(XChar_toLower_2, XChar_toLower((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET_U32(XChar_toCaseFolded_2, XChar_toCaseFolded((XChar)ucs4))
DEFINE_XCHAR_FUNC2_RET_U32(XChar_toTitleCase_2, XChar_toTitleCase((XChar)ucs4))

/* ========================================================================== */
/*                          流式转换：UTF-8                                     */
/* ========================================================================== */

int64_t XChar_fromUtf8Stream(const uint8_t* utf8, size_t input_size, XChar* out, size_t max_out)
{
    if (!utf8) return -1;
    size_t utf8_len = stream_input_len_u8(utf8, input_size);

    if (!out || max_out == 0) {
        /* 仅计算数量 */
        size_t count = 0;
        for (size_t i = 0; i < utf8_len; ) {
            uint8_t c = utf8[i];
            if ((c & 0x80) == 0) { i++; count++; }
            else if ((c & 0xE0) == 0xC0) { if (i + 1 >= utf8_len) break; i += 2; count++; }
            else if ((c & 0xF0) == 0xE0) { if (i + 2 >= utf8_len) break; i += 3; count++; }
            else if ((c & 0xF8) == 0xF0) { if (i + 3 >= utf8_len) break; i += 4; count += 2; }
            else return -1;
        }
        return (int64_t)count;
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < utf8_len && out_idx + 1 < max_out; ) {
        uint8_t c = utf8[i];
        uint32_t code = 0;
        if ((c & 0x80) == 0) { code = c; i++; }
        else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= utf8_len) return -1;
            code = ((c & 0x1F) << 6) | (utf8[i+1] & 0x3F); i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= utf8_len) return -1;
            code = ((c & 0x0F) << 12) | ((utf8[i+1] & 0x3F) << 6) | (utf8[i+2] & 0x3F); i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= utf8_len) return -1;
            code = ((c & 0x07) << 18) | ((utf8[i+1] & 0x3F) << 12) |
                   ((utf8[i+2] & 0x3F) << 6) | (utf8[i+3] & 0x3F); i += 4;
        } else return -1;

        if (code <= 0xFFFF) {
            out[out_idx++] = (XChar)code;
        } else {
            if (out_idx + 1 >= max_out) return -1;
            out[out_idx++] = XChar_fromUnicode(code);
            out[out_idx++] = XChar_from_unicode_low(code);
        }
    }
    if (out_idx < max_out) out[out_idx] = 0;
    else return -1;
    return (int64_t)out_idx;
}

int64_t XChar_toUtf8Stream(const XChar* ch, size_t input_count, uint8_t* utf8, size_t max_utf8)
{
    if (!ch) return -1;
    size_t ch_count = stream_input_len_xchar(ch, input_count);

    if (!utf8 || max_utf8 == 0) {
        size_t bytes = 0;
        for (size_t i = 0; i < ch_count; i++) {
            uint32_t code = ch[i];
            if (XChar_isHighSurrogate(ch[i]) && i + 1 < ch_count) { code = XChar_surrogateToUcs4(ch[i], ch[i+1]); i++; }
            if (code <= 0x7F) bytes += 1; else if (code <= 0x7FF) bytes += 2;
            else if (code <= 0xFFFF) bytes += 3; else if (code <= 0x10FFFF) bytes += 4;
            else return -1;
        }
        return (int64_t)bytes;
    }

    size_t out_idx = 0;
    for (size_t i = 0; i < ch_count && out_idx + 1 < max_utf8; i++) {
        uint32_t code = ch[i];
        if (XChar_isHighSurrogate(ch[i]) && i + 1 < ch_count) { code = XChar_surrogateToUcs4(ch[i], ch[i+1]); i++; }
        if (code <= 0x7F) {
            if (out_idx + 1 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)code;
        } else if (code <= 0x7FF) {
            if (out_idx + 2 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)(0xC0 | (code >> 6));
            utf8[out_idx++] = (uint8_t)(0x80 | (code & 0x3F));
        } else if (code <= 0xFFFF) {
            if (out_idx + 3 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)(0xE0 | (code >> 12));
            utf8[out_idx++] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
            utf8[out_idx++] = (uint8_t)(0x80 | (code & 0x3F));
        } else if (code <= 0x10FFFF) {
            if (out_idx + 4 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)(0xF0 | (code >> 18));
            utf8[out_idx++] = (uint8_t)(0x80 | ((code >> 12) & 0x3F));
            utf8[out_idx++] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
            utf8[out_idx++] = (uint8_t)(0x80 | (code & 0x3F));
        } else return -1;
    }
    if (out_idx < max_utf8) utf8[out_idx] = '\0'; else return -1;
    return (int64_t)out_idx;
}

/* ========================================================================== */
/*                          流式转换：UTF-16                                    */
/* ========================================================================== */

int64_t XChar_fromUtf16Stream(const uint16_t* utf16_str, size_t input_size, XChar* out, size_t max_count)
{
    if (!utf16_str) return -1;
    size_t len = stream_input_len_u16(utf16_str, input_size);
    if (!out || max_count == 0) return (int64_t)len;
    size_t out_idx = 0;
    while (out_idx < len && out_idx + 1 < max_count) {
        out[out_idx] = (XChar)utf16_str[out_idx]; out_idx++;
    }
    if (out_idx < max_count) out[out_idx] = 0; else return -1;
    return (int64_t)out_idx;
}

int64_t XChar_toUtf16Stream(const XChar* xchars, size_t input_count, uint16_t* out_buf, size_t buf_size)
{
    if (!xchars) return -1;
    size_t count = stream_input_len_xchar(xchars, input_count);
    if (!out_buf || buf_size == 0) return (int64_t)count;
    size_t out_idx = 0;
    while (out_idx < count && out_idx + 1 < buf_size) {
        out_buf[out_idx] = xchars[out_idx]; out_idx++;
    }
    if (out_idx < buf_size) out_buf[out_idx] = 0; else return -1;
    return (int64_t)out_idx;
}

/* ========================================================================== */
/*                          流式转换：UTF-32                                    */
/* ========================================================================== */

int64_t XChar_fromUtf32Stream(const uint32_t* utf32, size_t input_count, XChar* out, size_t max_out)
{
    if (!utf32) return -1;
    size_t count = stream_input_len_u32(utf32, input_count);
    if (!out || max_out == 0) {
        size_t n = 0;
        for (size_t i = 0; i < count; i++) n += (utf32[i] > 0xFFFF) ? 2 : 1;
        return (int64_t)n;
    }
    size_t out_idx = 0;
    for (size_t i = 0; i < count && out_idx + 1 < max_out; i++) {
        uint32_t code = utf32[i];
        if (code > UNICODE_MAX_CODEPOINT) return -1;
        if (code <= 0xFFFF) { out[out_idx++] = (XChar)code; }
        else {
            if (out_idx + 1 >= max_out) return -1;
            out[out_idx++] = XChar_fromUnicode(code);
            out[out_idx++] = XChar_from_unicode_low(code);
        }
    }
    if (out_idx < max_out) out[out_idx] = 0; else return -1;
    return (int64_t)out_idx;
}

int64_t XChar_toUtf32Stream(const XChar* ch, size_t input_count, uint32_t* utf32, size_t max_utf32)
{
    if (!ch) return -1;
    size_t ch_count = stream_input_len_xchar(ch, input_count);
    if (!utf32 || max_utf32 == 0) {
        size_t n = 0;
        for (size_t i = 0; i < ch_count; i++) {
            if (XChar_isHighSurrogate(ch[i]) && i + 1 < ch_count) i++;
            n++;
        }
        return (int64_t)n;
    }
    size_t out_idx = 0;
    for (size_t i = 0; i < ch_count && out_idx + 1 < max_utf32; i++) {
        if (XChar_isHighSurrogate(ch[i]) && i + 1 < ch_count) {
            utf32[out_idx++] = XChar_surrogateToUcs4(ch[i], ch[i+1]); i++;
        } else { utf32[out_idx++] = ch[i]; }
    }
    if (out_idx < max_utf32) utf32[out_idx] = 0; else return -1;
    return (int64_t)out_idx;
}

/* ========================================================================== */
/*                          流式转换：Latin-1                                   */
/* ========================================================================== */

int64_t XChar_fromLatin1Stream(const uint8_t* latin1, size_t input_size, XChar* out, size_t max_out)
{
    if (!latin1) return -1;
    size_t len = stream_input_len_u8(latin1, input_size);
    if (!out || max_out == 0) return (int64_t)len;
    size_t out_idx = 0;
    while (out_idx < len && out_idx + 1 < max_out) {
        out[out_idx] = (XChar)latin1[out_idx]; out_idx++;
    }
    if (out_idx < max_out) out[out_idx] = 0; else return -1;
    return (int64_t)out_idx;
}

int64_t XChar_toLatin1Stream(const XChar* ch, size_t input_count, uint8_t* latin1, size_t max_latin1)
{
    if (!ch) return -1;
    size_t count = stream_input_len_xchar(ch, input_count);
    for (size_t i = 0; i < count; i++) { if (ch[i] > 0xFF) return -1; }
    if (!latin1 || max_latin1 == 0) return (int64_t)count;
    size_t out_idx = 0;
    while (out_idx < count && out_idx + 1 < max_latin1) {
        latin1[out_idx] = (uint8_t)ch[out_idx]; out_idx++;
    }
    if (out_idx < max_latin1) latin1[out_idx] = '\0'; else return -1;
    return (int64_t)out_idx;
}

/* ========================================================================== */
/*              流式转换：GBK/本地编码（委托平台抽象层）                           */
/* ========================================================================== */

int64_t XChar_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out)
{
    return XCharPlatform_fromGbkStream(gbk, input_size, out, max_out);
}

int64_t XChar_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    return XCharPlatform_toGbkStream(ch, input_count, gbk, max_gbk);
}

int64_t XChar_fromLocalStream(const char* local_str, size_t input_size, XChar* out, size_t max_out)
{
    return XCharPlatform_fromGbkStream(local_str, input_size, out, max_out);
}

int64_t XChar_toLocalStream(const XChar* ch, size_t input_count, char* local_str, size_t max_local)
{
    return XCharPlatform_toGbkStream(ch, input_count, local_str, max_local);
}

/* ========================================================================== */
/*              UTF-8 与 GBK 互转（委托平台抽象层）                               */
/* ========================================================================== */

int64_t XChar_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
{
    return XCharPlatform_utf8ToGbkStream(utf8_str, input_size, gbk_buf, max_len);
}

int64_t XChar_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
{
    return XCharPlatform_gbkToUtf8Stream(gbk_str, input_size, utf8_buf, max_len);
}

/* ========================================================================== */
/*                     数值与字符串互转                                          */
/* ========================================================================== */

static int XChar_digit_to_value(XChar ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    return -1;
}

static XChar XChar_value_to_digit(int value, bool uppercase) {
    if (value >= 0 && value <= 9) return '0' + value;
    if (value >= 10 && value <= 35) return uppercase ? ('A' + (value - 10)) : ('a' + (value - 10));
    return 0;
}

static unsigned long long XChar_parse_unsigned(const XChar* xchars, size_t input_count, int base, bool* success)
{
    if (success) *success = false;
    if (!xchars || base < 2 || base > 36) return 0;
    size_t len = XChar_getInputLength(xchars, input_count);
    if (len == 0) return 0;
    unsigned long long result = 0;
    size_t i = 0;
    while (i < len && (xchars[i] == ' ' || xchars[i] == '\t' || xchars[i] == '\n')) i++;
    bool has_digit = false;
    while (i < len) {
        int d = XChar_digit_to_value(xchars[i]);
        if (d == -1 || d >= base) break;
        if (result > (ULLONG_MAX - d) / base) return 0;
        result = result * base + d; has_digit = true; i++;
    }
    if (has_digit && success) *success = true;
    return result;
}

static long long XChar_parse_signed(const XChar* xchars, size_t input_count, int base, bool* success)
{
    if (success) *success = false;
    if (!xchars || base < 2 || base > 36) return 0;
    size_t len = XChar_getInputLength(xchars, input_count);
    if (len == 0) return 0;
    size_t i = 0; int sign = 1;
    while (i < len && (xchars[i] == ' ' || xchars[i] == '\t' || xchars[i] == '\n')) i++;
    if (i >= len) return 0;
    if (xchars[i] == '-') { sign = -1; i++; }
    else if (xchars[i] == '+') i++;
    unsigned long long abs_val = XChar_parse_unsigned(&xchars[i], len - i, base, success);
    if (success && !*success) return 0;
    bool overflow = (sign == 1) ? (abs_val > (unsigned long long)LLONG_MAX) :
                    (abs_val > (unsigned long long)LLONG_MAX + 1);
    if (overflow) { if (success) *success = false; return 0; }
    if (success) *success = true;
    return (long long)(abs_val * sign);
}

static double XChar_parse_float(const XChar* xchars, size_t input_count, bool* success)
{
    if (success) *success = false;
    if (!xchars) return 0.0;
    size_t len = XChar_getInputLength(xchars, input_count);
    if (len == 0) return 0.0;
    size_t i = 0; int sign = 1; double result = 0.0; bool has_digits = false;
    while (i < len && (xchars[i] == ' ' || xchars[i] == '\t' || xchars[i] == '\n')) i++;
    if (i >= len) return 0.0;
    if (xchars[i] == '-') { sign = -1; i++; } else if (xchars[i] == '+') i++;
    while (i < len) {
        int d = XChar_digit_to_value(xchars[i]); if (d < 0 || d > 9) break;
        result = result * 10.0 + d; has_digits = true; i++;
    }
    if (i < len && xchars[i] == '.') {
        i++; double div = 10.0;
        while (i < len) {
            int d = XChar_digit_to_value(xchars[i]); if (d < 0 || d > 9) break;
            result += d / div; div *= 10.0; has_digits = true; i++;
        }
    }
    if (i < len && (xchars[i] == 'e' || xchars[i] == 'E')) {
        i++; int es = 1; int exp = 0; bool has_exp = false;
        if (i < len && (xchars[i] == '-' || xchars[i] == '+')) { es = (xchars[i] == '-') ? -1 : 1; i++; }
        while (i < len) {
            int d = XChar_digit_to_value(xchars[i]); if (d < 0 || d > 9) break;
            exp = exp * 10 + d; has_exp = true; i++;
        }
        if (has_exp) { exp *= es; while (exp > 0) { result *= 10.0; exp--; } while (exp < 0) { result /= 10.0; exp++; } }
    }
    if (has_digits && success) *success = true;
    return result * sign;
}

static size_t calc_uint_decimal_len(unsigned long long num) {
    if (num == 0) return 1;
    size_t len = 0; while (num > 0) { len++; num /= 10; } return len;
}

static int64_t XChar_from_unsigned_num(unsigned long long value, int base, XChar* out, size_t max_out, bool uppercase)
{
    if (base < 2 || base > 36) return -1;
    if (value == 0) {
        if (!out) return 1;
        if (max_out < 1) return -1;
        out[0] = '0'; if (max_out >= 2) out[1] = 0; return 1;
    }
    size_t req = 0; { unsigned long long t = value; while (t > 0) { t /= base; req++; } }
    if (!out) return (int64_t)req;
    if (max_out < req) return -1;
    unsigned long long t = value;
    for (size_t i = 0; i < req; i++) { out[req - 1 - i] = XChar_value_to_digit((int)(t % base), uppercase); t /= base; }
    if (max_out > req) out[req] = 0;
    return (int64_t)req;
}

static int64_t XChar_from_signed_num(long long value, int base, XChar* out, size_t max_out, bool uppercase)
{
    if (base < 2 || base > 36) return -1;
    if (value < 0) {
        int64_t abs_len = XChar_from_unsigned_num((unsigned long long)(-value), base, NULL, 0, uppercase);
        if (abs_len == -1) return -1;
        int64_t req = 1 + abs_len;
        if (!out) return req;
        if (max_out < (size_t)req) return -1;
        out[0] = '-';
        int64_t w = XChar_from_unsigned_num((unsigned long long)(-value), base, &out[1], max_out - 1, uppercase);
        if (w == -1) return -1;
        if (max_out > (size_t)req) out[req] = 0;
        return req;
    }
    return XChar_from_unsigned_num((unsigned long long)value, base, out, max_out, uppercase);
}

static int64_t XChar_from_floating_point(double value, char format, XChar* out, size_t max_out, int precision)
{
    if (format != 'f' && format != 'F' && format != 'e' && format != 'E' && format != 'g' && format != 'G') return -1;
    if (!out && max_out != 0) return -1;
    if (isnan(value)) {
        if (!out) return 3; if (max_out < 3) return -1;
        out[0]='N'; out[1]='a'; out[2]='N'; if (max_out >= 4) out[3]=0; return 3;
    }
    if (isinf(value)) {
        size_t inf_len = (value > 0) ? 3 : 4;
        if (!out) return (int64_t)inf_len; if (max_out < inf_len) return -1;
        if (value < 0) out[0] = '-';
        out[(value<0)?1:0] = 'I'; out[(value<0)?2:1] = 'n'; out[(value<0)?3:2] = 'f';
        if (max_out > inf_len) out[inf_len] = 0; return (int64_t)inf_len;
    }
    bool neg = (value < 0.0); double abs_val = neg ? -value : value;
    int prec = precision; bool upper = (format=='F'||format=='E'||format=='G');
    if (prec < 0) { prec = (format=='f'||format=='F') ? 6 : (format=='e'||format=='E') ? 5 : 6; }
    if (prec > 30) prec = 30; if (prec < 0) prec = 0;
    unsigned long long int_part = 0; double frac = 0.0; int exponent = 0; bool sci = false;
    if (abs_val == 0.0) { int_part = 0; frac = 0.0; sci = (format=='e'||format=='E'); }
    else if (format=='f'||format=='F') { int_part = (unsigned long long)abs_val; frac = abs_val - (double)int_part; }
    else if (format=='e'||format=='E') { sci = true; exponent = (int)floor(log10(abs_val)); double sv = abs_val / pow(10.0, exponent); int_part = (unsigned long long)sv; frac = sv - (double)int_part; }
    else { double l = log10(abs_val); sci = (fabs(l) >= (double)prec || l < -4.0);
        if (sci) { exponent = (int)floor(l); double sv = abs_val / pow(10.0, exponent); int_part = (unsigned long long)sv; frac = sv - (double)int_part; prec = prec > 0 ? prec - 1 : 0; }
        else { int_part = (unsigned long long)abs_val; frac = abs_val - (double)int_part; prec = (int)(prec - calc_uint_decimal_len(int_part)); if (prec < 0) prec = 0; } }
    size_t sign_len = neg ? 1 : 0; size_t int_len = calc_uint_decimal_len(int_part);
    size_t frac_len = 0; size_t exp_len = 0;
    if (prec > 0) {
        frac_len = 1 + (size_t)prec;
        if (format=='g'||format=='G') { double tf = frac; size_t lnz = 0; bool hasDigit = false; for (int i=0;i<prec;i++){tf*=10.0;int d=(int)tf;if(d!=0){hasDigit=true;lnz=(size_t)i;}tf-=(double)d;} if (!hasDigit&&tf<0.5)frac_len=0; else frac_len=1+(hasDigit?(lnz+1):1); }
    }
    if (sci) { exp_len = 1 + 1 + 2; if (abs(exponent) >= 100) exp_len++; if (exponent == 0) exp_len = 1 + 1 + 2; }
    size_t req = sign_len + int_len + frac_len + exp_len;
    if (!out) return (req <= (size_t)INT64_MAX) ? (int64_t)req : -1;
    if (max_out < req) return -1;
    size_t pos = 0;
    if (neg) out[pos++] = '-';
    if (int_part == 0) { out[pos++] = '0'; }
    else { XChar ib[64]; size_t bi = 0; unsigned long long ti = int_part; while(ti>0){ib[bi++]='0'+(int)(ti%10);ti/=10;} for(size_t i=0;i<bi;i++) out[pos++]=ib[bi-1-i]; }
    if (frac_len > 0) {
        out[pos++] = '.'; double tf = frac; size_t afl = frac_len - 1; int rc = 0;
        for (size_t i=0;i<afl;i++){tf*=10.0;int d=(int)tf;if(i==afl-1){double nf=tf-(double)d;rc=(nf>=0.5)?1:0;}tf-=(double)d;}
        tf = frac;
        for (size_t i=0;i<afl;i++){tf*=10.0;int d=(int)tf;if(i==afl-1)d+=rc;if(d>=10){d=0;if(pos>sign_len+int_len){pos--;while(pos>sign_len+int_len&&out[pos]=='9'){out[pos]='0';pos--;}out[pos]+=1;pos++;}}out[pos++]='0'+d;tf-=(double)d;}
    }
    if (sci) { out[pos++] = upper ? 'E' : 'e'; bool en = (exponent<0); out[pos++] = en ? '-' : '+'; int ae = en ? -exponent : exponent;
        XChar eb[4]; size_t ei = 0; int te = ae; if(te==0)eb[ei++]='0'; else while(te>0){eb[ei++]='0'+(te%10);te/=10;} while(ei<2)eb[ei++]='0'; for(size_t i=0;i<ei;i++)out[pos++]=eb[ei-1-i]; }
    if (max_out > req) out[req] = 0;
    return (int64_t)req;
}

short XChar_toShort(const XChar* x, size_t n, int b, bool* s) { long long v = XChar_parse_signed(x,n,b,s); if(s&&!*s)return 0; if(v<SHRT_MIN||v>SHRT_MAX){if(s)*s=false;return 0;} return(short)v; }
int XChar_toInt(const XChar* x, size_t n, int b, bool* s) { long long v = XChar_parse_signed(x,n,b,s); if(v<INT_MIN||v>INT_MAX){if(s)*s=false;return 0;} return(int)v; }
long XChar_toLong(const XChar* x, size_t n, int b, bool* s) { long long v = XChar_parse_signed(x,n,b,s); if(s&&!*s)return 0; if(v<LONG_MIN||v>LONG_MAX){if(s)*s=false;return 0;} return(long)v; }
long long XChar_toLongLong(const XChar* x, size_t n, int b, bool* s) { return XChar_parse_signed(x,n,b,s); }
unsigned short XChar_toUShort(const XChar* x, size_t n, int b, bool* s) { unsigned long long v=XChar_parse_unsigned(x,n,b,s); if(s&&!*s)return 0; if(v>USHRT_MAX){if(s)*s=false;return 0;} return(unsigned short)v; }
unsigned int XChar_toUInt(const XChar* x, size_t n, int b, bool* s) { unsigned long long v=XChar_parse_unsigned(x,n,b,s); if(s&&!*s)return 0; if(v>UINT_MAX){if(s)*s=false;return 0;} return(unsigned int)v; }
unsigned long XChar_toULong(const XChar* x, size_t n, int b, bool* s) { unsigned long long v=XChar_parse_unsigned(x,n,b,s); if(s&&!*s)return 0; if(v>ULONG_MAX){if(s)*s=false;return 0;} return(unsigned long)v; }
unsigned long long XChar_toULongLong(const XChar* x, size_t n, int b, bool* s) { return XChar_parse_unsigned(x,n,b,s); }
float XChar_toFloat(const XChar* x, size_t n, bool* s) { double v=XChar_parse_float(x,n,s); if(s&&!*s)return 0.0f; if(v>FLT_MAX||v<-FLT_MAX){if(s)*s=false;return 0.0f;} return(float)v; }
double XChar_toDouble(const XChar* x, size_t n, bool* s) { return XChar_parse_float(x,n,s); }

int64_t XChar_fromShort(short v, int b, XChar* o, size_t m, bool u) { return XChar_from_signed_num((long long)v,b,o,m,u); }
int64_t XChar_fromInt(int v, int b, XChar* o, size_t m, bool u) { return XChar_from_signed_num((long long)v,b,o,m,u); }
int64_t XChar_fromLong(long v, int b, XChar* o, size_t m, bool u) { return XChar_from_signed_num((long long)v,b,o,m,u); }
int64_t XChar_fromLongLong(long long v, int b, XChar* o, size_t m, bool u) { return XChar_from_signed_num(v,b,o,m,u); }
int64_t XChar_fromUShort(unsigned short v, int b, XChar* o, size_t m, bool u) { return XChar_from_unsigned_num((unsigned long long)v,b,o,m,u); }
int64_t XChar_fromUInt(unsigned int v, int b, XChar* o, size_t m, bool u) { return XChar_from_unsigned_num((unsigned long long)v,b,o,m,u); }
int64_t XChar_fromULong(unsigned long v, int b, XChar* o, size_t m, bool u) { return XChar_from_unsigned_num((unsigned long long)v,b,o,m,u); }
int64_t XChar_fromULongLong(unsigned long long v, int b, XChar* o, size_t m, bool u) { return XChar_from_unsigned_num(v,b,o,m,u); }
int64_t XChar_fromFloat(float v, char f, XChar* o, size_t m, int p) { return XChar_from_floating_point((double)v,f,o,m,p); }
int64_t XChar_fromDouble(double v, char f, XChar* o, size_t m, int p) { return XChar_from_floating_point(v,f,o,m,p); }