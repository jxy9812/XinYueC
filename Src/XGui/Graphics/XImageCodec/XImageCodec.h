/******************************************************************************
 * @file       XImageCodec.h
 * @brief      独立图像编解码算法统一接口。
 * @note       各格式算法分别实现于 XImageCodecBmp.c / XImageCodecPng.c /
 *              XImageCodecGif.c / XImageCodecSvg.c / XImageCodecJpeg.c，
 *              通过本接口统一集成；
 *              格式算法不依赖 XImageReader、XImageWriter 或 XPixmap，
 *              上层图像类（XImage、XPixmap、XImageReader、XImageWriter 等）
 *              统一调用本模块的 XImageCodec_* 公共 API 完成编解码。
 *              五种格式可通过 XImageCodec_config.h 中的 XIMAGECODEC_BMP_ON /
 *              XIMAGECODEC_PNG_ON / XIMAGECODEC_JPEG_ON / XIMAGECODEC_GIF_ON /
 *              XIMAGECODEC_SVG_ON 独立裁剪，XIMAGECODEC_ON 为模块总开关。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XIMAGECODEC_H
#define XIMAGECODEC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "XString.h"
#include "XByteArray.h"
#include "XImageCodec_config.h"
#include "XImage.h"

#if XIMAGECODEC_ON

typedef struct XImage XImage;

/** @brief 图像编解码格式。 */
typedef enum XImageCodecFormat
{
    XImageCodecFormat_Unknown = 0, /**< 未知/无效格式。 */
    XImageCodecFormat_Bmp,         /**< Windows BMP（24/32 位无压缩，含 Alpha）。 */
    XImageCodecFormat_Png,         /**< PNG（8 位灰度/灰度+Alpha/RGB/RGBA）。 */
    XImageCodecFormat_Jpeg,        /**< JPEG（基线+渐进/算术/12 位/CMYK 解码，YCbCr 4:2:0 编码）。 */
    XImageCodecFormat_Gif,         /**< GIF（GIF87a/GIF89a 静态首帧；XIMAGECODEC_GIF_ANIM_ON 开启时含多帧动画）。 */
    XImageCodecFormat_Svg          /**< SVG（内嵌 PNG、纯色矩形；XIMAGECODEC_SVG_VECTOR_ON 开启时含矢量渲染）。 */
} XImageCodecFormat;

/**
 * @brief 从 XString 格式名解析编解码格式。
 * @param format 格式名称（不区分大小写，如 "png"、"JPEG"）；NULL 或空字符串返回 Unknown。
 * @return 解析出的格式枚举。
 */
XImageCodecFormat XImageCodec_formatFromName(const XString* format);

/**
 * @brief 使用 UTF-8 格式名解析编解码格式的兼容重载。
 * @param format UTF-8 格式名称（不区分大小写）；NULL 或空字符串返回 Unknown。
 * @return 解析出的格式枚举。
 */
XImageCodecFormat XImageCodec_formatFromName_2(const char* format);

/**
 * @brief 根据文件头自动识别格式。
 * @param data 编码数据；不能为 NULL。
 * @param size 数据字节数。
 * @return 识别出的格式，失败返回 Unknown。
 */
XImageCodecFormat XImageCodec_detect(const uint8_t* data, size_t size);

/**
 * @brief 查询某格式是否有可用解码算法。
 * @param format 待查询格式。
 * @return 有可用的解码算法返回 true，否则返回 false。
 */
bool XImageCodec_canDecode(XImageCodecFormat format);

/**
 * @brief 查询某格式是否有可用编码算法。
 * @param format 待查询格式。
 * @return 有可用的编码算法返回 true，否则返回 false。
 */
bool XImageCodec_canEncode(XImageCodecFormat format);

/**
 * @brief 从内存编码数据中探测图像宽度与高度。
 * @param data   输入图像数据；不能为 NULL。
 * @param size   输入数据字节数。
 * @param width  输出图像宽度；成功后填写大于 0 的值。
 * @param height 输出图像高度；成功后填写大于 0 的值。
 * @param format 指定格式；Unknown 时根据文件头自动识别。
 * @return 成功探测到尺寸返回 true，数据无效或不支持的格式返回 false。
 * @note 仅读取格式文件头或足够的前段数据，不解码完整像素数据。
 */
bool XImageCodec_probeSize(const uint8_t* data, size_t size,
                           XImageCodecFormat format,
                           int* width, int* height);

/**
 * @brief 获取格式的标准小写名称。
 * @param format 待查询格式。
 * @return 新建的 XString 名称（调用者负责释放）；未知格式返回空字符串。
 */
XString* XImageCodec_formatName(XImageCodecFormat format);

/**
 * @brief 获取格式的 UTF-8 兼容名称。
 * @param format 待查询格式。
 * @return 内部静态 UTF-8 名称；未知格式返回 NULL。
 */
const char* XImageCodec_formatName_2(XImageCodecFormat format);

/**
 * @brief 将内存中的图像编码数据解码到 XImage。
 * @param data   输入编码数据；不能为 NULL。
 * @param size   输入数据字节数。
 * @param format 指定格式；Unknown 时根据文件头自动识别。
 * @param out    输出图像对象；成功后 out 包含解码结果，调用者负责释放。
 * @return 成功返回 true（解码结果覆盖 out 原有内容）。
 */
bool XImageCodec_decode(const uint8_t* data, size_t size,
                        XImageCodecFormat format, XImage* out);

/**
 * @brief 将 XImage 编码为指定格式的内存数据。
 * @param image   输入图像；不能为 NULL。
 * @param format  编码格式；不支持的格式返回 false。
 * @param quality 质量参数（1..100，<=0 取默认 75）；仅 JPEG 编码使用，
 *                其它格式忽略该参数，无损格式传 -1 即可。
 * @param out     输出字节数组；成功后 out 包含编码数据，调用者负责释放。
 * @return 成功返回 true（编码结果覆盖 out 原有内容）。
 */
bool XImageCodec_encode(const XImage* image, XImageCodecFormat format,
                        int quality, XByteArray* out);

/* ====================================================================== */
/* 动画 API（GIF 多帧；受 XIMAGECODEC_GIF_ON 与 XIMAGECODEC_GIF_ANIM_ON）  */
/* ====================================================================== */
#if XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON

/** @brief GIF 帧处置方式（Graphic Control Extension Disposal Method）。 */
typedef enum XImageCodecFrameDisposal
{
    XImageCodecFrameDisposal_None = 0,           /**< 未指定/忽略。 */
    XImageCodecFrameDisposal_Keep = 1,           /**< 保留上一帧（默认）。 */
    XImageCodecFrameDisposal_RestoreBackground = 2, /**< 恢复为背景（透明）。 */
    XImageCodecFrameDisposal_RestorePrevious = 3    /**< 恢复为上一帧画布。 */
} XImageCodecFrameDisposal;

/** @brief GIF 动画单帧：完整画布快照与帧元信息。 */
typedef struct XImageCodecFrame
{
    XImage image;                     /**< 帧画布（逻辑屏幕完整快照，ARGB32）。 */
    int delayMs;                      /**< 显示延迟毫秒数（GCE 延迟 x10）。 */
    XImageCodecFrameDisposal disposal;/**< 本帧绘制后的处置方式。 */
    int left;                         /**< 本帧图像块原始左坐标。 */
    int top;                          /**< 本帧图像块原始上坐标。 */
    int width;                        /**< 本帧图像块原始宽度。 */
    int height;                       /**< 本帧图像块原始高度。 */
} XImageCodecFrame;

/** @brief GIF 多帧动画解码结果。 */
typedef struct XImageCodecAnimation
{
    int frameCount;   /**< 帧数量（>0）。 */
    int loopCount;    /**< 循环次数：-1=无限循环，0=播放一次，>0=指定次数。 */
    XImageCodecFrame* frames; /**< 帧数组（frameCount 个），由 XImageCodecAnimation_delete 统一释放。 */
} XImageCodecAnimation;

/**
 * @brief 解码 GIF 多帧动画。
 * @param data 输入 GIF 数据；不能为 NULL。
 * @param size 数据字节数。
 * @return 动画结果；非 GIF 数据、无任何帧或参数无效返回 NULL。
 *         调用者使用 XImageCodecAnimation_delete 释放。
 */
XImageCodecAnimation* XImageCodec_decodeAnimation(const uint8_t* data, size_t size);

/**
 * @brief 释放动画解码结果。
 * @param animation 动画对象；NULL 安全。
 */
void XImageCodecAnimation_delete(XImageCodecAnimation* animation);

#endif /* XIMAGECODEC_GIF_ON && XIMAGECODEC_GIF_ANIM_ON */

#ifdef __cplusplus
}
#endif

#endif /* XIMAGECODEC_ON */

#endif
