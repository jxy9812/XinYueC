/**
 * @file       HsfVfdDemoSim.h
 * @brief      变频器模拟源：进程内假从站链路 + XVfdAtv61 装配（内部接口）。
 * @details    页面只经 HsfVfdProvider.h 消费本模块；本头文件供
 *             HsfVfdProvider.c 与页面的 200ms 驱动定时器使用。
 */
#ifndef HSFVFDEMOSIM_H
#define HSFVFDEMOSIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XVFD_ON

#include "XVfdDevice.h"
#include "HsfVfdProvider.h"

/**
 * @brief 变频器模拟源（进程内单例；不释放随进程）。
 */
typedef struct HsfVfdDemoSim HsfVfdDemoSim;

/**
 * @brief 创建并装配模拟源（通道组 + 假从站链路 + ATV61 设备 + 信号桥）。
 * @return 成功返回句柄；失败返回 NULL（事件循环未起时装配仍安全）。
 * @note 装配后须由页面调用 hsfVfdSimTick() 驱动（200ms 一拍）。
 */
HsfVfdDemoSim* hsfVfdSimCreate(void);

/** @brief 取设备基类视图（借用；页面渲染只读入口）。 */
const XVfdDevice* hsfVfdSimDevice(const HsfVfdDemoSim* sim);

/** @brief 取设备可变视图（provider 控制动作入口；NULL 时返回 NULL）。 */
XVfdDevice* hsfVfdSimDeviceMut(HsfVfdDemoSim* sim);

/** @brief 查故障码品牌文本（ATV61 字母码+中文；未知码返回 NULL）。 */
const char* hsfVfdSimFaultText(const HsfVfdDemoSim* sim, uint16_t code);

/** @brief 推进一拍：假从站生成遥测 → feedRole → 设备解码（200ms 调一次）。 */
void hsfVfdSimTick(HsfVfdDemoSim* sim);

/** @brief 注册状态快照变化回调（设备 statusUpdated 信号 → 页面通知）。 */
bool hsfVfdSimSubscribe(HsfVfdDemoSim* sim, HsfVfdNotifyFn cb, void* page);

/** @brief 注销回调（页面销毁时必调，防 provider 侧悬垂）。 */
void hsfVfdSimUnsubscribe(HsfVfdDemoSim* sim, void* page);

/** @brief 注销回调时回传 provider 表指针（通知桥用；Provider.c 注册时调）。 */
void hsfVfdSimSetProvider(const HsfVfdProvider* provider);

/** @brief 登记模块单例（Provider.c 懒创建后调；tick(NULL)/device(NULL) 解析目标）。 */
void hsfVfdSimSetInstance(HsfVfdDemoSim* sim);

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* HSFVFDEMOSIM_H */
