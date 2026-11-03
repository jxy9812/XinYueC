/**
 * @file       XVCandlestickModelMapper.h
 * @brief      纵向 K 线模型映射器（对标 Qt Charts 6.8 QVCandlestickModelMapper）。
 * @details    继承 XCandlestickModelMapper（对应 QVCandlestickModelMapper :
 *             QCandlestickModelMapper）。纵向语义：五个字段各自指定**行**号，
 *             firstSetColumn/lastSetColumn 沿**列**推进。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVCANDLESTICKMODELMAPPER_H
#define XVCANDLESTICKMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XCandlestickModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XVCandlestickModelMapper)
XCLASS_DEFINE_EXTEND_END(XVCandlestickModelMapper, XCandlestickModelMapper)

/** @brief 纵向 K 线模型映射器（对标 QVCandlestickModelMapper）。 */
typedef struct XVCandlestickModelMapper
{
    XCandlestickModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XVCandlestickModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XVCandlestickModelMapper_class_init(void);
/** @brief 初始化嵌入式纵向 K 线映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XVCandlestickModelMapper_init(XVCandlestickModelMapper* self);
/** @brief 堆上创建纵向 K 线映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XVCandlestickModelMapper* XVCandlestickModelMapper_create_ex(XMemoryType memory);
#define XVCandlestickModelMapper_create() \
    XVCandlestickModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== V 向专有参数（对标 QVCandlestickModelMapper） ==================== */

/** @brief 设置时间戳所在行（对标 setTimestampRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setTimestampRow(XVCandlestickModelMapper* self,
                                              int row);
/** @brief 查询时间戳所在行（对标 timestampRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XVCandlestickModelMapper_timestampRow(const XVCandlestickModelMapper* self);
/** @brief 设置开盘所在行（对标 setOpenRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setOpenRow(XVCandlestickModelMapper* self, int row);
/** @brief 查询开盘所在行（对标 openRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XVCandlestickModelMapper_openRow(const XVCandlestickModelMapper* self);
/** @brief 设置最高所在行（对标 setHighRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setHighRow(XVCandlestickModelMapper* self, int row);
/** @brief 查询最高所在行（对标 highRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XVCandlestickModelMapper_highRow(const XVCandlestickModelMapper* self);
/** @brief 设置最低所在行（对标 setLowRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setLowRow(XVCandlestickModelMapper* self, int row);
/** @brief 查询最低所在行（对标 lowRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XVCandlestickModelMapper_lowRow(const XVCandlestickModelMapper* self);
/** @brief 设置收盘所在行（对标 setCloseRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setCloseRow(XVCandlestickModelMapper* self, int row);
/** @brief 查询收盘所在行（对标 closeRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XVCandlestickModelMapper_closeRow(const XVCandlestickModelMapper* self);
/** @brief 设置首个 set 所在列（对标 setFirstSetColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setFirstSetColumn(XVCandlestickModelMapper* self,
                                                int column);
/** @brief 查询首个 set 所在列（对标 firstSetColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XVCandlestickModelMapper_firstSetColumn(const XVCandlestickModelMapper* self);
/** @brief 设置末个 set 所在列（对标 setLastSetColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XVCandlestickModelMapper_setLastSetColumn(XVCandlestickModelMapper* self,
                                               int column);
/** @brief 查询末个 set 所在列（对标 lastSetColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XVCandlestickModelMapper_lastSetColumn(const XVCandlestickModelMapper* self);

/* ==================== 信号（对标 QVCandlestickModelMapper Q_SIGNALS） ==================== */

/** @brief timestampRowChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_timestampRowChanged_signal(XVCandlestickModelMapper* self);
/** @brief openRowChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_openRowChanged_signal(XVCandlestickModelMapper* self);
/** @brief highRowChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_highRowChanged_signal(XVCandlestickModelMapper* self);
/** @brief lowRowChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_lowRowChanged_signal(XVCandlestickModelMapper* self);
/** @brief closeRowChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_closeRowChanged_signal(XVCandlestickModelMapper* self);
/** @brief firstSetColumnChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_firstSetColumnChanged_signal(XVCandlestickModelMapper* self);
/** @brief lastSetColumnChanged 信号（无载荷）。 */
void* XVCandlestickModelMapper_lastSetColumnChanged_signal(XVCandlestickModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XVCANDLESTICKMODELMAPPER_H */
