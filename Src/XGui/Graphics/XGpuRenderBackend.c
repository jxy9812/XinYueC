/******************************************************************************
 * @file       XGpuRenderBackend.c
 * @brief      XGui 阶段 1 OpenGL GPU 光栅后端。
 * @details    后端只依赖 XPlatformOffscreenSurface 的不透明接口，不包含
 *             平台 GL 头。离屏上下文中创建 RGBA8 FBO，使用 GLES 2.0
 *             兼容的最小 shader/纹理管线绘制矩形、图像和 CPU 字形，帧末
 *             readback 到 XImage。创建或运行时任一步失败都由 XPainter
 *             转回软件光栅，保证 GPU 不可用时行为不变。
 ******************************************************************************/
#include "XGpuRenderBackend.h"
#include "XDateTime.h"
#include <stdlib.h>
#include <stdio.h>

#if XPLATFORMINTEGRATION_ON && XGPU_ON

#include "XPlatformGraphics.h"
#include "XImage.h"
#include "XMemory.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==================== 最小 GLES 2/桌面 GL 类型与常量 ==================== */

typedef unsigned int XglEnum;
typedef unsigned int XglBitfield;
typedef unsigned int XglUInt;
typedef int XglInt;
typedef int XglSizei;
typedef ptrdiff_t XglSizeiptr;
typedef float XglFloat;
typedef unsigned char XglBoolean;
typedef char XglChar;

#define XGL_FALSE                  0
#define XGL_COLOR_BUFFER_BIT       0x00004000u
#define XGL_BLEND                  0x0BE2u
#define XGL_SCISSOR_TEST           0x0C11u
#define XGL_ONE                    1u
#define XGL_SRC_ALPHA              0x0302u
#define XGL_ONE_MINUS_SRC_ALPHA    0x0303u
#define XGL_FRAMEBUFFER             0x8D40u
#define XGL_READ_FRAMEBUFFER        0x8CA8u
#define XGL_DRAW_FRAMEBUFFER        0x8CA9u
#define XGL_COLOR_ATTACHMENT0       0x8CE0u
#define XGL_FRAMEBUFFER_COMPLETE     0x8CD5u
#define XGL_TEXTURE_2D              0x0DE1u
#define XGL_TEXTURE0                0x84C0u
#define XGL_RGBA                    0x1908u
#define XGL_UNSIGNED_BYTE           0x1401u
#define XGL_TEXTURE_MIN_FILTER      0x2801u
#define XGL_TEXTURE_MAG_FILTER      0x2800u
#define XGL_TEXTURE_WRAP_S          0x2802u
#define XGL_TEXTURE_WRAP_T          0x2803u
#define XGL_NEAREST                 0x2600u
#define XGL_CLAMP_TO_EDGE           0x812Fu
#define XGL_UNPACK_ALIGNMENT        0x0CF5u
#define XGL_PACK_ALIGNMENT           0x0D05u
#define XGL_ARRAY_BUFFER             0x8892u
#define XGL_DYNAMIC_DRAW             0x88E8u
#define XGL_FLOAT                    0x1406u
#define XGL_TRIANGLE_STRIP          0x0005u
#define XGL_VERTEX_SHADER            0x8B31u
#define XGL_FRAGMENT_SHADER          0x8B30u
#define XGL_COMPILE_STATUS           0x8B81u
#define XGL_LINK_STATUS              0x8B82u

typedef void (*XglGenObjectsProc)(XglSizei, XglUInt*);
typedef void (*XglDeleteObjectsProc)(XglSizei, const XglUInt*);

/* ==================== 字形图集（阶段 3） ==================== */

/** @brief 图集纹理边长（像素；512x512 RGBA ≈ 4MB，可容约 4 千个 8px 字形）。 */
#define XGPU_GLYPH_ATLAS_SIZE 512
/** @brief 图集条目上限（超出即整体重置，防止条目数组无界增长）。 */
#define XGPU_GLYPH_ATLAS_MAX_ENTRIES 8192

/** @brief 字形图集条目：键 + 尺寸 + 图集内位置（行式 shelf 装箱）。 */
typedef struct XGpuGlyphAtlasEntry
{
    uint64_t m_key;
    int m_width;
    int m_height;
    int m_x;
    int m_y;
} XGpuGlyphAtlasEntry;

/* ==================== 运行期 GL 函数表 ==================== */

struct XGpuRenderBackend
{
    XPlatformOffscreenSurface* m_surface;   /**< 离屏上下文宿主（离屏模式）。 */
    XPlatformOpenGLContext* m_windowContext; /**< 窗口 GL 上下文（窗口直通模式）。 */
    bool m_windowMode;                       /**< 是否窗口直通模式。 */
    int m_width;
    int m_height;
    bool m_valid;
    bool m_firstFrame;   /**< 窗口直通模式：会话创建/重建后首帧需初始化内容。 */

    XglUInt m_framebuffer;
    XglUInt m_colorTexture;
    XglUInt m_sourceTexture;
    XglUInt m_vertexBuffer;
    XglUInt m_solidProgram;
    XglUInt m_textureProgram;
    XglInt m_solidColorLocation;
    XglInt m_textureSamplerLocation;
    XglInt m_textureModulateLocation;

    uint8_t* m_pixels;
    size_t m_pixelsCapacity;

    void (*glBlitFramebuffer)(XglInt, XglInt, XglInt, XglInt, XglInt, XglInt,
                              XglInt, XglInt, XglBitfield, XglEnum);
    bool m_hasBlit;                /**< glBlitFramebuffer 可用（2b 快路径）。 */

    XglUInt m_glyphAtlasTexture;   /**< 字形图集纹理（RGBA 四通道=覆盖度）。 */
    XGpuGlyphAtlasEntry* m_glyphEntries; /**< 图集条目数组（拥有）。 */
    int m_glyphEntryCount;
    int m_glyphEntryCapacity;
    int m_glyphCursorX;            /**< shelf 装箱当前行游标。 */
    int m_glyphCursorY;
    int m_glyphRowHeight;
    unsigned m_glyphUploads;       /**< 累计上传次数（含重置后重传）。 */
    unsigned m_glyphHits;          /**< 累计命中次数。 */

    void (*glViewport)(XglInt, XglInt, XglSizei, XglSizei);
    void (*glClearColor)(XglFloat, XglFloat, XglFloat, XglFloat);
    void (*glClear)(XglBitfield);
    void (*glEnable)(XglEnum);
    void (*glDisable)(XglEnum);
    void (*glBlendFunc)(XglEnum, XglEnum);
    void (*glScissor)(XglInt, XglInt, XglSizei, XglSizei);
    void (*glPixelStorei)(XglEnum, XglInt);
    void (*glReadPixels)(XglInt, XglInt, XglSizei, XglSizei,
                         XglEnum, XglEnum, void*);

    XglGenObjectsProc glGenFramebuffers;
    XglDeleteObjectsProc glDeleteFramebuffers;
    void (*glBindFramebuffer)(XglEnum, XglUInt);
    void (*glFramebufferTexture2D)(XglEnum, XglEnum, XglEnum, XglUInt,
                                   XglInt);
    XglEnum (*glCheckFramebufferStatus)(XglEnum);

    XglGenObjectsProc glGenTextures;
    XglDeleteObjectsProc glDeleteTextures;
    const XglChar* (*glGetString)(XglEnum);
    void (*glBindTexture)(XglEnum, XglUInt);
    void (*glTexParameteri)(XglEnum, XglEnum, XglInt);
    void (*glTexImage2D)(XglEnum, XglInt, XglInt, XglSizei, XglSizei,
                         XglInt, XglEnum, XglEnum, const void*);
    void (*glTexSubImage2D)(XglEnum, XglInt, XglInt, XglInt, XglSizei,
                            XglSizei, XglEnum, XglEnum, const void*);
    void (*glActiveTexture)(XglEnum);

    XglGenObjectsProc glGenBuffers;
    XglDeleteObjectsProc glDeleteBuffers;
    void (*glBindBuffer)(XglEnum, XglUInt);
    void (*glBufferData)(XglEnum, XglSizeiptr, const void*, XglEnum);
    void (*glEnableVertexAttribArray)(XglUInt);
    void (*glDisableVertexAttribArray)(XglUInt);
    void (*glVertexAttribPointer)(XglUInt, XglInt, XglEnum, XglBoolean,
                                  XglSizei, const void*);
    void (*glDrawArrays)(XglEnum, XglInt, XglSizei);

    XglUInt (*glCreateShader)(XglEnum);
    void (*glShaderSource)(XglUInt, XglSizei, const XglChar* const*,
                           const XglInt*);
    void (*glCompileShader)(XglUInt);
    void (*glGetShaderiv)(XglUInt, XglEnum, XglInt*);
    void (*glDeleteShader)(XglUInt);
    XglUInt (*glCreateProgram)(void);
    void (*glAttachShader)(XglUInt, XglUInt);
    void (*glBindAttribLocation)(XglUInt, XglUInt, const XglChar*);
    void (*glLinkProgram)(XglUInt);
    void (*glGetProgramiv)(XglUInt, XglEnum, XglInt*);
    void (*glDeleteProgram)(XglUInt);
    void (*glUseProgram)(XglUInt);
    XglInt (*glGetUniformLocation)(XglUInt, const XglChar*);
    void (*glUniform1i)(XglInt, XglInt);
    void (*glUniform4f)(XglInt, XglFloat, XglFloat, XglFloat, XglFloat);
};

static void* xgpu_proc(XGpuRenderBackend* self, const char* name)
{
    if (!self || !name) return NULL;
    if (self->m_windowMode)
        return self->m_windowContext
            ? XPlatformOpenGLContext_getProcAddress(self->m_windowContext, name)
            : NULL;
    return self->m_surface
        ? XPlatformOffscreenSurface_getProcAddress(self->m_surface, name)
        : NULL;
}

/* ==================== 上下文操作抽象（离屏 / 窗口直通共用） ==================== */

static bool xgpu_make_current(XGpuRenderBackend* self)
{
    if (!self) return false;
    if (self->m_windowMode)
        return self->m_windowContext &&
               XPlatformOpenGLContext_makeCurrent(self->m_windowContext);
    return self->m_surface &&
           XPlatformOffscreenSurface_makeCurrent(self->m_surface);
}

static void xgpu_done_current(XGpuRenderBackend* self)
{
    if (!self) return;
    if (self->m_windowMode)
    {
        if (self->m_windowContext)
            XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
    }
    else if (self->m_surface)
        XPlatformOffscreenSurface_doneCurrent(self->m_surface);
}

static void xgpu_context_destroy(XGpuRenderBackend* self)
{
    if (!self) return;
    if (self->m_windowMode)
    {
        if (self->m_windowContext)
            XPlatformOpenGLContext_destroy(self->m_windowContext);
        self->m_windowContext = NULL;
    }
    else
    {
        if (self->m_surface)
            XPlatformOffscreenSurface_destroy(self->m_surface);
        self->m_surface = NULL;
    }
}

/* ==================== 全局会话管理（阶段 2 窗口直通） ==================== */

static XGpuRenderBackend* g_xgpuWindowSession = NULL;   /**< 当前窗口直通会话（拥有）。 */
static XGpuRenderBackend* g_xgpuActiveSession = NULL;   /**< 当前活动会话（借用）。 */
static bool g_xgpuFrameDegraded = false;                /**< 本帧是否软件降级。 */
static bool g_xgpuLastFramePresented = false;           /**< 最近一帧是否 GPU present 上屏。 */
static bool g_xgpuWindowProbeFailed = false;            /**< 窗口 GL 上下文创建失败缓存。 */
static bool g_xgpuWindowAtExitRegistered = false;
static int g_xgpuRequested = -1;                        /**< -1 未探测；0/1 缓存。 */

static bool xgpu_text_equals(const char* value, const char* expected)
{
    unsigned char a;
    unsigned char b;
    if (!value || !expected) return false;
    while (*value && *expected)
    {
        a = (unsigned char)*value++;
        b = (unsigned char)*expected++;
        if (a >= (unsigned char)'A' && a <= (unsigned char)'Z')
            a = (unsigned char)(a + ('a' - 'A'));
        if (b >= (unsigned char)'A' && b <= (unsigned char)'Z')
            b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return *value == '\0' && *expected == '\0';
}

bool XGpuRenderBackend_requested(void)
{
    const char* value;
    if (g_xgpuRequested >= 0) return g_xgpuRequested != 0;
    value = getenv("XGUI_RENDER_BACKEND");
    if (!value || !*value) value = getenv("XGPU_BACKEND");
    g_xgpuRequested =
        xgpu_text_equals(value, "gpu") ||
        xgpu_text_equals(value, "opengl") ||
        xgpu_text_equals(value, "1") ||
        xgpu_text_equals(value, "true") ||
        xgpu_text_equals(value, "on") ? 1 : 0;
    return g_xgpuRequested != 0;
}

XGpuRenderBackend* XGpuRenderBackend_current(void)
{
    return g_xgpuActiveSession;
}

void XGpuRenderBackend_setFrameDegraded(bool degraded)
{
    g_xgpuFrameDegraded = degraded;
}

bool XGpuRenderBackend_frameDegraded(void)
{
    return g_xgpuFrameDegraded;
}

/** @brief 记录最近一帧上屏方式（true=GPU present；false=BitBlt/软件）。 */
void XGpuRenderBackend_setFramePresented(bool presented)
{
    g_xgpuLastFramePresented = presented;
}

/** @brief 最近一帧是否 GPU present 上屏（供截图/调试选择内容来源）。 */
bool XGpuRenderBackend_framePresented(void)
{
    return g_xgpuLastFramePresented;
}

static void xgpu_window_session_destroy_at_exit(void)
{
    XGpuRenderBackend_shutdown();
}

XGpuRenderBackend* XGpuRenderBackend_acquireForWindow(XWindow* window,
                                                      int width, int height)
{
    if (!window || width <= 0 || height <= 0) return NULL;
    if (g_xgpuWindowProbeFailed) return NULL;
    if (g_xgpuWindowSession &&
        (XGpuRenderBackend_width(g_xgpuWindowSession) != width ||
         XGpuRenderBackend_height(g_xgpuWindowSession) != height))
    {
        XGpuRenderBackend_destroy(g_xgpuWindowSession);
        g_xgpuWindowSession = NULL;
        g_xgpuActiveSession = NULL;
    }
    if (!g_xgpuWindowSession)
    {
        g_xgpuWindowSession =
            XGpuRenderBackend_createForWindow(window, width, height);
        if (!g_xgpuWindowSession)
        {
            g_xgpuActiveSession = NULL;
            g_xgpuWindowProbeFailed = true; /* 窗口 GL 不可用：保持软件/阶段 1。 */
            return NULL;
        }
        if (!g_xgpuWindowAtExitRegistered)
        {
            if (atexit(xgpu_window_session_destroy_at_exit) == 0)
                g_xgpuWindowAtExitRegistered = true;
        }
    }
    g_xgpuActiveSession = g_xgpuWindowSession;
    g_xgpuFrameDegraded = false;
    return g_xgpuWindowSession;
}

void XGpuRenderBackend_endWindowFrame(void)
{
    g_xgpuActiveSession = NULL;
    g_xgpuFrameDegraded = false;
}

void XGpuRenderBackend_shutdown(void)
{
    if (g_xgpuWindowSession)
    {
        XGpuRenderBackend_destroy(g_xgpuWindowSession);
        g_xgpuWindowSession = NULL;
    }
    g_xgpuActiveSession = NULL;
    g_xgpuFrameDegraded = false;
    g_xgpuWindowProbeFailed = false;
}

static bool xgpu_load_proc(XGpuRenderBackend* self, const char* name,
                           void* destination, size_t destinationSize)
{
    void* procedure;
    if (!destination || destinationSize != sizeof(procedure)) return false;
    procedure = xgpu_proc(self, name);
    if (!procedure) return false;
    memcpy(destination, &procedure, sizeof(procedure));
    return true;
}

static bool xgpu_load_proc_alias(XGpuRenderBackend* self, const char* name,
                                 const char* alias, void* destination,
                                 size_t destinationSize)
{
    void* procedure = xgpu_proc(self, name);
    if (!procedure && alias) procedure = xgpu_proc(self, alias);
    if (!procedure || !destination || destinationSize != sizeof(procedure))
        return false;
    memcpy(destination, &procedure, sizeof(procedure));
    return true;
}

#define XGPU_LOAD(member) \
    do { if (!xgpu_load_proc(self, #member, &self->member, \
                             sizeof(self->member))) goto failed; } while (0)

/* ==================== 资源辅助 ==================== */

static void xgpu_set_blend(XGpuRenderBackend* self, bool sourceOver);
static bool xgpu_draw_quad_uv(XGpuRenderBackend* self, XglUInt program,
                              XglUInt texture, float x, float y, float width,
                              float height, float u0, float v0, float u1,
                              float v1, const float* color, bool textured);

static bool xgpu_reserve_pixels(XGpuRenderBackend* self, size_t bytes)
{
    uint8_t* replacement;
    if (!self || bytes == 0) return false;
    if (self->m_pixels && self->m_pixelsCapacity >= bytes) return true;
    replacement = (uint8_t*)XRealloc_System(self->m_pixels, bytes);
    if (!replacement) return false;
    self->m_pixels = replacement;
    self->m_pixelsCapacity = bytes;
    return true;
}

static uint8_t xgpu_mul255(unsigned a, unsigned b)
{
    return (uint8_t)((a * b + 127u) / 255u);
}

static uint8_t xgpu_unpremultiply(uint8_t value, uint8_t alpha)
{
    unsigned result;
    if (alpha == 0u) return 0u;
    result = ((unsigned)value * 255u + alpha / 2u) / alpha;
    return (uint8_t)(result > 255u ? 255u : result);
}

static bool xgpu_upload_image(XGpuRenderBackend* self, const XImage* image,
                              XglUInt texture, int width, int height)
{
    size_t bytes;
    int y;
    if (!self || !image || texture == 0 || width <= 0 || height <= 0 ||
        XImage_width(image) < width || XImage_height(image) < height)
        return false;
    bytes = (size_t)width * (size_t)height * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    /* 快速路径：ARGB32 与 ARGB32_Premultiplied 是同一预乘布局。源已预乘，
       逐行直拷并做 R/B 交换（ARGB32 小端 B,G,R,A → GL 上传字节序
       R,G,B,A），避免逐像素 XImage_pixel/mul255 的函数调用开销
       （520x360 静态场景每帧约 19 万像素）。 */
    if (XImage_format(image) == XImageFormat_ARGB32 ||
        XImage_format(image) == XImageFormat_ARGB32_Premultiplied)
    {
        const uint8_t* src = XImage_constBits(image);
        int bpl = XImage_bytesPerLine(image);
        if (src && bpl >= width * 4)
        {
            for (y = 0; y < height; ++y)
            {
                const uint8_t* srow = src + (size_t)y * (size_t)bpl;
                uint8_t* drow = self->m_pixels + (size_t)y * (size_t)width * 4u;
                int x;
                for (x = 0; x < width; ++x)
                {
                    drow[x * 4 + 0] = srow[x * 4 + 2]; /* R <- B */
                    drow[x * 4 + 1] = srow[x * 4 + 1]; /* G */
                    drow[x * 4 + 2] = srow[x * 4 + 0]; /* B <- R */
                    drow[x * 4 + 3] = srow[x * 4 + 3]; /* A */
                }
            }
            self->glBindTexture(XGL_TEXTURE_2D, texture);
            self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
            self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA,
                               width, height, 0, XGL_RGBA, XGL_UNSIGNED_BYTE,
                               self->m_pixels);
            return true;
        }
    }
    for (y = 0; y < height; ++y)
    {
        int x;
        uint8_t* row = self->m_pixels + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; ++x)
        {
            uint32_t argb = XImage_pixel(image, x, y);
            uint8_t a = (uint8_t)(argb >> 24);
            row[x * 4] = xgpu_mul255((unsigned)((argb >> 16) & 0xffu), a);
            row[x * 4 + 1] = xgpu_mul255((unsigned)((argb >> 8) & 0xffu), a);
            row[x * 4 + 2] = xgpu_mul255((unsigned)(argb & 0xffu), a);
            row[x * 4 + 3] = a;
        }
    }
    self->glBindTexture(XGL_TEXTURE_2D, texture);
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
    /* The source texture can be smaller than the render target.  Re-specify
       its storage so UV [0,1] covers exactly the uploaded image rather than
       only a small corner of the session-sized texture. */
    self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA, width, height, 0,
                       XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
    return true;
}

static bool xgpu_upload_alpha(XGpuRenderBackend* self, const uint8_t* alpha,
                              int width, int height, int stride,
                              uint32_t color, float opacity)
{
    size_t bytes;
    int y;
    unsigned colorA;
    if (!self || !alpha || width <= 0 || height <= 0 || stride < width)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    colorA = (unsigned)((color >> 24) & 0xffu);
    colorA = (unsigned)(colorA * (unsigned)(opacity * 255.0f + 0.5f) +
                        127u) / 255u;
    bytes = (size_t)width * (size_t)height * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    for (y = 0; y < height; ++y)
    {
        int x;
        const uint8_t* source = alpha + (size_t)y * (size_t)stride;
        uint8_t* row = self->m_pixels + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; ++x)
        {
            unsigned coverage = source[x];
            unsigned a = (coverage * colorA + 127u) / 255u;
            row[x * 4] = xgpu_mul255((unsigned)((color >> 16) & 0xffu),
                                     (uint8_t)a);
            row[x * 4 + 1] = xgpu_mul255((unsigned)((color >> 8) & 0xffu),
                                         (uint8_t)a);
            row[x * 4 + 2] = xgpu_mul255((unsigned)(color & 0xffu),
                                         (uint8_t)a);
            row[x * 4 + 3] = (uint8_t)a;
        }
    }
    self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
    self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA, width, height, 0,
                       XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
    return true;
}

/* ==================== 字形图集（阶段 3） ==================== */

/**
 * @brief      清空图集条目与装箱游标（纹理内容无需清理，随后按需覆盖）。
 * @param      resetCounters true 同时清零命中/上传统计（公共 reset 用）。
 */
static void xgpu_glyph_atlas_reset(XGpuRenderBackend* self, bool resetCounters)
{
    if (!self) return;
    self->m_glyphEntryCount = 0;
    self->m_glyphCursorX = 0;
    self->m_glyphCursorY = 0;
    self->m_glyphRowHeight = 0;
    if (resetCounters)
    {
        self->m_glyphUploads = 0;
        self->m_glyphHits = 0;
    }
}

/**
 * @brief      在图集中为 w×h 的字形分配一个位置（行式 shelf 装箱）。
 * @details    当前行放不下换行；图集纵向放不下或条目数超上限时整体
 *             重置（旧条目全部失效，重置后仍放不下说明字形大于图集，
 *             返回 false 由调用方回退逐字形上传路径）。
 */
static bool xgpu_glyph_atlas_alloc(XGpuRenderBackend* self, int width,
                                   int height, int* outX, int* outY)
{
    if (width <= 0 || height <= 0 || width > XGPU_GLYPH_ATLAS_SIZE ||
        height > XGPU_GLYPH_ATLAS_SIZE)
        return false;
    if (self->m_glyphCursorX + width > XGPU_GLYPH_ATLAS_SIZE)
    {
        self->m_glyphCursorX = 0;
        self->m_glyphCursorY += self->m_glyphRowHeight;
        self->m_glyphRowHeight = 0;
    }
    if (self->m_glyphCursorY + height > XGPU_GLYPH_ATLAS_SIZE ||
        self->m_glyphEntryCount >= XGPU_GLYPH_ATLAS_MAX_ENTRIES)
        xgpu_glyph_atlas_reset(self, false);
    if (self->m_glyphCursorX + width > XGPU_GLYPH_ATLAS_SIZE)
    {
        self->m_glyphCursorX = 0;
        self->m_glyphCursorY += self->m_glyphRowHeight;
        self->m_glyphRowHeight = 0;
    }
    if (self->m_glyphCursorY + height > XGPU_GLYPH_ATLAS_SIZE)
        return false;
    *outX = self->m_glyphCursorX;
    *outY = self->m_glyphCursorY;
    self->m_glyphCursorX += width;
    if (height > self->m_glyphRowHeight)
        self->m_glyphRowHeight = height;
    return true;
}

/**
 * @brief      在图集条目数组中查找键+尺寸完全匹配的条目。
 * @details    线性扫描：常规一帧的字形数远小于条目上限，比较成本相对
 *             每字形一次光栅化+上传可忽略；若后续 profiler 显示热点，
 *             再引入键哈希桶。
 */
static const XGpuGlyphAtlasEntry* xgpu_glyph_atlas_find(
    const XGpuRenderBackend* self, uint64_t key, int width, int height)
{
    int i;
    for (i = 0; i < self->m_glyphEntryCount; ++i)
    {
        const XGpuGlyphAtlasEntry* entry = &self->m_glyphEntries[i];
        if (entry->m_key == key && entry->m_width == width &&
            entry->m_height == height)
            return entry;
    }
    return NULL;
}

/**
 * @brief      把覆盖图以"RGBA 四通道均为覆盖度"的形式写入上传暂存区。
 * @details    采样结果 vec4(c,c,c,c) 与预乘颜色 modulate 相乘即得正确的
 *             预乘输出（rgb = a × 颜色），因此同一份覆盖度缓存可服务
 *             任意颜色与透明度。
 */
static bool xgpu_glyph_atlas_expand_coverage(XGpuRenderBackend* self,
                                             const uint8_t* alpha, int width,
                                             int height, int stride)
{
    size_t bytes = (size_t)width * (size_t)height * 4u;
    int y;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    for (y = 0; y < height; ++y)
    {
        int x;
        const uint8_t* source = alpha + (size_t)y * (size_t)stride;
        uint8_t* row = self->m_pixels + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; ++x)
        {
            uint8_t coverage = source[x];
            row[x * 4] = coverage;
            row[x * 4 + 1] = coverage;
            row[x * 4 + 2] = coverage;
            row[x * 4 + 3] = coverage;
        }
    }
    return true;
}

bool XGpuRenderBackend_drawGlyphAlpha(XGpuRenderBackend* self,
                                      uint64_t key, int width, int height,
                                      const uint8_t* alpha, int stride,
                                      int x, int y, uint32_t color,
                                      float opacity, bool sourceOver)
{
    const XGpuGlyphAtlasEntry* entry;
    XGpuGlyphAtlasEntry* stored;
    float modulate[4];
    unsigned colorA;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (!XGpuRenderBackend_isValid(self) || width <= 0 ||
        height <= 0 || stride < width || (!alpha && !xgpu_glyph_atlas_find(
            self, key, width, height)))
        return false;
    entry = xgpu_glyph_atlas_find(self, key, width, height);
    if (!entry)
    {
        int atlasX = 0;
        int atlasY = 0;
        /* 超出图集能力的字形（大于图集或分配失败）回退逐字形上传路径。 */
        if (!xgpu_glyph_atlas_alloc(self, width, height, &atlasX, &atlasY))
        {
            return XGpuRenderBackend_drawAlphaBitmap(
                self, alpha, width, height, stride, x, y, color, opacity,
                sourceOver);
        }
        if (!xgpu_glyph_atlas_expand_coverage(self, alpha, width, height,
                                              stride))
        {
            return false;
        }
        self->glBindTexture(XGL_TEXTURE_2D, self->m_glyphAtlasTexture);
        self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
        self->glTexSubImage2D(XGL_TEXTURE_2D, 0, atlasX, atlasY, width,
                              height, XGL_RGBA, XGL_UNSIGNED_BYTE,
                              self->m_pixels);
        if (self->m_glyphEntryCount >= self->m_glyphEntryCapacity)
        {
            int newCapacity = self->m_glyphEntryCapacity > 0
                                  ? self->m_glyphEntryCapacity * 2
                                  : 256;
            XGpuGlyphAtlasEntry* entries;
            if (newCapacity > XGPU_GLYPH_ATLAS_MAX_ENTRIES)
                newCapacity = XGPU_GLYPH_ATLAS_MAX_ENTRIES;
            entries = (XGpuGlyphAtlasEntry*)XRealloc_System(
                self->m_glyphEntries,
                (size_t)newCapacity * sizeof(*entries));
            if (!entries) return false;
            self->m_glyphEntries = entries;
            self->m_glyphEntryCapacity = newCapacity;
        }
        stored = &self->m_glyphEntries[self->m_glyphEntryCount++];
        stored->m_key = key;
        stored->m_width = width;
        stored->m_height = height;
        stored->m_x = atlasX;
        stored->m_y = atlasY;
        entry = stored;
        ++self->m_glyphUploads;
    }
    else
        ++self->m_glyphHits;
    /* 预乘颜色（透明度折入 alpha）：采样 vec4(c,c,c,c) × modulate 即
       得 rgb = a × 颜色 的预乘输出，与既有上传路径逐像素等价。 */
    colorA = (unsigned)((color >> 24) & 0xffu);
    colorA = (unsigned)(colorA * (unsigned)(opacity * 255.0f + 0.5f) +
                        127u) / 255u;
    modulate[0] = (float)xgpu_mul255((unsigned)((color >> 16) & 0xffu),
                                     (uint8_t)colorA) / 255.0f;
    modulate[1] = (float)xgpu_mul255((unsigned)((color >> 8) & 0xffu),
                                     (uint8_t)colorA) / 255.0f;
    modulate[2] = (float)xgpu_mul255((unsigned)(color & 0xffu),
                                     (uint8_t)colorA) / 255.0f;
    modulate[3] = (float)colorA / 255.0f;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad_uv(
        self, self->m_textureProgram, self->m_glyphAtlasTexture,
        (float)x, (float)y, (float)width, (float)height,
        (float)entry->m_x / (float)XGPU_GLYPH_ATLAS_SIZE,
        (float)entry->m_y / (float)XGPU_GLYPH_ATLAS_SIZE,
        (float)(entry->m_x + width) / (float)XGPU_GLYPH_ATLAS_SIZE,
        (float)(entry->m_y + height) / (float)XGPU_GLYPH_ATLAS_SIZE,
        modulate, true);
}

bool XGpuRenderBackend_glyphAtlasContains(const XGpuRenderBackend* self,
                                          uint64_t key, int width, int height)
{
    if (!XGpuRenderBackend_isValid(self)) return false;
    return xgpu_glyph_atlas_find(self, key, width, height) != NULL;
}

void XGpuRenderBackend_resetGlyphAtlas(XGpuRenderBackend* self)
{
    if (!self) return;
    xgpu_glyph_atlas_reset(self, true);
}

unsigned XGpuRenderBackend_glyphAtlasUploadCount(const XGpuRenderBackend* self)
{
    return self ? self->m_glyphUploads : 0u;
}

unsigned XGpuRenderBackend_glyphAtlasHitCount(const XGpuRenderBackend* self)
{
    return self ? self->m_glyphHits : 0u;
}

static XglUInt xgpu_compile_shader(XGpuRenderBackend* self, XglEnum type,
                                   const char* source)
{
    XglUInt shader;
    XglInt status = 0;
    const XglChar* sources[1];
    if (!self || !source) return 0;
    shader = self->glCreateShader(type);
    if (!shader) return 0;
    sources[0] = source;
    self->glShaderSource(shader, 1, sources, NULL);
    self->glCompileShader(shader);
    self->glGetShaderiv(shader, XGL_COMPILE_STATUS, &status);
    if (!status)
    {
        self->glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static XglUInt xgpu_create_program(XGpuRenderBackend* self,
                                   const char* fragmentSource)
{
    static const char vertexSource[] =
        "attribute vec2 a_position;"
        "attribute vec2 a_texcoord;"
        "varying vec2 v_texcoord;"
        "void main(){gl_Position=vec4(a_position,0.0,1.0);"
        "v_texcoord=a_texcoord;}";
    XglUInt vertex;
    XglUInt fragment;
    XglUInt program;
    XglInt status = 0;
    if (!self || !fragmentSource) return 0;
    vertex = xgpu_compile_shader(self, XGL_VERTEX_SHADER, vertexSource);
    fragment = xgpu_compile_shader(self, XGL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment)
    {
        if (vertex) self->glDeleteShader(vertex);
        if (fragment) self->glDeleteShader(fragment);
        return 0;
    }
    program = self->glCreateProgram();
    if (!program)
    {
        self->glDeleteShader(vertex);
        self->glDeleteShader(fragment);
        return 0;
    }
    self->glAttachShader(program, vertex);
    self->glAttachShader(program, fragment);
    self->glBindAttribLocation(program, 0, "a_position");
    self->glBindAttribLocation(program, 1, "a_texcoord");
    self->glLinkProgram(program);
    self->glGetProgramiv(program, XGL_LINK_STATUS, &status);
    self->glDeleteShader(vertex);
    self->glDeleteShader(fragment);
    if (!status)
    {
        self->glDeleteProgram(program);
        return 0;
    }
    return program;
}

static void xgpu_prepare_texture(XGpuRenderBackend* self, XglUInt texture,
                                 int width, int height)
{
    self->glBindTexture(XGL_TEXTURE_2D, texture);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_MIN_FILTER, XGL_NEAREST);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_MAG_FILTER, XGL_NEAREST);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_WRAP_S, XGL_CLAMP_TO_EDGE);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_WRAP_T, XGL_CLAMP_TO_EDGE);
    self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA, width, height, 0,
                       XGL_RGBA, XGL_UNSIGNED_BYTE, NULL);
}

static void xgpu_set_blend(XGpuRenderBackend* self, bool sourceOver)
{
    if (!sourceOver)
    {
        self->glDisable(XGL_BLEND);
        return;
    }
    self->glEnable(XGL_BLEND);
    /* 所有上传的纹理和纯色均为预乘 RGBA。 */
    self->glBlendFunc(XGL_ONE, XGL_ONE_MINUS_SRC_ALPHA);
}

/**
 * @brief      计算矩形顶点与 UV（全纹理或子矩形）。
 * @details    顶点排布与既有 quad 一致（三角带 TL/TR/BL/BR）；UV 以图集
 *             纹理的纹素坐标换算，纹理行 0 位于 v=0（与 glTexSubImage2D
 *             的行序一致），因此条目上边缘用 v0、下边缘用 v1。
 */
static void xgpu_rect_vertices_uv(const XGpuRenderBackend* self, float x,
                                  float y, float width, float height,
                                  float* vertices, bool textured, float u0,
                                  float v0, float u1, float v1)
{
    float left = x * 2.0f / (float)self->m_width - 1.0f;
    float right = (x + width) * 2.0f / (float)self->m_width - 1.0f;
    float top = 1.0f - y * 2.0f / (float)self->m_height;
    float bottom = 1.0f - (y + height) * 2.0f / (float)self->m_height;
    float rightUv = textured ? u1 : 0.0f;
    float bottomUv = textured ? v1 : 0.0f;
    vertices[0] = left; vertices[1] = top; vertices[2] = u0; vertices[3] = v0;
    vertices[4] = right; vertices[5] = top; vertices[6] = rightUv; vertices[7] = v0;
    vertices[8] = left; vertices[9] = bottom; vertices[10] = u0; vertices[11] = bottomUv;
    vertices[12] = right; vertices[13] = bottom; vertices[14] = rightUv; vertices[15] = bottomUv;
}

static void xgpu_rect_vertices(const XGpuRenderBackend* self, float x, float y,
                               float width, float height, float* vertices,
                               bool textured)
{
    xgpu_rect_vertices_uv(self, x, y, width, height, vertices, textured,
                          0.0f, 0.0f, 1.0f, 1.0f);
}

static bool xgpu_draw_quad(XGpuRenderBackend* self, XglUInt program,
                           XglUInt texture, float x, float y, float width,
                           float height, const float* color, bool textured);

static bool xgpu_draw_quad_uv(XGpuRenderBackend* self, XglUInt program,
                              XglUInt texture, float x, float y, float width,
                              float height, float u0, float v0, float u1,
                              float v1, const float* color, bool textured)
{
    float vertices[16];
    if (!self || !program || width <= 0.0f || height <= 0.0f) return false;
    xgpu_rect_vertices_uv(self, x, y, width, height, vertices, textured,
                          u0, v0, u1, v1);
    self->glUseProgram(program);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    self->glBufferData(XGL_ARRAY_BUFFER, (XglSizeiptr)sizeof(vertices),
                       vertices, XGL_DYNAMIC_DRAW);
    self->glEnableVertexAttribArray(0);
    self->glEnableVertexAttribArray(1);
    self->glVertexAttribPointer(0, 2, XGL_FLOAT, XGL_FALSE,
                                (XglSizei)(sizeof(float) * 4u), (const void*)0);
    self->glVertexAttribPointer(1, 2, XGL_FLOAT, XGL_FALSE,
                                (XglSizei)(sizeof(float) * 4u),
                                (const void*)(sizeof(float) * 2u));
    if (textured)
    {
        self->glActiveTexture(XGL_TEXTURE0);
        self->glBindTexture(XGL_TEXTURE_2D, texture);
        self->glUniform1i(self->m_textureSamplerLocation, 0);
        self->glUniform4f(self->m_textureModulateLocation,
                          color[0], color[1], color[2], color[3]);
    }
    else
        self->glUniform4f(self->m_solidColorLocation,
                          color[0], color[1], color[2], color[3]);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
    self->glDisableVertexAttribArray(0);
    self->glDisableVertexAttribArray(1);
    return true;
}

static bool xgpu_draw_quad(XGpuRenderBackend* self, XglUInt program,
                           XglUInt texture, float x, float y, float width,
                           float height, const float* color, bool textured)
{
    return xgpu_draw_quad_uv(self, program, texture, x, y, width, height,
                             0.0f, 0.0f, 1.0f, 1.0f, color, textured);
}

/* ==================== 生命周期 ==================== */

/* ==================== GL 初始化（离屏 / 窗口直通共用） ==================== */

/**
 * @brief 在已 makeCurrent 的上下文上加载 GL 函数并建立 FBO/纹理/shader。
 * @return true 成功；false 失败（调用方负责 doneCurrent 与释放）。
 */
static bool xgpu_gl_initialize(XGpuRenderBackend* self, int width, int height)
{
    const char solidFragment[] =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform vec4 u_color;"
        "void main(){gl_FragColor=u_color;}";
    const char textureFragment[] =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform sampler2D u_texture;"
        "uniform vec4 u_modulate;"
        "varying vec2 v_texcoord;"
        "void main(){gl_FragColor=texture2D(u_texture,v_texcoord)*u_modulate;}";
    if (!self || width <= 0 || height <= 0) return false;

    XGPU_LOAD(glViewport); XGPU_LOAD(glClearColor); XGPU_LOAD(glClear);
    XGPU_LOAD(glEnable); XGPU_LOAD(glDisable); XGPU_LOAD(glBlendFunc);
    XGPU_LOAD(glScissor); XGPU_LOAD(glPixelStorei); XGPU_LOAD(glReadPixels);
    if (!xgpu_load_proc_alias(self, "glGenFramebuffers", "glGenFramebuffersOES",
                              &self->glGenFramebuffers, sizeof(self->glGenFramebuffers)) ||
        !xgpu_load_proc_alias(self, "glDeleteFramebuffers", "glDeleteFramebuffersOES",
                              &self->glDeleteFramebuffers, sizeof(self->glDeleteFramebuffers)) ||
        !xgpu_load_proc_alias(self, "glBindFramebuffer", "glBindFramebufferOES",
                              &self->glBindFramebuffer, sizeof(self->glBindFramebuffer)) ||
        !xgpu_load_proc_alias(self, "glFramebufferTexture2D",
                              "glFramebufferTexture2DOES",
                              &self->glFramebufferTexture2D,
                              sizeof(self->glFramebufferTexture2D)) ||
        !xgpu_load_proc_alias(self, "glCheckFramebufferStatus",
                              "glCheckFramebufferStatusOES",
                              &self->glCheckFramebufferStatus,
                              sizeof(self->glCheckFramebufferStatus)))
        return false;
    XGPU_LOAD(glGenTextures); XGPU_LOAD(glDeleteTextures); XGPU_LOAD(glBindTexture);
    XGPU_LOAD(glTexParameteri); XGPU_LOAD(glTexImage2D); XGPU_LOAD(glTexSubImage2D);
    XGPU_LOAD(glActiveTexture); XGPU_LOAD(glGenBuffers); XGPU_LOAD(glDeleteBuffers);
    XGPU_LOAD(glBindBuffer); XGPU_LOAD(glBufferData);
    XGPU_LOAD(glEnableVertexAttribArray); XGPU_LOAD(glDisableVertexAttribArray);
    XGPU_LOAD(glVertexAttribPointer); XGPU_LOAD(glDrawArrays);
    XGPU_LOAD(glCreateShader); XGPU_LOAD(glShaderSource); XGPU_LOAD(glCompileShader);
    XGPU_LOAD(glGetShaderiv); XGPU_LOAD(glDeleteShader); XGPU_LOAD(glCreateProgram);
    XGPU_LOAD(glAttachShader); XGPU_LOAD(glBindAttribLocation); XGPU_LOAD(glLinkProgram);
    XGPU_LOAD(glGetProgramiv); XGPU_LOAD(glDeleteProgram); XGPU_LOAD(glUseProgram);
    XGPU_LOAD(glGetUniformLocation); XGPU_LOAD(glUniform1i); XGPU_LOAD(glUniform4f);
    XGPU_LOAD(glGetString);

    self->glGenFramebuffers(1, &self->m_framebuffer);
    self->glGenTextures(1, &self->m_colorTexture);
    self->glGenTextures(1, &self->m_sourceTexture);
    self->glGenBuffers(1, &self->m_vertexBuffer);
    if (!self->m_framebuffer || !self->m_colorTexture ||
        !self->m_sourceTexture || !self->m_vertexBuffer)
        return false;
    xgpu_prepare_texture(self, self->m_colorTexture, width, height);
    xgpu_prepare_texture(self, self->m_sourceTexture, width, height);
    self->glGenTextures(1, &self->m_glyphAtlasTexture);
    if (!self->m_glyphAtlasTexture) return false;
    xgpu_prepare_texture(self, self->m_glyphAtlasTexture,
                         XGPU_GLYPH_ATLAS_SIZE, XGPU_GLYPH_ATLAS_SIZE);
    /* 2b 快路径：glBlitFramebuffer（GL3/GLES3/扩展）可选；不可用时
       presentToWindow 回退全屏 quad 采样合成。 */
    self->m_hasBlit = xgpu_load_proc_alias(
        self, "glBlitFramebuffer", "glBlitFramebufferNV",
        &self->glBlitFramebuffer, sizeof(self->glBlitFramebuffer));
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glFramebufferTexture2D(XGL_FRAMEBUFFER, XGL_COLOR_ATTACHMENT0,
                                  XGL_TEXTURE_2D, self->m_colorTexture, 0);
    if (self->glCheckFramebufferStatus(XGL_FRAMEBUFFER) != XGL_FRAMEBUFFER_COMPLETE)
        return false;
    self->m_solidProgram = xgpu_create_program(self, solidFragment);
    self->m_textureProgram = xgpu_create_program(self, textureFragment);
    if (!self->m_solidProgram || !self->m_textureProgram) return false;
    self->m_solidColorLocation = self->glGetUniformLocation(
        self->m_solidProgram, "u_color");
    self->m_textureSamplerLocation = self->glGetUniformLocation(
        self->m_textureProgram, "u_texture");
    self->m_textureModulateLocation = self->glGetUniformLocation(
        self->m_textureProgram, "u_modulate");
    if (self->m_solidColorLocation < 0 || self->m_textureSamplerLocation < 0 ||
        self->m_textureModulateLocation < 0)
        return false;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, 0);
    return true;

failed:
    return false;
}

XGpuRenderBackend* XGpuRenderBackend_create(int width, int height)
{
    XGpuRenderBackend* self;
    if (width <= 0 || height <= 0) return NULL;
    self = (XGpuRenderBackend*)XCalloc_System(1u, sizeof(*self));
    if (!self) return NULL;
    self->m_width = width;
    self->m_height = height;
    self->m_surface = XPlatformOffscreenSurface_create((uint32_t)width,
                                                        (uint32_t)height);
    if (!self->m_surface || !XPlatformOffscreenSurface_makeCurrent(self->m_surface))
        goto failed;
    if (!xgpu_gl_initialize(self, width, height)) goto failed;
    self->m_valid = true;
    XPlatformOffscreenSurface_doneCurrent(self->m_surface);
    return self;

failed:
    XGpuRenderBackend_destroy(self);
    return NULL;
}

XGpuRenderBackend* XGpuRenderBackend_createForWindow(XWindow* window,
                                                     int width, int height)
{
    XGpuRenderBackend* self;
    if (!window || width <= 0 || height <= 0) return NULL;
    self = (XGpuRenderBackend*)XCalloc_System(1u, sizeof(*self));
    if (!self) return NULL;
    self->m_width = width;
    self->m_height = height;
    self->m_windowMode = true;
    self->m_windowContext = XPlatformOpenGLContext_create(window);
    if (!self->m_windowContext ||
        !XPlatformOpenGLContext_makeCurrent(self->m_windowContext))
        goto failed;
    if (!xgpu_gl_initialize(self, width, height)) goto failed;
    self->m_valid = true;
    self->m_firstFrame = true;
    XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
    return self;

failed:
    XGpuRenderBackend_destroy(self);
    return NULL;
}

bool XGpuRenderBackend_isWindowMode(const XGpuRenderBackend* self)
{
    return self && self->m_windowMode;
}

void XGpuRenderBackend_destroy(XGpuRenderBackend* self)
{
    if (!self) return;
    if (self->m_valid && xgpu_make_current(self))
    {
        if (self->glDeleteProgram && self->m_solidProgram)
            self->glDeleteProgram(self->m_solidProgram);
        if (self->glDeleteProgram && self->m_textureProgram)
            self->glDeleteProgram(self->m_textureProgram);
        if (self->glDeleteBuffers && self->m_vertexBuffer)
            self->glDeleteBuffers(1, &self->m_vertexBuffer);
        if (self->glDeleteTextures && self->m_sourceTexture)
            self->glDeleteTextures(1, &self->m_sourceTexture);
        if (self->glDeleteTextures && self->m_colorTexture)
            self->glDeleteTextures(1, &self->m_colorTexture);
        if (self->glDeleteTextures && self->m_glyphAtlasTexture)
            self->glDeleteTextures(1, &self->m_glyphAtlasTexture);
        if (self->glDeleteFramebuffers && self->m_framebuffer)
            self->glDeleteFramebuffers(1, &self->m_framebuffer);
        xgpu_done_current(self);
    }
    xgpu_context_destroy(self);
    if (self->m_pixels) XFree_System(self->m_pixels);
    if (self->m_glyphEntries) XFree_System(self->m_glyphEntries);
    XFree_System(self);
}

bool XGpuRenderBackend_isValid(const XGpuRenderBackend* self)
{
    return self && self->m_valid &&
           (self->m_windowMode ? self->m_windowContext != NULL
                               : self->m_surface != NULL);
}

int XGpuRenderBackend_width(const XGpuRenderBackend* self)
{ return XGpuRenderBackend_isValid(self) ? self->m_width : 0; }

int XGpuRenderBackend_height(const XGpuRenderBackend* self)
{ return XGpuRenderBackend_isValid(self) ? self->m_height : 0; }

/* ==================== 帧控制与原语 ==================== */

bool XGpuRenderBackend_beginFrameImage(XGpuRenderBackend* self,
                                       const XImage* initialImage)
{
    if (!XGpuRenderBackend_isValid(self) || !xgpu_make_current(self))
        return false;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glViewport(0, 0, self->m_width, self->m_height);
    self->glPixelStorei(XGL_PACK_ALIGNMENT, 1);
    self->glDisable(XGL_SCISSOR_TEST);
    xgpu_set_blend(self, true);
    if (self->m_windowMode)
    {
        /* 窗口直通：FBO 是持久缓冲（不清除、不重传旧 XImage），脏区绘制
           叠加在上一帧内容上；仅会话首帧需要以 XImage（若同尺寸）或
           透明初始化画布。 */
        if (self->m_firstFrame)
        {
            if (initialImage && XImage_width(initialImage) == self->m_width &&
                XImage_height(initialImage) == self->m_height)
            {
                if (!xgpu_upload_image(self, initialImage,
                                       self->m_colorTexture,
                                       self->m_width, self->m_height))
                {
                    xgpu_done_current(self);
                    return false;
                }
            }
            else
                XGpuRenderBackend_clear(self, 0u);
            self->m_firstFrame = false;
        }
        return true;
    }
    if (initialImage && XImage_width(initialImage) == self->m_width &&
        XImage_height(initialImage) == self->m_height)
    {
        if (!xgpu_upload_image(self, initialImage, self->m_colorTexture,
                               self->m_width, self->m_height))
        {
            xgpu_done_current(self);
            return false;
        }
    }
    else
        XGpuRenderBackend_clear(self, 0u);
    return true;
}

bool XGpuRenderBackend_beginFrame(XGpuRenderBackend* self)
{ return XGpuRenderBackend_beginFrameImage(self, NULL); }

void XGpuRenderBackend_clear(XGpuRenderBackend* self, uint32_t argb)
{
    unsigned a;
    if (!XGpuRenderBackend_isValid(self)) return;
    a = (argb >> 24) & 0xffu;
    self->glClearColor((XglFloat)xgpu_mul255((argb >> 16) & 0xffu,
                                              (uint8_t)a) / 255.0f,
                       (XglFloat)xgpu_mul255((argb >> 8) & 0xffu,
                                              (uint8_t)a) / 255.0f,
                       (XglFloat)xgpu_mul255(argb & 0xffu, (uint8_t)a) / 255.0f,
                       (XglFloat)a / 255.0f);
    self->glClear(XGL_COLOR_BUFFER_BIT);
}

void XGpuRenderBackend_setClipRect(XGpuRenderBackend* self, const XRect* rect)
{
    int x0, y0, x1, y1;
    if (!XGpuRenderBackend_isValid(self)) return;
    if (!rect)
    {
        self->glDisable(XGL_SCISSOR_TEST);
        return;
    }
    x0 = rect->x < 0 ? 0 : rect->x;
    y0 = rect->y < 0 ? 0 : rect->y;
    x1 = rect->x > INT_MAX - rect->width ? INT_MAX : rect->x + rect->width;
    y1 = rect->y > INT_MAX - rect->height ? INT_MAX : rect->y + rect->height;
    if (x1 > self->m_width) x1 = self->m_width;
    if (y1 > self->m_height) y1 = self->m_height;
    if (x1 <= x0 || y1 <= y0)
    {
        self->glEnable(XGL_SCISSOR_TEST);
        self->glScissor(0, 0, 0, 0);
        return;
    }
    self->glEnable(XGL_SCISSOR_TEST);
    self->glScissor(x0, self->m_height - y1, x1 - x0, y1 - y0);
}

bool XGpuRenderBackend_fillRect(XGpuRenderBackend* self, const XRect* rect,
                                uint32_t color, float opacity, bool sourceOver)
{
    float rgba[4];
    unsigned a;
    if (!XGpuRenderBackend_isValid(self) || !rect || rect->width <= 0 ||
        rect->height <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    a = (unsigned)((color >> 24) & 0xffu);
    a = (unsigned)(a * (unsigned)(opacity * 255.0f + 0.5f) + 127u) / 255u;
    rgba[0] = (float)xgpu_mul255((color >> 16) & 0xffu, (uint8_t)a) / 255.0f;
    rgba[1] = (float)xgpu_mul255((color >> 8) & 0xffu, (uint8_t)a) / 255.0f;
    rgba[2] = (float)xgpu_mul255(color & 0xffu, (uint8_t)a) / 255.0f;
    rgba[3] = (float)a / 255.0f;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad(self, self->m_solidProgram, 0, (float)rect->x,
                          (float)rect->y, (float)rect->width,
                          (float)rect->height, rgba, false);
}

bool XGpuRenderBackend_drawImage(XGpuRenderBackend* self, const XImage* image,
                                 int x, int y, int width, int height,
                                 float opacity, bool sourceOver)
{
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (!XGpuRenderBackend_isValid(self) || !image || width <= 0 || height <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (XImage_width(image) != width || XImage_height(image) != height)
        return false;
    if (!xgpu_upload_image(self, image, self->m_sourceTexture, width, height))
        return false;
    /* The source texture is premultiplied, so opacity scales RGB and alpha
       together before the premultiplied blend. */
    modulate[0] = opacity;
    modulate[1] = opacity;
    modulate[2] = opacity;
    modulate[3] = opacity;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad(self, self->m_textureProgram, self->m_sourceTexture,
                          (float)x, (float)y, (float)width, (float)height,
                          modulate, true);
}

bool XGpuRenderBackend_drawAlphaBitmap(XGpuRenderBackend* self,
                                       const uint8_t* alpha, int width,
                                       int height, int stride, int x, int y,
                                       uint32_t color, float opacity,
                                       bool sourceOver)
{
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (!XGpuRenderBackend_isValid(self) || !xgpu_upload_alpha(
            self, alpha, width, height, stride, color, opacity))
        return false;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad(self, self->m_textureProgram, self->m_sourceTexture,
                          (float)x, (float)y, (float)width, (float)height,
                          modulate, true);
}

bool XGpuRenderBackend_readback(XGpuRenderBackend* self, XImage* target)
{
    size_t bytes;
    int y;
    if (!XGpuRenderBackend_isValid(self) || !target ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glPixelStorei(XGL_PACK_ALIGNMENT, 1);
    self->glReadPixels(0, 0, self->m_width, self->m_height, XGL_RGBA,
                       XGL_UNSIGNED_BYTE, self->m_pixels);
    /* 快速路径：ARGB32 与 ARGB32_Premultiplied 在 XImage 中是同一预乘
       布局（XImageFormat_ARGB32 直接映射到 Premultiplied）。FBO 经预乘
       混合后的帧逐行直接写入目标像素缓冲（GL 原点左下 → XImage 左上
       翻转 + RGBA→ARGB32 小端的 R/B 交换），避免逐像素 XImage_setPixel
       的函数调用开销（520x360 每帧约 19 万像素）。 */
    if (XImage_format(target) == XImageFormat_ARGB32 ||
        XImage_format(target) == XImageFormat_ARGB32_Premultiplied)
    {
        uint8_t* dst = XImage_bits(target);
        int bpl = XImage_bytesPerLine(target);
        if (dst && bpl >= self->m_width * 4)
        {
            for (y = 0; y < self->m_height; ++y)
            {
                const uint8_t* row = self->m_pixels +
                    (size_t)(self->m_height - 1 - y) *
                    (size_t)self->m_width * 4u;
                uint8_t* line = dst + (size_t)y * (size_t)bpl;
                int x;
                for (x = 0; x < self->m_width; ++x)
                {
                    const uint8_t* p = row + (size_t)x * 4u;
                    line[x * 4 + 0] = p[2]; /* B */
                    line[x * 4 + 1] = p[1]; /* G */
                    line[x * 4 + 2] = p[0]; /* R */
                    line[x * 4 + 3] = p[3]; /* A（预乘帧直接入预乘图像） */
                }
            }
            return true;
        }
    }
    /* 慢路径：其它格式逐像素经 XImage_setPixel 转换。 */
    for (y = 0; y < self->m_height; ++y)
    {
        int x;
        const uint8_t* row = self->m_pixels +
            (size_t)(self->m_height - 1 - y) * (size_t)self->m_width * 4u;
        for (x = 0; x < self->m_width; ++x)
        {
            const uint8_t* pixel = row + (size_t)x * 4u;
            uint8_t a = pixel[3];
            uint32_t argb = ((uint32_t)a << 24) |
                ((uint32_t)xgpu_unpremultiply(pixel[0], a) << 16) |
                ((uint32_t)xgpu_unpremultiply(pixel[1], a) << 8) |
                (uint32_t)xgpu_unpremultiply(pixel[2], a);
            XImage_setPixel(target, x, y, argb);
        }
    }
    return true;
}

void XGpuRenderBackend_endFrame(XGpuRenderBackend* self)
{
    if (!XGpuRenderBackend_isValid(self)) return;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, 0);
    xgpu_done_current(self);
}

/* ==================== 窗口直通上屏（阶段 2） ==================== */

bool XGpuRenderBackend_presentToWindow(XGpuRenderBackend* self)
{
    float vertices[16];
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static unsigned profCount;
    static double profQuadUs, profSwapUs;
    int64_t profT0 = 0, profT1 = 0, profT2 = 0;
    static int profOn = -1;
    if (profOn < 0)
    {
        const char* env = getenv("XGPU_PROFILE");
        profOn = env && *env ? 1 : 0;
    }
    if (!XGpuRenderBackend_isValid(self) || !self->m_windowMode ||
        !self->m_windowContext)
        return false;
    if (!XPlatformOpenGLContext_makeCurrent(self->m_windowContext))
        return false;
    if (profOn) profT0 = XDateTime_currentNSecsSinceEpoch() / 1000;
    /* 把 FBO 颜色纹理合成到窗口默认帧缓冲。2b 快路径：同尺寸 1:1 时
       用 glBlitFramebuffer（固定功能拷贝，无采样/无 shader）；blit
       不可用（GLES2 等）回退全屏 quad 采样（NDC 直接映射：FBO 与窗口
       默认帧缓冲同为 GL 左下原点，uv 不翻转）。 */
    self->glDisable(XGL_BLEND);
    self->glDisable(XGL_SCISSOR_TEST);
    if (self->m_hasBlit)
    {
        /* READ=FBO（持久画面），DRAW=默认帧缓冲；blit 后恢复原绑定。 */
        self->glBindFramebuffer(XGL_READ_FRAMEBUFFER, self->m_framebuffer);
        self->glBindFramebuffer(XGL_DRAW_FRAMEBUFFER, 0);
        self->glViewport(0, 0, self->m_width, self->m_height);
        self->glBlitFramebuffer(0, 0, self->m_width, self->m_height,
                                0, 0, self->m_width, self->m_height,
                                XGL_COLOR_BUFFER_BIT, XGL_NEAREST);
        self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
        self->glEnable(XGL_BLEND);
        if (!XPlatformOpenGLContext_swapBuffers(self->m_windowContext))
        {
            XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
            return false;
        }
        XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
        g_xgpuLastFramePresented = true;
        return true;
    }
    vertices[0]  = -1.0f; vertices[1]  =  1.0f; vertices[2]  = 0.0f; vertices[3]  = 1.0f;
    vertices[4]  =  1.0f; vertices[5]  =  1.0f; vertices[6]  = 1.0f; vertices[7]  = 1.0f;
    vertices[8]  = -1.0f; vertices[9]  = -1.0f; vertices[10] = 0.0f; vertices[11] = 0.0f;
    vertices[12] =  1.0f; vertices[13] = -1.0f; vertices[14] = 1.0f; vertices[15] = 0.0f;
    self->glUseProgram(self->m_textureProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    self->glBufferData(XGL_ARRAY_BUFFER, (XglSizeiptr)sizeof(vertices),
                       vertices, XGL_DYNAMIC_DRAW);
    self->glEnableVertexAttribArray(0);
    self->glEnableVertexAttribArray(1);
    self->glVertexAttribPointer(0, 2, XGL_FLOAT, XGL_FALSE,
                                (XglSizei)(sizeof(float) * 4u), (const void*)0);
    self->glVertexAttribPointer(1, 2, XGL_FLOAT, XGL_FALSE,
                                (XglSizei)(sizeof(float) * 4u),
                                (const void*)(sizeof(float) * 2u));
    self->glActiveTexture(XGL_TEXTURE0);
    self->glBindTexture(XGL_TEXTURE_2D, self->m_colorTexture);
    self->glUniform1i(self->m_textureSamplerLocation, 0);
    self->glUniform4f(self->m_textureModulateLocation,
                      modulate[0], modulate[1], modulate[2], modulate[3]);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
    self->glDisableVertexAttribArray(0);
    self->glDisableVertexAttribArray(1);
    if (profOn) profT1 = XDateTime_currentNSecsSinceEpoch() / 1000;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glEnable(XGL_BLEND);
    if (!XPlatformOpenGLContext_swapBuffers(self->m_windowContext))
    {
        XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
        return false;
    }
    if (profOn)
    {
        profT2 = XDateTime_currentNSecsSinceEpoch() / 1000;
        profQuadUs += (double)(profT1 - profT0);
        profSwapUs += (double)(profT2 - profT1);
        if (++profCount == 300)
        {
            fprintf(stderr, "[profile] present avg quad=%.3fms swap=%.3fms "
                            "(n=%u)\n",
                    profQuadUs / profCount / 1000.0,
                    profSwapUs / profCount / 1000.0, profCount);
            profCount = 0; profQuadUs = 0; profSwapUs = 0;
        }
    }
    XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
    g_xgpuLastFramePresented = true;
    return true;
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
