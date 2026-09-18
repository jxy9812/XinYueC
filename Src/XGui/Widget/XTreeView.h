/**
 * @file       XTreeView.h
 * @brief      XTreeView 树视图（对标 Qt 6.8 QTreeView）。
 * @details    以 m_base 组合继承 XAbstractItemView；当前模型为扁平表格，
 *             树渲染按第一列绘制（层次缩进预留 indentation）；支持表头
 *             隐藏、缩进宽度、树展开族（expand/collapse 状态以平行数组
 *             按行承载）、列隐藏/列宽状态。树条目由 XTreeWidget 提供扩展。
 * @note       模块总开关 XTABLEWIDGET_ON；XTreeView→XAbstractItemView。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTREEVIEW_H
#define XTREEVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGeometry.h"
#include "XAbstractItemView.h"
#include "XHeaderView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTreeView)
XCLASS_DEFINE_EXTEND_END(XTreeView, XAbstractItemView)

/** @brief 树视图对象；m_base 必须是第一个成员（嵌 XAbstractItemView）。 */
typedef struct XTreeView
{
    XAbstractItemView m_base;   /**< 基类成员；必须是第一个。 */
    int m_indentation;          /**< 缩进宽度（像素，默认 20）。 */
    bool m_headerHidden;        /**< 表头隐藏（默认 false）。 */
    int m_rowHeight;            /**< 行高（像素，默认 24）。 */
    bool* m_expanded;           /**< 行展开状态表（平行数组；对象拥有）。 */
    int m_rowStateCount;        /**< 行展开状态表长度（随模型行数同步）。 */
    bool* m_columnHidden;       /**< 列隐藏状态表（平行数组；对象拥有）。 */
    int* m_columnWidths;        /**< 列宽表（平行数组；0=自动铺满）。 */
    int m_columnStateCount;     /**< 列状态表长度。 */
    bool m_expandsOnDoubleClick; /**< 双击切换展开（默认 true）。 */
    bool m_itemsExpandable;      /**< 条目可展开（默认 true）。 */
    bool m_rootIsDecorated;      /**< 绘制展开控件列（默认 true）。 */
    bool m_sortingEnabled;       /**< 排序使能（默认 false）。 */
    bool m_uniformRowHeights;    /**< 等高行（默认 false）。 */
    int m_sortColumn;            /**< 排序列（-1=未设置；默认 -1）。 */
    int m_sortOrder;             /**< 排序方向（0=升序，1=降序；默认 0）。 */
    bool m_selectionRectVisible; /**< 拖拽橡皮筋选区可见（默认 false）。 */
    bool m_allColumnsShowFocus;  /**< 焦点框横贯全部列（默认 false）。 */
    int m_treePosition;          /**< 树位置列（-1=跟随视觉第 0 列；默认 0）。 */
    bool* m_rowHidden;           /**< 行隐藏状态表（平行数组；对象拥有）。 */
    int m_rowHiddenCount;        /**< 行隐藏状态表长度（随模型行数同步）。 */
    int m_autoExpandDelay;       /**< 悬停自动展开延时（毫秒；-1=禁用；默认 -1）。 */
    bool m_animated;             /**< 展开/收起动画开关（默认 false）。 */
    bool m_wordWrap;             /**< 条目文本自动换行（默认 false）。 */
    XHeaderView* m_header;       /**< 表头对象借用指针（不拥有；默认 NULL）。 */
} XTreeView;

/* ==================== 生命周期 ==================== */

XVtable* XTreeView_class_init(void);
/** @brief 初始化树视图。
 * @param self 目标视图；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XTreeView_init(XTreeView* self, XWidget* parent, XWidgetFlags flags);
/** @brief 使用指定内存类型创建树视图。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XTreeView* XTreeView_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
#define XTreeView_create(parent, flags) \
    XTreeView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XTreeView_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XTreeView_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QTreeView） ==================== */

/** @brief 设置缩进宽度。
 * @param self 目标视图。
 * @param indentation 缩进（像素；>=0）。
 * @return 无返回值。
 */
void XTreeView_setIndentation(XTreeView* self, int indentation);
/** @brief 查询缩进宽度。 @param self 目标视图。 @return 缩进。 */
int XTreeView_indentation(const XTreeView* self);
/** @brief 恢复缩进宽度缺省值（对标 resetIndentation）。
 * @param self 目标视图。
 * @return 无返回值（状态变化时请求一次重绘）。
 * @note Qt 恢复为样式/字体测算的缺省值；本库缺省值为固定常量 20 像素
 *       （与 init 缺省一致），本函数将缩进复位为该值。
 */
void XTreeView_resetIndentation(XTreeView* self);
/** @brief 设置表头隐藏。
 * @param self 目标视图。
 * @param hidden true 隐藏。
 * @return 无返回值。
 * @note 已挂接表头对象（setHeader）时同步镜像其可见性（对标 Qt 中
 *       headerHidden 即表头的 setHidden 状态承载）。
 */
void XTreeView_setHeaderHidden(XTreeView* self, bool hidden);
/** @brief 查询表头隐藏。 @param self 目标视图。 @return 隐藏返回 true。 */
bool XTreeView_isHeaderHidden(const XTreeView* self);
/** @brief 设置行高（项目库扩展：统一行高像素）。
 * @param self 目标视图。
 * @param height 行高（像素；>0）。
 * @return 无返回值。
 */
void XTreeView_setRowHeight(XTreeView* self, int height);
/** @brief 查询行高。 @param self 目标视图。 @return 行高。 */
int XTreeView_rowHeight(const XTreeView* self);

/* ==================== 树展开族（对标 QTreeView） ==================== */

/** @brief 展开指定行（对标 expand；状态由折叠变展开时发射 expanded(row)）。
 * @param self 目标视图。
 * @param row 行号（越界忽略）。
 * @return 无返回值。
 */
void XTreeView_expand(XTreeView* self, int row);
/** @brief 收起指定行（对标 collapse；状态由展开变折叠时发射 collapsed(row)）。
 * @param self 目标视图。
 * @param row 行号（越界忽略）。
 * @return 无返回值。
 */
void XTreeView_collapse(XTreeView* self, int row);
/** @brief 查询行展开状态（对标 isExpanded）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 已展开返回 true；越界或未同步返回 false（默认折叠）。
 */
bool XTreeView_isExpanded(const XTreeView* self, int row);
/** @brief 设置行展开状态（对标 setExpanded；变化时发射 expanded/collapsed）。
 * @param self 目标视图。
 * @param row 行号（越界忽略）。
 * @param expand true 展开，false 收起。
 * @return 无返回值。
 */
void XTreeView_setExpanded(XTreeView* self, int row, bool expand);
/** @brief 展开全部行（对标 expandAll；不对单行发射 expanded，对标 Qt）。
 * @param self 目标视图。
 * @return 无返回值。
 */
void XTreeView_expandAll(XTreeView* self);
/** @brief 收起全部行（对标 collapseAll；不对单行发射 collapsed，对标 Qt）。
 * @param self 目标视图。
 * @return 无返回值。
 */
void XTreeView_collapseAll(XTreeView* self);
/** @brief 展开到指定深度（对标 expandToDepth）。
 * @param self 目标视图。
 * @param depth 目标深度（本参数在行模型下不参与计算）。
 * @return 无返回值。
 * @note 当前为扁平行模型、无父子层次，语义简化为全部行置为展开。
 */
void XTreeView_expandToDepth(XTreeView* self, int depth);
/** @brief 递归展开指定行及其全部子层级（对标 expandRecursively）。
 * @param self 目标视图。
 * @param row 目标行号（本参数在平铺模型下不参与计算）。
 * @return 无返回值。
 * @note 当前为扁平行模型、无父子层次，任意行递归展开等效于展开全部行
 *       （expandAll）；批量置位不逐行发射 expanded，对标 Qt。
 */
void XTreeView_expandRecursively(XTreeView* self, int row);
/** @brief 设置双击切换展开（对标 setExpandsOnDoubleClick；默认 true）。
 * @param self 目标视图。
 * @param enable true 允许双击切换。
 * @return 无返回值。
 */
void XTreeView_setExpandsOnDoubleClick(XTreeView* self, bool enable);
/** @brief 查询双击切换展开（对标 expandsOnDoubleClick）。
 * @param self 目标视图。
 * @return 允许返回 true。
 */
bool XTreeView_expandsOnDoubleClick(const XTreeView* self);
/** @brief 设置条目可展开（对标 setItemsExpandable；默认 true）。
 * @param self 目标视图。
 * @param enable true 可展开。
 * @return 无返回值。
 * @note 当前扁平行模型无交互展开入口，本接口为状态存取（渲染分支
 *       判定使用），交互接线由派生 XTreeWidget 决定。
 */
void XTreeView_setItemsExpandable(XTreeView* self, bool enable);
/** @brief 查询条目可展开（对标 itemsExpandable）。
 * @param self 目标视图。
 * @return 可展开返回 true。
 */
bool XTreeView_itemsExpandable(const XTreeView* self);
/** @brief 设置绘制根级展开控件列（对标 setRootIsDecorated；默认 true）。
 * @param self 目标视图。
 * @param show true 绘制 +/- 展开控件。
 * @return 无返回值。
 */
void XTreeView_setRootIsDecorated(XTreeView* self, bool show);
/** @brief 查询根级展开控件列（对标 rootIsDecorated）。
 * @param self 目标视图。
 * @return 绘制返回 true。
 */
bool XTreeView_rootIsDecorated(const XTreeView* self);
/** @brief 设置排序使能（对标 setSortingEnabled；默认 false）。
 * @param self 目标视图。
 * @param enable true 使能。
 * @return 无返回值。
 * @note 当前扁平行模型未接表头点击排序，本接口为状态存取。
 */
void XTreeView_setSortingEnabled(XTreeView* self, bool enable);
/** @brief 查询排序使能（对标 isSortingEnabled）。
 * @param self 目标视图。
 * @return 使能返回 true。
 */
bool XTreeView_isSortingEnabled(const XTreeView* self);
/** @brief 设置等高行优化（对标 setUniformRowHeights；默认 false）。
 * @param self 目标视图。
 * @param uniform true 声明全部行等高。
 * @return 无返回值。
 * @note 本库渲染恒为统一行高（m_rowHeight），本接口为状态存取。
 */
void XTreeView_setUniformRowHeights(XTreeView* self, bool uniform);
/** @brief 查询等高行优化（对标 uniformRowHeights）。
 * @param self 目标视图。
 * @return 等高返回 true。
 */
bool XTreeView_uniformRowHeights(const XTreeView* self);

/* ==================== 索引导航族（对标 QTreeView indexAbove/indexBelow） ==================== */

/** @brief 查询视觉上方的相邻行（对标 indexAbove）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 上一行行号；self 为 NULL、行越界或无上行（row==0）返回 -1。
 * @note 当前为扁平行模型、无父子层次，"上方"即平铺行序的 row-1（同
 *       visualRect 坐标系，不考虑列序与展开状态）。
 */
int XTreeView_indexAbove(const XTreeView* self, int row);
/** @brief 查询视觉下方的相邻行（对标 indexBelow）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 下一行行号；self 为 NULL、行越界或无下行（末行）返回 -1。
 * @note 当前为扁平行模型、无父子层次，"下方"即平铺行序的 row+1。
 */
int XTreeView_indexBelow(const XTreeView* self, int row);

/* ==================== 列状态族（对标 QTreeView/QTreeWidget） ==================== */

/** @brief 设置列隐藏（对标 setColumnHidden）。
 * @param self 目标视图。
 * @param column 列号（<0 忽略）。
 * @param hide true 隐藏。
 * @return 无返回值。
 */
void XTreeView_setColumnHidden(XTreeView* self, int column, bool hide);
/** @brief 查询列隐藏（对标 isColumnHidden）。
 * @param self 目标视图。
 * @param column 列号。
 * @return 隐藏返回 true；越界返回 false。
 */
bool XTreeView_isColumnHidden(const XTreeView* self, int column);
/** @brief 设置列宽（对标 setColumnWidth；0 表示自动铺满）。
 * @param self 目标视图。
 * @param column 列号（<0 忽略）。
 * @param width 宽度（像素；<0 忽略，0 恢复自动）。
 * @return 无返回值。
 */
void XTreeView_setColumnWidth(XTreeView* self, int column, int width);
/** @brief 查询列宽（对标 columnWidth）。
 * @param self 目标视图。
 * @param column 列号。
 * @return 宽度（像素）；未设置或越界返回 0（自动铺满）。
 */
int XTreeView_columnWidth(const XTreeView* self, int column);
/** @brief 隐藏指定列（对标 hideColumn；转发 setColumnHidden(column,true)）。
 * @param self 目标视图。
 * @param column 列号（<0 忽略）。
 * @return 无返回值。
 */
void XTreeView_hideColumn(XTreeView* self, int column);
/** @brief 显示指定列（对标 showColumn；转发 setColumnHidden(column,false)）。
 * @param self 目标视图。
 * @param column 列号（<0 忽略）。
 * @return 无返回值。
 */
void XTreeView_showColumn(XTreeView* self, int column);
/** @brief 按内容调整指定列宽（对标 resizeColumnToContents）。
 * @param self 目标视图。
 * @param column 列号（当前不参与计算）。
 * @return 无返回值。
 * @note 简化承载：内容宽度测算未接（同 XHeaderView 的
 *       ResizeToContents 模式），本接口仅调度一次全量重绘，列宽保持
 *       不变；精确测算由派生 XTreeWidget 接线。
 */
void XTreeView_resizeColumnToContents(XTreeView* self, int column);

/* ==================== 排序族（对标 QTreeView） ==================== */

/** @brief 按列排序（对标 sortByColumn；状态承载）。
 * @param self 目标视图。
 * @param column 排序列（-1 表示恢复自然顺序并清除排序列；<-1 忽略）。
 * @param order 排序方向（0=升序，1=降序，对标 Qt::SortOrder）。
 * @return 无返回值。
 * @note 当前扁平行模型未接模型排序本体与表头指示器联动，本接口仅记录
 *       排序列与方向并触发重绘，供派生控件读取；不发射任何信号
 *       （对标 Qt 在未使能 sortingEnabled 时同步 sort 的行为此处降级）。
 */
void XTreeView_sortByColumn(XTreeView* self, int column, int order);
/** @brief 查询排序列（对标 sortColumn）。
 * @param self 目标视图。
 * @return 排序列；未设置或 self 为 NULL 返回 -1。
 */
int XTreeView_sortColumn(const XTreeView* self);
/** @brief 查询排序方向（对标 QHeaderView::sortIndicatorOrder 的视图侧承载）。
 * @param self 目标视图。
 * @return 排序方向（0=升序，1=降序）；self 为 NULL 返回 0（升序）。
 */
int XTreeView_sortIndicatorOrder(const XTreeView* self);

/* ==================== 几何查询族（行高/列宽反推，同 indexAt 口径） ==================== */

/** @brief 查询行 row 与列 column 的视口几何矩形（对标 visualRect）。
 * @param self 目标视图。
 * @param row 行号（<0 返回空矩形；不按模型行数裁剪，与 indexAt 同口径）。
 * @param column 列号（越界或隐藏列返回空矩形）。
 * @return 视口坐标下的单元格矩形；无效入参返回 {0,0,0,0}。
 * @note 与 indexAt 同口径：y 向扣除表头高度、行高取统一行高；x 向按可见
 *       列累计列宽，未显式设宽的列按“自动铺满”均摊剩余视口宽度。
 */
XRect XTreeView_visualRect(const XTreeView* self, int row, int column);
/** @brief 查询列在视口中的水平起始位置（对标 columnViewportPosition）。
 * @param self 目标视图。
 * @param column 列号。
 * @return 起始坐标（像素）；self 为 NULL 或列号越界返回 -1。
 * @note 口径同 visualRect 的 x 向累计：从第 0 列起按可见列累计生效列宽
 *       （显式列宽优先，未设宽列取自动均摊份额），隐藏列跳过（宽度按 0
 *       计）；目标列自身隐藏时返回其 0 宽度位置（同 Qt 隐藏段语义）。
 */
int XTreeView_columnViewportPosition(const XTreeView* self, int column);
/** @brief 位置反查行号（对标 QTableView::rowAt，基于统一行高）。
 * @param self 目标视图。
 * @param y 视口坐标 y（像素）。
 * @return 行号；self 为 NULL、负坐标或命中表头区域返回 -1。
 */
int XTreeView_rowAt(const XTreeView* self, int y);
/** @brief 位置反查列号（对标 QTableView::columnAt，按可见列累计宽度）。
 * @param self 目标视图。
 * @param x 视口坐标 x（像素）。
 * @return 列号；self 为 NULL、负坐标、无模型或超出全部可见列范围返回 -1。
 * @note 列宽分摊口径同 visualRect：显式列宽优先，其余可见列均摊剩余
 *       视口宽度。
 */
int XTreeView_columnAt(const XTreeView* self, int x);

/* ==================== 视图状态族（对标 QTreeView/QAbstractItemView） ==================== */

/** @brief 设置拖拽橡皮筋选区是否可见（对标 setSelectionRectVisible；默认 false）。
 * @param self 目标视图。
 * @param visible true 显示橡皮筋选区。
 * @return 无返回值。
 * @note 当前扁平行模型未接拖拽橡皮筋交互，本接口为状态存取。
 */
void XTreeView_setSelectionRectVisible(XTreeView* self, bool visible);
/** @brief 查询拖拽橡皮筋选区可见（对标 isSelectionRectVisible）。
 * @param self 目标视图。
 * @return 可见返回 true。
 */
bool XTreeView_isSelectionRectVisible(const XTreeView* self);
/** @brief 设置焦点框横贯全部列（对标 setAllColumnsShowFocus；默认 false）。
 * @param self 目标视图。
 * @param enable true 焦点框横贯全部列，否则仅单列。
 * @return 无返回值。
 */
void XTreeView_setAllColumnsShowFocus(XTreeView* self, bool enable);
/** @brief 查询焦点框横贯全部列（对标 allColumnsShowFocus）。
 * @param self 目标视图。
 * @return 横贯全部列返回 true。
 */
bool XTreeView_allColumnsShowFocus(const XTreeView* self);
/** @brief 设置树位置列（对标 setTreePosition；默认 0，-1=跟随视觉第 0 列）。
 * @param self 目标视图。
 * @param column 树位置列号（可为 -1，语义对标 Qt）。
 * @return 无返回值。
 * @note 树位置绘制未接：当前扁平行模型树结构恒按第 0 列绘制，本接口为
 *       状态存取，供派生 XTreeWidget 接线使用。
 */
void XTreeView_setTreePosition(XTreeView* self, int column);
/** @brief 查询树位置列（对标 treePosition；返回 -1 表示跟随视觉第 0 列）。
 * @param self 目标视图。
 * @return 树位置列号。
 */
int XTreeView_treePosition(const XTreeView* self);
/** @brief 设置展开/收起动画开关（对标 setAnimated；默认 false）。
 * @param self 目标视图。
 * @param enable true 启用动画。
 * @return 无返回值。
 * @note 当前渲染无动画管线，本接口为状态存取。
 */
void XTreeView_setAnimated(XTreeView* self, bool enable);
/** @brief 查询展开/收起动画开关（对标 isAnimated）。
 * @param self 目标视图。
 * @return 启用返回 true。
 */
bool XTreeView_isAnimated(const XTreeView* self);
/** @brief 设置条目文本自动换行（对标 setWordWrap；默认 false）。
 * @param self 目标视图。
 * @param on true 换行。
 * @return 无返回值。
 * @note 当前绘制为单行省略文本，本接口为状态存取，供派生控件读取生效。
 */
void XTreeView_setWordWrap(XTreeView* self, bool on);
/** @brief 查询条目文本自动换行（对标 wordWrap）。
 * @param self 目标视图。
 * @return 换行返回 true。
 */
bool XTreeView_wordWrap(const XTreeView* self);
/** @brief 设置悬停自动展开延时（对标 setAutoExpandDelay；默认 -1=禁用）。
 * @param self 目标视图。
 * @param delay 延时（毫秒；-1 禁用）。
 * @return 无返回值。
 * @note 拖拽悬停展开交互未接（无拖放事件路径），本接口为状态存取。
 */
void XTreeView_setAutoExpandDelay(XTreeView* self, int delay);
/** @brief 查询悬停自动展开延时（对标 autoExpandDelay）。
 * @param self 目标视图。
 * @return 延时（毫秒；-1=禁用）；self 为 NULL 返回 -1。
 */
int XTreeView_autoExpandDelay(const XTreeView* self);

/* ==================== 行隐藏族（对标 QTreeView setRowHidden/isRowHidden） ==================== */

/** @brief 设置行隐藏（对标 setRowHidden；平行数组随模型行数同步）。
 * @param self 目标视图。
 * @param row 行号（<0 或越界忽略）。
 * @param hide true 隐藏（新增行默认可见）。
 * @return 无返回值（状态变化时请求一次重绘）。
 */
void XTreeView_setRowHidden(XTreeView* self, int row, bool hide);
/** @brief 查询行隐藏（对标 isRowHidden）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 隐藏返回 true；越界或未建表返回 false。
 */
bool XTreeView_isRowHidden(const XTreeView* self, int row);

/* ==================== 跨列合并族（对标 QTreeView firstColumnSpanned） ==================== */

/** @brief 设置行首列跨全行宽（对标 setFirstColumnSpanned）。
 * @param self 目标视图。
 * @param row 行号（当前不参与计算）。
 * @param span 合并开关（当前不参与计算）。
 * @return 无返回值。
 * @note 当前为扁平行模型、第一列恒单列渲染，跨列合并不可表达；本接口
 *       为空操作保留（同 XTableView setRowSpan 先例），不存储、不发信号。
 */
void XTreeView_setFirstColumnSpanned(XTreeView* self, int row, bool span);
/** @brief 查询行首列是否跨全行宽（对标 isFirstColumnSpanned）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 恒返回 false（平铺模型无跨列能力）。
 */
bool XTreeView_isFirstColumnSpanned(const XTreeView* self, int row);

/* ==================== 表头对象挂接（对标 QTreeView setHeader/header） ==================== */

/** @brief 挂接水平表头对象（对标 setHeader；借用，不转移所有权）。
 * @param self 目标视图。
 * @param header 表头对象借用指针；可为 NULL 清除挂接；方向非水平（0）
 *               忽略（对标 Qt 断言语义的收敛）。
 * @return 无返回值（挂接后请求一次重绘）。
 * @note 与 headerHidden 机制对接：挂接时以视图侧隐藏状态镜像表头可见性
 *       （XWidget_setHidden），此后 setHeaderHidden 变化同样镜像；本库
 *       表头渲染由视图自身完成，挂接对象仅承载几何/指示器状态。
 */
void XTreeView_setHeader(XTreeView* self, XHeaderView* header);
/** @brief 查询挂接的表头对象（对标 header）。
 * @param self 目标视图。
 * @return 表头对象借用指针；内部无表头对象（未挂接）返回 NULL。
 */
XHeaderView* XTreeView_header(const XTreeView* self);

/* ==================== 模型变化槽（对标 QAbstractItemView::dataChanged） ==================== */

/** @brief 数据变化槽（对标 dataChanged；区间重绘请求）。
 * @param self 目标视图。
 * @param topRow 起始行。 @param leftCol 起始列。
 * @param bottomRow 结束行（含）。 @param rightCol 结束列（含）。
 * @return 无返回值（区间合法时请求一次重绘）。
 * @note Qt 中 dataChanged 由模型数据变化自动驱动；本库平铺模型下可
 *       在外部直接修改模型数据后手动调用触发重绘；区间逆序或完全
 *       越界忽略（对标 Qt 无效区间丢弃语义）。
 */
void XTreeView_dataChanged(XTreeView* self, int topRow, int leftCol,
                           int bottomRow, int rightCol);

/* ==================== 信号（对标 QTreeView） ==================== */

/** @brief expanded(int) 信号（行由折叠变展开时发射；载荷：行号）。 */
void* XTreeView_expanded_signal(XTreeView* self, int row);
/** @brief collapsed(int) 信号（行由展开变折叠时发射；载荷：行号）。 */
void* XTreeView_collapsed_signal(XTreeView* self, int row);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XTREEVIEW_H */
