/**
 * @file       XTextBrowser.h
 * @brief      XTextBrowser 富文本浏览控件（对标 Qt 6.8 QTextBrowser
 *             核心公共 API）。
 * @details    继承 XPlainTextEdit（对标 QTextBrowser 继承 QTextEdit），
 *             默认只读；增加源导航 API（setSource/source/backward/
 *             forward/home/reload）与导航信号。第一版不做 HTML 渲染，
 *             setSource 仅记录 URL 并触发 sourceChanged。
 * @note       模块总开关 XTEXTBROWSER_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XTEXTBROWSER_H
#define XTEXTBROWSER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#if XPLAINTEXTEDIT_ON
#include "XTextEdit.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

XCLASS_DEFINE_BEGING(XTextBrowser)
XCLASS_DEFINE_EXTEND_END(XTextBrowser, XTextEdit)

typedef struct XTextBrowser
{
    XTextEdit m_base;  /**< 基类成员；必须是第一个。 */
    char m_source[256];     /**< 当前源 URL。 */
    bool m_openLinks;       /**< 链接可点击（默认 true）。 */
} XTextBrowser;

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XTextBrowser_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XTextBrowser_init(XTextBrowser* self, XWidget* parent, XWidgetFlags flags);
#define XTextBrowser_create(parent, flags) XTextBrowser_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XTextBrowser* XTextBrowser_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTextBrowser_deinit_base(self) XTextEdit_deinit_base((XTextEdit*)(self))
#define XTextBrowser_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      设置浏览源 URL（对标 setSource）。
 */
void XTextBrowser_setSource(XTextBrowser* self, const char* url);
/**
 * @brief      获取当前浏览源 URL（对标 source）。
 */
const char* XTextBrowser_source(const XTextBrowser* self);
/**
 * @brief      设置链接可点击（对标 setOpenLinks）。
 */
void XTextBrowser_setOpenLinks(XTextBrowser* self, bool open);
/**
 * @brief      获取链接可点击状态。
 */
bool XTextBrowser_openLinks(const XTextBrowser* self);
/**
 * @brief      导航后退（对标 backward）。
 */
void XTextBrowser_backward(XTextBrowser* self);
/**
 * @brief      导航前进（对标 forward）。
 */
void XTextBrowser_forward(XTextBrowser* self);
/**
 * @brief      回到首页（对标 home）。
 */
void XTextBrowser_home(XTextBrowser* self);
/**
 * @brief      刷新当前源（对标 reload）。
 */
void XTextBrowser_reload(XTextBrowser* self);

/**
 * @brief      源变化信号（真发射）。
 */
void* XTextBrowser_sourceChanged_signal(XTextBrowser* self, const char* url);
/**
 * @brief      后退可用信号（真发射）。
 */
void* XTextBrowser_backwardAvailable_signal(XTextBrowser* self, bool available);
/**
 * @brief      前进可用信号（真发射）。
 */
void* XTextBrowser_forwardAvailable_signal(XTextBrowser* self, bool available);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON */

#ifdef __cplusplus
}
#endif

/* ==================== 信号 ==================== */

void* XTextBrowser_anchorClicked_signal(XTextBrowser* self);
void* XTextBrowser_highlighted_signal(XTextBrowser* self);
#endif /* XTEXTBROWSER_H */