/******************************************************************************
 * @file       XFontFace.c
 * @brief      XFont 字库后端抽象类、注册表和解析器实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFontFace.h"
#include "XFont.h"
#include "XFontFace_Protected.h"
#include "XFontBitmapFace.h"
#include "XFontOutlineFace.h"
#include <string.h>

enum
{
    XFONT_FACE_REGISTRY_CAPACITY = XFONT_MAX_BITMAP_PROVIDERS +
                                   XFONT_MAX_OUTLINE_PROVIDERS + 2
};

static XFontFace* g_faces[XFONT_FACE_REGISTRY_CAPACITY];
static size_t g_faceCount;
static bool g_builtinProvidersInitialized;

/* Built-in faces are linked according to XFont_config.h and registered on the
   first face lookup, keeping startup cost out of applications that do not use
   fonts. */
static void XFontFace_ensureBuiltinProviders(void)
{
    if (g_builtinProvidersInitialized)
        return;
    g_builtinProvidersInitialized = true;
    XFontFace_registerBuiltinProviders();
}

static bool VXFontFace_info(const XFontFace* self, const XFont* font,
                            XFontFaceInfo* info)
{
    (void)self;
    (void)font;
    (void)info;
    return false;
}

static bool VXFontFace_loadBitmapGlyph(const XFontFace* self,
                                       const XFont* font, uint32_t codepoint,
                                       XFontGlyphDsc* dsc, unsigned char* out,
                                       size_t outSize)
{
    (void)self;
    (void)font;
    (void)codepoint;
    (void)dsc;
    (void)out;
    (void)outSize;
    return false;
}

static bool VXFontFace_loadOutlineGlyph(const XFontFace* self,
                                        const XFont* font, uint32_t codepoint,
                                        XFontOutlineGlyphMetrics* metrics,
                                        const XFontOutlineSink* sink)
{
    (void)self;
    (void)font;
    (void)codepoint;
    (void)metrics;
    (void)sink;
    return false;
}

static int VXFontFace_bitmapGlyphRowBytes(const XFontFace* self,
                                          const XFont* font,
                                          const XFontGlyphDsc* dsc, int bpp)
{
    (void)self;
    (void)font;
    if (!dsc || dsc->box_w == 0 || dsc->box_h == 0 ||
        (bpp != 1 && bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8))
        return 0;
    return ((int)dsc->box_w * bpp + 7) / 8;
}

static void VXFontFace_deinit(XFontFace* self)
{
    if (self)
        XClass_Deinit_Parent(XClass, (XClass*)self);
}

XVtable* XFontFace_class_init(void)
{
    void* table[] = {
        (void*)VXFontFace_info,
        (void*)VXFontFace_loadBitmapGlyph,
        (void*)VXFontFace_loadOutlineGlyph,
        (void*)VXFontFace_bitmapGlyphRowBytes
    };
    XVTABLE_INIT_DEFAULT(XFontFace)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFontFace_deinit);
    return XVTABLE_DEFAULT;
}

void XFontFace_init(XFontFace* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XFontFace);
    self->m_kind = XFontFace_None;
}

bool XFontFace_register(XFontFace* face)
{
    size_t i;
    if (!face || !face->m_family || !face->m_family[0] ||
        (face->m_kind != XFontFace_Bitmap &&
         face->m_kind != XFontFace_Outline) ||
        !XClassGetVtable(face))
        return false;
    for (i = 0; i < g_faceCount; ++i)
    {
        XFontFace* current = g_faces[i];
        if (current && current->m_kind == face->m_kind &&
            strcmp(current->m_family, face->m_family) == 0)
        {
            g_faces[i] = face;
            return true;
        }
    }
    if (g_faceCount >= XFONT_FACE_REGISTRY_CAPACITY)
        return false;
    g_faces[g_faceCount++] = face;
    return true;
}

static const XFontFace* XFontFace_find(const char* family,
                                       XFontFaceKind kind)
{
    size_t i;
    if (!family || !family[0])
        return NULL;
    for (i = 0; i < g_faceCount; ++i)
    {
        const XFontFace* face = g_faces[i];
        if (face && face->m_kind == kind &&
            strcmp(face->m_family, family) == 0)
            return face;
    }
    return NULL;
}

static const XFontFace* XFontFace_first(XFontFaceKind kind)
{
    size_t i;
    for (i = 0; i < g_faceCount; ++i)
        if (g_faces[i] && g_faces[i]->m_kind == kind)
            return g_faces[i];
    return NULL;
}

static bool XFontFace_hasXfoSuffix(const char* family)
{
    size_t length;
    if (!family)
        return false;
    length = strlen(family);
    return length >= 4u && family[length - 4u] == '.' &&
           (family[length - 3u] == 'x' || family[length - 3u] == 'X') &&
           (family[length - 2u] == 'f' || family[length - 2u] == 'F') &&
           (family[length - 1u] == 'o' || family[length - 1u] == 'O');
}

const XFontFace* XFont_face(const XFont* font)
{
    const char* family = font ? XFont_family(font) : XFONT_DEFAULT_FAMILY;
    const XFontFace* bitmap;
    const XFontFace* outline;
    XFontBitmapInfo bitmapFileInfo;
    XFontOutlineInfo outlineFileInfo;
    bool hasBitmapFile;
    bool hasOutlineFile;
    int strategy = font ? XFont_styleStrategy(font) : XFont_PreferDefault;
    bool forceOutline = (strategy & XFont_ForceOutline) != 0;
    bool preferOutline = forceOutline ||
                         ((strategy & XFont_PreferOutline) != 0);

    if (!family || !family[0])
        family = XFONT_DEFAULT_FAMILY;

    XFontFace_ensureBuiltinProviders();

    bitmap = XFontFace_find(family, XFontFace_Bitmap);
    outline = XFontFace_find(family, XFontFace_Outline);
    if (preferOutline)
    {
        if (outline)
            return outline;
    }
    if (bitmap && !forceOutline)
        return bitmap;
    if (outline)
        return outline;

    /* A path or an unknown family can be resolved by the file backends. */
    memset(&bitmapFileInfo, 0, sizeof(bitmapFileInfo));
    memset(&outlineFileInfo, 0, sizeof(outlineFileInfo));
    hasBitmapFile = XFontBitmapFace_fileInfo(font, &bitmapFileInfo);
    hasOutlineFile = XFontOutlineFace_fileInfo(font, &outlineFileInfo);
    if (hasOutlineFile &&
        (preferOutline || XFontFace_hasXfoSuffix(family) ||
         !hasBitmapFile))
        return XFontOutlineFace_fileFace();
    if (forceOutline)
        return NULL;
    if (hasBitmapFile)
        return XFontBitmapFace_fileFace();
    if (hasOutlineFile)
        return XFontOutlineFace_fileFace();
    return XFontFace_first(preferOutline ? XFontFace_Outline :
                           XFontFace_Bitmap);
}

XFontFaceKind XFontFace_kind(const XFontFace* self)
{
    return self ? self->m_kind : XFontFace_None;
}

bool XFontFace_info_base(const XFontFace* self, const XFont* font,
                         XFontFaceInfo* info)
{
    if (!info || !self || !XClassGetVtable(self))
        return false;
    return XClassGetVirtualFunc(self, EXFontFace_Info,
        bool(*)(const XFontFace*, const XFont*, XFontFaceInfo*))
        (self, font, info);
}

bool XFontFace_loadBitmapGlyph_base(const XFontFace* self, const XFont* font,
                                    uint32_t codepoint, XFontGlyphDsc* dsc,
                                    unsigned char* out, size_t outSize)
{
    if (!self || !XClassGetVtable(self))
        return false;
    return XClassGetVirtualFunc(self, EXFontFace_LoadBitmapGlyph,
        bool(*)(const XFontFace*, const XFont*, uint32_t, XFontGlyphDsc*,
                unsigned char*, size_t))(self, font, codepoint, dsc, out,
                                          outSize);
}

bool XFontFace_loadOutlineGlyph_base(const XFontFace* self, const XFont* font,
                                     uint32_t codepoint,
                                     XFontOutlineGlyphMetrics* metrics,
                                     const XFontOutlineSink* sink)
{
    if (!self || !XClassGetVtable(self))
        return false;
    return XClassGetVirtualFunc(self, EXFontFace_LoadOutlineGlyph,
        bool(*)(const XFontFace*, const XFont*, uint32_t,
                XFontOutlineGlyphMetrics*, const XFontOutlineSink*))
        (self, font, codepoint, metrics, sink);
}

int XFontFace_bitmapGlyphRowBytes_base(const XFontFace* self,
                                       const XFont* font,
                                       const XFontGlyphDsc* dsc, int bpp)
{
    if (!self || !XClassGetVtable(self))
        return 0;
    return XClassGetVirtualFunc(self, EXFontFace_BitmapGlyphRowBytes,
        int(*)(const XFontFace*, const XFont*, const XFontGlyphDsc*, int))
        (self, font, dsc, bpp);
}
