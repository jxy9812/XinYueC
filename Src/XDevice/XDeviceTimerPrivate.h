/**
 * @file XDeviceTimerPrivate.h
 * @brief 定时器设备内部调度接口。
 * @details 仅供 XDeviceTimer 和事件调度器实现使用，不构成稳定公共 API。所有
 *          操作必须遵守所属 XAbstractEventDispatcher 的线程亲和性。
 */
#ifndef XDEVICETIMERPRIVATE_H
#define XDEVICETIMERPRIVATE_H

#include "XDeviceTimer.h"
#include "XAbstractEventDispatcher.h"
#include "XTimerData.h"

/** @brief 获取进程级时间轮。 @return 全局时间轮指针；由定时器设备持有，调用方不得释放。 */
XTimeWheelGroup* XDeviceTimer_timeWheel(void);
/**
 * @brief 将定时器数据加入指定后端。
 * @param dispatcher 所属事件调度器，只借用且必须在其线程调用。
 * @param data 定时器数据，只借用，必须保持到取消或回调完成。
 * @param intervalNs 间隔，单位纳秒。
 * @param requestedType 请求的定时器类型。
 * @param actualType 输出实际采用的类型，可为 NULL。
 * @param backendHandle 输出后端句柄，可为 NULL。
 * @return 成功返回 true，参数非法或后端资源不足返回 false。
 */
bool XDeviceTimer_schedule(XAbstractEventDispatcher* dispatcher, XTimerData* data,
    XDuration intervalNs, XTimerType requestedType, XTimerType* actualType,
    XHandle* backendHandle);
/** @brief 从后端取消定时器。@param dispatcher 调度器；@param actualType 实际后端类型；@param backendHandle 后端句柄。 @return 成功返回 true。 */
bool XDeviceTimer_cancel(XAbstractEventDispatcher* dispatcher, XTimerType actualType,
    XHandle backendHandle);
/** @brief 处理到期定时器。@param dispatcher 当前调度器；@param processGlobalWheel 是否同时处理全局时间轮。 */
void XDeviceTimer_process(XAbstractEventDispatcher* dispatcher, bool processGlobalWheel);
/** @brief 查询当前调度器最早的高精度到期时间。@param dispatcher 调度器，只读借用。 @return 单调时钟纳秒时间戳，无定时器返回 UINT64_MAX。 */
uint64_t XDeviceTimer_nextPreciseDeadline(const XAbstractEventDispatcher* dispatcher);
/** @brief 释放调度器关联的高精度后端资源。@param dispatcher 调度器，只能在销毁流程调用。 */
void XDeviceTimer_releaseDispatcher(XAbstractEventDispatcher* dispatcher);

#endif /* XDEVICETIMERPRIVATE_H */
