/******************************************************************************
 * @file       XPlatformIntegration.c
 * @brief      XPlatformIntegration 平台集成层类实现（对标 Qt 6.8
 *             QPlatformIntegration 全部公共 API）。
 * @details    本文件实现嵌入式平台集成单后端：
 *             - 生命周期：class_init / init / create_ex / deinit，init 时
 *               创建并双向绑定 XPlatformNativeInterface 与
 *               XPlatformInputContext 子对象；
 *             - 能力位：默认开启嵌入式能力（线程化位图/窗口遮罩/多窗口/
 *               应用状态/非全屏/窗口管理/窗口激活/同步/应用图标/绘制事件），
 *               OpenGL/RHI/外部窗口按 Drive 运行时能力动态置位；
 *             - 窗口句柄工厂：createPlatformWindow 幂等登记并挂接
 *               XWindow 平台句柄；createForeignWindow 在 Linux X11/Windows
 *               Win32 上挂接外部原生窗口，其它后端返回 NULL；
 *               createPlatformBackingStore 转发 Drive 平台后端
 *               （Linux/Windows 提供实现，其它平台回落 NULL）；
 *               createPlatformOpenGLContext / createPlatformVulkanInstance
 *               通过 XPlatformGraphics 转发 Drive 的 GLX/WGL/Vulkan 后端；
 *             - 子单例：nativeInterface / inputContext / clipboard /
 *               styleHints；fontDatabase / drag / services / keyMapper 恒 NULL；
 *               accessibility 通过 XPlatformAccessibility 转发到 Drive；
 *             - 样式提示：styleHint() 映射 XStyleHints 状态或嵌入式默认值，
 *               返回新建 XVariant；
 *             - 平台元信息与行为：themeNames 含 "embedded"、sync 空实现、
 *               beep 恒 false、quit 转发 XCoreApplication_quit、
 *               setApplicationIcon/setApplicationBadge 进程内存值。
 *             模块不依赖任何平台 API，具体系统窗口栈/图形栈/输入法框架
 *             均由 Drive 后端实现。
 * @note       模块总开关 XPLATFORMINTEGRATION_ON 定义于 XGuiConfig.h；
 *             置 0 时本文件实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformIntegration.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include "XCoreApplication.h"
#include "XEvent.h"
#include "XAbstractEventDispatcher.h"
#include "XThreadData.h"
#include "XPlatformGraphics.h"
#include "XPlatformFontDatabase.h"
#include "XPlatformTheme.h"
#include "XPlatformServices.h"
#include "XPlatformDrag.h"
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XPlatformAccessibility.h"
#endif
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */
#include <string.h>

#if XPLATFORMINTEGRATION_ON

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
#include "XPlatformBackingStore.h"
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

/** @brief XPlatformIntegration 私有数据块。 */
struct XPlatformIntegrationPrivate
{
    XPlatformNativeInterface* m_nativeInterface; /**< 平台原生接口（拥有）。 */
    XPlatformInputContext*    m_inputContext;    /**< 平台输入上下文（拥有）。 */
#if XWINDOW_ON && XACCESSIBLE_ON
    XPlatformAccessibility* m_accessibility; /**< 辅助功能桥接（拥有）。 */
#endif
    XClipboard*  m_clipboard;    /**< 剪贴板单例（借用，XGuiApplication 注入）。 */
    XStyleHints* m_styleHints;   /**< 样式提示单例（借用，XGuiApplication 注入）。 */
    XString* m_themeName;        /**< 平台主题名（拥有，恒 "embedded"）。 */
    XPlatformFontDatabase* m_fontDatabase; /**< 平台字体家族快照（拥有）。 */
    XPlatformTheme* m_theme;     /**< 平台主题快照（拥有）。 */
    XPlatformServices* m_services; /**< 桌面服务访问器（拥有）。 */
    XPlatformDrag* m_drag;          /**< 出站拖放对象（拥有）。 */
    XIcon*   m_applicationIcon;  /**< 应用图标（拥有深拷贝）。 */
    int64_t  m_badgeNumber;      /**< 应用徽标数。 */
    uint64_t m_capabilities;     /**< 能力位掩码。 */
    XVector* m_platformWindows;  /**< 平台窗口句柄登记表（拥有元素）。 */
};

/** @brief 能力位转位掩码：枚举值从 1 起，能力位 n 对应第 n-1 位。 */
static uint64_t capabilityBit(XPlatformIntegrationCapability cap)
{
    if (cap < 1) return 0;
    return (uint64_t)1u << (cap - 1);
}

/** @brief 深拷贝图标；输入为 NULL 时返回 NULL。 */
static XIcon* integration_cloneIcon(const XIcon* icon)
{
    XIcon* copy;
    if (!icon) return NULL;
    copy = XIcon_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XIcon_copy_base(copy, icon);
    return copy;
}

static void VXPlatformIntegration_deinit(XPlatformIntegration* self)
{
    size_t i;
    size_t n;
    if (!self) return;
    if (self->m_data) {
        /* 平台窗口句柄由本层拥有，逐个释放。 */
        if (self->m_data->m_platformWindows) {
            n = XVector_size_base((const XContainer*)self->m_data->m_platformWindows);
            for (i = 0; i < n; ++i) {
                XPlatformWindow** p = (XPlatformWindow**)XVector_at_base(
                        self->m_data->m_platformWindows, (int64_t)i);
                if (p && *p) XPlatformWindow_delete_base(*p);
            }
            XVector_delete_base((XClass*)self->m_data->m_platformWindows);
            self->m_data->m_platformWindows = NULL;
        }
#if XPLATFORMINPUTCTX_ON
        if (self->m_data->m_inputContext) {
            XPlatformInputContext_delete_base(self->m_data->m_inputContext);
            self->m_data->m_inputContext = NULL;
        }
#endif /* XPLATFORMINPUTCTX_ON */
        if (self->m_data->m_nativeInterface) {
            XPlatformNativeInterface_delete_base(self->m_data->m_nativeInterface);
            self->m_data->m_nativeInterface = NULL;
        }
#if XWINDOW_ON && XACCESSIBLE_ON
        if (self->m_data->m_accessibility) {
            XPlatformAccessibility_delete_base(self->m_data->m_accessibility);
            self->m_data->m_accessibility = NULL;
        }
#endif
        if (self->m_data->m_themeName) {
            XString_delete_base(self->m_data->m_themeName);
            self->m_data->m_themeName = NULL;
        }
        if (self->m_data->m_fontDatabase) {
            XPlatformFontDatabase_destroy(self->m_data->m_fontDatabase);
            self->m_data->m_fontDatabase = NULL;
        }
        if (self->m_data->m_theme) {
            XPlatformTheme_destroy(self->m_data->m_theme);
            self->m_data->m_theme = NULL;
        }
        if (self->m_data->m_services) {
            XPlatformServices_destroy(self->m_data->m_services);
            self->m_data->m_services = NULL;
        }
        if (self->m_data->m_drag) {
            XPlatformDrag_delete(self->m_data->m_drag);
            self->m_data->m_drag = NULL;
        }
        if (self->m_data->m_applicationIcon) {
            XIcon_delete_base(self->m_data->m_applicationIcon);
            self->m_data->m_applicationIcon = NULL;
        }
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XPlatformIntegration_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlatformIntegration)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlatformIntegration_deinit);
    return XVTABLE_DEFAULT;
}

void XPlatformIntegration_init(XPlatformIntegration* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPlatformIntegration));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XPlatformIntegration);
    self->m_data = (XPlatformIntegrationPrivate*)XMalloc_System(sizeof(XPlatformIntegrationPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XPlatformIntegrationPrivate));

    self->m_data->m_themeName = XString_create_utf8("embedded");
    self->m_data->m_fontDatabase = XPlatformFontDatabase_create();
    self->m_data->m_theme = XPlatformTheme_create(NULL);
    self->m_data->m_services = XPlatformServices_create();
    self->m_data->m_drag = XPlatformDrag_create();
    self->m_data->m_platformWindows = XVector_Create(XPlatformWindow*);

    /* 嵌入式默认能力位（对照 QPlatformIntegration::Capability 枚举值）。 */
    self->m_data->m_capabilities =
        capabilityBit(XPlatformIntegrationCapability_ThreadedPixmaps) |
        capabilityBit(XPlatformIntegrationCapability_WindowMasks) |
        capabilityBit(XPlatformIntegrationCapability_MultipleWindows) |
        capabilityBit(XPlatformIntegrationCapability_ApplicationState) |
        capabilityBit(XPlatformIntegrationCapability_NonFullScreenWindows) |
        capabilityBit(XPlatformIntegrationCapability_WindowManagement) |
        capabilityBit(XPlatformIntegrationCapability_WindowActivation) |
        capabilityBit(XPlatformIntegrationCapability_SyncState) |
        capabilityBit(XPlatformIntegrationCapability_ApplicationIcon) |
        capabilityBit(XPlatformIntegrationCapability_PaintEvents);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
    /* BackingStoreStaticContents：随 XBackingStore 实现默认开启；总开关
     * 关闭时该位不置位（测试断言按对应开关裁剪）。 */
    self->m_data->m_capabilities |=
        capabilityBit(XPlatformIntegrationCapability_BackingStoreStaticContents);
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

    if (XPlatformGraphics_isOpenGLAvailable()) {
        self->m_data->m_capabilities |=
            capabilityBit(XPlatformIntegrationCapability_OpenGL) |
            capabilityBit(XPlatformIntegrationCapability_AllGLFunctionsQueryable);
    }
    if (XPlatformGraphics_isVulkanAvailable()) {
        self->m_data->m_capabilities |=
            capabilityBit(XPlatformIntegrationCapability_RhiBasedRendering);
    }
#if XPLATFORMNATIVEWINDOW_ON
    if (XPlatformNativeWindow_isAvailable())
        self->m_data->m_capabilities |=
            capabilityBit(XPlatformIntegrationCapability_ForeignWindows);
#endif

#if XPLATFORMNATIVEINTERFACE_ON
    self->m_data->m_nativeInterface =
        XPlatformNativeInterface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_data->m_nativeInterface)
        XPlatformNativeInterface_setIntegration(self->m_data->m_nativeInterface, self);
#endif /* XPLATFORMNATIVEINTERFACE_ON */
#if XPLATFORMINPUTCTX_ON
    self->m_data->m_inputContext =
        XPlatformInputContext_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif /* XPLATFORMINPUTCTX_ON */
#if XWINDOW_ON && XACCESSIBLE_ON
    self->m_data->m_accessibility =
        XPlatformAccessibility_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif
}

XPlatformIntegration* XPlatformIntegration_create_ex(XMemoryType memory)
{
    XPlatformIntegration* self;
    self = (XPlatformIntegration*)XMemory_malloc(sizeof(XPlatformIntegration), memory);
    if (!self) return NULL;
    XPlatformIntegration_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 能力查询 ==================== */

bool XPlatformIntegration_hasCapability(const XPlatformIntegration* self,
                                        XPlatformIntegrationCapability cap)
{
    uint64_t bit;
    if (!self || !self->m_data) return false;
    bit = capabilityBit(cap);
    if (!bit) return false;
    return (self->m_data->m_capabilities & bit) != 0;
}

/* ==================== 平台对象创建 ==================== */

XPlatformWindow* XPlatformIntegration_createPlatformWindow(
        XPlatformIntegration* self, XWindow* window)
{
#if XPLATFORMWINDOW_ON
    XPlatformWindow* pw;
    size_t i;
    size_t n;
    if (!self || !self->m_data || !self->m_data->m_platformWindows) return NULL;
    n = XVector_size_base((const XContainer*)self->m_data->m_platformWindows);
    for (i = 0; i < n; ++i) {
        pw = XVector_At_Base(self->m_data->m_platformWindows, (int64_t)i, XPlatformWindow*);
        if (!pw) continue;
        if (XPlatformWindow_window(pw) == window) return pw; /* 幂等。 */
    }
    pw = XPlatformWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, window);
    if (!pw) return NULL;
    XVector_Push_Back_Base(self->m_data->m_platformWindows, XPlatformWindow*, pw);
#if XWINDOW_ON
    if (window) {
        XWindow_setHandle(window, (XWindowPlatform*)pw); /* 挂接平台句柄（借用）。 */
#if XPLATFORMNATIVEWINDOW_ON
        /* 标记真实原生窗口挂接：此后 XWindow 首次显示将创建系统窗口
           （X11 Window / Win32 HWND）并接管真实 WId。仅持有假句柄的
           回归用例不受影响（门槛为 m_platform 与本标志同时成立）。 */
        XWindow_setNativeWindowAttached(window, true);
#endif /* XPLATFORMNATIVEWINDOW_ON */
    }
#endif /* XWINDOW_ON */
    return pw;
#else /* !XPLATFORMWINDOW_ON */
    (void)self; (void)window;
    return NULL;
#endif /* XPLATFORMWINDOW_ON */
}

XPlatformWindow* XPlatformIntegration_createForeignWindow(
        XPlatformIntegration* self, XWindow* window, XWindowId nativeHandle)
{
#if XPLATFORMWINDOW_ON && XPLATFORMNATIVEWINDOW_ON && XWINDOW_ON
    XPlatformWindow* pw;
    size_t i;
    size_t n;
    if (!self || !self->m_data || !window || nativeHandle == 0) return NULL;
    /* 与 createPlatformWindow 保持幂等：已有平台对象直接复用，避免同一
       XWindow 在登记表中出现两个句柄对象。已创建窗口不能改变外部句柄。 */
    n = XVector_size_base((const XContainer*)self->m_data->m_platformWindows);
    for (i = 0; i < n; ++i) {
        pw = XVector_At_Base(self->m_data->m_platformWindows,
                             (int64_t)i, XPlatformWindow*);
        if (pw && XPlatformWindow_window(pw) == window) {
            if (XPlatformWindow_isForeign(pw))
                return XWindow_winId(window) == nativeHandle ? pw : NULL;
            if (!XWindow_attachForeignHandle(window, nativeHandle)) return NULL;
            XPlatformWindow_setForeign(pw, true);
            XWindow_setHandle(window, (XWindowPlatform*)pw);
            return pw;
        }
    }
    pw = XPlatformWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, window);
    if (!pw) return NULL;
    XVector_Push_Back_Base(self->m_data->m_platformWindows, XPlatformWindow*, pw);
    if (!XWindow_attachForeignHandle(window, nativeHandle)) {
        n = XVector_size_base((const XContainer*)self->m_data->m_platformWindows);
        if (n > 0)
            XVector_remove_base(self->m_data->m_platformWindows, (int64_t)n - 1, 1);
        XPlatformWindow_delete_base(pw);
        return NULL;
    }
    XPlatformWindow_setForeign(pw, true);
    XWindow_setHandle(window, (XWindowPlatform*)pw);
    return pw;
#else
    (void)self; (void)window; (void)nativeHandle;
    return NULL;
#endif
}

XPlatformBackingStore* XPlatformIntegration_createPlatformBackingStore(
        XPlatformIntegration* self, XWindow* window)
{
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
    /* 转发 Drive 平台后端：Linux 软件缓冲 / Windows GDI，其它平台由
     * XPlatformBackingStore_unsupported.c 回落 NULL（不分配内存）。 */
    (void)self;
    return XPlatformBackingStore_create(window);
#else /* !XBACKINGSTORE_ON || !XPLATFORMBACKINGSTORE_ON */
    (void)self; (void)window;
    return NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
}

void* XPlatformIntegration_createPlatformPixmap(
        XPlatformIntegration* self, int pixelType)
{
    /* XPixmap 由进程内位图承载，无独立平台位图数据层：恒 NULL。 */
    (void)self; (void)pixelType;
    return NULL;
}

void* XPlatformIntegration_createPlatformOpenGLContext(
        XPlatformIntegration* self, void* context)
{
    (void)self;
#if XWINDOW_ON
    return XPlatformOpenGLContext_create((XWindow*)context);
#else
    (void)context;
    return NULL;
#endif
}

void* XPlatformIntegration_createPlatformSharedGraphicsCache(
        XPlatformIntegration* self, const char* cacheId)
{
    (void)self; (void)cacheId;
    return NULL;
}

void* XPlatformIntegration_createImagePaintEngine(
        XPlatformIntegration* self, void* paintDevice)
{
    /* 绘制由 XPainter 进程内实现：恒 NULL。 */
    (void)self; (void)paintDevice;
    return NULL;
}

void* XPlatformIntegration_createEventDispatcher(
        XPlatformIntegration* self)
{
    /* 返回统一调度器；原生窗口事件仍由 processNativeEvents() 泵入。 */
    (void)self;
#if XSYNC_ON && XTHREADDATA_ON
    {
        XThreadData* data = XThreadData_current();
        return data ? (void*)XThreadData_createEventDispatcher(data)
                    : (void*)XEventDispatcher_create(NULL);
    }
#else
    return NULL;
#endif
}

void XPlatformIntegration_initialize(XPlatformIntegration* self)
{
    /* 空后端：子对象在 init 已创建，无额外初始化动作。 */
    (void)self;
}

void XPlatformIntegration_destroy(XPlatformIntegration* self)
{
    /* 空后端：资源由析构统一回收。 */
    (void)self;
}

/* ==================== 原生窗口事件泵（平台事件源转发） ==================== */

bool XPlatformIntegration_isNativeWindowAvailable(
        const XPlatformIntegration* self)
{
#if XPLATFORMNATIVEWINDOW_ON
    (void)self;
    return XPlatformNativeWindow_isAvailable();
#else /* !XPLATFORMNATIVEWINDOW_ON */
    (void)self;
    return false;
#endif /* XPLATFORMNATIVEWINDOW_ON */
}

bool XPlatformIntegration_processNativeEvents(
        const XPlatformIntegration* self)
{
#if XPLATFORMNATIVEWINDOW_ON
    (void)self;
    return XPlatformNativeWindow_processPendingEvents();
#else /* !XPLATFORMNATIVEWINDOW_ON */
    (void)self;
    return false;
#endif /* XPLATFORMNATIVEWINDOW_ON */
}

bool XPlatformIntegration_waitForNativeEvents(
        const XPlatformIntegration* self, int maxMilliseconds)
{
#if XPLATFORMNATIVEWINDOW_ON
    (void)self;
    return XPlatformNativeWindow_waitForEvents(maxMilliseconds);
#else /* !XPLATFORMNATIVEWINDOW_ON */
    (void)self; (void)maxMilliseconds;
    return false;
#endif /* XPLATFORMNATIVEWINDOW_ON */
}

/* ==================== 子单例访问 ==================== */

void* XPlatformIntegration_fontDatabase(const XPlatformIntegration* self)
{
    return self && self->m_data ? self->m_data->m_fontDatabase : NULL;
}

XClipboard* XPlatformIntegration_clipboard(const XPlatformIntegration* self)
{
    if (!self || !self->m_data) return NULL;
    return self->m_data->m_clipboard;
}

void XPlatformIntegration_setClipboard(XPlatformIntegration* self,
                                       XClipboard* clipboard)
{
    if (!self || !self->m_data) return;
    self->m_data->m_clipboard = clipboard;
}

void* XPlatformIntegration_drag(const XPlatformIntegration* self)
{
    return self && self->m_data ? self->m_data->m_drag : NULL;
}

XPlatformInputContext* XPlatformIntegration_inputContext(
        const XPlatformIntegration* self)
{
    if (!self || !self->m_data) return NULL;
    return self->m_data->m_inputContext;
}

void* XPlatformIntegration_accessibility(const XPlatformIntegration* self)
{
#if XWINDOW_ON && XACCESSIBLE_ON
    return self && self->m_data ? self->m_data->m_accessibility : NULL;
#else
    (void)self;
    return NULL;
#endif
}

XPlatformNativeInterface* XPlatformIntegration_nativeInterface(
        const XPlatformIntegration* self)
{
    if (!self || !self->m_data) return NULL;
    return self->m_data->m_nativeInterface;
}

void* XPlatformIntegration_services(const XPlatformIntegration* self)
{
    return self && self->m_data ? self->m_data->m_services : NULL;
}

/* ==================== 样式提示 / 输入状态 ==================== */

/** @brief 读取风格提示整型；未注入单例时返回嵌入式默认值。 */
static int integration_styleInt(const XPlatformIntegration* self,
                                XPlatformIntegrationStyleHint hint,
                                int fallback)
{
#if XSTYLEHINTS_ON
    XStyleHints* sh;
    if (!self || !self->m_data) return fallback;
    sh = self->m_data->m_styleHints;
    if (!sh) return fallback;
    switch (hint) {
        case XPlatformIntegrationStyleHint_CursorFlashTime:
            return XStyleHints_cursorFlashTime(sh);
        case XPlatformIntegrationStyleHint_KeyboardInputInterval:
            return XStyleHints_keyboardInputInterval(sh);
        case XPlatformIntegrationStyleHint_MouseDoubleClickInterval:
            return XStyleHints_mouseDoubleClickInterval(sh);
        case XPlatformIntegrationStyleHint_StartDragDistance:
            return XStyleHints_startDragDistance(sh);
        case XPlatformIntegrationStyleHint_StartDragTime:
            return XStyleHints_startDragTime(sh);
        case XPlatformIntegrationStyleHint_KeyboardAutoRepeatRate:
            return XStyleHints_keyboardAutoRepeatRate(sh);
        case XPlatformIntegrationStyleHint_PasswordMaskDelay:
            return XStyleHints_passwordMaskDelay(sh);
        case XPlatformIntegrationStyleHint_StartDragVelocity:
            return XStyleHints_startDragVelocity(sh);
        case XPlatformIntegrationStyleHint_TabFocusBehavior:
            return (int)XStyleHints_tabFocusBehavior(sh);
        case XPlatformIntegrationStyleHint_WheelScrollLines:
            return XStyleHints_wheelScrollLines(sh);
        case XPlatformIntegrationStyleHint_MouseQuickSelectionThreshold:
            return XStyleHints_mouseQuickSelectionThreshold(sh);
        case XPlatformIntegrationStyleHint_MouseDoubleClickDistance:
            return XStyleHints_mouseDoubleClickDistance(sh);
        default:
            return fallback;
    }
#else /* !XSTYLEHINTS_ON */
    (void)self; (void)hint; (void)fallback;
    return fallback;
#endif /* XSTYLEHINTS_ON */
}

/** @brief 读取风格提示布尔；未注入单例时返回嵌入式默认值。 */
static bool integration_styleBool(const XPlatformIntegration* self,
                                  XPlatformIntegrationStyleHint hint,
                                  bool fallback)
{
#if XSTYLEHINTS_ON
    XStyleHints* sh;
    if (!self || !self->m_data) return fallback;
    sh = self->m_data->m_styleHints;
    if (!sh) return fallback;
    switch (hint) {
        case XPlatformIntegrationStyleHint_ShowIsFullScreen:
            return XStyleHints_showIsFullScreen(sh);
        case XPlatformIntegrationStyleHint_UseRtlExtensions:
            return XStyleHints_useRtlExtensions(sh);
        case XPlatformIntegrationStyleHint_SetFocusOnTouchRelease:
            return XStyleHints_setFocusOnTouchRelease(sh);
        case XPlatformIntegrationStyleHint_ShowIsMaximized:
            return XStyleHints_showIsMaximized(sh);
        case XPlatformIntegrationStyleHint_ItemViewActivateItemOnSingleClick:
            return XStyleHints_singleClickActivation(sh);
        case XPlatformIntegrationStyleHint_ShowShortcutsInContextMenus:
            return XStyleHints_showShortcutsInContextMenus(sh);
        default:
            return fallback;
    }
#else /* !XSTYLEHINTS_ON */
    (void)self; (void)hint; (void)fallback;
    return fallback;
#endif /* XSTYLEHINTS_ON */
}

XVariant* XPlatformIntegration_styleHint(const XPlatformIntegration* self,
                                         XPlatformIntegrationStyleHint hint)
{
    switch (hint) {
        case XPlatformIntegrationStyleHint_CursorFlashTime:
            return XVariant_create_int(integration_styleInt(self, hint, 1000));
        case XPlatformIntegrationStyleHint_KeyboardInputInterval:
            return XVariant_create_int(integration_styleInt(self, hint, 400));
        case XPlatformIntegrationStyleHint_MouseDoubleClickInterval:
            return XVariant_create_int(integration_styleInt(self, hint, 400));
        case XPlatformIntegrationStyleHint_StartDragDistance:
            return XVariant_create_int(integration_styleInt(self, hint, 10));
        case XPlatformIntegrationStyleHint_StartDragTime:
            return XVariant_create_int(integration_styleInt(self, hint, 500));
        case XPlatformIntegrationStyleHint_KeyboardAutoRepeatRate:
            return XVariant_create_int(integration_styleInt(self, hint, 33));
        case XPlatformIntegrationStyleHint_ShowIsFullScreen:
            return XVariant_create_bool(integration_styleBool(self, hint, false));
        case XPlatformIntegrationStyleHint_PasswordMaskDelay:
            return XVariant_create_int(integration_styleInt(self, hint, 0));
#if XSTYLEHINTS_ON
        case XPlatformIntegrationStyleHint_FontSmoothingGamma: {
            XStyleHints* sh = (self && self->m_data) ? self->m_data->m_styleHints : NULL;
            return XVariant_create_float(
                sh ? XStyleHints_fontSmoothingGamma(sh) : 1.0f);
        }
#endif /* XSTYLEHINTS_ON */
        case XPlatformIntegrationStyleHint_StartDragVelocity:
            return XVariant_create_int(integration_styleInt(self, hint, 0));
        case XPlatformIntegrationStyleHint_UseRtlExtensions:
            return XVariant_create_bool(integration_styleBool(self, hint, false));
#if XSTYLEHINTS_ON
        case XPlatformIntegrationStyleHint_PasswordMaskCharacter: {
            XStyleHints* sh = (self && self->m_data) ? self->m_data->m_styleHints : NULL;
            return XVariant_create_uint32(
                sh ? XStyleHints_passwordMaskCharacter(sh) : 0x2022u);
        }
#endif /* XSTYLEHINTS_ON */
        case XPlatformIntegrationStyleHint_SetFocusOnTouchRelease:
            return XVariant_create_bool(integration_styleBool(self, hint, true));
        case XPlatformIntegrationStyleHint_ShowIsMaximized:
            return XVariant_create_bool(integration_styleBool(self, hint, false));
        case XPlatformIntegrationStyleHint_MousePressAndHoldInterval:
            return XVariant_create_int(integration_styleInt(self, hint, 500));
        case XPlatformIntegrationStyleHint_TabFocusBehavior:
            return XVariant_create_int(integration_styleInt(self, hint, 0xff));
        case XPlatformIntegrationStyleHint_ReplayMousePressOutsidePopup:
            return XVariant_create_bool(false);
        case XPlatformIntegrationStyleHint_ItemViewActivateItemOnSingleClick:
            return XVariant_create_bool(integration_styleBool(self, hint, false));
        case XPlatformIntegrationStyleHint_UiEffects:
            return XVariant_create_int(0);
        case XPlatformIntegrationStyleHint_WheelScrollLines:
            return XVariant_create_int(integration_styleInt(self, hint, 3));
        case XPlatformIntegrationStyleHint_ShowShortcutsInContextMenus:
            return XVariant_create_bool(integration_styleBool(self, hint, false));
        case XPlatformIntegrationStyleHint_MouseQuickSelectionThreshold:
            return XVariant_create_int(integration_styleInt(self, hint, 0));
        case XPlatformIntegrationStyleHint_MouseDoubleClickDistance:
            return XVariant_create_int(integration_styleInt(self, hint, 5));
        case XPlatformIntegrationStyleHint_FlickStartDistance:
            return XVariant_create_int(30);
        case XPlatformIntegrationStyleHint_FlickMaximumVelocity:
            return XVariant_create_int(3000);
        case XPlatformIntegrationStyleHint_FlickDeceleration:
            return XVariant_create_int(1500);
        case XPlatformIntegrationStyleHint_UnderlineShortcut:
            return XVariant_create_bool(false);
        default:
            return NULL;
    }
}

XWindowState XPlatformIntegration_defaultWindowState(
        const XPlatformIntegration* self, XWindowFlags flags)
{
    /* 嵌入式无窗口管理器策略：恒普通状态。 */
    (void)self; (void)flags;
    return XWindowState_NoState;
}

XKeyboardModifiers XPlatformIntegration_queryKeyboardModifiers(
        const XPlatformIntegration* self)
{
    (void)self;
#if XGUIAPPLICATION_ON
    /* 转发程序化修饰键状态。 */
    return XGuiApplication_queryKeyboardModifiers();
#else /* !XGUIAPPLICATION_ON */
    return 0;
#endif /* XGUIAPPLICATION_ON */
}

void* XPlatformIntegration_possibleKeys(const XPlatformIntegration* self,
                                        void* keyEvent)
{
    /* 无键盘码表协议：恒 NULL。 */
    (void)self; (void)keyEvent;
    return NULL;
}

void* XPlatformIntegration_keyMapper(const XPlatformIntegration* self)
{
    (void)self;
    return NULL;
}

/* ==================== 平台主题 ==================== */

XVector* XPlatformIntegration_themeNames(const XPlatformIntegration* self)
{
    XVector* out;
    XString* name;
    if (!self || !self->m_data) return NULL;
    out = XVector_Create(XString*);
    if (!out) return NULL;
    name = self->m_data->m_themeName;
    if (name)
        XVector_Push_Back_Base(out, XString*, name); /* 元素为借用指针。 */
    return out;
}

XString* XPlatformIntegration_themeName(const XPlatformIntegration* self)
{
    if (!self || !self->m_data || !self->m_data->m_themeName) return NULL;
    return XString_create_copy(self->m_data->m_themeName);
}

void* XPlatformIntegration_createPlatformTheme(
        const XPlatformIntegration* self, const XString* name)
{
    const char* value = name ? XString_toUtf8(name) : NULL;
    (void)self;
    return XPlatformTheme_create(value);
}

void* XPlatformIntegration_createPlatformOffscreenSurface(
        XPlatformIntegration* self, void* surface)
{
    (void)self; (void)surface;
    return (void*)XPlatformOffscreenSurface_create(1, 1);
}

void* XPlatformIntegration_createPlatformSessionManager(
        XPlatformIntegration* self, const XString* id, const XString* key)
{
    (void)self; (void)id; (void)key;
    return NULL;
}

/* ==================== 同步 / 平台行为 ==================== */

void XPlatformIntegration_sync(XPlatformIntegration* self)
{
    /* 空后端无真实窗口栈：恒 no-op。 */
    (void)self;
}

int XPlatformIntegration_openGLModuleType(XPlatformIntegration* self)
{
    (void)self;
    return XPlatformGraphics_isOpenGLAvailable() ? 1 : 0;
}

void XPlatformIntegration_setApplicationIcon(XPlatformIntegration* self,
                                             const XIcon* icon)
{
    XIcon* copy;
    if (!self || !self->m_data) return;
    copy = integration_cloneIcon(icon);
    if (self->m_data->m_applicationIcon) {
        XIcon_delete_base(self->m_data->m_applicationIcon);
        self->m_data->m_applicationIcon = NULL;
    }
    self->m_data->m_applicationIcon = copy;
}

void XPlatformIntegration_setApplicationBadge(XPlatformIntegration* self,
                                              int64_t number)
{
    if (!self || !self->m_data) return;
    self->m_data->m_badgeNumber = number;
}

bool XPlatformIntegration_beep(XPlatformIntegration* self)
{
    /* 嵌入式无系统提示音：恒 false。 */
    (void)self;
    return false;
}

void XPlatformIntegration_quit(XPlatformIntegration* self)
{
    (void)self;
    XCoreApplication_quit();
}

void* XPlatformIntegration_createPlatformVulkanInstance(
        XPlatformIntegration* self, void* instance)
{
    (void)self;
    (void)instance;
    return XPlatformVulkanInstance_create();
}

/* ==================== 内部状态访问 ==================== */

void XPlatformIntegration_setStyleHints(XPlatformIntegration* self,
                                        XStyleHints* hints)
{
    if (!self || !self->m_data) return;
    self->m_data->m_styleHints = hints;
}

#endif /* XPLATFORMINTEGRATION_ON */
