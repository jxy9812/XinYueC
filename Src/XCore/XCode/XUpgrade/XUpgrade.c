/**
 * @file       XUpgrade.c
 * @brief      跨平台自升级模块公共入口实现：进程内单例引擎的装载与转发。
 * @details    XUpgrade.h 公共 API 的模块单例包装：init 创建并 start 引擎，
 *             deinit 停止并释放；其余控制接口透传 XUpgradeEngine_*。监听器
 *             允许在 init 之前注册（暂存静态，init 时落到引擎）。selfCheck
 *             可在 init 之前独立运行：临时建引擎（安装根由可执行文件路径
 *             推导）完成 §8.3 自检后即释放，不占引擎锁、不起工作线程；已
 *             init（引擎运行中）时调用直接返回 false，不透传运行中引擎。
 */
#include "XUpgrade.h"

#include "XUpgradeEngine.h"
#include "XUpgradeIdentity.h"

#include "XObject.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XThread.h"
#include "XAtomic.h"

#if XUPGRADE_ON

/* ========================================================================== */
/*                          模块单例与 init 前暂存                             */
/* ========================================================================== */

/** @brief 进程内引擎单例；XUpgrade_init 成功后非 NULL。 */
static XUpgradeEngine* g_xupgrade_engine = NULL;
/** @brief 引擎已初始化标志（与 g_xupgrade_engine 联动）。 */
static bool g_xupgrade_inited = false;
/** @brief init 之前注册的监听器暂存（init 时落到引擎）。 */
static XUpgradeListener g_xupgrade_pendingListener = NULL;
/** @brief init 之前注册的监听器上下文暂存。 */
static void* g_xupgrade_pendingData = NULL;
/** @brief init 之前设置的 silent 维护窗口暂存（契约"建议 init 前调用"）。 */
static XUpgradeApplyWindow g_xupgrade_pendingApplyWindow;
/** @brief 是否存在维护窗口暂存（false=未暂存/已清除）。 */
static bool g_xupgrade_hasPendingApplyWindow = false;
/** @brief init 之前设置的 forced 宽限期覆盖暂存（小时；含哨兵值）。 */
static uint32_t g_xupgrade_pendingForcedGraceHours = 0;
/** @brief 是否存在宽限期覆盖暂存。 */
static bool g_xupgrade_hasPendingForcedGrace = false;

/** @brief init/deinit 生命周期互斥状态：0=空闲 1=迁移进行中 2=已初始化。 */
enum {
    XUPGRADE_LIFECYCLE_IDLE = 0,
    XUPGRADE_LIFECYCLE_BUSY = 1,
    XUPGRADE_LIFECYCLE_READY = 2
};
/*
 * 生命周期登记的并发保护：XUpgrade_init/XUpgrade_deinit 经 CAS 竞争
 * g_xupgrade_engine 的迁移权，败者让位等待，消除"并发 init 双双通过
 * 幂等守卫 → 双双 create/start → 后写覆盖先写，败者引擎（堆体 + 两个
 * OS 互斥 + 常驻工作线程）永无 stop/XClassDelete"的登记竞态；init 与
 * deinit 并发同理被串行化。静态存储零初始化即空闲态。
 */
static XAtomic_Int32 g_xupgrade_lifecycle;

/** @brief 竞取生命周期迁移权（与并发 init/deinit 互斥，胜出方返回）。 */
static void xupgrade_lifecycle_acquire(void)
{
    for (;;) {
        int32_t expected = XAtomic_load_int32(&g_xupgrade_lifecycle,
                                              XAtomic_MemoryOrder_Acquire);
        if (expected == XUPGRADE_LIFECYCLE_BUSY) {
            /* 其他线程的 init/deinit 迁移进行中：让位等待（迁移只做
             * create/start 或 stop/delete，窗口为毫秒级）。 */
            XThread_msleep(1);
            continue;
        }
        if (XAtomic_compare_exchange_strong_int32(&g_xupgrade_lifecycle, &expected,
                                                  XUPGRADE_LIFECYCLE_BUSY,
                                                  XAtomic_MemoryOrder_AcqRel,
                                                  XAtomic_MemoryOrder_Acquire))
            return;
        /* CAS 失败：状态在读取后被并发改写，重读重试。 */
    }
}

/** @brief 交出生命周期迁移权并落终态（init 成功=READY，其余=IDLE）。 */
static void xupgrade_lifecycle_release(int32_t state)
{
    XAtomic_store_int32(&g_xupgrade_lifecycle, state, XAtomic_MemoryOrder_Release);
}

void XUpgrade_setListener(XUpgradeListener listener, void* userData)
{
    if (g_xupgrade_inited && g_xupgrade_engine) {
        XUpgradeEngine_setListener(g_xupgrade_engine, listener, userData);
        /* 同步清暂存：否则 deinit→init 循环后旧监听器经 init 通道覆盖
         * 运行期的最新设置（同 setApplyWindow 的 hasPending 清除口径）。
         * 2026-10-08 评审发现。 */
        g_xupgrade_pendingListener = listener;
        g_xupgrade_pendingData = userData;
        return;
    }
    g_xupgrade_pendingListener = listener;
    g_xupgrade_pendingData = userData;
}

void XUpgrade_setApplyWindow(const XUpgradeApplyWindow* window)
{
    if (g_xupgrade_inited && g_xupgrade_engine) {
        XUpgradeEngine_setApplyWindow(g_xupgrade_engine, window);
        g_xupgrade_hasPendingApplyWindow = false;
        return;
    }
    /* init 之前调用：暂存（契约"建议在 XUpgrade_init 之前调用"，模式同
     * 监听器暂存），init 完成（start 之后）作为覆盖值落到引擎。传 NULL
     * 清除暂存（init 通道 config.m_applyWindow 随后生效）。 */
    if (window) {
        g_xupgrade_pendingApplyWindow = *window;
        g_xupgrade_hasPendingApplyWindow = true;
    } else {
        g_xupgrade_hasPendingApplyWindow = false;
    }
}

void XUpgrade_setForcedGraceHours(uint32_t hours)
{
    if (g_xupgrade_inited && g_xupgrade_engine) {
        XUpgradeEngine_setForcedGraceHours(g_xupgrade_engine, hours);
        g_xupgrade_hasPendingForcedGrace = false;
        return;
    }
    /* init 之前调用：暂存（含 XUPGRADE_FORCED_GRACE_FROM_MANIFEST 哨兵=
     * 清除覆盖），init 完成后落到引擎。 */
    g_xupgrade_pendingForcedGraceHours = hours;
    g_xupgrade_hasPendingForcedGrace = true;
}

bool XUpgrade_setTrustKey(const uint8_t* publicKey, uint32_t len)
{
    /* 信任锚为验签模块级单一状态（引擎入口同为此转发），与引擎生命周期
     * 无关——契约明示测试/产品注入点，init 之前调用必须可用。 */
    extern bool XUpgradeVerify_setTrustKey(const uint8_t* publicKey, uint32_t len);
    return XUpgradeVerify_setTrustKey(publicKey, len);
}

bool XUpgrade_init(const XUpgradeConfig* config)
{
    XUpgradeEngine* engine;
    if (!config || !config->m_updateUrl || !config->m_targetOs) return false;
    if (g_xupgrade_inited && g_xupgrade_engine) return true; /* 幂等（无锁快路径） */

    /* 竞取迁移权：与并发 init/deinit 串行化（败者在 acquire 内等待）。 */
    xupgrade_lifecycle_acquire();
    /* 双检：并发的 init 可能已在等待期间完成创建与发布。 */
    if (g_xupgrade_inited && g_xupgrade_engine) {
        xupgrade_lifecycle_release(XUPGRADE_LIFECYCLE_READY);
        return true; /* 幂等 */
    }
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!engine) {
        xupgrade_lifecycle_release(XUPGRADE_LIFECYCLE_IDLE);
        return false;
    }
    if (g_xupgrade_pendingListener)
        XUpgradeEngine_setListener(engine, g_xupgrade_pendingListener, g_xupgrade_pendingData);
    if (!XUpgradeEngine_start(engine, config)) {
        XClassDelete((XClass*)engine);
        xupgrade_lifecycle_release(XUPGRADE_LIFECYCLE_IDLE);
        return false;
    }
    /* init 前暂存的运行期覆盖值落到引擎（start 已装 config.m_applyWindow，
     * 此处按契约"本入口随后覆盖"次序覆盖）。 */
    if (g_xupgrade_hasPendingApplyWindow)
        XUpgradeEngine_setApplyWindow(engine, &g_xupgrade_pendingApplyWindow);
    if (g_xupgrade_hasPendingForcedGrace)
        XUpgradeEngine_setForcedGraceHours(engine, g_xupgrade_pendingForcedGraceHours);
    g_xupgrade_engine = engine;
    g_xupgrade_inited = true;
    xupgrade_lifecycle_release(XUPGRADE_LIFECYCLE_READY);
    return true;
}

void XUpgrade_deinit(void)
{
    XUpgradeEngine* engine;
    if (!g_xupgrade_inited || !g_xupgrade_engine) return; /* 无锁快路径 */
    xupgrade_lifecycle_acquire();
    /* 双检：并发的 deinit 可能已在等待期间完成释放。 */
    if (!g_xupgrade_inited || !g_xupgrade_engine) {
        xupgrade_lifecycle_release(XUPGRADE_LIFECYCLE_IDLE);
        return;
    }
    engine = g_xupgrade_engine;
    g_xupgrade_engine = NULL;
    g_xupgrade_inited = false;
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    xupgrade_lifecycle_release(XUPGRADE_LIFECYCLE_IDLE);
}

bool XUpgrade_checkNow(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return false;
    return XUpgradeEngine_checkNow(g_xupgrade_engine);
}

bool XUpgrade_startUpgrade(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return false;
    return XUpgradeEngine_startUpgrade(g_xupgrade_engine);
}

uint32_t XUpgrade_remoteVersionCount(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return 0;
    return XUpgradeEngine_remoteVersionCount(g_xupgrade_engine);
}

bool XUpgrade_remoteVersionAt(uint32_t index, char* versionOut, size_t cap)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return false;
    return XUpgradeEngine_remoteVersionAt(g_xupgrade_engine, index, versionOut, cap);
}

bool XUpgrade_installVersion(const char* version)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return false;
    return XUpgradeEngine_installVersion(g_xupgrade_engine, version);
}

void XUpgrade_skipVersion(const char* componentId, const char* version)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return;
    XUpgradeEngine_skipVersion(g_xupgrade_engine, componentId, version);
}

XUpgradeApplyWindow XUpgrade_applyWindow(void)
{
    XUpgradeApplyWindow window;
    XMemset(&window, 0, sizeof(window)); /* 未 init：m_enabled=false 口径 */
    if (!g_xupgrade_inited || !g_xupgrade_engine) return window;
    return XUpgradeEngine_applyWindow(g_xupgrade_engine);
}

uint32_t XUpgrade_forcedGraceHours(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return 0; /* 无 manifest 上下文 */
    return XUpgradeEngine_forcedGraceHours(g_xupgrade_engine);
}

XUpgradeGrayDecision XUpgrade_grayDecision(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine)
        return XUpgradeGrayDecision_NotEvaluated; /* 从未检查口径 */
    return XUpgradeEngine_grayDecision(g_xupgrade_engine);
}

bool XUpgrade_applyNow(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return false;
    return XUpgradeEngine_applyNow(g_xupgrade_engine);
}

bool XUpgrade_selfCheck(void)
{
    XUpgradeEngine* engine;
    bool ok;
    /* 已 init 时拒绝：init 已 start 引擎（工作线程流水经 set_state/save_state
     * 并发改写状态镜像并落盘 state.json），不再透传运行中的引擎——自检与流水
     * 并发跑属数据竞争路径；契约口径 selfCheck 是启动早期动作（GUI/init 之
     * 前，临时引擎路径）。未 init 时走临时引擎独立自检（不变）。 */
    if (g_xupgrade_inited && g_xupgrade_engine) return false;
    /* init 之前独立自检：临时引擎，根由可执行文件路径推导，不起线程不加锁。 */
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!engine) return false;
    ok = XUpgradeEngine_selfCheck(engine);
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    return ok;
}

bool XUpgrade_confirmBoot(void)
{
    if (!g_xupgrade_inited || !g_xupgrade_engine) return false;
    return XUpgradeEngine_confirmBoot(g_xupgrade_engine);
}

#endif /* XUPGRADE_ON */
