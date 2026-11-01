/**
 * @file       XIoChannel_Protected.h
 * @brief      工业通道抽象基类保护 API（仅供通道适配器子类与内部实现使用）。
 * @details    仅供 XIoChannel 的子类（XDigitalInput/XDigitalOutput/
 *             XAnalogInput/XAnalogOutput 及各传输适配器）与框架内部实现
 *             使用，不对外公开；普通业务代码不得包含本头文件。适配器在
 *             挂接/通信成败路径上经本头文件维护 valid 标志与错误码。
 */
#ifndef XIOCHANNEL_PROTECTED_H
#define XIOCHANNEL_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XIoChannel.h"
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON


/**
 * @brief 设置通道数据有效标志（保护接口）。
 * @param point 目标通道；传入 NULL 时函数不执行任何操作。
 * @param valid true=通道数据可信；false=不可信（未挂接/通信失败）。
 * @return 无。本函数不发射信号；适配器应在状态翻转的语义节点（挂接成功、
 *         通信失败、恢复等）自行发射业务信号。
 */
void XIoChannel_setValid(XIoChannel* point, bool valid);

/**
 * @brief 记录通道错误并发射 errorOccurred 信号（保护接口）。
 * @param point 目标通道；传入 NULL 时函数不执行任何操作。
 * @param error 错误码（XIoChannelError 枚举值）。
 * @return 无。error 为 XIoChannelError_None 时仅清除错误码、不发射信号；
 *         非 None 时记录错误码、清除 valid 标志并发射 errorOccurred。
 * @note 适配器在通信失败的确定路径调用；对可恢复的瞬时忙碌（Busy）也照常
 *       调用，由业务层决定是否忽略。
 */
void XIoChannel_setError(XIoChannel* point, int error);

/**
 * @brief 清除通道错误码（保护接口）。
 * @param point 目标通道；传入 NULL 时函数不执行任何操作。
 * @return 无。只复位 m_lastError 为 XIoChannelError_None，不发射信号；
 *         通信恢复正常时由适配器调用，随后应配套 XIoChannel_setValid(point, true)。
 */
void XIoChannel_clearError(XIoChannel* point);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOCHANNEL_PROTECTED_H */
