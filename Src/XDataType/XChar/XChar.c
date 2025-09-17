#include "XChar.h"
#include "XMemory.h"
#include <float.h>
#include <math.h> 
#ifdef _WIN32
// Windows平台GBK转换（依赖Windows API）
#include <windows.h>
#elif defined(__linux__) 
#include <iconv.h>
#include <errno.h>
#endif

// 代理对相关常量
#define SURROGATE_OFFSET                            0x10000
#define UTF16_HIGH_SURROGATE_START                  0xD800
#define UTF16_HIGH_SURROGATE_END                    0xDBFF
#define UTF16_LOW_SURROGATE_START                   0xDC00
#define UTF16_LOW_SURROGATE_END                     0xDFFF
#define UNICODE_MAX_CODEPOINT                       0x10FFFF  // 最大有效Unicode码点

XChar XChar_from(uint16_t code)
{
    XChar ch = { code };
    return ch;
}

uint8_t XChar_to_utf8(XChar ch)
{
    return ch& (~0x80);
}

// 创建XChar实例（高代理）
XChar XChar_from_unicode(uint32_t unicode) {
    XChar ch = { 0 };
    if (unicode > UNICODE_MAX_CODEPOINT) return ch;

    // 基础多语言平面（排除代理对范围）
    if (unicode <= 0xFFFF) {
        if ((unicode >= UTF16_HIGH_SURROGATE_START && unicode <= UTF16_HIGH_SURROGATE_END) ||
            (unicode >= UTF16_LOW_SURROGATE_START && unicode <= UTF16_LOW_SURROGATE_END)) {
            return ch; // 代理范围视为无效
        }
        ch = (uint16_t)unicode;
        return ch;
    }

    // 补充平面字符（生成高代理）
    unicode -= SURROGATE_OFFSET;
    ch = (uint16_t)(UTF16_HIGH_SURROGATE_START + (unicode >> 10));
    return ch;
}

// 创建补充平面字符的低代理
XChar XChar_from_unicode_low(uint32_t unicode) {
    XChar ch = { 0 };
    if (unicode < 0x10000 || unicode > UNICODE_MAX_CODEPOINT) return ch;

    unicode -= SURROGATE_OFFSET;
    ch = (uint16_t)(UTF16_LOW_SURROGATE_START + (unicode & 0x3FF));
    return ch;
}

// 获取Unicode码点（单字符/高代理）
uint32_t XChar_unicode(const XChar ch) {
    return ch;
}

// 判断是否为字母（扩展多语言支持）
bool XChar_is_letter(const XChar ch) {
    uint16_t code = ch;

    // 基本拉丁字母
    if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z'))
        return true;

    // 扩展拉丁字母
    if ((code >= 0x00C0 && code <= 0x00D6) ||
        (code >= 0x00D8 && code <= 0x00F6) ||
        (code >= 0x00F8 && code <= 0x02AF))
        return true;

    // 日文平假名/片假名
    if ((code >= 0x3040 && code <= 0x309F) ||  // 平假名
        (code >= 0x30A0 && code <= 0x30FF))    // 片假名
        return true;

    // 韩文Hangul
    if ((code >= 0xAC00 && code <= 0xD7AF) ||  // 韩文字母
        (code >= 0x1100 && code <= 0x11FF) ||  // 韩文字母首音
        (code >= 0x3130 && code <= 0x318F))    // 韩文兼容字母
        return true;

    // 俄文字母
    if ((code >= 0x0400 && code <= 0x04FF) ||  // 西里尔字母
        (code >= 0x0500 && code <= 0x052F))    // 西里尔补充字母
        return true;

    return false;
}

// 判断是否为数字（完善中文数字和范围）
bool XChar_is_digit(const XChar ch) {
    uint16_t code = ch;

    // 基本拉丁数字
    if (code >= '0' && code <= '9') return true;

    // 阿拉伯-印度数字
    if ((code >= 0x0660 && code <= 0x0669) || (code >= 0x06F0 && code <= 0x06F9))
        return true;

    // 印度数字系统
    if ((code >= 0x0966 && code <= 0x096F) || (code >= 0x09E6 && code <= 0x09EF) ||
        (code >= 0x0AE6 && code <= 0x0AEF) || (code >= 0x0BE7 && code <= 0x0BEF) ||
        (code >= 0x0CE6 && code <= 0x0CEF) || (code >= 0x0DE6 && code <= 0x0DEF) ||
        (code >= 0x0EE6 && code <= 0x0EEF) || (code >= 0x0FE6 && code <= 0x0FEF))
        return true;

    // 中文数字（补充大写和亿）
    if (code == 0x3007 ||  // 零
        (code >= 0x4E00 && code <= 0x4E09) ||  // 一-三
        code == 0x56DB || code == 0x4E95 ||    // 四、五
        code == 0x516D || code == 0x4E03 ||    // 六、七
        code == 0x516B || code == 0x4E5D ||    // 八、九
        code == 0x5341 || code == 0x767E ||    // 十、百
        code == 0x5343 || code == 0x4E07 ||    // 千、万
        code == 0x4EBF ||                      // 亿
        code == 0x58F9 || code == 0x8D30 ||    // 壹、贰
        code == 0x53C1 || code == 0x8086 ||    // 叁、肆
        code == 0x4F0D || code == 0x9678 ||    // 伍、陆
        code == 0x4E03 || code == 0x516B ||    // 柒、捌
        code == 0x7396)                        // 玖
        return true;

    // 全角数字
    if (code >= 0xFF10 && code <= 0xFF19) return true;

    // 其他语言数字
    if ((code >= 0x1040 && code <= 0x1049) || (code >= 0x17E0 && code <= 0x17E9) ||
        (code >= 0x1810 && code <= 0x1819) || (code >= 0x2D30 && code <= 0x2D39) ||
        (code >= 0xA620 && code <= 0xA629) || (code >= 0xA830 && code <= 0xA839) ||
        (code >= 0x1D7CE && code <= 0x1D7FF))
        return true;

    // 上标和下标数字
    if ((code >= 0x2070 && code <= 0x2079) || (code >= 0x2080 && code <= 0x2089))
        return true;

    return false;
}

// 数字字符转整数 value
int XChar_digit_value(const XChar ch) {
    if (!ch || !XChar_is_digit(ch)) return -1;
    uint16_t code = ch;

    // 基本拉丁数字
    if (code >= '0' && code <= '9') return code - '0';

    // 阿拉伯-印度数字
    if (code >= 0x0660 && code <= 0x0669) return code - 0x0660;
    if (code >= 0x06F0 && code <= 0x06F9) return code - 0x06F0;

    // 印度数字系统
    if (code >= 0x0966 && code <= 0x096F) return code - 0x0966;
    if (code >= 0x09E6 && code <= 0x09EF) return code - 0x09E6;
    if (code >= 0x0AE6 && code <= 0x0AEF) return code - 0x0AE6;
    if (code >= 0x0BE7 && code <= 0x0BEF) return code - 0x0BE7;
    if (code >= 0x0CE6 && code <= 0x0CEF) return code - 0x0CE6;
    if (code >= 0x0DE6 && code <= 0x0DEF) return code - 0x0DE6;
    if (code >= 0x0EE6 && code <= 0x0EEF) return code - 0x0EE6;
    if (code >= 0x0FE6 && code <= 0x0FEF) return code - 0x0FE6;

    // 全角数字
    if (code >= 0xFF10 && code <= 0xFF19) return code - 0xFF10;

    // 其他语言数字
    if (code >= 0x1040 && code <= 0x1049) return code - 0x1040;
    if (code >= 0x17E0 && code <= 0x17E9) return code - 0x17E0;
    if (code >= 0x1810 && code <= 0x1819) return code - 0x1810;
    if (code >= 0x2D30 && code <= 0x2D39) return code - 0x2D30;
    if (code >= 0xA620 && code <= 0xA629) return code - 0xA620;
    if (code >= 0xA830 && code <= 0xA839) return code - 0xA830;
    if (code >= 0x1D7CE && code <= 0x1D7FF) return code - 0x1D7CE;

    // 上标和下标数字
    if (code >= 0x2070 && code <= 0x2079) {
        return (code == 0x2070) ? 0 : (code - 0x2071 + 1);
    }
    if (code >= 0x2080 && code <= 0x2089) return code - 0x2080;

    // 中文数字（含大写）
    switch (code) {
    case 0x3007: return 0;   // 零
    case 0x4E00: case 0x58F9: return 1; // 一、壹
    case 0x4E8C: case 0x8D30: return 2; // 二、贰
    case 0x4E09: case 0x53C1: return 3; // 三、叁
    case 0x56DB: case 0x8086: return 4; // 四、肆
    case 0x4E95: case 0x4F0D: return 5; // 五、伍
    case 0x516D: case 0x9678: return 6; // 六、陆
    case 0x4E03: case 0x67D2: return 7; // 七、柒
    case 0x516B: case 0x634C: return 8; // 八、捌
    case 0x4E5D: case 0x7396: return 9; // 九、玖
    }

    return -1;
}

// 判断是否为空白字符（扩展范围）
bool XChar_is_space(const XChar  ch) {
    uint16_t code = ch;

    // ASCII空白
    if (code == ' ' || code == '\t' || code == '\n' ||
        code == '\r' || code == '\v' || code == '\f')
        return true;

    // Unicode空白
    if (code == 0x00A0 ||        // 非换行空格
        (code >= 0x2000 && code <= 0x200A) || // 各种宽度空格
        code == 0x202F || code == 0x205F ||  // 窄空格、数学空格
        code == 0x3000)                      // 中文全角空格
        return true;

    return false;
}

// 判断是否为标点符号（精确范围）
bool XChar_is_punct(const XChar ch) {
    uint16_t code = ch;

    // ASCII标点
    if ((code >= 0x21 && code <= 0x2F) ||
        (code >= 0x3A && code <= 0x40) ||
        (code >= 0x5B && code <= 0x60) ||
        (code >= 0x7B && code <= 0x7E))
        return true;

    // 通用标点（精确区间）
    if ((code >= 0x2010 && code <= 0x2027) ||  // 连字符、引号等
        (code >= 0x2030 && code <= 0x2043) ||  // 百分号变体、括号等
        (code >= 0x2045 && code <= 0x205E))    // 其他标点
        return true;

    // 中文标点
    if ((code >= 0x3001 && code <= 0x3002) ||  // ，。
        (code >= 0xFF01 && code <= 0xFF0F) ||  // 全角标点
        (code >= 0xFF1A && code <= 0xFF1F))    // 全角冒号等
        return true;

    return false;
}

// 判断是否为小写字母
bool XChar_is_lower(const XChar ch) {
    uint16_t code = ch;

    // ASCII小写
    if (code >= 'a' && code <= 'z') return true;

    // 扩展拉丁小写
    if ((code >= 0x00DF && code <= 0x00F6) ||
        (code >= 0x00F8 && code <= 0x00FF))
        return true;

    // 拉丁扩展-A小写
    if ((code >= 0x0100 && code <= 0x0137) ||
        (code >= 0x013A && code <= 0x0148) ||
        (code >= 0x014B && code <= 0x017E))
        return true;

    // 希腊小写
    if ((code >= 0x03B1 && code <= 0x03C9) ||
        (code >= 0x03CA && code <= 0x03CE))
        return true;

    // 西里尔小写
    if ((code >= 0x0430 && code <= 0x044F) ||
        (code >= 0x0451 && code <= 0x045F))
        return true;

    return false;
}

// 判断是否为大写字母
bool XChar_is_upper(const XChar ch) {
    if (!ch) return false;
    uint16_t code = ch;

    // ASCII大写
    if (code >= 'A' && code <= 'Z') return true;

    // 扩展拉丁大写
    if ((code >= 0x00C0 && code <= 0x00D6) ||
        (code >= 0x00D8 && code <= 0x00DE))
        return true;

    // 拉丁扩展-A大写
    if ((code >= 0x0100 && code <= 0x0137) ||
        (code >= 0x0139 && code <= 0x0147) ||
        (code >= 0x014A && code <= 0x017D))
        return true;

    // 希腊大写
    if ((code >= 0x0391 && code <= 0x03A9) ||
        (code >= 0x03AA && code <= 0x03AF))
        return true;

    // 西里尔大写
    if ((code >= 0x0410 && code <= 0x042F) ||
        (code >= 0x0401 && code <= 0x040F))
        return true;

    return false;
}

// 转换为大写字母
XChar XChar_to_upper(const XChar ch) {
    XChar result = ch;
    if (!XChar_is_lower(ch)) return result;

    // ASCII小写转大写
    if (ch>= 'a' && ch <= 'z') {
        result -= 32;
        return result;
    }

    // 特殊字符映射
    switch (ch) {
        // 拉丁扩展
    case 0x00DF: result = 0x1E9E; break; // ß -> ẞ
    case 0x00E0: result = 0x00C0; break; // à -> À
    case 0x00E1: result = 0x00C1; break; // á -> Á
    case 0x00E2: result = 0x00C2; break; // â -> Â
    case 0x00E3: result = 0x00C3; break; // ã -> Ã
    case 0x00E4: result = 0x00C4; break; // ä -> Ä
    case 0x00E5: result = 0x00C5; break; // å -> Å
    case 0x00E6: result = 0x00C6; break; // æ -> Æ
    case 0x00E7: result = 0x00C7; break; // ç -> Ç
    case 0x00E8: result = 0x00C8; break; // è -> È
    case 0x00E9: result = 0x00C9; break; // é -> É
    case 0x00EA: result = 0x00CA; break; // ê -> Ê
    case 0x00EB: result = 0x00CB; break; // ë -> Ë
    case 0x00EC: result = 0x00CC; break; // ì -> Ì
    case 0x00ED: result = 0x00CD; break; // í -> Í
    case 0x00EE: result = 0x00CE; break; // î -> Î
    case 0x00EF: result = 0x00CF; break; // ï -> Ï
    case 0x00F0: result = 0x00D0; break; // ð -> Ð
    case 0x00F1: result = 0x00D1; break; // ñ -> Ñ
    case 0x00F2: result = 0x00D2; break; // ò -> Ò
    case 0x00F3: result = 0x00D3; break; // ó -> Ó
    case 0x00F4: result = 0x00D4; break; // ô -> Ô
    case 0x00F5: result = 0x00D5; break; // õ -> Õ
    case 0x00F6: result = 0x00D6; break; // ö -> Ö
    case 0x00F8: result = 0x00D8; break; // ø -> Ø
    case 0x00F9: result = 0x00D9; break; // ù -> Ù
    case 0x00FA: result = 0x00DA; break; // ú -> Ú
    case 0x00FB: result = 0x00DB; break; // û -> Û
    case 0x00FC: result = 0x00DC; break; // ü -> Ü
    case 0x00FD: result = 0x00DD; break; // ý -> Ý
    case 0x00FE: result = 0x00DE; break; // þ -> Þ

        // 希腊字母
    case 0x03B1: result = 0x0391; break; // α -> Α
    case 0x03B2: result = 0x0392; break; // β -> Β
    case 0x03B3: result = 0x0393; break; // γ -> Γ
    case 0x03B4: result = 0x0394; break; // δ -> Δ
    case 0x03B5: result = 0x0395; break; // ε -> Ε
    case 0x03B6: result = 0x0396; break; // ζ -> Ζ
    case 0x03B7: result = 0x0397; break; // η -> Η
    case 0x03B8: result = 0x0398; break; // θ -> Θ
    case 0x03B9: result = 0x0399; break; // ι -> Ι
    case 0x03BA: result = 0x039A; break; // κ -> Κ
    case 0x03BB: result = 0x039B; break; // λ -> Λ
    case 0x03BC: result = 0x039C; break; // μ -> Μ
    case 0x03BD: result = 0x039D; break; // ν -> Ν
    case 0x03BE: result = 0x039E; break; // ξ -> Ξ
    case 0x03BF: result = 0x039F; break; // ο -> Ο
    case 0x03C0: result = 0x03A0; break; // π -> Π
    case 0x03C1: result = 0x03A1; break; // ρ -> Ρ
    case 0x03C2: 
    case 0x03C3: result = 0x03A3; break; // ς/σ -> Σ
    case 0x03C4: result = 0x03A4; break; // τ -> Τ
    case 0x03C5: result = 0x03A5; break; // υ -> Υ
    case 0x03C6: result = 0x03A6; break; // φ -> Φ
    case 0x03C7: result = 0x03A7; break; // χ -> Χ
    case 0x03C8: result = 0x03A8; break; // ψ -> Ψ
    case 0x03C9: result = 0x03A9; break; // ω -> Ω

        // 西里尔字母
    case 0x0430: result = 0x0410; break; // а -> А
    case 0x0431: result = 0x0411; break; // б -> Б
    case 0x0432: result = 0x0412; break; // в -> В
    case 0x0433: result = 0x0413; break; // г -> Г
    case 0x0434: result = 0x0414; break; // д -> Д
    case 0x0435: result = 0x0415; break; // е -> Е
    case 0x0436: result = 0x0416; break; // ж -> Ж
    case 0x0437: result = 0x0417; break; // з -> З
    case 0x0438: result = 0x0418; break; // и -> И
    case 0x0439: result = 0x0419; break; // й -> Й
    case 0x043a: result = 0x041a; break; // к -> К
    case 0x043b: result = 0x041b; break; // л -> Л
    case 0x043c: result = 0x041c; break; // м -> М
    case 0x043d: result = 0x041d; break; // н -> Н
    case 0x043e: result = 0x041e; break; // о -> О
    case 0x043f: result = 0x041f; break; // п -> П
    case 0x0440: result = 0x0420; break; // р -> Р
    case 0x0441: result = 0x0421; break; // с -> С
    case 0x0442: result = 0x0422; break; // т -> Т
    case 0x0443: result = 0x0423; break; // у -> У
    case 0x0444: result = 0x0424; break; // ф -> Ф
    case 0x0445: result = 0x0425; break; // х -> Х
    case 0x0446: result = 0x0426; break; // ц -> Ц
    case 0x0447: result = 0x0427; break; // ч -> Ч
    case 0x0448: result = 0x0428; break; // ш -> Ш
    case 0x0449: result = 0x0429; break; // щ -> Щ
    case 0x044a: result = 0x042a; break; // ъ -> Ъ
    case 0x044b: result = 0x042b; break; // ы -> Ы
    case 0x044c: result = 0x042c; break; // ь -> Ь
    case 0x044d: result = 0x042d; break; // э -> Э
    case 0x044e: result = 0x042e; break; // ю -> Ю
    case 0x044f: result = 0x042f; break; // я -> Я
    }

    return result;
}

// 转换为小写字母
XChar XChar_to_lower(const XChar ch) {
    XChar result = ch;
    if (!XChar_is_upper(ch)) return result;

    // ASCII大写转小写
    if (ch >= 'A' && ch <= 'Z') {
        result += 32;
        return result;
    }

    // 特殊字符映射
    switch (ch) {
        // 拉丁扩展
    case 0x1E9E: result = 0x00DF; break; // ẞ -> ß
    case 0x00C0: result = 0x00E0; break; // À -> à
    case 0x00C1: result = 0x00E1; break; // Á -> á
    case 0x00C2: result = 0x00E2; break; // Â -> â
    case 0x00C3: result = 0x00E3; break; // Ã -> ã
    case 0x00C4: result = 0x00E4; break; // Ä -> ä
    case 0x00C5: result = 0x00E5; break; // Å -> å
    case 0x00C6: result = 0x00E6; break; // Æ -> æ
    case 0x00C7: result = 0x00E7; break; // Ç -> ç
    case 0x00C8: result = 0x00E8; break; // È -> è
    case 0x00C9: result = 0x00E9; break; // É -> é
    case 0x00CA: result = 0x00EA; break; // Ê -> ê
    case 0x00CB: result = 0x00EB; break; // Ë -> ë
    case 0x00CC: result = 0x00EC; break; // Ì -> ì
    case 0x00CD: result = 0x00ED; break; // Í -> í
    case 0x00CE: result = 0x00EE; break; // Î -> î
    case 0x00CF: result = 0x00EF; break; // Ï -> ï
    case 0x00D0: result = 0x00F0; break; // Ð -> ð
    case 0x00D1: result = 0x00F1; break; // Ñ -> ñ
    case 0x00D2: result = 0x00F2; break; // Ò -> ò
    case 0x00D3: result = 0x00F3; break; // Ó -> ó
    case 0x00D4: result = 0x00F4; break; // Ô -> ô
    case 0x00D5: result = 0x00F5; break; // Õ -> õ
    case 0x00D6: result = 0x00F6; break; // Ö -> ö
    case 0x00D8: result = 0x00F8; break; // Ø -> ø
    case 0x00D9: result = 0x00F9; break; // Ù -> ù
    case 0x00DA: result = 0x00FA; break; // Ú -> ú
    case 0x00DB: result = 0x00FB; break; // Û -> û
    case 0x00DC: result = 0x00FC; break; // Ü -> ü
    case 0x00DD: result = 0x00FD; break; // Ý -> ý
    case 0x00DE: result = 0x00FE; break; // Þ -> þ

        // 希腊字母
    case 0x0391: result = 0x03B1; break; // Α -> α
    case 0x0392: result = 0x03B2; break; // Β -> β
    case 0x0393: result = 0x03B3; break; // Γ -> γ
    case 0x0394: result = 0x03B4; break; // Δ -> δ
    case 0x0395: result = 0x03B5; break; // Ε -> ε
    case 0x0396: result = 0x03B6; break; // Ζ -> ζ
    case 0x0397: result = 0x03B7; break; // Η -> η
    case 0x0398: result = 0x03B8; break; // Θ -> θ
    case 0x0399: result = 0x03B9; break; // Ι -> ι
    case 0x039A: result = 0x03BA; break; // Κ -> κ
    case 0x039B: result = 0x03BB; break; // Λ -> λ
    case 0x039C: result = 0x03BC; break; // Μ -> μ
    case 0x039D: result = 0x03BD; break; // Ν -> ν
    case 0x039E: result = 0x03BE; break; // Ξ -> ξ
    case 0x039F: result = 0x03BF; break; // Ο -> ο
    case 0x03A0: result = 0x03C0; break; // Π -> π
    case 0x03A1: result = 0x03C1; break; // Ρ -> ρ
    case 0x03A3: result = 0x03C3; break; // Σ -> σ
    case 0x03A4: result = 0x03C4; break; // Τ -> τ
    case 0x03A5: result = 0x03C5; break; // Υ -> υ
    case 0x03A6: result = 0x03C6; break; // Φ -> φ
    case 0x03A7: result = 0x03C7; break; // Χ -> χ
    case 0x03A8: result = 0x03C8; break; // Ψ -> ψ
    case 0x03A9: result = 0x03C9; break; // Ω -> ω

        // 西里尔字母
    case 0x0410: result = 0x0430; break; // А -> а
    case 0x0411: result = 0x0431; break; // Б -> б
    case 0x0412: result = 0x0432; break; // В -> в
    case 0x0413: result = 0x0433; break; // Г -> г
    case 0x0414: result = 0x0434; break; // Д -> д
    case 0x0415: result = 0x0435; break; // Е -> е
    case 0x0416: result = 0x0436; break; // Ж -> ж
    case 0x0417: result = 0x0437; break; // З -> з
    case 0x0418: result = 0x0438; break; // И -> и
    case 0x0419: result = 0x0439; break; // Й -> й
    case 0x041A: result = 0x043A; break; // К -> к
    case 0x041B: result = 0x043B; break; // Л -> л
    case 0x041C: result = 0x043C; break; // М -> м
    case 0x041D: result = 0x043D; break; // Н -> н
    case 0x041E: result = 0x043E; break; // О -> о
    case 0x041F: result = 0x043F; break; // П -> п
    case 0x0420: result = 0x0440; break; // Р -> р
    case 0x0421: result = 0x0441; break; // С -> с
    case 0x0422: result = 0x0442; break; // Т -> т
    case 0x0423: result = 0x0443; break; // У -> у
    case 0x0424: result = 0x0444; break; // Ф -> ф
    case 0x0425: result = 0x0445; break; // Х -> х
    case 0x0426: result = 0x0446; break; // Ц -> ц
    case 0x0427: result = 0x0447; break; // Ч -> ч
    case 0x0428: result = 0x0448; break; // Ш -> ш
    case 0x0429: result = 0x0449; break; // Щ -> щ
    case 0x042A: result = 0x044A; break; // Ъ -> ъ
    case 0x042B: result = 0x044B; break; // Ы -> ы
    case 0x042C: result = 0x044C; break; // Ь -> ь
    case 0x042D: result = 0x044D; break; // Э -> э
    case 0x042E: result = 0x044E; break; // Ю -> ю
    case 0x042F: result = 0x044F; break; // Я -> я
    }

    return result;
}

// 新增：判断控制字符
bool XChar_is_control(const XChar ch) {
    uint16_t code = ch;
    return (code >= 0x0000 && code <= 0x001F) ||  // C0控制字符
        (code >= 0x007F && code <= 0x009F);    // C1控制字符
}

// 新增：判断符号字符
bool XChar_is_symbol(const XChar ch) {
    uint16_t code = ch;

    // 数学符号
    if ((code >= 0x2200 && code <= 0x22FF) ||
        (code >= 0x27C0 && code <= 0x27EF))
        return true;

    // 货币符号
    if ((code >= 0x20A0 && code <= 0x20CF) ||
        code == 0x0024 || code == 0x00A2 || code == 0x00A3)
        return true;

    // 单位符号
    if ((code >= 0x3300 && code <= 0x33FF) ||
        (code >= 0x2100 && code <= 0x214F))
        return true;

    return false;
}

// 新增：判断表情符号
bool XChar_is_emoji(const XChar ch) {
    uint16_t code = ch;

    // 表情符号主范围
    if ((code >= 0x1F600 && code <= 0x1F64F) ||  // 情感表情
        (code >= 0x1F300 && code <= 0x1F5FF) ||  // 符号与象形图
        (code >= 0x1F680 && code <= 0x1F6FF) ||  // 交通与地图
        (code >= 0x1F1E0 && code <= 0x1F1FF))    // 国旗 emoji
        return true;

    return false;
}

// 新增：判断全角字符
bool XChar_is_fullwidth(const XChar ch) {
    uint16_t code = ch;
    // 全角ASCII范围、中文、日文、韩文等
    return (code >= 0xFF01 && code <= 0xFFEF) ||
        (code >= 0x4E00 && code <= 0x9FFF) ||
        (code >= 0x3040 && code <= 0x30FF) ||
        (code >= 0xAC00 && code <= 0xD7AF);
}

// 新增：判断半角字符
bool XChar_is_halfwidth(const XChar ch) {
    uint16_t code = ch;
    // 基本ASCII和半角符号
    return (code >= 0x0020 && code <= 0x007E) ||
        (code >= 0xFF61 && code <= 0xFF9F);  // 半角片假名
}

// 新增：半角转全角
XChar XChar_to_fullwidth(const XChar ch) {
    XChar result = ch;
    uint16_t code = ch;

    // 半角ASCII转全角
    if (code >= 0x20 && code <= 0x7E) {
        result = code + 0xFEE0;
        return result;
    }

    // 半角片假名转全角
    if (code >= 0xFF61 && code <= 0xFF9F) {
        result = code + 0x0040;
        return result;
    }

    return result;
}

// 新增：全角转半角
XChar XChar_to_halfwidth(const XChar ch) {
    XChar result = ch;
    uint16_t code = ch;

    // 全角ASCII转半角
    if (code >= 0xFF01 && code <= 0xFF5E) {
        result = code - 0xFEE0;
        return result;
    }

    // 全角片假名转半角
    if (code >= 0xFFA1 && code <= 0xFFDF) {
        result = code - 0x0040;
        return result;
    }

    return result;
}

// 代理对相关判断
bool XChar_is_high_surrogate(const XChar ch) {
    return ch && (ch >= UTF16_HIGH_SURROGATE_START &&
        ch <= UTF16_HIGH_SURROGATE_END);
}

bool XChar_is_low_surrogate(const XChar ch) {
    return ch && (ch >= UTF16_LOW_SURROGATE_START &&
        ch <= UTF16_LOW_SURROGATE_END);
}

bool XChar_is_surrogate(const XChar ch) {
    return XChar_is_high_surrogate(ch) || XChar_is_low_surrogate(ch);
}

// 从代理对获取完整Unicode码点
uint32_t XChar_surrogate_to_unicode(const XChar high, const XChar low) {
    if (!XChar_is_high_surrogate(high) || !XChar_is_low_surrogate(low)) {
        return 0;
    }
    uint32_t high_val = high - UTF16_HIGH_SURROGATE_START;
    uint32_t low_val = low - UTF16_LOW_SURROGATE_START;
    return (high_val << 10) + low_val + SURROGATE_OFFSET;
}

bool XChar_equals(const XChar a, const XChar b, XCharCaseSensitivity cs)
{
    if (cs == XCharCaseSensitive) {
        return a == b;
    }
    else {
        XChar a_lower = XChar_to_lower(a);
        XChar b_lower = XChar_to_lower(b);
        return a_lower == b_lower;
    }
}

int32_t XChar_compare(const XChar a, const XChar b) {
    return (a> b) ? 1 : (a < b ? -1 : 0);
}



// --------------------------
// UTF-8 转换实现
// --------------------------
int64_t XChar_from_utf8_stream(const uint8_t* utf8, size_t input_size, XChar* out, size_t max_out) {
    if (!utf8) return -1;

    // 计算实际输入长度
    size_t utf8_len = 0;
    if (input_size == 0) {
        // 自动检测NULL结尾
        while (utf8[utf8_len] != '\0') {
            utf8_len++;
        }
    }
    else {
        // 使用指定大小，但不超过NULL终止位置
        utf8_len = input_size;
        for (size_t i = 0; i < input_size; i++) {
            if (utf8[i] == '\0') {
                utf8_len = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!out || max_out == 0) {
        size_t count = 0;
        size_t i = 0;
        while (i < utf8_len) {
            uint8_t c = utf8[i];
            if ((c & 0x80) == 0) {
                // 1字节
                i++;
                count++;
            }
            else if ((c & 0xE0) == 0xC0) {
                // 2字节
                if (i + 1 >= utf8_len) break;
                i += 2;
                count++;
            }
            else if ((c & 0xF0) == 0xE0) {
                // 3字节
                if (i + 2 >= utf8_len) break;
                i += 3;
                count++;
            }
            else if ((c & 0xF8) == 0xF0) {
                // 4字节（转换为UTF-16代理对）
                if (i + 3 >= utf8_len) break;
                i += 4;
                count += 2; // 占2个XChar
            }
            else {
                // 无效UTF-8
                return -1;
            }
        }
        return (int64_t)count;
    }

    // 实际转换
    size_t out_idx = 0;
    size_t i = 0;
    while (i < utf8_len && out_idx + 1 < max_out) { // 留一个位置放终止符
        uint8_t c = utf8[i];
        uint32_t code = 0;

        if ((c & 0x80) == 0) {
            // 1字节：0xxxxxxx
            code = c;
            i++;
        }
        else if ((c & 0xE0) == 0xC0) {
            // 2字节：110xxxxx 10xxxxxx
            if (i + 1 >= utf8_len) return -1;
            code = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F);
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0) {
            // 3字节：1110xxxx 10xxxxxx 10xxxxxx
            if (i + 2 >= utf8_len) return -1;
            code = ((c & 0x0F) << 12) | ((utf8[i + 1] & 0x3F) << 6) | (utf8[i + 2] & 0x3F);
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0) {
            // 4字节：11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            if (i + 3 >= utf8_len) return -1;
            code = ((c & 0x07) << 18) | ((utf8[i + 1] & 0x3F) << 12) |
                ((utf8[i + 2] & 0x3F) << 6) | (utf8[i + 3] & 0x3F);
            i += 4;
        }
        else {
            // 无效UTF-8序列
            return -1;
        }

        // 转换为UTF-16
        if (code <= 0xFFFF) {
            out[out_idx++] = XChar_from_unicode(code);
        }
        else {
            // 补充平面，生成代理对
            if (out_idx + 1 >= max_out) return -1; // 需要两个位置
            out[out_idx++] = XChar_from_unicode(code);
            out[out_idx++] = XChar_from_unicode_low(code);
        }
    }

    // 添加终止符
    if (out_idx < max_out) {
        out[out_idx] = 0;
    }
    else {
        return -1; // 缓冲区不足
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_utf8_stream(const XChar* ch, size_t input_count, uint8_t* utf8, size_t max_utf8) {
    if (!ch) return -1;

    // 计算实际输入XChar数量
    size_t ch_count = 0;
    if (input_count == 0) {
        // 自动检测终止符
        while (ch[ch_count] != 0) {
            ch_count++;
        }
    }
    else {
        // 使用指定数量，但不超过终止符位置
        ch_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i] == 0) {
                ch_count = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!utf8 || max_utf8 == 0) {
        size_t byte_count = 0;
        for (size_t i = 0; i < ch_count; i++) {
            uint32_t code = ch[i];

            // 处理代理对
            if (XChar_is_high_surrogate(ch[i]) && i + 1 < ch_count) {
                code = XChar_surrogate_to_unicode(ch[i], ch[i + 1]);
                i++; // 跳过低代理
            }

            if (code <= 0x7F) {
                byte_count += 1;
            }
            else if (code <= 0x7FF) {
                byte_count += 2;
            }
            else if (code <= 0xFFFF) {
                byte_count += 3;
            }
            else if (code <= 0x10FFFF) {
                byte_count += 4;
            }
            else {
                return -1; // 无效码点
            }
        }
        return (int64_t)byte_count;
    }

    // 实际转换
    size_t out_idx = 0;
    for (size_t i = 0; i < ch_count && out_idx + 1 < max_utf8; i++) {
        uint32_t code = ch[i];

        // 处理代理对
        if (XChar_is_high_surrogate(ch[i]) && i + 1 < ch_count) {
            code = XChar_surrogate_to_unicode(ch[i], ch[i + 1]);
            i++; // 跳过低代理
        }

        if (code <= 0x7F) {
            // 1字节
            if (out_idx + 1 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)code;
        }
        else if (code <= 0x7FF) {
            // 2字节
            if (out_idx + 2 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)(0xC0 | (code >> 6));
            utf8[out_idx++] = (uint8_t)(0x80 | (code & 0x3F));
        }
        else if (code <= 0xFFFF) {
            // 3字节
            if (out_idx + 3 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)(0xE0 | (code >> 12));
            utf8[out_idx++] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
            utf8[out_idx++] = (uint8_t)(0x80 | (code & 0x3F));
        }
        else if (code <= 0x10FFFF) {
            // 4字节
            if (out_idx + 4 >= max_utf8) return -1;
            utf8[out_idx++] = (uint8_t)(0xF0 | (code >> 18));
            utf8[out_idx++] = (uint8_t)(0x80 | ((code >> 12) & 0x3F));
            utf8[out_idx++] = (uint8_t)(0x80 | ((code >> 6) & 0x3F));
            utf8[out_idx++] = (uint8_t)(0x80 | (code & 0x3F));
        }
        else {
            return -1; // 无效码点
        }
    }

    // 添加终止符
    if (out_idx < max_utf8) {
        utf8[out_idx] = '\0';
    }
    else {
        return -1; // 缓冲区不足
    }

    return (int64_t)out_idx;
}


// --------------------------
// UTF-16编码转换函数
// --------------------------

int64_t XChar_from_utf16_stream(const uint16_t* utf16_str, size_t input_size, XChar* out_xchars, size_t max_count) {
    if (!utf16_str) return -1;

    // 计算实际输入长度
    size_t utf16_len = 0;
    if (input_size == 0) {
        // 自动检测L'\0'结尾
        while (utf16_str[utf16_len] != 0) {
            utf16_len++;
        }
    }
    else {
        // 使用指定大小，但不超过终止符位置
        utf16_len = input_size;
        for (size_t i = 0; i < input_size; i++) {
            if (utf16_str[i] == 0) {
                utf16_len = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!out_xchars || max_count == 0) {
        return (int64_t)utf16_len; // 1:1转换（代理对已在UTF-16中）
    }

    // 实际转换
    size_t out_idx = 0;
    while (out_idx < utf16_len && out_idx + 1 < max_count) {
        out_xchars[out_idx] = XChar_from(utf16_str[out_idx]);
        out_idx++;
    }

    // 添加终止符
    if (out_idx < max_count) {
        out_xchars[out_idx] = 0;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_utf16_stream(const XChar* xchars, size_t input_count, uint16_t* out_buf, size_t buf_size) {
    if (!xchars) return -1;

    // 计算实际输入XChar数量
    size_t xchar_count = 0;
    if (input_count == 0) {
        while (xchars[xchar_count] != 0) {
            xchar_count++;
        }
    }
    else {
        xchar_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (xchars[i] == 0) {
                xchar_count = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!out_buf || buf_size == 0) {
        return (int64_t)xchar_count; // 1:1转换
    }

    // 实际转换
    size_t out_idx = 0;
    while (out_idx < xchar_count && out_idx + 1 < buf_size) {
        out_buf[out_idx] = xchars[out_idx];
        out_idx++;
    }

    // 添加终止符
    if (out_idx < buf_size) {
        out_buf[out_idx] = 0;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

// --------------------------
// UTF-32编码转换函数
// --------------------------

int64_t XChar_from_utf32_stream(const uint32_t* utf32, size_t input_count, XChar* out, size_t max_out) {
    if (!utf32) return -1;

    // 计算实际输入码点数量
    size_t utf32_count = 0;
    if (input_count == 0) {
        while (utf32[utf32_count] != 0) {
            utf32_count++;
        }
    }
    else {
        utf32_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (utf32[i] == 0) {
                utf32_count = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!out || max_out == 0) {
        size_t xchar_count = 0;
        for (size_t i = 0; i < utf32_count; i++) {
            uint32_t code = utf32[i];
            if (code > 0xFFFF) {
                xchar_count += 2; // 代理对
            }
            else {
                xchar_count += 1;
            }
        }
        return (int64_t)xchar_count;
    }

    // 实际转换
    size_t out_idx = 0;
    for (size_t i = 0; i < utf32_count && out_idx + 1 < max_out; i++) {
        uint32_t code = utf32[i];
        if (code > UNICODE_MAX_CODEPOINT) {
            return -1; // 无效码点
        }

        if (code <= 0xFFFF) {
            out[out_idx++] = XChar_from_unicode(code);
        }
        else {
            // 补充平面，生成代理对
            if (out_idx + 1 >= max_out) return -1;
            out[out_idx++] = XChar_from_unicode(code);
            out[out_idx++] = XChar_from_unicode_low(code);
        }
    }

    // 添加终止符
    if (out_idx < max_out) {
        out[out_idx] = 0;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_utf32_stream(const XChar* ch, size_t input_count, uint32_t* utf32, size_t max_utf32) {
    if (!ch) return -1;

    // 计算实际输入XChar数量
    size_t ch_count = 0;
    if (input_count == 0) {
        while (ch[ch_count] != 0) {
            ch_count++;
        }
    }
    else {
        ch_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i] == 0) {
                ch_count = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!utf32 || max_utf32 == 0) {
        size_t utf32_count = 0;
        for (size_t i = 0; i < ch_count; i++) {
            if (XChar_is_high_surrogate(ch[i]) && i + 1 < ch_count) {
                i++; // 跳过低代理
            }
            utf32_count++;
        }
        return (int64_t)utf32_count;
    }

    // 实际转换
    size_t out_idx = 0;
    for (size_t i = 0; i < ch_count && out_idx + 1 < max_utf32; i++) {
        uint32_t code;
        if (XChar_is_high_surrogate(ch[i]) && i + 1 < ch_count) {
            code = XChar_surrogate_to_unicode(ch[i], ch[i + 1]);
            i++; // 跳过低代理
        }
        else {
            code = ch[i];
        }
        utf32[out_idx++] = code;
    }

    // 添加终止符
    if (out_idx < max_utf32) {
        utf32[out_idx] = 0;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}
// --------------------------
// Latin-1编码转换函数
// --------------------------

int64_t XChar_from_latin1_stream(const uint8_t* latin1, size_t input_size, XChar* out, size_t max_out) {
    if (!latin1) return -1;

    // 计算实际输入长度
    size_t latin1_len = 0;
    if (input_size == 0) {
        while (latin1[latin1_len] != '\0') {
            latin1_len++;
        }
    }
    else {
        latin1_len = input_size;
        for (size_t i = 0; i < input_size; i++) {
            if (latin1[i] == '\0') {
                latin1_len = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!out || max_out == 0) {
        return (int64_t)latin1_len;
    }

    // 实际转换（Latin1直接映射到UTF-16）
    size_t out_idx = 0;
    while (out_idx < latin1_len && out_idx + 1 < max_out) {
        out[out_idx] = XChar_from((uint16_t)latin1[out_idx]);
        out_idx++;
    }

    // 添加终止符
    if (out_idx < max_out) {
        out[out_idx] = 0;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_latin1_stream(const XChar* ch, size_t input_count, uint8_t* latin1, size_t max_latin1) {
    if (!ch) return -1;

    // 计算实际输入XChar数量
    size_t ch_count = 0;
    if (input_count == 0) {
        while (ch[ch_count] != 0) {
            ch_count++;
        }
    }
    else {
        ch_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i] == 0) {
                ch_count = i;
                break;
            }
        }
    }

    // 检查是否有超出Latin1范围的码点
    for (size_t i = 0; i < ch_count; i++) {
        if (ch[i] > 0xFF) {
            return -1; // 超出范围
        }
    }

    // 仅计算所需大小
    if (!latin1 || max_latin1 == 0) {
        return (int64_t)ch_count;
    }

    // 实际转换
    size_t out_idx = 0;
    while (out_idx < ch_count && out_idx + 1 < max_latin1) {
        latin1[out_idx] = (uint8_t)ch[out_idx];
        out_idx++;
    }

    // 添加终止符
    if (out_idx < max_latin1) {
        latin1[out_idx] = '\0';
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

// --------------------------
// 本地编码转换实现（平台相关）
// --------------------------

// --------------------------
// GBK 转 XChar（支持输出为NULL时计算大小）
// --------------------------
int64_t XChar_from_gbk_stream(const char* gbk, size_t input_size, XChar* out, size_t max_out)
{
    if (!gbk) return -1;

    // 计算输入GBK字符串的实际长度（不含终止符）
    size_t actual_len = input_size;
    if (input_size == 0) {
        // 自动检测终止符
        while (gbk[actual_len] != '\0') {
            actual_len++;
        }
    }
    else {
        // 使用指定大小，但不超过终止符位置
        for (size_t i = 0; i < input_size; i++) 
        {
            if (gbk[i] == 0) 
            {
                actual_len = i;
                break;
            }
        }
    }

    if (actual_len == 0) {
        // 空字符串处理
        if (out && max_out > 0) {
            out[0] = 0;
        }
        return 0;
    }

    int64_t required = -1;

#ifdef _WIN32
    // Windows平台使用MultiByteToWideChar（GBK对应代码页936）
    // 先计算所需UTF-16字符数（含终止符）
    int wchar_count = MultiByteToWideChar(CP_ACP, 0, gbk, actual_len+1, NULL, 0);
    if (wchar_count <= 0) return -1;

    required = (int64_t)(wchar_count - 1); // 不含终止符的数量

    // 仅计算所需大小
    if (!out || max_out == 0) {
        return required;
    }

    // 检查输出缓冲区是否足够（需包含终止符）
    if (max_out < (size_t)wchar_count) {
        return -1;
    }

    // 执行转换
    if (MultiByteToWideChar(CP_ACP, 0, gbk, actual_len+1, (wchar_t*)out, (int)max_out) <= 0) {
        return -1;
    }

    // 添加终止符
    out[wchar_count - 1] = 0;

#elif defined(__linux__)
    // Linux平台使用iconv
    iconv_t cd = iconv_open("UTF-16LE", "GBK"); // XChar使用UTF-16LE编码
    if (cd == (iconv_t)-1) return -1;

    // 计算所需UTF-16字符数（先获取字节数再转换为字符数）
    char* in_buf = (char*)gbk;
    size_t in_left = actual_len;
    char dummy[4096];
    char* out_buf = dummy;
    size_t out_left = sizeof(dummy);
    size_t result = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);

    // 重置转换描述符
    iconv(cd, NULL, NULL, &out_buf, &out_left);
    required = (int64_t)((sizeof(dummy) - out_left) / 2); // UTF-16每个字符2字节

    // 仅计算所需大小
    if (!out || max_out == 0) {
        iconv_close(cd);
        return required;
    }

    // 检查输出缓冲区是否足够（需包含终止符）
    if (max_out < (size_t)(required + 1)) {
        iconv_close(cd);
        return -1;
    }

    // 执行实际转换
    in_buf = (char*)gbk;
    in_left = actual_len;
    out_buf = (char*)out;
    out_left = max_out * 2; // 每个XChar占2字节
    result = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);

    if (result == (size_t)-1 && errno != E2BIG) {
        iconv_close(cd);
        return -1;
    }

    // 添加终止符
    size_t converted = (max_out * 2 - out_left) / 2;
    out[converted] = 0;
    required = (int64_t)converted;

    iconv_close(cd);
#endif

    return required;
}

int64_t XChar_to_gbk_stream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    if (!ch) return -1;

    // 计算输入XChar数组的实际长度（不含终止符）
    size_t actual_count = input_count;
    if (input_count == 0) {
        // 自动检测终止符
        while (ch[actual_count] != 0) {
            actual_count++;
        }
    }
    else {
        // 使用指定大小，但不超过终止符位置
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i] == 0) {
                actual_count = i;
                break;
            }
        }
    }

    if (actual_count == 0) {
        // 空数组处理
        if (gbk && max_gbk > 0) {
            gbk[0] = '\0';
        }
        return 0;
    }

    int64_t required = -1;

#ifdef _WIN32
    // Windows平台使用WideCharToMultiByte
    // 先计算所需GBK字节数（含终止符）
    int gbk_len = WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)ch, actual_count+1, NULL, 0, NULL, NULL);
    if (gbk_len <= 0) return -1;

    required = (int64_t)(gbk_len - 1); // 不含终止符的数量

    // 仅计算所需大小
    if (!gbk || max_gbk == 0) {
        return required;
    }

    // 检查输出缓冲区是否足够（需包含终止符）
    if (max_gbk < (size_t)gbk_len) {
        return -1;
    }

    // 执行转换
    if (WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)ch, actual_count+1, gbk, (int)max_gbk, NULL, NULL) <= 0) {
        return -1;
    }

    // 添加终止符
    gbk[gbk_len - 1] = '\0';

#elif defined(__linux__)
    // Linux平台使用iconv
    iconv_t cd = iconv_open("GBK", "UTF-16LE"); // XChar使用UTF-16LE编码
    if (cd == (iconv_t)-1) return -1;

    // 计算所需GBK字节数
    char* in_buf = (char*)ch;
    size_t in_left = actual_count * 2; // 每个XChar占2字节
    char dummy[4096];
    char* out_buf = dummy;
    size_t out_left = sizeof(dummy);
    size_t result = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);

    // 重置转换描述符
    iconv(cd, NULL, NULL, &out_buf, &out_left);
    required = (int64_t)(sizeof(dummy) - out_left);

    // 仅计算所需大小
    if (!gbk || max_gbk == 0) {
        iconv_close(cd);
        return required;
    }

    // 检查输出缓冲区是否足够（需包含终止符）
    if (max_gbk < (size_t)(required + 1)) {
        iconv_close(cd);
        return -1;
    }

    // 执行实际转换
    in_buf = (char*)ch;
    in_left = actual_count * 2;
    out_buf = gbk;
    out_left = max_gbk;
    result = iconv(cd, &in_buf, &in_left, &out_buf, &out_left);

    if (result == (size_t)-1 && errno != E2BIG) {
        iconv_close(cd);
        return -1;
    }

    // 添加终止符
    size_t converted = max_gbk - out_left;
    gbk[converted] = '\0';
    required = (int64_t)converted;

    iconv_close(cd);
#endif

    return required;
}

int64_t XChar_from_local_stream(const char* local_str, size_t input_size, XChar* out, size_t max_out)
{
#ifdef _WIN32
    // Windows本地编码为GBK
    return XChar_from_gbk_stream(local_str, input_size, out, max_out);
#elif defined(__linux__)
    // Linux本地编码为UTF-8
    return XChar_from_utf8_stream((const uint8_t*)local_str, input_size, out, max_out);
#else
    return -1;
#endif
}

int64_t XChar_to_local_stream(const XChar* ch, size_t input_count, char* local_str, size_t max_local)
{
#ifdef _WIN32
    // Windows本地编码为GBK
    return XChar_to_gbk_stream(ch, input_count, local_str, max_local);
#elif defined(__linux__)
    // Linux本地编码为UTF-8
    return XChar_to_utf8_stream(ch, input_count, (uint8_t*)local_str, max_local);
#else
    return -1;
#endif
}
// --------------------------
// UTF-8与GBK互转函数（跨平台）
// --------------------------
int64_t XUTF8_to_gbk_stream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
{
    if (!utf8_str) return -1;

    // 计算实际输入长度
    size_t utf8_len = 0;
    if (input_size == 0) {
        while (utf8_str[utf8_len] != '\0') {
            utf8_len++;
        }
    }
    else {
        utf8_len = input_size;
        for (size_t i = 0; i < input_size; i++) {
            if (utf8_str[i] == '\0') {
                utf8_len = i;
                break;
            }
        }
    }

#ifdef _WIN32
    // Windows使用API转换
    int wchar_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, (int)utf8_len, NULL, 0);
    if (wchar_len <= 0) return -1;

    wchar_t* wstr = (wchar_t*)XMemory_malloc(sizeof(wchar_t) * (wchar_len + 1));
    if (!wstr) return -1;

    if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, (int)utf8_len, wstr, wchar_len) != wchar_len) {
        XMemory_free(wstr);
        return -1;
    }
    wstr[wchar_len] = L'\0';

    // 计算GBK所需大小
    int gbk_len = WideCharToMultiByte(CP_ACP, 0, wstr, wchar_len, NULL, 0, NULL, NULL);
    if (gbk_len <= 0) {
        XMemory_free(wstr);
        return -1;
    }

    // 仅返回大小
    if (!gbk_buf || max_len == 0) {
        XMemory_free(wstr);
        return (int64_t)gbk_len;
    }

    // 检查缓冲区
    if (max_len < (size_t)gbk_len + 1) {
        XMemory_free(wstr);
        return -1;
    }

    // 转换为GBK
    if (WideCharToMultiByte(CP_ACP, 0, wstr, wchar_len, gbk_buf, gbk_len, NULL, NULL) != gbk_len) {
        XMemory_free(wstr);
        return -1;
    }
    gbk_buf[gbk_len] = '\0';

    XMemory_free(wstr);
    return (int64_t)gbk_len;

#elif defined(__linux__)
    // Linux使用iconv转换
    iconv_t cd = iconv_open("GBK", "UTF-8");
    if (cd == (iconv_t)-1) return -1;

    // 计算所需大小（第一次转换获取长度）
    char* in_buf = (char*)utf8_str;
    size_t in_left = utf8_len;
    size_t out_left = 0;
    char* dummy_buf = NULL;
    int64_t result = -1;

    // 第一次调用获取所需大小
    if (iconv(cd, &in_buf, &in_left, &dummy_buf, &out_left) == (size_t)-1 && errno != E2BIG) {
        iconv_close(cd);
        return -1;
    }
    size_t required_len = out_left;

    // 仅返回大小
    if (!gbk_buf || max_len == 0) {
        iconv_close(cd);
        return (int64_t)required_len;
    }

    // 检查缓冲区
    if (max_len < required_len + 1) {
        iconv_close(cd);
        return -1;
    }

    // 实际转换
    in_buf = (char*)utf8_str;
    in_left = utf8_len;
    char* out_buf = gbk_buf;
    out_left = max_len - 1; // 留一个字节放终止符

    if (iconv(cd, &in_buf, &in_left, &out_buf, &out_left) != (size_t)-1) {
        *out_buf = '\0';
        result = (int64_t)(max_len - 1 - out_left);
    }

    iconv_close(cd);
    return result;
#else
return -1;
#endif
}

int64_t XGBK_to_utf8_stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
{
    if (!gbk_str) return -1;

    // 计算实际输入长度
    size_t gbk_len = 0;
    if (input_size == 0) {
        while (gbk_str[gbk_len] != '\0') {
            gbk_len++;
        }
    }
    else {
        gbk_len = input_size;
        for (size_t i = 0; i < input_size; i++) {
            if (gbk_str[i] == '\0') {
                gbk_len = i;
                break;
            }
        }
    }

#ifdef _WIN32
    // Windows使用API转换
    int wchar_len = MultiByteToWideChar(CP_ACP, 0, gbk_str, (int)gbk_len, NULL, 0);
    if (wchar_len <= 0) return -1;

    wchar_t* wstr = (wchar_t*)XMemory_malloc(sizeof(wchar_t) * (wchar_len + 1));
    if (!wstr) return -1;

    if (MultiByteToWideChar(CP_ACP, 0, gbk_str, (int)gbk_len, wstr, wchar_len) != wchar_len) {
        XMemory_free(wstr);
        return -1;
    }
    wstr[wchar_len] = L'\0';

    // 计算UTF-8所需大小
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, wchar_len, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) {
        XMemory_free(wstr);
        return -1;
    }

    // 仅返回大小
    if (!utf8_buf || max_len == 0) {
        XMemory_free(wstr);
        return (int64_t)utf8_len;
    }

    // 检查缓冲区
    if (max_len < (size_t)utf8_len + 1) {
        XMemory_free(wstr);
        return -1;
    }

    // 转换为UTF-8
    if (WideCharToMultiByte(CP_UTF8, 0, wstr, wchar_len, utf8_buf, utf8_len, NULL, NULL) != utf8_len) {
        XMemory_free(wstr);
        return -1;
    }
    utf8_buf[utf8_len] = '\0';

    XMemory_free(wstr);
    return (int64_t)utf8_len;
#elif defined(__linux__)
    // Linux使用iconv转换
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == (iconv_t)-1) return -1;

    // 计算所需大小（第一次转换获取长度）
    char* in_buf = (char*)gbk_str;
    size_t in_left = gbk_len;
    size_t out_left = 0;
    char* dummy_buf = NULL;
    int64_t result = -1;

    // 第一次调用获取所需大小
    if (iconv(cd, &in_buf, &in_left, &dummy_buf, &out_left) == (size_t)-1 && errno != E2BIG) {
        iconv_close(cd);
        return -1;
    }
    size_t required_len = out_left;

    // 仅返回大小
    if (!utf8_buf || max_len == 0) {
        iconv_close(cd);
        return (int64_t)required_len;
    }

    // 检查缓冲区
    if (max_len < required_len + 1) {
        iconv_close(cd);
        return -1;
    }

    // 实际转换
    in_buf = (char*)gbk_str;
    in_left = gbk_len;
    char* out_buf = utf8_buf;
    out_left = max_len - 1; // 留一个字节放终止符

    if (iconv(cd, &in_buf, &in_left, &out_buf, &out_left) != (size_t)-1) {
        *out_buf = '\0';
        result = (int64_t)(max_len - 1 - out_left);
    }

    iconv_close(cd);
    return result;
#else
    return -1;
#endif
}
/*                                  数值与字符串互转                                */   
/**
 * @brief 计算UTF-16字符串的实际长度（不含终止符）
 * @param xchars 待处理的XChar数组（UTF-16字符串，以code=0为终止符）
 * @param input_count 外部提供的缓冲区大小（XChar元素数量），0表示不限制范围
 * @return size_t 有效字符数（不含终止符），xchars为NULL时返回0
 */
static size_t XChar_get_input_length_stream(const XChar* xchars, size_t input_count) {
    if (!xchars) return 0;

    if (input_count == 0) {
        size_t len = 0;
        while (xchars[len] != 0) len++;
        return len;
    }
    else {
        size_t len = 0;
        while (len < input_count && xchars[len] != 0) len++;
        return len;
    }
}

/**
 * @brief 将UTF-16字符转换为进制对应的值（0-35）
 * @param ch 待转换的XChar（数字/字母）
 * @return int 转换后的值（0-35），无效字符返回-1
 */
static int XChar_digit_to_value(XChar ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'Z') return 10 + (ch - 'A');
    return -1;
}

/**
 * @brief 将进制值（0-35）转换为UTF-16字符
 * @param value 待转换的值（0-35）
 * @param uppercase 字母是否使用大写（16-36进制时生效）
 * @return XChar 转换后的字符，无效值返回0
 */
static XChar XChar_value_to_digit(int value, bool uppercase) {
    if (value >= 0 && value <= 9) return '0' + value;
    if (value >= 10 && value <= 35) {
        return uppercase ? ('A' + (value - 10)) : ('a' + (value - 10));
    }
    return 0;
}

/**
 * @brief 通用无符号整数解析（XChar数组→unsigned long long）
 * @param xchars 输入的UTF-16字符串
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=解析成功，false=失败
 * @return unsigned long long 解析结果，失败返回0
 */
static unsigned long long XChar_parse_unsigned(const XChar* xchars, size_t input_count, int base, bool* success) {
    // 初始化success（仅当非NULL时）
    if (success != NULL) {
        *success = false;
    }

    // 入参合法性检查
    if (!xchars || base < 2 || base > 36) {
        return 0;
    }

    size_t len = XChar_get_input_length_stream(xchars, input_count);
    if (len == 0) {
        return 0;
    }

    unsigned long long result = 0;
    size_t i = 0;

    // 跳过前导空白字符
    while (i < len && (xchars[i] == ' ' || xchars[i] == '\t' || xchars[i] == '\n')) {
        i++;
    }
    if (i >= len) {
        return 0;
    }

    // 解析数字部分
    bool has_valid_digit = false;
    while (i < len) {
        int digit = XChar_digit_to_value(xchars[i]);
        if (digit == -1 || digit >= base) {
            break; // 遇到无效字符，停止解析
        }

        // 溢出检查：避免result * base + digit超出ULLONG_MAX
        if (result > (ULLONG_MAX - digit) / base) {
            return 0; // 溢出，返回0且success保持false
        }

        result = result * base + digit;
        has_valid_digit = true;
        i++;
    }

    // 仅当有有效数字时，才视为解析成功
    if (has_valid_digit) {
        if (success != NULL) {
            *success = true;
        }
    }

    return result;
}
/**
 * @brief 通用有符号整数解析（XChar数组→long long）
 * @param xchars 输入的UTF-16字符串
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=解析成功，false=失败
 * @return long long 解析结果，失败返回0
 */
static long long XChar_parse_signed(const XChar* xchars, size_t input_count, int base, bool* success) {
    // 1. 初始化success（仅当success非NULL时）
    if (success != NULL) {
        *success = false;
    }

    // 入参合法性检查：xchars为空或进制非法，直接返回0
    if (!xchars || base < 2 || base > 36) {
        return 0;
    }

    // 获取UTF-16字符串有效长度（不越界）
    size_t len = XChar_get_input_length_stream(xchars, input_count);
    if (len == 0) { // 空字符串，无有效字符
        return 0;
    }

    size_t i = 0;
    int sign = 1;

    // 跳过前导空白字符（空格、制表符、换行符）
    while (i < len && (xchars[i] == ' ' || xchars[i] == '\t' || xchars[i] == '\n')) {
        i++;
    }
    if (i >= len) { // 仅包含空白字符，无有效数字
        return 0;
    }

    // 处理正负符号（仅支持开头的'+'或'-'）
    if (xchars[i] == '-') {
        sign = -1;
        i++;
    }
    else if (xchars[i] == '+') {
        i++;
    }

    // 解析无符号部分（注意：此处success可能为NULL，需确保XChar_parse_unsigned也处理NULL）
    unsigned long long abs_val = XChar_parse_unsigned(&xchars[i], len - i, base, success);

    // 2. 检查无符号解析结果（仅当success非NULL时判断）
    if (success != NULL && !*success) {
        return 0;
    }

    // 3. 溢出检查：根据符号判断是否超出long long范围
    bool overflow = false;
    if (sign == 1) {
        // 正数：不能超过LLONG_MAX
        if (abs_val > LLONG_MAX) {
            overflow = true;
        }
    }
    else {
        // 负数：不能超过LLONG_MIN的绝对值（即 (unsigned long long)LLONG_MAX + 1）
        if (abs_val > (unsigned long long)LLONG_MAX + 1) {
            overflow = true;
        }
    }

    // 溢出时设置success（仅当success非NULL时）
    if (overflow) {
        if (success != NULL) {
            *success = false;
        }
        return 0;
    }

    // 4. 解析成功：设置success为true（仅当success非NULL时）
    if (success != NULL) {
        *success = true;
    }

    // 计算最终结果（符号×绝对值）
    return (long long)(abs_val * sign);
}
/**
 * @brief 浮点数解析（XChar数组→double）
 * @param xchars 输入的UTF-16字符串
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param success 输出参数：true=解析成功，false=失败
 * @return double 解析结果，失败返回0.0
 */
static double XChar_parse_float(const XChar* xchars, size_t input_count, bool* success) {
    if (success) *success = false;
    if (!xchars) return 0.0;

    size_t len = XChar_get_input_length_stream(xchars, input_count);
    if (len == 0) return 0.0;

    size_t i = 0;
    int sign = 1;
    double result = 0.0;
    bool has_digits = false;

    // 跳过前导空格
    while (i < len && (xchars[i] == ' ' || xchars[i] == '\t' || xchars[i] == '\n')) i++;
    if (i >= len) return 0.0;

    // 处理符号
    if (xchars[i] == '-') {
        sign = -1;
        i++;
    }
    else if (xchars[i] == '+') {
        i++;
    }

    // 解析整数部分
    while (i < len) {
        int digit = XChar_digit_to_value(xchars[i]);
        if (digit < 0 || digit > 9) break;

        result = result * 10.0 + digit;
        has_digits = true;
        i++;
    }

    // 解析小数部分
    if (i < len && xchars[i] == '.') {
        i++;
        double fraction = 0.0;
        double divisor = 10.0;

        while (i < len) {
            int digit = XChar_digit_to_value(xchars[i]);
            if (digit < 0 || digit > 9) break;

            fraction += digit / divisor;
            divisor *= 10.0;
            has_digits = true;
            i++;
        }
        result += fraction;
    }

    // 解析指数部分
    if (i < len && (xchars[i] == 'e' || xchars[i] == 'E')) {
        i++;
        int exp_sign = 1;
        int exponent = 0;
        bool has_exp_digits = false;

        // 指数符号
        if (i < len && (xchars[i] == '-' || xchars[i] == '+')) {
            exp_sign = (xchars[i] == '-') ? -1 : 1;
            i++;
        }

        // 指数数值
        while (i < len) {
            int digit = XChar_digit_to_value(xchars[i]);
            if (digit < 0 || digit > 9) break;

            exponent = exponent * 10 + digit;
            has_exp_digits = true;
            i++;
        }

        // 应用指数
        if (has_exp_digits) {
            exponent *= exp_sign;
            while (exponent > 0) { result *= 10.0; exponent--; }
            while (exponent < 0) { result /= 10.0; exponent++; }
        }
    }

    if (has_digits) {
        if (success) *success = true;
        return result * sign;
    }
    return 0.0;
}

/**
 * @brief 无符号整数转UTF-16数组（核心辅助函数）
 * @param value 待转换的无符号整数
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度（不含终止符），out空返回所需长度；失败返回-1
 */
static int64_t XChar_from_unsigned_num(unsigned long long value, int base, XChar* out, size_t max_out, bool uppercase) {
    // 入参合法性检查
    if (base < 2 || base > 36) return -1;

    // 情况1：值为0（特殊处理，避免循环计算）
    if (value == 0) {
        if (out == NULL) return 1; // 仅需1个字符（'0'）
        if (max_out < 1) return -1; // 缓冲区至少能存1个字符

        out[0] = '0';
        if (max_out >= 2) out[1] = 0; // 有空间则加终止符
        return 1;
    }

    // 计算所需字符数（无论out是否为NULL，先算长度）
    size_t req_len = 0;
    unsigned long long temp_val = value;
    while (temp_val > 0) {
        temp_val /= base;
        req_len++;
    }

    // 情况2：out为NULL，直接返回所需长度
    if (out == NULL) return (int64_t)req_len;

    // 情况3：out非空，检查缓冲区是否足够
    if (max_out < req_len) return -1; // 连字符都存不下，返回失败

    // 填充缓冲区（逆序计算→正序写入）
    temp_val = value;
    for (size_t i = 0; i < req_len; i++) {
        int rem = (int)(temp_val % base);
        temp_val /= base;
        out[req_len - 1 - i] = XChar_value_to_digit(rem, uppercase);
    }

    // 有空间则添加终止符（不占返回长度）
    if (max_out > req_len) out[req_len] = 0;

    return (int64_t)req_len;
}

/**
 * @brief 有符号整数转UTF-16数组（核心辅助函数）
 * @param value 待转换的有符号整数
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度（不含终止符），out空返回所需长度；失败返回-1
 */
static int64_t XChar_from_signed_num(long long value, int base, XChar* out, size_t max_out, bool uppercase) {
    // 入参合法性检查
    if (base < 2 || base > 36) return -1;

    // 情况1：负数（需额外1个负号字符）
    if (value < 0) {
        // 计算无符号部分所需长度（递归调用辅助函数，out传NULL仅算长度）
        int64_t abs_len = XChar_from_unsigned_num((unsigned long long)(-value), base, NULL, 0, uppercase);
        if (abs_len == -1) return -1;

        // 总所需长度 = 负号(1) + 无符号部分长度
        int64_t req_len = 1 + abs_len;

        // 情况1.1：out为NULL，返回总所需长度
        if (out == NULL) return req_len;

        // 情况1.2：out非空，检查缓冲区是否足够
        if (max_out < req_len) return -1;

        // 填充负号 + 无符号部分
        out[0] = '-';
        int64_t write_len = XChar_from_unsigned_num((unsigned long long)(-value), base, &out[1], max_out - 1, uppercase);
        if (write_len == -1) return -1;

        // 有空间则加终止符
        if (max_out > req_len) out[req_len] = 0;

        return req_len;
    }

    // 情况2：非负数（直接复用无符号转换逻辑）
    return XChar_from_unsigned_num((unsigned long long)value, base, out, max_out, uppercase);
}

/**
 * @brief 辅助函数：计算无符号整数的十进制位数（无数学库依赖，100%精确）
 * @param num 待计算的无符号整数
 * @return size_t 十进制位数（如num=0返回1，num=123返回3）
 */
static size_t calc_uint_decimal_len(unsigned long long num) {
    if (num == 0) return 1;
    size_t len = 0;
    while (num > 0) {
        len++;
        num /= 10;  // 十进制右移，计数位数
    }
    return len;
}

/**
 * @brief 浮点数转UTF-16数组（核心辅助函数，修复长度计算异常问题）
 * @param value 待转换的浮点数
 * @param format 格式控制字符：'f'/'F'（固定点）、'e'/'E'（科学计数）、'g'/'G'（自动）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param precision 小数/有效数字精度（-1=自动：f=6位，e=5位，g=6位；最大限制为30）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
static int64_t XChar_from_floating_point(double value, char format, XChar* out, size_t max_out, int precision) {
    // --------------------------
    // 1. 入参合法性校验（增强版）
    // --------------------------
    // 格式校验：仅允许 f/F/e/E/g/G
    const bool is_valid_format = (format == 'f' || format == 'F' ||
        format == 'e' || format == 'E' ||
        format == 'g' || format == 'G');
    if (!is_valid_format) return -1;
    // out为NULL时，max_out必须为0（避免参数矛盾）
    if (out == NULL && max_out != 0) return -1;
    // 特殊值处理（NaN/无穷大，不受格式影响）
    if (isnan(value)) {  // 替代value != value，更明确（需<math.h>）
        const size_t nan_len = 3;
        if (out == NULL) return nan_len;
        if (max_out < nan_len) return -1;
        out[0] = 'N'; out[1] = 'a'; out[2] = 'N';
        if (max_out >= nan_len + 1) out[nan_len] = 0;
        return nan_len;
    }
    if (isinf(value)) {  // 替代value > DBL_MAX，更准确（需<math.h>）
        const size_t inf_len = (value > 0) ? 3 : 4;  // 正无穷3位，负无穷4位
        if (out == NULL) return inf_len;
        if (max_out < inf_len) return -1;
        if (value < 0) out[0] = '-';  // 负号
        const XChar inf_str[] = { 'I', 'n', 'f' };
        for (size_t i = 0; i < 3; i++) {
            out[(value < 0 ? 1 : 0) + i] = inf_str[i];
        }
        if (max_out >= inf_len + 1) out[inf_len] = 0;
        return inf_len;
    }

    // --------------------------
    // 2. 基础参数初始化（修复精度限制）
    // --------------------------
    const bool is_negative = (value < 0.0 && !isnan(value) && !isinf(value));  // 排除-0.0
    const double abs_val = is_negative ? -value : value;
    int final_prec = precision;
    const bool use_upper = (format == 'F' || format == 'E' || format == 'G');

    // 自动精度 + 精度最大值限制（避免长度溢出，double有效精度约15-17位）
    if (final_prec < 0) {
        if (format == 'f' || format == 'F') final_prec = 6;
        if (format == 'e' || format == 'E') final_prec = 5;
        if (format == 'g' || format == 'G') final_prec = 6;
    }
    const size_t MAX_PRECISION = 30;  // 限制最大精度（超过无意义）
    if (final_prec > (int)MAX_PRECISION) final_prec = (int)MAX_PRECISION;
    if (final_prec < 0) final_prec = 0;

    // --------------------------
    // 3. 按格式计算核心参数（修复整数位数计算）
    // --------------------------
    unsigned long long int_part = 0;
    double frac_part = 0.0;
    int exponent = 0;
    bool use_scientific = false;

    // 统一处理abs_val=0的情况（避免分支错误）
    if (abs_val == 0.0) {
        int_part = 0;
        frac_part = 0.0;
        exponent = 0;
        use_scientific = (format == 'e' || format == 'E');  // e格式强制科学计数
    }
    else {
        if (format == 'f' || format == 'F') {
            // 固定点：直接拆分整数/小数（修复int_part计算）
            int_part = (unsigned long long)abs_val;
            frac_part = abs_val - (double)int_part;  // 避免浮点数精度误差
        }
        else if (format == 'e' || format == 'E') {
            // 科学计数：标准化为 [1.0, 10.0) × 10^exponent（修复指数计算）
            use_scientific = true;
            exponent = (int)floor(log10(abs_val));  // 计算指数（如123.456→2）
            const double scaled_val = abs_val / pow(10.0, exponent);  // 标准化到[1,10)
            int_part = (unsigned long long)scaled_val;
            frac_part = scaled_val - (double)int_part;
        }
        else if (format == 'g' || format == 'G') {
            // 自动格式：判断阈值（|log10(abs_val)| ≥ final_prec 则用科学计数）
            const double log10_val = log10(abs_val);
            use_scientific = (fabs(log10_val) >= (double)final_prec || log10_val < -4.0);

            if (use_scientific) {
                // 按科学计数处理（同e格式）
                exponent = (int)floor(log10_val);
                const double scaled_val = abs_val / pow(10.0, exponent);
                int_part = (unsigned long long)scaled_val;
                frac_part = scaled_val - (double)int_part;
                final_prec = (final_prec > 0) ? (final_prec - 1) : 0;  // 有效数字-1（整数占1位）
            }
            else {
                // 按固定点处理（同f格式）
                int_part = (unsigned long long)abs_val;
                frac_part = abs_val - (double)int_part;
                // 精度=有效数字-整数位数（如123.456，有效数字4→小数位1）
                const size_t int_len = calc_uint_decimal_len(int_part);
                final_prec = (int)(final_prec - int_len);
                if (final_prec < 0) final_prec = 0;
            }
        }
    }

    // --------------------------
    // 4. 计算总所需长度（修复核心：精确计算各部分长度）
    // --------------------------
    const size_t sign_len = is_negative ? 1 : 0;  // 符号位（负1正0）
    const size_t int_len = calc_uint_decimal_len(int_part);  // 整数位数（无数学库依赖）
    size_t frac_len = 0;  // 小数部分长度（含小数点）
    size_t exp_len = 0;   // 指数部分长度（含e/E+符号+数字）

    // 计算小数长度（f/e格式：1+final_prec；g格式：去末尾0后长度）
    if (final_prec > 0) {
        frac_len = 1 + (size_t)final_prec;  // 基础长度：1个小数点 + final_prec个小数位

        // g格式特殊处理：去除末尾0和多余小数点
        if (format == 'g' || format == 'G') {
            // 模拟小数部分，找到最后一个非零数字位置
            double temp_frac = frac_part;
            size_t last_non_zero = 0;
            for (int i = 0; i < final_prec; i++) {
                temp_frac *= 10.0;
                const int digit = (int)temp_frac;
                if (digit != 0) last_non_zero = (size_t)i;  // 更新最后非零位置
                temp_frac -= (double)digit;
            }
            // 无有效小数位（如0.000）→ 去掉小数点
            if (last_non_zero == 0 && temp_frac < 0.5) {  // 四舍五入后仍为0
                frac_len = 0;
            }
            else {
                // 有效小数位：last_non_zero + 1（如0.1203→last_non_zero=3→4位小数）
                frac_len = 1 + (last_non_zero + 1);
            }
        }
    }

    // 计算指数长度（e/g格式：e/E + 符号 + 至少2位数字）
    if (use_scientific) {
        exp_len = 1 + 1 + 2;  // 1(e/E) + 1(±) + 2(数字，如02)
        // 指数绝对值≥100→数字部分加1位（如123→3位）
        if (abs(exponent) >= 100) exp_len++;
        // 指数为0→特殊处理（如1.23e00→无需额外长度）
        if (exponent == 0) exp_len = 1 + 1 + 2;  // 固定3位（e+00）
    }

    // 总长度 = 符号 + 整数 + 小数 + 指数（无溢出风险）
    const size_t req_len = sign_len + int_len + frac_len + exp_len;

    // out为NULL时，直接返回精确计算的长度（修复后不会异常大）
    if (out == NULL) {
        return (req_len <= (size_t)INT64_MAX) ? (int64_t)req_len : -1;  // 避免长度溢出int64_t
    }

    // --------------------------
    // 5. 填充输出缓冲区（原有逻辑保留，确保正确性）
    // --------------------------
    if (max_out < req_len) return -1;
    size_t pos = 0;

    // 5.1 填充符号位
    if (is_negative) out[pos++] = '-';

    // 5.2 填充整数部分（逆序计算→正序写入）
    if (int_part == 0) {
        out[pos++] = '0';
    }
    else {
        XChar int_buf[64] = { 0 };  // 足够存ULLONG_MAX（19位）
        size_t buf_idx = 0;
        unsigned long long temp_int = int_part;
        while (temp_int > 0) {
            int_buf[buf_idx++] = '0' + (int)(temp_int % 10);
            temp_int /= 10;
        }
        // 正序复制
        for (size_t i = 0; i < buf_idx; i++) {
            out[pos++] = int_buf[buf_idx - 1 - i];
        }
    }

    // 5.3 填充小数部分（含四舍五入）
    if (frac_len > 0) {
        out[pos++] = '.';
        double temp_frac = frac_part;
        const size_t actual_frac_len = frac_len - 1;  // 实际小数位数（不含小数点）
        int round_carry = 0;  // 四舍五入进位标记

        // 第一步：计算四舍五入是否需要进位
        for (size_t i = 0; i < actual_frac_len; i++) {
            temp_frac *= 10.0;
            const int digit = (int)temp_frac;
            if (i == actual_frac_len - 1) {
                // 最后一位：看小数点后一位是否≥5（需进位）
                const double next_frac = temp_frac - (double)digit;
                round_carry = (next_frac >= 0.5) ? 1 : 0;
            }
            temp_frac -= (double)digit;
        }

        // 第二步：填充小数位并处理进位
        temp_frac = frac_part;
        for (size_t i = 0; i < actual_frac_len; i++) {
            temp_frac *= 10.0;
            int digit = (int)temp_frac;
            // 最后一位加进位
            if (i == actual_frac_len - 1) digit += round_carry;

            // 处理进位（如digit=10→写0，前一位加1）
            if (digit >= 10) {
                digit = 0;
                // 回溯小数部分进位
                if (pos > sign_len + int_len) {
                    pos--;
                    while (pos > sign_len + int_len && out[pos] == '9') {
                        out[pos] = '0';
                        pos--;
                    }
                    out[pos] += 1;
                    pos++;
                }
            }

            out[pos++] = '0' + digit;
            temp_frac -= (double)digit;
        }
    }

    // 5.4 填充指数部分
    if (use_scientific) {
        // 填充e/E
        out[pos++] = use_upper ? 'E' : 'e';
        // 填充指数符号
        const bool exp_neg = (exponent < 0);
        out[pos++] = exp_neg ? '-' : '+';
        const int abs_exp = exp_neg ? -exponent : exponent;

        // 填充指数数字（确保至少2位，不足补前导0）
        XChar exp_buf[4] = { 0 };
        size_t exp_buf_idx = 0;
        int temp_exp = abs_exp;
        if (temp_exp == 0) {
            exp_buf[exp_buf_idx++] = '0';
        }
        else {
            while (temp_exp > 0) {
                exp_buf[exp_buf_idx++] = '0' + (temp_exp % 10);
                temp_exp /= 10;
            }
        }
        // 补前导0（如exp=5→05，exp=123→123）
        const size_t exp_digit_min = 2;
        while (exp_buf_idx < exp_digit_min) {
            exp_buf[exp_buf_idx++] = '0';
        }
        // 正序复制
        for (size_t i = 0; i < exp_buf_idx; i++) {
            out[pos++] = exp_buf[exp_buf_idx - 1 - i];
        }
    }

    // 6. 添加终止符（缓冲区足够时）
    if (max_out > req_len) {
        out[req_len] = 0;
    }

    return (int64_t)req_len;
}

// --------------------------
// 上层接口：XChar数组→数值类型
// --------------------------

/**
 * @brief UTF-16数组转short（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return short 转换结果，失败返回0
 */
short XChar_to_short_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    long long value = XChar_parse_signed(xchars, input_count, base, success);
    if (!*success) return 0;
    if (value < SHRT_MIN || value > SHRT_MAX) {
        if (success) *success = false;
        return 0;
    }
    return (short)value;
}

/**
 * @brief UTF-16数组转int（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return int 转换结果，失败返回0
 */
int XChar_to_int_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    long long value = XChar_parse_signed(xchars, input_count, base, success);
    if (!*success) return 0;
    if (value < INT_MIN || value > INT_MAX) {
        if (success) *success = false;
        return 0;
    }
    return (int)value;
}

/**
 * @brief UTF-16数组转long（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return long 转换结果，失败返回0
 */
long XChar_to_long_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    long long value = XChar_parse_signed(xchars, input_count, base, success);
    if (!*success) return 0;
    if (value < LONG_MIN || value > LONG_MAX) {
        if (success) *success = false;
        return 0;
    }
    return (long)value;
}

/**
 * @brief UTF-16数组转long long（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return long long 转换结果，失败返回0
 */
long long XChar_to_longlong_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    return XChar_parse_signed(xchars, input_count, base, success);
}

/**
 * @brief UTF-16数组转unsigned short（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return unsigned short 转换结果，失败返回0
 */
unsigned short XChar_to_ushort_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    unsigned long long value = XChar_parse_unsigned(xchars, input_count, base, success);
    if (!*success) return 0;
    if (value > USHRT_MAX) {
        if (success) *success = false;
        return 0;
    }
    return (unsigned short)value;
}

/**
 * @brief UTF-16数组转unsigned int（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return unsigned int 转换结果，失败返回0
 */
unsigned int XChar_to_uint_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    unsigned long long value = XChar_parse_unsigned(xchars, input_count, base, success);
    if (!*success) return 0;
    if (value > UINT_MAX) {
        if (success) *success = false;
        return 0;
    }
    return (unsigned int)value;
}

/**
 * @brief UTF-16数组转unsigned long（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return unsigned long 转换结果，失败返回0
 */
unsigned long XChar_to_ulong_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    unsigned long long value = XChar_parse_unsigned(xchars, input_count, base, success);
    if (!*success) return 0;
    if (value > ULONG_MAX) {
        if (success) *success = false;
        return 0;
    }
    return (unsigned long)value;
}

/**
 * @brief UTF-16数组转unsigned long long（支持2-36进制）
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param base 进制（2-36）
 * @param success 输出参数：true=转换成功，false=失败（溢出/无效格式）
 * @return unsigned long long 转换结果，失败返回0
 */
unsigned long long XChar_to_ulonglong_stream(const XChar* xchars, size_t input_count, int base, bool* success) {
    return XChar_parse_unsigned(xchars, input_count, base, success);
}

/**
 * @brief UTF-16数组转float
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param success 输出参数：true=转换成功，false=失败（无效格式）
 * @return float 转换结果，失败返回0.0f
 */
float XChar_to_float_stream(const XChar* xchars, size_t input_count, bool* success) {
    double value = XChar_parse_float(xchars, input_count, success);
    if (!*success) return 0.0f;
    if (value > FLT_MAX || value < -FLT_MAX) {
        if (success) *success = false;
        return 0.0f;
    }
    return (float)value;
}

/**
 * @brief UTF-16数组转double
 * @param xchars 输入的UTF-16数组
 * @param input_count 输入缓冲区大小（0自动查终止符）
 * @param success 输出参数：true=转换成功，false=失败（无效格式）
 * @return double 转换结果，失败返回0.0
 */
double XChar_to_double_stream(const XChar* xchars, size_t input_count, bool* success) {
    return XChar_parse_float(xchars, input_count, success);
}


// --------------------------
// 上层接口：数值类型→UTF-16数组
// --------------------------

/**
 * @brief short转UTF-16数组（支持2-36进制）
 * @param value 待转换的short值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_short_stream(short value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_signed_num((long long)value, base, out, max_out, uppercase);
}

/**
 * @brief int转UTF-16数组（支持2-36进制）
 * @param value 待转换的int值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_int_stream(int value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_signed_num((long long)value, base, out, max_out, uppercase);
}

/**
 * @brief long转UTF-16数组（支持2-36进制）
 * @param value 待转换的long值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_long_stream(long value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_signed_num((long long)value, base, out, max_out, uppercase);
}

/**
 * @brief long long转UTF-16数组（支持2-36进制）
 * @param value 待转换的long long值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_longlong_stream(long long value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_signed_num(value, base, out, max_out, uppercase);
}

/**
 * @brief unsigned short转UTF-16数组（支持2-36进制）
 * @param value 待转换的unsigned short值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_ushort_stream(unsigned short value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_unsigned_num((unsigned long long)value, base, out, max_out, uppercase);
}

/**
 * @brief unsigned int转UTF-16数组（支持2-36进制）
 * @param value 待转换的unsigned int值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_uint_stream(unsigned int value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_unsigned_num((unsigned long long)value, base, out, max_out, uppercase);
}

/**
 * @brief unsigned long转UTF-16数组（支持2-36进制）
 * @param value 待转换的unsigned long值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_ulong_stream(unsigned long value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_unsigned_num((unsigned long long)value, base, out, max_out, uppercase);
}

/**
 * @brief unsigned long long转UTF-16数组（支持2-36进制）
 * @param value 待转换的unsigned long long值
 * @param base 进制（2-36）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param uppercase 字母是否大写（16-36进制时生效）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_ulonglong_stream(unsigned long long value, int base, XChar* out, size_t max_out, bool uppercase) {
    return XChar_from_unsigned_num(value, base, out, max_out, uppercase);
}

/**
 * @brief float转UTF-16数组（支持多格式）
 * @param value 待转换的float值
 * @param format 格式控制字符：'f'/'F'（固定点）、'e'/'E'（科学计数）、'g'/'G'（自动）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param precision 小数/有效数字精度（-1=自动：f=6位，e=5位，g=6位）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_float_stream(float value, char format, XChar* out, size_t max_out, int precision) {
    return XChar_from_floating_point((double)value, format, out, max_out, precision);
}

/**
 * @brief double转UTF-16数组（支持多格式）
 * @param value 待转换的double值
 * @param format 格式控制字符：'f'/'F'（固定点）、'e'/'E'（科学计数）、'g'/'G'（自动）
 * @param out 输出缓冲区（NULL时返回所需长度，不含终止符）
 * @param max_out 输出缓冲区最大容量（XChar数量）
 * @param precision 小数/有效数字精度（-1=自动：f=6位，e=5位，g=6位）
 * @return int64_t 成功：out非空返回实际写入长度，out空返回所需长度；失败返回-1
 */
int64_t XChar_from_double_stream(double value, char format, XChar* out, size_t max_out, int precision) {
    return XChar_from_floating_point(value, format, out, max_out, precision);
}