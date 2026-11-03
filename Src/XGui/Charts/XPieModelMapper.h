/**
 * @file       XPieModelMapper.h
 * @brief      XPieModelMapper 饼图序列模型映射器基类（对标 Qt Charts 6.8 QPieModelMapper）。
 * @details    把 XAbstractItemModel 的行列数据映射为饼图切片（数值 + 标签）。
 *             Qt 中该类构造函数为 protected、仅经 QHPieModelMapper /
 *             QVPieModelMapper 使用；本库同样以「基类 + H/V 具体子类」承载。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPIEMODELMAPPER_H
#define XPIEMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XPieSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XPieModelMapper)
XCLASS_DEFINE_EXTEND_END(XPieModelMapper, XObject)

/** @brief QPieModelMapper 对应实现。 */
typedef struct XPieModelMapper
{
    XObject m_base;                /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XAbstractItemModel* m_model;   /**< 数据模型（借用；不拥有）。 */
    XPieSeries* m_series; /**< 目标序列（借用；不拥有）。 */

    /* ---- 4 字节对齐组 ---- */
    int m_first;                   /**< 起始行/列（对标 first）。 */
    int m_count;                   /**< 映射数量（-1=按模型推导）。 */
    int m_orientation;             /**< XModelMapper_Orientation（0=横向 1=纵向）。 */
    int m_valuesSection;            /**< 数值所在行/列（对标 valuesSection）。 */
    int m_labelsSection;            /**< 标签所在行/列（对标 labelsSection）。 */
    int m_valuesRow;   /**< m_valuesRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_labelsRow;   /**< m_labelsRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_valuesColumn;   /**< m_valuesColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_labelsColumn;   /**< m_labelsColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstColumn;   /**< m_firstColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_columnCount;   /**< m_columnCount 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstRow;   /**< m_firstRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_rowCount;   /**< m_rowCount 专有行/列参数（对标 Qt 同名属性）。 */
} XPieModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XPieModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XPieModelMapper_init(XPieModelMapper* self);
/** @brief 堆上创建映射器（基类直建，一般不单独使用）。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XPieModelMapper* XPieModelMapper_create_ex(XMemoryType memory);
#define XPieModelMapper_create() XPieModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 模型与序列（对标 QPieModelMapper） ==================== */

/** @brief 设置数据模型（借用，不转移所有权）。 @param self 目标映射器指针。 @param model 数据模型；NULL 解除绑定。 @return 无返回值。 */
void XPieModelMapper_setModel(XPieModelMapper* self, XAbstractItemModel* model);
/** @brief 查询数据模型（借用）。 @param self 目标映射器指针。 @return 借用 XAbstractItemModel*；未绑定 NULL。 */
XAbstractItemModel* XPieModelMapper_model(const XPieModelMapper* self);
/** @brief 设置目标序列（借用，不转移所有权）。 @param self 目标映射器指针。 @param series 目标序列；NULL 解除绑定。 @return 无返回值。 */
void XPieModelMapper_setSeries(XPieModelMapper* self, XPieSeries* series);
/** @brief 查询目标序列（借用）。 @param self 目标映射器指针。 @return 借用 XPieSeries*；未绑定 NULL。 */
XPieSeries* XPieModelMapper_series(const XPieModelMapper* self);

/* ==================== 通用映射参数 ==================== */

/** @brief 设置起始行/列（对标 setFirst）。 @param self 目标映射器指针。 @param first 起始下标（<0 忽略）。 @return 无返回值。 */
void XPieModelMapper_setFirst(XPieModelMapper* self, int first);
/** @brief 查询起始行/列（对标 first）。 @param self 目标映射器指针。 @return 起始下标。 */
int XPieModelMapper_first(const XPieModelMapper* self);
/** @brief 设置映射数量（对标 setCount）。 @param self 目标映射器指针。 @param count 数量（-1=按模型推导）。 @return 无返回值。 */
void XPieModelMapper_setCount(XPieModelMapper* self, int count);
/** @brief 查询映射数量（对标 count）。 @param self 目标映射器指针。 @return 数量；-1 表示按模型推导。 */
int XPieModelMapper_count(const XPieModelMapper* self);
/** @brief 设置映射方向（对标 setOrientation）。 @param self 目标映射器指针。 @param orientation 0=横向 1=纵向。 @return 无返回值。 */
void XPieModelMapper_setOrientation(XPieModelMapper* self, int orientation);
/** @brief 查询映射方向（对标 orientation）。 @param self 目标映射器指针。 @return 0=横向 1=纵向。 */
int XPieModelMapper_orientation(const XPieModelMapper* self);

/* ==================== 专有映射参数 ==================== */
/** @brief 设置数值所在行/列（对标 setValuesSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XPieModelMapper_setValuesSection(XPieModelMapper* self, int section);
/** @brief 查询数值所在行/列（对标 valuesSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XPieModelMapper_valuesSection(const XPieModelMapper* self);
/** @brief 设置标签所在行/列（对标 setLabelsSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XPieModelMapper_setLabelsSection(XPieModelMapper* self, int section);
/** @brief 查询标签所在行/列（对标 labelsSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XPieModelMapper_labelsSection(const XPieModelMapper* self);

/* ==================== 同步（Qt 由信号驱动；本库显式触发） ==================== */

/**
 * @brief 立即把模型数据同步到目标序列（清空后重建）。
 * @param self 目标映射器指针。
 * @return 至少映射成功一项返回 true；未绑定或区间非法返回 false。
 */
bool XPieModelMapper_remap(XPieModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPIEMODELMAPPER_H */
