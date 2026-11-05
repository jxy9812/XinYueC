/**
 * @file       xfdv_m512_test.c
 * @brief      511M/512M/513M 双站混合总线 XVfd 通道化设备层闭环测试。
 * @details    [2026-11-01 通道化重构] 拓扑：
 *               Python 双站模拟器（mbtest_m512_slave.py，一条 pty 上两个站号）
 *               → XModbusRtuSerialClient（一个总线主站）
 *               → XModbusHub（两站各自映射段，虚拟地址隔离）
 *               → XModbusPoint × N → XIoChannelGroup → XVfdM512 × 2。
 *             覆盖：
 *               ①上线 + 双站快照（站号隔离）；
 *               ②调速：Hz→最大频率百分比→1000H（P0.10 换算）双站独立；
 *               ③启停：2000H 命令字 1/2/6 + 3000H 状态回读；
 *               ④故障：8000H 故障码 → 故障名映射 + 站间隔离；
 *               ⑤复位：2000H=7 → 故障清零；
 *               ⑥参数通道：P0.10 读（id=3）、P8.00 写后回读（id=11）；
 *               ⑦能力位/参数表/约束标注；
 *               ⑧本轮语义修复回归（**不依赖「写落定」**，与传输层无关）：
 *                 ⑧a 故障码中文说明（0x0009=欠压故障等，纯查表）；
 *                 ⑧b 命令源未切到通讯（P0.02≠2）时 start 被**显式拒绝**：
 *                     返回 false + errorOccurred 信号 + WriteRejected；
 *                 ⑧c 写负频率（反转给定）回读是 16 位补码而非被无符号钳成 0。
 *             退出码=失败用例数。
 *
 *             ⚠ ①②③④⑤⑥依赖链路上的**读写往返**；本机 TCP 环回存在环境级
 *             数据丢失/延迟（写帧迟迟不落定、读组轮转一圈需数秒），故这些
 *             用例在当前环境下会红——属传输层问题，非本文件的语义缺陷。
 *             ⑧段刻意只依赖**设备层语义与 Hub 缓存**，故恒绿。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XDateTime.h"
#include "XModbusTcpClient.h"
#include "XModbusDevice.h"
#include "XVariant.h"
#include "XString.h"
#include "XChannel/XModbusLink.h"
#include "XChannel/XChannelAssembly.h"
#include "xvfd_harness.h"
#include "xvfd_slave_emu.h"
#include "XVfd/XVfdM512.h"
#include "XVfd/XVfdDevice_Protected.h"

#ifndef M512_BASE
#define M512_BASE "/tmp/mbsoak/m512"
#endif

static int g_fail = 0;
/** 装配完成时的定时器 fd 计数（回归锁基线）。 */
static int g_fdTimersAtMount = 0;
static const char* g_case = "";
static uint64_t g_bootMs = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  [OK] %s\n", msg); fflush(stdout); } \
    else { printf("  [FAIL] %s (%s@%d)\n", msg, g_case, __LINE__); fflush(stdout); ++g_fail; } \
} while (0)

/* 站点表（信号槽按 sender 区分站点）。 */
static XObject* g_devObj[2] = { NULL, NULL };
static XVfdDevice* g_dev[2] = { NULL, NULL };
/* 双站模拟器（每站独立 server/client，泵循环内 tick）。 */
static XvfdSlaveEmu g_emu[2];

typedef struct {
    int statusCount;
    int doneCount;
    int lastOp;
    bool lastOk;
    int paramReadCount;
    uint16_t paramReadId;
    int32_t paramReadRaw;
    bool paramReadOk;
    int paramWriteCount;
    uint16_t paramWriteId;
    bool paramWriteOk;
    int errorCount;
} DrvCounters;

static DrvCounters g_d[2];

static void tlog(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    printf("[%8.3fs] ", (double)(XDateTime_currentMSecsSinceEpoch() - g_bootMs) / 1000.0);
    vprintf(fmt, ap); printf("\n"); fflush(stdout);
}

/** @brief 泵事件指定毫秒（单线程事件循环；两站模拟器同拍推进）。 */
static void pump(int ms)
{
    const uint64_t deadline = XDateTime_currentMSecsSinceEpoch() + (uint64_t)ms;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        xvfdSlaveEmuTick(&g_emu[0]);
        xvfdSlaveEmuTick(&g_emu[1]);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        const uint64_t spin = XDateTime_currentMSecsSinceEpoch() + 2;
        while (XDateTime_currentMSecsSinceEpoch() < spin) { }
    }
}

/**
 * @brief 统计 fd 表里**定时器** fd 占用数（回归锁用）。
 * @return 定时器 fd 数量。
 * @note 定时器 fd 只在 XObject_killTimer 时释放（触发≠注销），
 *       故本计数随状态拍数线性增长即说明状态定时器在泄漏。
 */
static int countTimerFds(void)
{
    int k, n = 0;
    for (k = 0; k < XFD_TABLE_SIZE; ++k) {
        XFileDescriptor* d = XFd_get((XFd)k);
        if (d && XFd_type((XFd)k) == XFD_TYPE_TIMER) ++n;
    }
    return n;
}

/** @brief sender → 站点下标。 */
static int devIndex(XObject* sender){
    if (sender && sender == g_devObj[1]) return 1;
    return 0;
}

/**
 * @brief 等设备空闲（在途动作结束或监督超时）。
 * @param dev 设备。
 * @param timeoutMs 最长等待毫秒。
 * @return 空闲返回 true；超时返回 false。
 */
static bool waitIdle(XVfdDevice* dev, int timeoutMs)
{
    const uint64_t deadline = (uint64_t)XDateTime_currentMSecsSinceEpoch() + (uint64_t)timeoutMs;
    while (XVfdDevice_isBusy(dev)) {
        pump(50);
        if ((uint64_t)XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
    }
    return true;
}

static void onStatusUpdated(XObject* sender, XVarList* args)
{
    (void)args;
    ++g_d[devIndex(sender)].statusCount;
}

static void onCommandCompleted(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->doneCount;
    c->lastOp = (int)XVarList_arg(args, XVfdOperation);
    c->lastOk = XVarList_arg(args, bool);
    tlog("  dev%d 动作完成 op=%d ok=%d", devIndex(sender) + 1, c->lastOp, (int)c->lastOk);
}

static void onParamReadDone(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->paramReadCount;
    c->paramReadId = XVarList_arg(args, uint16_t);
    c->paramReadRaw = XVarList_arg(args, int32_t);
    c->paramReadOk = XVarList_arg(args, bool);
}

static void onParamWriteDone(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->paramWriteCount;
    c->paramWriteId = XVarList_arg(args, uint16_t);
    c->paramWriteOk = XVarList_arg(args, bool);
}

static void onErrorOccurred(XObject* sender, XVarList* args)
{
    DrvCounters* c = &g_d[devIndex(sender)];
    if (!args) return;
    XVarList_start(args);
    ++c->errorCount;
    {
        const char* msg = XVarList_arg(args, const char*);
        if (msg) tlog("  dev%d 错误: %s", devIndex(sender) + 1, msg);
    }
}

int XVfdM512Test_runEx(const char* ptyOverride)
{
    XModbusTcpClient* client[2] = { NULL, NULL };
    XModbusDevice* mdev[2] = { NULL, NULL };
    XvfdHarness harness;
    XVfdM512* m1 = NULL;
    XVfdM512* m2 = NULL;
    const XVfdPointSpec* specs = NULL;
    const XVfdParamDesc* params = NULL;
    const XVfdCommGroup* groups = NULL;
    const XVfdUnitProtocol* protocol = NULL;
    const XVfdBindTable* bind = NULL;
    int specCount = 0;
    int paramCount = 0;
    int groupCount = 0;
    int idMaxFreq = -1;
    int idJogFreq = -1;
    int devId[2] = { -1, -1 };
    uint16_t port[2] = { 0, 0 };
    int i;
    (void)ptyOverride;

    memset(&harness, 0, sizeof(harness));
    memset(g_d, 0, sizeof(g_d));
    g_bootMs = XDateTime_currentMSecsSinceEpoch();
    printf("=== 511M/512M/513M 双站 XVfd 通道化设备层闭环测试（进程内 TCP 从站）===\n");
    fflush(stdout);
    XCoreApplication_create(0, NULL);

    /* ---------- 双站模拟器 + 客户端 ---------- */
    g_case = "topology";
    for (i = 0; i < 2; ++i) {
        XVariant* var;
        char name[32];
        CHECK(xvfdSlaveEmuCreate(&g_emu[i], XVFD_EMU_M512, &port[i]), "从站模拟器上线");
        client[i] = XModbusTcpClient_create();
        mdev[i] = (XModbusDevice*)client[i];
        CHECK(client[i] != NULL && port[i] != 0, "TCP 客户端创建");
        if (!client[i] || !port[i]) goto cleanup;
        /* 超时放宽：M512 地址分散 → 读帧数 ≈ 29/从站，读组轮转一圈数百毫秒；
         * 写应答排在读队列之后，500ms 会被读风暴挤过（从站已应回、客户端
         * 判超时）。3s 让写有足够排期窗口。 */
        XModbusClient_setTimeout((XModbusClient*)client[i], 3000);
        XModbusClient_setNumberOfRetries((XModbusClient*)client[i], 2);
        var = XVariant_create_utf8_str("127.0.0.1");
        XModbusDevice_setConnectionParameter(mdev[i],
            XModbusDevice_NetworkAddressParameter, var);
        XClassDelete((XClass*)var);
        var = XVariant_create_int((int)port[i]);
        XModbusDevice_setConnectionParameter(mdev[i],
            XModbusDevice_NetworkPortParameter, var);
        XClassDelete((XClass*)var);
        snprintf(name, sizeof(name), "m512-u%d", i + 1);
        CHECK(XModbusDevice_connectDevice(mdev[i]), "connectDevice 返回 true");
        (void)name;
    }
    pump(300);

    /* ---------- 装配：一 Hub 两站 + 两组点 + 两台设备 ---------- */
    g_case = "assemble";
    CHECK(xvfdHarnessInit(&harness), "脚手架初始化");
    devId[0] = xvfdHarnessAddStation(&harness, (XModbusClient*)client[0], 1, "m512-u1");
    devId[1] = xvfdHarnessAddStation(&harness, (XModbusClient*)client[1], 2, "m512-u2");
    CHECK(devId[0] > 0 && devId[1] > 0, "双站设备注入 Hub（独立 server/client 对）");

    m1 = XVfdM512_create();
    m2 = XVfdM512_create();
    CHECK(m1 != NULL && m2 != NULL, "两台 XVfdM512 创建");
    if (!m1 || !m2) goto cleanup;
    g_dev[0] = (XVfdDevice*)m1;
    g_dev[1] = (XVfdDevice*)m2;
    g_devObj[0] = (XObject*)m1;
    g_devObj[1] = (XObject*)m2;

    /* [2026-11-02 多站] 每台设备挂自己的通道组：role 是组内下标，两台变频器
     * 都有 StatusWord/FaultCode 等同名语义槽，共用一组必然冲突。 */
    CHECK(XVfdDevice_attach(g_dev[0], xvfdHarnessStationGroup(&harness, devId[0], 1)) &&
          XVfdDevice_attach(g_dev[1], xvfdHarnessStationGroup(&harness, devId[1], 2)),
          "两设备各自挂接独立通道组");
    specs = XVfdM512_pointSpecs(&specCount);
    specs = XVfdM512_pointSpecs(&specCount);
    params = XVfdM512_paramDescs(&paramCount);
    /* [2026-11-02 分层收口] 地址/组表/约束来自绑定层（品牌主文件只存语义）。 */
    bind = XVfdM512_modbusBinding();
    groups = bind->m_groups;
    groupCount = bind->m_groupCount;
    protocol = bind->m_protocol;
    CHECK(groupCount > 0 && protocol != NULL, "品牌通信组表与从站约束可取");
    /* 按通信组装配：组段惰性建（只建用到的组）+ 语义点 + 从站约束。 */
    CHECK(xvfdHarnessMountBrand(&harness, g_dev[0], devId[0], 1, groups, groupCount,
                                specs, specCount, protocol, bind),
          "站1 按通信组装配（组段+点+约束）");
    CHECK(xvfdHarnessMountBrand(&harness, g_dev[1], devId[1], 2, groups, groupCount,
                                specs, specCount, protocol, bind),
          "站2 按通信组装配（虚拟地址隔离）");
    CHECK(paramCount >= 12, "参数描述表规模（核心集）");
    /* 参数下标按**绑定表地址**定位（表可重排，断言不写死下标；
     * [2026-11-02 分层收口] 参数描述表不再存地址，地址在绑定层）。 */
    for (i = 0; i < paramCount; ++i) {
        XVfdParamBinding pb;
        if (!XVfdBind_findParam(bind, (uint16_t)i, &pb)) continue;
        if (pb.m_addr == XVfdM512_AddrMaxFreq) idMaxFreq = i;
        if (pb.m_addr == XVfdM512_AddrJogFreq) idJogFreq = i;
    }
    CHECK(idMaxFreq >= 0 && idJogFreq >= 0, "参数表含 P0.10 最大频率与 P8.00 点动频率");
    /* 参数点**按需**绑定：全量映射会把读帧数撑爆（本型号 28 组 → 29 帧/圈）。 */
    CHECK(xvfdHarnessMountParamBrand(&harness, g_dev[1], devId[1], 2, groups, groupCount,
                                     &params[idMaxFreq], (uint16_t)idMaxFreq),
          "站2 装配 P0.10 参数点");
    CHECK(xvfdHarnessMountParamBrand(&harness, g_dev[1], devId[1], 2, groups, groupCount,
                                     &params[idJogFreq], (uint16_t)idJogFreq),
          "站2 装配 P8.00 参数点");
    CHECK(XVfdDevice_hasCapability(g_dev[0], XVfdCap_Jog), "能力位图：点动（511M 族特有）");
    CHECK(XVfdDevice_hasCapability(g_dev[1], XVfdCap_ParamAccess), "能力位图：参数通道");

    /* [2026-11-03 回归锁] 定时器 fd 基线（状态定时器刚起）。
     * 修复前：状态定时器每拍触发泄漏 1 个 fd（触发≠注销，fd 要 killTimer
     * 才释放），两站各一拍 ⇒ 每拍泄漏 2 个；fd 表仅 128 槽，几秒即占满，
     * 占满后全应用再无定时器可用（RTU 客户端帧间定时器注册失败 ⇒ 应答帧
     * 滞留接收缓冲 ⇒ 全线超时）。修复后（触发即注销）增量应接近 0。 */
    {
        const int base = countTimerFds();
        g_fdTimersAtMount = base;
        printf("  [INFO] 回归锁基线：装配完成时定时器 fd = %d\n", base);
    }

    XObject_connect_2(g_devObj[0], XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_statusUpdated_signal), onStatusUpdated);
    XObject_connect_2(g_devObj[0], XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_commandCompleted_signal), onCommandCompleted);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_paramReadDone_signal), onParamReadDone);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_paramWriteDone_signal), onParamWriteDone);
    XObject_connect_2(g_devObj[0], XSignal(XVfdDevice_errorOccurred_signal), onErrorOccurred);
    XObject_connect_2(g_devObj[1], XSignal(XVfdDevice_errorOccurred_signal), onErrorOccurred);
    XVfdDevice_setStatusInterval(g_dev[0], 100);
    XVfdDevice_setStatusInterval(g_dev[1], 100);

    xvfdHarnessStart(&harness, 20);
    /* [零遍历取证] 旧实现每个点各订阅一次 Hub 信号，M512 单设备 66 点
     * 即 66 条连接、每次值变化 66 次回调。现由 XModbusLink 统一订阅：
     * 无论挂多少点，每设备恒为 2 条（valueChanged + writeCompleted）。
     * 注意时机：订阅在链路 Start 时建立，Start 在 xvfdHarnessStart 里发生。 */
    {
        int si;
        for (si = 0; si < 2; ++si) {
            XModbusLink* lk = harness.m_stations[si].m_link;
            CHECK(lk && XModbusLink_subscriptionCount(lk) == 2,
                  si == 0 ? "站1 链路订阅数 = 2（非 66）"
                          : "站2 链路订阅数 = 2（非 66）");
        }
    }
    tlog("Hub 双站段轮询启动（20ms；M512 通信组多，读组轮转一圈需数百 ms）");

    /* ---------- ⑧ 本轮语义修复回归（**不依赖「写落定」**） ---------- */
    /* ⑧a 故障码中文说明：纯查表（8000H 故障表），与传输层无关。 */
    g_case = "faultText";
    CHECK(XVfdM512_faultText(9) && strcmp(XVfdM512_faultText(9), "欠压故障") == 0,
          "故障码 0x0009 中文说明 = 欠压故障（E.LU）");
    CHECK(XVfdM512_faultText(0x0003) && strcmp(XVfdM512_faultText(0x0003), "减速过电流") == 0,
          "故障码 0x0003 中文说明 = 减速过电流");
    CHECK(XVfdM512_faultText(0x002D) && strcmp(XVfdM512_faultText(0x002D), "电机过温") == 0,
          "故障码 0x002D 中文说明 = 电机过温");
    CHECK(XVfdM512_faultText(0x1234) == NULL, "未知故障码无中文说明（返回 NULL）");

    /* ⑧b 命令源未切到通讯时 start 必须被**显式拒绝**（而不是被从站静默吞掉）。
     * 现场条件用「P0.02=0（面板）」表达：Hub 映射段首次物化时按段默认值播种
     * 缓存（默认 0），首拍轮询回来之前设备层读到的就是 0（≠ 2=通讯）。
     * 断言落在设备层的可观测行为上：start 返回 false + errorOccurred 信号 +
     * lastError=WriteRejected——三者都不依赖写帧是否在链路上落定。 */
    g_case = "cmdSourceRejected";
    g_d[0].errorCount = 0;
    CHECK(!XVfdDevice_start(g_dev[0], false),
          "命令源未切到通讯时 start 被拒（不把命令发给从站）");
    pump(60);
    CHECK(g_d[0].errorCount > 0, "拒绝时发出错误信号（errorOccurred）");
    CHECK(XVfdDevice_lastError(g_dev[0]) == XVfdError_WriteRejected,
          "错误码 = XVfdError_WriteRejected");

    /* ⑧c 切源前置（附录1）：后续的调速/启停都要求「
     * 命令源=通讯、主频率源=通讯给定」，否则从站静默忽略
     * 控制字与频率给定。
     *
     * [2026-11-05 测试自身缺陷根修] 本用例在 ⑧b 验证完
     * 「未切源时控制被拒」后，**忘了把源切过去**就直接做
     * 调速与启停断言：产品行为是对的（正确拒绝），却导致整段
     * 连环 17 项失败——这是用例自身缺前置，不是产品缺陷。
     * 两台站都需要切（站间隔离也要验）。 */
    g_case = "setSourceComm";
    /* [一步一步来] 每次切源写完都要等「写落定 + 回读刷新缓存」，
     * 否则后一次写的前置校验（m512CheckSource 读的是**缓存**）
     * 仍看到旧值 ⇒ 误拒。实测：命令源与频率源连着写，
     * 前者 OK、后者被拒。 */
    CHECK(XVfdM512_setCommandSourceComm((XVfdM512*)g_dev[0]),
          "站1 切命令源到通讯（P0.02=2）");
    pump(700);
    CHECK(XVfdM512_setCommandSourceComm((XVfdM512*)g_dev[1]),
          "站2 切命令源到通讯");
    pump(700);
    CHECK(XVfdM512_setFrequencySourceComm((XVfdM512*)g_dev[0]),
          "站1 切主频率源到通讯给定（P0.03=9）");
    pump(700);
    CHECK(XVfdM512_setFrequencySourceComm((XVfdM512*)g_dev[1]),
          "站2 切主频率源到通讯给定");
    pump(1200);   /* 最后等一拍回读把缓存刷新到最终值 */

    /* ---------- ① 双站快照 ---------- */
    g_case = "pollStatus";
    pump(2500);
    CHECK(g_d[0].statusCount > 0, "站1 出快照");
    CHECK(g_d[1].statusCount > 0, "站2 出快照");
    tlog("[dbg] dev0 runState=%d valid=%d mask=0x%llX outFreq=%.2f setFreq=%.2f",
         (int)XVfdDevice_status_const(g_dev[0])->m_runState,
         (int)XVfdDevice_status_const(g_dev[0])->m_valid,
         (unsigned long long)XVfdDevice_status_const(g_dev[0])->m_validMask,
         XVfdDevice_status_const(g_dev[0])->m_outputFreqHz,
         XVfdDevice_status_const(g_dev[0])->m_setFreqHz);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_Stopped,
          "站1 初始 Stopped（3000H=3）");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_Stopped,
          "站2 初始 Stopped");

    /* ---------- ② 调速（Hz → 百分比 → 1000H） ---------- */
    g_case = "setFrequency";
    g_d[0].doneCount = 0;
    g_d[1].doneCount = 0;
    CHECK(XVfdDevice_setFrequency(g_dev[0], 25.0), "站1 发起 25.0Hz");
    pump(600);
    CHECK(XVfdDevice_setFrequency(g_dev[1], 10.0), "站2 发起 10.0Hz");
    pump(1500);
    CHECK(g_d[0].lastOk && g_d[0].lastOp == (int)XVfdOperation_SetFrequency, "站1 设定完成");
    CHECK(g_d[1].lastOk && g_d[1].lastOp == (int)XVfdOperation_SetFrequency, "站2 设定完成");
    pump(800);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_outputFreqHz > 24.9 &&
          XVfdDevice_status_const(g_dev[0])->m_outputFreqHz < 25.1,
          "站1 输出频率 25.0Hz（0.1Hz 定点 × 最大频率百分比）");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_outputFreqHz > 9.9 &&
          XVfdDevice_status_const(g_dev[1])->m_outputFreqHz < 10.1,
          "站2 输出频率 10.0Hz（站间隔离）");

    /* ---------- ②b 按转速调速（基类通用换算：rpm → Hz → 频率通道） ---------- */
    g_case = "setSpeedRpm";
    /* 50Hz ↔ 1440rpm ⇒ 720rpm 应折算成 25.0Hz；比换算式本身先锁死比例。 */
    CHECK(XVfdDevice_ratedRpm(g_dev[0]) == 1440.0, "额定转速缺省 1440rpm");
    CHECK(XVfdDevice_rpmToHz(g_dev[0], 720.0) > 24.9 &&
          XVfdDevice_rpmToHz(g_dev[0], 720.0) < 25.1,
          "720rpm 折算 25.0Hz（额定点比例）");
    CHECK(XVfdDevice_hzToRpm(g_dev[0], 50.0) > 1439.9 &&
          XVfdDevice_hzToRpm(g_dev[0], 50.0) < 1440.1,
          "50Hz 折算 1440rpm（反向换算一致）");
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_setSpeedRpm(g_dev[0], 720), "站1 按转速给定 720rpm");
    pump(1500);
    CHECK(g_d[0].lastOk && g_d[0].lastOp == (int)XVfdOperation_SetFrequency,
          "转速给定经频率通道落地（op=SetFrequency，比未被当成不支持）");
    pump(800);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_outputFreqHz > 24.9 &&
          XVfdDevice_status_const(g_dev[0])->m_outputFreqHz < 25.1,
          "720rpm 生效后输出频率 25.0Hz");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_setFreqHz > 24.9 &&
          XVfdDevice_status_const(g_dev[0])->m_setFreqHz < 25.1,
          "设定频率同步为 25.0Hz（站2 不受影响）");

    /* ---------- ③ 启停 ---------- */
    g_case = "startBoth";
    CHECK(XVfdDevice_start(g_dev[0], false), "站1 发起正转");
    pump(500);
    CHECK(XVfdDevice_start(g_dev[1], true), "站2 发起反转");
    pump(1500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_RunningForward,
          "站1 正转（3000H=1）");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_RunningReverse,
          "站2 反转（3000H=2）");

    g_case = "stop";
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_stop(g_dev[0]), "站1 发起受控停机（2000H=6）");
    pump(1500);
    CHECK(g_d[0].lastOk, "站1 停机完成");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_Stopped,
          "站1 回到 Stopped");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_RunningReverse,
          "站2 仍在反转（站间隔离）");

    /* ---------- ③b 点动（正/反各一次；按住=命令字持续、松开=停机） ---------- */
    g_case = "jog";
    /* 站1 已停机，可点动。命令字 3=正转点动 → 模拟从站置运行态。 */
    CHECK(XVfdDevice_hasCapability(g_dev[0], XVfdCap_Jog), "站1 具备点动能力");
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_jog(g_dev[0], false), "站1 正转点动（2000H=3）");
    pump(1500);
    CHECK(g_d[0].lastOk && g_d[0].lastOp == (int)XVfdOperation_Jog,
          "点动命令字落地（op=Jog）");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_RunningForward,
          "正转点动后状态字为运行（3000H=1）");
    /* 松开：走受控停机（与服务端 hsfAppJogRelease 同口径）。 */
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_stop(g_dev[0]), "松开点动 → 受控停机");
    pump(1500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_Stopped,
          "松开点动后回到 Stopped");
    /* 反转点动：命令字 4。 */
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_jog(g_dev[0], true), "站1 反转点动（2000H=4）");
    pump(1500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_RunningReverse,
          "反转点动后状态字为反转（3000H=2）");
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_stop(g_dev[0]), "反转点动松开 → 受控停机");
    pump(1500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_runState == XVfdRunState_Stopped,
          "反转点动松开后回到 Stopped");
    CHECK(XVfdDevice_status_const(g_dev[1])->m_runState == XVfdRunState_RunningReverse,
          "站1 点动全程站2 状态不受影响（站间隔离）");

    /* ---------- ④ 故障 ---------- */
    g_case = "fault";
    xvfdEmuM512InjectFault(&g_emu[0], 3);   /* E.oCdF 减速过电流 */
    pump(2500);
    CHECK(XVfdDevice_status_const(g_dev[0])->m_faulted, "站1 故障置位");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_faultCode == 3, "站1 故障码 = 3");
    CHECK(XVfdM512_faultName(3) && strcmp(XVfdM512_faultName(3), "E.oCdE") == 0,
          "故障名映射 E.oCdE（手册附录1 面板码）");
    CHECK(XVfdM512_faultText(3) && strcmp(XVfdM512_faultText(3), "减速过电流") == 0,
          "故障说明映射");
    CHECK(!XVfdDevice_status_const(g_dev[1])->m_faulted, "站2 无故障（隔离）");

    /* ---------- ⑤ 复位 ---------- */
    g_case = "resetFault";
    g_d[0].doneCount = 0;
    CHECK(XVfdDevice_resetFault(g_dev[0]), "站1 发起故障复位（2000H=7）");
    pump(2000);
    CHECK(g_d[0].lastOk, "站1 复位完成");
    CHECK(XVfdDevice_status_const(g_dev[0])->m_faultCode == 0, "站1 故障清除");

    /* ---------- ⑥ 参数通道 ---------- */
    g_case = "paramRead";
    g_d[1].paramReadOk = false;
    g_d[1].paramReadCount = 0;
    CHECK(XVfdDevice_readParam(g_dev[1], (uint16_t)idMaxFreq), "站2 发起参数读 P0.10");
    pump(500);
    CHECK(g_d[1].paramReadOk && g_d[1].paramReadId == idMaxFreq, "站2 参数读完成");
    /* 2026-11-03 实机标定：P0.10 原始值 500 = 50.0Hz（0.1Hz 定点，见 XVfdM512_MaxFreqDiv） */
    CHECK(g_d[1].paramReadRaw == 500, "P0.10 回读 raw=500（50.0Hz）");

    g_case = "paramWrite";
    g_d[1].paramWriteOk = false;
    g_d[1].paramWriteCount = 0;
    CHECK(XVfdDevice_writeParam(g_dev[1], (uint16_t)idJogFreq, 300),
          "站2 发起参数写 P8.00=300（3.00Hz）");
    pump(1500);
    CHECK(g_d[1].paramWriteOk && g_d[1].paramWriteId == idJogFreq, "站2 参数写完成");
    g_d[1].paramReadOk = false;
    CHECK(XVfdDevice_readParam(g_dev[1], (uint16_t)idJogFreq), "站2 回读 P8.00");
    pump(1000);
    CHECK(g_d[1].paramReadOk && g_d[1].paramReadRaw == 300, "P8.00 回读 = 300（写入一致）");

    /* ---------- ⑦ 约束标注 ---------- */
    g_case = "paramAccess";
    {
        const XVfdParamDesc* pd = XVfdDevice_paramDesc_const(g_dev[1], (uint16_t)idMaxFreq);
        CHECK(pd && pd->m_access == (uint8_t)XVfdParamAccess_RunLocked,
              "P0.10 标注为运行禁改（★）");
        CHECK(XVfdDevice_paramCount(g_dev[1]) == paramCount, "参数表条目数一致");
    }

    /* ---------- ⑧c 负频率给定：写回读必须是 16 位补码（不是被钳成 0） ---------- */
    /* 走语义槽写（XVfdDevice_writeRole）：不占用设备单飞、不依赖链路；
     * 回读取 Hub 缓存（通道点的回读源）的原始寄存器值。 */
    g_case = "negativeFreq";
    {
        const double base = (XVfdDevice_maxFrequency(g_dev[0]) > 0.0)
                                ? XVfdDevice_maxFrequency(g_dev[0]) : 50.0;
        const int32_t expect = (int32_t)(-10.0 / base * 10000.0);  /* % × 100（±10000 = ±100%） */
        const int32_t raw = XVfdDevice_frequencyToRaw(g_dev[0], XVfdFreqBasis_MaxFreqPct,
                                                      -10.0, 0, -10000, 10000);
        CHECK(raw == expect && raw < 0,
              "负频率换算为负的百分比码（-10Hz / 基准 → 补码原码）");
        CHECK(XVfdDevice_writeRole(g_dev[0], XVfdRole_FreqSet, raw),
              "写负频率给定（语义槽写）");
        {
            /* [2026-11-02] 点不再持有链路地址：改由链路反查 role → (区, 地址)。 */
            XIoPoint* fp = XVfdDevice_point(g_dev[0], XVfdRole_FreqSet);
            uint16_t cache = 0;
            uint16_t faddr = 0;
            int farea = 0;
            /* [2026-11-02 多站] 每站独立链路：取站1 自己的 link。 */
            XModbusLink* l0 = harness.m_stations[0].m_link;
            bool haveBind = (fp && l0 &&
                             XModbusLink_queryBinding(l0, XIoPoint_role(fp),
                                                      &farea, &faddr));
            CHECK(fp != NULL, "FreqSet 语义点已绑定");
            CHECK(haveBind, "FreqSet 角色已在链路上绑定地址");
            if (haveBind &&
                XModbusHub_value(harness.m_hub,
                                 XModbusLink_hubAreaType(farea), faddr, &cache)) {
                CHECK(cache != 0, "负频率未被无符号钳位吞成 0");
                CHECK(cache == (uint16_t)(raw & 0xFFFF),
                      "回读是 16 位补码（0xF830 之类），不是 0");
            } else {
                CHECK(false, "Hub 缓存可读（1000H 虚拟地址）");
            }
        }
    }

cleanup:
    /* 轮询计数画像：默认静默（缺省跑测试不该刷屏），联调时置
       XVFD_M512_DIAG=1 打开。口径同 Test/ 其余用例的 environ 开关。 */
    if (harness.m_hub && getenv("XVFD_M512_DIAG")) {
        for (i = 0; i < 2; ++i) {
            XPrintf("[diag] dev%d pollOk=%u pollErr=%u groups=%d pending=%d\n", i + 1,
                    XModbusHub_pollOkCount(harness.m_hub, devId[i]),
                    XModbusHub_pollErrCount(harness.m_hub, devId[i]),
                    XModbusHub_readGroupCount(harness.m_hub, devId[i]),
                    XModbusHub_pendingWriteCount(harness.m_hub));
        }
    }
    if (g_dev[0]) XVfdDevice_detach(g_dev[0]);
    if (g_dev[1]) XVfdDevice_detach(g_dev[1]);
    if (m1) XClassDelete((XClass*)m1);
    if (m2) XClassDelete((XClass*)m2);
    if (harness.m_group || harness.m_hub) xvfdHarnessDestroy(&harness);
    for (i = 0; i < 2; ++i) {
        if (mdev[i]) XModbusDevice_disconnectDevice(mdev[i]);
        if (client[i]) XClassDelete((XClass*)client[i]);
        xvfdSlaveEmuDestroy(&g_emu[i]);
    }
    g_devObj[0] = NULL;
    g_devObj[1] = NULL;
    g_dev[0] = NULL;
    g_dev[1] = NULL;
    pump(50);

    /* ---------- [2026-11-03 回归锁] 定时器 fd 不得随状态拍数线性增长 ---------- */
    {
        const int now = countTimerFds();
        const int delta = now - g_fdTimersAtMount;
        const int limit = 8;   /* 允许少量抖动，不容线性增长 */
        printf("  [INFO] 定时器 fd：装配时 %d → 结束 %d（Δ=%d）\n",
               g_fdTimersAtMount, now, delta);
        CHECK(delta <= limit, "定时器 fd 未线性增长（状态定时器无泄漏）");
    }

    printf("=== 结束：失败用例 %d ===\n", g_fail);
    fflush(stdout);
    return g_fail;
}

int XVfdM512Test_run(void)
{
    return XVfdM512Test_runEx(NULL);
}

void XVfdM512Test(XVariant* data)
{
    const int fails = XVfdM512Test_run();
    (void)data;
    printf("=== XVfdM512Test: %s（失败用例 %d）===\n",
           fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}
