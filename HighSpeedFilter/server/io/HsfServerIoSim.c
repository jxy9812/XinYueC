/**
 * @file       HsfServerIoSim.c
 * @brief      服务端模拟设备工厂实现。
 * @details    模型参数全部来自 HsfServerConfig.h 的现场口径（变频器最大
 *             频率取 M512 的 50Hz、模拟量用 249Ω 采样电阻、数字量 16 点/
 *             台），**不另设一套模拟专用参数**——否则模拟联调通过、现场
 *             参数一改就崩，等于没测。
 */
#include "HsfServerIoSim.h"

#if HSF_SIM_MODE || 1   /* 始终编译：--sim 需要运行期打开 */

#include "XModbusVirtualClient.h"
#include "XModbusVirtualModel.h"
#include "XMemory.h"          /* XFree_System：模型是裸 malloc，无 XClass 生命周期。 */
#include "XPrintf.h"
#include <string.h>

/* ==================== 单例 ==================== */

static HsfServerIoSimModels s_sim;
static bool s_simReady = false;
/* [阀故障注入覆盖] 命令行 --sim-valve-move / --sim-valve-no-feedback 落点：
 * 0=缺省（10000ms 现场节奏）、UINT32_MAX=卡死；noFeedback=开关未接。 */
static uint32_t s_valveMoveOverrideMs = 0;
static bool s_valveNoFeedback = false;

/* M512 最大频率（Hz）：与 XVfdM512 的 50Hz 口径一致。 */
#define HSF_SIM_VFD_MAX_HZ   50.0

/**
 * @brief 阀路挂在哪台数字量模块上。
 * @details [为什么模型要分主/扩展] 数字量模块按"台"建模：每台 16 点，
 *          逻辑点号 = 台内本地点号 + 台序 × 16。阀的 DO 是**全局**通道号，
 *          与单台点数一比就知道它落在哪台上：
 *            · 全局点 < 单台点数 ⇒ 主模块（本地点号 = 全局点号）；
 *            · 否则 ⇒ 扩展模块（本地点号 = 全局点号 − 单台点数）。
 *          2026-11-06 起现场只剩 .13 一台 16 点模块、六路阀在 DO0..5，
 *          故阀由**主模块**承载，联动（写 DO → 走行程 → 到位 DI 变）
 *          必须开在主模块上——开在扩展模块上等于阀永远走不动。
 * @return true=阀在扩展模块上。
 */
static bool simValveOnExt(void)
{
    return HSF_VALVE_DO(0) >= HSF_DIG_CHANNELS;
}

const HsfServerIoSimModels* HsfServerIoSim_ensure(void)
{
    if (s_simReady) return &s_sim;

    memset(&s_sim, 0, sizeof(s_sim));
    s_sim.m_vfd = XVirtualModelVfd_create(HSF_SIM_VFD_MAX_HZ);
    s_sim.m_tempHum = XVirtualModelTempHum_create();
    s_sim.m_flowMeter = XVirtualModelFlow_create();
    s_sim.m_digMain = XVirtualModelDigital_create(HSF_SIM_DIG_CHANNELS_PER_DEV);
    s_sim.m_digExt = XVirtualModelDigital_create(HSF_SIM_DIG_CHANNELS_PER_DEV);
    s_sim.m_analog = XVirtualModelAnalog_create(HSF_AI_CHANNELS);
    /* 浊度仪两台：index 决定缺省基准/字序（0=站1 CDAB、1=站2 DCBA）。
     * 两台必须能看出是两台独立仪器，而不是同一个值贴两遍。 */
    s_sim.m_turbidity[0] = XVirtualModelTurbidity_create(0);
    s_sim.m_turbidity[1] = XVirtualModelTurbidity_create(1);
    if (!s_sim.m_vfd || !s_sim.m_tempHum || !s_sim.m_flowMeter ||
        !s_sim.m_digMain || !s_sim.m_digExt || !s_sim.m_analog ||
        !s_sim.m_turbidity[0] || !s_sim.m_turbidity[1]) {
        XPrintf("HsfServerIoSim: 模型创建失败（内存不足）\n");
        HsfServerIoSim_destroy();
        return NULL;
    }
    /* ---- 数字量两台模型的分工 ----
     *  - 每台 16 点，逻辑点号 = 本地点号 + 台序 × 16；
     *  - **阀由哪台承载由 HSF_VALVE_DO 宏决定**（见 simValveOnExt）：
     *    承载阀的那台开到位联动（写 DO → 10s 行程 → 到位 DI 变），否则
     *    阀永远停在"动作中"，超时告警路径永远走不到；
     *  - 不承载阀的那台是"工艺侧非阀类点"：只给缺省 DI，不参与联动
     *    （避免写 DO 时凭空改动 DI，掩盖真实的"无反馈"诊断）。
     * 2026-11-06 现场只剩 .13 一台 16 点模块、六路阀在 DO0..5 ⇒
     * 主模块承载阀。 */
    XVirtualModelDigital_setValveInterlock(s_sim.m_digMain, !simValveOnExt());
    XVirtualModelDigital_setValveInterlock(s_sim.m_digExt, simValveOnExt());
    /* [阀区边界] 只有阀路本身参与行程：阀区之外是进水泵 DO6、按钮灯
     * DO7..9、按钮输入 DI12..15 —— 写它们不产生行程、也不动任何到位位。 */
    XVirtualModelDigital_setValveChannels(
        s_sim.m_digMain, simValveOnExt() ? 0 : HSF_VALVE_ACTIVE_COUNT);
    XVirtualModelDigital_setValveChannels(
        s_sim.m_digExt, simValveOnExt() ? HSF_VALVE_ACTIVE_COUNT : 0);
    /* [故障注入] 行程覆盖只作用**承载阀的那台**——卡死/行程过长/开关
     * 未接三类现场故障都在这一侧复现，供 UI 验证超时告警与文案分支。 */
    {
        struct XVirtualModelDigital* valveModel =
            simValveOnExt() ? s_sim.m_digExt : s_sim.m_digMain;
        if (s_valveMoveOverrideMs != 0)
            XVirtualModelDigital_setMoveMs(valveModel, s_valveMoveOverrideMs);
        if (s_valveNoFeedback)
            XVirtualModelDigital_setNoFeedback(valveModel, true);
    }
    /* [DI/DO 初值对齐 Python VirtualDigital]
     *  · DO 全部为 0（Python do_init 缺省 0）：开机没有输出带电。
     *  · DI 静态位 = 0x00005000：按钮组的**急停（DI12）与停止（DI14）
     *    都是常闭触点**——静止时回路通 ⇒ DI=1（按下才是 0）。启动/复位
     *    （DI13/DI15）是常开：未按 = 0。阀区那 12 位由行程动态推导
     *    （开机六路全部"已关" ⇒ 关到位位吸合）。
     *    [2026-11-09 修] 原为 0x00001000（只把急停置位），等于把停止键
     *    当常开 ⇒ 模拟器一开机就认为停止键被按住（服务端判 pressed 恒真，
     *    客户端按钮组卡显示「停止●按下」）。与 Python 侧同改。
     *    ⇒ 两端数字量页开机看到的位图完全一致。 */
    XVirtualModelDigital_setDo(s_sim.m_digMain, 0u);
    XVirtualModelDigital_setDo(s_sim.m_digExt, 0u);
    XVirtualModelDigital_setDi(s_sim.m_digMain, 0x00005000u);
    XVirtualModelDigital_setDi(s_sim.m_digExt, 0u);

    /* ---- 路 B 三台设备的寄存器值按 **2026-11-09 板端实测口径** 布
     * （docs/HSF板端ttyS4三设备取数实测总表_20261109.md §1.1/§1.3/§9）：
     *  · 站 1 浊度仪（模型台 0）：CDAB、恒定 2.29f NTU（实测 20 轮完全
     *    不动 ⇒ 波动幅 0，"恒定"本身是实测口径）；
     *  · 第二台浊度仪（模型台 1；面板站号 2026-11-10 起为 3）：DCBA
     *    （整字节反序=小端 float32）、基准
     *    0.045f NTU **±0.001 缓慢波动** + 温度 19.3℃ **±0.05 缓慢漂移**，
     *    0x0000×4 一帧同刻（实测 40s 窗：浊度 0.04458~0.04577、温度
     *    19.25~19.3125）；
     *  · 站 5 流量计：ABCD、基准 3.6f m³/h **±2% 正弦微波动**（周期 60s）、
     *    空管报警 False、单位码 5=m³/h。
     * [站号跟随 HsfServerConfig.h] 虚拟客户端按 HSF_TURBIDITYx_UNIT_ID /
     * HSF_FLOWMETER_UNIT_ID 绑模型（见 createBusClient），这里不出现任何
     * 写死的站号——改站号只改那个头文件，模拟与真机装配同步生效。
     * [保真口径] 基准值与实测一一对应；波动让"数据在刷新"可见（恒定值
     * 容易误判成界面卡住）。需要逐位断言的用例（回归 §K3）自己用
     * setNtu/setInstantFlow 钉死固定值，与本装配口径互补。 */
    XVirtualModelTurbidity_setWordOrder(s_sim.m_turbidity[0],
                                        (int)XVirtualModelTurbWordOrder_Cdab);
    XVirtualModelTurbidity_setNtu(s_sim.m_turbidity[0], 2.29);
    XVirtualModelTurbidity_setWobble(s_sim.m_turbidity[0], 0.0);
    XVirtualModelTurbidity_setWordOrder(s_sim.m_turbidity[1],
                                        (int)XVirtualModelTurbWordOrder_Dcba);
    XVirtualModelTurbidity_setWobble(s_sim.m_turbidity[1], 0.001);
    XVirtualModelTurbidity_setTempC(s_sim.m_turbidity[1], 19.3);
    XVirtualModelTurbidity_setTempWobble(s_sim.m_turbidity[1], 0.05);
    XVirtualModelFlow_setBaseFlow(s_sim.m_flowMeter, 3.6);

    s_simReady = true;
    XPrintf("HsfServerIoSim: 模拟设备已装配（变频器 %.0fHz 上限 / 温湿度 %.1f℃ %.1f%% / "
            "流量 3.6m3/h ±2%%波动·空管False·单位5 / 浊度 站1 2.29CDAB恒定·"
            "站2 0.045DCBA±0.001+19.3℃±0.05 / 数字量 %d 点（阀在%s模块） / 模拟量 %d 路）\n",
            HSF_SIM_VFD_MAX_HZ,
            (double)265 / 10.0, (double)550 / 10.0,
            HSF_DIG_HOST_EXT[0] ? (HSF_DIG_CHANNELS * 2) : HSF_DIG_CHANNELS,
            simValveOnExt() ? "扩展" : "主",
            HSF_AI_CHANNELS);
    return &s_sim;
}

void HsfServerIoSim_setValveOverride(uint32_t moveMs, bool noFeedback)
{
    /* [幂等] ensure 前随时可改；ensure 后再改对已建模型也即时生效
     * （覆盖值语义不变，模型 setter 本身幂等）。 */
    s_valveMoveOverrideMs = moveMs;
    s_valveNoFeedback = noFeedback;
    if (s_simReady) {
        /* 只作用承载阀的那台模型（与 ensure 时的口径一致）。 */
        struct XVirtualModelDigital* valveModel =
            simValveOnExt() ? s_sim.m_digExt : s_sim.m_digMain;
        if (moveMs != 0)
            XVirtualModelDigital_setMoveMs(valveModel, moveMs);
        if (noFeedback)
            XVirtualModelDigital_setNoFeedback(valveModel, true);
    }
}

void HsfServerIoSim_tick(void)
{
    if (!s_simReady) return;
    /* 时间驱动状态只在心跳拍推进（读路径只序列化当前值——见
     * XModbusVirtualModels.c 各模型注记）。阀行程/浊度波动内部按单调钟
     * 计算，无需在此推进。 */
    XVirtualModelVfd_tick(s_sim.m_vfd);
    XVirtualModelFlow_tick(s_sim.m_flowMeter);
}

void HsfServerIoSim_destroy(void)
{
    /* 模型是**裸 malloc**（XMemory_malloc）出来的，没有 XClass 生命周期，
     * 只能用 free 释放——见 XModbusVirtualModels.c 各 create 的实现。 */
    if (s_sim.m_vfd)      { XFree_System(s_sim.m_vfd);      s_sim.m_vfd = NULL; }
    if (s_sim.m_tempHum)  { XFree_System(s_sim.m_tempHum);  s_sim.m_tempHum = NULL; }
    if (s_sim.m_flowMeter){ XFree_System(s_sim.m_flowMeter);s_sim.m_flowMeter = NULL; }
    if (s_sim.m_digMain)  { XFree_System(s_sim.m_digMain);  s_sim.m_digMain = NULL; }
    if (s_sim.m_digExt)   { XFree_System(s_sim.m_digExt);   s_sim.m_digExt = NULL; }
    if (s_sim.m_analog)   { XFree_System(s_sim.m_analog);   s_sim.m_analog = NULL; }
    if (s_sim.m_turbidity[0]) { XFree_System(s_sim.m_turbidity[0]); s_sim.m_turbidity[0] = NULL; }
    if (s_sim.m_turbidity[1]) { XFree_System(s_sim.m_turbidity[1]); s_sim.m_turbidity[1] = NULL; }
    s_simReady = false;
}

/* ==================== 客户端工厂 ==================== */

XModbusVirtualClient* HsfServerIoSim_createBusClient(bool hasVfd,
                                                     bool hasTempHum,
                                                     bool hasFlowMeter,
                                                     bool hasTurbidity)
{
    const HsfServerIoSimModels* m = HsfServerIoSim_ensure();
    XModbusVirtualClient* c;

    if (!m) return NULL;
    /* 缺省模型留空：本路的所有从站都显式绑定。若某个站忘了绑，
     * Open 会因"无模型"失败并报错——比静默读到别的站的寄存器安全。 */
    c = XModbusVirtualClient_create(NULL);
    if (!c) return NULL;

    if (hasVfd) {
        if (!XModbusVirtualClient_bindUnit(c, HSF_VFD_UNIT_ID,
                                           XVirtualModel_base(m->m_vfd))) {
            XPrintf("HsfServerIoSim: 变频器模型绑定失败（站 %d）\n",
                    HSF_VFD_UNIT_ID);
        }
    }
    if (hasTempHum) {
        if (!XModbusVirtualClient_bindUnit(c, HSF_TEMPHUM_UNIT_ID,
                                           XVirtualModel_base(m->m_tempHum))) {
            XPrintf("HsfServerIoSim: 温湿度模型绑定失败（站 %d）\n",
                    HSF_TEMPHUM_UNIT_ID);
        }
    }
    if (hasFlowMeter) {
        if (!XModbusVirtualClient_bindUnit(c, HSF_FLOWMETER_UNIT_ID,
                                           XVirtualModel_base(m->m_flowMeter))) {
            XPrintf("HsfServerIoSim: 流量计模型绑定失败（站 %d）\n",
                    HSF_FLOWMETER_UNIT_ID);
        }
    }
    /* 浊度仪两台与流量计**共用同一条 485**（现场实测接法），靠站号区分。
     * 每台必须绑自己的模型：只绑一个会让两台读到同一份寄存器（两个页面
     * 读数一模一样），而且不报任何错误——最难查的一类模拟失真。 */
    if (hasTurbidity) {
        static const int s_turbIds[2] = {
            HSF_TURBIDITY1_UNIT_ID, HSF_TURBIDITY2_UNIT_ID
        };
        int t;
        for (t = 0; t < 2; ++t) {
            if (!XModbusVirtualClient_bindUnit(
                    c, s_turbIds[t], XVirtualModel_base(m->m_turbidity[t]))) {
                XPrintf("HsfServerIoSim: 浊度仪%d 模型绑定失败（站 %d）\n",
                        t + 1, s_turbIds[t]);
            }
        }
    }
    /* ---- ttyS4 保真：应答时延 + 站1 长帧后恢复期（2026-11-09 实测口径）----
     * [时延在从站应答路径] 虚拟从站构造应答前按站号休眠（宏缺省=实测值，
     * 可 -D 覆盖，见 HsfServerConfig.h 的 HSF_SIM_* 注记）。真实仪表的应答
     * 从来不是零时延；带上它，主站的站间间隙/静默闸才不是只跑不验的死代码。
     * [恢复期] 站 5 的 49 字节长帧收口后 HSF_SIM_RECOVERY_MS 内站 1 无应答
     * ——主站侧 HSF_TURBIDITY1_TX_QUIET_MS(200) 刻意大于它 ⇒ 联调时站 1
     * 不掉线，但间隙逻辑被真实锻炼。只在与真机同接法的路 B 客户端上生效；
     * 时延数值不进回归断言（CI 抖动），行为方向由 §K 锁。 */
    XModbusVirtualClient_setUnitReplyLatency(c, HSF_TURBIDITY1_UNIT_ID,
                                             HSF_SIM_TURBIDITY1_REPLY_MS_MIN,
                                             HSF_SIM_TURBIDITY1_REPLY_MS_MAX);
    XModbusVirtualClient_setUnitReplyLatency(c, HSF_TURBIDITY2_UNIT_ID,
                                             HSF_SIM_TURBIDITY2_REPLY_MS_MIN,
                                             HSF_SIM_TURBIDITY2_REPLY_MS_MAX);
    XModbusVirtualClient_setUnitReplyLatency(c, HSF_FLOWMETER_UNIT_ID,
                                             HSF_SIM_FLOWMETER_REPLY_MS_MIN,
                                             HSF_SIM_FLOWMETER_REPLY_MS_MAX);
    XModbusVirtualClient_setRecoveryGate(c, HSF_FLOWMETER_UNIT_ID,
                                         HSF_TURBIDITY1_UNIT_ID,
                                         HSF_SIM_RECOVERY_MS);
    return c;
}

XModbusVirtualClient* HsfServerIoSim_createDigitalMain(void)
{
    const HsfServerIoSimModels* m = HsfServerIoSim_ensure();
    if (!m) return NULL;
    /* 数字量站是**独占 Hub**（不与 485 共线），故用缺省模型即可，
     * 不必 bindUnit——但站号仍是 HSF_DIG_UNIT_ID，故两个都设上，
     * 让"按站号分派"和"缺省回落"两条路径都成立。
     * [主模块 = 现场 HSF_DIG_HOST 那台（当前 .13）] */
    {
        XModbusVirtualClient* c = XModbusVirtualClient_create(
            XVirtualModel_base(m->m_digMain));
        if (c) XModbusVirtualClient_bindUnit(c, HSF_DIG_UNIT_ID,
                                             XVirtualModel_base(m->m_digMain));
        return c;
    }
}

XModbusVirtualClient* HsfServerIoSim_createDigitalExt(void)
{
    const HsfServerIoSimModels* m = HsfServerIoSim_ensure();
    if (!m) return NULL;
    {
        XModbusVirtualClient* c = XModbusVirtualClient_create(
            XVirtualModel_base(m->m_digExt));
        if (c) XModbusVirtualClient_bindUnit(c, HSF_DIG_UNIT_ID,
                                             XVirtualModel_base(m->m_digExt));
        return c;
    }
}

XModbusVirtualClient* HsfServerIoSim_createAnalog(void)
{
    const HsfServerIoSimModels* m = HsfServerIoSim_ensure();
    if (!m) return NULL;
    {
        XModbusVirtualClient* c = XModbusVirtualClient_create(
            XVirtualModel_base(m->m_analog));
        if (c) XModbusVirtualClient_bindUnit(c, HSF_AI_UNIT_ID,
                                             XVirtualModel_base(m->m_analog));
        return c;
    }
}

#endif /* HSF_SIM_MODE || 1 */
