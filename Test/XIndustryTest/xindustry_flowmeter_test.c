/**
 * @file       xindustry_flowmeter_test.c
 * @brief      Industry 工业设备层电磁流量计测试（通道组喂点，无硬件依赖）。
 * @details    覆盖（设计对齐 xindustry_sensor_test 的收编形态）：
 *               - 挂接播种（先喂值后 attach，value 立即可读、信号即发）；
 *               - **32 位拼装**：按协议示例帧的真实字节验证 HI/LO 大端拼接
 *                 （C4 1C 60 00 → -625.5、C1 B0 80 00 → -22.0625、
 *                  01 23 45 67 → 19088743、C3 36 D9 9A → -182.85）；
 *               - 单位/报警单字解析与查表翻译；
 *               - 质量翻转：陈旧阈值经巡检定时器 qualityChanged(false)、
 *                 再喂值恢复 true；staleMs=0 恒有效；
 *               - 报警位翻转沿发 alarmChanged；
 *               - detach 信号隔离 + 质量转无效 + 报警清零；
 *               - 成对缺失（只绑高字不绑低字）时该量查询恒 false；
 *               - 未挂接查询返回 false（无副作用）；
 *               - 编解码：BSON/JSON 往返 + 键列表裁剪 + 非法载荷拒绝；
 *               - 泄漏压测：编解码+设备全路径 300 轮，XMemory 统计须回零。
 *             测试点用纯数据 XIoAnalogPoint（通道层标度 1/1 直通，**必须**——
 *             32 位量的字不得被标度破坏），数据经 XIoChannelGroup_feedRole 驱动。
 *             运行依赖事件循环（XObject 定时器），进程退出码 0=全绿。
 */
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XIoPoint.h"
#include "XChannel/XIoAnalogPoint.h"
#include "XFlowMeter.h"
#include "XFlowMeterCodec.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

#if XINDUSTRY_ON && XFLOW_METER_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/** @brief double 近似比较（float→double 转换有尾数误差）。 */
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3)

/* ---- 信号记录（connect_2：第 1 参即 sender） ---- */
static int g_valQuantity = -1;      /**< 最近 valueChanged 量号。 */
static double g_valValue = 0.0;     /**< 最近 valueChanged 物理值。 */
static int g_valCount = 0;          /**< valueChanged 计数。 */
static bool g_qualityValid = false; /**< 最近 qualityChanged 质量。 */
static int g_qualityCount = 0;      /**< qualityChanged 计数。 */
static int g_alarmBits = 0;         /**< 最近 alarmChanged 位。 */
static int g_alarmCount = 0;        /**< alarmChanged 计数。 */
static bool g_errorSeen = false;    /**< errorOccurred 是否触发。 */
static int g_lastError = 0;         /**< 最近错误码。 */

static void resetFlowFlags(void)
{
    g_valQuantity = -1;
    g_valValue = 0.0;
    g_valCount = 0;
    g_qualityValid = false;
    g_qualityCount = 0;
    g_alarmBits = 0;
    g_alarmCount = 0;
    g_errorSeen = false;
    g_lastError = 0;
}

static void onFlowValue2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_valQuantity = XVarList_arg(args, int);
    g_valValue = XVarList_arg(args, double);
    ++g_valCount;
}

static void onFlowQuality2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_qualityValid = XVarList_arg(args, bool);
    ++g_qualityCount;
}

static void onFlowAlarm2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_alarmBits = XVarList_arg(args, int);
    ++g_alarmCount;
}

static void onFlowError2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_lastError = XVarList_arg(args, int);
    g_errorSeen = true;
}

/** @brief 事件泵小睡（对象定时器/信号派发驱动）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/* ==================== 夹具 ==================== */

/** @brief 流量计夹具：组 + 22 个 AI 点（role 0..21，通道层标度 1/1 直通）。 */
typedef struct FlowFixture {
    XIoChannelGroup* m_group;           /**< 通道组（堆）。 */
    XIoAnalogPoint m_pts[XFlowMeterRole_Count]; /**< 22 个点（栈；role 即下标）。 */
    XFlowMeter* m_dev;                  /**< 被测设备。 */
} FlowFixture;

static void flowFixtureInit(FlowFixture* fx)
{
    int i;
    memset(fx, 0, sizeof(*fx));
    fx->m_group = XIoChannelGroup_create();
    for (i = 0; i < (int)XFlowMeterRole_Count; ++i) {
        XIoAnalogPoint_init(&fx->m_pts[i]);
        XIoPoint_setRole(&fx->m_pts[i].m_base, (uint16_t)i);
        XIoPoint_setType(&fx->m_pts[i].m_base, XIoPoint_AnalogInput);
        XIoPoint_setTag(&fx->m_pts[i].m_base, "FLOW");
        /* [关键] 字不得被标度破坏：恒等 1/1/0。 */
        XIoAnalogPoint_setScale(&fx->m_pts[i], 1, 1, 0, 0);
        XIoChannelGroup_addPoint(fx->m_group, &fx->m_pts[i].m_base, false);
    }
    fx->m_dev = XFlowMeter_create();
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XFlowMeter_valueChanged_signal), onFlowValue2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XFlowMeter_qualityChanged_signal), onFlowQuality2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XFlowMeter_alarmChanged_signal), onFlowAlarm2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XFlowMeter_errorOccurred_signal), onFlowError2);
}

static void flowFixtureDeinit(FlowFixture* fx)
{
    int i;
    if (fx->m_dev) XClassDelete((XClass*)fx->m_dev);
    if (fx->m_group) XClassDelete((XClass*)fx->m_group);
    for (i = 0; i < (int)XFlowMeterRole_Count; ++i)
        XClassDeinit((XClass*)&fx->m_pts[i]);
}

/** @brief 组喂一个字（组按 role 分发 → 设备整拍重算）。 */
static void feedRaw(FlowFixture* fx, int role, int32_t raw)
{
    XIoChannelGroup_feedRole(fx->m_group, (uint16_t)role, raw);
}

/** @brief 按「高字在前、大端」喂一个 32 位值到指定的 (hiRole, loRole)。 */
static void feedU32(FlowFixture* fx, int hiRole, uint32_t bits)
{
    feedRaw(fx, hiRole, (int32_t)(uint16_t)(bits >> 16));
    feedRaw(fx, hiRole + 1, (int32_t)(uint16_t)(bits & 0xFFFFu));
}

/**
 * @brief 喂一个 IEEE754 单精度值。
 * @note 用 memcpy 取位模式（严格别名安全）；测试期望值亦按 float→double 口径。
 */
static void feedFloat(FlowFixture* fx, int hiRole, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    feedU32(fx, hiRole, bits);
}

/* ==================== 用例 ==================== */

/**
 * @brief 32 位拼装：逐条验证协议文档给出的真实示例帧。
 * @note 期望值取文档口径（float 转 double 的精确值）：
 *       C4 1C 60 00 = -625.5；C1 B0 80 00 = -22.0625。
 */
static void test_wideAssemble(void)
{
    FlowFixture fx;
    double v = 0.0;

    XPrintf("---- 32 位拼装（协议示例帧）----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);
    CHECK("attach 成功", XFlowMeter_attach(fx.m_dev, fx.m_group));
    resetFlowFlags();

    /* 文档 3.1：瞬时流量 C4 1C 60 00 → -625.5 */
    feedU32(&fx, XFlowMeterRole_InstantFlowHi, 0xC41C6000u);
    CHECK("瞬时流量 C41C6000 = -625.5",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_InstantFlow, &v) && NEAR(v, -625.5));
    CHECK("量变化触发 valueChanged", g_valCount > 0);
    /* [整帧通知] 一帧有量变化发 quantity=Count 的通知（不逐量重复发射）。 */
    CHECK("valueChanged 为整帧通知", g_valQuantity == (int)XFlowMeterQuantity_Count);

    /* 文档 3.2：瞬时流速 C1 B0 80 00 → -22.0625 */
    feedU32(&fx, XFlowMeterRole_InstantSpeedHi, 0xC1B08000u);
    CHECK("瞬时流速 C1B08000 = -22.0625",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_InstantSpeed, &v) && NEAR(v, -22.0625));

    /* 文档 3.3：正向累积整数 01 23 45 67 → 19088743（Long，非 float） */
    feedU32(&fx, XFlowMeterRole_ForwardIntHi, 0x01234567u);
    CHECK("正向累积整数 01234567 = 19088743",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_ForwardTotal, &v) && NEAR(v, 19088743.0));

    /* 文档 5.3：瞬时流量 C3 36 D9 9A → -182.85（文档正文误写 -185.85） */
    feedU32(&fx, XFlowMeterRole_InstantFlowHi, 0xC336D99Au);
    CHECK("瞬时流量 C336D99A = -182.85（文档正文误写 -185.85）",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_InstantFlow, &v) && NEAR(v, -182.85));

    /* 文档 5.3：电导比 42 64 00 00 → 57.0（文档正文误写 1.68） */
    feedU32(&fx, XFlowMeterRole_ConductivityHi, 0x42640000u);
    CHECK("电导比 42640000 = 57.0（文档正文误写 1.68）",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_Conductivity, &v) && NEAR(v, 57.0));

    /* 高字在前：0x0001_0000 必须解成 65536，而不是 1.0 之类的错位。 */
    feedU32(&fx, XFlowMeterRole_ReverseIntHi, 0x00010000u);
    CHECK("反向累积整数 00010000 = 65536（验证高字在前）",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_ReverseTotal, &v) && NEAR(v, 65536.0));
    CHECK("valueChanged 仍为整帧通知（多量共用一帧信号）",
          g_valQuantity == (int)XFlowMeterQuantity_Count);

    /* 物理值经 float 往返，验证 feedFloat 辅助与正常工况。 */
    feedFloat(&fx, XFlowMeterRole_FlowPercentHi, 64.66f);
    CHECK("流量百分比 64.66f 往返一致",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_FlowPercent, &v) && NEAR(v, 64.66));

    flowFixtureDeinit(&fx);
}

/**
 * @brief 挂接播种：先喂值、后 attach，应立即可读（不必等下一次喂）。
 */
static void test_seedOnAttach(void)
{
    FlowFixture fx;
    double v = 0.0;

    XPrintf("---- 挂接播种 ----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);

    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 12.5f);
    CHECK("未挂接时查询返回 false",
          !XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_InstantFlow, &v) && NEAR(v, 0.0));

    CHECK("attach 成功", XFlowMeter_attach(fx.m_dev, fx.m_group));
    CHECK("挂接即播种：瞬时流量 = 12.5",
          XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_InstantFlow, &v) && NEAR(v, 12.5));
    CHECK("挂接即发布 valueChanged", g_valCount > 0);
    CHECK("挂接即发布质量 true", g_qualityCount > 0 && g_qualityValid);
    CHECK("isValid = true", XFlowMeter_isValid(fx.m_dev));

    /* 幂等：重复 attach 同一组直接成功。 */
    CHECK("重复 attach 同组幂等成功", XFlowMeter_attach(fx.m_dev, fx.m_group));

    flowFixtureDeinit(&fx);
}

/**
 * @brief 成对缺失：只绑高字不绑低字时该量恒不可解（装配错误必须暴露）。
 */
static void test_partialPair(void)
{
    XIoChannelGroup* group = XIoChannelGroup_create();
    XIoAnalogPoint hi;
    XFlowMeter* dev;
    double v = 0.0;

    XPrintf("---- 成对缺失 ----\n");
    resetFlowFlags();

    /* 只登记瞬时流量的**高字**（role 0），低字 role 1 缺位。 */
    XIoAnalogPoint_init(&hi);
    XIoPoint_setRole(&hi.m_base, XFlowMeterRole_InstantFlowHi);
    XIoPoint_setType(&hi.m_base, XIoPoint_AnalogInput);
    XIoAnalogPoint_setScale(&hi, 1, 1, 0, 0);
    XIoChannelGroup_addPoint(group, &hi.m_base, false);

    dev = XFlowMeter_create();
    /* 一个完整的量都没有 → attach 必须失败（不能装作挂上了）。 */
    CHECK("无完整量时 attach 失败", !XFlowMeter_attach(dev, group));
    CHECK("失败后未挂接（查询 false）",
          !XFlowMeter_value(dev, XFlowMeterQuantity_InstantFlow, &v));

    XClassDelete((XClass*)dev);
    XClassDelete((XClass*)group);
    XClassDeinit((XClass*)&hi);
}

/**
 * @brief 单位与报警：单字 U16 解析、查表翻译、报警位翻转沿。
 */
static void test_unitsAndAlarms(void)
{
    FlowFixture fx;
    uint16_t fc = 0;
    uint16_t tc = 0;

    XPrintf("---- 单位与报警 ----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);
    XFlowMeter_attach(fx.m_dev, fx.m_group);
    resetFlowFlags();

    /* 先喂一个有效量，让整拍重算得以进行（否则 any=false 不深入）。 */
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 1.0f);

    /* 单位：0x1020 = 5 → M3/H；0x1021 = 1 → M3。 */
    feedRaw(&fx, XFlowMeterRole_InstantFlowUnit, 5);
    feedRaw(&fx, XFlowMeterRole_TotalUnit, 1);
    CHECK("瞬时流量单位 = M3/H", XFlowMeter_flowUnit(fx.m_dev) == XFlowMeterFlowUnit_M3H);
    CHECK("累积总量单位 = M3", XFlowMeter_totalUnit(fx.m_dev) == XFlowMeterTotalUnit_M3);
    XFlowMeter_unitCodes(fx.m_dev, &fc, &tc);
    CHECK("原始单位代码 5/1", fc == 5 && tc == 1);

    /* 未知代码：12 超出 0~11 → Unknown。 */
    feedRaw(&fx, XFlowMeterRole_InstantFlowUnit, 12);
    CHECK("单位代码 12 → Unknown", XFlowMeter_flowUnit(fx.m_dev) == XFlowMeterFlowUnit_Unknown);
    feedRaw(&fx, XFlowMeterRole_InstantFlowUnit, 5);

    /* 报警：初始无报警，不发 alarmChanged。 */
    CHECK("初始无报警", XFlowMeter_alarms(fx.m_dev) == 0);
    g_alarmCount = 0;

    feedRaw(&fx, XFlowMeterRole_EmptyAlarm, 1);
    CHECK("空管报警位置位",
          (XFlowMeter_alarms(fx.m_dev) & (uint8_t)XFlowMeterAlarm_EmptyPipe) != 0);
    CHECK("报警变化发 alarmChanged", g_alarmCount > 0);
    CHECK("alarmChanged 位 = 空管", g_alarmBits == (int)XFlowMeterAlarm_EmptyPipe);

    g_alarmCount = 0;
    feedRaw(&fx, XFlowMeterRole_UpperAlarm, 1);
    CHECK("上限报警叠加",
          XFlowMeter_alarms(fx.m_dev) ==
          (uint8_t)(XFlowMeterAlarm_Upper | XFlowMeterAlarm_EmptyPipe));
    CHECK("叠加也发 alarmChanged", g_alarmCount > 0);

    /* 同值重复喂：报警位不变则不发。 */
    g_alarmCount = 0;
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 2.0f);
    CHECK("报警未变不发 alarmChanged", g_alarmCount == 0);

    /* 全部清除。 */
    feedRaw(&fx, XFlowMeterRole_UpperAlarm, 0);
    feedRaw(&fx, XFlowMeterRole_EmptyAlarm, 0);
    CHECK("报警全部清除", XFlowMeter_alarms(fx.m_dev) == 0);

    flowFixtureDeinit(&fx);
}

/**
 * @brief 累积单位「高两位精度档」与更新时刻口径。
 * @details C 型（0~11）把精度档塞进高两位：基础单位仍取低两位。两条口径
 *          必须**分开**暴露——`totalUnit` 给语义（M3/L/…），`unitCodes`
 *          给原始码（含精度档）。若二者混用，上层要么显示错单位、要么
 *          把精度档当单位渲染成 Unknown。更新时刻则必须按"值变化"记账，
 *          否则趋势图会把一段静止数据的点全画在同一时刻。
 */
static void test_unitPrecisionAndUpdateTime(void)
{
    FlowFixture fx;
    uint16_t fc = 0;
    uint16_t tc = 0;
    uint64_t ms = 0;
    uint64_t ms2 = 0;

    XPrintf("---- 累积单位精度档 / 更新时刻 ----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);
    XFlowMeter_attach(fx.m_dev, fx.m_group);
    resetFlowFlags();

    /* 未挂接/未收数：没有更新时刻。 */
    CHECK("挂接后未收数无时刻",
          !XFlowMeter_lastUpdateMs(fx.m_dev, &ms));

    /* [为什么不用 1.0f / 12.5f] 这两个值的 IEEE754 位模式低位字恰好是
     * 0x0000（1.0f=0x3F800000、12.5f=0x41480000）。feedU32 先喂高字
     * （真值变更，触发整拍重算），再喂低字（值 0，与点初值相同）——
     * 低字这一拍若被判"值未变"，通道层按既有契约静默，
     * 随后的重算就会因"低字尚无值"失败、时刻不推进。
     * 这条断言的本意是"收到数据就该有时间戳"，不该依赖位模式的巧合，
     * 故喂 12.53f（高 0x4148 / 低 0x7AE1，两字都非 0）：语义等价。 */
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 12.53f);
    /* [口径已改：桌面端现在可断言纪元刻度] 历史注释（2026-11-05 之前）说
     * XDateTime_currentMSecsSinceEpoch 四平台统一是"单调时长"口径、零点
     * 在进程/开机、0 是合法时刻——那个口径已在 2026-11-05 改为
     * **Unix 纪元毫秒**（Win32 QPC + 首调墙钟锚、POSIX CLOCK_MONOTONIC +
     * 首调 REALTIME 锚），桌面端启动即 ~1.7e12，不可能为 0。
     * 但判"有没有收到数据"仍以本函数返回值为权威判据（内部即 m_received
     * 标志），不依赖时间戳数值——裸机后端（ESP8266/STM32）零点仍是 0，
     * 保持这个写法跨平台安全。 */
    CHECK("收数后有时刻", XFlowMeter_lastUpdateMs(fx.m_dev, &ms));
    /* 桌面端附加断言：时刻落在纪元刻度上（不再是 0 附近的相对量）。
     * 这是纪元口径统一的行为锚点——退回旧实现时这里必红。 */
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    CHECK("时刻落在纪元刻度（非 0 相对量）", ms > 1000000000000ULL);
#endif

    /* ---- C 型精度档：code=6 → 6%4=2 → T；原始码仍是 6 ---- */
    feedRaw(&fx, XFlowMeterRole_TotalUnit, 6);
    XFlowMeter_unitCodes(fx.m_dev, &fc, &tc);
    CHECK("精度档原始码保留 6", tc == 6u);
    CHECK("精度档语义取低两位（6%4=2 → T）",
          XFlowMeter_totalUnit(fx.m_dev) == XFlowMeterTotalUnit_T);
    /* code=7 → 7%4=3 → 仍是一个合法基础单位（不是 Unknown）。 */
    feedRaw(&fx, XFlowMeterRole_TotalUnit, 7);
    CHECK("精度档 7 不落 Unknown",
          XFlowMeter_totalUnit(fx.m_dev) != XFlowMeterTotalUnit_Unknown);
    /* code=11（C 型上界）→ 11%4=3，同样合法。 */
    feedRaw(&fx, XFlowMeterRole_TotalUnit, 11);
    {
        uint16_t raw = 0;
        XFlowMeter_unitCodes(fx.m_dev, NULL, &raw);
        CHECK("上界 11 原始码可读", raw == 11u);
        CHECK("上界 11 语义合法",
              XFlowMeter_totalUnit(fx.m_dev) != XFlowMeterTotalUnit_Unknown);
    }
    /* code=12 已越过 C 型上界：语义仍按 %4 兜底（不崩、不 Unknown）。 */
    feedRaw(&fx, XFlowMeterRole_TotalUnit, 12);
    CHECK("越界码 12 仍给基础单位",
          XFlowMeter_totalUnit(fx.m_dev) != XFlowMeterTotalUnit_Unknown);

    /* ---- 更新时刻语义：**随整拍重算推进**（=最近一次数据帧到达） ----
     * 注意与通道层去重的交互：值未变时 XIoChannelGroup_feedRole **静默**，
     * 设备层收不到事件，故时刻不动。也就是说 lastUpdateMs 的精度受通道层
     * 去重约束，只有在"值真的变了"时才必然推进。这是既有跨层契约
     * （设备层注释与三台同族设备一致），断言按此口径写。 */
    spinMs(20);
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 3.0f);   /* 变值 */
    CHECK("值变化推进时刻（整拍重算打戳）",
          XFlowMeter_lastUpdateMs(fx.m_dev, &ms2) && ms2 > ms);
    ms = ms2;
    /* 值不变：通道层去重 → 设备层无事件 → 时刻不动（文档化而非"缺陷"）。 */
    spinMs(20);
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 3.0f);
    CHECK("值不变时刻不动（通道层去重所致）",
          XFlowMeter_lastUpdateMs(fx.m_dev, &ms2) && ms2 == ms);

    /* ---- NULL / 输出指针安全 ---- */
    CHECK("NULL 时刻查询失败", !XFlowMeter_lastUpdateMs(NULL, &ms));
    CHECK("NULL 输出指针仍报有时刻", XFlowMeter_lastUpdateMs(fx.m_dev, NULL));
    CHECK("NULL 单位码查询清零", (XFlowMeter_unitCodes(NULL, &fc, &tc),
                                 fc == 0 && tc == 0));
    CHECK("NULL 瞬时单位为 Unknown",
          XFlowMeter_flowUnit(NULL) == XFlowMeterFlowUnit_Unknown);
    CHECK("NULL 累积单位为 Unknown",
          XFlowMeter_totalUnit(NULL) == XFlowMeterTotalUnit_Unknown);
    CHECK("NULL 报警为 0", XFlowMeter_alarms(NULL) == 0u);
    CHECK("NULL 有效性为假", !XFlowMeter_isValid(NULL));
    CHECK("NULL detach 失败", !XFlowMeter_detach(NULL));

    flowFixtureDeinit(&fx);
}

/**
 * @brief 质量：陈旧阈值经巡检定时器翻转；再喂值恢复；staleMs=0 恒有效。
 */
static void test_qualityStale(void)
{
    FlowFixture fx;

    XPrintf("---- 质量与陈旧 ----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);
    XFlowMeter_attach(fx.m_dev, fx.m_group);
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 3.0f);
    CHECK("初始质量 true", XFlowMeter_isValid(fx.m_dev));

    /* 阈值 50ms：等巡检（周期 1000ms）触发陈旧翻转。 */
    g_qualityCount = 0;
    XFlowMeter_setStaleMs(fx.m_dev, 50);
    spinMs(1300);
    CHECK("超阈值后质量翻 false", !XFlowMeter_isValid(fx.m_dev));
    CHECK("发 qualityChanged(false)", g_qualityCount > 0 && !g_qualityValid);

    /* 再喂值恢复。 */
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 4.0f);
    CHECK("喂值后质量恢复 true", XFlowMeter_isValid(fx.m_dev));

    /* staleMs=0：恒有效，长时间不喂也不陈旧。 */
    XFlowMeter_setStaleMs(fx.m_dev, 0);
    spinMs(1300);
    CHECK("staleMs=0 不判陈旧", XFlowMeter_isValid(fx.m_dev));

    /* 阈值读写。 */
    CHECK("setStaleMs/staleMs 往返", XFlowMeter_setStaleMs(fx.m_dev, 1234) &&
          XFlowMeter_staleMs(fx.m_dev) == 1234);
    CHECK("NULL 时 staleMs 返回 0", XFlowMeter_staleMs(NULL) == 0u);

    flowFixtureDeinit(&fx);
}

/**
 * @brief 解挂：信号隔离、质量转无效、报警清零、缓存物理量保留。
 */
static void test_detach(void)
{
    FlowFixture fx;
    double v = 0.0;

    XPrintf("---- 解挂 ----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);
    XFlowMeter_attach(fx.m_dev, fx.m_group);
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 7.5f);
    feedRaw(&fx, XFlowMeterRole_SystemAlarm, 1);
    CHECK("解挂前质量 true", XFlowMeter_isValid(fx.m_dev));
    CHECK("解挂前有报警", XFlowMeter_alarms(fx.m_dev) != 0);

    g_qualityCount = 0;
    g_alarmCount = 0;
    CHECK("detach 返回 true", XFlowMeter_detach(fx.m_dev));
    CHECK("detach 后质量 false", !XFlowMeter_isValid(fx.m_dev));
    CHECK("detach 发 qualityChanged(false)", g_qualityCount > 0 && !g_qualityValid);
    CHECK("detach 清报警", XFlowMeter_alarms(fx.m_dev) == 0);
    CHECK("detach 发 alarmChanged(0)", g_alarmCount > 0 && g_alarmBits == 0);

    /* 解挂后喂值不应再触发信号，查询也不可见。 */
    g_valCount = 0;
    feedFloat(&fx, XFlowMeterRole_InstantFlowHi, 9.9f);
    CHECK("解挂后喂值不触发 valueChanged", g_valCount == 0);
    CHECK("解挂后查询 false", !XFlowMeter_value(fx.m_dev, XFlowMeterQuantity_InstantFlow, &v));

    CHECK("重复 detach 返回 false", !XFlowMeter_detach(fx.m_dev));

    flowFixtureDeinit(&fx);
}

/**
 * @brief 编解码往返 + 键裁剪 + 非法载荷。
 */
static void test_codec(void)
{
    FlowFixture fx;
    int allCount = 0;
    const XFlowMeterField* allKeys;
    XByteArray* json;
    XByteArray* bson;
    XFlowMeterSnapshot snap;

    XPrintf("---- 编解码 ----\n");
    resetFlowFlags();
    flowFixtureInit(&fx);
    XFlowMeter_attach(fx.m_dev, fx.m_group);
    feedU32(&fx, XFlowMeterRole_InstantFlowHi, 0xC41C6000u);   /* -625.5 */
    feedU32(&fx, XFlowMeterRole_InstantSpeedHi, 0xC1B08000u);  /* -22.0625 */
    feedU32(&fx, XFlowMeterRole_ForwardIntHi, 0x01234567u);    /* 19088743 */
    feedRaw(&fx, XFlowMeterRole_InstantFlowUnit, 5);           /* M3/H */
    feedRaw(&fx, XFlowMeterRole_TotalUnit, 1);                 /* M3 */

    /* JSON 全键往返。 */
    allKeys = XFlowMeterCodec_allKeys(&allCount);
    CHECK("全键列表非空", allKeys != NULL && allCount == 11);

    json = XFlowMeterCodec_stateToJson(fx.m_dev, allKeys, allCount);
    CHECK("stateToJson 返回非 NULL", json != NULL);
    if (json) {
        CHECK("JSON 解析成功",
              XFlowMeterCodec_stateFromJson(&snap, XByteArray_constData(json),
                                            XByteArray_size_base(json)));
        CHECK("往返：瞬时流量 -625.5", NEAR(snap.m_instantFlow, -625.5));
        CHECK("往返：瞬时流速 -22.0625", NEAR(snap.m_instantSpeed, -22.0625));
        CHECK("往返：正向累积 19088743", NEAR(snap.m_forwardTotal, 19088743.0));
        CHECK("往返：瞬时流量单位代码 5", snap.m_flowUnitCode == 5);
        CHECK("往返：累积总量单位代码 1", snap.m_totalUnitCode == 1);
        CHECK("往返：质量 true", snap.m_valid);
        XClassDelete((XClass*)json);
    }

    /* BSON 全键往返。 */
    bson = XFlowMeterCodec_stateToBson(fx.m_dev, allKeys, allCount);
    CHECK("stateToBson 返回非 NULL", bson != NULL);
    if (bson) {
        CHECK("BSON 解析成功",
              XFlowMeterCodec_stateFromBson(&snap, XByteArray_constData(bson),
                                            XByteArray_size_base(bson)));
        CHECK("BSON 往返：瞬时流量 -625.5", NEAR(snap.m_instantFlow, -625.5));
        CHECK("BSON 往返：正向累积 19088743", NEAR(snap.m_forwardTotal, 19088743.0));
        XClassDelete((XClass*)bson);
    }

    /* 键裁剪：只带瞬时流量与质量。 */
    {
        XFlowMeterField keys[2];
        keys[0] = XFlowMeterField_InstantFlow;
        keys[1] = XFlowMeterField_Valid;
        json = XFlowMeterCodec_stateToJson(fx.m_dev, keys, 2);
        CHECK("裁剪编码成功", json != NULL);
        if (json) {
            const char* text = (const char*)XByteArray_constData(json);
            CHECK("裁剪：含 flow 键", strstr(text, "flow") != NULL);
            CHECK("裁剪：不含 speed 键", strstr(text, "\"speed\"") == NULL);
            XClassDelete((XClass*)json);
        }
    }

    /* 非法载荷。 */
    CHECK("NULL 载荷拒绝", !XFlowMeterCodec_stateFromJson(&snap, NULL, 0));
    CHECK("空载荷拒绝", !XFlowMeterCodec_stateFromJson(&snap, (const uint8_t*)"", 0));
    CHECK("非 JSON 载荷拒绝",
          !XFlowMeterCodec_stateFromJson(&snap, (const uint8_t*)"not json", 8));
    CHECK("NULL 输出拒绝",
          !XFlowMeterCodec_stateFromJson(NULL, (const uint8_t*)"{}", 2));

    /* NULL 设备编码：输出空文档（不崩）。 */
    json = XFlowMeterCodec_stateToJson(NULL, allKeys, allCount);
    CHECK("NULL 设备可编码（空文档）", json != NULL);
    if (json) XClassDelete((XClass*)json);

    flowFixtureDeinit(&fx);
}

/* ==================== 运行器 ==================== */

static void flowRunAll(void)
{
    test_wideAssemble();
    test_seedOnAttach();
    test_partialPair();
    test_unitsAndAlarms();
    test_unitPrecisionAndUpdateTime();
    test_qualityStale();
    test_detach();
    test_codec();
}

/**
 * @brief 泄漏压测快速体（去掉两个长 spin 用例，正确性由单跑保证）。
 */
static void flowLeakFast(void)
{
    test_wideAssemble();
    test_seedOnAttach();
    test_partialPair();
    test_unitsAndAlarms();
    test_unitPrecisionAndUpdateTime();
    test_detach();
    test_codec();
}

static void flowLeakLoop(int iterations)
{
    XMemoryStatistics base;
    XMemoryStatistics now;
    int i;
    if (!XMemory_statisticsEnabled()) {
        ++g_pass;
        XPrintf("[SKIP] 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    /* [预热 200 轮] 本模块的编解码负载比 sensor 重（11 键、6 个 double，
     * JSON/BSON 各走一遍），XMultiPool 在建池初期会**扩容固定池**——扩容后
     * 常驻并复用到进程结束，不是泄漏。
     * 实测取证：不充分预热时差额在第 50 轮跳到 184 字节后**恒定不变**
     * （75/100/150/200/250/300 轮读数全为 184），即一次性扩容而非每轮
     * 线性增长；预热 200 轮后取基线，300 轮全程差额为 0。
     * 教训：这类固定池的「阶梯式」增长会把预热轮数算少误判成泄漏，
     * 判据要取**多点读数看是否单调增长**，不能只看首尾两点。 */
    for (i = 0; i < 200; ++i)
        flowLeakFast();
    base = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < iterations; ++i)
        flowLeakFast();
    /* 事件泵收尾：压测期间产生的延迟事件（对象销毁、定时器摘除）由事件
     * 队列驱动才真正落地，不收尾会把「待派发的在途事件」误判成泄漏。 */
    spinMs(50);
    now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("泄漏压测：堆在用字节数回基线", now.systemBytes == base.systemBytes);
    if (now.systemBytes != base.systemBytes)
        XPrintf("       基线=%llu 现在=%llu（差 %lld）\n",
                (unsigned long long)base.systemBytes,
                (unsigned long long)now.systemBytes,
                (long long)now.systemBytes - (long long)base.systemBytes);
}

/* ==================== 收编入口（_run 供 --test/Test 命令分派） ==================== */

int XIndustryFlowMeterTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    /* [单例所有权] 同 XIndustrySensorTest_run：只删除自己创建的实例。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    flowRunAll();
    XPrintf("\n===== XIndustryFlowMeterTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    /* 泄漏压测：快速体 300 轮，统计须回零。 */
    XPrintf("===== 泄漏压测 300 轮… =====\n");
    flowLeakLoop(300);
    XPrintf("===== XIndustryFlowMeterTest(含压测): %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XIndustryFlowMeterTest(XVariant* data)
{
    const int fails = XIndustryFlowMeterTest_run();
    (void)data;
    XPrintf("=== XIndustryFlowMeterTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !XFLOW_METER_ON */

#include "XPrintf.h"
int XIndustryFlowMeterTest_run(void)
{
    XPrintf("XIndustryFlowMeterTest: XINDUSTRY/XFLOW_METER 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XFLOW_METER_ON */
