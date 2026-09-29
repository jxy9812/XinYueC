/******************************************************************************
 * @file       XGpuRenderDriver_vulkan.c
 * @brief      XGui GPU 渲染驱动——Vulkan 实现。
 * @details    实现 XGpuRenderDriver.h 的驱动操作表：离屏会话（渲染目标
 *             VkImage + framebuffer + renderPass + solid/texture 两个
 *             pipeline）与窗口会话（VK_KHR_xlib_surface + swapchain，
 *             每交换链图像一个 framebuffer）。执行模型：帧内绘制原语
 *             按调用顺序录制进单个 primary 命令缓冲，顶点数据写入
 *             HOST_VISIBLE|COHERENT 顶点缓冲游标（vkCmdDraw 以
 *             firstVertex 偏移绘制），endFrame 提交并等待 fence 后执行
 *             挂起的 readback 拷贝。XGPU_VK_PRESENT_V2（默认开，置 0
 *             回退）：endFrame 提交即返回（fence 移交下一帧 beginFrame
 *             批量回收），transfer/半帧打断以独立 fence 取代
 *             vkQueueWaitIdle 全队列同步；GPU 侧依赖由同队列提交序
 *             保证，staging/命令缓冲复用前经 retire 查询回收。
 *             Vulkan 图像行序为上到下，与 XImage
 *             一致，readback 无需行翻转。本文件仅含跨平台 Vulkan 核心
 *             头（vulkan_core.h）；窗口平台 surface（X11 Xlib / Win32）
 *             的系统 API 实现位于 Drive（XPlatformGraphicsDriver_*）。
 * @note       仅在 XPLATFORMINTEGRATION_ON && XGPU_ON && XINYUE_C_HAS_VULKAN
 *             时编译；窗口会话 surface 扩展由 Drive 平台实现提供，离屏
 *             会话无扩展依赖。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGpuRenderDriver.h"

#include "XAlgorithm.h"
#if XPLATFORMINTEGRATION_ON && XGPU_ON && defined(XINYUE_C_HAS_VULKAN)

#include "XImage.h"
#include "XMemory.h"
#include "XSystem.h"
#include "XDateTime.h"
#include "XPlatformGraphics.h"
#include "XWindow.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
/* 仅包含跨平台 Vulkan SDK 核心头；窗口表面（X11 Xlib / Win32）的系统
   API 实现位于 Drive（XPlatformGraphicsDriver_createVulkanWindowSurface），
   本文件不含任何平台窗口系统头。 */
#include <vulkan/vulkan.h>

#include "XGpuRenderDriver_vulkan_shaders.h"

/* ==================== 帧级同步阶段计时（XGPU_VK_STAGE_PROF=1 启用） ==================== */

/*
 * 归因工具：只在提交与同步点打点（swapchain acquire、vkQueueSubmit、
 * fence 等待、vkQueuePresentKHR、读回 mapped 拷出、waitIdle 类），
 * 不逐绘制原语。每阶段累计总耗时/调用次数/单次最大值（尖刺归因），
 * 2s 窗口聚合输出 stderr（前缀 [xvkl-stage]，对齐 XGPU_PROF 口径），
 * 会话销毁时输出末窗累计（END 标记）；atexit 兜底补打（正常退出但
 * 未走销毁路径时），保证短运行（<窗口时长）也能拿到一份实测。诊断
 * 开关默认关；环境变量置
 * 非 "0" 即开（"0" 显式关）。时钟与 GL 驱动/XGPU_PROF 同源
 * （XDateTime_currentNSecsSinceEpoch，纳秒）。
 */

/** @brief 计时阶段枚举（聚合表下标）。 */
typedef enum XvklStageId
{
    XvklStage_Acquire = 0,       /**< vkAcquireNextImageKHR（FIFO 背压可见）。 */
    XvklStage_FrameFenceWait,    /**< 帧提交 fence 等待（begin 回收/end V1）。 */
    XvklStage_SuspendFenceWait,  /**< 半帧打断 fence 等待（含兜底回收）。 */
    XvklStage_TransferFenceWait, /**< transfer fence 回收等待（retire）。 */
    XvklStage_ReadbackFenceWait, /**< 读回 fence 等待（槽回收+合并等待）。 */
    XvklStage_SubmitFrame,       /**< endFrame vkQueueSubmit。 */
    XvklStage_SubmitSuspend,     /**< 半帧打断 vkQueueSubmit。 */
    XvklStage_SubmitTransfer,    /**< transfer/读回拷贝 vkQueueSubmit。 */
    XvklStage_Present,           /**< vkQueuePresentKHR。 */
    XvklStage_QueueWaitIdle,     /**< vkQueueWaitIdle（V1 路径）。 */
    XvklStage_DeviceWaitIdle,    /**< vkDeviceWaitIdle（销毁等罕见点）。 */
    XvklStage_ReadbackCopyout,   /**< 读回 staging mapped CPU 拷出。 */
    XvklStage_Count
} XvklStageId;

/** @brief 阶段聚合表（进程级单例；跨会话累计，销毁时窗口收口）。 */
static struct XvklStageProf
{
    uint64_t m_ns[XvklStage_Count];    /**< 各阶段累计耗时。 */
    uint64_t m_maxNs[XvklStage_Count]; /**< 各阶段单次最大耗时。 */
    uint32_t m_count[XvklStage_Count]; /**< 各阶段调用次数。 */
    uint64_t m_windowStartNs;          /**< 当前聚合窗口起点。 */
    uint32_t m_frames;                 /**< 当前窗口 beginFrame 次数。 */
} g_xvklStageProf;

static const char* const g_xvklStageNames[XvklStage_Count] =
{
    "acquire", "frameFenceWait", "suspendFenceWait", "transferFenceWait",
    "readbackFenceWait", "submitFrame", "submitSuspend", "submitTransfer",
    "present", "queueWaitIdle", "deviceWaitIdle", "readbackCopyout"
};

static void xvkl_stage_prof_atexit_report(void);
static bool xvkl_begin_prof_on(void);
static void xvkl_begin_prof_report(void);
static bool xvkl_obj_prof_on(void);
static void xvkl_obj_prof_report(void);

/** @brief XGPU_VK_STAGE_PROF 开关（"0"=显式关；其余非空=开；缓存）。 */
static bool xvkl_stage_prof_on(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_STAGE_PROF");
        cached = v && *v && !(v[0] == '0' && v[1] == 0) ? 1 : 0;
        if (cached)
        {
            g_xvklStageProf.m_windowStartNs =
                XDateTime_currentNSecsSinceEpoch();
            atexit(xvkl_stage_prof_atexit_report);
        }
    }
    return cached != 0;
}

/** @brief 打点：累计一次阶段耗时（delta=now-startNs，含单次最大值）。 */
static void xvkl_stage_record(XvklStageId stage, uint64_t startNs)
{
    uint64_t now = XDateTime_currentNSecsSinceEpoch();
    uint64_t delta = now > startNs ? now - startNs : 0;
    g_xvklStageProf.m_ns[stage] += delta;
    g_xvklStageProf.m_count[stage] += 1;
    if (delta > g_xvklStageProf.m_maxNs[stage])
        g_xvklStageProf.m_maxNs[stage] = delta;
}

/** @brief 输出当前窗口聚合并重开窗口（final=1 附 END 标记）。 */
static void xvkl_stage_prof_report(int final)
{
    uint64_t now = XDateTime_currentNSecsSinceEpoch();
    double secs = (double)(now - g_xvklStageProf.m_windowStartNs) /
                  1e9;
    int i;
    fprintf(stderr, "[xvkl-stage] %s window=%.1fs frames=%u\n",
            final ? "END" : "2s", secs, g_xvklStageProf.m_frames);
    for (i = 0; i < XvklStage_Count; ++i)
    {
        if (!g_xvklStageProf.m_count[i]) continue;
        fprintf(stderr,
                "[xvkl-stage]   %-18s n=%-7u total=%10.3fms "
                "avg=%9.4fms max=%9.4fms perFrame=%8.4fms\n",
                g_xvklStageNames[i], g_xvklStageProf.m_count[i],
                (double)g_xvklStageProf.m_ns[i] / 1e6,
                (double)g_xvklStageProf.m_ns[i] / 1e6 /
                    (double)g_xvklStageProf.m_count[i],
                (double)g_xvklStageProf.m_maxNs[i] / 1e6,
                g_xvklStageProf.m_frames
                    ? (double)g_xvklStageProf.m_ns[i] / 1e6 /
                          (double)g_xvklStageProf.m_frames
                    : 0.0);
    }
    xvkl_begin_prof_report();
    xvkl_obj_prof_report();
    XMemset(g_xvklStageProf.m_ns, 0, sizeof(g_xvklStageProf.m_ns));
    XMemset(g_xvklStageProf.m_maxNs, 0, sizeof(g_xvklStageProf.m_maxNs));
    XMemset(g_xvklStageProf.m_count, 0, sizeof(g_xvklStageProf.m_count));
    g_xvklStageProf.m_frames = 0;
    g_xvklStageProf.m_windowStartNs = now;
}

/** @brief 进程退出兜底：会话销毁未执行（正常退出但未走 destroy，或
             退路被绕过）时补打最终报告；销毁报告已打（frames 已归零）
             则静默，避免重复。进程被强杀时本钩子不运行——靠 2s 周期
             窗口保证短运行也有输出。 */
static void xvkl_stage_prof_atexit_report(void)
{
    if (g_xvklStageProf.m_frames) xvkl_stage_prof_report(1);
}

/** @brief beginFrame 侧窗口 tick：帧计数并按 2s 窗口驱动周期输出。 */
static void xvkl_stage_prof_tick(void)
{
    if (!xvkl_stage_prof_on() && !xvkl_begin_prof_on() && !xvkl_obj_prof_on())
        return;
    ++g_xvklStageProf.m_frames;
    if (XDateTime_currentNSecsSinceEpoch() -
        g_xvklStageProf.m_windowStartNs >= 2000000000u)
        xvkl_stage_prof_report(0);
}

/* ==================== begin 链细分计时（XGPU_VK_BEGIN_PROF=1 启用） ==================== */

/* 定位 begin_image GPU 分支耗时归属：total=驱动 begin_frame 全函数，
   fence/acquire/copyInit=三个阻塞嫌疑段（其余归 other）。与
   XGPU_PAINTER_PROF 的 begin_image 计时同开相减，即可把 painter 侧
   （画布清零/会话获取/批防御）与驱动侧 begin 链拆开归因。 */
static struct XvklBeginProf
{
    uint64_t m_totalNs;   /**< begin_frame 全函数累计。 */
    uint64_t m_fenceNs;   /**< 帧/悬置 fence 等待累计。 */
    uint64_t m_acquireNs; /**< swapchain acquire 累计。 */
    uint64_t m_copyNs;    /**< initialImage 画布整幅预乘上传累计。 */
    uint64_t m_prepNs;    /**< prepare_frame_target（帧目标准备）累计。 */
    uint64_t m_maxNs;     /**< 单次 begin_frame 最大耗时。 */
    uint32_t m_count;     /**< begin_frame 次数。 */
} g_xvklBeginProf;

/** @brief XGPU_VK_BEGIN_PROF 开关（"0"=显式关；其余非空=开；缓存）。 */
static bool xvkl_begin_prof_on(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_BEGIN_PROF");
        cached = v && *v && !(v[0] == '0' && v[1] == 0) ? 1 : 0;
    }
    return cached != 0;
}

/** @brief begin 链窗口行输出（随 stage-prof 的 2s/END 窗口驱动）。 */
static void xvkl_begin_prof_report(void)
{
    uint64_t known;
    uint64_t other;
    if (!xvkl_begin_prof_on() || !g_xvklBeginProf.m_count) return;
    known = g_xvklBeginProf.m_fenceNs + g_xvklBeginProf.m_acquireNs +
            g_xvklBeginProf.m_copyNs + g_xvklBeginProf.m_prepNs;
    other = g_xvklBeginProf.m_totalNs > known
                ? g_xvklBeginProf.m_totalNs - known
                : 0;
    fprintf(stderr,
            "[xvkl-begin] n=%-7u avg=%9.4fms max=%9.4fms fence=%9.4fms "
            "acquire=%9.4fms copyInit=%9.4fms prep=%9.4fms other=%9.4fms\n",
            g_xvklBeginProf.m_count,
            (double)g_xvklBeginProf.m_totalNs / 1e6 /
                (double)g_xvklBeginProf.m_count,
            (double)g_xvklBeginProf.m_maxNs / 1e6,
            (double)g_xvklBeginProf.m_fenceNs / 1e6 /
                (double)g_xvklBeginProf.m_count,
            (double)g_xvklBeginProf.m_acquireNs / 1e6 /
                (double)g_xvklBeginProf.m_count,
            (double)g_xvklBeginProf.m_copyNs / 1e6 /
                (double)g_xvklBeginProf.m_count,
            (double)g_xvklBeginProf.m_prepNs / 1e6 /
                (double)g_xvklBeginProf.m_count,
            (double)other / 1e6 / (double)g_xvklBeginProf.m_count);
    XMemset(&g_xvklBeginProf, 0, sizeof(g_xvklBeginProf));
}

/* ==================== 对象创建审计（XGPU_VK_OBJ_PROF=1 启用） ==================== */

/* 第 8 轮统筹样本：amdvlk64 serialization 锁疑为管线编译锁——审计
   vkCreateGraphicsPipelines/vkCreateShaderModule/vkCreateRenderPass/
   vkAllocateCommandBuffers 等每窗口发生数：稳态（会话创建完成后）必须
   为 0；非 0 即存在每帧/周期性对象重建（会话重试、staging/画布扩容、
   swapchain 重建等），按计数定位后实施管线缓存/复用。计数点覆盖全部
   驱动内创建入口（含 helper），窗口驱动与 [xvkl-stage] 同源。 */
static struct XvklObjProf
{
    uint64_t m_winStartNs;      /**< 窗口起点。 */
    uint32_t m_frames;          /**< 窗口内 begin_frame 次数。 */
    uint32_t m_graphicsPipelines;
    uint32_t m_shaderModules;
    uint32_t m_renderPasses;
    uint32_t m_cmdBuffers;
    uint32_t m_framebuffers;
    uint32_t m_swapchains;
    uint32_t m_images;
    uint32_t m_buffers;
    uint32_t m_deviceMemory;
} g_xvklObjProf;

static bool g_xvklObjOn = false; /**< 惰性初始化（首次 hit/report 判定）。 */

/** @brief XGPU_VK_OBJ_PROF 开关（"0"=显式关；其余非空=开；缓存）。 */
static bool xvkl_obj_prof_on(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_OBJ_PROF");
        g_xvklObjOn = v && *v && !(v[0] == '0' && v[1] == 0) ? 1 : 0;
        if (g_xvklObjOn)
            g_xvklObjProf.m_winStartNs = XDateTime_currentNSecsSinceEpoch();
        cached = g_xvklObjOn ? 1 : 0;
    }
    return cached != 0;
}

/** @brief 对象创建命中计数（门控关闭时零开销直返）。 */
static void xvkl_obj_hit(uint32_t* slot)
{
    if (g_xvklObjOn) *slot += 1;
}

/** @brief 对象审计窗口行（随 stage-prof 的 2s/END 窗口驱动）。 */
static void xvkl_obj_prof_report(void)
{
    if (!g_xvklObjOn) return;
    fprintf(stderr,
            "[xvkl-obj] frames=%-6u pipelines=%-4u shaders=%-4u "
            "passes=%-4u cmdbuf=%-4u fb=%-4u swap=%-3u img=%-4u "
            "buf=%-4u mem=%-4u\n",
            g_xvklObjProf.m_frames,
            g_xvklObjProf.m_graphicsPipelines,
            g_xvklObjProf.m_shaderModules,
            g_xvklObjProf.m_renderPasses,
            g_xvklObjProf.m_cmdBuffers,
            g_xvklObjProf.m_framebuffers,
            g_xvklObjProf.m_swapchains,
            g_xvklObjProf.m_images,
            g_xvklObjProf.m_buffers,
            g_xvklObjProf.m_deviceMemory);
    XMemset(&g_xvklObjProf.m_graphicsPipelines, 0,
            sizeof(g_xvklObjProf.m_graphicsPipelines));
    g_xvklObjProf.m_shaderModules = 0;
    g_xvklObjProf.m_renderPasses = 0;
    g_xvklObjProf.m_cmdBuffers = 0;
    g_xvklObjProf.m_framebuffers = 0;
    g_xvklObjProf.m_swapchains = 0;
    g_xvklObjProf.m_images = 0;
    g_xvklObjProf.m_buffers = 0;
    g_xvklObjProf.m_deviceMemory = 0;
    g_xvklObjProf.m_frames = 0;
    g_xvklObjProf.m_winStartNs = XDateTime_currentNSecsSinceEpoch();
}

/* ==================== 会话结构 ==================== */

/* 帧环深度上限（XGPU_VK_FRAMES_IN_FLIGHT 可配 1~3，默认 1）：1=旧单槽
   路径（beginFrame 逐帧全 GPU 排空，当前最优，2026-09-28 第 2 轮实测
   K=2 使 p0 16.3→13.4 故回退默认）；2~3=命令缓冲/fence/信号量/顶点
   缓冲按槽轮转，endFrame N+1 的 CPU 录制/提交与 GPU 执行/present N
   重叠（实测在本负载下反而劣化，疑似 FIFO 背压前移至 acquire 集中化
   ——仅作显式开关保留供复测）。按槽信号量复用的合法性由单队列提交序
   保证：renderDone[s] 的 wait（present N）在队列序上先于 submit(N+K)
   对它的再次 signal；imageReady[s] 的再次 signal 前必经本槽 fence 等
   待（覆盖 submit N 对它的 wait）。 */
#define XGPU_VK_MAX_FRAMES_IN_FLIGHT 3

struct XGpuRenderDriverSession
{
    bool m_window;               /**< 是否窗口会话（swapchain 上屏）。 */
    XWindow* m_windowObject;     /**< 窗口对象（借用，仅用于创建 surface）。 */
    int m_width;                 /**< 渲染宽度（像素）。 */
    int m_height;                /**< 渲染高度（像素）。 */
    bool m_recording;            /**< 帧命令缓冲是否在录制中。 */
    VkFormat m_targetFormat;     /**< 渲染目标格式（swapchain 需一致）。 */

    VkInstance m_instance;       /**< Vulkan 实例（每会话一个，简化）。 */
    VkPhysicalDevice m_physical; /**< 物理设备（队列族 0 含 GRAPHICS）。 */
    VkDevice m_device;           /**< 逻辑设备。 */
    VkQueue m_queue;             /**< 图形队列。 */
    uint32_t m_queueFamily;      /**< 图形/窗口 present 所在队列族。 */
    VkCommandPool m_cmdPool;     /**< 命令池。 */
    /* 帧环（XGPU_VK_FRAMES_IN_FLIGHT，默认 1=旧单槽最优路径；2~3 显式
       启用按槽轮转）：命令缓冲/fence/信号量/顶点缓冲按槽轮转；单数字段
       为当前槽镜像（begin_frame 装载、帧内恒定，endFrame 尾部推进槽
       下标）。 */
    VkCommandBuffer m_frameCmd[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    int m_frameSlot;             /**< 当前帧槽下标（begin 取用，end 推进）。 */
    int m_framesInFlight;        /**< 帧环深度（V1 同步路径强制 1）。 */
    VkCommandBuffer m_cmd;       /**< 当前槽帧命令缓冲（镜像，每帧重置重录）。 */
    /* transfer 链双缓冲轮转（第 9 轮）：命令缓冲/fence/staging 按帧槽
       独立（K 份），帧 N 用槽 A、帧 N+1 用槽 B——B 的 retire 等的是
       早已信号的历史 fence，消除跨帧 transferFenceWait（第 8 轮实测
       ~1.2ms/帧）；帧内同槽串行保持（staging 重写安全必需）。单值
       字段为当前槽镜像（begin_frame 装载，帧内恒定）。 */
    VkCommandBuffer m_transferCmds[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkFence m_transferFences[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    bool m_transferInFlight[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    /**< 各槽 transfer 是否有待回收提交。 */
    int m_transferSlot;          /**< 当前 transfer 槽（submit 后轮转）。 */
    VkCommandBuffer m_transferCmd; /**< 当前槽 transfer 命令缓冲（镜像）。 */
    VkFence m_transferFence;     /**< 当前槽 transfer 提交 fence（镜像）。 */
    VkFence m_frameFences[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkFence m_frameFence;        /**< 当前槽帧提交 fence（镜像）。 */

    /* XGPU_VK_PRESENT_V2（present 模型二期，fence 批量回收，默认开；
       =0 回退旧同步路径）：endFrame 异步提交，fence 回收点移至下一帧
       beginFrame；transfer/suspend 提交以独立 fence 取代全队列等待。 */
    bool m_presentV2;            /**< V2 异步提交模型开关（创建时读环境变量）。 */
    bool m_frameFenceArmed[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    /**< 各槽上一帧 endFrame 提交成功、该槽 fence 待回收。 */
    VkFence m_suspendFence;      /**< 半帧打断提交 fence（V2，离屏 upload/readback）。 */

    /* XGPU_VK_ASYNC_READBACK（读回异步化，默认开；置 "0" 回退旧串行
       路径）：专用读回命令缓冲 + 双缓冲 HOST_VISIBLE 读回 staging +
       每槽独立 fence。半帧提交（suspend）不再同步等待，与整幅读回拷贝
       以同队列提交序衔接（沿用 V2 的同序约定），CPU 侧一次
       vkWaitForFences 合并旧路径 suspend/拷贝的两次串行 GPU 往返；
       读回 staging 独立于上传 staging，消除上传/读回互相 retire 的
       串行点与扩容churn。CPU 拷出与旧路径共用同一例程，单帧静态画面
       读回与旧路径逐位一致。 */
    bool m_asyncReadback;        /**< 读回异步化开关（创建时读环境变量）。 */
    VkCommandBuffer m_readbackCmd; /**< 读回专用命令缓冲（独立于 m_transferCmd）。 */
    /* canvas keep（XGPU_VK_CANVAS_KEEP 默认开，第 10 轮）：同 swapchain
       image 连续帧跳过冗余 copy_initial 整幅上传——IMMEDIATE 无 vsync
       强制轮换 + FBO keep-open 自持 + 帧末 drb 脏区双向同步 ⇒ paintImage
       与 FBO 恒等链保持，跳过帧与上传帧的 FBO 逐位一致；60 帧强制重
       同步自愈（防 degraded 软件帧 lost-update 长尾）。 */
    int m_keepSyncedSlot;        /**< 上次整幅上传命中的 swapchain image 下标（-1=无）。 */
    uint32_t m_keepStreak;       /**< 连续跳过帧数（60 帧强制重同步）。 */
    VkBuffer m_readbackBuffer[2];  /**< 双缓冲读回 staging（首用时惰性分配）。 */
    VkDeviceMemory m_readbackMemory[2];
    void* m_readbackMapped[2];     /**< 持久映射。 */
    size_t m_readbackCapacity[2];  /**< 各槽字节容量。 */
    bool m_readbackCoherent[2];   /**< 各槽内存是否 COHERENT（非 CACHED+COHERENT 档需 invalidate）。 */
    VkFence m_readbackFence[2];    /**< 各槽读回提交 fence。 */
    bool m_readbackInFlight[2];    /**< 各槽是否有待回收读回提交。 */
    int m_readbackIndex;           /**< 下一笔读回使用的槽（乒乓）。 */
    bool m_suspendInFlight;        /**< 半帧异步提交待回收（读回流程内部）。 */

    VkRenderPass m_renderPass;   /**< 单子通道渲染通道（loadOp=LOAD）。 */
    VkPipelineLayout m_solidLayout;   /**< 纯色管线布局（push constant）。 */
    VkPipeline m_solidPipeline;  /**< 纯色填充管线。 */
    VkPipeline m_solidSourcePipeline; /**< 纯色 Source 覆盖管线。 */
    VkPipelineLayout m_texLayout;     /**< 纹理管线布局（set0 采样器）。 */
    VkPipeline m_texPipeline;    /**< 纹理采样管线。 */
    VkPipeline m_texSourcePipeline; /**< 纹理 Source 覆盖管线。 */
    VkSampler m_sampler;         /**< 最近邻纹理采样器。 */
    VkDescriptorSetLayout m_texSetLayout; /**< 纹理描述符布局。 */
    VkDescriptorPool m_descPool; /**< 描述符池。 */
    VkDescriptorSet m_sourceSet; /**< 源纹理描述符。 */
    VkDescriptorSet m_atlasSet;  /**< 图集描述符。 */

    /* 离屏渲染目标。 */
    VkImage m_colorImage;
    VkDeviceMemory m_colorMemory;
    VkImageView m_colorView;
    VkImageLayout m_colorLayout; /**< 离屏图像当前布局。 */

    /* 窗口 swapchain。 */
    VkSurfaceKHR m_surface;      /**< 平台窗口表面（Drive 创建，不透明）。 */
    VkSwapchainKHR m_swapchain;  /**< 交换链。 */
    uint32_t m_swapCount;        /**< 交换链图像数。 */
    VkImage m_swapImages[8];     /**< 交换链图像（借用，来自 swapchain）。 */
    VkImageView m_swapViews[8];  /**< 交换链图像视图（拥有）。 */
    VkFramebuffer m_swapFbs[8];  /**< 每图像 framebuffer（拥有）。 */
    VkImageLayout m_swapLayouts[8]; /**< 交换链图像当前布局。 */
    uint32_t m_imageIndex;       /**< 当前获取的交换链图像下标。 */
    VkSemaphore m_imageReadys[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore m_renderDones[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore m_imageReady;    /**< 当前槽 acquire 信号量（镜像）。 */
    VkSemaphore m_renderDone;    /**< 当前槽 present 信号量（镜像）。 */
    bool m_imageWaitConsumed;    /**< 本帧 acquire 信号量是否已消费。 */

    /* 几何：HOST_VISIBLE 顶点缓冲（pos2+uv2+color4，8 floats/顶点），
       按槽一份（帧环下防止下一帧 CPU 写顶点覆盖在途帧仍在读取的
       顶点数据——同队列序管不了 CPU 侧映射写入）。 */
    VkBuffer m_vertexBuffers[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_vertexMemories[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    float* m_vertexMappeds[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    uint32_t m_vertexCapacities[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkBuffer m_vertexBuffer;     /**< 当前槽顶点缓冲（镜像）。 */
    float* m_vertexMapped;       /**< 当前槽持久映射（镜像）。 */
    uint32_t m_vertexCapacity;   /**< 当前槽顶点容量（个，镜像）。 */
    uint32_t m_vertexCursor;     /**< 本帧已写顶点数。 */

    /* 一次性上传 staging（HOST_VISIBLE，按需扩容）——按帧槽独立（transfer
       链槽化的一部分：staging 重写与 GPU 读它的 transfer 分槽后互不
       等待）；单值字段为当前槽镜像。 */
    VkBuffer m_stagingBuffers[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkDeviceMemory m_stagingMemories[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    void* m_stagingMappeds[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    size_t m_stagingCapacities[XGPU_VK_MAX_FRAMES_IN_FLIGHT];
    VkBuffer m_stagingBuffer;
    void* m_stagingMapped;       /**< 持久映射（当前槽镜像）。 */
    size_t m_stagingCapacity;    /**< staging 容量（字节，当前槽镜像）。 */

    /* 帧内内联上传 staging（XGPU_VK_UPLOAD_BATCH）：独立于 transferCmd
       的 m_staging*（图集上传/读回共用，会在帧中途被覆写/扩容）——
       同帧多笔上传按游标追加互不覆写；本帧 m_cmd 执行后下帧复用
       （K=1 下 beginFrame fence 等待覆盖在途读取）。 */
    VkBuffer m_frameStagingBuffer;
    VkDeviceMemory m_frameStagingMemory;
    void* m_frameStagingMapped;  /**< 持久映射。 */
    size_t m_frameStagingCapacity; /**< 容量（字节）。 */
    size_t m_frameStageCursor;   /**< 本帧已追加字节（beginFrame 清零）。 */
    size_t m_frameStageDemand;   /**< 回退笔记下的扩容需求（游标 0 时扩）。 */

    /* 源纹理与图集（复用单图像，绘制时整幅重传）。 */
    VkImage m_sourceImage;
    VkDeviceMemory m_sourceMemory;
    VkImageView m_sourceView;
    VkImageLayout m_sourceLayout;
    int m_sourceWidth;           /**< 当前源纹理尺寸（0=未创建）。 */
    int m_sourceHeight;
    VkImage m_atlasImage;
    VkDeviceMemory m_atlasMemory;
    VkImageView m_atlasView;
    VkImageLayout m_atlasLayout;

    XImage* m_pendingReadback;   /**< 帧末待回读目标（借用）。 */
};

/* ==================== 内存与工具 ==================== */

/**
 * @brief      在给定内存类型上分配并绑定缓冲/图像内存。
 * @param      device 逻辑设备。
 * @param      physical 物理设备。
 * @param      requirements 内存需求。
 * @param      flags 期望属性位（HOST_VISIBLE/DEVICE_LOCAL 等）。
 * @param      outMemory 输出设备内存。
 * @return     true 成功；false 找不到兼容内存类型或分配失败。
 */
static bool xvkl_alloc_memory(VkDevice device, VkPhysicalDevice physical,
                              const VkMemoryRequirements* req,
                              VkMemoryPropertyFlags flags,
                              VkDeviceMemory* outMemory)
{
    VkPhysicalDeviceMemoryProperties props;
    uint32_t i;
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (i = 0; i < props.memoryTypeCount; ++i)
    {
        if ((req->memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags)
        {
            VkMemoryAllocateInfo ai;
            XMemset(&ai, 0, sizeof(ai));
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req->size;
            ai.memoryTypeIndex = i;
            if (vkAllocateMemory(device, &ai, NULL, outMemory) ==
                VK_SUCCESS)
            {
                xvkl_obj_hit(&g_xvklObjProf.m_deviceMemory);
                return true;
            }
            return false;
        }
    }
    return false;
}

/** @brief 创建 HOST_VISIBLE|COHERENT 缓冲并持久映射。 */
static bool xvkl_create_host_buffer(VkDevice device, VkPhysicalDevice physical,
                                    VkDeviceSize bytes, VkBuffer* outBuffer,
                                    VkDeviceMemory* outMemory, void** outMapped)
{
    VkBufferCreateInfo bi;
    VkMemoryRequirements req;
    if (!outBuffer || !outMemory) return false;
    XMemset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, NULL, outBuffer) != VK_SUCCESS)
        return false;
    xvkl_obj_hit(&g_xvklObjProf.m_buffers);
    vkGetBufferMemoryRequirements(device, *outBuffer, &req);
    if (!xvkl_alloc_memory(device, physical, &req,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           outMemory))
        return false;
    if (vkBindBufferMemory(device, *outBuffer, *outMemory, 0) != VK_SUCCESS)
        return false;
    return vkMapMemory(device, *outMemory, 0, bytes, 0, outMapped) ==
           VK_SUCCESS;
}

/** @brief 创建设备本地图像及其内存与视图。 */
static bool xvkl_create_color_image(VkDevice device,
                                    VkPhysicalDevice physical, int width,
                                    int height, VkFormat format,
                                    VkImageUsageFlags usage, VkImage* outImage,
                                    VkDeviceMemory* outMemory,
                                    VkImageView* outView)
{
    VkImageCreateInfo ii;
    VkMemoryRequirements req;
    VkImageViewCreateInfo vi;
    if (!outImage || !outMemory || !outView) return false;
    XMemset(&ii, 0, sizeof(ii));
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent.width = (uint32_t)width;
    ii.extent.height = (uint32_t)height;
    ii.extent.depth = 1;
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ii, NULL, outImage) != VK_SUCCESS)
        return false;
    xvkl_obj_hit(&g_xvklObjProf.m_images);
    vkGetImageMemoryRequirements(device, *outImage, &req);
    if (!xvkl_alloc_memory(device, physical, &req,
                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outMemory))
        return false;
    if (vkBindImageMemory(device, *outImage, *outMemory, 0) != VK_SUCCESS)
        return false;
    XMemset(&vi, 0, sizeof(vi));
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = *outImage;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &vi, NULL, outView) != VK_SUCCESS)
        return false;
    return true;
}

/** @brief 创建最近邻、边缘钳位的组合采样器。 */
static bool xvkl_create_sampler(XGpuRenderDriverSession* self)
{
    VkSamplerCreateInfo si;
    if (!self) return false;
    XMemset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    return vkCreateSampler(self->m_device, &si, NULL, &self->m_sampler) ==
           VK_SUCCESS;
}

/** @brief 为纹理描述符写入采样器与图像视图。 */
static void xvkl_update_texture_descriptor(XGpuRenderDriverSession* self,
                                            VkDescriptorSet set,
                                            VkImageView view)
{
    VkDescriptorImageInfo info;
    VkWriteDescriptorSet write;
    if (!self || !set || !view || !self->m_sampler) return;
    XMemset(&info, 0, sizeof(info));
    info.sampler = self->m_sampler;
    info.imageView = view;
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    XMemset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(self->m_device, 1, &write, 0, NULL);
}

/* ==================== render pass 与 pipeline ==================== */

static bool xvkl_create_render_pass(VkDevice device, VkFormat format,
                                    bool presentSrc, VkRenderPass* outPass)
{
    VkAttachmentDescription attachment;
    VkAttachmentReference colorRef;
    VkSubpassDescription subpass;
    VkRenderPassCreateInfo ci;
    XMemset(&attachment, 0, sizeof(attachment));
    attachment.format = format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  /* 帧首显式清/上传。 */
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    /* 窗口会话最终布局必须为 PRESENT_SRC（present 的规范要求）；
       离屏保持 COLOR_ATTACHMENT（下一帧 LOAD 读取）。 */
    attachment.finalLayout = presentSrc
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    XMemset(&colorRef, 0, sizeof(colorRef));
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    XMemset(&subpass, 0, sizeof(subpass));
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    XMemset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &attachment;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 0;
    if (vkCreateRenderPass(device, &ci, NULL, outPass) != VK_SUCCESS)
        return false;
    xvkl_obj_hit(&g_xvklObjProf.m_renderPasses);
    return true;
}

/**
 * @brief      创建图形管线：顶点布局 pos(2F)+uv(2F)+color(4F)。
 * @param      textured true 片段输出 texture(uv)*color；false 输出 color。
 */
static bool xvkl_create_pipeline(VkDevice device, VkRenderPass pass,
                                 VkPipelineLayout layout, bool textured,
                                 bool sourceOver, VkPipeline* outPipeline)
{
    VkPipelineShaderStageCreateInfo stages[2];
    VkVertexInputBindingDescription binding;
    VkVertexInputAttributeDescription attrs[3];
    VkPipelineVertexInputStateCreateInfo vertexInput;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo viewport;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo multisample;
    VkPipelineColorBlendAttachmentState blendAttachment;
    VkPipelineColorBlendStateCreateInfo blend;
    VkPipelineDynamicStateCreateInfo dynamic;
    VkDynamicState dynStates[2];
    VkGraphicsPipelineCreateInfo ci;
    VkShaderModule vs = 0, fs = 0;
    bool ok = false;
    VkShaderModuleCreateInfo sci;
    XMemset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    sci.codeSize = KSPVVERTEX_WORDS * sizeof(uint32_t);
    sci.pCode = kSpvVertex;
    if (vkCreateShaderModule(device, &sci, NULL, &vs) != VK_SUCCESS)
        return false;
    xvkl_obj_hit(&g_xvklObjProf.m_shaderModules);
    sci.codeSize = (textured ? KSPVFRAGMENTTEXTURE_WORDS
                             : KSPVFRAGMENTSOLID_WORDS) * sizeof(uint32_t);
    sci.pCode = textured ? kSpvFragmentTexture : kSpvFragmentSolid;
    if (vkCreateShaderModule(device, &sci, NULL, &fs) != VK_SUCCESS)
    {
        vkDestroyShaderModule(device, vs, NULL);
        return false;
    }
    xvkl_obj_hit(&g_xvklObjProf.m_shaderModules);
    XMemset(stages, 0, sizeof(stages));
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    XMemset(&binding, 0, sizeof(binding));
    binding.binding = 0;
    binding.stride = sizeof(float) * 8u;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    XMemset(attrs, 0, sizeof(attrs));
    attrs[0].binding = 0; attrs[0].location = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = 0;
    attrs[1].binding = 0; attrs[1].location = 1;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT; attrs[1].offset = 8;
    attrs[2].binding = 0; attrs[2].location = 2;
    attrs[2].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[2].offset = 16;
    XMemset(&vertexInput, 0, sizeof(vertexInput));
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 3;
    vertexInput.pVertexAttributeDescriptions = attrs;
    XMemset(&assembly, 0, sizeof(assembly));
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    XMemset(&viewport, 0, sizeof(viewport));
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    XMemset(&raster, 0, sizeof(raster));
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    XMemset(&multisample, 0, sizeof(multisample));
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    XMemset(&blendAttachment, 0, sizeof(blendAttachment));
    blendAttachment.blendEnable = sourceOver ? VK_TRUE : VK_FALSE;
    blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    XMemset(&blend, 0, sizeof(blend));
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    XMemset(&dynamic, 0, sizeof(dynamic));
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
    dynStates[1] = VK_DYNAMIC_STATE_SCISSOR;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynStates;
    XMemset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &assembly;
    ci.pViewportState = &viewport;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &multisample;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    ci.layout = layout;
    ci.renderPass = pass;
    ci.subpass = 0;
    ok = vkCreateGraphicsPipelines(device, 0, 1, &ci, NULL, outPipeline) ==
         VK_SUCCESS;
    if (ok) xvkl_obj_hit(&g_xvklObjProf.m_graphicsPipelines);
    vkDestroyShaderModule(device, vs, NULL);
    vkDestroyShaderModule(device, fs, NULL);
    return ok;
}

/* ==================== 会话创建/销毁 ==================== */

static bool xvkl_available(void)
{
    return true; /* 由 sessionCreate 最终裁定。 */
}

/** @brief 从物理设备选择同时支持图形的队列族；窗口时还需 present。 */
static bool xvkl_find_queue_family(XGpuRenderDriverSession* self,
                                   uint32_t* outFamily)
{
    uint32_t count = 0;
    uint32_t i;
    VkQueueFamilyProperties* props;
    if (!self || !outFamily) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(self->m_physical, &count, NULL);
    if (!count) return false;
    props = (VkQueueFamilyProperties*)XMalloc_System(
        (size_t)count * sizeof(*props));
    if (!props) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(self->m_physical, &count, props);
    for (i = 0; i < count; ++i)
    {
        VkBool32 present = VK_TRUE;
        if (!(props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
        if (self->m_window)
        {
            /* 连接句柄与窗口有效性由 Drive 的 surface 创建入口校验；
               这里只需确认已建 surface 的 present 支持。 */
            if (!self->m_windowObject || !XWindow_winId(self->m_windowObject) ||
                !self->m_surface ||
                vkGetPhysicalDeviceSurfaceSupportKHR(self->m_physical, i,
                                                     self->m_surface,
                                                     &present) != VK_SUCCESS ||
                !present)
                continue;
        }
        *outFamily = i;
        XFree_System(props);
        return true;
    }
    XFree_System(props);
    return false;
}

static bool xvkl_create_device_objects(XGpuRenderDriverSession* self,
                                       VkFormat targetFormat)
{
    VkDescriptorPoolSize poolSize;
    VkDescriptorPoolCreateInfo poolCi;
    VkDescriptorSetLayoutBinding binding;
    VkDescriptorSetLayoutCreateInfo setCi;
    VkPushConstantRange pushRange;
    VkPipelineLayoutCreateInfo layoutCi;
    VkDescriptorSetAllocateInfo allocCi;
    self->m_targetFormat = targetFormat;
    if (!xvkl_create_render_pass(self->m_device, targetFormat, self->m_window,
                                 &self->m_renderPass))
        return false;
    XMemset(&pushRange, 0, sizeof(pushRange));
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 4u;
    XMemset(&layoutCi, 0, sizeof(layoutCi));
    layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCi.pushConstantRangeCount = 1;
    layoutCi.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(self->m_device, &layoutCi, NULL,
                               &self->m_solidLayout) != VK_SUCCESS)
        return false;
    XMemset(&binding, 0, sizeof(binding));
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    XMemset(&setCi, 0, sizeof(setCi));
    setCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    setCi.bindingCount = 1;
    setCi.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(self->m_device, &setCi, NULL,
                                    &self->m_texSetLayout) != VK_SUCCESS)
        return false;
    {
        VkPipelineLayoutCreateInfo texLayoutCi = layoutCi;
        texLayoutCi.pushConstantRangeCount = 0;
        texLayoutCi.pPushConstantRanges = NULL;
        texLayoutCi.setLayoutCount = 1;
        texLayoutCi.pSetLayouts = &self->m_texSetLayout;
        if (vkCreatePipelineLayout(self->m_device, &texLayoutCi, NULL,
                                   &self->m_texLayout) != VK_SUCCESS)
            return false;
    }
    XMemset(&poolSize, 0, sizeof(poolSize));
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 4;
    XMemset(&poolCi, 0, sizeof(poolCi));
    poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCi.maxSets = 4;
    poolCi.poolSizeCount = 1;
    poolCi.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(self->m_device, &poolCi, NULL,
                               &self->m_descPool) != VK_SUCCESS)
        return false;
    if (!xvkl_create_sampler(self))
        return false;
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_solidLayout, false, true,
                              &self->m_solidPipeline))
    {
        fprintf(stderr, "vulkan: solid blend pipeline failed\n");
        return false;
    }
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_solidLayout, false, false,
                              &self->m_solidSourcePipeline))
    {
        fprintf(stderr, "vulkan: solid source pipeline failed\n");
        return false;
    }
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_texLayout, true, true,
                              &self->m_texPipeline))
    {
        fprintf(stderr, "vulkan: texture blend pipeline failed\n");
        return false;
    }
    if (!xvkl_create_pipeline(self->m_device, self->m_renderPass,
                              self->m_texLayout, true, false,
                              &self->m_texSourcePipeline))
    {
        fprintf(stderr, "vulkan: texture source pipeline failed\n");
        return false;
    }
    XMemset(&allocCi, 0, sizeof(allocCi));
    allocCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocCi.descriptorPool = self->m_descPool;
    allocCi.descriptorSetCount = 1;
    allocCi.pSetLayouts = &self->m_texSetLayout;
    if (vkAllocateDescriptorSets(self->m_device, &allocCi,
                                 &self->m_sourceSet) != VK_SUCCESS)
        return false;
    if (vkAllocateDescriptorSets(self->m_device, &allocCi,
                                 &self->m_atlasSet) != VK_SUCCESS)
        return false;
    return true;
}

static void xvkl_destroy_device_objects(XGpuRenderDriverSession* self)
{
    if (!self) return;
    if (self->m_solidPipeline)
        vkDestroyPipeline(self->m_device, self->m_solidPipeline, NULL);
    if (self->m_solidSourcePipeline)
        vkDestroyPipeline(self->m_device, self->m_solidSourcePipeline, NULL);
    if (self->m_texPipeline)
        vkDestroyPipeline(self->m_device, self->m_texPipeline, NULL);
    if (self->m_texSourcePipeline)
        vkDestroyPipeline(self->m_device, self->m_texSourcePipeline, NULL);
    if (self->m_solidLayout)
        vkDestroyPipelineLayout(self->m_device, self->m_solidLayout, NULL);
    if (self->m_texLayout)
        vkDestroyPipelineLayout(self->m_device, self->m_texLayout, NULL);
    if (self->m_texSetLayout)
        vkDestroyDescriptorSetLayout(self->m_device, self->m_texSetLayout,
                                     NULL);
    if (self->m_descPool)
        vkDestroyDescriptorPool(self->m_device, self->m_descPool, NULL);
    if (self->m_sampler)
        vkDestroySampler(self->m_device, self->m_sampler, NULL);
    if (self->m_renderPass)
        vkDestroyRenderPass(self->m_device, self->m_renderPass, NULL);
}

/** @brief 创建窗口平台 surface（X11/Win32 系统 API 均位于 Drive）。 */
static bool xvkl_create_surface(XGpuRenderDriverSession* self,
                                XWindow* window)
{
    void* surface = NULL;
    if (!self || !window || !self->m_instance) return false;
    if (!XPlatformGraphicsDriver_createVulkanWindowSurface(
            (void*)self->m_instance, window, &surface))
        return false;
    self->m_surface = (VkSurfaceKHR)surface;
    return true;
}

/** @brief 创建离屏颜色图像与 framebuffer。 */
static bool xvkl_create_offscreen_target(XGpuRenderDriverSession* self)
{
    VkFramebufferCreateInfo fi;
    VkImageView attachments[1];
    if (!self || self->m_window) return false;
    if (!xvkl_create_color_image(
            self->m_device, self->m_physical, self->m_width, self->m_height,
            self->m_targetFormat,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            &self->m_colorImage, &self->m_colorMemory, &self->m_colorView))
        return false;
    XMemset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = self->m_renderPass;
    fi.attachmentCount = 1;
    attachments[0] = self->m_colorView;
    fi.pAttachments = attachments;
    fi.width = (uint32_t)self->m_width;
    fi.height = (uint32_t)self->m_height;
    fi.layers = 1;
    if (vkCreateFramebuffer(self->m_device, &fi, NULL, &self->m_swapFbs[0]) !=
        VK_SUCCESS)
        return false;
    xvkl_obj_hit(&g_xvklObjProf.m_framebuffers);
    self->m_swapCount = 1;
    self->m_colorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

/**
 * @brief      解析 XGPU_VK_PRESENT_MODE 并钳到设备支持的呈现模式。
 * @details    默认 FIFO（现行为）；"mailbox"/"immediate"/"relaxed" 显式
 *             选择。【第 8 轮 K=3 健康帧归因（统筹补跑）：acquire=
 *             5.48ms/帧（max 12.9ms）为剩余最大单段=FIFO 背压在
 *             vkAcquireNextImageKHR 上的等待——本扫描即第五轮计划的
 *             present mode 三模式实测入口】IMMEDIATE 跳过 FIFO 队列
 *             （撕裂）；RELAXED=FIFO 但错过 vsync 立即呈现（撕裂介于
 *             两者，第 4 轮旧成本结构下 immediate 无收益的结论在新
 *             结构下需重测）。请求的模式不被支持时回退 FIFO（Vulkan
 *             规范保证 FIFO 恒支持）。
 */
static VkPresentModeKHR xvkl_choose_present_mode(VkPhysicalDevice physical,
                                                 VkSurfaceKHR surface)
{
    VkPresentModeKHR modes[16];
    uint32_t count = 0;
    /* 默认 IMMEDIATE（2026-09-28）：本机 RDP+AMD 22.20 栈实测 FIFO 的显示
       栈传输背压为帧间 30ms 级天花板（p0 27.2 / p4t20 13.1），IMMEDIATE 解锁
       后 p0 194.6 / p4t20 82.6——与 GL 回读+BitBlt 通道「不等 vsync」的呈现
       语义对齐（GL 路径从未等待垂直同步）。原生屏 tearing 敏感场景可用
       XGPU_VK_PRESENT_MODE=fifo 回退（mailbox/relaxed 亦可选）。 */
    VkPresentModeKHR want = VK_PRESENT_MODE_IMMEDIATE_KHR;
    const char* v = XSystem_environment("XGPU_VK_PRESENT_MODE");
    uint32_t i;
    if (v && v[0])
    {
        if (v[0] == 'm' && v[1] == 'a')
            want = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (v[0] == 'f')
            want = VK_PRESENT_MODE_FIFO_KHR;
        else if (v[0] == 'r')
            want = VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }
    if (want == VK_PRESENT_MODE_FIFO_KHR)
        return want;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
                                                  &count, NULL) != VK_SUCCESS ||
        count == 0 || count > 16)
        return VK_PRESENT_MODE_FIFO_KHR;
    if (vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
                                                  &count, modes) != VK_SUCCESS)
        return VK_PRESENT_MODE_FIFO_KHR;
    for (i = 0; i < count; ++i)
    {
        if (modes[i] == want)
            return want;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

/** @brief 创建窗口 swapchain、图像视图与 framebuffer。 */
static bool xvkl_create_swapchain(XGpuRenderDriverSession* self)
{
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR formats[32];
    uint32_t formatCount = 0;
    VkSurfaceFormatKHR chosen;
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkSwapchainCreateInfoKHR ci;
    VkExtent2D extent;
    uint32_t imageCount;
    uint32_t i;
    VkFramebufferCreateInfo fi;
    if (!self || !self->m_window || !self->m_surface) return false;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(self->m_physical,
                                                  self->m_surface, &caps) !=
        VK_SUCCESS)
        return false;
    presentMode = xvkl_choose_present_mode(self->m_physical, self->m_surface);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &formatCount, NULL) != VK_SUCCESS ||
        formatCount == 0 || formatCount > 32)
        return false;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &formatCount, formats) != VK_SUCCESS)
        return false;
    chosen = formats[0];
    for (i = 0; i < formatCount; ++i)
    {
        if (formats[i].format == self->m_targetFormat)
        {
            chosen = formats[i];
            break;
        }
    }
    for (i = 0; i < formatCount; ++i)
    {
        if (chosen.format == self->m_targetFormat) break;
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
            formats[i].format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            chosen = formats[i];
            break;
        }
    }
    if (caps.currentExtent.width != UINT32_MAX)
        extent = caps.currentExtent;
    else
    {
        extent.width = (uint32_t)self->m_width;
        extent.height = (uint32_t)self->m_height;
        if (extent.width < caps.minImageExtent.width)
            extent.width = caps.minImageExtent.width;
        if (extent.width > caps.maxImageExtent.width)
            extent.width = caps.maxImageExtent.width;
        if (extent.height < caps.minImageExtent.height)
            extent.height = caps.minImageExtent.height;
        if (extent.height > caps.maxImageExtent.height)
            extent.height = caps.maxImageExtent.height;
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        return false;
    imageCount = caps.minImageCount + 1u;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;
    XMemset(&ci, 0, sizeof(ci));
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = self->m_surface;
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        ci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = presentMode;
    ci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(self->m_device, &ci, NULL, &self->m_swapchain) !=
        VK_SUCCESS)
        return false;
    xvkl_obj_hit(&g_xvklObjProf.m_swapchains);
    self->m_keepSyncedSlot = -1; /* swapchain 重建：canvas keep 失效。 */
    self->m_keepStreak = 0;
    if (vkGetSwapchainImagesKHR(self->m_device, self->m_swapchain,
                                &self->m_swapCount, NULL) != VK_SUCCESS ||
        self->m_swapCount == 0 || self->m_swapCount > 8)
        return false;
    if (vkGetSwapchainImagesKHR(self->m_device, self->m_swapchain,
                                &self->m_swapCount, self->m_swapImages) !=
        VK_SUCCESS)
        return false;
    self->m_targetFormat = chosen.format;
    self->m_width = (int)extent.width;
    self->m_height = (int)extent.height;
    for (i = 0; i < self->m_swapCount; ++i)
    {
        VkImageViewCreateInfo vi;
        XMemset(&vi, 0, sizeof(vi));
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = self->m_swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = self->m_targetFormat;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(self->m_device, &vi, NULL, &self->m_swapViews[i]) !=
            VK_SUCCESS)
            return false;
        self->m_swapLayouts[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    XMemset(&fi, 0, sizeof(fi));
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = self->m_renderPass;
    fi.attachmentCount = 1;
    fi.width = extent.width;
    fi.height = extent.height;
    fi.layers = 1;
    for (i = 0; i < self->m_swapCount; ++i)
    {
        fi.pAttachments = &self->m_swapViews[i];
        if (vkCreateFramebuffer(self->m_device, &fi, NULL,
                                &self->m_swapFbs[i]) != VK_SUCCESS)
            return false;
        xvkl_obj_hit(&g_xvklObjProf.m_framebuffers);
    }
    return true;
}

const XGpuRenderDriverProcs* XGpuRenderDriver_vulkan_procs(void);

static VkFormat xvkl_surface_format(XGpuRenderDriverSession* self)
{
    VkSurfaceFormatKHR formats[32];
    uint32_t count = 0;
    uint32_t i;
    if (!self || !self->m_surface ||
        vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &count, NULL) != VK_SUCCESS ||
        count == 0 || count > 32 ||
        vkGetPhysicalDeviceSurfaceFormatsKHR(self->m_physical, self->m_surface,
                                             &count, formats) != VK_SUCCESS)
        return VK_FORMAT_B8G8R8A8_UNORM;
    for (i = 0; i < count; ++i)
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            return formats[i].format;
    return formats[0].format;
}

static bool xvkl_present_v2(void); /* 定义于帧控制节（transfer 提交附近）。 */
static bool xvkl_async_readback(void); /* 定义于读回节（readback 附近）。 */
static int xvkl_frames_in_flight(void); /* 定义于帧控制节（present_v2 附近）。 */

static XGpuRenderDriverSession* xvkl_session_create(XWindow* window,
                                                    int width, int height)
{
    XGpuRenderDriverSession* self;
    self =
        (XGpuRenderDriverSession*)XCalloc_System(1u, sizeof(*self));
    VkApplicationInfo app;
    VkInstanceCreateInfo ici;
    VkDeviceQueueCreateInfo qci;
    VkDeviceCreateInfo dci;
    float priority = 1.0f;
    VkCommandPoolCreateInfo poolCi;
    VkCommandBufferAllocateInfo cbAi;
    VkFenceCreateInfo fci;
    VkSemaphoreCreateInfo sci;
    const char* const* surfaceExtensions = NULL;
    uint32_t extensionCount = 0;
    const char* failStage = "unknown";
    if (!self) return NULL;
    self->m_window = window != NULL;
    self->m_width = width;
    self->m_height = height;
    XMemset(&app, 0, sizeof(app));
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "XinYueC";
    app.apiVersion = VK_API_VERSION_1_0;
    XMemset(&ici, 0, sizeof(ici));
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    self->m_windowObject = window;
    if (window)
    {
        /* 窗口会话需要 surface 扩展（平台相关，由 Drive 查询）；
           实例创建在 surface 之前完成。 */
        if (!XPlatformGraphicsDriver_vulkanWindowSurfaceExtensions(
                &surfaceExtensions, &extensionCount) ||
            !surfaceExtensions || !extensionCount)
            goto fail;
        ici.enabledExtensionCount = extensionCount;
        ici.ppEnabledExtensionNames = surfaceExtensions;
    }
    if (vkCreateInstance(&ici, NULL, &self->m_instance) != VK_SUCCESS)
    {
        failStage = "vkCreateInstance";
        goto fail;
    }
    if (self->m_window && !xvkl_create_surface(self, window))
    {
        failStage = "createSurface";
        goto fail;
    }
    {
        uint32_t count = 0;
        VkPhysicalDevice devices[8];
        if (vkEnumeratePhysicalDevices(self->m_instance, &count, NULL) !=
                VK_SUCCESS ||
            count == 0 || count > 8 ||
            vkEnumeratePhysicalDevices(self->m_instance, &count, devices) !=
                VK_SUCCESS)
        {
            failStage = "enumeratePhysicalDevices";
            goto fail;
        }
        self->m_physical = devices[0];
    }
    if (!xvkl_find_queue_family(self, &self->m_queueFamily))
    {
        failStage = "findQueueFamily";
        goto fail;
    }
    XMemset(&qci, 0, sizeof(qci));
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = self->m_queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    XMemset(&dci, 0, sizeof(dci));
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    if (self->m_window)
    {
        static const char* const extensions[] = { "VK_KHR_swapchain" };
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = extensions;
    }
    if (vkCreateDevice(self->m_physical, &dci, NULL, &self->m_device) !=
        VK_SUCCESS)
    {
        failStage = "vkCreateDevice";
        goto fail;
    }
    vkGetDeviceQueue(self->m_device, self->m_queueFamily, 0, &self->m_queue);
    if (!self->m_queue)
    {
        failStage = "getDeviceQueue";
        goto fail;
    }
    /* 读回异步化开关在命令缓冲分配前读取：异步路径需要第三条
       PRIMARY 命令缓冲（专用读回，与帧/上传命令缓冲互不复用）。
       帧环深度同在此读取：每槽一条帧命令缓冲（V1 同步路径强制 1，
       与逐帧全排空语义绑定）。 */
    self->m_asyncReadback = xvkl_async_readback();
    self->m_framesInFlight = xvkl_present_v2() ? xvkl_frames_in_flight() : 1;
    self->m_frameSlot = 0;
    XMemset(&poolCi, 0, sizeof(poolCi));
    poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolCi.queueFamilyIndex = self->m_queueFamily;
    if (vkCreateCommandPool(self->m_device, &poolCi, NULL,
                            &self->m_cmdPool) != VK_SUCCESS)
    {
        failStage = "vkCreateCommandPool";
        goto fail;
    }
    XMemset(&cbAi, 0, sizeof(cbAi));
    cbAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAi.commandPool = self->m_cmdPool;
    cbAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAi.commandBufferCount = (uint32_t)(self->m_framesInFlight * 2 +
                                         (self->m_asyncReadback ? 1 : 0));
    {
        /* 容量 9 与下方 total>9 守卫一致（total = framesInFlight*2 +
           async?1:0，K=3+异步 时 =7）。修复 2026-09-29：原 buffers[5]
           在 K=3 时被 vkAllocateCommandBuffers 越界写 2 句柄（RTC:
           Stack around 'buffers' corrupted → 模态框挂死 main）。 */
        VkCommandBuffer buffers[9];
        uint32_t bi;
        uint32_t transferBase = (uint32_t)self->m_framesInFlight;
        uint32_t total = transferBase + (uint32_t)self->m_framesInFlight +
                         (self->m_asyncReadback ? 1u : 0u);
        if (total > 9 ||
            vkAllocateCommandBuffers(self->m_device, &cbAi, buffers) !=
                VK_SUCCESS)
        {
            failStage = "vkAllocateCommandBuffers";
            goto fail;
        }
        xvkl_obj_hit(&g_xvklObjProf.m_cmdBuffers);
        for (bi = 0; bi < (uint32_t)self->m_framesInFlight; ++bi)
            self->m_frameCmd[bi] = buffers[bi];
        self->m_cmd = buffers[0];
        for (bi = 0; bi < (uint32_t)self->m_framesInFlight; ++bi)
            self->m_transferCmds[bi] = buffers[transferBase + bi];
        self->m_transferCmd = self->m_transferCmds[0];
        if (self->m_asyncReadback)
            self->m_readbackCmd = buffers[total - 1];
    }
    XMemset(&fci, 0, sizeof(fci));
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    {
        int fi;
        for (fi = 0; fi < self->m_framesInFlight; ++fi)
        {
            if (vkCreateFence(self->m_device, &fci, NULL,
                              &self->m_frameFences[fi]) != VK_SUCCESS)
            {
                failStage = "vkCreateFence";
                goto fail;
            }
        }
        self->m_frameFence = self->m_frameFences[0];
    }
    self->m_presentV2 = xvkl_present_v2();
    if (self->m_presentV2)
    {
        /* V2 辅助 fence：初始 UNSIGNALED（未提交的工作无需等待）。
           transfer fence 按帧槽 ×K（transfer 链槽化，第 9 轮）。 */
        VkFenceCreateInfo ufci;
        int tfi;
        XMemset(&ufci, 0, sizeof(ufci));
        ufci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        for (tfi = 0; tfi < self->m_framesInFlight; ++tfi)
        {
            if (vkCreateFence(self->m_device, &ufci, NULL,
                              &self->m_transferFences[tfi]) != VK_SUCCESS)
            {
                failStage = "vkCreateFence(v2)";
                goto fail;
            }
        }
        self->m_transferFence = self->m_transferFences[0];
        self->m_keepSyncedSlot = -1; /* 会话创建：canvas keep 失效待首帧同步。 */
        self->m_keepStreak = 0;
        if (vkCreateFence(self->m_device, &ufci, NULL,
                          &self->m_suspendFence) != VK_SUCCESS)
        {
            failStage = "vkCreateFence(v2-suspend)";
            goto fail;
        }
    }
    if (self->m_asyncReadback)
    {
        /* 异步读回辅助 fence：初始 UNSIGNALED（无在途工作，无需等待）。
           半帧异步提交复用 m_suspendFence——V2 块未创建时（PRESENT_V2=0
           且 ASYNC_READBACK 开）在此补建，保证开关组合正交可用。 */
        VkFenceCreateInfo ufci;
        XMemset(&ufci, 0, sizeof(ufci));
        ufci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (!self->m_suspendFence &&
            vkCreateFence(self->m_device, &ufci, NULL,
                          &self->m_suspendFence) != VK_SUCCESS)
        {
            failStage = "vkCreateFence(async-suspend)";
            goto fail;
        }
        if (vkCreateFence(self->m_device, &ufci, NULL,
                          &self->m_readbackFence[0]) != VK_SUCCESS ||
            vkCreateFence(self->m_device, &ufci, NULL,
                          &self->m_readbackFence[1]) != VK_SUCCESS)
        {
            failStage = "vkCreateFence(async-readback)";
            goto fail;
        }
    }
    XMemset(&sci, 0, sizeof(sci));
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    if (self->m_window)
    {
        int semi;
        for (semi = 0; semi < self->m_framesInFlight; ++semi)
        {
            if (vkCreateSemaphore(self->m_device, &sci, NULL,
                                  &self->m_imageReadys[semi]) != VK_SUCCESS)
            {
                failStage = "vkCreateSemaphore(imageReady)";
                goto fail;
            }
            if (vkCreateSemaphore(self->m_device, &sci, NULL,
                                  &self->m_renderDones[semi]) != VK_SUCCESS)
            {
                failStage = "vkCreateSemaphore(renderDone)";
                goto fail;
            }
        }
        self->m_imageReady = self->m_imageReadys[0];
        self->m_renderDone = self->m_renderDones[0];
    }
    if (!xvkl_create_device_objects(
            self, self->m_window ? xvkl_surface_format(self)
                                 : VK_FORMAT_B8G8R8A8_UNORM))
    {
        failStage = "createDeviceObjects";
        goto fail;
    }
    if (self->m_window)
    {
        if (!xvkl_create_swapchain(self))
        {
            failStage = "createSwapchain";
            goto fail;
        }
    }
    else if (!xvkl_create_offscreen_target(self))
    {
        fprintf(stderr, "vulkan: offscreen target failed\n");
        goto fail;
    }
    {
        int vi;
        for (vi = 0; vi < self->m_framesInFlight; ++vi)
        {
            if (!xvkl_create_host_buffer(self->m_device, self->m_physical,
                                         1024u * 1024u,
                                         &self->m_vertexBuffers[vi],
                                         &self->m_vertexMemories[vi],
                                         (void**)&self->m_vertexMappeds[vi]))
            {
                fprintf(stderr, "vulkan: vertex buffer failed\n");
                goto fail;
            }
            self->m_vertexCapacities[vi] =
                1024u * 1024u / (sizeof(float) * 8u);
        }
        self->m_vertexBuffer = self->m_vertexBuffers[0];
        self->m_vertexMapped = self->m_vertexMappeds[0];
        self->m_vertexCapacity = self->m_vertexCapacities[0];
    }
    return self;

fail:
    fprintf(stderr, "vulkan: session create failed at '%s'\n", failStage);
    XGpuRenderDriver_vulkan_procs()->sessionDestroy(self);
    return NULL;
}

static void xvkl_session_destroy(XGpuRenderDriverSession* self)
{
    uint64_t profT0;
    if (!self) return;
    profT0 = xvkl_stage_prof_on() ? XDateTime_currentNSecsSinceEpoch() : 0;
    if (self->m_device) vkDeviceWaitIdle(self->m_device);
    if (profT0) xvkl_stage_record(XvklStage_DeviceWaitIdle, profT0);
    if (xvkl_stage_prof_on()) xvkl_stage_prof_report(1);
    if (!self->m_device)
    {
        if (self->m_surface && self->m_instance)
            vkDestroySurfaceKHR(self->m_instance, self->m_surface, NULL);
        if (self->m_instance) vkDestroyInstance(self->m_instance, NULL);
        XFree_System(self);
        return;
    }
    /* Framebuffers must outlive neither their image views nor the render pass. */
    {
        uint32_t i;
        for (i = 0; i < self->m_swapCount; ++i)
            if (self->m_swapFbs[i])
                vkDestroyFramebuffer(self->m_device, self->m_swapFbs[i], NULL);
    }
    xvkl_destroy_device_objects(self);
    {
        int vi;
        for (vi = 0; vi < self->m_framesInFlight; ++vi)
        {
            if (self->m_vertexBuffers[vi])
                vkDestroyBuffer(self->m_device, self->m_vertexBuffers[vi],
                                NULL);
            if (self->m_vertexMemories[vi])
                vkFreeMemory(self->m_device, self->m_vertexMemories[vi],
                             NULL);
        }
    }
    {
        int si;
        for (si = 0; si < self->m_framesInFlight; ++si)
        {
            if (self->m_stagingBuffers[si])
                vkDestroyBuffer(self->m_device, self->m_stagingBuffers[si],
                                NULL);
            if (self->m_stagingMemories[si])
                vkFreeMemory(self->m_device, self->m_stagingMemories[si],
                             NULL);
        }
    }
    if (self->m_frameStagingBuffer)
        vkDestroyBuffer(self->m_device, self->m_frameStagingBuffer, NULL);
    if (self->m_frameStagingMemory)
        vkFreeMemory(self->m_device, self->m_frameStagingMemory, NULL);
    if (self->m_sourceView)
        vkDestroyImageView(self->m_device, self->m_sourceView, NULL);
    if (self->m_sourceImage)
        vkDestroyImage(self->m_device, self->m_sourceImage, NULL);
    if (self->m_sourceMemory)
        vkFreeMemory(self->m_device, self->m_sourceMemory, NULL);
    if (self->m_atlasView)
        vkDestroyImageView(self->m_device, self->m_atlasView, NULL);
    if (self->m_atlasImage)
        vkDestroyImage(self->m_device, self->m_atlasImage, NULL);
    if (self->m_atlasMemory)
        vkFreeMemory(self->m_device, self->m_atlasMemory, NULL);
    if (self->m_colorView)
        vkDestroyImageView(self->m_device, self->m_colorView, NULL);
    if (self->m_colorImage)
        vkDestroyImage(self->m_device, self->m_colorImage, NULL);
    if (self->m_colorMemory)
        vkFreeMemory(self->m_device, self->m_colorMemory, NULL);
    {
        uint32_t i;
        for (i = 0; i < self->m_swapCount; ++i)
            if (self->m_swapViews[i])
                vkDestroyImageView(self->m_device, self->m_swapViews[i], NULL);
    }
    if (self->m_swapchain)
        vkDestroySwapchainKHR(self->m_device, self->m_swapchain, NULL);
    if (self->m_surface)
        vkDestroySurfaceKHR(self->m_instance, self->m_surface, NULL);
    {
        int fi;
        for (fi = 0; fi < self->m_framesInFlight; ++fi)
        {
            if (self->m_imageReadys[fi])
                vkDestroySemaphore(self->m_device, self->m_imageReadys[fi],
                                   NULL);
            if (self->m_renderDones[fi])
                vkDestroySemaphore(self->m_device, self->m_renderDones[fi],
                                   NULL);
            if (self->m_frameFences[fi])
                vkDestroyFence(self->m_device, self->m_frameFences[fi],
                               NULL);
        }
    }
    {
        int fi;
        for (fi = 0; fi < self->m_framesInFlight; ++fi)
        {
            if (self->m_transferFences[fi])
                vkDestroyFence(self->m_device, self->m_transferFences[fi],
                               NULL);
        }
    }
    if (self->m_suspendFence)
        vkDestroyFence(self->m_device, self->m_suspendFence, NULL);
    {
        /* 异步读回资源：双槽 staging 与各槽 fence（readbackCmd 随
           cmdPool 销毁；入口处 vkDeviceWaitIdle 已覆盖在途提交）。 */
        uint32_t i;
        for (i = 0; i < 2; ++i)
        {
            if (self->m_readbackFence[i])
                vkDestroyFence(self->m_device, self->m_readbackFence[i],
                               NULL);
            if (self->m_readbackBuffer[i])
                vkDestroyBuffer(self->m_device, self->m_readbackBuffer[i],
                                NULL);
            if (self->m_readbackMemory[i])
                vkFreeMemory(self->m_device, self->m_readbackMemory[i],
                             NULL);
        }
    }
    if (self->m_cmdPool)
        vkDestroyCommandPool(self->m_device, self->m_cmdPool, NULL);
    if (self->m_device) vkDestroyDevice(self->m_device, NULL);
    if (self->m_instance) vkDestroyInstance(self->m_instance, NULL);
    XFree_System(self);
}

static bool xvkl_make_current(XGpuRenderDriverSession* self)
{
    return self != NULL; /* Vulkan 无 current 概念：恒真（会话即上下文）。 */
}

static void xvkl_done_current(XGpuRenderDriverSession* self)
{
    (void)self;
}

/* ==================== 帧控制与录制 ==================== */

static bool xvkl_stage_pixels(XGpuRenderDriverSession* self, size_t bytes);
static void xvkl_clear(XGpuRenderDriverSession* self, uint32_t argb);

static void xvkl_image_barrier(VkCommandBuffer cmd, VkImage image,
                               VkImageLayout oldLayout,
                               VkImageLayout newLayout,
                               VkAccessFlags srcAccess,
                               VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage,
                               VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier;
    XMemset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
}

static VkImage xvkl_frame_image(const XGpuRenderDriverSession* self)
{
    return self->m_window ? self->m_swapImages[self->m_imageIndex]
                          : self->m_colorImage;
}

static VkFramebuffer xvkl_framebuffer(const XGpuRenderDriverSession* self)
{
    return self->m_window ? self->m_swapFbs[self->m_imageIndex]
                          : self->m_swapFbs[0];
}

static VkImageLayout xvkl_frame_layout(const XGpuRenderDriverSession* self)
{
    return self->m_window ? self->m_swapLayouts[self->m_imageIndex]
                          : self->m_colorLayout;
}

static void xvkl_set_frame_layout(XGpuRenderDriverSession* self,
                                  VkImageLayout layout)
{
    if (self->m_window)
        self->m_swapLayouts[self->m_imageIndex] = layout;
    else
        self->m_colorLayout = layout;
}

/**
 * @brief      读取 present 模型二期开关 XGPU_VK_PRESENT_V2。
 * @details    默认开（V2 异步提交模型）；置 "0" 回退旧同步路径（每笔
 *             transfer 与每帧 endFrame 的全队列等待）。进程内读一次缓存。
 */
static bool xvkl_present_v2(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_PRESENT_V2");
        cached = v && v[0] == '0' && v[1] == 0 ? 0 : 1;
    }
    return cached != 0;
}

/**
 * @brief      读取帧环深度 XGPU_VK_FRAMES_IN_FLIGHT（1~3，默认 3）。
 * @details    默认 1=单槽路径（每帧 beginFrame 全 GPU 排空——当前
 *             稳态默认：K=3 帧环在 XGuiGpu_Test 交互回归中触发
 *             0xC0000005（S12 回退后仍崩），根因待 validate layers
 *             定位，回退默认保烟测；XGPU_VK_FRAMES_IN_FLIGHT=2/3 可
 *             显式启用帧环复测（第 8 轮统筹 K 扫描 K=3 曾达 30fps 手动
 *             口径）——endFrame N+1 的 CPU 录制/提交与 GPU 执行/
 *             present N 重叠；每槽独立命令缓冲/fence/信号量/顶点
 *             缓冲，信号量复用合法性由单队列提交序与本槽 fence 等待
 *             保证。非法/越界值钳到 1~3。
 */
static int xvkl_frames_in_flight(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_FRAMES_IN_FLIGHT");
        cached = 3;
        if (v && v[0])
        {
            int n = atoi(v);
            if (n < 1) n = 1;
            if (n > XGPU_VK_MAX_FRAMES_IN_FLIGHT)
                n = XGPU_VK_MAX_FRAMES_IN_FLIGHT;
            cached = n;
        }
    }
    return cached;
}

/**
 * @brief      回收在途 transfer 提交（V2：fence 等待+复位；V1：恒真）。
 * @details    V2 下所有 transferCmd reset、staging 重写/销毁与 staging
 *             CPU 读回前必须经过本入口：保证本槽上一笔 transfer 执行
 *             完毕且 fence 复位，才允许复用其资源。槽化（第 9 轮）后
 *             跨帧使用间隔 ≥K-1 帧，fence 绝大多数已信号（查询语义
 *             零阻塞）。fence 已信号时 vkWaitForFences 立即返回。
 */
static bool xvkl_transfer_retire(XGpuRenderDriverSession* self)
{
    int slot;
    uint64_t profT0;
    if (!self || !self->m_presentV2 ||
        !self->m_transferInFlight[self->m_transferSlot])
        return true;
    slot = self->m_transferSlot;
    profT0 = xvkl_stage_prof_on() ? XDateTime_currentNSecsSinceEpoch() : 0;
    if (vkWaitForFences(self->m_device, 1,
                        &self->m_transferFences[slot], VK_TRUE,
                        UINT64_MAX) != VK_SUCCESS)
        return false;
    if (profT0) xvkl_stage_record(XvklStage_TransferFenceWait, profT0);
    if (vkResetFences(self->m_device, 1,
                      &self->m_transferFences[slot]) != VK_SUCCESS)
        return false;
    self->m_transferInFlight[slot] = false;
    return true;
}

static bool xvkl_submit_transfer(XGpuRenderDriverSession* self, bool needWait)
{
    VkSubmitInfo submit;
    uint64_t profT0;
    VkResult r;
    if (!self || !self->m_transferCmd) return false;
    if (vkEndCommandBuffer(self->m_transferCmd) != VK_SUCCESS) return false;
    XMemset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &self->m_transferCmd;
    if (!self->m_presentV2)
    {
        profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        r = vkQueueSubmit(self->m_queue, 1, &submit, 0);
        if (profT0) xvkl_stage_record(XvklStage_SubmitTransfer, profT0);
        if (r != VK_SUCCESS) return false;
        profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        r = vkQueueWaitIdle(self->m_queue);
        if (profT0) xvkl_stage_record(XvklStage_QueueWaitIdle, profT0);
        return r == VK_SUCCESS;
    }
    /* V2：fence 异步提交。同队列后续提交按序执行，GPU 侧依赖（屏障、
       采样就绪）由队列序保证；CPU 侧仅在回收（retire）或本笔结果需
       立即读回（needWait）时等待，取代旧路径逐笔 vkQueueWaitIdle
       全队列同步（34ms/会话主因，2026-09-25 在册）。
       【第 12 轮回退注记（两段）】①帧内合并提交（惰性挂起+依赖点
       flush）实测 fps=0——flush 调用点未随挂起语义完整落地（教训：
       挂起语义必须与其全部 flush 点同轮交付，缺一即断供）；②双槽
       轮转+镜像装载激活后 XGuiGpu_Test/demo 0xC0000005/0xC000041D
       （transfer 槽轮转与 canvas keep 跳过/读回槽的交互在真机上踩空）。
       均已回退：槽恒 0（=S11 稳态实测 210+），槽化资产保留待带
       交互专项验证后经统筹重启。 */
    profT0 = xvkl_stage_prof_on() ? XDateTime_currentNSecsSinceEpoch() : 0;
    r = vkQueueSubmit(self->m_queue, 1, &submit, self->m_transferFence);
    if (profT0) xvkl_stage_record(XvklStage_SubmitTransfer, profT0);
    if (r != VK_SUCCESS) return false;
    self->m_transferInFlight[self->m_transferSlot] = true;
    /* needWait（结果立即读回）：retire 对准本槽（本笔）；异步路径直接
       轮转——本槽留待下次使用前 retire（彼时早已完成），跨帧等待由
       槽距离消除（S11 210.6 构成）。 */
    if (needWait && !xvkl_transfer_retire(self)) return false;
    self->m_transferSlot = (self->m_transferSlot + 1) %
                           (self->m_framesInFlight > 0 ? self->m_framesInFlight
                                                       : 1);
    self->m_transferCmd = self->m_transferCmds[self->m_transferSlot];
    self->m_transferFence = self->m_transferFences[self->m_transferSlot];
    return true;
}

static bool xvkl_begin_transfer(XGpuRenderDriverSession* self)
{
    VkCommandBufferBeginInfo begin;
    if (!self || !self->m_transferCmd) return false;
    /* V2：复位前回收在途 transfer（fence 批量查询，已信号即零开销）。 */
    if (!xvkl_transfer_retire(self)) return false;
    if (vkResetCommandBuffer(self->m_transferCmd, 0) != VK_SUCCESS)
        return false;
    XMemset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return vkBeginCommandBuffer(self->m_transferCmd, &begin) == VK_SUCCESS;
}

/** @brief XGPU_VK_COPYINIT_BATCH 开关（"0"=回退逐像素旧循环；默认
           开=GL 对齐批量路径：premul/全不透明直传+半透明快腿。第 5 轮
           栈采样实锤 begin 链初始拷贝每帧 48 万次 XImage_pixel 调用
           25-70ms；GL 同段 0.45ms 的根因即 GL 对 premul/全不透明整幅
           直传（XGpuRenderDriver_gl.c:999-1029），本路径对齐该语义）。 */
static bool xvkl_copyinit_batch(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_COPYINIT_BATCH");
        cached = v && v[0] == '0' && v[1] == 0 ? 0 : 1;
    }
    return cached != 0;
}

/** @brief XGPU_VK_CANVAS_KEEP 开关（"0"=回退每帧整幅上传旧行为；默认
           开=同 swapchain image 连续帧跳过冗余 copy_initial，第 10 轮）。 */
static bool xvkl_canvas_keep(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_CANVAS_KEEP");
        cached = v && v[0] == '0' && v[1] == 0 ? 0 : 1;
    }
    return cached != 0;
}

static bool xvkl_copy_initial_image(XGpuRenderDriverSession* self,
                                    const XImage* initialImage)
{
    size_t bytes;
    int row;
    if (!self || !initialImage || XImage_width(initialImage) != self->m_width ||
        XImage_height(initialImage) != self->m_height)
        return false;
    /* canvas keep（XGPU_VK_CANVAS_KEEP 默认开，第 10 轮）：同 swapchain
       image 连续帧的整幅上传冗余消除——IMMEDIATE 无 vsync 强制轮换、
       FBO keep-open 自持上帧最终内容、帧末 drb 脏区把 FBO 同步回
       paintImage ⇒ paintImage ≡ FBO 恒等链保持，跳过帧与上传帧的
       FBO 逐位一致；degraded/软件帧不经过本入口（painter gate），其
       CPU 写造成的失步由 streak 冻结 + 60 帧强制重同步自愈。 */
    if (xvkl_canvas_keep() &&
        self->m_keepSyncedSlot == (int)self->m_imageIndex &&
        self->m_keepStreak < 60u)
    {
        self->m_keepStreak += 1;
        return true;
    }
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    if (xvkl_copyinit_batch() &&
        (XImage_format(initialImage) == XImageFormat_ARGB32 ||
         XImage_format(initialImage) ==
             XImageFormat_ARGB32_Premultiplied))
    {
        /* GL 对齐批量路径（XGPU_VK_COPYINIT_BATCH 默认开）：
           ① premul 格式位图即预乘值——GL 对 premul initialImage 整幅
              直传（XGpuRenderDriver_gl.c:999-1029），位图内存序
              （B,G,R,A 小端）与 B8G8R8A8_UNORM staging 布局逐字节一致，
              逐行 memcpy 零函数调用零除法；
           ② ARGB32 直色先扫 alpha：全不透明时预乘=恒等
              （(255c+127)/255=c 精确）→ 同样直传；含透明 → 逐像素预乘
              数学必要（GL 同样对含半透明 ARGB32 预乘），行直读免
              48 万次 getter，a=255/a=0 快路径免绝大多数除法，其余保持
              原公式原舍入——三路与旧逐像素循环逐位一致。 */
        const uint8_t* srcBits = XImage_constBits(initialImage);
        int bpl = XImage_bytesPerLine(initialImage);
        int premulSrc = XImage_format(initialImage) ==
                        XImageFormat_ARGB32_Premultiplied;
        int opaque = premulSrc;
        if (!opaque)
        {
            for (row = 0; row < self->m_height && opaque; ++row)
            {
                const uint8_t* srow = srcBits + (size_t)row * (size_t)bpl;
                int col;
                for (col = 0; col < self->m_width; ++col)
                {
                    if (srow[col * 4 + 3] != 255u)
                    {
                        opaque = 0;
                        break;
                    }
                }
            }
        }
        if (opaque)
        {
            for (row = 0; row < self->m_height; ++row)
            {
                XMemcpy((uint8_t*)self->m_stagingMapped +
                            (size_t)row * (size_t)self->m_width * 4u,
                        srcBits + (size_t)row * (size_t)bpl,
                        (size_t)self->m_width * 4u);
            }
        }
        else
        {
            for (row = 0; row < self->m_height; ++row)
            {
                const uint8_t* srow = srcBits + (size_t)row * (size_t)bpl;
                uint8_t* dst = (uint8_t*)self->m_stagingMapped +
                    (size_t)row * (size_t)self->m_width * 4u;
                int col;
                for (col = 0; col < self->m_width; ++col)
                {
                    uint8_t b = srow[col * 4 + 0];
                    uint8_t g = srow[col * 4 + 1];
                    uint8_t r = srow[col * 4 + 2];
                    uint8_t a = srow[col * 4 + 3];
                    if (a == 255u)
                    {
                        dst[col * 4 + 0] = b;
                        dst[col * 4 + 1] = g;
                        dst[col * 4 + 2] = r;
                        dst[col * 4 + 3] = 255u;
                    }
                    else if (a == 0u)
                    {
                        dst[col * 4 + 0] = 0u;
                        dst[col * 4 + 1] = 0u;
                        dst[col * 4 + 2] = 0u;
                        dst[col * 4 + 3] = 0u;
                    }
                    else
                    {
                        /* 预乘除法消去（精确恒等，非近似）：
                           (v+127)/255 == (w + 1 + (w>>8)) >> 8，
                           w = v+127（v=c*a ≤ 65025 → w ≤ 65152，
                           h=w>>8 ≤ 254、l=w&255 → h+l ≤ 509 < 510，恒等
                           式在此域内对全部取值成立；Hacker's Delight
                           div255 技巧）——除法→移位，结果逐位一致。 */
                        unsigned vb = (unsigned)b * a + 127u;
                        unsigned vg = (unsigned)g * a + 127u;
                        unsigned vr = (unsigned)r * a + 127u;
                        dst[col * 4 + 0] =
                            (uint8_t)((vb + 1u + (vb >> 8)) >> 8);
                        dst[col * 4 + 1] =
                            (uint8_t)((vg + 1u + (vg >> 8)) >> 8);
                        dst[col * 4 + 2] =
                            (uint8_t)((vr + 1u + (vr >> 8)) >> 8);
                        dst[col * 4 + 3] = a;
                    }
                }
            }
        }
    }
    else
    {
        for (row = 0; row < self->m_height; ++row)
        {
            int col;
            uint8_t* dst = (uint8_t*)self->m_stagingMapped +
                (size_t)row * (size_t)self->m_width * 4u;
            for (col = 0; col < self->m_width; ++col)
            {
                uint32_t argb = XImage_pixel(initialImage, col, row);
                unsigned a = (argb >> 24) & 0xffu;
                dst[col * 4 + 0] =
                    (uint8_t)(((argb & 0xffu) * a + 127u) / 255u);
                dst[col * 4 + 1] =
                    (uint8_t)((((argb >> 8) & 0xffu) * a + 127u) / 255u);
                dst[col * 4 + 2] =
                    (uint8_t)((((argb >> 16) & 0xffu) * a + 127u) / 255u);
                dst[col * 4 + 3] = (uint8_t)a;
            }
        }
    }
    if (!xvkl_begin_transfer(self)) return false;
    xvkl_image_barrier(self->m_transferCmd, xvkl_frame_image(self),
                       xvkl_frame_layout(self), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    {
        VkBufferImageCopy region;
        XMemset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = (uint32_t)self->m_width;
        region.imageExtent.height = (uint32_t)self->m_height;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(self->m_transferCmd, self->m_stagingBuffer,
                               xvkl_frame_image(self),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
    }
    xvkl_image_barrier(self->m_transferCmd, xvkl_frame_image(self),
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (!xvkl_submit_transfer(self, false)) return false;
    xvkl_set_frame_layout(self, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    /* canvas keep 标记：本 image 本帧内容已与 paintImage 同步。 */
    self->m_keepSyncedSlot = (int)self->m_imageIndex;
    self->m_keepStreak = 0;
    return true;
}

static bool xvkl_prepare_frame_target(XGpuRenderDriverSession* self)
{
    if (!xvkl_begin_transfer(self)) return false;
    if (xvkl_frame_layout(self) != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        xvkl_image_barrier(self->m_transferCmd, xvkl_frame_image(self),
                           xvkl_frame_layout(self),
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                           0, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                           VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    }
    if (!xvkl_submit_transfer(self, false)) return false;
    xvkl_set_frame_layout(self, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    return true;
}

/** @brief 渲染通道实例开启（不含命令缓冲 begin；内联上传复用）：全幅
           viewport/scissor + 顶点缓冲重绑，与 suspend/resume 的 resume
           腿语义一致（剪裁由后续 setClipRect 重施）。 */
static bool xvkl_begin_render_pass_instances(XGpuRenderDriverSession* self)
{
    VkRenderPassBeginInfo rp;
    VkViewport viewport;
    VkRect2D scissor;
    if (!self) return false;
    XMemset(&rp, 0, sizeof(rp));
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = self->m_renderPass;
    rp.framebuffer = xvkl_framebuffer(self);
    rp.renderArea.extent.width = (uint32_t)self->m_width;
    rp.renderArea.extent.height = (uint32_t)self->m_height;
    vkCmdBeginRenderPass(self->m_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)self->m_width;
    viewport.height = (float)self->m_height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = (uint32_t)self->m_width;
    scissor.extent.height = (uint32_t)self->m_height;
    vkCmdSetViewport(self->m_cmd, 0, 1, &viewport);
    vkCmdSetScissor(self->m_cmd, 0, 1, &scissor);
    vkCmdBindVertexBuffers(self->m_cmd, 0, 1, &self->m_vertexBuffer,
                           (VkDeviceSize[1]){ 0 });
    return true;
}

static bool xvkl_begin_render_pass(XGpuRenderDriverSession* self)
{
    VkCommandBufferBeginInfo begin;
    if (!self) return false;
    XMemset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(self->m_cmd, &begin) != VK_SUCCESS) return false;
    return xvkl_begin_render_pass_instances(self);
}

/**
 * @brief      渲染目标转入 COLOR_ATTACHMENT_OPTIMAL（帧首绘制前置屏障）。
 * @param      cmd 目标命令缓冲（调用方已 begin）。
 * @param      image 渲染目标图像。
 * @return     无。
 */
static void xvkl_transition_color_for_draw(VkCommandBuffer cmd, VkImage image)
{
    VkImageMemoryBarrier barrier;
    XMemset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         NULL, 0, NULL, 1, &barrier);
}

/**
 * @brief      读取 acquire 超时 XGPU_VK_ACQUIRE_TIMEOUT_MS（默认 0）。
 * @details    0=vkAcquireNextImageKHR 以 UINT64_MAX 永久阻塞（旧行为）；
 *             N>0 时 acquire 最多阻塞 N 毫秒，超时（VK_TIMEOUT/
 *             VK_NOT_READY）按获取失败处理——beginFrame 返回 false，
 *             上层走降级帧路径（软件提交腿补上屏），UI 线程不被远程/
 *             虚拟显示栈的呈现节律拖住。诊断背景：wprof 显示 VK 帧
 *             250ms 缺口位于 beginFrame 的 acquire（FIFO 呈现节律），
 *             本开关与 XGPU_VK_PRESENT_MODE=immediate 二选一或叠加使用。
 */
static uint64_t xvkl_acquire_timeout_ns(void)
{
    static int64_t cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_ACQUIRE_TIMEOUT_MS");
        cached = 0;
        if (v && *v)
        {
            int ms = atoi(v);
            if (ms < 0) ms = 0;
            cached = (int64_t)ms;
        }
    }
    return cached > 0 ? (uint64_t)cached * 1000000ull : UINT64_MAX;
}

static bool xvkl_begin_frame_impl(XGpuRenderDriverSession* self,
                                  const XImage* initialImage)
{
    VkResult acquired;
    int slot;
    if (!self) return false;
    xvkl_stage_prof_tick();
    if (g_xvklObjOn) g_xvklObjProf.m_frames += 1;
    /* 当前槽镜像装载：本帧所有 m_cmd/fence/信号量/顶点访问经镜像落到
       本槽资源；fence 等待只回收本槽上一轮提交（K>1 时为 N-K 帧而非
       N-1 帧——CPU 录制与 GPU 执行/present 重叠的落点）。 */
    slot = self->m_frameSlot;
    self->m_cmd = self->m_frameCmd[slot];
    self->m_frameFence = self->m_frameFences[slot];
    if (self->m_window)
    {
        self->m_imageReady = self->m_imageReadys[slot];
        self->m_renderDone = self->m_renderDones[slot];
    }
    self->m_vertexBuffer = self->m_vertexBuffers[slot];
    self->m_vertexMapped = self->m_vertexMappeds[slot];
    self->m_vertexCapacity = self->m_vertexCapacities[slot];
    if (self->m_recording)
    {
        vkCmdEndRenderPass(self->m_cmd);
        vkEndCommandBuffer(self->m_cmd);
        self->m_recording = false;
    }
    if (!self->m_presentV2)
    {
        uint64_t profT0 = (xvkl_stage_prof_on() || xvkl_begin_prof_on())
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        vkWaitForFences(self->m_device, 1, &self->m_frameFence, VK_TRUE,
                        UINT64_MAX);
        if (profT0)
        {
            xvkl_stage_record(XvklStage_FrameFenceWait, profT0);
            g_xvklBeginProf.m_fenceNs +=
                XDateTime_currentNSecsSinceEpoch() - profT0;
        }
        vkResetFences(self->m_device, 1, &self->m_frameFence);
    }
    else
    {
        /* V2（fence 批量回收点）：上一帧 endFrame 异步提交，本帧重录
           m_cmd/复用顶点与源纹理资源前回收其 fence——回收点从 endFrame
           移到下一帧 beginFrame，CPU 记录/应用逻辑与 GPU 执行重叠，
           资源退役纪律与旧路径完全一致（录制开始时上帧必已完结）。
           未武装（上帧提交失败/首帧）直接跳过等待防死等。 */
        if (self->m_frameFenceArmed[slot])
        {
            uint64_t profT0 = (xvkl_stage_prof_on() || xvkl_begin_prof_on())
                ? XDateTime_currentNSecsSinceEpoch() : 0;
            vkWaitForFences(self->m_device, 1, &self->m_frameFence, VK_TRUE,
                            UINT64_MAX);
            if (profT0)
            {
                xvkl_stage_record(XvklStage_FrameFenceWait, profT0);
                g_xvklBeginProf.m_fenceNs +=
                    XDateTime_currentNSecsSinceEpoch() - profT0;
            }
        }
        vkResetFences(self->m_device, 1, &self->m_frameFence);
        self->m_frameFenceArmed[slot] = false;
    }
    /* 读回异步化的悬置半帧兜底：仅 fence API 失败时才可能到达此处
       （正常流程在读回内部已合并回收）。等待并复位，保证随后 m_cmd
       复位/重开时上一笔半帧提交确已完结（与旧路径"录制开始时在途
       必已完结"的复用纪律一致）。 */
    if (self->m_suspendInFlight)
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        vkWaitForFences(self->m_device, 1, &self->m_suspendFence, VK_TRUE,
                        UINT64_MAX);
        if (profT0) xvkl_stage_record(XvklStage_SuspendFenceWait, profT0);
        self->m_suspendInFlight = false;
    }
    self->m_vertexCursor = 0;
    self->m_frameStageCursor = 0;
    self->m_pendingReadback = NULL;
    self->m_imageWaitConsumed = false;
    if (self->m_window)
    {
        uint64_t profT0 = (xvkl_stage_prof_on() || xvkl_begin_prof_on())
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        acquired = vkAcquireNextImageKHR(self->m_device, self->m_swapchain,
                                         xvkl_acquire_timeout_ns(),
                                         self->m_imageReady, 0,
                                         &self->m_imageIndex);
        if (profT0)
        {
            xvkl_stage_record(XvklStage_Acquire, profT0);
            g_xvklBeginProf.m_acquireNs +=
                XDateTime_currentNSecsSinceEpoch() - profT0;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
            return false;
    }
    if (initialImage && XImage_width(initialImage) == self->m_width &&
        XImage_height(initialImage) == self->m_height)
    {
        uint64_t ciT0 = xvkl_begin_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        if (!xvkl_copy_initial_image(self, initialImage)) return false;
        if (ciT0)
            g_xvklBeginProf.m_copyNs +=
                XDateTime_currentNSecsSinceEpoch() - ciT0;
    }
    else if (xvkl_begin_prof_on())
    {
        uint64_t ppT0 = XDateTime_currentNSecsSinceEpoch();
        int okP = xvkl_prepare_frame_target(self);
        g_xvklBeginProf.m_prepNs +=
            XDateTime_currentNSecsSinceEpoch() - ppT0;
        if (!okP) return false;
    }
    else if (!xvkl_prepare_frame_target(self))
        return false;
    if (!xvkl_begin_render_pass(self)) return false;
    self->m_recording = true;
    if (!initialImage) xvkl_clear(self, 0u);
    self->m_frameSlot = (slot + 1) % self->m_framesInFlight;
    return true;
}

static bool xvkl_begin_frame(XGpuRenderDriverSession* self,
                             const XImage* initialImage)
{
    /* XGPU_VK_BEGIN_PROF：total=全函数；分段累计见 g_xvklBeginProf。 */
    uint64_t t0 = xvkl_begin_prof_on()
        ? XDateTime_currentNSecsSinceEpoch() : 0;
    bool ok = xvkl_begin_frame_impl(self, initialImage);
    if (t0)
    {
        uint64_t delta = XDateTime_currentNSecsSinceEpoch() - t0;
        g_xvklBeginProf.m_totalNs += delta;
        if (delta > g_xvklBeginProf.m_maxNs)
            g_xvklBeginProf.m_maxNs = delta;
        g_xvklBeginProf.m_count += 1;
    }
    return ok;
}

static void xvkl_end_frame(XGpuRenderDriverSession* self)
{
    VkSubmitInfo si;
    if (!self || !self->m_recording) return;
    vkCmdEndRenderPass(self->m_cmd);
    vkEndCommandBuffer(self->m_cmd);
    self->m_recording = false;
    XMemset(&si, 0, sizeof(si));
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &self->m_cmd;
    if (self->m_window && !self->m_imageWaitConsumed)
    {
        VkPipelineStageFlags waitStage =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &self->m_imageReady;
        si.pWaitDstStageMask = &waitStage;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &self->m_renderDone;
    }
    if (self->m_window)
    {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &self->m_renderDone;
    }
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        VkResult r = vkQueueSubmit(self->m_queue, 1, &si, self->m_frameFence);
        if (profT0) xvkl_stage_record(XvklStage_SubmitFrame, profT0);
        if (r != VK_SUCCESS) return;
    }
    if (!self->m_presentV2)
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        vkWaitForFences(self->m_device, 1, &self->m_frameFence, VK_TRUE,
                        UINT64_MAX);
        if (profT0) xvkl_stage_record(XvklStage_FrameFenceWait, profT0);
    }
    else
    {
        /* V2：提交即返回（武装 fence 待下一帧 beginFrame 回收），
           present 紧随提交入队（同队列有序，等待 renderDone 信号量），
           CPU 侧不再阻塞等待本帧 GPU 执行完毕——2 FPS 主因之一。 */
        self->m_frameFenceArmed[self->m_frameSlot] = true;
    }
    xvkl_set_frame_layout(self, self->m_window
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (self->m_window)
    {
        VkPresentInfoKHR pi;
        XMemset(&pi, 0, sizeof(pi));
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &self->m_renderDone;
        pi.swapchainCount = 1;
        pi.pSwapchains = &self->m_swapchain;
        pi.pImageIndices = &self->m_imageIndex;
        {
            uint64_t profT0 = xvkl_stage_prof_on()
                ? XDateTime_currentNSecsSinceEpoch() : 0;
            vkQueuePresentKHR(self->m_queue, &pi);
            if (profT0) xvkl_stage_record(XvklStage_Present, profT0);
        }
    }
    /* 帧环推进：下帧启用下一槽（提交失败/未录制的早退路径不推进，
       原槽原 fence 状态原样重试）。 */
    self->m_frameSlot = (self->m_frameSlot + 1) % self->m_framesInFlight;
}

/**
 * @brief      暂停主图形命令并等待执行完成；随后由调用方执行一次同步
 *             transfer。该路径只用于离屏会话，窗口会话必须维持 acquire /
 *             present 信号量的单次提交协议。
 */
static bool xvkl_suspend_for_transfer(XGpuRenderDriverSession* self)
{
    VkSubmitInfo submit;
    if (!self || !self->m_recording || self->m_window) return false;
    vkCmdEndRenderPass(self->m_cmd);
    if (vkEndCommandBuffer(self->m_cmd) != VK_SUCCESS) return false;
    self->m_recording = false;
    XMemset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &self->m_cmd;
    if (!self->m_presentV2)
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        VkResult r;
        r = vkQueueSubmit(self->m_queue, 1, &submit, 0);
        if (profT0) xvkl_stage_record(XvklStage_SubmitSuspend, profT0);
        if (r != VK_SUCCESS) return false;
        profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        r = vkQueueWaitIdle(self->m_queue);
        if (profT0) xvkl_stage_record(XvklStage_QueueWaitIdle, profT0);
        return r == VK_SUCCESS;
    }
    /* V2：仅等待本次半帧提交（suspend fence）——后续 transfer 与
       resume 的复位各自经 retire/fence 有序回收；m_cmd 在本 fence
       信号后复位/重开，无执行竞争。复位先行：fence 需未信号才能
       被本次提交置位。 */
    if (vkResetFences(self->m_device, 1, &self->m_suspendFence) != VK_SUCCESS)
        return false;
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        VkResult r = vkQueueSubmit(self->m_queue, 1, &submit,
                                   self->m_suspendFence);
        if (profT0) xvkl_stage_record(XvklStage_SubmitSuspend, profT0);
        if (r != VK_SUCCESS) return false;
    }
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        VkResult r = vkWaitForFences(self->m_device, 1, &self->m_suspendFence,
                                     VK_TRUE, UINT64_MAX);
        if (profT0) xvkl_stage_record(XvklStage_SuspendFenceWait, profT0);
        return r == VK_SUCCESS;
    }
}

static bool xvkl_resume_after_transfer(XGpuRenderDriverSession* self)
{
    if (!self || self->m_window) return false;
    if (vkResetCommandBuffer(self->m_cmd, 0) != VK_SUCCESS) return false;
    if (!xvkl_prepare_frame_target(self) || !xvkl_begin_render_pass(self))
        return false;
    self->m_recording = true;
    return true;
}

/**
 * @brief      把 staging 中的整帧像素拷出到目标 XImage（读回 CPU 收尾）。
 * @details    串行路径（源=m_stagingMapped）与异步读回路径（源=读回槽
 *             映射）共用本例程：ARGB32/预乘目标逐行 memcpy，其余格式
 *             逐像素去预乘。两路径像素来源与循环逐字节一致——读回异步化
 *             只改变 CPU 等待结构，不改像素通路（位一致红线）。
 */
static bool xvkl_readback_copyout(XGpuRenderDriverSession* self,
                                  XImage* target, const uint8_t* src)
{
    int x;
    int y;
    if (XImage_format(target) == XImageFormat_ARGB32 ||
        XImage_format(target) == XImageFormat_ARGB32_Premultiplied)
    {
        uint8_t* dst = XImage_bits(target);
        int bpl = XImage_bytesPerLine(target);
        if (!dst || bpl < self->m_width * 4) return false;
        for (y = 0; y < self->m_height; ++y)
        {
            uint8_t* line = dst + (size_t)y * (size_t)bpl;
            const uint8_t* row = src + (size_t)y *
                (size_t)self->m_width * 4u;
            for (x = 0; x < self->m_width; ++x)
            {
                line[x * 4 + 0] = row[x * 4 + 0];
                line[x * 4 + 1] = row[x * 4 + 1];
                line[x * 4 + 2] = row[x * 4 + 2];
                line[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        return true;
    }
    for (y = 0; y < self->m_height; ++y)
        for (x = 0; x < self->m_width; ++x)
        {
            const uint8_t* pixel = src +
                ((size_t)y * (size_t)self->m_width + (size_t)x) * 4u;
            uint8_t a = pixel[3];
            uint8_t r = a ? (uint8_t)(((unsigned)pixel[2] * 255u + a / 2u) / a) : 0;
            uint8_t g = a ? (uint8_t)(((unsigned)pixel[1] * 255u + a / 2u) / a) : 0;
            uint8_t b = a ? (uint8_t)(((unsigned)pixel[0] * 255u + a / 2u) / a) : 0;
            XImage_setPixel(target, x, y, ((uint32_t)a << 24) |
                            ((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
        }
    return true;
}

static bool xvkl_copy_frame_to_image(XGpuRenderDriverSession* self,
                                     XImage* target)
{
    size_t bytes;
    VkBufferImageCopy region;
    bool ok;
    if (!self || !target || self->m_window ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xvkl_stage_pixels(self, bytes) || !xvkl_begin_transfer(self))
        return false;
    xvkl_image_barrier(self->m_transferCmd, self->m_colorImage,
                       self->m_colorLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    XMemset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)self->m_width;
    region.imageExtent.height = (uint32_t)self->m_height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(self->m_transferCmd, self->m_colorImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           self->m_stagingBuffer, 1, &region);
    xvkl_image_barrier(self->m_transferCmd, self->m_colorImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    /* V2：本笔结果 CPU 立即读回（needWait）——仅等本笔 transfer 的
       fence，不再全队列等待；像素通路与 V1 逐位一致。 */
    if (!xvkl_submit_transfer(self, true)) return false;
    self->m_colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        ok = xvkl_readback_copyout(self, target,
                                   (const uint8_t*)self->m_stagingMapped);
        if (profT0) xvkl_stage_record(XvklStage_ReadbackCopyout, profT0);
    }
    return ok;
}

/* ==================== 读回异步化（XGPU_VK_ASYNC_READBACK） ==================== */

/*
 * 旧串行链每笔读回两次完整 CPU<->GPU 往返：
 *   ① suspend：提交半帧 + fence 同步等待（等已录绘制执行完毕）；
 *   ② 拷贝：共享上传 staging/transferCmd（stage 前的 retire 可能先等
 *      一笔仍在执行的上传提交）+ 提交后 fence 同步等待拷贝完成。
 * 异步路径（默认开；XGPU_VK_ASYNC_READBACK=0 逐字回退旧串行路径）：
 *   - 半帧异步提交（独立 suspend fence，不等待）；
 *   - 整幅拷贝录制进专用读回命令缓冲，写入双缓冲读回槽（各槽独立
 *     fence，与上传 staging/transfer 完全解耦，零互相 retire）；
 *   - 一次 vkWaitForFences 合并等待两 fence：拷贝 fence 经同队列提交
 *     序传递覆盖半帧（V2 同序约定）；同时等两 fence 对 m_cmd/staging
 *     复用给出正式的 CPU 侧保证，resume 复位即安全；
 *   - CPU 拷出走与串行路径同一例程（xvkl_readback_copyout），像素
 *     通路、布局三段式与串行路径完全一致——单帧静态画面读回逐位
 *     一致，仅 CPU 等待结构改变。
 */

/**
 * @brief      读取读回异步化开关 XGPU_VK_ASYNC_READBACK。
 * @details    默认关（2026-09-29 定版）：批量初始拷贝+IMMEDIATE 呈现落地
 *             后，串行读回路径实测反而更快（vk p0 297.3 vs 异步 210.6、
 *             p4t20 195.1 vs 111.6）——异步机器的 suspend/双槽/合并等待
 *             开销已无可摊销对象，且其状态机存在提交期 NULL 解引用崩溃
 *             （amdvlk64 内 mov rcx,[rax+8]，rax=0，栈：readback_submit_
 *             copy→vkQueueSubmit）。置 "1" 启用实验性异步路径（未修复前
 *             禁用于生产）。进程内读一次缓存。
 */
static bool xvkl_async_readback(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_ASYNC_READBACK");
        cached = v && v[0] == '1' && v[1] == 0 ? 1 : 0;
    }
    return cached != 0;
}

/** @brief 等待并复位一个读回槽的在途提交（未在途时零开销恒真）。 */
static bool xvkl_readback_slot_retire(XGpuRenderDriverSession* self,
                                      int slot)
{
    uint64_t profT0;
    if (!self->m_readbackInFlight[slot]) return true;
    profT0 = xvkl_stage_prof_on() ? XDateTime_currentNSecsSinceEpoch() : 0;
    if (vkWaitForFences(self->m_device, 1, &self->m_readbackFence[slot],
                        VK_TRUE, UINT64_MAX) != VK_SUCCESS)
        return false;
    if (profT0) xvkl_stage_record(XvklStage_ReadbackFenceWait, profT0);
    if (vkResetFences(self->m_device, 1, &self->m_readbackFence[slot]) !=
        VK_SUCCESS)
        return false;
    self->m_readbackInFlight[slot] = false;
    return true;
}

/**
 * @brief      创建读回专用缓冲（CPU 读优化三档选堆，第 11 轮）。
 * @details    优先 HOST_VISIBLE|HOST_CACHED|HOST_COHERENT（CPU 读缓存
 *             友好且免 invalidate）；次选 HOST_VISIBLE|HOST_CACHED
 *             （CPU 读缓存友好，GPU 写可见性需 fence 后
 *             xvkl_readback_invalidate 刷缓存行）；回退现状
 *             HOST_VISIBLE|HOST_COHERENT。修复 AMD APU 统一内存下
 *             第一个满足 HOST_VISIBLE|COHERENT 的堆常为 DEVICE_LOCAL
 *             DDR（CPU UNCACHED）导致 copyout 每次灾难读的成因
 *             （第 10 轮 readbackCopyout 单次 6.5-9.4ms）。所选堆
 *             经 XGPU_VK_OBJ_PROF=1 输出一次供归因。
 */
static bool xvkl_create_readback_buffer(VkDevice device,
                                        VkPhysicalDevice physical,
                                        VkDeviceSize bytes,
                                        VkBuffer* outBuffer,
                                        VkDeviceMemory* outMemory,
                                        void** outMapped,
                                        bool* outCoherent)
{
    static const VkMemoryPropertyFlags kTiers[3] =
    {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    };
    VkBufferCreateInfo bi;
    VkMemoryRequirements req;
    VkPhysicalDeviceMemoryProperties props;
    uint32_t typeCount;
    int tier;
    int picked = -1;
    uint32_t pickedFlags = 0;
    /* 崩溃防御（cdb 实锤 session_create+0x6cf 读 memoryTypes[rax]，
       rax=垃圾）：props 清零防栈垃圾；memoryTypeCount 钳制 ≤32（规范
       上限）防垃圾计数驱动循环越界；堆索引严格取自扫描循环变量
       （< typeCount ≤ 32），绝不允许位掩码/未初始化值当下标。 */
    XMemset(&props, 0, sizeof(props));
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    typeCount = props.memoryTypeCount;
    if (typeCount > 32u)
        typeCount = 32u;
    XMemset(&bi, 0, sizeof(bi));
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, NULL, outBuffer) != VK_SUCCESS)
        return false;
    vkGetBufferMemoryRequirements(device, *outBuffer, &req);
    /* 两阶段：先纯扫描选堆（不分配、不做副作用），索引严格来自
       循环变量。 */
    for (tier = 0; tier < 3 && picked < 0; ++tier)
    {
        uint32_t ti;
        for (ti = 0; ti < typeCount; ++ti)
        {
            if (!(req.memoryTypeBits & (1u << ti)))
                continue;
            if ((props.memoryTypes[ti].propertyFlags & kTiers[tier]) !=
                kTiers[tier])
                continue;
            picked = (int)ti;
            pickedFlags = props.memoryTypes[ti].propertyFlags;
            break;
        }
    }
    /* 无匹配堆/属性异常 → 回退 S11 之前的旧选择逻辑
       （HOST_VISIBLE|HOST_COHERENT 单档，xvkl_alloc_memory 旧选择器）。 */
    if (picked < 0 ||
        picked >= (int)typeCount ||
        !(req.memoryTypeBits & (1u << picked)))
    {
        if (xvkl_alloc_memory(device, physical, &req,
                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                              outMemory) &&
            vkMapMemory(device, *outMemory, 0, bytes, 0, outMapped) ==
                VK_SUCCESS)
        {
            *outCoherent = true;
            if (g_xvklObjOn)
                fprintf(stderr,
                        "[xvkl-obj] readback heap fallback "
                        "(HOST_VISIBLE|HOST_COHERENT)\n");
            xvkl_obj_hit(&g_xvklObjProf.m_buffers);
            return true;
        }
        vkDestroyBuffer(device, *outBuffer, NULL);
        *outBuffer = 0;
        return false;
    }
    /* 防御断言：typeIndex 越界一律回退（picked 已由循环保证，冗余
       防御统筹指令④）。 */
    if (picked >= (int)typeCount)
    {
        vkDestroyBuffer(device, *outBuffer, NULL);
        *outBuffer = 0;
        return false;
    }
    {
        VkMemoryAllocateInfo ai;
        XMemset(&ai, 0, sizeof(ai));
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = (uint32_t)picked;
        if (vkAllocateMemory(device, &ai, NULL, outMemory) != VK_SUCCESS ||
            vkMapMemory(device, *outMemory, 0, bytes, 0, outMapped) !=
                VK_SUCCESS)
        {
            if (*outMemory)
            {
                vkFreeMemory(device, *outMemory, NULL);
                *outMemory = 0;
            }
            vkDestroyBuffer(device, *outBuffer, NULL);
            *outBuffer = 0;
            return false;
        }
    }
    *outCoherent =
        (pickedFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    if (g_xvklObjOn)
        fprintf(stderr,
                "[xvkl-obj] readback heap type=%d coherent=%d cached=%d "
                "(flags=0x%x)\n",
                picked, *outCoherent ? 1 : 0,
                (pickedFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) != 0,
                pickedFlags);
    xvkl_obj_hit(&g_xvklObjProf.m_buffers);
    return true;
}

/** @brief 非 COHERENT 读回堆的 CPU 可见性刷（fence 后、copyout 前）。 */
static void xvkl_readback_invalidate(XGpuRenderDriverSession* self, int slot)
{
    VkMappedMemoryRange range;
    if (self->m_readbackCoherent[slot]) return;
    XMemset(&range, 0, sizeof(range));
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = self->m_readbackMemory[slot];
    range.offset = 0;
    range.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(self->m_device, 1, &range);
}

/** @brief 确保读回槽容量（惰性分配，按需扩容；不复用上传 staging）。 */
static bool xvkl_ensure_readback_slot(XGpuRenderDriverSession* self,
                                      int slot, size_t bytes)
{
    if (self->m_readbackBuffer[slot] &&
        self->m_readbackCapacity[slot] >= bytes)
        return true;
    /* 调用方已先 retire 本槽（fence 已信号），销毁重建无执行竞争。 */
    if (self->m_readbackBuffer[slot])
        vkDestroyBuffer(self->m_device, self->m_readbackBuffer[slot], NULL);
    if (self->m_readbackMemory[slot])
        vkFreeMemory(self->m_device, self->m_readbackMemory[slot], NULL);
    self->m_readbackBuffer[slot] = 0;
    self->m_readbackMemory[slot] = 0;
    self->m_readbackMapped[slot] = NULL;
    self->m_readbackCapacity[slot] = 0;
    if (!xvkl_create_readback_buffer(self->m_device, self->m_physical, bytes,
                                     &self->m_readbackBuffer[slot],
                                     &self->m_readbackMemory[slot],
                                     &self->m_readbackMapped[slot],
                                     &self->m_readbackCoherent[slot]))
        return false;
    self->m_readbackCapacity[slot] = bytes;
    return true;
}

/**
 * @brief      xvkl_suspend_for_transfer 的异步变体：半帧提交不等待。
 * @details    已录绘制必须先于读回拷贝执行（拷贝要读到它们的结果），
 *             同队列提交序保证该依赖；CPU 侧等待合并进读回 fence 的
 *             一次 vkWaitForFences。m_cmd 的复位资格由 suspend fence
 *             的信号给出（合并等待完成后），与旧路径的等待时点不同、
 *             复用纪律相同。
 */
static bool xvkl_suspend_for_transfer_async(XGpuRenderDriverSession* self)
{
    VkSubmitInfo submit;
    if (!self || !self->m_recording || self->m_window) return false;
    if (self->m_suspendInFlight) return false; /* 单悬置纪律：先回收再武装。 */
    vkCmdEndRenderPass(self->m_cmd);
    if (vkEndCommandBuffer(self->m_cmd) != VK_SUCCESS) return false;
    self->m_recording = false;
    XMemset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &self->m_cmd;
    /* 复位先行：fence 需未信号才能被本次提交置位（同旧路径注释）。 */
    if (vkResetFences(self->m_device, 1, &self->m_suspendFence) != VK_SUCCESS)
        return false;
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        VkResult r = vkQueueSubmit(self->m_queue, 1, &submit,
                                   self->m_suspendFence);
        if (profT0) xvkl_stage_record(XvklStage_SubmitSuspend, profT0);
        if (r != VK_SUCCESS) return false;
    }
    self->m_suspendInFlight = true;
    return true;
}

/**
 * @brief      在专用读回命令缓冲中录制整幅 COLOR_ATTACHMENT→
 *             TRANSFER_SRC→拷贝→COLOR_ATTACHMENT 三段式并异步提交。
 * @details    屏障序列与串行路径逐位一致；目的缓冲为双缓冲读回槽，
 *             fence 为本槽专用 fence。提交后布局簿记回到
 *             COLOR_ATTACHMENT_OPTIMAL（与串行路径一致）。
 */
static bool xvkl_readback_submit_copy(XGpuRenderDriverSession* self,
                                      int slot)
{
    VkCommandBufferBeginInfo begin;
    VkSubmitInfo submit;
    VkBufferImageCopy region;
    /* 提交对象图完整性防御（第 8 轮 cdb 实锤：vkQueueSubmit 深处 ICD
       NULL 解引用）——任何提交对象缺失/未初始化一律优雅失败（上层
       走软件回退），绝不把 NULL/半初始化对象送进 ICD。 */
    if (!self || !self->m_readbackCmd || !self->m_readbackFence[slot] ||
        !self->m_readbackBuffer[slot] || !self->m_readbackMapped[slot] ||
        !self->m_colorImage || !self->m_queue || !self->m_device)
        return false;
    XMemset(&begin, 0, sizeof(begin));
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    /* 显式归位：readbackCmd 可能处于任意前态（已 end 未 begin/未
       reset/外部破坏），显式 reset 强制可 begin 状态（统筹第一嫌疑
       的直接处置），幂等无害。 */
    if (vkResetCommandBuffer(self->m_readbackCmd, 0) != VK_SUCCESS)
        return false;
    if (vkBeginCommandBuffer(self->m_readbackCmd, &begin) != VK_SUCCESS)
        return false;
    xvkl_image_barrier(self->m_readbackCmd, self->m_colorImage,
                       self->m_colorLayout,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    XMemset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent.width = (uint32_t)self->m_width;
    region.imageExtent.height = (uint32_t)self->m_height;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(self->m_readbackCmd, self->m_colorImage,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           self->m_readbackBuffer[slot], 1, &region);
    xvkl_image_barrier(self->m_readbackCmd, self->m_colorImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    if (vkEndCommandBuffer(self->m_readbackCmd) != VK_SUCCESS)
        return false;
    XMemset(&submit, 0, sizeof(submit));
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &self->m_readbackCmd;
    if (!self->m_readbackFence[slot] || !self->m_readbackCmd ||
        !self->m_readbackBuffer[slot] || !self->m_colorImage)
        return false; /* 提交对象图防御（第 8 轮 ICD NULL AV）。 */
    if (vkResetFences(self->m_device, 1, &self->m_readbackFence[slot]) !=
        VK_SUCCESS)
        return false;
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        VkResult r = vkQueueSubmit(self->m_queue, 1, &submit,
                                   self->m_readbackFence[slot]);
        if (profT0) xvkl_stage_record(XvklStage_SubmitTransfer, profT0);
        if (r != VK_SUCCESS) return false;
    }
    self->m_readbackInFlight[slot] = true;
    self->m_colorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return true;
}

/**
 * @brief      读回后的渲染通道重开（精简 resume）。
 * @details    suspend fence 已在合并等待中信号（m_cmd 复位安全）；
 *             读回拷贝的尾段屏障已把布局簿记恢复为
 *             COLOR_ATTACHMENT_OPTIMAL，无需布局 transfer 提交——复位
 *             m_cmd 后直接重启渲染通道（loadOp=LOAD 保留已画内容），
 *             省去旧路径 resume 里的空布局提交及其 retire。命令缓冲
 *             的 vkBeginCommandBuffer 由 xvkl_begin_render_pass 独占
 *             执行（本函数不得重复 begin——RECORDING 态二次 begin 失败
 *             会把 m_cmd 置为 INVALID，后续 begin_frame 全部失效，
 *             2026-09-28 冒烟 "line session not gpu" 根因）。
 */
static bool xvkl_resume_after_readback_async(XGpuRenderDriverSession* self)
{
    if (vkResetCommandBuffer(self->m_cmd, 0) != VK_SUCCESS) return false;
    if (!xvkl_begin_render_pass(self)) return false;
    self->m_recording = true;
    return true;
}

/**
 * @brief      读回异步化主流程（XGPU_VK_ASYNC_READBACK 开时接管
 *             readback 操作表入口）。
 * @param      active 进入时帧是否在录制中（决定 suspend/resume 腿）。
 * @return     true 目标图像已含本帧整幅内容（与串行路径逐位一致）；
 *             false 失败（会话状态恢复到与串行路径失败时同构）。
 */
static bool xvkl_readback_async(XGpuRenderDriverSession* self,
                                XImage* target, bool active)
{
    VkFence waitFences[2];
    uint32_t waitCount;
    int slot;
    size_t bytes;
    bool suspended;
    bool ok;
    if (!self || !target || self->m_window ||
        XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    /* 防御：上一笔读回异常遗留的悬置半帧先回收（fence 已信号时本
       调用为查询语义），恢复 m_cmd 可复位资格再武装新半帧。 */
    if (self->m_suspendInFlight)
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        if (vkWaitForFences(self->m_device, 1, &self->m_suspendFence,
                            VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            return false;
        if (profT0) xvkl_stage_record(XvklStage_SuspendFenceWait, profT0);
        self->m_suspendInFlight = false;
    }
    slot = self->m_readbackIndex;
    bytes = (size_t)self->m_width * (size_t)self->m_height * 4u;
    if (!xvkl_readback_slot_retire(self, slot)) return false;
    if (!xvkl_ensure_readback_slot(self, slot, bytes)) return false;
    suspended = false;
    if (active)
    {
        /* 半帧异步提交（失败时会话状态与串行路径 suspend 失败同构：
           recording=false、无在途工作，直接报失败）。 */
        if (!xvkl_suspend_for_transfer_async(self)) return false;
        suspended = true;
    }
    if (!xvkl_readback_submit_copy(self, slot))
    {
        /* 拷贝提交失败：悬置半帧先回收（m_cmd 复位资格），再按串行
           路径失败口径重开渲染通道（帧继续录制）。无论 resume 成败
           一律报失败——旧串行路径在本情形的返回值恒为 false。 */
        if (suspended)
        {
            uint64_t profT0 = xvkl_stage_prof_on()
                ? XDateTime_currentNSecsSinceEpoch() : 0;
            if (vkWaitForFences(self->m_device, 1, &self->m_suspendFence,
                                VK_TRUE, UINT64_MAX) != VK_SUCCESS)
                return false;
            if (profT0)
                xvkl_stage_record(XvklStage_SuspendFenceWait, profT0);
            self->m_suspendInFlight = false;
        }
        if (active && !xvkl_resume_after_readback_async(self)) return false;
        return false;
    }
    /* 单次合并等待：读回 fence + 半帧 suspend fence（若有）。 */
    waitFences[0] = self->m_readbackFence[slot];
    waitCount = 1;
    if (suspended)
    {
        waitFences[1] = self->m_suspendFence;
        waitCount = 2;
    }
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        if (vkWaitForFences(self->m_device, waitCount, waitFences, VK_TRUE,
                            UINT64_MAX) != VK_SUCCESS)
            return false;
        if (profT0) xvkl_stage_record(XvklStage_ReadbackFenceWait, profT0);
    }
    if (vkResetFences(self->m_device, waitCount, waitFences) != VK_SUCCESS)
        return false;
    self->m_readbackInFlight[slot] = false;
    self->m_suspendInFlight = false;
    /* 非 COHERENT 读回堆：fence 后先刷 CPU 缓存行再拷出（第 11 轮）。 */
    xvkl_readback_invalidate(self, slot);
    /* CPU 拷出：与串行路径同一例程、同一布局（逐位一致）。 */
    {
        uint64_t profT0 = xvkl_stage_prof_on()
            ? XDateTime_currentNSecsSinceEpoch() : 0;
        ok = xvkl_readback_copyout(
            self, target, (const uint8_t*)self->m_readbackMapped[slot]);
        if (profT0) xvkl_stage_record(XvklStage_ReadbackCopyout, profT0);
    }
    self->m_readbackIndex = slot ^ 1;
    if (active && !xvkl_resume_after_readback_async(self)) return false;
    return ok;
}

static bool xvkl_readback(XGpuRenderDriverSession* self, XImage* target)
{
    bool active;
    bool ok;
    if (!self || !target || self->m_window) return false;
    active = self->m_recording;
    if (self->m_asyncReadback)
        return xvkl_readback_async(self, target, active);
    if (active && !xvkl_suspend_for_transfer(self)) return false;
    ok = xvkl_copy_frame_to_image(self, target);
    if (active && !xvkl_resume_after_transfer(self)) return false;
    return ok;
}

/* ==================== 原语（帧内录制） ==================== */

/**
 * @brief      写 4 顶点（NDC：Vulkan y 轴向下，ny = y*2/h - 1）并录制
 *             指定管线的 draw 调用。
 * @param      u0/v0/u1/v1 纹理子矩形（归一化）：(u0,v0) 对应四边形左上
 *             顶点、(u1,v1) 对应右下。字形图集子矩形采样必须用它——
 *             固定 0..1 会把整幅 512x512 图集（绝大部分为空）拉进字形
 *             小四边形，采样 alpha≈0 导致字形不上屏（2026-09-24 实测
 *             "atlas pixels wrong" 根因，对标 GL 驱动 glyphAtlasDraw 的
 *             atlasX/512 子矩形约定）。
 */
static bool xvkl_record_quad(XGpuRenderDriverSession* self, float x1, float y1,
                             float x2, float y2, float x3, float y3,
                             float x4, float y4, uint32_t premulColor,
                             bool sourceOver, bool textured, VkImageView view,
                             float u0, float v0, float u1, float v1)
{
    float* v;
    uint32_t first;
    unsigned i;
    VkPipeline pipeline;
    if (!self || !self->m_recording) return false;
    pipeline = textured
        ? (sourceOver ? self->m_texPipeline : self->m_texSourcePipeline)
        : (sourceOver ? self->m_solidPipeline : self->m_solidSourcePipeline);
    if (!pipeline) return false;
    if (self->m_vertexCursor + 4u > self->m_vertexCapacity) return false;
    first = self->m_vertexCursor;
    v = self->m_vertexMapped + (size_t)first * 8u;
    {
        float xs[4] = { x1, x2, x3, x4 };
        float ys[4] = { y1, y2, y3, y4 };
        for (i = 0; i < 4; ++i)
        {
            float* d = v + (size_t)i * 8u;
            d[0] = xs[i] * 2.0f / (float)self->m_width - 1.0f;
            d[1] = ys[i] * 2.0f / (float)self->m_height - 1.0f;
            d[2] = textured ? (i == 1 || i == 3 ? u1 : u0) : 0.0f;
            d[3] = textured ? (i >= 2 ? v1 : v0) : 0.0f;
            d[4] = (float)((premulColor >> 16) & 0xffu) / 255.0f;
            d[5] = (float)((premulColor >> 8) & 0xffu) / 255.0f;
            d[6] = (float)(premulColor & 0xffu) / 255.0f;
            d[7] = (float)((premulColor >> 24) & 0xffu) / 255.0f;
        }
    }
    vkCmdBindPipeline(self->m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    if (textured)
    {
        VkDescriptorSet sets[1];
        sets[0] = view == self->m_atlasView ? self->m_atlasSet
                                            : self->m_sourceSet;
        vkCmdBindDescriptorSets(self->m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                self->m_texLayout, 0, 1, sets, 0, NULL);
    }
    self->m_vertexCursor += 4u;
    vkCmdDraw(self->m_cmd, 4, 1, first, 0);
    return true;
}

static void xvkl_clear(XGpuRenderDriverSession* self, uint32_t argb)
{
    VkClearAttachment attachment;
    VkClearRect rect;
    if (!self || !self->m_recording) return;
    XMemset(&attachment, 0, sizeof(attachment));
    attachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    attachment.clearValue.color.float32[0] =
        (float)((argb >> 16) & 0xffu) / 255.0f;
    attachment.clearValue.color.float32[1] =
        (float)((argb >> 8) & 0xffu) / 255.0f;
    attachment.clearValue.color.float32[2] =
        (float)(argb & 0xffu) / 255.0f;
    attachment.clearValue.color.float32[3] =
        (float)((argb >> 24) & 0xffu) / 255.0f;
    XMemset(&rect, 0, sizeof(rect));
    rect.rect.extent.width = (uint32_t)self->m_width;
    rect.rect.extent.height = (uint32_t)self->m_height;
    rect.layerCount = 1;
    vkCmdClearAttachments(self->m_cmd, 1, &attachment, 1, &rect);
}

static void xvkl_set_clip_rect(XGpuRenderDriverSession* self, const XRect* rect)
{
    VkRect2D scissor;
    if (!self || !self->m_recording) return;
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = (uint32_t)self->m_width;
    scissor.extent.height = (uint32_t)self->m_height;
    if (rect && rect->width > 0 && rect->height > 0)
    {
        int x0 = rect->x < 0 ? 0 : rect->x;
        int y0 = rect->y < 0 ? 0 : rect->y;
        int x1 = rect->x + rect->width;
        int y1 = rect->y + rect->height;
        if (x1 > self->m_width) x1 = self->m_width;
        if (y1 > self->m_height) y1 = self->m_height;
        if (x1 > x0 && y1 > y0)
        {
            scissor.offset.x = x0;
            scissor.offset.y = y0;
            scissor.extent.width = (uint32_t)(x1 - x0);
            scissor.extent.height = (uint32_t)(y1 - y0);
        }
        else
        {
            scissor.extent.width = 0;
            scissor.extent.height = 0;
        }
    }
    vkCmdSetScissor(self->m_cmd, 0, 1, &scissor);
}

static bool xvkl_fill_rect(XGpuRenderDriverSession* self, const XRect* rect,
                           uint32_t premulColor, float opacity,
                           bool sourceOver)
{
    unsigned a;
    unsigned r;
    unsigned g;
    unsigned b;
    if (!self || !self->m_recording || !rect || rect->width <= 0 ||
        rect->height <= 0)
        return false;
    /* fillRect 入口的 color 是非预乘 ARGB（通用层原样透传，含 opacity）：
       透明度折入 alpha 后 RGB 必须按同一 alpha 预乘——管线是预乘
       SourceOver（src=ONE），非预乘源会把颜色放大 1/a 倍（实测半透明
       fillRect 输出 ff4c7298，GL 正确值 ff1c2a38；对标 GL 驱动
       xgld_fill_rect 的 xgpu_mul255 预乘）。 */
    (void)opacity; /* 透明度已折入 premulColor（下方计算）。 */
    a = (unsigned)((premulColor >> 24) & 0xffu);
    a = (unsigned)(a * (unsigned)(opacity * 255.0f + 0.5f) + 127u) / 255u;
    r = (((premulColor >> 16) & 0xffu) * a + 127u) / 255u;
    g = (((premulColor >> 8) & 0xffu) * a + 127u) / 255u;
    b = ((premulColor & 0xffu) * a + 127u) / 255u;
    premulColor = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                  ((uint32_t)g << 8) | (uint32_t)b;
    return xvkl_record_quad(
        self, (float)rect->x, (float)rect->y,
        (float)(rect->x + rect->width), (float)rect->y,
        (float)rect->x, (float)(rect->y + rect->height),
        (float)(rect->x + rect->width), (float)(rect->y + rect->height),
        premulColor, sourceOver, false, NULL, 0.0f, 0.0f, 1.0f, 1.0f);
}

static bool xvkl_draw_solid_quad(XGpuRenderDriverSession* self, float x1,
                                 float y1, float x2, float y2, float x3,
                                 float y3, float x4, float y4,
                                 uint32_t premulColor, bool sourceOver)
{
    return xvkl_record_quad(self, x1, y1, x2, y2, x3, y3, x4, y4, premulColor,
                            sourceOver, false, NULL, 0.0f, 0.0f, 1.0f, 1.0f);
}

/**
 * @brief      确保 staging 缓冲容量并把像素数据写入映射区。
 * @return     true 成功；false 扩容失败。
 */
static bool xvkl_stage_pixels(XGpuRenderDriverSession* self, size_t bytes)
{
    int slot;
    /* V2：staging 写入/销毁前回收在途 transfer——fence 已信号时本调用
       为零开销查询；在途时等待，避免 CPU 重写映射区与 GPU 读旧内容
       竞争（旧路径由逐笔 vkQueueWaitIdle 隐式保证）。槽化（第 9 轮）：
       staging 按帧槽独立，retire 只等本槽——跨帧等待由帧环距离自然
       消除；帧内同槽多次上传仍按序串行（staging 复用安全必需）。 */
    if (!xvkl_transfer_retire(self)) return false;
    slot = self->m_transferSlot;
    if (self->m_stagingBuffers[slot] &&
        self->m_stagingCapacities[slot] >= bytes)
        return true;
    if (self->m_stagingBuffers[slot])
        vkDestroyBuffer(self->m_device, self->m_stagingBuffers[slot], NULL);
    if (self->m_stagingMemories[slot])
        vkFreeMemory(self->m_device, self->m_stagingMemories[slot], NULL);
    self->m_stagingBuffers[slot] = 0;
    self->m_stagingMemories[slot] = 0;
    self->m_stagingMappeds[slot] = NULL;
    if (!xvkl_create_host_buffer(self->m_device, self->m_physical, bytes,
                                 &self->m_stagingBuffers[slot],
                                 &self->m_stagingMemories[slot],
                                 &self->m_stagingMappeds[slot]))
        return false;
    self->m_stagingCapacities[slot] = bytes;
    /* 镜像同步为当前槽（后续填充/上传走镜像引用）。 */
    self->m_stagingBuffer = self->m_stagingBuffers[slot];
    self->m_stagingMapped = self->m_stagingMappeds[slot];
    self->m_stagingCapacity = self->m_stagingCapacities[slot];
    return true;
}

/** @brief 确保源纹理存在且尺寸匹配（丢弃重建）。 */
static bool xvkl_ensure_source_image(XGpuRenderDriverSession* self, int width,
                                     int height)
{
    if (self->m_sourceImage && self->m_sourceWidth == width &&
        self->m_sourceHeight == height)
        return true;
    if (self->m_sourceView)
        vkDestroyImageView(self->m_device, self->m_sourceView, NULL);
    if (self->m_sourceImage)
        vkDestroyImage(self->m_device, self->m_sourceImage, NULL);
    if (self->m_sourceMemory)
        vkFreeMemory(self->m_device, self->m_sourceMemory, NULL);
    self->m_sourceImage = 0;
    self->m_sourceView = 0;
    self->m_sourceMemory = 0;
    self->m_sourceLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (!xvkl_create_color_image(self->m_device, self->m_physical, width,
                                 height, VK_FORMAT_B8G8R8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                                 &self->m_sourceImage, &self->m_sourceMemory,
                                 &self->m_sourceView))
        return false;
    self->m_sourceWidth = width;
    self->m_sourceHeight = height;
    xvkl_update_texture_descriptor(self, self->m_sourceSet, self->m_sourceView);
    return true;
}

/* ==================== drawImage 内联合批（XGPU_VK_UPLOAD_BATCH） ==================== */

/** @brief 读取 drawImage 上传合批开关（非 "0" 置位才开；默认关——
           2026-09-28 第 3 轮实测内联合批使 p0 16.3→12.2，已回退默认，
           三段式保持为最优路径；置 "1" 可显式复测内联）。 */
static bool xvkl_upload_batch(void)
{
    static int cached = -1;
    if (cached < 0)
    {
        const char* v = XSystem_environment("XGPU_VK_UPLOAD_BATCH");
        cached = v && *v && !(v[0] == '0' && v[1] == 0) ? 1 : 0;
    }
    return cached != 0;
}

/**
 * @brief      确保帧内内联上传 staging 容量。
 * @details    仅游标为 0 时允许扩容（已录拷贝命令引用旧缓冲，本帧中途
 *             扩容会悬空它们）；游标非 0 且容量不足时记下需求并返回
 *             false，调用方回退三段式，下帧游标 0 时按需求扩到位
 *             （稳态后本帧容量收敛，不再回退）。本帧 m_cmd 尚未提交、
 *             上帧已 fence 等待，扩容无执行竞争。
 */
static bool xvkl_frame_stage_ensure(XGpuRenderDriverSession* self,
                                    size_t bytes)
{
    size_t need;
    if (self->m_frameStagingBuffer &&
        self->m_frameStagingCapacity - self->m_frameStageCursor >= bytes)
        return true;
    if (self->m_frameStageCursor > 0)
    {
        if (self->m_frameStageDemand <
            self->m_frameStageCursor + bytes)
            self->m_frameStageDemand = self->m_frameStageCursor + bytes;
        return false;
    }
    need = bytes > self->m_frameStageDemand ? bytes : self->m_frameStageDemand;
    if (self->m_frameStagingBuffer)
    {
        vkDestroyBuffer(self->m_device, self->m_frameStagingBuffer, NULL);
        vkFreeMemory(self->m_device, self->m_frameStagingMemory, NULL);
        self->m_frameStagingBuffer = 0;
        self->m_frameStagingMemory = 0;
        self->m_frameStagingMapped = NULL;
        self->m_frameStagingCapacity = 0;
    }
    if (!xvkl_create_host_buffer(self->m_device, self->m_physical, need,
                                 &self->m_frameStagingBuffer,
                                 &self->m_frameStagingMemory,
                                 &self->m_frameStagingMapped))
        return false;
    self->m_frameStagingCapacity = need;
    self->m_frameStageDemand = 0;
    return true;
}

/**
 * @brief      帧内内联源纹理上传（XGPU_VK_UPLOAD_BATCH 合批腿）。
 * @details    在当前帧命令缓冲内：① 结束当前渲染通道实例（不结束命令
 *             缓冲，loadOp=LOAD 保留已画内容）；② UNDEFINED→TRANSFER_DST
 *             屏障 + CopyBufferToImage + →SHADER_READ 屏障（屏障序列与
 *             三段式逐位相同）；③ 重开渲染通道实例继续录制。拷贝与已录
 *             绘制在同一命令缓冲内按队列序执行——已录绘制先采样旧源内
 *             容、拷贝其次、后续绘制采样新内容，语义与
 *             suspend/upload/resume 三段式完全一致，且零额外提交、零
 *             fence 等待、零命令缓冲复位。
 */
static bool xvkl_inline_source_upload(XGpuRenderDriverSession* self,
                                      const VkBufferImageCopy* region)
{
    VkImageMemoryBarrier barrier;
    if (!self || !self->m_recording || !self->m_sourceImage) return false;
    vkCmdEndRenderPass(self->m_cmd);
    self->m_recording = false;
    /* 首屏障 src=FRAGMENT_SHADER/SHADER_READ：同帧已录绘制（上一渲染
       通道实例）可能仍采样本源纹理，拷贝写与其构成同命令缓冲内的
       WRITE-AFTER-READ——内联路径无 suspend fence 等待可借，必须以屏
       障显式声明执行/内存依赖（三段式路径由 suspend 等待提供该依赖，
       故其首屏障 src=TOP_OF_PIPE 即可，两者最终顺序语义一致）。 */
    XMemset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = self->m_sourceImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &barrier);
    vkCmdCopyBufferToImage(self->m_cmd, self->m_frameStagingBuffer,
                           self->m_sourceImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, region);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(self->m_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         NULL, 0, NULL, 1, &barrier);
    if (!xvkl_begin_render_pass_instances(self)) return false;
    self->m_recording = true;
    return true;
}

static bool xvkl_draw_image_uv(XGpuRenderDriverSession* self,
                               const XImage* image,
                               int x, int y, int width, int height,
                               float u0, float v0, float u1, float v1,
                               float opacity, bool sourceOver)
{
    VkBufferImageCopy region;
    unsigned alpha;
    uint32_t premul;
    int srcW;
    int srcH;
    size_t frameOffset = 0;
    bool inlineUp = false;
    if (!self || !self->m_recording || !image || width <= 0 || height <= 0)
        return false;
    /* UV 变体：源与目标尺寸解耦（渐变 LUT 256x1 → 任意目标矩形，
       对标 GL 驱动 xgld_draw_image_uv；整幅变体经 0..1 UV 退化为
       恒等映射，仍要求尺寸一致由包装器保证）。 */
    srcW = XImage_width(image);
    srcH = XImage_height(image);
    if (srcW <= 0 || srcH <= 0) return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    alpha = (unsigned)(opacity * 255.0f + 0.5f);
    /* drawImage 语义：opacity 同时缩放预乘 RGB 与 alpha。源图像已是预乘
       布局，CPU 侧按 alpha 缩放后经 staging 上传。 */
    {
        size_t bytes = (size_t)srcW * (size_t)srcH * 4u;
        uint8_t* mapped;
        const uint8_t* src = XImage_constBits(image);
        int bpl = XImage_bytesPerLine(image);
        int row;
        /* 内联守卫：仅当源纹理已存在且尺寸匹配（ensure_source_image
           必为无操作、不销毁重建）——尺寸变化时本帧可能有更早的内联
           绘制仍 pending 引用旧图像，销毁会悬空它们（legacy 由 suspend
           等待保护，内联须自行规避），此时回退三段式。 */
        inlineUp = xvkl_upload_batch() &&
                   self->m_sourceImage &&
                   self->m_sourceWidth == srcW &&
                   self->m_sourceHeight == srcH &&
                   xvkl_frame_stage_ensure(self, bytes);
        if (inlineUp)
        {
            frameOffset = self->m_frameStageCursor;
            self->m_frameStageCursor = frameOffset + bytes;
            mapped = (uint8_t*)self->m_frameStagingMapped + frameOffset;
        }
        else
        {
            if (!xvkl_stage_pixels(self, bytes)) return false;
            mapped = (uint8_t*)self->m_stagingMapped;
        }
        /* XImage 小端 ARGB32 内存字节序 = B,G,R,A，与 VK_FORMAT_
           B8G8R8A8_UNORM 的内存布局一致：逐字节直拷，不做 GL 那样的
           R/B 交换（移植期误留交换导致贴图红蓝互换，2026-09-24 修）。 */
        for (row = 0; row < srcH; ++row)
        {
            const uint8_t* srow = src + (size_t)row * (size_t)bpl;
            uint8_t* drow = mapped + (size_t)row * (size_t)srcW * 4u;
            XMemcpy(drow, srow, (size_t)srcW * 4u);
        }
        if (alpha != 255u)
        {
            for (row = 0; row < srcH; ++row)
            {
                uint8_t* drow = mapped + (size_t)row * (size_t)srcW * 4u;
                int col;
                for (col = 0; col < srcW; ++col)
                {
                    drow[col * 4 + 0] =
                        (uint8_t)((drow[col * 4 + 0] * alpha + 127) / 255);
                    drow[col * 4 + 1] =
                        (uint8_t)((drow[col * 4 + 1] * alpha + 127) / 255);
                    drow[col * 4 + 2] =
                        (uint8_t)((drow[col * 4 + 2] * alpha + 127) / 255);
                    drow[col * 4 + 3] =
                        (uint8_t)((drow[col * 4 + 3] * alpha + 127) / 255);
                }
            }
        }
    }
    if (!xvkl_ensure_source_image(self, srcW, srcH)) return false;
    if (inlineUp)
    {
        /* 内联合批（XGPU_VK_UPLOAD_BATCH=1 显式启用，默认关）：零额外
           提交/零等待，拷贝与已录绘制同队列按序（语义与三段式一致，
           屏障序列逐位相同，见 xvkl_inline_source_upload）。 */
        XMemset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.bufferOffset = (VkDeviceSize)frameOffset;
        region.imageExtent.width = (uint32_t)srcW;
        region.imageExtent.height = (uint32_t)srcH;
        region.imageExtent.depth = 1;
        if (!xvkl_inline_source_upload(self, &region)) return false;
    }
    else
    {
        /* 上传必须在渲染通道外执行。三段式（全部用带错误检查的现成助手）：
           ① suspend——提交并等待 m_cmd 中已录绘制（它们采样旧源内容，
           先执行才不被新上传覆盖）；② transferCmd 上传新源内容（帧命令
           缓冲不手工 reset，布局簿记由助手维护）；③ resume——重开渲染
           通道继续录制。原内联实现 submit/reset/begin 全不查返回值且
           布局簿记失效，实测帧内后续绘制全部失效（2026-09-24）。 */
        if (!xvkl_suspend_for_transfer(self)) return false;
        if (!xvkl_begin_transfer(self)) return false;
        {
            VkImageMemoryBarrier barrier;
            XMemset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = self->m_sourceImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(self->m_transferCmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                                 0, NULL, 1, &barrier);
        }
        XMemset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent.width = (uint32_t)srcW;
        region.imageExtent.height = (uint32_t)srcH;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(self->m_transferCmd, self->m_stagingBuffer,
                               self->m_sourceImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        {
            VkImageMemoryBarrier barrier;
            XMemset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = self->m_sourceImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(self->m_transferCmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 NULL, 0, NULL, 1, &barrier);
        }
        if (!xvkl_submit_transfer(self, false)) return false;
        if (!xvkl_resume_after_transfer(self)) return false;
    }
    if (!inlineUp)
    {
        /* 重新绑定源纹理描述符（图像视图内容已更新）。内联合批路径跳
           过：视图/布局不变，本次更新为等值重写，而同帧更早的内联绘制
           可能 pending 引用该描述符集（pending 期更新非法）。 */
        VkDescriptorImageInfo imageInfo;
        VkWriteDescriptorSet write;
        XMemset(&imageInfo, 0, sizeof(imageInfo));
        imageInfo.sampler = 0;
        imageInfo.imageView = self->m_sourceView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        XMemset(&write, 0, sizeof(write));
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = self->m_sourceSet;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(self->m_device, 1, &write, 0, NULL);
    }
    premul = 0xffffffffu; /* 源已含透明度（上面按 opacity 缩放过）。 */
    return xvkl_record_quad(self, (float)x, (float)y,
                            (float)(x + width), (float)y,
                            (float)x, (float)(y + height),
                            (float)(x + width), (float)(y + height),
                            premul, sourceOver, true, self->m_sourceView,
                            u0, v0, u1, v1);
}

static bool xvkl_draw_image(XGpuRenderDriverSession* self, const XImage* image,
                            int x, int y, int width, int height,
                            float opacity, bool sourceOver)
{
    /* 整幅变体：UV 全幅 0..1（与原实现一致）。 */
    return xvkl_draw_image_uv(self, image, x, y, width, height,
                              0.0f, 0.0f, 1.0f, 1.0f, opacity, sourceOver);
}

/**
 * @brief      子矩形区域绘制（对标 GL xgld_draw_image_region 的
 *             TexSubImage 增量语义）：仅把源图像 (srcX,srcY,srcW,srcH)
 *             上传到源纹理并 1:1 绘制到 (dstX,dstY)，替代整幅重传。
 *             批量脏区通道热路径——操作表此前缺本入口，882 次/5s 全部
 *             走整幅回退（XPainter painterGpuBatchFlush 的
 *             drawImageRegion→false→drawImage 整幅链路）。
 * @details    staging 行距=整幅宽×4（源内存序镜像，免紧凑重排）：
 *             行 row 写入 baseOffset+row×整幅宽×4，baseOffset=
 *             srcY×整幅宽×4+srcX×4（每行只 memcpy 子矩形 srcW×4 字节）；
 *             vkCmdCopyBufferToImage 用 bufferOffset=baseOffset、
 *             bufferRowLength=整幅宽（buffer 行距以纹素表达）、
 *             imageOffset/extent=子矩形。baseOffset 恒为 4 的倍数
 *             （整幅宽×4 与 srcX×4 均 4 对齐），满足 Vulkan 对
 *             texel block size 的对齐要求；异常布局（bits 为空或
 *             行宽<整幅宽×4）按行重排进紧凑 staging（bufferOffset=0、
 *             紧凑行距），对标 GL 逐像素回退。BGRA 内存序直拷不变
 *             （P0-3 语义）。上传走 transferCmd 三段式（同
 *             drawImageUv：suspend→transfer→resume）；屏障用
 *             UNDEFINED→TRANSFER_DST 丢弃语义——增量上传只承诺本次
 *             子矩形，且绘制仅采样本次上传区域（imageOffset 对齐
 *             1:1），跨区域无采样依赖，全幅 drawImageUv 亦总是整幅
 *             重传，故不依赖既有区域内容。XGPU_VK_REGION_DIRECT=0
 *             回退整幅路径（返回 false，调用方回退 drawImage）。
 */
static bool xvkl_draw_image_region(XGpuRenderDriverSession* self,
                                   const XImage* image, int srcX, int srcY,
                                   int srcW, int srcH, int dstX, int dstY,
                                   float opacity, bool sourceOver)
{
    static int regionDirect = -1;
    VkBufferImageCopy region;
    VkDescriptorImageInfo imageInfo;
    VkWriteDescriptorSet write;
    unsigned alpha;
    int iw;
    int ih;
    int bpl;
    int row;
    const uint8_t* src;
    uint8_t* mapped;
    size_t baseOffset;
    size_t dstPitch;
    size_t stageBytes;
    size_t frameOffset = 0;
    bool compact;
    bool inlineUp = false;
    if (regionDirect < 0)
    {
        const char* rd = XSystem_environment("XGPU_VK_REGION_DIRECT");
        regionDirect = rd && *rd && rd[0] == '0' && rd[1] == 0 ? 0 : 1;
    }
    if (!regionDirect) return false;
    if (!self || !self->m_recording || !image || srcW <= 0 || srcH <= 0)
        return false;
    iw = XImage_width(image);
    ih = XImage_height(image);
    if (iw <= 0 || ih <= 0 || srcX < 0 || srcY < 0 ||
        srcX + srcW > iw || srcY + srcH > ih)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    alpha = (unsigned)(opacity * 255.0f + 0.5f);
    src = XImage_constBits(image);
    bpl = XImage_bytesPerLine(image);
    compact = !src || bpl < iw * 4;
    if (compact)
    {
        baseOffset = 0;
        dstPitch = (size_t)srcW * 4u;
        stageBytes = dstPitch * (size_t)srcH;
    }
    else
    {
        baseOffset = (size_t)srcY * (size_t)iw * 4u + (size_t)srcX * 4u;
        dstPitch = (size_t)iw * 4u;
        stageBytes = baseOffset + dstPitch * (size_t)(srcH - 1) +
                     (size_t)srcW * 4u;
    }
    /* 内联守卫（同 drawImageUv）：尺寸匹配才内联，避免 pending 绘制
       引用的源图像被销毁重建。 */
    inlineUp = xvkl_upload_batch() &&
               self->m_sourceImage &&
               self->m_sourceWidth == iw &&
               self->m_sourceHeight == ih &&
               xvkl_frame_stage_ensure(self, stageBytes);
    if (inlineUp)
    {
        frameOffset = self->m_frameStageCursor;
        self->m_frameStageCursor = frameOffset + stageBytes;
        mapped = (uint8_t*)self->m_frameStagingMapped + frameOffset;
    }
    else
    {
        if (!xvkl_stage_pixels(self, stageBytes)) return false;
        mapped = (uint8_t*)self->m_stagingMapped;
    }
    for (row = 0; row < srcH; ++row)
    {
        uint8_t* drow = mapped + baseOffset + (size_t)row * dstPitch;
        int col;
        if (compact)
        {
            /* 异常布局：逐像素重排（ARGB32 值分解为 B,G,R,A 内存序）。 */
            for (col = 0; col < srcW; ++col)
            {
                uint32_t argb = XImage_pixel(image, srcX + col, srcY + row);
                drow[col * 4 + 0] = (uint8_t)(argb & 0xffu);
                drow[col * 4 + 1] = (uint8_t)((argb >> 8) & 0xffu);
                drow[col * 4 + 2] = (uint8_t)((argb >> 16) & 0xffu);
                drow[col * 4 + 3] = (uint8_t)((argb >> 24) & 0xffu);
            }
        }
        else
        {
            /* XImage 小端 ARGB32 内存字节序 B,G,R,A 与 B8G8R8A8_UNORM
               一致：逐行直拷（源 stride=整幅宽），不做 R/B 交换。 */
            XMemcpy(drow,
                    src + (size_t)(srcY + row) * (size_t)bpl +
                        (size_t)srcX * 4u,
                    (size_t)srcW * 4u);
        }
        /* drawImage 语义：opacity CPU 侧缩放预乘 RGB 与 alpha（管线无
           modulate，同 drawImageUv）。 */
        if (alpha != 255u)
        {
            for (col = 0; col < srcW; ++col)
            {
                drow[col * 4 + 0] =
                    (uint8_t)((drow[col * 4 + 0] * alpha + 127) / 255);
                drow[col * 4 + 1] =
                    (uint8_t)((drow[col * 4 + 1] * alpha + 127) / 255);
                drow[col * 4 + 2] =
                    (uint8_t)((drow[col * 4 + 2] * alpha + 127) / 255);
                drow[col * 4 + 3] =
                    (uint8_t)((drow[col * 4 + 3] * alpha + 127) / 255);
            }
        }
    }
    if (!xvkl_ensure_source_image(self, iw, ih)) return false;
    if (inlineUp)
    {
        /* 内联合批（XGPU_VK_UPLOAD_BATCH=1 显式启用，默认关）：零额外
           提交/零等待，子矩形 region 与三段式逐位一致，仅 bufferOffset
           改指帧私有 staging 的本笔游标处（非紧凑模式 bufferRowLength
           保留）。 */
        XMemset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.bufferOffset = (VkDeviceSize)(frameOffset + baseOffset);
        if (!compact)
            region.bufferRowLength = (uint32_t)iw;
        region.imageOffset.x = srcX;
        region.imageOffset.y = srcY;
        region.imageExtent.width = (uint32_t)srcW;
        region.imageExtent.height = (uint32_t)srcH;
        region.imageExtent.depth = 1;
        if (!xvkl_inline_source_upload(self, &region)) return false;
    }
    else
    {
        /* transferCmd 三段式（同 drawImageUv）：suspend 提交已录绘制，
           transferCmd 上传子矩形，resume 重开渲染通道继续录制。 */
        if (!xvkl_suspend_for_transfer(self)) return false;
        if (!xvkl_begin_transfer(self)) return false;
        {
            VkImageMemoryBarrier barrier;
            XMemset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = self->m_sourceImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(self->m_transferCmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                                 0, NULL, 1, &barrier);
        }
        XMemset(&region, 0, sizeof(region));
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        if (compact)
        {
            region.imageOffset.x = srcX;
            region.imageOffset.y = srcY;
        }
        else
        {
            /* 非紧凑：staging 镜像源布局（行距=整幅宽×4），子矩形起点由
               bufferOffset 寻址（恒 4 对齐），bufferRowLength 以纹素表达
               buffer 行距。 */
            region.bufferOffset = (VkDeviceSize)baseOffset;
            region.bufferRowLength = (uint32_t)iw;
            region.imageOffset.x = srcX;
            region.imageOffset.y = srcY;
        }
        region.imageExtent.width = (uint32_t)srcW;
        region.imageExtent.height = (uint32_t)srcH;
        region.imageExtent.depth = 1;
        vkCmdCopyBufferToImage(self->m_transferCmd, self->m_stagingBuffer,
                               self->m_sourceImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                               &region);
        {
            VkImageMemoryBarrier barrier;
            XMemset(&barrier, 0, sizeof(barrier));
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = self->m_sourceImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(self->m_transferCmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                                 NULL, 0, NULL, 1, &barrier);
        }
        if (!xvkl_submit_transfer(self, false)) return false;
        if (!xvkl_resume_after_transfer(self)) return false;
    }
    if (!inlineUp)
    {
        /* 重新绑定源纹理描述符（同 drawImageUv，视图内容已更新）。内联
           路径跳过：等值重写且 pending 期更新非法（同 drawImageUv）。 */
        XMemset(&imageInfo, 0, sizeof(imageInfo));
        imageInfo.sampler = 0;
        imageInfo.imageView = self->m_sourceView;
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        XMemset(&write, 0, sizeof(write));
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = self->m_sourceSet;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(self->m_device, 1, &write, 0, NULL);
    }
    /* UV：子矩形按图像坐标归一化（顶行在 v=0，无翻转），1:1 绘制到
       (dstX,dstY)——drawImageRegion 的宽高=srcW/srcH。源已含透明度。 */
    return xvkl_record_quad(self, (float)dstX, (float)dstY,
                            (float)(dstX + srcW), (float)dstY,
                            (float)dstX, (float)(dstY + srcH),
                            (float)(dstX + srcW), (float)(dstY + srcH),
                            0xffffffffu, sourceOver, true,
                            self->m_sourceView,
                            (float)srcX / (float)iw,
                            (float)srcY / (float)ih,
                            (float)(srcX + srcW) / (float)iw,
                            (float)(srcY + srcH) / (float)ih);
}

static bool xvkl_draw_alpha_bitmap(XGpuRenderDriverSession* self,
                                   const uint8_t* alpha, int width,
                                   int height, int stride, int x, int y,
                                   uint32_t premulColor, float opacity,
                                   bool sourceOver)
{
    /* 覆盖图经通用层展开不足——此处直接以覆盖度构造 RGBA 灰度再复用
       drawImage 的上传绘制（premulColor 折入顶点色）。 */
    size_t bytes = (size_t)width * (size_t)height * 4u;
    uint8_t* gray;
    XImage proxy;
    int row;
    bool ok;
    if (!self || !self->m_recording || !alpha || width <= 0 || height <= 0 ||
        stride < width)
        return false;
    gray = (uint8_t*)XMalloc_System(bytes);
    if (!gray) return false;
    for (row = 0; row < height; ++row)
    {
        const uint8_t* src = alpha + (size_t)row * (size_t)stride;
        uint8_t* dst = gray + (size_t)row * (size_t)width * 4u;
        int col;
        for (col = 0; col < width; ++col)
        {
            dst[col * 4 + 0] = premulColor & 0xffu;         /* B */
            dst[col * 4 + 1] = (premulColor >> 8) & 0xffu;  /* G */
            dst[col * 4 + 2] = (premulColor >> 16) & 0xffu; /* R */
            dst[col * 4 + 3] = src[col];                    /* A=覆盖度 */
        }
    }
    XImage_init_ex(&proxy, width, height, XImageFormat_ARGB32);
    if (!XImage_isNull(&proxy))
    {
        XMemcpy(XImage_bits(&proxy), gray, bytes);
        ok = xvkl_draw_image(self, &proxy, x, y, width, height, 1.0f,
                             sourceOver);
    }
    XImage_deinit_base(&proxy);
    XFree_System(gray);
    return ok;
}

static bool xvkl_glyph_atlas_upload(XGpuRenderDriverSession* self,
                                    const uint8_t* coverage, int width,
                                    int height, int atlasX, int atlasY)
{
    size_t bytes = (size_t)width * (size_t)height * 4u;
    uint8_t* mapped;
    VkBufferImageCopy region;
    VkImageMemoryBarrier barrier;
    int row;
    if (!self || !coverage || width <= 0 || height <= 0 || atlasX < 0 ||
        atlasY < 0 || atlasX + width > XGPU_RENDER_GLYPH_ATLAS_SIZE ||
        atlasY + height > XGPU_RENDER_GLYPH_ATLAS_SIZE)
        return false;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    mapped = (uint8_t*)self->m_stagingMapped;
    for (row = 0; row < height; ++row)
    {
        const uint8_t* src = coverage + (size_t)row * (size_t)width;
        uint8_t* dst = mapped + (size_t)row * (size_t)width * 4u;
        int col;
        for (col = 0; col < width; ++col)
        {
            dst[col * 4] = src[col];
            dst[col * 4 + 1] = src[col];
            dst[col * 4 + 2] = src[col];
            dst[col * 4 + 3] = src[col];
        }
    }
    if (!self->m_atlasImage &&
        !xvkl_create_color_image(self->m_device, self->m_physical,
                                 XGPU_RENDER_GLYPH_ATLAS_SIZE,
                                 XGPU_RENDER_GLYPH_ATLAS_SIZE,
                                 VK_FORMAT_B8G8R8A8_UNORM,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                                 &self->m_atlasImage, &self->m_atlasMemory,
                                 &self->m_atlasView))
        return false;
    /* 图集描述符集必须随图像建立即写入——m_atlasSet 在分配后从未更新
       就被 record_quad 绑定采样，内容未定义（lavapipe SIGSEGV 最强
       候选，2026-09-24 复核确认高危；此后视图不变，无需重复更新）。 */
    xvkl_update_texture_descriptor(self, self->m_atlasSet, self->m_atlasView);
    /* 图集上传走专用 transfer 命令缓冲（独立提交；旧路径阻塞等待，
       V2 异步提交——字形上传频率低，回收点在后续 retire）。不得触碰
       m_cmd：帧中（m_recording=true，drawGlyphAlpha
       首字形触发）m_cmd 正在录制、已录命令缓冲 reset 即丢弃本帧全部
       已录绘制并进入非录制态 UB（2026-09-24 实测 fill/drawImage 全部
       消失的根因；复核确认高危）。录制中提交 transferCmd 合法——m_cmd
       尚未提交，无执行竞争；后续字形绘制经描述符采样已就绪的图集。 */
    if (!xvkl_begin_transfer(self)) return false;
    XMemset(&barrier, 0, sizeof(barrier));
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = self->m_atlasImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(self->m_transferCmd,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL,
                         1, &barrier);
    XMemset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = atlasX;
    region.imageOffset.y = atlasY;
    region.imageExtent.width = (uint32_t)width;
    region.imageExtent.height = (uint32_t)height;
    region.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(self->m_transferCmd, self->m_stagingBuffer,
                           self->m_atlasImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    {
        VkImageMemoryBarrier toRead = barrier;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(self->m_transferCmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             NULL, 0, NULL, 1, &toRead);
    }
    if (!xvkl_submit_transfer(self, false)) return false;
    return true;
}

static bool xvkl_glyph_atlas_draw(XGpuRenderDriverSession* self, int atlasX,
                                  int atlasY, int width, int height, int x,
                                  int y, uint32_t premulColor, bool sourceOver)
{
    if (!self || width <= 0 || height <= 0 || atlasX < 0 || atlasY < 0)
        return false;
    /* 图集子矩形采样（atlasX/512..(atlasX+width)/512，对标 GL 驱动
       glyphAtlasDraw 的 UV 约定）：record_quad 此前恒用 0..1 全幅 UV，
       把整幅图集拉进字形小四边形，采样到的 alpha 近乎处处为 0，字形
       不上屏（"atlas pixels wrong" 根因）。 */
    return xvkl_record_quad(self, (float)x, (float)y,
                            (float)(x + width), (float)y,
                            (float)x, (float)(y + height),
                            (float)(x + width), (float)(y + height),
                            premulColor, sourceOver, true, self->m_atlasView,
                            (float)atlasX / (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
                            (float)atlasY / (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
                            (float)(atlasX + width) /
                                (float)XGPU_RENDER_GLYPH_ATLAS_SIZE,
                            (float)(atlasY + height) /
                                (float)XGPU_RENDER_GLYPH_ATLAS_SIZE);
}

static bool xvkl_glyph_atlas_readback(XGpuRenderDriverSession* self,
                                      int atlasX, int atlasY, int atlasWidth,
                                      int atlasHeight, uint8_t* outCoverage)
{
    size_t bytes = (size_t)atlasWidth * (size_t)atlasHeight * 4u;
    VkBufferImageCopy region;
    int y;
    if (!self || atlasX < 0 || atlasY < 0 || atlasWidth <= 0 ||
        atlasHeight <= 0 || !outCoverage || !self->m_atlasImage)
        return false;
    if (!xvkl_stage_pixels(self, bytes)) return false;
    /* 同 glyph_atlas_upload：走 transferCmd，不触碰录制中的 m_cmd。 */
    if (!xvkl_begin_transfer(self)) return false;
    XMemset(&region, 0, sizeof(region));
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset.x = atlasX;
    region.imageOffset.y = atlasY;
    region.imageExtent.width = (uint32_t)atlasWidth;
    region.imageExtent.height = (uint32_t)atlasHeight;
    region.imageExtent.depth = 1;
    vkCmdCopyImageToBuffer(self->m_transferCmd, self->m_atlasImage,
                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           self->m_stagingBuffer, 1, &region);
    /* V2：读回需等待本笔 transfer 完成（needWait）——范围仅本笔。 */
    if (!xvkl_submit_transfer(self, true)) return false;
    for (y = 0; y < atlasHeight; ++y)
    {
        const uint8_t* src = (const uint8_t*)self->m_stagingMapped +
            (size_t)y * (size_t)atlasWidth * 4u;
        uint8_t* dst = outCoverage + (size_t)y * (size_t)atlasWidth;
        XMemcpy(dst, src, (size_t)atlasWidth);
    }
    return true;
}

static bool xvkl_present_to_window(XGpuRenderDriverSession* self)
{
    (void)self;
    return true; /* present 已在 endFrame 内执行（swapchain 模型）。 */
}

/* ==================== 驱动操作表 ==================== */

/*
 * Vulkan 的会话没有 OpenGL 那样的 current/uncurrent 操作，
 * 但仍通过统一操作表暴露完整生命周期。resize 暂留 NULL：通用层
 * 会在窗口尺寸变化时销毁并重建 Vulkan 会话，避免在当前简化的
 * swapchain 生命周期中留下悬挂的 framebuffer/view。
 */
static bool xvkl_upload_target_image(XGpuRenderDriverSession* self,
                                     const XImage* image);

static const XGpuRenderDriverProcs g_xvklProcs =
{
    .available = xvkl_available,
    .sessionCreate = xvkl_session_create,
    .sessionDestroy = xvkl_session_destroy,
    .makeCurrent = xvkl_make_current,
    .doneCurrent = xvkl_done_current,
    .beginFrame = xvkl_begin_frame,
    .endFrame = xvkl_end_frame,
    .resize = NULL,
    .presentToWindow = xvkl_present_to_window,
    .readback = xvkl_readback,
    .clear = xvkl_clear,
    .setClipRect = xvkl_set_clip_rect,
    .fillRect = xvkl_fill_rect,
    .drawImage = xvkl_draw_image,
    .drawImageUv = xvkl_draw_image_uv,
    .drawImageRegion = xvkl_draw_image_region,
    .drawAlphaBitmap = xvkl_draw_alpha_bitmap,
    .glyphAtlasUpload = xvkl_glyph_atlas_upload,
    .glyphAtlasDraw = xvkl_glyph_atlas_draw,
    .drawSolidQuad = xvkl_draw_solid_quad,
    .glyphAtlasReadback = xvkl_glyph_atlas_readback,
    .uploadTargetImage = xvkl_upload_target_image
};

/**
 * @brief      录制中即时读回：打断渲染通道并提交已录命令，拷贝渲染
 *             目标到 staging 后按原布局恢复，重新开始渲染通道与命令
 *             录制（后续原语继续追加；已画内容经 LOAD 保留）。
 */
static bool xvkl_upload_target_image(XGpuRenderDriverSession* self,
                                     const XImage* image)
{
    bool resumeRecording;
    if (!self || !image || XImage_width(image) != self->m_width ||
        XImage_height(image) != self->m_height)
        return false;
    /* 帧中同步上传：先提交已有绘制，再复用帧首上传的规范 transfer
       序列（COLOR_ATTACHMENT -> TRANSFER_DST -> COLOR_ATTACHMENT）。
       上传完成后恢复 render pass，后续绘制仍追加到本帧。 */
    resumeRecording = self->m_recording;
    if (resumeRecording && !xvkl_suspend_for_transfer(self)) return false;
    if (!xvkl_copy_initial_image(self, image)) return false;
    if (resumeRecording && !xvkl_resume_after_transfer(self)) return false;
    return true;
}

const XGpuRenderDriverProcs* XGpuRenderDriver_vulkan_procs(void)
{
    return &g_xvklProcs;
}

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON && XINYUE_C_HAS_VULKAN */
