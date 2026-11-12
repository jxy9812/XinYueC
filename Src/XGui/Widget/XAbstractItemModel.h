/**
 * @file       XAbstractItemModel.h
 * @brief      XAbstractItemModel 条目模型基类（对标 Qt 6.8 QAbstractItemModel）。
 * @details    提供内存二维条目模型：行/列数、单元格文本读写、行列表头、
 *             数据变化信号。QModelIndex 以 (row,column) 平面索引表达（本库
 *             控件体系沿用 int 索引风格）；树形 parent/children 接口为后续
 *             XTreeView 预留（当前返回 -1 表示根）。XTableWidget 内建本模型，
 *             XTableView/XListView 等视图经 model() 读取数据。
 * @note       模块总开关 XTABLEWIDGET_ON；XAbstractItemModel 为 XObject 派生。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTITEMMODEL_H
#define XABSTRACTITEMMODEL_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XVector.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/**
 * @brief      条目标志位（对标 Qt 6.8 Qt::ItemFlag，数值一致）。
 */
typedef enum XItemFlag
{
    XItemFlag_NoItemFlags = 0,      /**< 无标志（对标 Qt::NoItemFlags）。 */
    XItemFlag_ItemIsSelectable = 1, /**< 可选中（对标 ItemIsSelectable）。 */
    XItemFlag_ItemIsEditable = 2,   /**< 可编辑（对标 ItemIsEditable）。 */
    XItemFlag_ItemIsDragEnabled = 4,   /**< 可拖出（对标 ItemIsDragEnabled）。 */
    XItemFlag_ItemIsDropEnabled = 8,   /**< 可接收拖放（对标 ItemIsDropEnabled）。 */
    XItemFlag_ItemIsUserCheckable = 16,/**< 可勾选（对标 ItemIsUserCheckable）。 */
    XItemFlag_ItemIsEnabled = 32,   /**< 可用（对标 ItemIsEnabled）。 */
    XItemFlag_ItemIsAutoTristate = 64, /**< 自动三态（对标 ItemIsAutoTristate）。 */
    XItemFlag_ItemNeverHasChildren = 128, /**< 永无子级（对标 ItemNeverHasChildren）。 */
    XItemFlag_ItemIsUserTristate = 256 /**< 用户三态（对标 ItemIsUserTristate）。 */
} XItemFlag;

/**
 * @brief      匹配标志（对标 Qt 6.8 Qt::MatchFlag，**数值逐项一致**；
 *             qnamespace.h:1530-1544）。
 *
 * @note       本库以 int 位组合承载（XAbstractItemModel_match 的 flags
 *             形参）。注意 Qt 6.8 的编号与 Qt5 不同：
 *             类型掩码 = flags & MatchTypeMask(0x0F)（Exactly=0/
 *             Contains=1/StartsWith=2/EndsWith=3/RegularExpression=4/
 *             Wildcard=5/FixedString=8）；MatchCaseSensitive=0x10、
 *             MatchWrap=0x20、MatchRecursive=0x40 是独立位。
 *             语义按 Qt 6.8 实现（qabstractitemmodel.cpp:2515-2600）：
 *             ① MatchExactly 按 **QVariant 相等**比较（对文本即区分
 *             大小写，不受 MatchCaseSensitive 影响）；
 *             ② 其余文本模式按 cs 比较，缺省**不区分大小写**；
 *             ③ 只扫描 start 所在**列**的各行，MatchWrap 从尾回绕；
 *             ④ MatchRegularExpression/MatchWildcard 需要正则子系统，
 *             本库无 XRegularExpression（对应 Qt QRegularExpression）
 *             ⇒ 传入时返回 0 条（不降级成子串匹配，避免给出错答案）。
 */
typedef enum XMatchFlag
{
    XMatchFlag_MatchExactly = 0,          /**< 全等（QVariant 相等）。 */
    XMatchFlag_MatchContains = 1,         /**< 包含（对标 MatchContains）。 */
    XMatchFlag_MatchStartsWith = 2,       /**< 前缀（对标 MatchStartsWith）。 */
    XMatchFlag_MatchEndsWith = 3,         /**< 后缀（对标 MatchEndsWith）。 */
    XMatchFlag_MatchRegularExpression = 4,/**< 正则（需正则子系统，未建）。 */
    XMatchFlag_MatchWildcard = 5,         /**< 通配（需正则子系统，未建）。 */
    XMatchFlag_MatchFixedString = 8,      /**< 定长串（对标 MatchFixedString）。 */
    XMatchFlag_MatchTypeMask = 0x0F,      /**< 匹配方式掩码（对标 MatchTypeMask）。 */
    XMatchFlag_MatchCaseSensitive = 0x10, /**< 区分大小写（位）。 */
    XMatchFlag_MatchWrap = 0x20,          /**< 到尾部后回绕（对标 MatchWrap）。 */
    XMatchFlag_MatchRecursive = 0x40      /**< 递归子树（平铺模型恒等）。 */
} XMatchFlag;

/**
 * @brief      布局变化提示（对标 Qt 6.8 QAbstractItemModel::LayoutChangeHint，
 *             **数值逐项一致**；qabstractitemmodel.h:334-339）。
 *
 * @note       与 Qt 同义：layoutAboutToBeChanged/layoutChanged 的提示位，
 *             视图据此选择「整体重排」还是「按列排序」的刷新路径。
 *             本库信号只承载 hint（Qt 另有 QList<QPersistentModelIndex>
 *             parents 形参，本库无持久索引对象，见 XAbstractItemModel.h
 *             信号注释）。
 */
typedef enum XLayoutChangeHint
{
    XLayoutChangeHint_NoLayoutChangeHint = 0,   /**< 无提示（对标
                                                     NoLayoutChangeHint）。 */
    XLayoutChangeHint_VerticalSortHint = 1,     /**< 竖向排序（对标
                                                     VerticalSortHint）。 */
    XLayoutChangeHint_HorizontalSortHint = 2    /**< 横向排序（对标
                                                     HorizontalSortHint）。 */
} XLayoutChangeHint;

/**
 * @brief      索引校验选项（对标 Qt 6.8 QAbstractItemModel::CheckIndexOption，
 *             **数值逐项一致**；qabstractitemmodel.h:342-347）。
 *
 * @note       位组合以 int 承载（与 XAbstractItemViewEditTrigger 同一写法），
 *             `checkIndex` 的 options 形参即本枚举按位或。
 */
typedef enum XCheckIndexOption
{
    XCheckIndexOption_NoOption = 0x0000,        /**< 只做基本校验（对标
                                                     NoOption）。 */
    XCheckIndexOption_IndexIsValid = 0x0001,    /**< 索引必须有效（对标
                                                     IndexIsValid）。 */
    XCheckIndexOption_DoNotUseParent = 0x0002,  /**< 跳过父链与区间校验（对标
                                                     DoNotUseParent）。 */
    XCheckIndexOption_ParentIsInvalid = 0x0004  /**< 父索引必须无效（对标
                                                     ParentIsInvalid）。 */
} XCheckIndexOption;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XAbstractItemModel)
/** @brief 条目标志虚槽（对标 QAbstractItemModel::flags 虚函数，
 *         qabstractitemmodel.h:321）：派生模型可重载本槽给出
 *        ItemIsEditable/ItemIsDropEnabled 等位；缺省实现见
 *        XAbstractItemModel_flags_base（Selectable|Enabled）。 */
XCLASS_DEFINE_ENUM(XAbstractItemModel, Flags) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XAbstractItemModel)

/**
 * @brief      条目模型对象；m_class 必须是第一个成员（嵌 XObject）。
 * @details    字段含义：
 *             - m_cells：单元格文本向量（行主序 m_rows*m_cols，元素
 *               XString*，对象拥有，可为 NULL）；
 *             - m_hHeader/m_vHeader：列头/行头文本向量（元素 XString*，
 *               对象拥有）；
 *             - m_rows/m_cols：模型维度。
 *             存储容量一律由 XVector 管理，对象不保留裸数组与容量字段。
 *             调用方不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractItemModel
{
    XObject m_class;    /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XVector m_cells;    /**< 单元格文本向量（行主序 m_rows*m_cols；
                             元素 XString*，对象拥有）。 */
    XVector m_hHeader;  /**< 列头文本向量（元素 XString*，对象拥有）。 */
    XVector m_vHeader;  /**< 行头文本向量（元素 XString*，对象拥有）。 */
    int     m_rows;     /**< 行数。 */
    int     m_cols;     /**< 列数。 */
} XAbstractItemModel;

/* ==================== 生命周期 ==================== */

/** @brief 初始化模型类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XAbstractItemModel_class_init(void);
/** @brief 初始化空模型（0 行 0 列）。 @param self 目标模型；不可为 NULL。 */
void XAbstractItemModel_init(XAbstractItemModel* self);
/** @brief 使用指定内存类型创建模型。
 * @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XAbstractItemModel* XAbstractItemModel_create_ex(XMemoryType memory);
#define XAbstractItemModel_create() \
    XAbstractItemModel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 维度 ==================== */

/** @brief 查询行数（对标 QAbstractItemModel::rowCount）。
 * @param self 目标模型；可为 NULL。
 * @return 当前行数；self 为 NULL 返回 0。
 */
int XAbstractItemModel_rowCount(const XAbstractItemModel* self);
/** @brief 查询列数（对标 columnCount）。
 * @param self 目标模型；可为 NULL。
 * @return 当前列数；self 为 NULL 返回 0。
 */
int XAbstractItemModel_columnCount(const XAbstractItemModel* self);
/** @brief 设置模型维度（扩容时新增单元格为空；缩容裁剪尾部）。
 *
 *        单元格按新维度**重排行主序**：列数变化时既有数据按 (row,col)
 *        搬到新行距位置（列 0 的数据不会因扩列被当成行 0 的列 1）。
 *        行数变化发射 rowsInserted/rowsRemoved，列数变化发射
 *        columnsInserted/columnsRemoved（对标 Qt 的结构变化信号，视图
 *        接 columnsInserted 的刷新槽才能在 setDimension 列变化路径响应）。
 *
 * @param self 目标模型。
 * @param rows 行数（>=0）。
 * @param cols 列数（>=0）。
 * @return 无返回值（维度变化时发射行/列结构变化信号）。
 */
void XAbstractItemModel_setDimension(XAbstractItemModel* self,
                                     int rows, int cols);
/* 【冗余 API 清理 2026-11-11】原 XAbstractItemModel_setRowCount /
 * XAbstractItemModel_setColumnCount 已删除：二者分别只是
 * setDimension(rows, 当前列数) / setDimension(当前行数, cols) 的纯包装；
 * Qt 6.8 的 QAbstractItemModel 没有单维 setRowCount/setColumnCount
 * （qabstractitemmodel.h 只有 insertRows/removeRows 结构操作，单维
 * setRowCount 属 QTableWidget/QTreeWidget 的便捷层）。调用点已迁到主
 * API XAbstractItemModel_setDimension（Test/QtAlign/
 * XAbstractItemModelAlignTest.c、Test/XGuiDemo/xgui_demo_apitest_views.c）。 */

/* ==================== 索引契约（对标 QAbstractItemModel index/hasIndex/sibling） ==================== */

/**
 * @brief 索引是否有效（对标 QAbstractItemModel::hasIndex，qabstractitemmodel.h:270）。
 *
 *        语义（对齐 Qt 6.8 实测）：row < 0 或 column < 0 直接无效；否则
 *        row < rowCount 且 column < columnCount 才有效。平面模型的
 *        parent 恒为根，故无需父索引参数（Qt 的 parent 形参在根处缺省）。
 *
 * @param self 目标模型；可为 NULL（NULL 一律无效）。
 * @param row 行号。
 * @param column 列号。
 * @return 有效返回 true。
 */
bool XAbstractItemModel_hasIndex(const XAbstractItemModel* self, int row,
                                 int column);
/**
 * @brief 规格化索引（对标 QAbstractItemModel::index(row, column, root)）。
 *
 *        Qt 返回 QModelIndex（无效索引的 row()/column() 均为 -1）；本库以
 *        (row,column) 平面承载，故以双输出表达：有效时原样回写，无效时
 *        输出 (-1,-1) 且返回 false（与 XModelIndex 无独立对象的库内口径
 *        一致，见 XAbstractItemView_currentIndex 的先例）。
 *
 * @param self 目标模型；可为 NULL。
 * @param row 行号。
 * @param column 列号。
 * @param outRow 输出行号（可空；无效置 -1）。
 * @param outCol 输出列号（可空；无效置 -1）。
 * @return 索引有效返回 true。
 */
bool XAbstractItemModel_index(const XAbstractItemModel* self, int row,
                              int column, int* outRow, int* outCol);
/**
 * @brief 兄弟索引（对标 QAbstractItemModel::sibling(row, column, idx)）。
 *
 *        Qt 语义：返回与 idx 同父、位于 (row,column) 的索引（缺省实现即
 *        index(row, column, parent(idx))）。平面模型的父恒为根 ⇒ 兄弟
 *        判定与 XAbstractItemModel_index 同源（fromRow/fromCol 仅作来源
 *        合法性说明，不参与计算）。
 *
 * @param self 目标模型；可为 NULL。
 * @param row 目标行。
 * @param column 目标列。
 * @param fromRow 来源索引行（借用 Qt 形参；平面模型不参与计算）。
 * @param fromCol 来源索引列（同上）。
 * @param outRow 输出行号（可空；无效置 -1）。
 * @param outCol 输出列号（可空；无效置 -1）。
 * @return 兄弟索引有效返回 true。
 */
bool XAbstractItemModel_sibling(const XAbstractItemModel* self, int row,
                                int column, int fromRow, int fromCol,
                                int* outRow, int* outCol);
/**
 * @brief 校验索引是否满足调用方声明的前置条件（对标
 *        QAbstractItemModel::checkIndex(index, options)，
 *        qabstractitemmodel.h:351）。
 *
 *        语义（逐条对齐 Qt 6.8 qabstractitemmodel.cpp:3586-3641）：
 *        ① 索引无效（row<0 或 column<0，等价 Qt 的 `!index.isValid()`）：
 *           带 IndexIsValid 返回 false，否则返回 true（Qt 同：无条件信任
 *           无效索引）；
 *        ② row<0 / column<0：返回 false；
 *        ③ 未带 DoNotUseParent 时：ParentIsInvalid 要求 parent 无效
 *           （本库平铺模型 `parent` 恒 -1，恒满足）；随后按父索引的
 *           rowCount/columnCount 做越界校验——本库平铺模型的父恒为根，
 *           即 `row < rowCount(self)` 且 `column < columnCount(self)`；
 *        ④ 带 DoNotUseParent 时**跳过** ③ 的父链与区间校验（Qt 同：
 *           区间校验整段位于 `if (!(options & DoNotUseParent))` 内）。
 *
 * @param self 目标模型；可为 NULL（索引一律视为无效）。
 * @param row 行号。
 * @param col 列号。
 * @param options XCheckIndexOption 位组合。
 * @return 满足全部声明的前置条件返回 true。
 */
bool XAbstractItemModel_checkIndex(const XAbstractItemModel* self, int row,
                                   int col, int options);

/* ==================== 数据 ==================== */

/** @brief 读取单元格文本（内部借用 XString*；不得释放）。
 * @param self 目标模型。
 * @param row 行（越界返回 NULL）。
 * @param col 列（越界返回 NULL）。
 * @return 借用 XString*；空单元格或越界返回 NULL。
 */
const XString* XAbstractItemModel_data(const XAbstractItemModel* self,
                                       int row, int col);
/** @brief 读取单元格文本（UTF-8 借用；越界或空返回空串）。
 * @param self 目标模型；可为 NULL。
 * @param row 行号（越界返回空串）。
 * @param col 列号（越界返回空串）。
 * @return 文本的 UTF-8 借用指针（内部转换缓存，不得释放或修改，随单元
 *         格下次修改失效）；空单元格、越界或 self 为 NULL 返回空串 ""。
 */
const char* XAbstractItemModel_data_2(const XAbstractItemModel* self,
                                      int row, int col);
/** @brief 写入单元格文本（XString 主版本；对标 QAbstractItemModel::setData）。
 * @param self 目标模型。
 * @param row 行（越界返回 false）。
 * @param col 列（越界返回 false）。
 * @param value 借用 XString*；可为 NULL（清空）。
 * @return 成功返回 true（并发射 dataChanged 的单元格区间：
 *         左上==右下==(row,col)，roles=Display|Edit）。
 */
bool XAbstractItemModel_setData(XAbstractItemModel* self, int row, int col,
                                const XString* value);
/** @brief 写入单元格文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标模型。
 * @param row 行号（越界返回 false）。
 * @param col 列号（越界返回 false）。
 * @param value UTF-8 文本借用指针（按 UTF-8 解码后深拷贝）；NULL 等价
 *        清空，函数返回后调用方仍可释放或修改源缓冲区。
 * @return 成功返回 true（并发射 dataChanged 单元格区间）；解码、内存分配
 *         失败或越界返回 false，此时原单元格内容保持不变。
 */
bool XAbstractItemModel_setData_2(XAbstractItemModel* self, int row, int col,
                                  const char* value);

/* ==================== 表头 ==================== */

/** @brief 读取表头文本（内部借用 XString*；不得释放）。
 * @param self 目标模型。
 * @param section 行列号（行头=行号、列头=列号；越界返回 NULL）。
 * @param orientation 方向（0=水平列头，1=垂直行头，对标 Qt::Orientation 数值）。
 * @return 借用 XString*；未设置或越界返回 NULL。
 */
const XString* XAbstractItemModel_headerData(const XAbstractItemModel* self,
                                             int section, int orientation);
/** @brief 读取表头文本（UTF-8 借用）。
 * @param self 目标模型；可为 NULL。
 * @param section 行列号（行头=行号、列头=列号；越界返回空串）。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @return 表头文本的 UTF-8 借用指针（内部转换缓存，不得释放或修改）；
 *         未设置、越界或 self 为 NULL 返回空串 ""。
 */
const char* XAbstractItemModel_headerData_2(const XAbstractItemModel* self,
                                            int section, int orientation);
/** @brief 设置表头文本（XString 主版本）。
 * @param self 目标模型。
 * @param section 行列号。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @param value 借用 XString*；可为 NULL（清空）。
 * @return 成功返回 true（并发射 headerDataChanged(orientation,section,
 *         section)，对标 Qt 表头数据变化驱动表头重绘）。
 */
bool XAbstractItemModel_setHeaderData(XAbstractItemModel* self, int section,
                                      int orientation, const XString* value);
/** @brief 设置表头文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标模型。
 * @param section 行列号（越界返回 false）。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @param value UTF-8 文本借用指针（按 UTF-8 解码后深拷贝）；NULL 等价
 *        清空，函数返回后调用方仍可释放或修改源缓冲区。
 * @return 成功返回 true；解码、内存分配失败或越界返回 false，此时原表头
 *         内容保持不变。
 */
bool XAbstractItemModel_setHeaderData_2(XAbstractItemModel* self, int section,
                                        int orientation, const char* value);

/* ==================== 树形预留 ==================== */

/** @brief 父索引（对标 QAbstractItemModel::parent；当前扁平模型恒为根 -1）。
 * @param self 目标模型。
 * @param row 子行。
 * @param col 子列。
 * @return 父行号；根或无父返回 -1。
 */
int XAbstractItemModel_parent(const XAbstractItemModel* self,
                              int row, int col);

/* ==================== 结构操作（对标 QAbstractItemModel） ==================== */

/** @brief 查询条目标志（对标 QAbstractItemModel::flags；**虚槽分派**）。
 *
 *        语义（对齐 Qt 6.8 基类缺省，qabstractitemmodel.h:321）：无效索引
 *        返回 NoItemFlags；有效索引返回 ItemIsSelectable|ItemIsEnabled
 *        （不携带基类缺省的 ItemIsDropEnabled——本库拖放基础设施未建）。
 *        可编辑位由**派生模型重载虚槽**给出（对标 Qt 派生模型在
 *        flags() 里并 ItemIsEditable，如 QStandardItemModel 实测 47=
 *        Selectable|Editable|DragEnabled|DropEnabled|Enabled）；重载方式：
 *        `XCLASS_DEFINE_ENUM` 继承本类虚表并在 class_init 里
 *        `XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_Flags, 函数)`。
 *        视图侧编辑门禁另有总闸 XAbstractItemView_itemsEditable（历史
 *        约定，与模型 flags 取并集）。
 *
 * @param self 目标模型；可为 NULL（NULL 一律 NoItemFlags）。
 * @param row 行号。
 * @param col 列号。
 * @return XItemFlag 位组合。
 */
int XAbstractItemModel_flags(const XAbstractItemModel* self, int row, int col);
/** @brief flags 的基类缺省实现（供派生模型重载后静态回调父类；
 *         非虚分派）。
 * @param self 目标模型；可为 NULL。
 * @param row 行号。
 * @param col 列号。
 * @return 无效索引 NoItemFlags；有效索引 Selectable|Enabled。
 */
int XAbstractItemModel_flags_base(const XAbstractItemModel* self, int row,
                                  int col);
/** @brief 查询模型是否有条目（对标 QAbstractItemModel::hasChildren 的
 *        扁平收敛：根索引下 行数、列数均大于 0 即有子级）。
 * @param self 目标模型；可为 NULL。
 * @return 有条目返回 true。
 */
bool XAbstractItemModel_hasChildren(const XAbstractItemModel* self);
/** @brief 指定父索引下是否有子条目（对标 hasChildren(index) 的虚函数，
 *        qabstractitemmodel.h:305）。
 *
 *        语义（对齐 Qt 基类缺省）：parent 无效 ⇒ 等价根查询
 *        XAbstractItemModel_hasChildren；parent 有效 ⇒ rowCount(parent) > 0
 *        ——本库为平铺模型（无层级），任何有效索引的 rowCount 恒 0，
 *        故返回 false（与 Qt 平铺模型上 hasChildren(子索引)==false 同）。
 *
 * @param self 目标模型；可为 NULL（false）。
 * @param row 父索引行号。
 * @param col 父索引列号。
 * @return 有子条目返回 true。
 */
bool XAbstractItemModel_hasChildrenAt(const XAbstractItemModel* self,
                                      int row, int col);
/** @brief 在 row 前插入 count 行（对标 QAbstractItemModel::insertRows；
 *        扁平模型的真实现，非虚占位）。
 *
 *        新行为空行；row 及其后行整体后移，单元格文本/表头随之搬移。
 *        成功后发射 rowsAboutToBeInserted/rowsInserted(first,last)（闭区间）。
 *
 * @param self 目标模型。
 * @param row 插入位置（0..rowCount；越界或 count<=0 返回 false）。
 * @param count 插入行数。
 * @return 成功返回 true。
 */
bool XAbstractItemModel_insertRows(XAbstractItemModel* self, int row,
                                   int count);
/** @brief 自 row 起移除 count 行（对标 QAbstractItemModel::removeRows）。
 *
 *        越界部分收敛为移除至模型末尾（本库收敛口径）；成功后发射
 *        rowsAboutToBeRemoved/rowsRemoved(first,last)（闭区间）。
 *
 * @param self 目标模型。
 * @param row 起始行号。
 * @param count 移除行数（<=0 返回 false）。
 * @return 实际移除成功返回 true。
 */
bool XAbstractItemModel_removeRows(XAbstractItemModel* self, int row,
                                   int count);
/** @brief 在 column 前插入 count 列（对标 insertColumns；行为同
 *        insertRows，成功后发射 columnsAboutToBeInserted/columnsInserted
 *        (first,last) 闭区间）。
 * @param self 目标模型。
 * @param column 插入位置（0..columnCount；越界或 count<=0 返回 false）。
 * @param count 插入列数。
 * @return 成功返回 true。
 */
bool XAbstractItemModel_insertColumns(XAbstractItemModel* self, int column,
                                      int count);
/** @brief 自 column 起移除 count 列（对标 removeColumns；行为同
 *        removeRows，成功后发射 columnsAboutToBeRemoved/columnsRemoved
 *        (first,last) 闭区间）。
 * @param self 目标模型。
 * @param column 起始列号。
 * @param count 移除列数（<=0 返回 false）。
 * @return 实际移除成功返回 true。
 */
bool XAbstractItemModel_removeColumns(XAbstractItemModel* self, int column,
                                      int count);

/* ---- 单行/单列便捷形态（对标 QAbstractItemModel::insertRow/removeRow/
 *      insertColumn/removeColumn，qabstractitemmodel.h:310-313；Qt 为
 *      insertRows(row,1,root) / removeRows(row,1,root) 的内联转发） ---- */

/** @brief 插入一行（对标 insertRow(row)：等价 insertRows(row,1)）。
 * @param self 目标模型。
 * @param row 插入位置（0..rowCount）。
 * @return 成功返回 true。
 */
bool XAbstractItemModel_insertRow(XAbstractItemModel* self, int row);
/** @brief 移除一行（对标 removeRow(row)：等价 removeRows(row,1)）。
 * @param self 目标模型。
 * @param row 起始行号。
 * @return 实际移除成功返回 true。
 */
bool XAbstractItemModel_removeRow(XAbstractItemModel* self, int row);
/** @brief 插入一列（对标 insertColumn(column)：等价 insertColumns(column,1)）。
 * @param self 目标模型。
 * @param column 插入位置（0..columnCount）。
 * @return 成功返回 true。
 */
bool XAbstractItemModel_insertColumn(XAbstractItemModel* self, int column);
/** @brief 移除一列（对标 removeColumn(column)：等价 removeColumns(column,1)）。
 * @param self 目标模型。
 * @param column 起始列号。
 * @return 实际移除成功返回 true。
 */
bool XAbstractItemModel_removeColumn(XAbstractItemModel* self, int column);
/** @brief 按列文本排序行（对标 QAbstractItemModel::sort；Qt 基类为空
 *        实现、由模型覆写，本内存模型给出现成排序）。
 *
 *        语义：按 column 列单元格文本（UTF-8，XStrcmp 字典序）对全部
 *        行重排（稳定交换排序，同 XTableWidget_sortItems 口径）；order
 *        0=升序（AscOrder）、非 0=降序。空单元格按空串参与比较。重排
 *        后发射 layoutChanged。
 *
 * @param self 目标模型。
 * @param column 排序列（越界忽略）。
 * @param order 0=升序，非 0=降序（对标 Qt::SortOrder 数值）。
 * @return 无返回值。
 */
void XAbstractItemModel_sort(XAbstractItemModel* self, int column, int order);

/* ==================== 增量取数（对标 QAbstractItemModel::canFetchMore/
 *      fetchMore，qabstractitemmodel.h:325-326） ==================== */

/** @brief 是否还有更多数据可取（对标 canFetchMore(parent)）。
 *
 *        基类缺省返回 false（Qt 的 QAbstractItemModel::canFetchMore 缺省
 *        亦 false，qabstractitemmodel.cpp）；本内存模型一次性持有全部
 *        单元格，无增量来源，恒 false。派生模型可按需重载（C 虚表子类）。
 *
 * @param self 目标模型；可为 NULL（false）。
 * @param row 父索引行号（-1=根；平铺模型不使用）。
 * @param col 父索引列号（-1=根）。
 * @return 还有更多数据返回 true。
 */
bool XAbstractItemModel_canFetchMore(const XAbstractItemModel* self,
                                     int row, int col);
/** @brief 取更多数据（对标 fetchMore(parent)）。
 *
 *        基类缺省为空操作（Qt 缺省实现不做任何事）；本内存模型无增量
 *        来源，恒空操作。
 *
 * @param self 目标模型；可为 NULL（空操作）。
 * @param row 父索引行号（-1=根）。
 * @param col 父索引列号（-1=根）。
 * @return 无返回值。
 */
void XAbstractItemModel_fetchMore(XAbstractItemModel* self, int row, int col);

/* ==================== 行/列移动（对标 moveRows/moveColumns） ==================== */

/** @brief 在模型内移动一段行（对标 QAbstractItemModel::moveRows，
 *        qabstractitemmodel.h:329）。
 *
 *        语义（对齐 Qt 6.8 基类实现）：源区间 [sourceRow, sourceRow+count)
 *        整体搬到 destinationChild 之前；destinationChild 的下标按**移动前**
 *        的模型解释（Qt 同款）。destinationChild == sourceRow 或
 *        sourceRow+count 是空操作 ⇒ 返回 false（Qt 返回 false 且不发信号）。
 *        成功后发射 rowsAboutToBeMoved/rowsMoved（first,last,destinationRow）。
 *
 * @param self 目标模型。
 * @param sourceRow 源起始行（>=0）。
 * @param count 移动行数（>0）。
 * @param destinationChild 目标位置（0..rowCount）。
 * @return 实际移动返回 true；越界/空操作返回 false。
 */
bool XAbstractItemModel_moveRows(XAbstractItemModel* self, int sourceRow,
                                 int count, int destinationChild);
/** @brief 移动一行（对标 QAbstractItemModel::moveRow(sourceRow,
 *        destinationChild)：等价 moveRows(sourceRow, 1, destinationChild)，
 *        qabstractitemmodel.h:329）。
 *
 *        目标位按**移动前**下标解释（Qt 同款）；目标 == 源行或源行+1
 *        为空操作 ⇒ false。成功后发射 rowsAboutToBeMoved/rowsMoved
 *        (sourceRow, sourceRow, destinationChild)。Qt 形参里的
 *        sourceParent/destinationParent 在本库平铺模型下恒为根，故省去
 *        （与 moveRows 省父索引同一口径）。
 *
 * @param self 目标模型。
 * @param sourceRow 源行（>=0）。
 * @param destinationChild 目标位置（0..rowCount）。
 * @return 实际移动返回 true；越界/空操作返回 false。
 */
bool XAbstractItemModel_moveRow(XAbstractItemModel* self, int sourceRow,
                                int destinationChild);
/** @brief 在模型内移动一段列（对标 moveColumns，语义同 moveRows）。
 *
 *        平铺模型按行主序存储：逐行把列块搬到新列位（整列数据随动）。
 *
 * @param self 目标模型。
 * @param sourceColumn 源起始列（>=0）。
 * @param count 移动列数（>0）。
 * @param destinationChild 目标位置（0..columnCount）。
 * @return 实际移动返回 true；越界/空操作返回 false。
 */
bool XAbstractItemModel_moveColumns(XAbstractItemModel* self, int sourceColumn,
                                    int count, int destinationChild);
/** @brief 移动一列（对标 QAbstractItemModel::moveColumn(sourceColumn,
 *        destinationChild)：等价 moveColumns(sourceColumn, 1,
 *        destinationChild)，qabstractitemmodel.h:331；Qt 形参的
 *        sourceParent/destinationParent 在平铺模型下恒为根，故省去）。
 *
 *        成功后发射 columnsAboutToBeMoved/columnsMoved(sourceColumn,
 *        sourceColumn, destinationChild)；空操作/越界返回 false。
 *
 * @param self 目标模型。
 * @param sourceColumn 源列（>=0）。
 * @param destinationChild 目标位置（0..columnCount）。
 * @return 实际移动返回 true；越界/空操作返回 false。
 */
bool XAbstractItemModel_moveColumn(XAbstractItemModel* self,
                                   int sourceColumn, int destinationChild);

/* ==================== 搜索（对标 QAbstractItemModel::match） ==================== */

/** @brief 按文本在模型中查找匹配项（对标 QAbstractItemModel::match）。
 *
 *        Qt 返回 QModelIndexList；本库以 (row,column) 平面承载，命中序号
 *        写入调用方数组（outRows/outCols 按命中序号对齐，均可为 NULL 表示
 *        只统计）。**扫描范围对齐 Qt 6.8**（qabstractitemmodel.cpp:2529-
 *        2538）：只扫 start 所在**列**，从 startRow 向下；MatchWrap 到尾
 *        后从第 0 行继续到 startRow-1（不跨列；平铺模型无子树，
 *        MatchRecursive 恒等）。
 *
 *        role 语义：DisplayRole(0)/EditRole(2) 走单元格文本（本模型
 *        Display/Edit 同通道，同 Qt 缺省模型）；其余 role 本模型无承载，
 *        返回 0 条（不伪造空匹配）。
 *
 *        匹配方式取 flags & MatchTypeMask(0x0F)：MatchExactly 按值相等
 *        （**区分大小写**，Qt 用 QVariant::operator==）；MatchContains/
 *        MatchStartsWith/MatchEndsWith/MatchFixedString 按 cs 比较，缺省
 *        不区分大小写（flags & MatchCaseSensitive 才区分）。
 *        MatchRegularExpression/MatchWildcard 需要正则子系统（本库无
 *        QRegularExpression 对应物）⇒ 返回 0 条。
 *
 * @param self 目标模型；可为 NULL（0）。
 * @param startRow 起始行（越界按 0 收敛）。
 * @param startCol 起始列（决定扫描列；越界按末列收敛）。
 * @param role XItemDataRole（DisplayRole/EditRole 生效）。
 * @param value 匹配文本（UTF-8 借用；NULL 按空串）。
 * @param flags XMatchFlag 位组合。
 * @param outRows 命中行号输出数组（借用；可 NULL）。
 * @param outCols 命中列号输出数组（借用；可 NULL）。
 * @param maxHits 输出容量（<=0 只统计）。
 * @return 命中总数（可能大于 maxHits 表示输出截断）。
 */
int XAbstractItemModel_match(const XAbstractItemModel* self, int startRow,
                             int startCol, int role, const char* value,
                             int flags, int* outRows, int* outCols,
                             int maxHits);

/* ==================== 其它基类缺省（对标 QAbstractItemModel 基类实现） ==================== */

/** @brief 条目跨行跨列尺寸（对标 QAbstractItemModel::span，缺省 (1,1)）。
 * @param self 目标模型；可为 NULL（输出 1,1 并返回 false）。
 * @param row 行号。
 * @param col 列号。
 * @param outRowSpan 输出行跨（可空；恒 1）。
 * @param outColSpan 输出列跨（可空；恒 1）。
 * @return 索引有效返回 true。
 */
bool XAbstractItemModel_span(const XAbstractItemModel* self, int row, int col,
                             int* outRowSpan, int* outColSpan);
/** @brief 提交编辑（对标 QAbstractItemModel::submit，缺省 true）。
 *
 *        基类无编辑暂存区，恒成功（Qt 缺省 submit 返回 true、revert 空
 *        操作，qabstractitemmodel.cpp）。
 *
 * @param self 目标模型；可为 NULL（false）。
 * @return true。
 */
bool XAbstractItemModel_submit(XAbstractItemModel* self);
/** @brief 放弃编辑（对标 QAbstractItemModel::revert，缺省空操作）。
 * @param self 目标模型；可为 NULL（空操作）。
 * @return 无返回值。
 */
void XAbstractItemModel_revert(XAbstractItemModel* self);
/** @brief 伙伴索引（对标 QAbstractItemModel::buddy，缺省无效索引）。
 *
 *        Qt 缺省返回无效 QModelIndex；本库以 (row,column) 双输出承载，
 *        无效时输出 (-1,-1) 并返回 false（同 XAbstractItemModel_index
 *        的无效约定）。
 *
 * @param self 目标模型；可为 NULL。
 * @param row 行号。
 * @param col 列号。
 * @param outRow 输出伙伴行（可空；无效置 -1）。
 * @param outCol 输出伙伴列（可空；无效置 -1）。
 * @return 有伙伴返回 true（基类恒 false）。
 */
bool XAbstractItemModel_buddy(const XAbstractItemModel* self, int row,
                              int col, int* outRow, int* outCol);

/* ==================== 重置协议（对标 beginResetModel/endResetModel） ==================== */

/** @brief 模型重置前置通知（对标 QAbstractItemModel::beginResetModel）。
 *
 *        发射 modelAboutToBeReset，随即 modelReset 由
 *        XAbstractItemModel_endResetModel 发射（Qt 的 begin/end 配对协议）。
 *        只发信号、不改数据（Qt 同：数据清理由具体模型在两步之间完成）。
 *
 * @param self 目标模型；可为 NULL（不发射）。
 * @return 无返回值。
 */
void XAbstractItemModel_beginResetModel(XAbstractItemModel* self);
/** @brief 模型重置后置通知（对标 QAbstractItemModel::endResetModel）。
 *
 *        发射 modelReset，视图清空缓存/当前索引（本库视图经
 *        modelReset 信号接刷新槽）。
 *
 * @param self 目标模型；可为 NULL（不发射）。
 * @return 无返回值。
 */
void XAbstractItemModel_endResetModel(XAbstractItemModel* self);

/* ==================== 信号（对标 QAbstractItemModel） ==================== */

/**
 * @brief dataChanged(topLeftRow,topLeftCol,bottomRightRow,bottomRightCol,
 *        roles) 信号（单元格数据变化时发射）。
 *
 *        载荷形状对齐 Qt 6.8（qabstractitemmodel.h:356）：
 *        `dataChanged(const QModelIndex &topLeft, const QModelIndex
 *        &bottomRight, const QList<int> &roles)` ⇒ 本库以 5 个 int 承载：
 *        左上 (row,col)、右下 (row,col)、角色位掩码。**前两个参数仍是
 *        左上角**，单格写入时右下==左上（既有按前两参读取的槽无需改动）。
 *
 *        roles 编码：位 N = 角色 N 变化（如 DisplayRole=0、EditRole=2 ⇒
 *        0x5）；0 表示“未指明角色”（对齐 Qt 空 roles 列表 = 全部角色）。
 *
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param topRow 变化区间左上角行号。
 * @param topCol 变化区间左上角列号。
 * @param bottomRow 变化区间右下角行号。
 * @param bottomCol 变化区间右下角列号。
 * @param roles 角色位掩码（见上）。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_dataChanged_signal(XAbstractItemModel* self,
                                            int topRow, int topCol,
                                            int bottomRow, int bottomCol,
                                            int roles);
/** @brief modelReset() 信号（模型整体重置后发射；对标 modelReset）。 */
void* XAbstractItemModel_modelReset_signal(XAbstractItemModel* self);
/** @brief modelAboutToBeReset() 信号（重置前置；对标 modelAboutToBeReset）。 */
void* XAbstractItemModel_modelAboutToBeReset_signal(XAbstractItemModel* self);
/** @brief layoutAboutToBeChanged(hint) 信号（布局重排前置；对标
 *        QAbstractItemModel::layoutAboutToBeChanged(parents, hint)，
 *        qabstractitemmodel.h:360）。
 *
 *        载荷为单 int：XLayoutChangeHint（Qt 的 parents 列表形参在本库
 *        无持久索引承载，省去；hint 即 Qt 的末位形参）。发射点：
 *        XAbstractItemModel_sort（NoLayoutChangeHint）、move 族
 *        （同传 NoLayoutChangeHint——Qt 的 moveRows/moveColumns 只发
 *        rowsMoved/columnsMoved，不发布局族信号，本库同）。
 *
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识
 *        （**不读取 hint**，XSignal 以单参 NULL 调用的既有契约）。
 * @param hint XLayoutChangeHint 取值。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_layoutAboutToBeChanged_signal(
    XAbstractItemModel* self, int hint);
/** @brief rowsAboutToBeInserted(first,last) 信号（插入前置）。
 * @note 载荷 (first,last) 为**闭区间**（Qt rowsAboutToBeInserted(parent,
 *       first, last)）；rowsInserted 同形。
 */
void* XAbstractItemModel_rowsAboutToBeInserted_signal(
    XAbstractItemModel* self, int first, int last);
/** @brief rowsAboutToBeRemoved(first,last) 信号（移除前置；闭区间）。 */
void* XAbstractItemModel_rowsAboutToBeRemoved_signal(
    XAbstractItemModel* self, int first, int last);
/** @brief columnsAboutToBeInserted(first,last) 信号（插入前置；闭区间）。 */
void* XAbstractItemModel_columnsAboutToBeInserted_signal(
    XAbstractItemModel* self, int first, int last);
/** @brief columnsAboutToBeRemoved(first,last) 信号（移除前置；闭区间）。 */
void* XAbstractItemModel_columnsAboutToBeRemoved_signal(
    XAbstractItemModel* self, int first, int last);
/** @brief rowsInserted(first,last) 信号（行插入后发射；闭区间）。
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param first 插入区间首行（含）。
 * @param last 插入区间末行（含）。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_rowsInserted_signal(XAbstractItemModel* self,
                                             int first, int last);
/** @brief rowsRemoved(first,last) 信号（行移除后发射；闭区间）。 */
void* XAbstractItemModel_rowsRemoved_signal(XAbstractItemModel* self,
                                            int first, int last);
/** @brief headerDataChanged(orientation,first,last) 信号（表头数据变化时
 *        发射；对标 QAbstractItemModel::headerDataChanged 信号，
 *        qabstractitemmodel.h:358）。
 *
 *        发射点：setHeaderData 落库成功后（first==last==section，单段
 *        口径）。QHeaderView 侧经 XHeaderView_setModel 接线本信号驱动
 *        表头重绘（对标 Qt 模型信号 → QHeaderView::headerDataChanged
 *        槽链路）。
 *
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param orientation 方向（0=水平列头，1=垂直行头）。
 * @param first 变化首段号。
 * @param last 变化末段号。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_headerDataChanged_signal(XAbstractItemModel* self,
                                                  int orientation, int first,
                                                  int last);
/** @brief columnsInserted(first,last) 信号（列插入后发射；闭区间）。 */
void* XAbstractItemModel_columnsInserted_signal(XAbstractItemModel* self,
                                                int first, int last);
/** @brief columnsRemoved(first,last) 信号（列移除后发射；闭区间）。 */
void* XAbstractItemModel_columnsRemoved_signal(XAbstractItemModel* self,
                                               int first, int last);
/** @brief rowsAboutToBeMoved(first,last,destinationRow) 信号（行移动前置；
 *        对标 rowsAboutToBeMoved(parent,start,end,destParent,destRow)）。
 * @note 本库无父索引（平铺模型），载荷省去两个 parent 参数（3 int）。
 */
void* XAbstractItemModel_rowsAboutToBeMoved_signal(XAbstractItemModel* self,
                                                   int first, int last,
                                                   int destinationRow);
/** @brief rowsMoved(first,last,destinationRow) 信号（行移动后发射）。 */
void* XAbstractItemModel_rowsMoved_signal(XAbstractItemModel* self, int first,
                                          int last, int destinationRow);
/** @brief columnsAboutToBeMoved(first,last,destinationColumn) 信号。 */
void* XAbstractItemModel_columnsAboutToBeMoved_signal(
    XAbstractItemModel* self, int first, int last, int destinationColumn);
/** @brief columnsMoved(first,last,destinationColumn) 信号。 */
void* XAbstractItemModel_columnsMoved_signal(XAbstractItemModel* self,
                                             int first, int last,
                                             int destinationColumn);
/** @brief layoutChanged(hint) 信号（条目布局重排后发射；对标
 *        QAbstractItemModel::layoutChanged(parents, hint)，
 *        qabstractitemmodel.h:359；载荷与发射点口径见
 *        layoutAboutToBeChanged(hint)）。
 * @param self 目标模型；可为 NULL，NULL 时不发射、仅返回信号标识。
 * @param hint XLayoutChangeHint 取值。
 * @return 信号标识借用指针（内部存储，不得释放、不得解引用）。
 */
void* XAbstractItemModel_layoutChanged_signal(XAbstractItemModel* self,
                                              int hint);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XABSTRACTITEMMODEL_H */
