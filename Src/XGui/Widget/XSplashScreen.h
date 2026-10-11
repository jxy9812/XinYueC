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
#include "XString.h"
#if XPIXMAP_ON
#include "XPixmap.h"
#endif

#if XWIDGET_ON && XSPLASHSCREEN_ON

XCLASS_DEFINE_BEGING(XSplashScreen)
XCLASS_DEFINE_EXTEND_END(XSplashScreen, XWidget)

typedef struct XSplashScreen
{
    XWidget m_class;       /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XPixmap* m_pixmap;     /**< 背景图（拥有，可为 NULL）。 */
    XString* m_message;   /**< 当前消息文本（对象拥有）。 */
    int m_alignment;       /**< 消息对齐（XAlignment 位掩码）。 */
    uint32_t m_color;      /**< 消息颜色（ARGB）。 */
} XSplashScreen;

XVtable* XSplashScreen_class_init(void);
void XSplashScreen_init(XSplashScreen* self, XWidget* parent,
                        XWidgetFlags flags);
#define XSplashScreen_create(parent, flags) XSplashScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XSplashScreen* XSplashScreen_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);

#if XPIXMAP_ON
/**
 * @brief      设置背景图（对标 QSplashScreen::setPixmap）。
 * @param      self 目标启动画面；可为 NULL。
 * @param      pixmap 源位图借用指针；可为 NULL 表示清除背景图。函数
 *             内部深拷贝，调用方在返回后可立即释放源对象。
 * @return     无。参数非法时保持原对象不变。
 */
void XSplashScreen_setPixmap(XSplashScreen* self, const XPixmap* pixmap);
/** @brief 查询背景图（对标 pixmap()；借用指针）。 */
const XPixmap* XSplashScreen_pixmap(const XSplashScreen* self);
#endif /* XPIXMAP_ON */

/**
 * @brief      显示消息（对标 QSplashScreen::showMessage）。
 * @param      self 目标启动画面；可为 NULL。
 * @param      utf8 以 '\0' 结尾的 UTF-8 消息文本，按 UTF-8 解码；可为
 *             NULL 按空消息处理。文本由对象内部持有，不转移调用者
 *             所有权。
 * @param      alignment 对齐方式（XAlignment 位掩码组合）。
 * @param      color 消息颜色（ARGB）。
 * @return     无。参数非法时保持原对象不变。
 */
void XSplashScreen_showMessage(XSplashScreen* self, const char* utf8,
                               int alignment, uint32_t color);
/**
 * @brief      清除底部消息文本并发射 messageChanged("")（对标
 *             QSplashScreen::clearMessage；Qt 不做空串判定，空消息再
 *             清除仍发射）。
 * @param      self 目标启动画面；可为 NULL。
 * @return     无。
 */
void XSplashScreen_clearMessage(XSplashScreen* self);
/**
 * @brief      查询当前消息的 UTF-8 文本（对标 QSplashScreen::message）。
 * @param      self 目标启动画面；可为 NULL。
 * @return     借用指针：指向对象内部消息的 UTF-8 缓存，禁止释放或
 *             修改，下次修改消息后失效；无消息或 self 为 NULL 返回
 *             空串。
 */
const char* XSplashScreen_message(const XSplashScreen* self);
/**
 * @brief      结束并关闭启动画面（对标 QSplashScreen::finish；当前为
 *             直接关闭的简化实现，不等待 widget 显示）。
 * @param      self 目标启动画面；可为 NULL。
 * @param      widget 待等待显示的控件借用指针；当前版本未使用，可传
 *             NULL。
 * @return     无。
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
