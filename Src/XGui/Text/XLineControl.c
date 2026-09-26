/**
 * @file       XLineControl.c
 * @brief      XLineControl 私有文本控制器实现（Qt 6.8
 *             QWidgetLineControl 的 C99 逐方法移植）。
 * @details    与基准 qwidgetlinecontrol.cpp 逐函数对照：文本变更走
 *             "变更命令入撤销栈 → finishChange 校验/回滚/信号结算" 的
 *             Qt 同款两阶段通路；显示文本由回显状态机
 *             （Normal/NoEcho/Password/PasswordEchoOnEdit）派生；输入
 *             掩码以槽位表（分隔符/可编辑位/大小写模式）驱动
 *             maskString/clearString/stripString/findInMask 四件套；
 *             撤销栈为命令差量栈（Insert/Remove/Delete/选区删除/
 *             SetSelection/Separator），按字输入分组、按删除段分组，
 *             密码模式下撤销退化为清空（安全约束）。位置一律为 UTF-8
 *             字节偏移（恒在字符边界），Qt 代理对成对处理分支一一映射
 *             为 UTF-8 续字节成对处理；度量经 XPainter 字体度量接口
 *             （整数像素，Qt 为 qreal，口径在头部注明）。
 * @note       依赖：XMemory（唯一内存入口）、XTextUtf8（码点边界）、
 *             XEvent（XKeyEvent/XTimerEvent）、XWindowEvent
 *             （XInputMethodEvent）、XGuiApplication（inputMethod/
 *             clipboard/styleHints/font，不可用时逐点退化为 Qt 空对象
 *             语义）、XPainter（度量与绘制）、XCompleter（补全联动）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XLineControl.h"

#include "XMemory.h"
#include "XTextClipboard.h"
#include "XTextUtf8.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XStringUtils.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
#include "XClipboard.h"
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
#if XPAINTER_ON
#include "XPainter.h"
#endif /* XPAINTER_ON */
#if XWIDGET_ON && XTABLEWIDGET_ON
#include "XCompleter.h"
#define XLC_COMPLETER_ON 1
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

/* 剪贴板子系统裁剪时的模式常量兜底（XGUI_ON=0 巡检，值口径与
   XClipboard.h 枚举一致：Clipboard=0/Selection=1；键位分流仅以整数
   传参 copy/paste，§8.0g9）。 */
#if !XCLIPBOARD_ON
#define XClipboardMode_Clipboard 0
#define XClipboardMode_Selection 1
#endif /* !XCLIPBOARD_ON */

#if XLINECONTROL_ON

/* ==================== 常量 ==================== */

/** @brief 默认最大字符数（对标 Qt 构造默认 32767）。 */
#define XLC_DEFAULT_MAX_LENGTH 32767
/** @brief 密码模式文本缓冲预留字符数（对标 m_text.reserve(30)）。 */
#define XLC_PASSWORD_RESERVE_CHARS 30
/** @brief 键盘导航长按 Back 清空延时 ms（对标 startTimer(750)）。 */
#define XLC_DELETE_ALL_DELAY_MS 750
/** @brief 光标包围盒左右包络（对标 QRect(cix-5,0,w+9,ch)）。 */
#define XLC_CURSOR_RECT_PAD 5
/** @brief 默认绘制四色（Qt fusion 亮色系近似；接入层可覆盖）。 */
#define XLC_COLOR_HIGHLIGHT       0xFF308CC6u
#define XLC_COLOR_HIGHLIGHTEDTEXT 0xFFFFFFFFu
#define XLC_COLOR_TEXT            0xFF000000u
#define XLC_COLOR_WINDOW          0xFFFFFFFFu

/* ==================== 前置声明（构建器依赖编码器） ==================== */
static int xlc_utf8EncodeCp(uint32_t cp, char out[5]);

/* ==================== 内部字符串构建器 ==================== */

/** @brief 动态 UTF-8 串构建器（NUL 结尾；对标 QString 拼接面）。 */
typedef struct xlc_str
{
    char* s;    /**< 缓冲（XMemory 系统分配器）。 */
    int   len;  /**< 有效字节长（不含 NUL）。 */
    int   cap;  /**< 容量（含 NUL 位）。 */
} xlc_str;

/** @brief 置空构建器（不分配；配合 xlc_strFree 安全释放）。 */
static void xlc_strInit(xlc_str* b)
{
    if (!b) return;
    b->s = NULL;
    b->len = 0;
    b->cap = 0;
}

/** @brief 释放构建器缓冲。 */
static void xlc_strFree(xlc_str* b)
{
    if (!b) return;
    if (b->s) { XFree_System(b->s); b->s = NULL; }
    b->len = 0;
    b->cap = 0;
}

/** @brief 确保容量 >= needed+1 并维持 NUL 终止；失败置空返回 false。 */
static bool xlc_strReserve(xlc_str* b, int needed)
{
    int cap;
    char* ns;
    if (!b) return false;
    if (needed < 0) needed = 0;
    if (b->s && b->cap > needed) return true;
    cap = needed + 1;
    if (cap < 16) cap = 16;
    if (cap < 32) cap = 32;
    if (b->cap > 0 && cap < b->cap * 2) cap = b->cap * 2;
    ns = (char*)XRealloc_System(b->s, (size_t)cap);
    if (!ns) return false;
    b->s = ns;
    b->cap = cap;
    b->s[b->len] = '\0';
    return true;
}

/** @brief 追加一段字节。 */
static bool xlc_strAppendBytes(xlc_str* b, const char* s, int n)
{
    if (!b || n < 0) return false;
    if (n == 0) return true;
    if (!s || !xlc_strReserve(b, b->len + n)) return false;
    XMemcpy(b->s + b->len, s, (size_t)n);
    b->len += n;
    b->s[b->len] = '\0';
    return true;
}

/** @brief 追加单个 UTF-8 字符（原样字节）。 */
static bool xlc_strAppendUtf8(xlc_str* b, const char* s, int seqLen)
{ return xlc_strAppendBytes(b, s, seqLen); }

/** @brief 追加一个码点（编码为 UTF-8）。 */
static bool xlc_strAppendCp(xlc_str* b, uint32_t cp)
{
    char buf[5];
    int n = xlc_utf8EncodeCp(cp, buf);
    if (n <= 0) return false;
    return xlc_strAppendBytes(b, buf, n);
}

/** @brief 追加 char（ASCII/UTF-8 单字节）。 */
static bool xlc_strAppendCh(xlc_str* b, char c)
{ return xlc_strAppendBytes(b, &c, 1); }

/* ==================== UTF-8 辅助（对标 QString 的码元/代理对运算） ==================== */

/** @brief 判断字节是否为 UTF-8 续字节（对标 QChar::isLowSurrogate 场景判定）。 */
static bool xlc_isContByte(uint8_t b)
{ return (b & 0xC0) == 0x80; }

/**
 * @brief 计算 pos 处 UTF-8 字符的字节长（1~4；越界/截断钳位）。
 * @note  对标 QString 的码元步进；pos>=len 返回 0。
 */
static int xlc_seqLenAt(const char* s, int len, int pos)
{
    if (!s || pos < 0 || pos >= len) return 0;
    return XTextUtf8_seqLen(s + pos, len - pos);
}

/**
 * @brief 解码 pos 处码点；seqLen 输出字节长（可 NULL）。
 * @return 码点值；非法输入返回 0 并按 1 字节消耗。
 */
static uint32_t xlc_decodeAt(const char* s, int len, int pos, int* seqLen)
{
    uint8_t b0;
    uint32_t cp;
    int n;
    if (seqLen) *seqLen = 1;
    if (!s || pos < 0 || pos >= len) return 0u;
    b0 = (uint8_t)s[pos];
    if (b0 < 0x80u) return (uint32_t)b0;
    n = xlc_seqLenAt(s, len, pos);
    if (n <= 1) return (uint32_t)b0;
    switch (n) {
    case 2:
        cp = (uint32_t)(b0 & 0x1Fu);
        break;
    case 3:
        cp = (uint32_t)(b0 & 0x0Fu);
        break;
    default:
        cp = (uint32_t)(b0 & 0x07u);
        break;
    }
    {
        int i;
        for (i = 1; i < n; ++i) {
            uint8_t bi = (uint8_t)s[pos + i];
            if (!xlc_isContByte(bi)) { if (seqLen) *seqLen = i; return cp; }
            cp = (cp << 6) | (uint32_t)(bi & 0x3Fu);
        }
    }
    if (seqLen) *seqLen = n;
    return cp;
}

/** @brief 编码码点为 UTF-8（out 至少 5 字节）；返回字节数（1~4；非法 0）。 */
static int xlc_utf8EncodeCp(uint32_t cp, char out[5])
{
    if (!out) return 0;
    if (cp < 0x80u) { out[0] = (char)cp; out[1] = '\0'; return 1; }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
        return 3;
    }
    if (cp <= 0x10FFFFu) {
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        out[4] = '\0';
        return 4;
    }
    out[0] = '\0';
    return 0;
}

/** @brief 字符 pos 的下一个字符边界（对标 nextCursorPosition(SkipCharacters)）。 */
static int xlc_nextBoundary(const char* s, int len, int pos)
{ (void)len; return (int)XTextUtf8_nextBoundary(s, (size_t)pos); }

/** @brief 字符 pos 的前一个字符边界（对标 previousCursorPosition(SkipCharacters)）。 */
static int xlc_prevBoundary(const char* s, int len, int pos)
{ (void)len; return (int)XTextUtf8_prevBoundary(s, (size_t)pos); }

/** @brief 统计 [0,len) 内的字符数。 */
static int xlc_countChars(const char* s, int len)
{
    int pos = 0, count = 0;
    if (!s || len <= 0) return 0;
    while (pos < len) {
        int n = xlc_seqLenAt(s, len, pos);
        if (n <= 0) break;
        pos += n;
        ++count;
    }
    return count;
}

/** @brief 前 maxChars 个字符的字节长（对标 QString::left(n).size()）。 */
static int xlc_charsByteLen(const char* s, int len, int maxChars)
{
    int pos = 0, count = 0;
    if (!s || len <= 0 || maxChars <= 0) return 0;
    while (pos < len && count < maxChars) {
        int n = xlc_seqLenAt(s, len, pos);
        if (n <= 0) break;
        pos += n;
        ++count;
    }
    return pos;
}

/** @brief pos 之前 n 个字符的字节长（用于负向选区/回退）。 */
static int xlc_prevCharsByteLen(const char* s, int pos, int n)
{
    int p = pos;
    int count = 0;
    if (!s || pos <= 0 || n <= 0) return 0;
    while (p > 0 && count < n) {
        int pb = xlc_prevBoundary(s, pos, p);
        if (pb >= p) break; /* 防御：无法继续回退。 */
        p = pb;
        ++count;
    }
    return pos - p;
}

/* ==================== 字符分类（对标 QChar 判定位；ASCII 精确） ==================== */

/**
 * @brief 判断空白（对标 QChar::isSpace；逐项 Unicode 空白表）。
 */
static bool xlc_isSpaceCp(uint32_t cp)
{
    switch (cp) {
    case 0x09u: case 0x0Au: case 0x0Bu: case 0x0Cu: case 0x0Du:
    case 0x20u: case 0x85u: case 0xA0u: case 0x1680u:
    case 0x2000u: case 0x2001u: case 0x2002u: case 0x2003u:
    case 0x2004u: case 0x2005u: case 0x2006u: case 0x2007u:
    case 0x2008u: case 0x2009u: case 0x200Au:
    case 0x2028u: case 0x2029u: case 0x202Fu: case 0x205Fu: case 0x3000u:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 判断字母（对标 QChar::isLetter）。
 * @note  无 Unicode 类别表：ASCII 精确；>=0x80 一律按字母近似
 *        （覆盖 CJK/西文输入场景；ASCII 掩码场景不受影响）。
 */
static bool xlc_isLetterCp(uint32_t cp)
{
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) return true;
    return cp >= 0x80u;
}

/**
 * @brief 判断数字（对标 QChar::isNumber；Nd 类近似）。
 * @note  ASCII 数字精确；另含全角数字 0xFF10..0xFF19。
 */
static bool xlc_isNumberCp(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') return true;
    return cp >= 0xFF10u && cp <= 0xFF19u;
}

/** @brief 判断字母或数字（对标 QChar::isLetterOrNumber）。 */
static bool xlc_isLetterOrNumberCp(uint32_t cp)
{ return xlc_isLetterCp(cp) || xlc_isNumberCp(cp); }

/**
 * @brief 判断可打印（对标 QChar::isPrint）。
 * @note  无类别表：>=0x20 且非 DEL 即可打印（>=0x80 全可打印近似）。
 */
static bool xlc_isPrintCp(uint32_t cp)
{ return cp >= 0x20u && cp != 0x7Fu; }

/** @brief 数字值（对标 QChar::digitValue；-1 表示非数字）。 */
static int xlc_digitValueCp(uint32_t cp)
{
    if (cp >= '0' && cp <= '9') return (int)(cp - '0');
    if (cp >= 0xFF10u && cp <= 0xFF19u) return (int)(cp - 0xFF10u);
    return -1;
}

/**
 * @brief 判断词分隔符（对标 QTextEngine::atWordSeparator 字符表，逐项一致）。
 */
static bool xlc_isWordSeparatorCp(uint32_t cp)
{
    switch (cp) {
    case '.': case ',': case '?': case '!': case '@': case '#':
    case '$': case ':': case ';': case '-': case '<': case '>':
    case '[': case ']': case '(': case ')': case '{': case '}':
    case '=': case '/': case '+': case '%': case '&': case '^':
    case '*': case '\'': case '"': case '`': case '~': case '|':
    case '\\':
        return true;
    default:
        return false;
    }
}

/** @brief 大写化（对标 QChar::toUpper；ASCII 精确，其余原样）。 */
static uint32_t xlc_toUpperCp(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z') return cp - 'a' + 'A';
    return cp;
}

/** @brief 小写化（对标 QChar::toLower；ASCII 精确，其余原样）。 */
static uint32_t xlc_toLowerCp(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') return cp - 'A' + 'a';
    return cp;
}

/* ==================== 定容缓冲（对标 QString 的 reserve/扩容/NUL 终止） ==================== */

/** @brief 保证缓冲容量 >= needed+1；失败时保持原状返回 false。 */
static bool xlc_bufReserve(char** buf, int* cap, int needed)
{
    int newCap;
    char* ns;
    if (!buf || !cap || needed < 0) return false;
    if (*buf && *cap > needed) return true;
    newCap = needed + 1;
    if (newCap < 32) newCap = 32;
    if (*cap > 0 && newCap < *cap * 2) newCap = *cap * 2;
    ns = (char*)XRealloc_System(*buf, (size_t)newCap);
    if (!ns) return false;
    *buf = ns;
    *cap = newCap;
    return true;
}

/** @brief 释放缓冲并清零三 元组。 */
static void xlc_bufFree(char** buf, int* len, int* cap)
{
    if (buf && *buf) { XFree_System(*buf); *buf = NULL; }
    if (len) *len = 0;
    if (cap) *cap = 0;
}

/** @brief 拷贝 n 字节进缓冲（NUL 终止；len 可为 NULL 表示不跟踪长度）。 */
static bool xlc_bufAssign(char** buf, int* len, int* cap, const char* s, int n)
{
    if (!buf || !cap || n < 0) return false;
    if (n > 0 && (!s || !xlc_bufReserve(buf, cap, n))) {
        if (len) *len = 0;
        if (*buf) (*buf)[0] = '\0';
        return false;
    }
    /* n==0（空文本初始化）也需先分配 1 字节承载 NUL，否则对 NULL 缓冲
       解引用（零初始化对象经 init(txt="") 即触发）。 */
    if (n == 0 && !*buf && !xlc_bufReserve(buf, cap, 1)) return false;
    if (n > 0) XMemcpy(*buf, s, (size_t)n);
    if (len) *len = n;
    (*buf)[n] = '\0';
    return true;
}

/* ==================== 编辑文本模型操作（对标 QString insert/remove/replace） ==================== */

/** @brief 在 pos 处插入 n 字节（容量不足时扩容失败返回 false）。 */
static bool xlc_textInsert(XLineControl* self, int pos, const char* s, int n)
{
    if (!self || n < 0 || !s) return false;
    if (pos < 0) pos = 0;
    if (pos > self->m_textLen) pos = self->m_textLen;
    if (!xlc_bufReserve(&self->m_text, &self->m_textCap, self->m_textLen + n)) return false;
    XMemmove(self->m_text + pos + n, self->m_text + pos,
             (size_t)(self->m_textLen - pos));
    XMemcpy(self->m_text + pos, s, (size_t)n);
    self->m_textLen += n;
    self->m_text[self->m_textLen] = '\0';
    return true;
}

/** @brief 删除 pos 起 n 字节（对标 QString::remove）。 */
static void xlc_textRemove(XLineControl* self, int pos, int n)
{
    if (!self || pos < 0 || n <= 0 || pos >= self->m_textLen) return;
    if (pos + n > self->m_textLen) n = self->m_textLen - pos;
    XMemmove(self->m_text + pos, self->m_text + pos + n,
             (size_t)(self->m_textLen - pos - n));
    self->m_textLen -= n;
    self->m_text[self->m_textLen] = '\0';
}

/** @brief 替换 pos 起 n 字节为 r/rn（对标 QString::replace）。 */
static bool xlc_textReplace(XLineControl* self, int pos, int n,
                            const char* r, int rn)
{
    if (!self) return false;
    if (pos < 0) pos = 0;
    if (pos > self->m_textLen) pos = self->m_textLen;
    if (pos + n > self->m_textLen) n = self->m_textLen - pos;
    if (n < 0) n = 0;
    if (rn < 0 || !r) rn = 0;
    if (rn > n) {
        if (!xlc_bufReserve(&self->m_text, &self->m_textCap,
                            self->m_textLen + rn - n)) return false;
    }
    XMemmove(self->m_text + pos + rn, self->m_text + pos + n,
             (size_t)(self->m_textLen - pos - n));
    if (rn > 0) XMemcpy(self->m_text + pos, r, (size_t)rn);
    self->m_textLen += rn - n;
    self->m_text[self->m_textLen] = '\0';
    return true;
}

/* ==================== 前向声明（内部引擎） ==================== */

static void xlc_internalInsert(XLineControl* self, const char* s, int sLen);
static void xlc_internalDelete(XLineControl* self, bool wasBackspace);
static void XLineControl_internalRemove(XLineControl* self, int pos);
static void xlc_internalSetText(XLineControl* self, const char* txt,
                                int txtLen, int pos, bool edited);
static bool xlc_finishChange(XLineControl* self, int validateFromState,
                             bool update, bool edited);
static void xlc_internalUndo(XLineControl* self, int until);
static void xlc_internalRedo(XLineControl* self);
static void xlc_internalDeselect(XLineControl* self);
static void xlc_removeSelectedText(XLineControl* self);
static void xlc_addCommand(XLineControl* self, XLineControlCommand cmd);
static void xlc_emitCursorPositionChanged(XLineControl* self);
static void xlc_updateDisplayText(XLineControl* self, bool forceUpdate);
static int  xlc_redoTextLayout(XLineControl* self);
static void xlc_parseInputMask(XLineControl* self, const char* maskFields);
static bool xlc_isValidInput(const XLineControl* self, uint32_t key, uint32_t mask);
static bool xlc_textHasAcceptableInput(const XLineControl* self,
                                       const char* str, int strLen);
static void xlc_maskString(const XLineControl* self, int pos,
                           const char* str, int strLen, bool clear,
                           xlc_str* out);
static void xlc_clearString(const XLineControl* self, int pos, int len,
                            xlc_str* out);
static void xlc_stripString(const XLineControl* self, const char* str,
                            int strLen, xlc_str* out);
static int  xlc_findInMask(const XLineControl* self, int pos, bool forward,
                           bool findSeparator, uint32_t searchChar,
                           bool hasSearchChar);
static void xlc_cancelPasswordEchoTimer(XLineControl* self);
static void xlc_separate(XLineControl* self);
static void xlc_refreshTextReturn(XLineControl* self);
static int  xlc_nextCursorPosition(const XLineControl* self, int pos);
static int  xlc_prevCursorPosition(const XLineControl* self, int pos);
static int  xlc_mapTextToLayout(const XLineControl* self, int textPos);
static int  xlc_layoutCursorToX(const XLineControl* self, int layoutPos);
static char* xlc_strdupRange(const char* s, int n);
static void xlc_emitInt(XLineControl* self, size_t signal, int value);
static void xlc_emitInt2(XLineControl* self, size_t signal, int a, int b);
static void xlc_emitVoid(XLineControl* self, size_t signal);
static void xlc_emitText(XLineControl* self, size_t signal, const char* text);
static void xlc_emitBool(XLineControl* self, size_t signal, bool value);
static void xlc_emitRect(XLineControl* self, size_t signal, XRect rect);

/* ==================== 坐标映射与布局（对标 QTextLayout 投影） ==================== */

/** @brief 堆拷贝 [s, s+n)；失败返回 NULL。 */
static char* xlc_strdupRange(const char* s, int n)
{
    char* out;
    if (n < 0 || !s) return NULL;
    out = (char*)XMalloc_System((size_t)n + 1);
    if (!out) return NULL;
    XMemcpy(out, s, (size_t)n);
    out[n] = '\0';
    return out;
}

/**
 * @brief 文本坐标 → 显示坐标（按字符序号换算）。
 * @details 所有回显变换（密码掩码/控制字符替换）均"一文本字符 ↔ 一显示
 *          字符"，故按字符计数换算；Normal/替换保距场景退化为恒等。
 */
static int xlc_mapTextToDisplay(const XLineControl* self, int textPos)
{
    const char* s = self->m_text;
    int len = self->m_textLen;
    const char* d = self->m_displayText;
    int dLen = self->m_displayLen;
    int ti = 0, di = 0;
    if (textPos < 0) return 0;
    if (textPos > len) textPos = len;
    while (ti < textPos && di < dLen) {
        int sn = xlc_seqLenAt(s, len, ti);
        int dn = xlc_seqLenAt(d, dLen, di);
        if (sn <= 0 || dn <= 0) break;
        ti += sn;
        di += dn;
    }
    return di;
}

/**
 * @brief 文本坐标 → 布局坐标（含 preedit 插入位移）。
 * @details 无 preedit 时布局串即显示串（恒等）；有 preedit 时插入点
 *          之后的位置后移 preeditLen。
 */
static int xlc_mapTextToLayout(const XLineControl* self, int textPos)
{
    int disp = xlc_mapTextToDisplay(self, textPos);
    if (self->m_preeditLen > 0 && disp >= self->m_preeditLayoutPos)
        disp += self->m_preeditLen;
    return disp;
}

/**
 * @brief 当前光标的布局字节位置（组合区感知；对标 Qt cursor+preeditCursor）。
 * @details 组合中（preedit 非空）且文本光标在组合插入点时，视觉光标 =
 *          组合区起点 + 组合内字节偏移（m_preeditCursor 存字节）。
 *          不得在 mapTextToLayout 结果上再加 preeditCursor——映射已把
 *          插入点整体移到组合区尾，二次叠加会让光标越过组合串（中文
 *          组合时光标回跳到组合串倒数第一字符之前，14.124）。
 */
static int xlc_cursorLayoutPos(const XLineControl* self)
{
    int pos;
    if (!self) return 0;
    pos = xlc_mapTextToLayout(self, self->m_cursor);
    if (self->m_preeditLen > 0 && self->m_preeditCursor >= 0
        && self->m_cursor == self->m_preeditPos)
        pos = self->m_preeditLayoutPos + self->m_preeditCursor;
    return pos;
}

/**
 * @brief 布局字节位置吸附到所在/前方字符边界。
 * @details pos 恰在字符边界（含 0/len）时原样返回；落在多字节字符
 *          中间时回退到该字符起点。不得无条件用 prevBoundary——那会把
 *          边界位置也整体左移一个字符（光标/选区末端系统性偏左一个
 *          字，中文字符双宽时最明显，14.124）。
 */
static int xlc_layoutSnapBoundary(const XLineControl* self, int pos)
{
    int acc;
    if (!self->m_layoutText || self->m_layoutLen <= 0) return 0;
    if (pos <= 0) return 0;
    if (pos > self->m_layoutLen) pos = self->m_layoutLen;
    acc = 0;
    while (acc < pos) {
        int seq = xlc_seqLenAt(self->m_layoutText, self->m_layoutLen, acc);
        if (seq <= 0) break;
        if (acc + seq >= pos)
            return (acc + seq == pos) ? pos : acc;
        acc += seq;
    }
    return acc;
}

/**
 * @brief 布局位置 → 像素 X（对标 QTextLine::cursorToX）。
 * @details 布局串 [0,layoutPos) 前缀宽度；位置吸附到字符边界。
 */
static int xlc_layoutCursorToX(const XLineControl* self, int layoutPos)
{
    int len = self->m_layoutLen;
    if (!self->m_layoutText || len <= 0) return 0;
    if (layoutPos <= 0) return 0;
    if (layoutPos > len) layoutPos = len;
    layoutPos = xlc_layoutSnapBoundary(self, layoutPos);
#if XPAINTER_ON
    return XPainter_textWidthRange(self->m_font, self->m_layoutText, 0,
                                   layoutPos);
#else
    (void)layoutPos;
    return 0;
#endif /* XPAINTER_ON */
}

/**
 * @brief 重排文本布局（对标 redoTextLayout()）。
 * @details 布局串 = 显示文本在 preedit 插入点拼接 preedit 后的全文；
 *          首行宽/高与 ascent 经 XPainter 度量（Qt 为 QTextLine 度量，
 *          返回 qRound(ascent)）。
 * @return 首行 ascent（像素）。
 */
static int xlc_redoTextLayout(XLineControl* self)
{
    const XFont* font;
    xlc_str layout;
    int preeditInsert;
    int ascent = 0;

    if (!self) return 0;
    /* 计算 preedit 在显示坐标中的插入点。 */
    if (self->m_preeditLen > 0 && self->m_preeditText) {
        preeditInsert = xlc_mapTextToDisplay(
            self, self->m_preeditPos < 0 ? 0 : self->m_preeditPos);
        if (preeditInsert > self->m_displayLen)
            preeditInsert = self->m_displayLen;
    } else {
        preeditInsert = self->m_displayLen;
    }
    self->m_preeditLayoutPos = preeditInsert;

    xlc_strInit(&layout);
    xlc_strAppendBytes(&layout, self->m_displayText, self->m_displayLen);
    if (self->m_preeditLen > 0 && self->m_preeditText) {
        xlc_str splice; /* 中段拼接：display[0,ins) + preedit + display[ins,) */
        xlc_strInit(&splice);
        xlc_strAppendBytes(&splice, self->m_displayText, preeditInsert);
        xlc_strAppendBytes(&splice, self->m_preeditText, self->m_preeditLen);
        xlc_strAppendBytes(&splice, self->m_displayText + preeditInsert,
                           self->m_displayLen - preeditInsert);
        xlc_strFree(&layout);
        layout = splice;
    }
    xlc_bufAssign(&self->m_layoutText, &self->m_layoutLen, &self->m_layoutCap,
                  layout.s ? layout.s : "", layout.len);
    xlc_strFree(&layout);

#if XPAINTER_ON
    font = self->m_font;
    self->m_layoutLineWidth = XPainter_textWidthRange(
        font, self->m_layoutText, 0, self->m_layoutLen);
    /* 行盒高对拍 HEAD 基线（14.123 ③）：ascent+descent 逐项度量后相加
     * （下限 14），不用 XPainter_textHeight——outline 表的 m_height 含
     * lineGap 且整体取整，光标/选区会比壳的行盒高出一截（"光标不对"
     * 的几何根源）。 */
    self->m_layoutLineHeight = XPainter_textAscent(font) +
                               XPainter_textDescent(font);
    if (self->m_layoutLineHeight < 14) self->m_layoutLineHeight = 14;
    ascent = XPainter_textAscent(font);
#else
    self->m_layoutLineWidth = 0;
    self->m_layoutLineHeight = 0;
#endif /* XPAINTER_ON */
    self->m_layoutAscent = ascent;
    return ascent;
}

/* ==================== 按词/按字光标步进（对标 QTextLayout::SkipWords） ==================== */

/** @brief 取 m_text 中 pos 处字符并前推；返回下一边界。 */
static int xlc_nextCursorPosition(const XLineControl* self, int pos)
{
    const char* s = self->m_text;
    int len = self->m_textLen;
    uint32_t cp;
    int seq;
    if (pos < 0 || pos >= len) return pos;
    cp = xlc_decodeAt(s, len, pos, &seq);
    if (xlc_isWordSeparatorCp(cp)) {
        pos += seq;
        while (pos < len) {
            cp = xlc_decodeAt(s, len, pos, &seq);
            if (!xlc_isWordSeparatorCp(cp)) break;
            pos += seq;
        }
    } else {
        while (pos < len) {
            cp = xlc_decodeAt(s, len, pos, &seq);
            if (xlc_isSpaceCp(cp) || xlc_isWordSeparatorCp(cp)) break;
            pos += seq;
        }
    }
    while (pos < len) {
        cp = xlc_decodeAt(s, len, pos, &seq);
        if (!xlc_isSpaceCp(cp)) break;
        pos += seq;
    }
    return pos;
}

/** @brief 对标 QTextLayout::previousCursorPosition(SkipWords)。 */
static int xlc_prevCursorPosition(const XLineControl* self, int pos)
{
    const char* s = self->m_text;
    int len = self->m_textLen;
    int pb;
    uint32_t cp;
    int seq;
    if (pos <= 0 || pos > len) return pos;
    while (pos > 0) {
        pb = xlc_prevBoundary(s, len, pos);
        if (xlc_isSpaceCp(xlc_decodeAt(s, len, pb, &seq))) pos = pb;
        else break;
    }
    if (pos > 0) {
        pb = xlc_prevBoundary(s, len, pos);
        if (xlc_isWordSeparatorCp(xlc_decodeAt(s, len, pb, &seq))) {
            pos = pb;
            while (pos > 0) {
                pb = xlc_prevBoundary(s, len, pos);
                if (!xlc_isWordSeparatorCp(xlc_decodeAt(s, len, pb, &seq))) break;
                pos = pb;
            }
        } else {
            while (pos > 0) {
                pb = xlc_prevBoundary(s, len, pos);
                cp = xlc_decodeAt(s, len, pb, &seq);
                if (xlc_isSpaceCp(cp) || xlc_isWordSeparatorCp(cp)) break;
                pos = pb;
            }
        }
    }
    return pos;
}

/* ==================== 显示文本（对标 updateDisplayText） ==================== */

/** @brief 追加一个"显示字符"：不可打印字符替换为空格（对标控制字符替换）。 */
static void xlc_appendDisplayChar(xlc_str* str, const char* s, int len, int pos)
{
    uint32_t cp;
    int seq = 1;
    cp = xlc_decodeAt(s, len, pos, &seq);
    if ((cp < 0x20u && cp != 0x09u) || cp == 0x2028u || cp == 0x2029u) {
        xlc_strAppendCh(str, ' ');
    } else {
        xlc_strAppendUtf8(str, s + pos, seq);
    }
}

/** @brief 逐字符把 src 追加进显示构建器（保距替换）。 */
static void xlc_appendAllDisplayChars(xlc_str* str, const char* s, int len)
{
    int pos = 0;
    if (!s || len <= 0) return;
    while (pos < len) {
        int seq = xlc_seqLenAt(s, len, pos);
        if (seq <= 0) break;
        xlc_appendDisplayChar(str, s, len, pos);
        pos += seq;
    }
}

/**
 * @brief 刷新显示文本并重排（对标 updateDisplayText）。
 * @details 回显状态机：NoEcho → 空串；Password → 逐字符掩码化，回显
 *          延迟定时器活跃期间显示光标前一字符（Qt 的代理对成对恢复在
 *          此退化为整字符恢复，行为等价）；PasswordEchoOnEdit 且非编辑
 *          态 → 逐字符掩码化。随后替换不可打印字符、重排布局并按变化
 *          发射 displayTextChanged。
 */
static void xlc_updateDisplayText(XLineControl* self, bool forceUpdate)
{
    xlc_str str;
    xlc_str orig; /* 旧显示快照（内容级比较；缓冲可能被重分配）。 */
    bool changed;

    if (!self) return;
    xlc_strInit(&orig);
    xlc_strAppendBytes(&orig, self->m_displayText, self->m_displayLen);
    xlc_strInit(&str);

    if (self->m_echoMode == (uint32_t)XLineControlEchoMode_NoEcho) {
        /* 空串（对标 str = QString::fromLatin1("")）。 */
    } else if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Password) {
        int revealCharIdx = -1;
        int pos = 0;
        int charIdx = 0;
        if (self->m_passwordEchoEditing
            && self->m_cursor > 0 && self->m_cursor <= self->m_textLen) {
            /* 对标 Qt：m_cursor-1 处码元回显；代理对成对分支退化为整字符。 */
            int revealStart = xlc_prevBoundary(self->m_text, self->m_textLen,
                                               self->m_cursor);
            int walk = 0;
            while (walk < revealStart) {
                int n = xlc_seqLenAt(self->m_text, self->m_textLen, walk);
                if (n <= 0) break;
                walk += n;
                ++charIdx;
            }
            revealCharIdx = charIdx;
        }
        charIdx = 0; /* 回溯求序号时已前移,掩码循环须从 0 重新计数 */
        {
            int passLen = (int)XStrlen(self->m_passwordCharacter);
            if (passLen <= 0) passLen = 1;
            while (pos < self->m_textLen) {
                int seq = xlc_seqLenAt(self->m_text, self->m_textLen, pos);
                if (seq <= 0) break;
                if (charIdx == revealCharIdx)
                    xlc_appendDisplayChar(&str, self->m_text,
                                          self->m_textLen, pos);
                else
                    xlc_strAppendUtf8(&str, self->m_passwordCharacter,
                                      passLen);
                pos += seq;
                ++charIdx;
            }
        }
    } else {
        xlc_appendAllDisplayChars(&str, self->m_text, self->m_textLen);
        if (self->m_echoMode == (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit
            && !self->m_passwordEchoEditing) {
            /* 整串掩码化（对标 str.fill(m_passwordCharacter)）。 */
            int i;
            int passLen = (int)XStrlen(self->m_passwordCharacter);
            xlc_str masked;
            xlc_strInit(&masked);
            for (i = 0; i < str.len; ) {
                int seq = xlc_seqLenAt(str.s, str.len, i);
                if (seq <= 0) break;
                xlc_strAppendUtf8(&masked, self->m_passwordCharacter,
                                  passLen > 0 ? passLen : 1);
                i += seq;
            }
            xlc_strFree(&str);
            str = masked;
        }
    }

    /* preedit 组合区拼接（对标 displayText 的 preeditArea 插入；
     * NoEcho 已在 IME 路径丢弃，Password 的掩码 preedit 已按
     * 密码符转写存储，此处按字节位原样插入）。 */
    if (self->m_preeditLen > 0 && self->m_preeditPos >= 0
        && self->m_preeditPos <= str.len) {
        xlc_str withPre;
        xlc_strInit(&withPre);
        xlc_strAppendBytes(&withPre, str.s, self->m_preeditPos);
        xlc_strAppendBytes(&withPre, self->m_preeditText,
                           self->m_preeditLen);
        xlc_strAppendBytes(&withPre, str.s + self->m_preeditPos,
                           str.len - self->m_preeditPos);
        xlc_strFree(&str);
        str = withPre;
    }
    xlc_bufAssign(&self->m_displayText, &self->m_displayLen,
                  &self->m_displayCap, str.s ? str.s : "", str.len);
    xlc_strFree(&str);

    self->m_layoutAscent = xlc_redoTextLayout(self);
    xlc_refreshTextReturn(self);

    changed = (orig.len != self->m_displayLen)
              || (orig.s && self->m_displayText
                  && XMemcmp(orig.s, self->m_displayText,
                             (size_t)self->m_displayLen) != 0);
    if (changed || forceUpdate)
        xlc_emitText(self,
                     (size_t)XLineControl_displayTextChanged_signal(
                         self, self->m_displayText),
                     self->m_displayText);
    xlc_strFree(&orig);
}

/** @brief 刷新 text() 返回缓存（掩码场景经 stripString 剥离占位）。 */
static void xlc_refreshTextReturn(XLineControl* self)
{
    if (!self) return;
    if (self->m_maskData) {
        xlc_str stripped;
        xlc_strInit(&stripped);
        xlc_stripString(self, self->m_text, self->m_textLen, &stripped);
        xlc_bufAssign(&self->m_textReturn, &self->m_textReturnLen,
                      &self->m_textReturnCap,
                      stripped.s ? stripped.s : "", stripped.len);
        xlc_strFree(&stripped);
    } else {
        xlc_bufAssign(&self->m_textReturn, &self->m_textReturnLen,
                      &self->m_textReturnCap,
                      self->m_text ? self->m_text : "", self->m_textLen);
    }
}

/* ==================== 输入掩码引擎（对标 parseInputMask/isValidInput/maskString 族） ==================== */

/**
 * @brief 取掩码槽位 i 的字符码点。
 * @note  掩码字符表（A/a/N/n/X/x/9/0/D/d/#/H/h/B/b）均为 ASCII，
 *        与 Qt 的码元比较逐项一致。
 */
static uint32_t xlc_maskCpAt(const XLineControl* self, int i)
{
    if (!self->m_maskData || i < 0 || i >= self->m_maskDataCount) return 0u;
    return xlc_decodeAt(self->m_maskData[i].m_maskChar, 4, 0, NULL);
}

/** @brief 取 m_blank 占位符码点。 */
static uint32_t xlc_blankCp(const XLineControl* self)
{ return xlc_decodeAt(self->m_blank, 4, 0, NULL); }

/** @brief 写入掩码槽位字符（定容 char[5]，NUL 终止；超长钳位 4 字节）。 */
static void xlc_maskCharSet(char dst[5], const char* s, int n)
{
    if (n < 0) n = 0;
    if (n > 4) n = 4;
    if (n > 0 && s) XMemcpy(dst, s, (size_t)n);
    dst[n] = '\0';
}

/**
 * @brief 判定 key 是否匹配掩码字符 mask（对标 isValidInput，逐 case 一致）。
 * @note  字母/可打印类判定对非 ASCII 采用文档化的启发式（见分类函数）。
 */
static bool xlc_isValidInput(const XLineControl* self, uint32_t key, uint32_t mask)
{
    uint32_t blank = xlc_blankCp(self);
    switch (mask) {
    case 'A':
        if (xlc_isLetterCp(key)) return true;
        break;
    case 'a':
        if (xlc_isLetterCp(key) || key == blank) return true;
        break;
    case 'N':
        if (xlc_isLetterOrNumberCp(key)) return true;
        break;
    case 'n':
        if (xlc_isLetterOrNumberCp(key) || key == blank) return true;
        break;
    case 'X':
        if (xlc_isPrintCp(key) && key != blank) return true;
        break;
    case 'x':
        if (xlc_isPrintCp(key) || key == blank) return true;
        break;
    case '9':
        if (xlc_isNumberCp(key)) return true;
        break;
    case '0':
        if (xlc_isNumberCp(key) || key == blank) return true;
        break;
    case 'D':
        if (xlc_isNumberCp(key) && xlc_digitValueCp(key) > 0) return true;
        break;
    case 'd':
        if ((xlc_isNumberCp(key) && xlc_digitValueCp(key) > 0) || key == blank)
            return true;
        break;
    case '#':
        if (xlc_isNumberCp(key) || key == '+' || key == '-' || key == blank)
            return true;
        break;
    case 'B':
        if (key == '0' || key == '1') return true;
        break;
    case 'b':
        if (key == '0' || key == '1' || key == blank) return true;
        break;
    case 'H':
        if (xlc_isNumberCp(key) || (key >= 'a' && key <= 'f')
            || (key >= 'A' && key <= 'F'))
            return true;
        break;
    case 'h':
        if (xlc_isNumberCp(key) || (key >= 'a' && key <= 'f')
            || (key >= 'A' && key <= 'F') || key == blank)
            return true;
        break;
    default:
        break;
    }
    return false;
}

/**
 * @brief 把 str 自掩码位 pos 起套入掩码（对标 maskString，逐分支一致）。
 * @param clear true 表示遇到分隔符时用占位符填充来源（对标 clear 路径），
 *        false 表示沿用当前编辑文本内容。
 * @note  原实现的 strIndex 按码元步进；此处按完整 UTF-8 字符步进
 *        （fill 前缀切片按字符序号换算字节区间）。
 */
static void xlc_maskString(const XLineControl* self, int pos,
                           const char* str, int strLen, bool clear,
                           xlc_str* out)
{
    xlc_str fill;
    int strIndex = 0;
    int i = pos;

    if (!self || !out) return;
    if (!self->m_maskData || pos >= self->m_maskDataCount) return;
    if (pos < 0) pos = 0;

    xlc_strInit(&fill); /* 未初始化直接 realloc 垃圾栈值（ASan 实证） */
    if (clear) xlc_clearString(self, 0, self->m_maskDataCount, &fill);
    else xlc_strAppendBytes(&fill, self->m_text, self->m_textLen);

    while (i < self->m_maskDataCount) {
        uint32_t sc;
        int scLen;
        if (strIndex >= strLen || !str) break;
        sc = xlc_decodeAt(str, strLen, strIndex, &scLen);
        if (self->m_maskData[i].m_separator) {
            xlc_strAppendUtf8(out, self->m_maskData[i].m_maskChar,
                              (int)XStrlen(self->m_maskData[i].m_maskChar));
            if (sc == xlc_maskCpAt(self, i)) strIndex += scLen;
            ++i;
        } else {
            if (xlc_isValidInput(self, sc, xlc_maskCpAt(self, i))) {
                switch (self->m_maskData[i].m_caseMode) {
                case XLineControlCaseMode_Upper:
                    xlc_strAppendCp(out, xlc_toUpperCp(sc));
                    break;
                case XLineControlCaseMode_Lower:
                    xlc_strAppendCp(out, xlc_toLowerCp(sc));
                    break;
                default:
                    xlc_strAppendUtf8(out, str + strIndex, scLen);
                    break;
                }
                ++i;
            } else {
                /* 先找与输入字符相同的分隔符（对标 findInMask(true,true)）。 */
                int n = xlc_findInMask(self, i, true, true, sc, true);
                if (n != -1) {
                    bool singleChar = (xlc_countChars(str, strLen) == 1);
                    if (singleChar || i == 0
                        || (i > 0 && (!self->m_maskData[i - 1].m_separator
                                      || xlc_maskCpAt(self, i - 1) != sc))) {
                        /* s += fill.mid(i, n-i+1)：字符区间 [i, n] 切片。 */
                        int b0 = xlc_charsByteLen(fill.s, fill.len, i);
                        int b1 = xlc_charsByteLen(fill.s, fill.len, n + 1);
                        xlc_strAppendBytes(out, fill.s + b0, b1 - b0);
                        i = n + 1;
                    }
                } else {
                    /* 再找可接纳该字符的编辑位（对标 findInMask(true,false)）。 */
                    n = xlc_findInMask(self, i, true, false, sc, true);
                    if (n != -1) {
                        int b0 = xlc_charsByteLen(fill.s, fill.len, i);
                        int b1 = xlc_charsByteLen(fill.s, fill.len, n);
                        xlc_strAppendBytes(out, fill.s + b0, b1 - b0);
                        switch (self->m_maskData[n].m_caseMode) {
                        case XLineControlCaseMode_Upper:
                            xlc_strAppendCp(out, xlc_toUpperCp(sc));
                            break;
                        case XLineControlCaseMode_Lower:
                            xlc_strAppendCp(out, xlc_toLowerCp(sc));
                            break;
                        default:
                            xlc_strAppendUtf8(out, str + strIndex, scLen);
                            break;
                        }
                        i = n + 1;
                    }
                }
            }
            strIndex += scLen;
        }
    }
    xlc_strFree(&fill);
}

/** @brief 生成 [pos, pos+len) 的"清空串"（分隔符保留、编辑位填占位）。 */
static void xlc_clearString(const XLineControl* self, int pos, int len,
                            xlc_str* out)
{
    int end;
    int i;
    if (!self || !out || !self->m_maskData) return;
    if (pos >= self->m_maskDataCount) return;
    if (pos < 0) pos = 0;
    end = self->m_maskDataCount < pos + len ? self->m_maskDataCount
                                            : pos + len;
    for (i = pos; i < end; ++i) {
        if (self->m_maskData[i].m_separator)
            xlc_strAppendUtf8(out, self->m_maskData[i].m_maskChar,
                              (int)XStrlen(self->m_maskData[i].m_maskChar));
        else
            xlc_strAppendUtf8(out, self->m_blank,
                              (int)XStrlen(self->m_blank));
    }
}

/** @brief 剥离未填占位（分隔符保留；对标 stripString）。 */
static void xlc_stripString(const XLineControl* self, const char* str,
                            int strLen, xlc_str* out)
{
    int end;
    int i;
    int pos;
    if (!out) return;
    if (!self || !self->m_maskData) {
        xlc_strAppendBytes(out, str, strLen);
        return;
    }
    if (!str || strLen <= 0) return;
    end = self->m_maskDataCount < strLen ? self->m_maskDataCount : strLen;
    pos = 0;
    for (i = 0; i < end; ) {
        int seq = xlc_seqLenAt(str, strLen, pos);
        if (seq <= 0) break;
        if (self->m_maskData[i].m_separator) {
            xlc_strAppendUtf8(out, self->m_maskData[i].m_maskChar,
                              (int)XStrlen(self->m_maskData[i].m_maskChar));
        } else {
            uint32_t cp = xlc_decodeAt(str, strLen, pos, NULL);
            if (cp != xlc_blankCp(self))
                xlc_strAppendUtf8(out, str + pos, seq);
        }
        pos += seq;
        ++i;
    }
}

/**
 * @brief 在掩码槽位表中向前/向后搜索（对标 findInMask）。
 * @param findSeparator true 找与 searchChar 相同的分隔符；false 找可
 *        接纳 searchChar 的编辑位（searchChar 有效时）或任意编辑位。
 */
static int xlc_findInMask(const XLineControl* self, int pos, bool forward,
                          bool findSeparator, uint32_t searchChar,
                          bool hasSearchChar)
{
    int end = forward ? self->m_maskDataCount : -1;
    int step = forward ? 1 : -1;
    int i = pos;
    if (!self->m_maskData || pos >= self->m_maskDataCount || pos < 0)
        return -1;
    while (i != end) {
        if (findSeparator) {
            if (self->m_maskData[i].m_separator
                && xlc_maskCpAt(self, i) == searchChar)
                return i;
        } else {
            if (!self->m_maskData[i].m_separator) {
                if (!hasSearchChar) return i;
                if (xlc_isValidInput(self, searchChar, xlc_maskCpAt(self, i)))
                    return i;
            }
        }
        i += step;
    }
    return -1;
}

/**
 * @brief pos 起的下一个可编辑位（无则文本末尾；对标 nextMaskBlank）。
 * @note  Qt 的掩码槽位序号即码元位置；本实现位置为字节偏移，槽位序号
 *        与字节偏移经字符计数互转（下同）。
 */
static int xlc_nextMaskBlank(XLineControl* self, int pos)
{
    int len = self->m_textLen;
    int clamped = pos < 0 ? 0 : (pos > len ? len : pos);
    int slot = xlc_countChars(self->m_text, clamped);
    int c = xlc_findInMask(self, slot, true, false, 0u, false);
    if (c != slot) xlc_separate(self);
    if (c == -1) return len;
    return xlc_charsByteLen(self->m_text, len, c);
}

/** @brief pos 起的上一个可编辑位（无则 0；对标 prevMaskBlank）。 */
static int xlc_prevMaskBlank(XLineControl* self, int pos)
{
    int len = self->m_textLen;
    int clamped = pos < 0 ? 0 : (pos > len ? len : pos);
    int slot = xlc_countChars(self->m_text, clamped);
    int c = xlc_findInMask(self, slot, false, false, 0u, false);
    if (c != slot) xlc_separate(self);
    if (c == -1) return 0;
    return xlc_charsByteLen(self->m_text, len, c);
}

/**
 * @brief 解析输入掩码（对标 parseInputMask，两趟扫描逐分支一致）。
 * @details 第一趟统计槽位数（转义与 <>!{}[] 元字符不计）；第二趟按
 *          大小写切换字符（<、>、!）与转义（\）填充槽位表；随后以当前
 *          文本重走 internalSetText 完成掩码适配。空串或以 ';' 开头清除
 *          掩码并复位 maxLength。
 */
static void xlc_parseInputMask(XLineControl* self, const char* maskFields)
{
    int fieldsLen = (maskFields && self) ? (int)XStrlen(maskFields) : 0;
    int delimiter = -1;
    int i;
    int maxLen = 0;
    int index = 0;
    int caseMode = (int)XLineControlCaseMode_NoCaseMode;

    if (!self) return;
    if (maskFields) {
        const char* semi = XStrchr(maskFields, ';');
        delimiter = semi ? (int)(semi - maskFields) : -1;
    }
    if (fieldsLen == 0 || delimiter == 0) {
        if (self->m_maskData) {
            if (self->m_maskData) XFree_System(self->m_maskData);
            self->m_maskData = NULL;
            self->m_maskDataCount = 0;
            if (self->m_inputMask) { XFree_System(self->m_inputMask); self->m_inputMask = NULL; }
            self->m_maxLength = XLC_DEFAULT_MAX_LENGTH;
            xlc_internalSetText(self, "", 0, -1, false);
        }
        return;
    }

    /* 拆掩码体与占位符。 */
    {
        int bodyLen = delimiter == -1 ? fieldsLen : delimiter;
        char* body = xlc_strdupRange(maskFields, bodyLen);
        if (self->m_inputMask) XFree_System(self->m_inputMask);
        self->m_inputMask = body;
        if (delimiter != -1 && delimiter + 1 < fieldsLen) {
            int seq = xlc_seqLenAt(maskFields, fieldsLen, delimiter + 1);
            if (seq > 0 && seq <= 4)
                XMemcpy(self->m_blank, maskFields + delimiter + 1, (size_t)seq);
            self->m_blank[seq > 0 ? seq : 0] = '\0';
        } else {
            self->m_blank[0] = ' ';
            self->m_blank[1] = '\0';
        }
    }

    /* 第一趟：统计槽位数。 */
    {
        int bodyLen = (int)XStrlen(self->m_inputMask);
        bool esc = false;
        for (i = 0; i < bodyLen; ) {
            int seq = xlc_seqLenAt(self->m_inputMask, bodyLen, i);
            uint32_t c = xlc_decodeAt(self->m_inputMask, bodyLen, i, NULL);
            if (seq <= 0) break;
            if (esc) { ++maxLen; esc = false; i += seq; continue; }
            if (c == '\\') { esc = true; i += seq; continue; }
            if (c != '!' && c != '<' && c != '>'
                && c != '{' && c != '}' && c != '[' && c != ']')
                ++maxLen;
            i += seq;
        }
    }

    /* 分配槽位表（保留旧表容量复用；简化为重新分配）。 */
    if (self->m_maskData) { XFree_System(self->m_maskData); self->m_maskData = NULL; }
    self->m_maskDataCount = 0;
    if (maxLen > 0) {
        self->m_maskData = (XLineControlMaskInputData*)XCalloc_System(
            (size_t)maxLen, sizeof(XLineControlMaskInputData));
        if (!self->m_maskData) { self->m_maxLength = XLC_DEFAULT_MAX_LENGTH; return; }
    }

    /* 第二趟：填充槽位。 */
    {
        int bodyLen = (int)XStrlen(self->m_inputMask);
        bool esc = false;
        for (i = 0; i < bodyLen && self->m_maskData; ) {
            int seq = xlc_seqLenAt(self->m_inputMask, bodyLen, i);
            uint32_t c = xlc_decodeAt(self->m_inputMask, bodyLen, i, NULL);
            bool isSeparator;
            if (seq <= 0) break;
            if (esc) {
                isSeparator = true;
                xlc_maskCharSet(self->m_maskData[index].m_maskChar,
                                self->m_inputMask + i, seq);
                self->m_maskData[index].m_separator = isSeparator;
                self->m_maskData[index].m_caseMode = caseMode;
                ++index;
                esc = false;
                i += seq;
                continue;
            }
            if (c == '<') { caseMode = (int)XLineControlCaseMode_Lower; i += seq; continue; }
            if (c == '>') { caseMode = (int)XLineControlCaseMode_Upper; i += seq; continue; }
            if (c == '!') { caseMode = (int)XLineControlCaseMode_NoCaseMode; i += seq; continue; }
            if (c == '{' || c == '}' || c == '[' || c == ']') { i += seq; continue; }
            switch (c) {
            case 'A': case 'a': case 'N': case 'n':
            case 'X': case 'x': case '9': case '0':
            case 'D': case 'd': case '#':
            case 'H': case 'h': case 'B': case 'b':
                isSeparator = false;
                break;
            case '\\':
                esc = true;
                /* Qt Q_FALLTHROUGH 到 default：本趟仅置转义，不占槽位。 */
                isSeparator = true;
                break;
            default:
                isSeparator = true;
                break;
            }
            if (!esc) {
                xlc_maskCharSet(self->m_maskData[index].m_maskChar,
                                self->m_inputMask + i, seq);
                self->m_maskData[index].m_separator = isSeparator;
                self->m_maskData[index].m_caseMode = caseMode;
                ++index;
            }
            i += seq;
        }
    }
    self->m_maskDataCount = index;
    self->m_maxLength = maxLen;
    xlc_internalSetText(self, self->m_text, self->m_textLen, -1, false);
}

/* ==================== 撤销栈引擎（对标 Command/addCommand/internalUndo/internalRedo） ==================== */

/** @brief 判断单个码点是否为 UTF-8 续字节语义（对标 isLowSurrogate 判定位）。 */
static bool xlc_charAtIsContinuation(const XLineControl* self, int pos)
{
    if (pos < 0 || pos >= self->m_textLen || !self->m_text) return false;
    return xlc_isContByte((uint8_t)self->m_text[pos]);
}

/** @brief 组装"插入一字符"命令。 */
static XLineControlCommand xlc_cmdInsert(int pos, const char* utf8, int seqLen)
{
    XLineControlCommand cmd;
    XMemset(&cmd, 0, sizeof(cmd));
    cmd.type = (int)XLineControlCommandType_Insert;
    xlc_maskCharSet((char*)cmd.m_uc, utf8, seqLen);
    cmd.m_ucLen = (uint8_t)(seqLen > 0 ? seqLen : 1);
    cmd.pos = pos;
    cmd.selStart = -1;
    cmd.selEnd = -1;
    return cmd;
}

/** @brief 组装"删除一字符"命令（type = Remove/Delete/两选区删除型）。 */
static XLineControlCommand xlc_cmdRemove(int type, int pos,
                                         const char* utf8, int seqLen,
                                         int selStart, int selEnd)
{
    XLineControlCommand cmd;
    XMemset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    xlc_maskCharSet((char*)cmd.m_uc, utf8, seqLen);
    cmd.m_ucLen = (uint8_t)(seqLen > 0 ? seqLen : 1);
    cmd.pos = pos;
    cmd.selStart = selStart;
    cmd.selEnd = selEnd;
    return cmd;
}

/** @brief 组装 SetSelection/Separator 命令（不承载字符）。 */
static XLineControlCommand xlc_cmdMark(int type, int pos, int selStart, int selEnd)
{
    XLineControlCommand cmd;
    XMemset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    cmd.m_ucLen = 0;
    cmd.pos = pos;
    cmd.selStart = selStart;
    cmd.selEnd = selEnd;
    return cmd;
}

/** @brief 历史数组扩容。 */
static bool xlc_historyReserve(XLineControl* self, int needed)
{
    int newCap;
    XLineControlCommand* nh;
    if (needed <= self->m_historyCap) return true;
    newCap = needed < 16 ? 16 : needed;
    if (self->m_historyCap > 0 && newCap < self->m_historyCap * 2)
        newCap = self->m_historyCap * 2;
    nh = (XLineControlCommand*)XRealloc_System(
        self->m_history, (size_t)newCap * sizeof(XLineControlCommand));
    if (!nh) return false;
    self->m_history = nh;
    self->m_historyCap = newCap;
    return true;
}

/**
 * @brief 命令入栈（对标 addCommand；不施加命令）。
 * @details 先截断重做分支（history 尺寸收敛到 undoState）；分组标志
 *          挂起时先压 Separator；随后压命令并推进 undoState。
 */
static void xlc_addCommand(XLineControl* self, XLineControlCommand cmd)
{
    if (!self) return;
    self->m_historySize = self->m_undoState; /* 截断 redo 分支 */

    if (self->m_separator && self->m_undoState
        && self->m_history[self->m_undoState - 1].type
            != (int)XLineControlCommandType_Separator) {
        if (xlc_historyReserve(self, self->m_historySize + 1)) {
            self->m_history[self->m_historySize++] =
                xlc_cmdMark((int)XLineControlCommandType_Separator,
                            self->m_cursor, self->m_selstart, self->m_selend);
        }
    }
    self->m_separator = false;
    if (xlc_historyReserve(self, self->m_historySize + 1)) {
        self->m_history[self->m_historySize++] = cmd;
        self->m_undoState = self->m_historySize;
    }
}

/** @brief 置分组挂起标志（对标 separate()）。 */
static void xlc_separate(XLineControl* self)
{ if (self) self->m_separator = true; }

/**
 * @brief 撤销到基准态（对标 internalUndo）。
 * @details 循环弹栈施加逆向操作；until<0 时在命令组边界断开（类型
 *          变更且不跨选区删除族的分组判定与 Qt 逐项一致）；Separator
 *          恒继续（不参与分组判定）。
 */
static void xlc_internalUndo(XLineControl* self, int until)
{
    if (!self || !XLineControl_isUndoAvailable(self)) return;
    xlc_cancelPasswordEchoTimer(self);
    xlc_internalDeselect(self);

    while (self->m_undoState && self->m_undoState > until) {
        XLineControlCommand* cmd = &self->m_history[--self->m_undoState];
        switch (cmd->type) {
        case (int)XLineControlCommandType_Insert:
            xlc_textRemove(self, cmd->pos, cmd->m_ucLen);
            self->m_cursor = cmd->pos;
            break;
        case (int)XLineControlCommandType_SetSelection:
            self->m_selstart = cmd->selStart;
            self->m_selend = cmd->selEnd;
            self->m_cursor = cmd->pos;
            break;
        case (int)XLineControlCommandType_Remove:
        case (int)XLineControlCommandType_RemoveSelection:
            xlc_textInsert(self, cmd->pos, (const char*)cmd->m_uc, cmd->m_ucLen);
            self->m_cursor = cmd->pos + cmd->m_ucLen;
            break;
        case (int)XLineControlCommandType_Delete:
        case (int)XLineControlCommandType_DeleteSelection:
            xlc_textInsert(self, cmd->pos, (const char*)cmd->m_uc, cmd->m_ucLen);
            self->m_cursor = cmd->pos;
            break;
        case (int)XLineControlCommandType_Separator:
        default:
            continue; /* 对标 Qt：Separator 直接进入下一轮（跳过分组判定）。 */
        }
        if (until < 0 && self->m_undoState) {
            XLineControlCommand* next = &self->m_history[self->m_undoState - 1];
            if (next->type != cmd->type
                && next->type < (int)XLineControlCommandType_RemoveSelection
                && (cmd->type < (int)XLineControlCommandType_RemoveSelection
                    || next->type == (int)XLineControlCommandType_Separator))
                break;
        }
    }
    self->m_textDirty = true;
    /* 光标信号由 finishChange 在 text 系信号之后统一收尾（对齐 Qt）。 */
}

/** @brief 重做（对标 internalRedo；分组前进判定与 Qt 逐项一致）。 */
static void xlc_internalRedo(XLineControl* self)
{
    if (!self || !XLineControl_isRedoAvailable(self)) return;
    xlc_internalDeselect(self);
    while (self->m_undoState < self->m_historySize) {
        XLineControlCommand* cmd = &self->m_history[self->m_undoState++];
        switch (cmd->type) {
        case (int)XLineControlCommandType_Insert:
            xlc_textInsert(self, cmd->pos, (const char*)cmd->m_uc, cmd->m_ucLen);
            self->m_cursor = cmd->pos + cmd->m_ucLen;
            break;
        case (int)XLineControlCommandType_SetSelection:
            self->m_selstart = cmd->selStart;
            self->m_selend = cmd->selEnd;
            self->m_cursor = cmd->pos;
            break;
        case (int)XLineControlCommandType_Remove:
        case (int)XLineControlCommandType_Delete:
        case (int)XLineControlCommandType_RemoveSelection:
        case (int)XLineControlCommandType_DeleteSelection:
            xlc_textRemove(self, cmd->pos, cmd->m_ucLen);
            self->m_selstart = cmd->selStart;
            self->m_selend = cmd->selEnd;
            self->m_cursor = cmd->pos;
            break;
        case (int)XLineControlCommandType_Separator:
        default:
            self->m_selstart = cmd->selStart;
            self->m_selend = cmd->selEnd;
            self->m_cursor = cmd->pos;
            break;
        }
        if (self->m_undoState < self->m_historySize) {
            XLineControlCommand* next = &self->m_history[self->m_undoState];
            if (next->type != cmd->type
                && cmd->type < (int)XLineControlCommandType_RemoveSelection
                && next->type != (int)XLineControlCommandType_Separator
                && (next->type < (int)XLineControlCommandType_RemoveSelection
                    || cmd->type == (int)XLineControlCommandType_Separator))
                break;
        }
    }
    self->m_textDirty = true;
    /* 光标信号由 finishChange 在 text 系信号之后统一收尾（对齐 Qt）。 */
}

/* ==================== 变更结算（对标 finishChange） ==================== */

/**
 * @brief 结算一次变更（对标 finishChange）。
 * @details textDirty 时先过校验钩子：Invalid 发射 inputRejected；
 *          validate 修改了文本则经 internalSetText 重入并提前返回；
 *          合法性由"可用→不可用"翻转触发回滚（internalUndo 至基准态并
 *          截断历史，IME 事务挂起时放弃回滚）；随后刷新显示并按 edited
 *          发射 textEdited/textChanged。selDirty 发射 selectionChanged；
 *          光标未动发射 updateMicroFocus；最后 emitCursorPositionChanged。
 * @return Qt 同语义：回滚/重入路径返回 false，其余返回 true。
 */
static bool xlc_finishChange(XLineControl* self, int validateFromState,
                             bool update, bool edited)
{
    (void)update; /* 对标 Qt：update 形参当前未使用。 */

    if (!self) return true;
    if (self->m_textDirty) {
        bool wasValidInput = self->m_validInput != 0u;
        self->m_validInput = 1;
        if (self->m_validateFunc && self->m_validator) {
            char* textCopy = xlc_strdupRange(self->m_text, self->m_textLen);
            int cursorCopy = self->m_cursor;
            if (textCopy) {
                int state = self->m_validateFunc(self->m_validator,
                                                 &textCopy, &cursorCopy,
                                                 self->m_validatorUserData);
                self->m_validInput =
                    (state != (int)XLineControlValidatorState_Invalid) ? 1u : 0u;
                if (self->m_validInput) {
                    int copyLen = (int)XStrlen(textCopy);
                    if (self->m_textLen != copyLen
                        || XMemcmp(self->m_text, textCopy, (size_t)copyLen) != 0) {
                        xlc_internalSetText(self, textCopy, copyLen,
                                            cursorCopy, edited);
                        XFree_System(textCopy);
                        return true;
                    }
                    self->m_cursor = cursorCopy;
                } else {
                    xlc_emitVoid(self,
                                 (size_t)XLineControl_inputRejected_signal(self));
                }
                XFree_System(textCopy);
            }
        }
        if (validateFromState >= 0 && wasValidInput && !self->m_validInput) {
            if (self->m_transactionCount > 0)
                return false;
            xlc_internalUndo(self, validateFromState);
            self->m_historySize = self->m_undoState;
            if (self->m_modifiedState > self->m_undoState)
                self->m_modifiedState = -1;
            self->m_validInput = 1;
            self->m_textDirty = false;
        }
        xlc_updateDisplayText(self, false);

        if (self->m_textDirty) {
            self->m_textDirty = false;
            {
                const char* actualText = XLineControl_text(self);
                if (edited)
                    xlc_emitText(self,
                                 (size_t)XLineControl_textEdited_signal(
                                     self, actualText),
                                 actualText);
                xlc_emitText(self,
                             (size_t)XLineControl_textChanged_signal(
                                 self, actualText),
                             actualText);
            }
        }
    }
    if (self->m_selDirty) {
        self->m_selDirty = false;
        xlc_emitVoid(self, (size_t)XLineControl_selectionChanged_signal(self));
    }
    if (self->m_cursor == self->m_lastCursorPos)
        xlc_emitVoid(self, (size_t)XLineControl_updateMicroFocus_signal(self));
    xlc_emitCursorPositionChanged(self);
    /* 挂起的拒绝发射收尾（对齐 Qt 顺序：text 系信号在前）。 */
    if (self->m_pendingInputRejected) {
        self->m_pendingInputRejected = 0u;
        xlc_emitVoid(self,
                     (size_t)XLineControl_inputRejected_signal(self));
    }
    return true;
}

/* ==================== 内部文本操作（对标 internalInsert/internalDelete/internalRemove/removeSelectedText/internalSetText） ==================== */

/**
 * @brief 掩码场景取编辑位字符字节（越界/分隔符返回占位）。
 * @note  对标 internalInsert 掩码分支中 m_text.at(m_cursor+i) 的
 *        "被覆盖旧字符"取样。
 */
static const char* xlc_textCharAt(const XLineControl* self, int pos, int* seqLen)
{
    int n;
    if (seqLen) *seqLen = 1;
    n = xlc_seqLenAt(self->m_text, self->m_textLen, pos);
    if (n <= 0) return NULL;
    if (seqLen) *seqLen = n;
    return self->m_text + pos;
}

/**
 * @brief 插入串到光标处并记录撤销命令（对标 internalInsert）。
 * @details Password 回显下按 passwordMaskDelay 启动回显延迟定时器；
 *          掩码场景经 maskString 逐字符生成 DeleteSelection/Insert 命令
 *          对，被完全拒绝发射 inputRejected；普通场景按 maxLength 截断，
 *          溢出发射 inputRejected。不调用 finishChange（与 Qt 一致，
 *          可能短暂处于非法态）。
 */
static void xlc_internalInsert(XLineControl* self, const char* s, int sLen)
{
    if (!self || !s || sLen <= 0) return;
    if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Password) {
        int delay = self->m_passwordMaskDelay;
        if (delay > 0) {
            if (self->m_passwordEchoTimer == XTIMER_INVALID_ID)
                self->m_passwordEchoTimer = XObject_startTimer_ms(
                    (XObject*)self, (uint64_t)delay, XTimerType_CoarseTimer);
            /* 明文窗口状态与定时器双承载：无事件循环（验收/离线）时
               标志位独立成立，定时器到期经 timerEvent 复位。 */
            self->m_passwordEchoEditing = 1u;
        }
    }
    if (XLineControl_hasSelectedText(self)) {
        xlc_addCommand(self, xlc_cmdMark(
            (int)XLineControlCommandType_SetSelection,
            self->m_cursor, self->m_selstart, self->m_selend));
    }
    if (self->m_maskData) {
        xlc_str ms;
        int i;
        int pos;
        xlc_strInit(&ms);
        xlc_maskString(self, self->m_cursor, s, sLen, false, &ms);
        if (ms.len == 0 && sLen > 0) {
            /* 对标 Qt：整串无处可落，仅发射拒绝并原样返回（不置
             * textDirty、不动光标，避免脏 text 信号与光标漂移）。 */
            self->m_pendingInputRejected = 1u;
            if (self->m_pendingInputRejected) {
                self->m_pendingInputRejected = 0u;
                xlc_emitVoid(self,
                             (size_t)XLineControl_inputRejected_signal(self));
            }
            xlc_strFree(&ms);
            return;
        }
        pos = self->m_cursor;
        for (i = 0; i < ms.len; ) {
            int seq = xlc_seqLenAt(ms.s, ms.len, i);
            int oldSeq = 1;
            const char* oldCh = xlc_textCharAt(self, pos, &oldSeq);
            if (seq <= 0) break;
            /* 对标：DeleteSelection(旧字符) + Insert(新字符) 成对入栈。 */
            xlc_addCommand(self, xlc_cmdRemove(
                (int)XLineControlCommandType_DeleteSelection, pos,
                oldCh, oldSeq, -1, -1));
            xlc_addCommand(self, xlc_cmdInsert(pos, ms.s + i, seq));
            pos += seq;
            i += seq;
        }
        /* 对标 m_text.replace(m_cursor, ms.size(), ms)：替换被消费槽位
         * 的字节区（UTF-8 槽位字节宽可变，按槽位序号换算被替换字节长）。
         * 原实现 remove 长度传 0 退化为纯插入，每次击键净增一空槽。 */
        {
            int slotStart = xlc_countChars(self->m_text, self->m_cursor);
            int msChars = xlc_countChars(ms.s, ms.len);
            int replacedBytes = xlc_charsByteLen(self->m_text,
                                                 self->m_textLen,
                                                 slotStart + msChars)
                                - self->m_cursor;
            xlc_textReplace(self, self->m_cursor, replacedBytes,
                            ms.s, ms.len);
        }
        self->m_cursor += ms.len;
        self->m_cursor = xlc_nextMaskBlank(self, self->m_cursor);
        self->m_textDirty = true;
        xlc_strFree(&ms);
    } else {
        int remaining = self->m_maxLength - xlc_countChars(self->m_text,
                                                           self->m_textLen);
        if (remaining != 0) {
            int keepBytes = xlc_charsByteLen(s, sLen, remaining);
            int pos = self->m_cursor;
            int i;
            xlc_textInsert(self, pos, s, keepBytes);
            for (i = 0; i < keepBytes; ) {
                int seq = xlc_seqLenAt(s, sLen, i);
                if (seq <= 0) break;
                xlc_addCommand(self, xlc_cmdInsert(pos, s + i, seq));
                pos += seq;         /* 对标 Command(Insert, m_cursor++, ...)：
                                       每字符一条命令、光标逐字符推进。 */
                self->m_cursor = pos;
                i += seq;
            }
            self->m_textDirty = true;
        }
        if (xlc_countChars(s, sLen) > remaining)
            self->m_pendingInputRejected = 1u; /* text 系信号后补发 */
    }
}

/**
 * @brief 删除单个字符并入栈（对标 internalDelete）。
 * @details wasBackspace 记录命令方向（Remove/Delete）；掩码场景以占位
 *          串覆盖该位并追加 Insert 命令（可撤销恢复），普通场景移除。
 */
static void xlc_internalDelete(XLineControl* self, bool wasBackspace)
{
    int seq = 1;
    const char* ch;
    if (!self) return;
    if (self->m_cursor < self->m_textLen) {
        int cmdType;
        xlc_cancelPasswordEchoTimer(self);
        if (XLineControl_hasSelectedText(self)) {
            xlc_addCommand(self, xlc_cmdMark(
                (int)XLineControlCommandType_SetSelection,
                self->m_cursor, self->m_selstart, self->m_selend));
        }
        cmdType = (self->m_maskData ? 2 : 0)
                  + (wasBackspace ? (int)XLineControlCommandType_Remove
                                  : (int)XLineControlCommandType_Delete);
        ch = xlc_textCharAt(self, self->m_cursor, &seq);
        xlc_addCommand(self, xlc_cmdRemove(cmdType, self->m_cursor,
                                           ch, seq, -1, -1));
        if (self->m_maskData) {
            xlc_str blanked;
            xlc_strInit(&blanked);
            xlc_clearString(self, self->m_cursor, 1, &blanked);
            xlc_textReplace(self, self->m_cursor, seq,
                            blanked.s ? blanked.s : "", blanked.len);
            ch = xlc_textCharAt(self, self->m_cursor, &seq);
            xlc_addCommand(self, xlc_cmdInsert(self->m_cursor, ch, seq));
            xlc_strFree(&blanked);
        } else {
            xlc_textRemove(self, self->m_cursor, seq);
        }
        self->m_textDirty = true;
    }
}

/**
 * @brief 删除任意位置单个字符（对标头文件声明 internalRemove）。
 * @details Qt 6.8 中该方法仅有声明、无定义（私有死声明）；本实现按
 *          声明语义补齐为"定位到 pos 的掩码删除/普通删除"通用入口，
 *          供接入层组合使用，不改变 Qt 行为面。
 */
static void XLineControl_internalRemove(XLineControl* self, int pos)
{
    int seq;
    const char* ch;
    if (!self || pos < 0 || pos >= self->m_textLen) return;
    seq = xlc_seqLenAt(self->m_text, self->m_textLen, pos);
    if (seq <= 0) return;
    ch = self->m_text + pos;
    if (self->m_maskData) {
        xlc_str blanked;
        xlc_strInit(&blanked);
        xlc_addCommand(self, xlc_cmdRemove(
            (int)XLineControlCommandType_Delete, pos, ch, seq, -1, -1));
        xlc_clearString(self, pos, 1, &blanked);
        xlc_textReplace(self, pos, seq, blanked.s ? blanked.s : "", blanked.len);
        ch = xlc_textCharAt(self, pos, &seq);
        xlc_addCommand(self, xlc_cmdInsert(pos, ch, seq));
        xlc_strFree(&blanked);
    } else {
        xlc_addCommand(self, xlc_cmdRemove(
            (int)XLineControlCommandType_Delete, pos, ch, seq, -1, -1));
        xlc_textRemove(self, pos, seq);
    }
    self->m_textDirty = true;
}

/**
 * @brief 删除选区并入栈（对标 removeSelectedText）。
 * @details 光标在选区内时拆分 DeleteSelection 命令以精确恢复光标；
 *          在选区外时按 RemoveSelection 自尾向首入栈；掩码场景以占位
 *          串覆盖并追加 Insert 命令。可能使光标/长度临时非法（与 Qt
 *          一致，由 finishChange 收敛）。
 */
static void xlc_removeSelectedText(XLineControl* self)
{
    int i;
    if (!self) return;
    if (self->m_selstart < self->m_selend
        && self->m_selend <= self->m_textLen) {
        xlc_cancelPasswordEchoTimer(self);
        xlc_separate(self);
        xlc_addCommand(self, xlc_cmdMark(
            (int)XLineControlCommandType_SetSelection,
            self->m_cursor, self->m_selstart, self->m_selend));
        if (self->m_selstart <= self->m_cursor && self->m_cursor < self->m_selend) {
            /* 光标在选区内：按字符序号空间拆分命令以恢复正确光标位置。 */
            int curIdx = xlc_countChars(self->m_text, self->m_cursor);
            int startIdx = xlc_countChars(self->m_text, self->m_selstart);
            int endIdx = xlc_countChars(self->m_text, self->m_selend);
            for (i = curIdx; i >= startIdx; --i) {
                int b = xlc_charsByteLen(self->m_text, self->m_textLen, i);
                int seq = xlc_seqLenAt(self->m_text, self->m_textLen, b);
                const char* ch;
                if (seq <= 0) break;
                ch = xlc_textCharAt(self, b, &seq);
                xlc_addCommand(self, xlc_cmdRemove(
                    (int)XLineControlCommandType_DeleteSelection,
                    b, ch, seq, -1, 1));
            }
            for (i = endIdx - 1; i > curIdx; --i) {
                /* 对标 pos = i - m_cursor + m_selstart - 1（字符序号运算）。 */
                int b = xlc_charsByteLen(self->m_text, self->m_textLen,
                                         i - curIdx + startIdx - 1);
                int seq = xlc_seqLenAt(self->m_text, self->m_textLen,
                                       xlc_charsByteLen(self->m_text,
                                                        self->m_textLen, i));
                const char* ch;
                if (seq <= 0) break;
                ch = xlc_textCharAt(self,
                                    xlc_charsByteLen(self->m_text,
                                                     self->m_textLen, i),
                                    &seq);
                xlc_addCommand(self, xlc_cmdRemove(
                    (int)XLineControlCommandType_DeleteSelection,
                    b, ch, seq, -1, -1));
            }
        } else {
            for (i = self->m_selend - 1; i >= self->m_selstart; ) {
                int seq = xlc_seqLenAt(self->m_text, self->m_textLen, i);
                int prev;
                const char* ch;
                if (seq <= 0) break;
                ch = xlc_textCharAt(self, i, &seq);
                xlc_addCommand(self, xlc_cmdRemove(
                    (int)XLineControlCommandType_RemoveSelection,
                    i, ch, seq, -1, -1));
                prev = xlc_prevBoundary(self->m_text, self->m_textLen, i);
                /* i 回退到选区起点后 prevBoundary 恒返回原值；无进度即
                   断开，否则本循环原地压入海量撤销命令（历史数组指数
                   扩容直至 OOM）。 */
                if (prev >= i) break;
                i = prev;
            }
        }
        if (self->m_maskData) {
            xlc_str blanked;
            xlc_strInit(&blanked);
            xlc_clearString(self, self->m_selstart,
                            self->m_selend - self->m_selstart, &blanked);
            xlc_textReplace(self, self->m_selstart,
                            self->m_selend - self->m_selstart,
                            blanked.s ? blanked.s : "", blanked.len);
            {
                int w;
                for (w = 0; w < self->m_selend - self->m_selstart; ) {
                    int seq = xlc_seqLenAt(self->m_text, self->m_textLen,
                                           self->m_selstart + w);
                    const char* ch;
                    if (seq <= 0) break;
                    ch = xlc_textCharAt(self, self->m_selstart + w, &seq);
                    xlc_addCommand(self, xlc_cmdInsert(self->m_selstart + w,
                                                       ch, seq));
                    w += seq;
                }
            }
            xlc_strFree(&blanked);
        } else {
            xlc_textRemove(self, self->m_selstart,
                           self->m_selend - self->m_selstart);
        }
        if (self->m_cursor > self->m_selstart)
            self->m_cursor -= (self->m_cursor < self->m_selend
                                   ? self->m_cursor : self->m_selend)
                              - self->m_selstart;
        xlc_internalDeselect(self);
        self->m_textDirty = true;
    }
}

/**
 * @brief 内部设文本（对标 internalSetText）。
 * @details 取消回显定时器、清选区、发射 resetInputContext；掩码场景
 *          经 maskString 适配并补齐尾段占位，内容被改写且 edited 时
 *          发射 inputRejected；清空撤销历史并复位修改基准；光标钳位
 *          到 pos 或末尾；按文本是否变化结算（finishChange(-1,true,edited)）。
 */
static void xlc_internalSetText(XLineControl* self, const char* txt,
                                int txtLen, int pos, bool edited)
{
    char* oldText;
    int oldLen;
    if (!self) return;
    xlc_cancelPasswordEchoTimer(self);
    xlc_internalDeselect(self);
    xlc_emitVoid(self, (size_t)XLineControl_resetInputContext_signal(self));

    oldText = xlc_strdupRange(self->m_text, self->m_textLen);
    oldLen = self->m_textLen;
    if (self->m_maskData) {
        xlc_str masked;
        xlc_strInit(&masked);
        xlc_maskString(self, 0, txt ? txt : "", txtLen, true, &masked);
        xlc_clearString(self, masked.len,
                        self->m_maskDataCount - masked.len, &masked);
        xlc_bufAssign(&self->m_text, &self->m_textLen, &self->m_textCap,
                      masked.s ? masked.s : "", masked.len);
        xlc_strFree(&masked);
        if (edited && oldText && self->m_text
            && oldLen == self->m_textLen
            && XMemcmp(oldText, self->m_text, (size_t)oldLen) == 0)
            xlc_emitVoid(self, (size_t)XLineControl_inputRejected_signal(self));
    } else {
        int keepBytes = txtLen > 0
                        ? xlc_charsByteLen(txt, txtLen, self->m_maxLength)
                        : 0;
        xlc_bufAssign(&self->m_text, &self->m_textLen, &self->m_textCap,
                      txt, keepBytes);
    }
    self->m_historySize = 0;            /* 对标 m_history.clear() */
    self->m_modifiedState = self->m_undoState = 0;
    self->m_cursor = (pos < 0 || pos > self->m_textLen)
                     ? self->m_textLen : pos;
    self->m_textDirty = (oldLen != self->m_textLen)
                        || (oldText && self->m_text
                            && XMemcmp(oldText, self->m_text,
                                       (size_t)oldLen) != 0)
                        || (oldText == NULL && self->m_textLen > 0);
    if (oldText) XFree_System(oldText);
    xlc_finishChange(self, -1, true, edited);
}

/** @brief 光标位置变化信号收敛（对标 emitCursorPositionChanged）。 */
static void xlc_emitCursorPositionChanged(XLineControl* self)
{
    if (!self) return;
    if (self->m_cursor != self->m_lastCursorPos) {
        const int oldLast = self->m_lastCursorPos;
        self->m_lastCursorPos = self->m_cursor;
        xlc_emitInt2(self,
                     (size_t)XLineControl_cursorPositionChanged_signal(
                         self, oldLast, self->m_cursor),
                     oldLast, self->m_cursor);
    }
}

/** @brief 清选区内部入口（对标 internalDeselect；置 selDirty）。 */
static void xlc_internalDeselect(XLineControl* self)
{
    if (!self) return;
    if (self->m_selend > self->m_selstart) self->m_selDirty = true;
    self->m_selstart = 0;
    self->m_selend = 0;
}

/* ==================== 信号发射辅助（XObject_emitSignal 封装） ==================== */

/** @brief 发射无参信号。 */
static void xlc_emitVoid(XLineControl* self, size_t signal)
{
    XVarList* arguments;
    if (!self || signal == 0) return;
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_create(0);
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 int 单参信号。 */
static void xlc_emitInt(XLineControl* self, size_t signal, int value)
{
    XVarList* arguments;
    if (!self || signal == 0) return;
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 (int,int) 双参信号。 */
static void xlc_emitInt2(XLineControl* self, size_t signal, int a, int b)
{
    XVarList* arguments;
    if (!self || signal == 0) return;
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 const char* 单参信号。 */
static void xlc_emitText(XLineControl* self, size_t signal, const char* text)
{
    XVarList* arguments;
    if (!text) text = "";
    if (!self || signal == 0) return;
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(const char*, text));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 bool 单参信号。 */
static void xlc_emitBool(XLineControl* self, size_t signal, bool value)
{
    XVarList* arguments;
    int v = value ? 1 : 0;
    if (!self || signal == 0) return;
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, v));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 XRect 单参信号（按值拷贝入参）。 */
static void xlc_emitRect(XLineControl* self, size_t signal, XRect rect)
{
    XVarList* arguments;
    if (!self || signal == 0) return;
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(XRect, rect));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 信号标识（对标 QWidgetLineControl signals） ==================== */

void* XLineControl_cursorPositionChanged_signal(XLineControl* self, int oldPos, int newPos)
{ (void)self; (void)oldPos; (void)newPos;
  return (void*)(size_t)XLineControl_cursorPositionChanged_signal; }
void* XLineControl_selectionChanged_signal(XLineControl* self)
{ (void)self; return (void*)(size_t)XLineControl_selectionChanged_signal; }
void* XLineControl_displayTextChanged_signal(XLineControl* self, const char* text)
{ (void)self; (void)text;
  return (void*)(size_t)XLineControl_displayTextChanged_signal; }
void* XLineControl_textChanged_signal(XLineControl* self, const char* text)
{ (void)self; (void)text;
  return (void*)(size_t)XLineControl_textChanged_signal; }
void* XLineControl_textEdited_signal(XLineControl* self, const char* text)
{ (void)self; (void)text;
  return (void*)(size_t)XLineControl_textEdited_signal; }
void* XLineControl_resetInputContext_signal(XLineControl* self)
{ (void)self; return (void*)(size_t)XLineControl_resetInputContext_signal; }
void* XLineControl_updateMicroFocus_signal(XLineControl* self)
{ (void)self; return (void*)(size_t)XLineControl_updateMicroFocus_signal; }
void* XLineControl_accepted_signal(XLineControl* self)
{ (void)self; return (void*)(size_t)XLineControl_accepted_signal; }
void* XLineControl_editingFinished_signal(XLineControl* self)
{ (void)self; return (void*)(size_t)XLineControl_editingFinished_signal; }
void* XLineControl_updateNeeded_signal(XLineControl* self, XRect rect)
{ (void)self; (void)rect;
  return (void*)(size_t)XLineControl_updateNeeded_signal; }
void* XLineControl_inputRejected_signal(XLineControl* self)
{ (void)self; return (void*)(size_t)XLineControl_inputRejected_signal; }
void* XLineControl_editFocusChange_signal(XLineControl* self, bool moving)
{ (void)self; (void)moving;
  return (void*)(size_t)XLineControl_editFocusChange_signal; }

/* ==================== 回显定时器（对标 cancelPasswordEchoTimer） ==================== */

/** @brief 取消密码回显延迟定时器（对标 cancelPasswordEchoTimer）。 */
static void xlc_cancelPasswordEchoTimer(XLineControl* self)
{
    if (!self) return;
    if (self->m_passwordEchoTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_passwordEchoTimer);
        self->m_passwordEchoTimer = XTIMER_INVALID_ID;
    }
}

/* ==================== 文本模型公共 API ==================== */

const char* XLineControl_text(const XLineControl* self)
{
    if (!self) return "";
    /* 掩码模式对齐 Qt：text() 为剥离占位符（保留分隔符）的口径，
       由 refreshTextReturn 维护；无掩码即原始文本。 */
    if (self->m_maskData)
        return self->m_textReturn ? self->m_textReturn : "";
    return self->m_text ? self->m_text : "";
}

const char* XLineControl_displayText(const XLineControl* self)
{
    if (!self) return "";
    return self->m_displayText ? self->m_displayText : "";
}

void XLineControl_setText(XLineControl* self, const char* txt)
{
    int txtLen = (txt && self) ? (int)XStrlen(txt) : 0;
    if (!self) return;
    if (XLineControl_composeMode(self)) {
#if XGUIAPPLICATION_ON
        XInputMethod* im = XGuiApplication_inputMethod();
        if (im) XInputMethod_reset(im);
#endif
    }
    xlc_internalSetText(self, txt, txtLen, -1, false);
}

void XLineControl_insert(XLineControl* self, const char* newText)
{
    int priorState;
    int len;
    if (!self) return;
    priorState = self->m_undoState;
    len = newText ? (int)XStrlen(newText) : 0;
    xlc_removeSelectedText(self);
    xlc_internalInsert(self, newText, len);
    xlc_finishChange(self, priorState, false, true);
}

void XLineControl_clear(XLineControl* self)
{
    int priorState;
    if (!self) return;
    priorState = self->m_undoState;
    self->m_selstart = 0;
    self->m_selend = self->m_textLen;
    xlc_removeSelectedText(self);
    xlc_separate(self);
    xlc_finishChange(self, priorState, false, false);
}

void XLineControl_backspace(XLineControl* self)
{
    int priorState;
    if (!self) return;
    priorState = self->m_undoState;
    if (XLineControl_hasSelectedText(self)) {
        xlc_removeSelectedText(self);
    } else if (self->m_cursor) {
        self->m_cursor = xlc_prevBoundary(self->m_text, self->m_textLen,
                                          self->m_cursor);
        if (self->m_maskData)
            self->m_cursor = xlc_prevMaskBlank(self, self->m_cursor);
        if (self->m_cursor > 0
            && xlc_charAtIsContinuation(self, self->m_cursor)) {
            /* 对标 Qt：低代理在前的成对删除分支（UTF-8 续字节等价）。 */
            int prev = xlc_prevBoundary(self->m_text, self->m_textLen,
                                        self->m_cursor);
            if (prev < self->m_cursor
                && xlc_seqLenAt(self->m_text, self->m_textLen, prev) > 1) {
                xlc_internalDelete(self, true);
                self->m_cursor = xlc_prevBoundary(self->m_text,
                                                  self->m_textLen,
                                                  self->m_cursor);
            }
        }
        xlc_internalDelete(self, true);
    }
    xlc_finishChange(self, priorState, false, true);
}

void XLineControl_del(XLineControl* self)
{
    int priorState;
    if (!self) return;
    priorState = self->m_undoState;
    if (XLineControl_hasSelectedText(self)) {
        xlc_removeSelectedText(self);
    } else {
        /* 对标 Qt del()（nextCursorPosition 默认 SkipCharacters）：仅删
           光标处单个字符；本实现 internalDelete 每调用移除一个完整
           UTF-8 序列，故按字符计一次，而非按 SkipWords 字节距离循环。 */
        if (self->m_cursor < self->m_textLen)
            xlc_internalDelete(self, false);
    }
    xlc_finishChange(self, priorState, false, true);
}

const char* XLineControl_surroundingText(const XLineControl* self)
{
    if (!self) return "";
    return self->m_text ? self->m_text : "";
}

/* ==================== 撤销栈公共 API ==================== */

bool XLineControl_isUndoAvailable(const XLineControl* self)
{
    if (!self) return false;
    /* 密码模式下出于安全仅允许撤销到清空（末命令为 Insert 型）。 */
    return !self->m_readOnly && self->m_undoState > 0
           && (self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal
               || self->m_history[self->m_undoState - 1].type
                  == (int)XLineControlCommandType_Insert);
}

bool XLineControl_isRedoAvailable(const XLineControl* self)
{
    if (!self) return false;
    /* 密码模式一律禁用重做（安全约束，与 Qt 一致）。 */
    return !self->m_readOnly
           && self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal
           && self->m_undoState < self->m_historySize;
}

void XLineControl_clearUndo(XLineControl* self)
{
    if (!self) return;
    self->m_historySize = 0;
    self->m_modifiedState = self->m_undoState = 0;
}

bool XLineControl_isModified(const XLineControl* self)
{
    if (!self) return false;
    return self->m_modifiedState != self->m_undoState;
}

void XLineControl_setModified(XLineControl* self, bool modified)
{
    if (!self) return;
    self->m_modifiedState = modified ? -1 : self->m_undoState;
}

void XLineControl_undo(XLineControl* self)
{
    if (!self) return;
    if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal) {
        xlc_internalUndo(self, -1);
        xlc_finishChange(self, -1, true, true);
    } else {
        /* 密码模式下撤销退化为清空（安全约束）。 */
        xlc_cancelPasswordEchoTimer(self);
        XLineControl_clear(self);
    }
}

void XLineControl_redo(XLineControl* self)
{
    if (!self) return;
    xlc_internalRedo(self);
    xlc_finishChange(self, -1, true, true);
}

const char* XLineControl_undoText(const XLineControl* self)
{
    const XLineControlCommand* cmd;
    if (!self || !XLineControl_isUndoAvailable(self)) return NULL;
    cmd = &self->m_history[self->m_undoState - 1];
    switch (cmd->type) {
    case (int)XLineControlCommandType_Insert:
        return "undoInsert";
    case (int)XLineControlCommandType_Remove:
    case (int)XLineControlCommandType_RemoveSelection:
    case (int)XLineControlCommandType_Delete:
    case (int)XLineControlCommandType_DeleteSelection:
        return "undoDelete";
    default:
        return "undo";
    }
}

const char* XLineControl_redoText(const XLineControl* self)
{
    const XLineControlCommand* cmd;
    if (!self || !XLineControl_isRedoAvailable(self)) return NULL;
    cmd = &self->m_history[self->m_undoState];
    switch (cmd->type) {
    case (int)XLineControlCommandType_Insert:
        return "redoInsert";
    case (int)XLineControlCommandType_Remove:
    case (int)XLineControlCommandType_RemoveSelection:
    case (int)XLineControlCommandType_Delete:
    case (int)XLineControlCommandType_DeleteSelection:
        return "redoDelete";
    default:
        return "redo";
    }
}

/* ==================== 选区族公共 API ==================== */

bool XLineControl_hasSelectedText(const XLineControl* self)
{
    if (!self) return false;
    return self->m_textLen > 0 && self->m_selend > self->m_selstart;
}

bool XLineControl_allSelected(const XLineControl* self)
{
    if (!self) return false;
    return self->m_textLen > 0 && self->m_selstart == 0
           && self->m_selend == self->m_textLen;
}

int XLineControl_selectionStart(const XLineControl* self)
{ return XLineControl_hasSelectedText(self) ? self->m_selstart : -1; }

int XLineControl_selectionEnd(const XLineControl* self)
{ return XLineControl_hasSelectedText(self) ? self->m_selend : -1; }

/** @brief 堆拷贝字节区间（UTF-8；失败返回 NULL）。 */
static char* xlc_midDup(const XLineControl* self, int start, int end)
{
    if (!self || start < 0 || end > self->m_textLen || start >= end)
        return NULL;
    return xlc_strdupRange(self->m_text + start, end - start);
}

char* XLineControl_selectedText(const XLineControl* self)
{
    if (!XLineControl_hasSelectedText(self)) return NULL;
    return xlc_midDup(self, self->m_selstart, self->m_selend);
}

char* XLineControl_textBeforeSelection(const XLineControl* self)
{
    if (!XLineControl_hasSelectedText(self)) return NULL;
    return xlc_midDup(self, 0, self->m_selstart);
}

char* XLineControl_textAfterSelection(const XLineControl* self)
{
    if (!XLineControl_hasSelectedText(self)) return NULL;
    return xlc_midDup(self, self->m_selend, self->m_textLen);
}

int XLineControl_textStart(const XLineControl* self)
{ (void)self; return 0; }

int XLineControl_textEnd(const XLineControl* self)
{ return self ? self->m_textLen : 0; }

bool XLineControl_inSelection(const XLineControl* self, int x)
{
    int pos;
    if (!self || self->m_selstart >= self->m_selend) return false;
    pos = XLineControl_xToPos(self, x,
                              (int)XLineControlCursorPosition_OnCharacter);
    return pos >= self->m_selstart && pos < self->m_selend;
}

void XLineControl_removeSelection(XLineControl* self)
{
    int priorState;
    if (!self) return;
    priorState = self->m_undoState;
    xlc_removeSelectedText(self);
    xlc_finishChange(self, priorState, false, true);
}

void XLineControl_deselect(XLineControl* self)
{
    if (!self) return;
    xlc_internalDeselect(self);
    xlc_finishChange(self, -1, false, true);
}

void XLineControl_selectAll(XLineControl* self)
{
    if (!self) return;
    self->m_selstart = self->m_selend = self->m_cursor = 0;
    XLineControl_moveCursor(self, self->m_textLen, true);
}

void XLineControl_setSelection(XLineControl* self, int start, int length)
{
    int textLen;
    if (!self) return;
    XLineControl_commitPreedit(self);

    textLen = self->m_textLen;
    if (start < 0 || start > textLen) {
        /* 对标 qWarning("...Invalid start position")。 */
        return;
    }

    if (length > 0) {
        int endByte = start + xlc_charsByteLen(self->m_text + start,
                                               textLen - start, length);
        if (start == self->m_selstart && endByte == self->m_selend
            && self->m_cursor == self->m_selend)
            return;
        self->m_selstart = start;
        self->m_selend = endByte < textLen ? endByte : textLen;
        self->m_cursor = self->m_selend;
    } else if (length < 0) {
        /* 负长度：从 start 起向左取 |length| 个字符的字节起点。 */
        int startByte = start - xlc_prevCharsByteLen(self->m_text, start,
                                                     -length);
        if (start == self->m_selend && startByte == self->m_selstart
            && self->m_cursor == self->m_selstart)
            return;
        self->m_selstart = startByte < 0 ? 0 : startByte;
        self->m_selend = start;
        self->m_cursor = self->m_selstart;
    } else if (self->m_selstart != self->m_selend) {
        self->m_selstart = 0;
        self->m_selend = 0;
        self->m_cursor = start;
    } else {
        self->m_cursor = start;
        xlc_emitCursorPositionChanged(self);
        return;
    }
    xlc_emitVoid(self, (size_t)XLineControl_selectionChanged_signal(self));
    xlc_emitCursorPositionChanged(self);
}

void XLineControl_selectWordAtPos(XLineControl* self, int cursor)
{
    int next;
    int c;
    int endPos;
    if (!self) return;
    next = xlc_nextBoundary(self->m_text, self->m_textLen, cursor);
    if (next > XLineControl_textEnd(self)) next = self->m_textLen;
    c = xlc_prevCursorPosition(self, next);
    XLineControl_moveCursor(self, c, false);
    endPos = xlc_nextCursorPosition(self, c);
    /* 词尾空白不入选（对标 while (end > cursor && m_text[end-1].isSpace())）。 */
    while (endPos > cursor) {
        int pb = xlc_prevBoundary(self->m_text, self->m_textLen, endPos);
        uint32_t cp = xlc_decodeAt(self->m_text, self->m_textLen, pb, NULL);
        if (!xlc_isSpaceCp(cp)) break;
        endPos = pb;
    }
    XLineControl_moveCursor(self, endPos, true);
}

void XLineControl_deleteSelected(XLineControl* self)
{
    int priorState;
    if (!self || !XLineControl_hasSelectedText(self)) return;
    priorState = self->m_undoState;
    xlc_emitVoid(self, (size_t)XLineControl_resetInputContext_signal(self));
    xlc_removeSelectedText(self);
    xlc_separate(self);
    xlc_finishChange(self, priorState, false, true);
}

/* ==================== 光标族公共 API ==================== */

int XLineControl_cursor(const XLineControl* self)
{ return self ? self->m_cursor : 0; }

int XLineControl_preeditCursor(const XLineControl* self)
{ return self ? self->m_preeditCursor : 0; }

int XLineControl_cursorPosition(const XLineControl* self)
{ return self ? self->m_cursor : 0; }

void XLineControl_setCursorPosition(XLineControl* self, int pos)
{
    if (!self) return;
    if (pos <= self->m_textLen)
        XLineControl_moveCursor(self, pos < 0 ? 0 : pos, false);
}

int XLineControl_cursorWidth(const XLineControl* self)
{ return self ? self->m_cursorWidth : 0; }

void XLineControl_setCursorWidth(XLineControl* self, int value)
{ if (self) self->m_cursorWidth = value; }

int XLineControl_cursorMoveStyle(const XLineControl* self)
{ return self ? self->m_cursorMoveStyle : 0; }

void XLineControl_setCursorMoveStyle(XLineControl* self, int style)
{
    if (!self) return;
    self->m_cursorMoveStyle = style;
}

void XLineControl_moveCursor(XLineControl* self, int pos, bool mark)
{
    if (!self) return;
    XLineControl_commitPreedit(self);

    if (pos != self->m_cursor) {
        xlc_separate(self);
        if (self->m_maskData)
            pos = pos > self->m_cursor ? xlc_nextMaskBlank(self, pos)
                                       : xlc_prevMaskBlank(self, pos);
    }
    if (mark) {
        int anchor;
        if (self->m_selend > self->m_selstart && self->m_cursor == self->m_selstart)
            anchor = self->m_selend;
        else if (self->m_selend > self->m_selstart && self->m_cursor == self->m_selend)
            anchor = self->m_selstart;
        else
            anchor = self->m_cursor;
        self->m_selstart = anchor < pos ? anchor : pos;
        self->m_selend = anchor > pos ? anchor : pos;
        xlc_updateDisplayText(self, false);
    } else {
        xlc_internalDeselect(self);
    }
    self->m_cursor = pos;
    if (mark || self->m_selDirty) {
        self->m_selDirty = false;
        xlc_emitVoid(self, (size_t)XLineControl_selectionChanged_signal(self));
    }
    xlc_emitCursorPositionChanged(self);
}

void XLineControl_cursorForward(XLineControl* self, bool mark, int steps)
{
    int c;
    if (!self) return;
    c = self->m_cursor;
    if (steps > 0) {
        while (steps-- > 0)
            c = xlc_nextBoundary(self->m_text, self->m_textLen, c);
    } else if (steps < 0) {
        while (steps++ < 0)
            c = xlc_prevBoundary(self->m_text, self->m_textLen, c);
    }
    XLineControl_moveCursor(self, c, mark);
}

void XLineControl_cursorWordForward(XLineControl* self, bool mark)
{
    if (!self) return;
    XLineControl_moveCursor(self, xlc_nextCursorPosition(self, self->m_cursor),
                            mark);
}

void XLineControl_cursorWordBackward(XLineControl* self, bool mark)
{
    if (!self) return;
    XLineControl_moveCursor(self, xlc_prevCursorPosition(self, self->m_cursor),
                            mark);
}

void XLineControl_home(XLineControl* self, bool mark)
{ XLineControl_moveCursor(self, 0, mark); }

void XLineControl_end(XLineControl* self, bool mark)
{ if (self) XLineControl_moveCursor(self, self->m_textLen, mark); }

int XLineControl_xToPos(const XLineControl* self, int x, int betweenOrOn)
{
#if XPAINTER_ON
    const char* s;
    int len;
    int pos = 0;
    int acc = 0;
    if (!self || !self->m_layoutText || x <= 0) return 0;
    s = self->m_layoutText;
    len = self->m_layoutLen;
    while (pos < len) {
        int seq = xlc_seqLenAt(s, len, pos);
        int w;
        if (seq <= 0) break;
        w = XPainter_textWidthRange(self->m_font, s, pos, pos + seq);
        if (betweenOrOn == (int)XLineControlCursorPosition_OnCharacter) {
            if (x < acc + w) return pos; /* x 落在该字符内。 */
        } else {
            if (x <= acc + w / 2) return pos; /* 就近边界。 */
        }
        acc += w;
        pos += seq;
    }
    return len;
#else
    (void)self; (void)x; (void)betweenOrOn;
    return 0;
#endif /* XPAINTER_ON */
}

XRect XLineControl_rectForPos(const XLineControl* self, int pos)
{
    XRect r;
    int cix;
    int layoutPos;
    if (!self) { r.x = 0; r.y = 0; r.width = 0; r.height = 0; return r; }
    layoutPos = (pos == self->m_cursor)
        ? xlc_cursorLayoutPos(self)
        : xlc_mapTextToLayout(self, pos);
    cix = xlc_layoutCursorToX(self, layoutPos);
    r.x = cix - XLC_CURSOR_RECT_PAD;
    r.y = 0;
    r.width = self->m_cursorWidth + 2 * XLC_CURSOR_RECT_PAD - 1;
    r.height = self->m_layoutLineHeight + 1;
    return r;
}

XRect XLineControl_cursorRect(const XLineControl* self)
{ return XLineControl_rectForPos(self, XLineControl_cursor(self)); }

XRect XLineControl_anchorRect(const XLineControl* self)
{
    if (!self) return XLineControl_cursorRect(self);
    if (!XLineControl_hasSelectedText(self))
        return XLineControl_cursorRect(self);
    return XLineControl_rectForPos(self,
                                   self->m_cursor == self->m_selstart
                                       ? self->m_selend
                                       : self->m_selstart);
}

int XLineControl_cursorToX(const XLineControl* self, int cursor)
{
    if (!self) return 0;
    return xlc_layoutCursorToX(self, cursor);
}

int XLineControl_cursorToXCurrent(const XLineControl* self)
{
    if (!self) return 0;
    return xlc_layoutCursorToX(self, xlc_cursorLayoutPos(self));
}

/* ==================== 只读/长度/拖拽公共 API ==================== */

bool XLineControl_isReadOnly(const XLineControl* self)
{ return self ? self->m_readOnly != 0u : false; }

void XLineControl_setReadOnly(XLineControl* self, bool enable)
{
    if (!self || (self->m_readOnly != 0u) == enable) return;
    self->m_readOnly = enable ? 1u : 0u;
    XLineControl_updateCursorBlinking(self);
}

int XLineControl_maxLength(const XLineControl* self)
{ return self ? self->m_maxLength : 0; }

void XLineControl_setMaxLength(XLineControl* self, int maxLength)
{
    if (!self) return;
    if (self->m_maskData) return; /* 掩码长度优先（对标 Qt）。 */
    self->m_maxLength = maxLength;
    xlc_internalSetText(self, self->m_text, self->m_textLen, -1, false);
}

bool XLineControl_dragEnabled(const XLineControl* self)
{ return self ? self->m_dragEnabled != 0u : false; }

void XLineControl_setDragEnabled(XLineControl* self, bool enable)
{ if (self) self->m_dragEnabled = enable ? 1u : 0u; }

/* ==================== 回显状态机公共 API ==================== */

uint32_t XLineControl_echoMode(const XLineControl* self)
{ return self ? self->m_echoMode : 0u; }

void XLineControl_setEchoMode(XLineControl* self, uint32_t mode)
{
    if (!self) return;
    xlc_cancelPasswordEchoTimer(self);
    self->m_echoMode = mode & 0x3u; /* 对标 uint m_echoMode : 2 位域截断。 */
    self->m_passwordEchoEditing = false;
    if (self->m_echoMode != (uint32_t)XLineControlEchoMode_Normal) {
        /* 密码场景预留容量，降低重分配暴露面（对标 m_text.reserve(30)）。 */
        xlc_bufReserve(&self->m_text, &self->m_textCap,
                       XLC_PASSWORD_RESERVE_CHARS * 4);
    }
    xlc_updateDisplayText(self, false);
}

const char* XLineControl_passwordCharacter(const XLineControl* self)
{ return (self && self->m_passwordCharacter[0]) ? self->m_passwordCharacter : "*"; }

void XLineControl_setPasswordCharacter(XLineControl* self, const char* character)
{
    int seq;
    if (!self) return;
    seq = character ? xlc_seqLenAt(character, (int)XStrlen(character), 0) : 0;
    if (seq <= 0) {
        self->m_passwordCharacter[0] = '*';
        self->m_passwordCharacter[1] = '\0';
    } else {
        xlc_maskCharSet(self->m_passwordCharacter, character, seq);
    }
    xlc_updateDisplayText(self, false);
}

int XLineControl_passwordMaskDelay(const XLineControl* self)
{ return self ? self->m_passwordMaskDelay : -1; }

void XLineControl_setPasswordMaskDelay(XLineControl* self, int delay)
{ if (self) self->m_passwordMaskDelay = delay; }

bool XLineControl_passwordEchoEditing(const XLineControl* self)
{
    if (!self) return false;
    if (self->m_passwordEchoTimer != XTIMER_INVALID_ID) return true;
    return self->m_passwordEchoEditing;
}

void XLineControl_updatePasswordEchoEditing(XLineControl* self, bool editing)
{
    if (!self) return;
    xlc_cancelPasswordEchoTimer(self);
    self->m_passwordEchoEditing = editing;
    xlc_updateDisplayText(self, false);
}

/* ==================== 校验与掩码公共 API ==================== */

void XLineControl_setValidator(XLineControl* self, void* validator,
                               XLineControlValidateFunc validateFunc,
                               XLineControlFixupFunc fixupFunc,
                               void* userData)
{
    if (!self) return;
    self->m_validator = validator;
    self->m_validateFunc = validator ? validateFunc : NULL;
    self->m_fixupFunc = validator ? fixupFunc : NULL;
    self->m_validatorUserData = userData;
}

void* XLineControl_validator(const XLineControl* self)
{ return self ? self->m_validator : NULL; }

/**
 * @brief 文本级可接受判定（对标私有 hasAcceptableInput(str)）。
 * @details 校验钩子须 Acceptable；掩码场景要求长度等于槽位表长度且
 *          逐槽匹配（分隔符精确、编辑位 isValidInput）。
 */
static bool xlc_textHasAcceptableInput(const XLineControl* self,
                                       const char* str, int strLen)
{
    int i;
    int pos;
    if (!self) return false;
    if (self->m_validateFunc && self->m_validator) {
        char* textCopy = xlc_strdupRange(str, strLen);
        int cursorCopy = self->m_cursor;
        if (!textCopy) return false;
        if (self->m_validateFunc(self->m_validator, &textCopy, &cursorCopy,
                                 self->m_validatorUserData)
            != (int)XLineControlValidatorState_Acceptable) {
            XFree_System(textCopy);
            return false;
        }
        XFree_System(textCopy);
    }
    if (!self->m_maskData) return true;
    if (strLen != self->m_maskDataCount) return false;
    pos = 0;
    for (i = 0; i < self->m_maskDataCount; ++i) {
        uint32_t cp;
        int seq = xlc_seqLenAt(str, strLen, pos);
        if (seq <= 0) return false;
        cp = xlc_decodeAt(str, strLen, pos, NULL);
        if (self->m_maskData[i].m_separator) {
            if (cp != xlc_maskCpAt(self, i)) return false;
        } else {
            if (!xlc_isValidInput(self, cp, xlc_maskCpAt(self, i)))
                return false;
        }
        pos += seq;
    }
    return true;
}

bool XLineControl_hasAcceptableInput(const XLineControl* self)
{
    if (!self) return false;
    return xlc_textHasAcceptableInput(self, self->m_text, self->m_textLen);
}

bool XLineControl_fixup(XLineControl* self)
{
    char* textCopy;
    int cursorCopy;
    int copyLen;
    if (!self || !self->m_fixupFunc || !self->m_validator) return false;
    /* 前置：仅在当前不可接受时修复（对标 Qt 注释约定）。 */
    textCopy = xlc_strdupRange(self->m_text, self->m_textLen);
    if (!textCopy) return false;
    cursorCopy = self->m_cursor;
    self->m_fixupFunc(self->m_validator, &textCopy, self->m_validatorUserData);
    copyLen = (int)XStrlen(textCopy);
    if (xlc_textHasAcceptableInput(self, textCopy, copyLen)) {
        bool changedText = (self->m_textLen != copyLen)
                           || XMemcmp(self->m_text, textCopy,
                                      (size_t)copyLen) != 0;
        if (changedText || cursorCopy != self->m_cursor)
            xlc_internalSetText(self, textCopy, copyLen, cursorCopy, false);
        XFree_System(textCopy);
        return true;
    }
    XFree_System(textCopy);
    return false;
}

const char* XLineControl_inputMask(const XLineControl* self)
{
    if (!self) return "";
    return self->m_maskReturn ? self->m_maskReturn : "";
}

/** @brief 刷新 inputMask() 组装缓存（"掩码[;占位]"）。 */
static void xlc_refreshMaskReturn(XLineControl* self)
{
    if (!self) return;
    if (self->m_inputMask) {
        xlc_str mask;
        xlc_strInit(&mask);
        xlc_strAppendUtf8(&mask, self->m_inputMask,
                          (int)XStrlen(self->m_inputMask));
        if (XStrcmp(self->m_blank, " ") != 0) {
            xlc_strAppendCh(&mask, ';');
            xlc_strAppendUtf8(&mask, self->m_blank,
                              (int)XStrlen(self->m_blank));
        }
        xlc_bufAssign(&self->m_maskReturn, &self->m_maskReturnLen,
                      &self->m_maskReturnCap, mask.s ? mask.s : "", mask.len);
        xlc_strFree(&mask);
    } else {
        xlc_bufAssign(&self->m_maskReturn, &self->m_maskReturnLen,
                      &self->m_maskReturnCap, "", 0);
    }
}

void XLineControl_setInputMask(XLineControl* self, const char* mask)
{
    if (!self) return;
    xlc_parseInputMask(self, mask);
    xlc_refreshMaskReturn(self);
    if (self->m_maskData)
        XLineControl_moveCursor(self, xlc_nextMaskBlank(self, 0), false);
}

int XLineControl_nextMaskBlank(XLineControl* self, int pos)
{
    if (!self || !self->m_maskData) return pos;
    return xlc_nextMaskBlank(self, pos);
}

int XLineControl_prevMaskBlank(XLineControl* self, int pos)
{
    if (!self || !self->m_maskData) return pos;
    return xlc_prevMaskBlank(self, pos);
}

/* ==================== IME 公共 API ==================== */

bool XLineControl_composeMode(const XLineControl* self)
{
    if (!self) return false;
    return self->m_preeditLen > 0;
}

void XLineControl_setPreeditArea(XLineControl* self, int cursor, const char* text)
{
    int len;
    if (!self) return;
    len = text ? (int)XStrlen(text) : 0;
    xlc_bufAssign(&self->m_preeditText, &self->m_preeditLen,
                  &self->m_preeditCap, text, len);
    self->m_preeditPos = cursor;
    xlc_redoTextLayout(self);
}

const char* XLineControl_preeditAreaText(const XLineControl* self)
{
    if (!self) return "";
    return self->m_preeditText ? self->m_preeditText : "";
}

void XLineControl_commitPreedit(XLineControl* self)
{
    if (!self || !XLineControl_composeMode(self)) return;
#if XGUIAPPLICATION_ON
    {
        XInputMethod* im = XGuiApplication_inputMethod();
        if (im) XInputMethod_commit(im);
    }
#endif
    if (!XLineControl_composeMode(self)) return;
    self->m_preeditCursor = 0;
    XLineControl_setPreeditArea(self, -1, "");
    xlc_updateDisplayText(self, true);
}

const char* XLineControl_cancelText(const XLineControl* self)
{ return (self && self->m_cancelText) ? self->m_cancelText : ""; }

void XLineControl_setCancelText(XLineControl* self, const char* text)
{
    int len;
    if (!self) return;
    len = text ? (int)XStrlen(text) : 0;
    xlc_bufAssign(&self->m_cancelText, NULL, &self->m_cancelTextCap, text, len);
}

/* ==================== IME 事件处理（对标 processInputMethodEvent） ==================== */

/**
 * @brief 把 preedit 串按回显模式掩码化（对标 Password 分支的 preeditString.fill）。
 */
static void xlc_maskedPreedit(const XLineControl* self, const char* preedit,
                              int preeditLen, xlc_str* out)
{
    int pos = 0;
    int passLen = (int)XStrlen(self->m_passwordCharacter);
    if (passLen <= 0) passLen = 1;
    while (pos < preeditLen) {
        int seq = xlc_seqLenAt(preedit, preeditLen, pos);
        if (seq <= 0) break;
        xlc_strAppendUtf8(out, self->m_passwordCharacter, passLen);
        pos += seq;
    }
}

void XLineControl_processInputMethodEvent(XLineControl* self,
                                          XInputMethodEvent* event)
{
    int priorState = -1;
    bool isGettingInput;
    bool cursorPositionChanged = false;
    bool selectionChange = false; /* XGui IME 事件无文本选区属性，恒 false。 */
    const char* commit = "";
    const char* preedit = "";
    int commitLen;
    int preeditLen;
    int replacementStart;
    int replacementLength;
    int c;

    if (!self || !event) return;
#if !XWINDOWEVENT_ON
    (void)event;
#endif
#if XWINDOWEVENT_ON
    if (event->m_commitString && XString_size_base((const XContainer*)event->m_commitString) > 0)
        commit = XString_toUtf8(event->m_commitString);
    if (event->m_preeditString && XString_size_base((const XContainer*)event->m_preeditString) > 0)
        preedit = XString_toUtf8(event->m_preeditString);
    replacementStart = event->m_replacementStart;
    replacementLength = event->m_replacementLength;
#else
    replacementStart = 0;
    replacementLength = 0;
#endif
    commitLen = (int)XStrlen(commit);
    preeditLen = (int)XStrlen(preedit);

    isGettingInput = commitLen > 0
                     || preeditLen != (int)XStrlen(XLineControl_preeditAreaText(self))
                     || replacementLength > 0;

    if (isGettingInput) {
        /* 有输入进入：先删选区（对标 Qt）。 */
        priorState = self->m_undoState;
        if (self->m_echoMode == (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit
            && !XLineControl_passwordEchoEditing(self)) {
            XLineControl_updatePasswordEchoEditing(self, true);
            self->m_selstart = 0;
            self->m_selend = self->m_textLen;
        }
        xlc_removeSelectedText(self);
    }

    c = self->m_cursor; /* 提交串插入后的光标预估位。 */
    if (replacementStart <= 0) {
        int removed = -replacementStart < replacementLength
                          ? -replacementStart : replacementLength;
        c += commitLen - (removed > 0 ? removed : 0);
    }

    self->m_cursor += replacementStart;
    if (self->m_cursor < 0) self->m_cursor = 0;

    /* 替换区：以光标为基准的删除。 */
    if (replacementLength) {
        self->m_selstart = self->m_cursor;
        self->m_selend = self->m_selstart + replacementLength;
        xlc_removeSelectedText(self);
    }
    if (commitLen > 0) {
        xlc_internalInsert(self, commit, commitLen);
        cursorPositionChanged = true;
    } else {
        self->m_cursor = c < 0 ? 0 : (c > self->m_textLen ? self->m_textLen : c);
    }

    /* Qt 的 Selection 属性循环（文本选区定位）在 XGui IME 事件契约中
     * 由 cursorPosition/anchorPosition 表达为 preedit 内光标，本实现
     * 仅驱动 preeditCursor（见下）；文本选区路径保留 selectionChange
     * 结构位（恒 false，@note）。 */

    /* preedit 区按回显模式写入（对标 NoEcho/Password/default 三分支）。 */
    switch (self->m_echoMode) {
    case (uint32_t)XLineControlEchoMode_NoEcho:
        XLineControl_setPreeditArea(self, 0, "");
        break;
    case (uint32_t)XLineControlEchoMode_Password: {
        xlc_str masked;
        xlc_strInit(&masked);
        xlc_maskedPreedit(self, preedit, preeditLen, &masked);
        XLineControl_setPreeditArea(self, self->m_cursor,
                                    masked.s ? masked.s : "");
        xlc_strFree(&masked);
        break;
    }
    default:
        XLineControl_setPreeditArea(self, self->m_cursor, preedit);
        break;
    }

    {
        const int oldPreeditCursor = self->m_preeditCursor;
        /* m_preeditCursor 统一存「组合串内字节偏移」：默认为组合串尾
           （读存储串长度——Password 回显时存储的是掩码转写串，字节长
           与原文不同）；消费端 xlc_cursorLayoutPos 直接按布局字节坐标
           叠加，中文每字符 3 字节，混用字符序号会落到字符中间。 */
        self->m_preeditCursor = self->m_preeditLen;
        self->m_hideCursor = false;
#if XWINDOWEVENT_ON
        /* 对标 Cursor 属性：XGui 事件以 cursorPosition 表达 preedit 内
         * 光标（>=0 有效；字符序号，换算字节）；无 length 位，恒显示
         * 光标。 */
        if (event->m_cursorPosition >= 0) {
            int acc = 0;
            int remain = event->m_cursorPosition;
            while (remain > 0 && acc < self->m_preeditLen) {
                int seq = xlc_seqLenAt(self->m_preeditText,
                                       self->m_preeditLen, acc);
                if (seq <= 0) break;
                acc += seq;
                --remain;
            }
            self->m_preeditCursor = acc;
            self->m_hideCursor = false;
        }
#endif
        /* 对标 TextFormat 属性：XGui 无富文本格式承载，不参与布局。 */
        xlc_updateDisplayText(self, true);
        if (cursorPositionChanged)
            xlc_emitCursorPositionChanged(self);
        else if (self->m_preeditCursor != oldPreeditCursor)
            xlc_emitVoid(self,
                         (size_t)XLineControl_updateMicroFocus_signal(self));
    }

    if (isGettingInput)
        xlc_finishChange(self, priorState, false, true);

    if (selectionChange)
        xlc_emitVoid(self, (size_t)XLineControl_selectionChanged_signal(self));

#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
    /* 对标 Qt QLineEdit::inputMethodEvent 尾段（qlineedit.cpp:1812-1815）：
     * `if (!e->commitString().isEmpty()) d->control->complete(Qt::Key_unknown);`
     * ——IME 提交落定后驱动一次补全。修复 night #30：真实 X11 键入在西文
     * 路径（@im=none 直映/fcitx 透传）经 Xutf8LookupString 以输入法提交
     * 形态进入本函数，不再走 processKeyEvent 的按键插入分支，complete()
     * 永不触发、默认弹层永不出现（apitest 以直发按键注入故测试通过——
     * 与真实页面路径的口径差异即此）。键值用 XKey_None 哨兵（Qt 用
     * Key_unknown）：complete() 仅在 Inline 模式区分 Up/Down/Backspace，
     * 哨兵不与其重合。 */
    if (commitLen > 0)
        XLineControl_complete(self, XKey_None);
#endif /* XLC_COMPLETER_ON */
}

/* ==================== 剪贴板公共 API（对标 copy/paste） ==================== */

void XLineControl_copy(const XLineControl* self, int mode)
{
    char* t;
    bool written = false;
    if (!self) return;
    t = XLineControl_selectedText(self);
    if (t && self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal) {
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
        XClipboard* clip = XGuiApplication_clipboard();
        if (clip) {
            XString* s = XString_create_utf8(t);
            if (s) {
                XClipboard_setText(clip, s, (XClipboardMode)mode);
                XString_delete_base((XClass*)s);
                written = true;
            }
        }
#endif
        if (!written) {
            /* 平台剪贴板不可用：回退共享层（进程内承载，验收/离线同源）。 */
            XTextClipboard_setText(t);
            written = true;
        }
    }
    if (t) XFree_System(t);
}

void XLineControl_paste(XLineControl* self, int mode)
{
    char* clipText = NULL;
    bool systemOnly = false;
    if (!self) return;
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        XClipboard* clip = XGuiApplication_clipboard();
        /* 对标 Qt 中键语义（QWidgetLineControl::paste(Selection)）：
           Selection 模式且系统后端可用并支持选择区（supportsSelection）
           时仅取系统 PRIMARY，空则不动作——不回退共享层，避免 X11 下
           中键在 PRIMARY 为空时意外粘出 CLIPBOARD 旧内容。Clipboard
           模式维持"系统剪贴板→共享层"回退链零回归；无系统后端（嵌入式
           裁剪）时 systemOnly 恒为 false，Selection 仍走共享层回退。 */
        systemOnly = (clip != NULL &&
                      mode == (int)XClipboardMode_Selection &&
                      XClipboard_supportsSelection(clip));
        if (clip) {
            XString* s = XClipboard_text(clip, (XClipboardMode)mode);
            if (s) {
                /* 对标 Qt：按 UTF-8 字节数截取（此前用 size_base=字符数
                 * 截取，中文 3 字节/字符被截断为 2 字节→粘出空白）。 */
                size_t blen = XString_toUtf8_length(s);
                if (blen > 0)
                    clipText = xlc_strdupRange(
                        XString_toUtf8(s), (int)blen);
                XString_delete_base((XClass*)s);
            }
        }
    }
#else
    (void)mode;
#endif
    if (!clipText && !systemOnly) {
        /* 平台剪贴板不可用：回退共享层（进程内承载）。 */
        const char* shared = XTextClipboard_getText();
        if (shared && shared[0])
            clipText = xlc_strdupRange(shared, (int)XStrlen(shared));
    }
    /* 对标 Qt：剪贴板无文本时不做任何事——此前在带选区时仍走
     * "删除选区+插入空"，Ctrl+V 表现为把选区删成空白。 */
    if (!clipText || clipText[0] == '\0') {
        if (clipText) XFree_System(clipText);
        return;
    }
    xlc_separate(self); /* 独立撤销组（对标 Qt）。 */
    XLineControl_insert(self, clipText);
    xlc_separate(self);
    XFree_System(clipText);
}

/* ==================== 补全器联动（对标 completer/complete/advanceToEnabledItem） ==================== */

/**
 * @brief 迭代到下一个可用候选（对标 advanceToEnabledItem）。
 * @note  XCompleter 无候选可用位（Qt::ItemIsEnabled）承载，所有候选
 *        视为可用（@note）；环绕与恢复语义与 Qt 一致。
 */
static bool xlc_advanceToEnabledItem(XLineControl* self, int dir)
{
    int start;
    int i;
    if (!self || !self->m_completer) return false;
    start = XCompleter_currentRow(self->m_completer);
    if (start == -1) return false;
    i = start + dir;
    if (dir == 0) dir = 1;
    do {
        if (!XCompleter_setCurrentRow(self->m_completer, i)) {
            if (!XCompleter_wrapAround(self->m_completer)) break;
            i = i > 0 ? 0 : XCompleter_completionCount(self->m_completer) - 1;
        } else {
            /* 无可用位查询：候选恒可用（@note）。 */
            return true;
        }
    } while (i != start);
    XCompleter_setCurrentRow(self->m_completer, start); /* 恢复。 */
    return false;
}

void XLineControl_complete(XLineControl* self, int key)
{
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
    const char* text;
    if (!self || !self->m_completer || XLineControl_isReadOnly(self)
        || self->m_echoMode != (uint32_t)XLineControlEchoMode_Normal)
        return;
    text = XLineControl_text(self);
    if (XCompleter_completionMode(self->m_completer)
        == XCompleterCompletionMode_InlineCompletion) {
        if (key == XKey_Backspace) return;
        {
            int n = 0;
            if (key == XKey_Up || key == XKey_Down) {
                char* after = XLineControl_textAfterSelection(self);
                if (after) {
                    XFree_System(after);
                    return;
                }
                {
                    char* prefix = XLineControl_hasSelectedText(self)
                                       ? XLineControl_textBeforeSelection(self)
                                       : xlc_strdupRange(text,
                                                         (int)XStrlen(text));
                    XString* current = XCompleter_currentCompletion(
                        self->m_completer);
                    XString* prefix0 = XCompleter_completionPrefix(
                        self->m_completer);
                    bool same = current && prefix
                                && XStrcmp(XString_toUtf8(current),
                                           prefix) == 0;
                    bool prefixSame = prefix0 && prefix
                                      && XStrcmp(XString_toUtf8(prefix0),
                                                 prefix) == 0;
                    if (prefix) {
                        if (!same || !prefixSame)
                            XCompleter_setCompletionPrefix_2(
                                self->m_completer, prefix);
                        else
                            n = (key == XKey_Up) ? -1 : 1;
                        XFree_System(prefix);
                    }
                    if (current) XString_delete_base((XClass*)current);
                    if (prefix0) XString_delete_base((XClass*)prefix0);
                }
            } else {
                XCompleter_setCompletionPrefix_2(self->m_completer, text);
            }
            if (!xlc_advanceToEnabledItem(self, n)) return;
        }
    } else {
        if (text[0] == '\0') {
            /* 对标 Qt QWidgetLineControl::complete 空文本分支
             * （qwidgetlinecontrol.cpp:1463-1469：`if (text.isEmpty())
             * { if (popup) popup->hide(); return; }`）：文本清空时若
             * 弹层开着同步收层，不留孤儿浮层（第九轮活体 ① 修复面④：
             * 此前直接 return，清空后弹层残留）。 */
            XCompleter_hidePopup(self->m_completer);
            return;
        }
        XCompleter_setCompletionPrefix_2(self->m_completer, text);
    }
    XCompleter_complete(self->m_completer);
#else
    (void)self;
    (void)key;
#endif /* XLC_COMPLETER_ON */
}

void* XLineControl_completer(const XLineControl* self)
{ return self ? (void*)self->m_completer : NULL; }

void XLineControl_setCompleter(XLineControl* self, void* completer)
{ if (self) self->m_completer = (XCompleter*)completer; }

/* ==================== 键盘处理（对标 processKeyEvent/processShortcutOverrideEvent） ==================== */

/* XEvent.h 未收录的 Qt::Key 保留区键值（数值与 Qt 一致）。 */
#define XLC_KEY_DIRECTION_L 0x01000056 /**< 方向键 L（切 LTR）。 */
#define XLC_KEY_DIRECTION_R 0x01000057 /**< 方向键 R（切 RTL）。 */
#define XLC_KEY_SELECT      0x01000060 /**< 键盘导航确认键。 */
#define XLC_KEY_BACK        0x01000061 /**< 键盘导航返回键。 */

/** @brief 精确匹配键值 + 修饰键组合（对标 QKeySequence 匹配的 C 口径）。 */
static bool xlc_matchKey(const XKeyEvent* ke, int key,
                         XKeyboardModifiers mods)
{
    return ke && ke->m_key == key && ke->m_modifiers == mods;
}

/** @brief Ctrl+字母匹配（字母大小写不敏感）。
 *  @details 平台层对字母键交付小写 ASCII（XK_c→'c'），此前仅匹配
 *           大写 'C'/'V'/'X'/'Z'/'Y'/'A'/'K'，Ctrl 组合快捷键全部
 *           落空（多行控制器大小写双写故正常，14.125 用户报告）。 */
static bool xlc_matchCtrlLetter(const XKeyEvent* ke, char letter,
                                XKeyboardModifiers mods)
{
    int key = ke ? ke->m_key : 0;
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    return key == (int)letter && ke->m_modifiers == mods;
}

/** @brief 组合修饰键。 */
#define XLC_MODS(...) ((XKeyboardModifiers)(__VA_ARGS__))

/**
 * @brief 由键值推导输入文本（平台契约：可打印字符即 ASCII 码位）。
 * @return 有可打印文本返回其字节长（1），否则 0；out 写入单字节。
 * @details 拉丁字母大小写按 Shift 修饰位派生（问题 #32 收官，对标 Qt：
 *          键事件文本随 Shift 并行于键值——qxcbkeyboard.cpp
 *          handleKeyEvent:865-866 sym 与 lookupString 同源产出、字母
 *          键值恒 Key_T 大写口径（:872 keysymToQtKey），大小写由
 *          text() 表达；消费侧 qwidgetlinecontrol.cpp:1921
 *          insert(event->text())）。平台层字母键值经大写归一恒
 *          [0x41,0x5A]（XPlatformNativeWindow_posix.c 大写归一），此处
 *          兼容小写键值（其他交付方直灌）。CapsLock 无修饰位承载
 *          （XKeyboardModifiers 契约冻结，XEvent.h:129-137），锁存态
 *          字母由平台层 LockMask 守卫留在 IME 提交通道，不入本函数。
 */
static int xlc_keyToText(const XKeyEvent* ke, char* out)
{
    int key;
    if (!ke || !out) return 0;
    key = ke->m_key;
    if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
    if (key >= 'A' && key <= 'Z') {
        out[0] = (ke->m_modifiers & XKeyboardModifier_ShiftModifier)
                     ? (char)key
                     : (char)(key + ('a' - 'A'));
        out[1] = '\0';
        return 1;
    }
    if (key >= 0x20 && key <= 0x7E) {
        out[0] = (char)key;
        out[1] = '\0';
        return 1;
    }
    out[0] = '\0';
    return 0;
}

/**
 * @brief 判定是否为可接纳输入键（对标 QInputControl::isAcceptableInput）。
 * @details XGui 平台契约下 text 由键值推导：空文本（功能键/组合键）
 *          拒绝；Ctrl / Ctrl+Shift 组合拒绝（QTBUG-35734 语义）；其余
 *          可打印键接纳。Other_Format/PrivateUse/代理对分支在"键值即
 *          ASCII 码位"契约下不可达（多字节/组合文本经 IME 事件进入），
 *        保留 @note。
 */
static bool xlc_isAcceptableInput(const XLineControl* self,
                                  const XKeyEvent* event)
{
    char text[2];
    XKeyboardModifiers mods;
    (void)self;
    if (!event) return false;
    if (xlc_keyToText(event, text) == 0) return false;
    mods = event->m_modifiers;
    if (mods == XKeyboardModifier_ControlModifier
        || mods == (XKeyboardModifier_ShiftModifier
                    | XKeyboardModifier_ControlModifier))
        return false;
    return xlc_isPrintCp((uint32_t)(uint8_t)text[0]);
}

/** @brief 键盘导航开关内部读取。 */
static bool xlc_keypadNavigationEnabled(const XLineControl* self)
{ return self ? self->m_keypadNavigationEnabled : false; }

void XLineControl_processKeyEvent(XLineControl* self, XKeyEvent* event)
{
    bool inlineCompletionAccepted = false;
    bool unknown = false;
    bool visual;
    int key;
    XKeyboardModifiers mods;
    char keyText[2];

    if (!self || !event) return;
    key = event->m_key;
    mods = event->m_modifiers;
    visual = self->m_cursorMoveStyle
             == (int)XLineControlCursorMoveStyle_VisualMoveStyle;
    (void)visual;
    (void)keyText;

#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
    if (self->m_completer) {
        XCompleterCompletionMode completionMode =
            XCompleter_completionMode(self->m_completer);
        XWidget* popup = XCompleter_popup(self->m_completer);
        if ((completionMode == XCompleterCompletionMode_PopupCompletion
             || completionMode == XCompleterCompletionMode_UnfilteredPopupCompletion)
            && popup && XWidget_isVisible(popup)) {
            /* 弹窗下的按键转发给补全器：Escape 隐藏弹层（对标
               QCompleter popup 的 Esc 隐藏语义）并忽略本按键。 */
            if (key == XKey_Escape) {
                XCompleter_hidePopup(self->m_completer);
                XEvent_ignore((XEvent*)event);
                return;
            }
            /* 对标 QCompleter popup 键盘激活（Qt 由弹层事件过滤承接
             * Return 并以 activated(QString)→QLineEdit::setText 回填）：
             * 弹层可见时 Return/Enter 采纳当前候选并收层，随后落入
             * 下方公共 Return 路径发射 accepted/editingFinished。 */
            if (key == XKey_Return || key == XKey_Enter) {
                XString* activated =
                    XCompleter_currentCompletion(self->m_completer);
                if (activated) {
                    XLineControl_setText(self,
                                         XString_toUtf8(activated));
                    XString_delete_base((XClass*)activated);
                }
                XCompleter_hidePopup(self->m_completer);
            }
            /* 对标 QCompleter popup 键盘导航：弹层可见时 Up/Down 环绕
               移动当前候选，并把当前候选文本回填编辑框（Qt 语义：
               popup 高亮 + 行编辑同步为当前候选）。回填走整串替换，
               不触发前缀重算/弹层重建（导航期间候选列表保持不变）；
               textChanged 触发的 demo 状态行更新无害。 */
            if (key == XKey_Up || key == XKey_Down) {
                int cur = XCompleter_currentRow(self->m_completer);
                int count = XCompleter_completionCount(self->m_completer);
                int target = cur + (key == XKey_Down ? 1 : -1);
                XString* text;
                if (count > 0) {
                    if (target < 0) target = count - 1;
                    if (target >= count) target = 0;
                    if (XCompleter_setCurrentRow(self->m_completer, target)) {
                        text = XCompleter_currentCompletion(self->m_completer);
                        if (text)
                        {
                            XLineControl_setText(self,
                                                 XString_toUtf8(text));
                            XString_delete_base((XClass*)text);
                        }
                    }
                }
                XEvent_accept((XEvent*)event);
                return;
            }
        } else if (completionMode == XCompleterCompletionMode_InlineCompletion) {
            if (key == XKey_Enter || key == XKey_Return || key == XKey_F4) {
                XString* current = XCompleter_currentCompletion(self->m_completer);
                XString* prefix = XCompleter_completionPrefix(self->m_completer);
                char* after = XLineControl_textAfterSelection(self);
                if (current && prefix
                    && XString_size_base((const XContainer*)current) > 0
                    && XLineControl_hasSelectedText(self)
                    && XString_size_base((const XContainer*)prefix) > 0
                    && after == NULL) {
                    XLineControl_setText(self, XString_toUtf8(current));
                    inlineCompletionAccepted = true;
                }
                if (current) XString_delete_base((XClass*)current);
                if (prefix) XString_delete_base((XClass*)prefix);
                if (after) XFree_System(after);
            }
        }
    }
#endif /* XLC_COMPLETER_ON */

    if (key == XKey_Return || key == XKey_Enter) {
        if (XLineControl_hasAcceptableInput(self) || XLineControl_fixup(self)) {
#if XGUIAPPLICATION_ON
            XInputMethod* im = XGuiApplication_inputMethod();
            if (im) {
                XInputMethod_commit(im);
                /* XGui 单行控件无 ImhMultiLine 提示承载：恒隐藏（Qt 默认）。 */
                XInputMethod_hide(im);
            }
#endif
            xlc_emitVoid(self, (size_t)XLineControl_accepted_signal(self));
            xlc_emitVoid(self,
                         (size_t)XLineControl_editingFinished_signal(self));
        }
        if (inlineCompletionAccepted) XEvent_accept((XEvent*)event);
        else XEvent_ignore((XEvent*)event);
        return;
    }

    if (self->m_echoMode == (uint32_t)XLineControlEchoMode_PasswordEchoOnEdit
        && !XLineControl_passwordEchoEditing(self)
        && !XLineControl_isReadOnly(self)
        && xlc_keyToText(event, keyText) > 0
        && !(mods & XKeyboardModifier_ControlModifier)) {
        /* PasswordEchoOnEdit 首个输入键：清空并切入编辑回显。 */
        XLineControl_updatePasswordEchoEditing(self, true);
        XLineControl_clear(self);
    }

    /* ---- 快捷键族（QKeySequence 标准绑定映射，见头文件 @details） ---- */
    if (xlc_matchCtrlLetter(event, 'Z', XLC_MODS(XKeyboardModifier_ControlModifier))) {
        if (!XLineControl_isReadOnly(self)) XLineControl_undo(self);
    }
    else if (xlc_matchCtrlLetter(event, 'Z', XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))
             || xlc_matchCtrlLetter(event, 'Y', XLC_MODS(XKeyboardModifier_ControlModifier))) {
        if (!XLineControl_isReadOnly(self)) XLineControl_redo(self);
    }
    else if (xlc_matchCtrlLetter(event, 'A', XLC_MODS(XKeyboardModifier_ControlModifier))) {
        XLineControl_selectAll(self);
    }
    else if (xlc_matchCtrlLetter(event, 'C', XLC_MODS(XKeyboardModifier_ControlModifier))
             || xlc_matchKey(event, XKey_Insert, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        XLineControl_copy(self, (int)XClipboardMode_Clipboard);
    }
    else if (xlc_matchCtrlLetter(event, 'V', XLC_MODS(XKeyboardModifier_ControlModifier))
             || xlc_matchKey(event, XKey_Insert, XLC_MODS(XKeyboardModifier_ShiftModifier))) {
        if (!XLineControl_isReadOnly(self)) {
            int mode = (int)XClipboardMode_Clipboard;
            if (self->m_keyboardScheme == (int)XLineControlKeyboardScheme_X11
                && mods == XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier)
                && key == XKey_Insert)
                mode = (int)XClipboardMode_Selection;
            XLineControl_paste(self, mode);
        }
    }
    else if (xlc_matchCtrlLetter(event, 'X', XLC_MODS(XKeyboardModifier_ControlModifier))
             || xlc_matchKey(event, XKey_Delete, XLC_MODS(XKeyboardModifier_ShiftModifier))) {
        if (!XLineControl_isReadOnly(self) && XLineControl_hasSelectedText(self)) {
            XLineControl_copy(self, (int)XClipboardMode_Clipboard);
            XLineControl_del(self);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
            /* 剪切清空路径同款收层（全选剪切→complete() 空文本分支
               隐藏弹层；非清空剪切按新前缀重算，与 Backspace 一致）。 */
            XLineControl_complete(self, key);
#endif
        }
    }
    else if (xlc_matchCtrlLetter(event, 'K', XLC_MODS(XKeyboardModifier_ControlModifier))) {
        /* DeleteEndOfLine：选中光标到行尾并删除。 */
        if (!XLineControl_isReadOnly(self)) {
            XLineControl_setSelection(self, XLineControl_cursor(self),
                                      XLineControl_textEnd(self)
                                          - XLineControl_cursor(self));
            XLineControl_copy(self, (int)XClipboardMode_Clipboard);
            XLineControl_del(self);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
            /* 删至行尾同属清空路径族：光标在行首时整串清空→收层。 */
            XLineControl_complete(self, key);
#endif
        }
    }
    else if (xlc_matchKey(event, XKey_Home, XKeyboardModifier_NoModifier)) {
        XLineControl_home(self, false);
    }
    else if (xlc_matchKey(event, XKey_End, XKeyboardModifier_NoModifier)) {
        XLineControl_end(self, false);
    }
    else if (xlc_matchKey(event, XKey_Home, XLC_MODS(XKeyboardModifier_ShiftModifier))) {
        XLineControl_home(self, true);
    }
    else if (xlc_matchKey(event, XKey_End, XLC_MODS(XKeyboardModifier_ShiftModifier))) {
        XLineControl_end(self, true);
    }
    else if (xlc_matchKey(event, XKey_Right, XKeyboardModifier_NoModifier)) {
        if (XLineControl_hasSelectedText(self)
            && self->m_keyboardScheme != (int)XLineControlKeyboardScheme_Windows)
            XLineControl_moveCursor(self, XLineControl_selectionEnd(self), false);
        else
            XLineControl_cursorForward(self, false,
                                       (XLineControl_layoutDirection(self)
                                        == (int)XLineControlLayoutDirection_LeftToRight) ? 1 : -1);
    }
    else if (xlc_matchKey(event, XKey_Right, XLC_MODS(XKeyboardModifier_ShiftModifier))) {
        XLineControl_cursorForward(self, true,
                                   (XLineControl_layoutDirection(self)
                                    == (int)XLineControlLayoutDirection_LeftToRight) ? 1 : -1);
    }
    else if (xlc_matchKey(event, XKey_Left, XKeyboardModifier_NoModifier)) {
        if (XLineControl_hasSelectedText(self)
            && self->m_keyboardScheme != (int)XLineControlKeyboardScheme_Windows)
            XLineControl_moveCursor(self, XLineControl_selectionStart(self), false);
        else
            XLineControl_cursorForward(self, false,
                                       (XLineControl_layoutDirection(self)
                                        == (int)XLineControlLayoutDirection_LeftToRight) ? -1 : 1);
    }
    else if (xlc_matchKey(event, XKey_Left, XLC_MODS(XKeyboardModifier_ShiftModifier))) {
        XLineControl_cursorForward(self, true,
                                   (XLineControl_layoutDirection(self)
                                    == (int)XLineControlLayoutDirection_LeftToRight) ? -1 : 1);
    }
    else if (xlc_matchKey(event, XKey_Right, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal)
            XLineControl_cursorWordForward(self, false);
        else
            XLineControl_end(self, false);
    }
    else if (xlc_matchKey(event, XKey_Left, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal)
            XLineControl_cursorWordBackward(self, false);
        else if (!XLineControl_isReadOnly(self))
            XLineControl_home(self, false);
    }
    else if (xlc_matchKey(event, XKey_Right, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))) {
        if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal)
            XLineControl_cursorWordForward(self, true);
        else
            XLineControl_end(self, true);
    }
    else if (xlc_matchKey(event, XKey_Left, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))) {
        if (self->m_echoMode == (uint32_t)XLineControlEchoMode_Normal)
            XLineControl_cursorWordBackward(self, true);
        else
            XLineControl_home(self, true);
    }
    else if (xlc_matchKey(event, XKey_Delete, XKeyboardModifier_NoModifier)) {
        if (!XLineControl_isReadOnly(self)) {
            XLineControl_del(self);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
            /* 对标 Backspace 同款"变更后 complete"语义（Backspace 分支
               backspace(); complete(Key_Backspace)）：Delete 与剪切等
               清空路径此前不调 complete，Ctrl+A+Delete 清空后补全弹层
               残留（complete() 空文本分支即 Qt qwidgetlinecontrol.cpp
               :1464-1469 的 popup->hide()，第九轮 W2 修复分支仅在
               complete() 内可达——Delete 路径补调后收层语义到达）。 */
            XLineControl_complete(self, XKey_Delete);
#endif
        }
    }
    else if (xlc_matchKey(event, XKey_Delete, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        /* DeleteEndOfWord。 */
        if (!XLineControl_isReadOnly(self)) {
            if (!XLineControl_hasSelectedText(self))
                XLineControl_cursorWordForward(self, true);
            if (XLineControl_hasSelectedText(self))
                XLineControl_del(self);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
            /* 同 Delete：词删除清空路径统一收层（清空→弹层不残留）。 */
            XLineControl_complete(self, XKey_Delete);
#endif
        }
    }
    else if (xlc_matchKey(event, XKey_Backspace, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        /* DeleteStartOfWord。 */
        if (!XLineControl_isReadOnly(self)) {
            if (!XLineControl_hasSelectedText(self))
                XLineControl_cursorWordBackward(self, true);
            if (XLineControl_hasSelectedText(self))
                XLineControl_del(self);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
            /* 同 Backspace：词删除清空路径统一收层；Inline 模式下
               complete(Key_Backspace) 首分支直接返回（对标 Qt）。 */
            XLineControl_complete(self, XKey_Backspace);
#endif
        }
    }
    else if (xlc_matchKey(event, XKey_Home, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        XLineControl_home(self, false); /* MoveToStartOfDocument。 */
    }
    else if (xlc_matchKey(event, XKey_End, XLC_MODS(XKeyboardModifier_ControlModifier))) {
        XLineControl_end(self, false); /* MoveToEndOfDocument。 */
    }
    else if (xlc_matchKey(event, XKey_Home, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))) {
        XLineControl_home(self, true); /* SelectStartOfDocument。 */
    }
    else if (xlc_matchKey(event, XKey_End, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))) {
        XLineControl_end(self, true); /* SelectEndOfDocument。 */
    }
    else {
        bool handled = false;
        if (self->m_keyboardScheme == (int)XLineControlKeyboardScheme_Mac
            && (key == XKey_Up || key == XKey_Down)) {
            /* Mac 方案：上下键映射行首/行尾。 */
            XKeyboardModifiers myMods =
                (XKeyboardModifiers)(mods & ~XKeyboardModifier_KeypadModifier);
            if (myMods & XKeyboardModifier_ShiftModifier) {
                if (myMods == XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier)
                    || myMods == XLC_MODS(XKeyboardModifier_AltModifier | XKeyboardModifier_ShiftModifier)
                    || myMods == XLC_MODS(XKeyboardModifier_ShiftModifier)) {
                    if (key == XKey_Up) XLineControl_home(self, true);
                    else XLineControl_end(self, true);
                }
            } else {
                if (myMods == XLC_MODS(XKeyboardModifier_ControlModifier)
                    || myMods == XLC_MODS(XKeyboardModifier_AltModifier)
                    || myMods == XKeyboardModifier_NoModifier) {
                    if (key == XKey_Up) XLineControl_home(self, false);
                    else XLineControl_end(self, false);
                }
            }
            handled = true;
        }
        if (mods & XKeyboardModifier_ControlModifier) {
            switch (key) {
            case XKey_Backspace:
                if (!XLineControl_isReadOnly(self)) {
                    XLineControl_cursorWordBackward(self, true);
                    XLineControl_del(self);
                }
                break;
            case XKey_Up:
            case XKey_Down:
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
                XLineControl_complete(self, key);
#endif
                break;
            default:
                if (!handled) unknown = true;
                break;
            }
        } else {
            switch (key) {
            case XKey_Backspace:
                if (!XLineControl_isReadOnly(self)) {
                    XLineControl_backspace(self);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
                    XLineControl_complete(self, XKey_Backspace);
#endif
                }
                break;
            case XLC_KEY_BACK:
                if (xlc_keypadNavigationEnabled(self)
                    && !XKeyEvent_autoRepeat(event)
                    && !XLineControl_isReadOnly(self)) {
                    if (XStrlen(XLineControl_text(self)) == 0) {
                        XLineControl_setText(self,
                                             XLineControl_cancelText(self));
                        if (XLineControl_passwordEchoEditing(self))
                            XLineControl_updatePasswordEchoEditing(self, false);
                        xlc_emitBool(self,
                                     (size_t)XLineControl_editFocusChange_signal(
                                         self, false),
                                     false);
                    } else if (self->m_deleteAllTimer == XTIMER_INVALID_ID) {
                        self->m_deleteAllTimer = XObject_startTimer_ms(
                            (XObject*)self, XLC_DELETE_ALL_DELAY_MS,
                            XTimerType_CoarseTimer);
                    }
                } else {
                    unknown = true;
                }
                break;
            default:
                if (!handled) unknown = true;
                break;
            }
        }
    }

    if (key == XLC_KEY_DIRECTION_L || key == XLC_KEY_DIRECTION_R) {
        XLineControl_setLayoutDirection(
            self,
            key == XLC_KEY_DIRECTION_L
                ? (int)XLineControlLayoutDirection_LeftToRight
                : (int)XLineControlLayoutDirection_RightToLeft);
        unknown = false;
    }

    if (unknown && !XLineControl_isReadOnly(self)
        && xlc_isAcceptableInput(self, event)) {
        char text[2];
        (void)xlc_keyToText(event, text);
        XLineControl_insert(self, text);
#if defined(XLC_COMPLETER_ON) && XLC_COMPLETER_ON
        XLineControl_complete(self, key);
#endif
        XEvent_accept((XEvent*)event);
        return;
    }

    if (unknown) {
        XEvent_ignore((XEvent*)event);
    } else {
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
        {
            XClipboard* clip = XGuiApplication_clipboard();
            if (clip && XClipboard_supportsSelection(clip))
                XLineControl_copy(self, (int)XClipboardMode_Selection);
        }
#endif
        XEvent_accept((XEvent*)event);
    }
}

void XLineControl_processShortcutOverrideEvent(XLineControl* self, XKeyEvent* ke)
{
    XKeyboardModifiers mods;
    if (!self || !ke) return;
    mods = ke->m_modifiers;
    /* 无编辑副作用的编辑键族恒 accept（对标 Qt 首分支）。 */
    if (xlc_matchKey(ke, 'C', XLC_MODS(XKeyboardModifier_ControlModifier))
        || xlc_matchKey(ke, XKey_Insert, XLC_MODS(XKeyboardModifier_ControlModifier))
        || xlc_matchKey(ke, XKey_Right, XLC_MODS(XKeyboardModifier_ControlModifier))
        || xlc_matchKey(ke, XKey_Left, XLC_MODS(XKeyboardModifier_ControlModifier))
        || xlc_matchKey(ke, XKey_Home, XKeyboardModifier_NoModifier)
        || xlc_matchKey(ke, XKey_End, XKeyboardModifier_NoModifier)
        || xlc_matchKey(ke, XKey_Home, XLC_MODS(XKeyboardModifier_ControlModifier))
        || xlc_matchKey(ke, XKey_End, XLC_MODS(XKeyboardModifier_ControlModifier))
        || xlc_matchKey(ke, XKey_Right, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))
        || xlc_matchKey(ke, XKey_Left, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))
        || xlc_matchKey(ke, XKey_Home, XLC_MODS(XKeyboardModifier_ShiftModifier))
        || xlc_matchKey(ke, XKey_End, XLC_MODS(XKeyboardModifier_ShiftModifier))
        || xlc_matchKey(ke, XKey_Home, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))
        || xlc_matchKey(ke, XKey_End, XLC_MODS(XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier))
        || xlc_matchKey(ke, 'A', XLC_MODS(XKeyboardModifier_ControlModifier))) {
        XEvent_accept((XEvent*)ke);
    } else if (xlc_matchKey(ke, 'V', XLC_MODS(XKeyboardModifier_ControlModifier))
               || xlc_matchKey(ke, XKey_Insert, XLC_MODS(XKeyboardModifier_ShiftModifier))
               || xlc_matchKey(ke, 'X', XLC_MODS(XKeyboardModifier_ControlModifier))
               || xlc_matchKey(ke, XKey_Delete, XLC_MODS(XKeyboardModifier_ShiftModifier))
               || xlc_matchKey(ke, 'Z', XLC_MODS(XKeyboardModifier_ControlModifier))
               || xlc_matchKey(ke, 'Y', XLC_MODS(XKeyboardModifier_ControlModifier))
               || xlc_matchKey(ke, 'K', XLC_MODS(XKeyboardModifier_ControlModifier))) {
        if (!XLineControl_isReadOnly(self)) XEvent_accept((XEvent*)ke);
    } else if (mods == XKeyboardModifier_NoModifier
               || mods == XKeyboardModifier_ShiftModifier
               || mods == XKeyboardModifier_KeypadModifier) {
        if (ke->m_key < XKey_Escape) {
            if (!XLineControl_isReadOnly(self)) XEvent_accept((XEvent*)ke);
        } else {
            switch (ke->m_key) {
            case XKey_Delete:
            case XKey_Backspace:
                if (!XLineControl_isReadOnly(self)) XEvent_accept((XEvent*)ke);
                break;
            case XKey_Home:
            case XKey_End:
            case XKey_Left:
            case XKey_Right:
                XEvent_accept((XEvent*)ke);
                break;
            default:
                break;
            }
        }
    }
}

int XLineControl_keyboardScheme(const XLineControl* self)
{ return self ? self->m_keyboardScheme : 0; }

void XLineControl_setKeyboardScheme(XLineControl* self, int scheme)
{ if (self) self->m_keyboardScheme = scheme; }

bool XLineControl_keypadNavigationEnabled(const XLineControl* self)
{ return xlc_keypadNavigationEnabled(self); }

void XLineControl_setKeypadNavigationEnabled(XLineControl* self, bool enable)
{ if (self) self->m_keypadNavigationEnabled = enable; }

/* ==================== 无障碍（对标 setAccessibleObject/accessibleObject） ==================== */

void XLineControl_setAccessibleObject(XLineControl* self, XObject* object)
{
    if (!self) return;
    self->m_accessibleObject = object; /* 对标 Q_ASSERT(object)。 */
}

XObject* XLineControl_accessibleObject(const XLineControl* self)
{
    if (!self) return NULL;
    if (self->m_accessibleObject) return self->m_accessibleObject;
    return ((XObject*)self)->m_parent; /* 对标回退 parent()。 */
}

/* ==================== 绘制数据（对标 setFont/layoutDirection/palette/geometry/draw） ==================== */

void XLineControl_setFont(XLineControl* self, const XFont* font)
{
    if (!self) return;
    if (self->m_font) { XFont_delete_base((XClass*)self->m_font); self->m_font = NULL; }
    if (font) {
        self->m_font = (XFont*)XMalloc_System(sizeof(XFont));
        if (self->m_font) {
            XMemset(self->m_font, 0, sizeof(XFont));
            XCopy(self->m_font, (const XClass*)font); /* 深拷贝（对标 QFont 值语义）。 */
            /* 壳为堆分配：登记堆所有权位（XCopy 不继承；缺位时
               delete_base 只 deinit 不 free，逐替换泄漏 880B 壳，
               §8.0g7 ASan 定位）。 */
            Set_Class_IsHeap(self->m_font, true);
        }
    }
    /* 字体变更只需重排（updateDisplayText 内恒经 redoTextLayout），
     * 显示文本本身不变则不发 displayTextChanged——壳在 paintEvent/命中
     * 前都会 syncControlFont，若此处强制发射，绘制内 XWidget_update 会
     * 再投递 PAINT，形成每帧自激的重绘风暴（14.123 ①）。 */
    xlc_updateDisplayText(self, false);
}

int XLineControl_layoutDirection(const XLineControl* self)
{
    if (!self) return (int)XLineControlLayoutDirection_LeftToRight;
    if (self->m_layoutDirection == (int)XLineControlLayoutDirection_Auto
        && self->m_textLen > 0) {
        /* Auto：按首字符判向（对标 m_text.isRightToLeft() 的简化口径：
         * 阿拉伯/希伯来区段视为 RTL，其余 LTR）。 */
        uint32_t cp = xlc_decodeAt(self->m_text, self->m_textLen, 0, NULL);
        if ((cp >= 0x0590u && cp <= 0x08FFu) || (cp >= 0xFB1Du && cp <= 0xFDFFu)
            || (cp >= 0xFE70u && cp <= 0xFEFCu))
            return (int)XLineControlLayoutDirection_RightToLeft;
        return (int)XLineControlLayoutDirection_LeftToRight;
    }
    return self->m_layoutDirection;
}

void XLineControl_setLayoutDirection(XLineControl* self, int direction)
{
    if (!self) return;
    if (direction != self->m_layoutDirection) {
        self->m_layoutDirection = direction;
        xlc_updateDisplayText(self, false);
    }
}

XLineControlPalette XLineControl_palette(const XLineControl* self)
{
    XLineControlPalette p;
    if (!self) {
        p.m_highlight = 0; p.m_highlightedText = 0;
        p.m_text = 0; p.m_window = 0;
        return p;
    }
    return self->m_palette;
}

void XLineControl_setPalette(XLineControl* self, const XLineControlPalette* palette)
{
    if (!self) return;
    if (palette) self->m_palette = *palette;
    else {
        self->m_palette.m_highlight = XLC_COLOR_HIGHLIGHT;
        self->m_palette.m_highlightedText = XLC_COLOR_HIGHLIGHTEDTEXT;
        self->m_palette.m_text = XLC_COLOR_TEXT;
        self->m_palette.m_window = XLC_COLOR_WINDOW;
    }
}

int XLineControl_ascent(const XLineControl* self)
{ return self ? self->m_layoutAscent : 0; }

int XLineControl_width(const XLineControl* self)
{ return self ? self->m_layoutLineWidth + 1 : 0; }

int XLineControl_height(const XLineControl* self)
{ return self ? self->m_layoutLineHeight + 1 : 0; }

int XLineControl_naturalTextWidth(const XLineControl* self)
{ return self ? self->m_layoutLineWidth : 0; }

const XLineControlTextLayout* XLineControl_textLayout(const XLineControl* self)
{
    /* 只读视图由对象内存尾部逻辑承载：这里以静态映射避免额外分配；
     * 视图随对象状态刷新（借用指针，生存期同 self）。 */
    static XLineControlTextLayout view; /* 单线程 GUI 约定下共享只读视图。 */
    if (!self) return NULL;
    view.m_text = self->m_displayText ? self->m_displayText : "";
    view.m_preeditText = self->m_preeditText ? self->m_preeditText : "";
    view.m_layoutText = self->m_layoutText ? self->m_layoutText : "";
    view.m_preeditPosition = self->m_preeditLayoutPos;
    view.m_preeditCursor = self->m_preeditCursor;
    view.m_hideCursor = self->m_hideCursor != 0u;
    view.m_cursorMoveStyle = self->m_cursorMoveStyle;
    view.m_font = self->m_font;
    view.m_ascent = self->m_layoutAscent;
    view.m_lineWidth = self->m_layoutLineWidth;
    view.m_lineHeight = self->m_layoutLineHeight;
    return &view;
}

/**
 * @brief 绘制选区/掩码反选背景（对标 draw 的 DrawSelections 分支）。
 * @return true 表示存在选区背景（影响文本前景配色）。
 */
static bool xlc_drawSelections(XLineControl* self, XPainter* painter,
                               const XPoint* offset, const XRect* clip,
                               bool cursorPhase)
{
    XRect selRect;
    int x0, x1;
    if (self->m_selstart < self->m_selend) {
        x0 = xlc_layoutCursorToX(self,
                                 xlc_mapTextToLayout(self, self->m_selstart));
        x1 = xlc_layoutCursorToX(self,
                                 xlc_mapTextToLayout(self, self->m_selend));
        selRect.x = offset->x + x0;
        selRect.y = offset->y;
        selRect.width = x1 - x0;
        selRect.height = self->m_layoutLineHeight;
        if (!clip || (selRect.x < clip->x + clip->width
                      && selRect.x + selRect.width > clip->x))
            XPainter_fillRect(painter, &selRect, self->m_palette.m_highlight);
        return true;
    }
    if (cursorPhase && self->m_maskData) {
        /* 掩码反选一格：背景 Text、前景 Window（光标在可编辑位上）。
         * 门禁（R-32 内层，Qt // mask selection 分支语义）：此格为掩码
         * 行编辑专属——仅当 m_maskData 存在（NULL=无掩码）时绘制；
         * 无掩码时细光标归壳层 Cursor 旗标承担，两者互斥。 */
        int cursorV = xlc_mapTextToLayout(self, self->m_cursor);
        int cursorX = xlc_layoutCursorToX(self, cursorV);
        int seq = xlc_seqLenAt(self->m_layoutText, self->m_layoutLen,
                               cursorV);
        if (seq <= 0) seq = 1;
        selRect.x = offset->x + cursorX;
        selRect.y = offset->y;
        /* 根因修复（R-31）：段宽必须走字节口径（与 XLineControl_draw
         * Text 分支的 blinkStart/nextBoundary 同口径）——cursorX 是
         * xlc_layoutCursorToX 返回的像素 X，旧代码把它当字节偏移喂给
         * seqLenAt/textWidthRange：等宽字库下光标过 len/8 处即读出
         * layoutText 越界字节并得出 0/垃圾宽。字节定段、像素定 x。 */
        selRect.width = XPainter_textWidthRange(
            self->m_font, self->m_layoutText, cursorV, cursorV + seq);
        selRect.height = self->m_layoutLineHeight;
        if (cursorV < self->m_layoutLen
            && (!clip || (selRect.x < clip->x + clip->width
                          && selRect.x + selRect.width > clip->x)))
            XPainter_fillRect(painter, &selRect, self->m_palette.m_text);
    }
    return false;
}

/** @brief 逐字符绘制布局文本（选区段用高亮前景色；对标 textLayout draw）。 */
static void xlc_drawText(XLineControl* self, XPainter* painter,
                         const XPoint* offset, const XRect* clip,
                         bool hasSelection, int blinkStart, int blinkEnd)
{
    int pos = 0;
    int xacc = 0;
#if XPAINTER_ON
    while (pos < self->m_layoutLen) {
        int seq = xlc_seqLenAt(self->m_layoutText, self->m_layoutLen, pos);
        int w;
        uint32_t color;
        bool inSel;
        bool inBlink;
        if (seq <= 0) break;
        w = XPainter_textWidthRange(self->m_font, self->m_layoutText,
                                    pos, pos + seq);
        inSel = hasSelection && pos >= self->m_selstart && pos < self->m_selend;
        inBlink = !hasSelection && blinkStart >= 0
                  && pos >= blinkStart && pos < blinkEnd;
        if (inSel) color = self->m_palette.m_highlightedText;
        else if (inBlink) color = self->m_palette.m_window;
        else color = self->m_palette.m_text;
        if (!clip || (xacc + w > clip->x - offset->x))
            XPainter_drawGlyph(painter, offset->x + xacc,
                               offset->y + self->m_layoutAscent,
                               self->m_layoutText + pos, color);
        xacc += w;
        pos += seq;
    }
#else
    (void)self; (void)painter; (void)offset; (void)clip; (void)hasSelection;
    (void)blinkStart; (void)blinkEnd;
#endif /* XPAINTER_ON */
}

void XLineControl_draw(XLineControl* self, XPainter* painter,
                       const XPoint* offset, const XRect* clip, int flags)
{
    XPoint origin;
    bool hasSelection = false;
    bool cursorPhase;
    if (!self || !painter) return;
    /* 光标相位（14.123 ②）：blinkStatus 之外再叠加宿主经 Cursor 旗标
     * 下发的焦点门——HEAD 语义为"焦点内常显、失焦无光标/无掩码反选"，
     * 控制器无 widget 身份，焦点态只能由壳经旗标传入。 */
    cursorPhase = (flags & (int)XLineControlDrawFlag_Cursor) != 0
                  && self->m_blinkStatus != 0u
                  && !self->m_hideCursor;
    origin.x = offset ? offset->x : 0;
    origin.y = offset ? offset->y : 0;

    if (flags & (int)XLineControlDrawFlag_Selections)
        hasSelection = xlc_drawSelections(self, painter, &origin, clip,
                                          cursorPhase);

    if (flags & (int)XLineControlDrawFlag_Text) {
        int blinkStart = -1;
        int blinkEnd = -1;
        /* 掩码反选格仅掩码场景（inputMask 占位以 Window 色呈现）：
         * 此前门禁缺失，普通行编辑光标亮相时光标处字符被背景色清除
         * ——用户实测「光标左移后右侧字符变空白」。无掩码时字符恒
         * Text 色绘制，光标竖线由 DrawFlag_Cursor 分支覆盖（对标
         * qlineedit.cpp paintEvent：字符先绘、光标竖线覆盖其上）。 */
        if (self->m_maskData && !hasSelection && cursorPhase
            && self->m_cursor < self->m_layoutLen) {
            blinkStart = xlc_mapTextToLayout(self, self->m_cursor);
            blinkEnd = xlc_nextBoundary(self->m_layoutText, self->m_layoutLen,
                                        blinkStart);
        }
        xlc_drawText(self, painter, &origin, clip, hasSelection,
                     blinkStart, blinkEnd);
    }

    if (flags & (int)XLineControlDrawFlag_Cursor) {
        /* 门禁（R-32 内层）：有掩码时不画细光标——Qt 壳层仅在
         * inputMask 为空时下发 DrawCursor（qlineedit.cpp paintEvent），
         * 此处再挡一层，保证掩码反选格与细光标互斥，宿主误传双旗标
         * 也不出现两态同屏。 */
        if (cursorPhase && !self->m_maskData) {
            int cursor = xlc_cursorLayoutPos(self);
            {
                XRect cursorRect;
                cursorRect.x = origin.x + xlc_layoutCursorToX(self, cursor);
                cursorRect.y = origin.y;
                cursorRect.width = self->m_cursorWidth > 0
                                       ? self->m_cursorWidth : 1;
                cursorRect.height = self->m_layoutLineHeight;
                if (!clip || (cursorRect.x < clip->x + clip->width
                              && cursorRect.x + cursorRect.width > clip->x))
                    XPainter_fillRect(painter, &cursorRect,
                                      self->m_palette.m_text);
            }
        }
    }
}

/* ==================== 光标闪烁（对标 setBlinkingCursorEnabled 族） ==================== */

/** @brief XStyleHints cursorFlashTimeChanged 槽（联动闪烁重排）。 */
static void VXLineControl_styleHintsChanged(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XLineControl_updateCursorBlinking((XLineControl*)receiver);
}

void XLineControl_setBlinkingCursorEnabled(XLineControl* self, bool enable)
{
    if (!self || (self->m_blinkEnabled != 0u) == enable) return;
    self->m_blinkEnabled = enable ? 1u : 0u;
#if XSTYLEHINTS_ON && XGUIAPPLICATION_ON
    {
        XStyleHints* hints = XGuiApplication_styleHints();
        if (hints) {
            if (enable)
                XObject_connect_1((XObject*)hints,
                                  (size_t)XStyleHints_cursorFlashTimeChanged_signal(
                                      hints, 0),
                                  (XObject*)self, VXLineControl_styleHintsChanged,
                                  XConnectionType_Direct);
            else
                XObject_disconnect_1((XObject*)hints,
                                     (size_t)XStyleHints_cursorFlashTimeChanged_signal(
                                         hints, 0),
                                     (XObject*)self,
                                     VXLineControl_styleHintsChanged);
        }
    }
#endif
    XLineControl_updateCursorBlinking(self);
}

void XLineControl_updateCursorBlinking(XLineControl* self)
{
    if (!self) return;
    if (self->m_blinkTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_blinkTimer);
        self->m_blinkTimer = XTIMER_INVALID_ID;
    }
    if (self->m_blinkEnabled && !self->m_readOnly) {
        int flashTime = 1000; /* 默认闪周期（无样式提示时）。 */
#if XSTYLEHINTS_ON && XGUIAPPLICATION_ON
        {
            XStyleHints* hints = XGuiApplication_styleHints();
            if (hints) flashTime = XStyleHints_cursorFlashTime(hints);
        }
#endif
        if (flashTime >= 2)
            self->m_blinkTimer = XObject_startTimer_ms(
                (XObject*)self, (uint64_t)(flashTime / 2),
                XTimerType_CoarseTimer);
    }
    self->m_blinkStatus = 1;
    {
        XRect area;
        if (XStrlen(XLineControl_inputMask(self)) == 0)
            area = XLineControl_cursorRect(self);
        else {
            area.x = 0; area.y = 0; area.width = 0; area.height = 0;
        }
        xlc_emitRect(self, (size_t)XLineControl_updateNeeded_signal(self, area),
                     area);
    }
}

void XLineControl_resetCursorBlinkTimer(XLineControl* self)
{
    if (!self || !self->m_blinkEnabled
        || self->m_blinkTimer == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, self->m_blinkTimer);
    self->m_blinkTimer = XTIMER_INVALID_ID;
    {
        int flashTime = 1000;
#if XSTYLEHINTS_ON && XGUIAPPLICATION_ON
        {
            XStyleHints* hints = XGuiApplication_styleHints();
            if (hints) flashTime = XStyleHints_cursorFlashTime(hints);
        }
#endif
        if (flashTime >= 2)
            self->m_blinkTimer = XObject_startTimer_ms(
                (XObject*)self, (uint64_t)(flashTime / 2),
                XTimerType_CoarseTimer);
    }
    self->m_blinkStatus = 1;
}

bool XLineControl_cursorBlinkStatus(const XLineControl* self)
{ return self ? self->m_blinkStatus != 0u : false; }

/* ==================== 生命周期（对标 init/构造/析构/timerEvent） ==================== */

/** @brief 定时器事件虚槽（对标 timerEvent；闪烁/清空/三击/密码回显）。 */
static void VXLineControl_timerEvent(XObject* object, XTimerEvent* event)
{
    XLineControl* self = (XLineControl*)object;
    XTimerId id;
    if (!self || !event) return;
    id = XTimerEvent_timerId(event);
    if (id != XTIMER_INVALID_ID && id == self->m_blinkTimer) {
        self->m_blinkStatus = self->m_blinkStatus ? 0u : 1u;
        {
            XRect area;
            if (XStrlen(XLineControl_inputMask(self)) == 0)
                area = XLineControl_cursorRect(self);
            else {
                area.x = 0; area.y = 0; area.width = 0; area.height = 0;
            }
            xlc_emitRect(self,
                         (size_t)XLineControl_updateNeeded_signal(self, area),
                         area);
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    if (id != XTIMER_INVALID_ID && id == self->m_deleteAllTimer) {
        XObject_killTimer((XObject*)self, self->m_deleteAllTimer);
        self->m_deleteAllTimer = XTIMER_INVALID_ID;
        XLineControl_clear(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    if (id != XTIMER_INVALID_ID && id == self->m_tripleClickTimer) {
        XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
        self->m_tripleClickTimer = XTIMER_INVALID_ID;
        XEvent_accept((XEvent*)event);
        return;
    }
    if (id != XTIMER_INVALID_ID && id == self->m_passwordEchoTimer) {
        XObject_killTimer((XObject*)self, self->m_passwordEchoTimer);
        self->m_passwordEchoTimer = XTIMER_INVALID_ID;
        self->m_passwordEchoEditing = 0u;
        xlc_updateDisplayText(self, false);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

/** @brief 析构虚槽（对标 ~QWidgetLineControl；密码内存清零）。 */
static void VXLineControl_deinit(XLineControl* self)
{
    if (!self) return;
    xlc_cancelPasswordEchoTimer(self);
    if (self->m_blinkTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_blinkTimer);
    if (self->m_deleteAllTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_deleteAllTimer);
    if (self->m_tripleClickTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
    /* 密码类回显下销毁前清零文本内存（对标 m_text.fill(u'\0')）。 */
    if (self->m_echoMode != (uint32_t)XLineControlEchoMode_Normal
        && self->m_text && self->m_textCap > 0)
        XMemset(self->m_text, 0, (size_t)self->m_textCap);
    xlc_bufFree(&self->m_text, &self->m_textLen, &self->m_textCap);
    xlc_bufFree(&self->m_displayText, &self->m_displayLen, &self->m_displayCap);
    xlc_bufFree(&self->m_preeditText, &self->m_preeditLen, &self->m_preeditCap);
    xlc_bufFree(&self->m_layoutText, &self->m_layoutLen, &self->m_layoutCap);
    xlc_bufFree(&self->m_textReturn, &self->m_textReturnLen,
                &self->m_textReturnCap);
    xlc_bufFree(&self->m_maskReturn, &self->m_maskReturnLen,
                &self->m_maskReturnCap);
    xlc_bufFree(&self->m_cancelText, NULL, &self->m_cancelTextCap);
    if (self->m_font) {
        XFont_delete_base((XClass*)self->m_font);
        self->m_font = NULL;
    }
    if (self->m_maskData) {
        XFree_System(self->m_maskData);
        self->m_maskData = NULL;
    }
    self->m_maskDataCount = 0;
    if (self->m_inputMask) {
        XFree_System(self->m_inputMask);
        self->m_inputMask = NULL;
    }
    if (self->m_history) {
        XFree_System(self->m_history);
        self->m_history = NULL;
    }
    self->m_historySize = 0;
    self->m_historyCap = 0;
    self->m_undoState = 0;
    if (self->m_transactions) {
        XFree_System(self->m_transactions);
        self->m_transactions = NULL;
    }
    self->m_transactionCount = 0;
    self->m_transactionCap = 0;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XLineControl_init(XLineControl* self, const char* txt)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XLineControl);

    /* 默认值逐项对齐 Qt 构造函数。 */
    self->m_cursor = 0;
    self->m_preeditCursor = 0;
    self->m_preeditPos = -1;
    self->m_preeditLayoutPos = 0;
    self->m_cursorWidth = 0;
    self->m_layoutDirection = (int)XLineControlLayoutDirection_Auto;
    self->m_cursorMoveStyle =
        (int)XLineControlCursorMoveStyle_LogicalMoveStyle;
    self->m_hideCursor = 0;
    self->m_separator = 0;
    self->m_readOnly = 0;
    self->m_dragEnabled = 0;
    self->m_echoMode = (uint32_t)XLineControlEchoMode_Normal;
    self->m_textDirty = 0;
    self->m_selDirty = 0;
    self->m_pendingInputRejected = 0u;
    self->m_validInput = 1;
    self->m_blinkStatus = 0;
    self->m_blinkEnabled = 0;
    self->m_blinkTimer = XTIMER_INVALID_ID;
    self->m_deleteAllTimer = XTIMER_INVALID_ID;
    self->m_tripleClickTimer = XTIMER_INVALID_ID;
    self->m_passwordEchoTimer = XTIMER_INVALID_ID;
    self->m_layoutAscent = 0;
    self->m_maxLength = XLC_DEFAULT_MAX_LENGTH;
    self->m_lastCursorPos = -1;
    self->m_maskData = NULL;
    self->m_maskDataCount = 0;
    self->m_modifiedState = 0;
    self->m_undoState = 0;
    self->m_selstart = 0;
    self->m_selend = 0;
    self->m_passwordEchoEditing = false;
    self->m_passwordMaskDelay = -1; /* 无平台主题：默认禁用（Qt 由主题注入）。 */
    /* 缺省跟随部署平台（XGui 主战场 Linux/X11，对齐 Qt 平台主题口径）；
     * 嵌入式接入层可经 setKeyboardScheme 覆盖。 */
    self->m_keyboardScheme = (int)XLineControlKeyboardScheme_X11;
    self->m_keypadNavigationEnabled = false;
    self->m_accessibleObject = NULL;
    self->m_completer = NULL;
    self->m_blank[0] = ' ';
    self->m_blank[1] = '\0';
    self->m_passwordCharacter[0] = '*';
    self->m_passwordCharacter[1] = '\0';
    self->m_palette.m_highlight = XLC_COLOR_HIGHLIGHT;
    self->m_palette.m_highlightedText = XLC_COLOR_HIGHLIGHTEDTEXT;
    self->m_palette.m_text = XLC_COLOR_TEXT;
    self->m_palette.m_window = XLC_COLOR_WINDOW;

    /* init(txt)：设文本并让光标落尾。 */
    {
        int txtLen = txt ? (int)XStrlen(txt) : 0;
        xlc_bufAssign(&self->m_text, &self->m_textLen, &self->m_textCap,
                      txt, txtLen);
        xlc_updateDisplayText(self, false);
        self->m_cursor = self->m_textLen;
    }
}

XLineControl* XLineControl_create_ex(XMemoryType memory, const char* txt)
{
    XLineControl* self = (XLineControl*)XMemory_malloc(sizeof(XLineControl),
                                                       memory);
    if (!self) return NULL;
    XLineControl_init(self, txt);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XVtable* XLineControl_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLineControl)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXLineControl_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLineControl_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 内部奇偶校验钩子（对标私有 internalRemove 的外部承载） ==================== */

/*
 * Qt 6.8 头文件声明 internalRemove(int) 而源文件无定义（私有死声明）。
 * 本实现按声明语义补齐（见 XLineControl_internalRemove 注释），并以
 * 外部链接承载供接入层组合使用，同时消除未使用告警。
 */
void XLineControl_internalRemove_public(XLineControl* self, int pos)
{ XLineControl_internalRemove(self, pos); }

#endif /* XLINECONTROL_ON */
