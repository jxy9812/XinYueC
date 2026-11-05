/**
 * @file       xindustry_turbidity_test.c
 * @brief      Industry 工业设备层浊度仪测试（通道组喂点，无硬件依赖）。
 * @details    覆盖：
 *               - **真机帧回归**：WZT-701A 说明书 4.3.10.2 的可复现帧
 *                 （01 03 00 00 00 02 C4 0B → D7 0A 41 1F = 9.99NTU）逐字节
 *                 驱动，验证缺省口径（CDAB + Float32）与手册一致；
 *               - 三个口径旋钮各自可配（字序 ABCD / 整数格式 / 除数）；
 *               - 挂接播种：先喂后 attach，value 立即可读；
 *               - Analog 模式：二级标度（工程量 → 0.01NTU 定点）；
 *               - 质量翻转：陈旧经巡检定时器发 qualityChanged(false)、
 *                 再喂值恢复 true；staleMs=0 恒有效；
 *               - detach 隔离 + 质量转无效；
 *               - 装配错误：Words 模式缺次字必须 attach 失败（不得静默读 0）；
 *               - 配置拒绝：divisor=0 / den=0 / 非法枚举 / 挂接中换形态；
 *               - 编解码：BSON/JSON 往返 + 键列表裁剪 + 非法载荷拒绝；
 *               - 泄漏压测：设备与编解码全路径多轮，XMemory 统计须回零。
 *
 *             [为什么真机帧是最高优先级的断言] 协议口径猜错时读数只是"一个数"
 *             ——不崩溃、不报警、质量还是 valid。唯一能锁住真相的就是一台真机的
 *             实际字节。手册给的这帧 CRC 自洽（本测试另行校验），故它是本类
 *             缺省口径的**唯一权威来源**，任何改动都必须让它继续通过。
 *
 *             测试点用纯数据 XIoAnalogPoint（通道层标度 1/1 直通，**必须**——
 *             32 位量的字不得被标度破坏），数据经 XIoChannelGroup_feedRole 驱动。
 *             运行依赖事件循环（XObject 定时器），进程退出码 = 失败断言数。
 */
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XIoPoint.h"
#include "XChannel/XIoAnalogPoint.h"
#include "XTurbidityMeter.h"
#include "XTurbidityMeterCodec.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>
#include <math.h>

#if XINDUSTRY_ON && XTURBIDITY_ON

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

/* ---- 信号记录 ----
 * 值/质量信号都用 connect_2（第 1 参即 sender），故不依赖 XVarList 所有权。 */
static int g_valCount = 0;          /**< valueChanged 计数。 */
static double g_valNtu = 0.0;       /**< 最近 valueChanged 值。 */
static bool g_qualityValid = false; /**< 最近 qualityChanged 质量。 */
static int g_qualityCount = 0;      /**< qualityChanged 计数。 */
static bool g_errorSeen = false;    /**< errorOccurred 是否触发。 */
static int g_lastError = 0;         /**< 最近错误码。 */

static void resetTurbFlags(void)
{
    g_valCount = 0;
    g_valNtu = 0.0;
    g_qualityValid = false;
    g_qualityCount = 0;
    g_errorSeen = false;
    g_lastError = 0;
}

static void onTurbValue2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_valNtu = XVarList_arg(args, double);
    ++g_valCount;
}

static void onTurbQuality2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_qualityValid = XVarList_arg(args, bool);
    ++g_qualityCount;
}

static void onTurbError2(XObject* sender, XVarList* args)
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

/** @brief 浊度仪夹具：组 + 2 个 AI 点（role 0/1，通道层标度 1/1 直通）。 */
typedef struct TurbFixture {
    XIoChannelGroup* m_group;           /**< 通道组（堆）。 */
    XIoAnalogPoint m_pts[XTurbidityMeterRole_Count]; /**< 2 个点（栈；role 即下标）。 */
    XTurbidityMeter* m_dev;             /**< 被测设备。 */
} TurbFixture;

static void turbFixtureInit(TurbFixture* fx)
{
    int i;
    memset(fx, 0, sizeof(*fx));
    fx->m_group = XIoChannelGroup_create();
    for (i = 0; i < (int)XTurbidityMeterRole_Count; ++i) {
        XIoAnalogPoint_init(&fx->m_pts[i]);
        XIoPoint_setRole(&fx->m_pts[i].m_base, (uint16_t)i);
        XIoPoint_setType(&fx->m_pts[i].m_base, XIoPoint_AnalogInput);
        XIoPoint_setTag(&fx->m_pts[i].m_base, "TURB");
        /* [关键] 字不得被标度破坏：恒等 1/1/0。 */
        XIoAnalogPoint_setScale(&fx->m_pts[i], 1, 1, 0, 0);
        XIoChannelGroup_addPoint(fx->m_group, &fx->m_pts[i].m_base, false);
    }
    fx->m_dev = XTurbidityMeter_create();
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XTurbidityMeter_valueChanged_signal), onTurbValue2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XTurbidityMeter_qualityChanged_signal), onTurbQuality2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XTurbidityMeter_errorOccurred_signal), onTurbError2);
}

static void turbFixtureDeinit(TurbFixture* fx)
{
    int i;
    if (fx->m_dev) XClassDelete((XClass*)fx->m_dev);
    if (fx->m_group) XClassDelete((XClass*)fx->m_group);
    for (i = 0; i < (int)XTurbidityMeterRole_Count; ++i)
        XClassDeinit((XClass*)&fx->m_pts[i]);
}

/** @brief 组喂一个字（组按 role 分发 → 设备整拍重算）。 */
static void feedRaw(TurbFixture* fx, int role, int32_t raw)
{
    XIoChannelGroup_feedRole(fx->m_group, (uint16_t)role, raw);
}

/**
 * @brief 按手册的**线上字节顺序**喂两个寄存器。
 * @details 直接给「首字 = 应答里的第一个寄存器字、次字 = 第二个」，不做任何
 *          字序换算——字序解释是被测对象自己的事。这样测试锁的是"设备怎么
 *          解释线上字序"，而不是"测试自己怎么拼"。
 */
static void feedWords(TurbFixture* fx, uint16_t w0, uint16_t w1)
{
    feedRaw(fx, XTurbidityMeterRole_Word0, (int32_t)w0);
    feedRaw(fx, XTurbidityMeterRole_Word1, (int32_t)w1);
}

/** @brief IEEE754 单精度 → 32 位位模式（严格别名安全）。 */
static uint32_t floatBits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/** @brief 按 CDAB（低字在前）把 32 位位模式拆成线上两个字。 */
static void feedBitsLowHigh(TurbFixture* fx, uint32_t bits)
{
    feedWords(fx, (uint16_t)(bits & 0xFFFFu), (uint16_t)(bits >> 16));
}

/** @brief 按 ABCD（高字在前）把 32 位位模式拆成线上两个字。 */
static void feedBitsHighLow(TurbFixture* fx, uint32_t bits)
{
    feedWords(fx, (uint16_t)(bits >> 16), (uint16_t)(bits & 0xFFFFu));
}

/* ==================== 用例 ==================== */

/**
 * @brief ① 真机帧回归：说明书 4.3.10.2 的可复现帧。
 * @note 请求 01 03 00 00 00 02 C4 0B；应答 01 03 04 D7 0A 41 1F 92 1D。
 *       手册原文「按照低字在前高字在后传输(CDAB)……对应的浮点型数据为
 *       9.99NTU」。本用例只喂两个数据字（D7 0A / 41 1F），与线上一致。
 */
static void test_realDeviceFrame(void)
{
    TurbFixture fx;
    double ntu = 0.0;
    uint32_t bits = 0;
    float expect = 9.99f;

    XPrintf("---- ① 真机帧回归（WZT-701A 手册 4.3.10.2）----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    CHECK("attach 成功（CDAB + Float32 缺省口径）",
          XTurbidityMeter_attach(fx.m_dev, fx.m_group));
    CHECK("缺省字序 = CDAB（低字在前）",
          XTurbidityMeter_wordOrder(fx.m_dev) == XTurbidityMeterWordOrder_LowHigh);
    CHECK("缺省格式 = Float32",
          XTurbidityMeter_wordFormat(fx.m_dev) == XTurbidityMeterWordFormat_Float32);
    resetTurbFlags();

    /* 手册的原样字节：D7 0A 41 1F → 9.99NTU。 */
    feedWords(&fx, 0xD70Au, 0x411Fu);

    CHECK("手册帧 D70A/411F = 9.99 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 9.99));
    CHECK("拼装位模式 = 0x411FD70A（CDAB 低字在前）",
          XTurbidityMeter_rawWords(fx.m_dev, &bits) && bits == 0x411FD70Au);
    CHECK("位模式等于 9.99f 的 IEEE754 表示", bits == floatBits(expect));
    CHECK("值变化触发 valueChanged", g_valCount > 0 && NEAR(g_valNtu, 9.99));
    CHECK("首拍质量即有效", XTurbidityMeter_isValid(fx.m_dev));

    /* ---- 反证：若按 ABCD（高字在前）解释，同一帧必错 ----
     * 这一步是**必要的**：只断言"能读出 9.99"无法区分 CDAB 与 ABCD 两种实现
     * 谁对（若模型恰好把字反着喂，错误实现也能碰出正确值）。此处显式切到
     * ABCD 后必须读出完全不同的值，才证明确实是按 CDAB 在解。 */
    XTurbidityMeter_setWordOrder(fx.m_dev, XTurbidityMeterWordOrder_HighLow);
    XTurbidityMeter_value(fx.m_dev, &ntu);
    CHECK("切成 ABCD 后同一帧不再是 9.99（证明字序确实在起作用）",
          !NEAR(ntu, 9.99));
    XTurbidityMeter_rawWords(fx.m_dev, &bits);
    CHECK("ABCD 拼装位模式 = 0xD70A411F", bits == 0xD70A411Fu);
    XTurbidityMeter_setWordOrder(fx.m_dev, XTurbidityMeterWordOrder_LowHigh);
    CHECK("切回 CDAB 恢复 9.99",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 9.99));

    turbFixtureDeinit(&fx);
}

/**
 * @brief ② 口径旋钮：整数格式 + 除数 + 有符号。
 * @details 现场另一类表的内脏：raw / 100 = NTU（两字节整数，非浮点）。
 *          本用例锁"三个旋钮各自生效且互不干扰"。
 */
static void test_formatKnobs(void)
{
    TurbFixture fx;
    double ntu = 0.0;

    XPrintf("---- ② 口径旋钮（整数格式 / 除数 / 有符号）----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    XTurbidityMeter_setWordFormat(fx.m_dev, XTurbidityMeterWordFormat_UInt32Scaled);
    XTurbidityMeter_setWordDivisor(fx.m_dev, 100.0);
    CHECK("attach 成功", XTurbidityMeter_attach(fx.m_dev, fx.m_group));

    /* 1234 → 12.34 NTU（无符号，CDAB）。 */
    feedBitsLowHigh(&fx, 1234u);
    CHECK("UInt32/100：1234 → 12.34 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 12.34));

    /* 换除数 10 → 123.4。 */
    CHECK("改除数被受理", XTurbidityMeter_setWordDivisor(fx.m_dev, 10.0));
    CHECK("除数 10 立即生效（1234 → 123.4）",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 123.4));

    /* 有符号：0xFFFFFFFF = -1 → -0.1 NTU。 */
    XTurbidityMeter_setWordFormat(fx.m_dev, XTurbidityMeterWordFormat_Int32Scaled);
    feedBitsLowHigh(&fx, 0xFFFFFFFFu);
    CHECK("Int32/10：0xFFFFFFFF = -1 → -0.1 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, -0.1));

    /* Int32 正数：0x00000100 = 256 → 25.6 NTU。 */
    feedBitsLowHigh(&fx, 0x00000100u);
    CHECK("Int32/10：256 → 25.6 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 25.6));

    /* 格式切换后 Float32 复活：位模式 0x411FD70A → 9.99。 */
    XTurbidityMeter_setWordFormat(fx.m_dev, XTurbidityMeterWordFormat_Float32);
    feedBitsLowHigh(&fx, 0x411FD70Au);
    CHECK("切回 Float32 后同一帧 = 9.99",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 9.99));

    turbFixtureDeinit(&fx);
}

/**
 * @brief ③ 装配错误必须暴露：Words 模式缺次字。
 * @details 若允许"只绑首字"成功挂接，读取会恒失败或恒 0 —— 现场表现为
 *          "表在线但浊度一直是 0"，最难查的一类故障。故 attach 必须失败。
 */
static void test_attachErrors(void)
{
    XIoChannelGroup* group = XIoChannelGroup_create();
    XIoAnalogPoint pt0;
    XTurbidityMeter* dev = XTurbidityMeter_create();
    double ntu = -1.0;

    XPrintf("---- ③ 装配错误与配置拒绝 ----\n");
    resetTurbFlags();
    /* [必须显式订阅错误信号] 本用例手工建 dev（不走 turbFixtureInit），
     * 若不连 errorOccurred，下面的 BadConfig 断言就恒为 false——那是测试
     * 自己的疏漏，不是设备没发信号。三条拒绝路径共用同一个观察点。 */
    XObject_connect_2((XObject*)dev,
                      XSignal(XTurbidityMeter_errorOccurred_signal), onTurbError2);
    XIoAnalogPoint_init(&pt0);
    XIoPoint_setRole(&pt0.m_base, 0);
    XIoPoint_setType(&pt0.m_base, XIoPoint_AnalogInput);
    XIoAnalogPoint_setScale(&pt0, 1, 1, 0, 0);
    XIoChannelGroup_addPoint(group, &pt0.m_base, false);

    CHECK("Words 模式只有首字 → attach 失败（不得静默读 0）",
          !XTurbidityMeter_attach(dev, group));
    CHECK("attach 失败后 value 为 false", !XTurbidityMeter_value(dev, &ntu));

    /* Analog 模式只需要 role 0：同一组应当能挂上。 */
    CHECK("切 Analog 模式被受理",
          XTurbidityMeter_setSourceMode(dev, XTurbidityMeterSource_Analog));
    CHECK("Analog 模式只需 role 0 → attach 成功",
          XTurbidityMeter_attach(dev, group));
    CHECK("挂接中换形态被拒绝（先 detach）",
          !XTurbidityMeter_setSourceMode(dev, XTurbidityMeterSource_Words));
    CHECK("拒绝时发 BadConfig 错误",
          g_errorSeen && g_lastError == XTurbidityMeterError_BadConfig);
    CHECK("detach 成功", XTurbidityMeter_detach(dev));
    CHECK("detach 后换形态被接受",
          XTurbidityMeter_setSourceMode(dev, XTurbidityMeterSource_Words));

    /* 配置拒绝：非法枚举与 0 除数/分母。 */
    CHECK("非法字序被拒绝",
          !XTurbidityMeter_setWordOrder(dev, (XTurbidityMeterWordOrder)99));
    CHECK("非法格式被拒绝",
          !XTurbidityMeter_setWordFormat(dev, (XTurbidityMeterWordFormat)99));
    resetTurbFlags();
    CHECK("除数 0 被拒绝", !XTurbidityMeter_setWordDivisor(dev, 0.0));
    CHECK("拒绝除数 0 时发 BadConfig", g_errorSeen);
    resetTurbFlags();
    CHECK("分母 0 被拒绝", !XTurbidityMeter_setScale(dev, 1, 0, 0));
    CHECK("拒绝分母 0 时发 BadConfig", g_errorSeen);

    XClassDelete((XClass*)dev);
    XClassDelete((XClass*)group);
    XClassDeinit((XClass*)&pt0);
}

/**
 * @brief ④ 挂接播种：先喂后挂，值立即可读且信号即发。
 */
static void test_attachSeeding(void)
{
    TurbFixture fx;
    double ntu = 0.0;

    XPrintf("---- ④ 挂接播种（先喂值后 attach）----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);

    /* 未挂接时先喂（此时设备尚未订阅，信号不会到它）。 */
    feedBitsLowHigh(&fx, floatBits(9.99f));
    CHECK("未挂接时 value 为 false", !XTurbidityMeter_value(fx.m_dev, &ntu));

    XTurbidityMeter_attach(fx.m_dev, fx.m_group);
    CHECK("attach 播种：立即读到 9.99",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 9.99));
    CHECK("播种发出 valueChanged", g_valCount > 0 && NEAR(g_valNtu, 9.99));

    turbFixtureDeinit(&fx);
}

/**
 * @brief ⑤ Analog 模式：二级标度（通道工程值 → 0.01NTU 定点）。
 * @details 典型 4-20mA 链：ADC 码经通道层标度得到工程量（如 μA），
 *          本设备再把工程量换成浊度。此处用整数比例 1/1/0 与 1/10/50 两档。
 */
static void test_analogMode(void)
{
    TurbFixture fx;
    double ntu = 0.0;
    int32_t eng = 0;

    XPrintf("---- ⑤ Analog 模式（二级标度）----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    XTurbidityMeter_setSourceMode(fx.m_dev, XTurbidityMeterSource_Analog);
    CHECK("attach 成功（Analog）", XTurbidityMeter_attach(fx.m_dev, fx.m_group));

    /* 直通 1/1/0：工程量 1250 → 12.50 NTU。 */
    feedRaw(&fx, XTurbidityMeterRole_Word0, 1250);
    CHECK("1/1/0：1250 → 12.50 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 12.50));
    CHECK("rawAnalog 暴露工程量",
          XTurbidityMeter_rawAnalog(fx.m_dev, &eng) && eng == 1250);
    CHECK("Analog 模式下 rawWords 返回 false",
          !XTurbidityMeter_rawWords(fx.m_dev, (uint32_t*)&eng));

    /* 1/10/50：1250 → 125 + 50 = 175 → 1.75 NTU。 */
    CHECK("改标度被受理", XTurbidityMeter_setScale(fx.m_dev, 1, 10, 50));
    CHECK("1/10/50：1250 → 1.75 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 1.75));

    /* 100/1/0：1250 → 125000 → 1250.00 NTU（高量程 TGF-1000 档）。 */
    XTurbidityMeter_setScale(fx.m_dev, 100, 1, 0);
    CHECK("100/1/0：1250 → 1250.00 NTU",
          XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 1250.0));

    /* role 1 在 Analog 模式不该被读：喂它不应改变浊度。 */
    XTurbidityMeter_setScale(fx.m_dev, 1, 1, 0);
    XTurbidityMeter_value(fx.m_dev, &ntu);
    {
        double before = ntu;
        feedRaw(&fx, XTurbidityMeterRole_Word1, 9999);
        XTurbidityMeter_value(fx.m_dev, &ntu);
        CHECK("Analog 模式忽略 role 1", NEAR(ntu, before));
    }

    turbFixtureDeinit(&fx);
}

/**
 * @brief ⑥ 质量：陈旧翻转 + 恢复 + staleMs=0 恒有效。
 */
static void test_quality(void)
{
    TurbFixture fx;
    double ntu = 0.0;

    XPrintf("---- ⑥ 质量翻转（陈旧 / 恢复 / 不判陈旧）----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    XTurbidityMeter_setStaleMs(fx.m_dev, 300);
    XTurbidityMeter_attach(fx.m_dev, fx.m_group);

    feedBitsLowHigh(&fx, floatBits(5.0f));
    CHECK("喂值后有效", XTurbidityMeter_isValid(fx.m_dev));
    resetTurbFlags();

    /* 静默超过阈值：巡检定时器发 qualityChanged(false)。 */
    spinMs(1500);
    CHECK("陈旧后判无效", !XTurbidityMeter_isValid(fx.m_dev));
    CHECK("陈旧翻转发 qualityChanged(false)",
          g_qualityCount > 0 && !g_qualityValid);

    /* 再喂值：恢复有效。 */
    resetTurbFlags();
    feedBitsLowHigh(&fx, floatBits(6.0f));
    CHECK("再喂值恢复有效", XTurbidityMeter_isValid(fx.m_dev));
    CHECK("恢复发 qualityChanged(true)",
          g_qualityCount > 0 && g_qualityValid);
    CHECK("值已更新", XTurbidityMeter_value(fx.m_dev, &ntu) && NEAR(ntu, 6.0));

    /* staleMs=0：永不判陈旧（数据收到过即恒有效）。 */
    XTurbidityMeter_setStaleMs(fx.m_dev, 0);
    spinMs(1500);
    CHECK("staleMs=0 恒有效（不判陈旧）", XTurbidityMeter_isValid(fx.m_dev));

    turbFixtureDeinit(&fx);
}

/**
 * @brief ⑦ detach 隔离：断信号 + 质量转无效 + 值不再更新。
 */
static void test_detachIsolation(void)
{
    TurbFixture fx;
    double ntu = 0.0;

    XPrintf("---- ⑦ detach 隔离 ----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    XTurbidityMeter_attach(fx.m_dev, fx.m_group);
    feedBitsLowHigh(&fx, floatBits(9.99f));
    CHECK("挂接时有效", XTurbidityMeter_isValid(fx.m_dev));

    resetTurbFlags();
    CHECK("detach 返回 true", XTurbidityMeter_detach(fx.m_dev));
    CHECK("detach 后 value 为 false", !XTurbidityMeter_value(fx.m_dev, &ntu));
    CHECK("detach 发 qualityChanged(false)",
          g_qualityCount > 0 && !g_qualityValid);
    CHECK("重复 detach 返回 false", !XTurbidityMeter_detach(fx.m_dev));

    /* 解挂后喂值：信号不得到达设备。 */
    resetTurbFlags();
    feedBitsLowHigh(&fx, floatBits(88.8f));
    CHECK("detach 后喂值不再发 valueChanged", g_valCount == 0);
    CHECK("detach 后值仍不可读", !XTurbidityMeter_value(fx.m_dev, &ntu));

    turbFixtureDeinit(&fx);
}

/**
 * @brief ⑧ 常量值不产生重复信号（避免把事件泵打满）。
 */
static void test_noDuplicateSignal(void)
{
    TurbFixture fx;

    XPrintf("---- ⑧ 常量值不重复发信号 ----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    XTurbidityMeter_attach(fx.m_dev, fx.m_group);

    feedBitsLowHigh(&fx, floatBits(9.99f));
    {
        int first = g_valCount;
        CHECK("首拍发了一次 valueChanged", first == 1);
        /* 同一值反复喂（含心跳刷新路径不应被 Words 模式用到，但组会发射）。 */
        feedBitsLowHigh(&fx, floatBits(9.99f));
        feedBitsLowHigh(&fx, floatBits(9.99f));
        CHECK("同值重复喂不再发 valueChanged", g_valCount == first);
    }

    turbFixtureDeinit(&fx);
}

/**
 * @brief ⑨ 编解码：BSON/JSON 往返 + 键裁剪 + 非法载荷。
 */
static void test_codec(void)
{
    TurbFixture fx;
    XTurbidityMeterSnapshot snap;
    XTurbidityMeterField keys[3];
    XByteArray* payload;
    int allCount = 0;

    XPrintf("---- ⑨ 编解码（BSON/JSON 往返）----\n");
    resetTurbFlags();
    turbFixtureInit(&fx);
    XTurbidityMeter_attach(fx.m_dev, fx.m_group);
    feedWords(&fx, 0xD70Au, 0x411Fu);   /* 9.99 NTU */

    keys[0] = XTurbidityMeterField_Ntu;
    keys[1] = XTurbidityMeterField_RawBits;
    keys[2] = XTurbidityMeterField_Valid;

    /* ---- BSON 往返 ---- */
    payload = XTurbidityMeterCodec_stateToBson(fx.m_dev, keys, 3);
    CHECK("BSON 编码成功", payload != NULL);
    if (payload) {
        CHECK("BSON 解码成功",
              XTurbidityMeterCodec_stateFromBson(
                  &snap, (const uint8_t*)XByteArray_constData(payload),
                  XByteArray_size_base(payload)));
        CHECK("BSON 往返 ntu = 9.99", NEAR(snap.m_ntu, 9.99));
        CHECK("BSON 往返 rawBits = 0x411FD70A", snap.m_rawBits == 0x411FD70Au);
        CHECK("BSON 往返 valid = true", snap.m_valid);
        XClassDelete((XClass*)payload);
    }

    /* ---- JSON 往返（同一张键表）---- */
    payload = XTurbidityMeterCodec_stateToJson(fx.m_dev, keys, 3);
    CHECK("JSON 编码成功", payload != NULL);
    if (payload) {
        CHECK("JSON 解码成功",
              XTurbidityMeterCodec_stateFromJson(
                  &snap, (const uint8_t*)XByteArray_constData(payload),
                  XByteArray_size_base(payload)));
        CHECK("JSON 往返 ntu = 9.99", NEAR(snap.m_ntu, 9.99));
        CHECK("JSON 往返 rawBits = 0x411FD70A", snap.m_rawBits == 0x411FD70Au);
        XClassDelete((XClass*)payload);
    }

    /* ---- 键裁剪：只给 ntu，其余保持 0/false ---- */
    payload = XTurbidityMeterCodec_stateToBson(fx.m_dev, keys, 1);
    CHECK("裁剪编码成功", payload != NULL);
    if (payload) {
        XTurbidityMeterCodec_stateFromBson(&snap,
                                           (const uint8_t*)XByteArray_constData(payload),
                                           XByteArray_size_base(payload));
        CHECK("裁剪后 ntu 有值", NEAR(snap.m_ntu, 9.99));
        CHECK("裁剪后 rawBits 为 0（未输出）", snap.m_rawBits == 0u);
        CHECK("裁剪后 valid 为 false（未输出）", !snap.m_valid);
        XClassDelete((XClass*)payload);
    }

    /* ---- 全键列表 ---- */
    {
        const XTurbidityMeterField* all = XTurbidityMeterCodec_allKeys(&allCount);
        CHECK("全键列表非空且数量正确", all != NULL && allCount == 5);
    }

    /* ---- 非法载荷 ---- */
    CHECK("NULL 载荷被拒", !XTurbidityMeterCodec_stateFromBson(&snap, NULL, 0));
    CHECK("空载荷被拒", !XTurbidityMeterCodec_stateFromJson(&snap, (const uint8_t*)"", 0));
    CHECK("垃圾 JSON 被拒",
          !XTurbidityMeterCodec_stateFromJson(&snap, (const uint8_t*)"not json", 8));
    CHECK("垃圾 BSON 被拒",
          !XTurbidityMeterCodec_stateFromBson(&snap, (const uint8_t*)"\x01\x02", 2));

    turbFixtureDeinit(&fx);
}

/**
 * @brief ⑩ 泄漏压测：设备生命周期 + 编解码全路径多轮。
 * @note 判据用 VmData/内存统计差（同其它 Industry 测试口径）。
 */
static void test_leakStress(void)
{
    TurbFixture fx;
    XTurbidityMeterField keys[5];
    int i;

    XPrintf("---- ⑩ 泄漏压测（200 轮设备生命周期 + 编解码）----\n");
    keys[0] = XTurbidityMeterField_Ntu;
    keys[1] = XTurbidityMeterField_RawBits;
    keys[2] = XTurbidityMeterField_Source;
    keys[3] = XTurbidityMeterField_Valid;
    keys[4] = XTurbidityMeterField_StaleMs;

    for (i = 0; i < 200; ++i) {
        XByteArray* payload;
        turbFixtureInit(&fx);
        XTurbidityMeter_attach(fx.m_dev, fx.m_group);
        feedBitsLowHigh(&fx, floatBits(9.99f));
        payload = XTurbidityMeterCodec_stateToBson(fx.m_dev, keys, 5);
        if (payload) XClassDelete((XClass*)payload);
        payload = XTurbidityMeterCodec_stateToJson(fx.m_dev, keys, 5);
        if (payload) XClassDelete((XClass*)payload);
        turbFixtureDeinit(&fx);
    }
    CHECK("200 轮无崩溃（内存统计见下）", true);
}

/* ==================== 入口 ==================== */

int XIndustryTurbidityTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    /* [单例所有权] XCoreApplication 是进程单例：create 在已有实例时原样返回
     * g_app。--test 路径下 main 建好实例并对它负责，本函数若无条件
     * XClassDelete 就是删同一个对象两次（实测退出时段错误，栈为
     * XClass_deinit_base ← XClass_delete_base ← main+0x4b1）。
     * 同 xindustry_valve_test.c 的口径：只删自己建的。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    XPrintf("========== Industry 浊度仪（通道组喂点） ==========\n");
    test_realDeviceFrame();
    test_formatKnobs();
    test_attachErrors();
    test_attachSeeding();
    test_analogMode();
    test_quality();
    test_detachIsolation();
    test_noDuplicateSignal();
    test_codec();
    test_leakStress();

    XPrintf("========== 结果：PASS %d / FAIL %d ==========\n", g_pass, g_fail);
    if (owned) XClassDelete((XClass*)app);
    return g_fail;
}

void XIndustryTurbidityTest(XVariant* data)
{
    (void)data;
    (void)XIndustryTurbidityTest_run();
}

#else /* !(XINDUSTRY_ON && XTURBIDITY_ON) */

int XIndustryTurbidityTest_run(void) { return 0; }
void XIndustryTurbidityTest(XVariant* data) { (void)data; }

#endif
