/**
 * @file       XModbusVirtualModels.c
 * @brief      具体设备模型实现（变频器 / 数字量 / 模拟量 / 温湿度 / 流量计）。
 * @details    每个模型只实现"寄存器读写字"，不含业务语义：
 *              · 不解释功能码的业务含义；
 *              · 不做单位换算（模型里存的就是线上原始字）；
 *              · 不解码状态字/故障码。
 *            业务语义由真实设备类（XVfdM512 / XFlowMeter / …）负责，只有一份。
 *
 *            [寄存器地图来源] 逐项对齐现场实测与 Python 侧 devices.py：
 *              · M512：0x1000 设定频率(±10000=±100%)、0x1001 输出频率(0.1Hz)、
 *                0x1002 母线电压、0x1004 输出电流、0x2000 命令字、
 *                0x3000 运行状态字、0x8000 故障码；
 *              · MT3A：DI=FC02@0、DO=FC01/FC05@0，单台最多 16 点，
 *                超 16 点或 addr+count 跨出 16 ⇒ EXC03/EXC02（实测硬约束）；
 *              · 模拟量：FC04 输入寄存器 0x0000..N，毫伏口径；
 *              · 温湿度：FC04 输入寄存器 0x0000 温度 / 0x0001 湿度，各 ×0.1；
 *              · 流量计：FC03 保持寄存器 0x1010.. 变量区（32 位量高字在前）。
 */
#include "XModbusVirtualModel.h"

#if XMODBUS_ON && XMODBUS_CLIENT_ON

#include "XMemory.h"
#include "XDateTime.h"     /* 阀行程计时（与上层阀状态机同一个计时源）。 */
#include <string.h>
#include <stdlib.h>
#include <math.h>          /* sin：浊度模型的波动曲线（两台相位错开）。 */

/* 功能码（避免依赖具体枚举名，按 Modbus 规范字面值）。 */
#define FC_READ_COILS        0x01u
#define FC_READ_DISCRETE     0x02u
#define FC_READ_HOLDING      0x03u
#define FC_READ_INPUT        0x04u
#define FC_WRITE_COIL        0x05u
#define FC_WRITE_REGISTER    0x06u
#define FC_WRITE_COILS       0x0Fu
#define FC_WRITE_REGISTERS   0x10u

/* ==================== 虚表转发 ==================== */

XModbusVirtualResult XModbusVirtualModel_read(struct XModbusVirtualModel* model,
                                              uint8_t fc, uint16_t addr,
                                              uint16_t count, uint16_t* out,
                                              uint16_t outCap, uint16_t* outLen)
{
    if (outLen) *outLen = 0;
    if (!model || !model->m_vtable || !model->m_vtable->read)
        return XModbusVirtual_IllegalAddress;
    return model->m_vtable->read(model->m_self, (uint8_t)(fc & 0x7Fu), addr,
                                 count, out, outCap, outLen);
}

XModbusVirtualResult XModbusVirtualModel_write(struct XModbusVirtualModel* model,
                                               uint8_t fc, uint16_t addr,
                                               uint16_t count,
                                               const uint16_t* values)
{
    if (!model || !model->m_vtable || !model->m_vtable->write)
        return XModbusVirtual_IllegalAddress;
    return model->m_vtable->write(model->m_self, (uint8_t)(fc & 0x7Fu), addr,
                                  count, values);
}

/* ============================================================================
 *                            变频器（M512 / ATV61）
 * ============================================================================ */

typedef struct XVirtualModelVfd {
    XModbusVirtualModel m_base;
    double m_maxHz;
    double m_setHz;       /**< 设定频率（Hz）。 */
    double m_outHz;       /**< 输出频率（Hz；运行时向设定值爬升）。 */
    bool m_running;
    uint16_t m_faultCode;
    uint64_t m_tick;
} XVirtualModelVfd;

/* M512 命令字（与 XVfdM512.c 同口径：0x2000 写命令）。 */
#define VFD_CMD_STOP          1u
#define VFD_CMD_FORWARD       2u
#define VFD_CMD_REVERSE       3u
#define VFD_CMD_FWD_JOG       4u
#define VFD_CMD_REV_JOG       5u
#define VFD_CMD_FAULT_RESET   6u

/* 运行状态字（0x3000）位：与 XVfdM512.c 的解码口径一致。 */
#define VFD_STAT_RUN          0x0001u
#define VFD_STAT_REVERSE      0x0002u
#define VFD_STAT_JOG          0x0004u
#define VFD_STAT_FAULT        0x0008u

static double vfdHzToRaw(double hz) { return hz * 10.0; }          /* 0.1Hz */
static double vfdPctToRaw(double hz, double maxHz)
{
    /* 0x1000 是百分比：±10000 = ±100%。 */
    if (maxHz <= 0.0) maxHz = 50.0;
    return hz / maxHz * 10000.0;
}

/**
 * @brief M512 已实现的寄存器地址区间（含端点）。
 * @details [为什么需要区间表而不是"未建模即 EXC02"] 主站是**按段整块读**的
 *          （变频器的通信组一次读 9 个：0x1000~0x1008）。若模型对段内任何
 *          一个未显式建模的地址回 EXC02，整块读都失败——上层拿不到段内
 *          **本来正常**的那些点（实测：输出频率/母线电压/电流全无数据，
 *          `vm` 有效位只剩 5 个，页面显示"未就绪"）。
 *
 *          真实 M512 对这些地址都是应答的（只是值各有含义），故模型按
 *          "区间内一律应答、区间外才 EXC02"来建模。区间取自
 *          XVfdM512 的通信组表（见 HsfServerIoVfd 装配日志的 seg 列表）。
 */
typedef struct XVfdRange { uint16_t m_lo, m_hi; } XVfdRange;

static const XVfdRange s_vfdRanges[] = {
    { 0x1000, 0x1020 },   /* 监控量 + DI/DO + AI + PID 区。 */
    { 0x1F00, 0x1F01 },   /* 口令写入区。 */
    { 0x2000, 0x2004 },   /* 命令字 + 输出设定区。 */
    { 0x3000, 0x3000 },   /* 运行状态字。 */
    { 0x7022, 0x7022 },   /* 模块温度。 */
    { 0x8000, 0x8000 },   /* 故障码。 */
    { 0xF001, 0xF013 },   /* 命令/频率源 + 加/减速时间等参数。 */
    { 0xFA00, 0xFA04 },   /* PID 参数。 */
    { 0xFC00, 0xFC0F },   /* 参数组 PC。 */
};

static bool vfdAddrKnown(uint16_t addr)
{
    size_t i;
    for (i = 0; i < sizeof(s_vfdRanges) / sizeof(s_vfdRanges[0]); ++i) {
        if (addr >= s_vfdRanges[i].m_lo && addr <= s_vfdRanges[i].m_hi)
            return true;
    }
    return false;
}

static XModbusVirtualResult vfdRead(void* self, uint8_t fc, uint16_t addr,
                                    uint16_t count, uint16_t* out,
                                    uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelVfd* m = (XVirtualModelVfd*)self;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    if (fc != FC_READ_HOLDING) return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;

    /* 运行中输出频率向设定值爬升（每读一次升 10%，模拟真实加速过程）。 */
    if (m->m_running) {
        m->m_outHz += (m->m_setHz - m->m_outHz) * 0.1;
        if (m->m_setHz >= 0.0 ? (m->m_outHz > m->m_setHz - 0.05)
                              : (m->m_outHz < m->m_setHz + 0.05))
            m->m_outHz = m->m_setHz;
    } else {
        m->m_outHz += (0.0 - m->m_outHz) * 0.2;
        if (m->m_outHz < 0.05 && m->m_outHz > -0.05) m->m_outHz = 0.0;
    }
    ++m->m_tick;

    for (i = 0; i < count; ++i) {
        const uint16_t reg = (uint16_t)(addr + i);
        int32_t v = 0;
        /* [段内未知地址] 应答 0 而不是异常：主站整块读时，一个未建模地址
         * 回 EXC02 会让整段失败，把本来正常的点一起拖没（见 s_vfdRanges
         * 注记）。区间外才按真从站口径回 EXC02。 */
        if (!vfdAddrKnown(reg)) return XModbusVirtual_IllegalAddress;
        switch (reg) {
        case 0x1000:    /* 设定频率（百分比 ±10000 = ±100%） */
            v = (int32_t)vfdPctToRaw(m->m_setHz, m->m_maxHz);
            break;
        case 0x1001:    /* 输出频率（0.1Hz） */
            v = (int32_t)vfdHzToRaw(m->m_outHz);
            break;
        case 0x1002:    /* 母线电压（0.1V）≈ 540.0V */
            v = 5400;
            break;
        case 0x1003:    /* 输出电压（1V） */
            v = (int32_t)(m->m_running ? 380 : 0);
            break;
        case 0x1004:    /* 输出电流（0.01A） */
            v = m->m_running ? 1250 : 0;
            break;
        case 0x1005:    /* 输出功率（0.1kW） */
            v = m->m_running ? 55 : 0;
            break;
        case 0x1006:    /* 输出转矩（0.1%） */
            v = m->m_running ? 420 : 0;
            break;
        case 0x3000: {  /* 运行状态字 */
            uint16_t st = 0;
            if (m->m_running) st |= VFD_STAT_RUN;
            if (m->m_setHz < 0.0) st |= VFD_STAT_REVERSE;
            if (m->m_faultCode) st |= VFD_STAT_FAULT;
            v = st;
            break;
        }
        case 0x8000:    /* 故障码 */
            v = m->m_faultCode;
            break;
        default:
            /* 区间内且未显式建模：真实从站会应答（多为 0 或设备实况值）。
             * 回 0 让整段读成功，段内已建模的点不受牵连。 */
            v = 0;
            break;
        }
        /* 负值按 16 位补码（保持寄存器是无符号字）。 */
        out[i] = (uint16_t)((uint32_t)v & 0xFFFFu);
    }
    *outLen = count;
    return XModbusVirtual_Ok;
}

static XModbusVirtualResult vfdWrite(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, const uint16_t* values)
{
    XVirtualModelVfd* m = (XVirtualModelVfd*)self;

    if (!m || !values) return XModbusVirtual_IllegalValue;
    if (fc != FC_WRITE_REGISTER) return XModbusVirtual_IllegalFunction;
    if (count != 1) return XModbusVirtual_IllegalValue;

    switch (addr) {
    case 0x1000: {
        /* 设定频率：按百分比解释，反算 Hz。 */
        int16_t pct = (int16_t)values[0];
        double maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
        m->m_setHz = (double)pct / 10000.0 * maxHz;
        break;
    }
    case 0x2000: {
        const uint16_t cmd = values[0];
        if (cmd == VFD_CMD_STOP) {
            m->m_running = false;
        } else if (cmd == VFD_CMD_FORWARD || cmd == VFD_CMD_FWD_JOG) {
            m->m_running = true;
            if (m->m_setHz < 0.0) m->m_setHz = -m->m_setHz;
        } else if (cmd == VFD_CMD_REVERSE || cmd == VFD_CMD_REV_JOG) {
            m->m_running = true;
            if (m->m_setHz > 0.0) m->m_setHz = -m->m_setHz;
        } else if (cmd == VFD_CMD_FAULT_RESET) {
            m->m_faultCode = 0;
        } else {
            return XModbusVirtual_IllegalValue;
        }
        break;
    }
    default:
        return XModbusVirtual_IllegalAddress;
    }
    return XModbusVirtual_Ok;
}

static const XModbusVirtualModelVtable s_vfdVtable = {
    vfdRead, vfdWrite, NULL
};

struct XVirtualModelVfd* XVirtualModelVfd_create(double maxHz)
{
    XVirtualModelVfd* m = (XVirtualModelVfd*)XMemory_malloc(
        sizeof(XVirtualModelVfd), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    memset(m, 0, sizeof(*m));
    m->m_base.m_vtable = &s_vfdVtable;
    m->m_base.m_self = m;
    m->m_maxHz = (maxHz > 0.0) ? maxHz : 50.0;
    m->m_setHz = 0.0;
    m->m_outHz = 0.0;
    m->m_running = false;
    return m;
}

void XVirtualModelVfd_setFrequency(struct XVirtualModelVfd* m, double hz)
{
    if (m) m->m_setHz = hz;
}

double XVirtualModelVfd_outputFrequency(const struct XVirtualModelVfd* m)
{
    return m ? m->m_outHz : 0.0;
}

bool XVirtualModelVfd_running(const struct XVirtualModelVfd* m)
{
    return m ? m->m_running : false;
}

/* ============================================================================
 *                        数字量（MT3A-IO1632）
 * ============================================================================ */

/** @brief 模型可建模的最大阀路数（与现场 6 路对齐，留余量）。 */
#define XVIRTUAL_DIG_VALVE_CAP  8

/**
 * @brief 单路阀的行程状态。
 * @details [为什么需要行程] 见 XVirtualModelDigital 的注记。
 */
typedef struct XVirtualDigTravel {
    bool m_target;      /**< 目标位（true=开）。 */
    bool m_done;        /**< 已到位（DI 按 target 呈现）。 */
    int64_t m_startMs;  /**< 行程起点（框架毫秒；-1=无在途行程）。 */
} XVirtualDigTravel;

typedef struct XVirtualModelDigital {
    XModbusVirtualModel m_base;
    int m_channels;        /**< 16 或 32。 */
    uint32_t m_di;         /**< 非阀区 DI 位图（阀区由行程动态推导）。 */
    uint32_t m_do;
    bool m_interlock;      /**< 写 DO 时驱动行程（false=纯线圈模型）。 */
    /**
     * @brief 阀机械行程时间（毫秒）。
     * @details [为什么必须建模行程而不是立即联动] 立即联动下"命令"与
     *          "到位"是同一瞬间，上层阀状态机永远看不到「动作中」，
     *          20s 超时告警那条分支成为**死代码**——模拟联调会给人
     *          "超时逻辑已验证"的错觉，其实一行都没跑到。
     *          现场满开满关约 10s（见 HsfServerConfig 的
     *          HSF_VALVE_MOVE_TIMEOUT_MS 注记），模型缺省同值。
     *          取 UINT32_MAX 表示"阀卡死"（永远不到位）。
     */
    uint32_t m_moveMs;
    /** @brief 行程开关未接线（DI 恒 0，无论 DO 怎么变）。 */
    bool m_noFeedback;
    /** @brief 每路阀的行程状态（下标 = 模型内的 DO 通道号）。 */
    XVirtualDigTravel m_travel[XVIRTUAL_DIG_VALVE_CAP];
} XVirtualModelDigital;

/** @brief 当前毫秒（模型时钟；与框架计时源同轴，便于与上层时刻对账）。 */
static int64_t digNowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 推进某路行程并回答"是否已到位"。
 * @param m 模型；idx 阀下标（= 模型内 DO 通道号）。
 * @return 已到位返回 true（含无在途行程）。
 * @note 顺便把到位的行程落到 m_done，供 di_mask 直接读。
 */
static bool digTravelArrived(XVirtualModelDigital* m, int idx)
{
    XVirtualDigTravel* t;
    int64_t now;

    if (!m || idx < 0 || idx >= XVIRTUAL_DIG_VALVE_CAP) return true;
    t = &m->m_travel[idx];
    if (t->m_done) return true;
    /* [行程开关没接 / 阀卡死] 永远等不到反馈。这两者在上层会分别走
     * "无到位反馈（检查接线）"与"动作超时未到位"，是不同的现场结论。 */
    if (m->m_noFeedback) return false;
    if (m->m_moveMs == UINT32_MAX) return false;
    now = digNowMs();
    if (now - t->m_startMs >= (int64_t)m->m_moveMs) {
        t->m_done = true;
        return true;
    }
    return false;
}

/**
 * @brief 按行程状态推导 DI 位图。
 * @details 阀区以外用静态 m_di；阀区按"已到位 ⇒ 按 target 呈现；动作中
 *          ⇒ 两个到位都不吸"推导。现场接法（与 HsfServerConfig 一致）：
 *          DO n 对应 关到位 DI(2n)、开到位 DI(2n+1)。
 */
static uint32_t digDiMask(XVirtualModelDigital* m)
{
    uint32_t di;
    int i;

    if (!m) return 0;
    di = m->m_di;
    if (!m->m_interlock) return di;   /* 纯线圈模型：DI 与 DO 无关。 */
    for (i = 0; i < XVIRTUAL_DIG_VALVE_CAP; ++i) {
        const int closeDi = i * 2;
        const int openDi = i * 2 + 1;
        if (closeDi >= m->m_channels) break;
        di &= ~((1u << closeDi) | ((openDi < m->m_channels) ? (1u << openDi) : 0u));
        if (!digTravelArrived(m, i)) continue;   /* 动作中：两位都不吸。 */
        if (m->m_travel[i].m_target) {
            if (openDi < m->m_channels) di |= (1u << openDi);
        } else {
            di |= (1u << closeDi);
        }
    }
    return di;
}

static XModbusVirtualResult digRead(void* self, uint8_t fc, uint16_t addr,
                                    uint16_t count, uint16_t* out,
                                    uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelDigital* m = (XVirtualModelDigital*)self;
    uint32_t src;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    if (fc != FC_READ_COILS && fc != FC_READ_DISCRETE)
        return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;
    /* [实测硬约束] 单台模块单次最多 16 点；超了回 EXC03，addr 越界回 EXC02。
     * 模型照此口径，才能复现"读 32 点被拒 ⇒ 必须两台聚合"的真实行为。 */
    if (count < 1 || count > 16) return XModbusVirtual_IllegalValue;
    if ((uint32_t)addr + count > (uint32_t)m->m_channels)
        return XModbusVirtual_IllegalAddress;

    /* 线圈读 DO 位图；离散输入读**行程推导后的** DI 位图（会推进计时）。 */
    src = (fc == FC_READ_COILS) ? m->m_do : digDiMask(m);
    for (i = 0; i < count; ++i)
        out[i] = (uint16_t)((src >> (addr + i)) & 1u);
    *outLen = count;
    return XModbusVirtual_Ok;
}

static XModbusVirtualResult digWrite(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, const uint16_t* values)
{
    XVirtualModelDigital* m = (XVirtualModelDigital*)self;
    uint16_t i;

    if (!m || !values) return XModbusVirtual_IllegalValue;
    if (fc != FC_WRITE_COIL && fc != FC_WRITE_COILS)
        return XModbusVirtual_IllegalFunction;
    if (count < 1 || count > 16) return XModbusVirtual_IllegalValue;
    if ((uint32_t)addr + count > (uint32_t)m->m_channels)
        return XModbusVirtual_IllegalAddress;

    for (i = 0; i < count; ++i) {
        const uint16_t ch = (uint16_t)(addr + i);
        const bool on = values[i] != 0;
        if (on) m->m_do |= (1u << ch);
        else    m->m_do &= ~(1u << ch);

        /* [阀区变位 ⇒ 启动这一路的行程] DO 通道 ch 对应阀 ch：
         * 关到位 DI(2ch)、开到位 DI(2ch+1)。
         *
         * [只有"目标真变"才重新起算] 两个必须同时满足才重启行程：
         *   · 目标位变了（on != t->m_target）；或
         *   · 已到位但目标仍是同一侧（说明这一路刚从"卡死/没接开关"
         *     恢复，或者重复写同一目标希望它再动一次）。
         * 不看条件一律重启的写法有个现场可感的坑：上位机重复下发同一
         * 目标（去抖重发、按钮连点）会把行程打回原点，永远到不了位。 */
        if (m->m_interlock && ch < XVIRTUAL_DIG_VALVE_CAP) {
            XVirtualDigTravel* t = &m->m_travel[ch];
            if (t->m_target != on || t->m_done) {
                t->m_target = on;
                t->m_startMs = digNowMs();
                t->m_done = false;
            }
        }
    }
    return XModbusVirtual_Ok;
}

static const XModbusVirtualModelVtable s_digVtable = { digRead, digWrite, NULL };

struct XVirtualModelDigital* XVirtualModelDigital_create(int channels)
{
    XVirtualModelDigital* m;
    if (channels < 1) channels = 16;
    if (channels > 32) channels = 32;
    m = (XVirtualModelDigital*)XMemory_malloc(sizeof(XVirtualModelDigital),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    memset(m, 0, sizeof(*m));
    m->m_base.m_vtable = &s_digVtable;
    m->m_base.m_self = m;
    m->m_channels = channels;
    m->m_interlock = true;
    /* [缺省行程 = 现场真实节奏] 10s 满开满关（见 HsfServerConfig 的
     * HSF_VALVE_MOVE_TIMEOUT_MS 注记：现场实测 10s，超时判据 20s）。
     * 用真实时间尺度而不是"立即到位"，才能让「动作中」与「20s 超时」
     * 两条分支在模拟联调里真正跑到。 */
    m->m_moveMs = 10000u;
    /* 每路阀初始"已到位、目标=关"：开机即稳态，不要一启动就让六路阀
     * 显示"动作中"。 */
    {
        int i;
        for (i = 0; i < XVIRTUAL_DIG_VALVE_CAP; ++i) {
            m->m_travel[i].m_target = false;
            m->m_travel[i].m_done = true;
            m->m_travel[i].m_startMs = -1;
        }
    }
    /* [非阀区 DI 给点信号] 保留奇数/偶数位的差异，界面看着不像全 0。
     * 阀区的位由 digDiMask 动态推导，这里写进去会被覆盖——不影响。 */
    {
        int i;
        for (i = 0; i < channels; ++i) {
            if ((i % 2) == 0) m->m_di |= (1u << i);
        }
    }
    return m;
}

void XVirtualModelDigital_setDi(struct XVirtualModelDigital* m, uint32_t mask)
{
    if (m) m->m_di = mask;
}

uint32_t XVirtualModelDigital_doMask(const struct XVirtualModelDigital* m)
{
    return m ? m->m_do : 0u;
}

uint32_t XVirtualModelDigital_diMask(const struct XVirtualModelDigital* m)
{
    /* [必须走行程推导] 直接返回 m_di 会漏掉阀区的动态行程状态；
     * 本查询给的是"此刻真实的 DI 读数"口径。非 const 转换只因为
     * digDiMask 会推进行程计时（与真实模块"被读到就刷新"同构）。 */
    return m ? digDiMask((XVirtualModelDigital*)m) : 0u;
}

void XVirtualModelDigital_setValveInterlock(struct XVirtualModelDigital* m,
                                            bool enable)
{
    if (m) m->m_interlock = enable;
}

void XVirtualModelDigital_setMoveMs(struct XVirtualModelDigital* m,
                                    uint32_t moveMs)
{
    if (m && moveMs > 0) m->m_moveMs = moveMs;
}

void XVirtualModelDigital_setNoFeedback(struct XVirtualModelDigital* m,
                                        bool noFeedback)
{
    if (m) m->m_noFeedback = noFeedback;
}

/* ============================================================================
 *                        模拟量（中盛 8 路 4-20mA）
 * ============================================================================ */

typedef struct XVirtualModelAnalog {
    XModbusVirtualModel m_base;
    int m_channels;
    uint16_t m_mv[32];
} XVirtualModelAnalog;

static XModbusVirtualResult anaRead(void* self, uint8_t fc, uint16_t addr,
                                    uint16_t count, uint16_t* out,
                                    uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelAnalog* m = (XVirtualModelAnalog*)self;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    if (fc != FC_READ_INPUT) return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;
    if ((uint32_t)addr + count > (uint32_t)m->m_channels)
        return XModbusVirtual_IllegalAddress;
    for (i = 0; i < count; ++i) out[i] = m->m_mv[addr + i];
    *outLen = count;
    return XModbusVirtual_Ok;
}

static XModbusVirtualResult anaWrite(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, const uint16_t* values)
{
    (void)self; (void)fc; (void)addr; (void)count; (void)values;
    /* [只读模块] 该模块实测不支持写；照实现场的拒绝口径。 */
    return XModbusVirtual_IllegalFunction;
}

static const XModbusVirtualModelVtable s_anaVtable = { anaRead, anaWrite, NULL };

struct XVirtualModelAnalog* XVirtualModelAnalog_create(int channels)
{
    XVirtualModelAnalog* m;
    int i;
    if (channels < 1) channels = 8;
    if (channels > 32) channels = 32;
    m = (XVirtualModelAnalog*)XMemory_malloc(sizeof(XVirtualModelAnalog),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    memset(m, 0, sizeof(*m));
    m->m_base.m_vtable = &s_anaVtable;
    m->m_base.m_self = m;
    m->m_channels = channels;
    /* 缺省给 8mA（量程中点）。
     * 毫伏 = mA × 249Ω（采样电阻口径，见 HsfServerConfig.HSF_AI_SHUNT_OHM）。 */
    for (i = 0; i < channels; ++i) m->m_mv[i] = (uint16_t)(8.0 * 249.0);
    return m;
}

void XVirtualModelAnalog_setMillivolts(struct XVirtualModelAnalog* m, int ch,
                                       uint16_t mv)
{
    if (!m || ch < 0 || ch >= m->m_channels) return;
    m->m_mv[ch] = mv;
}

/* ============================================================================
 *                        温湿度（SHT30）
 * ============================================================================ */

typedef struct XVirtualModelTempHum {
    XModbusVirtualModel m_base;
    int m_tempTenths;
    int m_humTenths;
} XVirtualModelTempHum;

static XModbusVirtualResult thRead(void* self, uint8_t fc, uint16_t addr,
                                   uint16_t count, uint16_t* out,
                                   uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelTempHum* m = (XVirtualModelTempHum*)self;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    if (fc != FC_READ_INPUT) return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;
    /* [实测地址空间止于 0x0002] 见记忆 zhongsheng-temphum-rs485-probe：
     * 取数只用 FC04 0x0000 × 2（温度/湿度）。 */
    if ((uint32_t)addr + count > 2u) return XModbusVirtual_IllegalAddress;
    for (i = 0; i < count; ++i) {
        out[i] = (uint16_t)((addr + i == 0) ? (uint16_t)m->m_tempTenths
                                            : (uint16_t)m->m_humTenths);
    }
    *outLen = count;
    return XModbusVirtual_Ok;
}

static XModbusVirtualResult thWrite(void* self, uint8_t fc, uint16_t addr,
                                    uint16_t count, const uint16_t* values)
{
    (void)self; (void)fc; (void)addr; (void)count; (void)values;
    /* [实测不支持 FC10] 见记忆：非法请求静默丢弃；模型回 EXC01 让上层可见。 */
    return XModbusVirtual_IllegalFunction;
}

static const XModbusVirtualModelVtable s_thVtable = { thRead, thWrite, NULL };

struct XVirtualModelTempHum* XVirtualModelTempHum_create(void)
{
    XVirtualModelTempHum* m = (XVirtualModelTempHum*)XMemory_malloc(
        sizeof(XVirtualModelTempHum), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    m->m_base.m_vtable = &s_thVtable;
    m->m_base.m_self = m;
    m->m_tempTenths = 265;   /* 26.5℃ */
    m->m_humTenths = 550;    /* 55.0% */
    return m;
}

void XVirtualModelTempHum_set(struct XVirtualModelTempHum* m, int temp_tenths,
                              int hum_tenths)
{
    if (!m) return;
    m->m_tempTenths = temp_tenths;
    m->m_humTenths = hum_tenths;
}

/* ============================================================================
 *                        电磁流量计（LD / TRX-485）
 * ============================================================================ */

typedef struct XVirtualModelFlow {
    XModbusVirtualModel m_base;
    float m_instant;     /**< 瞬时流量 m3/h。 */
} XVirtualModelFlow;

/* LD 变量区：0x1010 起（高字在前）；单位码 0x1020/0x1021 等。 */
#define FLOW_VADDR_BASE      0x1010u
#define FLOW_VADDR_SPAN      22u
#define FLOW_ROLE_INSTANT_HI 0
#define FLOW_ROLE_INSTANT_LO 1
#define FLOW_ROLE_UNIT       16      /* 0x1020 瞬时流量单位 */
#define FLOW_ROLE_TOTAL_UNIT 17      /* 0x1021 累积总量单位 */
#define FLOW_ROLE_ALARM      18      /* 0x1022 起四字报警 */

static XModbusVirtualResult flowRead(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, uint16_t* out,
                                     uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelFlow* m = (XVirtualModelFlow*)self;
    uint16_t i;
    uint32_t bits;
    float f = 0.0f;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    if (fc != FC_READ_HOLDING && fc != FC_READ_INPUT)
        return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;
    if (addr < FLOW_VADDR_BASE ||
        (uint32_t)addr + count > (uint32_t)FLOW_VADDR_BASE + FLOW_VADDR_SPAN)
        return XModbusVirtual_IllegalAddress;

    memcpy(&bits, &m->m_instant, sizeof(bits));

    for (i = 0; i < count; ++i) {
        const uint16_t idx = (uint16_t)(addr + i - FLOW_VADDR_BASE);
        switch (idx) {
        case FLOW_ROLE_INSTANT_HI: out[i] = (uint16_t)(bits >> 16); break;
        case FLOW_ROLE_INSTANT_LO: out[i] = (uint16_t)(bits & 0xFFFFu); break;
        case FLOW_ROLE_UNIT:       out[i] = 5;  break;  /* M3/H */
        case FLOW_ROLE_TOTAL_UNIT: out[i] = 1;  break;  /* M3 */
        default:                   out[i] = 0;  break;  /* 其余量/报警 = 0。 */
        }
    }
    (void)f;
    *outLen = count;
    return XModbusVirtual_Ok;
}

static XModbusVirtualResult flowWrite(void* self, uint8_t fc, uint16_t addr,
                                      uint16_t count, const uint16_t* values)
{
    (void)self; (void)fc; (void)addr; (void)count; (void)values;
    return XModbusVirtual_IllegalFunction;
}

static const XModbusVirtualModelVtable s_flowVtable = { flowRead, flowWrite, NULL };

struct XVirtualModelFlow* XVirtualModelFlow_create(void)
{
    XVirtualModelFlow* m = (XVirtualModelFlow*)XMemory_malloc(
        sizeof(XVirtualModelFlow), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    m->m_base.m_vtable = &s_flowVtable;
    m->m_base.m_self = m;
    /* [缺省值必须选"低字非 0"的浮点] 12.5f 的 IEEE754 位模式是
     * 0x41480000——低字恰好为 0。而 32 位量由**高低两字**拼成，组层
     * feedRole 对"值未变"会静默：低字 0 与点初值相同 ⇒ 永远不喂 ⇒
     * 设备层整拍重算时低字缺失 ⇒ 瞬时流量恒 0（真机上曾因此以为模型没接）。
     * 12.53f = 0x41487AE1，两字都非 0，不会有这个巧合。
     * （同名坑见 xindustry_flowmeter_test.c 的"为什么不用 1.0f/12.5f"。） */
    m->m_instant = 12.53f;
    return m;
}

void XVirtualModelFlow_setInstantFlow(struct XVirtualModelFlow* m, float v)
{
    if (m) m->m_instant = v;
}

/* ==================== 在线浊度仪（WZT-701A） ==================== */

/**
 * @struct XVirtualModelTurbidity
 * @brief 浊度仪模型：FC03 读 0x0000 ×2，**CDAB 低字在前 + IEEE754 float32**。
 *
 * @details [口径来源是真机手册，不是"市面常见"猜测]
 *          WZT-701A 说明书 4.3.10.2「测试命令」给出可复现帧：
 *              发送：01 03 00 00 00 02 C4 0B
 *              回复：01 03 04 D7 0A 41 1F 92 1D
 *              原文：按照低字在前高字在后传输(CDAB)…对应的浮点型数据为 9.99NTU
 *          即 (0x411F << 16) | 0xD70A = 0x411FD70A，memcpy 成 float 恰为 9.99f。
 *
 *          [与电磁流量计的口径**不同**，不要照抄]
 *          XVirtualModelFlow 是**高字在前（ABCD）**。两者来自不同厂家协议表；
 *          任何"统一成一种字序"的改动都会让其中一台读数变成天文数字。
 *
 *          [功能码也不同于流量计/温湿度] 手册明确是 **FC03**（读保持寄存器），
 *          而流量计/温湿度是 FC04。写错会被从站拒绝，现场表现为"仪表在线但
 *          读数一直是 0"。
 */
typedef struct XVirtualModelTurbidity {
    XModbusVirtualModel m_base;
    int m_index;            /**< 台号 0/1（决定基准值与相位）。 */
    double m_baseNtu;       /**< 基准浊度（NTU）。 */
    double m_fixedNtu;      /**< 固定值；<0 表示未固定（按正弦波动）。 */
    int64_t m_t0Ms;         /**< 起算时刻（毫秒）。 */
} XVirtualModelTurbidity;

#define TURB_WOBBLE     0.03    /**< 波动幅度（相对值 ±3%）。 */
#define TURB_PERIOD_MS  45000.0 /**< 波动周期（毫秒；与 Python 侧 45s 同值）。 */
#define TURB_PI         3.14159265358979323846

/** @brief 取当前应在寄存器里的 NTU 值。 */
static double turbModelNtuNow(const XVirtualModelTurbidity* m)
{
    double phase;
    double t;

    if (!m) return 0.0;
    if (m->m_fixedNtu >= 0.0) return m->m_fixedNtu;
    t = (double)(XDateTime_currentMSecsSinceEpoch() - m->m_t0Ms);
    /* 两台相位错开 180°：一眼能看出是两个独立仪器，而不是同一个值贴两遍。 */
    phase = (t / TURB_PERIOD_MS) * 2.0 * TURB_PI;
    phase += TURB_PI * (double)m->m_index;
    return m->m_baseNtu * (1.0 + TURB_WOBBLE * sin(phase));
}

/** @brief 取当前 32 位位模式（CDAB 的字序只在**线上**体现，模型给出位模式）。 */
static uint32_t turbModelBits(const XVirtualModelTurbidity* m)
{
    float f = (float)turbModelNtuNow(m);
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static XModbusVirtualResult turbRead(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, uint16_t* out,
                                     uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelTurbidity* m = (XVirtualModelTurbidity*)self;
    uint32_t bits;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    /* 手册：只支持 FC03（读保持寄存器）。 */
    if (fc != FC_READ_HOLDING) return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;
    /* [越界不抛异常，回 0] 与真机同口径：对"有但不用"的寄存器回 0 而不是
     * 异常帧。抛异常会把"读成功但值为 0"变成"整拍失败 ⇒ 设备永远离线"。 */
    bits = turbModelBits(m);
    for (i = 0; i < count; ++i) {
        const uint16_t reg = (uint16_t)(addr + i);
        if (reg == 0u)
            out[i] = (uint16_t)(bits & 0xFFFFu);        /* CDAB：低字在前 */
        else if (reg == 1u)
            out[i] = (uint16_t)(bits >> 16);
        else
            out[i] = 0u;
    }
    *outLen = count;
    return XModbusVirtual_Ok;
}

static XModbusVirtualResult turbWrite(void* self, uint8_t fc, uint16_t addr,
                                      uint16_t count, const uint16_t* values)
{
    (void)self; (void)fc; (void)addr; (void)count; (void)values;
    /* 仪表对外只暴露测量值；标定/清洗都在面板上做（手册 4.3.3 / 4.3.8）。 */
    return XModbusVirtual_IllegalFunction;
}

static const XModbusVirtualModelVtable s_turbVtable = { turbRead, turbWrite, NULL };

struct XVirtualModelTurbidity* XVirtualModelTurbidity_create(int index)
{
    XVirtualModelTurbidity* m = (XVirtualModelTurbidity*)XMemory_malloc(
        sizeof(XVirtualModelTurbidity), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    m->m_base.m_vtable = &s_turbVtable;
    m->m_base.m_self = m;
    m->m_index = (index != 0) ? 1 : 0;
    /* 基准浊度：1 号（原水）高、2 号（出水）低——与 Python 侧 BASE_BY_INDEX 同值。 */
    m->m_baseNtu = (m->m_index == 0) ? 12.5 : 3.2;
    /* [缺省不固定，按正弦波动] 恒定值看不出"数据在刷新"；两台用不同相位，
     * 这样一眼能看出是两台独立的仪器。需要精确断言时用 setNtu 固定。
     * 基准值保持"低字非 0"：12.5f = 0x41480000 的低字恰为 0，但本模型每拍
     * 重算位模式（正弦波动使值持续变化），不存在流量计那个"低字永不被喂"
     * 的巧合。若调用方 setNtu 固定了一个低字为 0 的值，属测试自己选定的口径。 */
    m->m_fixedNtu = -1.0;
    m->m_t0Ms = XDateTime_currentMSecsSinceEpoch();
    return m;
}

void XVirtualModelTurbidity_setNtu(struct XVirtualModelTurbidity* m, double ntu)
{
    if (!m) return;
    m->m_fixedNtu = (ntu > 0.0) ? ntu : 0.0;
}

double XVirtualModelTurbidity_ntu(const struct XVirtualModelTurbidity* m)
{
    if (!m) return 0.0;
    return turbModelNtuNow(m);
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
