/**
 * @file       XTextEdit.c
 * @brief      富文本编辑控件实现（对标 Qt 6.8 QTextEdit 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XTextEdit.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XTextDocument.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON

/* ==================== 生命周期与虚表 ==================== */

static void VX_textEdit_resizeEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    if (!te || !te->m_editor) return;
    XWidget_setGeometry((XWidget*)te->m_editor, 0, 0,
                        XWidget_width(self), XWidget_height(self));
}

static void VX_textEdit_paintEvent(XWidget* self, XEvent* event)
{
    XTextEdit* te = (XTextEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    int i, j, y = 0;
#if XTEXTDOCUMENT_ON
    uint32_t defaultColor = 0xFF000000u;
    XTextDocument* doc;
    if (!te || !event) return;
    doc = te->m_textDoc;
    if (!doc || !doc->m_blocks) return;
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
    r.x = 0; r.y = 0;
    r.width = XWidget_width(self);
    r.height = XWidget_height(self);
    /* 白色背景 */
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    /* 逐块→逐片段渲染 */
    for (i = 0; i < doc->m_blockCount; ++i) {
        XTDBlock* blk = &doc->m_blocks[i];
        int xStart = 2;
        if (blk->alignment & 0x04) xStart = (r.width - 100) / 2; /* 简化居中 */
        for (j = 0; j < blk->fragmentCount; ++j) {
            XTDFragment* f = &blk->fragments[j];
            uint32_t color = f->fmt.fgColor != 0 ? f->fmt.fgColor : defaultColor;
            XFont font = XWidget_font(self);
            XPainter_setFont(&painter, &font);
            if (f->fmt.bold) {
                /* 粗体：加深颜色模拟 */
                color = 0xFF000000u;
            XFont_deinit_base(&font);
            }
            XPainter_setPen(&painter, color);
            XPainter_drawText(&painter, xStart, y + 14, f->text, color);
            xStart += (int)XStrlen(f->text) * 8;
            if (f->fmt.underline) {
                XPainter_drawLine(&painter, xStart - (int)XStrlen(f->text) * 8, y + 16,
                                  xStart, y + 16);
            }
        }
        y += 18; /* 行高 */
    }
    XPainter_deinit(&painter);
#else
    {
        XRect r;
        XImage* image;
        XPainter painter;
        XPoint offset;
        image = XWidget_paintImage(self);
        if (!image) return;
        XPainter_init(&painter, NULL);
        if (!XPainter_begin_image(&painter, image)) return;
        offset = XWidget_paintOffset(self);
        if (offset.x != 0 || offset.y != 0)
            XPainter_translate(&painter, (float)offset.x, (float)offset.y);
        XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
        XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
        XPainter_deinit(&painter);
    }
#endif
}

static void VXTextEdit_deinit(XTextEdit* self)
{
    if (!self) return;
#if XTEXTDOCUMENT_ON
    /* 仅释放内部默认文档；setDocument 接管的外部文档所有权归调用方。 */
    if (self->m_textDoc && self->m_textDocOwned) {
        XClass_delete_base((XClass*)self->m_textDoc);
    }
    self->m_textDoc = NULL;
#endif
    if (self->m_fontFamily) {
        XString_delete_base(self->m_fontFamily);
        self->m_fontFamily = NULL;
    }
    if (self->m_documentTitle) {
        XString_delete_base(self->m_documentTitle);
        self->m_documentTitle = NULL;
    }
    if (self->m_markdown) {
        XString_delete_base(self->m_markdown);
        self->m_markdown = NULL;
    }
    if (self->m_editor) {
        XClass_delete_base((XClass*)self->m_editor);
        self->m_editor = NULL;
    }
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

XVtable* XTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_textEdit_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_textEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextEdit_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 内嵌编辑器信号桥（壳转接，真发射） ====================
 * 对标 Qt：QTextEdit 的 textChanged 等信号由内建设施发出。XTextEdit 以
 * 内嵌 XPlainTextEdit 承载编辑能力，此处把其信号转接为壳的同名信号。
 * 此前 8 个信号函数仅返回标识、无任何发射点（死信号）。 */

static void xte_fwdVoid(XObject* receiver, XVarList* args, size_t signal)
{
    XTextEdit* self = (XTextEdit*)receiver;
    if (!self) {
        if (args) XVarList_delete(args);
        return;
    }
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xte_fwdTextChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_textChanged_signal); }

static void xte_fwdCursorPositionChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_cursorPositionChanged_signal); }

static void xte_fwdSelectionChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_selectionChanged_signal); }

static void xte_fwdCopyAvailable(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_copyAvailable_signal); }

static void xte_fwdModificationChanged(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_modificationChanged_signal); }

static void xte_fwdUndoAvailable(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_undoAvailable_signal); }

static void xte_fwdRedoAvailable(XObject* receiver, XVarList* args)
{ xte_fwdVoid(receiver, args, (size_t)XTextEdit_redoAvailable_signal); }

static void xte_connectEditorSignals(XTextEdit* self)
{
    XObject* ed;
    if (!self || !self->m_editor) return;
    ed = (XObject*)self->m_editor;
#define XTE_CONNECT(sig, slot) \
    XObject_connect_1(ed, (size_t)(sig), (XObject*)self, (slot), \
                      XConnectionType_Direct)
    XTE_CONNECT(XPlainTextEdit_textChanged_signal(self->m_editor),
                xte_fwdTextChanged);
    XTE_CONNECT(XPlainTextEdit_cursorPositionChanged_signal(self->m_editor),
                xte_fwdCursorPositionChanged);
    XTE_CONNECT(XPlainTextEdit_selectionChanged_signal(self->m_editor),
                xte_fwdSelectionChanged);
    XTE_CONNECT(XPlainTextEdit_copyAvailable_signal(self->m_editor, true),
                xte_fwdCopyAvailable);
    XTE_CONNECT(XPlainTextEdit_modificationChanged_signal(self->m_editor,
                                                          false),
                xte_fwdModificationChanged);
    XTE_CONNECT(XPlainTextEdit_undoAvailable_signal(self->m_editor, false),
                xte_fwdUndoAvailable);
    XTE_CONNECT(XPlainTextEdit_redoAvailable_signal(self->m_editor, false),
                xte_fwdRedoAvailable);
#undef XTE_CONNECT
}

void XTextEdit_init(XTextEdit* self, XWidget* parent,
                           XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    self->m_editor = XPlainTextEdit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
    XWidget_resize((XWidget*)self->m_editor, 200, 100);
    xte_connectEditorSignals(self);
#if XTEXTDOCUMENT_ON
    self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    self->m_textDocOwned = true; /* 内部默认文档：拥有并负责释放。 */
#endif
    XClassSetVtable(self, XTextEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_textColor = 0xFF000000u;
    self->m_alignment = 1; /* AlignLeft */

    self->m_fontFamily = XString_create();
    self->m_documentTitle = XString_create();
    self->m_markdown = XString_create();
    self->m_fontWeight = 400;
    self->m_fontPointSize = 10.0;
    self->m_tabStopDistance = 80.0;
    self->m_cursorWidth = 1;
    self->m_lineWrapMode = 0;
    self->m_lineWrapColumnOrWidth = 0; /* 对标 Qt 默认值 0。 */
    self->m_wordWrapMode = 1;
    self->m_acceptRichText = true;
    self->m_autoFormatting = 0;
    self->m_centerOnScroll = false;
    self->m_textBackgroundColor = 0;
}

XTextEdit* XTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XTextEdit* self = (XTextEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 字符格式存取 ==================== */

void XTextEdit_setBold(XTextEdit* self, bool bold) { if (self) self->m_bold = bold; }
bool XTextEdit_isBold(const XTextEdit* self) { return self ? self->m_bold : false; }
void XTextEdit_setItalic(XTextEdit* self, bool italic) { if (self) self->m_italic = italic; }
bool XTextEdit_isItalic(const XTextEdit* self) { return self ? self->m_italic : false; }
void XTextEdit_setUnderline(XTextEdit* self, bool underline) { if (self) self->m_underline = underline; }
bool XTextEdit_isUnderline(const XTextEdit* self) { return self ? self->m_underline : false; }
void XTextEdit_setTextColor(XTextEdit* self, uint32_t color) { if (self) self->m_textColor = color; }
uint32_t XTextEdit_textColor(const XTextEdit* self) { return self ? self->m_textColor : 0; }
void XTextEdit_setAlignment(XTextEdit* self, int alignment) { if (self) self->m_alignment = alignment; }
int XTextEdit_alignment(const XTextEdit* self) { return self ? self->m_alignment : 0; }

/* ==================== HTML 解析（基础子集） ==================== */

static void xte_skipTag(const char** p) { while (**p && **p != '>') ++(*p); if (**p) ++(*p); }

/** @brief 清除 setMarkdown 承载的原文（内容被替换类接口重置后调用）。
 * @param self 目标控件指针；可为 NULL。
 * @return 无返回值。
 */
static void xte_resetMarkdown(XTextEdit* self)
{
    if (!self || !self->m_markdown) return;
    XString_assign_utf8(self->m_markdown, "");
}

/** @brief 以内嵌编辑器当前文本刷新富文本文档（纯文本口径同步）。
 * @details 与 toHtml 导出前的刷新同源：编辑器是文本写入源，文档按
 *          纯文本口径重建，保证文档渲染/导出反映显示内容。
 * @param self 目标控件指针；NULL、文档或内嵌编辑器缺失时无操作。
 * @return 无返回值。
 */
static void xte_syncDocFromEditor(XTextEdit* self)
{
#if XTEXTDOCUMENT_ON
    char* plain;
    if (!self || !self->m_textDoc || !self->m_editor) return;
    plain = XPlainTextEdit_toPlainText(self->m_editor);
    if (!plain) return;
    XTextDocument_setPlainText(self->m_textDoc, plain);
    XFree_System(plain);
#else
    (void)self;
#endif
}

/** @brief 剥离 HTML 标签得到纯文本（b/i/u/br/p 子集，setHtml 与
 *         insertHtml 共用同一口径）。
 * @details 识别 b/i/u 开闭标签跟踪行内格式终态；br/p 视为换行；其余
 *          标签整体丢弃；实体（&amp; 等）不展开原样拷贝；输出截断到
 *          cap-1 字节。
 * @param html 输入 HTML；不为 NULL。
 * @param plain 输出缓冲；不为 NULL。
 * @param cap 输出缓冲容量（含 NUL）。
 * @param bold 粗体终态输出；可为 NULL 忽略。
 * @param italic 斜体终态输出；可为 NULL 忽略。
 * @param underline 下划线终态输出；可为 NULL 忽略。
 * @return 无返回值。
 */
static void xte_htmlStripToPlain(const char* html, char* plain, size_t cap,
                                 bool* bold, bool* italic, bool* underline)
{
    const char* p = html;
    size_t o = 0;
    bool b = false, i = false, u = false;
    if (!html || !plain || cap == 0) return;
    while (*p && o < cap - 1) {
        if (*p == '<') {
            ++p;
            if (XStrncmp(p, "b>", 2) == 0) { b = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/b>", 3) == 0) { b = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "i>", 2) == 0) { i = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/i>", 3) == 0) { i = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "u>", 2) == 0) { u = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/u>", 3) == 0) { u = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "br", 2) == 0 || XStrncmp(p, "p", 1) == 0) { plain[o++] = '\n'; xte_skipTag(&p); }
            else xte_skipTag(&p);
        } else {
            plain[o++] = *p++;
        }
    }
    plain[o] = '\0';
    if (bold) *bold = b;
    if (italic) *italic = i;
    if (underline) *underline = u;
}

void XTextEdit_setHtml(XTextEdit* self, const char* html)
{
    char plain[4096];
    bool bold = false, italic = false, underline = false;
    if (!self || !html) return;
    xte_htmlStripToPlain(html, plain, sizeof(plain), &bold, &italic, &underline);
    self->m_bold = bold;
    self->m_italic = italic;
    self->m_underline = underline;
    /* 内容整体被替换：Markdown 原文承载失效，一并清除。 */
    xte_resetMarkdown(self);
    if (self->m_editor) XPlainTextEdit_setPlainText(self->m_editor, plain);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) XTextDocument_setHtml(self->m_textDoc, html);
#endif
}

char* XTextEdit_toHtml(const XTextEdit* self)
{
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) {
        /* 编辑器与文档可能被分别写入（setPlainText 只改编辑器）；
         * 导出前先以编辑器当前文本刷新文档，保证 toHtml 反映显示内容。 */
        char* plain = XPlainTextEdit_toPlainText(self->m_editor);
        if (plain) {
            XTextDocument_setPlainText(self->m_textDoc, plain);
            XFree_System(plain);
        }
        return XTextDocument_toHtml(self->m_textDoc);
    }
#endif
    char* plain;
    size_t cap;
    char* html;
    size_t o = 0;
    int i;
    if (!self) return NULL;
    plain = XPlainTextEdit_toPlainText(self->m_editor);
    if (!plain) return NULL;
    cap = XStrlen(plain) * 8 + 128;
    html = (char*)XMalloc_System(cap);
    if (!html) { XFree_System(plain); return NULL; }
    o = (size_t)XSnprintf(html, cap, "<html><body>");
    for (i = 0; plain[i]; ++i) {
        if (plain[i] == '\n') o += (size_t)XSnprintf(html + o, cap - o, "<br>");
        else if (plain[i] == '<') o += (size_t)XSnprintf(html + o, cap - o, "&lt;");
        else if (plain[i] == '>') o += (size_t)XSnprintf(html + o, cap - o, "&gt;");
        else if (plain[i] == '&') o += (size_t)XSnprintf(html + o, cap - o, "&amp;");
        else o += (size_t)XSnprintf(html + o, cap - o, "%c", plain[i]);
    }
    o += (size_t)XSnprintf(html + o, cap - o, "</body></html>");
    XFree_System(plain);
    return html;
}

/* ==================== 纯文本 / setText 自动探测 ==================== */

/** @brief Qt::mightBeRichText 子集启发式：判定文本是否"像"富文本。
 * @details 跳过前导空白后定位首个 '<'（遇 '\n' 放弃），检查其到首个 '>'
 *          之间是否构成标签状构造：'!'/'?' 开头视为注释/处理指令；或
 *          可选 '/' 后跟至少一个字母/数字；出现其他字符则判为普通文本。
 * @param text 待判定 UTF-8 文本；可为 NULL。
 * @return 判定为富文本返回 true，否则返回 false。
 */
static bool xte_mightBeRichText(const char* text)
{
    const char* open;
    const char* close;
    const char* q;
    bool sawName = false;
    if (!text) return false;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        ++text;
    open = text;
    while (*open && *open != '<' && *open != '\n') ++open;
    if (*open != '<') return false;
    close = open + 1;
    while (*close && *close != '>') ++close;
    if (*close != '>') return false;
    q = open + 1;
    if (*q == '!' || *q == '?') return true;
    if (*q == '/') ++q;
    while (*q && *q != '>' && *q != ' ' && *q != '\t'
           && *q != '\r' && *q != '\n' && *q != '/') {
        if (((*q >= 'a') && (*q <= 'z')) || ((*q >= 'A') && (*q <= 'Z'))
            || (*q >= '0' && *q <= '9')) {
            sawName = true;
            ++q;
        } else {
            return false;
        }
    }
    return sawName;
}

XString* XTextEdit_toPlainText(const XTextEdit* self)
{
    XString* out = XString_create();
    char* plain;
    if (!out) return NULL;
    if (!self || !self->m_editor) return out;
    plain = XPlainTextEdit_toPlainText(self->m_editor);
    if (plain) {
        XString_assign_utf8(out, plain);
        XFree_System(plain);
    }
    return out;
}

void XTextEdit_setText(XTextEdit* self, const char* text)
{
    if (!self || !text) return;
    if (xte_mightBeRichText(text)) {
        XTextEdit_setHtml(self, text);
        return;
    }
    /* 纯文本分支：统一走 setPlainText（复位字符格式/清除 Markdown
     * 原文/富文本文档同步，语义与原实现一致）。 */
    XTextEdit_setPlainText(self, text);
}

/* ==================== 撤销 / 重做 ==================== */

void XTextEdit_undo(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    XPlainTextEdit_undo(self->m_editor);
}

void XTextEdit_redo(XTextEdit* self)
{
    if (!self || !self->m_editor) return;
    XPlainTextEdit_redo(self->m_editor);
}

bool XTextEdit_canUndo(const XTextEdit* self)
{
    if (!self || !self->m_editor || !self->m_editor->m_undoStack) return false;
    return XVector_size_base((const XContainer*)self->m_editor->m_undoStack) > 0;
}

bool XTextEdit_canRedo(const XTextEdit* self)
{
    if (!self || !self->m_editor || !self->m_editor->m_redoStack) return false;
    return XVector_size_base((const XContainer*)self->m_editor->m_redoStack) > 0;
}

/* ==================== 信号 ==================== */

void* XTextEdit_textChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_textChanged_signal;
}


void* XTextEdit_copyAvailable_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_copyAvailable_signal;
}
void* XTextEdit_cursorPositionChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_cursorPositionChanged_signal;
}
void* XTextEdit_modificationChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_modificationChanged_signal;
}
void* XTextEdit_redoAvailable_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_redoAvailable_signal;
}
void* XTextEdit_selectionChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_selectionChanged_signal;
}
void* XTextEdit_undoAvailable_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_undoAvailable_signal;
}

void XTextEdit_append(XTextEdit* self, const char* text) { XPlainTextEdit_appendPlainText(&self->m_editor->m_base, text); }
void XTextEdit_copy_2(XTextEdit* self) { XPlainTextEdit_copy(&self->m_editor->m_base); }
void XTextEdit_cut_2(XTextEdit* self) { XPlainTextEdit_cut(&self->m_editor->m_base); }
void XTextEdit_paste_2(XTextEdit* self) { XPlainTextEdit_paste(&self->m_editor->m_base); }
void XTextEdit_clear_2(XTextEdit* self) { XPlainTextEdit_clear(&self->m_editor->m_base); }
void XTextEdit_selectAll_2(XTextEdit* self) { XPlainTextEdit_selectAll(&self->m_editor->m_base); }
bool XTextEdit_canPaste(XTextEdit* self) { return XPlainTextEdit_isReadOnly(&self->m_editor->m_base) ? false : true; }
void XTextEdit_setAcceptRichText(XTextEdit* self, bool accept) { if (self) self->m_acceptRichText = accept; }
bool XTextEdit_acceptRichText(const XTextEdit* self) { return self ? self->m_acceptRichText : true; }
void XTextEdit_setTextBackgroundColor(XTextEdit* self, uint32_t color) { if (self) self->m_textBackgroundColor = color; }
uint32_t XTextEdit_textBackgroundColor(const XTextEdit* self) { return self ? self->m_textBackgroundColor : 0; }
void XTextEdit_setFontFamily(XTextEdit* self, const char* family)
{
    if (!self) return;
    if (!self->m_fontFamily) self->m_fontFamily = XString_create();
    if (self->m_fontFamily)
        XString_assign_utf8(self->m_fontFamily, family ? family : "");
}
const char* XTextEdit_fontFamily(const XTextEdit* self)
{
    if (!self || !self->m_fontFamily) return "";
    return XString_toUtf8(self->m_fontFamily);
}
void XTextEdit_setFontWeight(XTextEdit* self, int weight) { if (self && weight > 0) self->m_fontWeight = weight; }
int XTextEdit_fontWeight(const XTextEdit* self) { return self ? self->m_fontWeight : 400; }
void XTextEdit_setFontPointSize(XTextEdit* self, double size) { if (self && size > 0) self->m_fontPointSize = size; }
double XTextEdit_fontPointSize(const XTextEdit* self) { return self ? self->m_fontPointSize : 10.0; }
void XTextEdit_setCurrentFont(XTextEdit* self, const char* family) { XTextEdit_setFontFamily(self, family); }

XFont XTextEdit_currentFont(const XTextEdit* self)
{
    XFont font;
    if (!self) {
        XFont_init(&font);
        return font;
    }
    /* 对标 QTextEdit::currentFont：Qt 返回光标处字符格式字体；本库为
       整篇单格式，即当前字体属性组合。字号按四舍五入收敛为整型点值
       （XFont_init_ex 口径）。 */
    XFont_init_ex(&font, XTextEdit_fontFamily(self),
                  (int)(self->m_fontPointSize > 0.0
                            ? self->m_fontPointSize + 0.5
                            : 10),
                  self->m_fontWeight, XTextEdit_isItalic(self));
    XFont_setUnderline(&font, XTextEdit_isUnderline(self));
    return font;
}
void XTextEdit_zoomIn(XTextEdit* self, int range) { if (self) { self->m_fontPointSize += (range > 0 ? range : 1); if (self->m_fontPointSize > 100) self->m_fontPointSize = 100; } }
void XTextEdit_zoomOut(XTextEdit* self, int range) { if (self) { self->m_fontPointSize -= (range > 0 ? range : 1); if (self->m_fontPointSize < 1) self->m_fontPointSize = 1; } }
void XTextEdit_setTabStopDistance(XTextEdit* self, double distance) { if (self && distance >= 0) self->m_tabStopDistance = distance; }
double XTextEdit_tabStopDistance(const XTextEdit* self) { return self ? self->m_tabStopDistance : 80.0; }
void XTextEdit_setAutoFormatting(XTextEdit* self, int features) { if (self) self->m_autoFormatting = features; }
int XTextEdit_autoFormatting(const XTextEdit* self) { return self ? self->m_autoFormatting : 0; }
void XTextEdit_setTabChangesFocus(XTextEdit* self, bool b) { (void)self; (void)b; /* 键盘焦点链由 XWidget 统一管理；存储位预留。 */ }
bool XTextEdit_tabChangesFocus(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setDocumentTitle(XTextEdit* self, const char* title)
{
    if (!self) return;
    if (!self->m_documentTitle) self->m_documentTitle = XString_create();
    if (self->m_documentTitle)
        XString_assign_utf8(self->m_documentTitle, title ? title : "");
}
const char* XTextEdit_documentTitle(const XTextEdit* self)
{
    if (!self || !self->m_documentTitle) return "";
    return XString_toUtf8(self->m_documentTitle);
}
void XTextEdit_setUndoRedoEnabled_2(XTextEdit* self, bool enable) { XPlainTextEdit_setUndoRedoEnabled(&self->m_editor->m_base, enable); }
bool XTextEdit_isUndoRedoEnabled_2(const XTextEdit* self) { return XPlainTextEdit_isUndoRedoEnabled(&self->m_editor->m_base); }
void XTextEdit_setLineWrapMode(XTextEdit* self, int mode) { if (self) self->m_lineWrapMode = mode; }
int XTextEdit_lineWrapMode(const XTextEdit* self) { return self ? self->m_lineWrapMode : 0; }
void XTextEdit_setWordWrapMode(XTextEdit* self, int policy) { if (self) self->m_wordWrapMode = policy; }
int XTextEdit_wordWrapMode(const XTextEdit* self) { return self ? self->m_wordWrapMode : 1; }
void XTextEdit_setReadOnly_2(XTextEdit* self, bool ro) { XPlainTextEdit_setReadOnly(&self->m_editor->m_base, ro); }
bool XTextEdit_isReadOnly_2(const XTextEdit* self) { return XPlainTextEdit_isReadOnly(&self->m_editor->m_base); }
void XTextEdit_setPlaceholderText_2(XTextEdit* self, const char* text) { XPlainTextEdit_setPlaceholderText(&self->m_editor->m_base, text); }
const char* XTextEdit_placeholderText_2(const XTextEdit* self) { return XPlainTextEdit_placeholderText(&self->m_editor->m_base); }
void XTextEdit_ensureCursorVisible_2(XTextEdit* self) { XPlainTextEdit_ensureCursorVisible(&self->m_editor->m_base); }
void XTextEdit_setCenterOnScroll(XTextEdit* self, bool enabled) { if (self) self->m_centerOnScroll = enabled; }
bool XTextEdit_centerOnScroll(const XTextEdit* self) { return self ? self->m_centerOnScroll : false; }
void XTextEdit_setExtraSelections(XTextEdit* self, void* selections) { (void)self; (void)selections; }
void XTextEdit_setBackgroundVisible(XTextEdit* self, bool visible) { (void)self; (void)visible; }
bool XTextEdit_backgroundVisible(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setTextCursor_2(XTextEdit* self, void* cursor) { (void)self; (void)cursor; }
void* XTextEdit_textCursor(const XTextEdit* self) { (void)self; return NULL; }
void XTextEdit_setCursorWidth(XTextEdit* self, int width) { if (self && width > 0) self->m_cursorWidth = width; }
int XTextEdit_cursorWidth(const XTextEdit* self) { return self ? self->m_cursorWidth : 1; }
/* ==================== 光标几何与查找（对标 QPlainTextEdit 同组 API） ==== */

bool XTextEdit_find_2(XTextEdit* self, const char* exp, int flags)
{
    bool hit;
    if (!self || !self->m_editor) return false;
    /* 委托内嵌编辑器：命中置光标并请求编辑器重绘（XPlainTextEdit_find
     * 内部已 update）；外层控件富文本文档渲染同步刷新一次。 */
    hit = XPlainTextEdit_find(self->m_editor, exp, flags);
    if (hit) XWidget_update((XWidget*)self);
    return hit;
}

XRect XTextEdit_cursorRect(const XTextEdit* self)
{
    /* 与内嵌编辑器 paintEvent 同口径：编辑器常驻 (0,0) 铺满本控件，
     * 其局部坐标即本控件局部坐标；NULL 逐层兜底返回零矩形。 */
    return XPlainTextEdit_cursorRect(self ? self->m_editor : NULL);
}

XString* XTextEdit_anchorAt(const XTextEdit* self, const XPoint* pos)
{
#if XTEXTDOCUMENT_ON
    const XTextDocument* doc;
    int i;
    int y = 0;
    if (!self || !pos) return XString_create_utf8("");
    doc = self->m_textDoc;
    if (!doc || !doc->m_blocks) return XString_create_utf8("");
    /* 命中几何与富文本绘制路径（VX_textEdit_paintEvent）逐段同口径：
     * 块带高 18、基线 y+14、起笔 x=2（居中简化 (width-100)/2）、
     * 片段步进 strlen*8。锚点承载于片段 anchorHref（空=无链接）。 */
    for (i = 0; i < doc->m_blockCount; ++i) {
        const XTDBlock* blk = &doc->m_blocks[i];
        if (blk->fragmentCount > 0 && pos->y >= y && pos->y < y + 18) {
            int j;
            int xStart = 2;
            if (blk->alignment & 0x04)
                xStart = (XWidget_width((const XWidget*)self) - 100) / 2;
            for (j = 0; j < blk->fragmentCount; ++j) {
                const XTDFragment* f = &blk->fragments[j];
                const char* txt = f->text ? XString_toUtf8(f->text) : "";
                int w = (int)XStrlen(txt) * 8;
                const char* href = (f->fmt.anchorHref && w > 0)
                                       ? XString_toUtf8(f->fmt.anchorHref)
                                       : NULL;
                if (href && href[0] && pos->x >= xStart &&
                    pos->x < xStart + w)
                    return XString_create_copy(f->fmt.anchorHref);
                xStart += w;
            }
            return XString_create_utf8(""); /* 块带内未命中：无链接。 */
        }
        y += 18; /* 行高（与绘制一致）。 */
    }
    return XString_create_utf8("");
#else
    (void)self;
    (void)pos;
    return XString_create_utf8("");
#endif
}

void XTextEdit_setTextCursor(XTextEdit* self, int line, int col)
{
    if (!self || !self->m_editor) return;
    /* 委托内嵌编辑器钳位并置光标（内部已 update）；外层同步刷新。 */
    XPlainTextEdit_setTextCursor(self->m_editor, line, col);
    XWidget_update((XWidget*)self);
}

int XTextEdit_textCursorLine(const XTextEdit* self)
{
    if (!self || !self->m_editor) return 0;
    return XPlainTextEdit_textCursorLine(self->m_editor);
}

int XTextEdit_textCursorColumn(const XTextEdit* self)
{
    if (!self || !self->m_editor) return 0;
    return XPlainTextEdit_textCursorColumn(self->m_editor);
}

/* ==================== 文档/插入/资源/Markdown 补齐组（对标 QTextEdit） ==== */

/** @brief 内嵌编辑器行高（与 XPlainTextEdit.c 的 XPE_LINE_HEIGHT 同口径）。 */
#define XTE_LINE_HEIGHT 16
/** @brief 行左留白（与 XPlainTextEdit 绘制/cursorRect 口径一致）。 */
#define XTE_LEFT_MARGIN 2

/** @brief 读取内嵌编辑器第 index 行文本（只读借用，不转移所有权）。
 * @param editor 内嵌编辑器指针；可为 NULL。
 * @param index 行号（0 起；越界返回空串）。
 * @return 行文本借用指针（编辑器行数组元素）；无效输入返回 ""。
 */
static const char* xte_editorLineAt(const XPlainTextEdit* editor, int index)
{
    char** item;
    if (!editor || !editor->m_lines || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)editor->m_lines))
        return "";
    item = (char**)XVector_at_base(editor->m_lines, index);
    return (item && *item) ? *item : "";
}

/** @brief 读取内嵌编辑器垂直滚动条取值（像素口径，同 cursorRect）。
 * @param editor 内嵌编辑器指针；可为 NULL。
 * @return 垂直滚动取值；滚动条缺失返回 0。
 */
static int xte_editorScrollY(const XPlainTextEdit* editor)
{
    XScrollBar* vsb;
    if (!editor) return 0;
    vsb = XAbstractScrollArea_verticalScrollBar(
        (const XAbstractScrollArea*)editor);
    return vsb ? XScrollBar_value(vsb) : 0;
}

/** @brief 按 UTF-8 首字节返回该码点的字节长度（非法首字节按 1 步进）。
 * @param lead 首字节。
 * @return 1-4 字节长度。
 */
static int xte_utf8CharLen(unsigned char lead)
{
    if ((lead & 0x80u) == 0) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;
}

XPoint XTextEdit_cursorForPosition(const XTextEdit* self, const XPoint* pos)
{
    XPoint pt;
    const XPlainTextEdit* editor;
    const char* line;
    XFont font;
    int scroll;
    int lineCount;
    int lineIdx;
    int relX;
    int off;
    pt.x = 0;
    pt.y = 0;
    if (!self || !pos) return pt;
    editor = self->m_editor;
    if (!editor) return pt;
    lineCount = XPlainTextEdit_blockCount(editor);
    if (lineCount <= 0) return pt;
    /* Y 反查：与 cursorRect 的 y = 行号*行高 - 滚动取值 同口径求逆。 */
    scroll = xte_editorScrollY(editor);
    lineIdx = (pos->y + scroll) / XTE_LINE_HEIGHT;
    if (lineIdx < 0) lineIdx = 0;
    if (lineIdx > lineCount - 1) lineIdx = lineCount - 1;
    line = xte_editorLineAt(editor, lineIdx);
    /* X 反查：减去行左留白后逐字符累加宽度（字节偏移口径），定位
     * 光标列；与 setTextCursor 的钳位规则一致可往返配合。 */
    font = XWidget_fontMetrics((const XWidget*)editor);
    relX = pos->x - XTE_LEFT_MARGIN;
    off = 0;
    while (line[off] != '\0' && relX > 0) {
        int adv = xte_utf8CharLen((unsigned char)line[off]);
        int w = XPainter_textWidthRange(&font, line, off, off + adv);
        if (relX < w) break;
        relX -= w;
        off += adv;
    }
    pt.x = lineIdx; /* x 分量承载行号（0 起）。 */
    pt.y = off;     /* y 分量承载列（行内 UTF-8 字节偏移）。 */
    return pt;
}

void XTextEdit_insertPlainText(XTextEdit* self, const char* text)
{
    if (!self || !self->m_editor || !text) return;
    /* 委托内嵌编辑器在光标处插入（支持 \n 跨行），随后文档纯文本口径
     * 同步并请求重绘，保持文档与显示一致。 */
    XPlainTextEdit_insertPlainText(self->m_editor, text);
    xte_syncDocFromEditor(self);
    XWidget_update((XWidget*)self);
}

void XTextEdit_insertHtml(XTextEdit* self, const char* html)
{
    char plain[4096];
    if (!self || !html) return;
    /* 与 setHtml 同口径剥离标签，仅保留纯文本后按光标处插入。 */
    xte_htmlStripToPlain(html, plain, sizeof(plain), NULL, NULL, NULL);
    XTextEdit_insertPlainText(self, plain);
}

int XTextEdit_lineWrapColumnOrWidth(const XTextEdit* self)
{
    return self ? self->m_lineWrapColumnOrWidth : 0;
}

void XTextEdit_setLineWrapColumnOrWidth(XTextEdit* self, int w)
{
    if (!self) return;
    /* 状态承载：平铺模型第一版不换行绘制，仅存储不触发重排。 */
    self->m_lineWrapColumnOrWidth = w;
}

XVariant* XTextEdit_loadResource(XTextEdit* self, int type, const char* name)
{
    /* 资源体系未建：对标 Qt 默认实现返回无效 QVariant 的语义，以 NULL
     * 承载；type/name 仅保持签名一致。 */
    (void)self;
    (void)type;
    (void)name;
    return NULL;
}

void XTextEdit_mergeCurrentCharFormat(XTextEdit* self, int format)
{
    if (!self) return;
    /* 位集合并：仅置位方向的属性生效，未置位属性保持不变。 */
    if (format & XTextEditCharFormat_Bold) self->m_bold = true;
    if (format & XTextEditCharFormat_Italic) self->m_italic = true;
    if (format & XTextEditCharFormat_Underline) self->m_underline = true;
    /* 信号为标识桩：保留 currentCharFormatChanged 接线点。 */
    XTextEdit_currentCharFormatChanged_signal(self);
}

void XTextEdit_setCurrentCharFormat(XTextEdit* self, int format)
{
    if (!self) return;
    /* 整体替换：置位属性开、未置位属性关（对标 setCharFormat）。 */
    self->m_bold = (format & XTextEditCharFormat_Bold) ? true : false;
    self->m_italic = (format & XTextEditCharFormat_Italic) ? true : false;
    self->m_underline = (format & XTextEditCharFormat_Underline) ? true : false;
    XTextEdit_currentCharFormatChanged_signal(self);
}

void XTextEdit_scrollToAnchor(XTextEdit* self, const char* anchor)
{
    /* 锚点几何未建：对标 Qt 接口存在性，当前为无操作。 */
    (void)self;
    (void)anchor;
}

#if XTEXTDOCUMENT_ON
void XTextEdit_setDocument(XTextEdit* self, XTextDocument* doc)
{
    if (!self || doc == self->m_textDoc) return;
    /* 释放原内部默认文档；外部接管文档所有权归调用方。 */
    if (self->m_textDoc && self->m_textDocOwned)
        XClass_delete_base((XClass*)self->m_textDoc);
    if (doc) {
        self->m_textDoc = doc;      /* 外部接管：不拥有。 */
        self->m_textDocOwned = false;
    } else {
        /* 对标 setDocument(nullptr)：重置为内部默认文档。 */
        self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        self->m_textDocOwned = true;
    }
    /* 显示以新文档为准：编辑器内容同步为新文档纯文本。 */
    if (self->m_textDoc && self->m_editor) {
        char* plain = XTextDocument_toPlainText(self->m_textDoc);
        if (plain) {
            XPlainTextEdit_setPlainText(self->m_editor, plain);
            XFree_System(plain);
        }
    }
    XWidget_update((XWidget*)self);
}

XTextDocument* XTextEdit_document(const XTextEdit* self)
{
    return self ? self->m_textDoc : NULL;
}
#endif /* XTEXTDOCUMENT_ON */

void XTextEdit_setMarkdown(XTextEdit* self, const char* md)
{
    const char* text = (md != NULL) ? md : "";
    if (!self) return;
    /* 原文承载：markdown()/toMarkdown() 返回该原文。 */
    if (!self->m_markdown) self->m_markdown = XString_create();
    if (self->m_markdown) XString_assign_utf8(self->m_markdown, text);
    /* 显示降级：Markdown 解析未建，按纯文本写入并同步文档。 */
    self->m_bold = false;
    self->m_italic = false;
    self->m_underline = false;
    if (self->m_editor) XPlainTextEdit_setPlainText(self->m_editor, text);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) XTextDocument_setPlainText(self->m_textDoc, text);
#endif
    XWidget_update((XWidget*)self);
}

char* XTextEdit_toMarkdown(const XTextEdit* self)
{
    const char* raw;
    size_t len;
    char* out;
    if (self && self->m_markdown) {
        raw = XString_toUtf8(self->m_markdown);
        if (raw && raw[0] != '\0') {
            len = XStrlen(raw);
            out = (char*)XMalloc_System(len + 1);
            if (out) XStrcpy(out, raw);
            return out;
        }
    }
    /* 降级：当前纯文本本身是合法 Markdown（与 toPlainText 同源）。 */
    return XPlainTextEdit_toPlainText(self ? self->m_editor : NULL);
}

const char* XTextEdit_markdown(const XTextEdit* self)
{
    if (!self || !self->m_markdown) return "";
    return XString_toUtf8(self->m_markdown);
}

void XTextEdit_setPlainText(XTextEdit* self, const char* text)
{
    const char* body = (text != NULL) ? text : "";
    if (!self) return;
    /* 与 setText 纯文本分支同口径：复位字符格式、清除 Markdown 原文、
     * 富文本文档同步。 */
    self->m_bold = false;
    self->m_italic = false;
    self->m_underline = false;
    xte_resetMarkdown(self);
    if (self->m_editor) XPlainTextEdit_setPlainText(self->m_editor, body);
#if XTEXTDOCUMENT_ON
    if (self->m_textDoc) XTextDocument_setPlainText(self->m_textDoc, body);
#endif
    XWidget_update((XWidget*)self);
}

void XTextEdit_print(XTextEdit* self, void* printer) { (void)self; (void)printer; }
void* XTextEdit_createStandardContextMenu(XTextEdit* self) { (void)self; return NULL; }
void XTextEdit_setTextInteractionFlags(XTextEdit* self, int flags) { (void)self; (void)flags; }
int XTextEdit_textInteractionFlags(const XTextEdit* self) { (void)self; return 0; }
void XTextEdit_setOverwriteMode(XTextEdit* self, bool overwrite) { (void)self; (void)overwrite; }
bool XTextEdit_overwriteMode(const XTextEdit* self) { (void)self; return false; }
int XTextEdit_cursorRect_width(const XTextEdit* self) { (void)self; return 1; }
void XTextEdit_moveCursor_2(XTextEdit* self, int operation, int mode) { (void)self; (void)operation; (void)mode; }
bool XTextEdit_cursorCanPaste(const XTextEdit* self) { (void)self; return false; }
void* XTextEdit_currentCharFormatChanged_signal(XTextEdit* self)
{
    (void)self;
    return (void*)(size_t)XTextEdit_currentCharFormatChanged_signal;
}

#endif /* XTEXTEDIT_ON */