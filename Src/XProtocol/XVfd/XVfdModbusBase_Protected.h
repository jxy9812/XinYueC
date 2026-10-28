/**
 * @file       XVfdModbusBase_Protected.h
 * @brief      Modbus 语义中间基类内部接口（仅供品牌子类使用，不对外公开）。
 * @details    提供轮询组注册、参数表注册、请求提交包装、动作收口等子类装配
 *             入口；公开头文件不传递引入本文件。
 */
#ifndef XVFDMODBUSBASE_PROTECTED_H
#define XVFDMODBUSBASE_PROTECTED_H

#include "XVfdModbusBase.h"

#if XVFD_ON
#if XVFD_DEVICE_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 轮询读组解析钩子：应答成功后由引擎调用，子类在此把寄存器值回填快照。
 * @param owner 设备实例（XVfdModbusBase*）
 * @param result 应答数据单元（只读借用）
 */
typedef void (*XVfdModbusParseFunc)(XObject* owner, const XModbusDataUnit* result);

/**
 * @brief 注册一个轮询读组（连续地址段）。
 * @param base 实例（非 NULL）
 * @param addr 起始地址（品牌编码已换算）
 * @param count 寄存器数
 * @param areaMask 归属轮询项掩码（XVfdPollItem 位或；PollDue 按设备掩码过滤）
 * @param parse 解析钩子（非 NULL）
 * @return 组下标；表满返回 -1
 */
int XVfdModbusBase_addPollGroup(XVfdModbusBase* base, uint16_t addr, uint16_t count,
                                uint32_t areaMask, XVfdModbusParseFunc parse);

/**
 * @brief 注册品牌参数描述表（const，借用）。
 * @param base 实例（非 NULL）
 * @param table 描述表数组（静态常量）
 * @param count 条目数
 */
void XVfdModbusBase_setParamTable(XVfdModbusBase* base, const XVfdParamDesc* table, int count);

/**
 * @brief 提交读请求（设备级单飞由调用方保证；完成回调在链路收口路径执行）。
 * @param base 实例（非 NULL）
 * @param addr 起始地址
 * @param count 寄存器数
 * @param done 完成回调（非 NULL）
 * @param userData 用户数据（透传）
 * @return 受理返回 true
 */
bool XVfdModbusBase_submitRead(XVfdModbusBase* base, uint16_t addr, uint16_t count,
                               XVfdModbusLinkDoneFunc done, void* userData,
                               XModbusRequestPriority priority);

/**
 * @brief 提交单寄存器写请求。
 * @param base 实例（非 NULL）
 * @param addr 目标地址
 * @param value 写入值
 * @param done 完成回调（非 NULL）
 * @param userData 用户数据
 * @return 受理返回 true
 */
bool XVfdModbusBase_submitWriteSingle(XVfdModbusBase* base, uint16_t addr, uint16_t value,
                                      XVfdModbusLinkDoneFunc done, void* userData,
                                      XModbusRequestPriority priority);

/**
 * @brief 动作起跑：占用设备级单飞并登记动作类型。
 * @param base 实例（非 NULL）
 * @param op 动作类型
 * @return 上一次动作已收口、本动作受理返回 true；忙返回 false（lastError=Busy）
 */
bool XVfdModbusBase_busyStart(XVfdModbusBase* base, XVfdOperation op);

/**
 * @brief 动作收口：清忙并发射 commandCompleted，随后补发挂起动作。
 * @param base 实例（非 NULL）
 * @param ok 是否成功
 */
void XVfdModbusBase_finishAction(XVfdModbusBase* base, bool ok);

/**
 * @brief 动作三态入口：空闲返回 0（调用方继续 busyStart+提交）；挂起返回 1
 *        （动作已排队，调用方直接返回 true，收口后经虚槽补发）；忙碌/未绑定
 *        返回 -1（lastError 已置 Busy/NotAttached）。
 * @param base 实例（非 NULL）
 * @param op 动作类型
 * @param b1 布尔参数（reverse 等）
 * @param i1 整型参数（index/rpm/scope/mode/password 低 16 位）
 * @param d1 浮点参数 1（hz/accel/value）
 * @param d2 浮点参数 2（decel）
 * @return 0=继续 1=已挂起 -1=拒绝
 */
int XVfdModbusBase_actionEntry(XVfdModbusBase* base, XVfdOperation op,
                               bool b1, int i1, double d1, double d2);

/**
 * @brief 获取当前在途动作类型。
 * @param base 实例（非 NULL）
 * @return XVfdOperation
 */
XVfdOperation XVfdModbusBase_currentOp(XVfdModbusBase* base);

#ifdef __cplusplus
}
#endif

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */
#endif /* XVFDMODBUSBASE_PROTECTED_H */
