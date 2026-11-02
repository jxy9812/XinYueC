/**
 * @file       HsfMenuPage.h
 * @brief      通用菜单列表页：页头 + 可选「返回」钮 + 纵排导航按钮列。
 * @details    主菜单与各子菜单（监视/数据/设置）共用本模块装配（组合复用），
 *             条目/backIndex 在 create 时定死。按钮点击经导航回调透传
 *             页索引——本模块不感知页序语义。整页为堆分配 XWidget 载体，
 *             随主窗级联释放（demo_wrapTabPage 同款口径）。
 */
#ifndef HSFMENUPAGE_H
#define HSFMENUPAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "HsfPages.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XBoxLayout.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

#define HSF_MENU_MAX_ENTRIES 8 /**< 菜单按钮列上限（含「返回」钮）。 */

/** @brief 菜单列表页对象；m_base 必须是第一个成员（XObject 化接收者）。 */
typedef struct HsfMenuPage
{
    XWidget     m_base;       /**< 页容器；必须是第一个成员。 */
    XBoxLayout  m_layout;     /**< 页内垂直布局。 */
    XLabel      m_head;       /**< 页头标题。 */
    XPushButton m_btns[HSF_MENU_MAX_ENTRIES]; /**< 按钮列（返回钮居首）。 */
    int         m_pageIndex[HSF_MENU_MAX_ENTRIES]; /**< 按钮→目标页索引。 */
    HsfMenuNavFn m_navFn;        /**< 导航回调；NULL=按钮无动作。 */
    void*        m_navUserData;  /**< 回调用户数据（归属主窗）。 */
    int          m_count;        /**< 实际按钮数。 */
} HsfMenuPage;

/**
 * @brief      创建菜单列表页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param headText 页头标题（UTF-8，借用；NULL 不建页头）。
 * @param entries 条目数组（借用）；可为 NULL（仅返回钮）。
 * @param count 条目数；超过上限截断。
 * @param backIndex 「返回」钮目标页；负数=不建返回钮。
 * @param navFn 导航回调；NULL 时按钮无动作。
 * @param userData 透传回调的用户数据。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfMenuPage_create(XWidget* parent, const char* headText,
                            const HsfMenuEntry* entries, int count,
                            int backIndex, HsfMenuNavFn navFn,
                            void* userData);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFMENUPAGE_H */
