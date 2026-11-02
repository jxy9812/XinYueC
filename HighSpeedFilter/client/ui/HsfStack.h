/**
 * @file       HsfStack.h
 * @brief      堆栈窗口（中段内容区宿主）：XStackedLayout 托管多页切换。
 * @details    宿主控件双向 Expanding（根垂直布局中段伸展段），页面经
 *             addPage 挂入堆叠，go/setCurrentIndex 切页。矩形由宿主
 *             布局自动分配，本模块不感知页面内容与页序语义。
 */
#ifndef HSFSTACK_H
#define HSFSTACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XStackedLayout.h"

#if XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON

/** @brief 堆栈窗口对象；m_host 必须是第一个成员（XObject 化接收者）。 */
typedef struct HsfStack
{
    XWidget        m_host;   /**< 中段宿主控件；必须是第一个成员。 */
    XStackedLayout m_layout; /**< 堆叠布局（托管在宿主上）。 */
} HsfStack;

/**
 * @brief      初始化堆栈窗口：建宿主控件并托管堆叠布局。
 * @param self 待初始化对象；不可为 NULL。
 * @param parent 父控件（通常为主窗根，挂进根垂直布局中段）。
 */
void HsfStack_init(HsfStack* self, XWidget* parent);

/**
 * @brief      取宿主控件（页面容器应以宿主为父创建）。
 * @param self 目标堆栈窗口。
 * @return     宿主控件借用指针。
 */
XWidget* HsfStack_host(HsfStack* self);

/**
 * @brief      向堆叠末尾追加页面控件。
 * @param self 目标堆栈窗口。
 * @param page 页面控件（父应为宿主；借用指针，随宿主级联析构）。
 * @return     页面索引；失败 -1。
 */
int HsfStack_addPage(HsfStack* self, XWidget* page);

/**
 * @brief      切换当前页（越界收敛到有效范围）。
 * @param self 目标堆栈窗口。
 * @param index 目标页索引。
 */
void HsfStack_go(HsfStack* self, int index);

/**
 * @brief      取当前页索引。
 * @param self 目标堆栈窗口。
 * @return     当前索引；空堆叠 -1。
 */
int HsfStack_current(HsfStack* self);

#endif /* XWIDGET_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFSTACK_H */
