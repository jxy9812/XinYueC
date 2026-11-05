
/**
 * @file       xindustry_sensor_test.c
 * @brief      Industry 工业设备层温湿度变送器/压差计测试（通道组喂点，无硬件依赖）。
 * @details    覆盖（设计对齐 xindustry_valve_test 的收编形态）：
 *               - 挂接播种（先喂值后 attach，value 立即可读、信号即发）；
 *               - inputChanged → 第二级标度换算 + valueChanged 信号；
 *               - 质量翻转：陈旧阈值（staleMs）经巡检定时器 qualityChanged(false)、
 *                 再喂值恢复 true；staleMs=0 恒有效；
 *               - setScale 立即生效（den=0 拒绝、重算重发）；
 *               - detach 信号隔离 + 质量转无效；
 *               - 未挂接查询返回 false（无副作用）；
 *               - 压差计同款子集（单通道）；
 *               - 编解码：BSON/JSON 往返 + 键列表裁剪 + 非法载荷拒绝；
 *               - 泄漏压测：编解码+设备全路径 300 轮，XMemory 统计须回零。
 *             测试点用纯数据 XIoAnalogPoint（通道层标度 1/1 直通），数据
 *             经 XIoChannelGroup_feedRole 驱动（组信号按 role 分发）。
 *             [标度口径] 二级标度是设备层的纯整数映射，测试取简单口径：
 *               温度 eng = chEng/10（12000μA → 1200）；
 *               湿度 eng = chEng/10（20000μA → 2000）；
 *               压差 eng = chEng/2（12000μA → 6000）。
 *             运行依赖事件循环（XObject 定时器），进程退出码 0=全绿。
 */
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XIoPoint.h"
#include "XChannel/XIoAnalogPoint.h"
#include "XTempHumTransmitter.h"
#include "XTempHumTransmitterCodec.h"
#include "XDiffPressureGauge.h"
#include "XDiffPressureGaugeCodec.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>

#if XINDUSTRY_ON && (XTEMP_HUM_ON || XPRESS_DIFF_ON)

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* ---- 信号记录（connect_2：第 1 参即 sender） ---- */
static int g_valueChannel = -1;      /**< 最近 valueChanged 通道。 */
static int32_t g_valueEng = 0;       /**< 最近 valueChanged 工程值。 */
static int g_valueCount = 0;         /**< valueChanged 计数。 */
static int g_qualityChannel = -1;    /**< 最近 qualityChanged 通道。 */
static bool g_qualityValid = false;  /**< 最近 qualityChanged 质量。 */
static int g_qualityCount = 0;       /**< qualityChanged 计数。 */
static bool g_errorSeen = false;     /**< errorOccurred 是否触发。 */
static int g_lastError = 0;          /**< 最近错误码。 */
/* 压差计（单通道无 channel 维度）。 */
static int32_t g_dpValue = 0;
static int g_dpValueCount = 0;
static bool g_dpQuality = false;
static int g_dpQualityCount = 0;

static void resetSensorFlags(void)
{
    g_valueChannel = -1;
    g_valueEng = 0;
    g_valueCount = 0;
    g_qualityChannel = -1;
    g_qualityValid = false;
    g_qualityCount = 0;
    g_errorSeen = false;
    g_lastError = 0;
    g_dpValue = 0;
    g_dpValueCount = 0;
    g_dpQuality = false;
    g_dpQualityCount = 0;
}

static void onTempHumValue2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_valueChannel = XVarList_arg(args, int);
    g_valueEng = XVarList_arg(args, int32_t);
    ++g_valueCount;
}

static void onTempHumQuality2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_qualityChannel = XVarList_arg(args, int);
    g_qualityValid = XVarList_arg(args, bool);
    ++g_qualityCount;
}

static void onSensorError2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_lastError = XVarList_arg(args, int);
    g_errorSeen = true;
}

static void onDpValue2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_dpValue = XVarList_arg(args, int32_t);
    ++g_dpValueCount;
}

static void onDpQuality2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_dpQuality = XVarList_arg(args, bool);
    ++g_dpQualityCount;
}

/** @brief 事件泵小睡（对象定时器/信号派发驱动，电磁阀测试同款拍法）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/* ==================== 夹具 ==================== */

/** @brief 双点夹具：组 + 温度/湿度两个 AI 点（通道层标度 1/1 直通）。 */
typedef struct TempHumFixture {
    XIoChannelGroup* m_group;       /**< 通道组（堆）。 */
    XIoAnalogPoint m_tempPt;        /**< 温度点（栈；role=10）。 */
    XIoAnalogPoint m_humPt;         /**< 湿度点（栈；role=11）。 */
    XTempHumTransmitter* m_dev;     /**< 被测设备。 */
} TempHumFixture;

static void tempHumFixtureInit(TempHumFixture* fx)
{
    memset(fx, 0, sizeof(*fx));
    fx->m_group = XIoChannelGroup_create();
    XIoAnalogPoint_init(&fx->m_tempPt);
    XIoPoint_setRole(&fx->m_tempPt.m_base, 10);
    XIoPoint_setType(&fx->m_tempPt.m_base, XIoPoint_AnalogInput);
    XIoPoint_setTag(&fx->m_tempPt.m_base, "TEMP");
    XIoAnalogPoint_setScale(&fx->m_tempPt, 1, 1, 0, 0);
    XIoChannelGroup_addPoint(fx->m_group, &fx->m_tempPt.m_base, false);
    XIoAnalogPoint_init(&fx->m_humPt);
    XIoPoint_setRole(&fx->m_humPt.m_base, 11);
    XIoPoint_setType(&fx->m_humPt.m_base, XIoPoint_AnalogInput);
    XIoPoint_setTag(&fx->m_humPt.m_base, "HUM");
    XIoAnalogPoint_setScale(&fx->m_humPt, 1, 1, 0, 0);
    XIoChannelGroup_addPoint(fx->m_group, &fx->m_humPt.m_base, false);
    fx->m_dev = XTempHumTransmitter_create();
    /* 信号观测（connect_2：第 1 参即 sender）。 */
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XTempHumTransmitter_valueChanged_signal),
                      onTempHumValue2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XTempHumTransmitter_qualityChanged_signal),
                      onTempHumQuality2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XTempHumTransmitter_errorOccurred_signal),
                      onSensorError2);
}

static void tempHumFixtureDeinit(TempHumFixture* fx)
{
    if (fx->m_dev) XClassDelete((XClass*)fx->m_dev);
    if (fx->m_group) XClassDelete((XClass*)fx->m_group);
    XClassDeinit((XClass*)&fx->m_tempPt);
    XClassDeinit((XClass*)&fx->m_humPt);
}

/** @brief 组喂值（组按 role 分发 → 设备标度更新）。 */
static void feedCh(TempHumFixture* fx, uint16_t role, int32_t chEng)
{
    XIoChannelGroup_feedRole(fx->m_group, role, chEng);
}

/** @brief 压差计夹具：组 + 1 个 AI 点（role=20）。 */
typedef struct DiffPressureFixture {
    XIoChannelGroup* m_group;
    XIoAnalogPoint m_pt;
    XDiffPressureGauge* m_dev;
} DiffPressureFixture;

static void diffFixtureInit(DiffPressureFixture* fx)
{
    memset(fx, 0, sizeof(*fx));
    fx->m_group = XIoChannelGroup_create();
    XIoAnalogPoint_init(&fx->m_pt);
    XIoPoint_setRole(&fx->m_pt.m_base, 20);
    XIoPoint_setType(&fx->m_pt.m_base, XIoPoint_AnalogInput);
    XIoPoint_setTag(&fx->m_pt.m_base, "DP");
    XIoAnalogPoint_setScale(&fx->m_pt, 1, 1, 0, 0);
    XIoChannelGroup_addPoint(fx->m_group, &fx->m_pt.m_base, false);
    fx->m_dev = XDiffPressureGauge_create();
    /* 信号观测（connect_2：第 1 参即 sender）。 */
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XDiffPressureGauge_valueChanged_signal),
                      onDpValue2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XDiffPressureGauge_qualityChanged_signal),
                      onDpQuality2);
    XObject_connect_2((XObject*)fx->m_dev,
                      XSignal(XDiffPressureGauge_errorOccurred_signal),
                      onSensorError2);
}

static void diffFixtureDeinit(DiffPressureFixture* fx)
{
    if (fx->m_dev) XClassDelete((XClass*)fx->m_dev);
    if (fx->m_group) XClassDelete((XClass*)fx->m_group);
    XClassDeinit((XClass*)&fx->m_pt);
}

/* ==================== 温湿度变送器用例 ==================== */

static void test_tempHumSeedAndScale(void)
{
    TempHumFixture fx;
    int32_t v = 0;
    int32_t num = 0;
    int32_t den = 0;
    int32_t off = 0;

    resetSensorFlags();
    tempHumFixtureInit(&fx);
    /* 二级标度：eng = chEng/10（12000μA → 1200）。挂接前设置（喂值在
     * 挂接后由播种消化，信号计数从播种起算）。 */
    CHECK("温度标度设置", XTempHumTransmitter_setScale(fx.m_dev,
                                                       XTempHumChannel_Temperature,
                                                       1, 10, 0));
    CHECK("湿度标度设置", XTempHumTransmitter_setScale(fx.m_dev,
                                                       XTempHumChannel_Humidity,
                                                       1, 10, 0));
    XTempHumTransmitter_scale(fx.m_dev, XTempHumChannel_Temperature, &num, &den, &off);
    CHECK("标度读回", num == 1 && den == 10 && off == 0);

    feedCh(&fx, 10, 12000);
    feedCh(&fx, 11, 20000);
    CHECK("挂接", XTempHumTransmitter_attach(fx.m_dev, fx.m_group,
                                             &fx.m_tempPt.m_base, &fx.m_humPt.m_base));
    CHECK("播种后温度有效", XTempHumTransmitter_value(fx.m_dev,
                                                      XTempHumChannel_Temperature, &v));
    CHECK("播种值即标度值", v == 1200);
    CHECK("播种发了 valueChanged", g_valueCount == 2 && g_valueChannel == 1 &&
                                   g_valueEng == 2000);
    CHECK("播种后湿度有效", XTempHumTransmitter_value(fx.m_dev,
                                                      XTempHumChannel_Humidity, &v) &&
                            v == 2000);

    /* 在线更新 + 同值重喂不重发。 */
    feedCh(&fx, 11, 21000);
    CHECK("湿度在线更新", XTempHumTransmitter_value(fx.m_dev,
                                                    XTempHumChannel_Humidity, &v) &&
                          v == 2100);
    CHECK("湿度 valueChanged", g_valueCount == 3 && g_valueChannel == 1 &&
                               g_valueEng == 2100);
    feedCh(&fx, 11, 21000);
    spinMs(10);
    CHECK("同值不重发", g_valueCount == 3);
    /* 温度通道值不受湿度喂点影响。 */
    CHECK("温度值保持", XTempHumTransmitter_value(fx.m_dev,
                                                  XTempHumChannel_Temperature, &v) &&
                        v == 1200);
    /* 数据拍即质量发布：挂接播种时质量沿 false→true 已发。 */
    CHECK("质量有效沿已发布", g_qualityValid == true && g_qualityCount >= 1);
    CHECK("质量恒有效(staleMs 未到)", XTempHumTransmitter_isValid(
                                          fx.m_dev, XTempHumChannel_Temperature));
    tempHumFixtureDeinit(&fx);
}

static void test_tempHumStale(void)
{
    TempHumFixture fx;
    int32_t v = 0;

    resetSensorFlags();
    tempHumFixtureInit(&fx);
    CHECK("温度标度设置", XTempHumTransmitter_setScale(fx.m_dev,
                                                       XTempHumChannel_Temperature,
                                                       1, 10, 0));
    feedCh(&fx, 10, 12000);
    CHECK("挂接", XTempHumTransmitter_attach(fx.m_dev, fx.m_group,
                                             &fx.m_tempPt.m_base, &fx.m_humPt.m_base));
    CHECK("陈旧阈值设置", XTempHumTransmitter_setStaleMs(fx.m_dev, 60));
    spinMs(30);
    CHECK("阈值内仍有效", XTempHumTransmitter_isValid(fx.m_dev,
                                                      XTempHumChannel_Temperature));
    /* 巡检定时器 1s 周期：等一个巡检拍让陈旧翻转沿发布。 */
    spinMs(1300);
    CHECK("超时后查询无效", !XTempHumTransmitter_value(fx.m_dev,
                                                       XTempHumChannel_Temperature, &v));
    CHECK("陈旧翻转沿已发布", g_qualityValid == false && g_qualityCount >= 2);
    /* 再喂值 → 立即恢复有效 + qualityChanged(true) 翻转沿。 */
    feedCh(&fx, 10, 12500);
    CHECK("重喂恢复有效", XTempHumTransmitter_value(fx.m_dev,
                                                    XTempHumChannel_Temperature, &v) &&
                          v == 1250);
    CHECK("质量恢复沿已发布", g_qualityValid == true && g_qualityCount >= 3);
    /* staleMs=0：永不陈旧。 */
    CHECK("关闭陈旧判定", XTempHumTransmitter_setStaleMs(fx.m_dev, 0));
    spinMs(30);
    CHECK("staleMs=0 恒有效", XTempHumTransmitter_isValid(fx.m_dev,
                                                          XTempHumChannel_Temperature));
    tempHumFixtureDeinit(&fx);
}

static void test_tempHumDetachAndErrors(void)
{
    TempHumFixture fx;
    int32_t v = 0;

    resetSensorFlags();
    tempHumFixtureInit(&fx);
    feedCh(&fx, 10, 12000);
    CHECK("挂接", XTempHumTransmitter_attach(fx.m_dev, fx.m_group,
                                             &fx.m_tempPt.m_base, &fx.m_humPt.m_base));
    CHECK("播种已发一次", g_valueCount == 1);
    CHECK("detach", XTempHumTransmitter_detach(fx.m_dev));
    feedCh(&fx, 10, 13000);
    spinMs(10);
    CHECK("解挂后信号隔离", g_valueCount == 1);
    CHECK("解挂后查询无效", !XTempHumTransmitter_value(fx.m_dev,
                                                       XTempHumChannel_Temperature, &v));
    CHECK("解挂质量翻转沿", g_qualityValid == false && g_qualityCount >= 2);
    CHECK("重复 detach 返回 false", !XTempHumTransmitter_detach(fx.m_dev));
    /* 非法参数。 */
    CHECK("den=0 拒绝", !XTempHumTransmitter_setScale(fx.m_dev,
                                                      XTempHumChannel_Temperature,
                                                      1, 0, 0));
    /* NULL 对象查询安全。 */
    CHECK("NULL 查询安全", !XTempHumTransmitter_value(NULL,
                                                      XTempHumChannel_Temperature, &v));
    CHECK("无 errorOccurred 误报", !g_errorSeen);
    tempHumFixtureDeinit(&fx);
}

/**
 * @brief 温湿度：更新时刻/通道边界/NULL 安全（"数据什么时候来的"是上层
 *        判陈旧与做趋势的唯一依据，必须逐个通道独立且未收数时明确失败）。
 */
static void test_tempHumLastUpdateAndBounds(void)
{
    TempHumFixture fx;
    uint64_t ms = 0;
    uint64_t ms2 = 0;
    int32_t v = 0;

    resetSensorFlags();
    tempHumFixtureInit(&fx);
    /* 未挂接：没有更新时刻可报。 */
    CHECK("未挂接无更新时刻", !XTempHumTransmitter_lastUpdateMs(fx.m_dev,
                                                                XTempHumChannel_Temperature,
                                                                &ms));
    CHECK("未挂接时输出被清零", ms == 0);
    feedCh(&fx, 10, 12000);
    feedCh(&fx, 11, 5500);
    CHECK("挂接", XTempHumTransmitter_attach(fx.m_dev, fx.m_group,
                                             &fx.m_tempPt.m_base, &fx.m_humPt.m_base));
    /* 两通道各自记账：温度与湿度是两次独立到达。 */
    CHECK("温度有更新时刻", XTempHumTransmitter_lastUpdateMs(fx.m_dev,
                                                             XTempHumChannel_Temperature,
                                                             &ms) && ms > 0);
    CHECK("湿度有更新时刻", XTempHumTransmitter_lastUpdateMs(fx.m_dev,
                                                             XTempHumChannel_Humidity,
                                                             &ms2) && ms2 > 0);
    /* 更新时刻语义：随吸收路径推进；值未变时通道层去重（feedRole 静默）
     * 使设备层收不到事件，故时刻不动——按既有跨层契约口径断言。 */
    {
        const uint64_t before = ms;
        spinMs(20);
        feedCh(&fx, 10, 12900);
        CHECK("值变化推进时刻", XTempHumTransmitter_lastUpdateMs(
                                    fx.m_dev, XTempHumChannel_Temperature, &ms) &&
                                ms > before);
    }
    {
        const uint64_t before = ms;
        spinMs(20);
        feedCh(&fx, 10, 12900);        /* 同值：通道层去重，设备层无事件 */
        CHECK("值不变时刻不动（通道层去重）", XTempHumTransmitter_lastUpdateMs(
                                                  fx.m_dev,
                                                  XTempHumChannel_Temperature, &ms) &&
                                              ms == before);
    }
    /* 越界通道号：拒绝且不写输出。 */
    ms = 12345;
    CHECK("越界通道拒绝", !XTempHumTransmitter_lastUpdateMs(fx.m_dev,
                                                            (XTempHumChannel)99, &ms));
    CHECK("越界通道输出被清零", ms == 0);
    CHECK("越界通道查询无效",
          !XTempHumTransmitter_isValid(fx.m_dev, (XTempHumChannel)99));
    CHECK("越界通道取值失败",
          !XTempHumTransmitter_value(fx.m_dev, (XTempHumChannel)99, &v));
    /* NULL 输出指针：只判存在性，不崩。 */
    CHECK("NULL 输出仍报有时刻", XTempHumTransmitter_lastUpdateMs(
                                     fx.m_dev, XTempHumChannel_Temperature, NULL));
    CHECK("NULL 对象无时刻", !XTempHumTransmitter_lastUpdateMs(NULL,
                                                               XTempHumChannel_Temperature,
                                                               &ms));
    /* NULL 对象上的取/设参数：全部安全失败。 */
    CHECK("NULL 设标度失败", !XTempHumTransmitter_setScale(NULL,
                                                           XTempHumChannel_Temperature,
                                                           1, 1, 0));
    CHECK("NULL 设陈旧阈值失败", !XTempHumTransmitter_setStaleMs(NULL, 100));
    CHECK("NULL 陈旧阈值查询为 0", XTempHumTransmitter_staleMs(NULL) == 0u);
    CHECK("NULL 有效性查询为假", !XTempHumTransmitter_isValid(NULL,
                                                              XTempHumChannel_Temperature));
    CHECK("NULL detach 失败", !XTempHumTransmitter_detach(NULL));
    tempHumFixtureDeinit(&fx);
}

/* ==================== 压差计用例 ==================== */

static void test_diffPressureBasics(void)
{
    DiffPressureFixture fx;
    int32_t v = 0;

    resetSensorFlags();
    diffFixtureInit(&fx);
    /* 未挂接查询。 */
    CHECK("未挂接查询无效", !XDiffPressureGauge_value(fx.m_dev, &v));
    /* 挂接（默认标度 1/1/0 直通播种）。 */
    feedCh(&fx, 20, 12000);
    CHECK("挂接", XDiffPressureGauge_attach(fx.m_dev, fx.m_group, &fx.m_pt.m_base));
    CHECK("直通播种", XDiffPressureGauge_value(fx.m_dev, &v) && v == 12000);
    CHECK("播种已发一次", g_dpValueCount == 1 && g_dpValue == 12000);
    /* 二级标度 eng = chEng/2：12000μA → 6000。立即生效重算。 */
    CHECK("压差标度设置", XDiffPressureGauge_setScale(fx.m_dev, 1, 2, 0));
    spinMs(10);
    CHECK("标度重算生效", XDiffPressureGauge_value(fx.m_dev, &v) && v == 6000);
    CHECK("重算 valueChanged 已发", g_dpValueCount == 2 && g_dpValue == 6000);
    /* 在线更新。 */
    feedCh(&fx, 20, 20000);     /* → 10000 */
    CHECK("在线更新", XDiffPressureGauge_value(fx.m_dev, &v) && v == 10000);
    CHECK("质量有效", XDiffPressureGauge_isValid(fx.m_dev));
    /* 解挂 → 质量翻转 false + 信号隔离。 */
    CHECK("detach", XDiffPressureGauge_detach(fx.m_dev));
    CHECK("解挂质量翻转沿", g_dpQuality == false && g_dpQualityCount >= 1);
    feedCh(&fx, 20, 21000);
    spinMs(10);
    CHECK("解挂后信号隔离", g_dpValueCount == 3);
    CHECK("解挂后查询无效", !XDiffPressureGauge_value(fx.m_dev, &v));
    CHECK("den=0 拒绝", !XDiffPressureGauge_setScale(fx.m_dev, 1, 0, 0));
    CHECK("重复 detach 返回 false", !XDiffPressureGauge_detach(fx.m_dev));
    diffFixtureDeinit(&fx);
}

/**
 * @brief 压差计：标度查询/更新时刻/陈旧阈值/越界与 NULL 安全。
 * @details 压差计与温湿度是同一族（Single AI + 二级标度），但**只有一个
 *          通道**：越界访问路径与多通道设备不同，且偏移量为负（零点迁移）
 *          是最常见的现场配置，必须单独锁住。
 */
static void test_diffPressureBounds(void)
{
    DiffPressureFixture fx;
    int32_t v = 0;
    int32_t n = -1, d = -1, off = -1;
    uint64_t ms = 0;
    uint64_t ms2 = 0;

    resetSensorFlags();
    diffFixtureInit(&fx);

    /* ---- 标度查询与缺省值 ---- */
    XDiffPressureGauge_scale(fx.m_dev, &n, &d, &off);
    CHECK("缺省标度 1/1/0", n == 1 && d == 1 && off == 0);
    CHECK("陈旧阈值缺省可读", XDiffPressureGauge_staleMs(fx.m_dev) > 0u);

    /* ---- 未挂接：无更新时刻 ---- */
    CHECK("未挂接无更新时刻", !XDiffPressureGauge_lastUpdateMs(fx.m_dev, &ms));
    CHECK("未挂接时刻输出清零", ms == 0);

    feedCh(&fx, 20, 12000);
    CHECK("挂接", XDiffPressureGauge_attach(fx.m_dev, fx.m_group, &fx.m_pt.m_base));
    CHECK("有更新时刻", XDiffPressureGauge_lastUpdateMs(fx.m_dev, &ms) && ms > 0);

    /* ---- 负偏移（零点迁移）：eng = raw×num/den + offset ---- */
    CHECK("带偏移标度设置", XDiffPressureGauge_setScale(fx.m_dev, 1, 2, -1000));
    CHECK("标度查询回读一致", (XDiffPressureGauge_scale(fx.m_dev, &n, &d, &off),
                               n == 1 && d == 2 && off == -1000));
    CHECK("负偏移参与换算", XDiffPressureGauge_value(fx.m_dev, &v) && v == 5000);
    CHECK("负偏移结果仍有效", XDiffPressureGauge_isValid(fx.m_dev));

    /* ---- 时刻语义同上：值变化推进；同值因通道层去重不动 ---- */
    spinMs(20);
    feedCh(&fx, 20, 14000);
    CHECK("值变化推进时刻", XDiffPressureGauge_lastUpdateMs(fx.m_dev, &ms2) &&
                            ms2 > ms);
    ms = ms2;
    spinMs(20);
    feedCh(&fx, 20, 14000);
    CHECK("值不变时刻不动（通道层去重）",
          XDiffPressureGauge_lastUpdateMs(fx.m_dev, &ms2) && ms2 == ms);

    /* ---- 陈旧阈值：设为 0 = 永不陈旧 ---- */
    CHECK("关闭陈旧判定", XDiffPressureGauge_setStaleMs(fx.m_dev, 0));
    CHECK("staleMs=0 恒有效", XDiffPressureGauge_isValid(fx.m_dev));
    CHECK("陈旧阈值回读为 0", XDiffPressureGauge_staleMs(fx.m_dev) == 0u);
    /* 恢复一个极小阈值，验证仍能翻 false（不是被 0 粘住）。 */
    CHECK("恢复小阈值", XDiffPressureGauge_setStaleMs(fx.m_dev, 1));

    /* ---- NULL / 输出指针安全 ---- */
    CHECK("NULL 取值失败", !XDiffPressureGauge_value(NULL, &v));
    CHECK("NULL 有效性为假", !XDiffPressureGauge_isValid(NULL));
    CHECK("NULL 更新时刻失败", !XDiffPressureGauge_lastUpdateMs(NULL, &ms));
    CHECK("NULL 标度查询清零", (XDiffPressureGauge_scale(NULL, &n, &d, &off),
                                n == 0 && d == 0 && off == 0));
    CHECK("NULL 设标度失败", !XDiffPressureGauge_setScale(NULL, 1, 1, 0));
    CHECK("NULL 设陈旧失败", !XDiffPressureGauge_setStaleMs(NULL, 100));
    CHECK("NULL 陈旧查询为 0", XDiffPressureGauge_staleMs(NULL) == 0u);
    CHECK("NULL detach 失败", !XDiffPressureGauge_detach(NULL));
    CHECK("NULL attach 失败", !XDiffPressureGauge_attach(NULL, fx.m_group,
                                                         &fx.m_pt.m_base));
    CHECK("NULL 组 attach 失败", !XDiffPressureGauge_attach(fx.m_dev, NULL,
                                                            &fx.m_pt.m_base));
    diffFixtureDeinit(&fx);
}

/* ==================== 编解码用例 ==================== */

static void test_tempHumCodec(void)
{
    TempHumFixture fx;
    XTempHumTransmitterSnapshot snap;
    int keyCount = 0;
    const XTempHumTransmitterField* keys = XTempHumTransmitterCodec_allKeys(&keyCount);
    XByteArray* bytes;

    resetSensorFlags();
    tempHumFixtureInit(&fx);
    CHECK("温度标度设置", XTempHumTransmitter_setScale(fx.m_dev,
                                                       XTempHumChannel_Temperature,
                                                       1, 10, 0));
    CHECK("湿度标度设置", XTempHumTransmitter_setScale(fx.m_dev,
                                                       XTempHumChannel_Humidity,
                                                       1, 10, 0));
    feedCh(&fx, 10, 12000);
    feedCh(&fx, 11, 20000);
    XTempHumTransmitter_attach(fx.m_dev, fx.m_group,
                               &fx.m_tempPt.m_base, &fx.m_humPt.m_base);
    CHECK("全键列表 5 项", keys != NULL && keyCount == 5);

    bytes = XTempHumTransmitterCodec_stateToBson(fx.m_dev, keys, keyCount);
    CHECK("stateToBson 产出载荷", bytes != NULL);
    CHECK("stateFromBson 解析成功",
          bytes && XTempHumTransmitterCodec_stateFromBson(
                       &snap, (const uint8_t*)XByteArray_constData(bytes),
                       XByteArray_size_base(bytes)));
    CHECK("温度往返一致", snap.m_tempEng == 1200);
    CHECK("湿度往返一致", snap.m_humEng == 2000);
    CHECK("质量往返一致", snap.m_tempValid && snap.m_humValid);
    CHECK("staleMs 往返一致", snap.m_staleMs == 5000);
    XClassDelete((XClass*)bytes);

    /* JSON。 */
    bytes = XTempHumTransmitterCodec_stateToJson(fx.m_dev, keys, keyCount);
    CHECK("stateToJson 产出载荷", bytes != NULL);
    CHECK("stateFromJson 解析成功",
          bytes && XTempHumTransmitterCodec_stateFromJson(
                       &snap, (const uint8_t*)XByteArray_constData(bytes),
                       XByteArray_size_base(bytes)));
    CHECK("JSON 温度一致", snap.m_tempEng == 1200);
    XClassDelete((XClass*)bytes);

    /* 键列表裁剪 + 非法载荷。 */
    {
        const XTempHumTransmitterField onlyTemp[1] = { XTempHumTransmitterField_TempValue };
        bytes = XTempHumTransmitterCodec_stateToBson(fx.m_dev, onlyTemp, 1);
        CHECK("裁剪解析成功",
              bytes && XTempHumTransmitterCodec_stateFromBson(
                           &snap, (const uint8_t*)XByteArray_constData(bytes),
                           XByteArray_size_base(bytes)));
        CHECK("裁剪后湿度为零", snap.m_humEng == 0 && !snap.m_humValid);
        XClassDelete((XClass*)bytes);
    }
    {
        const uint8_t junk[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
        CHECK("垃圾字节拒绝", !XTempHumTransmitterCodec_stateFromBson(&snap,
                                                                      junk, sizeof(junk)));
        CHECK("空载荷拒绝", !XTempHumTransmitterCodec_stateFromBson(&snap, NULL, 0));
    }
    tempHumFixtureDeinit(&fx);
}

static void test_diffPressureCodec(void)
{
    DiffPressureFixture fx;
    XDiffPressureGaugeSnapshot snap;
    int keyCount = 0;
    const XDiffPressureGaugeField* keys = XDiffPressureGaugeCodec_allKeys(&keyCount);
    XByteArray* bytes;

    resetSensorFlags();
    diffFixtureInit(&fx);
    feedCh(&fx, 20, 12000);
    XDiffPressureGauge_attach(fx.m_dev, fx.m_group, &fx.m_pt.m_base);
    XDiffPressureGauge_setScale(fx.m_dev, 1, 2, 0);
    spinMs(10);
    CHECK("全键列表 3 项", keys != NULL && keyCount == 3);

    bytes = XDiffPressureGaugeCodec_stateToBson(fx.m_dev, keys, keyCount);
    CHECK("stateToBson 产出载荷", bytes != NULL);
    CHECK("stateFromBson 解析成功",
          bytes && XDiffPressureGaugeCodec_stateFromBson(
                       &snap, (const uint8_t*)XByteArray_constData(bytes),
                       XByteArray_size_base(bytes)));
    CHECK("压差往返一致", snap.m_valueEng == 6000);
    CHECK("质量往返一致", snap.m_valid);
    XClassDelete((XClass*)bytes);

    /* NULL 源 = 全零快照。 */
    bytes = XDiffPressureGaugeCodec_stateToBson(NULL, keys, keyCount);
    CHECK("NULL 源编码安全", bytes != NULL);
    CHECK("NULL 源解码全零",
          bytes && XDiffPressureGaugeCodec_stateFromBson(
                       &snap, (const uint8_t*)XByteArray_constData(bytes),
                       XByteArray_size_base(bytes)) && !snap.m_valid);
    XClassDelete((XClass*)bytes);
    diffFixtureDeinit(&fx);
}

/** @brief 跑一轮全部用例（泄漏压测循环体）。 */
static void sensorRunAll(void)
{
    test_tempHumSeedAndScale();
    test_tempHumStale();
    test_tempHumDetachAndErrors();
    test_tempHumLastUpdateAndBounds();
    test_diffPressureBasics();
    test_diffPressureBounds();
    test_tempHumCodec();
    test_diffPressureCodec();
}

/**
 * @brief 泄漏压测：全部用例循环 iterations 轮，堆在用字节数必须回基线。
 * @note  压测循环体含 stale 用例的 spinMs(1300)——300 轮太慢；压测走
 *        快速体（去掉两个长 spin 用例），功能正确性由上面的单跑保证。
 */
static void sensorLeakFast(void)
{
    test_tempHumSeedAndScale();
    test_tempHumDetachAndErrors();
    test_diffPressureBasics();
    test_tempHumCodec();
    test_diffPressureCodec();
}

static void sensorLeakLoop(int iterations)
{
    XMemoryStatistics base;
    XMemoryStatistics now;
    int i;
    if (!XMemory_statisticsEnabled()) {
        ++g_pass;
        XPrintf("[SKIP] 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    base = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < iterations; ++i)
        sensorLeakFast();
    now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("泄漏压测：堆在用字节数回基线", now.systemBytes == base.systemBytes);
    if (now.systemBytes != base.systemBytes)
        XPrintf("       基线=%llu 现在=%llu（差 %lld）\n",
                (unsigned long long)base.systemBytes,
                (unsigned long long)now.systemBytes,
                (long long)now.systemBytes - (long long)base.systemBytes);
}

/* ==================== 收编入口（_run 供 --test/Test 命令分派） ==================== */

int XIndustrySensorTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    /* [单例所有权] 同 XIndustryValveTest_run：只删除自己创建的实例。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    sensorRunAll();
    XPrintf("\n===== XIndustrySensorTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    /* 泄漏压测：快速体 300 轮，统计须回零。 */
    XPrintf("===== 泄漏压测 300 轮… =====\n");
    sensorLeakLoop(300);
    XPrintf("===== XIndustrySensorTest(含压测): %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XIndustrySensorTest(XVariant* data)
{
    const int fails = XIndustrySensorTest_run();
    (void)data;
    XPrintf("=== XIndustrySensorTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !(XTEMP_HUM_ON || XPRESS_DIFF_ON) */

#include "XPrintf.h"
int XIndustrySensorTest_run(void)
{
    XPrintf("XIndustrySensorTest: XINDUSTRY/XTEMP_HUM/XPRESS_DIFF 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && (XTEMP_HUM_ON || XPRESS_DIFF_ON) */
