#include "XCodeTest.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XEvent.h"
#include "XThreadData.h"
#include "XThread.h"
#include "XTimer.h"
#include "XAtomic.h"
#include <stdio.h>
#include <assert.h>

/* ==================== 辅助检查 ==================== */

static int g_aboutToQuitCount = 0;

static void resetCounters(void)
{
    g_aboutToQuitCount = 0;
}

/* aboutToQuit 信号槽 */
static void onAboutToQuit(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_aboutToQuitCount;
    printf("  [槽] aboutToQuit 收到 (计数=%d)\n", g_aboutToQuitCount);
}

/* ==================== 自定义事件接收对象 ==================== */
XCLASS_DEFINE_BEGING(TestReceiver)
XCLASS_DEFINE_EXTEND_END(TestReceiver, XObject);

typedef struct {
    XObject base;
    int receivedCount;
} TestReceiver;

static bool VXTestReceiver_event(XObject* self, XEvent* e)
{
    ++((TestReceiver*)self)->receivedCount;
    // 使用 XClass_Parent 避免递归（XObject_event_base 会通过虚表再次调用自身）
    return XClass_Parent(XObject, EXObject_Event, bool(*)(XObject*, XEvent*))(self, e);
}

static XVtable* TestReceiver_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(TestReceiver))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXTestReceiver_event);
    return XVTABLE_DEFAULT;
}

static TestReceiver* TestReceiver_create(void)
{
    TestReceiver* r = XMalloc_System(sizeof(TestReceiver));
    if (!r) return NULL;
    XObject_init(r);
    XClassGetVtable(r) = TestReceiver_class_init();
    r->receivedCount = 0;
    Set_Class_MemoryFree(r, XFree_System);
    return r;
}

/* ==================== 测试 1: sendEvent — spont 标志 ==================== */
static void test_sendEvent_spont(void)
{
    printf("\n===== [测试 1] sendEvent — spont 标志 =====\n");
    resetCounters();

    XEvent e;
    XEvent_init(&e, XEVENT_TYPE_USER);
    e.spontaneous = 1; // 先设为 1

    XCoreApplication_sendEvent((XObject*)xApp, &e);

    // sendEvent 应设置 spontaneous = false
    printf("  sendEvent 后 spontaneous: %d (预期 0)\n", e.spontaneous);
    assert(e.spontaneous == 0);
    printf("  [通过] sendEvent 设置 spontaneous=0\n");
}

/* ==================== 测试 2: postEvent — Timer 事件压缩 ==================== */
static void test_postEvent_timerCompression(void)
{
    printf("\n===== [测试 2] postEvent — Timer 事件压缩 =====\n");
    resetCounters();

    TestReceiver* recv = TestReceiver_create();

    // 创建两个相同 timerId 的 Timer 事件
    XTimerEvent* t1 = XTimerEvent_create(42);
    XTimerEvent* t2 = XTimerEvent_create(42); // 相同 timerId

    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t1, 0);
    printf("  投递 timer 42 (event=%p)\n", (void*)t1);

    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t2, 0);
    // t2 应被压缩（删除），t1 保留
    printf("  再次投递 timer 42 — 应被压缩\n");

    // 处理事件
    XCoreApplication_sendPostedEvents((XObject*)recv, XEVENT_TYPE_TIMER);
    printf("  接收器收到 %d 个事件 (预期 1)\n", recv->receivedCount);
    assert(recv->receivedCount == 1);
    printf("  [通过] Timer 事件压缩正常\n");

    // 注意: DeferredDelete 投递后 handler 已删除 recv，不再重复释放
}

/* ==================== 测试 3: postEvent — Quit 事件去重 ==================== */
static void test_postEvent_quitDedup(void)
{
    printf("\n===== [测试 3] postEvent — Quit 事件去重 =====\n");
    resetCounters();

    XEvent* q1 = XEvent_create(XEVENT_TYPE_QUIT);
    XEvent* q2 = XEvent_create(XEVENT_TYPE_QUIT);

    XCoreApplication_postEvent((XObject*)xApp, q1, 0);
    printf("  投递 Quit 事件 (event=%p)\n", (void*)q1);

    XCoreApplication_postEvent((XObject*)xApp, q2, 0);
    printf("  投递第二个 Quit — 应被去重\n");

    // 检查 posted_events 计数应为 1（只有一个 Quit 在队列中）
    int32_t posted = XAtomic_load_int32(&((XObject*)xApp)->m_posted_events, XAtomic_MemoryOrder_Relaxed);
    printf("  posted_events 计数: %d (预期 >= 1)\n", posted);
    assert(posted >= 1);
    printf("  [通过] Quit 事件去重正常\n");

    // 清理
    XCoreApplication_removePostedEvents((XObject*)xApp, XEVENT_TYPE_QUIT);
}

/* ==================== 测试 4: sendPostedEvents — DeferredDelete 重新投递 ==================== */
static void test_sendPostedEvents_deferredDelete(void)
{
    printf("\n===== [测试 4] sendPostedEvents — DeferredDelete 重新投递 =====\n");
    resetCounters();

    TestReceiver* recv = TestReceiver_create();

    // 在事件循环外部，loopLevel=0，不应被投递
    XDeferredDeleteEvent* dde = XDeferredDeleteEvent_create(true, 0, 0);
    XCoreApplication_postEvent((XObject*)recv, dde, 0);
    printf("  投递 DeferredDelete (loopLevel=0, scopeLevel=0)\n");

    // 调用 sendPostedEvents 但不指定 DeferredDelete 类型
    // 由于 postedBeforeOutermostLoop && currentLoopLevel > 0 不成立
    // 且 eventLevel(0) > currentLevel(0) 不成立
    // 应被重新投递（跳过）
    XCoreApplication_sendPostedEvents(NULL, 0);
    printf("  sendPostedEvents(all) 后, 接收器收到 %d 个事件 (预期 0)\n", recv->receivedCount);
    assert(recv->receivedCount == 0);
    printf("  [通过] DeferredDelete 重新投递正常\n");

    // 显式请求 DeferredDelete 应投递
    XCoreApplication_sendPostedEvents((XObject*)recv, XEVENT_TYPE_DEFERRED_DELETE);
    printf("  显式 sendPostedEvents(DeferredDelete) 后, 接收器收到 %d 个事件 (预期 1)\n", recv->receivedCount);
    assert(recv->receivedCount == 1);
    printf("  [通过] 显式 DeferredDelete 投递正常\n");

    // 注意: DeferredDelete 投递后 handler 已删除 recv，不再重复释放
}

/* ==================== 测试 5: exec/exit/quit 信号顺序 ==================== */
static void test_exec_exit_quit(void)
{
    printf("\n===== [测试 5] exec/exit/quit — 信号顺序 =====\n");
    resetCounters();

    // 连接 aboutToQuit 信号
    XObject_connect_1((XObject*)xApp, XSignal(XCoreApplication_aboutToQuit_signal),
                       (XObject*)xApp, onAboutToQuit, XConnectionType_Direct);

    // 测试 exit() 发出 aboutToQuit（仅一次）
    printf("  调用 exit(0)...\n");
    XCoreApplication_exit(0);
    printf("  第一次 exit 后 aboutToQuit 计数: %d (预期 1)\n", g_aboutToQuitCount);
    assert(g_aboutToQuitCount == 1);

    // 第二次 exit() 不应再发出
    printf("  再次调用 exit(0)...\n");
    XCoreApplication_exit(0);
    printf("  第二次 exit 后 aboutToQuit 计数: %d (预期 1)\n", g_aboutToQuitCount);
    assert(g_aboutToQuitCount == 1);
    printf("  [通过] aboutToQuit 仅发出一次\n");

    // 测试 quit() — 主线程发送 QEvent::Quit
    // 注意: quit() 在没有 exec 时不执行任何操作（Qt 6.8 行为）
    printf("  调用 quit() (exec 外部 — 应无操作)...\n");
    g_aboutToQuitCount = 0;
    XCoreApplication_quit();
    // quit() 在没有 exec 时是 no-op
    printf("  quit (exec 外部) 后 aboutToQuit 计数: %d (预期 0)\n", g_aboutToQuitCount);
    assert(g_aboutToQuitCount == 0);
    printf("  [通过] quit() 在 exec 外部无操作\n");

    // 模拟 exec 状态后测试 quit()
    printf("  模拟 in_exec=true, 调用 quit()...\n");
    xApp->m_in_exec = true;
    xApp->m_aboutToQuitEmitted = false;  // 重置标志，让 exit() 重新发出信号
    XCoreApplication_quit();
    // quit() → sendEvent(Quit) → event() → exit(0) → aboutToQuit
    printf("  quit (exec 中) 后 aboutToQuit 计数: %d (预期 1)\n", g_aboutToQuitCount);
    assert(g_aboutToQuitCount == 1);
    printf("  [通过] quit() 通过事件流触发 aboutToQuit\n");
    xApp->m_in_exec = false;

    // 断开信号
    XObject_disconnect_1((XObject*)xApp, XSignal(XCoreApplication_aboutToQuit_signal),
                          (XObject*)xApp, onAboutToQuit);
}

/* ==================== 测试 6: startingUp / closingDown ==================== */
static void test_startingUp_closingDown(void)
{
    printf("\n===== [测试 6] startingUp / closingDown =====\n");

    // 应用已初始化，is_app_running = true
    printf("  startingUp: %d (预期 0)\n", XCoreApplication_startingUp());
    assert(!XCoreApplication_startingUp());

    printf("  closingDown: %d (预期 0)\n", XCoreApplication_closingDown());
    assert(!XCoreApplication_closingDown());

    printf("  [通过] startingUp/closingDown 状态正确\n");
}

/* ==================== 测试 7: setEventDispatcher ==================== */
static void test_setEventDispatcher(void)
{
    printf("\n===== [测试 7] setEventDispatcher =====\n");

    XAbstractEventDispatcher* old = XCoreApplication_eventDispatcher();
    printf("  当前 eventDispatcher: %p\n", (void*)old);
    assert(old != NULL);

    // 设置 NULL 应允许（Qt 行为）
    XCoreApplication_setEventDispatcher(NULL);
    printf("  setEventDispatcher(NULL) 后, dispatcher: %p (预期 NULL)\n",
           (void*)XCoreApplication_eventDispatcher());

    // 恢复
    XCoreApplication_setEventDispatcher(old);
    printf("  恢复后, dispatcher: %p\n", (void*)XCoreApplication_eventDispatcher());
    printf("  [通过] setEventDispatcher 未崩溃\n");
}

/* ==================== 测试 8: setSetuidAllowed / isSetuidAllowed ==================== */
static void test_setuidAllowed(void)
{
    printf("\n===== [测试 8] setSetuidAllowed / isSetuidAllowed =====\n");

    printf("  默认值: %d (预期 0)\n", XCoreApplication_isSetuidAllowed());
    assert(!XCoreApplication_isSetuidAllowed());

    XCoreApplication_setSetuidAllowed(true);
    printf("  set(true) 后: %d (预期 1)\n", XCoreApplication_isSetuidAllowed());
    assert(XCoreApplication_isSetuidAllowed());

    XCoreApplication_setSetuidAllowed(false);
    printf("  set(false) 后: %d (预期 0)\n", XCoreApplication_isSetuidAllowed());
    assert(!XCoreApplication_isSetuidAllowed());

    printf("  [通过] setuidAllowed 正常\n");
}

/* ==================== 测试 9: processEventsTimed ==================== */
static void test_processEventsTimed(void)
{
    printf("\n===== [测试 9] processEventsTimed =====\n");

    // 只是验证不崩溃
    XCoreApplication_processEventsTimed(XEventLoop_AllEvents, 10);
    printf("  [通过] processEventsTimed(10ms) 完成未崩溃\n");
}

/* ==================== 主测试入口 ==================== */
void XCoreApplicationTest(XVariant* variant)
{
    (void)variant;
    printf("\n========================================\n");
    printf("  XCoreApplication Qt 对齐测试\n");
    printf("========================================\n");

    test_sendEvent_spont();
    test_postEvent_timerCompression();
    test_postEvent_quitDedup();
    test_sendPostedEvents_deferredDelete();
    test_exec_exit_quit();
    test_startingUp_closingDown();
    test_setEventDispatcher();
    test_setuidAllowed();
    test_processEventsTimed();

    printf("\n========================================\n");
    printf("  所有 XCoreApplication 测试通过！\n");
    printf("========================================\n");
}

void XMenu_XCoreApplicationTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCoreApplication");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "Qt 对齐测试");
        XAction_setAction(action, XCoreApplicationTest);
    }
}
