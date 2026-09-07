/******************************************************************************
 * @file       XGpuRenderDriver_gl.c
 * @brief      XGui GPU 渲染驱动——OpenGL/GLES 实现。
 * @details    实现 XGpuRenderDriver.h 的驱动操作表：离屏表面（GLX/WGL
 *             离屏 PBuffer）或窗口 GL 上下文中创建 RGBA8 FBO，使用
 *             GLES 2.0 兼容的最小 shader/纹理管线绘制矩形、图像和 CPU
 *             alpha 覆盖图；字形图集纹理由本驱动持有（通用层仅做装箱
 *             与键管理）；窗口会话上屏优先 1:1 glBlitFramebuffer，回退
 *             全屏 quad 采样。全部系统头（GL 函数经运行期 getProcAddress
 *             解析，无平台 GL 头）隔离在本文件内。
 * @note       仅在 XPLATFORMINTEGRATION_ON && XGPU_ON 时编译。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGpuRenderDriver.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON

#include "XPlatformGraphics.h"
#include "XImage.h"
#include "XMemory.h"
#include "XDateTime.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
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

/* ==================== GL 驱动会话 ==================== */

struct XGpuRenderDriverSession
{
    XPlatformOffscreenSurface* m_surface;    /**< 离屏上下文宿主（离屏会话）。 */
    XPlatformOpenGLContext* m_windowContext; /**< 窗口 GL 上下文（窗口会话）。 */
    bool m_windowSession;                    /**< 是否窗口会话。 */
    int m_width;                             /**< 渲染缓冲宽度（像素）。 */
    int m_height;                            /**< 渲染缓冲高度（像素）。 */
    bool m_firstFrame;                       /**< 窗口会话：首帧需初始化内容。 */
    bool m_valid;                            /**< 初始化是否成功（销毁时决定是否清理 GL 资源）。 */

    XglUInt m_framebuffer;                   /**< 离屏 FBO（窗口会话同样使用离屏 FBO）。 */
    XglUInt m_colorTexture;                  /**< 渲染目标颜色纹理。 */
    XglUInt m_sourceTexture;                 /**< 一次性上传源纹理（图像/覆盖图）。 */
    XglUInt m_vertexBuffer;                  /**< 全屏/quad 顶点缓冲。 */
    XglUInt m_solidProgram;                  /**< 纯色填充 program。 */
    XglUInt m_textureProgram;                /**< 纹理采样 program。 */
    XglInt m_solidColorLocation;             /**< u_color 位置。 */
    XglInt m_textureSamplerLocation;         /**< u_texture 位置。 */
    XglInt m_textureModulateLocation;        /**< u_modulate 位置。 */

    uint8_t* m_pixels;                       /**< 上传/回读暂存缓冲（拥有）。 */
    size_t m_pixelsCapacity;                 /**< 暂存缓冲容量（字节）。 */

    void (*glBlitFramebuffer)(XglInt, XglInt, XglInt, XglInt, XglInt, XglInt,
                              XglInt, XglInt, XglBitfield, XglEnum);
    bool m_hasBlit;                          /**< glBlitFramebuffer 可用（2b 快路径）。 */

    XglUInt m_glyphAtlasTexture;             /**< 字形图集纹理（RGBA 四通道=覆盖度）。 */

    XglEnum (*glGetError)(void);
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

/* ==================== 上下文与函数加载 ==================== */

static void* xgld_proc(XGpuRenderDriverSession* self, const char* name)
{
    if (!self || !name) return NULL;
    if (self->m_windowSession)
        return self->m_windowContext
            ? XPlatformOpenGLContext_getProcAddress(self->m_windowContext, name)
            : NULL;
    return self->m_surface
        ? XPlatformOffscreenSurface_getProcAddress(self->m_surface, name)
        : NULL;
}

static bool xgld_make_current(XGpuRenderDriverSession* self)
{
    if (!self) return false;
    if (self->m_windowSession)
        return self->m_windowContext &&
               XPlatformOpenGLContext_makeCurrent(self->m_windowContext);
    return self->m_surface &&
           XPlatformOffscreenSurface_makeCurrent(self->m_surface);
}

static void xgld_done_current(XGpuRenderDriverSession* self)
{
    if (!self) return;
    if (self->m_windowSession)
    {
        if (self->m_windowContext)
            XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
    }
    else if (self->m_surface)
        XPlatformOffscreenSurface_doneCurrent(self->m_surface);
}

static void xgld_context_destroy(XGpuRenderDriverSession* self)
{
    if (!self) return;
    if (self->m_windowSession)
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

static bool xgld_load_proc(XGpuRenderDriverSession* self, const char* name,
                           void* destination, size_t destinationSize)
{
    void* procedure;
    if (!destination || destinationSize != sizeof(procedure)) return false;
    procedure = xgld_proc(self, name);
    if (!procedure) return false;
    memcpy(destination, &procedure, sizeof(procedure));
    return true;
}

static bool xgld_load_proc_alias(XGpuRenderDriverSession* self,
                                 const char* name, const char* alias,
                                 void* destination, size_t destinationSize)
{
    void* procedure = xgld_proc(self, name);
    if (!procedure && alias) procedure = xgld_proc(self, alias);
    if (!procedure || !destination || destinationSize != sizeof(procedure))
        return false;
    memcpy(destination, &procedure, sizeof(procedure));
    return true;
}

#define XGPU_LOAD(member) \
    do { if (!xgld_load_proc(self, #member, &self->member, \
                             sizeof(self->member))) goto failed; } while (0)

/* ==================== 资源辅助 ==================== */

static void xgpu_set_blend(XGpuRenderDriverSession* self, bool sourceOver);
static bool xgpu_draw_quad_uv(XGpuRenderDriverSession* self, XglUInt program,
                              XglUInt texture, float x, float y, float width,
                              float height, float u0, float v0, float u1,
                              float v1, const float* color, bool textured);

static bool xgpu_reserve_pixels(XGpuRenderDriverSession* self, size_t bytes)
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

static bool xgpu_upload_image_flip(XGpuRenderDriverSession* self,
                                   const XImage* image,
                                   XglUInt texture, int width, int height,
                                   bool flipY);

static bool xgpu_upload_image(XGpuRenderDriverSession* self,
                              const XImage* image,
                              XglUInt texture, int width, int height)
{
    return xgpu_upload_image_flip(self, image, texture, width, height, false);
}

/**
 * @brief      上传图像到纹理；flipY 时行序翻转（帧画布语义）。
 * @details    采样用途（drawImage 的 sourceTexture）保持无翻转——quad
 *             的 UV 映射与 GL 的 NDC y 翻转已配平；帧画布用途
 *             （colorTexture：beginFrameImage/uploadTargetImage）必须
 *             翻转——绘制原语的 NDC y 翻转使"内容顶行"落在 FBO 高地址
 *             行，背景/快照的顶行也必须落在高地址行才能对齐（否则
 *             CPU 直写内容经"上传->快照读回"一圈后上下颠倒，
 *             RasterOp 等依赖既有画面的命令输出错误）。
 */
static bool xgpu_upload_image_flip(XGpuRenderDriverSession* self,
                                   const XImage* image,
                                   XglUInt texture, int width, int height,
                                   bool flipY)
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
                /* flipY：源行 y 落到纹理行 height-1-y（与 readback 的
                   翻转对称，帧画布"顶行"对齐 FBO 高地址行）。 */
                int sourceRow = flipY ? height - 1 - y : y;
                const uint8_t* srow = src + (size_t)sourceRow * (size_t)bpl;
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
        int sourceRow = flipY ? height - 1 - y : y;
        uint8_t* row = self->m_pixels + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; ++x)
        {
            uint32_t argb = XImage_pixel(image, x, sourceRow);
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

static bool xgpu_upload_alpha(XGpuRenderDriverSession* self,
                              const uint8_t* alpha,
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

static XglUInt xgld_compile_shader(XGpuRenderDriverSession* self, XglEnum type,
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

static XglUInt xgld_create_program(XGpuRenderDriverSession* self,
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
    vertex = xgld_compile_shader(self, XGL_VERTEX_SHADER, vertexSource);
    fragment = xgld_compile_shader(self, XGL_FRAGMENT_SHADER, fragmentSource);
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

static void xgld_prepare_texture(XGpuRenderDriverSession* self, XglUInt texture,
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

static void xgpu_set_blend(XGpuRenderDriverSession* self, bool sourceOver)
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

static void xgpu_rect_vertices_uv(XGpuRenderDriverSession* self, float x,
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

static bool xgpu_draw_quad_uv(XGpuRenderDriverSession* self, XglUInt program,
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

static bool xgpu_draw_quad(XGpuRenderDriverSession* self, XglUInt program,
                           XglUInt texture, float x, float y, float width,
                           float height, const float* color, bool textured)
{
    return xgpu_draw_quad_uv(self, program, texture, x, y, width, height,
                             0.0f, 0.0f, 1.0f, 1.0f, color, textured);
}

/* ==================== GL 初始化（离屏 / 窗口直通共用） ==================== */

/**
 * @brief 在已 makeCurrent 的上下文上加载 GL 函数并建立 FBO/纹理/shader。
 * @return true 成功；false 失败（调用方负责 doneCurrent 与释放）。
 */
static bool xgld_initialize(XGpuRenderDriverSession* self, int width, int height)
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

    XGPU_LOAD(glGetError);
    XGPU_LOAD(glViewport); XGPU_LOAD(glClearColor); XGPU_LOAD(glClear);
    XGPU_LOAD(glEnable); XGPU_LOAD(glDisable); XGPU_LOAD(glBlendFunc);
    XGPU_LOAD(glScissor); XGPU_LOAD(glPixelStorei); XGPU_LOAD(glReadPixels);
    if (!xgld_load_proc_alias(self, "glGenFramebuffers", "glGenFramebuffersOES",
                              &self->glGenFramebuffers, sizeof(self->glGenFramebuffers)) ||
        !xgld_load_proc_alias(self, "glDeleteFramebuffers", "glDeleteFramebuffersOES",
                              &self->glDeleteFramebuffers, sizeof(self->glDeleteFramebuffers)) ||
        !xgld_load_proc_alias(self, "glBindFramebuffer", "glBindFramebufferOES",
                              &self->glBindFramebuffer, sizeof(self->glBindFramebuffer)) ||
        !xgld_load_proc_alias(self, "glFramebufferTexture2D",
                              "glFramebufferTexture2DOES",
                              &self->glFramebufferTexture2D,
                              sizeof(self->glFramebufferTexture2D)) ||
        !xgld_load_proc_alias(self, "glCheckFramebufferStatus",
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
    self->glGenTextures(1, &self->m_glyphAtlasTexture);
    self->glGenBuffers(1, &self->m_vertexBuffer);
    if (!self->m_framebuffer || !self->m_colorTexture ||
        !self->m_sourceTexture || !self->m_glyphAtlasTexture ||
        !self->m_vertexBuffer)
        return false;
    xgld_prepare_texture(self, self->m_colorTexture, width, height);
    xgld_prepare_texture(self, self->m_sourceTexture, width, height);
    if (!self->m_hasBlit)
        self->m_hasBlit = xgld_load_proc_alias(
            self, "glBlitFramebuffer", "glBlitFramebufferNV",
            &self->glBlitFramebuffer, sizeof(self->glBlitFramebuffer));
    xgld_prepare_texture(self, self->m_glyphAtlasTexture,
                         XGPU_RENDER_GLYPH_ATLAS_SIZE,
                         XGPU_RENDER_GLYPH_ATLAS_SIZE);
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glFramebufferTexture2D(XGL_FRAMEBUFFER, XGL_COLOR_ATTACHMENT0,
                                  XGL_TEXTURE_2D, self->m_colorTexture, 0);
    if (self->glCheckFramebufferStatus(XGL_FRAMEBUFFER) != XGL_FRAMEBUFFER_COMPLETE)
        return false;
    self->m_solidProgram = xgld_create_program(self, solidFragment);
    self->m_textureProgram = xgld_create_program(self, textureFragment);
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

/* ==================== 驱动操作表实现 ==================== */

static bool xgld_available(void)
{
    /* OpenGL 无轻量进程级探测：由 sessionCreate 最终裁定，这里恒真。 */
    return true;
}

static XGpuRenderDriverSession* xgld_session_create_window(XWindow* window,
                                                           int width,
                                                           int height)
{
    XGpuRenderDriverSession* self =
        (XGpuRenderDriverSession*)XCalloc_System(1u, sizeof(*self));
    if (!self) return NULL;
    self->m_width = width;
    self->m_height = height;
    self->m_windowSession = true;
    self->m_firstFrame = true;
    self->m_windowContext = XPlatformOpenGLContext_create(window);
    if (!self->m_windowContext ||
        !XPlatformOpenGLContext_makeCurrent(self->m_windowContext))
        goto failed;
    if (!xgld_initialize(self, width, height)) goto failed;
    self->m_valid = true;
    XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
    return self;

failed:
    xgld_done_current(self);
    xgld_context_destroy(self);
    if (self->m_pixels) XFree_System(self->m_pixels);
    XFree_System(self);
    return NULL;
}

static XGpuRenderDriverSession* xgld_session_create_offscreen(int width,
                                                              int height)
{
    XGpuRenderDriverSession* self =
        (XGpuRenderDriverSession*)XCalloc_System(1u, sizeof(*self));
    if (!self) return NULL;
    self->m_width = width;
    self->m_height = height;
    self->m_surface = XPlatformOffscreenSurface_create((uint32_t)width,
                                                        (uint32_t)height);
    if (!self->m_surface || !XPlatformOffscreenSurface_makeCurrent(self->m_surface))
        goto failed;
    if (!xgld_initialize(self, width, height)) goto failed;
    self->m_valid = true;
    XPlatformOffscreenSurface_doneCurrent(self->m_surface);
    return self;

failed:
    xgld_done_current(self);
    xgld_context_destroy(self);
    if (self->m_pixels) XFree_System(self->m_pixels);
    XFree_System(self);
    return NULL;
}

static XGpuRenderDriverSession* xgld_session_create(XWindow* window,
                                                    int width, int height)
{
    return window ? xgld_session_create_window(window, width, height)
                  : xgld_session_create_offscreen(width, height);
}

static void xgld_session_destroy(XGpuRenderDriverSession* self)
{
    if (!self) return;
    if (self->m_valid && xgld_make_current(self))
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
        xgld_done_current(self);
    }
    xgld_context_destroy(self);
    if (self->m_pixels) XFree_System(self->m_pixels);
    XFree_System(self);
}

static bool xgld_begin_frame(XGpuRenderDriverSession* self,
                             const XImage* initialImage)
{
    if (!self || !xgld_make_current(self))
        return false;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glViewport(0, 0, self->m_width, self->m_height);
    self->glPixelStorei(XGL_PACK_ALIGNMENT, 1);
    self->glDisable(XGL_SCISSOR_TEST);
    xgpu_set_blend(self, true);
    if (self->m_windowSession)
    {
        /* 窗口直通：FBO 是持久缓冲（不清除、不重传旧 XImage），脏区绘制
           叠加在上一帧内容上；仅会话首帧需要以 XImage（若同尺寸）或
           透明初始化画布。 */
        if (self->m_firstFrame)
        {
            if (initialImage && XImage_width(initialImage) == self->m_width &&
                XImage_height(initialImage) == self->m_height)
            {
                if (!xgpu_upload_image_flip(self, initialImage,
                                            self->m_colorTexture,
                                            self->m_width, self->m_height,
                                            true))
                {
                    xgld_done_current(self);
                    return false;
                }
            }
            else
                XGpuRenderDriver_procs(XGpuRenderDriver_OpenGL)->clear(
                    self, 0u);
            self->m_firstFrame = false;
        }
        return true;
    }
    if (initialImage && XImage_width(initialImage) == self->m_width &&
        XImage_height(initialImage) == self->m_height)
    {
        if (!xgpu_upload_image_flip(self, initialImage, self->m_colorTexture,
                                    self->m_width, self->m_height, true))
            return false;
    }
    else
        XGpuRenderDriver_procs(XGpuRenderDriver_OpenGL)->clear(self, 0u);
    return true;
}

static void xgld_end_frame(XGpuRenderDriverSession* self)
{
    if (!self) return;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, 0);
    xgld_done_current(self);
}

static bool xgld_present_to_window(XGpuRenderDriverSession* self)
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
    if (!self || !self->m_windowSession || !self->m_windowContext)
        return false;
    if (!xgld_make_current(self))
        return false;
    if (profOn) profT0 = XDateTime_currentNSecsSinceEpoch() / 1000;
    self->glDisable(XGL_BLEND);
    self->glDisable(XGL_SCISSOR_TEST);
    if (self->m_hasBlit)
    {
        /* 2b 快路径：READ=FBO（持久画面），DRAW=默认帧缓冲；blit 后
           恢复原绑定。 */
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
        return true;
    }
    /* 全屏 quad 采样合成（NDC 直接映射：FBO 与窗口默认帧缓冲同为
       GL 左下原点，uv 不翻转）。 */
    vertices[0]  = -1.0f; vertices[1]  =  1.0f; vertices[2]  = 0.0f; vertices[3]  = 1.0f;
    vertices[4]  =  1.0f; vertices[5]  =  1.0f; vertices[6]  = 1.0f; vertices[7]  = 1.0f;
    vertices[8]  = -1.0f; vertices[9]  = -1.0f; vertices[10] = 0.0f; vertices[11] = 0.0f;
    vertices[12] =  1.0f; vertices[13] = -1.0f; vertices[14] = 1.0f; vertices[15] = 0.0f;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, 0);
    self->glViewport(0, 0, self->m_width, self->m_height);
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
    return true;
}

static bool xgld_readback(XGpuRenderDriverSession* self, XImage* target)
{
    size_t bytes;
    int y;
    if (!self || !target ||
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

static void xgld_clear(XGpuRenderDriverSession* self, uint32_t argb)
{
    unsigned a;
    if (!self) return;
    a = (argb >> 24) & 0xffu;
    self->glClearColor((XglFloat)xgpu_mul255((argb >> 16) & 0xffu,
                                             (uint8_t)a) / 255.0f,
                       (XglFloat)xgpu_mul255((argb >> 8) & 0xffu,
                                             (uint8_t)a) / 255.0f,
                       (XglFloat)xgpu_mul255((argb & 0xffu),
                                             (uint8_t)a) / 255.0f,
                       (XglFloat)a / 255.0f);
    self->glClear(XGL_COLOR_BUFFER_BIT);
}

static void xgld_set_clip_rect(XGpuRenderDriverSession* self, const XRect* rect)
{
    int x0, y0, x1, y1;
    if (!self) return;
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

static bool xgld_fill_rect(XGpuRenderDriverSession* self, const XRect* rect,
                           uint32_t color, float opacity, bool sourceOver)
{
    float rgba[4];
    unsigned a;
    if (!self || !rect || rect->width <= 0 || rect->height <= 0)
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

static bool xgld_draw_image(XGpuRenderDriverSession* self, const XImage* image,
                            int x, int y, int width, int height,
                            float opacity, bool sourceOver)
{
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (!self || !image || width <= 0 || height <= 0)
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

static bool xgld_draw_alpha_bitmap(XGpuRenderDriverSession* self,
                                   const uint8_t* alpha, int width,
                                   int height, int stride, int x, int y,
                                   uint32_t color, float opacity,
                                   bool sourceOver)
{
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (!self || !xgpu_upload_alpha(self, alpha, width, height, stride,
                                    color, opacity))
        return false;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad(self, self->m_textureProgram, self->m_sourceTexture,
                          (float)x, (float)y, (float)width, (float)height,
                          modulate, true);
}

static bool xgld_draw_solid_quad(XGpuRenderDriverSession* self, float x1,
                                 float y1, float x2, float y2, float x3,
                                 float y3, float x4, float y4,
                                 uint32_t premulColor, bool sourceOver)
{
    float vertices[16];
    float rgba[4];
    if (!self) return false;
    rgba[0] = (float)((premulColor >> 16) & 0xffu) / 255.0f;
    rgba[1] = (float)((premulColor >> 8) & 0xffu) / 255.0f;
    rgba[2] = (float)(premulColor & 0xffu) / 255.0f;
    rgba[3] = (float)((premulColor >> 24) & 0xffu) / 255.0f;
    vertices[0] = x1 * 2.0f / (float)self->m_width - 1.0f;
    vertices[1] = 1.0f - y1 * 2.0f / (float)self->m_height;
    vertices[2] = 0.0f; vertices[3] = 0.0f;
    vertices[4] = x2 * 2.0f / (float)self->m_width - 1.0f;
    vertices[5] = 1.0f - y2 * 2.0f / (float)self->m_height;
    vertices[6] = 0.0f; vertices[7] = 0.0f;
    vertices[8] = x3 * 2.0f / (float)self->m_width - 1.0f;
    vertices[9] = 1.0f - y3 * 2.0f / (float)self->m_height;
    vertices[10] = 0.0f; vertices[11] = 0.0f;
    vertices[12] = x4 * 2.0f / (float)self->m_width - 1.0f;
    vertices[13] = 1.0f - y4 * 2.0f / (float)self->m_height;
    vertices[14] = 0.0f; vertices[15] = 0.0f;
    self->glUseProgram(self->m_solidProgram);
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
    self->glUniform4f(self->m_solidColorLocation,
                      rgba[0], rgba[1], rgba[2], rgba[3]);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
    self->glDisableVertexAttribArray(0);
    self->glDisableVertexAttribArray(1);
    return true;
}

static bool xgld_upload_target_image(XGpuRenderDriverSession* self,
                                     const XImage* image)
{
    if (!self || !image || XImage_width(image) != self->m_width ||
        XImage_height(image) != self->m_height)
        return false;
    return xgpu_upload_image_flip(self, image, self->m_colorTexture,
                                  self->m_width, self->m_height, true);
}

static bool xgld_glyph_atlas_upload(XGpuRenderDriverSession* self,
                                    const uint8_t* coverage, int width,
                                    int height, int atlasX, int atlasY)
{
    size_t bytes;
    int y;
    if (!self || !coverage || width <= 0 || height <= 0 ||
        atlasX < 0 || atlasY < 0 ||
        atlasX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        atlasY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    bytes = (size_t)width * (size_t)height * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    for (y = 0; y < height; ++y)
    {
        int x;
        const uint8_t* source = coverage + (size_t)y * (size_t)width;
        uint8_t* row = self->m_pixels + (size_t)y * (size_t)width * 4u;
        for (x = 0; x < width; ++x)
        {
            uint8_t c = source[x];
            row[x * 4] = c;
            row[x * 4 + 1] = c;
            row[x * 4 + 2] = c;
            row[x * 4 + 3] = c;
        }
    }
    self->glBindTexture(XGL_TEXTURE_2D, self->m_glyphAtlasTexture);
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
    self->glTexSubImage2D(XGL_TEXTURE_2D, 0, atlasX, atlasY, width, height,
                          XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
    return true;
}

static bool xgld_glyph_atlas_draw(XGpuRenderDriverSession* self, int atlasX,
                                  int atlasY, int width, int height, int x,
                                  int y, uint32_t premulColor,
                                  bool sourceOver)
{
    float modulate[4];
    if (!self || width <= 0 || height <= 0 || atlasX < 0 || atlasY < 0 ||
        atlasX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        atlasY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    modulate[0] = (float)((premulColor >> 16) & 0xffu) / 255.0f;
    modulate[1] = (float)((premulColor >> 8) & 0xffu) / 255.0f;
    modulate[2] = (float)(premulColor & 0xffu) / 255.0f;
    modulate[3] = (float)((premulColor >> 24) & 0xffu) / 255.0f;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad_uv(
        self, self->m_textureProgram, self->m_glyphAtlasTexture,
        (float)x, (float)y, (float)width, (float)height,
        (float)atlasX / (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
        (float)atlasY / (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
        (float)(atlasX + width) / (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
        (float)(atlasY + height) / (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
        modulate, true);
}

static bool xgld_glyph_atlas_readback(XGpuRenderDriverSession* self,
                                      int atlasX, int atlasY, int atlasWidth,
                                      int atlasHeight, uint8_t* outCoverage)
{
    size_t bytes;
    XglUInt tempFbo = 0;
    int y;
    bool ok = false;
    if (!self || atlasX < 0 || atlasY < 0 || atlasWidth <= 0 ||
        atlasHeight <= 0 || !outCoverage ||
        atlasX + atlasWidth > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        atlasY + atlasHeight > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    if (!self->glGenFramebuffers || !self->glBindFramebuffer ||
        !self->glFramebufferTexture2D || !self->glCheckFramebufferStatus)
        return false;
    bytes = (size_t)atlasWidth * (size_t)atlasHeight * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    if (!xgld_make_current(self)) return false;
    self->glGenFramebuffers(1, &tempFbo);
    self->glBindFramebuffer(XGL_FRAMEBUFFER, tempFbo);
    self->glFramebufferTexture2D(XGL_FRAMEBUFFER, XGL_COLOR_ATTACHMENT0,
                                 XGL_TEXTURE_2D, self->m_glyphAtlasTexture, 0);
    if (self->glCheckFramebufferStatus(XGL_FRAMEBUFFER) ==
        XGL_FRAMEBUFFER_COMPLETE)
    {
        self->glPixelStorei(XGL_PACK_ALIGNMENT, 1);
        self->glReadPixels(atlasX, atlasY, atlasWidth, atlasHeight, XGL_RGBA,
                           XGL_UNSIGNED_BYTE, self->m_pixels);
        /* GL 原点在下方：读回的行序翻转后取覆盖度通道（四通道同值）。 */
        for (y = 0; y < atlasHeight; ++y)
        {
            const uint8_t* row = self->m_pixels +
                (size_t)(atlasHeight - 1 - y) * (size_t)atlasWidth * 4u;
            uint8_t* line = outCoverage + (size_t)y * (size_t)atlasWidth;
            int x;
            for (x = 0; x < atlasWidth; ++x)
                line[x] = row[(size_t)x * 4u + 3u];
        }
        ok = true;
    }
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    if (tempFbo) self->glDeleteFramebuffers(1, &tempFbo);
    xgld_done_current(self);
    return ok;
}

/* ==================== 驱动操作表 ==================== */

static const XGpuRenderDriverProcs g_xgldOpenGLProcs =
{
    .available = xgld_available,
    .sessionCreate = xgld_session_create,
    .sessionDestroy = xgld_session_destroy,
    .makeCurrent = xgld_make_current,
    .doneCurrent = xgld_done_current,
    .beginFrame = xgld_begin_frame,
    .endFrame = xgld_end_frame,
    .presentToWindow = xgld_present_to_window,
    .readback = xgld_readback,
    .clear = xgld_clear,
    .setClipRect = xgld_set_clip_rect,
    .fillRect = xgld_fill_rect,
    .drawImage = xgld_draw_image,
    .drawAlphaBitmap = xgld_draw_alpha_bitmap,
    .drawSolidQuad = xgld_draw_solid_quad,
    .glyphAtlasUpload = xgld_glyph_atlas_upload,
    .glyphAtlasDraw = xgld_glyph_atlas_draw,
    .glyphAtlasReadback = xgld_glyph_atlas_readback,
    .uploadTargetImage = xgld_upload_target_image
};

const XGpuRenderDriverProcs* XGpuRenderDriver_gl_procs(void)
{
    return &g_xgldOpenGLProcs;
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
