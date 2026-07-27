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
 * @return     指向新创建的 XImageReader 对象的指针，失败返回 NULL
 */
XImageReader* XImageReader_create();

/**
 * @brief      初始化 XImageReader 实例（空读取器）
 * @param self 待初始化的 XImageReader 对象指针
 */
void XImageReader_init(XImageReader* self);

/**
 * @brief      使用设备初始化图像读取器
 * @param self   待初始化的 XImageReader 对象指针
 * @param device XIODevice 指针
 * @param format 格式字符串（可为空字符串，表示自动检测）
 */
void XImageReader_init_device(XImageReader* self, XIODevice* device, const char* format);

/**
 * @brief      使用文件名初始化图像读取器
 * @param self     待初始化的 XImageReader 对象指针
 * @param fileName 文件名
 * @param format   格式字符串（可为空字符串，表示自动检测）
 */
void XImageReader_init_file(XImageReader* self, const char* fileName, const char* format);

/**
 * @brief      释放 XImageReader 资源
 * @param self 待释放的 XImageReader 对象指针
 */
void XImageReader_deinit(XImageReader* self);

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
void XImageReader_deinit_base(XImageReader* self);

/* ========== 格式设置 ========== */

/**
 * @brief      设置图像格式
 * @param self   目标 XImageReader 对象指针
 * @param format 格式字符串
 */
void XImageReader_setFormat(XImageReader* self, const char* format);

/**
 * @brief      获取图像格式
 * @param self 目标 XImageReader 对象指针
 * @return 格式字符串
 */
const char* XImageReader_format(const XImageReader* self);

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
 * @param ignored 参数（保留，当前未使用）
 */
void XImageReader_setDecideFormatFromContent(XImageReader* self, bool ignored);

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
 * @param fileName 文件名
 */
void XImageReader_setFileName(XImageReader* self, const char* fileName);

/**
 * @brief      获取文件名
 * @param self 目标 XImageReader 对象指针
 * @return 文件名
 */
const char* XImageReader_fileName(const XImageReader* self);

/* ========== 图像信息 ========== */

/**
 * @brief      获取图像尺寸
 * @param self 目标 XImageReader 对象指针
 * @param out  输出尺寸指针
 */
void XImageReader_size(const XImageReader* self, XSize* out);

/**
 * @brief      获取图像格式类型
 * @param self 目标 XImageReader 对象指针
 * @return 图像格式枚举
 */

/**
 * @brief      获取文本键列表
 * @param self 目标 XImageReader 对象指针
 * @return 文本键列表字符串数组
 */
void* XImageReader_textKeys(const XImageReader* self);

/**
 * @brief      获取指定键的文本
 * @param self 目标 XImageReader 对象指针
 * @param key  文本键
 * @return 文本值字符串
 */
const char* XImageReader_text(const XImageReader* self, const char* key);

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
 * @brief      设置背景色
 * @param self  目标 XImageReader 对象指针
 * @param color ARGB 颜色值
 */
void XImageReader_setBackgroundColor(XImageReader* self, uint32_t color);

/**
 * @brief      获取背景色
 * @param self 目标 XImageReader 对象指针
 * @return ARGB 颜色值
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
const char* XImageReader_subType(const XImageReader* self);

/**
 * @brief      获取支持的子类型列表
 * @param self 目标 XImageReader 对象指针
 * @return 支持的子类型列表
 */
void* XImageReader_supportedSubTypes(const XImageReader* self);

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
const char* XImageReader_errorString(const XImageReader* self);

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
const char* XImageReader_imageFormat(const char* fileName);

/**
 * @brief      通过设备检测图像格式
 * @param device XIODevice 指针
 * @return 格式字符串
 */
const char* XImageReader_imageFormatDevice(XIODevice* device);

/**
 * @brief      获取支持的图像格式列表
 * @return 新分配的 XVector（元素为 const char*）；调用者负责
 *         使用 XVector_delete_base 释放向量，元素字符串由库静态持有
 */
void* XImageReader_supportedImageFormats();

/**
 * @brief      获取支持的 MIME 类型列表
 * @return 新分配的 XVector（元素为 const char*）；调用者负责释放向量
 */
void* XImageReader_supportedMimeTypes();

/**
 * @brief      获取指定 MIME 类型对应的图像格式列表
 * @param mimeType MIME 类型字符串
 * @return 新分配的 XVector（元素为 const char*）；调用者负责释放向量
 */
void* XImageReader_imageFormatsForMimeType(const char* mimeType);

/**
 * @brief      获取内存分配限制（MB）
 * @return 限制值（MB）
 */
int XImageReader_allocationLimit();

/**
 * @brief      设置内存分配限制（MB）
 * @param mbLimit 限制值（MB）
 */
void XImageReader_setAllocationLimit(int mbLimit);

#ifdef __cplusplus
}
#endif
#endif /* XIMAGEREADER_H */


