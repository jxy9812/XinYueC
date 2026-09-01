/** @file       XImageCodecInternal.h
 * @brief      XImageCodec 内部共享工具与各格式编解码入口（不对外公开）。
 * @note       仅供 XImageCodec.c 及各图像格式实现文件包含。
 *              公共编解码 API 见 XImageCodec.h，上层图像类只应使用
 *              XImageCodec_* 公共接口，不应包含本头文件。
 *              本文件受 XImageCodec_config.h 的 XIMAGECODEC_ON 总开关与
 *              XIMAGECODEC_BMP_ON / PNG_ON / JPEG_ON / GIF_ON / PPM_ON /
 *              XBM_ON / SVG_ON / ICO_ON
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

/*
 * ICC/LUT 资源设置规格。该结构仅供图像内部和编解码器使用，不属于
 * XColorSpace 公共值类型，调用者提供的指针只在设置函数执行期间借用，
 * 函数成功后图像会复制全部数据。LUT 通道顺序为红、绿、蓝，16 位
 * 元素按当前主机字节序保存；m_lutTwoWay 表示该表允许逆向查找。
 */
typedef struct XImageColorProfileSpec
{
    const uint8_t* m_iccData;       /**< ICC 原始字节，可为空。 */
    size_t m_iccSize;               /**< ICC 字节数，零表示未提供。 */
    const void* m_lutData[3];       /**< 每个通道的 LUT 数据，可为空。 */
    uint32_t m_lutElements[3];      /**< 对应通道元素数，至少为 2。 */
    uint8_t m_lutBits[3];           /**< 元素位宽，仅支持 8 或 16。 */
    bool m_lutTwoWay[3];            /**< 是否允许逆向查找。 */
} XImageColorProfileSpec;

#if XIMAGECODEC_ON

/**
 * @brief 为图像设置 ICC/LUT 内部资源。
 * @param self 目标图像；不能为空。
 * @param spec 资源规格；传入 NULL 或空规格表示清除当前资源。
 * @return 成功返回 true；参数非法或内存不足返回 false，失败时图像不变。
 */
bool XImageCodecInternal_setColorProfile(XImage* self,
                                         const XImageColorProfileSpec* spec);

/**
 * @brief 复制图像保存的 ICC 原始字节。
 * @param self 源图像；不能为空。
 * @param out 输出字节数组；不能为空，已有内容会被替换。
 * @return 成功返回 true；无 ICC 时输出为空并返回 true。
 */
bool XImageCodecInternal_copyIccProfile(const XImage* self, XByteArray* out);

/**
 * @brief 复制图像指定通道的 LUT 原始字节。
 * @param self 源图像；不能为空。
 * @param channel 通道索引，0/1/2 分别表示红/绿/蓝。
 * @param out 输出缓冲区；无 LUT 时可为空，有 LUT 时容量必须足够。
 * @param outBytes 输出缓冲区容量（字节）。
 * @param elements 返回元素个数，可为空。
 * @param bits 返回元素位宽，可为空。
 * @param twoWay 返回是否支持逆向查找，可为空。
 * @return 成功返回 true；通道非法、缓冲区不足或参数无效返回 false。
 */
bool XImageCodecInternal_copyLut(const XImage* self, int channel,
                                 void* out, size_t outBytes,
                                 uint32_t* elements, uint8_t* bits,
                                 bool* twoWay);

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

#if XIMAGECODEC_PPM_ON
/**
 * @brief 解码 PPM/PBM/PGM（P1-P6）数据到 XImage。
 * @param data 输入 PPM 家族数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out 输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true；头部、样本数据或尺寸无效时返回 false。
 */
bool XImageCodecInternal_decodePpm(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 将 XImage 编码为 PPM P6（二进制 RGB）数据。
 * @param image 输入图像；不能为 NULL。
 * @param out 输出字节数组；不能为 NULL。
 * @return 成功返回 true；图像为空或扩容失败时返回 false。
 */
bool XImageCodecInternal_encodePpm(const XImage* image, XByteArray* out);

/**
 * @brief 按 PPM 处理器子类型写出便携位图、灰度图或彩色图。
 * @param image 输入图像；不能为 NULL。
 * @param subtype 子类型名称（pbm/pbmraw、pgm/pgmraw 或 ppm/ppmraw）。
 * @param out 输出字节数组；不能为 NULL。
 * @return 成功返回 true；子类型、图像尺寸或输出扩容失败时返回 false。
 * @note 与 Qt QPpmHandler::write() 一致，raw 后缀只影响识别键，实际输出
 *       由图像类别选择 P4/P5/P6 二进制变体。
 */
bool XImageCodecInternal_encodePpmSubtype(const XImage* image,
                                          const char* subtype,
                                          XByteArray* out);

/**
 * @brief 从 PPM/PBM/PGM 头部探测图像尺寸。
 * @param data 输入数据；不能为 NULL。
 * @param size 输入字节数。
 * @param width 输出宽度。
 * @param height 输出高度。
 * @return 头部合法且尺寸可表示时返回 true。
 */
bool XImageCodecInternal_probePpmSize(const uint8_t* data, size_t size,
                                      int* width, int* height);
#endif /* XIMAGECODEC_PPM_ON */

#if XIMAGECODEC_XBM_ON
/**
 * @brief 解码 XBM 数据到 XImage。
 * @param data 输入 XBM 文本数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out 输出图像对象；成功后为 MonoLSB，调用者负责释放。
 * @return 成功返回 true；头部、尺寸或十六进制数据无效返回 false。
 * @note 与 Qt QXbmHandler::read_xbm_body() 一致，数据不足时保留已写入
 *       字节并让未覆盖区域保持白色，只要头部之后出现过 0x 标记即可成功。
 */
bool XImageCodecInternal_decodeXbm(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 将 XImage 编码为 XBM 数据并使用给定的 C 标识符作为数组名称。
 * @param image 输入图像；不能为 NULL。
 * @param name  文件基名或 C 标识符；NULL/空字符串使用 image。
 * @param out   输出字节数组；不能为 NULL。
 * @return 成功返回 true；图像为空、尺寸无效或扩容失败返回 false。
 */
bool XImageCodecInternal_encodeXbmNamed(const XImage* image, const char* name,
                                        XByteArray* out);

/**
 * @brief 从 XBM 头部探测图像尺寸。
 * @param data 输入 XBM 文本数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param width 输出宽度。
 * @param height 输出高度。
 * @return 头部合法且尺寸在 Qt 允许范围内返回 true。
 */
bool XImageCodecInternal_probeXbmSize(const uint8_t* data, size_t size,
                                      int* width, int* height);
#endif /* XIMAGECODEC_XBM_ON */

#if XIMAGECODEC_XPM_ON
/**
 * @brief 解码 XPM 数据到 XImage。
 * @param data 输入 XPM 文本；不能为 NULL。
 * @param size 输入字节数。
 * @param out 输出图像对象；成功后由调用者负责释放。
 * @return 成功返回 true；魔数、头部、调色板或像素串非法返回 false。
 */
bool XImageCodecInternal_decodeXpm(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 从 XPM 头部探测图像尺寸。
 * @param data 输入 XPM 文本；不能为 NULL。
 * @param size 输入字节数。
 * @param width 输出图像宽度。
 * @param height 输出图像高度。
 * @return 头部合法且尺寸在 Qt 允许范围内返回 true。
 */
bool XImageCodecInternal_probeXpmSize(const uint8_t* data, size_t size,
                                      int* width, int* height);

/**
 * @brief 探测 XPM 尺寸及 Qt 处理器可报告的像素格式。
 * @param data 输入 XPM 文本；不能为 NULL。
 * @param size 输入数据字节数。
 * @param width 输出图像宽度。
 * @param height 输出图像高度。
 * @param imageFormat 输出像素格式；颜色数不超过 256 时为 Indexed8，
 *                    超过 256 时按 Qt QXpmHandler 语义返回 Invalid。
 * @return 头部合法并成功填写结果返回 true；否则返回 false。
 */
bool XImageCodecInternal_probeXpmImageFormat(const uint8_t* data, size_t size,
                                             int* width, int* height,
                                             XImageFormat* imageFormat);

/**
 * @brief 将 XImage 编码为 XPM C 字符串数组。
 * @param image 输入图像；不能为 NULL。
 * @param name 文件名或 C 标识符；NULL/空字符串使用 image。
 * @param out 输出字节数组；不能为 NULL。
 * @return 成功返回 true；图像为空、颜色数超过 64^4 或扩容失败返回 false。
 */
bool XImageCodecInternal_encodeXpmNamed(const XImage* image, const char* name,
                                        XByteArray* out);
#endif /* XIMAGECODEC_XPM_ON */

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
 * @brief 从 JPEG APP1 Exif 段探测图像方向。
 * @param data 输入 JPEG 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param transformation 输出 Qt 风格变换枚举值，0..7 对应 None..Rotate270。
 * @return 成功扫描 JPEG 头返回 true；数据截断或标记段非法返回 false。
 * @note 仅读取 Exif Orientation，不解码像素；未提供方向时输出 None。
 */
bool XImageCodecInternal_probeJpegTransformation(const uint8_t* data,
                                                 size_t size,
                                                 int* transformation);

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

#if XIMAGECODEC_ICO_ON
/**
 * @brief 解码 ICO/CUR 首个目录条目到 XImage。
 * @param data 输入 ICO/CUR 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param out 输出图像对象；成功后由调用者负责释放。
 * @return 成功返回 true；目录、嵌入 PNG 或 1/4/8/24/32 位 DIB 非法时返回 false。
 * @note 门面 API 不暴露多条目枚举，因此按 Qt 图标处理器的默认行为只解码首个条目。
 */
bool XImageCodecInternal_decodeIco(const uint8_t* data, size_t size, XImage* out);

/**
 * @brief 从 ICO/CUR 首个目录条目探测图像尺寸。
 * @param data 输入 ICO/CUR 数据；不能为 NULL。
 * @param size 输入数据字节数。
 * @param width 输出宽度。
 * @param height 输出高度。
 * @return 目录合法且尺寸在 ICO 允许范围内返回 true，否则返回 false。
 */
bool XImageCodecInternal_probeIcoSize(const uint8_t* data, size_t size,
                                      int* width, int* height);

/**
 * @brief 将单幅 XImage 编码为一个 32 位 ICO 条目。
 * @param image 输入图像；不能为 NULL。
 * @param out 输出字节数组；成功后包含完整 ICO 数据。
 * @return 成功返回 true；图像为空、尺寸无效或扩容失败返回 false。
 * @note 大于 256 像素的图像按 Qt KeepAspectRatio/SmoothTransformation 语义缩放到 ICO 上限。
 */
bool XImageCodecInternal_encodeIco(const XImage* image, XByteArray* out);
#endif /* XIMAGECODEC_ICO_ON */

#endif /* XIMAGECODEC_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIMAGECODEC_INTERNAL_H */
