/**
 * @file       XTextEdit.h
 * @brief      XTextEdit 富文本编辑控件（对标 Qt 6.8 QTextEdit 核心公共 API）。
 * @details    继承 XPlainTextEdit，增加逐块字符格式：粗体/斜体/下划线/
 *             颜色/对齐；setHtml 解析基础 HTML 子集（b/i/u/br/p/font）；
 *             toHtml 生成对应 HTML；信号 textChanged/selectionChanged。
 * @note       模块总开关 XTEXTEDIT_ON。依赖 XPLAINTEXTEDIT_ON。
 * @author     XinYueC 团队
 */
#ifndef XTEXTEDIT_H
#define XTEXTEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#if XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#include "XTextDocument.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON

XCLASS_DEFINE_BEGING(XTextEdit)
XCLASS_DEFINE_EXTEND_END(XTextEdit, XAbstractScrollArea)

typedef struct XTextEdit
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XPlainTextEdit* m_editor;   /**< 内嵌多行编辑器（拥有）。 */
#if XTEXTDOCUMENT_ON
    XTextDocument* m_textDoc;   /**< 富文本文档（拥有）。 */
#endif
    bool m_bold;            /**< 当前粗体格式。 */
    bool m_italic;          /**< 当前斜体格式。 */
    bool m_underline;       /**< 当前下划线格式。 */
    uint32_t m_textColor;   /**< 当前文字颜色（ARGB）。 */
    int m_alignment;        /**< 当前对齐。 */
    XString* m_fontFamily;  /**< 当前字体族（对象拥有）。 */
    int m_fontWeight;       /**< 当前字重（默认 400）。 */
    double m_fontPointSize; /**< 当前字号（磅；默认 10）。 */
    double m_tabStopDistance; /**< 制表位距（默认 80）。 */
    int m_cursorWidth;      /**< 光标宽（像素；默认 1）。 */
    int m_lineWrapMode;     /**< 换行模式（默认 WidgetWidth）。 */
    int m_wordWrapMode;     /**< 单词换行策略（默认 WrapAtWordBoundaryOrAnywhere）。 */
    bool m_acceptRichText;  /**< 接受富文本（默认 true）。 */
    int m_autoFormatting;   /**< 自动格式化位集（默认 0）。 */
    bool m_centerOnScroll;  /**< 滚动居中（默认 false）。 */
    XString* m_documentTitle; /**< 文档标题（对象拥有）。 */
    uint32_t m_textBackgroundColor; /**< 文本背景色（ARGB；0=默认）。 */
} XTextEdit;

/** @brief X文本Editclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XTextEdit_class_init(void);
/** @brief X文本Editinit（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XTextEdit_init(XTextEdit* self, XWidget* parent, XWidgetFlags flags);
#define XTextEdit_create(parent, flags) XTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief X文本Editcreateex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XTextEdit* XTextEdit_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTextEdit_deinit_base(self) XAbstractScrollArea_deinit_base((XAbstractScrollArea*)(self))
#define XTextEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/* 字符格式 */
/**
 * @brief      设置粗体格式。
 */
void XTextEdit_setBold(XTextEdit* self, bool bold);
/**
 * @brief      获取粗体格式。
 */
bool XTextEdit_isBold(const XTextEdit* self);
/**
 * @brief      设置斜体格式。
 */
void XTextEdit_setItalic(XTextEdit* self, bool italic);
/**
 * @brief      获取斜体格式。
 */
bool XTextEdit_isItalic(const XTextEdit* self);
/**
 * @brief      设置下划线格式。
 */
void XTextEdit_setUnderline(XTextEdit* self, bool underline);
/**
 * @brief      获取下划线格式。
 */
bool XTextEdit_isUnderline(const XTextEdit* self);

/** @brief 查询斜体（对标 QTextEdit::fontItalic 属性 READ；宏别名复用
 *         XTextEdit_isItalic）。 */
#define XTextEdit_fontItalic(self) XTextEdit_isItalic((self))
/** @brief 设置斜体（对标 QTextEdit::setFontItalic；宏别名复用
 *         XTextEdit_setItalic）。 */
#define XTextEdit_setFontItalic(self, italic) \
    XTextEdit_setItalic((self), (italic))
/** @brief 查询下划线（对标 QTextEdit::fontUnderline 属性 READ；宏别名
 *         复用 XTextEdit_isUnderline）。 */
#define XTextEdit_fontUnderline(self) XTextEdit_isUnderline((self))
/** @brief 设置下划线（对标 QTextEdit::setFontUnderline；宏别名复用
 *         XTextEdit_setUnderline）。 */
#define XTextEdit_setFontUnderline(self, underline) \
    XTextEdit_setUnderline((self), (underline))
/**
 * @brief      设置文字颜色。
 */
void XTextEdit_setTextColor(XTextEdit* self, uint32_t color);
/**
 * @brief      获取文字颜色。
 */
uint32_t XTextEdit_textColor(const XTextEdit* self);
/**
 * @brief      设置对齐方式。
 */
void XTextEdit_setAlignment(XTextEdit* self, int alignment);
/**
 * @brief      获取对齐方式。
 */
int XTextEdit_alignment(const XTextEdit* self);

/* HTML */
/**
 * @brief      设置 HTML 内容（b/i/u/br/p 子集）。
 */
void XTextEdit_setHtml(XTextEdit* self, const char* html);
/**
 * @brief      导出 HTML 文本（含实体转义；对标 toHtml）。
 */
char* XTextEdit_toHtml(const XTextEdit* self);

/* ==================== 信号 ==================== */

/**
 * @brief      文本变化信号（真发射）。
 */
void* XTextEdit_textChanged_signal(XTextEdit* self);

/** @brief currentCharFormatChanged() 信号（对标 QTextEdit::currentCharFormatChanged；
 *         光标格式变化时触发，Task 2.3 接线）。 */
void* XTextEdit_currentCharFormatChanged_signal(XTextEdit* self);

#endif /* XTEXTEDIT_ON */

#ifdef __cplusplus
}
#endif

/** @brief X文本EditcopyAvailable 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_copyAvailable_signal(XTextEdit* self);
/** @brief X文本Editcursor位置变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_cursorPositionChanged_signal(XTextEdit* self);
/** @brief X文本Editmodification变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_modificationChanged_signal(XTextEdit* self);
/** @brief X文本EditredoAvailable 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_redoAvailable_signal(XTextEdit* self);
/** @brief X文本Editselection变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_selectionChanged_signal(XTextEdit* self);
/** @brief X文本EditundoAvailable 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_undoAvailable_signal(XTextEdit* self);
/** @brief X文本Editappend（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XTextEdit_append(XTextEdit* self, const char* text);
/** @brief X文本Editcopy2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_copy_2(XTextEdit* self);
/** @brief X文本Editcut2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_cut_2(XTextEdit* self);
/** @brief X文本Editpaste2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_paste_2(XTextEdit* self);
/** @brief X文本Editclear2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_clear_2(XTextEdit* self);
/** @brief X文本EditselectAll2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_selectAll_2(XTextEdit* self);
/** @brief X文本Editcan粘贴（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_canPaste(XTextEdit* self);
/** @brief X文本EditsetAcceptRich文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param accept bool 参数。
 * @return 无返回值。
 */
void XTextEdit_setAcceptRichText(XTextEdit* self, bool accept);
/** @brief X文本EditacceptRich文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_acceptRichText(const XTextEdit* self);
/** @brief X文本Editset文本背景颜色（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param color ARGB 颜色值。
 * @return 无返回值。
 */
void XTextEdit_setTextBackgroundColor(XTextEdit* self, uint32_t color);
/** @brief X文本Edittext背景颜色（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
uint32_t XTextEdit_textBackgroundColor(const XTextEdit* self);
/** @brief X文本Editset字体Family（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param family 字体族。
 * @return 无返回值。
 */
void XTextEdit_setFontFamily(XTextEdit* self, const char* family);
/** @brief X文本EditfontFamily（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XTextEdit_fontFamily(const XTextEdit* self);
/** @brief X文本Editset字体Weight（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param weight int 参数。
 * @return 无返回值。
 */
void XTextEdit_setFontWeight(XTextEdit* self, int weight);
/** @brief X文本EditfontWeight（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_fontWeight(const XTextEdit* self);
/** @brief X文本Editset字体Point尺寸（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param size 尺寸（像素）。
 * @return 无返回值。
 */
void XTextEdit_setFontPointSize(XTextEdit* self, double size);
/** @brief X文本EditfontPoint尺寸（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
double XTextEdit_fontPointSize(const XTextEdit* self);
/** @brief X文本Editset当前字体（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param family 字体族。
 * @return 无返回值。
 */
void XTextEdit_setCurrentFont(XTextEdit* self, const char* family);
/** @brief X文本EditzoomIn（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param range 范围值。
 * @return 无返回值。
 */
void XTextEdit_zoomIn(XTextEdit* self, int range);
/** @brief X文本EditzoomOut（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param range 范围值。
 * @return 无返回值。
 */
void XTextEdit_zoomOut(XTextEdit* self, int range);
/** @brief X文本Editset页签StopDistance（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param distance double 参数。
 * @return 无返回值。
 */
void XTextEdit_setTabStopDistance(XTextEdit* self, double distance);
/** @brief X文本EdittabStopDistance（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
double XTextEdit_tabStopDistance(const XTextEdit* self);
/** @brief X文本Editset自动Formatting（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param features int 参数。
 * @return 无返回值。
 */
void XTextEdit_setAutoFormatting(XTextEdit* self, int features);
/** @brief X文本EditautoFormatting（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_autoFormatting(const XTextEdit* self);
/** @brief X文本Editset页签Changes焦点（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param b bool 参数。
 * @return 无返回值。
 */
void XTextEdit_setTabChangesFocus(XTextEdit* self, bool b);
/** @brief X文本EdittabChanges焦点（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_tabChangesFocus(const XTextEdit* self);
/** @brief X文本Editset文档标题（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param title 标题文本。
 * @return 无返回值。
 */
void XTextEdit_setDocumentTitle(XTextEdit* self, const char* title);
/** @brief X文本Editdocument标题（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XTextEdit_documentTitle(const XTextEdit* self);
/** @brief X文本Editset撤销重做启用2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param enable bool 开关：true 启用。
 * @return 无返回值。
 */
void XTextEdit_setUndoRedoEnabled_2(XTextEdit* self, bool enable);
/** @brief X文本Editis撤销重做启用2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_isUndoRedoEnabled_2(const XTextEdit* self);
/** @brief X文本Editset行换行模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XTextEdit_setLineWrapMode(XTextEdit* self, int mode);
/** @brief X文本Editline换行模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_lineWrapMode(const XTextEdit* self);
/** @brief X文本Editset单词换行模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param policy 策略枚举。
 * @return 无返回值。
 */
void XTextEdit_setWordWrapMode(XTextEdit* self, int policy);
/** @brief X文本Editword换行模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_wordWrapMode(const XTextEdit* self);
/** @brief X文本EditsetReadOnly2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param ro bool 参数。
 * @return 无返回值。
 */
void XTextEdit_setReadOnly_2(XTextEdit* self, bool ro);
/** @brief X文本EditisReadOnly2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_isReadOnly_2(const XTextEdit* self);
/** @brief X文本Editset占位文本2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XTextEdit_setPlaceholderText_2(XTextEdit* self, const char* text);
/** @brief X文本Editplaceholder文本2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XTextEdit_placeholderText_2(const XTextEdit* self);
/** @brief X文本Editensure光标可见2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_ensureCursorVisible_2(XTextEdit* self);
/** @brief X文本Editset居中On滚动（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param enabled bool 开关：true 启用。
 * @return 无返回值。
 */
void XTextEdit_setCenterOnScroll(XTextEdit* self, bool enabled);
/** @brief X文本EditcenterOn滚动（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_centerOnScroll(const XTextEdit* self);
/** @brief X文本Editset额外Selections（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param selections 选择集指针。
 * @return 无返回值。
 */
void XTextEdit_setExtraSelections(XTextEdit* self, void* selections);
/** @brief X文本Editset背景可见（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param visible bool：true 可见。
 * @return 无返回值。
 */
void XTextEdit_setBackgroundVisible(XTextEdit* self, bool visible);
/** @brief X文本Editbackground可见（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_backgroundVisible(const XTextEdit* self);
/** @brief X文本Editset文本光标2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param cursor 光标指针。
 * @return 无返回值。
 */
void XTextEdit_setTextCursor_2(XTextEdit* self, void* cursor);
/** @brief X文本Edittext光标（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_textCursor(const XTextEdit* self);
/** @brief X文本Editset光标宽（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param width 宽（像素）。
 * @return 无返回值。
 */
void XTextEdit_setCursorWidth(XTextEdit* self, int width);
/** @brief X文本Editcursor宽（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_cursorWidth(const XTextEdit* self);
/** @brief X文本Editfind2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param exp 匹配表达式。
 * @param flags 窗口标志位组合。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_find_2(XTextEdit* self, const char* exp, int flags);
/** @brief X文本Editprint（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param printer 打印机指针。
 * @return 无返回值。
 */
void XTextEdit_print(XTextEdit* self, void* printer);
/** @brief X文本EditcreateStandardContext菜单（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTextEdit_createStandardContextMenu(XTextEdit* self);
/** @brief X文本Editset文本交互Flags（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XTextEdit_setTextInteractionFlags(XTextEdit* self, int flags);
/** @brief X文本Edittext交互Flags（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_textInteractionFlags(const XTextEdit* self);
/** @brief X文本Editset覆盖模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param overwrite bool 参数。
 * @return 无返回值。
 */
void XTextEdit_setOverwriteMode(XTextEdit* self, bool overwrite);
/** @brief X文本Editoverwrite模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_overwriteMode(const XTextEdit* self);
/** @brief X文本Editcursor矩形width（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTextEdit_cursorRect_width(const XTextEdit* self);
/** @brief X文本Editmove光标2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param operation int 参数。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XTextEdit_moveCursor_2(XTextEdit* self, int operation, int mode);
/** @brief X文本EditcursorCan粘贴（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTextEdit_cursorCanPaste(const XTextEdit* self);
#endif /* XTEXTEDIT_H */