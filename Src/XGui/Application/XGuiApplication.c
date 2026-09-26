/******************************************************************************
 * @file       XGuiApplication.c
 * @brief      XGuiApplication GUI 应用类实现（对标 Qt 6.8 QGuiApplication）。
 * @details    本文件实现 XGuiApplication 的全部公开 API：
 *             - 生命周期：class_init / init / create_ex / deinit，继承
 *               XCoreApplication 全部虚槽（Event 沿用父类分发；notify
 *               重载为键事件先经平台输入上下文 filterEvent 过滤——返回
 *               true 吞掉事件，对标 QInputContext::filterEvent 遗产语义，
 *               空后端恒 false 行为不变），另重载析构清理 GUI 尾部资源；
 *             - 元信息：应用显示名 / 桌面文件名 / 平台名 / 徽标数；
 *             - 窗口注册表：allWindows / topLevelWindows / topLevelAt /
 *               addWindow / removeWindow（lastWindowClosed 与 quit 策略）；
 *             - 屏幕：primaryScreen / screens / screenAt /
 *               devicePixelRatio / screenAdded / screenRemoved /
 *               setPrimaryScreen，全部转发 XScreen 注册表；
 *             - 光标覆盖栈：setOverrideCursor / changeOverrideCursor /
 *               restoreOverrideCursor / overrideCursor（深拷贝入栈）；
 *             - 字体 / 调色板：深拷贝保存并发射 fontChanged /
 *               paletteChanged（XPALETTE_ON 守卫）；调色板随系统颜色方案
 *               （theme 深浅色）联动：colorSchemeChanged 时整体切换内置
 *               深色/浅色标准调色板（对标 Qt 6.5+ StandardPalette 深浅
 *               语义），显式 setPalette 置守卫标志（对标 AA_SetPalette）
 *               后不再被 theme 覆盖；变化时向全部顶层控件广播
 *               ApplicationPaletteChange 并触发重绘；
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
#include "XPlatformNativeWindow.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
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

#if XGUIAPPLICATION_ON

#if XWIDGET_ON
#include "XWidget.h"
#include "XWidget_Protected.h"   /* XWidget_appFocusWidget：notify 键重定向需查应用焦点控件 */
#if XWIDGET_ON && XDIALOG_ON
#include "XDialog.h"             /* XDialog_done：最后窗口关闭时收口模态 exec 阻塞循环 */
#endif
#endif /* XWIDGET_ON */

#if XPALETTE_ON && XAPPLICATION_ON
/* 顶层控件注册表（调色板变化广播用，对标 QApplication::topLevelWidgets）。 */
#include "XApplication.h"
#endif /* XPALETTE_ON && XAPPLICATION_ON */

/* ==================== 前向声明与辅助函数 ==================== */

static void VXGuiApplication_deinit(XGuiApplication* app);
static bool VXGuiApplication_notify(XObject* receiver, XEvent* event);
#if XWIDGET_ON
/** @brief 惰性注册"属性设置完成"钩子（定义见应用程序属性一节）。 */
static void guiApp_ensureAttributeHookInstalled(void);
#endif /* XWIDGET_ON */

/*
 * XCoreApplication 只保存一个基类指针，不能仅凭它的地址或一个非基类
 * vtable 就判断对象是否带有 XGuiApplication 尾部。该标记只在本类 init
 * 完成基类初始化后建立，并在本类析构开始时清除，因此读取它不会触碰
 * 未初始化的调用方栈对象，也不会把未知的 XCoreApplication 派生类当成
 * GUI 应用处理。
 */
static XGuiApplication* g_guiApplication = NULL;

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
    XCopy(copy, icon);
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
    /* WSI 注入的区域设置（BCP 47）优先：按语言主子标签识别 RTL 族
     * （阿拉伯语/希伯来语/波斯语/乌尔都语等），对标 QLocale 布局方向
     * 查询语义；未注入时退回平台输入上下文。 */
    if (app && app->m_platformLocale[0]) {
        static const char* const rtlLanguages[] = {
            "ar", "he", "fa", "ur", "ps", "syr", "dv", "ckb"
        };
        size_t i;
        for (i = 0; i < sizeof(rtlLanguages) / sizeof(rtlLanguages[0]); ++i) {
            const char* lang = rtlLanguages[i];
            size_t len = 0;
            while (lang[len] != '\0') ++len;
            if (XStrncmp(app->m_platformLocale, lang, len) == 0 &&
                (app->m_platformLocale[len] == '\0' ||
                 app->m_platformLocale[len] == '-' ||
                 app->m_platformLocale[len] == '_'))
                return XGuiLayoutDirection_RightToLeft;
        }
    }
#if XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON
    {
        XPlatformInputContext* inputContext;
        if (app && app->m_platformIntegration) {
            inputContext = XPlatformIntegration_inputContext(
                app->m_platformIntegration);
            if (inputContext && XPlatformInputContext_inputDirection(inputContext) ==
                    XInputMethodLayoutDirection_RightToLeft)
                return XGuiLayoutDirection_RightToLeft;
        }
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
        XCopy(copy, cursor);
    return copy;
}
#endif /* XCURSOR_ON */

#if XINPUTMETHOD_ON || (XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON)
/** @brief 解析当前生效的平台输入上下文（对标 Qt 的 qApp 输入上下文解析）。 */
static XPlatformInputContext* XGuiApplication_activeInputContext(
        XGuiApplication* app)
{
    XPlatformInputContext* context = NULL;
#if XINPUTMETHOD_ON
    /* QInputMethod 持有当前 platformContext（创建时与集成层双向绑定；
     * 平台集成可用 XInputMethod_setPlatformContext 注入真实输入上下文）。 */
    XInputMethod* inputMethod = app ? XGuiApplication_inputMethod() : NULL;
    if (inputMethod)
        context = XInputMethod_platformContext(inputMethod);
#endif /* XINPUTMETHOD_ON */
#if XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON
    if (!context && app && app->m_platformIntegration)
        context = XPlatformIntegration_inputContext(app->m_platformIntegration);
#else
    (void)app;
#endif /* XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON */
    return context;
}
#endif /* XINPUTMETHOD_ON || (XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON) */

/** @brief notify 虚槽：按键派发前先经输入上下文过滤（Qt4 遗产语义）。 */
static bool VXGuiApplication_notify(XObject* receiver, XEvent* event)
{
#if XINPUTMETHOD_ON || (XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON)
    /* 对标 Qt4 QApplication::notify 的 QInputContext::filterEvent 遗产语义
     * （Qt6 由 platformContext 过滤承接）：KEY_PRESS/KEY_RELEASE 在派发到
     * 窗口/控件之前先问输入上下文，返回 true 即吞掉该事件。
     * 平台 XIM（XFilterEvent）与 DBus portal（fcitx）在 WSI 键事件入口
     * 之前已自行消费组合键，因此本钩子位于 IME 处理之后、控件派发之前，
     * 两条过滤路径互不打架；空后端默认恒 false，派发行为不变。 */
    if (event && (event->type == XEVENT_TYPE_KEY_PRESS ||
                  event->type == XEVENT_TYPE_KEY_RELEASE)) {
#if XWIDGET_ON
        /* 对标 Qt QGuiApplicationPrivate::processKeyEvent：平台键事件
         * 交付给应用焦点窗口（focusWindow()），而不是发起投递的原生
         * 窗口。XGui 单原生窗口模型下，对话框/消息框是应用内 XWindow
         * （不占原生窗口）：对话框 open() 抢焦点后，焦点控件的顶层窗
         * 口≠原生窗口对象，若仍按原生窗口投递，活动对话框永远收不到
         * 任何按键（真键盘路径 Esc/Enter 全部无响应——实测 demo 消息
         * 框开箱后按 Esc/Return 均无效）。焦点控件为空或其顶层就在
         * 原生窗口树内（与 receiver 同一对象）时保持原投递对象。 */
        {
            XWidget* focusWidget = XWidget_appFocusWidget();
            if (focusWidget) {
                XWidget* top = focusWidget->m_isWindow
                    ? focusWidget : XWidget_topLevelWidget(focusWidget);
                /* Popup 豁免（页6 文件对话框组合框弹层键盘根修，
                   2026-09-25）：弹层（XComboPopupView 等）是独立顶层
                   Popup 原生窗，平台按键按 X 焦点/键盘抓取本就投递到
                   弹层桥（receiver）；弹层控件从不成为应用焦点控件
                   （弹层不 setFocus），若仍按单原生窗口模型把键重定向
                   到焦点控件顶层，模态对话框内弹层的 Esc/方向键/Return
                   将永远到不了弹层（实测 :120：X 焦点已在弹层 0x200004，
                   Esc 仍重定向进对话框焦点链把对话框 reject）。对标 Qt：
                   popup 打开期间键事件交付 popup（QApplicationPrivate
                   popup 分支），收层后焦点回交宿主链不受影响。 */
                if (top && top->m_windowHandle &&
                    (XObject*)top != receiver &&
                    XWindow_type((XWindow*)receiver) !=
                        XWindowType_Popup)
                    receiver = (XObject*)top->m_windowHandle;
            }
        }
#endif /* XWIDGET_ON */
        XGuiApplication* app = XGuiApplication_instance();
        XPlatformInputContext* inputContext =
            XGuiApplication_activeInputContext(app);
        if (inputContext &&
            XPlatformInputContext_filterEvent(inputContext, event))
            return true; /* 输入法消费：事件不再下发。 */
    }
#endif /* XINPUTMETHOD_ON || (XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON) */
    return XClass_Parent(XCoreApplication, EXCoreApplication_Notify,
                         bool (*)(XObject*, XEvent*))(receiver, event);
}

/* ==================== 类初始化与生命周期 ==================== */

XVtable* XGuiApplication_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGuiApplication)
    XVTABLE_INHERIT_XCLASS(XCoreApplication);
    /* 重载 notify：键事件先经输入上下文过滤；event 槽沿用父类分发。 */
    XVTABLE_OVERLOAD_DEFAULT(EXCoreApplication_Notify, VXGuiApplication_notify);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGuiApplication_deinit);
    return XVTABLE_DEFAULT;
}

XGuiApplication* XGuiApplication_instance(void)
{
    XCoreApplication* core = XCoreApplication_instance();
    if (!core || g_guiApplication != (XGuiApplication*)core)
        return NULL;
    return g_guiApplication;
}

XGuiApplication* XGuiApplication_create_ex(XMemoryType memory, int argc,
                                           char** argv)
{
    XGuiApplication* app;
    if (XCoreApplication_instance())
        return XGuiApplication_instance();

    app = (XGuiApplication*)XMemory_malloc(sizeof(XGuiApplication), memory);
    if (!app) return NULL;

    XGuiApplication_init(app, argc, argv);
    /* init 是 void API；若初始化期间单例被其它路径抢先建立，不能把
       尚未初始化的对象当成成功结果返回，也不能对它调用 XClass_deinit。 */
    if (XCoreApplication_instance() != (XCoreApplication*)app ||
        XGuiApplication_instance() != app) {
        XMemory_free(app, memory);
        return NULL;
    }
    Set_Class_Memory(app, memory);
    Set_Class_IsHeap(app, true);
    return app;
}

void XGuiApplication_init(XGuiApplication* app, int argc, char** argv)
{
    XCoreApplication* active;
    XMemory* previousMemory = NULL;
    bool previousIsHeap = false;
    bool reinitialize = false;
    if (app == NULL) return;

    /* XClass 的 vtable 不能用于探测未初始化的栈对象。只有全局单例
       明确指向当前地址时，才可确认这里是一次已初始化对象的重建；
       其它活动应用则保持单例不变并拒绝本次初始化。 */
    active = XCoreApplication_instance();
    if (active && active != (XCoreApplication*)app)
        return;
    if (active == (XCoreApplication*)app) {
        /* XGuiApplication_instance() 只读取已经由 g_app 证明有效的对象，
           同时拒绝把一个普通 XCoreApplication 当成 GUI 尾部来扩展。 */
        if (!XGuiApplication_instance())
            return;
        previousMemory = Class_Memory(app);
        previousIsHeap = Class_IsHeap(app);
        reinitialize = true;
        /* 派生的 XApplication 也通过当前 vtable 完整清理自己的尾部。 */
        XClass_deinit_base((XClass*)app);
    }

    /* 先清空 GUI 尾部字段，再由基类初始化统一清零 XGuiApplication 中的
       XObject/XCoreApplication 部分，最后套用本类虚函数表。 */
    XMemset(((XCoreApplication*)app) + 1, 0,
           sizeof(XGuiApplication) - sizeof(XCoreApplication));
    XCoreApplication_init((XCoreApplication*)app, argc, argv);
    XClassSetVtable(app, XGuiApplication);
#if XWIDGET_ON
    /* GUI 应用实例建立即注册属性钩子：此后应用直调基类
       XCoreApplication_setAttribute 设置 GUI 属性（如属性 12）与经
       XGuiApplication_setAttribute 包装层行为一致（批次十七报备项收敛）。 */
    guiApp_ensureAttributeHookInstalled();
#endif /* XWIDGET_ON */
    if (reinitialize) {
        /* 保留已初始化对象的完整所有权信息；首次初始化则使用
           XCoreApplication/XClass 建立的默认内存方法和非堆标记。 */
        Class_Memory(app) = previousMemory ? previousMemory :
            XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
        Class_IsHeap(app) = previousIsHeap;
    }

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
    /* 调色板随颜色方案联动默认开启（对标 Qt 6.5 深浅色跟随）；
       显式调色板标志保持 memset 的 false（对标 AA_SetPalette 未置位）。 */
    app->m_paletteSchemeFollow = true;
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

    /* 所有成员已经处于可用状态后再发布 GUI 单例，避免初始化回调看到
       只有 XCoreApplication 已登记、GUI 尾部尚未完成的中间状态。 */
    g_guiApplication = app;
}

static void VXGuiApplication_deinit(XGuiApplication* app)
{
    size_t i;
    size_t n;
    if (!app || g_guiApplication != app)
        return;

    /* 先摘除 GUI 单例标记，防止析构回调重入时再次看到半析构对象。 */
    g_guiApplication = NULL;
    if (((XObject*)app)->was_deleted)
        return;

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
    app->m_focusWindow = NULL;
    app->m_modalWindow = NULL;
    app->m_focusObject = NULL;
#if XINPUTMETHOD_ON
    /* 输入法先于集成层释放（其绑定输入上下文由集成层拥有）。 */
    if (app->m_inputMethod) {
#if XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON
        if (app->m_platformIntegration) {
            XPlatformInputContext* context =
                XPlatformIntegration_inputContext(app->m_platformIntegration);
            if (context)
                XPlatformInputContext_setInputMethod(context, NULL);
        }
#endif /* XPLATFORMINTEGRATION_ON && XPLATFORMINPUTCTX_ON */
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

/* ==================== 应用程序属性（GUI 语义接线） ==================== */

#if XWIDGET_ON
/*
 * AA_SynthesizeMouseForUnhandledTouchEvents(=12) 的三态接线（显式开 /
 * 显式关 / 未设置）。取舍：XCoreApplication 的 XBitArray 只有位值，
 * 无法区分「显式关」与「未设置」，而该属性在 Qt 6 的默认值是开；
 * 转发体收敛进"属性设置完成"钩子（批次十七报备项收敛）：基类
 * XCoreApplication_setAttribute 写完位数组后回调本文件注册的钩子，
 * GUI 包装层与直调基类两条路径行为一致（Qt 对 GUI 属性的转发同样
 * 位于 QGuiApplication 私有层而非 QCoreApplication）。用静态 bool
 * 记录是否被显式设置：
 *   - 未显式设置：不触碰框架开关（XWidget.c 的 g_touchMouseSynthEnabled
 *     保持默认 true，即 Qt 默认开语义），testAttribute 按默认开回答；
 *   - 显式设置：转发到框架开关 XWidget_setTouchMouseSynthesisEnabled
 *     （false 关、true 开），testAttribute 回读基类位值。
 * 框架开关是运行时单一事实源；应用也可绕过属性直接调
 * XWidget_setTouchMouseSynthesisEnabled（此时属性域查询不受影响，
 * 与 Qt 只存在应用级属性的差异已被 XWidget.h 注释登记）。
 */
static bool g_synthMouseAttrExplicitlySet = false;
static bool g_attributeHookInstalled = false;

/** @brief 属性设置完成钩子体：属性 12 显式设置时同步框架开关（幂等）。 */
static void guiApp_attributeHook(XCoreApplicationAttribute attribute, bool on)
{
    if (attribute ==
            XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_MOUSE_FOR_UNHANDLED_TOUCH_EVENTS) {
        g_synthMouseAttrExplicitlySet = true;
        XWidget_setTouchMouseSynthesisEnabled(on);
    }
}

/** @brief 惰性注册钩子（幂等）；包装层与实例初始化双入口保证任何
 *         触达 GUI 属性语义的路径都已完成注册。 */
static void guiApp_ensureAttributeHookInstalled(void)
{
    if (!g_attributeHookInstalled) {
        g_attributeHookInstalled = true;
        XCoreApplication_setAttributeHook(guiApp_attributeHook);
    }
}

void XGuiApplication_setAttribute(XCoreApplicationAttribute attribute, bool on)
{
    guiApp_ensureAttributeHookInstalled();
    XCoreApplication_setAttribute(attribute, on);
    /* 基类在无应用实例时提前返回（不会触发钩子）；此处兜底保证
       预创建期的静态设置（对标 QGuiApplication::setAttribute 静态期
       可用性）仍立即转发框架开关。有实例时基类钩子已转发，本分支
       不走（避免重复）。 */
    if (!XCoreApplication_instance())
        guiApp_attributeHook(attribute, on);
}

bool XGuiApplication_testAttribute(XCoreApplicationAttribute attribute)
{
    if (attribute ==
            XCORE_APPLICATION_ATTRIBUTE_SYNTHESIZE_MOUSE_FOR_UNHANDLED_TOUCH_EVENTS &&
        !g_synthMouseAttrExplicitlySet)
        return true; /* 未设置：按 Qt 6 默认值（开）回答。 */
    return XCoreApplication_testAttribute(attribute);
}
#endif /* XWIDGET_ON */

/* ==================== 应用元信息 ==================== */

void XGuiApplication_setApplicationDisplayName(const XString* name)
{
    XGuiApplication* app = XGuiApplication_instance();
    const XString* oldDisplay;
    XString* replacement = NULL;
    bool changed;
    if (!app) return;
    oldDisplay = XGuiApplication_applicationDisplayName();
    changed = (name != oldDisplay);
    if (name && oldDisplay)
        changed = !XString_equals(oldDisplay, name, XChar_CaseSensitive);
    if (!name && !oldDisplay) changed = false;
    if (!changed && ((name == NULL) == (app->m_displayName == NULL))) return;
    if (name) {
        replacement = XGuiApplication_cloneString(name);
        if (!replacement) return;
    }
    if (app->m_displayName) { XString_delete_base(app->m_displayName); app->m_displayName = NULL; }
    app->m_displayName = replacement;
    if (changed) XGuiApplication_applicationDisplayNameChanged_signal(app);
}

void XGuiApplication_setApplicationDisplayName_2(const char* name)
{
    XString* tmp = NULL;
    if (name) {
        tmp = XString_create_utf8(name);
        if (!tmp) return;
    }
    XGuiApplication_setApplicationDisplayName(tmp);
    if (tmp) XString_delete_base(tmp);
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
    XString* replacement;
    if (!app) return;
    replacement = XGuiApplication_cloneString(name);
    if (name && !replacement) return;
    if (app->m_desktopFileName) { XString_delete_base(app->m_desktopFileName); app->m_desktopFileName = NULL; }
    app->m_desktopFileName = replacement;
}

void XGuiApplication_setDesktopFileName_2(const char* name)
{
    XString* tmp = NULL;
    if (name) {
        tmp = XString_create_utf8(name);
        if (!tmp) return;
    }
    XGuiApplication_setDesktopFileName(tmp);
    if (tmp) XString_delete_base(tmp);
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
        if (w && !XVector_push_back_1_base(out, &w)) {
            XVector_delete_base((XClass*)out);
            return NULL;
        }
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
        if (XWindow_parent(w, XWindowAncestor_ExcludeTransients) == NULL &&
            !XVector_push_back_1_base(out, &w)) {
            XVector_delete_base((XClass*)out);
            return NULL;
        }
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

    /* 移除最后一个可见顶层窗口时发射 lastWindowClosed；若启用退出策略
       再请求退出。根因(R-97)：此前仅在注册表完全清空时发射，不查剩余
       窗口可见性——隐藏的顶层窗口会永久阻断信号与退出策略。对标 Qt
       QGuiApplicationPrivate::shouldQuit：按「是否还有可见顶层」判定。 */
    if (wasTopLevel) {
        /* Do not re-enter topLevelWindows() here: all entries are borrowed,
           and callers may be in a nested destruction path. */
        bool anyVisibleTopLevel = false;
        for (size_t i = 0; i < n; ++i) {
            XWindow* remaining =
                XVector_At_Base(app->m_windows, (int64_t)i, XWindow*);
            if (remaining && XWindow_isTopLevel(remaining) &&
                XWindow_isVisible(remaining)) {
                anyVisibleTopLevel = true;
                break;
            }
        }
        if (!anyVisibleTopLevel) {
            XGuiApplication_lastWindowClosed_signal(app);
            if (app->m_quitOnLastWindowClosed) {
#if XWIDGET_ON && XDIALOG_ON
                /* 子控件对话框 exec 兜底：XDialog_exec 的阻塞是裸
                   while+processEvents（非 XEventLoop），quit 标记的循环
                   退出它感知不到。最后窗口关闭（窗面已随之销毁）时若仍
                   有应用模态对话框在 exec，先 done() 结束其阻塞循环，控
                   制权回到主循环后即可感知退出——否则主窗毁后进程滞留在
                   死对话框的模态循环（实测：对话框页打开输入对话框后关
                   主窗，窗面 0 children 而进程 S 态滞留）。对标 Qt：
                   QDialog::exec 是真 QEventLoop，QCoreApplication::exit
                   会退出全部嵌套循环（qcoreapplication.cpp，exit 遍历
                   d->eventLoops）；子控件对话框因无独立循环，只能经
                   done() 收敛到同一效果（对话框按 reject 关闭，应用退
                   出）——与 Qt「模态框随宿主窗体终结」的可见行为一致。 */
                XWidget* xdlgModal = XWidget_applicationModalWidget();
                if (xdlgModal)
                    XDialog_done((XDialog*)xdlgModal, 0);
#endif
                XCoreApplication_quit();
            }
        }
    }
}
#endif /* XWINDOW_ON */

/* ==================== 图标 ==================== */

void XGuiApplication_setWindowIcon(const XIcon* icon)
{
    XGuiApplication* app = XGuiApplication_instance();
    XIcon* replacement;
    if (!app) return;
    replacement = XGuiApplication_cloneIcon(icon);
    if (icon && !replacement) return;
    if (app->m_windowIcon) XIcon_delete_base(app->m_windowIcon);
    app->m_windowIcon = replacement;
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
    XVector* list;
    XScreen* hit = NULL;
    if (!pos) return NULL;
    list = XScreen_screens();
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
    if (!XVector_push_back_1_base(app->m_overrideStack, &copy))
        XCursor_delete_base(copy);
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
    XFont* replacement = NULL;
    if (!app) return;
    if (font) {
        replacement = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      NULL, -1, -1, false);
        if (!replacement) return;
        XCopy(replacement, font);
    }
    if (app->m_font) XFont_delete_base(app->m_font);
    app->m_font = replacement;
    XGuiApplication_fontChanged_signal(app, replacement);
}

XFont* XGuiApplication_font(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    XFont* copy;
    if (!app || !app->m_font) return NULL;
    copy = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, -1, -1, false);
    if (!copy) return NULL;
    XCopy(copy, app->m_font);
    return copy;
}

#if XPALETTE_ON
/* ==================== 调色板 × 颜色方案联动（对标 Qt 6.5+ 深浅色通道） ==================== */

/**
 * @brief      向全部顶层控件广播调色板变化并触发重绘。
 * @details    对标 QApplicationPrivate::handlePaletteChanged：向顶层控件
 *             发送 ApplicationPaletteChange 事件并请求重绘。控件层
 *             XWidget_palette() 在每次绘制时实时解析应用调色板
 *             （无显式 setPalette 的控件随应用整体换肤），因此更新
 *             顶层控件脏区即完成全树重绘；事件本身供控件级监听者
 *             刷新内部调色板缓存（当前无消费者，与 Qt 事件面一致）。
 *             仅顶层粒度广播为最小实现（Qt 逐控件发送，本框架单窗口
 *             后备存储模型下顶层脏区已覆盖整棵控件树）。
 */
static void XGuiApplication_broadcastTopLevelPaletteChanged(void)
{
#if XAPPLICATION_ON && XWIDGET_ON
    XVector* topLevel;
    size_t i;
    size_t n;
    topLevel = XApplication_topLevelWidgets();
    if (!topLevel) return;
    n = XVector_size_base((const XContainer*)topLevel);
    for (i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(topLevel, (int64_t)i, XWidget*);
        if (!w) continue;
        {
            /* 栈区事件：sendEvent 不取得所有权（对标 Qt sendEvent 语义）。 */
            XEvent paletteEvent;
            XEvent_init(&paletteEvent, XEVENT_TYPE_APPLICATION_PALETTE_CHANGE);
            XGuiApplication_sendEvent((XObject*)w, &paletteEvent);
        }
        XWidget_update(w);
    }
    /* 应用级调色板广播的保留层批量失效（树中保留层不随顶层 update
       自动失效——缓存 blit 跳过 paintEvent 派发，需显式命中）。 */
#if XWIDGET_ON
    XWidget_invalidateAllRetainedLayers();
#endif
    XVector_delete_base((XClass*)topLevel);
#endif /* XAPPLICATION_ON && XWIDGET_ON */
}

#if XSTYLEHINTS_ON
/**
 * @brief      将应用调色板对齐到指定颜色方案的标准调色板（内部）。
 * @details    深色 scheme 用内置深色标准组、浅色/未知用浅色标准组
 *             （对标 Qt 6.8 qt_fusionPalette 的 darkAppearance 分支：
 *             Unknown 与 Light 同走浅色路径）。
 * @return     调色板发生变化返回 true。
 */
static bool XGuiApplication_applyStandardPaletteForScheme(
        XGuiApplication* app, XStyleHintsColorScheme scheme)
{
    XPalette next;
    if (scheme == XStyleHintsColorScheme_Dark)
        XPalette_init_dark(&next);
    else
        XPalette_init_default(&next);
    if (XPalette_isEqual(&next, &app->m_palette))
        return false;
    XPalette_copy(&app->m_palette, &next);
    return true;
}

/** @brief colorSchemeChanged 联动槽：深浅色翻转时切换标准调色板并广播。
 *         对标 QGuiApplicationPrivate::handleThemeChanged → updatePalette：
 *         平台主题颜色方案变化时应用调色板按 StandardPalette 深浅语义
 *         重建，再经 handlePaletteChanged 传播。两道守卫（对标 Qt 的
 *         AA_SetPalette 与嵌入主题自管理扩展）任一生效即不覆盖：
 *         - m_paletteExplicitlySet：用户显式 setPalette 过（AA_SetPalette）；
 *         - m_paletteSchemeFollow 为 false：联动被显式关闭。 */
static void guiApp_colorSchemeChangedSlot(XObject* sender, XVarList* args)
{
    XGuiApplication* app = XGuiApplication_instance();
    XStyleHints* hints = (XStyleHints*)sender;
    XStyleHintsColorScheme scheme;
    (void)args;
    if (!app) return;
    if (app->m_paletteExplicitlySet || !app->m_paletteSchemeFollow)
        return;
    /* XStyleHints_setColorScheme 先落值再发射信号，单线程模型下读回
       单例当前值即本次变化的方案（对 args 为空的程序化发射同样成立）。 */
    scheme = XStyleHints_colorScheme(hints);
    if (XGuiApplication_applyStandardPaletteForScheme(app, scheme)) {
        XGuiApplication_paletteChanged_signal(app, &app->m_palette);
        XGuiApplication_broadcastTopLevelPaletteChanged();
    }
}
#endif /* XSTYLEHINTS_ON */

void XGuiApplication_setPalette(const XPalette* palette)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (palette)
        XPalette_copy(&app->m_palette, palette);
    else
        XPalette_init_default(&app->m_palette);
    /* 对标 Qt::AA_SetPalette：QGuiApplicationPrivate::setPalette 按
       解析掩码置位该属性；本值类型无逐角色掩码，退化为整盘显式标志。
       NULL 重置同为显式操作：重置为浅色标准调色板并冻结后续 theme
       联动，语义可预期。 */
    app->m_paletteExplicitlySet = true;
    XGuiApplication_paletteChanged_signal(app, &app->m_palette);
    XGuiApplication_broadcastTopLevelPaletteChanged();
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

bool XGuiApplication_paletteColorSchemeFollowEnabled(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_paletteSchemeFollow : true;
}

void XGuiApplication_setPaletteColorSchemeFollowEnabled(bool on)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app || app->m_paletteSchemeFollow == on) return;
    app->m_paletteSchemeFollow = on;
#if XSTYLEHINTS_ON
    /* 重新开启且未显式 setPalette 时立即对齐当前方案：关闭期间 scheme
       可能已翻转（对标 handleThemeChanged 在主题变化时 updatePalette
       的即时性）；显式标志生效时保持用户调色板不动。 */
    if (on && !app->m_paletteExplicitlySet) {
        XStyleHints* hints = XGuiApplication_styleHints();
        XStyleHintsColorScheme scheme = hints
            ? XStyleHints_colorScheme(hints)
            : XStyleHintsColorScheme_Unknown;
        if (XGuiApplication_applyStandardPaletteForScheme(app, scheme)) {
            XGuiApplication_paletteChanged_signal(app, &app->m_palette);
            XGuiApplication_broadcastTopLevelPaletteChanged();
        }
    }
#endif /* XSTYLEHINTS_ON */
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

/* ==================== 平台区域设置 ==================== */

void XGuiApplication_setPlatformLocaleUtf8(const char* localeUtf8)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return;
    if (localeUtf8 && localeUtf8[0]) {
        size_t i;
        /* 缓冲定长 64 字节：BCP 47 名称很短，越界截断即可，不引入分配。 */
        for (i = 0; i < sizeof(app->m_platformLocale) - 1 &&
                    localeUtf8[i] != '\0'; ++i)
            app->m_platformLocale[i] = localeUtf8[i];
        app->m_platformLocale[i] = '\0';
    } else {
        /* NULL/空串等价清除：Auto 方向解析退回平台输入上下文。 */
        app->m_platformLocale[0] = '\0';
    }
    /* 请求方向为 Auto 时按新区域重解析有效方向（值变化时内部发射
       layoutDirectionChanged）；显式 LTR/RTL 请求在该函数内部直接
       短路，不受区域设置影响。 */
    XGuiApplication_notifyPlatformInputDirectionChanged();
}

const char* XGuiApplication_platformLocaleUtf8(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    return app ? app->m_platformLocale : NULL;
}

/* ==================== 样式提示 / 剪贴板 / 输入法 ==================== */

#if XSTYLEHINTS_ON
XStyleHints* XGuiApplication_styleHints(void)
{
    XGuiApplication* app = XGuiApplication_instance();
    if (!app) return NULL;
    if (!app->m_styleHints) {
        app->m_styleHints = XStyleHints_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#if XPALETTE_ON
        /* theme×调色板联动入口：平台 handleThemeChanged（WSI）→
           XStyleHints_setColorScheme → colorSchemeChanged → 联动槽完成
           深浅标准调色板切换（对标 QGuiApplicationPrivate::
           handleThemeChanged → updatePalette 链）。传 NULL 取信号标识，
           不触发发射（信号函数 self 为 NULL 时仅返回地址）。 */
        if (app->m_styleHints)
            XObject_connect_2((XObject*)app->m_styleHints,
                              (size_t)XStyleHints_colorSchemeChanged_signal(
                                  NULL, XStyleHintsColorScheme_Unknown),
                              guiApp_colorSchemeChangedSlot);
#endif /* XPALETTE_ON */
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
        XPlatformNativeWindow_installClipboardBackend();
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
#if XWIDGET_ON
            /* 对标 Qt：QInputMethod 通过向焦点对象自动发送
               QInputMethodQueryEvent 获取 ImCursorRectangle 等属性。这里把
               内置桥接 handler 注册为默认查询回调，将查询转发给焦点控件的
               XWidget_inputMethodQuery 虚槽；集成方仍可经 setQueryHandler
               覆盖。生命周期：handler 为静态函数、userData 为 NULL，输入法
               对象随应用析构一并删除，不留悬挂回调。 */
            XInputMethod_setQueryHandler(app->m_inputMethod,
                                         XInputMethod_defaultQueryHandler,
                                         NULL);
#endif /* XWIDGET_ON */
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
    return XPlatformNativeInterface_platformFunction_2(ni, functionName);
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
                                     const XString* id, const XString* key)
{
    XGuiApplication* app = XGuiApplication_instance();
    XString* replacementId = NULL;
    XString* replacementKey = NULL;
    if (!app) return;

    if (id) {
        replacementId = XString_create_copy(id);
        if (!replacementId) return;
    }
    if (key) {
        replacementKey = XString_create_copy(key);
        if (!replacementKey) {
            if (replacementId) XString_delete_base(replacementId);
            return;
        }
    }

    app->m_isSessionRestored = restored;
    app->m_isSavingSession = saving;
    if (app->m_sessionId) { XString_delete_base(app->m_sessionId); app->m_sessionId = NULL; }
    if (app->m_sessionKey) { XString_delete_base(app->m_sessionKey); app->m_sessionKey = NULL; }
    app->m_sessionId = replacementId;
    app->m_sessionKey = replacementKey;
}

void XGuiApplication_setSessionState_2(bool restored, bool saving,
                                       const char* id, const char* key)
{
    XString* tmpId = NULL;
    XString* tmpKey = NULL;
    if (id) {
        tmpId = XString_create_utf8(id);
        if (!tmpId) return;
    }
    if (key) {
        tmpKey = XString_create_utf8(key);
        if (!tmpKey) { if (tmpId) XString_delete_base(tmpId); return; }
    }
    XGuiApplication_setSessionState(restored, saving, tmpId, tmpKey);
    if (tmpKey) XString_delete_base(tmpKey);
    if (tmpId) XString_delete_base(tmpId);
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
