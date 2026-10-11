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
#include "XFontFt.h"
#include "XDateTime.h" /* 文件探测退避计时：单调毫秒（时间源约束同 XWindowDecoration 限帧） */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

enum
{
    XFONT_FACE_REGISTRY_CAPACITY = XFONT_MAX_BITMAP_PROVIDERS +
                                   XFONT_MAX_OUTLINE_PROVIDERS + 2
};

static XFontFace* g_faces[XFONT_FACE_REGISTRY_CAPACITY];
static size_t g_faceCount;
static bool g_builtinProvidersInitialized;
/* FT 探测失败的一次性 stderr 警告旗标（进程期只告警一次，见 XFont_face）。 */
static bool g_ftProbeFailedWarned;

/* 文件探测退避窗口（ms）：FT/位图两级文件探测全落空（缺文件降级）后，
 * 窗口内跳过文件 I/O——XFont_face 位于逐字形热路径，缺字库时每字形
 * 最多 6+ 次失败 open 即「降级卡顿」（用户实测 2026-10-11）。窗口过期
 * 自动重探，「文件落盘自动转正」语义保留，采纳延迟≤一个窗口；命中
 * 任一级则不记戳，正常路径零额外开销。 */
#define XFONT_FILE_PROBE_BACKOFF_MS 500
static int64_t g_fileProbeBackoffMs; /* 0=无退避；>0=上次全落空时刻。 */

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

/* 新槽默认实现（§1.2）：无字号载体的 face 一律转发既有 LoadOutlineGlyph
   槽位（scale 丢弃）——点阵/纯 provider 等既有 face 零改动，行为逐位
   不变；scale 合法性已在包装函数校验。 */
static bool VXFontFace_loadOutlineGlyphScaled(const XFontFace* self,
                                              const XFont* font, float scale,
                                              uint32_t codepoint,
                                              XFontOutlineGlyphMetrics* metrics,
                                              const XFontOutlineSink* sink)
{
    (void)scale;
    return XFontFace_loadOutlineGlyph_base(self, font, codepoint, metrics,
                                           sink);
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
        (void*)VXFontFace_bitmapGlyphRowBytes,
        (void*)VXFontFace_loadOutlineGlyphScaled
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

const XFontFace* XFont_face(const XFont* font)
{
    const char* family = font ? XFont_family(font) : XFONT_DEFAULT_FAMILY;
    const XFontFace* bitmap;
    const XFontFace* outline;
    XFontBitmapInfo bitmapFileInfo;
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
    /* 退避闸（降级卡顿根修 2026-10-11）：两级文件探测全落空后的窗口内
     * 跳过文件 I/O，直接落既有注册表/位图回退——逐字形热路径不再付
     * 失败 open 风暴；窗口过期自动重探，文件落盘转正延迟≤一个窗口
     * （语义见 g_fileProbeBackoffMs 处注）。 */
    {
        int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        if (g_fileProbeBackoffMs != 0 &&
            nowMs - g_fileProbeBackoffMs < XFONT_FILE_PROBE_BACKOFF_MS)
        {
            if (forceOutline)
                return NULL;
            return XFontFace_first(preferOutline ? XFontFace_Outline :
                                   XFontFace_Bitmap);
        }
    }
#if XFONT_FT_FACE_ON
    /* FT 是唯一轮廓字实现（自研 XFO1 链已移除）：先探 .ttf/.otf/.ttc。
       命中即返回家族槽静态 face——其 m_ft 位使两槽位分发均走 XFontFt。 */
    {
        XFontOutlineInfo outlineFileInfo;
        memset(&outlineFileInfo, 0, sizeof(outlineFileInfo));
        if (XFontFt_fileInfo(font, &outlineFileInfo))
            return XFontFt_fileFace(family);
    }
    /* FT 探测失败（缺文件/魔数不符/解析锁存/家族槽满/init 失败）：
       落位图回退，进程期只发一行 stderr 警告（XFont_face 位于逐字形
       热路径，逐次告警会刷屏；缺文件可重探，文件落盘后自动转正）。 */
    if (!g_ftProbeFailedWarned)
    {
        g_ftProbeFailedWarned = true;
        fprintf(stderr,
                "XFont: FT outline font resolve failed for family '%s' "
                "(missing or corrupt file?), falling back to bitmap\n",
                family);
    }
#endif
    if (forceOutline)
    {
        g_fileProbeBackoffMs = XDateTime_currentMSecsSinceEpoch();
        return NULL;
    }
    if (XFontBitmapFace_fileInfo(font, &bitmapFileInfo))
        return XFontBitmapFace_fileFace();
    /* 两级文件探测全落空：记退避戳（窗口内后续字形零文件 I/O）。 */
    g_fileProbeBackoffMs = XDateTime_currentMSecsSinceEpoch();
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

bool XFontFace_loadOutlineGlyphScaled_base(const XFontFace* self,
                                           const XFont* font, float scale,
                                           uint32_t codepoint,
                                           XFontOutlineGlyphMetrics* metrics,
                                           const XFontOutlineSink* sink)
{
    /* scale = target_px/unitsPerEm（§1.2）：<=0 或非有限视为非法直接判负
       （合法域由 painter 既有整数目标字号保证，此处兜底防误用）。 */
    if (!self || !XClassGetVtable(self) || !(scale > 0.0f) ||
        !isfinite((double)scale))
        return false;
    return XClassGetVirtualFunc(self, EXFontFace_LoadOutlineGlyphScaled,
        bool(*)(const XFontFace*, const XFont*, float, uint32_t,
                XFontOutlineGlyphMetrics*, const XFontOutlineSink*))
        (self, font, scale, codepoint, metrics, sink);
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
