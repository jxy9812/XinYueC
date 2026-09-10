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
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XVector* m_lines;           /**< 文本行数组（char*，拥有）。 */
    int m_cursorLine;           /**< 光标行（0 起）。 */
    int m_cursorCol;            /**< 光标列（字节偏移）。 */
    bool m_readOnly;            /**< 只读。 */
    int m_wrapMode;             /**< 换行模式（默认 WidgetWidth）。 */
    int m_maxBlockCount;        /**< 块数上限（0 = 无限制）。 */
    char m_placeholder[256];    /**< 占位文本。 */
    bool m_undoEnabled;         /**< 撤销开关（默认 true）。 */
    XVector* m_undoStack;       /**< 撤销快照栈（char*）。 */
    XVector* m_redoStack;       /**< 重做快照栈（char*）。 */
    bool m_selectionActive;     /**< 选区激活（selectAll 置位）。 */
} XPlainTextEdit;

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

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLAINTEXTEDIT_H */