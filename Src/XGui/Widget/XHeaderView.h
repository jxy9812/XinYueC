/**
 * @file       XHeaderView.h
 * @brief      XHeaderView 表头视图（对标 Qt 6.8 QHeaderView）。
 * @details    管理水平（列）或垂直（行）表头区间的尺寸布局；表头文本由
 *             关联模型（XAbstractItemModel）提供，渲染由 XTableView 统一
 *             完成（本类只负责几何：count/sectionSize/sectionPosition）。
 * @note       模块总开关 XTABLEWIDGET_ON；XHeaderView 为 XWidget 派生。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHEADERVIEW_H
#define XHEADERVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XHeaderView)
XCLASS_DEFINE_EXTEND_END(XHeaderView, XWidget)

/** @brief 表头视图对象；m_base 必须是第一个成员（嵌 XWidget）。 */
typedef struct XHeaderView
{
    XWidget   m_base;      /**< 基类成员；必须是第一个。 */
    int       m_orientation; /**< 方向（0=水平列头，1=垂直行头）。 */
    int       m_count;     /**< 区间数。 */
    int       m_defaultSize; /**< 默认区间尺寸（默认 30）。 */
    XVector*  m_sections;  /**< 区间尺寸表（int；对象拥有）。 */
    bool      m_stretchLast; /**< 末尾拉伸（默认 false）。 */
    int       m_sectionMovedFrom; /**< moveSection 记录（预留）。 */
    bool*     m_hidden;    /**< 区间隐藏表（平行数组；对象拥有）。 */
    int       m_hiddenCount; /**< 隐藏区间数。 */
    bool      m_sectionsClickable; /**< 区间可点击（对标 sectionsClickable）。 */
    bool      m_sectionsMovable;   /**< 区间可拖动（对标 sectionsMovable）。 */
    bool      m_sortIndicatorShown; /**< 排序指示器可见（默认 false）。 */
    int       m_sortIndicatorSection; /**< 排序指示器区间（-1 无）。 */
    int       m_sortIndicatorOrder;   /**< 排序方向（0=升序，1=降序）。 */
} XHeaderView;

/** @brief 排序方向（对标 Qt::SortOrder，数值一致）。 */
typedef enum XHeaderViewSortOrder
{
    XHeaderViewSortOrder_Ascending = 0,  /**< 升序。 */
    XHeaderViewSortOrder_Descending = 1  /**< 降序。 */
} XHeaderViewSortOrder;

/* ==================== 生命周期 ==================== */

XVtable* XHeaderView_class_init(void);
/** @brief 初始化表头。
 * @param self 目标表头；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @param orientation 方向（0=水平，1=垂直）。
 * @return 无返回值。
 */
void XHeaderView_init(XHeaderView* self, XWidget* parent, XWidgetFlags flags,
                      int orientation);
/** @brief 使用指定内存类型创建表头。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @param orientation 方向（0=水平，1=垂直）。
 * @return 新建对象；失败 NULL。
 */
XHeaderView* XHeaderView_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags, int orientation);
#define XHeaderView_create(parent, flags, orientation) \
    XHeaderView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags), \
                          (orientation))
#define XHeaderView_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XHeaderView_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 几何 ==================== */

/** @brief 查询方向。 @param self 目标表头。 @return 0=水平，1=垂直。 */
int XHeaderView_orientation(const XHeaderView* self);
/** @brief 查询区间数。 @param self 目标表头。 @return 区间数。 */
int XHeaderView_count(const XHeaderView* self);
/** @brief 设置区间数（新增用默认尺寸；缩小编排尾部）。
 * @param self 目标表头。
 * @param count 区间数（>=0）。
 * @return 无返回值。
 */
void XHeaderView_setCount(XHeaderView* self, int count);
/** @brief 读取区间尺寸。
 * @param self 目标表头。
 * @param section 区间号（越界返回默认尺寸）。
 * @return 区间尺寸（像素）。
 */
int XHeaderView_sectionSize(const XHeaderView* self, int section);
/** @brief 设置区间尺寸。
 * @param self 目标表头。
 * @param section 区间号（越界忽略）。
 * @param size 尺寸（像素；<=0 忽略）。
 * @return 无返回值。
 */
void XHeaderView_setSectionSize(XHeaderView* self, int section, int size);
/** @brief 查询默认区间尺寸。 @param self 目标表头。 @return 默认尺寸。 */
int XHeaderView_defaultSectionSize(const XHeaderView* self);
/** @brief 设置默认区间尺寸。 @param self 目标表头。 @param size 尺寸（像素）。 */
void XHeaderView_setDefaultSectionSize(XHeaderView* self, int size);
/** @brief 查询区间起始位置（对标 sectionPosition）。
 * @param self 目标表头。
 * @param section 区间号。
 * @return 起始坐标（像素）；越界返回 -1。
 */
int XHeaderView_sectionPosition(const XHeaderView* self, int section);
/** @brief 末尾拉伸开关。
 * @param self 目标表头。
 * @param stretch true 拉伸。
 * @return 无返回值。
 */
void XHeaderView_setStretchLastSection(XHeaderView* self, bool stretch);
/** @brief 查询末尾拉伸开关。 @param self 目标表头。 @return 拉伸返回 true。 */
bool XHeaderView_isStretchLastSection(const XHeaderView* self);

/* ==================== 段管理（对标 QHeaderView 段族） ==================== */

/** @brief 隐藏区间（对标 hideSection）。 @param self 目标表头。 @param section 区间号（越界忽略）。 @return 无返回值。 */
void XHeaderView_hideSection(XHeaderView* self, int section);
/** @brief 显示区间（对标 showSection）。 @param self 目标表头。 @param section 区间号（越界忽略）。 @return 无返回值。 */
void XHeaderView_showSection(XHeaderView* self, int section);
/** @brief 查询区间是否隐藏（对标 isSectionHidden）。 @param self 目标表头。 @param section 区间号。 @return 隐藏返回 true；越界返回 false。 */
bool XHeaderView_isSectionHidden(const XHeaderView* self, int section);
/** @brief 查询隐藏区间数（对标 hiddenSectionCount）。 @param self 目标表头。 @return 隐藏数。 */
int XHeaderView_hiddenSectionCount(const XHeaderView* self);
/** @brief 设置区间可点击（对标 setSectionsClickable）。 @param self 目标表头。 @param clickable true 可点击。 @return 无返回值。 */
void XHeaderView_setSectionsClickable(XHeaderView* self, bool clickable);
/** @brief 查询区间可点击（对标 sectionsClickable）。 @param self 目标表头。 @return 可点击返回 true。 */
bool XHeaderView_sectionsClickable(const XHeaderView* self);
/** @brief 设置区间可拖动（对标 setSectionsMovable）。 @param self 目标表头。 @param movable true 可拖动。 @return 无返回值。 */
void XHeaderView_setSectionsMovable(XHeaderView* self, bool movable);
/** @brief 查询区间可拖动（对标 sectionsMovable）。 @param self 目标表头。 @return 可拖动返回 true。 */
bool XHeaderView_sectionsMovable(const XHeaderView* self);
/** @brief 移动区间（对标 moveSection；from 位置移到 to）。 @param self 目标表头。 @param from 源区间号。 @param to 目标区间号。 @return 无返回值。 */
void XHeaderView_moveSection(XHeaderView* self, int from, int to);
/** @brief 交换两个区间（对标 swapSections）。 @param self 目标表头。 @param first 区间号。 @param second 区间号。 @return 无返回值。 */
void XHeaderView_swapSections(XHeaderView* self, int first, int second);
/** @brief 设置排序指示器（对标 setSortIndicator；发射 sortIndicatorChanged）。
 * @param self 目标表头。 @param section 区间号。 @param order 排序方向。 @return 无返回值。 */
void XHeaderView_setSortIndicator(XHeaderView* self, int section, int order);
/** @brief 查询排序指示器区间（对标 sortIndicatorSection）。 @param self 目标表头。 @return 区间号（-1 无）。 */
int XHeaderView_sortIndicatorSection(const XHeaderView* self);
/** @brief 查询排序方向（对标 sortIndicatorOrder）。 @param self 目标表头。 @return 排序方向。 */
int XHeaderView_sortIndicatorOrder(const XHeaderView* self);
/** @brief 设置排序指示器可见（对标 setSortIndicatorShown）。 @param self 目标表头。 @param shown true 显示。 @return 无返回值。 */
void XHeaderView_setSortIndicatorShown(XHeaderView* self, bool shown);
/** @brief 查询排序指示器可见（对标 isSortIndicatorShown）。 @param self 目标表头。 @return 显示返回 true。 */
bool XHeaderView_isSortIndicatorShown(const XHeaderView* self);

/* ==================== 视觉序与位置反查（对标 QHeaderView visualIndex 族） ==================== */

/**
 * @brief 查询区间的视觉序号（对标 QHeaderView::visualIndex）。
 *
 *        @note 当前实现中 moveSection/swapSections 直接重排尺寸表，
 *        "移动即重排"，逻辑序与视觉序恒一致，故恒等返回 logicalIndex；
 *        若后续引入独立视觉映射表则替换此实现。
 *
 * @param self 目标表头；可为 NULL。
 * @param logicalIndex 逻辑区间号。
 * @return 视觉序号；越界或 self 为 NULL 时返回 -1。
 */
int XHeaderView_visualIndex(const XHeaderView* self, int logicalIndex);
/**
 * @brief 查询视觉序号对应的逻辑区间号（对标 QHeaderView::visualIndexAt）。
 *
 *        @note 同 visualIndex：移动即重排，视觉序与逻辑序恒一致，
 *        故恒等返回 visualIndex；若后续引入独立视觉映射表则替换此实现。
 *
 * @param self 目标表头；可为 NULL。
 * @param visualIndex 视觉序号。
 * @return 逻辑区间号；越界或 self 为 NULL 时返回 -1。
 */
int XHeaderView_visualIndexAt(const XHeaderView* self, int visualIndex);
/**
 * @brief 像素位置反查逻辑区间号（对标 QHeaderView::logicalIndexAt）。
 *
 *        @note 位置坐标与 sectionPosition 同一坐标系（逐段累加
 *        sectionSize 反查；当前模型下隐藏区间仍占位，不跳过）。
 *
 * @param self 目标表头；可为 NULL。
 * @param position 相对表头起点的像素位置。
 * @return 覆盖该位置的区间号；位置越界（<0 或超出总长）或 self 为
 *         NULL 时返回 -1。
 */
int XHeaderView_logicalIndexAt(const XHeaderView* self, int position);
/**
 * @brief 查询区间尺寸提示（对标 QHeaderView::sectionSizeHint）。
 *
 *        @note 无内容感知：恒返回 defaultSectionSize，不依据区间
 *        内容测算（与 Qt 按内容提示的行为不同）。
 *
 * @param self 目标表头；可为 NULL。
 * @param logicalIndex 逻辑区间号（当前不参与计算）。
 * @return 建议尺寸（像素）；self 为 NULL 时返回 0。
 */
int XHeaderView_sectionSizeHint(const XHeaderView* self, int logicalIndex);
/**
 * @brief 查询表头总长（对标 QHeaderView::length）：全部可见段尺寸之和。
 *
 *        @note 跳过隐藏区间（依据现有 m_hidden 表），与 Qt 中隐藏段
 *        长度按 0 计的语义一致。
 *
 * @param self 目标表头；可为 NULL。
 * @return 可见段尺寸总和（像素）；self 为 NULL 时返回 0。
 */
int XHeaderView_length(const XHeaderView* self);
/**
 * @brief 查询表头滚动偏移（对标 QHeaderView::offset）。
 *
 *        @note 当前实现无滚动偏移承载，恒返回 0；故不提供 setOffset
 *        写入接口（避免死存储）。
 *
 * @param self 目标表头；可为 NULL。
 * @return 恒为 0。
 */
int XHeaderView_offset(const XHeaderView* self);

/** @brief sectionClicked(int) 信号（对标 QHeaderView::sectionClicked；载荷：区间号）。 */
void* XHeaderView_sectionClicked_signal(XHeaderView* self, int section);
/** @brief sortIndicatorChanged(int,int) 信号（对标 QHeaderView::sortIndicatorChanged；载荷：区间号、方向）。 */
void* XHeaderView_sortIndicatorChanged_signal(XHeaderView* self, int logicalIndex, int order);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XHEADERVIEW_H */
