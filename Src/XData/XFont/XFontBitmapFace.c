/******************************************************************************
 * @file       XFontBitmapFace.c
 * @brief      XFont 点阵字库具体类实现及文件后端适配。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFontBitmapFace.h"
#include "XFont.h"
#include "XFontFace_Protected.h"
#include <string.h>

static XFontBitmapFace g_bitmapFaces[XFONT_MAX_BITMAP_PROVIDERS];
static size_t g_bitmapFaceCount;
static XFontBitmapFace g_fileFace;
static bool g_fileFaceInitialized;

static int XFontBitmapFace_rowBytes(const XFontGlyphDsc* dsc, int bpp)
{
    if (!dsc || dsc->box_w == 0 || dsc->box_h == 0 ||
        (bpp != 1 && bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8))
        return 0;
    return ((int)dsc->box_w * bpp + 7) / 8;
}

static size_t XFontBitmapFace_packedBytes(const XFontGlyphDsc* dsc, int bpp)
{
    size_t bits;
    if (!dsc || dsc->box_w == 0 || dsc->box_h == 0 ||
        (bpp != 1 && bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8))
        return 0;
    bits = (size_t)dsc->box_w * (size_t)dsc->box_h * (size_t)bpp;
    return (bits + 7u) / 8u;
}

static unsigned XFontBitmapFace_readPixel(const uint8_t* data, size_t bit,
                                          int bpp)
{
    unsigned value = 0;
    int i;
    for (i = 0; i < bpp; ++i)
    {
        size_t current = bit + (size_t)i;
        value = (value << 1) |
                ((data[current / 8u] >>
                  (7u - (unsigned)(current % 8u))) & 1u);
    }
    return value;
}

static void XFontBitmapFace_writePixel(uint8_t* data, size_t bit, int bpp,
                                       unsigned value)
{
    int i;
    for (i = 0; i < bpp; ++i)
    {
        size_t current = bit + (size_t)i;
        uint8_t mask = (uint8_t)(1u <<
                                  (7u - (unsigned)(current % 8u)));
        if ((value & (1u << (bpp - 1 - i))) != 0u)
            data[current / 8u] |= mask;
    }
}

static void XFontBitmapFace_unpack(const uint8_t* source, uint8_t* dest,
                                   const XFontGlyphDsc* dsc, int bpp,
                                   int rowBytes)
{
    int y;
    memset(dest, 0, (size_t)rowBytes * (size_t)dsc->box_h);
    for (y = 0; y < dsc->box_h; ++y)
    {
        int x;
        for (x = 0; x < dsc->box_w; ++x)
        {
            size_t sourceBit = ((size_t)y * (size_t)dsc->box_w +
                                (size_t)x) * (size_t)bpp;
            size_t destBit = (size_t)y * (size_t)rowBytes * 8u +
                             (size_t)x * (size_t)bpp;
            XFontBitmapFace_writePixel(dest, destBit, bpp,
                                       XFontBitmapFace_readPixel(source,
                                                                  sourceBit,
                                                                  bpp));
        }
    }
}

static bool XFontBitmapFace_findGlyph(const XFontBitmapData* data,
                                      uint32_t cp, XFontGlyphDsc* dsc)
{
    uint16_t i;
    if (!data || !data->glyph_dsc || !data->cmaps || !dsc ||
        data->bitmap_format != 0)
        return false;
    for (i = 0; i < data->cmap_num; ++i)
    {
        const XFontCmap* cmap = &data->cmaps[i];
        uint32_t relative;
        uint32_t glyphId;
        if (cp < cmap->range_start ||
            cp - cmap->range_start >= cmap->range_length)
            continue;
        relative = cp - cmap->range_start;
        switch (cmap->type)
        {
        case XFontCmapFormat0Full:
        case XFontCmapFormat0Tiny:
            glyphId = (uint32_t)cmap->glyph_id_start + relative;
            if (cmap->glyph_id_ofs_list)
                glyphId += ((const uint8_t*)cmap->glyph_id_ofs_list)[relative];
            break;
        case XFontCmapSparseFull:
        case XFontCmapSparseTiny:
        {
            uint16_t lo = 0;
            uint16_t hi = cmap->list_length;
            const uint16_t* list = cmap->unicode_list;
            if (!list) return false;
            while (lo < hi)
            {
                uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
                if (list[mid] < relative) lo = (uint16_t)(mid + 1u);
                else hi = mid;
            }
            if (lo >= cmap->list_length || list[lo] != relative)
                continue;
            glyphId = (uint32_t)cmap->glyph_id_start + lo;
            if (cmap->glyph_id_ofs_list)
                glyphId += cmap->type == XFontCmapSparseTiny
                    ? ((const uint8_t*)cmap->glyph_id_ofs_list)[lo]
                    : ((const uint16_t*)cmap->glyph_id_ofs_list)[lo];
            break;
        }
        default:
            continue;
        }
        if (glyphId == 0u)
            return false;
        *dsc = data->glyph_dsc[glyphId];
        return true;
    }
    return false;
}

static bool XFontBitmapFace_validProvider(const XFontBitmapProvider* provider)
{
    const XFontBitmapInfo* info;
    if (!provider || !provider->m_family || !provider->m_family[0] ||
        (!provider->m_data && !provider->m_loadGlyph))
        return false;
    info = &provider->m_info;
    if (info->m_width < 1 || info->m_width > XFONT_BITMAP_MAX_WIDTH ||
        info->m_height < 1 || info->m_height > XFONT_BITMAP_MAX_HEIGHT ||
        info->m_ascent < 0 || info->m_descent < 0 ||
        info->m_ascent + info->m_descent > info->m_height ||
        (info->m_bpp != 1 && info->m_bpp != 2 && info->m_bpp != 3 &&
         info->m_bpp != 4 && info->m_bpp != 8))
        return false;
    if (info->m_rowBytes != (info->m_width * info->m_bpp + 7) / 8 ||
        info->m_rowBytes < 1 || info->m_rowBytes > XFONT_BITMAP_MAX_ROW_BYTES)
        return false;
    return !provider->m_data ||
           (provider->m_data->bpp == (uint16_t)info->m_bpp &&
            provider->m_data->bitmap_format == 0 &&
            provider->m_data->glyph_dsc && provider->m_data->cmaps &&
            provider->m_data->cmap_num != 0);
}

static bool VXFontBitmapFace_info(const XFontFace* base, const XFont* font,
                                  XFontFaceInfo* info)
{
    const XFontBitmapFace* self = (const XFontBitmapFace*)base;
    XFontBitmapInfo fileInfo;
    if (!self || !info)
        return false;
    if (self->m_file)
    {
        if (!XFontBitmapFace_fileInfo(font, &fileInfo))
            return false;
        info->m_kind = XFontFace_Bitmap;
        info->m_bitmap = fileInfo;
        return true;
    }
    info->m_kind = XFontFace_Bitmap;
    info->m_bitmap = self->m_provider.m_info;
    return true;
}

static bool VXFontBitmapFace_loadGlyph(const XFontFace* base,
                                       const XFont* font, uint32_t codepoint,
                                       XFontGlyphDsc* dsc, unsigned char* out,
                                       size_t outSize)
{
    const XFontBitmapFace* self = (const XFontBitmapFace*)base;
    const XFontBitmapProvider* provider;
    XFontBitmapInfo info;
    size_t rowBytes;
    size_t bitmapSize;
    if (!self || !dsc || (out && outSize == 0u) || (!out && outSize != 0u))
        return false;
    if (self->m_file)
        return XFontBitmapFace_fileLoadGlyph(font, codepoint, dsc, out,
                                             outSize);
    provider = &self->m_provider;
    info = provider->m_info;
    if (provider->m_data)
    {
        if (!XFontBitmapFace_findGlyph(provider->m_data, codepoint, dsc))
            return false;
        if (!out)
            return true;
        rowBytes = (size_t)XFontBitmapFace_rowBytes(dsc, info.m_bpp);
        bitmapSize = rowBytes * (size_t)dsc->box_h;
        if (!provider->m_data->glyph_bitmap || outSize < bitmapSize)
            return false;
        if (XFontBitmapFace_packedBytes(dsc, info.m_bpp) == bitmapSize)
            memcpy(out, provider->m_data->glyph_bitmap + dsc->bitmap_index,
                   bitmapSize);
        else
            XFontBitmapFace_unpack(provider->m_data->glyph_bitmap +
                                       dsc->bitmap_index, out, dsc,
                                   info.m_bpp, (int)rowBytes);
        if (bitmapSize < outSize)
            memset(out + bitmapSize, 0, outSize - bitmapSize);
        return true;
    }
    dsc->bitmap_index = 0;
    dsc->adv_w = (uint32_t)info.m_width * 16u;
    dsc->box_w = (uint8_t)info.m_width;
    dsc->box_h = (uint8_t)info.m_height;
    dsc->ofs_x = 0;
#if XFONT_LVGL_FMT_TXT_LARGE
    dsc->ofs_y = (int16_t)-info.m_descent;
#else
    dsc->ofs_y = (int8_t)-info.m_descent;
#endif
    if (!out)
        return true;
    if (outSize < (size_t)info.m_rowBytes * (size_t)info.m_height)
        return false;
    return provider->m_loadGlyph &&
           provider->m_loadGlyph(codepoint, out, outSize);
}

static int VXFontBitmapFace_rowBytes(const XFontFace* base,
                                     const XFont* font,
                                     const XFontGlyphDsc* dsc, int bpp)
{
    (void)base;
    (void)font;
    return XFontBitmapFace_rowBytes(dsc, bpp);
}

static void VXFontBitmapFace_deinit(XFontBitmapFace* self)
{
    if (self)
        XClass_Deinit_Parent(XFontFace, (XFontFace*)self);
}

static void VXFontBitmapFace_copy(XFontBitmapFace* dest,
                                  const XFontBitmapFace* src)
{
    if (!dest || !src || dest == src || !XClassGetVtable((XClass*)src))
        return;
    if (!XClassGetVtable((XClass*)dest))
    {
        if (src->m_file) XFontBitmapFace_initFile(dest);
        else XFontBitmapFace_init(dest, &src->m_provider);
    }
    dest->m_provider = src->m_provider;
    dest->m_file = src->m_file;
    dest->m_class.m_kind = src->m_class.m_kind;
    dest->m_class.m_family = src->m_class.m_family;
}

static void VXFontBitmapFace_move(XFontBitmapFace* dest, XFontBitmapFace* src)
{
    if (!dest || !src || dest == src || !XClassGetVtable((XClass*)src))
        return;
    VXFontBitmapFace_copy(dest, src);
    memset(&src->m_provider, 0, sizeof(src->m_provider));
    src->m_file = false;
    src->m_class.m_kind = XFontFace_None;
    src->m_class.m_family = NULL;
}

XVtable* XFontBitmapFace_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFontBitmapFace)
    XVTABLE_INHERIT_XCLASS(XFontFace);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFontBitmapFace_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFontBitmapFace_move);
    XVTABLE_OVERLOAD_DEFAULT(EXFontFace_Info, VXFontBitmapFace_info);
    XVTABLE_OVERLOAD_DEFAULT(EXFontFace_LoadBitmapGlyph,
                             VXFontBitmapFace_loadGlyph);
    XVTABLE_OVERLOAD_DEFAULT(EXFontFace_BitmapGlyphRowBytes,
                             VXFontBitmapFace_rowBytes);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFontBitmapFace_deinit);
    return XVTABLE_DEFAULT;
}

void XFontBitmapFace_init(XFontBitmapFace* self,
                          const XFontBitmapProvider* provider)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XFontBitmapFace);
    self->m_class.m_kind = XFontFace_Bitmap;
    if (provider)
    {
        self->m_provider = *provider;
        self->m_class.m_family = provider->m_family;
    }
}

void XFontBitmapFace_initFile(XFontBitmapFace* self)
{
    XFontBitmapFace_init(self, NULL);
    if (self)
        self->m_file = true;
}

bool XFontBitmapFace_registerProvider(const XFontBitmapProvider* provider)
{
    size_t i;
    if (!XFontBitmapFace_validProvider(provider))
        return false;
    for (i = 0; i < g_bitmapFaceCount; ++i)
        if (strcmp(g_bitmapFaces[i].m_class.m_family,
                   provider->m_family) == 0)
        {
            XFontBitmapFace_init(&g_bitmapFaces[i], provider);
            return XFontFace_register(&g_bitmapFaces[i].m_class);
        }
    if (g_bitmapFaceCount >= XFONT_MAX_BITMAP_PROVIDERS)
        return false;
    XFontBitmapFace_init(&g_bitmapFaces[g_bitmapFaceCount], provider);
    if (!XFontFace_register(&g_bitmapFaces[g_bitmapFaceCount].m_class))
        return false;
    ++g_bitmapFaceCount;
    return true;
}

const XFontFace* XFontBitmapFace_fileFace(void)
{
    if (!g_fileFaceInitialized)
    {
        XFontBitmapFace_initFile(&g_fileFace);
        g_fileFaceInitialized = true;
    }
    return &g_fileFace.m_class;
}
