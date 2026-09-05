/******************************************************************************
 * @file       XBitmap.h
 * @brief      XBitmap 单色位图类（对标 Qt 6.8 QBitmap）
 * @author     XinYueC 团队
 * @note       继承自 XPixmap，提供单色（1 位）位图功能，用于掩码、蒙版等操作。
 *             非空位图使用 MonoLSB 存储，颜色表固定遵循 Qt::color0=白色、
 *             Qt::color1=黑色；空位图的深度为 0。
 ******************************************************************************/
#ifndef XBITMAP_H
#define XBITMAP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XPixmap.h"
#include "XClass.h"

/** @brief XVariant 前向声明，用于 QBitmap/QVariant 兼容适配。 */
typedef struct XVariant XVariant;


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
XBitmap* XBitmap_create_ex(XMemoryType memory);

/**
 * @brief      初始化 XBitmap 实例（创建空位图）
 * @param self 待初始化的 XBitmap 对象指针
 */
void XBitmap_init(XBitmap* self);

/**
 * @brief      使用指定宽度和高度创建位图；像素内容按 Qt 约定未初始化，
 *             实际嵌入式存储会以零位开始。
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
void XBitmap_init_file(XBitmap* self, const XString* fileName, const XString* format);
/**
 * @brief 使用 UTF-8 文件名和格式初始化位图的兼容重载。
 * @param self 待初始化的 XBitmap 对象指针。
 * @param fileName UTF-8 编码的文件名；可为 NULL。
 * @param format UTF-8 编码的格式名；可为 NULL 以自动检测。
 */
void XBitmap_init_file_2(XBitmap* self, const char* fileName, const char* format);

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

/**
 * @brief      释放 XBitmap 资源
 * @param self 待释放的 XBitmap 对象指针
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
/** @brief 通过 XClass 虚表释放位图资源。 @param self 待释放的位图对象指针。 */
#define XBitmap_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 交换两个位图的数据所有权。 */
void XBitmap_swap(XBitmap* self, XBitmap* other);

/**
 * @brief      虚函数调度：删除（释放堆上对象）
 * @param self 待删除的对象指针
 */
/** @brief 删除堆上的位图对象。 @param self 待删除的位图对象指针。 */
#define XBitmap_delete_base(self) XClass_delete_base((XClass*)(self))

/* ========== 操作方法 ========== */

/**
 * @brief      清除位图（填充为 Qt::color0，即白色和全 0 位）
 * @param self 目标 XBitmap 对象指针
 */
void XBitmap_clear(XBitmap* self);

/**
 * @brief      使用变换矩阵变换位图
 * @param self    目标 XBitmap 对象指针
 * @param m00-m23 变换矩阵参数
 * @param out     输出结果位图指针
 */
void XBitmap_transformed(const XBitmap* self, const XImageTransform* matrix,
                         XBitmap* out);

/**
 * @brief 使用六个浮点参数执行仿射变换的兼容 API。
 * @param self 源位图对象指针。
 * @param m00,m01,m02,m10,m11,m12 仿射变换矩阵参数。
 * @param out 输出位图对象指针，旧内容会被释放。
 */
void XBitmap_transformed_2(const XBitmap* self, float m00, float m01, float m02,
                           float m10, float m11, float m12, XBitmap* out);

/**
 * @brief 将位图封装为 XVariant 借用指针。
 * @param self 源位图对象指针；Variant 不接管其生命周期。
 * @return 存储 XBitmap* 的 XVariant；失败返回 NULL。
 * @note 该接口用于 C API 的 QVariant 兼容适配，Variant 复制后仍指向同一对象。
 */
XVariant* XBitmap_toVariant(const XBitmap* self);

/**
 * @brief 从 XVariant 取得位图借用指针。
 * @param variant 变体对象指针。
 * @return 变体中保存的 XBitmap*；类型不匹配返回 NULL。
 */
XBitmap* XBitmap_fromVariant(const XVariant* variant);

/** @brief 保留与 XVariant 类型适配命名一致的创建/读取宏。 */
#define XVariant_create_Bitmap XBitmap_toVariant
#define XVariant_toBitmap      XBitmap_fromVariant

/* ========== 静态方法 ========== */

/**
 * @brief      从 XImage 创建位图
 * @param image 源 XImage 对象指针；为空图像时输出空位图
 * @param flags 转换标志；位图转换始终强制使用 MonoLSB 存储
 * @param out   输出位图指针；旧内容会被释放，成功后颜色表为白色/黑色
 */
void XBitmap_fromImage(const XImage* image, uint32_t flags, XBitmap* out);

/**
 * @brief      从原始位数据创建位图
 * @param size       尺寸结构体指针；宽度和高度必须为正数
 * @param bits       原始位数据指针；每行按输入位序紧密排列
 * @param monoFormat 单色格式（仅允许 Mono 或 MonoLSB）
 * @param out        输出位图指针；旧内容会被释放
 */
void XBitmap_fromData(const XSize* size, const uint8_t* bits, XImageFormat monoFormat, XBitmap* out);

/**
 * @brief      从 XPixmap 创建位图
 * @param pixmap 源 XPixmap 对象指针；空图像时输出空位图
 * @param out    输出位图指针；源为现有一位 XBitmap 时共享底层数据，否则抖动转换
 */
void XBitmap_fromPixmap(const XPixmap* pixmap, XBitmap* out);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XBitmap_create
#define XBitmap_create() XBitmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XBITMAP_H */
