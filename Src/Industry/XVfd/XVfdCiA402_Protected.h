/**
 * @file       XVfdCiA402_Protected.h
 * @brief      通用 CiA402 状态机骨架内部接口（仅供子类与内部实现使用）。
 * @details    集中放置品牌子类需要的：状态字解码入口、画像表安全取用、
 *             迁移命令推导。
 * @note       约束：本头自包含（显式 include 自己的公开头），可单独包含。
 */
#ifndef XVFDCIA402_PROTECTED_H
#define XVFDCIA402_PROTECTED_H

#include "XVfdCiA402.h"

#if XVFD_ON
#if XVFD_CIA402_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 状态字解码入口：刷新快照运行态字段（通用部分）+ 调品牌重载解码。
 * @param dev 实例；不能为 NULL。
 * @param status 状态字原始值。
 * @return 无。
 * @note 通用部分：运行/故障/警告/反转/到位/远端 + 状态→XVfdRunState 映射；
 *       品牌专有位（加速/减速/直流制动等）由 EXVfdCiA402_DecodeBrand 虚槽补。
 */
void XVfdCiA402_applyStatus(XVfdCiA402* dev, uint16_t status);

/**
 * @brief 辅助状态字解码入口：缓存辅助字后按缓存状态字重解码。
 * @param dev 实例；不能为 NULL。
 * @param aux 辅助状态字原始值。
 * @return 无。
 * @note 品牌辅助字常携带加速/减速/直流制动等运行细节，到达后须重解状态。
 */
void XVfdCiA402_applyAux(XVfdCiA402* dev, uint16_t aux);

/**
 * @brief 取品牌画像表（NULL 安全）。
 * @param dev 实例（可 NULL）。
 * @return 画像表指针；未挂接返回 NULL。
 */
const XVfdCiA402Profile* XVfdCiA402_profileOf(const XVfdCiA402* dev);

/**
 * @brief 由当前状态推导下一步命令字与期望状态码。
 * @param dev 实例；不能为 NULL（须已挂画像表）。
 * @param status 状态字原始值。
 * @param reverse true=反转。
 * @param outExpect 输出该步写完后应到达的状态码（不能为 NULL）。
 * @return 命令字（含方向位）。
 * @note 迁移表：Disabled→Shutdown→Ready、Ready→Switch on→Switched、
 *       Switched→Enable operation→Running；其它状态一律按「推进到运行」处理。
 */
uint16_t XVfdCiA402_nextCommand(const XVfdCiA402* dev, uint16_t status, bool reverse,
                                 uint16_t* outExpect);

#ifdef __cplusplus
}
#endif

#endif /* XVFD_CIA402_ON */
#endif /* XVFD_ON */
#endif /* XVFDCIA402_PROTECTED_H */
