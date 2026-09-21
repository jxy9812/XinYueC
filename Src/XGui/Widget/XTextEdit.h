/**
 * @file       XTextEdit.h
 * @brief      XTextEdit 富文本编辑控件（对标 Qt 6.8 QTextEdit 核心公共 API）。
 * @details    继承 XAbstractScrollArea，内嵌 XPlainTextEdit 承载纯文本
 *             编辑（所见即所存，标签不展开进缓冲）；富文本渲染子集以
 *             只读预览模式承载（setHtml/显式开关进入：隐藏编辑器、壳
 *             逐块绘制 b/i/u/s、font color/size、br、p align、a href，
 *             嵌套上限一层；编辑类接口自动退出预览）。另含逐块字符
 *             格式状态、toHtml 导出、anchor 悬停/点击信号（对标 QLabel
 *             链接模式）；信号 textChanged/selectionChanged。
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

/** @brief 平铺字符格式位集（对标 QTextCharFormat 可承载子集的扁平化，
 *         供 setCurrentCharFormat/mergeCurrentCharFormat 使用）。 */
typedef enum XTextEditCharFormatProp
{
    XTextEditCharFormat_None      = 0x00, /**< 无属性。 */
    XTextEditCharFormat_Bold      = 0x01, /**< 粗体（对标 QFont::Bold）。 */
    XTextEditCharFormat_Italic    = 0x02, /**< 斜体（对标 QFont::StyleItalic）。 */
    XTextEditCharFormat_Underline = 0x04  /**< 下划线。 */
} XTextEditCharFormatProp;

#ifndef XVARIANT_H
/** @brief XVariant 前向声明（XVariant.h 未被引入时保证本头文件自洽）。 */
typedef struct XVariant XVariant;
#endif

typedef struct XTextEdit
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XPlainTextEdit* m_editor;   /**< 内嵌多行编辑器（拥有）。 */
#if XTEXTDOCUMENT_ON
    XTextDocument* m_textDoc;   /**< 富文本文档。 */
    bool m_textDocOwned;        /**< 文档所有权：true=内部创建并负责释放；
                                     false=外部 setDocument 接管不释放。 */
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
    int m_lineWrapColumnOrWidth; /**< 换行列宽/像素宽（默认 0；对标
                                      lineWrapColumnOrWidth 属性承载）。 */
    int m_wordWrapMode;     /**< 单词换行策略（默认 WrapAtWordBoundaryOrAnywhere）。 */
    bool m_acceptRichText;  /**< 接受富文本（默认 true）。 */
    int m_autoFormatting;   /**< 自动格式化位集（默认 0）。 */
    bool m_centerOnScroll;  /**< 滚动居中（默认 false）。 */
    XString* m_documentTitle; /**< 文档标题（对象拥有）。 */
    XString* m_markdown;    /**< Markdown 原文（对象拥有；setMarkdown
                                  写入、内容被替换接口清除）。 */
    uint32_t m_textBackgroundColor; /**< 文本背景色（ARGB；0=默认）。 */
    bool m_richPreview;     /**< 只读富文本预览态：true=隐藏内嵌编辑器、
                                  壳按渲染子集逐块绘制 m_textDoc（对标
                                  QTextEdit::setHtml 进入富文本呈现；
                                  编辑缓冲保持纯文本语义——所见即所存）。 */
    XString* m_hoverAnchor; /**< 预览态悬停链接 URL（对象拥有；NULL=无；
                                  对标 QLabel 悬停按 href 去重）。 */
    XString* m_pressedAnchor; /**< 预览态按压链接 URL（对象拥有；NULL=无；
                                  对标 QLabel 按下记录、释放同链触发）。 */
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

/* 撤销 / 重做 */
/** @brief 撤销上一次编辑（对标 QTextEdit::undo；委托内嵌编辑器快照栈）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_undo(XTextEdit* self);
/** @brief 重做上一次被撤销的编辑（对标 QTextEdit::redo；委托内嵌编辑器
 *         快照栈）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTextEdit_redo(XTextEdit* self);
/** @brief 查询当前是否可撤销（对标 QTextEdit::canUndo；与
 *         undoAvailable 信号同源的直接查询接口）。
 * @param self 目标控件指针。
 * @return 撤销快照栈非空返回 true，否则返回 false。
 */
bool XTextEdit_canUndo(const XTextEdit* self);
/** @brief 查询当前是否可重做（对标 QTextEdit::canRedo；与
 *         redoAvailable 信号同源的直接查询接口）。
 * @param self 目标控件指针。
 * @return 重做快照栈非空返回 true，否则返回 false。
 */
bool XTextEdit_canRedo(const XTextEdit* self);

/* HTML */
/**
 * @brief      设置 HTML 内容（渲染子集解析；编辑缓冲同步剥离文本）。
 * @details    渲染子集（b/i/u/s、font color/size、br、p align、a href，
 *             嵌套上限一层）解析写入富文本文档（toHtml 导出与只读预览
 *             的真源）；编辑缓冲同步剥离标签后的纯文本（标签不展开进
 *             缓冲——所见即所存，对标 QPlainTextEdit 无 setHtml 的纯
 *             文本语义）。富文本视觉呈现不在此切换：经
 *             XTextEdit_setRichPreview(true) 显式进入只读预览（保持
 *             编辑态下 toHtml 以编辑器为文本源的既有语义）。
 */
void XTextEdit_setHtml(XTextEdit* self, const char* html);
/**
 * @brief      导出 HTML 文本（含实体转义；对标 toHtml）。
 */
char* XTextEdit_toHtml(const XTextEdit* self);

/** @brief 设置只读富文本预览开关（渲染子集的显式呈现入口）。
 * @details on=true：隐藏内嵌编辑器，壳按渲染子集逐块绘制当前富文本文
 *          档（锚点悬停高亮、手型光标、点击发射链接信号）；on=false：
 *          回到纯文本编辑态（编辑器恢复显示）。编辑类接口（输入/粘贴/
 *          撤销等）与 setPlainText/setText 纯文本分支自动退出预览。
 * @param self 目标控件指针；NULL 无操作。
 * @param on true 进入预览；false 退出预览。
 * @return 无返回值。
 */
void XTextEdit_setRichPreview(XTextEdit* self, bool on);

/** @brief 查询只读富文本预览态。
 * @param self 目标控件指针；NULL 返回 false。
 * @return 预览态返回 true。
 */
bool XTextEdit_isRichPreview(const XTextEdit* self);

/* 纯文本 / setText */
/** @brief 导出纯文本（对标 QTextEdit::toPlainText；去除全部标记）。
 * @param self 目标控件指针。
 * @return 返回新建 XString*（UTF-8 内容；self 无效时为空串对象）；
 *         内存分配失败返回 NULL。调用方负责用 XString_delete_base 释放。
 */
XString* XTextEdit_toPlainText(const XTextEdit* self);
/** @brief 设置文本内容（对标 QTextEdit::setText；HTML/纯文本自动探测）。
 * @details 采用 Qt::mightBeRichText 子集启发式：跳过前导空白后，若首个
 *          '<'（且位于首个换行之前）到其匹配 '>' 之间构成标签状构造
 *          （'!'/'?' 开头，或可选 '/' 后跟至少一个字母/数字），判定为
 *          富文本并走 XTextEdit_setHtml 语义；否则按纯文本写入内嵌
 *          编辑器并同步富文本文档，同时复位粗体/斜体/下划线为默认
 *          （与 setHtml 解析后无开放标签的语义一致）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；可为 HTML 片段；NULL 忽略。
 * @return 无返回值。
 */
void XTextEdit_setText(XTextEdit* self, const char* text);

/* ==================== 信号 ==================== */

/**
 * @brief      文本变化信号（真发射）。
 */
void* XTextEdit_textChanged_signal(XTextEdit* self);

/** @brief currentCharFormatChanged() 信号（对标 QTextEdit::currentCharFormatChanged；
 *         光标格式变化时触发，Task 2.3 接线）。 */
void* XTextEdit_currentCharFormatChanged_signal(XTextEdit* self);

/** @brief linkHovered(const char*) 信号（对标 QLabel::linkHovered）。
 * @details 载荷：悬停链接 URL（XString* 堆拷贝随参数表释放；离开链接
 *          时为空串）。真实发射点：只读富文本预览态下鼠标移入/切换/
 *          离开链接（URL 去重变化）时，由壳鼠标移动处理发射，同时驱动
 *          悬停高亮重绘与手型光标（对标 QLabel 悬停链路）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      url 悬停链接 URL（UTF-8）；可为 NULL，视为空串。
 * @return     不透明的 linkHovered 信号标识；不得解引用/释放。
 */
void* XTextEdit_linkHovered_signal(XTextEdit* self, const char* url);

/** @brief linkActivated(const char*) 信号（对标 QLabel::linkActivated）。
 * @details 载荷：激活链接 URL（XString* 堆拷贝随参数表释放）。真实发射
 *          点：只读富文本预览态下鼠标按下与释放命中同一链接时发射
 *          （对标 QLabel 按下记录+释放同链激活模式）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      url 激活链接 URL（UTF-8）；可为 NULL，视为空串。
 * @return     不透明的 linkActivated 信号标识；不得解引用/释放。
 */
void* XTextEdit_linkActivated_signal(XTextEdit* self, const char* url);

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
/** @brief 查询当前字体（对标 QTextEdit::currentFont 属性 READ）。
 * @param self 目标控件指针。
 * @return 以值返回当前字体属性（族/字重/字号/斜体/下划线）组合的
 *         XFont；self 为空返回默认构造字体。
 * @note Qt 返回光标处字符格式的字体；本库为整篇单格式（纯文本模型），
 *       即当前字体属性的组合值。返回对象所有权归调用方，使用完毕
 *       必须 XFont_deinit_base。
 */
XFont XTextEdit_currentFont(const XTextEdit* self);
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
/** @brief 从当前光标处查找文本（对标 QTextEdit::find；实现既有空桩）。
 * @details 委托内嵌编辑器 XPlainTextEdit_find：按 UTF-8 字节偏移逐行
 *          匹配；flags 仅支持最低位，与
 *          QTextDocument::FindFlag::FindBackward 数值一致。
 *          命中后将内嵌编辑器光标置于命中处并请求重绘（向前查找光标
 *          落在命中结束处，向后查找落在命中起始处，便于连续查找），
 *          同时刷新外层控件；未命中保持原位。
 * @param self 目标控件指针；NULL 或内嵌编辑器缺失时返回 false。
 * @param exp UTF-8 查找串；NULL 或空串返回 false。
 * @param flags 查找方向标志：1 = FindBackward（向后查找），
 *        0 = 向前查找。
 * @return 命中返回 true；未命中返回 false。
 */
bool XTextEdit_find_2(XTextEdit* self, const char* exp, int flags);

/* ==================== 光标几何与查找（对标 QPlainTextEdit 同组 API） ==== */

/** @brief 查询当前光标竖线矩形（对标 QTextEdit::cursorRect 的行列简化）。
 * @details 委托内嵌编辑器 XPlainTextEdit_cursorRect，与其 paintEvent
 *          自绘同一套度量口径：行高 16px、行左留白 2px；X 方向按控件
 *          字体测量光标前列宽（XPainter_textWidthRange，UTF-8 字节偏移
 *          口径），Y 方向随内嵌编辑器垂直滚动条取值偏移。内嵌编辑器
 *          常驻 (0,0) 并铺满本控件，故返回矩形可直接作为本控件局部
 *          坐标使用。
 * @param self 目标控件指针；NULL 或内嵌编辑器缺失时返回零矩形。
 * @return 光标矩形（宽度取内嵌编辑器 cursorWidth 设定值，高度为一行
 *         行高）。
 */
XRect XTextEdit_cursorRect(const XTextEdit* self);

/** @brief 返回坐标 pos 处的超链接锚点（对标 QTextEdit::anchorAt）。
 * @details 以富文本文档片段的 anchorHref 为承载，与只读预览绘制路径
 *          （VX_textEdit_paintEvent 经 xte_walkRich）同一套逐块几何
 *          命中：块高随片段字体度量、块宽实测、按块对齐定位起笔 X；
 *          命中返回片段 href 拷贝，未命中返回 0 长度字符串对象。
 * @note       非预览态文档按纯文本口径承载（无锚点片段），同样返回
 *             0 长度字符串。
 * @param self 目标控件指针；可为 NULL。
 * @param pos 控件局部坐标点；可为 NULL。
 * @return 堆上新建的 XString*（锚点 href 或空串），调用方以
 *         XString_delete_base 释放；内存分配失败返回 NULL。
 */
XString* XTextEdit_anchorAt(const XTextEdit* self, const XPoint* pos);

/** @brief 设置光标位置（对标 QTextEdit::setTextCursor 的平铺行列简化）。
 * @details 委托内嵌编辑器 XPlainTextEdit_setTextCursor：行列越界时
 *          钳位到有效范围（行 [0, 行数-1]、列 [0, 该行字节数]），并
 *          请求重绘。既有 setTextCursor_2/textCursor（void*
 *          QTextCursor 承载版）为预留桩；本组平铺行列接口与
 *          XPlainTextEdit 同名接口语义一致。
 * @param self 目标控件指针；NULL 或内嵌编辑器缺失时无操作。
 * @param line 目标行（0 起）。
 * @param col 目标列（行内 UTF-8 字节偏移）。
 * @return 无返回值。
 */
void XTextEdit_setTextCursor(XTextEdit* self, int line, int col);

/** @brief 查询光标行（内嵌编辑器光标的平铺行列查询）。
 * @param self 目标控件指针；NULL 或内嵌编辑器缺失时返回 0。
 * @return 光标行（0 起）。
 */
int XTextEdit_textCursorLine(const XTextEdit* self);

/** @brief 查询光标列（内嵌编辑器光标的平铺行列查询）。
 * @param self 目标控件指针；NULL 或内嵌编辑器缺失时返回 0。
 * @return 光标列（行内 UTF-8 字节偏移）。
 */
int XTextEdit_textCursorColumn(const XTextEdit* self);

/* ==================== 文档/插入/资源/Markdown 补齐组（对标 QTextEdit） ==== */

/** @brief 返回坐标 pos 处的文本光标位置（对标 QTextEdit::cursorForPosition
 *         的平铺行列反查）。
 * @details 与 XTextEdit_cursorRect/XPlainTextEdit_cursorRect 同一套度量
 *          口径做逆映射：行高 16px、行左留白 2px。Y 方向先加回内嵌
 *          编辑器垂直滚动条取值再除以行高得行号；X 方向减去左留白后，
 *          按控件字体逐字符累加宽度（XPainter_textWidthRange，UTF-8
 *          字节偏移口径）定位列。行列均钳位到有效范围（行
 *          [0, 行数-1]、列 [0, 该行字节数]），与
 *          XTextEdit_setTextCursor 的钳位规则一致，可与 cursorRect/
 *          setTextCursor 往返配合。
 * @param self 目标控件指针；NULL、内嵌编辑器缺失、无有效内容或 pos 为
 *             NULL 时返回 {0,0}。
 * @param pos 控件局部坐标点（与 cursorRect 返回矩形同坐标系）。
 * @return 反查结果：x 分量 = 行号（0 起），y 分量 = 列（行内 UTF-8
 *         字节偏移）。
 */
XPoint XTextEdit_cursorForPosition(const XTextEdit* self, const XPoint* pos);

/** @brief 在当前光标处插入纯文本（对标 QTextEdit::insertPlainText）。
 * @details 委托内嵌编辑器 XPlainTextEdit_insertPlainText（支持 \n 跨行
 *          插入，插入后光标落在插入文本之后）；随后以编辑器当前文本
 *          刷新富文本文档并请求重绘，保持文档与显示一致。
 * @note       格式承载差异：新插入文本不携带当前字符格式（Qt 会套用
 *             currentCharFormat）；富文本文档按纯文本口径同步，原文档
 *             片段格式会被重建。
 * @param self 目标控件指针；NULL、内嵌编辑器缺失或 text 为 NULL 时无操作。
 * @param text UTF-8 文本（可含 \n）；空串无效果。
 * @return 无返回值。
 */
void XTextEdit_insertPlainText(XTextEdit* self, const char* text);

/** @brief 在当前光标处插入 HTML（对标 QTextEdit::insertHtml 的平铺降级）。
 * @details 复用 setHtml 的标签剥离口径（b/i/u/br/p 子集，其余标签整体
 *          丢弃、实体不展开）得到纯文本后，按 XTextEdit_insertPlainText
 *          语义在光标处插入。
 * @note       格式差异：Qt 保留 HTML 内联格式插入；平铺模型剥离全部
 *             标记仅插纯文本，当前字符格式状态保持不变。
 * @param self 目标控件指针；NULL 或 html 为 NULL 时无操作。
 * @param html UTF-8 HTML 片段。
 * @return 无返回值。
 */
void XTextEdit_insertHtml(XTextEdit* self, const char* html);

/** @brief 查询换行列宽/像素宽（对标 QTextEdit::lineWrapColumnOrWidth
 *         属性 READ）。
 * @details 状态承载：返回 setLineWrapColumnOrWidth 存储值，默认 0
 *          （与 Qt 默认一致）。语义对齐 Qt：仅当 lineWrapMode 为定宽
 *          模式时该值参与布局（FixedPixelWidth 为像素、FixedColumnWidth
 *          为列数）；平铺模型第一版不换行绘制，该值仅存储。
 * @param self 目标控件指针；NULL 返回 0。
 * @return 存储的换行列宽/像素宽。
 */
int XTextEdit_lineWrapColumnOrWidth(const XTextEdit* self);

/** @brief 设置换行列宽/像素宽（对标 QTextEdit::lineWrapColumnOrWidth
 *         属性 WRITE）。
 * @details 状态承载：原样存储 w（Qt 亦不钳位）；平铺模型第一版不换行
 *          绘制，不触发重排。
 * @param self 目标控件指针；NULL 无操作。
 * @param w 像素宽或列宽（含义随 lineWrapMode）。
 * @return 无返回值。
 */
void XTextEdit_setLineWrapColumnOrWidth(XTextEdit* self, int w);

/** @brief 加载指定类型与名称的资源（对标 QTextEdit::loadResource）。
 * @details Qt 中该函数是 QTextDocument::loadResource 的扩展，默认实现
 *          返回无效 QVariant；平铺模型未建立资源缓存体系（图片等资源
 *          不参与富文本布局），故恒返回 NULL 承载"无效 QVariant"语义。
 * @note       资源体系未建：type/name 仅保持签名一致，不被使用；资源
 *             加载请在上层自行缓存实现。
 * @param self 目标控件指针；可为 NULL。
 * @param type 资源类型（QTextDocument::ResourceType 数值承载）。
 * @param name 资源名称（QUrl 的 UTF-8 字符串承载）；可为 NULL。
 * @return 恒返回 NULL（资源未建立，调用方无需释放）。
 */
XVariant* XTextEdit_loadResource(XTextEdit* self, int type, const char* name);

/** @brief 合并当前字符格式（对标 QTextEdit::mergeCurrentCharFormat 的
 *         平铺位集承载）。
 * @details format 中置位的属性（XTextEditCharFormatProp 位组合）逐一
 *          应用到当前字符格式状态，未置位属性保持不变；随后触发
 *          currentCharFormatChanged 信号桩。
 * @note       三态局限：int 位集无法表达"显式取消某属性"，合并仅支持
 *             置位方向（对标 QTextCharFormat 仅携带已置属性再合并的
 *             语义）；颜色/字体族等富格式不在位集承载范围，经
 *             setTextColor/setFontFamily 等独立属性接口承载。
 * @param self 目标控件指针；NULL 无操作。
 * @param format XTextEditCharFormatProp 位组合。
 * @return 无返回值。
 */
void XTextEdit_mergeCurrentCharFormat(XTextEdit* self, int format);

/** @brief 设置当前字符格式（对标 QTextEdit::setCurrentCharFormat 的
 *         平铺位集承载）。
 * @details 整体替换当前字符格式状态：format 置位属性开、未置位属性关
 *          （对标 QTextCursor::setCharFormat 的整体替换语义）；随后触发
 *          currentCharFormatChanged 信号桩。
 * @note       承载差异：Qt 的 QTextCharFormat 还含颜色/字号/字体族等，
 *             平铺模型以位集承载粗体/斜体/下划线三属性，其余经
 *             setTextColor/setFontFamily/setFontPointSize 等接口承载。
 * @param self 目标控件指针；NULL 无操作。
 * @param format XTextEditCharFormatProp 位组合。
 * @return 无返回值。
 */
void XTextEdit_setCurrentCharFormat(XTextEdit* self, int format);

/** @brief 滚动到指定锚点（对标 QTextEdit::scrollToAnchor 的子集降级）。
 * @details 渲染子集仅承载 <a href> 链接锚（不解析 <a name> 目标锚），
 *          无目标锚几何可定位；本函数保持接口存在性，当前为无操作。
 * @note       子集边界：name 目标锚不在渲染子集内；anchor 仅保持签名
 *             一致，不被使用。
 * @param self 目标控件指针；可为 NULL。
 * @param anchor 锚点名称；可为 NULL，不被使用。
 * @return 无返回值。
 */
void XTextEdit_scrollToAnchor(XTextEdit* self, const char* anchor);

#if XTEXTDOCUMENT_ON
/** @brief 外接富文本文档（对标 QTextEdit::setDocument）。
 * @details doc 非 NULL 时接管为显示文档（不获得所有权，与 Qt 一致，
 *          调用方保证其生命周期覆盖本控件）；doc 为 NULL 时新建内部
 *          默认文档（对标 setDocument(nullptr) 语义）。切换后以新文档
 *          纯文本刷新内嵌编辑器显示内容（编辑器文本以文档为准），并
 *          请求重绘。原内部默认文档将被释放。
 * @note       承载差异：文档变更信号（contentsChanged 等）未接线到本
 *             控件；插入类接口仍按"编辑器为文本源"以纯文本口径回写
 *             外接文档，外接文档的片段格式可能被重建。
 * @param self 目标控件指针；NULL 无操作。
 * @param doc 富文本文档指针；NULL 表示重置为内部默认文档。
 * @return 无返回值。
 */
void XTextEdit_setDocument(XTextEdit* self, XTextDocument* doc);

/** @brief 返回当前富文本文档（对标 QTextEdit::document）。
 * @details 返回内部默认文档或经 setDocument 接管的外部文档；不转移
 *          所有权，调用方不得释放。
 * @param self 目标控件指针；NULL 返回 NULL。
 * @return 当前文档指针（可能为空内容文档）。
 */
XTextDocument* XTextEdit_document(const XTextEdit* self);
#endif /* XTEXTDOCUMENT_ON */

/** @brief 设置 Markdown 内容（对标 QTextEdit::setMarkdown 的平铺降级）。
 * @details 原文承载：完整保存 md 原文（markdown()/toMarkdown() 返回的
 *          即该原文）；显示降级：Markdown 语法解析未建，按纯文本写入
 *          内嵌编辑器并同步富文本文档，同时复位粗体/斜体/下划线（与
 *          setText 纯文本分支同口径）并请求重绘。
 * @note       Markdown 解析未建：标题/加粗/列表等标记不做转换，原文
 *             直接作为纯文本显示。
 * @param self 目标控件指针；NULL 无操作。
 * @param md UTF-8 Markdown 文本；NULL 按空串处理。
 * @return 无返回值。
 */
void XTextEdit_setMarkdown(XTextEdit* self, const char* md);

/** @brief 导出 Markdown 文本（对标 QTextEdit::toMarkdown 的平铺降级）。
 * @details 若内容经 setMarkdown 写入则返回其原文堆拷贝；否则以当前
 *          纯文本降级导出（纯文本本身是合法 Markdown）。
 * @note       Markdown 生成未建：不按文档结构生成标记语法。
 * @param self 目标控件指针；NULL 返回 NULL。
 * @return 堆上新建的 UTF-8 文本（调用方以 XFree_System 释放）；内存
 *         分配失败返回 NULL。
 */
char* XTextEdit_toMarkdown(const XTextEdit* self);

/** @brief 查询 Markdown 原文（markdown 属性 READ 承载）。
 * @param self 目标控件指针；NULL 或未经 setMarkdown 写入时返回空串。
 * @return setMarkdown 存储的原文（内部只读指针，调用方勿释放）。
 */
const char* XTextEdit_markdown(const XTextEdit* self);

/** @brief 设置纯文本内容（对标 QTextEdit::setPlainText）。
 * @details 与 setText 纯文本分支同口径：替换内嵌编辑器内容并复位
 *          粗体/斜体/下划线，富文本文档同步为同内容，同时清除
 *          setMarkdown 存储的原文（此后 toMarkdown 回退为纯文本导出）
 *          并请求重绘。与 setText 的差异：不做富文本探测，一律按纯
 *          文本处理。
 * @param self 目标控件指针；NULL 无操作。
 * @param text UTF-8 文本（\n 分段）；NULL 按空串处理。
 * @return 无返回值。
 */
void XTextEdit_setPlainText(XTextEdit* self, const char* text);

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

#endif /* XTEXTEDIT_ON */
