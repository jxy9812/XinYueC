/**
 * @file       xindustry_valve_test.c
 * @brief      Industry 工业设备层电磁阀状态机测试（mock 通道，无硬件依赖）。
 * @details    覆盖：XIoChannel 公共状态与接口默认槽、None 口径同步到位、
 *             DualLimit 口径限位推进与播种、双限位矛盾、在途超时转故障、
 *             同步写拒绝、异步写失败、未挂接拒绝、解挂信号隔离。mock 通道
 *             以真实子类化路径（虚表重载）实现，兼作适配器接入样例。
 *             运行依赖事件循环（XObject 定时器），进程退出码 0=全绿。
 */
#include "XIoChannel.h"
#include "XIoChannel_Protected.h"
#include "XDigitalInput.h"
#include "XDigitalOutput.h"
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

/* 通道 errorOccurred 信号记录 */
static bool g_channelErrorSeen = false;
static int g_channelLastError = 0;

static void resetFlags(void)
{
    g_lastState = XSolenoidValveState_Unknown;
    g_opened = false;
    g_closed = false;
    g_timedOut = false;
    g_errorSeen = false;
    g_lastError = 0;
    g_channelErrorSeen = false;
    g_channelLastError = 0;
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

static void onChannelError2(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_start(args);
    g_channelLastError = XVarList_arg(args, int);
    g_channelErrorSeen = true;
}

static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);   /* 非阻塞拍 + 小睡：对象定时器泵法同相位回归测试 */
    }
}

/* ==================== mock 数字输出通道 ==================== */

typedef struct XMockDigitalOutput {
    XDigitalOutput m_class;   /**< 第一个成员，接口基类。 */
    bool m_last;              /**< 最近一次成功写入的逻辑值。 */
    bool m_failSync;          /**< 置位时下一次写同步拒绝（返回 false）。 */
    bool m_failAsync;         /**< 置位时下一次写受理后确认失败（writeDone(false)）。 */
} XMockDigitalOutput;

XCLASS_DEFINE_BEGING(XMockDigitalOutput)
XCLASS_DEFINE_EXTEND_END(XMockDigitalOutput, XDigitalOutput)

static bool VXMockDigitalOutput_write(XDigitalOutput* self, bool active)
{
    XMockDigitalOutput* mock = (XMockDigitalOutput*)self;

    if (mock->m_failSync) {
        mock->m_failSync = false;
        return false;                       /* 同步拒绝：不发完成信号 */
    }
    if (mock->m_failAsync) {
        mock->m_failAsync = false;
        XDigitalOutput_writeDone_signal(self, false);  /* 异步失败确认 */
        return true;
    }
    mock->m_last = active;
    XDigitalOutput_writeDone_signal(self, true);       /* 同步成功确认 */
    return true;
}

XVtable* XMockDigitalOutput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMockDigitalOutput)
    XVTABLE_INHERIT_XCLASS(XDigitalOutput);
    XVTABLE_OVERLOAD_DEFAULT(EXDigitalOutput_Write, VXMockDigitalOutput_write);
    return XVTABLE_DEFAULT;
}

static void XMockDigitalOutput_init(XMockDigitalOutput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XMockDigitalOutput));
    XDigitalOutput_init((XDigitalOutput*)obj);
    XClassSetVtable(obj, XMockDigitalOutput);
}

/* ==================== mock 数字输入通道 ==================== */

typedef struct XMockDigitalInput {
    XDigitalInput m_class;    /**< 第一个成员，接口基类。 */
    bool m_active;            /**< 当前逻辑值；经 mockInputSet 变更并推送。 */
} XMockDigitalInput;

XCLASS_DEFINE_BEGING(XMockDigitalInput)
XCLASS_DEFINE_EXTEND_END(XMockDigitalInput, XDigitalInput)

static bool VXMockDigitalInput_read(XDigitalInput* self, bool* active)
{
    XMockDigitalInput* mock = (XMockDigitalInput*)self;

    if (!active) return false;
    *active = mock->m_active;
    return true;
}

XVtable* XMockDigitalInput_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMockDigitalInput)
    XVTABLE_INHERIT_XCLASS(XDigitalInput);
    XVTABLE_OVERLOAD_DEFAULT(EXDigitalInput_Read, VXMockDigitalInput_read);
    return XVTABLE_DEFAULT;
}

static void XMockDigitalInput_init(XMockDigitalInput* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XMockDigitalInput));
    XDigitalInput_init((XDigitalInput*)obj);
    XClassSetVtable(obj, XMockDigitalInput);
}

static void mockInputSet(XMockDigitalInput* mock, bool active)
{
    if (mock->m_active == active) return;
    mock->m_active = active;
    XDigitalInput_inputChanged_signal((XDigitalInput*)mock, active);
}

/* ==================== 用例 ==================== */

static void test_ioChannelBasics(void)
{
    XDigitalInput* di = XDigitalInput_create();
    bool value = true;

    resetFlags();
    XObject_connect_2((XObject*)di, XSignal(XIoChannel_errorOccurred_signal),
                      onChannelError2);
    XIoChannel_setTag((XIoChannel*)di, "DI0");
    CHECK("tag 保存", strcmp(XIoChannel_tag_const((XIoChannel*)di), "DI0") == 0);
    CHECK("默认无效", !XIoChannel_isValid((XIoChannel*)di));
    XIoChannel_setValid((XIoChannel*)di, true);
    CHECK("setValid 生效", XIoChannel_isValid((XIoChannel*)di));
    XIoChannel_setError((XIoChannel*)di, XIoChannelError_Timeout);
    CHECK("错误码记录", XIoChannel_lastError((XIoChannel*)di) == XIoChannelError_Timeout);
    CHECK("出错清 valid", !XIoChannel_isValid((XIoChannel*)di));
    CHECK("errorOccurred 信号", g_channelErrorSeen &&
          g_channelLastError == XIoChannelError_Timeout);
    value = true;
    CHECK("接口默认槽拒绝读", !XDigitalInput_read_base(di, &value) && value == true);
    XClassDelete(di);
}

static void test_noneModeSync(void)
{
    XMockDigitalOutput mout;
    XSolenoidValve* valve = XSolenoidValve_create();

    XMockDigitalOutput_init(&mout);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_opened_signal), onOpened2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_closed_signal), onClosed2);

    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, (XDigitalOutput*)&mout));
    CHECK("挂接后 Unknown", g_lastState == XSolenoidValveState_Unknown);
    CHECK("开阀受理", XSolenoidValve_open(valve));
    CHECK("None 口径同步开到位", g_lastState == XSolenoidValveState_Open && g_opened);
    CHECK("mock 已驱动有效态", mout.m_last == true);
    CHECK("关阀受理", XSolenoidValve_close(valve));
    CHECK("None 口径同步关到位", g_lastState == XSolenoidValveState_Closed && g_closed);
    CHECK("mock 已驱动无效态", mout.m_last == false);
    CHECK("开位幂等", XSolenoidValve_open(valve) &&
          g_lastState == XSolenoidValveState_Open);
    CHECK("isFullyOpen/isFullyClosed", XSolenoidValve_isFullyOpen(valve) == true &&
          XSolenoidValve_isFullyClosed(valve) == false);
    XClassDelete(valve);
}

static void test_dualLimitProgress(void)
{
    XMockDigitalOutput mout;
    XMockDigitalInput mol;
    XMockDigitalInput mcl;
    XSolenoidValve* valve = XSolenoidValve_create();

    XMockDigitalOutput_init(&mout);
    XMockDigitalInput_init(&mol);
    XMockDigitalInput_init(&mcl);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_opened_signal), onOpened2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_closed_signal), onClosed2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    CHECK("双限位挂接", XSolenoidValve_attachFeedback_2(valve,
          (XDigitalInput*)&mol, (XDigitalInput*)&mcl));
    CHECK("反馈口径切换", XSolenoidValve_setFeedbackMode(valve,
          XSolenoidValveFeedbackMode_DualLimit));
    XSolenoidValve_setTimeoutMs(valve, 0);   /* 本用例关监督，专心测限位 */
    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, (XDigitalOutput*)&mout));
    CHECK("播种：双限位无效为 Unknown", g_lastState == XSolenoidValveState_Unknown);

    CHECK("开阀受理进入 Opening", XSolenoidValve_open(valve) &&
          g_lastState == XSolenoidValveState_Opening);
    mockInputSet(&mol, true);
    CHECK("开到位输入推进 Open", g_lastState == XSolenoidValveState_Open && g_opened);

    CHECK("关阀受理进入 Closing", XSolenoidValve_close(valve) &&
          g_lastState == XSolenoidValveState_Closing);
    mockInputSet(&mol, false);
    CHECK("在途双无效保持 Closing", g_lastState == XSolenoidValveState_Closing);
    mockInputSet(&mcl, true);
    CHECK("关到位输入推进 Closed", g_lastState == XSolenoidValveState_Closed && g_closed);

    mockInputSet(&mcl, false);
    CHECK("稳态位置丢失转 Unknown", g_lastState == XSolenoidValveState_Unknown);
    mockInputSet(&mol, true);
    CHECK("外部造成开位同样发布", g_lastState == XSolenoidValveState_Open && g_opened);
    mockInputSet(&mcl, true);
    XPrintf("[diag] state=%d errorSeen=%d lastError=%d\n",
            (int)XSolenoidValve_state(valve), (int)g_errorSeen, g_lastError);
    CHECK("双限位矛盾上报错误", g_errorSeen &&
          g_lastError == XSolenoidValveError_LimitContradiction);
    CHECK("双限位矛盾转 Fault", g_lastState == XSolenoidValveState_Fault);
    XClassDelete(valve);
}

static void test_commandTimeout(void)
{
    XMockDigitalOutput mout;
    XMockDigitalInput mol;
    XMockDigitalInput mcl;
    XSolenoidValve* valve = XSolenoidValve_create();

    XMockDigitalOutput_init(&mout);
    XMockDigitalInput_init(&mol);
    XMockDigitalInput_init(&mcl);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_commandTimeout_signal), onTimeout2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    XSolenoidValve_attachFeedback_2(valve, (XDigitalInput*)&mol, (XDigitalInput*)&mcl);
    XSolenoidValve_setFeedbackMode(valve, XSolenoidValveFeedbackMode_DualLimit);
    CHECK("设置监督超时", XSolenoidValve_setTimeoutMs(valve, 60));
    XSolenoidValve_attachOutput(valve, (XDigitalOutput*)&mout);

    CHECK("开阀受理", XSolenoidValve_open(valve));
    spinMs(300);
    CHECK("超时转 Fault", g_lastState == XSolenoidValveState_Fault);
    CHECK("commandTimeout 信号", g_timedOut);
    CHECK("Timeout 错误上报", g_errorSeen &&
          g_lastError == XSolenoidValveError_Timeout);
    CHECK("Fault 后可复位重试（关阀）", XSolenoidValve_close(valve));
    mockInputSet(&mcl, true);
    CHECK("重试后关到位", g_lastState == XSolenoidValveState_Closed);
    XClassDelete(valve);
}

static void test_writeFailures(void)
{
    XMockDigitalOutput mout;
    XSolenoidValve* valve = XSolenoidValve_create();

    XMockDigitalOutput_init(&mout);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);
    XSolenoidValve_attachOutput(valve, (XDigitalOutput*)&mout);

    mout.m_failSync = true;
    CHECK("同步拒绝返回 false", !XSolenoidValve_open(valve));
    CHECK("同步拒绝转 Fault", g_lastState == XSolenoidValveState_Fault);
    CHECK("WriteRejected 上报", g_errorSeen &&
          g_lastError == XSolenoidValveError_WriteRejected);

    XSolenoidValve_setFeedbackMode(valve, XSolenoidValveFeedbackMode_DualLimit);
    mout.m_failAsync = true;
    CHECK("异步失败仍算受理", XSolenoidValve_open(valve));
    CHECK("writeDone(false) 在途判 Fault", g_lastState == XSolenoidValveState_Fault &&
          g_lastError == XSolenoidValveError_WriteFailed);
    XClassDelete(valve);
}

static void test_notAttachedAndDetach(void)
{
    XMockDigitalOutput mout;
    XSolenoidValve* valve = XSolenoidValve_create();

    XMockDigitalOutput_init(&mout);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    CHECK("未挂接开阀拒绝", !XSolenoidValve_open(valve));
    CHECK("NotAttached 上报", g_errorSeen &&
          g_lastError == XSolenoidValveError_NotAttached);

    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, (XDigitalOutput*)&mout));
    CHECK("开阀", XSolenoidValve_open(valve));
    CHECK("解挂出口", XSolenoidValve_detachOutput(valve));
    {
        XSolenoidValveState before = XSolenoidValve_state(valve);
        XDigitalOutput_writeDone_signal((XDigitalOutput*)&mout, false);
        CHECK("解挂后出口信号被隔离",
              XSolenoidValve_state(valve) == before &&
              XSolenoidValve_state(valve) == XSolenoidValveState_Open);
    }
    CHECK("重复解挂返回 false", !XSolenoidValve_detachOutput(valve));
    XClassDelete(valve);
}

/* 边界补充：单限位挂接、双空拒绝、模式切换推断、关位幂等 */
static void test_feedbackEdgeCases(void)
{
    XMockDigitalOutput mout;
    XMockDigitalInput mol;
    XSolenoidValve* valve = XSolenoidValve_create();

    XMockDigitalOutput_init(&mout);
    XMockDigitalInput_init(&mol);
    resetFlags();
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_stateChanged_signal), onStateChanged2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_opened_signal), onOpened2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_closed_signal), onClosed2);
    XObject_connect_2((XObject*)valve, XSignal(XSolenoidValve_errorOccurred_signal), onError2);

    CHECK("双空反馈挂接拒绝", !XSolenoidValve_attachFeedback_2(valve, NULL, NULL));
    CHECK("单限位挂接成功", XSolenoidValve_attachFeedback_2(valve,
          (XDigitalInput*)&mol, NULL));
    CHECK("反馈口径切换", XSolenoidValve_setFeedbackMode(valve,
          XSolenoidValveFeedbackMode_DualLimit));
    XSolenoidValve_setTimeoutMs(valve, 0);
    CHECK("挂接出口", XSolenoidValve_attachOutput(valve, (XDigitalOutput*)&mout));
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
}

/* ==================== main ==================== */

int main(int argc, char** argv)
{
    XCoreApplication* app;

    (void)argc;
    app = XCoreApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    test_ioChannelBasics();
    test_noneModeSync();
    test_dualLimitProgress();
    test_commandTimeout();
    test_writeFailures();
    test_notAttachedAndDetach();
    test_feedbackEdgeCases();

    XPrintf("\n===== XIndustryValve_Test: %d passed, %d failed =====\n",
            g_pass, g_fail);
    XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

#else /* !XINDUSTRY_ON || !XSOLENOID_VALVE_ON */

#include "XPrintf.h"
int main(void)
{
    XPrintf("XIndustryValve_Test: XINDUSTRY/XSOLENOID_VALVE 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XSOLENOID_VALVE_ON */
