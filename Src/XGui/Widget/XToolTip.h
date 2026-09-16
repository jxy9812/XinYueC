/**
 * @file       XToolTip.h
 * @brief      XToolTip 工具提示静态 API 类（对标 Qt 6.8 QToolTip 全部
 *             公共 API）。
 * @details    功能范围：
 *             - showText(pos,text,widget,rect,msecShowTime)/hideText/
 *               isVisible/text；
 *             - 外观：font/setFont、palette/setPalette（XGui 使用
 *               XPalette 值类型，可用时直接存储与应用到提示控件）；
 *             - 实现为模块级静态状态：当前文本（XString* 拥有）、可见
 *               标志、位置、关联控件（借用）；首次 showText 时惰性创建
 *               全局顶层提示控件（XLabel 优先，XLABEL_ON=0 时退化为
 *               仅存储不显示），无窗口环境安全退化。
 * @note       模块沿用 XGuiConfig.h 既有控件开关模式：无新开关，直接
 *             在 XWIDGET_ON 下编译。msecShowTime 仅存储记录，本实现
 *             不提供自动隐藏定时器（@note 裁剪说明）。
 * @author     XinYueC 团队
 */
#ifndef XTOOLTIP_H
#define XTOOLTIP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XString.h"
#include "XWidget.h"
#include "XGeometry.h"
#include "XFont.h"
#include "XPalette.h"

#if XWIDGET_ON

/* ==================== 静态 API（对标 QToolTip 全部公共 API） ==================== */

/**
 * @brief      显示工具提示（对标 QToolTip::showText）。
 * @details    保存文本（深拷贝）与位置，更新可见状态，创建/复用全局
 *             顶层提示控件并移动到 (x,y) 后显示；关联控件仅借用存储，
 *             不取得所有权。无窗口环境（XWIDGET_ON=0 或平台无窗口）下
 *             安全退化：仅存储状态不显示。
 * @param      x 提示框左上角 X 坐标（屏幕/窗口坐标由控件体系解释）。
 * @param      y 提示框左上角 Y 坐标。
 * @param      text 提示文本借用指针；可为 NULL 表示空文本（等价
 *             hideText 语义）。
 * @param      widget 关联控件借用指针；可为 NULL（对标 QToolTip 的
 *             QWidget* w 默认 nullptr）。
 * @param      rect 屏幕矩形借用指针（用于放置策略）；可为 NULL。
 * @param      msecShowTime 显示时长（毫秒）；<0 表示使用系统默认。
 *             本实现仅存储记录，不启动自动隐藏定时器。
 * @return     无返回值。
 */
void XToolTip_showText(int x, int y, const XString* text, XWidget* widget,
                       const XRect* rect, int msecShowTime);

/**
 * @brief      显示工具提示（UTF-8 兼容重载，转发主版本）。
 * @param      x 提示框左上角 X 坐标。
 * @param      y 提示框左上角 Y 坐标。
 * @param      utf8 以 '\0' 结尾的 UTF-8 提示文本；可为 NULL。
 * @param      widget 关联控件借用指针；可为 NULL。
 * @param      rect 屏幕矩形借用指针；可为 NULL。
 * @param      msecShowTime 显示时长（毫秒）；<0 表示系统默认。
 * @return     无返回值。
 */
void XToolTip_showText_2(int x, int y, const char* utf8, XWidget* widget,
                         const XRect* rect, int msecShowTime);

/**
 * @brief      隐藏当前工具提示并清空状态（对标 QToolTip::hideText）。
 * @details    Qt 中 hideText 等价于 showText(QPoint(),QString())；本实现
 *             直接隐藏全局提示控件并清空文本/可见标志。
 * @return     无返回值。
 */
void XToolTip_hideText(void);

/**
 * @brief      查询工具提示当前是否可见（对标 QToolTip::isVisible）。
 * @return     可见返回 true；未显示或已隐藏返回 false。
 */
bool XToolTip_isVisible(void);

/**
 * @brief      获取当前提示文本的拷贝（对标 QToolTip::text）。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；未显示或无文本时返回 NULL。
 */
XString* XToolTip_text(void);

/**
 * @brief      获取当前提示字体副本（对标 QToolTip::font）。
 * @return     XFont 值副本（含深拷贝资源），调用方使用后必须
 *             XFont_deinit_base 释放。
 */
XFont XToolTip_font(void);

/**
 * @brief      设置提示字体（对标 QToolTip::setFont）。
 * @details    深拷贝存储；若全局提示控件已创建则同步应用。
 * @param      font 源字体借用指针；可为 NULL（恢复默认字体）。
 * @return     无返回值。
 */
void XToolTip_setFont(const XFont* font);

/**
 * @brief      获取当前提示调色板副本（对标 QToolTip::palette）。
 * @return     XPalette 值副本（无堆资源，无需释放）。
 */
XPalette XToolTip_palette(void);

/**
 * @brief      设置提示调色板（对标 QToolTip::setPalette）。
 * @details    深拷贝存储；若全局提示控件已创建则同步应用。
 * @param      palette 源调色板借用指针；可为 NULL（恢复默认调色板）。
 * @return     无返回值。
 */
void XToolTip_setPalette(const XPalette* palette);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON */
#endif /* XTOOLTIP_H */
