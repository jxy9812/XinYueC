#include "XChar.h"
#include "XMemory.h"
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
        ch.code = (uint16_t)unicode;
        return ch;
    }

    // 补充平面字符（生成高代理）
    unicode -= SURROGATE_OFFSET;
    ch.code = (uint16_t)(UTF16_HIGH_SURROGATE_START + (unicode >> 10));
    return ch;
}

// 创建补充平面字符的低代理
XChar XChar_from_unicode_low(uint32_t unicode) {
    XChar ch = { 0 };
    if (unicode < 0x10000 || unicode > UNICODE_MAX_CODEPOINT) return ch;

    unicode -= SURROGATE_OFFSET;
    ch.code = (uint16_t)(UTF16_LOW_SURROGATE_START + (unicode & 0x3FF));
    return ch;
}

// 获取Unicode码点（单字符/高代理）
uint32_t XChar_unicode(const XChar* ch) {
    if (!ch) return 0;
    return ch->code;
}

// 判断是否为字母（扩展多语言支持）
bool XChar_is_letter(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
bool XChar_is_digit(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
int XChar_digit_value(const XChar* ch) {
    if (!ch || !XChar_is_digit(ch)) return -1;
    uint16_t code = ch->code;

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
bool XChar_is_space(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
bool XChar_is_punct(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
bool XChar_is_lower(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
bool XChar_is_upper(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
XChar XChar_to_upper(const XChar* ch) {
    if (!ch) return (XChar) { 0 }; // 空指针安全处理
    XChar result = *ch;
    if (!XChar_is_lower(ch)) return result;

    // ASCII小写转大写
    if (ch->code >= 'a' && ch->code <= 'z') {
        result.code -= 32;
        return result;
    }

    // 特殊字符映射
    switch (ch->code) {
        // 拉丁扩展
    case 0x00DF: result.code = 0x1E9E; break; // ß -> ẞ
    case 0x00E0: result.code = 0x00C0; break; // à -> À
    case 0x00E1: result.code = 0x00C1; break; // á -> Á
    case 0x00E2: result.code = 0x00C2; break; // â -> Â
    case 0x00E3: result.code = 0x00C3; break; // ã -> Ã
    case 0x00E4: result.code = 0x00C4; break; // ä -> Ä
    case 0x00E5: result.code = 0x00C5; break; // å -> Å
    case 0x00E6: result.code = 0x00C6; break; // æ -> Æ
    case 0x00E7: result.code = 0x00C7; break; // ç -> Ç
    case 0x00E8: result.code = 0x00C8; break; // è -> È
    case 0x00E9: result.code = 0x00C9; break; // é -> É
    case 0x00EA: result.code = 0x00CA; break; // ê -> Ê
    case 0x00EB: result.code = 0x00CB; break; // ë -> Ë
    case 0x00EC: result.code = 0x00CC; break; // ì -> Ì
    case 0x00ED: result.code = 0x00CD; break; // í -> Í
    case 0x00EE: result.code = 0x00CE; break; // î -> Î
    case 0x00EF: result.code = 0x00CF; break; // ï -> Ï
    case 0x00F0: result.code = 0x00D0; break; // ð -> Ð
    case 0x00F1: result.code = 0x00D1; break; // ñ -> Ñ
    case 0x00F2: result.code = 0x00D2; break; // ò -> Ò
    case 0x00F3: result.code = 0x00D3; break; // ó -> Ó
    case 0x00F4: result.code = 0x00D4; break; // ô -> Ô
    case 0x00F5: result.code = 0x00D5; break; // õ -> Õ
    case 0x00F6: result.code = 0x00D6; break; // ö -> Ö
    case 0x00F8: result.code = 0x00D8; break; // ø -> Ø
    case 0x00F9: result.code = 0x00D9; break; // ù -> Ù
    case 0x00FA: result.code = 0x00DA; break; // ú -> Ú
    case 0x00FB: result.code = 0x00DB; break; // û -> Û
    case 0x00FC: result.code = 0x00DC; break; // ü -> Ü
    case 0x00FD: result.code = 0x00DD; break; // ý -> Ý
    case 0x00FE: result.code = 0x00DE; break; // þ -> Þ

        // 希腊字母
    case 0x03B1: result.code = 0x0391; break; // α -> Α
    case 0x03B2: result.code = 0x0392; break; // β -> Β
    case 0x03B3: result.code = 0x0393; break; // γ -> Γ
    case 0x03B4: result.code = 0x0394; break; // δ -> Δ
    case 0x03B5: result.code = 0x0395; break; // ε -> Ε
    case 0x03B6: result.code = 0x0396; break; // ζ -> Ζ
    case 0x03B7: result.code = 0x0397; break; // η -> Η
    case 0x03B8: result.code = 0x0398; break; // θ -> Θ
    case 0x03B9: result.code = 0x0399; break; // ι -> Ι
    case 0x03BA: result.code = 0x039A; break; // κ -> Κ
    case 0x03BB: result.code = 0x039B; break; // λ -> Λ
    case 0x03BC: result.code = 0x039C; break; // μ -> Μ
    case 0x03BD: result.code = 0x039D; break; // ν -> Ν
    case 0x03BE: result.code = 0x039E; break; // ξ -> Ξ
    case 0x03BF: result.code = 0x039F; break; // ο -> Ο
    case 0x03C0: result.code = 0x03A0; break; // π -> Π
    case 0x03C1: result.code = 0x03A1; break; // ρ -> Ρ
    case 0x03C2: case 0x03C3: result.code = 0x03A3; break; // ς/σ -> Σ
    case 0x03C4: result.code = 0x03A4; break; // τ -> Τ
    case 0x03C5: result.code = 0x03A5; break; // υ -> Υ
    case 0x03C6: result.code = 0x03A6; break; // φ -> Φ
    case 0x03C7: result.code = 0x03A7; break; // χ -> Χ
    case 0x03C8: result.code = 0x03A8; break; // ψ -> Ψ
    case 0x03C9: result.code = 0x03A9; break; // ω -> Ω

        // 西里尔字母
    case 0x0430: result.code = 0x0410; break; // а -> А
    case 0x0431: result.code = 0x0411; break; // б -> Б
    case 0x0432: result.code = 0x0412; break; // в -> В
    case 0x0433: result.code = 0x0413; break; // г -> Г
    case 0x0434: result.code = 0x0414; break; // д -> Д
    case 0x0435: result.code = 0x0415; break; // е -> Е
    case 0x0436: result.code = 0x0416; break; // ж -> Ж
    case 0x0437: result.code = 0x0417; break; // з -> З
    case 0x0438: result.code = 0x0418; break; // и -> И
    case 0x0439: result.code = 0x0419; break; // й -> Й
    case 0x043a: result.code = 0x041a; break; // к -> К
    case 0x043b: result.code = 0x041b; break; // л -> Л
    case 0x043c: result.code = 0x041c; break; // м -> М
    case 0x043d: result.code = 0x041d; break; // н -> Н
    case 0x043e: result.code = 0x041e; break; // о -> О
    case 0x043f: result.code = 0x041f; break; // п -> П
    case 0x0440: result.code = 0x0420; break; // р -> Р
    case 0x0441: result.code = 0x0421; break; // с -> С
    case 0x0442: result.code = 0x0422; break; // т -> Т
    case 0x0443: result.code = 0x0423; break; // у -> У
    case 0x0444: result.code = 0x0424; break; // ф -> Ф
    case 0x0445: result.code = 0x0425; break; // х -> Х
    case 0x0446: result.code = 0x0426; break; // ц -> Ц
    case 0x0447: result.code = 0x0427; break; // ч -> Ч
    case 0x0448: result.code = 0x0428; break; // ш -> Ш
    case 0x0449: result.code = 0x0429; break; // щ -> Щ
    case 0x044a: result.code = 0x042a; break; // ъ -> Ъ
    case 0x044b: result.code = 0x042b; break; // ы -> Ы
    case 0x044c: result.code = 0x042c; break; // ь -> Ь
    case 0x044d: result.code = 0x042d; break; // э -> Э
    case 0x044e: result.code = 0x042e; break; // ю -> Ю
    case 0x044f: result.code = 0x042f; break; // я -> Я
    }

    return result;
}

// 转换为小写字母
XChar XChar_to_lower(const XChar* ch) {
    if (!ch) return (XChar) { 0 }; // 空指针安全处理
    XChar result = *ch;
    if (!XChar_is_upper(ch)) return result;

    // ASCII大写转小写
    if (ch->code >= 'A' && ch->code <= 'Z') {
        result.code += 32;
        return result;
    }

    // 特殊字符映射
    switch (ch->code) {
        // 拉丁扩展
    case 0x1E9E: result.code = 0x00DF; break; // ẞ -> ß
    case 0x00C0: result.code = 0x00E0; break; // À -> à
    case 0x00C1: result.code = 0x00E1; break; // Á -> á
    case 0x00C2: result.code = 0x00E2; break; // Â -> â
    case 0x00C3: result.code = 0x00E3; break; // Ã -> ã
    case 0x00C4: result.code = 0x00E4; break; // Ä -> ä
    case 0x00C5: result.code = 0x00E5; break; // Å -> å
    case 0x00C6: result.code = 0x00E6; break; // Æ -> æ
    case 0x00C7: result.code = 0x00E7; break; // Ç -> ç
    case 0x00C8: result.code = 0x00E8; break; // È -> è
    case 0x00C9: result.code = 0x00E9; break; // É -> é
    case 0x00CA: result.code = 0x00EA; break; // Ê -> ê
    case 0x00CB: result.code = 0x00EB; break; // Ë -> ë
    case 0x00CC: result.code = 0x00EC; break; // Ì -> ì
    case 0x00CD: result.code = 0x00ED; break; // Í -> í
    case 0x00CE: result.code = 0x00EE; break; // Î -> î
    case 0x00CF: result.code = 0x00EF; break; // Ï -> ï
    case 0x00D0: result.code = 0x00F0; break; // Ð -> ð
    case 0x00D1: result.code = 0x00F1; break; // Ñ -> ñ
    case 0x00D2: result.code = 0x00F2; break; // Ò -> ò
    case 0x00D3: result.code = 0x00F3; break; // Ó -> ó
    case 0x00D4: result.code = 0x00F4; break; // Ô -> ô
    case 0x00D5: result.code = 0x00F5; break; // Õ -> õ
    case 0x00D6: result.code = 0x00F6; break; // Ö -> ö
    case 0x00D8: result.code = 0x00F8; break; // Ø -> ø
    case 0x00D9: result.code = 0x00F9; break; // Ù -> ù
    case 0x00DA: result.code = 0x00FA; break; // Ú -> ú
    case 0x00DB: result.code = 0x00FB; break; // Û -> û
    case 0x00DC: result.code = 0x00FC; break; // Ü -> ü
    case 0x00DD: result.code = 0x00FD; break; // Ý -> ý
    case 0x00DE: result.code = 0x00FE; break; // Þ -> þ

        // 希腊字母
    case 0x0391: result.code = 0x03B1; break; // Α -> α
    case 0x0392: result.code = 0x03B2; break; // Β -> β
    case 0x0393: result.code = 0x03B3; break; // Γ -> γ
    case 0x0394: result.code = 0x03B4; break; // Δ -> δ
    case 0x0395: result.code = 0x03B5; break; // Ε -> ε
    case 0x0396: result.code = 0x03B6; break; // Ζ -> ζ
    case 0x0397: result.code = 0x03B7; break; // Η -> η
    case 0x0398: result.code = 0x03B8; break; // Θ -> θ
    case 0x0399: result.code = 0x03B9; break; // Ι -> ι
    case 0x039A: result.code = 0x03BA; break; // Κ -> κ
    case 0x039B: result.code = 0x03BB; break; // Λ -> λ
    case 0x039C: result.code = 0x03BC; break; // Μ -> μ
    case 0x039D: result.code = 0x03BD; break; // Ν -> ν
    case 0x039E: result.code = 0x03BE; break; // Ξ -> ξ
    case 0x039F: result.code = 0x03BF; break; // Ο -> ο
    case 0x03A0: result.code = 0x03C0; break; // Π -> π
    case 0x03A1: result.code = 0x03C1; break; // Ρ -> ρ
    case 0x03A3: result.code = 0x03C3; break; // Σ -> σ
    case 0x03A4: result.code = 0x03C4; break; // Τ -> τ
    case 0x03A5: result.code = 0x03C5; break; // Υ -> υ
    case 0x03A6: result.code = 0x03C6; break; // Φ -> φ
    case 0x03A7: result.code = 0x03C7; break; // Χ -> χ
    case 0x03A8: result.code = 0x03C8; break; // Ψ -> ψ
    case 0x03A9: result.code = 0x03C9; break; // Ω -> ω

        // 西里尔字母
    case 0x0410: result.code = 0x0430; break; // А -> а
    case 0x0411: result.code = 0x0431; break; // Б -> б
    case 0x0412: result.code = 0x0432; break; // В -> в
    case 0x0413: result.code = 0x0433; break; // Г -> г
    case 0x0414: result.code = 0x0434; break; // Д -> д
    case 0x0415: result.code = 0x0435; break; // Е -> е
    case 0x0416: result.code = 0x0436; break; // Ж -> ж
    case 0x0417: result.code = 0x0437; break; // З -> з
    case 0x0418: result.code = 0x0438; break; // И -> и
    case 0x0419: result.code = 0x0439; break; // Й -> й
    case 0x041A: result.code = 0x043A; break; // К -> к
    case 0x041B: result.code = 0x043B; break; // Л -> л
    case 0x041C: result.code = 0x043C; break; // М -> м
    case 0x041D: result.code = 0x043D; break; // Н -> н
    case 0x041E: result.code = 0x043E; break; // О -> о
    case 0x041F: result.code = 0x043F; break; // П -> п
    case 0x0420: result.code = 0x0440; break; // Р -> р
    case 0x0421: result.code = 0x0441; break; // С -> с
    case 0x0422: result.code = 0x0442; break; // Т -> т
    case 0x0423: result.code = 0x0443; break; // У -> у
    case 0x0424: result.code = 0x0444; break; // Ф -> ф
    case 0x0425: result.code = 0x0445; break; // Х -> х
    case 0x0426: result.code = 0x0446; break; // Ц -> ц
    case 0x0427: result.code = 0x0447; break; // Ч -> ч
    case 0x0428: result.code = 0x0448; break; // Ш -> ш
    case 0x0429: result.code = 0x0449; break; // Щ -> щ
    case 0x042A: result.code = 0x044A; break; // Ъ -> ъ
    case 0x042B: result.code = 0x044B; break; // Ы -> ы
    case 0x042C: result.code = 0x044C; break; // Ь -> ь
    case 0x042D: result.code = 0x044D; break; // Э -> э
    case 0x042E: result.code = 0x044E; break; // Ю -> ю
    case 0x042F: result.code = 0x044F; break; // Я -> я
    }

    return result;
}

// 新增：判断控制字符
bool XChar_is_control(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;
    return (code >= 0x0000 && code <= 0x001F) ||  // C0控制字符
        (code >= 0x007F && code <= 0x009F);    // C1控制字符
}

// 新增：判断符号字符
bool XChar_is_symbol(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

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
bool XChar_is_emoji(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;

    // 表情符号主范围
    if ((code >= 0x1F600 && code <= 0x1F64F) ||  // 情感表情
        (code >= 0x1F300 && code <= 0x1F5FF) ||  // 符号与象形图
        (code >= 0x1F680 && code <= 0x1F6FF) ||  // 交通与地图
        (code >= 0x1F1E0 && code <= 0x1F1FF))    // 国旗 emoji
        return true;

    return false;
}

// 新增：判断全角字符
bool XChar_is_fullwidth(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;
    // 全角ASCII范围、中文、日文、韩文等
    return (code >= 0xFF01 && code <= 0xFFEF) ||
        (code >= 0x4E00 && code <= 0x9FFF) ||
        (code >= 0x3040 && code <= 0x30FF) ||
        (code >= 0xAC00 && code <= 0xD7AF);
}

// 新增：判断半角字符
bool XChar_is_halfwidth(const XChar* ch) {
    if (!ch) return false;
    uint16_t code = ch->code;
    // 基本ASCII和半角符号
    return (code >= 0x0020 && code <= 0x007E) ||
        (code >= 0xFF61 && code <= 0xFF9F);  // 半角片假名
}

// 新增：半角转全角
XChar XChar_to_fullwidth(const XChar* ch) {
    if (!ch) return (XChar) { 0 };
    XChar result = *ch;
    uint16_t code = ch->code;

    // 半角ASCII转全角
    if (code >= 0x20 && code <= 0x7E) {
        result.code = code + 0xFEE0;
        return result;
    }

    // 半角片假名转全角
    if (code >= 0xFF61 && code <= 0xFF9F) {
        result.code = code + 0x0040;
        return result;
    }

    return result;
}

// 新增：全角转半角
XChar XChar_to_halfwidth(const XChar* ch) {
    if (!ch) return (XChar) { 0 };
    XChar result = *ch;
    uint16_t code = ch->code;

    // 全角ASCII转半角
    if (code >= 0xFF01 && code <= 0xFF5E) {
        result.code = code - 0xFEE0;
        return result;
    }

    // 全角片假名转半角
    if (code >= 0xFFA1 && code <= 0xFFDF) {
        result.code = code - 0x0040;
        return result;
    }

    return result;
}

// 代理对相关判断
bool XChar_is_high_surrogate(const XChar* ch) {
    return ch && (ch->code >= UTF16_HIGH_SURROGATE_START &&
        ch->code <= UTF16_HIGH_SURROGATE_END);
}

bool XChar_is_low_surrogate(const XChar* ch) {
    return ch && (ch->code >= UTF16_LOW_SURROGATE_START &&
        ch->code <= UTF16_LOW_SURROGATE_END);
}

bool XChar_is_surrogate(const XChar* ch) {
    return XChar_is_high_surrogate(ch) || XChar_is_low_surrogate(ch);
}

// 从代理对获取完整Unicode码点
uint32_t XChar_surrogate_to_unicode(const XChar* high, const XChar* low) {
    if (!high || !low || !XChar_is_high_surrogate(high) || !XChar_is_low_surrogate(low)) {
        return 0;
    }
    uint32_t high_val = high->code - UTF16_HIGH_SURROGATE_START;
    uint32_t low_val = low->code - UTF16_LOW_SURROGATE_START;
    return (high_val << 10) + low_val + SURROGATE_OFFSET;
}

// 字符比较
bool XEquality_XChar(const XChar* a, const XChar* b) {
    if (!a || !b) return false; // 空指针比较为false
    return a->code == b->code;
}

bool XChar_equals(const XChar* a, const XChar* b, XCharCaseSensitivity cs)
{
    if (cs == XCharCaseSensitive) {
        return a->code == b->code;
    }
    else {
        XChar a_lower = XChar_to_lower(a);
        XChar b_lower = XChar_to_lower(b);
        return a_lower.code == b_lower.code;
    }
}

int XChar_compare(const XChar* a, const XChar* b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    return (a->code > b->code) ? 1 : (a->code < b->code ? -1 : 0);
}



// --------------------------
// UTF-8 转换实现
// --------------------------
int64_t XChar_from_utf8(const uint8_t* utf8, XChar* out, size_t max_out)
{
    if (!utf8) return -1;

    size_t count = 0;
    const uint8_t* p = utf8;
    bool calculate_only = (out == NULL);

    while (*p != '\0') {
        uint32_t unicode = 0;
        int bytes = 0;
        //// 在解析前打印当前字节（仅调试用）
        //printf("当前字节: 0x%02X, 下1字节: 0x%02X, 下2字节: 0x%02X\n",
        //    (unsigned int)*p,
        //    (unsigned int)(p[1] & 0xFF),
        //    (unsigned int)(p[2] & 0xFF));

        if ((*p & 0x80) == 0) {
            // 1字节：0xxxxxxx
            unicode = *p;
            bytes = 1;
        }
        else if ((*p & 0xE0) == 0xC0) {
            // 2字节：110xxxxx 10xxxxxx
            if (p[1] == '\0') return -1; // 不完整序列
            unicode = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            // 检查是否为有效的2字节编码（避免过度长编码）
            if (unicode < 0x80) return -1;
            bytes = 2;
        }
        else if ((*p & 0xF0) == 0xE0) {
            // 3字节：1110xxxx 10xxxxxx 10xxxxxx（中文常用）
            if (p[1] == '\0' || p[2] == '\0') return -1;
            unicode = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            // 检查有效性（0x800 ~ 0xFFFF）
            if (unicode < 0x800) return -1;
            bytes = 3;
        }
        else if ((*p & 0xF8) == 0xF0) {
            // 4字节：11110xxx ...
            if (p[1] == '\0' || p[2] == '\0' || p[3] == '\0') return -1;
            unicode = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            if (unicode < 0x10000 || unicode > 0x10FFFF) return -1;
            bytes = 4;
        }
        else 
        {
            p++;
            continue;
            //return -1; // 无效UTF-8前缀（如10xxxxxx开头的字节）
        }

        // 计算所需XChar数量
        size_t needed = (unicode <= 0xFFFF) ? 1 : 2;

        if (!calculate_only) {
            if (count + needed > max_out) return -1; // 空间不足
            // 写入XChar（处理代理对）
            if (unicode <= 0xFFFF) {
                out[count].code = (uint16_t)unicode;
                count++;
            }
            else {
                unicode -= 0x10000;
                out[count].code = 0xD800 | (unicode >> 10);
                out[count + 1].code = 0xDC00 | (unicode & 0x3FF);
                count += 2;
            }
        }
        else {
            count += needed;
        }

        p += bytes;
    }

    // 终止符处理
    if (!calculate_only && count < max_out) 
    {
        out[count].code = 0;
    }

    return count;
}

int64_t XChar_to_utf8(const XChar* ch, uint8_t* utf8, size_t max_utf8)
{
    if (!ch) return -1; // 输入字符为NULL直接返回错误

    size_t total_bytes = 0; // 总需字节数（不包含终止符）
    const XChar* p = ch;

    while (p->code != 0) {
        uint32_t unicode;
        size_t bytes_needed;

        // 处理代理对
        if (XChar_is_high_surrogate(p) && XChar_is_low_surrogate(p + 1)) {
            unicode = XChar_surrogate_to_unicode(p, p + 1);
            p += 2; // 跳过低代理
        }
        else {
            unicode = XChar_unicode(p);
            p++;
        }

        // 计算当前字符所需的UTF-8字节数
        if (unicode <= 0x7F) {
            bytes_needed = 1;
        }
        else if (unicode <= 0x7FF) {
            bytes_needed = 2;
        }
        else if (unicode <= 0xFFFF) {
            bytes_needed = 3;
        }
        else if (unicode <= 0x10FFFF) {
            bytes_needed = 4;
        }
        else {
            return -1; // 无效Unicode码点
        }

        // 如果是计算长度模式，累加总字节数
        if (!utf8) {
            total_bytes += bytes_needed;
        }
        else {
            // 实际写入模式：检查空间是否足够
            if (total_bytes + bytes_needed > max_utf8) {
                return -1;
            }

            // 写入UTF-8字节序列
            if (unicode <= 0x7F) {
                utf8[total_bytes++] = (uint8_t)unicode;
            }
            else if (unicode <= 0x7FF) {
                utf8[total_bytes++] = 0xC0 | (unicode >> 6);
                utf8[total_bytes++] = 0x80 | (unicode & 0x3F);
            }
            else if (unicode <= 0xFFFF) {
                utf8[total_bytes++] = 0xE0 | (unicode >> 12);
                utf8[total_bytes++] = 0x80 | ((unicode >> 6) & 0x3F);
                utf8[total_bytes++] = 0x80 | (unicode & 0x3F);
            }
            else if (unicode <= 0x10FFFF) {
                utf8[total_bytes++] = 0xF0 | (unicode >> 18);
                utf8[total_bytes++] = 0x80 | ((unicode >> 12) & 0x3F);
                utf8[total_bytes++] = 0x80 | ((unicode >> 6) & 0x3F);
                utf8[total_bytes++] = 0x80 | (unicode & 0x3F);
            }
        }
    }

    // 处理终止符（'\0'）
    if (!utf8) 
    {
        //total_bytes += 1; // 额外加1字节存放终止符
        return (int)total_bytes;
    }
    else 
    {
        // 确保有空间写入终止符
        if (total_bytes < max_utf8) 
        {
            utf8[total_bytes] = '\0';
        }
        return total_bytes;
    }
}

int64_t XChar_from_utf16(const uint16_t* utf16_str, XChar* out_xchars, size_t max_count)
{
    if (!utf16_str) return -1;

    int64_t xchar_count = 0;
    const uint16_t* current = utf16_str;

    // 第一遍：计算所需XChar数量
    while (*current != L'\0') {
        uint16_t code = (uint16_t)*current;
        // 检测高代理（需要与低代理组合）
        if (code >= UTF16_HIGH_SURROGATE_START && code <= UTF16_HIGH_SURROGATE_END) {
            // 检查下一个字符是否为低代理
            if (*(current + 1) != L'\0' &&
                (uint16_t) * (current + 1) >= UTF16_LOW_SURROGATE_START &&
                (uint16_t) * (current + 1) <= UTF16_LOW_SURROGATE_END) {
                xchar_count += 2;  // 高+低代理对需要2个XChar
                current += 2;      // 跳过低代理
                continue;
            }
        }
        // 普通字符或单个代理（视为无效）
        xchar_count++;
        current++;
    }

    // 仅计算长度时直接返回
    if (!out_xchars) return xchar_count;

    // 检查缓冲区大小是否足够（包含终止符）
    if (max_count < (size_t)xchar_count + 1) return -1;

    // 第二遍：实际转换
    current = utf16_str;
    XChar* out = out_xchars;
    while (*current != L'\0') {
        uint16_t code = (uint16_t)*current;
        // 处理代理对
        if (code >= UTF16_HIGH_SURROGATE_START && code <= UTF16_HIGH_SURROGATE_END) {
            if (*(current + 1) != L'\0' &&
                (uint16_t) * (current + 1) >= UTF16_LOW_SURROGATE_START &&
                (uint16_t) * (current + 1) <= UTF16_LOW_SURROGATE_END) {
                // 高代理转换为第一个XChar
                *out++ = (XChar){ code };
                // 低代理转换为第二个XChar
                *out++ = (XChar){ (uint16_t) * (current + 1) };
                current += 2;
                continue;
            }
        }
        // 处理普通字符
        *out++ = (XChar){ code };
        current++;
    }

    // 添加终止符
    *out = (XChar){ 0 };
    return xchar_count;
}

// 将XChar数组转换为UTF-16编码
// 参数：
//   xchars - 输入的XChar数组（以空字符结尾）
//   out_buf - 输出缓冲区（NULL时仅计算所需长度）
//   buf_size - 输出缓冲区缓冲区大小大小（以uint16_t为单位）
// 返回值：
//   成功时返回写入的uint16_t数量（不含终止符），失败时返回-1
int64_t XChar_to_utf16(const XChar* xchars, uint16_t* out_buf, size_t buf_size) 
{
    if (!xchars) return -1;

    // 第一遍：计算有效长度（不含终止符）
    int64_t len = 0;
    const XChar* current = xchars;
    while (current->code != 0) {  // XChar数组以0为终止符
        len++;
        current++;
    }

    // 仅计算长度时直接返回
    if (!out_buf) return len;

    // 检查缓冲区是否足够（包含终止符）
    if (buf_size < (size_t)len + 1) return -1;

    // 第二遍：直接复制（XChar.code本身就是UTF-16代码单元）
    current = xchars;
    uint16_t* out = out_buf;
    while (current->code != 0) {
        *out++ = current->code;  // 直接复制uint16_t值
        current++;
    }
    *out = L'\0';  // 添加终止符

    return len;
}

// --------------------------
// UTF-32 转换实现（支持输出为NULL时计算所需大小）
// --------------------------
int64_t XChar_from_utf32(const uint32_t* utf32, XChar* out, size_t max_out)
{
    if (!utf32) return -1; // 输入为NULL直接返回错误

    size_t count = 0;
    const uint32_t* p = utf32;
    bool calculate_only = (out == NULL);

    while (*p != 0)
    {
        uint32_t code = *p++;

        if (code > 0x10FFFF) return -1; // 无效码点

        // 计算当前码点需要的XChar数量
        size_t needed = (code <= 0xFFFF) ? 1 : 2;

        // 仅计算大小时累加计数，不检查max_out（避免限制）
        if (calculate_only)
        {
            count += needed;
        }
        // 实际写入时需检查空间是否足够
        else
        {
            if (count + needed > max_out) return -1;

            if (code <= 0xFFFF)
            {
                // 基本平面，直接存储
                out[count].code = (uint16_t)code;
                count++;
            }
            else
            {
                // 补充平面，转换为代理对
                code -= 0x10000;
                out[count].code = 0xD800 | (code >> 10);       // 高代理
                out[count + 1].code = 0xDC00 | (code & 0x3FF); // 低代理
                count += 2;
            }
        }
    }

    // 实际写入时添加终止符（不占用返回的count）
    if (!calculate_only && count < max_out)
    {
        out[count].code = 0;
    }

    // 返回值：仅为有效字符所需的XChar数量（不含终止符）
    return count;
}

int64_t XChar_to_utf32(const XChar* ch, uint32_t* utf32, size_t max_utf32)
{
    if (!ch) return -1; // 输入为NULL直接返回错误

    size_t count = 0;
    const XChar* p = ch;
    bool calculate_only = (utf32 == NULL);

    while (p->code != 0)
    {
        // 计算当前XChar序列对应的UTF-32码点数量（恒为1）
        size_t needed = 1;

        // 仅计算大小时累加计数
        if (calculate_only)
        {
            count += needed;
        }
        // 实际写入时需检查空间是否足够
        else
        {
            if (count + needed > max_utf32) return -1;
        }

        // 解析Unicode码点（无论是否计算模式，都需要推进指针）
        if (XChar_is_high_surrogate(p) && XChar_is_low_surrogate(p + 1))
        {
            if (!calculate_only)
            {
                utf32[count] = XChar_surrogate_to_unicode(p, p + 1);
            }
            p += 2; // 跳过低代理
        }
        else
        {
            if (!calculate_only)
            {
                utf32[count] = XChar_unicode(p);
            }
            p++;
        }

        // 实际写入时才累加计数（与计算模式保持一致）
        if (!calculate_only)
        {
            count++;
        }
    }

    // 实际写入时添加终止符（不占用返回的count）
    if (!calculate_only && count < max_utf32)
    {
        utf32[count] = 0;
    }

    // 返回值：仅为有效字符所需的uint32_t数量（不含终止符）
    return count;
}

// --------------------------
// Latin-1 转 XChar（支持输出为NULL时计算大小）
// --------------------------
int64_t XChar_from_latin1(const uint8_t* latin1, XChar* out, size_t max_out)
{
    if (!latin1) return -1; // 输入为NULL直接返回错误

    size_t count = 0;
    const uint8_t* p = latin1;
    bool calculate_only = (out == NULL);

    while (*p != '\0') 
    {
        // 每个Latin-1字符对应1个XChar
        size_t needed = 1;

        if (calculate_only) 
        {
            // 仅计算大小：累加计数（不检查max_out）
            count += needed;
        }
        else 
        {
            // 实际写入：检查空间是否充足（预留终止符位置）
            if (count + needed >= max_out) return -1;
            out[count].code = (uint16_t)*p; // 直接映射（0x00-0xFF）
            count += needed;
        }

        p++;
    }

    // 实际写入时添加终止符（不占用返回的count）
    if (!calculate_only && count < max_out) 
    {
        out[count].code = 0;
    }

    // 返回有效字符所需的XChar数量（不含终止符）
    return count;
}

// --------------------------
// XChar 转 Latin-1（支持输出为NULL时计算大小）
// --------------------------
int64_t XChar_to_latin1(const XChar* ch, uint8_t* latin1, size_t max_latin1) {
    if (!ch) return -1; // 输入为NULL直接返回错误

    size_t count = 0;
    const XChar* p = ch;
    bool calculate_only = (latin1 == NULL);

    while (p->code != 0) 
    {
        // 检查是否超出Latin-1范围（0x00-0xFF）
        if (p->code > 0xFF) return -1;

        // 每个XChar对应1个Latin-1字节
        size_t needed = 1;

        if (calculate_only) 
        {
            // 仅计算大小：累加计数（不检查max_latin1）
            count += needed;
        }
        else 
        {
            // 实际写入：检查空间是否充足（预留终止符位置）
            if (count + needed >= max_latin1) return -1;
            latin1[count] = (uint8_t)p->code;
            count += needed;
        }

        p++;
    }

    // 实际写入时添加终止符（不占用返回的count）
    if (!calculate_only && count < max_latin1) 
    {
        latin1[count] = '\0';
    }

    // 返回有效字符所需的字节数（不含终止符）
    return count;
}

// --------------------------
// 本地编码转换实现（平台相关）
// --------------------------

// --------------------------
// GBK 转 XChar（支持输出为NULL时计算大小）
// --------------------------
int64_t XChar_from_gbk(const char* gbk, XChar* out, size_t max_out) 
{
    if (!gbk) return -1; // 输入为NULL直接返回错误
#ifdef _WIN32
    bool calculate_only = (out == NULL);
    size_t count = 0;

    // 第一步：获取GBK转换为UTF-16（宽字符）的长度
    int wcs_len = MultiByteToWideChar(
        CP_ACP,         // GBK对应代码页（系统默认ANSI）
        0,              // 转换选项
        gbk,            // 输入GBK字符串
        -1,             // 自动计算长度（含终止符）
        NULL,           // 仅获取长度
        0
    );
    if (wcs_len <= 0) return -1; // 转换失败

    // 计算有效字符对应的XChar数量（不含终止符）
    size_t needed_xchars = (size_t)(wcs_len - 1); // 减去宽字符终止符

    // 仅计算大小时，直接返回所需XChar数量（不含终止符）
    if (calculate_only) {
        return needed_xchars;
    }

    // 实际写入模式：检查输出缓冲区是否足够
    if (needed_xchars > max_out) {
        return -1; // 空间不足
    }

    // 第二步：实际转换为宽字符
    uint16_t* wcs = (uint16_t*)XMemory_malloc(wcs_len * sizeof(uint16_t));
    if (!wcs) return -1;

    if (MultiByteToWideChar(CP_ACP, 0, gbk, -1, wcs, wcs_len) <= 0) {
        XMemory_free(wcs);
        return -1;
    }

    // 复制到XChar数组（UTF-16兼容，不含终止符）
    for (count = 0; count < needed_xchars; count++) {
        out[count].code = (uint16_t)wcs[count];
    }

    // 添加XChar终止符（不占用有效计数）
    if (count < max_out) {
        out[count].code = 0;
    }

    XMemory_free(wcs);
    return count;
#elif defined(__linux__) 
    bool calculate_only = (out == NULL);
    size_t count = 0;

    // 1. 打开iconv转换描述符（GBK -> UTF-16LE，小端UTF-16，与XChar兼容）
    iconv_t cd = iconv_open("UTF-16LE", "GBK");
    if (cd == (iconv_t)-1) return -1;

    // 2. 计算输入GBK字符串长度（不含终止符）
    size_t in_len = strlen(gbk);
    char* in_buf = (char*)gbk;
    size_t remaining_in = in_len;

    // 3. 计算所需XChar数量（仅计算模式）
    if (calculate_only) {
        // 临时缓冲区用于计算转换后长度
        char temp_buf[4096];
        char* out_buf = temp_buf;
        size_t temp_out_len = sizeof(temp_buf);
        size_t total_out = 0;

        while (remaining_in > 0) {
            size_t out_len = temp_out_len;
            char* p = out_buf;
            size_t ret = iconv(cd, &in_buf, &remaining_in, &p, &out_len);
            if (ret == (size_t)-1 && errno != E2BIG) {
                iconv_close(cd);
                return -1; // 转换错误
            }
            total_out += (temp_out_len - out_len);
        }
        iconv_close(cd);
        // UTF-16每个字符占2字节，因此XChar数量 = 总字节数 / 2
        return (total_out / 2);
    }

    // 4. 实际转换模式：将GBK转换为XChar（UTF-16LE）
    if (max_out == 0) {
        iconv_close(cd);
        return -1;
    }

    char* out_buf = (char*)out; // XChar与UTF-16LE字节兼容
    size_t remaining_out = max_out * sizeof(XChar); // 总可用字节数

    while (remaining_in > 0 && remaining_out > 0) {
        char* in_ptr = in_buf;
        char* out_ptr = out_buf;
        size_t in_bytes = remaining_in;
        size_t out_bytes = remaining_out;

        size_t ret = iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
        if (ret == (size_t)-1) {
            if (errno != E2BIG) { // 除了缓冲区不足外的错误
                iconv_close(cd);
                return -1;
            }
        }

        // 计算转换的XChar数量（每2字节对应1个XChar）
        size_t converted = (remaining_out - out_bytes) / sizeof(XChar);
        count += converted;

        // 更新剩余缓冲区
        remaining_in -= (in_buf - in_ptr);
        in_buf = in_ptr;
        remaining_out = out_bytes;
        out_buf = out_ptr;
    }

    // 添加终止符（不占用有效计数）
    if (count < max_out) {
        out[count].code = 0;
    }

    iconv_close(cd);
    return count;
#else
    return -1;
#endif
}

// --------------------------
// XChar 转 GBK（支持输出为NULL时计算大小）
// --------------------------
int64_t XChar_to_gbk(const XChar* ch, char* gbk, size_t max_gbk)
{
    if (!ch) return -1; // 输入为NULL直接返回错误
#ifdef _WIN32
    // 计算XChar数组长度（不含终止符）
    size_t len = 0;
    while (ch[len].code != 0) len++;

    // 转换为Windows宽字符（UTF-16）
    uint16_t* wcs = (uint16_t*)XMemory_malloc((len + 1) * sizeof(uint16_t));
    if (!wcs) return -1;

    for (size_t i = 0; i < len; i++) 
    {
        wcs[i] = (uint16_t)ch[i].code;
    }
    wcs[len] = L'\0'; // 宽字符终止符

    // 第一步：获取GBK所需长度（含终止符）
    int64_t gbk_len = WideCharToMultiByte(
        CP_ACP,         // 目标代码页（GBK）
        0,              // 转换选项
        wcs,            // 输入宽字符串
        -1,             // 自动计算长度（含终止符）
        NULL,           // 仅获取长度
        0,
        NULL,
        NULL
    );

    if (gbk_len <= 0) 
    {
        XMemory_free(wcs);
        return -1; // 转换失败
    }

    // 有效字符数 = 总长度 - 1（减去GBK终止符）
    int64_t valid_len = gbk_len - 1;

    // 仅计算大小时，返回有效字符数（不含终止符）
    if (!gbk)
    {
        XMemory_free(wcs);
        return valid_len;
    }

    // 实际写入模式：检查缓冲区是否足够
    if ((size_t)gbk_len > max_gbk) 
    {
        XMemory_free(wcs);
        return -1; // 空间不足
    }

    // 第二步：执行实际转换
    if (WideCharToMultiByte(CP_ACP, 0, wcs, -1, gbk, gbk_len, NULL, NULL) <= 0) 
    {
        XMemory_free(wcs);
        return -1; // 转换失败
    }

    XMemory_free(wcs);
    return valid_len; // 返回有效字符数（不含终止符）
#elif defined(__linux__)
    bool calculate_only = (gbk == NULL);
    size_t count = 0;

    // 1. 计算XChar数组长度（不含终止符）
    size_t xchar_len = 0;
    while (ch[xchar_len].code != 0) xchar_len++;

    // 2. 打开iconv转换描述符（UTF-16LE -> GBK）
    iconv_t cd = iconv_open("GBK", "UTF-16LE");
    if (cd == (iconv_t)-1) return -1;

    // 3. 准备输入缓冲区（XChar与UTF-16LE字节兼容）
    char* in_buf = (char*)ch;
    size_t remaining_in = xchar_len * sizeof(XChar);

    // 4. 计算所需GBK字节数（仅计算模式）
    if (calculate_only) {
        char temp_buf[4096];
        char* out_buf = temp_buf;
        size_t temp_out_len = sizeof(temp_buf);
        size_t total_out = 0;

        while (remaining_in > 0) {
            size_t out_len = temp_out_len;
            char* p = out_buf;
            size_t ret = iconv(cd, &in_buf, &remaining_in, &p, &out_len);
            if (ret == (size_t)-1 && errno != E2BIG) {
                iconv_close(cd);
                return -1; // 转换错误
            }
            total_out += (temp_out_len - out_len);
        }
        iconv_close(cd);
        return (int)total_out; // 返回有效字节数（不含终止符）
    }

    // 5. 实际转换模式：将XChar转换为GBK
    if (max_gbk == 0) {
        iconv_close(cd);
        return -1;
    }

    char* out_buf = gbk;
    size_t remaining_out = max_gbk - 1; // 预留终止符位置

    while (remaining_in > 0 && remaining_out > 0) {
        char* in_ptr = in_buf;
        char* out_ptr = out_buf;
        size_t in_bytes = remaining_in;
        size_t out_bytes = remaining_out;

        size_t ret = iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
        if (ret == (size_t)-1) {
            if (errno != E2BIG) { // 除了缓冲区不足外的错误
                iconv_close(cd);
                return -1;
            }
        }

        // 计算转换的GBK字节数
        count += (remaining_out - out_bytes);

        // 更新剩余缓冲区
        remaining_in -= (in_buf - in_ptr);
        in_buf = in_ptr;
        remaining_out = out_bytes;
        out_buf = out_ptr;
    }

    // 添加终止符（不占用有效计数）
    if (count < max_gbk - 1) {
        gbk[count] = '\0';
    }

    iconv_close(cd);
    return count;
#else
    return -1;
#endif
}

int64_t XChar_from_local(const char* local_str, XChar* out, size_t max_out)
{
#ifdef _WIN32
    return XChar_from_gbk(local_str,out,max_out);
#elif defined(__linux__)
    return XChar_from_utf8(local_str,out,max_out);
#else
    return -1;
#endif
}

int64_t XChar_to_local(const XChar* ch, char* local_str, size_t max_local)
{
#ifdef _WIN32
    return XChar_to_gbk(ch, local_str, max_local);
#elif defined(__linux__)
    return XChar_to_utf8(ch, local_str, max_local);
#else
    return -1;
#endif
}

int64_t XUTF8_to_gbk(const char* utf8_str, char* gbk_buf, size_t max_len)
{
    if (!utf8_str) return -1;

#ifdef _WIN32
    // 步骤1：UTF-8转宽字符（UTF-16）
    int64_t wcs_len = MultiByteToWideChar(
        CP_UTF8,       // 源编码：UTF-8
        0,             // 转换选项
        utf8_str,      // 输入UTF-8字符串
        -1,            // 自动计算长度（含终止符）
        NULL,          // 仅获取长度
        0
    );
    if (wcs_len <= 0) return -1;

    // 分配宽字符缓冲区
    uint16_t* wcs_buf = (uint16_t*)XMemory_malloc(wcs_len * sizeof(uint16_t));
    if (!wcs_buf) return -1;

    // 实际转换为宽字符
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wcs_buf, wcs_len) <= 0) {
        XMemory_free(wcs_buf);
        return -1;
    }

    // 步骤2：计算GBK长度或执行转换
    int gbk_len;
    if (!gbk_buf) {
        // 仅计算长度：必须传入实际宽字符串才能得到正确结果
        gbk_len = WideCharToMultiByte(
            CP_ACP,       // 目标编码：GBK
            0,            // 转换选项
            wcs_buf,      // 传入实际宽字符串
            -1,           // 自动计算长度（含终止符）
            NULL,         // 仅获取长度
            0,
            NULL,
            NULL
        );
        XMemory_free(wcs_buf);  // 提前释放宽字符缓冲区
        return (gbk_len > 0) ? (gbk_len - 1) : -1;  // 减去终止符
    }

    // 实际转换到GBK缓冲区
    if (max_len == 0) {
        XMemory_free(wcs_buf);
        return -1;
    }

    gbk_len = WideCharToMultiByte(
        CP_ACP,       // 目标编码：GBK
        0,            // 转换选项
        wcs_buf,      // 输入宽字符串
        -1,           // 自动计算长度
        gbk_buf,      // 输出GBK缓冲区
        (int)max_len, // 缓冲区大小
        NULL,
        NULL
    );

    XMemory_free(wcs_buf);  // 释放宽字符缓冲区

    if (gbk_len <= 0) return -1;
    return gbk_len - 1;  // 返回不含终止符的长度

#elif defined(__linux__)
    // Linux平台：使用iconv系统库（保持不变）
    iconv_t cd = iconv_open("GBK", "UTF-8");
    if (cd == (iconv_t)-1) return -1;

    // 计算输入UTF-8长度（不含终止符）
    size_t in_len = strlen(utf8_str);
    char* in_buf = (char*)utf8_str;
    size_t remaining_in = in_len;

    // 仅计算所需GBK长度
    if (!gbk_buf) {
        char temp[4096];
        char* out_buf = temp;
        size_t total = 0;
        size_t temp_len = sizeof(temp);

        while (remaining_in > 0) {
            size_t out_len = temp_len;
            char* p = out_buf;
            size_t ret = iconv(cd, &in_buf, &remaining_in, &p, &out_len);
            if (ret == (size_t)-1 && errno != E2BIG) {
                iconv_close(cd);
                return -1;
            }
            total += temp_len - out_len;
        }
        iconv_close(cd);
        return total;
    }

    // 实际转换到GBK缓冲区
    if (max_len == 0) {
        iconv_close(cd);
        return -1;
    }

    char* out_buf = gbk_buf;
    size_t remaining_out = max_len - 1;  // 预留终止符位置
    size_t total_out = 0;

    while (remaining_in > 0 && remaining_out > 0) {
        char* in_ptr = in_buf;
        char* out_ptr = out_buf;
        size_t in_bytes = remaining_in;
        size_t out_bytes = remaining_out;

        size_t ret = iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
        if (ret == (size_t)-1 && errno != E2BIG) {
            iconv_close(cd);
            return -1;
        }

        total_out += remaining_out - out_bytes;
        remaining_in -= in_buf - in_ptr;
        in_buf = in_ptr;
        remaining_out = out_bytes;
        out_buf = out_ptr;
    }

    // 添加终止符
    if (total_out < max_len - 1) {
        gbk_buf[total_out] = '\0';
    }

    iconv_close(cd);
    return total_out;
#else
return -1;
#endif
}

int64_t XGBK_to_utf8(const char* gbk_str, char* utf8_buf, size_t max_len)
{
    if (!gbk_str) return -1;

#ifdef _WIN32
    // Windows平台：使用WideCharToMultiByte和MultiByteToWideChar
    // 步骤1：GBK转宽字符（UTF-16）
    int wcs_len = MultiByteToWideChar(
        CP_ACP,        // 源编码：GBK（系统默认ANSI代码页）
        0,             // 转换选项
        gbk_str,       // 输入GBK字符串
        -1,            // 自动计算长度（含终止符）
        NULL,          // 仅获取长度
        0
    );
    if (wcs_len <= 0) return -1;

    // 分配宽字符缓冲区
    uint16_t* wcs_buf = (uint16_t*)XMemory_malloc(wcs_len * sizeof(uint16_t));
    if (!wcs_buf) return -1;

    // 实际转换为宽字符
    if (MultiByteToWideChar(CP_ACP, 0, gbk_str, -1, wcs_buf, wcs_len) <= 0) {
        XMemory_free(wcs_buf);
        return -1;
    }

    // 步骤2：宽字符转UTF-8
    int utf8_len;
    if (!utf8_buf) {
        // 仅计算所需UTF-8长度
        utf8_len = WideCharToMultiByte(
            CP_UTF8,       // 目标编码：UTF-8
            0,             // 转换选项
            wcs_buf,       // 输入宽字符串
            -1,            // 自动计算长度
            NULL,          // 仅获取长度
            0,
            NULL,
            NULL
        );
        XMemory_free(wcs_buf);
        return (utf8_len > 0) ? (utf8_len - 1) : -1;  // 减去终止符
    }

    // 实际转换到UTF-8缓冲区
    if (max_len == 0) {
        XMemory_free(wcs_buf);
        return -1;
    }

    utf8_len = WideCharToMultiByte(
        CP_UTF8,        // 目标编码：UTF-8
        0,              // 转换选项
        wcs_buf,        // 输入宽字符串
        -1,             // 自动计算长度
        utf8_buf,       // 输出UTF-8缓冲区
        (int)max_len,   // 缓冲区大小
        NULL,
        NULL
    );

    XMemory_free(wcs_buf);

    if (utf8_len <= 0) return -1;
    return utf8_len - 1;  // 返回不含终止符的长度

#elif defined(__linux__)
    // Linux平台：使用iconv库
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == (iconv_t)-1) return -1;

    // 计算输入GBK长度（不含终止符）
    size_t in_len = strlen(gbk_str);
    char* in_buf = (char*)gbk_str;
    size_t remaining_in = in_len;

    // 仅计算所需UTF-8长度
    if (!utf8_buf) {
        char temp[4096];
        char* out_buf = temp;
        size_t total = 0;
        size_t temp_len = sizeof(temp);

        while (remaining_in > 0) {
            size_t out_len = temp_len;
            char* p = out_buf;
            size_t ret = iconv(cd, &in_buf, &remaining_in, &p, &out_len);
            if (ret == (size_t)-1 && errno != E2BIG) {
                iconv_close(cd);
                return -1;
            }
            total += temp_len - out_len;
        }
        iconv_close(cd);
        return total;
    }

    // 实际转换到UTF-8缓冲区
    if (max_len == 0) {
        iconv_close(cd);
        return -1;
    }

    char* out_buf = utf8_buf;
    size_t remaining_out = max_len - 1;  // 预留终止符位置
    size_t total_out = 0;

    while (remaining_in > 0 && remaining_out > 0) {
        char* in_ptr = in_buf;
        char* out_ptr = out_buf;
        size_t in_bytes = remaining_in;
        size_t out_bytes = remaining_out;

        size_t ret = iconv(cd, &in_ptr, &in_bytes, &out_ptr, &out_bytes);
        if (ret == (size_t)-1 && errno != E2BIG) {
            iconv_close(cd);
            return -1;
        }

        total_out += remaining_out - out_bytes;
        remaining_in -= in_buf - in_ptr;
        in_buf = in_ptr;
        remaining_out = out_bytes;
        out_buf = out_ptr;
    }

    // 添加终止符
    if (total_out < max_len - 1) {
        utf8_buf[total_out] = '\0';
    }

    iconv_close(cd);
    return total_out;
#else
    return -1;
#endif
}
