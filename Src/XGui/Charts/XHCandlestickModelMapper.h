/**
 * @file       XHCandlestickModelMapper.h
 * @brief      横向 K 线模型映射器（对标 Qt Charts 6.8 QHCandlestickModelMapper）。
 * @details    继承 XCandlestickModelMapper（对应 QHCandlestickModelMapper :
 *             QCandlestickModelMapper）。横向语义：五个字段各自指定**列**号，
 *             firstSetRow/lastSetRow 沿**行**推进。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHCANDLESTICKMODELMAPPER_H
#define XHCANDLESTICKMODELMAPPER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XCandlestickModelMapper.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XHCandlestickModelMapper)
XCLASS_DEFINE_EXTEND_END(XHCandlestickModelMapper, XCandlestickModelMapper)

/** @brief 横向 K 线模型映射器（对标 QHCandlestickModelMapper）。 */
typedef struct XHCandlestickModelMapper
{
    XCandlestickModelMapper m_base; /**< 基类成员；必须是第一个。 */
} XHCandlestickModelMapper;

/** @brief 初始化类虚函数表。 @return 共享虚函数表指针。 */
XVtable* XHCandlestickModelMapper_class_init(void);
/** @brief 初始化嵌入式横向 K 线映射器。 @param self 目标映射器指针。 @return 无返回值。 */
void XHCandlestickModelMapper_init(XHCandlestickModelMapper* self);
/** @brief 堆上创建横向 K 线映射器。 @param memory 内存类型。 @return 新建对象；失败 NULL。 */
XHCandlestickModelMapper* XHCandlestickModelMapper_create_ex(XMemoryType memory);
#define XHCandlestickModelMapper_create() \
    XHCandlestickModelMapper_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== H 向专有参数（对标 QHCandlestickModelMapper） ==================== */

/** @brief 设置时间戳所在列（对标 setTimestampColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setTimestampColumn(XHCandlestickModelMapper* self,
                                                 int column);
/** @brief 查询时间戳所在列（对标 timestampColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XHCandlestickModelMapper_timestampColumn(const XHCandlestickModelMapper* self);
/** @brief 设置开盘所在列（对标 setOpenColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setOpenColumn(XHCandlestickModelMapper* self,
                                            int column);
/** @brief 查询开盘所在列（对标 openColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XHCandlestickModelMapper_openColumn(const XHCandlestickModelMapper* self);
/** @brief 设置最高所在列（对标 setHighColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setHighColumn(XHCandlestickModelMapper* self,
                                            int column);
/** @brief 查询最高所在列（对标 highColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XHCandlestickModelMapper_highColumn(const XHCandlestickModelMapper* self);
/** @brief 设置最低所在列（对标 setLowColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setLowColumn(XHCandlestickModelMapper* self,
                                           int column);
/** @brief 查询最低所在列（对标 lowColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XHCandlestickModelMapper_lowColumn(const XHCandlestickModelMapper* self);
/** @brief 设置收盘所在列（对标 setCloseColumn）。 @param self 目标映射器指针。 @param column 列下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setCloseColumn(XHCandlestickModelMapper* self,
                                             int column);
/** @brief 查询收盘所在列（对标 closeColumn）。 @param self 目标映射器指针。 @return 列下标；未设置 -1。 */
int XHCandlestickModelMapper_closeColumn(const XHCandlestickModelMapper* self);
/** @brief 设置首个 set 所在行（对标 setFirstSetRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setFirstSetRow(XHCandlestickModelMapper* self,
                                             int row);
/** @brief 查询首个 set 所在行（对标 firstSetRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XHCandlestickModelMapper_firstSetRow(const XHCandlestickModelMapper* self);
/** @brief 设置末个 set 所在行（对标 setLastSetRow）。 @param self 目标映射器指针。 @param row 行下标。 @return 无返回值。 */
void XHCandlestickModelMapper_setLastSetRow(XHCandlestickModelMapper* self,
                                            int row);
/** @brief 查询末个 set 所在行（对标 lastSetRow）。 @param self 目标映射器指针。 @return 行下标；未设置 -1。 */
int XHCandlestickModelMapper_lastSetRow(const XHCandlestickModelMapper* self);

/* ==================== 信号（对标 QHCandlestickModelMapper Q_SIGNALS） ==================== */

/** @brief timestampColumnChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_timestampColumnChanged_signal(XHCandlestickModelMapper* self);
/** @brief openColumnChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_openColumnChanged_signal(XHCandlestickModelMapper* self);
/** @brief highColumnChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_highColumnChanged_signal(XHCandlestickModelMapper* self);
/** @brief lowColumnChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_lowColumnChanged_signal(XHCandlestickModelMapper* self);
/** @brief closeColumnChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_closeColumnChanged_signal(XHCandlestickModelMapper* self);
/** @brief firstSetRowChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_firstSetRowChanged_signal(XHCandlestickModelMapper* self);
/** @brief lastSetRowChanged 信号（无载荷）。 */
void* XHCandlestickModelMapper_lastSetRowChanged_signal(XHCandlestickModelMapper* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XHCANDLESTICKMODELMAPPER_H */
