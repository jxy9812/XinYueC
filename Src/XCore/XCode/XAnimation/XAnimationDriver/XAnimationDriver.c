#include "XAnimationDriver.h"

#include "XAnimation_p.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XMemory.h"

/** @brief 驱动器帧间隔（毫秒），对标 Qt QUnifiedTimer 的默认帧率。 */
#define XANIMATION_FRAME_INTERVAL_MS 16

/** @brief 顶层 Running 动画登记表元素。 */
typedef struct XAnimationDriver_RegisterEntry {
    XAbstractAnimation* m_animation; ///< 登记的动画，不取得所有权。
} XAnimationDriver_RegisterEntry;

static void VXAnimationDriver_deinit(XAnimationDriver* driver);
static void VXAnimationDriver_timerEvent(XAnimationDriver* driver, XTimerEvent* event);

/** @brief 框架默认驱动器（对标 Qt 的 QUnifiedTimer 内部驱动，懒创建）。 */
static XAnimationDriver* g_defaultDriver = NULL;
/** @brief 顶层动画登记表（模块级，对标 QAnimationTimer 的注册表）。 */
static XVector* g_registeredAnimations = NULL;

static void VXAnimationDriver_deinit(XAnimationDriver* driver)
{
    if (!driver)
        return;
    XAnimationDriver_uninstall(driver);
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit,
                   void(*)(XObject*))((XObject*)driver);
}

XVtable* XAnimationDriver_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAnimationDriver)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXAnimationDriver_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAnimationDriver_deinit);
    return XVTABLE_DEFAULT;
}

XAnimationDriver* XAnimationDriver_create_ex(XMemoryType memory)
{
    XAnimationDriver* driver = XMemory_malloc(sizeof(XAnimationDriver), memory);
    if (!driver)
        return NULL;
    XAnimationDriver_init(driver);
    Set_Class_Memory(driver, memory);
    Set_Class_IsHeap(driver, true);
    return driver;
}

void XAnimationDriver_init(XAnimationDriver* driver)
{
    if (!driver)
        return;
    XObject_init((XObject*)driver);
    XClassSetVtable(driver, XAnimationDriver);
    driver->m_timerId = XTIMER_INVALID_ID;
    driver->m_elapsedStart = 0;
    driver->m_running = false;
}

XAnimationDriver* XAnimationDriver_defaultInstance(void)
{
    if (!g_defaultDriver) {
        g_defaultDriver = XAnimationDriver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!g_defaultDriver)
            return NULL;
    }
    return g_defaultDriver;
}

void XAnimationDriver_install(XAnimationDriver* driver)
{
    if (!driver)
        driver = XAnimationDriver_defaultInstance();
    if (!driver || driver->m_running)
        return;
    driver->m_running = true;
    driver->m_elapsedStart = XDateTime_currentMSecsSinceEpoch();
    driver->m_timerId = XObject_startTimer_ms((XObject*)driver, XANIMATION_FRAME_INTERVAL_MS,
                                              XTimerType_PreciseTimer);
    XAnimationDriver_started_signal(driver);
}

void XAnimationDriver_uninstall(XAnimationDriver* driver)
{
    if (!driver)
        driver = g_defaultDriver;
    if (!driver || !driver->m_running)
        return;
    driver->m_running = false;
    if (driver->m_timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)driver, driver->m_timerId);
        driver->m_timerId = XTIMER_INVALID_ID;
    }
    XAnimationDriver_stopped_signal(driver);
}

void XAnimationDriver_advanceAnimation_internal(void)
{
    XAnimationDriver* driver = g_defaultDriver;
    if (!driver || !driver->m_running || !g_registeredAnimations)
        return;

    const int64_t now = XDateTime_currentMSecsSinceEpoch();
    const int delta = (int)(now - driver->m_elapsedStart);
    driver->m_elapsedStart = now;
    if (delta <= 0)
        return;

    /* 快照后遍历：推进中动画可能到端点自停并从登记表移除。 */
    const int64_t count = XVector_size_base((const XContainer*)g_registeredAnimations);
    XAbstractAnimation** snapshot = (XAbstractAnimation**)XMemory_malloc(
        (size_t)count * sizeof(XAbstractAnimation*), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!snapshot)
        return;
    for (int64_t i = 0; i < count; ++i) {
        XAnimationDriver_RegisterEntry* entry
            = XVector_at_base(g_registeredAnimations, i);
        snapshot[i] = entry ? entry->m_animation : NULL;
    }
    for (int64_t i = 0; i < count; ++i) {
        XAbstractAnimation* animation = snapshot[i];
        if (!animation || animation->m_state != XAbstractAnimation_Running)
            continue;
        if (animation->m_group
            && XAbstractAnimation_state((const XAbstractAnimation*)animation->m_group)
                   != XAbstractAnimation_Stopped) {
            continue;
        }
        XAbstractAnimation_setCurrentTime(animation, animation->m_totalCurrentTime + delta);
    }
    XMemory_free(snapshot, XCLASS_DEFAULT_MEMORY_TYPE);
}

void XAnimationDriver_advance(XAnimationDriver* driver)
{
    if (!driver)
        driver = g_defaultDriver;
    if (!driver)
        return;
    XAnimationDriver_advanceAnimation_internal();
}

bool XAnimationDriver_isRunning(const XAnimationDriver* driver)
{
    if (!driver)
        driver = g_defaultDriver;
    return driver && driver->m_running;
}

int64_t XAnimationDriver_elapsed(const XAnimationDriver* driver)
{
    if (!driver)
        driver = g_defaultDriver;
    if (!driver || !driver->m_running)
        return 0;
    return XDateTime_currentMSecsSinceEpoch() - driver->m_elapsedStart;
}

static void VXAnimationDriver_timerEvent(XAnimationDriver* driver, XTimerEvent* event)
{
    if (!driver || !event)
        return;
    if (XTimerEvent_timerId(event) == driver->m_timerId) {
        XAnimationDriver_advanceAnimation_internal();
        XEvent_accept((XEvent*)event);
    }
}

void XAbstractAnimation_registerDriver_internal(XAbstractAnimation* animation)
{
    if (!animation || animation->m_hasRegisteredTimer)
        return;
    if (!g_registeredAnimations) {
        g_registeredAnimations = XVector_Create(XAnimationDriver_RegisterEntry);
        if (!g_registeredAnimations)
            return;
    }
    XAnimationDriver_RegisterEntry entry;
    entry.m_animation = animation;
    if (!XVector_push_back_1_base(g_registeredAnimations, &entry))
        return;
    animation->m_hasRegisteredTimer = true;
    /* 首个顶层动画出现时自动安装驱动器（对标 Qt 的 ensureTimerUpdate 链）。 */
    XAnimationDriver* driver = XAnimationDriver_defaultInstance();
    if (driver && !driver->m_running)
        XAnimationDriver_install(driver);
}

void XAbstractAnimation_unregisterDriver_internal(XAbstractAnimation* animation)
{
    if (!animation || !animation->m_hasRegisteredTimer)
        return;
    animation->m_hasRegisteredTimer = false;
    if (!g_registeredAnimations)
        return;
    for (int64_t i = 0;
         i < XVector_size_base((const XContainer*)g_registeredAnimations); ++i) {
        XAnimationDriver_RegisterEntry* entry
            = XVector_at_base(g_registeredAnimations, i);
        if (entry && entry->m_animation == animation) {
            XVector_removeAt_base(g_registeredAnimations, i);
            break;
        }
    }
    /* 最后一个顶层动画注销后卸载驱动器，定时器不再空转。 */
    if (g_defaultDriver && g_defaultDriver->m_running
        && XVector_isEmpty_base((const XContainer*)g_registeredAnimations)) {
        XAnimationDriver_uninstall(g_defaultDriver);
    }
}

void* XAnimationDriver_started_signal(XAnimationDriver* driver)
{
    XEmitSignal((XObject*)driver, XAnimationDriver_started_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAnimationDriver_stopped_signal(XAnimationDriver* driver)
{
    XEmitSignal((XObject*)driver, XAnimationDriver_stopped_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
