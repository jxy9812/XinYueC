/**
 * @file       XTitleBar_Protected.h
 * @brief      XTitleBar 保护接口（仅供子类、装饰模块与内部实现使用）。
 * @details    仅供子类和内部实现使用，不对外公开；普通应用代码不应直接
 *             包含或调用本文件中的接口。装饰模块（XWindowDecoration）
 *             经 XTitleBar_setActiveSubControls 注入按住/悬停的活动子控
 *             件位、经 XTitleBar_activeSubControls 借用读回查；
 *             XTitleBar_buildOption 组装绘制/命中共用的 CC_TitleBar 选
 *             项；XTitleBar_defaultHeight 返回样式度量的条高（几何钉位
 *             与输入拦截归装饰模块，本类只画）。虚函数表槽位枚举按项目
 *             惯例位于公共头文件 XTitleBar.h；子类和
 *             XTitleBar.c 必须显式包含本文件，不能依赖公共头文件的间
 *             接声明。
 *             本文件不依赖 Win32、POSIX、Qt 或其他平台 API。
 * @note       依赖 XTitleBar.h；模块随 XWIDGET_ON/XWINDOW_ON/XSTYLE_ON/
 *             XWINDOWEVENT_ON 裁剪。
 *             定制说明：继承 XTitleBar 覆写 paintEvent（虚槽
 *             EXWidget_PaintEvent，经 XWidget_paintEvent_base 分派）可
 *             完全接管条带外观；覆写 hitTest（虚槽 EXTitleBar_HitTest，
 *             经 XTitleBar_hitTest_base 分派）可定制按钮命中。覆写实
 *             现中需要基类行为时用 XClass_Parent 访问父类槽位，不得递
 *             归调用自身 *_base 入口。
 * @author     XinYueC 团队
 */
#ifndef XTITLEBAR_PROTECTED_H
#define XTITLEBAR_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XTitleBar.h"
#include "XStyleOption.h"

#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

/* ==================== 度量 ==================== */

/**
 * @brief      返回标题栏条高（样式 PM 度量；钳到容得下按钮的最小高度）。
 * @details    取 XStylePM_TitleBarHeight 并与按钮尺寸 +4px、8px 下限三
 *             者取大（原 XWindowDecoration 条高度量原样迁入）；装饰模
 *             块按该值钉位标题栏几何。样式缺席时返回 0（默认实现据此
 *             短路绘制与命中）。
 * @return     条高（像素，恒正）；样式缺席返回 0。
 */
int XTitleBar_defaultHeight(void);

/* ==================== 选项组装（绘制/命中共用，一次事实源） ==================== */

/**
 * @brief      组装 CC_TitleBar 样式选项（绘制与命中共用，一次事实源）。
 * @details    宿主顶层 = XWidget_parentWidget(self)：标题取宿主
 *             windowTitle（UTF-8 借用缓存）、图标取宿主 m_icon（借用）、
 *             flags/windowState 取宿主桥接窗口、焦点窗口 ==
 *             XGuiApplication_focusWindow 时置 Active 态（对标 WM 活动
 *             标题栏着色）；子控件位按窗口提示位组装（未定制时对齐 Qt
 *             默认：标题+系统菜单+最小/最大/关闭；XMENU_ON 关闭时收窄
 *             系统菜单钮；最大化/全屏时 □ 形换还原形）；活动子控件位注
 *             入 m_activeSubControls 并同步按下态高亮。整条矩形高取本
 *             控件真实几何高（绘制/排版/命中三方同源此矩形，控件画满
 *             自身几何；几何零高时退 defaultHeight 兜底）。
 * @param      self 目标标题栏控件；可为 NULL。
 * @param      opt 输出选项；调用方提供存储空间，成功时被完整初始化，
 *                 文本/图标为宿主内部数据借用指针，仅在本次绘制/命中
 *                 期间有效，调用方不得释放。
 * @param      outBarH 输出样式度量条高（defaultHeight；与整条矩形高
 *                 可不同，矩形高以本控件几何为准）；可为 NULL 表示不
 *                 需要。
 * @return     选项已组装返回 true；self/opt 为空、宿主非顶层窗口、桥
 *             接窗口未建或条高非法返回 false（opt 保持未初始化状态，
 *             调用方不得使用）。
 */
bool XTitleBar_buildOption(XTitleBar* self, XStyleOptionTitleBar* opt,
                           int* outBarH);

/* ==================== 活动子控件注入（装饰模块驱动） ==================== */

/**
 * @brief      注入当前活动子控件位并在变化后重绘本控件。
 * @details    装饰模块在按钮按住/悬停状态变化时调用（值采用原实现
 *             armed?armed:hot 的按住优先合并口径，0=清除）。本类仅存储
 *             单值，无法区分按住/悬停两态：组装选项时对非零值统一按
 *             Sunken+MouseOver 双置位注入（样式按下态优先绘制），无悬
 *             停的触屏设备观感与原实现完全一致，桌面悬停呈按下底色。
 * @param      self 目标标题栏控件；可为 NULL，NULL 时不执行操作。
 * @param      subControls 新活动子控件位（XStyleSC_TitleBar* 组合；
 *                          0=无活动子控件）。
 * @return     无返回值；值未变化时不重绘。
 */
void XTitleBar_setActiveSubControls(XTitleBar* self, int subControls);

/**
 * @brief      借用读当前活动子控件位。
 * @param      self 目标标题栏控件；可为 NULL。
 * @return     当前活动子控件位（XStyleSC_TitleBar* 组合）；空指针返回 0。
 */
int XTitleBar_activeSubControls(const XTitleBar* self);

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTITLEBAR_PROTECTED_H */
