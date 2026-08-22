/******************************************************************************
 * @file       XPlatformNativeInterface.c
 * @brief      XPlatformNativeInterface 平台原生接口类实现（对标 Qt 6.8
 *             QPlatformNativeInterface）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformNativeInterface.h"
#include "XMemory.h"
#include "XString.h"
#include "XVarList.h"
#include "XEventType.h"
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
#include "XPlatformBackingStore.h"
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */
#if XSCREEN_ON
#include "XScreen.h"
#endif /* XSCREEN_ON */
#include <string.h>

#if XPLATFORMNATIVEINTERFACE_ON

#define XPLATFORMNATIVEINTERFACE_MAX_FUNCTIONS 32

typedef struct XPlatformFunctionEntry
{
    char* m_name;     /**< 函数名（对象拥有）。 */
    void* m_function; /**< 函数指针值（不拥有）。 */
} XPlatformFunctionEntry;

/** @brief XPlatformNativeInterface 私有数据块。 */
struct XPlatformNativeInterfacePrivate
{
    XPlatformIntegration* m_integration; /**< 所属集成层借用指针。 */
    XPlatformFunctionEntry m_functions[
        XPLATFORMNATIVEINTERFACE_MAX_FUNCTIONS]; /**< 平台函数注册表。 */
};

/** @brief 资源名相等判断（区分大小写，对标 QByteArray 精确匹配）。 */
static bool resourceMatch(const char* resource, const char* expected)
{
    return resource && expected && strcmp(resource, expected) == 0;
}

static void VXPlatformNativeInterface_deinit(XPlatformNativeInterface* self)
{
    int i;
    if (!self) return;
    if (self->m_data) {
        for (i = 0; i < XPLATFORMNATIVEINTERFACE_MAX_FUNCTIONS; ++i) {
            if (self->m_data->m_functions[i].m_name)
                XFree_System(self->m_data->m_functions[i].m_name);
        }
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XPlatformNativeInterface_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlatformNativeInterface)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlatformNativeInterface_deinit);
    return XVTABLE_DEFAULT;
}

void XPlatformNativeInterface_init(XPlatformNativeInterface* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPlatformNativeInterface));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XPlatformNativeInterface);
    self->m_data = (XPlatformNativeInterfacePrivate*)XMalloc_System(sizeof(XPlatformNativeInterfacePrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XPlatformNativeInterfacePrivate));
}

XPlatformNativeInterface* XPlatformNativeInterface_create_ex(XMemoryType memory)
{
    XPlatformNativeInterface* self;
    self = (XPlatformNativeInterface*)XMemory_malloc(sizeof(XPlatformNativeInterface), memory);
    if (!self) return NULL;
    XPlatformNativeInterface_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XPlatformNativeInterface_setIntegration(XPlatformNativeInterface* self,
                                             XPlatformIntegration* integration)
{
    if (!self || !self->m_data) return;
    self->m_data->m_integration = integration;
}

XPlatformIntegration* XPlatformNativeInterface_integration(const XPlatformNativeInterface* self)
{
    return (self && self->m_data) ? self->m_data->m_integration : NULL;
}

/* ==================== 原生资源查询 ==================== */

void* XPlatformNativeInterface_nativeResourceForIntegration(
        const XPlatformNativeInterface* self, const char* resource)
{
    if (!self || !self->m_data) return NULL;
    if (resourceMatch(resource, "integration") ||
        resourceMatch(resource, "integration-handle"))
        return (void*)self->m_data->m_integration;
#if XPLATFORMNATIVEWINDOW_ON
    /* 真实原生连接句柄：X11 返回 Display*，Win32 返回 HINSTANCE；
       未连接窗口系统时返回 NULL（对标 QPlatformNativeInterface 的
       nativeResourceForIntegration("display")）。 */
    if (resourceMatch(resource, "display") ||
        resourceMatch(resource, "hinstance") ||
        resourceMatch(resource, "native-connection"))
        return XPlatformNativeWindow_nativeConnection(NULL);
#endif /* XPLATFORMNATIVEWINDOW_ON */
    return NULL;
}

void* XPlatformNativeInterface_nativeResourceForWindow(
        const XPlatformNativeInterface* self, const char* resource, XWindow* window)
{
    (void)self;
    if (!resource || !window) return NULL;
    if (resourceMatch(resource, "window"))
        return (void*)window;
#if XWINDOW_ON
    if (resourceMatch(resource, "window-handle"))
        return (void*)XWindow_handle(window);
    if (resourceMatch(resource, "native-window-id"))
        return (void*)(uintptr_t)XWindow_winId(window);
#endif /* XWINDOW_ON */
    return NULL;
}

void* XPlatformNativeInterface_nativeResourceForScreen(
        const XPlatformNativeInterface* self, const char* resource, XScreen* screen)
{
    XScreen* target;
    (void)self;
    if (!resource) return NULL;
    if (!resourceMatch(resource, "screen") && !resourceMatch(resource, "screen-handle"))
        return NULL;
    target = screen;
#if XSCREEN_ON
    if (!target)
        target = XScreen_primaryScreen();
#else
    (void)target;
#endif /* XSCREEN_ON */
    return (void*)target;
}

void* XPlatformNativeInterface_nativeResourceForBackingStore(
        const XPlatformNativeInterface* self, const char* resource, void* backingStore)
{
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
    if (!self || !resource) return NULL;
    /* 资源名 "paintdevice"：返回后端内部 XImage 绘制设备（对标 Qt 栅格
     * 后备存储的 nativeResourceForBackingStore("paintdevice")）。 */
    if (!resourceMatch(resource, "paintdevice"))
        return NULL;
    return (void*)XPlatformBackingStore_paintDevice(
            (XPlatformBackingStore*)backingStore);
#else /* !XBACKINGSTORE_ON || !XPLATFORMBACKINGSTORE_ON */
    (void)self; (void)resource; (void)backingStore;
    return NULL;
#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
}

void* XPlatformNativeInterface_nativeResourceForCursor(
        const XPlatformNativeInterface* self, const char* resource, XCursor* cursor)
{
    /* 嵌入式无系统光标句柄，恒 NULL。 */
    (void)self; (void)resource; (void)cursor;
    return NULL;
}

void* XPlatformNativeInterface_nativeResourceFunctionForIntegration(
        const XPlatformNativeInterface* self, const char* resource)
{
    return XPlatformNativeInterface_platformFunction(self, resource);
}

void* XPlatformNativeInterface_nativeResourceFunctionForScreen(
        const XPlatformNativeInterface* self, const char* resource)
{
    return XPlatformNativeInterface_platformFunction(self, resource);
}

void* XPlatformNativeInterface_nativeResourceFunctionForWindow(
        const XPlatformNativeInterface* self, const char* resource)
{
    return XPlatformNativeInterface_platformFunction(self, resource);
}

void* XPlatformNativeInterface_nativeResourceFunctionForBackingStore(
        const XPlatformNativeInterface* self, const char* resource)
{
    return XPlatformNativeInterface_platformFunction(self, resource);
}

void* XPlatformNativeInterface_nativeResourceFunctionForCursor(
        const XPlatformNativeInterface* self, const char* resource)
{
    return XPlatformNativeInterface_platformFunction(self, resource);
}

void* XPlatformNativeInterface_platformFunction(
        const XPlatformNativeInterface* self, const char* name)
{
    int i;
    if (!self || !self->m_data || !name || !name[0]) return NULL;
    for (i = 0; i < XPLATFORMNATIVEINTERFACE_MAX_FUNCTIONS; ++i) {
        if (self->m_data->m_functions[i].m_name &&
            strcmp(self->m_data->m_functions[i].m_name, name) == 0)
            return self->m_data->m_functions[i].m_function;
    }
    return NULL;
}

bool XPlatformNativeInterface_registerPlatformFunction(
        XPlatformNativeInterface* self, const char* name, void* function)
{
    int i;
    int freeSlot = -1;
    char* copy;
    if (!self || !self->m_data || !name || !name[0]) return false;
    for (i = 0; i < XPLATFORMNATIVEINTERFACE_MAX_FUNCTIONS; ++i) {
        XPlatformFunctionEntry* entry = &self->m_data->m_functions[i];
        if (!entry->m_name) {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (strcmp(entry->m_name, name) != 0) continue;
        if (!function) {
            XFree_System(entry->m_name);
            entry->m_name = NULL;
            entry->m_function = NULL;
        } else {
            entry->m_function = function;
        }
        return true;
    }
    if (!function) return true; /* 注销不存在条目与 Qt 无命中语义一致。 */
    if (freeSlot < 0) return false;
    copy = XMemory_strdup(name);
    if (!copy) return false;
    self->m_data->m_functions[freeSlot].m_name = copy;
    self->m_data->m_functions[freeSlot].m_function = function;
    return true;
}

/* ==================== 窗口原生属性 ==================== */

XVariantHashMap* XPlatformNativeInterface_windowProperties(
        const XPlatformNativeInterface* self, XPlatformWindow* platformWindow)
{
    (void)self;
    if (!platformWindow) return NULL;
    return XPlatformWindow_properties(platformWindow);
}

XVariant* XPlatformNativeInterface_windowProperty(
        const XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const char* name)
{
    (void)self;
    if (!platformWindow) return NULL;
    return XPlatformWindow_property(platformWindow, name);
}

XVariant* XPlatformNativeInterface_windowProperty_2(
        const XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const char* name, const XVariant* defaultValue)
{
    XVariant* stored;
    (void)self;
    if (!platformWindow) return NULL;
    stored = XPlatformWindow_property(platformWindow, name);
    if (stored)
        return XVariant_create_copy(stored);
    return defaultValue ? XVariant_create_copy(defaultValue) : NULL;
}

/** @brief 发射信号并管理参数列表生命周期（与 XGuiApplication/XWindow 相同模式）。 */
static void platformNativeInterface_emit(XPlatformNativeInterface* self,
                                         size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

void XPlatformNativeInterface_setWindowProperty(
        XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const char* name, const XVariant* value)
{
    XString* key;
    if (!self || !platformWindow || !name) return;
    XPlatformWindow_setProperty(platformWindow, name, value);
    key = XString_create_utf8(name);
    if (!key) return;
    XPlatformNativeInterface_windowPropertyChanged_signal(self, platformWindow, key);
    XString_delete_base((XClass*)key);
}

void* XPlatformNativeInterface_windowPropertyChanged_signal(
        XPlatformNativeInterface* self, XPlatformWindow* platformWindow,
        const XString* propertyName)
{
    if (!self) return (void*)(size_t)XPlatformNativeInterface_windowPropertyChanged_signal;
    platformNativeInterface_emit(self,
        (size_t)XPlatformNativeInterface_windowPropertyChanged_signal,
        XVarList_Create(XVar(XPlatformWindow*, platformWindow),
                        XVar(XString*, propertyName)));
    return (void*)(size_t)XPlatformNativeInterface_windowPropertyChanged_signal;
}

#endif /* XPLATFORMNATIVEINTERFACE_ON */
