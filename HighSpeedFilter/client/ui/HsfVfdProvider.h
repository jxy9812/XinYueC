/**
 * @file       HsfVfdProvider.h
 * @brief      变频器页数据提供者接口：页面与后端之间的唯一接缝。
 * @details    页面只认本函数表，不知道任何具体链路/品牌。本战役内置
 *             进程内模拟源（HsfVfdDemoSource：XIoLink 假从站 + XVfdAtv61），
 *             后续接真后端（Modbus RTU/TCP 真链路）时换一套函数表即可，
 *             页面零改动。
 *
 *             [为何做成纯 C 函数表] 页面生命周期随主窗级联释放，而后端
 *             （将来可能是连接管理器/协议栈）生命周期独立：函数表 + 回调
 *             注册/注销让两侧互不持有指针，页面对 provider 只在回调在册
 *             期间被调，避免悬垂。
 *
 *             [约定] 所有回调入参 userData = m_userData；允许整表为 NULL
 *             或个别槽为 NULL（页面按 NULL 降级：动作按钮灰、数值显 "--"）。
 */
#ifndef HSFVFDPROVIDER_H
#define HSFVFDPROVIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#if XVFD_ON

#include "XVfdDevice.h"

/**
 * @brief 数据变化通知回调（provider 在状态快照更新后调用）。
 * @param provider 发起通知的 provider。
 * @param page 页面注册时传入的令牌（原样回传）。
 */
typedef void (*HsfVfdNotifyFn)(const struct HsfVfdProvider* provider, void* page);

/**
 * @brief 变频器数据/控制提供者（纯 C 函数表；页面零业务）。
 */
typedef struct HsfVfdProvider {
    void* m_userData;                                       /**< 各回调首参。 */

    /* ---- 数据（渲染只读） ---- */
    /** @brief 渲染数据源：返回设备基类视图（借用；页面只读 status_const）。 */
    const XVfdDevice* (*device)(void* userData);
    /** @brief 查当前故障码的品牌文本（如 "OCF"/"E.LU"；未知码返回 NULL）。 */
    const char* (*faultText)(void* userData, uint16_t code);

    /* ---- 控制闸（统一修改许可） ---- */
    /** @brief 查控制闸状态。 */
    bool (*controlEnabled)(void* userData);
    /** @brief 设控制闸；返回是否受理（provider 不在线返回 false）。 */
    bool (*setControlEnabled)(void* userData, bool enabled);

    /* ---- 控制动作（异步；结果经 device() 的 commandCompleted 信号回传） ---- */
    /** @brief 启动（reverse=反转；无该能力由设备层拒绝）。 */
    /** @brief 启动（reverse=反转；hz>0 表示**捎带给定**）。
     *  @details 给定捎带在启动命令里下发（服务端先写给定再发命令字），
     *           避免"先单独发给定被停机闸拒 ⇒ 按旧给定跑"。 */
    bool (*start)(void* userData, bool reverse, double hz);
    /** @brief 受控停机。 */
    bool (*stop)(void* userData);
    /** @brief 自由停车。 */
    bool (*freeStop)(void* userData);
    /** @brief 点动（reverse=反向点动）。 */
    /** @brief 点动（reverse=反向点动；hz>0 表示**捎带给定**）。 */
    bool (*jog)(void* userData, bool reverse, double hz);
    /**
     * @brief 点动释放（松手即停）。
     * @details [为什么单列一个槽] 松手必须"立刻停"，不能等服务端 600ms
     *          看门狗；而普通 stop 是"受控停机"语义（减速到位）。远端
     *          后端用专用 op=101（HSF_OP_JOG_RELEASE）；本地模拟后端可
     *          为 NULL（页面回落到 stop()）。
     */
    bool (*jogRelease)(void* userData);
    /** @brief 频率给定（Hz；工程单位，品牌口径换算在设备层）。 */
    bool (*setFrequency)(void* userData, double hz);
    /** @brief 故障复位。 */
    bool (*resetFault)(void* userData);

    /* ---- 订阅 ---- */
    /** @brief 注册数据变化回调（同页重复注册按一次计）。 */
    bool (*subscribe)(void* userData, HsfVfdNotifyFn cb, void* page);
    /** @brief 注销数据变化回调（页面对 provider 生命周期早于 provider 时必调）。 */
    void (*unsubscribe)(void* userData, void* page);

    /* ---- 远端后端扩展（可选；NULL 时页面回落 device() 路径） ---- */
    /** @brief 状态快照（借用，随下次通知失效；远端后端无本地设备对象，
     *         快照由网络遥测解码而来）。NULL=走 device() 的 status_const。 */
    const XVfdStatus* (*snapshot)(void* userData);
    /** @brief 能力位图（XVfdCapability 位或；远端后端本地无设备能力表，
     *         由协议约定给出）。NULL=走 device() 的 caps()。 */
    uint32_t (*caps)(void* userData);
    /** @brief 取最近一拍控制应答（远端后端专用；本地后端无此概念，恒 NULL）。
     *  @param outSeq 输出应答序号（每拍自增；0=从未收到）。
     *  @param outErrUtf8 输出失败原因（UTF-8；成功为空串；容量 cap）。
     *  @return 已收到过应答返回 true；从未收到/无此槽返回 false。
     *  @note  页面按序号增量观测：序号变化即有新应答，err 非空提示被拒、
     *         空为成功（提示回落）。轮询口径，无需回调。 */
    bool (*commandError)(void* userData, int* outSeq,
                         char* outErrUtf8, size_t cap);
} HsfVfdProvider;

/**
 * @brief 取模拟源 provider（本战役内置；进程内假从站 + ATV61）。
 * @return 静态 provider 表指针；禁释放。整表可用，device() 恒非 NULL。
 * @note 模拟源懒创建：首次调用时装配（事件循环已存在后调用才安全——
 *       页面创建期即满足）。
 */
const HsfVfdProvider* HsfVfdProvider_demo(void);

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* HSFVFDPROVIDER_H */
