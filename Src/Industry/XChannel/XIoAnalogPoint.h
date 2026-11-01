/**
 * @file       XIoAnalogPoint.h
 * @brief      轻量模拟量 IO 点（XIoPoint 派生：头嵌入 + 标度四件套）。64B。
 * @details    [2026-11-01 轻量化] C 首成员继承 XIoPoint，基指针可安全互转、
 *             XClassDelete 兼容基指针释放（虚表 deinit 链）。
 *             标度语义：AI eng=raw*Num/Den+Offset（Den 0 视 1，向零取整）；
 *             AO raw=(v-Offset)*Den/Num（Num 0 视 1，余数就近取整 .5 远离
 *             零，钳位 0..65535）。m_deadband 仅 AI 消费（AO 不用）。
 */
#ifndef XIOANALOGPOINT_H
#define XIOANALOGPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoPoint.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

XCLASS_DEFINE_BEGING(XIoAnalogPoint)
XCLASS_DEFINE_EXTEND_END(XIoAnalogPoint, XIoPoint)

/**
 * @brief XIoAnalogPoint 轻量模拟量 IO 点（64B）。
 */
typedef struct XIoAnalogPoint {
    XIoPoint m_base;      /**< 基点（首成员继承；基/派生指针可互转）。 */
    int32_t m_scaleNum;   /**< 标度分子。 */
    int32_t m_scaleDen;   /**< 标度分母；0 视为 1。 */
    int32_t m_offset;     /**< 工程值偏移量。 */
    int32_t m_deadband;   /**< AI 变化死区（工程值）；负值按 0。AO 不消费。 */
} XIoAnalogPoint;

/**
 * @brief 初始化 XIoAnalogPoint 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XIoAnalogPoint_class_init(void);

/**
 * @brief 栈上初始化模拟量点（基段初始化 + 标度尾段清零 + 设虚表）。
 * @param point 目标点；不能为 NULL，可为未初始化内存。
 * @return 无。
 */
void XIoAnalogPoint_init(XIoAnalogPoint* point);

/**
 * @brief AI 原始码 → 定点工程值：raw * scaleNum / scaleDen + offset。
 * @param point 目标模拟量点（提供标度）；不能为 NULL。
 * @param raw 原始码。
 * @return 定点工程值（int64 中间量；scaleDen 0 视 1；向零取整）。
 */
int32_t xIoAnalogPointToEngineering(const XIoAnalogPoint* point, int32_t raw);

/**
 * @brief AO 定点工程值 → 原始码：(value - offset) * scaleDen / scaleNum。
 * @param point 目标模拟量点（提供标度）；不能为 NULL。
 * @param value 定点工程值。
 * @return 16 位原始码（钳位 0..65535；.5 远离零取整；scaleNum 0 视 1）。
 */
uint16_t xIoAnalogPointToRaw(const XIoAnalogPoint* point, int32_t value);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOANALOGPOINT_H */
