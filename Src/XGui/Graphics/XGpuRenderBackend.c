/******************************************************************************
 * @file       XGpuRenderBackend.c
 * @brief      XGui GPU 渲染后端通用层（可插拔驱动工厂 + 字形图集管理）。
 * @details    对齐 Qt QRhi 后端模式：本文件只做与图形 API 无关的通用
 *             逻辑——公共 API 形状、全局会话管理（请求探测/窗口会话/
 *             降级与上屏标志）、字形图集的装箱/键查找/统计，并把绘制
 *             原语转调给 `XGpuRenderDriver_procs` 返回的驱动操作表。
 *             具体图形 API（OpenGL/Vulkan）在各自的驱动实现文件中，
 *             系统头文件只允许出现在驱动实现内。驱动不可用时由
 *             XPainter 转回软件光栅，保证行为不变。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGpuRenderBackend.h"
#include "XGpuRenderDriver.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON

#include "XImage.h"
#include "XMemory.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ==================== 字形图集（阶段 3） ==================== */

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

/* ==================== 通用会话 ==================== */

struct XGpuRenderBackend
{
    int m_width;                     /**< 渲染缓冲宽度（像素）。 */
    int m_height;                    /**< 渲染缓冲高度（像素）。 */
    bool m_windowMode;               /**< 是否窗口直通会话。 */
    bool m_valid;                    /**< 驱动会话创建成功。 */
    const XGpuRenderDriverProcs* m_driver; /**< 驱动操作表（借用，注册表单例）。 */
    XGpuRenderDriverType m_driverType;     /**< 实际驱动类型（有序回退后的真值）。 */
    XGpuRenderDriverSession* m_session;    /**< 驱动会话（由驱动创建/销毁）。 */
    XImage* m_syncTarget;                  /**< SYNC 读回/上传目标（借用，XGUI_GPU_SYNC 调试模式）。 */

    XGpuGlyphAtlasEntry* m_glyphEntries;   /**< 图集条目数组（拥有）。 */
    int m_glyphEntryCount;
    int m_glyphEntryCapacity;
    int m_glyphCursorX;              /**< shelf 装箱当前行游标。 */
    int m_glyphCursorY;
    int m_glyphRowHeight;
    unsigned m_glyphUploads;         /**< 累计上传次数（含重置后重传）。 */
    unsigned m_glyphHits;            /**< 累计命中次数。 */
};

/* ==================== 驱动注册表（对齐 Qt 后端工厂） ==================== */

struct XGpuRenderDriverSession;
const XGpuRenderDriverProcs* XGpuRenderDriver_gl_procs(void);
#if defined(XINYUE_C_HAS_VULKAN)
const XGpuRenderDriverProcs* XGpuRenderDriver_vulkan_procs(void);
#endif /* XINYUE_C_HAS_VULKAN */

/**
 * @brief      取指定类型的驱动操作表（聚合各驱动实现）。
 * @details    Vulkan 骨架未编译（XINYUE_C_HAS_VULKAN 未定义）或驱动
 *             初始化失败时返回 NULL，调用方按 vulkan -> gl -> software
 *             有序回退。
 * @param      type 驱动类型。
 * @return     驱动操作表（借用）；类型未实现返回 NULL。
 */
const XGpuRenderDriverProcs* XGpuRenderDriver_procs(XGpuRenderDriverType type)
{
#if defined(XINYUE_C_HAS_VULKAN)
    if (type == XGpuRenderDriver_Vulkan)
        return XGpuRenderDriver_vulkan_procs();
#endif /* XINYUE_C_HAS_VULKAN */
    if (type == XGpuRenderDriver_OpenGL)
        return XGpuRenderDriver_gl_procs();
    return NULL;
}

/* ==================== 命令级同步读回（XGUI_GPU_SYNC 调试模式） ==================== */

/**
 * @brief      XGUI_GPU_SYNC=1 时在每命令后把渲染目标读回宿主图像。
 * @details    GPU 帧模型（帧末可见）与既有"绘制后立即断言像素"的回归
 *             用例不兼容；该钩子（挂接在全部原语 API 的必经漏斗上）让
 *             GPU 环境回归可行。默认关闭；开启时每命令一次 GPU->CPU
 *             读回，性能大幅下降，仅用于回归与调试。
 */
/* 同步读回目标由各会话实例持有（m_syncTarget），不再用全局单例——
   多 painter/多尺寸画布下全局注册会交叉污染（实测曾被 12x2 图像
   污染，导致 16x16 会话的 upload 尺寸不符失败）。 */

/**
 * @brief      XGUI_GPU_SYNC=1 时在每个原语前把宿主图像上传为渲染目标。
 * @details    与命令后的读回配对成"每命令双向同步"：帧中的 CPU 直写
 *             （XImage_setPixel 等）对后续 GPU 绘制可见，GPU 绘制对
 *             帧中读取可见。缺一侧都会破坏另一侧（读回会覆盖帧中
 *             直写；直写会被后续原语的 FBO 内容掩盖）。
 */
static void xgpu_sync_upload_if_requested(XGpuRenderBackend* self)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = getenv("XGUI_GPU_SYNC");
        requested = value && *value ? 1 : 0;
    }
    if (self && requested && self->m_syncTarget &&
        self->m_driver->uploadTargetImage)
        self->m_driver->uploadTargetImage(self->m_session,
                                          self->m_syncTarget);
}

/**
 * @brief      XGUI_GPU_SYNC=1 时把渲染目标读回宿主图像（命令级同步）。
 * @details    GPU 帧模型（帧末可见）与既有"绘制后立即断言像素"的回归
 *             用例不兼容；该钩子挂在全部原语 API 的必经漏斗上，让 GPU
 *             环境回归可行。默认关闭；开启时每命令一次 GPU->CPU 读回，
 *             性能大幅下降，仅用于回归与调试。
 */
static void xgpu_sync_readback_if_requested(XGpuRenderBackend* self)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = getenv("XGUI_GPU_SYNC");
        requested = value && *value ? 1 : 0;
    }
    if (self && requested && self->m_syncTarget)
        self->m_driver->readback(self->m_session, self->m_syncTarget);
}

/**
 * @brief      注册/清除同步读回目标（XPainter 绑定图像时调用）。
 * @param      target 目标图像（借用）；NULL 清除。
 * @return     无。
 */
void XGpuRenderBackend_setSyncTarget(XGpuRenderBackend* self, XImage* target)
{
    if (!self) return;
    self->m_syncTarget = target;
}

bool XGpuRenderBackend_uploadFrame(XGpuRenderBackend* self, XImage* target)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = getenv("XGUI_GPU_SYNC");
        requested = value && *value ? 1 : 0;
    }
    if (!XGpuRenderBackend_isValid(self) || !requested || !target)
        return true; /* 无同步需求：视为成功（调用方无需回退）。 */
    if (!self->m_driver->uploadTargetImage)
        return true; /* 驱动未实现上传：同步模式降级（帧中直写不可见）。 */
    return self->m_driver->uploadTargetImage(self->m_session, target);
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
        xgpu_text_equals(value, "vulkan") ||
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
        /* 原地 resize 优先（Vulkan swapchain 重建，避免全会话重建的
           instance/device 开销）；不支持时销毁重建（GL 现状）。 */
        if (g_xgpuWindowSession->m_driver->resize &&
            g_xgpuWindowSession->m_driver->resize(
                g_xgpuWindowSession->m_session, width, height))
        {
            g_xgpuWindowSession->m_width = width;
            g_xgpuWindowSession->m_height = height;
        }
        else
        {
            XGpuRenderBackend_destroy(g_xgpuWindowSession);
            g_xgpuWindowSession = NULL;
            g_xgpuActiveSession = NULL;
        }
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

/* ==================== 字形图集管理 ==================== */

/** @brief 饱和乘法：(a*b+127)/255，用于预乘与透明度折算。 */
static uint8_t xgpu_mul255_scalar(unsigned a, unsigned b)
{
    return (uint8_t)((a * b + 127u) / 255u);
}

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
    if (width <= 0 || height <= 0 ||
        width > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    if (self->m_glyphCursorX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE)
    {
        self->m_glyphCursorX = 0;
        self->m_glyphCursorY += self->m_glyphRowHeight;
        self->m_glyphRowHeight = 0;
    }
    if (self->m_glyphCursorY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        self->m_glyphEntryCount >= XGPU_GLYPH_ATLAS_MAX_ENTRIES)
        xgpu_glyph_atlas_reset(self, false);
    if (self->m_glyphCursorX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE)
    {
        self->m_glyphCursorX = 0;
        self->m_glyphCursorY += self->m_glyphRowHeight;
        self->m_glyphRowHeight = 0;
    }
    if (self->m_glyphCursorY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
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

/* ==================== 生命周期 ==================== */

/**
 * @brief      通用会话创建：按类型取驱动操作表并创建驱动会话。
 * @param      window 目标窗口；NULL 表示离屏会话。
 * @param      width/height 渲染缓冲尺寸（像素）。
 * @return     新通用会话；驱动不支持或创建失败返回 NULL。
 */
/**
 * @brief      解析渲染驱动类型（XGUI_RENDER_BACKEND/XGPU_BACKEND）。
 * @details    vulkan 显式请求 Vulkan；其余 gpu/opengl/1/true/on 与默认
 *             为 OpenGL（创建失败时按 vulkan -> gl -> software 有序回退）。
 */
static XGpuRenderDriverType xgpu_driver_type(void)
{
    const char* value = getenv("XGUI_RENDER_BACKEND");
    if (!value || !*value) value = getenv("XGPU_BACKEND");
    if (xgpu_text_equals(value, "vulkan"))
        return XGpuRenderDriver_Vulkan;
    return XGpuRenderDriver_OpenGL;
}

static XGpuRenderBackend* xgpu_create_ex(XWindow* window, int width,
                                         int height)
{
    XGpuRenderBackend* self;
    const XGpuRenderDriverProcs* driver;
    XGpuRenderDriverType type = xgpu_driver_type();
    if (width <= 0 || height <= 0) return NULL;
    driver = XGpuRenderDriver_procs(type);
    if (!driver && type == XGpuRenderDriver_Vulkan)
    {
        /* 显式类型未实现/未编译：回退 OpenGL。 */
        type = XGpuRenderDriver_OpenGL;
        driver = XGpuRenderDriver_procs(type);
    }
    if (!driver || !driver->available) return NULL;
    self = (XGpuRenderBackend*)XCalloc_System(1u, sizeof(*self));
    if (!self) return NULL;
    self->m_width = width;
    self->m_height = height;
    self->m_windowMode = window != NULL;
    self->m_driver = driver;
    self->m_driverType = type;
    self->m_session = driver->sessionCreate(window, width, height);
    if (!self->m_session && type == XGpuRenderDriver_Vulkan)
    {
        /* 有序回退：Vulkan 驱动创建失败 -> OpenGL -> software。 */
        driver = XGpuRenderDriver_procs(XGpuRenderDriver_OpenGL);
        if (!driver) return NULL;
        self->m_driver = driver;
        self->m_driverType = XGpuRenderDriver_OpenGL;
        self->m_session = driver->sessionCreate(window, width, height);
    }
    if (!self->m_session)
    {
        XFree_System(self);
        return NULL;
    }
    self->m_valid = true;
    return self;
}


XGpuRenderBackend* XGpuRenderBackend_create(int width, int height)
{
    return xgpu_create_ex(NULL, width, height);
}

XGpuRenderBackend* XGpuRenderBackend_createForWindow(XWindow* window,
                                                     int width, int height)
{
    return xgpu_create_ex(window, width, height);
}

void XGpuRenderBackend_destroy(XGpuRenderBackend* self)
{
    if (!self) return;
    if (self->m_driver && self->m_session)
        self->m_driver->sessionDestroy(self->m_session);
    if (self->m_glyphEntries) XFree_System(self->m_glyphEntries);
    XFree_System(self);
}

bool XGpuRenderBackend_isWindowMode(const XGpuRenderBackend* self)
{
    return self && self->m_valid && self->m_windowMode;
}

XGpuRenderDriverType XGpuRenderBackend_driverType(
        const XGpuRenderBackend* self)
{
    return self ? self->m_driverType : XGpuRenderDriver_OpenGL;
}

bool XGpuRenderBackend_isValid(const XGpuRenderBackend* self)
{
    return self && self->m_valid && self->m_driver && self->m_session;
}

int XGpuRenderBackend_width(const XGpuRenderBackend* self)
{ return XGpuRenderBackend_isValid(self) ? self->m_width : 0; }

int XGpuRenderBackend_height(const XGpuRenderBackend* self)
{ return XGpuRenderBackend_isValid(self) ? self->m_height : 0; }

/* ==================== 帧控制与原语 ==================== */

bool XGpuRenderBackend_beginFrameImage(XGpuRenderBackend* self,
                                       const XImage* initialImage)
{
    if (!XGpuRenderBackend_isValid(self)) return false;
    return self->m_driver->beginFrame(self->m_session, initialImage);
}

bool XGpuRenderBackend_beginFrame(XGpuRenderBackend* self)
{ return XGpuRenderBackend_beginFrameImage(self, NULL); }

void XGpuRenderBackend_clear(XGpuRenderBackend* self, uint32_t argb)
{
    if (!XGpuRenderBackend_isValid(self)) return;
    self->m_driver->clear(self->m_session, argb);
}

void XGpuRenderBackend_setClipRect(XGpuRenderBackend* self, const XRect* rect)
{
    if (!XGpuRenderBackend_isValid(self)) return;
    self->m_driver->setClipRect(self->m_session, rect);
}

bool XGpuRenderBackend_fillRect(XGpuRenderBackend* self, const XRect* rect,
                                uint32_t color, float opacity, bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self) || !rect || rect->width <= 0 ||
        rect->height <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    xgpu_sync_upload_if_requested(self);
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->fillRect(self->m_session, rect,
                                                    color, opacity,
                                                    sourceOver);
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_drawImage(XGpuRenderBackend* self, const XImage* image,
                                 int x, int y, int width, int height,
                                 float opacity, bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self) || !image || width <= 0 || height <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (XImage_width(image) != width || XImage_height(image) != height)
        return false;
    xgpu_sync_upload_if_requested(self);
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->drawImage(self->m_session, image,
                                                     x, y, width, height,
                                                     opacity, sourceOver);
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_drawAlphaBitmap(XGpuRenderBackend* self,
                                       const uint8_t* alpha, int width,
                                       int height, int stride, int x, int y,
                                       uint32_t color, float opacity,
                                       bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self) || !alpha || width <= 0 ||
        height <= 0 || stride < width)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    xgpu_sync_upload_if_requested(self);
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->drawAlphaBitmap(
            self->m_session, alpha, width, height, stride, x, y, color,
            opacity, sourceOver);
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_drawGlyphAlpha(XGpuRenderBackend* self,
                                      uint64_t key, int width, int height,
                                      const uint8_t* alpha, int stride,
                                      int x, int y, uint32_t color,
                                      float opacity, bool sourceOver)
{
    const XGpuGlyphAtlasEntry* entry;
    xgpu_sync_upload_if_requested(self);
    uint32_t premulColor;
    unsigned colorA;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    /* 覆盖图必须紧凑（stride == width）：图集子矩形上传按紧凑布局。
       painter 侧两条字形路径的缓冲均为自分配紧凑布局，恒满足。 */
    if (!XGpuRenderBackend_isValid(self) || width <= 0 ||
        height <= 0 || stride != width || (!alpha && !xgpu_glyph_atlas_find(
            self, key, width, height)))
        return false;
    entry = xgpu_glyph_atlas_find(self, key, width, height);
    /* alpha 非空 = 调用方提供了新鲜覆盖图数据（含非 AA 二值钳位）。
       命中图集缓存时仍需重新上传——图集可能存有旧的未钳位内容
       （实测铁证：非 AA 边缘 0x1b 残留导致 "disabling text
       antialiasing restores hard edge" 失败）。 */
    if (entry && alpha)
    {
        self->m_driver->glyphAtlasUpload(self->m_session, alpha,
                                          width, height, entry->m_x,
                                          entry->m_y);
    }
    if (!entry)
    {
        int atlasX = 0;
        int atlasY = 0;
        /* 超出图集能力的字形（大于图集或分配失败）回退逐字形上传路径。 */
        if (!xgpu_glyph_atlas_alloc(self, width, height, &atlasX, &atlasY))
            return XGpuRenderBackend_drawAlphaBitmap(
                self, alpha, width, height, stride, x, y, color, opacity,
                sourceOver);
        if (!self->m_driver->glyphAtlasUpload(self->m_session, alpha,
                                              width, height, atlasX, atlasY))
            return false;
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
        {
            XGpuGlyphAtlasEntry* stored = &self->m_glyphEntries[self->m_glyphEntryCount];
            stored->m_key = key;
            stored->m_width = width;
            stored->m_height = height;
            stored->m_x = atlasX;
            stored->m_y = atlasY;
            ++self->m_glyphEntryCount;
            entry = stored;
        }
        ++self->m_glyphUploads;
    }
    else
        ++self->m_glyphHits;
    /* 绘制期预乘：modulate 乘法把透明度折入预乘颜色的 alpha。 */
    colorA = (unsigned)((color >> 24) & 0xffu);
    colorA = (unsigned)(colorA * (unsigned)(opacity * 255.0f + 0.5f) +
                        127u) / 255u;
    premulColor = ((uint32_t)colorA << 24) |
                  ((uint32_t)xgpu_mul255_scalar((color >> 16) & 0xffu,
                                                colorA) << 16) |
                  ((uint32_t)xgpu_mul255_scalar((color >> 8) & 0xffu,
                                                colorA) << 8) |
                  (uint32_t)xgpu_mul255_scalar(color & 0xffu, colorA);
    xgpu_sync_upload_if_requested(self);
    xgpu_sync_upload_if_requested(self);
    {
        bool drawOk = self->m_driver->glyphAtlasDraw(
            self->m_session, entry->m_x, entry->m_y, width, height,
            x, y, premulColor, sourceOver);
        xgpu_sync_readback_if_requested(self);
        return drawOk;
    }
}

bool XGpuRenderBackend_drawSolidQuad(XGpuRenderBackend* self, float x1,
                                     float y1, float x2, float y2, float x3,
                                     float y3, float x4, float y4,
                                     uint32_t premulColor, bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self)) return false;
    xgpu_sync_upload_if_requested(self);
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->drawSolidQuad(
            self->m_session, x1, y1, x2, y2, x3, y3, x4, y4, premulColor,
            sourceOver);
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_readback(XGpuRenderBackend* self, XImage* target)
{
    if (!XGpuRenderBackend_isValid(self) || !target ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    return self->m_driver->readback(self->m_session, target);
}

void XGpuRenderBackend_endFrame(XGpuRenderBackend* self)
{
    if (!XGpuRenderBackend_isValid(self)) return;
    self->m_driver->endFrame(self->m_session);
}

/* ==================== 窗口直通上屏（阶段 2） ==================== */

bool XGpuRenderBackend_presentToWindow(XGpuRenderBackend* self)
{
    if (!XGpuRenderBackend_isValid(self) || !self->m_windowMode)
        return false;
    return self->m_driver->presentToWindow(self->m_session);
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
