/**
 * @file       main_server.c
 * @brief      高速过滤器服务端入口：XCoreApplication 控制台事件循环。
 * @details    用法：HsfServer [端口] [串口名] [从站地址] [TCP IP] [TCP 端口]
 *             （缺省 6000 / COM26 / 1 / 192.168.1.117 / 502，端口 0=系统分配）。
 *             [远程 IO 演示] main 内直接用 XModbusHub 挂两台远程 IO
 *             （各 12 路输入 + 16 路输出）：
 *               - 设备①：RTU 串口（8N1@9600，从站地址见参数）
 *               - 设备②：Modbus TCP（192.168.1.117:502）
 *             统一虚拟地址空间（Hub 聚合，互不冲突）：
 *               - 输入 DI：v0..11 ← 串口，v12..23 ← TCP（各远端 0..11）
 *               - 输出 Coils：v0..15 ← 串口，v16..31 ← TCP（各远端 0..15）
 *             输入状态变化经 valueChanged 信号 → 槽内逐位比对，控制台
 *             打印「[设备] 输入 N: 旧值 -> 新值」；首拍打印一次初态。
 *             [输出跑马灯] XTimer 1s 一拍，两台设备并行各点亮一个输出
 *             循环步进（写受理即缓存+回显，落定值经 valueChanged 打印），
 *             用于输出现场验证；轮询区默认仅 DI|IR（输出不轮询，写出走
 *             缓存+重发）。[上线对齐] 输出不轮询故外部可能残留（重启/
 *             从站掉电），设备每次跃迁 Connected 时 Hub 按映射段默认值
 *             （addRange 末参默认值按设备可设不同值，此处全 0）一帧统一下发，
 *             保证映射与外部一致。
 *             事件循环内完成轮询/信号分发/滤波；Ctrl+C 退出。
 */
#include "XCoreApplication.h"
#include "XhsfServer.h"
#include "XhsfProtocol.h"
#include "XPrintf.h"
#include <stdlib.h>
#include <stdio.h>
#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusRtuSerialClient.h"
#include "XModbusTcpClient.h"
#include "XModbusDevice.h"
#include "XVariant.h"
#include "XTimer.h"

/* ==================== 远程 IO 点表 ==================== */
#define REMOTE_IO_DEVICE_COUNT 2                        /**< 设备数（串口+TCP）。 */
#define REMOTE_IO_DI_COUNT 12   /**< 单台输入点数（离散输入 DI）。 */
#define REMOTE_IO_DO_COUNT 16   /**< 单台输出点数（线圈 DO）。 */
#define REMOTE_IO_DI_TOTAL (REMOTE_IO_DI_COUNT * REMOTE_IO_DEVICE_COUNT)  /**< DI 虚拟总点数 24。 */
#define REMOTE_IO_DO_TOTAL (REMOTE_IO_DO_COUNT * REMOTE_IO_DEVICE_COUNT)  /**< Coil 虚拟总点数 32。 */

/* 设备标签：虚拟地址低半区=串口①，高半区=TCP②。 */
#define REMOTE_IO_TAG(addr, perCount) \
    (((addr) < (perCount)) ? "串口" : "TCP")

/* 输入基线（槽内逐位比对用；静态零初始化，与 DI 缓存位哨兵 0 一致）。 */
static uint16_t g_diLast[REMOTE_IO_DI_TOTAL];
static bool g_diBaselineReady = false;

/**
 * @brief       槽：valueChanged —— 输入状态变化打印（信号与槽通知）。
 * @details     DI 回填段信号到达后整体读 24 点缓存（两台设备），与基线
 *              逐位比对，只打印确实变化的输入；首拍打印一次初态后建立
 *              基线。Coils（输出）回显（写受理/收口回填）按信号携带的
 *              变化区间打印，便于调试。
 */
static void onValueChanged(XObject* sender, XVarList* args)
{
    XModbusHub* hub;
    uint16_t vals[REMOTE_IO_DI_TOTAL];
    int i;

    if (!args) return;
    XVarList_args_3(args, int, type, int, vStart, int, count);
    hub = (XModbusHub*)sender;
    if (!hub) return;

    if (type == (int)XModbusCoils) {            /* 输出回显（写后回填）。 */
        for (i = vStart; i < vStart + count && i < REMOTE_IO_DO_TOTAL; ++i) {
            uint16_t v = 0;
            if (i < 0) continue;
            if (XModbusHub_value(hub, XModbusCoils, (uint16_t)i, &v) && v != 0)
                XPrintf("[DO] [%s] 输出 %d 回显: 1\n",
                        REMOTE_IO_TAG(i, REMOTE_IO_DO_COUNT),
                        i % REMOTE_IO_DO_COUNT);
        }
        return;
    }
    if (type != (int)XModbusDiscreteInputs) return;

    if (!XModbusHub_values(hub, XModbusDiscreteInputs, 0,
                           REMOTE_IO_DI_TOTAL, vals)) return;

    if (!g_diBaselineReady) {                   /* 首拍：打印初态建基线。 */
        for (i = 0; i < REMOTE_IO_DI_TOTAL; ++i) {
            XPrintf("[DI] [%s] 输入 %d 初态: %d\n",
                    REMOTE_IO_TAG(i, REMOTE_IO_DI_COUNT),
                    i % REMOTE_IO_DI_COUNT, (int)vals[i]);
            g_diLast[i] = vals[i];
        }
        g_diBaselineReady = true;
        return;
    }
    for (i = 0; i < REMOTE_IO_DI_TOTAL; ++i) {  /* 逐位比对，只报变化沿。 */
        if (vals[i] != g_diLast[i]) {
            XPrintf("[DI] [%s] 输入 %d 状态改变: %d -> %d\n",
                    REMOTE_IO_TAG(i, REMOTE_IO_DI_COUNT),
                    i % REMOTE_IO_DI_COUNT,
                    (int)g_diLast[i], (int)vals[i]);
            g_diLast[i] = vals[i];
        }
    }
}

/** @brief 槽：设备上下线打印（Unconnected=0 Connecting=1 Connected=2）。
 *         输出上线对齐已内置于框架：未轮询输出区（Coils/HR）在设备
 *         跃迁 Connected 时由 Hub 自动按映射段默认值一帧统一下发。 */
static void onDeviceStateChanged(XObject* sender, XVarList* args)
{
    XModbusHub* hub = (XModbusHub*)sender;
    const char* name;
    if (!args || !hub) return;
    XVarList_args_2(args, int, devId, int, state);
    name = XModbusHub_deviceName_const(hub, devId);
    XPrintf("[NET] 设备 %d(%s) 状态 -> %d\n", devId, name ? name : "?", state);
}

/** @brief 槽：从站失联打印（连续轮询失败达阈值进入退避静默）。 */
static void onUnitLost(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_2(args, int, devId, int, unitId);
    (void)sender;
    XPrintf("[NET] 设备 %d 从站 %d 失联（退避静默中）\n", devId, unitId);
    //exit(0);
}

/** @brief 槽：从站恢复打印（静默到期探活成功）。 */
static void onUnitAlive(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_2(args, int, devId, int, unitId);
    (void)sender;
    XPrintf("[NET] 设备 %d 从站 %d 恢复\n", devId, unitId);
}

/** @brief 槽：写意图落定打印（ok=true=从站确认；false=重试达阈值仍失败）。 */
static void onWriteCompleted(XObject* sender, XVarList* args)
{
    if (!args) return;
    XVarList_args_4(args, int, type, int, vStart, int, count, bool, ok);
    (void)sender;
    (void)count;
    if (type != (int)XModbusCoils) return;
    XPrintf("[DO] [%s] 输出 %d 写入%s\n",
            REMOTE_IO_TAG(vStart, REMOTE_IO_DO_COUNT),
            vStart % REMOTE_IO_DO_COUNT, ok ? "确认" : "失败（重试中）");
}

/* ==================== 输出跑马灯（1s/拍，两台并行） ==================== */
static XModbusHub* g_hub = NULL;    /**< 跑马灯槽的 Hub 借用指针。 */
static XTimer* g_marquee = NULL;    /**< 1s 周期拍定时器。 */
static int g_marqueeStep = 0;       /**< 当前点亮步进（0..15，两台同拍同步）。 */
static int g_devSerial = -1;        /**< 串口设备句柄（诊断打印用）。 */
static int g_devTcp = -1;           /**< TCP 设备句柄（诊断打印用）。 */

/** @brief 槽：3s 诊断拍——轮询健康计数（观测 unitLost 失败计数是否累计）。 */
static void onDiagTick(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    if (!g_hub) return;
    if (g_devSerial > 0)
        XPrintf("[诊断] 串口 state=%d pollOk=%u pollErr=%u\n",
                (int)XModbusHub_deviceState(g_hub, g_devSerial),
                (unsigned)XModbusHub_pollOkCount(g_hub, g_devSerial),
                (unsigned)XModbusHub_pollErrCount(g_hub, g_devSerial));
    if (g_devTcp > 0)
        XPrintf("[诊断] TCP   state=%d pollOk=%u pollErr=%u\n",
                (int)XModbusHub_deviceState(g_hub, g_devTcp),
                (unsigned)XModbusHub_pollOkCount(g_hub, g_devTcp),
                (unsigned)XModbusHub_pollErrCount(g_hub, g_devTcp));
}

/**
 * @brief       槽：跑马灯拍——两台设备并行各点亮一个输出。
 * @details     每拍熄上一拍、点当前拍（写受理即缓存+回显，落定经
 *              writeCompleted；设备离线时写意图保留周期重发）。
 *              串口设备虚拟地址 v0..15，TCP 设备 v16..31（偏移半区）。
 */
static void onMarqueeTick(XObject* sender, XVarList* args)
{
    int cur = g_marqueeStep;
    int prev = (cur + REMOTE_IO_DO_COUNT - 1) % REMOTE_IO_DO_COUNT;
    (void)sender;
    (void)args;
    if (!g_hub) return;
    XModbusHub_writeValue(g_hub, XModbusCoils, (uint16_t)prev, 0);
    XModbusHub_writeValue(g_hub, XModbusCoils, (uint16_t)cur, 1);
    XModbusHub_writeValue(g_hub, XModbusCoils,
                          (uint16_t)(prev + REMOTE_IO_DO_COUNT), 0);
    XModbusHub_writeValue(g_hub, XModbusCoils,
                          (uint16_t)(cur + REMOTE_IO_DO_COUNT), 1);
    XPrintf("[跑马灯] 第 %d 拍：两台设备输出 %d 点亮\n", cur, cur);
    g_marqueeStep = (g_marqueeStep + 1) % REMOTE_IO_DO_COUNT;
}

int main(int argc, char** argv)
{
    XCoreApplication* app;
    XModbusRtuSerialClient* rtu;
    XModbusTcpClient* tcp;
    XModbusHub* hub;
    XModbusMapping* map;
    const char* comName;
    const char* tcpHost;
    int tcpPort;
    int unitId;
    int devId = -1;      /* 串口设备句柄（-1=未注入，如仅 TCP 模式）。 */
    int devIdTcp = -1;
    unsigned port = XHSF_SERVER_DEFAULT_PORT;

    /* 诊断型控制台程序：stdout 去缓冲，保证管道/重定向下逐行可见
     * （默认全缓冲 + 强杀进程会整块丢失输出，现场排障看不到任何日志）。 */
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1) port = (unsigned)atoi(argv[1]);
    comName = (argc > 2) ? argv[2] : "COM26";
    unitId = (argc > 3) ? atoi(argv[3]) : 1;
    tcpHost = (argc > 4) ? argv[4] : "192.168.1.117";
    tcpPort = (argc > 5) ? atoi(argv[5]) : 502;

    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("HsfServer: 事件循环创建失败\n");
        return 1;
    }
    XPrintf("HsfServer: 启动\n");

    //XhsfServer* srv;
    //srv = XhsfServer_create();
  /*  if (!srv || !XhsfServer_listen(srv, port)) {
        XPrintf("HsfServer: 监听失败（端口 %u）\n", port);
        if (srv) XhsfServer_deleteLater(srv);
        XClassDelete(app);
        return 1;
    }
    XPrintf("HsfServer: 监听端口 %u\n", XhsfServer_port(srv));*/

    /* 1. RTU 串口客户端：连接参数在注入 Hub 前设好（open 时按参数建串口）。 */
    rtu = XModbusRtuSerialClient_create();
    if (!rtu) {
        XPrintf("HsfServer: RTU 客户端创建失败\n");
        XClassDelete(app);
        return 1;
    }
    {
        XVariant* v = XVariant_create_utf8_str(comName);
        XModbusDevice_setConnectionParameter((XModbusDevice*)rtu,
            XModbusDevice_SerialPortNameParameter, v);
        XClassDelete((XClass*)v);
    }
    {
        XVariant* v = XVariant_create_int(9600);   /* 8N1@9600：远程 IO 常规缺省 */
        XModbusDevice_setConnectionParameter((XModbusDevice*)rtu,
            XModbusDevice_SerialBaudRateParameter, v);
        XClassDelete((XClass*)v);
    }
    {
        XVariant* v = XVariant_create_int(XSerialPort_Data8);
        XModbusDevice_setConnectionParameter((XModbusDevice*)rtu,
            XModbusDevice_SerialDataBitsParameter, v);
        XClassDelete((XClass*)v);
    }
    {
        XVariant* v = XVariant_create_int(XSerialPort_NoParity);
        XModbusDevice_setConnectionParameter((XModbusDevice*)rtu,
            XModbusDevice_SerialParityParameter, v);
        XClassDelete((XClass*)v);
    }
    {
        XVariant* v = XVariant_create_int(XSerialPort_OneStop);
        XModbusDevice_setConnectionParameter((XModbusDevice*)rtu,
            XModbusDevice_SerialStopBitsParameter, v);
        XClassDelete((XClass*)v);
    }
    /* [失联灵敏度] 500ms 超时 × 2 次尝试：单次轮询失败 ~1s（缺省 1000×3=3s
     * 太钝，断电后失联要 ~20s 才报）。现场误码多可回调 1000/2。 */
    XModbusClient_setTimeout((XModbusClient*)rtu, 500);
    XModbusClient_setNumberOfRetries((XModbusClient*)rtu, 1);

    /* 2. Hub 聚合设备：注入 client（所有权转移；Hub 统一置自动重连）。 */
    hub = XModbusHub_create();
    if (!hub) {
        XPrintf("HsfServer: ModbusHub 创建失败\n");
        XObject_deleteLater((XObject*)rtu);
        XClassDelete(app);
        return 1;
    }
    devId = XModbusHub_addDevice_2(hub, (XModbusClient*)rtu, "io-serial");
    if (devId <= 0) {
        XPrintf("HsfServer: 设备注入失败\n");
        XObject_deleteLater((XObject*)rtu);
        XModbusHub_deleteLater((XObject*)hub);
        XClassDelete(app);
        return 1;
    }

    /* 2b. Modbus TCP 客户端（设备②）：注入前设好网络参数。 */
    tcp = XModbusTcpClient_create();
    if (!tcp) {
        XPrintf("HsfServer: TCP 客户端创建失败\n");
        XModbusHub_deleteLater((XObject*)hub);
        XClassDelete(app);
        return 1;
    }
    {
        XVariant* v = XVariant_create_utf8_str(tcpHost);
        XModbusDevice_setConnectionParameter((XModbusDevice*)tcp,
            XModbusDevice_NetworkAddressParameter, v);
        XClassDelete((XClass*)v);
    }
    {
        XVariant* v = XVariant_create_int(tcpPort);
        XModbusDevice_setConnectionParameter((XModbusDevice*)tcp,
            XModbusDevice_NetworkPortParameter, v);
        XClassDelete((XClass*)v);
    }
    XModbusClient_setTimeout((XModbusClient*)tcp, 1000);
    XModbusClient_setNumberOfRetries((XModbusClient*)tcp, 2);

    //devIdTcp = XModbusHub_addDevice_2(hub, (XModbusClient*)tcp, "io-tcp");
    //if (devIdTcp <= 0) {
    //    XPrintf("HsfServer: TCP 设备注入失败\n");
    //    XModbusHub_deleteLater((XObject*)hub);
    //    XClassDelete(app);
    //    return 1;
    //}

    /* 3. 映射（统一虚拟地址空间，两台设备各占半区）：
     *    DI   v0..11  ← 串口远端 0..11；v12..23 ← TCP 远端 0..11（12 输入/台）
     *    Coil v0..15  ← 串口远端 0..15；v16..31 ← TCP 远端 0..15（16 输出/台）
     *    输出段末参给默认值：设备上线（含重连）且输出区未
     *    轮询时，Hub 按段默认值一帧统一下发（FC15），清掉外部残留、
     *    保证映射与外部一致；两台设备可设不同默认值（位区非 0 即 1）。 */
    map = XModbusMapping_create();
    {
        bool mapOk = (map != NULL);
        if (mapOk && devId > 0)          /* 串口设备（注释注入则跳过）。 */
            mapOk =
                XModbusMapping_addRange(map, XModbusDiscreteInputs, 0,
                                        REMOTE_IO_DI_COUNT, devId, unitId, 0, 0) &&
                XModbusMapping_addRange(map, XModbusCoils, 0,
                                        REMOTE_IO_DO_COUNT, devId, unitId, 0,
                                        0x0000);
        if (mapOk && devIdTcp > 0) {     /* TCP 设备。 */
            mapOk =
                XModbusMapping_addRange(map, XModbusDiscreteInputs,
                                        REMOTE_IO_DI_COUNT,
                                        REMOTE_IO_DI_COUNT, devIdTcp, unitId,
                                        0, 0) &&
                XModbusMapping_addRange(map, XModbusCoils, REMOTE_IO_DO_COUNT,
                                        REMOTE_IO_DO_COUNT, devIdTcp, unitId,
                                        0, 0x0000);
        }
        if (!mapOk) {
            XPrintf("HsfServer: 映射配置失败\n");
            if (map) XClassDelete((XClass*)map);
            XModbusHub_deleteLater((XObject*)hub);
            XClassDelete(app);
            return 1;
        }
    }
    XModbusHub_setMapping(hub, map); /* 所有权转移 Hub。 */

    /* 4. 轮询参数：两台设备各 50ms 采样输入变化（默认轮询区仅 DI|IR，
     *    输出不轮询；失联从站自动退避让出总线，恢复后自动续拍）。 */
    if (devId > 0) XModbusHub_setPollInterval(hub, devId, 50);
    if (devIdTcp > 0) XModbusHub_setPollInterval(hub, devIdTcp, 50);

    /* 5. 信号与槽：输入变化/上下线/从站失联恢复全部经信号通知。 */
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_valueChanged_signal),
                      onValueChanged);
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_writeCompleted_signal),
                      onWriteCompleted);
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_deviceStateChanged_signal),
                      onDeviceStateChanged);
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_unitLost_signal),
                      onUnitLost);
    XObject_connect_2((XObject*)hub, XSignal(XModbusHub_unitAlive_signal),
                      onUnitAlive);

    /* 6. 连接并启动轮询；写输出示例（按需调用，此处仅示范）：
     *    XModbusHub_writeValue(hub, XModbusCoils, 0, 1);    串口设备输出 0 置位
     *    XModbusHub_writeValue(hub, XModbusCoils, 16+5, 1); TCP 设备输出 5 置位 */
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);
    XPrintf("HsfServer: 挂载完成——串口[%s](%s)、TCP [%s:%d](%s)，轮询 50ms\n",
            comName, (devId > 0) ? "已注入" : "未注入",
            tcpHost, tcpPort, (devIdTcp > 0) ? "已注入" : "未注入");

    /* 7. 输出跑马灯：XTimer 1s 一拍（事件循环驱动），两台设备并行
     *    各点亮一个输出做写入验证；点亮/熄灭的落定经 valueChanged
     *    回显打印。 */
    g_hub = hub;
    g_devSerial = devId;
    g_devTcp = devIdTcp;
    g_marquee = XTimer_create();
    if (g_marquee) {
        XTimer_setInterval(g_marquee, 1000);
        XObject_connect_2((XObject*)g_marquee, XSignal(XTimer_timeout_signal),
                          onMarqueeTick);
        XTimer_start_base(g_marquee);
    } else {
        XPrintf("HsfServer: 跑马灯定时器创建失败（不影响轮询）\n");
    }

    /* [诊断] 3s 一拍打印轮询健康计数（观测从站断电后 pollErr 是否累计、
     * 涨到阈值时 unitLost 是否发出）。 */
    {
        XTimer* diag = XTimer_create();
        if (diag) {
            XTimer_setInterval(diag, 3000);
            XObject_connect_2((XObject*)diag, XSignal(XTimer_timeout_signal),
                              onDiagTick);
            XTimer_start_base(diag);
        }
    }

    XCoreApplication_exec(); /* 控制台事件循环：轮询/信号/跑马灯全在此驱动。 */

    if (g_marquee) XTimer_stop_base(g_marquee);
    g_hub = NULL;
    XModbusHub_stopPolling(hub);
    XModbusHub_deleteLater((XObject*)hub);
    XClassDelete(app);
    return 0;
}
