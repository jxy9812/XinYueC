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
#include "XImageFormat.h"
#include "XGuiTypes.h"
#include "XClass.h"
#include "XTypes.h"
#include "XMemory.h"


/* ========== XImage 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XImage)
XCLASS_DEFINE_EXTEND_END(XImage, XClass)
/* 前向声明 */
typedef struct XImageData XImageData;

/**
 * @brief      XImage 图像类结构体（对标 Qt 6.8 QImage）
 * @note       继承自 XObject，提供像素级图像数据访问和处理接口
 */
typedef struct XImage
{
    XClass    m_class;       /**< 继承的基类成员 */
    XImageData* m_data;       /**< 图像数据私有指针（内部引用计数管理） */
}XImage;

/**
 * @brief      初始化 XImage 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XImage_class_init();

/**
 * @brief      在堆上创建 XImage 实例
 * @return     指向新创建的 XImage 对象的指针，失败返回 NULL
 */
XImage* XImage_create();

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
void XImage_init_file(XImage* self, const char* fileName, const char* format);

/**
 * @brief      复制构造函数
 * @param self 目标 XImage 对象指针
 * @param other 源 XImage 对象指针
 */
void XImage_copy(XImage* self, const XImage* other);

/**
 * @brief      移动构造函数
 * @param self 目标 XImage 对象指针
 * @param other 源 XImage 对象指针（移动后源对象变为空）
 */
void XImage_move(XImage* self, XImage* other);

/**
 * @brief      释放 XImage 资源
 * @param self 待释放的 XImage 对象指针
 */
void XImage_deinit(XImage* self);

/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
void XImage_copy_base(XImage* dest, const XImage* src);

/**
 * @brief      虚函数调度：移动
 * @param dest 目标对象指针
 * @param src  源对象指针（移动后源对象变为空）
 */
void XImage_move_base(XImage* dest, XImage* src);

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
void XImage_deinit_base(XImage* self);

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
void XImage_delete_base(XImage* self);

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

/**
 * @brief      获取图像格式
 * @param self 目标 XImage 对象指针
 * @return 像素格式枚举值
 */
XImageFormat XImage_format(const XImage* self);

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
 * @param index    颜色索引
 * @param color    ARGB 颜色值（0xAARRGGBB）
 */
void XImage_setColor(XImage* self, int index, uint32_t color);

/**
 * @brief      将所有颜色表的颜色值设置为指定颜色
 * @param self     目标 XImage 对象指针
 * @param color    ARGB 颜色值
 */
void XImage_fill(XImage* self, uint32_t color);

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

/**
 * @brief      填充矩形区域
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
 * @return 像素索引值
 */
int XImage_pixelIndex(const XImage* self, int x, int y);

/**
 * @brief      获取指定坐标的 ARGB 颜色值
 * @param self 目标 XImage 对象指针
 * @param x    像素 x 坐标
 * @param y    像素 y 坐标
 * @return ARGB 颜色值（0xAARRGGBB）
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

/* ========== 文件操作 ========== */

/**
 * @brief      从文件加载图像
 * @param self     目标 XImage 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示自动检测）
 * @return 加载成功返回 true，失败返回 false
 */
bool XImage_load(XImage* self, const char* fileName, const char* format);

/**
 * @brief      从内存数据加载图像
 * @param self   目标 XImage 对象指针
 * @param data   图像数据指针
 * @param len    数据长度
 * @param format 图像格式字符串（NULL 表示自动检测）
 * @return 加载成功返回 true，失败返回 false
 */
bool XImage_loadFromData(XImage* self, const uint8_t* data, int len, const char* format);

/**
 * @brief      保存图像到文件
 * @param self     目标 XImage 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示从扩展名自动检测）
 * @param quality  质量参数（-1 表示默认）
 * @return 保存成功返回 true，失败返回 false
 */
bool XImage_save(const XImage* self, const char* fileName, const char* format, int quality);

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
 * @brief      设置设备像素比
 * @param self 目标 XImage 对象指针
 * @param scaleFactor 正数设备像素比
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
void XImage_fromData(const uint8_t* data, int size, const char* format, XImage* out);

/**
 * @brief      将图像转换为指定格式的像素数据
 * @param format 像素格式
 * @return 像素格式描述
 */
const char* XImage_formatToStr(XImageFormat format);

#ifdef __cplusplus
}
#endif
#endif /* XIMAGE_H */

