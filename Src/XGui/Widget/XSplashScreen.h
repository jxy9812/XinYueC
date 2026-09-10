/**
 * @file       XSplashScreen.h
 * @brief      XSplashScreen 启动画面控件（对标 Qt 6.8 QSplashScreen 全部
 *             公共 API）。
 * @details    功能范围：
 *             - setPixmap/pixmap（背景图）；
 *             - showMessage(text, alignment, color)/clearMessage/message
 *               （底部消息文本，alignment 为 XAlignment 位掩码）；
 *             - messageChanged(text) 信号；
 *             - finish(widget)：等待 widget 显示后关闭启动画面（第一版
 *               为直接关闭的简化实现）；
 *             - repaint()：立即重绘；
 *             - 鼠标点击关闭（对标 Qt 交互）。
 *             画面尺寸默认取 pixmap 尺寸（无图时 400x300）。
 * @note       模块总开关 XSPLASHSCREEN_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XPIXMAP_ON。
 * @author     XinYueC 团队
 */
#ifndef XSPLASHSCREEN_H
#define XSPLASHSCREEN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XPIXMAP_ON
#include "XPixmap.h"
#endif

#if XWIDGET_ON && XSPLASHSCREEN_ON

XCLASS_DEFINE_BEGING(XSplashScreen)
XCLASS_DEFINE_EXTEND_END(XSplashScreen, XWidget)

typedef struct XSplashScreen
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    XPixmap* m_pixmap;     /**< 背景图（拥有，可为 NULL）。 */
    char m_message[256];   /**< 当前消息文本。 */
    int m_alignment;       /**< 消息对齐（XAlignment 位掩码）。 */
    uint32_t m_color;      /**< 消息颜色（ARGB）。 */
} XSplashScreen;

XVtable* XSplashScreen_class_init(void);
void XSplashScreen_init(XSplashScreen* self, XWidget* parent,
                        XWidgetFlags flags);
#define XSplashScreen_create(parent, flags) XSplashScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XSplashScreen* XSplashScreen_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);
#define XSplashScreen_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XSplashScreen_delete_base(self) XClass_delete_base((XClass*)(self))

#if XPIXMAP_ON
/** @brief 设置背景图（对标 setPixmap；内部深拷贝）。 */
/**
 * @brief      设置位图。
 */
void XSplashScreen_setPixmap(XSplashScreen* self, const XPixmap* pixmap);
/** @brief 查询背景图（对标 pixmap()；借用指针）。 */
const XPixmap* XSplashScreen_pixmap(const XSplashScreen* self);
#endif /* XPIXMAP_ON */

/** @brief 显示消息（对标 showMessage；alignment 为 XAlignment 位掩码）。 */
/**
 * @brief      显示消息。
 */
void XSplashScreen_showMessage(XSplashScreen* self, const char* utf8,
                               int alignment, uint32_t color);
/** @brief 清除消息（对标 clearMessage）。 */
/**
 * @brief      清除消息。
 */
void XSplashScreen_clearMessage(XSplashScreen* self);
/** @brief 查询当前消息（对标 message()）。 */
/**
 * @brief      获取消息。
 */
const char* XSplashScreen_message(const XSplashScreen* self);
/** @brief 等待 widget 显示后关闭（对标 finish；第一版直接关闭）。 */
/**
 * @brief      完成启动画面。
 */
void XSplashScreen_finish(XSplashScreen* self, XWidget* widget);
/** @brief 立即重绘（对标 repaint()）。 */
void XSplashScreen_repaint(XSplashScreen* self);

/* ==================== 信号 ==================== */

/**
 * @brief      消息变化信号（真发射）。
 */
void* XSplashScreen_messageChanged_signal(XSplashScreen* self,
                                          const char* message);

#endif /* XWIDGET_ON && XSPLASHSCREEN_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSPLASHSCREEN_H */
