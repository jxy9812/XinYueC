/**
 * @file       XTreeWidget.h
 * @brief      XTreeWidget 树控件（对标 Qt 6.8 QTreeWidget）。
 * @details    以 m_base 组合继承 XTreeView；持有树条目
 *             （XTreeWidgetItem：文本 + 子节点数组），自绘递归渲染
 *             （缩进 = 深度 × indentation）；提供 addTopLevelItem/
 *             insertTopLevelItem/topLevelItem/takeTopLevelItem/clear；
 *             便捷族覆盖平铺行模型子集（当前项/部件挂载/批量行/
 *             可视矩形/滚动/选中行数组/查找/排序/位置反查/表头
 *             文本批量），信号族提供行号载荷的
 *             点击/双击/按下/激活/进入/变更/展开/折叠/当前项/
 *             选择变化十项；便捷族剩余覆盖 expandItem/collapseItem
 *             （对接展开状态承载）、indexOfTopLevelItem/itemAbove/
 *             itemBelow（平铺行反查）、setColumnCount（列数状态
 *             承载）与 setHeaderLabel（单标签转发批量族）。
 * @note       模块总开关 XTABLEWIDGET_ON；XTreeWidget→XTreeView。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTREEWIDGET_H
#define XTREEWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XTreeView.h"
#include "XString.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 树条目 ==================== */

/** @brief 树条目（对标 QTreeWidgetItem；文本 + 子节点，对象拥有）。 */
typedef struct XTreeWidgetItem
{
    XString* text;                /**< 显示文本（对象拥有）。 */
    struct XTreeWidgetItem** children; /**< 子节点数组（对象拥有）。 */
    int childCount;               /**< 子节点数。 */
    int childCapacity;            /**< 子节点容量。 */
    struct XTreeWidgetItem* parent; /**< 父节点（借用；顶层为 NULL）。 */
    struct XTreeWidget* owner;    /**< 所属树控件（借用；未被树持有为
                                       NULL）。顶层挂载时写入、takeTopLevelItem
                                       时清空；仅用于 itemChanged(row) 的
                                       发射定位（子条目不挂 owner，平铺行
                                       模型无子行号）。 */
} XTreeWidgetItem;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTreeWidget)
XCLASS_DEFINE_EXTEND_END(XTreeWidget, XTreeView)

/** @brief 树控件对象；m_base 必须是第一个成员（嵌 XTreeView）。 */
typedef struct XTreeWidget
{
    XTreeView m_base;             /**< 基类成员；必须是第一个。 */
    XTreeWidgetItem** m_topItems; /**< 顶层条目数组（对象拥有）。 */
    int m_topCount;               /**< 顶层条目数。 */
    int m_topCapacity;            /**< 顶层条目容量。 */
    XWidget*** m_cellWidgets;     /**< 单元格部件表：行指针数组 → 每行
                                       列指针表（下标 [row][column]）；
                                       表本身对象拥有，部件为借用
                                       （生命周期由调用方管理）。 */
    int m_cellRowCapacity;        /**< 部件表行容量（与 m_topCapacity
                                       经 xtw_ensureTop 同步扩容）。 */
    int m_cellColCapacity;        /**< 部件表每行列容量（列越界时按
                                       xtw_ensureCols 模式倍增扩容）。 */
    int m_sortColumn;             /**< 最近排序列（便捷族 sortItems 承载；
                                       -1=未排序）。 */
    int m_sortOrder;              /**< 最近排序序：0=升序，1=降序。 */
    int m_enteredRow;             /**< 上次发射 itemEntered 的顶层行号；
                                       -2=尚未进入任何行（同 XListWidget
                                       差分口径）。 */
    bool* m_topExpanded;          /**< 顶层行展开状态表（平行数组，下标与
                                       m_topItems 对齐；对象拥有）。默认
                                       展开（与历史"子树恒绘制"行为一致）；
                                       指示器点击/双击切换时发射
                                       itemExpanded/itemCollapsed。 */
    int m_topExpCapacity;         /**< 展开状态表容量（与 m_topCapacity
                                       经 xtw_ensureTop 同步扩容）。 */
    XString** m_headerLabels;     /**< 表头文本表（对象拥有；便捷族
                                       setHeaderLabels 承载，下标=
                                       列号；倍增扩容、新增区清零）。 */
    int m_headerCount;            /**< 已设置表头文本数（历史最大写入
                                       个数；查询承载用）。 */
    int m_headerCapacity;         /**< 表头文本表容量。 */
    int m_columnCount;            /**< 列数承载（setColumnCount 承载；
                                       默认 1；>1 时单元格部件列容量经
                                       xtw_ensureCellCols 同步扩容；
                                       自绘仍为单文本列渲染）。 */
    XTreeWidgetItem* m_invisibleRoot; /**< 不可见根条目（对标
                                       invisibleRootItem；对象拥有；
                                       init 创建）。其 children 借用
                                       m_topItems 顶层存储（唯一例外于
                                       "children 对象拥有"约定），经
                                       xtw_syncRoot/addChild 钩子双向
                                       同步；对它的增删即顶层增删。 */
    XTreeWidgetItem* m_headerItem;    /**< 表头条目（对标 headerItem；
                                       对象拥有；init 懒创建失败为
                                       NULL）。children 文本镜像
                                       m_headerLabels（绘制路径仍读
                                       标签表）；setHeaderItem 移交
                                       所有权并回填标签。 */
} XTreeWidget;

/* ==================== 生命周期 ==================== */

XVtable* XTreeWidget_class_init(void);
/** @brief 初始化树控件。
 * @param self 目标控件；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XTreeWidget_init(XTreeWidget* self, XWidget* parent,
                      XWidgetFlags flags);
/** @brief 使用指定内存类型创建树控件。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XTreeWidget* XTreeWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XTreeWidget_create(parent, flags) \
    XTreeWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XTreeWidget_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XTreeWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 条目（对标 QTreeWidget） ==================== */

/** @brief 创建树条目（文本 + 父节点；对标 new QTreeWidgetItem）。
 * @param text 借用 XString*；可为 NULL（空文本）。
 * @param parent 父条目借用指针；NULL=顶层（由 addTopLevelItem 挂接）。
 * @return 新建条目（堆；由树或调用方 delete）。
 */
XTreeWidgetItem* XTreeWidgetItem_create(const XString* text,
                                        XTreeWidgetItem* parent);
/** @brief 创建树条目（UTF-8 兼容重载）。 */
XTreeWidgetItem* XTreeWidgetItem_create_2(const char* text,
                                          XTreeWidgetItem* parent);
/** @brief 释放树条目及其子树。
 * @param item 目标条目；可为 NULL。
 * @return 无返回值。
 */
void XTreeWidgetItem_delete(XTreeWidgetItem* item);
/** @brief 读取条目文本（内部借用 XString*；不得释放）。 */
const XString* XTreeWidgetItem_text(const XTreeWidgetItem* item);
/** @brief 读取条目文本（UTF-8 借用）。 */
const char* XTreeWidgetItem_text_2(const XTreeWidgetItem* item);
/** @brief 设置条目文本（XString 主版本）。
 * @param item 目标条目。
 * @param text 借用 XString*；可为 NULL（清空）。
 * @return 无返回值。
 */
void XTreeWidgetItem_setText(XTreeWidgetItem* item, const XString* text);
/** @brief 设置条目文本（UTF-8 兼容重载，转发主版本）。 */
void XTreeWidgetItem_setText_2(XTreeWidgetItem* item, const char* text);
/** @brief 追加子条目。
 * @param item 目标条目。
 * @param child 子条目（所有权转移给 item）。
 * @return 成功返回 true。
 */
bool XTreeWidgetItem_addChild(XTreeWidgetItem* item,
                              XTreeWidgetItem* child);
/** @brief 查询子条目数。 @param item 目标条目。 @return 子条目数。 */
int XTreeWidgetItem_childCount(const XTreeWidgetItem* item);
/** @brief 读取子条目。 @param item 目标条目。 @param index 序号。 @return 借用指针。 */
XTreeWidgetItem* XTreeWidgetItem_child(const XTreeWidgetItem* item,
                                       int index);

/** @brief 追加顶层条目。
 * @param self 目标控件。
 * @param item 条目（所有权转移给树）。
 * @return 成功返回 true。
 */
bool XTreeWidget_addTopLevelItem(XTreeWidget* self, XTreeWidgetItem* item);
/** @brief 插入顶层条目。
 * @param self 目标控件。
 * @param index 插入位置（0 起）。
 * @param item 条目（所有权转移给树）。
 * @return 成功返回 true。
 */
bool XTreeWidget_insertTopLevelItem(XTreeWidget* self, int index,
                                    XTreeWidgetItem* item);
/** @brief 读取顶层条目。 @param self 目标控件。 @param index 序号。 @return 借用指针。 */
XTreeWidgetItem* XTreeWidget_topLevelItem(const XTreeWidget* self, int index);
/** @brief 查询顶层条目数。 @param self 目标控件。 @return 顶层条目数。 */
int XTreeWidget_topLevelItemCount(const XTreeWidget* self);
/** @brief 不可见根条目（对标 invisibleRootItem）。
 * @param self 目标控件。
 * @return 哨兵根条目借用指针；self 为空返回 NULL。
 * @note Qt 语义：根条目承载全部顶层条目，child(i) 即 topLevelItem(i)，
 *       对其 addChild 的条目成为顶层条目（所有权转入控件）。本库实现
 *       为 children 借用顶层存储的哨兵（构造时创建），经双向同步保证
 *       与 topLevelItem 族一致；对哨兵调用 XTreeWidgetItem_delete 由
 *       控件析构负责，调用方不得删除。 */
XTreeWidgetItem* XTreeWidget_invisibleRootItem(const XTreeWidget* self);
/** @brief 索引 → 条目（对标 itemFromIndex；索引=indexFromItem 约定的
 *        顶层行号，恒等简化）。
 * @param self 目标控件。
 * @param row 顶层行号。
 * @return 借用指针；越界或 self 为空返回 NULL。
 */
XTreeWidgetItem* XTreeWidget_itemFromIndex(const XTreeWidget* self, int row);
/** @brief 移除顶层条目（所有权归还调用方）。
 * @param self 目标控件。
 * @param index 序号。
 * @return 被移除条目；越界返回 NULL。
 */
XTreeWidgetItem* XTreeWidget_takeTopLevelItem(XTreeWidget* self, int index);
/** @brief 清空全部条目（同时释放单元格部件表、复位当前索引为 -1）。
 * @param self 目标控件。 */
void XTreeWidget_clear(XTreeWidget* self);

/* ==================== 便捷族第一波（平铺行模型，对标 QTreeWidget） ==================== */

/** @brief 查询当前条目行号（对标 currentItem；行模型下以行号承载）。
 * @param self 目标控件。
 * @return 当前行号（取基类当前索引；越界或无当前项返回 -1）。
 */
int XTreeWidget_currentItem(const XTreeWidget* self);
/** @brief 设置当前条目并滚动至可见（对标 setCurrentItem）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号；越界忽略）。
 * @return 无返回值。
 * @note 行号基于平铺顶层行（与 addTopLevelItems 家族同一行序）；
 *       当前索引固定写第 0 列，经基类 XAbstractItemView 承载，
 *       鼠标命中亦写同一状态。滚动按本控件行高/缩进几何计算
 *       EnsureVisible 语义（不依赖基类固定网格 visualRect）。
 */
void XTreeWidget_setCurrentItem(XTreeWidget* self, int row);
/** @brief 读取单元格挂载部件（对标 itemWidget）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @param column 列号。
 * @return 部件借用指针（所有权仍属调用方，树不删除）；
 *         越界或未挂载返回 NULL。
 */
XWidget* XTreeWidget_itemWidget(const XTreeWidget* self, int row,
                                int column);
/** @brief 挂载单元格部件（对标 setItemWidget；三维承载 [行][列]→部件）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号；越界忽略）。
 * @param column 列号（<0 忽略；超出列容量时按倍增模式扩容全部行表）。
 * @param widget 部件借用指针；树不取得所有权、不重设父控件、
 *               不负责删除（同 removeItemWidget 置 NULL 语义配套）。
 * @return 无返回值。
 * @note 部件表行容量与顶层条目数组同步扩容；尚未挂过部件的行懒分配
 *       列指针表。列数变化入口（未来 setColumnCount 族）应调用同一
 *       列扩容辅助以保持各行列容量一致。
 */
void XTreeWidget_setItemWidget(XTreeWidget* self, int row, int column,
                               XWidget* widget);
/** @brief 移除单元格挂载部件（对标 removeItemWidget；置 NULL）。
 * @param self 目标控件。
 * @param row 行号。
 * @param column 列号。
 * @return 无返回值。
 * @note 仅将承载指针置 NULL（部件为借用，不删除、不改父控件）；
 *       越界或该格无挂载为无操作。
 */
void XTreeWidget_removeItemWidget(XTreeWidget* self, int row, int column);
/** @brief 批量追加文本行（对标 addTopLevelItems 便捷形态）。
 * @param self 目标控件。
 * @param texts UTF-8 文本数组（借用；元素可为 NULL=空文本行）。
 * @param count 文本个数（<=0 忽略）。
 * @return 成功追加的行数；分配中途失败即停止（返回已追加数）。
 */
int XTreeWidget_addTopLevelItems(XTreeWidget* self,
                                 const char* const* texts, int count);
/** @brief 批量插入文本行（对标 insertTopLevelItems 便捷形态）。
 * @param self 目标控件。
 * @param index 插入位置（0 起；>行数或 <0 返回 0）。
 * @param texts UTF-8 文本数组（借用；元素可为 NULL=空文本行）。
 * @param count 文本个数（<=0 返回 0）。
 * @return 成功插入的行数；分配中途失败即停止（返回已插入数）。
 */
int XTreeWidget_insertTopLevelItems(XTreeWidget* self, int index,
                                    const char* const* texts, int count);
/** @brief 查询条目可视矩形（对标 visualItemRect；基于行高/缩进几何）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 视图坐标矩形；越界返回全零矩形。
 * @note 几何与自绘一致：y 按之前各行展开态子树行数累计 × 行高、
 *       高 = 行高（m_rowHeight，未配置回退 24）；顶层行深度 0 故
 *       x=0、宽=控件宽，缩进（indentation）预留给子行几何。表头
 *       不占行带。行折叠时折叠行的子树不参与累计。
 */
XRect XTreeWidget_visualItemRect(const XTreeWidget* self, int row);
/** @brief 查询当前排序列（对标 sortColumn）。
 * @param self 目标控件。
 * @return 最近一次 sortItems 写入的列号；未排序或 self 为 NULL 返回 -1。
 * @note 承载为本控件 m_sortColumn/m_sortOrder（便捷族 sortItems 落地时
 *       写入；XTreeView 基类无排序列字段，不对接表视图字段）。
 */
int XTreeWidget_sortColumn(const XTreeWidget* self);

/* ==================== 信号族（对标 QTreeWidget，行号载荷平铺子集） ==================== */

/** @brief itemClicked(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标左键按下命中条目行路径（先 itemPressed 后
 *       itemClicked，同 XTableWidget 约定）；点在展开指示器上时切换
 *       展开而不发射本信号。
 */
void* XTreeWidget_itemClicked_signal(XTreeWidget* self, int row);
/** @brief itemDoubleClicked(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标左键双击命中条目行路径（随后按
 *       expandsOnDoubleClick 切换展开、发射 itemActivated）。
 */
void* XTreeWidget_itemDoubleClicked_signal(XTreeWidget* self, int row);
/** @brief itemPressed(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标左键按下命中条目行路径（先于 itemClicked）。
 */
void* XTreeWidget_itemPressed_signal(XTreeWidget* self, int row);
/** @brief itemActivated(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：双击激活路径（对标 Qt 平台双击激活语义）。
 *       键盘激活（Enter/Return）路径未建立——本控件基线无键盘
 *       事件处理，句柄先行预留，待键盘导航接入后补发射点。
 */
void* XTreeWidget_itemActivated_signal(XTreeWidget* self, int row);
/** @brief itemEntered(row) 信号（鼠标进入新顶层行时发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：mouseMoveEvent 悬停进入新顶层行（m_enteredRow
 *       差分判重；命中几何与点击同走 xtw_rowAtY 展开态行带）。
 */
void* XTreeWidget_itemEntered_signal(XTreeWidget* self, int row);
/** @brief itemChanged(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：条目文本变化路径——顶层条目经
 *       XTreeWidgetItem_setText/_2 改文本后按其 owner 定位行号发射；
 *       子条目（平铺行模型无子行号）与未挂树的条目不发射。
 *       编辑器路径（editItem）建立后为第二发射点。
 */
void* XTreeWidget_itemChanged_signal(XTreeWidget* self, int row);
/** @brief itemExpanded(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：顶层行展开指示器点击/双击切换由折叠变展开路径
 *       （独立于基类 XTreeView 的 expanded(row)：基类展开族按模型行
 *       承载，本控件条目树未接模型，故在本控件内独立承载与发射）。
 *       无子条目的行不切换、不发射。
 */
void* XTreeWidget_itemExpanded_signal(XTreeWidget* self, int row);
/** @brief itemCollapsed(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：顶层行展开指示器点击/双击切换由展开变折叠路径
 *       （独立承载说明同 itemExpanded）；折叠后子树停止绘制。
 */
void* XTreeWidget_itemCollapsed_signal(XTreeWidget* self, int row);
/** @brief currentItemChanged(current, previous) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param current 当前行号（无当前项为 -1）。
 * @param previous 前当前行号（无前当前项为 -1）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：setCurrentItem 当前项变化路径、鼠标按下换当前项
 *       路径、clear 使当前项失效路径。
 */
void* XTreeWidget_currentItemChanged_signal(XTreeWidget* self, int current,
                                            int previous);
/** @brief itemSelectionChanged() 信号（调用即发射并返回地址；无载荷）。
 * @param self 目标控件。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：选择集合变化路径——setCurrentItem/鼠标按下的
 *       选择联动（行首次进入选择集合时发射）与 clear 的选择清空；
 *       NoSelection 模式不发射。
 */
void* XTreeWidget_itemSelectionChanged_signal(XTreeWidget* self);

/* ==================== 数据便捷族第二波（平铺行模型） ==================== */

/** @brief 查询列数（对标 columnCount）。
 * @param self 目标控件。
 * @return 列数（m_columnCount 承载；默认 1）。
 * @note 对接 setColumnCount 状态承载：默认 1（同 Qt 新建 QTreeWidget
 *       的默认列数，行为不变）；setColumnCount 可改写，自绘当前仍为
 *       平铺单文本列渲染（每行一次 drawText），多列渲染待列绘制路径
 *       建立后接入；单元格挂载部件（setItemWidget）的列容量是承载
 *       容量、非显示列数。
 */
int XTreeWidget_columnCount(const XTreeWidget* self);
/** @brief 触发条目编辑（对标 editItem；句柄预留）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号；越界忽略）。
 * @param column 列号（<0 或 >=columnCount 忽略）。
 * @return 无返回值。
 * @note 句柄预留：本控件尚无编辑器/委托（editor/delegate）机制，
 *       编辑触发无从落地，当前为校验入参后的无操作；编辑路径建立
 *       后应在此发射 itemChanged(row)。
 */
void XTreeWidget_editItem(XTreeWidget* self, int row, int column);
/** @brief 条目 → 行号（对标 indexFromItem；行号即索引，恒等简化）。
 * @param self 目标控件。
 * @param item 目标条目（借用）。
 * @return 平铺顶层行号；未挂树、为子条目或 self 为 NULL 返回 -1。
 * @note 平铺行模型下索引即行号（恒等映射，无独立 QModelIndex）；
 *       仅顶层条目参与映射。
 */
int XTreeWidget_indexFromItem(const XTreeWidget* self,
                              const XTreeWidgetItem* item);
/** @brief 滚动使指定条目可见（对标 scrollToItem）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号；越界忽略）。
 * @return 无返回值。
 * @note 与 setCurrentItem 同一滚动机制（EnsureVisible 语义）；行 y
 *       按展开态子树累计行数计算（同 visualItemRect 口径）。
 */
void XTreeWidget_scrollToItem(XTreeWidget* self, int row);
/** @brief 读取选中行号数组（对标 selectedItems）。
 * @param self 目标控件。
 * @param outRows 选中行号输出数组（调用方分配）；可为 NULL。
 * @param maxCount 输出数组容量；<=0 表示只统计不写出。
 * @return 选中行数（写出个数）；返回值可能大于 maxCount 表示截断。
 *         self 为 NULL 返回 0。
 * @note 选择集合由基类选择模型承载（行,0）；鼠标按下/setCurrentItem
 *       的 SelectCurrent 联动写入。
 */
int XTreeWidget_selectedItems(const XTreeWidget* self, int* outRows,
                              int maxCount);

/* ==================== 便捷族二（查找/排序/命中/表头，对标 QTreeWidget） ==================== */

/** @brief 按文本查找条目（对标 findItems；前序遍历全部行）。
 * @param self 目标控件。
 * @param text 匹配文本（UTF-8 借用；NULL 返回 0）。
 * @param flags 匹配方式：1=精确相等（MatchExactly），0=包含子串
 *              （MatchContains）；均区分大小写，其余值按 0 处理。
 * @param outRows 命中行号输出数组（调用方分配）；可为 NULL（只统计）。
 * @param maxCount 输出数组容量；<=0 表示只统计不写出。
 * @return 命中总数（遍历完整条目树后统计，可能大于 maxCount 表示
 *         输出被截断）；self 为 NULL 返回 0。
 * @note 前序遍历全部行：顶层行在前、子树随后逐层展开（整树参与
 *       匹配，对标 Qt findItems 经 QAbstractItemModel::match 的全树
 *       语义）。行号为全树前序序号：仅顶层条目构成的树与平铺顶层
 *       行号一致；子条目命中时平铺行模型无子行号，以本序号承载。
 */
int XTreeWidget_findItems(const XTreeWidget* self, const char* text,
                          int flags, int* outRows, int maxCount);
/** @brief 按列文本排序顶层条目（对标 sortItems；冒泡实现）。
 * @param self 目标控件。
 * @param column 排序列（本控件恒单文本列，仅 0 有效）。
 * @param order 排序序：0=升序（AscendingOrder），1=降序
 *              （DescendingOrder）；其余值忽略。
 * @return 无返回值。
 * @note 整行数据随动重排：条目指针（连同其子树）、单元格挂载部件
 *       行表、展开状态表三者整体换位（平行数组行下标对齐不破坏）；
 *       当前项/选择按行号承载不随动（与 XTableWidget_sortItems 同
 *       口径）。排序键为条目 UTF-8 文本 XStrcmp（空文本视为 ""），
 *       稳定冒泡、相等不换位。结果写入 m_sortColumn/m_sortOrder
 *       （sortColumn 读取）。
 */
void XTreeWidget_sortItems(XTreeWidget* self, int column, int order);
/** @brief 位置反查行号（对标 itemAt）。
 * @param self 目标控件。
 * @param x 控件内容坐标 x（同 mousePressEvent 命中坐标口径）。
 * @param y 控件内容坐标 y。
 * @return 平铺顶层行号；越界（x/y 为负、x 超控件宽、y 超出行带
 *         总高）返回 -1。
 * @note 几何与自绘/鼠标命中同一口径：行带 y 按之前各行展开态子树
 *       行数累计（折叠行的子树不占位）、高 = 行高，表头不占行带。
 *       基类 XTreeView 的 indexAt 按内部 m_model 固定网格反推，与
 *       本控件条目树几何不一致，故不走基类虚槽分派。
 */
int XTreeWidget_itemAt(const XTreeWidget* self, int x, int y);
/** @brief 批量设置表头文本（对标 setHeaderLabels）。
 * @param self 目标控件。
 * @param labels UTF-8 表头文本数组（借用；元素可为 NULL=空文本）。
 * @param count 文本个数（<=0 忽略）。
 * @return 无返回值。
 * @note 新增表头文本存储（本控件原无表头承载）：XString* 指针表
 *       倍增扩容、新增区清零防野指针；已设置条目就地覆写。存储/
 *       查询承载为主——自绘当前无表头带（表头不占行带），渲染待
 *       表头绘制路径建立后接入；clear 不清除表头文本（Qt 同语义：
 *       clear 只移除条目与选择）。
 */
void XTreeWidget_setHeaderLabels(XTreeWidget* self,
                                 const char* const* labels, int count);
/** @brief 设置单条表头文本（对标 setHeaderLabel；转发 setHeaderLabels）。
 * @param self 目标控件。
 * @param label 表头文本（UTF-8 借用；NULL=空文本）。
 * @return 无返回值。
 * @note 便捷转发：以单元素数组走 setHeaderLabels 同一承载（首次写入
 *       创建存储、重复写入就地覆写）；渲染待表头绘制路径建立后接入。
 */
void XTreeWidget_setHeaderLabel(XTreeWidget* self, const char* label);
/** @brief 读取指定列表头文本（对标 headerItem()->text(column) 便捷化）。
 * @param self 目标控件。
 * @param column 列号。
 * @return UTF-8 文本借用指针；未设置或越界返回空串。
 */
const char* XTreeWidget_headerLabel(const XTreeWidget* self, int column);
/** @brief 表头条目（对标 headerItem）。
 * @param self 目标控件。
 * @return 表头条目借用指针；init 创建（构造失败为 NULL）。
 * @note 子节点文本镜像 m_headerLabels（setHeaderLabels/setHeaderItem
 *       双向同步）；列文本读取以子条目 text 承载，对标 Qt 表头即
 *       条目的模型。
 */
XTreeWidgetItem* XTreeWidget_headerItem(const XTreeWidget* self);
/** @brief 设置表头条目（对标 setHeaderItem；控件接管所有权）。
 * @param self 目标控件。
 * @param item 新表头条目（移交所有权；条目子节点文本成为各列表头，
 *             并回填标签承载 m_headerLabels 供绘制路径读取）。
 * @return 无返回值（成功时请求一次重绘）。
 * @note 旧表头条目由控件删除；item 后续随控件析构，调用方不得重复
 *       删除。
 */
void XTreeWidget_setHeaderItem(XTreeWidget* self, XTreeWidgetItem* item);

/* ==================== 展开/折叠与结构便捷族（对标 QTreeWidget） ==================== */

/** @brief 展开顶层行（对标 expandItem；对接展开状态承载）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号；越界、无子条目或已展开为无操作）。
 * @return 无返回值。
 * @note 对接基类语义说明：XTreeView_expand/collapse 按基类模型行
 *       承载，本控件条目树未接模型，故对接本控件 m_topExpanded 展开
 *       状态（与 itemExpanded/itemCollapsed 信号同一承载）；由折叠
 *       变展开时发射 itemExpanded(row)，折叠行的子树恢复绘制。
 */
void XTreeWidget_expandItem(XTreeWidget* self, int row);
/** @brief 收起顶层行（对标 collapseItem；对接展开状态承载）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号；越界、无子条目或已折叠为无操作）。
 * @return 无返回值。
 * @note 对接说明同 expandItem（m_topExpanded 独立承载）；由展开变
 *       折叠时发射 itemCollapsed(row)，折叠后子树停止绘制。
 */
void XTreeWidget_collapseItem(XTreeWidget* self, int row);
/** @brief 按文本反查顶层行号（对标 indexOfTopLevelItem 的文本匹配形态）。
 * @param self 目标控件。
 * @param text 查找文本（UTF-8 借用；NULL 返回 -1）。
 * @return 首个精确匹配（区分大小写）的顶层行号；未命中 -1。
 * @note 仅顶层条目参与匹配（对标 Qt indexOfTopLevelItem 只查顶层）；
 *       与 findItems 精确模式同一比较口径。
 */
int XTreeWidget_indexOfTopLevelItem(const XTreeWidget* self,
                                    const char* text);
/** @brief 平铺上一行（对标 itemAbove；平铺模型 above = row - 1）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 上一行行号；row <= 0、越界或 self 为 NULL 返回 -1。
 * @note 平铺顶层行模型：可视次序即行号次序（above = row - 1，无独立
 *       条目树遍历）；行号以 int 承载条目（-1=无）。
 */
int XTreeWidget_itemAbove(const XTreeWidget* self, int row);
/** @brief 平铺下一行（对标 itemBelow；平铺模型 below = row + 1）。
 * @param self 目标控件。
 * @param row 行号（平铺顶层行号）。
 * @return 下一行行号；row + 1 越界（>=顶层行数）、row < 0 或
 *         self 为 NULL 返回 -1。
 * @note 平铺顶层行模型：可视次序即行号次序（below = row + 1）；
 *       行号以 int 承载条目（-1=无）。
 */
int XTreeWidget_itemBelow(const XTreeWidget* self, int row);
/** @brief 设置列数（对标 setColumnCount；对接既有列机制）。
 * @param self 目标控件。
 * @param count 新列数（>=1；小于 1 忽略）。
 * @return 无返回值。
 * @note columnCount 由状态承载（默认 1，行为不变）；count 增大时
 *       同步扩容单元格部件各行列容量（xtw_ensureCellCols），新列的
 *       setItemWidget 挂载即时可用。自绘当前仍为平铺单文本列渲染
 *       （每行一次 drawText），多列渲染待列绘制路径建立后接入。
 */
void XTreeWidget_setColumnCount(XTreeWidget* self, int count);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XTREEWIDGET_H */
