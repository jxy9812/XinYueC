/******************************************************************************
 * @file       XImageFormat.h
 * @brief      XImage 格式枚举定义（对标 Qt 6.8 QImage::Format）
 * @author     XinYueC 团队
 * @note       本文件定义 XImage 支持的所有像素格式枚举，供 XImage、XImageIOHandler 等类使用
 ******************************************************************************/
#ifndef XIMAGEFORMAT_H
#define XIMAGEFORMAT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief      XImage 像素格式枚举（对标 Qt 6.8 QImage::Format）
 * @note       枚举值保持与 Qt 6.8 完全一致，便于格式转换和兼容
 */
typedef enum XImageFormat
{
    XImageFormat_Invalid,                     /**< 无效格式 */
    XImageFormat_Mono,                        /**< 单色（1 位，每字节 8 像素，MSB 优先） */
    XImageFormat_MonoLSB,                     /**< 单色（1 位，每字节 8 像素，LSB 优先） */
    XImageFormat_Indexed8,                    /**< 索引 8 位（使用颜色表） */
    XImageFormat_RGB32,                       /**< 32 位 RGB（0x00RRGGBB） */
    XImageFormat_ARGB32,                      /**< 32 位 ARGB（0xAARRGGBB） */
    XImageFormat_ARGB32_Premultiplied,        /**< 32 位预乘 Alpha ARGB */
    XImageFormat_RGB16,                       /**< 16 位 RGB（5-6-5） */
    XImageFormat_ARGB8565_Premultiplied,      /**< 16 位 RGB + 8 位预乘 Alpha */
    XImageFormat_RGB666,                      /**< 18 位 RGB（6-6-6） */
    XImageFormat_ARGB6666_Premultiplied,      /**< 24 位预乘 Alpha RGB（6-6-6-6） */
    XImageFormat_RGB555,                      /**< 16 位 RGB（5-5-5，高位未使用） */
    XImageFormat_ARGB8555_Premultiplied,      /**< 24 位预乘 Alpha RGB（8-5-5-5） */
    XImageFormat_RGB888,                      /**< 24 位 RGB（8-8-8） */
    XImageFormat_RGB444,                      /**< 12 位 RGB（4-4-4） */
    XImageFormat_ARGB4444_Premultiplied,      /**< 16 位预乘 Alpha ARGB（4-4-4-4） */
    XImageFormat_RGBX8888,                    /**< 32 位 RGB（8-8-8-8，Alpha 未使用） */
    XImageFormat_RGBA8888,                    /**< 32 位 RGBA（8-8-8-8） */
    XImageFormat_RGBA8888_Premultiplied,      /**< 32 位预乘 Alpha RGBA（8-8-8-8） */
    XImageFormat_BGR30,                       /**< 32 位 BGR（2 位未使用，10-10-10） */
    XImageFormat_A2BGR30_Premultiplied,       /**< 32 位预乘 Alpha BGR（2-10-10-10） */
    XImageFormat_RGB30,                       /**< 32 位 RGB（2 位未使用，10-10-10） */
    XImageFormat_A2RGB30_Premultiplied,       /**< 32 位预乘 Alpha RGB（2-10-10-10） */
    XImageFormat_Alpha8,                      /**< 8 位 Alpha（灰度） */
    XImageFormat_Grayscale8,                  /**< 8 位灰度 */
    XImageFormat_RGBX64,                      /**< 64 位 RGB（16-16-16，Alpha 未使用） */
    XImageFormat_RGBA64,                      /**< 64 位 RGBA（16-16-16-16） */
    XImageFormat_RGBA64_Premultiplied,        /**< 64 位预乘 Alpha RGBA（16-16-16-16） */
    XImageFormat_Grayscale16,                 /**< 16 位灰度 */
    XImageFormat_BGR888,                      /**< 24 位 BGR（8-8-8） */
    XImageFormat_RGBX16FPx4,                  /**< 64 位半精度浮点 RGB（16-16-16-16，Alpha 未使用） */
    XImageFormat_RGBA16FPx4,                  /**< 64 位半精度浮点 RGBA（16-16-16-16） */
    XImageFormat_RGBA16FPx4_Premultiplied,    /**< 64 位预乘半精度浮点 RGBA */
    XImageFormat_RGBX32FPx4,                  /**< 128 位单精度浮点 RGB（32-32-32-32，Alpha 未使用） */
    XImageFormat_RGBA32FPx4,                  /**< 128 位单精度浮点 RGBA（32-32-32-32） */
    XImageFormat_RGBA32FPx4_Premultiplied,    /**< 128 位预乘单精度浮点 RGBA */
    XImageFormat_CMYK8888,                    /**< 32 位 CMYK（8-8-8-8） */
    XImageFormat_NImageFormats                /**< 格式计数（内部使用） */
} XImageFormat;

/**
 * @brief      XImage 反色模式枚举（对标 Qt 6.8 QImage::InvertMode）
 */
typedef enum XImageInvertMode
{
    XImageInvertMode_InvertRgb,   /**< 反色 RGB 分量，保持 Alpha 不变 */
    XImageInvertMode_InvertRgba   /**< 反色所有分量（包括 Alpha） */
} XImageInvertMode;

/**
 * @brief      获取像素格式的位深度
 * @param format 像素格式
 * @return 每个像素的位数，无效格式返回 0
 */
int XImageFormat_bitDepth(XImageFormat format);

/**
 * @brief      判断像素格式是否包含 Alpha 通道
 * @param format 像素格式
 * @return 包含 Alpha 通道返回 true，否则返回 false
 */
bool XImageFormat_hasAlpha(XImageFormat format);

/**
 * @brief      判断像素格式是否为预乘 Alpha 格式
 * @param format 像素格式
 * @return 预乘 Alpha 格式返回 true，否则返回 false
 */
bool XImageFormat_isPremultiplied(XImageFormat format);

/**
 * @brief      获取像素格式每行像素的最小字节对齐数
 * @param format 像素格式
 * @return 字节对齐数（通常为 4）
 */
int XImageFormat_bytesPerLineAlignment(XImageFormat format);

/**
 * @brief      计算给定宽度和格式下每行需要的字节数
 * @param width  图像宽度（像素）
 * @param format 像素格式
 * @return 每行字节数
 */
int XImageFormat_bytesPerLine(int width, XImageFormat format);

#ifdef __cplusplus
}
#endif
#endif /* XIMAGEFORMAT_H */
