/**
 * @file       XVfdDevice_Protected.h
 * @brief      变频器抽象父类内部接口（仅供子类与子系统内部使用，不对外公开）。
 * @details    集中放置子类需要的快照回填、信号发射、能力位图与链路访问入口；
 *             公开头文件 XVfdDevice.h 不传递引入本文件。
 */
#ifndef XVFDDEVICE_PROTECTED_H
#define XVFDDEVICE_PROTECTED_H

#include "XVfdDevice.h"

#if XVFD_ON
#if XVFD_DEVICE_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取可变状态快照（子类解析回填用）。
 * @param dev 设备实例（非 NULL）
 * @return 内部快照指针；仅子类在解析路径中写入
 */
XVfdStatus* XVfdDevice_status(XVfdDevice* dev);

/**
 * @brief 标记快照有效并发射 statusUpdated 信号。
 * @param dev 设备实例（非 NULL）
 * @note 内部置 m_valid/m_haveStatus 并刷新时间戳。
 */
void XVfdDevice_emitStatus(XVfdDevice* dev);

/**
 * @brief 发射动作完成信号并清设备级忙标志。
 * @param dev 设备实例（非 NULL）
 * @param op 动作类型
 * @param ok 是否成功
 */
void XVfdDevice_emitDone(XVfdDevice* dev, XVfdOperation op, bool ok);

/**
 * @brief 发射错误信号（message 为静态常量文本即可，禁释放调用方栈文本）。
 * @param dev 设备实例（非 NULL）
 * @param message 错误描述（UTF-8）
 */
void XVfdDevice_emitError(XVfdDevice* dev, const char* message);

/**
 * @brief 记录失败原因（供 lastError 查询；不发射信号）。
 * @param dev 设备实例（非 NULL）
 * @param error 错误码
 */
void XVfdDevice_setLastError(XVfdDevice* dev, XVfdError error);

/**
 * @brief 设置能力位（子类 init 时逐位置位）。
 * @param dev 设备实例（非 NULL）
 * @param cap 能力位
 */
void XVfdDevice_setCapability(XVfdDevice* dev, uint32_t cap);

/**
 * @brief 获取绑定链路（借用；可 NULL）。
 * @param dev 设备实例（非 NULL）
 * @return 链路指针
 */
XVfdLink* XVfdDevice_link(XVfdDevice* dev);

/**
 * @brief 轮询到期分派入口：链路计划表到期回调内调用（EXVfdDevice_PollDue 槽）。
 * @param dev 设备实例（非 NULL）
 * @return 设备已受理本拍轮询（提交成功）返回 true
 */
bool XVfdDevice_pollDue_base(XVfdDevice* dev);

#ifdef __cplusplus
}
#endif

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */
#endif /* XVFDDEVICE_PROTECTED_H */
