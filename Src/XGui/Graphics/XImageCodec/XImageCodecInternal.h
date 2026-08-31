/** @file       XImageCodecInternal.h
 * @brief      XImageCodec 内部共享工具与各格式编解码入口（不对外公开）。
 * @note       仅供 XImageCodec.c 及各图像格式实现文件包含。
 *              公共编解码 API 见 XImageCodec.h，上层图像类只应使用
 *              XImageCodec_* 公共接口，不应包含本头文件。
 *              本文件受 XImageCodec_config.h 的 XIMAGECODEC_ON 总开关与
 *              XIMAGECODEC_BMP_ON / PNG_ON / JPEG_ON / GIF_ON / SVG_ON
 *              各格式开关控制：关闭的格式其编解码入口声明被裁剪。
 * @author     XinYueC 团队
 */
#ifndef XIMAGECODEC_INTERNAL_H
#define XIMAGECODEC_INTERNAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "XImageCodec.h"
#include "XImageCodec_config.h"
#include "XByteArray.h"
#include "XImage.h"

#if XIMAGECODEC_ON

/* ====================================================================== */
/* 公共端序读写工具（无符号小端/大端，跨平台按字节序解释）                  */
/* ====================================================================== */

/**
 * @brief 读取 2 字节小端无符号整数。
 * @param p 数据指针，取前 2 字节，不能为 NULL。
 * @return 解析得到的数值。
 */
uint16_t XImageCodecInternal_readU16LE(const uint8_t* p);

/**
 * @brief 读取 4 字节小端无符号整数。
 * @param p 数据指针，取前 4 字节，不能为 NULL。
 * @return 解析得到的数值。
 */
uint32_t XImageCodecInternal_readU32LE(const uint8_t* p);

/**
 * @brief 读取 2 字节大端无符号整数。
 * @param p 数据指针，取前 2 字节，不能为 NULL。
 * @return 解析得到的数值。
 */
uint16_t XImageCodecInternal_readU16BE(const uint8_t* p);

/**
 * @brief 读取 4 字节大端无符号整数。
 * @param p 数据指针，取前 4 字节，不能为 NULL。
 * @return 解析得到的数值。
 */
uint32_t XImageCodecInternal_readU32BE(const uint8_t* p);

/**
 * @brief 向缓冲区写入 2 字节小端无符号整数。
 * @param p     目标缓冲区，需要至少 2 字节空间。
 * @param value 待写入数值。
 */
void XImageCodecInternal_writeU16LE(uint8_t* p, uint16_t value);

/**
 * @brief 向缓冲区写入 2 字节大端无符号整数。
 * @param p     目标缓冲区，需要至少 2 字节空间。
 * @param value 待写入数值。
 */
void XImageCodecInternal_writeU16BE(uint8_t* p, uint16_t value);

/**
 * @brief 向缓冲区写入 4 字节小端无符号整数。
 * @param p     目标缓冲区，需要至少 4 字节空间。
 * @param value 待写入数值。
 */
void XImageCodecInternal_writeU32LE(uint8_t* p, uint32_t value);

/**
 * @brief 向缓冲区写入 4 字节大端无符号整数。
 * @param p     目标缓冲区，需要至少 4 字节空间。
 * @param value 待写入数值。
 */
void XImageCodecInternal_writeU32BE(uint8_t* p, uint32_t value);

/* ====================================================================== */
/* XByteArray 追加工具                                                     */
/* ====================================================================== */

/**
 * @brief 将一段二进制数据追加到 XByteArray 末尾。
 * @param out  目标字节数组；不能为 NULL。
 * @param data 待追加数据；size 为 0 时可为 NULL。
 * @param size 待追加字节数。
 * @return 成功返回 true；参数无效或扩容失败返回 false。
 */
bool XImageCodecInternal_appendBytes(XByteArray* out,
                                     const void* data, size_t size);

/* ====================================================================== */
/* 各图像格式编解码入口（由 XImageCodec.c 统一分发，随格式开关裁剪）          */
/* ====================================================================== */

#if XIMAGECODEC_BMP_ON
/**
 * @brief 解码 BMP 数据到 XImage。
 * @param data 输入 BMP 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out  输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeBmp(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 解码不含 BMP 文件头的 DIB 数据到 XImage。
 * @param data 输入 DIB 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out  输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeDib(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 将 XImage 编码为 BMP 数据。
 * @param image 输入图像；不能为 NULL。
 * @param out   输出字节数组；不能为 NULL。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodeBmp(const XImage* image, XByteArray* out);

/**
 * @brief 将 XImage 编码为不含 BMP 文件头的 DIB 数据。
 * @param image 输入图像；不能为 NULL。
 * @param out   输出字节数组；不能为 NULL。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodeDib(const XImage* image, XByteArray* out);
#endif /* XIMAGECODEC_BMP_ON */

#if XIMAGECODEC_PNG_ON
/**
 * @brief 解码 PNG 数据到 XImage。
 * @param data 输入 PNG 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out  输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodePng(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 将 XImage 编码为 PNG 数据。
 * @param image 输入图像；不能为 NULL。
 * @param out   输出字节数组；不能为 NULL。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodePng(const XImage* image, XByteArray* out);
#endif /* XIMAGECODEC_PNG_ON */

#if XIMAGECODEC_GIF_ON
/**
 * @brief 解码 GIF 数据到 XImage。
 * @param data 输入 GIF 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out  输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeGif(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 将 XImage 编码为 GIF 数据。
 * @param image 输入图像；不能为 NULL。
 * @param out   输出字节数组；不能为 NULL。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodeGif(const XImage* image, XByteArray* out);

#if XIMAGECODEC_GIF_ANIM_ON
/**
 * @brief 解码 GIF 多帧动画到 XImageCodecAnimation。
 * @param data      输入 GIF 数据；不能为 NULL。
 * @param size      数据字节数。
 * @param animation 输出动画对象（先置零）；成功时 frames 由调用者释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeGifFrames(const uint8_t* data, size_t size,
                                         XImageCodecAnimation* animation);
#endif /* XIMAGECODEC_GIF_ANIM_ON */
#endif /* XIMAGECODEC_GIF_ON */

#if XIMAGECODEC_JPEG_ON
/**
 * @brief 解码 JPEG 数据到 XImage。
 * @note  支持基线/渐进/算术编码、8/12 位精度与 YCbCr/灰度/CMYK 四分量
 *        （SOF0/SOF1/SOF2），抽样因子 1/2/4 与 DRI/RSTn 重启间隔；
 *        不支持 4:1:1、尺寸超过 65535 的异常数据等非规范输入。
 * @param data 输入 JPEG 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out  输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeJpeg(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 将 XImage 编码为基线 JPEG（YCbCr 4:2:0）数据。
 * @param image   输入图像；不能为 NULL。
 * @param quality 质量参数（1..100，<=0 表示默认 75）。
 * @param out     输出字节数组；成功后 out 包含编码数据。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodeJpeg(const XImage* image, int quality,
                                    XByteArray* out);
#endif /* XIMAGECODEC_JPEG_ON */

#if XIMAGECODEC_SVG_ON
/**
 * @brief 解码 SVG 数据到 XImage。
 * @param data 输入 SVG 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out  输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeSvg(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 从 SVG 数据中探测默认宽与高。
 * @param data   输入 SVG 数据；不能为 NULL。
 * @param size   输入数据字节数。
 * @param width  输出宽度；成功后大于 0。
 * @param height 输出高度；成功后大于 0。
 * @return 成功返回 true；无宽高/viewBox 或解析失败返回 false。
 * @note 优先读取根 svg 的 width/height 属性，缺失时回退 viewBox 尺寸，
 *       与解码路径的尺寸选择一致，不解码整幅图像。
 */
bool XImageCodecInternal_probeSvgSize(const uint8_t* data, size_t size,
                                      int* width, int* height);

/**
 * @brief 将 XImage 编码为 SVG 数据。
 * @param image 输入图像；不能为 NULL。
 * @param out   输出字节数组；不能为 NULL。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodeSvg(const XImage* image, XByteArray* out);
#endif /* XIMAGECODEC_SVG_ON */

#endif /* XIMAGECODEC_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIMAGECODEC_INTERNAL_H */
