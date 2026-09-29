/**
 * @file       XWindowDecoration_Protected.h
 * @brief      XWindowDecoration 内部钩子（仅供框架内部实现调用）。
 * @details    仅供框架内部实现使用，不对外公开：公开头 XWindowDecoration.h
 *             只承载装饰判定/边距/绘制/输入的公共入口；本头承载生命周期
 *             联动钩子（调用方=XWidget.c 析构路径），使用方必须显式
 *             include，不得依赖间接传递。
 * @note       随 XWIDGET_ON/XWINDOW_ON/XSTYLE_ON/XWINDOWEVENT_ON 生效，
 *             与公共头一致；未装饰/未登记时全部入口零开销短路。
 * @author     XinYueC 团队
 */
#ifndef XWINDOWDECORATION_PROTECTED_H
#define XWINDOWDECORATION_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XWindowDecoration.h"

#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

/**
 * @brief      顶层控件销毁摘项钩子：按控件指针摘除装饰注册表项。
 * @details    注册表键=顶层控件指针，而项的生命周期此前只挂在窗口销毁
 *             上——控件先于其桥接窗口消亡（直接 delete、deferred 删除
 *             序）时项残留为悬垂指针，后续任意窗口注销扫表将解引用已
 *             释放控件（ASan UAF，回归套件默认 CSD 下实测）。由
 *             VXWidget_deinit 在字段析构前调用；非顶层/未登记控件零效
 *             果。本模块创建的默认条经 deleteLater 异步释放（控件树析
 *             构随后撤销子树与挂起事件，无双重释放）；自定义条为借用，
 *             只随状态丢弃指针。
 * @param      top 正在反初始化的控件；可为 NULL。
 * @return     无。
 */
void XWindowDecoration_notifyTopDestroyed(XWidget* top);

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWINDOWDECORATION_PROTECTED_H */
