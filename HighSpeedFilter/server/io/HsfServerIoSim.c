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

/* M512 最大频率（Hz）：与 XVfdM512 的 50Hz 口径一致。 */
#define HSF_SIM_VFD_MAX_HZ   50.0

const HsfServerIoSimModels* HsfServerIoSim_ensure(void)
{
    if (s_simReady) return &s_sim;

    memset(&s_sim, 0, sizeof(s_sim));
    s_sim.m_vfd = XVirtualModelVfd_create(HSF_SIM_VFD_MAX_HZ);
    s_sim.m_tempHum = XVirtualModelTempHum_create();
    s_sim.m_flowMeter = XVirtualModelFlow_create();
    s_sim.m_dig12 = XVirtualModelDigital_create(HSF_SIM_DIG_CHANNELS_PER_DEV);
    s_sim.m_dig13 = XVirtualModelDigital_create(HSF_SIM_DIG_CHANNELS_PER_DEV);
    s_sim.m_analog = XVirtualModelAnalog_create(HSF_AI_CHANNELS);
    /* 浊度仪两台：index 决定基准浊度与波动相位（0=原水高、1=出水低）。
     * 两台必须能看出是两台独立仪器，而不是同一个值贴两遍。 */
    s_sim.m_turbidity[0] = XVirtualModelTurbidity_create(0);
    s_sim.m_turbidity[1] = XVirtualModelTurbidity_create(1);
    if (!s_sim.m_vfd || !s_sim.m_tempHum || !s_sim.m_flowMeter ||
        !s_sim.m_dig12 || !s_sim.m_dig13 || !s_sim.m_analog ||
        !s_sim.m_turbidity[0] || !s_sim.m_turbidity[1]) {
        XPrintf("HsfServerIoSim: 模型创建失败（内存不足）\n");
        HsfServerIoSim_destroy();
        return NULL;
    }
    /* 数字量两台都要关掉到位联动？**不关**：
     *  - .12（逻辑点 0..15）与 .13（16..31）各自维护 16 位的 DI/DO 位图；
     *  - 电磁阀的 6 路点位全部落在 .13 那半区（HSF_VALVE_* 宏），
     *    联动必须由 .13 承担，否则阀永远停在"动作中"，超时告警路径走不到；
     *  - .12 建模成"工艺侧非阀类点"：只给缺省 DI，不参与阀联动（避免
     *    写 DO 时凭空改动 DI，掩盖真实的"无反馈"诊断）。
     * 两台的缺省行为都是 interlock=true，故这里显式区分。 */
    XVirtualModelDigital_setValveInterlock(s_sim.m_dig12, false);
    XVirtualModelDigital_setValveInterlock(s_sim.m_dig13, true);

    s_simReady = true;
    XPrintf("HsfServerIoSim: 模拟设备已装配（变频器 %.0fHz 上限 / 温湿度 %.1f℃ %.1f%% / "
            "流量 %.2fm3/h / 浊度 12.5与3.2NTU / 数字量 16+16 / 模拟量 %d 路）\n",
            HSF_SIM_VFD_MAX_HZ,
            (double)265 / 10.0, (double)550 / 10.0, (double)12.53,
            HSF_AI_CHANNELS);
    return &s_sim;
}

void HsfServerIoSim_destroy(void)
{
    /* 模型是**裸 malloc**（XMemory_malloc）出来的，没有 XClass 生命周期，
     * 只能用 free 释放——见 XModbusVirtualModels.c 各 create 的实现。 */
    if (s_sim.m_vfd)      { XFree_System(s_sim.m_vfd);      s_sim.m_vfd = NULL; }
    if (s_sim.m_tempHum)  { XFree_System(s_sim.m_tempHum);  s_sim.m_tempHum = NULL; }
    if (s_sim.m_flowMeter){ XFree_System(s_sim.m_flowMeter);s_sim.m_flowMeter = NULL; }
    if (s_sim.m_dig12)    { XFree_System(s_sim.m_dig12);    s_sim.m_dig12 = NULL; }
    if (s_sim.m_dig13)    { XFree_System(s_sim.m_dig13);    s_sim.m_dig13 = NULL; }
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
    return c;
}

XModbusVirtualClient* HsfServerIoSim_createDigitalMain(void)
{
    const HsfServerIoSimModels* m = HsfServerIoSim_ensure();
    if (!m) return NULL;
    /* 数字量站是**独占 Hub**（不与 485 共线），故用缺省模型即可，
     * 不必 bindUnit——但站号仍是 HSF_DIG_UNIT_ID，故两个都设上，
     * 让"按站号分派"和"缺省回落"两条路径都成立。 */
    {
        XModbusVirtualClient* c = XModbusVirtualClient_create(
            XVirtualModel_base(m->m_dig12));
        if (c) XModbusVirtualClient_bindUnit(c, HSF_DIG_UNIT_ID,
                                             XVirtualModel_base(m->m_dig12));
        return c;
    }
}

XModbusVirtualClient* HsfServerIoSim_createDigitalExt(void)
{
    const HsfServerIoSimModels* m = HsfServerIoSim_ensure();
    if (!m) return NULL;
    {
        XModbusVirtualClient* c = XModbusVirtualClient_create(
            XVirtualModel_base(m->m_dig13));
        if (c) XModbusVirtualClient_bindUnit(c, HSF_DIG_UNIT_ID,
                                             XVirtualModel_base(m->m_dig13));
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
