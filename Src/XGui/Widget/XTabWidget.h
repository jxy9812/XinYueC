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
    XWidget* m_cornerWidgets[4];     /**< 四角部件（借用，不拥有；下标对标
                                          Qt::Corner：0=TopLeft，1=TopRight，
                                          2=BottomLeft，3=BottomRight）。 */
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
/** @brief 设置当前页的内容控件（对标 setWidget 系列替换语义）。
 * @details 当前页已有内容时：旧控件经 XWidget_setParent(NULL) 摘除
 *          父链转独立顶层（不销毁，所有权转移调用方，Qt setWidget
 *          家族同语义），随后装入新控件（reparent 到当前页容器）并
 *          重排布局、同步显隐。widget 与现有内容同指针时幂等忽略；
 *          传 NULL 等价清空当前页内容。新控件若已登记在其它页
 *          （indexOf 命中），仅解除该页借用记录——控件实体随
 *          reparent 归入当前页，原页转为无内容。尚无任何页时不动作
 *          （建页须页签文本，走 addTab/insertTab）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param widget 内容控件借用指针；可为 NULL（清空当前页）。
 * @return 无返回值。
 */
void XTabWidget_setWidget(XTabWidget* self, XWidget* widget);
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
/** @brief 移除全部页签（对标 QTabWidget::clear）。
 * @details 等价于循环移除全部页；页控件按所有者规则一并释放，
 *          用户内容控件仅解除借用记录。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XTabWidget_clear(XTabWidget* self);
/** @brief 设置文档模式（对标 QTabWidget::setDocumentMode；转发页签条）。 */
void XTabWidget_setDocumentMode(XTabWidget* self, bool enable);
/** @brief 查询文档模式（对标 QTabWidget::documentMode；转发页签条）。 */
bool XTabWidget_documentMode(const XTabWidget* self);
/** @brief 设置省略模式（对标 QTabWidget::setElideMode；转发页签条）。 */
void XTabWidget_setElideMode(XTabWidget* self, int mode);
/** @brief 查询省略模式（对标 QTabWidget::elideMode；转发页签条）。 */
int XTabWidget_elideMode(const XTabWidget* self);
/** @brief 设置页签形状（对标 QTabWidget::setTabShape；转发页签条）。 */
void XTabWidget_setTabShape(XTabWidget* self, int shape);
/** @brief 查询页签形状（对标 QTabWidget::tabShape；转发页签条）。 */
int XTabWidget_tabShape(const XTabWidget* self);
/** @brief 设置滚动按钮（对标 QTabWidget::setUsesScrollButtons；转发页签条）。 */
void XTabWidget_setUsesScrollButtons(XTabWidget* self, bool enable);
/** @brief 查询滚动按钮（对标 QTabWidget::usesScrollButtons；转发页签条）。 */
bool XTabWidget_usesScrollButtons(const XTabWidget* self);
/** @brief 设置页签图标尺寸（对标 QTabWidget::setIconSize；转发页签条，
 *         单 int 方边值承载）。
 */
void XTabWidget_setIconSize(XTabWidget* self, int size);
/** @brief 查询页签图标尺寸（对标 QTabWidget::iconSize；转发页签条）。 */
int XTabWidget_iconSize(const XTabWidget* self);
/** @brief 查询页签提示（对标 QTabWidget::tabToolTip；转发页签条）。
 * @param self 目标控件；传入 NULL 或页签号无效时返回 NULL。
 * @param index 页签号。
 * @return 借用内部 XString 指针；禁止释放或修改。
 */
const XString* XTabWidget_tabToolTip(const XTabWidget* self, int index);
/** @brief 查询页签帮助文本（对标 QTabWidget::tabWhatsThis；转发页签条）。
 * @details 项目简化：帮助文本与提示共用存储，返回值同 tabToolTip。
 * @param self 目标控件；传入 NULL 或页签号无效时返回 NULL。
 * @param index 页签号。
 * @return 借用内部 XString 指针；禁止释放或修改。
 */
const XString* XTabWidget_tabWhatsThis(const XTabWidget* self, int index);

/** @brief 角部件枚举码（对标 Qt::Corner）。
 * @details 0=TopLeftCorner（左上），1=TopRightCorner（右上），
 *          2=BottomLeftCorner（左下），3=BottomRightCorner（右下）。
 *          Qt 仅布局上两角，下两角为预留位。
 */
#define XTABWIDGET_CORNER_TOPLEFT     0
#define XTABWIDGET_CORNER_TOPRIGHT    1
#define XTABWIDGET_CORNER_BOTTOMLEFT  2
#define XTABWIDGET_CORNER_BOTTOMRIGHT 3

/** @brief 读取指定角的角部件（对标 QTabWidget::cornerWidget；借用，不拥有）。
 * @param self 目标控件；传入 NULL 时返回 NULL。
 * @param corner 角部件枚举码（0..3，对标 Qt::Corner；越界返回 NULL）。
 * @return 角部件借用指针；该角未设置时返回 NULL，不得释放。
 */
XWidget* XTabWidget_cornerWidget(const XTabWidget* self, int corner);
/** @brief 设置指定角的角部件（对标 QTabWidget::setCornerWidget；借用挂载）。
 * @details 角部件按借用语义挂到本控件（reparent 为子控件，本体生命期
 *          归调用方管理）；同角旧部件被隐藏并解除登记（不销毁，Qt 同
 *          语义）。widget 传 NULL 等价于清除该角。
 * @note    内部几何放置：上两角在页签条行内按当前尺寸放置（左上贴
 *          左缘、右上贴右缘，高度截到页签条高）；下两角为预留位，
 *          仅承载不参与布局（若布局未接则仅承载）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param widget 角部件借用指针；可为 NULL（清除该角）。
 * @param corner 角部件枚举码（0..3，对标 Qt::Corner；越界忽略）。
 * @return 无返回值。
 */
void XTabWidget_setCornerWidget(XTabWidget* self, XWidget* widget, int corner);

/* ==================== 信号（转发页签条） ==================== */

void* XTabWidget_currentChanged_signal(XTabWidget* self, int index);
void* XTabWidget_tabClicked_signal(XTabWidget* self);
/** @brief tabCloseRequested(int) 信号地址（对标 QTabWidget::
 *         tabCloseRequested；载荷：页签索引）。
 * @details 发射点为“tabsClosable 且点击页签关闭按钮”——发射路径在
 *          XTabBar 侧（QTabBar::tabCloseRequested 对标位），本控件经
 *          init 中的 connect_2 转发真发射。当前 XTabBar 关闭按钮交互
 *          尚未实现（tabsClosable 仅存状态），故此句柄为预留：一旦
 *          页签条侧接通发射，容器即同步转发，无需改动本文件以外的
 *          连接关系。
 * @param self 目标选项卡容器指针；可为 NULL。
 * @return 不透明的 tabCloseRequested 信号标识；返回值不指向可释放
 *         对象，也不得解引用。
 */
void* XTabWidget_tabCloseRequested_signal(XTabWidget* self);

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
