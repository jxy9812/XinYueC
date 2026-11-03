/**
 * @file       XBoxPlotModelMapper.h
 * @brief      XBoxPlotModelMapper 盒须序列模型映射器基类（对标 Qt Charts 6.8 QBoxPlotModelMapper）。
 * @details    把 XAbstractItemModel 的单元格逐格映射为 XBoxSet 的 5 个取值槽位。
 *             Qt 中该类构造函数为 protected、仅经 QHBoxPlotModelMapper /
 *             QVBoxPlotModelMapper 使用。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBOXPLOTMODELMAPPER_H
#define XBOXPLOTMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XBoxPlotSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBoxPlotModelMapper)
XCLASS_DEFINE_EXTEND_END(XBoxPlotModelMapper, XObject)

/** @brief QBoxPlotModelMapper 对应实现。 */
typedef struct XBoxPlotModelMapper
{
    XObject m_base;                /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XAbstractItemModel* m_model;   /**< 数据模型（借用；不拥有）。 */
    XBoxPlotSeries* m_series; /**< 目标序列（借用；不拥有）。 */

    /* ---- 4 字节对齐组 ---- */
    int m_first;                   /**< 起始行/列（对标 first）。 */
    int m_count;                   /**< 映射数量（-1=按模型推导）。 */
    int m_orientation;             /**< XModelMapper_Orientation（0=横向 1=纵向）。 */
    int m_firstBoxSetSection;       /**< 首个盒组所在行/列（对标 firstBoxSetSection）。 */
    int m_lastBoxSetSection;        /**< 末个盒组所在行/列（对标 lastBoxSetSection）。 */
    int m_firstBoxSetRow;   /**< m_firstBoxSetRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_lastBoxSetRow;   /**< m_lastBoxSetRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstBoxSetColumn;   /**< m_firstBoxSetColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_lastBoxSetColumn;   /**< m_lastBoxSetColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstColumn;   /**< m_firstColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_columnCount;   /**< m_columnCount 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstRow;   /**< m_firstRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_rowCount;   /**< m_rowCount 专有行/列参数（对标 Qt 同名属性）。 */
} XBoxPlotModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XBoxPlotModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XBoxPlotModelMapper_init(XBoxPlotModelMapper* self);
/** @brief 堆上创建映射器（基类直建，一般不单独使用）。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XBoxPlotModelMapper* XBoxPlotModelMapper_create_ex(XMemoryType memory);
#define XBoxPlotModelMapper_create() XBoxPlotModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 模型与序列（对标 QBoxPlotModelMapper） ==================== */

/** @brief 设置数据模型（借用，不转移所有权）。 @param self 目标映射器指针。 @param model 数据模型；NULL 解除绑定。 @return 无返回值。 */
void XBoxPlotModelMapper_setModel(XBoxPlotModelMapper* self, XAbstractItemModel* model);
/** @brief 查询数据模型（借用）。 @param self 目标映射器指针。 @return 借用 XAbstractItemModel*；未绑定 NULL。 */
XAbstractItemModel* XBoxPlotModelMapper_model(const XBoxPlotModelMapper* self);
/** @brief 设置目标序列（借用，不转移所有权）。 @param self 目标映射器指针。 @param series 目标序列；NULL 解除绑定。 @return 无返回值。 */
void XBoxPlotModelMapper_setSeries(XBoxPlotModelMapper* self, XBoxPlotSeries* series);
/** @brief 查询目标序列（借用）。 @param self 目标映射器指针。 @return 借用 XBoxPlotSeries*；未绑定 NULL。 */
XBoxPlotSeries* XBoxPlotModelMapper_series(const XBoxPlotModelMapper* self);

/* ==================== 通用映射参数 ==================== */

/** @brief 设置起始行/列（对标 setFirst）。 @param self 目标映射器指针。 @param first 起始下标（<0 忽略）。 @return 无返回值。 */
void XBoxPlotModelMapper_setFirst(XBoxPlotModelMapper* self, int first);
/** @brief 查询起始行/列（对标 first）。 @param self 目标映射器指针。 @return 起始下标。 */
int XBoxPlotModelMapper_first(const XBoxPlotModelMapper* self);
/** @brief 设置映射数量（对标 setCount）。 @param self 目标映射器指针。 @param count 数量（-1=按模型推导）。 @return 无返回值。 */
void XBoxPlotModelMapper_setCount(XBoxPlotModelMapper* self, int count);
/** @brief 查询映射数量（对标 count）。 @param self 目标映射器指针。 @return 数量；-1 表示按模型推导。 */
int XBoxPlotModelMapper_count(const XBoxPlotModelMapper* self);
/** @brief 设置映射方向（对标 setOrientation）。 @param self 目标映射器指针。 @param orientation 0=横向 1=纵向。 @return 无返回值。 */
void XBoxPlotModelMapper_setOrientation(XBoxPlotModelMapper* self, int orientation);
/** @brief 查询映射方向（对标 orientation）。 @param self 目标映射器指针。 @return 0=横向 1=纵向。 */
int XBoxPlotModelMapper_orientation(const XBoxPlotModelMapper* self);

/* ==================== 专有映射参数 ==================== */
/** @brief 设置首个盒组所在行/列（对标 setFirstBoxSetSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XBoxPlotModelMapper_setFirstBoxSetSection(XBoxPlotModelMapper* self, int section);
/** @brief 查询首个盒组所在行/列（对标 firstBoxSetSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XBoxPlotModelMapper_firstBoxSetSection(const XBoxPlotModelMapper* self);
/** @brief 设置末个盒组所在行/列（对标 setLastBoxSetSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XBoxPlotModelMapper_setLastBoxSetSection(XBoxPlotModelMapper* self, int section);
/** @brief 查询末个盒组所在行/列（对标 lastBoxSetSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XBoxPlotModelMapper_lastBoxSetSection(const XBoxPlotModelMapper* self);

/* ==================== 同步（Qt 由信号驱动；本库显式触发） ==================== */

/**
 * @brief 立即把模型数据同步到目标序列（清空后重建）。
 * @param self 目标映射器指针。
 * @return 至少映射成功一项返回 true；未绑定或区间非法返回 false。
 */
bool XBoxPlotModelMapper_remap(XBoxPlotModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XBOXPLOTMODELMAPPER_H */
