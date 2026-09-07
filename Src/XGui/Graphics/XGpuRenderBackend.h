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

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XGeometry.h"

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
 * @brief      把当前 FBO 内容合成到窗口默认帧缓冲并 swapBuffers 上屏。
 * @details    仅窗口直通模式有效：makeCurrent(窗口) → 绑定默认帧缓冲 →
 *             全屏 quad 采样 FBO 颜色纹理 → swapBuffers → doneCurrent。
 * @return     true 已上屏；false 非窗口模式或上下文不可用。
 */
bool XGpuRenderBackend_presentToWindow(XGpuRenderBackend* self);

/* ==================== 全局会话管理（阶段 2 直通接线） ==================== */

/**
 * @brief      是否请求 GPU 渲染后端（读 XGUI_RENDER_BACKEND / XGPU_BACKEND
 *             环境变量，首次调用后缓存）。
 * @return     true 请求 GPU；false 默认软件。
 */
bool XGpuRenderBackend_requested(void);

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

/** @brief 标记本帧已发生软件降级（后续 flush 应走 BitBlt 而非 GPU present）。 */
void XGpuRenderBackend_setFrameDegraded(bool degraded);

/** @brief 查询本帧是否发生软件降级。 */
bool XGpuRenderBackend_frameDegraded(void);

/** @brief 记录最近一帧上屏方式（true=GPU present；false=BitBlt/软件）。 */
void XGpuRenderBackend_setFramePresented(bool presented);

/** @brief 最近一帧是否 GPU present 上屏（供截图/调试选择内容来源）。 */
bool XGpuRenderBackend_framePresented(void);

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

/** @brief 以最近邻纹理绘制完整图像；目标尺寸必须与源图像一致。 */
bool XGpuRenderBackend_drawImage(XGpuRenderBackend* self, const XImage* image,
                                 int x, int y, int width, int height,
                                 float opacity, bool sourceOver);

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

/** @brief 结束一帧：解除 FBO 绑定并 doneCurrent。 */
void XGpuRenderBackend_endFrame(XGpuRenderBackend* self);

#endif /* XPLATFORMINTEGRATION_ON && XGPU_ON */

#endif /* XGPURENDERBACKEND_H */
