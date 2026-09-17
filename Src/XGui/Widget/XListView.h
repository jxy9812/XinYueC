/**
 * @file       XListView.h
 * @brief      XListView 列表视图（对标 Qt 6.8 QListView）。
 * @details    以 m_base 组合继承 XAbstractItemView；从数据模型
 *             （XAbstractItemModel）按单列垂直列表渲染条目（行高
 *             24px），支持间隔 spacing、选中高亮；选择/信号/命中
 *             全部复用基类数据通路。状态族（flow/gridSize/wrapping/
 *             viewMode/resizeMode/layoutMode/batchSize/itemAlignment/
 *             selectionRectVisible/wordWrap/行隐藏）对标 QListView
 *             同名属性，在平铺行模型下为状态存取 + 轻量绘制联动
 *             （行隐藏跳过绘制与命中、网格高参与槽位、词换行与
 *             条目对齐参与文本绘制）。
 * @note       模块总开关 XTABLEWIDGET_ON；XListView 为 XWidget 派生链
 *             （XListView→XAbstractItemView→XAbstractScrollArea→XFrame）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLISTVIEW_H
#define XLISTVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemView.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 枚举（对标 QListView 状态枚举，数值对齐 Qt） ==================== */

/**
 * @brief      排列方向（对标 QListView::Flow，数值逐项一致）。
 * @details    TopDown 自上而下逐行排布（默认）；LeftToRight 自左向右
 *             换行排布。XGui 平铺行模型下存储该状态并参与命中/绘制的
 *             槽位计算；横向流式重排布局不实现（@note）。
 */
typedef enum XListViewFlow
{
    XListViewFlow_TopDown = 0,      /**< 自上而下（对标 QListView::Flow_TopToBottom）。 */
    XListViewFlow_LeftToRight = 1   /**< 自左向右（对标 QListView::Flow_LeftToRight）。 */
} XListViewFlow;

/**
 * @brief      视图模式（对标 QListView::ViewMode，数值逐项一致）。
 * @details    ListMode 普通列表（默认）；IconMode 图标模式。切换模式
 *             时按 Qt 语义联动 wrapping 与 flow（ListMode 复位换行、
 *             回退 TopDown；IconMode 打开换行、切 LeftToRight）。
 */
typedef enum XListViewViewMode
{
    XListViewViewMode_ListMode = 0, /**< 列表模式（对标 QListView::ListMode）。 */
    XListViewViewMode_IconMode = 1  /**< 图标模式（对标 QListView::IconMode）。 */
} XListViewViewMode;

/**
 * @brief      尺寸调整模式（对标 QListView::ResizeMode，数值逐项一致）。
 * @details    Static 视图尺寸变化不重排（默认）；Adjust 视图尺寸变化
 *             时重排条目。XGui 为同步绘制模型，Adjust 仅作为状态声明
 *             供接入层读取（@note）。
 */
typedef enum XListViewResizeMode
{
    XListViewResizeMode_Static = 0, /**< 不随视图尺寸重排（对标 Static）。 */
    XListViewResizeMode_Adjust = 1  /**< 随视图尺寸重排（对标 Adjust）。 */
} XListViewResizeMode;

/**
 * @brief      布局模式（对标 QListView::LayoutMode，数值逐项一致）。
 * @details    SinglePass 一次性完成布局（默认）；Batched 按批次布局，
 *             批量由 batchSize 指定。XGui 为同步绘制模型，仅存储状态
 *             供接入层读取（@note）。
 */
typedef enum XListViewLayoutMode
{
    XListViewLayoutMode_SinglePass = 0, /**< 单趟布局（对标 SinglePass）。 */
    XListViewLayoutMode_Batched = 1     /**< 批式布局（对标 Batched）。 */
} XListViewLayoutMode;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XListView)
XCLASS_DEFINE_EXTEND_END(XListView, XAbstractItemView)

/** @brief 列表视图对象；m_base 必须是第一个成员（嵌 XAbstractItemView）。 */
typedef struct XListView
{
    XAbstractItemView m_base;   /**< 基类成员；必须是第一个。 */
    int m_spacing;              /**< 条目间距（像素，默认 0）。 */
    int m_modelColumn;          /**< 渲染列（默认 0）。 */
    int m_rowHeight;            /**< 行高（像素，默认 24）。 */
    int m_flow;                 /**< 排列方向（XListViewFlow，默认 TopDown）。 */
    int m_gridWidth;            /**< 网格宽（像素；<=0 未启用，默认 -1）。 */
    int m_gridHeight;           /**< 网格高（像素；<=0 未启用，默认 -1）。 */
    bool m_wrapping;            /**< 换行排布（默认 false）。 */
    int m_viewMode;             /**< 视图模式（XListViewViewMode，默认 ListMode）。 */
    int m_resizeMode;           /**< 尺寸调整模式（XListViewResizeMode，默认 Static）。 */
    int m_layoutMode;           /**< 布局模式（XListViewLayoutMode，默认 SinglePass）。 */
    int m_batchSize;            /**< 批式布局批量（>0，默认 100）。 */
    int m_itemAlignment;        /**< 条目对齐位掩码（Qt::Alignment 位值，默认 0）。 */
    bool m_selectionRectVisible; /**< 橡皮筋选择框可见（默认 false）。 */
    bool m_wordWrap;            /**< 文本按词换行（默认 false）。 */
    bool* m_rowHidden;          /**< 行隐藏状态表（平行数组；对象拥有）。 */
    int m_rowStateCount;        /**< 行隐藏状态表长度（随模型行数同步）。 */
} XListView;

/* ==================== 生命周期 ==================== */

XVtable* XListView_class_init(void);
/** @brief 初始化列表视图。
 * @param self 目标视图；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @return 无返回值。
 */
void XListView_init(XListView* self, XWidget* parent, XWidgetFlags flags);
/** @brief 使用指定内存类型创建列表视图。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @return 新建对象；失败 NULL。
 */
XListView* XListView_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
#define XListView_create(parent, flags) \
    XListView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
#define XListView_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XListView_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QListView） ==================== */

/** @brief 设置条目间距。
 * @param self 目标视图。
 * @param spacing 间距（像素；>=0）。
 * @return 无返回值。
 */
void XListView_setSpacing(XListView* self, int spacing);
/** @brief 查询条目间距。 @param self 目标视图。 @return 间距。 */
int XListView_spacing(const XListView* self);
/** @brief 设置渲染列（对标 QListView::setModelColumn）。
 * @param self 目标视图。
 * @param column 列号（>=0）。
 * @return 无返回值。
 */
void XListView_setModelColumn(XListView* self, int column);
/** @brief 查询渲染列。 @param self 目标视图。 @return 列号。 */
int XListView_modelColumn(const XListView* self);
/** @brief 设置行高（项目库扩展：统一行高像素）。
 * @param self 目标视图。
 * @param height 行高（像素；>0）。
 * @return 无返回值。
 */
void XListView_setRowHeight(XListView* self, int height);
/** @brief 查询行高。 @param self 目标视图。 @return 行高。 */
int XListView_rowHeight(const XListView* self);

/* ==================== 状态族（对标 QListView 状态属性） ==================== */

/** @brief 设置排列方向（对标 QListView::setFlow）。
 * @param self 目标视图。
 * @param flow 排列方向（XListViewFlow 值；非法值忽略）。
 * @return 无返回值。
 * @note 平铺行模型下存储状态；横向流式重排布局不实现（@note）。
 */
void XListView_setFlow(XListView* self, int flow);
/** @brief 查询排列方向。
 * @param self 目标视图。
 * @return XListViewFlow 值；对象无效返回 0（TopDown）。
 */
int XListView_flow(const XListView* self);
/** @brief 设置布局网格尺寸（对标 QListView::setGridSize；XGui 无 QSize，宽高分开）。
 * @param self 目标视图。
 * @param width 网格宽（像素；<=0 表示该维未启用，按 -1 存储）。
 * @param height 网格高（像素；<=0 表示该维未启用，按 -1 存储）。
 * @return 无返回值。
 * @note 默认 (-1,-1)；网格高参与绘制槽位与命中步进（取与行高的较大值），
 *       网格宽收缩条目单元宽度；Qt 默认 QSize(-1,0) 的“未启用”语义一致。
 */
void XListView_setGridSize(XListView* self, int width, int height);
/** @brief 查询网格宽。
 * @param self 目标视图。
 * @return 网格宽（像素；<=0 表示未启用）。
 */
int XListView_gridSizeWidth(const XListView* self);
/** @brief 查询网格高。
 * @param self 目标视图。
 * @return 网格高（像素；<=0 表示未启用）。
 */
int XListView_gridSizeHeight(const XListView* self);
/** @brief 设置换行排布（对标 QListView::setWrapping）。
 * @param self 目标视图。
 * @param enable true 允许条目换行排布。
 * @return 无返回值。
 * @note 平铺行模型下仅存储状态，换行重排布局不实现（@note）。
 */
void XListView_setWrapping(XListView* self, bool enable);
/** @brief 查询换行排布。 @param self 目标视图。 @return 已启用返回 true。 */
bool XListView_isWrapping(const XListView* self);
/** @brief 设置视图模式（对标 QListView::setViewMode）。
 * @param self 目标视图。
 * @param mode 视图模式（XListViewViewMode 值；非法值忽略）。
 * @return 无返回值。
 * @note 对齐 Qt 联动：切 ListMode 复位 wrapping=false、flow=TopDown；
 *       切 IconMode 置 wrapping=true、flow=LeftToRight；spacing/gridSize
 *       等其余模式属性 XGui 不自动复位（@note）。
 */
void XListView_setViewMode(XListView* self, int mode);
/** @brief 查询视图模式。
 * @param self 目标视图。
 * @return XListViewViewMode 值；对象无效返回 0（ListMode）。
 */
int XListView_viewMode(const XListView* self);
/** @brief 设置尺寸调整模式（对标 QListView::setResizeMode）。
 * @param self 目标视图。
 * @param mode 尺寸调整模式（XListViewResizeMode 值；非法值忽略）。
 * @return 无返回值。
 * @note XGui 为同步绘制模型，Adjust 仅作为状态声明（@note）。
 */
void XListView_setResizeMode(XListView* self, int mode);
/** @brief 查询尺寸调整模式。
 * @param self 目标视图。
 * @return XListViewResizeMode 值；对象无效返回 0（Static）。
 */
int XListView_resizeMode(const XListView* self);
/** @brief 设置布局模式（对标 QListView::setLayoutMode）。
 * @param self 目标视图。
 * @param mode 布局模式（XListViewLayoutMode 值；非法值忽略）。
 * @return 无返回值。
 * @note XGui 为同步绘制模型，Batched 仅作为状态声明（@note）。
 */
void XListView_setLayoutMode(XListView* self, int mode);
/** @brief 查询布局模式。
 * @param self 目标视图。
 * @return XListViewLayoutMode 值；对象无效返回 0（SinglePass）。
 */
int XListView_layoutMode(const XListView* self);
/** @brief 设置批式布局批量（对标 QListView::setBatchSize）。
 * @param self 目标视图。
 * @param batchSize 每批条目数（>0；<=0 忽略，对标 Qt 拒绝非正值）。
 * @return 无返回值。
 */
void XListView_setBatchSize(XListView* self, int batchSize);
/** @brief 查询批式布局批量。
 * @param self 目标视图。
 * @return 每批条目数（默认 100）；对象无效返回 0。
 */
int XListView_batchSize(const XListView* self);
/** @brief 设置条目对齐（对标 QListView::setItemAlignment；按位存取）。
 * @param self 目标视图。
 * @param alignment 对齐位掩码（Qt::Alignment 位值，可组合）。
 * @return 无返回值。
 * @note 非 0 时条目文本按该掩码在行单元内对齐绘制（水平/垂直位均
 *       参与，未指定的维度回退左对齐/底对齐，@note）。
 */
void XListView_setItemAlignment(XListView* self, int alignment);
/** @brief 查询条目对齐位掩码。
 * @param self 目标视图。
 * @return 对齐位掩码；对象无效返回 0。
 */
int XListView_itemAlignment(const XListView* self);
/** @brief 设置橡皮筋选择框可见（对标 QListView::setSelectionRectVisible）。
 * @param self 目标视图。
 * @param show true 显示橡皮筋框。
 * @return 无返回值。
 * @note 平铺行模型下仅存储状态，橡皮筋框绘制不实现（@note）。
 */
void XListView_setSelectionRectVisible(XListView* self, bool show);
/** @brief 查询橡皮筋选择框可见。 @param self 目标视图。 @return 已启用返回 true。 */
bool XListView_isSelectionRectVisible(const XListView* self);
/** @brief 设置文本按词换行（对标 QListView::setWordWrap）。
 * @param self 目标视图。
 * @param on true 按词换行。
 * @return 无返回值。
 * @note true 时条目文本在行单元内以 TextWordWrap 方式换行裁剪绘制。
 */
void XListView_setWordWrap(XListView* self, bool on);
/** @brief 查询文本按词换行。 @param self 目标视图。 @return 已启用返回 true。 */
bool XListView_wordWrap(const XListView* self);
/** @brief 设置行隐藏（对标 QListView::setRowHidden；平行数组随模型行数同步）。
 * @param self 目标视图。
 * @param row 行号（越界或未同步忽略）。
 * @param hide true 隐藏该行。
 * @return 无返回值。
 * @note 隐藏行不参与绘制与命中测试；数组长度与模型行数同步，
 *       新增行默认可见（参照 XTreeView 行状态数组模式）。
 */
void XListView_setRowHidden(XListView* self, int row, bool hide);
/** @brief 查询行隐藏（对标 QListView::isRowHidden）。
 * @param self 目标视图。
 * @param row 行号。
 * @return 已隐藏返回 true；越界、未同步或对象无效返回 false。
 */
bool XListView_isRowHidden(const XListView* self, int row);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XLISTVIEW_H */
