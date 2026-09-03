/******************************************************************************
 * @file       XGuiApplication.c
 * @brief      XGuiApplication GUI 应用类实现（对标 Qt 6.8 QGuiApplication）。
 * @details    本文件实现 XGuiApplication 的全部公开 API：
 *             - 生命周期：class_init / init / create_ex / deinit，继承
 *               XCoreApplication 全部虚槽（Notify/Event 沿用父类分发），
 *               仅重载析构清理 GUI 尾部资源；
 *             - 元信息：应用显示名 / 桌面文件名 / 平台名 / 徽标数；
 *             - 窗口注册表：allWindows / topLevelWindows / topLevelAt /
 *               addWindow / removeWindow（lastWindowClosed 与 quit 策略）；
 *             - 屏幕：primaryScreen / screens / screenAt /
 *               devicePixelRatio / screenAdded / screenRemoved /
 *               setPrimaryScreen，全部转发 XScreen 注册表；
 *             - 光标覆盖栈：setOverrideCursor / changeOverrideCursor /
 *               restoreOverrideCursor / overrideCursor（深拷贝入栈）；
 *             - 字体 / 调色板：深拷贝保存并发射 fontChanged /
 *               paletteChanged（XPALETTE_ON 守卫）；
 *             - 输入状态 / 布局方向 / 应用状态 / DPI 策略 / 桌面设置 /
 *               退出策略 / 会话状态 / sync / exec / notify；
 *             - 样式提示与剪贴板惰性单例；
 *             - 全部 14 个信号（fontDatabaseChanged / screenAdded /
 *               screenRemoved / primaryScreenChanged / lastWindowClosed /
 *               focusObjectChanged / focusWindowChanged /
 *               applicationStateChanged / layoutDirectionChanged /
 *               commitDataRequest / saveStateRequest /
 *               applicationDisplayNameChanged / paletteChanged / fontChanged）。
 *             平台层：XPlatformIntegration 平台集成层（init 时创建），
 *               inputMethod()/platformNativeInterface()/platformFunction()/
 *               sync() 对接集成层（XPLATFORMINTEGRATION_ON 关闭时退化为
 *               NULL/空实现）。
 *             模块不依赖任何平台 API：窗口/屏幕由平台接入钩子程序化登记；
 *             平台层为进程内嵌入式单后端。
 * @note       模块总开关 XGUIAPPLICATION_ON 定义于 XGuiConfig.h；置 0 时
 *             本文件实现体整体裁剪。依赖子开关 XSTYLEHINTS_ON/
 *             XCLIPBOARD_ON/XMIMEDATA_ON/XPALETTE_ON/XCURSOR_ON/
 *             XWINDOW_ON/XSCREEN_ON，关闭时对应 API 按头文件注释退化为
 *             空实现/返回 NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiApplication.h"
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XPlatformAccessibility.h"
#endif
#include "XMemory.h"
#include "XPrintf.h"
#include "XString.h"
#include "XVector.h"
#include "XVarList.h"
#include "XGeometry.h"
#include "XFont.h"
#include "XEvent.h"
#include "XAbstractEventDispatcher.h"
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
#include "XWindowSystemInterface.h"
#endif /* XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON */
#include <string.h>

#if XGUIAPPLICATION_ON

/* ==================== 前向声明与辅助函数 ==================== */

static void VXGuiApplication_deinit(XGuiApplication* app);

#if XPLATFORMINTEGRATION_ON
/** @brief 主事件分发器回调：把平台原生事件注入公共事件队列。 */
static bool XGuiApplication_pumpNativeEvents(void* userData)
{
    XGuiApplication* app = (XGuiApplication*)userData;
    if (!app || !app->m_platformIntegration)
        return false;
    return XPlatformIntegration_processNativeEvents(
        app->m_platformIntegration);
}
#endif /* XPLATFORMINTEGRATION_ON */

/** @brief 深拷贝字符串；输入为 NULL 时返回 NULL。 */
static XString* XGuiApplication_cloneString(const XString* source)
{
    return source ? XString_create_copy(source) : NULL;
}

/** @brief 深拷贝图标；输入为 NULL 时返回 NULL。 */
static XIcon* XGuiApplication_cloneIcon(const XIcon* icon)
{
    XIcon* copy;
    if (!icon) return NULL;
    copy = XIcon_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XIcon_copy_base(copy, icon);
    return copy;
}

/** @brief 发射信号并管理参数列表生命周期（与 XWindow/XScreen 相同模式）。 */
static void XGuiApplication_emit(XGuiApplication* self, size_t signal,
                                 XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/** @brief 将 Auto 布局方向解析为平台当前语言对应的有效方向。 */
static XGuiLayoutDirection XGuiApplication_resolveAutoLayoutDirection(
        const XGuiApplication* app)
{
#if XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON
    XPlatformInputContext* inputContext;
    if (app && app->m_platformIntegration) {
        inputContext = XPlatformIntegration_inputContext(
            app->m_platformIntegration);
        if (inputContext && XPlatformInputContext_inputDirection(inputContext) ==
                XInputMethodLayoutDirection_RightToLeft)
            return XGuiLayoutDirection_RightToLeft;
    }
#else
    (void)app;
#endif /* XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON */
    return XGuiLayoutDirection_LeftToRight;
}

#if XCURSOR_ON
/** @brief 深拷贝光标；输入为 NULL 时拷贝一个空光标对象。 */
static XCursor* XGuiApplication_cloneCursor(const XCursor* cursor)
{
    XCursor* copy = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    if (cursor)
        XCursor_copy_base(copy, cursor);
    return copy;
}
#endif /* XCURSOR_ON */

/* ==================== 类初始化与生命周期 ==================== */

XVtable* XGuiApplication_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGuiApplication)
    XVTABLE_INHERIT_XCLASS(XCoreApplication);
    /* 仅重载析构；Notify/Event 沿用 XCoreApplication 的父类分发。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGuiApplication_deinit);
    return XVTABLE_DEFAULT;
}

XGuiApplication* XGuiApplication_instance(void)
{
    XCoreApplication* core = XCoreApplication_instance();
    if (!core) return NULL;
    /* A plain XCoreApplication is not layout-compatible with the GUI tail.
       Do not reinterpret it as XGuiApplication when clients ask for the GUI
       singleton or attempt to create a GUI app after a core app exists. */
    if (XClassGetVtable(core) == XCoreApplication_class_init()) return NULL;
    return (XGuiApplication*)core;
}

XGuiApplication* XGuiApplication_create_ex(XMemoryType memory, int argc,
                                           char** argv)
{
    if (XCoreApplication_instance())
        return XGuiApplication_instance();

    XGuiApplication* app = (XGuiApplication*)XMemory_malloc(sizeof(XGuiApplication), memory);
    if (!app) return NULL;

    XGuiApplication_init(app, argc, argv);
    Set_Class_Memory(app, memory);
    Set_Class_IsHeap(app, true);
    return app;
}

void XGuiApplication_init(XGuiApplication* app, int argc, char** argv)
{
    if (app == NULL) return;

    /* 先清空 GUI 尾部字段，再由基类初始化统一清零 XGuiApplication 中的
       XObject/XCoreApplication 部分，最后套用本类虚函数表。 */
    memset(((XCoreApplication*)app) + 1, 0,
           sizeof(XGuiApplication) - sizeof(XCoreApplication));
    XCoreApplication_init((XCoreApplication*)app, argc, argv);
    XClassSetVtable(app, XGuiApplication);

    /* Qt 6.8 QGuiApplication 默认值。 */
    app->m_quitOnLastWindowClosed = true;
    app->m_desktopSettingsAware = true;
    app->m_requestedLayoutDirection = XGuiLayoutDirection_Auto;
    app->m_layoutDirection = XGuiLayoutDirection_LeftToRight;
    app->m_applicationState = XGuiApplicationState_Inactive;
    app->m_dpiPolicy = XGuiDpiRoundingPolicy_Unset;
    app->m_platformName = XString_create_utf8("xiniyue-embedded");
    app->m_overrideStack = XVector_Create(XCursor*);
    app->m_windows = XVector_Create(XWindow*);
#if XPALETTE_ON
    XPalette_init_default(&app->m_palette);
#endif /* XPALETTE_ON */
#if XPLATFORMINTEGRATION_ON
    app->m_platformIntegration =
        XPlatformIntegration_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    /* XGuiApplication_exec() 复用 XCoreApplication 的事件循环。将原生
     * 平台事件源挂入其现有轮询链后，exec 会自动处理 X11 Expose/Configure/
     * 输入等事件；应用端不需要再手写 wait/processEvents 循环。 */
    if (app->m_platformIntegration)
        app->m_nativeEventPump = XAbstractEventDispatcher_addPollCallback(
            XGuiApplication_pumpNativeEvents, app);
    app->m_layoutDirection = XGuiApplication_resolveAutoLayoutDirection(app);
#endif /* XPLATFORMINTEGRATION_ON */
}

static void VXGuiApplication_deinit(XGuiApplication* app)
{
    size_t i;
    size_t n;
    if (!app) return;

    /* 释放 GUI 尾部拥有的堆资源；借用指针（窗口/焦点/模态）只清空不释放。 */
    if (app->m_displayName) { XString_delete_base(app->m_displayName); app->m_displayName = NULL; }
    if (app->m_desktopFileName) { XString_delete_base(app->m_desktopFileName); app->m_desktopFileName = NULL; }
    if (app->m_platformName) { XString_delete_base(app->m_platformName); app->m_platformName = NULL; }
    if (app->m_sessionId) { XString_delete_base(app->m_sessionId); app->m_sessionId = NULL; }
    if (app->m_sessionKey) { XString_delete_base(app->m_sessionKey); app->m_sessionKey = NULL; }
    if (app->m_windowIcon) { XIcon_delete_base(app->m_windowIcon); app->m_windowIcon = NULL; }
    if (app->m_font) { XFont_delete_base(app->m_font); app->m_font = NULL; }
    if (app->m_overrideStack) {
#if XCURSOR_ON
        n = XVector_size_base((const XContainer*)app->m_overrideStack);
        for (i = 0; i < n; ++i) {
            XCursor** p = (XCursor**)XVector_at_base(app->m_overrideStack, (int64_t)i);
            if (p && *p) XCursor_delete_base(*p);
        }
#else
        (void)i; (void)n;
#endif /* XCURSOR_ON */
        XVector_delete_base((XClass*)app->m_overrideStack);
        app->m_overrideStack = NULL;
    }
    if (app->m_windows) {
        /* 窗口对象由调用方拥有，这里只释放注册表容器。 */
        XVector_delete_base((XClass*)app->m_windows);
        app->m_windows = NULL;
    }
#if XINPUTMETHOD_ON
    /* 输入法先于集成层释放（其绑定输入上下文由集成层拥有）。 */
    if (app->m_inputMethod) {
        XInputMethod_delete_base(app->m_inputMethod);
        app->m_inputMethod = NULL;
    }
#endif /* XINPUTMETHOD_ON */
#if XPLATFORMINTEGRATION_ON
    if (app->m_nativeEventPump) {
        XAbstractEventDispatcher_removePollCallback(app->m_nativeEventPump);
        app->m_nativeEventPump = NULL;
    }
    if (app->m_platformIntegration) {
        XPlatformIntegration_delete_base(app->m_platformIntegration);
        app->m_platformIntegration = NULL;
    }
#endif /* XPLATFORMINTEGRATION_ON */
#if XSTYLEHINTS_ON
    if (app->m_styleHints) {
        XStyleHints_delete_base(app->m_styleHints);
        app->m_styleHints = NULL;
    }
#endif /* XSTYLEHINTS_ON */
#if XCLIPBOARD_ON
    if (app->m_clipboard) {
        XClipboard_delete_base(app->m_clipboard);
        app->m_clipboard = NULL;
    }
#endif /* XCLIPBOARD_ON */

    /* 父类析构：释放 XCoreApplication 资源并清空全局单例。 */
    XClass_Deinit_Parent(XCoreApplication, (XCoreApplication*)app);
}

/* ==================== 应用元信息 ==================== */

void XGuiApplication_setApplicationDisplayName(const XString* name)
{
    XGuiApplication* app = XGuiApplication_instance();
    const XString* oldDisplay;
    bool changed;
    if (!app) return;
    oldDisplay = XGuiApplication_applicationDisplayName();
    changed = (name != oldDisplay);
    if (name && oldDisplay)
        changed = !XString_equals(oldDisplay, name, XChar_CaseSensitive);
    if (!name && !oldDisplay) changed = false;
    if (!changed && ((name == NULL) == (app->m_displayName == NULL))) return;
    if (app->m_displayName) { XString_delete_base(app->m_displayName); app->m_displayName = NULL; }
    app->m_displayName = XGuiApplication_cloneString(name);
    if (changed) XGuiApplication_applicationDisplayNameChanged_signal(app);
}

const XString* XGuiApplication_applicationDisplayName(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return NULL;
    /* Qt falls back to QCoreApplication::applicationName() until an explicit
       display name is set. */
    return app->m_displayName ? app->m_displayName
                              : XCoreApplication_applicationName();
}

void XGuiApplication_setDesktopFileName(const XString* name)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (app->m_desktopFileName) { XString_delete_base(app->m_desktopFileName); app->m_desktopFileName = NULL; }
    app->m_desktopFileName = XGuiApplication_cloneString(name);
}

const XString* XGuiApplication_desktopFileName(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_desktopFileName : NULL;
}

const XString* XGuiApplication_platformName(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_platformName : NULL;
}

void XGuiApplication_setBadgeNumber(int64_t number)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_badgeNumber = number;
}

int64_t XGuiApplication_badgeNumber(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_badgeNumber : 0;
}

/* ==================== 窗口注册表 ==================== */

#if XWINDOW_ON
XVector* XGuiApplication_allWindows(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    XVector* out;
    if (!app || !app->m_windows) return NULL;
    out = XVector_Create(XWindow*);
    if (!out) return NULL;
    for (size_t i = 0; i < XVector_size_base((const XContainer*)app->m_windows); ++i) {
        XWindow* w = XVector_At_Base(app->m_windows, (int64_t)i, XWindow*);
        if (w) XVector_Push_Back_Base(out, XWindow*, w);
    }
    return out;
}

XVector* XGuiApplication_topLevelWindows(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    XVector* out;
    if (!app || !app->m_windows) return NULL;
    out = XVector_Create(XWindow*);
    if (!out) return NULL;
    for (size_t i = 0; i < XVector_size_base((const XContainer*)app->m_windows); ++i) {
        XWindow* w = XVector_At_Base(app->m_windows, (int64_t)i, XWindow*);
        if (!w) continue;
        if (XWindow_parent(w, XWindowAncestor_ExcludeTransients) == NULL)
            XVector_Push_Back_Base(out, XWindow*, w);
    }
    return out;
}

XWindow* XGuiApplication_topLevelAt(const XPoint* pos)
{
    XGuiApplication* app = XGuiApplication_instance();
    XWindow* fallback = NULL;
    size_t n;
    int64_t i;
    if (!app || !pos || !app->m_windows) return NULL;
    n = XVector_size_base((const XContainer*)app->m_windows);
    for (i = (int64_t)n - 1; i >= 0; --i) {
        XWindow* w = XVector_At_Base(app->m_windows, i, XWindow*);
        if (!w) continue;
        if (XWindow_parent(w, XWindowAncestor_ExcludeTransients) != NULL)
            continue;
        {
            XRect r = XWindow_geometry(w);
            if (!XRect_contains(&r, pos->x, pos->y)) continue;
        }
        /* 优先返回可见窗口；后登记的窗口视为更上层。 */
        if (XWindow_isVisible(w)) return w;
        if (!fallback) fallback = w;
    }
    return fallback;
}

void XGuiApplication_addWindow(XWindow* win)
{
    XGuiApplication* app = XGuiApplication_instance();
    size_t n;
    if (!app || !win || !app->m_windows) return;
    n = XVector_size_base((const XContainer*)app->m_windows);
    for (size_t i = 0; i < n; ++i) {
        if (XVector_At_Base(app->m_windows, (int64_t)i, XWindow*) == win)
            return; /* 幂等登记。 */
    }
    XVector_Push_Back_Base(app->m_windows, XWindow*, win);
}

bool XGuiApplication_replaceWindow(XWindow* oldWindow, XWindow* newWindow)
{
    XGuiApplication* app = XGuiApplication_instance();
    size_t n;
    bool replaced = false;
    if (!app || !app->m_windows || !oldWindow || !newWindow) return false;
    n = XVector_size_base((const XContainer*)app->m_windows);
    for (size_t i = 0; i < n; ++i) {
        XWindow** slot = (XWindow**)XVector_at_base(app->m_windows, (int64_t)i);
        if (slot && *slot == oldWindow) {
            *slot = newWindow;
            replaced = true;
        }
    }
    if (!replaced) return false;
    if (app->m_focusWindow == oldWindow) app->m_focusWindow = newWindow;
    if (app->m_modalWindow == oldWindow) app->m_modalWindow = newWindow;
    return true;
}

void XGuiApplication_removeWindow(XWindow* win)
{
    XGuiApplication* app = XGuiApplication_instance();
    size_t n;
    bool removed = false;
    bool wasTopLevel;
    if (!app || !win || !app->m_windows) return;
    n = XVector_size_base((const XContainer*)app->m_windows);
    for (size_t i = 0; i < n; ++i) {
        if (XVector_At_Base(app->m_windows, (int64_t)i, XWindow*) == win) {
            /* Remove every duplicate registration.  The public hook is
               idempotent, but this also repairs legacy duplicate entries. */
            XVector_remove_base(app->m_windows, (int64_t)i, 1);
            --n;
            --i;
            removed = true;
        }
    }
    if (!removed) return;
    wasTopLevel = (XWindow_parent(win, XWindowAncestor_ExcludeTransients) == NULL);

    /* All window references are borrowed.  Clear them at removal time so
       callers may destroy a window immediately after unregistering it. */
    if (app->m_focusWindow == win) {
        app->m_focusWindow = NULL;
        app->m_focusObject = NULL;
        XGuiApplication_focusWindowChanged_signal(app, NULL);
        XGuiApplication_focusObjectChanged_signal(app, NULL);
    }
    if (app->m_modalWindow == win)
        app->m_modalWindow = NULL;

    /* 移除最后一个顶层窗口时发射 lastWindowClosed；若启用退出策略再请求退出。 */
    if (wasTopLevel) {
        /* Do not re-enter topLevelWindows() here: all entries are borrowed,
           and callers may be in a nested destruction path. */
        bool empty = XVector_size_base((const XContainer*)app->m_windows) == 0;
        if (empty) {
            XGuiApplication_lastWindowClosed_signal(app);
            if (app->m_quitOnLastWindowClosed)
                XCoreApplication_quit();
        }
    }
}
#endif /* XWINDOW_ON */

/* ==================== 图标 ==================== */

void XGuiApplication_setWindowIcon(const XIcon* icon)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (app->m_windowIcon) { XIcon_delete_base(app->m_windowIcon); app->m_windowIcon = NULL; }
    app->m_windowIcon = XGuiApplication_cloneIcon(icon);
}

XIcon* XGuiApplication_windowIcon(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app || !app->m_windowIcon) return NULL;
    return XGuiApplication_cloneIcon(app->m_windowIcon);
}

/* ==================== 焦点 / 模态 ==================== */

XWindow* XGuiApplication_focusWindow(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_focusWindow : NULL;
}

XObject* XGuiApplication_focusObject(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_focusObject : NULL;
}

XWindow* XGuiApplication_modalWindow(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_modalWindow : NULL;
}

#if XWINDOW_ON
void XGuiApplication_setFocusWindow(XWindow* window, XObject* object)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (window && !object)
        object = (XObject*)window; /* Qt 语义：焦点对象缺省为窗口自身。 */
    if (app->m_focusWindow != window) {
        app->m_focusWindow = window;
        XGuiApplication_focusWindowChanged_signal(app, window);
    }
    if (app->m_focusObject != object) {
        app->m_focusObject = object;
        XGuiApplication_focusObjectChanged_signal(app, object);
#if XINPUTMETHOD_ON && XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON
        /* Qt 在焦点对象变化时通知平台输入上下文，并重新计算 ImEnabled。 */
        {
            XInputMethod* inputMethod = XGuiApplication_inputMethod();
            XPlatformInputContext* context = inputMethod
                ? XInputMethod_platformContext(inputMethod) : NULL;
            if (context) {
                XPlatformInputContext_setFocusObject(context, object);
                XInputMethod_update(inputMethod, XInputMethodQuery_ImEnabled);
            }
        }
#endif /* XINPUTMETHOD_ON && XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON */
    }
}

void XGuiApplication_setModalWindow(XWindow* window)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_modalWindow = window;
}
#endif /* XWINDOW_ON */

/* ==================== 屏幕 ==================== */

#if XSCREEN_ON
XScreen* XGuiApplication_primaryScreen(void)
{
    return XScreen_primaryScreen();
}

XVector* XGuiApplication_screens(void)
{
    return XScreen_screens();
}

XScreen* XGuiApplication_screenAt(const XPoint* pos)
{
    XVector* list = XScreen_screens();
    XScreen* hit = NULL;
    if (!pos) return NULL;
    if (!list) return NULL;
    for (size_t i = 0; i < XVector_size_base((const XContainer*)list); ++i) {
        XScreen* s = XVector_At_Base(list, (int64_t)i, XScreen*);
        if (!s) continue;
        XRect r = XScreen_geometry(s);
        if (XRect_contains(&r, pos->x, pos->y)) { hit = s; break; }
    }
    XVector_delete_base((XClass*)list);
    return hit;
}

float XGuiApplication_devicePixelRatio(void)
{
    XVector* screens = XScreen_screens();
    float maximum = 1.0f;
    size_t i;
    if (!screens) return maximum;
    for (i = 0; i < XVector_size_base((const XContainer*)screens); ++i) {
        XScreen* screen = XVector_At_Base(screens, (int64_t)i, XScreen*);
        float ratio = XScreen_devicePixelRatio(screen);
        if (ratio > maximum) maximum = ratio;
    }
    XVector_delete_base((XClass*)screens);
    return maximum;
}

void XGuiApplication_screenAdded(XScreen* screen)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app || !screen) return;
    XScreen_register(screen);
    XGuiApplication_screenAdded_signal(app, screen);
}

void XGuiApplication_screenRemoved(XScreen* screen)
{
    XGuiApplication* app = XGuiApplication_instance();
    XScreen* oldPrimary;
    XScreen* newPrimary = NULL;
    if (!app || !screen) return;
    oldPrimary = XScreen_primaryScreen();
    XScreen_unregister(screen);
    XGuiApplication_screenRemoved_signal(app, screen);
    /* Removing the primary screen promotes the first remaining screen, as
       QGuiApplication does, and notifies observers of the change. */
    if (oldPrimary == screen) {
        XVector* screens = XScreen_screens();
        if (screens && XVector_size_base((const XContainer*)screens) > 0)
            newPrimary = XVector_At_Base(screens, 0, XScreen*);
        if (screens) XVector_delete_base((XClass*)screens);
        XScreen_setPrimary(newPrimary);
        XGuiApplication_primaryScreenChanged_signal(app, newPrimary);
    }
}

void XGuiApplication_setPrimaryScreen(XScreen* screen)
{
    XGuiApplication* app = XGuiApplication_instance();
    bool changed;
    if (!app) return;
    changed = (XScreen_primaryScreen() != screen);
    XScreen_setPrimary(screen);
    if (changed)
        XGuiApplication_primaryScreenChanged_signal(app, screen);
}
#endif /* XSCREEN_ON */

/* ==================== 光标覆盖栈 ==================== */

#if XCURSOR_ON
XCursor* XGuiApplication_overrideCursor(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    size_t n;
    if (!app || !app->m_overrideStack) return NULL;
    n = XVector_size_base((const XContainer*)app->m_overrideStack);
    if (n == 0) return NULL;
    return XVector_At_Base(app->m_overrideStack, (int64_t)(n - 1), XCursor*);
}

void XGuiApplication_setOverrideCursor(const XCursor* cursor)
{
    XGuiApplication* app = XGuiApplication_instance();
    XCursor* copy;
    if (!app || !app->m_overrideStack) return;
    copy = XGuiApplication_cloneCursor(cursor);
    if (!copy) return;
    XVector_Push_Back_Base(app->m_overrideStack, XCursor*, copy);
}

void XGuiApplication_changeOverrideCursor(const XCursor* cursor)
{
    XGuiApplication* app = XGuiApplication_instance();
    XCursor* copy;
    size_t n;
    if (!app || !app->m_overrideStack) return;
    n = XVector_size_base((const XContainer*)app->m_overrideStack);
    if (n == 0) {
        XGuiApplication_setOverrideCursor(cursor);
        return;
    }
    copy = XGuiApplication_cloneCursor(cursor);
    if (!copy) return;
    XCursor_delete_base(XVector_At_Base(app->m_overrideStack, (int64_t)(n - 1), XCursor*));
    XVector_At_Base(app->m_overrideStack, (int64_t)(n - 1), XCursor*) = copy;
}

void XGuiApplication_restoreOverrideCursor(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    size_t n;
    if (!app || !app->m_overrideStack) return;
    n = XVector_size_base((const XContainer*)app->m_overrideStack);
    if (n == 0) return;
    XCursor_delete_base(XVector_At_Base(app->m_overrideStack, (int64_t)(n - 1), XCursor*));
    XVector_remove_base(app->m_overrideStack, (int64_t)(n - 1), 1);
}
#endif /* XCURSOR_ON */

/* ==================== 字体 / 调色板 ==================== */

void XGuiApplication_setFont(const XFont* font)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (app->m_font) { XFont_delete_base(app->m_font); app->m_font = NULL; }
    if (font) {
        app->m_font = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      NULL, -1, -1, false);
        if (app->m_font)
            XFont_copy_base(app->m_font, font);
    }
    XGuiApplication_fontChanged_signal(app, app->m_font);
}

XFont* XGuiApplication_font(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    XFont* copy;
    if (!app || !app->m_font) return NULL;
    copy = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, -1, -1, false);
    if (!copy) return NULL;
    XFont_copy_base(copy, app->m_font);
    return copy;
}

#if XPALETTE_ON
void XGuiApplication_setPalette(const XPalette* palette)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (palette)
        XPalette_copy(&app->m_palette, palette);
    else
        XPalette_init_default(&app->m_palette);
    XGuiApplication_paletteChanged_signal(app, &app->m_palette);
}

XPalette XGuiApplication_palette(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    XPalette out;
    if (app)
        XPalette_copy(&out, &app->m_palette);
    else
        XPalette_init_default(&out);
    return out;
}
#endif /* XPALETTE_ON */

/* ==================== 输入状态 ==================== */

XKeyboardModifiers XGuiApplication_keyboardModifiers(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_keyboardModifiers : XKeyboardModifier_NoModifier;
}

XKeyboardModifiers XGuiApplication_queryKeyboardModifiers(void)
{
    XGuiApplication* app = XGuiApplication_instance();
#if XPLATFORMINTEGRATION_ON
    if (app && app->m_platformIntegration)
        return XPlatformIntegration_queryKeyboardModifiers(
            app->m_platformIntegration);
#endif /* XPLATFORMINTEGRATION_ON */
    return app ? app->m_keyboardModifiers : XKeyboardModifier_NoModifier;
}

void XGuiApplication_setKeyboardModifiers(XKeyboardModifiers modifiers)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_keyboardModifiers = modifiers;
}

XMouseButton XGuiApplication_mouseButtons(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_mouseButtons : XMouseButton_NoButton;
}

void XGuiApplication_setMouseButtons(XMouseButton buttons)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_mouseButtons = buttons;
}

/* ==================== 布局方向 ==================== */

void XGuiApplication_setLayoutDirection(XGuiLayoutDirection direction)
{
    XGuiApplication* app = XGuiApplication_instance();
    XGuiLayoutDirection effective;
    if (!app || (direction != XGuiLayoutDirection_LeftToRight &&
                 direction != XGuiLayoutDirection_RightToLeft &&
                 direction != XGuiLayoutDirection_Auto)) return;
    app->m_requestedLayoutDirection = direction;
    effective = direction == XGuiLayoutDirection_Auto
        ? XGuiApplication_resolveAutoLayoutDirection(app) : direction;
    if (app->m_layoutDirection == effective) return;
    app->m_layoutDirection = effective;
    XGuiApplication_layoutDirectionChanged_signal(app, effective);
}

void XGuiApplication_notifyPlatformInputDirectionChanged(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    XGuiLayoutDirection effective;
    if (!app || app->m_requestedLayoutDirection != XGuiLayoutDirection_Auto)
        return;
    effective = XGuiApplication_resolveAutoLayoutDirection(app);
    if (app->m_layoutDirection == effective) return;
    app->m_layoutDirection = effective;
    XGuiApplication_layoutDirectionChanged_signal(app, effective);
}

XGuiLayoutDirection XGuiApplication_layoutDirection(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_layoutDirection : XGuiLayoutDirection_LeftToRight;
}

bool XGuiApplication_isRightToLeft(void)
{
    return XGuiApplication_layoutDirection() == XGuiLayoutDirection_RightToLeft;
}

bool XGuiApplication_isLeftToRight(void)
{
    return XGuiApplication_layoutDirection() == XGuiLayoutDirection_LeftToRight;
}

/* ==================== 样式提示 / 剪贴板 / 输入法 ==================== */

#if XSTYLEHINTS_ON
XStyleHints* XGuiApplication_styleHints(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return NULL;
    if (!app->m_styleHints) {
        app->m_styleHints = XStyleHints_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#if XPLATFORMINTEGRATION_ON
        /* 注入集成层，使平台 styleHint() 可映射单例状态。 */
        if (app->m_platformIntegration)
            XPlatformIntegration_setStyleHints(app->m_platformIntegration,
                                               app->m_styleHints);
#endif /* XPLATFORMINTEGRATION_ON */
    }
    return app->m_styleHints;
}
#else
XStyleHints* XGuiApplication_styleHints(void)
{
    return NULL;
}
#endif /* XSTYLEHINTS_ON */

#if XCLIPBOARD_ON
XClipboard* XGuiApplication_clipboard(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return NULL;
    if (!app->m_clipboard) {
        app->m_clipboard = XClipboard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#if XPLATFORMINTEGRATION_ON
        /* 注入集成层，使平台 clipboard() 可返回进程内单例。 */
        if (app->m_platformIntegration)
            XPlatformIntegration_setClipboard(app->m_platformIntegration,
                                              app->m_clipboard);
#endif /* XPLATFORMINTEGRATION_ON */
    }
    return app->m_clipboard;
}
#else
XClipboard* XGuiApplication_clipboard(void)
{
    return NULL;
}
#endif /* XCLIPBOARD_ON */

#if XINPUTMETHOD_ON
XInputMethod* XGuiApplication_inputMethod(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return NULL;
    if (!app->m_inputMethod) {
        app->m_inputMethod = XInputMethod_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (app->m_inputMethod) {
#if XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON
            /* 与集成层输入上下文双向绑定：网络层转发经
               XPlatformInputContext 承载。 */
            if (app->m_platformIntegration) {
                XPlatformInputContext* ctx =
                    XPlatformIntegration_inputContext(app->m_platformIntegration);
                XInputMethod_setPlatformContext(app->m_inputMethod, ctx);
                if (ctx)
                    XPlatformInputContext_setInputMethod(ctx, app->m_inputMethod);
            }
#endif /* XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON */
        }
    }
    return app->m_inputMethod;
}
#else /* !XINPUTMETHOD_ON */
XInputMethod* XGuiApplication_inputMethod(void)
{
    return NULL;
}
#endif /* XINPUTMETHOD_ON */

/* ==================== 平台接口 ==================== */

XPlatformNativeInterface* XGuiApplication_platformNativeInterface(void)
{
    XGuiApplication* app = XGuiApplication_instance();
#if XPLATFORMINTEGRATION_ON
    if (!app || !app->m_platformIntegration) return NULL;
    return XPlatformIntegration_nativeInterface(app->m_platformIntegration);
#else /* !XPLATFORMINTEGRATION_ON */
    (void)app;
    return NULL;
#endif /* XPLATFORMINTEGRATION_ON */
}

void* XGuiApplication_platformFunction(const char* functionName)
{
    XGuiApplication* app = XGuiApplication_instance();
#if XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEINTERFACE_ON
    XPlatformNativeInterface* ni;
    if (!app || !app->m_platformIntegration) return NULL;
    ni = XPlatformIntegration_nativeInterface(app->m_platformIntegration);
    if (!ni) return NULL;
    return XPlatformNativeInterface_platformFunction(ni, functionName);
#else /* !(XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEINTERFACE_ON) */
    (void)app; (void)functionName;
    return NULL;
#endif /* XPLATFORMINTEGRATION_ON && XPLATFORMNATIVEINTERFACE_ON */
}

/* ==================== 桌面设置 / 退出策略 ==================== */

void XGuiApplication_setDesktopSettingsAware(bool on)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_desktopSettingsAware = on;
}

bool XGuiApplication_desktopSettingsAware(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_desktopSettingsAware : true;
}

void XGuiApplication_setQuitOnLastWindowClosed(bool quit)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_quitOnLastWindowClosed = quit;
}

bool XGuiApplication_quitOnLastWindowClosed(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_quitOnLastWindowClosed : true;
}

/* ==================== 应用状态 / DPI 策略 ==================== */

void XGuiApplication_setApplicationState(XGuiApplicationState state)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app || app->m_applicationState == state) return;
    app->m_applicationState = state;
    XGuiApplication_applicationStateChanged_signal(app, state);
}

XGuiApplicationState XGuiApplication_applicationState(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_applicationState : XGuiApplicationState_Inactive;
}

void XGuiApplication_setHighDpiScaleFactorRoundingPolicy(
        XGuiDpiRoundingPolicy policy)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (app) app->m_dpiPolicy = policy;
}

XGuiDpiRoundingPolicy XGuiApplication_highDpiScaleFactorRoundingPolicy(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_dpiPolicy : XGuiDpiRoundingPolicy_Unset;
}

void XGuiApplication_processEvents(XEventLoopProcessEventsFlags flags)
{
#if XPLATFORMINTEGRATION_ON
    XGuiApplication* app = XGuiApplication_instance();
    /* 原生事件由 XAbstractEventDispatcher 的轮询回调统一泵空；这里不再
       重复调用平台后端，避免每次 processEvents 扫描两遍 X11/Win32 队列。 */
    if (app && app->m_platformIntegration) {
#if XWINDOW_ON && XACCESSIBLE_ON
        XPlatformAccessibility_processEvents((XPlatformAccessibility*)
            XPlatformIntegration_accessibility(app->m_platformIntegration));
#endif
    }
#else /* !XPLATFORMINTEGRATION_ON */
    (void)flags;
#endif /* XPLATFORMINTEGRATION_ON */
    XCoreApplication_processEvents(flags);
}

bool XGuiApplication_waitForEvents(int maxMilliseconds)
{
#if XPLATFORMINTEGRATION_ON
    XGuiApplication* app = XGuiApplication_instance();
    /* 对标 QEventDispatcher 的 processEvents(WaitForMoreEvents)：
       阻塞等待平台原生事件（X11 poll / Win32 MsgWaitForMultipleObjects），
       供自绘主循环避免忙轮询；返回后调用方应再调 processEvents 处理。 */
    if (app && app->m_platformIntegration) {
        return XPlatformIntegration_waitForNativeEvents(
            app->m_platformIntegration, maxMilliseconds);
    }
#else /* !XPLATFORMINTEGRATION_ON */
    (void)maxMilliseconds;
#endif /* XPLATFORMINTEGRATION_ON */
    return false;
}

/* ==================== 会话 ==================== */

bool XGuiApplication_isSessionRestored(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_isSessionRestored : false;
}

const XString* XGuiApplication_sessionId(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_sessionId : NULL;
}

const XString* XGuiApplication_sessionKey(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_sessionKey : NULL;
}

bool XGuiApplication_isSavingSession(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_isSavingSession : false;
}

void XGuiApplication_setSessionState(bool restored, bool saving,
                                     const char* id, const char* key)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    app->m_isSessionRestored = restored;
    app->m_isSavingSession = saving;
    if (app->m_sessionId) { XString_delete_base(app->m_sessionId); app->m_sessionId = NULL; }
    if (app->m_sessionKey) { XString_delete_base(app->m_sessionKey); app->m_sessionKey = NULL; }
    if (id) app->m_sessionId = XString_create_utf8(id);
    if (key) app->m_sessionKey = XString_create_utf8(key);
}

/* ==================== 同步 ==================== */

void XGuiApplication_sync(void)
{
    /* Qt 6.8: 先交付已排队应用事件，再同步窗口系统，随后交付同步产生的事件。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
#if XPLATFORMINTEGRATION_ON
    XGuiApplication* app = XGuiApplication_instance();
    /* 对标 QGuiApplication::sync：仅当平台声明 SyncState 能力时同步并冲刷。 */
    if (app && app->m_platformIntegration &&
        XPlatformIntegration_hasCapability(app->m_platformIntegration,
            XPlatformIntegrationCapability_SyncState)) {
        XPlatformIntegration_sync(app->m_platformIntegration);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
        XWindowSystemInterface_flushWindowSystemEvents(XEventLoop_AllEvents);
#endif /* XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON */
    }
#endif /* XPLATFORMINTEGRATION_ON */
}

/* ==================== 通知信号（14 个，对标 QGuiApplication 全部信号） ==================== */

void* XGuiApplication_fontDatabaseChanged_signal(XGuiApplication* app)
{
    if (!app) return (void*)(size_t)XGuiApplication_fontDatabaseChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_fontDatabaseChanged_signal, NULL);
    return (void*)(size_t)XGuiApplication_fontDatabaseChanged_signal;
}

void* XGuiApplication_screenAdded_signal(XGuiApplication* app, XScreen* screen)
{
    if (!app) return (void*)(size_t)XGuiApplication_screenAdded_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_screenAdded_signal,
                         XVarList_Create(XVar(XScreen*, screen)));
    return (void*)(size_t)XGuiApplication_screenAdded_signal;
}

void* XGuiApplication_screenRemoved_signal(XGuiApplication* app, XScreen* screen)
{
    if (!app) return (void*)(size_t)XGuiApplication_screenRemoved_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_screenRemoved_signal,
                         XVarList_Create(XVar(XScreen*, screen)));
    return (void*)(size_t)XGuiApplication_screenRemoved_signal;
}

void* XGuiApplication_primaryScreenChanged_signal(XGuiApplication* app,
                                                  XScreen* screen)
{
    if (!app) return (void*)(size_t)XGuiApplication_primaryScreenChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_primaryScreenChanged_signal,
                         XVarList_Create(XVar(XScreen*, screen)));
    return (void*)(size_t)XGuiApplication_primaryScreenChanged_signal;
}

void* XGuiApplication_lastWindowClosed_signal(XGuiApplication* app)
{
    if (!app) return (void*)(size_t)XGuiApplication_lastWindowClosed_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_lastWindowClosed_signal, NULL);
    return (void*)(size_t)XGuiApplication_lastWindowClosed_signal;
}

void* XGuiApplication_focusObjectChanged_signal(XGuiApplication* app,
                                                XObject* focusObject)
{
    if (!app) return (void*)(size_t)XGuiApplication_focusObjectChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_focusObjectChanged_signal,
                         XVarList_Create(XVar(XObject*, focusObject)));
    return (void*)(size_t)XGuiApplication_focusObjectChanged_signal;
}

void* XGuiApplication_focusWindowChanged_signal(XGuiApplication* app,
                                                XWindow* focusWindow)
{
    if (!app) return (void*)(size_t)XGuiApplication_focusWindowChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_focusWindowChanged_signal,
                         XVarList_Create(XVar(XWindow*, focusWindow)));
    return (void*)(size_t)XGuiApplication_focusWindowChanged_signal;
}

void* XGuiApplication_applicationStateChanged_signal(XGuiApplication* app,
                                                    XGuiApplicationState state)
{
    if (!app) return (void*)(size_t)XGuiApplication_applicationStateChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_applicationStateChanged_signal,
                         XVarList_Create(XVar(XGuiApplicationState, state)));
    return (void*)(size_t)XGuiApplication_applicationStateChanged_signal;
}

void* XGuiApplication_layoutDirectionChanged_signal(XGuiApplication* app,
                                                   XGuiLayoutDirection direction)
{
    if (!app) return (void*)(size_t)XGuiApplication_layoutDirectionChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_layoutDirectionChanged_signal,
                         XVarList_Create(XVar(XGuiLayoutDirection, direction)));
    return (void*)(size_t)XGuiApplication_layoutDirectionChanged_signal;
}

void* XGuiApplication_commitDataRequest_signal(XGuiApplication* app,
                                               XSessionManager* sessionManager)
{
    if (!app) return (void*)(size_t)XGuiApplication_commitDataRequest_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_commitDataRequest_signal,
                         XVarList_Create(XVar(XSessionManager*, sessionManager)));
    return (void*)(size_t)XGuiApplication_commitDataRequest_signal;
}

void* XGuiApplication_saveStateRequest_signal(XGuiApplication* app,
                                              XSessionManager* sessionManager)
{
    if (!app) return (void*)(size_t)XGuiApplication_saveStateRequest_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_saveStateRequest_signal,
                         XVarList_Create(XVar(XSessionManager*, sessionManager)));
    return (void*)(size_t)XGuiApplication_saveStateRequest_signal;
}

void* XGuiApplication_applicationDisplayNameChanged_signal(XGuiApplication* app)
{
    if (!app) return (void*)(size_t)XGuiApplication_applicationDisplayNameChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_applicationDisplayNameChanged_signal, NULL);
    return (void*)(size_t)XGuiApplication_applicationDisplayNameChanged_signal;
}

void* XGuiApplication_paletteChanged_signal(XGuiApplication* app, XPalette* palette)
{
    if (!app) return (void*)(size_t)XGuiApplication_paletteChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_paletteChanged_signal,
                         XVarList_Create(XVar(XPalette*, palette)));
    return (void*)(size_t)XGuiApplication_paletteChanged_signal;
}

void* XGuiApplication_fontChanged_signal(XGuiApplication* app, XFont* font)
{
    if (!app) return (void*)(size_t)XGuiApplication_fontChanged_signal;
    XGuiApplication_emit(app, (size_t)XGuiApplication_fontChanged_signal,
                         XVarList_Create(XVar(XFont*, font)));
    return (void*)(size_t)XGuiApplication_fontChanged_signal;
}

#endif /* XGUIAPPLICATION_ON */
