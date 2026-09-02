/******************************************************************************
 * @file       XImageReader.h
 * @brief      XImageReader 图像读取器类（对标 Qt 6.8 QImageReader）
 * @author     XinYueC 团队
 * @note       提供从文件或设备读取图像的功能，支持格式检测、缩放、裁剪、动画等
 ******************************************************************************/
#ifndef XIMAGEREADER_H
#define XIMAGEREADER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XImage.h"
#include "XImageFormat.h"
#include "XImageIOHandler.h"
#include "XIODevice.h"
#include "XClass.h"
#include "XTypes.h"
#include "XString.h"
#include "XStringList.h"


/* ========== XImageReader 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XImageReader)
XCLASS_DEFINE_EXTEND_END(XImageReader, XClass)

/* 前向声明 */
typedef struct XImageReaderPrivate XImageReaderPrivate;

/**
 * @brief      XImageReader 错误码枚举（对标 Qt 6.8 QImageReader::ImageReaderError）
 */
typedef enum XImageReaderError
{
    XImageReaderError_UnknownError,          /**< 未知错误 */
    XImageReaderError_FileNotFoundError,     /**< 文件未找到 */
    XImageReaderError_DeviceError,           /**< 设备错误 */
    XImageReaderError_UnsupportedFormatError,/**< 不支持的格式 */
    XImageReaderError_InvalidDataError       /**< 无效数据 */
} XImageReaderError;

/**
 * @brief      XImageReader 图像读取器类结构体（对标 Qt 6.8 QImageReader）
 * @note       继承自 XClass，提供从文件或设备读取图像的功能
 */
typedef struct XImageReader
{
    XClass                m_class;   /**< 继承的基类成员 */
    XImageReaderPrivate*   m_data;    /**< 私有数据指针 */
}XImageReader;

/**
 * @brief      在堆上创建 XImageReader 实例
 * @param memory 对象使用的项目内存类型。
 * @return     指向新创建的 XImageReader 对象的指针，失败返回 NULL
 */
XImageReader* XImageReader_create_ex(XMemoryType memory);

/**
 * @brief      初始化 XImageReader 实例（空读取器）
 * @param self 待初始化的 XImageReader 对象指针
 */
void XImageReader_init(XImageReader* self);

/**
 * @brief      使用设备初始化图像读取器
 * @param self   待初始化的 XImageReader 对象指针
 * @param device XIODevice 指针
 * @param format XString 格式（可为空，表示自动检测）
 */
void XImageReader_init_device(XImageReader* self, XIODevice* device, const XString* format);

/**
 * @brief 使用 UTF-8 格式字符串初始化读取器的兼容重载。
 * @param self 待读取器对象指针。
 * @param device 输入设备指针，由调用方持有。
 * @param format UTF-8 格式名；传入 NULL 或空串表示自动检测。
 */
void XImageReader_init_device_2(XImageReader* self, XIODevice* device, const char* format);

/**
 * @brief      使用文件名初始化图像读取器
 * @param self     待初始化的 XImageReader 对象指针
 * @param fileName XString 文件名
 * @param format   XString 格式（可为空，表示自动检测）
 */
void XImageReader_init_file(XImageReader* self, const XString* fileName, const XString* format);

/**
 * @brief 使用 UTF-8 文件名和格式初始化读取器的兼容重载。
 * @param self 待读取器对象指针。
 * @param fileName UTF-8 文件名；传入 NULL 表示未设置文件。
 * @param format UTF-8 格式名；传入 NULL 或空串表示自动检测。
 */
void XImageReader_init_file_2(XImageReader* self, const char* fileName, const char* format);

/** @brief 通过 XClass 虚表释放读取器资源。 @param self 待读取器指针。 */
#define XImageReader_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 通过 XClass 虚表移动读取器资源。 @param self 目标读取器指针。 @param other 源读取器指针。 */
#define XImageReader_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))
#define XImageReader_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的图像读取器。 @param self 待读取器指针。 */
#define XImageReader_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 格式设置 ========== */

/**
 * @brief      设置图像格式
 * @param self   目标 XImageReader 对象指针
 * @param format XString 格式
 */
void XImageReader_setFormat(XImageReader* self, const XString* format);

/**
 * @brief 使用 UTF-8 格式字符串设置图像格式的兼容重载。
 * @param self 目标读取器对象指针。
 * @param format UTF-8 格式名；传入 NULL 或空串表示自动检测。
 */
void XImageReader_setFormat_2(XImageReader* self, const char* format);

/**
 * @brief      获取图像格式
 * @param self 目标 XImageReader 对象指针
 * @return 格式字符串
 */
XString* XImageReader_format(const XImageReader* self);

/**
 * @brief 获取内部格式字符串引用。
 * @param self 读取器对象指针。
 * @return 显式设置的格式，或自动探测后处理器报告的格式；返回值由读取器持有，
 *         对象无设备、格式不支持或探测失败时返回 NULL。
 */
const XString* XImageReader_format_const(const XImageReader* self);

/**
 * @brief 获取 UTF-8 兼容格式字符串。
 * @param self 读取器对象指针。
 * @return UTF-8 格式名；返回值由读取器内部字符串或探测缓存持有，不得释放。
 */
const char* XImageReader_format_2(const XImageReader* self);

/**
 * @brief      设置是否自动检测图像格式
 * @param self    目标 XImageReader 对象指针
 * @param enabled 启用自动检测
 */
void XImageReader_setAutoDetectImageFormat(XImageReader* self, bool enabled);

/**
 * @brief      查询是否自动检测图像格式
 * @param self 目标 XImageReader 对象指针
 * @return 启用返回 true
 */
bool XImageReader_autoDetectImageFormat(const XImageReader* self);

/**
 * @brief      设置是否根据内容决定格式
 * @param self    目标 XImageReader 对象指针
 * @param enabled 是否忽略显式格式和文件扩展名并按设备内容决定格式
 * @note       该标志独立于 autoDetectImageFormat；传入 true 或 false 均不改写
 *             自动探测状态。按内容选择处理器时，读取器内部会优先使用该标志。
 */
void XImageReader_setDecideFormatFromContent(XImageReader* self, bool enabled);

/**
 * @brief      查询是否根据内容决定格式
 * @param self 目标 XImageReader 对象指针
 * @return 当前值
 */
bool XImageReader_decideFormatFromContent(const XImageReader* self);

/* ========== 设备与文件 ========== */

/**
 * @brief      设置 IO 设备
 * @param self   目标 XImageReader 对象指针
 * @param device XIODevice 指针
 */
void XImageReader_setDevice(XImageReader* self, XIODevice* device);

/**
 * @brief      获取 IO 设备
 * @param self 目标 XImageReader 对象指针
 * @return XIODevice 指针
 */
XIODevice* XImageReader_device(const XImageReader* self);

/**
 * @brief      设置文件名
 * @param self     目标 XImageReader 对象指针
 * @param fileName XString 文件名
 */
void XImageReader_setFileName(XImageReader* self, const XString* fileName);

/** @brief 使用 UTF-8 文件名设置文件名的兼容重载。 */
void XImageReader_setFileName_2(XImageReader* self, const char* fileName);

/**
 * @brief      获取文件名
 * @param self 目标 XImageReader 对象指针
 * @return 文件名
 */
XString* XImageReader_fileName(const XImageReader* self);

/** @brief 获取内部文件名字符串引用；返回值由读取器持有。 */
const XString* XImageReader_fileName_const(const XImageReader* self);

/** @brief 获取 UTF-8 兼容文件名字符串；返回值由内部 XString 缓存持有。 */
const char* XImageReader_fileName_2(const XImageReader* self);

/* ========== 图像信息 ========== */

/**
 * @brief      获取图像尺寸
 * @param self 目标 XImageReader 对象指针
 * @param out  输出尺寸指针
 */
void XImageReader_size(const XImageReader* self, XSize* out);

/**
 * @brief 获取处理器报告的图像像素格式。
 * @param self 读取器对象指针。
 * @return 处理器支持 ImageFormat 选项时返回图像格式，否则返回
 *         XImageFormat_Invalid；该查询不会读取图像像素数据。
 */
XImageFormat XImageReader_imageFormatValue(const XImageReader* self);

/**
 * @brief 获取图像描述中的文本键列表。
 * @param self 读取器对象指针。
 * @return 新建的文本键列表；键按 Qt QMap 规则排序，调用方负责释放；处理器不支持
 *         Description 或读取器无效时返回空列表。
 */
XStringList* XImageReader_textKeys(const XImageReader* self);

/**
 * @brief      获取指定键的文本
 * @param self 目标 XImageReader 对象指针
 * @param key  文本键
 * @return 文本值字符串；未找到时返回空字符串对象，调用方负责释放。
 */
XString* XImageReader_text(const XImageReader* self, const XString* key);
/**
 * @brief 使用 UTF-8 键获取文本元数据的兼容重载。
 * @param self 读取器对象指针。
 * @param key UTF-8 编码的文本键。
 * @return UTF-8 文本值指针，由内部缓存持有，不得释放；空键或未找到时返回空串。
 */
const char* XImageReader_text_2(const XImageReader* self, const char* key);

/* ========== 裁剪与缩放 ========== */

/**
 * @brief      设置裁剪矩形
 * @param self 目标 XImageReader 对象指针
 * @param rect 裁剪矩形指针
 */
void XImageReader_setClipRect(XImageReader* self, const XRect* rect);

/**
 * @brief      获取裁剪矩形
 * @param self 目标 XImageReader 对象指针
 * @param out  输出矩形指针
 */
void XImageReader_clipRect(const XImageReader* self, XRect* out);

/**
 * @brief      设置缩放尺寸
 * @param self 目标 XImageReader 对象指针
 * @param size 缩放尺寸指针
 */
void XImageReader_setScaledSize(XImageReader* self, const XSize* size);

/**
 * @brief      获取缩放尺寸
 * @param self 目标 XImageReader 对象指针
 * @param out  输出尺寸指针
 */
void XImageReader_scaledSize(const XImageReader* self, XSize* out);

/**
 * @brief      设置质量参数
 * @param self    目标 XImageReader 对象指针
 * @param quality 质量值（-1 表示默认）
 */
void XImageReader_setQuality(XImageReader* self, int quality);

/**
 * @brief      获取质量参数
 * @param self 目标 XImageReader 对象指针
 * @return 质量值
 */
int XImageReader_quality(const XImageReader* self);

/**
 * @brief      设置缩放裁剪矩形
 * @param self 目标 XImageReader 对象指针
 * @param rect 缩放裁剪矩形指针
 */
void XImageReader_setScaledClipRect(XImageReader* self, const XRect* rect);

/**
 * @brief      获取缩放裁剪矩形
 * @param self 目标 XImageReader 对象指针
 * @param out  输出矩形指针
 */
void XImageReader_scaledClipRect(const XImageReader* self, XRect* out);

/**
 * @brief 设置图像处理器读取时使用的背景色。
 * @param self 目标 XImageReader 对象指针。
 * @param color ARGB32 颜色值；仅在当前处理器支持 BackgroundColor 时生效。
 */
void XImageReader_setBackgroundColor(XImageReader* self, uint32_t color);

/**
 * @brief 获取图像处理器当前使用的背景色。
 * @param self 读取器对象指针。
 * @return 处理器支持 BackgroundColor 时返回其 ARGB32 选项值；不支持、未初始化或
 *         选项查询失败时返回 0，表示无效背景色。
 */
uint32_t XImageReader_backgroundColor(const XImageReader* self);

/* ========== 动画支持 ========== */

/**
 * @brief      判断是否支持动画
 * @param self 目标 XImageReader 对象指针
 * @return 支持动画返回 true
 */
bool XImageReader_supportsAnimation(const XImageReader* self);

/**
 * @brief      获取变换类型
 * @param self 目标 XImageReader 对象指针
 * @return 变换类型
 */
XImageIOHandlerTransformation XImageReader_transformation(const XImageReader* self);

/**
 * @brief      设置是否自动变换
 * @param self    目标 XImageReader 对象指针
 * @param enabled 启用自动变换
 */
void XImageReader_setAutoTransform(XImageReader* self, bool enabled);

/**
 * @brief      查询是否自动变换
 * @param self 目标 XImageReader 对象指针
 * @return 启用返回 true
 */
bool XImageReader_autoTransform(const XImageReader* self);

/**
 * @brief      获取子类型
 * @param self 目标 XImageReader 对象指针
 * @return 子类型字符串
 */
XString* XImageReader_subType(const XImageReader* self);

/** @brief 获取内部子类型字符串引用；BMP 读取器没有子类型时返回 NULL。 */
const XString* XImageReader_subType_const(const XImageReader* self);

/** @brief 获取 UTF-8 兼容子类型字符串；返回值由内部 XString 缓存持有。 */
const char* XImageReader_subType_2(const XImageReader* self);

/**
 * @brief      获取支持的子类型列表
 * @param self 目标 XImageReader 对象指针
 * @return 支持的子类型列表
 */
XStringList* XImageReader_supportedSubTypes(const XImageReader* self);

/* ========== 读取操作 ========== */

/**
 * @brief      判断是否可以读取
 * @param self 目标 XImageReader 对象指针
 * @return 可以读取返回 true
 */
bool XImageReader_canRead(const XImageReader* self);

/**
 * @brief      读取图像
 * @param self 目标 XImageReader 对象指针
 * @param out  输出图像指针
 * @return 读取成功返回 true
 * @note 文件名基名末尾为 @2x 至 @9x 时，成功读取的图像设备像素比按 Qt 规则设置；
 *       设置 QT_HIGHDPI_DISABLE_2X_IMAGE_LOADING 非空可禁用该推导。
 */
bool XImageReader_read(XImageReader* self, XImage* out);

/**
 * @brief      跳转到下一帧
 * @param self 目标 XImageReader 对象指针
 * @return 跳转成功返回 true
 */
bool XImageReader_jumpToNextImage(XImageReader* self);

/**
 * @brief      跳转到指定帧
 * @param self        目标 XImageReader 对象指针
 * @param imageNumber 帧编号
 * @return 跳转成功返回 true
 */
bool XImageReader_jumpToImage(XImageReader* self, int imageNumber);

/**
 * @brief      获取循环次数
 * @param self 目标 XImageReader 对象指针
 * @return 循环次数
 */
int XImageReader_loopCount(const XImageReader* self);

/**
 * @brief      获取总帧数
 * @param self 目标 XImageReader 对象指针
 * @return 帧数
 */
int XImageReader_imageCount(const XImageReader* self);

/**
 * @brief      获取下一帧延迟（毫秒）
 * @param self 目标 XImageReader 对象指针
 * @return 延迟毫秒数
 */
int XImageReader_nextImageDelay(const XImageReader* self);

/**
 * @brief      获取当前帧编号
 * @param self 目标 XImageReader 对象指针
 * @return 当前帧编号
 */
int XImageReader_currentImageNumber(const XImageReader* self);

/**
 * @brief      获取当前帧矩形
 * @param self 目标 XImageReader 对象指针
 * @param out  输出矩形指针
 */
void XImageReader_currentImageRect(const XImageReader* self, XRect* out);

/* ========== 错误处理 ========== */

/**
 * @brief      获取错误码
 * @param self 目标 XImageReader 对象指针
 * @return 错误码枚举
 */
XImageReaderError XImageReader_error(const XImageReader* self);

/**
 * @brief      获取错误描述
 * @param self 目标 XImageReader 对象指针
 * @return 错误描述字符串
 */
XString* XImageReader_errorString(const XImageReader* self);

/** @brief 获取内部错误描述引用；未设置错误文本时返回空值对象。 */
const XString* XImageReader_errorString_const(const XImageReader* self);

/** @brief 获取 UTF-8 兼容错误描述；无错误文本时返回稳定的 "Unknown error"。 */
const char* XImageReader_errorString_2(const XImageReader* self);

/**
 * @brief      判断是否支持指定选项
 * @param self   目标 XImageReader 对象指针
 * @param option 图像选项枚举
 * @return 支持返回 true
 */
bool XImageReader_supportsOption(const XImageReader* self, XImageIOHandlerOption option);

/* ========== 静态方法 ========== */

/**
 * @brief      通过文件名检测图像格式
 * @param fileName 文件名
 * @return 格式字符串
 */
XString* XImageReader_imageFormat(const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名探测图像格式的兼容重载。
 * @param fileName UTF-8 编码的文件名。
 * @return UTF-8 格式名指针，由内部静态缓存持有（最多 255 个字节）；失败时返回 NULL，不得释放。
 */
const char* XImageReader_imageFormat_2(const char* fileName);

/**
 * @brief      通过设备检测图像格式
 * @param device XIODevice 指针
 * @return 格式字符串
 */
XString* XImageReader_imageFormatDevice(XIODevice* device);
/**
 * @brief 从设备探测图像格式并返回 UTF-8 兼容指针。
 * @param device 输入设备指针，由调用方持有。
 * @return UTF-8 格式名指针，由内部缓存持有（最多 255 个字节）；失败时返回 NULL。
 */
const char* XImageReader_imageFormatDevice_2(XIODevice* device);

/**
 * @brief      获取支持的图像格式列表
 * @return 新分配的 XStringList（元素为 XString）；调用者负责使用
 *         XStringList_delete_base 释放列表，元素由列表拥有
 */
XStringList* XImageReader_supportedImageFormats();

/**
 * @brief      获取支持的 MIME 类型列表
 * @return 新分配的 XStringList（元素为 XString）；调用者负责释放列表
 */
XStringList* XImageReader_supportedMimeTypes();

/**
 * @brief      获取指定 MIME 类型对应的图像格式列表
 * @param mimeType MIME 类型字符串
 * @return 新分配的 XStringList（元素为 XString）；调用者负责释放列表
 */
XStringList* XImageReader_imageFormatsForMimeType(const XString* mimeType);
/**
 * @brief 使用 UTF-8 MIME 类型查询格式列表的兼容重载。
 * @param mimeType UTF-8 编码的 MIME 类型。
 * @return 新建的 XStringList；调用方负责释放。
 */
XStringList* XImageReader_imageFormatsForMimeType_2(const char* mimeType);

/**
 * @brief      获取内存分配限制（MB）
 * @return 限制值（MB）
 */
int XImageReader_allocationLimit();

/**
 * @brief      设置内存分配限制（MB）
 * @param mbLimit 限制值（MB）
 * @note       负值按 Qt 语义被忽略；传入 0 可关闭内存分配检查。
 */
void XImageReader_setAllocationLimit(int mbLimit);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XImageReader_create
#define XImageReader_create() XImageReader_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XIMAGEREADER_H */
