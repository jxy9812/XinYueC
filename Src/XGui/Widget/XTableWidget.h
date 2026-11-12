/**
 * @file       XTableWidget.h
 * @brief      表格控件公开 API（对齐 Qt 6.8 QTableWidget / QTableWidgetItem）。
 * @details    继承链与 Qt 一致：XTableWidget → XTableView → XAbstractItemView
 *             → XAbstractScrollArea → XWidget。本类在 XTableView 之上提供
 *             「行×列表格」的便捷层：单元格条目对象（XTableWidgetItem）由
 *             表格持有所有权、按 (row,column) 索引；表头标签、单元格部件
 *             挂载、当前格/选区、排序与查找便捷族均对齐 Qt 同名接口。
 *             实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或
 *             其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABLEWIDGET_H
#define XTABLEWIDGET_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XAbstractScrollArea.h"
#include "XTableView.h"
#include "XAbstractItemModel.h"
#include "XVector.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XTABLEWIDGET_ON

XCLASS_DEFINE_BEGING(XTableWidget)
XCLASS_DEFINE_EXTEND_END(XTableWidget, XTableView)

/* ==========================================================================
 * XTableWidgetItem（对齐 Qt 6.8 QTableWidgetItem）
 * ========================================================================== */

XCLASS_DEFINE_BEGING(XTableWidgetItem)
XCLASS_DEFINE_ENUM(XTableWidgetItem, Clone) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XTableWidgetItem)

/** @brief XTableWidget 前向声明（条目的 m_view 反向引用；定义见本节末）。 */
typedef struct XTableWidget XTableWidget;

/**
 * @brief role→数据映射元素（对齐 Qt 私有 `QWidgetItemData`）。
 * @details 由条目对象在 `m_values` 中持有；`value` 归条目所有，随条目释放。
 */
typedef struct XTableWidgetItemData
{
    int role;        /**< 数据角色（XItemDataRole；EditRole 已归一化为 DisplayRole）。 */
    XVariant* value; /**< 角色数据（对象拥有；NULL 表示该角色无值）。 */
} XTableWidgetItemData;

/**
 * @brief 条目类型标识（对齐 `QTableWidgetItem::ItemType`）。
 * @details `type()` 返回构造时写入的整数；`_Type` 是基类条目的缺省值，
 *          `_UserType` 是调用方自定义子类类型的起点。
 */
typedef enum XTableWidgetItemType
{
    XTableWidgetItem_Type = 0,      /**< 基类条目类型（对齐 `QTableWidgetItem::Type`）。 */
    XTableWidgetItem_UserType = 1000/**< 用户自定义类型起点（对齐 `UserType`）。 */
} XTableWidgetItemType;

/** @brief XVariant 前向声明（role 数据的载体；定义见 XData/XVariant）。 */
typedef struct XVariant XVariant;
/** @brief XIcon 前向声明（DecorationRole 的 Qt 对齐载体）。 */
typedef struct XIcon XIcon;
/** @brief XFont 前向声明（FontRole 的载体；借用指针）。 */
typedef struct XFont XFont;
/** @brief XDataStream 前向声明（条目序列化载体；定义见 XCode/XDataStream）。 */
typedef struct XDataStream XDataStream;

/**
 * @brief 表格单元格条目（对齐 Qt 6.8 `QTableWidgetItem`）。
 *
 * @details 每个单元格拥有一个堆上的 `XTableWidgetItem`；条目本身由所属
 *          表格拥有并释放（对齐 Qt：`setItem` 后 item 归 table 所有）。
 *          role→数据存放在对象内部的 `m_values` 容器中，元素为
 *          `XTableWidgetItemData{ int role; XVariant* value; }`，值按深拷贝
 *          持有。`DisplayRole` 与 `EditRole` 共用同一存储槽（对齐 Qt
 *          `QTableWidgetItem::setData` 的 EditRole→DisplayRole 归一化）。
 *
 * @note  本类型继承 XObject，因此必须经 `_init` / `_create` 系列构造，
 *        并经 `XClassDeinit` / `XClassDelete` 释放；禁止栈上裸声明后按值
 *        传入 `XTableWidget_setItem`（旧版的 POD 值拷贝语义已取消）。
 * @note  条目的 `m_view` 是所属表格的**借用**指针，不转移所有权；条目被
 *        `takeItem` 取出或表格析构后置 NULL（对齐 Qt `item->view = nullptr`），
 *        此时 `row()`/`column()` 返回 -1。
 */
typedef struct XTableWidgetItem
{
    XObject m_class;          /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XTableWidget* m_view;     /**< 所属表格（借用指针，不转移所有权）；NULL=游离条目。 */
    XVector m_values;         /**< role→XVariant* 映射（元素 XTableWidgetItemData；对象拥有）。 */
    int m_type;               /**< 条目类型（XTableWidgetItemType 或自定义值）。 */
    uint32_t m_itemFlags;     /**< 条目标志位（XItemFlag 位组合）；默认 0，语义见 XTableWidgetItem_flags。 */
    uint32_t m_selected : 1;  /**< 选中标记（对齐 Qt `QTableWidgetItemPrivate::selected`）。 */
    uint32_t m_itemFlagsUnused : 31; /**< 保留位；调用方不得依赖。 */
} XTableWidgetItem;

/**
 * @brief 单元格挂载部件条目（内部承载记录；对齐 `setCellWidget` 承载表）。
 *
 * @details widget 由表格拥有（对齐 Qt：`setIndexWidget` 接管所有权、
 *          reparent 到 viewport、替换与移除时 `deleteLater`）。
 */
typedef struct XTableWidgetCellWidget
{
    XWidget* widget;  /**< 挂载的部件（对象拥有；表格析构/替换/移除时释放）。 */
    int16_t row;      /**< 挂载行号。 */
    int16_t column;   /**< 挂载列号。 */
} XTableWidgetCellWidget;

/* -------------------- XTableWidgetItem 生命周期 -------------------- */

/** @brief 初始化并返回 XTableWidgetItem 虚函数表。 @return 单例虚函数表指针；调用方不得释放。 */
XVtable* XTableWidgetItem_class_init(void);

/**
 * @brief 初始化条目（栈对象；对齐 `QTableWidgetItem(int type = Type)`）。
 *
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @return 无。
 * @note  缺省标志位对齐 Qt：`ItemIsEditable | ItemIsSelectable |
 *        ItemIsUserCheckable | ItemIsEnabled | ItemIsDragEnabled |
 *        ItemIsDropEnabled`。
 */
void XTableWidgetItem_init(XTableWidgetItem* self);

/**
 * @brief 初始化条目并设置类型（栈对象；对齐 `QTableWidgetItem(int type)`）。
 *
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param type 条目类型（XTableWidgetItemType 或自定义值）。
 * @return 无。
 */
void XTableWidgetItem_init_type(XTableWidgetItem* self, int type);

/**
 * @brief 初始化条目并设置文本（栈对象；对齐 `QTableWidgetItem(const QString&, int)`）。
 *
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param text 显示文本；NULL 按空串处理，不转移所有权。
 * @param type 条目类型。
 * @return 无。
 */
void XTableWidgetItem_init_text(XTableWidgetItem* self, const XString* text,
                                int type);

/**
 * @brief 初始化条目并设置图标与文本（栈对象；对齐 `QTableWidgetItem(const QIcon&, const QString&, int)`）。
 *
 * @param self  目标条目；传入 NULL 时函数不执行任何操作。
 * @param icon  图标；借用，本条目不取得所有权（内部另存副本语义由
 *              `XTableWidgetItem_setIcon` 定义）。可为 NULL。
 * @param text  显示文本；NULL 按空串处理。
 * @param type  条目类型。
 * @return 无。
 */
void XTableWidgetItem_init_iconText(XTableWidgetItem* self, const XIcon* icon,
                                    const XString* text, int type);

/**
 * @brief 堆上创建条目（对齐 `QTableWidgetItem(int type = Type)`）。
 *
 * @param memory 内存类型。
 * @return 新建条目，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 */
XTableWidgetItem* XTableWidgetItem_create_ex(XMemoryType memory);

/**
 * @brief 堆上创建条目（默认内存类型）。
 * @return 新建条目；分配失败返回 NULL。
 */
XTableWidgetItem* XTableWidgetItem_create(void);

/**
 * @brief 堆上创建带文本的条目（对齐 `QTableWidgetItem(const QString&, int)`）。
 *
 * @param text 显示文本；NULL 按空串处理，不转移所有权。
 * @param type 条目类型。
 * @return 新建条目，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 */
XTableWidgetItem* XTableWidgetItem_create_text(const XString* text, int type);

/**
 * @brief 堆上创建带文本的条目（UTF-8 入参；对齐 `QTableWidgetItem(const QString&)`）。
 *
 * @param utf8 显示文本（UTF-8）；NULL 按空串处理。
 * @param type 条目类型。
 * @return 新建条目，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 */
XTableWidgetItem* XTableWidgetItem_create_utf8(const char* utf8, int type);

/**
 * @brief 堆上创建带图标与文本的条目（对齐 `QTableWidgetItem(const QIcon&, const QString&, int)`）。
 *
 * @param icon 图标；借用，本条目不取得所有权。可为 NULL。
 * @param text 显示文本；NULL 按空串处理。
 * @param type 条目类型。
 * @return 新建条目，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 */
XTableWidgetItem* XTableWidgetItem_create_iconText(const XIcon* icon,
                                                   const XString* text,
                                                   int type);

/**
 * @brief 拷贝创建条目（对齐 `QTableWidgetItem(const QTableWidgetItem&)`）。
 *
 * @param other 源条目；传入 NULL 返回 NULL。
 * @return 新建条目（深拷贝全部 role 数据与标志位，`m_view` 置 NULL），
 *         由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 * @note  对齐 Qt 拷贝构造（qtablewidget.cpp:1521-1533）：**type() 不拷贝**
 *        ——复制出的条目恒为 `XTableWidgetItem_Type`（自定义类型的 clone()
 *        同样回落 Type）；选中态也不拷贝（Qt 的选中态在选择模型里，
 *        新条目未选中）。与 `XClassCopy` 的 operator= 语义区分：
 *        operator= 只拷 role 表与 flags、保留左侧 type（Qt :1536-1544）。
 */
XTableWidgetItem* XTableWidgetItem_create_copy(const XTableWidgetItem* other);

/* -------------------- XTableWidgetItem 克隆 -------------------- */

/**
 * @brief 多态克隆（对齐 `QTableWidgetItem::clone`）。
 *
 * @param self 目标条目；传入 NULL 返回 NULL。
 * @return 新建条目（同 `XTableWidgetItem_create_copy` 语义），由调用方以
 *         `XClassDelete` 释放；分配失败返回 NULL。
 * @note  子类可重载 `EXTableWidgetItem_Clone` 槽以克隆自身类型；未重载时
 *        回落基类拷贝。
 */
XTableWidgetItem* XTableWidgetItem_clone_base(const XTableWidgetItem* self);

/* -------------------- XTableWidgetItem 查询/设置 -------------------- */

/**
 * @brief 查询条目类型（对齐 `QTableWidgetItem::type`）。
 * @param self 目标条目；可为 NULL。
 * @return 条目类型；self 为 NULL 返回 0。
 */
int XTableWidgetItem_type(const XTableWidgetItem* self);

/**
 * @brief 查询所属表格（对齐 `QTableWidgetItem::tableWidget`）。
 * @param self 目标条目；可为 NULL。
 * @return 所属表格借用指针（归表格所有，调用方不得释放）；游离条目或
 *         self 为 NULL 返回 NULL。
 */
XTableWidget* XTableWidgetItem_tableWidget(const XTableWidgetItem* self);

/**
 * @brief 查询条目所在行（对齐 `QTableWidgetItem::row`）。
 * @param self 目标条目；可为 NULL。
 * @return 行号；游离条目（不属于任何表格）或 self 为 NULL 返回 -1。
 */
int XTableWidgetItem_row(const XTableWidgetItem* self);

/**
 * @brief 查询条目所在列（对齐 `QTableWidgetItem::column`）。
 * @param self 目标条目；可为 NULL。
 * @return 列号；游离条目或 self 为 NULL 返回 -1。
 */
int XTableWidgetItem_column(const XTableWidgetItem* self);

/**
 * @brief 查询条目标志位（对齐 `QTableWidgetItem::flags`）。
 * @param self 目标条目；可为 NULL。
 * @return XItemFlag 位组合；self 为 NULL 返回 0。
 */
uint32_t XTableWidgetItem_flags(const XTableWidgetItem* self);

/**
 * @brief 设置条目标志位（对齐 `QTableWidgetItem::setFlags`）。
 * @param self  目标条目；传入 NULL 时函数不执行任何操作。
 * @param flags XItemFlag 位组合。
 * @return 无。
 * @note  对齐 Qt（qtablewidget.cpp:1129-1135）：**同值也写入并通知**
 *        `itemChanged`/`cellChanged`（Qt 无同值早退）。
 */
void XTableWidgetItem_setFlags(XTableWidgetItem* self, uint32_t flags);

/**
 * @brief 查询条目是否被标记为选中（对齐 `QTableWidgetItem::isSelected`）。
 * @param self 目标条目；可为 NULL。
 * @return 选中返回 true；self 为 NULL 返回 false。
 */
bool XTableWidgetItem_isSelected(const XTableWidgetItem* self);

/**
 * @brief 设置条目选中标记（对齐 `QTableWidgetItem::setSelected`）。
 * @param self   目标条目；传入 NULL 时函数不执行任何操作。
 * @param select true 标记为选中。
 * @return 无。
 * @note  对齐 Qt：**条目必须先插入表格**（tableWidget() 非 NULL）才可
 *        选中；游离条目 setSelected 是空操作（gap 296，Qt 的
 *        `if (d->view)` 分支）。选中态由所属表格的选中路径与
 *        `XTableWidget_setRangeSelected` 维护（对齐 Qt：item 的 selected
 *        位由 selectionModel 驱动）。
 */
void XTableWidgetItem_setSelected(XTableWidgetItem* self, bool select);

/* -------------------- role 数据（对齐 data / setData） -------------------- */

/**
 * @brief 读取指定角色的数据（对齐 `QTableWidgetItem::data`）。
 *
 * @param self 目标条目；可为 NULL。
 * @param role 数据角色（XItemDataRole）；`EditRole` 按 `DisplayRole` 处理
 *             （对齐 Qt 归一化）。
 * @return 该角色数据的借用指针（归条目所有，调用方不得释放，随条目析构
 *         或该角色被重设而失效）；无该角色数据或 self 为 NULL 返回 NULL。
 */
const XVariant* XTableWidgetItem_data(const XTableWidgetItem* self, int role);

/**
 * @brief 写入指定角色的数据（对齐 `QTableWidgetItem::setData`）。
 *
 * @param self  目标条目；传入 NULL 时函数不执行任何操作。
 * @param role  数据角色；`EditRole` 归一化为 `DisplayRole`。
 * @param value 角色数据；**借用**——函数深拷贝其内容，返回后调用方仍拥有
 *              并须自行释放 `value`。传 NULL 表示移除该角色数据。
 * @return 无。
 * @note  写入 `DisplayRole` 会触发所属表格的 `itemChanged`/`cellChanged`
 *        与模型同步（对齐 Qt `QTableModel::itemChanged`）。
 * @note  对齐 Qt 的同值早退（qtablewidget.cpp:1399-1412）：该 role 已有
 *        值与新值相等时直接返回，**不发**任何变更信号。
 */
void XTableWidgetItem_setData(XTableWidgetItem* self, int role,
                              const XVariant* value);

/**
 * @brief 读取显示文本（对齐 `QTableWidgetItem::text`；返回新建副本）。
 *
 * @param self 目标条目；可为 NULL。
 * @return 新建 XString，由调用方以 `XClassDelete` 释放；无显示文本或
 *         self 为 NULL 时返回空串对象（仍须释放）；分配失败返回 NULL。
 * @note 对齐 Qt `text() = data(DisplayRole).toString()`：DisplayRole 为
 *       int/double/bool 等标量时**转成文本**（本机 Qt 6.8.0 实测
 *       42→"42"、3.5→"3.5"、true→"true"，double 用最短往返表示；
 *       gap 295）；容器/几何等无文本表示的类型返回空串（Qt 同）。
 */
XString* XTableWidgetItem_text(const XTableWidgetItem* self);

/**
 * @brief 读取显示文本（借用；对齐 `QTableWidgetItem::text` 的零拷贝形态）。
 *
 * @param self 目标条目；可为 NULL。
 * @return 内部字符串借用指针（禁止释放、禁止跨写入持有）；无显示文本或
 *         self 为 NULL 返回 NULL。**仅覆盖 String 型 DisplayRole**：
 *         借用形态没有承载数值→文本转换结果的存储，需要文本化数值请用
 *         XTableWidgetItem_text（返回值归调用方）。
 */
const XString* XTableWidgetItem_text_const(const XTableWidgetItem* self);

/**
 * @brief 读取显示文本的 UTF-8 视图（借用）。
 *
 * @param self 目标条目；可为 NULL。
 * @return 内部编码缓存指针（禁止释放；下次写入或条目析构后失效）；
 *         无显示文本或 self 为 NULL 返回空串 `""`（非 NULL）。
 * @note DisplayRole 为数值/布尔时按 QVariant::toString 文本化，结果落在
 *       线程局部 4 槽环缓冲：同线程连续 4 次调用内有效（GUI 单线程足够）；
 *       需要独立生命周期请用 XTableWidgetItem_text。
 */
const char* XTableWidgetItem_text_toUtf8(const XTableWidgetItem* self);

/**
 * @brief 设置显示文本（对齐 `QTableWidgetItem::setText`）。
 *
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param text 显示文本；NULL 按空串处理，不转移所有权。
 * @return 无。
 */
void XTableWidgetItem_setText(XTableWidgetItem* self, const XString* text);

/**
 * @brief 设置显示文本（UTF-8 入参）。
 *
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param utf8 显示文本（UTF-8）；NULL 按空串处理。
 * @return 无。
 */
void XTableWidgetItem_setText_utf8(XTableWidgetItem* self, const char* utf8);

/* -------------------- 便捷 role 访问器（对齐 Qt 同名成员） -------------------- */

/**
 * @brief 读取图标（对齐 `QTableWidgetItem::icon`；DecorationRole）。
 * @param self 目标条目；可为 NULL。
 * @return 图标借用指针（归条目所有，禁止释放）；未设置或 self 为 NULL 返回 NULL。
 */
const XIcon* XTableWidgetItem_icon(const XTableWidgetItem* self);

/**
 * @brief 设置图标（对齐 `QTableWidgetItem::setIcon`；DecorationRole）。
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param icon 图标；借用，本条目不取得所有权（内部转为 DecorationRole 数据）。
 *             可为 NULL（清除该角色）。
 * @return 无。
 */
void XTableWidgetItem_setIcon(XTableWidgetItem* self, const XIcon* icon);

/**
 * @brief 读取状态栏提示（对齐 `QTableWidgetItem::statusTip`；StatusTipRole）。
 * @param self 目标条目；可为 NULL。
 * @return 新建 XString，由调用方以 `XClassDelete` 释放；未设置返回空串对象。
 */
XString* XTableWidgetItem_statusTip(const XTableWidgetItem* self);
/** @brief 读取状态栏提示的 UTF-8 视图（借用）。 @param self 目标条目。 @return 借用字符串；未设置返回 ""。 */
const char* XTableWidgetItem_statusTip_toUtf8(const XTableWidgetItem* self);
/** @brief 设置状态栏提示（对齐 `setStatusTip`）。 @param self 目标条目。 @param tip 提示文本；NULL 清除。 @return 无。 */
void XTableWidgetItem_setStatusTip(XTableWidgetItem* self, const XString* tip);
/** @brief 设置状态栏提示（UTF-8）。 @param self 目标条目。 @param utf8 提示文本；NULL 清除。 @return 无。 */
void XTableWidgetItem_setStatusTip_utf8(XTableWidgetItem* self, const char* utf8);

/** @brief 读取工具提示（对齐 `QTableWidgetItem::toolTip`；ToolTipRole）。 @param self 目标条目。 @return 新建 XString，调用方释放。 */
XString* XTableWidgetItem_toolTip(const XTableWidgetItem* self);
/** @brief 读取工具提示的 UTF-8 视图（借用）。 @param self 目标条目。 @return 借用字符串；未设置返回 ""。 */
const char* XTableWidgetItem_toolTip_toUtf8(const XTableWidgetItem* self);
/** @brief 设置工具提示（对齐 `setToolTip`）。 @param self 目标条目。 @param tip 提示文本；NULL 清除。 @return 无。 */
void XTableWidgetItem_setToolTip(XTableWidgetItem* self, const XString* tip);
/** @brief 设置工具提示（UTF-8）。 @param self 目标条目。 @param utf8 提示文本；NULL 清除。 @return 无。 */
void XTableWidgetItem_setToolTip_utf8(XTableWidgetItem* self, const char* utf8);

/** @brief 读取"What's This"文本（对齐 `QTableWidgetItem::whatsThis`；WhatsThisRole）。 @param self 目标条目。 @return 新建 XString，调用方释放。 */
XString* XTableWidgetItem_whatsThis(const XTableWidgetItem* self);
/** @brief 读取"What's This"的 UTF-8 视图（借用）。 @param self 目标条目。 @return 借用字符串；未设置返回 ""。 */
const char* XTableWidgetItem_whatsThis_toUtf8(const XTableWidgetItem* self);
/** @brief 设置"What's This"文本（对齐 `setWhatsThis`）。 @param self 目标条目。 @param text 文本；NULL 清除。 @return 无。 */
void XTableWidgetItem_setWhatsThis(XTableWidgetItem* self, const XString* text);
/** @brief 设置"What's This"文本（UTF-8）。 @param self 目标条目。 @param utf8 文本；NULL 清除。 @return 无。 */
void XTableWidgetItem_setWhatsThis_utf8(XTableWidgetItem* self, const char* utf8);

/**
 * @brief 读取字体（对齐 `QTableWidgetItem::font`；FontRole）。
 * @param self 目标条目；可为 NULL。
 * @return 字体借用指针（归条目所有，禁止释放）；未设置或 self 为 NULL 返回 NULL。
 */
const XFont* XTableWidgetItem_font(const XTableWidgetItem* self);
/**
 * @brief 设置字体（对齐 `QTableWidgetItem::setFont`；FontRole）。
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param font 字体；借用，本条目不取得所有权。可为 NULL（清除该角色）。
 * @return 无。
 */
void XTableWidgetItem_setFont(XTableWidgetItem* self, const XFont* font);

/**
 * @brief 读取文本对齐（对齐 `QTableWidgetItem::textAlignment`；TextAlignmentRole）。
 * @param self 目标条目；可为 NULL。
 * @return XAlignment 位组合；未设置或 self 为 NULL 返回 0（交给委托缺省）。
 */
int XTableWidgetItem_textAlignment(const XTableWidgetItem* self);
/**
 * @brief 设置文本对齐（对齐 `QTableWidgetItem::setTextAlignment`）。
 * @param self      目标条目；传入 NULL 时函数不执行任何操作。
 * @param alignment XAlignment 位组合；0 表示清除（回落缺省对齐）。
 * @return 无。
 */
void XTableWidgetItem_setTextAlignment(XTableWidgetItem* self, int alignment);

/**
 * @brief 读取背景色（对齐 `QTableWidgetItem::background`；BackgroundRole）。
 * @param self 目标条目；可为 NULL。
 * @param out  颜色输出（调用方提供存储空间）；可为 NULL。
 * @return 设置了背景色返回 true；未设置、self 为 NULL 或 out 为 NULL 返回
 *         false（`out` 保持不变）。
 */
bool XTableWidgetItem_background(const XTableWidgetItem* self, XColor* out);
/**
 * @brief 设置背景色（对齐 `QTableWidgetItem::setBackground`）。
 * @param self  目标条目；传入 NULL 时函数不执行任何操作。
 * @param color 背景色；传 NULL 等价于清除（对齐 Qt `Qt::NoBrush` 语义）。
 * @return 无。
 */
void XTableWidgetItem_setBackground(XTableWidgetItem* self, const XColor* color);

/**
 * @brief 读取前景色（对齐 `QTableWidgetItem::foreground`；ForegroundRole）。
 * @param self 目标条目；可为 NULL。
 * @param out  颜色输出（调用方提供存储空间）；可为 NULL。
 * @return 设置了前景色返回 true；未设置、self 为 NULL 或 out 为 NULL 返回
 *         false（`out` 保持不变）。
 */
bool XTableWidgetItem_foreground(const XTableWidgetItem* self, XColor* out);
/**
 * @brief 设置前景色（对齐 `QTableWidgetItem::setForeground`）。
 * @param self  目标条目；传入 NULL 时函数不执行任何操作。
 * @param color 前景色；传 NULL 等价于清除。
 * @return 无。
 */
void XTableWidgetItem_setForeground(XTableWidgetItem* self, const XColor* color);

/**
 * @brief 读取勾选状态（对齐 `QTableWidgetItem::checkState`；CheckStateRole）。
 * @param self 目标条目；可为 NULL。
 * @return XItemCheckState（0 未选 / 1 半选 / 2 选中）；未设置或 self 为
 *         NULL 返回 `XItemCheckState_Unchecked`。
 */
int XTableWidgetItem_checkState(const XTableWidgetItem* self);
/**
 * @brief 设置勾选状态（对齐 `QTableWidgetItem::setCheckState`）。
 * @param self  目标条目；传入 NULL 时函数不执行任何操作。
 * @param state XItemCheckState（0/1/2；**原样存取**，不做范围校验——
 *              对齐 Qt：本机 Qt 6.8.0 实测 setCheckState((Qt::CheckState)7)
 *              后 checkState()==7，gap 297）。
 * @return 无。
 */
void XTableWidgetItem_setCheckState(XTableWidgetItem* self, int state);

/**
 * @brief 读取尺寸提示（对齐 `QTableWidgetItem::sizeHint`；SizeHintRole）。
 * @param self 目标条目；可为 NULL。
 * @param out  尺寸输出（调用方提供存储空间）；可为 NULL。
 * @return 设置了尺寸提示返回 true；未设置、self 为 NULL 或 out 为 NULL 返回
 *         false（`out` 保持不变）。
 */
bool XTableWidgetItem_sizeHint(const XTableWidgetItem* self, XSize* out);
/**
 * @brief 设置尺寸提示（对齐 `QTableWidgetItem::setSizeHint`）。
 * @param self 目标条目；传入 NULL 时函数不执行任何操作。
 * @param size 尺寸；传 NULL 或宽/高为**负**等价于清除（对齐 Qt
 *             `QSize::isValid()`：`w >= 0 && h >= 0`——(0,0) 是有效尺寸，
 *             会原样存储）。
 * @return 无。
 */
void XTableWidgetItem_setSizeHint(XTableWidgetItem* self, const XSize* size);

/* -------------------- 比较 -------------------- */

/**
 * @brief 条目排序比较（对齐 `QTableWidgetItem::operator<`）。
 *
 * @param lhs 左条目；可为 NULL（按"最小"处理）。
 * @param rhs 右条目；可为 NULL。
 * @return `lhs` 严格小于 `rhs` 返回 true。
 * @note  对齐 Qt `QAbstractItemModelPrivate::variantLessThan`
 *        （qabstractitemmodel.cpp:828-839）：
 *        ① 任一值为浮点 ⇒ 双精度数值比较；② 两侧均为整型族 ⇒ 64 位整数
 *        比较；③ 其他（含字符串、缺失值）⇒
 *        `QString::localeAwareCompare` 区域感知比较，缺失值的文本为空串。
 *        缺失 DisplayRole 的条目与"空串条目"同序（都最小）。
 */
bool XTableWidgetItem_lessThan(const XTableWidgetItem* lhs,
                               const XTableWidgetItem* rhs);

/* -------------------- 序列化（对齐 QTableWidgetItem::read/write） -------------------- */

/**
 * @brief 把条目全部状态写出到数据流（对齐 `QTableWidgetItem::write`）。
 *
 * @param self   目标条目；传入 NULL 返回 false。
 * @param stream 目标流（见 `XDataStream`）；传入 NULL 返回 false。
 * @return 全部字段写出成功返回 true；失败返回 false（流状态已置）。
 *
 * @note  线格式（对齐 Qt `QTableWidgetItem::write` 的字段序：
 *        role 列表 → type → flags，Qt 6.8 qtablewidget.cpp:1451-1470）：
 *        1. `quint32 roleCount`；
 *        2. 每个 role：`quint32 role` + 该 role 的 `XVariant` 序列化
 *           （`XVariant` 的流格式见 XDataStream/XVariant 集成，按
 *           「类型标签 + 负载」自描述）；
 *        3. `quint32 type`；
 *        4. `quint32 flags`。
 *        `selected` 位**不写出**（对齐 Qt：选中态由选择模型持有，
 *        不属于条目持久状态）。
 * @warning 本格式为**本库自有**二进制布局，不与 Qt QDataStream 的
 *          QVariant 编码互通（Qt 的 QVariant 流格式与 Qt 版本强绑定，
 *          跨版本亦不兼容，故不做字节级对位）。
 */
bool XTableWidgetItem_write(const XTableWidgetItem* self, XDataStream* stream);

/**
 * @brief 从数据流读回条目全部状态（对齐 `QTableWidgetItem::read`）。
 *
 * @param self   目标条目；传入 NULL 返回 false。原 role 数据与标志位
 *               被**整体替换**（不清 `m_view`：读取发生在条目已归属
 *               表格时不应解绑）。
 * @param stream 源流；传入 NULL 返回 false。
 * @return 成功返回 true；数据不足/损坏返回 false（流状态已置，条目
 *         保持调用前状态不变）。
 * @note  与 `XTableWidgetItem_write` 严格对称。
 */
bool XTableWidgetItem_read(XTableWidgetItem* self, XDataStream* stream);

/* ==========================================================================
 * XTableWidgetSelectionRange（对齐 Qt 6.8 QTableWidgetSelectionRange）
 * ========================================================================== */

/**
 * @brief 矩形选区（对齐 `QTableWidgetSelectionRange`）。
 * @details 缺省构造为「空范围」：`top=-1, left=-1, bottom=-2, right=-2`，
 *          此时 `rowCount()`/`columnCount()` 均为 0（对齐 Qt 缺省值）。
 */
typedef struct XTableWidgetSelectionRange
{
    int m_top;    /**< 起始行号（含）；缺省 -1。 */
    int m_left;   /**< 起始列号（含）；缺省 -1。 */
    int m_bottom; /**< 结束行号（含）；缺省 -2。 */
    int m_right;  /**< 结束列号（含）；缺省 -2。 */
} XTableWidgetSelectionRange;

/**
 * @brief 构造矩形选区（对齐 `QTableWidgetSelectionRange(top, left, bottom, right)`）。
 * @param top    起始行号（含）。
 * @param left   起始列号（含）。
 * @param bottom 结束行号（含）。
 * @param right  结束列号（含）。
 * @return 按值返回的选区结构（无堆资源，调用方无需释放）。
 */
XTableWidgetSelectionRange XTableWidgetSelectionRange_create(
    int top, int left, int bottom, int right);
/** @brief 选区起始行号（对齐 `topRow`）。 @param self 目标选区；可为 NULL。 @return 起始行；NULL 返回 -1。 */
int XTableWidgetSelectionRange_topRow(const XTableWidgetSelectionRange* self);
/** @brief 选区结束行号（对齐 `bottomRow`）。 @param self 目标选区；可为 NULL。 @return 结束行；NULL 返回 -1。 */
int XTableWidgetSelectionRange_bottomRow(const XTableWidgetSelectionRange* self);
/** @brief 选区起始列号（对齐 `leftColumn`）。 @param self 目标选区；可为 NULL。 @return 起始列；NULL 返回 -1。 */
int XTableWidgetSelectionRange_leftColumn(const XTableWidgetSelectionRange* self);
/** @brief 选区结束列号（对齐 `rightColumn`）。 @param self 目标选区；可为 NULL。 @return 结束列；NULL 返回 -1。 */
int XTableWidgetSelectionRange_rightColumn(const XTableWidgetSelectionRange* self);
/** @brief 选区跨行数（对齐 `rowCount`）。 @param self 目标选区；可为 NULL。 @return 行数；空范围或 NULL 返回 0。 */
int XTableWidgetSelectionRange_rowCount(const XTableWidgetSelectionRange* self);
/** @brief 选区跨列数（对齐 `columnCount`）。 @param self 目标选区；可为 NULL。 @return 列数；空范围或 NULL 返回 0。 */
int XTableWidgetSelectionRange_columnCount(const XTableWidgetSelectionRange* self);
/** @brief 选区相等比较（对齐 `operator==`）。 @param lhs 左选区。 @param rhs 右选区。 @return 四边均相等返回 true；任一为 NULL 时两侧同为 NULL 才返回 true。 */
bool XTableWidgetSelectionRange_equals(const XTableWidgetSelectionRange* lhs,
                                       const XTableWidgetSelectionRange* rhs);

/* ==========================================================================
 * XTableWidget
 * ========================================================================== */

/**
 * @brief 表格控件（对齐 Qt 6.8 `QTableWidget`）。
 *
 * @details 行×列二维条目存储 + 表头标签 + 当前单元格；继承
 *          XAbstractScrollArea 后滚动条随内容自动按需显示。
 *          条目对象由表格持有所有权：插入/移除行列、clear/clearContents
 *          与析构路径负责释放。
 */
typedef struct XTableWidget
{
    XTableView m_base;            /**< 基类成员；必须是第一个。 */
    /* 指针组（8B 对齐）。 */
    XTableWidgetItem*** m_cells;  /**< 单元格条目指针矩阵（行数组 × 列槽；元素可为 NULL）；对象拥有。 */
    XString** m_hHeaders;         /**< 水平表头标签（对象拥有；下标=列号；元素可为 NULL）。 */
    XString** m_vHeaders;         /**< 垂直表头标签（对象拥有；下标=行号；元素可为 NULL）。 */
    XAbstractItemModel* m_model;  /**< 内建数据模型桥（对象拥有；与单元格同步）。 */
    bool         m_headerSortEnabled; /**< 点表头即排序（对标 QTableView::setSortingEnabled；默认 false）。 */
    XTableWidgetItem* m_itemPrototype; /**< 原型条目（对象拥有；对齐 Qt setItemPrototype 接管所有权）。 */
    XTableWidgetCellWidget* m_cellWidgets; /**< 单元格挂载部件条目数组（部件对象拥有）。 */
    /* 行列数/容量/表头 px 收窄；selection/entered 依赖 -1/-2 哨兵语义。 */
    int16_t m_rows;               /**< 当前行数。 */
    int16_t m_columns;            /**< 当前列数。 */
    int16_t m_rowCapacity;        /**< 行指针数组容量。 */
    int16_t m_vHeaderCapacity;    /**< 垂直表头容量。 */
    int16_t m_selectionRow;       /**< 选中单元格行；-1=无（用于 itemSelectionChanged）。 */
    int16_t m_selectionColumn;    /**< 选中单元格列；-1=无。 */
    int16_t m_enteredRow;         /**< 上次发射 cellEntered 的行；-2=尚未进入任何单元格。 */
    int16_t m_enteredColumn;      /**< 上次发射 cellEntered 的列；-2=尚未进入。 */
    int16_t m_cellWidgetCount;    /**< 挂载条目数。 */
    int16_t m_cellWidgetCapacity; /**< 挂载条目数组容量。 */
    uint16_t m_headerHeight;      /**< 水平表头高度（像素）。 */
    uint16_t m_headerWidth;       /**< 垂直表头宽度（像素）。 */
    uint32_t m_vHeaderVisible : 1; /**< 垂直表头（行号列）可见；0=隐藏（单元格左移贴边）。 */
    uint32_t m_selectionChangedPending : 1; /**< 选区等待发射 itemSelectionChanged。 */
    uint32_t m_tableWidgetFlagsUnused : 30; /**< 保留位；调用方不得依赖。 */
} XTableWidget;

/** @brief 初始化并返回本类虚函数表。 @return 单例虚函数表指针；调用方不得释放。 */
XVtable* XTableWidget_class_init(void);

/**
 * @brief 初始化表格控件（栈对象/嵌入成员使用；对齐 `QTableWidget(QWidget*)`）。
 *
 * @param self   目标表格控件指针；传入 NULL 时函数不执行任何操作。
 * @param parent 父控件；可为 NULL（顶层控件）。借用，不转移所有权。
 * @param flags  窗口标志位组合。
 * @return 无。
 * @note  初始 0 行 0 列、无当前单元格、行高 24、表头 24/40、网格可见；
 *        内部滚动条策略 AsNeeded。
 */
void XTableWidget_init(XTableWidget* self, XWidget* parent, XWidgetFlags flags);

/**
 * @brief 堆上创建表格控件（对齐 `QTableWidget(QWidget*)` 构造）。
 *
 * @param memory 内存类型。
 * @param parent 父控件；可为 NULL。
 * @param flags  窗口标志位组合。
 * @return 新建表格指针，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 */
XTableWidget* XTableWidget_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags);

/**
 * @brief 堆上创建指定行列数的表格（对齐 `QTableWidget(int rows, int columns, QWidget*)`）。
 *
 * @param memory  内存类型。
 * @param rows    初始行数；负值按 0 处理。
 * @param columns 初始列数；负值按 0 处理。
 * @param parent  父控件；可为 NULL。
 * @param flags   窗口标志位组合。
 * @return 新建表格指针，由调用方以 `XClassDelete` 释放；分配失败返回 NULL。
 * @note  对齐 Qt：直接在构造时设定行列数，不触发 rowsInserted/columnsInserted
 *        等结构变更信号。
 */
XTableWidget* XTableWidget_create_size(XMemoryType memory, int rows,
                                       int columns, XWidget* parent,
                                       XWidgetFlags flags);

/** @brief 堆创建的默认内存类型包装。 @param parent 父控件。 @param flags 标志位。 @return 新建表格；失败 NULL。 */
#define XTableWidget_create(parent, flags) \
    XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/* ===== 尺寸（对齐 QTableWidget 行列 API） ===== */

/**
 * @brief 查询内建数据模型（对齐 `QTableWidget::model` 的内建模型语义）。
 * @param self 目标表格控件；可为 NULL。
 * @return 内建模型借用指针（归表格所有，禁止释放）；self 为 NULL 返回 NULL。
 */
XAbstractItemModel* XTableWidget_model(const XTableWidget* self);

/**
 * @brief 设置行数（对齐 `QTableWidget::setRowCount`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param rows 新行数；负值忽略（对齐 Qt：`if (rows < 0 || rc == rows) return;`）。
 * @return 无。
 * @note  扩大时新增空行（无条目）；缩小时删除尾部行及其条目（对齐 Qt：
 *        被删条目直接 delete）。表头条目随之增删。
 */
void XTableWidget_setRowCount(XTableWidget* self, int rows);

/**
 * @brief 显示/隐藏垂直表头（行号列）。
 * @param self 目标表格控件；可为 NULL。
 * @param visible true=显示行号列（缺省）；false=隐藏。
 * @return 无。
 * @details [对标 Qt] `QTableView::verticalHeader()->setVisible(false)`：
 *          隐藏后**行号列整列不占宽**（单元格左移贴边），而不是留一条
 *          空白的窄带。本实现此前把行号列画死在表体里（没有可见性开关），
 *          想要"纯数据表"外观的页面（如 HSF 首页设备表）只能留着 1/2/3
 *          行号列，与 Python 端的 `verticalHeader().setVisible(False)` 不一致。
 */
void XTableWidget_setVerticalHeaderVisible(XTableWidget* self, bool visible);

/**
 * @brief 查询行数（对齐 `QTableWidget::rowCount`）。
 * @param self 目标表格控件；可为 NULL。
 * @return 当前行数；self 为 NULL 返回 0。
 */
int XTableWidget_rowCount(const XTableWidget* self);

/**
 * @brief 设置列数（对齐 `QTableWidget::setColumnCount`）。
 *
 * @param self    目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param columns 新列数；负值忽略。
 * @return 无。
 * @note  扩大时每行补空槽；缩小时删除尾部列及其条目与表头条目。
 */
void XTableWidget_setColumnCount(XTableWidget* self, int columns);

/**
 * @brief 查询列数（对齐 `QTableWidget::columnCount`）。
 * @param self 目标表格控件；可为 NULL。
 * @return 当前列数；self 为 NULL 返回 0。
 */
int XTableWidget_columnCount(const XTableWidget* self);

/* ===== 行列增删（对齐 QTableWidget 同名槽） ===== */

/**
 * @brief 在 row 位置插入一行（对齐 `QTableWidget::insertRow`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row  插入位置（0=最前；> 行数时忽略，对齐 Qt 的入参校验）。
 * @return 无。
 * @note  原有行及其条目、行头条目、挂载部件、当前格与选区随之下移；
 *        新行为空（无条目），表头条目为 NULL。
 */
void XTableWidget_insertRow(XTableWidget* self, int row);

/**
 * @brief 在 column 位置插入一列（对齐 `QTableWidget::insertColumn`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param column 插入位置；> 列数时忽略。
 * @return 无。
 * @note  原有列及其条目、列头条目右移；新列为空槽。
 */
void XTableWidget_insertColumn(XTableWidget* self, int column);

/**
 * @brief 移除指定行（对齐 `QTableWidget::removeRow`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row  待移除行号；越界时忽略。
 * @return 无。
 * @note  该行全部条目与行头条目**被删除**（对齐 Qt，不返回给调用方）；
 *        后续行及其条目、挂载部件、当前格与选区前移。
 */
void XTableWidget_removeRow(XTableWidget* self, int row);

/**
 * @brief 移除指定列（对齐 `QTableWidget::removeColumn`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param column 待移除列号；越界时忽略。
 * @return 无。
 * @note  该列全部条目与列头条目被删除。
 */
void XTableWidget_removeColumn(XTableWidget* self, int column);

/* ===== 单元格条目（对齐 setItem / item / takeItem / itemPrototype） ===== */

/**
 * @brief 写入单元格条目（对齐 `QTableWidget::setItem`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row    目标行号。
 * @param column 目标列号。
 * @param item   条目对象；**所有权转移给表格**（对齐 Qt）。传 NULL 表示清空
 *               该格（等价 `delete takeItem(row, column)`）。
 * @return 无。
 * @warning 传入非 NULL 后调用方**不得**再释放该条目；同一 item 不得交给
 *          两个表格（对齐 Qt 的 `cannot insert an item that is already
 *          owned by another QTableWidget` 警告分支——本库在 item 已属其它
 *          表格时忽略本次调用并保持原格不变）。
 * @warning 若 `(row, column)` 越界，函数仍会把 item 的所属表格设为 self
 *          但**不存入表格**（对齐 Qt 父本行为：该条目不会再被表格释放）。
 */
void XTableWidget_setItem(XTableWidget* self, int row, int column,
                          XTableWidgetItem* item);

/**
 * @brief 读取单元格条目（对齐 `QTableWidget::item`）。
 *
 * @param self   目标表格控件；可为 NULL。
 * @param row    行号。
 * @param column 列号。
 * @return 条目借用指针（归表格所有，禁止释放，随行列结构调整/clear 失效）；
 *         该格为空、越界或 self 为 NULL 返回 NULL。
 */
XTableWidgetItem* XTableWidget_item(const XTableWidget* self, int row,
                                    int column);

/**
 * @brief 读取单元格条目（const 借用形态）。
 *
 * @param self   目标表格控件；可为 NULL。
 * @param row    行号。
 * @param column 列号。
 * @return 条目只读借用指针（禁止释放）；该格为空、越界或 self 为 NULL 返回 NULL。
 */
const XTableWidgetItem* XTableWidget_item_const(const XTableWidget* self,
                                                int row, int column);

/**
 * @brief 取出单元格条目（对齐 `QTableWidget::takeItem`）。
 *
 * @param self   目标表格控件；传入 NULL 时返回 NULL。
 * @param row    行号。
 * @param column 列号。
 * @return 被取出的条目，**所有权移交调用方**（须以 `XClassDelete` 释放）；
 *         该格为空或越界返回 NULL。
 * @note  取出后该格清空，条目的 `m_view` 置 NULL；发射 `itemChanged` 与
 *        `cellChanged`（对齐 Qt 经 dataChanged 桥转出）。
 */
XTableWidgetItem* XTableWidget_takeItem(XTableWidget* self, int row, int column);

/**
 * @brief 读取原型条目（对齐 `QTableWidget::itemPrototype`）。
 * @param self 目标表格控件；可为 NULL。
 * @return 原型条目借用指针（归表格所有，禁止释放）；未设置或 self 为 NULL 返回 NULL。
 */
XTableWidgetItem* XTableWidget_itemPrototype(const XTableWidget* self);

/**
 * @brief 设置原型条目（对齐 `QTableWidget::setItemPrototype`）。
 *
 * @param self      目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param prototype 原型条目；**所有权转移给表格**（对齐 Qt：旧原型被
 *                  delete，新原型由表格持有并在析构时释放）。可为 NULL
 *                  （清除原型）。
 * @return 无。
 * @warning 传入非 NULL 后调用方不得再释放该条目。
 * @note  对齐 Qt：原型不参与现有条目，只在后续按需「创建新条目」的路径
 *        （如 `XTableWidgetItem` 工厂）中用作克隆模板。
 */
void XTableWidget_setItemPrototype(XTableWidget* self,
                                   XTableWidgetItem* prototype);

/* ===== 单元格部件挂载（对齐 setCellWidget / cellWidget / removeCellWidget） ===== */

/**
 * @brief 在单元格上挂载部件（对齐 `QTableWidget::setCellWidget`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row    目标行号。
 * @param column 目标列号。
 * @param widget 部件；**所有权转移给表格**（对齐 Qt：reparent 到 viewport，
 *               替换/移除/析构时释放）。传 NULL 等价 `removeCellWidget`。
 * @return 无。
 * @warning 传入非 NULL 后调用方不得再释放该部件。
 * @note  目标格越界忽略；重复挂载会释放旧部件（对齐 Qt `deleteLater`）。
 *        行/列插入、移除、缩行缩列时挂载表随行号同步平移或解除对应条目；
 *        `clear()` 与 `clearContents()` 均解除全部挂载（对齐 Qt
 *        模型 reset 路径释放编辑器）。
 */
void XTableWidget_setCellWidget(XTableWidget* self, int row, int column,
                                XWidget* widget);

/**
 * @brief 读取单元格挂载部件（对齐 `QTableWidget::cellWidget`）。
 *
 * @param self   目标表格控件；可为 NULL。
 * @param row    行号。
 * @param column 列号。
 * @return 部件借用指针（归表格所有，禁止释放）；越界或未挂载返回 NULL。
 */
XWidget* XTableWidget_cellWidget(const XTableWidget* self, int row, int column);

/**
 * @brief 移除并释放单元格挂载部件（对齐 `QTableWidget::removeCellWidget`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row    行号。
 * @param column 列号。
 * @return 无。
 * @note  对齐 Qt（`setCellWidget(row, column, nullptr)`）：部件被释放而
 *        非仅解除关联。越界或该格无挂载为无操作。
 */
void XTableWidget_removeCellWidget(XTableWidget* self, int row, int column);

/* ===== 表头（对齐 *HeaderItem / *HeaderLabels） ===== */

/**
 * @brief 批量设置水平表头标签（对齐 `QTableWidget::setHorizontalHeaderLabels`）。
 *
 * @param self   目标表格控件；传入 NULL 或 labels 为 NULL 时函数不执行任何操作。
 * @param labels 标签字符串数组（UTF-8）；元素可为 NULL（按空串处理）。
 * @param count  标签个数；超出当前列数的部分仍写入表头存储但不参与绘制
 *               （对齐 Qt：只改 labels，不改列数）。
 * @return 无。
 */
void XTableWidget_setHorizontalHeaderLabels(XTableWidget* self,
                                            const char* const* labels,
                                            int count);

/**
 * @brief 批量设置垂直表头标签（对齐 `QTableWidget::setVerticalHeaderLabels`）。
 *
 * @param self   目标表格控件；传入 NULL 或 labels 为 NULL 时函数不执行任何操作。
 * @param labels 标签字符串数组（UTF-8）；元素可为 NULL（按空串处理）。
 * @param count  标签个数。
 * @return 无。
 */
void XTableWidget_setVerticalHeaderLabels(XTableWidget* self,
                                          const char* const* labels,
                                          int count);

/**
 * @brief 读取水平表头文本（对齐 `QTableWidget::horizontalHeaderItem` 的文本承载形态）。
 *
 * @param self   目标表格控件；可为 NULL。
 * @param column 列号。
 * @return 新建 XString，由调用方以 `XClassDelete` 释放。未显式设置时返回
 *         **`column + 1` 的 1 基列号文本**（对齐 Qt `QTableModel::headerData`：
 *         无 header item 时 DisplayRole 返回 `section + 1`）；column 越界或
 *         self 为 NULL 返回空串对象。
 */
XString* XTableWidget_horizontalHeaderItem(const XTableWidget* self,
                                           int column);

/**
 * @brief 读取垂直表头文本（对齐 `QTableWidget::verticalHeaderItem` 的文本承载形态）。
 *
 * @param self 目标表格控件；可为 NULL。
 * @param row  行号。
 * @return 新建 XString，由调用方以 `XClassDelete` 释放。未显式设置时返回
 *         **`row + 1` 的 1 基行号文本**（对齐 Qt）；row 越界或 self 为
 *         NULL 返回空串对象。
 */
XString* XTableWidget_verticalHeaderItem(const XTableWidget* self, int row);

/**
 * @brief 设置单个水平表头标签（对齐 `QTableWidget::setHorizontalHeaderItem`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param column 列号；负值忽略。
 * @param text   标签文本；NULL 等价清空该列标签。借用，不转移所有权。
 * @return 无。
 * @note  对齐 Qt：不创建 header item 以外的副作用，也不改变列数；越界列号
 *        直接忽略（qt 实现同样忽略越界 section）。
 */
void XTableWidget_setHorizontalHeaderItem(XTableWidget* self, int column,
                                          const XString* text);

/**
 * @brief 设置单个垂直表头标签（对齐 `QTableWidget::setVerticalHeaderItem`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row  行号；负值忽略。
 * @param text 标签文本；NULL 等价清空该行标签。
 * @return 无。
 */
void XTableWidget_setVerticalHeaderItem(XTableWidget* self, int row,
                                        const XString* text);

/**
 * @brief 取出水平表头文本（对齐 `QTableWidget::takeHorizontalHeaderItem`）。
 *
 * @param self   目标表格控件；传入 NULL 时返回 NULL。
 * @param column 列号。
 * @return 被取出的文本对象，**所有权移交调用方**（须以 `XClassDelete`
 *         释放）；越界或该列未设置文本返回 NULL。
 */
XString* XTableWidget_takeHorizontalHeaderItem(XTableWidget* self, int column);

/**
 * @brief 取出垂直表头文本（对齐 `QTableWidget::takeVerticalHeaderItem`）。
 *
 * @param self 目标表格控件；传入 NULL 时返回 NULL。
 * @param row  行号。
 * @return 被取出的文本对象，**所有权移交调用方**（须以 `XClassDelete`
 *         释放）；越界或该行未设置文本返回 NULL。
 */
XString* XTableWidget_takeVerticalHeaderItem(XTableWidget* self, int row);

/* ===== 清空（对齐 clear / clearContents） ===== */

/**
 * @brief 清空整个表格（对齐 `QTableWidget::clear`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @return 无。
 * @note  删除全部单元格条目与**水平/垂直表头条目**，解除全部单元格挂载
 *        部件，清空当前格与选区（对齐 Qt：先 `selectionModel()->clear()`
 *        再清模型）。行列数归零。
 * @note  对齐 Qt：**不**清除 `setSpan` 合并区间（只有 `clearSpans` 清除）。
 */
void XTableWidget_clear(XTableWidget* self);

/**
 * @brief 清空全部单元格内容（对齐 `QTableWidget::clearContents`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @return 无。
 * @note  删除全部单元格条目并解除挂载部件，清空当前格与选区；**行列数与
 *        表头条目保留**（对齐 Qt）。同样**不**清除合并区间。
 */
void XTableWidget_clearContents(XTableWidget* self);

/* ===== 当前格（对齐 setCurrentItem / setCurrentCell / current*） ===== */

/**
 * @brief 设置当前单元格（对齐 `QTableWidget::setCurrentCell`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param row    当前行号。
 * @param column 当前列号。
 * @return 无。
 * @note  越界行列会把当前格清为 -1/-1（对齐 Qt：无效索引清除当前项）；
 *        同格重设不发射信号。发射 `currentItemChanged`（仅当新旧至少一个
 *        条目存在）、`currentCellChanged` 与 `itemSelectionChanged`。
 *        选择命令按 Qt 视图缺省口径取 `ClearAndSelect`（selectionMode 为
 *        NoSelection 时取 `NoUpdate`）——本机 Qt 6.8.0 实测：默认视图
 *        setCurrentCell(1,1) 后 item(1,1)->isSelected()==true。需要显式
 *        命令时用 setCurrentCell_2。
 */
void XTableWidget_setCurrentCell(XTableWidget* self, int row, int column);

/**
 * @brief 设置当前单元格并按选择命令更新选区（对齐
 *        `QTableWidget::setCurrentCell(int,int,SelectionFlags)` 重载）。
 *
 *        语义（对齐 Qt 6.8：`d->selectionModel->setCurrentIndex(index,
 *        command)`，qtablewidget.cpp:2235-2239；本机实测
 *        setCurrentCell(1,1,ClearAndSelect) 后该格 isSelected()=true，
 *        NoUpdate 时选区不动）：NoUpdate 只移动当前格；
 *        Clear/ClearAndSelect 先清空既有选区再选中目标格；
 *        Select/Deselect/Toggle 并入/移出/翻转（幂等）；
 *        Rows/Columns 把目标格展开为整行/整列后施加命令。
 *
 * @param self    目标表格控件；NULL 无操作。
 * @param row     当前行号（越界 ⇒ 当前格清为 -1/-1，同 Qt 无效索引）。
 * @param column  当前列号。
 * @param command XItemSelectionModelSelectionFlag 位组合。
 * @return 无。
 */
void XTableWidget_setCurrentCell_2(XTableWidget* self, int row, int column,
                                   int command);

/**
 * @brief 查询当前单元格行号（对齐 `QTableWidget::currentRow`）。
 * @param self 目标表格控件；可为 NULL。
 * @return 当前行号；无当前单元格或 self 为 NULL 返回 -1。
 */
int XTableWidget_currentRow(const XTableWidget* self);

/**
 * @brief 查询当前单元格列号（对齐 `QTableWidget::currentColumn`）。
 * @param self 目标表格控件；可为 NULL。
 * @return 当前列号；无当前单元格或 self 为 NULL 返回 -1。
 */
int XTableWidget_currentColumn(const XTableWidget* self);

/**
 * @brief 读取当前单元格条目（对齐 `QTableWidget::currentItem`）。
 * @param self 目标表格控件；可为 NULL。
 * @return 条目借用指针（归表格所有，禁止释放）；当前格为空、无当前格或
 *         self 为 NULL 返回 NULL。
 */
XTableWidgetItem* XTableWidget_currentItem(const XTableWidget* self);

/**
 * @brief 把指定条目设为当前项（对齐 `QTableWidget::setCurrentItem`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param item 目标条目；必须是本表格的条目（`item->m_view == self`），否则
 *             整体忽略（对齐 Qt：非本表条目得到无效索引 → 清除当前项）。
 * @return 无。
 * @note  等价于 `setCurrentCell(item->row(), item->column())`。
 */
void XTableWidget_setCurrentItem(XTableWidget* self, const XTableWidgetItem* item);

/**
 * @brief 把指定条目设为当前项并按选择命令更新选区（对齐
 *        `QTableWidget::setCurrentItem(item, SelectionFlags)` 重载；
 *        qtablewidget.cpp:2205-2209）。
 *
 * @param self    目标表格控件；NULL 无操作。
 * @param item    目标条目（必须属于本表格；否则按无效索引处理：
 *                当前格清为 -1/-1，再施加命令）。
 * @param command XItemSelectionModelSelectionFlag 位组合。
 * @return 无。
 * @note  转发 setCurrentCell_2（同 Qt 的实现路径）。
 */
void XTableWidget_setCurrentItem_2(XTableWidget* self,
                                   const XTableWidgetItem* item, int command);

/* ===== 索引反查（对齐 row / column / indexFromItem / itemFromIndex） ===== */

/**
 * @brief 条目所在行反查（对齐 `QTableWidget::row`）。
 * @param self 目标表格控件；可为 NULL。
 * @param item 条目借用指针（必须来自本表格）。
 * @return 条目所在行；游离条目、外部位址或 self 为 NULL 返回 -1。
 */
int XTableWidget_row(const XTableWidget* self, const XTableWidgetItem* item);

/**
 * @brief 条目所在列反查（对齐 `QTableWidget::column`）。
 * @param self 目标表格控件；可为 NULL。
 * @param item 条目借用指针（必须来自本表格）。
 * @return 条目所在列；游离条目、外部位址或 self 为 NULL 返回 -1。
 */
int XTableWidget_column(const XTableWidget* self, const XTableWidgetItem* item);

/**
 * @brief 由条目反查所在格（对齐 `QTableWidget::indexFromItem` 的 (row,column) 承载）。
 * @param self   目标表格控件；可为 NULL。
 * @param item   条目借用指针。
 * @param column 列号输出；可为 NULL。未命中写 -1。
 * @return 行号；未命中（条目不属于本表格、越界、self 或 item 为 NULL）返回 -1。
 */
int XTableWidget_indexFromItem(const XTableWidget* self,
                               const XTableWidgetItem* item, int* column);

/**
 * @brief 由格反查条目（对齐 `QTableWidget::itemFromIndex` 的 (row,column) 承载）。
 * @param self   目标表格控件；可为 NULL。
 * @param row    行号。
 * @param column 列号。
 * @return 条目借用指针（归表格所有，禁止释放）；该格为空、越界或 self 为
 *         NULL 返回 NULL。
 */
XTableWidgetItem* XTableWidget_itemFromIndex(const XTableWidget* self,
                                             int row, int column);

/* ===== 编辑与持久编辑器（对齐 QTableWidget 的 item* 形态） ===== */

/**
 * @brief 触发条目编辑（对齐 `QTableWidget::editItem`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param item 目标条目；不属于本表格时整体忽略（对齐 Qt `edit(index)` 对
 *             无效索引的空操作）。
 * @return 无。
 */
void XTableWidget_editItem(XTableWidget* self, const XTableWidgetItem* item);

/**
 * @brief 打开条目持久编辑器（对齐 `QTableWidget::openPersistentEditor`）。
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param item 目标条目；不属于本表格时整体忽略。
 * @return 无。
 */
void XTableWidget_openPersistentEditor(XTableWidget* self,
                                       const XTableWidgetItem* item);

/**
 * @brief 关闭条目持久编辑器（对齐 `QTableWidget::closePersistentEditor`）。
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param item 目标条目；不属于本表格时整体忽略。
 * @return 无。
 */
void XTableWidget_closePersistentEditor(XTableWidget* self,
                                        const XTableWidgetItem* item);

/**
 * @brief 查询条目持久编辑器是否打开（对齐 `QTableWidget::isPersistentEditorOpen`）。
 * @param self 目标表格控件；可为 NULL。
 * @param item 目标条目；不属于本表格时返回 false。
 * @return 打开返回 true；未打开、条目不属于本表格或 self 为 NULL 返回 false。
 */
bool XTableWidget_isPersistentEditorOpen(const XTableWidget* self,
                                         const XTableWidgetItem* item);

/* ===== 排序与滚动（对齐 QTableWidget 同名） ===== */

/**
 * @brief 按列排序（对齐 `QTableWidget::sortItems`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param column 排序列号；越界时整体忽略（对齐 Qt：模型 sort 对越界列不重排）。
 * @param order  0 = 升序，非 0 = 降序。
 * @return 无。
 * @note  依据该列 `DisplayRole` 比较，整行（含条目指针与行头条目）一起移动；
 *        该列为空的行走「不可排序」组，保持原有相对顺序排在最后（对齐
 *        `QTableModel::sort`）。对齐 Qt：`verticalHeaderItems` **不**随之
 *        重排——行头标签留在原位（Qt 6.8 既有行为）。
 */
void XTableWidget_sortItems(XTableWidget* self, int column, int order);

/**
 * @brief 开关"点表头即排序"（对标 QTableView::setSortingEnabled）。
 * @details [Qt 口径] QTableView 在开启后把表头的 sectionClicked 连到
 *          sortByColumn ⇒ 单击表头按该列排序（方向由排序指示器决定）。
 *          本框架的排序落在 XTableWidget（它同时拥有 model 与单元格），
 *          故实现在 XTableWidget_setSortingEnabled 里接线。
 * @note 默认关闭：应用若自己定义"双击表头排序"（如 HSF 客户端历史/日志页），
 *       不要再开这个，否则单击与双击会各排一次。
 */
void XTableWidget_setSortingEnabled(XTableWidget* self, bool enable);

/**
 * @brief 滚动使指定条目可见（对齐 `QTableWidget::scrollToItem`）。
 *
 * @param self 目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param item 目标条目；不属于本表格时整体忽略。
 * @param hint 定位提示（XAbstractItemViewScrollHint）；传
 *             `XAbstractItemViewScrollHint_EnsureVisible` 即 Qt 缺省。
 * @return 无。
 */
void XTableWidget_scrollToItem(XTableWidget* self, const XTableWidgetItem* item,
                               int hint);

/* ===== 便捷查找/选择（对齐 QTableWidget 便捷族） ===== */

/**
 * @brief 查找文本匹配的单元格（对齐 `QTableWidget::findItems`）。
 *
 * @param self     目标表格控件；传入 NULL 时返回 0。
 * @param text     待查找文本（UTF-8）；NULL 按空串处理。
 * @param flags    匹配方式：1 = 精确相等（对齐 `Qt::MatchExactly`）；
 *                 0 = 包含子串（对齐 `Qt::MatchContains`）；其余值按 0 处理。
 * @param out      命中的条目指针输出数组（调用方提供存储空间）；可为 NULL
 *                 （只统计不写出）。元素为借用指针，禁止释放。
 * @param maxCount out 数组容量；<= 0 表示只统计不写出。
 * @return 命中总数（写出条数不超过 maxCount；返回值大于 maxCount 表示输出
 *         被截断）。
 * @note  对齐 Qt 遍历顺序：**列优先**——先输出第 0 列自顶向下的全部命中，
 *        再输出第 1 列，以此类推（qtablewidget.cpp:2424-2437）。
 */
int XTableWidget_findItems(const XTableWidget* self, const char* text,
                           int flags, XTableWidgetItem** out, int maxCount);

/**
 * @brief 坐标反查条目（对齐 `QTableWidget::itemAt`）。
 *
 * @param self 目标表格控件；可为 NULL。
 * @param x    横向坐标（控件本地，像素）。
 * @param y    纵向坐标（控件本地，像素）。
 * @return 命中格条目借用指针（归表格所有，禁止释放）；表头区、空格或越界
 *         返回 NULL。命中落在合并区间覆盖格时返回区间原点条目（对齐 Qt）。
 * @note 隐藏行/列占 0 尺寸、不参与命中（与绘制同源的隐藏感知几何，
 *       gap 160；经 XTableView_setColumnHidden/setRowHidden 下发的隐藏
 *       在本表绘制/命中/内容尺寸中生效）。
 */
XTableWidgetItem* XTableWidget_itemAt(const XTableWidget* self, int x, int y);

/**
 * @brief 输出全部选中条目（对齐 `QTableWidget::selectedItems`）。
 *
 * @param self     目标表格控件；传入 NULL 时返回 0。
 * @param out      选中条目指针输出数组（调用方提供存储空间）；可为 NULL
 *                 （只统计不写出）。元素为借用指针，禁止释放。
 * @param maxCount out 数组容量；<= 0 表示只统计不写出。
 * @return 选中条目总数（写出条数不超过 maxCount）。
 * @note  对齐 Qt：只返回**实际存在条目**的选中格（空格跳过），按选择模型
 *        的选中索引顺序（行主序）输出。
 */
int XTableWidget_selectedItems(const XTableWidget* self,
                               XTableWidgetItem** out, int maxCount);

/**
 * @brief 输出全部选中范围（对齐 `QTableWidget::selectedRanges`）。
 *
 * @param self     目标表格控件；传入 NULL 时返回 0。
 * @param out      选区结构输出数组（调用方提供存储空间）；可为 NULL
 *                 （只统计不写出）。元素按值存放，无所有权问题。
 * @param maxCount out 数组容量；<= 0 表示只统计不写出。
 * @return 选区总数（写出条数不超过 maxCount）。
 * @note  对逐格选中标记做逐行归并：同一行取最小/最大选中列为跨度，行号连续
 *        且跨度一致的范围合并为一条；当前跟踪选区未被标记覆盖时追加一条
 *        1x1 范围。行内选中列不连续时离散空洞并入 [最小,最大] 跨度
 *        （单个矩形无法表达离散列）。
 */
int XTableWidget_selectedRanges(const XTableWidget* self,
                                XTableWidgetSelectionRange* out,
                                int maxCount);

/**
 * @brief 范围选中标记（对齐 `QTableWidget::setRangeSelected`）。
 *
 * @param self   目标表格控件；传入 NULL 时函数不执行任何操作。
 * @param range  选区矩形；两角都必须落在有效行列范围内，否则**整体忽略**
 *               （对齐 Qt 的 `hasIndex` 双向校验）。可为 NULL（无操作）。
 * @param select true = 选中范围；false = 取消该范围选中。
 * @return 无。
 * @note  对齐 Qt：只做 `Select`/`Deselect`，**不会**清除既有选区。有实际
 *        变化时发射 `itemSelectionChanged`。
 */
void XTableWidget_setRangeSelected(XTableWidget* self,
                                   const XTableWidgetSelectionRange* range,
                                   bool select);

/**
 * @brief 条目可视矩形（对齐 `QTableWidget::visualItemRect`）。
 *
 * @param self 目标表格控件；可为 NULL。
 * @param item 目标条目；不属于本表格时返回空矩形。
 * @return 视口坐标矩形；未找到或 self/item 为 NULL 返回空矩形 (0,0,0,0)。
 */
XRect XTableWidget_visualItemRect(const XTableWidget* self,
                                  const XTableWidgetItem* item);

/**
 * @brief 逻辑行号 → 视觉行号（对齐 `QTableWidget::visualRow`）。
 *
 * @param self      目标表格控件；可为 NULL。
 * @param logicalRow 逻辑行号。
 * @return 视觉行号（经垂直表头 `visualIndex`）；负值或 self 为 NULL 返回 -1。
 */
int XTableWidget_visualRow(const XTableWidget* self, int logicalRow);

/**
 * @brief 逻辑列号 → 视觉列号（对齐 `QTableWidget::visualColumn`）。
 *
 * @param self         目标表格控件；可为 NULL。
 * @param logicalColumn 逻辑列号。
 * @return 视觉列号（经水平表头 `visualIndex`）；负值或 self 为 NULL 返回 -1。
 */
int XTableWidget_visualColumn(const XTableWidget* self, int logicalColumn);

/* ==================== 信号（仅返回自身地址；发射经 emitSignal） ==================== */

/** @brief itemPressed 信号地址（载荷：item 指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemPressed_signal(XTableWidget* self);
/** @brief itemClicked 信号地址（载荷：item 指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemClicked_signal(XTableWidget* self);
/** @brief itemDoubleClicked 信号地址（载荷：item 指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemDoubleClicked_signal(XTableWidget* self);
/** @brief itemActivated 信号地址（载荷：item 指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemActivated_signal(XTableWidget* self);
/** @brief itemEntered 信号地址（载荷：item 指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemEntered_signal(XTableWidget* self);
/** @brief itemChanged 信号地址（载荷：item 指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemChanged_signal(XTableWidget* self);
/** @brief currentItemChanged 信号地址（载荷：current、previous 条目指针）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_currentItemChanged_signal(XTableWidget* self);
/** @brief itemSelectionChanged 信号地址（无载荷）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_itemSelectionChanged_signal(XTableWidget* self);
/** @brief cellPressed 信号地址（载荷：row, column）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_cellPressed_signal(XTableWidget* self);
/** @brief cellClicked 信号地址（载荷：row, column）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_cellClicked_signal(XTableWidget* self);
/** @brief cellDoubleClicked 信号地址（载荷：row, column）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_cellDoubleClicked_signal(XTableWidget* self);
/** @brief cellActivated 信号地址（载荷：row, column）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_cellActivated_signal(XTableWidget* self);
/** @brief cellEntered 信号地址（载荷：row, column）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_cellEntered_signal(XTableWidget* self);
/** @brief cellChanged 信号地址（载荷：row, column）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_cellChanged_signal(XTableWidget* self);
/** @brief currentCellChanged 信号地址（载荷：currentRow、currentColumn、previousRow、previousColumn）。 @param self 目标表格控件。 @return 信号槽地址。 */
void* XTableWidget_currentCellChanged_signal(XTableWidget* self);

#endif /* XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XTABLEWIDGET_H */
