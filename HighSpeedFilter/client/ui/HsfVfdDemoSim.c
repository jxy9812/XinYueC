/**
 * @file       HsfVfdDemoSim.c
 * @brief      变频器模拟源实现：XIoLink 子类（进程内假从站）+ ATV61 装配。
 * @details    [链路职责] XIoLink 子类只做两件事：
 *               1) BindRole：记录 role → 内部遥测槽（本实现无真实地址）；
 *               2) 数据生成：tick 时按 ATV61 品牌语义更新仿真值，经
 *                  XIoChannelGroup_feedRole 推入通道组（设备层照常解码）。
 *             [同步链路口径] 写意图（CMD/LFR1/...）在 WriteRole 内直接生效
 *             并 completeWrite，IsAsync 返回 false —— 设备层不 arm 监督
 *             定时器，动作立即完成（XIoLink.h 的同步链路契约）。
 *
 *             [剧本状态机] STOPPED（停机待命 2s）→ RAMP_UP（0→50Hz 斜坡）
 *             → RUN（恒速 + 遥测摆动 6s）→ RAMP_DOWN（减速回 0）→ 循环；
 *             每第 3 圈在恒速段注入故障 OCF(9) 3s 后自动复位（演示故障
 *             横幅与复位交互）。
 *
 *             [为什么不复用 Test 的从站模拟器] Test/xvfd_slave_emu.h 是
 *             真实 Modbus TCP 从站（测试用），这里要的是「无网络栈、零端口、
 *             随 HsfClient 进程内跑」的演示源，两者目标不同；语义（ETA 位、
 *             0.1Hz 定点、LFT 码）严格对齐 ATV61 手册与 Test 侧同款。
 */
#include "HsfVfdDemoSim.h"

#if XVFD_ON

#include "XIoChannelGroup.h"
#include "XIoLink.h"
#include "XIoAnalogPoint.h"
#include "XVfdAtv61.h"
#include "XObject.h"
#include "XMemory.h"
#include "XDateTime.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ==================== 类定义：XIoLink 子类 ==================== */

typedef struct HsfVfdDemoSim HsfVfdDemoSim;

/**
 * @brief 假从站链路（XIoLink 子类）：进程内遥测生成器。
 */
typedef struct HsfVfdSimLink {
    XIoLink m_base;                     /**< 基类子对象（第一成员）。 */
    HsfVfdDemoSim* m_sim;               /**< 回属模拟源（tick 生成遥测）。 */
    uint32_t m_boundMask[(XVfdRole_Count + 31) / 32]; /**< role 绑定位图。 */
} HsfVfdSimLink;

/* 模拟源完整类型（链路前置声明后才可内嵌）。 */
struct HsfVfdDemoSim {
    XIoChannelGroup* m_group;           /**< 通道组（点托管）。 */
    HsfVfdSimLink* m_link;              /**< 假从站链路（组挂接）。 */
    XVfdAtv61* m_drive;                 /**< ATV61 设备（继承 XVfdDevice）。 */
    /* ---- 通知桥 ---- */
    HsfVfdNotifyFn m_cb;                /**< 页面回调（单页注册即覆盖）。 */
    void* m_page;                       /**< 页面令牌。 */
    /* ---- 仿真剧本 ---- */
    int m_phase;                        /**< 剧本阶段（Phase 枚举）。 */
    int64_t m_phaseMs;                  /**< 当前阶段已推进毫秒。 */
    int m_cycle;                        /**< 已完成圈数。 */
    int m_faultMs;                      /**< 故障剩余毫秒（0=无故障）。 */
    /* ---- 遥测槽（原生口径，ATV61 定点） ---- */
    uint16_t m_eta;                     /**< 3201 状态字。 */
    uint16_t m_eti;                     /**< 3206 辅助状态字。 */
    uint16_t m_lft;                     /**< 7121 故障码。 */
    uint16_t m_rfr;                     /**< 3202 输出频率 0.1Hz。 */
    uint16_t m_frh;                     /**< 3203 斜坡前给定 0.1Hz。 */
    uint16_t m_lcr;                     /**< 3204 电流 A。 */
    uint16_t m_otr;                     /**< 3205 转矩 0.1%。 */
    uint16_t m_uln;                     /**< 3207 电网电压 0.1V。 */
    uint16_t m_uop;                     /**< 3208 输出电压 1V。 */
    uint16_t m_thd;                     /**< 3209 变频器热状态 %。 */
    uint16_t m_vbus;                    /**< 3243 母线电压 0.1V。 */
    uint16_t m_rfrd;                    /**< 8604 输出转速 rpm。 */
    uint16_t m_thr;                     /**< 9630 电机热状态 %。 */
    /* ---- 给定（WriteRole 回写） ---- */
    uint16_t m_cmd;                     /**< 8501 命令字。 */
    uint16_t m_lfr1;                    /**< 8521 频率给定 0.1Hz（有符号语义）。 */
    uint16_t m_lfrd;                    /**< 8602 转速给定 rpm。 */
    uint16_t m_acc;                     /**< 9001 加速时间 0.01s。 */
    uint16_t m_dec;                     /**< 9002 减速时间 0.01s。 */
    bool m_reverse;                     /**< 方向位缓存（CMD bit11）。 */
};

/* ==================== 剧本阶段 ==================== */

enum { Phase_RampUp = 0, Phase_Run = 1, Phase_RampDown = 2, Phase_Stopped = 3, Phase_COUNT = 4 };
#define SIM_TICK_MS      200     /**< 链路推进拍（与页面驱动定时器一致）。 */
#define SIM_RAMPUP_MS    5000    /**< 0→50Hz 斜坡时长。 */
#define SIM_RUN_MS       6000    /**< 恒速运行时长。 */
#define SIM_RAMPDOWN_MS  4000    /**< 减速回零时长。 */
#define SIM_STOPPED_MS   800     /**< 停机待命时长（短：截图落在斜坡段）。 */
#define SIM_FAULT_EVERY  3       /**< 每 3 圈注入一次故障。 */
#define SIM_FAULT_MS     3000    /**< 故障持续时间。 */
#define SIM_TARGET_HZ10  500     /**< 目标频率 50.0Hz（0.1Hz 定点）。 */

/* ==================== vtable 覆写 ==================== */

static bool VSimLink_bindRole(XIoLink* link, uint16_t role, uint32_t nativeAddr, uint32_t aux)
{
    HsfVfdSimLink* self = (HsfVfdSimLink*)link;
    (void)nativeAddr; (void)aux;    /* 假从站无真实地址，role 即槽号。 */
    if (!self || role >= XVfdRole_Count) return false;
    self->m_boundMask[role >> 5] |= (1u << (role & 31));
    return true;
}

static bool VSimLink_readRole(XIoLink* link, uint16_t role, int32_t* outRaw)
{
    (void)link; (void)role;
    /* 本链路数据全走 feedRole 推送，不支持拉取（返回 false = 无缓存值）。 */
    if (outRaw) *outRaw = 0;
    return false;
}

/** @brief 写意图落定：按 role 回写仿真给定槽（同步完成）。 */
static bool VSimLink_writeRole(XIoLink* link, uint16_t role, int32_t raw)
{
    HsfVfdSimLink* self = (HsfVfdSimLink*)link;
    HsfVfdDemoSim* sim;
    if (!self || !self->m_sim || !self->m_sim->m_group) return false;
    sim = self->m_sim;
    switch (role) {
    case XVfdRole_CommandWord:
        sim->m_cmd = (uint16_t)raw;
        sim->m_reverse = ((raw >> 11) & 1) != 0;
        break;
    case XVfdRole_FreqSet:   sim->m_lfr1 = (uint16_t)(int16_t)raw; break;
    case XVfdRole_SpeedSet:  sim->m_lfrd = (uint16_t)raw; break;
    case XVfdRole_AccelTime: sim->m_acc = (uint16_t)raw; break;
    case XVfdRole_DecelTime: sim->m_dec = (uint16_t)raw; break;
    default: return false;          /* 未仿真给定写：拒绝（组层报失败沿）。 */
    }
    /* 同步链路：返回前完成写落定（不 arm 设备层监督定时器）。 */
    XIoChannelGroup_completeWrite(sim->m_group, role, true);
    return true;
}

static bool VSimLink_isAsync(const XIoLink* link)
{
    (void)link;
    return false;                   /* 同步链路：写意图返回前已落定。 */
}

static bool VSimLink_start(XIoLink* link)
{
    (void)link;                     /* 假从站无资源需启停。 */
    return true;
}

static void VSimLink_stop(XIoLink* link)
{
    (void)link;
}

/* 假从站链路类（XIoLink 直派生；重载全部链路槽）。 */
XCLASS_DEFINE_BEGING(HsfVfdSimLink)
XCLASS_DEFINE_EXTEND_END(HsfVfdSimLink, XIoLink)

XVtable* HsfVfdSimLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfVfdSimLink)
    XVTABLE_INHERIT_XCLASS(XIoLink);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_BindRole, VSimLink_bindRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_ReadRole, VSimLink_readRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_WriteRole, VSimLink_writeRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_IsAsync, VSimLink_isAsync);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Start, VSimLink_start);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Stop, VSimLink_stop);
    XCLASS_SHOW_SIZE_DEFAULT(HsfVfdSimLink);
    return XVTABLE_DEFAULT;
}

/* ==================== 遥测生成（ATV61 品牌语义） ==================== */

/** @brief CiA402 命令字状态机（语义对齐 Test/xvfd_slave_emu.h 同款）。 */
static void simApplyCmd(HsfVfdDemoSim* sim, uint16_t cmd)
{
    int bit7 = (cmd >> 7) & 1;
    int bit3 = (cmd >> 3) & 1;
    int bit1 = (cmd >> 1) & 1;
    int bit0 = cmd & 1;
    sim->m_reverse = ((cmd >> 11) & 1) != 0;
    if (sim->m_lft && !bit7) return;            /* 故障中命令被抑制。 */
    if (bit7) {                                 /* Fault reset 上升沿。 */
        sim->m_lft = 0;
        sim->m_phase = Phase_Stopped;           /* 复位后回停机待命。 */
        sim->m_phaseMs = 0;
        return;
    }
    if (!bit1 && !bit0) {                       /* Disable voltage → 状态 2。 */
        sim->m_phase = Phase_Stopped;
        sim->m_phaseMs = 0;
    } else if (!bit3) {                         /* Shutdown/Switch on/Disable op。 */
        /* 就绪态：回到待命但不停机剧情（由剧本主导运行）。 */
        sim->m_phase = Phase_Stopped;
        sim->m_phaseMs = 0;
    }
    /* Enable operation（bit3=1）：剧本已处于运行相位，自然衔接。 */
}

/** @brief 依当前剧本相位推进全部遥测槽（原生口径）。 */
static void simGenerateTelemetry(HsfVfdDemoSim* sim, int64_t nowMs)
{
    int targetHz10 = SIM_TARGET_HZ10;
    int hz10 = 0;
    bool running = false;

    /* ---- 剧本相位推进 ---- */
    if (sim->m_lft) {
        /* 故障中：输出频率沿减速到 0，不推进正常剧本。 */
        if (sim->m_faultMs > 0) {
            sim->m_faultMs -= SIM_TICK_MS;
            if (sim->m_faultMs <= 0) sim->m_lft = 0;    /* 自动复位（演示）。 */
        }
        sim->m_eta = XVfdAtv61_EtaStateFault | 0x0010u;
        targetHz10 = sim->m_rfr;                /* 故障即冻结/滑落。 */
        hz10 = sim->m_rfr > 40 ? sim->m_rfr - 40 : 0;
    } else {
        switch (sim->m_phase) {
        case Phase_RampUp:
            running = true;
            hz10 = (int)(SIM_TARGET_HZ10 * sim->m_phaseMs / SIM_RAMPUP_MS);
            if (sim->m_phaseMs >= SIM_RAMPUP_MS) {
                sim->m_phase = Phase_Run; sim->m_phaseMs = 0;
            }
            break;
        case Phase_Run:
            running = true;
            hz10 = SIM_TARGET_HZ10;
            if (sim->m_phaseMs >= SIM_RUN_MS) {
                sim->m_phase = Phase_RampDown; sim->m_phaseMs = 0;
            }
            break;
        case Phase_RampDown:
            running = true;
            hz10 = SIM_TARGET_HZ10 -
                   (int)(SIM_TARGET_HZ10 * sim->m_phaseMs / SIM_RAMPDOWN_MS);
            if (sim->m_phaseMs >= SIM_RAMPDOWN_MS) {
                sim->m_phase = Phase_Stopped; sim->m_phaseMs = 0;
                ++sim->m_cycle;
            }
            break;
        default:                                /* Phase_Stopped。 */
            hz10 = 0;
            if (sim->m_phaseMs >= SIM_STOPPED_MS) {
                sim->m_phase = Phase_RampUp; sim->m_phaseMs = 0;
            }
            break;
        }
        if (sim->m_phaseMs < 0) sim->m_phaseMs = 0;
        sim->m_phaseMs += SIM_TICK_MS;
    }
    if (hz10 < 0) hz10 = 0;
    if (hz10 > SIM_TARGET_HZ10) hz10 = SIM_TARGET_HZ10;
    sim->m_rfr = (uint16_t)hz10;
    sim->m_frh = (uint16_t)targetHz10;

    /* ---- ETA 状态字 ---- */
    {
        uint16_t eta = 0x0010u;                 /* Voltage enabled 恒置。 */
        if (running) {
            eta |= XVfdAtv61_EtaStateRunning | XVfdAtv61_EtaOperationEnabled;
            eta |= XVfdAtv61_EtaRemote;         /* 命令来自总线。 */
            if (hz10 >= SIM_TARGET_HZ10) eta |= XVfdAtv61_EtaTargetReached;
        } else if (sim->m_lft) {
            eta |= XVfdAtv61_EtaFault;
        } else {
            eta |= XVfdAtv61_EtaStateSwitched;  /* Switched on（待机）。 */
        }
        if (sim->m_reverse && running) eta |= XVfdAtv61_EtaReverse;
        sim->m_eta = eta;
    }

    /* ---- 随动遥测（演示曲线有生命感；正弦摆动） ---- */
    {
        double frac = (double)hz10 / (double)SIM_TARGET_HZ10;   /* 0..1。 */
        double osc = sin((double)nowMs / 900.0);                /* -1..1。 */
        double oscSlow = sin((double)nowMs / 2100.0);
        /* 电流：比例负载 + 摆动（8~26A）。 */
        sim->m_lcr = (uint16_t)(8.0 + 18.0 * frac + 2.0 * osc);
        /* 转矩：0.1% 定点（15%~95%）。 */
        sim->m_otr = (uint16_t)((15.0 + 80.0 * frac + 6.0 * oscSlow) * 10.0);
        /* 输出电压随频率（0~380V）。 */
        sim->m_uop = (uint16_t)(380.0 * frac);
        /* 母线电压：轻摆（530~560V，0.1V 定点）。 */
        sim->m_vbus = (uint16_t)((545.0 + 12.0 * oscSlow) * 10.0);
        /* 电网电压恒定 380.0V。 */
        sim->m_uln = 3800;
        /* 转速：40 极对演示（rpm = Hz × 30）。 */
        sim->m_rfrd = (uint16_t)((double)hz10 / 10.0 * 30.0 + 0.5);
        /* 热状态缓升缓降。 */
        {
            int thd = sim->m_thd;
            if (running && thd < 62) thd += 1;
            else if (!running && thd > 25) thd -= 1;
            sim->m_thd = (uint16_t)thd;
        }
        {
            int thr = sim->m_thr;
            if (running && thr < 55) thr += 1;
            else if (!running && thr > 30) thr -= 1;
            sim->m_thr = (uint16_t)thr;
        }
        /* ETI：加减速位（斜坡相位）。 */
        sim->m_eti = (sim->m_phase == Phase_RampUp && running) ? XVfdAtv61_EtiAccel
                   : ((sim->m_phase == Phase_RampDown && running) ? XVfdAtv61_EtiDecel : 0);
    }

    /* ---- 周期故障注入（恒速段；展示横幅/复位按钮） ---- */
    if (!sim->m_lft && running &&
        sim->m_phase == Phase_Run && sim->m_faultMs == 0 &&
        sim->m_cycle > 0 && (sim->m_cycle % SIM_FAULT_EVERY) == 0) {
        sim->m_lft = 9;                         /* OCF 过流。 */
        sim->m_faultMs = SIM_FAULT_MS;
        sim->m_cycle++;                         /* 防同圈重复注入。 */
    }
}

/** @brief 全部遥测槽 feedRole 推进通道组（值未变时组层静默）。 */
static void simFeedAll(HsfVfdDemoSim* sim)
{
    XIoChannelGroup* g = sim->m_group;
    if (!g) return;
    XIoChannelGroup_feedRole(g, XVfdRole_StatusWord,     (int32_t)sim->m_eta);
    XIoChannelGroup_feedRole(g, XVfdRole_AuxStateWord,   (int32_t)sim->m_eti);
    XIoChannelGroup_feedRole(g, XVfdRole_FaultCode,      (int32_t)sim->m_lft);
    XIoChannelGroup_feedRole(g, XVfdRole_OutputFreq,     (int32_t)sim->m_rfr);
    XIoChannelGroup_feedRole(g, XVfdRole_SetFreq,        (int32_t)sim->m_frh);
    XIoChannelGroup_feedRole(g, XVfdRole_OutputCurrent,  (int32_t)sim->m_lcr);
    XIoChannelGroup_feedRole(g, XVfdRole_OutputVoltage,  (int32_t)sim->m_uop);
    XIoChannelGroup_feedRole(g, XVfdRole_MainsVoltage,   (int32_t)sim->m_uln);
    XIoChannelGroup_feedRole(g, XVfdRole_BusVoltage,     (int32_t)sim->m_vbus);
    XIoChannelGroup_feedRole(g, XVfdRole_Torque,         (int32_t)sim->m_otr);
    XIoChannelGroup_feedRole(g, XVfdRole_SpeedRpm,       (int32_t)sim->m_rfrd);
    XIoChannelGroup_feedRole(g, XVfdRole_DriveThermal,   (int32_t)sim->m_thd);
    XIoChannelGroup_feedRole(g, XVfdRole_MotorThermal,   (int32_t)sim->m_thr);
}

/* ==================== 设备信号桥 ==================== */

/* 单通知槽（模拟源单例语义：同刻只有一页在册；connect_2 无接收者路径）。 */
static HsfVfdNotifyFn g_simNotifyCb = NULL;
static void* g_simNotifyPage = NULL;
static const HsfVfdProvider* g_simNotifyProvider = NULL;

/** @brief 设备快照发布 → 页面回调（桥接信号 → 函数表通知）。
 *  @note  connect_2 无接收者对象：sender 即发射方。 */
static void simOnStatusUpdated(XObject* sender, XVarList* args)
{
    (void)args; (void)sender;
    if (g_simNotifyCb && g_simNotifyPage)
        g_simNotifyCb(g_simNotifyProvider, g_simNotifyPage);
}

/* ==================== 对外接口 ==================== */

/* 模块级单例（tick(NULL) 的解析目标；HsfVfdProvider_demo 懒创建后登记）。 */
static HsfVfdDemoSim* g_simInstance = NULL;

void hsfVfdSimSetInstance(HsfVfdDemoSim* sim)
{
    g_simInstance = sim;
}

const XVfdDevice* hsfVfdSimDevice(const HsfVfdDemoSim* sim)
{
    /* NULL=取单例（页面定时器只管拍，不管装配）。 */
    if (!sim) sim = g_simInstance;
    return (sim && sim->m_drive) ? (const XVfdDevice*)sim->m_drive : NULL;
}

XVfdDevice* hsfVfdSimDeviceMut(HsfVfdDemoSim* sim)
{
    return (sim && sim->m_drive) ? (XVfdDevice*)sim->m_drive : NULL;
}

const char* hsfVfdSimFaultText(const HsfVfdDemoSim* sim, uint16_t code)
{
    (void)sim;
    /* 品牌文本查询（ATV61 字母码 + 中文说明）。 */
    return XVfdAtv61_faultText(code);
}

void hsfVfdSimTick(HsfVfdDemoSim* sim)
{
    if (!sim) sim = g_simInstance;      /* NULL=推进模块单例。 */
    if (!sim) return;
    simGenerateTelemetry(sim, XDateTime_currentMSecsSinceEpoch());
    simFeedAll(sim);
}bool hsfVfdSimSubscribe(HsfVfdDemoSim* sim, HsfVfdNotifyFn cb, void* page)
{
    if (!sim || !cb) return false;
    g_simNotifyCb = cb;
    g_simNotifyPage = page;
    /* provider 表指针由 Provider.c 在注册时经本回填（见 hsfVfdSimSetProvider）。 */
    return true;
}

void hsfVfdSimSetProvider(const HsfVfdProvider* provider)
{
    g_simNotifyProvider = provider;
}

void hsfVfdSimUnsubscribe(HsfVfdDemoSim* sim, void* page)
{
    if (!sim || g_simNotifyPage != page) return;
    g_simNotifyCb = NULL;
    g_simNotifyPage = NULL;
}

HsfVfdDemoSim* hsfVfdSimCreate(void)
{
    HsfVfdDemoSim* sim;
    const XVfdPointSpec* specs;
    int specCount = 0;
    int i;

    sim = (HsfVfdDemoSim*)XMemory_malloc(sizeof(HsfVfdDemoSim),
                                         XCLASS_DEFAULT_MEMORY_TYPE);
    if (!sim) return NULL;
    memset(sim, 0, sizeof(*sim));
    /* 演示开场即恒速运行（截图/首屏即满数据；后续按剧本循环）。
     * XHSF_VFD_IDLE=1：恒停机态（验证「启动」字样/点动使能等停止态 UI）。 */
    if (XSystem_environment("XHSF_VFD_IDLE")) {
        sim->m_phase = Phase_Stopped;
        sim->m_phaseMs = 0;
    } else {
        sim->m_phase = Phase_Run;
        sim->m_phaseMs = SIM_RAMPUP_MS;
    }
    sim->m_thd = 45;
    sim->m_thr = 40;

    /* ---- 通道组 + 假从站链路 ---- */
    sim->m_group = XIoChannelGroup_create();
    sim->m_link = (HsfVfdSimLink*)XMemory_malloc(sizeof(HsfVfdSimLink),
                                                 XCLASS_DEFAULT_MEMORY_TYPE);
    if (!sim->m_group || !sim->m_link) goto fail;
    memset(sim->m_link, 0, sizeof(HsfVfdSimLink));
    XIoLink_init(&sim->m_link->m_base);
    XClassGetVtable(sim->m_link) = HsfVfdSimLink_class_init();
    sim->m_link->m_sim = sim;
    if (!XIoChannelGroup_setLink(sim->m_group, &sim->m_link->m_base)) goto fail;
    if (!XIoLink_start_base(&sim->m_link->m_base)) goto fail;

    /* ---- ATV61 设备装配（点规格逐条建点绑定；标度不在通道层） ---- */
    sim->m_drive = XVfdAtv61_create();
    if (!sim->m_drive) goto fail;
    if (!XVfdDevice_attach((XVfdDevice*)sim->m_drive, sim->m_group)) goto fail;
    specs = XVfdAtv61_pointSpecs(&specCount);
    for (i = 0; i < specCount; ++i) {
        /* [2026-11-03 数字/模拟拆分] AI/AO 用 XIoAnalogPoint 子类（承载
         * 标度四件套；本工程语义点标度恒 1/1，但类型要求模拟量走子类）。 */
        XIoPoint* pt = (specs[i].m_type == (uint8_t)XIoPoint_AnalogInput ||
                        specs[i].m_type == (uint8_t)XIoPoint_AnalogOutput)
                           ? (XIoPoint*)XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
                           : XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!pt) goto fail;
        XIoPoint_setRole(pt, specs[i].m_role);
        XIoPoint_setType(pt, (XIoPointType)specs[i].m_type);
        XIoPoint_setTag(pt, specs[i].m_tag);
        XIoPoint_setSigned(pt, specs[i].m_signed != 0);
        if (specs[i].m_type == (uint8_t)XIoPoint_AnalogInput ||
            specs[i].m_type == (uint8_t)XIoPoint_AnalogOutput) {
            XIoAnalogPoint_setScale((XIoAnalogPoint*)pt, 1, 1, 0, 0);
            if (specs[i].m_type == (uint8_t)XIoPoint_AnalogInput)
                XIoPoint_setUseHolding(pt, true);   /* ATV61 全 HR 区。 */
        }
        if (!XIoChannelGroup_addPoint(sim->m_group, pt, true)) {
            XClassDelete((XClass*)pt);
            goto fail;
        }
        if (!XVfdDevice_bindPoint((XVfdDevice*)sim->m_drive, &specs[i], pt)) {
            (void)XIoChannelGroup_removeRole(sim->m_group, specs[i].m_role);
            goto fail;
        }
        (void)XIoLink_bindRole_base(&sim->m_link->m_base, specs[i].m_role, 0, 0);
    }
    XVfdDevice_setStatusInterval((XVfdDevice*)sim->m_drive, 100);
    XObject_connect_2((XObject*)sim->m_drive,
                      XSignal(XVfdDevice_statusUpdated_signal),
                      simOnStatusUpdated);
    return sim;

fail:
    if (sim->m_drive) {
        XVfdDevice_detach((XVfdDevice*)sim->m_drive);
        XClassDelete((XClass*)sim->m_drive);
    }
    if (sim->m_link) XMemory_free(sim->m_link, XCLASS_DEFAULT_MEMORY_TYPE);
    if (sim->m_group) XClassDelete((XClass*)sim->m_group);
    XMemory_free(sim, XCLASS_DEFAULT_MEMORY_TYPE);
    return NULL;
}

#endif /* XVFD_ON */
