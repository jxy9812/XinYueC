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
#include "XSystem.h"

#include "XAlgorithm.h"
#include "XGpuRenderDriver.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON

#include "XPlatformGraphics.h"
#include "XImage.h"
#include "XMemory.h"
#include "XDateTime.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ==================== 最小 GLES 2/桌面 GL 类型与常量 ==================== */

/* GL 函数调用约定：Windows 上桌面 GL/WGL 的全部 GL 函数为 __stdcall
 * （GL/APIENTRY）， cdecl 调用方配 stdcall 被调方会在每次带参调用后
 * ESP 不平衡——Debug 的 /RTC 栈检查以 _RTC_CheckEsp 当场报错，Release
 * -O2 下表现为偶发栈损坏（xgld_initialize 崩溃的根因，2026-09-22 cdb
 * 抓栈定位）。非 Windows 平台（GLX/EGL）为 cdecl，宏展开为空。 */
#if defined(_WIN32)
#define XGLAPI __stdcall
#else
#define XGLAPI
#endif

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
#define XGL_TEXTURE1                0x84C1u
#define XGL_RGBA                    0x1908u
#define XGL_UNSIGNED_BYTE           0x1401u
#define XGL_TEXTURE_MIN_FILTER      0x2801u
#define XGL_TEXTURE_MAG_FILTER      0x2800u
#define XGL_TEXTURE_WRAP_S          0x2802u
#define XGL_TEXTURE_WRAP_T          0x2803u
#define XGL_NEAREST                 0x2600u
#define XGL_CLAMP_TO_EDGE           0x812Fu
#define XGL_UNPACK_ALIGNMENT        0x0CF5u
#define XGL_UNPACK_ROW_LENGTH       0x0CF2u
#define XGL_BGRA                    0x80E1u
#define XGL_UNSIGNED_INT_8_8_8_8_REV 0x8367u
#define XGL_PACK_ALIGNMENT           0x0D05u
#define XGL_ARRAY_BUFFER             0x8892u
#define XGL_DYNAMIC_DRAW             0x88E8u
#define XGL_FLOAT                    0x1406u
#define XGL_TRIANGLE_STRIP          0x0005u
#define XGL_VERTEX_SHADER            0x8B31u
#define XGL_VENDOR                   0x1F00u
#define XGL_RENDERER                 0x1F01u
#define XGL_VERSION                  0x1F02u
#define XGL_FRAGMENT_SHADER          0x8B30u
#define XGL_COMPILE_STATUS           0x8B81u
#define XGL_LINK_STATUS              0x8B82u

typedef void (XGLAPI *XglGenObjectsProc)(XglSizei, XglUInt*);
typedef void (XGLAPI *XglDeleteObjectsProc)(XglSizei, const XglUInt*);

/* ==================== GL 驱动会话 ==================== */

/* ==================== P-A（2026-09-25）drawImage 纹理身份缓存 ==================== */

/* 问题：图表页每帧对未变化的静态层大图整幅 glTexSubImage2D 重传
 * （~1.83MB/帧），瓦片/静态层缓存的上传节省全被淹没。方案：会话内
 * 建 {XImage*, 内容版本号, 纹理} 小 LRU——drawImage/drawImageUv 以
 * {对象指针+版本+格式+尺寸} 全键匹配，命中直接绑现成纹理跳过上传，
 * 失配重传并换版；drawImageRegion 只消费命中（不填充：脏区流每帧
 * 换图时避免整幅重传回退）。版本号由 XImage 侧逐写点维护
 * （XImageData_markDirty 单点派生，XImage_contentVersion 读取）。
 * 纪律（m_quadBatch 教训）：改/删任何缓存纹理内容前先 xgld_flush_quads
 * ——待定批 quad 可能仍引用该纹理；session destroy 出口冲批后配对
 * 释放全部缓存纹理。XGPU_TEX_IDENTITY_CACHE=1 启用（默认关，新特性
 * 沿排障开关先例）。 */
#define XGPU_TEX_IDENTITY_CACHE_SIZE 6

typedef struct XgpuTexIdentityEntry
{
    const XImage* m_image;   /**< 源图像对象（指针身份）。 */
    uint32_t      m_version; /**< 上传时的内容版本号。 */
    uint32_t      m_format;  /**< 上传时的像素格式（reinterpretAsFormat
                                   改格式不改版本时靠本键防串）。 */
    int           m_width;   /**< 上传时图像宽。 */
    int           m_height;  /**< 上传时图像高。 */
    XglUInt       m_texture; /**< 缓存纹理（0=空槽）。 */
    uint64_t      m_stamp;   /**< LRU 时钟戳（单调递增，小者最旧）。 */
} XgpuTexIdentityEntry;

/* P-A2（2026-09-25）换版跳过跟随表：组合态（身份缓存+图例瓦片）劣化
 * 归因在案（XCV_PROF 实测：劣化窗 blit 段 257→880-915µs、rebuild=0，
 * 即静态层未被重建却被反复重 populate 整幅重传）。机制：逐帧换版的
 * 过路图（一次性缩放/裁剪临时图、逐帧重写内容的小图）每次 populate 都
 * 按 LRU 逐出稳定条目（静态层/瓦片），稳定图被迫逐帧整幅重传（~1.83MB）
 * 并在逐帧同步 readback 前置下放大为整帧 GPU 排空停顿。跟随表按源图
 * 指针记录「连续换版次数」，连续两次换版即判为频繁变化图，populate
 * 拒收（该图回退 m_sourceTexture 旧路径，像素逐位一致，只是不再进 LRU
 * 搅动）；版本回稳自动复位重新收编。XGPU_TEX_IDENTITY_CHURN_SKIP=1
 * 启用（默认关=现状逐位不变）。无 GL 资源，session calloc 清零即空表，
 * destroy 无需清理。 */
#define XGPU_TEX_IDENTITY_CHURN_SIZE 4

typedef struct XgpuTexChurnEntry
{
    const XImage* m_image;   /**< 源图像对象（指针身份；NULL=空槽）。 */
    uint32_t      m_version; /**< 最近一次所见内容版本号。 */
    int           m_streak;  /**< 连续换版次数（0=版本稳定）。 */
} XgpuTexChurnEntry;

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
    int m_sourceTexWidth;                    /**< 源纹理当前存储宽（子矩形上传的
                                                  存储一致性跟踪；0=未初始化）。 */
    int m_sourceTexHeight;                   /**< 源纹理当前存储高。 */
    XglUInt m_vertexBuffer;                  /**< 全屏/quad 顶点缓冲。 */
    XglUInt m_solidProgram;                  /**< 纯色填充 program。 */
    XglUInt m_textureProgram;                /**< 纹理采样 program。 */
    XglUInt m_gradientProgram;               /**< 渐变×覆盖双采样 program。 */
    XglUInt m_gradientLutTexture;            /**< 渐变 LUT 纹理（256×1 预乘）。 */
    XglUInt m_gradientMaskTexture;           /**< 渐变覆盖掩码纹理（路径 bbox）。 */
    XglInt m_solidColorLocation;             /**< u_color 位置。 */
    XglInt m_textureSamplerLocation;         /**< u_texture 位置。 */
    XglInt m_textureModulateLocation;        /**< u_modulate 位置。 */
    XglInt m_gradientMaskLocation;           /**< u_mask 位置（unit0）。 */
    XglInt m_gradientLutLocation;            /**< u_lut 位置（unit1）。 */
    XglInt m_gradientModulateLocation;       /**< 渐变 u_modulate 位置。 */
    XglInt m_gradientLutAxisLocation;        /**< u_lutAxis 位置（0=水平 t 沿 x，1=沿 y）。 */

    uint8_t* m_pixels;                       /**< 上传/回读暂存缓冲（拥有）。 */
    size_t m_pixelsCapacity;                 /**< 暂存缓冲容量（字节）。 */

    /* 冗余状态调用缓存（每帧数百 quad 的固定开销削减；均按会话持有，
       上下文切换经会话隔离无串扰）。 */
    int m_blendState;                        /**< -1 未知 / 0 禁用 / 1 SourceOver。 */
    XglUInt m_activeProgram;                 /**< 当前 program（0=未知）。 */
    uint32_t m_solidColorKey;                /**< 最近纯色 uniform（ARGB 键）。 */
    bool m_solidColorValid;                  /**< 纯色 uniform 缓存有效。 */
    float m_modulateCache[4];                /**< 最近纹理 modulate uniform。 */
    bool m_modulateValid;                    /**< modulate 缓存有效。 */

    /* 统一顶点批（pos2+uv2+color4，48 float/quad）：纯色 quad（白纹理
       +uv=中心，白×色=色，逐位精确）与图集字形（同图集纹理跨字形存续）
       跑批；drawImage 系（上传即变形 m_sourceTexture）保持即时并先冲
       批。冲批触发=纹理/混合切换、scissor 变更、帧界/回读/上屏。
       XGPU_QUAD_BATCH=0 退回逐 quad 即时（排障开关）。 */
    XglUInt m_batchProgram;                  /**< 批 program（frag=texture2D*v_color）。 */
    XglInt m_batchSamplerLocation;           /**< 批程序 u_texture。 */
    XglUInt m_whiteTexture;                  /**< 1×1 白纹理（纯色 quad 采样恒 1）。 */
    XglUInt m_quadBatchTex;                  /**< 批内当前纹理（0=空批）。 */
    float* m_quadBatch;                      /**< 批顶点数组（拥有；48 float/quad）。 */
    int m_quadBatchCount;                    /**< 待冲批 quad 数。 */
    int m_quadBatchCapacity;                 /**< 批容量（quad 数）。 */
    int m_quadBatchBlend;                    /**< 批内混合态（-1 空 / 0 Source / 1 SourceOver）。 */
    int m_attribLayout;                      /**< 0=即时纹理布局(pos2+uv2,16B) 1=批布局(pos2+uv2+color4,32B) -1 未知。 */

    /* P0-2（2026-09-25）scissor 同矩形缓存：painter 每命令 setClipRect
       同值重入（同控件裁剪下多命令/嵌套保存恢复）不再 flush_quads，
       待定 quad 得以跨命令累积，批均 quad 数上升。缓存按会话持有
       （上下文切换经会话隔离无串扰）。XGPU_SCISSOR_CACHE=0 回退
       无条件 flush 旧行为（诊断用）。 */
    bool m_scissorOn;                        /**< scissor 当前是否启用（缓存）。 */
    bool m_scissorValid;                     /**< scissor 缓存是否有效（初始化前置 false）。 */
    int m_scissorX;                          /**< 缓存 scissor x（GL 窗口坐标）。 */
    int m_scissorY;                          /**< 缓存 scissor y（GL 窗口坐标）。 */
    int m_scissorW;                          /**< 缓存 scissor 宽。 */
    int m_scissorH;                          /**< 缓存 scissor 高。 */

    /* P-A（2026-09-25）drawImage 纹理身份缓存：静态层大图免每帧整幅
       重传。GL 纹理按上下文命名空间隔离，本表按会话持有（与 FBO/源
       纹理同纪），session destroy 冲批后配对释放。XGPU_TEX_IDENTITY_
       CACHE=1 启用（默认关）。 */
    XgpuTexIdentityEntry m_identityCache[XGPU_TEX_IDENTITY_CACHE_SIZE];
    uint64_t m_identityStamp;                /**< LRU 单调时钟。 */
    XgpuTexChurnEntry m_identityChurn[XGPU_TEX_IDENTITY_CHURN_SIZE];
                                             /**< P-A2 换版跟随表（无 GL 资源）。 */

    void (XGLAPI *glBlitFramebuffer)(XglInt, XglInt, XglInt, XglInt, XglInt, XglInt,
                              XglInt, XglInt, XglBitfield, XglEnum);
    bool m_hasBlit;                          /**< glBlitFramebuffer 可用（2b 快路径）。 */

    XglUInt m_glyphAtlasTexture;             /**< 字形图集纹理（RGBA 四通道=覆盖度）。 */

    XglEnum (XGLAPI *glGetError)(void);
    void (XGLAPI *glViewport)(XglInt, XglInt, XglSizei, XglSizei);
    void (XGLAPI *glClearColor)(XglFloat, XglFloat, XglFloat, XglFloat);
    void (XGLAPI *glClear)(XglBitfield);
    void (XGLAPI *glEnable)(XglEnum);
    void (XGLAPI *glDisable)(XglEnum);
    void (XGLAPI *glBlendFunc)(XglEnum, XglEnum);
    void (XGLAPI *glScissor)(XglInt, XglInt, XglSizei, XglSizei);
    void (XGLAPI *glPixelStorei)(XglEnum, XglInt);
    void (XGLAPI *glReadPixels)(XglInt, XglInt, XglSizei, XglSizei,
                         XglEnum, XglEnum, void*);

    XglGenObjectsProc glGenFramebuffers;
    XglDeleteObjectsProc glDeleteFramebuffers;
    void (XGLAPI *glBindFramebuffer)(XglEnum, XglUInt);
    void (XGLAPI *glFramebufferTexture2D)(XglEnum, XglEnum, XglEnum, XglUInt,
                                   XglInt);
    XglEnum (XGLAPI *glCheckFramebufferStatus)(XglEnum);

    XglGenObjectsProc glGenTextures;
    XglDeleteObjectsProc glDeleteTextures;
    const XglChar* (XGLAPI *glGetString)(XglEnum);
    void (XGLAPI *glBindTexture)(XglEnum, XglUInt);
    void (XGLAPI *glTexParameteri)(XglEnum, XglEnum, XglInt);
    void (XGLAPI *glTexImage2D)(XglEnum, XglInt, XglInt, XglSizei, XglSizei,
                         XglInt, XglEnum, XglEnum, const void*);
    void (XGLAPI *glTexSubImage2D)(XglEnum, XglInt, XglInt, XglInt, XglSizei,
                            XglSizei, XglEnum, XglEnum, const void*);
    void (XGLAPI *glActiveTexture)(XglEnum);

    XglGenObjectsProc glGenBuffers;
    XglDeleteObjectsProc glDeleteBuffers;
    void (XGLAPI *glBindBuffer)(XglEnum, XglUInt);
    void (XGLAPI *glBufferData)(XglEnum, XglSizeiptr, const void*, XglEnum);
    void (XGLAPI *glBufferSubData)(XglEnum, XglSizeiptr, XglSizeiptr,
                                   const void*);
    void (XGLAPI *glEnableVertexAttribArray)(XglUInt);
    void (XGLAPI *glDisableVertexAttribArray)(XglUInt);
    void (XGLAPI *glVertexAttribPointer)(XglUInt, XglInt, XglEnum, XglBoolean,
                                  XglSizei, const void*);
    void (XGLAPI *glDrawArrays)(XglEnum, XglInt, XglSizei);

    XglUInt (XGLAPI *glCreateShader)(XglEnum);
    void (XGLAPI *glShaderSource)(XglUInt, XglSizei, const XglChar* const*,
                           const XglInt*);
    void (XGLAPI *glCompileShader)(XglUInt);
    void (XGLAPI *glGetShaderiv)(XglUInt, XglEnum, XglInt*);
    void (XGLAPI *glDeleteShader)(XglUInt);
    XglUInt (XGLAPI *glCreateProgram)(void);
    void (XGLAPI *glAttachShader)(XglUInt, XglUInt);
    void (XGLAPI *glBindAttribLocation)(XglUInt, XglUInt, const XglChar*);
    void (XGLAPI *glLinkProgram)(XglUInt);
    void (XGLAPI *glGetProgramiv)(XglUInt, XglEnum, XglInt*);
    void (XGLAPI *glDeleteProgram)(XglUInt);
    void (XGLAPI *glUseProgram)(XglUInt);
    XglInt (XGLAPI *glGetUniformLocation)(XglUInt, const XglChar*);
    void (XGLAPI *glUniform1i)(XglInt, XglInt);
    void (XGLAPI *glUniform4f)(XglInt, XglFloat, XglFloat, XglFloat, XglFloat);
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

/* 当前已 makeCurrent 的会话追踪：GL 纹理/FBO ID 按上下文命名空间
   隔离，多会话（窗口+离屏）交替操作时必须先 ensure 自己的上下文，
   否则上传/采样/读回全部串号（实测图集字形跨会话不可见的根因）。 */
static XGpuRenderDriverSession* g_xgldCurrentSession = NULL;

static bool xgld_make_current(XGpuRenderDriverSession* self)
{
    if (!self) return false;
    if (self->m_windowSession)
    {
        if (self->m_windowContext &&
            XPlatformOpenGLContext_makeCurrent(self->m_windowContext))
        {
            g_xgldCurrentSession = self;
            return true;
        }
        return false;
    }
    if (self->m_surface &&
        XPlatformOffscreenSurface_makeCurrent(self->m_surface))
    {
        g_xgldCurrentSession = self;
        return true;
    }
    return false;
}

static bool xgld_ensure_current(XGpuRenderDriverSession* self)
{
    if (g_xgldCurrentSession == self) return true;
    if (!xgld_make_current(self)) return false;
    g_xgldCurrentSession = self;
    return true;
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
    g_xgldCurrentSession = NULL;
}

/* present 路径的直接 doneCurrent（绕过助手）同样要清追踪器。 */
static void xgld_clear_current_tracker(void)
{
    g_xgldCurrentSession = NULL;
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
    XMemcpy(destination, &procedure, sizeof(procedure));
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
    XMemcpy(destination, &procedure, sizeof(procedure));
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
    if (!flipY &&
        (XImage_format(image) == XImageFormat_ARGB32 ||
         XImage_format(image) == XImageFormat_ARGB32_Premultiplied) &&
        XImage_width(image) == width && XImage_height(image) == height)
    {
        /* P0-3（2026-09-25）：整幅直传（无翻转时 ARGB32 内存序与
           GL_BGRA+UINT_8_8_8_8_REV 逐字节一致），免暂存+交换。 */
        const uint8_t* src = XImage_constBits(image);
        int bpl = XImage_bytesPerLine(image);
        if (src && bpl >= width * 4)
        {
            self->glBindTexture(XGL_TEXTURE_2D, texture);
            self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
            self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA,
                               width, height, 0, XGL_BGRA,
                               XGL_UNSIGNED_INT_8_8_8_8_REV, src);
            if (texture == self->m_sourceTexture)
            {
                self->m_sourceTexWidth = width;
                self->m_sourceTexHeight = height;
            }
            return true;
        }
    }
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
            if (texture == self->m_sourceTexture)
            {
                self->m_sourceTexWidth = width;
                self->m_sourceTexHeight = height;
            }
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
    if (texture == self->m_sourceTexture)
    {
        self->m_sourceTexWidth = width;
        self->m_sourceTexHeight = height;
    }
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
    /* 存储尺寸跟踪必须同步：否则 drawImageRegion 的 TexSubImage 增量
       上传会按陈旧尺寸写入过小存储（GL 静默报错→采样陈旧内容污迹，
       2026-09-24 线帧蓝色污迹根因）。 */
    self->m_sourceTexWidth = width;
    self->m_sourceTexHeight = height;
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

static XglUInt xgld_create_program_vx(XGpuRenderDriverSession* self,
                                      const char* vertexSource,
                                      const char* fragmentSource,
                                      const char* colorAttribName)
{
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
    if (colorAttribName)
        self->glBindAttribLocation(program, 2, colorAttribName);
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

static XglUInt xgld_create_program(XGpuRenderDriverSession* self,
                                   const char* fragmentSource)
{
    static const char vertexSource[] =
        "attribute vec2 a_position;"
        "attribute vec2 a_texcoord;"
        "varying vec2 v_texcoord;"
        "void main(){gl_Position=vec4(a_position,0.0,1.0);"
        "v_texcoord=a_texcoord;}";
    return xgld_create_program_vx(self, vertexSource, fragmentSource, NULL);
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
    int want = sourceOver ? 1 : 0;
    if (self->m_blendState == want) return; /* 冗余状态跳过。 */
    if (!sourceOver)
    {
        self->glDisable(XGL_BLEND);
    }
    else
    {
        self->glEnable(XGL_BLEND);
        /* 所有上传的纹理和纯色均为预乘 RGBA。 */
        self->glBlendFunc(XGL_ONE, XGL_ONE_MINUS_SRC_ALPHA);
    }
    self->m_blendState = want;
}

/** @brief program 切换缓存（连续同类 quad 免重复 glUseProgram）。 */
static void xgpu_use_program(XGpuRenderDriverSession* self, XglUInt program)
{
    if (self->m_activeProgram == program) return;
    self->glUseProgram(program);
    self->m_activeProgram = program;
}

/** @brief 纹理 modulate uniform 缓存。 */
static void xgpu_set_modulate(XGpuRenderDriverSession* self,
                              XglInt location, const float* rgba)
{
    if (self->m_modulateValid &&
        self->m_modulateCache[0] == rgba[0] &&
        self->m_modulateCache[1] == rgba[1] &&
        self->m_modulateCache[2] == rgba[2] &&
        self->m_modulateCache[3] == rgba[3])
        return;
    self->glUniform4f(location, rgba[0], rgba[1], rgba[2], rgba[3]);
    self->m_modulateCache[0] = rgba[0];
    self->m_modulateCache[1] = rgba[1];
    self->m_modulateCache[2] = rgba[2];
    self->m_modulateCache[3] = rgba[3];
    self->m_modulateValid = true;
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

/* quad 顶点上传：glBufferData 每次孤儿化重分配（驱动自动复用，读中
   缓冲不阻塞）。勿改 glBufferSubData 原位更新——GPU 仍在读该缓冲时
   逐 quad 隐式同步停顿，实测 216→106 FPS（2026-09-24 复实证）。 */
static void xgpu_vertex_data(XGpuRenderDriverSession* self,
                             const float* vertices)
{
    self->glBufferData(XGL_ARRAY_BUFFER,
                       (XglSizeiptr)(sizeof(float) * 16u), vertices,
                       XGL_DYNAMIC_DRAW);
}

/* ==================== 纯色 quad 批合并 ==================== */

/** @brief 切换顶点属性布局（0=纹理 pos2+uv2 / 1=批 pos2+color4）。
 *  @note  指针随当前 VBO 绑定捕获；两布局共用同一 VBO。 */
static void xgld_set_attrib_layout(XGpuRenderDriverSession* self, int layout)
{
    if (self->m_attribLayout == layout) return;
    if (layout == 0)
    {
        /* 即时纹理布局（旧 texture 程序/渐变/present）：pos2+uv2。 */
        self->glEnableVertexAttribArray(0);
        self->glEnableVertexAttribArray(1);
        self->glDisableVertexAttribArray(2);
        self->glVertexAttribPointer(0, 2, XGL_FLOAT, XGL_FALSE,
                                    (XglSizei)(sizeof(float) * 4u),
                                    (const void*)0);
        self->glVertexAttribPointer(1, 2, XGL_FLOAT, XGL_FALSE,
                                    (XglSizei)(sizeof(float) * 4u),
                                    (const void*)(sizeof(float) * 2u));
    }
    else
    {
        /* 批布局：pos2+uv2+color4（32B/顶点）。 */
        self->glEnableVertexAttribArray(0);
        self->glEnableVertexAttribArray(1);
        self->glEnableVertexAttribArray(2);
        self->glVertexAttribPointer(0, 2, XGL_FLOAT, XGL_FALSE,
                                    (XglSizei)(sizeof(float) * 8u),
                                    (const void*)0);
        self->glVertexAttribPointer(1, 2, XGL_FLOAT, XGL_FALSE,
                                    (XglSizei)(sizeof(float) * 8u),
                                    (const void*)(sizeof(float) * 2u));
        self->glVertexAttribPointer(2, 4, XGL_FLOAT, XGL_FALSE,
                                    (XglSizei)(sizeof(float) * 8u),
                                    (const void*)(sizeof(float) * 4u));
    }
    self->m_attribLayout = layout;
}

/** @brief unit0 纹理绑定（无条件——上传/渐变等路径会直改绑定，缓存
 *         易失真；字形跑批期间同纹理重复绑定的代价可接受）。 */
static void xgld_bind_texture0(XGpuRenderDriverSession* self, XglUInt texture)
{
    self->glActiveTexture(XGL_TEXTURE0);
    self->glBindTexture(XGL_TEXTURE_2D, texture);
}

/** @brief 冲批：累积 quad 一次 drawArrays 提交（退化三角带连接）。
 *  @note  冲批点=采样纹理切换/混合切换/scissor 变更/帧界（endFrame/
 *         present/readback/uploadTarget/clear）以及上传即变形源纹理的
 *         即时型原语（drawImage 系/drawAlphaBitmap）之前。 */
static void xgld_flush_quads(XGpuRenderDriverSession* self)
{
    int totalVerts;
    if (!self || self->m_quadBatchCount == 0) return;
    totalVerts = self->m_quadBatchCount * 6;
    xgpu_set_blend(self, self->m_quadBatchBlend != 0);
    xgpu_use_program(self, self->m_batchProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgld_set_attrib_layout(self, 1);
    xgld_bind_texture0(self, self->m_quadBatchTex);
    self->glBufferData(XGL_ARRAY_BUFFER,
                       (XglSizeiptr)(sizeof(float) * 48u *
                                     (size_t)self->m_quadBatchCount),
                       self->m_quadBatch, XGL_DYNAMIC_DRAW);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, (XglSizei)totalVerts);
    self->m_quadBatchCount = 0;
    self->m_quadBatchBlend = -1;
    self->m_quadBatchTex = 0;
}

/** @brief 批状态就绪：纹理/混合与批内不同则先冲批（跨字形存续的
 *         关键——同纹理同混合的连续 quad 不再被纹理型原语打散）。 */
static void xgld_batch_set_state(XGpuRenderDriverSession* self,
                                 XglUInt texture, bool sourceOver)
{
    int wantBlend = sourceOver ? 1 : 0;
    if (self->m_quadBatchCount > 0 &&
        (self->m_quadBatchTex != texture ||
         self->m_quadBatchBlend != wantBlend))
        xgld_flush_quads(self);
    if (self->m_quadBatchCount == 0)
    {
        self->m_quadBatchTex = texture;
        self->m_quadBatchBlend = wantBlend;
    }
    xgld_bind_texture0(self, texture);
}

/** @brief 追加一个 quad 到批（4 顶点 + 2 退化连接顶点=48 float/quad）。
 *  @return true 已入批；false 批不可用（调用方退回即时绘制）。 */
static bool xgld_append_quad(XGpuRenderDriverSession* self, const float* v32)
{
    float* dst;
    if (self->m_quadBatchCount >= self->m_quadBatchCapacity)
    {
        int newCap = self->m_quadBatchCapacity > 0
                         ? self->m_quadBatchCapacity * 2 : 1024;
        float* grown = (float*)XRealloc_System(
            self->m_quadBatch,
            (size_t)newCap * 48u * sizeof(float));
        if (!grown) return false;
        self->m_quadBatch = grown;
        self->m_quadBatchCapacity = newCap;
    }
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgld_set_attrib_layout(self, 1);
    dst = self->m_quadBatch + (size_t)self->m_quadBatchCount * 48u;
    {
        /* 退化连接：重复上一 quad 末顶点（BR，偏移 24..31）与本 quad
           首顶点（TL）；首 quad 无上邻，用自身 TL 充当（零面积退化）。 */
        const float* prevBR = self->m_quadBatchCount > 0
                                  ? dst - 48u + 24u
                                  : v32;
        XMemcpy(dst, prevBR, sizeof(float) * 8u);
        XMemcpy(dst + 8, v32, sizeof(float) * 8u);
        XMemcpy(dst + 16, v32, sizeof(float) * 32u);
    }
    self->m_quadBatchCount++;
    return true;
}

/** @brief 排障开关：XGPU_QUAD_BATCH=0 退回逐 quad 即时绘制。 */
static bool xgld_quad_batch_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_QUAD_BATCH");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/** @brief 身份缓存开关：XGPU_TEX_IDENTITY_CACHE=1 启用，默认关
 *         （新特性默认关闭，排障口径与既有开关相反——按需求文字面）。 */
static bool xgld_tex_identity_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_TEX_IDENTITY_CACHE");
        enabled = value && *value && value[0] == '1' && value[1] == 0;
    }
    return enabled != 0;
}

/** @brief 换版跳过开关：XGPU_TEX_IDENTITY_CHURN_SKIP=1 启用，默认关
 *         （P-A2 修复沿身份缓存开关先例；关=populate 行为逐位现状）。 */
static bool xgld_tex_identity_churn_skip(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value =
            XSystem_environment("XGPU_TEX_IDENTITY_CHURN_SKIP");
        enabled = value && *value && value[0] == '1' && value[1] == 0;
    }
    return enabled != 0;
}

/**
 * @brief populate 准入：频繁换版图拒收，防逐出稳定条目（P-A2）。
 * @return true 允许 populate；false 拒收（调用方回退 m_sourceTexture
 *         旧路径，像素一致，只是本图不再占用 LRU 槽位）。
 * @note 判据：跟随表按源图指针记录最近版本；与上次所见版本相同=稳定
 *       放行（streak 清零）；不同=换版一次，连续两次即拒收——一次性
 *       临时图与逐帧重写图第二帧起不再搅动 LRU。表满未登记的新指针
 *       放行（不阻断首传；下一帧换版自然进表）。开关关时恒 true。
 */
static bool xgld_identity_churn_admit(XGpuRenderDriverSession* self,
                                      const XImage* image)
{
    uint32_t version;
    int i;
    int seen = -1;
    int freeSlot = -1;
    if (!self || !image) return true;
    if (!xgld_tex_identity_churn_skip()) return true;
    version = XImage_contentVersion(image);
    for (i = 0; i < XGPU_TEX_IDENTITY_CHURN_SIZE; ++i)
    {
        XgpuTexChurnEntry* entry = &self->m_identityChurn[i];
        if (!entry->m_image)
        {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (entry->m_image == image)
        {
            seen = i;
            break;
        }
    }
    if (seen < 0)
    {
        if (freeSlot >= 0)
        {
            self->m_identityChurn[freeSlot].m_image = image;
            self->m_identityChurn[freeSlot].m_version = version;
            self->m_identityChurn[freeSlot].m_streak = 0;
        }
        return true;
    }
    if (self->m_identityChurn[seen].m_version == version)
    {
        self->m_identityChurn[seen].m_streak = 0; /* 版本回稳：重新收编。 */
        return true;
    }
    self->m_identityChurn[seen].m_version = version;
    {
        /* 首次换版仍放行一次（容错偶发重绘）；连续第二次换版起拒收。 */
        int admit = self->m_identityChurn[seen].m_streak < 1;
        self->m_identityChurn[seen].m_streak++;
        return admit;
    }
}

/**
 * @brief 身份缓存查找：{指针+版本+格式+尺寸} 全键匹配。
 * @param self    会话。
 * @param image   源图像。
 * @param width   期望的图像存储宽（上传口径）。
 * @param height  期望的图像存储高。
 * @param outSlot 未命中时输出可用槽（空槽优先，否则 LRU 最旧；表满且
 *                无匹配时为最旧条目），可为 NULL（纯消费查找）。
 * @return 命中返回缓存纹理名并刷新 LRU 时钟；未命中返回 0。
 * @note 命中路径无 GL 调用（仅读表+时钟），quad 直接入批跨帧采样——
 *       缓存纹理内容只在本表变更（populate 复用槽前冲批）与 destroy
 *       （冲批后释放）两处变动，待定批引用安全。
 */
static XglUInt xgld_identity_cache_find(XGpuRenderDriverSession* self,
                                        const XImage* image,
                                        int width, int height, int* outSlot)
{
    int i;
    int slot = -1;
    uint64_t oldest = 0;
    if (outSlot) *outSlot = -1;
    if (!self || !image) return 0;
    for (i = 0; i < XGPU_TEX_IDENTITY_CACHE_SIZE; ++i)
    {
        XgpuTexIdentityEntry* entry = &self->m_identityCache[i];
        if (!entry->m_texture)
        {
            if (slot < 0) slot = i; /* 空槽优先复用，不逐出活条目。 */
            continue;
        }
        if (entry->m_image == image &&
            entry->m_version == XImage_contentVersion(image) &&
            entry->m_format == (uint32_t)XImage_format(image) &&
            entry->m_width == width && entry->m_height == height)
        {
            entry->m_stamp = ++self->m_identityStamp;
            return entry->m_texture;
        }
        if (slot < 0 || entry->m_stamp < oldest)
        {
            slot = i;
            oldest = entry->m_stamp;
        }
    }
    if (outSlot) *outSlot = slot;
    return 0;
}

/**
 * @brief 身份缓存填充：整幅（无翻转）上传进缓存纹理并登记。
 * @return 缓存纹理名；分配或上传失败返回 0（调用方回退 m_sourceTexture
 *         旧路径，会话状态不受影响）。
 * @note 复用已有纹理（版本失配/逐出）前先冲批——同帧早先 drawImage
 *       的待定批 quad 可能仍引用该纹理旧内容（m_quadBatch 教训）。
 *       失配即"失配重传"：同图新版本内容覆盖同槽纹理，旧引用已冲批
 *       落盘不再采样。新分配纹理上传失败当场配对释放（登记前失败，
 *       无待定引用，无需冲批）。
 */
static XglUInt xgld_identity_cache_populate(XGpuRenderDriverSession* self,
                                            const XImage* image,
                                            int width, int height, int slot)
{
    XgpuTexIdentityEntry* entry;
    XglUInt texture;
    uint32_t version;
    if (!self || !image || slot < 0 || slot >= XGPU_TEX_IDENTITY_CACHE_SIZE)
        return 0;
    version = XImage_contentVersion(image);
    entry = &self->m_identityCache[slot];
    if (entry->m_texture)
    {
        xgld_flush_quads(self); /* 待定 quad 按旧纹理内容先落盘。 */
        texture = entry->m_texture;
    }
    else
    {
        self->glGenTextures(1, &texture);
        if (!texture) return 0;
        /* 过滤/环绕参数必须显式设（NEAREST/CLAMP；默认 mipmap 过滤对
           非 mip 纹理采样不完整），与 m_sourceTexture 同口径。 */
        xgld_prepare_texture(self, texture, width, height);
    }
    if (!xgpu_upload_image(self, image, texture, width, height))
    {
        if (!entry->m_texture)
            self->glDeleteTextures(1, &texture); /* 新纹理配对释放。 */
        return 0;
    }
    entry->m_image = image;
    entry->m_version = version;
    entry->m_format = (uint32_t)XImage_format(image);
    entry->m_width = width;
    entry->m_height = height;
    entry->m_texture = texture;
    entry->m_stamp = ++self->m_identityStamp;
    return texture;
}

/**
 * @brief 释放全部身份缓存纹理（session destroy 专用出口）。
 * @note 必须在 xgld_flush_quads 之后调用（destroy 路径已保证）：待定
 *       批 quad 先按缓存纹理内容落盘，再删除纹理——配对纪律，与
 *       m_sourceTexture/m_colorTexture 同口径。仅登记为已释放；条目
 *       版本/指针域无需清零（会话随即整体释放）。
 */
static void xgld_identity_cache_clear(XGpuRenderDriverSession* self)
{
    int i;
    if (!self || !self->glDeleteTextures) return;
    for (i = 0; i < XGPU_TEX_IDENTITY_CACHE_SIZE; ++i)
    {
        if (self->m_identityCache[i].m_texture)
        {
            self->glDeleteTextures(1, &self->m_identityCache[i].m_texture);
            self->m_identityCache[i].m_texture = 0;
        }
    }
}

/** @brief 纯色 uniform 缓存（网格线等同色 quad 连续提交时免 uniform）。 */
static void xgpu_set_solid_color(XGpuRenderDriverSession* self,
                                 const float* rgba)
{
    uint32_t key = (uint32_t)(rgba[0] * 255.0f + 0.5f) << 16 |
                   (uint32_t)(rgba[1] * 255.0f + 0.5f) << 8 |
                   (uint32_t)(rgba[2] * 255.0f + 0.5f);
    key |= rgba[3] > 0.5f ? 0x1000000u : 0u;
    if (self->m_solidColorValid && self->m_solidColorKey == key) return;
    self->glUniform4f(self->m_solidColorLocation,
                      rgba[0], rgba[1], rgba[2], rgba[3]);
    self->m_solidColorKey = key;
    self->m_solidColorValid = true;
}

/** @brief 纯色任意四顶点 quad 入批（白纹理采样恒 1，白×色=色逐位精确；
 *         顶点序 TL,TR,BL,BR）。批关闭或入批失败退回 uniform 即时。 */
static void xgpu_emit_solid_quad4(XGpuRenderDriverSession* self,
                                  float x1, float y1, float x2, float y2,
                                  float x3, float y3, float x4, float y4,
                                  const float* color, bool sourceOver)
{
    if (xgld_quad_batch_enabled())
    {
        float v[32];
        v[0]  = x1 * 2.0f / (float)self->m_width - 1.0f;
        v[1]  = 1.0f - y1 * 2.0f / (float)self->m_height;
        v[2]  = 0.5f; v[3] = 0.5f;
        v[4]  = color[0]; v[5] = color[1]; v[6] = color[2]; v[7] = color[3];
        v[8]  = x2 * 2.0f / (float)self->m_width - 1.0f;
        v[9]  = 1.0f - y2 * 2.0f / (float)self->m_height;
        v[10] = 0.5f; v[11] = 0.5f;
        v[12] = color[0]; v[13] = color[1]; v[14] = color[2]; v[15] = color[3];
        v[16] = x3 * 2.0f / (float)self->m_width - 1.0f;
        v[17] = 1.0f - y3 * 2.0f / (float)self->m_height;
        v[18] = 0.5f; v[19] = 0.5f;
        v[20] = color[0]; v[21] = color[1]; v[22] = color[2]; v[23] = color[3];
        v[24] = x4 * 2.0f / (float)self->m_width - 1.0f;
        v[25] = 1.0f - y4 * 2.0f / (float)self->m_height;
        v[26] = 0.5f; v[27] = 0.5f;
        v[28] = color[0]; v[29] = color[1]; v[30] = color[2]; v[31] = color[3];
        xgld_batch_set_state(self, self->m_whiteTexture, sourceOver);
        if (xgld_append_quad(self, v))
            return;
    }
    xgpu_set_blend(self, sourceOver);
    xgpu_use_program(self, self->m_solidProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgld_set_attrib_layout(self, 0);
    {
        float vertices[16];
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
        xgpu_vertex_data(self, vertices);
    }
    xgpu_set_solid_color(self, color);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
}

/** @brief 轴对齐纯色矩形入批便捷口。 */
static void xgpu_emit_solid_quad(XGpuRenderDriverSession* self,
                                 float x, float y, float width, float height,
                                 const float* color, bool sourceOver)
{
    xgpu_emit_solid_quad4(self, x, y, x + width, y, x, y + height,
                          x + width, y + height, color, sourceOver);
}

static bool xgpu_draw_quad_uv(XGpuRenderDriverSession* self, XglUInt program,
                              XglUInt texture, float x, float y, float width,
                              float height, float u0, float v0, float u1,
                              float v1, const float* color, bool textured)
{
    float vertices[16];
    if (!self || !program || width <= 0.0f || height <= 0.0f) return false;
    if (!textured)
    {
        /* 纯色填充：并入统一批（白纹理采样恒 1）。 */
        xgpu_emit_solid_quad(self, x, y, width, height, color,
                             self->m_blendState != 0);
        return true;
    }
    if (texture == self->m_sourceTexture)
    {
        /* 上传即变形源纹理（drawImage 系）：不可跨后续上传延迟采样，
           冲批后即时绘制（旧路径，modulate uniform）。 */
        xgld_flush_quads(self);
        xgpu_rect_vertices_uv(self, x, y, width, height, vertices, true,
                              u0, v0, u1, v1);
        xgpu_use_program(self, program);
        self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
        xgpu_vertex_data(self, vertices);
        xgld_set_attrib_layout(self, 0);
        xgld_bind_texture0(self, texture);
        self->glUniform1i(self->m_textureSamplerLocation, 0);
        xgpu_set_modulate(self, self->m_textureModulateLocation, color);
        self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
        return true;
    }
    /* 缓存型纹理（字形图集等）：入批跨字形存续。 */
    {
        float v[32];
        v[0]  = x * 2.0f / (float)self->m_width - 1.0f;
        v[1]  = 1.0f - y * 2.0f / (float)self->m_height;
        v[2]  = u0; v[3] = v0;
        v[4]  = color[0]; v[5] = color[1]; v[6] = color[2]; v[7] = color[3];
        v[8]  = (x + width) * 2.0f / (float)self->m_width - 1.0f;
        v[9]  = 1.0f - y * 2.0f / (float)self->m_height;
        v[10] = u1; v[11] = v0;
        v[12] = color[0]; v[13] = color[1]; v[14] = color[2]; v[15] = color[3];
        v[16] = x * 2.0f / (float)self->m_width - 1.0f;
        v[17] = 1.0f - (y + height) * 2.0f / (float)self->m_height;
        v[18] = u0; v[19] = v1;
        v[20] = color[0]; v[21] = color[1]; v[22] = color[2]; v[23] = color[3];
        v[24] = (x + width) * 2.0f / (float)self->m_width - 1.0f;
        v[25] = 1.0f - (y + height) * 2.0f / (float)self->m_height;
        v[26] = u1; v[27] = v1;
        v[28] = color[0]; v[29] = color[1]; v[30] = color[2]; v[31] = color[3];
        xgld_batch_set_state(self, texture, self->m_blendState != 0);
        if (xgld_append_quad(self, v))
            return true;
    }
    /* 入批失败退回即时（旧纹理路径）。 */
    xgld_flush_quads(self);
    xgpu_rect_vertices_uv(self, x, y, width, height, vertices, textured,
                          u0, v0, u1, v1);
    xgpu_use_program(self, program);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgpu_vertex_data(self, vertices);
    xgld_set_attrib_layout(self, 0);
    xgld_bind_texture0(self, texture);
    self->glUniform1i(self->m_textureSamplerLocation, 0);
    xgpu_set_modulate(self, self->m_textureModulateLocation, color);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
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
    /* 批管线：frag = texture2D(u_texture, v_texcoord) * v_color。纯色
       quad 绑 1×1 白纹理 + uv 中心（白×色=色，逐位精确）；图集字形
       绑图集 + 子矩形 UV + v_color=modulate——与旧 uniform 路径同数
       学。纹理/混合切换才冲批：字形长跑批得以存续。 */
    static const char batchVertex[] =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "attribute vec2 a_position;"
        "attribute vec2 a_texcoord;"
        "attribute vec4 a_color;"
        "varying vec2 v_texcoord;"
        "varying vec4 v_color;"
        "void main(){gl_Position=vec4(a_position,0.0,1.0);"
        "v_texcoord=a_texcoord;v_color=a_color;}";
    static const char batchFragment[] =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform sampler2D u_texture;"
        "varying vec2 v_texcoord;"
        "varying vec4 v_color;"
        "void main(){gl_FragColor=texture2D(u_texture,v_texcoord)*v_color;}";
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
    /* 渐变×覆盖双采样（方向 B：fillPath 原生化的 LUT 通道）：unit0=
       路径覆盖掩码（bbox 全幅 0..1），unit1=256×1 渐变 LUT（u 轴承载
       渐变参数 t，v 固定 0.5）。片元输出 = LUT 色 × 覆盖度 × 调制。 */
    const char gradientFragment[] =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "uniform sampler2D u_mask;"
        "uniform sampler2D u_lut;"
        "uniform int u_lutAxis;"
        "uniform vec4 u_modulate;"
        "varying vec2 v_texcoord;"
        "void main(){"
        "vec4 m=texture2D(u_mask,v_texcoord);"
        "float t=(u_lutAxis==0)?v_texcoord.x:v_texcoord.y;"
        "vec4 c=texture2D(u_lut,vec2(t,0.5));"
        "gl_FragColor=c*m*u_modulate;}";;
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
    XGPU_LOAD(glBufferSubData);
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
    self->glGenTextures(1, &self->m_whiteTexture);
    self->glGenBuffers(1, &self->m_vertexBuffer);
    if (!self->m_framebuffer || !self->m_colorTexture ||
        !self->m_sourceTexture || !self->m_glyphAtlasTexture ||
        !self->m_whiteTexture || !self->m_vertexBuffer)
        return false;
    xgld_prepare_texture(self, self->m_colorTexture, width, height);
    xgld_prepare_texture(self, self->m_sourceTexture, width, height);
    self->m_sourceTexWidth = width;
    self->m_sourceTexHeight = height;
    /* 1×1 白纹理：纯色 quad 经批管线采样白点（白×v_color=v_color，
       逐位精确），与字形共用同一批程序。prepare_texture 设 NEAREST/
       CLAMP_TO_EDGE（默认 mipmap 过滤对非 mip 纹理采样不完整）。 */
    xgld_prepare_texture(self, self->m_whiteTexture, 1, 1);
    self->glBindTexture(XGL_TEXTURE_2D, self->m_whiteTexture);
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
    {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        self->glTexSubImage2D(XGL_TEXTURE_2D, 0, 0, 0, 1, 1, XGL_RGBA,
                              XGL_UNSIGNED_BYTE, white);
    }
    /* quad 顶点缓冲一次性分配存储（后续经 glBufferData 孤儿化更新）。
       顶点属性布局由 xgld_set_attrib_layout 按需切换（纹理/批两布局）；
       calloc 清零会把 m_attribLayout 置 0（=纹理布局已配置的假象，属性
       指针从未真正设定），必须显式置 -1=未知（2026-09-24 atlas/polygon
       纹理绘制全哑的根因）。 */
    self->m_attribLayout = -1;
    self->m_quadBatchBlend = -1;
    self->m_scissorValid = false; /* P0-2：scissor 缓存无效=首设必真置。 */
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    self->glBufferData(XGL_ARRAY_BUFFER,
                       (XglSizeiptr)(sizeof(float) * 16u), NULL,
                       XGL_DYNAMIC_DRAW);
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
    self->m_batchProgram = xgld_create_program_vx(
        self, batchVertex, batchFragment, "a_color");
    self->m_textureProgram = xgld_create_program(self, textureFragment);
    self->m_gradientProgram = xgld_create_program(self, gradientFragment);
    if (!self->m_solidProgram || !self->m_batchProgram ||
        !self->m_textureProgram) return false;
    self->m_solidColorLocation = self->glGetUniformLocation(
        self->m_solidProgram, "u_color");
    self->m_textureSamplerLocation = self->glGetUniformLocation(
        self->m_textureProgram, "u_texture");
    self->m_textureModulateLocation = self->glGetUniformLocation(
        self->m_textureProgram, "u_modulate");
    if (self->m_solidColorLocation < 0 || self->m_textureSamplerLocation < 0 ||
        self->m_textureModulateLocation < 0)
        return false;
    self->m_batchSamplerLocation = self->glGetUniformLocation(
        self->m_batchProgram, "u_texture");
    if (self->m_batchSamplerLocation < 0) return false;
    self->glUseProgram(self->m_batchProgram);
    self->glUniform1i(self->m_batchSamplerLocation, 0);
    self->glUseProgram(0);
    /* GL 实现指纹：一次性打印（XGPU_PROF=1 时）——判别硬件驱动 vs
       软件实现（llvmpipe/Mesa softpipe）与 RDP 会话显示栈，
       回答"GPU 到底启用到哪一层"（2026-09-24 排查用）。 */
    {
        static int glInfoPrinted = 0;
        const char* prof = XSystem_environment("XGPU_PROF");
        if (!glInfoPrinted && prof && *prof && self->glGetString)
        {
            const char* vendor = (const char*)self->glGetString(XGL_VENDOR);
            const char* renderer = (const char*)self->glGetString(XGL_RENDERER);
            const char* version = (const char*)self->glGetString(XGL_VERSION);
            fprintf(stderr, "[gl-info] vendor=%s renderer=%s version=%s\n",
                    vendor ? vendor : "?", renderer ? renderer : "?",
                    version ? version : "?");
            glInfoPrinted = 1;
        }
    }
    /* 渐变通道（可选能力：装配失败仅禁用渐变快速路径，回退软件）。 */
    self->glGenTextures(1, &self->m_gradientLutTexture);
    self->glGenTextures(1, &self->m_gradientMaskTexture);
    if (!self->m_gradientLutTexture || !self->m_gradientMaskTexture)
        return false;
    xgld_prepare_texture(self, self->m_gradientLutTexture, 256, 1);
    if (self->m_gradientProgram)
    {
        self->m_gradientMaskLocation = self->glGetUniformLocation(
            self->m_gradientProgram, "u_mask");
        self->m_gradientLutLocation = self->glGetUniformLocation(
            self->m_gradientProgram, "u_lut");
        self->m_gradientModulateLocation = self->glGetUniformLocation(
            self->m_gradientProgram, "u_modulate");
        self->m_gradientLutAxisLocation = self->glGetUniformLocation(
            self->m_gradientProgram, "u_lutAxis");
        if (self->m_gradientMaskLocation < 0 ||
            self->m_gradientLutLocation < 0 ||
            self->m_gradientModulateLocation < 0 ||
            self->m_gradientLutAxisLocation < 0)
            return false;
        self->glUseProgram(self->m_gradientProgram);
        self->glUniform1i(self->m_gradientMaskLocation, 0);
        self->glUniform1i(self->m_gradientLutLocation, 1);
        self->glUseProgram(0);
    }
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
    {
        /* vsync 显式关闭：SwapBuffers 默认随刷新率同步会把直通帧率
           钳在 60Hz（低于软件路径数百 FPS 的量级），与"GPU 远超软件"
           的目标直接冲突。加载失败（驱动不提供扩展）按默认行为继续。 */
        typedef void (XGLAPI *xgldSwapIntervalProc)(int);
        xgldSwapIntervalProc swapInterval =
            (xgldSwapIntervalProc)xgld_proc(self, "wglSwapIntervalEXT");
        if (swapInterval) swapInterval(0);
    }
    if (!xgld_initialize(self, width, height)) goto failed;
    self->m_valid = true;
    xgld_done_current(self); /* 清追踪器：create 后上下文不保持当前。 */
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
    xgld_done_current(self); /* 清追踪器：create 后上下文不保持当前。 */
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
        xgld_flush_quads(self);
        /* P-A：身份缓存纹理配对释放——必须在冲批之后（待定 quad 先按
           缓存纹理内容落盘），与 m_sourceTexture 同口径。 */
        xgld_identity_cache_clear(self);
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
    if (self->m_quadBatch) XFree_System(self->m_quadBatch);
    XFree_System(self);
}

static bool xgld_begin_frame(XGpuRenderDriverSession* self,
                             const XImage* initialImage)
{
    if (!xgld_ensure_current(self)) return false;
    if (!self || !xgld_make_current(self))
        return false;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glViewport(0, 0, self->m_width, self->m_height);
    self->glPixelStorei(XGL_PACK_ALIGNMENT, 1);
    self->glDisable(XGL_SCISSOR_TEST);
    self->m_scissorValid = false; /* P0-2：旁路禁用 scissor，缓存失效（否则同矩形早返回会跳过重启用）。 */
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
    xgld_ensure_current(self);
    if (!self) return;
    xgld_flush_quads(self);
    self->glBindFramebuffer(XGL_FRAMEBUFFER, 0);
    /* 上下文保持当前（不再 doneCurrent）：同一 UI 线程的下一位图器
       begin 经 xgld_ensure_current O(1) 直返——此前每控件帧各一对
       makeCurrent/doneCurrent，800x600 图表页 ~45 对/帧实测成为
       直通路径的主要 CPU 开销之一。present/destroy 各出口仍显式
       解绑（xgld_clear_current_tracker 语义不变）。 */
}

static bool xgld_present_to_window(XGpuRenderDriverSession* self)
{
    if (!xgld_ensure_current(self)) return false;
    float vertices[16];
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static unsigned profCount;
    static double profQuadUs, profSwapUs;
    int64_t profT0 = 0, profT1 = 0, profT2 = 0;
    static int profOn = -1;
    if (profOn < 0)
    {
        const char* env = XSystem_environment("XGPU_PROFILE");
        profOn = env && *env ? 1 : 0;
    }
    if (!self || !self->m_windowSession || !self->m_windowContext)
        return false;
    if (!xgld_make_current(self))
        return false;
    xgld_flush_quads(self);
    if (profOn) profT0 = XDateTime_currentNSecsSinceEpoch() / 1000;
    xgpu_set_blend(self, false);
    self->glDisable(XGL_SCISSOR_TEST);
    self->m_scissorValid = false; /* P0-2：旁路禁用 scissor，缓存失效（同上）。 */
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
        xgpu_set_blend(self, true);
        if (!XPlatformOpenGLContext_swapBuffers(self->m_windowContext))
        {
            XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
            xgld_clear_current_tracker();
            return false;
        }
        XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
        xgld_clear_current_tracker();
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
    xgpu_use_program(self, self->m_textureProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgpu_vertex_data(self, vertices);
    xgld_set_attrib_layout(self, 0);
    self->glActiveTexture(XGL_TEXTURE0);
    self->glBindTexture(XGL_TEXTURE_2D, self->m_colorTexture);
    self->glUniform1i(self->m_textureSamplerLocation, 0);
    self->glUniform4f(self->m_textureModulateLocation,
                      modulate[0], modulate[1], modulate[2], modulate[3]);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
    if (profOn) profT1 = XDateTime_currentNSecsSinceEpoch() / 1000;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    xgpu_set_blend(self, true);
    if (!XPlatformOpenGLContext_swapBuffers(self->m_windowContext))
    {
        XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
        xgld_clear_current_tracker();
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
    xgld_clear_current_tracker();
    return true;
}

static bool xgld_readback(XGpuRenderDriverSession* self, XImage* target)
{
    if (!xgld_ensure_current(self)) return false;
    size_t bytes;
    int y;
    if (!self || !target ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    xgld_flush_quads(self);
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
    xgld_ensure_current(self);
    unsigned a;
    if (!self) return;
    xgld_flush_quads(self);
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
    xgld_ensure_current(self);
    int x0, y0, x1, y1;
    bool on;
    static int cacheEnabled = -1; /* -1 未读环境；0=旧行为（诊断回退）。 */
    if (!self) return;
    if (cacheEnabled < 0)
    {
        const char* ce = XSystem_environment("XGPU_SCISSOR_CACHE");
        cacheEnabled = ce && *ce && !(ce[0] == '0' && ce[1] == 0) ? 0 : 1;
    }
    /* P0-2：先算目标态（不动 GL、不冲批）。同矩形且已启用 → 直返：
       待定 quad 仍在本剪裁下落盘，顺序与剪裁语义均不变，只是免去
       每命令一次的 flush（此前批均 3-4 quad 即被 setClipRect 切断）。 */
    if (!rect)
    {
        on = false;
        x0 = y0 = 0;
        x1 = y1 = 0;
    }
    else
    {
        x0 = rect->x < 0 ? 0 : rect->x;
        y0 = rect->y < 0 ? 0 : rect->y;
        x1 = rect->x > INT_MAX - rect->width ? INT_MAX : rect->x + rect->width;
        y1 = rect->y > INT_MAX - rect->height ? INT_MAX : rect->y + rect->height;
        if (x1 > self->m_width) x1 = self->m_width;
        if (y1 > self->m_height) y1 = self->m_height;
        on = !(x1 <= x0 || y1 <= y0);
        if (!on) { x0 = y0 = 0; x1 = y1 = 0; /* 空剪裁：启用+零窗口（原语义）。 */ }
    }
    if (cacheEnabled && self->m_quadBatchCount == 0 &&
        self->m_scissorValid &&
        self->m_scissorOn == on &&
        self->m_scissorX == x0 && self->m_scissorY == y0 &&
        self->m_scissorW == x1 - x0 && self->m_scissorH == y1 - y0)
        return;
    xgld_flush_quads(self); /* 待定批在旧 scissor 下落盘，再改剪裁。 */
    if (!on)
    {
        self->glDisable(XGL_SCISSOR_TEST);
    }
    else if (x1 - x0 <= 0 || y1 - y0 <= 0)
    {
        self->glEnable(XGL_SCISSOR_TEST);
        self->glScissor(0, 0, 0, 0);
    }
    else
    {
        self->glEnable(XGL_SCISSOR_TEST);
        self->glScissor(x0, self->m_height - y1, x1 - x0, y1 - y0);
    }
    self->m_scissorOn = on;
    self->m_scissorValid = true;
    self->m_scissorX = x0;
    self->m_scissorY = y0;
    self->m_scissorW = x1 - x0;
    self->m_scissorH = y1 - y0;
}

static bool xgld_fill_rect(XGpuRenderDriverSession* self, const XRect* rect,
                           uint32_t color, float opacity, bool sourceOver)
{
    if (!xgld_ensure_current(self)) return false;
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
    if (!xgld_ensure_current(self)) return false;
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    XglUInt texture = 0;
    if (!self || !image || width <= 0 || height <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (XImage_width(image) != width || XImage_height(image) != height)
        return false;
    /* P-A 纹理身份缓存：未变化静态层命中即免整幅重传（默认关，
       XGPU_TEX_IDENTITY_CACHE=1 启用）。命中失败走原 m_sourceTexture
       上传路径，行为与关闭时逐位一致。P-A2：频繁换版图 populate 拒收
       （XGPU_TEX_IDENTITY_CHURN_SKIP=1），防逐出稳定条目引发逐帧
       整幅重传；拒收即原路径，像素逐位一致。 */
    if (xgld_tex_identity_enabled())
    {
        int slot = -1;
        texture = xgld_identity_cache_find(self, image, width, height, &slot);
        if (!texture && xgld_identity_churn_admit(self, image))
            texture = xgld_identity_cache_populate(self, image, width,
                                                   height, slot);
    }
    if (!texture &&
        !xgpu_upload_image(self, image, self->m_sourceTexture, width, height))
        return false;
    if (!texture) texture = self->m_sourceTexture;
    /* The source texture is premultiplied, so opacity scales RGB and alpha
       together before the premultiplied blend. */
    modulate[0] = opacity;
    modulate[1] = opacity;
    modulate[2] = opacity;
    modulate[3] = opacity;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad(self, self->m_textureProgram, texture,
                          (float)x, (float)y, (float)width, (float)height,
                          modulate, true);
}

static bool xgld_draw_image_uv(XGpuRenderDriverSession* self,
                               const XImage* image, int x, int y, int width,
                               int height, float u0, float v0, float u1,
                               float v1, float opacity, bool sourceOver)
{
    if (!xgld_ensure_current(self)) return false;
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    int iw;
    int ih;
    XglUInt texture = 0;
    if (!self || !image || width <= 0 || height <= 0)
        return false;
    iw = XImage_width(image);
    ih = XImage_height(image);
    if (iw <= 0 || ih <= 0) return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    /* P-A 纹理身份缓存：与 drawImage 同口径（整幅存储为键，UV 子矩形
       照常采样）；命中免整幅重传。P-A2 换版拒收同 drawImage。 */
    if (xgld_tex_identity_enabled())
    {
        int slot = -1;
        texture = xgld_identity_cache_find(self, image, iw, ih, &slot);
        if (!texture && xgld_identity_churn_admit(self, image))
            texture = xgld_identity_cache_populate(self, image, iw, ih, slot);
    }
    if (!texture &&
        !xgpu_upload_image(self, image, self->m_sourceTexture, iw, ih))
        return false;
    if (!texture) texture = self->m_sourceTexture;
    /* 源纹理为预乘布局：opacity 对 RGB/A 同步缩放后再预乘混合。 */
    modulate[0] = opacity;
    modulate[1] = opacity;
    modulate[2] = opacity;
    modulate[3] = opacity;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad_uv(self, self->m_textureProgram,
                             texture,
                             (float)x, (float)y, (float)width, (float)height,
                             u0, v0, u1, v1, modulate, true);
}

/**
 * @brief 子矩形区域绘制（批量提交脏区通道）：仅上传源图像的子区域
 *        （CPU 侧逐行 R/B 交换进暂存缓冲 + glTexSubImage2D 增量上传），
 *        再按 1:1 采样绘制到目标位置。源纹理存储按整幅图像跟踪，尺寸
 *        不符时以 glTexImage2D(NULL) 重分配（免整幅数据上传）。
 */
static bool xgld_draw_image_region(XGpuRenderDriverSession* self,
                                   const XImage* image, int srcX, int srcY,
                                   int srcW, int srcH, int dstX, int dstY,
                                   float opacity, bool sourceOver)
{
    float modulate[4];
    float u0;
    float v0;
    float u1;
    float v1;
    const uint8_t* src;
    int bpl;
    int iw;
    int ih;
    int y;
    if (!xgld_ensure_current(self)) return false;
    if (!self || !image || srcW <= 0 || srcH <= 0)
        return false;
    iw = XImage_width(image);
    ih = XImage_height(image);
    if (iw <= 0 || ih <= 0 || srcX < 0 || srcY < 0 ||
        srcX + srcW > iw || srcY + srcH > ih)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    /* P-A 纹理身份缓存（只消费不填充）：整图内容已在缓存纹理时，
       区域上传整体跳过，直接按子矩形 UV 采样缓存纹理。不填充的
       理由：脏区流（每帧同图换版本）若填充即整幅重传，反而回退
       现有增量上传；填充由 drawImage/drawImageUv 整幅路径负责。
       未命中时下行原路径逐行为先，行为与缓存关闭时一致。 */
    if (xgld_tex_identity_enabled())
    {
        XglUInt cached = xgld_identity_cache_find(self, image, iw, ih, NULL);
        if (cached)
        {
            u0 = (float)srcX / (float)iw;
            v0 = (float)srcY / (float)ih;
            u1 = (float)(srcX + srcW) / (float)iw;
            v1 = (float)(srcY + srcH) / (float)ih;
            modulate[0] = opacity;
            modulate[1] = opacity;
            modulate[2] = opacity;
            modulate[3] = opacity;
            xgpu_set_blend(self, sourceOver);
            return xgpu_draw_quad_uv(self, self->m_textureProgram, cached,
                                     (float)dstX, (float)dstY, (float)srcW,
                                     (float)srcH, u0, v0, u1, v1, modulate,
                                     true);
        }
    }
    if (self->m_sourceTexWidth != iw || self->m_sourceTexHeight != ih)
    {
        /* 存储尺寸不符：重分配存储（NULL 数据，免整幅上传）。 */
        self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
        self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
        self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA, iw, ih, 0,
                           XGL_RGBA, XGL_UNSIGNED_BYTE, NULL);
        self->m_sourceTexWidth = iw;
        self->m_sourceTexHeight = ih;
    }
    src = XImage_constBits(image);
    bpl = XImage_bytesPerLine(image);
    {
        /* P0-3（2026-09-25）：子区域直传——GL_BGRA + UNSIGNED_INT_8_8_8_8_REV
           的字节语义（首分量取最低字节）与 ARGB32 小端内存序 B,G,R,A
           逐字节一致，配 UNPACK_ROW_LENGTH=iw 免 CPU 逐像素 R/B 交换与
           暂存拷贝（桌面 GL 1.2+ 语义，AMD Radeon 实测 4.6 支持）。
           XGPU_REGION_SWAP=1 回退 CPU 交换旧路径（诊断用）。 */
        static int cpuSwap = -1;
        if (cpuSwap < 0)
        {
            const char* cs = XSystem_environment("XGPU_REGION_SWAP");
            cpuSwap = cs && *cs && !(cs[0] == '0' && cs[1] == 0) ? 1 : 0;
        }
        if (!cpuSwap && src && bpl >= iw * 4)
        {
            self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
            self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
            self->glPixelStorei(XGL_UNPACK_ROW_LENGTH, iw);
            self->glTexSubImage2D(XGL_TEXTURE_2D, 0, srcX, srcY, srcW, srcH,
                                  XGL_BGRA, XGL_UNSIGNED_INT_8_8_8_8_REV,
                                  src + (size_t)srcY * (size_t)bpl +
                                      (size_t)srcX * 4u);
            self->glPixelStorei(XGL_UNPACK_ROW_LENGTH, 0);
        }
        else
        {
            if (!xgpu_reserve_pixels(self, (size_t)srcW * (size_t)srcH * 4u))
                return false;
            if (!src || bpl < iw * 4)
            {
                /* 非常规布局：逐像素回退。 */
                int x;
                for (y = 0; y < srcH; ++y)
                {
                    uint8_t* row =
                        self->m_pixels + (size_t)y * (size_t)srcW * 4u;
                    for (x = 0; x < srcW; ++x)
                    {
                        uint32_t argb = XImage_pixel(image, srcX + x, srcY + y);
                        uint8_t a = (uint8_t)(argb >> 24);
                        row[x * 4 + 0] = (uint8_t)((argb >> 16) & 0xffu);
                        row[x * 4 + 1] = (uint8_t)((argb >> 8) & 0xffu);
                        row[x * 4 + 2] = (uint8_t)(argb & 0xffu);
                        row[x * 4 + 3] = a;
                    }
                }
            }
            else
            {
                for (y = 0; y < srcH; ++y)
                {
                    const uint8_t* srow =
                        src + (size_t)(srcY + y) * (size_t)bpl +
                        (size_t)srcX * 4u;
                    uint8_t* drow =
                        self->m_pixels + (size_t)y * (size_t)srcW * 4u;
                    int x;
                    for (x = 0; x < srcW; ++x)
                    {
                        drow[x * 4 + 0] = srow[x * 4 + 2]; /* R <- B */
                        drow[x * 4 + 1] = srow[x * 4 + 1]; /* G */
                        drow[x * 4 + 2] = srow[x * 4 + 0]; /* B <- R */
                        drow[x * 4 + 3] = srow[x * 4 + 3]; /* A */
                    }
                }
            }
            self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
            self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
            self->glTexSubImage2D(XGL_TEXTURE_2D, 0, srcX, srcY, srcW, srcH,
                                  XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
        }
    }
    {
        /* 重复上传收口（2026-09-25 审计）：P0-3 直传块插入后，旧函数体
           的「CPU R/B 交换 + TexSubImage」被整段遗留于尾部，对同一
           (srcX,srcY,srcW,srcH) 区域做第二次等值上传——三条路径写入
           内容逐位相同，纯冗余（2× 上传带宽 + 快路径下全量 CPU 交换）；
           且直传快路径未经 reserve_pixels 即写 m_pixels（会话 XCalloc
           起始为 NULL，容量不足时为越界写隐患）。默认跳过遗留体；
           XGPU_GL_DUP_UPLOAD=1 诊断回退旧双重上传（含隐患路径）。 */
        static int dupUpload = -1;
        if (dupUpload < 0)
        {
            const char* du = XSystem_environment("XGPU_GL_DUP_UPLOAD");
            dupUpload = du && *du && !(du[0] == '0' && du[1] == 0) ? 1 : 0;
        }
        if (dupUpload)
        {
            if (!src || bpl < iw * 4)
            {
                /* 非常规布局：逐像素回退。 */
                int x;
                for (y = 0; y < srcH; ++y)
                {
                    uint8_t* row =
                        self->m_pixels + (size_t)y * (size_t)srcW * 4u;
                    for (x = 0; x < srcW; ++x)
                    {
                        uint32_t argb =
                            XImage_pixel(image, srcX + x, srcY + y);
                        uint8_t a = (uint8_t)(argb >> 24);
                        row[x * 4 + 0] = (uint8_t)((argb >> 16) & 0xffu);
                        row[x * 4 + 1] = (uint8_t)((argb >> 8) & 0xffu);
                        row[x * 4 + 2] = (uint8_t)(argb & 0xffu);
                        row[x * 4 + 3] = a;
                    }
                }
            }
            else
            {
                for (y = 0; y < srcH; ++y)
                {
                    const uint8_t* srow =
                        src + (size_t)(srcY + y) * (size_t)bpl +
                        (size_t)srcX * 4u;
                    uint8_t* drow =
                        self->m_pixels + (size_t)y * (size_t)srcW * 4u;
                    int x;
                    for (x = 0; x < srcW; ++x)
                    {
                        drow[x * 4 + 0] = srow[x * 4 + 2]; /* R <- B */
                        drow[x * 4 + 1] = srow[x * 4 + 1]; /* G */
                        drow[x * 4 + 2] = srow[x * 4 + 0]; /* B <- R */
                        drow[x * 4 + 3] = srow[x * 4 + 3]; /* A */
                    }
                }
            }
            self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
            self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
            self->glTexSubImage2D(XGL_TEXTURE_2D, 0, srcX, srcY, srcW,
                                  srcH, XGL_RGBA, XGL_UNSIGNED_BYTE,
                                  self->m_pixels);
        }
    }
    /* UV：图像顶行在纹理 v=0（上传无翻转），与 draw_quad_uv 的
       「目标顶边←v0」约定一致，子区域按图像坐标归一化。 */
    u0 = (float)srcX / (float)iw;
    v0 = (float)srcY / (float)ih;
    u1 = (float)(srcX + srcW) / (float)iw;
    v1 = (float)(srcY + srcH) / (float)ih;
    modulate[0] = opacity;
    modulate[1] = opacity;
    modulate[2] = opacity;
    modulate[3] = opacity;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad_uv(self, self->m_textureProgram,
                             self->m_sourceTexture,
                             (float)dstX, (float)dstY, (float)srcW,
                             (float)srcH, u0, v0, u1, v1, modulate, true);
}

static bool xgld_draw_alpha_bitmap(XGpuRenderDriverSession* self,
                                   const uint8_t* alpha, int width,
                                   int height, int stride, int x, int y,
                                   uint32_t color, float opacity,
                                   bool sourceOver)
{
    if (!xgld_ensure_current(self)) return false;
    float modulate[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    if (!self || !xgpu_upload_alpha(self, alpha, width, height, stride,
                                    color, opacity))
        return false;
    xgpu_set_blend(self, sourceOver);
    return xgpu_draw_quad(self, self->m_textureProgram, self->m_sourceTexture,
                          (float)x, (float)y, (float)width, (float)height,
                          modulate, true);
}

/* 渐变×覆盖双纹理绘制（方向 B fillPath 原生化核心）：coverage 为路径
   覆盖图（每像素 1 字节，bbox 局部），lutRgba 为 256×1 预乘 ARGB LUT
   （u 轴承载渐变参数 t）。掩码整幅上传专用纹理，LUT 常驻专用纹理；
   片元 = LUT(t) × 覆盖度 × 不透明度，单次 TRIANGLE_STRIP 完成。 */
static bool xgld_draw_gradient_alpha(XGpuRenderDriverSession* self,
                                     const unsigned char* coverage,
                                     int width, int height, int x, int y,
                                     const unsigned char* lutRgba,
                                     int lutAxis, float opacity,
                                     bool sourceOver)
{
    float modulate[4];
    float vertices[16];
    size_t bytes;
    int px, py;
    if (!self || !coverage || !lutRgba || width <= 0 || height <= 0)
        return false;
    if (!xgld_ensure_current(self)) return false;
    xgld_flush_quads(self);
    bytes = (size_t)width * (size_t)height * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    for (py = 0; py < height; ++py)
    {
        const unsigned char* src =
            coverage + (size_t)py * (size_t)width;
        unsigned char* row =
            self->m_pixels + (size_t)py * (size_t)width * 4u;
        for (px = 0; px < width; ++px)
        {
            unsigned char c = src[px];
            row[px * 4] = c;
            row[px * 4 + 1] = c;
            row[px * 4 + 2] = c;
            row[px * 4 + 3] = c;
        }
    }
    /* 掩码纹理：路径 bbox 尺寸独立分配（texImage2D 重定尺寸）。 */
    self->glBindTexture(XGL_TEXTURE_2D, self->m_gradientMaskTexture);
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
    self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA, width, height,
                       0, XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
    /* LUT：256×1 预乘 ARGB 每次同步（渐变停止点可变）。 */
    self->glBindTexture(XGL_TEXTURE_2D, self->m_gradientLutTexture);
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, 1);
    self->glTexSubImage2D(XGL_TEXTURE_2D, 0, 0, 0, 256, 1, XGL_RGBA,
                          XGL_UNSIGNED_BYTE, lutRgba);
    modulate[0] = opacity;
    modulate[1] = opacity;
    modulate[2] = opacity;
    modulate[3] = opacity;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, self->m_framebuffer);
    self->glViewport(0, 0, self->m_width, self->m_height);
    xgpu_set_blend(self, sourceOver);
    xgpu_use_program(self, self->m_gradientProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgpu_rect_vertices_uv(self, (float)x, (float)y, (float)width,
                          (float)height, vertices, true, 0.0f, 0.0f, 1.0f,
                          1.0f);
    xgpu_vertex_data(self, vertices);
    xgld_set_attrib_layout(self, 0);
    /* unit0=掩码（UV 全幅），unit1=LUT（片元内 vec2(x,0.5) 采样）。 */
    self->glActiveTexture(XGL_TEXTURE0);
    self->glBindTexture(XGL_TEXTURE_2D, self->m_gradientMaskTexture);
    self->glActiveTexture(XGL_TEXTURE1);
    self->glBindTexture(XGL_TEXTURE_2D, self->m_gradientLutTexture);
    self->glUniform1i(self->m_gradientMaskLocation, 0);
    self->glUniform1i(self->m_gradientLutLocation, 1);
    self->glUniform1i(self->m_gradientLutAxisLocation, lutAxis);
    self->glUniform4f(self->m_gradientModulateLocation,
                      modulate[0], modulate[1], modulate[2], modulate[3]);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
    self->glActiveTexture(XGL_TEXTURE0);
    return true;
}

static bool xgld_draw_solid_quad(XGpuRenderDriverSession* self, float x1,
                                 float y1, float x2, float y2, float x3,
                                 float y3, float x4, float y4,
                                 uint32_t premulColor, bool sourceOver)
{
    if (!xgld_ensure_current(self)) return false;
    float rgba[4];
    if (!self) return false;
    rgba[0] = (float)((premulColor >> 16) & 0xffu) / 255.0f;
    rgba[1] = (float)((premulColor >> 8) & 0xffu) / 255.0f;
    rgba[2] = (float)(premulColor & 0xffu) / 255.0f;
    rgba[3] = (float)((premulColor >> 24) & 0xffu) / 255.0f;
    xgpu_emit_solid_quad4(self, x1, y1, x2, y2, x3, y3, x4, y4, rgba,
                          sourceOver);
    return true;
}

static bool xgld_upload_target_image(XGpuRenderDriverSession* self,
                                     const XImage* image)
{
    if (!xgld_ensure_current(self)) return false;
    xgld_flush_quads(self);
    if (!self || !image || XImage_width(image) != self->m_width ||
        XImage_height(image) != self->m_height)
        return false;
    return xgpu_upload_image_flip(self, image, self->m_colorTexture,
                                  self->m_width, self->m_height, true);
}

/** @brief 图集重置安全变体开关（XGPU_ATLAS_RESET_SAFE=0 回退旧行为）。
 *  @note  图集 upload（TexSubImage2D）即时变形图集纹理内容，而字形 quad
 *         按设计跨字形在待定批中存续（同图集纹理）。帧中图集满触发
 *         整体重置（XGpuRenderBackend 侧 xgpu_glyph_atlas_alloc）后，
 *         重打包的新 upload 会复用待定批 quad 仍引用的槽位坐标——旧
 *         quad 冲批时采样到的是重分配后的内容，帧中即出现跨字形污染
 *         （atlas stress corrupted frame 的冲批缺口）。安全变体在每次
 *         图集 upload 前先冲批：待定 quad 全部按 upload 前的图集内容
 *         落盘，再改纹理，重置/重打包后引用彻底失效。默认开启；
 *         XGPU_ATLAS_RESET_SAFE=0 恢复旧直传行为（诊断用）。 */
static bool xgld_atlas_reset_safe(void)
{
    static int safe = -1;
    if (safe < 0)
    {
        const char* value = XSystem_environment("XGPU_ATLAS_RESET_SAFE");
        safe = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return safe != 0;
}

static bool xgld_glyph_atlas_upload(XGpuRenderDriverSession* self,
                                    const uint8_t* coverage, int width,
                                    int height, int atlasX, int atlasY)
{
    if (!xgld_ensure_current(self)) return false;
    size_t bytes;
    int y;
    if (!self || !coverage || width <= 0 || height <= 0 ||
        atlasX < 0 || atlasY < 0 ||
        atlasX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        atlasY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    if (xgld_atlas_reset_safe())
        xgld_flush_quads(self); /* 安全变体：待定批按旧图集内容先落盘。 */
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
    if (!xgld_ensure_current(self)) return false;
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
    if (!xgld_ensure_current(self)) return false;
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
    .drawImageUv = xgld_draw_image_uv,
    .drawImageRegion = xgld_draw_image_region,
    .drawGradientAlpha = xgld_draw_gradient_alpha,
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
