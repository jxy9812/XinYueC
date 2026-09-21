/******************************************************************************
 * @file       XMainWindow.h
 * @brief      XMainWindow 主窗口控件（对标 Qt 6.8 QMainWindow 核心
 *             公共 API）。
 * @details    功能范围：
 *             - menuBar()/setMenuBar()、menuWidget()/setMenuWidget()
 *               （惰性创建内置 XMenuBar / 接管任意位置控件）；
 *             - createPopupMenu()（新建停靠/工具栏右键菜单，归调用方）；
 *             - statusBar()/setStatusBar()（惰性创建内置 XStatusBar）；
 *             - addToolBar(area, toolbar)/addToolBar_2(title)（创建）、
 *               insertToolBar/removeToolBar、
 *               insertToolBarBreak/removeToolBarBreak/toolBarBreak、
 *               iconSize/setIconSize、setToolButtonStyle/toolButtonStyle；
 *             - setCentralWidget/centralWidget/takeCentralWidget；
 *             - addDockWidget(area, dock)/removeDockWidget、
 *               splitDockWidget/tabifyDockWidget/tabifiedDockWidgets/
 *               restoreDockWidget/resizeDocks、isSeparator；
 *             - setDockOptions/dockOptions（存储）；
 *             - 布局：菜单栏(顶) → 工具栏区 → 中央控件 → 停靠区 →
 *               状态栏(底)，resizeEvent 触发重排。
 * @note       模块总开关 XMAINWINDOW_ON 定义于 XGuiConfig.h。
 * @note       停靠体系为简化模型：四个停靠区（左/右两条停靠列 +
 *             Top/Bottom 两条停靠行）参与几何布局——列内可见面板按登记
 *             顺序行堆叠（行高可经 resizeDocks 覆盖，列宽默认 160 像素），
 *             行内可见面板按登记顺序横排（宽度可经 resizeDocks 覆盖，
 *             行高默认 100 像素）；标签组只记录分组与活动面板，不绘制
 *             真实标签条。浮动（setFloating(true)）面板脱离主窗布局，
 *             成独立顶层窗口。相关接口的 @note 逐条注明与 Qt 6.8.3 的差异。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMAINWINDOW_H
#define XMAINWINDOW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XDOCKWIDGET_ON
#include "XDockWidget.h"
#include "XToolBar.h"
#endif
#if XMENU_ON
#include "XMenu.h"
#endif

#if XWIDGET_ON && XMAINWINDOW_ON

/** @brief 主窗口停靠选项（对标 QMainWindow::DockOption，数值一致）。 */
typedef enum XMainWindowDockOption
{
    XMainWindowDockOption_AnimatedDocks = 0x01,
    XMainWindowDockOption_AllowNestedDocks = 0x02,
    XMainWindowDockOption_AllowTabbedDocks = 0x04,
    XMainWindowDockOption_ForceTabbedDocks = 0x08,
    XMainWindowDockOption_VerticalTabs = 0x10,
    XMainWindowDockOption_GroupedDragging = 0x20
} XMainWindowDockOption;

XCLASS_DEFINE_BEGING(XMainWindow)
XCLASS_DEFINE_EXTEND_END(XMainWindow, XWidget)

typedef struct XMainWindow
{
    XWidget m_base;              /**< 基类成员；必须是第一个。 */
    XWidget* m_menuBar;          /**< 菜单栏（XMenuBar*，拥有或外部）。 */
    bool m_menuBarOwned;         /**< 菜单栏是否内部创建。 */
    XWidget* m_statusBar;        /**< 状态栏（XStatusBar*，拥有或外部）。 */
    bool m_statusBarOwned;       /**< 状态栏是否内部创建。 */
    XWidget* m_central;          /**< 中央控件（借用）。 */
    XVector* m_toolBars;         /**< 工具栏数组（XToolBar*，借用）。 */
    XVector* m_toolBarAreas;     /**< 工具栏停靠区域（int）。 */
    XVector* m_docks;            /**< 停靠面板数组（XDockWidget*，借用）。 */
    XVector* m_dockAreas;        /**< 停靠面板区域（int）。 */
    XVector* m_dockHeights;      /**< 停靠面板跨向尺寸覆盖（int，与
                                  *   m_docks 同长；语义随区域：左/右列
                                  *   面板 = 行高覆盖，Top/Bottom 行面板
                                  *   = 宽度覆盖；0 = 自动均分；对标
                                  *   resizeDocks 的结果存储）。 */
    XVector* m_dockTabGroups;    /**< 停靠面板标签组（元素为 XVector*，
                                  *   组内为 XDockWidget* 借用指针；仅记录
                                  *   成组关系，不绘制标签条）。 */
    int m_leftDockWidth;         /**< 左侧停靠列宽度（像素；默认 160，
                                  *   可经 resizeDocks 横向调整）。 */
    int m_rightDockWidth;        /**< 右侧停靠列宽度（像素；默认 160，
                                  *   可经 resizeDocks 横向调整）。 */
    int m_topDockHeight;         /**< 顶部停靠行高度（像素；默认 100，
                                  *   可经 resizeDocks 纵向调整）。 */
    int m_bottomDockHeight;      /**< 底部停靠行高度（像素；默认 100，
                                  *   可经 resizeDocks 纵向调整）。 */
    int m_dockOptions;           /**< 停靠选项。 */
    int m_iconSize;              /**< 工具栏图标尺寸。 */
    int m_toolButtonStyle;       /**< 全局工具按钮样式（XToolButtonStyle 取值）。 */
    XWidget* m_activeTabifiedDock; /**< 最近激活的标签化停靠面板（借用）。 */
    bool m_documentMode;       /**< 文档模式（对标 documentMode）。 */
    bool m_animated;           /**< 动画（对标 animated）。 */
    bool m_dockNestingEnabled; /**< 停靠嵌套（对标 dockNestingEnabled）。 */
    bool m_unifiedTitleAndToolBarOnMac; /**< 统一标题栏（属性存储）。 */
    int m_tabPosition;         /**< 页签位置（对标 tabPosition）。 */
    int m_tabShape;            /**< 页签形状（对标 tabShape）。 */
    bool m_separator;          /**< 工具栏分隔线（对标 setSeparator）。 */
    int m_corner;              /**< 角控件位置位集（对标 corner）。 */
} XMainWindow;

/** @brief XMainWindowclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XMainWindow_class_init(void);
void XMainWindow_init(XMainWindow* self, XWidget* parent,
                      XWidgetFlags flags);
#define XMainWindow_create(parent, flags) XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMainWindow* XMainWindow_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XMainWindow_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMainWindow_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 菜单栏与状态栏 ==================== */

/** @brief 返回内置菜单栏（不存在则惰性创建；对标 menuBar()）。 */
XWidget* XMainWindow_menuBar(XMainWindow* self);
/** @brief 设置外部菜单栏（对标 setMenuBar）。 */
void XMainWindow_setMenuBar(XMainWindow* self, XWidget* menuBar);
/** @brief 查询菜单栏位置控件（对标 QMainWindow::menuWidget）。
 * @details 与 menuBar() 不同：本函数不惰性创建，直接返回 setMenuWidget/
 *          setMenuBar 放置在该位置的控件（可能是非菜单栏控件）。
 * @param self 目标主窗口；可为 NULL。
 * @return 菜单栏位置控件借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XWidget* XMainWindow_menuWidget(const XMainWindow* self);
/** @brief 设置菜单栏位置控件（对标 QMainWindow::setMenuWidget）。
 * @details Qt 中 setMenuBar 即转发 setMenuWidget，两者完全等价；本函数与
 *          XMainWindow_setMenuBar 行为一致：内部惰性创建的旧菜单栏被
 *          删除，外部传入的控件只借用（不归 XMainWindow 所有，不得由
 *          XMainWindow 删除），并重设父对象、显示与重排。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param menuWidget 菜单栏位置控件借用指针；可为 NULL 表示清空该位置。
 * @return 无返回值。
 */
void XMainWindow_setMenuWidget(XMainWindow* self, XWidget* menuWidget);
#if XMENU_ON
/** @brief 新建停靠面板/工具栏右键弹出菜单（对标 createPopupMenu）。
 * @details 新建空 XMenu 并把父对象设为本主窗口（未释放时随主窗口析构
 *          级联销毁）。
 * @param self 目标主窗口；可为 NULL。
 * @return 新建的 XMenu 指针，所有权归调用方，必须用 XMenu_delete_base
 *         释放；self 为 NULL 或分配失败时返回 NULL。
 * @note 与 Qt 差异：Qt 会向菜单填充各停靠面板/工具栏的 toggleViewAction，
 *       并在无可填充项时返回 nullptr；XMenu 没有“加入已有 XAction”的
 *       接口（XMenu_addAction 只按文本新建动作），故本实现返回空菜单且
 *       除分配失败外不返回 NULL，填充交给调用方。
 */
XMenu* XMainWindow_createPopupMenu(XMainWindow* self);
#endif /* XMENU_ON */
/** @brief 返回内置状态栏（不存在则惰性创建；对标 statusBar()）。 */
XWidget* XMainWindow_statusBar(XMainWindow* self);
/** @brief 设置外部状态栏（对标 setStatusBar）。 */
void XMainWindow_setStatusBar(XMainWindow* self, XWidget* statusBar);

/* ==================== 中央控件与工具栏 ==================== */

void XMainWindow_setCentralWidget(XMainWindow* self, XWidget* widget);
/** @brief XMainWindowcentral控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XMainWindow_centralWidget(const XMainWindow* self);
/** @brief XMainWindowtakeCentral控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XMainWindow_takeCentralWidget(XMainWindow* self);
/** @brief XMainWindowadd工具条（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param area 区域枚举。
 * @param toolbar 工具栏指针。
 * @return 无返回值。
 */
void XMainWindow_addToolBar(XMainWindow* self, int area, XWidget* toolbar);
/** @brief XMainWindowadd工具条2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param utf8Title const char 参数。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XMainWindow_addToolBar_2(XMainWindow* self, const char* utf8Title);
/** @brief 查询全局工具栏图标尺寸（对标 QMainWindow::iconSize）。
 * @param self 目标主窗口；可为 NULL。
 * @return 图标方边像素值（默认 16）；self 为 NULL 时返回 16。
 * @note 与 Qt 差异：Qt 返回 QSize，XGui 用单 int 方边像素表示（与
 *       XToolBar_iconSize 一致）。
 */
int XMainWindow_iconSize(const XMainWindow* self);
/** @brief 设置全局工具栏图标尺寸（对标 QMainWindow::setIconSize）。
 * @details 尺寸变化时同步到所有已登记工具栏（对齐 Qt 中主窗口
 *          iconSizeChanged 连接各工具栏 _q_updateIconSize 的行为），并真
 *          发射 iconSizeChanged(size, size)。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param size 图标方边像素；必须大于 0，size <= 0 或与当前值相同时忽略。
 * @return 无返回值。
 * @note 与 Qt 差异：Qt 接收 QSize，非法尺寸回退到样式默认值并记录
 *       explicitIconSize；XGui 无样式表体系，size <= 0 直接忽略。
 */
void XMainWindow_setIconSize(XMainWindow* self, int size);
/** @brief 在 before 工具栏之前插入断行（对标 insertToolBarBreak）。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param before 基准工具栏借用指针；未登记时无动作（对齐 Qt 找不到基准
 *               即返回）；该工具栏前已有断行时无动作。
 * @return 无返回值。
 * @note XGui 用 m_toolBars/m_toolBarAreas 中的哨兵条目（NULL 工具栏 +
 *       区域 0，由 addToolBarBreak 写入）表示断行，不额外维护平行标志
 *       数组，避免与既有 addToolBarBreak/insertToolBar/removeToolBar 的
 *       数组同步逻辑形成双份事实来源。
 */
void XMainWindow_insertToolBarBreak(XMainWindow* self, XWidget* before);
/** @brief 移除 before 工具栏之前的断行（对标 removeToolBarBreak）。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param before 基准工具栏借用指针；未登记或其前一项不是断行时无动作。
 * @return 无返回值。
 */
void XMainWindow_removeToolBarBreak(XMainWindow* self, XWidget* before);
/** @brief 查询工具栏之前是否有断行（对标 QMainWindow::toolBarBreak）。
 * @param self 目标主窗口；可为 NULL。
 * @param toolbar 工具栏借用指针；可为 NULL。
 * @return 该工具栏登记项之前存在断行时返回 true；未登记、位于首个断行
 *         段（对齐 Qt 的 j > 0 判定）或参数无效时返回 false。
 * @note 与 Qt 差异：简化布局把每个顶部工具栏各放一行，断行只影响登记
 *       顺序与 toolBarBreak 查询结果，不改变几何。
 */
bool XMainWindow_toolBarBreak(const XMainWindow* self, const XWidget* toolbar);
/** @brief 设置全局工具按钮样式（对标 setToolButtonStyle）。
 * @details 与当前值相同时不发射信号；变化时真发射
 *          toolButtonStyleChanged(toolButtonStyle)。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param toolButtonStyle XToolButtonStyle 取值。
 * @return 无返回值。
 */
void XMainWindow_setToolButtonStyle(XMainWindow* self, int toolButtonStyle);
/** @brief 查询全局工具按钮样式（对标 toolButtonStyle）。
 * @param self 目标主窗口；可为 NULL。
 * @return XToolButtonStyle 取值（默认 IconOnly）；self 为 NULL 时返回
 *         XToolButtonStyle_IconOnly。
 */
int XMainWindow_toolButtonStyle(const XMainWindow* self);

/* ==================== 停靠面板 ==================== */

/** @brief 把 dock 停靠到主窗口的指定区域（对标 QMainWindow::
 *         addDockWidget）。
 * @details 四区几何（对标 Qt）：Left/Right 为纵向停靠列（列内面板按
 *          登记顺序行堆叠，列宽默认 160），Top/Bottom 为横向停靠行
 *          （行内面板按登记顺序横排，行高默认 100，位于工具栏之下/
 *          状态栏之上）；登记后面板显示并立即重排。重复登记同一面板时
 *          按 Qt 语义视为移动：仅更新区域，不产生重复条目。面板当前
 *          处于浮动状态时先回归停靠（对标 Qt addDockWidget 会把浮动
 *          面板重新停靠）。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param area 停靠区域码（XDockWidgetArea 单个位）。
 * @param dock 停靠面板指针（XDockWidget*，借用）。
 * @return 无返回值。
 */
void XMainWindow_addDockWidget(XMainWindow* self, int area,
                               XWidget* dock);
/** @brief XMainWindowremove停靠控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param dock 停靠窗指针。
 * @return 无返回值。
 */
void XMainWindow_removeDockWidget(XMainWindow* self, XWidget* dock);
/** @brief XMainWindowset停靠Options（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param options int 参数。
 * @return 无返回值。
 */
void XMainWindow_setDockOptions(XMainWindow* self, int options);
/** @brief XMainWindowdockOptions（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMainWindow_dockOptions(const XMainWindow* self);
/** @brief 把 dock 停靠到 after 的相邻位置（对标 splitDockWidget）。
 * @details 简化实现为“登记区域/顺序调整 + 显示”：after 已在标签组中时
 *          按 Qt 语义把 dock 作为新标签加入（等价 tabifyDockWidget）；
 *          否则 dock 登记在 after 之后，并按 orientation 调整区域
 *          （Horizontal：落到相邻的左/右列；Vertical：留在 after 所在
 *          列，由登记顺序表达上下相邻）。dock 未登记时先按 after 的区域
 *          登记（对齐 Qt 会把它接入主窗口）。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param after 基准停靠面板借用指针；未登记或为 NULL 时无动作（对齐 Qt
 *              找不到基准即不动作）。
 * @param dock 待切分的停靠面板借用指针；为 NULL 或与 after 相同时无动作。
 * @param orientation 切分方向（1 = Horizontal 水平，2 = Vertical 垂直，
 *                    数值对齐 Qt::Orientation）。
 * @return 无返回值。
 * @note 与 Qt 差异：XGui 停靠体系没有嵌套布局与分隔条，无法把区域真正
 *       二分，本接口只调整登记区域与顺序并显示面板，几何由简化布局
 *       （左/右列行堆叠）决定；Top/Bottom 区域不参与几何布局。
 */
void XMainWindow_splitDockWidget(XMainWindow* self, XDockWidget* after,
                                 XDockWidget* dock, int orientation);
/** @brief 把 second 与 first 放进同一标签组（对标 tabifyDockWidget）。
 * @details first 未登记时无动作（对齐 Qt）；second 未登记时先按 first 的
 *          区域登记；随后 second 加入 first 所在标签组并被激活（同组其它
 *          面板隐藏，对齐 Qt 只有当前标签可见），同时真发射
 *          tabifiedDockWidgetActivated(second)，并把 second 记为
 *          m_activeTabifiedDock。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param first 基准停靠面板借用指针；未登记或为 NULL 时无动作。
 * @param second 待标签化的停靠面板借用指针；为 NULL 或与 first 相同时
 *               无动作。
 * @return 无返回值。
 * @note 与 Qt 差异：Qt 在真实标签条的 currentChanged 时发射
 *       tabifiedDockWidgetActivated；XGui 无标签条，改为在
 *       tabifyDockWidget 激活时发射。
 */
void XMainWindow_tabifyDockWidget(XMainWindow* self, XDockWidget* first,
                                  XDockWidget* second);
/** @brief 返回与 dock 同标签组的停靠面板（对标 tabifiedDockWidgets）。
 * @param self 目标主窗口；可为 NULL。
 * @param dock 停靠面板借用指针；可为 NULL。
 * @return 内部标签组数组借用指针（元素为 XDockWidget*）；dock 未登记、
 *         所在组不足两个成员或参数无效时返回 NULL。返回值随
 *         addDockWidget/removeDockWidget/tabifyDockWidget 失效，调用方
 *         不得释放或长期持有。
 * @note 与 Qt 差异：Qt 返回 QList<QDockWidget*> 值列表且不含 dock 自身；
 *       XGui 无 QList，返回内部借用 const XVector*（含 dock 自身，调用方
 *       按指针比较跳过即可），且仅在组内成员数 >= 2 时非 NULL（对齐 Qt
 *       “独占标签条不算成组”的判定）。
 */
const XVector* XMainWindow_tabifiedDockWidgets(const XMainWindow* self,
                                               const XDockWidget* dock);
/** @brief 恢复停靠面板（对标 restoreDockWidget）。
 * @details 面板已登记时取消浮动状态、恢复显示（若在标签组内则同时把它
 *          设为该组活动面板并隐藏同组其它面板）并重排。
 * @param self 目标主窗口；可为 NULL。
 * @param dock 停靠面板借用指针；可为 NULL。
 * @return 面板属于本主窗口（已登记）并完成恢复返回 true；self/dock 为
 *         NULL 或面板未登记返回 false。
 * @note 与 Qt 差异：Qt 从 saveState 快照恢复上次位置；XGui 的 saveState
 *       只记录工具栏区域（见 saveState 说明），故本接口按“恢复可见性/
 *       浮动状态”实现。
 */
bool XMainWindow_restoreDockWidget(XMainWindow* self, XDockWidget* dock);
/** @brief 按给定尺寸调整停靠面板（对标 resizeDocks）。
 * @details Horizontal：docks[i] 位于左/右列时把该列列宽设为 sizes[i]，
 *          位于 Top/Bottom 行时把该面板的行内宽度覆盖设为 sizes[i]；
 *          Vertical：docks[i] 位于 Top/Bottom 行时把该行行高设为
 *          sizes[i]，位于左/右列时把行高覆盖设为 sizes[i]，未指定覆盖
 *          的面板均分剩余空间。sizes[i] <= 0、面板为 NULL、未登记或处于
 *          浮动状态时跳过该项（对齐 Qt 的跳过语义）。调整后立即重排。
 * @param self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param docks 停靠面板指针数组（借用，长度为 count）；为 NULL 时不执行
 *              操作。
 * @param sizes 与 docks 一一对应的像素尺寸数组（借用，长度为 count）；
 *              为 NULL 时不执行操作。
 * @param count 数组元素个数；<= 0 时不执行操作。
 * @param orientation 调整方向（1 = Horizontal 调宽度，2 = Vertical 调
 *                    高度，数值对齐 Qt::Orientation）。
 * @return 无返回值。
 * @note 与 Qt 差异：Qt 签名为 QList<QDockWidget*> + QList<int> +
 *       Qt::Orientation，XGui 用裸数组 + count；Qt 尊重
 *       minimumSize/maximumSize 并按权重分配剩余空间，XGui 按“指定值
 *       优先、其余均分、总量超限时按比例压缩”处理。
 */
void XMainWindow_resizeDocks(XMainWindow* self, XDockWidget** docks,
                             const int* sizes, int count, int orientation);
/** @brief 查询坐标是否落在停靠区分隔条上（对标 isSeparator）。
 * @param self 目标主窗口；可为 NULL。
 * @param pos 主窗口坐标系中的坐标借用指针；可为 NULL。
 * @return 命中以下分隔带之一返回 true：左/右停靠列与中央区域之间的
 *         竖直分隔带（列边界 ±2 像素），或 Top/Bottom 停靠行与中央区域
 *         之间的水平分隔带（行边界 ±2 像素）；否则返回 false。
 * @note 与 Qt 差异：Qt 用 findSeparator 遍历真实分隔条控件；XGui 简化
 *       布局没有分隔条控件，本实现按区边界几何判定（列/行边界 ±2 像素）。
 */
bool XMainWindow_isSeparator(const XMainWindow* self, const XPoint* pos);

/* ==================== Task 2.4：QMainWindow 布局 API ==================== */

/** @brief 查询工具栏停靠区域（对标 QMainWindow::toolBarArea）。
 * @param self 目标主窗口。
 * @param toolbar 工具栏借用指针。
 * @return 区域码；未登记返回 0。
 */
int XMainWindow_toolBarArea(const XMainWindow* self, const XWidget* toolbar);
/** @brief 查询停靠面板区域（对标 QMainWindow::dockWidgetArea）。
 * @param self 目标主窗口。
 * @param dock 停靠面板借用指针。
 * @return 区域码；未登记返回 0。
 */
int XMainWindow_dockWidgetArea(const XMainWindow* self,
                               const XWidget* dock);
/** @brief 设置工具栏分隔线（对标 QMainWindow::setSeparator）。
 * @param self 目标主窗口。
 * @param sep true 显示分隔线。
 * @return 无返回值。
 */
void XMainWindow_setSeparator(XMainWindow* self, bool sep);
/** @brief 设置文档模式（对标 setDocumentMode）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setDocumentMode(XMainWindow* self, bool enable);
/** @brief 查询文档模式。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_documentMode(const XMainWindow* self);
/** @brief 设置停靠动画（对标 setAnimated）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setAnimated(XMainWindow* self, bool enable);
/** @brief 查询停靠动画。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_isAnimated(const XMainWindow* self);
/** @brief 设置停靠嵌套（对标 setDockNestingEnabled）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setDockNestingEnabled(XMainWindow* self, bool enable);
/** @brief 查询停靠嵌套。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_isDockNestingEnabled(const XMainWindow* self);
/** @brief 设置统一标题栏（属性存储；对标 setUnifiedTitleAndToolBarOnMac）。
 * @param self 目标主窗口。
 * @param enable true 开启。
 * @return 无返回值。
 */
void XMainWindow_setUnifiedTitleAndToolBarOnMac(XMainWindow* self,
                                                bool enable);
/** @brief 查询统一标题栏。 @param self 目标主窗口。 @return 开启返回 true。 */
bool XMainWindow_isUnifiedTitleAndToolBarOnMac(const XMainWindow* self);
/** @brief 查询统一标题栏存储位（对标 QMainWindow::unifiedTitleAndToolBarOnMac）。
 * @param self 目标主窗口；可为 NULL。
 * @return 存储位为真返回 true；self 为 NULL 时返回 false。
 * @note 与既有 XMainWindow_isUnifiedTitleAndToolBarOnMac 完全等价，故以
 *       别名宏提供 Qt 原名。Qt 在非 macOS 平台恒返回 false（编译期
 *       裁剪），XGui 面向 Linux/X11、不伪造平台句柄（见 XGui.md 第 11
 *       节），只维护该属性存储位，与 setUnifiedTitleAndToolBarOnMac 的
 *       既有行为保持一致。
 */
#define XMainWindow_unifiedTitleAndToolBarOnMac(self) XMainWindow_isUnifiedTitleAndToolBarOnMac((self))
/** @brief 设置页签位置（对标 setTabPosition）。
 * @param self 目标主窗口。
 * @param position 位置码。
 * @return 无返回值。
 */
void XMainWindow_setTabPosition(XMainWindow* self, int position);
/** @brief 查询页签位置。 @param self 目标主窗口。 @return 位置码。 */
int XMainWindow_tabPosition(const XMainWindow* self);
/** @brief 设置页签形状（对标 setTabShape）。
 * @param self 目标主窗口。
 * @param shape 形状码。
 * @return 无返回值。
 */
void XMainWindow_setTabShape(XMainWindow* self, int shape);
/** @brief 查询页签形状。 @param self 目标主窗口。 @return 形状码。 */
int XMainWindow_tabShape(const XMainWindow* self);
/** @brief 设置角控件位置（对标 setCorner）。
 * @param self 目标主窗口。
 * @param corner 角位码（Qt::Corner）。
 * @param area 停靠区域码。
 * @return 无返回值。
 */
void XMainWindow_setCorner(XMainWindow* self, int corner, int area);
/** @brief 查询角控件位置。 @param self 目标主窗口。 @param corner 角位码。 @return 区域码。 */
int XMainWindow_corner(const XMainWindow* self, int corner);
/** @brief 工具栏中断（对标 addToolBarBreak）。
 * @param self 目标主窗口。
 * @return 无返回值。
 */
void XMainWindow_addToolBarBreak(XMainWindow* self);
/** @brief 插入工具栏（对标 insertToolBar）。
 * @param self 目标主窗口。
 * @param before 插入基准工具栏借用指针。
 * @param toolbar 待插入工具栏借用指针。
 * @return 无返回值。
 */
void XMainWindow_insertToolBar(XMainWindow* self, XWidget* before,
                               XWidget* toolbar);
/** @brief 移除工具栏（对标 removeToolBar；不删除对象）。
 * @param self 目标主窗口。
 * @param toolbar 工具栏借用指针。
 * @return 无返回值。
 */
void XMainWindow_removeToolBar(XMainWindow* self, XWidget* toolbar);
/** @brief 保存窗口布局状态（对标 QMainWindow::saveState；返回新建
 *         XString* 布局快照，调用方负责 delete_base）。
 * @details 快照格式 "XMWSTATE:2;"：工具栏登记项 "t<区域>;"（含断行
 *          哨兵）、停靠几何 "g<左列宽>,<右列宽>,<顶行高>,<底行高>;"、
 *          停靠面板项 "d<区域>:<可见>:<浮动>:<跨向覆盖>;"（按登记顺序）、
 *          标签组项 "p<成员数>:<下标0>,<下标1>,...;"（v2 内追加段，下标
 *          为停靠面板登记顺序；对标 Qt saveState 持久化 tabified
 *          groups），覆盖 dock 布局（区域、显隐、浮动状态、尺寸覆盖、
 *          tabify 编组）。组内活动标签由 d 条目的显隐位表达（活动标签
 *          可见、其余隐藏）。
 * @param self 目标主窗口。
 * @return 新建 XString*；失败返回 NULL。
 */
XString* XMainWindow_saveState(const XMainWindow* self);
/** @brief 恢复窗口布局状态（对标 QMainWindow::restoreState）。
 * @details 解析 saveState 快照并按登记顺序回放：工具栏区域、停靠面板
 *          区域/显隐/浮动状态/跨向覆盖、四区几何尺寸、标签组编组
 *          （p 条目按下标重建 tabify 组；旧快照缺 p 段时恢复为无组，
 *          格式向后兼容），随后还原各组活动标签（首个未隐藏成员）并
 *          重排。恢复前先解散既有编组（对标 Qt restoreState 重建整个
 *          布局）。浮动面板经 XDockWidget_setFloating 恢复为独立顶层
 *          窗口。快照损坏或格式不识别返回 false（对齐 Qt）；state 为
 *          NULL 视为重置，当前无可重置字段，直接返回 true。
 * @param self 目标主窗口。
 * @param state 借用 XString* 快照；可为 NULL（重置）。
 * @return 恢复成功返回 true；快照无效返回 false。
 */
bool XMainWindow_restoreState(XMainWindow* self, const XString* state);

/* ==================== 信号 ==================== */

/** @brief iconSizeChanged(int,int) 信号（对标 QMainWindow::iconSizeChanged）。
 * @details setIconSize 改变尺寸时真发射；载荷为宽、高（XGui 为方边值，
 *          两者相同）。self 为 NULL 时只返回信号标识，不发射。
 * @param self 目标主窗口；可为 NULL。
 * @param width 新图标宽度（像素）。
 * @param height 新图标高度（像素）。
 * @return 不透明的信号标识（非 NULL）；返回值不指向可释放对象，也不得
 *         解引用；供 XObject_connect 使用。
 */
void* XMainWindow_iconSizeChanged_signal(XMainWindow* self, int width,
                                         int height);
/** @brief toolButtonStyleChanged(int) 信号（对标 QMainWindow::toolButtonStyleChanged）。
 * @details setToolButtonStyle 改变样式时真发射；self 为 NULL 时只返回
 *          信号标识，不发射。
 * @param self 目标主窗口；可为 NULL。
 * @param toolButtonStyle 新的 XToolButtonStyle 取值。
 * @return 不透明的信号标识（非 NULL）；供 XObject_connect 使用。
 */
void* XMainWindow_toolButtonStyleChanged_signal(XMainWindow* self,
                                                int toolButtonStyle);
/** @brief tabifiedDockWidgetActivated(XDockWidget*) 信号（对标 QMainWindow::tabifiedDockWidgetActivated）。
 * @details tabifyDockWidget/restoreDockWidget 激活标签面板时真发射，并把
 *          该面板记为 m_activeTabifiedDock；self 为 NULL 时只返回信号
 *          标识，不发射。
 * @param self 目标主窗口；可为 NULL。
 * @param dockWidget 被激活的停靠面板借用指针（以 XWidget* 载荷传递）。
 * @return 不透明的信号标识（非 NULL）；供 XObject_connect 使用。
 */
void* XMainWindow_tabifiedDockWidgetActivated_signal(XMainWindow* self,
                                                     XWidget* dockWidget);

#endif /* XWIDGET_ON && XMAINWINDOW_ON */

#endif /* XMAINWINDOW_H */