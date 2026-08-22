/******************************************************************************
 * @file       XBackingStore.c
 * @brief      XBackingStore 后备存储类实现（对标 Qt 6.8 QBackingStore）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBackingStore.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON

#include "XPlatformBackingStore.h"
#include "XPlatformIntegration.h"
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */
#include "XPlatformNativeInterface.h"
/** @brief XBackingStore 私有数据块。 */
struct XBackingStorePrivate
{
    XWindow*              m_window;   /**< 绑定的窗口（借用，不持有）。 */
    XPlatformBackingStore* m_platform; /**< 平台后端句柄（拥有，delete 释放）。 */
    XSize                 m_size;     /**< 最近一次成功 resize 的尺寸快照。 */
};

/** @brief 释放 XBackingStore：平台后端与私有块一并销毁。 */
static void VXBackingStore_deinit(XBackingStore* self)
{
    if (!self) return;
    if (self->m_data) {
        if (self->m_data->m_platform) {
            XPlatformBackingStore_delete(self->m_data->m_platform);
            self->m_data->m_platform = NULL;
        }
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XBackingStore_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBackingStore)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBackingStore_deinit);
    return XVTABLE_DEFAULT;
}

void XBackingStore_init(XBackingStore* self, XWindow* window)
{
    XPlatformNativeInterface* ni;
    XPlatformIntegration* gpi;
    if (!self) return;
    memset(self, 0, sizeof(XBackingStore));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XBackingStore);
    self->m_data = (XBackingStorePrivate*)XMalloc_System(sizeof(XBackingStorePrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XBackingStorePrivate));
    self->m_data->m_window = window;
    XSize_init(&self->m_data->m_size, 0, 0);
    /* 经应用集成层创建平台后端；没有应用/集成层时安全退化为空后端。 */
    ni = NULL;
#if XGUIAPPLICATION_ON
    ni = XGuiApplication_platformNativeInterface();
#endif /* XGUIAPPLICATION_ON */
    gpi = ni ? XPlatformNativeInterface_integration(ni) : NULL;
    if (gpi)
        self->m_data->m_platform =
            XPlatformIntegration_createPlatformBackingStore(gpi, window);
}

XBackingStore* XBackingStore_create_ex(XMemoryType memory, XWindow* window)
{
    XBackingStore* self = (XBackingStore*)XMemory_malloc(sizeof(XBackingStore), memory);
    if (!self) return NULL;
    XBackingStore_init(self, window);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XWindow* XBackingStore_window(const XBackingStore* self)
{
    return (self && self->m_data) ? self->m_data->m_window : NULL;
}

XImage* XBackingStore_paintDevice(XBackingStore* self)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return NULL;
    return XPlatformBackingStore_paintDevice(self->m_data->m_platform);
}

bool XBackingStore_toImage(XBackingStore* self, XImage* out)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return false;
    return XPlatformBackingStore_toImage(self->m_data->m_platform, out);
}

void XBackingStore_resize(XBackingStore* self, const XSize* size)
{
    if (!self || !self->m_data || !self->m_data->m_platform || !size) return;
    if (size->width < 0 || size->height < 0) return;
    XPlatformBackingStore_resize(self->m_data->m_platform, size);
    self->m_data->m_size = *size;
}

XSize XBackingStore_size(const XBackingStore* self)
{
    XSize out;
    XSize_init(&out, 0, 0);
    if (!self || !self->m_data) return out;
    return self->m_data->m_size;
}

bool XBackingStore_scroll(XBackingStore* self, const XRegion* area,
                          int dx, int dy)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return false;
    return XPlatformBackingStore_scroll(self->m_data->m_platform, area, dx, dy);
}

void XBackingStore_beginPaint(XBackingStore* self, const XRegion* region)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return;
    XPlatformBackingStore_beginPaint(self->m_data->m_platform, region);
}

void XBackingStore_endPaint(XBackingStore* self)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return;
    XPlatformBackingStore_endPaint(self->m_data->m_platform);
}

void XBackingStore_flush(XBackingStore* self, const XRegion* region,
                         XWindow* window, const XPoint* offset)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return;
    XPlatformBackingStore_flush(self->m_data->m_platform, window, region, offset);
}

void XBackingStore_setStaticContents(XBackingStore* self, const XRegion* region)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return;
    XPlatformBackingStore_setStaticContents(self->m_data->m_platform, region);
}

XRegion XBackingStore_staticContents(const XBackingStore* self)
{
    XRegion out;
    XRegion_init(&out);
    if (!self || !self->m_data || !self->m_data->m_platform) return out;
    out = XPlatformBackingStore_staticContents(self->m_data->m_platform);
    return out;
}

bool XBackingStore_hasStaticContents(const XBackingStore* self)
{
    if (!self || !self->m_data || !self->m_data->m_platform) return false;
    return XPlatformBackingStore_hasStaticContents(self->m_data->m_platform);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
