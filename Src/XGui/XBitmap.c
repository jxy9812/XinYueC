/******************************************************************************
 * @file       XBitmap.c
 * @brief      XBitmap 单色位图类实现（对标 Qt 6.8 QBitmap）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBitmap.h"
#include "XImage.h"
#include "XClass.h"
#include "XVtable.h"
#include "XMemory.h"
#include <string.h>
#include <stdlib.h>

/* ========== 虚函数实现 ========== */

static void VXBitmap_copy(XBitmap* dest, const XBitmap* src)
{
    if (ISNULL(dest, "XBitmap") || ISNULL(src, "XBitmap")) return;
    if (XClassIsVtableNull(dest)) XBitmap_init(dest);
    XClass_Parent(XPixmap, EXClass_Copy, void(*)(XPixmap*, const XPixmap*))
        ((XPixmap*)dest, (const XPixmap*)src);
}

static void VXBitmap_deinit(XBitmap* self)
{
    if (ISNULL(self, "XBitmap")) return;
    XClass_Parent(XPixmap, EXClass_Deinit, void(*)(XPixmap*))((XPixmap*)self);
}

/* ========== 虚函数表初始化 ========== */

XVtable* XBitmap_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XBitmap));
    XVTABLE_INHERIT_XCLASS(XPixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXBitmap_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBitmap_deinit);
    return XVTABLE_DEFAULT;
}

XBitmap* XBitmap_create()
{
    XBitmap* self = (XBitmap*)XMalloc_System(sizeof(XBitmap));
    if (!self) return NULL;
    XBitmap_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XBitmap_init(XBitmap* self)
{
    if (ISNULL(self, "XBitmap")) return;
    memset(self, 0, sizeof(XBitmap));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XBitmap);
}

void XBitmap_init_ex(XBitmap* self, int width, int height)
{
    if (ISNULL(self, "XBitmap")) return;
    XImage img;
    XImage_init_ex(&img, width, height, XImageFormat_Mono);
    XPixmap_init_bitmap_image((XPixmap*)self, &img, 0);
    XClassSetVtable(self, XBitmap);
    XImage_deinit_base(&img);
}

void XBitmap_init_size(XBitmap* self, const XSize* size)
{
    if (size)
        XBitmap_init_ex(self, size->width, size->height);
    else
        XBitmap_init(self);
}

void XBitmap_init_file(XBitmap* self, const char* fileName, const char* format)
{
    if (ISNULL(self, "XBitmap")) return;
    XBitmap_init(self);
    XImage img;
    XImage_init_file(&img, fileName, format);
    if (!XImage_isNull(&img))
    {
        XImage mono;
        XImage_convertToFormat(&img, XImageFormat_Mono, 0, &mono);
        XPixmap_init_bitmap_image((XPixmap*)self, &mono, 0);
        XClassSetVtable(self, XBitmap);
        XImage_deinit_base(&mono);
    }
    XImage_deinit_base(&img);
}

void XBitmap_init_pixmap(XBitmap* self, const XPixmap* other)
{
    if (ISNULL(self, "XBitmap") || ISNULL(other, "XPixmap")) return;
    XBitmap_init(self);
    // 从 fromPixmap 转换
    XBitmap_fromPixmap(other, self);
}

void XBitmap_copy(XBitmap* self, const XBitmap* other)
{
    if (ISNULL(self, "XBitmap") || ISNULL(other, "XBitmap")) return;
    if (!XClassIsVtableNull(self))
        XBitmap_deinit_base(self);
    XBitmap_init(self);
    XBitmap_copy_base(self, other);
}

void XBitmap_deinit(XBitmap* self)
{
    XBitmap_deinit_base(self);
}

void XBitmap_copy_base(XBitmap* dest, const XBitmap* src)
{
    if (ISNULL(dest, "XBitmap") || ISNULL(src, "XBitmap")) return;
    if (ISNULL(XClassGetVtable(src), "Vtable")) return;
    XClassGetVirtualFunc(src, EXClass_Copy, void(*)(XBitmap*, const XBitmap*))(dest, src);
}

void XBitmap_deinit_base(XBitmap* self)
{
    if (ISNULL(self, "XBitmap")) return;
    if (ISNULL(XClassGetVtable(self), "Vtable")) return;
    XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XBitmap*))(self);
}



void XBitmap_delete_base(XBitmap* self)
{
    XClass_delete_base((XClass*)self);
}
void XBitmap_clear(XBitmap* self)
{
    XPixmap_fill((XPixmap*)self, 0xFF000000); // color0
}

void XBitmap_transformed(const XBitmap* self, float m00, float m01, float m02,
                         float m10, float m11, float m12, XBitmap* out)
{
    if (!out) return;
    XPixmap_transformed((const XPixmap*)self, m00, m01, m02, m10, m11, m12, 0, (XPixmap*)out);
    XClassSetVtable(out, XBitmap);
}

void XBitmap_fromImage(const XImage* image, uint32_t flags, XBitmap* out)
{
    if (!image || !out) return;
    if (XImage_isNull(image))
    {
        XBitmap_init(out);
        return;
    }
    XImage mono;
    XImage_init(&mono);
    XImage_convertToFormat(image, XImageFormat_MonoLSB, flags, &mono);
    XPixmap_init_bitmap_image((XPixmap*)out, &mono, 0);
    XClassSetVtable(out, XBitmap);
    XImage_deinit_base(&mono);
}

void XBitmap_fromData(const XSize* size, const uint8_t* bits, XImageFormat monoFormat, XBitmap* out)
{
    if (!out) return;
    if (!size || !bits || size->width <= 0 || size->height <= 0 ||
        (monoFormat != XImageFormat_Mono && monoFormat != XImageFormat_MonoLSB))
    {
        XBitmap_init(out);
        return;
    }
    XImage img;
    XImage_init_ex(&img, size->width, size->height, monoFormat);
    const int sourceStride = (size->width + 7) / 8;
    for (int y = 0; y < size->height; ++y)
        memcpy(XImage_scanLine(&img, y), bits + y * sourceStride, (size_t)sourceStride);
    XBitmap_fromImage(&img, 0, out);
    XImage_deinit_base(&img);
}

void XBitmap_fromPixmap(const XPixmap* pixmap, XBitmap* out)
{
    if (!pixmap || !out) return;
    XImage img;
    XPixmap_toImage(pixmap, &img);
    XBitmap_fromImage(&img, 0, out);
    XImage_deinit_base(&img);
}


