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
 *              · M512：0x1000 设定频率(±10000=±100%)、0x1001 输出频率(0.1Hz，
 *                实测定标 ÷10)、0x1002 母线电压、0x1004 输出电流、
 *                0x2000 命令字(1=正转…7=故障复位)、0x3000 运行状态字
 *                (枚举 1/2/3)、0x8000 故障码；
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

uint8_t XModbusVirtualModel_responseFunction(
    const struct XModbusVirtualModel* model, uint8_t requestFc)
{
    if (!model || !model->m_vtable || !model->m_vtable->responseFunction)
        return requestFc;
    return model->m_vtable->responseFunction(model->m_self, requestFc);
}

/* ============================================================================
 *                            变频器（M512 / ATV61）
 * ============================================================================ */

/**
 * @struct XVirtualModelVfd
 * @brief M512 变频器寄存器模型——**逐项对齐 Python virtualdev.VirtualVfd**。
 *
 * @details [为什么以 Python 为基准] Python 侧 devices.py 的单位与命令字口径
 *          是**实机逐点标定**的结果（15Hz 实测 raw=1500 ⇒ 0x1001 ÷100 才对；
 *          命令字 1=正转 … 7=故障复位）。模拟从站必须与真机同口径，否则
 *          "模拟联调通过、现场接上真机即翻车"——模拟器的价值是复刻真机
 *          行为，不是复刻一个自洽的假设。
 *
 *          [与旧实现的差别——每条都是实锅]
 *            · 命令字旧映射（1=停/2=正转/3=反转/4,5=点动/6=复位）是**错的**：
 *              与 XVfdM512.c 驱动（1=正转…7=复位，见 XVfdM512.h CmdRunForward=1）
 *              相反——模拟模式下客户端发"启动"写 1，旧模型却当成"停机"。
 *              旧注释"与 XVfdM512.c 同口径"与事实相反，勿再沿用。
 *            · 0x1001 刻度 = **×100（0.01Hz）**：2026-11-10 板端实机复测
 *              定案（此前本文件按 ×10，与 Python 侧 ÷100 相反，导致两端
 *              显示的输出频率/转速差 10 倍）：
 *              给定 9720（97.2% ⇒ 48.6Hz）→ raw=**4860**；给定 2000
 *              （20% ⇒ 10.0Hz）→ 稳态 raw=**1000**；两点都精确等于
 *              Hz×100 ⇒ ÷100。旧实测定标文档里「0x1001=100」是**漏了
 *              一个 0**（10.0Hz 的 raw 是 1000）。现在模拟从站与真机、
 *              与 Python 侧 devices.py 三方同口径。
 *            · 状态字旧为位掩码（RUN|REVERSE|…）；真机 0x3000 是**枚举**
 *              1=正转 2=反转 3=停机（读数量必须=1，XVfdM512.c DecodePoint
 *              按 1/2/3 解码——模型与驱动两侧同错时闭环测不出来，此为教训）。
 *            · 爬坡旧为"每次读向目标靠 10%"——轮询节拍一变加速时间跟着变，
 *              且一次扫描多个读组会推进多次；Python 是固定斜率 2.5Hz/拍
 *              （100ms 拍 ⇒ 25Hz/s，0→50Hz 约 2s）。
 *            · 母线电压 540V→553.2V（真机稳定值）；输出电压/电流/功率改为
 *              随输出频率缩放（0Hz 全 0，50Hz 满值 380V/3.2A/15kW）。
 *            · 停机语义：输出强制 0（自由停车口径，两种停机不区分取更安全
 *              的立刻归零）；停机时写 0x1000 只**记忆**给定、不产生输出——
 *              否则"停机 + 输出 30Hz / 转速 720rpm"自相矛盾。
 */
typedef struct XVirtualModelVfd {
    XModbusVirtualModel m_base;
    double m_maxHz;
    int32_t m_setPct;     /**< 0x1000 给定（±10000=±100%，int16 补码口径）。 */
    double m_outHz;       /**< 输出频率（Hz）。 */
    double m_targetHz;    /**< 爬坡目标（Hz；仅运行中跟随给定）。 */
    bool m_running;
    bool m_reverse;
    bool m_jogging;
    /**
     * @brief 减速停止进行中（输出按斜率降、降到 0 才置停机）。
     * @details [为什么要这个状态] 真机的**减速停止（F3）**是按减速时间斜坡
     *          降到 0，状态字在降到 0 那一刻才变"停机"；**自由停车（F4）**
     *          才是立刻断输出。旧实现"只要不在运行态就把输出置 0"，于是
     *          界面永远看不到"正在停机"这段过程（用户 2026-11-06 明确要
     *          看这个过程）。与 Python VirtualVfd 的 decelerating 同口径。
     */
    bool m_decelerating;
    /**
     * @brief 运行中换向待决：True=正在按 DEC 减速过零，过零后翻向加速。
     * @details [换向 = 减速过零再反向加速，对齐真机] 运行中收到反方向命令字
     *          （0x2000 写 1↔2）不是瞬间翻方向位——真机先按减速时间把输出
     *          降到 0（这段状态字仍报**原**方向），过零那一刻才翻方向、再按
     *          加速时间向新方向爬到给定。旧实现直接翻 m_reverse ⇒ 频率不经过
     *          0 的"假换向"。过零处理见 vfdTick；m_revPendingReverse 记过零后
     *          要翻去的目标方向。自由停车/减速停机/点动命令会取消待决。
     */
    bool m_revPending;
    bool m_revPendingReverse;   /**< 过零后要翻去的目标方向。 */
    uint16_t m_faultCode;
    /**
     * @brief P 组参数区存储（0xF000..0xF013，下标 = 地址-0xF000）。
     * @details [为什么必须建模参数区] 驱动控制前置检查读 P0.02/P0.03
     *          （命令源=2 / 主频率源=9，真机已配置——见使用指南 §1.6），
     *          且装配期会把两者**写成**通讯源；模型不建模这两个寄存器时
     *          读回 0 ⇒ 检查恒失败 ⇒ 全部控制被拒（"控制闸/能力不符"），
     *          模拟模式下变频器永远操作不动。缺省值即真机实况。
     */
    uint16_t m_params[0x14];
    uint64_t m_tick;
} XVirtualModelVfd;

/* P 组参数区（与 XVfdM512.h 的 Addr* 同址）。 */
#define VFD_PADDR_BASE       0xF000u
#define VFD_PADDR_SPAN       0x14u
#define VFD_PADDR_CMDSRC     0xF002u   /**< P0.02 命令源（2=通讯）。 */
#define VFD_PADDR_FRQSRC     0xF003u   /**< P0.03 主频率源X（9=通讯给定）。 */
#define VFD_PADDR_MAXFREQ    0xF00Au   /**< P0.10 最大频率（0.1Hz，500=50Hz）。 */
#define VFD_PADDR_ACC        0xF011u   /**< P0.17 加速时间（0.1s，100=10.0s，满量程 0↔P0.10）。 */
#define VFD_PADDR_DEC        0xF012u   /**< P0.18 减速时间（0.1s，同上）。 */

/* M512 命令字（与 XVfdM512.h 的 XVfdM512_Cmd* 同值：0x2000 写命令）。
 * [来源] 真机手册 + Python devices.py CMD_*：1=正转 2=反转 3=正点动
 * 4=反点动 5=自由停机 6=减速停机 7=故障复位。 */
#define VFD_CMD_RUN_FORWARD   1u
#define VFD_CMD_RUN_REVERSE   2u
#define VFD_CMD_JOG_FORWARD   3u
#define VFD_CMD_JOG_REVERSE   4u
#define VFD_CMD_FREE_STOP     5u
#define VFD_CMD_DECEL_STOP    6u
#define VFD_CMD_FAULT_RESET   7u

/* [爬坡斜率兜底] 常规斜率按 ACC/DEC 参数推导（vfdRampHzPerTick）；参数
 * 未配置（写 0）时回退本固定值：每拍 ±2.5Hz，推进节拍=100ms 心跳拍
 * ⇒ 25Hz/s，与 Python VirtualVfd.RAMP_HZ_PER_TICK 同值（推进入口见
 * vfdTick）。 */
#define VFD_RAMP_HZ_PER_TICK  2.5

/** @brief 给定百分比 → 目标频率（Hz）。
 *  @note  基准优先取 P0.10 参数（0.1Hz 定点，与驱动 m512MaxFreq 同源同口径），
 *         未配置时回退 create 传入的最大频率。 */
static double vfdSetHzOf(const XVirtualModelVfd* m)
{
    double maxHz;
    if (!m) return 0.0;
    if (m->m_params[VFD_PADDR_MAXFREQ - VFD_PADDR_BASE] > 0)
        maxHz = (double)m->m_params[VFD_PADDR_MAXFREQ - VFD_PADDR_BASE] / 10.0;
    else
        maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
    return (double)m->m_setPct / 10000.0 * maxHz;
}

/** @brief 按加减速时间参数推导每拍斜率（Hz/拍，拍=100ms）。
 *  @details [满量程时间口径，对齐真机] ACC(P0.17)/DEC(P0.18) = 输出从 0 走到
 *           P0.10 最大频率（或反向）的秒数（0.1s 定点）：斜率 = maxHz / 秒 /
 *           10（每秒 10 拍）。缺省 100=10.0s、50Hz ⇒ 0.5Hz/拍（5Hz/s）——
 *           12Hz 换向全程约 5s，"先慢慢停、再反向加速"肉眼可见（用户
 *           2026-11-06 选定 ACC/DEC 口径，替换旧的固定 25Hz/s）。
 *           [参数 ≤0] 未配置（显式写 0）时回退固定斜率兜底，不钳 0.1s——
 *           与 Python 侧同口径（Python 用 RAMP_HZ_PER_TICK 兜底）。 */
static double vfdRampHzPerTick(const XVirtualModelVfd* m, bool decel)
{
    double maxHz, sec;
    uint16_t param;
    if (!m) return VFD_RAMP_HZ_PER_TICK;
    if (m->m_params[VFD_PADDR_MAXFREQ - VFD_PADDR_BASE] > 0)
        maxHz = (double)m->m_params[VFD_PADDR_MAXFREQ - VFD_PADDR_BASE] / 10.0;
    else
        maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
    param = m->m_params[(decel ? VFD_PADDR_DEC : VFD_PADDR_ACC) - VFD_PADDR_BASE];
    sec = (double)param / 10.0;
    if (sec <= 0.0) return VFD_RAMP_HZ_PER_TICK;
    return maxHz / sec / 10.0;
}

/** @brief 写 0x1000 的公共路径（FC06 与 setFrequency 共用）。
 *  @note  [停机时写给定不能产生输出] 给定值是"下次启动用多少"，写它只是
 *         **记住**目标；真正的输出由命令字（0x2000）决定。旧实现无条件
 *         更新爬坡目标，于是停机状态改给定 ⇒ 输出从 0 爬到目标频率 ⇒
 *         界面显示"停机 + 输出 30Hz"甚至"转速 720rpm"。正确语义：目标
 *         只在**运行中**才跟随给定；停机时目标保持 0，给定只记在 m_setPct。 */
static void vfdWriteSetPct(XVirtualModelVfd* m, int32_t pct)
{
    if (pct > 10000) pct = 10000;      /* [钳位而不是拒绝] 与 Python 侧同口径： */
    if (pct < -10000) pct = -10000;    /* 现场偶尔给超范围值，钳位后按满速跑。 */
    m->m_setPct = pct;
    /* [换向减速中给定只记忆不动目标] 服务端处理带给定的 START 是"先写
     * 0x1000 再写 0x2000"两步；两步之间 100ms 心跳拍可能插入一次推进——
     * 若写给定就把目标从 0 拉回给定，会出现"正在减速却先弹高半拍"的毛刺。
     * 换向待决期间给定只记入 m_setPct，过零翻向时 vfdTick 用 vfdSetHzOf()
     * 取到的是**新**给定，反向加速直达用户改的值（物理上也说得通：换向
     * 途中改给定，过零后按新给定走）。 */
    if (m->m_running && !m->m_revPending) m->m_targetHz = vfdSetHzOf(m);
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
    /* [2026-11-10 板端实测补齐] 真机 M512 对 0xFC18 起也**正常应答**
     * （板端 ttyS3 探针：0xFC18x4 → 13 字节 OK），而模型区间只到
     * 0xFC0F ⇒ C 端通信组[14]（0xFC18×4）拿到 EXC02 ⇒ 异常被当成
     * 该从站失败 ⇒ 整站退避 ⇒ 变频器在模拟里也"离线"（真机同样受害：
     * 组[12] 0xFA00×5 真机无应答，Hub 又缺超时收口）。 */
    { 0xFC18, 0xFC1B },   /* 参数组 PD（真机应答，模型原先漏建）。 */
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

/**
 * @brief 推进一拍：输出频率向目标线性爬坡。
 * @details [斜率固定而不是"每次读靠 10%"] 旧实现把推进挂在读调用上：读几
 *          次就爬多快，轮询节拍一变加速时间跟着变，且一次扫描多个读组会
 *          推进多次。Python 是每拍固定 +2.5Hz（100ms 拍 ⇒ 25Hz/s）。
 *
 *          [停机分两种 —— 对齐真机与 Python VirtualVfd]
 *            · **减速停止（F3，命令字 6）**：输出按斜率降到 0，**降到 0
 *              才把运行标志清掉**（状态字这时才变"停机"）。真机按减速时间
 *              走坡道（现场要好几秒），这段过程就是界面上的"正在停机"，
 *              用户 2026-11-06 明确要看它。
 *            · **自由停车（F4，命令字 5）**：立刻断输出（真机自由停车就是
 *              切断输出，不是斜坡）。
 *          旧实现两者都"不在运行态就置 0"，于是减速停止也瞬断 ⇒ 界面永远
 *          看不到"正在停机"。
 * @note  推进入口只有心跳拍（XVirtualModelVfd_tick ← HsfServerIoSim_tick）；
 *        读路径只序列化当前值。
 */
static void vfdTick(XVirtualModelVfd* m)
{
    double delta;
    if (!m) return;
    if (!m->m_running) {
        /* 自由停车等"瞬断"路径：输出立刻归零。 */
        m->m_outHz = 0.0;
        m->m_targetHz = 0.0;
        m->m_decelerating = false;
        ++m->m_tick;
        return;
    }
    delta = m->m_targetHz - m->m_outHz;
    {
        /* 斜率按方向取：升速用 ACC、降速用 DEC（vfdRampHzPerTick）。 */
        const double slope = vfdRampHzPerTick(m, delta < 0.0);
        if (delta > slope) {
            m->m_outHz += slope;
            m->m_decelerating = false;
        } else if (delta < -slope) {
            m->m_outHz -= slope;
            m->m_decelerating = true;
        } else {
            m->m_outHz = m->m_targetHz;
            /* [不能在这里无脑清减速标记] 最后一拍 delta 会落进死区，若此时把
             * decelerating 清掉，下面"降到 0 就置停机/换向"的分支就永远进
             * 不去 ⇒ 减停到 0 后运行标志仍为 True（与 Python 侧同一个坑）。 */
            if (!(m->m_decelerating && m->m_outHz <= 0.0))
                m->m_decelerating = (delta < 0.0);
        }
    }
    /* 斜坡降到 0：有换向待决就翻向加速（保持运行），否则真正停机。 */
    if (m->m_decelerating && m->m_outHz <= 0.0) {
        if (m->m_revPending) {
            /* [换向过零] 这一刻才翻方向位——状态字（0x3000）在此之前一直
             * 报**原**方向（电机确实还在原方向转），与真机一致。目标取
             * vfdSetHzOf() 现算：减速期间用户改的给定在这里生效。 */
            m->m_reverse = m->m_revPendingReverse;
            m->m_revPending = false;
            m->m_decelerating = false;
            m->m_outHz = 0.0;
            m->m_targetHz = vfdSetHzOf(m);
        } else {
            m->m_outHz = 0.0;
            m->m_targetHz = 0.0;
            m->m_decelerating = false;
            m->m_running = false;
        }
    }
    ++m->m_tick;
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

    /* [推进不在读路径] 爬坡由统一心跳拍驱动（XVirtualModelVfd_tick ←
     * HsfServerIoSim_tick）：读只是序列化当前值。挂在读上会让加速时间
     * 随轮询节拍漂移（一次扫描 7 个读组推进 7 次）。 */

    for (i = 0; i < count; ++i) {
        const uint16_t reg = (uint16_t)(addr + i);
        int32_t v = 0;
        /* [段内未知地址] 应答 0 而不是异常：主站整块读时，一个未建模地址
         * 回 EXC02 会让整段失败，把本来正常的点一起拖没（见 s_vfdRanges
         * 注记）。区间外才按真从站口径回 EXC02。 */
        if (!vfdAddrKnown(reg)) return XModbusVirtual_IllegalAddress;
        switch (reg) {
        case 0x1000:    /* 给定频率（百分比 ±10000 = ±100%，int16 补码） */
            v = (int32_t)(m->m_setPct & 0xFFFFu);
            break;
        case 0x1001:    /* 输出频率（**0.01Hz**，2026-11-10 板端实机复测 ÷100） */
            v = (int32_t)(m->m_outHz * 100.0 +
                          (m->m_outHz >= 0.0 ? 0.5 : -0.5));
            break;
        case 0x1002:    /* 母线电压（0.1V）：真机稳定在 553.2V 左右 */
            v = 5532;
            break;
        case 0x1003: {  /* 输出电压（1V）：与频率成正比，0Hz→0V、50Hz→380V */
            double maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
            v = (int32_t)(m->m_outHz / maxHz * 380.0 +
                          (m->m_outHz >= 0.0 ? 0.5 : -0.5));
            break;
        }
        case 0x1004: {  /* 输出电流（0.01A）：转起来就有负载（满频 3.2A） */
            double maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
            double amps = (m->m_outHz > 0.1)
                              ? 3.2 * (m->m_outHz / maxHz) : 0.0;
            v = (int32_t)(amps * 100.0 + 0.5);
            break;
        }
        case 0x1005: {  /* 输出功率（0.1kW）：满频 15kW */
            double maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
            v = (int32_t)(m->m_outHz / maxHz * 15.0 * 10.0 +
                          (m->m_outHz >= 0.0 ? 0.5 : -0.5));
            break;
        }
        case 0x1006:    /* 输出转矩（0.1%）：空载轻载，42% 恒定（Python 同值） */
            v = 420;
            break;
        case 0x101F:    /* 主频率X（0.1Hz）＝U0.01 设定频率显示源：跟随给定。
                         * [为什么必须建模] 驱动把"设定频率"遥测绑在这里
                         * （XVfdM512_Modbus.c SetFreq → 0x101F），不建模则
                         * 界面"设定频率"恒 0（仿真器 xvfd_slave_emu 同款镜像）。 */
        case 0x1020:    /* 辅频率Y 同口径镜像。 */
            v = (int32_t)(vfdSetHzOf(m) * 10.0 +
                          (vfdSetHzOf(m) >= 0.0 ? 0.5 : -0.5));
            break;
        case 0x3000:    /* 运行状态字：**枚举** 1=正转 2=反转 3=停机。
                         * [不是位掩码] XVfdM512.c DecodePoint 按 1/2/3 解码；
                         * 点动不单独报状态（设备层口径"不单独报点动状态"）。 */
            v = (!m->m_running) ? 3 : (m->m_reverse ? 2 : 1);
            break;
        case 0x8000:    /* 故障码（0=无故障；运行状态由驱动层按故障派生） */
            v = m->m_faultCode;
            break;
        default:
            if (reg >= VFD_PADDR_BASE &&
                reg < VFD_PADDR_BASE + VFD_PADDR_SPAN) {
                /* P 组参数区：建模值直接回（未显式赋值的参数回 0——真实
                 * 从站对"有但未配置"的参数也正常应答，回异常会让整段读
                 * 失败，见 s_vfdRanges 注记）。 */
                v = m->m_params[reg - VFD_PADDR_BASE];
            } else {
                /* 区间内且未显式建模：真实从站会应答（多为 0 或设备实况值）。
                 * 回 0 让整段读成功，段内已建模的点不受牵连。 */
                v = 0;
            }
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
    case 0x1000:
        /* 给定频率：int16 补码百分比（无符号读出会把 -500 当成 65036）。 */
        vfdWriteSetPct(m, (int32_t)(int16_t)values[0]);
        break;
    case 0x2000: {
        /* 命令字：与 XVfdM512.h 的 XVfdM512_Cmd* 同值（1=正转…7=复位）。
         * [停机走命令字，不走给定] 5/6 停机把爬坡目标清 0，输出随 tick 归零。 */
        const uint16_t cmd = values[0];
        if (cmd == VFD_CMD_RUN_FORWARD || cmd == VFD_CMD_RUN_REVERSE) {
            const bool rev = (cmd == VFD_CMD_RUN_REVERSE);
            if (m->m_running && rev != m->m_reverse) {
                /* [运行中换向 = 先减速过零再反向加速] 对齐真机：不瞬间翻
                 * 方向位，先按 DEC 把输出降向 0（状态字仍报原方向），过零
                 * 那一刻 vfdTick 才翻向、按 ACC 向新方向爬到给定。减速途中
                 * 再收到同向命令（含翻回原方向）走下面分支＝取消换向。 */
                m->m_revPending = true;
                m->m_revPendingReverse = rev;
                m->m_targetHz = 0.0;
                m->m_decelerating = true;
            } else {
                m->m_revPending = false;
                m->m_running = true;
                m->m_reverse = rev;
                m->m_decelerating = false;
                m->m_targetHz = vfdSetHzOf(m);
            }
            m->m_jogging = false;
        } else if (cmd == VFD_CMD_JOG_FORWARD || cmd == VFD_CMD_JOG_REVERSE) {
            /* 点动接管：取消换向待决，方向/目标按点动立即生效（点动是
             * 按住即走的短促动作，沿用既有瞬时口径）。 */
            m->m_revPending = false;
            m->m_running = true;
            m->m_reverse = (cmd == VFD_CMD_JOG_REVERSE);
            m->m_jogging = true;
            m->m_decelerating = false;
            m->m_targetHz = vfdSetHzOf(m);
        } else if (cmd == VFD_CMD_FREE_STOP) {
            /* **自由停车：立刻断输出**（真机自由停车是切断输出，不是斜坡）。 */
            m->m_revPending = false;
            m->m_running = false;
            m->m_jogging = false;
            m->m_decelerating = false;
            m->m_targetHz = 0.0;
            m->m_outHz = 0.0;
        } else if (cmd == VFD_CMD_DECEL_STOP) {
            /* **减速停止：斜坡降到 0**（真机按减速时间走坡道）。这里把
             * 目标设 0 但**保持运行标志**，由 vfdTick 把输出斜坡降下来、
             * 降到 0 才置停机 —— 界面这段就是"正在停机"。
             * [为什么不立刻 running=false] 真机状态字在降到 0 那一刻才变
             * "停机"；立刻置 False 会让状态字先变停机、输出却还有几十 Hz，
             * 界面上就是"停机 + 输出 30Hz"的自相矛盾。
             * [停止覆盖换向] 用户点了停止就是真停：清换向待决，过零走停机。 */
            m->m_revPending = false;
            m->m_targetHz = 0.0;
            m->m_jogging = false;
            m->m_decelerating = true;
        } else if (cmd == VFD_CMD_FAULT_RESET) {
            m->m_faultCode = 0;
        } else {
            /* [未知命令字忽略而不是报异常] 与 Python 侧同口径：真机对
             * 未知命令字静默忽略；报 EXC03 会让上层把"命令不受支持"误判
             * 成"链路故障"。 */
            XPrintf("XVirtualModelVfd: 未知命令字 %u（忽略）\n",
                    (unsigned)cmd);
        }
        break;
    }
    default:
        if (addr >= VFD_PADDR_BASE &&
            addr < VFD_PADDR_BASE + VFD_PADDR_SPAN) {
            /* P 组参数区写（P0.02/P0.03 装配期写通讯源；P0.10 可改上限）。
             * 真机对有效参数写正常受理——回异常会让驱动的 ensure-sources
             * 失败，控制前置检查全拒。 */
            m->m_params[addr - VFD_PADDR_BASE] = values[0];
            break;
        }
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
    /* 缺省给定 50%（=25Hz）：与 Python VirtualVfd.set_percent=5000 同值。
     * 停机态下它只是"下次启动用多少"，不产生输出。 */
    m->m_setPct = 5000;
    m->m_outHz = 0.0;
    m->m_targetHz = 0.0;
    m->m_running = false;
    /* [P 组参数区缺省 = 真机实况] P0.02=2（命令源=通讯）、P0.03=9（主频率
     * 源=通讯给定）——真机前置条件（使用指南 §1.6，仿真器同款种子值）；
     * P0.10=500（最大频率 50.0Hz，0.1Hz 实测定标）；
     * P0.17/P0.18=100（加/减速时间各 10.0s，真机 10s 减速口径——爬坡斜率
     * 由 vfdRampHzPerTick 按它们推导，与 Python VirtualVfd 同值）。 */
    m->m_params[VFD_PADDR_CMDSRC - VFD_PADDR_BASE] = 2;
    m->m_params[VFD_PADDR_FRQSRC - VFD_PADDR_BASE] = 9;
    m->m_params[VFD_PADDR_MAXFREQ - VFD_PADDR_BASE] = 500;
    m->m_params[VFD_PADDR_ACC - VFD_PADDR_BASE] = 100;
    m->m_params[VFD_PADDR_DEC - VFD_PADDR_BASE] = 100;
    return m;
}

void XVirtualModelVfd_setFrequency(struct XVirtualModelVfd* m, double hz)
{
    double maxHz;
    if (!m) return;
    maxHz = (m->m_maxHz > 0.0) ? m->m_maxHz : 50.0;
    /* 与写 0x1000 完全同路径（百分比 + 运行中才跟随目标）。 */
    vfdWriteSetPct(m, (int32_t)(hz / maxHz * 10000.0 +
                                (hz >= 0.0 ? 0.5 : -0.5)));
}

void XVirtualModelVfd_tick(struct XVirtualModelVfd* m)
{
    vfdTick(m);
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
    int m_valveChannels;   /**< 本模块内的阀路数（DO 通道 0..m_valveChannels-1）。 */
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
    int valves;

    if (!m) return 0;
    di = m->m_di;
    if (!m->m_interlock) return di;   /* 纯线圈模型：DI 与 DO 无关。 */
    /* [阀区边界 = 本模块的阀路数] 阀区之外（如按钮组 DI12..15、进水泵
     * 的 DO6）**不是阀**：写它不该拉掉任何到位位。早先按"容量上限 8"
     * 推导，会把 DI12..15 也当成阀的到位位——阀一动作就把按钮组的
     * 输入点拉成 0，急停回路在界面上凭空变成"断开"。 */
    valves = m->m_valveChannels;
    if (valves > XVIRTUAL_DIG_VALVE_CAP) valves = XVIRTUAL_DIG_VALVE_CAP;
    for (i = 0; i < valves; ++i) {
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
         * [只有"目标真变"才重新起算] 与 Python VirtualDigital.write_single
         * 同口径：`target != on` 才重启行程。已到位后**同值重写不再重启**
         * ——旧实现多一个"done 也重启"的发明语义，重复写同一目标（服务端
         * 在途重发、外部改动回写）会让已到位的阀凭空再走一段 10s"动作中"，
         * 与真机不符（真机线圈已是该状态，再写一次阀不动）。 */
        if (m->m_interlock && m->m_valveChannels > 0 &&
            ch < (uint16_t)m->m_valveChannels && ch < XVIRTUAL_DIG_VALVE_CAP) {
            XVirtualDigTravel* t = &m->m_travel[ch];
            if (t->m_target != on) {
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
    /* [缺省阀路数 = 6] 与 Python VirtualDigital.VALVE_COUNT 同值：现场
     * 六路电磁阀，DO 通道 0..5 → 关到位 DI(2n) / 开到位 DI(2n+1)。
     * 阀区**只有**这 6 路——通道 6 往后是进水泵、按钮灯、按钮输入，
     * 写它们不产生行程、也不动任何到位位。 */
    m->m_valveChannels = 6;
    if (m->m_valveChannels > XVIRTUAL_DIG_VALVE_CAP)
        m->m_valveChannels = XVIRTUAL_DIG_VALVE_CAP;
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
    /* [非阀区 DI 缺省 0] 与 Python VirtualDigital 同口径：BASE_DI 经
     * `& ~0x0FFF0000` 掩掉阀区后实为 0——界面上阀区外的 DI 就是全 0
     * （数据有效，只是没信号）。旧实现给"偶数位吸合"是发明观感，两端
     * 页面对不上。阀区的位由 digDiMask 动态推导（初始六路全部"已关"
     * ⇒ 关到位位吸合），与 Python 的开机稳态一致。 */
    return m;
}

void XVirtualModelDigital_setDi(struct XVirtualModelDigital* m, uint32_t mask)
{
    if (m) m->m_di = mask;
}

void XVirtualModelDigital_setDo(struct XVirtualModelDigital* m, uint32_t mask)
{
    if (m) m->m_do = mask;
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

void XVirtualModelDigital_setValveChannels(struct XVirtualModelDigital* m,
                                           int count)
{
    if (!m) return;
    if (count < 0) count = 0;
    if (count > XVIRTUAL_DIG_VALVE_CAP) count = XVIRTUAL_DIG_VALVE_CAP;
    if (count > m->m_channels) count = m->m_channels;
    m->m_valveChannels = count;
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
    /* [缺省值对齐 Python VirtualAnalog] 通道 0 模拟压差计（EJA110A，0~1MPa）：
     * 2985mV ≈ 11.988mA ≈ 0.499MPa；其余通道 4mA 活零点（995mV ≈ 3.996mA，
     * 落在 0.05mA 容差内）。旧实现 8 路全 8mA 是"量程中点"的发明观感，
     * 两端页面对不上（Python 端压差显示 0.5MPa、其余显示 0）。
     * 毫伏 = mA × 249Ω（采样电阻口径，见 HsfServerConfig.HSF_AI_SHUNT_OHM）。 */
    for (i = 0; i < channels; ++i)
        m->m_mv[i] = (i == 0) ? 2985u : 995u;
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

/**
 * @struct XVirtualModelFlow
 * @brief LD 电磁流量计模型——**逐项对齐 Python virtualdev.VirtualFlowMeter**。
 *
 * @details [为什么要有波动与累积量] 恒定值看不出"数据在刷新"，容易误判成
 *          界面卡住；累积量缺失则历史曲线/总量页无数据。Python 口径：
 *          瞬时流量 36.5 m³/h ±2% 正弦（周期 60s），正/反向累积量按**真实
 *          经过的时间**积分（dt 不按"被访问次数"算，否则轮询节拍一变累积
 *          速度跟着变）。
 *
 *          [变量区 22 寄存器地图（高字在前 ABCD）]
 *            0x1010/11 Float 瞬时流量     0x1012/13 Float 瞬时流速
 *            0x1014/15 Float 流量百分比   0x1016/17 Float 流体电导比
 *            0x1018/19 Long  正向累积整数 0x101A/1B Float 正向累积小数
 *            0x101C/1D Long  反向累积整数 0x101E/1F Float 反向累积小数
 *            0x1020 U16 瞬时流量单位      0x1021 U16 累积总量单位
 *            0x1022~0x1025 U16 四种报警
 *          [单位码两张表各用各的] 瞬时流量表 5=m³/h；累积总量表 1=m³。
 *          Python 注释里的实锅：把累积单位填成 10（瞬时表的邻居码），
 *          界面显示"?"。
 */
typedef struct XVirtualModelFlow {
    XModbusVirtualModel m_base;
    double m_fixedFlow;      /**< 固定瞬时流量（m³/h）；<0=未固定（按正弦波动）。 */
    /**
     * @brief 波动基准流量（m³/h）。
     * @details [缺省与现场] 缺省 36.5（DN50 量程中点的老口径，FLOW_BASE_M3H）；
     *          模拟装配处用 setBaseFlow 换成 2026-11-09 ttyS4 实测的 3.6
     *          （灌水后口径）。±2% 正弦、60s 周期绕本基准波动。
     */
    double m_baseFlow;
    double m_forwardTotal;   /**< 正向累积量（m³）。 */
    double m_reverseTotal;   /**< 反向累积量（m³）。 */
    int64_t m_lastMs;        /**< 上次积分时刻（单调毫秒）。 */
    int64_t m_t0Ms;          /**< 波动相位起算时刻。 */
    uint16_t m_flowUnit;     /**< 0x1020 瞬时流量单位（5=m³/h）。 */
    uint16_t m_totalUnit;    /**< 0x1021 累积总量单位（1=m³）。 */
    uint16_t m_alarm[4];     /**< 0x1022~0x1025 四种报警（0=正常）。 */
} XVirtualModelFlow;

/* LD 变量区：0x1010 起（高字在前）；单位码 0x1020/0x1021 等。 */
#define FLOW_VADDR_BASE      0x1010u
#define FLOW_VADDR_SPAN      22u
#define FLOW_ROLE_INSTANT_HI 0
#define FLOW_ROLE_INSTANT_LO 1
#define FLOW_ROLE_SPEED_HI   2
#define FLOW_ROLE_SPEED_LO   3
#define FLOW_ROLE_PCT_HI     4
#define FLOW_ROLE_PCT_LO     5
#define FLOW_ROLE_COND_HI    6
#define FLOW_ROLE_COND_LO    7
#define FLOW_ROLE_FWD_INT_HI 8
#define FLOW_ROLE_FWD_INT_LO 9
#define FLOW_ROLE_FWD_FRAC_HI 10
#define FLOW_ROLE_FWD_FRAC_LO 11
#define FLOW_ROLE_REV_INT_HI 12
#define FLOW_ROLE_REV_INT_LO 13
#define FLOW_ROLE_REV_FRAC_HI 14
#define FLOW_ROLE_REV_FRAC_LO 15
#define FLOW_ROLE_UNIT       16      /* 0x1020 瞬时流量单位 */
#define FLOW_ROLE_TOTAL_UNIT 17      /* 0x1021 累积总量单位 */
#define FLOW_ROLE_ALARM      18      /* 0x1022 起四字报警 */

/* [DN50 量程中点] 瞬时流量基准（m³/h），与 Python BASE_FLOW 同值。 */
#define FLOW_BASE_M3H        36.5
/* [波动幅度/周期] ±2%、60s——与 Python 同值（sim/README 口径表）。 */
#define FLOW_WOBBLE          0.02
#define FLOW_PERIOD_MS       60000.0
/* [管道内径] 由流量反算流速用（DN50 → 0.05m），与 Python 同值。 */
#define FLOW_PIPE_DIAMETER   0.05
#define FLOW_PI              3.14159265358979323846

/** @brief 当前瞬时流量（固定值优先，否则绕基准 ±2% 正弦波动，周期 60s）。 */
static double flowNow(const XVirtualModelFlow* m)
{
    double phase;
    if (!m) return 0.0;
    if (m->m_fixedFlow >= 0.0) return m->m_fixedFlow;
    phase = (double)((XDateTime_currentMSecsSinceEpoch() - m->m_t0Ms) %
                     (int64_t)FLOW_PERIOD_MS) / FLOW_PERIOD_MS * 2.0 * FLOW_PI;
    return m->m_baseFlow * (1.0 + FLOW_WOBBLE * sin(phase));
}

/**
 * @brief 推进累积量并返回当前瞬时流量。
 * @details dt 用真实经过的时间（XDateTime 单调毫秒），不按"被访问次数"算
 *          ——与 Python _step 同口径。多次调用只是把同一段 dt 切得更细，
 *          积分结果不变，故读路径与心跳 tick 同时驱动也安全。
 */
static double flowStep(XVirtualModelFlow* m)
{
    int64_t now;
    int64_t dt;
    double flow;
    if (!m) return 0.0;
    now = XDateTime_currentMSecsSinceEpoch();
    dt = now - m->m_lastMs;
    if (dt < 0) dt = 0;
    m->m_lastMs = now;
    flow = flowNow(m);
    m->m_forwardTotal += flow / 3600.0 * (double)dt / 1000.0;
    return flow;
}

/** @brief float32 位模式 → (高字, 低字)，高字在前（ABCD）。 */
static void flowSplitFloat(double value, uint16_t* hi, uint16_t* lo)
{
    float f = (float)value;
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    *hi = (uint16_t)(bits >> 16);
    *lo = (uint16_t)(bits & 0xFFFFu);
}

/** @brief uint32 → (高字, 低字)。 */
static void flowSplitLong(uint32_t value, uint16_t* hi, uint16_t* lo)
{
    *hi = (uint16_t)(value >> 16);
    *lo = (uint16_t)(value & 0xFFFFu);
}

static XModbusVirtualResult flowRead(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, uint16_t* out,
                                     uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelFlow* m = (XVirtualModelFlow*)self;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    /* [功能码] 真机 FC03/FC04 都能读变量区（Python 驱动走 FC03、本工程
     * 驱动走 FC04），模型两者都收——驱动层差异不是模拟要复刻的对象。 */
    if (fc != FC_READ_HOLDING && fc != FC_READ_INPUT)
        return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;

    /* 变量区之外（参数区）返回 0 而不是异常：与 Python 同口径——真机对
     * "有但不用"的寄存器正常应答，抛异常会把"读成功但值为 0"变成
     * "整拍失败"。 */
    if (addr < FLOW_VADDR_BASE ||
        (uint32_t)addr >= (uint32_t)FLOW_VADDR_BASE + FLOW_VADDR_SPAN) {
        for (i = 0; i < count; ++i) out[i] = 0;
        *outLen = count;
        return XModbusVirtual_Ok;
    }

    {
        const double flow = flowStep(m);
        const double speed =
            (flow / 3600.0) / (FLOW_PI * (FLOW_PIPE_DIAMETER / 2.0) *
                               (FLOW_PIPE_DIAMETER / 2.0));
        const double pct = 100.0 * flow / (m->m_baseFlow * 2.0);
        const double fwdIntD = (m->m_forwardTotal < 0.0)
                                   ? 0.0 : m->m_forwardTotal;
        const double revIntD = (m->m_reverseTotal < 0.0)
                                   ? 0.0 : m->m_reverseTotal;
        const uint32_t fwdInt = (uint32_t)fwdIntD;
        const uint32_t revInt = (uint32_t)revIntD;
        uint16_t hi, lo;

        for (i = 0; i < count; ++i) {
            const uint16_t idx =
                (uint16_t)((uint32_t)addr + i - FLOW_VADDR_BASE);
            out[i] = 0;
            switch (idx) {
            case FLOW_ROLE_INSTANT_HI:
            case FLOW_ROLE_INSTANT_LO:
                flowSplitFloat(flow, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_INSTANT_HI) ? hi : lo;
                break;
            case FLOW_ROLE_SPEED_HI:
            case FLOW_ROLE_SPEED_LO:
                flowSplitFloat(speed, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_SPEED_HI) ? hi : lo;
                break;
            case FLOW_ROLE_PCT_HI:
            case FLOW_ROLE_PCT_LO:
                flowSplitFloat(pct, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_PCT_HI) ? hi : lo;
                break;
            case FLOW_ROLE_COND_HI:
            case FLOW_ROLE_COND_LO:
                flowSplitFloat(1.0, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_COND_HI) ? hi : lo;
                break;
            case FLOW_ROLE_FWD_INT_HI:
            case FLOW_ROLE_FWD_INT_LO:
                flowSplitLong(fwdInt, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_FWD_INT_HI) ? hi : lo;
                break;
            case FLOW_ROLE_FWD_FRAC_HI:
            case FLOW_ROLE_FWD_FRAC_LO:
                flowSplitFloat(fwdIntD - (double)fwdInt, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_FWD_FRAC_HI) ? hi : lo;
                break;
            case FLOW_ROLE_REV_INT_HI:
            case FLOW_ROLE_REV_INT_LO:
                flowSplitLong(revInt, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_REV_INT_HI) ? hi : lo;
                break;
            case FLOW_ROLE_REV_FRAC_HI:
            case FLOW_ROLE_REV_FRAC_LO:
                flowSplitFloat(revIntD - (double)revInt, &hi, &lo);
                out[i] = (idx == FLOW_ROLE_REV_FRAC_HI) ? hi : lo;
                break;
            case FLOW_ROLE_UNIT:       out[i] = m->m_flowUnit;  break;
            case FLOW_ROLE_TOTAL_UNIT: out[i] = m->m_totalUnit; break;
            default:
                if (idx >= FLOW_ROLE_ALARM &&
                    idx < FLOW_ROLE_ALARM + 4u)
                    out[i] = m->m_alarm[idx - FLOW_ROLE_ALARM];
                break;
            }
        }
    }
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
    memset(m, 0, sizeof(*m));
    m->m_base.m_vtable = &s_flowVtable;
    m->m_base.m_self = m;
    m->m_forwardTotal = 0.0;
    m->m_reverseTotal = 0.0;
    m->m_lastMs = XDateTime_currentMSecsSinceEpoch();
    m->m_t0Ms = m->m_lastMs;
    /* [单位码两张表各用各的] 瞬时 5=m³/h、累积 1=m³（Python 同值）。 */
    m->m_flowUnit = 5;
    m->m_totalUnit = 1;
    m->m_fixedFlow = -1.0;   /* 缺省不固定，按正弦波动。 */
    /* [波动基准缺省] DN50 量程中点 36.5（老口径）；现场口径由装配处
     * setBaseFlow 切到实测值。 */
    m->m_baseFlow = FLOW_BASE_M3H;
    /* [低字非 0 不是靠选缺省值了] 波动使瞬时流量每拍都变，32 位量的两字
     * 每帧都会被组层喂到——旧实现靠 12.53f（0x41487AE1 两字非 0）躲
     * "低字恰为 0 不被喂"的坑，波动口径下该坑不存在（仅启动瞬间相位≈0
     * 时低字可能为 0，下一拍即变）。 */
    return m;
}

void XVirtualModelFlow_tick(struct XVirtualModelFlow* m)
{
    if (m) flowStep(m);
}

void XVirtualModelFlow_setInstantFlow(struct XVirtualModelFlow* m, float v)
{
    /* [固定瞬时流量] 测试/演示用（对齐浊度 setNtu 的做法）：设 ≥0 的值
     * 后不再波动；需要恢复波动时传负数。 */
    if (m) m->m_fixedFlow = (double)v;
}

void XVirtualModelFlow_setBaseFlow(struct XVirtualModelFlow* m, double base)
{
    if (!m || !(base > 0.0)) return;
    /* [绕基准波动] 与 setInstantFlow（钉死）互补：设基准并把固定值摘掉，
     * 恢复 ±2% 正弦微波动（2026-11-09 ttyS4 实测口径：3.6 m³/h ±2%）。 */
    m->m_baseFlow = base;
    m->m_fixedFlow = -1.0;
}

/* ==================== 在线浊度仪（站 1 WZT-701A / 站 2 ZHEYI-5100ZDY） ==================== */

/**
 * @struct XVirtualModelTurbidity
 * @brief 浊度仪模型：FC03 读 0x0000 起，**逐台字序 + IEEE754 float32**。
 *
 * @details [口径来源是实测，不是"市面常见"猜测]
 *          站 1（WZT-701A，CDAB）说明书 4.3.10.2「测试命令」给出可复现帧：
 *              发送：01 03 00 00 00 02 C4 0B
 *              回复：01 03 04 D7 0A 41 1F 92 1D
 *              原文：按照低字在前高字在后传输(CDAB)…对应的浮点型数据为 9.99NTU
 *          即 (0x411F << 16) | 0xD70A = 0x411FD70A，memcpy 成 float 恰为 9.99f。
 *          站 2（ZHEYI-5100ZDY，DCBA = 整字节反序 = 小端 float32）2026-11-09
 *          板端实测帧：`4F DD 37 3D` 按 <f 解 = 0.0449NTU（按旧 CDAB 口径解
 *          出 1.1e-5 假数且不报错）；温度字 `00 00 99 41` 按 DCBA = 19.125℃，
 *          与浊度同一帧（0x0000×4）同刻读出。四口径对照见
 *          docs/HSF板端ttyS4三设备取数实测总表_20261109.md §5.3/§7。
 *
 *          [两家字序不同，模型必须逐台可配]
 *          XVirtualModelFlow 是**高字在前（ABCD）**，站 1 是 CDAB、站 2 是
 *          DCBA——任何"统一成一种字序"的改动都会让其中一台读数变成假数。
 *          字序由 setWordOrder 配（create 按台号给实测缺省）。
 *
 *          [功能码也不同于流量计/温湿度] 手册明确是 **FC03**（读保持寄存器），
 *          而流量计/温湿度是 FC04。写错会被从站拒绝，现场表现为"仪表在线但
 *          读数一直是 0"。
 */
typedef struct XVirtualModelTurbidity {
    XModbusVirtualModel m_base;
    int m_index;            /**< 台号 0/1（决定基准值与相位）。 */
    double m_baseNtu;       /**< 基准浊度（NTU）。 */
    double m_fixedNtu;      /**< 固定值；<0 表示未固定（按波动口径）。 */
    /**
     * @brief 浊度微波动幅度（NTU，绝对值）；<0 = 未配过（旧口径：相对 ±3%）。
     * @details [2026-11-09 实测分台] 站 1 WZT-701A 20 轮完全恒定（幅 0）、
     *          站 2 ZHEYI-5100ZDY 0.045±0.001 缓慢波动（幅 0.001，45s 周期）。
     *          由 setWobble 配置；未配置时保持历史行为（相对 ±3% 正弦）。
     */
    double m_wobbleAmp;
    int m_wordOrder;        /**< 线上字序（XVirtualModelTurbWordOrder）。 */
    double m_tempC;         /**< 水温（℃）；<0 = 本机不带温度（0x0002/3 恒 0）。 */
    /** @brief 温度漂移幅度（℃，绕 m_tempC 正弦；0 = 恒温；周期 60s）。 */
    double m_tempAmp;
    int64_t m_t0Ms;         /**< 起算时刻（毫秒）。 */
} XVirtualModelTurbidity;

#define TURB_WOBBLE     0.03    /**< 旧口径波动幅度（相对值 ±3%；未配 setWobble 时用）。 */
#define TURB_PERIOD_MS  45000.0 /**< 浊度波动周期（毫秒；与 Python 侧 45s 同值）。 */
#define TURB_TEMP_PERIOD_MS 60000.0 /**< 温度漂移周期（毫秒；与流量计同档慢变量）。 */
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
    if (m->m_wobbleAmp < 0.0)
        return m->m_baseNtu * (1.0 + TURB_WOBBLE * sin(phase));
    /* [绝对幅口径] 站 1 幅 0 = 恒定（实测 20 轮不动）、站 2 幅 0.001 =
     * 0.045±0.001 缓慢波动——分台幅值由 setWobble 配。 */
    return m->m_baseNtu + m->m_wobbleAmp * sin(phase);
}

/** @brief 取当前温度值（未开温度返回 <0；含 0 幅恒温与漂移两态）。 */
static double turbModelTempNow(const XVirtualModelTurbidity* m)
{
    double phase;
    double t;

    if (!m) return -1.0;
    if (m->m_tempC < 0.0) return -1.0;
    if (m->m_tempAmp <= 0.0) return m->m_tempC;
    t = (double)(XDateTime_currentMSecsSinceEpoch() - m->m_t0Ms);
    phase = (t / TURB_TEMP_PERIOD_MS) * 2.0 * TURB_PI;
    /* [站 2 实测] 温度 19.25~19.3125℃（40s 窗）——按 19.3±0.05 缓慢漂移。 */
    return m->m_tempC + m->m_tempAmp * sin(phase);
}

/** @brief 取当前浊度 32 位位模式（字序只在**寄存器拆字**时体现）。 */
static uint32_t turbModelBits(const XVirtualModelTurbidity* m)
{
    float f = (float)turbModelNtuNow(m);
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/** @brief 取当前温度的 32 位位模式（未开温度返回 0）。 */
static uint32_t turbModelTempBits(const XVirtualModelTurbidity* m)
{
    float f;
    uint32_t bits = 0;
    const double t = turbModelTempNow(m);
    if (!m || t < 0.0) return 0u;
    f = (float)t;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/**
 * @brief 按模型字序把 32 位位模式拆成线上寄存器字（reg0/reg1）。
 * @details CDAB：reg0=低字、reg1=高字（WZT-701A：`8F 5C 40 12` →
 *          reg0=0x8F5C、reg1=0x4012，设备层 CDAB 拼回 0x40128F5C=2.29f）。
 *          DCBA：线上 4 字节 = float32 的小端字节序 ⇒ reg0 = 位模式低 16 位
 *          的整字节反序、reg1 = 高 16 位的整字节反序（ZHEYI：位模式
 *          0x3D37DD4F → reg0=0x4FDD、reg1=0x373D，设备层 DCBA 拼回原位模式
 *          = 0.0449f）。两台分别与各自设备层字序旋钮配套 ⇒ 读回保真。
 */
static void turbModelSplitRegs(uint32_t bits, int wordOrder,
                               uint16_t* reg0, uint16_t* reg1)
{
    if (wordOrder == (int)XVirtualModelTurbWordOrder_Dcba) {
        *reg0 = (uint16_t)(((bits & 0x000000FFu) << 8) |
                           ((bits & 0x0000FF00u) >> 8));
        *reg1 = (uint16_t)(((bits & 0x00FF0000u) >> 8) |
                           ((bits & 0xFF000000u) >> 24));
        return;
    }
    *reg0 = (uint16_t)(bits & 0xFFFFu);        /* CDAB：低字在前 */
    *reg1 = (uint16_t)(bits >> 16);
}

static XModbusVirtualResult turbRead(void* self, uint8_t fc, uint16_t addr,
                                     uint16_t count, uint16_t* out,
                                     uint16_t outCap, uint16_t* outLen)
{
    XVirtualModelTurbidity* m = (XVirtualModelTurbidity*)self;
    uint16_t r0, r1, t0, t1;
    uint16_t i;

    if (!m || !out || !outLen) return XModbusVirtual_IllegalValue;
    /* [功能码分台] 手册：站 1 WZT-701A 只支持 FC03（FC04 无应答）；
     * 站 2 ZHEYI-5100ZDY **FC04 也受理**（2026-11-09 实测文档 §5.5），
     * 但应答功能码恒回 FC03——由 turbResponseFunction 带到线上帧，
     * 模型层只需按同一份寄存器地图回值。 */
    if (fc != FC_READ_HOLDING &&
        !(fc == FC_READ_INPUT && m->m_index == 1))
        return XModbusVirtual_IllegalFunction;
    if (outCap < count) return XModbusVirtual_IllegalValue;
    /* [越界不抛异常，回 0] 与真机同口径：对"有但不用"的寄存器回 0 而不是
     * 异常帧。抛异常会把"读成功但值为 0"变成"整拍失败 ⇒ 设备永远离线"。 */
    turbModelSplitRegs(turbModelBits(m), m->m_wordOrder, &r0, &r1);
    turbModelSplitRegs(turbModelTempBits(m), m->m_wordOrder, &t0, &t1);
    for (i = 0; i < count; ++i) {
        const uint16_t reg = (uint16_t)(addr + i);
        if (reg == 0u)          out[i] = r0;
        else if (reg == 1u)     out[i] = r1;
        else if (reg == 2u)     out[i] = t0;   /* 温度低字（未开温度=0）。 */
        else if (reg == 3u)     out[i] = t1;   /* 温度高字。 */
        else                    out[i] = 0u;
    }
    *outLen = count;
    return XModbusVirtual_Ok;
}

/**
 * @brief 浊度仪的应答功能码覆盖（站 2 怪癖：FC04 请求也回 FC03）。
 * @details [实测依据] docs/HSF板端ttyS4三设备取数实测总表_20261109.md
 *          §5.5/附录 A：`02 04 00 00 00 02 …` → `02 03 04 …`——应答功能码
 *          **恒为 03**，即使请求的是 FC04。按「应答功能码 == 请求功能码」
 *          校验的主站会误判；模拟从站必须把这条怪癖如实带上。
 *          站 1 WZT-701A 没有这个怪癖（FC04 直接被拒，见 turbRead）。
 */
static uint8_t turbResponseFunction(void* self, uint8_t requestFc)
{
    const XVirtualModelTurbidity* m = (const XVirtualModelTurbidity*)self;
    if (!m) return requestFc;
    if (m->m_index == 1 && requestFc == FC_READ_INPUT)
        return FC_READ_HOLDING;
    return requestFc;
}

static XModbusVirtualResult turbWrite(void* self, uint8_t fc, uint16_t addr,
                                      uint16_t count, const uint16_t* values)
{
    (void)self; (void)fc; (void)addr; (void)count; (void)values;
    /* 仪表对外只暴露测量值；标定/清洗都在面板上做（手册 4.3.3 / 4.3.8）。 */
    return XModbusVirtual_IllegalFunction;
}

static const XModbusVirtualModelVtable s_turbVtable = {
    turbRead, turbWrite, NULL, turbResponseFunction
};

struct XVirtualModelTurbidity* XVirtualModelTurbidity_create(int index)
{
    XVirtualModelTurbidity* m = (XVirtualModelTurbidity*)XMemory_malloc(
        sizeof(XVirtualModelTurbidity), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!m) return NULL;
    m->m_base.m_vtable = &s_turbVtable;
    m->m_base.m_self = m;
    m->m_index = (index != 0) ? 1 : 0;
    /* [2026-11-09 现场口径布值] 台 0 = 站 1 WZT-701A（原水）：CDAB、基准
     * 2.29NTU（实测 20 轮恒定值）、无温度；台 1 = 站 2 ZHEYI-5100ZDY（出水）：
     * DCBA、基准 0.045NTU（实测均值 0.04507）、温度 19.3℃（实测均值 19.27）。
     * 字序/温度可被 setWordOrder / setTempC 覆盖；基准值可用 setNtu 固定。 */
    m->m_baseNtu = (m->m_index == 0) ? 2.29 : 0.045;
    m->m_wordOrder = (m->m_index == 0)
                         ? (int)XVirtualModelTurbWordOrder_Cdab
                         : (int)XVirtualModelTurbWordOrder_Dcba;
    m->m_tempC = (m->m_index == 0) ? -1.0 : 19.3;
    /* [缺省不固定，按正弦波动] 恒定值看不出"数据在刷新"；两台用不同相位，
     * 这样一眼能看出是两台独立的仪器。需要精确断言时用 setNtu 固定。
     * [幅值未配 = 旧口径] m_wobbleAmp<0 时走相对 ±3%（历史行为一字不改）；
     * 模拟装配处按实测分台给绝对幅（站 1 恒定 / 站 2 ±0.001）。 */
    m->m_fixedNtu = -1.0;
    m->m_wobbleAmp = -1.0;
    m->m_tempAmp = 0.0;
    m->m_t0Ms = XDateTime_currentMSecsSinceEpoch();
    return m;
}

void XVirtualModelTurbidity_setNtu(struct XVirtualModelTurbidity* m, double ntu)
{
    if (!m) return;
    m->m_fixedNtu = (ntu > 0.0) ? ntu : 0.0;
}

void XVirtualModelTurbidity_setWobble(struct XVirtualModelTurbidity* m,
                                      double ampNtu)
{
    if (!m) return;
    if (!(ampNtu >= 0.0)) return;       /* 负幅非法：忽略，不回退旧口径。 */
    /* [幅值接管波动口径] 设过绝对幅后由它说了算（0=恒定、>0=±幅正弦）；
     * 同时摘掉固定值——两者并存时 fixedNtu 恒赢，wobble 配了也不生效，
     * 与"最后设的旋钮生效"直觉相反。setNtu 仍可随时重新钉死。 */
    m->m_wobbleAmp = ampNtu;
    m->m_fixedNtu = -1.0;
}

void XVirtualModelTurbidity_setTempWobble(struct XVirtualModelTurbidity* m,
                                          double ampC)
{
    if (!m) return;
    if (!(ampC >= 0.0)) return;
    m->m_tempAmp = ampC;
}

double XVirtualModelTurbidity_ntu(const struct XVirtualModelTurbidity* m)
{
    if (!m) return 0.0;
    return turbModelNtuNow(m);
}

void XVirtualModelTurbidity_setWordOrder(struct XVirtualModelTurbidity* m, int order)
{
    if (!m) return;
    if (order != (int)XVirtualModelTurbWordOrder_Cdab &&
        order != (int)XVirtualModelTurbWordOrder_Dcba)
        return;                 /* 非法值忽略：模型不该能被配成假口径。 */
    m->m_wordOrder = order;
}

void XVirtualModelTurbidity_setTempC(struct XVirtualModelTurbidity* m, double tempC)
{
    if (!m) return;
    m->m_tempC = tempC;
}

double XVirtualModelTurbidity_tempC(const struct XVirtualModelTurbidity* m)
{
    if (!m || m->m_tempC < 0.0) return 0.0;
    return m->m_tempC;
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
