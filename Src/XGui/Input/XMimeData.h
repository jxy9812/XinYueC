/******************************************************************************
 * @file       XMimeData.h
 * @brief      XMimeData 剪贴板 MIME 数据容器（对标 Qt 6.8 QMimeData）。
 * @details    XMimeData 继承 XObject，保存可跨进程/跨模块交换的剪贴板
 *             数据：纯文本（text/plain）、HTML（text/html）、颜色
 *             （application/x-color）、图像（application/x-qt-image），
 *             以及任意自定义格式名 + 字节数据的扩展表。本模块不依赖任何
 *             平台 API，剪贴板数据完全程序化存储，供 XClipboard 使用。
 *             与 Qt 的 QMimeData 一致：本对象可被拷贝（深拷贝自定义表、
 *             共享引用计数的图像像素数据），并将自身所有权交给
 *             XClipboard_setMimeData 后由剪贴板统一释放。
 * @note       模块开关 XMIMEDATA_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XMimeData 公共 API，XClipboard 退化为仅文本模式。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMIMEDATA_H
#define XMIMEDATA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XColor.h"
#if XMIMEDATA_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XMimeDataPrivate XMimeDataPrivate;/** @brief 声明 XMimeData 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XMimeData)
XCLASS_DEFINE_EXTEND_END(XMimeData, XObject)



/**
 * @brief      XMimeData 剪贴板 MIME 数据对象；m_class 必须为第一个成员。
 * @details    所有数据保存在 m_data 私有块中，调用方不得直接访问。
 */
typedef struct XMimeData
{
    XObject          m_class; /**< 第一个成员，由 XObject 管理，禁止手工修改。 */
    XMimeDataPrivate* m_data; /**< 私有数据快照，由 XMimeData 拥有。 */
} XMimeData;

/**
 * @brief      初始化 XMimeData 类虚函数表并返回共享表指针。
 * @return     XMimeData 类的共享 XVtable 指针。
 */
XVtable* XMimeData_class_init(void);

/**
 * @brief      初始化空 XMimeData（无任何格式）。
 * @param      self 待初始化对象；必须与 XMimeData_deinit_base 成对调用。
 */
void XMimeData_init(XMimeData* self);

/**
 * @brief      使用默认内存类型在堆上创建空 XMimeData。
 * @return     新对象指针；失败返回 NULL，调用方用 XMimeData_delete_base 释放。
 */
#define XMimeData_create() XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建空 XMimeData。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XMimeData* XMimeData_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XMimeData 资源（栈/外部存储对象使用）。 */
#define XMimeData_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XMimeData 对象。 */
#define XMimeData_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 通过 XClass 虚表深拷贝 XMimeData；未初始化的目标会先自动初始化。 */
#define XMimeData_copy_base(self, other) XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 通过 XClass 虚表移动 XMimeData；移动后源对象私有块为空。 */
#define XMimeData_move_base(self, other) XClass_move_base((XClass*)(self), (XClass*)(other))

/**
 * @brief      清空全部数据（对标 QMimeData::clear）。
 * @param      self 目标对象；可为 NULL。
 */
void XMimeData_clear(XMimeData* self);

/**
 * @brief      判断是否包含指定 MIME 类型（对标 QMimeData::hasFormat）。
 * @details    MIME 类型名大小写不敏感；内置格式名：text/plain、text/html、
 *             application/x-color、application/x-qt-image；自定义格式与
 *             setData 登记的格式名逐一比对（不含参数部分，如 ";charset=utf-8"）。
 * @param      self     目标对象；可为 NULL（视为空）。
 * @param      mimeType UTF-8 编码的 MIME 类型名；可为 NULL。
 * @return     存在返回 true。
 */
bool XMimeData_hasFormat(const XMimeData* self, const char* mimeType);

/**
 * @brief      返回全部可用格式名（对标 QMimeData::formats）。
 * @return     新建的 XStringList，按内置格式优先、自定义格式在后的顺序；
 *             调用方用 XStringList_delete_base 释放。
 */
XStringList* XMimeData_formats(const XMimeData* self);

/* ==================== 纯文本（对标 QMimeData::text / setText） ==================== */

/** @brief 是否存在纯文本数据。 */
bool XMimeData_hasText(const XMimeData* self);

/**
 * @brief      读取纯文本（对标 QMimeData::text）。
 * @return     新建 XString 堆拷贝（UTF-8），无文本时返回 NULL；
 *             调用方用 XString_delete_base 释放。
 */
XString* XMimeData_text(const XMimeData* self);

/**
 * @brief      设置纯文本（对标 QMimeData::setText）。
 * @param      self 目标对象；可为 NULL。
 * @param      text UTF-8 文本；为空串时仍登记 text/plain 格式。
 */
void XMimeData_setText(XMimeData* self, const XString* text);

/* ==================== HTML（对标 QMimeData::html / setHtml） ==================== */

/** @brief 是否存在 HTML 数据。 */
bool XMimeData_hasHtml(const XMimeData* self);

/**
 * @brief      读取 HTML（对标 QMimeData::html）。
 * @return     新建 XString 堆拷贝，无 HTML 时返回 NULL；调用方释放。
 */
XString* XMimeData_html(const XMimeData* self);

/**
 * @brief      设置 HTML（对标 QMimeData::setHtml）。
 * @param      self 目标对象；可为 NULL。
 * @param      html UTF-8 编码的 HTML 文档。
 */
void XMimeData_setHtml(XMimeData* self, const XString* html);

/* ==================== 颜色（对标 QMimeData::colorData / setColorData） ==================== */

/** @brief 是否存在颜色数据。 */
bool XMimeData_hasColor(const XMimeData* self);

/**
 * @brief      读取颜色（对标 QMimeData::colorData）。
 * @return     已登记的颜色值；未登记时返回 XColor_create() 的无效颜色。
 */
XColor XMimeData_colorData(const XMimeData* self);

/**
 * @brief      设置颜色（对标 QMimeData::setColorData）。
 * @param      self  目标对象；可为 NULL。
 * @param      color 颜色值。
 */
void XMimeData_setColorData(XMimeData* self, XColor color);

/* ==================== 图像（对标 QMimeData::imageData / setImageData） ==================== */

/** @brief 是否存在图像数据。 */
bool XMimeData_hasImage(const XMimeData* self);

/**
 * @brief      读取图像（对标 QMimeData::imageData）。
 * @details    返回的 XImage 与内部共享引用计数的像素数据（copy_base 语义），
 *             调用方用 XImage_delete_base 释放。
 * @return     新建 XImage 对象；无图像时返回 NULL。
 */
XImage* XMimeData_imageData(const XMimeData* self);

/**
 * @brief      设置图像（对标 QMimeData::setImageData，纯图分支）。
 * @details    内部共享引用计数像素数据（不深拷贝位图本身），登记
 *             application/x-qt-image 格式；会覆盖旧图像，不清除其他数据。
 * @param      self  目标对象；可为 NULL。
 * @param      image 源图像；可为 NULL（等价清除图像标记）。
 */
void XMimeData_setImageData(XMimeData* self, const XImage* image);

/* ==================== 自定义格式（对标 QMimeData::setData / data） ==================== */

/**
 * @brief      登记自定义格式及字节数据（对标 QMimeData::setData）。
 * @details    对于内置格式名自动路由：text/plain -> setText、text/html ->
 *             setHtml；其余格式名作为自定义项保存（data 按原始字节保留，
 *             允许包含任意二进制内容）。同一格式名重复登记时覆盖。
 * @param      self   目标对象；可为 NULL。
 * @param      format UTF-8 编码的 MIME 类型名；可为 NULL。
 * @param      data   格式数据（字节缓冲语义）；可为 NULL（等价空数据）。
 */
void XMimeData_setData(XMimeData* self, const char* format, const XString* data);

/**
 * @brief      读取指定格式的原始数据（对标 QMimeData::data）。
 * @param      self   目标对象；可为 NULL。
 * @param      format UTF-8 编码的 MIME 类型名；可为 NULL。
 * @return     新建 XString 堆拷贝（保留原始字节）；无该格式时返回 NULL，
 *             调用方用 XString_delete_base 释放。
 */
XString* XMimeData_data(const XMimeData* self, const char* format);

#endif /* XMIMEDATA_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMIMEDATA_H */
