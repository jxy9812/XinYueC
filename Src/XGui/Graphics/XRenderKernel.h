/******************************************************************************
 * @file       XRenderKernel.h
 * @brief      目标格式渲染内核表契约（软件光栅内核按 dest 格式分派）。
 * @details    对标 Skia blitter 思路并融合 LVGL 的目标格式内核组织：
 *             每种目标像素格式注册一张内核 ops 表（span 级原语函数指针），
 *             绘制热路径经 XRenderKernel_forFormat(dest) 一次性解析表，
 *             之后只剩指针调用、零格式分支——加新格式/新加速器（NEON/
 *             Helium/DMA2D）只新增注册条目，不动 painter。
 *             调用约定（全部 span 级、行基址寻址）：
 *             - rowBytes/dstRow 恒为目标扫描行首地址，x/count 为像素列；
 *             - 颜色与源行统一为 0xAARRGGBB 预乘 ARGB32（painter 规范色），
 *               内核自行压缩/展开到目标格式（RGB565 等）；
 *             - 未注册格式的槽位返回 NULL，调用方回退既有逐像素路径
 *               （零回归保障：默认 ARGB32_Premultiplied 路径不经本表）。
 * @note       模块归属光栅层，随 XPAINTER_ON 裁剪；内置内核注册见
 *             XRenderKernel.c（XRenderKernel_rgb565.c 为首个消费示例）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XRENDERKERNEL_H
#define XRENDERKERNEL_H

#include "XGuiConfig.h"

#if XPAINTER_ON

#include <stdint.h>
#include "XImageFormat.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 内核槽位容量：覆盖 XImageFormat 枚举全集（现 30+，留余量）。 */
#define XRENDERKERNEL_FORMAT_SLOTS 48

/** @brief 单一目标格式的渲染内核表（span 级原语；成员可缺省 NULL）。 */
typedef struct XRenderKernelOps
{
    /** 不透明纯色填充一段目标行（argbPrem 为预乘 ARGB32）。 */
    void (*fillSpanOpaque)(uint8_t* rowBytes, int x, int count,
                           uint32_t argbPrem);
    /** 半透明纯色 source-over 填充一段目标行（源预乘；alpha=255 时内部
     *  应走不透明直写免读改写）。 */
    void (*fillSpanBlend)(uint8_t* rowBytes, int x, int count,
                          uint32_t argbPrem);
    /** 不透明源行拷贝（srcRow 为 ARGB32/RGB32 规范行，alpha 恒 255）。 */
    void (*blitSpan)(uint8_t* dstRow, int dstX, const uint32_t* srcRow,
                     int srcX, int count);
    /** 预乘 source-over 源行混合（srcRow 为 ARGB32_Premultiplied 行）。 */
    void (*blendSpan)(uint8_t* dstRow, int dstX, const uint32_t* srcRow,
                      int srcX, int count);
    /** 字形灰度 mask 混合（mask 为 count 字节 0..255 覆盖率，调制
     *  colorPrem 后 source-over 入目标行）。 */
    void (*glyphMaskSpan)(uint8_t* rowBytes, int x, int count,
                          const uint8_t* mask, uint32_t colorPrem);
    /** putPixel 终端存储（预乘 source-over 单像素；XPainter 的
     *  putPixel 计算完合成色后经此写入，替代 XImage_setPixel 兜底）。 */
    void (*storePrem)(uint8_t* rowBytes, int x, uint32_t argbPrem);
} XRenderKernelOps;

/**
 * @brief      注册一张格式内核表（后注册覆盖先注册；NULL 清除槽位）。
 * @details    内置内核在 forFormat 首次调用时惰性注册（单线程 GUI 线程
 *             使用，无需加锁）；平台/加速器变体可在此之后覆盖注入。
 * @param      format 目标像素格式。
 * @param      ops 内核表；NULL 表示清除该格式注册。
 */
void XRenderKernel_register(XImageFormat format, const XRenderKernelOps* ops);

/**
 * @brief      按目标格式解析内核表。
 * @param      format 目标像素格式。
 * @return     已注册的内核表；未注册/非法格式返回 NULL（调用方回退
 *             既有逐像素路径，保证零回归）。
 */
const XRenderKernelOps* XRenderKernel_forFormat(XImageFormat format);

#ifdef __cplusplus
}
#endif

#endif /* XPAINTER_ON */
#endif /* XRENDERKERNEL_H */
