/**
 * @file       XSettings_ini.c
 * @brief      XSettings 的 INI 算法核心实现（转义/节解析/类型前缀/写出）。
 * @details    逐分支对齐 Qt 6.8.3 qsettings.cpp（行号见各函数注释）；统一
 *             非 Windows 语义：键大小写敏感、节/键字典序、'\n' 换行。
 *             实现只依赖 XinYueC 抽象层（XString/XByteArray/XVariant/
 *             XMemory），禁止调用 Win32、POSIX、Qt 或其他平台 API，亦
 *             禁用 stdio/string.h/ctype.h（内存与串原语走 XMemory/
 *             XStringUtils）。
 * @author     XinYueC 团队
 */
#include "XSettings_ini.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include "XVariantList.h"
#include "XMap.h"
#include "XPrintf.h"

/* ==========================================================================
 * 字节缓冲
 * ========================================================================== */

void xbb_init(XSettingsByteBuf* buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void xbb_free(XSettingsByteBuf* buf)
{
    if (buf->data)
        XFree_System(buf->data);
    xbb_init(buf);
}

static bool xbb_reserve(XSettingsByteBuf* buf, size_t extra)
{
    size_t need = buf->len + extra;
    if (need <= buf->cap)
        return true;
    size_t cap = buf->cap ? buf->cap : 64;
    while (cap < need)
        cap *= 2;
    char* data = (char*)XRealloc_System(buf->data, cap);
    if (!data)
        return false;
    buf->data = data;
    buf->cap = cap;
    return true;
}

bool xbb_append(XSettingsByteBuf* buf, const char* bytes, size_t n)
{
    if (n == 0)
        return true;
    if (!bytes || !xbb_reserve(buf, n))
        return false;
    XMemcpy(buf->data + buf->len, bytes, n);
    buf->len += n;
    return true;
}

bool xbb_append_char(XSettingsByteBuf* buf, char ch)
{
    return xbb_append(buf, &ch, 1);
}

bool xbb_append_str(XSettingsByteBuf* buf, const char* str)
{
    return xbb_append(buf, str, XStrlen(str));
}

bool xbb_append_string(XSettingsByteBuf* buf, const XString* str)
{
    if (!str)
        return xbb_append(buf, "", 0);
    const char* utf8 = XString_toUtf8(str);
    return xbb_append(buf, utf8, utf8 ? XStrlen(utf8) : 0);
}

XString* xbb_toString(const XSettingsByteBuf* buf)
{
    return XString_create_with_length_utf8(buf->data ? buf->data : "", buf->len);
}

/* ==========================================================================
 * 十六进制工具
 * ========================================================================== */

/** @brief 取 32 位值的第 nibbleNibble 个半字节的大写十六进制字符。 */
static char xsettingsHexUpper(uint32_t value, int nibble)
{
    uint32_t digit = (value >> (nibble * 4)) & 0xF;
    return (char)(digit < 10 ? '0' + digit : 'A' + (digit - 10));
}

/** @brief 取半字节的小写十六进制字符（值 \xHH 转义专用，对标小写口径）。 */
static char xsettingsHexLower(int value)
{
    static const char digits[] = "0123456789abcdef";
    return digits[value & 0xF];
}

static int xsettingsHexValue(char ch, bool* ok)
{
    if (ch >= '0' && ch <= '9') { *ok = true; return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { *ok = true; return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { *ok = true; return ch - 'A' + 10; }
    *ok = false;
    return 0;
}

/** @brief ASCII 字母/数字白名单（对标 isAsciiLetterOrNumber）。 */
static bool xsettingsIsAsciiLetterOrNumber(char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

/** @brief 字节段 ASCII 不区分大小写比较（[General] 映射专用）。 */
static bool xsettingsAsciiEqualsInsensitive(const char* bytes, size_t len, const char* lowerWord)
{
    size_t wordLen = XStrlen(lowerWord);
    if (len != wordLen)
        return false;
    for (size_t i = 0; i < len; ++i) {
        char ch = bytes[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        if (ch != lowerWord[i])
            return false;
    }
    return true;
}

/* ==========================================================================
 * INI 键转义（对标 iniEscapedKey qsettings.cpp:509-533）
 * ========================================================================== */

bool xsettingsEscapedKey(const XString* key, XSettingsByteBuf* out)
{
    if (!key || !out)
        return false;
    size_t size = XString_size(key);
    for (size_t i = 0; i < size; ++i) {
        XChar ch = XString_at(key, i);
        if (ch == (XChar)'/') {
            if (!xbb_append_char(out, '\\'))
                return false;
        }
        else if ((ch >= (XChar)'a' && ch <= (XChar)'z') ||
                 (ch >= (XChar)'A' && ch <= (XChar)'Z') ||
                 (ch >= (XChar)'0' && ch <= (XChar)'9') ||
                 ch == (XChar)'_' || ch == (XChar)'-' || ch == (XChar)'.') {
            if (!xbb_append_char(out, (char)ch))
                return false;
        }
        else if ((uint32_t)ch <= 0xFF) {
            /* %HH 两位大写十六进制：高位在前（对齐 Qt，写反会产出 %02
             * 这类错码，读端虽然能还原但文本与 Qt 不一致） */
            char seq[3];
            seq[0] = '%';
            seq[1] = xsettingsHexUpper((uint32_t)ch, 1);
            seq[2] = xsettingsHexUpper((uint32_t)ch, 0);
            if (!xbb_append(out, seq, 3))
                return false;
        }
        else {
            /* %UXXXX 四位大写十六进制（按 16 位码元，代理对拆两段） */
            char seq[6];
            seq[0] = '%';
            seq[1] = 'U';
            seq[2] = xsettingsHexUpper((uint32_t)ch, 3);
            seq[3] = xsettingsHexUpper((uint32_t)ch, 2);
            seq[4] = xsettingsHexUpper((uint32_t)ch, 1);
            seq[5] = xsettingsHexUpper((uint32_t)ch, 0);
            if (!xbb_append(out, seq, 6))
                return false;
        }
    }
    return true;
}

/* ==========================================================================
 * INI 键解转义（对标 iniUnescapedKey qsettings.cpp:535-590）
 * ========================================================================== */

XString* xsettingsUnescapedKey(const char* bytes, size_t len)
{
    XString* decoded = XString_create_with_length_utf8(bytes, len);
    if (!decoded)
        return NULL;
    XString* out = XString_create();
    if (!out) {
        XClassDelete(decoded);
        return NULL;
    }
    size_t size = XString_size(decoded);
    for (size_t i = 0; i < size; ++i) {
        XChar ch = XString_at(decoded, i);
        if (ch == (XChar)'\\') {
            XString_append_char(out, (XChar)'/');
            continue;
        }
        if (ch != (XChar)'%' || i == size - 1) {
            /* 非 '%'，或 '%' 是最后一个字符：原样拷贝 */
            XString_append_char(out, ch);
            continue;
        }
        size_t firstDigitPos = i + 1;
        int numDigits = 2;
        if (XString_at(decoded, i + 1) == (XChar)'U') {
            firstDigitPos = i + 2;
            numDigits = 4;
        }
        if (firstDigitPos + (size_t)numDigits > size) {
            /* 容错 A：数字不够 → 字面 '%'，i 前进 1 */
            XString_append_char(out, (XChar)'%');
            continue;
        }
        bool ok = true;
        int value = 0;
        for (int d = 0; d < numDigits; ++d) {
            bool digitOk = false;
            int digit = xsettingsHexValue((char)XString_at(decoded, firstDigitPos + (size_t)d), &digitOk);
            if (!digitOk) {
                ok = false;
                break;
            }
            value = (value << 4) | digit;
        }
        if (!ok) {
            /* 容错 B：非十六进制 → 字面 '%'，后续字符照常处理 */
            XString_append_char(out, (XChar)'%');
            continue;
        }
        XString_append_char(out, (XChar)value);
        i = firstDigitPos + (size_t)numDigits - 1;
    }
    XClassDelete(decoded);
    return out;
}

/* ==========================================================================
 * INI 值转义（对标 iniEscapedString qsettings.cpp:592-666 /
 * iniEscapedStringList qsettings.cpp:676-695）
 * ========================================================================== */

/** @brief 原始字节前缀内容（@ByteArray/@Variant/@DateTime，禁用编码转义）。 */
static bool xsettingsIsRawPrefix(const XString* str)
{
    return XString_startsWith_utf8(str, "@ByteArray(", XChar_CaseSensitive) ||
           XString_startsWith_utf8(str, "@Variant(", XChar_CaseSensitive) ||
           XString_startsWith_utf8(str, "@DateTime(", XChar_CaseSensitive);
}

bool xsettingsEscapedString(const XString* str, XSettingsByteBuf* out)
{
    if (!str || !out)
        return false;
    bool useCodec = !xsettingsIsRawPrefix(str);
    bool needsQuotes = false;
    size_t startPos = out->len;
    size_t size = XString_size(str);
    bool escapeNextIfDigit = false;
    for (size_t i = 0; i < size; ++i) {
        XChar ch = XString_at(str, i);
        /* 原始字节前缀内容（@ByteArray/@Variant/@DateTime）不做引号包裹：
         * 其载荷是 base64/Latin1 字节，''='' 是正常载荷字符，引号包裹会
         * 让读端把载荷当作文本转义而破坏内容（对齐 Qt 的 useCodec 口径）。 */
        if (useCodec && (ch == (XChar)';' || ch == (XChar)',' || ch == (XChar)'='))
            needsQuotes = true;
        if (escapeNextIfDigit) {
            escapeNextIfDigit = false;
            if ((uint32_t)ch <= 0xFF) {
                bool digitOk = false;
                xsettingsHexValue((char)ch, &digitOk);
                if (digitOk) {
                    /* 前一 \0/\xHH 会被解码并入十六进制 → 本字符强制 \xHH */
                    char seq[4];
                    seq[0] = '\\';
                    seq[1] = 'x';
                    seq[2] = xsettingsHexLower((int)((uint32_t)ch >> 4));
                    seq[3] = xsettingsHexLower((int)ch);
                    if (!xbb_append(out, seq, 4))
                        return false;
                    escapeNextIfDigit = true;
                    continue;
                }
            }
        }
        switch (ch) {
        case (XChar)'\0':
            if (!xbb_append_str(out, "\\0"))
                return false;
            escapeNextIfDigit = true;
            break;
        case (XChar)'\a':
            if (!xbb_append_str(out, "\\a")) return false;
            break;
        case (XChar)'\b':
            if (!xbb_append_str(out, "\\b")) return false;
            break;
        case (XChar)'\f':
            if (!xbb_append_str(out, "\\f")) return false;
            break;
        case (XChar)'\n':
            if (!xbb_append_str(out, "\\n")) return false;
            break;
        case (XChar)'\r':
            if (!xbb_append_str(out, "\\r")) return false;
            break;
        case (XChar)'\t':
            if (!xbb_append_str(out, "\\t")) return false;
            break;
        case (XChar)'\v':
            if (!xbb_append_str(out, "\\v")) return false;
            break;
        case (XChar)'"':
            if (!xbb_append_str(out, "\\\"")) return false;
            break;
        case (XChar)'\\':
            if (!xbb_append_str(out, "\\\\")) return false;
            break;
        default:
            if ((uint32_t)ch <= 0x1F || ((uint32_t)ch >= 0x7F && !useCodec)) {
                /* \x + 小写十六进制（变长，对标 QByteArray::number(ch,16)） */
                if (!xbb_append_str(out, "\\x"))
                    return false;
                uint32_t v = (uint32_t)ch;
                char hex[8];
                int hexLen = 0;
                if (v == 0) {
                    hex[hexLen++] = '0';
                }
                else {
                    char tmp[8];
                    while (v > 0) {
                        tmp[hexLen++] = xsettingsHexLower((int)(v & 0xF));
                        v >>= 4;
                    }
                    for (int k = 0; k < hexLen; ++k)
                        hex[k] = tmp[hexLen - 1 - k];
                }
                if (!xbb_append(out, hex, (size_t)hexLen))
                    return false;
                escapeNextIfDigit = true;
            }
            else if (useCodec) {
                /* UTF-8 编码输出（代理对合成码点；孤立代理按 '?' 兜底） */
                uint32_t cp = (uint32_t)ch;
                char utf8[4];
                int n = 0;
                if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < size) {
                    XChar low = XString_at(str, i + 1);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((uint32_t)(ch - 0xD800) << 10) + (uint32_t)(low - 0xDC00);
                        ++i;
                    }
                }
                if (cp < 0x80) {
                    utf8[n++] = (char)cp;
                }
                else if (cp < 0x800) {
                    utf8[n++] = (char)(0xC0 | (cp >> 6));
                    utf8[n++] = (char)(0x80 | (cp & 0x3F));
                }
                else if (cp < 0x10000) {
                    utf8[n++] = (char)(0xE0 | (cp >> 12));
                    utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    utf8[n++] = (char)(0x80 | (cp & 0x3F));
                }
                else {
                    utf8[n++] = (char)(0xF0 | (cp >> 18));
                    utf8[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                    utf8[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                    utf8[n++] = (char)(0x80 | (cp & 0x3F));
                }
                if (!xbb_append(out, utf8, (size_t)n))
                    return false;
            }
            else {
                if (!xbb_append_char(out, (char)ch))
                    return false;
            }
            break;
        }
    }
    /* 引号条件：含 ; , =，或非空且首/尾字符为空格（对标 660-665） */
    bool wrap = needsQuotes;
    if (!wrap && out->len > startPos) {
        if (out->data[startPos] == ' ' || out->data[out->len - 1] == ' ')
            wrap = true;
    }
    if (wrap) {
        size_t bodyLen = out->len - startPos;
        if (!xbb_reserve(out, 2))
            return false;
        XMemmove(out->data + startPos + 1, out->data + startPos, bodyLen);
        out->data[startPos] = '"';
        out->data[startPos + bodyLen + 1] = '"';
        out->len += 2;   /* 首尾各一引号（此前 +1 丢尾引号，被后续写入覆盖） */
    }
    return true;
}

bool xsettingsEscapedStringList(const XStringList* list, XSettingsByteBuf* out)
{
    if (!out)
        return false;
    size_t count = list ? XStringList_size_base((XStringList*)list) : 0;
    if (count == 0)
        return xbb_append_str(out, "@Invalid()");
    for (size_t i = 0; i < count; ++i) {
        if (i > 0 && !xbb_append_str(out, ", "))
            return false;
        const XString* item = XStringList_at_base(list, i);
        if (item && !xsettingsEscapedString(item, out))
            return false;
    }
    return true;
}

/* ==========================================================================
 * INI 值解转义状态机（对标 iniUnescapedStringList qsettings.cpp:697-848）
 * ========================================================================== */

/** @brief 裁掉超出 chopLimit 的尾部空白（对标 iniChopTrailingSpaces 668-674）。 */
static void xsettingsChopTrailingSpaces(XString* str, size_t chopLimit)
{
    size_t size = XString_size(str);
    while (size > chopLimit) {
        XChar ch = XString_at(str, size - 1);
        if (ch != (XChar)' ' && ch != (XChar)'\t')
            break;
        XString_remove_base(str, size - 1, 1);
        --size;
    }
}

XStringList* xsettingsUnescapedStringList(const char* bytes, size_t len, bool* isStringListOut)
{
    XStringList* result = XStringList_create();
    XString* current = XString_create();
    if (!result || !current) {
        if (result) XClassDelete(result);
        if (current) XClassDelete(current);
        if (isStringListOut)
            *isStringListOut = false;
        return NULL;
    }
    bool isStringList = false;
    bool inQuotedString = false;
    bool currentValueIsQuoted = false;
    size_t chopLimit = 0;
    size_t i = 0;
    enum { StNormal, StSkipSpaces, StHexEscape, StOctEscape } state = StSkipSpaces;
    int escapeVal = 0;
    int octDigits = 0;

    while (i < len) {
        char ch = bytes[i];
        switch (state) {
        case StSkipSpaces:
            if (ch == ' ' || ch == '\t')
                ++i;
            else
                state = StNormal;
            break;
        case StNormal:
            if (ch == '\\') {
                ++i;
                if (i >= len)
                    goto end_pos;                       /* 悬空反斜杠：丢弃 */
                char esc = bytes[i];
                ++i;
                switch (esc) {
                case 'a': XString_append_char(current, (XChar)'\a'); chopLimit = XString_size(current); break;
                case 'b': XString_append_char(current, (XChar)'\b'); chopLimit = XString_size(current); break;
                case 'f': XString_append_char(current, (XChar)'\f'); chopLimit = XString_size(current); break;
                case 'n': XString_append_char(current, (XChar)'\n'); chopLimit = XString_size(current); break;
                case 'r': XString_append_char(current, (XChar)'\r'); chopLimit = XString_size(current); break;
                case 't': XString_append_char(current, (XChar)'\t'); chopLimit = XString_size(current); break;
                case 'v': XString_append_char(current, (XChar)'\v'); chopLimit = XString_size(current); break;
                case '"': XString_append_char(current, (XChar)'"'); chopLimit = XString_size(current); break;
                case '?': XString_append_char(current, (XChar)'?'); chopLimit = XString_size(current); break;
                case '\'': XString_append_char(current, (XChar)'\''); chopLimit = XString_size(current); break;
                case '\\': XString_append_char(current, (XChar)'\\'); chopLimit = XString_size(current); break;
                case 'x':
                    escapeVal = 0;
                    if (i >= len)
                        goto end_pos;                   /* \x 悬空：不输出 */
                    {
                        bool digitOk = false;
                        xsettingsHexValue(bytes[i], &digitOk);
                        if (digitOk)
                            state = StHexEscape;
                        /* 非十六进制：\x 两字符被吞，后续按普通文本继续 */
                    }
                    break;
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7':
                    escapeVal = esc - '0';
                    octDigits = 1;
                    state = StOctEscape;
                    break;
                case '\n':
                case '\r':
                    /* 行续接：\r\n/\n\r 对一并吞掉 */
                    if (i < len && ((bytes[i] == '\n' && esc == '\r') || (bytes[i] == '\r' && esc == '\n')))
                        ++i;
                    break;
                default:
                    /* 其他字符：整个"反斜杠+字符"静默跳过 */
                    break;
                }
            }
            else if (ch == '"') {
                inQuotedString = !inQuotedString;
                if (inQuotedString)
                    currentValueIsQuoted = true;
                ++i;
                /* 对齐 Qt：进入引号后按字面处理（引号内逗号/空格不做分隔）；
                 * 仅退出引号后回到跳过空白态（引号后允许 ", x" 形式）。 */
                state = inQuotedString ? StNormal : StSkipSpaces;
            }
            else if (ch == ',' && !inQuotedString) {
                if (!currentValueIsQuoted)
                    xsettingsChopTrailingSpaces(current, chopLimit);
                isStringList = true;
                if (!XStringList_push_back_move_base(result, current)) {
                    XClassDelete(current);
                    goto fail;
                }
                XClassDelete(current);   /* move 后源壳仍须释放 */
                current = XString_create();
                if (!current)
                    goto fail;
                currentValueIsQuoted = false;
                ++i;
                state = StSkipSpaces;
            }
            else if (ch == ',' && inQuotedString) {
                /* 引号内逗号是字面字符（Qt: 引号内不切分列表） */
                XString_append_char(current, (XChar)ch);
                ++i;
            }
            else if (ch == ' ' || ch == '\t') {
                /* 字面空格是值的一部分（首尾由跳过/裁剪处理） */
                XString_append_char(current, (XChar)ch);
                ++i;
            }
            else {
                /* 字面段：到下一个 '\' '"' ',' 为止按 UTF-8 解码追加。
                 * 逐字节 UTF-8 解码后直接用 XChar 追加，避免取用
                 * XString_toUtf8 的临时缓存（同源串修改即失效）。 */
                size_t start = i;
                while (i < len && bytes[i] != '\\' && bytes[i] != '"' && bytes[i] != ',')
                    ++i;
                XString* segment = XString_create_with_length_utf8(bytes + start, i - start);
                if (!segment)
                    goto fail;
                size_t segLen = XString_size(segment);
                for (size_t k = 0; k < segLen; ++k)
                    XString_append_char(current, XString_at(segment, k));
                XClassDelete(segment);
                if (!inQuotedString)
                    chopLimit = XString_size(current);
            }
            break;
        case StHexEscape: {
            bool digitOk = false;
            int digit = xsettingsHexValue(ch, &digitOk);
            if (digitOk) {
                escapeVal = (escapeVal << 4) | digit;   /* 16 位回绕截断 */
                escapeVal &= 0xFFFF;
                ++i;
            }
            else {
                XString_append_char(current, (XChar)escapeVal);
                chopLimit = XString_size(current);
                state = StNormal;
            }
            break;
        }
        case StOctEscape:
            if (octDigits < 3 && ch >= '0' && ch <= '7') {
                escapeVal = (escapeVal << 3) | (ch - '0');
                ++octDigits;
                ++i;
            }
            else {
                XString_append_char(current, (XChar)(escapeVal & 0xFFFF));
                chopLimit = XString_size(current);
                state = StNormal;
            }
            break;
        }
    }
end_pos:
    if (!currentValueIsQuoted)
        xsettingsChopTrailingSpaces(current, chopLimit);
    /* 列表与非列表都必须把当前元素放入结果：非列表时 result 即
     * 「恰好一个元素」的列表（调用方按 size()>0 取首元素），此前非列表
     * 分支直接 XClassDelete(current) 导致所有普通值解析为空。
     * move 语义仅转移内容，源壳仍须释放（否则每次解析值都泄漏一个壳）。 */
    XStringList_push_back_move_base(result, current);
    XClassDelete(current);
    if (isStringListOut)
        *isStringListOut = isStringList;
    return result;
fail:
    XClassDelete(current);
    XClassDelete(result);
    if (isStringListOut)
        *isStringListOut = false;
    return NULL;
}

/* ==========================================================================
 * TLV 序列化（@Variant/@DateTime 内部承载，不与 Qt 二进制兼容）
 * ========================================================================== */

/** @brief 该变体类型能否走内部 TLV 承载。 */
static bool xsettingsTlvEncodable(int type)
{
    switch (type) {
    case XVariantType_NULL:
    case XVariantType_Bool:
    case XVariantType_Int8:
    case XVariantType_Uint8:
    case XVariantType_Int16:
    case XVariantType_Uint16:
    case XVariantType_Int32:
    case XVariantType_Uint32:
    case XVariantType_Int64:
    case XVariantType_Uint64:
    case XVariantType_Int:
    case XVariantType_Size_t:
    case XVariantType_Float:
    case XVariantType_Double:
    case XVariantType_String:
    case XVariantType_ByteArray:
    case XVariantType_StringList:
    case XVariantType_Point:
    case XVariantType_Size:
    case XVariantType_Rect:
    case XVariantType_Date:
    case XVariantType_Time:
    case XVariantType_DateTime:
    case XVariantType_List:
    case XVariantType_Map:
    case XVariantType_Hash:
        return true;
    default:
        return false;
    }
}

static bool xbb_append_u32(XSettingsByteBuf* buf, uint32_t value)
{
    char bytes[4];
    bytes[0] = (char)(value & 0xFF);
    bytes[1] = (char)((value >> 8) & 0xFF);
    bytes[2] = (char)((value >> 16) & 0xFF);
    bytes[3] = (char)((value >> 24) & 0xFF);
    return xbb_append(buf, bytes, 4);
}

static bool xbb_append_i64(XSettingsByteBuf* buf, int64_t value)
{
    uint64_t u = (uint64_t)value;
    char bytes[8];
    for (int i = 0; i < 8; ++i)
        bytes[i] = (char)((u >> (i * 8)) & 0xFF);
    return xbb_append(buf, bytes, 8);
}

static bool xbb_append_utf8_len(XSettingsByteBuf* buf, const XString* str)
{
    if (!str)
        return xbb_append_u32(buf, 0);
    const char* utf8 = XString_toUtf8(str);
    size_t len = utf8 ? XStrlen(utf8) : 0;
    if (!xbb_append_u32(buf, (uint32_t)len))
        return false;
    return xbb_append(buf, utf8, len);
}

/** @brief 变体 → TLV 字节（类型字节 + 载荷；容器递归）。调用方释放。 */
static XByteArray* xsettingsEncodeTlv(const XVariant* variant)
{
    if (!variant || !xsettingsTlvEncodable(variant->m_type))
        return NULL;
    XSettingsByteBuf buf;
    xbb_init(&buf);
    if (!xbb_append_char(&buf, (char)variant->m_type))
        goto fail;
    switch (variant->m_type) {
    case XVariantType_NULL:
        break;
    case XVariantType_Bool:
        if (!xbb_append_char(&buf, XVariant_toBool(variant) ? 1 : 0))
            goto fail;
        break;
    case XVariantType_Int8:
    case XVariantType_Char:
        if (!xbb_append_char(&buf, (char)XVariant_toInt64(variant)))
            goto fail;
        break;
    case XVariantType_Uint8:
    case XVariantType_UChar:
        if (!xbb_append_char(&buf, (char)(uint8_t)XVariant_toInt64(variant)))
            goto fail;
        break;
    case XVariantType_Int16:
    case XVariantType_Uint16:
    case XVariantType_Int32:
    case XVariantType_Uint32:
    case XVariantType_Int:
    case XVariantType_Size_t:
        if (!xbb_append_u32(&buf, (uint32_t)(int64_t)XVariant_toInt64(variant)))
            goto fail;
        break;
    case XVariantType_Int64:
    case XVariantType_Uint64:
        if (!xbb_append_i64(&buf, XVariant_toInt64(variant)))
            goto fail;
        break;
    case XVariantType_Float:
    case XVariantType_Double: {
        double d = XVariant_toDouble(variant);
        uint64_t bits;
        XMemcpy(&bits, &d, sizeof(bits));
        if (!xbb_append_i64(&buf, (int64_t)bits))
            goto fail;
        break;
    }
    case XVariantType_String: {
        XString* str = XString_fromVariant_ref((XVariant*)variant);
        if (!xbb_append_utf8_len(&buf, str))
            goto fail;
        break;
    }
    case XVariantType_ByteArray: {
        XByteArray* array = XByteArray_fromVariant_ref((XVariant*)variant);
        size_t n = array ? XByteArray_size_base(array) : 0;
        uint8_t* raw = array ? XByteArray_data(array) : NULL;
        if (!xbb_append_u32(&buf, (uint32_t)n))
            goto fail;
        if (n && !xbb_append(&buf, (const char*)raw, n))
            goto fail;
        break;
    }
    case XVariantType_StringList: {
        XStringList* list = XStringList_fromVariant_ref((XVariant*)variant);
        size_t count = list ? XStringList_size_base(list) : 0;
        if (!xbb_append_u32(&buf, (uint32_t)count))
            goto fail;
        for (size_t i = 0; i < count; ++i) {
            const XString* item = XStringList_at_base(list, i);
            if (!xbb_append_utf8_len(&buf, item))
                goto fail;
        }
        break;
    }
    case XVariantType_Point: {
        XPoint point = XPoint_fromVariant(variant);
        if (!xbb_append_u32(&buf, (uint32_t)point.x) || !xbb_append_u32(&buf, (uint32_t)point.y))
            goto fail;
        break;
    }
    case XVariantType_Size: {
        XSize size = XSize_fromVariant(variant);
        if (!xbb_append_u32(&buf, (uint32_t)size.width) || !xbb_append_u32(&buf, (uint32_t)size.height))
            goto fail;
        break;
    }
    case XVariantType_Rect: {
        XRect rect = XRect_fromVariant(variant);
        if (!xbb_append_u32(&buf, (uint32_t)rect.x) || !xbb_append_u32(&buf, (uint32_t)rect.y) ||
            !xbb_append_u32(&buf, (uint32_t)rect.width) || !xbb_append_u32(&buf, (uint32_t)rect.height))
            goto fail;
        break;
    }
    case XVariantType_Date: {
        XDate* date = (XDate*)XVariant_toRef((XVariant*)variant, XVariantType_Date);
        if (!xbb_append_i64(&buf, date ? date->m_jd : -1))
            goto fail;
        break;
    }
    case XVariantType_Time: {
        XTime* time = (XTime*)XVariant_toRef((XVariant*)variant, XVariantType_Time);
        if (!xbb_append_u32(&buf, (uint32_t)(time ? time->m_msecs : -1)))
            goto fail;
        break;
    }
    case XVariantType_DateTime: {
        XDateTime* dateTime = (XDateTime*)XVariant_toRef((XVariant*)variant, XVariantType_DateTime);
        if (!xbb_append_i64(&buf, dateTime ? dateTime->m_date.m_jd : -1))
            goto fail;
        if (!xbb_append_u32(&buf, (uint32_t)(dateTime ? dateTime->m_time.m_msecs : -1)))
            goto fail;
        break;
    }
    case XVariantType_List: {
        XVariantList* list = (XVariantList*)XVariant_toRef((XVariant*)variant, XVariantType_List);
        size_t count = list ? XVariantList_size_base(list) : 0;
        if (!xbb_append_u32(&buf, (uint32_t)count))
            goto fail;
        for (size_t i = 0; i < count; ++i) {
            XVariant* item = (XVariant*)XVariantList_at_base(list, i);   /* 值槽内联对象借用 */
            if (!item || !xsettingsTlvEncodable(item->m_type))
                goto fail;
            XByteArray* encoded = xsettingsEncodeTlv(item);
            bool ok = encoded != NULL;
            if (ok)
                ok = xbb_append(&buf, (const char*)XByteArray_data(encoded),
                                XByteArray_size_base(encoded));
            if (encoded)
                XClassDelete(encoded);
            if (!ok)
                goto fail;
        }
        break;
    }
    case XVariantType_Map:
    case XVariantType_Hash: {
        XMap* map = (XMap*)XVariant_toRef((XVariant*)variant, variant->m_type);
        size_t count = map ? XMap_size_base(map) : 0;
        if (!xbb_append_u32(&buf, (uint32_t)count))
            goto fail;
        if (map) {
            XVector* keys = XMap_keys_base(map);
            XVector* values = XMapBase_values_base(map);
            size_t keyCount = keys ? XVector_size_base(keys) : 0;
            size_t valueCount = values ? XVector_size_base(values) : 0;
            bool ok = (keyCount == valueCount);
            for (size_t i = 0; ok && i < keyCount; ++i) {
                /* XVariantMap 为值槽内联存储：槽地址即对象地址 */
                XString* key = (XString*)XVector_at_base(keys, i);
                XVariant* value = (XVariant*)XVector_at_base(values, i);
                if (!key || !value || !xsettingsTlvEncodable(value->m_type)) {
                    ok = false;
                    break;
                }
                ok = xbb_append_utf8_len(&buf, key);
                if (ok) {
                    /* xsettingsEncodeTlv 的产物首字节即类型，切勿再写一次
                     * （历史缺陷：重复写类型导致解码错位、内层值变垃圾） */
                    XByteArray* encoded = xsettingsEncodeTlv(value);
                    ok = encoded != NULL;
                    if (ok)
                        ok = xbb_append(&buf, (const char*)XByteArray_data(encoded),
                                        XByteArray_size_base(encoded));
                    if (encoded)
                        XClassDelete(encoded);
                }
            }
            if (keys) XClassDelete(keys);
            if (values) XClassDelete(values);
            if (!ok)
                goto fail;
        }
        break;
    }
    default:
        goto fail;
    }
    {
        XByteArray* out = XByteArray_create_with_data(buf.data, buf.len);
        xbb_free(&buf);
        return out;
    }
fail:
    xbb_free(&buf);
    return NULL;
}

/* ==========================================================================
 * TLV 解码
 * ========================================================================== */

typedef struct XSettingsTlvReader
{
    const uint8_t* data;   /**< 数据。 */
    size_t len;            /**< 长度。 */
    size_t pos;            /**< 游标。 */
    bool ok;               /**< 是否仍可读。 */
} XSettingsTlvReader;

static bool xtrReadBytes(XSettingsTlvReader* r, uint8_t* out, size_t n)
{
    if (!r->ok || r->len - r->pos < n) {
        r->ok = false;
        return false;
    }
    if (out)
        XMemcpy(out, r->data + r->pos, n);
    r->pos += n;
    return true;
}

static uint32_t xtrReadU32(XSettingsTlvReader* r)
{
    uint8_t bytes[4];
    if (!xtrReadBytes(r, bytes, 4))
        return 0;
    return (uint32_t)(uint8_t)bytes[0] | ((uint32_t)(uint8_t)bytes[1] << 8) |
           ((uint32_t)(uint8_t)bytes[2] << 16) | ((uint32_t)(uint8_t)bytes[3] << 24);
}

static int64_t xtrReadI64(XSettingsTlvReader* r)
{
    uint8_t bytes[8];
    if (!xtrReadBytes(r, bytes, 8))
        return 0;
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i)
        u |= (uint64_t)(uint8_t)bytes[i] << (i * 8);
    return (int64_t)u;
}

static XString* xtrReadString(XSettingsTlvReader* r)
{
    uint32_t len = xtrReadU32(r);
    if (!r->ok || r->len - r->pos < len) {
        r->ok = false;
        return NULL;
    }
    XString* str = XString_create_with_length_utf8((const char*)r->data + r->pos, len);
    r->pos += len;
    return str;
}

static XVariant* xtrReadVariant(XSettingsTlvReader* r)
{
    uint8_t typeByte = 0;
    if (!xtrReadBytes(r, &typeByte, 1))
        return NULL;
    switch (typeByte) {
    case XVariantType_NULL:
        return XVariant_create_null();
    case XVariantType_Bool: {
        uint8_t byte = 0;
        if (!xtrReadBytes(r, &byte, 1))
            return NULL;
        return XVariant_create_bool(byte != 0);
    }
    case XVariantType_Int8: {
        uint8_t byte = 0;
        if (!xtrReadBytes(r, &byte, 1))
            return NULL;
        return XVariant_create_int8((int8_t)byte);
    }
    case XVariantType_Uint8: {
        uint8_t byte = 0;
        if (!xtrReadBytes(r, &byte, 1))
            return NULL;
        return XVariant_create_uint8(byte);
    }
    case XVariantType_Char: {
        uint8_t byte = 0;
        if (!xtrReadBytes(r, &byte, 1))
            return NULL;
        return XVariant_create_char((char)byte);
    }
    case XVariantType_UChar: {
        uint8_t byte = 0;
        if (!xtrReadBytes(r, &byte, 1))
            return NULL;
        return XVariant_create_uchar(byte);
    }
    case XVariantType_Int16:
    case XVariantType_Uint16:
    case XVariantType_Int32:
    case XVariantType_Uint32:
    case XVariantType_Int:
    case XVariantType_Size_t: {
        uint32_t u = xtrReadU32(r);
        if (!r->ok)
            return NULL;
        if (typeByte == XVariantType_Int16) return XVariant_create_int16((int16_t)u);
        if (typeByte == XVariantType_Uint16) return XVariant_create_uint16((uint16_t)u);
        if (typeByte == XVariantType_Int32) return XVariant_create_int32((int32_t)u);
        if (typeByte == XVariantType_Uint32) return XVariant_create_uint32(u);
        if (typeByte == XVariantType_Int) return XVariant_create_int((int)u);
        return XVariant_create_size_t((size_t)u);
    }
    case XVariantType_Int64: {
        int64_t v = xtrReadI64(r);
        return r->ok ? XVariant_create_int64(v) : NULL;
    }
    case XVariantType_Uint64: {
        int64_t v = xtrReadI64(r);
        return r->ok ? XVariant_create_uint64((uint64_t)v) : NULL;
    }
    case XVariantType_Float:
    case XVariantType_Double: {
        int64_t bits = xtrReadI64(r);
        if (!r->ok)
            return NULL;
        double d;
        uint64_t u = (uint64_t)bits;
        XMemcpy(&d, &u, sizeof(d));
        return typeByte == XVariantType_Float ? XVariant_create_float((float)d) : XVariant_create_double(d);
    }
    case XVariantType_String: {
        XString* str = xtrReadString(r);
        if (!r->ok || !str)
            return NULL;
        XVariant* variant = XString_toVariant_move(str);   /* 变体接管字符串 */
        XClassDelete(str);   /* move 后源壳仍须释放 */
        return variant;
    }
    case XVariantType_ByteArray: {
        uint32_t len = xtrReadU32(r);
        if (!r->ok || r->len - r->pos < len) {
            r->ok = false;
            return NULL;
        }
        XByteArray* array = XByteArray_create_with_data((const char*)r->data + r->pos, len);
        r->pos += len;
        if (!array)
            return NULL;
        XVariant* variant = XByteArray_toVariant_move(array);
        XClassDelete(array);   /* move 后源壳仍须释放 */
        return variant;
    }
    case XVariantType_StringList: {
        uint32_t count = xtrReadU32(r);
        XStringList* list = r->ok ? XStringList_create() : NULL;
        if (!list) {
            r->ok = false;
            return NULL;
        }
        bool ok = true;
        for (uint32_t i = 0; ok && i < count; ++i) {
            XString* item = xtrReadString(r);
            ok = r->ok && item != NULL;
            if (item) {
                XStringList_push_back_move_base(list, item);   /* move 后源壳仍须释放 */
                XClassDelete(item);
            }
        }
        if (!ok) {
            XClassDelete(list);
            return NULL;
        }
        XVariant* variant = XStringList_toVariant_move(list);
        XClassDelete(list);   /* move 后源壳仍须释放 */
        return variant;
    }
    case XVariantType_Point: {
        uint32_t x = xtrReadU32(r);
        uint32_t y = xtrReadU32(r);
        if (!r->ok)
            return NULL;
        XPoint point;
        XPoint_init(&point, (int)x, (int)y);
        return XPoint_toVariant(point);
    }
    case XVariantType_Size: {
        uint32_t w = xtrReadU32(r);
        uint32_t h = xtrReadU32(r);
        if (!r->ok)
            return NULL;
        XSize size;
        XSize_init(&size, (int)w, (int)h);
        return XSize_toVariant(size);
    }
    case XVariantType_Rect: {
        uint32_t v[4];
        for (int i = 0; i < 4; ++i)
            v[i] = xtrReadU32(r);
        if (!r->ok)
            return NULL;
        XRect rect;
        XRect_init(&rect, (int)v[0], (int)v[1], (int)v[2], (int)v[3]);
        return XRect_toVariant(rect);
    }
    case XVariantType_Date: {
        int64_t jd = xtrReadI64(r);
        if (!r->ok)
            return NULL;
        XDate date = XDate_create();
        date.m_jd = jd;
        return XDate_toVariant(&date);
    }
    case XVariantType_Time: {
        uint32_t msecs = xtrReadU32(r);
        if (!r->ok)
            return NULL;
        XTime time = XTime_create();
        time.m_msecs = (msecs == 0xFFFFFFFFu) ? -1 : (int)msecs;
        return XTime_toVariant(&time);
    }
    case XVariantType_DateTime: {
        int64_t jd = xtrReadI64(r);
        uint32_t msecs = xtrReadU32(r);
        if (!r->ok)
            return NULL;
        XDateTime dateTime;
        if (jd == -1 || msecs == 0xFFFFFFFFu) {
            dateTime = XDateTime_create();
        }
        else {
            XDate date = XDate_create();
            date.m_jd = jd;
            XTime time = XTime_create();
            time.m_msecs = (int)msecs;
            dateTime = XDateTime_create_datetime(date, time);
        }
        return XDateTime_toVariant(&dateTime);
    }
    case XVariantType_List: {
        uint32_t count = xtrReadU32(r);
        XVariantList* list = r->ok ? XVariantList_create() : NULL;
        if (!list) {
            r->ok = false;
            return NULL;
        }
        bool ok = true;
        for (uint32_t i = 0; ok && i < count; ++i) {
            XVariant* item = xtrReadVariant(r);
            ok = r->ok && item != NULL;
            if (item) {
                XVariantList_push_back_move_base(list, item);   /* move 后源壳仍须释放 */
                XClassDelete(item);
            }
        }
        if (!ok) {
            XClassDelete(list);
            return NULL;
        }
        XVariant* variant = XVariantList_toVariant_move(list);
        XClassDelete(list);   /* move 后源壳仍须释放 */
        return variant;
    }
    case XVariantType_Map:
    case XVariantType_Hash: {
        uint32_t count = xtrReadU32(r);
        /* 值语义 map（XVariantMap 工厂）：局部对象深拷贝入表，整表自动释放 */
        XMap* map = r->ok ? XMap_create_XVariantMap() : NULL;
        if (!map) {
            r->ok = false;
            return NULL;
        }
        bool ok = true;
        for (uint32_t i = 0; ok && i < count; ++i) {
            XString* key = xtrReadString(r);
            XVariant* value = r->ok ? xtrReadVariant(r) : NULL;
            ok = r->ok && key != NULL && value != NULL;
            if (ok) {
                /* 值语义：局部对象深拷贝入表，用后即 deinit */
                XString keyObj;
                XString_init(&keyObj);
                XString_assign(&keyObj, key);
                XVariant valueObj;
                XVariant_init(&valueObj, NULL, 0, XVariantType_NULL);
                XVariant_setValue(&valueObj, value);
                ok = XMapBase_insert_base(map, &keyObj, &valueObj);
                XClassDeinit(&keyObj);
                XClassDeinit(&valueObj);
            }
            if (key)
                XClassDelete(key);
            if (value)
                XClassDelete(value);
        }
        if (!ok) {
            XClassDelete(map);   /* 内联对象由注册方法自动释放 */
            return NULL;
        }
        XVariant* variant = XMap_toVariant_move(map);
        XClassDelete(map);   /* move 后源壳仍须释放 */
        return variant;
    }
    default:
        r->ok = false;
        return NULL;
    }
}

/** @brief 前缀 + base64(TLV 载荷) + ')' 组装。调用方释放。 */
static XString* xsettingsPrefixedPayload(const char* prefix, const XByteArray* payload)
{
    XByteArray* base64 = XByteArray_toBase64((XByteArray*)payload);
    if (!base64)
        return NULL;
    size_t n = XByteArray_size_base(base64);
    uint8_t* raw = XByteArray_data(base64);
    /* base64 文本以 NUL 结尾时，XContainerSize 可能计入终止符：按首个
     * NUL 截断，避免把 '\0' 写进 @Variant(...) 载荷（读端会当控制字符）。 */
    for (size_t i = 0; i < n; ++i) {
        if (raw[i] == 0) {
            n = i;
            break;
        }
    }
    XString* out = XString_create_utf8(prefix);
    if (out) {
        for (size_t i = 0; i < n && out; ++i) {
            if (!XString_append_char(out, (XChar)raw[i])) {
                XClassDelete(out);
                out = NULL;
            }
        }
        if (out && !XString_append_char(out, (XChar)')')) {
            XClassDelete(out);
            out = NULL;
        }
    }
    XClassDelete(base64);
    return out;
}

/** @brief TLV 字节 → @Variant(...)/@DateTime(...) 文本。调用方释放。 */
static XString* xsettingsTlvToPrefixed(const char* prefix, const XVariant* variant)
{
    XByteArray* payload = xsettingsEncodeTlv(variant);
    if (!payload)
        return NULL;
    XString* out = xsettingsPrefixedPayload(prefix, payload);
    XClassDelete(payload);
    return out;
}

/** @brief base64 内容 → TLV 解码；失败返回 NULL。 */
static XVariant* xsettingsTlvFromContent(const XString* content)
{
    size_t size = XString_size(content);
    XByteArray* raw = XByteArray_create();
    if (!raw)
        return NULL;
    for (size_t i = 0; i < size; ++i) {
        XChar ch = XString_at(content, i);
        if ((uint32_t)ch > 0xFF) {
            XClassDelete(raw);
            return NULL;
        }
        if (!XByteArray_append_1(raw, (uint8_t)ch)) {
            XClassDelete(raw);
            return NULL;
        }
    }
    XByteArray* bytes = XByteArray_fromBase64(raw);
    XClassDelete(raw);
    if (!bytes) {
        return NULL;
    }
    XSettingsTlvReader reader;
    reader.data = (const uint8_t*)XByteArray_data(bytes);
    reader.len = XByteArray_size_base(bytes);
    /* 反解结果可能含尾部 NUL 终止符（容器 size 计入口径差异）：按首个
     * 能完整消费的前缀长度判定，避免多 1 字节被判为"未消费完"而丢值。 */
    reader.pos = 0;
    reader.ok = true;
    XVariant* variant = xtrReadVariant(&reader);
    bool fullyConsumed = reader.ok && reader.pos <= reader.len;
    for (size_t pad = reader.pos; pad < reader.len; ++pad) {
        if (reader.data[pad] != 0) {
            fullyConsumed = false;
            break;
        }
    }
    XClassDelete(bytes);
    if (!fullyConsumed) {
        if (variant)
            XClassDelete(variant);
        return NULL;
    }
    return variant;
}

/* ==========================================================================
 * 数字最短往返文本（对标 QString::number(x,'g',QLocale::FloatingPointShortest)）
 * ========================================================================== */

XString* xsettingsNumberShortestDouble(double value)
{
    XString* out = NULL;
    for (int precision = 1; precision <= 17; ++precision) {
        XString* candidate = XString_number_double(value, 'g', precision);
        if (!candidate)
            continue;
        bool ok = false;
        double roundTrip = XString_toDouble(candidate, &ok);
        if (ok && roundTrip == value) {
            out = candidate;
            break;
        }
        XClassDelete(candidate);
    }
    if (!out)
        out = XString_number_double(value, 'g', 17);
    if (!out)
        return NULL;
    /* 指数归一：剥多余前导零，保符号 + 至少两位（Qt 口径 "1e+20"） */
    const char* bytes = XString_toUtf8(out);
    size_t len = bytes ? XStrlen(bytes) : 0;
    int64_t epos = -1;
    for (size_t i = 0; i < len; ++i) {
        if (bytes[i] == 'e' || bytes[i] == 'E') {
            epos = (int64_t)i;
            break;
        }
    }
    if (epos < 0 || epos + 2 >= (int64_t)len)
        return out;   /* 无指数：原样返回 */
    size_t digitsStart = (size_t)epos + 2;   /* 'e' + 符号之后 */
    size_t digitsEnd = digitsStart;
    while (digitsEnd < len && bytes[digitsEnd] >= '0' && bytes[digitsEnd] <= '9')
        ++digitsEnd;
    size_t digitCount = digitsEnd - digitsStart;
    if (digitCount <= 2)
        return out;
    size_t firstSignificant = digitsStart;
    while (firstSignificant < digitsEnd - 2 && bytes[firstSignificant] == '0')
        ++firstSignificant;
    if (firstSignificant == digitsStart)
        return out;   /* 无可剥前导零 */
    XSettingsByteBuf fixed;
    xbb_init(&fixed);
    bool ok = xbb_append(&fixed, bytes, digitsStart) &&
              xbb_append(&fixed, bytes + firstSignificant, digitsEnd - firstSignificant) &&
              xbb_append(&fixed, bytes + digitsEnd, len - digitsEnd);
    XClassDelete(out);
    out = ok ? xbb_toString(&fixed) : NULL;
    xbb_free(&fixed);
    return out;
}

/* ==========================================================================
 * 类型前缀：变体 ↔ 文本（对标 variantToString 371-451 / stringToVariant 454-507）
 * ========================================================================== */

XString* xsettingsVariantToString(const XVariant* variant)
{
    if (!variant || variant->m_type == XVariantType_NULL)
        return XString_create_utf8("@Invalid()");
    switch (variant->m_type) {
    case XVariantType_ByteArray: {
        /* @ByteArray(原始字节 Latin1 视角 + )，逐字节保真 */
        XByteArray* array = XByteArray_fromVariant_ref((XVariant*)variant);
        size_t n = array ? XByteArray_size_base(array) : 0;
        uint8_t* raw = array ? XByteArray_data(array) : NULL;
        XString* out = XString_create_utf8("@ByteArray(");
        if (!out)
            return NULL;
        for (size_t i = 0; i < n; ++i) {
            if (!XString_append_char(out, (XChar)raw[i])) {
                XClassDelete(out);
                return NULL;
            }
        }
        if (!XString_append_char(out, (XChar)')')) {
            XClassDelete(out);
            return NULL;
        }
        return out;
    }
    case XVariantType_String: {
        XString* str = XString_fromVariant_ref((XVariant*)variant);
        bool hasNul = false;
        size_t size = str ? XString_size(str) : 0;
        for (size_t i = 0; i < size; ++i) {
            if (XString_at(str, i) == (XChar)'\0') {
                hasNul = true;
                break;
            }
        }
        XString* out = XString_create();
        if (!out)
            return NULL;
        bool ok = true;
        if (hasNul) {
            /* 含 U+0000 → @String(...) */
            ok = XString_append_utf8(out, "@String(") &&
                 (!str || XString_append_utf8(out, XString_toUtf8(str))) &&
                 XString_append_char(out, (XChar)')');
        }
        else {
            /* 以 @ 开头 → 前置一个 @（@@ 双写，读端剥一层） */
            if (size > 0 && XString_at(str, 0) == (XChar)'@')
                ok = XString_append_char(out, (XChar)'@');
            if (ok && str)
                ok = XString_append_utf8(out, XString_toUtf8(str));
        }
        if (!ok) {
            XClassDelete(out);
            return NULL;
        }
        return out;
    }
    case XVariantType_Bool:
        return XString_create_utf8(XVariant_toBool(variant) ? "true" : "false");
    case XVariantType_Int:
    case XVariantType_Int8:
    case XVariantType_Int16:
    case XVariantType_Int32:
    case XVariantType_Int64:
        return XString_number_llong(XVariant_toInt64(variant), 10);
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Uint64:
    case XVariantType_Size_t:
        return XString_number_llong((long long)XVariant_toUint64(variant), 10);
    case XVariantType_Float:
    case XVariantType_Double:
        return xsettingsNumberShortestDouble(XVariant_toDouble(variant));
    case XVariantType_Point: {
        XPoint point = XPoint_fromVariant(variant);
        return XString_create_fmt_utf8("@Point(%d %d)", point.x, point.y);
    }
    case XVariantType_Size: {
        XSize size = XSize_fromVariant(variant);
        return XString_create_fmt_utf8("@Size(%d %d)", size.width, size.height);
    }
    case XVariantType_Rect: {
        XRect rect = XRect_fromVariant(variant);
        return XString_create_fmt_utf8("@Rect(%d %d %d %d)", rect.x, rect.y, rect.width, rect.height);
    }
    case XVariantType_StringList: {
        /* 字符串列表：逐元素为纯字符串，", " 连接（经 escapedStringList 落盘） */
        XStringList* list = XStringList_fromVariant_ref((XVariant*)variant);
        XSettingsByteBuf buf;
        xbb_init(&buf);
        bool ok = xsettingsEscapedStringList(list, &buf);
        XString* out = ok ? xbb_toString(&buf) : NULL;
        xbb_free(&buf);
        return out;
    }
    case XVariantType_List: {
        /* 通用列表：QVariantList size==1 按单值写（Qt 规避坑位），否则逐元素 */
        XVariantList* list = (XVariantList*)XVariant_toRef((XVariant*)variant, XVariantType_List);
        size_t count = list ? XVariantList_size_base(list) : 0;
        if (count == 1) {
            XVariant* single = (XVariant*)XVariantList_at_base(list, 0);   /* 值槽内联对象借用 */
            return xsettingsVariantToString(single);
        }
        XStringList* texts = XStringList_create();
        if (!texts)
            return NULL;
        bool ok = true;
        for (size_t i = 0; ok && i < count; ++i) {
            XVariant* item = (XVariant*)XVariantList_at_base(list, i);   /* 值槽内联对象借用 */
            XString* text = xsettingsVariantToString(item);
            if (text) {
                XStringList_push_back_move_base(texts, text);   /* move 后源壳仍须释放 */
                XClassDelete(text);
            }
            else
                ok = false;
        }
        if (!ok) {
            XClassDelete(texts);
            return NULL;
        }
        XSettingsByteBuf buf;
        xbb_init(&buf);
        ok = xsettingsEscapedStringList(texts, &buf);
        XString* out = ok ? xbb_toString(&buf) : NULL;
        xbb_free(&buf);
        XClassDelete(texts);
        return out;
    }
    case XVariantType_DateTime:
        return xsettingsTlvToPrefixed("@DateTime(", variant);
    default:
        /* Map/Hash 等其余可编码类型走 @Variant；Ptr/Pair/Json/Bson/User 不可
         * 编码 → @Invalid()（文件头 @details 裁剪边界） */
        if (xsettingsTlvEncodable(variant->m_type))
            return xsettingsTlvToPrefixed("@Variant(", variant);
        return XString_create_utf8("@Invalid()");
    }
}

/** @brief "(a b c)" 内容按空格切参（对标 splitArgs）；空 token 跳过。 */
static XStringList* xsettingsSplitArgs(const XString* content)
{
    XStringList* args = XStringList_create();
    if (!args)
        return NULL;
    size_t size = XString_size(content);
    size_t i = 0;
    while (i < size) {
        while (i < size && XString_at(content, i) == (XChar)' ')
            ++i;
        size_t start = i;
        while (i < size && XString_at(content, i) != (XChar)' ')
            ++i;
        if (i > start) {
            XString* arg = XString_mid(content, start, i - start);
            if (!arg || !XStringList_push_back_move_base(args, arg)) {
                if (arg) XClassDelete(arg);
                XClassDelete(args);
                return NULL;
            }
            XClassDelete(arg);   /* move 后源壳仍须释放 */
        }
    }
    return args;
}

/** @brief 从 @Rect/@Size/@Point 文本构造几何变体；参数数不符返回 NULL（顺延）。 */
static XVariant* xsettingsGeometryVariant(const XString* s, const char* prefix, int expected)
{
    if (!XString_startsWith_utf8(s, prefix, XChar_CaseSensitive))
        return NULL;
    size_t size = XString_size(s);
    size_t plen = XStrlen(prefix);
    if (size < plen + 2 || XString_at(s, size - 1) != (XChar)')')
        return NULL;
    XString* content = XString_mid(s, plen, size - plen - 1);
    if (!content)
        return NULL;
    XStringList* args = xsettingsSplitArgs(content);
    XClassDelete(content);
    if (!args)
        return NULL;
    size_t count = XStringList_size_base(args);
    if ((int)count != expected) {
        XClassDelete(args);
        return NULL;
    }
    int values[4] = { 0, 0, 0, 0 };
    bool ok = true;
    for (int i = 0; i < expected; ++i) {
        const XString* arg = XStringList_at_base(args, (size_t)i);
        values[i] = arg ? XString_toInt(arg, &ok, 10) : 0;
        if (!ok)
            break;
    }
    XClassDelete(args);
    if (!ok)
        return NULL;
    if (expected == 4) {
        XRect rect;
        XRect_init(&rect, values[0], values[1], values[2], values[3]);
        return XRect_toVariant(rect);
    }
    if (XStrlen(prefix) == 6) {   /* "@Size(" */
        XSize size;
        XSize_init(&size, values[0], values[1]);
        return XSize_toVariant(size);
    }
    XPoint point;
    XPoint_init(&point, values[0], values[1]);
    return XPoint_toVariant(point);
}

XVariant* xsettingsStringToVariant(const XString* s)
{
    if (!s || XString_size(s) == 0 || XString_at(s, 0) != (XChar)'@')
        return s ? XString_toVariant(s) : NULL;
    size_t size = XString_size(s);
    if (size > 0 && XString_at(s, size - 1) == (XChar)')') {
        if (XString_startsWith_utf8(s, "@ByteArray(", XChar_CaseSensitive)) {
            /* 内容 Latin1 视角还原字节（>0xFF 码元映射为 '?'，对标 toLatin1） */
            XByteArray* array = XByteArray_create();
            if (!array)
                return NULL;
            for (size_t i = 11; i < size - 1; ++i) {
                XChar ch = XString_at(s, i);
                if (!XByteArray_append_1(array, (uint8_t)((uint32_t)ch > 0xFF ? '?' : (uint8_t)ch))) {
                    XClassDelete(array);
                    return NULL;
                }
            }
            XVariant* variant = XByteArray_toVariant_move(array);
            XClassDelete(array);   /* move 后源壳仍须释放 */
            return variant;
        }
        if (XString_startsWith_utf8(s, "@String(", XChar_CaseSensitive)) {
            XString* content = XString_mid(s, 8, size - 9);
            if (!content)
                return NULL;
            XVariant* variant = XString_toVariant_move(content);
            XClassDelete(content);   /* move 后源壳仍须释放 */
            return variant;
        }
        if (XString_startsWith_utf8(s, "@Variant(", XChar_CaseSensitive) ||
            XString_startsWith_utf8(s, "@DateTime(", XChar_CaseSensitive)) {
            size_t offset = XString_startsWith_utf8(s, "@DateTime(", XChar_CaseSensitive) ? 10 : 9;
            XString* content = XString_mid(s, offset, size - offset - 1);
            if (!content)
                return NULL;
            XVariant* variant = xsettingsTlvFromContent(content);
            XClassDelete(content);
            /* 解码失败按 Qt 口径产出无效变体（而非回退普通串） */
            return variant ? variant : XVariant_create_null();
        }
        {
            XVariant* geometry = xsettingsGeometryVariant(s, "@Rect(", 4);
            if (!geometry)
                geometry = xsettingsGeometryVariant(s, "@Size(", 2);
            if (!geometry)
                geometry = xsettingsGeometryVariant(s, "@Point(", 2);
            if (geometry)
                return geometry;
        }
        if (size == 10 && XString_equals_utf8(s, "@Invalid()", XChar_CaseSensitive))
            return XVariant_create_null();
    }
    if (XString_startsWith_utf8(s, "@@", XChar_CaseSensitive)) {
        /* @@ 双写 → 剥一层 @ 还原普通字符串 */
        XString* content = XString_mid(s, 1, size - 1);
        if (!content)
            return NULL;
        XVariant* variant = XString_toVariant_move(content);
        XClassDelete(content);   /* move 后源壳仍须释放 */
        return variant;
    }
    return XString_toVariant(s);
}

XVariant* xsettingsStringListToVariantList(const XStringList* list)
{
    if (!list)
        return NULL;
    size_t count = XStringList_size_base(list);
    /* 降级判定：任一元素以 '@' 开头且（长度<2 或第二字符 != '@'） */
    bool degrade = false;
    for (size_t i = 0; i < count; ++i) {
        const XString* item = XStringList_at_base(list, i);
        if (item && XString_size(item) > 0 && XString_at(item, 0) == (XChar)'@') {
            if (XString_size(item) < 2 || XString_at(item, 1) != (XChar)'@') {
                degrade = true;
                break;
            }
        }
    }
    if (!degrade) {
        /* 合成 StringList：'@@' 元素剥一层 '@' */
        XStringList* clean = XStringList_create();
        if (!clean)
            return NULL;
        for (size_t i = 0; i < count; ++i) {
            const XString* item = XStringList_at_base(list, i);
            if (item && XString_size(item) >= 2 && XString_at(item, 0) == (XChar)'@' &&
                XString_at(item, 1) == (XChar)'@') {
                XString* stripped = XString_mid(item, 1, XString_size(item) - 1);
                if (!stripped || !XStringList_push_back_move_base(clean, stripped)) {
                    if (stripped) XClassDelete(stripped);
                    XClassDelete(clean);
                    return NULL;
                }
                XClassDelete(stripped);   /* move 后源壳仍须释放 */
            }
            else if (item) {
                XString* copy = XString_create_copy(item);
                if (!copy || !XStringList_push_back_move_base(clean, copy)) {
                    if (copy) XClassDelete(copy);
                    XClassDelete(clean);
                    return NULL;
                }
                XClassDelete(copy);   /* move 后源壳仍须释放 */
            }
        }
        XVariant* variant = XStringList_toVariant_move(clean);
        XClassDelete(clean);   /* move 后源壳仍须释放 */
        return variant;
    }
    /* 逐元素 stringToVariant → List 变体 */
    XVariantList* outList = XVariantList_create();
    if (!outList)
        return NULL;
    for (size_t i = 0; i < count; ++i) {
        const XString* item = XStringList_at_base(list, i);
        XVariant* value = item ? xsettingsStringToVariant(item) : NULL;
        if (!value || !XVariantList_push_back_move_base(outList, value)) {
            if (value) XClassDelete(value);
            XClassDelete(outList);
            return NULL;
        }
    }
    XVariant* variant = XVariantList_toVariant_move(outList);
    XClassDelete(outList);   /* move 后源壳仍须释放 */
    return variant;
}

/* ==========================================================================
 * INI 行扫描（对标 readIniLine qsettings.cpp:1553-1620）
 * ========================================================================== */

/** @brief 消费 pos 处的换行（\n、\r 或 \r\n/\n\r 对）。 */
static size_t xsettingsConsumeNewline(const uint8_t* data, size_t size, size_t pos)
{
    if (pos >= size)
        return pos;
    char ch = (char)data[pos];
    if (ch == '\n' || ch == '\r') {
        ++pos;
        if (pos < size && ((ch == '\n' && data[pos] == '\r') || (ch == '\r' && data[pos] == '\n')))
            ++pos;
    }
    return pos;
}

bool xsettingsReadIniLine(const uint8_t* data, size_t size, size_t* dataPosInOut,
                          size_t* lineStartOut, size_t* lineLenOut, int64_t* equalsPosOut)
{
    size_t pos = *dataPosInOut;
    if (pos >= size)
        return false;
restart:
    /* 跳过行首空白（Space = '\t' '\n' '\r' ' '，对标 charTraits） */
    while (pos < size && (data[pos] == ' ' || data[pos] == '\t' ||
                          data[pos] == '\n' || data[pos] == '\r'))
        ++pos;
    if (pos >= size) {
        *dataPosInOut = pos;
        return false;
    }
    size_t start = pos;
    int64_t equalsPos = -1;
    bool inQuotes = false;
    bool haveContent = false;
    size_t i = pos;
    while (i < size) {
        char ch = (char)data[i];
        if (ch == '"') {
            inQuotes = !inQuotes;
            haveContent = true;
            ++i;
        }
        else if (ch == '\\') {
            /* 行续接：吞反斜杠及下一字符；下一字符为换行时连 \r\n 对一并吞 */
            haveContent = true;
            ++i;
            if (i < size) {
                char nx = (char)data[i];
                if (nx == '\n' || nx == '\r') {
                    i = xsettingsConsumeNewline(data, size, i);
                }
                else {
                    ++i;
                }
            }
        }
        else if (ch == '=') {
            if (!inQuotes && equalsPos < 0)
                equalsPos = (int64_t)i;
            haveContent = true;
            ++i;
        }
        else if (ch == '\n' || ch == '\r') {
            if (inQuotes) {
                haveContent = true;   /* 引号内换行是值的一部分（跨物理行） */
                ++i;
            }
            else {
                break;   /* 行结束（换行由本函数消费） */
            }
        }
        else if (ch == ';') {
            if (!haveContent) {
                /* 行首 ';'：整行注释透明吞掉（跳到物理行尾并消费换行） */
                while (i < size && data[i] != '\n' && data[i] != '\r')
                    ++i;
                i = xsettingsConsumeNewline(data, size, i);
                pos = i;                    /* 关键：restart 必须从新位置继续，
                                             * 否则会从旧 pos 重复扫描同一注释行
                                             * 而死循环（2026-11-02 修复） */
                *dataPosInOut = i;
                goto restart;
            }
            if (!inQuotes)
                break;   /* 引号外 ';'：行在此截断，';' 留给下次调用 */
            haveContent = true;
            ++i;
        }
        else {
            if (ch != ' ' && ch != '\t')
                haveContent = true;
            ++i;
        }
    }
    if (i == start && !inQuotes) {
        /* 纯空行：透明吞掉换行后重启（pos 必须同步推进，否则死循环） */
        i = xsettingsConsumeNewline(data, size, i);
        pos = i;
        *dataPosInOut = i;
        goto restart;
    }
    {
        /* 行内容到非引号换行为止；换行由本函数消费，不计入行内容 */
        size_t contentEnd = i;
        if (i < size && (data[i] == '\n' || data[i] == '\r') && !inQuotes)
            i = xsettingsConsumeNewline(data, size, i);
        *dataPosInOut = i;
        *lineStartOut = start;
        *lineLenOut = contentEnd - start;
        *equalsPosOut = equalsPos;
    }
    return true;
}

/* ==========================================================================
 * 节解析（对标 readIniFile qsettings.cpp:1627-1692）
 * ========================================================================== */

/** @brief 字节段两端裁空白（' ' 与 '\t'）。 */
static bool xsettingsTrimSlice(const uint8_t* data, size_t start, size_t end,
                               size_t* trimStart, size_t* trimEnd)
{
    while (start < end && (data[start] == ' ' || data[start] == '\t'))
        ++start;
    while (end > start && (data[end - 1] == ' ' || data[end - 1] == '\t'))
        --end;
    *trimStart = start;
    *trimEnd = end;
    return end > start;
}

/** @brief 节块落表（map 深拷贝键值；块为空也插入条目，保证节存在性语义）。 */
static bool xsettingsFlushSection(XMap* sections, const XString* sectionName,
                                  const XSettingsByteBuf* block)
{
    if (!sections || !sectionName)
        return false;
    /* 同名节多次出现（如 [General] 分散在全文件）必须合并成同一节块，
     * 否则后段块覆盖前段（Qt readIniFile 的 FLUSH 语义为追加合并）。 */
    if (XMapBase_contains(sections, &sectionName)) {
        void* slot = XMapBase_value_base(sections, &sectionName);
        XByteArray* existing = slot ? *(XByteArray**)slot : NULL;
        if (!existing)
            return false;
        if (block->len == 0)
            return true;
        /* 以换行连接新旧块：直接往既有块尾部追加 */
        if (XByteArray_size_base(existing) > 0) {
            if (!XByteArray_append_1(existing, (uint8_t)'\n'))
                return false;
        }
        for (size_t i = 0; i < block->len; ++i) {
            if (!XByteArray_append_1(existing, (uint8_t)block->data[i]))
                return false;
        }
        return true;
    }
    XByteArray* payload = XByteArray_create_with_data(block->data, block->len);
    if (!payload)
        return false;
    /* xsettingsReadIniSections 持有 map 与条目所有权：直接以裸 insert 移交 */
    XString* keyCopy = XString_create_copy(sectionName);
    if (!keyCopy) {
        XClassDelete(payload);
        return false;
    }
    if (!XMapBase_insert_base(sections, &keyCopy, &payload)) {
        XClassDelete(keyCopy);
        XClassDelete(payload);
        return false;
    }
    return true;
}

bool xsettingsReadIniSections(const uint8_t* data, size_t size, XMap* sectionsOut, bool* okOut)
{
    if (okOut)
        *okOut = true;
    if (!sectionsOut)
        return false;
    size_t pos = 0;
    /* 跳过 UTF-8 BOM（对标 1651-1653） */
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        pos = 3;
    XString* currentSection = XString_create();
    XSettingsByteBuf block;
    xbb_init(&block);
    if (!currentSection) {
        xbb_free(&block);
        return false;
    }
    size_t lineStart = 0;
    size_t lineLen = 0;
    int64_t equalsPos = 0;
    bool ok = true;
    while (xsettingsReadIniLine(data, size, &pos, &lineStart, &lineLen, &equalsPos)) {
        if (lineLen > 0 && data[lineStart] == '[') {
            /* 先落上一节（对标 FLUSH_CURRENT_SECTION） */
            if (!xsettingsFlushSection(sectionsOut, currentSection, &block)) {
                ok = false;
                break;
            }
            XString_clear_base(currentSection);
            xbb_free(&block);
            xbb_init(&block);
            /* 节头解析：找 ']' */
            size_t bracket = lineStart;
            for (size_t i = lineStart; i < lineStart + lineLen; ++i) {
                if (data[i] == ']') {
                    bracket = i;
                    break;
                }
            }
            if (bracket == lineStart || data[bracket] != ']') {
                /* 缺 ']'：置 FormatError，整行按内容行归入当前节 */
                if (okOut)
                    *okOut = false;
                if (!xbb_append(&block, (const char*)data + lineStart, lineLen))
                    ok = false;
                continue;
            }
            size_t nameStart = 0;
            size_t nameEnd = 0;
            if (!xsettingsTrimSlice(data, lineStart + 1, bracket, &nameStart, &nameEnd)) {
                /* 空节名按 [General] 语义（对标 iniSection.trimmed 空值容错） */
                continue;
            }
            size_t nameLen = nameEnd - nameStart;
            const char* nameBytes = (const char*)data + nameStart;
            if (xsettingsAsciiEqualsInsensitive(nameBytes, nameLen, "general")) {
                /* [General]/[GENERAL] → 根节（不加 '/'） */
                continue;
            }
            if (nameLen > 1 && nameBytes[0] == '%' &&
                xsettingsAsciiEqualsInsensitive(nameBytes + 1, nameLen - 1, "general")) {
                /* [%General] → 组前缀 "General/"：剥掉 '%' 转义标记，只保留
                 * 原拼写组名（对齐 Qt：currentSection = iniSection.mid(1) + '/'） */
                XString* sectionText = XString_create_with_length_utf8(nameBytes + 1, nameLen - 1);
                if (!sectionText) {
                    ok = false;
                    continue;
                }
                XString_append_utf8(currentSection, XString_toUtf8(sectionText));
                XClassDelete(sectionText);
                XString_append_char(currentSection, (XChar)'/');
                continue;
            }
            XString* unescaped = xsettingsUnescapedKey(nameBytes, nameLen);
            if (!unescaped) {
                ok = false;
                continue;
            }
            XString_append_utf8(currentSection, XString_toUtf8(unescaped));
            XString_append_char(currentSection, (XChar)'/');
            XClassDelete(unescaped);
        }
        else if (lineLen > 0) {
            /* 内容行：以 '\n' 连接进当前节原始块 */
            if (block.len > 0 && !xbb_append_char(&block, '\n')) {
                ok = false;
                break;
            }
            if (!xbb_append(&block, (const char*)data + lineStart, lineLen)) {
                ok = false;
                break;
            }
        }
        /* 空逻辑行透明 */
    }
    /* 文件结束：落最后一节 */
    if (ok && !xsettingsFlushSection(sectionsOut, currentSection, &block))
        ok = false;
    XClassDelete(currentSection);
    xbb_free(&block);
    return ok;
}

/* ==========================================================================
 * INI 文件写出（对标 writeIniFile qsettings.cpp:1782-1868，统一非 Windows 口径）
 * ========================================================================== */

/** @brief 写出条目：转义全键 + 首个 '/' 切分（节名段/剩余段）。 */
typedef struct XSettingsIniEntry
{
    size_t index;          /**< 原始数组下标。 */
    char* section;         /**< 转义节名段（无 '/' 时为 ""，堆分配）。 */
    size_t sectionLen;     /**< 节名段长度。 */
    char* rest;            /**< 转义剩余段（堆分配）。 */
    size_t restLen;        /**< 剩余段长度。 */
} XSettingsIniEntry;

/** @brief 按节名字典序 + 剩余段字典序插入排序（键序确定性输出）。 */
static void xsettingsIniSort(XSettingsIniEntry* entries, size_t n)
{
    for (size_t i = 1; i < n; ++i) {
        XSettingsIniEntry key = entries[i];
        size_t j = i;
        while (j > 0) {
            XSettingsIniEntry* prev = &entries[j - 1];
            int cmp;
            if (prev->sectionLen != key.sectionLen)
                cmp = prev->sectionLen < key.sectionLen ? -1 : 1;
            else
                cmp = (prev->sectionLen == 0) ? 0 : XMemcmp(prev->section, key.section, key.sectionLen);
            if (cmp == 0) {
                if (prev->restLen != key.restLen)
                    cmp = prev->restLen < key.restLen ? -1 : 1;
                else
                    cmp = (prev->restLen == 0) ? 0 : XMemcmp(prev->rest, key.rest, key.restLen);
            }
            if (cmp <= 0)
                break;
            entries[j] = entries[j - 1];
            --j;
        }
        entries[j] = key;
    }
}

/** @brief 变体 → 转义后的值字节（StringList/List 走列表通道，其余单值）。 */
static bool xsettingsEscapedValue(const XVariant* variant, XSettingsByteBuf* out)
{
    if (variant && variant->m_type == XVariantType_List) {
        XVariantList* list = (XVariantList*)XVariant_toRef((XVariant*)variant, XVariantType_List);
        size_t count = list ? XVariantList_size_base(list) : 0;
        if (count == 1) {
            /* Qt 坑位规避：单元素 QVariantList 按单值写 */
            return xsettingsEscapedValue((XVariant*)XVariantList_at_base(list, 0), out);
        }
        XStringList* texts = XStringList_create();
        if (!texts)
            return false;
        bool ok = true;
        for (size_t i = 0; ok && i < count; ++i) {
            XVariant* item = (XVariant*)XVariantList_at_base(list, i);   /* 值槽内联对象借用 */
            XString* text = xsettingsVariantToString(item);
            if (text) {
                XStringList_push_back_move_base(texts, text);   /* move 后源壳仍须释放 */
                XClassDelete(text);
            }
            else
                ok = false;
        }
        if (ok)
            ok = xsettingsEscapedStringList(texts, out);
        XClassDelete(texts);
        return ok;
    }
    if (variant && variant->m_type == XVariantType_StringList) {
        XStringList* list = XStringList_fromVariant_ref((XVariant*)variant);
        return xsettingsEscapedStringList(list, out);
    }
    XString* text = xsettingsVariantToString(variant);
    if (!text)
        return false;
    bool ok = xsettingsEscapedString(text, out);
    XClassDelete(text);
    return ok;
}

bool xsettingsWriteIniFile(XString** keys, XVariant** values, size_t n, XSettingsByteBuf* out)
{
    if (!out)
        return false;
    if (n > 0 && (!keys || !values))
        return false;
    XSettingsIniEntry* entries = NULL;
    XSettingsByteBuf* escapedKeys = NULL;
    if (n > 0) {
        entries = (XSettingsIniEntry*)XMalloc_System(n * sizeof(XSettingsIniEntry));
        escapedKeys = (XSettingsByteBuf*)XMalloc_System(n * sizeof(XSettingsByteBuf));
        if (!entries || !escapedKeys) {
            if (entries) XFree_System(entries);
            if (escapedKeys) XFree_System(escapedKeys);
            return false;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        xbb_init(&escapedKeys[i]);
        if (!keys[i] || !xsettingsEscapedKey(keys[i], &escapedKeys[i])) {
            for (size_t k = 0; k <= i; ++k)
                xbb_free(&escapedKeys[k]);
            XFree_System(entries);
            XFree_System(escapedKeys);
            return false;
        }
        /* 首个 '/' 切分（转义形态中 '/' 只剩真实分隔符，'\\' 是被转义的 '/'） */
        size_t slash = escapedKeys[i].len;
        for (size_t p = 0; p < escapedKeys[i].len; ++p) {
            if (escapedKeys[i].data[p] == '/') {
                slash = p;
                break;
            }
        }
        entries[i].index = i;
        entries[i].section = escapedKeys[i].data;           /* 借用 escapedKeys 存储 */
        if (slash < escapedKeys[i].len) {
            entries[i].sectionLen = slash;
            entries[i].rest = escapedKeys[i].data + slash + 1;
            entries[i].restLen = escapedKeys[i].len - slash - 1;
        }
        else {
            /* 无 '/'：整键入 [General] 节 */
            entries[i].sectionLen = 0;
            entries[i].rest = escapedKeys[i].data;
            entries[i].restLen = escapedKeys[i].len;
        }
    }
    xsettingsIniSort(entries, n);
    bool ok = true;
    const char* currentSection = NULL;
    size_t currentSectionLen = 0;
    bool firstSection = true;
    for (size_t i = 0; ok && i < n; ++i) {
        XSettingsIniEntry* entry = &entries[i];
        bool sectionChanged = !currentSection ||
                              currentSectionLen != entry->sectionLen ||
                              (entry->sectionLen > 0 && XMemcmp(currentSection, entry->section, entry->sectionLen) != 0);
        if (sectionChanged) {
            if (!firstSection && !xbb_append_char(out, '\n'))
                ok = false;   /* 节间空行 */
            if (ok) {
                if (!xbb_append_char(out, '['))
                    ok = false;
            }
            if (ok) {
                if (entry->sectionLen == 0) {
                    ok = xbb_append_str(out, "General");
                }
                else if (xsettingsAsciiEqualsInsensitive(entry->section, entry->sectionLen, "general")) {
                    ok = xbb_append_str(out, "%General");
                }
                else {
                    ok = xbb_append(out, entry->section, entry->sectionLen);
                }
            }
            if (ok)
                ok = xbb_append_str(out, "]\n");
            currentSection = entry->section;
            currentSectionLen = entry->sectionLen;
            firstSection = false;
        }
        if (ok)
            ok = xbb_append(out, entry->rest, entry->restLen) &&
                 xbb_append_char(out, '=') &&
                 xsettingsEscapedValue(values[entry->index], out) &&
                 xbb_append_char(out, '\n');
    }
    for (size_t i = 0; i < n; ++i)
        xbb_free(&escapedKeys[i]);
    XFree_System(entries);
    XFree_System(escapedKeys);
    return ok;
}
