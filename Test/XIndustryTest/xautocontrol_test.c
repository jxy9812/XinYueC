/**
 * @file       xautocontrol_test.c
 * @brief      Industry 工业设备层自动运行控制器测试（纯框架件，无设备依赖）。
 * @details    覆盖（设计对齐 xindustry_buttonstation_test 收编形态）：
 *               - 状态机四板斧：start/stop 幂等、急停锁存（启动被拒/普通
 *                 停止无效）、复位回停止态且非重启、复位幂等；
 *               - 步进闸门：未注册回调/停止态/急停态全部静默跳过、
 *                 最小步进间隔节流；
 *               - 节流：同 key 间隔内被挡、到期放行、异 key 互不影响、
 *                 表满安全放行；
 *               - 记账：noteAction 成功计数/失败记原因；
 *               - 报警面：raiseAlarm FIFO 取走即消费、满挤最旧、急停自动
 *                 入队、pendingAlarmCount；
 *               - 序列化：状态/告警 BSON↔JSON 四路往返（XAutoControlCodec）；
 *               - NULL 安全与泄漏压测（200 轮）。
 *             运行依赖事件循环（信号派发），进程退出码 0=全绿。
 */
#include "XAutoControl.h"
#include "XAutoControlCodec.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>
#include <stdint.h>

#if XINDUSTRY_ON && XAUTO_CONTROL_ON

/* ==================== 测试辅助 ==================== */

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(desc, cond) \
    do { \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); } \
        else      { g_fail++; XPrintf("[FAIL] %s\n", desc); } \
    } while (0)

/* ---- 信号记录（connect_2：第 1 参即 sender） ---- */
static int g_started = 0;
static int g_stopped = 0;
static int g_estop = 0;
static int g_reset = 0;
static int g_state = -1;
static int g_stateCount = 0;
static int g_alarmKind = 0;
static char g_alarmText[128];
static int g_alarmCount = 0;

static void resetFlags(void)
{
    g_started = 0;
    g_stopped = 0;
    g_estop = 0;
    g_reset = 0;
    g_state = -1;
    g_stateCount = 0;
    g_alarmKind = 0;
    g_alarmText[0] = '\0';
    g_alarmCount = 0;
}

static void onStarted2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_started;
}

static void onStopped2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_stopped;
}

static void onEStop2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_estop;
}

static void onReset2(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++g_reset;
}

static void onState2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_state = XVarList_arg(args, int);
    ++g_stateCount;
}

static void onAlarm2(XObject* sender, XVarList* args)
{
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    g_alarmKind = XVarList_arg(args, int);
    (void)XVarList_arg(args, int64_t);
    {
        const char* text = XVarList_arg(args, const char*);
        snprintf(g_alarmText, sizeof(g_alarmText), "%s", text ? text : "");
    }
    ++g_alarmCount;
}

/** @brief 事件泵小睡（信号派发驱动，同其它 Industry 测试拍法）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/** @brief 装一个连好全部信号的控制器。 */
static XAutoController* makeCtrl(void)
{
    XAutoController* ctl = XAutoController_create();
    XObject_connect_2((XObject*)ctl,
                      XSignal(XAutoController_started_signal), onStarted2);
    XObject_connect_2((XObject*)ctl,
                      XSignal(XAutoController_stopped_signal), onStopped2);
    XObject_connect_2((XObject*)ctl,
                      XSignal(XAutoController_eStopTriggered_signal), onEStop2);
    XObject_connect_2((XObject*)ctl,
                      XSignal(XAutoController_resetDone_signal), onReset2);
    XObject_connect_2((XObject*)ctl,
                      XSignal(XAutoController_stateChanged_signal), onState2);
    XObject_connect_2((XObject*)ctl,
                      XSignal(XAutoController_alarmRaised_signal), onAlarm2);
    return ctl;
}

static void freeCtrl(XAutoController* ctl)
{
    XClassDelete((XClass*)ctl);
}

/* ==================== 用例：状态机四板斧 ==================== */

static void test_lifecycle(void)
{
    XAutoController* ctl;
    uint64_t now = 1000;

    resetFlags();
    ctl = makeCtrl();
    CHECK("初始停止态", !XAutoController_running(ctl) &&
          XAutoController_state(ctl) == XAutoControlState_Stopped);
    CHECK("初始相位 idle", strcmp(XAutoController_phase(ctl), "idle") == 0);

    /* 急停锁存中启动被拒（先验安全闸，再验正常路径）。 */
    CHECK("停止态急停受理", XAutoController_eStop(ctl, now));
    CHECK("急停态查询", XAutoController_estopped(ctl) &&
          XAutoController_state(ctl) == XAutoControlState_EmergencyStop);
    CHECK("急停中启动被拒", !XAutoController_start(ctl, now));
    CHECK("急停中普通停止无效", !XAutoController_stop(ctl, now));
    CHECK("急停幂等", !XAutoController_eStop(ctl, now));
    spinMs(5);
    CHECK("急停信号已发", g_estop == 1);
    CHECK("急停自动入队报警", g_alarmCount == 1 &&
          g_alarmKind == (int)XAutoControlAlarm_EmergencyStop);

    /* 复位：回停止态，不是重启。 */
    CHECK("复位受理", XAutoController_reset(ctl, now));
    CHECK("复位后停止态", !XAutoController_estopped(ctl) &&
          XAutoController_state(ctl) == XAutoControlState_Stopped);
    CHECK("复位幂等", !XAutoController_reset(ctl, now));
    spinMs(5);
    CHECK("复位信号已发", g_reset == 1);
    CHECK("停止信号未发（急停不经 stop）", g_stopped == 0);

    /* 正常运行/停止。 */
    CHECK("启动受理", XAutoController_start(ctl, now));
    CHECK("运行态查询", XAutoController_running(ctl));
    CHECK("启动幂等", !XAutoController_start(ctl, now));
    spinMs(5);
    CHECK("启动信号已发", g_started == 1);
    CHECK("stateChanged 推进", g_stateCount >= 2 &&
          g_state == (int)XAutoControlState_Running);
    CHECK("停止受理", XAutoController_stop(ctl, now));
    CHECK("停止幂等", !XAutoController_stop(ctl, now));
    spinMs(5);
    CHECK("停止信号已发", g_stopped == 1);

    /* NULL 安全。 */
    CHECK("NULL 查询安全", !XAutoController_running(NULL) &&
          !XAutoController_estopped(NULL) &&
          XAutoController_state(NULL) == XAutoControlState_Stopped);
    CHECK("NULL 生命周期安全", !XAutoController_start(NULL, now) &&
          !XAutoController_stop(NULL, now) &&
          !XAutoController_eStop(NULL, now) &&
          !XAutoController_reset(NULL, now));
    freeCtrl(ctl);
}

/* ==================== 用例：步进闸门 ==================== */

static int g_stepCount = 0;
static void stepCounter(void* ud, XAutoController* ctl, uint64_t nowMs)
{
    (void)ud; (void)ctl; (void)nowMs;
    ++g_stepCount;
}

static void test_stepGate(void)
{
    XAutoController* ctl;
    uint64_t now = 2000;

    resetFlags();
    g_stepCount = 0;
    ctl = makeCtrl();

    /* 未注册回调：安全默认（不动作）。 */
    XAutoController_step(ctl, now);
    CHECK("未注册回调静默跳过", g_stepCount == 0);

    /* 停止态：闸门拦住。 */
    XAutoController_setStepHandler(ctl, stepCounter, NULL);
    XAutoController_step(ctl, now);
    CHECK("停止态 step 不放行", g_stepCount == 0);

    /* 运行：首拍放行；间隔内被挡；到期放行。 */
    CHECK("启动", XAutoController_start(ctl, now));
    XAutoController_step(ctl, now);
    CHECK("运行首拍放行", g_stepCount == 1);
    XAutoController_step(ctl, now + 10);     /* 50ms 间隔内。 */
    CHECK("间隔内被挡", g_stepCount == 1);
    XAutoController_step(ctl, now + 60);
    CHECK("到期放行", g_stepCount == 2);

    /* 急停：全面冻结（连步进计时都不留）。 */
    CHECK("运行中急停", XAutoController_eStop(ctl, now + 70));
    XAutoController_step(ctl, now + 200);
    CHECK("急停态 step 不放行", g_stepCount == 2);

    /* 自定义步进间隔。 */
    CHECK("设步进间隔 100ms", XAutoController_setMinStepIntervalMs(ctl, 100));
    CHECK("步进间隔回读", XAutoController_minStepIntervalMs(ctl) == 100);
    CHECK("复位解急停", XAutoController_reset(ctl, now + 210));
    CHECK("再启动", XAutoController_start(ctl, now + 220));
    g_stepCount = 0;
    XAutoController_step(ctl, now + 220);
    CHECK("重启后首拍放行", g_stepCount == 1);
    XAutoController_step(ctl, now + 250);    /* 100ms 内。 */
    CHECK("新间隔内被挡", g_stepCount == 1);
    XAutoController_step(ctl, now + 330);
    CHECK("新间隔到期放行", g_stepCount == 2);
    CHECK("NULL step 安全", (XAutoController_step(NULL, now), true));
    freeCtrl(ctl);
}

/* ==================== 用例：节流 ==================== */

static void test_throttle(void)
{
    XAutoController* ctl;
    uint64_t now = 5000;

    ctl = makeCtrl();
    CHECK("同 key 首次放行", XAutoController_throttle(ctl, "open1", 1000, now));
    CHECK("间隔内被挡", !XAutoController_throttle(ctl, "open1", 1000, now + 500));
    CHECK("到期放行", XAutoController_throttle(ctl, "open1", 1000, now + 1100));
    CHECK("异 key 互不影响", XAutoController_throttle(ctl, "open2", 1000, now));
    CHECK("间隔 0 恒放行", XAutoController_throttle(ctl, "open1", 0, now));

    /* 表满：安全放行 + 记 lastError（宁可多动一次不许卡死）。 */
    {
        char key[32];
        int i;
        bool allPassed = true;
        for (i = 0; i < XAUTO_CONTROL_THROTTLE_SLOTS; ++i) {
            snprintf(key, sizeof(key), "k%d", i);
            if (!XAutoController_throttle(ctl, key, 60000, now)) allPassed = false;
        }
        CHECK("表容量内全放行", allPassed);
        snprintf(key, sizeof(key), "k%d", XAUTO_CONTROL_THROTTLE_SLOTS);
        CHECK("表满仍放行（安全侧）", XAutoController_throttle(ctl, key, 60000, now));
        CHECK("表满记 lastError", XAutoController_lastError(ctl)[0] != '\0');
    }
    CHECK("NULL throttle 安全", !XAutoController_throttle(NULL, "k", 1, now));
    XClassDelete((XClass*)ctl);
}

/* ==================== 用例：记账与报警面 ==================== */

static void test_accountingAndAlarms(void)
{
    XAutoController* ctl;
    XAutoControlAlarm alarm;
    int drained = 0;

    ctl = makeCtrl();
    CHECK("初始动作数 0", XAutoController_actionCount(ctl) == 0);

    XAutoController_noteAction(ctl, true, NULL);
    XAutoController_noteAction(ctl, true, "");
    CHECK("成功动作计数", XAutoController_actionCount(ctl) == 2);
    CHECK("成功清 lastError", XAutoController_lastError(ctl)[0] == '\0');

    XAutoController_noteAction(ctl, false, "写 DO 失败：模块离线");
    CHECK("失败不计数", XAutoController_actionCount(ctl) == 2);
    CHECK("失败记原因", strcmp(XAutoController_lastError(ctl),
                               "写 DO 失败：模块离线") == 0);
    XAutoController_noteAction(ctl, true, NULL);
    CHECK("再成功清原因", XAutoController_lastError(ctl)[0] == '\0');

    /* 报警 FIFO。 */
    CHECK("报警1 入队", XAutoController_raiseAlarm(ctl, (int)XAutoControlAlarm_ActionFailed,
                                                   "动作1", 100));
    CHECK("报警2 入队", XAutoController_raiseAlarm(ctl, (int)XAutoControlAlarm_Custom,
                                                   "动作2", 200));
    CHECK("在队计数", XAutoController_pendingAlarmCount(ctl) == 2);
    CHECK("取走最旧（动作1）", XAutoController_takeAlarm(ctl, &alarm) &&
          alarm.m_kind == (int)XAutoControlAlarm_ActionFailed &&
          strcmp(alarm.m_text, "动作1") == 0 && alarm.m_atMs == 100);
    CHECK("取走次旧（动作2）", XAutoController_takeAlarm(ctl, &alarm) &&
          alarm.m_kind == (int)XAutoControlAlarm_Custom);
    CHECK("队列空", XAutoController_pendingAlarmCount(ctl) == 0 &&
          !XAutoController_takeAlarm(ctl, &alarm));

    /* 满挤最旧（最新告警必在）。 */
    {
        char text[32];
        int i;
        for (i = 0; i < XAUTO_CONTROL_ALARM_CAPACITY + 2; ++i) {
            snprintf(text, sizeof(text), "n%d", i);
            XAutoController_raiseAlarm(ctl, (int)XAutoControlAlarm_Custom, text,
                                       (uint64_t)i);
        }
        CHECK("超容量入队恒成功",
              XAutoController_pendingAlarmCount(ctl) == XAUTO_CONTROL_ALARM_CAPACITY);
        /* 最旧两条（n0/n1）被挤掉，队头应为 n2。 */
        CHECK("挤掉最旧（队头 n2）", XAutoController_takeAlarm(ctl, &alarm) &&
              strcmp(alarm.m_text, "n2") == 0);
        /* 最新一条（n9）仍在队尾。 */
        while (XAutoController_takeAlarm(ctl, &alarm))
            drained++;
        CHECK("最新告警保留", drained == XAUTO_CONTROL_ALARM_CAPACITY - 1 &&
              strcmp(alarm.m_text, "n9") == 0);
    }
    CHECK("NULL 报警面安全", !XAutoController_raiseAlarm(NULL, 1, "t", 0) &&
          !XAutoController_takeAlarm(NULL, &alarm) &&
          XAutoController_pendingAlarmCount(NULL) == 0);
    XClassDelete((XClass*)ctl);
}

/* ==================== 用例：状态快照 ==================== */

static void test_status(void)
{
    XAutoController* ctl;
    XAutoControlStatus st;

    ctl = makeCtrl();
    XAutoController_start(ctl, 9000);
    XAutoController_setPhase(ctl, "drain_wait");
    XAutoController_noteAction(ctl, true, NULL);
    XAutoController_noteAction(ctl, false, "阀操作失败");
    XAutoController_raiseAlarm(ctl, (int)XAutoControlAlarm_Custom, "越限", 100);

    XAutoController_status(ctl, &st);
    CHECK("快照运行态", st.m_running && st.m_state == (int)XAutoControlState_Running);
    CHECK("快照相位", strcmp(st.m_phase, "drain_wait") == 0);
    CHECK("快照动作数", st.m_actions == 1);
    CHECK("快照失败原因", strcmp(st.m_lastError, "阀操作失败") == 0);
    CHECK("快照在队报警", st.m_pendingAlarms == 1);
    CHECK("快照步进间隔", st.m_stepIntervalMs == 50);

    /* 急停投影。 */
    XAutoController_eStop(ctl, 100);
    XAutoController_status(ctl, &st);
    CHECK("急停快照", !st.m_running && st.m_state == (int)XAutoControlState_EmergencyStop);

    /* NULL 快照。 */
    XAutoController_status(NULL, &st);
    CHECK("NULL 快照全零停止", !st.m_running && st.m_state == 0);
    XClassDelete((XClass*)ctl);
}

/* ==================== 用例：编解码往返 ==================== */

static void test_codec(void)
{
    XAutoController* ctl;
    XAutoControlStatus st;
    XAutoControlStatus back;
    XAutoControlAlarm alarm;
    XAutoControlAlarm alarmBack;
    XByteArray* bytes;
    const uint8_t* data;
    uint32_t size;

    ctl = makeCtrl();
    XAutoController_start(ctl, 100);
    XAutoController_setPhase(ctl, "ph1");
    XAutoController_noteAction(ctl, true, NULL);
    XAutoController_noteAction(ctl, false, "err1");
    XAutoController_raiseAlarm(ctl, (int)XAutoControlAlarm_Custom, "text1", 7);
    XAutoController_status(ctl, &st);
    alarm.m_kind = (int)XAutoControlAlarm_EmergencyStop;
    alarm.m_atMs = 42;
    snprintf(alarm.m_text, sizeof(alarm.m_text), "急停文本");

    /* BSON 状态往返。 */
    {
        int keyCount = 0;
        const XAutoControlField* keys = XAutoControlCodec_allKeys(&keyCount);
        bytes = XAutoControlCodec_statusToBson(&st, keys, keyCount);
    }
    CHECK("状态 BSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("状态 BSON 解码", XAutoControlCodec_statusFromBson(&back, data, size));
        CHECK("状态 BSON 往返一致", back.m_running == st.m_running &&
              back.m_state == st.m_state && back.m_actions == st.m_actions &&
              strcmp(back.m_phase, st.m_phase) == 0 &&
              strcmp(back.m_lastError, st.m_lastError) == 0 &&
              back.m_pendingAlarms == st.m_pendingAlarms &&
              back.m_stepIntervalMs == st.m_stepIntervalMs);
        XClassDelete((XClass*)bytes);
    }
    CHECK("状态 BSON 非法输入拒绝", !XAutoControlCodec_statusFromBson(&back, NULL, 0));

    /* JSON 状态往返。 */
    {
        int keyCount = 0;
        const XAutoControlField* keys = XAutoControlCodec_allKeys(&keyCount);
        bytes = XAutoControlCodec_statusToJson(&st, keys, keyCount);
    }
    CHECK("状态 JSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("状态 JSON 解码", XAutoControlCodec_statusFromJson(&back, data, size));
        CHECK("状态 JSON 往返一致", back.m_running == st.m_running &&
              back.m_state == st.m_state &&
              strcmp(back.m_phase, st.m_phase) == 0);
        XClassDelete((XClass*)bytes);
    }

    /* 告警帧往返（两种格式）。 */
    bytes = XAutoControlCodec_alarmToBson(&alarm);
    CHECK("告警 BSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("告警 BSON 解码", XAutoControlCodec_alarmFromBson(&alarmBack, data, size));
        CHECK("告警 BSON 往返一致", alarmBack.m_kind == alarm.m_kind &&
              alarmBack.m_atMs == alarm.m_atMs &&
              strcmp(alarmBack.m_text, alarm.m_text) == 0);
        XClassDelete((XClass*)bytes);
    }
    bytes = XAutoControlCodec_alarmToJson(&alarm);
    CHECK("告警 JSON 编码", bytes != NULL);
    if (bytes) {
        data = (const uint8_t*)XByteArray_constData(bytes);
        size = (uint32_t)XByteArray_size_base(bytes);
        CHECK("告警 JSON 解码", XAutoControlCodec_alarmFromJson(&alarmBack, data, size));
        CHECK("告警 JSON 往返一致", alarmBack.m_kind == alarm.m_kind &&
              strcmp(alarmBack.m_text, alarm.m_text) == 0);
        XClassDelete((XClass*)bytes);
    }
    /* NULL 快照编码（全零帧）与非法解码。 */
    {
        XByteArray* nullBytes = XAutoControlCodec_statusToBson(NULL, NULL, 0);
        CHECK("NULL 快照编码安全", nullBytes != NULL);
        XClassDelete((XClass*)nullBytes);
    }
    CHECK("告警解码拒绝非法", !XAutoControlCodec_alarmFromBson(&alarmBack, NULL, 0));
    XClassDelete((XClass*)ctl);
}

/** @brief 跑一轮全部用例（泄漏压测循环体）。 */
static void autoCtlRunAll(void)
{
    test_lifecycle();
    test_stepGate();
    test_throttle();
    test_accountingAndAlarms();
    test_status();
    test_codec();
}

/**
 * @brief 泄漏压测：全部用例循环 iterations 轮，堆在用字节数必须回基线。
 */
static void autoCtlLeakLoop(int iterations)
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
        autoCtlRunAll();
    now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("泄漏压测：堆在用字节数回基线", now.systemBytes == base.systemBytes);
    if (now.systemBytes != base.systemBytes)
        XPrintf("       基线=%llu 现在=%llu（差 %lld）\n",
                (unsigned long long)base.systemBytes,
                (unsigned long long)now.systemBytes,
                (long long)now.systemBytes - (long long)base.systemBytes);
}

/* ==================== 收编入口（_run 供 --test/Test 命令分派） ==================== */

int XAutoControlTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    autoCtlRunAll();
    XPrintf("\n===== XAutoControlTest: %d passed, %d failed =====\n",
            g_pass, g_fail);

    XPrintf("===== 泄漏压测 200 轮… =====\n");
    autoCtlLeakLoop(200);
    XPrintf("===== XAutoControlTest(含压测): %d passed, %d failed =====\n",
            g_pass, g_fail);

    if (owned) XClassDelete(app);
    return g_fail == 0 ? 0 : 1;
}

void XAutoControlTest(XVariant* data)
{
    const int fails = XAutoControlTest_run();
    (void)data;
    XPrintf("=== XAutoControlTest: %s（失败 %d 项）===\n",
            fails == 0 ? "PASS" : "FAIL", fails);
    fflush(stdout);
}

#else /* !XINDUSTRY_ON || !XAUTO_CONTROL_ON */

#include "XPrintf.h"
int XAutoControlTest_run(void)
{
    XPrintf("XAutoControlTest: XINDUSTRY/XAUTO_CONTROL 未启用，跳过\n");
    return 0;
}

#endif /* XINDUSTRY_ON && XAUTO_CONTROL_ON */
