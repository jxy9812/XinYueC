/**
 * @file       XGuiRemoteAdapt.h
 * @brief      XGuiRemote 服务端编码帧率自适应降档阶梯(纯函数状态机, 内部头)。
 * @details    [perf9 路4 自动降档 2026-10-05] scrcpy 式自动降档: 编码轮耗时
 *             /队列水位超预算 → 认领门控帧率沿阶梯 60→30→15 下行; 持续
 *             富余 → 迟滞回升。真机定谳背景: performance 档(ARGB32/zlib/
 *             128tile/60fps)在 A33 单轮倾倒 29.21ms > 16.7ms 预算, 交付崩
 *             塌至 0.05fps——硬钉高帧率在慢端只会积压+丢批, 主动降档把同
 *             一份编码时间摊进更长的节拍, 交付反而连续。
 *             设计要点:
 *               - 阶梯由会话档位 plan.maxFps 派生(0 步=原速, 恒不高于档
 *                 位), resource(15fps)天然平阶梯零影响; 换档(PROFILE_SET)
 *                 即重置——与既有能力协商/逐会话降级语义取并集, 老 peer
 *                 兼容(纯服务端认领节流, 无线上语义变化)。
 *               - 下行快(连续 2 轮超预算即降), 上行慢(连续 8 轮富余且距
 *                 最近移动 ≥1s)——非对称迟滞防抖动。
 *               - 全部状态编码线程私有(无锁); 纯函数可单测
 *                 (Test/XGuiTest/XGuiRemoteAdaptTest.c)。
 * @author     XinYueC 团队
 */
#ifndef XGUI_REMOTE_ADAPT_H
#define XGUI_REMOTE_ADAPT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 阶梯地板帧率(= resource 档设计节拍, 全库定谳的可交付下限)。 */
#define XGUI_REMOTE_ADAPT_FLOOR_FPS   15
/** @brief 阶梯最深步(0=原速, 1=半速, 2=地板)。 */
#define XGUI_REMOTE_ADAPT_MAX_STEP    2
/** @brief 下行: 连续超预算轮数阈值(快降)。 */
#define XGUI_REMOTE_ADAPT_DOWN_ROUNDS 2
/** @brief 上行: 连续富余轮数阈值(慢升)。 */
#define XGUI_REMOTE_ADAPT_UP_ROUNDS   8
/** @brief 上行: 距最近阶梯移动的最小驻留 ms(防抖动)。 */
#define XGUI_REMOTE_ADAPT_DWELL_MS    1000

/** @brief 阶梯状态(编码线程私有, 无锁; 全零=步 0 原速)。 */
typedef struct XGuiRemoteAdapt {
    int     baseFps;    /**< 阶梯基准(会话 plan.maxFps 快照; 变化即重置)。 */
    int     step;       /**< 当前阶梯步(0..XGUI_REMOTE_ADAPT_MAX_STEP)。 */
    int     overRun;    /**< 连续超预算轮数(下行计数)。 */
    int     underRun;   /**< 连续富余轮数(上行计数)。 */
    int64_t lastMoveMs; /**< 最近阶梯移动时刻 ms(0=未动; 上行驻留基准)。 */
} XGuiRemoteAdapt;

/**
 * @brief  当前阶梯帧率: min(档位, 阶梯值); 档位 ≤ 地板时恒等于档位。
 * @note   baseFps<=0 视为无门控(0=不节流), 原样返回。
 */
static inline int xgui_remote_adapt_fps(const XGuiRemoteAdapt* a)
{
    int fps;
    if (a == NULL || a->baseFps <= 0) {
        return (a != NULL) ? a->baseFps : 0;
    }
    fps = a->baseFps;
    if (a->step >= 1) {
        int half = a->baseFps / 2;
        fps = (half > XGUI_REMOTE_ADAPT_FLOOR_FPS) ? half
                                                   : XGUI_REMOTE_ADAPT_FLOOR_FPS;
    }
    if (a->step >= 2) {
        fps = XGUI_REMOTE_ADAPT_FLOOR_FPS;
    }
    if (fps > a->baseFps) {
        fps = a->baseFps; /* 档位低于地板(如 5fps 窄档)不升。 */
    }
    return fps;
}

/** @brief 重置到步 0(会话建立/换档 PROFILE_SET 时调用)。 */
static inline void xgui_remote_adapt_reset(XGuiRemoteAdapt* a, int baseFps)
{
    if (a == NULL) {
        return;
    }
    a->baseFps   = baseFps;
    a->step      = 0;
    a->overRun   = 0;
    a->underRun  = 0;
    a->lastMoveMs = 0;
}

/**
 * @brief  一轮认领→编码→入队结束后喂状态机。
 * @param  baseFps  本轮生效档位 maxFps(变化即自动重置, PROFILE_SET 并集)。
 * @param  roundMs  本轮耗时(认领起至入队止, 含扫描+编码)。
 * @param  budgetMs 轮预算 ms(<=0 派生为 1000/当前阶梯 fps)。
 * @param  qHigh    队列高水位/背压信号(超预算同义)。
 * @param  nowMs    单调毫秒戳。
 * @return 喂养后的阶梯帧率(供日志/门控)。
 */
static inline int xgui_remote_adapt_feed(XGuiRemoteAdapt* a, int baseFps,
                                         int roundMs, int budgetMs,
                                         int qHigh, int64_t nowMs)
{
    int cur;
    int budget;
    if (a == NULL) {
        return baseFps;
    }
    if (baseFps != a->baseFps) {
        xgui_remote_adapt_reset(a, baseFps); /* 换档并集: 基准变化即复位。 */
    }
    cur = xgui_remote_adapt_fps(a);
    if (a->baseFps <= 0 || a->baseFps <= XGUI_REMOTE_ADAPT_FLOOR_FPS) {
        return cur; /* 平阶梯(无门控/档位≤地板): 无可降, 零状态积累。 */
    }
    budget = (budgetMs > 0) ? budgetMs : (cur > 0 ? 1000 / cur : 0);
    if (budget <= 0) {
        return cur;
    }
    if (roundMs > budget || qHigh) {
        ++a->overRun;
        a->underRun = 0;
    } else if (roundMs * 2 < budget && !qHigh) {
        ++a->underRun;
        a->overRun = 0;
    } else {
        a->overRun = 0; /* 中性轮: 双计数清零, 防陈旧积累。 */
        a->underRun = 0;
    }
    /* 下行快: 连续超预算即降一步(恒可执行, 无驻留)。 */
    if (a->overRun >= XGUI_REMOTE_ADAPT_DOWN_ROUNDS &&
        a->step < XGUI_REMOTE_ADAPT_MAX_STEP) {
        ++a->step;
        a->overRun  = 0;
        a->underRun = 0;
        a->lastMoveMs = nowMs;
    } else if (a->underRun >= XGUI_REMOTE_ADAPT_UP_ROUNDS && a->step > 0 &&
               (a->lastMoveMs == 0 || nowMs - a->lastMoveMs >=
                                          XGUI_REMOTE_ADAPT_DWELL_MS)) {
        /* 上行慢: 富余计数 × 驻留迟滞。 */
        --a->step;
        a->overRun  = 0;
        a->underRun = 0;
        a->lastMoveMs = nowMs;
    }
    return xgui_remote_adapt_fps(a);
}

#ifdef __cplusplus
}
#endif

#endif /* XGUI_REMOTE_ADAPT_H */
