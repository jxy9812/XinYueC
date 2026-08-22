/******************************************************************************
 * @file       XGridLayout.h
 * @brief      XGridLayout 网格布局（对标 Qt 6.8 QGridLayout）。
 * @details    XGridLayout 继承 XLayout，把子条目按行/列排放到单元格：
 *             - 条目定位：addWidget/addLayout/addItem 均提供
 *               (row, column) 与 (row, column, rowSpan, columnSpan)
 *               重载；不指定位置的 addItem 自动放置到扫描找到的第一个
 *               空格（先逐行、再逐列，对标 QGridLayout::addItem）；
 *             - 跨行跨列：rowSpan/columnSpan 为占用行/列数（1=单格），
 *               传入负数表示延伸到当前网格的行/列边界（对标 Qt 的 -1
 *               语义）；跨行跨列的 min/hint/max 与间距按 Qt
 *               distributeMultiBox 回填，行列 stretch 再参与额外空间分配；
 *             - 行列伸缩：setRowStretch/setColumnStretch（对标 Qt 同名
 *               API），多余空间按 stretch 比例分配；stretch 全部为 0 时
 *               分给可伸展（expanding）的条目；
 *             - 行列最小尺寸：setRowMinimumHeight/setColumnMinimumWidth
 *               （对标 Qt 同名 API）；
 *             - 间距：setHorizontalSpacing/setVerticalSpacing 独立行列
 *               间距（-1 沿用父级默认）；setSpacing 同时设置两者；
 *             - 原点角：setOriginCorner 支持 TopLeft/TopRight/
 *               BottomLeft/BottomRight 四角原点（对标 Qt::Corner），
 *               右下/左下/右上原点分别镜像水平/垂直/双向排放；
 *             - 查询：cellRect(row,column)（最近一次激活后的单元格矩形）、
 *               itemAtPosition(row,column)（命中跨格条目的行列查找）、
 *               getItemPosition()（条目占用行列输出）、rowCount()/
 *               columnCount()（当前最大已占用行列）；
 *             - 尺寸协商：sizeHint/minimumSize/maximumSize/
 *               expandingDirections、heightForWidth 体系（任一子条目
 *               支持 hfw 即启用，先按列宽分配再以 hfw 高度回填行高）。
 *             条目对齐与盒式布局一致：alignment==0 填满整格，设置了
 *             水平/垂直对齐位后由条目自身按首选尺寸收拢摆放（对齐
 *             数值与 Qt::Alignment 一致）。
 * @note       内部条目的 addWidget 归布局所有（布局 deinit 时释放）；
 *             addLayout/addItem 传入的条目（含子布局）不取所有权。
 *             模块开关 XLAYOUT_GRID_ON（XLayout_config.h），置 0 时本类
 *             公共 API 全部裁剪。不依赖任何平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGRIDLAYOUT_H
#define XGRIDLAYOUT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XLayout.h"
#include "XLayout_config.h"

#if XLAYOUT_ON && XLAYOUT_GRID_ON

/* ==================== 原点角（对标 Qt::Corner） ==================== */

/**
 * @brief      网格原点角（对标 Qt 6.8 Qt::Corner，数值一致）。
 * @details    决定第 0 行/第 0 列位于网格的哪个角：
 *             - TopLeft：左上角（常规）；
 *             - TopRight：水平镜像（第 0 列在右）；
 *             - BottomLeft：垂直镜像（第 0 行在下）；
 *             - BottomRight：水平+垂直同时镜像。
 */
typedef enum XGridLayoutOriginCorner
{
    XGridLayoutOriginCorner_TopLeft     = 0, /**< 左上角（默认）。 */
    XGridLayoutOriginCorner_TopRight    = 1, /**< 右上角，水平镜像。 */
    XGridLayoutOriginCorner_BottomLeft  = 2, /**< 左下角，垂直镜像。 */
    XGridLayoutOriginCorner_BottomRight = 3  /**< 右下角，双向镜像。 */
} XGridLayoutOriginCorner;

/* ==================== 默认定位方向（对标 Qt::Orientation） ==================== */

/**
 * @brief      默认定位方向（对标 Qt 6.8 Qt::Orientation，数值一致）。
 * @details    setDefaultPositioning 指定 addItem/addWidgetAuto 等
 *             “无显式坐标”条目的自动排布方向：
 *             - Horizontal：网格扩展为 1 行 n 列，条目按行优先（先列后行）
 *               逐格放入，游标列先进位；
 *             - Vertical：网格扩展为 n 行 1 列，条目按列优先（先先后行）
 *               逐格放入，游标行先进位。
 */
typedef enum XOrientation
{
    XOrientation_Horizontal = 1, /**< 水平优先（对标 Qt::Horizontal）。 */
    XOrientation_Vertical   = 2  /**< 垂直优先（对标 Qt::Vertical）。 */
} XOrientation;

/* ==================== 虚函数表（不新增槽位，仅重载 XLayout 槽位） ==================== */

/**
 * @brief XGridLayout 虚函数表枚举：继承 XLayout 全部槽位，不新增虚函数。
 */
XCLASS_DEFINE_BEGING(XGridLayout)
XCLASS_DEFINE_EXTEND_END(XGridLayout, XLayout)

/** @brief 网格单元格描述（内部实现；调用方不得直接修改）。 */
typedef struct XGridLayoutCell
{
    int m_row;         /**< 起始行（从 0 开始）。 */
    int m_column;      /**< 起始列（从 0 开始）。 */
    int m_rowSpan;     /**< 结束行（含）；>= m_row，行内占用 m_rowSpan-m_row+1 行。 */
    int m_columnSpan;  /**< 结束列（含）；>= m_column。 */
} XGridLayoutCell;

/** @brief 网格布局内部数组的初始容量（内部）。 */
#define XGRIDLAYOUT_CHUNK 8

/**
 * @brief      XGridLayout 网格布局对象；m_base 必须是第一个成员。
 * @details    字段含义（实现内部状态，调用方不得直接修改）：
 *             - m_cells：与条目数组一一平行的单元格数组；
 *             - m_rowStretch/m_columnStretch：行列伸展因子数组（行/列索引，
 *               随行列数动态扩容）：
 *             - m_rowMinHeight/m_columnMinWidth：行列显式最小尺寸数组；
 *             - m_hSpacing/m_vSpacing：水平/垂直间距（-1 沿用父级默认）；
 *             - m_originCorner：原点角（见 XGridLayoutOriginCorner）；
 *             - m_rowCount/m_columnCount：当前网格行/列数（构造时为 1x1，
 *               添加条目时只扩不减，takeAt() 后保持原尺寸）；
 *             - m_cacheColPos/m_cacheColSize/m_cacheRowPos/m_cacheRowSize：
 *               最近一次激活后的行列位置/尺寸缓存（cellRect 查询使用）。
 */
typedef struct XGridLayout
{
    XLayout                m_base;          /**< 基类成员；必须是第一个。 */
    XGridLayoutCell*       m_cells;         /**< 单元格数组（与条目平行）。 */
    int                    m_cellCapacity;  /**< 单元格数组容量。 */
    int*                   m_rowStretch;    /**< 行伸展因子数组。 */
    int                    m_rowStretchCapacity;  /**< 行伸展数组容量。 */
    int*                   m_columnStretch;       /**< 列伸展因子数组。 */
    int                    m_columnStretchCapacity; /**< 列伸展数组容量。 */
    int*                   m_rowMinHeight;  /**< 行最小高度数组。 */
    int                    m_rowMinHeightCapacity; /**< 行最小高度数组容量。 */
    int*                   m_columnMinWidth;       /**< 列最小宽度数组。 */
    int                    m_columnMinWidthCapacity; /**< 列最小宽度数组容量。 */
    int                    m_hSpacing;      /**< 水平间距；-1 沿用父级默认。 */
    int                    m_vSpacing;      /**< 垂直间距；-1 沿用父级默认。 */
    XGridLayoutOriginCorner m_originCorner; /**< 原点角。 */
    int                    m_rowCount;      /**< 当前网格行数（初始为 1）。 */
    int                    m_columnCount;   /**< 当前网格列数（初始为 1）。 */
    int                    m_nextR;         /**< 默认定位游标行（对标 Qt nextR）。 */
    int                    m_nextC;         /**< 默认定位游标列（对标 Qt nextC）。 */
    uint8_t                m_addVertical;   /**< 默认定位方向：1=先竖后横、0=先横后竖（对标 Qt addVertical）。 */
    int*                   m_cacheColPos;   /**< 列位置缓存（cellRect）。 */
    int*                   m_cacheColSize;  /**< 列宽度缓存（cellRect）。 */
    int*                   m_cacheRowPos;   /**< 行位置缓存（cellRect）。 */
    int*                   m_cacheRowSize;  /**< 行高度缓存（cellRect）。 */
    int                    m_cacheColPosCount;  /**< 列位置缓存容量。 */
    int                    m_cacheColSizeCount; /**< 列尺寸缓存容量。 */
    int                    m_cacheRowPosCount;  /**< 行位置缓存容量。 */
    int                    m_cacheRowSizeCount; /**< 行尺寸缓存容量。 */
} XGridLayout;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化 XGridLayout 类虚函数表并返回共享表指针。
 * @return     XGridLayout 类的共享 XVtable 指针。
 */
XVtable* XGridLayout_class_init(void);

/**
 * @brief      初始化网格布局（对标 QGridLayout::QGridLayout(QWidget*)）。
 * @details    初始行/列数为 1x1；添加条目时网格只扩不减，takeAt() 后保留
 *             当前行列数；间距 -1（沿用父级）、
 *             原点角 TopLeft（对标 Qt 默认）。调用方不得直接实例化
 *             XGridLayout 之外的布局对象。
 * @param      self 待初始化的布局对象；不可为 NULL。
 */
void XGridLayout_init(XGridLayout* self);

/**
 * @brief      创建网格布局并可选挂接到控件（对标 QGridLayout::QGridLayout）。
 * @param      parent 可选父控件；非 NULL 时自动挂接为控件的顶层布局。
 * @return     堆上新建布局对象；失败返回 NULL。
 */
XGridLayout* XGridLayout_create(XWidget* parent);

/* ==================== 间距（对标 QGridLayout） ==================== */

/**
 * @brief      设置单元格水平间距（对标 QGridLayout::setHorizontalSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @param      spacing 水平间距像素；-1 表示沿用父级/默认（0）。
 */
void XGridLayout_setHorizontalSpacing(XGridLayout* self, int spacing);

/**
 * @brief      返回单元格水平间距（对标 QGridLayout::horizontalSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @return     水平间距；未设置(-1)或失败返回 -1。
 */
int XGridLayout_horizontalSpacing(const XGridLayout* self);

/**
 * @brief      设置单元格垂直间距（对标 QGridLayout::setVerticalSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @param      spacing 垂直间距像素；-1 表示沿用父级/默认（0）。
 */
void XGridLayout_setVerticalSpacing(XGridLayout* self, int spacing);

/**
 * @brief      返回单元格垂直间距（对标 QGridLayout::verticalSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @return     垂直间距；未设置(-1)或失败返回 -1。
 */
int XGridLayout_verticalSpacing(const XGridLayout* self);

/**
 * @brief      同时设置水平/垂直间距（对标 QGridLayout::setSpacing）。
 * @param      self 目标布局；可为 NULL。
 * @param      spacing 两个方向的间距像素；-1 表示沿用父级/默认。
 */
void XGridLayout_setSpacing(XGridLayout* self, int spacing);

/**
 * @brief      返回网格间距（对标 QGridLayout::spacing）。
 * @details    在 Qt 中网格的 spacing 语义上没有单一值（水平/垂直可不同），
 *             本实现按“水平与垂直一致时返回该值，不一致时返回 -1”处理，
 *             与 setSpacing 成对使用（对标 Qt 对 spacing() 的文档约定）。
 * @param      self 目标布局；可为 NULL。
 * @return     间距像素（-1 表示未设置或水平/垂直不同）。
 */
int XGridLayout_spacing(const XGridLayout* self);

/* ==================== 行列伸缩（对标 QGridLayout） ==================== */

/**
 * @brief      设置行的伸展因子（对标 QGridLayout::setRowStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      row 行号（从 0 开始）；越界时自动扩展行数。
 * @param      stretch 伸展因子（≥0；0=该行不参与多余空间分配）。
 */
void XGridLayout_setRowStretch(XGridLayout* self, int row, int stretch);

/**
 * @brief      返回行的伸展因子（对标 QGridLayout::rowStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      row 行号（从 0 开始）。
 * @return     伸展因子；越界或失败返回 0。
 */
int XGridLayout_rowStretch(const XGridLayout* self, int row);

/**
 * @brief      设置列的伸展因子（对标 QGridLayout::setColumnStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      column 列号（从 0 开始）；越界时自动扩展列数。
 * @param      stretch 伸展因子（≥0）。
 */
void XGridLayout_setColumnStretch(XGridLayout* self, int column, int stretch);

/**
 * @brief      返回列的伸展因子（对标 QGridLayout::columnStretch）。
 * @param      self 目标布局；可为 NULL。
 * @param      column 列号（从 0 开始）。
 * @return     伸展因子；越界或失败返回 0。
 */
int XGridLayout_columnStretch(const XGridLayout* self, int column);

/* ==================== 行列最小尺寸（对标 QGridLayout） ==================== */

/**
 * @brief      设置行的最小高度（对标 QGridLayout::setRowMinimumHeight）。
 * @param      self 目标布局；可为 NULL。
 * @param      row 行号（从 0 开始）；越界时自动扩展行数。
 * @param      minSize 最小高度像素（≥0）。
 */
void XGridLayout_setRowMinimumHeight(XGridLayout* self, int row, int minSize);

/**
 * @brief      返回行的最小高度（对标 QGridLayout::rowMinimumHeight）。
 * @param      self 目标布局；可为 NULL。
 * @param      row 行号（从 0 开始）。
 * @return     最小高度；越界或失败返回 0。
 */
int XGridLayout_rowMinimumHeight(const XGridLayout* self, int row);

/**
 * @brief      设置列的最小宽度（对标 QGridLayout::setColumnMinimumWidth）。
 * @param      self 目标布局；可为 NULL。
 * @param      column 列号（从 0 开始）；越界时自动扩展列数。
 * @param      minSize 最小宽度像素（≥0）。
 */
void XGridLayout_setColumnMinimumWidth(XGridLayout* self, int column, int minSize);

/**
 * @brief      返回列的最小宽度（对标 QGridLayout::columnMinimumWidth）。
 * @param      self 目标布局；可为 NULL。
 * @param      column 列号（从 0 开始）。
 * @return     最小宽度；越界或失败返回 0。
 */
int XGridLayout_columnMinimumWidth(const XGridLayout* self, int column);

/* ==================== 行列数（对标 QGridLayout） ==================== */

/**
 * @brief      返回当前网格列数（对标 QGridLayout::columnCount）。
 * @details    返回当前网格列数；空布局构造时为 1，添加条目时只扩不减，
 *             takeAt() 后保留当前值。
 * @param      self 目标布局；可为 NULL。
 * @return     列数；失败返回 0。
 */
int XGridLayout_columnCount(const XGridLayout* self);

/**
 * @brief      返回当前网格行数（对标 QGridLayout::rowCount）。
 * @details    返回当前网格行数；空布局构造时为 1，添加条目时只扩不减，
 *             takeAt() 后保留当前值。
 * @param      self 目标布局；可为 NULL。
 * @return     行数；失败返回 0。
 */
int XGridLayout_rowCount(const XGridLayout* self);

/* ==================== 添加条目（对标 QGridLayout add* 系列） ==================== */

/**
 * @brief      把控件自动放入下一个默认定位单元格（对标
 *             QGridLayout::addWidget(QWidget*) 无坐标重载）。
 * @details    Qt 中该无坐标重载内联转发给 QLayout::addWidget，等价于
 *             走 addItem 的默认定位游标（nextR/nextC）；本函数行为
 *             完全一致：自动创建内部控件条目（归布局所有），控件本身
 *             为借用、不取得所有权。方向默认先横后竖，可用
 *             setDefaultPositioning 调整。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL（忽略）。
 */
void XGridLayout_addWidgetAuto(XGridLayout* self, XWidget* widget);

/**
 * @brief      把控件添加到指定单元格（对标 QGridLayout::addWidget）。
 * @details    自动创建内部控件条目（归布局所有）；控件本身为借用，
 *             不取得所有权。对齐为 0 时控件填满整格。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL（忽略）。
 * @param      row 起始行（从 0 开始）；负数按 0 处理。
 * @param      column 起始列（从 0 开始）；负数按 0 处理。
 * @param      alignment 对齐标志（0=填满；XLayoutAlignment 组合）。
 */
void XGridLayout_addWidget(XGridLayout* self, XWidget* widget,
                           int row, int column, XLayoutAlignments alignment);

/**
 * @brief      把控件添加到指定单元格并跨若干行/列（对标 QGridLayout::addWidget）。
 * @details    rowSpan/columnSpan 为占用行/列数（1=单格，必须 ≥1）；
 *             传入负数表示延伸到当前网格的行/列边界；0 按 1 处理。
 * @param      self 目标布局；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL（忽略）。
 * @param      row 起始行；负数按 0 处理。
 * @param      column 起始列；负数按 0 处理。
 * @param      rowSpan 占用行数（<0 延伸到底部）。
 * @param      columnSpan 占用列数（<0 延伸到右缘）。
 * @param      alignment 对齐标志（0=填满）。
 */
void XGridLayout_addWidgetSpan(XGridLayout* self, XWidget* widget,
                               int row, int column, int rowSpan, int columnSpan,
                               XLayoutAlignments alignment);

/**
 * @brief      把子布局添加到指定单元格（对标 QGridLayout::addLayout）。
 * @details    子布局对象按借用指针挂入（不取所有权），其父布局自动
 *             登记为 self；对齐为 0 时子布局填满整格。
 * @param      self 目标布局；可为 NULL。
 * @param      child 子布局借用指针；可为 NULL（忽略）。
 * @param      row 起始行；负数按 0 处理。
 * @param      column 起始列；负数按 0 处理。
 * @param      alignment 对齐标志（0=填满）。
 */
void XGridLayout_addLayout(XGridLayout* self, XLayout* child,
                           int row, int column, XLayoutAlignments alignment);

/**
 * @brief      把子布局添加到指定单元格并跨若干行/列（对标 QGridLayout::addLayout）。
 * @param      self 目标布局；可为 NULL。
 * @param      child 子布局借用指针；可为 NULL（忽略）。
 * @param      row 起始行；负数按 0 处理。
 * @param      column 起始列；负数按 0 处理。
 * @param      rowSpan 占用行数（<0 延伸到底部）。
 * @param      columnSpan 占用列数（<0 延伸到右缘）。
 * @param      alignment 对齐标志（0=填满）。
 */
void XGridLayout_addLayoutSpan(XGridLayout* self, XLayout* child,
                               int row, int column, int rowSpan, int columnSpan,
                               XLayoutAlignments alignment);

/**
 * @brief      把任意条目自动放入下一个默认定位单元格（对标
 *             QGridLayout::addItem(QLayoutItem*)）。
 * @details    Qt 中无显式坐标的 addItem 走内部“默认定位游标”
 *             （nextR/nextC）：先取游标位置，条目放入后按方向推进
 *             （默认先横后竖；setDefaultPositioning 可改为先竖后横），
 *             游标越界自动绕行到下一行/列。本实现完全对齐该语义，
 *             不再使用“扫描第一个空格”的老算法。条目按借用指针保存，
 *             释放责任仍在调用方。
 * @param      self 目标布局；可为 NULL。
 * @param      item 条目借用指针；可为 NULL（忽略）。
 */
void XGridLayout_addItem(XGridLayout* self, XLayoutItem* item);

/**
 * @brief      把任意条目添加到指定单元格（对标 QGridLayout::addItem）。
 * @param      self 目标布局；可为 NULL。
 * @param      item 条目借用指针；可为 NULL（忽略）。
 * @param      row 起始行；负数按 0 处理。
 * @param      column 起始列；负数按 0 处理。
 * @param      rowSpan 占用行数（<0 延伸到底部）。
 * @param      columnSpan 占用列数（<0 延伸到右缘）。
 * @param      alignment 对齐标志（0=填满）。
 */
void XGridLayout_addItemAt(XGridLayout* self, XLayoutItem* item,
                           int row, int column, int rowSpan, int columnSpan,
                           XLayoutAlignments alignment);

/* ==================== 查询（对标 QGridLayout） ==================== */

/**
 * @brief      返回指定单元格的矩形（对标 QGridLayout::cellRect）。
 * @details    返回最近一次激活/分配后的单元格位置与尺寸（相对控件
 *             客户区）；布局尚未分配过几何或行列越界时返回全 0 矩形。
 *             跨格条目的单元格只返回起始格（不合并）。
 * @param      self 目标布局；可为 NULL。
 * @param      row 行号（从 0 开始）。
 * @param      column 列号（从 0 开始）。
 * @return     单元格矩形；失败返回全 0 矩形。
 */
XRect XGridLayout_cellRect(const XGridLayout* self, int row, int column);

/**
 * @brief      返回占用指定单元格的条目（对标 QGridLayout::itemAtPosition）。
 * @details    多个条目跨格重叠时（不推荐）返回最后挂入的那个条目。
 * @param      self 目标布局；可为 NULL。
 * @param      row 行号（从 0 开始）。
 * @param      column 列号（从 0 开始）。
 * @return     条目借用指针；没有条目占用或失败返回 NULL。
 */
XLayoutItem* XGridLayout_itemAtPosition(const XGridLayout* self,
                                        int row, int column);

/**
 * @brief      输出条目的占用行列（对标 QGridLayout::getItemPosition）。
 * @param      self 目标布局；可为 NULL。
 * @param      index 条目索引（从 0 开始）。
 * @param      row/column/rowSpan/columnSpan 输出参数；可为 NULL 跳过，
 *             输出退出网边界时返回当前网格尺寸。
 * @return     条目存在时返回 true；越界或失败返回 false。
 */
bool XGridLayout_getItemPosition(const XGridLayout* self, int index,
                                 int* row, int* column,
                                 int* rowSpan, int* columnSpan);

/* ==================== 默认定位（对标 QGridLayout） ==================== */

/**
 * @brief      设置默认定位方向与网格规模（对标
 *             QGridLayout::setDefaultPositioning）。
 * @details    Qt 文档约定（内部常用）：设置 addItem()/addWidgetAuto()
 *             等无坐标条目使用的自动排布：
 *             - orient==Horizontal：网格扩展为 1 行 n 列，条目先横后竖
 *               逐格放入（行固定为 0，列 0..n-1 逐个填满后换到新行）；
 *             - orient==Vertical：网格扩展为 n 行 1 列，条目先竖后横
 *               逐格放入（列固定为 0，行 0..n-1 逐个填满后换到新列）。
 *             @note n<=0 时按 1 处理（对标 Qt expand 的 qMax 语义）；
 *             该方法只影响“之后的”无坐标添加，已放入的条目不动。
 * @param      self 目标布局；可为 NULL。
 * @param      n 目标行/列数。
 * @param      orient 默认定位方向（XOrientation_Horizontal/Vertical）。
 */
void XGridLayout_setDefaultPositioning(XGridLayout* self, int n,
                                       XOrientation orient);

/* ==================== 原点角（对标 QGridLayout） ==================== */

/**
 * @brief      设置网格原点角（对标 QGridLayout::setOriginCorner）。
 * @details    左上为默认；右上/左下/右下分别产生水平/垂直/双向镜像
 *             排放。设置后立即失效并触发重算（如已激活）。
 * @param      self 目标布局；可为 NULL。
 * @param      corner 原点角枚举值。
 */
void XGridLayout_setOriginCorner(XGridLayout* self,
                                 XGridLayoutOriginCorner corner);

/**
 * @brief      返回网格原点角（对标 QGridLayout::originCorner）。
 * @param      self 目标布局；可为 NULL。
 * @return     当前原点角；失败返回 TopLeft。
 */
XGridLayoutOriginCorner XGridLayout_originCorner(const XGridLayout* self);

#endif /* XLAYOUT_ON && XLAYOUT_GRID_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGRIDLAYOUT_H */
