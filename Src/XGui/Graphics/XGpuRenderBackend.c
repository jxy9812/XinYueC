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
#include "XSystem.h"

#include "XAlgorithm.h"
#include "XGpuRenderBackend.h"
#include "XGpuRenderDriver.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON

#include "XImage.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XThread.h"
#include <limits.h>

/* ==================== 帧级诊断埋点（XGPU_PROF=1 启用） ==================== */
/* 直通重构量化工具：按 5s 窗口聚合 readback/drawImage/present 次数与
   均耗，并按原语类（fillRect/solidQuad/glyphQuad）聚合「backend 包装
   层」CPU 侧差分耗时（验证/证伪逐原语提交成本假设；批内合并的
   glUniform/glDrawArrays 发生在驱动侧帧末冲批，不计入此类），另一次性
   打印实际驱动类型。远端 RX 6800 XT 与本机同口径。 */

/** @brief XGPU_PROF 环境开关（进程内缓存）。 */
static bool xgpu_prof_requested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = XSystem_environment("XGPU_PROF");
        requested = value && *value ? 1 : 0;
    }
    return requested != 0;
}

/**
 * @brief XGUI_GPU_SYNC 环境开关（与 XPainter 侧同口径："0"=关）。
 * @note  此前后端三处钩子只查非空，"0" 被判为开——与 painter 侧
 *        painterGpuSyncRequested 的 "0"=关 语义相反（2026-09-24 复核
 *        确认高危：设 0 反而每原语整帧上传+读回）。统一为 "0"=关。
 */
static bool xgpu_sync_requested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = XSystem_environment("XGUI_GPU_SYNC");
        requested = value && *value &&
                            !(value[0] == '0' && value[1] == 0)
                        ? 1 : 0;
    }
    return requested != 0;
}

/** @brief 逐段计数器（µs 累计 + 次数）。 */
static struct XGpuProf
{
    uint64_t m_readbackUs;   /**< readback 累计耗时。 */
    uint32_t m_readbackCount; /**< readback 次数（批量后应≈帧数）。 */
    uint64_t m_drawImageUs;  /**< drawImage 累计耗时。 */
    uint32_t m_drawImageCount; /**< drawImage 次数（含原语与批量提交）。 */
    uint32_t m_fillRectCount; /**< fillRect 原语次数（逐行填充诊断）。 */
    uint32_t m_solidQuadCount; /**< solidQuad 原语次数（虚线段诊断）。 */
    uint64_t m_fillRectUs;   /**< fillRect 包装层累计耗时（验证/证伪
                                  「逐原语提交成本」假设：backend 校验层
                                  +proc 间接的 CPU 侧开销，含批入口）。 */
    uint64_t m_solidQuadUs;  /**< solidQuad 包装层累计耗时（同上口径）。 */
    uint64_t m_glyphQuadUs;  /**< glyphQuad（图集绘制路径）包装层累计
                                  耗时（同上口径；图集上传/回退路径
                                  不计入，避免口径混叠）。 */
    uint32_t m_glyphQuadCount; /**< glyphQuad 次数（图集绘制路径）。 */
    uint64_t m_presentUs;    /**< presentToWindow 累计耗时。 */
    uint32_t m_presentCount; /**< present 次数。 */
    uint32_t m_frameCount;   /**< beginFrame 次数。 */
    uint64_t m_windowStartUs; /**< 窗口起点（5s 聚合）。 */
    bool m_driverPrinted;    /**< 驱动类型已打印。 */
} g_xgpuProf;

/** @brief 当前 µs 时钟。 */
static uint64_t xgpu_prof_now_us(void)
{
    return (uint64_t)(XDateTime_currentNSecsSinceEpoch() / 1000);
}

/** @brief beginFrame 时驱动类型一次性打印与 5s 窗口聚合输出。
 *  @note  本节位于会话结构定义之前，windowMode 由调用方传入。 */
static void xgpu_prof_frame_tick(const XGpuRenderBackend* self,
                                 bool windowMode)
{
    if (!xgpu_prof_requested()) return;
    if (!g_xgpuProf.m_driverPrinted)
    {
        /* 对齐 XGpuRenderDriverType 枚举序（OpenGL=0, Vulkan=1）。 */
        static const char* const names[] = { "opengl", "vulkan" };
        int type = (int)XGpuRenderBackend_driverType(self);
        fprintf(stderr, "[xgpu-prof] driver=%s window=%d\n",
                (type >= 0 && type <= 1) ? names[type] : "?",
                (int)windowMode);
        g_xgpuProf.m_driverPrinted = true;
        g_xgpuProf.m_windowStartUs = xgpu_prof_now_us();
    }
    ++g_xgpuProf.m_frameCount;
    {
        uint64_t now = xgpu_prof_now_us();
        if (now - g_xgpuProf.m_windowStartUs >= 5000000u)
        {
            double secs = (double)(now - g_xgpuProf.m_windowStartUs) /
                          1e6;
            fprintf(stderr,
                    "[xgpu-prof] %.1fs frames=%u readback=%u (%.3fms/次) "
                    "drawImage=%u (%.3fms/次) fillRect=%u (%.4fms/次) "
                    "solidQuad=%u (%.4fms/次) glyphQuad=%u (%.4fms/次) "
                    "present=%u (%.3fms/次)\n",
                    secs, g_xgpuProf.m_frameCount,
                    g_xgpuProf.m_readbackCount,
                    g_xgpuProf.m_readbackCount
                        ? (double)g_xgpuProf.m_readbackUs /
                              (double)g_xgpuProf.m_readbackCount / 1000.0
                        : 0.0,
                    g_xgpuProf.m_drawImageCount,
                    g_xgpuProf.m_drawImageCount
                        ? (double)g_xgpuProf.m_drawImageUs /
                              (double)g_xgpuProf.m_drawImageCount / 1000.0
                        : 0.0,
                    g_xgpuProf.m_fillRectCount,
                    g_xgpuProf.m_fillRectCount
                        ? (double)g_xgpuProf.m_fillRectUs /
                              (double)g_xgpuProf.m_fillRectCount / 1000.0
                        : 0.0,
                    g_xgpuProf.m_solidQuadCount,
                    g_xgpuProf.m_solidQuadCount
                        ? (double)g_xgpuProf.m_solidQuadUs /
                              (double)g_xgpuProf.m_solidQuadCount / 1000.0
                        : 0.0,
                    g_xgpuProf.m_glyphQuadCount,
                    g_xgpuProf.m_glyphQuadCount
                        ? (double)g_xgpuProf.m_glyphQuadUs /
                              (double)g_xgpuProf.m_glyphQuadCount / 1000.0
                        : 0.0,
                    g_xgpuProf.m_presentCount,
                    g_xgpuProf.m_presentCount
                        ? (double)g_xgpuProf.m_presentUs /
                              (double)g_xgpuProf.m_presentCount / 1000.0
                        : 0.0);
            g_xgpuProf.m_readbackUs = 0;
            g_xgpuProf.m_readbackCount = 0;
            g_xgpuProf.m_drawImageUs = 0;
            g_xgpuProf.m_drawImageCount = 0;
            g_xgpuProf.m_fillRectCount = 0;
            g_xgpuProf.m_solidQuadCount = 0;
            g_xgpuProf.m_fillRectUs = 0;
            g_xgpuProf.m_solidQuadUs = 0;
            g_xgpuProf.m_glyphQuadUs = 0;
            g_xgpuProf.m_glyphQuadCount = 0;
            g_xgpuProf.m_presentUs = 0;
            g_xgpuProf.m_presentCount = 0;
            g_xgpuProf.m_frameCount = 0;
            g_xgpuProf.m_windowStartUs = now;
        }
    }
}

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
/* 脏区读回直连入口（定义于 XGpuRenderDriver_gl.c）：XGpuRenderDriverProcs
 * 结构体在共享接口头 XGpuRenderDriver.h 中（通用层不可扩展），readbackRect
 * 过渡期以外部链接函数跨编译单元直调；仅当活动驱动表为本 GL 表（下方
 * 包装做指针身份比对）时允许调用，其余驱动走整帧 readback 回退。 */
bool XGpuRenderDriver_gl_readbackRect(XGpuRenderDriverSession* self,
                                      XImage* target, int x, int y,
                                      int width, int height);
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
    if (self && xgpu_sync_requested() && self->m_syncTarget &&
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
    if (self && xgpu_sync_requested() && self->m_syncTarget)
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
    if (!XGpuRenderBackend_isValid(self) || !xgpu_sync_requested() || !target)
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
static int g_xgpuRequestedOverride = -1;                /**< 运行期覆盖（addRequestedOverride）；-1 未设置。 */

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

/* ==================== 会话创建瞬时失败重试（GPU 冷启动攻坚） ==================== */
/* 现象（第一夜 B 路）：GPU 进程被 taskkill /f 强杀后，短时间内新实例的
   GL 会话创建可能瞬时失败（驱动侧 wgl 资源收尾完成前），数分钟自愈。
   旧口径下任一创建路径一次失败即进程级永久缓存（探测缓存/窗口失败缓存/
   XPainter 侧 g_xgpuRenderProbeFailed），瞬时失败被放大成永久软件回退。
   新口径：小延迟内联重试一次吸收亚百毫秒级瞬态 + 冷却重探，累计失败
   有总次数上限（真坏环境有界退避，不死循环不逐帧拖垮帧率）。 */

/** @brief 内联重试前的休眠毫秒数（强杀后驱动收尾通常亚百毫秒级）。 */
#define XGPU_SESSION_RETRY_DELAY_MS 50u

/** @brief 窗口会话失败后的冷却期毫秒数（期间按旧口径立即回退，不逐帧重试）。 */
#define XGPU_WINDOW_RETRY_COOLDOWN_MS 500u

/** @brief 窗口会话累计失败上限：达到后回到旧口径永久缓存（进程生命周期）。 */
#define XGPU_WINDOW_RETRY_MAX_FAILURES 3

/** @brief 探测累计失败上限：达到后失败永久缓存（真坏环境有界）。 */
#define XGPU_PROBE_MAX_FAILURES 3

/** @brief XGPU_SESSION_RETRY 环境开关（进程内缓存；仅 "0"=关，默认开）。 */
static bool xgpu_session_retry_requested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = XSystem_environment("XGPU_SESSION_RETRY");
        requested = (value && *value &&
                            value[0] == '0' && value[1] == 0)
                        ? 0 : 1;
    }
    return requested != 0;
}

/** @brief 当前 µs 时钟（冷却计时用；与 XGPU_PROF 聚合同源）。 */
static uint64_t xgpu_session_now_us(void)
{
    return (uint64_t)(XDateTime_currentNSecsSinceEpoch() / 1000);
}

/** @brief 窗口会话累计失败次数与最近失败时刻（冷却/预算用）。 */
static int g_xgpuWindowFailCount = 0;
static uint64_t g_xgpuWindowFailNs = 0;

/**
 * @brief      窗口会话失败缓存是否放行本次创建尝试。
 * @details    返回 false 时调用方按旧口径立即得到 NULL（软件回退帧）；
 *             返回 true 时暂时解除失败缓存放行一次重探——成败都会在
 *             acquireForWindow 侧更新缓存与计数。XGPU_SESSION_RETRY=0
 *             时本门恒不放行（与旧的一次性永久缓存完全一致）。
 * @return     true 允许继续尝试创建；false 本调用短路返回 NULL。
 */
static bool xgpu_window_retry_gate(void)
{
    if (!g_xgpuWindowProbeFailed) return true;
    if (!xgpu_session_retry_requested()) return false;
    if (g_xgpuWindowFailCount >= XGPU_WINDOW_RETRY_MAX_FAILURES) return false;
    if (xgpu_session_now_us() - g_xgpuWindowFailNs <
        (uint64_t)XGPU_WINDOW_RETRY_COOLDOWN_MS * 1000u)
        return false;
    g_xgpuWindowProbeFailed = false; /* 冷却已过且预算未耗尽：放行重探。 */
    return true;
}

bool XGpuRenderBackend_requested(void)
{
    const char* value;
    if (g_xgpuRequested >= 0) return g_xgpuRequested != 0;
    /* 运行期覆盖（自动探测宿主调用 addRequestedOverride）优先于环境变量。 */
    if (g_xgpuRequestedOverride >= 0)
    {
        g_xgpuRequested = g_xgpuRequestedOverride;
        return g_xgpuRequested != 0;
    }
    value = XSystem_environment("XGUI_RENDER_BACKEND");
    if (!value || !*value) value = XSystem_environment("XGPU_BACKEND");
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

/* 最近一次会话创建的驱动类型记录：current() 只对窗口会话非 NULL，
   离屏会话下 driverType(query) 恒回 openGL 默认值——诊断输出会误导
   （"离屏也显示 opengl"假象，2026-09-24 排查 GL/Vulkan 会话归属时
   踩坑）。本记录在任何会话创建成功后更新，供诊断取真实值。 */
static XGpuRenderDriverType g_xgpuLastDriverType = XGpuRenderDriver_OpenGL;
static bool g_xgpuLastDriverTypeValid = false;

/** @brief 最近创建会话的实际驱动类型（含离屏会话；无会话时返回
 *         OpenGL 默认并置 *outValid=false）。 */
XGpuRenderDriverType XGpuRenderBackend_lastDriverType(bool* outValid)
{
    if (outValid) *outValid = g_xgpuLastDriverTypeValid;
    return g_xgpuLastDriverType;
}

static void xgpu_window_session_destroy_at_exit(void)
{
    XGpuRenderBackend_shutdown();
}

XGpuRenderBackend* XGpuRenderBackend_acquireForWindow(XWindow* window,
                                                      int width, int height)
{
    if (!window || width <= 0 || height <= 0) return NULL;
    if (!xgpu_window_retry_gate()) return NULL;
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
            /* 窗口 GL 不可用：保持软件/阶段 1。失败仍缓存，但新口径下带
               冷却与总次数预算（见 xgpu_window_retry_gate）——强杀残留
               类瞬时失败可在数百毫秒后自愈重探，而非进程级永久回退。 */
            g_xgpuWindowProbeFailed = true;
            ++g_xgpuWindowFailCount;
            g_xgpuWindowFailNs = xgpu_session_now_us();
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
    /* 失败预算一并清零：进程退出/应用析构后再重建（嵌入式的应用重启
       流程）拿满新预算，与「缓存清空」的既有语义一致。 */
    g_xgpuWindowFailCount = 0;
    g_xgpuWindowFailNs = 0;
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

/** @brief XGPU_ATLAS_RESET_SAFE 环境开关（"0"=关，默认开）。
 *  @note  与驱动侧同名的安全变体配对：开启时命中重传失败不再沿用旧
 *         图集内容绘制（该槽位可能已被帧中重置重打包复用），回退
 *         drawAlphaBitmap 逐字形路径，保证"重置后旧 glyph 引用不悬空"。 */
static bool xgpu_atlas_reset_safe_requested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = XSystem_environment("XGPU_ATLAS_RESET_SAFE");
        requested = value && *value &&
                            !(value[0] == '0' && value[1] == 0)
                        ? 1 : 0;
    }
    return requested != 0;
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
    const char* value = XSystem_environment("XGUI_RENDER_BACKEND");
    if (!value || !*value) value = XSystem_environment("XGPU_BACKEND");
    if (xgpu_text_equals(value, "vulkan"))
        return XGpuRenderDriver_Vulkan;
    return XGpuRenderDriver_OpenGL;
}

/**
 * @brief      驱动会话创建（瞬时失败一次性内联重试）。
 * @details    GPU 冷启动攻坚：强杀残留场景下驱动收尾通常亚百毫秒级，
 *             首次 sessionCreate 可能瞬时失败——休眠一小段后重试一次，
 *             避免把瞬态放大为调用方的永久回退（探测/窗口缓存）。
 *             XGPU_SESSION_RETRY=0 时与旧口径逐字节一致（单次调用）。
 *             成功路径零额外开销（只在失败分支休眠/打印）。
 * @return     驱动会话；重试后仍失败返回 NULL（调用方按原失败路径处理）。
 */
static XGpuRenderDriverSession* xgpu_session_create_with_retry(
    const XGpuRenderDriverProcs* driver, XWindow* window, int width,
    int height)
{
    XGpuRenderDriverSession* session = driver->sessionCreate(window, width,
                                                             height);
    if (!session && xgpu_session_retry_requested())
    {
        XThread_msleep(XGPU_SESSION_RETRY_DELAY_MS);
        session = driver->sessionCreate(window, width, height);
        /* 罕见异常路径的常驻一行诊断（不受 XGPU_PROF 门控）：定位
           「瞬态失败 vs 真坏环境」必须留下证据。 */
        fprintf(stderr,
                "[xgpu] session retry: %s (window=%p %dx%d)\n",
                session ? "recovered on 2nd attempt" : "failed twice",
                (void*)window, width, height);
    }
    return session;
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
    self->m_session = xgpu_session_create_with_retry(driver, window, width,
                                                     height);
    if (!self->m_session && type == XGpuRenderDriver_Vulkan)
    {
        /* 有序回退：Vulkan 驱动创建失败 -> OpenGL -> software。 */
        driver = XGpuRenderDriver_procs(XGpuRenderDriver_OpenGL);
        if (!driver) return NULL;
        self->m_driver = driver;
        self->m_driverType = XGpuRenderDriver_OpenGL;
        self->m_session = xgpu_session_create_with_retry(driver, window,
                                                         width, height);
    }
    if (!self->m_session)
    {
        XFree_System(self);
        return NULL;
    }
    self->m_valid = true;
    g_xgpuLastDriverType = self->m_driverType;
    g_xgpuLastDriverTypeValid = true;
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

void XGpuRenderBackend_addRequestedOverride(bool on)
{
    /* -1→0/1 的覆盖写入即生效（requested 下次调用读覆盖值）。 */
    g_xgpuRequestedOverride = on ? 1 : 0;
    g_xgpuRequested = g_xgpuRequestedOverride;
}

bool XGpuRenderBackend_probeAvailable(void)
{
    /* 进程内缓存：探测有一次性成本（离屏 1x1 窗口 + GL 上下文创建）。 */
    static int probed = -1;   /**< -1 未探测；0/1 已定论（0=永久不可用）。 */
    static int failCount = 0; /**< 累计失败次数（瞬时失败预算，见下）。 */
    bool available;
    if (probed >= 0) return probed != 0;
    {
        /* 1x1 离屏会话试创建：sessionCreate 内部完成 makeCurrent + 核心
           GL 函数加载 + FBO/纹理完整性校验（xgld_initialize），任一步
           失败即判定本机无可用 OpenGL——探测会话随即销毁，不留资源。
           成功后销毁探测会话；正式渲染会话由宿主流程另行创建。
           （create 内部在 XGPU_SESSION_RETRY 口径下自带一次内联重试，
           已经吸收亚百毫秒级瞬态。） */
        XGpuRenderBackend* probe = XGpuRenderBackend_create(1, 1);
        available = XGpuRenderBackend_isValid(probe);
        if (probe) XGpuRenderBackend_destroy(probe);
    }
    if (available)
    {
        probed = 1;
    }
    else
    {
        ++failCount;
        /* 冷启动攻坚：强杀残留短窗内的探测失败是瞬时现象，预算内不永久
           写死——下次调用重探；预算耗尽（XGPU_PROBE_MAX_FAILURES）或
           XGPU_SESSION_RETRY=0 时按旧口径永久缓存失败（真坏环境有界，
           不会死循环烧探测成本）。 */
        if (!(xgpu_session_retry_requested() &&
              failCount < XGPU_PROBE_MAX_FAILURES))
            probed = 0;
    }
    return available;
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
    xgpu_prof_frame_tick(self, self->m_windowMode);
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
    {
        uint64_t profT0 = xgpu_prof_requested() ? xgpu_prof_now_us() : 0;
        bool primitiveOk = self->m_driver->fillRect(self->m_session, rect,
                                                    color, opacity,
                                                    sourceOver);
        if (xgpu_prof_requested())
            g_xgpuProf.m_fillRectUs += xgpu_prof_now_us() - profT0;
        ++g_xgpuProf.m_fillRectCount;
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
    {
        uint64_t profT0 = xgpu_prof_requested()
                              ? xgpu_prof_now_us()
                              : 0;
        bool primitiveOk = self->m_driver->drawImage(self->m_session, image,
                                                     x, y, width, height,
                                                     opacity, sourceOver);
        if (xgpu_prof_requested())
        {
            g_xgpuProf.m_drawImageUs += xgpu_prof_now_us() - profT0;
            ++g_xgpuProf.m_drawImageCount;
        }
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_drawImageUv(XGpuRenderBackend* self,
                                   const XImage* image, int x, int y,
                                   int width, int height, float u0, float v0,
                                   float u1, float v1, float opacity,
                                   bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self) || !image || width <= 0 ||
        height <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (XImage_width(image) <= 0 || XImage_height(image) <= 0)
        return false;
    if (!self->m_driver->drawImageUv)
        return false; /* 驱动未实现：调用方回退软件路径。 */
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->drawImageUv(
            self->m_session, image, x, y, width, height, u0, v0, u1, v1,
            opacity, sourceOver);
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_drawImageRegion(XGpuRenderBackend* self,
                                       const XImage* image, int srcX,
                                       int srcY, int srcW, int srcH,
                                       int dstX, int dstY, float opacity,
                                       bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self) || !image || srcW <= 0 ||
        srcH <= 0)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (srcX < 0 || srcY < 0 || srcX + srcW > XImage_width(image) ||
        srcY + srcH > XImage_height(image))
        return false;
    if (!self->m_driver->drawImageRegion)
        return false; /* 驱动未实现：调用方回退整幅路径。 */
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->drawImageRegion(
            self->m_session, image, srcX, srcY, srcW, srcH, dstX, dstY,
            opacity, sourceOver);
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_drawGradientAlpha(XGpuRenderBackend* self,
                                         const unsigned char* coverage,
                                         int width, int height, int x, int y,
                                         const unsigned char* lutPremul,
                                         int lutAxis, float opacity,
                                         bool sourceOver)
{
    if (!XGpuRenderBackend_isValid(self) || !coverage || width <= 0 ||
        height <= 0 || !lutPremul)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    if (!self->m_driver->drawGradientAlpha)
        return false; /* 驱动未实现：调用方回退软件路径。 */
    xgpu_sync_upload_if_requested(self);
    {
        bool primitiveOk = self->m_driver->drawGradientAlpha(
            self->m_session, coverage, width, height, x, y, lutPremul,
            lutAxis, opacity, sourceOver);
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
    uint32_t premulColor;
    unsigned colorA;
    xgpu_sync_upload_if_requested(self);
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
        if (!self->m_driver->glyphAtlasUpload(self->m_session, alpha,
                                              width, height, entry->m_x,
                                              entry->m_y))
        {
            /* 命中重传失败：安全变体下不画旧图集内容（槽位可能已被
               重置重打包复用），回退逐字形上传路径；命中计数保持。 */
            ++self->m_glyphHits;
            if (xgpu_atlas_reset_safe_requested())
                return XGpuRenderBackend_drawAlphaBitmap(
                    self, alpha, width, height, stride, x, y, color,
                    opacity, sourceOver);
        }
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
    {
        uint64_t profT0 = xgpu_prof_requested() ? xgpu_prof_now_us() : 0;
        bool drawOk = self->m_driver->glyphAtlasDraw(
            self->m_session, entry->m_x, entry->m_y, width, height,
            x, y, premulColor, sourceOver);
        if (xgpu_prof_requested())
            g_xgpuProf.m_glyphQuadUs += xgpu_prof_now_us() - profT0;
        ++g_xgpuProf.m_glyphQuadCount;
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
    {
        uint64_t profT0 = xgpu_prof_requested() ? xgpu_prof_now_us() : 0;
        bool primitiveOk = self->m_driver->drawSolidQuad(
            self->m_session, x1, y1, x2, y2, x3, y3, x4, y4, premulColor,
            sourceOver);
        if (xgpu_prof_requested())
            g_xgpuProf.m_solidQuadUs += xgpu_prof_now_us() - profT0;
        ++g_xgpuProf.m_solidQuadCount;
        xgpu_sync_readback_if_requested(self);
        return primitiveOk;
    }
}

bool XGpuRenderBackend_readback(XGpuRenderBackend* self, XImage* target)
{
    uint64_t profT0;
    if (!XGpuRenderBackend_isValid(self) || !target ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    profT0 = xgpu_prof_requested() ? xgpu_prof_now_us() : 0;
    {
        bool ok = self->m_driver->readback(self->m_session, target);
        if (xgpu_prof_requested())
        {
            g_xgpuProf.m_readbackUs += xgpu_prof_now_us() - profT0;
            ++g_xgpuProf.m_readbackCount;
        }
        return ok;
    }
}

bool XGpuRenderBackend_readbackRect(XGpuRenderBackend* self, XImage* target,
                                    int x, int y, int width, int height)
{
    uint64_t profT0;
    bool ok;
    if (!XGpuRenderBackend_isValid(self) || !target ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    /* 越界钳位（驱动侧双重防护）：负/超界部分裁剪到会话范围内；钳位后
       为空 = 无事可做，按成功返回（调用方不做整帧回退）。 */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (width > self->m_width - x) width = self->m_width - x;
    if (height > self->m_height - y) height = self->m_height - y;
    if (width <= 0 || height <= 0) return true;
    /* 活动驱动非 GL 表（回退 vulkan/未来其它驱动）无直连入口：返回
       false 让调用方回退整帧 readback，语义等价旧路径。 */
    if (self->m_driver != XGpuRenderDriver_procs(XGpuRenderDriver_OpenGL))
        return false;
    profT0 = xgpu_prof_requested() ? xgpu_prof_now_us() : 0;
    ok = XGpuRenderDriver_gl_readbackRect(self->m_session, target,
                                          x, y, width, height);
    if (xgpu_prof_requested())
    {
        /* 计入与 readback 同一累加器：[xgpu-prof] 的 readback 均值
           反映的就是呈现链真实读回成本（新旧通道同口径可比）。 */
        g_xgpuProf.m_readbackUs += xgpu_prof_now_us() - profT0;
        ++g_xgpuProf.m_readbackCount;
    }
    return ok;
}

void XGpuRenderBackend_endFrame(XGpuRenderBackend* self)
{
    if (!XGpuRenderBackend_isValid(self)) return;
    self->m_driver->endFrame(self->m_session);
}

/* ==================== 窗口直通上屏（阶段 2） ==================== */

bool XGpuRenderBackend_presentToWindow(XGpuRenderBackend* self)
{
    uint64_t profT0;
    bool ok;
    if (!XGpuRenderBackend_isValid(self) || !self->m_windowMode)
        return false;
    profT0 = xgpu_prof_requested() ? xgpu_prof_now_us() : 0;
    ok = self->m_driver->presentToWindow(self->m_session);
    if (xgpu_prof_requested())
    {
        g_xgpuProf.m_presentUs += xgpu_prof_now_us() - profT0;
        ++g_xgpuProf.m_presentCount;
    }
    return ok;
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */
