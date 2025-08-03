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
int64_t XChar_from_utf8(const uint8_t* utf8, size_t input_size, XChar* out, size_t max_out) {
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
        out[out_idx] = XCharNULL;
    }
    else {
        return -1; // 缓冲区不足
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_utf8(const XChar* ch, size_t input_count, uint8_t* utf8, size_t max_utf8) {
    if (!ch) return -1;

    // 计算实际输入XChar数量
    size_t ch_count = 0;
    if (input_count == 0) {
        // 自动检测终止符
        while (ch[ch_count].code != 0) {
            ch_count++;
        }
    }
    else {
        // 使用指定数量，但不超过终止符位置
        ch_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i].code == 0) {
                ch_count = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!utf8 || max_utf8 == 0) {
        size_t byte_count = 0;
        for (size_t i = 0; i < ch_count; i++) {
            uint32_t code = ch[i].code;

            // 处理代理对
            if (XChar_is_high_surrogate(&ch[i]) && i + 1 < ch_count) {
                code = XChar_surrogate_to_unicode(&ch[i], &ch[i + 1]);
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
        uint32_t code = ch[i].code;

        // 处理代理对
        if (XChar_is_high_surrogate(&ch[i]) && i + 1 < ch_count) {
            code = XChar_surrogate_to_unicode(&ch[i], &ch[i + 1]);
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

int64_t XChar_from_utf16(const uint16_t* utf16_str, size_t input_size, XChar* out_xchars, size_t max_count) {
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
        out_xchars[out_idx] = XCharNULL;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_utf16(const XChar* xchars, size_t input_count, uint16_t* out_buf, size_t buf_size) {
    if (!xchars) return -1;

    // 计算实际输入XChar数量
    size_t xchar_count = 0;
    if (input_count == 0) {
        while (xchars[xchar_count].code != 0) {
            xchar_count++;
        }
    }
    else {
        xchar_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (xchars[i].code == 0) {
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
        out_buf[out_idx] = xchars[out_idx].code;
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

int64_t XChar_from_utf32(const uint32_t* utf32, size_t input_count, XChar* out, size_t max_out) {
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
        out[out_idx] = XCharNULL;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_utf32(const XChar* ch, size_t input_count, uint32_t* utf32, size_t max_utf32) {
    if (!ch) return -1;

    // 计算实际输入XChar数量
    size_t ch_count = 0;
    if (input_count == 0) {
        while (ch[ch_count].code != 0) {
            ch_count++;
        }
    }
    else {
        ch_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i].code == 0) {
                ch_count = i;
                break;
            }
        }
    }

    // 仅计算所需大小
    if (!utf32 || max_utf32 == 0) {
        size_t utf32_count = 0;
        for (size_t i = 0; i < ch_count; i++) {
            if (XChar_is_high_surrogate(&ch[i]) && i + 1 < ch_count) {
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
        if (XChar_is_high_surrogate(&ch[i]) && i + 1 < ch_count) {
            code = XChar_surrogate_to_unicode(&ch[i], &ch[i + 1]);
            i++; // 跳过低代理
        }
        else {
            code = ch[i].code;
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

int64_t XChar_from_latin1(const uint8_t* latin1, size_t input_size, XChar* out, size_t max_out) {
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
        out[out_idx] = XCharNULL;
    }
    else {
        return -1;
    }

    return (int64_t)out_idx;
}

int64_t XChar_to_latin1(const XChar* ch, size_t input_count, uint8_t* latin1, size_t max_latin1) {
    if (!ch) return -1;

    // 计算实际输入XChar数量
    size_t ch_count = 0;
    if (input_count == 0) {
        while (ch[ch_count].code != 0) {
            ch_count++;
        }
    }
    else {
        ch_count = input_count;
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i].code == 0) {
                ch_count = i;
                break;
            }
        }
    }

    // 检查是否有超出Latin1范围的码点
    for (size_t i = 0; i < ch_count; i++) {
        if (ch[i].code > 0xFF) {
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
        latin1[out_idx] = (uint8_t)ch[out_idx].code;
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
int64_t XChar_from_gbk(const char* gbk, size_t input_size, XChar* out, size_t max_out)
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
            out[0] = XCharNULL;
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
    out[wchar_count - 1] = XCharNULL;

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
    out[converted] = XCharNULL;
    required = (int64_t)converted;

    iconv_close(cd);
#endif

    return required;
}

int64_t XChar_to_gbk(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    if (!ch) return -1;

    // 计算输入XChar数组的实际长度（不含终止符）
    size_t actual_count = input_count;
    if (input_count == 0) {
        // 自动检测终止符
        while (ch[actual_count].code != 0) {
            actual_count++;
        }
    }
    else {
        // 使用指定大小，但不超过终止符位置
        for (size_t i = 0; i < input_count; i++) {
            if (ch[i].code == 0) {
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

int64_t XChar_from_local(const char* local_str, size_t input_size, XChar* out, size_t max_out)
{
#ifdef _WIN32
    // Windows本地编码为GBK
    return XChar_from_gbk(local_str, input_size, out, max_out);
#elif defined(__linux__)
    // Linux本地编码为UTF-8
    return XChar_from_utf8((const uint8_t*)local_str, input_size, out, max_out);
#else
    return -1;
#endif
}

int64_t XChar_to_local(const XChar* ch, size_t input_count, char* local_str, size_t max_local)
{
#ifdef _WIN32
    // Windows本地编码为GBK
    return XChar_to_gbk(ch, input_count, local_str, max_local);
#elif defined(__linux__)
    // Linux本地编码为UTF-8
    return XChar_to_utf8(ch, input_count, (uint8_t*)local_str, max_local);
#else
    return -1;
#endif
}
// --------------------------
// UTF-8与GBK互转函数（跨平台）
// --------------------------
int64_t XUTF8_to_gbk(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
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

int64_t XGBK_to_utf8(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
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
