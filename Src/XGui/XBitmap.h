/******************************************************************************
 * @file       XBitmap.h
 * @brief      XBitmap 单色位图类（对标 Qt 6.8 QBitmap）
 * @author     XinYueC 团队
 * @note       继承自 XPixmap，提供单色（1 位）位图功能，用于掩码、蒙版等操作
 ******************************************************************************/
#ifndef XBITMAP_H
#define XBITMAP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XPixmap.h"
#include "XClass/XClass.h"


/* ========== XBitmap 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XBitmap)
XCLASS_DEFINE_EXTEND_END(XBitmap, XPixmap)

/**
 * @brief      XBitmap 位图类结构体（对标 Qt 6.8 QBitmap）
 * @note       继承自 XPixmap，单色（1 位）位图，通常用于掩码和蒙版操作
 */
typedef struct XBitmap
{
    XPixmap m_class;   /**< 继承的基类成员 */
}XBitmap;

/**
 * @brief      初始化 XBitmap 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XBitmap_class_init();

/**
 * @brief      在堆上创建 XBitmap 实例
 * @return     指向新创建的 XBitmap 对象的指针，失败返回 NULL
 */
XBitmap* XBitmap_create();

/**
 * @brief      初始化 XBitmap 实例（创建空位图）
 * @param self 待初始化的 XBitmap 对象指针
 */
void XBitmap_init(XBitmap* self);

/**
 * @brief      使用指定宽度和高度创建位图
 * @param self   待初始化的 XBitmap 对象指针
 * @param width  位图宽度
 * @param height 位图高度
 */
void XBitmap_init_ex(XBitmap* self, int width, int height);

/**
 * @brief      使用指定尺寸创建位图
 * @param self 待初始化的 XBitmap 对象指针
 * @param size 尺寸结构体指针
 */
void XBitmap_init_size(XBitmap* self, const XSize* size);

/**
 * @brief      从文件加载位图
 * @param self     待初始化的 XBitmap 对象指针
 * @param fileName 文件名
 * @param format   图像格式字符串（NULL 表示自动检测）
 */
void XBitmap_init_file(XBitmap* self, const char* fileName, const char* format);

/**
 * @brief      从 XPixmap 创建位图（内部使用，弃用提示：请使用 fromPixmap）
 * @param self  待初始化的 XBitmap 对象指针
 * @param other 源 XPixmap 对象指针
 */
void XBitmap_init_pixmap(XBitmap* self, const XPixmap* other);

/**
 * @brief      复制构造函数
 * @param self 目标 XBitmap 对象指针
 * @param other 源 XBitmap 对象指针
 */
void XBitmap_copy(XBitmap* self, const XBitmap* other);

/**
 * @brief      释放 XBitmap 资源
 * @param self 待释放的 XBitmap 对象指针
 */
void XBitmap_deinit(XBitmap* self);

/**
 * @brief      虚函数调度：拷贝
 * @param dest 目标对象指针
 * @param src  源对象指针
 */
void XBitmap_copy_base(XBitmap* dest, const XBitmap* src);

/**
 * @brief      虚函数调度：释放
 * @param self 待释放的对象指针
 */
void XBitmap_deinit_base(XBitmap* self);

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
void XBitmap_delete_base(XBitmap* self);

/* ========== 操作方法 ========== */

/**
 * @brief      清除位图（填充为 color0，即全 0）
 * @param self 目标 XBitmap 对象指针
 */
void XBitmap_clear(XBitmap* self);

/**
 * @brief      使用变换矩阵变换位图
 * @param self    目标 XBitmap 对象指针
 * @param m00-m23 变换矩阵参数
 * @param out     输出结果位图指针
 */
void XBitmap_transformed(const XBitmap* self, float m00, float m01, float m02,
                         float m10, float m11, float m12, XBitmap* out);

/* ========== 静态方法 ========== */

/**
 * @brief      从 XImage 创建位图
 * @param image 源 XImage 对象指针
 * @param flags 转换标志
 * @param out   输出位图指针
 */
void XBitmap_fromImage(const XImage* image, uint32_t flags, XBitmap* out);

/**
 * @brief      从原始位数据创建位图
 * @param size       尺寸结构体指针
 * @param bits       原始位数据指针
 * @param monoFormat 单色格式（默认 XImageFormat_MonoLSB）
 * @param out        输出位图指针
 */
void XBitmap_fromData(const XSize* size, const uint8_t* bits, XImageFormat monoFormat, XBitmap* out);

/**
 * @brief      从 XPixmap 创建位图
 * @param pixmap 源 XPixmap 对象指针
 * @param out    输出位图指针
 */
void XBitmap_fromPixmap(const XPixmap* pixmap, XBitmap* out);

#ifdef __cplusplus
}
#endif
#endif /* XBITMAP_H */


