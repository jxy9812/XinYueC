/**
 * @file       XKeySequence.c
 * @brief      XKeyCombination / XKeySequence 键序列值类型实现。
 * @details    文本编解码与键名表逐条对齐 Qt 6.8.3：
 *             - `QKeySequencePrivate::encodeString`（qkeysequence.cpp:
 *               1200-1265）的修饰位输出顺序 Meta → Ctrl → Alt → Shift → Num；
 *             - `QKeySequencePrivate::keyName`（qkeysequence.cpp:1268-1317）
 *               的单字符大写 / F1..F35 / keyname 表；
 *             - `QKeySequencePrivate::decodeString`（qkeysequence.cpp:
 *               1040-1200）的小写化 + '+' 分段修饰名判定；
 *             - `QKeySequence::matches`（qkeysequence.cpp:1335-1354）；
 *             - `QKeySequence::mnemonic`（qkeysequence.cpp:935-967）。
 * @author     XinYueC 团队
 */
#if XGUI_ON

#include "XKeySequence.h"
#include "XStringUtils.h"
#include "XAlgorithm.h"
#include "XMemory.h"

#if XWIDGET_ON

/* ==================== 内部工具 ==================== */

/** @brief ASCII 小写化（只处理 A-Z；Qt 的 `toLower()` 对本表内的键名等价）。 */
static char xks_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

/** @brief 大小写不敏感比较（ASCII；长度由调用方保证）。 */
static bool xks_equalsIgnoreCase(const char* a, size_t aLen,
                                 const char* b)
{
    size_t i;
    size_t bLen;
    if (!a || !b) return false;
    bLen = XStrlen(b);
    if (aLen != bLen) return false;
    for (i = 0; i < aLen; ++i) {
        if (xks_lower(a[i]) != xks_lower(b[i])) return false;
    }
    return true;
}

/**
 * @brief Qt keyname 表（qkeysequence.cpp:389-... 的可翻译键名子集）。
 * @details 只收录 XKey 枚举已有的项（XEvent.h 的 XKey 到 F35 为止，无
 *          multimedia/launcher 键）；顺序与 Qt 表一致，便于逐项对拍。
 */
static const struct
{
    int key;
    const char* name;
} kXksKeyNames[] = {
    { XKey_Space,        "Space" },
    { XKey_Escape,       "Esc" },
    { XKey_Tab,          "Tab" },
    { XKey_Backtab,      "Backtab" },
    { XKey_Backspace,    "Backspace" },
    { XKey_Return,       "Return" },
    { XKey_Enter,        "Enter" },
    { XKey_Insert,       "Ins" },
    { XKey_Delete,       "Del" },
    { XKey_Pause,        "Pause" },
    { XKey_Print,        "Print" },
    { XKey_SysReq,       "SysReq" },
    { XKey_Home,         "Home" },
    { XKey_End,          "End" },
    { XKey_Left,         "Left" },
    { XKey_Up,           "Up" },
    { XKey_Right,        "Right" },
    { XKey_Down,         "Down" },
    { XKey_PageUp,       "PgUp" },
    { XKey_PageDown,     "PgDown" },
    { XKey_CapsLock,     "CapsLock" },
    { XKey_NumLock,      "NumLock" },
    { XKey_ScrollLock,   "ScrollLock" },
    { XKey_Clear,        "Clear" }
};

/** @brief 键名表条目数。 */
#define XKS_KEYNAME_COUNT \
    ((int)(sizeof(kXksKeyNames) / sizeof(kXksKeyNames[0])))

/** @brief 十进制数字串转 int（Qt `QString::toInt()` 的受限等价）。 */
static bool xks_parseInt(const char* s, size_t len, int* out)
{
    size_t i;
    int value = 0;
    if (!s || !out || len == 0 || len > 4) return false;
    for (i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + (int)(s[i] - '0');
    }
    *out = value;
    return true;
}

/* ==================== 组合（XKeyCombination） ==================== */

XKeyCombination XKeyCombination_empty(void)
{
    XKeyCombination c;
    c.modifiers = XKeyboardModifier_NoModifier;
    c.key = 0;
    return c;
}

XKeyCombination XKeyCombination_make(XKeyboardModifiers modifiers, int key)
{
    XKeyCombination c;
    c.modifiers = modifiers;
    c.key = key;
    return c;
}

bool XKeyCombination_isEmpty(const XKeyCombination* self)
{
    if (!self) return true;
    return self->key == 0 && self->modifiers == XKeyboardModifier_NoModifier;
}

bool XKeyCombination_isEqual(const XKeyCombination* a,
                             const XKeyCombination* b)
{
    if (!a && !b) return true;
    if (!a || !b) {
        /* NULL 视作空组合（与 Qt 的默认构造 QKeyCombination 同值）。 */
        const XKeyCombination empty = XKeyCombination_empty();
        return XKeyCombination_isEqual(a ? a : &empty, b ? b : &empty);
    }
    return a->key == b->key && a->modifiers == b->modifiers;
}

/* ==================== 序列（XKeySequence） ==================== */

XKeySequence XKeySequence_empty(void)
{
    XKeySequence s;
    XMemset(&s, 0, sizeof(s));
    return s;
}

XKeySequence XKeySequence_fromCombination(XKeyCombination combination)
{
    XKeySequence s = XKeySequence_empty();
    if (XKeyCombination_isEmpty(&combination))
        return s;
    s.combos[0] = combination;
    s.count = 1;
    return s;
}

XKeySequence XKeySequence_fromKey(XKeyboardModifiers modifiers, int key)
{
    return XKeySequence_fromCombination(
        XKeyCombination_make(modifiers, key));
}

int XKeySequence_count(const XKeySequence* self)
{
    if (!self) return 0;
    if (self->count < 0) return 0;
    if (self->count > XKEYSEQUENCE_MAX_LENGTH) return XKEYSEQUENCE_MAX_LENGTH;
    return self->count;
}

bool XKeySequence_isEmpty(const XKeySequence* self)
{
    return XKeySequence_count(self) == 0;
}

XKeyCombination XKeySequence_at(const XKeySequence* self, int index)
{
    if (!self || index < 0 || index >= XKeySequence_count(self))
        return XKeyCombination_empty();
    return self->combos[index];
}

int XKeySequence_firstKey(const XKeySequence* self)
{
    return XKeySequence_at(self, 0).key;
}

bool XKeySequence_isEqual(const XKeySequence* a, const XKeySequence* b)
{
    int n;
    int i;
    if (!a && !b) return true;
    n = XKeySequence_count(a);
    if (n != XKeySequence_count(b)) return false;
    for (i = 0; i < n; ++i) {
        if (!XKeyCombination_isEqual(&a->combos[i], &b->combos[i]))
            return false;
    }
    return true;
}

XKeySequenceMatch XKeySequence_matches(const XKeySequence* self,
                                       const XKeySequence* seq)
{
    int userN = XKeySequence_count(self);
    int seqN = XKeySequence_count(seq);
    XKeySequenceMatch match;
    int i;
    /* 逐条对齐 qkeysequence.cpp:1337-1353。 */
    if (userN > seqN)
        return XKeySequenceMatch_NoMatch;
    match = (userN == seqN ? XKeySequenceMatch_ExactMatch
                           : XKeySequenceMatch_PartialMatch);
    for (i = 0; i < userN; ++i) {
        if (!XKeyCombination_isEqual(&self->combos[i], &seq->combos[i]))
            return XKeySequenceMatch_NoMatch;
    }
    return match;
}

/* ==================== 键名 ==================== */

int XKeySequence_keyName(int key, char* out, size_t cap)
{
    char tmp[32];
    size_t len = 0;
    int i;
    tmp[0] = '\0';
    if (key && key < (int)XKey_Escape && key != (int)XKey_Space) {
        /* Qt: `p = QChar::fromUcs2(key).toUpper()`（qkeysequence.cpp:
         * 1274-1275）。ASCII 走 XToUpper；>0x7e 的可打印码位原样输出
         * （本库无完整 Unicode 大小写表，与 XLabel 的助记符口径一致）。 */
        int upper = XToUpper(key);
        if (upper >= 0x20 && upper <= 0x7e) {
            tmp[0] = (char)upper;
            tmp[1] = '\0';
            len = 1;
        } else if (key >= 0x20) {
            /* 非 ASCII 可打印码位：按 UTF-8 编码写出。 */
            uint32_t cp = (uint32_t)key;
            if (cp < 0x800u) {
                tmp[len++] = (char)(0xC0u | (cp >> 6));
                tmp[len++] = (char)(0x80u | (cp & 0x3Fu));
            } else if (cp < 0x10000u) {
                tmp[len++] = (char)(0xE0u | (cp >> 12));
                tmp[len++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                tmp[len++] = (char)(0x80u | (cp & 0x3Fu));
            } else {
                tmp[len++] = (char)(0xF0u | (cp >> 18));
                tmp[len++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
                tmp[len++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
                tmp[len++] = (char)(0x80u | (cp & 0x3Fu));
            }
            tmp[len] = '\0';
        }
    } else if (key >= (int)XKey_F1 && key <= (int)XKey_F35) {
        /* Qt: "F%1".arg(key - Qt::Key_F1 + 1)（qkeysequence.cpp:1276-1278）。 */
        int n = key - (int)XKey_F1 + 1;
        char digits[8];
        int d = 0;
        char rev[8];
        int r = 0;
        while (n > 0 && d < 7) {
            rev[d++] = (char)('0' + (n % 10));
            n /= 10;
        }
        while (d > 0)
            digits[r++] = rev[--d];
        digits[r] = '\0';
        tmp[len++] = 'F';
        {
            int k;
            for (k = 0; k < r; ++k)
                tmp[len++] = digits[k];
        }
        tmp[len] = '\0';
    } else if (key) {
        for (i = 0; i < XKS_KEYNAME_COUNT; ++i) {
            if (kXksKeyNames[i].key == key) {
                size_t n = XStrlen(kXksKeyNames[i].name);
                if (n > sizeof(tmp) - 1) n = sizeof(tmp) - 1;
                XMemcpy(tmp, kXksKeyNames[i].name, n);
                tmp[n] = '\0';
                len = n;
                break;
            }
        }
        /* Qt 的回落：查不到 keyname 时用 Unicode 大写码位（见上方分支
         * 同款逻辑）；本库对未知功能键直接输出 "?"（与既有
         * XKeySequenceEdit 的 xks_keyName 口径一致，避免把 0x01xxxxxx
         * 这种伪码位当字符写出去）。 */
        if (len == 0) {
            tmp[0] = '?';
            tmp[1] = '\0';
            len = 1;
        }
    }
    if (out && cap > 0) {
        size_t n = len < cap - 1 ? len : cap - 1;
        XMemcpy(out, tmp, n);
        out[n] = '\0';
    }
    return (int)len;
}

int XKeySequence_keyFromName(const char* nameUtf8, size_t length)
{
    int i;
    if (!nameUtf8 || length == 0) return 0;
    /* 单字符：大写码位（Qt decodeString 的 `accelRef.at(0).toUpper()`）。 */
    if (length == 1) {
        unsigned char c = (unsigned char)nameUtf8[0];
        if (c >= 0x20) return XToUpper((int)c);
        return 0;
    }
    /* f1..f35（Qt：`accelRef.at(0) == 'f' && (fnum = mid(1).toInt()) >= 1`）。 */
    if (xks_lower(nameUtf8[0]) == 'f') {
        int n = 0;
        if (xks_parseInt(&nameUtf8[1], length - 1, &n) && n >= 1 && n <= 35)
            return (int)XKey_F1 + n - 1;
    }
    for (i = 0; i < XKS_KEYNAME_COUNT; ++i) {
        if (xks_equalsIgnoreCase(nameUtf8, length, kXksKeyNames[i].name))
            return kXksKeyNames[i].key;
    }
    return 0;
}

/* ==================== 文本编解码 ==================== */

/** @brief 追加文本（容量安全，返回新的写入长度）。 */
static size_t xks_append(char* out, size_t cap, size_t used,
                         const char* text)
{
    size_t n;
    if (!text) return used;
    n = XStrlen(text);
    if (out && cap > 0 && used < cap - 1) {
        size_t room = cap - 1 - used;
        size_t copy = n < room ? n : room;
        XMemcpy(out + used, text, copy);
        out[used + copy] = '\0';
    }
    return used + n;
}

int XKeySequence_toString(const XKeySequence* self, XKeySequenceFormat format,
                          char* out, size_t cap)
{
    size_t used = 0;
    int n;
    int i;
    (void)format; /* NativeText/PortableText 当前输出相同（见头文件 @note）。 */
    if (out && cap > 0) out[0] = '\0';
    n = XKeySequence_count(self);
    for (i = 0; i < n; ++i) {
        XKeyCombination c = self->combos[i];
        char keyName[32];
        if (i > 0)
            used = xks_append(out, cap, used, ", ");
        /* 修饰位顺序 Meta → Ctrl → Alt → Shift（qkeysequence.cpp:
         * 1236-1250 的 encodeString；Mac 分支不适用本平台）。 */
        if (c.modifiers & XKeyboardModifier_MetaModifier)
            used = xks_append(out, cap, used, "Meta+");
        if (c.modifiers & XKeyboardModifier_ControlModifier)
            used = xks_append(out, cap, used, "Ctrl+");
        if (c.modifiers & XKeyboardModifier_AltModifier)
            used = xks_append(out, cap, used, "Alt+");
        if (c.modifiers & XKeyboardModifier_ShiftModifier)
            used = xks_append(out, cap, used, "Shift+");
        if (c.modifiers & XKeyboardModifier_KeypadModifier)
            used = xks_append(out, cap, used, "Num+");
        XKeySequence_keyName(c.key, keyName, sizeof(keyName));
        used = xks_append(out, cap, used, keyName);
    }
    return (int)used;
}

/**
 * @brief 解析单段组合（`Ctrl+Shift+A`）；失败返回 false 且 *out 置空组合。
 * @details 逐条对齐 Qt `QKeySequencePrivate::decodeString`
 *          （qkeysequence.cpp:1098-1188）的**分隔符定位**：
 *          `p = accel.lastIndexOf('+', accel.size() - 2)`——搜索起点是
 *          `size-2`（不是 `size-1`），这正是 "Ctrl++" 能把最后的 '+'
 *          当**键本身**的原因；找不到 '+' 时整段都是键名（无修饰位）。
 *          找到分隔符后，其左侧按 '+' 切段且每段必须是修饰名，右侧整段
 *          是键名（经 keyName 逆解析）。
 */
static bool xks_parseCombination(const char* text, size_t length,
                                 XKeyCombination* out)
{
    XKeyboardModifiers modifiers = XKeyboardModifier_NoModifier;
    size_t sep = (size_t)-1;
    size_t k;
    int key = 0;
    if (!out) return false;
    *out = XKeyCombination_empty();
    if (!text || length == 0) return false;

    /* Qt: `lastIndexOf('+', size - 2)`——**搜索起点是 size-2**（长度 1
     * 时不存在分隔符）。这条正是 "Ctrl++" 能把最后的 '+' 当键本身的原因：
     * 起点是倒数第二个字符，故末尾的 '+' 不会被选作分隔符。 */
    if (length >= 2) {
        k = length - 2;
        for (;;) {
            if (text[k] == '+') { sep = k; break; }
            if (k == 0) break;
            --k;
        }
    }

    if (sep == (size_t)-1) {
        /* 无分隔符：整段是键名，无修饰位。 */
        key = XKeySequence_keyFromName(text, length);
        if (key == 0) return false;
        out->modifiers = XKeyboardModifier_NoModifier;
        out->key = key;
        return true;
    }

    /* 分隔符左侧：按 '+' 切段，每段必须是修饰名。 */
    {
        size_t start = 0;
        size_t i;
        for (i = 0; i <= sep; ++i) {
            if (i != sep && text[i] != '+') continue;
            {
                size_t segLen = i - start;
                const char* seg = text + start;
                if (segLen == 0)
                    return false; /* "++A"/"+A" 之类：空修饰段非法。 */
                if (xks_equalsIgnoreCase(seg, segLen, "ctrl") ||
                    xks_equalsIgnoreCase(seg, segLen, "control"))
                    modifiers |= XKeyboardModifier_ControlModifier;
                else if (xks_equalsIgnoreCase(seg, segLen, "shift"))
                    modifiers |= XKeyboardModifier_ShiftModifier;
                else if (xks_equalsIgnoreCase(seg, segLen, "alt"))
                    modifiers |= XKeyboardModifier_AltModifier;
                else if (xks_equalsIgnoreCase(seg, segLen, "meta") ||
                         xks_equalsIgnoreCase(seg, segLen, "super"))
                    modifiers |= XKeyboardModifier_MetaModifier;
                else if (xks_equalsIgnoreCase(seg, segLen, "num"))
                    modifiers |= XKeyboardModifier_KeypadModifier;
                else
                    return false; /* 非修饰名 ⇒ 整段非法（Qt 返回 Key_unknown）。 */
            }
            start = i + 1;
        }
    }

    /* 分隔符右侧：整段键名。 */
    key = XKeySequence_keyFromName(text + sep + 1, length - sep - 1);
    if (key == 0) return false;
    out->modifiers = modifiers;
    out->key = key;
    return true;
}

/** @brief 跳过首尾空白（Qt 的 QString::split(',') 不去空白，但其
 *         `assign` 用 `splitRef(',')` 后逐段 trim 处理；本库同口径 trim）。 */
static void xks_trim(const char** text, size_t* length)
{
    const char* s;
    size_t n;
    if (!text || !length || !*text) return;
    s = *text;
    n = *length;
    while (n > 0 && (s[0] == ' ' || s[0] == '\t')) {
        ++s;
        --n;
    }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        --n;
    *text = s;
    *length = n;
}

bool XKeySequence_fromString(const char* utf8, XKeySequence* out)
{
    size_t length;
    size_t start = 0;
    size_t i;
    int count = 0;
    if (out) *out = XKeySequence_empty();
    if (!out || !utf8) return false;
    length = XStrlen(utf8);
    if (length == 0) return false;
    /* 按 ',' 切成最多 4 段（Qt 的 `assign` 同样以 ',' 分隔多组）。 */
    for (i = 0; i <= length; ++i) {
        if (i == length || utf8[i] == ',') {
            const char* seg = &utf8[start];
            size_t segLen = i - start;
            XKeyCombination combo;
            xks_trim(&seg, &segLen);
            if (segLen > 0 &&
                xks_parseCombination(seg, segLen, &combo)) {
                if (count < XKEYSEQUENCE_MAX_LENGTH) {
                    out->combos[count] = combo;
                    ++count;
                }
            }
            start = i + 1;
        }
    }
    out->count = count;
    return count > 0;
}

XKeySequence XKeySequence_mnemonic(const char* textUtf8)
{
    XKeySequence result = XKeySequence_empty();
    const char* p;
    if (!textUtf8) return result;
    p = textUtf8;
    while (*p) {
        if (*p != '&') {
            ++p;
            continue;
        }
        ++p;
        if (*p == '\0')
            break;
        if (*p == '&') {
            /* `&&` = 字面 '&'，跳过（Qt mnemonic 的 `if (text.at(p) != '&')`）。 */
            ++p;
            continue;
        }
        {
            /* 解码一个 UTF-8 码位（Qt `QChar c = text.at(p); c.isPrint()`）。 */
            unsigned char c0 = (unsigned char)p[0];
            uint32_t cp = c0;
            int extra = 0;
            int k;
            if ((c0 & 0xE0u) == 0xC0u) { cp = c0 & 0x1Fu; extra = 1; }
            else if ((c0 & 0xF0u) == 0xE0u) { cp = c0 & 0x0Fu; extra = 2; }
            else if ((c0 & 0xF8u) == 0xF0u) { cp = c0 & 0x07u; extra = 3; }
            for (k = 0; k < extra; ++k) {
                unsigned char cc = (unsigned char)p[1 + k];
                if ((cc & 0xC0u) != 0x80u) {
                    cp = 0xFFFDu;
                    extra = k;
                    break;
                }
                cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
            }
            if (cp >= 0x21u && cp != 0x7Fu) { /* isPrint 的实用近似 */
                /* Qt：`c.toUpper()` 后配 Alt 修饰位（qkeysequence.cpp:952-953）。 */
                int upper;
                if (cp >= (uint32_t)'a' && cp <= (uint32_t)'z')
                    upper = (int)(cp - (uint32_t)'a' + (uint32_t)'A');
                else
                    upper = (int)cp;
                return XKeySequence_fromKey(
                    XKeyboardModifier_AltModifier, upper);
            }
            p += extra + 1;
        }
    }
    return result;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
