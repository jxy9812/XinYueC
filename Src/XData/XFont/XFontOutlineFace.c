/******************************************************************************
 * @file       XFontOutlineFace.c
 * @brief      XFont 轮廓字库具体类实现。
 * @details    [已移除 2026-10-07] 原 m_file（外挂 XFO1 文件后端）分支与
 *             XFontOutlineFace_initFile/fileFace 随自研轮廓字实现整体
 *             删除；轮廓字实现只余 FT 文件后端（XFontFt，m_ft 位分发）
 *             与纯 provider 注册两类。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFontOutlineFace.h"
#include "XFont.h"
#include "XFontFace_Protected.h"
#include "XFontFt.h"
#include <string.h>

static XFontOutlineFace g_outlineFaces[XFONT_MAX_OUTLINE_PROVIDERS];
static size_t g_outlineFaceCount;

static bool XFontOutlineFace_validProvider(
    const XFontOutlineProvider* provider)
{
    if (!provider || !provider->m_family || !provider->m_family[0] ||
        !provider->m_loadGlyph || provider->m_info.unitsPerEm < 1 ||
        provider->m_info.unitsPerEm > 65535 || provider->m_info.ascent < 0 ||
        provider->m_info.descent < 0 || provider->m_info.lineGap < 0 ||
        provider->m_info.ascent > 32767 || provider->m_info.descent > 32767 ||
        provider->m_info.lineGap > 32767)
        return false;
    return true;
}

static bool VXFontOutlineFace_info(const XFontFace* base, const XFont* font,
                                   XFontFaceInfo* info)
{
    const XFontOutlineFace* self = (const XFontOutlineFace*)base;
    XFontOutlineInfo fileInfo;
    if (!self || !info)
        return false;
    if (self->m_ft)
    {
        /* FT 文件后端：度量经家族槽免字号读定。 */
        if (!XFontFt_fileInfo(font, &fileInfo))
            return false;
        info->m_kind = XFontFace_Outline;
        info->m_outline = fileInfo;
        return true;
    }
    info->m_kind = XFontFace_Outline;
    info->m_outline = self->m_provider.m_info;
    return true;
}

static bool VXFontOutlineFace_loadGlyph(
    const XFontFace* base, const XFont* font, uint32_t codepoint,
    XFontOutlineGlyphMetrics* metrics, const XFontOutlineSink* sink)
{
    const XFontOutlineFace* self = (const XFontOutlineFace*)base;
    if (!self || !metrics)
        return false;
    if (self->m_ft)
        return XFontFt_fileLoadGlyph(font, codepoint, metrics, sink);
    return self->m_provider.m_loadGlyph(codepoint, metrics, sink,
                                        self->m_provider.m_userData);
}

/* 新槽重载：两分支——m_ft 走带 float scale 的 FT 加载（像素→设计单位
   闭合回路的 FT 侧入口）；纯 provider 无字号载体，转发旧槽同款行为
   （scale 丢弃），既有 face 经新槽访问也逐位不变。 */
static bool VXFontOutlineFace_loadGlyphScaled(
    const XFontFace* base, const XFont* font, float scale, uint32_t codepoint,
    XFontOutlineGlyphMetrics* metrics, const XFontOutlineSink* sink)
{
    const XFontOutlineFace* self = (const XFontOutlineFace*)base;
    if (!self || !metrics)
        return false;
    if (self->m_ft)
        return XFontFt_fileLoadGlyphScaled(font, scale, codepoint, metrics,
                                           sink);
    return self->m_provider.m_loadGlyph(codepoint, metrics, sink,
                                        self->m_provider.m_userData);
}

static void VXFontOutlineFace_deinit(XFontOutlineFace* self)
{
    if (self)
        XClass_Deinit_Parent(XFontFace, (XFontFace*)self);
}

static void VXFontOutlineFace_copy(XFontOutlineFace* dest,
                                   const XFontOutlineFace* src)
{
    if (!dest || !src || dest == src || !XClassGetVtable((XClass*)src))
        return;
    if (!XClassGetVtable((XClass*)dest))
    {
        /* m_ft face 的 provider 载体已由 XFontFt 填全（家族槽桩），走
           init 分支即可；后端位在下方逐字段同步，拷贝不丢 FT 分支。 */
        XFontOutlineFace_init(dest, &src->m_provider);
    }
    dest->m_provider = src->m_provider;
    dest->m_ft = src->m_ft; /* 后端位必须同步：漏改=拷贝出的 face 静默丢 FT 分支。 */
    dest->m_class.m_kind = src->m_class.m_kind;
    dest->m_class.m_family = src->m_class.m_family;
}

static void VXFontOutlineFace_move(XFontOutlineFace* dest,
                                   XFontOutlineFace* src)
{
    if (!dest || !src || dest == src || !XClassGetVtable((XClass*)src))
        return;
    VXFontOutlineFace_copy(dest, src);
    memset(&src->m_provider, 0, sizeof(src->m_provider));
    src->m_ft = false; /* 移出侧后端位复位。 */
    src->m_class.m_kind = XFontFace_None;
    src->m_class.m_family = NULL;
}

XVtable* XFontOutlineFace_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFontOutlineFace)
    XVTABLE_INHERIT_XCLASS(XFontFace);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFontOutlineFace_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFontOutlineFace_move);
    XVTABLE_OVERLOAD_DEFAULT(EXFontFace_Info, VXFontOutlineFace_info);
    XVTABLE_OVERLOAD_DEFAULT(EXFontFace_LoadOutlineGlyph,
                             VXFontOutlineFace_loadGlyph);
    XVTABLE_OVERLOAD_DEFAULT(EXFontFace_LoadOutlineGlyphScaled,
                             VXFontOutlineFace_loadGlyphScaled);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFontOutlineFace_deinit);
    return XVTABLE_DEFAULT;
}

void XFontOutlineFace_init(XFontOutlineFace* self,
                           const XFontOutlineProvider* provider)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XFontOutlineFace);
    self->m_class.m_kind = XFontFace_Outline;
    if (provider)
    {
        self->m_provider = *provider;
        self->m_class.m_family = provider->m_family;
    }
}

bool XFontOutlineFace_registerProvider(const XFontOutlineProvider* provider)
{
    size_t i;
#if !XFONT_OUTLINE_ON
    (void)provider;
    return false;
#else
    if (!XFontOutlineFace_validProvider(provider))
        return false;
    for (i = 0; i < g_outlineFaceCount; ++i)
        if (strcmp(g_outlineFaces[i].m_class.m_family,
                   provider->m_family) == 0)
        {
            XFontOutlineFace_init(&g_outlineFaces[i], provider);
            return XFontFace_register(&g_outlineFaces[i].m_class);
        }
    if (g_outlineFaceCount >= XFONT_MAX_OUTLINE_PROVIDERS)
        return false;
    XFontOutlineFace_init(&g_outlineFaces[g_outlineFaceCount], provider);
    if (!XFontFace_register(&g_outlineFaces[g_outlineFaceCount].m_class))
        return false;
    ++g_outlineFaceCount;
    return true;
#endif
}
