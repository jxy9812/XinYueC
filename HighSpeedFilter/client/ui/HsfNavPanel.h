/**
 * @file       HsfNavPanel.h
 * @brief      左侧停靠导航面板：菜单树（父节点 + 缩进子节点）。
 * @details    宽屏形态停靠窗口左侧（XSplitter 内首条目，拖拽把手可改
 *             宽度）；窄屏（竖屏）形态由主窗强制收起，导航改由主菜单
 *             页承载。条目树来自 HsfPages_navTree（与紧凑菜单同源），
 *             子节点按层级缩进挂在父节点下（同 XGuiWindowDemo 侧栏
 *             树形口径）。
 *
 *             布局机制**对标 XGuiWindowDemo 侧栏**：滚动区
 *             widgetResizable=false + 内容区显式 resize + 条目绝对
 *             几何定位（不用盒布局——盒布局在有余量时会把条目均分
 *             拉开，且内容区被撑到视口高后失去滚动语义）。
 *             面板自身尺寸变化由类内 resizeEvent 重载即时重排（分割条
 *             拖拽过程中条目宽度逐帧跟随，同 Qt 布局激活语义）。
 *             条目可勾选并入互斥组（当前页常亮）。
 */
#ifndef HSFNAVPANEL_H
#define HSFNAVPANEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XButtonGroup.h"
#include "XScrollArea.h"
#include "XFrame.h"
#include "HsfPages.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XSCROLLAREA_ON

#define HSF_NAV_MAX_NODES 24 /**< 导航条目上限。 */

/** @brief 导航点击回调（userData=归属主窗，index=目标页）。 */
typedef void (*HsfNavPanelNavFn)(void* userData, int index);

/** @brief 导航树条目对象：一条目（按钮）。
 *  @details 按钮**直接挂内容区**（几何按内容区坐标定位，对标
 *           XGuiWindowDemo 侧栏）；导航经互斥组 idClicked 单连接
 *           派发（组 id = 条目下标），故条目无需自持容器。 */
typedef struct HsfNavItem
{
    XPushButton   m_btn;       /**< 条目按钮（父=内容区）。 */
    int           m_nodeIndex; /**< 对应树节点下标。 */
    int           m_parent;    /**< 父节点下标（-1=顶层）。 */
} HsfNavItem;

/** @brief 左侧导航面板对象；m_base 必须是第一个成员。 */
typedef struct HsfNavPanel
{
    XWidget     m_base;      /**< 面板容器；必须是第一个成员。 */
    XLabel      m_title;     /**< 面板标题「导航」。 */
    XScrollArea m_scroll;    /**< 滚动壳（条目多时可滚；不可拉伸）。 */
    XWidget     m_content;   /**< 滚动内容（条目宿主；显式 resize）。 */
    XButtonGroup m_group;    /**< 条目互斥组（当前页常亮）。 */
    HsfNavItem  m_items[HSF_NAV_MAX_NODES]; /**< 各树节点条目。 */
    int         m_count;      /**< 条目数。 */
    bool        m_ready;      /**< 初始化完成（子控件齐备）后才允许 resize
                                *   重排——构造中途收到 RESIZE 必须跳过。 */
    bool        m_mobile;     /**< 移动态（竖屏菜单页）：44px 触控行 +
                                *   分区间距、无标题行（同 XGuiWindowDemo
                                *   移动端菜单口径）。 */
    int         m_expanded;   /**< 当前展开的主菜单节点下标（-1=全收）。
                                *   单展开手风琴：点另一个主菜单收起前一个。 */
    int         m_activePage; /**< 当前页（高亮）。 */
    HsfNavPanelNavFn m_navFn;      /**< 导航回调。 */
    void*        m_navUserData;   /**< 回调用户数据。 */
} HsfNavPanel;

/**
 * @brief      初始化导航面板（标题 + 滚动壳 + 树条目）。
 * @param self 待初始化对象；不可为 NULL。
 * @param parent 父控件（分割条）。
 */
void HsfNavPanel_init(HsfNavPanel* self, XWidget* parent);

/**
 * @brief      初始化导航面板（移动态：竖屏菜单页用的树形列表）。
 * @param self 待初始化对象；不可为 NULL。
 * @param parent 父控件。
 * @param mobile true=移动态（44px 触控行/分区间距/无标题行）。
 */
void HsfNavPanel_initMobile(HsfNavPanel* self, XWidget* parent);

/**
 * @brief      注册导航回调。
 * @param self 目标面板。
 * @param fn   回调（点击条目时透传目标页索引）。
 * @param userData 透传给回调的用户数据。
 */
void HsfNavPanel_setNavHandler(HsfNavPanel* self, HsfNavPanelNavFn fn,
                               void* userData);

/**
 * @brief      设当前页（高亮对应条目）。
 * @param self 目标面板。
 * @param pageIndex 当前页索引；越界忽略。
 */
void HsfNavPanel_setActivePage(HsfNavPanel* self, int pageIndex);

/**
 * @brief      程序化点击指定条目（自测/无头验证用；等价用户点击）。
 * @param self 目标面板。
 * @param itemIndex 条目下标（= 互斥组 id）。
 */
void HsfNavPanel_clickItem(HsfNavPanel* self, int itemIndex);

/**
 * @brief      按当前面板尺寸重排条目几何（面板宽度变化/内容变化后调用）。
 * @details 标题行 + 滚动壳占位 + 条目树（顶层左对齐，子节点缩进）。
 *          **面板自身尺寸变化已自动重排**（本类重载 resizeEvent，分割条
 *          拖拽/布局重排都会即时刷新，见 HsfNavPanel.c）；本入口供
 *          「尺寸没变但要重排」的场合显式调用（手风琴展开收起/切页）。
 * @param self 目标面板。
 */
void HsfNavPanel_update(HsfNavPanel* self);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XSCROLLAREA_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFNAVPANEL_H */
