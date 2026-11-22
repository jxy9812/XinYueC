/******************************************************************************
 * @file       XGpuRenderBackend.h
 * @brief      XGpu 软件/GPU 渲染后端中的 GPU 光栅会话（阶段 1：OpenGL）。
 * @details    本对象是 XPainter 的 GPU 光栅后端：以平台离屏 GL 上下文
 *             （XPlatformOffscreenSurface）为宿主，FBO 离屏渲染 + 帧末
 *             readback 到绑定 XImage，对上层（XWidget/XBackingStore）完全
 *             透明。阶段 1 实现 clear / fillRect / drawImage，以及 CPU
 *             字形 alpha 位图上传；复杂变换和其它绘制由 XPainter 层按
 *             「整帧回退软件」策略保证正确性。
 *             本文件不包含任何平台 GL 头：全部 GL 函数经
 *             XPlatformOffscreenSurface_getProcAddress 在运行期解析
 *             （桌面 GLX/WGL 与嵌入式 GLES 共用同一套封装）。
 * @note       模块总开关 XPLATFORMINTEGRATION_ON 与 XGPU_ON 定义于
 *             XGuiConfig.h；任一处 0 时本模块整体裁剪（嵌入式无 GPU）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGPURENDERBACKEND_H
#define XGPURENDERBACKEND_H

#if XGUI_ON

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XGeometry.h"
#include "XGpuRenderDriver.h"

#if XPLATFORMINTEGRATION_ON && XGPU_ON

typedef struct XImage XImage;
typedef struct XWindow XWindow;

/** @brief GPU 光栅会话（不透明；实现细节仅在 .c 中）。 */
typedef struct XGpuRenderBackend XGpuRenderBackend;

/**
 * @brief      创建 GPU 光栅会话（离屏 GL 上下文 + FBO）。
 * @details    内部创建 XPlatformOffscreenSurface（可 makeCurrent 的离屏
 *             GL 上下文）、编译纯色 shader、建立与 width×height 等大的
 *             FBO/纹理。创建失败（无 GL 驱动/离屏表面不可用）返回 NULL，
 *             调用方据此回退软件渲染。
 * @param      width/height 渲染缓冲尺寸（与目标 XImage 一致）。
 * @return     会话指针；失败返回 NULL（用 XGpuRenderBackend_destroy 释放）。
 */
XGpuRenderBackend* XGpuRenderBackend_create(int width, int height);

/**
 * @brief      创建「窗口直通」GPU 光栅会话（阶段 2）。
 * @details    以 XPlatformOpenGLContext（绑定 XWindow 原生表面的 GL 上下文）
 *             为宿主，在窗口上下文中建立离屏 FBO 渲染；帧末经
 *             XGpuRenderBackend_presentToWindow 把 FBO 合成到窗口默认帧
 *             缓冲并 swapBuffers 直通上屏（不再 readback/BitBlt）。
 *             创建失败返回 NULL（调用方回退阶段 1 或软件）。
 * @param      window 绑定窗口（须已建立原生表面）；可为 NULL 则回退离屏。
 * @param      width/height 渲染缓冲尺寸。
 * @return     会话指针；失败返回 NULL。
 */
XGpuRenderBackend* XGpuRenderBackend_createForWindow(XWindow* window,
                                                     int width, int height);

/** @brief 会话是否为「窗口直通」模式（createForWindow 创建）。 */
bool XGpuRenderBackend_isWindowMode(const XGpuRenderBackend* self);

/**
 * @brief      查询会话实际使用的渲染驱动类型。
 * @details    显式请求 Vulkan 而驱动创建失败时会按 vulkan -> gl ->
 *             software 有序回退，本查询用于区分真实后端（防"请求
 *             Vulkan 实际跑 GL/软件"的验证假绿）。
 * @param      self 目标会话；NULL 返回 OpenGL（软件回退侧无会话对象）。
 * @return     实际驱动类型枚举。
 */
XGpuRenderDriverType XGpuRenderBackend_driverType(
        const XGpuRenderBackend* self);

/**
 * @brief      把当前 FBO 内容合成到窗口默认帧缓冲并 swapBuffers 上屏。
 * @details    仅窗口直通模式有效：makeCurrent(窗口) → 绑定默认帧缓冲 →
 *             全屏 quad 采样 FBO 颜色纹理 → swapBuffers → doneCurrent。
 * @return     true 已上屏；false 非窗口模式或上下文不可用。
 */
bool XGpuRenderBackend_presentToWindow(XGpuRenderBackend* self);

/* ==================== 全局会话管理（阶段 2 直通接线） ==================== */

/**
 * @brief      是否请求 GPU 渲染后端。解析优先级（高→低）：
 *             ① addRequestedOverride 运行期覆盖；② XGUI_RENDER_BACKEND /
 *             XGPU_BACKEND 环境变量（白名单 gpu/opengl/vulkan/1/true/on，
 *             其余值含 software 一律软件）；③ 编译期默认——桌面
 *             （XGPU_RUNTIME_DEFAULT_ON=1；2026-11-23 起 Windows 默认
 *             打开）以 probeAvailable 探测结果为准：探测按后端顺序
 *             **Vulkan → OpenGL → 软件**（VK 会话创建失败自动回退 GL），
 *             任一可用即 GPU、全部失败保持软件；非 Windows 平台仍恒软件。
 *             首次解析后缓存；默认层探测失败不落负缓存（预算内重探自愈）。
 * @return     true 请求 GPU；false 软件光栅。
 */
bool XGpuRenderBackend_requested(void);

/**
 * @brief      压入/弹出「仅软件光栅」作用域（框架内部用）。
 * @details    作用域内 XGpuRenderBackend_requested() 恒返回 false ⇒ 新建
 *             的 XPainter 绑定全部走软件光栅（不 acquire/不切换 GPU 会话，
 *             也不触碰当前窗口帧）。唯一消费方=XWidget 子树静态层重建：
 *             静态层画布是 CPU XImage，尺寸通常与窗口 FBO 不同；若让
 *             子绘制器在窗口帧中途 acquire 离屏会话，会拆建会话并逐
 *             painter readback（重建成本从「一次子树绘制」放大到
 *             「每控件一次整幅上传+回读」），故重建窗口内显式限定软件。
 *             必须严格配对（可嵌套，计数式）；作用域外行为不变。
 * @return     无返回值。
 */
void XGpuRenderBackend_pushSoftwareOnly(void);
void XGpuRenderBackend_popSoftwareOnly(void);
/** @brief 当前是否处于 pushSoftwareOnly 作用域（诊断用）。 */
bool XGpuRenderBackend_softwareOnlyActive(void);

/**
 * @brief      查询会话帧内缓冲压力百分比（0~100）。
 * @details    keep-open 跨 flush 累积顶点/命令时，通用层在 flush 边界
 *             （安全点）按本值决定是否提前收口帧；驱动未实现该能力
 *             （GL）恒返回 0，行为与旧路径一致。不得在帧中途调用后
 *             依赖其一致性——只在收口点消费。
 * @param      self 会话；NULL/无效返回 0。
 * @return     0~100 压力百分比。
 */
unsigned XGpuRenderBackend_framePressure(const XGpuRenderBackend* self);

/**
 * @brief      提交一条 GPU 解析抗锯齿线段（M4；可选能力）。
 * @details    薄转发到驱动的 drawAaLine（片元按屏幕空间导数做覆盖率
 *             羽化；GL 与 Vulkan 均已实现，VK 用生成的 SPIR-V 1.0
 *             片元）。驱动未实现或着色器装配失败时返回 false，调用方
 *             回退既有 CPU 覆盖率路径（像素逐位不变）。几何/颜色语义
 *             见驱动操作表同名条目。
 * @param      self 会话。
 * @param      x0/y0/x1/y1 中心线端点（设备坐标，浮点）。
 * @param      halfWidth 半线宽（像素）。
 * @param      capStyle 0=FlatCap；1=SquareCap。
 * @param      premulColor 预乘 ARGB32 颜色（opacity 已折入）。
 * @param      sourceOver true 预乘 SourceOver；false 直接覆盖。
 * @return     true 已提交；false 未实现/会话无效/批不可用。
 */
bool XGpuRenderBackend_drawAaLine(XGpuRenderBackend* self, float x0, float y0,
                                  float x1, float y1, float halfWidth,
                                  int capStyle, uint32_t premulColor,
                                  bool sourceOver);

/**
 * @brief      运行期请求覆盖：显式置位/禁用 GPU 渲染请求（优先于环境变量）。
 * @details    供宿主程序在 XGuiApplication 初始化前调用——「自动探测」
 *             模式下宿主先探测显卡可用性（XGpuRenderBackend_probeAvailable），
 *             成功则 addRequestedOverride(true) 让本进程默认走 GPU 直通，
 *             失败/无独显则保持软件。见 probeAvailable 注。
 * @param      on true=请求 GPU；false=撤销请求（回软件）。
 */
void XGpuRenderBackend_addRequestedOverride(bool on);

/**
 * @brief      轻量 GPU 可用性探测（1x1 离屏 GL 会话试创建，幂等缓存）。
 * @details    「自动探测」入口：创建 1x1 离屏 GL 上下文并验证核心函数
 *             加载——成功说明本机存在可用 OpenGL（独立/集成显卡均可），
 *             失败（无显卡/驱动缺失）说明应保持软件。结果进程内缓存；
 *             首次调用有一次性窗口创建成本（微秒级，无像素渲染）。
 *             XGPU_SESSION_RETRY 重试口径（默认开）下，失败不再一次性
 *             永久写死：累计失败达上限前，后续调用会重新探测（强杀残留
 *             类瞬时失败自愈）；XGPU_SESSION_RETRY=0 回退旧的一次性
 *             永久缓存口径。
 * @return     true 本机存在可用 OpenGL 上下文能力；false 不可用。
 */
bool XGpuRenderBackend_probeAvailable(void);


/**
 * @brief      把当前窗口设为「GPU 直通活动窗口」并返回其窗口会话。
 * @details    由 XWidget_repaint 在绘制前调用：首次为该窗口创建/复用窗口
 *             会话（createForWindow）；之后 XPainter 经
 *             XGpuRenderBackend_current() 获取并在其上绘制，flush 阶段经
 *             XGpuRenderBackend_presentToWindow 上屏。尺寸变化重建会话。
 * @param      window 目标窗口（须已有原生表面）。
 * @param      width/height 渲染缓冲尺寸。
 * @return     窗口会话；创建失败返回 NULL（上层回退软件/阶段 1）。
 */
XGpuRenderBackend* XGpuRenderBackend_acquireForWindow(XWindow* window,
                                                      int width, int height);

/** @brief 返回当前活动的 GPU 会话（acquireForWindow 设置的）；无则 NULL。 */
XGpuRenderBackend* XGpuRenderBackend_current(void);

/**
 * @brief      返回窗口直通会话（只读访问器，不改变活动会话/降级态）。
 * @details    `XGpuRenderBackend_current` 只在 flush 的绘制段内非 NULL
 *             （`endWindowFrame` 会清空），而呈现之后仍需要 CPU 像素的
 *             消费者——`XWidget_grab`、`XBackingStore_toImage`、拖动快照、
 *             演示截图——必须拿到持久会话才能把 FBO 读回后备图像。
 *             **swap 上屏模式（缺省）下 paintImage 不参与呈现**，直接深
 *             拷贝会拿到陈旧内容；本入口让这些消费者自行补一次 readback。
 * @return     窗口会话指针（借用，进程生命周期内有效）；未创建则 NULL。
 */
XGpuRenderBackend* XGpuRenderBackend_windowSession(void);

/** @brief 标记本帧已发生软件降级（后续 flush 应走 BitBlt 而非 GPU present）。 */
void XGpuRenderBackend_setFrameDegraded(bool degraded);

/** @brief 查询本帧是否发生软件降级。 */
bool XGpuRenderBackend_frameDegraded(void);

/** @brief 记录最近一帧上屏方式（true=GPU present；false=BitBlt/软件）。 */
void XGpuRenderBackend_setFramePresented(bool presented);

/** @brief 最近一帧是否 GPU present 上屏（供截图/调试选择内容来源）。 */
bool XGpuRenderBackend_framePresented(void);

/** @brief 最近创建会话的实际驱动类型（含离屏会话）。
 *  @param outValid 可空；输出该记录是否有效（尚无会话创建时为 false，
 *         此时返回 OpenGL 默认值）。current() 只对窗口会话非 NULL，
 *         离屏诊断必须经本入口取真实驱动（2026-09-24 假象修复）。 */
XGpuRenderDriverType XGpuRenderBackend_lastDriverType(bool* outValid);

/** @brief 结束当前窗口直通帧（present 后复位活动状态）。 */
void XGpuRenderBackend_endWindowFrame(void);

/** @brief 释放全部全局会话（进程退出/应用析构时调用；可重复安全）。 */
void XGpuRenderBackend_shutdown(void);

/** @brief 释放 GPU 光栅会话及其全部 GL 资源。 */
void XGpuRenderBackend_destroy(XGpuRenderBackend* self);

/** @brief 会话是否有效（创建成功且 GL 初始化完成）。 */
bool XGpuRenderBackend_isValid(const XGpuRenderBackend* self);

/** @brief 会话渲染缓冲宽度（与目标 XImage 一致）。 */
int XGpuRenderBackend_width(const XGpuRenderBackend* self);

/** @brief 会话渲染缓冲高度。 */
int XGpuRenderBackend_height(const XGpuRenderBackend* self);

/**
 * @brief      开始一帧：makeCurrent、绑定 FBO、设置视口与混合/裁剪状态。
 * @param      self 会话指针。
 * @return     true 可绘制；false 上下文不可用。
 */
bool XGpuRenderBackend_beginFrame(XGpuRenderBackend* self);

/** @brief 开始一帧并把已有 XImage 内容上传到 GPU FBO 作为初始画布。 */
bool XGpuRenderBackend_beginFrameImage(XGpuRenderBackend* self,
                                       const XImage* initialImage);

/** @brief 用指定 ARGB32 预乘颜色清空整帧。 */
void XGpuRenderBackend_clear(XGpuRenderBackend* self, uint32_t argb);

/**
 * @brief      填充矩形（阶段 1 GPU 原语）。
 * @details    rect 为设备坐标（调用方已应用平移/单位变换）；color 为
 *             ARGB32 预乘颜色；opacity 0.0~1.0 整体透明度（与 color 相乘）。
 *             sourceOver=true 使用预乘 SourceOver 混合，false 使用直接
 *             覆盖（Source）。裁剪由调用方经 setClipRect 设置。
 * @return     true 已提交；false 参数非法或会话无效。
 */
bool XGpuRenderBackend_fillRect(XGpuRenderBackend* self, const XRect* rect,
                                uint32_t color, float opacity,
                                bool sourceOver);

/**
 * @brief      批量绘制同色轴对齐矩形（一次提交）。
 * @details    实心扫描填充的 GPU 通道：逐行跨距（1px 高）一次提交。
 *             rects 为设备坐标、color 为预乘 ARGB32（opacity 已折入）；
 *             驱动未实现该原语时返回 false，调用方回退软件局部提交。
 * @param      self 会话。
 * @param      rects 矩形数组（借用）。
 * @param      count 矩形个数。
 * @param      color 预乘 ARGB32 颜色。
 * @param      sourceOver true 预乘 SourceOver；false 直接覆盖。
 * @return     true 已提交；false 参数非法/会话无效/驱动未实现。
 */
bool XGpuRenderBackend_drawRectBatch(XGpuRenderBackend* self,
                                     const XRect* rects,
                                     const uint32_t* colors, int count,
                                     uint32_t color, bool sourceOver);

/** @brief 以最近邻纹理绘制完整图像；目标尺寸必须与源图像一致。 */
bool XGpuRenderBackend_drawImage(XGpuRenderBackend* self, const XImage* image,
                                 int x, int y, int width, int height,
                                 float opacity, bool sourceOver);

/** @brief 任意 UV 子矩形绘制：源小纹理按 (u0,v0)-(u1,v1) 子区域拉伸
 *         到目标矩形（方向 B：渐变 LUT 等复用通道）。
 * @return true 已提交；false 参数非法、会话无效或驱动未实现。 */
bool XGpuRenderBackend_drawImageUv(XGpuRenderBackend* self,
                                   const XImage* image, int x, int y,
                                   int width, int height, float u0, float v0,
                                   float u1, float v1, float opacity,
                                   bool sourceOver);

/**
 * @brief      以双线性采样绘制源图子矩形（SmoothPixmapTransform 通道）。
 * @details    参数同 drawImageUv；采样过滤为双线性。驱动未实现返回
 *             false，调用方回退软件逐像素双线性（允许 ±1~2 量化差）。
 */
bool XGpuRenderBackend_drawImageUvSmooth(XGpuRenderBackend* self,
                                         const XImage* image, int x, int y,
                                         int width, int height, float u0,
                                         float v0, float u1, float v1,
                                         float opacity, bool sourceOver);

/** @brief 子矩形区域绘制：仅上传源图像 (srcX,srcY,srcW,srcH) 区域
 *         （TexSubImage 增量上传）并 1:1 绘制到目标 (dstX,dstY)——
 *         批量提交的脏区通道（透明暂存画布只上传脏区）。
 * @return true 已提交；false 参数非法、会话无效或驱动未实现
 *         （调用方回退整幅路径）。 */
bool XGpuRenderBackend_drawImageRegion(XGpuRenderBackend* self,
                                       const XImage* image, int srcX,
                                       int srcY, int srcW, int srcH,
                                       int dstX, int dstY, float opacity,
                                       bool sourceOver);

/** @brief 渐变×覆盖双纹理绘制（方向 B fillPath 原生化通道）。
 *  @param coverage 路径覆盖图（每像素 1 字节；借用，调用期间有效）。
 *  @param width/height 覆盖图尺寸（>0）。
 *  @param x/y 目标位置（设备坐标，= 覆盖图左上）。
 *  @param lutPremul 256×1 预乘 ARGB32 渐变 LUT（u 轴承载 t；借用）。
 *  @return true 已提交；false 参数非法、会话无效或驱动未实现
 *          （调用方回退软件路径）。 */
bool XGpuRenderBackend_drawGradientAlpha(XGpuRenderBackend* self,
                                         const unsigned char* coverage,
                                         int width, int height, int x, int y,
                                         const unsigned char* lutPremul,
                                         int lutAxis, float opacity,
                                         bool sourceOver);

/** @brief 上传 CPU 字形 alpha 位图并以指定颜色绘制。 */
bool XGpuRenderBackend_drawAlphaBitmap(XGpuRenderBackend* self,
                                       const uint8_t* alpha, int width,
                                       int height, int stride, int x, int y,
                                       uint32_t color, float opacity,
                                       bool sourceOver);

/**
 * @brief      经会话级字形图集绘制 CPU alpha 覆盖图（阶段 3）。
 * @details    key 唯一标识一份覆盖图（调用方由字体 face/codepoint/缩放
 *             混合得出）。命中时直接从图集纹理子矩形采样绘制，零 CPU
 *             转换与零纹理上传；未命中时把覆盖图（RGBA 四通道均为覆盖
 *             度，颜色在绘制期经 modulate 预乘）上传入集后再绘制——同
 *             一份缓存可服务任意颜色/透明度。图集满时整体重置（旧条目
 *             全部失效，随后按需重传）；单字形超过图集尺寸时回退
 *             drawAlphaBitmap 逐字形上传路径。
 * @param      key 覆盖图唯一键（相同键+宽高视为同一份覆盖图）。
 * @param      alpha 覆盖图（每像素 1 字节）；调用方已知命中时可传
 *             NULL（命中路径不读取 alpha；未命中且 alpha 为 NULL 返回
 *             false，调用方应回退光栅化后重试）。
 * @return     true 已提交；false 参数非法或会话无效。
 */
bool XGpuRenderBackend_drawGlyphAlpha(XGpuRenderBackend* self,
                                      uint64_t key, int width, int height,
                                      const uint8_t* alpha, int stride,
                                      int x, int y, uint32_t color,
                                      float opacity, bool sourceOver);

/**
 * @brief      绘制任意四顶点纯色 quad（画线快速路径基础原语）。
 * @details    顶点为设备坐标浮点值（半开像素范围），顶点顺序 TL/TR/
 *             BL/BR 与既有 quad 一致；颜色为预乘 ARGB32（透明度已
 *             折入 alpha）。
 * @param      self 会话。
 * @param      x1/y1..x4/y4 四顶点设备坐标。
 * @param      premulColor 预乘 ARGB32 颜色。
 * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
 * @return     true 已提交；false 参数非法或会话无效。
 */
bool XGpuRenderBackend_drawSolidQuad(XGpuRenderBackend* self, float x1,
                                     float y1, float x2, float y2, float x3,
                                     float y3, float x4, float y4,
                                     uint32_t premulColor, bool sourceOver);

/**
 * @brief      注册/清除命令级同步读回目标（XGUI_GPU_SYNC=1 调试模式）。
 * @details    XPainter 绑定图像时调用；注册后每个 GPU 原语提交都会把
 *             渲染目标读回该图像（帧中像素可见，供"绘制后立即断言"的
 *             回归用例使用）。默认关闭，性能大幅下降，仅用于回归。
 * @param      target 目标图像（借用）；NULL 清除注册。
 * @return     无。
 */
void XGpuRenderBackend_setSyncTarget(XGpuRenderBackend* self, XImage* target);

/**
 * @brief      把宿主同步目标图像（setSyncTarget 注册的）整体上传为
 *             渲染目标内容（XGUI_GPU_SYNC 调试模式专用）。
 * @param      self 会话。
 * @return     true 已上传（或无注册目标）；false 会话无效或上传失败。
 */
bool XGpuRenderBackend_uploadFrame(XGpuRenderBackend* self, XImage* target);

/**
 * @brief      查询覆盖图是否已在字形图集中（配合 alpha=NULL 的命中绘制，
 *             让调用方在命中时完全跳过 CPU 光栅化）。
 */
bool XGpuRenderBackend_glyphAtlasContains(const XGpuRenderBackend* self,
                                          uint64_t key, int width, int height);

/** @brief 清空字形图集条目并复位统计计数（测试/调试；会话销毁自动释放）。 */
void XGpuRenderBackend_resetGlyphAtlas(XGpuRenderBackend* self);

/** @brief 字形图集累计上传次数（图集满重置后的重传也计入）。 */
unsigned XGpuRenderBackend_glyphAtlasUploadCount(const XGpuRenderBackend* self);

/** @brief 字形图集累计命中次数。 */
unsigned XGpuRenderBackend_glyphAtlasHitCount(const XGpuRenderBackend* self);

/**
 * @brief      设置矩形裁剪（设备坐标）；NULL 清除裁剪。
 * @details    阶段 1 仅支持单矩形裁剪（scissor）；复杂裁剪由上层回退软件。
 */
void XGpuRenderBackend_setClipRect(XGpuRenderBackend* self,
                                   const XRect* rect);

/**
 * @brief      把 FBO 帧 readback 到目标 XImage（ARGB32 预乘）。
 * @details    目标 XImage 尺寸须与会话一致；内部做 GL（左下原点）到
 *             XImage（左上原点）的 Y 翻转，并将 GL_RGBA 结果转换为
 *             XImage 的 ARGB32 语义。调用方在 endFrame 前调用。
 * @return     true 成功；false 会话无效或目标非法。
 */
bool XGpuRenderBackend_readback(XGpuRenderBackend* self, XImage* target);

/**
 * @brief      把 FBO 帧的子矩形区域读回到目标 XImage 同坐标区域（脏区读回）。
 * @details    readback 的区域版（P-dirty-readback 2026-09-25）：只把
 *             (x,y,width,height)（设备坐标）从 FBO 读回并写入 target 的
 *             同坐标区域，其余像素保持不动——供脏区呈现链把「每帧全窗
 *             读回」收敛为「本帧呈现区域读回」。语义对齐 readback：目标
 *             XImage 尺寸须与会话一致、GL→XImage 行序翻转与 RGBA→ARGB32
 *             转换一致；越界部分钳位到会话范围内，钳位后为空视为成功
 *             （无事可做，不回退）。活动驱动非 GL（操作表指针身份不符）
 *             时返回 false，调用方回退整帧 readback（行为等价旧路径）。
 *             调用方在 endFrame 前调用。
 * @return     true 成功（含空区域无事可做）；false 会话无效、目标非法
 *             或驱动不支持。
 */
bool XGpuRenderBackend_readbackRect(XGpuRenderBackend* self, XImage* target,
                                    int x, int y, int width, int height);

/** @brief 结束一帧：解除 FBO 绑定并 doneCurrent。 */
void XGpuRenderBackend_endFrame(XGpuRenderBackend* self);

/* ==================== 渲染到纹理（渲染目标栈转发） ==================== */

/**
 * @brief      会话的实际驱动是否支持「渲染到纹理」原语。
 * @details    能力判据=驱动操作表三个条目（push/pop/drawRenderTarget）
 *             全部非 NULL（本轮 GL 支持、Vulkan 未实现）。调用方在任何
 *             push 之前用它决定是否走 GPU 离屏路径；返回 false 时下游
 *             包装函数一律返回失败值，不会崩溃。
 * @param      self 会话；NULL 返回 false。
 * @return     true 支持；false 不支持（调用方保持既有直绘路径）。
 */
bool XGpuRenderBackend_supportsRenderTargets(const XGpuRenderBackend* self);

/**
 * @brief      压入渲染目标（后续绘制切到离屏颜色纹理）。
 * @details    薄转发：参数/语义见驱动接口 XGpuRenderDriverProcs 的同名
 *             条目。目标由驱动会话持有并释放（会话销毁时配对释放），
 *             通用层不分配任何资源；reuseId 复用同一纹理可避免每帧
 *             重建存储。成功后目标处于当前栈顶，须以 popRenderTarget
 *             配对退出。
 * @param      self 会话。
 * @param      width/height 目标像素尺寸（>0）。
 * @param      reuseId 复用句柄；XGPU_RENDER_TARGET_NONE=新建。
 * @param      outTargetId 输出句柄（不得为 NULL；失败写 NONE）。
 * @return     true 已切换；false 会话/驱动不支持、参数非法或创建失败。
 */
bool XGpuRenderBackend_pushRenderTarget(XGpuRenderBackend* self, int width,
                                        int height,
                                        XGpuRenderTargetId reuseId,
                                        XGpuRenderTargetId* outTargetId);

/**
 * @brief      弹出渲染目标（恢复上一个目标及其视口/裁剪）。
 * @details    栈为空时为无副作用失败（重复 pop 不破坏当前目标）。
 * @param      self 会话。
 * @return     true 已恢复上一个目标；false 栈为空、会话或驱动不支持。
 */
bool XGpuRenderBackend_popRenderTarget(XGpuRenderBackend* self);

/**
 * @brief      当前渲染目标栈深度（0=直接绘制到会话渲染目标）。
 * @details    供调用方做配对校验/收尾兜底（异常路径一次 pop 到 0）。
 *             深度由通用层按成功 push/pop 计数，驱动侧兜底弹栈
 *             （帧末未弹出的目标）不会反映到这里——帧末仍非 0 即
 *             说明调用方漏了 pop，应视为缺陷并修正。
 * @param      self 会话；NULL 返回 0。
 * @return     >=0 的栈深度。
 */
int XGpuRenderBackend_renderTargetDepth(const XGpuRenderBackend* self);

/**
 * @brief      把渲染目标纹理采样绘制到当前渲染目标（纹理画回来）。
 * @details    薄转发到驱动的 drawRenderTarget：按目标矩形缩放、预乘
 *             混合、内部做渲染目标→图像朝向的 V 翻转（与 readback 的
 *             行翻转同源），输出朝向与 drawImage 一致。纹理身份独立于
 *             以 XImage 指针为键的纹理身份缓存，无键冲突；本入口永不
 *             回退到软件路径（失败即返回 false，由调用方决定回退）。
 * @param      self 会话。
 * @param      targetId 目标句柄（pushRenderTarget 返回）。
 * @param      x/y/width/height 目标矩形（当前渲染目标设备坐标）。
 * @param      opacity 整体透明度（0.0~1.0）。
 * @param      sourceOver true 预乘 SourceOver 混合；false 直接覆盖。
 * @return     true 已提交；false 句柄无效/驱动未实现/参数非法。
 */
bool XGpuRenderBackend_drawRenderTarget(XGpuRenderBackend* self,
                                        XGpuRenderTargetId targetId, int x,
                                        int y, int width, int height,
                                        float opacity, bool sourceOver);

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */

#endif /* XGUI_ON */

#endif /* XGPURENDERBACKEND_H */
