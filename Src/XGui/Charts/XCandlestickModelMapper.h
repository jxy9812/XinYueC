/**
 * @file       XCandlestickModelMapper.h
 * @brief      XCandlestickModelMapper K 线序列模型映射器基类（对标 Qt Charts
 *             6.8 QCandlestickModelMapper）。
 * @details    把 XAbstractItemModel 的行列数据映射为若干 XCandlestickSet，
 *             每个 set 需要时间戳/开盘/最高/最低/收盘五列（或五行，视方向）。
 *
 *             与 Qt 一致：本基类只持有「五个字段各自的 section 下标」与
 *             「set 区间」，具体方向（哪一个是行、哪一个是列）由
 *             XHCandlestickModelMapper / XVCandlestickModelMapper 决定。
 *             Qt 中这些 setter/getter 为 protected；本库为 C 侧可用性
 *             统一公开（语义与 Qt 一致，命名沿用 Qt 的 setXxx/xxx）。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCANDLESTICKMODELMAPPER_H
#define XCANDLESTICKMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractItemModel.h"
#include "XModelMapper_Protected.h"
#include "XCandlestickSeries.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XCandlestickModelMapper)
XCLASS_DEFINE_EXTEND_END(XCandlestickModelMapper, XObject)

/** @brief K 线序列模型映射器基类（对标 QCandlestickModelMapper）。 */
typedef struct XCandlestickModelMapper
{
    XObject m_base;                  /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组 ---- */
    XAbstractItemModel* m_model;     /**< 数据模型（借用；不拥有）。 */
    XCandlestickSeries* m_series;    /**< 目标序列（借用；不拥有）。 */

    /* ---- 4 字节对齐组 ---- */
    int m_timestampSection;          /**< 时间戳所在行/列（对标 timestamp）。 */
    int m_openSection;               /**< 开盘所在行/列（对标 open）。 */
    int m_highSection;               /**< 最高所在行/列（对标 high）。 */
    int m_lowSection;                /**< 最低所在行/列（对标 low）。 */
    int m_closeSection;              /**< 收盘所在行/列（对标 close）。 */
    int m_firstSetSection;           /**< 首个 set 所在行/列（对标 firstSetSection）。 */
    int m_lastSetSection;            /**< 末个 set 所在行/列（对标 lastSetSection）。 */
    int m_orientation;               /**< 0=横向 1=纵向。 */
} XCandlestickModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XCandlestickModelMapper_class_init(void);
/** @brief 初始化嵌入式映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XCandlestickModelMapper_init(XCandlestickModelMapper* self);
/** @brief 堆上创建映射器（基类直建，一般不单独使用）。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XCandlestickModelMapper* XCandlestickModelMapper_create_ex(XMemoryType memory);
#define XCandlestickModelMapper_create() \
    XCandlestickModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 模型与序列（对标 QCandlestickModelMapper） ==================== */

/** @brief 设置数据模型（借用，不转移所有权）。 @param self 目标映射器指针。 @param model 数据模型；NULL 解除绑定。 @return 无返回值。 */
void XCandlestickModelMapper_setModel(XCandlestickModelMapper* self,
                                      XAbstractItemModel* model);
/** @brief 查询数据模型（借用）。 @param self 目标映射器指针。 @return 借用 XAbstractItemModel*；未绑定 NULL。 */
XAbstractItemModel* XCandlestickModelMapper_model(
    const XCandlestickModelMapper* self);
/** @brief 设置目标序列（借用，不转移所有权）。 @param self 目标映射器指针。 @param series 目标序列；NULL 解除绑定。 @return 无返回值。 */
void XCandlestickModelMapper_setSeries(XCandlestickModelMapper* self,
                                       XCandlestickSeries* series);
/** @brief 查询目标序列（借用）。 @param self 目标映射器指针。 @return 借用 XCandlestickSeries*；未绑定 NULL。 */
XCandlestickSeries* XCandlestickModelMapper_series(
    const XCandlestickModelMapper* self);

/** @brief 查询映射方向（对标 orientation；Qt 为纯虚，由子类实现）。
 * @param self 目标映射器指针。
 * @return 0=横向 1=纵向。
 */
int XCandlestickModelMapper_orientation(const XCandlestickModelMapper* self);

/* ==================== 五字段 section（对标 QCandlestickModelMapper protected） ==================== */

/** @brief 设置时间戳所在行/列（对标 setTimestamp）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setTimestamp(XCandlestickModelMapper* self,
                                          int section);
/** @brief 查询时间戳所在行/列（对标 timestamp）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_timestamp(const XCandlestickModelMapper* self);
/** @brief 设置开盘所在行/列（对标 setOpen）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setOpen(XCandlestickModelMapper* self,
                                     int section);
/** @brief 查询开盘所在行/列（对标 open）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_open(const XCandlestickModelMapper* self);
/** @brief 设置最高所在行/列（对标 setHigh）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setHigh(XCandlestickModelMapper* self,
                                     int section);
/** @brief 查询最高所在行/列（对标 high）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_high(const XCandlestickModelMapper* self);
/** @brief 设置最低所在行/列（对标 setLow）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setLow(XCandlestickModelMapper* self, int section);
/** @brief 查询最低所在行/列（对标 low）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_low(const XCandlestickModelMapper* self);
/** @brief 设置收盘所在行/列（对标 setClose）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setClose(XCandlestickModelMapper* self,
                                      int section);
/** @brief 查询收盘所在行/列（对标 close）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_close(const XCandlestickModelMapper* self);

/** @brief 设置首个 set 所在行/列（对标 setFirstSetSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setFirstSetSection(XCandlestickModelMapper* self,
                                                int section);
/** @brief 查询首个 set 所在行/列（对标 firstSetSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_firstSetSection(
    const XCandlestickModelMapper* self);
/** @brief 设置末个 set 所在行/列（对标 setLastSetSection）。 @param self 目标映射器指针。 @param section 行/列下标。 @return 无返回值。 */
void XCandlestickModelMapper_setLastSetSection(XCandlestickModelMapper* self,
                                               int section);
/** @brief 查询末个 set 所在行/列（对标 lastSetSection）。 @param self 目标映射器指针。 @return 行/列下标；未设置 -1。 */
int XCandlestickModelMapper_lastSetSection(
    const XCandlestickModelMapper* self);

/* ==================== 同步（Qt 由信号驱动；本库显式触发） ==================== */

/**
 * @brief 立即把模型数据同步到目标序列（清空后重建）。
 * @param self 目标映射器指针。
 * @return 至少映射成功一组返回 true；未绑定或区间非法返回 false。
 */
bool XCandlestickModelMapper_remap(XCandlestickModelMapper* self);

/* ==================== 信号（对标 QCandlestickModelMapper Q_SIGNALS） ==================== */

/** @brief modelReplaced 信号（无载荷）。 */
void* XCandlestickModelMapper_modelReplaced_signal(XCandlestickModelMapper* self);
/** @brief seriesReplaced 信号（无载荷）。 */
void* XCandlestickModelMapper_seriesReplaced_signal(XCandlestickModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCANDLESTICKMODELMAPPER_H */
