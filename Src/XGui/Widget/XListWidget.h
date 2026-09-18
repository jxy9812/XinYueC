/**
 * @file       XListWidget.h
 * @brief      XListWidget 列表控件（对标 Qt 6.8 QListWidget）。
 * @details    以 m_base 组合继承 XListView；持有内建数据模型桥
 *             （XAbstractItemModel 单列）并通过 XListView 的 model 渲染
 *             呈现条目；提供 addItem/addItems/insertItem/item/takeItem/
 *             clear/findItems/sortItems/itemAt/visualItemRect 等条目便捷
 *             族与行级部件挂载（借用语义）；选择基于基类选择模型
 *             （selectedItems/scrollToItem），并承载 currentItemChanged/
 *             itemSelectionChanged 信号（setCurrentItem/setCurrentRow
 *             与行移除/清空路径真实发射）；便捷族剩余覆盖
 *             currentRowChanged/currentTextChanged/itemActivated/
 *             itemChanged/itemClicked/itemDoubleClicked/itemEntered/
 *             itemPressed 信号族（覆写基类鼠标/键盘事件叠加行号发射，
 *             itemChanged 经内建模型 dataChanged 桥接）、insertItems/
 *             indexFromItem/itemFromIndex/row/editItem 与排序使能
 *             （isSortingEnabled/setSortingEnabled，开启时插入自动
 *             排序）。
 * @note       模块总开关 XTABLEWIDGET_ON；XListWidget→XListView。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLISTWIDGET_H
#define XLISTWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XListView.h"
#include "XAbstractItemModel.h"
#include "XString.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XListWidget)
XCLASS_DEFINE_EXTEND_END(XListWidget, XListView)

/** @brief 列表控件对象；m_base 必须是第一个成员（嵌 XListView）。 */
typedef struct XListWidget
{
    XListView m_base;             /**< 基类成员；必须是第一个。 */
    XAbstractItemModel* m_model;  /**< 内建条目模型（对象拥有；单列）。 */
    XWidget** m_rowWidgets;       /**< 行级挂载部件表（平行数组，下标=行号）；
                                       表本身对象拥有，部件为借用
                                       （生命周期由调用方管理，列表不删除）；
                                       行数变化入口（插入/移除/排序/清空）
                                       同步平移，deinit 释放承载表。 */
    int m_rowWidgetCapacity;      /**< 行级部件表容量（倍增扩容；与模型
                                       行数独立，行数收缩时平移清尾）。 */
    bool m_sortingEnabled;        /**< 排序使能（setSortingEnabled 承载；
                                       开启时插入条目按 m_sortOrder
                                       自动排序，对标 sortingEnabled）。 */
    int m_sortOrder;              /**< 最近排序序：0=升序，1=降序（默认
                                       0；sortItems 落地时写入，
                                       setSortingEnabled(true) 按此排序）。 */
    int m_enteredRow;             /**< 上次发射 itemEntered 的行号；
                                       -2=尚未进入任何行（差分判重）。 */
    bool m_dataGuard;             /**< 内部数据改写保护：插入平移/取出
                                       前移/排序写回等批量 setData 期间
                                       置位，抑制 itemChanged 桥接转发
                                       （对标 Qt 内部路径不发 itemChanged）。 */
} XListWidget;

/* ==================== 生命周期 ==================== */

XVtable* XListWidget_class_init(void);
/** @brief 初始化列表控件。
 * @param self 目标控件；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XListWidget_init(XListWidget* self, XWidget* parent,
                      XWidgetFlags flags);
/** @brief 使用指定内存类型创建列表控件。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XListWidget* XListWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XListWidget_create(parent, flags) \
    XListWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XListWidget_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XListWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 条目（对标 QListWidget） ==================== */

/** @brief 追加条目（XString 主版本；对标 addItem）。
 * @param self 目标控件。
 * @param text 借用 XString*；不能为 NULL。
 * @return 新条目行号；失败 -1。
 */
int XListWidget_addItem(XListWidget* self, const XString* text);
/** @brief 追加条目（UTF-8 兼容重载，转发主版本）。 */
int XListWidget_addItem_2(XListWidget* self, const char* text);
/** @brief 批量追加文本条目（对标 addItems 便捷形态）。
 * @param self 目标控件。
 * @param texts UTF-8 文本数组（借用；元素可为 NULL=空文本行）。
 * @param count 文本个数（<=0 返回 0）。
 * @return 成功追加的条目数；写入中途失败即停止（返回已追加数）。
 */
int XListWidget_addItems(XListWidget* self, const char* const* texts,
                         int count);
/** @brief 插入条目（XString 主版本；对标 insertItem）。
 * @param self 目标控件。
 * @param row 插入行（0 起，负数最前、超出追加；真实行内插入，
 *            原行及其后条目整体后移）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 插入行号；失败 -1。
 */
int XListWidget_insertItem(XListWidget* self, int row, const XString* text);
/** @brief 插入条目（UTF-8 兼容重载，转发主版本）。 */
int XListWidget_insertItem_2(XListWidget* self, int row, const char* text);
/** @brief 批量插入文本条目（对标 insertItems 便捷形态）。
 * @param self 目标控件。
 * @param index 插入行（0 起；负数最前、超出当前行数追加，同
 *              insertItem 收敛口径）。
 * @param texts UTF-8 文本数组（借用；元素可为 NULL=空文本行）。
 * @param count 文本个数（<=0 返回 0）。
 * @return 成功插入的条目数；写入中途失败即停止（返回已插入数）。
 * @note 逐行经 insertItem 真实行内插入（原行及其后条目整体后移）；
 *       排序使能开启时插入完成后按当前排序序自动排序（对标
 *       sortingEnabled 语义），返回值为插入时行号、自动排序后的
 *       实际行号可能变化。
 */
int XListWidget_insertItems(XListWidget* self, int index,
                            const char* const* texts, int count);
/** @brief 查询条目数（对标 count）。 @param self 目标控件。 @return 条目数。 */
int XListWidget_count(const XListWidget* self);
/** @brief 读取条目文本（内部借用 XString*；不得释放）。
 * @param self 目标控件。
 * @param row 行号（越界返回 NULL）。
 * @return 借用 XString*。
 */
const XString* XListWidget_item(const XListWidget* self, int row);
/** @brief 读取条目文本（UTF-8 借用）。 */
const char* XListWidget_item_2(const XListWidget* self, int row);
/** @brief 读取条目文本（新建副本版；对标 item 的独立持有形态）。
 * @param self 目标控件。
 * @param row 行号（越界返回 NULL）。
 * @return 新建 XString*（堆上文本副本，由调用方用 XString_delete_base
 *         释放；行文本为空时返回空串对象）。
 */
XString* XListWidget_item_new(const XListWidget* self, int row);
/** @brief 取出条目（对标 takeItem：移除该行并归还文本所有权）。
 * @param self 目标控件。
 * @param row 行号（越界返回 NULL）。
 * @return 被取出条目的文本副本（堆上 XString*，由调用方用
 *         XString_delete_base 释放；空文本行返回空串对象）；
 *         该行自列表移除，其后条目整体前移。
 */
XString* XListWidget_takeItem(XListWidget* self, int row);
/** @brief 清空全部条目（同时释放行级部件承载表）。 @param self 目标控件。 */
void XListWidget_clear(XListWidget* self);

/* ==================== 当前条目（对标 currentItem/setCurrentItem） ==================== */

/** @brief 查询当前条目（对标 currentItem；行模型下以行号承载）。
 * @param self 目标控件。
 * @return 当前行号（取基类 XAbstractItemView 当前索引；越界或无当前项
 *         返回 -1）。
 */
int XListWidget_currentItem(const XListWidget* self);
/** @brief 设置当前条目并滚动至可见（对标 setCurrentItem）。
 * @param self 目标控件。
 * @param row 行号（越界忽略；清除当前项请用 setCurrentRow(-1)）。
 * @return 无返回值。
 * @note 经基类 XAbstractItemView_setCurrentIndex 承载（含选择模型联动），
 *       并按本控件行几何（visualItemRect）滚动 EnsureVisible。
 */
void XListWidget_setCurrentItem(XListWidget* self, int row);
/** @brief 查询当前行。 @param self 目标控件。 @return 行号；无返回 -1。 */
int XListWidget_currentRow(const XListWidget* self);
/** @brief 设置当前行。
 * @param self 目标控件。
 * @param row 行号（-1 清除）。
 * @return 无返回值。
 * @note 真实发射点：当前行变化发射 currentItemChanged(current, previous)；
 *       选择集合随之变化（含 row<0 清除选中）发射 itemSelectionChanged。
 */
void XListWidget_setCurrentRow(XListWidget* self, int row);
/** @brief 滚动使指定条目可见（对标 scrollToItem；EnsureVisible 语义）。
 * @param self 目标控件。
 * @param row 行号（越界忽略）。
 * @return 无返回值。
 * @note 与 setCurrentItem 同一滚动机制：按本控件行几何
 *       （visualItemRect）计算目标值写入垂直滚动条，滚动范围由
 *       滚动条自身收敛。
 */
void XListWidget_scrollToItem(XListWidget* self, int row);

/* ==================== 选中（对标 selectedItems） ==================== */

/** @brief 查询选中行数组（对标 selectedItems；基于选择模型）。
 * @param self 目标控件。
 * @param outRows 选中行号输出数组（借用；调用方分配）。
 * @param maxCount 输出数组容量（<=0 返回 0；命中数超出时截断）。
 * @return 实际写入 outRows 的选中行数（按行序升序）。
 * @note 行模型下单选路径由 setCurrentRow/setCurrentItem 写入选择模型；
 *       无选择模型或无选中返回 0。
 */
int XListWidget_selectedItems(const XListWidget* self, int* outRows,
                              int maxCount);

/* ==================== 查找/排序/几何（对标 QListWidget） ==================== */

/** @brief 按文本查找条目（对标 findItems；命中行号写入 outRows）。
 * @param self 目标控件。
 * @param text 查找文本（UTF-8 借用；NULL 返回 0）。
 * @param flags 匹配方式：1=精确相等（MatchExactly），0=包含子串
 *              （MatchContains）；均区分大小写。
 * @param outRows 命中行号输出数组（借用；调用方分配）。
 * @param maxCount 输出数组容量（<=0 返回 0；命中数超出时截断）。
 * @return 实际写入 outRows 的命中行数（按行序升序）。
 */
int XListWidget_findItems(const XListWidget* self, const char* text,
                          int flags, int* outRows, int maxCount);
/** @brief 排序全部条目（对标 sortItems(Qt::SortOrder)；整行重排）。
 * @param self 目标控件。
 * @param order 排序序：0=升序（AscendingOrder），1=降序
 *              （DescendingOrder）；其他值忽略。
 * @return 无返回值。
 * @note 按条目文本字典序排序（NULL 视为空串、参与排序且最小）；稳定
 *       排序（相等条目保持原相对次序）；行级挂载部件随行同步重排。
 */
void XListWidget_sortItems(XListWidget* self, int order);
/** @brief 查询排序使能（对标 isSortingEnabled）。
 * @param self 目标控件。
 * @return 排序使能开启返回 true；默认关闭。
 */
bool XListWidget_isSortingEnabled(const XListWidget* self);
/** @brief 设置排序使能（对标 setSortingEnabled）。
 * @param self 目标控件。
 * @param enable true=开启：立即按当前排序序排序一次，此后
 *               addItem/insertItem/addItems/insertItems 写入的条目
 *               自动按该序排序；false=关闭（存量次序保持，不再自动
 *               排序）。
 * @return 无返回值。
 * @note 排序序由最近一次 sortItems 记录（未排序过默认升序）；自动
 *       排序为整行重排（条目文本 + 行级挂载部件随动，同 sortItems
 *       口径）；重复设置同值无操作。
 */
void XListWidget_setSortingEnabled(XListWidget* self, bool enable);
/** @brief 位置反查条目（对标 itemAt；经基类 indexAt 虚槽分派）。
 * @param self 目标控件。
 * @param x 视图坐标 X。
 * @param y 视图坐标 Y。
 * @return 命中行号；未命中或参数无效返回 -1。
 * @note 与绘制/鼠标命中同一几何（行高/间距/网格/行隐藏一致）。
 */
int XListWidget_itemAt(const XListWidget* self, int x, int y);
/** @brief 查询条目可视矩形（对标 visualItemRect；基于行几何）。
 * @param self 目标控件。
 * @param row 行号（越界返回全零矩形）。
 * @return 视图坐标矩形。
 * @note 几何与 XListView 自绘一致：槽高=max(行高,网格高)、槽宽=网格宽
 *       启用且小于视口时收缩；y 为该行之前可见行的槽高+间距累加
 *       （隐藏行不占位）；表头不占行带。
 */
XRect XListWidget_visualItemRect(const XListWidget* self, int row);

/* ==================== 索引反查/编辑（对标 QListWidget） ==================== */

/** @brief 行号 → 索引（对标 indexFromItem；行号即索引，恒等简化）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 有效行返回 row 本身；越界或 self 为 NULL 返回 -1。
 * @note 单列行模型下索引即行号（无独立 QModelIndex），恒等映射
 *       （与 itemFromIndex 互为对称）。
 */
int XListWidget_indexFromItem(const XListWidget* self, int row);
/** @brief 索引 → 条目文本（对标 itemFromIndex 的行文本承载形态）。
 * @param self 目标控件。
 * @param row 行号（越界返回 NULL）。
 * @return 该行文本内部借用指针（不得释放）；与 item(row) 同一承载
 *         （恒等映射的对称 API）。
 */
const XString* XListWidget_itemFromIndex(const XListWidget* self, int row);
/** @brief 按文本反查行号（对标 row 的文本匹配形态）。
 * @param self 目标控件。
 * @param text 查找文本（UTF-8 借用；NULL 返回 -1）。
 * @return 首个精确匹配（区分大小写）的行号；未命中 -1。
 */
int XListWidget_row(const XListWidget* self, const char* text);
/** @brief 触发条目编辑（对标 editItem；句柄预留）。
 * @param self 目标控件。
 * @param row 行号（越界忽略）。
 * @return 无返回值。
 * @note 句柄预留：本控件尚无编辑器/委托（editor/delegate）机制，
 *       编辑触发无从落地，当前为校验入参后的无操作；编辑路径建立
 *       后应在此发射 itemChanged(row)。
 */
void XListWidget_editItem(XListWidget* self, int row);

/* ==================== 行级部件挂载（对标 setItemWidget/itemWidget） ==================== */

/** @brief 挂载行级部件（对标 setItemWidget；单列，下标=行号）。
 * @param self 目标控件。
 * @param row 行号（越界忽略）。
 * @param widget 部件借用指针；列表不取得所有权、不重设父控件、
 *               不负责删除（与 removeItemWidget 置 NULL 语义配套）。
 * @return 无返回值。
 * @note 部件表随行数变化入口（插入/移除/排序/清空）同步平移或释放；
 *       重复挂载覆盖旧指针（旧部件仅解除关联，不被删除）。
 */
void XListWidget_setItemWidget(XListWidget* self, int row, XWidget* widget);
/** @brief 读取行级挂载部件（对标 itemWidget）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 部件借用指针（所有权仍属调用方，列表不删除）；
 *         越界或未挂载返回 NULL。
 */
XWidget* XListWidget_itemWidget(const XListWidget* self, int row);
/** @brief 移除行级挂载部件（对标 removeItemWidget；置 NULL）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 无返回值。
 * @note 仅将承载指针置 NULL（部件为借用，不删除、不改父控件）；
 *       越界或该行无挂载为无操作。
 */
void XListWidget_removeItemWidget(XListWidget* self, int row);

/* ==================== 信号（对标 QListWidget） ==================== */

/** @brief currentItemChanged(current, previous) 信号（发射并返回地址）。
 * @param self 目标控件。
 * @param current 当前行号（无当前项为 -1）。
 * @param previous 前当前行号（无前当前项为 -1）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：setCurrentRow/setCurrentItem 当前行变化路径、
 *       takeItem/clear 使当前项失效路径。
 */
void* XListWidget_currentItemChanged_signal(XListWidget* self, int current,
                                            int previous);
/** @brief itemSelectionChanged() 信号（发射并返回地址；无载荷）。
 * @param self 目标控件。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：选择集合变化路径——setCurrentRow/setCurrentItem
 *       的选择联动与 row<0 清除、takeItem 行移除选择前移、clear 清空。
 */
void* XListWidget_itemSelectionChanged_signal(XListWidget* self);

/** @brief currentRowChanged(current, previous) 信号（发射并返回地址）。
 * @param self 目标控件。
 * @param current 当前行号（无当前项为 -1）。
 * @param previous 前当前行号（无前当前项为 -1）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：setCurrentRow/setCurrentItem 当前行变化路径、
 *       takeItem/clear 使当前项失效路径、鼠标按下命中行换当前项
 *       路径（与 currentItemChanged 同点成对发射）。
 */
void* XListWidget_currentRowChanged_signal(XListWidget* self, int current,
                                           int previous);
/** @brief currentTextChanged(text) 信号（发射并返回地址）。
 * @param self 目标控件。
 * @param text 新当前行 UTF-8 文本（借用；无当前项为 ""）。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：与 currentRowChanged 同点成对发射（当前行变化
 *       即随行文本更新；对标 QListView currentTextChanged）。
 */
void* XListWidget_currentTextChanged_signal(XListWidget* self,
                                            const char* text);
/** @brief itemActivated(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：双击命中行路径、键盘 Enter/Return 激活当前行
 *       路径（经覆写基类键盘处理叠加发射，对标 QListView 激活语义）。
 */
void* XListWidget_itemActivated_signal(XListWidget* self, int row);
/** @brief itemChanged(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：数据变化路径——内建模型 dataChanged(row,0) 桥接
 *       （init 时连接；外部经 XListWidget_model 取模型 setData 改文本
 *       即触发）。内部批量改写（插入平移/取出前移/排序写回）受
 *       m_dataGuard 保护不逐行发射；编辑器路径（editItem）建立后为
 *       第二发射点。
 */
void* XListWidget_itemChanged_signal(XListWidget* self, int row);
/** @brief itemClicked(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标左键按下命中行路径（先 itemPressed 后
 *       itemClicked，同 XTreeWidget/XTableWidget 约定）。
 */
void* XListWidget_itemClicked_signal(XListWidget* self, int row);
/** @brief itemDoubleClicked(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标左键双击命中行路径（随后发射 itemActivated）。
 */
void* XListWidget_itemDoubleClicked_signal(XListWidget* self, int row);
/** @brief itemEntered(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标移动进入新行路径（经覆写基类移动处理叠加
 *       发射；m_enteredRow 差分判重，同 XTableWidget cellEntered
 *       口径；事件投递依赖窗口层移动事件派发）。
 */
void* XListWidget_itemEntered_signal(XListWidget* self, int row);
/** @brief itemPressed(row) 信号（调用即发射并返回地址）。
 * @param self 目标控件。
 * @param row 行号。
 * @return 信号槽地址（连接用句柄）。
 * @note 真实发射点：鼠标左键按下命中行路径（先于 itemClicked）。
 */
void* XListWidget_itemPressed_signal(XListWidget* self, int row);

/** @brief 内建条目模型（对标 QListWidget::model）。
 * @param self 目标控件。
 * @return 模型借用指针。
 */
XAbstractItemModel* XListWidget_model(const XListWidget* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XLISTWIDGET_H */
