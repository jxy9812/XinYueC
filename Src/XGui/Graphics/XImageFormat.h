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
 * @note       格式名称和映射语义与 Qt 6.8 对齐；XPixelFormat 辅助枚举属于
 *             XinYueC 兼容层，调用方应通过转换函数使用，不依赖其底层数值。
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

/** @brief 像素格式的通道模型（对标 Qt QPixelFormat::ColorModel）。 */
typedef enum XPixelFormatModel
{
    XPixelFormatModel_Invalid = 0, /**< 无效模型。 */
    XPixelFormatModel_RGB,          /**< 红绿蓝通道模型。 */
    XPixelFormatModel_BGR,          /**< 蓝绿红通道模型。 */
    XPixelFormatModel_Indexed,      /**< 颜色索引模型；Mono 也使用此模型。 */
    XPixelFormatModel_Gray,         /**< 灰度通道模型。 */
    XPixelFormatModel_CMYK,         /**< 青品红黄黑通道模型。 */
    XPixelFormatModel_HSL,          /**< HSL 通道模型。 */
    XPixelFormatModel_HSV,          /**< HSV 通道模型。 */
    XPixelFormatModel_YUV,          /**< YUV 通道模型。 */
    XPixelFormatModel_Alpha,        /**< 仅 Alpha 通道模型。 */
    /* 保留旧接口名称，并提供 Qt 使用的灰度命名。 */
    XPixelFormatModel_Mono = XPixelFormatModel_Indexed, /**< 旧版单色模型别名，现按索引模型处理。 */
    XPixelFormatModel_Grayscale = XPixelFormatModel_Gray /**< Qt 灰度模型名称别名。 */
} XPixelFormatModel;

/** @brief Alpha 通道是否参与颜色表示（对标 Qt QPixelFormat::AlphaUsage）。 */
typedef enum XPixelFormatAlphaUsage
{
    XPixelFormatAlpha_Ignores = 0, /**< 存在填充 Alpha 位但读取时忽略。 */
    XPixelFormatAlpha_Uses = 1,    /**< Alpha 位参与颜色表示。 */
    XPixelFormatAlpha_IgnoresAlpha = XPixelFormatAlpha_Ignores, /**< Qt 忽略 Alpha 的名称别名。 */
    XPixelFormatAlpha_UsesAlpha = XPixelFormatAlpha_Uses /**< Qt 使用 Alpha 的名称别名。 */
} XPixelFormatAlphaUsage;

/** @brief Alpha 通道在像素中的逻辑位置（对标 Qt QPixelFormat::AlphaPosition）。 */
typedef enum XPixelFormatAlphaPosition
{
    XPixelFormatAlpha_AtBeginning = 0, /**< Alpha 位位于通道序列前端。 */
    XPixelFormatAlpha_AtEnd = 1        /**< Alpha 位位于通道序列末端。 */
} XPixelFormatAlphaPosition;

/** @brief Alpha 是否为预乘形式（对标 Qt QPixelFormat::AlphaPremultiplied）。 */
typedef enum XPixelFormatAlphaPremultiplied
{
    XPixelFormatAlpha_NotPremultiplied = 0, /**< 未预乘 Alpha。 */
    XPixelFormatAlpha_Premultiplied = 1     /**< 已预乘 Alpha。 */
} XPixelFormatAlphaPremultiplied;

/** @brief 通道数值的数据类型解释（对标 Qt QPixelFormat::TypeInterpretation）。 */
typedef enum XPixelFormatTypeInterpretation
{
    XPixelFormatType_UnsignedInteger = 0, /**< 无符号整数。 */
    XPixelFormatType_UnsignedShort = 1,   /**< 无符号短整数。 */
    XPixelFormatType_UnsignedByte = 2,    /**< 无符号字节。 */
    XPixelFormatType_FloatingPoint = 3   /**< 浮点数。 */
} XPixelFormatTypeInterpretation;

/** @brief 通道数据的字节序（对标 Qt QPixelFormat::ByteOrder）。 */
typedef enum XPixelFormatByteOrder
{
    XPixelFormatByteOrder_LittleEndian = 0, /**< 小端序。 */
    XPixelFormatByteOrder_BigEndian = 1,    /**< 大端序。 */
    XPixelFormatByteOrder_CurrentSystemEndian = 2 /**< 本机字节序。 */
} XPixelFormatByteOrder;

/** @brief 图像像素布局描述（对标 Qt QPixelFormat）。 */
typedef struct XPixelFormat
{
    XPixelFormatModel m_model; /**< 颜色通道模型。 */
    uint8_t m_redSize; /**< 红色通道位数。 */
    uint8_t m_greenSize; /**< 绿色通道位数。 */
    uint8_t m_blueSize; /**< 蓝色通道位数。 */
    uint8_t m_alphaSize; /**< Alpha 通道位数。 */
    uint8_t m_cyanSize; /**< 青色通道位数。 */
    uint8_t m_magentaSize; /**< 品红通道位数。 */
    uint8_t m_yellowSize; /**< 黄色通道位数。 */
    uint8_t m_blackSize; /**< 黑色通道位数。 */
    uint8_t m_fourthSize; /**< 第四个通道位数；CMYK 时与青色到黑色字段对应。 */
    uint8_t m_fifthSize; /**< 第五个通道位数；QPixelFormat 保留通道。 */
    uint8_t m_channelCount; /**< 通道总数。 */
    bool m_premultiplied; /**< 是否使用预乘 Alpha。 */
    bool m_byteOrdered; /**< 旧版字节排列标志，保留供兼容代码读取。 */
    XPixelFormatAlphaUsage m_alphaUsage; /**< Alpha 是否参与颜色表示。 */
    XPixelFormatAlphaPosition m_alphaPosition; /**< Alpha 的逻辑位置。 */
    XPixelFormatTypeInterpretation m_typeInterpretation; /**< 通道数据类型解释。 */
    XPixelFormatByteOrder m_byteOrder; /**< 通道数据字节序。 */
    uint8_t m_subEnum; /**< YUV 等扩展模型的子枚举值。 */
} XPixelFormat;

/**
 * @brief 比较两个完整像素格式描述是否相同。
 * @param left 左侧像素格式描述。
 * @param right 右侧像素格式描述。
 * @return 所有模型、通道和存储语义均相同返回 true，否则返回 false。
 */
bool XPixelFormat_equals(const XPixelFormat* left, const XPixelFormat* right);

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

/**
 * @brief 获取指定图像格式的通道布局描述。
 * @param format 图像格式枚举值。
 * @return 像素通道布局描述；未知格式返回无效布局。
 */
XPixelFormat XImageFormat_pixelFormat(XImageFormat format);

/**
 * @brief 将图像格式转换为完整像素格式描述（对标 QImage::toPixelFormat）。
 * @param format 图像格式枚举值；无效值返回空像素格式。
 * @return 与图像格式对应的像素布局和存储语义。
 */
XPixelFormat XImageFormat_toPixelFormat(XImageFormat format);

/**
 * @brief 将完整像素格式描述转换回图像格式（对标 QImage::toImageFormat）。
 * @param format 像素格式描述。
 * @return 精确匹配的图像格式；没有匹配项返回 Invalid。
 */
XImageFormat XImageFormat_toImageFormat(XPixelFormat format);

#ifdef __cplusplus
}
#endif
#endif /* XIMAGEFORMAT_H */
