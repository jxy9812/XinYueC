/**
 * @file       XTabWidget.h
 * @brief      XTabWidget 选项卡容器控件（对标 Qt 6.8 QTabWidget）。
 * @details    XTabBar（顶部页签）+ 页容器（每页一个 XWidget 子容器）
 *             的组合控件：addTab/insertTab 接收页内容控件（借用），
 *             页容器几何由本控件在 resize 时分配；removeTab 移除页签
 *             （页控件不销毁，由调用方管理）。
 * @note       模块总开关 XTABWIDGET_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABWIDGET_H
#define XTABWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XTabBar.h"

#if XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTabWidget)
XCLASS_DEFINE_EXTEND_END(XTabWidget, XWidget)

/** @brief XTabWidget 选项卡容器对象。 */
typedef struct XTabWidget
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    XTabBar m_tabBar;                /**< 顶部页签条（拥有）。 */
    XWidget** m_pages;               /**< 页容器数组（每页 XWidget 拥有）。 */
    XWidget** m_clients;             /**< 用户内容控件（借用指针记录）。 */
    int m_count;                     /**< 页数。 */
    int m_capacity;                  /**< 容量。 */
    int m_currentIndex;              /**< 当前页。 */
    int m_tabPosition;               /**< 页签位置（North=0，其余保留字段）。 */
    bool m_tabsClosable;             /**< 可关闭（转发页签条）。 */
    bool m_movable;                  /**< 可拖动（转发页签条）。 */
} XTabWidget;

/* ==================== 生命周期 ==================== */

XVtable* XTabWidget_class_init(void);
void XTabWidget_init(XTabWidget* self, XWidget* parent, XWidgetFlags flags);
#define XTabWidget_create(parent, flags) XTabWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XTabWidget* XTabWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTabWidget_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XTabWidget_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== API（对标 QTabWidget public API 子集） ==================== */

/** @brief 追加页签（XString 主版本；对标 QTabWidget::addTab）。
 * @param self 目标控件指针。
 * @param page 页控件借用指针；不能为 NULL。
 * @param label 借用 XString*；不能为 NULL。
 * @return 新页签索引；参数无效时返回 -1。
 */
int XTabWidget_addTab(XTabWidget* self, XWidget* page, const XString* label);
/** @brief 追加页签（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param page 页控件借用指针；不能为 NULL。
 * @param label UTF-8 文本；不能为 NULL。
 * @return 新页签索引；参数无效时返回 -1。
 */
int XTabWidget_addTab_2(XTabWidget* self, XWidget* page, const char* label);
/** @brief 插入页签（XString 主版本；对标 QTabWidget::insertTab）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param page 页控件借用指针；不能为 NULL。
 * @param label 借用 XString*；不能为 NULL。
 * @return 插入位置索引；参数无效时返回 -1。
 */
int XTabWidget_insertTab(XTabWidget* self, int index, XWidget* page,
                         const XString* label);
/** @brief 插入页签（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param page 页控件借用指针；不能为 NULL。
 * @param label UTF-8 文本；不能为 NULL。
 * @return 插入位置索引；参数无效时返回 -1。
 */
int XTabWidget_insertTab_2(XTabWidget* self, int index, XWidget* page,
                           const char* label);
void XTabWidget_removeTab(XTabWidget* self, int index);
int XTabWidget_count(const XTabWidget* self);
int XTabWidget_currentIndex(const XTabWidget* self);
void XTabWidget_setCurrentIndex(XTabWidget* self, int index);
XWidget* XTabWidget_currentWidget(const XTabWidget* self);
XWidget* XTabWidget_widget(const XTabWidget* self, int index);
int XTabWidget_indexOf(const XTabWidget* self, const XWidget* page);
/** @brief 读取页签文本（返回新建 XString*，调用方负责 delete_base；对标 QTabWidget::tabText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 新建 XString*；参数无效时返回 NULL。
 */
XString* XTabWidget_tabText(const XTabWidget* self, int index);
/** @brief 读取页签文本（UTF-8 借用；对标 QTabWidget::tabText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 内部 UTF-8 借用指针；参数无效时返回空串，不得释放或修改。
 */
const char* XTabWidget_tabText_2(const XTabWidget* self, int index);
/** @brief 设置页签文本（XString 主版本；对标 QTabWidget::setTabText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XTabWidget_setTabText(XTabWidget* self, int index, const XString* text);
/** @brief 设置页签文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XTabWidget_setTabText_2(XTabWidget* self, int index, const char* text);
bool XTabWidget_isTabEnabled(const XTabWidget* self, int index);
void XTabWidget_setTabEnabled(XTabWidget* self, int index, bool enabled);
XTabBar* XTabWidget_tabBar(const XTabWidget* self);
/** @brief 设置当前页控件（对标 QTabWidget::setCurrentWidget）。
 * @param self 目标控件。
 * @param page 页控件借用指针；不能为 NULL。
 * @return 无返回值。
 */
void XTabWidget_setCurrentWidget(XTabWidget* self, XWidget* page);
/** @brief 设置页签图标路径（XString 主版本；嵌入式路径表达）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param path 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabWidget_setTabIcon(XTabWidget* self, int index, const XString* path);
/** @brief 设置页签图标路径（UTF-8 兼容重载）。 */
void XTabWidget_setTabIcon_2(XTabWidget* self, int index, const char* path);
/** @brief 读取页签图标路径（内部借用 XString*；不得释放）。 */
const XString* XTabWidget_tabIcon(const XTabWidget* self, int index);
/** @brief 读取页签图标路径（UTF-8 借用）。 */
const char* XTabWidget_tabIcon_2(const XTabWidget* self, int index);
/** @brief 设置页签提示（XString 主版本；对标 setTabToolTip）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param tip 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabWidget_setTabToolTip(XTabWidget* self, int index,
                              const XString* tip);
/** @brief 设置页签提示（UTF-8 兼容重载）。 */
void XTabWidget_setTabToolTip_2(XTabWidget* self, int index,
                                const char* tip);
/** @brief 设置页签帮助文本（XString 主版本；对标 setTabWhatsThis）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param text 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabWidget_setTabWhatsThis(XTabWidget* self, int index,
                                const XString* text);
/** @brief 设置页签帮助文本（UTF-8 兼容重载）。 */
void XTabWidget_setTabWhatsThis_2(XTabWidget* self, int index,
                                  const char* text);
/** @brief 设置页签可见（对标 setTabVisible）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param visible true 显示。
 * @return 无返回值。
 */
void XTabWidget_setTabVisible(XTabWidget* self, int index, bool visible);
/** @brief 查询页签可见。 @param self 目标控件。 @param index 页签号。 @return 可见返回 true。 */
bool XTabWidget_isTabVisible(const XTabWidget* self, int index);
/** @brief 设置页签条自动隐藏（对标 setTabBarAutoHide）。
 * @param self 目标控件。
 * @param enable true 自动隐藏。
 * @return 无返回值。
 */
void XTabWidget_setTabBarAutoHide(XTabWidget* self, bool enable);
/** @brief 查询页签条自动隐藏。 @param self 目标控件。 @return 自动隐藏返回 true。 */
bool XTabWidget_tabBarAutoHide(const XTabWidget* self);
int XTabWidget_tabPosition(const XTabWidget* self);
void XTabWidget_setTabPosition(XTabWidget* self, int position);
bool XTabWidget_tabsClosable(const XTabWidget* self);
void XTabWidget_setTabsClosable(XTabWidget* self, bool closable);
bool XTabWidget_isMovable(const XTabWidget* self);
void XTabWidget_setMovable(XTabWidget* self, bool movable);

/* ==================== 信号（转发页签条） ==================== */

void* XTabWidget_currentChanged_signal(XTabWidget* self, int index);
void* XTabWidget_tabClicked_signal(XTabWidget* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */
#ifdef __cplusplus
}
#endif

/* ==================== 信号 ==================== */

/**
 * @brief      tabBarClicked(int) 信号地址（对标 QTabWidget::
 *             tabBarClicked）。
 * @details    页签条上左键单击某页签时由 XTabBar 点击事件转发真发射；
 *             self 非 NULL 且有已连接槽时经 XObject_emitSignal 同步
 *             通知，否则只返回信号标识。
 * @param      self 目标选项卡容器指针；可为 NULL。
 * @return     不透明的 tabBarClicked 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XTabWidget_tabBarClicked_signal(XTabWidget* self);

/**
 * @brief      tabBarDoubleClicked(int) 信号地址（对标 QTabWidget::
 *             tabBarDoubleClicked）。
 * @details    页签条上双击某页签时由 XTabBar 双击事件转发真发射；
 *             self 非 NULL 且有已连接槽时经 XObject_emitSignal 同步
 *             通知，否则只返回信号标识。
 * @param      self 目标选项卡容器指针；可为 NULL。
 * @return     不透明的 tabBarDoubleClicked 信号标识；返回值不指向
 *             可释放对象，也不得解引用。
 */
void* XTabWidget_tabBarDoubleClicked_signal(XTabWidget* self);
#endif /* XTABWIDGET_H */
