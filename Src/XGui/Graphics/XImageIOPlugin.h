/******************************************************************************
 * @file       XImageIOPlugin.h
 * @brief      XImageIOPlugin 图像 I/O 插件抽象类（对标 Qt 6.8 QImageIOPlugin）。
 ******************************************************************************/
#ifndef XIMAGEIOPLUGIN_H
#define XIMAGEIOPLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"
#include "XImageIOHandler.h"
#include "XStringList.h"

XCLASS_DEFINE_BEGING(XImageIOPlugin)
XCLASS_DEFINE_ENUM(XImageIOPlugin, Capabilities) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XImageIOPlugin, Create),
XCLASS_DEFINE_ENUM(XImageIOPlugin, Keys),
XCLASS_DEFINE_ENUM(XImageIOPlugin, NameFilters),
XCLASS_DEFINE_ENUM(XImageIOPlugin, MimeTypes),
XCLASS_DEFINE_END(XImageIOPlugin)

typedef enum XImageIOPluginCapability
{
    XImageIOPlugin_CanRead = 0x01, /**< 插件支持读取图像。 */
    XImageIOPlugin_CanWrite = 0x02, /**< 插件支持写入图像。 */
    XImageIOPlugin_CanReadIncremental = 0x04 /**< 插件支持增量读取。
        * 对标 Qt 6.8 qimageiohandler.h:111 的 CanReadIncremental。 */
} XImageIOPluginCapability;

/**
 * @brief 图像 I/O 插件对象。
 * @note 派生插件通过 XClass 虚函数表提供格式探测和处理器创建逻辑。
 */
typedef struct XImageIOPlugin
{
    XObject m_class; /**< 继承的 XObject 基类成员。 */
} XImageIOPlugin;

/**
 * @brief 初始化 XImageIOPlugin 虚函数表。
 * @return 插件类虚函数表指针。
 */
XVtable* XImageIOPlugin_class_init(void);

/**
 * @brief 按指定内存类型创建图像 I/O 插件。
 * @param memory 对象使用的内存类型。
 * @return 新建插件指针；失败时返回 NULL。
 */
XImageIOPlugin* XImageIOPlugin_create_ex(XMemoryType memory);

/**
 * @brief 初始化图像 I/O 插件实例。
 * @param self 待初始化的插件指针。
 */
void XImageIOPlugin_init(XImageIOPlugin* self);

/**
 * @brief 释放插件实例持有的资源。
 * @param self 待释放的插件指针。
 */
#define XImageIOPlugin_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief 删除堆上分配的插件实例。
 * @param self 待删除的插件指针。
 */
/** @brief 删除堆上的图像 I/O 插件。 @param self 待删除的插件指针。 */
#define XImageIOPlugin_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief 查询插件对设备和格式的能力。
 * @param self 插件指针。
 * @param device 待读写的设备指针，可为 NULL。
 * @param format 格式名；使用 XString 表示，可为 NULL。
 * @return XImageIOPluginCapability 位掩码。
 */
uint32_t XImageIOPlugin_capabilities_base(const XImageIOPlugin* self,
                                          XIODevice* device, const XString* format);

/**
 * @brief 为设备创建图像 I/O 处理器。
 * @param self 插件指针。
 * @param device 处理器绑定的设备指针。
 * @param format 图像格式名；可为 NULL 以便插件自动识别。
 * @return 新建的 XImageIOHandler 指针；不支持时返回 NULL。
 */
XImageIOHandler* XImageIOPlugin_create_base(const XImageIOPlugin* self,
                                            XIODevice* device,
                                            const XString* format);

/**
 * @brief 获取插件支持的格式键列表。
 * @param self 插件指针。
 * @return 插件元数据列表指针；由插件管理，调用方不得释放。未提供元数据时返回 NULL。
 */
XStringList* XImageIOPlugin_keys_base(const XImageIOPlugin* self);

/**
 * @brief 获取插件支持的 MIME 类型列表，每个元素与 keys_base 返回的
 *        格式键一一对应；列出该格式对应的 image/* MIME 类型。
 * @param self 插件指针。
 * @return 插件元数据列表指针；由插件管理，调用方不得释放。未提供元数据时返回 NULL。
 */
XStringList* XImageIOPlugin_mimeTypes_base(const XImageIOPlugin* self);

/**
 * @brief 获取插件支持的文件名过滤器列表。
 * @param self 插件指针。
 * @return 插件元数据列表指针；由插件管理，调用方不得释放。未提供元数据时返回 NULL。
 */
XStringList* XImageIOPlugin_nameFilters_base(const XImageIOPlugin* self);

#ifdef __cplusplus
}
#endif

#undef XImageIOPlugin_create
#define XImageIOPlugin_create() XImageIOPlugin_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif
