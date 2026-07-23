/******************************************************************************
 * @file       XPixmap.c
 * @brief      XPixmap 像素图类实现（对标 Qt 6.8 QPixmap）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPixmap.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XAtomic.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>

/* ========== 私有数据结构 ========== */

/**
 * @brief      XPlatformPixmap 平台像素图数据
 * @note       内部包装 XImage，提供平台相关优化
 */
typedef struct XPlatformPixmap
{
    XAtomic_int32_t  m_refCount;        /**< 引用计数 */
    XImage           m_image;            /**< 内部图像数据 */
    float            m_devicePixelRatio; /**< 设备像素比 */
    bool             m_isQBitmap;        /**< 是否为位图 */
    int64_t          m_cacheKey;         /**< 缓存键值 */
    int              m_serialNumber;     /**< 序列号 */
}XPlatformPixmap;

static int g_pixmapSerialCounter = 0;

static XPlatformPixmap* XPlatformPixmap_create(int width, int height)
{
    XPlatformPixmap* d = (XPlatformPixmap*)XMalloc_System(sizeof(XPlatformPixmap));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPlatformPixmap));
    XAtomic_init(d->m_refCount, 1);
    XImage_init_ex(&d->m_image, width, height, XImageFormat_ARGB32_Premultiplied);
    d->m_devicePixelRatio = 1.0f;
    d->m_serialNumber = (g_pixmapSerialCounter++);
    d->m_cacheKey = ((int64_t)d->m_serialNumber << 32) | (int64_t)(uintptr_t)d;
    return d;
}

static XPlatformPixmap* XPlatformPixmap_createFromImage(const XImage* image)
{
    if (!image) return NULL;
    XPlatformPixmap* d = (XPlatformPixmap*)XMalloc_System(sizeof(XPlatformPixmap));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPlatformPixmap));
    XAtomic_init(d->m_refCount, 1);
    XImage_copy(&d->m_image, image);
    d->m_devicePixelRatio = 1.0f;
    d->m_serialNumber = (g_pixmapSerialCounter++);
    d->m_cacheKey = ((int64_t)d->m_serialNumber << 32) | (int64_t)(uintptr_t)d;
    return d;
}

static void XPlatformPixmap_ref(XPlatformPixmap* d)
{
    if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst);
}

static void XPlatformPixmap_unref(XPlatformPixmap* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
    {
        XImage_deinit_base(&d->m_image);
        XFree_System(d);
    }
}

/* ========== 虚函数实现 ========== */

static void VXPixmap_copy(XPixmap* dest, const XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    if (dest->m_data)
        XPlatformPixmap_unref(dest->m_data);
    dest->m_data = src->m_data;
    XPlatformPixmap_ref(dest->m_data);
}

static void VXPixmap_move(XPixmap* dest, XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    if (dest->m_data)
        XPlatformPixmap_unref(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

static void VXPixmap_deinit(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    if (self->m_data)
    {
        XPlatformPixmap_unref(self->m_data);
        self->m_data = NULL;
    }
}

/* ========== 虚函数表初始化 ========== */

XVtable* XPixmap_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XPixmap));
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPixmap_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPixmap_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPixmap_deinit);
    return XVTABLE_DEFAULT;
}

XPixmap* XPixmap_create()
{
    XPixmap* self = (XPixmap*)XMalloc_System(sizeof(XPixmap));
    if (!self) return NULL;
    XPixmap_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XPixmap_init(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    memset(self, 0, sizeof(XPixmap));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XPixmap);
}

void XPixmap_init_ex(XPixmap* self, int width, int height)
{
    if (ISNULL(self, "XPixmap")) return;
    XPixmap_init(self);
    if (width > 0 && height > 0)
        self->m_data = XPlatformPixmap_create(width, height);
}

void XPixmap_init_size(XPixmap* self, const XSize* size)
{
    if (size)
        XPixmap_init_ex(self, size->width, size->height);
    else
        XPixmap_init(self);
}

void XPixmap_init_file(XPixmap* self, const char* fileName, const char* format, uint32_t flags)
{
    XPixmap_init(self);
    XPixmap_load(self, fileName, format, flags);
}

void XPixmap_init_image(XPixmap* self, const XImage* image, uint32_t flags)
{
    if (ISNULL(self, "XPixmap") || ISNULL(image, "XImage")) return;
    XPixmap_init(self);
    self->m_data = XPlatformPixmap_createFromImage(image);
}

void XPixmap_copy(XPixmap* self, const XPixmap* other)
{
    if (ISNULL(self, "XPixmap") || ISNULL(other, "XPixmap")) return;
    if (!XClassIsVtableNull(self))
        XPixmap_deinit_base(self);
    XPixmap_init(self);
    XPixmap_copy_base(self, other);
}

void XPixmap_move(XPixmap* self, XPixmap* other)
{
    if (ISNULL(self, "XPixmap") || ISNULL(other, "XPixmap")) return;
    if (!XClassIsVtableNull(self))
        XPixmap_deinit_base(self);
    XPixmap_init(self);
    XPixmap_move_base(self, other);
}

void XPixmap_deinit(XPixmap* self)
{
    XPixmap_deinit_base(self);
}

void XPixmap_copy_base(XPixmap* dest, const XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    VXPixmap_copy(dest, src);
}
void XPixmap_move_base(XPixmap* dest, XPixmap* src)
{
    if (ISNULL(dest, "XPixmap") || ISNULL(src, "XPixmap")) return;
    VXPixmap_move(dest, src);
}

void XPixmap_deinit_base(XPixmap* self)
{
    if (ISNULL(self, "XPixmap")) return;
    VXPixmap_deinit(self);
}



void XPixmap_delete_base(XPixmap* self)
{
    XClass_delete_base((XClass*)self);
}
/* ========== 查询方法 ========== */

bool XPixmap_isNull(const XPixmap* self)
{
    return !self || !self->m_data || XImage_isNull(&self->m_data->m_image);
}

int XPixmap_width(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_width(&self->m_data->m_image) : 0;
}

int XPixmap_height(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_height(&self->m_data->m_image) : 0;
}

void XPixmap_size(const XPixmap* self, XSize* out)
{
    if (out)
    {
        out->width = XPixmap_width(self);
        out->height = XPixmap_height(self);
    }
}

void XPixmap_rect(const XPixmap* self, XRect* out)
{
    if (out)
    {
        out->x = 0;
        out->y = 0;
        out->width = XPixmap_width(self);
        out->height = XPixmap_height(self);
    }
}

int XPixmap_depth(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_depth(&self->m_data->m_image) : 0;
}

int XPixmap_defaultDepth() { return 32; }

/* ========== 填充与掩码 ========== */

void XPixmap_fill(XPixmap* self, uint32_t color)
{
    if (!self || !self->m_data) return;
    XPixmap_detach(self);
    XImage_fill(&self->m_data->m_image, color);
}

void XPixmap_mask(const XPixmap* self, XPixmap* out)
{
    if (!self || !self->m_data || !out) return;
    XPixmap_init_ex(out, XPixmap_width(self), XPixmap_height(self));
}

void XPixmap_setMask(XPixmap* self, const XPixmap* mask)
{
    (void)self;
    (void)mask;
}

bool XPixmap_hasAlpha(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_hasAlpha(&self->m_data->m_image) : false;
}

bool XPixmap_hasAlphaChannel(const XPixmap* self)
{
    return (self && self->m_data) ? XImage_hasAlphaChannel(&self->m_data->m_image) : false;
}

void XPixmap_createHeuristicMask(const XPixmap* self, bool clipTight, XPixmap* out)
{
    (void)self;
    (void)clipTight;
    (void)out;
}

void XPixmap_createMaskFromColor(const XPixmap* self, uint32_t maskColor, uint32_t mode, XPixmap* out)
{
    (void)self;
    (void)maskColor;
    (void)mode;
    (void)out;
}

/* ========== 缩放与变换 ========== */

void XPixmap_scaled(const XPixmap* self, int width, int height, uint32_t aspectMode, uint32_t mode, XPixmap* out)
{
    if (!self || !self->m_data || !out) return;
    XImage scaled;
    XImage_init(&scaled);
    XImage_scaled(&self->m_data->m_image, width, height, aspectMode, mode, &scaled);
    XPixmap_init_image(out, &scaled, 0);
    XImage_deinit_base(&scaled);
}

void XPixmap_scaledToWidth(const XPixmap* self, int width, uint32_t mode, XPixmap* out)
{
    if (!self || !self->m_data) return;
    int height = XPixmap_height(self) * width / XPixmap_width(self);
    XPixmap_scaled(self, width, height, 0, mode, out);
}

void XPixmap_scaledToHeight(const XPixmap* self, int height, uint32_t mode, XPixmap* out)
{
    if (!self || !self->m_data) return;
    int width = XPixmap_width(self) * height / XPixmap_height(self);
    XPixmap_scaled(self, width, height, 0, mode, out);
}

void XPixmap_transformed(const XPixmap* self, float m00, float m01, float m02,
                         float m10, float m11, float m12, uint32_t mode, XPixmap* out)
{
    (void)self;
    (void)m00;
    (void)m01;
    (void)m02;
    (void)m10;
    (void)m11;
    (void)m12;
    (void)mode;
    (void)out;
}

/* ========== 转换方法 ========== */

void XPixmap_toImage(const XPixmap* self, XImage* out)
{
    if (!self || !self->m_data || !out) return;
    XImage_init(out);
    XImage_copy_base(out, &self->m_data->m_image);
}

void XPixmap_fromImage(const XImage* image, uint32_t flags, XPixmap* out)
{
    if (!image || !out) return;
    XPixmap_init_image(out, image, flags);
}

void XPixmap_fromImageReader(void* reader, uint32_t flags, XPixmap* out)
{
    (void)reader;
    (void)flags;
    (void)out;
}

/* ========== 文件操作 ========== */

bool XPixmap_load(XPixmap* self, const char* fileName, const char* format, uint32_t flags)
{
    (void)self;
    (void)fileName;
    (void)format;
    (void)flags;
    return false;
}

bool XPixmap_loadFromData(XPixmap* self, const uint8_t* buf, uint32_t len, const char* format, uint32_t flags)
{
    (void)self;
    (void)buf;
    (void)len;
    (void)format;
    (void)flags;
    return false;
}

bool XPixmap_save(const XPixmap* self, const char* fileName, const char* format, int quality)
{
    return XImage_save(&self->m_data->m_image, fileName, format, quality);
}

/* ========== 其他 ========== */

void XPixmap_copyRect(const XPixmap* self, const XRect* rect, XPixmap* out)
{
    if (!self || !self->m_data || !out) return;
    XImage copied;
    XImage_init(&copied);
    XImage_copyRect(&self->m_data->m_image, rect, &copied);
    XPixmap_init_image(out, &copied, 0);
    XImage_deinit_base(&copied);
}

void XPixmap_scroll(XPixmap* self, int dx, int dy, const XRect* rect, XRegion* exposed)
{
    (void)self;
    (void)dx;
    (void)dy;
    (void)rect;
    (void)exposed;
}

int64_t XPixmap_cacheKey(const XPixmap* self)
{
    return (self && self->m_data) ? self->m_data->m_cacheKey : 0;
}

bool XPixmap_isDetached(const XPixmap* self)
{
    return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
}

void XPixmap_detach(XPixmap* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XPlatformPixmap* newData = XPlatformPixmap_createFromImage(&self->m_data->m_image);
        if (newData)
        {
            newData->m_devicePixelRatio = self->m_data->m_devicePixelRatio;
            newData->m_isQBitmap = self->m_data->m_isQBitmap;
            XPlatformPixmap_unref(self->m_data);
            self->m_data = newData;
        }
    }
}

bool XPixmap_isQBitmap(const XPixmap* self)
{
    return (self && self->m_data) ? self->m_data->m_isQBitmap : false;
}

float XPixmap_devicePixelRatio(const XPixmap* self)
{
    return (self && self->m_data) ? self->m_data->m_devicePixelRatio : 1.0f;
}

void XPixmap_setDevicePixelRatio(XPixmap* self, float scaleFactor)
{
    if (!self || !self->m_data) return;
    XPixmap_detach(self);
    self->m_data->m_devicePixelRatio = scaleFactor;
}

void XPixmap_deviceIndependentSize(const XPixmap* self, XSizeF* out)
{
    if (out)
    {
        float ratio = XPixmap_devicePixelRatio(self);
        out->width = XPixmap_width(self) / ratio;
        out->height = XPixmap_height(self) / ratio;
    }
}

void XPixmap_fromImageInPlace(XImage* image, uint32_t flags, XPixmap* out)
{
    if (!image || !out) return;
    XPixmap_init(out);
    out->m_data = XPlatformPixmap_createFromImage(image);
}



