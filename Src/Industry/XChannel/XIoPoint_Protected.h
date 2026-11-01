/**
 * @file       XIoPoint_Protected.h
 * @brief      轻量 IO 点保护 API（组对接助手；仅供点子类与组内部实现使用）。
 * @details    仅供 XIoPoint 的子类（XModbusPoint 等数据源点）与框架内部
 *             实现使用，不对外公开；普通业务代码不得包含本头文件（业务
 *             层只消费组的 inputChanged/writeDone 信号与组读写 API）。
 *             点子类在数据到达/写落定的语义节点经本头文件的组对接助手
 *             向登记组推送，组据此发射业务信号。
 */
#ifndef XIOPOINT_PROTECTED_H
#define XIOPOINT_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XIoPoint.h"
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/**
 * @brief [子类→组] 值喂入（保护接口）：更新点值/状态并经所属组发射
 *        inputChanged。
 * @param point 目标点；传入 NULL 时函数不执行任何操作。
 * @param value 新点值（点单位；DI/DO 取 0/1，AI/AO 为定点工程值）。
 * @return true=已发射（变化沿/首拍）；false=值未变或未登记组（仅更新
 *         本地缓存，不发射）。
 * @note 数据有效标志（m_valid）与首值推送标志（m_hasValue）由本函数
 *       维护；AI 的死区过滤由派生点自行判断后再调（本函数不做死区）。
 *       组周期 tick 拉取（ReadRaw）与子类推送共用同一值口径，天然
 *       静默去重。
 */
bool xIoPointFeedValue(XIoPoint* point, int32_t value);

/**
 * @brief [子类→组] 写落定转发（保护接口）：经所属组发射 writeDone。
 * @param point 目标点；传入 NULL 或未登记组时不执行任何操作。
 * @param ok true=写意图已获数据源确认；false=失败沿。
 * @return true=已转发（已登记组）；false=未登记（无组可转发）。
 * @note ok=true 时同时置位数据有效标志（写落定即可信）；组信号按点
 *       指针分发，业务层经 XIoChannelGroup_writeDone_signal 消费。
 */
bool xIoPointWriteDone(XIoPoint* point, bool ok);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOPOINT_PROTECTED_H */
