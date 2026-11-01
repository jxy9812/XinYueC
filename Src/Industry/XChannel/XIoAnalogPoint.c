/**
 * @file       XIoAnalogPoint.c
 * @brief      轻量模拟量 IO 点实现（XIoPoint 派生）。
 * @details    标度换算（AI 正向 / AO 逆向）在此收口，供组与数据源子类
 *             （XModbusPoint 等）共用。无堆成员；虚表继承 XIoPoint。
 */
#include "XIoAnalogPoint.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>

/**
 * @brief 点析构：无堆成员，仅走父类链。
 * @param point 目标点；可为 NULL。
 */
static void VXIoAnalogPoint_deinit(XIoAnalogPoint* point)
{
    if (!point) return;
    XClass_Deinit_Parent(XIoPoint, (XIoPoint*)point);
}

XVtable* XIoAnalogPoint_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIoAnalogPoint)
    XVTABLE_INHERIT_XCLASS(XIoPoint);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIoAnalogPoint_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XIoAnalogPoint);
    return XVTABLE_DEFAULT;
}

void XIoAnalogPoint_init(XIoAnalogPoint* point)
{
    if (!point) return;
    XIoPoint_init(&point->m_base);
    XClassGetVtable(point) = XIoAnalogPoint_class_init();
    memset(&point->m_scaleNum, 0, sizeof(XIoAnalogPoint) - sizeof(XIoPoint));
}

int32_t xIoAnalogPointToEngineering(const XIoAnalogPoint* point, int32_t raw)
{
    int64_t den = (point->m_scaleDen == 0) ? 1 : (int64_t)point->m_scaleDen;
    int64_t scaled = (int64_t)raw * (int64_t)point->m_scaleNum;
    return (int32_t)(scaled / den + (int64_t)point->m_offset);
}

uint16_t xIoAnalogPointToRaw(const XIoAnalogPoint* point, int32_t value)
{
    int64_t den = (point->m_scaleDen == 0) ? 1 : (int64_t)point->m_scaleDen;
    int64_t num = ((int64_t)value - (int64_t)point->m_offset) * den;
    int64_t numAbs = (num < 0) ? -num : num;
    int64_t snAbs = (point->m_scaleNum < 0) ? -(int64_t)point->m_scaleNum
                                            : (int64_t)point->m_scaleNum;
    int64_t quotient;
    int64_t remainder2;
    if (snAbs == 0) snAbs = 1;
    quotient = numAbs / snAbs;
    remainder2 = (numAbs % snAbs) * 2;
    if (remainder2 >= snAbs) quotient += 1;
    if (num < 0) quotient = -quotient;
    if (quotient < 0) quotient = 0;
    if (quotient > 65535) quotient = 65535;
    return (uint16_t)quotient;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
