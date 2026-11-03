/**
 * @file       XBarModelMapper.h
 * @brief      XBarModelMapper 柱状序列模型映射器基类（对标 Qt Charts 6.8 QBarModelMapper）。
 * @details    把 XAbstractItemModel 的行列数据映射为若干 XBarSet。Qt 中该类
 *             构造函数为 protected、仅经 QHBarModelMapper / QVBarModelMapper
 *             使用；本库同样以「基类 + H/V 具体子类」的类树承载。
 *
 *             方向语义（与 Qt 一致）：
 *             - 横向（H）：firstBarSetSection 等是**行**号，按**列**推进；
 *             - 纵向（V）：是**列**号，按**行**推进。
 * @note       模块总开关 XCHARTS_ON；实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBARMODELMAPPER_H
#define XBARMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XAbstractBarSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XBarModelMapper)
XCLASS_DEFINE_EXTEND_END(XBarModelMapper, XObject)

/** @brief QBarModelMapper 对应实现。 */
typedef struct XBarModelMapper
{
    XObject m_base;                /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XAbstractItemModel* m_model;   /**< 数据模型（借用；不拥有）。 */
    XAbstractBarSeries* m_series; /**< 目标序列（借用；不拥有）。 */

    /* ---- 4 字节对齐组 ---- */
    int m_first;                   /**< 起始行/列（对标 first）。 */
    int m_count;                   /**< 映射数量（-1=按模型推导）。 */
    int m_orientation;             /**< XModelMapper_Orientation（0=横向 1=纵向）。 */
    int m_firstBarSetSection;       /**< 首个柱组所在行/列（对标 firstBarSetSection）。 */
    int m_lastBarSetSection;        /**< 末个柱组所在行/列（对标 lastBarSetSection）。 */
    int m_firstBarSetRow;   /**< m_firstBarSetRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_lastBarSetRow;   /**< m_lastBarSetRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstBarSetColumn;   /**< m_firstBarSetColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_lastBarSetColumn;   /**< m_lastBarSetColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstColumn;   /**< m_firstColumn 专有行/列参数（对标 Qt 同名属性）。 */
    int m_columnCount;   /**< m_columnCount 专有行/列参数（对标 Qt 同名属性）。 */
    int m_firstRow;   /**< m_firstRow 专有行/列参数（对标 Qt 同名属性）。 */
    int m_rowCount;   /**< m_rowCount 专有行/列参数（对标 Qt 同名属性）。 */
} XBarModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XBarModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XBarModelMapper_init(XBarModelMapper* self);
/** @brief 堆上创建映射器（基类直建，一般不单独使用）。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XBarModelMapper* XBarModelMapper_create_ex(XMemoryType memory);
#define XBarModelMapper_create() XBarModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 模型与序列（对标 QBarModelMapper） ==================== */

/** @brief 设置数据模型（借用，不转移所有权）。 @param self 目标映射器指针。 @param model 数据模型；NULL 解除绑定。 @return 无返回值。 */
void XBarModelMapper_setModel(XBarModelMapper* self, XAbstractItemModel* model);
/** @brief 查询数据模型（借用）。 @param self 目标映射器指针。 @return 借用 XAbstractItemModel*；未绑定 NULL。 */
XAbstractItemModel* XBarModelMapper_model(const XBarModelMapper* self);
/** @brief 设置目标序列（借用，不转移所有权）。 @param self 目标映射器指针。 @param series 目标序列；NULL 解除绑定。 @return 无返回值。 */
void XBarModelMapper_setSeries(XBarModelMapper* self, XAbstractBarSeries* series);
/** @brief 查询目标序列（借用）。 @param self 目标映射器指针。 @return 借用 XAbstractBarSeries*；未绑定 NULL。 */
XAbstractBarSeries* XBarModelMapper_series(const XBarModelMapper* self);

/* ==================== 通用映射参数 ==================== */

/** @brief 设置起始行/列（对标 setFirst）。 @param self 目标映射器指针。 @param first 起始下标（<0 忽略）。 @return 无返回值。 */
void XBarModelMapper_setFirst(XBarModelMapper* self, int first);
/** @brief 查询起始行/列（对标 first）。 @param self 目标映射器指针。 @return 起始下标。 */
int XBarModelMapper_first(const XBarModelMapper* self);
/** @brief 设置映射数量（对标 setCount）。 @param self 目标映射器指针。 @param count 数量（-1=按模型推导）。 @return 无返回值。 */
void XBarModelMapper_setCount(XBarModelMapper* self, int count);
/** @brief 查询映射数量（对标 count）。 @param self 目标映射器指针。 @return 数量；-1 表示按模型推导。 */
int XBarModelMapper_count(const XBarModelMapper* self);
/** @brief 设置映射方向（对标 setOrientation）。 @param self 目标映射器指针。 @param orientation 0=横向 1=纵向。 @return 无返回值。 */
void XBarModelMapper_setOrientation(XBarModelMapper* self, int orientation);
/** @brief 查询映射方向（对标 orientation）。 @param self 目标映射器指针。 @return 0=横向 1=纵向。 */
int XBarModelMapper_orientation(const XBarModelMapper* self);

/* ==================== 专有映射参数 ==================== */
/** @brief 设置首个柱组所在行/列（对标 setFirstBarSetSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XBarModelMapper_setFirstBarSetSection(XBarModelMapper* self, int section);
/** @brief 查询首个柱组所在行/列（对标 firstBarSetSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XBarModelMapper_firstBarSetSection(const XBarModelMapper* self);
/** @brief 设置末个柱组所在行/列（对标 setLastBarSetSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XBarModelMapper_setLastBarSetSection(XBarModelMapper* self, int section);
/** @brief 查询末个柱组所在行/列（对标 lastBarSetSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置返回 -1。 */
int XBarModelMapper_lastBarSetSection(const XBarModelMapper* self);

/* ==================== 同步（Qt 由信号驱动；本库显式触发） ==================== */

/**
 * @brief 立即把模型数据同步到目标序列（清空后重建）。
 * @param self 目标映射器指针。
 * @return 至少映射成功一项返回 true；未绑定或区间非法返回 false。
 */
bool XBarModelMapper_remap(XBarModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XBARMODELMAPPER_H */
