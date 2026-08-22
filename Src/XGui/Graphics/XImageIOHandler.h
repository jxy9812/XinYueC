/******************************************************************************
 * @file       XImageIOHandler.h
 * @brief      XImageIOHandler 图像输入输出处理器基类（对标 Qt 6.8 QImageIOHandler）
 * @author     XinYueC 团队
 * @note       提供图像格式插件的抽象基类，子类实现具体格式的读写操作
 ******************************************************************************/
#ifndef XIMAGEIOHANDLER_H
#define XIMAGEIOHANDLER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XImage.h"
#include "XImageFormat.h"
#include "XClass.h"
#include "XIODevice.h"
#include "XString.h"

/**
 * @brief      XImageIOHandler 图像选项枚举（对标 Qt 6.8 QImageIOHandler::ImageOption）
 */
typedef enum XImageIOHandlerOption
{
    XImageIOHandlerOption_Size,                /**< 图像尺寸 */
    XImageIOHandlerOption_ClipRect,            /**< 裁剪矩形 */
    XImageIOHandlerOption_Description,         /**< 图像描述 */
    XImageIOHandlerOption_ScaledClipRect,      /**< 缩放后的裁剪矩形 */
    XImageIOHandlerOption_ScaledSize,          /**< 缩放后的尺寸 */
    XImageIOHandlerOption_CompressionRatio,    /**< 压缩比 */
    XImageIOHandlerOption_Gamma,               /**< Gamma 值 */
    XImageIOHandlerOption_Quality,             /**< 质量 */
    XImageIOHandlerOption_Name,                /**< 名称 */
    XImageIOHandlerOption_SubType,             /**< 子类型 */
    XImageIOHandlerOption_IncrementalReading,  /**< 增量读取 */
    XImageIOHandlerOption_Endianness,          /**< 字节序 */
    XImageIOHandlerOption_Animation,           /**< 动画 */
    XImageIOHandlerOption_BackgroundColor,     /**< 背景色 */
    XImageIOHandlerOption_ImageFormat,         /**< 图像格式 */
    XImageIOHandlerOption_SupportedSubTypes,   /**< 支持的子类型 */
    XImageIOHandlerOption_OptimizedWrite,      /**< 优化写入 */
    XImageIOHandlerOption_ProgressiveScanWrite,/**< 渐进式扫描写入 */
    XImageIOHandlerOption_ImageTransformation  /**< 图像变换 */
} XImageIOHandlerOption;

/**
 * @brief      XImageIOHandler 变换枚举（对标 Qt 6.8 QImageIOHandler::Transformation）
 */
typedef enum XImageIOHandlerTransformation
{
    XImageIOHandlerTransformation_None             = 0,   /**< 无变换 */
    XImageIOHandlerTransformation_Mirror           = 1,   /**< 镜像 */
    XImageIOHandlerTransformation_Flip             = 2,   /**< 翻转 */
    XImageIOHandlerTransformation_Rotate180        = 3,   /**< 旋转 180 度 */
    XImageIOHandlerTransformation_Rotate90         = 4,   /**< 旋转 90 度 */
    XImageIOHandlerTransformation_MirrorAndRotate90 = 5,  /**< 镜像并旋转 90 度 */
    XImageIOHandlerTransformation_FlipAndRotate90  = 6,   /**< 翻转并旋转 90 度 */
    XImageIOHandlerTransformation_Rotate270        = 7    /**< 旋转 270 度 */
} XImageIOHandlerTransformation;

/**
 * @brief 图像处理器选项的可移植 C 值容器。
 * @note option/setOption 的 out/value 参数按选项使用对应成员；未知选项返回 false。
 */
typedef struct XImageIOHandlerOptionValue
{
    XSize size; /**< 尺寸选项值。 */
    XRect rect; /**< 矩形选项值。 */
    uint32_t color; /**< ARGB32 颜色选项值。 */
    int integer; /**< 整数选项值。 */
    float real; /**< 浮点选项值。 */
    bool boolean; /**< 布尔选项值。 */
    XImageFormat format; /**< 图像格式选项值。 */
    XImageIOHandlerTransformation transformation; /**< 变换选项值。 */
    const XString* string; /**< 由调用方持有的字符串对象，不转移所有权。 */
} XImageIOHandlerOptionValue;

/* 前向声明 */
typedef struct XImageIOHandlerPrivate XImageIOHandlerPrivate;

/**
 * @brief      XImageIOHandler 图像 IO 处理器类结构体（对标 Qt 6.8 QImageIOHandler）
 * @note       继承自 XClass，作为图像格式插件的抽象基类
 */
typedef struct XImageIOHandler
{
    XClass                     m_class;   /**< 继承的基类成员 */
    XImageIOHandlerPrivate*     m_data;    /**< 私有数据指针 */
}XImageIOHandler;

/**
 * @brief      XImageIOHandler 虚函数表枚举
 */
XCLASS_DEFINE_BEGING(XImageIOHandler)
XCLASS_DEFINE_ENUM(XImageIOHandler, CanRead) = XCLASS_VTABLE_GET_SIZE(XClass),  /**< 能否读取 */
XCLASS_DEFINE_ENUM(XImageIOHandler, Read),                                       /**< 读取图像 */
XCLASS_DEFINE_ENUM(XImageIOHandler, Write),                                      /**< 写入图像 */
XCLASS_DEFINE_ENUM(XImageIOHandler, Option),                                     /**< 获取选项 */
XCLASS_DEFINE_ENUM(XImageIOHandler, SetOption),                                  /**< 设置选项 */
XCLASS_DEFINE_ENUM(XImageIOHandler, SupportsOption),                             /**< 是否支持选项 */
XCLASS_DEFINE_ENUM(XImageIOHandler, JumpToNextImage),                            /**< 跳转到下一帧 */
XCLASS_DEFINE_ENUM(XImageIOHandler, JumpToImage),                                /**< 跳转到指定帧 */
XCLASS_DEFINE_ENUM(XImageIOHandler, LoopCount),                                  /**< 循环次数 */
XCLASS_DEFINE_ENUM(XImageIOHandler, ImageCount),                                 /**< 帧数 */
XCLASS_DEFINE_ENUM(XImageIOHandler, NextImageDelay),                             /**< 下一帧延迟 */
XCLASS_DEFINE_ENUM(XImageIOHandler, CurrentImageNumber),                         /**< 当前帧编号 */
XCLASS_DEFINE_ENUM(XImageIOHandler, CurrentImageRect),                           /**< 当前帧矩形 */
XCLASS_DEFINE_END(XImageIOHandler)

/**
 * @brief      初始化 XImageIOHandler 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XImageIOHandler_class_init();

/**
 * @brief      在堆上创建 XImageIOHandler 实例
 * @return     指向新创建的 XImageIOHandler 对象的指针，失败返回 NULL
 */
XImageIOHandler* XImageIOHandler_create_ex(XMemoryType memory);

/**
 * @brief      初始化 XImageIOHandler 实例
 * @param self 待初始化的 XImageIOHandler 对象指针
 */
void XImageIOHandler_init(XImageIOHandler* self);

/**
 * @brief      释放 XImageIOHandler 资源
 * @param self 待释放的 XImageIOHandler 对象指针
 */
/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放处理器资源。 @param self 待释放的处理器指针。 */
#define XImageIOHandler_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的图像处理器。 @param self 待删除的处理器指针。 */
#define XImageIOHandler_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 设备管理 ========== */

/**
 * @brief      设置 IO 设备
 * @param self   目标 XImageIOHandler 对象指针
 * @param device XIODevice 指针
 */
void XImageIOHandler_setDevice(XImageIOHandler* self, XIODevice* device);

/**
 * @brief      获取 IO 设备
 * @param self 目标 XImageIOHandler 对象指针
 * @return XIODevice 指针
 */
XIODevice* XImageIOHandler_device(const XImageIOHandler* self);

/**
 * @brief      设置图像格式
 * @param self   目标 XImageIOHandler 对象指针
 * @param format 格式字符串
 */
void XImageIOHandler_setFormat(XImageIOHandler* self, const XString* format);
/**
 * @brief 使用 UTF-8 格式名设置图像格式的兼容重载。
 * @param self 目标处理器对象指针。
 * @param format UTF-8 编码的格式名；可为 NULL。
 */
void XImageIOHandler_setFormat_2(XImageIOHandler* self, const char* format);

/**
 * @brief      获取图像格式
 * @param self 目标 XImageIOHandler 对象指针
 * @return 格式字符串
 */
XString* XImageIOHandler_format(const XImageIOHandler* self);
/**
 * @brief 获取图像格式的内部只读引用。
 * @param self 处理器对象指针。
 * @return 内部 XString 引用；对象无格式时返回 NULL，不得释放。
 */
const XString* XImageIOHandler_format_const(const XImageIOHandler* self);
/**
 * @brief 获取图像格式的 UTF-8 兼容指针。
 * @param self 处理器对象指针。
 * @return UTF-8 格式名指针，由内部缓存持有，不得释放。
 */
const char* XImageIOHandler_format_2(const XImageIOHandler* self);

/* ========== 虚函数（供子类重载） ========== */

/**
 * @brief      虚函数：判断是否可以读取（纯虚）
 * @param self 目标 XImageIOHandler 对象指针
 * @return 可以读取返回 true
 */
bool XImageIOHandler_canRead_base(const XImageIOHandler* self);

/**
 * @brief      虚函数：读取图像到 XImage（纯虚）
 * @param self  目标 XImageIOHandler 对象指针
 * @param image 输出图像指针
 * @return 读取成功返回 true
 */
bool XImageIOHandler_read_base(XImageIOHandler* self, XImage* image);

/**
 * @brief      虚函数：写入图像
 * @param self  目标 XImageIOHandler 对象指针
 * @param image 待写入的图像指针
 * @return 写入成功返回 true
 */
bool XImageIOHandler_write_base(XImageIOHandler* self, const XImage* image);

/**
 * @brief      虚函数：获取图像选项值
 * @param self   目标 XImageIOHandler 对象指针
 * @param option 图像选项枚举
 * @param out    输出值指针
 * @return 获取成功返回 true
 */
bool XImageIOHandler_option_base(const XImageIOHandler* self, XImageIOHandlerOption option, void* out);

/**
 * @brief      虚函数：设置图像选项值
 * @param self   目标 XImageIOHandler 对象指针
 * @param option 图像选项枚举
 * @param value  值指针
 */
void XImageIOHandler_setOption_base(XImageIOHandler* self, XImageIOHandlerOption option, const void* value);

/**
 * @brief      虚函数：判断是否支持指定选项
 * @param self   目标 XImageIOHandler 对象指针
 * @param option 图像选项枚举
 * @return 支持返回 true
 */
bool XImageIOHandler_supportsOption_base(const XImageIOHandler* self, XImageIOHandlerOption option);

/** @brief 获取基类保存的选项值，适用于未被子类重载的处理器。 */
bool XImageIOHandler_optionValue(const XImageIOHandler* self,
                                 XImageIOHandlerOption option,
                                 XImageIOHandlerOptionValue* out);

/**
 * @brief      虚函数：跳转到下一帧
 * @param self 目标 XImageIOHandler 对象指针
 * @return 跳转成功返回 true
 */
bool XImageIOHandler_jumpToNextImage_base(XImageIOHandler* self);

/**
 * @brief      虚函数：跳转到指定帧
 * @param self        目标 XImageIOHandler 对象指针
 * @param imageNumber 帧编号
 * @return 跳转成功返回 true
 */
bool XImageIOHandler_jumpToImage_base(XImageIOHandler* self, int imageNumber);

/**
 * @brief      虚函数：获取循环次数
 * @param self 目标 XImageIOHandler 对象指针
 * @return 循环次数
 */
int XImageIOHandler_loopCount_base(const XImageIOHandler* self);

/**
 * @brief      虚函数：获取帧数
 * @param self 目标 XImageIOHandler 对象指针
 * @return 帧数
 */
int XImageIOHandler_imageCount_base(const XImageIOHandler* self);

/**
 * @brief      虚函数：获取下一帧延迟（毫秒）
 * @param self 目标 XImageIOHandler 对象指针
 * @return 延迟毫秒数
 */
int XImageIOHandler_nextImageDelay_base(const XImageIOHandler* self);

/**
 * @brief      虚函数：获取当前帧编号
 * @param self 目标 XImageIOHandler 对象指针
 * @return 当前帧编号
 */
int XImageIOHandler_currentImageNumber_base(const XImageIOHandler* self);

/**
 * @brief      虚函数：获取当前帧矩形
 * @param self 目标 XImageIOHandler 对象指针
 * @param out  输出矩形指针
 */
void XImageIOHandler_currentImageRect_base(const XImageIOHandler* self, XRect* out);

/* ========== 静态工具 ========== */

/**
 * @brief      分配图像内存
 * @param size   尺寸指针
 * @param format 像素格式
 * @param image  输出图像指针
 * @return 分配成功返回 true
 */
bool XImageIOHandler_allocateImage(const XSize* size, XImageFormat format, XImage* image);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XImageIOHandler_create
#define XImageIOHandler_create() XImageIOHandler_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XIMAGEIOHANDLER_H */
