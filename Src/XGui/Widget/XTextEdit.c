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
        image = XWidget_paintDevice(self);
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

XVtable* XTextEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextEdit)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_textEdit_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_textEdit_paintEvent);
    return XVTABLE_DEFAULT;
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
#if XTEXTDOCUMENT_ON
    self->m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif
    XClassSetVtable(self, XTextEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_textColor = 0xFF000000u;
    self->m_alignment = 1; /* AlignLeft */
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

void XTextEdit_setHtml(XTextEdit* self, const char* html)
{
    const char* p;
    char plain[4096];
    size_t o = 0;
    bool bold = false, italic = false, underline = false;
    if (!self || !html) return;
    p = html;
    while (*p && o < sizeof(plain) - 1) {
        if (*p == '<') {
            ++p;
            if (XStrncmp(p, "b>", 2) == 0) { bold = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/b>", 3) == 0) { bold = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "i>", 2) == 0) { italic = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/i>", 3) == 0) { italic = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "u>", 2) == 0) { underline = true; xte_skipTag(&p); }
            else if (XStrncmp(p, "/u>", 3) == 0) { underline = false; xte_skipTag(&p); }
            else if (XStrncmp(p, "br", 2) == 0 || XStrncmp(p, "p", 1) == 0) { plain[o++] = '\n'; xte_skipTag(&p); }
            else xte_skipTag(&p);
        } else {
            plain[o++] = *p++;
        }
    }
    plain[o] = '\0';
    self->m_bold = bold;
    self->m_italic = italic;
    self->m_underline = underline;
    XPlainTextEdit_setPlainText(self->m_editor, plain);
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
void XTextEdit_setAcceptRichText(XTextEdit* self, bool accept) { (void)self; (void)accept; }
bool XTextEdit_acceptRichText(const XTextEdit* self) { (void)self; return true; }
void XTextEdit_setTextBackgroundColor(XTextEdit* self, uint32_t color) { (void)self; (void)color; }
uint32_t XTextEdit_textBackgroundColor(const XTextEdit* self) { (void)self; return 0xFFFFFFFFu; }
void XTextEdit_setFontFamily(XTextEdit* self, const char* family) { (void)self; (void)family; }
const char* XTextEdit_fontFamily(const XTextEdit* self) { (void)self; return ""; }
void XTextEdit_setFontWeight(XTextEdit* self, int weight) { (void)self; (void)weight; }
int XTextEdit_fontWeight(const XTextEdit* self) { (void)self; return 400; }
void XTextEdit_setFontPointSize(XTextEdit* self, double size) { (void)self; (void)size; }
double XTextEdit_fontPointSize(const XTextEdit* self) { (void)self; return 12.0; }
void XTextEdit_setCurrentFont(XTextEdit* self, const char* family) { (void)self; (void)family; }
void XTextEdit_zoomIn(XTextEdit* self, int range) { (void)self; (void)range; }
void XTextEdit_zoomOut(XTextEdit* self, int range) { (void)self; (void)range; }
void XTextEdit_setTabStopDistance(XTextEdit* self, double distance) { (void)self; (void)distance; }
double XTextEdit_tabStopDistance(const XTextEdit* self) { (void)self; return 80.0; }
void XTextEdit_setAutoFormatting(XTextEdit* self, int features) { (void)self; (void)features; }
int XTextEdit_autoFormatting(const XTextEdit* self) { (void)self; return 0; }
void XTextEdit_setTabChangesFocus(XTextEdit* self, bool b) { (void)self; (void)b; }
bool XTextEdit_tabChangesFocus(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setDocumentTitle(XTextEdit* self, const char* title) { (void)self; (void)title; }
const char* XTextEdit_documentTitle(const XTextEdit* self) { (void)self; return ""; }
void XTextEdit_setUndoRedoEnabled_2(XTextEdit* self, bool enable) { XPlainTextEdit_setUndoRedoEnabled(&self->m_editor->m_base, enable); }
bool XTextEdit_isUndoRedoEnabled_2(const XTextEdit* self) { return XPlainTextEdit_isUndoRedoEnabled(&self->m_editor->m_base); }
void XTextEdit_setLineWrapMode(XTextEdit* self, int mode) { (void)self; (void)mode; }
int XTextEdit_lineWrapMode(const XTextEdit* self) { (void)self; return 1; }
void XTextEdit_setWordWrapMode(XTextEdit* self, int policy) { (void)self; (void)policy; }
int XTextEdit_wordWrapMode(const XTextEdit* self) { (void)self; return 0; }
void XTextEdit_setReadOnly_2(XTextEdit* self, bool ro) { XPlainTextEdit_setReadOnly(&self->m_editor->m_base, ro); }
bool XTextEdit_isReadOnly_2(const XTextEdit* self) { return XPlainTextEdit_isReadOnly(&self->m_editor->m_base); }
void XTextEdit_setPlaceholderText_2(XTextEdit* self, const char* text) { XPlainTextEdit_setPlaceholderText(&self->m_editor->m_base, text); }
const char* XTextEdit_placeholderText_2(const XTextEdit* self) { return XPlainTextEdit_placeholderText(&self->m_editor->m_base); }
void XTextEdit_ensureCursorVisible_2(XTextEdit* self) { XPlainTextEdit_ensureCursorVisible(&self->m_editor->m_base); }
void XTextEdit_setCenterOnScroll(XTextEdit* self, bool enabled) { (void)self; (void)enabled; }
bool XTextEdit_centerOnScroll(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setExtraSelections(XTextEdit* self, void* selections) { (void)self; (void)selections; }
void XTextEdit_setBackgroundVisible(XTextEdit* self, bool visible) { (void)self; (void)visible; }
bool XTextEdit_backgroundVisible(const XTextEdit* self) { (void)self; return false; }
void XTextEdit_setTextCursor_2(XTextEdit* self, void* cursor) { (void)self; (void)cursor; }
void* XTextEdit_textCursor(const XTextEdit* self) { (void)self; return NULL; }
void XTextEdit_setCursorWidth(XTextEdit* self, int width) { (void)self; (void)width; }
int XTextEdit_cursorWidth(const XTextEdit* self) { (void)self; return 1; }
bool XTextEdit_find_2(XTextEdit* self, const char* exp, int flags) { (void)self; (void)exp; (void)flags; return false; }
void XTextEdit_print(XTextEdit* self, void* printer) { (void)self; (void)printer; }
void* XTextEdit_createStandardContextMenu(XTextEdit* self) { (void)self; return NULL; }
void XTextEdit_setTextInteractionFlags(XTextEdit* self, int flags) { (void)self; (void)flags; }
int XTextEdit_textInteractionFlags(const XTextEdit* self) { (void)self; return 0; }
void XTextEdit_setOverwriteMode(XTextEdit* self, bool overwrite) { (void)self; (void)overwrite; }
bool XTextEdit_overwriteMode(const XTextEdit* self) { (void)self; return false; }
int XTextEdit_cursorRect_width(const XTextEdit* self) { (void)self; return 1; }
void XTextEdit_moveCursor_2(XTextEdit* self, int operation, int mode) { (void)self; (void)operation; (void)mode; }
bool XTextEdit_cursorCanPaste(const XTextEdit* self) { (void)self; return false; }
#endif /* XTEXTEDIT_ON */