/** @file XImageCodec_config.h
 * @brief XGui XImageCodec 图像编解码模块配置文件。
 * @note  通过本文件可以逐个裁剪 XImageCodec 支持的图像格式，仅保留实际
 *        需要的格式以减小嵌入式固件体积：
 *          1. XIMAGECODEC_BMP_ON   - BMP（DIB≥40；24/32 位 BI_RGB，正/倒行序，
 *                                   扩展：1/2/4/8 位索引、16 位 RGB555/565/BITFIELDS、
 *                                   RLE4/RLE8、BITMAPV4/V5 头与 Alpha 掩码）
 *          2. XIMAGECODEC_PNG_ON   - PNG（8 位灰度/灰度+Alpha/RGB/RGBA、反滤波 0~4；
 *                                   扩展：调色板 tRNS、16 位深、Adam7 隔行；
 *                                   zlib 复用库内自带实现，零第三方依赖）
 *          3. XIMAGECODEC_JPEG_ON  - JPEG（基线 8 位 SOF0 编码/解码，固定 4:2:0；
 *                                   扩展：渐进 SOF2、12 位、算术编码、CMYK/YCCK）
 *          4. XIMAGECODEC_GIF_ON   - GIF（GIF87a/GIF89a、全局/局部调色板、透明色；
 *                                   扩展：多帧动画 GCE 延迟/处置 + Netscape 循环）
 *          5. XIMAGECODEC_PPM_ON   - PPM/PBM/PGM（P1-P6 ASCII 与二进制变体；
 *                                   P4/P5/P6 子类型写出）
 *          6. XIMAGECODEC_XBM_ON   - X11 XBM（MonoLSB 头部、十六进制位图；
 *                                   支持读取和写出）
 *          7. XIMAGECODEC_XPM_ON   - X11 XPM（C 字符串数组、调色板、透明色；
 *                                   支持读取和写出）
 *          8. XIMAGECODEC_SVG_ON   - SVG/SVGZ（普通 XML、gzip 压缩 SVG；
 *                                   扩展：路径/形状/文字/渐变/变换等矢量渲染）
 *          9. XIMAGECODEC_ICO_ON   - ICO/CUR（首个条目、嵌入 PNG、1/4/8/24/32 位 DIB）
 *
 *        模块总开关 XIMAGECODEC_ON 在 XGuiConfig.h 中统一定义，此处仅
 *        提供兜底默认值；置 0 时裁剪整个 XImageCodec 对外公共 API 和全部
 *        格式算法。关闭某个格式开关后：
 *          - XImageCodec_canDecode/canEncode 对该格式返回 false；
 *          - XImageCodec_detect/formatFromName 不再识别该格式；
 *          - XImageCodec_decode/encode 对该格式直接返回 false。
 *        各格式的“扩展特性”开关默认全开（PC 侧获得完整支持），嵌入式裁剪
 *        时置 0 即可移除对应算法，相关格式自动降级为最小能力集合。
 *        关闭后若仍有其它模块无条件引用 XImageCodec 符号，需同步裁剪
 *        对应依赖。
 */

#ifndef XIMAGECODEC_CONFIG_H
#define XIMAGECODEC_CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

/* 引入 XGui 配置，确保 XIMAGECODEC_ON 主开关已定义。 */
#include "XGuiConfig.h"

/* ========================================================================== */
/*                        模块总开关                                          */
/* ========================================================================== */
/** @brief XImageCodec 模块总开关；置 0 时裁剪整个图像编解码公共 API 和所有子格式。
 *  @note 总开关在 XGuiConfig.h 中统一定义，此处仅兜底默认值。
 */
#ifndef XIMAGECODEC_ON
#define XIMAGECODEC_ON 1
#endif

#if XIMAGECODEC_ON

/* ========================================================================== */
/*                        各图像格式开关                                      */
/* ========================================================================== */

/* 扩展功能主控：
 *  各格式的“扩展特性”默认开启（PC/桌面环境获得完整支持），嵌入式裁剪时
 *  将对应开关置 0 即可移除对应算法，缩小固件体积。扩展开关只控制新增能力：
 *  关闭后相关格式降级为旧有最小能力集合（见各格式注释）。 */

/** @brief Windows BMP 格式（24/32 位无压缩，含 Alpha）。 */
#ifndef XIMAGECODEC_BMP_ON
#define XIMAGECODEC_BMP_ON 1
#endif

/** @brief BMP 扩展：1/2/4/8 位索引色、16 位（RGB555/565/BITFIELDS）解码。 */
#ifndef XIMAGECODEC_BMP_INDEXED_ON
#define XIMAGECODEC_BMP_INDEXED_ON 1
#endif

/** @brief BMP 扩展：RLE4/RLE8 行程编码解码。 */
#ifndef XIMAGECODEC_BMP_RLE_ON
#define XIMAGECODEC_BMP_RLE_ON 1
#endif

/** @brief BMP 扩展:BITMAPV4/V5 头、32 位 BITFIELDS 与 Alpha 掩码解码。 */
#ifndef XIMAGECODEC_BMP_V45_ON
#define XIMAGECODEC_BMP_V45_ON 1
#endif

/** @brief PNG 格式（基础：8 位灰度/灰度+Alpha/RGB/RGBA，使用库内 zlib）。 */
#ifndef XIMAGECODEC_PNG_ON
#define XIMAGECODEC_PNG_ON 1
#endif

/** @brief PNG 扩展：调色板（ColorType 3，位深 1/2/4/8，含 tRNS 透明）。 */
#ifndef XIMAGECODEC_PNG_PALETTE_ON
#define XIMAGECODEC_PNG_PALETTE_ON 1
#endif

/** @brief PNG 扩展：16 位深（灰度/RGB/灰度+Alpha/RGBA）解码。 */
#ifndef XIMAGECODEC_PNG_16BIT_ON
#define XIMAGECODEC_PNG_16BIT_ON 1
#endif

/** @brief PNG 扩展：Adam7 隔行解码与 1/2/4 位子采样展开。 */
#ifndef XIMAGECODEC_PNG_INTERLACE_ON
#define XIMAGECODEC_PNG_INTERLACE_ON 1
#endif

/** @brief JPEG 格式（基础：基线 8 位顺序编码/解码，零第三方依赖）。 */
#ifndef XIMAGECODEC_JPEG_ON
#define XIMAGECODEC_JPEG_ON 1
#endif

/** @brief JPEG 扩展：渐进式（SOF2 多扫描 + 逐次逼近）解码。 */
#ifndef XIMAGECODEC_JPEG_PROGRESSIVE_ON
#define XIMAGECODEC_JPEG_PROGRESSIVE_ON 1
#endif

/** @brief JPEG 扩展：12 位精度（SOF 支持 12）解码。 */
#ifndef XIMAGECODEC_JPEG_12BIT_ON
#define XIMAGECODEC_JPEG_12BIT_ON 1
#endif

/** @brief JPEG 扩展：CMYK/YCCK 四分量（含 Adobe APP14 转换）解码。 */
#ifndef XIMAGECODEC_JPEG_CMYK_ON
#define XIMAGECODEC_JPEG_CMYK_ON 1
#endif

/** @brief JPEG 扩展：JPEG-LS 之外的算术编码（JPEG 规范 Annex D）解码。 */
#ifndef XIMAGECODEC_JPEG_ARITHMETIC_ON
#define XIMAGECODEC_JPEG_ARITHMETIC_ON 1
#endif

/** @brief GIF 格式（GIF87a/GIF89a 静态首帧）。 */
#ifndef XIMAGECODEC_GIF_ON
#define XIMAGECODEC_GIF_ON 1
#endif

/** @brief GIF 扩展：多帧动画解码（GCE 延迟/透明/处置 + Netscape 循环）。 */
#ifndef XIMAGECODEC_GIF_ANIM_ON
#define XIMAGECODEC_GIF_ANIM_ON 1
#endif

/** @brief PPM 家族（P1-P6：PBM/PGM/PPM ASCII 与二进制变体）。 */
#ifndef XIMAGECODEC_PPM_ON
#define XIMAGECODEC_PPM_ON 1
#endif

/** @brief X11 XBM 位图格式（MonoLSB 头部与十六进制字节数组）。 */
#ifndef XIMAGECODEC_XBM_ON
#define XIMAGECODEC_XBM_ON 1
#endif

/** @brief X11 XPM 彩色图格式（C 字符串数组、调色板与透明色）。 */
#ifndef XIMAGECODEC_XPM_ON
#define XIMAGECODEC_XPM_ON 1
#endif

/** @brief SVG 格式（基础：内嵌 PNG 或纯色矩形栅格化）。 */
#ifndef XIMAGECODEC_SVG_ON
#define XIMAGECODEC_SVG_ON 1
#endif

/** @brief Windows ICO/CUR 格式（首个目录条目、嵌入 PNG、24/32 位 DIB）。 */
#ifndef XIMAGECODEC_ICO_ON
#define XIMAGECODEC_ICO_ON 1
#endif

/** @brief SVG 扩展：矢量渲染（路径/矩形/圆/椭圆/线/折线/多边形、
 *         填充/描边、线性/径向渐变、viewBox 变换、transforms、ASCII 文字）。 */
#ifndef XIMAGECODEC_SVG_VECTOR_ON
#define XIMAGECODEC_SVG_VECTOR_ON 1
#endif

#endif /* XIMAGECODEC_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIMAGECODEC_CONFIG_H */
