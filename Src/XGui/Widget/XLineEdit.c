/**
 * @file       XLineEdit.c
 * @brief      XLineEdit 单行编辑控件实现（对标 Qt 6.8 QLineEdit 全部公共 API）。
 * @details    内部表示：文本为 UTF-8 动态缓冲（NUL 结尾），光标/选区锚点
 *             为其中的字节偏移（恒定落在 UTF-8 字符边界上）。编辑操作：
 *             - 插入：可打印 ASCII（0x20..0x7e）与粘贴文本，maxLength 按
 *               字符数钳位（UTF-8 多字节字符整体计 1），inputMask 按位置
 *               类别逐字符过滤，validator 返回 Invalid 时整体拒绝并发射
 *               inputRejected；
 *             - 删除：Backspace 删光标前字符、Delete 删光标处字符（按
 *               UTF-8 续字节跳过，保证不切字符）；有选区时先删选区；
 *             - 移动：Left/Right 按 UTF-8 字符边界，Home/End 到首/尾，
 *               Ctrl+方向键按词移动，Shift+方向键扩展选区；
 *             - 快捷键：Ctrl+A 全选、Ctrl+C/X/V 复制/剪切/粘贴、
 *               Ctrl+Z/Y 撤销/重做（readOnly 时仅允许全选与复制）。
 *             绘制：frame 开时画凹陷边框；Base 底；回显模式决定显示文本
 *             （NoEcho 空、Password '*'、PasswordEchoOnEdit 焦点内正常）；
 *             inputMask 开启时显示按掩码过滤（不匹配字符显示占位符）；
 *             placeholder 在空文本时以 Mid 灰显；选区以 Highlight 反色
 *             高亮；清除按钮启用时文本非空绘制右侧简笔 ×；光标为焦点内
 *             的 1px 竖线（常显；闪烁为后续扩展）；文本超宽时按光标位置
 *             水平滚动（简化估算度量 8px/字符）。
 *             撤销/重做：每次用户编辑前把当前文本快照压入撤销栈（深
 *             XLINEEDIT_UNDO_DEPTH=20），undo/redo 交换快照并发射
 *             textChanged；setText 清空历史（Qt 语义）。
 *             剪贴板：优先 XGuiApplication_clipboard 的 XClipboard
 *             （XClipboardMode_Clipboard）；剪贴板不可用（未创建 GUI 应用
 *             或 XCLIPBOARD_ON/XGUIAPPLICATION_ON 关闭）时回退控件内部
 *             m_clipboardText 缓冲。
 *             键盘处理挂 XWidget 的 KeyPressEvent 虚槽；鼠标左键按下获得
 *             焦点并把光标定位到点击处，双击全选；失焦提交 editingFinished
 *             （自上次发射后用户编辑过才发射）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XLINEEDIT_ON

#include "XLineEdit.h"
#include "XWidget_Protected.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"
#include "XVarList.h"
#include "XString.h"
#include "XClipboard.h"
#if XMENU_ON
#include "XMenu.h"
#endif /* XMENU_ON */
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
#include <string.h>
#include <stdlib.h>

/** @brief 当前聚焦的 XLineEdit（全局；IME CommitString 直投目标）。 */
static XLineEdit* g_focusedLineEdit = NULL;

/* 光标竖线宽度与文本估算度量（每字符 8px，与点阵默认一致） */
#define XLINEEDIT_CURSOR_W 1
#define XLINEEDIT_CHAR_W   8
/* 内置 action 图标区宽度 */
#define XLINEEDIT_ACTION_W 16

/* ==================== 前向声明 ==================== */
static void  VXLineEdit_keyPressEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_inputMethodEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_keyReleaseEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_mousePressEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_focusInEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_focusOutEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_paintEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_changeEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_deinit(XLineEdit* self);
static void  VXLineEdit_copy(XLineEdit* self, const XLineEdit* other);
static void  VXLineEdit_move(XLineEdit* self, XLineEdit* other);
static void  xlineedit_updateSizeHints(XLineEdit* self);

/* ==================== 内部辅助 ==================== */

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t xlineedit_color(const XLineEdit* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief UTF-8 字符串的字符数（字节长度不含续字节）。 */
static size_t xlineedit_charCount(const char* utf8)
{
    size_t chars = 0;
    if (!utf8) return 0;
    while (*utf8) {
        ++utf8;
        while ((*utf8 & 0xC0u) == 0x80u) ++utf8; /* 跳过 10xxxxxx 续字节 */
        ++chars;
    }
    return chars;
}

/** @brief UTF-8 文本前 byteLen 字节内的字符数（byteLen 须为字符边界）。 */
static size_t xlineedit_charCountPrefix(const char* text, size_t byteLen)
{
    size_t chars = 0;
    size_t i = 0;
    if (!text) return 0;
    while (i < byteLen && text[i]) {
        ++i;
        while (i < byteLen && ((unsigned char)text[i] & 0xC0u) == 0x80u) ++i;
        ++chars;
    }
    return chars;
}

/** @brief 计算显示文本前 charCount 个字符的真实像素宽度。
 * @details 与 XPainter_drawText / posToCursor / cursorRect 使用同一字体
 *          度量（XPainter_textWidthRange 逐字符累计）：中文等双宽字符
 *          按真实字形宽计算，西文按单宽；旧实现按「字符数 × 固定 8px」
 *          估算，中文输入时光标/选区与文字错位。 */
static int xlineedit_displayWidth(const XFont* font, const char* display,
                                  size_t charCount)
{
    int width = 0;
    size_t byte = 0;
    size_t i;
    if (!font || !display) return 0;
    for (i = 0; i < charCount && display[byte]; ++i) {
        size_t next = byte;
        int charW;
        ++next;
        while ((display[next] & 0xC0u) == 0x80u) ++next;
        charW = XPainter_textWidthRange(font, display, (int)byte, (int)next);
        if (charW > 0) width += charW;
        byte = next;
    }
    return width;
}

/** @brief 从字节偏移向前回退到 UTF-8 字符边界（返回新偏移）。 */
static size_t xlineedit_prevBoundary(const char* text, size_t pos)
{
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && ((unsigned char)text[pos] & 0xC0u) == 0x80u) --pos;
    return pos;
}

/** @brief 从字节偏移向后前进到 UTF-8 字符边界（返回新偏移）。 */
static size_t xlineedit_nextBoundary(const char* text, size_t pos)
{
    if (!text || !text[pos]) return pos;
    ++pos;
    while (text[pos] && ((unsigned char)text[pos] & 0xC0u) == 0x80u) ++pos;
    return pos;
}

/** @brief 是否空白 ASCII 字符（词移动判定）。 */
static bool xlineedit_isSpace(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/** @brief 发射 const char* 参数信号（textChanged/textEdited）。 */
static void xlineedit_emitTextSignal(XLineEdit* self, size_t signal)
{
    XVarList* arguments;
    if (!self || !self->m_text) return;
    arguments = XVarList_Create(XVar(const char*, self->m_text));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 void 信号（returnPressed/editingFinished/selectionChanged/
 *         inputRejected）。 */
static void xlineedit_emitVoidSignal(XLineEdit* self, size_t signal)
{
    XVarList* arguments = XVarList_create(0);
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 cursorPositionChanged(int,int) 信号。 */
static void xlineedit_emitCursorPosSignal(XLineEdit* self, int oldPos,
                                          int newPos)
{
    XVarList* arguments = XVarList_Create(XVar(int, oldPos),
                                          XVar(int, newPos));
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XLineEdit_cursorPositionChanged_signal,
                           arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/* ==================== 选区 ==================== */

/** @brief 是否存在选区（锚点与光标不等）。 */
static bool xlineedit_hasSelection(const XLineEdit* self)
{
    return self && self->m_anchor != self->m_cursor;
}

/** @brief 选区起点字节偏移。 */
static size_t xlineedit_selStart(const XLineEdit* self)
{
    return self->m_anchor < self->m_cursor ? self->m_anchor : self->m_cursor;
}

/** @brief 选区终点字节偏移。 */
static size_t xlineedit_selEnd(const XLineEdit* self)
{
    return self->m_anchor > self->m_cursor ? self->m_anchor : self->m_cursor;
}

/* ==================== 撤销/重做栈 ==================== */

/** @brief 把当前文本快照压入撤销栈（栈满丢弃最旧）。 */
static void xlineedit_undoPush(XLineEdit* self)
{
    char* snap;
    if (!self || !self->m_text) return;
    snap = (char*)XMalloc_System(strlen(self->m_text) + 1);
    if (!snap) return;
    strcpy(snap, self->m_text);
    if (self->m_undoCount == XLINEEDIT_UNDO_DEPTH) {
        XFree_System(self->m_undoStack[0]);
        memmove(&self->m_undoStack[0], &self->m_undoStack[1],
                (size_t)(XLINEEDIT_UNDO_DEPTH - 1) * sizeof(char*));
        self->m_undoCount = XLINEEDIT_UNDO_DEPTH - 1;
    }
    self->m_undoStack[self->m_undoCount++] = snap;
}

/** @brief 清空撤销栈。 */
static void xlineedit_undoClear(XLineEdit* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_undoCount; ++i)
        XFree_System(self->m_undoStack[i]);
    self->m_undoCount = 0;
}

/** @brief 把当前文本快照压入重做栈（栈满丢弃最旧）。 */
static void xlineedit_redoPush(XLineEdit* self)
{
    char* snap;
    if (!self || !self->m_text) return;
    snap = (char*)XMalloc_System(strlen(self->m_text) + 1);
    if (!snap) return;
    strcpy(snap, self->m_text);
    if (self->m_redoCount == XLINEEDIT_UNDO_DEPTH) {
        XFree_System(self->m_redoStack[0]);
        memmove(&self->m_redoStack[0], &self->m_redoStack[1],
                (size_t)(XLINEEDIT_UNDO_DEPTH - 1) * sizeof(char*));
        self->m_redoCount = XLINEEDIT_UNDO_DEPTH - 1;
    }
    self->m_redoStack[self->m_redoCount++] = snap;
}

/** @brief 清空重做栈。 */
static void xlineedit_redoClear(XLineEdit* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_redoCount; ++i)
        XFree_System(self->m_redoStack[i]);
    self->m_redoCount = 0;
}

/* ==================== 输入掩码 ==================== */

/**
 * @brief 从掩码下标 cur 前进到下一个可编辑条目（跳过字面分隔符）。
 * @return 条目下标；耗尽（含 ';' 占位符后缀与结尾）返回 -1。
 */
static int xlineedit_maskNextEntry(const char* mask, int cur,
                                   char* classOut, bool* mandatoryOut)
{
    int i = cur;
    char c;
    if (!mask) return -1;
    while ((c = mask[i]) != '\0') {
        if (c == ';') return -1;
        switch (c) {
        case '0': case '9': case '#':
        case 'A': case 'a':
        case 'N': case 'n':
        case 'X': case 'x':
            if (classOut) *classOut = c;
            if (mandatoryOut)
                *mandatoryOut = (c == '0' || c == 'A' || c == 'N' || c == 'X');
            return i;
        default:
            ++i; /* 跳过字面分隔符 */
        }
    }
    return -1;
}

/** @brief 第 n 个文本字符对应的掩码条目信息（跳过字面量）；超出返回 false。 */
static bool xlineedit_maskEntryN(const char* mask, size_t n,
                                 char* classOut, bool* mandatoryOut)
{
    size_t count = 0;
    int entry = -1;
    if (!mask || mask[0] == '\0') return false;
    while (1) {
        entry = xlineedit_maskNextEntry(mask, entry + 1, classOut,
                                        mandatoryOut);
        if (entry < 0) return false;
        if (count == n) return true;
        ++count;
    }
}

/** @brief 单字节字符是否匹配掩码类别。 */
static bool xlineedit_maskCharMatches(char maskClass, unsigned char ch)
{
    switch (maskClass) {
    case '0':
    case '9':
        return ch >= '0' && ch <= '9';
    case '#':
        return (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' ||
               ch == ' ';
    case 'A':
    case 'a':
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    case 'N':
    case 'n':
        return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= 'a' && ch <= 'z');
    case 'X':
    case 'x':
        return true;
    default:
        return false;
    }
}

/** @brief 文本是否满足掩码：每个字符匹配其位置类别且全部必填位已填。 */
static bool xlineedit_maskSatisfied(const char* mask, const char* text)
{
    int entry = -1;
    char classChar;
    bool mandatory;
    const char* p;
    if (!mask || mask[0] == '\0') return true;
    if (!text) text = "";
    p = text;
    while (*p) {
        entry = xlineedit_maskNextEntry(mask, entry + 1, &classChar,
                                        &mandatory);
        if (entry < 0) return false; /* 文本超出掩码长度 */
        if (!xlineedit_maskCharMatches(classChar, (unsigned char)*p))
            return false;
        ++p;
        while ((*p & 0xC0u) == 0x80u) ++p;
    }
    while (1) {
        entry = xlineedit_maskNextEntry(mask, entry + 1, &classChar,
                                        &mandatory);
        if (entry < 0) break;
        if (mandatory) return false; /* 必填位缺字符 */
    }
    return true;
}

/**
 * @brief 按掩码过滤插入串：逐字符对照插入位置的掩码类别，匹配者保留
 *         （'X'/'x' 保留整个 UTF-8 字符），不匹配者丢弃。结果写入 out。
 */
static void xlineedit_filterInsert(const XLineEdit* self, size_t posByte,
                                   const char* insert, char* out,
                                   size_t outCap)
{
    size_t charIndex = 0;
    const char* p;
    size_t o = 0;
    if (!insert || !out || outCap == 0) return;
    out[0] = '\0';
    if (!self->m_inputMask || self->m_inputMask[0] == '\0') {
        strncpy(out, insert, outCap - 1);
        out[outCap - 1] = '\0';
        return;
    }
    /* 计算 posByte 之前的字符数（插入点的掩码序号起点）。 */
    p = self->m_text;
    while (p < self->m_text + posByte && *p) {
        ++p;
        while ((*p & 0xC0u) == 0x80u) ++p;
        ++charIndex;
    }
    for (; *insert && o + 1 < outCap; ) {
        char classChar;
        bool mandatory;
        size_t charLen = 1;
        bool matched = false;
        while ((insert[charLen] & 0xC0u) == 0x80u) ++charLen;
        if (!xlineedit_maskEntryN(self->m_inputMask, charIndex,
                                  &classChar, &mandatory))
            break; /* 超出掩码长度：丢弃剩余 */
        if (classChar == 'X' || classChar == 'x') {
            if (o + charLen + 1 > outCap) break;
            memcpy(out + o, insert, charLen);
            o += charLen;
            matched = true;
        } else if (charLen == 1 &&
                   xlineedit_maskCharMatches(classChar,
                                             (unsigned char)*insert)) {
            out[o++] = *insert;
            matched = true;
        }
        if (matched) ++charIndex;
        insert += charLen;
    }
    out[o] = '\0';
}

/* ==================== 显示文本（回显 + 掩码过滤） ==================== */

/**
 * @brief 把第 charIndex 个原始字符按回显模式与掩码转换为显示字节并追加
 *        到 out（容量 cap）；返回写入字节数（0 = 不显示）。
 */
static size_t xlineedit_appendDisplayChar(const XLineEdit* self,
                                          const char* ch, size_t charLen,
                                          size_t charIndex, char* out,
                                          size_t cap)
{
    bool starAll;
    char placeholder = ' ';
    if (!self || !ch || !out || cap == 0) return 0;
    if (self->m_echoMode == XLineEditEchoMode_NoEcho) return 0;
    starAll = (self->m_echoMode == XLineEditEchoMode_Password ||
               (self->m_echoMode == XLineEditEchoMode_PasswordEchoOnEdit &&
                !XWidget_hasFocus((XWidget*)self)));
    if (starAll) {
        out[0] = '*';
        return 1;
    }
    if (self->m_inputMask && self->m_inputMask[0]) {
        char classChar;
        bool mandatory;
        if (!xlineedit_maskEntryN(self->m_inputMask, charIndex,
                                  &classChar, &mandatory))
            return 0; /* 超出掩码：不显示 */
        if (classChar == 'X' || classChar == 'x' ||
            (charLen == 1 &&
             xlineedit_maskCharMatches(classChar, (unsigned char)*ch))) {
            if (cap < charLen) return 0;
            memcpy(out, ch, charLen);
            return charLen;
        }
        {
            const char* semi = strchr(self->m_inputMask, ';');
            if (semi && semi[1]) placeholder = semi[1];
        }
        out[0] = placeholder;
        return 1;
    }
    if (cap < charLen) return 0;
    memcpy(out, ch, charLen);
    return charLen;
}

/** @brief 刷新显示缓存 m_displayBuf（回显 + 掩码过滤后的完整显示文本）。 */
static void xlineedit_refreshDisplay(XLineEdit* self)
{
    size_t len;
    size_t cap;
    size_t o = 0;
    size_t charIndex = 0;
    const char* p;
    char* updated;
    if (!self) return;
    if (!self->m_text) {
        if (!self->m_displayBuf)
            self->m_displayBuf = (char*)XMalloc_System(1);
        if (self->m_displayBuf) self->m_displayBuf[0] = '\0';
        return;
    }
    len = strlen(self->m_text);
    cap = len + 1;
    if (self->m_displayBuf)
        updated = (char*)XRealloc_System(self->m_displayBuf, cap);
    else
        updated = (char*)XMalloc_System(cap);
    if (!updated) return;
    self->m_displayBuf = updated;
    p = self->m_text;
    while (*p) {
        size_t charLen = 1;
        size_t written;
        while ((p[charLen] & 0xC0u) == 0x80u) ++charLen;
        written = xlineedit_appendDisplayChar(self, p, charLen, charIndex,
                                              self->m_displayBuf + o,
                                              cap - 1 - o);
        o += written;
        ++charIndex;
        p += charLen;
    }
    self->m_displayBuf[o] = '\0';
}

/**
 * @brief 按选区把显示文本拆为三段（seg0 选区前、seg1 选中段、seg2 选区
 *        后），并输出各段字符数（像素估算用）。各 seg 由调用方分配，
 *        容量须不小于显示文本长度 +1。
 */
static void xlineedit_splitDisplay(const XLineEdit* self, size_t selStart,
                                   size_t selEnd, char* seg0, char* seg1,
                                   char* seg2, size_t* chars0, size_t* chars1,
                                   size_t* chars2)
{
    const char* p;
    size_t charIndex = 0;
    size_t o0 = 0, o1 = 0, o2 = 0;
    size_t c0 = 0, c1 = 0, c2 = 0;
    size_t cap = 0;
    if (!self || !self->m_text) goto done;
    cap = strlen(self->m_text) + 1;
    p = self->m_text;
    while (*p) {
        size_t charLen = 1;
        size_t start = (size_t)(p - self->m_text);
        char* seg;
        size_t* o;
        size_t* c;
        size_t written;
        while ((p[charLen] & 0xC0u) == 0x80u) ++charLen;
        if (start < selStart) { seg = seg0; o = &o0; c = &c0; }
        else if (start < selEnd) { seg = seg1; o = &o1; c = &c1; }
        else { seg = seg2; o = &o2; c = &c2; }
        written = xlineedit_appendDisplayChar(self, p, charLen, charIndex,
                                              seg ? seg + *o : NULL,
                                              seg ? cap - 1 - *o : 0);
        *o += written;
        if (written) ++*c;
        ++charIndex;
        p += charLen;
    }
done:
    if (seg0) seg0[o0] = '\0';
    if (seg1) seg1[o1] = '\0';
    if (seg2) seg2[o2] = '\0';
    if (chars0) *chars0 = c0;
    if (chars1) *chars1 = c1;
    if (chars2) *chars2 = c2;
}

/* ==================== 滚动与光标定位 ==================== */

/** @brief 按光标位置更新水平滚动偏移（简化估算度量，光标保持可见）。 */
static void xlineedit_updateViewOffset(XLineEdit* self)
{
    int textStart;
    int textEnd;
    int visibleW;
    int textW;
    int cursorX;
    int lo;
    int hi;
    size_t chars;
    size_t curChars;
    if (!self || !self->m_text) return;
    {
        XFont font = XWidget_font((XWidget*)self);
        chars = xlineedit_charCount(self->m_text);
        curChars = xlineedit_charCountPrefix(self->m_text, self->m_cursor);
        textStart = (self->m_frame ? 4 : 2) + self->m_textMargins.left;
        textEnd = XWidget_width((XWidget*)self) - (self->m_frame ? 4 : 2) -
                  self->m_textMargins.right -
                  ((self->m_clearButtonEnabled && self->m_text[0]) ? 18 : 0);
        if (textEnd <= textStart) textEnd = textStart + 1;
        visibleW = textEnd - textStart;
        /* 总宽/光标位都用真实字体宽度（逐字符累计，中文双宽正确），
           与 drawText 渲染、posToCursor/cursorRect 同度量。 */
        textW = xlineedit_displayWidth(&font, self->m_text, chars);
        if (textW <= visibleW) {
            self->m_viewOffset = 0;
            return;
        }
        cursorX = xlineedit_displayWidth(&font, self->m_text, curChars);
        lo = cursorX - visibleW + 1;
        if (lo < 0) lo = 0;
        hi = textW - visibleW;
        self->m_viewOffset = cursorX;
        if (self->m_viewOffset < lo) self->m_viewOffset = lo;
        if (self->m_viewOffset > hi) self->m_viewOffset = hi;
    }
}

/** @brief 像素 x 对应的光标字节偏移（简化估算度量反解）。 */
/** @brief 文本区起始 x（边框+边距+起始侧 action 区）。 */
static int xlineedit_textStartX(const XLineEdit* self)
{
    int x = (self->m_frame ? 4 : 2) + self->m_textMargins.left;
    int i;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        if (self->m_actionPositions[i] == XLineEditActionPosition_Leading)
            x += XLINEEDIT_ACTION_W;
    }
    return x;
}

/** @brief 末尾侧 action 占用总宽。 */
static int xlineedit_trailingActionWidth(const XLineEdit* self)
{
    int w = 0;
    int i;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        if (self->m_actionPositions[i] == XLineEditActionPosition_Trailing)
            w += XLINEEDIT_ACTION_W;
    }
    return w;
}

/** @brief 命中 action（x 坐标 → 索引；未命中返回 -1）。 */
static int xlineedit_hitAction(const XLineEdit* self, int x)
{
    int w = XWidget_width((XWidget*)self);
    int textStart = xlineedit_textStartX(self);
    int i;
    int leadIdx = 0;
    int trailIdx = 0;
    if (!self->m_actionCount) return -1;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        if (self->m_actionPositions[i] == XLineEditActionPosition_Leading) {
            int ax = textStart - XLINEEDIT_ACTION_W * (leadIdx + 1);
            if (x >= ax && x < ax + XLINEEDIT_ACTION_W) return i;
            ++leadIdx;
        } else {
            int ax = w - XLINEEDIT_ACTION_W * (trailIdx + 1);
            if (x >= ax && x < ax + XLINEEDIT_ACTION_W) return i;
            ++trailIdx;
        }
    }
    (void)textStart;
    return -1;
}

/** @brief 绘制 action 区（iconText 文本或空）。 */
static void xlineedit_paintActions(const XLineEdit* self, XPainter* painter,
                                   uint32_t textColor)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int leadIdx = 0;
    int trailIdx = 0;
    int i;
    if (!self->m_actionCount) return;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        XAction* action = self->m_actions[i];
        const char* iconText = "";
        int ax;
        int ay;
        if (!action) continue;
        if (self->m_actionPositions[i] == XLineEditActionPosition_Leading) {
            ax = xlineedit_textStartX(self) - XLINEEDIT_ACTION_W * (leadIdx + 1);
            ++leadIdx;
        } else {
            ax = w - XLINEEDIT_ACTION_W * (trailIdx + 1);
            ++trailIdx;
        }
        ay = (h - 14) / 2;
        {
            const XString* it = XAction_iconText_const(action);
            if (it) iconText = XString_toUtf8(it);
        }
        if (iconText && iconText[0])
            XPainter_drawText(painter, ax + 4, ay + 12, iconText, textColor);
        (void)h;
    }
}

static size_t xlineedit_posToCursor(const XLineEdit* self, int x)
{
    XFont font;
    int textStart;
    int clickX;
    size_t chars;
    size_t boundary;
    int prevWidth;
    const char* p;
    if (!self || !self->m_text) return 0;
    font = XWidget_font((XWidget*)self);
    textStart = xlineedit_textStartX(self);
    clickX = x - textStart + self->m_viewOffset;
    chars = xlineedit_charCount(self->m_text);
    if (clickX <= 0) return 0;
    /* 逐字符累计真实文本宽度，点击落在字符前半取其左边界、后半取
       其右边界（与绘制字体一致的度量，中文/西文都正确）。 */
    boundary = 0;
    prevWidth = 0;
    p = self->m_text;
    while (boundary < strlen(self->m_text)) {
        size_t next = boundary;
        int charW;
        ++next;
        while ((self->m_text[next] & 0xC0u) == 0x80u) ++next;
        charW = XPainter_textWidthRange(&font, self->m_text,
                                        (int)boundary, (int)next);
        if (prevWidth + charW / 2 > clickX)
            return boundary;
        prevWidth += charW;
        boundary = next;
        if ((int)chars == 0) break;
    }
    (void)p;
    return strlen(self->m_text);
}

/** @brief 移动光标（mark=true 保留锚点扩展选区；false 清除选区）并发射
 *         cursorPositionChanged/selectionChanged。 */
static void xlineedit_moveCursor(XLineEdit* self, size_t newPos, bool mark)
{
    size_t oldCursor;
    size_t maxPos;
    bool oldSel;
    bool newSel;
    if (!self || !self->m_text) return;
    maxPos = strlen(self->m_text);
    if (newPos > maxPos) newPos = maxPos;
    oldCursor = self->m_cursor;
    oldSel = xlineedit_hasSelection(self);
    if (newPos == oldCursor && (mark || self->m_anchor == oldCursor))
        return;
    self->m_cursor = newPos;
    if (!mark) self->m_anchor = newPos;
    newSel = xlineedit_hasSelection(self);
    if (self->m_cursor != oldCursor)
        xlineedit_emitCursorPosSignal(self, (int)oldCursor,
                                      (int)self->m_cursor);
    if (oldSel != newSel)
        xlineedit_emitVoidSignal(self, (size_t)XLineEdit_selectionChanged_signal);
    xlineedit_updateViewOffset(self);
    XWidget_update((XWidget*)self);
}

/** @brief 直接设置选区（锚点与光标）；变化时发射 selectionChanged。 */
static void xlineedit_setSelectionRange(XLineEdit* self, size_t anchor,
                                        size_t cursor)
{
    bool oldSel;
    bool newSel;
    size_t oldStart;
    size_t oldEnd;
    if (!self) return;
    oldSel = xlineedit_hasSelection(self);
    oldStart = xlineedit_selStart(self);
    oldEnd = xlineedit_selEnd(self);
    self->m_anchor = anchor;
    self->m_cursor = cursor;
    newSel = xlineedit_hasSelection(self);
    if (newSel != oldSel ||
        (newSel && (xlineedit_selStart(self) != oldStart ||
                    xlineedit_selEnd(self) != oldEnd))) {
        xlineedit_emitVoidSignal(self,
                                 (size_t)XLineEdit_selectionChanged_signal);
    }
    xlineedit_updateViewOffset(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 文本提交核心 ==================== */

/**
 * @brief 提交新文本并统一更新状态。
 * @param newText 新文本（可借用，函数内部复制）。
 * @param newCursor 新光标字节偏移。
 * @param userEdited true=用户编辑：压撤销栈、清重做栈、置 modified 与
 *        editingFinished 待发标志，并发射 textEdited；
 * @param clearHistory true=清空撤销/重做历史（setText 语义）；
 * @param emitChanged true=发射 textChanged。
 */
static void xlineedit_setContent(XLineEdit* self, const char* newText,
                                 size_t newCursor, bool userEdited,
                                 bool clearHistory, bool emitChanged)
{
    size_t oldCursor;
    bool oldSel;
    char* updated;
    if (!self) return;
    if (!newText) newText = "";
    oldCursor = self->m_cursor;
    oldSel = xlineedit_hasSelection(self);

    if (userEdited) {
        xlineedit_undoPush(self);
        xlineedit_redoClear(self);
        self->m_modified = true;
        self->m_finishedPending = true;
    }
    if (clearHistory) {
        xlineedit_undoClear(self);
        xlineedit_redoClear(self);
    }

    updated = (char*)XRealloc_System(self->m_text, strlen(newText) + 1);
    if (!updated) return;
    self->m_text = updated;
    strcpy(self->m_text, newText);
    self->m_cursor = newCursor;
    self->m_anchor = newCursor;

    xlineedit_refreshDisplay(self);
    if (emitChanged) {
        xlineedit_emitTextSignal(self, (size_t)XLineEdit_textChanged_signal);
        if (userEdited)
            xlineedit_emitTextSignal(self,
                                     (size_t)XLineEdit_textEdited_signal);
    }
    if (self->m_cursor != oldCursor)
        xlineedit_emitCursorPosSignal(self, (int)oldCursor,
                                      (int)self->m_cursor);
    if (oldSel || xlineedit_hasSelection(self))
        xlineedit_emitVoidSignal(self,
                                 (size_t)XLineEdit_selectionChanged_signal);
    xlineedit_updateViewOffset(self);
    xlineedit_updateSizeHints(self);
    XWidget_update((XWidget*)self);
}

/**
 * @brief 在光标处插入 utf8（替换选区）；按掩码逐字符过滤、maxLength
 *        钳位、validator 拒绝 Invalid。返回是否实际插入了内容。
 */
static bool xlineedit_insertText(XLineEdit* self, const char* utf8,
                                 bool userEdited)
{
    size_t textLen;
    size_t start;
    size_t end;
    size_t insertLen;
    size_t newLen;
    char* filtered;
    char* newText;
    if (!self || !self->m_text || !utf8 || !utf8[0]) return false;
    textLen = strlen(self->m_text);
    if (xlineedit_hasSelection(self)) {
        start = xlineedit_selStart(self);
        end = xlineedit_selEnd(self);
    } else {
        start = end = self->m_cursor;
    }
    filtered = (char*)XMalloc_System(strlen(utf8) + 1);
    if (!filtered) return false;
    xlineedit_filterInsert(self, start, utf8, filtered, strlen(utf8) + 1);
    insertLen = strlen(filtered);
    if (insertLen == 0) {
        XFree_System(filtered);
        return false;
    }
    /* maxLength 钳位（字符数；过滤后文本逐字符计数）。 */
    if (self->m_maxLength > 0) {
        size_t before = xlineedit_charCountPrefix(self->m_text, start);
        size_t after = xlineedit_charCount(self->m_text + end);
        size_t allowed;
        if (before + after >= (size_t)self->m_maxLength)
            allowed = 0;
        else
            allowed = (size_t)self->m_maxLength - before - after;
        if (allowed == 0) {
            XFree_System(filtered);
            return false;
        }
        if (xlineedit_charCount(filtered) > allowed) {
            size_t cut = 0;
            size_t chars = 0;
            while (cut < insertLen && chars < allowed) {
                ++cut;
                while (cut < insertLen &&
                       ((unsigned char)filtered[cut] & 0xC0u) == 0x80u)
                    ++cut;
                ++chars;
            }
            filtered[cut] = '\0';
            insertLen = cut;
        }
    }
    newLen = textLen - (end - start) + insertLen;
    newText = (char*)XMalloc_System(newLen + 1);
    if (!newText) {
        XFree_System(filtered);
        return false;
    }
    memcpy(newText, self->m_text, start);
    memcpy(newText + start, filtered, insertLen);
    memcpy(newText + start + insertLen, self->m_text + end,
           textLen - end + 1);
    newText[newLen] = '\0';
    /* 校验回调：Invalid 拒绝整个编辑。 */
    if (self->m_validator) {
        XLineEditValidatorState st =
            self->m_validator(self, newText, self->m_validatorUserData);
        if (st == XLineEditValidatorState_Invalid) {
            XFree_System(newText);
            XFree_System(filtered);
            return false;
        }
    }
    xlineedit_setContent(self, newText, start + insertLen, userEdited,
                         false, true);
    XFree_System(newText);
    XFree_System(filtered);
    return true;
}

/** @brief 删除 [from,to) 字节区间（视为用户编辑）。 */
static void xlineedit_eraseRange(XLineEdit* self, size_t from, size_t to)
{
    size_t textLen;
    char* newText;
    if (!self || !self->m_text || from >= to) return;
    textLen = strlen(self->m_text);
    if (to > textLen) to = textLen;
    newText = (char*)XMalloc_System(textLen - (to - from) + 1);
    if (!newText) return;
    memcpy(newText, self->m_text, from);
    memcpy(newText + from, self->m_text + to, textLen - to + 1);
    xlineedit_setContent(self, newText, from, true, false, true);
    XFree_System(newText);
}

/* ==================== 剪贴板 ==================== */

/** @brief 把文本写入剪贴板；剪贴板不可用时回退内部缓冲。 */
static void xlineedit_setClipboardText(XLineEdit* self, const char* text)
{
    if (!self || !text) return;
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        XClipboard* cb = XGuiApplication_clipboard();
        if (cb) {
            XString* str = XString_create_utf8(text);
            if (str) {
                XClipboard_setText(cb, str, XClipboardMode_Clipboard);
                XString_delete_base(str);
            }
            return;
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    {
        char* updated =
            (char*)XRealloc_System(self->m_clipboardText, strlen(text) + 1);
        if (!updated) return;
        self->m_clipboardText = updated;
        strcpy(self->m_clipboardText, text);
    }
}

/** @brief 读取剪贴板文本（新建堆拷贝，调用方 XFree_System）；无文本返回 NULL。 */
static char* xlineedit_getClipboardText(XLineEdit* self)
{
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        XClipboard* cb = XGuiApplication_clipboard();
        if (cb) {
            XString* str = XClipboard_text(cb, XClipboardMode_Clipboard);
            if (str) {
                const char* utf8 = XString_toUtf8(str);
                size_t len = XString_toUtf8_length(str);
                char* out = (char*)XMalloc_System(len + 1);
                if (out) {
                    memcpy(out, utf8, len);
                    out[len] = '\0';
                }
                XString_delete_base(str);
                return out;
            }
            return NULL;
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    if (self && self->m_clipboardText) {
        size_t len = strlen(self->m_clipboardText);
        char* out = (char*)XMalloc_System(len + 1);
        if (out) strcpy(out, self->m_clipboardText);
        return out;
    }
    return NULL;
}

/* ==================== 尺寸提示 ==================== */

/** @brief 同步估算尺寸到 XWidget 尺寸提示存储并请求重布局。 */
static void xlineedit_updateSizeHints(XLineEdit* self)
{
    XSize hint;
    XSize min;
    if (!self) return;
    hint = XLineEdit_sizeHint(self);
    min = XLineEdit_minimumSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &min);
    XWidget_updateGeometry((XWidget*)self);
}

/* ==================== 键盘处理 ==================== */

/** @brief 键盘按下：编辑/移动/回车/快捷键（readOnly 仅允许移动/选择/复制）。 */
static void VXLineEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XKeyEvent* ke;
    int key;
    XKeyboardModifiers mods;
    bool ctrl;
    bool shift;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    mods = ke->m_modifiers;
    ctrl = (mods & XKeyboardModifier_ControlModifier) != 0;
    shift = (mods & XKeyboardModifier_ShiftModifier) != 0;

    /* 编辑类快捷键（readOnly 时仅允许全选与复制）。 */
    if (ctrl && ((key >= 'a' && key <= 'z') ||
                 (key >= 'A' && key <= 'Z'))) {
        switch (key) {
        case 'a':
        case 'A':
            XLineEdit_selectAll(edit);
            return;
        case 'c':
        case 'C':
            XLineEdit_copy(edit);
            return;
        case 'x':
        case 'X':
            if (!edit->m_readOnly) XLineEdit_cut(edit);
            return;
        case 'v':
        case 'V':
            if (!edit->m_readOnly) XLineEdit_paste(edit);
            return;
        case 'z':
        case 'Z':
            if (!edit->m_readOnly) XLineEdit_undo(edit);
            return;
        case 'y':
        case 'Y':
            if (!edit->m_readOnly) XLineEdit_redo(edit);
            return;
        default:
            break;
        }
        XEvent_ignore(event);
        return;
    }

    switch (key) {
    case XKey_Backspace:
        if (edit->m_readOnly) { XEvent_ignore(event); return; }
        XLineEdit_backspace(edit);
        return;
    case XKey_Delete:
        if (edit->m_readOnly) { XEvent_ignore(event); return; }
        XLineEdit_del(edit);
        return;
    case XKey_Left:
        if (ctrl)
            XLineEdit_cursorWordBackward(edit, shift);
        else
            XLineEdit_cursorBackward(edit, shift, 1);
        return;
    case XKey_Right:
        if (ctrl)
            XLineEdit_cursorWordForward(edit, shift);
        else
            XLineEdit_cursorForward(edit, shift, 1);
        return;
    case XKey_Home:
        XLineEdit_home(edit, shift);
        return;
    case XKey_End:
        XLineEdit_end(edit, shift);
        return;
    case XKey_Return:
    case XKey_Enter:
        xlineedit_emitVoidSignal(edit,
                                 (size_t)XLineEdit_returnPressed_signal);
        xlineedit_emitVoidSignal(edit,
                                 (size_t)XLineEdit_editingFinished_signal);
        edit->m_finishedPending = false;
        return;
    default:
        break;
    }

    if (key >= 0x20 && key <= 0x7e && !ke->m_autoRepeat) {
        char ch[2];
        if (edit->m_readOnly) { XEvent_ignore(event); return; }
        ch[0] = (char)key;
        ch[1] = '\0';
        if (!xlineedit_insertText(edit, ch, true))
            xlineedit_emitVoidSignal(edit,
                                     (size_t)XLineEdit_inputRejected_signal);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 键盘释放：默认忽略（自动重复/修饰键行为为后续扩展）。 */
static void VXLineEdit_keyReleaseEvent(XWidget* self, XEvent* event)
{
    if (!self || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) return;
    XEvent_ignore(event);
}

/**
 * @brief      输入法事件：提交文本插入光标处（对标 QWidget::
 *             inputMethodEvent 的 commitString 处理；preedit 组合
 *             文本第一版不支持，确认后整串插入）。
 * @param      self  编辑框对象。
 * @param      event 输入法事件（m_commitString 为已确认文本）。
 * @return     无返回值。
 */
#if XMENU_ON
/** @brief 上下文菜单事件：创建标准菜单并弹出到事件全局坐标（对标
 *         QLineEdit::contextMenuEvent 的 createStandardContextMenu +
 *         popup；关闭即删对齐 WA_DeleteOnClose）。 */
static void VXLineEdit_contextMenuEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XContextMenuEvent* ctx;
    XMenu* menu;
    XPoint global;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_CONTEXT_MENU) return;
    ctx = (XContextMenuEvent*)event;
    menu = XLineEdit_createStandardContextMenu(edit);
    if (!menu) return;
    global = XContextMenuEvent_globalPosition(ctx);
    /* 对标 Qt：popup 非阻塞；关闭后由 DeleteOnClose 属性自删，
       与 Qt 菜单的 WA_DeleteOnClose 语义一致。 */
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &global);
    XEvent_accept(event);
}
#endif /* XMENU_ON */

static void VXLineEdit_inputMethodEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XInputMethodEvent* ime;
    const XString* commit;
    const char* utf8;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_INPUT_METHOD) return;
    ime = (XInputMethodEvent*)event;
    commit = ime->m_commitString; /* 直接字段访问（事件拥有，借用）。 */
    if (!commit) return;
    XLineEdit_insert(edit, XString_toUtf8(commit));
    XEvent_accept(event);
}

/* ==================== 鼠标处理 ==================== */

/** @brief 左键按下：获得焦点；命中清除按钮则清空文本；否则把光标定位到
 *         点击处（Shift+点击扩展选区）。 */
static void VXLineEdit_mousePressEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XMouseEvent* me;
    XPoint pos;
    bool shift;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    XWidget_setFocusPolicy(self, XWidgetFocusPolicy_ClickFocus);
    XWidget_setFocus(self);
    g_focusedLineEdit = self;
    pos = XMouseEvent_position(me);
    /* 内置 action 命中：触发 action 后返回（不移动光标）。 */
    {
        int actionIdx = xlineedit_hitAction(edit, pos.x);
        if (actionIdx >= 0 && edit->m_actions[actionIdx]) {
            XAction_trigger(edit->m_actions[actionIdx]);
            XEvent_accept(event);
            return;
        }
    }
    /* 清除按钮命中：点击清除文本（视为用户编辑）。 */
    if (edit->m_clearButtonEnabled && !edit->m_readOnly &&
        edit->m_text && edit->m_text[0] &&
        pos.x >= edit->m_clearButtonRect.x &&
        pos.x < edit->m_clearButtonRect.x + edit->m_clearButtonRect.width &&
        pos.y >= edit->m_clearButtonRect.y &&
        pos.y < edit->m_clearButtonRect.y + edit->m_clearButtonRect.height) {
        xlineedit_setContent(edit, "", 0, true, false, true);
        XEvent_accept(event);
        return;
    }
    shift = (me->m_modifiers & XKeyboardModifier_ShiftModifier) != 0;
    xlineedit_moveCursor(edit, xlineedit_posToCursor(edit, pos.x), shift);
    XEvent_accept(event);
}

/** @brief 鼠标双击：全选（对标 Qt 双击选词的简化行为）。 */
static void VXLineEdit_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    XLineEdit_selectAll(edit);
    XEvent_accept(event);
}

/* ==================== 焦点处理 ==================== */

/** @brief 获得焦点：刷新回显（PasswordEchoOnEdit）并重绘。 */
static void VXLineEdit_focusInEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_IN) return;
    xlineedit_refreshDisplay(edit);
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 失去焦点：自上次发射后用户编辑过则提交 editingFinished；刷新
 *         回显（PasswordEchoOnEdit 切回密码）并重绘。 */
static void VXLineEdit_focusOutEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_OUT) return;
    if (edit->m_finishedPending) {
        edit->m_finishedPending = false;
        xlineedit_emitVoidSignal(edit,
                                 (size_t)XLineEdit_editingFinished_signal);
    }
    xlineedit_refreshDisplay(edit);
    XWidget_update(self);
    XEvent_accept(event);
}

/* ==================== 绘制 ==================== */

/** @brief 绘制边框 + 文本/占位 + 选区高亮 + 清除按钮 + 光标（焦点内）。 */
static void VXLineEdit_paintEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    uint32_t text;
    uint32_t mid;
    uint32_t highlight;
    uint32_t highlightedText;
    const char* display;
    int tx;
    int ty;
    int baseline = 0;
    int editBaseline = 0;
    int lineH = 14;
    size_t selStart = 0;
    size_t selEnd = 0;
    bool hasSel;

    if (!edit || r.width <= 2 || r.height <= 2) return;
    r.x = 0; r.y = 0;
    base  = xlineedit_color(edit, XPaletteColorRole_Base);
    dark  = xlineedit_color(edit, XPaletteColorRole_Dark);
    light = xlineedit_color(edit, XPaletteColorRole_Light);
    text  = xlineedit_color(edit, XPaletteColorRole_Text);
    mid   = xlineedit_color(edit, XPaletteColorRole_Mid);
    highlight       = xlineedit_color(edit, XPaletteColorRole_Highlight);
    highlightedText = xlineedit_color(edit, XPaletteColorRole_HighlightedText);

    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 绘制字体 = 控件字体（与光标/点击的文本度量同一来源，否则
       估算与渲染错位随输入增长）。 */
    {
        XFont paintFont = XWidget_font(self);
        XPainter_setFont(&painter, &paintFont);
    }

    if (edit->m_frame) {
        XRect e = r;
        e.height = 1;
        XPainter_fillRect(&painter, &e, dark);
        e = r; e.width = 1;
        XPainter_fillRect(&painter, &e, dark);
        e = r; e.x = r.x + r.width - 1; e.width = 1;
        XPainter_fillRect(&painter, &e, light);
        e = r; e.y = r.y + r.height - 1; e.height = 1;
        XPainter_fillRect(&painter, &e, light);
    }

    xlineedit_refreshDisplay(edit);
    display = (edit->m_displayBuf) ? edit->m_displayBuf : "";
    tx = xlineedit_textStartX(edit);
    {
        XFont font = XWidget_font(self);
        int ascent = XPainter_textAscent(&font);
        int descent = XPainter_textDescent(&font);
        lineH = ascent + descent; /* 字形盒高（不含行距）。 */
        if (lineH < 14) lineH = 14;
        /* 行盒垂直居中：ty 为行顶部；绘制基线 = 顶部 + 上伸高度。 */
        ty = (r.height - lineH) / 2;
        baseline = ty + ascent;
        editBaseline = 1;
    }
    if (!editBaseline)
        baseline = ty + 12; /* 回退：无字体时的固定基线偏移 */
    hasSel = xlineedit_hasSelection(edit);
    if (hasSel) {
        selStart = xlineedit_selStart(edit);
        selEnd = xlineedit_selEnd(edit);
    }

    if (edit->m_text && edit->m_text[0]) {
        int baseX = tx - edit->m_viewOffset;
        XFont font = XWidget_font(self);
        if (hasSel) {
            char* seg0;
            char* seg1;
            char* seg2;
            size_t c0 = 0;
            size_t c1 = 0;
            size_t c2 = 0;
            size_t displayLen = strlen(display);
            seg0 = (char*)XMalloc_System(displayLen + 1);
            seg1 = (char*)XMalloc_System(displayLen + 1);
            seg2 = (char*)XMalloc_System(displayLen + 1);
            if (seg0 && seg1 && seg2) {
                xlineedit_splitDisplay(edit, selStart, selEnd,
                                       seg0, seg1, seg2, &c0, &c1, &c2);
                if (c1 > 0) {
                    /* 选区/分段定位用真实字体宽度（display 逐字符累计），
                       中文双宽与西文单宽都与 drawText 渲染对齐。 */
                    int selX = baseX + xlineedit_displayWidth(&font, display, c0);
                    int selW = xlineedit_displayWidth(&font, display, c0 + c1) -
                               xlineedit_displayWidth(&font, display, c0);
                    XRect selRect;
                    selRect.x = selX;
                    selRect.y = ty + 1;
                    selRect.width = selW;
                    selRect.height = r.height - 2;
                    XPainter_fillRect(&painter, &selRect, highlight);
                }
                if (seg0[0])
                    XPainter_drawText(&painter, baseX, baseline, seg0, text);
                if (seg1[0])
                    XPainter_drawText(&painter,
                                      baseX + xlineedit_displayWidth(&font,
                                          display, c0),
                                      baseline, seg1, highlightedText);
                if (seg2[0])
                    XPainter_drawText(&painter,
                                      baseX + xlineedit_displayWidth(&font,
                                          display, c0 + c1),
                                      baseline, seg2, text);
            }
            XFree_System(seg0);
            XFree_System(seg1);
            XFree_System(seg2);
        } else {
            XPainter_drawText(&painter, baseX, baseline, display, text);
        }
        /* 光标：按前缀真实文本宽度定位（与绘制同字体，焦点内常显）。
           K = 光标前对应的显示字符数；displayWidth 按 display 逐字符
           累计真实宽（Normal 下与按 m_cursor 字节偏移等价；Password
           掩码字符等宽也正确）。旧实现把字符数当字节偏移传
           textWidthRange，中文（3 字节/字符、双宽）随输入越来越多地
           落后于文字。 */
        if (XWidget_hasFocus(self)) {
            XFont font = XWidget_font(self);
            int cx = baseX;
            cx += xlineedit_displayWidth(
                &font, display,
                xlineedit_charCountPrefix(edit->m_text, edit->m_cursor));
            if (cx >= r.x + 1 && cx <= r.x + r.width - 1) {
                XRect cursor = { cx, ty, XLINEEDIT_CURSOR_W, lineH };
                XPainter_fillRect(&painter, &cursor, text);
            }
        }
    } else if (edit->m_placeholder[0]) {
        XPainter_drawText(&painter, tx - edit->m_viewOffset, baseline,
                          edit->m_placeholder, mid);
        if (XWidget_hasFocus(self)) {
            XRect cursor = { tx - edit->m_viewOffset, ty,
                             XLINEEDIT_CURSOR_W, lineH };
            XPainter_fillRect(&painter, &cursor, text);
        }
    } else if (XWidget_hasFocus(self)) {
        XRect cursor = { tx - edit->m_viewOffset, ty,
                         XLINEEDIT_CURSOR_W, lineH };
        XPainter_fillRect(&painter, &cursor, text);
    }

    /* 清除按钮（简笔 ×；点击清除由 mousePressEvent 处理）。 */
    if (edit->m_clearButtonEnabled && edit->m_text && edit->m_text[0]) {
        XRect btn;
        btn.width = 16;
        btn.height = 14;
        btn.x = r.x + r.width - (edit->m_frame ? 4 : 2) -
                edit->m_textMargins.right - btn.width;
        btn.y = (r.height - btn.height) / 2;
        edit->m_clearButtonRect = btn;
        XPainter_setPen(&painter, mid);
        XPainter_drawLine(&painter, btn.x + 3, btn.y + 3,
                          btn.x + btn.width - 4, btn.y + btn.height - 4);
        XPainter_drawLine(&painter, btn.x + btn.width - 4, btn.y + 3,
                          btn.x + 3, btn.y + btn.height - 4);
    } else {
        edit->m_clearButtonRect.x = 0;
        edit->m_clearButtonRect.y = 0;
        edit->m_clearButtonRect.width = 0;
        edit->m_clearButtonRect.height = 0;
    }
    /* 内置 action 图标区（末尾侧，绘制在清除按钮之后/文本之上）。 */
    xlineedit_paintActions(edit, &painter, text);

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 虚槽 ==================== */

static void VXLineEdit_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

/** @brief 反初始化：释放全部堆资源后调用父类 deinit。 */
static void VXLineEdit_deinit(XLineEdit* self)
{
    int i;
    if (!self) return;
    if (self->m_text) {
        XFree_System(self->m_text);
        self->m_text = NULL;
    }
    if (self->m_displayBuf) {
        XFree_System(self->m_displayBuf);
        self->m_displayBuf = NULL;
    }
    if (self->m_inputMask) {
        XFree_System(self->m_inputMask);
        self->m_inputMask = NULL;
    }
    if (self->m_clipboardText) {
        XFree_System(self->m_clipboardText);
        self->m_clipboardText = NULL;
    }
    for (i = 0; i < self->m_undoCount; ++i)
        XFree_System(self->m_undoStack[i]);
    for (i = 0; i < self->m_redoCount; ++i)
        XFree_System(self->m_redoStack[i]);
    self->m_undoCount = 0;
    self->m_redoCount = 0;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 深拷贝：基类深拷贝后复制文本与全部编辑字段（含撤销/重做栈）。 */
static void VXLineEdit_copy(XLineEdit* self, const XLineEdit* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLineEdit_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    XLineEdit_setText(self, other->m_text);
    memcpy(self->m_placeholder, other->m_placeholder,
           sizeof(self->m_placeholder));
    self->m_cursor = other->m_cursor;
    self->m_anchor = other->m_anchor;
    self->m_viewOffset = other->m_viewOffset;
    self->m_maxLength = other->m_maxLength;
    self->m_echoMode = other->m_echoMode;
    self->m_readOnly = other->m_readOnly;
    self->m_frame = other->m_frame;
    self->m_alignment = other->m_alignment;
    self->m_clearButtonEnabled = other->m_clearButtonEnabled;
    self->m_dragEnabled = other->m_dragEnabled;
    self->m_cursorMoveStyle = other->m_cursorMoveStyle;
    self->m_textMargins = other->m_textMargins;
    self->m_modified = other->m_modified;
    self->m_finishedPending = other->m_finishedPending;
    self->m_validator = other->m_validator;
    self->m_validatorUserData = other->m_validatorUserData;
    if (other->m_inputMask)
        XLineEdit_setInputMask(self, other->m_inputMask);
    /* 撤销/重做栈：深拷贝（setText 已清空本对象历史）。 */
    for (i = 0; i < other->m_undoCount; ++i) {
        char* snap =
            (char*)XMalloc_System(strlen(other->m_undoStack[i]) + 1);
        if (!snap) break;
        strcpy(snap, other->m_undoStack[i]);
        self->m_undoStack[self->m_undoCount++] = snap;
    }
    for (i = 0; i < other->m_redoCount; ++i) {
        char* snap =
            (char*)XMalloc_System(strlen(other->m_redoStack[i]) + 1);
        if (!snap) break;
        strcpy(snap, other->m_redoStack[i]);
        self->m_redoStack[self->m_redoCount++] = snap;
    }
    if (other->m_clipboardText) {
        char* cb = (char*)XMalloc_System(strlen(other->m_clipboardText) + 1);
        if (cb) {
            strcpy(cb, other->m_clipboardText);
            self->m_clipboardText = cb;
        }
    }
    self->m_clearButtonRect = other->m_clearButtonRect;
}

/** @brief 移动语义：基类移动后转移全部缓冲，源对象归构造默认值。 */
static void VXLineEdit_move(XLineEdit* self, XLineEdit* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLineEdit_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    /* 转移文本缓冲。 */
    self->m_text = other->m_text;
    other->m_text = NULL;
    if (!self->m_text) {
        self->m_text = (char*)XMalloc_System(1);
        if (self->m_text) self->m_text[0] = '\0';
    }
    /* 转移显示/掩码/剪贴板缓冲与撤销重做栈。 */
    self->m_displayBuf = other->m_displayBuf;
    other->m_displayBuf = NULL;
    self->m_inputMask = other->m_inputMask;
    other->m_inputMask = NULL;
    self->m_clipboardText = other->m_clipboardText;
    other->m_clipboardText = NULL;
    for (i = 0; i < other->m_undoCount; ++i) {
        self->m_undoStack[i] = other->m_undoStack[i];
        other->m_undoStack[i] = NULL;
    }
    self->m_undoCount = other->m_undoCount;
    other->m_undoCount = 0;
    for (i = 0; i < other->m_redoCount; ++i) {
        self->m_redoStack[i] = other->m_redoStack[i];
        other->m_redoStack[i] = NULL;
    }
    self->m_redoCount = other->m_redoCount;
    other->m_redoCount = 0;

    memcpy(self->m_placeholder, other->m_placeholder,
           sizeof(self->m_placeholder));
    self->m_cursor = other->m_cursor;
    self->m_anchor = other->m_anchor;
    self->m_viewOffset = other->m_viewOffset;
    self->m_maxLength = other->m_maxLength;
    self->m_echoMode = other->m_echoMode;
    self->m_readOnly = other->m_readOnly;
    self->m_frame = other->m_frame;
    self->m_alignment = other->m_alignment;
    self->m_clearButtonEnabled = other->m_clearButtonEnabled;
    self->m_dragEnabled = other->m_dragEnabled;
    self->m_cursorMoveStyle = other->m_cursorMoveStyle;
    self->m_textMargins = other->m_textMargins;
    self->m_modified = other->m_modified;
    self->m_finishedPending = other->m_finishedPending;
    self->m_validator = other->m_validator;
    self->m_validatorUserData = other->m_validatorUserData;
    self->m_clearButtonRect = other->m_clearButtonRect;

    /* 源对象归构造默认值。 */
    other->m_placeholder[0] = '\0';
    other->m_cursor = 0;
    other->m_anchor = 0;
    other->m_viewOffset = 0;
    other->m_maxLength = 0;
    other->m_echoMode = XLineEditEchoMode_Normal;
    other->m_readOnly = false;
    other->m_frame = true;
    other->m_alignment = XAlignment_Left;
    other->m_clearButtonEnabled = false;
    other->m_dragEnabled = false;
    other->m_cursorMoveStyle = XLineEditCursorMoveStyle_LogicalMoveStyle;
    XMargins_init(&other->m_textMargins, 0, 0, 0, 0);
    other->m_modified = false;
    other->m_finishedPending = false;
    other->m_validator = NULL;
    other->m_validatorUserData = NULL;
    XRect_init(&other->m_clearButtonRect, 0, 0, 0, 0);

    xlineedit_updateSizeHints(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 生命周期 ==================== */

XVtable* XLineEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLineEdit)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXLineEdit_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodEvent, VXLineEdit_inputMethodEvent);
#if XMENU_ON
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent,
                             VXLineEdit_contextMenuEvent);
#endif /* XMENU_ON */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent,
                             VXLineEdit_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXLineEdit_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXLineEdit_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXLineEdit_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VXLineEdit_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXLineEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXLineEdit_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXLineEdit_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXLineEdit_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLineEdit_deinit);

    return XVTABLE_DEFAULT;
}

void XLineEdit_init(XLineEdit* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XLineEdit);

    self->m_text = (char*)XMalloc_System(1);
    if (self->m_text) self->m_text[0] = '\0';
    self->m_placeholder[0] = '\0';
    self->m_cursor = 0;
    self->m_anchor = 0;
    self->m_viewOffset = 0;
    self->m_maxLength = 0;
    self->m_echoMode = XLineEditEchoMode_Normal;
    self->m_readOnly = false;
    self->m_frame = true;
    self->m_alignment = XAlignment_Left;
    self->m_displayBuf = NULL;
    self->m_inputMask = NULL;
    self->m_validator = NULL;
    self->m_validatorUserData = NULL;
    self->m_clearButtonEnabled = false;
    self->m_dragEnabled = false;
    self->m_cursorMoveStyle = XLineEditCursorMoveStyle_LogicalMoveStyle;
    XMargins_init(&self->m_textMargins, 0, 0, 0, 0);
    self->m_modified = false;
    self->m_finishedPending = false;
    self->m_undoCount = 0;
    self->m_redoCount = 0;
    self->m_clipboardText = NULL;
    /* 内置 action 槽：结构体成员数组，XWidget_init 只清基类部分；不初始化
       的话 m_actionCount 为堆残留垃圾，首帧绘制会解引用野指针（Debug CRT
       cdcd 填充模式直接暴露）。撤销/重做栈同为成员指针数组，一并清零。 */
    self->m_actionCount = 0;
    memset(self->m_actions, 0, sizeof(self->m_actions));
    memset(self->m_actionPositions, 0, sizeof(self->m_actionPositions));
    memset(self->m_undoStack, 0, sizeof(self->m_undoStack));
    memset(self->m_redoStack, 0, sizeof(self->m_redoStack));
    XRect_init(&self->m_clearButtonRect, 0, 0, 0, 0);
    XWidget_setFocusPolicy(self, XWidgetFocusPolicy_ClickFocus);
    xlineedit_updateSizeHints(self);
}

XLineEdit* XLineEdit_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XLineEdit* self = (XLineEdit*)XMemory_malloc(sizeof(XLineEdit), memory);
    if (!self) return NULL;
    XLineEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本 API ==================== */

const char* XLineEdit_text(const XLineEdit* self)
{
    return (self && self->m_text) ? self->m_text : "";
}

const char* XLineEdit_displayText(const XLineEdit* self)
{
    XLineEdit* e = (XLineEdit*)self;
    if (!e) return "";
    xlineedit_refreshDisplay(e);
    return (e->m_displayBuf) ? e->m_displayBuf : "";
}

void XLineEdit_setText(XLineEdit* self, const char* text)
{
    if (!self || !text) return;
    if (self->m_text && strcmp(self->m_text, text) == 0) return;
    xlineedit_setContent(self, text, strlen(text), false, true, true);
}

void XLineEdit_clear(XLineEdit* self)
{
    XLineEdit_setText(self, "");
}

void XLineEdit_insert(XLineEdit* self, const char* utf8)
{
    if (!self || !utf8) return;
    if (self->m_readOnly) return;
    xlineedit_insertText(self, utf8, true);
}

const char* XLineEdit_placeholderText(const XLineEdit* self)
{
    return (self && self->m_placeholder[0]) ? self->m_placeholder : "";
}

void XLineEdit_setPlaceholderText(XLineEdit* self, const char* placeholder)
{
    if (!self) return;
    if (!placeholder) placeholder = "";
    strncpy(self->m_placeholder, placeholder,
            sizeof(self->m_placeholder) - 1);
    self->m_placeholder[sizeof(self->m_placeholder) - 1] = '\0';
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

/* ==================== 编辑属性 ==================== */

bool XLineEdit_isReadOnly(const XLineEdit* self)
{
    return self ? self->m_readOnly : false;
}
void XLineEdit_setReadOnly(XLineEdit* self, bool readOnly)
{
    if (!self) return;
    self->m_readOnly = readOnly;
}
int XLineEdit_echoMode(const XLineEdit* self)
{
    return self ? self->m_echoMode : XLineEditEchoMode_Normal;
}
void XLineEdit_setEchoMode(XLineEdit* self, int echoMode)
{
    if (!self || self->m_echoMode == echoMode) return;
    if (echoMode < XLineEditEchoMode_Normal ||
        echoMode > XLineEditEchoMode_PasswordEchoOnEdit)
        return;
    self->m_echoMode = echoMode;
    /* Qt 语义：切换回显模式清除选区并把光标移到末尾。 */
    self->m_anchor = self->m_cursor =
        self->m_text ? strlen(self->m_text) : 0;
    xlineedit_refreshDisplay(self);
    XWidget_update((XWidget*)self);
}
int XLineEdit_maxLength(const XLineEdit* self)
{
    return self ? self->m_maxLength : 0;
}
void XLineEdit_setMaxLength(XLineEdit* self, int maxLength)
{
    if (!self || maxLength < 0) return;
    self->m_maxLength = maxLength;
    if (maxLength > 0 && self->m_text &&
        (int)xlineedit_charCount(self->m_text) > maxLength) {
        /* 截断到第 maxLength 个字符边界。 */
        size_t cut = 0;
        int chars = 0;
        const char* p = self->m_text;
        char* truncated;
        while (*p) {
            ++p;
            if ((*p & 0xC0u) != 0x80u) ++chars;
            if (chars == maxLength) { cut = (size_t)(p - self->m_text); break; }
        }
        if (cut) {
            truncated = (char*)XMalloc_System(cut + 1);
            if (truncated) {
                memcpy(truncated, self->m_text, cut);
                truncated[cut] = '\0';
                xlineedit_setContent(self, truncated, cut, false, false, true);
                XFree_System(truncated);
            }
        }
    }
}
int XLineEdit_alignment(const XLineEdit* self)
{
    return self ? self->m_alignment : XAlignment_Left;
}
void XLineEdit_setAlignment(XLineEdit* self, int alignment)
{
    if (!self || self->m_alignment == alignment) return;
    self->m_alignment = alignment;
    XWidget_update((XWidget*)self);
}
bool XLineEdit_hasFrame(const XLineEdit* self)
{
    return self ? self->m_frame : true;
}
void XLineEdit_setFrame(XLineEdit* self, bool on)
{
    if (!self || self->m_frame == on) return;
    self->m_frame = on;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}
void XLineEdit_addAction(XLineEdit* self, XAction* action, int position)
{
    if (!self || !action) return;
    if (position != XLineEditActionPosition_Leading &&
        position != XLineEditActionPosition_Trailing)
        return;
    if (self->m_actionCount >= XLINEEDIT_MAX_ACTIONS) return;
    self->m_actions[self->m_actionCount] = action;
    self->m_actionPositions[self->m_actionCount] = (uint8_t)position;
    ++self->m_actionCount;
    XWidget_update((XWidget*)self);
}

bool XLineEdit_isClearButtonEnabled(const XLineEdit* self)
{
    return self ? self->m_clearButtonEnabled : false;
}
void XLineEdit_setClearButtonEnabled(XLineEdit* self, bool enable)
{
    if (!self || self->m_clearButtonEnabled == enable) return;
    self->m_clearButtonEnabled = enable;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}
void XLineEdit_setValidator(XLineEdit* self, XLineEditValidatorFunc validator,
                            void* userData)
{
    if (!self) return;
    self->m_validator = validator;
    self->m_validatorUserData = userData;
}
XLineEditValidatorFunc XLineEdit_validator(const XLineEdit* self)
{
    return self ? self->m_validator : NULL;
}
XSize XLineEdit_sizeHint(const XLineEdit* self)
{
    XSize s;
    size_t chars = 0;
    int w;
    int h;
    if (!self) {
        XSize_init(&s, 0, 0);
        return s;
    }
    if (self->m_text) chars = xlineedit_charCount(self->m_text);
    if (self->m_placeholder[0]) {
        size_t pc = xlineedit_charCount(self->m_placeholder);
        if (pc > chars) chars = pc;
    }
    {
        /* 首选宽度按真实字体度量（取文本与 placeholder 中较宽者），
           中文双宽不再被按 8px 低估。 */
        XFont font = XWidget_font((const XWidget*)self);
        const char* textPtr = self->m_text ? self->m_text : "";
        const char* phPtr = self->m_placeholder;
        int textW = xlineedit_displayWidth(&font, textPtr, chars);
        int phW = self->m_placeholder[0]
                      ? xlineedit_displayWidth(&font, phPtr,
                                               xlineedit_charCount(phPtr))
                      : 0;
        int contentW = textW > phW ? textW : phW;
        w = (int)((self->m_frame ? 8 : 4) + self->m_textMargins.left +
                  self->m_textMargins.right + contentW +
                  ((self->m_clearButtonEnabled) ? 18 : 0));
    }
    if (w < 40) w = 40;
    h = 14 + self->m_textMargins.top + self->m_textMargins.bottom +
        (self->m_frame ? 4 : 2);
    XSize_init(&s, w, h);
    return s;
}
XSize XLineEdit_minimumSizeHint(const XLineEdit* self)
{
    XSize s;
    int w;
    int h;
    if (!self) {
        XSize_init(&s, 0, 0);
        return s;
    }
    w = (int)((self->m_frame ? 8 : 4) + self->m_textMargins.left +
              self->m_textMargins.right + XLINEEDIT_CHAR_W +
              ((self->m_clearButtonEnabled) ? 18 : 0));
    if (w < 24) w = 24;
    h = 14 + self->m_textMargins.top + self->m_textMargins.bottom +
        (self->m_frame ? 4 : 2);
    XSize_init(&s, w, h);
    return s;
}
XRect XLineEdit_cursorRect(const XLineEdit* self)
{
    XRect rect;
    int tx;
    int ty;
    int baseline = 0;
    int editBaseline = 0;    int cx;
    memset(&rect, 0, sizeof(rect));
    if (!self) return rect;
    {
        XFont font = XWidget_font((XWidget*)self);
        int lineH = XPainter_textHeight(&font);
        int ascent = XPainter_textAscent(&font);
        if (lineH < 14) lineH = 14;
        ty = (XWidget_height((XWidget*)self) - lineH) / 2;
        tx = xlineedit_textStartX(self);
        cx = tx - self->m_viewOffset;
        if (self->m_text) {
            /* m_cursor 是 UTF-8 字节偏移，textWidthRange 的区间参数也是
               字节偏移：直接传 m_cursor。旧实现把「字符数」当「字节偏移」
               传入，中文（3 字节/字符、双宽字形）下光标随输入越来越多地
               落后于文字。 */
            cx += XPainter_textWidthRange(&font, self->m_text, 0,
                                          (int)self->m_cursor);
        }
        rect.x = cx;
        rect.y = ty + 1;
        rect.width = XLINEEDIT_CURSOR_W;
        rect.height = lineH - 2;
        (void)ascent;
    }
    return rect;
}

int XLineEdit_cursorPosition(const XLineEdit* self)
{
    return self ? (int)self->m_cursor : 0;
}
void XLineEdit_setCursorPosition(XLineEdit* self, int position)
{
    size_t pos;
    if (!self || !self->m_text) return;
    if (position < 0) position = 0;
    if ((size_t)position > strlen(self->m_text))
        position = (int)strlen(self->m_text);
    pos = xlineedit_prevBoundary(self->m_text, (size_t)position);
    xlineedit_moveCursor(self, pos, false);
}
int XLineEdit_cursorPositionAt(const XLineEdit* self, const XPoint* pos)
{
    if (!self) return 0;
    return (int)xlineedit_posToCursor(self, pos ? pos->x : 0);
}

/* ==================== 光标移动与编辑键 ==================== */

void XLineEdit_cursorForward(XLineEdit* self, bool mark, int steps)
{
    size_t pos;
    int i;
    if (!self || !self->m_text) return;
    pos = self->m_cursor;
    if (steps < 0) {
        for (i = 0; i < -steps; ++i)
            pos = xlineedit_prevBoundary(self->m_text, pos);
    } else {
        for (i = 0; i < steps; ++i)
            pos = xlineedit_nextBoundary(self->m_text, pos);
    }
    xlineedit_moveCursor(self, pos, mark);
}

void XLineEdit_cursorBackward(XLineEdit* self, bool mark, int steps)
{
    size_t pos;
    int i;
    if (!self || !self->m_text) return;
    pos = self->m_cursor;
    if (steps < 0) {
        for (i = 0; i < -steps; ++i)
            pos = xlineedit_nextBoundary(self->m_text, pos);
    } else {
        for (i = 0; i < steps; ++i)
            pos = xlineedit_prevBoundary(self->m_text, pos);
    }
    xlineedit_moveCursor(self, pos, mark);
}

void XLineEdit_cursorWordForward(XLineEdit* self, bool mark)
{
    size_t pos;
    if (!self || !self->m_text) return;
    pos = self->m_cursor;
    /* 跳过空白，再跳过单词（词=连续非空白）。 */
    while (self->m_text[pos] &&
           xlineedit_isSpace((unsigned char)self->m_text[pos]))
        pos = xlineedit_nextBoundary(self->m_text, pos);
    while (self->m_text[pos] &&
           !xlineedit_isSpace((unsigned char)self->m_text[pos]))
        pos = xlineedit_nextBoundary(self->m_text, pos);
    xlineedit_moveCursor(self, pos, mark);
}

void XLineEdit_cursorWordBackward(XLineEdit* self, bool mark)
{
    size_t pos;
    if (!self || !self->m_text) return;
    pos = self->m_cursor;
    /* 反向跳过空白，再反向跳过单词。 */
    while (pos > 0 &&
           xlineedit_isSpace((unsigned char)self->m_text[
               xlineedit_prevBoundary(self->m_text, pos)]))
        pos = xlineedit_prevBoundary(self->m_text, pos);
    while (pos > 0 &&
           !xlineedit_isSpace((unsigned char)self->m_text[
               xlineedit_prevBoundary(self->m_text, pos)]))
        pos = xlineedit_prevBoundary(self->m_text, pos);
    xlineedit_moveCursor(self, pos, mark);
}

void XLineEdit_backspace(XLineEdit* self)
{
    size_t from;
    size_t to;
    if (!self || !self->m_text || self->m_readOnly) return;
    if (xlineedit_hasSelection(self)) {
        from = xlineedit_selStart(self);
        to = xlineedit_selEnd(self);
    } else {
        to = self->m_cursor;
        from = xlineedit_prevBoundary(self->m_text, to);
    }
    xlineedit_eraseRange(self, from, to);
}

void XLineEdit_del(XLineEdit* self)
{
    size_t from;
    size_t to;
    if (!self || !self->m_text || self->m_readOnly) return;
    if (xlineedit_hasSelection(self)) {
        from = xlineedit_selStart(self);
        to = xlineedit_selEnd(self);
    } else {
        from = self->m_cursor;
        to = xlineedit_nextBoundary(self->m_text, from);
    }
    xlineedit_eraseRange(self, from, to);
}

void XLineEdit_home(XLineEdit* self, bool mark)
{
    if (!self) return;
    xlineedit_moveCursor(self, 0, mark);
}

void XLineEdit_end(XLineEdit* self, bool mark)
{
    if (!self || !self->m_text) return;
    xlineedit_moveCursor(self, strlen(self->m_text), mark);
}

/* ==================== 修改状态 ==================== */

bool XLineEdit_isModified(const XLineEdit* self)
{
    return self ? self->m_modified : false;
}

void XLineEdit_setModified(XLineEdit* self, bool modified)
{
    if (self) self->m_modified = modified;
}

/* ==================== 选区 ==================== */

void XLineEdit_setSelection(XLineEdit* self, int start, int length)
{
    size_t s;
    size_t e;
    size_t i;
    size_t maxLen;
    if (!self || !self->m_text) return;
    maxLen = strlen(self->m_text);
    if (length < 0) {
        /* 负长度：选区向 start 左侧扩展。 */
        s = (start + length < 0) ? 0 : (size_t)(start + length);
        e = (start < 0) ? 0 : (size_t)start;
    } else {
        s = (start < 0) ? 0 : (size_t)start;
        e = s;
        for (i = 0; i < (size_t)length; ++i)
            e = xlineedit_nextBoundary(self->m_text, e);
    }
    if (s > maxLen) s = maxLen;
    if (e > maxLen) e = maxLen;
    xlineedit_setSelectionRange(self, s, e);
}

bool XLineEdit_hasSelectedText(const XLineEdit* self)
{
    return xlineedit_hasSelection(self);
}

char* XLineEdit_selectedText(const XLineEdit* self)
{
    size_t s;
    size_t e;
    size_t len;
    char* out;
    if (!self || !xlineedit_hasSelection(self)) return NULL;
    s = xlineedit_selStart(self);
    e = xlineedit_selEnd(self);
    len = e - s;
    out = (char*)XMalloc_System(len + 1);
    if (!out) return NULL;
    memcpy(out, self->m_text + s, len);
    out[len] = '\0';
    return out;
}

int XLineEdit_selectionStart(const XLineEdit* self)
{
    if (!self || !xlineedit_hasSelection(self)) return -1;
    return (int)xlineedit_selStart(self);
}

int XLineEdit_selectionEnd(const XLineEdit* self)
{
    if (!self || !xlineedit_hasSelection(self)) return -1;
    return (int)xlineedit_selEnd(self);
}

int XLineEdit_selectionLength(const XLineEdit* self)
{
    size_t startChars;
    size_t endChars;
    if (!self || !xlineedit_hasSelection(self)) return 0;
    startChars = xlineedit_charCountPrefix(self->m_text,
                                           xlineedit_selStart(self));
    endChars = xlineedit_charCountPrefix(self->m_text,
                                         xlineedit_selEnd(self));
    return (int)(endChars - startChars);
}

void XLineEdit_deselect(XLineEdit* self)
{
    if (!self) return;
    xlineedit_moveCursor(self, self->m_cursor, false);
}

void XLineEdit_selectAll(XLineEdit* self)
{
    if (!self || !self->m_text) return;
    xlineedit_setSelectionRange(self, 0, strlen(self->m_text));
}

/* ==================== 撤销/重做 ==================== */

bool XLineEdit_isUndoAvailable(const XLineEdit* self)
{
    return self ? self->m_undoCount > 0 : false;
}

bool XLineEdit_isRedoAvailable(const XLineEdit* self)
{
    return self ? self->m_redoCount > 0 : false;
}

void XLineEdit_undo(XLineEdit* self)
{
    char* snap;
    if (!self || self->m_undoCount == 0) return;
    snap = self->m_undoStack[--self->m_undoCount];
    xlineedit_redoPush(self);
    xlineedit_setContent(self, snap, strlen(snap), false, false, true);
    XFree_System(snap);
}

void XLineEdit_redo(XLineEdit* self)
{
    char* snap;
    if (!self || self->m_redoCount == 0) return;
    snap = self->m_redoStack[--self->m_redoCount];
    xlineedit_undoPush(self);
    xlineedit_setContent(self, snap, strlen(snap), false, false, true);
    XFree_System(snap);
}

/* ==================== 剪贴板 ==================== */

void XLineEdit_cut(XLineEdit* self)
{
    size_t s;
    size_t e;
    char* sel;
    if (!self || self->m_readOnly || !xlineedit_hasSelection(self)) return;
    s = xlineedit_selStart(self);
    e = xlineedit_selEnd(self);
    sel = (char*)XMalloc_System(e - s + 1);
    if (sel) {
        memcpy(sel, self->m_text + s, e - s);
        sel[e - s] = '\0';
        xlineedit_setClipboardText(self, sel);
        XFree_System(sel);
    }
    xlineedit_eraseRange(self, s, e);
}

void XLineEdit_copy(XLineEdit* self)
{
    size_t s;
    size_t e;
    char* sel;
    if (!self || !xlineedit_hasSelection(self)) return;
    s = xlineedit_selStart(self);
    e = xlineedit_selEnd(self);
    sel = (char*)XMalloc_System(e - s + 1);
    if (!sel) return;
    memcpy(sel, self->m_text + s, e - s);
    sel[e - s] = '\0';
    xlineedit_setClipboardText(self, sel);
    XFree_System(sel);
}

void XLineEdit_paste(XLineEdit* self)
{
    char* text;
    if (!self || self->m_readOnly) return;
    text = xlineedit_getClipboardText(self);
    if (!text) return;
    if (text[0] == '\0') {
        XFree_System(text);
        return;
    }
    if (!xlineedit_insertText(self, text, true)) {
        xlineedit_emitVoidSignal(self, (size_t)XLineEdit_inputRejected_signal);
    }
    XFree_System(text);
}

/* ==================== 其他属性 ==================== */

bool XLineEdit_dragEnabled(const XLineEdit* self)
{
    return self ? self->m_dragEnabled : false;
}

void XLineEdit_setDragEnabled(XLineEdit* self, bool b)
{
    if (self) self->m_dragEnabled = b;
}

int XLineEdit_cursorMoveStyle(const XLineEdit* self)
{
    return self ? self->m_cursorMoveStyle
                : XLineEditCursorMoveStyle_LogicalMoveStyle;
}

void XLineEdit_setCursorMoveStyle(XLineEdit* self, int style)
{
    if (!self) return;
    self->m_cursorMoveStyle = style;
}

const char* XLineEdit_inputMask(const XLineEdit* self)
{
    return (self && self->m_inputMask) ? self->m_inputMask : "";
}

void XLineEdit_setInputMask(XLineEdit* self, const char* inputMask)
{
    char* updated;
    if (!self) return;
    if (!inputMask) inputMask = "";
    if (self->m_inputMask && strcmp(self->m_inputMask, inputMask) == 0)
        return;
    updated = (char*)XRealloc_System(self->m_inputMask,
                                     strlen(inputMask) + 1);
    if (!updated) return;
    self->m_inputMask = updated;
    strcpy(self->m_inputMask, inputMask);
    xlineedit_refreshDisplay(self);
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

bool XLineEdit_hasAcceptableInput(const XLineEdit* self)
{
    XLineEditValidatorState state;
    if (!self || !self->m_text || !self->m_text[0]) return false;
    if (!xlineedit_maskSatisfied(self->m_inputMask, self->m_text))
        return false;
    if (self->m_validator) {
        state = self->m_validator((XLineEdit*)self, self->m_text,
                                  self->m_validatorUserData);
        if (state != XLineEditValidatorState_Acceptable) return false;
    }
    return true;
}

void XLineEdit_setTextMargins(XLineEdit* self, int left, int top,
                              int right, int bottom)
{
    if (!self) return;
    self->m_textMargins.left = left;
    self->m_textMargins.top = top;
    self->m_textMargins.right = right;
    self->m_textMargins.bottom = bottom;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

void XLineEdit_setTextMargins_2(XLineEdit* self, const XMargins* margins)
{
    if (!self) return;
    if (margins)
        self->m_textMargins = *margins;
    else
        XMargins_init(&self->m_textMargins, 0, 0, 0, 0);
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

XMargins XLineEdit_textMargins(const XLineEdit* self)
{
    XMargins m;
    XMargins_init(&m, 0, 0, 0, 0);
    if (self) m = self->m_textMargins;
    return m;
}

/* ==================== 信号 ==================== */

void* XLineEdit_textChanged_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_textChanged_signal;
}
void* XLineEdit_textEdited_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_textEdited_signal;
}
void* XLineEdit_cursorPositionChanged_signal(XLineEdit* self, int oldPos,
                                             int newPos)
{
    (void)self;
    (void)oldPos;
    (void)newPos;
    return (void*)(size_t)XLineEdit_cursorPositionChanged_signal;
}
void* XLineEdit_returnPressed_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_returnPressed_signal;
}
void* XLineEdit_editingFinished_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_editingFinished_signal;
}
void* XLineEdit_selectionChanged_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_selectionChanged_signal;
}
void* XLineEdit_inputRejected_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_inputRejected_signal;
}

XLineEdit* XLineEdit_focusedLineEdit(void)
{
    return g_focusedLineEdit;
}

#endif /* XWIDGET_ON && XLINEEDIT_ON */

#if XMENU_ON
/* ==================== 标准右键菜单（对标 QLineEdit::
   createStandardContextMenu / contextMenuEvent） ==================== */

/** @brief 菜单动作槽：撤销。 */
static void xlineedit_menuUndoSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_undo((XLineEdit*)receiver);
}

/** @brief 菜单动作槽：重做。 */
static void xlineedit_menuRedoSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_redo((XLineEdit*)receiver);
}

/** @brief 菜单动作槽：剪切。 */
static void xlineedit_menuCutSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_cut((XLineEdit*)receiver);
}

/** @brief 菜单动作槽：复制。 */
static void xlineedit_menuCopySlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_copy((XLineEdit*)receiver);
}

/** @brief 菜单动作槽：粘贴。 */
static void xlineedit_menuPasteSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_paste((XLineEdit*)receiver);
}

/** @brief 菜单动作槽：删除选中文本（对标
 *         QWidgetLineControl::_q_deleteSelected）。 */
static void xlineedit_menuDeleteSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_del((XLineEdit*)receiver);
}

/** @brief 菜单动作槽：全选。 */
static void xlineedit_menuSelectAllSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XLineEdit_selectAll((XLineEdit*)receiver);
}

/** @brief 添加菜单动作并连接触发槽（返回动作便于设置启用态）。 */
static XAction* xlineedit_addMenuAction(XMenu* menu, const char* utf8,
                                        XSlotFunc1 slot, XLineEdit* edit)
{
    XAction* action = XMenu_addAction_2(menu, utf8);
    if (action && slot)
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          (XObject*)edit, slot, XConnectionType_Direct);
    return action;
}

/** @brief 是否已全选（存在选区且覆盖全部文本；对标 allSelected）。 */
static bool xlineedit_allSelected(const XLineEdit* self)
{
    return xlineedit_hasSelection(self) &&
           xlineedit_selStart(self) == 0 &&
           xlineedit_selEnd(self) == strlen(self->m_text);
}

XMenu* XLineEdit_createStandardContextMenu(XLineEdit* self)
{
    XMenu* menu;
    XAction* action;
    XString* name;
    bool readOnly;
    bool hasSel;
    bool echoNormal;
    bool hasText;
    bool hasClip;
    char* clip;
    if (!self) return NULL;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!menu) return NULL;
    /* 对标 Qt：qt_edit_menu 对象名供测试与样式查找。 */
    name = XString_create_utf8("qt_edit_menu");
    if (name) {
        XObject_setObjectName((XObject*)menu, name);
        XString_delete_base((XClass*)name);
    }
    readOnly = self->m_readOnly;
    hasSel = xlineedit_hasSelection(self);
    echoNormal = XLineEdit_echoMode(self) == (int)XLineEditEchoMode_Normal;
    hasText = self->m_text[0] != '\0';
    clip = xlineedit_getClipboardText(self);
    hasClip = clip && clip[0] != '\0';
    if (clip) XFree_System(clip);

    if (!readOnly) {
        action = xlineedit_addMenuAction(menu, "撤销(&U)",
                                         xlineedit_menuUndoSlot, self);
        XAction_setEnabled(action, XLineEdit_isUndoAvailable(self));
        action = xlineedit_addMenuAction(menu, "重做(&R)",
                                         xlineedit_menuRedoSlot, self);
        XAction_setEnabled(action, XLineEdit_isRedoAvailable(self));
        XMenu_addSeparator(menu);
    }
    if (!readOnly) {
        action = xlineedit_addMenuAction(menu, "剪切(&T)",
                                         xlineedit_menuCutSlot, self);
        XAction_setEnabled(action, hasSel && echoNormal);
    }
    action = xlineedit_addMenuAction(menu, "复制(&C)",
                                     xlineedit_menuCopySlot, self);
    XAction_setEnabled(action, hasSel && echoNormal);
    if (!readOnly) {
        action = xlineedit_addMenuAction(menu, "粘贴(&P)",
                                         xlineedit_menuPasteSlot, self);
        XAction_setEnabled(action, hasClip);
        action = xlineedit_addMenuAction(menu, "删除",
                                         xlineedit_menuDeleteSlot, self);
        XAction_setEnabled(action, hasText && hasSel);
    }
    if (!XMenu_actions(menu) ||
        XVector_size_base(XMenu_actions(menu)) == 0)
        return menu;
    XMenu_addSeparator(menu);
    action = xlineedit_addMenuAction(menu, "全选(&A)",
                                     xlineedit_menuSelectAllSlot, self);
    XAction_setEnabled(action, hasText && !xlineedit_allSelected(self));
    return menu;
}
#endif /* XMENU_ON */
