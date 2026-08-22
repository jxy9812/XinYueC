/******************************************************************************
 * @file       XPlatformWindow.c
 * @brief      XPlatformWindow 平台窗口句柄类实现（对标 Qt 6.8 QPlatformWindow
 *             轻量子集）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformWindow.h"
#include "XMemory.h"
#include "XString.h"
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif
#if XGUIAPPLICATION_ON && XWINDOW_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON && XWINDOW_ON */
#include <string.h>

#if XPLATFORMWINDOW_ON

/** @brief 原生句柄 ID 分配器；自 1 递增，0 恒表示无效。 */
static uint64_t g_nextNativeId = 0;

/** @brief XPlatformWindow 私有数据块。 */
struct XPlatformWindowPrivate
{
    XWindow* m_window;                 /**< 绑定的 XWindow 借用指针。 */
    uint64_t m_nativeId;               /**< 原生句柄 ID（自增分配）。 */
    bool m_foreign;                    /**< 是否为外部原生窗口。 */
    XVariantHashMap* m_properties;     /**< 原生属性表（XString→XVariant，拥有）。 */
};

static void VXPlatformWindow_deinit(XPlatformWindow* self)
{
    if (!self) return;
    if (self->m_data) {
#if XPLATFORMNATIVEWINDOW_ON
        if (self->m_data->m_foreign && self->m_data->m_window)
            XPlatformNativeWindow_destroy(self->m_data->m_window);
#endif
        if (self->m_data->m_properties) {
            XHashMap_delete_base(self->m_data->m_properties);
            self->m_data->m_properties = NULL;
        }
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XPlatformWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlatformWindow)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlatformWindow_deinit);
    return XVTABLE_DEFAULT;
}

void XPlatformWindow_init(XPlatformWindow* self, XWindow* window)
{
    if (!self) return;
    memset(self, 0, sizeof(XPlatformWindow));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XPlatformWindow);
    self->m_data = (XPlatformWindowPrivate*)XMalloc_System(sizeof(XPlatformWindowPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XPlatformWindowPrivate));
    self->m_data->m_window = window;
    self->m_data->m_nativeId = ++g_nextNativeId;
    self->m_data->m_properties = XHashMap_create_XVariantHashMap();
}

XPlatformWindow* XPlatformWindow_create_ex(XMemoryType memory, XWindow* window)
{
    XPlatformWindow* self = (XPlatformWindow*)XMemory_malloc(sizeof(XPlatformWindow), memory);
    if (!self) return NULL;
    XPlatformWindow_init(self, window);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 窗口句柄访问 ==================== */

XWindow* XPlatformWindow_window(const XPlatformWindow* self)
{
    return (self && self->m_data) ? self->m_data->m_window : NULL;
}

void XPlatformWindow_setWindow(XPlatformWindow* self, XWindow* window)
{
    if (self && self->m_data) self->m_data->m_window = window;
}

uint64_t XPlatformWindow_handle(const XPlatformWindow* self)
{
    return (self && self->m_data) ? self->m_data->m_nativeId : 0;
}

void XPlatformWindow_setForeign(XPlatformWindow* self, bool foreign)
{
    if (self && self->m_data) self->m_data->m_foreign = foreign;
}

bool XPlatformWindow_isForeign(const XPlatformWindow* self)
{ return self && self->m_data && self->m_data->m_foreign; }

XWindowId XPlatformWindow_winId(const XPlatformWindow* self)
{
    XWindow* window;
    if (!self || !self->m_data) return 0;
#if XWINDOW_ON
    window = self->m_data->m_window;
    if (window) return XWindow_winId(window);
#endif /* XWINDOW_ON */
    return (XWindowId)self->m_data->m_nativeId;
}

XRect XPlatformWindow_geometry(const XPlatformWindow* self)
{
    XRect zero = { 0, 0, 0, 0 };
    if (!self || !self->m_data) return zero;
#if XWINDOW_ON
    if (self->m_data->m_window)
        return XWindow_geometry(self->m_data->m_window);
#endif /* XWINDOW_ON */
    return zero;
}

void XPlatformWindow_setGeometry(XPlatformWindow* self, const XRect* rect)
{
    XRect value;
    if (!self || !self->m_data || !self->m_data->m_window) return;
#if XWINDOW_ON
    value = rect ? *rect : (XRect){ 0, 0, 0, 0 };
    XWindow_setGeometry_rect(self->m_data->m_window, &value);
#else
    (void)rect;
#endif /* XWINDOW_ON */
}

bool XPlatformWindow_isVisible(const XPlatformWindow* self)
{
    if (!self || !self->m_data) return false;
#if XWINDOW_ON
    if (self->m_data->m_window)
        return XWindow_isVisible(self->m_data->m_window);
#endif /* XWINDOW_ON */
    return false;
}

void XPlatformWindow_setVisible(XPlatformWindow* self, bool visible)
{
    if (!self || !self->m_data || !self->m_data->m_window) return;
#if XWINDOW_ON
    XWindow_setVisible(self->m_data->m_window, visible);
#else
    (void)visible;
#endif /* XWINDOW_ON */
}

void XPlatformWindow_requestActivate(XPlatformWindow* self)
{
    if (!self || !self->m_data || !self->m_data->m_window) return;
#if XGUIAPPLICATION_ON && XWINDOW_ON
    XGuiApplication_setFocusWindow(self->m_data->m_window, NULL);
#else
    /* 无 GUI 应用/窗口子系统：嵌入式无焦点系统，no-op。 */
#endif
}

/* ==================== 原生属性表 ==================== */

/**
 * @brief      查询 UTF-8 属性名对应的属性值。
 * @details    属性表由 XHashMap_create_XVariantHashMap 创建，键采用
 *             XString_hash 内容哈希（配合 XString_compare 内容比较），因此
 *             内容相同的新 XString 可以直接命中，无需先取回存储键。
 * @param      self 目标对象；可为 NULL。
 * @param      name UTF-8 属性名；可为 NULL。
 * @return     属性值借用指针；未找到/入参非法返回 NULL。
 */
static XVariant* xplatformWindow_lookupProperty(const XPlatformWindow* self,
                                                const char* name)
{
    XString* key;
    XVariant* value;
    if (!self || !self->m_data || !self->m_data->m_properties || !name)
        return NULL;
    key = XString_create_utf8(name);
    if (!key) return NULL;
    value = (XVariant*)XMapBase_value_base((XMapBase*)self->m_data->m_properties,
                                           key);
    XString_delete_base((XClass*)key);
    return value;
}

XVariantHashMap* XPlatformWindow_properties(const XPlatformWindow* self)
{
    return (self && self->m_data) ? self->m_data->m_properties : NULL;
}

XVariant* XPlatformWindow_property(const XPlatformWindow* self, const char* name)
{
    return xplatformWindow_lookupProperty(self, name);
}

void XPlatformWindow_setProperty(XPlatformWindow* self, const char* name,
                                 const XVariant* value)
{
    XString* key;
    if (!self || !self->m_data || !self->m_data->m_properties || !name)
        return;
    /* 值为 NULL 等价于移除属性（与 Qt 语义一致）。 */
    if (!value) {
        XPlatformWindow_removeProperty(self, name);
        return;
    }
    /* 已存在同名属性时，XHashMap 按内容比较命中并原地更新值。 */
    key = XString_create_utf8(name);
    if (!key) return;
    XHashMap_insert_base(self->m_data->m_properties, key, value);
    XString_delete_base((XClass*)key);
}

bool XPlatformWindow_removeProperty(XPlatformWindow* self, const char* name)
{
    XString* key;
    bool removed;
    if (!self || !self->m_data || !self->m_data->m_properties || !name)
        return false;
    key = XString_create_utf8(name);
    if (!key) return false;
    removed = XMapBase_remove_base((XMapBase*)self->m_data->m_properties, key);
    XString_delete_base((XClass*)key);
    return removed;
}

#endif /* XPLATFORMWINDOW_ON */
