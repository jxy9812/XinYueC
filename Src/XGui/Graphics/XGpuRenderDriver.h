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

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */

#endif /* XGPURENDERDRIVER_H */
