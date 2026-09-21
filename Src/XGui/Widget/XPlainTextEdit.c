/**
 * @file       XPlainTextEdit.c
 * @brief      多行纯文本编辑控件壳实现（对标 Qt 6.8 QPlainTextEdit 全部
 *             公共 API；编辑能力面迁入 XTextControl 私有文本控制器）。
 * @details    与同名头文件的公共 API 一一对应。壳化拆分（对照
 *             docs/xgui-audit/2026-09-19/editor-delta-audit.md 第三章）：
 *             - 迁入控制器：行存储/文本读写/撤销重做/编辑原语/键盘分派/
 *               IME 提交/命中测试/光标矩形/查找/剪贴板/选区锚点模型/
 *               块数承载/块数字符统计/覆盖模式与交互标志语义/moveCursor
 *               分派/标准菜单数据源（16 项）；
 *             - 壳保留：滚动区机制与内容尺寸联动、视口背景/边框/占位
 *               绘制、光标绘制的调用入口、ensureCursorVisible 滚动条
 *               数学、焦点策略、行高度量口径回退、换行/字体/缩放属性、
 *               document 借用接口、updateRequest 信号、上下文菜单弹出、
 *               rich 子集扩展点、anchorAt 转发、documentTitle；
 *             - 事件入口统一换算视口→内容坐标后经
 *               XTextControl_processEvent 路由；坐标口径：内容 X = 视口
 *               X - 行左留白 2px + 水平滚动值，内容 Y = 视口 Y + 垂直
 *               滚动值（与绘制 translate(2, -scroll) 互逆）。
 *             实现细节见头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XPlainTextEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XClipboard.h"
#include "XGuiApplication.h"
#include "XTextUtf8.h"
#include "XTextClipboard.h"
#include "XTextDocument.h"
#include "XVariant.h"
#include "XGuiConfig.h"

#if XMENU_ON
#include "XMenu.h"
#include "XAction.h"
#include "XTextMenu.h"
#endif /* XMENU_ON */

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XWindowEvent.h"
#include <stdio.h>
#include <stdint.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON

/* ==================== 内部工具 ==================== */

/** @brief 行左留白（绘制与命中共用口径；与控制器 draw 原点平移量一致）。 */
#define XPE_TEXT_LEFT 2
/** @brief 字体度量失败时的回退行高（对标控制器 XTC_DEFAULT_LINE_HEIGHT）。 */
#define XPE_FALLBACK_LINE_HEIGHT 16
/** @brief 拖选边缘自动滚动启动间隔（对标 QTextEdit autoScrollTimer 100ms）。 */
#define XPE_AUTO_SCROLL_INTERVAL_MS 100
/** @brief 拖选边缘自动滚动的视口边缘判定带宽（px）。 */
#define XPE_AUTO_SCROLL_EDGE 16

static uint32_t xpe_color(const XPlainTextEdit* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self; (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

static void xpe_emitChanged(XPlainTextEdit* self)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_textChanged_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      发射 selectionChanged 信号（对标 selectionChanged 真发射）。
 * @param      self 目标控件指针；NULL 时无操作。
 * @return     无返回值。
 */
static void xpe_emitSelectionChanged(XPlainTextEdit* self)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_create(0);
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_selectionChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      发射 cursorPositionChanged 信号（真发射；发射源为控制器
 *             cursorPositionChanged 经壳转发）。
 * @param      self 目标控件指针；NULL 时无操作。
 * @return     无返回值。
 */
static void xpe_emitCursorPositionChanged(XPlainTextEdit* self)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_create(0);
    if (!args) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_cursorPositionChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      发射 bool 单参信号（撤销/重做/复制可用/修改标志族）。
 * @param      self 目标控件指针。
 * @param      signal 信号标识（壳公开信号函数地址）。
 * @param      value 载荷。
 * @return     无返回值。
 */
static void xpe_emitBool(XPlainTextEdit* self, size_t signal, bool value)
{
    XVarList* arguments = XVarList_Create(XVar(bool, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief      发射 int 单参信号（blockCountChanged）。
 * @param      self 目标控件指针。
 * @param      signal 信号标识（壳公开信号函数地址）。
 * @param      value 载荷。
 * @return     无返回值。
 */
static void xpe_emitInt(XPlainTextEdit* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief      取控件视口矩形（局部坐标）。
 * @param      self 目标控件；NULL 时返回零矩形。
 * @return     视口矩形。
 */
static XRect xpe_viewportRect(const XPlainTextEdit* self)
{
    XRect r;

    if (!self) {
        XRect_init(&r, 0, 0, 0, 0);
        return r;
    }
    XRect_init(&r, 0, 0, XWidget_width((XWidget*)self),
               XWidget_height((XWidget*)self));
    return r;
}

/* ==================== 控制器查询适配（公开 API 组合，缺口补偿） ==== */

/**
 * @brief      控制器行高（px）。
 * @details    控制器未公开行高查询：cursorRectAt(0).height 即行高（光标
 *             矩形高度恒为一行行高，软换行/NoWrap 同一口径），字体度量
 *             失败回退 16。
 * @param      ctl 目标控制器；可为 NULL。
 * @return     行高像素值。
 */
static int xpe_ctlLineHeight(const XTextControl* ctl)
{
    XRect r;
    if (!ctl) return XPE_FALLBACK_LINE_HEIGHT;
    r = XTextControl_cursorRectAt(ctl, 0);
    return r.height > 0 ? r.height : XPE_FALLBACK_LINE_HEIGHT;
}

/**
 * @brief      控制器逻辑块数（对标 blockCount；不含软换行拆分）。
 * @details    块数上限裁剪与 blockCount 查询的口径（Qt blockCount 为
 *             逻辑块，永不随折行变化）。
 * @param      ctl 目标控制器；可为 NULL。
 * @return     逻辑块数（恒 >= 1；控制器为空时返回 0）。
 */
static int xpe_ctlBlockCount(const XTextControl* ctl)
{
    return XTextControl_blockCount(ctl);
}

/**
 * @brief      控制器绝对位置 → (可视行, 可视行内字节列)。
 * @details    对标 QPlainTextEdit 可视行口径（lineCount 即可视行）：
 *             委托控制器 posToVisualLineCol（软换行行内含 preedit 视觉
 *             列换算）。
 * @param      ctl 目标控制器；可为 NULL。
 * @param      pos 文档绝对 UTF-8 字节位置（越界由控制器钳位）。
 * @param      line 输出可视行号；可为 NULL。
 * @param      col 输出可视行内字节列；可为 NULL。
 * @return     无返回值。
 */
static void xpe_ctlPosToLineCol(const XTextControl* ctl, int pos,
                                int* line, int* col)
{
    XTextControl_posToVisualLineCol(ctl, pos, line, col);
}

/**
 * @brief      控制器 (可视行, 行内字节列) → 绝对位置。
 * @details    可视行口径（与 xpe_ctlPosToLineCol 互逆）：委托控制器
 *             visualLineColToPos（行列越界由控制器钳位）。
 * @param      ctl 目标控制器；可为 NULL。
 * @param      line 目标可视行（0 起，越界钳位）。
 * @param      col 目标列（可视行内 UTF-8 字节偏移，越界钳位）。
 * @return     文档绝对字节位置。
 */
static int xpe_ctlLineColToPos(const XTextControl* ctl, int line, int col)
{
    return XTextControl_visualLineColToPos(ctl, line, col);
}

/**
 * @brief      文档是否为空（对标 QTextDocument::isEmpty 的壳级判定，
 *             占位文本显示条件）。
 * @param      self 目标控件；可为 NULL。
 * @return     单空行/零行文档返回 true。
 */
static bool xpe_documentEmpty(const XPlainTextEdit* self)
{
    XTextControl* ctl = self ? self->m_control : NULL;
    char* text;
    bool empty;
    if (!ctl) return true;
    if (xpe_ctlBlockCount(ctl) > 1) return false;
    text = XTextControl_toPlainText(ctl);
    empty = (!text || text[0] == '\0');
    if (text) XFree_System(text);
    return empty;
}

/**
 * @brief      视口局部坐标 → 控制器内容坐标（事件入口共用换算）。
 * @details    内容 X = 视口 X - 行左留白 + 水平滚动值；内容 Y = 视口 Y
 *             + 垂直滚动值。与 paintEvent 的 translate(2, -scroll) 互逆。
 * @param      self 目标控件；可为 NULL。
 * @param      viewportPos 视口局部坐标点；可为 NULL（返回 {0,0}）。
 * @return     内容坐标点。
 */
static XPoint xpe_toContentPos(const XPlainTextEdit* self,
                               const XPoint* viewportPos)
{
    XPoint content;
    XScrollBar* vsb;
    XScrollBar* hsb;
    int vs = 0;
    int hs = 0;
    content.x = viewportPos ? viewportPos->x - XPE_TEXT_LEFT : 0;
    content.y = viewportPos ? viewportPos->y : 0;
    if (!self) return content;
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    hsb = XAbstractScrollArea_horizontalScrollBar((XAbstractScrollArea*)self);
    if (vsb) vs = XScrollBar_value(vsb);
    if (hsb) hs = XScrollBar_value(hsb);
    content.x += hs;
    content.y += vs;
    return content;
}

/**
 * @brief      按壳交互标志/只读态重算控制器交互标志。
 * @details    只读映射为剥掉 TextEditable（对标 Qt readOnly 实现）；
 *             壳交互标志为默认值 0 时取编辑器默认组合（保持既有
 *             "标志未使用时编辑可用"的现网口径）。
 * @param      self 目标控件；可为 NULL。
 * @return     无返回值。
 */
static void xpe_syncInteractionFlags(XPlainTextEdit* self)
{
    int flags;
    if (!self || !self->m_control) return;
    flags = self->m_textInteractionFlags;
    if (flags == 0)
        flags = (int)XTextControlInteraction_TextEditorInteraction;
    if (!self->m_readOnly)
        flags |= (int)XTextControlInteraction_TextEditable;
    XTextControl_setTextInteractionFlags(self->m_control, flags);
}

/**
 * @brief      经控制器公开 API 删除文档绝对区间 [start, end)。
 * @details    控制器缺口补偿：无公开 removeSelectedText，借
 *             setTextCursor（选区间）+ cut（删除选区）组合实现；剪贴板
 *             先存后还，保证对外无剪贴板副作用。撤销栈获得一条精确
 *             remove 命令（与控制器编辑管线一致）。
 * @param      self 目标控件；不可为 NULL（m_control 非空）。
 * @param      start 起始绝对位置（含）。
 * @param      end 结束绝对位置（排他）。
 * @return     无返回值。
 */
static void xpe_ctlRemoveRange(XPlainTextEdit* self, int start, int end)
{
    XTextControl* ctl;
    char* saved = NULL;
    const char* clip;
    if (!self || !self->m_control || end <= start) return;
    ctl = self->m_control;
    clip = XTextClipboard_getText();
    if (clip && clip[0]) {
        size_t n = XStrlen(clip);
        saved = (char*)XMalloc_System(n + 1);
        if (saved) XMemcpy(saved, clip, n + 1);
    }
    XTextControl_setTextCursor(ctl, start, end, false);
    XTextControl_cut(ctl);
    /* 还原剪贴板（getText 借用指针在 cut 写剪贴板后失效，故先拷贝）。 */
    XTextClipboard_setText(saved ? saved : "");
    if (saved) XFree_System(saved);
}

/**
 * @brief      UTF-8 文本保留末尾 max 行的起点。
 * @param      utf8 文本；不为 NULL。
 * @param      max 保留行数；> 0。
 * @return     保留区起点指针；行数不足 max 时返回原文本起点。
 */
static const char* xpe_tailStart(const char* utf8, int max)
{
    size_t len = XStrlen(utf8);
    size_t i = len;
    int found = 0;
    while (i > 0 && found < max) {
        --i;
        if (utf8[i] == 0x0A) ++found;
    }
    if (found < max) return utf8; /* 换行不足：行数 <= max，无需裁剪。 */
    return utf8 + i + 1;          /* utf8[i] 为第 max 个换行。 */
}

/**
 * @brief      块数上限裁剪（控制器 maximumBlockCount 缺口的壳级补偿）。
 * @details    超限丢弃最旧行：可编辑时走 xpe_ctlRemoveRange（保留撤销）；
 *             只读时撤销栈本就被控制器禁用（init 语义），整文重置
 *             裁剪。m_inTrim 防止裁剪引发的 textChanged 重入。
 * @param      self 目标控件；可为 NULL。
 * @return     无返回值。
 */
static void xpe_enforceMaxBlockCount(XPlainTextEdit* self)
{
    XTextControl* ctl;
    int max;
    int count;
    int cutLen = 0;
    int i;
    char* text;
    if (!self || self->m_inTrim || !self->m_control) return;
    max = self->m_maxBlockCount;
    if (max <= 0) return;
    ctl = self->m_control;
    count = xpe_ctlBlockCount(ctl);
    if (count <= max) return;
    text = XTextControl_toPlainText(ctl);
    if (!text) return;
    for (i = 0; i < count - max; ++i) {
        const char* nl = XStrchr(text + cutLen, 0x0A);
        if (!nl) break;
        cutLen = (int)(nl - text) + 1;
    }
    self->m_inTrim = true;
    if (self->m_readOnly) {
        const char* tail = xpe_tailStart(text, max);
        if (tail != text) XTextControl_setPlainText(ctl, tail);
    } else if (cutLen > 0) {
        xpe_ctlRemoveRange(self, 0, cutLen);
    }
    self->m_inTrim = false;
    XFree_System(text);
}

/**
 * @brief      无选区时按码点粒度删除前一/后一码点（控制器缺口补偿）。
 * @details    XTextControl 的无选区 Backspace/Delete 为字节粒度（中文会
 *             残留残缺 UTF-8 序列），本函数按码点边界换算删除区间后走
 *             xpe_ctlRemoveRange；跨行（行首退格/行尾 Delete）交控制器
 *             处理（'\' + 'n' 恰为单字节，控制器删除精确）。
 * @param      self 目标控件；可为 NULL。
 * @param      backspace true = 删前一码点（Backspace）；false = 删后一
 *             码点（Delete）。
 * @return     已在本函数完成删除返回 true（事件不再转发控制器）；交由
 *             控制器处理（有选区/跨行/边界）返回 false。
 */
static bool xpe_eraseCodepoint(XPlainTextEdit* self, bool backspace)
{
    XTextControl* ctl = self ? self->m_control : NULL;
    int pos = 0;
    int anchor = 0;
    int line = 0;
    int col = 0;
    int lineLen = 0;
    int start;
    int end;
    char* lineText = NULL;
    XTextControlImValue value;
    if (!ctl) return false;
    XTextControl_textCursor(ctl, &pos, &anchor);
    if (pos != anchor) return false; /* 有选区：控制器整段删除本就精确。 */
    xpe_ctlPosToLineCol(ctl, pos, &line, &col);
    XMemset(&value, 0, sizeof(value));
    if (XTextControl_inputMethodQuery(
            ctl, (int)XInputMethodQuery_ImSurroundingText, -1, &value) &&
        value.type == 3 && value.text)
        lineText = value.text;
    if (lineText) lineLen = (int)XStrlen(lineText);
    if (col < 0) col = 0;
    if (col > lineLen) col = lineLen;
    start = pos;
    end = pos;
    if (backspace) {
        if (pos > 0 && col > 0 && lineText) {
            /* 按码点边界回退（与原壳实现 XTextUtf8_prevBoundary 语义
               一致）：整码点移除，不留残缺 UTF-8 续字节。 */
            int prev = (int)XTextUtf8_prevBoundary(lineText, (size_t)col);
            start = pos - (col - prev);
            end = pos;
        }
    } else {
        if (lineText && col < lineLen) {
            /* 按码点边界前进删除：整码点移除（同原壳实现语义）。 */
            int seq = XTextUtf8_seqLen(lineText + col, lineLen - col);
            if (seq > 0) {
                start = pos;
                end = pos + seq;
            }
        }
    }
    if (lineText) XFree_System(lineText);
    if (end <= start) return false; /* 行首退格/行尾 Delete：交控制器。 */
    xpe_ctlRemoveRange(self, start, end);
    return true;
}

/* ==================== 兼容镜像（供不可修改消费方 XTextEdit） ========== */

/**
 * @brief      重建行文本镜像 m_lines（权威在控制器，单向派生）。
 * @details    仅供既有消费方 XTextEdit 只读借用（xte_editorLineAt）；
 *             控制器 textChanged 时整体重建（O(文档长)，与消费方逐行
 *             绘制同级开销）。行数组内容与控制器 toPlainText 按 '\n'
 *             拆分逐一对应，恒至少一行。
 * @param      self 目标控件；可为 NULL。
 * @return     无返回值。
 */
static void xpe_rebuildLinesMirror(XPlainTextEdit* self)
{
    char* text = NULL;
    const char* src;
    size_t start = 0;
    int64_t i;
    int64_t n;
    if (!self || !self->m_lines) return;
    if (self->m_control) text = XTextControl_toPlainText(self->m_control);
    src = text ? text : "";
    n = XVector_size_base((const XContainer*)self->m_lines);
    for (i = 0; i < n; ++i) {
        char** item = (char**)XVector_at_base(self->m_lines, i);
        if (item && *item) XFree_System(*item);
    }
    XVector_clear_base(self->m_lines);
    while (start <= XStrlen(src)) {
        const char* nl = XStrchr(src + start, 0x0A);
        size_t end = nl ? (size_t)(nl - (src + start)) : XStrlen(src + start);
        char* line = (char*)XMalloc_System(end + 1);
        if (!line) break;
        XMemcpy(line, src + start, end);
        line[end] = 0;
        XVector_push_back_1_base(self->m_lines, &line);
        if (!nl) break;
        start += end + 1;
    }
    if (text) XFree_System(text);
}

/**
 * @brief      撤销/重做可用哨兵栈镜像（XTextEdit 仅查询空/非空）。
 * @param      self 目标控件；可为 NULL。
 * @param      available 控制器 undoAvailable/redoAvailable 态。
 * @param      stack 哨兵栈（m_undoStack/m_redoStack）。
 * @return     无返回值。
 */
static void xpe_mirrorUndoState(XPlainTextEdit* self, bool available,
                                XVector* stack)
{
    if (!self || !stack) return;
    if (available) {
        if (XVector_size_base((const XContainer*)stack) == 0) {
            char* marker = NULL; /* 哨兵占位：内容不被任何消费方解引用。 */
            XVector_push_back_1_base(stack, &marker);
        }
    } else if (XVector_size_base((const XContainer*)stack) > 0) {
        XVector_clear_base(stack);
    }
}

/* ==================== 控制器信号转发槽（发射点唯一原则） ========== */

/** @brief 转发槽：textChanged（块数上限补偿 → 行镜像重建 → 壳信号）。 */
static void xpe_ctlTextChangedSlot(XObject* receiver, XVarList* args)
{
    XPlainTextEdit* self = (XPlainTextEdit*)receiver;
    (void)args;
    if (!self) return;
    xpe_enforceMaxBlockCount(self);
    xpe_rebuildLinesMirror(self);
    xpe_emitChanged(self);
}

/** @brief 转发槽：selectionChanged。 */
static void xpe_ctlSelectionChangedSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    xpe_emitSelectionChanged((XPlainTextEdit*)receiver);
}

/** @brief 转发槽：cursorPositionChanged。 */
static void xpe_ctlCursorPositionChangedSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    xpe_emitCursorPositionChanged((XPlainTextEdit*)receiver);
}

/** @brief 转发槽：undoAvailable(bool)（哨兵栈镜像 + 壳信号转发）。 */
static void xpe_ctlUndoAvailableSlot(XObject* receiver, XVarList* args)
{
    XPlainTextEdit* self = (XPlainTextEdit*)receiver;
    if (!args || !self) return;
    XVarList_args_1(args, bool, value);
    xpe_mirrorUndoState(self, value, self->m_undoStack);
    xpe_emitBool(self, (size_t)XPlainTextEdit_undoAvailable_signal, value);
}

/** @brief 转发槽：redoAvailable(bool)（哨兵栈镜像 + 壳信号转发）。 */
static void xpe_ctlRedoAvailableSlot(XObject* receiver, XVarList* args)
{
    XPlainTextEdit* self = (XPlainTextEdit*)receiver;
    if (!args || !self) return;
    XVarList_args_1(args, bool, value);
    xpe_mirrorUndoState(self, value, self->m_redoStack);
    xpe_emitBool(self, (size_t)XPlainTextEdit_redoAvailable_signal, value);
}

/** @brief 转发槽：copyAvailable(bool)。 */
static void xpe_ctlCopyAvailableSlot(XObject* receiver, XVarList* args)
{
    if (!args || !receiver) return;
    XVarList_args_1(args, bool, value);
    xpe_emitBool((XPlainTextEdit*)receiver,
                 (size_t)XPlainTextEdit_copyAvailable_signal, value);
}

/** @brief 转发槽：modificationChanged(bool)。 */
static void xpe_ctlModificationChangedSlot(XObject* receiver, XVarList* args)
{
    if (!args || !receiver) return;
    XVarList_args_1(args, bool, value);
    xpe_emitBool((XPlainTextEdit*)receiver,
                 (size_t)XPlainTextEdit_modificationChanged_signal, value);
}

/** @brief 转发槽：blockCountChanged(int)。 */
static void xpe_ctlBlockCountChangedSlot(XObject* receiver, XVarList* args)
{
    if (!args || !receiver) return;
    XVarList_args_1(args, int, value);
    xpe_emitInt((XPlainTextEdit*)receiver,
                (size_t)XPlainTextEdit_blockCountChanged_signal, value);
}

/** @brief 转发槽：updateRequest → 请求控件重绘（载荷为内容坐标矩形）。 */
static void xpe_ctlUpdateRequestSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XWidget_update((XWidget*)receiver);
}

/** @brief 转发槽：documentSizeChanged → 内容尺寸联动（滚动区机制）。 */
static void xpe_ctlDocumentSizeSlot(XObject* receiver, XVarList* args)
{
    XPlainTextEdit* self = (XPlainTextEdit*)receiver;
    if (!args || !self) return;
    XVarList_args_1(args, XSize, size);
    /* 与原 xpe_afterChange 口径一致：高度附加 4px 绘制余量。 */
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)self,
                                       size.width, size.height + 4);
    XWidget_update((XWidget*)self);
}

/**
 * @brief      转发槽：visibilityRequest → 滚动条联动（控制器
 *             ensureCursorVisible 语义的壳级执行，对标 Qt 视图层
 *             ensureVisible；最小滚动量使光标矩形可见）。
 */
static void xpe_ctlVisibilitySlot(XObject* receiver, XVarList* args)
{
    XPlainTextEdit* self = (XPlainTextEdit*)receiver;
    XScrollBar* vsb;
    XScrollBar* hsb;
    if (!args || !self) return;
    XVarList_args_1(args, XRect, area);
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (vsb) {
        int v = XScrollBar_value(vsb);
        int h = XWidget_height((XWidget*)self);
        if (area.y < v)
            v = area.y;
        else if (area.y + area.height > v + h)
            v = area.y + area.height - h;
        XScrollBar_setValue(vsb, v);
    }
    hsb = XAbstractScrollArea_horizontalScrollBar((XAbstractScrollArea*)self);
    if (hsb) {
        int hv = XScrollBar_value(hsb);
        int w = XWidget_width((XWidget*)self);
        if (area.x < hv)
            hv = area.x;
        else if (area.x + area.width > hv + w)
            hv = area.x + area.width - w;
        XScrollBar_setValue(hsb, hv);
    }
}

/** @brief 连接控制器信号 → 壳信号转发（发射点唯一：控制器发射，壳转接）。 */
static void xpe_connectControl(XPlainTextEdit* self)
{
    XObject* ctl;
    XObject* receiver;
    if (!self || !self->m_control) return;
    ctl = (XObject*)self->m_control;
    receiver = (XObject*)self;
#define XPE_CONNECT(sig, slot) \
    XObject_connect_1(ctl, (size_t)(sig), receiver, (slot), \
                      XConnectionType_Direct)
    XPE_CONNECT(XTextControl_textChanged_signal, xpe_ctlTextChangedSlot);
    XPE_CONNECT(XTextControl_selectionChanged_signal,
                xpe_ctlSelectionChangedSlot);
    XPE_CONNECT(XTextControl_cursorPositionChanged_signal,
                xpe_ctlCursorPositionChangedSlot);
    XPE_CONNECT(XTextControl_undoAvailable_signal, xpe_ctlUndoAvailableSlot);
    XPE_CONNECT(XTextControl_redoAvailable_signal, xpe_ctlRedoAvailableSlot);
    XPE_CONNECT(XTextControl_copyAvailable_signal, xpe_ctlCopyAvailableSlot);
    XPE_CONNECT(XTextControl_modificationChanged_signal,
                xpe_ctlModificationChangedSlot);
    XPE_CONNECT(XTextControl_blockCountChanged_signal,
                xpe_ctlBlockCountChangedSlot);
    XPE_CONNECT(XTextControl_updateRequest_signal, xpe_ctlUpdateRequestSlot);
    XPE_CONNECT(XTextControl_documentSizeChanged_signal,
                xpe_ctlDocumentSizeSlot);
    XPE_CONNECT(XTextControl_visibilityRequest_signal,
                xpe_ctlVisibilitySlot);
#undef XPE_CONNECT
}

/* ==================== 拖选边缘自动滚动（壳保留，100ms 启动） ======== */

/** @brief 停止自动滚动定时器。 */
static void xpe_autoScrollStop(XPlainTextEdit* self)
{
    if (!self) return;
    if (self->m_autoScrollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_autoScrollTimer);
        self->m_autoScrollTimer = XTIMER_INVALID_ID;
    }
    self->m_autoScrollDir = 0;
}

/**
 * @brief      按指针位置更新自动滚动状态（拖选进行中且进入视口上下
 *             边缘带宽时以 100ms 间隔启动，对标 QTextEdit 的
 *             autoScrollTimer 启动间隔）。
 * @param      self 目标控件；可为 NULL。
 * @param      viewportPos 视口局部坐标（控件局部口径）。
 * @return     无返回值。
 */
static void xpe_autoScrollUpdate(XPlainTextEdit* self, const XPoint* viewportPos)
{
    int h;
    int dir = 0;
    if (!self || !viewportPos) return;
    h = XWidget_height((XWidget*)self);
    if (viewportPos->y < XPE_AUTO_SCROLL_EDGE)
        dir = -1;
    else if (viewportPos->y > h - XPE_AUTO_SCROLL_EDGE)
        dir = 1;
    if (dir == 0) {
        xpe_autoScrollStop(self);
        return;
    }
    self->m_autoScrollDir = dir;
    if (self->m_autoScrollTimer == XTIMER_INVALID_ID)
        self->m_autoScrollTimer = XObject_startTimer_ms(
            (XObject*)self, XPE_AUTO_SCROLL_INTERVAL_MS, XTimerType_CoarseTimer);
}

/** @brief 自动滚动步进：按当前方向滚动一行。 */
static void xpe_autoScrollTick(XPlainTextEdit* self)
{
    XScrollBar* vsb;
    int lh;
    if (!self) return;
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (!vsb) return;
    lh = xpe_ctlLineHeight(self->m_control);
    if (lh <= 0) lh = XPE_FALLBACK_LINE_HEIGHT;
    XScrollBar_setValue(vsb, XScrollBar_value(vsb) +
                                 (self->m_autoScrollDir >= 0 ? lh : -lh));
}

/* ==================== 事件入口（视口→内容坐标 → 控制器路由） ======== */

/**
 * @brief      鼠标事件转发：拷贝事件并换算为内容坐标后投递控制器，
 *             随后把 accept/ignore 结果同步回原始事件（对标 Qt 事件
 *             结果透传；坐标平移对标 QPlainTextEdit 私有类的
 *             mapToContents）。
 * @param      self 目标控件；不可为 NULL（m_control 非空）。
 * @param      event 原始鼠标事件。
 * @return     无返回值。
 */
static void xpe_syncControlFont(XPlainTextEdit* self)
{
    XFont font;
    if (!self || !self->m_control) return;
    font = XWidget_font((XWidget*)self);
    XTextControl_setFont(self->m_control, &font);
    XFont_deinit_base(&font);
}

/**
 * @brief      按换行模式把折行宽度下发控制器（对标 Qt WidgetWidth 模式
 *             下文档 textWidth = 视口宽的私有同步路径）。
 * @details    WidgetWidth：折行宽 = 控件宽 - 行左留白（视口文本区宽）；
 *             NoWrap：清除定宽（-1），长行改由水平滚动/逐行裁剪承载。
 *             取值未变化时控制器 setTextWidth 幂等无操作。
 * @param      self 目标控件；可为 NULL。
 * @return     无返回值。
 */
static void xpe_updateWrapWidth(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    if (self->m_wrapMode == (int)XPlainTextEditMode_WidgetWidth) {
        int w = XWidget_width((XWidget*)self) - XPE_TEXT_LEFT;
        if (w < 1) w = 1;
        XTextControl_setTextWidth(self->m_control, w);
    } else {
        /* 对标 Qt textWidth = -1（无固定宽）：不软换行。 */
        XTextControl_setTextWidth(self->m_control, -1);
    }
}

static void xpe_forwardMouseEvent(XPlainTextEdit* self, XEvent* event)
{
    XMouseEvent translated;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!self || !self->m_control) return;
    xpe_syncControlFont(self);
    translated = *me;
    translated.m_position = xpe_toContentPos(self, &me->m_position);
    XTextControl_processEvent(self->m_control, (XEvent*)&translated);
    if (XEvent_isAccepted((XEvent*)&translated))
        XEvent_accept(event);
    else
        XEvent_ignore(event);
}

/** @brief 鼠标按下：聚焦并把点击坐标换算后交控制器定位/选区。 */
static void VX_plainTextEdit_mousePressEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        return;
    /* 焦点编排属壳（控制器无 widget 身份，对标 Qt 壳级取焦点）。 */
    XWidget_setFocus(self);
    if (edit->m_control) xpe_forwardMouseEvent(edit, event);
    XWidget_update(self);
}

/** @brief 鼠标移动：拖选扩展交控制器；按住拖选时维护边缘自动滚动。 */
static void VX_plainTextEdit_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XMouseEvent* me;
    if (!edit || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    me = (XMouseEvent*)event;
    if (edit->m_control) xpe_forwardMouseEvent(edit, event);
    if (me->m_buttons & (int)XMouseButton_LeftButton)
        xpe_autoScrollUpdate(edit, &me->m_position);
    else
        xpe_autoScrollStop(edit);
}

/** @brief 鼠标释放：停止边缘自动滚动并把收尾交控制器。 */
static void VX_plainTextEdit_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE)
        return;
    xpe_autoScrollStop(edit);
    if (edit->m_control) xpe_forwardMouseEvent(edit, event);
}

/** @brief 鼠标双击：双击选词/三击判定起表交控制器。 */
static void VX_plainTextEdit_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK)
        return;
    if (edit->m_control) xpe_forwardMouseEvent(edit, event);
    XWidget_update(self);
}

/** @brief 焦点进出：转发控制器（光标可见性/闪烁/指示选区清理）并重绘。 */
static void VX_plainTextEdit_focusEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    if (!edit || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_FOCUS_IN ||
        XEvent_type(event) == XEVENT_TYPE_FOCUS_OUT) {
        if (edit->m_control) XTextControl_processEvent(edit->m_control, event);
        XWidget_update(self);
        XEvent_accept(event);
    }
}

/** @brief 输入法事件：提交/组合文本交控制器（只读壳级门禁保留）。 */
static void VX_plainTextEdit_inputMethodEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_INPUT_METHOD) return;
    /* 旧口径：只读不投递 IME（控制器对可选中文本的 IME 门禁更宽）。 */
    if (edit->m_readOnly || !edit->m_control) return;
    XTextControl_processEvent(edit->m_control, event);
    XEvent_accept(event);
}

#if XINPUTMETHOD_ON
/** @brief 基类默认查询复刻（对标 QWidget::inputMethodQuery 默认实现）。
 *  @note  基类实现为 XWidget.c 内部静态，虚槽重载后无法显式回调，按其
 *         文档契约逐项复刻：ImCursorRectangle=(w/2,0,1,h)、
 *         ImInputItemClipRectangle=控件矩形浮点副本、ImHints=hints、
 *         ImEnabled=true，其余查询项返回 NULL（等价无效 QVariant）。 */
static XVariant* xpe_inputMethodQueryBase(const XWidget* self,
                                          XInputMethodQuery query)
{
    if (!self) return NULL;
    switch (query) {
    case XInputMethodQuery_ImCursorRectangle: {
        XRectF rect;
        rect.x = (float)XWidget_width(self) / 2.0f;
        rect.y = 0.0f;
        rect.width = 1.0f;
        rect.height = (float)XWidget_height(self);
        return XVariant_create(&rect, sizeof(rect), XVariantType_User);
    }
    case XInputMethodQuery_ImInputItemClipRectangle: {
        XRect rect = XWidget_rect(self);
        XRectF rectF;
        rectF.x = (float)rect.x;
        rectF.y = (float)rect.y;
        rectF.width = (float)rect.width;
        rectF.height = (float)rect.height;
        return XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
    }
    case XInputMethodQuery_ImHints: {
        int32_t value = (int32_t)XWidget_inputMethodHints(self);
        return XVariant_create(&value, sizeof(value), XVariantType_Int32);
    }
    case XInputMethodQuery_ImEnabled: {
        bool enabled = true;
        return XVariant_create(&enabled, sizeof(enabled), XVariantType_Bool);
    }
    default:
        return NULL;
    }
}

/**
 * @brief      输入法查询虚槽：委托控制器全枚举（对标 QPlainTextEdit::
 *             inputMethodQuery 转发 QTextDocument 控制层，使 IME 拿到
 *             真实光标矩形/环绕文本/选区态，而非基类兜底的居中假矩形）。
 * @details    控制器平铺结果转 XVariant（1 整数→Int32 / 2 布尔→Bool /
 *             3 文本→String / 4 矩形→User 的 XRectF）；矩形再按
 *             XPlainTextEdit_cursorRect 同口径做视口换算（+行左留白
 *             XPE_TEXT_LEFT、扣垂直/水平滚动取值），输出控件局部坐标
 *             （对标 Qt 平台输入法以焦点控件坐标系消费 ImCursorRectangle）。
 *             控制器缺席或无对应查询项时回落基类默认复刻；查询本身
 *             不修改控件状态。
 */
static XVariant* VX_plainTextEdit_inputMethodQuery(const XWidget* self,
                                                   XInputMethodQuery query)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XTextControlImValue value;
    XVariant* var = NULL;
    if (!edit || !edit->m_control)
        return xpe_inputMethodQueryBase(self, query);
    XMemset(&value, 0, sizeof(value));
    if (!XTextControl_inputMethodQuery(edit->m_control, (int)query, -1,
                                       &value))
        return xpe_inputMethodQueryBase(self, query);
    switch (value.type) {
    case 1: {
        int32_t i = value.i;
        var = XVariant_create(&i, sizeof(i), XVariantType_Int32);
        break;
    }
    case 2: {
        bool b = value.b;
        var = XVariant_create(&b, sizeof(b), XVariantType_Bool);
        break;
    }
    case 3:
        /* 文本为堆拷贝（XString_toVariant_utf8 内部再拷，搬运后即释）。 */
        var = value.text ? XString_toVariant_utf8(value.text) : NULL;
        break;
    case 4: {
        /* 内容坐标 → 控件局部：行左留白与滚动扣除（与
           XPlainTextEdit_cursorRect 逐项同口径）。 */
        XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
            (XAbstractScrollArea*)edit);
        XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar(
            (XAbstractScrollArea*)edit);
        XRectF rectF;
        rectF.x = (float)(value.rect.x + XPE_TEXT_LEFT -
                          (hsb ? XScrollBar_value(hsb) : 0));
        rectF.y = (float)(value.rect.y -
                          (vsb ? XScrollBar_value(vsb) : 0));
        rectF.width = (float)value.rect.width;
        rectF.height = (float)value.rect.height;
        var = XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
        break;
    }
    default:
        break;
    }
    /* 类别 3 的堆文本用后即释（XTextControlImValue 契约）。 */
    if (value.text) XFree_System(value.text);
    return var ? var : xpe_inputMethodQueryBase(self, query);
}
#endif /* XINPUTMETHOD_ON */

#if XMENU_ON
/** @brief 右键菜单事件：弹出标准编辑菜单（菜单数据源为控制器
 *         createStandardContextMenu；弹出/DeleteOnClose 编排保留壳）。 */
static void VX_plainTextEdit_contextMenuEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XContextMenuEvent* ctx;
    XMenu* menu;
    XPoint global;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_CONTEXT_MENU) return;
    ctx = (XContextMenuEvent*)event;
    menu = edit->m_control
               ? XTextControl_createStandardContextMenu(edit->m_control)
               : NULL;
    if (!menu) return;
    global = XContextMenuEvent_globalPosition(ctx);
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &global);
    XEvent_accept(event);
}
#endif /* XMENU_ON */

/**
 * @brief      键盘事件：交控制器路由（全选/复制快捷键、光标移动与
 *             Shift 扩展、Ctrl 编辑族、编辑原语）。无修饰 Backspace/
 *             Delete 先经壳级码点粒度补偿（控制器该两分支为字节粒度）。
 */
static void VX_plainTextEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XKeyEvent* ke;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    if (!edit->m_control) return;
    ke = (XKeyEvent*)event;
    if (!edit->m_readOnly &&
        (ke->m_key == (int)XKey_Backspace ||
         ke->m_key == (int)XKey_Delete) &&
        ((int)ke->m_modifiers &
         ~((int)XKeyboardModifier_ShiftModifier
           | (int)XKeyboardModifier_KeypadModifier)) == 0) {
        if (xpe_eraseCodepoint(edit, ke->m_key == (int)XKey_Backspace)) {
            XEvent_accept(event);
            XWidget_update(self);
            return;
        }
    }
    xpe_syncControlFont(edit);
    XTextControl_processEvent(edit->m_control, event);
}

/** @brief 绘制：壳画背景/凹陷边框/占位文本，正文/选区/光标/IME 下划线
 *         由控制器 XTextControl_draw 在内容坐标绘制（translate 互逆）。 */
static void VX_plainTextEdit_paintEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XScrollBar* vsb;
    int scroll = 0;
    uint32_t placeholder;
    if (!edit || !event) return;
    xpe_syncControlFont(edit);
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    if (vsb) scroll = XScrollBar_value(vsb);
    placeholder = xpe_color(edit, XPaletteColorRole_Mid);
    /* 绘制范围 = 事件脏区（非 PAINT 入口退化为整控件）：背景填充、
       边框、行绘制全部限幅在脏区内，避免小区域刷新（性能浮层/光标
       闪烁）触发整页文本重绘。 */
    {
        int pw = XWidget_width(self);
        int ph = XWidget_height(self);
        XRect clip;
        if (event && XEvent_type(event) == XEVENT_TYPE_PAINT)
            clip = XPaintEvent_rect((const XPaintEvent*)event);
        else
            XRect_init(&clip, 0, 0, pw, ph);
        if (clip.x < 0) { clip.width += clip.x; clip.x = 0; }
        if (clip.y < 0) { clip.height += clip.y; clip.y = 0; }
        if (clip.x + clip.width > pw) clip.width = pw - clip.x;
        if (clip.y + clip.height > ph) clip.height = ph - clip.y;
        if (clip.width > 0 && clip.height > 0)
        {
            XPainter_setClipRect(&painter, &clip,
                                 XPainterClipOperation_ReplaceClip);
            /* 背景：清屏防止父控件渲染透出。 */
            XPainter_fillRect(&painter, &clip, 0xFFFFFFFFu);
            /* 边框：上/左 dark、下/右 light 的凹陷框
               （对标 QAbstractScrollArea 默认 StyledPanel|Sunken，与
               XLineEdit 手绘回退同款）。 */
            {
                uint32_t dark = xpe_color(edit, XPaletteColorRole_Dark);
                uint32_t light = xpe_color(edit, XPaletteColorRole_Light);
                if (dark == 0u) dark = 0xFF808080u;
                if (light == 0u) light = 0xFFE0E0E0u;
                XPainter_fillRect(&painter, &(XRect){0, 0, pw, 1}, dark);
                XPainter_fillRect(&painter, &(XRect){0, 0, 1, ph}, dark);
                XPainter_fillRect(&painter,
                    &(XRect){0, ph - 1, pw, 1}, light);
                XPainter_fillRect(&painter,
                    &(XRect){pw - 1, 0, 1, ph}, light);
            }
        }
    }
    /* 占位文本：空内容灰显（空判定读控制器，绘制属壳）。
       注：远端帧数优化提交(28c41c1c)在旧自绘路径上追加了"滚动视口∩
       事件脏区"行范围限幅；本地迁移已将正文/光标绘制委托给控制器
       （XTextControl_draw），旧行循环不复存在，故该段不适用，取本地
       委托结构。若需等效脏区限幅，应在控制器绘制入口补裁剪（待办）。 */
    if (xpe_documentEmpty(edit) && edit->m_placeholder &&
        XString_toUtf8(edit->m_placeholder) &&
        XString_toUtf8(edit->m_placeholder)[0] != 0) {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XFont_deinit_base(&font);
        XPainter_drawText(&painter, 4, 14,
                          XString_toUtf8(edit->m_placeholder), placeholder);
    }
    /* 正文绘制入口：平移行左留白与垂直滚动后交控制器
       （选区高亮/额外选择集/锚点/IME 下划线/闪烁光标由控制器绘制）。 */
    if (edit->m_control) {
        XRect content;
        XPainter_translate(&painter, (float)XPE_TEXT_LEFT, (float)-scroll);
        XRect_init(&content, 0, scroll, XWidget_width(self),
                   XWidget_height(self));
        XTextControl_draw(edit->m_control, &painter, &content);
    }
    XPainter_deinit(&painter);
}

/** @brief 定时器：拖选边缘自动滚动步进（其余交父类链处理）。 */
static void VX_plainTextEdit_timerEvent(XObject* object, XTimerEvent* event)
{
    XPlainTextEdit* self = (XPlainTextEdit*)object;
    if (!self || !event) return;
    if (XTimerEvent_timerId(event) == self->m_autoScrollTimer) {
        xpe_autoScrollTick(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XAbstractScrollArea, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 尺寸变化：父类完成视口/滚动条几何联动后重发折行宽度
 *         （对标 Qt WidgetWidth 模式 resize → 文档 textWidth 重设）。 */
static void VX_plainTextEdit_resizeEvent(XWidget* self, XEvent* event)
{
    XPlainTextEdit* edit = (XPlainTextEdit*)self;
    XClass_Parent(XAbstractScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    if (edit) xpe_updateWrapWidth(edit);
}

static void VX_plainTextEdit_scrollContentsBy(XAbstractScrollArea* self, int dx, int dy)
{
    XPlainTextEdit* edit;
    XRect r;

    (void)dx;
    XWidget_update((XWidget*)self);
    edit = (XPlainTextEdit*)self;
    if (edit) {
        r = xpe_viewportRect(edit);
        XPlainTextEdit_updateRequest_signal(edit, &r, dy);
    }
}

static void VX_plainTextEdit_deinit(XPlainTextEdit* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    xpe_autoScrollStop(self);
    if (self->m_control) {
        /* 行存储/撤销栈/文档镜像/锚点等所有权在控制器，随其销毁。 */
        XClass_delete_base((XClass*)self->m_control);
        self->m_control = NULL;
    }
    if (self->m_lines) {
        /* 兼容镜像行缓冲随壳释放（哨兵栈条目为 NULL，不需释放）。 */
        n = XVector_size_base((const XContainer*)self->m_lines);
        for (i = 0; i < n; ++i) {
            char** item = (char**)XVector_at_base(self->m_lines, i);
            if (item && *item) XFree_System(*item);
        }
        XVector_delete_base(self->m_lines);
        self->m_lines = NULL;
    }
    if (self->m_undoStack) {
        XVector_delete_base(self->m_undoStack);
        self->m_undoStack = NULL;
    }
    if (self->m_redoStack) {
        XVector_delete_base(self->m_redoStack);
        self->m_redoStack = NULL;
    }
    if (self->m_placeholder) {
        XString_delete_base(self->m_placeholder);
        self->m_placeholder = NULL;
    }
    if (self->m_documentTitle) {
        XString_delete_base(self->m_documentTitle);
        self->m_documentTitle = NULL;
    }
    if (self->m_extraSelCache) {
        /* 条目为纯值结构，无堆内成员，整体销毁即可。 */
        XVector_delete_base(self->m_extraSelCache);
        self->m_extraSelCache = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XPlainTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlainTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_plainTextEdit_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_plainTextEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_plainTextEdit_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VX_plainTextEdit_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent, VX_plainTextEdit_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_plainTextEdit_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VX_plainTextEdit_focusEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VX_plainTextEdit_focusEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodEvent, VX_plainTextEdit_inputMethodEvent);
#if XINPUTMETHOD_ON
    /* 输入法查询虚槽：委托控制器真实状态（对标 QPlainTextEdit::
       inputMethodQuery 转发文档控制层；基类兜底只回居中假矩形）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodQuery, VX_plainTextEdit_inputMethodQuery);
#endif /* XINPUTMETHOD_ON */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent, VX_plainTextEdit_contextMenuEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VX_plainTextEdit_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_plainTextEdit_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractScrollArea_ScrollContentsBy, VX_plainTextEdit_scrollContentsBy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_plainTextEdit_deinit);
    return XVTABLE_DEFAULT;
}

void XPlainTextEdit_init(XPlainTextEdit* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPlainTextEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_wrapMode = (int)XPlainTextEditMode_WidgetWidth;
    self->m_undoEnabled = true;
    self->m_backgroundVisible = true;
    self->m_tabStopDistance = 40;
    /* 断行规则缺省 WordWrap（对标 QPlainTextEdit 默认
       QTextOption::WordWrap：CJK 逐字可断、Latin 按词边界）。 */
    self->m_wordWrapMode = (int)XTextControlWrap_WordWrap;
    self->m_documentTitle = XString_create();
    self->m_extraSelCache = XVector_Create(XPlainTextEditExtraSelection);
    self->m_autoScrollTimer = XTIMER_INVALID_ID;
    /* 兼容镜像：行文本数组 + 撤销/重做哨兵栈（仅供既有消费方
       XTextEdit 只读查询；权威状态在控制器）。 */
    self->m_lines = XVector_Create(char*);
    self->m_undoStack = XVector_Create(char*);
    self->m_redoStack = XVector_Create(char*);
    /* 单空行初始文档（与控制器初始文档一致）。 */
    if (self->m_lines) {
        char* empty = (char*)XMalloc_System(1);
        if (empty) {
            empty[0] = '\0';
            XVector_push_back_1_base(self->m_lines, &empty);
        }
    }
    /* 私有文本控制器：init 创建（deinit 销毁），承载行模型/光标选区/
       撤销重做/IME/命中测试/菜单数据源（对标 Qt control 私有指针）。 */
    self->m_control = XTextControl_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_control) {
        XPalette palette;
        XFont font;
        xpe_syncInteractionFlags(self);
        /* 调色板/字体下发控制器（绘制取色与行高/基线度量随字体刷新）。 */
        palette = XWidget_palette((XWidget*)self);
        XTextControl_setPalette(self->m_control, &palette);
        font = XWidget_font((XWidget*)self);
        /* setFont 深拷贝字体（含 m_family/m_styleName），壳的深拷贝副本
           用完即释放。 */
        XTextControl_setFont(self->m_control, &font);
        XFont_deinit_base((XClass*)&font);
        /* 换行开关/断行规则下发控制器（折行行为由控制器承载）。 */
        XTextControl_setLineWrapMode(self->m_control, self->m_wrapMode);
        XTextControl_setWordWrapMode(self->m_control, self->m_wordWrapMode);
        xpe_connectControl(self);
    }
    /* 对标 QPlainTextEditPrivate::init 的 StrongFocus（qplaintextedit.cpp:790）：
       无焦点策略时键盘事件永远到不了控件，编辑功能名存实亡。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
    XWidget_resize(self, 240, 180);
    hint.width = 240;
    hint.height = 180;
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* 初始几何就绪后下发折行宽度（WidgetWidth = 视口文本区宽）。 */
    xpe_updateWrapWidth(self);
}

XPlainTextEdit* XPlainTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XPlainTextEdit* self = (XPlainTextEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPlainTextEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XPlainTextEdit_setPlainText(XPlainTextEdit* self, const char* utf8)
{
    const char* tail = utf8;
    char* heapCopy = NULL;
    if (!self || !self->m_control) return;
    /* 块数上限：装载前裁剪保留末尾 max 行（与原 xpe_afterChange 超限
       丢弃最旧行口径一致，避免控制器装载后再裁）。 */
    if (utf8 && self->m_maxBlockCount > 0) {
        const char* start = xpe_tailStart(utf8, self->m_maxBlockCount);
        if (start != utf8) {
            size_t n = XStrlen(start);
            heapCopy = (char*)XMalloc_System(n + 1);
            if (heapCopy) {
                XMemcpy(heapCopy, start, n + 1);
                tail = heapCopy;
            }
        }
    }
    XTextControl_setPlainText(self->m_control, tail);
    if (heapCopy) XFree_System(heapCopy);
}

char* XPlainTextEdit_toPlainText(const XPlainTextEdit* self)
{
    if (!self || !self->m_control) {
        char* out = (char*)XMalloc_System(1);
        if (out) out[0] = '\0';
        return out;
    }
    return XTextControl_toPlainText(self->m_control);
}

void XPlainTextEdit_appendPlainText(XPlainTextEdit* self, const char* utf8)
{
    if (!self || !self->m_control || !utf8) return;
    /* 对标控制器 append 语义：文末新起一段插入，光标不动（Qt 一致）。 */
    XTextControl_appendPlainText(self->m_control, utf8);
    xpe_enforceMaxBlockCount(self);
}

void XPlainTextEdit_insertPlainText(XPlainTextEdit* self, const char* utf8)
{
    if (!self || !self->m_control || !utf8) return;
    XTextControl_insertPlainText(self->m_control, utf8);
}

void XPlainTextEdit_clear(XPlainTextEdit* self)
{
    if (!self) return;
    XPlainTextEdit_setPlainText(self, "");
}

bool XPlainTextEdit_isReadOnly(const XPlainTextEdit* self)
{
    return self ? self->m_readOnly : false;
}

void XPlainTextEdit_setReadOnly(XPlainTextEdit* self, bool readOnly)
{
    if (!self || self->m_readOnly == readOnly) return;
    self->m_readOnly = readOnly;
    /* 只读映射控制器交互标志（剥掉 TextEditable，对标 Qt readOnly）。 */
    xpe_syncInteractionFlags(self);
}

int XPlainTextEdit_lineWrapMode(const XPlainTextEdit* self)
{
    return self ? self->m_wrapMode : 0;
}

void XPlainTextEdit_setLineWrapMode(XPlainTextEdit* self, int mode)
{
    if (!self || self->m_wrapMode == mode) return;
    self->m_wrapMode = mode;
    /* 对标 QPlainTextEdit::setLineWrapMode：开关下发控制器（重布局 +
       documentSize/updateRequest 通知），并同步折行宽度
       （WidgetWidth=视口文本区宽 / NoWrap=清除定宽）。 */
    if (self->m_control) XTextControl_setLineWrapMode(self->m_control, mode);
    xpe_updateWrapWidth(self);
    XWidget_update((XWidget*)self);
}

int XPlainTextEdit_maximumBlockCount(const XPlainTextEdit* self)
{
    return self ? self->m_maxBlockCount : 0;
}

void XPlainTextEdit_setMaximumBlockCount(XPlainTextEdit* self, int maximum)
{
    if (!self || maximum < 0) return;
    self->m_maxBlockCount = maximum;
    /* 立即裁剪（对标原 xpe_afterChange 的超限丢弃）。 */
    xpe_enforceMaxBlockCount(self);
}

void XPlainTextEdit_setPlaceholderText(XPlainTextEdit* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_placeholder) self->m_placeholder = XString_create();
    if (self->m_placeholder)
        XString_assign_utf8(self->m_placeholder, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

const char* XPlainTextEdit_placeholderText(const XPlainTextEdit* self)
{
    {
        const char* text;
        if (!self || !self->m_placeholder) return "";
        text = XString_toUtf8(self->m_placeholder);
        return text ? text : "";
    }
}

bool XPlainTextEdit_isUndoRedoEnabled(const XPlainTextEdit* self)
{
    return self ? self->m_undoEnabled : false;
}

void XPlainTextEdit_setUndoRedoEnabled(XPlainTextEdit* self, bool enable)
{
    if (!self) return;
    self->m_undoEnabled = enable;
    /* 对标控制器语义：关闭时清空撤销/重做历史（Qt 一致）。 */
    if (self->m_control)
        XTextControl_setUndoRedoEnabled(self->m_control, enable);
}

int XPlainTextEdit_cursorLine(const XPlainTextEdit* self)
{
    int pos = 0;
    int anchor = 0;
    int line = 0;
    if (!self || !self->m_control) return 0;
    XTextControl_textCursor(self->m_control, &pos, &anchor);
    xpe_ctlPosToLineCol(self->m_control, pos, &line, NULL);
    return line;
}

int XPlainTextEdit_cursorColumn(const XPlainTextEdit* self)
{
    int pos = 0;
    int anchor = 0;
    int col = 0;
    if (!self || !self->m_control) return 0;
    XTextControl_textCursor(self->m_control, &pos, &anchor);
    xpe_ctlPosToLineCol(self->m_control, pos, NULL, &col);
    return col;
}

/* ==================== 光标几何与查找（对标 QPlainTextEdit public API） ==== */

XRect XPlainTextEdit_cursorRect(const XPlainTextEdit* self)
{
    XRect rect;
    XScrollBar* vsb;
    XScrollBar* hsb;
    int vs = 0;
    int hs = 0;
    if (!self || !self->m_control) {
        XRect_init(&rect, 0, 0, 0, 0);
        return rect;
    }
    /* 矩形取自控制器（内容坐标），壳做视口换算：行左留白 2px、
       扣除垂直/水平滚动取值；输出为控件局部坐标。 */
    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)self);
    hsb = XAbstractScrollArea_horizontalScrollBar((XAbstractScrollArea*)self);
    if (vsb) vs = XScrollBar_value(vsb);
    if (hsb) hs = XScrollBar_value(hsb);
    rect = XTextControl_cursorRect(self->m_control);
    rect.x += XPE_TEXT_LEFT - hs;
    rect.y -= vs;
    return rect;
}

XString* XPlainTextEdit_anchorAt(const XPlainTextEdit* self,
                                 const XPoint* pos)
{
    if (!self || !self->m_control) return XString_create_utf8("");
    return XTextControl_anchorAt(self->m_control, pos);
}

bool XPlainTextEdit_find(XPlainTextEdit* self, const char* text, int flags)
{
    if (!self || !self->m_control) return false;
    /* 委托控制器 find：命中后选区即命中串（Qt 语义）。 */
    return XTextControl_find(self->m_control, text, flags);
}

void XPlainTextEdit_setTextCursor(XPlainTextEdit* self, int line, int col)
{
    XTextControl* ctl;
    int pos;
    if (!self || !self->m_control) return;
    ctl = self->m_control;
    if (XTextControl_lineCount(ctl) <= 0) return;
    pos = xpe_ctlLineColToPos(ctl, line, col);
    /* 无选区收拢（旧口径无选区语义）。 */
    XTextControl_setTextCursor(ctl, pos, pos, false);
}

int XPlainTextEdit_textCursorLine(const XPlainTextEdit* self)
{
    return XPlainTextEdit_cursorLine(self);
}

int XPlainTextEdit_textCursorColumn(const XPlainTextEdit* self)
{
    return XPlainTextEdit_cursorColumn(self);
}

void XPlainTextEdit_undo(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    XTextControl_undo(self->m_control);
}

void XPlainTextEdit_redo(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    XTextControl_redo(self->m_control);
}

void XPlainTextEdit_copy(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    /* 对标控制器 copy：仅有选区时执行（原全文复制口径由选区模型取代）。 */
    XTextControl_copy(self->m_control);
}

void XPlainTextEdit_cut(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    /* 对标控制器 cut：删除选区（原"剪切即清空全文"由选区模型取代）。 */
    XTextControl_cut(self->m_control);
}

void XPlainTextEdit_paste(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    XTextControl_paste(self->m_control);
}

void XPlainTextEdit_selectAll(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return;
    /* 对标控制器 selectAll：锚点 0 / 位置文档尾（原复位光标 (0,0) 的
       单布尔口径由锚点模型取代）。 */
    XTextControl_selectAll(self->m_control);
}

void XPlainTextEdit_ensureCursorVisible(XPlainTextEdit* self)
{
    XScrollBar* vsb;
    XTextControl* ctl;
    int pos = 0;
    int anchor = 0;
    int line = 0;
    int lh;
    int target;
    if (!self) return;
    vsb = XAbstractScrollArea_verticalScrollBar(
        (XAbstractScrollArea*)self);
    ctl = self->m_control;
    if (!vsb || !ctl) return;
    /* 壳保留滚动条数学（旧行顶对齐口径）：光标行顶 = line x 行高。 */
    XTextControl_textCursor(ctl, &pos, &anchor);
    xpe_ctlPosToLineCol(ctl, pos, &line, NULL);
    lh = xpe_ctlLineHeight(ctl);
    target = line * lh;
    XScrollBar_setValue(vsb, target);
}

void* XPlainTextEdit_textChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_textChanged_signal;
}

void* XPlainTextEdit_updateRequest_signal(XPlainTextEdit* self,
                                          const XRect* rect, int dy)
{
    XRect area;
    XVarList* args;

    if (!self)
        return (void*)(size_t)XPlainTextEdit_updateRequest_signal;
    if (rect)
        area = *rect;
    else
        area = xpe_viewportRect(self);
    if (((XObject*)self)->m_signalSlot) {
        args = XVarList_Create(XVar(XRect, area), XVar(int, dy));
        if (!args)
            return (void*)(size_t)XPlainTextEdit_updateRequest_signal;
        XObject_emitSignal((XObject*)self,
                           (size_t)XPlainTextEdit_updateRequest_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XPlainTextEdit_updateRequest_signal;
}

void* XPlainTextEdit_cursorPositionChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_cursorPositionChanged_signal;
}

/* ==================== 状态族与信号（2026-09-18 批次） ==================== */

int XPlainTextEdit_blockCount(const XPlainTextEdit* self)
{
    /* 对标 QPlainTextEdit::blockCount：逻辑块口径（不含软换行拆分）。 */
    return (self && self->m_control) ? xpe_ctlBlockCount(self->m_control) : 0;
}

int XPlainTextEdit_lineCount(const XPlainTextEdit* self)
{
    /* 对标 QPlainTextEdit::lineCount：可视行口径（折行后总行数）。 */
    return (self && self->m_control) ? XTextControl_lineCount(self->m_control)
                                     : 0;
}

bool XPlainTextEdit_canPaste(const XPlainTextEdit* self)
{
    /* 对标控制器 canPaste：可编辑且剪贴板有非空文本（原 !readOnly 口径
       由控制器统一承载）。 */
    return (self && self->m_control) ? XTextControl_canPaste(self->m_control)
                                     : false;
}

void XPlainTextEdit_setCursorWidth(XPlainTextEdit* self, int width)
{
    if (!self || width <= 0) return;
    if (self->m_control) XTextControl_setCursorWidth(self->m_control, width);
    XWidget_update((XWidget*)self);
}

int XPlainTextEdit_cursorWidth(const XPlainTextEdit* self)
{
    return (self && self->m_control) ? XTextControl_cursorWidth(self->m_control)
                                     : 1;
}

void XPlainTextEdit_setCenterCursor(XPlainTextEdit* self, bool center)
{ if (self) self->m_centerCursor = center; }

bool XPlainTextEdit_centerCursor(const XPlainTextEdit* self)
{ return self ? self->m_centerCursor : false; }

void XPlainTextEdit_setCenterOnScroll(XPlainTextEdit* self, bool on)
{ if (self) self->m_centerOnScroll = on; }

bool XPlainTextEdit_centerOnScroll(const XPlainTextEdit* self)
{ return self ? self->m_centerOnScroll : false; }

void XPlainTextEdit_setBackgroundVisible(XPlainTextEdit* self, bool visible)
{ if (self) { self->m_backgroundVisible = visible; XWidget_update((XWidget*)self); } }

bool XPlainTextEdit_backgroundVisible(const XPlainTextEdit* self)
{ return self ? self->m_backgroundVisible : true; }

void XPlainTextEdit_setTabChangesFocus(XPlainTextEdit* self, bool change)
{ if (self) self->m_tabChangesFocus = change; }

bool XPlainTextEdit_tabChangesFocus(const XPlainTextEdit* self)
{ return self ? self->m_tabChangesFocus : false; }

void XPlainTextEdit_setTabStopDistance(XPlainTextEdit* self, int distance)
{ if (self && distance > 0) self->m_tabStopDistance = distance; }

int XPlainTextEdit_tabStopDistance(const XPlainTextEdit* self)
{ return self ? self->m_tabStopDistance : 40; }

void XPlainTextEdit_setOverwriteMode(XPlainTextEdit* self, bool overwrite)
{
    if (self && self->m_control)
        XTextControl_setOverwriteMode(self->m_control, overwrite);
}

bool XPlainTextEdit_overwriteMode(const XPlainTextEdit* self)
{
    return (self && self->m_control)
               ? XTextControl_overwriteMode(self->m_control) : false;
}

void XPlainTextEdit_setWordWrapMode(XPlainTextEdit* self, int mode)
{
    if (!self || self->m_wordWrapMode == mode) return;
    self->m_wordWrapMode = mode;
    /* 对标 QTextOption::setWrapMode：断行规则下发控制器并触发重布局
       （documentSize/updateRequest 由控制器发射，随重绘生效）。 */
    if (self->m_control) XTextControl_setWordWrapMode(self->m_control, mode);
    XWidget_update((XWidget*)self);
}

int XPlainTextEdit_wordWrapMode(const XPlainTextEdit* self)
{ return self ? self->m_wordWrapMode : (int)XTextControlWrap_WordWrap; }

void XPlainTextEdit_setTextInteractionFlags(XPlainTextEdit* self, int flags)
{
    if (!self || self->m_textInteractionFlags == flags) return;
    self->m_textInteractionFlags = flags;
    xpe_syncInteractionFlags(self);
}

int XPlainTextEdit_textInteractionFlags(const XPlainTextEdit* self)
{ return self ? self->m_textInteractionFlags : 0; }

void XPlainTextEdit_setDocumentTitle(XPlainTextEdit* self, const XString* title)
{
    if (!self) return;
    if (!self->m_documentTitle) {
        self->m_documentTitle = XString_create();
        if (!self->m_documentTitle) return;
    }
    if (title) XString_assign(self->m_documentTitle, title);
    else XString_assign_utf8(self->m_documentTitle, "");
}

void XPlainTextEdit_setDocumentTitle_2(XPlainTextEdit* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_documentTitle) self->m_documentTitle = XString_create();
    if (!self->m_documentTitle) return;
    XString_assign_utf8(self->m_documentTitle, utf8 ? utf8 : "");
}

XString* XPlainTextEdit_documentTitle(const XPlainTextEdit* self)
{
    XString* out = XString_create();
    if (!out) return NULL;
    if (self && self->m_documentTitle) XString_assign(out, self->m_documentTitle);
    return out;
}

void XPlainTextEdit_moveCursor(XPlainTextEdit* self, int operation, int mode)
{
    if (!self || !self->m_control) return;
    /* operation 对标 QTextCursor::MoveOperation 数值（XTextControlMove-
       Operation），mode 对标 MoveMode；控制器内完成移动与选区扩展。 */
    XTextControl_moveCursor(self->m_control, operation, mode);
}

void XPlainTextEdit_appendHtml(XPlainTextEdit* self, const char* html)
{
    if (!self || !self->m_control) return;
    /* 委托控制器 HTML 子集（剥标签 + 锚点提取）。 */
    XTextControl_appendHtml(self->m_control, html);
}

void* XPlainTextEdit_undoAvailable_signal(XPlainTextEdit* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XPlainTextEdit_undoAvailable_signal; }

void* XPlainTextEdit_redoAvailable_signal(XPlainTextEdit* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XPlainTextEdit_redoAvailable_signal; }

void* XPlainTextEdit_copyAvailable_signal(XPlainTextEdit* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XPlainTextEdit_copyAvailable_signal; }

void* XPlainTextEdit_modificationChanged_signal(XPlainTextEdit* self, bool changed)
{ (void)self; (void)changed; return (void*)(size_t)XPlainTextEdit_modificationChanged_signal; }

void* XPlainTextEdit_blockCountChanged_signal(XPlainTextEdit* self, int newCount)
{ (void)self; (void)newCount; return (void*)(size_t)XPlainTextEdit_blockCountChanged_signal; }

/* ==================== 光标/格式/文档/额外选区/缩放（2026-09-17 批次） ==== */

XPoint XPlainTextEdit_cursorForPosition(const XPlainTextEdit* self,
                                        const XPoint* pos)
{
    XPoint result;
    XTextControl* ctl;
    XPoint content;
    int hit;
    int line = 0;
    int col = 0;
    result.x = 0;
    result.y = 0;
    if (!self || !pos || !self->m_control) return result;
    ctl = self->m_control;
    xpe_syncControlFont(self);
    /* 壳做 contents 平移后委托控制器命中测试（Qt 同构）。 */
    content = xpe_toContentPos(self, pos);
    hit = XTextControl_cursorForPosition(ctl, &content);
    if (hit < 0) return result;
    xpe_ctlPosToLineCol(ctl, hit, &line, &col);
    result.x = line;
    result.y = col;
    return result;
}

#if XMENU_ON
XMenu* XPlainTextEdit_createStandardContextMenu(XPlainTextEdit* self)
{
    if (!self || !self->m_control) return NULL;
    /* 菜单数据源（动作集合与灰化条件）迁控制器：撤销/重做/剪切/复制/
       粘贴/删除/全选，灰化读控制器状态；只读时控制器仅提供复制/全选。 */
    return XTextControl_createStandardContextMenu(self->m_control);
}
#endif /* XMENU_ON */

int XPlainTextEdit_currentCharFormat(const XPlainTextEdit* self)
{
    return (self && self->m_control)
               ? XTextControl_currentCharFormat(self->m_control) : 0;
}

void XPlainTextEdit_setCurrentCharFormat(XPlainTextEdit* self, int format)
{
    if (self && self->m_control)
        XTextControl_setCurrentCharFormat(self->m_control, format);
}

void XPlainTextEdit_mergeCurrentCharFormat(XPlainTextEdit* self, int format)
{
    /* @note 对标简化：Qt 按属性粒度合并（仅覆盖显式置位属性），此处
       以位值按位或覆盖承载（控制器同口径）。 */
    if (self && self->m_control)
        XTextControl_mergeCurrentCharFormat(self->m_control, format);
}

#if XTEXTDOCUMENT_ON
XTextDocument* XPlainTextEdit_document(const XPlainTextEdit* self)
{
    /* 借用语义：文档镜像所有权归控制器（析构统一释放）。 */
    return (self && self->m_control) ? XTextControl_document(self->m_control)
                                     : NULL;
}

void XPlainTextEdit_setDocument(XPlainTextEdit* self, XTextDocument* doc)
{
    if (!self || !self->m_control) return;
    /* 桥接保持既有口径：控制器接管所有权并把文档纯文本镜像进行模型；
       传 NULL 回退新建空文档（对标 Qt）。 */
    XTextControl_setDocument(self->m_control, doc);
}
#endif /* XTEXTDOCUMENT_ON */

int XPlainTextEdit_extraSelections(const XPlainTextEdit* self,
                                   const XPlainTextEditExtraSelection** selections)
{
    const XTextControlExtraSelection* items = NULL;
    int count;
    int i;
    int64_t n;
    if (selections) *selections = NULL;
    if (!self || !self->m_control || !self->m_extraSelCache) return 0;
    /* 控制器绝对区间 → (行, 列, 长度) 承载换算（借用缓存视图）。 */
    XVector_clear_base((XContainer*)self->m_extraSelCache);
    count = XTextControl_extraSelections(self->m_control, &items);
    for (i = 0; items && i < count; ++i) {
        XPlainTextEditExtraSelection entry;
        int line = 0;
        int col = 0;
        xpe_ctlPosToLineCol(self->m_control, items[i].start, &line, &col);
        entry.line = line;
        entry.col = col;
        entry.length = items[i].end > items[i].start
                           ? items[i].end - items[i].start : 0;
        entry.color = items[i].color;
        XVector_push_back_1_base(self->m_extraSelCache, &entry);
    }
    n = XVector_size_base((const XContainer*)self->m_extraSelCache);
    if (n > 0 && selections)
        *selections = (const XPlainTextEditExtraSelection*)XVector_at_base(
            self->m_extraSelCache, 0);
    return (int)n;
}

void XPlainTextEdit_setExtraSelections(XPlainTextEdit* self,
                                       const XPlainTextEditExtraSelection* selections,
                                       int count)
{
    XTextControlExtraSelection* converted = NULL;
    int i;
    if (!self || !self->m_control) return;
    if (count < 0) count = 0;
    if (count > 0)
        converted = (XTextControlExtraSelection*)XMalloc_System(
            (size_t)count * sizeof(XTextControlExtraSelection));
    for (i = 0; converted && selections && i < count; ++i) {
        int start = xpe_ctlLineColToPos(self->m_control, selections[i].line,
                                        selections[i].col);
        converted[i].start = start;
        converted[i].end = start +
                           (selections[i].length > 0 ? selections[i].length : 0);
        converted[i].color = selections[i].color;
    }
    /* 承载迁控制器（高亮由 XTextControl_draw 渲染）。 */
    XTextControl_setExtraSelections(self->m_control, converted,
                                    converted ? count : 0);
    if (converted) XFree_System(converted);
    XWidget_update((XWidget*)self);
}

XVariant* XPlainTextEdit_loadResource(XPlainTextEdit* self, int type,
                                      const char* name)
{
    /* 委托控制器：平铺模型无资源存储，恒返回 NULL（对标 Qt 默认实现）。 */
    if (!self || !self->m_control) return NULL;
    return XTextControl_loadResource(self->m_control, type, name);
}

XPoint XPlainTextEdit_textCursor(const XPlainTextEdit* self)
{
    XPoint result;
    int pos = 0;
    int anchor = 0;
    int line = 0;
    int col = 0;
    result.x = 0;
    result.y = 0;
    if (!self || !self->m_control) return result;
    XTextControl_textCursor(self->m_control, &pos, &anchor);
    xpe_ctlPosToLineCol(self->m_control, pos, &line, &col);
    result.x = line;
    result.y = col;
    return result;
}

/**
 * @brief      zoomIn/zoomOut 公共实现：按带符号增量调整字体大小。
 * @details    点大小与像素字号同步增减：点阵渲染路径按像素字号整倍
 *             缩放，仅改点大小不产生视觉变化，故两者同步钳位下限 1；
 *             字体变更同步下发控制器（行高/基线度量随字体刷新）。
 * @param      self 目标控件指针；NULL 或 delta 为 0 时无操作。
 * @param      delta 带符号增量（点数/像素数）。
 * @return     无返回值。
 */
static void xpe_zoomApply(XPlainTextEdit* self, int delta)
{
    XFont font;
    int ps;
    int px;
    if (!self || delta == 0) return;
    font = XWidget_font((XWidget*)self);
    ps = XFont_pointSize(&font);
    if (ps <= 0) ps = (int)XFONT_DEFAULT_POINT_SIZE;
    ps += delta;
    if (ps < 1) ps = 1;
    XFont_setPointSize(&font, ps);
    px = XFont_bitmapPixelSize(&font, XPE_FALLBACK_LINE_HEIGHT);
    px += delta;
    if (px < 1) px = 1;
    XFont_setPixelSize(&font, px);
    XWidget_setFont((XWidget*)self, &font);
    XTextControl_setFont(self->m_control, &font);
    XFont_deinit_base((XClass*)&font);
}

void XPlainTextEdit_zoomIn(XPlainTextEdit* self, int range)
{
    /* 对标 Qt：range 可为负（zoomIn 负值即缩小），仅钳位避免无操作。 */
    xpe_zoomApply(self, range);
}

void XPlainTextEdit_zoomOut(XPlainTextEdit* self, int range)
{
    xpe_zoomApply(self, -range);
}

/* ==================== 选区查询（2026-09-17 补齐批次） ==================== */

bool XPlainTextEdit_hasSelectedText(const XPlainTextEdit* self)
{
    int pos = 0;
    int anchor = 0;
    if (!self || !self->m_control) return false;
    XTextControl_textCursor(self->m_control, &pos, &anchor);
    /* 锚点/位置模型：位置与锚点不等即存在选区（可跨行）。 */
    return pos != anchor;
}

char* XPlainTextEdit_selectedText(const XPlainTextEdit* self)
{
    char* text;
    if (!self || !self->m_control) return NULL;
    text = XTextControl_createMimeDataFromSelection(self->m_control);
    if (!text) return NULL;
    if (text[0] == '\0') {
        /* 无选区时控制器返回空串：按既有契约导出 NULL。 */
        XFree_System(text);
        return NULL;
    }
    return text;
}

void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XPlainTextEdit_selectionChanged_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */
