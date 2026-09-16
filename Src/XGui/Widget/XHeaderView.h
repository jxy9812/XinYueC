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
} XHeaderView;

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

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XHEADERVIEW_H */
