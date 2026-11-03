/**
 * @file       XXYModelMapper.h
 * @brief      XXYModelMapper XY 序列模型映射器基类（对标 Qt Charts 6.8
 *             QXYModelMapper）。
 * @details    把 XAbstractItemModel 的行列数据映射为 XY 点集。Qt 中该类
 *             构造函数为 protected、仅经 QHXYModelMapper / QVXYModelMapper
 *             使用；本库同样以「基类 + H/V 具体子类」的类树承载。
 *
 *             方向语义（与 Qt 一致）：
 *             - 横向（H）：xSection/ySection 是**行**号，按**列**推进；
 *             - 纵向（V）：xSection/ySection 是**列**号，按**行**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XXYMODELMAPPER_H
#define XXYMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XAbstractItemModel.h"
#include "XXYSeries.h"

#if XCHARTS_ON

#include "XModelMapper_Protected.h"

XCLASS_DEFINE_BEGING(XXYModelMapper)
XCLASS_DEFINE_EXTEND_END(XXYModelMapper, XObject)

/** @brief XY 序列模型映射器基类（对标 QXYModelMapper）。 */
typedef struct XXYModelMapper
{
    XObject m_base;                /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XAbstractItemModel* m_model;   /**< 数据模型（借用；不拥有）。 */
    XXYSeries* m_series;           /**< 目标 XY 序列（借用；不拥有）。 */

    /* ---- 4 字节对齐组 ---- */
    int m_first;                   /**< 起始行/列（对标 first）。 */
    int m_count;                   /**< 映射数量（-1=按模型推导）。 */
    int m_orientation;             /**< XModelMapper_Orientation。 */
    int m_xSection;                /**< x 所在行/列（对标 xSection）。 */
    int m_ySection;                /**< y 所在行/列（对标 ySection）。 */
} XXYModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XXYModelMapper_class_init(void);

/**
 * @brief 初始化嵌入式 XY 模型映射器。
 * @param self 目标映射器指针，不能为空。
 * @return 无返回值。
 */
void XXYModelMapper_init(XXYModelMapper* self);

/**
 * @brief 堆上创建 XY 模型映射器（基类直建，一般不单独使用）。
 * @param memory 内存类型。
 * @return 映射器指针；分配失败返回 NULL。
 */
XXYModelMapper* XXYModelMapper_create_ex(XMemoryType memory);
#define XXYModelMapper_create() \
    XXYModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 模型与序列（对标 QXYModelMapper） ==================== */

/** @brief 设置数据模型（对标 setModel；借用，不转移所有权）。
 * @param self 目标映射器指针。
 * @param model 数据模型指针；可为 NULL（解除绑定）。
 * @return 无返回值。
 */
void XXYModelMapper_setModel(XXYModelMapper* self, XAbstractItemModel* model);
/** @brief 查询数据模型（借用；对标 model）。
 * @param self 目标映射器指针。
 * @return 借用 XAbstractItemModel*；未绑定返回 NULL。调用者不得释放。
 */
XAbstractItemModel* XXYModelMapper_model(const XXYModelMapper* self);
/** @brief 设置目标 XY 序列（对标 setSeries；借用，不转移所有权）。
 * @param self 目标映射器指针。
 * @param series 目标序列指针；可为 NULL（解除绑定）。
 * @return 无返回值。
 */
void XXYModelMapper_setSeries(XXYModelMapper* self, XXYSeries* series);
/** @brief 查询目标 XY 序列（借用；对标 series）。
 * @param self 目标映射器指针。
 * @return 借用 XXYSeries*；未绑定返回 NULL。调用者不得释放。
 */
XXYSeries* XXYModelMapper_series(const XXYModelMapper* self);

/* ==================== 映射参数（对标 QXYModelMapper） ==================== */

/** @brief 设置起始行/列（对标 setFirst）。 @param self 目标映射器指针。 @param first 起始下标（<0 钳位 0）。 @return 无返回值。 */
void XXYModelMapper_setFirst(XXYModelMapper* self, int first);
/** @brief 查询起始行/列（对标 first）。 @param self 目标映射器指针。 @return 起始下标。 */
int XXYModelMapper_first(const XXYModelMapper* self);
/** @brief 设置映射数量（对标 setCount）。 @param self 目标映射器指针。 @param count 数量（-1=按模型推导）。 @return 无返回值。 */
void XXYModelMapper_setCount(XXYModelMapper* self, int count);
/** @brief 查询映射数量（对标 count）。 @param self 目标映射器指针。 @return 数量；-1 表示按模型推导。 */
int XXYModelMapper_count(const XXYModelMapper* self);
/** @brief 设置映射方向（对标 setOrientation）。 @param self 目标映射器指针。 @param orientation XModelMapper_Orientation。 @return 无返回值。 */
void XXYModelMapper_setOrientation(XXYModelMapper* self, int orientation);
/** @brief 查询映射方向（对标 orientation）。 @param self 目标映射器指针。 @return XModelMapper_Orientation。 */
int XXYModelMapper_orientation(const XXYModelMapper* self);
/** @brief 设置 x 所在行/列（对标 setXSection）。 @param self 目标映射器指针。 @param xSection 行/列下标。 @return 无返回值。 */
void XXYModelMapper_setXSection(XXYModelMapper* self, int xSection);
/** @brief 查询 x 所在行/列（对标 xSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XXYModelMapper_xSection(const XXYModelMapper* self);
/** @brief 设置 y 所在行/列（对标 setYSection）。 @param self 目标映射器指针。 @param ySection 行/列下标。 @return 无返回值。 */
void XXYModelMapper_setYSection(XXYModelMapper* self, int ySection);
/** @brief 查询 y 所在行/列（对标 ySection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XXYModelMapper_ySection(const XXYModelMapper* self);

/* ==================== 同步（Qt 由信号驱动；本库显式触发） ==================== */

/**
 * @brief 立即把模型数据同步到目标序列（清空后重建）。
 * @param self 目标映射器指针。
 * @return 至少映射成功一项返回 true；未绑定或区间非法返回 false。
 */
bool XXYModelMapper_remap(XXYModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XXYMODELMAPPER_H */
