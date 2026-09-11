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
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON

XCLASS_DEFINE_BEGING(XTextEdit)
XCLASS_DEFINE_EXTEND_END(XTextEdit, XAbstractScrollArea)

typedef struct XTextEdit
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XPlainTextEdit* m_editor;   /**< 内嵌多行编辑器（拥有）。 */
    bool m_bold;            /**< 当前粗体格式。 */
    bool m_italic;          /**< 当前斜体格式。 */
    bool m_underline;       /**< 当前下划线格式。 */
    uint32_t m_textColor;   /**< 当前文字颜色（ARGB）。 */
    int m_alignment;        /**< 当前对齐。 */
} XTextEdit;

XVtable* XTextEdit_class_init(void);
void XTextEdit_init(XTextEdit* self, XWidget* parent, XWidgetFlags flags);
#define XTextEdit_create(parent, flags) XTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
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

#endif /* XTEXTEDIT_ON */

#ifdef __cplusplus
}
#endif

void* XTextEdit_copyAvailable_signal(XTextEdit* self);
void* XTextEdit_cursorPositionChanged_signal(XTextEdit* self);
void* XTextEdit_modificationChanged_signal(XTextEdit* self);
void* XTextEdit_redoAvailable_signal(XTextEdit* self);
void* XTextEdit_selectionChanged_signal(XTextEdit* self);
void* XTextEdit_undoAvailable_signal(XTextEdit* self);
#endif /* XTEXTEDIT_H */