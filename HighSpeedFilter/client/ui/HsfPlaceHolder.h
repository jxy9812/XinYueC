/**
 * @file       HsfPlaceHolder.h
 * @brief      共占位叶子页：标题 + 说明 + 待接入提示（层次化居中排布）。
 * @details    各叶子子菜单（数字量/模拟量/…）先以本模块占位建骨架，
 *             真实页面内容后续在各页模块内替换实现。
 */
#ifndef HSFPLACEHOLDER_H
#define HSFPLACEHOLDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/**
 * @brief      创建占位叶子页（堆分配，随主窗级联释放）。
 * @param parent 父控件（堆叠宿主）。
 * @param title 页标题（UTF-8）。
 * @param desc 功能说明（UTF-8）。
 * @param hint 待接入提示（UTF-8）。
 * @param backIndex 返回目标页（保留：后续页内返回入口用）。
 * @param navFn 导航回调（保留：后续页内入口用）。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfPlaceHolder_create(XWidget* parent, const char* title,
                               const char* desc, const char* hint,
                               int backIndex, HsfMenuNavFn navFn,
                               void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFPLACEHOLDER_H */
