/**
 * @file       XPlainTextEdit.h
 * @brief      XPlainTextEdit 多行纯文本编辑控件（对标 Qt 6.8
 *             QPlainTextEdit 核心公共 API）。
 * @details    功能范围：
 *             - 继承 XAbstractScrollArea：视口 + 双滚动条 + 内容尺寸
 *               联动（行数 x 行高）；
 *             - 文本：setPlainText/toPlainText、appendPlainText、
 *               insertPlainText、clear（
 分块存储，逐行绘制）；
 *             - 光标：行/列内部光标，Left/Right/Up/Down/Home/End 移动，
 *               ensureCursorVisible；
 *             - 编辑：可打印字符插入、Backspace/Delete、Enter 分行；
 *             - 剪贴板：copy/cut/paste（XClipboard，对标 Qt 剪贴板交互）；
 *             - selectAll（对标）；undo/redo（快照栈）；
 *             - 只读 setReadOnly/isReadOnly；
 *             - LineWrapMode 枚举（NoWrap/WidgetWidth，数值对齐；第一版
 *               存储不换行绘制）；
 *             - maximumBlockCount（块数上限，超限丢弃最旧块）；
 *             - placeholderText 占位文本（空内容灰显）；
 *             - 信号 textChanged()。
 *             与 Qt 差异：富文本/QTextDocument/HTML 子集暂不涉及。
 * @note       模块总开关 XPLAINTEXTEDIT_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XPLAINTEXTEDIT_H
#define XPLAINTEXTEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#include "XTextDocument.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON

/** @brief 换行模式（对标 QPlainTextEdit::LineWrapMode，数值一致）。 */
typedef enum XPlainTextEditMode
{
    XPlainTextEditMode_NoWrap = 0,      /**< 不换行。 */
    XPlainTextEditMode_WidgetWidth = 1  /**< 按控件宽度换行。 */
} XPlainTextEditMode;

XCLASS_DEFINE_BEGING(XPlainTextEdit)
XCLASS_DEFINE_EXTEND_END(XPlainTextEdit, XAbstractScrollArea)

typedef struct XPlainTextEdit
{
    XAbstractScrollArea m_base;
#if XTEXTDOCUMENT_ON
    XTextDocument* m_textDoc; /**< 富文本文档。 */
#endif
    XVector* m_lines;           /**< 文本行数组（char*，拥有）。 */
    int m_cursorLine;           /**< 光标行（0 起）。 */
    int m_cursorCol;            /**< 光标列（字节偏移）。 */
    bool m_readOnly;            /**< 只读。 */
    int m_wrapMode;             /**< 换行模式（默认 WidgetWidth）。 */
    int m_maxBlockCount;        /**< 块数上限（0 = 无限制）。 */
    XString* m_placeholder;    /**< 占位文本（对象拥有）。 */
    bool m_undoEnabled;         /**< 撤销开关（默认 true）。 */
    XVector* m_undoStack;       /**< 撤销快照栈（char*）。 */
    XVector* m_redoStack;       /**< 重做快照栈（char*）。 */
    bool m_selectionActive;     /**< 选区激活（selectAll 置位）。 */
} XPlainTextEdit;

/** @brief XPlain文本Editclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XPlainTextEdit_class_init(void);
void XPlainTextEdit_init(XPlainTextEdit* self, XWidget* parent,
                         XWidgetFlags flags);
#define XPlainTextEdit_create(parent, flags) XPlainTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XPlainTextEdit* XPlainTextEdit_create_ex(XMemoryType memory,
                                         XWidget* parent, XWidgetFlags flags);
#define XPlainTextEdit_deinit_base(self) XAbstractScrollArea_deinit_base((XAbstractScrollArea*)(self))
#define XPlainTextEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 文本（对标 QPlainTextEdit public API） ========== */

/**
 * @brief      设置纯文本。
 */
void XPlainTextEdit_setPlainText(XPlainTextEdit* self, const char* utf8);
/** @brief 导出纯文本（\n 连接全部块；返回堆拷贝，调用方 XFree_System）。 */
/**
 * @brief      导出纯文本。
 */
char* XPlainTextEdit_toPlainText(const XPlainTextEdit* self);
/**
 * @brief      追加纯文本行。
 */
void XPlainTextEdit_appendPlainText(XPlainTextEdit* self, const char* utf8);
/**
 * @brief      在光标处插入文本。
 */
void XPlainTextEdit_insertPlainText(XPlainTextEdit* self, const char* utf8);
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XPlainTextEdit_clear(XPlainTextEdit* self);

/* ==================== 编辑与属性 ==================== */

/**
 * @brief      获取只读状态。
 */
bool XPlainTextEdit_isReadOnly(const XPlainTextEdit* self);
/**
 * @brief      设置只读。
 */
void XPlainTextEdit_setReadOnly(XPlainTextEdit* self, bool readOnly);
/**
 * @brief      获取换行模式。
 */
int XPlainTextEdit_lineWrapMode(const XPlainTextEdit* self);
/**
 * @brief      设置换行模式。
 */
void XPlainTextEdit_setLineWrapMode(XPlainTextEdit* self, int mode);
/**
 * @brief      获取最大块数。
 */
int XPlainTextEdit_maximumBlockCount(const XPlainTextEdit* self);
/**
 * @brief      设置最大块数。
 */
void XPlainTextEdit_setMaximumBlockCount(XPlainTextEdit* self, int maximum);
/**
 * @brief      设置占位文本。
 */
void XPlainTextEdit_setPlaceholderText(XPlainTextEdit* self, const char* utf8);
/**
 * @brief      获取占位文本。
 */
const char* XPlainTextEdit_placeholderText(const XPlainTextEdit* self);
/**
 * @brief      获取撤销重做开关。
 */
bool XPlainTextEdit_isUndoRedoEnabled(const XPlainTextEdit* self);
/**
 * @brief      设置撤销重做开关。
 */
void XPlainTextEdit_setUndoRedoEnabled(XPlainTextEdit* self, bool enable);
/**
 * @brief      获取光标行。
 */
int XPlainTextEdit_cursorLine(const XPlainTextEdit* self);
/**
 * @brief      获取光标列。
 */
int XPlainTextEdit_cursorColumn(const XPlainTextEdit* self);

/* ==================== 编辑槽（对标 public slots） ==================== */

void XPlainTextEdit_undo(XPlainTextEdit* self);
/** @brief XPlain文本Editredo（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XPlainTextEdit_redo(XPlainTextEdit* self);
/**
 * @brief      复制。
 */
void XPlainTextEdit_copy(XPlainTextEdit* self);
/**
 * @brief      剪切。
 */
void XPlainTextEdit_cut(XPlainTextEdit* self);
/**
 * @brief      粘贴。
 */
void XPlainTextEdit_paste(XPlainTextEdit* self);
/**
 * @brief      全选。
 */
void XPlainTextEdit_selectAll(XPlainTextEdit* self);
/**
 * @brief      确保光标可见。
 */
void XPlainTextEdit_ensureCursorVisible(XPlainTextEdit* self);

/* ==================== 信号 ==================== */

/**
 * @brief      文本变化信号（真发射）。
 */
void* XPlainTextEdit_textChanged_signal(XPlainTextEdit* self);
/**
 * @brief      光标位置变化信号（真发射）。
 */
void* XPlainTextEdit_cursorPositionChanged_signal(XPlainTextEdit* self);

/**
 * @brief      视口重绘请求信号（对标 QPlainTextEdit::updateRequest）。
 * @details    滚动或内容变化触发视口重绘时真发射，载荷为需要重绘的
 *             视口矩形与垂直滚动增量 dy（内容上移为正，0 表示全量）；
 *             self 非 NULL 且有已连接槽时经 XObject_emitSignal 同步
 *             通知，否则只返回信号标识。
 * @param      self 目标控件指针；可为 NULL。
 * @param      rect 需要重绘的视口区域；NULL 视为整个视口。
 * @param      dy 垂直滚动增量（像素）。
 * @return     不透明的 updateRequest 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XPlainTextEdit_updateRequest_signal(XPlainTextEdit* self,
                                          const XRect* rect, int dy);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */

#ifdef __cplusplus
}
#endif

/** @brief XPlain文本Editblock数量变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_blockCountChanged_signal(XPlainTextEdit* self);
/** @brief XPlain文本EditcopyAvailable 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_copyAvailable_signal(XPlainTextEdit* self);
/** @brief XPlain文本Editmodification变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_modificationChanged_signal(XPlainTextEdit* self);
/** @brief XPlain文本EditredoAvailable 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_redoAvailable_signal(XPlainTextEdit* self);
/** @brief XPlain文本Editselection变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self);
/** @brief XPlain文本EditundoAvailable 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_undoAvailable_signal(XPlainTextEdit* self);
/** @brief XPlain文本Editset文本背景颜色2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param color ARGB 颜色值。
 * @return 无返回值。
 */
void XPlainTextEdit_setTextBackgroundColor_2(XPlainTextEdit* self, uint32_t color);
/** @brief XPlain文本Edittext背景颜色2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
uint32_t XPlainTextEdit_textBackgroundColor_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset字体Family2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param family 字体族。
 * @return 无返回值。
 */
void XPlainTextEdit_setFontFamily_2(XPlainTextEdit* self, const char* family);
/** @brief XPlain文本EditfontFamily2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XPlainTextEdit_fontFamily_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset字体Weight2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param weight int 参数。
 * @return 无返回值。
 */
void XPlainTextEdit_setFontWeight_2(XPlainTextEdit* self, int weight);
/** @brief XPlain文本EditfontWeight2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_fontWeight_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset字体Point尺寸2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param size 尺寸（像素）。
 * @return 无返回值。
 */
void XPlainTextEdit_setFontPointSize_2(XPlainTextEdit* self, double size);
/** @brief XPlain文本EditfontPoint尺寸2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
double XPlainTextEdit_fontPointSize_2(const XPlainTextEdit* self);
/** @brief XPlain文本EditzoomIn2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param range 范围值。
 * @return 无返回值。
 */
void XPlainTextEdit_zoomIn_2(XPlainTextEdit* self, int range);
/** @brief XPlain文本EditzoomOut2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param range 范围值。
 * @return 无返回值。
 */
void XPlainTextEdit_zoomOut_2(XPlainTextEdit* self, int range);
/** @brief XPlain文本Printset居中On滚动2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param enabled bool 开关：true 启用。
 * @return 无返回值。
 */
void XPlainTextPrint_setCenterOnScroll_2(XPlainTextEdit* self, bool enabled);
/** @brief XPlain文本EditcenterOn滚动2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XPlainTextEdit_centerOnScroll_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editblock数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_blockCount_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editcharacter数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_characterCount_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset额外Selections2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param selections 选择集指针。
 * @return 无返回值。
 */
void XPlainTextEdit_setExtraSelections_2(XPlainTextEdit* self, void* selections);
/** @brief XPlain文本Editset单词换行模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param policy 策略枚举。
 * @return 无返回值。
 */
void XPlainTextEdit_setWordWrapMode_2(XPlainTextEdit* self, int policy);
/** @brief XPlain文本Editword换行模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_wordWrapMode_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset光标宽2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param width 宽（像素）。
 * @return 无返回值。
 */
void XPlainTextEdit_setCursorWidth_2(XPlainTextEdit* self, int width);
/** @brief XPlain文本Editcursor宽2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorWidth_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset页签StopDistance2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param distance double 参数。
 * @return 无返回值。
 */
void XPlainTextEdit_setTabStopDistance_2(XPlainTextEdit* self, double distance);
/** @brief XPlain文本EdittabStopDistance2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
double XPlainTextEdit_tabStopDistance_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editanchor于（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param x X 坐标（像素）。
 * @param y Y 坐标（像素）。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_anchorAt(XPlainTextEdit* self, int x, int y);
/** @brief XPlain文本Editset背景可见2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param visible bool：true 可见。
 * @return 无返回值。
 */
void XPlainTextEdit_setBackgroundVisible_2(XPlainTextEdit* self, bool visible);
/** @brief XPlain文本Editbackground可见2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XPlainTextEdit_backgroundVisible_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset页签Changes焦点2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param b bool 参数。
 * @return 无返回值。
 */
void XPlainTextEdit_setTabChangesFocus_2(XPlainTextEdit* self, bool b);
/** @brief XPlain文本EdittabChanges焦点2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XPlainTextEdit_tabChangesFocus_2(const XPlainTextEdit* self);
/** @brief XPlain文本EditblockBounding矩形y（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param block int 参数。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_blockBoundingRect_y(const XPlainTextEdit* self, int block);
/** @brief XPlain文本Editset最大Block数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param maximum 最大值。
 * @return 无返回值。
 */
void XPlainTextEdit_setMaximumBlockCount_2(XPlainTextEdit* self, int maximum);
/** @brief XPlain文本EditmaximumBlock数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_maximumBlockCount_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editfind2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param exp 匹配表达式。
 * @param flags 窗口标志位组合。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XPlainTextEdit_find_2(XPlainTextEdit* self, const char* exp, int flags);
/** @brief XPlain文本Editset文本交互Flags2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XPlainTextEdit_setTextInteractionFlags_2(XPlainTextEdit* self, int flags);
/** @brief XPlain文本Edittext交互Flags2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_textInteractionFlags_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editprint2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param printer 打印机指针。
 * @return 无返回值。
 */
void XPlainTextEdit_print_2(XPlainTextEdit* self, void* printer);
/** @brief XPlain文本EditcreateStandardContext菜单2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XPlainTextEdit_createStandardContextMenu_2(XPlainTextEdit* self);
/** @brief XPlain文本Editmove光标3（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param operation int 参数。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XPlainTextEdit_moveCursor_3(XPlainTextEdit* self, int operation, int mode);
/** @brief XPlain文本Editcursor矩形width2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorRect_width_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editcenter光标（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XPlainTextEdit_centerCursor(XPlainTextEdit* self);
/** @brief XPlain文本EditcursorCan粘贴2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XPlainTextEdit_cursorCanPaste_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editset覆盖模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param overwrite bool 参数。
 * @return 无返回值。
 */
void XPlainTextEdit_setOverwriteMode_2(XPlainTextEdit* self, bool overwrite);
/** @brief XPlain文本Editoverwrite模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XPlainTextEdit_overwriteMode_2(const XPlainTextEdit* self);
/** @brief XPlain文本Editline宽2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_lineWidth_2(const XPlainTextEdit* self);
/** @brief XPlain文本EditcursorX（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorX(const XPlainTextEdit* self);
/** @brief XPlain文本EditcursorY（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorY(const XPlainTextEdit* self);
/** @brief XPlain文本EditsetPlain文本Margins（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param left 左侧坐标/列号。
 * @param top 顶部坐标/行号。
 * @param right 右侧坐标/列号。
 * @param bottom 底部坐标/行号。
 * @return 无返回值。
 */
void XPlainTextEdit_setPlainTextMargins(XPlainTextEdit* self, int left, int top, int right, int bottom);
/** @brief XPlain文本Editcursor高（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorHeight(const XPlainTextEdit* self);
/** @brief XPlain文本EditcontentOffsetY（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_contentOffsetY(const XPlainTextEdit* self);
/** @brief XPlain文本Editline间距（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_lineSpacing(const XPlainTextEdit* self);
/** @brief XPlain文本EditfontAscent（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_fontAscent(const XPlainTextEdit* self);
/** @brief XPlain文本EditlineHeight2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_lineHeight2(const XPlainTextEdit* self);
/** @brief XPlain文本Editblock高（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param blockIndex int 参数。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_blockHeight(const XPlainTextEdit* self, int blockIndex);
/** @brief XPlain文本EditvisibleBlock数量（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_visibleBlockCount(const XPlainTextEdit* self);
/** @brief XPlain文本Editfirst可见Block（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_firstVisibleBlock(const XPlainTextEdit* self);
/** @brief XPlain文本Editlast可见Block（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_lastVisibleBlock(const XPlainTextEdit* self);
/** @brief XPlain文本Editset页签Stop宽（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param width 宽（像素）。
 * @return 无返回值。
 */
void XPlainTextEdit_setTabStopWidth(XPlainTextEdit* self, int width);
/** @brief XPlain文本EdittabStop宽（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_tabStopWidth(const XPlainTextEdit* self);
/** @brief XPlain文本Editcursor单词Left（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorWordLeft(const XPlainTextEdit* self);
/** @brief XPlain文本Editcursor单词Right（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XPlainTextEdit_cursorWordRight(const XPlainTextEdit* self);
#endif /* XPLAINTEXTEDIT_H */