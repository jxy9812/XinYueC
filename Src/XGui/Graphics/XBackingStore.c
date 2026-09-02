/******************************************************************************
 * @file       XBackingStore.c
 * @brief      XBackingStore 后备存储类实现（对标 Qt 6.8 QBackingStore）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBackingStore.h"
#include "XMemory.h"
#include "XString.h"
#include "XImageFormat.h"
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
    XSize                 m_size;     /**< 最近一次接受的有效 resize 尺寸。 */
    XRegion               m_staticContents; /**< Qt 语义的静态区域快照。 */
};

/**
 * @brief 按 Qt QBackingStore::handle() 语义懒创建平台后端。
 * @details 构造阶段已经有平台后端时直接复用；如果对象创建时尚未
 *          建立 XGuiApplication 平台集成，则在后续调用 handle() 时
 *          再尝试创建。平台句柄始终由 XBackingStore 持有，调用方只借用。
 */
static XPlatformBackingStore* xBackingStore_ensureHandle(
        const XBackingStore* self)
{
    if (!self || !self->m_data) return NULL;
    if (self->m_data->m_platform) return self->m_data->m_platform;
#if XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON
    {
        XPlatformNativeInterface* ni;
        XPlatformIntegration* gpi;
        ni = XGuiApplication_platformNativeInterface();
        gpi = ni ? XPlatformNativeInterface_integration(ni) : NULL;
        if (gpi)
            self->m_data->m_platform =
                XPlatformIntegration_createPlatformBackingStore(
                    gpi, self->m_data->m_window);
    }
#endif /* XGUIAPPLICATION_ON && XPLATFORMNATIVEINTERFACE_ON */
    return self->m_data->m_platform;
}

/** @brief 释放 XBackingStore：平台后端与私有块一并销毁。 */
static void VXBackingStore_deinit(XBackingStore* self)
{
    if (!self) return;
    if (self->m_data) {
        if (self->m_data->m_platform) {
            XPlatformBackingStore_delete(self->m_data->m_platform);
            self->m_data->m_platform = NULL;
        }
        XRegion_deinit(&self->m_data->m_staticContents);
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
    if (!self) return;
    memset(self, 0, sizeof(XBackingStore));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XBackingStore);
    self->m_data = (XBackingStorePrivate*)XMalloc_System(sizeof(XBackingStorePrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XBackingStorePrivate));
    self->m_data->m_window = window;
    XSize_init(&self->m_data->m_size, 0, 0);
    XRegion_init(&self->m_data->m_staticContents);
    /* 优先按 Qt 构造路径创建；没有应用/集成层时安全退化为空后端。
     * 后续 handle() 仍会再次尝试，以覆盖集成层延后建立的场景。 */
    xBackingStore_ensureHandle(self);
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
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return NULL;
    return XPlatformBackingStore_paintDevice(platform);
}

bool XBackingStore_nextTile(XBackingStore* self, XRect* tileRect)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform || !tileRect) return false;
    return XPlatformBackingStore_nextTile(platform, tileRect);
}

XPoint XBackingStore_paintOrigin(const XBackingStore* self)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (!self || !self->m_data || !self->m_data->m_platform)
        return out;
    return XPlatformBackingStore_paintOrigin(self->m_data->m_platform);
}

XPlatformBackingStore* XBackingStore_handle(const XBackingStore* self)
{
    /* QBackingStore::handle() 在首次调用时创建平台对象，之后只返回同一
     * 借用句柄；它不把平台对象的所有权转交调用方。 */
    return xBackingStore_ensureHandle(self);
}

bool XBackingStore_toImage(XBackingStore* self, XImage* out)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return false;
    return XPlatformBackingStore_toImage(platform, out);
}

void XBackingStore_resize(XBackingStore* self, const XSize* size)
{
    XPlatformBackingStore* platform;
    if (!self || !self->m_data || !size) return;
    if (size->width < 0 || size->height < 0) return;
    /* QBackingStore::resize 先更新公共 size/nativeSize，再把尺寸交给平台
     * 后端；即使后端暂时不可用，size() 仍反映调用方最近一次传入的有效
     * 尺寸。XGui 没有 Qt 的 DPR 层，因此这里直接记录逻辑像素尺寸。 */
    self->m_data->m_size = *size;
    platform = XBackingStore_handle(self);
    if (platform) {
        XPlatformBackingStore_resize(platform, size);
        /* 平台后端只需要当前缓冲范围内的静态提示，而公共快照不能被
         * 后端为实现提示而裁剪；重新设置会让各后端按自身缓冲裁剪。 */
        XPlatformBackingStore_setStaticContents(
            platform, &self->m_data->m_staticContents);
    }
}

bool XBackingStore_setBuffers(XBackingStore* self,
                              void* buffer1, void* buffer2,
                              size_t bufferSize)
{
    XPlatformBackingStore* platform;
    if (!self || !self->m_data) return false;
    platform = XBackingStore_handle(self);
    if (!platform) return false;
    if (!XPlatformBackingStore_setBuffers(platform, buffer1, buffer2,
                                          bufferSize))
        return false;
    return true;
}

size_t XBackingStore_requiredBufferSize(const XSize* size)
{
    return XPlatformBackingStore_requiredBufferSize(size);
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
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return false;
    return XPlatformBackingStore_scroll(platform, area, dx, dy);
}

void XBackingStore_beginPaint(XBackingStore* self, const XRegion* region)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return;
    XPlatformBackingStore_beginPaint(platform, region);
}

void XBackingStore_endPaint(XBackingStore* self)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return;
    XPlatformBackingStore_endPaint(platform);
}

void XBackingStore_flush(XBackingStore* self, const XRegion* region,
                         XWindow* window, const XPoint* offset)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform) return;
    /* Qt QBackingStore::flush 的 nullptr window 表示绑定的顶层窗口，而
     * 非空 window 才表示一次子窗口提交；平台层不能替公共层补这个默认。 */
    if (!window) window = self->m_data->m_window;
    XPlatformBackingStore_flush(platform, window, region, offset);
}

void XBackingStore_flushTile(XBackingStore* self, XWindow* window,
                             const XRect* tileRect, const XPoint* offset)
{
    XPlatformBackingStore* platform = XBackingStore_handle(self);
    if (!platform || !tileRect) return;
    if (!window) window = self->m_data->m_window;
    XPlatformBackingStore_flushTile(platform, window, tileRect, offset);
}

void XBackingStore_setStaticContents(XBackingStore* self, const XRegion* region)
{
    XRegion copy;
    if (!self || !self->m_data) return;

    /* QBackingStore::setStaticContents 只把值存到公共私有状态（Qt
     * qbackingstore.cpp:271-281），不按当前 backing image 裁剪。先复制到
     * 临时对象并验证容量，避免内存不足时无声破坏调用者之前的快照。 */
    XRegion_init(&copy);
    if (region) {
        XRegion_copy(region, &copy);
        if (region->count > 0 && copy.count != region->count) {
            XRegion_deinit(&copy);
            return;
        }
    }
    XRegion_deinit(&self->m_data->m_staticContents);
    self->m_data->m_staticContents = copy;
    XRegion_init(&copy);

    /* 平台实现可以为 resize/合成建立裁剪后的提示，但这不应改变上方
     * 公共 API 返回的原始快照。NULL 与空区域都传给平台以清除旧提示。 */
    if (self->m_data->m_platform)
        XPlatformBackingStore_setStaticContents(
            self->m_data->m_platform, &self->m_data->m_staticContents);
}

XRegion XBackingStore_staticContents(const XBackingStore* self)
{
    XRegion out;
    XRegion_init(&out);
    if (!self || !self->m_data) return out;
    XRegion_copy(&self->m_data->m_staticContents, &out);
    return out;
}

bool XBackingStore_hasStaticContents(const XBackingStore* self)
{
    if (!self || !self->m_data) return false;
    return !XRegion_isEmpty(&self->m_data->m_staticContents);
}

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */
