/**
 * @file       xfdv_hub_limits_test.c
 * @brief      XVfd 从站级通讯约束单测（Hub 侧三条约束 + M512 从站侧硬约束/遥测语义）。
 * @details    [2026-11-02] 校验 XModbusHub_setUnitLimits 登记的三条**从站级**约束
 *             确实生效（实现见 Src/XCore/XProtocol/XModbus/XModbusHub.c）：
 *               ① m_maxReadRegisters：单帧读寄存器数上限——超限的连续区被拆成多帧；
 *               ② m_rangeBarrier：映射段边界是读帧硬边界——段两侧不并进同一读帧；
 *               ③ m_singleWriteOnly：只允许单寄存器写——相邻写不并成一帧 FC16，
 *                  而是逐字 FC06 下发。
 *             每条约束都配「无约束对照组」：只有对照组的读组数 / 从站收到的帧
 *             形态与实验组**不同**，才算这条约束真的在起作用（否则「两组一样」
 *             既可能是约束生效也可能是约束没接上）。
 *
 *             另含 M512 **从站侧**两组用例（手册附录 1 三条硬约束 + 遥测/命令语义）：
 *               ④ 功能码白名单 / 单次读 n ≤ 12 / 不跨功能码组 → 异常帧
 *                  83H/86H + 错误码 01/02/03；
 *               ⑤ 快捷遥测（1001H~1020H）、命令（2000H~2004H）、状态（3000H）、
 *                  故障（8000H）、电机温度（7022H）的单位与联动语义。
 *
 *             断言只落在**可观测行为**上：
 *               - 读：Hub 的读组数（XModbusHub_readGroupCount，公开 API）
 *                     + 从站侧收到的 FC03 帧（起始地址 / 寄存器数）；
 *               - 写：从站侧收到的写帧（FC06/FC16、起始地址、数量、写入值）；
 *               - 从站语义：模拟器寄存器里的真值（手册最小单位）。
 *             不读任何内部私有字段。
 *
 *             载体是**进程内假从站**（XModbusClient 的测试子类，重写
 *             sendRawRequest）：请求逐条记录，并在测试泵里**异步**应答
 *             （模拟从站时延；同步应答会撞上 Hub 的同步收口嵌套护栏，
 *              第二个字的下发会被吞掉、脏区间滞留）。
 *             全程**不碰任何传输层**（无 socket / 无 pty / 无串口），
 *             故本用例不受本机 TCP 环回数据丢失的影响，可稳定判定。
 *
 *             退出码=失败用例数。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CXinYueConfig.h"
#include "XModbus_config.h"

#if XMODBUS_ON && XMODBUS_CLIENT_ON && XMODBUS_HUB_ON

#include "XClass.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusClient.h"
#include "XModbusClient_Protected.h"
#include "XModbusDevice.h"
#include "XModbusDevice_Protected.h"
#include "XModbusReply.h"
#include "XModbusReply_Protected.h"
#include "XModbusPdu.h"
#include "XModbusDataUnit.h"
/* M512 品牌层（仅取其配置宏与通信组表；见文件末尾「M512 从站侧硬约束」段）。 */
#include "XVfd/XVfd_config.h"
#if XVFD_ON && XVFD_M512_ON
#include "XVfd/XVfdM512.h"
#endif

/* ==================== 基础设施 ==================== */

static int g_fail = 0;
static const char* g_case = "";

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s (%s@%d)\n", msg, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

/* ==================== 进程内假从站 ==================== */

/** @brief 假从站收到的请求流水上限。 */
#define XVFD_LIMIT_MAXREQ 256

XCLASS_DEFINE_BEGING(XVfdLimitSlaveClient)
XCLASS_DEFINE_EXTEND_END(XVfdLimitSlaveClient, XModbusClient)

/**
 * @brief 假从站记录的一条请求（从站视角）。
 */
typedef struct XVfdLimitSlaveReq {
    uint8_t  m_fc;        /**< 功能码（3=FC03 读 / 6=FC06 写 / 16=FC16 写）。 */
    uint16_t m_addr;      /**< 远端起始地址。 */
    uint16_t m_count;     /**< 读=寄存器数；FC06=1；FC16=写入寄存器数。 */
    uint16_t m_value;     /**< FC06 的写入值。 */
} XVfdLimitSlaveReq;

/**
 * @brief 假从站在途（已收到、未应答）的请求。
 */
typedef struct XVfdLimitSlavePending {
    XModbusReply* m_reply; /**< 待应答的 Reply（Hub 持有所有权）。 */
    uint8_t  m_fc;         /**< 功能码。 */
    uint8_t  m_type;       /**< XModbusRegisterType（读的结果单元类型）。 */
    uint16_t m_addr;       /**< 远端起始地址。 */
    uint16_t m_count;      /**< 寄存器数。 */
} XVfdLimitSlavePending;

/**
 * @brief 假从站客户端（XModbusClient 子类；记录请求 + 异步应答）。
 */
typedef struct XVfdLimitSlaveClient {
    XModbusClient m_base;                            /**< 基类（继承自 XModbusClient）。 */
    XVfdLimitSlaveReq m_reqs[XVFD_LIMIT_MAXREQ];      /**< 请求流水。 */
    int m_reqCount;                                  /**< 已记录请求数。 */
    XVfdLimitSlavePending m_pending[XVFD_LIMIT_MAXREQ]; /**< 在途未应答请求。 */
    int m_pendingCount;                              /**< 在途请求数。 */
    int m_readCount;                                 /**< FC03 次数。 */
    int m_writeCount;                                /**< 写请求次数（FC06+FC16）。 */
    int m_fc6Count;                                  /**< FC06 次数。 */
    int m_fc16Count;                                 /**< FC16 次数。 */
} XVfdLimitSlaveClient;

/**
 * @brief 假从站的 open：直接置「已连接」（无真实传输层）。
 */
static bool VXvfdLimitSlave_open(XModbusDevice* device)
{
    if (!device) return false;
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    return true;
}

/**
 * @brief 假从站的 close：置「未连接」。
 */
static void VXvfdLimitSlave_close(XModbusDevice* device)
{
    if (!device) return;
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

/**
 * @brief 假从站的 sendRawRequest：记录请求 + 建 Reply 入在途队列（不立即应答）。
 * @param baseClient 客户端（实为 XVfdLimitSlaveClient）。
 * @param request 请求。
 * @param serverAddress 从站地址。
 * @param type 参数传递语义（Copy/Move/Ref）。
 * @return 新建的 Reply（未终结，由测试泵异步应答）；失败返回 NULL。
 */
static XModbusReply* VXvfdLimitSlave_sendRawRequest(XModbusClient* baseClient,
                                                    const XModbusRequest* request,
                                                    int serverAddress,
                                                    XFuncParamType type)
{
    XVfdLimitSlaveClient* c = (XVfdLimitSlaveClient*)baseClient;
    XModbusPdu_FunctionCode fc;
    const uint8_t* d;
    size_t n;
    XModbusReply* reply = NULL;
    if (!c || !request) return NULL;
    fc = XModbusPdu_functionCode((const XModbusPdu*)request);
    d = (const uint8_t*)XContainerDataAddr((const XContainer*)((const XModbusPdu*)request)->m_data);
    n = XByteArray_size_base(((const XModbusPdu*)request)->m_data);
    /* 记录（从站视角）：地址在 data[0..1]，数量/值在 data[2..3]。 */
    if (c->m_reqCount < XVFD_LIMIT_MAXREQ) {
        XVfdLimitSlaveReq* r = &c->m_reqs[c->m_reqCount++];
        r->m_fc = (uint8_t)fc;
        r->m_addr = (n >= 2) ? (uint16_t)(((uint16_t)d[0] << 8) | d[1]) : 0;
        if (fc == XModbusPdu_WriteSingleRegister) {
            r->m_count = 1;
            r->m_value = (n >= 4) ? (uint16_t)(((uint16_t)d[2] << 8) | d[3]) : 0;
        } else {
            r->m_count = (n >= 4) ? (uint16_t)(((uint16_t)d[2] << 8) | d[3]) : 0;
            r->m_value = 0;
        }
    }
    if (fc == XModbusPdu_ReadHoldingRegisters) {
        ++c->m_readCount;
    } else if (fc == XModbusPdu_WriteSingleRegister) {
        ++c->m_writeCount;
        ++c->m_fc6Count;
    } else if (fc == XModbusPdu_WriteMultipleRegisters) {
        ++c->m_writeCount;
        ++c->m_fc16Count;
    }
    /* 建 Reply（按调用方给的参数语义接管/复制请求）。 */
    if (type == XFuncParamType_Copy) {
        reply = XModbusClient_createReply(baseClient, request, serverAddress);
    } else if (type == XFuncParamType_Move) {
        reply = XModbusClient_createReply_move(baseClient, (XModbusRequest*)request,
                                               serverAddress);
    } else {
        reply = XModbusClient_createReply_ref(baseClient, (XModbusRequest*)request,
                                              serverAddress);
    }
    if (!reply) return NULL;
    XModbusReply_setState(reply, XModbusReply_State_Waiting);
    if (c->m_pendingCount < XVFD_LIMIT_MAXREQ) {
        XVfdLimitSlavePending* p = &c->m_pending[c->m_pendingCount++];
        p->m_reply = reply;
        p->m_fc = (uint8_t)fc;
        p->m_type = (uint8_t)XModbusHoldingRegisters;
        if (fc == XModbusPdu_ReadCoils) p->m_type = (uint8_t)XModbusCoils;
        else if (fc == XModbusPdu_ReadDiscreteInputs) p->m_type = (uint8_t)XModbusDiscreteInputs;
        else if (fc == XModbusPdu_ReadInputRegisters) p->m_type = (uint8_t)XModbusInputRegisters;
        p->m_addr = (n >= 2) ? (uint16_t)(((uint16_t)d[0] << 8) | d[1]) : 0;
        p->m_count = (fc == XModbusPdu_WriteSingleRegister)
                         ? 1
                         : ((n >= 4) ? (uint16_t)(((uint16_t)d[2] << 8) | d[3]) : 0);
    } else {
        /* 队列满：直接终结（不让 Hub 卡在途）。 */
        XModbusReply_setFinished(reply, true);
    }
    return reply;
}

/**
 * @brief 假从站类初始化（继承 XModbusClient，重载 open/close/sendRawRequest）。
 */
static XVtable* XVfdLimitSlave_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdLimitSlaveClient)
        XVTABLE_INHERIT_XCLASS(XModbusClient);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, VXvfdLimitSlave_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, VXvfdLimitSlave_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest, VXvfdLimitSlave_sendRawRequest);
    XCLASS_SHOW_SIZE_DEFAULT(XVfdLimitSlaveClient);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 建一个假从站客户端（堆对象；所有权随 Hub 注入后归 Hub）。
 * @return 新客户端；失败返回 NULL。
 */
static XVfdLimitSlaveClient* XVfdLimitSlave_create(void)
{
    XVfdLimitSlaveClient* c =
        (XVfdLimitSlaveClient*)XMemory_malloc(sizeof(XVfdLimitSlaveClient),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    XModbusClient_init(&c->m_base);
    XClassGetVtable(c) = XVfdLimitSlave_class_init();
    Set_Class_Memory(c, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(c, true);
    return c;
}

/**
 * @brief 假从站应答一拍：把在途请求逐条应答（读回填值、写回显）。
 * @param c 假从站。
 * @return 无。
 * @note 应答是**异步**的（相对 Hub 的发送调用）：避免 Hub「发送即同步终态」
 *       的嵌套收口护栏把第二笔写吞掉。
 */
static void XVfdLimitSlave_tick(XVfdLimitSlaveClient* c)
{
    int i = 0;
    if (!c) return;
    while (i < c->m_pendingCount) {
        XVfdLimitSlavePending p = c->m_pending[i];
        if (p.m_reply && (p.m_fc == XModbusPdu_ReadHoldingRegisters ||
                          p.m_fc == XModbusPdu_ReadInputRegisters ||
                          p.m_fc == XModbusPdu_ReadCoils ||
                          p.m_fc == XModbusPdu_ReadDiscreteInputs)) {
            XModbusDataUnit* unit =
                XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XModbusRegisterType)p.m_type,
                                           p.m_addr, p.m_count);
            if (unit) {
                uint16_t i2;
                for (i2 = 0; i2 < p.m_count; ++i2) {
                    /* 值里带远端地址：便于核对「哪段被读到」。 */
                    XModbusDataUnit_setValue(unit, i2,
                        (uint16_t)(0x1000u + p.m_addr + i2));
                }
                XModbusReply_setResult(p.m_reply, unit);
                XClassDelete(unit);
            }
        }
        XModbusReply_setFinished(p.m_reply, true);
        /* 移除本条（应答过程中可能又入队新的在途请求 → 取末位补位）。 */
        c->m_pending[i] = c->m_pending[c->m_pendingCount - 1];
        --c->m_pendingCount;
    }
}

/* ==================== 测试台架 ==================== */

/**
 * @brief 一段映射（虚拟区间 → 远端区间）。
 */
typedef struct XVfdLimitRange {
    uint16_t m_vstart;    /**< 虚拟起始地址。 */
    uint16_t m_count;     /**< 段长。 */
    uint16_t m_remote;    /**< 远端起始地址。 */
} XVfdLimitRange;

/**
 * @brief 测试台架（Hub + 假从站）。
 */
typedef struct XVfdLimitRig {
    XModbusHub* m_hub;               /**< Hub。 */
    XVfdLimitSlaveClient* m_slave;   /**< 假从站（所有权已归 Hub）。 */
    int m_devId;                     /**< 设备句柄。 */
} XVfdLimitRig;

/**
 * @brief 装配台架：建 Hub + 假从站 + 映射 + 从站约束。
 * @param rig 台架（出参）。
 * @param ranges 映射段。
 * @param n 段数。
 * @param limits 从站约束；NULL=不登记（对照组）。
 * @param unitId 从站地址。
 * @return 成功返回 true。
 */
static bool XVfdLimitRig_build(XVfdLimitRig* rig, const XVfdLimitRange* ranges, int n,
                               const XModbusHubUnitLimits* limits, int unitId)
{
    XModbusMapping* map;
    int i;
    memset(rig, 0, sizeof(*rig));
    rig->m_devId = -1;
    rig->m_slave = XVfdLimitSlave_create();
    if (!rig->m_slave) return false;
    if (!XModbusDevice_connectDevice((XModbusDevice*)rig->m_slave)) return false;
    rig->m_hub = XModbusHub_create();
    if (!rig->m_hub) return false;
    rig->m_devId = XModbusHub_addDevice_2(rig->m_hub,
                                          (XModbusClient*)rig->m_slave, "limit-slave");
    if (rig->m_devId <= 0) return false;
    map = XModbusMapping_create();
    if (!map) return false;
    for (i = 0; i < n; ++i) {
        if (!XModbusMapping_addRange(map, XModbusHoldingRegisters, ranges[i].m_vstart,
                                     ranges[i].m_count, rig->m_devId, unitId,
                                     ranges[i].m_remote, 0)) {
            XClassDelete((XClass*)map);
            return false;
        }
    }
    XModbusHub_setMapping(rig->m_hub, map);   /* 所有权移交 Hub */
    if (limits) {
        XModbusHub_setUnitLimits(rig->m_hub, rig->m_devId, unitId, limits);
    }
    /* 只轮询 HR（VFD 全部用址在 HR）。 */
    XModbusHub_setPollAreas(rig->m_hub, (1u << XModbusHoldingRegisters));
    XModbusHub_setPollInterval(rig->m_hub, rig->m_devId, 20);
    /* 合并窗口拉长：两次相邻写落进同一条脏区间（FC16 与逐字 FC06 的分水岭）。 */
    XModbusHub_setWriteCoalesceMs(rig->m_hub, 5000);
    return true;
}

/**
 * @brief 拆台架（Hub 析构连带释放假从站）。
 * @param rig 台架。
 * @return 无。
 */
static void XVfdLimitRig_destroy(XVfdLimitRig* rig)
{
    if (!rig) return;
    if (rig->m_hub) {
        XModbusHub_stopPolling(rig->m_hub);
        XObject_deleteLater((XObject*)rig->m_hub);
        rig->m_hub = NULL;
    }
    rig->m_slave = NULL;   /* 所有权已随 Hub 释放 */
    rig->m_devId = -1;
}

/**
 * @brief 泵事件若干毫秒（假从站在每轮前应答在途请求）。
 * @param rig 台架。
 * @param ms 毫秒数。
 * @return 无。
 */
static void XVfdLimitRig_pump(XVfdLimitRig* rig, int ms)
{
    const uint64_t deadline = (uint64_t)XDateTime_currentMSecsSinceEpoch() + (uint64_t)ms;
    do {
        XVfdLimitSlave_tick(rig->m_slave);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        {
            const uint64_t spin = (uint64_t)XDateTime_currentMSecsSinceEpoch() + 2;
            while ((uint64_t)XDateTime_currentMSecsSinceEpoch() < spin) { /* 2ms 节拍 */ }
        }
    } while ((uint64_t)XDateTime_currentMSecsSinceEpoch() < deadline);
    XVfdLimitSlave_tick(rig->m_slave);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/**
 * @brief 统计从站收到的 FC03 读帧。
 * @param c 假从站。
 * @param outMaxCount 输出：读帧里的最大寄存器数（无读帧时 0）。
 * @param outFrames 输出：读帧条数。
 * @param outMaxSpan 输出：读帧跨越的**最大远端地址跨度**（末地址-首地址+1）。
 * @return 无。
 */
static void XVfdLimitSlave_readStats(const XVfdLimitSlaveClient* c, uint16_t* outMaxCount,
                                     int* outFrames, uint16_t* outMaxSpan)
{
    int i;
    uint16_t lo = 0xFFFF;
    uint16_t hi = 0;
    *outMaxCount = 0;
    *outFrames = 0;
    *outMaxSpan = 0;
    if (!c) return;
    for (i = 0; i < c->m_reqCount; ++i) {
        const XVfdLimitSlaveReq* r = &c->m_reqs[i];
        uint16_t end;
        if (r->m_fc != XModbusPdu_ReadHoldingRegisters) continue;
        ++(*outFrames);
        if (r->m_count > *outMaxCount) *outMaxCount = r->m_count;
        end = (uint16_t)(r->m_addr + r->m_count);
        if (r->m_addr < lo) lo = r->m_addr;
        if (end > hi) hi = end;
    }
    if (lo != 0xFFFF) *outMaxSpan = (uint16_t)(hi - lo);
}

/**
 * @brief 统计从站收到的写帧（按 FC 分类）。
 * @param c 假从站。
 * @param outFc6 输出：FC06 次数。
 * @param outFc16 输出：FC16 次数。
 * @param outMaxCount 输出：写帧里的最大写入寄存器数（FC16 口径；FC06 记 1）。
 * @return 无。
 */
static void XVfdLimitSlave_writeStats(const XVfdLimitSlaveClient* c, int* outFc6,
                                      int* outFc16, uint16_t* outMaxCount)
{
    int i;
    *outFc6 = 0;
    *outFc16 = 0;
    *outMaxCount = 0;
    if (!c) return;
    for (i = 0; i < c->m_reqCount; ++i) {
        const XVfdLimitSlaveReq* r = &c->m_reqs[i];
        if (r->m_fc == XModbusPdu_WriteSingleRegister) {
            ++(*outFc6);
            if (1u > *outMaxCount) *outMaxCount = 1;
        } else if (r->m_fc == XModbusPdu_WriteMultipleRegisters) {
            ++(*outFc16);
            if (r->m_count > *outMaxCount) *outMaxCount = r->m_count;
        }
    }
}

/**
 * @brief 取第 idx 条写帧的地址（按记录顺序）。
 * @param c 假从站。
 * @param idx 序号（写帧序号，从 0 起）。
 * @param outAddr 输出：远端起始地址。
 * @param outValue 输出：写入值（FC06 有效；FC16 输出 0）。
 * @param outCount 输出：写入寄存器数。
 * @return 命中返回 true。
 */
static bool XVfdLimitSlave_writeAt(const XVfdLimitSlaveClient* c, int idx,
                                   uint16_t* outAddr, uint16_t* outValue,
                                   uint16_t* outCount)
{
    int i;
    int seen = 0;
    if (!c) return false;
    for (i = 0; i < c->m_reqCount; ++i) {
        const XVfdLimitSlaveReq* r = &c->m_reqs[i];
        if (r->m_fc != XModbusPdu_WriteSingleRegister &&
            r->m_fc != XModbusPdu_WriteMultipleRegisters) continue;
        if (seen == idx) {
            if (outAddr) *outAddr = r->m_addr;
            if (outValue) *outValue = r->m_value;
            if (outCount) *outCount = r->m_count;
            return true;
        }
        ++seen;
    }
    return false;
}

/* ==================== 用例 ==================== */

/**
 * @brief 用例①：单帧读上限（m_maxReadRegisters）。
 * @details 30 个连续寄存器、上限 12 → 读组数应为 3（12+12+6），
 *          从站侧每个 FC03 帧的寄存器数都 ≤ 12；对照组（不登记约束）
 *          读组数为 1、帧长 30。
 * @return 失败用例数。
 */
static int caseMaxReadRegisters(void)
{
    XVfdLimitRig rig;
    XVfdLimitRange ranges[1];
    XModbusHubUnitLimits lim;
    uint16_t maxCount = 0;
    uint16_t span = 0;
    int frames = 0;
    int groups = 0;
    int failBefore = g_fail;

    g_case = "maxReadRegisters";
    memset(ranges, 0, sizeof(ranges));
    ranges[0].m_vstart = 0;
    ranges[0].m_count = 30;
    ranges[0].m_remote = 0x1000;

    /* 实验组：上限 12。 */
    memset(&lim, 0, sizeof(lim));
    lim.m_maxReadRegisters = 12;
    CHECK(XVfdLimitRig_build(&rig, ranges, 1, &lim, 1), "台架装配（读上限 12）");
    if (!rig.m_hub) return g_fail - failBefore;
    XModbusHub_startPolling(rig.m_hub);
    XVfdLimitRig_pump(&rig, 400);
    groups = XModbusHub_readGroupCount(rig.m_hub, rig.m_devId);
    XVfdLimitSlave_readStats(rig.m_slave, &maxCount, &frames, &span);
    CHECK(groups > 1, "超上限的连续区被拆成多个读组（读组数 > 1）");
    CHECK(groups == 3, "读组数 = 3（30 字按 12/12/6 切分）");
    CHECK(frames > 0, "从站侧收到读帧");
    CHECK(maxCount <= 12, "从站侧每个读帧的寄存器数 ≤ 12（不超单帧上限）");
    CHECK(span == 30, "读帧合起来覆盖整段 30 字（无漏读）");
    XVfdLimitRig_destroy(&rig);

    /* 对照组：不登记约束 → 一帧读完。 */
    CHECK(XVfdLimitRig_build(&rig, ranges, 1, NULL, 1), "台架装配（对照组：无约束）");
    if (!rig.m_hub) return g_fail - failBefore;
    XModbusHub_startPolling(rig.m_hub);
    XVfdLimitRig_pump(&rig, 400);
    groups = XModbusHub_readGroupCount(rig.m_hub, rig.m_devId);
    XVfdLimitSlave_readStats(rig.m_slave, &maxCount, &frames, &span);
    CHECK(groups == 1, "对照组读组数 = 1（不拆帧）");
    CHECK(maxCount == 30, "对照组一帧读完 30 字");
    XVfdLimitRig_destroy(&rig);
    return g_fail - failBefore;
}

/**
 * @brief 用例②：功能码组边界（m_rangeBarrier）。
 * @details 两个**远端相邻**的映射段（0x1000~0x1007 / 0x1008~0x100F）：
 *          登记边界约束 → 两段不得并进同一读帧（读组数 2、每帧 ≤ 8 字）；
 *          对照组 → 远端连续被合并成一帧 16 字。
 * @return 失败用例数。
 */
static int caseRangeBarrier(void)
{
    XVfdLimitRig rig;
    XVfdLimitRange ranges[2];
    XModbusHubUnitLimits lim;
    uint16_t maxCount = 0;
    uint16_t span = 0;
    int frames = 0;
    int groups = 0;
    int failBefore = g_fail;

    g_case = "rangeBarrier";
    memset(ranges, 0, sizeof(ranges));
    ranges[0].m_vstart = 0;
    ranges[0].m_count = 8;
    ranges[0].m_remote = 0x1000;
    ranges[1].m_vstart = 8;
    ranges[1].m_count = 8;
    ranges[1].m_remote = 0x1008;

    /* 实验组：组边界为硬边界。 */
    memset(&lim, 0, sizeof(lim));
    lim.m_rangeBarrier = true;
    CHECK(XVfdLimitRig_build(&rig, ranges, 2, &lim, 1), "台架装配（组边界约束）");
    if (!rig.m_hub) return g_fail - failBefore;
    XModbusHub_startPolling(rig.m_hub);
    XVfdLimitRig_pump(&rig, 400);
    groups = XModbusHub_readGroupCount(rig.m_hub, rig.m_devId);
    XVfdLimitSlave_readStats(rig.m_slave, &maxCount, &frames, &span);
    CHECK(groups == 2, "映射段边界两侧不并组（读组数 = 2）");
    CHECK(maxCount <= 8, "从站侧读帧不跨段（每帧 ≤ 8 字）");
    CHECK(span == 16, "两帧合起来覆盖整段 16 字");
    XVfdLimitRig_destroy(&rig);

    /* 对照组：远端连续 → 并成一帧。 */
    CHECK(XVfdLimitRig_build(&rig, ranges, 2, NULL, 1), "台架装配（对照组：无约束）");
    if (!rig.m_hub) return g_fail - failBefore;
    XModbusHub_startPolling(rig.m_hub);
    XVfdLimitRig_pump(&rig, 400);
    groups = XModbusHub_readGroupCount(rig.m_hub, rig.m_devId);
    XVfdLimitSlave_readStats(rig.m_slave, &maxCount, &frames, &span);
    CHECK(groups == 1, "对照组读组数 = 1（远端连续被合并）");
    CHECK(maxCount == 16, "对照组一帧读 16 字（跨段合并）");
    XVfdLimitRig_destroy(&rig);
    return g_fail - failBefore;
}

/**
 * @brief 用例③：只允许单寄存器写（m_singleWriteOnly）。
 * @details 相邻两个地址各写一次（合并窗口内 → 同一条脏区间）：
 *          登记约束 → 从站收到**两次 FC06**（各 1 字，地址相邻）；
 *          对照组 → 从站收到**一次 FC16**（2 字）。
 * @return 失败用例数。
 */
static int caseSingleWriteOnly(void)
{
    XVfdLimitRig rig;
    XVfdLimitRange ranges[1];
    XModbusHubUnitLimits lim;
    int fc6 = 0;
    int fc16 = 0;
    uint16_t maxCount = 0;
    uint16_t addr0 = 0;
    uint16_t addr1 = 0;
    uint16_t val0 = 0;
    uint16_t val1 = 0;
    uint16_t cnt = 0;
    int failBefore = g_fail;

    g_case = "singleWriteOnly";
    memset(ranges, 0, sizeof(ranges));
    ranges[0].m_vstart = 0;
    ranges[0].m_count = 2;
    ranges[0].m_remote = 0x2000;

    /* 实验组：只 FC06。 */
    memset(&lim, 0, sizeof(lim));
    lim.m_singleWriteOnly = true;
    CHECK(XVfdLimitRig_build(&rig, ranges, 1, &lim, 1), "台架装配（只 FC06 约束）");
    if (!rig.m_hub) return g_fail - failBefore;
    XModbusHub_writeValue(rig.m_hub, XModbusHoldingRegisters, 0, 0x1111);
    XModbusHub_writeValue(rig.m_hub, XModbusHoldingRegisters, 1, 0x2222);
    XModbusHub_flush(rig.m_hub);
    XVfdLimitRig_pump(&rig, 200);
    XVfdLimitSlave_writeStats(rig.m_slave, &fc6, &fc16, &maxCount);
    CHECK(fc16 == 0, "从站侧没有收到 FC16（多寄存器写）");
    CHECK(fc6 == 2, "从站侧收到两次 FC06（两次单寄存器写）");
    CHECK(maxCount == 1, "每次 FC06 只写 1 个寄存器");
    CHECK(XVfdLimitSlave_writeAt(rig.m_slave, 0, &addr0, &val0, &cnt) && addr0 == 0x2000,
          "第 1 次写落在远端 0x2000");
    CHECK(XVfdLimitSlave_writeAt(rig.m_slave, 1, &addr1, &val1, &cnt) && addr1 == 0x2001,
          "第 2 次写落在远端 0x2001");
    CHECK(val0 == 0x1111 && val1 == 0x2222, "两次写入值各自正确");
    XVfdLimitRig_destroy(&rig);

    /* 对照组：无约束 → 相邻写并成一帧 FC16。 */
    CHECK(XVfdLimitRig_build(&rig, ranges, 1, NULL, 1), "台架装配（对照组：无约束）");
    if (!rig.m_hub) return g_fail - failBefore;
    XModbusHub_writeValue(rig.m_hub, XModbusHoldingRegisters, 0, 0x1111);
    XModbusHub_writeValue(rig.m_hub, XModbusHoldingRegisters, 1, 0x2222);
    XModbusHub_flush(rig.m_hub);
    XVfdLimitRig_pump(&rig, 200);
    XVfdLimitSlave_writeStats(rig.m_slave, &fc6, &fc16, &maxCount);
    CHECK(fc16 == 1, "对照组：相邻写并成一帧 FC16");
    CHECK(fc6 == 0, "对照组：没有 FC06");
    CHECK(maxCount == 2, "对照组：FC16 一帧写 2 个寄存器");
    XVfdLimitRig_destroy(&rig);
    return g_fail - failBefore;
}

/* ==================== M512 从站侧硬约束（手册附录 1） ==================== */

#if XVFD_ON && XVFD_M512_ON

#include "xvfd_slave_emu.h"

/**
 * @brief 构造一条 Modbus 请求 PDU（供从站侧直调）。
 * @param fc 功能码。
 * @param addr 起始地址。
 * @param count 数量（FC06 传 1）。
 * @return 新请求（调用方 XClassDelete 释放）；失败返回 NULL。
 */
static XModbusRequest* xvfdLimitMakeRequest(uint8_t fc, uint16_t addr, uint16_t count)
{
    XModbusRequest* req = XModbusRequest_create();
    uint8_t data[4];
    if (!req) return NULL;
    data[0] = (uint8_t)(addr >> 8);
    data[1] = (uint8_t)(addr & 0xFF);
    data[2] = (uint8_t)(count >> 8);
    data[3] = (uint8_t)(count & 0xFF);
    XModbusPdu_setFunctionCode((XModbusPdu*)req, (XModbusPdu_FunctionCode)fc);
    XModbusPdu_setData((XModbusPdu*)req, data, 4);
    return req;
}

/**
 * @brief 直接问受约束的 M512 从站要一个应答（**不经传输层**）。
 * @param srv 从站。
 * @param fc 功能码。
 * @param addr 起始地址。
 * @param count 数量。
 * @return 应答（调用方 XClassDelete 释放）；失败返回 NULL。
 */
static XModbusResponse* xvfdLimitAskM512(XvfdM512Server* srv, uint8_t fc,
                                          uint16_t addr, uint16_t count)
{
    XModbusRequest* req = xvfdLimitMakeRequest(fc, addr, count);
    XModbusResponse* resp;
    if (!req) return NULL;
    resp = VXvfdM512Server_processRequest((XModbusServer*)srv, req);
    XClassDelete((XClass*)req);
    return resp;
}

/**
 * @brief 用例④：M512 从站侧三条硬约束（FC 白名单 / 读数量上限 / 不跨组）。
 * @details 判定走**纯逻辑**入口（xvfdM512CheckRequestBrand）与从站请求处理
 *          入口（异常帧功能码 + 错误码），二者都**不依赖传输层**。
 * @return 失败用例数。
 */
static int caseM512SlaveConstraints(void)
{
    uint8_t exc = 0;
    uint8_t excFc = 0;
    XvfdM512Server* srv = NULL;
    XModbusResponse* resp = NULL;
    int failBefore = g_fail;

    g_case = "m512SlaveConstraints";

    /* ① 只支持 FC03（读）与 FC06（写）。 */
    CHECK(!xvfdM512CheckRequestBrand(0x04, 0x1000, 1, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_FUNCTION && excFc == 0x83,
          "FC04（读输入寄存器）被拒：错误码 01 + 异常帧 83H");
    CHECK(!xvfdM512CheckRequestBrand(0x05, 0x2000, 1, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_FUNCTION && excFc == 0x86,
          "FC05（写单线圈）被拒：错误码 01 + 异常帧 86H");
    CHECK(!xvfdM512CheckRequestBrand(0x0F, 0x2000, 1, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_FUNCTION && excFc == 0x86,
          "FC0F（写多线圈）被拒：错误码 01 + 异常帧 86H");
    CHECK(!xvfdM512CheckRequestBrand(0x10, 0x2000, 2, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_FUNCTION && excFc == 0x86,
          "FC10（写多寄存器）被拒：错误码 01 + 异常帧 86H");
    CHECK(xvfdM512CheckRequestBrand(0x03, 0x1001, 8, &exc, &excFc),
          "FC03（读保持寄存器）放行");
    CHECK(xvfdM512CheckRequestBrand(0x06, 0x2000, 1, &exc, &excFc),
          "FC06（写单寄存器）放行");

    /* ② 单次读寄存器数 n ≤ 12。 */
    CHECK(!xvfdM512CheckRequestBrand(0x03, 0x1001, 13, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_VALUE && excFc == 0x83,
          "单次读 13 字被拒：错误码 03 + 异常帧 83H");
    CHECK(xvfdM512CheckRequestBrand(0x03, 0xFC00, 12, &exc, &excFc),
          "单次读 12 字放行（上限内，且整段落在 FC00H~FC0FH 组内）");
    CHECK(!xvfdM512CheckRequestBrand(0x03, 0xFC00, 13, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_VALUE,
          "单次读 13 字（组更大也放不下）被拒：错误码 03");
    CHECK(!xvfdM512CheckRequestBrand(0x03, 0x1001, 12, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_ADDRESS,
          "从 1001H 读 12 字：数量合规但跨组 → 错误码 02（组边界优先于数量）");

    /* ③ 一帧读不得跨过功能码组边界。
     * [2026-11-04 组表口径对齐] 通信组表已按真机实测合并：0x1000（给定）
     * 与 0x1001~0x1008 同属一条读窗条目（0x1000 起读 8 字真机正常，
     * 从 0x1001 起连读静默返全 0），故「1000H 是独立条目、读 2 字即跨组」
     * 的旧前置不再成立。跨组判据改用该条目自身的尾端：0x1000 起读 10 字
     * 越出 0x1008（数量仍在 12 字上限内）⇒ 仍须以错误码 02 拒绝。 */
    CHECK(xvfdM512CheckRequestBrand(0x03, 0x1000, 2, &exc, &excFc),
          "从 1000H 读 2 字在同一条目内放行（0x1000~0x1008 已合为一条）");
    CHECK(!xvfdM512CheckRequestBrand(0x03, 0x1000, 10, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_ADDRESS && excFc == 0x83,
          "从 1000H 读 10 字跨出 0x1008 被拒：错误码 02");
    CHECK(!xvfdM512CheckRequestBrand(0x03, 0x1001, 9, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_ADDRESS,
          "从 1001H 读 9 字跨组被拒（1001H~1008H 是一条条目）：错误码 02");
    CHECK(xvfdM512CheckRequestBrand(0x03, 0x1001, 8, &exc, &excFc),
          "整组读 1001H~1008H 放行");
    CHECK(!xvfdM512CheckRequestBrand(0x03, 0x1014, 1, &exc, &excFc) &&
          exc == XVFD_M512_EXC_ILLEGAL_ADDRESS,
          "读组间空洞地址（1014H）被拒：错误码 02");

    /* 从站请求处理入口：异常帧功能码与错误码（不经传输层）。 */
    srv = xvfdM512Server_create(NULL);
    CHECK(srv != NULL, "受约束从站创建");
    if (srv) {
        resp = xvfdLimitAskM512(srv, 0x04, 0x1000, 1);
        CHECK(resp && XModbusPdu_functionCodeRaw((const XModbusPdu*)resp) == 0x83 &&
              XModbusPdu_exceptionCode((const XModbusPdu*)resp) == 0x01,
              "从站回异常帧 83H + 错误码 01（FC04）");
        if (resp) XClassDelete((XClass*)resp);
        resp = xvfdLimitAskM512(srv, 0x10, 0x2000, 2);
        CHECK(resp && XModbusPdu_functionCodeRaw((const XModbusPdu*)resp) == 0x86 &&
              XModbusPdu_exceptionCode((const XModbusPdu*)resp) == 0x01,
              "从站回异常帧 86H + 错误码 01（FC10）");
        if (resp) XClassDelete((XClass*)resp);
        resp = xvfdLimitAskM512(srv, 0x03, 0x1001, 13);
        CHECK(resp && XModbusPdu_functionCodeRaw((const XModbusPdu*)resp) == 0x83 &&
              XModbusPdu_exceptionCode((const XModbusPdu*)resp) == 0x03,
              "从站回异常帧 83H + 错误码 03（读 13 字）");
        if (resp) XClassDelete((XClass*)resp);
        resp = xvfdLimitAskM512(srv, 0x03, 0x1000, 2);
        CHECK(resp && XModbusPdu_functionCodeRaw((const XModbusPdu*)resp) == 0x83 &&
              XModbusPdu_exceptionCode((const XModbusPdu*)resp) == 0x02,
              "从站回异常帧 83H + 错误码 02（跨功能码组）");
        if (resp) XClassDelete((XClass*)resp);
        XObject_deleteLater((XObject*)srv);
    }
    return g_fail - failBefore;
}

/**
 * @brief 用例⑤：M512 从站侧的遥测/命令语义（进程内模拟器，**不依赖传输层**）。
 * @details 直接查模拟器的保持寄存器（从站侧真值），逐项核对手册的最小单位：
 *          1001H 运行频率(0.01Hz)、1002H 母线电压(0.1V)、1003H 输出电压(1V)、
 *          1004H 电流(0.01A)、1005H 功率(0.1kW)、1006H 转矩(0.1%)、
 *          1007H/1008H DI/DO 位图、1009H~100BH AI(0.01V)、1010H/1011H PID、
 *          1012H PLC、1013H 脉冲频率(0.01kHz)、1019H 线速度、101AH/101BH 时间、
 *          101FH/1020H 主/辅频率、7022H 电机温度、3000H 状态、8000H 故障。
 * @return 失败用例数。
 */
static int caseM512SlaveSemantics(void)
{
    XvfdSlaveEmu em;
    int failBefore = g_fail;

    g_case = "m512SlaveSemantics";
    memset(&em, 0, sizeof(em));
    CHECK(xvfdSlaveEmuCreate(&em, XVFD_EMU_M512, NULL), "M512 从站模拟器上线");
    /* ① 静态遥测项（单位按手册最小单位）。 */
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x1002) == 5400, "1002H 母线电压 540.0V（0.1V 口径）");
    CHECK(xvfdEmuGet(&em, 0x1003) == 380, "1003H 输出电压 380V（1V 口径）");
    CHECK(xvfdEmuGet(&em, 0x1004) == 100, "1004H 输出电流 1.00A（0.01A 口径）");
    CHECK(xvfdEmuGet(&em, 0x1005) == 75, "1005H 输出功率 7.5kW（0.1kW 口径）");
    CHECK(xvfdEmuGet(&em, 0x1006) == 690, "1006H 输出转矩 69.0%（0.1% 口径）");
    CHECK(xvfdEmuGet(&em, 0x1007) == 0x0001, "1007H DI 输入状态位图");
    CHECK(xvfdEmuGet(&em, 0x1009) == 1000 && xvfdEmuGet(&em, 0x100A) == 500 &&
          xvfdEmuGet(&em, 0x100B) == 250, "1009H~100BH AI1~AI3（0.01V 口径）");
    CHECK(xvfdEmuGet(&em, 0x1010) == 50 && xvfdEmuGet(&em, 0x1011) == 48,
          "1010H/1011H PID 设定与反馈");
    CHECK(xvfdEmuGet(&em, 0x1012) == 1, "1012H PLC 阶段");
    CHECK(xvfdEmuGet(&em, 0x1013) == 1000, "1013H PULSE 输入脉冲频率（0.01kHz 口径）");
    CHECK(xvfdEmuGet(&em, 0x1019) == 120, "1019H 线速度（1 m/Min 口径）");
    CHECK(xvfdEmuGet(&em, 0x101A) == 600 && xvfdEmuGet(&em, 0x101B) == 600,
          "101AH/101BH 上电时间与运行时间");
    CHECK(xvfdEmuGet(&em, 0x1020) == 0, "1020H 辅频率 Y");
    CHECK(xvfdEmuGet(&em, 0x7022) == 45, "7022H 电机温度（1℃ 口径）");
    CHECK(xvfdEmuGet(&em, 0x3000) == 3, "3000H 初始状态 = 3（停机）");
    CHECK(xvfdEmuGet(&em, 0x8000) == 0, "8000H 初始无故障");
    /* [2026-11-03 对齐真机 0.1Hz 口径] 模拟器已按实机标定（m_maxFreq=500=
     * 50.0Hz，0.1Hz 定点；同 df7b7dac 的 M512 真机联调口径），本组断言原按
     * 旧 0.01Hz 口径写（5000/2500/-1000），物理频率相同但原始码差 10 倍。 */
    CHECK(xvfdEmuGet(&em, 0xF00A) == 500, "F00AH P0.10 最大频率 50.0Hz（0.1Hz 口径）");

    /* ② 1000H 通讯设定值 → 1001H/101FH 运行频率（P0.10 百分比换算）。 */
    xvfdEmuPut(&em, 0x1000, 5000);              /* +50.00% → 25.0Hz */
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x1001) == 250, "1001H 运行频率跟随 1000H（+50% → 250=25.0Hz）");
    CHECK(xvfdEmuGet(&em, 0x101F) == 250, "101FH 主频率 X 跟随");
    xvfdEmuPut(&em, 0x1000, (uint16_t)(-2000)); /* -20.00% → -10.0Hz */
    xvfdSlaveEmuTick(&em);
    CHECK((int16_t)xvfdEmuGet(&em, 0x1001) == -100,
          "负给定 → 1001H 是 16 位补码（-100=-10.0Hz），不是 0");

    /* ③ 2000H 命令字 → 3000H 状态（命令源=通讯时生效）。 */
    xvfdEmuPut(&em, 0x2000, 1);
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x3000) == 1, "2000H=1 → 3000H=1（正转）");
    xvfdEmuPut(&em, 0x2000, 6);
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x3000) == 3, "2000H=6 → 3000H=3（减速停机）");

    /* ④ 命令源未切通讯（P0.02≠2）→ 命令字被**静默忽略**（手册附录1）。 */
    xvfdEmuPut(&em, 0xF002, 0);
    xvfdEmuPut(&em, 0x2000, 1);
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x3000) == 3, "命令源=面板时 2000H 命令字被静默忽略（状态不变）");
    xvfdEmuPut(&em, 0xF002, 2);

    /* ⑤ 8000H 故障码回写 + 2001H DO 强制 → 1008H 回读。 */
    xvfdEmuM512InjectFault(&em, 9);
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x8000) == 9, "8000H 回写故障码 9");
    xvfdEmuPut(&em, 0x2001, 0x0005);
    xvfdSlaveEmuTick(&em);
    CHECK(xvfdEmuGet(&em, 0x1008) == 0x0005, "2001H DO 强制位图回写 1008H");

    xvfdSlaveEmuDestroy(&em);
    return g_fail - failBefore;
}

#endif /* XVFD_ON && XVFD_M512_ON */

/* ==================== 入口 ==================== */

int XVfdHubLimitsTest_run(void)
{
    printf("=== XVfd 从站级通讯约束单测（Hub 读上限/组边界/只 FC06 + M512 从站侧硬约束）===\n");
    fflush(stdout);
    XCoreApplication_create(0, NULL);
    caseMaxReadRegisters();
    caseRangeBarrier();
    caseSingleWriteOnly();
#if XVFD_ON && XVFD_M512_ON
    caseM512SlaveConstraints();
    caseM512SlaveSemantics();
#endif
    /* 让 deleteLater 的对象真正析构（模拟器 / 受约束从站）。 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}

void XVfdHubLimitsTest(XVariant* data)
{
    const int fails = XVfdHubLimitsTest_run();
    (void)data;
    printf("=== XVfdHubLimitsTest: %s（失败用例 %d）===\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON && XMODBUS_HUB_ON */
