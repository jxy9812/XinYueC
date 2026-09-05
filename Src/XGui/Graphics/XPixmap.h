/******************************************************************************
 * @file       XPixmap.h
 * @brief      XPixmap 像素图类（对标 Qt 6.8 QPixmap）
 * @author     XinYueC 团队
 * @note       提供屏幕优化的像素图显示，支持从文件加载、缩放、变换、掩码等操作
 ******************************************************************************/
#ifndef XPIXMAP_H
#define XPIXMAP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XImage.h"
#include "XImageFormat.h"
#include "XClass.h"
#include "XTypes.h"


/* ========== XPixmap 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XPixmap)
XCLASS_DEFINE_EXTEND_END(XPixmap, XClass)

/* 前向声明 */
typedef struct XPlatformPixmap XPlatformPixmap;
typedef struct XBitmap XBitmap;
typedef struct XImageReader XImageReader;

/**
 * @brief XPixmap 图像转换标志（数值对齐 Qt 6.8 Qt::ImageConversionFlag）。
 * @note NoOpaqueDetection 禁止把实际全不透明的输入降为 RGB32；
 *       NoFormatConversion 保留输入格式。其余颜色/抖动标志由 XImage
 *       转换层解释或按当前后端能力处理。
 */
typedef enum XPixmapImageConversionFlag
{
    XPixmapImageConversion_NoOpaqueDetection = 0x00000100,
    XPixmapImageConversion_NoFormatConversion = 0x00000200
} XPixmapImageConversionFlag;

/**
 * @brief      XPixmap 像素图类结构体（对标 Qt 6.8 QPixmap）
 * @note       继承自 XClass，提供屏幕优化的像素图显示功能
 */
typedef struct XPixmap
{
    XClass          m_class;       /**< 继承的基类成员 */
    XPlatformPixmap* m_data;        /**< 平台像素图私有数据指针（引用计数管理） */
}XPixmap;

/**
 * @brief      初始化 XPixmap 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XPixmap_class_init();

/**
 * @brief      在堆上创建 XPixmap 实例
 * @return     指向新创建的 XPixmap 对象的指针，失败返回 NULL
 */
XPixmap* XPixmap_create_ex(XMemoryType memory);

/**
 * @brief      初始化 XPixmap 实例（创建空像素图）
 * @param self 待初始化的 XPixmap 对象指针
 */
void XPixmap_init(XPixmap* self);

/**
 * @brief      使用指定宽度、高度创建像素图
 * @param self   待初始化的 XPixmap 对象指针
 * @param width  像素图宽度
 * @param height 像素图高度
 */
void XPixmap_init_ex(XPixmap* self, int width, int height);

/**
 * @brief      使用指定尺寸创建像素图
 * @param self 待初始化的 XPixmap 对象指针
 * @param size 尺寸结构体指针
 */
void XPixmap_init_size(XPixmap* self, const XSize* size);

/**
 * @brief      从文件加载像素图
 * @param self     待初始化的 XPixmap 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示自动检测）
 * @param flags    转换标志
 */
void XPixmap_init_file(XPixmap* self, const XString* fileName, const XString* format, uint32_t flags);
/**
 * @brief 使用 UTF-8 文件名和格式初始化像素图的兼容重载。
 * @param self 待初始化的像素图对象指针。
 * @param fileName UTF-8 编码的文件名；可为 NULL。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @param flags 图像转换标志。
 */
void XPixmap_init_file_2(XPixmap* self, const char* fileName, const char* format, uint32_t flags);

/**
 * @brief      从 XImage 创建像素图
 * @param self   待初始化的 XPixmap 对象指针；调用前应为未初始化对象，
 *               或已通过 XPixmap_deinit_base() 释放的平台数据
 * @param image  源 XImage 对象指针
 * @param flags  转换标志
 */
void XPixmap_init_image(XPixmap* self, const XImage* image, uint32_t flags);

/** @internal Constructs a pixmap backed by a one-bit bitmap image. */
void XPixmap_init_bitmap_image(XPixmap* self, const XImage* image, uint32_t flags);

/**
 * @brief      复制构造函数
 * @param self 目标 XPixmap 对象指针
 * @param other 源 XPixmap 对象指针
 */

/**
 * @brief      移动构造函数
 * @param self 目标 XPixmap 对象指针
 * @param other 源 XPixmap 对象指针（移动后源对象变为空）
 */

/**
 * @brief      释放 XPixmap 资源
 * @param self 待释放的 XPixmap 对象指针
 */
/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
/** @brief 通过 XClass 虚表释放像素图资源。 @param self 待释放的像素图指针。 */
#define XPixmap_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
/** @brief 删除堆上的像素图对象。 @param self 待删除的像素图指针。 */
#define XPixmap_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 查询方法 ========== */

/**
 * @brief      判断像素图是否为 null
 * @param self 目标 XPixmap 对象指针
 * @return 像素图为 null 返回 true，否则返回 false
 */
bool XPixmap_isNull(const XPixmap* self);

/**
 * @brief      获取像素图宽度
 * @param self 目标 XPixmap 对象指针
 * @return 宽度（像素）
 */
int XPixmap_width(const XPixmap* self);

/**
 * @brief      获取像素图高度
 * @param self 目标 XPixmap 对象指针
 * @return 高度（像素）
 */
int XPixmap_height(const XPixmap* self);

/**
 * @brief      获取像素图尺寸
 * @param self 目标 XPixmap 对象指针
 * @param out  输出尺寸结构体指针
 */
void XPixmap_size(const XPixmap* self, XSize* out);

/**
 * @brief      获取像素图矩形区域
 * @param self 目标 XPixmap 对象指针
 * @param out  输出矩形结构体指针
 */
void XPixmap_rect(const XPixmap* self, XRect* out);

/**
 * @brief      获取像素图位深度
 * @param self 目标 XPixmap 对象指针
 * @return 位深度
 */
int XPixmap_depth(const XPixmap* self);

/**
 * @brief      获取默认显示深度
 * @return 默认显示深度（像素位数）
 */
int XPixmap_defaultDepth();

/* ========== 填充与掩码 ========== */

/**
 * @brief      使用指定颜色填充像素图
 * @param self  目标 XPixmap 对象指针
 * @param color ARGB 颜色值（0xAARRGGBB，默认白色）
 */
void XPixmap_fill(XPixmap* self, uint32_t color);

/**
 * @brief      获取掩码位图
 * @param self 目标 XPixmap 对象指针
 * @param out  输出掩码位图指针
 */
void XPixmap_mask(const XPixmap* self, XBitmap* out);

/** @brief 返回像素图掩码的旧 XPixmap 兼容版本。 */
void XPixmap_mask_2(const XPixmap* self, XPixmap* out);

/**
 * @brief 获取单色位图掩码（对齐 QPixmap::mask()）。
 * @param self 源像素图
 * @param out 输出 XBitmap；调用者负责初始化或反初始化已有对象
 */
void XPixmap_maskBitmap(const XPixmap* self, XBitmap* out);

/**
 * @brief      设置掩码位图
 * @param self 目标 XPixmap 对象指针
 * @param mask 掩码位图指针
 */
void XPixmap_setMask(XPixmap* self, const XPixmap* mask);

/**
 * @brief      判断像素图是否有 Alpha 通道
 * @param self 目标 XPixmap 对象指针
 * @return 有 Alpha 通道返回 true
 */
bool XPixmap_hasAlpha(const XPixmap* self);

/**
 * @brief      判断像素图是否实际使用了 Alpha 通道
 * @param self 目标 XPixmap 对象指针
 * @return 使用了 Alpha 通道返回 true
 */
bool XPixmap_hasAlphaChannel(const XPixmap* self);

/**
 * @brief      从颜色创建启发式掩码
 * @param self      目标 XPixmap 对象指针
 * @param clipTight 是否紧密裁剪
 * @param out       输出掩码位图指针
 */
void XPixmap_createHeuristicMask(const XPixmap* self, bool clipTight, XPixmap* out);

/**
 * @brief      从指定颜色创建掩码
 * @param self      目标 XPixmap 对象指针
 * @param maskColor 掩码颜色值
 * @param mode      掩码模式
 * @param out       输出掩码位图指针
 */
void XPixmap_createMaskFromColor(const XPixmap* self, uint32_t maskColor, uint32_t mode, XPixmap* out);

/* ========== 缩放与变换 ========== */

/**
 * @brief      缩放像素图
 * @param self    目标 XPixmap 对象指针
 * @param width   目标宽度
 * @param height  目标高度
 * @param aspectMode 宽高比模式
 * @param mode    变换模式
 * @param out     输出结果像素图指针
 */
void XPixmap_scaled(const XPixmap* self, int width, int height, uint32_t aspectMode, uint32_t mode, XPixmap* out);

/**
 * @brief      根据宽度等比缩放
 * @param self  目标 XPixmap 对象指针
 * @param width 目标宽度
 * @param mode  变换模式
 * @param out   输出结果像素图指针
 */
void XPixmap_scaledToWidth(const XPixmap* self, int width, uint32_t mode, XPixmap* out);

/**
 * @brief      根据高度等比缩放
 * @param self   目标 XPixmap 对象指针
 * @param height 目标高度
 * @param mode   变换模式
 * @param out    输出结果像素图指针
 */
void XPixmap_scaledToHeight(const XPixmap* self, int height, uint32_t mode, XPixmap* out);

/**
 * @brief      使用变换矩阵变换像素图
 * @param self    目标 XPixmap 对象指针
 * @param m00-m23 变换矩阵参数
 * @param mode    变换模式
 * @param out     输出结果像素图指针
 */
void XPixmap_transformed(const XPixmap* self, float m00, float m01, float m02,
                         float m10, float m11, float m12, uint32_t mode, XPixmap* out);

/**
 * @brief 计算变换后包围盒所需的真实变换矩阵（对齐 QPixmap::trueMatrix）。
 * @param matrix 输入二维仿射矩阵
 * @param width 源宽度
 * @param height 源高度
 * @param out 输出平移修正后的矩阵
 */
void XPixmap_trueMatrix(const XImageTransform* matrix, int width, int height,
                        XImageTransform* out);

/** @brief 使用六参数仿射矩阵的兼容重载。 */
void XPixmap_trueMatrix_2(float m00, float m01, float m02, float m10, float m11,
                          float m12, int width, int height, XImageTransform* out);

/* ========== 转换方法 ========== */

/**
 * @brief      将像素图转换为 XImage
 * @param self 目标 XPixmap 对象指针
 * @param out  输出图像指针
 */
void XPixmap_toImage(const XPixmap* self, XImage* out);

/**
 * @brief      从 XImage 创建静态像素图
 * @param image 源 XImage 对象指针
 * @param flags 转换标志
 * @param out   输出像素图指针
 */
void XPixmap_fromImage(const XImage* image, uint32_t flags, XPixmap* out);

/**
 * @brief      从 XImageReader 创建静态像素图
 * @param reader 图像读取器指针
 * @param flags  转换标志
 * @param out    输出像素图指针
 */
void XPixmap_fromImageReader(XImageReader* reader, uint32_t flags, XPixmap* out);

/** @brief 将图像转换到现有像素图；失败时保留原像素图。 */
bool XPixmap_convertFromImage(XPixmap* self, const XImage* image, uint32_t flags);

/** @brief 交换两个像素图的数据所有权，支持自交换。 */
void XPixmap_swap(XPixmap* self, XPixmap* other);

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载像素图
 * @param self     目标 XPixmap 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示自动检测）
 * @param flags    转换标志
 * @return 加载成功返回 true，失败返回 false
 */
bool XPixmap_load(XPixmap* self, const XString* fileName, const XString* format, uint32_t flags);
/**
 * @brief 使用 UTF-8 文件名和格式加载像素图的兼容重载。
 * @param self 目标像素图对象指针。
 * @param fileName UTF-8 编码的文件名。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @param flags 图像转换标志。
 * @return 加载成功返回 true，失败返回 false。
 */
bool XPixmap_load_2(XPixmap* self, const char* fileName, const char* format, uint32_t flags);

/**
 * @brief      从内存数据加载像素图
 * @param self   目标 XPixmap 对象指针
 * @param buf    数据缓冲区指针
 * @param len    数据长度
 * @param format 图像格式字符串（NULL 表示自动检测）
 * @param flags  转换标志
 * @return 加载成功返回 true，失败返回 false
 */
bool XPixmap_loadFromData(XPixmap* self, const uint8_t* buf, uint32_t len, const XString* format, uint32_t flags);
/**
 * @brief 使用 UTF-8 格式名从内存数据加载像素图的兼容重载。
 * @param self 目标像素图对象指针。
 * @param buf 图像数据缓冲区。
 * @param len 数据字节数。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @param flags 图像转换标志。
 * @return 加载成功返回 true，失败返回 false。
 */
bool XPixmap_loadFromData_2(XPixmap* self, const uint8_t* buf, uint32_t len, const char* format, uint32_t flags);

/**
 * @brief      保存像素图到文件
 * @param self     目标 XPixmap 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示从扩展名自动检测）
 * @param quality  质量参数（-1 表示默认）
 * @return 保存成功返回 true，失败返回 false
 */
bool XPixmap_save(const XPixmap* self, const XString* fileName, const XString* format, int quality);
/**
 * @brief 使用 UTF-8 文件名和格式保存像素图的兼容重载。
 * @param self 源像素图对象指针。
 * @param fileName UTF-8 编码的目标文件名。
 * @param format UTF-8 编码的格式名；可为 NULL 以按扩展名判断。
 * @param quality 编码质量，-1 表示默认值。
 * @return 保存成功返回 true，失败返回 false。
 */
bool XPixmap_save_2(const XPixmap* self, const char* fileName, const char* format, int quality);
/**
 * @brief 从 XIODevice 读取像素图；设备由调用方持有。
 * @param self 目标像素图对象指针。
 * @param device 输入设备指针。
 * @param format XString 格式名；可为 NULL 以自动检测。
 * @param flags 图像转换标志。
 * @return 读取成功返回 true，失败返回 false。
 */
bool XPixmap_loadDevice(XPixmap* self, XIODevice* device, const XString* format, uint32_t flags);
/**
 * @brief 使用 UTF-8 格式名从 XIODevice 读取像素图的兼容重载。
 * @param self 目标像素图对象指针。
 * @param device 输入设备指针。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 * @param flags 图像转换标志。
 * @return 读取成功返回 true，失败返回 false。
 */
bool XPixmap_loadDevice_2(XPixmap* self, XIODevice* device, const char* format, uint32_t flags);
/**
 * @brief 将像素图写入 XIODevice；设备由调用方持有。
 * @param self 源像素图对象指针。
 * @param device 输出设备指针。
 * @param format XString 格式名；可为 NULL 以自动判断。
 * @param quality 编码质量，-1 表示默认值。
 * @return 写入成功返回 true，失败返回 false。
 */
bool XPixmap_saveDevice(const XPixmap* self, XIODevice* device, const XString* format, int quality);
/**
 * @brief 使用 UTF-8 格式名将像素图写入 XIODevice 的兼容重载。
 * @param self 源像素图对象指针。
 * @param device 输出设备指针。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动判断。
 * @param quality 编码质量，-1 表示默认值。
 * @return 写入成功返回 true，失败返回 false。
 */
bool XPixmap_saveDevice_2(const XPixmap* self, XIODevice* device, const char* format, int quality);
/**
 * @brief 获取像素图后端设备类型。
 * @param self 像素图对象指针。
 * @return 后端设备类型编号；空像素图返回 0。
 */
int XPixmap_devType(const XPixmap* self);
/**
 * @brief 获取像素图绘制引擎句柄。
 * @param self 像素图对象指针。
 * @return 绘制引擎句柄；当前无原生绘制引擎时返回 NULL。
 */
void* XPixmap_paintEngine(const XPixmap* self);

/* ========== 其他 ========== */

/**
 * @brief      复制像素图指定区域
 * @param self   目标 XPixmap 对象指针
 * @param rect   要复制的矩形区域（NULL 表示复制整个像素图）
 * @param out    输出结果像素图指针
 */
void XPixmap_copyRect(const XPixmap* self, const XRect* rect, XPixmap* out);

/**
 * @brief      滚动像素图内容
 * @param self     目标 XPixmap 对象指针
 * @param dx       X 方向偏移
 * @param dy       Y 方向偏移
 * @param rect     滚动区域
 * @param exposed  暴露区域输出（可为 NULL）
 */
void XPixmap_scroll(XPixmap* self, int dx, int dy, const XRect* rect, XRegion* exposed);

/**
 * @brief      获取缓存键值
 * @param self 目标 XPixmap 对象指针
 * @return 缓存键值（64 位）
 */
int64_t XPixmap_cacheKey(const XPixmap* self);

/**
 * @brief      判断数据是否已分离
 * @param self 目标 XPixmap 对象指针
 * @return 已分离返回 true
 */
bool XPixmap_isDetached(const XPixmap* self);

/**
 * @brief      分离数据（写时复制）
 * @param self 目标 XPixmap 对象指针
 */
void XPixmap_detach(XPixmap* self);

/**
 * @brief      判断是否为位图（单色）
 * @param self 目标 XPixmap 对象指针
 * @return 是位图返回 true
 */
bool XPixmap_isQBitmap(const XPixmap* self);

/**
 * @brief      获取设备像素比
 * @param self 目标 XPixmap 对象指针
 * @return 设备像素比
 */
float XPixmap_devicePixelRatio(const XPixmap* self);

/**
 * @brief      设置设备像素比
 * @param self         目标 XPixmap 对象指针
 * @param scaleFactor  设备像素比；Qt 允许零和负值，接口按原值保存
 */
void XPixmap_setDevicePixelRatio(XPixmap* self, float scaleFactor);

/**
 * @brief      获取与设备无关的尺寸
 * @param self 目标 XPixmap 对象指针
 * @param out  输出尺寸结构体指针
 * @return 无返回值；空像素图输出 (0,0)，其余情况为像素尺寸除以设备像素比
 */
void XPixmap_deviceIndependentSize(const XPixmap* self, XSizeF* out);

/**
 * @brief      从 XImage 就地转换（内部使用）
 * @param image 源 XImage 对象指针
 * @param flags 转换标志
 * @param out   输出像素图指针
 */
void XPixmap_fromImageInPlace(XImage* image, uint32_t flags, XPixmap* out);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XPixmap_create
#define XPixmap_create() XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XPIXMAP_H */
