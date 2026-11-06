/**
 * @file       HsfVfdProvider.c
 * @brief      变频器数据提供者：进程内模拟源（XIoLink 假从站 + XVfdAtv61）。
 * @details    [本战役内置后端] 不开真实串口/TCP：自研「进程内假从站链路」
 *             （XIoLink 子类，链路抽象文档明列的合法形态），按 ATV61 品牌表
 *             语义生成遥测（ETA/RFR/LCR/OTR/...），经 feedRole 喂进通道组，
 *             由**真实** XVfdDevice 基类解码出状态快照 —— 页面走的是与真
 *             后端完全相同的数据路径（信号/快照/validMask/控制闸）。
 *
 *             [演示剧本] 上电 0→50Hz 斜坡 → 恒速运行（电流/转矩/母线电压
 *             随动摆动）→ 周期性注入/复位故障（展示故障横幅与复位按钮）。
 *             全部剧情在链路 tick 内推进，由页面创建的 200ms XTimer 驱动。
 *
 *             [换真后端] 实现同一张 HsfVfdProvider 函数表（device() 返回
 *             真链路上的 XVfdDevice 即可），页面零改动。
 */
#include "HsfVfdProvider.h"

#if XVFD_ON

#include "HsfVfdDemoSim.h"
#include "XObject.h"
#include "XMemory.h"
#include <string.h>

/* ==================== provider 槽实现（薄封装 → 模拟源设备） ==================== */

static const XVfdDevice* demoDevice(void* userData)
{
    return hsfVfdSimDevice((HsfVfdDemoSim*)userData);
}

static const char* demoFaultText(void* userData, uint16_t code)
{
    return hsfVfdSimFaultText((HsfVfdDemoSim*)userData, code);
}

static bool demoControlEnabled(void* userData)
{
    const XVfdDevice* dev = hsfVfdSimDevice((HsfVfdDemoSim*)userData);
    return dev ? XVfdDevice_controlEnabled(dev) : false;
}

static bool demoSetControlEnabled(void* userData, bool enabled)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    if (!dev) return false;
    XVfdDevice_setControlEnabled(dev, enabled);
    return true;
}

static bool demoStart(void* userData, bool reverse, double hz)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    /* [捎带给定] 演示源同样先写给定再启动（与远端口径一致）。 */
    if (dev && hz > 0.0) XVfdDevice_setFrequency(dev, hz);
    return dev ? XVfdDevice_start(dev, reverse) : false;
}

static bool demoStop(void* userData)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    return dev ? XVfdDevice_stop(dev) : false;
}

static bool demoFreeStop(void* userData)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    return dev ? XVfdDevice_freeStop(dev) : false;
}

static bool demoJog(void* userData, bool reverse, double hz)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    if (dev && hz > 0.0) XVfdDevice_setFrequency(dev, hz);
    return dev ? XVfdDevice_jog(dev, reverse) : false;
}

static bool demoSetFrequency(void* userData, double hz)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    return dev ? XVfdDevice_setFrequency(dev, hz) : false;
}

static bool demoResetFault(void* userData)
{
    XVfdDevice* dev = hsfVfdSimDeviceMut((HsfVfdDemoSim*)userData);
    return dev ? XVfdDevice_resetFault(dev) : false;
}

static bool demoSubscribe(void* userData, HsfVfdNotifyFn cb, void* page)
{
    return hsfVfdSimSubscribe((HsfVfdDemoSim*)userData, cb, page);
}

static void demoUnsubscribe(void* userData, void* page)
{
    hsfVfdSimUnsubscribe((HsfVfdDemoSim*)userData, page);
}

/* ==================== provider 静态表 ==================== */

/* [可写修正 2026-11-03] 原为 static const —— 但下方懒创建时要回填
   m_userData（模拟源指针），const 对象落在只读段，回填即写只读内存 →
   访问违例（建页阶段 HsfMonitorVfd_create → HsfVfdProvider_demo 直接
   崩，App 起不来）。改为可写静态表；对外出口仍返回 const 指针
   （HsfVfdProvider_demo 的 const 契约不变）。 */
static HsfVfdProvider kDemoProvider = {
    /* m_userData */ NULL,               /* 懒创建后回填模拟源指针 */
    demoDevice,
    demoFaultText,
    demoControlEnabled,
    demoSetControlEnabled,
    demoStart,
    demoStop,
    demoFreeStop,
    demoJog,
    NULL,          /* jogRelease：本地模拟回落 stop()。 */
    demoSetFrequency,
    demoResetFault,
    demoSubscribe,
    demoUnsubscribe
};

const HsfVfdProvider* HsfVfdProvider_demo(void)
{
    /* 模拟源懒创建（进程内单例；随进程生命周期，不释放）。 */
    static HsfVfdDemoSim* sim = NULL;
    if (!sim) {
        sim = hsfVfdSimCreate();
        if (!sim) return NULL;
        /* 通知桥需要 provider 表指针（回调参数）；回填静态表 userData。 */
        hsfVfdSimSetProvider(&kDemoProvider);
        kDemoProvider.m_userData = sim;
        /* 模块单例登记：页面定时器 tick(NULL) / device(NULL) 的解析目标。 */
        hsfVfdSimSetInstance(sim);
    }
    return &kDemoProvider;
}

#endif /* XVFD_ON */
