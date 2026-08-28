/******************************************************************************
 * @file       XImageWriter.h
 * @brief      XImageWriter 图像写入器类（对标 Qt 6.8 QImageWriter）
 * @author     XinYueC 团队
 * @note       提供将图像写入文件或设备的功能，支持格式选择、质量设置等
 ******************************************************************************/
#ifndef XIMAGEWRITER_H
#define XIMAGEWRITER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XImage.h"
#include "XImageIOHandler.h"
#include "XIODevice.h"
#include "XClass.h"
#include "XString.h"
#include "XStringList.h"


/* ========== XImageWriter 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XImageWriter)
XCLASS_DEFINE_EXTEND_END(XImageWriter, XClass)

/* 前向声明 */
typedef struct XImageWriterPrivate XImageWriterPrivate;

/**
 * @brief      XImageWriter 错误码枚举（对标 Qt 6.8 QImageWriter::ImageWriterError）
 */
typedef enum XImageWriterError
{
    XImageWriterError_UnknownError,           /**< 未知错误 */
    XImageWriterError_DeviceError,            /**< 设备错误 */
    XImageWriterError_UnsupportedFormatError,  /**< 不支持的格式 */
    XImageWriterError_InvalidImageError       /**< 无效图像 */
} XImageWriterError;

/**
 * @brief      XImageWriter 图像写入器类结构体（对标 Qt 6.8 QImageWriter）
 * @note       继承自 XClass，提供将图像写入文件或设备的功能
 */
typedef struct XImageWriter
{
    XClass                m_class;   /**< 继承的基类成员 */
    XImageWriterPrivate*   m_data;    /**< 私有数据指针 */
}XImageWriter;

/**
 * @brief      在堆上创建 XImageWriter 实例
 * @return     指向新创建的 XImageWriter 对象的指针，失败返回 NULL
 */
XImageWriter* XImageWriter_create_ex(XMemoryType memory);

/**
 * @brief      初始化 XImageWriter 实例（空写入器）
 * @param self 待初始化的 XImageWriter 对象指针
 */
void XImageWriter_init(XImageWriter* self);

/**
 * @brief      使用设备初始化图像写入器
 * @param self   待初始化的 XImageWriter 对象指针
 * @param device XIODevice 指针
 * @param format XString 格式（可为空，表示自动检测）
 */
void XImageWriter_init_device(XImageWriter* self, XIODevice* device, const XString* format);

/** @brief 使用 UTF-8 格式字符串初始化写入器的兼容重载。 */
void XImageWriter_init_device_2(XImageWriter* self, XIODevice* device, const char* format);

/**
 * @brief      使用文件名初始化图像写入器
 * @param self     待初始化的 XImageWriter 对象指针
 * @param fileName XString 文件名
 * @param format   XString 格式（可为空，表示从扩展名自动检测）
 */
void XImageWriter_init_file(XImageWriter* self, const XString* fileName, const XString* format);

/** @brief 使用 UTF-8 文件名和格式初始化写入器的兼容重载。 */
void XImageWriter_init_file_2(XImageWriter* self, const char* fileName, const char* format);

/**
 * @brief      释放 XImageWriter 资源
 * @param self 待释放的 XImageWriter 对象指针
 */
/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放写入器资源。 @param self 待写入器指针。 */
#define XImageWriter_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的图像写入器。 @param self 待写入器指针。 */
#define XImageWriter_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 格式设置 ========== */

/**
 * @brief      设置图像格式
 * @param self   目标 XImageWriter 对象指针
 * @param format XString 格式
 */
void XImageWriter_setFormat(XImageWriter* self, const XString* format);

/** @brief 使用 UTF-8 格式字符串设置图像格式的兼容重载。 */
void XImageWriter_setFormat_2(XImageWriter* self, const char* format);

/**
 * @brief      获取图像格式
 * @param self 目标 XImageWriter 对象指针
 * @return 格式字符串
 */
XString* XImageWriter_format(const XImageWriter* self);

/** @brief 获取内部格式字符串引用；返回值由写入器持有。 */
const XString* XImageWriter_format_const(const XImageWriter* self);

/** @brief 获取 UTF-8 兼容格式字符串；返回值由内部 XString 缓存持有。 */
const char* XImageWriter_format_2(const XImageWriter* self);

/* ========== 设备与文件 ========== */

/**
 * @brief      设置 IO 设备
 * @param self   目标 XImageWriter 对象指针
 * @param device XIODevice 指针
 */
void XImageWriter_setDevice(XImageWriter* self, XIODevice* device);

/**
 * @brief      获取 IO 设备
 * @param self 目标 XImageWriter 对象指针
 * @return XIODevice 指针
 */
XIODevice* XImageWriter_device(const XImageWriter* self);

/**
 * @brief      设置文件名
 * @param self     目标 XImageWriter 对象指针
 * @param fileName XString 文件名
 */
void XImageWriter_setFileName(XImageWriter* self, const XString* fileName);

/** @brief 使用 UTF-8 文件名设置文件名的兼容重载。 */
void XImageWriter_setFileName_2(XImageWriter* self, const char* fileName);

/**
 * @brief      获取文件名
 * @param self 目标 XImageWriter 对象指针
 * @return 文件名
 */
XString* XImageWriter_fileName(const XImageWriter* self);

/** @brief 获取内部文件名字符串引用；返回值由写入器持有。 */
const XString* XImageWriter_fileName_const(const XImageWriter* self);

/** @brief 获取 UTF-8 兼容文件名字符串；返回值由内部 XString 缓存持有。 */
const char* XImageWriter_fileName_2(const XImageWriter* self);

/* ========== 参数设置 ========== */

/**
 * @brief      设置质量参数
 * @param self    目标 XImageWriter 对象指针
 * @param quality 质量值（-1 表示默认，0-100 范围）
 */
void XImageWriter_setQuality(XImageWriter* self, int quality);

/**
 * @brief      获取质量参数
 * @param self 目标 XImageWriter 对象指针
 * @return 质量值
 */
int XImageWriter_quality(const XImageWriter* self);

/**
 * @brief      设置压缩参数
 * @param self        目标 XImageWriter 对象指针
 * @param compression 压缩值
 */
void XImageWriter_setCompression(XImageWriter* self, int compression);

/**
 * @brief      获取压缩参数
 * @param self 目标 XImageWriter 对象指针
 * @return 压缩值
 */
int XImageWriter_compression(const XImageWriter* self);

/**
 * @brief      设置子类型
 * @param self 目标 XImageWriter 对象指针
 * @param type XString 子类型
 */
void XImageWriter_setSubType(XImageWriter* self, const XString* type);

/** @brief 使用 UTF-8 子类型字符串设置图像子类型的兼容重载。 */
void XImageWriter_setSubType_2(XImageWriter* self, const char* type);

/**
 * @brief      获取子类型
 * @param self 目标 XImageWriter 对象指针
 * @return 子类型字符串
 */
XString* XImageWriter_subType(const XImageWriter* self);

/** @brief 获取内部子类型字符串引用。 */
const XString* XImageWriter_subType_const(const XImageWriter* self);

/** @brief 获取 UTF-8 兼容子类型字符串；返回值由内部 XString 缓存持有。 */
const char* XImageWriter_subType_2(const XImageWriter* self);

/**
 * @brief      获取支持的子类型列表
 * @param self 目标 XImageWriter 对象指针
 * @return 支持的子类型列表
 */
XStringList* XImageWriter_supportedSubTypes(const XImageWriter* self);

/**
 * @brief      设置是否启用优化写入
 * @param self    目标 XImageWriter 对象指针
 * @param optimize 启用优化写入
 */
void XImageWriter_setOptimizedWrite(XImageWriter* self, bool optimize);

/**
 * @brief      查询是否启用优化写入
 * @param self 目标 XImageWriter 对象指针
 * @return 启用返回 true
 */
bool XImageWriter_optimizedWrite(const XImageWriter* self);

/**
 * @brief      设置是否启用渐进式扫描写入
 * @param self        目标 XImageWriter 对象指针
 * @param progressive 启用渐进式扫描
 */
void XImageWriter_setProgressiveScanWrite(XImageWriter* self, bool progressive);

/**
 * @brief      查询是否启用渐进式扫描写入
 * @param self 目标 XImageWriter 对象指针
 * @return 启用返回 true
 */
bool XImageWriter_progressiveScanWrite(const XImageWriter* self);

/**
 * @brief      获取变换类型
 * @param self 目标 XImageWriter 对象指针
 * @return 变换类型
 */
XImageIOHandlerTransformation XImageWriter_transformation(const XImageWriter* self);

/**
 * @brief      设置变换类型
 * @param self        目标 XImageWriter 对象指针
 * @param orientation 变换类型
 */
void XImageWriter_setTransformation(XImageWriter* self, XImageIOHandlerTransformation orientation);

/**
 * @brief      设置文本元数据
 * @param self 目标 XImageWriter 对象指针
 * @param key  文本键
 * @param text 文本值
 */
void XImageWriter_setText(XImageWriter* self, const XString* key, const XString* text);

/**
 * @brief 使用 UTF-8 键和值设置文本元数据。
 * @param self 目标写入器对象指针。
 * @param key 文本键；Qt 语义会先去除首尾空白并合并内部空白。
 * @param text 文本值；Qt 语义会先去除首尾空白并合并内部空白。
 * @note 多次调用会以两个换行符拼接为 Description 选项；最终是否写入由
 *       当前图像处理器的 Description 能力决定。
 */
void XImageWriter_setText_2(XImageWriter* self, const char* key, const char* text);

/* ========== 写入操作 ========== */

/**
 * @brief      判断是否可以写入
 * @param self 目标 XImageWriter 对象指针
 * @return 可以写入返回 true
 */
bool XImageWriter_canWrite(const XImageWriter* self);

/**
 * @brief      写入图像
 * @param self  目标 XImageWriter 对象指针
 * @param image 待写入的图像指针
 * @return 写入成功返回 true
 */
bool XImageWriter_write(XImageWriter* self, const XImage* image);

/* ========== 错误处理 ========== */

/**
 * @brief      获取错误码
 * @param self 目标 XImageWriter 对象指针
 * @return 错误码枚举
 */
XImageWriterError XImageWriter_error(const XImageWriter* self);

/**
 * @brief      获取错误描述
 * @param self 目标 XImageWriter 对象指针
 * @return 错误描述字符串
 */
XString* XImageWriter_errorString(const XImageWriter* self);

/** @brief 获取内部错误描述引用。 */
const XString* XImageWriter_errorString_const(const XImageWriter* self);

/** @brief 获取 UTF-8 兼容错误描述；返回值由内部 XString 缓存持有。 */
const char* XImageWriter_errorString_2(const XImageWriter* self);

/**
 * @brief      判断是否支持指定选项
 * @param self   目标 XImageWriter 对象指针
 * @param option 图像选项枚举
 * @return 支持返回 true
 */
bool XImageWriter_supportsOption(const XImageWriter* self, XImageIOHandlerOption option);

/* ========== 静态方法 ========== */

/**
 * @brief      获取支持的图像格式列表
 * @return 新分配的 XStringList（元素为 XString）；调用者负责使用
 *         XStringList_delete_base 释放列表，元素由列表拥有
 */
XStringList* XImageWriter_supportedImageFormats();

/**
 * @brief      获取支持的 MIME 类型列表
 * @return 新分配的 XStringList（元素为 XString）；调用者负责释放列表
 */
XStringList* XImageWriter_supportedMimeTypes();

/**
 * @brief      获取指定 MIME 类型对应的图像格式列表
 * @param mimeType MIME 类型字符串
 * @return 新分配的 XStringList（元素为 XString）；调用者负责释放列表
 */
XStringList* XImageWriter_imageFormatsForMimeType(const XString* mimeType);
/**
 * @brief 使用 UTF-8 MIME 类型查询可写格式列表的兼容重载。
 * @param mimeType UTF-8 编码的 MIME 类型。
 * @return 新建的 XStringList；调用方负责使用 XStringList_delete_base 释放。
 */
XStringList* XImageWriter_imageFormatsForMimeType_2(const char* mimeType);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XImageWriter_create
#define XImageWriter_create() XImageWriter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XIMAGEWRITER_H */
