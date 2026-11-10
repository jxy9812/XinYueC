/**
 * @file       hsf_regression_test.c
 * @brief      HSF C 端**总回归**（单文件验证入口）。
 *
 *     bin\XinYueC_Static.exe --test hsf-regression
 *
 * [为什么是"一个文件"]
 * 回归锁原先散在 xmodbus_virtual_test / hsf_sim_mode_test / … 多个文件里，
 * 改一处功能要翻好几处才知道该跑哪些。这里把**框架层契约**与
 * **客户端协议契约**收成一份，跑一次就覆盖；以后加/改功能先在这里补断言。
 * Python 端有对应的总回归文件：
 *   HighSpeedFilter/client_py/tests/test_hsf_regression.py
 * 两份互为对照：Python 侧钉"服务端发的形状"，本文件钉"C 端按形状取的映射"。
 *
 * [覆盖面]
 *   §A 框架 Modbus 四层根修（2026-11-09「485 从站跑久了停摆」）
 *      A1 异常应答必须按失败记账（原口径当成功 ⇒ 看门狗永远健康、缓存不回填）
 *      A2 嵌套同步收口不再整包丢帧（回填/计数/退避记账/沿全都要有）
 *      A3 写持续失败也不饿死读组（失败不在同拍重试 ⇒ 读写交替计数奇偶不变）
 *      A4 按从站粒度的新鲜度（deviceWorstUnitAge：块级 lastOkMs）
 *
 *   §B 客户端历史应答解析契约（跨端）
 *      B1 应答键与 offset/limit 回显（客户端靠它认领应答，对不上就丢）
 *      B2 行宽 18 列、数值列是数值（不是预格式化文本）
 *      B3 列下标映射：freq/current/rpm/turb/flow/press/temp/hum 各取对列
 *
 *   §C 表格排序三态（app 层助手 HsfTableSort 的核心状态机）
 *
 *   §D 框架层「Qt 对齐补全」回归锁（2026-11-09 补的那几处发射点）
 *      D1 sectionDoubleClicked 有**真实发射点**且载荷=段号（此前只有声明）
 *      D2 sectionEntered 有**真实发射点**且载荷=段号（同段内不重复发）
 *      D3 表头释放时翻转排序指示器（Qt mouseReleaseEvent 尾段口径）
 *      D4 setSortIndicator 之后指示器段号/方向可读
 *      D5 setSortingEnabled(true) ⇒ 单击表头按该列排序（Qt 口径：空值下沉）
 *      D6 setSortingEnabled(false) ⇒ 表头段不再可点（不再触发排序）
 *      D7 信号函数的 NULL 取 id 约定（不崩、不发射；今天这里崩过）
 *      D8 app 层三态策略（新列⇒降 / 同列⇒升 / 再点⇒取消）+ 指示器联动
 *
 *   §E 交叉测试（层与层、端与端互相验证）
 *      E1 (a) 框架原语 XTableWidget_sortItems ↔ app 三态策略：同一行序
 *      E2 (b) C 客户端解析器 ↔ Python 服务端**真实产出的**历史应答载荷
 *      E3 (c) C 服务端 ↔ Python 服务端：两份历史应答逐键逐列同形
 *      E4 (d) 框架 Hub 读组/回填 ↔ app 设备站读到的值（同一时刻同一寄存器）
 *
 *   §F 覆盖面扩容（今天 C 端改过但此前没被 §A/§B/§C 覆盖的行为）
 *      F1 读组失败的可观测日志（限频打印真的有，且带组/地址/错误码）
 *      F2 块级陈旧诊断（"轮询停滞"逐块逐组列出跳过原因）
 *      F3 deviceWorstUnitAge 的两种语义：从未上报 vs 曾上报后掉线
 *      F4 「只续写不续轮询」的节拍不被放大（写持续失败时读节拍不翻倍）
 *
 *   §G 泄漏自检（工作集 3 轮堆增量必须为 0；含读组失败日志 + 日志捕获管路）
 *
 *   §H 历史页「图表缩放取数」专项回归（2026-11-09 并入本文件）
 *      断言本体（Z1..Z11）就在**本文件**的 §H 段里（原独立文件
 *      hsf_chart_zoom_test.c 已删除——用户口径「C 端只留一个测试文件」）。
 *      app 层纯逻辑助手 HsfChartZoom.c 由本文件单 TU 收录。
 *      覆盖：批次计划（窗口逐行/全量跳批）、可见窗同基准换算、全量视图
 *      判定、已取窗覆盖、取数 limit 轮转、轴域与取数窗同源、点落在窗内并
 *      铺满视口、状态行随窗口重算、纯逻辑路径无泄漏。
 *
 *   §I 快照/控制命令的「请求-应答」兜底通道（2026-11-09 根修）
 *      I1 0x0103 模拟量快照应答：model（值/掩码/标定/名字）被刷新 + 通知
 *      I2 0x0106 流量计快照应答：按 side 落进对应侧 + 通知 + 判新鲜
 *      I3 0x0112 浊度仪快照应答：台数/名字/NTU/在线位被刷新 + 通知
 *      I4 0x0113 泵快照应答：刷新 + 通知；**失败包络不覆盖 model**
 *      I5 0x0102 变频器快照应答：XVfdStatus 被解码（频率/转速/运行位）
 *      I6 五条命令的畸形/空载荷：不崩、不覆盖已有 model
 *      I7 0x0114 泵控制应答：命令序列号推进/错误文案/并帧快照应用
 *      I8 0x0115 自动运行应答：模式与运行位回填 + 失败原因浮出
 *      I9 未连线时自愈补拍是空操作（页面 1Hz 兜底不发假请求）
 *      断言方式 = 白盒：HsfLinkClient_testDispatchResponse 把构造好的
 *      Response 帧喂进线上同一条分发函数（离屏收不到真实应答）。
 *
 *   §J 认证「策略性拒绝」失败语义（2026-11-09 根修：模拟模式偶发无数据）
 *      J1 服务端查表返回 Rejected ⇒ 客户端收到 **Rejected(4)**、零次
 *         authChallenge（不许伪造"随机盐+全 0 摘要"的假挑战）、只给一次结论
 *      J2 反枚举口径不变：lookup=BadUser 仍走假挑战 ⇒ 客户端最终
 *         **BadPassword(2)**（顺手别把这条改坏的锁）
 *      断言方式 = 回环设备对驱动两个会话（与 XLinkTest.c 认证用例同夹具），
 *      真时钟假步进，不依赖网络。
 *
 *   §M 读组「不应答」与「异常应答」的框架级回归锁（2026-11-10）
 *      M1 某组不应答 ⇒ 在途超时收口（判失败 + 记失败计数 + 报从站退避 +
 *         释放在途标记）⇒ **别的组照常轮询**（不能出现"一站哑掉、整条
 *         总线停摆"；修前：单飞闸把该设备后续所有读组永久堵死）。
 *      M2 异常应答（EXC02/EXC03）是**组级**事实 ⇒ 不牵连同从站的其它组
 *         （修前：按从站级失败回报退避表 ⇒ 连续 3 次后整站被静默）。
 *      M3 静默闸饥饿逃逸：静默期配置离谱（现场根因：调用方漏填该字段
 *         留下的栈垃圾 0xCCCCCCCC）时，框架仍保证该从站不会被**永久饿死**。
 *   §K 三设备接入（ttyS4 路 B：浊度仪 ×2 + 电磁流量计，2026-11-10）
 *      K1 实测原始帧逐字节：站 1 `8F 5C 40 12`（CDAB）= 2.29f（锁站 1
 *         不回退）；站 2 `4F DD 37 3D`（DCBA 整字节反序=小端）= 0.0449f、
 *         温度字 `00 00 99 41` 同字序 = 19.125℃（同刻）；非法字序拒绝。
 *      K2 站号配置锁：浊度仪 1/2、流量计 5；字序 0/2、寄存器数 2/4、
 *         虚拟地址不重叠（换表只改 HsfServerConfig.h 两个定义）。
 *      K3 模拟器（三台共用一条虚拟 485）装配后经**真实解码路径**读回：
 *         站1==2.29、站2==0.045+温度 19.3、站5==3.6、空管 False、单位码 5。
 *      K4 §K 跑完堆回基线（对齐 §G：预热 1 轮 + 3 轮工作集增量 == 0）。
 *      K5 发帧前静默闸（m_preTxQuietMs）行为锁（见 §K 块注详述）。
 *      K6 应答时延旋钮（2026-11-10 保真扩容）：宏缺省=实测值（站1 37~43/
 *         站2 31~33/站5 68~69ms，恢复期 125ms）；setter/getter 成对可配；
 *         模拟装配处接线锁。**时延数值本身不做耗时断言**（CI 抖动），
 *         只断言行为方向（应答照常到达）。
 *      K7 站 1 长帧后恢复期行为锁：站 5 的 49 字节长帧收口后窗口内站 1
 *         **无应答**（NULL=上层按超时处理）+ 计数；窗口外恢复；窗口 0=
 *         关闸可配。
 *      K8 站 2 功能码怪癖：FC04 请求也受理但**应答功能码恒 03**、数据与
 *         FC03 逐字节一致；站 1 无 FC04（仍异常应答 fc|0x80）。
 *      K9 数值微抖动方向锁（实测 §1.3 分台口径）：站 1 恒定 2.29、
 *         站 2 0.045±0.001 + 温度 19.3±0.05、站 5 3.6±2% 正弦波动、
 *         空管 False；setInstantFlow 钉死口径不回退。
 *
 * [与 Python 侧总回归的分工]
 *   Python §1 钉服务端列名/列序/类型；本文件 §B3 钉 C 端按**下标**取哪列。
 *   任一端改了形状，另一端的断言就会红。
 */
#include "HsfLinkHistory.h"
#include "HsfTableSort.c"   /* app 层助手：单 TU 收录（与 hsf_*_test.c 同款） */
/* §H：历史页缩放取数的纯逻辑助手（原来由独立的 hsf_chart_zoom_test.c 收录，
 *     2026-11-09 并入本文件 —— 用户口径「C 端只有一个测试文件」）。 */
#include "HsfChartZoom.c"
/* §I：客户端快照/控制应答的分发（HsfLinkClient 在 Static 目标里**已作为
 * 独立 TU 编译**——见 Test/CMakeLists.txt 的 HSF_CLIENT_TEST_MODULE_SOURCES，
 * 故这里只 include 头文件走公开 API + 测试钩子，**不能**单 TU 收录 .c
 * （会与同一目标里的那份实现撞键 LNK2005）。 */
#include "HsfLinkClient.h"
#if XVFD_ON
#include "XVfdCodec.h"      /* §I5：用同一套编解码器造变频器快照应答。 */
#include "XVfdM512.h"       /* §O：M512 遥测刻度（实机标定锁）。 */
#endif

#include "XModbusVirtualClient.h"
#include "XModbusVirtualModel.h"
#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusClient.h"
#include "XModbusDevice.h"
/* §M 假从站要直接用受保护接口（setState / createReply* / setState(Reply)）：
 * 不包含时 C 会按"隐式返回 int"处理这些函数，x64 下指针高 32 位被截掉，
 * 随后 setState 直接 AV（XModbusVirtualClient.c 里的同款注记）。 */
#include "XModbusClient_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XModbusReply_Protected.h"
#include "XModbusPdu.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include "XBson.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XByteArray.h"
#include "XString.h"
#include "XClass.h"
#include "XMemory.h"
/* §D：框架 Qt 对齐（表头/表格排序） */
#include "XTableWidget.h"
#include "XTableView.h"
#include "XHeaderView.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XObject.h"
#include "XVector.h"
#include "XGeometry.h"
/* §E(d)：app 层设备站（服务端 485 侧的温湿度站） */
#include "HsfServerBus.h"
#include "HsfServerIoTempHum.h"
/* §K：ttyS4 路 B 三设备（浊度仪 ×2 + 流量计）——设备层字序旋钮 + 站模块 +
 * 虚拟模型走同一条真实解码路径（含 2026-11-09 实测的 CDAB/DCBA 双口径）。 */
#if XINDUSTRY_ON && XTURBIDITY_ON
#include "XTurbidityMeter.h"
#include "XFlowMeter.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "HsfServerConfig.h"    /* §K2：站号/字序/寄存器数宏（K2 配置锁）。 */
#include "HsfServerIoTurbidity.h"
#include "HsfServerIoFlowMeter.h"
#include "HsfServerIoSim.h"     /* §K6：模拟装配处的时延/恢复期接线锁。 */
#endif
/* §N：现场口径对齐（侧号解析 + 流量计健康判定走公开 API）。 */
#include "HsfServerIo.h"        /* §N2：HsfServerIo_turbiditySide 等。 */
#include "HsfServerIoFlowMeter.h" /* §N3：HsfServerIoFlowMeter_health。 */
/* §J：策略性拒绝（Rejected）语义 —— 用回环设备对驱动两个会话（与
 *     XLinkTest.c 的认证用例同一夹具），不依赖真实网络与真实时钟。 */
#include "XLinkSession.h"
#include "XLinkProto.h"
#include "XGuiRemoteLoopback.h"
#include <string.h>
#include <math.h>
#include <stdarg.h>

#if XMODBUS_ON && XMODBUS_CLIENT_ON

/* §H：历史页「图表缩放取数」专项（2026-11-09 已并入本文件，见下文 §H 段）。
 *     保留这个前置声明是为了在 §H 段之前就能引用（并把 `--test hsf-chart-zoom`
 *     这个旧入口继续指到同一个函数，行为零变化）。
 *     返回值 = 本次 §H 的失败数（0 表示全绿）。 */
extern int HsfChartZoomTest_run(void);

static int g_pass = 0;
static int g_fail = 0;

/* ---- 断言宏 ----
 *
 * [用法] CHECK(描述, 判据) 或 CHECK(描述, 判据, "格式串", 实参…)。
 *         带详情时，失败会打印格式化后的详情（printf 风格）。
 *
 * [为什么详情写成 hsfChkDetail("" __VA_ARGS__)]
 *   MSVC 传统预处理器把 __VA_ARGS__ 当成**一个不可拆分的片段**，且为空时
 *   整个消失 ⇒ 直接转发给变参函数会在"无详情"时留下空实参（编译错误），
 *   直接传另一个宏则无法按实参个数分派（实测 COUNT 方案展开成 _1）。
 *   办法：在详情前拼一个**空串字面量**——字符串字面量可拼接：
 *     · 无详情 ⇒ hsfChkDetail("")            （1 个实参，合法）
 *     · 有详情 ⇒ hsfChkDetail("" "count=%d", n) ⇒ 首参拼成 "count=%d"，
 *                其余实参原样传给变参函数（由编译器按逗号拆分）。
 *   [顺带] 失败时额外打印 #cond（判据的源码文本），定位更快。
 */
static void hsfChkDetail(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "%s", ap);
    va_end(ap);
    XPrintf("%s", buf);
}

#define CHECK(desc, cond, ...)                                             \
    do {                                                                   \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); }              \
        else {                                                             \
            g_fail++;                                                      \
            XPrintf("[FAIL] %s ", desc);                                   \
            hsfChkDetail("" __VA_ARGS__);                                  \
            XPrintf("\n        判据：%s\n", #cond);                        \
        }                                                                  \
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

/** @brief 当前堆在用字节数（泄漏定位用；统计被裁剪时恒 0）。 */
static uint64_t heapNow(void)
{
    if (!XMemory_statisticsEnabled()) return 0;
    return XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
}

/** @brief 冲刷事件循环的延迟删除队列（deleteLater 的释放由事件循环完成）。 */
static void flushDeferred(void)
{
    int i;
    for (i = 0; i < 10; ++i) {
        XCoreApplication_processEvents(0);
        XThread_msleep(1);
    }
}

/* ============================================================================
 * §A 框架 Modbus 四层根修
 * ==========================================================================*/

/**
 * @brief 造一个「虚拟从站 + Hub」的最小装配（进程内，无串口/无套接字）。
 */
typedef struct {
    struct XVirtualModelDigital* model;
    XModbusVirtualClient* client;
    XModbusHub* hub;
    int devId;
} HubFixture;

static bool hubFixtureMake(HubFixture* fx)
{
    if (!fx) return false;
    memset(fx, 0, sizeof(*fx));
    fx->model = XVirtualModelDigital_create(16);
    fx->client = XModbusVirtualClient_create(fx->model);
    fx->hub = XModbusHub_create();
    if (!fx->model || !fx->client || !fx->hub) return false;
    if (!XModbusVirtualClient_bindUnit(fx->client, 1,
                                       XVirtualModel_base(fx->model))) {
        return false;
    }
    fx->devId = XModbusHub_addDevice_2(fx->hub, (XModbusClient*)fx->client,
                                        "虚拟站");
    return fx->devId >= 0;
}

/**
 * @brief 拆夹具（Hub → 客户端 → 模型；客户端所有权在 Hub，模型是借用）。
 * @note  §A 各用例此前不回收 ⇒ 每跑一次总回归就漏一个 Hub + 客户端 + 模型。
 *        回归文件自己漏内存会让"泄漏压测"失去意义，故统一在此收口。
 * @note  [模型不是 XClass] 虚拟模型（XVirtualModelDigital 等）是裸
 *        XMemory_malloc 出来的普通对象，**没有 destroy 实现**（见
 *        Test/XProtocolTest/xmodbus_virtual_test.c 的同款注记）⇒ 只能按
 *        其分配槽位（XCLASS_DEFAULT_MEMORY_TYPE = XMEMORY_TYPE_SYSTEM）
 *        用 XFree_System 释放；误用 XClassDelete 会调 XClass 虚表崩掉。
 */
static void hubFixtureFree(HubFixture* fx)
{
    if (!fx) return;
    if (fx->hub) { XClassDelete((XClass*)fx->hub); fx->hub = NULL; }
    fx->client = NULL;   /* 所有权已随 Hub 释放 */
    if (fx->model) { XFree_System(fx->model); fx->model = NULL; }
    fx->devId = -1;
}

/**
 * @brief A1：Modbus 异常应答必须按**失败**记账。
 * @details 虚拟模型单次最多 16 点（实测硬约束）；读 17 点 ⇒ 回 EXC03。
 *          原口径把异常帧当成功：lastSuccessMs 假性推进（应用层看门狗
 *          永远判健康）、pollOkCount 虚增、缓存永不回填 ⇒ 现场"设备在线
 *          但无数据"，只有重启服务端能恢复。
 */
static void testA1_exceptionIsFailure(void)
{
    HubFixture fx;
    XModbusMapping* map;
    uint32_t okCount = 0;
    uint32_t errCount = 0;
    uint64_t worst = 0;

    XPrintf("--- A1 异常应答按失败记账 ---\n");
    CHECK("A1 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }

    map = XModbusMapping_create();
    /* 读段 17 点 > 模型单次上限 16 ⇒ 模型回 EXC03（异常帧）。 */
    CHECK("A1 映射段登记（DI 17 点，故意超上限）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);          /* 所有权移交 Hub */
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);

    spinMs(500);
    okCount = XModbusHub_pollOkCount(fx.hub, fx.devId);
    errCount = XModbusHub_pollErrCount(fx.hub, fx.devId);
    XPrintf("[INFO] A1 pollOk=%u pollErr=%u lastSuccess=%llu\n", okCount,
            errCount, (unsigned long long)
            XModbusHub_deviceLastSuccessMs(fx.hub, fx.devId));
    CHECK("A1 异常帧按失败记账（pollErr>0）", errCount > 0);
    CHECK("A1 异常帧不算成功（pollOk 不增长）", okCount == 0);
    CHECK("A1 异常帧不推进 lastSuccessMs（看门狗得以看见）",
          XModbusHub_deviceLastSuccessMs(fx.hub, fx.devId) == 0);
    /* A4：按从站粒度的新鲜度——从未回填 ⇒ UINT64_MAX。 */
    CHECK("A4 最陈旧从站龄 = 从未（UINT64_MAX）",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, NULL, &worst)
          && worst == UINT64_MAX);
    hubFixtureFree(&fx);
}

/**
 * @brief A2 + A3：嵌套同步收口不再丢帧；写持续失败也不饿死读。
 * @details 写段越界（远端 30..31 > 模型 16 点）⇒ 写必回异常 ⇒ 脏区间
 *          常驻 ⇒ 每拍都走"写收口→续泵→读收口"的嵌套路径。
 *          原实现在这里把**读帧整包丢弃**（pollOk 冻结 0、块级 lastOkMs
 *          不推进 ⇒ 点级新鲜度恒陈旧）；且失败的写在同拍被立即重试两次 ⇒
 *          「读写交替让路」的交替计数奇偶不变 ⇒ 读永远轮不到。
 */
static void testA2_nestedCloseoutAccounting(void)
{
    HubFixture fx;
    XModbusMapping* map;
    uint32_t okCount = 0;
    uint32_t errCount = 0;
    bool valid = false;

    XPrintf("--- A2/A3 嵌套收口记账 + 写失败不饿死读 ---\n");
    CHECK("A2 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }

    map = XModbusMapping_create();
    /* 读段正常（DI 2 点）；写段越界 ⇒ 写必失败（脏区间常驻）。 */
    CHECK("A2 映射段登记（DI 2 点 + Coils 越界写段）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    fx.devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusCoils, 0x200, 2, fx.devId, 1,
                                    30, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    {
        uint16_t vals[2];
        vals[0] = 1;
        vals[1] = 1;
        XModbusHub_writeValues(fx.hub, XModbusCoils, 0x200, 2, vals);
    }
    spinMs(1200);
    okCount = XModbusHub_pollOkCount(fx.hub, fx.devId);
    errCount = XModbusHub_pollErrCount(fx.hub, fx.devId);
    valid = XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x100, 3000);
    XPrintf("[INFO] A2 pollOk=%u pollErr=%u\n", okCount, errCount);
    CHECK("A2 读组成功被记账（嵌套丢帧已修）", okCount > 0);
    CHECK("A2 点级新鲜度保持有效（块级 lastOkMs 推进）", valid);
    /* A3：写持续失败也不该把读饿死。 */
    CHECK("A3 写持续失败也不饿死读组（pollOk 持续增长）", okCount > 10);
    (void)errCount;   /* 写失败不计入 pollErrCount（写路径有自己的沿） */
    hubFixtureFree(&fx);
}

/**
 * @brief A4：按从站粒度的新鲜度接口（块级 lastOkMs）。
 * @details 设备级"最后成功应答"会被同一 Hub 上**其它从站的成功掩盖**：
 *          一条 485 线上挂变频器+温湿度两台，变频器一成功，设备级计时器
 *          就一直新鲜 ⇒ 温湿度那台停止刷新也看不出来（现场实证）。
 *          本接口按"最陈旧从站的块级 lastOkMs"给判据，看门狗据此判停滞。
 */
static void testA4_worstUnitAge(void)
{
    HubFixture fx;
    XModbusMapping* map;
    int worstUnit = -1;
    uint64_t worst = 0;

    XPrintf("--- A4 按从站粒度的新鲜度 ---\n");
    CHECK("A4 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("A4 映射段登记（DI 2 点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(400);
    CHECK("A4 能取到最陈旧从站",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst));
    CHECK("A4 最陈旧从站号正确（站 1）", worstUnit == 1,
          "unit=%d", worstUnit);
    CHECK("A4 新鲜从站的点龄在阈值内", worst != UINT64_MAX && worst <= 3000,
          "age=%llu ms", (unsigned long long)worst);
    hubFixtureFree(&fx);
}

/* ============================================================================
 * §B 客户端历史应答解析契约（跨端）
 * ==========================================================================*/


/** @brief 造一个 BSON 字符串值（XBsonValue_create_string 吃 XString*）。 */
static XBsonValue* bsonStr(const char* utf8)
{
    XString* str = XString_create_utf8(utf8 ? utf8 : "");
    XBsonValue* v = XBsonValue_create_string(str);
    if (str) XClassDelete((XClass*)str);
    return v;
}

/**
 * @brief 造一个 **Python 服务端同形状** 的历史应答载荷。
 * @details 形状（与 server_py/hsfsrv/app.py 的 _h_history_page 及
 *          client_py/tests/test_hsf_regression.py §1 逐字对齐）：
 *            offset / limit / total / rows
 *            rows 每行 18 列：ts,mode,vfd_state,freq_hz,current_a,rpm,
 *            turb1,turb2,flow,flow_unit,flow_total,total_unit,press_mpa,
 *            press_note,temp,hum,di,do
 *          **数值列必须是数值**（不是预格式化文本）——C 端按列下标取原值
 *          画图表，收到文本就会静默读成 0。
 */
static XByteArray* buildHistoryPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* rows = XBsonArray_create();
    XBsonArray* row = XBsonArray_create();
    XByteArray* bytes = NULL;
    static const char* kCols[] = {
        "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
        "turb1", "turb2", "flow", "flow_unit", "flow_total",
        "total_unit", "press_mpa", "press_note", "temp", "hum", "di", "do",
        "water_temp"   /* 2026-11-10：水温，追加在末尾 */
    };
    XBsonValue* v;
    int i;

    if (!doc || !rows || !row) {
        if (doc) XClassDelete((XClass*)doc);
        if (rows) XClassDelete((XClass*)rows);
        if (row) XClassDelete((XClass*)row);
        return NULL;
    }
    /* 一行：ts=1794201311849，其余按上面的列序给可区分的值。 */
    v = XBsonValue_create_int64(1794201311849LL);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("手动");
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("停机");
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(46.10);      /* freq_hz */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(2.95);       /* current_a */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(1328.0);      /* rpm */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(12.31);       /* turb1 */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(3.26);        /* turb2 */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(36.94);       /* flow */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("m3/h");        /* flow_unit */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(119.36);      /* flow_total */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("m3");          /* total_unit */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(0.4992);      /* press_mpa */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("");             /* press_note */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(26.5);        /* temp */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(55.0);        /* hum */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_int64(21845);        /* di */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_int64(256);          /* do */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    /* [水温 2026-11-10] 生产列序**末尾**新增的一格（带温度的机型才有值；
     * 固件里没有温度的机型这里是 null）。 */
    v = XBsonValue_create_double(19.35);       /* water_temp */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);

    {
        /* [与服务端同款] 行数组塞进值里再挂到 rows：setArray_ref 借用，
         * append_move_base 把内容移进数组元素，随后释放值壳。 */
        XBsonValue* rv = XBsonValue_create(XBSON_TYPE_ARRAY);
        if (rv) {
            XBsonValue_setArray_ref(rv, row);
            row = NULL;                      /* 所有权交给值 */
            XBsonArray_append_move_base(rows, rv);
            XBsonValue_delete(rv);
        }
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "offset", 0);
    XBsonDocument_insert_keyUtf8_int32(doc, "limit", 20);
    XBsonDocument_insert_keyUtf8_int32(doc, "total", 14342);
    XBsonDocument_insert_keyUtf8_array_move(doc, "rows", rows);
    {
        XBsonArray* cols = XBsonArray_create();
        /* [跟协议走] 直接用 HSF_HISTORY_WIRE_COLS（19），不写死 18 —— 写死
         * 会在加了 water_temp 后漏发最后一列的列名。 */
        for (i = 0; i < HSF_HISTORY_WIRE_COLS; ++i) {
            XBsonValue* cv = bsonStr(kCols[i]);
            XBsonArray_append_move_base(cols, cv);
            XBsonValue_delete(cv);
        }
        XBsonDocument_insert_keyUtf8_array_move(doc, "cols", cols);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief §B：客户端按列下标取值 —— 必须取对列。
 */
static void testB_historyParseContract(void)
{
    XByteArray* bytes = buildHistoryPayload();
    HsfHistoryPage page;
    const HsfHistoryRow* row;

    XPrintf("--- B 客户端历史应答解析契约 ---\n");
    CHECK("B 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    memset(&page, 0, sizeof(page));
    CHECK("B 解析成功",
          HsfHistoryPage_parse(XByteArray_constData(bytes),
                              (uint32_t)XByteArray_size_base(bytes), &page));
    CHECK("B offset 回显解析正确", page.m_offset == 0,
          "offset=%d", page.m_offset);
    CHECK("B limit 回显解析正确", page.m_limit == 20,
          "limit=%d", page.m_limit);
    CHECK("B total 解析正确", page.m_total == 14342,
          "total=%d", page.m_total);
    CHECK("B 行数正确", page.m_count == 1, "count=%d", page.m_count);
    if (page.m_count <= 0) {
        XClassDelete((XClass*)bytes);
        return;
    }
    row = &page.m_rows[0];
    CHECK("B ts 取自第 0 列（epoch 毫秒）",
          row->m_ts == 1794201311849LL, "ts=%lld", (long long)row->m_ts);
    /* 显示列（2026-11-10 起第 8 位是水温，其后整体右移一位）：
     *   3=freq 4=current 5=rpm 6=turb1 7=turb2 **8=水温**
     *   9=flow(+unit) 10=flow_total(+unit) 11=press 12=temp 13=hum
     *   14=DI 15=DO */
    CHECK("B freq 取自第 3 列",
          row->m_cells[3] && fabs(atof(row->m_cells[3]) - 46.10) < 0.01,
          "cells[3]=%s", row->m_cells[3]);
    CHECK("B turb1 取自第 6 列",
          row->m_cells[6] && fabs(atof(row->m_cells[6]) - 12.31) < 0.01,
          "cells[6]=%s", row->m_cells[6]);
    CHECK("B turb2 取自第 7 列",
          row->m_cells[7] && fabs(atof(row->m_cells[7]) - 3.26) < 0.01,
          "cells[7]=%s", row->m_cells[7]);
    /* [水温 2026-11-10] 线上在最后一格（18），显示插在第 8 位、按 %.1f 格式化
     * （19.35 → "19.4"）；原值旁路保留全精度 19.35。 */
    CHECK("B 水温取自第 8 列（线上第 18 格）",
          row->m_cells[8] && strcmp(row->m_cells[8], "19.4") == 0
          && row->m_hasNum[5] && fabs(row->m_num[5] - 19.35) < 1e-6,
          "cells[8]=%s num[5]=%f", row->m_cells[8], row->m_num[5]);
    CHECK("B press 取自第 11 列",
          row->m_cells[11] && fabs(atof(row->m_cells[11]) - 0.499) < 0.001,
          "cells[11]=%s", row->m_cells[11]);
    CHECK("B temp 取自第 12 列",
          row->m_cells[12] && fabs(atof(row->m_cells[12]) - 26.5) < 0.05,
          "cells[12]=%s", row->m_cells[12]);
    CHECK("B hum 取自第 13 列",
          row->m_cells[13] && fabs(atof(row->m_cells[13]) - 55.0) < 0.05,
          "cells[13]=%s", row->m_cells[13]);
    /* 图表原值旁路：num[] 必须是数值（图表按它画，不是按显示格）。 */
    {
        bool ok = true;
        int n;
        for (n = 0; n < HSF_HISTORY_NUM_COUNT; ++n) {
            if (!row->m_hasNum[n]) ok = false;
        }
        CHECK("B 数值列原值齐全（图表按原值画）", ok);
    }
    XClassDelete((XClass*)bytes);
}


/**
 * @brief §C：表格「双击列标题排序」的**核心逻辑**（页面侧实现，框架只负责
 *        发出双击信号）。
 * @details 口径与 Python 客户端 _header_dblclicked 一致：新列⇒降序；
 *          同列降⇒升序；同列升⇒取消排序。排序作用于当前页。
 *          [为什么不直接测 GUI 事件] 双击要靠真实鼠标输入合成，CI 里不稳；
 *          这里锁住"状态机 + 重排 + 写回"这段纯逻辑，双击信号的发射在
 *          框架侧（XHeaderView：sectionDoubleClicked 此前只有声明没有
 *          发射点，已补上）。
 */
static void testC_tableSort(void)
{
    XTableWidget* table;
    HsfTableSort sort;
    static const char* kRows[3][2] = {
        { "2026-11-09 13:00:00", "30.00" },
        { "2026-11-09 12:00:00", "10.00" },
        { "2026-11-09 14:00:00", "20.00" }
    };
    int r;

    XPrintf("--- C 表格双击列标题排序（核心逻辑）---" "\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("C 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    for (r = 0; r < 3; ++r) {
        int c;
        for (c = 0; c < 2; ++c) {
            XTableWidgetItem* it = XTableWidgetItem_create_utf8(kRows[r][c], 0);
            XTableWidget_setItem(table, r, c, it);
        }
    }

    HsfTableSort_init(&sort);
    CHECK("C 初始为不排序", sort.m_col == -1);

    /* 新列 ⇒ 降序（数值列 30/10/20 ⇒ 30、20、10）。 */
    HsfTableSort_toggle(&sort, 1);
    HsfTableSort_apply(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("C 新列⇒降序（首行=最大 30.00）",
              v && strcmp(v, "30.00") == 0, "首行=%s", v ? v : "-");
    }
    /* 同列再点 ⇒ 升序（10、20、30）。 */
    HsfTableSort_toggle(&sort, 1);
    HsfTableSort_apply(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("C 同列降⇒升序（首行=最小 10.00）",
              v && strcmp(v, "10.00") == 0, "首行=%s", v ? v : "-");
    }
    /* 第三次 ⇒ 取消排序（回到服务端原序：第一行仍是 13:00:00）。 */
    HsfTableSort_toggle(&sort, 1);
    HsfTableSort_apply(&sort, table);
    CHECK("C 同列升⇒取消排序", sort.m_col == -1);
    /* [取消排序的口径] 助手层只负责退出排序态；"回到服务端原序"由页面
     * 负责（重取当前页）——表里此刻已是排过序的行，本地没有原序可还原。 */
    CHECK("C 取消排序后助手层退出排序态（页面再重取原序）",
          sort.m_col == -1);
    XClassDelete((XClass*)table);
}

/* ============================================================================
 * §D 框架层「Qt 对齐补全」回归锁
 * ==========================================================================*/

/**
 * @brief 表头信号探针（记录收到的载荷）。
 */
typedef struct {
    int m_dblCount;        /**< sectionDoubleClicked 收到次数。 */
    int m_dblSection;      /**< 收到的段号。 */
    int m_enteredCount;    /**< sectionEntered 收到次数。 */
    int m_enteredSection;  /**< 收到的段号。 */
    int m_clickedCount;    /**< sectionClicked 收到次数。 */
    int m_clickedSection;  /**< 收到的段号。 */
} HeaderProbe;

static HeaderProbe g_hdrProbe;

static void hdrDblSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    g_hdrProbe.m_dblCount++;
    if (args) {
        XVarList_start(args);
        g_hdrProbe.m_dblSection = XVarList_arg(args, int);
    }
}

static void hdrEnteredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    g_hdrProbe.m_enteredCount++;
    if (args) {
        XVarList_start(args);
        g_hdrProbe.m_enteredSection = XVarList_arg(args, int);
    }
}

static void hdrClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    g_hdrProbe.m_clickedCount++;
    if (args) {
        XVarList_start(args);
        g_hdrProbe.m_clickedSection = XVarList_arg(args, int);
    }
}

/**
 * @brief 在表头指定 x 处合成一次鼠标事件。
 * @note  **每次都必须重新 init 事件**：XMouseEvent_init 把坐标**拷贝**进
 *        事件体，改外部的 XPoint 不会改事件里的位置（首版踩过：复用同一
 *        个事件对象改 pos.x，结果事件里还是旧坐标 ⇒ 双击/悬停断言假红）。
 */
static void headerMouse(XHeaderView* hh, XEventType type, int x)
{
    XMouseEvent me;
    XPoint pos;
    if (!hh) return;
    XPoint_init(&pos, 0, 0);
    pos.x = x;
    pos.y = 5;
    XMouseEvent_init(&me, type,
                     (type == XEVENT_TYPE_MOUSE_MOVE) ? XMouseButton_NoButton
                                                     : XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, pos);
    switch (type) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        XWidget_mousePressEvent_base((XWidget*)hh, (XEvent*)&me);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        XWidget_mouseReleaseEvent_base((XWidget*)hh, (XEvent*)&me);
        break;
    case XEVENT_TYPE_MOUSE_MOVE:
        XWidget_mouseMoveEvent_base((XWidget*)hh, (XEvent*)&me);
        break;
    default:
        break;
    }
}

/**
 * @brief 在表头第 col 段中部模拟一次「按下 + 释放」。
 * @note  位置取段中点：避开分隔线手柄命中区（段尾 ±3px）。
 */
static void headerClickSection(XHeaderView* hh, int col)
{
    int start;
    int size;
    int x;
    if (!hh || col < 0) return;
    start = XHeaderView_sectionPosition(hh, col);
    size = XHeaderView_sectionSize(hh, col);
    if (start < 0) return;
    x = start + size / 2;
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, x);
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, x);
}

/**
 * @brief D1：sectionDoubleClicked 必须**真的有发射点**且载荷是段号。
 * @details 2026-11-09 之前该句柄全仓只有声明没有发射点 ⇒ 页面侧接了也
 *          收不到 ⇒ C 端「双击列标题排序」从未生效。这里按真实交互路径
 *          （同一段 400ms 内的第二次按压）验，而不是直接调发射函数。
 */
static void testD1_sectionDoubleClickedEmits(void)
{
    XHeaderView* hh;

    XPrintf("--- D1 表头双击信号（真实发射点 + 载荷）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D1 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 3);
    CHECK("D1 表头段数为 3", XHeaderView_count(hh) == 3);
    XObject_connect_1((XObject*)hh,
                      XSignal(XHeaderView_sectionDoubleClicked_signal),
                      (XObject*)hh, hdrDblSlot, XConnectionType_Direct);

    /* 第一次按压：只记按压，不发射双击。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D1 单次按压不发射双击", g_hdrProbe.m_dblCount == 0);
    /* 第二次按压（同一段、400ms 内）⇒ 发射双击。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D1 第二次按压发射 sectionDoubleClicked",
          g_hdrProbe.m_dblCount == 1,
          "count=%d", g_hdrProbe.m_dblCount);
    CHECK("D1 双击载荷=段号（0）", g_hdrProbe.m_dblSection == 0,
          "section=%d", g_hdrProbe.m_dblSection);
    /* 三击不连发（第二次按压已把双击"消耗掉"）。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D1 三击不连发", g_hdrProbe.m_dblCount == 1,
          "count=%d", g_hdrProbe.m_dblCount);
    /* 换段按压 ⇒ 不算双击上一段（段号变了）。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionPosition(hh, 2) +
                XHeaderView_sectionSize(hh, 2) / 2);
    CHECK("D1 换段按压不触发双击", g_hdrProbe.m_dblCount == 1,
          "count=%d", g_hdrProbe.m_dblCount);
    XObject_disconnect_1((XObject*)hh,
                         XSignal(XHeaderView_sectionDoubleClicked_signal),
                         (XObject*)hh, hdrDblSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D2：sectionEntered 必须真的有发射点且载荷是段号。
 * @details 悬停进入**新段**才发；同一段内移动不重复发（Qt entered 语义）。
 */
static void testD2_sectionEnteredEmits(void)
{
    XHeaderView* hh;

    XPrintf("--- D2 表头悬停进入信号（真实发射点 + 载荷）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D2 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 3);
    XObject_connect_1((XObject*)hh,
                      XSignal(XHeaderView_sectionEntered_signal),
                      (XObject*)hh, hdrEnteredSlot, XConnectionType_Direct);

    /* 悬停进入第 0 段。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_MOVE, XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D2 悬停进入第 0 段发射 sectionEntered",
          g_hdrProbe.m_enteredCount == 1,
          "count=%d", g_hdrProbe.m_enteredCount);
    CHECK("D2 悬停载荷=段号（0）", g_hdrProbe.m_enteredSection == 0,
          "section=%d", g_hdrProbe.m_enteredSection);
    /* 同一段内再移动一次 ⇒ 不重复发。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_MOVE,
                XHeaderView_sectionSize(hh, 0) / 2 + 1);
    CHECK("D2 同一段内移动不重复发", g_hdrProbe.m_enteredCount == 1,
          "count=%d", g_hdrProbe.m_enteredCount);
    /* 移到第 1 段 ⇒ 再发一次，载荷=1。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_MOVE,
                XHeaderView_sectionPosition(hh, 1) +
                XHeaderView_sectionSize(hh, 1) / 2);
    CHECK("D2 移入新段再次发射", g_hdrProbe.m_enteredCount == 2,
          "count=%d", g_hdrProbe.m_enteredCount);
    CHECK("D2 第二次载荷=段号（1）", g_hdrProbe.m_enteredSection == 1,
          "section=%d", g_hdrProbe.m_enteredSection);
    XObject_disconnect_1((XObject*)hh,
                         XSignal(XHeaderView_sectionEntered_signal),
                         (XObject*)hh, hdrEnteredSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D3 + D4：释放时翻转排序指示器；指示器段号/方向可读。
 * @details Qt 口径（qheaderview.cpp mouseReleaseEvent 尾段）：
 *          释放于按压段 ⇒ flipSortIndicator → setSortIndicator
 *          · 点**别的**段 ⇒ 切到该段、**升序**；
 *          · 点**同一**段 ⇒ 升/降**翻转**。
 */
static void testD3_sortIndicatorFlipsOnRelease(void)
{
    XHeaderView* hh;

    XPrintf("--- D3/D4 排序指示器（释放时翻转 + 可读）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D3 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 3);
    XHeaderView_setSectionsClickable(hh, true);
    CHECK("D3 表头段可点", XHeaderView_sectionsClickable(hh));
    XObject_connect_1((XObject*)hh, XSignal(XHeaderView_sectionClicked_signal),
                      (XObject*)hh, hdrClickedSlot, XConnectionType_Direct);

    /* 首次点第 0 段 ⇒ 指示器落在该段、升序。 */
    headerClickSection(hh, 0);
    CHECK("D3 首次点击发出 sectionClicked", g_hdrProbe.m_clickedCount == 1,
          "count=%d", g_hdrProbe.m_clickedCount);
    CHECK("D3 sectionClicked 载荷=段号（0）",
          g_hdrProbe.m_clickedSection == 0,
          "section=%d", g_hdrProbe.m_clickedSection);
    CHECK("D3 指示器落在第 0 段",
          XHeaderView_sortIndicatorSection(hh) == 0,
          "section=%d", XHeaderView_sortIndicatorSection(hh));
    CHECK("D3 首次点击指示器=升序（0）",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Ascending,
          "order=%d", XHeaderView_sortIndicatorOrder(hh));
    CHECK("D3 指示器可见", XHeaderView_isSortIndicatorShown(hh));

    /* 再点同一段 ⇒ 翻转成降序。 */
    headerClickSection(hh, 0);
    CHECK("D4 同段再点指示器翻转=降序（1）",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Descending,
          "order=%d", XHeaderView_sortIndicatorOrder(hh));
    CHECK("D4 指示器仍在第 0 段",
          XHeaderView_sortIndicatorSection(hh) == 0);

    /* 点别的段 ⇒ 切到该段、升序。 */
    headerClickSection(hh, 2);
    CHECK("D4 点别的段 ⇒ 指示器切到第 2 段",
          XHeaderView_sortIndicatorSection(hh) == 2,
          "section=%d", XHeaderView_sortIndicatorSection(hh));
    CHECK("D4 点别的段 ⇒ 方向=升序",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Ascending,
          "order=%d", XHeaderView_sortIndicatorOrder(hh));

    /* 程序化 setSortIndicator ⇒ 指示器可读（不依赖点击路径）。 */
    XHeaderView_setSortIndicator(hh, 1, XHeaderViewSortOrder_Descending);
    CHECK("D4 setSortIndicator 后段号可读",
          XHeaderView_sortIndicatorSection(hh) == 1);
    CHECK("D4 setSortIndicator 后方向可读",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Descending);
    XObject_disconnect_1((XObject*)hh, XSignal(XHeaderView_sectionClicked_signal),
                         (XObject*)hh, hdrClickedSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D5：setSortingEnabled(true) ⇒ 表头段单击 ⇒ 行按该列重排。
 * @details Qt 口径（QTableView::setSortingEnabled）：开启后表头的
 *          sectionClicked 连到 sortByColumn ⇒ 单击表头按该列排序。
 *          本框架的排序落在 XTableWidget（它同时拥有 model 与单元格）。
 *          同时验 Qt 的"空值下沉"（该列为空的行排在最后）。
 */
static void testD5_sortingEnabledSortsByColumn(void)
{
    XTableWidget* table;
    XHeaderView* hh;

    XPrintf("--- D5 开启排序后单击表头 ⇒ 按该列重排 ---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D5 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    {
        static const char* kVals[3] = { "30.00", "10.00", "20.00" };
        int r;
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a =
                XTableWidgetItem_create_utf8("row", 0);
            XTableWidgetItem* b =
                XTableWidgetItem_create_utf8(kVals[r], 0);
            XTableWidget_setItem(table, r, 0, a);
            XTableWidget_setItem(table, r, 1, b);
        }
    }
    hh = XTableView_horizontalHeader(&table->m_base);
    CHECK("D5 水平表头可取", hh != NULL);
    if (!hh) { XClassDelete((XClass*)table); return; }

    XTableWidget_setSortingEnabled(table, true);
    CHECK("D5 开启排序后表头段变为可点", XHeaderView_sectionsClickable(hh));

    /* 单击第 1 段（数值列）⇒ 按该列升序（首次点击 ⇒ 升序）。 */
    headerClickSection(hh, 1);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D5 单击表头 ⇒ 首行=该列最小值 10.00",
              v && strcmp(v, "10.00") == 0, "首行=%s", v ? v : "-");
        it = XTableWidget_item(table, 2, 1);
        v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D5 单击表头 ⇒ 末行=该列最大值 30.00",
              v && strcmp(v, "30.00") == 0, "末行=%s", v ? v : "-");
    }
    /* 再点同一段 ⇒ 指示器翻转为降序 ⇒ 排序方向随之翻转。 */
    headerClickSection(hh, 1);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D5 再点同段 ⇒ 降序（首行=最大值 30.00）",
              v && strcmp(v, "30.00") == 0, "首行=%s", v ? v : "-");
    }

    /* 空值下沉（Qt QTableModel::sort 口径）：该列为空的行排到最后。 */
    {
        static const char* kVals[3] = { "40.00", "50.00", "60.00" };
        int r;
        XTableWidget_clearContents(table);
        XTableWidget_setColumnCount(table, 2);
        XTableWidget_setRowCount(table, 3);
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
            XTableWidget_setItem(table, r, 0, a);
            if (r != 1) {
                XTableWidgetItem* b =
                    XTableWidgetItem_create_utf8(kVals[r], 0);
                XTableWidget_setItem(table, r, 1, b);
            }
            /* r==1：该列**没有条目**（空值） */
        }
        XTableWidget_sortItems(table, 1, 0);   /* 升序 */
        {
            const XTableWidgetItem* it = XTableWidget_item(table, 2, 1);
            CHECK("D5 空值下沉（空行排最后）", it == NULL,
                  "末行=%s",
                  it ? XTableWidgetItem_text_toUtf8(it) : "(空)");
            it = XTableWidget_item(table, 0, 1);
            CHECK("D5 非空行按升序排列（首行=40.00）",
                  it && strcmp(XTableWidgetItem_text_toUtf8(it),
                               "40.00") == 0);
        }
    }
    XClassDelete((XClass*)table);
}

/**
 * @brief D6：setSortingEnabled(false) ⇒ 表头段不再可点（不再触发排序）。
 */
static void testD6_sortingDisabledStopsSorting(void)
{
    XTableWidget* table;
    XHeaderView* hh;
    const char* before[3];
    int r;

    XPrintf("--- D6 关闭排序后表头段不再可点 ---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D6 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    {
        static const char* kVals[3] = { "30.00", "10.00", "20.00" };
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
            XTableWidgetItem* b = XTableWidgetItem_create_utf8(kVals[r], 0);
            XTableWidget_setItem(table, r, 0, a);
            XTableWidget_setItem(table, r, 1, b);
        }
    }
    hh = XTableView_horizontalHeader(&table->m_base);
    if (!hh) { XClassDelete((XClass*)table); return; }

    XTableWidget_setSortingEnabled(table, true);
    XTableWidget_setSortingEnabled(table, false);
    CHECK("D6 关闭排序后表头段不可点", !XHeaderView_sectionsClickable(hh));

    for (r = 0; r < 3; ++r) {
        const XTableWidgetItem* it = XTableWidget_item(table, r, 1);
        before[r] = it ? XTableWidgetItem_text_toUtf8(it) : "";
    }
    headerClickSection(hh, 1);
    {
        bool same = true;
        for (r = 0; r < 3; ++r) {
            const XTableWidgetItem* it = XTableWidget_item(table, r, 1);
            const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
            if (strcmp(v, before[r]) != 0) same = false;
        }
        CHECK("D6 关闭排序后单击表头不再重排行", same);
    }
    XClassDelete((XClass*)table);
}

/**
 * @brief D7：信号函数的 **NULL 取 id 约定**（今天这里崩过）。
 * @details XObject_signalIdOf 以 self=NULL 调用信号函数取其返回值（真实
 *          地址）。故所有信号函数必须在 if(self) 守卫内短路：
 *          · 以 NULL 调用 ⇒ 不崩、**不发射**；
 *          · 取到的 id 非 0，且与连接用的 XSignal(...) 一致（连接可达）。
 *          2026-11-09 实测：xhv_emitInt 未判 self ⇒ 「连信号」这一步就
 *          解引用 NULL ⇒ 客户端启动即段错误。
 */
static void testD7_nullSignalIdContract(void)
{
    XHeaderView* hh;
    size_t id = 0;
    int before = 0;

    XPrintf("--- D7 信号函数 NULL 取 id 约定（不崩、不发射）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));

    /* ① 以 NULL 调用取 id：不得崩。 */
    id = XObject_signalIdOf(
        (void* (*)(XObject*))XHeaderView_sectionDoubleClicked_signal);
    CHECK("D7 NULL 取 id 不崩且非 0", id != 0, "id=%llu",
          (unsigned long long)id);
    CHECK("D7 XSignal 取到的 id 与 signalIdOf 一致（连接可达）",
          (size_t)XSignal(XHeaderView_sectionDoubleClicked_signal) == id);

    /* ② 以 NULL 调用信号函数：不得发射。 */
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D7 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 2);
    XObject_connect_1((XObject*)hh,
                      XSignal(XHeaderView_sectionDoubleClicked_signal),
                      (XObject*)hh, hdrDblSlot, XConnectionType_Direct);
    before = g_hdrProbe.m_dblCount;
    XHeaderView_sectionDoubleClicked_signal(NULL, 7);   /* 必须不崩 */
    CHECK("D7 NULL 调用不发射", g_hdrProbe.m_dblCount == before,
          "count=%d", g_hdrProbe.m_dblCount);
    XHeaderView_sectionEntered_signal(NULL, 7);
    XHeaderView_sectionClicked_signal(NULL, 7);
    CHECK("D7 NULL 调用（其余信号）同样不崩不发射",
          g_hdrProbe.m_dblCount == before);
    XObject_disconnect_1((XObject*)hh,
                         XSignal(XHeaderView_sectionDoubleClicked_signal),
                         (XObject*)hh, hdrDblSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D8：app 层三态策略（新列⇒降 / 同列⇒升 / 再点⇒取消）+ 指示器联动。
 * @details 这是 app 层助手 HsfTableSort 的**策略**部分（框架只提供
 *          sortItems 原语与排序指示器）。排序指示器必须跟着策略走，
 *          否则界面上的箭头与实际行序对不上。
 */
static void testD8_appThreeStatePolicy(void)
{
    XTableWidget* table;
    XHeaderView* hh;
    HsfTableSort sort;

    XPrintf("--- D8 app 层三态排序策略 + 指示器联动 ---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D8 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    {
        static const char* kVals[3] = { "30.00", "10.00", "20.00" };
        int r;
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
            XTableWidgetItem* b = XTableWidgetItem_create_utf8(kVals[r], 0);
            XTableWidget_setItem(table, r, 0, a);
            XTableWidget_setItem(table, r, 1, b);
        }
    }
    hh = XTableView_horizontalHeader(&table->m_base);
    if (!hh) { XClassDelete((XClass*)table); return; }

    HsfTableSort_init(&sort);
    CHECK("D8 初始：不排序", sort.m_col == -1);

    /* ① 新列 ⇒ 降序。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("D8 新列⇒排序列=1 且为降序",
          sort.m_col == 1 && sort.m_desc);
    HsfTableSort_apply(&sort, table);
    HsfTableSort_updateIndicator(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D8 新列⇒降序（首行=30.00）",
              v && strcmp(v, "30.00") == 0, "首行=%s", v ? v : "-");
    }
    CHECK("D8 指示器跟随策略（第 1 段 / 降序 / 可见）",
          XHeaderView_sortIndicatorSection(hh) == 1 &&
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Descending &&
          XHeaderView_isSortIndicatorShown(hh));

    /* ② 同列再点 ⇒ 升序。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("D8 同列⇒升序（m_desc=false）",
          sort.m_col == 1 && !sort.m_desc);
    HsfTableSort_apply(&sort, table);
    HsfTableSort_updateIndicator(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D8 同列⇒升序（首行=10.00）",
              v && strcmp(v, "10.00") == 0, "首行=%s", v ? v : "-");
    }
    CHECK("D8 指示器跟随策略（第 1 段 / 升序）",
          XHeaderView_sortIndicatorSection(hh) == 1 &&
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Ascending);

    /* ③ 第三次 ⇒ 取消排序（退出排序态）。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("D8 再点⇒取消排序（退出排序态）", sort.m_col == -1);
    HsfTableSort_apply(&sort, table);
    HsfTableSort_updateIndicator(&sort, table);
    CHECK("D8 取消排序后指示器隐藏",
          !XHeaderView_isSortIndicatorShown(hh));

    /* ④ 换列 ⇒ 重新按"新列⇒降序"起算。 */
    HsfTableSort_toggle(&sort, 0);
    CHECK("D8 换列⇒新列降序", sort.m_col == 0 && sort.m_desc);
    XClassDelete((XClass*)table);
}

/* ============================================================================
 * §E 交叉测试（层与层、端与端互相验证）
 * ==========================================================================*/

/** @brief 取表格某列的显示文本（用于比对行序）。 */
static void tableColumnTexts(XTableWidget* table, int col, char out[][40],
                             int maxRows)
{
    int r;
    for (r = 0; r < maxRows; ++r) {
        const XTableWidgetItem* it =
            XTableWidget_item(table, r, col);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        snprintf(out[r], 40, "%s", v ? v : "(空)");
    }
}

/**
 * @brief E1(a)：框架原语 ↔ app 策略（同批数据、同列 ⇒ 同一行序）。
 * @details 两条路径：
 *          · 框架原语：直接 XTableWidget_sortItems(table, col, order)；
 *          · app 策略：HsfTableSort_toggle（决定列与方向）+ HsfTableSort_apply。
 *          app 策略最终也是调框架原语，故两者必须给出**完全一致**的行序；
 *          若哪天 app 层自己写了一套排序，这里就会红。
 */
static void testE1_frameworkPrimitiveVsAppPolicy(void)
{
    XTableWidget* tFrame;
    XTableWidget* tApp;
    HsfTableSort sort;
    char a[3][40];
    char b[3][40];
    static const char* kVals[3] = { "30.00", "10.00", "20.00" };
    int r;

    XPrintf("--- E1(a) 框架排序原语 ↔ app 三态策略 ---\n");
    tFrame = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    tApp = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("E1 两张表建成", tFrame && tApp);
    if (!tFrame || !tApp) {
        if (tFrame) XClassDelete((XClass*)tFrame);
        if (tApp) XClassDelete((XClass*)tApp);
        return;
    }
    XTableWidget_setColumnCount(tFrame, 2);
    XTableWidget_setRowCount(tFrame, 3);
    XTableWidget_setColumnCount(tApp, 2);
    XTableWidget_setRowCount(tApp, 3);
    for (r = 0; r < 3; ++r) {
        XTableWidgetItem* i1 = XTableWidgetItem_create_utf8("row", 0);
        XTableWidgetItem* i2 = XTableWidgetItem_create_utf8(kVals[r], 0);
        XTableWidgetItem* i3 = XTableWidgetItem_create_utf8("row", 0);
        XTableWidgetItem* i4 = XTableWidgetItem_create_utf8(kVals[r], 0);
        XTableWidget_setItem(tFrame, r, 0, i1);
        XTableWidget_setItem(tFrame, r, 1, i2);
        XTableWidget_setItem(tApp, r, 0, i3);
        XTableWidget_setItem(tApp, r, 1, i4);
    }

    /* ① 首击（新列）⇒ 降序。 */
    HsfTableSort_init(&sort);
    HsfTableSort_toggle(&sort, 1);
    XTableWidget_sortItems(tFrame, 1, sort.m_desc ? 1 : 0);
    HsfTableSort_apply(&sort, tApp);
    tableColumnTexts(tFrame, 1, a, 3);
    tableColumnTexts(tApp, 1, b, 3);
    CHECK("E1 降序：两条路径行序一致",
          strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 &&
          strcmp(a[2], b[2]) == 0,
          "框架=%s/%s/%s app=%s/%s/%s", a[0], a[1], a[2], b[0], b[1], b[2]);
    CHECK("E1 降序结果正确（30/20/10）",
          strcmp(a[0], "30.00") == 0 && strcmp(a[1], "20.00") == 0 &&
          strcmp(a[2], "10.00") == 0);

    /* ② 再点（同列）⇒ 升序。 */
    HsfTableSort_toggle(&sort, 1);
    XTableWidget_sortItems(tFrame, 1, sort.m_desc ? 1 : 0);
    HsfTableSort_apply(&sort, tApp);
    tableColumnTexts(tFrame, 1, a, 3);
    tableColumnTexts(tApp, 1, b, 3);
    CHECK("E1 升序：两条路径行序一致",
          strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 &&
          strcmp(a[2], b[2]) == 0);
    CHECK("E1 升序结果正确（10/20/30）",
          strcmp(a[0], "10.00") == 0 && strcmp(a[1], "20.00") == 0 &&
          strcmp(a[2], "30.00") == 0);

    /* ③ 第三次 ⇒ 取消排序：两条路径都不得再动行序。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("E1 第三次⇒app 退出排序态", sort.m_col == -1);
    HsfTableSort_apply(&sort, tApp);
    tableColumnTexts(tFrame, 1, a, 3);
    tableColumnTexts(tApp, 1, b, 3);
    CHECK("E1 取消排序：两条路径行序一致且保持不变",
          strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 &&
          strcmp(a[2], b[2]) == 0 &&
          strcmp(a[0], "10.00") == 0 && strcmp(a[1], "20.00") == 0 &&
          strcmp(a[2], "30.00") == 0,
          "框架=%s/%s/%s app=%s/%s/%s", a[0], a[1], a[2], b[0], b[1], b[2]);

    XClassDelete((XClass*)tFrame);
    XClassDelete((XClass*)tApp);
}

/* ---------------------------------------------------------------------------
 * §E(b)(c) 固化的服务端载荷
 * -------------------------------------------------------------------------*/

/**
 * @brief Python 服务端**真实产出**的历史应答载荷（0x0118，limit=2）。
 * @details [来源与生成方式] 2026-11-09 在本机起一个**隔离**的 Python
 *          服务端实例（临时 cfg，不碰仓库里的 hsf_data.db 与 cfg/ 账户库）：
 *            cd HighSpeedFilter/server_py
 *            python -m hsfsrv --sim --port <临时端口> \
 *                  --config <临时目录>/cfg/hsf_server_py.json
 *          用 client_py 的 XLinkClient 连上（admin/admin），
 *          发 {"offset":0,"limit":2} 的 0x0118 请求，把**原始应答 BSON
 *          字节**落盘后按十六进制固化到本文件。
 *          下面这串十六进制就是那份原始字节（逐字节，未做任何改写）。
 *
 *          [载荷内容（解码后）]
 *            ok=true total=10 offset=0 limit=2 dbRows=10
 *            cols = [ts,mode,vfd_state,freq_hz,current_a,rpm,turb1,turb2,
 *                    flow,flow_unit,flow_total,total_unit,press_mpa,
 *                    press_note,temp,hum,di,do]
 *            rows[0] = [1794213564972,'手动','停机',0.0,0.0,0.0,12.43,3.22,
 *                       36.3917,'m³/h',0.453,'m³',0.4992469879518072,'',
 *                       26.5,55.0,21845,256]
 *            rows[1] = [1794213569966,'手动','停机',0.0,0.0,0.0,12.68,3.15,
 *                       36.7705,'m³/h',0.505,'m³',0.4992469879518072,'',
 *                       26.5,55.0,21845,256]
 *          注意单位是 **m³/h**（真机/服务端口径，不是 ASCII 的 m3/h）。
 */
static const char kPyHistoryHex[] =
    "45030000086f6b000110746f74616c000a000000106f66667365740000000000106c696d6974000200000004636f"
    "6c73000801000002300003000000747300023100050000006d6f6465000232000a0000007666645f737461746500"
    "02330008000000667265715f687a000234000a00000063757272656e745f61000235000400000072706d00023600"
    "060000007475726231000237000600000074757262320002380005000000666c6f77000239000a000000666c6f77"
    "5f756e697400023130000b000000666c6f775f746f74616c00023131000b000000746f74616c5f756e6974000231"
    "32000a00000070726573735f6d706100023133000b00000070726573735f6e6f746500023134000500000074656d"
    "7000023135000400000068756d0002313600030000006469000231370003000000646f000004726f777300ab0100"
    "00043000d00000001230002c5276bfa101000002310007000000e6898be58aa80002320007000000e5819ce69cba"
    "000133000000000000000000013400000000000000000001350000000000000000000136005c8fc2f528dc284001"
    "3700c3f5285c8fc20940013800eeebc03923324240023900060000006dc2b32f680001313000cba145b6f3fddc3f"
    "02313100040000006dc2b30001313200064a78a3a9f3df3f023133000100000000013134000000000000803a4001"
    "3135000000000000804b401031360055550000103137000001000000043100d0000000123000ae6576bfa1010000"
    "02310007000000e6898be58aa80002320007000000e5819ce69cba00013300000000000000000001340000000000"
    "0000000001350000000000000000000136005c8fc2f5285c29400137003333333333330940013800b4c876be9f62"
    "4240023900060000006dc2b32f680001313000295c8fc2f528e03f02313100040000006dc2b30001313200064a78"
    "a3a9f3df3f023133000100000000013134000000000000803a40013135000000000000804b401031360055550000"
    "10313700000100000000106462526f7773000a000000126c6174657374547300ae6576bfa1010000126561726c69"
    "657374547300a5b575bfa10100001077696e646f775374617274547300000000001277696e646f77456e64547300"
    "0d6776bfa101000000";

/**
 * @brief C 服务端**真实产出**的历史应答载荷（0x0118，limit=2）。
 * @details [来源与生成方式] 2026-11-09 起一个**隔离**的 C 服务端实例：
 *          把 bin\HsfServer.exe **复制**到临时目录再跑（它的落盘目录是
 *          <程序目录>/cfg/，复制即隔离 ⇒ 不碰仓库里的 bin/cfg）：
 *            HsfServer.exe --sim <临时端口>
 *          同样用 client_py 的 XLinkClient 连上（admin/admin），发
 *          {"offset":0,"limit":2} 的 0x0118 请求，取**原始应答 BSON 字节**。
 *          下面这串十六进制就是那份原始字节（逐字节，未做任何改写）。
 *
 *          [载荷内容（解码后）]
 *            ok=true total=10 offset=0 limit=2 dbRows=10
 *            cols 与 Python 端**逐字相同**（18 个列名、同序）。
 *            rows[0] = [1794213617000,'手动','停机',0.0,0.0,0.0,12.2773,
 *                       3.2553,35.8531,'m³/h',0.0,'m³',0.4992,'',26.5,55.0,
 *                       21845,0]
 *            rows[1] = [1794213622000,'手动','停机',0.0,0.0,0.0,12.5273,
 *                       3.1911,35.771,'m³/h',0.0,'m³',0.4992,'',26.5,55.0,
 *                       21845,0]
 *          [与 Python 端的差异只在数据本身]（C 端模拟变频器停机时
 *          freq/current/rpm 为 0、正累计为 0；Python 端略有差别），
 *          **形状**（键集合/列序/逐列类型）必须完全一致——那是"两端同口径"
 *          的机器可查证据。
 */
static const char kCHistoryHex[] =
    "5d030000086f6b000110746f74616c000a000000106f66667365740000000000106c696d69740002000000126462"
    "526f7773000a00000000000000126c6174657374547300f03077bfa1010000126561726c69657374547300407d76"
    "bfa10100001277696e646f77537461727454730000000000000000001277696e646f77456e64547300784477bfa1"
    "01000004636f6c73000801000002300003000000747300023100050000006d6f6465000232000a0000007666645f"
    "73746174650002330008000000667265715f687a000234000a00000063757272656e745f61000235000400000072"
    "706d00023600060000007475726231000237000600000074757262320002380005000000666c6f77000239000a00"
    "0000666c6f775f756e697400023130000b000000666c6f775f746f74616c00023131000b000000746f74616c5f75"
    "6e697400023132000a00000070726573735f6d706100023133000b00000070726573735f6e6f7465000231340005"
    "00000074656d7000023135000400000068756d0002313600030000006469000231370003000000646f000004726f"
    "777300bb010000043000d8000000123000681d77bfa101000002310007000000e6898be58aa80002320007000000"
    "e5819ce69cba00013300000000000000000001340000000000000000000135000000000000000000013600925cfe"
    "43fa8d2840013700b459f5b9da0a0a40013800da1b7c6132ed4140023900060000006dc2b32f6800013130000000"
    "00000000000002313100040000006dc2b30001313200de718a8ee4f2df3f02313300010000000001313400000000"
    "0000803a40013135000000000000804b4012313600555500000000000012313700000000000000000000043100d8"
    "000000123000f03077bfa101000002310007000000e6898be58aa80002320007000000e5819ce69cba0001330000"
    "0000000000000001340000000000000000000135000000000000000000013600925cfe43fa0d2940013700f31fd2"
    "6f5f870940013800a69bc420b0e24140023900060000006dc2b32f68000131300000000000000000000231310004"
    "0000006dc2b30001313200de718a8ee4f2df3f023133000100000000013134000000000000803a40013135000000"
    "000000804b40123136005555000000000000123137000000000000000000000000";

/**
 * @brief 十六进制串 → 字节数组（调用方负责 XFree_System 释放）。
 */
static uint8_t* hexDecode(const char* hex, uint32_t* outLen)
{
    size_t n;
    uint8_t* buf;
    size_t i;
    if (!hex || !outLen) return NULL;
    n = strlen(hex) / 2u;
    if (n == 0) return NULL;
    buf = (uint8_t*)XMalloc_System(n);
    if (!buf) return NULL;
    for (i = 0; i < n; ++i) {
        int hi = 0;
        int lo = 0;
        char a = hex[i * 2];
        char b = hex[i * 2 + 1];
        hi = (a >= '0' && a <= '9') ? (a - '0')
           : (a >= 'a' && a <= 'f') ? (a - 'a' + 10) : -1;
        lo = (b >= '0' && b <= '9') ? (b - '0')
           : (b >= 'a' && b <= 'f') ? (b - 'a' + 10) : -1;
        if (hi < 0 || lo < 0) { XFree_System(buf); return NULL; }
        buf[i] = (uint8_t)((hi << 4) | lo);
    }
    *outLen = (uint32_t)n;
    return buf;
}

/**
 * @brief E2(b)：C 客户端解析器 ↔ Python 服务端真实载荷。
 * @details 逐列取值必须取对列（不是预格式化文本、列序不串、行间不串）。
 */
static void testE2_clientParserVsPythonPayload(void)
{
    uint8_t* bytes;
    uint32_t len = 0;
    HsfHistoryPage page;
    const HsfHistoryRow* r0;
    const HsfHistoryRow* r1;

    XPrintf("--- E2(b) C 客户端解析器 ↔ Python 服务端真实载荷 ---\n");
    bytes = hexDecode(kPyHistoryHex, &len);
    CHECK("E2 固化载荷解码成功", bytes != NULL && len > 0);
    if (!bytes) return;
    memset(&page, 0, sizeof(page));
    CHECK("E2 真实载荷解析成功",
          HsfHistoryPage_parse(bytes, len, &page));
    CHECK("E2 total 解析正确（10）", page.m_total == 10,
          "total=%d", page.m_total);
    CHECK("E2 dbRows 解析正确（10）", page.m_dbRows == 10,
          "dbRows=%lld", (long long)page.m_dbRows);
    CHECK("E2 offset 回显（0）", page.m_offset == 0);
    CHECK("E2 limit 回显（2）", page.m_limit == 2,
          "limit=%d", page.m_limit);
    CHECK("E2 行数正确（2）", page.m_count == 2,
          "count=%d", page.m_count);
    if (page.m_count < 2) {
        XFree_System(bytes);
        return;
    }
    r0 = &page.m_rows[0];
    r1 = &page.m_rows[1];

    /* --- 时间戳（第 0 列）--- */
    CHECK("E2 第 0 行 ts 取自第 0 列",
          r0->m_ts == 1794213564972LL,
          "ts=%lld", (long long)r0->m_ts);
    CHECK("E2 第 1 行 ts 取自第 0 列（行间不串）",
          r1->m_ts == 1794213569966LL,
          "ts=%lld", (long long)r1->m_ts);

    /* --- 文本列（1=mode 2=vfd_state）--- */
    CHECK("E2 mode 取自第 1 列",
          strcmp(r0->m_cells[1], "手动") == 0, "cells[1]=%s", r0->m_cells[1]);
    CHECK("E2 vfd_state 取自第 2 列",
          strcmp(r0->m_cells[2], "停机") == 0, "cells[2]=%s", r0->m_cells[2]);

    /* --- 数值列（3=freq 4=current 5=rpm 6=turb1 7=turb2）--- */
    CHECK("E2 freq 取自第 3 列", strcmp(r0->m_cells[3], "0.00") == 0,
          "cells[3]=%s", r0->m_cells[3]);
    CHECK("E2 current 取自第 4 列", strcmp(r0->m_cells[4], "0.00") == 0,
          "cells[4]=%s", r0->m_cells[4]);
    CHECK("E2 rpm 取自第 5 列", strcmp(r0->m_cells[5], "0") == 0,
          "cells[5]=%s", r0->m_cells[5]);
    CHECK("E2 turb1 取自第 6 列", strcmp(r0->m_cells[6], "12.43") == 0,
          "cells[6]=%s", r0->m_cells[6]);
    CHECK("E2 turb2 取自第 7 列", strcmp(r0->m_cells[7], "3.22") == 0,
          "cells[7]=%s", r0->m_cells[7]);
    /* --- 带单位的列（9=flow 10=flow_total；单位是 m³/h 不是 m3/h）--- */
    CHECK("E2 flow 取自第 9 列并带真单位",
          strcmp(r0->m_cells[9], "36.39 m³/h") == 0,
          "cells[9]=%s", r0->m_cells[9]);
    CHECK("E2 flow_total 取自第 10 列并带真单位",
          strcmp(r0->m_cells[10], "0.45 m³") == 0,
          "cells[10]=%s", r0->m_cells[10]);
    /* --- 压差/温湿/位图（水温插在第 8 位后整体右移一位）--- */
    CHECK("E2 press 取自第 11 列", strcmp(r0->m_cells[11], "0.499") == 0,
          "cells[11]=%s", r0->m_cells[11]);
    CHECK("E2 temp 取自第 12 列", strcmp(r0->m_cells[12], "26.5") == 0,
          "cells[12]=%s", r0->m_cells[12]);
    CHECK("E2 hum 取自第 13 列", strcmp(r0->m_cells[13], "55.0") == 0,
          "cells[13]=%s", r0->m_cells[13]);
    CHECK("E2 di 取自第 14 列", strcmp(r0->m_cells[14], "0x00005555") == 0,
          "cells[14]=%s", r0->m_cells[14]);
    CHECK("E2 do 取自第 15 列", strcmp(r0->m_cells[15], "0x00000100") == 0,
          "cells[15]=%s", r0->m_cells[15]);

    /* --- 图表原值旁路：必须是**数值**且逐列对应 ---
     * [这份载荷是 18 列的**老**载荷] 固化于 2026-11-09（加 water_temp 之前）
     * ⇒ 第 19 格不存在：水温显示 "--"、m_hasNum[5]=false。这正是要锁的
     * **向后兼容**口径（客户端不能因为少一格就整行错列/崩溃）。新列的正向
     * 解析由 §B 的合成载荷（含 water_temp）覆盖。 */
    {
        bool allHasExceptWater = true;
        int n;
        for (n = 0; n < HSF_HISTORY_NUM_COUNT; ++n) {
            if (n == 5) continue;            /* 5 = 水温（老载荷没有这一格） */
            if (!r0->m_hasNum[n]) allHasExceptWater = false;
        }
        CHECK("E2 图表原值齐全（水温除外；老载荷兼容）", allHasExceptWater);
        CHECK("E2 老载荷没有水温格 ⇒ 显示 -- 且原值缺失",
              strcmp(r0->m_cells[8], "--") == 0 && !r0->m_hasNum[5],
              "cells[8]=%s hasNum[5]=%d", r0->m_cells[8],
              (int)r0->m_hasNum[5]);
        CHECK("E2 原值 freq=0（列 3）", fabs(r0->m_num[0]) < 1e-9);
        CHECK("E2 原值 turb1=12.43（列 6）",
              fabs(r0->m_num[3] - 12.43) < 1e-6,
              "num[3]=%f", r0->m_num[3]);
        CHECK("E2 原值 turb2=3.22（列 7）",
              fabs(r0->m_num[4] - 3.22) < 1e-6,
              "num[4]=%f", r0->m_num[4]);
        /* [水温在 num[] 的第 5 位] num[] 按**显示列序**排（显示第 8 位 =
         * 水温）⇒ 其后各列下标整体后移一位。老载荷该位缺失（上面已断言）。 */
        CHECK("E2 原值 flow=36.3917（列 8）",
              fabs(r0->m_num[6] - 36.3917) < 1e-4,
              "num[6]=%f", r0->m_num[6]);
        CHECK("E2 原值 flow_total=0.453（列 10）",
              fabs(r0->m_num[7] - 0.453) < 1e-6,
              "num[7]=%f", r0->m_num[7]);
        CHECK("E2 原值 press=0.4992469879518072（列 12）",
              fabs(r0->m_num[8] - 0.4992469879518072) < 1e-12,
              "num[8]=%.15f", r0->m_num[8]);
        CHECK("E2 原值 temp=26.5（列 14）",
              fabs(r0->m_num[9] - 26.5) < 1e-9);
        CHECK("E2 原值 hum=55.0（列 15）",
              fabs(r0->m_num[10] - 55.0) < 1e-9);
        /* 第 1 行的值必须不同 ⇒ 行间不串。 */
        CHECK("E2 第 1 行原值 turb1=12.68（行间不串）",
              fabs(r1->m_num[3] - 12.68) < 1e-6,
              "num[3]=%f", r1->m_num[3]);
        CHECK("E2 第 1 行原值 flow=36.7705（行间不串）",
              fabs(r1->m_num[6] - 36.7705) < 1e-4,
              "num[6]=%f", r1->m_num[6]);
    }
    XFree_System(bytes);
}

/**
 * @brief BSON 值的"类型类"（两端载荷同形比对用）。
 */
static int bsonValueKind(const XBsonValue* v)
{
    XBsonType t;
    if (!v) return -1;
    t = XBsonValue_type(v);
    switch (t) {
    case XBSON_TYPE_NULL:      return 0;   /* 空 */
    case XBSON_TYPE_STRING:    return 1;   /* 文本 */
    case XBSON_TYPE_DOUBLE:
    case XBSON_TYPE_INT32:
    case XBSON_TYPE_INT64:
    case XBSON_TYPE_DATETIME:  return 2;   /* 数值 */
    default:                   return 3;   /* 其它 */
    }
}

/**
 * @brief E3(c)：C 服务端 ↔ Python 服务端：两份历史应答**逐键逐列同形**。
 * @details 比三样东西：
 *          ① 键集合一致（两端的包络键必须一样多、一样名）；
 *          ② 列序一致（cols 数组逐项相同）；
 *          ③ 逐列类型一致（数值列两端都是数值、文本列两端都是文本）。
 *          任一端改了形状（改了列名/列序/把数值预格式化成文本），这里就红。
 */
/* [口径] XBsonArray 的元素容器是**内嵌**的 XVector（首成员），故按仓库
 * 既有写法（Test/XDataTest/XJsonBsonCoverageTest.c）显式转成 XVector
 * 视图取元素；直接用 XBsonArray_count_base/at_base 会因指针类型不兼容
 * 触发 C4133。 */
#define HSF_ARR_COUNT(a) XVector_size_base((const XContainer*)(a))
#define HSF_ARR_AT(a, i) XVector_at_base((const XVector*)(a), (i))

static void testE3_cServerVsPyServerPayloadShape(void)
{
    uint8_t* pyb;
    uint8_t* cb;
    uint32_t pyLen = 0;
    uint32_t cLen = 0;
    XBsonDocument* pydoc = NULL;
    XBsonDocument* cdoc = NULL;
    XVector* pyKeys = NULL;
    XVector* cKeys = NULL;

    XPrintf("--- E3(c) C 服务端 ↔ Python 服务端 载荷同形 ---\n");
    pyb = hexDecode(kPyHistoryHex, &pyLen);
    cb = hexDecode(kCHistoryHex, &cLen);
    CHECK("E3 两份固化载荷解码成功", pyb && cb);
    if (!pyb || !cb) {
        if (pyb) XFree_System(pyb);
        if (cb) XFree_System(cb);
        return;
    }
    pydoc = XBsonDocument_create();
    cdoc = XBsonDocument_create();
    if (!pydoc || !cdoc) {
        CHECK("E3 文档对象建成", false);
        goto done;
    }
    if (!XBsonDocument_from_bytes(pydoc, pyb, pyLen) ||
        !XBsonDocument_from_bytes(cdoc, cb, cLen)) {
        CHECK("E3 两份载荷解析成功", false);
        goto done;
    }
    CHECK("E3 两份载荷解析成功", true);

    /* ① 键集合一致（顺序可不同 ⇒ 按集合比）。 */
    pyKeys = XBsonDocument_keys_base(pydoc);
    cKeys = XBsonDocument_keys_base(cdoc);
    CHECK("E3 键集合可取", pyKeys && cKeys);
    if (pyKeys && cKeys) {
        const size_t pn = XVector_size_base((const XContainer*)pyKeys);
        const size_t cn = XVector_size_base((const XContainer*)cKeys);
        bool same = (pn == cn);
        size_t i;
        XPrintf("[INFO] E3 Python 端键 %u 个 / C 端键 %u 个\n",
                (unsigned)pn, (unsigned)cn);
        for (i = 0; same && i < pn; ++i) {
            const XString* k = (const XString*)
                XVector_at_base(pyKeys, (int64_t)i);
            const char* ku = k ? XString_toUtf8(k) : NULL;
            if (!ku || !XBsonDocument_contains_keyUtf8(cdoc, ku)) {
                same = false;
                XPrintf("[INFO] E3 Python 独有键：%s\n", ku ? ku : "?");
            }
        }
        for (i = 0; same && i < cn; ++i) {
            const XString* k = (const XString*)
                XVector_at_base(cKeys, (int64_t)i);
            const char* ku = k ? XString_toUtf8(k) : NULL;
            if (!ku || !XBsonDocument_contains_keyUtf8(pydoc, ku)) {
                same = false;
                XPrintf("[INFO] E3 C 端独有键：%s\n", ku ? ku : "?");
            }
        }
        CHECK("E3 键集合一致（两端包络同形）", same);
    }

    /* ② 列序一致。 */
    {
        const XBsonArray* pyCols = NULL;
        const XBsonArray* cCols = NULL;
        XBsonValue* v = XBsonDocument_value_keyUtf8(pydoc, "cols");
        if (v) pyCols = XBsonValue_toArray(v);
        v = XBsonDocument_value_keyUtf8(cdoc, "cols");
        if (v) cCols = XBsonValue_toArray(v);
        CHECK("E3 两端都有 cols 数组", pyCols && cCols);
        if (pyCols && cCols) {
            const size_t pn = HSF_ARR_COUNT(pyCols);
            const size_t cn = HSF_ARR_COUNT(cCols);
            bool same = (pn == cn) && (pn == 18);
            size_t i;
            CHECK("E3 cols 列数一致且为 18", same,
                  "py=%llu c=%llu", (unsigned long long)pn,
                  (unsigned long long)cn);
            for (i = 0; same && i < pn; ++i) {
                const XBsonValue* a = HSF_ARR_AT(pyCols, (int64_t)i);
                const XBsonValue* b = HSF_ARR_AT(cCols, (int64_t)i);
                const XString* sa = a ? XBsonValue_toString(a) : NULL;
                const XString* sb = b ? XBsonValue_toString(b) : NULL;
                const char* ua = sa ? XString_toUtf8(sa) : NULL;
                const char* ub = sb ? XString_toUtf8(sb) : NULL;
                if (!ua || !ub || strcmp(ua, ub) != 0) {
                    same = false;
                    XPrintf("[INFO] E3 列序不一致 @%lld：py=%s c=%s\n",
                            (long long)i, ua ? ua : "(无)", ub ? ub : "(无)");
                }
            }
            CHECK("E3 列序逐项一致", same);
        }
    }

    /* ③ 逐列类型一致 + 行宽一致。 */
    {
        const XBsonArray* pyRows = NULL;
        const XBsonArray* cRows = NULL;
        XBsonValue* v = XBsonDocument_value_keyUtf8(pydoc, "rows");
        if (v) pyRows = XBsonValue_toArray(v);
        v = XBsonDocument_value_keyUtf8(cdoc, "rows");
        if (v) cRows = XBsonValue_toArray(v);
        CHECK("E3 两端都有 rows 数组", pyRows && cRows);
        if (pyRows && cRows) {
            const size_t pn = HSF_ARR_COUNT(pyRows);
            const size_t cn = HSF_ARR_COUNT(cRows);
            size_t i;
            bool widthOk = true;
            bool kindOk = true;
            for (i = 0; i < pn && i < cn; ++i) {
                const XBsonValue* pa = HSF_ARR_AT(pyRows, (int64_t)i);
                const XBsonValue* pb = HSF_ARR_AT(cRows, (int64_t)i);
                const XBsonArray* ra = pa ? XBsonValue_toArray(pa) : NULL;
                const XBsonArray* rb = pb ? XBsonValue_toArray(pb) : NULL;
                size_t j;
                if (!ra || !rb) { widthOk = false; break; }
                if (HSF_ARR_COUNT(ra) != HSF_ARR_COUNT(rb)) {
                    widthOk = false;
                    break;
                }
                for (j = 0; j < HSF_ARR_COUNT(ra); ++j) {
                    const XBsonValue* va = HSF_ARR_AT(ra, (int64_t)j);
                    const XBsonValue* vb = HSF_ARR_AT(rb, (int64_t)j);
                    const int ka = bsonValueKind(va);
                    const int kb = bsonValueKind(vb);
                    if (ka != kb) {
                        kindOk = false;
                        XPrintf("[INFO] E3 类型不一致 行%llu 列%llu：py=%d c=%d\n",
                                (unsigned long long)i,
                                (unsigned long long)j, ka, kb);
                    }
                }
            }
            /* [两份都是 18 列的老载荷] 固化于 2026-11-09（water_temp 之前），
             * 这里比的是"两端**同宽同形**"，不是"等于某个固定列数"。 */
            CHECK("E3 两端每行宽度一致", widthOk);
            CHECK("E3 逐列类型一致（数值列是数值、文本列是文本）", kindOk);
        }
    }

done:
    if (pyKeys) XClassDelete((XClass*)pyKeys);
    if (cKeys) XClassDelete((XClass*)cKeys);
    if (pydoc) XClassDelete((XClass*)pydoc);
    if (cdoc) XClassDelete((XClass*)cdoc);
    if (pyb) XFree_System(pyb);
    if (cb) XFree_System(cb);
}

/**
 * @brief E4(d)：框架 ↔ app 的分线稳定性（同一时刻同一寄存器）。
 * @details Hub 的读组/回填（框架层，XModbusHub_value）与设备站读到的值
 *          （app 层，HsfServerIoTempHum_values）必须一致：
 *          · 框架层看到的是**原始寄存器字**（含负温折返编码）；
 *          · app 层看到的是**工程值**（0.1 单位定点，负温已解码）。
 *          两者的换算关系由 app 层的 HsfServerIoTempHum_decodeTemp/
 *          decodeHum 定义 ⇒ 这里验"框架读到的字"经该换算后**正好等于**
 *          app 层读到的值（层间口径漂移即红）。
 */
static void testE4_frameworkVsAppLineConsistency(void)
{
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    HsfServerBus bus;
    HsfServerIoTempHum station;
    struct XVirtualModelTempHum* model = NULL;
    XModbusVirtualClient* client = NULL;
    XModbusMapping* map = NULL;
    const uint16_t vbase = 0x0100;
    const int unitId = 4;
    uint16_t rawT = 0;
    uint16_t rawH = 0;
    int32_t t10 = 0;
    int32_t h10 = 0;
    bool tv = false;
    bool hv = false;
    int waited;

    XPrintf("--- E4(d) 框架 Hub 回填 ↔ app 设备站读值 ---\n");
    memset(&bus, 0, sizeof(bus));
    HsfServerIoTempHum_init(&station);

    model = XVirtualModelTempHum_create();
    CHECK("E4 温湿度模型建成", model != NULL);
    if (!model) goto e4_done;
    /* 取一组**能验出折返编码**的值：-25.0℃ ⇒ 原字 10250；64.8%RH。 */
    XVirtualModelTempHum_set(model, 10250, 648);

    client = XModbusVirtualClient_create(XVirtualModel_base(model));
    CHECK("E4 虚拟客户端建成", client != NULL);
    if (!client) goto e4_done;
    if (!XModbusVirtualClient_bindUnit(client, unitId,
                                       XVirtualModel_base(model))) {
        CHECK("E4 从站模型绑定成功", false);
        goto e4_done;
    }
    CHECK("E4 总线接入注入客户端",
          HsfServerBus_openWithClient(&bus, "回归总线",
                                      (XModbusClient*)client, 500, 1));
    client = NULL;   /* 所有权已移交 Hub */
    map = HsfServerBus_createMapping(&bus);
    CHECK("E4 映射表建成", map != NULL);
    if (!map) goto e4_done;
    CHECK("E4 温湿度站装配成功",
          HsfServerIoTempHum_mount(&station, &bus, map, unitId, vbase, 3000));
    CHECK("E4 总线启动（映射提交 + 轮询拉起）",
          HsfServerBus_start(&bus, map, 30));
    map = NULL;      /* 所有权已移交 Hub */

    /* 等首帧回填。 */
    for (waited = 0; waited < 2000; waited += 20) {
        uint16_t probe = 0;
        if (XModbusHub_value(HsfServerBus_hub(&bus), XModbusInputRegisters,
                             vbase, &probe) && probe == 10250) {
            break;
        }
        spinMs(20);
    }
    CHECK("E4 框架层已回填温度原始字（10250）",
          XModbusHub_value(HsfServerBus_hub(&bus), XModbusInputRegisters,
                           vbase, &rawT) && rawT == 10250,
          "raw=%u", (unsigned)rawT);
    CHECK("E4 框架层已回填湿度原始字（648）",
          XModbusHub_value(HsfServerBus_hub(&bus), XModbusInputRegisters,
                           (uint16_t)(vbase + 1), &rawH) && rawH == 648,
          "raw=%u", (unsigned)rawH);

    /* app 层：设备站读到的工程值。 */
    CHECK("E4 设备站取到值",
          HsfServerIoTempHum_values(&station, &t10, &h10, &tv, &hv));
    CHECK("E4 设备站温度有效", tv);
    CHECK("E4 设备站湿度有效", hv);

    /* ★ 交叉：框架读到的原始字 经 app 的换算 正好等于 app 读到的值。 */
    CHECK("E4 交叉：框架原始字 ⇒ app 工程值（温度 -250 = -25.0℃）",
          t10 == HsfServerIoTempHum_decodeTemp(rawT),
          "app=%d decode(raw=%u)=%d", (int)t10, (unsigned)rawT,
          (int)HsfServerIoTempHum_decodeTemp(rawT));
    CHECK("E4 交叉：框架原始字 ⇒ app 工程值（湿度 648 = 64.8%RH）",
          h10 == HsfServerIoTempHum_decodeHum(rawH),
          "app=%d decode(raw=%u)=%d", (int)h10, (unsigned)rawH,
          (int)HsfServerIoTempHum_decodeHum(rawH));
    CHECK("E4 温度折返解码正确（-25.0℃）", t10 == -250,
          "t10=%d", (int)t10);
    CHECK("E4 湿度解码正确（64.8%RH）", h10 == 648,
          "h10=%d", (int)h10);
    /* 同一时刻同一寄存器：框架读到的字与 app 解码用的是同一个虚拟地址。 */
    CHECK("E4 分线一致：app 有效性与框架新鲜度同步",
          XModbusHub_pointValid(HsfServerBus_hub(&bus),
                                XModbusInputRegisters, vbase, 3000) == tv);

e4_done:
    if (map) XClassDelete((XClass*)map);
    /* 站是**栈上**对象：必须显式 deinit，否则 XObject 自身的资源（信号槽
     * 管理器等）不释放（deinit 内部会再调一次 unmount，幂等）。 */
    XClassDeinit((XClass*)&station);
    HsfServerBus_stop(&bus);
    if (client) XClassDelete((XClass*)client);
    /* 虚拟模型不是 XClass（无 destroy 实现）：按分配槽位释放，见
     * hubFixtureFree 的同款注记。 */
    if (model) XFree_System(model);
#else
    XPrintf("--- E4(d) 框架 ↔ app 分线一致性（本构建未开工业层，跳过）---\n");
#endif
}

/* ============================================================================
 * §F 覆盖面扩容
 * ==========================================================================*/

/**
 * @brief XPrintf 输出捕获（用于断言框架的可观测日志）。
 */
typedef struct {
    char*  m_buf;      /**< 累积缓冲（XMalloc_System 分配）。 */
    size_t m_len;      /**< 已写长度。 */
    size_t m_cap;      /**< 缓冲容量。 */
    bool   m_failed;   /**< 分配失败过。 */
} LogCapture;

static int64_t logCaptureWrite(void* userData, const char* data, size_t size)
{
    LogCapture* c = (LogCapture*)userData;
    if (!c || !data) return -1;
    if (c->m_len + size + 1 > c->m_cap) {
        size_t need = c->m_len + size + 1;
        size_t cap = c->m_cap ? c->m_cap : 1024;
        char* nb;
        while (cap < need) cap *= 2;
        nb = (char*)XMalloc_System(cap);
        if (!nb) { c->m_failed = true; return -1; }
        if (c->m_buf) {
            memcpy(nb, c->m_buf, c->m_len);
            XFree_System(c->m_buf);
        }
        c->m_buf = nb;
        c->m_cap = cap;
    }
    memcpy(c->m_buf + c->m_len, data, size);
    c->m_len += size;
    c->m_buf[c->m_len] = '\0';
    return (int64_t)size;
}

/** @brief 释放捕获缓冲。 */
static void logCaptureFree(LogCapture* c)
{
    if (!c) return;
    if (c->m_buf) { XFree_System(c->m_buf); c->m_buf = NULL; }
    c->m_len = 0;
    c->m_cap = 0;
}

/**
 * @brief F1 + F2：读组失败的**可观测日志** 与 **块级陈旧诊断**。
 * @details 2026-11-09 之前这两条路径**完全静默**（只加计数、诊断一条都
 *          不打）⇒ 现场只看到"没数据"，查不到是哪一组、哪条命令、什么
 *          错误码，也查不到读组压根没发（被三道闸门静默跳过）。
 *          这里用 XPrintf 的输出重定向把框架日志抓下来逐字断言。
 */
static void testF1F2_failureObservabilityAndStallDiag(void)
{
    HubFixture fx;
    XModbusMapping* map;
    LogCapture cap;
    XPrintfOutputScope scope;

    XPrintf("--- F1/F2 失败可观测日志 + 块级陈旧诊断 ---\n");
    memset(&cap, 0, sizeof(cap));
    memset(&scope, 0, sizeof(scope));

    CHECK("F1 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("F1 映射段登记（DI 17 点 ⇒ 必回异常帧）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);

    /* 把框架日志抓下来。**重定向必须在连上/起轮询之前建**：首次泵就会
     * 打"轮询停滞"诊断（块从未回填 ⇒ 立即判定陈旧），晚建就抓不到。 */
    CHECK("F1 日志重定向建立",
          XPrintf_outputPush(&scope, logCaptureWrite, &cap));
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(400);
    XPrintf_outputPop(&scope);

    XPrintf("[INFO] F1 抓到的框架日志 %u 字节\n", (unsigned)cap.m_len);
    if (cap.m_buf && cap.m_len > 0) XPrintf("[INFO] ---- 框架日志 ----\n%s\n",
                                            cap.m_buf);
    CHECK("F1 失败时框架确实打了日志（不再是静默）",
          cap.m_buf && cap.m_len > 0);
    if (cap.m_buf && cap.m_len > 0) {
        CHECK("F1 异常应答有专门的可观测日志（含组/起始/数量/异常码）",
              strstr(cap.m_buf, "异常应答") != NULL);
        CHECK("F1 读组失败有可观测日志（含 unit/类型/起始/数量/错误码）",
              strstr(cap.m_buf, "读组失败") != NULL);
        CHECK("F1 轮询停滞诊断被触发（含逐块陈旧信息）",
              strstr(cap.m_buf, "轮询停滞") != NULL);
        CHECK("F2 停滞诊断逐块列出陈旧块",
              strstr(cap.m_buf, "陈旧块") != NULL);
        CHECK("F2 停滞诊断列出读组与跳过原因",
              strstr(cap.m_buf, "组[") != NULL);
    }
    /* 计数侧的可观测：失败必须记账。 */
    CHECK("F1 失败被计数（pollErrCount>0）",
          XModbusHub_pollErrCount(fx.hub, fx.devId) > 0);
    logCaptureFree(&cap);
    hubFixtureFree(&fx);
}

/**
 * @brief F3：deviceWorstUnitAge 的两种语义。
 * @details ① 某从站**从未上报** ⇒ age = UINT64_MAX（"没有数据"）；
 *          ② 某从站**曾上报后掉线** ⇒ age = 距最后一次回填的毫秒数
 *             （有限、且随时间增长）——"曾经有、现在不更新了"。
 *          两种语义必须能区分：看门狗据此区分"从没连上"与"连上后掉线"。
 */
static void testF3_worstUnitAgeTwoSemantics(void)
{
    HubFixture fx;
    XModbusMapping* map;
    int worstUnit = -1;
    uint64_t worst = 0;
    uint64_t worst2 = 0;

    XPrintf("--- F3 deviceWorstUnitAge 两种语义 ---\n");

    /* ① 从未上报（读组必失败 ⇒ 块从未回填）。 */
    CHECK("F3 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("F3 映射段登记（DI 17 点 ⇒ 必失败）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(300);
    CHECK("F3 语义①：从未上报 ⇒ age = UINT64_MAX",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst)
          && worst == UINT64_MAX,
          "age=%llu", (unsigned long long)worst);
    CHECK("F3 语义①：从未上报时点级新鲜度判为无效",
          !XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x100, 3000));
    hubFixtureFree(&fx);

    /* ② 曾上报后掉线。 */
    CHECK("F3 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("F3 映射段登记（DI 2 点，正常）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(400);
    CHECK("F3 语义②前：确实上报过（点级新鲜）",
          XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x100, 3000));
    /* 掉线：断开全部设备 ⇒ 泵被"离线"门槛挡住，不再有回填。 */
    XModbusHub_disconnectAll(fx.hub);
    spinMs(150);
    CHECK("F3 语义②：曾上报后掉线 ⇒ age 有限（≠ UINT64_MAX）",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst)
          && worst != UINT64_MAX,
          "age=%llu", (unsigned long long)worst);
    CHECK("F3 语义②：age 已超过 100ms（确实在变老）", worst >= 100,
          "age=%llu", (unsigned long long)worst);
    spinMs(400);
    CHECK("F3 语义②：age 随时间增长（不是冻结的）",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst2)
          && worst2 > worst,
          "age1=%llu age2=%llu", (unsigned long long)worst,
          (unsigned long long)worst2);
    hubFixtureFree(&fx);
}

/**
 * @brief F4：「只续写不续轮询」的节拍不被放大。
 * @details 场景：一条读组（正常）+ 一条**永远失败**的写段（远端越界），
 *          脏区间常驻 ⇒ 每拍都会走"写收口→续泵"路径。
 *          修复前：收口尾部调的是"写+轮询"的统一泵 ⇒ 写被拒后同拍又发一
 *          帧**读** ⇒ 每拍两帧读（读节拍被放大一倍），并与「读写交替让
 *          路」的交替计数同拍加两次、奇偶不变。
 *          修复后：收口只发写、且失败后**同拍不再重发** ⇒ 读节拍回到
 *          轮询周期量级。
 *          判据：同样的窗口内，"有失败写"的读组完成数不得比"无写"的
 *          基线多出一倍。
 */
static void testF4_writeFailureDoesNotAmplifyPollTempo(void)
{
    HubFixture fxBase;
    HubFixture fxWrite;
    XModbusMapping* map;
    uint32_t baseReads = 0;
    uint32_t writeReads = 0;
    const int pollMs = 20;
    const int windowMs = 1200;

    XPrintf("--- F4 写持续失败时读节拍不被放大 ---\n");

    /* 基线：只有一条正常读组。 */
    CHECK("F4 基线装配", hubFixtureMake(&fxBase));
    if (fxBase.devId < 0) { hubFixtureFree(&fxBase); return; }
    map = XModbusMapping_create();
    XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                               fxBase.devId, 1, 0, 0, 0);
    XModbusHub_setMapping(fxBase.hub, map);
    XModbusHub_setPollAreas(fxBase.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fxBase.hub, pollMs);
    XModbusHub_setPollInterval(fxBase.hub, fxBase.devId, pollMs);
    XModbusHub_connectAll(fxBase.hub);
    XModbusHub_startPolling(fxBase.hub);
    spinMs(windowMs);
    baseReads = XModbusHub_pollOkCount(fxBase.hub, fxBase.devId);
    hubFixtureFree(&fxBase);

    /* 对照：同一条读组 + 一条永远失败的写段（远端 30..31 越界）。 */
    CHECK("F4 对照装配", hubFixtureMake(&fxWrite));
    if (fxWrite.devId < 0) { hubFixtureFree(&fxWrite); return; }
    map = XModbusMapping_create();
    XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                               fxWrite.devId, 1, 0, 0, 0);
    XModbusMapping_addRangeEx(map, XModbusCoils, 0x200, 2, fxWrite.devId, 1,
                               30, 0, 0);
    XModbusHub_setMapping(fxWrite.hub, map);
    XModbusHub_setPollAreas(fxWrite.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fxWrite.hub, pollMs);
    XModbusHub_setPollInterval(fxWrite.hub, fxWrite.devId, pollMs);
    XModbusHub_connectAll(fxWrite.hub);
    XModbusHub_startPolling(fxWrite.hub);
    {
        uint16_t vals[2];
        vals[0] = 1;
        vals[1] = 1;
        XModbusHub_writeValues(fxWrite.hub, XModbusCoils, 0x200, 2, vals);
    }
    spinMs(windowMs);
    writeReads = XModbusHub_pollOkCount(fxWrite.hub, fxWrite.devId);
    hubFixtureFree(&fxWrite);

    XPrintf("[INFO] F4 基线读组完成=%u，带失败写的读组完成=%u"
            "（窗口 %d ms，轮询 %d ms）\n",
            (unsigned)baseReads, (unsigned)writeReads, windowMs, pollMs);
    CHECK("F4 基线确实在轮询（读组完成数 > 0）", baseReads > 0);
    CHECK("F4 带失败写时读组仍在推进（没被饿死）", writeReads > 0);
    /* 上界：写持续失败不得把读节拍放大到基线的 1.5 倍以上。 */
    CHECK("F4 读节拍未被放大（≤ 基线×1.5 + 5）",
          writeReads <= (baseReads * 3u) / 2u + 5u,
          "基线=%u 对照=%u", (unsigned)baseReads, (unsigned)writeReads);
}

/* ============================================================================
 * §I 快照/控制命令的「请求-应答」兜底通道（2026-11-09 根修）
 * ==========================================================================*/

/**
 * @brief §I 用：文档 → BSON 字节（顺带释放文档壳，失败也给 NULL）。
 */
static XByteArray* iBsonBytes(XBsonDocument* doc)
{
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief §I 用：把一条构造好的应答喂给线上同一条业务分发。
 */
static bool iDispatch(uint16_t cmd, XByteArray* bytes)
{
    return HsfLinkClient_testDispatchResponse(
        cmd, bytes ? XByteArray_constData(bytes) : NULL,
        bytes ? (uint32_t)XByteArray_size_base(bytes) : 0);
}

/* 各主题的「订阅者被通知」计数（回调注册后由分发路径累加）。 */
static int s_iAiCb = 0;
static int s_iFmCb = 0;
static int s_iTurbCb = 0;
static int s_iPumpCb = 0;
static int s_iVfdCb = 0;

static void iOnAi(void* userData)   { (void)userData; ++s_iAiCb; }
static void iOnFm(void* userData)   { (void)userData; ++s_iFmCb; }
static void iOnTurb(void* userData) { (void)userData; ++s_iTurbCb; }
static void iOnPump(void* userData) { (void)userData; ++s_iPumpCb; }
static void iOnVfd(void* userData)  { (void)userData; ++s_iVfdCb; }

/**
 * @brief 模拟量全量快照应答（键表与推送帧同构：n/u{i}/r{i}/e{i}/eu{i}/en{i}/ew{i}）。
 */
static XByteArray* iBuildAiPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    char key[16];
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_FULL, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_AI_COUNT, 2);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_AI_VALIDMASK, 0x1);
    snprintf(key, sizeof(key), HSF_KEY_AI_UA_FMT, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 1500);       /* 1.500 mA */
    snprintf(key, sizeof(key), HSF_KEY_AI_RAW_FMT, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 2985);       /* mV */
    snprintf(key, sizeof(key), HSF_KEY_AI_UA_FMT, 1);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 4000);
    /* 标定四键（工程量/单位/设备名/越限说明）。 */
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_FMT, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 499);
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_UNIT_FMT, 0);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "MPa");
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NAME_FMT, 0);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "压差");
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NOTE_FMT, 0);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "断线");
    return iBsonBytes(doc);
}

/** @brief 电磁流量计快照应答（side/flow/speed/…/valid）。 */
static XByteArray* iBuildFlowMeterPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, 1);   /* B 侧 */
    XBsonDocument_insert_keyUtf8_bool(doc, "valid", true);
    XBsonDocument_insert_keyUtf8_double(doc, "flow", 36.5);
    XBsonDocument_insert_keyUtf8_double(doc, "speed", 1.25);
    XBsonDocument_insert_keyUtf8_double(doc, "forwardTotal", 119.36);
    XBsonDocument_insert_keyUtf8_double(doc, "reverseTotal", 0.5);
    XBsonDocument_insert_keyUtf8_int32(doc, "flowUnit", 1);
    XBsonDocument_insert_keyUtf8_int32(doc, "totalUnit", 1);
    XBsonDocument_insert_keyUtf8_int32(doc, "alarms", 0);
    return iBsonBytes(doc);
}

/** @brief 浊度仪组快照应答（count + v{i}_name/ntu/raw/online）。 */
static XByteArray* iBuildTurbidityPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    char key[24];
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_TURB_COUNT, 2);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "原水");
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_NTU);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 1250);       /* 12.50 NTU */
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_RAW);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 4660);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_ONLINE);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 1, HSF_KEY_TURB_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "出水");
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 1, HSF_KEY_TURB_NTU);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 320);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 1, HSF_KEY_TURB_ONLINE);
    XBsonDocument_insert_keyUtf8_bool(doc, key, false);
    return iBsonBytes(doc);
}

/** @brief 泵组快照应答（count + v{i}_name/on/ready/valid/text）。 */
static XByteArray* iBuildPumpPayload(bool on)
{
    XBsonDocument* doc = XBsonDocument_create();
    char key[24];
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_PUMP_COUNT, 1);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "进水泵");
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_ON);
    XBsonDocument_insert_keyUtf8_bool(doc, key, on);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_READY);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_VALVE_VALID);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_TEXT);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "已启动");
    return iBsonBytes(doc);
}

/**
 * @brief I1：模拟量快照应答（0x0103）必须刷新 model 并通知订阅者。
 */
static void testI1_aiSnapshotResponse(void)
{
    XByteArray* bytes = iBuildAiPayload();
    int32_t ua[HSF_LINK_AI_CH_MAX];
    uint32_t mask = 0;
    int n;
    int32_t eng = 0;
    char unit[12];
    const char* note = NULL;

    XPrintf("--- I1 模拟量快照应答 ---\n");
    CHECK("I1 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    HsfLinkClient_setAiCallback(iOnAi, NULL);
    CHECK("I1 分发成功", iDispatch(HSF_CMD_AI_SNAPSHOT, bytes));
    CHECK("I1 订阅者被通知一次（应答不再被静默丢弃）", s_iAiCb == 1,
          "cb=%d", s_iAiCb);
    memset(ua, 0, sizeof(ua));
    n = HsfLinkClient_aiValues(ua, &mask);
    CHECK("I1 通道数被应答刷新", n == 2, "n=%d", n);
    CHECK("I1 有效位掩码被应答刷新", mask == 0x1, "mask=0x%X",
          (unsigned)mask);
    CHECK("I1 工程值（μA 定点）被应答刷新", ua[0] == 1500 && ua[1] == 4000,
          "ua0=%d ua1=%d", ua[0], ua[1]);
    CHECK("I1 原始寄存器（mV）被应答刷新",
          HsfLinkClient_aiRawMv(0) == 2985, "raw0=%d",
          HsfLinkClient_aiRawMv(0));
    CHECK("I1 标定工程量被应答刷新",
          HsfLinkClient_aiEngineering(0, &eng, unit, (int)sizeof(unit), &note)
              && eng == 499 && strcmp(unit, "MPa") == 0 && note &&
              strcmp(note, "断线") == 0,
          "eng=%d unit=%s note=%s", eng, unit, note ? note : "(null)");
    CHECK("I1 设备名（全量帧 en0）被应答刷新",
          HsfLinkClient_aiName(0) && strcmp(HsfLinkClient_aiName(0), "压差") == 0,
          "name=%s", HsfLinkClient_aiName(0) ? HsfLinkClient_aiName(0) : "(null)");
    CHECK("I1 应答到达即判新鲜（自愈闭环）", HsfLinkClient_aiFresh());
    XClassDelete((XClass*)bytes);
}

/**
 * @brief I2：电磁流量计快照应答（0x0106）必须刷新对应侧的 model 并通知。
 */
static void testI2_flowMeterSnapshotResponse(void)
{
    XByteArray* bytes = iBuildFlowMeterPayload();
    const HsfLinkFlowMeter* fm;

    XPrintf("--- I2 流量计快照应答 ---\n");
    CHECK("I2 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    HsfLinkClient_setFlowMeterCallback(iOnFm, NULL);
    CHECK("I2 分发成功", iDispatch(HSF_CMD_FLOWMETER_SNAPSHOT, bytes));
    CHECK("I2 订阅者被通知一次", s_iFmCb == 1, "cb=%d", s_iFmCb);
    fm = HsfLinkClient_flowMeter(HSF_SIDE_B);
    CHECK("I2 按 side 落进 B 侧快照", fm && fm->m_received && fm->m_valid);
    if (fm) {
        CHECK("I2 瞬时流量被应答刷新",
              fabs(fm->m_flow - 36.5) < 0.001, "flow=%.3f", fm->m_flow);
        CHECK("I2 流速被应答刷新",
              fabs(fm->m_speed - 1.25) < 0.001, "speed=%.3f", fm->m_speed);
        CHECK("I2 正向累积量被应答刷新",
              fabs(fm->m_forwardTotal - 119.36) < 0.001,
              "total=%.3f", fm->m_forwardTotal);
    }
    CHECK("I2 应答到达即判新鲜（自愈闭环）",
          HsfLinkClient_flowMeterFresh(HSF_SIDE_B));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief I3：浊度仪快照应答（0x0112）必须刷新组快照并通知。
 */
static void testI3_turbiditySnapshotResponse(void)
{
    XByteArray* bytes = iBuildTurbidityPayload();
    const HsfLinkTurbiditySet* ts;

    XPrintf("--- I3 浊度仪快照应答 ---\n");
    CHECK("I3 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    HsfLinkClient_setTurbidityCallback(iOnTurb, NULL);
    CHECK("I3 分发成功", iDispatch(HSF_CMD_TURBIDITY_SNAPSHOT, bytes));
    CHECK("I3 订阅者被通知一次", s_iTurbCb == 1, "cb=%d", s_iTurbCb);
    ts = HsfLinkClient_turbidity();
    CHECK("I3 台数被应答刷新", ts && ts->m_received && ts->m_count == 2,
          "count=%d", ts ? ts->m_count : -1);
    if (ts && ts->m_count == 2) {
        CHECK("I3 第 1 台名字/NTU/在线位被应答刷新",
              strcmp(ts->m_items[0].m_name, "原水") == 0 &&
                  ts->m_items[0].m_ntu100 == 1250 &&
                  ts->m_items[0].m_online,
              "name=%s ntu=%d online=%d", ts->m_items[0].m_name,
              ts->m_items[0].m_ntu100, ts->m_items[0].m_online ? 1 : 0);
        CHECK("I3 第 1 台离线位被应答刷新",
              strcmp(ts->m_items[1].m_name, "出水") == 0 &&
                  ts->m_items[1].m_ntu100 == 320 &&
                  !ts->m_items[1].m_online,
              "name=%s ntu=%d online=%d", ts->m_items[1].m_name,
              ts->m_items[1].m_ntu100, ts->m_items[1].m_online ? 1 : 0);
    }
    CHECK("I3 应答到达即判新鲜", HsfLinkClient_turbidityFresh());
    XClassDelete((XClass*)bytes);
}

/**
 * @brief I4：泵快照应答（0x0113）必须刷新 model 并通知；
 *        失败包络（未装配/无权限）**不得**被当成「0 台泵」写进 model。
 */
static void testI4_pumpSnapshotResponse(void)
{
    XByteArray* okBytes = iBuildPumpPayload(true);
    XByteArray* errDoc;
    const HsfLinkPumps* ps;
    int countAfterOk;

    XPrintf("--- I4 泵快照应答 ---\n");
    CHECK("I4 载荷构造成功", okBytes != NULL);
    if (!okBytes) return;
    HsfLinkClient_setPumpCallback(iOnPump, NULL);
    CHECK("I4 分发成功", iDispatch(HSF_CMD_PUMP_SNAPSHOT, okBytes));
    CHECK("I4 订阅者被通知一次", s_iPumpCb == 1, "cb=%d", s_iPumpCb);
    ps = HsfLinkClient_pumps();
    CHECK("I4 台数/名字/输出状态被应答刷新",
          ps && ps->m_received && ps->m_count == 1 &&
              strcmp(ps->m_items[0].m_name, "进水泵") == 0 &&
              ps->m_items[0].m_on && ps->m_items[0].m_ready &&
              ps->m_items[0].m_valid &&
              strcmp(ps->m_items[0].m_text, "已启动") == 0,
          "count=%d name=%s on=%d", ps ? ps->m_count : -1,
          (ps && ps->m_count > 0) ? ps->m_items[0].m_name : "(none)",
          (ps && ps->m_count > 0) ? (ps->m_items[0].m_on ? 1 : 0) : -1);
    countAfterOk = ps ? ps->m_count : 0;

    /* 失败包络：服务端「进水泵组未装配」/「权限不足」走 {"ok":false,"err":...}。 */
    {
        XBsonDocument* doc = XBsonDocument_create();
        errDoc = NULL;
        if (doc) {
            XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
            XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ERR, "进水泵组未装配");
            errDoc = iBsonBytes(doc);
        }
    }
    CHECK("I4 失败包络构造成功", errDoc != NULL);
    if (errDoc) {
        CHECK("I4 失败包络分发不崩", iDispatch(HSF_CMD_PUMP_SNAPSHOT, errDoc));
        ps = HsfLinkClient_pumps();
        CHECK("I4 失败包络不覆盖已有泵快照（不变成 0 台）",
              ps && ps->m_count == countAfterOk &&
                  ps->m_items[0].m_on,
              "count=%d", ps ? ps->m_count : -1);
        CHECK("I4 失败包络不再通知订阅者（无新数据）", s_iPumpCb == 1,
              "cb=%d", s_iPumpCb);
        XClassDelete((XClass*)errDoc);
    }
    XClassDelete((XClass*)okBytes);
}

#if XVFD_ON
/**
 * @brief I5：变频器快照应答（0x0102）必须解码进 XVfdStatus 并通知。
 */
static void testI5_vfdSnapshotResponse(void)
{
    XVfdStatus st;
    int keyCount = 0;
    const uint64_t* keys = XVfdCodec_telemetryKeys(&keyCount);
    XByteArray* bytes;
    XByteArray* withSide = NULL;
    XBsonDocument* doc;
    const XVfdStatus* got;

    XPrintf("--- I5 变频器快照应答 ---\n");
    memset(&st, 0, sizeof(st));
    st.m_validMask = XVfdField_RunState | XVfdField_Running |
                     XVfdField_OutputFreqHz | XVfdField_SpeedRpm;
    st.m_valid = true;
    st.m_runState = XVfdRunState_ConstantSpeed;
    st.m_running = true;
    st.m_outputFreqHz = 46.1;
    st.m_speedRpm = 1383.0;
    /* 用**服务端同款**编解码器造载荷（键表只存在 XVfdCodec 一处）。 */
    bytes = XVfdCodec_statusToBson(&st, keys, keyCount);
    CHECK("I5 编码成功", bytes != NULL);
    if (!bytes) return;
    /* 服务端在快照上补 side 键后回包：解回文档 → 插 side → 重编码。 */
    doc = XBsonDocument_create();
    if (doc &&
        XBsonDocument_from_bytes(doc, (const uint8_t*)XByteArray_constData(bytes),
                                (size_t)XByteArray_size_base(bytes))) {
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, 0);   /* A 侧 */
        withSide = XBsonDocument_toBson(doc);
    }
    if (doc) XClassDelete((XClass*)doc);
    XClassDelete((XClass*)bytes);
    CHECK("I5 应答载荷构造成功（快照 + side）", withSide != NULL);
    if (!withSide) return;
    HsfLinkClient_setVfdCallback(iOnVfd, NULL);
    CHECK("I5 分发成功", iDispatch(HSF_CMD_VFD_SNAPSHOT, withSide));
    CHECK("I5 订阅者被通知一次", s_iVfdCb == 1, "cb=%d", s_iVfdCb);
    got = HsfLinkClient_vfdStatus();
    CHECK("I5 快照有效位被置上（应答不再被丢弃）",
          HsfLinkClient_vfdValid() && got && got->m_valid);
    if (got) {
        CHECK("I5 输出频率被应答刷新",
              fabs(got->m_outputFreqHz - 46.1) < 0.001,
              "hz=%.3f", got->m_outputFreqHz);
        CHECK("I5 转速被应答刷新",
              fabs(got->m_speedRpm - 1383.0) < 0.001,
              "rpm=%.3f", got->m_speedRpm);
        CHECK("I5 运行位被应答刷新", got->m_running);
    }
    XClassDelete((XClass*)withSide);
}
#endif /* XVFD_ON */

/**
 * @brief I6：畸形载荷必须「不崩 + 不覆盖已有 model」（五条命令全覆盖）。
 */
static void testI6_malformedPayloads(void)
{
    /* 不是 BSON 文档的 4 字节（长度前缀 = 0xEFBEADDE，远超缓冲）。 */
    static const uint8_t kGarbage[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const uint16_t cmds[5] = {
        HSF_CMD_AI_SNAPSHOT, HSF_CMD_FLOWMETER_SNAPSHOT,
        HSF_CMD_TURBIDITY_SNAPSHOT, HSF_CMD_PUMP_SNAPSHOT,
        HSF_CMD_VFD_SNAPSHOT
    };
    int32_t ua[HSF_LINK_AI_CH_MAX];
    uint32_t mask = 0;
    const HsfLinkPumps* ps = HsfLinkClient_pumps();
    const int pumpCount = ps ? ps->m_count : 0;
    const HsfLinkTurbiditySet* ts = HsfLinkClient_turbidity();
    const int turbCount = ts ? ts->m_count : 0;
    int i;

    XPrintf("--- I6 畸形载荷不崩、不覆盖 model ---\n");
    CHECK("I6 空载荷 + 非法参数被拒绝",
          !HsfLinkClient_testDispatchResponse(HSF_CMD_AI_SNAPSHOT, NULL, 4));
    for (i = 0; i < 5; ++i) {
        CHECK("I6 垃圾字节分发不崩（按命令码）", HsfLinkClient_testDispatchResponse(
                  cmds[i], kGarbage, (uint32_t)sizeof(kGarbage)));
        CHECK("I6 空载荷分发不崩（按命令码）",
              HsfLinkClient_testDispatchResponse(cmds[i], NULL, 0));
    }
    memset(ua, 0, sizeof(ua));
    CHECK("I6 畸形载荷后模拟量 model 保持原值",
          HsfLinkClient_aiValues(ua, &mask) == 2 && ua[0] == 1500 &&
              ua[1] == 4000,
          "n=%d ua0=%d", HsfLinkClient_aiValues(ua, &mask), ua[0]);
    ps = HsfLinkClient_pumps();
    ts = HsfLinkClient_turbidity();
    CHECK("I6 畸形载荷后泵/浊度 model 保持原值",
          ps && ps->m_count == pumpCount && ts && ts->m_count == turbCount,
          "pump=%d turb=%d", ps ? ps->m_count : -1, ts ? ts->m_count : -1);

    /* 失败包络（权限不足/设备未装配）：必须按「没有新数据」处理，**不得**把
     * model 清空——模拟量的 n 缺失会被解析成 0 通道，界面看起来像整页设备
     * 掉线，而实际上只是这条请求被拒。 */
    {
        XBsonDocument* doc = XBsonDocument_create();
        XByteArray* errBytes = NULL;
        if (doc) {
            XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
            XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ERR,
                                             "权限不足：需要查看遥测");
            errBytes = iBsonBytes(doc);
        }
        CHECK("I6 失败包络载荷构造成功", errBytes != NULL);
        if (errBytes) {
            CHECK("I6 模拟量失败包络分发不崩",
                  iDispatch(HSF_CMD_AI_SNAPSHOT, errBytes));
            memset(ua, 0, sizeof(ua));
            CHECK("I6 模拟量失败包络不清空 model（仍 2 通道）",
                  HsfLinkClient_aiValues(ua, &mask) == 2 && ua[0] == 1500,
                  "n=%d ua0=%d", HsfLinkClient_aiValues(ua, &mask), ua[0]);
            CHECK("I6 浊度失败包络不清零台数",
                  iDispatch(HSF_CMD_TURBIDITY_SNAPSHOT, errBytes) &&
                      HsfLinkClient_turbidity()->m_count == turbCount,
                  "turbCount=%d", HsfLinkClient_turbidity()->m_count);
            XClassDelete((XClass*)errBytes);
        }
    }
}

/**
 * @brief I7：泵控制应答（0x0114）—— 此前无应答分支，命令序列号永不推进。
 */
static void testI7_pumpControlResponse(void)
{
    const int seq0 = HsfLinkClient_commandSeq();
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    char key[24];

    XPrintf("--- I7 泵控制应答（无通用命令结果路径 ⇒ 必须显式处理）---\n");
    CHECK("I7 应答文档构造成功", doc != NULL);
    if (!doc) return;
    /* 成功应答 = {ok:true} + 整帧泵快照（服务端并帧下发）。 */
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_PUMP_COUNT, 1);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "进水泵");
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_ON);
    XBsonDocument_insert_keyUtf8_bool(doc, key, false);   /* 命令之后的真实态 */
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_READY);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_VALVE_VALID);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    bytes = iBsonBytes(doc);
    CHECK("I7 载荷构造成功", bytes != NULL);
    if (bytes) {
        CHECK("I7 分发成功", iDispatch(HSF_CMD_PUMP_CONTROL, bytes));
        CHECK("I7 命令序列号被应答推进（页面据此弹状态）",
              HsfLinkClient_commandSeq() == seq0 + 1,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        CHECK("I7 成功应答无错误文案",
              HsfLinkClient_lastCommandError()[0] == '\0',
              "err=%s", HsfLinkClient_lastCommandError());
        CHECK("I7 应答里带的泵快照被应用（on=false）",
              HsfLinkClient_pumps()->m_count == 1 &&
                  !HsfLinkClient_pumps()->m_items[0].m_on);
        XClassDelete((XClass*)bytes);
    }
}

/**
 * @brief I8：自动运行控制应答（0x0115）—— 失败原因必须能浮到状态行。
 */
static void testI8_autoControlResponse(void)
{
    const int seq0 = HsfLinkClient_commandSeq();
    XBsonDocument* doc;
    XByteArray* bytes;
    int mode = -1;

    XPrintf("--- I8 自动运行控制应答 ---\n");
    /* 成功：模式包络（与 RUN_MODE 同形）。 */
    doc = XBsonDocument_create();
    CHECK("I8 成功应答构造成功", doc != NULL);
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 1);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO, true);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_RUNNING, true);
        bytes = iBsonBytes(doc);
        CHECK("I8 成功应答分发成功",
              bytes && iDispatch(HSF_CMD_AUTO_CONTROL, bytes));
        CHECK("I8 模式/运行位被应答回填",
              HsfLinkClient_runMode(&mode) && mode == 1 &&
                  HsfLinkClient_autoRunning(&mode) && mode == 1,
              "mode=%d", mode);
        CHECK("I8 成功应答推进命令序列号",
              HsfLinkClient_commandSeq() == seq0 + 1,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        if (bytes) XClassDelete((XClass*)bytes);
    }
    /* 失败：包络带 err（服务端拒绝启动时也回显模式/运行位，但客户端只认
     * 错误文案 + 保留上次模式值）。 */
    doc = XBsonDocument_create();
    CHECK("I8 失败应答构造成功", doc != NULL);
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
        XBsonDocument_insert_keyUtf8_utf8(
            doc, HSF_KEY_ERR, "当前是手动模式：请先切到自动模式，再启动自动运行");
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 0);
        bytes = iBsonBytes(doc);
        CHECK("I8 失败应答分发成功",
              bytes && iDispatch(HSF_CMD_AUTO_CONTROL, bytes));
        CHECK("I8 失败原因浮到命令错误文案（页面据此提示）",
              HsfLinkClient_lastCommandError()[0] != '\0' &&
                  strstr(HsfLinkClient_lastCommandError(), "手动模式") != NULL,
              "err=%s", HsfLinkClient_lastCommandError());
        CHECK("I8 失败应答同样推进序列号（页面才看得到失败）",
              HsfLinkClient_commandSeq() == seq0 + 2,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        if (bytes) XClassDelete((XClass*)bytes);
    }
}

/**
 * @brief I9：未连线时自愈补拍不发请求（也不崩）。
 */
static void testI9_ensureFreshOffline(void)
{
    XPrintf("--- I9 未连线时补拍是空操作 ---\n");
    CHECK("I9 模拟量补拍：未就绪不发", !HsfLinkClient_aiEnsureFresh());
    CHECK("I9 流量计补拍：未就绪不发",
          !HsfLinkClient_flowMeterEnsureFresh());
    CHECK("I9 浊度仪补拍：未就绪不发", !HsfLinkClient_turbidityEnsureFresh());
    CHECK("I9 泵补拍：未就绪不发", !HsfLinkClient_pumpEnsureFresh());
    CHECK("I9 变频器补拍：未就绪不发", !HsfLinkClient_vfdEnsureFresh());
    /* 全主题掩码走一遍：未就绪必须整体空操作（页面激活路径不会抛错）。 */
    HsfLinkClient_ensureFreshTopics(0xFFFFFFFFu);
    CHECK("I9 全主题补拍总闸：未就绪不崩", true);
}

/* ============================================================================
 * §G 泄漏自检（回归文件自身不得漏内存）
 * ==========================================================================*/

/**
 * @brief 泄漏自检的**工作集**：把新增段落用到的对象生命周期各走一遍
 *        （表格+条目+排序、表头+信号连接、固化载荷解码+解析、Hub 夹具、
 *          读组失败日志 + 日志捕获管路）。
 * @note  [为什么现在**包含**「读组失败」+「日志捕获」路径] 这两条路径此前各有
 *        一处**框架侧固定泄漏**，2026-11-09 一并修掉并锁进本工作集：
 *        ① XModbusHub.c 的失败日志把 XModbusReply_errorString() 的**新建
 *           副本**直接丢给 printf（同一处调了两次、副本没人释放）⇒ 每条失败
 *           日志漏 2×sizeof(XString) = **192 字节**（§G 实测按轮 +192/+384/
 *           +576 线性上涨）；顺带 `%s` 拿到的是 XString* 而非 char*，日志里
 *           的"错误="一直是乱码。修法：只取一次副本、经 XString_toUtf8 取
 *           借用视图、打完 XClassDelete。
 *        ② XPrintf_outputPush/Pop 的标准输出旁路：Start 里建的队列互斥锁
 *           与队列缓冲在 Stop 里没回收，下一次 Start 直接覆盖指针 ⇒ 每对
 *           push/pop 漏 **48 字节**（XMutex）+ 队列缓冲。
 *        两条都改成"同一操作跑第二/第三轮增量为 0"后才收进工作集做长期
 *        回归锁：真泄漏会按轮线性上涨，惰性一次性初始化只涨第一轮（§G 的
 *        预热轮已吸收）。
 */
static void leakFailureLogPath(void)
{
    HubFixture fx;
    XModbusMapping* map;
    LogCapture cap;
    XPrintfOutputScope scope;

    memset(&cap, 0, sizeof(cap));
    memset(&scope, 0, sizeof(scope));
    /* 虚拟从站单次最多 16 点 ⇒ 读 17 点必回异常帧 ⇒ 触发「读组失败」日志
     * （每设备每秒限频一条；夹具每轮新建 ⇒ lastErrLogMs=0 ⇒ 恰好一条）。
     * 同时走一遍日志捕获作用域（push/pop 的管路生命周期也归本工作集）。 */
    if (!hubFixtureMake(&fx)) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    if (!map) { hubFixtureFree(&fx); return; }
    XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                              fx.devId, 1, 0, 0, 0);
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    (void)XPrintf_outputPush(&scope, logCaptureWrite, &cap);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(150);
    XPrintf_outputPop(&scope);
    logCaptureFree(&cap);
    hubFixtureFree(&fx);
    flushDeferred();
}

static void leakWorkload(void)
{
    /* ① 表格 + 条目 + 排序（D5/D6/D8/E1 的对象生命周期）。 */
    {
        XTableWidget* t = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 NULL, 0);
        if (t) {
            XHeaderView* hh;
            int r;
            XTableWidget_setColumnCount(t, 2);
            XTableWidget_setRowCount(t, 3);
            for (r = 0; r < 3; ++r) {
                XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
                XTableWidgetItem* b = XTableWidgetItem_create_utf8("30.00", 0);
                XTableWidget_setItem(t, r, 0, a);
                XTableWidget_setItem(t, r, 1, b);
            }
            hh = XTableView_horizontalHeader(&t->m_base);
            XTableWidget_setSortingEnabled(t, true);
            if (hh) headerClickSection(hh, 1);
            XTableWidget_setSortingEnabled(t, false);
            XClassDelete((XClass*)t);
        }
    }
    /* ② 表头 + 信号连接/断开（D1/D2/D3/D7）。 */
    {
        XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
        if (hh) {
            XHeaderView_setCount(hh, 3);
            XObject_connect_1((XObject*)hh,
                              XSignal(XHeaderView_sectionDoubleClicked_signal),
                              (XObject*)hh, hdrDblSlot,
                              XConnectionType_Direct);
            headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                        XHeaderView_sectionSize(hh, 0) / 2);
            headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                        XHeaderView_sectionSize(hh, 0) / 2);
            XObject_disconnect_1((XObject*)hh,
                                 XSignal(XHeaderView_sectionDoubleClicked_signal),
                                 (XObject*)hh, hdrDblSlot);
            XClassDelete((XClass*)hh);
        }
    }
    /* ③ 固化载荷解码 + BSON 解析（E2/E3 的缓冲与文档生命周期）。 */
    {
        uint32_t len = 0;
        uint8_t* bytes = hexDecode(kPyHistoryHex, &len);
        if (bytes) {
            HsfHistoryPage page;
            memset(&page, 0, sizeof(page));
            (void)HsfHistoryPage_parse(bytes, len, &page);
            XFree_System(bytes);
        }
        {
            XBsonDocument* doc = XBsonDocument_create();
            if (doc) {
                XVector* keys = XBsonDocument_keys_base(doc);
                if (keys) XClassDelete((XClass*)keys);
                XClassDelete((XClass*)doc);
            }
        }
    }
    /* ④ Hub 夹具（成功读组）。 */
    {
        HubFixture fx;
        if (hubFixtureMake(&fx)) {
            XModbusMapping* m = XModbusMapping_create();
            if (m) {
                XModbusMapping_addRangeEx(m, XModbusDiscreteInputs, 0x100, 2,
                                          fx.devId, 1, 0, 0, 0);
                XModbusHub_setMapping(fx.hub, m);
                XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
                XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
                XModbusHub_connectAll(fx.hub);
                XModbusHub_startPolling(fx.hub);
                spinMs(120);
            }
            hubFixtureFree(&fx);
        }
    }
    /* ⑤ 读组失败日志（F1 路径；框架侧曾按条漏两次深拷贝，2026-11-09 修）。 */
    leakFailureLogPath();
}

/**
 * @brief 造一行「指定 ts + 指定压差」的历史应答（最小形状，19 列）。
 * @details 只给断言要盯的列，其余留 null（解析器按 null → "--" 处理）。
 *          线上列序见 XhsfLinkProto.h 的 HSF_HISTORY_WIRE_COLS（19）。
 * @param ts        该行的墙钟 epoch 毫秒。
 * @param pressMpa  压差数值（<0 ⇒ 该格为 null）。
 * @param pressNote 压差越限说明（NULL/空 ⇒ 无说明）。
 * @param turb1     浊度1（<0 ⇒ null）。
 * @return 载荷字节（调用方释放）；失败返回 NULL。
 */
static XByteArray* buildHistoryRowPayload(int64_t ts, double pressMpa,
                                          const char* pressNote, double turb1,
                                          double flowTotal)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* rows = XBsonArray_create();
    XBsonArray* row = XBsonArray_create();
    XByteArray* bytes = NULL;
    XBsonValue* v;
    int i;

    if (!doc || !rows || !row) {
        if (doc) XClassDelete((XClass*)doc);
        if (rows) XClassDelete((XClass*)rows);
        if (row) XClassDelete((XClass*)row);
        return NULL;
    }
    /* 线上列序（19）：ts,mode,vfd_state,freq_hz,current_a,rpm,turb1,turb2,
     *   flow,flow_unit,flow_total,total_unit,press_mpa,press_note,temp,
     *   hum,di,do,water_temp。逐列 append，null = 该列无数据。 */
    v = XBsonValue_create_int64(ts);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    for (i = 1; i < 6; ++i) {           /* mode..rpm */
        v = XBsonValue_create_null();
        XBsonArray_append_move_base(row, v);
        XBsonValue_delete(v);
    }
    if (turb1 < 0.0) v = XBsonValue_create_null();
    else v = XBsonValue_create_double(turb1);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_null();         /* turb2 */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    for (i = 8; i < 12; ++i) {           /* flow..total_unit */
        v = XBsonValue_create_null();
        if (i == 10 && flowTotal >= 0.0) {   /* flow_total（显示列 10） */
            v = XBsonValue_create_double(flowTotal);
        }
        XBsonArray_append_move_base(row, v);
        XBsonValue_delete(v);
    }
    if (pressMpa < 0.0) v = XBsonValue_create_null();
    else v = XBsonValue_create_double(pressMpa);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr(pressNote ? pressNote : "");
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    for (i = 14; i < 19; ++i) {          /* temp..water_temp */
        v = XBsonValue_create_null();
        XBsonArray_append_move_base(row, v);
        XBsonValue_delete(v);
    }
    {
        /* [与服务端同款] 行数组塞进值里再挂到 rows：setArray_ref 借用，
         * append_move_base 把内容移进数组元素，随后释放值壳。 */
        XBsonValue* rv = XBsonValue_create(XBSON_TYPE_ARRAY);
        if (rv) {
            XBsonValue_setArray_ref(rv, row);
            row = NULL;                   /* 所有权交给值 */
            XBsonArray_append_move_base(rows, rv);
            XBsonValue_delete(rv);
        }
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "offset", 0);
    XBsonDocument_insert_keyUtf8_int32(doc, "limit", 1);
    XBsonDocument_insert_keyUtf8_int32(doc, "total", 1);
    XBsonDocument_insert_keyUtf8_array_move(doc, "rows", rows);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief §N6：历史页时间戳与异常行判据（2026-11-10 真 bug 回归锁）。
 * @details 两条都是「加水温列」那次改动放大/引入的**位置下标漂移**：
 *   ① 时间戳：旧实现「现在 − 年龄」反推，remMs 只回绕 24h 的时分秒、
 *      **不向日期借位** ⇒ 跨零点前的行（今天 08:00 看昨天 12:00 那行）
 *      age < 24h ⇒ days=0 ⇒ **日期多一天**（旧实现显示 2026-11-10，
 *      真值是 2026-11-09）。Python 端 time.localtime(ts/1000) 绝对正确。
 *      修法：按绝对 epoch 换算（墙钟 epoch + 本地时区偏移 → 日历展开）。
 *   ② 异常行：旧判据读 `m_cells[10]`，而加水温列后 10 是**正累计**、
 *      压差已移到 11 ⇒ 正累计几乎恒有值 ⇒ **几乎每行都被标异常（整表粉红）**；
 *      且判据应是「压差**越限说明**非空」（Python 的 bool(press_note)），
 *      不是「显示格 ≠ --」（那把「有压差读数」也当异常）。
 */
static void testN6_historyTsAndAbnormalCriteria(void)
{
    XByteArray* bytes;
    HsfHistoryPage page;
    int64_t ts;
    int64_t tzOffsetMs;
    XDateTime nowLocal;
    XDateTime yesterday;
    char expect[32];

    XPrintf("--- §N6 历史页时间戳 / 异常行判据（加水温列后的下标漂移回归锁）---\n");

    /* ---- ① 时间戳：昨天 23:59:59 那一行（确定性构造）----
     * [为什么挑这个时刻] 旧实现「现在 − 年龄」的 bug 只在**年龄 < 24h 且
     * 日历上是昨天**时暴露（age 整除出 days=0 ⇒ 日期停在今天）。昨天
     * 23:59:59 的年龄恒 < 24h（除非现在恰好也是 23:59:59，一年一次），
     * 且它的时刻**晚于**现在的时刻 ⇒ 旧实现必然把它显示成**今天** 23:59
     * ⇒ 断言稳定抓住该 bug（不像「昨天当前时刻−1h」那种构造：那个的
     * 年龄是 25h ⇒ days=1 ⇒ 旧实现反而是对的 ⇒ 抓不到）。 */
    nowLocal = XDateTime_currentDateTime();
    tzOffsetMs = XDateTime_toMSecsSinceEpoch(&nowLocal)
                 - XDateTime_currentWallMSecsSinceEpoch();
    {
        /* 本地日历上的「昨天 23:59:59」。 */
        XDate d = XDateTime_date(&nowLocal);
        XDate yd = XDate_addDays(&d, -1);
        XTime yt = XTime_create_time(23, 59, 59, 0);
        yesterday = XDateTime_create_datetime(yd, yt);
    }
    /* 期望显示串（用日历自己的年月日时分秒，**不**复用被测公式）。 */
    {
        XDate d = XDateTime_date(&yesterday);
        XTime t = XDateTime_time(&yesterday);
        snprintf(expect, sizeof(expect), "%04d-%02d-%02d %02d:%02d:%02d",
                 XDate_year(&d), XDate_month(&d), XDate_day(&d),
                 XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    }
    /* 该日历时刻对应的墙钟 epoch（本地日历按 UTC 解释再减偏移）。 */
    ts = XDateTime_toMSecsSinceEpoch(&yesterday) - tzOffsetMs;
    bytes = buildHistoryRowPayload(ts, -1.0, NULL, -1.0, -1.0);
    CHECK("§N6 造载荷成功", bytes != NULL);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        CHECK("§N6 解析成功",
              HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page));
        CHECK("§N6 一行解析出来", page.m_count == 1, "count=%d", page.m_count);
        if (page.m_count == 1) {
            CHECK("§N6 时间戳显示的是**昨天**（不是今天）",
                  strcmp(page.m_rows[0].m_cells[0], expect) == 0,
                  "显示=%s 期望=%s", page.m_rows[0].m_cells[0], expect);
        }
        XFree_System(bytes);
    }

    /* ---- ② 异常行：有压差数值但**无越限说明** ⇒ 不是异常行 ---- */
    bytes = buildHistoryRowPayload(0, 0.4992, "", -1.0, -1.0);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 有压差数值但无越限说明 ⇒ 不标异常行",
              page.m_count == 1 && !page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }

    /* ---- ②b 异常行：**正累计有值**（显示列 10）但无压差越限 ⇒ 仍不是异常行
     * [为什么单独一条] 旧判据读的是 m_cells[10]（正累计）⇒ 正累计几乎恒有值
     * ⇒ **几乎每一行都被标成异常 ⇒ 整表粉红**。本条把正累计填上值，
     * 锁住"有正累计 ≠ 异常"。 */
    bytes = buildHistoryRowPayload(0, 0.4992, "", -1.0, 119.36);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 正累计有值但无压差越限 ⇒ 不标异常行（旧判据会整表误标）",
              page.m_count == 1 && !page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }

    /* ---- ③ 异常行：压差**有越限说明** ⇒ 标异常行 ---- */
    bytes = buildHistoryRowPayload(0, 0.4992, "超上限", -1.0, -1.0);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 压差有越限说明 ⇒ 标异常行",
              page.m_count == 1 && page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }

    /* ---- ④ 异常行：浊度越限（≥20 NTU）⇒ 标异常行（口径不变，防回归） ---- */
    bytes = buildHistoryRowPayload(0, -1.0, "", 25.0, -1.0);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 浊度越限 ⇒ 标异常行",
              page.m_count == 1 && page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }
}

/**
 * @brief §G：回归文件自身的新增分配必须全部释放。
 * @details 预热一轮（把框架的惰性一次性初始化摊平），再连做 3 轮工作集，
 *          堆在用字节数必须回基线（差 0）。
 */
static void testG_leakSelfCheck(void)
{
    uint64_t base = 0;
    uint64_t now = 0;
    int i;

    XPrintf("--- §G 泄漏自检（回归文件自身）---\n");
    if (!XMemory_statisticsEnabled()) {
        XPrintf("[SKIP] §G 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    leakWorkload();                 /* 预热：摊平惰性初始化 */
    flushDeferred();
    base = heapNow();
    for (i = 0; i < 3; ++i) leakWorkload();
    flushDeferred();
    now = heapNow();
    CHECK("§G 回归自身无泄漏（3 轮工作集堆增量为 0）", now == base,
          "基线=%llu 现在=%llu 差=%lld", (unsigned long long)base,
          (unsigned long long)now, (long long)(now - base));
}

/**
 * @brief §J：认证「策略性拒绝」必须是 Rejected，不能伪装成口令错。
 *
 * @details [钉的是 2026-11-09 的"有时候客户端没数据"根因]
 *   原实现：服务端凭据查表对**任何**非 Ok（含"同账已在线/会话数上限"
 *   这种纯瞬时状态）都返回 BadUser，框架再按反枚举口径下发「随机盐 +
 *   全 0 摘要」的假挑战 ⇒ 客户端必然比对失败 ⇒ 收到 **BadPassword**。
 *   客户端把"口令错"当永久失败（正确），于是**一次链路抖动就让已登录的
 *   界面永久无数据**，而用户被告知的是"口令错误"（照提示重输还会把失败
 *   计数打高直至锁定）。
 *
 *   现在框架的查表枚举多了 `Rejected`：命中时**直接下发真实 Rejected**、
 *   不造假挑战、也不把它计入失败次数；客户端据此退避重试（有上限）。
 *   本组用回环设备对驱动两个会话，不需要真实网络。
 *
 * @note 反枚举口径**不能被这次修改带坏**：BadUser 仍走假挑战，客户端最终
 *       看到的仍是 BadPassword（J2 专门锁这条）。
 */
#if XLINK_AUTH_ON

static int g_jAuthStatus;       /**< 客户端收到的认证失败状态码。 */
static int g_jAuthFails;        /**< 客户端收到失败结论的次数。 */
static int g_jAuthChallenges;   /**< 客户端收到挑战的次数（Rejected 应为 0）。 */

static void jAuthReset(void)
{
    g_jAuthStatus = -1;
    g_jAuthFails = 0;
    g_jAuthChallenges = 0;
}

/** @brief 客户端 authChanged 槽：记录状态码。
 *  @note  `XVarList_args_3` **自带声明**（`const T const name`），故本地
 *         不能再声明 passed/status/role——否则 MSVC 报 C2373 重定义。 */
static void jAuthOnChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_3(args, bool, passed, uint8_t, status, uint8_t, role);
    (void)role;
    if (!passed) {
        ++g_jAuthFails;
        g_jAuthStatus = (int)status;
    }
}

/** @brief 客户端 authChallenge 槽：只计数。 */
static void jAuthOnChallenge(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_jAuthChallenges;
}

/** @brief 客户端应答桩：填任意 32 字节（服务端用全 0 摘要比对，必不匹配）。 */
static bool jAuthRespond(void* userData,
                         const XLinkAuthChallengePayload* challenge,
                         uint8_t* methodOut,
                         uint8_t responseOut[XLINK_AUTH_NONCE_BYTES],
                         uint8_t hashOut[XLINK_AUTH_NONCE_BYTES])
{
    (void)userData;
    (void)challenge;
    if (methodOut) *methodOut = (uint8_t)XLinkAuthMethod_IteratedHmac;
    memset(responseOut, 0x5A, XLINK_AUTH_NONCE_BYTES);
    memset(hashOut, 0x5A, XLINK_AUTH_NONCE_BYTES);
    return true;
}

/** @brief 服务端查表桩：固定返回「策略性拒绝」。 */
static int jAuthLookupRejected(void* userData, const char* user,
                               uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                               uint32_t* iterationsOut,
                               uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                               uint8_t* roleOut)
{
    (void)userData; (void)user; (void)iterationsOut; (void)roleOut;
    memset(saltOut, 0x11, XLINK_AUTH_SALT_BYTES);
    memset(hashOut, 0x22, XLINK_AUTH_NONCE_BYTES);
    return XLinkSessionAuthLookup_Rejected;
}

/** @brief 服务端查表桩：固定返回「用户不存在」（反枚举口径用）。 */
static int jAuthLookupBadUser(void* userData, const char* user,
                              uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                              uint32_t* iterationsOut,
                              uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                              uint8_t* roleOut)
{
    (void)userData; (void)user; (void)iterationsOut; (void)roleOut;
    memset(saltOut, 0x11, XLINK_AUTH_SALT_BYTES);
    memset(hashOut, 0x22, XLINK_AUTH_NONCE_BYTES);
    return XLinkSessionAuthLookup_BadUser;
}

/**
 * @brief 跑一组「服务端按 lookup 结果处置 → 客户端收到什么」的回环用例。
 * @param lookup 服务端查表桩。
 * @return 客户端最后一次收到的失败状态码；-1 表示没收到失败结论。
 */
static int jAuthRunCase(XLinkSessionAuthLookupCallback lookup)
{
    XGuiRemoteLoopbackDevice* devA = NULL;
    XGuiRemoteLoopbackDevice* devB = NULL;
    XLinkSession* server = NULL;
    XLinkSession* client = NULL;
    uint32_t now = 1000;
    int i;

    jAuthReset();
    devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
    if (!devA || !devB) {
        CHECK("§J 回环设备对创建成功", false, "createPair 失败");
        return -1;
    }
    server = XLinkSession_create_ex((XIODevice*)devA);
    client = XLinkSession_create_ex((XIODevice*)devB);
    if (!server || !client) {
        CHECK("§J 回环会话创建成功", false, "create_ex 失败");
    } else {
        XLinkSession_setAuthLookup(server, lookup, NULL);
        XLinkSession_setCapability(client, XLinkCapability_Auth);
        XLinkSession_setAuthUser(client, "probe");
        XLinkSession_setAuthResponse(client, jAuthRespond, NULL);
        XObject_connect_1((XObject*)client,
                          XSignal(XLinkSession_authChanged_signal),
                          (XObject*)server, jAuthOnChanged,
                          XConnectionType_Auto);
        /* [挑战信号只在**服务端**会话上发] XLinkSession.c 的两处发射点都在
         * 服务端处理 AuthRequest 的路径上（Ok 走真挑战、非 Ok 走"随机盐 +
         * 全 0 摘要"的假挑战）。客户端不发这个信号 ⇒ 必须挂服务端对象，
         * 否则计数恒 0（写这段时的实锅：挂客户端导致 §J2 误报）。 */
        XObject_connect_1((XObject*)server,
                          XSignal(XLinkSession_authChallenge_signal),
                          (XObject*)client, jAuthOnChallenge,
                          XConnectionType_Auto);
        for (i = 0; i < 12; ++i) {
            now += 10;
            XLinkSession_pump(server, now);
            XLinkSession_pump(client, now);
        }
        /* 被拒后两端都不该 Ready（业务消息一条都不能通）。 */
        CHECK("§J 被拒服务端未就绪", !XLinkSession_isReady(server));
        CHECK("§J 被拒客户端未就绪", !XLinkSession_isReady(client));
    }
    if (server) XClassDelete((XClass*)server);
    if (client) XClassDelete((XClass*)client);
    if (devA) XClassDelete((XClass*)devA);
    if (devB) XClassDelete((XClass*)devB);
    return g_jAuthStatus;
}

static void testJ_policyRejectSemantics(void)
{
    int status;

    /* J1 策略性拒绝：客户端必须看到 Rejected（4），**且没收到假挑战**。 */
    status = jAuthRunCase(jAuthLookupRejected);
    CHECK("§J1 策略拒绝下发真实 Rejected（不是口令错）",
          status == (int)XLinkAuthStatus_Rejected,
          "客户端状态码=%d（期望 %d=Rejected；2=BadPassword 说明还在造假挑战）",
          status, (int)XLinkAuthStatus_Rejected);
    CHECK("§J1 策略拒绝不伪造挑战（客户端零次 authChallenge）",
          g_jAuthChallenges == 0, "挑战次数=%d", g_jAuthChallenges);
    CHECK("§J1 策略拒绝只给一次失败结论（不反复重发）",
          g_jAuthFails == 1, "失败结论次数=%d", g_jAuthFails);

    /* J2 反枚举口径不变：用户不存在仍走"假挑战 → 口令错"（别被顺手改坏）。 */
    status = jAuthRunCase(jAuthLookupBadUser);
    CHECK("§J2 用户不存在仍走假挑战（客户端收到 BadPassword）",
          status == (int)XLinkAuthStatus_BadPassword,
          "客户端状态码=%d（期望 2=BadPassword）", status);
    CHECK("§J2 用户不存在会下发挑战（反枚举：不暴露账户是否存在）",
          g_jAuthChallenges >= 1, "挑战次数=%d", g_jAuthChallenges);
}

#endif /* XLINK_AUTH_ON */

/* ============================================================================
 * §K 三设备接入（ttyS4 路 B：浊度仪 ×2 + 电磁流量计；2026-11-09 板端实测定案）
 *
 * 钉的是 docs/HSF板端ttyS4三设备取数实测总表_20261109.md 的四条硬结论：
 *   K1 两台浊度仪**字节序不同**（站 1=CDAB、站 2=DCBA 整字节反序），
 *      用实测原始帧逐字节断言；站 1 的解码语义一个比特都不能回退。
 *      站 2 温度与浊度**同刻**（0x0000×4 一帧），进设备层（不进推送帧）。
 *   K2 站号配置锁：浊度仪 1/2、流量计 5（站 3 不存在——板端普查 1~20 零应答；
 *      站 2 后面要换表，站号只改 HsfServerConfig.h 两个定义）。
 *   K3 模拟器装配后经**真实解码路径**读回现场口径值：
 *      站 1==2.29（CDAB）、站 2==0.045 + 温度 19.3（DCBA 同刻）、
 *      站 5==3.6 且空管 False、单位码 5=m³/h——三台共用一条虚拟 485。
 *   K5 发帧前静默闸（m_preTxQuietMs）行为锁：Hub 读组序**区优先**（流量计
 *      IR 恒先于浊度 HR），「站5 长帧 → 站1」坏相位是结构性的——无闸对照
 *      两站首拍 <170ms；带 200ms 闸的浊度仪首拍 ≥180ms（恢复期外才发）。
 *   K4 §K 跑完堆回基线（对齐 §G 口径：预热 1 轮 + 3 轮工作集增量必须为 0）。
 *
 * [2026-11-10 保真扩容 —— 模拟从站把真实仪表的时序行为带上]
 *   真实仪表的应答从来不是零时延、站 1 在站 5 长帧后有恢复期、站 2 的
 *   应答功能码恒 03、三台的数值各自有微抖动。模拟器带上这些行为，主站的
 *   站间间隙 / 发帧前静默闸才不是"只跑不验"的死代码：
 *   K6 应答时延旋钮：HSF_SIM_* 宏缺省=实测值；setter/getter 成对可配；
 *      模拟装配处（HsfServerIoSim_createBusClient）接线锁。**时延数值
 *      本身不做耗时断言**（CI 抖动），只断言"应答照常到达"的行为方向。
 *   K7 站 1 恢复期行为锁：站 5 长帧（22 寄存器 = 49 字节）收口后窗口内
 *      站 1 无应答（SendRawRequest 返回 NULL=上层按超时处理）+ 计数；
 *      窗口外恢复；窗口 0 = 关闸（旋钮可配）。
 *   K8 站 2 功能码怪癖：FC04 请求也受理但应答功能码恒回 03、数据与 FC03
 *      逐字节一致（实测文档 §5.5/附录 A）；站 1 无 FC04（异常应答 fc|0x80）。
 *   K9 数值微抖动方向锁：站 1 恒定 2.29（实测 20 轮不动）、站 2
 *      0.045±0.001 + 温度 19.3±0.05、站 5 3.6±2% 正弦、空管 False——
 *      带内断言（不锁逐拍相位）；setInstantFlow 钉死口径不回退。
 * ==========================================================================*/

#if XINDUSTRY_ON && XTURBIDITY_ON && XMODBUS_HUB_ON

/** @brief 32 位位模式 → double（memcpy 口径，与 XTurbidityMeter 的解释一致）。 */
static double kBitsToFloat(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof(f));
    return (double)f;
}

/**
 * @brief 建一个恒等标度（1/1/0、无符号、保持寄存器）的原始字通道组。
 * @param outGroup 输出通道组（调用方 XClassDelete）。
 * @param pointCount 点数（role 0..pointCount-1）。
 * @return 全部点登记成功返回 true。
 * @note 与 HsfServerIoTurbidity 的 turbMakePoint 同款约束：这些字是 32 位量
 *       的原始位模式，任何非恒等标度都会破坏 IEEE754。
 */
static bool kMakeRawGroup(XIoChannelGroup** outGroup, int pointCount)
{
    XIoChannelGroup* group;
    int i;

    *outGroup = NULL;
    group = XIoChannelGroup_create();
    if (!group) return false;
    for (i = 0; i < pointCount; ++i) {
        XIoAnalogPoint* apt =
            XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XIoPoint* pt;
        bool ok;
        if (!apt) break;
        pt = &apt->m_base;
        ok = XIoPoint_setRole(pt, (uint16_t)i);
        XIoPoint_setType(pt, XIoPoint_AnalogInput);
        XIoPoint_setTag(pt, "K-TURB");
        XIoPoint_setUseHolding(pt, true);      /* 保持寄存器（FC03）。 */
        XIoPoint_setSigned(pt, false);         /* 原始位模式按无符号处理。 */
        XIoAnalogPoint_setScale(apt, 1, 1, 0, 0);  /* 恒等标度：不碰位模式。 */
        if (ok) ok = XIoChannelGroup_addPoint(group, pt, true);
        if (!ok) {
            XClassDelete((XClass*)apt);
            break;
        }
    }
    if (i < pointCount) {
        XClassDelete((XClass*)group);
        return false;
    }
    *outGroup = group;
    return true;
}

/**
 * @brief K1：实测原始帧的逐字节断言（字序旋钮的三种口径 + 温度同刻）。
 */
static void testK1_wordOrderDocumentedFrames(void)
{
    XIoChannelGroup* g2 = NULL;    /* 两字组（浊度）。 */
    XIoChannelGroup* g4 = NULL;    /* 四字组（浊度+温度，站 2 口径）。 */
    XTurbidityMeter* cdab = NULL;
    XTurbidityMeter* dcba = NULL;
    double ntu = 0.0;
    double temp = 0.0;
    uint32_t bits = 0;

    XPrintf("--- K1 实测帧逐字节（站 1 CDAB / 站 2 DCBA）---\n");

    /* ---- 站 1（WZT-701A，CDAB，缺省口径）：8F 5C 40 12 → 2.29f ----
     * 实测帧 `01 03 04 8F 5C 40 12 A1 38`：低字 0x8F5C 在 0x0000、
     * 高字 0x4012 在 0x0001 → 位模式 0x40128F5C = 2.29f。 */
    if (kMakeRawGroup(&g2, 2)) cdab = XTurbidityMeter_create();
    if (cdab && XTurbidityMeter_attach(cdab, g2)) {
        XIoChannelGroup_feedRole(g2, 0, (int32_t)0x8F5C);
        XIoChannelGroup_feedRole(g2, 1, (int32_t)0x4012);
        CHECK("K1 站1 实测帧 8F5C/4012（CDAB）= 2.29 NTU",
              XTurbidityMeter_value(cdab, &ntu) && ntu == kBitsToFloat(0x40128F5Cu),
              "ntu=%.7g 期望 %.7g", ntu, kBitsToFloat(0x40128F5Cu));
        CHECK("K1 站1 拼装位模式 = 0x40128F5C（站 1 解码语义未回退）",
              XTurbidityMeter_rawWords(cdab, &bits) && bits == 0x40128F5Cu,
              "bits=0x%08X", (unsigned)bits);
        /* 字序旋钮真的在起作用：同一帧切 DCBA 不再是 2.29，切回即恢复。 */
        CHECK("K1 站1 切 DCBA 后同一帧不再是 2.29（字序旋钮有效）",
              XTurbidityMeter_setWordOrder(cdab, XTurbidityMeterWordOrder_Dcba) &&
              XTurbidityMeter_value(cdab, &ntu) && ntu != kBitsToFloat(0x40128F5Cu));
        CHECK("K1 站1 切回 CDAB 恢复 2.29",
              XTurbidityMeter_setWordOrder(cdab, XTurbidityMeterWordOrder_LowHigh) &&
              XTurbidityMeter_value(cdab, &ntu) && ntu == kBitsToFloat(0x40128F5Cu));
        CHECK("K1 非法字序被拒绝（枚举外值不受理）",
              !XTurbidityMeter_setWordOrder(cdab, (XTurbidityMeterWordOrder)7));
    } else {
        CHECK("K1 CDAB 夹具建成", false);
    }

    /* ---- 站 2（ZHEYI-5100ZDY，DCBA 整字节反序=小端）----
     * 实测帧 `02 03 04 4F DD 37 3D 98 3C`：w0=0x4FDD、w1=0x373D →
     * 位模式 0x3D37DD4F ≈ 0.04489f（按旧 CDAB 口径解出 1.1e-5 假数）。 */
    if (kMakeRawGroup(&g4, 4)) dcba = XTurbidityMeter_create();
    if (dcba && XTurbidityMeter_setWordOrder(dcba, XTurbidityMeterWordOrder_Dcba) &&
        XTurbidityMeter_attach(dcba, g4)) {
        /* 浊度帧（×2 口径的 4 字节）：4F DD 37 3D → 0.04489f。 */
        XIoChannelGroup_feedRole(g4, 0, (int32_t)0x4FDD);
        XIoChannelGroup_feedRole(g4, 1, (int32_t)0x373D);
        CHECK("K1 站2 实测帧 4FDD/373D（DCBA）≈ 0.0449 NTU",
              XTurbidityMeter_value(dcba, &ntu) && ntu == kBitsToFloat(0x3D37DD4Fu),
              "ntu=%.7g 期望 %.7g", ntu, kBitsToFloat(0x3D37DD4Fu));
        CHECK("K1 站2 拼装位模式 = 0x3D37DD4F（小端整字节反序）",
              XTurbidityMeter_rawWords(dcba, &bits) && bits == 0x3D37DD4Fu,
              "bits=0x%08X", (unsigned)bits);
        /* 温度同刻帧（0x0000×4 的后两字，实测 `00 00 99 41` → 19.125℃）。 */
        XIoChannelGroup_feedRole(g4, 2, (int32_t)0x0000);
        XIoChannelGroup_feedRole(g4, 3, (int32_t)0x9941);
        CHECK("K1 站2 温度字 0000/9941（DCBA 同字序）= 19.125 ℃",
              XTurbidityMeter_temperature(dcba, &temp) &&
              temp == kBitsToFloat(0x41990000u),
              "temp=%.7g 期望 %.7g", temp, kBitsToFloat(0x41990000u));
        CHECK("K1 站2 浊度在温度字到达后不变（同刻重算不出错）",
              XTurbidityMeter_value(dcba, &ntu) && ntu == kBitsToFloat(0x3D37DD4Fu));
        CHECK("K1 站2 温度原始位模式 = 0x41990000",
              XTurbidityMeter_rawTemperature(dcba, &bits) && bits == 0x41990000u,
              "bits=0x%08X", (unsigned)bits);
    } else {
        CHECK("K1 DCBA 夹具建成", false);
    }

    if (cdab) XClassDelete((XClass*)cdab);
    if (dcba) XClassDelete((XClass*)dcba);
    if (g2) XClassDelete((XClass*)g2);
    if (g4) XClassDelete((XClass*)g4);
    flushDeferred();
}

/** @brief K2：站号/寄存器数/字序配置锁（换表只改 HsfServerConfig.h 两个定义）。 */
static void testK2_unitIdConfigLock(void)
{
    XPrintf("--- K2 站号与口径配置锁 ---\n");
    CHECK("K2 浊度仪1 站号 == 1（板端普查实测）", HSF_TURBIDITY1_UNIT_ID == 1,
          "定义=%d", (int)HSF_TURBIDITY1_UNIT_ID);
    CHECK("K2 浊度仪2 站号 == 3（2026-11-10 现场把面板站号改为 3；换站只改这一个定义）",
          HSF_TURBIDITY2_UNIT_ID == 3, "定义=%d", (int)HSF_TURBIDITY2_UNIT_ID);
    CHECK("K2 流量计站号 == 5", HSF_FLOWMETER_UNIT_ID == 5,
          "定义=%d", (int)HSF_FLOWMETER_UNIT_ID);
    CHECK("K2 三站站号互不相同", HSF_TURBIDITY1_UNIT_ID != HSF_TURBIDITY2_UNIT_ID &&
          HSF_TURBIDITY1_UNIT_ID != HSF_FLOWMETER_UNIT_ID &&
          HSF_TURBIDITY2_UNIT_ID != HSF_FLOWMETER_UNIT_ID);
    CHECK("K2 字序配置：站1=CDAB(0) 站2=DCBA(2)（实测两台字节序不同）",
          HSF_TURBIDITY1_WORD_ORDER == 0 && HSF_TURBIDITY2_WORD_ORDER == 2,
          "站1=%d 站2=%d", (int)HSF_TURBIDITY1_WORD_ORDER,
          (int)HSF_TURBIDITY2_WORD_ORDER);
    CHECK("K2 寄存器数：站1=×2（仅浊度）站2=×4（浊度+温度同刻）",
          HSF_TURBIDITY1_REG_COUNT == 2 && HSF_TURBIDITY2_REG_COUNT == 4,
          "站1=%d 站2=%d", (int)HSF_TURBIDITY1_REG_COUNT,
          (int)HSF_TURBIDITY2_REG_COUNT);
    CHECK("K2 静默期配置：站1=200ms（站5 长帧后恢复期）站2=0（免疫）",
          HSF_TURBIDITY1_TX_QUIET_MS == 200 && HSF_TURBIDITY2_TX_QUIET_MS == 0,
          "站1=%u 站2=%u", (unsigned)HSF_TURBIDITY1_TX_QUIET_MS,
          (unsigned)HSF_TURBIDITY2_TX_QUIET_MS);
    CHECK("K2 路 B 虚拟地址不重叠（站1 起点+2 == 站2 起点；流量计另段）",
          (int)HSF_TURBIDITY2_VADDR == (int)HSF_TURBIDITY_VADDR +
                                       (int)HSF_TURBIDITY1_REG_COUNT &&
          (int)HSF_FLOWMETER_VADDR + 22 <= (int)HSF_TURBIDITY_VADDR,
          "flow_end=0x%X turb1=0x%X", (unsigned)(HSF_FLOWMETER_VADDR + 22),
          (unsigned)HSF_TURBIDITY_VADDR);
}

/**
 * @brief K3 单轮工作集：路 B 三台设备一条虚拟 485，模拟器装配后经真实
 *        解码路径（Hub 轮询 → 站模块 → 设备层字序拼装）读回现场口径值。
 * @return 三站是否全部读到有效值（K4 的预热判据用）。
 */
static bool testK3_simRouteBFidelityOnce(void)
{
    HsfServerBus bus;
    HsfServerIoTurbidity t1;
    HsfServerIoTurbidity t2;
    HsfServerIoFlowMeter fs;
    struct XVirtualModelTurbidity* t1m = NULL;
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelFlow* fm = NULL;
    XModbusVirtualClient* client = NULL;
    XModbusMapping* map = NULL;
    double ntu1 = 0.0;
    double ntu2 = 0.0;
    double temp2 = 0.0;
    double flow = 0.0;
    int flowUnit = -1;
    int alarms = -1;
    bool fvalid = false;
    bool allValid = false;
    int waited;
    bool mounted;

    memset(&bus, 0, sizeof(bus));
    HsfServerIoTurbidity_init(&t1);
    HsfServerIoTurbidity_init(&t2);
    HsfServerIoFlowMeter_init(&fs);

    /* 模型按现场口径布值（与 HsfServerIoSim_ensure 同一套 setter）。 */
    t1m = XVirtualModelTurbidity_create(0);      /* 站 1：CDAB、无温度。 */
    t2m = XVirtualModelTurbidity_create(1);      /* 站 2：DCBA、带温度。 */
    fm = XVirtualModelFlow_create();             /* 站 5：ABCD。 */
    if (!t1m || !t2m || !fm) {
        CHECK("K3 虚拟模型建成", false);
        goto k3_done;
    }
    XVirtualModelTurbidity_setWordOrder(t1m, (int)XVirtualModelTurbWordOrder_Cdab);
    XVirtualModelTurbidity_setNtu(t1m, 2.29);
    XVirtualModelTurbidity_setWordOrder(t2m, (int)XVirtualModelTurbWordOrder_Dcba);
    XVirtualModelTurbidity_setNtu(t2m, 0.045);
    XVirtualModelTurbidity_setTempC(t2m, 19.3);
    XVirtualModelFlow_setInstantFlow(fm, 3.6f);

    /* 三台共用**一条** 485（与真机同接法），靠站号分派——站号全部来自
     * HsfServerConfig.h，模拟器没有写死站号的地方。 */
    client = XModbusVirtualClient_create(NULL);
    CHECK("K3 虚拟客户端建成", client != NULL);
    if (!client) goto k3_done;
    mounted =
        XModbusVirtualClient_bindUnit(client, HSF_TURBIDITY1_UNIT_ID,
                                      XVirtualModel_base(t1m)) &&
        XModbusVirtualClient_bindUnit(client, HSF_TURBIDITY2_UNIT_ID,
                                      XVirtualModel_base(t2m)) &&
        XModbusVirtualClient_bindUnit(client, HSF_FLOWMETER_UNIT_ID,
                                      XVirtualModel_base(fm));
    CHECK("K3 三台从站模型按配置站号绑定", mounted);

    /* 装配顺序与真机一致：浊度 1 → 浊度 2 → 流量计（站 1 恢复期 ≥200ms 的
     * 顺序前提，见 HsfServerIo.c 同款注释）。 */
    CHECK("K3 总线接入注入客户端",
          HsfServerBus_openWithClient(&bus, "K3-485B", (XModbusClient*)client,
                                      500, 1));
    client = NULL;   /* 所有权已移交 Hub。 */
    map = HsfServerBus_createMapping(&bus);
    CHECK("K3 映射表建成", map != NULL);
    if (!map) goto k3_done;
    mounted =
        HsfServerIoTurbidity_mount(&t1, &bus, map, HSF_TURBIDITY1_UNIT_ID, 0,
                                   (uint16_t)HSF_TURBIDITY_VADDR, 3000,
                                   HSF_TURBIDITY1_REG_COUNT,
                                   HSF_TURBIDITY1_WORD_ORDER,
                                   HSF_TURBIDITY1_TX_QUIET_MS) &&
        HsfServerIoTurbidity_mount(&t2, &bus, map, HSF_TURBIDITY2_UNIT_ID, 1,
                                   (uint16_t)HSF_TURBIDITY2_VADDR, 3000,
                                   HSF_TURBIDITY2_REG_COUNT,
                                   HSF_TURBIDITY2_WORD_ORDER,
                                   HSF_TURBIDITY2_TX_QUIET_MS) &&
        HsfServerIoFlowMeter_mount(&fs, &bus, map, HSF_FLOWMETER_UNIT_ID,
                                   (uint16_t)HSF_FLOWMETER_VADDR, 3000);
    CHECK("K3 三站装配成功（浊度1 CDAB×2+静默200 / 浊度2 DCBA×4 / 流量计 ABCD×22）",
          mounted);
    if (!mounted) goto k3_done;
    CHECK("K3 总线启动（映射提交 + 轮询拉起）", HsfServerBus_start(&bus, map, 20));
    map = NULL;

    /* 等三站都出有效值（进程内应答，首拍在几十毫秒内）。 */
    for (waited = 0; waited < 3000; waited += 20) {
        bool v1 = HsfServerIoTurbidity_ntu(&t1, &ntu1);
        bool v2 = HsfServerIoTurbidity_ntu(&t2, &ntu2);
        bool vf = HsfServerIoFlowMeter_values(&fs, &flow, NULL, NULL, NULL,
                                              &flowUnit, NULL, &alarms, &fvalid);
        if (v1 && v2 && vf && fvalid) { allValid = true; break; }
        spinMs(20);
    }
    CHECK("K3 三站首拍全部有效", allValid);

    /* ★ 保真断言：模拟器 → Hub → 站模块 → 设备层（同一 XTurbidityMeter /
     *   XFlowMeter 解码路径），固定值读回必须**逐位**等于设定值。 */
    CHECK("K3 站1 读回 == 2.29（CDAB 保真）",
          ntu1 == (double)(float)2.29, "ntu=%.7g", ntu1);
    CHECK("K3 站2 读回 == 0.045（DCBA 保真；旧 CDAB 口径会解出 1.1e-5）",
          ntu2 == (double)(float)0.045, "ntu=%.7g", ntu2);
    CHECK("K3 站2 温度读回 == 19.3（与浊度同刻，0x0000×4 一帧）",
          HsfServerIoTurbidity_tempC(&t2, &temp2) && temp2 == (double)(float)19.3,
          "temp=%.7g", temp2);
    CHECK("K3 站5 流量读回 == 3.6（ABCD 保真）",
          allValid && flow == (double)3.6f, "flow=%.7g", flow);
    CHECK("K3 站5 空管报警 False（报警位全 0）", allValid && alarms == 0,
          "alarms=0x%X", (unsigned)alarms);
    CHECK("K3 站5 单位码 == 5（m³/h，随表读上来的代码）", allValid && flowUnit == 5,
          "unit=%d", flowUnit);
    CHECK("K3 站对象的站号 == 配置定义（模拟器站号跟随 HsfServerConfig.h）",
          HsfServerIoTurbidity_unitId(&t1) == HSF_TURBIDITY1_UNIT_ID &&
          HsfServerIoTurbidity_unitId(&t2) == HSF_TURBIDITY2_UNIT_ID,
          "站1=%d 站2=%d", HsfServerIoTurbidity_unitId(&t1),
          HsfServerIoTurbidity_unitId(&t2));

k3_done:
    /* 拆除顺序与 E4 同款：站（栈上对象 XClassDeinit，deinit 内部 unmount）
     * → 总线（Hub 最后死）→ 模型（裸 malloc，按分配槽位释放）。 */
    XClassDeinit((XClass*)&t1);
    XClassDeinit((XClass*)&t2);
    XClassDeinit((XClass*)&fs);
    HsfServerBus_stop(&bus);
    if (map) XClassDelete((XClass*)map);
    if (client) XClassDelete((XClass*)client);
    if (t1m) XFree_System(t1m);
    if (t2m) XFree_System(t2m);
    if (fm) XFree_System(fm);
    flushDeferred();
    return allValid;
}

/**
 * @brief K5：发帧前静默闸（XModbusHubUnitLimits.m_preTxQuietMs）的行为锁。
 * @details 用两个独立 Hub（同一份「流量计 IR + 浊度仪 HR」装配）做对照：
 *   · 带闸（浊度仪 200ms）：浊度仪首拍必须比流量计首拍晚 ≥180ms——
 *     恢复期外才发（闸生效）；
 *   · 无闸对照：两站首拍几乎同时（<170ms）——证明"晚"是闸干出来的，
 *     不是读组天生的节奏（读组序区优先，流量计 IR 恒先于浊度 HR，
 *     对照组的相邻间隔只有扫描节拍级 ≈20ms，正是 ttyS4 站5→站1 坏相位）。
 */
static void testK5_preTxQuietGate(void)
{
    struct XVirtualModelTurbidity* turbQ = NULL;
    struct XVirtualModelFlow* flowQ = NULL;
    struct XVirtualModelTurbidity* turbC = NULL;
    struct XVirtualModelFlow* flowC = NULL;
    XModbusHub* hubQ = NULL;
    XModbusHub* hubC = NULL;
    XModbusVirtualClient* cQ = NULL;
    XModbusVirtualClient* cC = NULL;
    int64_t qFlow = 0;
    int64_t qTurb = 0;
    int64_t cFlow = 0;
    int64_t cTurb = 0;
    int waited;

    XPrintf("--- K5 发帧前静默闸行为锁（站1 恢复期 200ms）---\n");
    /* [映射表所有权] 两张映射表都经 XModbusHub_setMapping 移交 Hub，
     * 随 Hub 的 XClassDelete 一并释放——这里不再单独删。 */

    /* ---- 带闸装配：浊度仪 unit1（HR 0x0300×2，静默 200ms）+
     *      流量计 unit2（IR 0x0200×22，无约束）---- */
    turbQ = XVirtualModelTurbidity_create(0);
    flowQ = XVirtualModelFlow_create();
    cQ = (turbQ && flowQ) ? XModbusVirtualClient_create(NULL) : NULL;
    if (turbQ && flowQ && cQ) {
        int devId;
        XModbusMapping* map;
        XModbusHubUnitLimits lim;
        XModbusHubUnitLimits limFlow;
        (void)XVirtualModelTurbidity_setNtu(turbQ, 2.29);
        (void)XVirtualModelFlow_setInstantFlow(flowQ, 3.6f);
        if (XModbusVirtualClient_bindUnit(cQ, 1, XVirtualModel_base(turbQ)) &&
            XModbusVirtualClient_bindUnit(cQ, 2, XVirtualModel_base(flowQ))) {
            hubQ = XModbusHub_create();
            devId = hubQ ? XModbusHub_addDevice_2(hubQ, (XModbusClient*)cQ,
                                                  "K5-quiet") : -1;
            if (devId >= 0) {
                cQ = NULL;      /* 所有权已随 Hub。 */
                map = XModbusMapping_create();
                if (map) {
                    memset(&lim, 0, sizeof(lim));
                    lim.m_preTxQuietMs = 200;   /* 只有浊度仪带闸。 */
                    /* 流量计必须带真实节拍（500ms，对齐 HSF_FLOWMETER_MIN_
                     * POLL_MS）：否则它每拍都到期，总线永远不静默，闸等不到
                     * 满足——真机装配里流量计有自己的分级节拍，闸正是在
                     * 「流量计收口后、下一轮到期前」的空窗里放行浊度仪的。 */
                    memset(&limFlow, 0, sizeof(limFlow));
                    limFlow.m_minPollIntervalMs = 500;
                    XModbusMapping_addRangeEx(map, XModbusInputRegisters,
                                              0x0200, 22, devId, 2, 0x1010, 0, 0);
                    XModbusMapping_addRangeEx(map, XModbusHoldingRegisters,
                                              0x0300, 2, devId, 1, 0x0000, 0, 0);
                    XModbusHub_setUnitLimits(hubQ, devId, 1, &lim);
                    XModbusHub_setUnitLimits(hubQ, devId, 2, &limFlow);
                    XModbusHub_setMapping(hubQ, map);
                    XModbusHub_setPollAreas(hubQ, (1u << XModbusInputRegisters) |
                                                  (1u << XModbusHoldingRegisters));
                    XModbusHub_setPollInterval(hubQ, devId, 20);
                    XModbusHub_connectAll(hubQ);
                    XModbusHub_startPolling(hubQ);
                    for (waited = 0;
                         waited < 4000 && (qFlow == 0 || qTurb == 0);
                         waited += 10) {
                        if (qFlow == 0 &&
                            XModbusHub_pointValid(hubQ, XModbusInputRegisters,
                                                  0x0200, 10000))
                            qFlow = XDateTime_currentMSecsSinceEpoch();
                        if (qFlow != 0 && qTurb == 0 &&
                            XModbusHub_pointValid(hubQ, XModbusHoldingRegisters,
                                                  0x0300, 10000))
                            qTurb = XDateTime_currentMSecsSinceEpoch();
                        spinMs(10);
                    }
                }
            }
        }
    }
    CHECK("K5 带闸：流量计（IR）与浊度仪（HR）首拍都到", qFlow != 0 && qTurb != 0,
          "flow=%lld turb=%lld", (long long)qFlow, (long long)qTurb);
    CHECK("K5 带闸：浊度仪首拍比流量计晚 ≥180ms（200ms 恢复期外才发）",
          qFlow != 0 && qTurb != 0 && (qTurb - qFlow) >= 180,
          "gap=%lld ms", (long long)(qTurb - qFlow));
    XClassDelete((XClass*)hubQ);
    if (cQ) XClassDelete((XClass*)cQ);
    if (turbQ) XFree_System(turbQ);
    if (flowQ) XFree_System(flowQ);
    flushDeferred();

    /* ---- 无闸对照（同一装配、不设 preTxQuiet）---- */
    turbC = XVirtualModelTurbidity_create(0);
    flowC = XVirtualModelFlow_create();
    cC = (turbC && flowC) ? XModbusVirtualClient_create(NULL) : NULL;
    if (turbC && flowC && cC) {
        int devId;
        XModbusMapping* map;
        (void)XVirtualModelTurbidity_setNtu(turbC, 2.29);
        (void)XVirtualModelFlow_setInstantFlow(flowC, 3.6f);
        if (XModbusVirtualClient_bindUnit(cC, 1, XVirtualModel_base(turbC)) &&
            XModbusVirtualClient_bindUnit(cC, 2, XVirtualModel_base(flowC))) {
            hubC = XModbusHub_create();
            devId = hubC ? XModbusHub_addDevice_2(hubC, (XModbusClient*)cC,
                                                  "K5-ctrl") : -1;
            if (devId >= 0) {
                cC = NULL;
                map = XModbusMapping_create();
                if (map) {
                    XModbusMapping_addRangeEx(map, XModbusInputRegisters,
                                              0x0200, 22, devId, 2, 0x1010, 0, 0);
                    XModbusMapping_addRangeEx(map, XModbusHoldingRegisters,
                                              0x0300, 2, devId, 1, 0x0000, 0, 0);
                    XModbusHub_setMapping(hubC, map);
                    XModbusHub_setPollAreas(hubC, (1u << XModbusInputRegisters) |
                                                  (1u << XModbusHoldingRegisters));
                    XModbusHub_setPollInterval(hubC, devId, 20);
                    XModbusHub_connectAll(hubC);
                    XModbusHub_startPolling(hubC);
                    for (waited = 0;
                         waited < 4000 && (cFlow == 0 || cTurb == 0);
                         waited += 10) {
                        if (cFlow == 0 &&
                            XModbusHub_pointValid(hubC, XModbusInputRegisters,
                                                  0x0200, 10000))
                            cFlow = XDateTime_currentMSecsSinceEpoch();
                        if (cFlow != 0 && cTurb == 0 &&
                            XModbusHub_pointValid(hubC, XModbusHoldingRegisters,
                                                  0x0300, 10000))
                            cTurb = XDateTime_currentMSecsSinceEpoch();
                        spinMs(10);
                    }
                }
            }
        }
    }
    CHECK("K5 对照：流量计与浊度仪首拍都到", cFlow != 0 && cTurb != 0,
          "flow=%lld turb=%lld", (long long)cFlow, (long long)cTurb);
    CHECK("K5 对照（无闸）：两站首拍间隔 <170ms（坏相位确实存在，闸是必要的）",
          cFlow != 0 && cTurb != 0 && (cTurb - cFlow) < 170 &&
          (cTurb - cFlow) >= 0,
          "gap=%lld ms", (long long)(cTurb - cFlow));
    XClassDelete((XClass*)hubC);
    if (cC) XClassDelete((XClass*)cC);
    if (turbC) XFree_System(turbC);
    if (flowC) XFree_System(flowC);
    flushDeferred();
}

/* ==================== K6~K9：模拟从站的时序/怪癖/微抖动保真 ==================== */

/**
 * @brief 构造一条读请求并经虚拟客户端发出（进程内同步应答）。
 * @return Reply（调用方 XClassDelete 直删——**不要**换成 deleteLater：
 *         测试顶层 processEvents 的 loopLevel/scopeLevel 都是 0，顶层
 *         post 的延迟删除事件 eventLevel==currentLevel==0，按
 *         XDeferredDeleteEvent_shouldDeliver 的判据永不投递 ⇒ 整只 Reply
 *         泄漏（K4 实测每次 send 漏 ~600B）。Reply 不在信号派发期内，
 *         直删安全）；无应答返回 NULL。
 */
static XModbusReply* kSendReadRaw(XModbusVirtualClient* client, int unit,
                                  uint8_t fc, uint16_t addr, uint16_t count)
{
    XModbusRequest* req;
    XModbusReply* reply;
    uint8_t payload[4];

    if (!client) return NULL;
    req = XModbusRequest_create_with_code((XModbusPdu_FunctionCode)fc);
    if (!req) return NULL;
    payload[0] = (uint8_t)(addr >> 8);
    payload[1] = (uint8_t)(addr & 0xFFu);
    payload[2] = (uint8_t)(count >> 8);
    payload[3] = (uint8_t)(count & 0xFFu);
    XModbusPdu_setData((XModbusPdu*)req, payload, sizeof(payload));
    /* [Copy 语义] base 自拷贝请求入 Reply，本壳自行释放。 */
    reply = XModbusClient_sendRawRequest_base((XModbusClient*)client, req,
                                              unit);
    XClassDelete(req);
    return reply;
}

/** @brief 取应答 PDU 的数据区（首字节 = byteCount）。 */
static const uint8_t* kReplyData(const XModbusReply* reply, int* outSize)
{
    const uint8_t* data = NULL;
    int size = 0;

    if (reply && reply->m_rawResult) {
        data = (const uint8_t*)XContainerDataAddr(
            ((XModbusPdu*)reply->m_rawResult)->m_data);
        size = (int)XModbusPdu_dataSize((const XModbusPdu*)reply->m_rawResult);
    }
    if (outSize) *outSize = size;
    return data;
}

/** @brief 应答功能码（原始字节，含异常位）。 */
static uint8_t kReplyFc(const XModbusReply* reply)
{
    return (reply && reply->m_rawResult)
               ? (uint8_t)XModbusPdu_functionCodeRaw(
                     (const XModbusPdu*)reply->m_rawResult)
               : 0u;
}

/** @brief DCBA（整字节反序=小端 float32）两字 → double（站 2 口径）。 */
static double kDcbaRegsToFloat(uint16_t r0, uint16_t r1)
{
    /* 线上 4 字节 = r0 高、r0 低、r1 高、r1 低；DCBA 整 4 字节反序
     * （实测帧 4F DD 37 3D → 位模式 0x3D37DD4F = 0.0449f，验算锁）。 */
    const uint32_t bits = ((uint32_t)(r1 & 0xFFu) << 24) |
                          ((uint32_t)(r1 >> 8) << 16) |
                          ((uint32_t)(r0 & 0xFFu) << 8) |
                          (uint32_t)(r0 >> 8);
    return kBitsToFloat(bits);
}

/** @brief ABCD（高字在前，站 5 口径）两字 → double。 */
static double kAbcdRegsToFloat(uint16_t hi, uint16_t lo)
{
    return kBitsToFloat(((uint32_t)hi << 16) | (uint32_t)lo);
}

/**
 * @brief K6：应答时延旋钮（宏缺省=实测值 + setter/getter 可配 + 装配接线）。
 * @details [不锁耗时] 时延数值本身不做毫秒级断言（CI 抖动会假红）——只锁
 *          旋钮存在、可配、且配置后应答路径照常工作（行为方向）。
 */
static void testK6_replyLatencyKnob(void)
{
    struct XVirtualModelTurbidity* t1m = NULL;
    XModbusVirtualClient* c = NULL;
    XModbusReply* reply = NULL;
    int lo = -1;
    int hi = -1;

    XPrintf("--- K6 应答时延旋钮（实测口径缺省 + 可配）---\n");

    /* ---- 宏缺省锁（换口径只改这里；与 Python config.py 逐值同口径）---- */
    CHECK("K6 站1 时延宏缺省 37~43ms（实测文档 §1.1 响应时延列）",
          HSF_SIM_TURBIDITY1_REPLY_MS_MIN == 37 &&
          HSF_SIM_TURBIDITY1_REPLY_MS_MAX == 43,
          "%u~%u", (unsigned)HSF_SIM_TURBIDITY1_REPLY_MS_MIN,
          (unsigned)HSF_SIM_TURBIDITY1_REPLY_MS_MAX);
    CHECK("K6 站2 时延宏缺省 31~33ms",
          HSF_SIM_TURBIDITY2_REPLY_MS_MIN == 31 &&
          HSF_SIM_TURBIDITY2_REPLY_MS_MAX == 33,
          "%u~%u", (unsigned)HSF_SIM_TURBIDITY2_REPLY_MS_MIN,
          (unsigned)HSF_SIM_TURBIDITY2_REPLY_MS_MAX);
    CHECK("K6 站5 时延宏缺省 68~69ms（49 字节长帧，最慢一台）",
          HSF_SIM_FLOWMETER_REPLY_MS_MIN == 68 &&
          HSF_SIM_FLOWMETER_REPLY_MS_MAX == 69,
          "%u~%u", (unsigned)HSF_SIM_FLOWMETER_REPLY_MS_MIN,
          (unsigned)HSF_SIM_FLOWMETER_REPLY_MS_MAX);
    CHECK("K6 恢复期窗口宏缺省 125ms（实测门槛 §8.3）",
          HSF_SIM_RECOVERY_MS == 125, "%u", (unsigned)HSF_SIM_RECOVERY_MS);

    /* ---- 旋钮存在且可配（0~0 与实测档两态都应答照常）---- */
    t1m = XVirtualModelTurbidity_create(0);
    c = t1m ? XModbusVirtualClient_create(NULL) : NULL;
    if (t1m && c) {
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        (void)XModbusVirtualClient_bindUnit(c, HSF_TURBIDITY1_UNIT_ID,
                                            XVirtualModel_base(t1m));
        /* [先绑后开再发] Open 的"有模型"判据是绑定表非空（缺省模型为
         * NULL）；SendRawRequest 在未连接态直接拒绝（与真实客户端同一道
         * 状态闸）——K3 走 HsfServerBus_openWithClient，这里手动绑+开。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c)) {
            CHECK("K6 虚拟客户端 Open", false);
        }
        /* [缺省零时延] 既有用法一字不改：未配时延的客户端立刻应答。 */
        CHECK("K6 未配置时延 ⇒ 缺省（0,0）",
              XModbusVirtualClient_unitReplyLatency(c, 1, &lo, &hi) &&
              lo == 0 && hi == 0, "lo=%d hi=%d", lo, hi);
        XModbusVirtualClient_setUnitReplyLatency(c, 1, 0, 0);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K6 时延 (0,0)：读应答照常（方向断言，不量耗时）",
              reply != NULL && kReplyFc(reply) == 0x03,
              "fc=0x%02X", kReplyFc(reply));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        /* [实测档] 设成文档区间后同样应答（时延在从站应答路径，主站
         * 等待语义与真串口一致；不在这里量毫秒）。 */
        XModbusVirtualClient_setUnitReplyLatency(c, 1, 37, 43);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K6 时延 (37,43)：读应答照常到达（可配生效）",
              reply != NULL && kReplyFc(reply) == 0x03,
              "fc=0x%02X", kReplyFc(reply));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        CHECK("K6 getter 回读 (37,43)（成对旋钮可查）",
              XModbusVirtualClient_unitReplyLatency(c, 1, &lo, &hi) &&
              lo == 37 && hi == 43, "lo=%d hi=%d", lo, hi);
        /* [夹紧与越界] 负值夹 0、lo>hi 收敛、越界站号忽略。 */
        XModbusVirtualClient_setUnitReplyLatency(c, 1, -5, 3);
        CHECK("K6 负下限夹 0 且 lo<=hi 收敛",
              XModbusVirtualClient_unitReplyLatency(c, 1, &lo, &hi) &&
              lo == 0 && hi == 3, "lo=%d hi=%d", lo, hi);
        XModbusVirtualClient_setUnitReplyLatency(c, 999, 5, 9);
        CHECK("K6 越界站号被忽略（不越界写数组）",
              XModbusVirtualClient_unitReplyLatency(c, 999, &lo, &hi) == false);
        flushDeferred();
    } else {
        CHECK("K6 时延旋钮夹具建成", false);
    }
    XClassDelete((XClass*)c);
    if (t1m) XFree_System(t1m);
    flushDeferred();

    /* ---- 模拟装配处接线锁：createBusClient 必须把实测档接到三站上 ---- */
    {
        XModbusVirtualClient* sim = HsfServerIoSim_createBusClient(
            false, false, true, true);
        CHECK("K6 模拟装配路 B 客户端建成", sim != NULL);
        if (sim) {
            CHECK("K6 装配接线：站1 (37,43)",
                  XModbusVirtualClient_unitReplyLatency(
                      sim, HSF_TURBIDITY1_UNIT_ID, &lo, &hi) &&
                  lo == 37 && hi == 43, "lo=%d hi=%d", lo, hi);
            CHECK("K6 装配接线：站2 (31,33)",
                  XModbusVirtualClient_unitReplyLatency(
                      sim, HSF_TURBIDITY2_UNIT_ID, &lo, &hi) &&
                  lo == 31 && hi == 33, "lo=%d hi=%d", lo, hi);
            CHECK("K6 装配接线：站5 (68,69)",
                  XModbusVirtualClient_unitReplyLatency(
                      sim, HSF_FLOWMETER_UNIT_ID, &lo, &hi) &&
                  lo == 68 && hi == 69, "lo=%d hi=%d", lo, hi);
            CHECK("K6 装配接线：恢复期闸 125ms（站5→站1）",
                  XModbusVirtualClient_recoveryWindowMs(sim) == 125);
            XClassDelete((XClass*)sim);
        }
        HsfServerIoSim_destroy();     /* 单例模型是裸 malloc，显式回收。 */
        flushDeferred();
    }
}

/**
 * @brief K7：站 1「长帧后恢复期」行为锁。
 * @details 时延全配 0（确定性；恢复期窗口本身是毫秒判据，不靠时延）：
 *   a) 站 5 读 22 寄存器（=现场 49 字节长帧）成功；
 *   b) 立刻问站 1 → 无应答（NULL，上层按超时处理）+ 恢复期计数 1；
 *   c) 窗口外（spinMs 130 > 125）再问 → 恢复应答，计数不再涨；
 *   d) 旋钮可配：窗口 0 = 关闸（立刻问也照常应答）；
 *   e) 长帧站号不匹配时不误伤（把闸指向不存在的站 → 站 1 照常应答）。
 */
static void testK7_station1RecoveryGate(void)
{
    struct XVirtualModelTurbidity* t1m = NULL;
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelFlow* fm = NULL;
    XModbusVirtualClient* c = NULL;
    XModbusReply* reply = NULL;
    int dataSize = 0;

    XPrintf("--- K7 站1 长帧后恢复期（125ms 无应答窗口）---\n");
    t1m = XVirtualModelTurbidity_create(0);
    t2m = XVirtualModelTurbidity_create(1);
    fm = XVirtualModelFlow_create();
    c = (t1m && t2m && fm) ? XModbusVirtualClient_create(NULL) : NULL;
    if (t1m && t2m && fm && c) {
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        XVirtualModelTurbidity_setNtu(t2m, 0.045);
        XVirtualModelFlow_setInstantFlow(fm, 3.6f);
        (void)XModbusVirtualClient_bindUnit(c, HSF_TURBIDITY1_UNIT_ID,
                                            XVirtualModel_base(t1m));
        (void)XModbusVirtualClient_bindUnit(c, HSF_TURBIDITY2_UNIT_ID,
                                            XVirtualModel_base(t2m));
        (void)XModbusVirtualClient_bindUnit(c, HSF_FLOWMETER_UNIT_ID,
                                            XVirtualModel_base(fm));
        /* [先绑后开再发] 同 K6：绑定表非空才能 Open，未连接态发帧被拒。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c)) {
            CHECK("K7 虚拟客户端 Open", false);
        }
        XModbusVirtualClient_setUnitReplyLatency(c, HSF_TURBIDITY1_UNIT_ID,
                                                 0, 0);
        XModbusVirtualClient_setUnitReplyLatency(c, HSF_FLOWMETER_UNIT_ID,
                                                 0, 0);
        XModbusVirtualClient_setRecoveryGate(c, HSF_FLOWMETER_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 125);
        CHECK("K7 闸配置可读（窗口 getter == 125）",
              XModbusVirtualClient_recoveryWindowMs(c) == 125);

        /* a) 站 5 长帧：22 寄存器 = 44 字节数据 + 1 字节 byteCount = 45。 */
        reply = kSendReadRaw(c, HSF_FLOWMETER_UNIT_ID, 0x04, 0x1010, 22);
        CHECK("K7 站5 长帧（22 寄存器）应答照常",
              reply != NULL && kReplyFc(reply) == 0x04,
              "fc=0x%02X", kReplyFc(reply));
        (void)kReplyData(reply, &dataSize);
        CHECK("K7 长帧应答 45 字节（1 byteCount + 44 数据）",
              reply != NULL && dataSize == 45, "size=%d", dataSize);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();

        /* b) 长帧收口后立刻问站 1 ⇒ 无应答（真机 0ms 间隙 2/10 的死区）。 */
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 恢复期内问站1 ⇒ 无应答（NULL=上层按超时处理）",
              reply == NULL);
        CHECK("K7 恢复期拒答计数 = 1",
              XModbusVirtualClient_recoveryDropCount(c) == 1,
              "count=%llu",
              (unsigned long long)XModbusVirtualClient_recoveryDropCount(c));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();

        /* c) 窗口外（130ms > 125ms）恢复应答，计数不涨。 */
        spinMs(130);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 窗口外问站1 ⇒ 恢复应答",
              reply != NULL && kReplyFc(reply) == 0x03,
              "fc=0x%02X", kReplyFc(reply));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        CHECK("K7 窗口外拒答计数不涨（仍 1）",
              XModbusVirtualClient_recoveryDropCount(c) == 1);
        flushDeferred();

        /* d) 旋钮可配：窗口 0 = 关闸（立刻问站 1 照常应答）。 */
        XModbusVirtualClient_setRecoveryGate(c, HSF_FLOWMETER_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 0);
        CHECK("K7 窗口 0 ⇒ 闸关闭（getter==0）",
              XModbusVirtualClient_recoveryWindowMs(c) == 0);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 关闸后立刻问站1 ⇒ 照常应答",
              reply != NULL && kReplyFc(reply) == 0x03);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();

        /* e) 长帧站不匹配 ⇒ 不误伤（站 2 的短帧不该触发站 1 死区）。 */
        XModbusVirtualClient_setRecoveryGate(c, HSF_TURBIDITY2_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 125);
        reply = kSendReadRaw(c, HSF_TURBIDITY2_UNIT_ID, 0x03, 0x0000, 4);
        CHECK("K7 闸指向站2：短帧（4 寄存器）不算长帧",
              reply != NULL && kReplyFc(reply) == 0x03);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 短帧后问站1 ⇒ 不进死区（照常应答）",
              reply != NULL && kReplyFc(reply) == 0x03);
        CHECK("K7 全程恢复期拒答计数仍 = 1",
              XModbusVirtualClient_recoveryDropCount(c) == 1);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();
    } else {
        CHECK("K7 恢复期夹具建成", false);
    }
    XClassDelete((XClass*)c);
    if (t1m) XFree_System(t1m);
    if (t2m) XFree_System(t2m);
    if (fm) XFree_System(fm);
    flushDeferred();
}

/**
 * @brief K8：站 2 功能码怪癖（FC04 请求 → 应答功能码恒 03）。
 * @details 实测文档 §5.5/附录 A：`02 04 00 00 00 02 …` → `02 03 04 …`。
 *   按「应答功能码 == 请求功能码」校验的主站会误判——模拟从站必须把这条
 *   怪癖如实带上线上帧。站 1 WZT-701A 没有 FC04（仍异常应答 fc|0x80）。
 */
static void testK8_station2FunctionCodeQuirk(void)
{
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelTurbidity* t1m = NULL;
    XModbusVirtualClient* c2 = NULL;
    XModbusVirtualClient* c1 = NULL;
    XModbusReply* fc3Reply = NULL;
    XModbusReply* fc4Reply = NULL;
    const uint8_t* d3 = NULL;
    const uint8_t* d4 = NULL;
    int n3 = 0;
    int n4 = 0;

    XPrintf("--- K8 站2 功能码怪癖（FC04 请求也回 FC03）---\n");
    t2m = XVirtualModelTurbidity_create(1);
    c2 = t2m ? XModbusVirtualClient_create(NULL) : NULL;
    if (t2m && c2) {
        XVirtualModelTurbidity_setWordOrder(
            t2m, (int)XVirtualModelTurbWordOrder_Dcba);
        XVirtualModelTurbidity_setNtu(t2m, 0.045);
        XVirtualModelTurbidity_setTempC(t2m, 19.3);
        (void)XModbusVirtualClient_bindUnit(c2, HSF_TURBIDITY2_UNIT_ID,
                                            XVirtualModel_base(t2m));
        /* [先绑后开再发] 同 K6：绑定表非空才能 Open，未连接态发帧被拒。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c2)) {
            CHECK("K8 站2 虚拟客户端 Open", false);
        }
        fc3Reply = kSendReadRaw(c2, HSF_TURBIDITY2_UNIT_ID, 0x03,
                                0x0000, 4);
        fc4Reply = kSendReadRaw(c2, HSF_TURBIDITY2_UNIT_ID, 0x04,
                                0x0000, 4);
        CHECK("K8 站2 FC03 读 4 寄存器应答照常（fc=0x03）",
              fc3Reply != NULL && kReplyFc(fc3Reply) == 0x03,
              "fc=0x%02X", kReplyFc(fc3Reply));
        CHECK("K8 站2 FC04 请求被受理（有应答，不是 NULL/异常）",
              fc4Reply != NULL && kReplyFc(fc4Reply) == 0x03,
              "fc=0x%02X（期望恒回 03，不是 04/0x84）",
              kReplyFc(fc4Reply));
        d3 = kReplyData(fc3Reply, &n3);
        d4 = kReplyData(fc4Reply, &n4);
        CHECK("K8 FC04 应答数据与 FC03 逐字节一致（同一份寄存器）",
              d3 != NULL && d4 != NULL && n3 == 9 && n4 == 9 &&
              memcmp(d3, d4, (size_t)n3) == 0,
              "fc3=%d 字节 fc4=%d 字节", n3, n4);
        /* [数据口径] DCBA：浊度 <f(0..3)、温度 <f(4..7)——基准 0.045/19.3
         * 与 K3 同源（怪癖只动功能码，不动数据）。 */
        if (d4 && n4 >= 9) {
            const double ntu = kDcbaRegsToFloat(
                (uint16_t)((d4[1] << 8) | d4[2]),
                (uint16_t)((d4[3] << 8) | d4[4]));
            CHECK("K8 FC04 应答里的浊度 == 0.045（DCBA 保真不变）",
                  ntu == (double)(float)0.045, "ntu=%.7g", ntu);
        }
        if (fc3Reply) { XClassDelete((XClass*)fc3Reply); fc3Reply = NULL; }
        if (fc4Reply) { XClassDelete((XClass*)fc4Reply); fc4Reply = NULL; }
        flushDeferred();
    } else {
        CHECK("K8 站2 夹具建成", false);
    }
    XClassDelete((XClass*)c2);
    if (t2m) XFree_System(t2m);
    flushDeferred();

    /* 站 1 无 FC04：仍按异常应答拒绝（fc|0x80）——本条怪癖只属站 2。 */
    t1m = XVirtualModelTurbidity_create(0);
    c1 = t1m ? XModbusVirtualClient_create(NULL) : NULL;
    if (t1m && c1) {
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        (void)XModbusVirtualClient_bindUnit(c1, HSF_TURBIDITY1_UNIT_ID,
                                            XVirtualModel_base(t1m));
        /* [先绑后开再发] 同 K6。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c1)) {
            CHECK("K8 站1 虚拟客户端 Open", false);
        }
        fc4Reply = kSendReadRaw(c1, HSF_TURBIDITY1_UNIT_ID, 0x04,
                                0x0000, 2);
        CHECK("K8 站1 FC04 仍被拒（异常应答 fc=0x84）",
              fc4Reply != NULL && kReplyFc(fc4Reply) == 0x84,
              "fc=0x%02X", kReplyFc(fc4Reply));
        if (fc4Reply) { XClassDelete((XClass*)fc4Reply); fc4Reply = NULL; }
        flushDeferred();
    } else {
        CHECK("K8 站1 夹具建成", false);
    }
    XClassDelete((XClass*)c1);
    if (t1m) XFree_System(t1m);
    flushDeferred();
}

/**
 * @brief K9：数值微抖动方向锁（实测 §1.3 分台口径）。
 * @details 40s × 20 轮三台混跑：站 1 浊度 2.29 完全恒定、站 2 在
 *   0.04458~0.04477 缓慢波动且温度 19.25~19.3125、站 5 流量按基准正弦
 *   微动。断言用**带内**口径（不锁逐拍相位——波动周期 45s/60s，CI 里
 *   采样窗口只覆盖一小段相位，锁相位必然假红）。
 */
static void testK9_microWobbleBands(void)
{
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelTurbidity* t1m = NULL;
    struct XVirtualModelFlow* fm = NULL;
    int i;
    bool allInBand = true;
    bool tempInBand = true;

    XPrintf("--- K9 数值微抖动（站1 恒定 / 站2 ±0.001+温度漂移 / 站5 ±2%）---\n");

    /* ---- 站 2：0.045±0.001 缓慢波动 + 温度 19.3±0.05 漂移 ---- */
    t2m = XVirtualModelTurbidity_create(1);
    if (t2m) {
        XVirtualModelTurbidity_setWordOrder(
            t2m, (int)XVirtualModelTurbWordOrder_Dcba);
        XVirtualModelTurbidity_setWobble(t2m, 0.001);
        XVirtualModelTurbidity_setTempC(t2m, 19.3);
        XVirtualModelTurbidity_setTempWobble(t2m, 0.05);
        for (i = 0; i < 5; ++i) {
            uint16_t out[4] = {0, 0, 0, 0};
            uint16_t n = 0;
            if (XModbusVirtualModel_read(XVirtualModel_base(t2m), 0x03,
                                         0x0000, 4, out, 4, &n) !=
                XModbusVirtual_Ok) {
                allInBand = false;
                tempInBand = false;
                break;
            }
            {
                const double ntu = kDcbaRegsToFloat(out[0], out[1]);
                const double temp = kDcbaRegsToFloat(out[2], out[3]);
                if (!(ntu >= 0.0438 && ntu <= 0.0462)) allInBand = false;
                if (!(temp >= 19.24 && temp <= 19.36)) tempInBand = false;
            }
            spinMs(30);
        }
        CHECK("K9 站2 浊度带内 0.045±0.001（含 float32 圆整余量）", allInBand);
        CHECK("K9 站2 温度带内 19.3±0.05（缓慢漂移）", tempInBand);
        /* [幅 0 = 恒定] setWobble(0) 与站 1 的"实测不动"同款口径——
         * 配 0 后两拍逐位相等（恒真断言没意义，直接用读回证明）。 */
        XVirtualModelTurbidity_setWobble(t2m, 0.0);
        XVirtualModelTurbidity_setTempWobble(t2m, 0.0);
        {
            uint16_t a[4] = {0, 0, 0, 0};
            uint16_t b[4] = {0, 0, 0, 0};
            uint16_t n = 0;
            (void)XModbusVirtualModel_read(XVirtualModel_base(t2m), 0x03,
                                           0x0000, 4, a, 4, &n);
            spinMs(25);
            (void)XModbusVirtualModel_read(XVirtualModel_base(t2m), 0x03,
                                           0x0000, 4, b, 4, &n);
            CHECK("K9 站2 幅 0 后两拍逐位恒定（站1 同款口径可用在任一台）",
                  memcmp(a, b, sizeof(a)) == 0);
        }
    } else {
        CHECK("K9 站2 夹具建成", false);
    }
    if (t2m) XFree_System(t2m);
    flushDeferred();

    /* ---- 站 1：恒定 2.29（实测 20 轮最小=最大）---- */
    t1m = XVirtualModelTurbidity_create(0);
    if (t1m) {
        bool constant = true;
        uint16_t first[2] = {0, 0};
        uint16_t n = 0;
        XVirtualModelTurbidity_setWordOrder(
            t1m, (int)XVirtualModelTurbWordOrder_Cdab);
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        XVirtualModelTurbidity_setWobble(t1m, 0.0);   /* 实测口径：不动。 */
        for (i = 0; i < 6; ++i) {
            uint16_t out[2] = {0, 0};
            (void)XModbusVirtualModel_read(XVirtualModel_base(t1m), 0x03,
                                           0x0000, 2, out, 2, &n);
            if (i == 0) { first[0] = out[0]; first[1] = out[1]; }
            else if (out[0] != first[0] || out[1] != first[1]) constant = false;
            spinMs(20);
        }
        CHECK("K9 站1 浊度恒定（6 拍逐位不动；实测 20 轮最小=最大=2.29）",
              constant && first[0] == 0x8F5Cu && first[1] == 0x4012u,
              "reg=[0x%04X,0x%04X]", (unsigned)first[0], (unsigned)first[1]);
    } else {
        CHECK("K9 站1 夹具建成", false);
    }
    if (t1m) XFree_System(t1m);
    flushDeferred();

    /* ---- 站 5：基准 3.6 ±2% 正弦微波动 + 空管 False ---- */
    fm = XVirtualModelFlow_create();
    if (fm) {
        bool flowInBand = true;
        bool emptyFalse = true;
        uint16_t n = 0;
        XVirtualModelFlow_setBaseFlow(fm, 3.6);   /* 恢复波动口径。 */
        for (i = 0; i < 5; ++i) {
            uint16_t out[2] = {0, 0};
            uint16_t alarm[1] = {1};
            if (XModbusVirtualModel_read(XVirtualModel_base(fm), 0x04,
                                         0x1010, 2, out, 2, &n) !=
                XModbusVirtual_Ok ||
                XModbusVirtualModel_read(XVirtualModel_base(fm), 0x04,
                                         0x1024, 1, alarm, 1, &n) !=
                XModbusVirtual_Ok) {
                flowInBand = false;
                emptyFalse = false;
                break;
            }
            {
                const double flow = kAbcdRegsToFloat(out[0], out[1]);
                /* ±2% 正弦 = [3.528, 3.672]；带给 float32 圆整余量。 */
                if (!(flow >= 3.52 && flow <= 3.68)) flowInBand = false;
                if (alarm[0] != 0u) emptyFalse = false;
            }
            spinMs(60);
        }
        CHECK("K9 站5 流量带内 3.6±2%（正弦微波动，不再恒 3.6 钉死）",
              flowInBand);
        CHECK("K9 站5 空管报警维持 False（0x1024==0）", emptyFalse);
        /* [钉死口径不回退] setInstantFlow(≥0) 仍 = 恒定精确值（K3 用它）。 */
        XVirtualModelFlow_setInstantFlow(fm, 3.6f);
        {
            bool fixed = true;
            for (i = 0; i < 3; ++i) {
                uint16_t out[2] = {0, 0};
                (void)XModbusVirtualModel_read(XVirtualModel_base(fm), 0x04,
                                               0x1010, 2, out, 2, &n);
                if (kAbcdRegsToFloat(out[0], out[1]) != (double)3.6f)
                    fixed = false;
                spinMs(30);
            }
            CHECK("K9 setInstantFlow(3.6f) 钉死口径不回退（逐位恒 3.6）",
                  fixed);
        }
    } else {
        CHECK("K9 站5 夹具建成", false);
    }
    if (fm) XFree_System(fm);
    flushDeferred();
}

/** @brief K4：§K 全部工作集（K1+K2+K3+K5+K6~K9）跑完堆必须回基线（对齐 §G）。 */
static void testK4_heapBackToBaseline(void)
{
    uint64_t base = 0;
    uint64_t now = 0;
    int i;

    XPrintf("--- K4 §K 泄漏自检 ---\n");
    if (!XMemory_statisticsEnabled()) {
        XPrintf("[SKIP] K4 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    testK3_simRouteBFidelityOnce();     /* 预热：摊平站/组/Hub 惰性初始化。 */
    testK6_replyLatencyKnob();          /* 摊平 HsfServerIoSim 单例惰性初始化。 */
    flushDeferred();
    base = heapNow();
    for (i = 0; i < 3; ++i) {
        testK1_wordOrderDocumentedFrames();
        testK3_simRouteBFidelityOnce();
        testK5_preTxQuietGate();
        testK6_replyLatencyKnob();
        testK7_station1RecoveryGate();
        testK8_station2FunctionCodeQuirk();
        testK9_microWobbleBands();
        flushDeferred();
    }
    now = heapNow();
    CHECK("K4 §K 三站装配/拆除 3 轮堆增量回基线（0）", now == base,
          "基线=%llu 现在=%llu 差=%lld", (unsigned long long)base,
          (unsigned long long)now, (long long)(now - base));
}

static void testK_threeDeviceAccess(void)
{
    XPrintf("--- §K 三设备接入（ttyS4 路 B：浊度×2 + 流量计）---\n");
    testK1_wordOrderDocumentedFrames();
    testK2_unitIdConfigLock();
    testK5_preTxQuietGate();
    testK6_replyLatencyKnob();
    testK7_station1RecoveryGate();
    testK8_station2FunctionCodeQuirk();
    testK9_microWobbleBands();
    testK4_heapBackToBaseline();        /* 内部含 K3/K6 三轮 + 预热。 */
}

#endif /* XINDUSTRY_ON && XTURBIDITY_ON && XMODBUS_HUB_ON */

/* ============================================================================
 * §N 现场口径对齐（2026-11-10 板端实机复核，基准＝Python 服务端）
 *
 * [背景] 板端 C 服务端与 Python 服务端连的是**同一批真实设备**，但两边
 *        对同一张 DI 位图读出了不同的现场结论。逐条量出来的差异：
 *          N1 阀反馈 DI 位序：C 端「关在前」、板端 Python cfg「开在前」
 *             ⇒ 同一个 DI 位图，C 端判「阀1 开到位」、Python 判「阀1
 *             关到位」——**结论反了**，不是显示差异。现场设备定义以
 *             Python 服务端为准（用户已实机核对），C 端同步。
 *          N2 侧号由谁定：浊度仪/流量计挂在 B 侧是**服务端装配事实**，
 *             客户端不该在请求里点名。早先快照按请求里的 side 取设备，
 *             而客户端请求不带 side（默认 A 侧）⇒ 服务端去 A 侧找浊度仪
 *             （那儿没装）⇒ 回空帧 ⇒ 客户端页面恒"--"。板端实测：同一台
 *             服务器带 side=1 出 v0_ntu=229/v1_ntu=4，不带 side 全空。
 *             修法＝服务端解析实际装配侧（Python 侧同口径）。
 *          N3 流量计健康：告警位里「空管」在泵停/无流时必然置位，早先
 *             「alarms != 0 ⇒ 故障」把首页显示成「电磁流量计 故障」。
 *             只有**系统报警**算设备告警（Python 同口径）。
 *          N4 数字量模块名：两端首页设备表行名要对得上（现场登记名）。
 *
 * [钉法] 全部走公开 API，不复刻实现：
 *          N1 钉点位宏（它是阀反馈与通道名的唯一事实源）；
 *          N2 钉 IO 层的侧号解析（起模拟 IO ＝ 现场拓扑：A 侧 VFD/温湿度，
 *             B 侧浊度×2/流量计）；
 *          N3 钉健康判定的公开函数；
 *          N4 钉名字宏。
 * ==========================================================================*/
#if XINDUSTRY_ON && XMODBUS_HUB_ON

/**
 * @brief N1：阀反馈 DI 位序＝现场接线（开到位在前）。
 */
static void testN1_valveDiOrderFieldWiring(void)
{
    int i;
    int bad = 0;
    XPrintf("--- N1 阀反馈 DI 位序（现场：开在前）---\n");
    for (i = 0; i < 6; ++i) {
        int op = HSF_VALVE_OPEN_DI(i);
        int cl = HSF_VALVE_CLOSE_DI(i);
        if (op != 2 * i || cl != 2 * i + 1) ++bad;
    }
    CHECK("N1 阀 n：开到位 DI=2n、关到位 DI=2n+1（现场接线，板端 cfg 口径）",
          bad == 0, "不合的路数=%d", bad);
    CHECK("N1 开/关两路相邻且不重合（12 个 DI 被 6 路两两分完）",
          HSF_VALVE_OPEN_DI(0) == 0 && HSF_VALVE_CLOSE_DI(0) == 1 &&
          HSF_VALVE_OPEN_DI(5) == 10 && HSF_VALVE_CLOSE_DI(5) == 11 &&
          HSF_VALVE_CLOSE_DI(0) != HSF_VALVE_OPEN_DI(1));
    CHECK("N1 动作出口仍是 DO n（未受位序订正影响）",
          HSF_VALVE_DO(0) == 0 && HSF_VALVE_DO(5) == 5);
}

/**
 * @brief N2：侧号由服务端解析（请求侧没装 ⇒ 回落到实际装配侧）。
 */
static void testN2_sideResolvedByServer(void)
{
    XPrintf("--- N2 侧号由服务端解析 ---\n");
    /* 模拟 IO 的拓扑＝现场拓扑：路 A = 变频器 + 温湿度；路 B = 浊度×2 +
     * 流量计（板端实测：浊度/流量计都在 ttyS4 这条线上）。 */
    HsfServerIo_setSimMode(true);
    if (!HsfServerIo_start()) {
        CHECK("N2 模拟 IO 启动", false);
        return;
    }
    CHECK("N2 现场拓扑：浊度仪/流量计在 B 侧、A 侧没有",
          HsfServerIo_turbidity(HsfSide_A, 0) == NULL &&
          HsfServerIo_turbidity(HsfSide_B, 0) != NULL &&
          HsfServerIo_flowMeter(HsfSide_A) == NULL &&
          HsfServerIo_flowMeter(HsfSide_B) != NULL);
    CHECK("N2 请求侧没装 ⇒ 回落到实际装配侧（请求 A ⇒ 答 B）",
          HsfServerIo_turbiditySide(HsfSide_A) == HsfSide_B &&
          HsfServerIo_flowMeterSide(HsfSide_A) == HsfSide_B,
          "浊度侧=%d 流量计侧=%d", (int)HsfServerIo_turbiditySide(HsfSide_A),
          (int)HsfServerIo_flowMeterSide(HsfSide_A));
    CHECK("N2 请求侧装了 ⇒ 就用它（请求 B ⇒ 答 B）",
          HsfServerIo_turbiditySide(HsfSide_B) == HsfSide_B &&
          HsfServerIo_flowMeterSide(HsfSide_B) == HsfSide_B);
    HsfServerIo_stop();
}

/**
 * @brief N3：流量计健康只认系统报警（空管/上下限是工况信息）。
 */
static void testN3_flowMeterAlarmHealth(void)
{
    XPrintf("--- N3 流量计告警口径 ---\n");
    CHECK("N3 无告警 + 有数据 ⇒ Ok",
          HsfServerIoFlowMeter_health(0, true) == HsfDeviceHealth_Ok);
    CHECK("N3 空管位（bit2=4，泵停时必然置位）⇒ 仍是 Ok 不判故障",
          HsfServerIoFlowMeter_health(4, true) == HsfDeviceHealth_Ok,
          "得到=%d", HsfServerIoFlowMeter_health(4, true));
    CHECK("N3 上限/下限位 ⇒ 仍是 Ok（工况信息）",
          HsfServerIoFlowMeter_health(1, true) == HsfDeviceHealth_Ok &&
          HsfServerIoFlowMeter_health(2, true) == HsfDeviceHealth_Ok);
    CHECK("N3 系统报警位（bit3=8）⇒ Warning",
          HsfServerIoFlowMeter_health(8, true) == HsfDeviceHealth_Warning,
          "得到=%d", HsfServerIoFlowMeter_health(8, true));
    CHECK("N3 系统报警 + 空管 ⇒ 仍 Warning（系统位优先）",
          HsfServerIoFlowMeter_health(12, true) == HsfDeviceHealth_Warning);
    CHECK("N3 无告警但无数据 ⇒ Stale",
          HsfServerIoFlowMeter_health(0, false) == HsfDeviceHealth_Stale);
}

/**
 * @brief N4：数字量模块名两端一致（现场登记名「数字量2」）。
 */
static void testN4_digitalModuleName(void)
{
    XPrintf("--- N4 数字量模块名 ---\n");
    CHECK("N4 模块名 == 「数字量2」（板端 Python cfg 同名）",
          strcmp(HSF_DIG_MODULE_NAME, "数字量2") == 0, "得到=%s",
          HSF_DIG_MODULE_NAME);
}

/**
 * @brief N5：采集节键表覆盖「两台浊度 + 流量计」（逐台可设）。
 * @details [2026-11-10 现场要求] 两台浊度仪与流量计的采集周期必须各自可设：
 *          站1 有 200ms 发帧前静默期、站3 没有；流量计 49 字节长帧最慢。
 *          早期协议只有一台共用的 turbidityPollMs、流量计没有键（跟随温湿度）
 *          ⇒ 远端设置里"缺了几台设备的采集设置"。本锁钉住键字面（两端必须
 *          逐值一致）与「逐台字段确实存在且互不覆盖」。
 */
static void testN5_collectPollKeysPerDevice(void)
{
    HsfCollectConfig cfg;

    XPrintf("--- N5 采集节：两台浊度 + 流量计逐台可设 ---\n");
    CHECK("N5 键字面：浊度1/浊度2/流量计（与 Python proto.py 同名）",
          strcmp(HSF_CFGK_TURBIDITY1_POLL_MS, "turbidity1PollMs") == 0 &&
          strcmp(HSF_CFGK_TURBIDITY2_POLL_MS, "turbidity2PollMs") == 0 &&
          strcmp(HSF_CFGK_FLOWMETER_POLL_MS, "flowmeterPollMs") == 0);
    CHECK("N5 兼容键仍在（旧客户端只发 turbidityPollMs 时两台照它跑）",
          strcmp(HSF_CFGK_TURBIDITY_POLL_MS, "turbidityPollMs") == 0);
    memset(&cfg, 0, sizeof(cfg));
    HsfServerIo_collectConfig(&cfg);
    /* 未配 INI 时取宏缺省：两台浊度继承共同缺省，流量计取自己的宏。 */
    CHECK("N5 缺省：两台浊度取共同缺省、流量计取独立宏",
          cfg.turbidity1PollMs == cfg.turbidityPollMs &&
          cfg.turbidity2PollMs == cfg.turbidityPollMs &&
          cfg.flowmeterPollMs == (int)HSF_FLOWMETER_MIN_POLL_MS,
          "t1=%d t2=%d tdef=%d flow=%d", cfg.turbidity1PollMs,
          cfg.turbidity2PollMs, cfg.turbidityPollMs, cfg.flowmeterPollMs);
}

static void testN_fieldAlignment(void)
{
    XPrintf("--- §N 现场口径对齐（板端实机复核）---\n");
    testN1_valveDiOrderFieldWiring();
    testN2_sideResolvedByServer();
    testN3_flowMeterAlarmHealth();
    testN4_digitalModuleName();
    testN5_collectPollKeysPerDevice();
    /* [2026-11-10] 加水温列后的两处位置下标漂移回归锁（时间戳跨零点
     * 日期多一天 + 异常行判据读到正累计列 ⇒ 整表误标粉红）。 */
    testN6_historyTsAndAbnormalCriteria();
}

#endif /* XINDUSTRY_ON && XMODBUS_HUB_ON */

/* ============================================================================
 * §O 变频器遥测刻度（2026-11-10 板端实机复测定案）
 *
 * [钉的是什么] M512 的 0x1001「运行频率」刻度：工程值 = raw / 100（0.01Hz）。
 *   这条曾长期取 ÷10（"0.1Hz"），来源是旧实测定标文档里的一句
 *   「写 0x1000=2000 ⇒ 面板 10Hz、0x1001=100」——**漏了一个 0**。
 *   板端 hd-rk3568 对**真机**复测（现场变频器实跑，两组给定）：
 *     · 给定 9720（97.2% ⇒ 48.6Hz）→ raw = 4860；
 *     · 给定 2000（20%  ⇒ 10.0Hz）→ 稳态 raw = 1000。
 *   两点都精确等于 Hz×100 ⇒ ÷100。物理交叉验证：50Hz 机型（P0.10=50Hz）
 *   不可能输出 486Hz；而 48.6Hz 对应 1399.68rpm 恰为 48.6/50×1440。
 *
 * [为什么必须锁] 取 ÷10 时客户端显示的输出频率与转速**整整差 10 倍**
 *   （板端实测 486Hz / 13996rpm，真实 48.6Hz / 1399.7rpm）——界面在骗人。
 *   Python 侧 devices.py 一直是 ÷100（本锁的基准），两边必须同口径。
 * ==========================================================================*/
#if XVFD_ON

/**
 * @brief O1：M512 点规格表的输出频率除率 == 100（原生 0.01Hz）。
 */
static void testO1_m512FrequencyScale(void)
{
    int count = 0;
    const XVfdPointSpec* specs = XVfdM512_pointSpecs(&count);
    int i;
    int div = -1;

    XPrintf("--- O1 M512 输出频率刻度（实机复测 ÷100）---\n");
    CHECK("O1 取到 M512 点规格表", specs != NULL && count > 0);
    if (!specs) return;
    for (i = 0; i < count; ++i) {
        if (specs[i].m_role == XVfdRole_OutputFreq) {
            div = specs[i].m_div;
            break;
        }
    }
    CHECK("O1 输出频率除率 == 100（raw=4860 ⇔ 48.6Hz）", div == 100,
          "div=%d（改刻度前是 10 ⇒ 显示差 10 倍）", div);
    /* 相邻遥测的除率一并钉住（实机 raw：母线 5448→544.8V ÷10、电压 374→374V
     * ÷1、电流 ÷100、转矩 ÷10）。 */
    {
        int dBus = -1;
        int dVolt = -1;
        int dCur = -1;
        for (i = 0; i < count; ++i) {
            if (specs[i].m_role == XVfdRole_BusVoltage) dBus = specs[i].m_div;
            else if (specs[i].m_role == XVfdRole_OutputVoltage) dVolt = specs[i].m_div;
            else if (specs[i].m_role == XVfdRole_OutputCurrent) dCur = specs[i].m_div;
        }
        CHECK("O1 母线 ÷10 / 输出电压 ÷1 / 输出电流 ÷100（实机标定）",
              dBus == 10 && dVolt == 1 && dCur == 100,
              "母线=%d 电压=%d 电流=%d", dBus, dVolt, dCur);
    }
}

#endif /* XVFD_ON */

/* ============================================================================
 * §P 变频器「带给定启动」必须是**一个**设备动作（2026-11-10 板端实机根因）
 *
 * [现场症状] C 客户端点「启动」恒回「设备拒绝：设备忙（上一动作在途）」，
 *   而同一批设备的 Python 服务端能正常控制。
 *
 * [根因] 服务端原来分两次调用设备层：先 `XVfdDevice_setFrequency`（写
 *   0x1000 给定），再 `XVfdDevice_start`（写 0x2000 命令字）。设备层有
 *   **单飞闸**（m_busy：同一时刻只允许一个动作在途，落定由链路回执收口），
 *   第一笔写把闸占住，第二笔同拍发出 ⇒ 立刻被拒成「设备忙」。
 *
 * [修法] 把两笔合成**一个多阶段动作**（XVfdDevice_startWithGiven）：同一
 *   m_busy 周期内，给定写落定后在 ActionWriteDone 里推进到命令字阶段。
 *
 * [本锁钉什么] M512 必须**重载** EXVfdDevice_StartWithGiven，不能落到基类
 *   缺省实现（缺省实现就是"分两次调用"那条老路）。结构性断言，锁住"别把
 *   重载删了"。
 * ==========================================================================*/
#if XVFD_ON

/** @brief P1：M512 重载了 StartWithGiven（不走"两次调用"的缺省实现）。 */
static void testP1_startWithGivenIsOneAction(void)
{
    XVfdDevice* dev;
    XVfdDevice* base;
    void* m512Impl = NULL;
    void* baseImpl = NULL;

    XPrintf("--- P1 带给定启动＝一个动作（M512 重载）---\n");
    dev = (XVfdDevice*)XVfdM512_create();
    base = XVfdDevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    CHECK("P1 M512 与基类设备都建成", dev != NULL && base != NULL);
    if (dev) {
        m512Impl = XClassGetVirtualFunc((XObject*)dev,
                                        EXVfdDevice_StartWithGiven, void*);
    }
    if (base) {
        baseImpl = XClassGetVirtualFunc((XObject*)base,
                                        EXVfdDevice_StartWithGiven, void*);
    }
    CHECK("P1 基类有缺省实现（两次调用的老路，作为对照）", baseImpl != NULL);
    CHECK("P1 M512 槽位非空", m512Impl != NULL);
    CHECK("P1 M512 走自己的多阶段实现（≠ 基类缺省实现）",
          m512Impl != NULL && baseImpl != NULL && m512Impl != baseImpl);
    if (dev) XClassDelete((XClass*)dev);
    if (base) XClassDelete((XClass*)base);
}

#endif /* XVFD_ON */

/* ============================================================================
 *        §H 历史页「图表缩放取数」专项（2026-11-09 并入本文件）
 *
 * [为什么并进来] 用户口径「所有测试代码集成到一个测试文件，C 端一个、
 * Python 一个」。原来它在独立的 hsf_chart_zoom_test.c 里、由 main.c 的
 * `--test hsf-chart-zoom` 与 §H 两处调用；并进来之后断言只此一份，
 * `--test hsf-regression` 一条命令跑完全部 C 端断言。
 * 子断言编号沿用 Z1..Z11（与本文件 §A..§J 区分，避免重号难排查）。
 * ==========================================================================*/

/* ==================== §1 批次计划 ==================== */

/** @brief 窗口态：窗内行数 ≤ 上限 ⇒ 逐行取（步进 = 批行数 ⇒ 连续覆盖）。 */
static void testZ1_windowPlanIsRowByRow(void)
{
    HsfChartZoomPlan plan;

    /* 窗内 30 行（< 批行数 45）⇒ 一批取完，30 行全在（不跳批、不丢段）。 */
    (void)HsfChartZoom_planBatches(30, 45, true, 12000, 500, 12000, &plan);
    CHECK("Z1 窗口态·窗内 30 行 ⇒ 1 批（一次取完每一行）", plan.batches == 1);
    CHECK("Z1 窗口态·窗内 30 行 ⇒ 步进=批行数（连续无跳段）",
          plan.step == 45);
    CHECK("Z1 窗口态·窗内 30 行 ⇒ 非抽样态（全分辨率）", !plan.sampled);

    /* 窗内 9788 行 ⇒ 218 批 × 45 行 ⇒ 覆盖 9810 ≥ 9788（末批取不满即止）。 */
    (void)HsfChartZoom_planBatches(9788, 45, true, 12000, 500, 12000, &plan);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 218 批（逐行取）", plan.batches == 218);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 步进=批行数（连续覆盖窗内每一行）",
          plan.step == 45);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 覆盖到窗尾（(batches-1)*step+chunk ≥ total）",
          (long)(plan.batches - 1) * plan.step + 45 >= 9788L);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 非抽样态", !plan.sampled);

    /* 窗内行数超上限 ⇒ 退化为窗内抽样（并标记，状态行要注明）。
     * [不是"只取十来个簇"] 超上限时仍要**用满内存上限**均匀铺满整个窗：
     * 按目标点数（500）只取 12 批 × 45 行时，簇与簇之间是空的，缩放到窗内
     * 看到的还是断续线段（用户口径"不够连续"）。 */
    (void)HsfChartZoom_planBatches(20000, 45, true, 12000, 500, 12000, &plan);
    CHECK("Z1 窗口态·窗内 20000 行（超上限）⇒ 标记为窗内抽样", plan.sampled);
    CHECK("Z1 窗口态·窗内 20000 行 ⇒ 用满内存上限铺满窗（maxRows=12000 ⇒ 266 批）",
          plan.batches == 12000 / 45);
    /* [批数必须随内存上限变，而不是恒等于某个常数] 换一个 maxRows 再算一次：
     * 上限真被读取时批数应跟着变（2700/45=60）。只断言"≤ 上限"没有鉴别
     * 力——批数由别的因子决定时那条断言照样恒真。 */
    {
        HsfChartZoomPlan capPlan;
        CHECK("Z1 窗口态·批数随内存上限（maxRows=2700 ⇒ 60 批）",
              HsfChartZoom_planBatches(20000, 45, true, 12000, 500, 2700,
                                       &capPlan) == 2700 / 45
                  && capPlan.sampled);
    }
    CHECK("Z1 窗口态·窗内 20000 行 ⇒ 步长 > 批行数（窗内均匀跳批）",
          plan.step > 45);
    CHECK("Z1 窗口态·窗内 20000 行 ⇒ 末批够到窗尾",
          (long)(plan.batches - 1) * plan.step + 45 >= 20000L);
}

/** @brief 全量态：仍是跨区间跳批抽样（未缩放看全貌，不改成逐行全量）。 */
static void testZ2_fullPlanIsSampled(void)
{
    HsfChartZoomPlan plan;

    (void)HsfChartZoom_planBatches(18632, 45, false, 12000, 500, 12000, &plan);
    CHECK("Z2 全量态·18632 行 ⇒ 批数=ceil(目标点数/批行数)（500/45⇒12 批）",
          plan.batches == 12);
    CHECK("Z2 全量态·步进 > 批行数（跨区间跳批，不是连续取）",
          plan.step > 45);
    CHECK("Z2 全量态·末批够到区尾（(batches-1)*step+chunk ≥ total）",
          (long)(plan.batches - 1) * plan.step + 45 >= 18632L);
    CHECK("Z2 全量态·非窗口态 ⇒ 不标抽样", !plan.sampled);

    /* [上限截断] 目标点数 × 批行数一旦超过内存上限就必须截断，否则暂存
     * 会被撑爆：wantPoints=5000、chunk=45 ⇒ 名义 112 批，而 maxRows=900
     * 只装得下 20 批 ⇒ 实测批数 20（不是 112）。这条锁的是"上限真的被读"，
     * 与上面"≤ 上限"那类恒真断言不同。 */
    {
        HsfChartZoomPlan capPlan;
        (void)HsfChartZoom_planBatches(200000, 45, false, 12000, 5000, 900,
                                       &capPlan);
        CHECK("Z2 全量态·目标点数超上限 ⇒ 按内存上限截断（900/45=20 批）",
              capPlan.batches == 900 / 45);
    }
    /* 库很小（< 2 批）⇒ 按实际行数算，批数不为 0。 */
    (void)HsfChartZoom_planBatches(10, 45, false, 12000, 500, 12000, &plan);
    CHECK("Z2 全量态·库内 10 行 ⇒ 1 批", plan.batches == 1);
    CHECK("Z2 全量态·库内 0 行 ⇒ 0 批（无数据）",
          HsfChartZoom_planBatches(0, 45, false, 12000, 500, 12000, &plan) == 0);
}

/* ==================== §2 可见窗换算（同基准） ==================== */

/** @brief 轴域（相对基准秒）⇄ 绝对时刻（epoch ms）。 */
static void testZ3_visibleWindowUsesSameBase(void)
{
    const int64_t base = 1794210635000LL;   /* 2026-11-09 15:10:35 */
    int64_t lo = 0;
    int64_t hi = 0;

    CHECK("Z3 基准未定时不产出可见窗",
          !HsfChartZoom_visibleWindow(0, -100.0, 0.0, &lo, &hi));
    CHECK("Z3 可见窗换算：base + x*1000（与数据 X 值同基准）",
          HsfChartZoom_visibleWindow(base, -3600.0, 0.0, &lo, &hi) &&
          lo == base - 3600000LL && hi == base);
    CHECK("Z3 可见窗换算：域非法（hi<=lo）不产出",
          !HsfChartZoom_visibleWindow(base, 0.0, 0.0, &lo, &hi));
    CHECK("Z3 可见窗换算：负偏移（早期数据）落在基准之前",
          HsfChartZoom_visibleWindow(base, -234000.0, -171000.0, &lo, &hi) &&
          lo == base - 234000000LL && hi == base - 171000000LL);
}

/** @brief 取数窗外扩（1%，最小 1s）——边界行也取回，曲线才铺满两端。 */
static void testZ4_expandWindow(void)
{
    int64_t lo = 0;
    int64_t hi = 0;

    HsfChartZoom_expandWindow(1000000LL, 2000000LL, &lo, &hi);
    CHECK("Z4 取数窗外扩 1%（左右各 10s）",
          lo == 1000000LL - 10000LL && hi == 2000000LL + 10000LL);
    HsfChartZoom_expandWindow(1000000LL, 1000100LL, &lo, &hi);
    CHECK("Z4 取数窗外扩最小 1s（窄窗按 1s 外扩）",
          lo == 1000000LL - 1000LL && hi == 1000100LL + 1000LL);
}

/* ==================== §3 视口判定 ==================== */

/** @brief 可见窗 ≥ 全量域 90% ⇒ 全量视图（缩回全域 → 恢复全量抽样曲线）。 */
static void testZ5_fullViewDetection(void)
{
    const int64_t fullLo = 1000000LL;
    const int64_t fullHi = 1000000LL + 100000LL;   /* 100s 全量域 */

    CHECK("Z5 可见窗=全量域 ⇒ 全量视图",
          HsfChartZoom_isFullView(fullLo, fullHi, fullLo, fullHi));
    CHECK("Z5 可见窗 95% ⇒ 全量视图",
          HsfChartZoom_isFullView(fullLo + 2500LL, fullHi, fullLo, fullHi));
    CHECK("Z5 可见窗 50% ⇒ 非全量视图（进窗口模式取数）",
          !HsfChartZoom_isFullView(fullLo + 50000LL, fullHi, fullLo, fullHi));
    CHECK("Z5 全量域未知 ⇒ 保守按全量视图（不误触发取数）",
          HsfChartZoom_isFullView(fullLo, fullHi, 0, 0));
}

/** @brief 已取窗覆盖可见窗 ⇒ 不用再取（继续放大不重复取、缩回不空图）。 */
static void testZ6_windowCoverage(void)
{
    CHECK("Z6 可见窗在已取窗内 ⇒ 覆盖（不重复取）",
          HsfChartZoom_windowCovers(1000LL, 2000LL, 1200LL, 1800LL));
    CHECK("Z6 可见窗越过已取窗下界 ⇒ 不覆盖（须重取）",
          !HsfChartZoom_windowCovers(1000LL, 2000LL, 900LL, 1800LL));
    CHECK("Z6 可见窗越过已取窗上界 ⇒ 不覆盖（须重取）",
          !HsfChartZoom_windowCovers(1000LL, 2000LL, 1200LL, 2100LL));
    CHECK("Z6 未持有窗口（全量态）⇒ 不覆盖",
          !HsfChartZoom_windowCovers(0, 0, 1200LL, 1800LL));
}

/* ==================== §4 取数 limit 轮转 ==================== */

/** @brief 相邻两轮 limit 必不相等；与页大小 10/20/50 两两不相交。 */
static void testZ7_limitRotation(void)
{
    int seq;
    int prevProbe = -1;
    int prevChunk = -1;
    int sameProbe = 0;
    int sameChunk = 0;
    int clash = 0;

    for (seq = 0; seq < 72; ++seq) {
        int probe = 0;
        int chunk = 0;
        HsfChartZoom_nextLimits(seq, &probe, &chunk);
        if (probe == prevProbe) sameProbe++;
        if (chunk == prevChunk) sameChunk++;
        prevProbe = probe;
        prevChunk = chunk;
        /* 与页大小/导出大小（10/20/50）撞键 ⇒ 应答会被误认领 */
        if (probe == 10 || probe == 20 || probe == 50) clash++;
        if (chunk == 10 || chunk == 20 || chunk == 50) clash++;
        /* 探测批与正式批互撞 */
        if (probe == chunk) clash++;
    }
    CHECK("Z7 轮转 72 轮：相邻两轮探测批 limit 必不相等", sameProbe == 0);
    CHECK("Z7 轮转 72 轮：相邻两轮正式批 limit 必不相等", sameChunk == 0);
    CHECK("Z7 轮转 72 轮：limit 与页大小 10/20/50 及探测/正式互不撞键",
          clash == 0);
    {
        int probe = 0;
        int chunk = 0;
        HsfChartZoom_nextLimits(0, &probe, &chunk);
        CHECK("Z7 首轮 limit 在约定区间内（探测 1..8 / 正式 41..49）",
              probe >= 1 && probe <= 8 && chunk >= 41 && chunk <= 49);
    }
}

/* ==================== §6 轴域与取数窗同源（根修锁） ==================== */

/** @brief 轴域 → 取数窗只有一份计算（HsfChartZoom_view）。
 *  @details [现场根因] 2026-11-09 端到端取证：窗口取数在途时，"全量态重绘"
 *           把用户的轴域改写成"已到达那几行的时间跨度"（日志实测
 *           `[-13547.301, -10903.727]` → `[-12833.300, -10841.700]`），
 *           而请求窗仍按原轴域发出 ⇒ 取回的点与轴域不同参、两端被裁掉。
 *           修复的判据：**轴域与请求窗必须来自同一份计算**。这里钉住
 *           "视图一次算齐"的三条恒等式，页面只要用同一个视图就不可能错位。 */
static void testZ9_viewIsSingleSource(void)
{
    const int64_t base = 1794210635000LL;   /* 2026-11-09 15:10:35 */
    HsfChartZoomView v;
    int64_t expLo = 0;
    int64_t expHi = 0;

    CHECK("Z9 视图换算成功（基准已定 + 轴域合法）",
          HsfChartZoom_view(base, -13547.301, -10903.727, &v) && v.valid);
    CHECK("Z9 轴域按原值保留（重放轴域用的唯一事实源）",
          v.xLo == -13547.301 && v.xHi == -10903.727);
    HsfChartZoom_expandWindow(v.viewLo, v.viewHi, &expLo, &expHi);
    CHECK("Z9 取数窗 == 轴域绝对窗的外扩（同一份计算，不另算一遍）",
          v.fetchLo == expLo && v.fetchHi == expHi);
    CHECK("Z9 轴域 ⇄ 绝对窗同基准（base + x*1000，与数据 X 值同参）",
          v.viewLo == base + (int64_t)(v.xLo * 1000.0) &&
          v.viewHi == base + (int64_t)(v.xHi * 1000.0));
    CHECK("Z9 取数窗严格包含可见窗（边界行也取回，曲线铺满两端）",
          v.fetchLo < v.viewLo && v.fetchHi > v.viewHi);
    CHECK("Z9 窗宽 = 轴宽 + 2×外扩（左右对称，不偏向一侧）",
          (v.fetchHi - v.fetchLo) ==
              (v.viewHi - v.viewLo) + 2 * (v.viewLo - v.fetchLo));

    /* 换一个轴域 ⇒ 窗跟着换（没有"上一轮残留窗"这种第二事实源）。 */
    {
        HsfChartZoomView v2;
        CHECK("Z9 换轴域 ⇒ 取数窗随之重算（无残留窗）",
              HsfChartZoom_view(base, -3000.0, -2000.0, &v2) &&
              v2.fetchLo != v.fetchLo && v2.viewLo > v.viewLo);
        CHECK("Z9 同一轴域重复换算 ⇒ 同一窗（确定性）",
              HsfChartZoom_view(base, -3000.0, -2000.0, &v2) &&
              v2.fetchLo == base - 3000000LL - 10000LL &&
              v2.fetchHi == base - 2000000LL + 10000LL);
    }

    /* 基准未定 / 域非法 ⇒ 视图无效（页面据此不取数，不能发出 0 窗口请求）。 */
    CHECK("Z9 基准未定 ⇒ 视图无效",
          !HsfChartZoom_view(0, -100.0, 0.0, &v) && !v.valid);
    CHECK("Z9 域非法（hi<=lo）⇒ 视图无效",
          !HsfChartZoom_view(base, 0.0, 0.0, &v) && !v.valid);
    CHECK("Z9 out 为 NULL ⇒ 只判有效性（不写空指针）",
          !HsfChartZoom_view(0, -1.0, 0.0, NULL));
}

/* ==================== §7 取回的点落在窗口内 / 铺满视口 ==================== */

/** @brief ts 访问器（回归锁手里是纯 ts 数组）。 */
static int64_t zoomTestTsAt(const void* user, int index)
{
    const int64_t* ts = (const int64_t*)user;
    return ts[index];
}

/** @brief 数据点时间戳全部落在窗口内，且视口被铺满。 */
static void testZ10_pointsInsideWindow(void)
{
    const int64_t base = 1794210635000LL;
    const int64_t stepMs = 5000;            /* 5s 一行（与服务端采样同量级） */
    HsfChartZoomView v;
    int64_t ts[4200];
    int n = 0;
    int i;
    int inView;

    CHECK("Z10 视图可用（轴域 -2000s..-1000s）",
          HsfChartZoom_view(base, -2000.0, -1000.0, &v) && v.valid);

    /* 模拟服务端按取数窗返回的行：均匀覆盖 [fetchLo, fetchHi]。 */
    for (i = 0; v.fetchLo + (int64_t)i * stepMs <= v.fetchHi &&
                i < (int)(sizeof(ts) / sizeof(ts[0])); ++i) {
        ts[i] = v.fetchLo + (int64_t)i * stepMs;
        ++n;
    }
    CHECK("Z10 模拟数据量 = 取数窗跨度/间隔 + 1（1020s / 5s + 1 = 205）",
          n == 205);

    /* ① 每一条都在取数窗内（服务端按 loTs/hiTs 过滤的直接结果）。 */
    {
        int bad = 0;
        for (i = 0; i < n; ++i)
            if (ts[i] < v.fetchLo || ts[i] > v.fetchHi) ++bad;
        CHECK("Z10 数据点时间戳全部落在取数窗内（无窗外点）", bad == 0);
    }
    /* ② 视口内的点换算成 X 值后全部落在轴域内（同基准、同参）。 */
    {
        int bad = 0;
        for (i = 0; i < n; ++i) {
            double x;
            if (ts[i] < v.viewLo || ts[i] > v.viewHi) continue;
            x = (double)(ts[i] - base) / 1000.0;
            if (x < v.xLo || x > v.xHi) ++bad;
        }
        CHECK("Z10 视口内的点换算 X 后全部落在轴域内（同基准同参）", bad == 0);
    }
    /* ③ 视口内点数 = 窗口跨度/间隔 + 1（1% 边距的行在视口外）。 */
    inView = HsfChartZoom_countInView(&v, zoomTestTsAt, ts, n);
    CHECK("Z10 视口内点数 = 1000s/5s + 1（只有 1% 边距在视口外）",
          inView == 201);
    CHECK("Z10 数据铺满视口两端（首末点跨视口 ≥98%）",
          HsfChartZoom_viewFilled(&v, zoomTestTsAt, ts, n));

    /* ④ 只覆盖视口左半段 ⇒ 不算铺满（用户看到的"曲线从中间断掉"）。 */
    {
        int half = 0;
        for (i = 0; i < n && ts[i] <= v.viewLo + (v.viewHi - v.viewLo) * 40 / 100;
             ++i)
            ts[half++] = ts[i];
        CHECK("Z10 只有左半段数据 ⇒ 不判铺满（状态行要提示数据不足）",
              !HsfChartZoom_viewFilled(&v, zoomTestTsAt, ts, half));
        CHECK("Z10 只有左半段数据 ⇒ 视口内点数明显少于全窗",
              HsfChartZoom_countInView(&v, zoomTestTsAt, ts, half) < inView);
    }
    /* ⑤ 空窗 / 单点：不铺满，点数为 0/1（不能算出负数或越界）。 */
    CHECK("Z10 空数据 ⇒ 点数为 0 且不判铺满",
          HsfChartZoom_countInView(&v, zoomTestTsAt, ts, 0) == 0 &&
          !HsfChartZoom_viewFilled(&v, zoomTestTsAt, ts, 0));
    {
        int64_t one = v.viewLo + 1000;
        CHECK("Z10 单点数据 ⇒ 点数为 1 且不判铺满",
              HsfChartZoom_countInView(&v, zoomTestTsAt, &one, 1) == 1 &&
              !HsfChartZoom_viewFilled(&v, zoomTestTsAt, &one, 1));
    }
}

/* ==================== §8 状态行按窗口重算 ==================== */

/** @brief 窗口状态行：写明起止 + 行数，且不复用全量区间文案。 */
static void testZ11_statusFollowsWindow(void)
{
    HsfChartZoomStatus st;
    char a[256];
    char b[256];
    char c[256];
    char d[256];

    st.rowsHeld = 412;
    st.rowsInView = 380;
    st.sampled = false;
    st.filled = true;
    (void)HsfChartZoom_statusText("2026-11-09 11:03:12",
                                  "2026-11-09 11:36:21", &st, a, (int)sizeof(a));
    CHECK("Z11 窗口口径：写明窗口起止时刻",
          strstr(a, "11:03:12") != NULL && strstr(a, "11:36:21") != NULL);
    CHECK("Z11 窗口口径：写明窗内行数与视口行数",
          strstr(a, "412") != NULL && strstr(a, "380") != NULL);
    CHECK("Z11 窗口口径：注明逐行取回（窗内全分辨率）",
          strstr(a, "逐行取回") != NULL);
    CHECK("Z11 窗口口径：不含全量区间文案（筛选共 N 行 / 全区间抽样）",
          strstr(a, "筛选共") == NULL && strstr(a, "全区间抽样") == NULL);

    /* 换窗口 ⇒ 文案必变（状态行必须随可视窗口走）。 */
    (void)HsfChartZoom_statusText("2026-11-09 12:00:00",
                                  "2026-11-09 12:20:00", &st, b, (int)sizeof(b));
    CHECK("Z11 换窗口 ⇒ 状态行文字随之变化（不停在上一个窗口）",
          strcmp(a, b) != 0 && strstr(b, "12:00:00") != NULL);

    /* 窗内抽样 / 数据不足：两条附加口径都要能表达。 */
    st.sampled = true;
    (void)HsfChartZoom_statusText("11:03:12", "11:36:21", &st, c,
                                  (int)sizeof(c));
    CHECK("Z11 窗内行数超上限 ⇒ 文案注明窗内抽样", strstr(c, "窗内抽样") != NULL);
    st.sampled = false;
    st.filled = false;
    (void)HsfChartZoom_statusText("11:03:12", "11:36:21", &st, d,
                                  (int)sizeof(d));
    CHECK("Z11 数据未铺满视口 ⇒ 文案提示数据不足",
          strstr(d, "数据不足") != NULL);
    CHECK("Z11 抽样与数据不足两种文案互不混用",
          strstr(c, "数据不足") == NULL && strstr(d, "窗内抽样") == NULL);

    /* 窗口内 0 行：窗口口径的"无记录"，不能说成"筛选结果为 0 行"。 */
    st.rowsHeld = 0;
    st.rowsInView = 0;
    st.filled = false;
    (void)HsfChartZoom_statusText("11:03:12", "11:36:21", &st, a,
                                  (int)sizeof(a));
    CHECK("Z11 空窗 ⇒ 注明该时间段无记录",
          strstr(a, "无记录") != NULL && strstr(a, "筛选") == NULL);
    CHECK("Z11 空窗也不丢窗口起止", strstr(a, "11:03:12") != NULL &&
                                        strstr(a, "11:36:21") != NULL);
    CHECK("Z11 时间文本缺省（NULL）⇒ 用 -- 占位，不崩",
          HsfChartZoom_statusText(NULL, NULL, &st, b, (int)sizeof(b)) > 0 &&
          strstr(b, "--") != NULL);
}

/* ==================== §5 泄漏自检 ==================== */

/** @brief 本文件跑完堆在用字节数必须回基线（缩放取数是高频路径）。 */
static void testZ8_noLeak(void)
{
    XMemoryStatistics before;
    XMemoryStatistics after;
    int i;

    if (!XMemory_statisticsEnabled()) {
        XPrintf("[SKIP] Z8 泄漏自检：全局内存统计未开启\n");
        return;
    }
    before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < 2000; ++i) {
        HsfChartZoomPlan plan;
        HsfChartZoomView view;
        HsfChartZoomStatus st;
        int64_t lo = 0;
        int64_t hi = 0;
        int64_t ts[8];
        char buf[256];
        int k;
        (void)HsfChartZoom_planBatches(9000 + i, 45, true, 12000, 500, 12000,
                                        &plan);
        (void)HsfChartZoom_visibleWindow(1794210635000LL, -3600.0 - i, 0.0,
                                         &lo, &hi);
        HsfChartZoom_expandWindow(lo, hi, &lo, &hi);
        (void)HsfChartZoom_isFullView(lo, hi, 0, 100000);
        (void)HsfChartZoom_windowCovers(lo, hi, lo, hi);
        /* §6–§8 的助手同样是高频路径（每次视口变化都跑）：一并进泄漏自检。 */
        if (HsfChartZoom_view(1794210635000LL, -3600.0 - i, 0.0, &view)) {
            for (k = 0; k < 8; ++k)
                ts[k] = view.viewLo + (int64_t)k * 1000;
            st.rowsHeld = 8 + i;
            st.rowsInView = HsfChartZoom_countInView(&view, zoomTestTsAt, ts, 8);
            st.sampled = (i & 1) != 0;
            st.filled = HsfChartZoom_viewFilled(&view, zoomTestTsAt, ts, 8);
            (void)HsfChartZoom_statusText("11:00:00", "12:00:00", &st, buf,
                                          (int)sizeof(buf));
        }
    }
    after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("Z8 缩放取数纯逻辑 2000 轮：堆在用字节数回基线（无泄漏）",
          after.systemBytes <= before.systemBytes + 4096);
}

int HsfChartZoomTest_run(void)
{
    /* [并入后不再自清计数] 原来这是独立入口，自己 g_pass/g_fail 清零并打一行
     * "专项结果"。并进本文件后它与 §A..§J 共用同一对计数器 ⇒ 清零会抹掉
     * 前面所有段落的成绩；结果行也由本文件末尾统一出。 */
    XPrintf("--- §H 历史页图表缩放取数（子断言 Z1..Z11）---\n");
    XPrintf("--- §1 缩放取数的批次计划 ---\n");
    testZ1_windowPlanIsRowByRow();
    testZ2_fullPlanIsSampled();
    XPrintf("--- §2 可见窗换算（数据与轴域同基准）---\n");
    testZ3_visibleWindowUsesSameBase();
    testZ4_expandWindow();
    XPrintf("--- §3 视口判定（全量视图 / 已取窗覆盖）---\n");
    testZ5_fullViewDetection();
    testZ6_windowCoverage();
    XPrintf("--- §4 取数 limit 轮转（应答归属不串味）---\n");
    testZ7_limitRotation();
    XPrintf("--- §6 轴域与取数窗同源（视图一次算齐）---\n");
    testZ9_viewIsSingleSource();
    XPrintf("--- §7 取回的点落在窗口内 / 铺满视口 ---\n");
    testZ10_pointsInsideWindow();
    XPrintf("--- §8 状态行按窗口重算 ---\n");
    testZ11_statusFollowsWindow();
    XPrintf("--- §5 泄漏自检 ---\n");
    testZ8_noLeak();
    return g_fail;
}

/* ============================================================================
 *        §M 读组「不应答」与「异常应答」的框架级回归锁（2026-11-10）
 *
 * [钉的是什么] 两条**总线级活性**契约：
 *
 *   M1「某组不应答 ⇒ 别的组照常轮询」——
 *      从站对某一组**不应答**（真机 M512 对 0xFA00×5 无应答；模拟器越界
 *      地址亦然）时，在途标记会一直挂着 ⇒ Hub 的单飞闸（`if (dev->inflight)
 *      return;`）把该设备后续**所有**读组永久堵死。现场表现：变频器整站
 *      离线、失败计数=0、游标卡在 1、其余组从不再发，而同一总线上的别的站
 *      一切正常。框架级契约：**一个组的沉默拖不垮整条总线**——超时即收口
 *      （判失败 + 记失败计数 + 报从站退避 + 释放在途标记）并继续下一组。
 *
 *   M2「异常应答是组级事实 ⇒ 不牵连同从站的其它组」——
 *      Modbus 异常帧（EXC02/EXC03…）= "这个地址不支持"，是**组级**事实。
 *      若按从站级失败回报退避表，连续 3 次后**该从站的所有组**一起被静默
 *      ⇒ 现场"一台设备整体离线"，而真机只是其中一组不支持。
 *
 * [现场根因（同一批修复）] 2026-11-10 现场"变频器整站哑掉"的直接根因是
 *      配置侧把 XModbusHubUnitLimits 里的 m_preTxQuietMs 留成**未初始化
 *      的栈垃圾**（Debug 构建 = 0xCCCCCCCC ≈ 34 亿毫秒）⇒ 静默闸要求
 *      34 亿毫秒总线静默 ⇒ 该从站的读组永远轮不到。M3 钉住框架层的活性
 *      保证（静默闸饥饿逃逸）：**任何从站都不会被静默闸永久饿死**。
 * ==========================================================================*/

/**
 * @brief 假从站客户端：站 m_silentUnit **受理请求但永不终结 Reply**
 *        （= 真机对某组不应答的同款场景：请求发出去了，从站就是不应）。
 *        其余站立即应答（同步终态）。
 */
XCLASS_DEFINE_BEGING(XMSilentSlaveClient)
XCLASS_DEFINE_EXTEND_END(XMSilentSlaveClient, XModbusClient)

typedef struct XMSilentSlaveClient {
    XModbusClient m_base;        /**< 基类（继承自 XModbusClient）。 */
    int m_silentUnit;            /**< 永不应答的站号。 */
    int m_reqSilent;             /**< 不应答站收到的请求数。 */
    int m_reqOther;              /**< 其它站收到的请求数（合计）。 */
    int m_reqByUnit[16];         /**< 各站请求数（按站号分桶；越界忽略）。 */
    int m_abandonCount;           /**< abandonReply 被调用次数（超时收口摘手计数）。 */
} XMSilentSlaveClient;

/** @brief 假从站 open：直接置「已连接」（无真实传输层）。 */
static bool XMSilentSlave_open(XModbusDevice* device)
{
    if (!device) return false;
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    return true;
}

/** @brief 假从站 close：置「未连接」。 */
static void XMSilentSlave_close(XModbusDevice* device)
{
    if (!device) return;
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

/**
 * @brief 假从站 sendRawRequest：按站号分流——哑站只收不回，活站立即应答。
 */
static XModbusReply* XMSilentSlave_sendRawRequest(XModbusClient* baseClient,
                                                   const XModbusRequest* request,
                                                   int serverAddress,
                                                   XFuncParamType type)
{
    XMSilentSlaveClient* c = (XMSilentSlaveClient*)baseClient;
    XModbusReply* reply = NULL;
    const XModbusPdu* pdu = (const XModbusPdu*)request;
    const uint8_t* d;
    size_t n;
    uint16_t addr = 0;
    uint16_t count = 0;
    XModbusRegisterType rtype = XModbusDiscreteInputs;
    if (!c || !request) return NULL;
    d = (const uint8_t*)XContainerDataAddr((const XContainer*)pdu->m_data);
    n = XByteArray_size_base(pdu->m_data);
    if (n >= 4) {
        addr = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
        count = (uint16_t)(((uint16_t)d[2] << 8) | d[3]);
    }
    switch (XModbusPdu_functionCodeRaw(pdu)) {
    case (uint8_t)XModbusPdu_ReadCoils:       rtype = XModbusCoils; break;
    case (uint8_t)XModbusPdu_ReadDiscreteInputs: rtype = XModbusDiscreteInputs; break;
    case (uint8_t)XModbusPdu_ReadInputRegisters: rtype = XModbusInputRegisters; break;
    case (uint8_t)XModbusPdu_ReadHoldingRegisters: rtype = XModbusHoldingRegisters; break;
    default: rtype = XModbusDiscreteInputs; break;
    }
    if (serverAddress == c->m_silentUnit) {
        ++c->m_reqSilent;
    } else {
        ++c->m_reqOther;
    }
    if (serverAddress >= 0 && serverAddress < 16) ++c->m_reqByUnit[serverAddress];
    if (type == XFuncParamType_Copy) {
        reply = XModbusClient_createReply(baseClient, request, serverAddress);
    } else if (type == XFuncParamType_Move) {
        reply = XModbusClient_createReply_move(baseClient,
                                               (XModbusRequest*)request,
                                               serverAddress);
    } else {
        reply = XModbusClient_createReply_ref(baseClient,
                                              (XModbusRequest*)request,
                                              serverAddress);
    }
    if (!reply) return NULL;
    if (serverAddress == c->m_silentUnit) {
        /* 哑站：受理了但**永不终结** ⇒ Hub 的在途标记会一直挂着
         * （这正是真机 M512 对 0xFA00×5 无应答的形态）。 */
        XModbusReply_setState(reply, XModbusReply_State_Waiting);
        return reply;
    }
    /* 活站：回一个确定的值（0x5A5A），同步终结。 */
    {
        XModbusDataUnit unit;
        XModbusDataUnit_init_ex(&unit, rtype, addr, count ? count : 1);
        for (uint16_t i = 0; i < (count ? count : 1); ++i)
            XModbusDataUnit_setValue(&unit, i, 0x5A5Au);
        XModbusReply_setResult(reply, &unit);
        XClassDeinit(&unit);
    }
    XModbusReply_setState(reply, XModbusReply_State_Finished);
    return reply;
}

/**
 * @brief 假从站 abandonReply：记录「传输层被摘手」的次数。
 * @details 真机 RTU/TCP 传输层在等待应答期间持有 Reply；Hub 超时收口
 *          释放 Reply 前**必须**先调本函数摘手，否则迟到应答/超时回调
 *          会解引用已释放对象（UAF ⇒ 板端实测 SEGV）。本假从站照此计数，
 *          M1 断言「收口确实摘了手」。
 */
static bool XMSilentSlave_abandonReply(XModbusClient* baseClient,
                                       XModbusReply* reply)
{
    XMSilentSlaveClient* c = (XMSilentSlaveClient*)baseClient;
    if (!c || !reply) return false;
    ++c->m_abandonCount;
    return true;
}

/** @brief 假从站类初始化（继承 XModbusClient，重载 open/close/sendRawRequest）。 */
static XVtable* XMSilentSlave_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMSilentSlaveClient)
        XVTABLE_INHERIT_XCLASS(XModbusClient);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, XMSilentSlave_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, XMSilentSlave_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest,
                             XMSilentSlave_sendRawRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_AbandonReply,
                             XMSilentSlave_abandonReply);
    XCLASS_SHOW_SIZE_DEFAULT(XMSilentSlaveClient);
    return XVTABLE_DEFAULT;
}

/** @brief 建假从站客户端（堆对象；所有权随 Hub 注入后归 Hub）。 */
static XMSilentSlaveClient* XMSilentSlave_create(int silentUnit)
{
    XMSilentSlaveClient* c =
        (XMSilentSlaveClient*)XMemory_malloc(sizeof(XMSilentSlaveClient),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    XModbusClient_init(&c->m_base);
    XClassGetVtable(c) = XMSilentSlave_class_init();
    c->m_silentUnit = silentUnit;
    Set_Class_Memory(c, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(c, true);
    return c;
}

/**
 * @brief M1：某组不应答 ⇒ 在途超时收口 ⇒ 别的组照常轮询。
 * @details 站 1 哑（受理但永不终结 Reply），站 2 正常。修前：站 1 的读组
 *          一发出去，在途标记永挂 ⇒ 单飞闸把**整台设备**的读组全堵死 ⇒
 *          站 2 一次都轮不到（现场：变频器整站离线、失败计数=0、游标卡住）。
 *          修后：超时收口（判失败 + 释放标记）⇒ 站 2 持续拿到数据。
 */
static void testM1_silentGroupDoesNotBlockBus(void)
{
    XMSilentSlaveClient* client = NULL;
    XModbusHub* hub = NULL;
    XModbusMapping* map = NULL;
    int devId = -1;
    int silentReq = 0;
    int otherReq = 0;
    uint32_t errCount = 0;

    XPrintf("--- M1 某组不应答：超时收口后其它组照常轮询 ---\n");
    client = XMSilentSlave_create(1);
    hub = XModbusHub_create();
    CHECK("M1 假从站 + Hub 装配", client && hub);
    if (!client || !hub) goto done;
    /* 站 1 哑、站 2 活；两块都在同一个设备（同一串口）上。 */
    devId = XModbusHub_addDevice_2(hub, (XModbusClient*)client, "哑站");
    CHECK("M1 设备入 Hub", devId >= 0);
    if (devId < 0) goto done;
    map = XModbusMapping_create();
    CHECK("M1 映射段登记（站 1 两点 + 站 2 两点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x200, 2,
                                    devId, 2, 0, 0, 0));
    XModbusHub_setMapping(hub, map);          /* 所有权移交 Hub */
    map = NULL;
    XModbusHub_setPollAreas(hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(hub, 20);
    XModbusHub_setPollInterval(hub, devId, 20);
    /* [收口时限] 显式配 80ms（测试确定性）；实际收口 = 80 + 宽限 200ms。 */
    XModbusHub_setInflightTimeoutMs(hub, 80);
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);

    spinMs(1500);
    silentReq = client->m_reqSilent;
    otherReq = client->m_reqOther;
    errCount = XModbusHub_pollErrCount(hub, devId);
    XPrintf("[INFO] M1 哑站请求=%d 活站请求=%d pollErr=%u "
            "inFlightTimeoutMs=%zu\n",
            silentReq, otherReq, (unsigned)errCount,
            XModbusHub_inflightTimeoutMs(hub));
    /* ① 哑站被反复重试（说明在途标记被收口了，而不是第一次就永挂）。 */
    CHECK("M1 哑站在途超时后被重新发起（收口生效）", silentReq >= 2,
          "silentReq=%d", silentReq);
    /* ② 活站照常轮询（核心：不能出现"一站哑掉、整条总线停摆"）。 */
    CHECK("M1 活站读组照常轮询（未被哑站拖死）", otherReq >= 2,
          "otherReq=%d", otherReq);
    /* ③ 超时按失败记账（可观测：失败计数不再是 0）。 */
    CHECK("M1 在途超时按失败记账（pollErr>0）", errCount > 0,
          "pollErr=%u", (unsigned)errCount);
    /* ④ 活站的数据真的回填了（点级新鲜度有效）。 */
    CHECK("M1 活站点级数据有效（回填成功）",
          XModbusHub_pointValid(hub, XModbusDiscreteInputs, 0x200, 3000));
    /* ⑤ [UAF 根修 2026-11-10 板端 SEGV] 超时收口必须**先让传输层摘手**
     *    （RTU=m_currentReply / TCP=在途表），否则 Hub 释放 Reply 后，
     *    迟到的应答/超时回调会解引用已释放对象 ⇒ 崩。
     *    假从站的 abandonReply 计数即「摘手」证据。 */
    CHECK("M1 超时收口先摘传输层的手（abandonReply 被调用）",
          client->m_abandonCount >= 1, "abandon=%d", client->m_abandonCount);
done:
    if (map) XClassDelete((XClass*)map);
    if (hub) XClassDelete((XClass*)hub);   /* 客户端所有权在 Hub */
    else if (client) XClassDelete((XClass*)client);
}

/**
 * @brief M2：异常应答是**组级**事实 ⇒ 同从站的其它组照常拿数据。
 * @details 同一从站（站 1）两块：块 A 读 17 点（超出虚拟模型单次上限
 *          16 ⇒ 恒回 EXC03 异常帧），块 B 读 2 点（正常）。
 *          修前：异常帧按从站级失败回报退避表 ⇒ 连续 3 次后站 1 被静默
 *          5s ⇒ **块 B 也一起停摆**（现场"一台设备整体离线"）。
 *          修后：异常帧仍计失败（可观测），但**不进从站退避** ⇒ 块 B 持续
 *          有数据。
 */
static void testM2_exceptionDoesNotMuteWholeSlave(void)
{
    HubFixture fx;
    XModbusMapping* map;
    uint32_t okCount = 0;
    uint32_t errCount = 0;
    bool blockBValid = false;

    XPrintf("--- M2 异常应答不牵连同从站的其它组 ---\n");
    CHECK("M2 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    /* 块 A：17 点（超模型上限 ⇒ 恒异常）；块 B：2 点（正常）。同一站 1。 */
    CHECK("M2 映射段登记（站 1：异常块 17 点 + 正常块 2 点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x200, 2,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    /* [退避阈值取 1] 让"异常帧是否进入从站退避"这件事**当场可见**：
     * 缺省阈值 3 在本用例里被正常组的成功反复复位（成功即 failCount=0），
     * 永远触发不了静默 ⇒ 锁不住。阈值 1 = 任何一次失败回报立刻静默，
     * 于是"异常帧进不进退避表"直接体现在正常组还能不能继续拿数据。 */
    XModbusHub_setUnitBackoff(fx.hub, 1, 5000);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(1500);
    okCount = XModbusHub_pollOkCount(fx.hub, fx.devId);
    errCount = XModbusHub_pollErrCount(fx.hub, fx.devId);
    blockBValid = XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x200,
                                         3000);
    XPrintf("[INFO] M2 pollOk=%u pollErr=%u 正常块新鲜=%d\n",
            (unsigned)okCount, (unsigned)errCount, blockBValid ? 1 : 0);
    /* ① 异常帧仍按失败记账（可观测，不许悄悄吞掉）。 */
    CHECK("M2 异常应答仍计失败（pollErr>0）", errCount > 0,
          "pollErr=%u", (unsigned)errCount);
    /* ② 正常组持续成功（未被异常组拖进退避）。 */
    CHECK("M2 正常组持续轮询成功（pollOk>5）", okCount > 5,
          "pollOk=%u", (unsigned)okCount);
    /* ③ 正常组数据保持新鲜（块级 lastOkMs 在推进）。 */
    CHECK("M2 正常组数据保持新鲜（点级有效性）", blockBValid);
    hubFixtureFree(&fx);
}

/**
 * @brief M3：静默闸饥饿逃逸 —— 配置离谱时也不能把从站永久饿死。
 * @details 给站 1 配一个**超大**的静默期（模拟未初始化栈垃圾 0xCCCCCCCC
 *          的等效效果），且同设备上另有活站在持续发帧（把静默窗口顶掉）
 *          ⇒ 站 1 的读组被静默闸连续挡住。框架层的活性保证：连续挡住超过
 *          XMODBUS_HUB_QUIET_ESCAPE_MS ⇒ 放行一帧 ⇒ 站 1 不会被永久饿死。
 */
static void testM3_quietGateStarvationEscape(void)
{
    XMSilentSlaveClient* client = NULL;
    XModbusHub* hub = NULL;
    XModbusMapping* map = NULL;
    int devId = -1;
    XModbusHubUnitLimits lim;
    int reqUnit2 = 0;
    int reqUnit1 = 0;
    int reqTotal = 0;

    XPrintf("--- M3 静默闸饥饿逃逸（配置离谱也不饿死从站）---\n");
    /* 哑站给一个**不存在的站号** 9 ⇒ 站 1/站 2 都正常应答，
     * 本用例只考察静默闸（否则会把在途超时收口混进来）。 */
    client = XMSilentSlave_create(9);
    hub = XModbusHub_create();
    CHECK("M3 假从站 + Hub 装配", client && hub);
    if (!client || !hub) goto done;
    devId = XModbusHub_addDevice_2(hub, (XModbusClient*)client, "静默闸");
    CHECK("M3 设备入 Hub", devId >= 0);
    if (devId < 0) goto done;
    map = XModbusMapping_create();
    CHECK("M3 映射段登记（站 1 两点 + 站 2 两点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x200, 2,
                                    devId, 2, 0, 0, 0));
    XModbusHub_setMapping(hub, map);
    map = NULL;
    /* 站 1 配**离谱**的静默期（未初始化栈垃圾的等效值）。 */
    memset(&lim, 0, sizeof(lim));
    lim.m_preTxQuietMs = 0xFFFFFFFFu;
    CHECK("M3 站 1 静默期登记（离谱值）",
          XModbusHub_setUnitLimits(hub, devId, 1, &lim));
    XModbusHub_setPollAreas(hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(hub, 20);
    XModbusHub_setPollInterval(hub, devId, 20);
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);
    /* 跑够逃逸时限（2s）+ 余量。 */
    spinMs(3500);
    reqTotal = client->m_reqOther + client->m_reqSilent;
    reqUnit2 = client->m_reqByUnit[2];
    reqUnit1 = client->m_reqByUnit[1];   /* 站 1（被静默闸挡的那些）。 */
    (void)reqTotal;
    XPrintf("[INFO] M3 站 1 请求数=%d 站 2 请求数=%d\n", reqUnit1, reqUnit2);
    /* ① 活站持续轮询（静默闸没把整条总线掐死）。 */
    CHECK("M3 活站持续轮询（静默闸没把整条总线掐死）", reqUnit2 >= 2,
          "reqUnit2=%d", reqUnit2);
    /* ② 被静默闸挡住的站 1 最终**也被放行**（饥饿逃逸 ⇒ 不被永久饿死）。 */
    CHECK("M3 被静默闸挡住的从站最终放行（饥饿逃逸）", reqUnit1 >= 1,
          "reqUnit1=%d", reqUnit1);
done:
    if (map) XClassDelete((XClass*)map);
    if (hub) XClassDelete((XClass*)hub);
    else if (client) XClassDelete((XClass*)client);
}

/* ============================================================================
 *        §L 长稳 soak：XLink 会话生命周期 40 轮 + 堆零增量（2026-11-10）
 *
 * [钉的是什么] 客户端反复连接/订阅/请求-应答/拆除的**长时间稳定性**：
 *   每轮 = 回环设备对 → 两个裸会话 → 匿名握手到 Ready → 客户端订阅用户
 *   主题 → 客户端发一条请求（服务端 echo）→ 收到应答 → 全部拆除。
 *   预热 2 轮后连跑 40 轮，堆在用字节数必须回基线（差 0）——长跑泄漏
 *   在这里暴露（§G 只覆盖本文件自己的工作集，§L 覆盖 XLink 会话栈）。
 * [为什么匿名] 裸会话没有内建口令应答器（口令派生在 XLinkClient 里），
 *   匿名握手同样覆盖 Hello/能力位/订阅/请求/拆除全路径；认证语义已由
 *   §J 单独锁定。
 * ==========================================================================*/
static int l_soakRoundsOk = 0;
static int l_soakReqOk = 0;
static int l_soakSubOk = 0;

/** @brief 服务端分发：任何请求都原样回 1 字节 echo（请求 id 原样带回）。 */
static void l_soak_srv_dispatch(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    uint8_t echo = 0x7E;
    if (payload && frame->m_payloadLen > 0)
        echo = payload[0];
    XLinkSession_sendResponse(self, frame->m_requestId, frame->m_msgCode,
                              &echo, 1);
}

/** @brief 客户端分发：统计 Response（按请求 id 认领）。 */
static uint32_t l_soak_lastReqId = 0;
static int l_soak_respSeen = 0;
static void l_soak_cli_dispatch(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    (void)self;
    (void)payload;
    if (frame && frame->m_msgClass == XLinkMsgClass_Response)
        l_soak_respSeen++;
}

/** @brief 一轮：建-握-订-请求-收应答-拆。全部成功返回 1。 */
static int l_soak_round(int round)
{
    XGuiRemoteLoopbackDevice* devA = NULL;
    XGuiRemoteLoopbackDevice* devB = NULL;
    XLinkSession* server = NULL;
    XLinkSession* client = NULL;
    uint32_t now = 1000u + (uint32_t)round * 7u;
    int i;
    int ok = 1;

    devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
    server = devA ? XLinkSession_create_ex((XIODevice*)devA) : NULL;
    client = devB ? XLinkSession_create_ex((XIODevice*)devB) : NULL;
    if (!devA || !devB || !server || !client) {
        ok = 0;
    } else {
        XLinkSession_setDispatchCallback(server, l_soak_srv_dispatch, NULL);
        XLinkSession_setDispatchCallback(client, l_soak_cli_dispatch, NULL);
        for (i = 0; i < 16 && ok; ++i) {
            now += 10;
            XLinkSession_pump(server, now);
            XLinkSession_pump(client, now);
            if (XLinkSession_isReady(server) && XLinkSession_isReady(client))
                break;
        }
        if (!(XLinkSession_isReady(server) && XLinkSession_isReady(client)))
            ok = 0;
        /* 订阅一个用户主题（服务端会收到订阅帧并记账） */
        if (ok && !XLinkSession_subscribe(client,
                                          XLINK_TOPIC_USER_BASE + 7u))
            ok = 0;
        now += 10;
        XLinkSession_pump(server, now);
        XLinkSession_pump(client, now);
        /* 请求-应答一条（echo 0x5A） */
        {
            uint8_t req = 0x5A;
            uint32_t reqId = 0;
            l_soak_respSeen = 0;
            if (!XLinkSession_sendRequest_ex(client, 0x7001,
                                             XLinkDataType_Raw,
                                             &req, 1, &reqId))
                ok = 0;
            l_soak_lastReqId = reqId;
            for (i = 0; i < 24 && ok; ++i) {
                now += 10;
                XLinkSession_pump(server, now);
                XLinkSession_pump(client, now);
                if (l_soak_respSeen)
                    break;
            }
            if (!l_soak_respSeen)
                ok = 0;
        }
    }
    if (server) XClassDelete((XClass*)server);
    if (client) XClassDelete((XClass*)client);
    if (devA) XClassDelete((XClass*)devA);
    if (devB) XClassDelete((XClass*)devB);
    return ok;
}

static void testL_soakRounds(void)
{
    XMemoryStatistics before;
    XMemoryStatistics after;
    int i;
    int rounds_ok = 0;

    XPrintf("--- §L 长稳 soak：XLink 会话生命周期 40 轮 ---\n");
    /* 预热 2 轮：摊平会话栈的惰性初始化（与 §G 同口径） */
    (void)l_soak_round(-1);
    (void)l_soak_round(-1);
    flushDeferred();
    before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < 40; ++i) {
        if (l_soak_round(i))
            ++rounds_ok;
        if ((i % 10) == 9)
            flushDeferred();     /* 周期性排延迟删除，避免积压假增 */
    }
    flushDeferred();
    after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("§L soak 40 轮全部成功（建-握-订-请求-收应答-拆）",
          rounds_ok == 40, "成功 %d/40", rounds_ok);
    CHECK("§L soak 40 轮堆增量回基线（长跑无泄漏）",
          after.systemBytes == before.systemBytes,
          "堆 %llu -> %llu（差 %lld）",
          (unsigned long long)before.systemBytes,
          (unsigned long long)after.systemBytes,
          (long long)(after.systemBytes - before.systemBytes));
}

/* ============================================================================
 *                                  入口
 * ==========================================================================*/

int HsfRegressionTest_run(void)
{
    uint64_t memBefore = 0;
    bool memChecked = false;

    g_pass = 0;
    g_fail = 0;
    XPrintf("===== HSF C 端总回归 =====\n");
    XPrintf("--- §A 框架 Modbus 四层根修 ---\n");
    testA1_exceptionIsFailure();
    testA2_nestedCloseoutAccounting();
    testA4_worstUnitAge();
    XPrintf("--- §B 客户端协议契约 ---\n");
    testB_historyParseContract();
    XPrintf("--- §C 客户端表格排序 ---" "\n");
    testC_tableSort();

    memChecked = XMemory_statisticsEnabled();
    if (memChecked) {
        /* [预热提到测量之前 2026-11-09] 框架有一批**一次性惰性初始化**
         * （首个表格/表头的字体与段宽缓存、信号槽表、BSON/日志管路等），
         * 实测只在第一次用到时分配、同一段跑第二遍增量为 0（复现：把
         * testD1 连跑两遍，第二遍堆差 = 0）。那不属泄漏；若把它量进"新增
         * 段落净增"，固定成本会被误读成每轮泄漏。故先在测量前摊平一轮，
         * 真正的泄漏判据由 §G 的 3 轮工作集增量（含读组失败日志路径）承担。 */
        leakWorkload();
        flushDeferred();
        memBefore = heapNow();
    }

    XPrintf("--- §D 框架层「Qt 对齐补全」回归锁 ---\n");
    testD1_sectionDoubleClickedEmits();
    testD2_sectionEnteredEmits();
    testD3_sortIndicatorFlipsOnRelease();
    testD5_sortingEnabledSortsByColumn();
    testD6_sortingDisabledStopsSorting();
    testD7_nullSignalIdContract();
    testD8_appThreeStatePolicy();
    XPrintf("--- §E 交叉测试 ---\n");
    testE1_frameworkPrimitiveVsAppPolicy();
    testE2_clientParserVsPythonPayload();
    testE3_cServerVsPyServerPayloadShape();
    testE4_frameworkVsAppLineConsistency();
    XPrintf("--- §F 覆盖面扩容 ---\n");
    testF1F2_failureObservabilityAndStallDiag();
    testF3_worstUnitAgeTwoSemantics();
    testF4_writeFailureDoesNotAmplifyPollTempo();
    /* §H：断言本体在 hsf_chart_zoom_test.c，这里只调用并汇总成败
     * （不复制断言文本；同一函数也就是 --test hsf-chart-zoom 的入口）。 */
    XPrintf("--- §H 历史页图表缩放取数（断言见上一段子结果）---\n");
    {
        int hFails = HsfChartZoomTest_run();
        if (hFails == 0) {
            ++g_pass;
            XPrintf("[PASS] §H 历史页图表缩放取数专项全部通过"
                    "（子断言计数见其自身结果行）\n");
        } else {
            g_fail += hFails;
            XPrintf("[FAIL] §H 历史页图表缩放取数专项失败 %d 项\n", hFails);
        }
    }
    XPrintf("--- §I 快照/控制命令的请求-应答兜底通道 ---\n");
    testI1_aiSnapshotResponse();
    testI2_flowMeterSnapshotResponse();
    testI3_turbiditySnapshotResponse();
    testI4_pumpSnapshotResponse();
#if XVFD_ON
    testI5_vfdSnapshotResponse();
#endif
    testI6_malformedPayloads();
    testI7_pumpControlResponse();
    testI8_autoControlResponse();
    testI9_ensureFreshOffline();
#if XLINK_AUTH_ON
    /* §J：认证「策略性拒绝」失败语义（"有时候客户端没数据"的框架层根修）。
     * 用回环设备对驱动两个会话，不依赖网络。 */
    XPrintf("--- §J 认证策略拒绝（Rejected）语义 ---\n");
    testJ_policyRejectSemantics();
#endif
#if XINDUSTRY_ON && XTURBIDITY_ON && XMODBUS_HUB_ON
    /* §K：ttyS4 路 B 三设备接入（2026-11-09 板端实测定案：站 1=CDAB、
     * 站 3(原 2)=DCBA+温度同刻、站 5 ABCD；站号 1/3/5 锁，模拟器保真读回）。
     * K4 自带预热 + 3 轮堆增量口径，排在 §G 之前互不干扰。 */
    testK_threeDeviceAccess();
#endif
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    /* §N：现场口径对齐（2026-11-10 板端实机复核，基准＝Python 服务端）：
     * N1 阀反馈 DI 位序（开在前）/ N2 侧号由服务端解析 /
     * N3 流量计只认系统报警 / N4 数字量模块名。 */
    testN_fieldAlignment();
#endif
#if XVFD_ON
    /* §O：变频器遥测刻度（0x1001 ÷100，板端实机复测定案）。 */
    XPrintf("--- §O 变频器遥测刻度 ---\n");
    testO1_m512FrequencyScale();
    /* §P：带给定启动必须是一个设备动作（否则被单飞闸自拒成「设备忙」）。 */
    XPrintf("--- §P 带给定启动＝一个动作 ---\n");
    testP1_startWithGivenIsOneAction();
#endif
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    /* §M：读组「不应答」与「异常应答」的框架级回归锁（2026-11-10）。
     * M1 某组不应答 ⇒ 在途超时收口 ⇒ 别的组照常轮询；
     * M2 异常应答（组级事实）⇒ 不牵连同从站的其它组；
     * M3 静默闸饥饿逃逸 ⇒ 配置离谱也不把从站永久饿死。 */
    XPrintf("--- §M 读组不应答 / 异常应答 / 静默闸逃逸 ---\n");
    testM1_silentGroupDoesNotBlockBus();
    testM2_exceptionDoesNotMuteWholeSlave();
    testM3_quietGateStarvationEscape();
#endif
    XPrintf("--- §G 泄漏自检 ---\n");
    XPrintf("--- §L 长稳 soak：XLink 会话生命周期（40 轮 + 堆零增量）---\n");
    testL_soakRounds();
    testG_leakSelfCheck();
    if (memChecked) {
        flushDeferred();
        /* 预热已把一次性惰性初始化摊到 memBefore 之前 ⇒ 这里应恒为 0。
         * 非 0 就是新出现的按次/按轮泄漏，按 §G 的 3 轮工作集增量定位。 */
        XPrintf("[INFO] 新增段落（§D/§E/§F/§H/§I）堆净增 = %lld 字节"
                "（预热后口径；真泄漏判据见 §G 3 轮工作集增量，含读组失败"
                "日志与日志捕获管路）\n",
                (long long)(heapNow() - memBefore));
    }

    XPrintf("===== 结果：PASS %d / FAIL %d =====\n", g_pass, g_fail);
    return g_fail;
}

void HsfRegressionTest(XVariant* data)
{
    (void)data;
    HsfRegressionTest_run();
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
