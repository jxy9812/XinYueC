/******************************************************************************
 * @file       XSurfaceFormat.c
 * @brief      XSurfaceFormat 表面格式值类型实现（对标 Qt 6.8 QSurfaceFormat）。
 * @details    默认值与 Qt 6.8.3 qsurfaceformat.cpp QSurfaceFormatPrivate 构造
 *             完全一致：红/绿/蓝/Alpha/深度/模板缓冲大小 -1、采样数 -1、
 *             交换行为 Default、可渲染类型 Default、profile NoProfile、
 *             OpenGL 版本 2.0、交换间隔 1、无立体/选项、空色彩空间。
 *             提供进程级默认格式（对标 setDefaultFormat/defaultFormat），
 *             初始为出厂默认值。所有接口均为值语义，不分配外部资源。
 * @note       模块总开关 XSURFACEFORMAT_ON 定义于 XGuiConfig.h；置 0 时
 *             本文件实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XSurfaceFormat.h"
#include "XGuiConfig.h"
#include "XColorSpace.h"
#include <string.h>

#if XSURFACEFORMAT_ON

/** @brief 进程级默认格式；初始为出厂默认值。 */
static XSurfaceFormat g_defaultFormat;

/** @brief 默认格式是否已被显式设置过（未设置时返回出厂默认）。 */
static bool g_defaultFormatSet = false;

/** @brief 构造出厂默认格式（与 Qt QSurfaceFormatPrivate 一致）。 */
static XSurfaceFormat XSurfaceFormat_makeDefault(void)
{
    XSurfaceFormat f;
    memset(&f, 0, sizeof(f));
    f.m_options = 0;
    f.m_redBufferSize = -1;
    f.m_greenBufferSize = -1;
    f.m_blueBufferSize = -1;
    f.m_alphaBufferSize = -1;
    f.m_depthBufferSize = -1;
    f.m_stencilBufferSize = -1;
    f.m_samples = -1;
    f.m_swapBehavior = XSurfaceFormat_DefaultSwapBehavior;
    f.m_renderableType = XSurfaceFormat_DefaultRenderableType;
    f.m_profile = XSurfaceFormat_NoProfile;
    f.m_majorVersion = 2;
    f.m_minorVersion = 0;
    f.m_swapInterval = 1;
    f.m_stereo = false;
    f.m_colorSpace = XColorSpace_create();
    return f;
}

XSurfaceFormat XSurfaceFormat_create(void)
{
    if (g_defaultFormatSet)
        return g_defaultFormat;
    return XSurfaceFormat_makeDefault();
}

XSurfaceFormat XSurfaceFormat_create_ex(XSurfaceFormatOptions options)
{
    XSurfaceFormat f = XSurfaceFormat_create();
    f.m_options = options;
    f.m_stereo = (options & (XSurfaceFormatOptions)XSurfaceFormat_StereoBuffers) != 0;
    return f;
}

XSurfaceFormat XSurfaceFormat_copy(const XSurfaceFormat* self)
{
    return self ? *self : XSurfaceFormat_create();
}

/* ==================== 缓冲大小 ==================== */

void XSurfaceFormat_setDepthBufferSize(XSurfaceFormat* self, int size)
{
    if (self) self->m_depthBufferSize = size;
}

int XSurfaceFormat_depthBufferSize(const XSurfaceFormat* self)
{
    return self ? self->m_depthBufferSize : -1;
}

void XSurfaceFormat_setStencilBufferSize(XSurfaceFormat* self, int size)
{
    if (self) self->m_stencilBufferSize = size;
}

int XSurfaceFormat_stencilBufferSize(const XSurfaceFormat* self)
{
    return self ? self->m_stencilBufferSize : -1;
}

void XSurfaceFormat_setRedBufferSize(XSurfaceFormat* self, int size)
{
    if (self) self->m_redBufferSize = size;
}

int XSurfaceFormat_redBufferSize(const XSurfaceFormat* self)
{
    return self ? self->m_redBufferSize : -1;
}

void XSurfaceFormat_setGreenBufferSize(XSurfaceFormat* self, int size)
{
    if (self) self->m_greenBufferSize = size;
}

int XSurfaceFormat_greenBufferSize(const XSurfaceFormat* self)
{
    return self ? self->m_greenBufferSize : -1;
}

void XSurfaceFormat_setBlueBufferSize(XSurfaceFormat* self, int size)
{
    if (self) self->m_blueBufferSize = size;
}

int XSurfaceFormat_blueBufferSize(const XSurfaceFormat* self)
{
    return self ? self->m_blueBufferSize : -1;
}

void XSurfaceFormat_setAlphaBufferSize(XSurfaceFormat* self, int size)
{
    if (self) self->m_alphaBufferSize = size;
}

int XSurfaceFormat_alphaBufferSize(const XSurfaceFormat* self)
{
    return self ? self->m_alphaBufferSize : -1;
}

void XSurfaceFormat_setSamples(XSurfaceFormat* self, int numSamples)
{
    if (self) self->m_samples = numSamples;
}

int XSurfaceFormat_samples(const XSurfaceFormat* self)
{
    return self ? self->m_samples : -1;
}

/* ==================== 交换行为 / Alpha ==================== */

void XSurfaceFormat_setSwapBehavior(XSurfaceFormat* self,
                                    XSurfaceFormatSwapBehavior behavior)
{
    if (self) self->m_swapBehavior = behavior;
}

XSurfaceFormatSwapBehavior XSurfaceFormat_swapBehavior(const XSurfaceFormat* self)
{
    if (!self) return XSurfaceFormat_DefaultSwapBehavior;
    return self->m_swapBehavior;
}

bool XSurfaceFormat_hasAlpha(const XSurfaceFormat* self)
{
    return self && self->m_alphaBufferSize >= 0;
}

/* ==================== 渲染配置 ==================== */

void XSurfaceFormat_setProfile(XSurfaceFormat* self, XSurfaceFormatProfile profile)
{
    if (self) self->m_profile = profile;
}

XSurfaceFormatProfile XSurfaceFormat_profile(const XSurfaceFormat* self)
{
    if (!self) return XSurfaceFormat_NoProfile;
    return self->m_profile;
}

void XSurfaceFormat_setRenderableType(XSurfaceFormat* self,
                                      XSurfaceFormatRenderableType type)
{
    if (self) self->m_renderableType = type;
}

XSurfaceFormatRenderableType XSurfaceFormat_renderableType(const XSurfaceFormat* self)
{
    if (!self) return XSurfaceFormat_DefaultRenderableType;
    return self->m_renderableType;
}

void XSurfaceFormat_setMajorVersion(XSurfaceFormat* self, int majorVersion)
{
    if (self) self->m_majorVersion = majorVersion;
}

int XSurfaceFormat_majorVersion(const XSurfaceFormat* self)
{
    return self ? self->m_majorVersion : 0;
}

void XSurfaceFormat_setMinorVersion(XSurfaceFormat* self, int minorVersion)
{
    if (self) self->m_minorVersion = minorVersion;
}

int XSurfaceFormat_minorVersion(const XSurfaceFormat* self)
{
    return self ? self->m_minorVersion : 0;
}

void XSurfaceFormat_setVersion(XSurfaceFormat* self, int major, int minor)
{
    if (!self) return;
    self->m_majorVersion = major;
    self->m_minorVersion = minor;
}

void XSurfaceFormat_version(const XSurfaceFormat* self,
                            int* major, int* minor)
{
    if (major) *major = self ? self->m_majorVersion : 0;
    if (minor) *minor = self ? self->m_minorVersion : 0;
}

/* ==================== 立体缓冲 ==================== */

void XSurfaceFormat_setStereo(XSurfaceFormat* self, bool enable)
{
    if (!self) return;
    /* Qt stores stereo buffering in FormatOptions; keep the cached convenience
       flag in sync as well so setStereo(), setOption(StereoBuffers), and
       stereo()/testOption() observe one state. */
    self->m_stereo = enable;
    if (enable)
        self->m_options |= (XSurfaceFormatOptions)XSurfaceFormat_StereoBuffers;
    else
        self->m_options &= ~(XSurfaceFormatOptions)XSurfaceFormat_StereoBuffers;
}

bool XSurfaceFormat_stereo(const XSurfaceFormat* self)
{
    return self ? self->m_stereo : false;
}

/* ==================== 格式选项 ==================== */

void XSurfaceFormat_setOptions(XSurfaceFormat* self, XSurfaceFormatOptions options)
{
    if (!self) return;
    self->m_options = options;
    self->m_stereo = (options & (XSurfaceFormatOptions)XSurfaceFormat_StereoBuffers) != 0;
}

void XSurfaceFormat_setOption(XSurfaceFormat* self,
                              XSurfaceFormatOption option, bool on)
{
    if (!self) return;
    if (on)
        self->m_options |= (XSurfaceFormatOptions)option;
    else
        self->m_options &= ~(XSurfaceFormatOptions)option;
    if (option == XSurfaceFormat_StereoBuffers)
        self->m_stereo = on;
}

bool XSurfaceFormat_testOption(const XSurfaceFormat* self,
                               XSurfaceFormatOption option)
{
    return self && (self->m_options & (XSurfaceFormatOptions)option) != 0;
}

XSurfaceFormatOptions XSurfaceFormat_options(const XSurfaceFormat* self)
{
    return self ? self->m_options : 0;
}

/* ==================== 交换间隔 ==================== */

void XSurfaceFormat_setSwapInterval(XSurfaceFormat* self, int interval)
{
    if (self) self->m_swapInterval = interval;
}

int XSurfaceFormat_swapInterval(const XSurfaceFormat* self)
{
    return self ? self->m_swapInterval : 1;
}

/* ==================== 色彩空间 ==================== */

XColorSpace XSurfaceFormat_colorSpace(const XSurfaceFormat* self)
{
    if (self)
        return self->m_colorSpace;
    return XColorSpace_create();
}

void XSurfaceFormat_setColorSpace(XSurfaceFormat* self, XColorSpace colorSpace)
{
    if (self) self->m_colorSpace = colorSpace;
}

/* ==================== 进程级默认格式 ==================== */

void XSurfaceFormat_setDefaultFormat(const XSurfaceFormat* format)
{
    if (!format) {
        g_defaultFormat = XSurfaceFormat_makeDefault();
        g_defaultFormatSet = true;
        return;
    }
    g_defaultFormat = *format;
    g_defaultFormat.m_stereo =
        (g_defaultFormat.m_options & (XSurfaceFormatOptions)XSurfaceFormat_StereoBuffers) != 0;
    g_defaultFormatSet = true;
}

XSurfaceFormat XSurfaceFormat_defaultFormat(void)
{
    return XSurfaceFormat_create();
}

/* ==================== 相等比较 ==================== */

bool XSurfaceFormat_equals(const XSurfaceFormat* lhs, const XSurfaceFormat* rhs)
{
    if (!lhs || !rhs) return lhs == rhs;
    return lhs->m_options == rhs->m_options &&
           lhs->m_redBufferSize == rhs->m_redBufferSize &&
           lhs->m_greenBufferSize == rhs->m_greenBufferSize &&
           lhs->m_blueBufferSize == rhs->m_blueBufferSize &&
           lhs->m_alphaBufferSize == rhs->m_alphaBufferSize &&
           lhs->m_depthBufferSize == rhs->m_depthBufferSize &&
           lhs->m_stencilBufferSize == rhs->m_stencilBufferSize &&
           lhs->m_samples == rhs->m_samples &&
           lhs->m_swapBehavior == rhs->m_swapBehavior &&
           lhs->m_renderableType == rhs->m_renderableType &&
           lhs->m_profile == rhs->m_profile &&
           lhs->m_majorVersion == rhs->m_majorVersion &&
           lhs->m_minorVersion == rhs->m_minorVersion &&
           lhs->m_swapInterval == rhs->m_swapInterval &&
           lhs->m_stereo == rhs->m_stereo &&
           XColorSpace_equals(&lhs->m_colorSpace, &rhs->m_colorSpace);
}

#endif /* XSURFACEFORMAT_ON */
