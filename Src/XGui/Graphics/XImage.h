/******************************************************************************
 * @file       XImage.h
 * @brief      XImage 图像类（对标 Qt 6.8 QImage）
 * @author     XinYueC 团队
 * @note       提供像素级图像数据访问，支持多种像素格式、读写像素、格式转换、缩放等操作
 ******************************************************************************/
#ifndef XIMAGE_H
#define XIMAGE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XImageFormat.h"
#include "XColorSpace.h"
#include "XGeometry.h"
#include "XColor.h"
#include "XString.h"
#include "XClass.h"
#include "XTypes.h"
#include "XMemory.h"
#include "XStringList.h"


/* ========== XImage 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XImage)
XCLASS_DEFINE_EXTEND_END(XImage, XClass)
/* 前向声明 */
typedef struct XImageData XImageData;
typedef struct XIODevice XIODevice;

/**
 * @brief      XImage 二维齐次变换矩阵（对标 Qt 6.8 QTransform）。
 * @note       坐标变换为：
 *       x'=(m11*x+m21*y+dx)/(m13*x+m23*y+m33)，
 *       y'=(m12*x+m22*y+dy)/(m13*x+m23*y+m33)。
 *       m13/m23/m33 追加在旧六参数结构之后；旧代码只初始化六个字段时，
 *       会按 m13=0、m23=0、m33=1 的仿射矩阵处理。
 */
typedef struct XImageTransform
{
    float m11;
    float m12;
    float m21;
    float m22;
    float dx;
    float dy;
    float m13; /**< X 透视系数；仿射变换为 0。 */
    float m23; /**< Y 透视系数；仿射变换为 0。 */
    float m33; /**< 齐次缩放系数；旧六参数矩阵默认按 1 处理。 */
} XImageTransform;

/** @brief XImage 掩码颜色匹配模式。 */
typedef enum XImageMaskMode
{
    XImageMask_InColor = 0,
    XImageMask_OutColor = 1
} XImageMaskMode;

/**
 * @brief      XImage 图像类结构体（对标 Qt 6.8 QImage）
 * @note       继承自 XObject，提供像素级图像数据访问和处理接口
 */
typedef struct XImage
{
    XClass    m_class;       /**< 继承的基类成员 */
    XImageData* m_data;       /**< 图像数据私有指针（内部引用计数管理） */
}XImage;

/** @brief XVariant 前向声明，用于 XImage 的值类型适配接口。 */
typedef struct XVariant XVariant;

/**
 * @brief      初始化 XImage 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XImage_class_init();

/**
 * @brief      在堆上创建 XImage 实例
 * @return     指向新创建的 XImage 对象的指针，失败返回 NULL
 */
XImage* XImage_create_ex(XMemoryType memory);

/**
 * @brief      初始化 XImage 实例（创建空图像）
 * @param self 待初始化的 XImage 对象指针
 */
void XImage_init(XImage* self);

/**
 * @brief      使用指定大小和格式创建初始化的 XImage
 * @param self   待初始化的 XImage 对象指针
 * @param width  图像宽度（像素）
 * @param height 图像高度（像素）
 * @param format 像素格式
 */
void XImage_init_ex(XImage* self, int width, int height, XImageFormat format);

/**
 * @brief      使用指定大小和格式创建初始化的 XImage
 * @param self   待初始化的 XImage 对象指针
 * @param width  图像宽度（像素）
 * @param height 图像高度（像素）
 * @param format 像素格式
 * @param bytesPerLine 每行字节数（0 表示自动计算）
 * @param data   像素数据指针（NULL 表示内部分配）
 * @param cleanupFunction 清理回调函数（可为 NULL）
 * @param cleanupInfo 清理回调参数（可为 NULL）
 */
void XImage_init_ex_2(XImage* self, int width, int height, XImageFormat format,
                      int64_t bytesPerLine, uint8_t* data,
                      void (*cleanupFunction)(void*), void* cleanupInfo);

/**
 * @brief      从文件加载图像并初始化
 * @param self    待初始化的 XImage 对象指针
 * @param fileName 文件名
 * @param format  图像格式字符串（如 "PNG"、"JPEG"，NULL 表示自动检测）
 */
void XImage_init_file(XImage* self, const XString* fileName, const XString* format);
/**
 * @brief 使用 UTF-8 文件名和格式初始化图像的兼容重载。
 * @param self 待初始化的 XImage 对象指针。
 * @param fileName UTF-8 编码的文件名；可为 NULL。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 */
void XImage_init_file_2(XImage* self, const char* fileName, const char* format);

/**
 * @brief      复制构造函数
 * @param self 目标 XImage 对象指针
 * @param other 源 XImage 对象指针
 */

/**
 * @brief      移动构造函数
 * @param self 目标 XImage 对象指针
 * @param other 源 XImage 对象指针（移动后源对象变为空）
 */

/**
 * @brief      释放 XImage 资源
 * @param self 待释放的 XImage 对象指针
 */
/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
/**
 * @brief 通过 XClass 虚表复制图像。
 * @param self 目标图像对象指针。
 * @param other 源图像对象指针。
 */
#define XImage_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/**
 * @brief 通过 XClass 虚表移动图像。
 * @param self 目标图像对象指针。
 * @param other 源图像对象指针；移动后源对象为空。
 */
#define XImage_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放图像资源。 @param self 待释放的图像对象指针。 */
#define XImage_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
/** @brief 删除堆上的图像对象。 @param self 待删除的图像对象指针。 */
#define XImage_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 查询方法 ========== */

/**
 * @brief      判断图像是否为 null（无数据）
 * @param self 目标 XImage 对象指针
 * @return 图像为 null 返回 true，否则返回 false
 */
bool XImage_isNull(const XImage* self);

/**
 * @brief      获取图像宽度
 * @param self 目标 XImage 对象指针
 * @return 图像宽度（像素）
 */
int XImage_width(const XImage* self);

/**
 * @brief      获取图像高度
 * @param self 目标 XImage 对象指针
 * @return 图像高度（像素）
 */
int XImage_height(const XImage* self);

/**
 * @brief      获取图像尺寸
 * @param self 目标 XImage 对象指针
 * @param out  输出尺寸结构体指针
 */
void XImage_size(const XImage* self, XSize* out);

/**
 * @brief      获取图像矩形区域
 * @param self 目标 XImage 对象指针
 * @param out  输出矩形结构体指针
 */
void XImage_rect(const XImage* self, XRect* out);

/**
 * @brief      获取图像位深度
 * @param self 目标 XImage 对象指针
 * @return 每个像素的位数
 */
int XImage_depth(const XImage* self);

/** @brief 获取实际使用的位平面数（RGB32 等格式不计未使用的填充位）。 */
int XImage_bitPlaneCount(const XImage* self);

/**
 * @brief      获取图像格式
 * @param self 目标 XImage 对象指针
 * @return 像素格式枚举值
 */
XImageFormat XImage_format(const XImage* self);

/** @brief 获取当前图像像素布局描述（对标 QImage::pixelFormat）。 */
XPixelFormat XImage_pixelFormat(const XImage* self);

/**
 * @brief 获取图像色彩空间（对标 QImage::colorSpace）。
 * @param self 源图像对象指针。
 * @return 图像当前色彩空间值；未设置时返回无效色彩空间。
 */
XColorSpace XImage_colorSpace(const XImage* self);
/**
 * @brief 设置图像色彩空间（对标 QImage::setColorSpace）。
 * @param self 目标图像对象指针。
 * @param colorSpace 要写入的色彩空间值。
 */
void XImage_setColorSpace(XImage* self, XColorSpace colorSpace);

/**
 * @brief 判断图像是否带有有效色彩空间。
 * @param self 待检查的图像对象指针。
 * @return 已设置有效色彩空间返回 true，否则返回 false。
 */
bool XImage_hasColorSpace(const XImage* self);

/**
 * @brief 将图像转换到目标色彩空间并返回新图像。
 * @param self 源图像对象指针。
 * @param colorSpace 目标色彩空间。
 * @param flags 转换标志。
 * @param out 输出图像对象指针，旧内容会被释放。
 */
void XImage_convertedToColorSpace(const XImage* self, XColorSpace colorSpace,
                                  uint32_t flags, XImage* out);

/**
 * @brief 将图像转换到目标色彩空间和指定像素格式并返回新图像。
 * @param self 源图像对象指针。
 * @param colorSpace 目标色彩空间，必须是有效的目标空间。
 * @param format 输出像素格式，必须有效且与目标色彩模型兼容。
 * @param flags 图像格式转换标志。
 * @param out 输出图像对象指针；参数无效或转换失败时输出空图像。
 * @note 对标 Qt 6.8 `QImage::convertedToColorSpace(colorSpace, format, flags)`；
 *       与不带格式的重载不同，Invalid 格式不会自动选择中间格式。
 */
void XImage_convertedToColorSpace_ex(const XImage* self, XColorSpace colorSpace,
                                     XImageFormat format, uint32_t flags,
                                     XImage* out);
/**
 * @brief 就地转换图像到目标色彩空间。
 * @param self 目标图像对象指针。
 * @param colorSpace 目标色彩空间。
 * @param flags 转换标志。
 * @return 转换成功返回 true，否则返回 false。
 */
bool XImage_convertToColorSpace(XImage* self, XColorSpace colorSpace,
                                uint32_t flags);

/**
 * @brief 就地转换图像到目标色彩空间和指定像素格式。
 * @param self 目标图像对象指针。
 * @param colorSpace 目标色彩空间，必须是有效的目标空间。
 * @param format 输出像素格式，必须有效且与目标色彩模型兼容。
 * @param flags 图像格式转换标志。
 * @return 转换成功返回 true；源图像、目标空间或格式无效/不兼容时返回 false，
 *         且原图像保持不变。
 * @note 对标 Qt 6.8 `QImage::convertToColorSpace(colorSpace, format, flags)`；
 *       当目标空间与当前空间相同仍会执行指定格式转换。
 */
bool XImage_convertToColorSpace_ex(XImage* self, XColorSpace colorSpace,
                                   XImageFormat format, uint32_t flags);

/**
 * @brief 应用显式颜色变换并输出指定像素格式。
 * @param self 源图像对象指针。
 * @param transform 源色彩空间到目标色彩空间的变换描述。
 * @param format 输出像素格式。
 * @param flags 转换标志。
 * @param out 输出图像对象指针，旧内容会被释放。
 */
void XImage_applyColorTransform(const XImage* self, const XColorTransform* transform,
                                XImageFormat format, uint32_t flags, XImage* out);

/**
 * @brief      获取颜色表中的颜色数量
 * @param self 目标 XImage 对象指针
 * @return 颜色表中的颜色数量（索引格式有效）
 */
int XImage_colorCount(const XImage* self);

/**
 * @brief      设置颜色表大小
 * @param self  目标 XImage 对象指针
 * @param count 颜色数量
 */
void XImage_setColorCount(XImage* self, int count);

/**
 * @brief      获取颜色表中指定索引的颜色值
 * @param self  目标 XImage 对象指针
 * @param index 颜色索引
 * @return ARGB 颜色值（0xAARRGGBB）
 */
uint32_t XImage_color(const XImage* self, int index);

/**
 * @brief      设置颜色表中指定索引的颜色值
 * @param self     目标 XImage 对象指针
 * @param index    颜色索引；索引在当前位深范围内且超出颜色表时会自动扩展颜色表
 * @param color    ARGB 颜色值（0xAARRGGBB）
 */
void XImage_setColor(XImage* self, int index, uint32_t color);

/**
 * @brief      按存储像素值填充整幅图像（对标 QImage::fill(uint)）
 * @param self  目标 XImage 对象指针
 * @param pixel 原始像素值；位深不足时只使用低位，单色图只使用最低位
 */
void XImage_fill(XImage* self, uint32_t pixel);

/**
 * @brief 按 QColor 语义填充整幅图像（对标 QImage::fill(const QColor&)）。
 * @param self 目标 XImage 对象指针。
 * @param color 要填充的颜色；无效颜色不会改变图像。
 * @note 索引图像使用颜色表中的精确项，找不到时使用索引 0；单色图像仅
 *       在颜色为不透明白色时写入 1，其余颜色写入 0。
 */
void XImage_fillColor(XImage* self, const XColor* color);

/**
 * @brief      判断图像是否包含 Alpha 通道
 * @param self 目标 XImage 对象指针
 * @return 包含 Alpha 通道返回 true，否则返回 false
 */
bool XImage_hasAlphaChannel(const XImage* self);

/**
 * @brief      判断图像是否实际使用了 Alpha 通道
 * @param self 目标 XImage 对象指针
 * @return 使用了 Alpha 通道返回 true，否则返回 false
 */
bool XImage_hasAlpha(const XImage* self);

/**
 * @brief      判断图像是否所有像素均为灰度（R=G=B）
 * @param self 目标 XImage 对象指针
 * @return 全为灰度返回 true，否则返回 false
 */
bool XImage_allGray(const XImage* self);

/** @brief 判断图像是否为灰度图像（Qt QImage::isGrayscale 对应接口）。 */
bool XImage_isGrayscale(const XImage* self);

/**
 * @brief 获取指定坐标的 XColor 值。
 * @param self 目标图像对象指针。
 * @param x 像素横坐标，必须位于 [0,width) 范围内。
 * @param y 像素纵坐标，必须位于 [0,height) 范围内。
 * @return 坐标有效时返回像素颜色；坐标无效或图像为空时返回无效颜色。
 * @note 对 Grayscale16、RGBX64、RGBA64 和 RGBA64_Premultiplied 保留原生
 *       16 位通道精度；预乘格式返回的颜色按 Qt 规则还原为非预乘分量。
 */
XColor XImage_pixelColor(const XImage* self, int x, int y);

/** @brief 设置指定坐标的 XColor 值。 */
void XImage_setPixelColor(XImage* self, int x, int y, const XColor* color);

/**
 * @brief 复制颜色表到调用方缓冲区。
 * @param out 缓冲区；可为空以仅查询数量
 * @param maxCount out 可写入的最大颜色数
 * @return 颜色表项数量
 */
int XImage_colorTable(const XImage* self, uint32_t* out, int maxCount);

/** @brief 设置索引图像颜色表。 */
void XImage_setColorTable(XImage* self, const uint32_t* colors, int count);

/**
 * @brief 使用另一幅图像的 Alpha 通道合成当前图像。
 * @param self 目标图像对象指针；必要时按 Qt 的 Alpha 版本升级格式。
 * @param alphaChannel Alpha 来源图像；Alpha8 直接使用其字节，其它格式
 *                    先按 Qt 规则转换为灰度强度。
 * @return 参数、像素存储均有效且合成完成时返回 true；内存分配或格式
 *         转换失败时返回 false。
 * @note 目标已有 Alpha 时采用 DestinationIn 语义，将新旧 Alpha 相乘，
 *       不会覆盖已有透明度。目标与来源尺寸不同时，嵌入式实现使用
 *       最近邻采样近似 Qt QPainter 的平滑缩放路径。
 */
bool XImage_setAlphaChannel(XImage* self, const XImage* alphaChannel);

/**
 * @brief      根据图像 Alpha 通道创建一位 MonoLSB 掩码。
 * @param self 源图像对象指针；为空或 RGB32 时输出空图像。
 * @param flags 图像转换标志；flags=0 使用 128 阈值；OrderedAlphaDither 使用
 *              16x16 Bayer 矩阵；DiffuseAlphaDither 使用误差扩散。
 * @param out 输出掩码图像指针；已有内容会先释放，掩码颜色表为白色/黑色。
 * @note 深度为 1 的 Mono/MonoLSB 图像会先转为 Indexed8，使原颜色表中的
 *       Alpha 分量参与阈值判断；输出始终使用小端位序 MonoLSB。
 */
void XImage_createAlphaMask(const XImage* self, uint32_t flags, XImage* out);

/**
 * @brief      根据四角投票和边缘连通背景创建 MonoLSB 启发式掩码。
 * @param self 源图像对象指针；Alpha 通道会被忽略，仅比较 RGB。
 * @param clipTight 为 true 时只剥离边缘连通背景；为 false 时额外保留非背景
 *                  像素四邻域，得到较宽松的覆盖区域。
 * @param out 输出掩码图像指针；已有内容会先释放，并复制物理元数据。
 */
void XImage_createHeuristicMask(const XImage* self, bool clipTight, XImage* out);

/**
 * @brief      根据 Qt 的颜色匹配规则创建 MonoLSB 掩码。
 * @param self 源图像对象指针；为空时输出空图像。
 * @param color 要匹配的颜色值；非 32 位格式按 pixel() 的 0xAARRGGBB
 *              比较，32 位格式按扫描行的原始四字节存储值比较。
 * @param mode InColor 将匹配像素设为不透明，OutColor 将匹配像素设为透明。
 * @param out 输出掩码图像指针；已有内容会先释放，并复制物理元数据。
 */
void XImage_createMaskFromColor(const XImage* self, uint32_t color,
                                XImageMaskMode mode, XImage* out);

/**
 * @brief      以 ARGB 颜色填充矩形区域
 * @param self  目标 XImage 对象指针
 * @param rect  矩形区域指针（NULL 表示填充整个图像）
 * @param color ARGB 颜色值
 */
void XImage_fillRect(XImage* self, const XRect* rect, uint32_t color);

/**
 * @brief      清除矩形区域（填充为透明/黑色）
 * @param self  目标 XImage 对象指针
 * @param rect  矩形区域指针（NULL 表示清除整个图像）
 * @param color 清除颜色值（通常为 0 表示透明）
 */
void XImage_clear(XImage* self, const XRect* rect, uint32_t color);

/**
 * @brief      反转图像像素
 * @param self 目标 XImage 对象指针
 * @param mode 反转 RGB，或反转包括 Alpha 在内的所有分量
 */
void XImage_invertPixels(XImage* self, XImageInvertMode mode);

/* ========== 像素数据访问 ========== */

/**
 * @brief      获取像素数据的原始指针（可读写）
 * @param self 目标 XImage 对象指针
 * @return 像素数据缓冲区指针，NULL 表示空图像
 */
uint8_t* XImage_bits(XImage* self);

/**
 * @brief      获取像素数据的原始指针（只读）
 * @param self 目标 XImage 对象指针
 * @return 像素数据缓冲区指针，只读
 */
const uint8_t* XImage_constBits(const XImage* self);

/**
 * @brief      获取指定扫描线的像素数据指针（可读写）
 * @param self      目标 XImage 对象指针
 * @param scanLine  扫描线索引（0 为顶部）
 * @return 该扫描线的像素数据指针
 */
uint8_t* XImage_scanLine(XImage* self, int scanLine);

/**
 * @brief      获取指定扫描线的像素数据指针（只读）
 * @param self      目标 XImage 对象指针
 * @param scanLine  扫描线索引（0 为顶部）
 * @return 该扫描线的像素数据指针，只读
 */
const uint8_t* XImage_constScanLine(const XImage* self, int scanLine);

/**
 * @brief      获取每行数据的字节数
 * @param self 目标 XImage 对象指针
 * @return 每行字节数
 */
int XImage_bytesPerLine(const XImage* self);

/**
 * @brief      获取图像数据的总字节数
 * @param self 目标 XImage 对象指针
 * @return 总字节数
 */
int XImage_sizeInBytes(const XImage* self);

/**
 * @brief      获取指定坐标的像素索引值（索引格式使用）
 * @param self 目标 XImage 对象指针
 * @param x    像素 x 坐标
 * @param y    像素 y 坐标
 * @return 有效坐标返回像素索引；空图像或越界坐标返回 Qt 兼容哨兵 -12345。
 */
int XImage_pixelIndex(const XImage* self, int x, int y);

/**
 * @brief      获取指定坐标的 ARGB 颜色值
 * @param self 目标 XImage 对象指针
 * @param x    像素 x 坐标
 * @param y    像素 y 坐标
 * @return 有效坐标返回 ARGB 颜色值（0xAARRGGBB）；空图像或越界坐标返回
 *         Qt 兼容哨兵 12345。
 */
uint32_t XImage_pixel(const XImage* self, int x, int y);

/**
 * @brief      设置指定坐标的像素值
 * @param self          目标 XImage 对象指针
 * @param x             像素 x 坐标
 * @param y             像素 y 坐标
 * @param indexOrRgb    索引值或 ARGB 颜色值
 */
void XImage_setPixel(XImage* self, int x, int y, uint32_t indexOrRgb);

/**
 * @brief      判断指定坐标是否在图像有效区域内
 * @param self 目标 XImage 对象指针
 * @param x    像素 x 坐标
 * @param y    像素 y 坐标
 * @return 坐标有效返回 true，否则返回 false
 */
bool XImage_valid(const XImage* self, int x, int y);

/* ========== 图像复制与转换 ========== */

/**
 * @brief      复制图像指定区域
 * @param self  目标 XImage 对象指针
 * @param rect  要复制的矩形区域（NULL 表示复制整个图像）
 * @param out   输出结果图像指针
 */
void XImage_copyRect(const XImage* self, const XRect* rect, XImage* out);

/**
 * @brief      将图像转换为指定格式
 * @param self  目标 XImage 对象指针
 * @param format 目标像素格式
 * @param flags 转换标志
 * @param out   输出结果图像指针
 */
void XImage_convertToFormat(const XImage* self, XImageFormat format, uint32_t flags, XImage* out);

/**
 * @brief      将图像转换为指定格式（带颜色表）
 * @param self       目标 XImage 对象指针
 * @param format     目标像素格式
 * @param colorTable 颜色表指针
 * @param colorCount 颜色表大小
 * @param flags      转换标志
 * @param out        输出结果图像指针
 */
void XImage_convertToFormat_ex(const XImage* self, XImageFormat format,
                               const uint32_t* colorTable, int colorCount,
                               uint32_t flags, XImage* out);

/**
 * @brief      将图像转换为指定格式（就地转换，覆盖原数据）
 * @param self   目标 XImage 对象指针
 * @param format 目标像素格式
 * @param flags  转换标志
 * @return 转换成功返回 true，失败返回 false（原数据不变）
 */
bool XImage_convertToFormatInPlace(XImage* self, XImageFormat format, uint32_t flags);

/**
 * @brief      重新解释图像格式（不进行像素数据转换）
 * @param self   目标 XImage 对象指针
 * @param format 新的像素格式
 * @return 成功返回 true，格式不兼容返回 false
 */
bool XImage_reinterpretAsFormat(XImage* self, XImageFormat format);

/**
 * @brief      水平翻转图像
 * @param self 目标 XImage 对象指针
 * @param out  输出结果图像指针
 */
void XImage_mirrored(const XImage* self, bool horizontal, bool vertical, XImage* out);

/**
 * @brief      就地水平翻转图像
 * @param self       目标 XImage 对象指针
 * @param horizontal 是否水平翻转
 * @param vertical   是否垂直翻转
 */
void XImage_mirroredInPlace(XImage* self, bool horizontal, bool vertical);

/**
 * @brief 就地镜像图像的兼容别名（对标 QImage::mirror）。
 * @param self 目标 XImage 对象指针。
 * @param horizontal 是否沿垂直轴镜像。
 * @param vertical 是否沿水平轴镜像。
 */
void XImage_mirror(XImage* self, bool horizontal, bool vertical);

/**
 * @brief      交换 RGB 和 BGR 通道
 * @param self 目标 XImage 对象指针
 * @param out  输出结果图像指针
 */
void XImage_rgbSwapped(const XImage* self, XImage* out);

/**
 * @brief      就地交换 RGB 和 BGR 通道
 * @param self 目标 XImage 对象指针
 */
void XImage_rgbSwappedInPlace(XImage* self);

/**
 * @brief 就地交换 RGB 和 BGR 通道的兼容别名。
 * @param self 目标 XImage 对象指针。
 */
void XImage_rgbSwap(XImage* self);

/**
 * @brief      缩放图像
 * @param self    目标 XImage 对象指针
 * @param width   目标宽度
 * @param height  目标高度
 * @param aspectMode 宽高比模式
 * @param mode    变换模式（平滑/快速）
 * @param out     输出结果图像指针
 */
void XImage_scaled(const XImage* self, int width, int height, uint32_t aspectMode, uint32_t mode, XImage* out);

/**
 * @brief      根据宽度等比缩放
 * @param self  目标 XImage 对象指针
 * @param width 目标宽度
 * @param mode  变换模式
 * @param out   输出结果图像指针
 */
void XImage_scaledToWidth(const XImage* self, int width, uint32_t mode, XImage* out);

/**
 * @brief      根据高度等比缩放
 * @param self   目标 XImage 对象指针
 * @param height 目标高度
 * @param mode   变换模式
 * @param out    输出结果图像指针
 */
void XImage_scaledToHeight(const XImage* self, int height, uint32_t mode, XImage* out);

/** @brief 返回矩阵作用于图像边界后的平移修正和边界尺寸。 */
void XImage_trueMatrix(const XImageTransform* matrix, int width, int height,
                       XImageTransform* out, XSize* transformedSize);

/** @brief 按仿射矩阵变换图像，当前使用最近邻采样。 */
void XImage_transformed(const XImage* self, const XImageTransform* matrix,
                        uint32_t mode, XImage* out);

/**
 * @brief 将图像对象封装为 XVariant 借用指针。
 * @param self 源图像对象指针；Variant 不接管其生命周期。
 * @return 存储 XImage* 的 XVariant；失败返回 NULL。
 * @note 该接口用于 C API 的 QVariant 兼容适配，Variant 复制后仍是同一借用对象。
 */
XVariant* XImage_toVariant(const XImage* self);

/**
 * @brief 从 XVariant 取得图像借用指针。
 * @param variant 变体对象指针。
 * @return 变体中保存的 XImage*；类型不匹配返回 NULL。
 */
XImage* XImage_fromVariant(const XVariant* variant);

/** @brief 保留与 XVariant 类型适配命名一致的创建/读取宏。 */
#define XVariant_create_Image XImage_toVariant
#define XVariant_toImage      XImage_fromVariant

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载图像
 * @param self     目标 XImage 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示自动检测）
 * @return 加载成功返回 true，失败返回 false
 */
bool XImage_load(XImage* self, const XString* fileName, const XString* format);
/**
 * @brief 使用 UTF-8 文件名和格式加载图像的兼容重载。
 * @param self 目标图像对象指针。
 * @param fileName UTF-8 编码的文件名。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @return 加载成功返回 true，失败返回 false。
 */
bool XImage_load_2(XImage* self, const char* fileName, const char* format);

/**
 * @brief      从内存数据加载图像
 * @param self   目标 XImage 对象指针
 * @param data   图像数据指针
 * @param len    数据长度
 * @param format 图像格式字符串（NULL 表示自动检测）
 * @return 加载成功返回 true，失败返回 false
 */
bool XImage_loadFromData(XImage* self, const uint8_t* data, int len, const XString* format);
/**
 * @brief 使用 UTF-8 格式名从内存数据加载图像的兼容重载。
 * @param self 目标图像对象指针。
 * @param data 图像数据缓冲区。
 * @param len 缓冲区字节数。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @return 加载成功返回 true，失败返回 false。
 */
bool XImage_loadFromData_2(XImage* self, const uint8_t* data, int len, const char* format);

/**
 * @brief      保存图像到文件
 * @param self     目标 XImage 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示从扩展名自动检测）
 * @param quality  质量参数（-1 表示默认）
 * @return 保存成功返回 true，失败返回 false
 */
bool XImage_save(const XImage* self, const XString* fileName, const XString* format, int quality);
/**
 * @brief 使用 UTF-8 文件名和格式保存图像的兼容重载。
 * @param self 源图像对象指针。
 * @param fileName UTF-8 编码的目标文件名。
 * @param format UTF-8 编码的格式名；可为 NULL 以按扩展名判断。
 * @param quality 编码质量，-1 表示使用默认值。
 * @return 保存成功返回 true，失败返回 false。
 */
bool XImage_save_2(const XImage* self, const char* fileName, const char* format, int quality);

/**
 * @brief 从 XIODevice 读取图像；设备由调用方持有。
 * @param self 目标图像对象指针。
 * @param device 输入设备指针。
 * @param format XString 格式名；可为 NULL 以自动检测。
 * @return 读取成功返回 true，失败返回 false。
 */
bool XImage_loadDevice(XImage* self, XIODevice* device, const XString* format);
/**
 * @brief 使用 UTF-8 格式名从 XIODevice 读取图像的兼容重载。
 * @param self 目标图像对象指针。
 * @param device 输入设备指针。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @return 读取成功返回 true，失败返回 false。
 */
bool XImage_loadDevice_2(XImage* self, XIODevice* device, const char* format);

/**
 * @brief 将图像写入 XIODevice；设备由调用方持有。
 * @param self 源图像对象指针。
 * @param device 输出设备指针。
 * @param format XString 格式名；可为 NULL 以按设备或扩展名判断。
 * @param quality 编码质量，-1 表示使用默认值。
 * @return 写入成功返回 true，失败返回 false。
 */
bool XImage_saveDevice(const XImage* self, XIODevice* device, const XString* format, int quality);
/**
 * @brief 使用 UTF-8 格式名将图像写入 XIODevice 的兼容重载。
 * @param self 源图像对象指针。
 * @param device 输出设备指针。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动判断。
 * @param quality 编码质量，-1 表示使用默认值。
 * @return 写入成功返回 true，失败返回 false。
 */
bool XImage_saveDevice_2(const XImage* self, XIODevice* device, const char* format, int quality);

/* ========== 文本元数据 ========== */

/** @brief 获取文本元数据项数量。 */
int XImage_textCount(const XImage* self);

/**
 * @brief 获取图像文本元数据键的深复制列表。
 * @param self 图像对象指针；空图像也会返回空列表。
 * @return 新建的 XStringList；调用者负责使用 XStringList_delete_base 释放。
 * @note 返回列表按键的升序排列，修改返回列表不会影响图像自身。
 */
XStringList* XImage_textKeys(const XImage* self);

/** @brief 按索引获取文本元数据键副本；调用者负责释放返回的 XString。 */
XString* XImage_textKey(const XImage* self, int index);
/**
 * @brief 获取文本元数据键的内部只读引用。
 * @param self 图像对象指针。
 * @param index 元数据索引。
 * @return 内部 XString 引用；索引无效时返回 NULL，不得释放。
 */
const XString* XImage_textKey_const(const XImage* self, int index);
/**
 * @brief 获取文本元数据键的 UTF-8 兼容指针。
 * @param self 图像对象指针。
 * @param index 元数据索引。
 * @return UTF-8 指针；由内部缓存持有，不得释放。
 */
const char* XImage_textKey_2(const XImage* self, int index);

/**
 * @brief 按 XString 键获取文本元数据值副本；调用者负责释放返回的 XString。
 * @note 键为 NULL 或空字符串时返回全部文本元数据的聚合结果，
 *       格式为 "key: value\n\n..."（结尾多余的空行会被去掉，value 经过简化）。
 */
XString* XImage_text(const XImage* self, const XString* key);
/**
 * @brief 使用 UTF-8 键获取文本元数据值的兼容重载。
 * @param self 图像对象指针。
 * @param key UTF-8 编码的元数据键；传入 NULL 等价于 Qt 默认的空键，返回全部文本聚合结果。
 * @return 内部文本值的 UTF-8 指针，由图像对象持有，不得释放；空图像或未找到时返回空串。
 */
const char* XImage_text_2(const XImage* self, const char* key);

/** @brief 设置 UTF-8 文本元数据。重复键会覆盖原值。 */
void XImage_setText(XImage* self, const XString* key, const XString* value);

/**
 * @brief 获取对象持有的文本元数据引用，不复制。
 * @param self 图像对象指针。
 * @param key XString 元数据键。
 * @return 内部只读值引用；未找到时返回 NULL，不得释放。
 */
const XString* XImage_text_const(const XImage* self, const XString* key);

/**
 * @brief 使用 UTF-8 字符串设置文本元数据，转发到 XString 版本。
 * @param self 目标图像对象指针。
 * @param key UTF-8 编码的元数据键。
 * @param value UTF-8 编码的元数据值。
 */
void XImage_setText_2(XImage* self, const char* key, const char* value);

/* ========== 辅助数据 ========== */

/**
 * @brief      获取 X 方向分辨率
 * @param self 目标 XImage 对象指针
 * @return 每米的点数
 */
int XImage_dotsPerMeterX(const XImage* self);

/**
 * @brief      设置 X 方向分辨率
 * @param self 目标 XImage 对象指针
 * @param val  每米的点数
 */
void XImage_setDotsPerMeterX(XImage* self, int val);

/**
 * @brief      获取 Y 方向分辨率
 * @param self 目标 XImage 对象指针
 * @return 每米的点数
 */
int XImage_dotsPerMeterY(const XImage* self);

/**
 * @brief      设置 Y 方向分辨率
 * @param self 目标 XImage 对象指针
 * @param val  每米的点数
 */
void XImage_setDotsPerMeterY(XImage* self, int val);

/**
 * @brief      获取设备像素比
 * @param self 目标 XImage 对象指针
 * @return 设备像素比，默认值为 1.0
 */
float XImage_devicePixelRatio(const XImage* self);

/**
 * @brief 获取图像的设备无关尺寸（对标 QImage::deviceIndependentSize）。
 * @param self 目标图像对象指针。
 * @param out 输出浮点尺寸；空图像输出 (0,0)，输出指针可为 NULL。
 * @note 结果等于像素尺寸除以 devicePixelRatio；比例为零时遵循 C 浮点除法语义。
 */
void XImage_deviceIndependentSize(const XImage* self, XSizeF* out);

/**
 * @brief      设置设备像素比
 * @param self 目标 XImage 对象指针
 * @param scaleFactor 设备像素比；Qt 允许零和负值，接口按原值保存
 */
void XImage_setDevicePixelRatio(XImage* self, float scaleFactor);

/**
 * @brief      获取图像偏移
 * @param self 目标 XImage 对象指针
 * @param out  输出偏移坐标
 */
void XImage_offset(const XImage* self, XPoint* out);

/**
 * @brief      设置图像偏移
 * @param self 目标 XImage 对象指针
 * @param pos  偏移坐标
 */
void XImage_setOffset(XImage* self, const XPoint* pos);

/**
 * @brief      获取缓存键值
 * @param self 目标 XImage 对象指针
 * @return 缓存键值（64 位）
 */
int64_t XImage_cacheKey(const XImage* self);

/**
 * @brief      分离数据（写时复制）
 * @param self 目标 XImage 对象指针
 */
void XImage_detach(XImage* self);

/**
 * @brief      判断数据是否已分离
 * @param self 目标 XImage 对象指针
 * @return 已分离返回 true
 */
bool XImage_isDetached(const XImage* self);

/* ========== 静态工具方法 ========== */

/**
 * @brief      从内存数据创建图像
 * @param data   原始图像数据指针
 * @param size   数据大小
 * @param format 图像格式字符串（NULL 表示自动检测）
 * @param out    输出结果图像指针
 */
void XImage_fromData(const uint8_t* data, int size, const XString* format, XImage* out);
/**
 * @brief 使用 UTF-8 格式名从内存数据创建图像的兼容重载。
 * @param data 原始图像数据缓冲区。
 * @param size 数据字节数。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @param out 输出图像对象指针，旧内容会被释放。
 */
void XImage_fromData_2(const uint8_t* data, int size, const char* format, XImage* out);

/**
 * @brief      将图像转换为指定格式的像素数据
 * @param format 像素格式
 * @return 像素格式描述
 */
XString* XImage_formatToStr(XImageFormat format);
/**
 * @brief 获取图像格式对应的 UTF-8 名称兼容指针。
 * @param format 图像格式枚举值。
 * @return 静态 UTF-8 字符串；未知格式返回 NULL。
 */
const char* XImage_formatToStr_2(XImageFormat format);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XImage_create
#define XImage_create() XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XIMAGE_H */
