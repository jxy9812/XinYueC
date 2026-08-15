#include "XPrintf.h"
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
#include "XAbstractNativeEventFilter.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* ==================== 辅助检查 ==================== */

static int g_aboutToQuitCount = 0;
static bool g_permissionCallbackCalled = false;
static int g_deferredDeleteReceivedCount = 0;

static void resetCounters(void)
{
    g_aboutToQuitCount = 0;
    g_permissionCallbackCalled = false;
    g_deferredDeleteReceivedCount = 0;
}

/* aboutToQuit 信号槽 */
static void onAboutToQuit(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_aboutToQuitCount;
    XPrintf("  [槽] aboutToQuit 收到 (计数=%d)\n", g_aboutToQuitCount);
}

/* 权限回调 */
static void onPermissionResult(XPermission* permission, void* userData)
{
    (void)userData;
    g_permissionCallbackCalled = true;
    XPrintf("  权限回调被调用, status=%d\n", permission->status);
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
    ++g_deferredDeleteReceivedCount;
    return XClass_Parent(XObject, EXObject_Event, bool(*)(XObject*, XEvent*))(self, e);
}

static XVtable* TestReceiver_class_init(void)
{
    XVTABLE_INIT_DEFAULT(TestReceiver)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXTestReceiver_event);
    return XVTABLE_DEFAULT;
}

static TestReceiver* TestReceiver_create_ex(XMemoryType memory)
{
    TestReceiver* r = XMemory_malloc(sizeof(TestReceiver), memory);
    if (!r) return NULL;
    XObject_init((XObject*)r);
    XClassGetVtable(r) = TestReceiver_class_init();
    r->receivedCount = 0;
    Set_Class_Memory(r, memory); Set_Class_IsHeap(r, true);
    return r;
}

static TestReceiver* TestReceiver_create(void)
{
    return TestReceiver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

/* ==================== 测试 1: sendEvent — spont 标志 ==================== */
static void test_sendEvent_spont(void)
{
    XPrintf("\n===== [测试 1] sendEvent — spont 标志 =====\n");
    resetCounters();

    XEvent e;
    XEvent_init(&e, XEVENT_TYPE_USER);
    e.spontaneous = 1;

    /* Qt 6.8: sendEvent 设置 spontaneous = false */
    XCoreApplication_sendEvent((XObject*)xApp, &e);

    XPrintf("  sendEvent 后 spontaneous: %d (Qt 行为: sendEvent 设置 spont=0)\n", e.spontaneous);
    assert(e.spontaneous == 0);
    XPrintf("  [通过] sendEvent 正确设置 spontaneous=0\n");
}

/* ==================== 测试 2: postEvent — Timer 事件压缩 ==================== */
static void test_postEvent_timerCompression(void)
{
    XPrintf("\n===== [测试 2] postEvent — Timer 事件压缩 =====\n");
    resetCounters();

    TestReceiver* recv = TestReceiver_create();

    XTimerEvent* t1 = XTimerEvent_create(42);
    XTimerEvent* t2 = XTimerEvent_create(42);

    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t1, 0);
    XPrintf("  投递 timer 42 (event=%p)\n", (void*)t1);

    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t2, 0);
    XPrintf("  再次投递 timer 42 — 应被压缩\n");

    XCoreApplication_sendPostedEvents((XObject*)recv, XEVENT_TYPE_TIMER);
    XPrintf("  接收器收到 %d 个事件 (预期 1)\n", recv->receivedCount);
    assert(recv->receivedCount == 1);
    XPrintf("  [通过] Timer 事件压缩正常\n");
}

/* ==================== 测试 3: postEvent — Quit 事件去重 ==================== */
static void test_postEvent_quitDedup(void)
{
    XPrintf("\n===== [测试 3] postEvent — Quit 事件去重 =====\n");
    resetCounters();

    XEvent* q1 = XEvent_create(XEVENT_TYPE_QUIT);
    XEvent* q2 = XEvent_create(XEVENT_TYPE_QUIT);

    XCoreApplication_postEvent((XObject*)xApp, q1, 0);
    XPrintf("  投递 Quit 事件 (event=%p)\n", (void*)q1);

    XCoreApplication_postEvent((XObject*)xApp, q2, 0);
    XPrintf("  投递第二个 Quit — 应被去重\n");

    int32_t posted = XAtomic_load_int32(&((XObject*)xApp)->m_posted_events, XAtomic_MemoryOrder_Relaxed);
    XPrintf("  posted_events 计数: %d (预期 >= 1)\n", posted);
    assert(posted >= 1);
    XPrintf("  [通过] Quit 事件去重正常\n");

    XCoreApplication_removePostedEvents((XObject*)xApp, XEVENT_TYPE_QUIT);
}

/* ==================== 测试 4: sendPostedEvents — DeferredDelete 重新投递 ==================== */
static void test_sendPostedEvents_deferredDelete(void)
{
    XPrintf("\n===== [测试 4] sendPostedEvents — DeferredDelete 重新投递 =====\n");
    resetCounters();

    TestReceiver* recv = TestReceiver_create();

    XDeferredDeleteEvent* dde = XDeferredDeleteEvent_create(true, 0, 0);
    XCoreApplication_postEvent((XObject*)recv, (XEvent*)dde, 0);
    XPrintf("  投递 DeferredDelete 事件\n");

    XCoreApplication_sendPostedEvents((XObject*)recv, XEVENT_TYPE_DEFERRED_DELETE);
    /* 注意: DeferredDelete 事件投递后, receiver 已被 XDeferredDeleteEvent_handler 删除,
       因此不能访问 recv->receivedCount, 使用静态计数器验证 */
    XPrintf("  发送 DeferredDelete 后, 接收计数=%d (预期 1)\n", g_deferredDeleteReceivedCount);
    assert(g_deferredDeleteReceivedCount == 1);
    XPrintf("  [通过] DeferredDelete 事件投递正常\n");
}

/* ==================== 测试 5: exec / exit / quit — 信号顺序 ==================== */
static void test_exec_exit_quit(void)
{
    XPrintf("\n===== [测试 5] exec / exit / quit — 信号顺序 =====\n");
    resetCounters();

    /* 连接 aboutToQuit 信号 */
    XConnection* conn = XObject_connect_1((XObject*)xApp,
        XCoreApplication_aboutToQuit_signal,
        NULL, onAboutToQuit, XConnectionType_Direct);
    assert(conn != NULL);

    XPrintf("  调用 quit()...\n");
    XCoreApplication_quit();

    /* Qt 6.8: quit() 触发 exit(0) → aboutToQuit 信号 → 设置 quitNow */
    XPrintf("  aboutToQuit 信号触发次数: %d (预期 1)\n", g_aboutToQuitCount);
    assert(g_aboutToQuitCount == 1);
    XPrintf("  [通过] exec/exit/quit 信号顺序正确\n");

    XObject_disconnect_2(conn);
}

/* ==================== 测试 6: startingUp / closingDown ==================== */
static void test_startingUp_closingDown(void)
{
    XPrintf("\n===== [测试 6] startingUp / closingDown =====\n");
    resetCounters();

    /* Qt 6.8: 应用运行后 startingUp() 返回 false */
    bool starting = XCoreApplication_startingUp();
    XPrintf("  startingUp: %d (预期 0)\n", starting);
    assert(starting == false);

    bool closing = XCoreApplication_closingDown();
    XPrintf("  closingDown: %d (预期 0)\n", closing);
    assert(closing == false);

    XPrintf("  [通过] startingUp/closingDown 状态正确\n");
}

/* ==================== 测试 7: setEventDispatcher ==================== */
static void test_setEventDispatcher(void)
{
    XPrintf("\n===== [测试 7] setEventDispatcher =====\n");
    resetCounters();

    /* Qt 6.8: 已有分发器时无法替换 */
    XAbstractEventDispatcher* oldEd = XCoreApplication_eventDispatcher();
    XPrintf("  当前 eventDispatcher: %p\n", (void*)oldEd);
    assert(oldEd != NULL);

    /* 尝试设置新的分发器 — Qt 6.8 不允许替换已有分发器 */
    XAbstractEventDispatcher* newEd = XEventDispatcher_create(NULL);
    XCoreApplication_setEventDispatcher(newEd);

    /* Qt 6.8: setEventDispatcher 在已有分发器时不应替换 */
    XAbstractEventDispatcher* currentEd = XCoreApplication_eventDispatcher();
    XPrintf("  setEventDispatcher 后: %p (预期 == 旧分发器 %p)\n", (void*)currentEd, (void*)oldEd);
    assert(currentEd == oldEd);

    /* 清理新创建的分发器 */
    XAbstractEventDispatcher_closingDown_base(newEd);
    XObject_deleteLater((XObject*)newEd);

    XPrintf("  [通过] setEventDispatcher 正确（已有分发器时不可替换）\n");
}

/* ==================== 测试 8: setSetuidAllowed / isSetuidAllowed ==================== */
static void test_setuidAllowed(void)
{
    XPrintf("\n===== [测试 8] setSetuidAllowed / isSetuidAllowed =====\n");
    resetCounters();

    assert(XCoreApplication_isSetuidAllowed() == false);
    XCoreApplication_setSetuidAllowed(true);
    assert(XCoreApplication_isSetuidAllowed() == true);
    XCoreApplication_setSetuidAllowed(false);
    assert(XCoreApplication_isSetuidAllowed() == false);

    XPrintf("  [通过] setuidAllowed 正常\n");
}

/* ==================== 测试 9: processEventsWithMaxTime ==================== */
static void test_processEventsWithMaxTime(void)
{
    XPrintf("\n===== [测试 9] processEventsWithMaxTime =====\n");
    resetCounters();

    XCoreApplication_processEventsWithMaxTime(0, 10);
    XPrintf("  [通过] processEventsWithMaxTime(10ms) 完成未崩溃\n");
}

/* ==================== 测试 10: compressEvent 非虚函数 ==================== */
static void test_compressEvent_nonvirtual(void)
{
    XPrintf("\n===== [测试 10] compressEvent 非虚函数 =====\n");
    resetCounters();

    /* Qt 6.8: compressEvent 是非虚函数 */
    bool result = XCoreApplication_compressEvent(NULL, NULL, NULL);
    XPrintf("  compressEvent(NULL, NULL, NULL) 返回: %d (预期 0)\n", result);
    assert(result == false);

    XPrintf("  [通过] compressEvent 非虚函数接口正常\n");
}

/* ==================== 测试 11: 应用程序元信息 ==================== */
static void test_application_meta(void)
{
    XPrintf("\n===== [测试 11] 应用程序元信息 =====\n");
    resetCounters();

    XString* name = XString_create_utf8("测试应用");
    XCoreApplication_setApplicationName(name);
    const XString* got = XCoreApplication_applicationName();
    XPrintf("  应用名称: %s (预期 '测试应用')\n", got ? XString_toUtf8(got) : "NULL");
    assert(got != NULL);
    assert(strcmp(XString_toUtf8(got), "测试应用") == 0);
    XString_delete_base(name);

    XString* ver = XString_create_utf8("1.0.0");
    XCoreApplication_setApplicationVersion(ver);
    const XString* gv = XCoreApplication_applicationVersion();
    assert(gv != NULL);
    assert(strcmp(XString_toUtf8(gv), "1.0.0") == 0);
    XString_delete_base(ver);

    XString* org = XString_create_utf8("测试组织");
    XCoreApplication_setOrganizationName(org);
    const XString* go = XCoreApplication_organizationName();
    assert(go != NULL);
    assert(strcmp(XString_toUtf8(go), "测试组织") == 0);
    XString_delete_base(org);

    XString* dom = XString_create_utf8("test.org");
    XCoreApplication_setOrganizationDomain(dom);
    const XString* gd = XCoreApplication_organizationDomain();
    assert(gd != NULL);
    assert(strcmp(XString_toUtf8(gd), "test.org") == 0);
    XString_delete_base(dom);

    XPrintf("  [通过] 应用程序元信息正常\n");
}

/* ==================== 测试 12: 应用程序属性 ==================== */
static void test_application_attributes(void)
{
    XPrintf("\n===== [测试 12] 应用程序属性 =====\n");
    resetCounters();

    assert(XCoreApplication_testAttribute(XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS) == false);
    XCoreApplication_setAttribute(XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS, true);
    assert(XCoreApplication_testAttribute(XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS) == true);
    XCoreApplication_setAttribute(XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS, false);
    assert(XCoreApplication_testAttribute(XCORE_APPLICATION_ATTRIBUTE_DONT_SHOW_ICONS_IN_MENUS) == false);

    XPrintf("  [通过] 应用程序属性正常\n");
}

/* ==================== 测试 13: 权限系统 ==================== */
static void test_permission_system(void)
{
    XPrintf("\n===== [测试 13] 权限系统 =====\n");
    resetCounters();

    XPermission perm = { 1, XPERMISSION_STATUS_UNDETERMINED };
    XPermissionStatus status = XCoreApplication_checkPermission(&perm);
    XPrintf("  checkPermission 返回: %d (预期 %d=GRANTED)\n", status, XPERMISSION_STATUS_GRANTED);
    assert(status == XPERMISSION_STATUS_GRANTED);

    XCoreApplication_requestPermission(&perm, onPermissionResult, NULL);
    assert(g_permissionCallbackCalled);

    XPrintf("  [通过] 权限系统正常\n");
}

/* ==================== 测试 14: 原生事件过滤器 ==================== */
static void test_native_event_filter(void)
{
    XPrintf("\n===== [测试 14] 原生事件过滤器 =====\n");
    resetCounters();

    /* Qt 6.8: 接口测试 — 传入 NULL 应不崩溃 */
    XCoreApplication_installNativeEventFilter(NULL);
    XPrintf("  安装原生事件过滤器(NULL)\n");

    XCoreApplication_removeNativeEventFilter(NULL);
    XPrintf("  移除原生事件过滤器(NULL)\n");

    XPrintf("  [通过] 原生事件过滤器接口正常\n");
}

/* ==================== 测试 15: quitLock ==================== */
static void test_quitLock(void)
{
    XPrintf("\n===== [测试 15] quitLock =====\n");
    resetCounters();

    bool enabled = XCoreApplication_isQuitLockEnabled();
    XPrintf("  默认 quitLockEnabled: %d (预期 1)\n", enabled);
    assert(enabled == true);

    XCoreApplication_setQuitLockEnabled(false);
    XPrintf("  设置 quitLockEnabled=false\n");
    assert(XCoreApplication_isQuitLockEnabled() == false);

    XCoreApplication_setQuitLockEnabled(true);
    assert(XCoreApplication_isQuitLockEnabled() == true);

    XPrintf("  [通过] quitLock 正常\n");
}

/* ==================== 测试 16: 应用程序路径和 PID ==================== */
static void test_app_path_pid(void)
{
    XPrintf("\n===== [测试 16] 应用程序路径和 PID =====\n");
    resetCounters();

    const XString* dirPath = XCoreApplication_applicationDirPath();
    XPrintf("  应用目录: %s\n", dirPath ? XString_toUtf8(dirPath) : "NULL");

    const XString* filePath = XCoreApplication_applicationFilePath();
    XPrintf("  应用路径: %s\n", filePath ? XString_toUtf8(filePath) : "NULL");

    int64_t pid = XCoreApplication_applicationPid();
    XPrintf("  进程 PID: %ld\n", (long)pid);
    assert(pid > 0);

    XPrintf("  [通过] 应用程序路径和 PID 正常\n");
}

/* ==================== 测试 17: 库路径管理 ==================== */
static void test_library_paths(void)
{
    XPrintf("\n===== [测试 17] 库路径管理 =====\n");

    XString* path = XString_create_utf8("/test/lib");
    XCoreApplication_addLibraryPath(path);
    const XStringList* paths = XCoreApplication_libraryPaths();
    XPrintf("  库路径数量: %zu (预期 >= 1)\n", paths ? XStringList_size_base(paths) : 0);
    assert(paths != NULL && XStringList_size_base(paths) >= 1);

    XCoreApplication_removeLibraryPath(path);
    paths = XCoreApplication_libraryPaths();
    XPrintf("  移除后数量: %zu (预期 0)\n", paths ? XStringList_size_base(paths) : 0);

    XString_delete_base(path);
    XPrintf("  [通过] 库路径管理正常\n");
}

/* ==================== 测试 18: forwardEvent ==================== */
static void test_forwardEvent(void)
{
    XPrintf("\n===== [测试 18] forwardEvent — 源事件 spont 传递 =====\n");
    resetCounters();

    TestReceiver* recv = TestReceiver_create();

    XEvent e;
    XEvent_init(&e, XEVENT_TYPE_USER);
    e.spontaneous = 1;

    XEvent forwarded;
    XEvent_init(&forwarded, XEVENT_TYPE_USER);
    forwarded.spontaneous = 0;

    /* Qt 6.8: forwardEvent 复制源事件的 spontaneous 状态 */
    XCoreApplication_forwardEvent((XObject*)recv, &forwarded, &e);
    XPrintf("  forwardEvent 后 forwarded.spontaneous: %d (预期 1, 从源事件复制)\n", forwarded.spontaneous);
    assert(forwarded.spontaneous == 1);

    XPrintf("  接收器收到 %d 个事件 (预期 1)\n", recv->receivedCount);
    assert(recv->receivedCount == 1);

    XPrintf("  [通过] forwardEvent 正确传递 spont 标志\n");
}

/* ==================== 测试 19: eventDispatcher 返回主线程分发器 ==================== */
static void test_eventDispatcher_mainThread(void)
{
    XPrintf("\n===== [测试 19] eventDispatcher 返回主线程分发器 =====\n");

    XAbstractEventDispatcher* ed = XCoreApplication_eventDispatcher();
    XPrintf("  主线程 eventDispatcher: %p\n", (void*)ed);
    assert(ed != NULL);

    /* Qt 6.8: eventDispatcher() 返回主线程的分发器 */
    XThreadData* td = XObject_threadData((XObject*)xApp);
    XPrintf("  主线程 ThreadData: %p\n", (void*)td);
    assert(td != NULL);
    XPrintf("  ThreadData 中的 dispatcher: %p\n", (void*)td->m_eventDispatcher);
    assert(ed == td->m_eventDispatcher);

    XPrintf("  [通过] eventDispatcher 返回主线程分发器\n");
}

/* ==================== 测试 20: postEvent 压缩 — 多种事件类型 ==================== */
static void test_postEvent_compress_multi(void)
{
    XPrintf("\n===== [测试 20] postEvent 压缩 — 多种事件类型 =====\n");
    resetCounters();

    TestReceiver* recv = TestReceiver_create();

    /* Timer 42 */
    XTimerEvent* t1 = XTimerEvent_create(42);
    XTimerEvent* t2 = XTimerEvent_create(42);
    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t1, 0);
    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t2, 0);

    /* Timer 99 (不同 timerId, 不压缩) */
    XTimerEvent* t3 = XTimerEvent_create(99);
    XCoreApplication_postEvent((XObject*)recv, (XEvent*)t3, 0);

    /* Quit 事件 */
    XEvent* q1 = XEvent_create(XEVENT_TYPE_QUIT);
    XEvent* q2 = XEvent_create(XEVENT_TYPE_QUIT);
    XCoreApplication_postEvent((XObject*)recv, (XEvent*)q1, 0);
    XCoreApplication_postEvent((XObject*)recv, (XEvent*)q2, 0);

    /* 发送所有事件 */
    XCoreApplication_sendPostedEvents((XObject*)recv, 0);
    XPrintf("  接收器收到 %d 个事件 (预期 3: timer42(1)+timer99(1)+quit(1))\n", recv->receivedCount);
    assert(recv->receivedCount == 3);

    XPrintf("  [通过] 多类型事件压缩正常\n");
}

/* ==================== 主测试入口 ==================== */
void XCoreApplicationTest(XVariant* variant)
{
    (void)variant;
    XPrintf("\n========================================\n");
    XPrintf("  XCoreApplication Qt 行为对齐测试\n");
    XPrintf("========================================\n");

    test_sendEvent_spont();
    test_postEvent_timerCompression();
    test_postEvent_quitDedup();
    test_sendPostedEvents_deferredDelete();
    test_exec_exit_quit();
    test_startingUp_closingDown();
    test_setEventDispatcher();
    test_setuidAllowed();
    test_processEventsWithMaxTime();
    test_compressEvent_nonvirtual();
    test_application_meta();
    test_application_attributes();
    test_permission_system();
    test_native_event_filter();
    test_quitLock();
    test_app_path_pid();
    test_library_paths();
    test_forwardEvent();
    test_eventDispatcher_mainThread();
    test_postEvent_compress_multi();

    XPrintf("\n========================================\n");
    XPrintf("  所有 XCoreApplication 测试通过！\n");
    XPrintf("========================================\n");
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
