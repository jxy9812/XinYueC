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
/* P-PBO（2026-09-26）异步读回：像素打包缓冲 + 缓冲映射 + fence 同步。 */
#define XGL_PIXEL_PACK_BUFFER       0x88EBu
#define XGL_STREAM_READ             0x88E9u
#define XGL_READ_ONLY               0x88B8u
#define XGL_PACK_ROW_LENGTH         0x0D06u
#define XGL_SYNC_GPU_COMMANDS_COMPLETE 0x9117u
#define XGL_SYNC_FLUSH_COMMANDS_BIT    0x00000001u
#define XGL_ALREADY_SIGNALED        0x911Au
#define XGL_CONDITION_SATISFIED     0x911Cu

typedef void (XGLAPI *XglGenObjectsProc)(XglSizei, XglUInt*);
typedef void (XGLAPI *XglDeleteObjectsProc)(XglSizei, const XglUInt*);

/* P-PBO：GLsync 为不透明句柄；glClientWaitSync 超时单位为 64 位纳秒。 */
typedef void* XglSync;
typedef uint64_t XglUint64;
typedef void* (XGLAPI *XglMapBufferProc)(XglEnum, XglEnum);
typedef XglBoolean (XGLAPI *XglUnmapBufferProc)(XglEnum);
typedef XglSync (XGLAPI *XglFenceSyncProc)(XglEnum, XglBitfield);
typedef XglEnum (XGLAPI *XglClientWaitSyncProc)(XglSync, XglBitfield,
                                                XglUint64);
typedef void (XGLAPI *XglDeleteSyncProc)(XglSync);

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
       XGPU_QUAD_BATCH=0 退回逐 quad 即时（排障开关）。
       夜四 FULLBATCH（2026-09-26）：入批面已覆盖纯色 fillRect/
       drawSolidQuad/字形图集 quad（emit_solid_quad4 与 draw_quad_uv
       缓存纹理支路即批入口），逐原语残量=追加期固定 GL 调用（每 quad
       glActiveTexture+glBindTexture+glBindBuffer）。XGPU_FULLBATCH=0
       回退逐 quad 绑定现状；默认开=追加期零 GL 调用，绑定/布局统一
       推迟到冲批一次性完成（正确性依赖既有冲批纪律：全部旁路出口
       先冲批，批存续期间 unit0 绑定与 ARRAY_BUFFER 绑定无旁路改动）。 */
    XglUInt m_batchProgram;                  /**< 批 program（frag=texture2D*v_color）。 */
    XglInt m_batchSamplerLocation;           /**< 批程序 u_texture。 */
    XglUInt m_whiteTexture;                  /**< 1×1 白纹理（纯色 quad 采样恒 1）。 */
    XglUInt m_quadBatchTex;                  /**< 批内当前纹理（0=空批）。 */
    float* m_quadBatch;                      /**< 批顶点数组（拥有；48 float/quad）。 */
    int m_quadBatchCount;                    /**< 待冲批 quad 数。 */
    int m_quadBatchCapacity;                 /**< 批容量（quad 数）。 */
    int m_quadBatchBlend;                    /**< 批内混合态（-1 空 / 0 Source / 1 SourceOver）。 */
    /* 夜五 P-FLUSHGATE：批 scissor 快照——首个 quad 入批时冻结的 scissor
       缓存态（见 xgld_append_quad）。批内全部 quad 均在该 scissor 下记录
       （冲批纪律：批存续期间改 scissor 的出口一律先冲批），目标 scissor
       与快照相同即免冲批直接续批（xgld_set_clip_rect 门）。 */
    bool m_quadBatchScissorValid;            /**< 批 scissor 快照可信（首 quad 入批时按缓存置位）。 */
    bool m_quadBatchScissorOn;               /**< 批 scissor 快照：启用态。 */
    int m_quadBatchScissorX;                 /**< 批 scissor 快照 x（与 m_scissorX 同坐标系）。 */
    int m_quadBatchScissorY;                 /**< 批 scissor 快照 y。 */
    int m_quadBatchScissorW;                 /**< 批 scissor 快照宽。 */
    int m_quadBatchScissorH;                 /**< 批 scissor 快照高。 */
    /* prof（XGPU_PROFILE）：实际冲批（真实 drawArrays 落盘）次数与累计
       耗时；present 打点对齐 [profile] 既有列后清零（口径=每 300 派发）。 */
    uint32_t m_flushQuadCount;               /**< 实际冲批次数。 */
    uint64_t m_flushQuadUs;                  /**< 实际冲批累计耗时（µs）。 */
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

    /* L-固定成本（2026-09-26 夜三）逐派发固定成本削减：跨帧状态镜像。
     * GL 的 FBO 绑定（READ/DRAW）、viewport、PACK_ALIGNMENT、scissor
     * 均为按上下文留存的状态——doneCurrent/makeCurrent 往返不清零；
     * 本文件是全部 GL 调用的唯一入口（系统头隔离纪律），无外部旁路
     * 改状态，会话级镜像即真值。派发序列（begin/end/present）此前每
     * 次无条件重设这批状态并多付一次冗余 makeCurrent，构成 ~0.59ms/
     * 派发的会话级固定成本主体。逐项开关（默认开，=0 回退旧行为）：
     * XGPU_FBO_PERSIST end_frame 不解绑 FBO；XGPU_STATE_CACHE
     * begin_frame 冗余状态跳过+冗余二次 makeCurrent 消除；
     * XGPU_PRESENT_LEAN present blit 旁路混合往返（blit 不受混合态
     * 作用）；makeCurrent 消除另由 XGPU_MAKECURRENT_ONCE 独立门控
     * （与 begin/present 相关，见各入口注释）。均按会话持有（上下文
     * 切换经会话隔离无串扰），calloc 清零=「未知，首次必真调」。 */
    bool m_fboBindingKnown;                  /**< READ/DRAW FBO 绑定镜像是否可信。 */
    XglUInt m_boundFboRead;                  /**< 镜像：READ_FRAMEBUFFER 绑定。 */
    XglUInt m_boundFboDraw;                  /**< 镜像：DRAW_FRAMEBUFFER 绑定。 */
    bool m_viewportKnown;                    /**< viewport 镜像是否可信。 */
    int m_viewportW;                         /**< 镜像 viewport 宽。 */
    int m_viewportH;                         /**< 镜像 viewport 高。 */
    int m_packAlignment;                     /**< 镜像 PACK_ALIGNMENT（0=未知，
                                                  GL 合法值为 1/2/4/8）。 */

    /* E-F 路（2026-09-27 夜七）flush 段每冲批重复状态削减：UNPACK_*
     * 像素解包状态同为按上下文留存，本文件是唯一设值入口（区域直传
     * set(iw)/归 0 与各上传点前置归 0 全部经镜像助手），镜像即真值。
     * calloc 清零：m_unpackAlignment=0 ≠ GL 默认 4（0 作「未知」哨兵，
     * 首次必真调，保守正确）；m_unpackRowLength=0 恰为 GL 默认（同值
     * 跳过不早于真实状态）。XGPU_FLUSH_LEAN=0 时助手恒真调（逐位旧
     * 行为），镜像照常维护。 */
    int m_unpackAlignment;                   /**< 镜像 UNPACK_ALIGNMENT（0=未知）。 */
    int m_unpackRowLength;                   /**< 镜像 UNPACK_ROW_LENGTH（GL 默认 0）。 */

    /* TEMP-PROBE(XGPU_BATCH_LEAN，2026-09-27)：unit0 纹理绑定镜像。
     * batchDraw 剖析实测（XGPU_BATCH_PROF）冲批点纹理重绑属高频冗余
     * 状态调用：本文件是 glActiveTexture/glBindTexture 唯一入口（同
     * L-固定成本/E-F 路的会话镜像纪律，doneCurrent 往返不清零，上下文
     * 切换经会话隔离无串扰），全部直改点经 xgld_note_tex0_bind 同步
     * 镜像——冲批点同值即免 glActiveTexture+glBindTexture 两次真调。
     * XGPU_BATCH_LEAN=0 恒真调（逐位旧行为），镜像照常维护。 */
    XglUInt m_boundTex0;                     /**< 镜像：unit0 TEXTURE_2D 绑定名。 */
    bool m_boundTex0Valid;                   /**< 镜像可信（纹理删除等旁路失效）。 */

    /* P-A（2026-09-25）drawImage 纹理身份缓存：静态层大图免每帧整幅
       重传。GL 纹理按上下文命名空间隔离，本表按会话持有（与 FBO/源
       纹理同纪），session destroy 冲批后配对释放。XGPU_TEX_IDENTITY_
       CACHE=1 启用（默认关）。 */
    XgpuTexIdentityEntry m_identityCache[XGPU_TEX_IDENTITY_CACHE_SIZE];
    uint64_t m_identityStamp;                /**< LRU 单调时钟。 */
    XgpuTexChurnEntry m_identityChurn[XGPU_TEX_IDENTITY_CHURN_SIZE];
                                             /**< P-A2 换版跟随表（无 GL 资源）。 */

    /* P-IPU（XGPU_IMAGE_PREMUL_UPLOAD）：ARGB32 全不透明判定缓存
       {指针,内容版本,判定}。全不透明图像预乘=恒等（a=255 通道不变），
       判定命中即免逐像素转换走既有直传快路径（静态层逐帧上传零新增
       成本）；版本变化/换图即重扫。calloc 清零即空表。 */
    const XImage* m_opaqueScanImage[4];
    uint32_t m_opaqueScanVersion[4];
    uint8_t m_opaqueScanVerdict[4];          /**< 1=全不透明 0=含半透明。 */

    /* P-PBO（2026-09-26）双 PBO 异步读回：消除同步 glReadPixels 的
       CPU 停顿（GPU 增量每帧 ~1.36ms 的头号归因候选）。双缓冲 1 帧
       滞后——本帧读回异步写入 PBO[cur]（CPU 不等待），随即 map 上一
       帧的 PBO[prev] 拷出，呈现内容滞后一帧（60Hz 呈现下不可感知）。
       PBO 恒为全帧 RGBA 布局（PACK_ROW_LENGTH=m_width + (glY,x) 基址
       偏移），脏区逐帧漂移时拷出侧按本帧 bbox 对位取上一帧全帧图；
       m_pboBbox 记录各槽写入时的 bbox，本帧 bbox 超出其范围（首帧/
       漂移出界）即回退同步直读本帧（正确性优先）。XGPU_PBO_READBACK=0
       整链回退同步路径；创建于会话建立、销毁于会话销毁（配对纪律）。 */
    XglUInt m_readbackPbo[2];                /**< 读回 PBO 对（0=未建）。 */
    int m_readbackPboCur;                    /**< 本帧写入槽（0/1 轮转）。 */
    int m_readbackPboValid[2];               /**< 槽内已有一次完整读回写入。 */
    int m_pboBbox[2][4];                     /**< 槽写入时 bbox（x,y,w,h）。 */
    XglSync m_pboFence[2];                   /**< 槽读回 fence（NULL=无）。 */
    bool m_pboReady;                         /**< PBO 资源就绪（map 可用）。 */

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

    /* P-PBO（2026-09-26）异步读回函数：map/unmap 缺失时整链禁用
       （m_pboReady=false 走同步读回）；fence 三件套可选（GL<3.2 时
       为 NULL，跳过探测直接 map，最多阻塞一帧 DMA，正确性不变）。 */
    XglMapBufferProc glMapBuffer;
    XglUnmapBufferProc glUnmapBuffer;
    XglFenceSyncProc glFenceSync;
    XglClientWaitSyncProc glClientWaitSync;
    XglDeleteSyncProc glDeleteSync;

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

/* ==================== L-固定成本（2026-09-26 夜三）状态镜像辅助 ==================== */

/* 逐项排障开关（沿排障开关先例：默认开=新路径，"0"=逐位回退旧行为，
 * 其余非空值视为开）。固定成本归因（docs/xgui/night3-attribution.md
 * §5）：~0.59ms/派发 × 每迭代 2.18-3.62 次派发的会话级开销，头号
 * 假设=会话开关驱动成本（makeCurrent/绑定往返），本组开关即其逐项
 * 兑现与 A/B 计量手段。 */
static bool xgld_fbo_persist_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_FBO_PERSIST");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

static bool xgld_state_cache_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_STATE_CACHE");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

static bool xgld_makecurrent_once_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_MAKECURRENT_ONCE");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

static bool xgld_present_lean_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_PRESENT_LEAN");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/* P-SPR（2026-09-28）纯色 quad 预乘不变量修复开关（沿排障开关先例：
 * 默认开=修复生效，"0"=回退不修复）。域=drawSolidQuad 入口颜色。
 * 回退：XGPU_SOLID_PREMUL_REPAIR=0。 */
static bool xgld_premul_repair_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_SOLID_PREMUL_REPAIR");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/* E-F 路（2026-09-27 夜七）flush 段削减开关（缺省=开；"0"=逐位回退）：
 * 域=drawImageRegion 脏区直传每冲批重复的像素解包状态机调用
 * （UNPACK_ALIGNMENT 恒 1 重设 + UNPACK_ROW_LENGTH set(iw)/归 0 往返）。
 * 与 XGPU_STATE_CACHE 同口径，但独立成族——冲批是每帧高频路径，
 * 回退/对照需与 begin/present 的固定成本开关解耦。 */
static bool xgld_flush_lean_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_FLUSH_LEAN");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/* FBO 绑定镜像：绑定按上下文留存且本文件是唯一改绑入口，镜像即真值
 * （例外：session destroy 删除在绑 FBO 会隐式解绑，但随后即毁上下文，
 * 无后续 GL）。冗余绑定跳过在四种开关组合下都只省「目标相同的真
 * no-op 调用」，GL 状态流逐位不变——持久化本身只由 XGPU_FBO_PERSIST
 * 门控（end_frame 是否解绑），镜像维护不依赖开关。 */
/* ==================== TEMP-PROBE(XGPU_BATCH_PROF)：batchDraw 段驱动侧子计数 ====================
 * 任务：拆解 batchDraw 段（XPainter.c 九段计时最大项：0.1335ms/次、
 * 每帧 1 次、62% 墙钟）的内部构成。口径说明：batchDraw 本体=批内命令
 * 软件光栅重入（XPainter.c painterGpuSubmitSoftwareCommandRect 临时
 * 关断 m_gpuActive 后重入 drawCommand，画到 ARGB32 暂存画布）——重入
 * 期间零驱动调用，故本探针对【驱动侧同帧 CPU 路径】四维量化，用于
 * 证明/证伪「batchDraw 残差在驱动」：
 *   cmds       = 驱动原语入口被调次数（fillRect/drawImage 系/solidQuad/
 *                glyphAtlas/clip/clear——「命令解码」层）；
 *   quads      = xgld_append_quad 追加 quad 数与顶点字节数（192B/quad）；
 *   vboWrite   = 冲批 glBufferData（顶点缓冲写入，孤儿化重分配）耗时/次数；
 *   drawArrays = 冲批 glDrawArrays 落盘耗时/次数；imm=即时路径 16 float
 *                glBufferData 次数（固定 64B，不计时）；
 *   state      = 真发 GL 调用的状态切换次数（blend/program/texBind/
 *                layout/scissor 五类；同值跳过的冗余切换不计）。
 * XGPU_BATCH_PROF=1 开（建议与 XGPU_PROF=1/XGPU_PAINTER_PROF=1 同开），
 * 5s 窗口 stderr 一条汇总（tick 挂 present，口径对齐 [xpainter-prof]）。
 * 探针只读不改行为；退役时删除本块与各打点行（均带 TEMP-PROBE 标）。 */
static bool xgld_batch_prof_enabled(void)
{
    static int on = -1;
    if (on < 0)
    {
        const char* env = XSystem_environment("XGPU_BATCH_PROF");
        on = env && *env && !(env[0] == '0' && env[1] == 0) ? 1 : 0;
    }
    return on != 0;
}

static struct XgldBatchProf
{
    uint64_t m_windowStartNs; /**< 5s 窗口起点。 */
    bool m_headerPrinted;     /**< 标题已打印。 */
    uint32_t m_cmdDecode;     /**< 命令解码次数（驱动原语入口被调）。 */
    uint32_t m_quadAppend;    /**< quad 追加次数。 */
    uint64_t m_quadBytes;     /**< quad 追加字节数（192B/quad）。 */
    uint32_t m_vboWrite;      /**< 冲批 glBufferData 次数。 */
    uint64_t m_vboWriteNs;    /**< 冲批 glBufferData 累计（ns）。 */
    uint32_t m_drawArrays;    /**< 冲批 glDrawArrays 次数。 */
    uint64_t m_drawArraysNs;  /**< 冲批 glDrawArrays 累计（ns）。 */
    uint32_t m_immWrite;      /**< 即时路径 xgpu_vertex_data 次数。 */
    uint32_t m_stBlend;       /**< 混合态真切换次数。 */
    uint32_t m_stProgram;     /**< program 真切换次数。 */
    uint32_t m_stTexBind;     /**< unit0 纹理真绑定次数。 */
    uint32_t m_stLayout;      /**< 顶点布局真切换次数。 */
    uint32_t m_stScissor;     /**< scissor 真改写次数。 */
    uint32_t m_texBindSkip;   /**< LEAN 同值跳过的纹理绑定次数（收益）。 */
} g_xgldBatchProf;

/* TEMP-PROBE：命令解码打点（原语入口一行式）。 */
#define XGLD_BATCH_PROF_CMD() \
    do { if (xgld_batch_prof_enabled()) \
             ++g_xgldBatchProf.m_cmdDecode; } while (0)

/** @brief TEMP-PROBE 5s 窗口汇总（present 打点，逐窗口清零）。 */
static void xgld_batch_prof_tick(void)
{
    uint64_t now;
    if (!xgld_batch_prof_enabled()) return;
    now = XDateTime_currentNSecsSinceEpoch();
    if (!g_xgldBatchProf.m_headerPrinted)
    {
        fprintf(stderr, "[TEMP-PROBE batch-prof] segments=cmds/quads(bytes)/"
                "vboWrite/drawArrays/imm/state(blend,prog,texBind,layout,"
                "scissor,texSkip) (ms 总量 & ms/次)\n");
        g_xgldBatchProf.m_headerPrinted = true;
        g_xgldBatchProf.m_windowStartNs = now;
        return;
    }
    if (now - g_xgldBatchProf.m_windowStartNs < 5000000000u) return;
    {
        double secs = (double)(now - g_xgldBatchProf.m_windowStartNs) / 1e9;
        fprintf(stderr,
                "[TEMP-PROBE batch-prof] %.1fs cmds=%u quads=%u (%.1fKB) "
                "vboWrite=%u (%.3fms %.4f/次) drawArrays=%u (%.3fms %.4f/次) "
                "imm=%u st: blend=%u prog=%u texBind=%u(省%u) layout=%u "
                "scissor=%u\n",
                secs,
                g_xgldBatchProf.m_cmdDecode,
                g_xgldBatchProf.m_quadAppend,
                (double)g_xgldBatchProf.m_quadBytes / 1024.0,
                g_xgldBatchProf.m_vboWrite,
                (double)g_xgldBatchProf.m_vboWriteNs / 1e6,
                g_xgldBatchProf.m_vboWrite
                    ? (double)g_xgldBatchProf.m_vboWriteNs /
                          (double)g_xgldBatchProf.m_vboWrite / 1e6
                    : 0.0,
                g_xgldBatchProf.m_drawArrays,
                (double)g_xgldBatchProf.m_drawArraysNs / 1e6,
                g_xgldBatchProf.m_drawArrays
                    ? (double)g_xgldBatchProf.m_drawArraysNs /
                          (double)g_xgldBatchProf.m_drawArrays / 1e6
                    : 0.0,
                g_xgldBatchProf.m_immWrite,
                g_xgldBatchProf.m_stBlend,
                g_xgldBatchProf.m_stProgram,
                g_xgldBatchProf.m_stTexBind,
                g_xgldBatchProf.m_texBindSkip,
                g_xgldBatchProf.m_stLayout,
                g_xgldBatchProf.m_stScissor);
        g_xgldBatchProf.m_cmdDecode = 0;
        g_xgldBatchProf.m_quadAppend = 0;
        g_xgldBatchProf.m_quadBytes = 0;
        g_xgldBatchProf.m_vboWrite = 0;
        g_xgldBatchProf.m_vboWriteNs = 0;
        g_xgldBatchProf.m_drawArrays = 0;
        g_xgldBatchProf.m_drawArraysNs = 0;
        g_xgldBatchProf.m_immWrite = 0;
        g_xgldBatchProf.m_stBlend = 0;
        g_xgldBatchProf.m_stProgram = 0;
        g_xgldBatchProf.m_stTexBind = 0;
        g_xgldBatchProf.m_stLayout = 0;
        g_xgldBatchProf.m_stScissor = 0;
        g_xgldBatchProf.m_texBindSkip = 0;
        g_xgldBatchProf.m_windowStartNs = now;
    }
}

/* TEMP-PROBE(XGPU_BATCH_LEAN)：unit0 绑定镜像同步——旁路直改点统一走
 * 这里（冲批点经 xgld_bind_texture0 的镜像命中免真调）。调用方契约：
 * 执行时 active unit 必为 TEXTURE0（全文件不变式，gradient unit1 段落
 * 是直线代码、内无旁路观察者，出段即复位 TEXTURE0）。 */
static void xgld_note_tex0_bind(XGpuRenderDriverSession* self, XglUInt texture)
{
    self->m_boundTex0 = texture;
    self->m_boundTex0Valid = true;
}

static void xgld_bind_fbo_read(XGpuRenderDriverSession* self, XglUInt fbo)
{
    if (self->m_fboBindingKnown && self->m_boundFboRead == fbo) return;
    self->glBindFramebuffer(XGL_READ_FRAMEBUFFER, fbo);
    self->m_boundFboRead = fbo;
    self->m_fboBindingKnown = true;
}

static void xgld_bind_fbo_draw(XGpuRenderDriverSession* self, XglUInt fbo)
{
    if (self->m_fboBindingKnown && self->m_boundFboDraw == fbo) return;
    self->glBindFramebuffer(XGL_DRAW_FRAMEBUFFER, fbo);
    self->m_boundFboDraw = fbo;
    self->m_fboBindingKnown = true;
}

/* target=XGL_FRAMEBUFFER：一次调用同时设定 READ/DRAW（GL 语义），
 * 镜像两者。 */
static void xgld_bind_fbo(XGpuRenderDriverSession* self, XglUInt fbo)
{
    if (self->m_fboBindingKnown &&
        self->m_boundFboRead == fbo && self->m_boundFboDraw == fbo)
        return;
    self->glBindFramebuffer(XGL_FRAMEBUFFER, fbo);
    self->m_boundFboRead = fbo;
    self->m_boundFboDraw = fbo;
    self->m_fboBindingKnown = true;
}

/* viewport 镜像：本驱动全部视口同值 (0,0,m_width,m_height)（尺寸固定
 * 于会话创建，无中途 resize 路径），跳过只发生在同值重设上。
 * XGPU_STATE_CACHE=0 时恒真调（旧行为），镜像照常维护。 */
static void xgld_set_viewport(XGpuRenderDriverSession* self, int width,
                              int height)
{
    if (xgld_state_cache_enabled() && self->m_viewportKnown &&
        self->m_viewportW == width && self->m_viewportH == height)
        return;
    self->glViewport(0, 0, width, height);
    self->m_viewportKnown = true;
    self->m_viewportW = width;
    self->m_viewportH = height;
}

/* PACK_ALIGNMENT 镜像：begin_frame 设 1、读回两路设 4，逐派发来回
 * 翻转；同值跳过（XGPU_STATE_CACHE 门控，=0 恒真调）。 */
static void xgld_set_pack_alignment(XGpuRenderDriverSession* self, int align)
{
    if (xgld_state_cache_enabled() && self->m_packAlignment == align) return;
    self->glPixelStorei(XGL_PACK_ALIGNMENT, align);
    self->m_packAlignment = align;
}

/* E-F 路（2026-09-27 夜七）UNPACK_ALIGNMENT 镜像：本驱动 12 处上传点
 * 全部恒设 1（无一例外），逐次上传重设属白付状态机往返——上传是
 * 每冲批必经路径（flush 段主体），镜像同值跳过后每冲批省 1 次真调。
 * XGPU_FLUSH_LEAN=0 恒真调（旧行为），镜像照常维护；calloc 清零=0
 * 非 GL 任何合法值，作「未知」哨兵（首次必真调，保守正确）。 */
static void xgld_set_unpack_alignment(XGpuRenderDriverSession* self, int align)
{
    if (xgld_flush_lean_enabled() && self->m_unpackAlignment == align) return;
    self->glPixelStorei(XGL_UNPACK_ALIGNMENT, align);
    self->m_unpackAlignment = align;
}

/* E-F 路（2026-09-27 夜七）UNPACK_ROW_LENGTH 镜像：设值点唯一
 * （drawImageRegion 区域直传设行距 iw）；行距语义约束「源缓冲行宽
 * ≠ 上传宽度」的读入——行距==上传宽度（整幅上传）时设 0/设宽等价，
 * 故 LEAN 下区域直传不再每次归 0，改为各上传消费点前置显式归 0
 * （同值跳过：非区域流零新增调用，区域流后首个非同宽上传 1 次归 0）。
 * XGPU_FLUSH_LEAN=0 恒真调（含区域路径显式归 0=逐位旧行为）。 */
static void xgld_set_unpack_row_length(XGpuRenderDriverSession* self,
                                       int length)
{
    if (xgld_flush_lean_enabled() && self->m_unpackRowLength == length) return;
    self->glPixelStorei(XGL_UNPACK_ROW_LENGTH, length);
    self->m_unpackRowLength = length;
}

/* scissor 关闭走 P0-2 缓存：与 set_clip_rect 共用同一真值源（启用态
 * +矩形）。begin/present 的旁路禁用不再粗暴失效整个缓存——上帧末
 * scissor 已关时本帧连 glDisable 都省，setClipRect 同矩形早返回得以
 * 跨帧生效（待定批跨帧存续的前提之一）。XGPU_STATE_CACHE=0 时恒真
 * 调 glDisable 并按旧行为置缓存无效。 */
static void xgld_ensure_scissor_off(XGpuRenderDriverSession* self)
{
    if (xgld_state_cache_enabled() && self->m_scissorValid &&
        !self->m_scissorOn)
        return;
    self->glDisable(XGL_SCISSOR_TEST);
    self->m_scissorOn = false;
    self->m_scissorValid = xgld_state_cache_enabled();
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_stScissor; /* TEMP-PROBE：真改写计数。 */
    /* P-FLUSHGATE：旁路关剪裁使批 scissor 快照失真（快照可能为启用态）。
       常规路径本函数只在空批（冲批后）的 begin/present 触达，置位无副作用；
       非空批罕见边界（如跨帧残留）下快照失效即下个 setClipRect 按目标
       ≠快照兜底冲批，门不放过失真批次。 */
    self->m_quadBatchScissorValid = false;
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
static bool xgld_pbo_readback_enabled(void);
static bool xgld_full_batch_enabled(void);

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

/* ==================== P-IPU（XGPU_IMAGE_PREMUL_UPLOAD）直通格式预乘上传 ==================== */

/** @brief XGPU_IMAGE_PREMUL_UPLOAD 环境开关（缺省开=修复生效，
 *         "0"=回退「ARGB32 与预乘同布局直传」旧行为）。
 *  @details XImage 契约：ARGB32=非预乘，ARGB32_Premultiplied=预乘。
 *           GL 管线为预乘混合（ONE, ONE_MINUS_SRC_ALPHA），上传必须
 *           预乘字节。既有实现把两格式当同一布局直传，对 ARGB32 直通
 *           内容（GPU 批量画布 g_gpuBatchCanvas 的 putPixel 写入）等
 *           于把直通 RGB 当预乘再超加一次——半透明内容 G/B 饱和溢出
 *           （night4 面积图斜边笔 GPU (150,255,255) vs SW (125,210,
 *           207) 实测，撕裂协议主差异簇之一）。 */
static bool xgld_image_premul_upload_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_IMAGE_PREMUL_UPLOAD");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/** @brief ARGB32 全不透明判定（带 {指针,内容版本} 4 槽缓存）。
 *  @return 1=全不透明（预乘=恒等，可走既有直传快路径），0=含半透明
 *          （需预乘转换）。判定为纯优化，任何返回值不改变正确性。 */
static int xgld_argb32_opaque_verdict(XGpuRenderDriverSession* self,
                                      const XImage* image)
{
    const uint8_t* src;
    int w;
    int h;
    int bpl;
    uint32_t version;
    int i;
    int slot;
    int x;
    int y;
    if (!self || !image) return 0;
    version = XImage_contentVersion(image);
    slot = -1;
    for (i = 0; i < 4; ++i)
    {
        if (self->m_opaqueScanImage[i] == image)
        {
            if (self->m_opaqueScanVersion[i] == version)
                return self->m_opaqueScanVerdict[i];
            slot = i; /* 同图换版：原槽重扫。 */
            break;
        }
        if (self->m_opaqueScanImage[i] == NULL && slot < 0)
            slot = i;
    }
    if (slot < 0) slot = 0; /* 表满：覆盖 0 号（判定为纯优化）。 */
    src = XImage_constBits(image);
    w = XImage_width(image);
    h = XImage_height(image);
    bpl = XImage_bytesPerLine(image);
    if (!src || bpl < w * 4 || w <= 0 || h <= 0)
    {
        /* 无法安全扫描：按含半透明处理（走转换，正确性优先）。 */
        self->m_opaqueScanImage[slot] = NULL;
        return 0;
    }
    for (y = 0; y < h; ++y)
    {
        const uint8_t* srow = src + (size_t)y * (size_t)bpl;
        for (x = 0; x < w; ++x)
        {
            if (srow[(size_t)x * 4u + 3u] != 255u)
            {
                self->m_opaqueScanImage[slot] = image;
                self->m_opaqueScanVersion[slot] = version;
                self->m_opaqueScanVerdict[slot] = 0;
                return 0;
            }
        }
    }
    self->m_opaqueScanImage[slot] = image;
    self->m_opaqueScanVersion[slot] = version;
    self->m_opaqueScanVerdict[slot] = 1;
    return 1;
}

/** @brief 该次上传是否需要把 ARGB32 直通字节预乘（P-IPU）。
 *  @details 仅 ARGB32（非预乘格式）且开关开且图含半透明时为真；
 *           ARGB32_Premultiplied 与全不透明 ARGB32 恒假（既有直传
 *           路径逐字节不变）。 */
static int xgld_upload_needs_premul(XGpuRenderDriverSession* self,
                                    const XImage* image)
{
    if (!xgld_image_premul_upload_enabled()) return 0;
    if (XImage_format(image) != XImageFormat_ARGB32) return 0;
    return !xgld_argb32_opaque_verdict(self, image);
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
    int uploadPremul;
    if (!self || !image || texture == 0 || width <= 0 || height <= 0 ||
        XImage_width(image) < width || XImage_height(image) < height)
        return false;
    bytes = (size_t)width * (size_t)height * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    /* P-IPU：ARGB32（非预乘格式）且含半透明 → 上传前预乘（全不透明
       与预乘格式走既有直传，逐字节不变）。 */
    uploadPremul = xgld_upload_needs_premul(self, image);
    /* 快速路径：ARGB32_Premultiplied（及全不透明 ARGB32，预乘=恒等）
       与预乘布局一致，逐行直拷并做 R/B 交换（ARGB32 小端 B,G,R,A →
       GL 上传字节序 R,G,B,A），避免逐像素 XImage_pixel/mul255 的函数
       调用开销（520x360 静态场景每帧约 19 万像素）。 */
    if (!flipY && !uploadPremul &&
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
            xgld_note_tex0_bind(self, texture); /* TEMP-PROBE 镜像。 */
            xgld_set_unpack_alignment(self, 1);
            /* E-F 路：LEAN 下区域直传可能残留行距 iw——行距≠宽度的整幅
               读入依赖归 0，非同宽上传点前置显式归 0（镜像同值跳过，
               非区域流零新增 GL 调用）。 */
            xgld_set_unpack_row_length(self, 0);
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
                    /* P-IPU：ARGB32 直通内容按 a 预乘（a=255 逐字节不
                       变）；预乘格式直通。 */
                    uint8_t a = srow[x * 4 + 3];
                    if (uploadPremul)
                    {
                        drow[x * 4 + 0] = xgpu_mul255((unsigned)srow[x * 4 + 2], a);
                        drow[x * 4 + 1] = xgpu_mul255((unsigned)srow[x * 4 + 1], a);
                        drow[x * 4 + 2] = xgpu_mul255((unsigned)srow[x * 4 + 0], a);
                    }
                    else
                    {
                        drow[x * 4 + 0] = srow[x * 4 + 2]; /* R <- B */
                        drow[x * 4 + 1] = srow[x * 4 + 1]; /* G */
                        drow[x * 4 + 2] = srow[x * 4 + 0]; /* B <- R */
                    }
                    drow[x * 4 + 3] = a; /* A */
                }
            }
            self->glBindTexture(XGL_TEXTURE_2D, texture);
            xgld_note_tex0_bind(self, texture); /* TEMP-PROBE 镜像。 */
            xgld_set_unpack_alignment(self, 1);
            xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底。 */
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
    xgld_note_tex0_bind(self, texture); /* TEMP-PROBE 镜像。 */
    xgld_set_unpack_alignment(self, 1);
    /* E-F 路：LEAN 下区域直传可能残留行距 iw——暂存缓冲行宽=width，
       行距≠宽度的读入依赖归 0（镜像同值跳过，零新增调用）。 */
    xgld_set_unpack_row_length(self, 0);
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
    xgld_note_tex0_bind(self, self->m_sourceTexture); /* TEMP-PROBE 镜像。 */
    xgld_set_unpack_alignment(self, 1);
    xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底。 */
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
    xgld_note_tex0_bind(self, texture); /* TEMP-PROBE 镜像。 */
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_MIN_FILTER, XGL_NEAREST);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_MAG_FILTER, XGL_NEAREST);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_WRAP_S, XGL_CLAMP_TO_EDGE);
    self->glTexParameteri(XGL_TEXTURE_2D, XGL_TEXTURE_WRAP_T, XGL_CLAMP_TO_EDGE);
    xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底（NULL 数据
                                            不读源，统一量测口径）。 */
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
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_stBlend; /* TEMP-PROBE：真切换计数。 */
}

/** @brief program 切换缓存（连续同类 quad 免重复 glUseProgram）。 */
static void xgpu_use_program(XGpuRenderDriverSession* self, XglUInt program)
{
    if (self->m_activeProgram == program) return;
    self->glUseProgram(program);
    self->m_activeProgram = program;
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_stProgram; /* TEMP-PROBE：真切换计数。 */
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
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_immWrite; /* TEMP-PROBE：即时路径写入。 */
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
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_stLayout; /* TEMP-PROBE：真切换计数。 */
}

/** @brief XGPU_BATCH_LEAN 环境开关（缺省=开；"0"=逐位回退）。
 *  @details TEMP-PROBE 拆解转微优化：冲批/字形流路径 unit0 纹理同值
 *           重绑（glActiveTexture+glBindTexture）经会话镜像命中跳过。
 *           仅省冗余 GL 真调，绑定序列的可见结果逐位不变；镜像维护
 *           与开关解耦（关时恒真调，镜像照常更新）。 */
static bool xgld_batch_lean_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_BATCH_LEAN");
        enabled = !(value && *value &&
                    !(value[0] == '0' && value[1] == 0));
    }
    return enabled != 0;
}

/** @brief unit0 纹理绑定。
 *  TEMP-PROBE→XGPU_BATCH_LEAN（缺省开，"0"=逐位回退）：镜像命中（同
 *  纹理已绑 unit0）免 glActiveTexture+glBindTexture 两次真调——冲批
 *  点（xgld_flush_quads）恒绑本批纹理，纯色 quad 流（白纹理）与连续
 *  字形流（同图集）跨冲批同值，逐次重设属冗余状态机往返（batch-prof
 *  实测维度）。镜像由 xgld_note_tex0_bind 在全部旁路直改点同步维护，
 *  纹理删除点失效；仅省冗余真调，不改绑定序列的可见结果。 */
static void xgld_bind_texture0(XGpuRenderDriverSession* self, XglUInt texture)
{
    if (xgld_batch_lean_enabled() && self->m_boundTex0Valid &&
        self->m_boundTex0 == texture)
    {
        if (xgld_batch_prof_enabled())
            ++g_xgldBatchProf.m_texBindSkip; /* TEMP-PROBE：省掉的重绑。 */
        return;
    }
    self->glActiveTexture(XGL_TEXTURE0);
    self->glBindTexture(XGL_TEXTURE_2D, texture);
    xgld_note_tex0_bind(self, texture);
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_stTexBind; /* TEMP-PROBE：真绑定计数。 */
}

/** @brief XGPU_PROFILE 环境开关（同 present 打点口径：非空即开）。
 *  @note  开启时冲批点累计次数+耗时入会话 prof 域，present 每 300 派发
 *         打印 flush=次数（每次均值）列后清零；关闭零额外开销。 */
static bool xgld_profile_enabled(void)
{
    static int on = -1;
    if (on < 0)
    {
        const char* env = XSystem_environment("XGPU_PROFILE");
        on = env && *env ? 1 : 0;
    }
    return on != 0;
}

/** @brief 冲批：累积 quad 一次 drawArrays 提交（退化三角带连接）。
 *  @note  冲批点=采样纹理切换/混合切换/scissor 变更/帧界（endFrame/
 *         present/readback/uploadTarget/clear）以及上传即变形源纹理的
 *         即时型原语（drawImage 系/drawAlphaBitmap）之前。 */
static void xgld_flush_quads(XGpuRenderDriverSession* self)
{
    int totalVerts;
    int64_t profT0;
    uint64_t profVbo0 = 0;
    uint64_t profDraw0 = 0;
    bool profBatch = false;
    if (!self || self->m_quadBatchCount == 0) return;
    profT0 = xgld_profile_enabled()
                 ? XDateTime_currentNSecsSinceEpoch() / 1000 : 0;
    totalVerts = self->m_quadBatchCount * 6;
    xgpu_set_blend(self, self->m_quadBatchBlend != 0);
    xgpu_use_program(self, self->m_batchProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgld_set_attrib_layout(self, 1);
    xgld_bind_texture0(self, self->m_quadBatchTex);
    profBatch = xgld_batch_prof_enabled();
    if (profBatch)
        profVbo0 = XDateTime_currentNSecsSinceEpoch(); /* TEMP-PROBE。 */
    self->glBufferData(XGL_ARRAY_BUFFER,
                       (XglSizeiptr)(sizeof(float) * 48u *
                                     (size_t)self->m_quadBatchCount),
                       self->m_quadBatch, XGL_DYNAMIC_DRAW);
    if (profBatch)
    {
        ++g_xgldBatchProf.m_vboWrite; /* TEMP-PROBE：顶点缓冲写入。 */
        g_xgldBatchProf.m_vboWriteNs +=
            XDateTime_currentNSecsSinceEpoch() - profVbo0;
        profDraw0 = XDateTime_currentNSecsSinceEpoch();
    }
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, (XglSizei)totalVerts);
    if (profBatch)
    {
        ++g_xgldBatchProf.m_drawArrays; /* TEMP-PROBE：落盘提交。 */
        g_xgldBatchProf.m_drawArraysNs +=
            XDateTime_currentNSecsSinceEpoch() - profDraw0;
    }
    self->m_quadBatchCount = 0;
    self->m_quadBatchBlend = -1;
    self->m_quadBatchTex = 0;
    if (profT0)
    {
        ++self->m_flushQuadCount;
        self->m_flushQuadUs +=
            (uint64_t)(XDateTime_currentNSecsSinceEpoch() / 1000 - profT0);
    }
}

/** @brief 批状态就绪：纹理/混合与批内不同则先冲批（跨字形存续的
 *         关键——同纹理同混合的连续 quad 不再被纹理型原语打散）。
 *         FULLBATCH（默认开）：绑定推迟到冲批（批内存续期间 unit0
 *         绑定无旁路改动，见 xgld_full_batch_enabled 注），后续同批
 *         quad 免逐次 glActiveTexture+glBindTexture。 */
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
    if (xgld_full_batch_enabled()) return;
    xgld_bind_texture0(self, texture);
}

/** @brief 排障开关：XGPU_QUAD_DEGEN_LEGACY=1 回退桥接顶点旧偏移
 *         （24，实读 TR），复现楔形撕裂对照用；默认关=修后行为。 */
static bool xgld_quad_degen_legacy(void)
{
    static int legacy = -1;
    if (legacy < 0)
    {
        const char* value = XSystem_environment("XGPU_QUAD_DEGEN_LEGACY");
        legacy = value && *value &&
                         !(value[0] == '0' && value[1] == 0) ? 1 : 0;
    }
    return legacy != 0;
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
    /* FULLBATCH（默认开）：追加只写 CPU 侧批数组——VBO 绑定与批布局
       指针由冲批点（xgld_flush_quads：先 glBindBuffer 再
       set_attrib_layout(1)，指针捕获在绑 VBO）一次性设定，追加期
       逐 quad 两调全免。布局此后可能被即时型原语改回 0（drawImage
       系），冲批点按缓存差值重设，语义不变。 */
    if (!xgld_full_batch_enabled())
    {
        self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
        xgld_set_attrib_layout(self, 1);
    }
    dst = self->m_quadBatch + (size_t)self->m_quadBatchCount * 48u;
    if (self->m_quadBatchCount == 0)
    {
        /* P-FLUSHGATE：批建立快照——本 quad 记录时的 scissor 缓存态即
           批 scissor（空批 → 已有 quad 均已按其落盘）。冲批纪律保证批
           存续期间 GL scissor 不偏离快照（改 scissor 出口先冲批或经
           setClipRect 门），目标==快照即可续批免冲（见
           xgld_set_clip_rect）。 */
        self->m_quadBatchScissorValid = self->m_scissorValid;
        self->m_quadBatchScissorOn = self->m_scissorOn;
        self->m_quadBatchScissorX = self->m_scissorX;
        self->m_quadBatchScissorY = self->m_scissorY;
        self->m_quadBatchScissorW = self->m_scissorW;
        self->m_quadBatchScissorH = self->m_scissorH;
    }
    {
        /* 退化连接：重复上一 quad 末顶点（块内第 6 顶点，偏移 40..47）
           与本 quad 首顶点（TL）；首 quad 无上邻，用自身 TL 充当（零面积
           退化）。P0 撕裂根修（2026-09-26）：旧代码读偏移 24..31——那是
           本块 [P,V0,V0,V1,V2,V3] 发射布局的第 4 顶点（V1=TR），并非末
           顶点；TRIANGLE_STRIP 下批内每对相邻 quad 因此多渲染一个真实
           三角形（上一 quad 右下→右上→下一 quad 左上），把上一 quad 的
           顶点色拖进下一 quad 左上邻域——p4/t20 内层页签条蓝/灰楔形
           残影与图表连片实锤；REGION_DISABLE/CLEAR_ROWS_FULL/
           FRAME_KEEPOPEN/FASTPATH_EXT 四开关均不能排除，唯
           XGPU_QUAD_BATCH=0 根治（白纹理批内颜色是逐顶点属性，跨色
           楔形才可见，故平日批内同色 quad 不显）。 */
        const float* prevBR = self->m_quadBatchCount > 0
                                  ? dst - 48u + (xgld_quad_degen_legacy()
                                                     ? 24u : 40u)
                                  : v32;
        XMemcpy(dst, prevBR, sizeof(float) * 8u);
        XMemcpy(dst + 8, v32, sizeof(float) * 8u);
        XMemcpy(dst + 16, v32, sizeof(float) * 32u);
    }
    self->m_quadBatchCount++;
    if (xgld_batch_prof_enabled())
    {
        /* TEMP-PROBE：四边形追加字节数（48 float/quad=192B）。 */
        ++g_xgldBatchProf.m_quadAppend;
        g_xgldBatchProf.m_quadBytes += 48u * sizeof(float);
    }
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

/** @brief 全批化开关：XGPU_FULLBATCH=0 回退追加期逐 quad GL 绑定
 *         （glActiveTexture/glBindTexture/glBindBuffer）现状（排障
 *         对照）；默认开=追加期零 GL 调用，纹理/VBO/属性布局统一
 *         推迟到冲批一次性完成。
 *  @note  正确性依据既有冲批纪律：纹理变形（uploadTargetImage/
 *         glyphAtlasUpload/identityPopulate/gradient）、读回、clear、
 *         setClipRect、帧界/上屏等全部旁路出口先冲批——批存续期间
 *         unit0 与 ARRAY_BUFFER 绑定不可能被旁路改动；冲批点
 *         （xgld_flush_quads）自带同纹理绑定与批布局指针设定，故
 *         追加期免设。即时型原语（drawImage 系）冲批后自绑源纹理，
 *         本批后续追加在新 run 起点经冲批恢复绑定，语义不变。 */
static bool xgld_full_batch_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_FULLBATCH");
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
        {
            self->glDeleteTextures(1, &texture); /* 新纹理配对释放。 */
            /* TEMP-PROBE 镜像：被删纹理是 unit0 当前绑定，GL 语义将其
               复位为 0——镜像失效，防 LEAN 误判同值跳过。 */
            self->m_boundTex0Valid = false;
        }
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
    xgld_note_tex0_bind(self, self->m_whiteTexture); /* TEMP-PROBE 镜像。 */
    xgld_set_unpack_alignment(self, 1);
    xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底（1×1 读入
                                            对行距敏感，防越界读暂存）。 */
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
    xgld_bind_fbo(self, self->m_framebuffer);
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
    /* P-PBO：双 PBO 读回环建立（全帧 RGBA 一次性分配，STREAM_READ 提示
       驱动放「GPU 写/CPU 读」优化路径）。函数加载与资源创建失败一律只
       降级（m_pboReady=false 恒走同步读回），不使会话建立失败——本通道
       是性能优化，不能反向收窄可用性。 */
    if (xgld_pbo_readback_enabled())
    {
        xgld_load_proc(self, "glMapBuffer",
                       &self->glMapBuffer, sizeof(self->glMapBuffer));
        xgld_load_proc(self, "glUnmapBuffer",
                       &self->glUnmapBuffer, sizeof(self->glUnmapBuffer));
        xgld_load_proc(self, "glFenceSync",
                       &self->glFenceSync, sizeof(self->glFenceSync));
        xgld_load_proc(self, "glClientWaitSync",
                       &self->glClientWaitSync, sizeof(self->glClientWaitSync));
        xgld_load_proc(self, "glDeleteSync",
                       &self->glDeleteSync, sizeof(self->glDeleteSync));
        self->glGenBuffers(2, self->m_readbackPbo);
        if (self->glMapBuffer && self->glUnmapBuffer &&
            self->m_readbackPbo[0] && self->m_readbackPbo[1])
        {
            int i;
            for (i = 0; i < 2; ++i)
            {
                self->glBindBuffer(XGL_PIXEL_PACK_BUFFER,
                                   self->m_readbackPbo[i]);
                self->glBufferData(XGL_PIXEL_PACK_BUFFER,
                                   (XglSizeiptr)((size_t)width *
                                                 (size_t)height * 4u),
                                   NULL, XGL_STREAM_READ);
            }
            self->glBindBuffer(XGL_PIXEL_PACK_BUFFER, 0);
            self->m_pboReady = true;
        }
        else if (self->m_readbackPbo[0] && self->m_readbackPbo[1])
        {
            /* 半就绪（map 缺失等）：立即配对回收，不留孤儿缓冲。 */
            self->glDeleteBuffers(2, self->m_readbackPbo);
            self->m_readbackPbo[0] = 0;
            self->m_readbackPbo[1] = 0;
        }
    }
    xgld_bind_fbo(self, 0); /* 播种绑定镜像（旧路径终态同为解绑 0）。 */
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
        /* P-PBO 配对释放：fence 为引用计数句柄可随时删；PBO 与其它
           缓冲同纪删除。均在 makeCurrent 块内=上下文当前。 */
        if (self->m_pboFence[0] || self->m_pboFence[1])
        {
            int i;
            for (i = 0; i < 2; ++i)
            {
                if (self->m_pboFence[i] && self->glDeleteSync)
                    self->glDeleteSync(self->m_pboFence[i]);
                self->m_pboFence[i] = NULL;
            }
        }
        if ((self->m_readbackPbo[0] || self->m_readbackPbo[1]) &&
            self->glDeleteBuffers)
        {
            self->glDeleteBuffers(2, self->m_readbackPbo);
            self->m_readbackPbo[0] = 0;
            self->m_readbackPbo[1] = 0;
        }
        self->m_pboReady = false;
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
    if (!self || !xgld_ensure_current(self)) return false;
    /* L-固定成本：ensure_current 返回真即已保证本会话上下文当前
       （追踪器与平台层 doneCurrent 全配对），旧代码此处再无条件
       make_current 一次=每派发多付一整次 wglMakeCurrent（会话开关
       类驱动调用中档位最高者，0.59ms/派发归因的头号分量）。
       XGPU_MAKECURRENT_ONCE=0 回退旧双调（诊断对照）。 */
    if (!xgld_makecurrent_once_enabled() && !xgld_make_current(self))
        return false;
    xgld_bind_fbo(self, self->m_framebuffer);
    xgld_set_viewport(self, self->m_width, self->m_height);
    xgld_set_pack_alignment(self, 1);
    xgld_ensure_scissor_off(self);
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
    /* L-固定成本（FBO 持久化）：不再解绑 FBO——FBO 绑定按上下文留存，
       下帧同 FBO 经 xgld_bind_fbo 镜像直续，省每派发一对 bind/unbind
       （归因文档 §5 头号假设：会话开关驱动成本）。正确性：冲批纪律
       只要求提交时目标 FBO 在绑（本函数冲批先于一切状态变动）；帧间
       改绑的各出口（present blit/全屏 quad、读回、图集临时 FBO、渐变）
       全部经镜像助手维护，present 自带所需绑定。会话销毁删在绑 FBO
       由 GL 隐式解绑，无后续 GL，安全。XGPU_FBO_PERSIST=0 回退旧
       逐帧解绑（诊断对照）。上下文保持当前语义不变（见下）。 */
    if (!xgld_fbo_persist_enabled())
        xgld_bind_fbo(self, 0);
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
    bool lean;
    if (profOn < 0)
    {
        const char* env = XSystem_environment("XGPU_PROFILE");
        profOn = env && *env ? 1 : 0;
    }
    if (!self || !self->m_windowSession || !self->m_windowContext)
        return false;
    xgld_batch_prof_tick(); /* TEMP-PROBE(XGPU_BATCH_PROF)：5s 窗口汇总。 */
    /* L-固定成本：同 begin_frame——ensure_current 已保证上下文当前，
       冗余二次 makeCurrent 由 XGPU_MAKECURRENT_ONCE 门控消除。 */
    if (!xgld_makecurrent_once_enabled() && !xgld_make_current(self))
        return false;
    lean = xgld_present_lean_enabled();
    xgld_flush_quads(self);
    if (profOn) profT0 = XDateTime_currentNSecsSinceEpoch() / 1000;
    if (lean)
    {
        /* LEAN（默认）：glBlitFramebuffer 是定值拷贝，GL 规范明确
           混合/逻辑操作不作用于 blit——绘制残留的混合态无需关断再
           恢复（每派发 3 次冗余调用）；scissor 确实约束 blit，必须
           关，但走 P0-2 缓存免同值重复调用且不再失效缓存。 */
        xgld_ensure_scissor_off(self);
    }
    else
    {
        xgpu_set_blend(self, false);
        self->glDisable(XGL_SCISSOR_TEST);
        self->m_scissorValid = false; /* P0-2：旁路禁用 scissor，缓存失效（同上）。 */
    }
    if (self->m_hasBlit)
    {
        /* 2b 快路径：READ=FBO（持久画面），DRAW=默认帧缓冲；blit 后
           恢复原绑定（经镜像助手：FBO 持久化下 READ 已在绑，仅
           DRAW→0 与恢复两次真调）。 */
        xgld_bind_fbo_read(self, self->m_framebuffer);
        xgld_bind_fbo_draw(self, 0);
        xgld_set_viewport(self, self->m_width, self->m_height);
        self->glBlitFramebuffer(0, 0, self->m_width, self->m_height,
                                0, 0, self->m_width, self->m_height,
                                XGL_COLOR_BUFFER_BIT, XGL_NEAREST);
        xgld_bind_fbo(self, self->m_framebuffer);
        if (!lean) xgpu_set_blend(self, true);
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
       GL 左下原点，uv 不翻转）。采样绘制真实作用于混合态：无论开关
       组合，此处强制关断（LEAN 只免 blit 路径的混合往返）。 */
    xgpu_set_blend(self, false);
    vertices[0]  = -1.0f; vertices[1]  =  1.0f; vertices[2]  = 0.0f; vertices[3]  = 1.0f;
    vertices[4]  =  1.0f; vertices[5]  =  1.0f; vertices[6]  = 1.0f; vertices[7]  = 1.0f;
    vertices[8]  = -1.0f; vertices[9]  = -1.0f; vertices[10] = 0.0f; vertices[11] = 0.0f;
    vertices[12] =  1.0f; vertices[13] = -1.0f; vertices[14] = 1.0f; vertices[15] = 0.0f;
    xgld_bind_fbo(self, 0);
    xgld_set_viewport(self, self->m_width, self->m_height);
    xgpu_use_program(self, self->m_textureProgram);
    self->glBindBuffer(XGL_ARRAY_BUFFER, self->m_vertexBuffer);
    xgpu_vertex_data(self, vertices);
    xgld_set_attrib_layout(self, 0);
    self->glActiveTexture(XGL_TEXTURE0);
    self->glBindTexture(XGL_TEXTURE_2D, self->m_colorTexture);
    xgld_note_tex0_bind(self, self->m_colorTexture); /* TEMP-PROBE 镜像。 */
    self->glUniform1i(self->m_textureSamplerLocation, 0);
    self->glUniform4f(self->m_textureModulateLocation,
                      modulate[0], modulate[1], modulate[2], modulate[3]);
    self->glDrawArrays(XGL_TRIANGLE_STRIP, 0, 4);
    if (profOn) profT1 = XDateTime_currentNSecsSinceEpoch() / 1000;
    xgld_bind_fbo(self, self->m_framebuffer);
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
            /* flush 列（夜五）：300 派发窗口内实际冲批次数与每次均值，
               列风格对齐 [xgpu-prof] 的 fillRect/solidQuad 口径。冲批计数
               域按会话持有（与 scissor 快照同纪），随打印清零。 */
            fprintf(stderr, "[profile] present avg quad=%.3fms swap=%.3fms "
                            "flush=%u (%.4fms/次) (n=%u)\n",
                    profQuadUs / profCount / 1000.0,
                    profSwapUs / profCount / 1000.0,
                    self->m_flushQuadCount,
                    self->m_flushQuadCount
                        ? (double)self->m_flushQuadUs /
                              (double)self->m_flushQuadCount / 1000.0
                        : 0.0,
                    profCount);
            profCount = 0; profQuadUs = 0; profSwapUs = 0;
            self->m_flushQuadCount = 0;
            self->m_flushQuadUs = 0;
        }
    }
    XPlatformOpenGLContext_doneCurrent(self->m_windowContext);
    xgld_clear_current_tracker();
    return true;
}

/* ==================== P-PBO（2026-09-26）双 PBO 异步读回 ==================== */

/* 疑题：GPU 增量每帧 ~1.36ms（SW 0.09ms），增量基准经静态层缓存后脏区
 * 仅悬浮层 210x50，毫秒级开销头号候选=同步 glReadPixels 的 CPU 停顿
 * （管线冲刷 + PCIe 往返全程占住 CPU）。方案：双 PBO 轮转 1 帧滞后——
 * 本帧把读回【异步】写入 PBO[cur]（CPU 立即返回不等结果），随即 map
 * 上一帧的 PBO[prev] 拷出上一帧内容，呈现滞后一帧（60Hz 呈现 16.7ms
 * 下不可感知）。
 * 语义陷阱（脏区漂移）：上一帧 PBO 里只有其【写入时 bbox】区域是上一
 * 帧新内容，bbox 外是更早帧的陈旧字节。处理：PBO 恒为全帧 RGBA 布局
 * （glReadPixels 读 bbox 写入 PBO 的 (glY,x) 偏移处，PACK_ROW_LENGTH=
 * m_width），拷出按本帧 bbox 从上一帧全帧图对位取；并回语义（限频跳
 * 帧把区域并回 m_dirty，XWidget 呈现链）下 bbox 只增不减，本帧 bbox
 * 落在上一帧 bbox 内（含缩小）时每个像素都是上一帧内容，滞后语义成
 * 立；漂移出界/首帧（无可信槽）回退同步直读本帧——正确性优先。
 * XGPU_PBO_READBACK=0 整链回退同步路径（逐位旧行为）；
 * XGPU_PBO_STATS=1 每 300 次读回打印命中/回退分类统计。
 * XGPU_PBO_LAG_FIX 默认开（三夜九波修复，2026-09-27）：呈现链滞后通
 * 道整体休眠（拷出+异步写入一并跳过），置 0 恢复滞后行为。
 * 适用边界（二波 GL 冒烟回归，2026-09-26 收尾修复）：滞后语义只对
 * 呈现链 readbackRect 成立；全帧 readback 的调用方（painter 帧末
 * readback、回归像素断言、SYNC 逐命令读回、XWidget 降级帧补读回）
 * 契约是【本帧内容】——离屏 painter 会话同尺寸跨帧复用时，线块全帧
 * 读回曾命中上一帧（poly 块绿三角）的 PBO，黑线/渐变被陈旧像素顶替。
 * 故全帧 readback 默认同步直读；XGPU_PBO_READBACK_FULL=1 扩展滞后
 * 到全帧（复现回归口径的诊断开关）。 */

static bool xgld_pbo_readback_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_PBO_READBACK");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/** @brief XGPU_PBO_READBACK_FULL 环境开关（"1"=开，默认关）。
 *  @note  默认关=全帧 readback 同步直读本帧（正确性优先，G 路增量
 *         收益不受影响——增量基准的读回走呈现链 readbackRect）；
 *         置 1 恢复二波初期全帧也吃 PBO 滞后的行为（复现冒烟回归
 *         用，离屏像素断言会拿到上一帧内容）。 */
static bool xgld_pbo_full_readback_requested(void)
{
    static int requested = -1;
    if (requested < 0)
    {
        const char* value = XSystem_environment("XGPU_PBO_READBACK_FULL");
        requested = value && *value &&
                            !(value[0] == '0' && value[1] == 0)
                        ? 1 : 0;
    }
    return requested != 0;
}

/** @brief XGPU_PBO_LAG_FIX 环境开关（默认开=修复生效；"0"=回退滞后通道）。
 *  @note  为什么默认关掉滞后通道：三夜九波定向二分（docs/xgui/
 *         night3-bisect.md，2026-09-27）在确定性拖动轨迹下，唯一让拖动
 *         全程 clean 的配置是 XGPU_PBO_READBACK=0（同步直读）；滞后命中
 *         条件（bbox 包含 + fence 探测）在交互态失效，拷出呈现陈旧/半成
 *         品帧——整窗白/碎片且松开不自愈。同步直读同 FBO 同渲染全程干
 *         净，反证 FBO 内容本身无腐坏，病灶只在滞后拷出这一层。修复=
 *         呈现链默认不再进入滞后通道（拷出与异步写入一并休眠，行为逐
 *         位等同 XGPU_PBO_READBACK=0 的呈现链）；置 0 恢复旧行为，
 *         XGPU_PBO_READBACK_FULL 诊断复现须先置本开关 0。 */
/* 场景自适应滞后（2026-09-29）：mouse-grab 查询注入点。分层单向依赖——
 * GL 驱动不 include XWidget.h，由 XGui 初始化（XGuiApplication 启动路径）
 * 注入 XWidget_mouseGrabber 的薄包装；NULL（默认）=不自知交互态，滞后
 * 通道维持第三夜二分的旧口径（XGPU_PBO_LAG_FIX=0 才整体激活）。 */
static int (*g_xgldPointerGrabQuery)(void) = NULL;

void XGpuRenderDriver_gl_setPointerGrabQuery(int (*query)(void))
{
    g_xgldPointerGrabQuery = query;
}

static bool xgld_pbo_lag_fix_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_PBO_LAG_FIX");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

/* fence 0 超时探测：上一帧写入该槽的读回 DMA 是否已完成。未完成时
   map 会把本帧 CPU 停在 GPU 链上（正是本通道要消除的停顿形态），按
   停顿最小化回退同步直读。无 fence 能力（GL<3.2，函数指针 NULL）时
   直接 map：最多阻塞一帧 DMA，仍远轻于同步路径的全管线冲刷，正确性
   不受影响。探测带 SYNC_FLUSH_COMMANDS 位：未冲刷的 fence 允许提升，
   否则空闲 GPU 上也可能永远探不到信号。 */
static bool xgld_pbo_slot_ready(XGpuRenderDriverSession* self, int slot)
{
    XglEnum wait;
    if (!self->m_pboFence[slot] || !self->glClientWaitSync) return true;
    wait = self->glClientWaitSync(self->m_pboFence[slot],
                                  XGL_SYNC_FLUSH_COMMANDS_BIT,
                                  (XglUint64)0u);
    return wait == XGL_ALREADY_SIGNALED || wait == XGL_CONDITION_SATISFIED;
}

/* 拷出内核：把一段 GL 行序 RGBA 缓冲按 Y 翻转 + R/B 交换写入目标图像
 * 矩形。rowSrc0=图像行 y 的源地址，rowStride=图像行步进（两路调用方
 * 均为负——GL 行序与图像行序相反）。内层循环与原 readback/readbackRect
 * 逐位一致（仅行源地址参数化）：同步路径传紧排读回缓冲末行、步进
 * -width*4；PBO 路径传全帧布局内 (glY=m_height-y-1 行, x 列)、步进
 * -m_width*4。快速路径条件 bpl >= (x+width)*4 与两处原判定在 x=0 时
 * 同一（整帧读回 bpl >= m_width*4）。 */
static void xgld_copyout_rect_to_image(XImage* target, const uint8_t* rowSrc0,
                                       ptrdiff_t rowStride, int x, int y,
                                       int width, int height)
{
    int row;
    /* 快速路径：ARGB32 与 ARGB32_Premultiplied 在 XImage 中是同一预乘
       布局。预乘帧逐行直接写入（RGBA→ARGB32 小端 R/B 交换）。 */
    if (XImage_format(target) == XImageFormat_ARGB32 ||
        XImage_format(target) == XImageFormat_ARGB32_Premultiplied)
    {
        uint8_t* dst = XImage_bits(target);
        int bpl = XImage_bytesPerLine(target);
        if (dst && bpl >= (x + width) * 4)
        {
            for (row = 0; row < height; ++row)
            {
                const uint8_t* src = rowSrc0 + (ptrdiff_t)row * rowStride;
                uint8_t* line = dst + (size_t)(y + row) * (size_t)bpl +
                    (size_t)x * 4u;
                int col;
                for (col = 0; col < width; ++col)
                {
                    const uint8_t* p = src + (size_t)col * 4u;
                    line[col * 4 + 0] = p[2]; /* B */
                    line[col * 4 + 1] = p[1]; /* G */
                    line[col * 4 + 2] = p[0]; /* R */
                    line[col * 4 + 3] = p[3]; /* A（预乘帧直接入预乘图像） */
                }
            }
            return;
        }
    }
    /* 慢路径：其它格式逐像素经 XImage_setPixel 转换（坐标平移回目标
       图像坐标系，与整帧读回逐位一致）。 */
    for (row = 0; row < height; ++row)
    {
        int col;
        const uint8_t* src = rowSrc0 + (ptrdiff_t)row * rowStride;
        for (col = 0; col < width; ++col)
        {
            const uint8_t* pixel = src + (size_t)col * 4u;
            uint8_t a = pixel[3];
            uint32_t argb = ((uint32_t)a << 24) |
                ((uint32_t)xgpu_unpremultiply(pixel[0], a) << 16) |
                ((uint32_t)xgpu_unpremultiply(pixel[1], a) << 8) |
                (uint32_t)xgpu_unpremultiply(pixel[2], a);
            XImage_setPixel(target, x + col, y + row, argb);
        }
    }
}

/* 本帧异步读回写入 PBO[cur]：全帧布局下把 bbox 落到自己的 (glY,x)
 * 偏移（row r 落到全帧行 glY+r 的 [x,x+w) 列段）。写前 glBufferData
 * 孤儿化重分配：该槽上次写入在两帧前，孤儿化让驱动免于把新写入串行
 * 化在旧存储回收之后（PBO 常规纪律）。随后立 fence 供下一帧 0 超时
 * 探测。PACK_ROW_LENGTH 用后即清零——同步路径按紧排读回，共享上下文
 * 的像素打包状态不外溢。 */
static void xgld_pbo_issue_async_read(XGpuRenderDriverSession* self,
                                      int x, int y, int width, int height)
{
    int slot = self->m_readbackPboCur;
    int glY = self->m_height - y - height;
    uintptr_t offset =
        ((uintptr_t)glY * (uintptr_t)self->m_width + (uintptr_t)x) * 4u;
    xgld_bind_fbo(self, self->m_framebuffer);
    self->glBindBuffer(XGL_PIXEL_PACK_BUFFER, self->m_readbackPbo[slot]);
    self->glBufferData(XGL_PIXEL_PACK_BUFFER,
                       (XglSizeiptr)((size_t)self->m_width *
                                     (size_t)self->m_height * 4u),
                       NULL, XGL_STREAM_READ);
    xgld_set_pack_alignment(self, 4);
    self->glPixelStorei(XGL_PACK_ROW_LENGTH, self->m_width);
    /* PBO 绑定下 glReadPixels 的指针参数=缓冲内字节偏移（非客户指针）。 */
    self->glReadPixels(x, glY, width, height, XGL_RGBA, XGL_UNSIGNED_BYTE,
                       (void*)offset);
    self->glPixelStorei(XGL_PACK_ROW_LENGTH, 0);
    self->glBindBuffer(XGL_PIXEL_PACK_BUFFER, 0);
    if (self->glFenceSync)
    {
        if (self->m_pboFence[slot] && self->glDeleteSync)
            self->glDeleteSync(self->m_pboFence[slot]);
        self->m_pboFence[slot] = self->glFenceSync(
            XGL_SYNC_GPU_COMMANDS_COMPLETE, 0u);
    }
    self->m_readbackPboValid[slot] = 1;
    self->m_pboBbox[slot][0] = x;
    self->m_pboBbox[slot][1] = y;
    self->m_pboBbox[slot][2] = width;
    self->m_pboBbox[slot][3] = height;
    self->m_readbackPboCur = slot ^ 1;
}

/* 读回统一内核：readback（全帧）与 readbackRect（脏区 bbox）共用。
 * allowPboLag=允许吃 1 帧滞后：仅呈现链 readbackRect 传 true（屏幕
 * 16.7ms 内刷新，滞后不可感知）；全帧 readback 传 false（离屏像素
 * 契约=本帧内容），除非 XGPU_PBO_READBACK_FULL=1（诊断开关）。
 * 命中 PBO 拷出需同时满足（任一不满足回退同步直读本帧，正确性优先）：
 * 1) 允许滞后（见上）且 XGPU_PBO_READBACK 未置 0 且 PBO 资源就绪；
 * 2) PBO[prev] 已有可信写入（会话首帧/半就绪回退无）；
 * 3) 本帧 bbox ⊆ PBO[prev] 写入时 bbox（并回语义下含缩小，见节注释）；
 * 4) fence 0 超时探测通过（GPU 未拖帧）。
 * 回退帧仍照常发异步读保温链路：漂移一帧后 bbox 稳定，下一帧即可命中
 * （否则同步帧不写 PBO，链路永远建立不起来）。
 * 三夜九波起（2026-09-27）滞后通道默认休眠：XGPU_PBO_LAG_FIX 默认把
 * pboLag 压为假，拷出与异步写入一并跳过（呈现链逐位等同同步直读）——
 * 交互拖动二分实证上述命中条件在交互态失效、拷出产出陈旧/半成品帧
 * （docs/xgui/night3-bisect.md）；置 0 恢复下列命中条件与保温链路。 */
static bool xgld_readback_region(XGpuRenderDriverSession* self,
                                 XImage* target, int x, int y,
                                 int width, int height, bool allowPboLag)
{
    /* 滞后通道默认休眠（XGPU_PBO_LAG_FIX，见函数头注释）：交互拖动二分
       实证滞后拷出呈现陈旧/半成品帧，回退同步直读；置 0 恢复旧行为。
       【场景自适应 2026-09-29】XGPU_PBO_ADAPTIVE=1（默认开）时滞后通道
       按 mouse-grab 场景门控：无抓取（无拖动/交互序列）允许滞后拷出换
       吞吐（当前慢态实测 +35%：62→84 fps），有抓取强制同步直读保正确
       性（第三夜二分结论的交互语义完整保留）。查询函数由 XGui 初始化
       注入（g_xgldPointerGrabQuery，分层单向依赖：GL 驱动不 include
       XWidget.h）；未注入=NULL=保持旧口径（完全沿第三夜二分）。关闭
       自适应=完全旧口径。 */
    bool pboLag = (allowPboLag || xgld_pbo_full_readback_requested()) &&
                  !xgld_pbo_lag_fix_enabled();
    {
        static int adaptiveInit = -1;
        static int adaptiveOn = 1;
        if (adaptiveInit < 0)
        {
            const char* value = XSystem_environment("XGPU_PBO_ADAPTIVE");
            adaptiveOn = !(value && *value && value[0] == '0' &&
                           value[1] == 0);
            adaptiveInit = 1;
        }
        if (adaptiveOn && g_xgldPointerGrabQuery)
        {
            /* 自适应语义：滞后通道的休眠/激活由【当前场景】决定——无
               抓取=非交互态激活滞后（覆盖 lag_fix 的全局休眠，这是本
               改动的本体）；有抓取=交互序列强制同步直读（lag_fix 的
               正确性结论按场景保留）。查询未注入时不动旧口径。 */
            if (!g_xgldPointerGrabQuery())
                pboLag = allowPboLag ||
                         xgld_pbo_full_readback_requested();
            else
                pboLag = false;
        }
    }
    static unsigned stCalls, stPbo, stSeed, stStall, stMapFail;
    static int statsOn = -1;
    bool usedPbo = false;
    int prevSlot;
    if (statsOn < 0)
    {
        const char* value = XSystem_environment("XGPU_PBO_STATS");
        statsOn = value && *value && !(value[0] == '0' && value[1] == 0)
            ? 1 : 0;
    }
    /* 越界钳位（与通用层包装双重防护）：负/超界部分裁剪到渲染目标内；
       钳位后为空 = 无事可做，按成功返回（调用方无需整帧回退）。 */
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (width > self->m_width - x) width = self->m_width - x;
    if (height > self->m_height - y) height = self->m_height - y;
    if (width <= 0 || height <= 0) return true;
    if (XImage_width(target) != self->m_width ||
        XImage_height(target) != self->m_height)
        return false;
    ++stCalls;
    /* 待定批 quad 可能仍会写入本矩形：先冲批再读（与整帧读回同纪律）。 */
    xgld_flush_quads(self);
    xgld_bind_fbo(self, self->m_framebuffer);
    prevSlot = self->m_readbackPboCur ^ 1;
    if (pboLag && xgld_pbo_readback_enabled() && self->m_pboReady)
    {
        bool contained = self->m_readbackPboValid[prevSlot] &&
            x >= self->m_pboBbox[prevSlot][0] &&
            y >= self->m_pboBbox[prevSlot][1] &&
            x + width <= self->m_pboBbox[prevSlot][0] +
                         self->m_pboBbox[prevSlot][2] &&
            y + height <= self->m_pboBbox[prevSlot][1] +
                          self->m_pboBbox[prevSlot][3];
        if (contained && xgld_pbo_slot_ready(self, prevSlot))
        {
            void* mapped;
            self->glBindBuffer(XGL_PIXEL_PACK_BUFFER,
                               self->m_readbackPbo[prevSlot]);
            mapped = self->glMapBuffer(XGL_PIXEL_PACK_BUFFER, XGL_READ_ONLY);
            if (mapped)
            {
                xgld_copyout_rect_to_image(
                    target,
                    (const uint8_t*)mapped +
                        ((size_t)(self->m_height - y - 1) *
                         (size_t)self->m_width + (size_t)x) * 4u,
                    -(ptrdiff_t)((size_t)self->m_width * 4u),
                    x, y, width, height);
                self->glUnmapBuffer(XGL_PIXEL_PACK_BUFFER);
                usedPbo = true;
                ++stPbo;
            }
            else
            {
                /* 映射失败（驱动内存压力等罕见路径）：解映射态后按
                   回退计，落同步直读。 */
                self->glUnmapBuffer(XGL_PIXEL_PACK_BUFFER);
                ++stMapFail;
            }
            self->glBindBuffer(XGL_PIXEL_PACK_BUFFER, 0);
        }
        else if (contained) ++stStall; /* fence 未就绪：GPU 拖帧。 */
        else ++stSeed;                 /* 首帧或 bbox 漂移出界。 */
    }
    if (!usedPbo)
    {
        size_t bytes = (size_t)width * (size_t)height * 4u;
        if (!xgpu_reserve_pixels(self, bytes)) return false;
        /* 同步直读（PBO 关闭/首帧/漂移/拖帧时的路径，逐位保持原
           readback/readbackRect 行为）：紧排行距 width*4，恒 4 字节
           对齐，PACK_ALIGNMENT=4 即满足。 */
        xgld_set_pack_alignment(self, 4);
        self->glReadPixels(x, self->m_height - y - height, width, height,
                           XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
        xgld_copyout_rect_to_image(
            target,
            self->m_pixels + (size_t)(height - 1) * (size_t)width * 4u,
            -(ptrdiff_t)((size_t)width * 4u),
            x, y, width, height);
    }
    if (pboLag && xgld_pbo_readback_enabled() && self->m_pboReady)
        xgld_pbo_issue_async_read(self, x, y, width, height);
    if (statsOn && (stCalls % 300u) == 0u)
        fprintf(stderr,
                "[gl-pbo] calls=%u pbo=%u seed=%u stall=%u mapfail=%u\n",
                stCalls, stPbo, stSeed, stStall, stMapFail);
    return true;
}

static bool xgld_readback(XGpuRenderDriverSession* self, XImage* target)
{
    if (!xgld_ensure_current(self)) return false;
    if (!self || !target) return false;
    /* 全帧读回不吃 PBO 滞后（默认）：调用方是离屏像素契约（painter
       帧末/回归断言/SYNC 逐命令/XWidget 降级帧补读回），必须返回
       本帧内容；呈现链增量通道见 readbackRect 入口（allowPboLag=true）。 */
    return xgld_readback_region(self, target, 0, 0,
                                self->m_width, self->m_height, false);
}

/* ==================== 子矩形读回（P-dirty-readback 2026-09-25） ==================== */

/* 跨编译单元直连入口（非 XGpuRenderDriverProcs 成员）：操作表结构体
 * 定义于共享接口头 XGpuRenderDriver.h（不在本车道可改文件清单内），
 * 脏区呈现链需要的子矩形读回暂以外部链接函数暴露；通用层包装
 * （XGpuRenderBackend_readbackRect）仅在活动驱动为本 GL 操作表（指针
 * 身份比对）时直调，其余驱动返回 false 由调用方回退整帧 readback。
 * 操作表可扩展时应把本函数收编为 procs.readbackRect 并删除直连。
 * P-PBO（2026-09-26）起实现收编到上方 xgld_readback_region 统一内核
 * （PBO 命中走 1 帧滞后拷出，回退逐位保持本函数原同步行为）。滞后
 * 语义仅本入口成立（呈现链脏区读回，60Hz 下不可感知）；二波收尾
 * 起全帧 readback 默认同步直读（离屏像素契约，见 xgld_readback）。 */
bool XGpuRenderDriver_gl_readbackRect(XGpuRenderDriverSession* self,
                                      XImage* target, int x, int y,
                                      int width, int height)
{
    if (!xgld_ensure_current(self)) return false;
    if (!self || !target) return false;
    return xgld_readback_region(self, target, x, y, width, height, true);
}

static void xgld_clear(XGpuRenderDriverSession* self, uint32_t argb)
{
    xgld_ensure_current(self);
    unsigned a;
    if (!self) return;
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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

/** @brief XGPU_FLUSH_GATED 环境开关（=0 回退「批非空必冲批」现状）。
 *  @note  默认开：setClipRect 目标 scissor 与批建立快照相同则免冲批
 *         （见 xgld_set_clip_rect 门），outline 文本逐字形 ApplyStateClip
 *         不再切断批次。 */
static bool xgld_flush_gated_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0)
    {
        const char* value = XSystem_environment("XGPU_FLUSH_GATED");
        enabled = !(value && *value && value[0] == '0' && value[1] == 0);
    }
    return enabled != 0;
}

static void xgld_set_clip_rect(XGpuRenderDriverSession* self, const XRect* rect)
{
    xgld_ensure_current(self);
    int x0, y0, x1, y1;
    bool on;
    static int cacheEnabled = -1; /* -1 未读环境；0=旧行为（诊断回退）。 */
    if (!self) return;
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    /* P-FLUSHGATE（2026-09-26 夜五）：批非空门——目标 scissor 与批建立
       快照相同则免冲批直接续批。语义论证：批由【一次】glDrawArrays 在
       冲批时生效的 scissor 下提交，逐 quad 语义正确的充要条件是批内所有
       quad 记录时的 scissor == 提交时的 scissor（scissor 是光栅阶段剪裁，
       与批内已统一的纹理/混合/程序零交互；quad 提交顺序不变，blend 顺序
       语义不受影响）。冲批纪律保证批存续期间 GL scissor 恒等于快照：
       改 scissor 的全部出口（本门目标≠快照支路、clear/readback/upload/
       帧界/图集上传）一律先冲批；旁路关断（ensure_scissor_off）置快照
       失效。故快照各域与目标态全等时，待定批本就在目标 scissor 下语义
       正确——GL scissor 与缓存也同值（同上不变式），早返回无需任何 GL
       调用。收益：outline 文本逐字形 ApplyStateClip（XPainter.c
       painterGpuDrawOutlineGlyph）自「每字形一冲批（批均 ~1 quad）」
       恢复为整串/整帧一批。XGPU_FLUSH_GATED=0 回退现状；本门另受
       XGPU_SCISSOR_CACHE 门控（缓存诊断回退时一并回退）。 */
    if (cacheEnabled && xgld_flush_gated_enabled() &&
        self->m_quadBatchCount > 0 &&
        self->m_quadBatchScissorValid && self->m_scissorValid &&
        self->m_quadBatchScissorOn == on &&
        self->m_quadBatchScissorX == x0 && self->m_quadBatchScissorY == y0 &&
        self->m_quadBatchScissorW == x1 - x0 &&
        self->m_quadBatchScissorH == y1 - y0)
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
    if (xgld_batch_prof_enabled())
        ++g_xgldBatchProf.m_stScissor; /* TEMP-PROBE：真改写计数。 */
}

static bool xgld_fill_rect(XGpuRenderDriverSession* self, const XRect* rect,
                           uint32_t color, float opacity, bool sourceOver)
{
    if (!xgld_ensure_current(self)) return false;
    float rgba[4];
    unsigned a;
    if (!self || !rect || rect->width <= 0 || rect->height <= 0)
        return false;
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    int uploadPremul;
    if (!xgld_ensure_current(self)) return false;
    if (!self || !image || srcW <= 0 || srcH <= 0)
        return false;
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
    iw = XImage_width(image);
    ih = XImage_height(image);
    if (iw <= 0 || ih <= 0 || srcX < 0 || srcY < 0 ||
        srcX + srcW > iw || srcY + srcH > ih)
        return false;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    /* P-IPU：ARGB32 直通内容区域上传同样按 a 预乘（批画布冲批主路）。 */
    uploadPremul = xgld_upload_needs_premul(self, image);
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
        xgld_note_tex0_bind(self, self->m_sourceTexture); /* TEMP-PROBE 镜像。 */
        xgld_set_unpack_alignment(self, 1);
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
        if (!cpuSwap && !uploadPremul && src && bpl >= iw * 4)
        {
            self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
            xgld_note_tex0_bind(self, self->m_sourceTexture); /* TEMP-PROBE 镜像。 */
            xgld_set_unpack_alignment(self, 1);
            /* E-F 路：行距设值/归 0 经镜像助手——flush 段主体是同画布
               连续冲批，LEAN 下「set(iw)（同值跳过）+ texsub + 归 0 整省」
               每冲批少 2 次真调；非同宽上传点已前置归 0 兜底（行距
               语义只约束源行宽≠上传宽的读入，行距=iw 残留对其余
               TexImage/TexSub 读入是错位源，各上传点助手前置保证真值）。
               XGPU_FLUSH_LEAN=0：两调恒真发（set+归 0，逐位旧行为）。 */
            xgld_set_unpack_row_length(self, iw);
            self->glTexSubImage2D(XGL_TEXTURE_2D, 0, srcX, srcY, srcW, srcH,
                                  XGL_BGRA, XGL_UNSIGNED_INT_8_8_8_8_REV,
                                  src + (size_t)srcY * (size_t)bpl +
                                      (size_t)srcX * 4u);
            if (!xgld_flush_lean_enabled())
                xgld_set_unpack_row_length(self, 0);
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
                        if (uploadPremul)
                        {
                            row[x * 4 + 0] =
                                xgpu_mul255((unsigned)((argb >> 16) & 0xffu), a);
                            row[x * 4 + 1] =
                                xgpu_mul255((unsigned)((argb >> 8) & 0xffu), a);
                            row[x * 4 + 2] =
                                xgpu_mul255((unsigned)(argb & 0xffu), a);
                        }
                        else
                        {
                            row[x * 4 + 0] = (uint8_t)((argb >> 16) & 0xffu);
                            row[x * 4 + 1] = (uint8_t)((argb >> 8) & 0xffu);
                            row[x * 4 + 2] = (uint8_t)(argb & 0xffu);
                        }
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
                        /* P-IPU：ARGB32 直通内容按 a 预乘（a=255 逐字节
                           不变）；预乘格式直通。 */
                        uint8_t a = srow[x * 4 + 3];
                        if (uploadPremul)
                        {
                            drow[x * 4 + 0] =
                                xgpu_mul255((unsigned)srow[x * 4 + 2], a);
                            drow[x * 4 + 1] =
                                xgpu_mul255((unsigned)srow[x * 4 + 1], a);
                            drow[x * 4 + 2] =
                                xgpu_mul255((unsigned)srow[x * 4 + 0], a);
                        }
                        else
                        {
                            drow[x * 4 + 0] = srow[x * 4 + 2]; /* R <- B */
                            drow[x * 4 + 1] = srow[x * 4 + 1]; /* G */
                            drow[x * 4 + 2] = srow[x * 4 + 0]; /* B <- R */
                        }
                        drow[x * 4 + 3] = a; /* A */
                    }
                }
            }
            self->glBindTexture(XGL_TEXTURE_2D, self->m_sourceTexture);
            xgld_note_tex0_bind(self, self->m_sourceTexture); /* TEMP-PROBE 镜像。 */
        xgld_set_unpack_alignment(self, 1);
        xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底（暂存
                                                行宽=srcW≠iw 时归 0 必须
                                                真发，镜像自会判）。 */
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
            xgld_note_tex0_bind(self, self->m_sourceTexture); /* TEMP-PROBE 镜像。 */
            xgld_set_unpack_alignment(self, 1);
            xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    xgld_note_tex0_bind(self, self->m_gradientMaskTexture); /* TEMP-PROBE 镜像。 */
    xgld_set_unpack_alignment(self, 1);
    xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底（掩码+
                                            LUT 两次上传行宽均≠iw）。 */
    self->glTexImage2D(XGL_TEXTURE_2D, 0, (XglInt)XGL_RGBA, width, height,
                       0, XGL_RGBA, XGL_UNSIGNED_BYTE, self->m_pixels);
    /* LUT：256×1 预乘 ARGB 每次同步（渐变停止点可变）。 */
    self->glBindTexture(XGL_TEXTURE_2D, self->m_gradientLutTexture);
    xgld_note_tex0_bind(self, self->m_gradientLutTexture); /* TEMP-PROBE 镜像。 */
    xgld_set_unpack_alignment(self, 1);
    self->glTexSubImage2D(XGL_TEXTURE_2D, 0, 0, 0, 256, 1, XGL_RGBA,
                          XGL_UNSIGNED_BYTE, lutRgba);
    modulate[0] = opacity;
    modulate[1] = opacity;
    modulate[2] = opacity;
    modulate[3] = opacity;
    xgld_bind_fbo(self, self->m_framebuffer);
    xgld_set_viewport(self, self->m_width, self->m_height);
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
    xgld_note_tex0_bind(self, self->m_gradientMaskTexture); /* TEMP-PROBE 镜像。 */
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

/* ==================== P-SPR（XGPU_SOLID_PREMUL_REPAIR）纯色 quad 预乘不变量修复 ==================== */

/** @brief 预乘不变量检测与修复。
 *  @details 合法预乘色满足「每通道 rgb ≤ alpha」：premul 通道值
 *           round(c*a/255) ≤ a 对一切 c∈[0,255] 成立（上取整界）。
 *           夜四撕裂复核实测（Tools/night4 v3 协议口径）：面积图斜边
 *           笔 quad 以直通 RGB（0x5516AFA9：G=175/B=169 > a=85，违反
 *           不变量）到达本驱动，经 (ONE, ONE_MINUS_SRC_ALPHA) 预乘混
 *           合 RGB 超加，G/B 饱和 255（GPU 实测 (139,255,255)/(150,
 *           255,255)，SW 同位 (90,198,194)/(125,210,207)，cmp_frames
 *           阈值 30 全部计差异）。本修复：违例色按「调用方传的是直
 *           通 ARGB」语义就地转预乘（rgb' = round(rgb*a/255)）——合
 *           成结果回到与软件直通混合等价的值；合法预乘色逐字节不变
 *           （no-op），既有全部调用点零影响。 */
static uint32_t xgpu_repair_premul_color(uint32_t color)
{
    unsigned a = (color >> 24) & 0xffu;
    unsigned r = (color >> 16) & 0xffu;
    unsigned g = (color >> 8) & 0xffu;
    unsigned b = color & 0xffu;
    if (a == 0u || a == 255u) return color; /* 全透明/不透明：直通=预乘。 */
    if (r <= a && g <= a && b <= a) return color; /* 合法预乘：no-op。 */
    r = (r * a + 127u) / 255u;
    g = (g * a + 127u) / 255u;
    b = (b * a + 127u) / 255u;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

static bool xgld_draw_solid_quad(XGpuRenderDriverSession* self, float x1,
                                 float y1, float x2, float y2, float x3,
                                 float y3, float x4, float y4,
                                 uint32_t premulColor, bool sourceOver)
{
    if (!xgld_ensure_current(self)) return false;
    float rgba[4];
    if (!self) return false;
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
    /* P-SPR：预乘不变量违例修复（合法色 no-op）。 */
    if (xgld_premul_repair_enabled())
        premulColor = xgpu_repair_premul_color(premulColor);
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    xgld_note_tex0_bind(self, self->m_glyphAtlasTexture); /* TEMP-PROBE 镜像。 */
    xgld_set_unpack_alignment(self, 1);
    xgld_set_unpack_row_length(self, 0); /* E-F 路：残留行距兜底（图集暂存
                                            行宽=width≠iw，字形错位防线）。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
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
    XGLD_BATCH_PROF_CMD(); /* TEMP-PROBE：命令解码计数。 */
    if (!self->glGenFramebuffers || !self->glBindFramebuffer ||
        !self->glFramebufferTexture2D || !self->glCheckFramebufferStatus)
        return false;
    bytes = (size_t)atlasWidth * (size_t)atlasHeight * 4u;
    if (!xgpu_reserve_pixels(self, bytes)) return false;
    if (!xgld_make_current(self)) return false;
    self->glGenFramebuffers(1, &tempFbo);
    xgld_bind_fbo(self, tempFbo);
    self->glFramebufferTexture2D(XGL_FRAMEBUFFER, XGL_COLOR_ATTACHMENT0,
                                 XGL_TEXTURE_2D, self->m_glyphAtlasTexture, 0);
    if (self->glCheckFramebufferStatus(XGL_FRAMEBUFFER) ==
        XGL_FRAMEBUFFER_COMPLETE)
    {
        xgld_set_pack_alignment(self, 1); /* 读回改走镜像（防镜像失真）。 */
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
    xgld_bind_fbo(self, self->m_framebuffer); /* 从临时 FBO 恢复（镜像同步）。 */
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
