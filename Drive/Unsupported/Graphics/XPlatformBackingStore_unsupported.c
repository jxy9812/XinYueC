/******************************************************************************
 * @file       XPlatformBackingStore_unsupported.c
 * @brief      未提供平台后备存储后端的平台存根（XBackingStore 回落路径）。
 * @details    本文件严格遵循 Drive 平台存根惯例（见 XSystem_unsupported.c）：
 *             在既非 Linux 也非 Windows 的平台（FreeRTOS/裸机/其它 RTOS）
 *             上保持 XBackingStore 可链接。所有操作退化为无操作或空值：
 *             create 返回 NULL（XBackingStore_init 已安全容错为"空后端"），
 *             delete/绘制流程/静态内容设置均为无操作，查询类返回空值。
 *             产品需要在该类平台接入显示驱动时，应仿照 Drive/Posix 后端
 *             补充对应平台文件，并把本文件的哨兵守卫改为新平台专属条件。
 * @note       模块总开关 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 定义
 *             于 XGuiConfig.h；本文件同时编译时为二者共同的 1。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformBackingStore.h"

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

#if !defined(__linux__) && !defined(_WIN32)

/* ==================== 生命周期（全部无操作/空值） ==================== */

XPlatformBackingStore* XPlatformBackingStore_create(XWindow* window)
{
    (void)window;
    return NULL;
}

void XPlatformBackingStore_delete(XPlatformBackingStore* self)
{
    (void)self;
}

/* ==================== 访问器 ==================== */

XWindow* XPlatformBackingStore_window(const XPlatformBackingStore* self)
{
    (void)self;
    return NULL;
}

XImage* XPlatformBackingStore_paintDevice(XPlatformBackingStore* self)
{
    (void)self;
    return NULL;
}

bool XPlatformBackingStore_toImage(XPlatformBackingStore* self, XImage* out)
{
    (void)self; (void)out;
    return false;
}

/* ==================== 绘制流程 ==================== */

void XPlatformBackingStore_flush(XPlatformBackingStore* self,
                                 XWindow* window,
                                 const XRegion* region,
                                 const XPoint* offset)
{
    (void)self; (void)window; (void)region; (void)offset;
}

void XPlatformBackingStore_resize(XPlatformBackingStore* self,
                                  const XSize* size)
{
    (void)self; (void)size;
}

bool XPlatformBackingStore_scroll(XPlatformBackingStore* self,
                                  const XRegion* area, int dx, int dy)
{
    (void)self; (void)area; (void)dx; (void)dy;
    return false;
}

void XPlatformBackingStore_beginPaint(XPlatformBackingStore* self,
                                      const XRegion* region)
{
    (void)self; (void)region;
}

void XPlatformBackingStore_endPaint(XPlatformBackingStore* self)
{
    (void)self;
}

/* ==================== 静态内容 ==================== */

void XPlatformBackingStore_setStaticContents(XPlatformBackingStore* self,
                                             const XRegion* region)
{
    (void)self; (void)region;
}

XRegion XPlatformBackingStore_staticContents(
        const XPlatformBackingStore* self)
{
    XRegion out;
    (void)self;
    XRegion_init(&out);
    return out;
}

bool XPlatformBackingStore_hasStaticContents(
        const XPlatformBackingStore* self)
{
    (void)self;
    return false;
}

/* ==================== 平台扩展 ==================== */

void XPlatformBackingStore_setPresentCallback(
        XPlatformBackingStore* self,
        XPlatformBackingStorePresentFn callback, void* userData)
{
    (void)self; (void)callback; (void)userData;
}

void XPlatformBackingStore_setNativeTargetWindow(
        XPlatformBackingStore* self, void* nativeWindow)
{
    (void)self; (void)nativeWindow;
}

#endif /* !defined(__linux__) && !defined(_WIN32) */

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */
