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
#include "XVariant.h"
#include <string.h>
#include <limits.h>

/* Qt 的 QBitmap 不是“黑白图像”的别名，而是保证像素深度为 1 的
 * QPixmap。其约定是 bit=0 为 Qt::color0（白色），bit=1 为
 * Qt::color1（黑色）。以下两个小工具集中处理输出对象生命周期和
 * QPlatformPixmap::makeBitmapCompliantIfNeeded() 的颜色表归一化，避免
 * fromImage、fromData、文件加载三条路径各自产生不同的位图语义。 */
static void XBitmap_resetOutput(XBitmap* out)
{
    if (!out) return;
    if (!XClassIsVtableNull(out)) XBitmap_deinit_base(out);
    XBitmap_init(out);
}

static bool XBitmap_makeBitmapImage(const XImage* source, uint32_t flags,
                                    XImage* out)
{
    const uint32_t black = 0xff000000u;
    const uint32_t white = 0xffffffffu;
    const uint32_t bitmapColors[2] = {white, black};
    if (!source || !out || XImage_isNull(source)) return false;

    XImage_init(out);
    XImage_convertToFormat(source, XImageFormat_MonoLSB, flags, out);
    if (XImage_isNull(out)) return false;

    /* XImage_convertToFormat() preserves the packed bits when changing
       Mono/MSB to MonoLSB, but its lightweight conversion path creates the
       target palette independently.  Carry the source two-entry palette
       across first; otherwise QBitmap::fromData() would accidentally see
       the temporary black/white palette and invert already-correct bits. */
    if ((XImage_format(source) == XImageFormat_Mono ||
         XImage_format(source) == XImageFormat_MonoLSB) &&
        XImage_colorCount(source) >= 2)
    {
        uint32_t sourceColors[2];
        sourceColors[0] = XImage_color(source, 0);
        sourceColors[1] = XImage_color(source, 1);
        XImage_setColorTable(out, sourceColors, 2);
    }

    /* Qt 6.8 qbitmap.cpp:142-151 只在转换结果仍为普通黑/白顺序
       （index 0=black、index 1=white）时翻转位和颜色表；这是把普通
       QImage 的含义转换为 QBitmap 的 color0/color1 约定的关键步骤。
       不能无条件翻转，否则 fromData() 已经提供 color0/color1 时会反向。 */
    if (XImage_color(out, 0) == black && XImage_color(out, 1) == white)
    {
        XImage_invertPixels(out, XImageInvertMode_InvertRgb);
        XImage_setColor(out, 0, white);
        XImage_setColor(out, 1, black);
    }
    else if (XImage_colorCount(out) < 2)
    {
        /* QImage::Format_MonoLSB 新建时允许颜色表尚未显式填充；平台
           位图在 resize/fromImage 后仍必须可按 color0/color1 读取。 */
        XImage_setColorTable(out, bitmapColors, 2);
    }
    return true;
}

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
    XVTABLE_INIT_DEFAULT(XBitmap)
    XVTABLE_INHERIT_XCLASS(XPixmap);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXBitmap_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBitmap_deinit);
    return XVTABLE_DEFAULT;
}

XBitmap* XBitmap_create_ex(XMemoryType memory)
{
    XBitmap* self = (XBitmap*)XMemory_malloc(sizeof(XBitmap), memory);
    if (!self) return NULL;
    XBitmap_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
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
    const uint32_t bitmapColors[2] = {0xffffffffu, 0xff000000u};
    XImage_init_ex(&img, width, height, XImageFormat_MonoLSB);
    if (!XImage_isNull(&img)) XImage_setColorTable(&img, bitmapColors, 2);
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

void XBitmap_init_file_2(XBitmap* self, const char* fileName, const char* format)
{
    XString* fileNameString = fileName ? XString_create_utf8(fileName) : NULL;
    XString* formatString = format ? XString_create_utf8(format) : NULL;
    XBitmap_init_file(self, fileNameString, formatString);
    if (fileNameString) XString_delete_base((XClass*)fileNameString);
    if (formatString) XString_delete_base((XClass*)formatString);
}

void XBitmap_init_file(XBitmap* self, const XString* fileName, const XString* format)
{
    if (ISNULL(self, "XBitmap")) return;
    XBitmap_resetOutput(self);
    XImage img;
    XImage_init_file_2(&img, XString_toUtf8(fileName), XString_toUtf8(format));
    if (!XImage_isNull(&img))
    {
        XBitmap_fromImage(&img, 0, self);
    }
    XImage_deinit_base(&img);
}

void XBitmap_init_pixmap(XBitmap* self, const XPixmap* other)
{
    if (ISNULL(self, "XBitmap")) return;
    XBitmap_resetOutput(self);
    if (!other) return;
    // 从 fromPixmap 转换
    XBitmap_fromPixmap(other, self);
}

void XBitmap_swap(XBitmap* self, XBitmap* other)
{
    if (!self || !other || self == other) return;
    XPixmap_swap((XPixmap*)self, (XPixmap*)other);
    XClassSetVtable(self, XBitmap);
    XClassSetVtable(other, XBitmap);
}


void XBitmap_clear(XBitmap* self)
{
    if (!self) return;
    /* QBitmap::clear() is QPixmap::fill(Qt::color0).  Qt::color0 is the
       special white color whose monochrome storage value is zero; passing
       black through XPixmap_fill would set every bit and invert the mask. */
    XPixmap_fill((XPixmap*)self, 0);
}

void XBitmap_transformed_2(const XBitmap* self, float m00, float m01, float m02,
                           float m10, float m11, float m12, XBitmap* out)
{
    XImageTransform matrix = {0};
    matrix.m11 = m00;
    matrix.m21 = m01;
    matrix.dx = m02;
    matrix.m12 = m10;
    matrix.m22 = m11;
    matrix.dy = m12;
    XBitmap_transformed(self, &matrix, out);
}

void XBitmap_transformed(const XBitmap* self, const XImageTransform* matrix,
                         XBitmap* out)
{
    XImage source;
    XImage transformed;
    if (!out) return;
    if (!self || !matrix) {
        XBitmap_resetOutput(out);
        return;
    }
    XImage_init(&source);
    XImage_init(&transformed);
    XPixmap_toImage((const XPixmap*)self, &source);
    XImage_transformed(&source, matrix, 0, &transformed);
    XBitmap_fromImage(&transformed, 0, out);
    XImage_deinit_base(&source);
    XImage_deinit_base(&transformed);
}

XVariant* XBitmap_toVariant(const XBitmap* self)
{
    return XVariant_create_ptr((void*)self);
}

XBitmap* XBitmap_fromVariant(const XVariant* variant)
{
    if (!variant || XVariant_type((XVariant*)variant) != XVariantType_Ptr)
        return NULL;
    return (XBitmap*)XVariant_toPtr(variant);
}

void XBitmap_fromImage(const XImage* image, uint32_t flags, XBitmap* out)
{
    if (!out) return;
    XBitmap_resetOutput(out);
    if (!image || XImage_isNull(image)) return;
    XImage mono;
    if (!XBitmap_makeBitmapImage(image, flags, &mono)) return;
    XPixmap_init_bitmap_image((XPixmap*)out, &mono, 0);
    XClassSetVtable(out, XBitmap);
    XImage_deinit_base(&mono);
}

void XBitmap_fromData(const XSize* size, const uint8_t* bits, XImageFormat monoFormat, XBitmap* out)
{
    if (!out) return;
    XBitmap_resetOutput(out);
    if (!size || !bits || size->width <= 0 || size->height <= 0 ||
        (monoFormat != XImageFormat_Mono && monoFormat != XImageFormat_MonoLSB))
    {
        return;
    }
    XImage img;
    const uint32_t bitmapColors[2] = {0xffffffffu, 0xff000000u};
    XImage_init_ex(&img, size->width, size->height, monoFormat);
    if (XImage_isNull(&img)) return;
    XImage_setColorTable(&img, bitmapColors, 2);
    if ((int64_t)size->width + 7 > INT_MAX) {
        XImage_deinit_base(&img);
        return;
    }
    const int sourceStride = (size->width + 7) / 8;
    for (int y = 0; y < size->height; ++y)
        memcpy(XImage_scanLine(&img, y), bits + y * sourceStride, (size_t)sourceStride);
    XBitmap_fromImage(&img, 0, out);
    XImage_deinit_base(&img);
}

void XBitmap_fromPixmap(const XPixmap* pixmap, XBitmap* out)
{
    if (!out) return;
    XBitmap_resetOutput(out);
    if (!pixmap || XPixmap_isNull(pixmap)) return;

    /* QBitmap::fromPixmap() shallow-copies an existing one-bit bitmap.  The
       C layer has no paintingActive() state, so an XBitmap source is always
       safe to share through XPixmap's reference-counted platform object;
       ordinary depth-one XPixmap values are converted below to ensure the
       output is marked as a bitmap. */
    if (XPixmap_depth(pixmap) == 1 && XPixmap_isQBitmap(pixmap))
    {
        XPixmap_copy_base((XPixmap*)out, pixmap);
        XClassSetVtable(out, XBitmap);
        return;
    }
    XImage img;
    XImage_init(&img);   /* XPixmap_toImage 要求 out 为已初始化/空的 XImage 对象 */
    XPixmap_toImage(pixmap, &img);
    XBitmap_fromImage(&img, 0, out);
    XImage_deinit_base(&img);
}
