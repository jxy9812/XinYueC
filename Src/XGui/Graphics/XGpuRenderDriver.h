/******************************************************************************
 * @file       XGpuRenderDriver.h
 * @brief      XGui GPU 渲染驱动可插拔接口（对齐 Qt QRhi 后端模式）。
 * @details    定义渲染驱动必须实现的操作表（C 函数指针结构体）：通用层
 *             （XGpuRenderBackend，会话管理/字形图集装箱/统计/工厂）仅
 *             通过本接口说话，不感知具体图形 API。每个图形 API 一个驱动
 *             实现文件（XGpuRenderDriver_gl.c / XGpuRenderDriver_vulkan.c），
 *             编译期内置（非 dlopen 插件），系统头文件（GL/Vulkan SDK）
 *             只允许出现在驱动实现文件中。
 *             接口按 XGui 需要的原语粒度设计（clear/fillRect/drawImage/
 *             drawAlphaBitmap/字形图集子矩形），不引入 Qt RHI 的通用 3D
 *             资源对象（Buffer/Texture/Pipeline）——XGui 的 2D 原语面
 *             足够窄，按原语抽象更贴合嵌入式裁剪目标。
 * @note       本接口仅在 XPLATFORMINTEGRATION_ON && XGPU_ON 时可用；
 *             驱动实现失败（创建返回 NULL、操作返回 false）由通用层
 *             按 vulkan -> gl -> software 有序回退，保证行为不变。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGPURENDERDRIVER_H
#define XGPURENDERDRIVER_H

#if XGUI_ON

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XGeometry.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON

typedef struct XImage XImage;
typedef struct XWindow XWindow;

/* ==================== 驱动类型与常量 ==================== */

/**
 * @brief      渲染驱动类型（对齐 Qt QRhi 的后端枚举）。
 * @details    工厂按类型返回驱动操作表；未实现的类型返回 NULL，通用层
 *             据此走有序回退（vulkan -> gl -> software）。
 */
typedef enum XGpuRenderDriverType
{
    XGpuRenderDriver_OpenGL = 0, /**< OpenGL/GLES 后端（GLX/WGL/EGL）。 */
    XGpuRenderDriver_Vulkan = 1  /**< Vulkan 后端（骨架预留，未实现）。 */
} XGpuRenderDriverType;

/**
 * @brief      字形图集纹理边长（驱动契约：图集纹理按该尺寸创建）。
 * @details    通用层据此装箱与判断"字形大于图集"的回退；驱动实现必须
 *             使用同一常量创建图集纹理，否则子矩形坐标错位。
 */
#define XGPU_RENDER_GLYPH_ATLAS_SIZE 512

/**
 * @brief      字形图集顶部白纹素带高度（像素，驱动/通用层契约）。
 * @details    图集纵向区间 [0, XGPU_RENDER_GLYPH_ATLAS_WHITE_BAND) 恒为
 *             **不透明白**（RGBA 四通道全 255），由驱动在会话初始化时
 *             一次性写入，此后不再改动。通用层 shelf 装箱的纵向游标
 *             必须从该带之下开始（m_glyphCursorY 初值与重置值均等于该
 *             常量），**任何装箱/字形上传不得落入该带**——驱动不再为
 *             字形写入该区间，落入即被白块覆盖或采样到带外内容。
 *             用途：纯色 quad 采样图集内的白纹素（白×顶点色=顶点色，
 *             逐位精确），与字形共用同一纹理域，消除「纯色批 ↔ 字形批」
 *             因纹理切换而强制冲批（XGPU_SOLID_ATLAS_TEXEL=0 可回退到
 *             独立 1×1 白纹理的旧路径）。驱动一次写入 4×4 白块覆盖
 *             [0,4)×[0,4)；采样点取块内 (1,1) texel 中心，NEAREST 与
 *             LINEAR 下足迹全白。
 */
#define XGPU_RENDER_GLYPH_ATLAS_WHITE_BAND 4

/* ==================== 渲染目标句柄（渲染到纹理） ==================== */

/**
 * @brief      渲染目标句柄（不透明整数；0 = 无效/未创建）。
 * @details    pushRenderTarget 返回的句柄标识驱动持有的一份颜色纹理
 *             （+ 离屏帧缓冲）。句柄只在创建它的驱动会话生命周期内有
 *             效：会话销毁、窗口 resize 导致的重建、驱动切换之后全部
 *             失效（旧句柄再传入 push 返回 false，不静默换新）。
 *             调用方只应保存/比较句柄，不得解释其数值含义；内部纹理
 *             名/FBO 名不对通用层暴露。句柄由 1 单调递增分配（驱动内
 *             uint32，回绕时跳过 0），与既有纹理身份缓存的内部 GL 纹理
 *             名是两套独立身份，不共享命名空间。
 */
typedef uint32_t XGpuRenderTargetId;

/** @brief 无效渲染目标句柄（pushRenderTarget 失败时的输出值）。 */
#define XGPU_RENDER_TARGET_NONE 0u

/* ==================== 驱动会话（不透明） ==================== */

/**
 * @brief      渲染驱动会话；实现细节保持私有，不能包含系统句柄。
 * @details    GL 实现内部持有平台上下文（离屏表面或窗口 GL 上下文）、
 *             FBO/纹理/shader/顶点缓冲、覆盖度展开暂存与图集纹理；
 *             窗口会话与离屏会话由实现按 createSession 的 window 参数
 *             区分。生命周期由通用层经 procs 创建与销毁。
 */
typedef struct XGpuRenderDriverSession XGpuRenderDriverSession;

/* ==================== 驱动操作表 ==================== */

/**
 * @brief      渲染驱动操作表。
 * @details    全部操作以驱动会话为首参数；约定：
 *             - 会话创建成功后即已 makeCurrent（通用层无需显式切换即可
 *               初始化资源）；后续帧操作前由通用层显式 makeCurrent；
 *             - premulColor 一律为预乘 ARGB32（RGB 已乘 alpha，透明度
 *               已折入 alpha），驱动内部按预乘语义直接使用；
 *             - sourceOver=false 表示直接覆盖（Source）合成。
 */
typedef struct XGpuRenderDriverProcs
{
    /** @brief 运行环境是否可能支持本驱动（快速探测；最终以 sessionCreate 为准）。 */
    bool (*available)(void);

    /**
     * @brief      创建驱动会话并初始化全部驱动资源。
     * @param      window 目标窗口；NULL 表示离屏会话。
     * @param      width 渲染缓冲宽度（像素，>0）。
     * @param      height 渲染缓冲高度（像素，>0）。
     * @return     新驱动会话；失败返回 NULL（调用方按有序回退处理）。
     */
    XGpuRenderDriverSession* (*sessionCreate)(XWindow* window, int width,
                                              int height);

    /**
     * @brief      销毁驱动会话并释放全部驱动资源。
     * @param      session 驱动会话；NULL 不执行任何操作。释放后指针失效。
     * @return     无。
     */
    void (*sessionDestroy)(XGpuRenderDriverSession* session);

    /**
     * @brief      绑定驱动上下文到当前线程。
     * @param      session 驱动会话；NULL 返回 false。
     * @return     true 成功；false 上下文不可用。
     */
    bool (*makeCurrent)(XGpuRenderDriverSession* session);

    /**
     * @brief      解除当前线程的驱动上下文绑定。
     * @param      session 驱动会话；NULL 不执行任何操作。
     * @return     无。
     */
    void (*doneCurrent)(XGpuRenderDriverSession* session);

    /**
     * @brief      开始一帧：绑定渲染目标并复位视口/混合/裁剪状态。
     * @details    窗口会话为持久渲染目标：仅会话首帧用 initialImage
     *             （同尺寸时）初始化内容或清透明，其后保留上一帧内容；
     *             离屏会话每帧以 initialImage（同尺寸时）作为初始画布，
     *             否则清为全透明。
     * @param      session 驱动会话。
     * @param      initialImage 初始画布；NULL 或尺寸不符时按清透明处理。
     * @return     true 可绘制；false 上下文/渲染目标不可用。
     */
    bool (*beginFrame)(XGpuRenderDriverSession* session,
                       const XImage* initialImage);

    /**
     * @brief      结束一帧：解除渲染目标绑定并 doneCurrent。
     * @param      session 驱动会话；NULL 不执行任何操作。
     * @return     无。
     */
    void (*endFrame)(XGpuRenderDriverSession* session);

    /**
     * @brief      原地调整渲染目标尺寸（窗口 swapchain / 离屏渲染目标）。
     * @details    实现为可原地调整时返回 true（尺寸已更新）；不支持或
     *             失败返回 false（调用方销毁并重建整个会话）。窗口
     *             swapchain 重建后格式/renderPass 不变。
     * @param      session 驱动会话。
     * @param      width/height 新渲染尺寸（像素，>0）。
     * @return     true 已原地调整；false 调用方应销毁重建会话。
     */
    bool (*resize)(XGpuRenderDriverSession* session, int width, int height);

    /**
     * @brief      把渲染目标内容合成到窗口默认帧缓冲并交换上屏。
     * @details    仅窗口会话有效；优先 1:1 blit 快路径，回退全屏 quad
     *             采样。离屏会话返回 false。
     * @param      session 窗口驱动会话。
     * @return     true 已上屏；false 非窗口会话或上下文不可用。
     */
    bool (*presentToWindow)(XGpuRenderDriverSession* session);

    /**
     * @brief      把渲染目标内容回读到目标 ARGB32 图像（尺寸须一致）。
     * @param      session 驱动会话。
     * @param      target 目标图像（借用，调用方拥有）；尺寸不符返回 false。
     * @return     true 成功；false 会话无效或目标非法。
     */
    bool (*readback)(XGpuRenderDriverSession* session, XImage* target);

    /**
     * @brief      用预乘 ARGB32 颜色清空整帧。
     * @param      session 驱动会话。
     * @param      argb 预乘 ARGB32 颜色。
     * @return     无。会话无效时不执行任何操作。
     */
    void (*clear)(XGpuRenderDriverSession* session, uint32_t argb);

    /**
     * @brief      设置矩形裁剪（驱动渲染目标坐标）；NULL 清除裁剪。
     * @details    单矩形 scissor 语义；超出渲染目标的部分被钳位。
     * @param      session 驱动会话。
     * @param      rect 裁剪矩形（设备坐标）；NULL 清除裁剪。
     * @return     无。会话无效时不执行任何操作。
     */
    void (*setClipRect)(XGpuRenderDriverSession* session, const XRect* rect);

    /**
     * @brief      填充矩形（预乘纯色 quad）。
     * @param      session 驱动会话。
     * @param      rect 设备坐标矩形（宽高 >0）。
     * @param      premulColor 预乘 ARGB32 颜色（透明度已折入 alpha）。
     * @param      opacity 附加整体透明度（0.0~1.0，与 premulColor 相乘）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法或会话无效。
     */
    bool (*fillRect)(XGpuRenderDriverSession* session, const XRect* rect,
                     uint32_t premulColor, float opacity, bool sourceOver);

    /**
     * @brief      以最近邻纹理绘制完整图像（目标尺寸必须与源一致）。
     * @param      session 驱动会话。
     * @param      image 源图像（借用；ARGB32/ARGB32_Premultiplied 预乘语义）。
     * @param      x/y 目标位置（设备坐标）。
     * @param      width/height 目标尺寸（必须与源图像一致）。
     * @param      opacity 整体透明度（0.0~1.0）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法或会话无效。
     */
    bool (*drawImage)(XGpuRenderDriverSession* session, const XImage* image,
                      int x, int y, int width, int height, float opacity,
                      bool sourceOver);

    /**
     * @brief      任意 UV 子矩形绘制：源图像按 (u0,v0)-(u1,v1) 子区域
     *             拉伸到目标矩形（方向 B：渐变 LUT 等小纹理复用）。
     * @param      image 源图像（借用，调用期间有效；可为任意尺寸小纹理，
     *             与目标矩形尺寸解耦）。
     * @param      x/y/width/height 目标矩形（设备坐标）。
     * @param      u0/v0 采样起点、u1/v1 采样终点（纹理归一化坐标，
     *             (u0,v0) 对应矩形左上角、(u1,v1) 对应右下角）。
     * @param      opacity 整体透明度（0.0~1.0）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法、会话无效或驱动未实现
     *             （调用方应回退软件路径）。
     */
    bool (*drawImageUv)(XGpuRenderDriverSession* session,
                        const XImage* image, int x, int y, int width,
                        int height, float u0, float v0, float u1, float v1,
                        float opacity, bool sourceOver);

    /**
     * @brief      子矩形区域绘制：仅把源图像的 (srcX,srcY,srcW,srcH)
     *             区域上传到源纹理（TexSubImage 增量上传，非整幅重传），
     *             并 1:1 绘制到目标 (dstX,dstY)（批量提交的脏区通道——
     *             透明暂存画布只上传脏区，替代全帧快照+全帧上传）。
     * @param      image 源图像（借用；首次调用按整幅分配源纹理存储，
     *             尺寸变化时重新分配）。
     * @param      srcX/srcY/srcW/srcH 源区域（图像坐标，须位于图像内）。
     * @param      dstX/dstY 目标位置（设备坐标；宽高=srcW/srcH）。
     * @param      opacity 整体透明度（0.0~1.0）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法、会话无效或驱动未实现
     *             （调用方回退整幅路径）。
     */
    bool (*drawImageRegion)(XGpuRenderDriverSession* session,
                            const XImage* image, int srcX, int srcY,
                            int srcW, int srcH, int dstX, int dstY,
                            float opacity, bool sourceOver);

    /**
     * @brief      渐变×覆盖双纹理绘制（方向 B fillPath 原生化）。
     * @param      coverage 路径覆盖图（每像素 1 字节，bbox 局部；借用）。
     * @param      width/height 覆盖图尺寸（>0）。
     * @param      x/y 目标位置（设备坐标，= bbox 左上）。
     * @param      lutRgba 256×1 预乘 ARGB32 渐变 LUT（u 轴承载 t；借用）。
     * @param      opacity 整体不透明度（0.0~1.0）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法、会话无效或驱动未实现
     *             （调用方回退软件路径）。
     */
    bool (*drawGradientAlpha)(XGpuRenderDriverSession* session,
                              const unsigned char* coverage, int width,
                              int height, int x, int y,
                              const unsigned char* lutRgba, int lutAxis,
                              float opacity, bool sourceOver);

    /**
     * @brief      上传 CPU alpha 覆盖图并以指定颜色绘制（一次性，不缓存）。
     * @param      session 驱动会话。
     * @param      alpha 覆盖图（每像素 1 字节；借用，调用期间有效）。
     * @param      width/height 覆盖图尺寸（>0）。
     * @param      stride 行跨度（字节，>= width）。
     * @param      x/y 目标位置（设备坐标）。
     * @param      premulColor 预乘 ARGB32 颜色（透明度已折入 alpha）。
     * @param      opacity 附加整体透明度（0.0~1.0）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法或会话无效。
     */
    bool (*drawAlphaBitmap)(XGpuRenderDriverSession* session,
                            const uint8_t* alpha, int width, int height,
                            int stride, int x, int y, uint32_t premulColor,
                            float opacity, bool sourceOver);

    /**
     * @brief      把覆盖图上传到字形图集纹理的指定子矩形。
     * @details    覆盖图以"RGBA 四通道均为覆盖度"的形式写入图集（颜色
     *             在绘制期经 modulate 预乘）；(atlasX,atlasY) 由通用层
     *             装箱给出，必须位于图集纹理范围内。
     * @param      session 驱动会话。
     * @param      coverage 覆盖图（每像素 1 字节；借用）。
     * @param      width/height 覆盖图尺寸（>0，且 <= 图集边长）。
     * @param      atlasX/atlasY 图集内目标位置（纹素坐标，>=0）。
     * @return     true 已上传；false 参数非法或会话无效。
     */
    bool (*glyphAtlasUpload)(XGpuRenderDriverSession* session,
                             const uint8_t* coverage, int width, int height,
                             int atlasX, int atlasY);

    /**
     * @brief      绘制字形图集的一个条目子矩形（命中路径，零上传）。
     * @param      session 驱动会话。
     * @param      atlasX/atlasY/atlasWidth/atlasHeight 图集内条目子矩形
     *             （纹素坐标；subdiv>1 的灰度覆盖与二值覆盖同布局）。
     * @param      x/y 目标位置（设备坐标；尺寸与子矩形一致）。
     * @param      premulColor 预乘 ARGB32 颜色（透明度已折入 alpha）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法或会话无效。
     */
    bool (*glyphAtlasDraw)(XGpuRenderDriverSession* session, int atlasX,
                           int atlasY, int atlasWidth, int atlasHeight,
                           int x, int y, uint32_t premulColor,
                           bool sourceOver);

    /**
     * @brief      绘制任意四顶点纯色 quad（三角带 TL/TR/BL/BR）。
     * @details    画线快速路径的基础原语：轴对齐粗线、边框等由调用方
     *             计算四顶点（半开像素范围），驱动以纯色填充。
     * @param      session 驱动会话。
     * @param      x1/y1..x4/y4 四顶点设备坐标（浮点；顶点顺序
     *             TL,TR,BL,BR，与既有 quad 顶点排布一致）。
     * @param      premulColor 预乘 ARGB32 颜色（透明度已折入 alpha）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法或会话无效。
     */
    bool (*drawSolidQuad)(XGpuRenderDriverSession* session, float x1,
                          float y1, float x2, float y2, float x3, float y3,
                          float x4, float y4, uint32_t premulColor,
                          bool sourceOver);

    /**
     * @brief      把宿主图像整体上传为渲染目标内容（XGUI_GPU_SYNC=1
     *             同步调试模式专用）。
     * @details    通用层在每个原语前调用，使帧中的 CPU 直写
     *             （XImage_setPixel 等）对后续 GPU 绘制可见。可选操作：
     *             驱动未实现时该指针为 NULL，通用层跳过（帧中直写
     *             不可见的用例在同步模式下仍受限制）。
     * @param      session 驱动会话。
     * @param      image 宿主图像（借用；尺寸须与渲染目标一致）。
     * @return     true 已上传；false 参数非法、尺寸不符或会话无效。
     */
    bool (*uploadTargetImage)(XGpuRenderDriverSession* session,
                              const XImage* image);

    /**
     * @brief      从字形图集子矩形读回覆盖度（测试/诊断用，可选为 NULL）。
     * @details    驱动可返回 NULL 表示不支持读回；此时基于读回的断言
     *             由调用方跳过。
     * @param      session 驱动会话。
     * @param      atlasX/atlasY/atlasWidth/atlasHeight 图集内子矩形。
     * @param      outCoverage 输出缓冲（atlasWidth*atlasHeight 字节，调用方提供）。
     * @return     true 已读回；false 不支持或参数非法。
     */
    bool (*glyphAtlasReadback)(XGpuRenderDriverSession* session, int atlasX,
                               int atlasY, int atlasWidth, int atlasHeight,
                               uint8_t* outCoverage);

    /* ---------- 渲染到纹理（渲染目标栈；P3 场景保留的前置原语） ----------
       三个条目一律追加在操作表末尾（结构体字段增删与位置初始化器解耦
       的既有铁律；GL 表为指定初始化器，VK 表按位留 NULL）。
       可选能力：pushRenderTarget/popRenderTarget/drawRenderTarget 任一为
       NULL 即视为本驱动不支持渲染目标，通用层包装返回 false 且不崩溃
       （本轮 Vulkan 三个全留 NULL；接入点见 XGpuRenderDriver_vulkan.c
       的 g_xvklProcs）。 */

    /**
     * @brief      压入渲染目标：把后续绘制切换到一张离屏颜色纹理。
     * @details    成功后全部绘制原语（clear/fillRect/drawImage/字形/
     *             drawSolidQuad 等）都写入该目标的 RGBA8 颜色纹理，像素
     *             坐标语义与原渲染目标完全一致（设备 (0,0)=左上，y 向下；
     *             驱动不得为渲染目标另行引入翻转——采样回主目标时的
     *             y 方向约定见 drawRenderTarget）。驱动内部保存当前 FBO、
     *             视口、裁剪与渲染尺寸，popRenderTarget 原样恢复。
     *             目标内容初值：新建（或 reuse 且尺寸变化）清为全透明；
     *             reuse 且尺寸一致时保留既有内容（供跨帧增量重绘）。
     *             调用约束：须在 beginFrame 与 endFrame 之间配对调用；
     *             帧末仍未弹出的目标由驱动在 endFrame/beginFrame 处
     *             兜底弹出（栈不跨帧存续）。
     * @param      session 驱动会话。
     * @param      width/height 目标像素尺寸（>0）。
     * @param      reuseId 复用已有目标句柄；XGPU_RENDER_TARGET_NONE=新建。
     *             句柄不存在（含已失效/尺寸非法）返回 false——陈旧句柄
     *             不静默换新，防调用方以为拿到了旧内容。
     * @param      outTargetId 输出可用的目标句柄（不得为 NULL）。
     * @return     true 已切换（outTargetId 有效）；false 参数非法、会话
     *             或上下文无效、FBO 建立失败或目标池/栈已满（调用方应
     *             回退为直接绘制到当前目标）。
     */
    bool (*pushRenderTarget)(XGpuRenderDriverSession* session, int width,
                             int height, XGpuRenderTargetId reuseId,
                             XGpuRenderTargetId* outTargetId);

    /**
     * @brief      弹出渲染目标：恢复上一个渲染目标及其视口/裁剪状态。
     * @details    先把待定绘制提交到当前目标，再恢复 push 时保存的 FBO、
     *             渲染尺寸、视口与裁剪矩形。目标纹理内容保留（句柄仍可
     *             继续 reuse 做增量绘制，也可 drawRenderTarget 采样）。
     *             栈为空时为空操作并返回 false：重复 pop 不会破坏当前
     *             目标状态，也不会切换 FBO。
     * @param      session 驱动会话。
     * @return     true 已恢复上一个目标；false 栈为空、会话无效或上下文
     *             不可用（状态未改变）。
     */
    bool (*popRenderTarget)(XGpuRenderDriverSession* session);

    /**
     * @brief      把渲染目标的颜色纹理采样绘制到当前渲染目标。
     * @details    这是「纹理画回来」的驱动入口（drawImage 系列的纹理
     *             采样路径）：按目标矩形任意缩放，预乘 SourceOver/Source
     *             混合，透明度经 modulate 折入。纹理身份独立于既有
     *             纹理身份缓存（后者以 XImage 指针为键，本纹理无 XImage
     *             且从不进入该缓存），不产生键冲突。
     *             y 方向：渲染目标纹理的行 0 是对目标底行（GL 左下原点
     *             写入），与本驱动 readback 的行翻转同源——本原语内部
     *             翻转 V，使输出与 readback 得到同一朝向（设备的左上
     *             对应纹理 v=1）。调用方无需也不得再自行翻转。
     *             禁止把「正被渲染的目标」画进它自己（GL 反馈环）：
     *             驱动返回 false。
     * @param      session 驱动会话。
     * @param      targetId 渲染目标句柄（pushRenderTarget 返回）。
     * @param      x/y/width/height 目标矩形（当前渲染目标设备坐标）。
     * @param      opacity 整体透明度（0.0~1.0）。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 句柄无效、目标正在被渲染、参数非法、
     *             会话无效或驱动未实现（调用方回退软件/直绘）。
     */
    bool (*drawRenderTarget)(XGpuRenderDriverSession* session,
                             XGpuRenderTargetId targetId, int x, int y,
                             int width, int height, float opacity,
                             bool sourceOver);

    /**
     * @brief      批量绘制轴对齐矩形（同色一次提交）。
     * @details    「GPU 唯一渲染器」的实心扫描填充通道：XPainter 把扫描线
     *             跨距按行压成 1px 高矩形数组，驱动一次绑定+一次 draw
     *             全部提交（GL 追加进既有 quad 批；VK 走三角形列表管线），
     *             替代旧「CPU 逐像素写暂存画布 → 整块上传」的软件局部
     *             提交。跨距像素范围与软件扫描线**同源同参**（
     *             painterSpanPixelRange），逐位一致。
     *             矩形为设备空间、颜色为预乘 ARGB32（总透明度已折入）；
     *             调用方负责先设裁剪。
     * @param      session 驱动会话。
     * @param      rects 矩形数组（借用；宽高 >0）。
     * @param      count 矩形个数（>0）。
     * @param      premulColor 预乘 ARGB32 颜色。
     * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
     * @return     true 已提交；false 参数非法/会话无效/驱动未实现。
     */
    bool (*drawRectBatch)(XGpuRenderDriverSession* session, const XRect* rects,
                          const uint32_t* colors, int count,
                          uint32_t premulColor, bool sourceOver);

    /**
     * @brief      以**双线性**采样绘制源图子矩形（缩放贴图 GPU 通道）。
     * @details    契约与 drawImageUv 相同，唯一区别是采样过滤为双线性
     *             （对标 XPainter 的 SmoothPixmapTransform 提示）。驱动以
     *             线性采样器/线性纹理过滤实现；未实现时返回 false，调用
     *             方回退软件逐像素双线性（结果允许 ±1~2 量化差）。
     * @return     true 已提交；false 参数非法/会话无效/驱动未实现。
     */
    bool (*drawImageUvSmooth)(XGpuRenderDriverSession* session,
                              const XImage* image, int x, int y, int width,
                              int height, float u0, float v0, float u1,
                              float v1, float opacity, bool sourceOver);

    /**
     * @brief      帧内顶点/批缓冲压力百分比（0~100；可选，NULL=恒 0）。
     * @details    keep-open 让一次驱动帧跨多次 UI flush 累积顶点/命令；
     *             通用层在 **flush 边界（安全点）** 读取本值，达到阈值即
     *             提前 endFrame 收口（窗口渲染目标内容持久，下一帧
     *             beginFrame 保留内容），避免容量耗尽后逐原语回退软件
     *             画布（实测：VK 8MB 容量下每次拒绝的软件画布 flush
     *             4.27ms，占整帧 ~30%）。GL 无固定容量概念，留 NULL。
     * @param      session 驱动会话。
     * @return     0~100 的压力百分比；会话无效返回 0。
     */
    unsigned (*framePressurePercent)(XGpuRenderDriverSession* session);

    /**
     * @brief      GPU 解析抗锯齿线段（M4：替代 CPU 覆盖率光栅的 AA 线）。
     * @details    按「中心线 + 半线宽 + 端帽」在片元着色器里做 1 像素
     *             覆盖羽化（以屏幕空间导数求像素足迹），几何与颜色
     *             语义对齐 XPainter 的 AA 线条四边形：
     *               · 四边形纵向覆盖 [-halfWidth, +halfWidth]；
     *               · capStyle 0=FlatCap（端面不外延）、1=SquareCap
     *                 （两端沿方向各外延 halfWidth）；
     *               · premulColor 为预乘 ARGB32（opacity 已折入）。
     *             未实现/着色器编译失败时指针为 NULL（Vulkan 本阶段留
     *             NULL），调用方回退既有 CPU 覆盖率路径（逐位不变）。
     * @param      session 驱动会话。
     * @param      x0/y0/x1/y1 中心线端点（设备坐标，浮点，已裁剪）。
     * @param      halfWidth 半线宽（像素，>=0.5）。
     * @param      capStyle 0=FlatCap；1=SquareCap。
     * @param      premulColor 预乘 ARGB32 颜色。
     * @param      sourceOver true 预乘 SourceOver；false 直接覆盖。
     * @return     true 已提交；false 参数非法/会话无效/未实现。
     */
    bool (*drawAaLine)(XGpuRenderDriverSession* session,
                       float x0, float y0, float x1, float y1,
                       float halfWidth, int capStyle,
                       uint32_t premulColor, bool sourceOver);
} XGpuRenderDriverProcs;

/* ==================== 驱动注册表 ==================== */

/**
 * @brief      取指定类型的驱动操作表。
 * @details    编译期内置驱动注册表；类型未实现（如 Vulkan 骨架）或当前
 *             构建裁剪掉时返回 NULL，通用层据此按有序回退处理。
 * @param      type 驱动类型。
 * @return     驱动操作表（进程内单例，借用指针，不需释放）；类型未实现
 *             或参数非法返回 NULL。
 */
const XGpuRenderDriverProcs* XGpuRenderDriver_procs(XGpuRenderDriverType type);

/**
 * @brief      注入 mouse-grab 场景查询（GL 驱动的 PBO 滞后通道门控用）。
 * @details    分层单向依赖桥：GL 驱动不依赖 XWidget 头，由 GUI 初始化
 *             路径注入 XWidget_mouseGrabber 的薄包装（返回非 0 = 有交互
 *             抓取，驱动滞后通道即回同步直读保正确性）。未注入（默认
 *             NULL）= 驱动不自知交互态，维持既有开关口径。仅 GL 驱动
 *             消费；Vulkan/其它驱动忽略。
 * @param      query 查询函数（返回非 0 表示当前有鼠标抓取）；NULL 撤销。
 */
void XGpuRenderDriver_gl_setPointerGrabQuery(int (*query)(void));

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */

#endif /* XGUI_ON */

#endif /* XGPURENDERDRIVER_H */
