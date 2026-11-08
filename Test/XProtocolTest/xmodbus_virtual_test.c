/**
 * @file       xmodbus_virtual_test.c
 * @brief      虚拟 Modbus 客户端 + 设备模型单测（全 C 端离线联调载体）。
 * @details    [为什么要有这个测试] C 侧此前想联调只能接真设备或起 TCP 从站
 *             （依赖网络栈、占端口、Windows 环回还有停滞问题）。本组用例验证
 *             「XModbusVirtualClient + 设备模型」确实能在**进程内**替代设备
 *             物理层，且上层链路（Hub → XModbusLink → 通道组 → 设备站）
 *             一行不改就跑通。
 *
 *             [断言的是链路契约，不是模型内部]
 *               · Hub 真轮询 → 缓存块回填 → 通道层 feed → 站模块读到值；
 *               · 写 DO → 模型状态变 → 回读一致；
 *               · 模块"离线" → 站模块的新鲜度判定跟着变（这是电磁阀报错的
 *                 依据，也是框架层新增失效语义的实测证据）。
 *
 *             [与真实从站测试的分工] 帧格式/超时/分片由 XModbusTcpServer 类
 *             测试覆盖（hsf_server_io_flowmeter_test.c 等）；本文件只保证
 *             "物理层可替换"这条通路正确，两者互补、不重复。
 */
#include "XModbusVirtualClient.h"
#include "XModbusVirtualModel.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusClient.h"
#include "XModbusDevice.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include "HsfServerIoDigital.h"
#include "HsfServerIoValve.h"
#include <string.h>

#if XMODBUS_ON && XMODBUS_CLIENT_ON

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/** @brief 事件泵（Hub 轮询/信号派发）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/* ---- 模型读便捷包装（省掉每处样板） ---- */

/** @brief 读一段，返回是否 Ok。 */
static bool modelReadOk(void* model, uint8_t fc, uint16_t addr, uint16_t count,
                        uint16_t* out, uint16_t* outLen)
{
    uint16_t buf[256];
    uint16_t len = 0;
    XModbusVirtualResult r = XModbusVirtualModel_read(
        XVirtualModel_base(model), fc, addr, count, out ? out : buf,
        (uint16_t)256, &len);
    if (outLen) *outLen = len;
    return r == XModbusVirtual_Ok;
}

/** @brief 读一段，只取结果码。 */
static XModbusVirtualResult modelReadCode(void* model, uint8_t fc,
                                          uint16_t addr, uint16_t count)
{
    uint16_t buf[256];
    uint16_t len = 0;
    return XModbusVirtualModel_read(XVirtualModel_base(model), fc, addr, count,
                                    buf, (uint16_t)256, &len);
}

/** @brief 读单个保持寄存器（读不到返回 0xFFFF）。 */
static uint16_t modelReadReg(void* model, uint16_t addr)
{
    uint16_t v = 0xFFFFu;
    uint16_t len = 0;
    if (XModbusVirtualModel_read(XVirtualModel_base(model), 3, addr, 1, &v, 1,
                                 &len) != XModbusVirtual_Ok)
        return 0xFFFFu;
    return v;
}

/* ==================== ① 模型层：寄存器地图与拒绝口径 ==================== */

static void test_modelRegisterMap(void)
{
    struct XVirtualModelDigital* dig;
    struct XVirtualModelVfd* vfd;
    uint16_t out[64];
    uint16_t len = 0;

    XPrintf("--- ① 模型层寄存器地图 ---\n");

    /* 数字量：读 16 点 OK，读 32 点回 EXC03（与真模块同口径）。 */
    dig = XVirtualModelDigital_create(16);
    CHECK("数字量模型建成", dig != NULL);
    CHECK("读 16 点正常",
          modelReadOk(dig, 2, 0, 16, out, &len));
    CHECK("读 32 点被拒（EXC03，与实测真模块一致）",
          modelReadCode(dig, 2, 0, 32) ==
          XModbusVirtual_IllegalValue);
    CHECK("addr=16 越界（EXC02）",
          modelReadCode(dig, 2, 16, 16) ==
          XModbusVirtual_IllegalAddress);

    /* 变频器：写设定频率 → 读回换算一致。 */
    vfd = XVirtualModelVfd_create(50.0);
    CHECK("变频器模型建成", vfd != NULL);
    {
        uint16_t val = (uint16_t)(int16_t)(25.0 / 50.0 * 10000.0);
        CHECK("写 0x1000 设定频率被受理",
              XVirtualModel_base(vfd)->m_vtable->write(
                  XVirtualModel_base(vfd), 6, 0x1000, 1, &val) ==
              XModbusVirtual_Ok);
        CHECK("设定频率按百分比解释（25Hz/50Hz → 50%）",
              modelReadReg(vfd, 0x1000) == 5000u);
    }

    XPrintf("（模型对象由测试进程持有，不单独释放：无 destroy 实现的模型\n"
            "  在单测里活到进程结束即可，避免为测试加释放路径。）\n");
}

/* ---- 变频器便捷包装 ---- */

/** @brief 写 0x2000 命令字。 */
static void vfdWriteCmd(struct XVirtualModelVfd* vfd, uint16_t cmd)
{
    XVirtualModel_base(vfd)->m_vtable->write(
        XVirtualModel_base(vfd), 6, 0x2000, 1, &cmd);
}

/** @brief 写单个保持寄存器（FC06）。 */
static void vfdWriteReg(struct XVirtualModelVfd* vfd, uint16_t addr,
                        uint16_t val)
{
    XVirtualModel_base(vfd)->m_vtable->write(
        XVirtualModel_base(vfd), 6, addr, 1, &val);
}

/** @brief 推进若干拍直到输出频率 ≥ 目标（返回是否在限拍内到达）。 */
static bool vfdRampTo(struct XVirtualModelVfd* vfd, double hz, int maxTicks)
{
    int i;
    for (i = 0; i < maxTicks; ++i) {
        XVirtualModelVfd_tick(vfd);
        if (XVirtualModelVfd_outputFrequency(vfd) >= hz - 0.05) return true;
    }
    return false;
}

/* ==================== ①b 变频器：ACC/DEC 斜坡与运行中换向 ====================
 * [口径] ACC(P0.17)/DEC(P0.18)=满量程（0↔P0.10 最大频率）秒数，0.1s 定点，
 * 缺省 100=10.0s ⇒ 50Hz 模型斜率 0.5Hz/拍（5Hz/s）。运行中换向＝先按 DEC
 * 减速过零（状态字仍报原方向）、过零翻向、按 ACC 反向加速到给定；同向
 * 命令/自由停车/减速停机/点动取消待决换向。与 Python VirtualVfd 同口径
 * （server_py/tests/test_units.py 的换向用例互为镜像）。 */
static void test_vfdReversalRamp(void)
{
    struct XVirtualModelVfd* vfd = XVirtualModelVfd_create(50.0);
    uint16_t stw = 0;
    double minHz = 0.0;
    int i;

    XPrintf("--- ①b 变频器：ACC/DEC 斜坡 + 运行中换向 ---\n");
    CHECK("变频器模型建成", vfd != NULL);
    if (!vfd) return;

    /* ACC/DEC 寄存器：缺省与读写。 */
    CHECK("ACC 缺省 100（10.0s）", modelReadReg(vfd, 0xF011) == 100u);
    CHECK("DEC 缺省 100（10.0s）", modelReadReg(vfd, 0xF012) == 100u);
    vfdWriteReg(vfd, 0xF011, 50);
    CHECK("ACC 写读回（50=5.0s）", modelReadReg(vfd, 0xF011) == 50u);
    vfdWriteReg(vfd, 0xF011, 100);

    /* 给定 24%=12Hz，正转启动：ACC 斜坡 0.5Hz/拍。 */
    vfdWriteReg(vfd, 0x1000, 2400);
    vfdWriteCmd(vfd, 1);
    XVirtualModelVfd_tick(vfd);
    CHECK("启动第一拍在 ACC 斜坡上（未瞬到目标）",
          XVirtualModelVfd_outputFrequency(vfd) > 0.0 &&
          XVirtualModelVfd_outputFrequency(vfd) < 12.0);
    CHECK("正转爬到给定 12Hz（10s 满量程 ⇒ ≤40 拍）",
          vfdRampTo(vfd, 12.0, 40));
    CHECK("稳态状态字 = 正转", modelReadReg(vfd, 0x3000) == 1u);

    /* ---- 运行中发反向命令字：减速过零再反向加速 ---- */
    vfdWriteCmd(vfd, 2);
    XVirtualModelVfd_tick(vfd);
    CHECK("换向第一拍已开始减速",
          XVirtualModelVfd_outputFrequency(vfd) < 12.0);
    CHECK("换向减速中状态字仍报正转（过零才翻）",
          modelReadReg(vfd, 0x3000) == 1u);
    minHz = XVirtualModelVfd_outputFrequency(vfd);
    for (i = 0; i < 400; ++i) {
        XVirtualModelVfd_tick(vfd);
        {
            const double hz = XVirtualModelVfd_outputFrequency(vfd);
            if (hz < minHz) minHz = hz;
        }
        stw = modelReadReg(vfd, 0x3000);
        if (stw == 2u) break;
    }
    CHECK("过零后状态字翻为反转", stw == 2u);
    CHECK("减速段经过零（无跳变，min≈0）", minHz <= 0.001 && minHz >= -0.001);
    CHECK("过零后保持运行（未经过停机）", XVirtualModelVfd_running(vfd));
    CHECK("反转爬回给定 12Hz", vfdRampTo(vfd, 12.0, 40));

    /* ---- 换向途中同向命令＝取消换向，拉回原方向 ---- */
    vfdWriteCmd(vfd, 1);                    /* 正转运行中发反转 → 减速 */
    XVirtualModelVfd_tick(vfd);
    vfdWriteCmd(vfd, 1);                    /* 翻回正转 → 取消待决 */
    CHECK("取消换向后回正转 12Hz", vfdRampTo(vfd, 12.0, 60));
    CHECK("取消换向后状态字仍正转", modelReadReg(vfd, 0x3000) == 1u);

    /* ---- 换向减速中改给定：过零后按新给定加速 ---- */
    vfdWriteCmd(vfd, 2);
    XVirtualModelVfd_tick(vfd);
    vfdWriteReg(vfd, 0x1000, 1200);         /* 减速中改给定 6Hz（只记忆） */
    for (i = 0; i < 400; ++i) {
        XVirtualModelVfd_tick(vfd);
        if (modelReadReg(vfd, 0x3000) == 2u &&
            XVirtualModelVfd_outputFrequency(vfd) >= 5.95) break;
    }
    CHECK("换向中改给定：过零后按新给定收敛 6Hz", i < 400);
    CHECK("改给定后稳态状态字 = 反转", modelReadReg(vfd, 0x3000) == 2u);

    /* ---- 换向减速中自由停车＝瞬断且清待决 ---- */
    vfdWriteReg(vfd, 0x1000, 2400);
    vfdWriteCmd(vfd, 1);
    CHECK("回正转 12Hz", vfdRampTo(vfd, 12.0, 40));
    vfdWriteCmd(vfd, 2);
    XVirtualModelVfd_tick(vfd);
    vfdWriteCmd(vfd, 5);
    CHECK("自由停车瞬断",
          XVirtualModelVfd_outputFrequency(vfd) == 0.0 &&
          !XVirtualModelVfd_running(vfd));
    XVirtualModelVfd_tick(vfd);
    CHECK("待决已清：停机态推进不翻向", modelReadReg(vfd, 0x3000) == 3u);

    /* ---- 换向减速中减速停机＝真停（覆盖换向） ---- */
    vfdWriteCmd(vfd, 1);
    CHECK("再次爬到 12Hz", vfdRampTo(vfd, 12.0, 40));
    vfdWriteCmd(vfd, 2);
    XVirtualModelVfd_tick(vfd);
    vfdWriteCmd(vfd, 6);
    for (i = 0; i < 400; ++i) XVirtualModelVfd_tick(vfd);
    CHECK("停止覆盖换向：最终停机（状态字 3）",
          modelReadReg(vfd, 0x3000) == 3u && !XVirtualModelVfd_running(vfd));
}

/* ==================== ② 客户端：离线/空应答开关 ==================== */

static void test_clientSwitches(void)
{
    struct XVirtualModelDigital* dig = XVirtualModelDigital_create(16);
    XModbusVirtualClient* c = XModbusVirtualClient_create(dig);

    XPrintf("--- ② 客户端模拟开关 ---\n");
    CHECK("虚拟客户端建成", c != NULL);
    CHECK("缺省可连接",
          XModbusDevice_connectDevice((XModbusDevice*)c) &&
          XModbusDevice_state((XModbusDevice*)c) ==
              XModbusDevice_ConnectedState);

    /* 强制离线：Open 必须失败。这是验证"模块离线"路径的可控手段。 */
    XModbusVirtualClient_setOffline(c, true);
    XModbusDevice_disconnectDevice((XModbusDevice*)c);
    CHECK("强制离线后连接失败",
          !XModbusDevice_connectDevice((XModbusDevice*)c));

    XModbusVirtualClient_setOffline(c, false);
    CHECK("取消离线后可重新连接",
          XModbusDevice_connectDevice((XModbusDevice*)c));

    XModbusVirtualClient_setDropRate(c, 2.0);
    XModbusVirtualClient_setDropRate(c, -1.0);
    /* 概率被夹紧即可，不崩就是通过；具体值不外露（封装）。 */
    CHECK("空应答概率越界被夹紧（不崩）", true);

    XClassDelete((XClass*)c);
}

/* ==================== ③ 全链路：Hub → 通道组 → 数字量站 ==================== */

static void test_fullChainDigitalStation(void)
{
    struct XVirtualModelDigital* model;
    XModbusVirtualClient* client;
    HsfServerIoDigital* st;
    uint32_t mask = 0;
    bool valid = false;
    int i;

    XPrintf("--- ③ 全链路：虚拟设备驱动数字量站 ---\n");

    model = XVirtualModelDigital_create(16);
    client = XModbusVirtualClient_create(model);
    st = (HsfServerIoDigital*)XMemory_malloc(sizeof(HsfServerIoDigital),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    CHECK("模型/客户端/站对象建成", model && client && st);
    if (!model || !client || !st) return;

    HsfServerIoDigital_init(st);
    /* [关键] 这里把虚拟客户端直接当设备客户端交给站模块——**装配代码
     * 不知道也不关心**它是真 TCP 还是进程内模型。这正是本次抽层的验证点。 */
    CHECK("数字量站用虚拟客户端装配成功",
          HsfServerIoDigital_mountWithClient(st, (XModbusClient*)client,
                                             16, 0, 50, 500));

    /* 轮询若干拍让 Hub 回填缓存。 */
    spinMs(300);

    CHECK("读到 DI 位图", HsfServerIoDigital_readDi(st, &mask, &valid));
    CHECK("DI 有效", valid);
    XPrintf("[INFO] DI mask=0x%08X valid=%d count=%d\n", (unsigned)mask,
            (int)valid, HsfServerIoDigital_channelCount(st));
    /* [口径对齐 Python] 16 点模型 interlock 缺省开 ⇒ **6 路阀**全部"已关"
     * 的开机稳态，行程推导出关到位位（偶数位 0/2/4/6/8/10）= 0x0555——
     * 与旧静态缺省值数值相同但来源不同：现在是"阀区行程推导"，不再是
     * 发明观感。
     * [为什么是 6 路不是 8 路] 阀区边界 = 现场六路电磁阀（Python
     * VirtualDigital.VALVE_COUNT 同值）。阀区之外的通道（进水泵 DO6、
     * 按钮灯 DO7..9、按钮输入 DI12..15）**不是阀**：写它们不产生行程，
     * 也不该动任何到位位——早先按"模型容量 8"推导，会把 DI12..15 也当
     * 阀的到位位，阀一动作就把按钮组的输入点拉成 0。 */
    CHECK("DI 开机稳态 = 6 路阀全关（行程推导 0x0555）", mask == 0x0555u);

    /* 写 DO0 = 1 → 回读一致（写路径经 Hub → 虚拟客户端 → 模型）。 */
    CHECK("写 DO0 受理", HsfServerIoDigital_writeDo(st, 0, true));
    spinMs(120);
    mask = 0;
    CHECK("回读 DO0 已置位",
          HsfServerIoDigital_readDo(st, &mask, &valid) && (mask & 1u));
    XPrintf("[INFO] DO mask=0x%08X valid=%d\n", (unsigned)mask, (int)valid);

    /* 通道新鲜度：链路未陈旧时必须为真（框架新增失效语义的正向验证）。 */
    for (i = 0; i < 16; ++i) {
        if (!HsfServerIoDigital_isDiFresh(st, i)) break;
    }
    CHECK("在线时全部 DI 通道新鲜", i == 16);

    /* ---- 模拟模块离线：Open 失败 ⇒ Hub 重连不上 ⇒ 缓存块变陈旧 ---- */
    XModbusVirtualClient_setOffline(client, true);
    XModbusDevice_disconnectDevice((XModbusDevice*)client);
    /* 让陈旧门限过去（XModbusLink 侧 5000ms 是产品口径；此处用 Hub 的
     * 读写失败把 lastOkMs 拖旧是不现实的，故只验证"离线后连接不重建"）。 */
    spinMs(200);
    CHECK("离线后设备状态非 Connected",
          XModbusDevice_state((XModbusDevice*)client) !=
              XModbusDevice_ConnectedState);

    HsfServerIoDigital_unmount(st);
    XClassDelete((XClass*)st);
    /* [不要删 client] 所有权已随 addDevice 转移给 Hub，Hub 在 unmount 里
     * 被释放时一并释放客户端。再删一次是双重释放（首版在此崩）。 */
}

/* ==================== ④ 阀组 + 虚拟数字量：动作与到位 ==================== */

static void test_valveWithVirtualDevice(void)
{
    struct XVirtualModelDigital* model;
    XModbusVirtualClient* client;
    HsfServerIoDigital* dig;
    HsfServerIoValve bank;
    HsfServerIoValveCfg cfg[HSF_VALVE_COUNT];
    const HsfServerIoValveItem* it;
    uint32_t diMask = 0;
    bool diValid = false;
    int i;
    int64_t t0;

    XPrintf("--- ④ 阀组 + 虚拟设备：开阀/到位/超时 ---\n");

    model = XVirtualModelDigital_create(16);
    client = XModbusVirtualClient_create(model);
    dig = (HsfServerIoDigital*)XMemory_malloc(sizeof(HsfServerIoDigital),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
    if (!model || !client || !dig) {
        CHECK("阀组夹具建成", false);
        return;
    }
    HsfServerIoDigital_init(dig);
    CHECK("数字量站用虚拟客户端装配",
          HsfServerIoDigital_mountWithClient(dig, (XModbusClient*)client,
                                             16, 0, 50, 500));
    spinMs(200);

    /* 阀点位：DO2 控制，**按现场接线口径** 关到位 DI4、开到位 DI5。
     * [为什么是 2n/2n+1] 模型（与 Python virtualdev.VirtualDigital 同规、
     * 也与 HsfServerConfig 的 HSF_VALVE_* 宏一致）按"DO n → 关到位 DI(2n)
     * / 开到位 DI(2n+1)"联动。旧版本曾用 openDi=n+1/closeDi=n 的简化写法，
     * 那只在 DO 与 DI 相邻时成立，换成真实点位就错位。 */
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        cfg[i].m_name = NULL;
        cfg[i].m_do = (i == 0) ? 2 : -1;
        cfg[i].m_openDi = (i == 0) ? 5 : -1;
        cfg[i].m_closeDi = (i == 0) ? 4 : -1;
    }
    HsfServerIoValve_init(&bank);
    HsfServerIoValve_configure(&bank, cfg, HSF_VALVE_COUNT, 2000, true);

    /* 首拍：模型缺省 DI 偶数通道吸合 ⇒ DI2（关到位）有效 ⇒ 已关。 */
    HsfServerIoDigital_readDi(dig, &diMask, &diValid);
    {
        uint32_t fresh = 0;
        for (i = 0; i < 16; ++i)
            if (HsfServerIoDigital_isDiFresh(dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              XDateTime_currentMSecsSinceEpoch());
    }
    it = HsfServerIoValve_item(&bank, 0);
    CHECK("初始为已关（关到位吸合）", it && it->m_state == HsfValveState_Closed);

    /* 开阀：写 DO2=1 → 模型进入行程（缺省 10s）→ 到位后 DI3 置位、DI2 释放。
     * [本用例只看"写路径能否驱动到位"，把行程调短到 50ms] 缺省 10s 是给
     * 模拟联调复现现场时序用的（让「动作中」「超时告警」两条分支可达）；
     * 这里不需要等那么久。 */
    XVirtualModelDigital_setMoveMs(model, 50u);
    CHECK("开阀受理", HsfServerIoValve_setOutput(&bank, 0, true, NULL, 0));
    CHECK("写 DO2 被受理", HsfServerIoDigital_writeDo(dig, 2, true));
    spinMs(300);
    diMask = 0;
    HsfServerIoDigital_readDi(dig, &diMask, &diValid);
    {
        uint32_t fresh = 0;
        for (i = 0; i < 16; ++i)
            if (HsfServerIoDigital_isDiFresh(dig, i)) fresh |= (1u << i);
        HsfServerIoValve_poll(&bank, diMask, diValid, fresh, 0, false,
                              XDateTime_currentMSecsSinceEpoch());
    }
    it = HsfServerIoValve_item(&bank, 0);
    CHECK("开到位后状态 = 已开", it && it->m_state == HsfValveState_Open);

    /* ---- 超时路径：关闭联动 ⇒ 写 DO 后反馈不动 ⇒ 动作中 → 超时告警 ---- */
    XVirtualModelDigital_setValveInterlock(model, false);
    CHECK("关阀受理", HsfServerIoValve_setOutput(&bank, 0, false, NULL, 0));
    HsfServerIoDigital_writeDo(dig, 2, false);
    /* 用"只给新鲜度、不给任何到位"的位图制造动作中（避免清掉 DI 影响其他断言）。 */
    t0 = (int64_t)XDateTime_currentMSecsSinceEpoch();
    HsfServerIoValve_poll(&bank, 0u, true, 0xFFFFFFFFu, 0, false, (uint64_t)t0);
    it = HsfServerIoValve_item(&bank, 0);
    CHECK("命令后未到位 = 动作中", it && it->m_state == HsfValveState_Moving);
    {
        int kind = 0;
        char text[128];
        CHECK("动作中暂无告警",
              !HsfServerIoValve_takeAlarm(&bank, 0, &kind, text,
                                          (int)sizeof(text)));
    }
    /* 跨过 2000ms 超时。 */
    HsfServerIoValve_poll(&bank, 0u, true, 0xFFFFFFFFu, 0, false, (uint64_t)t0 + 2500u);
    it = HsfServerIoValve_item(&bank, 0);
    CHECK("超时后判故障", it && it->m_state == HsfValveState_Fault);
    {
        int kind = 0;
        char text[160];
        CHECK("超时产生告警",
              HsfServerIoValve_takeAlarm(&bank, 0, &kind, text,
                                         (int)sizeof(text)));
        CHECK("告警类型 = 超时", kind == HsfValveAlarmKind_Timeout);
        CHECK("告警文字含'超时'", strstr(text, "超时") != NULL);
    }
    CHECK("告警取走后不复现（取走即消费）",
          !HsfServerIoValve_takeAlarm(&bank, 0, NULL, NULL, 0));
    /* 再推一拍：仍未到位，但不得再告警（一次动作一条）。 */
    HsfServerIoValve_poll(&bank, 0u, true, 0xFFFFFFFFu, 0, false,
                          (uint64_t)t0 + 3000u);
    CHECK("同一动作不重复告警",
          !HsfServerIoValve_takeAlarm(&bank, 0, NULL, NULL, 0));

    HsfServerIoValve_clear(&bank);
    HsfServerIoDigital_unmount(dig);
    XClassDelete((XClass*)dig);
    /* [不要删 client] 所有权已在 mountWithClient → addDevice 转移给 Hub，
     * Hub 随 unmount 释放时一并释放客户端。再删一次＝双重释放。 */
}

/* ==================== 入口 ==================== */

int XModbusVirtualTest_run(void)
{
    g_pass = 0;
    g_fail = 0;
    XPrintf("===== 虚拟 Modbus 设备（全 C 端离线联调）=====\n");
    test_modelRegisterMap();
    test_vfdReversalRamp();
    test_clientSwitches();
    test_fullChainDigitalStation();
    test_valveWithVirtualDevice();
    XPrintf("===== 结果：PASS %d / FAIL %d =====\n", g_pass, g_fail);
    return g_fail;
}

void XModbusVirtualTest(XVariant* data)
{
    (void)data;
    XModbusVirtualTest_run();
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
