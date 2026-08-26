/******************************************************************************
 * @file       XIconThemeEngine.c
 * @brief      XIconThemeEngine 主题图标引擎实现（对标 Qt 6.8 QIconLoaderEngine）。
 * @note       取图时通过 XIconInternal_resolveThemePixmapSize 按目标尺寸
 *             解析主题文件，并按需缩放到方形目标尺寸；SVG 支持由 XGui
 *             图像编解码配置裁剪。
 ******************************************************************************/
#include "XIconThemeEngine.h"
#include "XIconThemeInternal.h"
#include "XPainter.h"
#include "XPixmap.h"
#include "XImage.h"
#include "XMemory.h"
#include <string.h>

static void themeEngine_pixmapForSize(const XIconThemeEngine* self,
                                      int targetSize, XPixmap* out)
{
    if (!self || !self->m_iconName || !out || targetSize <= 0) return;
    XIconInternal_resolveThemePixmapSize(
        XString_toUtf8(self->m_iconName), targetSize, out);
}

static void VXIconThemeEngine_paint(const XIconThemeEngine* self,
                                    void* painter, const XRect* rect,
                                    XIconMode mode, XIconState state)
{
    XPainter* target = (XPainter*)painter;
    XPixmap pixmap;
    XImage image;
    bool saved = false;
    (void)mode;
    (void)state;
    if (!self || !target || !rect || !target->m_drawImage) return;
    XPixmap_init(&pixmap);
    themeEngine_pixmapForSize(self, rect->width, &pixmap);
    if (!XPixmap_isNull(&pixmap)) {
        XImage_init(&image);
        XPixmap_toImage(&pixmap, &image);
        if (target->m_save) saved = target->m_save(target);
        target->m_drawImage(target, &image, rect->x, rect->y);
        if (saved && target->m_restore) target->m_restore(target);
        XImage_deinit_base(&image);
    }
    XPixmap_deinit_base(&pixmap);
}

static void VXIconThemeEngine_actualSize(const XIconThemeEngine* self,
                                         const XSize* size, XIconMode mode,
                                         XIconState state, XSize* out)
{
    XPixmap pixmap;
    int target;
    (void)mode;
    (void)state;
    if (!out) return;
    out->width = 0;
    out->height = 0;
    if (!self || !size || size->width <= 0 || size->height <= 0) return;
    target = size->width > size->height ? size->width : size->height;
    out->width = size->width;
    out->height = size->height;
    XPixmap_init(&pixmap);
    themeEngine_pixmapForSize(self, target, &pixmap);
    if (XPixmap_isNull(&pixmap)) {
        out->width = 0;
        out->height = 0;
    }
    XPixmap_deinit_base(&pixmap);
}

static void VXIconThemeEngine_pixmap(const XIconThemeEngine* self,
                                     const XSize* size, XIconMode mode,
                                     XIconState state, XPixmap* out)
{
    int target;
    (void)mode;
    (void)state;
    if (!out) return;
    XPixmap_init(out);
    if (!self || !size || size->width <= 0 || size->height <= 0) return;
    target = size->width > size->height ? size->width : size->height;
    themeEngine_pixmapForSize(self, target, out);
}

static void VXIconThemeEngine_addPixmap(XIconThemeEngine* self,
                                        const XPixmap* pixmap,
                                        XIconMode mode, XIconState state)
{
    (void)self;
    (void)pixmap;
    (void)mode;
    (void)state;
}

static void VXIconThemeEngine_addFile(XIconThemeEngine* self,
                                      const XString* fileName,
                                      const XSize* size, XIconMode mode,
                                      XIconState state)
{
    (void)self;
    (void)fileName;
    (void)size;
    (void)mode;
    (void)state;
}

static XString* VXIconThemeEngine_key(const XIconThemeEngine* self)
{
    XString* base = XString_create_utf8("qicon://theme/");
    if (base && self && self->m_iconName)
        XString_append(base, self->m_iconName);
    return base;
}

static XIconEngine* VXIconThemeEngine_clone(const XIconThemeEngine* self)
{
    if (!self) return NULL;
    return (XIconEngine*)XIconThemeEngine_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, self->m_iconName);
}

static bool VXIconThemeEngine_read(XIconThemeEngine* self, XIODevice* device)
{
    (void)self;
    (void)device;
    return false;
}

static bool VXIconThemeEngine_write(const XIconThemeEngine* self,
                                    XIODevice* device)
{
    (void)self;
    (void)device;
    return false;
}

static void VXIconThemeEngine_availableSizes(const XIconThemeEngine* self,
                                             XIconMode mode, XIconState state,
                                             XVector* out)
{
    (void)self;
    (void)mode;
    (void)state;
    if (out) XVector_clear_base((XContainer*)out);
}

static XString* VXIconThemeEngine_iconName(const XIconThemeEngine* self)
{
    return self && self->m_iconName
        ? XString_create_copy(self->m_iconName) : XString_create();
}

static bool VXIconThemeEngine_isNull(const XIconThemeEngine* self)
{
    XPixmap pixmap;
    bool found;
    if (!self || !self->m_iconName) return true;
    XPixmap_init(&pixmap);
    found = XIconInternal_resolveThemePixmap(
        XString_toUtf8(self->m_iconName), &pixmap);
    XPixmap_deinit_base(&pixmap);
    return !found;
}

static void VXIconThemeEngine_scaledPixmap(const XIconThemeEngine* self,
                                           const XSize* size, XIconMode mode,
                                           XIconState state, float scale,
                                           XPixmap* out)
{
    int target;
    float ratio = scale > 0.0f ? scale : 1.0f;
    (void)mode;
    (void)state;
    if (!out) return;
    XPixmap_init(out);
    if (!size || size->width <= 0 || size->height <= 0) return;
    target = (int)((double)(size->width > size->height
                            ? size->width : size->height) * ratio + 0.5);
    if (target <= 0) return;
    themeEngine_pixmapForSize(self, target, out);
    if (!XPixmap_isNull(out)) XPixmap_setDevicePixelRatio(out, ratio);
}

static void VXIconThemeEngine_virtualHook(const XIconThemeEngine* self,
                                          int id, void* data)
{
    (void)self;
    (void)id;
    (void)data;
}

static void VXIconThemeEngine_deinit(XIconThemeEngine* self)
{
    if (!self) return;
    if (self->m_iconName) {
        XString_delete_base((XClass*)self->m_iconName);
        self->m_iconName = NULL;
    }
    XClass_Deinit_Parent(XIconEngine, (XIconEngine*)self);
}

static void VXIconThemeEngine_copy(XIconThemeEngine* self,
                                   const XIconThemeEngine* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XIconThemeEngine_init(self, NULL);
    XClass_Parent(XIconEngine, EXClass_Copy,
                  void(*)(XIconEngine*, const XIconEngine*))(
        (XIconEngine*)self, (const XIconEngine*)other);
    if (self->m_iconName) {
        XString_delete_base((XClass*)self->m_iconName);
        self->m_iconName = NULL;
    }
    self->m_iconName = other->m_iconName
        ? XString_create_copy(other->m_iconName) : NULL;
}

static void VXIconThemeEngine_move(XIconThemeEngine* self,
                                   XIconThemeEngine* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XIconThemeEngine_init(self, NULL);
    XClass_Parent(XIconEngine, EXClass_Move,
                  void(*)(XIconEngine*, XIconEngine*))(
        (XIconEngine*)self, (XIconEngine*)other);
    if (self->m_iconName) {
        XString_delete_base((XClass*)self->m_iconName);
        self->m_iconName = NULL;
    }
    self->m_iconName = other->m_iconName;
    other->m_iconName = NULL;
}

XVtable* XIconThemeEngine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIconThemeEngine)
    XVTABLE_INHERIT_XCLASS(XIconEngine);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Paint, VXIconThemeEngine_paint);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_ActualSize, VXIconThemeEngine_actualSize);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Pixmap, VXIconThemeEngine_pixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_AddPixmap, VXIconThemeEngine_addPixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_AddFile, VXIconThemeEngine_addFile);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Key, VXIconThemeEngine_key);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Clone, VXIconThemeEngine_clone);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Read, VXIconThemeEngine_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_Write, VXIconThemeEngine_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_AvailableSizes, VXIconThemeEngine_availableSizes);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IconName, VXIconThemeEngine_iconName);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_IsNull, VXIconThemeEngine_isNull);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_ScaledPixmap, VXIconThemeEngine_scaledPixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXIconEngine_VirtualHook, VXIconThemeEngine_virtualHook);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXIconThemeEngine_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXIconThemeEngine_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIconThemeEngine_deinit);
    return XVTABLE_DEFAULT;
}

XIconThemeEngine* XIconThemeEngine_create_ex(XMemoryType memory,
                                             const XString* iconName)
{
    XIconThemeEngine* self = (XIconThemeEngine*)XMemory_malloc(
        sizeof(XIconThemeEngine), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XIconThemeEngine));
    XIconThemeEngine_init(self, iconName);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XIconThemeEngine* XIconThemeEngine_create_2_ex(XMemoryType memory,
                                               const char* iconName)
{
    XIconThemeEngine* result;
    XString* name = iconName ? XString_create_utf8(iconName) : NULL;
    result = XIconThemeEngine_create_ex(memory, name);
    if (name) XString_delete_base((XClass*)name);
    return result;
}

void XIconThemeEngine_init(XIconThemeEngine* self, const XString* iconName)
{
    if (!self) return;
    memset(self, 0, sizeof(XIconThemeEngine));
    XIconEngine_init(&self->m_base);
    XClassSetVtable(self, XIconThemeEngine);
    self->m_iconName = iconName ? XString_create_copy(iconName) : NULL;
}

void XIconThemeEngine_init_2(XIconThemeEngine* self, const char* iconName)
{
    XString* name = iconName ? XString_create_utf8(iconName) : NULL;
    XIconThemeEngine_init(self, name);
    if (name) XString_delete_base((XClass*)name);
}
