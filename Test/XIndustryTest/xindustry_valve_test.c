/**
 * @file       xindustry_valve_test.c
 * @brief      Industry 工业设备层电磁阀状态机测试（通道组+测试点，无硬件依赖）。
 * @details    [2026-11-01 通道组重构移植] 原 XIoChannel/XMockDigital* 通道
 *             模型已由 XIoChannelGroup + XIoPoint 轻量点模型取代。覆盖：
 *             XIoPoint 基础（tag/valid/默认槽拒绝）、None 口径同步到位、
 *             DualLimit 口径限位推进与播种、双限位矛盾、在途超时转故障、
 *             同步写拒绝、异步写失败、未挂接拒绝、解挂信号隔离。测试点以
 *             真实子类化路径（虚表重载 WriteRaw）实现：动作出口点记录写值
 *             并可注入同步拒绝/异步失败；限位输入点经 xIoPointFeedValue
 *             驱动（组信号按点指针分发）。兼作点子类接入样例。
 *             运行依赖事件循环（XObject 定时器），进程退出码 0=全绿。
 *             [口径变化] 原用例 test_ioChannelBasics 的通道错误码部分
 *             （errorOccurred/lastError）随 XIoChannel 概念一并移除——
 *             轻量点模型无错误码，故障语义由业务层（阀）承载。
 */
#include "XChannel/XIoChannelGroup.h"
#include "XChannel/XIoLink.h"
#include "XSolenoidValve.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>

#if XINDUSTRY_ON && XSOLENOID_VALVE_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* 电磁阀信号记录（connect_2：第 1 参即 sender） */
static XSolenoidValveState g_lastState = XSolenoidValveState_Unknown;
static bool g_opened = false;
static bool g_closed = false;
static bool g_timedOut = false;
static bool g_errorSeen = false;
static int g_lastError = 0;

static void resetFlags(void)
{
    g_lastState = XSolenoidValveState_Unknown;
    g_opened = false;
    g_closed = false;
    g_timedOut = false;
    g_errorSeen = false;
    g_lastError = 0;
}

static void onStateChanged2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_lastState = XVarList_arg(args, XSolenoidValveState);
}

static void onOpened2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_opened = true;
}

static void onClosed2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_closed = true;
}

static void onTimeout2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_timedOut = true;
}

static void onError2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_lastError = XVarList_arg(args, int);
    g_errorSeen = true;
}

static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);   /* 非阻塞拍 + 小睡：对象定时器泵法同相位回归测试 */
    }
}

/* ==================== 测试用假链路（XIoLink 子类） ==================== */

/**
 * @brief 测试假链路：记录写值、可注入同步拒绝/异步失败。
 * @details [2026-11-02 通道层重写] 旧测试用 XIoPoint 的 WriteRaw 虚槽注入
 *          这些行为；现在点退化为纯数据容器，链路能力在 XIoLink 上，故本
 *          测试改为实现一个假链路。
 */
XCLASS_DEFINE_BEGING(XTestLink)
XCLASS_DEFINE_ENUM(XTestLink, BindRole) = XCLASS_VTABLE_GET_SIZE(XIoLink),
XCLASS_DEFINE_ENUM(XTestLink, ReadRole),
XCLASS_DEFINE_ENUM(XTestLink, WriteRole),
XCLASS_DEFINE_ENUM(XTestLink, IsAsync),
XCLASS_DEFINE_END(XTestLink)

typedef struct XTestLink {
    XIoLink m_base;           /**< 第一个成员，基类继承。 */
    bool m_last;              /**< 最近一次成功写入的逻辑值。 */
    bool m_failSync;          /**< 置位时下一次写同步拒绝（返回 false）。 */
    bool m_failAsync;         /**< 置位时下一次写受理后确认失败。 */
    bool m_async;             /**< 是否声明为异步链路。 */
    int32_t m_values[8];      /**< role → 喂入值（读路径用）。 */
    bool m_hasValue[8];       /**< 该 role 是否已有值。 */
} XTestLink;

static bool VXTestLink_bindRole(XTestLink* self, uint16_t role, uint32_t addr, uint32_t aux)
{
    (void)addr;
    (void)aux;
    return self && role < 8;
}

static bool VXTestLink_readRole(XTestLink* self, uint16_t role, int32_t* outRaw)
{
    if (!self || !outRaw || role >= 8 || !self->m_hasValue[role]) return false;
    *outRaw = self->m_values[role];
    return true;
}

static bool VXTestLink_writeRole(XTestLink* self, uint16_t role, int32_t raw)
{
    XIoChannelGroup* group;
    (void)role;
    if (!self) return false;
    if (self->m_failSync) {
        self->m_failSync = false;
        return false;                       /* 同步拒绝：不发完成信号 */
    }
    group = (XIoChannelGroup*)XIoLink_group(&self->m_base);
    if (self->m_failAsync) {
        self->m_failAsync = false;
        if (group) XIoChannelGroup_completeWrite(group, 1, false);  /* 异步失败确认 */
        return true;
    }
    self->m_last = (raw != 0);
    if (group) XIoChannelGroup_completeWrite(group, 1, true);      /* 同步成功确认 */
    return true;
}

static bool VXTestLink_isAsync(const XTestLink* self)
{
    return self ? self->m_async : false;
}

XVtable* XTestLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTestLink)
    XVTABLE_INHERIT_XCLASS(XIoLink);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_BindRole, VXTestLink_bindRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_ReadRole, VXTestLink_readRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_WriteRole, VXTestLink_writeRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_IsAsync, VXTestLink_isAsync);
    XCLASS_SHOW_SIZE_DEFAULT(XTestLink);
    return XVTABLE_DEFAULT;
}

static void XTestLink_init(XTestLink* obj, bool asyncMode)
{
    if (!obj) return;
    XIoLink_init(&obj->m_base);
    XClassGetVtable(obj) = XTestLink_class_init();
    memset(&obj->m_last, 0, sizeof(XTestLink) - sizeof(XIoLink));
    obj->m_async = asyncMode;
}

/* ==================== 测试用点初始化（纯数据容器） ==================== */

static void testInputPointInit(XIoPoint* pt, uint16_t role, const char* tag)
{
    XIoPoint_init(pt);
    pt->m_type = (uint16_t)XIoPoint_DigitalInput;
    pt->m_role = role;
    pt->m_tag = tag;
}

static void testOutputPointInit(XIoPoint* pt, uint16_t role)
{
    XIoPoint_init(pt);
    pt->m_type = (uint16_t)XIoPoint_DigitalOutput;
    pt->m_role = role;
}

/* 限位驱动：经组喂新值（组按 role 分发 → 阀重读双限位） */
static void mockInputSet(XIoChannelGroup* grp, uint16_t role, bool active)
{
    if (grp) XIoChannelGroup_feedRole(grp, role, active ? 1 : 0);
}

/* ==================== 用例 ==================== */

static void test_ioPointBasics(void)
{
    XIoPoint pt;
    int32_t value = 0;

    XIoPoint_init(&pt);
    XIoPoint_setTag(&pt, "DI0");
    CHECK("tag 保存", strcmp(XIoPoint_tag(&pt), "DI0") == 0);
    CHECK("默认无效", !XIoPoint_isValid(&pt));
    CHECK("无值时不返回", !XIoPoint_value(&pt, &value));
    XIoPoint_setType(&pt, XIoPoint_DigitalInput);
    CHECK("类型设置生效", XIoPoint_type(&pt) == XIoPoint_DigitalInput);
    CHECK("role 默认 0", XIoPoint_role(&pt) == 0);
    CHECK("role 设置生效", XIoPoint_setRole(&pt, 7) && XIoPoint_role(&pt) == 7);
    CHECK("role 越界拒绝", !XIoPoint_setRole(&pt, 9999));
    XClassDeinit((XClass*)&pt);
}

/* 组+三点装配：出口(role=1)/开限位(role=2)/关限位(role=3) */
typedef struct ValveFixture {
    XIoChannelGroup* grp;
    XTestLink* link;
    XIoPoint out;
    XIoPoint mol;
    XIoPoint mcl;
} ValveFixture;

static void fixtureInit(ValveFixture* fx, bool asyncMode)
{
    memset(fx, 0, sizeof(*fx));
    fx->grp = XIoChannelGroup_create();
    fx->link = (XTestLink*)XMemory_malloc(sizeof(XTestLink), XCLASS_DEFAULT_MEMORY_TYPE);
    XTestLink_init(fx->link, asyncMode);
    Set_Class_IsHeap(fx->link, true);
    testOutputPointInit(&fx->out, 1);
    testInputPointInit(&fx->mol, 2, "openLimit");
    testInputPointInit(&fx->mcl, 3, "closeLimit");
    XIoChannelGroup_addPoint(fx->grp, &fx->out, false);
    XIoChannelGroup_addPoint(fx->grp, &fx->mol, false);
    XIoChannelGroup_addPoint(fx->grp, &fx->mcl, false);
    XIoChannelGroup_setLink(fx->grp, (XIoLink*)fx->link);
    XIoLink_start_base((XIoLink*)fx->link);
}

static void fixtureDeinit(ValveFixture* fx)
{
    XClassDeinit((XClass*)&fx->out);
    XClassDeinit((XClass*)&fx->mol);
    XClassDeinit((XClass*)&fx->mcl);
    XClassDelete((XClass*)fx->link);
    XClassDelete(fx->grp);
}

static void test_noneModeSync(void)
{
    ValveFixture fx;
    XSolenoidValve* valve = XSolenoidValve_create();

    fixtureInit(&fx, false);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_opened_signal), onOpened2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_closed_signal), onClosed2);

    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, fx.grp, (XIoPoint*)&fx.out));
    CHECK("挂接后 Unknown", g_lastState == XSolenoidValveState_Unknown);
    CHECK("开阀受理", XSolenoidValve_open(valve));
    CHECK("None 口径同步开到位", g_lastState == XSolenoidValveState_Open && g_opened);
    CHECK("测试点已驱动有效态", fx.link->m_last == true);
    CHECK("关阀受理", XSolenoidValve_close(valve));
    CHECK("None 口径同步关到位", g_lastState == XSolenoidValveState_Closed && g_closed);
    CHECK("测试点已驱动无效态", fx.link->m_last == false);
    CHECK("开位幂等", XSolenoidValve_open(valve) &&
          g_lastState == XSolenoidValveState_Open);
    CHECK("isFullyOpen/isFullyClosed", XSolenoidValve_isFullyOpen(valve) == true &&
          XSolenoidValve_isFullyClosed(valve) == false);
    XClassDelete(valve);
    fixtureDeinit(&fx);
}

static void test_dualLimitProgress(void)
{
    ValveFixture fx;
    XSolenoidValve* valve = XSolenoidValve_create();

    fixtureInit(&fx, false);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_opened_signal), onOpened2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_closed_signal), onClosed2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    CHECK("双限位挂接", XSolenoidValve_attachFeedback_2(valve, fx.grp,
          (XIoPoint*)&fx.mol, (XIoPoint*)&fx.mcl));
    CHECK("反馈口径切换", XSolenoidValve_setFeedbackMode(valve,
          XSolenoidValveFeedbackMode_DualLimit));
    XSolenoidValve_setTimeoutMs(valve, 0);   /* 本用例关监督，专心测限位 */
    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, fx.grp, (XIoPoint*)&fx.out));
    CHECK("播种：双限位无效为 Unknown", g_lastState == XSolenoidValveState_Unknown);

    CHECK("开阀受理进入 Opening", XSolenoidValve_open(valve) &&
          g_lastState == XSolenoidValveState_Opening);
    mockInputSet(fx.grp, 2, true);
    CHECK("开到位输入推进 Open", g_lastState == XSolenoidValveState_Open && g_opened);

    CHECK("关阀受理进入 Closing", XSolenoidValve_close(valve) &&
          g_lastState == XSolenoidValveState_Closing);
    mockInputSet(fx.grp, 2, false);
    CHECK("在途双无效保持 Closing", g_lastState == XSolenoidValveState_Closing);
    mockInputSet(fx.grp, 3, true);
    CHECK("关到位输入推进 Closed", g_lastState == XSolenoidValveState_Closed && g_closed);

    mockInputSet(fx.grp, 3, false);
    CHECK("稳态位置丢失转 Unknown", g_lastState == XSolenoidValveState_Unknown);
    mockInputSet(fx.grp, 2, true);
    CHECK("外部造成开位同样发布", g_lastState == XSolenoidValveState_Open && g_opened);
    mockInputSet(fx.grp, 3, true);
    CHECK("双限位矛盾上报错误", g_errorSeen &&
          g_lastError == XSolenoidValveError_LimitContradiction);
    CHECK("双限位矛盾转 Fault", g_lastState == XSolenoidValveState_Fault);
    XClassDelete(valve);
    fixtureDeinit(&fx);
}

static void test_commandTimeout(void)
{
    ValveFixture fx;
    XSolenoidValve* valve = XSolenoidValve_create();

    fixtureInit(&fx, false);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_commandTimeout_signal), onTimeout2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    XSolenoidValve_attachFeedback_2(valve, fx.grp, (XIoPoint*)&fx.mol, (XIoPoint*)&fx.mcl);
    XSolenoidValve_setFeedbackMode(valve, XSolenoidValveFeedbackMode_DualLimit);
    CHECK("设置监督超时", XSolenoidValve_setTimeoutMs(valve, 60));
    XSolenoidValve_attachOutput(valve, fx.grp, (XIoPoint*)&fx.out);

    CHECK("开阀受理", XSolenoidValve_open(valve));
    spinMs(300);
    CHECK("超时转 Fault", g_lastState == XSolenoidValveState_Fault);
    CHECK("commandTimeout 信号", g_timedOut);
    CHECK("Timeout 错误上报", g_errorSeen &&
          g_lastError == XSolenoidValveError_Timeout);
    CHECK("Fault 后可复位重试（关阀）", XSolenoidValve_close(valve));
    mockInputSet(fx.grp, 3, true);
    CHECK("重试后关到位", g_lastState == XSolenoidValveState_Closed);
    XClassDelete(valve);
    fixtureDeinit(&fx);
}

static void test_writeFailures(void)
{
    ValveFixture fx;
    XSolenoidValve* valve = XSolenoidValve_create();

    fixtureInit(&fx, false);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);
    XSolenoidValve_attachOutput(valve, fx.grp, (XIoPoint*)&fx.out);

    fx.link->m_failSync = true;
    CHECK("同步拒绝返回 false", !XSolenoidValve_open(valve));
    CHECK("同步拒绝转 Fault", g_lastState == XSolenoidValveState_Fault);
    CHECK("WriteRejected 上报", g_errorSeen &&
          g_lastError == XSolenoidValveError_WriteRejected);

    XSolenoidValve_setFeedbackMode(valve, XSolenoidValveFeedbackMode_DualLimit);
    fx.link->m_failAsync = true;
    CHECK("异步失败仍算受理", XSolenoidValve_open(valve));
    CHECK("writeDone(false) 在途判 Fault", g_lastState == XSolenoidValveState_Fault &&
          g_lastError == XSolenoidValveError_WriteFailed);
    XClassDelete(valve);
    fixtureDeinit(&fx);
}

static void test_notAttachedAndDetach(void)
{
    ValveFixture fx;
    XSolenoidValve* valve = XSolenoidValve_create();

    fixtureInit(&fx, false);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    CHECK("未挂接开阀拒绝", !XSolenoidValve_open(valve));
    CHECK("NotAttached 上报", g_errorSeen &&
          g_lastError == XSolenoidValveError_NotAttached);

    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, fx.grp, (XIoPoint*)&fx.out));
    CHECK("开阀", XSolenoidValve_open(valve));
    CHECK("解挂出口", XSolenoidValve_detachOutput(valve));
    {
        XSolenoidValveState before = XSolenoidValve_state(valve);
        /* 解挂后出口落定信号仍会经组发射（点还在组内），但阀已退订 → 隔离 */
        XIoChannelGroup_completeWrite(fx.grp, 1, false);
        CHECK("解挂后出口信号被隔离",
              XSolenoidValve_state(valve) == before &&
              XSolenoidValve_state(valve) == XSolenoidValveState_Open);
    }
    CHECK("重复解挂返回 false", !XSolenoidValve_detachOutput(valve));
    XClassDelete(valve);
    fixtureDeinit(&fx);
}

/* 边界补充：单限位挂接、双空拒绝、模式切换推断、关位幂等 */
static void test_feedbackEdgeCases(void)
{
    ValveFixture fx;
    XSolenoidValve* valve = XSolenoidValve_create();

    fixtureInit(&fx, false);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_opened_signal), onOpened2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_closed_signal), onClosed2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    CHECK("双空反馈挂接拒绝", !XSolenoidValve_attachFeedback_2(valve, fx.grp, NULL, NULL));
    CHECK("单限位挂接成功", XSolenoidValve_attachFeedback_2(valve, fx.grp,
          (XIoPoint*)&fx.mol, NULL));
    CHECK("反馈口径切换", XSolenoidValve_setFeedbackMode(valve,
          XSolenoidValveFeedbackMode_DualLimit));
    XSolenoidValve_setTimeoutMs(valve, 0);
    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, fx.grp, (XIoPoint*)&fx.out));
    CHECK("播种：单限位无效为 Unknown", g_lastState == XSolenoidValveState_Unknown);

    /* DualLimit 下无限位推不开：writeDone 不判到位 */
    CHECK("开阀受理", XSolenoidValve_open(valve));
    CHECK("DualLimit 下无开限位仍 Opening", g_lastState == XSolenoidValveState_Opening);

    /* 切回 None：口径即时生效，下一条命令由出口确认推断到位 */
    CHECK("切回 None 口径", XSolenoidValve_setFeedbackMode(valve,
          XSolenoidValveFeedbackMode_None));
    CHECK("关阀受理", XSolenoidValve_close(valve));
    CHECK("None 口径关到位", g_lastState == XSolenoidValveState_Closed && g_closed);
    CHECK("关位幂等", XSolenoidValve_close(valve) &&
          g_lastState == XSolenoidValveState_Closed);
    XClassDelete(valve);
    fixtureDeinit(&fx);
}

/* ==================== 收编入口（_run 供 --test/Test 命令分派并回失败数。
 * 进程内自跑，无硬件依赖） ==================== */

int XIndustryValveTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    /* [单例所有权] XCoreApplication 是进程单例：create 在已有实例时**原样
     * 返回 g_app**（XCoreApplication.c:115-116）。--test 路径下 main 已在
     * 入口建好实例并对它负责（main.c:376 的 XClassDelete(app)），本函数若
     * 无条件 XClassDelete 会把同一个对象删两次——实测退出时段错误，栈为
     * XClass_deinit_base ← XClass_delete_base ← main+0x4ac。故只删除自己
     * 创建的实例（创建前 instance() 为空），借用的实例留给 main 收。 */
    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    test_ioPointBasics();
    test_noneModeSync();
    test_dualLimitProgress();
    test_commandTimeout();
    test_writeFailures();
    test_notAttachedAndDetach();
    test_feedbackEdgeCases();

    XPrintf("\n===== XIndustryValveTest: %d passed, %d failed =====\n",
            g_pass, g_fail);
    if (owned) XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XIndustryValveTest(XVariant* data)
{
    const int fails = XIndustryValveTest_run();
    (void)data;
    XPrintf("=== XIndustryValveTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !XSOLENOID_VALVE_ON */

#include "XPrintf.h"
int XIndustryValveTest_run(void)
{
    XPrintf("XIndustryValveTest: XINDUSTRY/XSOLENOID_VALVE 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XSOLENOID_VALVE_ON */
