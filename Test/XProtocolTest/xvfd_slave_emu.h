/**
 * @file       xvfd_slave_emu.h
 * @brief      变频器从站模拟器（进程内 TCP 版，仅 Test 域使用）。
 * @details    [2026-11-01 通道化重构] 原端到端测试用 Python 从站 + pty 虚拟串口
 *             （本沙箱无 /dev/ptmx 无法运行），改为**同进程 TCP 环回**从站：
 *               XModbusTcpServer（自动端口，寄存器映射）+ 本模拟器按品牌语义
 *               在测试泵循环里 tick（读命令字/给定 → 推进状态机 → 回写状态/
 *               遥测），客户端走 XModbusTcpClient → Hub → 点 → 通道组 → 设备。
 *             语义复刻自 mbtest_atv61_slave.py / mbtest_m512_slave.py：
 *               - ATV61：CiA402 状态机（0x0006/0x0007/0x000F 推进、bit7 复位）、
 *                 LFT 故障注入、RFR 跟随 LFR1；
 *               - M512：2000H 命令字（1/2/3/4/5/6/7）、3000H 状态、8000H 故障、
 *                 1001H 遥测跟随 1000H 设定值（P0.10 百分比换算）。
 *             双站隔离：每站一对（server+client），Hub 按映射段分流。
 *
 *             [2026-11-02 手册约束落地] M512 从站按《511M/512M/513M 说明书》
 *             附录 1 实现**三条硬约束**（从站侧强制，不是主站自律）：
 *               ① 只支持 FC03（读）与 FC06（写）：收到 FC04/FC05/FC0F/FC10
 *                  一律回异常帧（读类 83H / 写类 86H）+ 错误码 01（命令码错误）；
 *               ② 单次读寄存器数 n ≤ 12：超了回 83H + 错误码 03（数据错误）；
 *               ③ 一帧读不得跨过功能码组边界：跨了回 83H + 错误码 02（地址错误）。
 *             约束判定是**纯逻辑**（xvfdM512CheckRequest）：不依赖传输层，
 *             单测可直接调用断言拒绝行为。
 *             遥测/命令语义同步补齐：0x1001~0x1020（快捷遥测各子区）、
 *             0x2000~0x2004（命令/DO 强制/AO1/AO2/PULSE 强制）、0x3000 状态、
 *             0x8000 故障、0x7022 电机温度，单位按手册最小单位。
 */
#ifndef XVFD_SLAVE_EMU_H
#define XVFD_SLAVE_EMU_H

#include "CXinYueConfig.h"
#include "XModbus_config.h"

#if XMODBUS_ON && XMODBUS_CLIENT_ON

#include <string.h>
#include "XClass.h"
#include "XMemory.h"
#include "XModbusTcpServer.h"
#include "XModbusTcpClient.h"
#include "XModbusServer.h"
#include "XModbusDataUnit.h"
#include "XModbusDevice.h"
#include "XModbusPdu.h"
#include "XModbusReply.h"
#include "XByteArray.h"
#include "XTcpServer.h"
#include "XVariant.h"
#include "XVfd/XVfdM512.h"

/** @brief 品牌。 */
#define XVFD_EMU_ATV61 0
#define XVFD_EMU_M512 1

#if XVFD_ON && XVFD_M512_ON
/** @brief M512 单次读寄存器数上限（手册附录 1：n ≤ 12）。 */
#define XVFD_M512_MAX_READ_REGISTERS 12
/** @brief M512 支持的读功能码（FC03 读保持寄存器）。 */
#define XVFD_M512_FC_READ 0x03
/** @brief M512 支持的写功能码（FC06 写单个寄存器）。 */
#define XVFD_M512_FC_WRITE 0x06
/** @brief M512 异常帧功能码：读类（手册附录 1：83H）。 */
#define XVFD_M512_EXC_FC_READ 0x83
/** @brief M512 异常帧功能码：写类（手册附录 1：86H）。 */
#define XVFD_M512_EXC_FC_WRITE 0x86
/** @brief 异常码 01：命令码错误（非法功能码）。 */
#define XVFD_M512_EXC_ILLEGAL_FUNCTION 0x01
/** @brief 异常码 02：地址错误（非法数据地址 / 跨功能码组）。 */
#define XVFD_M512_EXC_ILLEGAL_ADDRESS 0x02
/** @brief 异常码 03：数据错误（非法数据值 / 读数量超限）。 */
#define XVFD_M512_EXC_ILLEGAL_VALUE 0x03
#endif /* XVFD_ON && XVFD_M512_ON */

/**
 * @brief 从站模拟器。
 */
typedef struct XvfdSlaveEmu {
    XModbusTcpServer* m_server;     /**< TCP 从站（映射宿主） */
    XModbusDataUnitMap* m_map;      /**< 暂存的映射表（setMap_move 后归 server） */
    int m_brand;                    /**< XVFD_EMU_ATV61 / XVFD_EMU_M512 */
    /* ---- ATV61 ---- */
    int m_state;                    /**< CiA402 状态（2/3/4/5） */
    int m_fault;                    /**< LFT 故障码 */
    int m_reverse;                  /**< 反转（CMD bit11） */
    int m_refHz10;                  /**< 频率给定（0.1Hz 定点） */
    int m_refRpm;                   /**< 转速给定 */
    int m_driveThermal;             /**< 变频器热状态 % */
    int m_motorThermal;             /**< 电机热状态 % */
    /* ---- M512 ---- */
    int m_m512Fault;                /**< 8000H 故障值 */
    int m_m512State;                /**< 3000H 状态（1/2/3，缺省 3） */
    int m_setpoint;                 /**< 1000H 通讯设定值（±10000 = ±100%） */
    int m_maxFreq;                  /**< P0.10 原始码（0.1Hz 定点，缺省 500 = 50.0Hz；实机标定） */
    uint16_t m_lastCmd;             /**< 命令字边沿检测缓存 */
    uint16_t m_doForce;             /**< 2001H DO 强制位图（回写 1008H） */
    uint16_t m_aoForce1;            /**< 2002H AO1 强制（0~0x7FFF） */
    uint16_t m_aoForce2;            /**< 2003H AO2 强制（0~0x7FFF） */
    uint16_t m_pulseForce;          /**< 2004H PULSE 输出强制（0~0x7FFF） */
    /* ---- M512 从站约束观测 ---- */
    int m_m512ReadCount;            /**< 通过约束的读请求数 */
    int m_m512WriteCount;           /**< 通过约束的写请求数 */
    int m_m512RejectCount;          /**< 被约束拒绝的请求数 */
    uint8_t m_m512LastExc;          /**< 最近一次拒绝的异常码（01/02/03） */
    uint8_t m_m512LastExcFc;        /**< 最近一次拒绝的异常帧功能码（83H/86H） */
} XvfdSlaveEmu;

#if XVFD_ON && XVFD_M512_ON

/**
 * @brief 取某功能码的「异常帧功能码」：读类 83H / 写类 86H（手册附录 1）。
 * @param fc 请求功能码。
 * @return 0x83（读类）或 0x86（写类）。
 */
static uint8_t xvfdM512ExcFcOf(uint8_t fc)
{
    switch (fc) {
    case XModbusPdu_WriteSingleCoil:        /* FC05 */
    case XModbusPdu_WriteSingleRegister:     /* FC06 */
    case XModbusPdu_WriteMultipleCoils:      /* FC15 */
    case XModbusPdu_WriteMultipleRegisters:  /* FC16 */
        return XVFD_M512_EXC_FC_WRITE;
    default:
        return XVFD_M512_EXC_FC_READ;
    }
}

/**
 * @brief 找通信组（含指定远端地址的组）。
 * @param groups 组表。
 * @param n 组数。
 * @param remote 远端地址。
 * @return 组下标；未命中返回 -1。
 */
static int xvfdM512FindGroup(const XVfdCommGroup* groups, int n, uint16_t remote)
{
    int i;
    if (!groups || n <= 0) return -1;
    for (i = 0; i < n; ++i) {
        const XVfdCommGroup* g = &groups[i];
        if (!g) continue;
        if (remote >= g->m_remoteStart &&
            (uint32_t)remote < (uint32_t)g->m_remoteStart + g->m_count) return i;
    }
    return -1;
}

/**
 * @brief M512 从站请求约束判定（纯逻辑；不依赖传输层）。
 * @param groups 通信组表（手册条目级远端区间）。
 * @param groupCount 组数。
 * @param fc 请求功能码。
 * @param addr 起始远端地址。
 * @param count 读取寄存器数（FC06 传 1）。
 * @param outExc 输出：异常码（0=通过；1=命令码错误；2=地址错误；3=数据错误）。
 * @param outExcFc 输出：异常帧功能码（0x83 读类 / 0x86 写类）。
 * @return 通过返回 true；被拒返回 false（并填 outExc / outExcFc）。
 * @details 手册附录 1 三条硬约束：
 *          ① 只支持 FC03 与 FC06：其余功能码（含 FC04/FC05/FC0F/FC10）
 *             一律命令码错误（01）；
 *          ② 单次读寄存器数 n ≤ 12：超了数据错误（03）；
 *          ③ 一帧读不得跨过功能码组边界：起点不在任何组内、或读段越过所在组
 *             末址，一律地址错误（02）。
 *          写（FC06）不做地址校验——手册的「只改 RAM」地址变体（P 组
 *          FxxxH→0xxxH、A 组 AxxxH→4xxxH）本就不在通信组表内。
 */
static bool xvfdM512CheckRequest(const XVfdCommGroup* groups, int groupCount,
                                 uint8_t fc, uint16_t addr, uint16_t count,
                                 uint8_t* outExc, uint8_t* outExcFc)
{
    uint8_t exc = 0;
    uint8_t excFc = XVFD_M512_EXC_FC_READ;
    if (outExc) *outExc = 0;
    if (outExcFc) *outExcFc = XVFD_M512_EXC_FC_READ;
    /* ① 功能码白名单：只 FC03（读）/ FC06（写）。 */
    if (fc != XVFD_M512_FC_READ && fc != XVFD_M512_FC_WRITE) {
        exc = XVFD_M512_EXC_ILLEGAL_FUNCTION;
        excFc = xvfdM512ExcFcOf(fc);
        if (outExc) *outExc = exc;
        if (outExcFc) *outExcFc = excFc;
        return false;
    }
    if (fc == XVFD_M512_FC_READ) {
        int gi;
        /* ② 单次读寄存器数上限。 */
        if (count == 0 || count > XVFD_M512_MAX_READ_REGISTERS) {
            if (outExc) *outExc = XVFD_M512_EXC_ILLEGAL_VALUE;
            if (outExcFc) *outExcFc = XVFD_M512_EXC_FC_READ;
            return false;
        }
        /* ③ 功能码组边界。 */
        gi = xvfdM512FindGroup(groups, groupCount, addr);
        if (gi < 0) {
            if (outExc) *outExc = XVFD_M512_EXC_ILLEGAL_ADDRESS;
            if (outExcFc) *outExcFc = XVFD_M512_EXC_FC_READ;
            return false;
        }
        if ((uint32_t)addr + count >
            (uint32_t)groups[gi].m_remoteStart + groups[gi].m_count) {
            if (outExc) *outExc = XVFD_M512_EXC_ILLEGAL_ADDRESS;
            if (outExcFc) *outExcFc = XVFD_M512_EXC_FC_READ;
            return false;
        }
    }
    return true;
}

/**
 * @brief M512 从站请求约束判定（用品牌通信组表；便捷封装）。
 * @param fc 请求功能码。
 * @param addr 起始远端地址。
 * @param count 读取寄存器数（FC06 传 1）。
 * @param outExc 输出：异常码。
 * @param outExcFc 输出：异常帧功能码。
 * @return 通过返回 true；被拒返回 false。
 */
static bool xvfdM512CheckRequestBrand(uint8_t fc, uint16_t addr, uint16_t count,
                                      uint8_t* outExc, uint8_t* outExcFc)
{
    /* [2026-11-02 分层收口] 组表从绑定层取（品牌主文件只存语义）。 */
    const XVfdBindTable* b = XVfdM512_modbusBinding();
    return xvfdM512CheckRequest(b->m_groups, b->m_groupCount, fc, addr, count,
                                outExc, outExcFc);
}

#endif /* XVFD_ON && XVFD_M512_ON */

/**
 * @brief 在服务器映射上登记一段保持寄存器。
 * @param server TCP 从站。
 * @param start 起始地址。
 * @param count 点数。
 * @return 成功返回 true。
 */
static bool xvfdEmuAddRange(XvfdSlaveEmu* em, uint16_t start, uint16_t count)
{
    static XModbusRegisterType key = XModbusHoldingRegisters;
    XModbusDataUnit* unit;
    if (!em) return false;
    if (!em->m_map) {
        em->m_map = XModbusDataUnitMap_create();
        if (!em->m_map) return false;
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, key, start, count);
    if (!unit) return false;
    XMap_insert_base((XMapBase*)em->m_map, &key, unit);
    XClassDelete((XClass*)unit);
    return true;
}

/**
 * @brief 写服务器映射一个保持寄存器。
 * @param em 模拟器。
 * @param addr 地址。
 * @param v 值。
 * @return 无。
 */
static void xvfdEmuPut(XvfdSlaveEmu* em, uint16_t addr, uint16_t v)
{
    XModbusServer_setData2((XModbusServer*)em->m_server, XModbusHoldingRegisters,
                           addr, v);
}

/**
 * @brief 读服务器映射一个保持寄存器。
 * @param em 模拟器。
 * @param addr 地址。
 * @return 寄存器值（未映射返回 0）。
 */
static uint16_t xvfdEmuGet(XvfdSlaveEmu* em, uint16_t addr)
{
    uint16_t v = 0;
    if (!XModbusServer_data2((XModbusServer*)em->m_server, XModbusHoldingRegisters,
                             addr, &v)) return 0;
    return v;
}

#if XVFD_ON && XVFD_M512_ON

/**
 * @brief M512 受约束的从站（XModbusTcpServer 子类；重写请求处理）。
 * @details 在通用 Modbus 服务端之前插入手册三条硬约束的判定：不合规的请求
 *          直接回异常帧（不落到通用处理），合规的请求交回通用处理。
 */
XCLASS_DEFINE_BEGING(XvfdM512Server)
XCLASS_DEFINE_EXTEND_END(XvfdM512Server, XModbusTcpServer)

typedef struct XvfdM512Server {
    XModbusTcpServer m_base;   /**< 基类（继承自 XModbusTcpServer）。 */
    XvfdSlaveEmu* m_emu;       /**< 归属模拟器（借用；写拒绝计数）。 */
} XvfdM512Server;

/**
 * @brief M512 从站的请求处理（约束判定 + 通用处理）。
 * @param server 服务端（实为 XvfdM512Server）。
 * @param request 请求。
 * @return 正常应答；不合规时返回异常帧（83H/86H + 错误码）。
 */
static XModbusResponse* VXvfdM512Server_processRequest(XModbusServer* server,
                                                        const XModbusRequest* request)
{
    XvfdM512Server* s = (XvfdM512Server*)server;
    XModbusPdu_FunctionCode fc;
    uint16_t addr = 0;
    uint16_t count = 0;
    uint8_t exc = 0;
    uint8_t excFc = XVFD_M512_EXC_FC_READ;
    if (!s || !request) return NULL;
    fc = XModbusPdu_functionCode((const XModbusPdu*)request);
    {
        const uint8_t* d = XContainerDataAddr(((const XModbusPdu*)request)->m_data);
        size_t n = XByteArray_size_base(((const XModbusPdu*)request)->m_data);
        if (n >= 2) addr = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
        if (n >= 4) count = (uint16_t)(((uint16_t)d[2] << 8) | d[3]);
    }
    if (fc == XModbusPdu_WriteSingleRegister) count = 1;
    if (!xvfdM512CheckRequestBrand((uint8_t)fc, addr, count, &exc, &excFc)) {
        if (s->m_emu) {
            ++s->m_emu->m_m512RejectCount;
            s->m_emu->m_m512LastExc = exc;
            s->m_emu->m_m512LastExcFc = excFc;
        }
        return XModbusExceptionResponse_create_with_function_and_exception(
            (XModbusPdu_FunctionCode)excFc, (XModbusPdu_ExceptionCode)exc);
    }
    if (s->m_emu) {
        if (fc == XModbusPdu_ReadHoldingRegisters) ++s->m_emu->m_m512ReadCount;
        else if (fc == XModbusPdu_WriteSingleRegister) ++s->m_emu->m_m512WriteCount;
    }
    /* 合规请求 → 走通用 Modbus 服务端处理。 */
    return XClass_Parent(XModbusServer, EXModbusServer_ProcessRequest,
                         XModbusResponse* (*)(XModbusServer*,
                                              const XModbusRequest*))(server, request);
}

/**
 * @brief M512 受约束从站的类初始化。
 * @return 虚表（单例）。
 */
static XVtable* XvfdM512Server_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XvfdM512Server)
        XVTABLE_INHERIT_XCLASS(XModbusTcpServer);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusServer_ProcessRequest,
                             VXvfdM512Server_processRequest);
    XCLASS_SHOW_SIZE_DEFAULT(XvfdM512Server);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 建一个 M512 受约束从站（堆对象；释放走 XObject_deleteLater）。
 * @param em 归属模拟器（借用；写拒绝计数）。
 * @return 新从站；失败返回 NULL。
 */
static XvfdM512Server* xvfdM512Server_create(XvfdSlaveEmu* em)
{
    XvfdM512Server* s =
        (XvfdM512Server*)XMalloc_System(sizeof(XvfdM512Server));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    XModbusTcpServer_init(&s->m_base);
    XClassGetVtable(s) = XvfdM512Server_class_init();
    Set_Class_Memory(s, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(s, true);
    s->m_emu = em;
    return s;
}

#endif /* XVFD_ON && XVFD_M512_ON */

/**
 * @brief 创建模拟器（TCP 从站上线监听）。
 * @param em 模拟器。
 * @param brand 品牌（XVFD_EMU_*）。
 * @param outPort 输出监听端口；可为 NULL。
 * @return 成功返回 true。
 */
static bool xvfdSlaveEmuCreate(XvfdSlaveEmu* em, int brand, uint16_t* outPort)
{
    XVariant* a;
    XVariant* p;
    memset(em, 0, sizeof(*em));
    em->m_brand = brand;
    em->m_state = 2;            /* ATV61：Switch on disabled */
    em->m_m512State = 3;        /* M512：停机 */
    em->m_driveThermal = 25;
    em->m_motorThermal = 30;
    em->m_maxFreq = 500;        /* M512：P0.10 = 50.0Hz（0.1Hz 定点，实机标定） */
#if XVFD_ON && XVFD_M512_ON
    if (brand == XVFD_EMU_M512) {
        /* M512 走**受约束**的从站（手册三条硬约束在从站侧强制）。 */
        em->m_server = (XModbusTcpServer*)xvfdM512Server_create(em);
    } else
#endif
    {
        em->m_server = XModbusTcpServer_create();
    }
    if (!em->m_server) return false;
    a = XVariant_create_utf8_str("127.0.0.1");
    p = XVariant_create_int(0);
    XModbusDevice_setConnectionParameter((XModbusDevice*)em->m_server,
        XModbusDevice_NetworkAddressParameter, a);
    XModbusDevice_setConnectionParameter((XModbusDevice*)em->m_server,
        XModbusDevice_NetworkPortParameter, p);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)p);
    /* XModbusDataUnitMap 每寄存器类型一条 unit（xindustry 同款）：HR 用
     * 一条覆盖全部用址的大段（ATV61: 3201..9002；M512: 1000H..FFFFH，
     * 覆盖到 PP 组 FF00H/FF01H）。 */
    if (brand == XVFD_EMU_ATV61) {
        if (!xvfdEmuAddRange(em, 3201, 9002 - 3201 + 1)) return false;
    } else {
        if (!xvfdEmuAddRange(em, 0x1000, 0x10000 - 0x1000)) return false;
    }
    /* 对齐 xindustry_modbus_test 的成熟用法：setMap_ref_base（深拷贝进服务器）。 */
    if (!em->m_map ||
        !XModbusServer_setMap_ref_base((XModbusServer*)em->m_server, em->m_map)) {
        return false;
    }
    em->m_map = NULL;              /* 深拷贝完成，本侧副本释放（destroy 不再管） */
    /* M512 现场前置（附录1）：命令源=通讯(P0.02=2)、主频率源=通讯给定
     * (P0.03=9)、PID 给定源=通讯(PA.00=5)。不配置则 2000H/1000H 会被
     * 从站静默忽略——模拟器按「已正确配置」的现场状态起步。
     * **必须写在 setMap_ref_base 之后**：服务器装载映射前 setData2 无处可写。 */
    if (brand != XVFD_EMU_ATV61) {
        xvfdEmuPut(em, 0xF002, 2);
        xvfdEmuPut(em, 0xF003, 9);
        xvfdEmuPut(em, 0xFA00, 5);
    }
    if (!XModbusDevice_connectDevice((XModbusDevice*)em->m_server)) return false;
    if (outPort && em->m_server->m_tcpServer) {
        *outPort = XTcpServer_serverPort(em->m_server->m_tcpServer);
    }
    return true;
}

/**
 * @brief ATV61 故障注入（等价 python 的 fault 文件语义）。
 * @param em 模拟器。
 * @param code 故障码（0=清除）。
 * @return 无。
 */
static void xvfdEmuAtv61InjectFault(XvfdSlaveEmu* em, int code)
{
    em->m_fault = code;
    if (code) em->m_state = 2;
}

/**
 * @brief M512 故障注入。
 * @param em 模拟器。
 * @param code 故障码（0=清除）。
 * @return 无。
 */
static void xvfdEmuM512InjectFault(XvfdSlaveEmu* em, int code)
{
    em->m_m512Fault = code;
}

/**
 * @brief M512 命令字状态机（复刻 python apply_cmd）。
 * @param em 模拟器。
 * @param cmd 命令字。
 * @return 无。
 */
static void xvfdEmuM512ApplyCmd(XvfdSlaveEmu* em, uint16_t cmd)
{
    if (cmd == 1 || cmd == 3) em->m_m512State = 1;
    else if (cmd == 2 || cmd == 4) em->m_m512State = 2;
    else if (cmd == 5 || cmd == 6) em->m_m512State = 3;
    else if (cmd == 7) { em->m_m512Fault = 0; em->m_m512State = 3; }
}

/**
 * @brief ATV61 命令字状态机（复刻 python apply_cmd）。
 * @param em 模拟器。
 * @param cmd 命令字。
 * @return 无。
 */
static void xvfdEmuAtv61ApplyCmd(XvfdSlaveEmu* em, uint16_t cmd)
{
    int bit7 = (cmd >> 7) & 1;
    int bit3 = (cmd >> 3) & 1;
    int bit1 = (cmd >> 1) & 1;
    int bit0 = cmd & 1;
    em->m_reverse = ((cmd >> 11) & 1) ? 1 : 0;
    if (em->m_fault && !bit7) return;
    if (bit7) {                    /* Fault reset 上升沿 */
        em->m_fault = 0;
        em->m_state = 2;
        return;
    }
    if (!bit1 && !bit0) {          /* Disable voltage → 状态 2 */
        em->m_state = 2;
    } else if (!bit3) {            /* Shutdown / Switch on / Disable operation */
        em->m_state = (bit0 && bit1) ? 4 : 3;
    } else {                       /* Enable operation */
        em->m_state = 5;
    }
}

/**
 * @brief ATV61 ETA 状态字（复刻 python eta()）。
 * @param em 模拟器。
 * @return ETA 值。
 */
static uint16_t xvfdEmuAtv61Eta(const XvfdSlaveEmu* em)
{
    uint16_t eta = 0x0010;         /* Voltage enabled */
    switch (em->m_state) {
    case 2: eta |= 0x0040; break;
    case 3: eta |= 0x0021; break;
    case 4: eta |= 0x0023; break;
    case 5: eta |= 0x0027; break;
    default: break;
    }
    if (em->m_state != 2) eta |= 0x0020;   /* Quick stop 未激活 */
    if (em->m_fault) eta |= 0x0008;
    if (em->m_state == 5) {
        eta |= 0x0200;             /* Remote */
        eta |= 0x0400;             /* Target reached */
    }
    if (em->m_reverse) eta |= 0x4000;
    return eta;
}

/**
 * @brief 模拟器单拍：读命令/给定 → 推进状态机 → 回写状态与遥测。
 * @param em 模拟器。
 * @return 无。
 * @note 由测试泵循环（pump）每轮调用；无真实串口时序，语义等价即可。
 */
static void xvfdSlaveEmuTick(XvfdSlaveEmu* em)
{
    if (!em || !em->m_server) return;
    if (em->m_brand == XVFD_EMU_ATV61) {
        uint16_t cmd = xvfdEmuGet(em, 8501);
        if (cmd != em->m_lastCmd) {
            em->m_lastCmd = cmd;
            xvfdEmuAtv61ApplyCmd(em, cmd);
        }
        em->m_refHz10 = (int16_t)xvfdEmuGet(em, 8521);
        em->m_refRpm = (int16_t)xvfdEmuGet(em, 8602);
        xvfdEmuPut(em, 3201, xvfdEmuAtv61Eta(em));   /* ETA */
        xvfdEmuPut(em, 3202, (uint16_t)em->m_refHz10); /* RFR 0.1Hz */
        xvfdEmuPut(em, 3203, (uint16_t)em->m_refHz10); /* FRH */
        xvfdEmuPut(em, 3204, 100);                     /* LCR 电流 */
        xvfdEmuPut(em, 3205, 69);                      /* OTR 6.9% */
        xvfdEmuPut(em, 3206, 0);                       /* ETI */
        xvfdEmuPut(em, 3207, 240);                     /* ULN 24.0V */
        xvfdEmuPut(em, 3208, 240);                     /* UOP */
        xvfdEmuPut(em, 3209, (uint16_t)em->m_driveThermal);
        xvfdEmuPut(em, 3243, 0x0BB8);                  /* VBUS 300.0V */
        xvfdEmuPut(em, 7121, (uint16_t)em->m_fault);   /* LFT */
        xvfdEmuPut(em, 8604, (uint16_t)em->m_refRpm);  /* RFRD */
        xvfdEmuPut(em, 9630, (uint16_t)em->m_motorThermal);
    } else {
        /* ===== M512：手册语义（附录1 特殊地址区 + 快捷遥测） ===== */
        uint16_t cmd = xvfdEmuGet(em, 0x2000);
        uint16_t cmdSrc = xvfdEmuGet(em, 0xF002);   /* P0.02 命令源（2=通讯） */
        int hz010;   /* 0.1Hz 定点（实机标定：0x1001 单位是 0.1Hz） */
        int16_t setpct;
        if (cmd != em->m_lastCmd) {
            em->m_lastCmd = cmd;
            /* 手册附录1/§6.1：命令源未切到通讯（P0.02 ≠ 2）时 2000H 命令字
             * 被**静默忽略**——模拟器按此实现（设备层另有显式拒绝）。 */
            if (cmdSrc == 2) xvfdEmuM512ApplyCmd(em, cmd);
        }
        /* 1000H 通讯设定值（int16 补码，±10000 = ±100.00%）。 */
        setpct = (int16_t)xvfdEmuGet(em, 0x1000);
        em->m_setpoint = setpct;
        /* 运行频率 = 设定% × 最大频率（0.01Hz 口径）。 */
        /* 运行频率 raw = 设定% × P0.10raw ÷ 10000（两者同为 0.1Hz 定点，比值即工程值） */
        hz010 = (int)((double)setpct * (double)em->m_maxFreq / 10000.0);
        xvfdEmuPut(em, 0x1001, (uint16_t)(hz010 & 0xFFFF));  /* 运行频率 0.1Hz */
        xvfdEmuPut(em, 0x1002, 5400);          /* 母线电压 0.1V → 540.0V */
        xvfdEmuPut(em, 0x1003, 380);           /* 输出电压 1V → 380V */
        xvfdEmuPut(em, 0x1004, 100);           /* 输出电流 0.01A → 1.00A */
        xvfdEmuPut(em, 0x1005, 75);            /* 输出功率 0.1kW → 7.5kW */
        xvfdEmuPut(em, 0x1006, 690);           /* 输出转矩 0.1% → 69.0% */
        xvfdEmuPut(em, 0x1007, 0x0001);        /* DI 输入状态（位图） */
        /* 2001H DO 强制位图 → 1008H DO 输出状态（回读可见强制结果）。 */
        em->m_doForce = xvfdEmuGet(em, 0x2001);
        xvfdEmuPut(em, 0x1008, em->m_doForce); /* DO 输出状态（位图） */
        xvfdEmuPut(em, 0x1009, 1000);          /* AI1 0.01V → 10.00V */
        xvfdEmuPut(em, 0x100A, 500);           /* AI2 0.01V → 5.00V */
        xvfdEmuPut(em, 0x100B, 250);           /* AI3 0.01V → 2.50V */
        xvfdEmuPut(em, 0x1010, 50);            /* PID 设定（量程单位） */
        xvfdEmuPut(em, 0x1011, 48);            /* PID 反馈（量程单位） */
        xvfdEmuPut(em, 0x1012, 1);             /* PLC 阶段 */
        xvfdEmuPut(em, 0x1013, 1000);          /* PULSE 输入脉冲频率 0.01kHz */
        xvfdEmuPut(em, 0x1019, 120);           /* 线速度 1 m/Min */
        xvfdEmuPut(em, 0x101A, 600);           /* 当前上电时间 1Min → 10Min */
        xvfdEmuPut(em, 0x101B, 600);           /* 当前运行时间 0.1Min → 1.0Min */
        xvfdEmuPut(em, 0x101F, (uint16_t)(hz010 & 0xFFFF)); /* 主频率 X 0.1Hz */
        xvfdEmuPut(em, 0x1020, 0);             /* 辅频率 Y 0.01Hz */
        xvfdEmuPut(em, 0x7022, 45);            /* 电机温度 1℃ */
        /* 2002H~2004H：AO1/AO2/PULSE 输出强制（0~0x7FFF）——保存备查。 */
        em->m_aoForce1 = xvfdEmuGet(em, 0x2002);
        em->m_aoForce2 = xvfdEmuGet(em, 0x2003);
        em->m_pulseForce = xvfdEmuGet(em, 0x2004);
        /* 复刻 python：故障时状态字按设定方向显示运行，否则显示状态机状态 */
        xvfdEmuPut(em, 0x3000, (uint16_t)(em->m_m512Fault
            ? (em->m_setpoint >= 0 ? 1 : 2)
            : em->m_m512State));
        xvfdEmuPut(em, 0x8000, (uint16_t)em->m_m512Fault);
        xvfdEmuPut(em, 0xF00A, (uint16_t)em->m_maxFreq);
    }
}

/**
 * @brief 拆除模拟器（断开并删除 TCP 从站）。
 * @param em 模拟器。
 * @return 无。
 */
static void xvfdSlaveEmuDestroy(XvfdSlaveEmu* em)
{
    if (!em) return;
    if (em->m_map) {
        XClassDelete((XClass*)em->m_map);
        em->m_map = NULL;
    }
    if (em->m_server) {
        XModbusDevice_disconnectDevice((XModbusDevice*)em->m_server);
        XObject_deleteLater((XObject*)em->m_server);
        em->m_server = NULL;
    }
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
#endif /* XVFD_SLAVE_EMU_H */
