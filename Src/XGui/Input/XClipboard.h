/******************************************************************************
 * @file       XClipboard.h
 * @brief      XClipboard 剪贴板类（对标 Qt 6.8 QClipboard，实现全部公开 API）。
 * @details    XClipboard 继承 XObject，提供进程内剪贴板：三种模式
 *             （Clipboard/Selection/FindBuffer，对标 QClipboard::Mode）、
 *             文本/HTML/图像/像素图/MIME 数据读写、所有权标志与 4 个通知
 *             信号。本模块不依赖任何平台 API：所有数据程序化存储于对象
 *             内部，不连接系统剪贴板；XGuiApplication_clipboard 返回进程
 *             内单例后，未来平台后端可在 setMimeData 时把数据同步给系统
 *             剪贴板。Selection/FindBuffer 两种平台相关模式
 *             supportsSelection()/supportsFindBuffer() 恒为 false，与
 *             Qt 在不支持该选择缓冲的平台上行为一致。
 * @note       模块开关 XCLIPBOARD_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XClipboard 公共 API。依赖子开关 XMIMEDATA_ON，关闭时
 *             MIME 数据接口退化为仅文本模式（空实现）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCLIPBOARD_H
#define XCLIPBOARD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XString.h"
#if XMIMEDATA_ON
#include "XMimeData.h"
#endif

#if XCLIPBOARD_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XClipboardPrivate XClipboardPrivate;/** @brief 声明 XClipboard 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XClipboard)
XCLASS_DEFINE_EXTEND_END(XClipboard, XObject)



/**
 * @brief      剪贴板模式（对标 Qt 6.8 QClipboard::Mode）。
 * @details    Selection 为 X11 主选择缓冲、FindBuffer 为 X11 查找缓冲；
 *             本实现保留枚举以对齐 Qt，但两种模式仅做存储、不能与系统
 *             交互。
 */
typedef enum XClipboardMode
{
    XClipboardMode_Clipboard = 0, /**< 标准剪贴板。 */
    XClipboardMode_Selection,     /**< 选择缓冲（supportsSelection 恒 false）。 */
    XClipboardMode_FindBuffer,    /**< 查找缓冲（supportsFindBuffer 恒 false）。 */
    XClipboardMode_LastMode = XClipboardMode_FindBuffer /**< 最后一个模式。 */
} XClipboardMode;

/**
 * @brief      XClipboard 剪贴板对象；m_class 必须为第一个成员。
 * @details    每个模式的数据保存在 m_data 私有块中，调用方不得直接访问。
 */
typedef struct XClipboard
{
    XObject              m_class; /**< 第一个成员，由 XObject 管理。 */
    XClipboardPrivate*   m_data;  /**< 私有数据块，由 XClipboard 拥有。 */
} XClipboard;

/**
 * @brief      初始化 XClipboard 类虚函数表并返回共享表指针。
 * @return     XClipboard 类的共享 XVtable 指针。
 */
XVtable* XClipboard_class_init(void);

/**
 * @brief      初始化空 XClipboard（三种模式均无数据）。
 * @param      self 待初始化对象；必须与 XClipboard_deinit_base 成对调用。
 */
void XClipboard_init(XClipboard* self);

/**
 * @brief      使用默认内存类型在堆上创建空 XClipboard。
 * @return     新对象指针；失败返回 NULL，调用方用 XClipboard_delete_base 释放。
 */
#define XClipboard_create() XClipboard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建空 XClipboard。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XClipboard* XClipboard_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XClipboard 资源（栈/外部存储对象使用）。 */
#define XClipboard_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 深拷贝 XClipboard 资源。 */
#define XClipboard_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动 XClipboard 资源。 */
#define XClipboard_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))
/** @brief 删除堆上的 XClipboard 对象。 */
#define XClipboard_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 模式能力与所有权（对标 QClipboard） ==================== */

/**
 * @brief      查询是否支持选择缓冲（对标 QClipboard::supportsSelection）。
 * @return     恒为 false（本实现不接系统选择缓冲）。
 */
bool XClipboard_supportsSelection(const XClipboard* self);

/**
 * @brief      查询是否支持查找缓冲（对标 QClipboard::supportsFindBuffer）。
 * @return     恒为 false。
 */
bool XClipboard_supportsFindBuffer(const XClipboard* self);

/** @brief 剪贴板内容是否由本进程写入（对标 ownsClipboard）。 */
bool XClipboard_ownsClipboard(const XClipboard* self);
/** @brief 选择缓冲内容是否由本进程写入（对标 ownsSelection）。 */
bool XClipboard_ownsSelection(const XClipboard* self);
/** @brief 查找缓冲内容是否由本进程写入（对标 ownsFindBuffer）。 */
bool XClipboard_ownsFindBuffer(const XClipboard* self);

/* ==================== 内容清空（对标 QClipboard::clear） ==================== */

/**
 * @brief      清空指定模式的剪贴板内容（对标 QClipboard::clear）。
 * @details    清除文本与 MIME 数据、解开所有权，并发射 changed(mode)；
 *             若剪贴板内容本就为空，Qt 仍会通知，这里保持一致。
 * @param      self 目标对象；可为 NULL。
 * @param      mode 目标模式。
 */
void XClipboard_clear(XClipboard* self, XClipboardMode mode);

/* ==================== 文本（对标 QClipboard::text / setText） ==================== */

/**
 * @brief      读取指定模式下的纯文本（对标 QClipboard::text(Mode)）。
 * @return     新建 XString 堆拷贝（UTF-8），该模式无文本时返回 NULL；
 *             空文本返回非 NULL 的空 XString（以 toUtf8_length==0 区分）。
 *             调用方用 XString_delete_base 释放。
 */
XString* XClipboard_text(XClipboard* self, XClipboardMode mode);

/**
 * @brief      读取文本，同时输出子类型（对标 QClipboard::text(QString&, Mode)）。
 * @param      self    目标对象；可为 NULL。
 * @param      subtype 输出参数：文本子类型（"plain"）；有文本时分配堆拷贝，
 *                    无文本时置 NULL；可为 NULL。
 * @param      mode    目标模式。
 * @return     与 XClipboard_text 相同的堆拷贝文本；调用方释放。
 */
XString* XClipboard_text_2(XClipboard* self, XString** subtype, XClipboardMode mode);

/**
 * @brief      设置指定模式的纯文本（对标 QClipboard::setText）。
 * @details    深拷贝文本、标记本进程所有权并发射 changed(mode)；
 *             MIME 开启时按 Qt 语义创建新的 XMimeData，登记 text/plain 后
 *             通过 setMimeData() 转移所有权，因此 mimeData() 与 text() 保持
 *             同一份文本；MIME 裁剪关闭时退化为独立的 m_text 存储。
 * @param      self 目标对象；可为 NULL。
 * @param      text UTF-8 文本；可为 NULL（等价空串）。
 * @param      mode 目标模式。
 */
void XClipboard_setText(XClipboard* self, const XString* text, XClipboardMode mode);

/* ==================== MIME 数据（对标 QClipboard::mimeData / setMimeData） ==================== */

/**
 * @brief      读取指定模式下的 MIME 数据（对标 QClipboard::mimeData）。
 * @return     内部借用指针，由 XClipboard 拥有，调用方不得释放；
 *             该模式无数据时返回 NULL。
 */
const XMimeData* XClipboard_mimeData(const XClipboard* self, XClipboardMode mode);

/**
 * @brief      设置指定模式的 MIME 数据（对标 QClipboard::setMimeData）。
 * @details    函数接管 data 所有权（此后由 XClipboard 统一释放），标记
 *             本进程所有权，发射 changed(mode) 与 dataChanged()；
 *             同时清空同模式旧文本。
 * @param      self 目标对象；可为 NULL。
 * @param      data XMimeData 对象（堆分配）；可为 NULL（等价 clear）。
 * @param      mode 目标模式。
 */
void XClipboard_setMimeData(XClipboard* self, XMimeData* data, XClipboardMode mode);

/* ==================== 图像 / 像素图（对标 QClipboard::setImage / image 等） ==================== */

/**
 * @brief      读取指定模式下的图像（对标 QClipboard::image）。
 * @return     新建 XImage（与内部共享引用计数像素），该模式无图像时返回
 *             NULL；调用方用 XImage_delete_base 释放。
 */
XImage* XClipboard_image(const XClipboard* self, XClipboardMode mode);

/**
 * @brief      设置指定模式的图像（对标 QClipboard::setImage）。
 * @details    等价于构造含图像的 XMimeData 后调用 setMimeData。
 * @param      self  目标对象；可为 NULL。
 * @param      image 源图像；可为 NULL。
 * @param      mode  目标模式。
 */
void XClipboard_setImage(XClipboard* self, const XImage* image, XClipboardMode mode);

/**
 * @brief      读取指定模式下的像素图（对标 QClipboard::pixmap）。
 * @return     新建 XPixmap（像素数据由图像转换），无图像时返回 NULL；
 *             调用方用 XPixmap_delete_base 释放。
 */
XPixmap* XClipboard_pixmap(const XClipboard* self, XClipboardMode mode);

/**
 * @brief      设置指定模式的像素图（对标 QClipboard::setPixmap）。
 * @details    内部转换为 XImage 后按 setMimeData 语义保存。
 * @param      self   目标对象；可为 NULL。
 * @param      pixmap 源像素图；可为 NULL。
 * @param      mode   目标模式。
 */
void XClipboard_setPixmap(XClipboard* self, const XPixmap* pixmap, XClipboardMode mode);

/* ==================== 信号（对标 QClipboard 全部信号） ==================== */

/**
 * @brief      剪贴板内容变化信号（对标 QClipboard::changed）。
 * @param      self 目标对象。
 * @param      mode 发生变化的模式。
 * @return     信号句柄；槽用 XObject_connect_2 连接。
 */
void* XClipboard_changed_signal(XClipboard* self, XClipboardMode mode);

/** @brief 选择缓冲变化信号（对标 QClipboard::selectionChanged）。 */
void* XClipboard_selectionChanged_signal(XClipboard* self);

/** @brief 查找缓冲变化信号（对标 QClipboard::findBufferChanged）。 */
void* XClipboard_findBufferChanged_signal(XClipboard* self);

/** @brief 数据内容变化信号（对标 QClipboard::dataChanged）。 */
void* XClipboard_dataChanged_signal(XClipboard* self);

#endif /* XCLIPBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCLIPBOARD_H */
