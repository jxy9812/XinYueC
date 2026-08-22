/******************************************************************************
 * @file       XWindow.c
 * @brief      XWindow 顶层窗口类实现（对标 Qt 6.8 QWindow，实现全部公开 API）。
 * @details    本文件实现 XWindow 的全部属性、几何/尺寸约束、可见性与窗口
 *             状态、表面格式、屏幕归属、标题/图标/文件路径/模态/瞬态父窗口、
 *             键盘/鼠标抓取、全局坐标映射、19 个通知信号与 20 个事件分发
 *             入口。行为语义逐一与 Qt 6.8.3 qwindow.cpp / qsurface.cpp 对齐：
 *              - setVisible/updateVisibility 走 QWindowPrivate 同名流程：
 *                可见性变化先发 visibleChanged，再更新 visibility 并发射
 *                visibilityChanged；首次显示自动 create() 并发送 ShowEvent，
 *                隐藏发送 HideEvent（经 XCoreApplication_sendEvent 分发以便
 *                子类重载事件槽）；
 *              - setWindowStates 清除并忽略 WindowActive 位，生效状态按
 *                Minimized > FullScreen > Maximized > NoState 优先级取；
 *              - setGeometry/resize 在无平台窗口时直接更新几何并按逐字段
 *                变化发射 x/y/width/heightChanged；setFramePosition 等价
 *                setPosition；
 *              - setMinimumSize/setMaximumSize 使用 QWINDOWSIZE_MAX=16777215
 *                裁剪，并在当前几何越界时自动 resize 夹紧；
 *              - setTransientParent 只拒绝「非顶层」与「自身」；setParent
 *                只拒绝 Desktop 类型（视为 NULL），不自动隐藏、不清空瞬态
 *                父窗口；
 *              - setScreen 的 NULL 回退主屏幕、非顶层拒绝与 screenChanged
 *                发射；mapToGlobal/mapFromGlobal 为自身位置沿父链累加；
 *              - event 分发按 XEvent_type 路由到 19 个事件槽，未识别事件
 *                回退 XObject 默认 Event 实现。
 * @note       模块总开关 XWINDOW_ON 定义于 XGuiConfig.h；置 0 时本文件
 *             实现体整体裁剪。依赖子开关 XSURFACEFORMAT_ON / XSCREEN_ON /
 *             XCURSOR_ON，关闭时对应子能力按头文件回退语义退化为空实现。
 *             本模块不依赖任何平台 API，纯程序化属性窗口。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWindow.h"
#include "XAccessible.h"
#if XACCESSIBLE_ON
#include "XPlatformAccessibility.h"
#endif
#include "XVarList.h"
#include "XCoreApplication.h"
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */
#if XPLATFORMWINDOW_ON
#include "XPlatformWindow.h"
#endif /* XPLATFORMWINDOW_ON */
#include <string.h>

#if XWINDOW_ON

/* ==================== 私有实现 ==================== */

/**
 * @brief XWindow 私有属性快照；仅本文件访问。
 * @details 资源字段（字符串/图标/光标/遮罩）由本对象拥有，各项 setter 均
 *          深拷贝持有；父窗口/瞬态父窗口/屏幕/平台句柄为借用指针不拥有；
 *          m_parentWindow 与 XObject 的 m_parent 保持同步，供
 *          XWindow_parent(XWindowAncestor_IncludeTransients) 语义查询。
 */
struct XWindowPrivate
{
    XString* m_title;                   /**< 窗口标题；对象拥有。 */
    XString* m_filePath;                /**< 文件路径；对象拥有。 */
    XIcon* m_icon;                      /**< 窗口图标；对象拥有，可为 NULL。 */
#if XCURSOR_ON
    XCursor* m_cursor;                  /**< 窗口光标；对象拥有，可为 NULL。 */
#endif
    XRegion m_mask;                     /**< 形状遮罩；对象拥有（值类型容器）。 */
    bool m_hasMask;                     /**< 是否存在已设置的遮罩。 */

    XRect m_geometry;                   /**< 窗口几何（设备无关像素）。 */
    XWindowSurfaceType m_surfaceType;   /**< 表面类型；默认 Raster。 */
    XWindowFlags m_flags;               /**< 窗口标志位；默认 Window。 */
    bool m_visible;                     /**< 是否可见（对标 isVisible）。 */
    XWindowVisibility m_visibility;     /**< 可见性枚举；默认 Hidden。 */
    bool m_exposed;                     /**< 是否已暴露（对标 isExposed）。 */
    bool m_created;                     /**< 平台窗口资源是否已创建（占位）。 */
    XWindowId m_winId;                  /**< 窗口 id；未创建为 0。 */
    XWindowPlatform* m_platform;        /**< 平台窗口句柄；借用不拥有。 */
    bool m_nativeWindowAttached;    /**< 是否已挂接真实原生窗口（平台后端注入）。 */

    XWindow* m_parentWindow;            /**< 普通父窗口（借用，与 XObject 父同步）。 */
    XWindow* m_transientParent;         /**< 瞬态父窗口（借用）。 */
    XWindowModality m_modality;         /**< 模态；默认 NonModal。 */

    XWindowStates m_windowStates;       /**< 窗口状态位组合。 */
    bool m_positionAutomatic;           /**< 位置是否由平台自动摆放。 */
    bool m_active;                      /**< 是否激活。 */
    bool m_closing;                     /**< close() 重入保护。 */

    XSurfaceFormat m_format;/**< 请求的表面格式快照。 */

    float m_devicePixelRatio;           /**< 设备像素比；默认 1.0。 */
    XScreenOrientation m_contentOrientation; /**< 内容方向；默认 Primary。 */
    float m_opacity;                    /**< 不透明度；默认 1.0。 */

    XSize m_minimumSize;                /**< 最小尺寸；默认 (0,0)。 */
    XSize m_maximumSize;                /**< 最大尺寸；默认 (16777215,16777215)。 */
    XSize m_baseSize;                   /**< 基准尺寸；默认 (0,0)。 */
    XSize m_sizeIncrement;              /**< 尺寸步进；默认 (0,0)。 */

#if XSCREEN_ON
    XScreen* m_screen;                  /**< 显式关联屏幕（借用）；NULL 表示未设置。 */
#endif
    int m_alertMsec;                    /**< 最近一次 alert() 的毫秒数。 */
    bool m_updateRequested;             /**< requestUpdate() 待更新标志。 */
#if XACCESSIBLE_ON
    XAccessible* m_accessibleRoot;      /**< 窗口可访问根节点（拥有）。 */
#endif
};

/** @brief 最大尺寸上限（对标 Qt QWINDOWSIZE_MAX；qwindow.cpp 同值）。 */
#define XWINDOW_MAX_SIZE 16777215

/** @brief 窗口 id 分配器；自 1 递增，0 恒表示未创建。 */
static XWindowId g_nextWinId = 0;

/** @brief 浮点近似相等判断（不透明度等变化检测）。 */
static bool XWindow_floatNear(float a, float b)
{
    float delta = a - b;
    float bound = 1e-4f;
    return delta < 0.0f ? -delta <= bound : delta <= bound;
}

/** @brief 深拷贝字符串。 @param value 源字符串；可为 NULL。 @return 深拷贝或 NULL。 */
static XString* XWindow_copyString(const XString* value)
{
    return value ? XString_create_copy(value) : NULL;
}

/** @brief 深拷贝替换字符串字段。 @param dst 目标字段。 @param value 源字符串；可为 NULL 清空。 */
static void XWindow_setString(XString** dst, const XString* value)
{
    XString* copy = XWindow_copyString(value);
    if (*dst) XString_delete_base((XClass*)*dst);
    *dst = copy;
}

#if XCURSOR_ON
/** @brief 深拷贝光标（XCURSOR_ON 开启时）。 @param value 源光标；可为 NULL。 @return 深拷贝或 NULL。 */
static XCursor* XWindow_copyCursor(const XCursor* value)
{
    XCursor* copy;
    if (!value) return NULL;
    copy = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XCursor_copy_base(copy, value);
    return copy;
}

/** @brief 深拷贝替换光标字段。 @param dst 目标字段。 @param value 源光标；可为 NULL 清空。 */
static void XWindow_setCursorInternal(XCursor** dst, const XCursor* value)
{
    XCursor* copy = XWindow_copyCursor(value);
    if (*dst) XCursor_delete_base((XClass*)*dst);
    *dst = copy;
}

/** @brief 释放光标字段。 @param data 目标私有块；可为 NULL。 */
static void XWindow_clearCursor(XWindowPrivate* data)
{
    if (data && data->m_cursor) {
        XCursor_delete_base((XClass*)data->m_cursor);
        data->m_cursor = NULL;
    }
}
#endif /* XCURSOR_ON */

/** @brief 深拷贝图标。 @param value 源图标；可为 NULL。 @return 深拷贝或 NULL。 */
static XIcon* XWindow_copyIcon(const XIcon* value)
{
    XIcon* copy;
    if (!value) return NULL;
    copy = XIcon_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XIcon_copy_base(copy, value);
    return copy;
}

/** @brief 深拷贝替换图标字段。 @param dst 目标字段。 @param value 源图标；可为 NULL 清空。 */
static void XWindow_setIconInternal(XIcon** dst, const XIcon* value)
{
    XIcon* copy = XWindow_copyIcon(value);
    /* 内部图标由 create_ex 堆分配，需 delete（deinit+释放结构体）。 */
    if (*dst) XIcon_delete_base(*dst);
    *dst = copy;
}

/** @brief 格式化（合并）请求格式与默认格式：未显式设置字段回退默认。
 * @details 无平台后端时生效格式 = 请求格式中显式设置字段 + XSurfaceFormat_
 *          defaultFormat() 的其他字段（与 QWindow::format 缺省实现一致）。
 * @param requested 请求格式。 @return 合并后的生效格式。 */
static XSurfaceFormat XWindow_mergeFormat(const XSurfaceFormat* requested)
{
    XSurfaceFormat result;
#if XSURFACEFORMAT_ON
    XSurfaceFormat def;
    def = XSurfaceFormat_defaultFormat();
    if (!requested) return def;
    result = *requested;
    if (result.m_options == 0) result.m_options = def.m_options;
    if (result.m_redBufferSize < 0) result.m_redBufferSize = def.m_redBufferSize;
    if (result.m_greenBufferSize < 0) result.m_greenBufferSize = def.m_greenBufferSize;
    if (result.m_blueBufferSize < 0) result.m_blueBufferSize = def.m_blueBufferSize;
    if (result.m_alphaBufferSize < 0) result.m_alphaBufferSize = def.m_alphaBufferSize;
    if (result.m_depthBufferSize < 0) result.m_depthBufferSize = def.m_depthBufferSize;
    if (result.m_stencilBufferSize < 0) result.m_stencilBufferSize = def.m_stencilBufferSize;
    if (result.m_samples < 0) result.m_samples = def.m_samples;
    if (result.m_swapBehavior == 0) result.m_swapBehavior = def.m_swapBehavior;
    if (result.m_renderableType == 0) result.m_renderableType = def.m_renderableType;
    if (result.m_profile == 0) result.m_profile = def.m_profile;
    if (result.m_majorVersion == 0 && result.m_minorVersion == 0) {
        result.m_majorVersion = def.m_majorVersion;
        result.m_minorVersion = def.m_minorVersion;
    }
    if (result.m_swapInterval == 0) result.m_swapInterval = def.m_swapInterval;
#else
    (void)requested;
    memset(&result, 0, sizeof(result));
    result.m_redBufferSize = -1;
    result.m_greenBufferSize = -1;
    result.m_blueBufferSize = -1;
    result.m_alphaBufferSize = -1;
    result.m_depthBufferSize = -1;
    result.m_stencilBufferSize = -1;
    result.m_samples = -1;
    result.m_majorVersion = 2;
    result.m_swapInterval = 1;
#endif
    return result;
}

/** @brief 计算生效窗口状态（对标 QWindow::effectiveState 优先级）。 */
static XWindowState XWindow_effectiveState(const XWindowPrivate* data)
{
    if (!data) return XWindowState_NoState;
    if (data->m_windowStates & XWindowState_Minimized)
        return XWindowState_Minimized;
    if (data->m_windowStates & XWindowState_FullScreen)
        return XWindowState_FullScreen;
    if (data->m_windowStates & XWindowState_Maximized)
        return XWindowState_Maximized;
    return XWindowState_NoState;
}

/** @brief 沿普通父链累加全局偏移（对标 QWindowPrivate::globalPosition）。 */
static XPoint XWindow_globalOffset(const XWindow* self)
{
    XPoint offset = {0, 0};
    const XWindow* walk;
    if (!self) return offset;
    for (walk = self; walk; walk = walk->m_data
                               ? walk->m_data->m_parentWindow : NULL) {
        offset.x += walk->m_data ? walk->m_data->m_geometry.x : 0;
        offset.y += walk->m_data ? walk->m_data->m_geometry.y : 0;
    }
    return offset;
}

/** @brief 判断当前可见性枚举是否需要刷新并发射 visibilityChanged。 */
static void XWindow_updateVisibility(XWindow* self)
{
    XWindowPrivate* data;
    XWindowVisibility visibility;
    if (!self || !(data = self->m_data)) return;
    if (!data->m_visible) {
        visibility = XWindowVisibility_Hidden;
    } else if (data->m_windowStates & XWindowState_Minimized) {
        visibility = XWindowVisibility_Minimized;
    } else if (data->m_windowStates & XWindowState_FullScreen) {
        visibility = XWindowVisibility_FullScreen;
    } else if (data->m_windowStates & XWindowState_Maximized) {
        visibility = XWindowVisibility_Maximized;
    } else {
        visibility = XWindowVisibility_Windowed;
    }
    if (visibility != data->m_visibility) {
        data->m_visibility = visibility;
        XWindow_visibilityChanged_signal(self, visibility);
    }
}

/** @brief 发射信号并管理参数列表生命周期（与 XScreen_emit 相同模式）。 */
static void XWindow_emit(XWindow* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/** @brief 发送基础事件到窗口事件入口并释放事件对象。 */
static void XWindow_sendEvent(XWindow* self, XEventType type)
{
    XEvent* event;
    if (!self) return;
    event = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type);
    if (!event) return;
    XCoreApplication_sendEvent((XObject*)self, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
}

/* ==================== 事件默认槽与前向声明 ==================== */

static bool VXWindow_event(XWindow* self, XEvent* event);
static void VXWindow_deinit(XWindow* self);
static void VXWindow_copy(XWindow* self, const XWindow* other);
static void VXWindow_move(XWindow* self, XWindow* other);

/** @brief 默认空事件槽。 */
static void XWindow_defaultEventSlot(XWindow* self, XEvent* event)
{
    (void)self;
    (void)event;
}

/** @brief 默认关闭事件槽：接受关闭（对标 QWindow::closeEvent 默认行为）。 */
static void XWindow_defaultCloseEventSlot(XWindow* self, XEvent* event)
{
    (void)self;
    XEvent_accept(event);
}

/** @brief 默认原生事件槽：未处理。 */
static bool XWindow_defaultNativeEventSlot(XWindow* self, XEvent* event)
{
    (void)self;
    (void)event;
    return false;
}

/* ==================== 类初始化与生命周期 ==================== */

XVtable* XWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWindow)
    XVTABLE_INHERIT_XCLASS(XObject);
    void* events[] = {
        XWindow_defaultEventSlot,      /* ExposeEvent */
        XWindow_defaultEventSlot,      /* ResizeEvent */
        XWindow_defaultEventSlot,      /* PaintEvent */
        XWindow_defaultEventSlot,      /* MoveEvent */
        XWindow_defaultEventSlot,      /* FocusInEvent */
        XWindow_defaultEventSlot,      /* FocusOutEvent */
        XWindow_defaultEventSlot,      /* ShowEvent */
        XWindow_defaultEventSlot,      /* HideEvent */
        XWindow_defaultCloseEventSlot, /* CloseEvent */
        XWindow_defaultEventSlot,      /* KeyPressEvent */
        XWindow_defaultEventSlot,      /* KeyReleaseEvent */
        XWindow_defaultEventSlot,      /* MousePressEvent */
        XWindow_defaultEventSlot,      /* MouseReleaseEvent */
        XWindow_defaultEventSlot,      /* MouseDoubleClickEvent */
        XWindow_defaultEventSlot,      /* MouseMoveEvent */
        XWindow_defaultEventSlot,      /* WheelEvent */
        XWindow_defaultEventSlot,      /* TouchEvent */
        XWindow_defaultEventSlot,      /* TabletEvent */
        XWindow_defaultEventSlot,      /* InputMethodEvent */
        XWindow_defaultEventSlot,      /* DragEnterEvent */
        XWindow_defaultEventSlot,      /* DragMoveEvent */
        XWindow_defaultEventSlot,      /* DragLeaveEvent */
        XWindow_defaultEventSlot,      /* DropEvent */
        XWindow_defaultNativeEventSlot, /* NativeEvent */
        XWindow_defaultEventSlot,       /* EnterEvent */
        XWindow_defaultEventSlot        /* LeaveEvent */
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(events);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWindow_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXWindow_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXWindow_move);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXWindow_event);
    return XVTABLE_DEFAULT;
}

void XWindow_init(XWindow* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XWindow));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XWindow);
    ((XObject*)self)->is_window = 1;
    self->m_data = (XWindowPrivate*)XMalloc_System(sizeof(XWindowPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XWindowPrivate));
    XRegion_init(&self->m_data->m_mask);
#if XSURFACEFORMAT_ON
    self->m_data->m_format = XSurfaceFormat_create();
#else
    self->m_data->m_format = XWindow_mergeFormat(NULL);
#endif
    self->m_data->m_surfaceType = XWindowSurface_Raster;
    self->m_data->m_flags = XWindowType_Window;
    self->m_data->m_visibility = XWindowVisibility_Hidden;
    self->m_data->m_devicePixelRatio = 1.0f;
    self->m_data->m_contentOrientation = XScreenOrientation_Primary;
    self->m_data->m_opacity = 1.0f;
    self->m_data->m_minimumSize.width = 0;
    self->m_data->m_minimumSize.height = 0;
    self->m_data->m_maximumSize.width = XWINDOW_MAX_SIZE;
    self->m_data->m_maximumSize.height = XWINDOW_MAX_SIZE;
    self->m_data->m_baseSize.width = 0;
    self->m_data->m_baseSize.height = 0;
    self->m_data->m_sizeIncrement.width = 0;
    self->m_data->m_sizeIncrement.height = 0;
    self->m_data->m_positionAutomatic = true;
    self->m_data->m_modality = XWindowModality_NonModal;
#if XACCESSIBLE_ON
    self->m_data->m_accessibleRoot = XAccessible_createForWindow(self);
#endif
#if XGUIAPPLICATION_ON
    XGuiApplication_addWindow(self);
#endif
}

void XWindow_init_parent(XWindow* self, XWindow* parent)
{
    XWindow_init(self);
    if (!self || !self->m_data) return;
    XWindow_setParent(self, parent);
}

XWindow* XWindow_create_ex(XMemoryType memory)
{
    XWindow* self = (XWindow*)XMemory_malloc(sizeof(XWindow), memory);
    if (!self) return NULL;
    XWindow_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XWindow* XWindow_create_copy(const XWindow* other)
{
    XWindow* self;
    if (!other) return NULL;
    self = XWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XWindow_copy_base(self, other);
    return self;
}

XWindow* XWindow_create_move(XWindow* other)
{
    XWindow* self;
    if (!other) return NULL;
    self = XWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XWindow_move_base(self, other);
    return self;
}

static void VXWindow_deinit(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) {
        if (self) XClass_Deinit_Parent(XObject, (XObject*)self);
        return;
    }
#if XGUIAPPLICATION_ON
    /* XGuiApplication keeps borrowed window pointers.  Remove this window
       before releasing its private data so destruction cannot leave a stale
       entry in allWindows()/topLevelWindows(). */
    XGuiApplication_removeWindow(self);
#endif /* XGUIAPPLICATION_ON */
    if (data->m_title) XString_delete_base((XClass*)data->m_title);
    if (data->m_filePath) XString_delete_base((XClass*)data->m_filePath);
    if (data->m_icon) XIcon_delete_base(data->m_icon);
 #if XACCESSIBLE_ON
    XPlatformAccessibility_notifyWindow(XAccessibleEvent_ObjectDestroyed, self);
    if (data->m_accessibleRoot)
        XAccessible_delete_base(data->m_accessibleRoot);
 #endif
#if XCURSOR_ON
    XWindow_clearCursor(data);
#endif
    XRegion_deinit(&data->m_mask);
#if XPLATFORMNATIVEWINDOW_ON
    /* 安全网：窗口对象销毁时若仍挂接原生窗口，先归还平台资源（幂等）。 */
    if (data->m_nativeWindowAttached)
        XPlatformNativeWindow_destroy(self);
#endif /* XPLATFORMNATIVEWINDOW_ON */
    XFree_System(data);
    self->m_data = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXWindow_copy(XWindow* self, const XWindow* other)
{
    XWindowPrivate* source;
    if (!self || !other || self == other || !(source = other->m_data)) return;
    if (XClassIsVtableNull(self)) XWindow_init(self);
    if (!self->m_data) return;
    /* 先释放目标已有资源（与 XMovie_copy 模式一致）。 */
    if (self->m_data->m_title) XString_delete_base((XClass*)self->m_data->m_title);
    if (self->m_data->m_filePath) XString_delete_base((XClass*)self->m_data->m_filePath);
    if (self->m_data->m_icon) XIcon_delete_base(self->m_data->m_icon);
#if XCURSOR_ON
    XWindow_clearCursor(self->m_data);
#endif
    XRegion_deinit(&self->m_data->m_mask);
 #if XACCESSIBLE_ON
    if (self->m_data->m_accessibleRoot)
        XAccessible_delete_base(self->m_data->m_accessibleRoot);
 #endif
    memset(self->m_data, 0, sizeof(XWindowPrivate));
    XRegion_init(&self->m_data->m_mask);
    self->m_data->m_title = XWindow_copyString(source->m_title);
    self->m_data->m_filePath = XWindow_copyString(source->m_filePath);
    self->m_data->m_icon = XWindow_copyIcon(source->m_icon);
#if XCURSOR_ON
    self->m_data->m_cursor = XWindow_copyCursor(source->m_cursor);
#endif
    XRegion_copy(&source->m_mask, &self->m_data->m_mask);
    self->m_data->m_hasMask = source->m_hasMask;
    self->m_data->m_geometry = source->m_geometry;
    self->m_data->m_surfaceType = source->m_surfaceType;
    self->m_data->m_flags = source->m_flags;
    self->m_data->m_visible = source->m_visible;
    self->m_data->m_visibility = source->m_visibility;
    self->m_data->m_exposed = source->m_exposed;
    /* A copied window owns no native resources and must receive a fresh id
       only when it is subsequently created. */
    self->m_data->m_created = false;
    self->m_data->m_winId = 0;
    self->m_data->m_platform = NULL;
    self->m_data->m_nativeWindowAttached = false;
    self->m_data->m_modality = source->m_modality;
    self->m_data->m_windowStates = source->m_windowStates;
    self->m_data->m_positionAutomatic = source->m_positionAutomatic;
    self->m_data->m_active = source->m_active;
    self->m_data->m_closing = false;
    self->m_data->m_format = source->m_format;
    self->m_data->m_devicePixelRatio = source->m_devicePixelRatio;
    self->m_data->m_contentOrientation = source->m_contentOrientation;
    self->m_data->m_opacity = source->m_opacity;
    self->m_data->m_minimumSize = source->m_minimumSize;
    self->m_data->m_maximumSize = source->m_maximumSize;
    self->m_data->m_baseSize = source->m_baseSize;
    self->m_data->m_sizeIncrement = source->m_sizeIncrement;
    self->m_data->m_alertMsec = source->m_alertMsec;
    self->m_data->m_updateRequested = source->m_updateRequested;
 #if XACCESSIBLE_ON
    self->m_data->m_accessibleRoot = XAccessible_createForWindow(self);
 #endif
    /* 借用指针：父窗口/瞬态父窗口/屏幕均不深拷贝（Qt 拷贝构造不清父）。 */
    self->m_data->m_parentWindow = NULL;
    self->m_data->m_transientParent = NULL;
#if XSCREEN_ON
    self->m_data->m_screen = source->m_screen;
#endif
    ((XObject*)self)->is_window = 1;
}

static void VXWindow_move(XWindow* self, XWindow* other)
{
    bool otherWasRegistered = false;
    if (!self || !other || self == other || !other->m_data) return;
    if (XClassIsVtableNull(self)) XWindow_init(self);
    if (!self->m_data) return;
    VXWindow_deinit(self);
#if XGUIAPPLICATION_ON
    /* Move transfers the private state, so the application registry must
       transfer its borrowed pointer as well.  Otherwise the moved-from
       object (whose m_data becomes NULL) remains in allWindows(). */
    otherWasRegistered = XGuiApplication_replaceWindow(other, self);
#endif /* XGUIAPPLICATION_ON */
    self->m_data = other->m_data;
    other->m_data = NULL;
#if XPLATFORMWINDOW_ON
    if (self->m_data->m_platform)
        XPlatformWindow_setWindow((XPlatformWindow*)self->m_data->m_platform, self);
#endif /* XPLATFORMWINDOW_ON */
 #if XACCESSIBLE_ON
    if (self->m_data->m_accessibleRoot)
        XAccessible_delete_base(self->m_data->m_accessibleRoot);
    self->m_data->m_accessibleRoot = XAccessible_createForWindow(self);
    #endif
    ((XObject*)self)->is_window = 1;
#if XGUIAPPLICATION_ON
    (void)otherWasRegistered;
#endif /* XGUIAPPLICATION_ON */
}

/* ==================== 表面与窗口标识 ==================== */

void XWindow_setSurfaceType(XWindow* self, XWindowSurfaceType surfaceType)
{ if (self && self->m_data) self->m_data->m_surfaceType = surfaceType; }

XWindowSurfaceType XWindow_surfaceType(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_surfaceType
                                : XWindowSurface_Raster;
}

XWindowSurfaceClass XWindow_surfaceClass(const XWindow* self)
{
    (void)self;
    return XWindowSurfaceClass_Window;
}

bool XWindow_supportsOpenGL(const XWindow* self)
{
    XWindowSurfaceType type;
    if (!self || !self->m_data) return false;
    type = self->m_data->m_surfaceType;
    return type == XWindowSurface_OpenGL ||
           type == XWindowSurface_RasterGL;
}

/* ==================== 可见性与创建 ==================== */

bool XWindow_isVisible(const XWindow* self)
{ return self && self->m_data && self->m_data->m_visible; }

XWindowVisibility XWindow_visibility(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_visibility
                                : XWindowVisibility_Hidden;
}

void XWindow_setVisibility(XWindow* self, XWindowVisibility visibility)
{
    if (!self || !self->m_data) return;
    switch (visibility) {
    case XWindowVisibility_Hidden:     XWindow_hide(self); break;
    case XWindowVisibility_Automatic:  XWindow_show(self); break;
    case XWindowVisibility_Windowed:   XWindow_showNormal(self); break;
    case XWindowVisibility_Minimized:  XWindow_showMinimized(self); break;
    case XWindowVisibility_Maximized:  XWindow_showMaximized(self); break;
    case XWindowVisibility_FullScreen: XWindow_showFullScreen(self); break;
    default: break;
    }
}

void XWindow_createHandle(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data) || data->m_created) return;
    data->m_created = true;
#if XPLATFORMNATIVEWINDOW_ON
    /* 已挂接真实原生窗口（平台后端注入）时，创建系统窗口并接管真实 WId；
       创建失败/未挂接则继续回落嵌入式自增虚拟 WId 行为。 */
    if (data->m_platform && data->m_nativeWindowAttached) {
        if (XPlatformNativeWindow_create(self)) {
            data->m_winId = XPlatformNativeWindow_winId(self);
            /* 初始标题同步（后端 create 内已兜底，这里按公共层状态再同步，
               保证 create 之后用户先 setTitle 的时序正确）。 */
            XPlatformNativeWindow_setTitle(self, data->m_title);
        }
    }
#endif /* XPLATFORMNATIVEWINDOW_ON */
    if (data->m_winId == 0) data->m_winId = ++g_nextWinId;
#if XGUIAPPLICATION_ON
    XGuiApplication_addWindow(self);
#endif
#if XACCESSIBLE_ON
    XPlatformAccessibility_notifyWindow(XAccessibleEvent_ObjectCreated, self);
#endif
}

XWindowId XWindow_winId(const XWindow* self)
{
    XWindow* w = (XWindow*)self;
    if (!w || !w->m_data) return 0;
    if (!w->m_data->m_created) XWindow_createHandle(w);
    return w->m_data->m_winId;
}

void XWindow_destroy(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
#if XACCESSIBLE_ON
    XPlatformAccessibility_notifyWindow(XAccessibleEvent_ObjectDestroyed, self);
#endif
#if XPLATFORMNATIVEWINDOW_ON
    if (data->m_created && data->m_nativeWindowAttached)
        XPlatformNativeWindow_destroy(self); /* 幂等；销毁归平台注册表。 */
    if ((data->m_flags & XWindowType_TypeMask) == XWindowType_ForeignWindow)
        data->m_nativeWindowAttached = false;
#endif /* XPLATFORMNATIVEWINDOW_ON */
    data->m_created = false;
    data->m_winId = 0;
    data->m_exposed = false;
}

/* ==================== 父窗口与顶层判断 ==================== */

void XWindow_setParent(XWindow* self, XWindow* parent)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    /* Qt 6.8：Desktop 类型父窗口视为 NULL（使其成为顶层）。 */
    if (parent && XWindow_type(parent) == XWindowType_Desktop)
        parent = NULL;
    if (data->m_parentWindow == parent) return;
    data->m_parentWindow = parent;
    XObject_setParent((XObject*)self, (XObject*)parent);
    /* 可见且（父为 NULL 或父已创建平台句柄）时重新应用可见性。 */
    if (data->m_visible && (!parent ||
        (parent->m_data && parent->m_data->m_created)))
        XWindow_setVisible(self, true);
}

XWindow* XWindow_parent(const XWindow* self, XWindowAncestorMode mode)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return NULL;
    if (mode == XWindowAncestor_IncludeTransients && !data->m_parentWindow)
        return data->m_transientParent;
    return data->m_parentWindow;
}

bool XWindow_isTopLevel(const XWindow* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_parentWindow == NULL &&
           self->m_data->m_transientParent == NULL;
}

bool XWindow_isAncestorOf(const XWindow* self, const XWindow* child,
                          XWindowAncestorMode mode)
{
    const XWindow* walk;
    if (!self || !child) return false;
    for (walk = XWindow_parent(child, mode); walk;
         walk = XWindow_parent(walk, mode)) {
        if (walk == self) return true;
    }
    return false;
}

/* ==================== 模态 ==================== */

bool XWindow_isModal(const XWindow* self)
{
    return self && self->m_data &&
           self->m_data->m_modality != XWindowModality_NonModal;
}

XWindowModality XWindow_modality(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_modality
                                : XWindowModality_NonModal;
}

void XWindow_setModality(XWindow* self, XWindowModality modality)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data) || data->m_modality == modality) return;
    data->m_modality = modality;
    XWindow_modalityChanged_signal(self, modality);
}

/* ==================== 表面格式 ==================== */

void XWindow_setFormat(XWindow* self, const XSurfaceFormat* format)
{
    if (!self || !self->m_data) return;
#if XSURFACEFORMAT_ON
    self->m_data->m_format = format ? *format : XSurfaceFormat_defaultFormat();
#else
    self->m_data->m_format = XWindow_mergeFormat(format);
#endif
}

XSurfaceFormat XWindow_format(const XWindow* self)
{
    if (!self || !self->m_data) return XWindow_mergeFormat(NULL);
    return XWindow_mergeFormat(&self->m_data->m_format);
}

XSurfaceFormat XWindow_requestedFormat(const XWindow* self)
{
    if (!self || !self->m_data) return XWindow_mergeFormat(NULL);
    return self->m_data->m_format;
}

/* ==================== 窗口标志与类型 ==================== */

void XWindow_setFlags(XWindow* self, XWindowFlags flags)
{ if (self && self->m_data) self->m_data->m_flags = flags; }

XWindowFlags XWindow_flags(const XWindow* self)
{ return self && self->m_data ? self->m_data->m_flags : 0; }

void XWindow_setFlag(XWindow* self, XWindowType flag, bool on)
{
    if (!self || !self->m_data) return;
    if (on)
        self->m_data->m_flags |= (XWindowFlags)flag;
    else
        self->m_data->m_flags &= (XWindowFlags)~flag;
}

XWindowType XWindow_type(const XWindow* self)
{
    return self && self->m_data
        ? (XWindowType)(self->m_data->m_flags & XWindowType_TypeMask)
        : XWindowType_Widget;
}

/* ==================== 标题 ==================== */

XString* XWindow_title(const XWindow* self)
{
    const XString* value = self && self->m_data
                               ? self->m_data->m_title : NULL;
    return value ? XString_create_copy(value) : XString_create();
}

void XWindow_setTitle(XWindow* self, const XString* title)
{
    XWindowPrivate* data;
    const XString* old;
    if (!self || !(data = self->m_data)) return;
    old = data->m_title;
    if ((old == NULL) == (title == NULL)) {
        if (!old) return;
        if (XString_equals(old, title, XChar_CaseSensitive)) return;
    }
    XWindow_setString(&data->m_title, title);
    XWindow_windowTitleChanged_signal(self, data->m_title);
#if XACCESSIBLE_ON
    /* 设置标题通常早于 createHandle；此时不应为无障碍路径查询强制创建
       虚拟 WId，否则随后挂接平台窗口会错过真实原生句柄。 */
    if (data->m_created)
        XPlatformAccessibility_notifyWindow(XAccessibleEvent_NameChanged, self);
#endif
#if XPLATFORMNATIVEWINDOW_ON
    if (data->m_created && data->m_nativeWindowAttached)
        XPlatformNativeWindow_setTitle(self, data->m_title);
#endif /* XPLATFORMNATIVEWINDOW_ON */
}

void XWindow_setTitle_2(XWindow* self, const char* title)
{
    XString* value = title ? XString_create_utf8(title) : NULL;
    XWindow_setTitle(self, value);
    if (value) XString_delete_base((XClass*)value);
}

/* ==================== 透明度与遮罩 ==================== */

void XWindow_setOpacity(XWindow* self, float level)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    if (XWindow_floatNear(level, data->m_opacity)) return;
    data->m_opacity = level;
    XWindow_opacityChanged_signal(self, level);
}

float XWindow_opacity(const XWindow* self)
{ return self && self->m_data ? self->m_data->m_opacity : 1.0f; }

void XWindow_setMask(XWindow* self, const XRegion* region)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (region) {
        XRegion_copy(region, &data->m_mask);
        data->m_hasMask = true;
    } else {
        XRegion_clear(&data->m_mask);
        data->m_hasMask = false;
    }
}

void XWindow_mask(const XWindow* self, XRegion* out)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) {
        if (out) XRegion_clear(out);
        return;
    }
    if (!out) return;
    XRegion_clear(out);
    if (data->m_hasMask) XRegion_copy(&data->m_mask, out);
}

/* ==================== 激活 / 内容方向 / 设备像素比 ==================== */

bool XWindow_isActive(const XWindow* self)
{ return self && self->m_data && self->m_data->m_active; }

void XWindow_reportContentOrientationChange(XWindow* self,
                                            XScreenOrientation orientation)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data) ||
        data->m_contentOrientation == orientation) return;
    data->m_contentOrientation = orientation;
    XWindow_contentOrientationChanged_signal(self, orientation);
}

XScreenOrientation XWindow_contentOrientation(const XWindow* self)
{
    return self && self->m_data
        ? self->m_data->m_contentOrientation
        : XScreenOrientation_Primary;
}

float XWindow_devicePixelRatio(const XWindow* self)
{ return self && self->m_data ? self->m_data->m_devicePixelRatio : 1.0f; }

/* ==================== 窗口状态 ==================== */

XWindowState XWindow_windowState(const XWindow* self)
{
    return self && self->m_data ? XWindow_effectiveState(self->m_data)
                                : XWindowState_NoState;
}

XWindowStates XWindow_windowStates(const XWindow* self)
{ return self && self->m_data ? self->m_data->m_windowStates : 0; }

void XWindow_setWindowState(XWindow* self, XWindowState state)
{ XWindow_setWindowStates(self, (XWindowStates)state); }

void XWindow_setWindowStates(XWindow* self, XWindowStates states)
{
    XWindowPrivate* data;
    XWindowState before;
    XWindowState after;
    if (!self || !(data = self->m_data)) return;
    /* Qt 6.8：WindowActive 位不可写，清除并忽略。 */
    if (states & XWindowState_Active) {
        states &= (XWindowStates)~XWindowState_Active;
        /* 复用一个既有告警通道：错误日志。 */
        XERROR_PRINTF("XWindow::setWindowStates: ignore WindowActive\n");
    }
    before = XWindow_effectiveState(data);
    data->m_windowStates = states;
    after = XWindow_effectiveState(data);
    if (after != before)
        XWindow_windowStateChanged_signal(self, after);
    XWindow_updateVisibility(self);
}

/* ==================== 瞬态父窗口 ==================== */

void XWindow_setTransientParent(XWindow* self, XWindow* parent)
{
    XWindowPrivate* data;
    bool changed;
    if (!self || !(data = self->m_data)) return;
    /* Qt 6.8：只拒绝「非顶层」与「自身」。 */
    if (parent && (!XWindow_isTopLevel(parent) || parent == self)) {
        XERROR_PRINTF("XWindow::setTransientParent: invalid parent\n");
        return;
    }
    changed = data->m_transientParent != parent;
    data->m_transientParent = parent;
    /* Qt 6.8：无条件发射 transientParentChanged。 */
    XWindow_transientParentChanged_signal(self, parent);
    (void)changed;
}

XWindow* XWindow_transientParent(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_transientParent : NULL;
}

bool XWindow_isExposed(const XWindow* self)
{ return self && self->m_data && self->m_data->m_exposed; }

void XWindow_setExposed(XWindow* self, bool exposed)
{
    if (!self || !self->m_data) return;
    self->m_data->m_exposed = exposed;
}

/* ==================== 尺寸约束 ==================== */

XSize XWindow_minimumSize(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_minimumSize
                                : (XSize){0, 0};
}

XSize XWindow_maximumSize(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_maximumSize
                                : (XSize){0, 0};
}

XSize XWindow_baseSize(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_baseSize
                                : (XSize){0, 0};
}

XSize XWindow_sizeIncrement(const XWindow* self)
{
    return self && self->m_data ? self->m_data->m_sizeIncrement
                                : (XSize){0, 0};
}

int XWindow_minimumWidth(const XWindow* self)
{ return XWindow_minimumSize(self).width; }
int XWindow_minimumHeight(const XWindow* self)
{ return XWindow_minimumSize(self).height; }
int XWindow_maximumWidth(const XWindow* self)
{ return XWindow_maximumSize(self).width; }
int XWindow_maximumHeight(const XWindow* self)
{ return XWindow_maximumSize(self).height; }

/** @brief 设置尺寸约束共用实现（对标 QWindow::setMinimumSize/setMaximumSize）。 */
static void XWindow_setMinMaxSize(XWindow* self, const XSize* minimum,
                                  const XSize* maximum)
{
    XWindowPrivate* data;
    XSize min;
    XSize max;
    if (!self || !(data = self->m_data)) return;
    /* 未给出的维度保留当前值：setMinimumSize/setMaximumSize 互不影响
       （对标 Qt）；NULL 的公开语义由封装层展开为 (0,0)/QWINDOWSIZE_MAX。 */
    min = minimum ? *minimum : data->m_minimumSize;
    max = maximum ? *maximum : data->m_maximumSize;
    /* expandedTo(0,0) 语义：负值裁剪为 0；boundedTo(max) 语义。 */
    if (min.width < 0) min.width = 0;
    if (min.height < 0) min.height = 0;
    if (max.width > XWINDOW_MAX_SIZE) max.width = XWINDOW_MAX_SIZE;
    if (max.height > XWINDOW_MAX_SIZE) max.height = XWINDOW_MAX_SIZE;
    if (max.width < min.width) max.width = min.width;
    if (max.height < min.height) max.height = min.height;

    if (min.width != data->m_minimumSize.width) {
        data->m_minimumSize.width = min.width;
        XWindow_minimumWidthChanged_signal(self, min.width);
    }
    if (min.height != data->m_minimumSize.height) {
        data->m_minimumSize.height = min.height;
        XWindow_minimumHeightChanged_signal(self, min.height);
    }
    if (max.width != data->m_maximumSize.width) {
        data->m_maximumSize.width = max.width;
        XWindow_maximumWidthChanged_signal(self, max.width);
    }
    if (max.height != data->m_maximumSize.height) {
        data->m_maximumSize.height = max.height;
        XWindow_maximumHeightChanged_signal(self, max.height);
    }
    /* 当前几何越界时按 Qt 语义自动 resize 夹紧。 */
    if (data->m_geometry.width < data->m_minimumSize.width ||
        data->m_geometry.height < data->m_minimumSize.height ||
        data->m_geometry.width > data->m_maximumSize.width ||
        data->m_geometry.height > data->m_maximumSize.height) {
        XSize current = {data->m_geometry.width, data->m_geometry.height};
        if (current.width < data->m_minimumSize.width)
            current.width = data->m_minimumSize.width;
        if (current.height < data->m_minimumSize.height)
            current.height = data->m_minimumSize.height;
        if (current.width > data->m_maximumSize.width)
            current.width = data->m_maximumSize.width;
        if (current.height > data->m_maximumSize.height)
            current.height = data->m_maximumSize.height;
        XWindow_resize_2(self, current.width, current.height);
    }
}

void XWindow_setMinimumSize(XWindow* self, const XSize* size)
{
    XSize zero = {0, 0};
    if (!self || !self->m_data) return;
    XWindow_setMinMaxSize(self, size ? size : &zero, NULL);
}

void XWindow_setMaximumSize(XWindow* self, const XSize* size)
{
    XSize maxSize = {XWINDOW_MAX_SIZE, XWINDOW_MAX_SIZE};
    if (!self || !self->m_data) return;
    XWindow_setMinMaxSize(self, NULL, size ? size : &maxSize);
}

void XWindow_setBaseSize(XWindow* self, const XSize* size)
{
    if (!self || !self->m_data) return;
    self->m_data->m_baseSize = size ? *size : (XSize){0, 0};
}

void XWindow_setSizeIncrement(XWindow* self, const XSize* size)
{
    if (!self || !self->m_data) return;
    self->m_data->m_sizeIncrement = size ? *size : (XSize){0, 0};
}

void XWindow_setMinimumWidth(XWindow* self, int w)
{ if (self && self->m_data) XWindow_setMinimumSize(self, &(XSize){w, self->m_data->m_minimumSize.height}); }
void XWindow_setMinimumHeight(XWindow* self, int h)
{ if (self && self->m_data) XWindow_setMinimumSize(self, &(XSize){self->m_data->m_minimumSize.width, h}); }
void XWindow_setMaximumWidth(XWindow* self, int w)
{ if (self && self->m_data) XWindow_setMaximumSize(self, &(XSize){w, self->m_data->m_maximumSize.height}); }
void XWindow_setMaximumHeight(XWindow* self, int h)
{ if (self && self->m_data) XWindow_setMaximumSize(self, &(XSize){self->m_data->m_maximumSize.width, h}); }

/* ==================== 几何与坐标 ==================== */

XRect XWindow_geometry(const XWindow* self)
{ return self && self->m_data ? self->m_data->m_geometry
                              : (XRect){0, 0, 0, 0}; }

XMargins XWindow_frameMargins(const XWindow* self)
{
    (void)self;
    return (XMargins){0, 0, 0, 0};
}

XRect XWindow_frameGeometry(const XWindow* self)
{ return XWindow_geometry(self); }

XPoint XWindow_framePosition(const XWindow* self)
{ return (XPoint){XWindow_x(self), XWindow_y(self)}; }

void XWindow_setFramePosition(XWindow* self, const XPoint* point)
{ XWindow_setPosition(self, point); }

int XWindow_width(const XWindow* self)
{ return XWindow_geometry(self).width; }
int XWindow_height(const XWindow* self)
{ return XWindow_geometry(self).height; }
int XWindow_x(const XWindow* self)
{ return XWindow_geometry(self).x; }
int XWindow_y(const XWindow* self)
{ return XWindow_geometry(self).y; }

XSize XWindow_size(const XWindow* self)
{
    XRect geometry = XWindow_geometry(self);
    return (XSize){geometry.width, geometry.height};
}

XPoint XWindow_position(const XWindow* self)
{
    XRect geometry = XWindow_geometry(self);
    return (XPoint){geometry.x, geometry.y};
}

/** @brief 按位置/尺寸更新几何并发射逐字段变化信号（Qt 缺省无平台实现）。 */
static void XWindow_setGeometryFields(XWindow* self, int posx, int posy,
                                      int w, int h)
{
    XWindowPrivate* data;
    bool changed = false;
    if (!self || !(data = self->m_data)) return;
    if (posx != data->m_geometry.x) {
        data->m_geometry.x = posx;
        changed = true;
        XWindow_xChanged_signal(self, posx);
    }
    if (posy != data->m_geometry.y) {
        data->m_geometry.y = posy;
        changed = true;
        XWindow_yChanged_signal(self, posy);
    }
    if (w != data->m_geometry.width) {
        data->m_geometry.width = w;
        changed = true;
        XWindow_widthChanged_signal(self, w);
    }
    if (h != data->m_geometry.height) {
        data->m_geometry.height = h;
        changed = true;
        XWindow_heightChanged_signal(self, h);
    }
#if XPLATFORMNATIVEWINDOW_ON
    /* 原生窗口同步：后端按自身记录去重，杜绝 ConfigureNotify/WM_SIZE 回环。 */
    if (data->m_created && data->m_nativeWindowAttached) {
        XRect nativeGeom;
        nativeGeom.x = posx;
        nativeGeom.y = posy;
        nativeGeom.width = w;
        nativeGeom.height = h;
        XPlatformNativeWindow_setGeometry(self, &nativeGeom);
    }
#endif /* XPLATFORMNATIVEWINDOW_ON */
#if XACCESSIBLE_ON
    if (changed && data->m_created)
        XPlatformAccessibility_notifyWindow(XAccessibleEvent_LocationChanged,
                                            self);
#endif
}

void XWindow_setPosition(XWindow* self, const XPoint* pt)
{
    XRect geometry = XWindow_geometry(self);
    int x = pt ? pt->x : 0;
    int y = pt ? pt->y : 0;
    XWindow_setGeometry(self, x, y, geometry.width, geometry.height);
}

void XWindow_setPosition_2(XWindow* self, int posx, int posy)
{ XWindow_setGeometry(self, posx, posy, XWindow_width(self), XWindow_height(self)); }

void XWindow_resize(XWindow* self, const XSize* newSize)
{
    int w = newSize ? newSize->width : 0;
    int h = newSize ? newSize->height : 0;
    XWindow_setGeometryFields(self, XWindow_x(self), XWindow_y(self), w, h);
}

void XWindow_resize_2(XWindow* self, int w, int h)
{ XWindow_setGeometryFields(self, XWindow_x(self), XWindow_y(self), w, h); }

void XWindow_setGeometry(XWindow* self, int posx, int posy, int w, int h)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    data->m_positionAutomatic = false;
    XWindow_setGeometryFields(self, posx, posy, w, h);
}

void XWindow_setGeometry_rect(XWindow* self, const XRect* rect)
{
    XRect geometry = rect ? *rect : (XRect){0, 0, 0, 0};
    XWindow_setGeometry(self, geometry.x, geometry.y,
                        geometry.width, geometry.height);
}

void XWindow_setX(XWindow* self, int value)
{ XWindow_setGeometry(self, value, XWindow_y(self), XWindow_width(self), XWindow_height(self)); }
void XWindow_setY(XWindow* self, int value)
{ XWindow_setGeometry(self, XWindow_x(self), value, XWindow_width(self), XWindow_height(self)); }
void XWindow_setWidth(XWindow* self, int value)
{ XWindow_setGeometry(self, XWindow_x(self), XWindow_y(self), value, XWindow_height(self)); }
void XWindow_setHeight(XWindow* self, int value)
{ XWindow_setGeometry(self, XWindow_x(self), XWindow_y(self), XWindow_width(self), value); }

/* ==================== 坐标映射 ==================== */

XPoint XWindow_mapToGlobal(const XWindow* self, const XPoint* pos)
{
    XPoint offset = XWindow_globalOffset(self);
    XPoint p = pos ? *pos : (XPoint){0, 0};
    p.x += offset.x;
    p.y += offset.y;
    return p;
}

XPoint XWindow_mapFromGlobal(const XWindow* self, const XPoint* pos)
{
    XPoint offset = XWindow_globalOffset(self);
    XPoint p = pos ? *pos : (XPoint){0, 0};
    p.x -= offset.x;
    p.y -= offset.y;
    return p;
}

XPointF XWindow_mapToGlobal_f(const XWindow* self, const XPointF* pos)
{
    XPoint offset = XWindow_globalOffset(self);
    XPointF p = pos ? *pos : (XPointF){0.0f, 0.0f};
    p.x += (float)offset.x;
    p.y += (float)offset.y;
    return p;
}

XPointF XWindow_mapFromGlobal_f(const XWindow* self, const XPointF* pos)
{
    XPoint offset = XWindow_globalOffset(self);
    XPointF p = pos ? *pos : (XPointF){0.0f, 0.0f};
    p.x -= (float)offset.x;
    p.y -= (float)offset.y;
    return p;
}

/* ==================== 文件路径 / 图标 ==================== */

void XWindow_setFilePath(XWindow* self, const XString* filePath)
{ if (self && self->m_data) XWindow_setString(&self->m_data->m_filePath, filePath); }

void XWindow_setFilePath_2(XWindow* self, const char* filePath)
{
    XString* value = filePath ? XString_create_utf8(filePath) : NULL;
    XWindow_setFilePath(self, value);
    if (value) XString_delete_base((XClass*)value);
}

XString* XWindow_filePath(const XWindow* self)
{
    const XString* value = self && self->m_data
                               ? self->m_data->m_filePath : NULL;
    return value ? XString_create_copy(value) : XString_create();
}

void XWindow_setIcon(XWindow* self, const XIcon* icon)
{ if (self && self->m_data) XWindow_setIconInternal(&self->m_data->m_icon, icon); }

XIcon* XWindow_icon(const XWindow* self)
{
    XIcon* value = self && self->m_data ? self->m_data->m_icon : NULL;
    return XWindow_copyIcon(value);
}

/* ==================== 平台句柄 / 抓取 ==================== */

XWindowPlatform* XWindow_handle(const XWindow* self)
{ return self && self->m_data ? self->m_data->m_platform : NULL; }

void XWindow_setHandle(XWindow* self, XWindowPlatform* handle)
{ if (self && self->m_data) self->m_data->m_platform = handle; }

void XWindow_setNativeWindowAttached(XWindow* self, bool attached)
{ if (self && self->m_data) self->m_data->m_nativeWindowAttached = attached; }

bool XWindow_attachForeignHandle(XWindow* self, XWindowId nativeId)
{
#if XPLATFORMNATIVEWINDOW_ON
    XWindowPrivate* data;
    if (!self || !(data = self->m_data) || nativeId == 0) return false;
    /* 已创建的窗口已经拥有平台资源，不能在其上重绑外部句柄。 */
    if (data->m_created) return false;
    if (!XPlatformNativeWindow_attachForeign(self, nativeId)) return false;
    data->m_flags = XWindowType_ForeignWindow;
    data->m_created = true;
    data->m_winId = nativeId;
    data->m_nativeWindowAttached = true;
#if XGUIAPPLICATION_ON
    XGuiApplication_addWindow(self);
#endif
#if XACCESSIBLE_ON
    XPlatformAccessibility_notifyWindow(XAccessibleEvent_ObjectCreated, self);
#endif
    return true;
#else
    (void)self; (void)nativeId;
    return false;
#endif
}

bool XWindow_setKeyboardGrabEnabled(XWindow* self, bool grab)
{
#if XPLATFORMNATIVEWINDOW_ON
    if (self && self->m_data && self->m_data->m_nativeWindowAttached)
        return XPlatformNativeWindow_setKeyboardGrabEnabled(self, grab);
#else
    (void)self; (void)grab;
#endif /* XPLATFORMNATIVEWINDOW_ON */
    return false; /* Qt：平台不支持或窗口尚未挂接真实句柄。 */
}

bool XWindow_setMouseGrabEnabled(XWindow* self, bool grab)
{
#if XPLATFORMNATIVEWINDOW_ON
    if (self && self->m_data && self->m_data->m_nativeWindowAttached)
        return XPlatformNativeWindow_setMouseGrabEnabled(self, grab);
#else
    (void)self; (void)grab;
#endif /* XPLATFORMNATIVEWINDOW_ON */
    return false; /* Qt：平台不支持或窗口尚未挂接真实句柄。 */
}

/* ==================== 屏幕归属 ==================== */

#if XSCREEN_ON
XScreen* XWindow_screen(const XWindow* self)
{
    if (!self || !self->m_data) return NULL;
    if (self->m_data->m_screen) return self->m_data->m_screen;
    return XScreen_primaryScreen();
}

void XWindow_setScreen(XWindow* self, XScreen* screen)
{
    XWindowPrivate* data;
    XScreen* old;
    if (!self || !(data = self->m_data)) return;
    /* Qt 6.8：NULL 回退主屏幕；无主屏幕时 no-op。 */
    if (!screen) {
        screen = XScreen_primaryScreen();
        if (!screen) return;
    }
    old = data->m_screen;
    if (old == screen) return;
    /* Qt 6.8：只有顶层窗口可以设置屏幕（setTopLevelScreen 语义）。 */
    if (data->m_parentWindow != NULL) {
        XERROR_PRINTF("XWindow::setScreen: only top-level windows\n");
        return;
    }
    data->m_screen = screen;
    XWindow_screenChanged_signal(self, screen);
    /* 屏幕变化后同步设备像素比。 */
    data->m_devicePixelRatio = XScreen_devicePixelRatio(screen);
}
#else
XScreen* XWindow_screen(const XWindow* self)
{
    (void)self;
    return NULL;
}

void XWindow_setScreen(XWindow* self, XScreen* screen)
{
    (void)self;
    (void)screen;
}
#endif /* XSCREEN_ON */

/* ==================== 光标 ==================== */

#if XCURSOR_ON
XCursor* XWindow_cursor(const XWindow* self)
{
    XCursor* value = self && self->m_data ? self->m_data->m_cursor : NULL;
    return XWindow_copyCursor(value);
}

void XWindow_setCursor(XWindow* self, const XCursor* cursor)
{ if (self && self->m_data) XWindow_setCursorInternal(&self->m_data->m_cursor, cursor); }

void XWindow_unsetCursor(XWindow* self)
{ if (self && self->m_data) XWindow_setCursorInternal(&self->m_data->m_cursor, NULL); }
#else
XCursor* XWindow_cursor(const XWindow* self)
{
    (void)self;
    return NULL;
}

void XWindow_setCursor(XWindow* self, const XCursor* cursor)
{
    (void)self;
    (void)cursor;
}

void XWindow_unsetCursor(XWindow* self)
{ (void)self; }
#endif /* XCURSOR_ON */

/* ==================== 通过窗口 id 查找 ==================== */

XWindow* XWindow_fromWinId(XWindowId id)
{
#if XPLATFORMNATIVEWINDOW_ON
    /* 转发平台注册表反查（X11 Window / Win32 HWND）。 */
    return XPlatformNativeWindow_windowForWinId(id);
#else /* !XPLATFORMNATIVEWINDOW_ON */
    (void)id;
    /* 无平台后端：进程内不维护窗口注册表，恒返回 NULL（Qt 无平台时同理）。 */
    return NULL;
#endif /* XPLATFORMNATIVEWINDOW_ON */
}

/* ==================== 显示 / 隐藏 / 状态显示 ==================== */

void XWindow_requestActivate(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    /* Qt 6.8：WindowDoesNotAcceptFocus 标志忽略激活请求。 */
    if (data->m_flags & XWindowType_WindowDoesNotAcceptFocus) return;
#if XPLATFORMNATIVEWINDOW_ON
    if (data->m_nativeWindowAttached)
        (void)XPlatformNativeWindow_requestActivate(self);
#endif /* XPLATFORMNATIVEWINDOW_ON */
    data->m_active = true;
#if XGUIAPPLICATION_ON
    XGuiApplication_setFocusWindow(self, NULL);
#endif /* XGUIAPPLICATION_ON */
}

void XWindow_setVisible(XWindow* self, bool visible)
{
    XWindowPrivate* data;
    bool old;
    if (!self || !(data = self->m_data)) return;
    old = data->m_visible;
    if (visible != old) {
        data->m_visible = visible;
        XWindow_visibleChanged_signal(self, visible);
        XWindow_updateVisibility(self);
    } else if (self->m_data->m_platform) {
        return; /* 已处于目标状态的可见性不重复处理。 */
    }
    /* 显示时若原生窗口尚未创建则延迟创建（挂接/未挂接统一入口）。
       已挂接真实原生窗口的可见性变化在下方统一同步给平台后端。 */
    if (!self->m_data->m_created) {
        if (data->m_parentWindow &&
            (!data->m_parentWindow->m_data ||
             !data->m_parentWindow->m_data->m_created))
            return;
        if (visible) XWindow_createHandle(self);
    }
    if (visible) {
        XWindow_sendEvent(self, XEVENT_TYPE_SHOW);
        data->m_exposed = true;
    } else {
        XWindow_sendEvent(self, XEVENT_TYPE_HIDE);
        data->m_exposed = false;
    }
#if XPLATFORMNATIVEWINDOW_ON
    if (data->m_created && data->m_nativeWindowAttached)
        XPlatformNativeWindow_setVisible(self, visible);
#endif /* XPLATFORMNATIVEWINDOW_ON */
#if XACCESSIBLE_ON
    if (old != visible)
        XPlatformAccessibility_notifyWindow(XAccessibleEvent_StateChanged,
                                            self);
#endif
}

void XWindow_show(XWindow* self)
{
    if (!self || !self->m_data) return;
    /* 简化：无平台后端时顶层与子窗口均走 showNormal。 */
    XWindow_showNormal(self);
}

void XWindow_hide(XWindow* self)
{ XWindow_setVisible(self, false); }

void XWindow_showMinimized(XWindow* self)
{
    if (!self || !self->m_data) return;
    XWindow_setWindowStates(self, XWindowState_Minimized);
    XWindow_setVisible(self, true);
}

void XWindow_showMaximized(XWindow* self)
{
    if (!self || !self->m_data) return;
    XWindow_setWindowStates(self, XWindowState_Maximized);
    XWindow_setVisible(self, true);
}

void XWindow_showFullScreen(XWindow* self)
{
    if (!self || !self->m_data) return;
    XWindow_setWindowStates(self, XWindowState_FullScreen);
    XWindow_setVisible(self, true);
    XWindow_requestActivate(self);
}

void XWindow_showNormal(XWindow* self)
{
    if (!self || !self->m_data) return;
    XWindow_setWindowStates(self, XWindowState_NoState);
    XWindow_setVisible(self, true);
}

bool XWindow_close(XWindow* self)
{
    XWindowPrivate* data;
    bool accepted;
    if (!self || !(data = self->m_data)) return false;
    /* Qt 6.8：重入保护（嵌套 close 直接返回 true）。 */
    if (data->m_closing) return true;
    /* Qt 6.8：非顶层窗口不处理关闭。 */
    if (!XWindow_isTopLevel(self)) return false;
    /* Qt 6.8：无平台窗口直接返回 true，不派发 CloseEvent。 */
    if (!data->m_platform) return true;
    XEvent* event = XEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XEVENT_TYPE_CLOSE);
    if (!event) return false;
    data->m_closing = true;
    XCoreApplication_sendEvent((XObject*)self, (XEvent*)event);
    accepted = XEvent_isAccepted(event);
    XEvent_delete_base((XEvent*)event);
    data->m_closing = false;
    if (accepted) {
        XWindow_hide(self);
        XWindow_destroy(self);
    }
    return true;
}

void XWindow_raise(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    data->m_active = true;
}

void XWindow_lower(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return;
    data->m_active = false;
}

/** @brief 判断边缘组合是否为 Qt 合法 resize 边缘（单边或两条直角邻边）。 */
static bool XWindow_validResizeEdges(XWindowEdges edges)
{
    int top = (edges & XWindowEdge_Top) ? 1 : 0;
    int left = (edges & XWindowEdge_Left) ? 1 : 0;
    int right = (edges & XWindowEdge_Right) ? 1 : 0;
    int bottom = (edges & XWindowEdge_Bottom) ? 1 : 0;
    int count = top + left + right + bottom;
    if (count == 0 || count > 2) return false;
    if (count == 1) return true;
    /* 两条边必须是直角邻边：不允许对边（Top|Bottom / Left|Right）。 */
    if ((edges & XWindowEdge_Top) && (edges & XWindowEdge_Bottom)) return false;
    if ((edges & XWindowEdge_Left) && (edges & XWindowEdge_Right)) return false;
    return true;
}

bool XWindow_startSystemResize(XWindow* self, XWindowEdges edges)
{
    XWindowPrivate* data;
    bool minMaxOk;
    if (!self || !(data = self->m_data)) return false;
    if (!XWindow_isVisible(self) || !data->m_platform) return false;
    if (!XWindow_validResizeEdges(edges)) return false;
    minMaxOk = data->m_minimumSize.width != data->m_maximumSize.width ||
               data->m_minimumSize.height != data->m_maximumSize.height;
    return minMaxOk;
}

bool XWindow_startSystemMove(XWindow* self)
{
    XWindowPrivate* data;
    if (!self || !(data = self->m_data)) return false;
    return XWindow_isVisible(self) && data->m_platform != NULL;
}

void XWindow_alert(XWindow* self, int msec)
{
    if (!self || !self->m_data) return;
    self->m_data->m_alertMsec = msec;
}

void XWindow_requestUpdate(XWindow* self)
{
    if (!self || !self->m_data) return;
    self->m_data->m_updateRequested = true;
}

void* XWindow_accessibleRoot(const XWindow* self)
{
#if XACCESSIBLE_ON
    return self && self->m_data ? self->m_data->m_accessibleRoot : NULL;
#else
    (void)self;
    return NULL;
#endif
}

XObject* XWindow_focusObject(const XWindow* self)
{
    if (!self) return NULL;
    return (XObject*)self;
}

/* ==================== 通知信号（19 个，对标 QWindow 全部信号） ==================== */

void* XWindow_screenChanged_signal(XWindow* self, XScreen* screen)
{
    if (!self) return (void*)(size_t)XWindow_screenChanged_signal;
    XWindow_emit(self, (size_t)XWindow_screenChanged_signal,
                 XVarList_Create(XVar(XScreen*, screen)));
    return (void*)(size_t)XWindow_screenChanged_signal;
}

void* XWindow_modalityChanged_signal(XWindow* self, XWindowModality modality)
{
    if (!self) return (void*)(size_t)XWindow_modalityChanged_signal;
    XWindow_emit(self, (size_t)XWindow_modalityChanged_signal,
                 XVarList_Create(XVar(XWindowModality, modality)));
    return (void*)(size_t)XWindow_modalityChanged_signal;
}

void* XWindow_windowStateChanged_signal(XWindow* self, XWindowState windowState)
{
    if (!self) return (void*)(size_t)XWindow_windowStateChanged_signal;
    XWindow_emit(self, (size_t)XWindow_windowStateChanged_signal,
                 XVarList_Create(XVar(XWindowState, windowState)));
    return (void*)(size_t)XWindow_windowStateChanged_signal;
}

void* XWindow_windowTitleChanged_signal(XWindow* self, const XString* title)
{
    XString* value;
    if (!self) return (void*)(size_t)XWindow_windowTitleChanged_signal;
    value = (XString*)title;
    XWindow_emit(self, (size_t)XWindow_windowTitleChanged_signal,
                 XVarList_Create(XVar(XString*, value)));
    return (void*)(size_t)XWindow_windowTitleChanged_signal;
}

void* XWindow_xChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_xChanged_signal;
    XWindow_emit(self, (size_t)XWindow_xChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_xChanged_signal;
}

void* XWindow_yChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_yChanged_signal;
    XWindow_emit(self, (size_t)XWindow_yChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_yChanged_signal;
}

void* XWindow_widthChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_widthChanged_signal;
    XWindow_emit(self, (size_t)XWindow_widthChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_widthChanged_signal;
}

void* XWindow_heightChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_heightChanged_signal;
    XWindow_emit(self, (size_t)XWindow_heightChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_heightChanged_signal;
}

void* XWindow_minimumWidthChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_minimumWidthChanged_signal;
    XWindow_emit(self, (size_t)XWindow_minimumWidthChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_minimumWidthChanged_signal;
}

void* XWindow_minimumHeightChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_minimumHeightChanged_signal;
    XWindow_emit(self, (size_t)XWindow_minimumHeightChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_minimumHeightChanged_signal;
}

void* XWindow_maximumWidthChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_maximumWidthChanged_signal;
    XWindow_emit(self, (size_t)XWindow_maximumWidthChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_maximumWidthChanged_signal;
}

void* XWindow_maximumHeightChanged_signal(XWindow* self, int value)
{
    if (!self) return (void*)(size_t)XWindow_maximumHeightChanged_signal;
    XWindow_emit(self, (size_t)XWindow_maximumHeightChanged_signal,
                 XVarList_Create(XVar(int, value)));
    return (void*)(size_t)XWindow_maximumHeightChanged_signal;
}

void* XWindow_visibleChanged_signal(XWindow* self, bool visible)
{
    if (!self) return (void*)(size_t)XWindow_visibleChanged_signal;
    XWindow_emit(self, (size_t)XWindow_visibleChanged_signal,
                 XVarList_Create(XVar(bool, visible)));
    return (void*)(size_t)XWindow_visibleChanged_signal;
}

void* XWindow_visibilityChanged_signal(XWindow* self,
                                      XWindowVisibility visibility)
{
    if (!self) return (void*)(size_t)XWindow_visibilityChanged_signal;
    XWindow_emit(self, (size_t)XWindow_visibilityChanged_signal,
                 XVarList_Create(XVar(XWindowVisibility, visibility)));
    return (void*)(size_t)XWindow_visibilityChanged_signal;
}

void* XWindow_activeChanged_signal(XWindow* self)
{
    if (!self) return (void*)(size_t)XWindow_activeChanged_signal;
    XWindow_emit(self, (size_t)XWindow_activeChanged_signal, NULL);
    return (void*)(size_t)XWindow_activeChanged_signal;
}

void* XWindow_contentOrientationChanged_signal(XWindow* self,
                                             XScreenOrientation orientation)
{
    if (!self) return (void*)(size_t)XWindow_contentOrientationChanged_signal;
    XWindow_emit(self, (size_t)XWindow_contentOrientationChanged_signal,
                 XVarList_Create(XVar(XScreenOrientation, orientation)));
    return (void*)(size_t)XWindow_contentOrientationChanged_signal;
}

void* XWindow_focusObjectChanged_signal(XWindow* self, XObject* object)
{
    if (!self) return (void*)(size_t)XWindow_focusObjectChanged_signal;
    XWindow_emit(self, (size_t)XWindow_focusObjectChanged_signal,
                 XVarList_Create(XVar(XObject*, object)));
    return (void*)(size_t)XWindow_focusObjectChanged_signal;
}

void* XWindow_opacityChanged_signal(XWindow* self, float opacity)
{
    if (!self) return (void*)(size_t)XWindow_opacityChanged_signal;
    XWindow_emit(self, (size_t)XWindow_opacityChanged_signal,
                 XVarList_Create(XVar(float, opacity)));
    return (void*)(size_t)XWindow_opacityChanged_signal;
}

void* XWindow_transientParentChanged_signal(XWindow* self,
                                           XWindow* transientParent)
{
    if (!self) return (void*)(size_t)XWindow_transientParentChanged_signal;
    XWindow_emit(self, (size_t)XWindow_transientParentChanged_signal,
                 XVarList_Create(XVar(XWindow*, transientParent)));
    return (void*)(size_t)XWindow_transientParentChanged_signal;
}

/* ==================== 事件分发（对标 QWindow::event） ==================== */

static bool VXWindow_event(XWindow* self, XEvent* event)
{
    if (!self || !event) return false;
    switch (event->type) {
    case XEVENT_TYPE_EXPOSE:
        XWindow_exposeEvent_base(self, event);
        XEvent_accept(event);
        break;
    case XEVENT_TYPE_RESIZE:
        XWindow_resizeEvent_base(self, event);
        break;
    case XEVENT_TYPE_PAINT:
        XWindow_paintEvent_base(self, event);
        break;
    case XEVENT_TYPE_MOVE:
        XWindow_moveEvent_base(self, event);
        break;
    case XEVENT_TYPE_FOCUS_IN:
        XWindow_focusInEvent_base(self, event);
        XEvent_accept(event);
        break;
    case XEVENT_TYPE_FOCUS_OUT:
        XWindow_focusOutEvent_base(self, event);
        XEvent_accept(event);
        break;
    case XEVENT_TYPE_SHOW:
        XWindow_showEvent_base(self, event);
        break;
    case XEVENT_TYPE_HIDE:
        XWindow_hideEvent_base(self, event);
        break;
    case XEVENT_TYPE_CLOSE:
        XWindow_closeEvent_base(self, event);
        break;
    case XEVENT_TYPE_KEY_PRESS:
        XWindow_keyPressEvent_base(self, event);
        break;
    case XEVENT_TYPE_KEY_RELEASE:
        XWindow_keyReleaseEvent_base(self, event);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        XWindow_mousePressEvent_base(self, event);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        XWindow_mouseReleaseEvent_base(self, event);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK:
        XWindow_mouseDoubleClickEvent_base(self, event);
        break;
    case XEVENT_TYPE_MOUSE_MOVE:
        XWindow_mouseMoveEvent_base(self, event);
        break;
    case XEVENT_TYPE_WHEEL:
        XWindow_wheelEvent_base(self, event);
        break;
    case XEVENT_TYPE_ENTER:
        XWindow_enterEvent_base(self, event);
        XEvent_accept(event);
        break;
    case XEVENT_TYPE_LEAVE:
        XWindow_leaveEvent_base(self, event);
        XEvent_accept(event);
        break;
    case XEVENT_TYPE_TOUCH_BEGIN:
    case XEVENT_TYPE_TOUCH_UPDATE:
    case XEVENT_TYPE_TOUCH_END:
    case XEVENT_TYPE_TOUCH_CANCEL:
        XWindow_touchEvent_base(self, event);
        break;
    case XEVENT_TYPE_TABLET_MOVE:
    case XEVENT_TYPE_TABLET_PRESS:
    case XEVENT_TYPE_TABLET_RELEASE:
        XWindow_tabletEvent_base(self, event);
        break;
    case XEVENT_TYPE_INPUT_METHOD:
        XWindow_inputMethodEvent_base(self, event);
        break;
    case XEVENT_TYPE_DRAG_ENTER:
        XWindow_dragEnterEvent_base(self, event);
        break;
    case XEVENT_TYPE_DRAG_MOVE:
        XWindow_dragMoveEvent_base(self, event);
        break;
    case XEVENT_TYPE_DRAG_LEAVE:
        XWindow_dragLeaveEvent_base(self, event);
        break;
    case XEVENT_TYPE_DROP:
        XWindow_dropEvent_base(self, event);
        break;
    default:
        /* 未识别事件回退 XObject 默认 Event 实现（返回 e->accepted）。 */
        return XClass_Parent(XObject, EXObject_Event,
                             bool(*)(XObject*, XEvent*))((XObject*)self, event);
    }
    return true;
}

bool XWindow_event_base(XWindow* self, XEvent* event)
{
    if (!self || !event) return false;
    return XClassGetVirtualFunc(self, EXObject_Event,
                                bool(*)(XWindow*, XEvent*))(self, event);
}

#define XWINDOW_DEFINE_EVENT_BASE(Name, Type) \
    void XWindow_##Name##_base(XWindow* self, XEvent* event) \
    { \
        if (!self || !event) return; \
        XClassGetVirtualFunc(self, Type, XWindowEventSlot)(self, event); \
    }

XWINDOW_DEFINE_EVENT_BASE(exposeEvent, EXWindow_ExposeEvent)
XWINDOW_DEFINE_EVENT_BASE(resizeEvent, EXWindow_ResizeEvent)
XWINDOW_DEFINE_EVENT_BASE(paintEvent, EXWindow_PaintEvent)
XWINDOW_DEFINE_EVENT_BASE(moveEvent, EXWindow_MoveEvent)
XWINDOW_DEFINE_EVENT_BASE(focusInEvent, EXWindow_FocusInEvent)
XWINDOW_DEFINE_EVENT_BASE(focusOutEvent, EXWindow_FocusOutEvent)
XWINDOW_DEFINE_EVENT_BASE(showEvent, EXWindow_ShowEvent)
XWINDOW_DEFINE_EVENT_BASE(hideEvent, EXWindow_HideEvent)
XWINDOW_DEFINE_EVENT_BASE(closeEvent, EXWindow_CloseEvent)
XWINDOW_DEFINE_EVENT_BASE(keyPressEvent, EXWindow_KeyPressEvent)
XWINDOW_DEFINE_EVENT_BASE(keyReleaseEvent, EXWindow_KeyReleaseEvent)
XWINDOW_DEFINE_EVENT_BASE(mousePressEvent, EXWindow_MousePressEvent)
XWINDOW_DEFINE_EVENT_BASE(mouseReleaseEvent, EXWindow_MouseReleaseEvent)
XWINDOW_DEFINE_EVENT_BASE(mouseDoubleClickEvent, EXWindow_MouseDoubleClickEvent)
XWINDOW_DEFINE_EVENT_BASE(mouseMoveEvent, EXWindow_MouseMoveEvent)
XWINDOW_DEFINE_EVENT_BASE(wheelEvent, EXWindow_WheelEvent)
XWINDOW_DEFINE_EVENT_BASE(touchEvent, EXWindow_TouchEvent)
XWINDOW_DEFINE_EVENT_BASE(inputMethodEvent, EXWindow_InputMethodEvent)
XWINDOW_DEFINE_EVENT_BASE(dragEnterEvent, EXWindow_DragEnterEvent)
XWINDOW_DEFINE_EVENT_BASE(dragMoveEvent, EXWindow_DragMoveEvent)
XWINDOW_DEFINE_EVENT_BASE(dragLeaveEvent, EXWindow_DragLeaveEvent)
XWINDOW_DEFINE_EVENT_BASE(dropEvent, EXWindow_DropEvent)
XWINDOW_DEFINE_EVENT_BASE(enterEvent, EXWindow_EnterEvent)
XWINDOW_DEFINE_EVENT_BASE(leaveEvent, EXWindow_LeaveEvent)
XWINDOW_DEFINE_EVENT_BASE(tabletEvent, EXWindow_TabletEvent)

bool XWindow_nativeEvent_base(XWindow* self, XEvent* event)
{
    if (!self || !event) return false;
    return XClassGetVirtualFunc(self, EXWindow_NativeEvent,
                                bool(*)(XWindow*, XEvent*))(self, event);
}

#endif /* XWINDOW_ON */
