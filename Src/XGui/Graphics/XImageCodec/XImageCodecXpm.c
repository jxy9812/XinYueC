/******************************************************************************
 * @file       XImageCodecXpm.c
 * @brief      XPM（X11 Pixmap）图像编解码实现，对齐 Qt 6.8 QXpmHandler。
 * @note       XPM 是 C 源码形式的彩色图像。实现支持 Qt 处理器使用的
 *             XPM 文件头标记、1..15 字符像素键、调色板颜色字段、None
 *             透明色以及 64^4 颜色编码范围；所有动态存储使用项目内存
 *             接口，不调用平台 API 或标准 C 内存分配函数。
 ******************************************************************************/
#include "XImageCodecInternal.h"
#include "XImageCodec_config.h"
#include "XMemory.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if XIMAGECODEC_ON && XIMAGECODEC_XPM_ON

typedef struct XImageCodecXpmHeader
{
    int m_width;             /**< 图像宽度。 */
    int m_height;            /**< 图像高度。 */
    int m_colorCount;        /**< 调色板数量。 */
    int m_charsPerPixel;     /**< 每个像素键的字符数。 */
} XImageCodecXpmHeader;

typedef struct XImageCodecXpmColor
{
    char m_key[16];          /**< 像素键，末尾带 C 字符串结束符。 */
    uint32_t m_hash;         /**< 与 Qt xpmHash 相同的 32 位滚动哈希。 */
    uint32_t m_color;        /**< ARGB32 颜色；None 使用透明值。 */
} XImageCodecXpmColor;

static bool xpm_space(unsigned char value)
{
    return value == (unsigned char)' ' || value == (unsigned char)'\t' ||
           value == (unsigned char)'\n' || value == (unsigned char)'\r' ||
           value == (unsigned char)'\v' || value == (unsigned char)'\f';
}

/* Qt 的 xpmHash 在 unsigned int 中逐字节左移，故故意使用 uint32_t。 */
static uint32_t xpm_hash(const char* value, size_t length)
{
    uint32_t result = 0;
    size_t i;
    if (!value) return 0;
    for (i = 0; i < length; ++i)
        result = (result << 8) + (uint32_t)(unsigned char)value[i];
    return result;
}

/* 从任意注释/声明区域取得下一段双引号字符串。Qt 不解释反斜杠转义，
 * 因而这里同样把第一个双引号作为开始、下一个双引号作为结束。 */
static char* xpm_nextString(const uint8_t* data, size_t size, size_t* pos,
                            size_t* length)
{
    size_t begin;
    size_t end;
    char* result;
    if (!data || !pos || !length) return NULL;
    while (*pos < size && data[*pos] != (uint8_t)'"') ++*pos;
    if (*pos >= size) return NULL;
    begin = ++*pos;
    end = begin;
    while (end < size && data[end] != (uint8_t)'"') ++end;
    if (end >= size) return NULL;
    result = (char*)XMalloc_System(end - begin + 1u);
    if (!result) return NULL;
    if (end > begin) memcpy(result, data + begin, end - begin);
    result[end - begin] = '\0';
    *pos = end + 1u;
    *length = end - begin;
    return result;
}

static bool xpm_readInt(const char* value, size_t length, size_t* pos,
                        int* output)
{
    size_t cursor = *pos;
    unsigned long long magnitude = 0;
    bool negative = false;
    bool haveDigit = false;
    if (!value || !pos || !output) return false;
    while (cursor < length && xpm_space((unsigned char)value[cursor])) ++cursor;
    if (cursor < length && (value[cursor] == '+' || value[cursor] == '-')) {
        negative = value[cursor] == '-';
        ++cursor;
    }
    while (cursor < length && value[cursor] >= '0' && value[cursor] <= '9') {
        unsigned int digit = (unsigned int)(value[cursor] - '0');
        haveDigit = true;
        if (magnitude > ((unsigned long long)INT_MAX - digit) / 10ULL)
            return false;
        magnitude = magnitude * 10ULL + digit;
        ++cursor;
    }
    if (!haveDigit || negative || magnitude > (unsigned long long)INT_MAX)
        return false;
    *pos = cursor;
    *output = (int)magnitude;
    return true;
}

static bool xpm_parseHeader(const uint8_t* data, size_t size,
                            XImageCodecXpmHeader* header, size_t* pos)
{
    char* line;
    size_t length;
    size_t cursor = 0;
    size_t lineCursor = 0;
    bool ok;
    if (!data || !header || !pos || size < 6u ||
        memcmp(data, "/* XPM", 6u) != 0)
        return false;
    line = xpm_nextString(data, size, &cursor, &length);
    if (!line) return false;
    ok = xpm_readInt(line, length, &lineCursor, &header->m_width) &&
         xpm_readInt(line, length, &lineCursor, &header->m_height) &&
         xpm_readInt(line, length, &lineCursor, &header->m_colorCount) &&
         xpm_readInt(line, length, &lineCursor, &header->m_charsPerPixel);
    XFree_System(line);
    if (!ok || header->m_width <= 0 || header->m_width > 32767 ||
        header->m_height <= 0 || header->m_height > 32767 ||
        header->m_colorCount <= 0 || header->m_colorCount > 16777216 ||
        header->m_charsPerPixel <= 0 || header->m_charsPerPixel > 15)
        return false;
    *pos = cursor;
    return true;
}

static bool xpm_equalWord(const char* value, size_t length, const char* word)
{
    size_t wordLength = word ? strlen(word) : 0;
    size_t i;
    if (!value || !word || length != wordLength) return false;
    for (i = 0; i < length; ++i)
        if (tolower((unsigned char)value[i]) !=
            tolower((unsigned char)word[i]))
            return false;
    return true;
}

static bool xpm_isPrefix(const char* value, size_t length)
{
    return xpm_equalWord(value, length, "c") ||
           xpm_equalWord(value, length, "g") ||
           xpm_equalWord(value, length, "g4") ||
           xpm_equalWord(value, length, "m") ||
           xpm_equalWord(value, length, "s");
}

/* QColor 的 XPM 十六进制颜色解析支持 #RGB、#RRGGBB、#RRRRGGGGBBBB、
 * #AARRGGBB 等形式。QXpmHandler 会先把 ImageMagick 的 #RRGGBBAA
 * 截去末尾 Alpha；调用者在此之前完成同一截断。 */
static int xpm_hexDigit(unsigned char value)
{
    if (value >= (unsigned char)'0' && value <= (unsigned char)'9')
        return (int)(value - (unsigned char)'0');
    if (value >= (unsigned char)'a' && value <= (unsigned char)'f')
        return (int)(value - (unsigned char)'a') + 10;
    if (value >= (unsigned char)'A' && value <= (unsigned char)'F')
        return (int)(value - (unsigned char)'A') + 10;
    return -1;
}

static bool xpm_hexColor(const char* value, size_t length, uint32_t* color)
{
    size_t digits;
    size_t i;
    int components[4] = {0, 0, 0, 255};
    if (!value || !color || length < 2u || value[0] != '#') return false;
    digits = length - 1u;
    if (digits == 4u) {
        /* 4 个十六进制字符对应 Qt get_hex_rgb() 的无效长度。 */
        return false;
    }
    if (digits != 3u && digits != 6u && digits != 8u &&
        digits != 9u && digits != 12u)
        return false;
    for (i = 0; i < digits; ++i)
        if (xpm_hexDigit((unsigned char)value[i + 1u]) < 0) return false;
    if (digits == 3u) {
        components[0] = xpm_hexDigit((unsigned char)value[1]) * 17;
        components[1] = xpm_hexDigit((unsigned char)value[2]) * 17;
        components[2] = xpm_hexDigit((unsigned char)value[3]) * 17;
    } else if (digits == 6u || digits == 8u) {
        size_t offset = digits == 8u ? 2u : 0u;
        if (digits == 8u)
            /* Qt QColor 的 #AARRGGBB 分支把 alpha 当作普通两位
               十六进制整数；不能套用 #RGB 的 0x11 扩展规则。 */
            components[3] = xpm_hexDigit((unsigned char)value[1]) * 16 +
                            xpm_hexDigit((unsigned char)value[2]);
        components[0] = xpm_hexDigit((unsigned char)value[1u + offset]) * 16 +
                        xpm_hexDigit((unsigned char)value[2u + offset]);
        components[1] = xpm_hexDigit((unsigned char)value[3u + offset]) * 16 +
                        xpm_hexDigit((unsigned char)value[4u + offset]);
        components[2] = xpm_hexDigit((unsigned char)value[5u + offset]) * 16 +
                        xpm_hexDigit((unsigned char)value[6u + offset]);
    } else if (digits == 9u) {
        for (i = 0; i < 3u; ++i) {
            int value12 = xpm_hexDigit((unsigned char)value[1u + i * 3u]) * 256 +
                          xpm_hexDigit((unsigned char)value[2u + i * 3u]) * 16 +
                          xpm_hexDigit((unsigned char)value[3u + i * 3u]);
            {
                unsigned int value16 = (value12 << 4) | (value12 >> 8);
                components[i] = (int)((value16 + 128u -
                                       ((value16 + 128u) >> 8)) >> 8);
            }
        }
    } else {
        for (i = 0; i < 3u; ++i) {
            unsigned int value16 = 0;
            size_t offset = 1u + i * 4u;
            size_t digit;
            for (digit = 0; digit < 4u; ++digit)
                value16 = value16 * 16u +
                          (unsigned int)xpm_hexDigit((unsigned char)value[offset + digit]);
            components[i] = (int)((value16 + 128u -
                                   ((value16 + 128u) >> 8)) >> 8);
        }
    }
    *color = ((uint32_t)(components[3] & 0xff) << 24) |
             ((uint32_t)(components[0] & 0xff) << 16) |
             ((uint32_t)(components[1] & 0xff) << 8) |
             (uint32_t)(components[2] & 0xff);
    return true;
}

typedef struct XImageCodecXpmNamedColor
{
    const char* m_name;
    uint32_t m_rgb;
} XImageCodecXpmNamedColor;

/* Qt 内置 XPM 表有 657 个 X11 颜色名；未列出的名称按 Qt 的
 * value_or(0) 规则回退为不透明黑色。 */
static const XImageCodecXpmNamedColor g_xpmNamedColors[] = {
    /* Qt 6.8 qxpmhandler.cpp 的完整 657 项 X11 名称表。 */
    {"aliceblue", 0xfff0f8ffu},
    {"antiquewhite", 0xfffaebd7u},
    {"antiquewhite1", 0xffffefdbu},
    {"antiquewhite2", 0xffeedfccu},
    {"antiquewhite3", 0xffcdc0b0u},
    {"antiquewhite4", 0xff8b8378u},
    {"aquamarine", 0xff7fffd4u},
    {"aquamarine1", 0xff7fffd4u},
    {"aquamarine2", 0xff76eec6u},
    {"aquamarine3", 0xff66cdaau},
    {"aquamarine4", 0xff458b74u},
    {"azure", 0xfff0ffffu},
    {"azure1", 0xfff0ffffu},
    {"azure2", 0xffe0eeeeu},
    {"azure3", 0xffc1cdcdu},
    {"azure4", 0xff838b8bu},
    {"beige", 0xfff5f5dcu},
    {"bisque", 0xffffe4c4u},
    {"bisque1", 0xffffe4c4u},
    {"bisque2", 0xffeed5b7u},
    {"bisque3", 0xffcdb79eu},
    {"bisque4", 0xff8b7d6bu},
    {"black", 0xff000000u},
    {"blanchedalmond", 0xffffebcdu},
    {"blue", 0xff0000ffu},
    {"blue1", 0xff0000ffu},
    {"blue2", 0xff0000eeu},
    {"blue3", 0xff0000cdu},
    {"blue4", 0xff00008bu},
    {"blueviolet", 0xff8a2be2u},
    {"brown", 0xffa52a2au},
    {"brown1", 0xffff4040u},
    {"brown2", 0xffee3b3bu},
    {"brown3", 0xffcd3333u},
    {"brown4", 0xff8b2323u},
    {"burlywood", 0xffdeb887u},
    {"burlywood1", 0xffffd39bu},
    {"burlywood2", 0xffeec591u},
    {"burlywood3", 0xffcdaa7du},
    {"burlywood4", 0xff8b7355u},
    {"cadetblue", 0xff5f9ea0u},
    {"cadetblue1", 0xff98f5ffu},
    {"cadetblue2", 0xff8ee5eeu},
    {"cadetblue3", 0xff7ac5cdu},
    {"cadetblue4", 0xff53868bu},
    {"chartreuse", 0xff7fff00u},
    {"chartreuse1", 0xff7fff00u},
    {"chartreuse2", 0xff76ee00u},
    {"chartreuse3", 0xff66cd00u},
    {"chartreuse4", 0xff458b00u},
    {"chocolate", 0xffd2691eu},
    {"chocolate1", 0xffff7f24u},
    {"chocolate2", 0xffee7621u},
    {"chocolate3", 0xffcd661du},
    {"chocolate4", 0xff8b4513u},
    {"coral", 0xffff7f50u},
    {"coral1", 0xffff7256u},
    {"coral2", 0xffee6a50u},
    {"coral3", 0xffcd5b45u},
    {"coral4", 0xff8b3e2fu},
    {"cornflowerblue", 0xff6495edu},
    {"cornsilk", 0xfffff8dcu},
    {"cornsilk1", 0xfffff8dcu},
    {"cornsilk2", 0xffeee8cdu},
    {"cornsilk3", 0xffcdc8b1u},
    {"cornsilk4", 0xff8b8878u},
    {"cyan", 0xff00ffffu},
    {"cyan1", 0xff00ffffu},
    {"cyan2", 0xff00eeeeu},
    {"cyan3", 0xff00cdcdu},
    {"cyan4", 0xff008b8bu},
    {"darkblue", 0xff00008bu},
    {"darkcyan", 0xff008b8bu},
    {"darkgoldenrod", 0xffb8860bu},
    {"darkgoldenrod1", 0xffffb90fu},
    {"darkgoldenrod2", 0xffeead0eu},
    {"darkgoldenrod3", 0xffcd950cu},
    {"darkgoldenrod4", 0xff8b6508u},
    {"darkgray", 0xffa9a9a9u},
    {"darkgreen", 0xff006400u},
    {"darkgrey", 0xffa9a9a9u},
    {"darkkhaki", 0xffbdb76bu},
    {"darkmagenta", 0xff8b008bu},
    {"darkolivegreen", 0xff556b2fu},
    {"darkolivegreen1", 0xffcaff70u},
    {"darkolivegreen2", 0xffbcee68u},
    {"darkolivegreen3", 0xffa2cd5au},
    {"darkolivegreen4", 0xff6e8b3du},
    {"darkorange", 0xffff8c00u},
    {"darkorange1", 0xffff7f00u},
    {"darkorange2", 0xffee7600u},
    {"darkorange3", 0xffcd6600u},
    {"darkorange4", 0xff8b4500u},
    {"darkorchid", 0xff9932ccu},
    {"darkorchid1", 0xffbf3effu},
    {"darkorchid2", 0xffb23aeeu},
    {"darkorchid3", 0xff9a32cdu},
    {"darkorchid4", 0xff68228bu},
    {"darkred", 0xff8b0000u},
    {"darksalmon", 0xffe9967au},
    {"darkseagreen", 0xff8fbc8fu},
    {"darkseagreen1", 0xffc1ffc1u},
    {"darkseagreen2", 0xffb4eeb4u},
    {"darkseagreen3", 0xff9bcd9bu},
    {"darkseagreen4", 0xff698b69u},
    {"darkslateblue", 0xff483d8bu},
    {"darkslategray", 0xff2f4f4fu},
    {"darkslategray1", 0xff97ffffu},
    {"darkslategray2", 0xff8deeeeu},
    {"darkslategray3", 0xff79cdcdu},
    {"darkslategray4", 0xff528b8bu},
    {"darkslategrey", 0xff2f4f4fu},
    {"darkturquoise", 0xff00ced1u},
    {"darkviolet", 0xff9400d3u},
    {"deeppink", 0xffff1493u},
    {"deeppink1", 0xffff1493u},
    {"deeppink2", 0xffee1289u},
    {"deeppink3", 0xffcd1076u},
    {"deeppink4", 0xff8b0a50u},
    {"deepskyblue", 0xff00bfffu},
    {"deepskyblue1", 0xff00bfffu},
    {"deepskyblue2", 0xff00b2eeu},
    {"deepskyblue3", 0xff009acdu},
    {"deepskyblue4", 0xff00688bu},
    {"dimgray", 0xff696969u},
    {"dimgrey", 0xff696969u},
    {"dodgerblue", 0xff1e90ffu},
    {"dodgerblue1", 0xff1e90ffu},
    {"dodgerblue2", 0xff1c86eeu},
    {"dodgerblue3", 0xff1874cdu},
    {"dodgerblue4", 0xff104e8bu},
    {"firebrick", 0xffb22222u},
    {"firebrick1", 0xffff3030u},
    {"firebrick2", 0xffee2c2cu},
    {"firebrick3", 0xffcd2626u},
    {"firebrick4", 0xff8b1a1au},
    {"floralwhite", 0xfffffaf0u},
    {"forestgreen", 0xff228b22u},
    {"gainsboro", 0xffdcdcdcu},
    {"ghostwhite", 0xfff8f8ffu},
    {"gold", 0xffffd700u},
    {"gold1", 0xffffd700u},
    {"gold2", 0xffeec900u},
    {"gold3", 0xffcdad00u},
    {"gold4", 0xff8b7500u},
    {"goldenrod", 0xffdaa520u},
    {"goldenrod1", 0xffffc125u},
    {"goldenrod2", 0xffeeb422u},
    {"goldenrod3", 0xffcd9b1du},
    {"goldenrod4", 0xff8b6914u},
    {"gray", 0xffbebebeu},
    {"gray0", 0xff000000u},
    {"gray1", 0xff030303u},
    {"gray10", 0xff1a1a1au},
    {"gray100", 0xffffffffu},
    {"gray11", 0xff1c1c1cu},
    {"gray12", 0xff1f1f1fu},
    {"gray13", 0xff212121u},
    {"gray14", 0xff242424u},
    {"gray15", 0xff262626u},
    {"gray16", 0xff292929u},
    {"gray17", 0xff2b2b2bu},
    {"gray18", 0xff2e2e2eu},
    {"gray19", 0xff303030u},
    {"gray2", 0xff050505u},
    {"gray20", 0xff333333u},
    {"gray21", 0xff363636u},
    {"gray22", 0xff383838u},
    {"gray23", 0xff3b3b3bu},
    {"gray24", 0xff3d3d3du},
    {"gray25", 0xff404040u},
    {"gray26", 0xff424242u},
    {"gray27", 0xff454545u},
    {"gray28", 0xff474747u},
    {"gray29", 0xff4a4a4au},
    {"gray3", 0xff080808u},
    {"gray30", 0xff4d4d4du},
    {"gray31", 0xff4f4f4fu},
    {"gray32", 0xff525252u},
    {"gray33", 0xff545454u},
    {"gray34", 0xff575757u},
    {"gray35", 0xff595959u},
    {"gray36", 0xff5c5c5cu},
    {"gray37", 0xff5e5e5eu},
    {"gray38", 0xff616161u},
    {"gray39", 0xff636363u},
    {"gray4", 0xff0a0a0au},
    {"gray40", 0xff666666u},
    {"gray41", 0xff696969u},
    {"gray42", 0xff6b6b6bu},
    {"gray43", 0xff6e6e6eu},
    {"gray44", 0xff707070u},
    {"gray45", 0xff737373u},
    {"gray46", 0xff757575u},
    {"gray47", 0xff787878u},
    {"gray48", 0xff7a7a7au},
    {"gray49", 0xff7d7d7du},
    {"gray5", 0xff0d0d0du},
    {"gray50", 0xff7f7f7fu},
    {"gray51", 0xff828282u},
    {"gray52", 0xff858585u},
    {"gray53", 0xff878787u},
    {"gray54", 0xff8a8a8au},
    {"gray55", 0xff8c8c8cu},
    {"gray56", 0xff8f8f8fu},
    {"gray57", 0xff919191u},
    {"gray58", 0xff949494u},
    {"gray59", 0xff969696u},
    {"gray6", 0xff0f0f0fu},
    {"gray60", 0xff999999u},
    {"gray61", 0xff9c9c9cu},
    {"gray62", 0xff9e9e9eu},
    {"gray63", 0xffa1a1a1u},
    {"gray64", 0xffa3a3a3u},
    {"gray65", 0xffa6a6a6u},
    {"gray66", 0xffa8a8a8u},
    {"gray67", 0xffabababu},
    {"gray68", 0xffadadadu},
    {"gray69", 0xffb0b0b0u},
    {"gray7", 0xff121212u},
    {"gray70", 0xffb3b3b3u},
    {"gray71", 0xffb5b5b5u},
    {"gray72", 0xffb8b8b8u},
    {"gray73", 0xffbababau},
    {"gray74", 0xffbdbdbdu},
    {"gray75", 0xffbfbfbfu},
    {"gray76", 0xffc2c2c2u},
    {"gray77", 0xffc4c4c4u},
    {"gray78", 0xffc7c7c7u},
    {"gray79", 0xffc9c9c9u},
    {"gray8", 0xff141414u},
    {"gray80", 0xffccccccu},
    {"gray81", 0xffcfcfcfu},
    {"gray82", 0xffd1d1d1u},
    {"gray83", 0xffd4d4d4u},
    {"gray84", 0xffd6d6d6u},
    {"gray85", 0xffd9d9d9u},
    {"gray86", 0xffdbdbdbu},
    {"gray87", 0xffdededeu},
    {"gray88", 0xffe0e0e0u},
    {"gray89", 0xffe3e3e3u},
    {"gray9", 0xff171717u},
    {"gray90", 0xffe5e5e5u},
    {"gray91", 0xffe8e8e8u},
    {"gray92", 0xffebebebu},
    {"gray93", 0xffedededu},
    {"gray94", 0xfff0f0f0u},
    {"gray95", 0xfff2f2f2u},
    {"gray96", 0xfff5f5f5u},
    {"gray97", 0xfff7f7f7u},
    {"gray98", 0xfffafafau},
    {"gray99", 0xfffcfcfcu},
    {"green", 0xff00ff00u},
    {"green1", 0xff00ff00u},
    {"green2", 0xff00ee00u},
    {"green3", 0xff00cd00u},
    {"green4", 0xff008b00u},
    {"greenyellow", 0xffadff2fu},
    {"grey", 0xffbebebeu},
    {"grey0", 0xff000000u},
    {"grey1", 0xff030303u},
    {"grey10", 0xff1a1a1au},
    {"grey100", 0xffffffffu},
    {"grey11", 0xff1c1c1cu},
    {"grey12", 0xff1f1f1fu},
    {"grey13", 0xff212121u},
    {"grey14", 0xff242424u},
    {"grey15", 0xff262626u},
    {"grey16", 0xff292929u},
    {"grey17", 0xff2b2b2bu},
    {"grey18", 0xff2e2e2eu},
    {"grey19", 0xff303030u},
    {"grey2", 0xff050505u},
    {"grey20", 0xff333333u},
    {"grey21", 0xff363636u},
    {"grey22", 0xff383838u},
    {"grey23", 0xff3b3b3bu},
    {"grey24", 0xff3d3d3du},
    {"grey25", 0xff404040u},
    {"grey26", 0xff424242u},
    {"grey27", 0xff454545u},
    {"grey28", 0xff474747u},
    {"grey29", 0xff4a4a4au},
    {"grey3", 0xff080808u},
    {"grey30", 0xff4d4d4du},
    {"grey31", 0xff4f4f4fu},
    {"grey32", 0xff525252u},
    {"grey33", 0xff545454u},
    {"grey34", 0xff575757u},
    {"grey35", 0xff595959u},
    {"grey36", 0xff5c5c5cu},
    {"grey37", 0xff5e5e5eu},
    {"grey38", 0xff616161u},
    {"grey39", 0xff636363u},
    {"grey4", 0xff0a0a0au},
    {"grey40", 0xff666666u},
    {"grey41", 0xff696969u},
    {"grey42", 0xff6b6b6bu},
    {"grey43", 0xff6e6e6eu},
    {"grey44", 0xff707070u},
    {"grey45", 0xff737373u},
    {"grey46", 0xff757575u},
    {"grey47", 0xff787878u},
    {"grey48", 0xff7a7a7au},
    {"grey49", 0xff7d7d7du},
    {"grey5", 0xff0d0d0du},
    {"grey50", 0xff7f7f7fu},
    {"grey51", 0xff828282u},
    {"grey52", 0xff858585u},
    {"grey53", 0xff878787u},
    {"grey54", 0xff8a8a8au},
    {"grey55", 0xff8c8c8cu},
    {"grey56", 0xff8f8f8fu},
    {"grey57", 0xff919191u},
    {"grey58", 0xff949494u},
    {"grey59", 0xff969696u},
    {"grey6", 0xff0f0f0fu},
    {"grey60", 0xff999999u},
    {"grey61", 0xff9c9c9cu},
    {"grey62", 0xff9e9e9eu},
    {"grey63", 0xffa1a1a1u},
    {"grey64", 0xffa3a3a3u},
    {"grey65", 0xffa6a6a6u},
    {"grey66", 0xffa8a8a8u},
    {"grey67", 0xffabababu},
    {"grey68", 0xffadadadu},
    {"grey69", 0xffb0b0b0u},
    {"grey7", 0xff121212u},
    {"grey70", 0xffb3b3b3u},
    {"grey71", 0xffb5b5b5u},
    {"grey72", 0xffb8b8b8u},
    {"grey73", 0xffbababau},
    {"grey74", 0xffbdbdbdu},
    {"grey75", 0xffbfbfbfu},
    {"grey76", 0xffc2c2c2u},
    {"grey77", 0xffc4c4c4u},
    {"grey78", 0xffc7c7c7u},
    {"grey79", 0xffc9c9c9u},
    {"grey8", 0xff141414u},
    {"grey80", 0xffccccccu},
    {"grey81", 0xffcfcfcfu},
    {"grey82", 0xffd1d1d1u},
    {"grey83", 0xffd4d4d4u},
    {"grey84", 0xffd6d6d6u},
    {"grey85", 0xffd9d9d9u},
    {"grey86", 0xffdbdbdbu},
    {"grey87", 0xffdededeu},
    {"grey88", 0xffe0e0e0u},
    {"grey89", 0xffe3e3e3u},
    {"grey9", 0xff171717u},
    {"grey90", 0xffe5e5e5u},
    {"grey91", 0xffe8e8e8u},
    {"grey92", 0xffebebebu},
    {"grey93", 0xffedededu},
    {"grey94", 0xfff0f0f0u},
    {"grey95", 0xfff2f2f2u},
    {"grey96", 0xfff5f5f5u},
    {"grey97", 0xfff7f7f7u},
    {"grey98", 0xfffafafau},
    {"grey99", 0xfffcfcfcu},
    {"honeydew", 0xfff0fff0u},
    {"honeydew1", 0xfff0fff0u},
    {"honeydew2", 0xffe0eee0u},
    {"honeydew3", 0xffc1cdc1u},
    {"honeydew4", 0xff838b83u},
    {"hotpink", 0xffff69b4u},
    {"hotpink1", 0xffff6eb4u},
    {"hotpink2", 0xffee6aa7u},
    {"hotpink3", 0xffcd6090u},
    {"hotpink4", 0xff8b3a62u},
    {"indianred", 0xffcd5c5cu},
    {"indianred1", 0xffff6a6au},
    {"indianred2", 0xffee6363u},
    {"indianred3", 0xffcd5555u},
    {"indianred4", 0xff8b3a3au},
    {"ivory", 0xfffffff0u},
    {"ivory1", 0xfffffff0u},
    {"ivory2", 0xffeeeee0u},
    {"ivory3", 0xffcdcdc1u},
    {"ivory4", 0xff8b8b83u},
    {"khaki", 0xfff0e68cu},
    {"khaki1", 0xfffff68fu},
    {"khaki2", 0xffeee685u},
    {"khaki3", 0xffcdc673u},
    {"khaki4", 0xff8b864eu},
    {"lavender", 0xffe6e6fau},
    {"lavenderblush", 0xfffff0f5u},
    {"lavenderblush1", 0xfffff0f5u},
    {"lavenderblush2", 0xffeee0e5u},
    {"lavenderblush3", 0xffcdc1c5u},
    {"lavenderblush4", 0xff8b8386u},
    {"lawngreen", 0xff7cfc00u},
    {"lemonchiffon", 0xfffffacdu},
    {"lemonchiffon1", 0xfffffacdu},
    {"lemonchiffon2", 0xffeee9bfu},
    {"lemonchiffon3", 0xffcdc9a5u},
    {"lemonchiffon4", 0xff8b8970u},
    {"lightblue", 0xffadd8e6u},
    {"lightblue1", 0xffbfefffu},
    {"lightblue2", 0xffb2dfeeu},
    {"lightblue3", 0xff9ac0cdu},
    {"lightblue4", 0xff68838bu},
    {"lightcoral", 0xfff08080u},
    {"lightcyan", 0xffe0ffffu},
    {"lightcyan1", 0xffe0ffffu},
    {"lightcyan2", 0xffd1eeeeu},
    {"lightcyan3", 0xffb4cdcdu},
    {"lightcyan4", 0xff7a8b8bu},
    {"lightgoldenrod", 0xffeedd82u},
    {"lightgoldenrod1", 0xffffec8bu},
    {"lightgoldenrod2", 0xffeedc82u},
    {"lightgoldenrod3", 0xffcdbe70u},
    {"lightgoldenrod4", 0xff8b814cu},
    {"lightgoldenrodyellow", 0xfffafad2u},
    {"lightgray", 0xffd3d3d3u},
    {"lightgreen", 0xff90ee90u},
    {"lightgrey", 0xffd3d3d3u},
    {"lightpink", 0xffffb6c1u},
    {"lightpink1", 0xffffaeb9u},
    {"lightpink2", 0xffeea2adu},
    {"lightpink3", 0xffcd8c95u},
    {"lightpink4", 0xff8b5f65u},
    {"lightsalmon", 0xffffa07au},
    {"lightsalmon1", 0xffffa07au},
    {"lightsalmon2", 0xffee9572u},
    {"lightsalmon3", 0xffcd8162u},
    {"lightsalmon4", 0xff8b5742u},
    {"lightseagreen", 0xff20b2aau},
    {"lightskyblue", 0xff87cefau},
    {"lightskyblue1", 0xffb0e2ffu},
    {"lightskyblue2", 0xffa4d3eeu},
    {"lightskyblue3", 0xff8db6cdu},
    {"lightskyblue4", 0xff607b8bu},
    {"lightslateblue", 0xff8470ffu},
    {"lightslategray", 0xff778899u},
    {"lightslategrey", 0xff778899u},
    {"lightsteelblue", 0xffb0c4deu},
    {"lightsteelblue1", 0xffcae1ffu},
    {"lightsteelblue2", 0xffbcd2eeu},
    {"lightsteelblue3", 0xffa2b5cdu},
    {"lightsteelblue4", 0xff6e7b8bu},
    {"lightyellow", 0xffffffe0u},
    {"lightyellow1", 0xffffffe0u},
    {"lightyellow2", 0xffeeeed1u},
    {"lightyellow3", 0xffcdcdb4u},
    {"lightyellow4", 0xff8b8b7au},
    {"limegreen", 0xff32cd32u},
    {"linen", 0xfffaf0e6u},
    {"magenta", 0xffff00ffu},
    {"magenta1", 0xffff00ffu},
    {"magenta2", 0xffee00eeu},
    {"magenta3", 0xffcd00cdu},
    {"magenta4", 0xff8b008bu},
    {"maroon", 0xffb03060u},
    {"maroon1", 0xffff34b3u},
    {"maroon2", 0xffee30a7u},
    {"maroon3", 0xffcd2990u},
    {"maroon4", 0xff8b1c62u},
    {"mediumaquamarine", 0xff66cdaau},
    {"mediumblue", 0xff0000cdu},
    {"mediumorchid", 0xffba55d3u},
    {"mediumorchid1", 0xffe066ffu},
    {"mediumorchid2", 0xffd15feeu},
    {"mediumorchid3", 0xffb452cdu},
    {"mediumorchid4", 0xff7a378bu},
    {"mediumpurple", 0xff9370dbu},
    {"mediumpurple1", 0xffab82ffu},
    {"mediumpurple2", 0xff9f79eeu},
    {"mediumpurple3", 0xff8968cdu},
    {"mediumpurple4", 0xff5d478bu},
    {"mediumseagreen", 0xff3cb371u},
    {"mediumslateblue", 0xff7b68eeu},
    {"mediumspringgreen", 0xff00fa9au},
    {"mediumturquoise", 0xff48d1ccu},
    {"mediumvioletred", 0xffc71585u},
    {"midnightblue", 0xff191970u},
    {"mintcream", 0xfff5fffau},
    {"mistyrose", 0xffffe4e1u},
    {"mistyrose1", 0xffffe4e1u},
    {"mistyrose2", 0xffeed5d2u},
    {"mistyrose3", 0xffcdb7b5u},
    {"mistyrose4", 0xff8b7d7bu},
    {"moccasin", 0xffffe4b5u},
    {"navajowhite", 0xffffdeadu},
    {"navajowhite1", 0xffffdeadu},
    {"navajowhite2", 0xffeecfa1u},
    {"navajowhite3", 0xffcdb38bu},
    {"navajowhite4", 0xff8b795eu},
    {"navy", 0xff000080u},
    {"navyblue", 0xff000080u},
    {"oldlace", 0xfffdf5e6u},
    {"olivedrab", 0xff6b8e23u},
    {"olivedrab1", 0xffc0ff3eu},
    {"olivedrab2", 0xffb3ee3au},
    {"olivedrab3", 0xff9acd32u},
    {"olivedrab4", 0xff698b22u},
    {"orange", 0xffffa500u},
    {"orange1", 0xffffa500u},
    {"orange2", 0xffee9a00u},
    {"orange3", 0xffcd8500u},
    {"orange4", 0xff8b5a00u},
    {"orangered", 0xffff4500u},
    {"orangered1", 0xffff4500u},
    {"orangered2", 0xffee4000u},
    {"orangered3", 0xffcd3700u},
    {"orangered4", 0xff8b2500u},
    {"orchid", 0xffda70d6u},
    {"orchid1", 0xffff83fau},
    {"orchid2", 0xffee7ae9u},
    {"orchid3", 0xffcd69c9u},
    {"orchid4", 0xff8b4789u},
    {"palegoldenrod", 0xffeee8aau},
    {"palegreen", 0xff98fb98u},
    {"palegreen1", 0xff9aff9au},
    {"palegreen2", 0xff90ee90u},
    {"palegreen3", 0xff7ccd7cu},
    {"palegreen4", 0xff548b54u},
    {"paleturquoise", 0xffafeeeeu},
    {"paleturquoise1", 0xffbbffffu},
    {"paleturquoise2", 0xffaeeeeeu},
    {"paleturquoise3", 0xff96cdcdu},
    {"paleturquoise4", 0xff668b8bu},
    {"palevioletred", 0xffdb7093u},
    {"palevioletred1", 0xffff82abu},
    {"palevioletred2", 0xffee799fu},
    {"palevioletred3", 0xffcd6889u},
    {"palevioletred4", 0xff8b475du},
    {"papayawhip", 0xffffefd5u},
    {"peachpuff", 0xffffdab9u},
    {"peachpuff1", 0xffffdab9u},
    {"peachpuff2", 0xffeecbadu},
    {"peachpuff3", 0xffcdaf95u},
    {"peachpuff4", 0xff8b7765u},
    {"peru", 0xffcd853fu},
    {"pink", 0xffffc0cbu},
    {"pink1", 0xffffb5c5u},
    {"pink2", 0xffeea9b8u},
    {"pink3", 0xffcd919eu},
    {"pink4", 0xff8b636cu},
    {"plum", 0xffdda0ddu},
    {"plum1", 0xffffbbffu},
    {"plum2", 0xffeeaeeeu},
    {"plum3", 0xffcd96cdu},
    {"plum4", 0xff8b668bu},
    {"powderblue", 0xffb0e0e6u},
    {"purple", 0xffa020f0u},
    {"purple1", 0xff9b30ffu},
    {"purple2", 0xff912ceeu},
    {"purple3", 0xff7d26cdu},
    {"purple4", 0xff551a8bu},
    {"red", 0xffff0000u},
    {"red1", 0xffff0000u},
    {"red2", 0xffee0000u},
    {"red3", 0xffcd0000u},
    {"red4", 0xff8b0000u},
    {"rosybrown", 0xffbc8f8fu},
    {"rosybrown1", 0xffffc1c1u},
    {"rosybrown2", 0xffeeb4b4u},
    {"rosybrown3", 0xffcd9b9bu},
    {"rosybrown4", 0xff8b6969u},
    {"royalblue", 0xff4169e1u},
    {"royalblue1", 0xff4876ffu},
    {"royalblue2", 0xff436eeeu},
    {"royalblue3", 0xff3a5fcdu},
    {"royalblue4", 0xff27408bu},
    {"saddlebrown", 0xff8b4513u},
    {"salmon", 0xfffa8072u},
    {"salmon1", 0xffff8c69u},
    {"salmon2", 0xffee8262u},
    {"salmon3", 0xffcd7054u},
    {"salmon4", 0xff8b4c39u},
    {"sandybrown", 0xfff4a460u},
    {"seagreen", 0xff2e8b57u},
    {"seagreen1", 0xff54ff9fu},
    {"seagreen2", 0xff4eee94u},
    {"seagreen3", 0xff43cd80u},
    {"seagreen4", 0xff2e8b57u},
    {"seashell", 0xfffff5eeu},
    {"seashell1", 0xfffff5eeu},
    {"seashell2", 0xffeee5deu},
    {"seashell3", 0xffcdc5bfu},
    {"seashell4", 0xff8b8682u},
    {"sienna", 0xffa0522du},
    {"sienna1", 0xffff8247u},
    {"sienna2", 0xffee7942u},
    {"sienna3", 0xffcd6839u},
    {"sienna4", 0xff8b4726u},
    {"skyblue", 0xff87ceebu},
    {"skyblue1", 0xff87ceffu},
    {"skyblue2", 0xff7ec0eeu},
    {"skyblue3", 0xff6ca6cdu},
    {"skyblue4", 0xff4a708bu},
    {"slateblue", 0xff6a5acdu},
    {"slateblue1", 0xff836fffu},
    {"slateblue2", 0xff7a67eeu},
    {"slateblue3", 0xff6959cdu},
    {"slateblue4", 0xff473c8bu},
    {"slategray", 0xff708090u},
    {"slategray1", 0xffc6e2ffu},
    {"slategray2", 0xffb9d3eeu},
    {"slategray3", 0xff9fb6cdu},
    {"slategray4", 0xff6c7b8bu},
    {"slategrey", 0xff708090u},
    {"snow", 0xfffffafau},
    {"snow1", 0xfffffafau},
    {"snow2", 0xffeee9e9u},
    {"snow3", 0xffcdc9c9u},
    {"snow4", 0xff8b8989u},
    {"springgreen", 0xff00ff7fu},
    {"springgreen1", 0xff00ff7fu},
    {"springgreen2", 0xff00ee76u},
    {"springgreen3", 0xff00cd66u},
    {"springgreen4", 0xff008b45u},
    {"steelblue", 0xff4682b4u},
    {"steelblue1", 0xff63b8ffu},
    {"steelblue2", 0xff5caceeu},
    {"steelblue3", 0xff4f94cdu},
    {"steelblue4", 0xff36648bu},
    {"tan", 0xffd2b48cu},
    {"tan1", 0xffffa54fu},
    {"tan2", 0xffee9a49u},
    {"tan3", 0xffcd853fu},
    {"tan4", 0xff8b5a2bu},
    {"thistle", 0xffd8bfd8u},
    {"thistle1", 0xffffe1ffu},
    {"thistle2", 0xffeed2eeu},
    {"thistle3", 0xffcdb5cdu},
    {"thistle4", 0xff8b7b8bu},
    {"tomato", 0xffff6347u},
    {"tomato1", 0xffff6347u},
    {"tomato2", 0xffee5c42u},
    {"tomato3", 0xffcd4f39u},
    {"tomato4", 0xff8b3626u},
    {"turquoise", 0xff40e0d0u},
    {"turquoise1", 0xff00f5ffu},
    {"turquoise2", 0xff00e5eeu},
    {"turquoise3", 0xff00c5cdu},
    {"turquoise4", 0xff00868bu},
    {"violet", 0xffee82eeu},
    {"violetred", 0xffd02090u},
    {"violetred1", 0xffff3e96u},
    {"violetred2", 0xffee3a8cu},
    {"violetred3", 0xffcd3278u},
    {"violetred4", 0xff8b2252u},
    {"wheat", 0xfff5deb3u},
    {"wheat1", 0xffffe7bau},
    {"wheat2", 0xffeed8aeu},
    {"wheat3", 0xffcdba96u},
    {"wheat4", 0xff8b7e66u},
    {"white", 0xffffffffu},
    {"whitesmoke", 0xfff5f5f5u},
    {"yellow", 0xffffff00u},
    {"yellow1", 0xffffff00u},
    {"yellow2", 0xffeeee00u},
    {"yellow3", 0xffcdcd00u},
    {"yellow4", 0xff8b8b00u},
    {"yellowgreen", 0xff9acd32u},
};

static uint32_t xpm_namedColor(const char* value, size_t length)
{
    size_t i;
    for (i = 0; i < sizeof(g_xpmNamedColors) / sizeof(g_xpmNamedColors[0]); ++i)
        if (xpm_equalWord(value, length, g_xpmNamedColors[i].m_name))
            return g_xpmNamedColors[i].m_rgb;
    return 0xff000000u;
}

static bool xpm_parseColor(const char* value, size_t length,
                           uint32_t* color, bool* transparent)
{
    size_t cursor = 0;
    size_t prefixBegin = SIZE_MAX;
    size_t prefixLength = 0;
    size_t colorBegin = 0;
    size_t colorLength = 0;
    size_t tokenBegin;
    size_t tokenEnd;
    int pass;
    char compact[256];
    size_t compactLength;
    if (!value || !color || !transparent) return false;
    *transparent = false;
    /* Qt tokens are simplified and lower-cased before looking up c/g/g4/m. */
    for (pass = 0; pass < 4 && prefixBegin == SIZE_MAX; ++pass) {
        cursor = 0;
        while (cursor < length) {
            while (cursor < length && xpm_space((unsigned char)value[cursor])) ++cursor;
            tokenBegin = cursor;
            while (cursor < length && !xpm_space((unsigned char)value[cursor])) ++cursor;
            tokenEnd = cursor;
            if (tokenEnd > tokenBegin &&
                ((pass == 0 && xpm_equalWord(value + tokenBegin, tokenEnd - tokenBegin, "c")) ||
                 (pass == 1 && xpm_equalWord(value + tokenBegin, tokenEnd - tokenBegin, "g")) ||
                 (pass == 2 && xpm_equalWord(value + tokenBegin, tokenEnd - tokenBegin, "g4")) ||
                 (pass == 3 && xpm_equalWord(value + tokenBegin, tokenEnd - tokenBegin, "m")))) {
                prefixBegin = tokenBegin;
                prefixLength = tokenEnd - tokenBegin;
                break;
            }
        }
    }
    (void)prefixLength;
    if (prefixBegin == SIZE_MAX) return false;
    cursor = prefixBegin + prefixLength;
    while (cursor < length && xpm_space((unsigned char)value[cursor])) ++cursor;
    colorBegin = cursor;
    compactLength = 0;
    while (cursor < length) {
        while (cursor < length && xpm_space((unsigned char)value[cursor])) ++cursor;
        tokenBegin = cursor;
        while (cursor < length && !xpm_space((unsigned char)value[cursor])) ++cursor;
        tokenEnd = cursor;
        if (tokenEnd == tokenBegin) break;
        if (xpm_isPrefix(value + tokenBegin, tokenEnd - tokenBegin)) break;
        if (compactLength + tokenEnd - tokenBegin >= sizeof(compact)) break;
        memcpy(compact + compactLength, value + tokenBegin, tokenEnd - tokenBegin);
        compactLength += tokenEnd - tokenBegin;
    }
    colorLength = compactLength;
    (void)colorBegin;
    if (!colorLength) return false;
    if (xpm_equalWord(compact, colorLength, "none")) {
        *color = 0;
        *transparent = true;
        return true;
    }
    if (compact[0] == '#' &&
        colorLength > 1u && (colorLength - 1u) % 3u != 0u) {
        /* Qt qxpmhandler.cpp:901-904 removes a trailing ImageMagick alpha. */
        colorLength = ((colorLength - 1u) / 4u) * 3u + 1u;
        if (colorLength <= 1u) return false;
    }
    if (!xpm_hexColor(compact, colorLength, color))
        *color = xpm_namedColor(compact, colorLength);
    *color |= 0xff000000u;
    return true;
}

static int xpm_findColor(const XImageCodecXpmColor* colors, int count,
                         const char* key, size_t length)
{
    uint32_t hash = xpm_hash(key, length);
    int i;
    if (!colors || !key || length > 15u) return -1;
    /* Qt 使用 QMap<quint64, int> 仅以 32 位 xpmHash 作为键；重复键以及
       不同字符串的哈希碰撞均由后出现的颜色覆盖，不能再比较原始键。 */
    for (i = count - 1; i >= 0; --i)
        if (colors[i].m_hash == hash)
            return i;
    return -1;
}

bool XImageCodecInternal_probeXpmSize(const uint8_t* data, size_t size,
                                      int* width, int* height)
{
    XImageCodecXpmHeader header;
    size_t pos;
    if (!width || !height || !xpm_parseHeader(data, size, &header, &pos))
        return false;
    *width = header.m_width;
    *height = header.m_height;
    return true;
}

bool XImageCodecInternal_probeXpmImageFormat(const uint8_t* data, size_t size,
                                             int* width, int* height,
                                             XImageFormat* imageFormat)
{
    XImageCodecXpmHeader header;
    size_t pos;
    if (!width || !height || !imageFormat ||
        !xpm_parseHeader(data, size, &header, &pos))
        return false;
    *width = header.m_width;
    *height = header.m_height;
    /* QXpmHandler::option(ImageFormat) reports Invalid for a table larger
       than 256 entries because that query does not consume all color lines. */
    *imageFormat = header.m_colorCount <= 256
        ? XImageFormat_Indexed8 : XImageFormat_Invalid;
    return true;
}

bool XImageCodecInternal_decodeXpm(const uint8_t* data, size_t size, XImage* out)
{
    XImageCodecXpmHeader header;
    XImageCodecXpmColor* colors;
    XImage image;
    size_t pos;
    size_t colorBytes;
    int i;
    int y;
    bool transparent = false;
    if (!out || !xpm_parseHeader(data, size, &header, &pos)) return false;
    if ((size_t)header.m_colorCount > size ||
        (size_t)header.m_colorCount > SIZE_MAX / sizeof(*colors))
        return false;
    colorBytes = (size_t)header.m_colorCount * sizeof(*colors);
    colors = (XImageCodecXpmColor*)XMalloc_System(colorBytes);
    if (!colors) return false;
    memset(colors, 0, colorBytes);
    for (i = 0; i < header.m_colorCount; ++i) {
        char* line;
        size_t length;
        uint32_t color;
        bool lineTransparent;
        line = xpm_nextString(data, size, &pos, &length);
        if (!line || length < (size_t)header.m_charsPerPixel ||
            !xpm_parseColor(line + header.m_charsPerPixel,
                            length - (size_t)header.m_charsPerPixel,
                            &color, &lineTransparent)) {
            if (line) XFree_System(line);
            XFree_System(colors);
            return false;
        }
        memcpy(colors[i].m_key, line, (size_t)header.m_charsPerPixel);
        colors[i].m_key[header.m_charsPerPixel] = '\0';
        colors[i].m_hash = xpm_hash(colors[i].m_key,
                                    (size_t)header.m_charsPerPixel);
        colors[i].m_color = lineTransparent ? 0u : color;
        transparent = transparent || lineTransparent;
        XFree_System(line);
    }
    XImage_init_ex(&image, header.m_width, header.m_height,
                   header.m_colorCount <= 256
                       ? XImageFormat_Indexed8
                       : (transparent ? XImageFormat_ARGB32 : XImageFormat_RGB32));
    if (XImage_isNull(&image)) {
        XFree_System(colors);
        XImage_deinit_base(&image);
        return false;
    }
    XImage_fill(&image, 0u);
    if (header.m_colorCount <= 256) {
        XImage_setColorCount(&image, header.m_colorCount);
        for (i = 0; i < header.m_colorCount; ++i)
            XImage_setColor(&image, i, colors[i].m_color);
    }
    for (y = 0; y < header.m_height; ++y) {
        char* line;
        size_t length;
        int x;
        line = xpm_nextString(data, size, &pos, &length);
        if (!line) {
            XImage_deinit_base(&image);
            XFree_System(colors);
            return false;
        }
        for (x = 0; x < header.m_width; ++x) {
            size_t offset = (size_t)x * (size_t)header.m_charsPerPixel;
            int index = -1;
            if (offset + (size_t)header.m_charsPerPixel <= length)
                index = xpm_findColor(colors, header.m_colorCount,
                                      line + offset,
                                      (size_t)header.m_charsPerPixel);
            if (header.m_colorCount <= 256) {
                uint8_t* scan = XImage_scanLine(&image, y);
                if (scan) scan[x] = index >= 0 ? (uint8_t)index : 0u;
            } else if (index >= 0) {
                XImage_setPixel(&image, x, y, colors[index].m_color);
            }
        }
        XFree_System(line);
    }
    XFree_System(colors);
    XImage_move_base(out, &image);
    return true;
}

static void xpm_makeName(const char* name, char* output, size_t capacity)
{
    const char* begin = name && name[0] ? name : "image";
    const char* slash = strrchr(begin, '/');
    const char* backslash = strrchr(begin, '\\');
    const char* dot;
    size_t length = 0;
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    if (slash) begin = slash + 1;
    dot = strrchr(begin, '.');
    while (*begin && begin != dot && length + 1u < capacity) {
        unsigned char value = (unsigned char)*begin++;
        output[length++] = ((value >= 'a' && value <= 'z') ||
                            (value >= 'A' && value <= 'Z') ||
                            (value >= '0' && value <= '9') || value == '_')
            ? (char)value : '_';
    }
    if (!length && capacity > 1u) output[length++] = 'i';
    if (length && output[0] >= '0' && output[0] <= '9' && length + 1u < capacity) {
        memmove(output + 1, output, length);
        output[0] = '_';
        ++length;
    }
    if (capacity) output[length < capacity ? length : capacity - 1u] = '\0';
}

static void xpm_colorName(int cpp, uint64_t index, char* output, size_t capacity)
{
    static const char code[] = ".#abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    if (!output || capacity < (size_t)cpp + 1u || cpp < 1 || cpp > 4) return;

    /* Qt 的 xpm_color_name() 只交换倒数第二位字符对应的索引；
       这两个字符是历史实现保留的趣味键，不能对整个数值换位。 */
    if (cpp > 1) {
        if (cpp > 2) {
            if (cpp > 3) {
                output[3] = code[index % 64u];
                index /= 64u;
            } else {
                output[3] = '\0';
            }
            output[2] = code[index % 64u];
            index /= 64u;
        } else {
            output[2] = '\0';
        }
        if (index == 0u)
            index = 64u * 44u + 21u;
        else if (index == 64u * 44u + 21u)
            index = 0u;
        output[1] = code[index % 64u];
        index /= 64u;
    } else {
        output[1] = '\0';
    }
    output[0] = code[index % 64u];
    output[cpp] = '\0';
}

bool XImageCodecInternal_encodeXpmNamed(const XImage* image, const char* name,
                                        XByteArray* out)
{
    uint32_t* colors;
    size_t pixelCount;
    size_t colorCount = 0;
    size_t colorCapacity;
    size_t* paletteOrder;
    int width;
    int height;
    int x;
    int y;
    int cpp = 1;
    uint64_t limit = 64u;
    XImageFormat sourceFormat;
    bool preserveAlpha;
    char identifier[128];
    char key[5];
    char line[256];
    if (!image || !out || XImage_isNull(image)) return false;
    sourceFormat = XImage_format(image);
    preserveAlpha = sourceFormat == XImageFormat_ARGB32 ||
                    sourceFormat == XImageFormat_ARGB32_Premultiplied;
    width = XImage_width(image);
    height = XImage_height(image);
    if (width <= 0 || height <= 0 ||
        (size_t)width > SIZE_MAX / (size_t)height)
        return false;
    pixelCount = (size_t)width * (size_t)height;
    colorCapacity = pixelCount;
    if (colorCapacity > SIZE_MAX / sizeof(*colors)) return false;
    colors = (uint32_t*)XMalloc_System(colorCapacity * sizeof(*colors));
    if (!colors) return false;
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t color = XImage_pixel(image, x, y);
            size_t i;
            bool found = false;
            if (!preserveAlpha) color |= 0xff000000u;
            for (i = 0; i < colorCount; ++i)
                if (colors[i] == color) { found = true; break; }
            if (!found) {
                /* Qt numbers colors in first-seen order while walking scan
                   lines; preserve that order in the generated palette. */
                colors[colorCount++] = color;
            }
        }
    }
    if (colorCount > SIZE_MAX / sizeof(*paletteOrder)) {
        XFree_System(colors);
        return false;
    }
    paletteOrder = (size_t*)XMalloc_System(colorCount * sizeof(*paletteOrder));
    if (!paletteOrder) {
        XFree_System(colors);
        return false;
    }
    for (x = 0; x < (int)colorCount; ++x)
        paletteOrder[x] = (size_t)x;
    /* Qt uses std::map<QRgb,int>: output palette rows are sorted by RGB,
       while each key keeps the index assigned at first pixel occurrence. */
    for (x = 1; x < (int)colorCount; ++x) {
        size_t value = paletteOrder[x];
        int cursor = x;
        while (cursor > 0 &&
               colors[paletteOrder[cursor - 1]] > colors[value]) {
            paletteOrder[cursor] = paletteOrder[cursor - 1];
            --cursor;
        }
        paletteOrder[cursor] = value;
    }
    while ((uint64_t)colorCount > limit && cpp < 4) {
        limit *= 64u;
        ++cpp;
    }
    if ((uint64_t)colorCount > limit || colorCount > 16777216u) {
        XFree_System(paletteOrder);
        XFree_System(colors);
        return false;
    }
    xpm_makeName(name, identifier, sizeof(identifier));
    if (snprintf(line, sizeof(line), "/* XPM */\nstatic char *%s[]={\n\"%d %d %u %d\"",
                 identifier, width, height, (unsigned)colorCount, cpp) <= 0 ||
        !XImageCodecInternal_appendBytes(out, line, strlen(line))) {
        XFree_System(paletteOrder);
        XFree_System(colors);
        return false;
    }
    for (x = 0; x < (int)colorCount; ++x) {
        size_t paletteIndex = paletteOrder[x];
        uint32_t color = colors[paletteIndex];
        bool transparent = preserveAlpha && ((color >> 24) & 0xffu) == 0u;
        xpm_colorName(cpp, (uint64_t)paletteIndex, key, sizeof(key));
        if (transparent) {
            if (snprintf(line, sizeof(line), ",\n\"%s c None\"", key) <= 0) {
                XFree_System(paletteOrder);
                XFree_System(colors);
                return false;
            }
        } else if (snprintf(line, sizeof(line), ",\n\"%s c #%02x%02x%02x\"", key,
                            (unsigned)((color >> 16) & 0xffu),
                            (unsigned)((color >> 8) & 0xffu),
                            (unsigned)(color & 0xffu)) <= 0) {
            XFree_System(paletteOrder);
            XFree_System(colors);
            return false;
        }
        if (!XImageCodecInternal_appendBytes(out, line, strlen(line))) {
            XFree_System(paletteOrder);
            XFree_System(colors);
            return false;
        }
    }
    for (y = 0; y < height; ++y) {
        if (!XImageCodecInternal_appendBytes(out, ",\n\"", 3u)) {
            XFree_System(paletteOrder);
            XFree_System(colors);
            return false;
        }
        for (x = 0; x < width; ++x) {
            uint32_t color = XImage_pixel(image, x, y);
            size_t index = 0;
            if (!preserveAlpha) color |= 0xff000000u;
            while (index < colorCount && colors[index] != color) ++index;
            xpm_colorName(cpp, (uint64_t)index, key, sizeof(key));
            if (!XImageCodecInternal_appendBytes(out, key, (size_t)cpp)) {
                XFree_System(paletteOrder);
                XFree_System(colors);
                return false;
            }
        }
        if (!XImageCodecInternal_appendBytes(out, "\"", 1u)) {
            XFree_System(paletteOrder);
            XFree_System(colors);
            return false;
        }
    }
    if (!XImageCodecInternal_appendBytes(out, "};\n", 3u)) {
        XFree_System(paletteOrder);
        XFree_System(colors);
        return false;
    }
    XFree_System(paletteOrder);
    XFree_System(colors);
    return true;
}

#endif /* XIMAGECODEC_ON && XIMAGECODEC_XPM_ON */
