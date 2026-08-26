/*
 * @file       XImagePluginRegistry.h
 * @brief      XImageIOPlugin 源码级注册表，对标 Qt 6.8 图像插件发现机制。
 * @details    提供固定容量插件注册表，并负责把插件声明的格式、MIME 类型与
 *             XImageIOHandler 工厂接入 XImageReader/XImageWriter。
 */
#ifndef XIMAGEPLUGINREGISTRY_H
#define XIMAGEPLUGINREGISTRY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XGuiConfig.h"
#include "XImageIOPlugin.h"
#include "XIODevice.h"
#include "XStringList.h"

/**
 * @brief 插件注册表最大容量。
 * @note 当前采用固定容量静态存储，避免嵌入式环境引入动态注册链表。
 */
#ifndef XIMAGEPLUGINREGISTRY_CAPACITY
#define XIMAGEPLUGINREGISTRY_CAPACITY 32
#endif

/**
 * @brief 清空注册表。
 * @note 不释放插件对象，调用方仍需自行管理插件生命周期。
 */
void XImagePluginRegistry_clear(void);

/**
 * @brief 注册一个图像 I/O 插件。
 * @param plugin 待注册插件对象；插件必须保持存活，注册表不取得所有权。
 * @return 注册成功返回 true；参数无效、已存在或容量已满返回 false。
 */
bool XImagePluginRegistry_addPlugin(XImageIOPlugin* plugin);

/**
 * @brief 从注册表移除指定插件。
 * @param plugin 待移除插件对象。
 * @return 找到并移除返回 true；未注册则返回 false。
 * @note 不释放插件对象；移除后调用方可安全销毁插件。
 */
bool XImagePluginRegistry_removePlugin(XImageIOPlugin* plugin);

/**
 * @brief 查询当前注册的插件数量。
 * @return 插件数量。
 */
int XImagePluginRegistry_pluginCount(void);

/**
 * @brief 按索引获取插件对象。
 * @param index 插件索引，从 0 开始。
 * @return 插件对象指针；越界时返回 NULL。
 */
XImageIOPlugin* XImagePluginRegistry_pluginAt(int index);

/**
 * @brief 为设备创建图像读取处理器。
 * @param device 待读取的设备指针，不能为 NULL。
 * @param format 期望格式；可为 NULL 或空字符串，表示由注册表按内容探测。
 * @return 新建 XImageIOHandler 对象，所有权交给调用方；未找到返回 NULL。
 */
XImageIOHandler* XImagePluginRegistry_createReadHandler(XIODevice* device,
                                                        const XString* format);

/**
 * @brief 为设备创建图像写入处理器。
 * @param device 待写入的设备指针，不能为 NULL。
 * @param format 图像格式；不能为空，写入处理器必须明确格式。
 * @return 新建 XImageIOHandler 对象，所有权交给调用方；未找到返回 NULL。
 */
XImageIOHandler* XImagePluginRegistry_createWriteHandler(XIODevice* device,
                                                         const XString* format);

/**
 * @brief 查询插件注册表能否读出指定格式。
 * @param format 格式名；可为 NULL。
 * @return 至少一个插件声明支持读取该格式时返回 true。
 */
bool XImagePluginRegistry_supportsReadFormat(const XString* format);

/**
 * @brief 查询插件注册表能否写出指定格式。
 * @param format 格式名；可为 NULL。
 * @return 至少一个插件声明支持写入该格式时返回 true。
 */
bool XImagePluginRegistry_supportsWriteFormat(const XString* format);

/**
 * @brief 合并插件声明支持的图像格式列表。
 * @param readOnly true 表示仅统计 CanRead 插件；false 表示仅统计 CanWrite 插件。
 * @return 新建 XStringList，调用方负责释放。
 */
XStringList* XImagePluginRegistry_supportedImageFormats(bool readOnly);

/**
 * @brief 合并插件声明支持的 MIME 类型列表。
 * @param readOnly true 表示仅统计 CanRead 插件；false 表示仅统计 CanWrite 插件。
 * @return 新建 XStringList，调用方负责释放。
 */
XStringList* XImagePluginRegistry_supportedMimeTypes(bool readOnly);

/**
 * @brief 查询插件 MIME 类型对应的格式列表。
 * @param mimeType MIME 类型字符串。
 * @param readOnly true 表示仅查询可读插件；false 表示仅查询可写插件。
 * @return 新建 XStringList；未知 MIME 类型返回空列表。
 */
XStringList* XImagePluginRegistry_imageFormatsForMimeType(const XString* mimeType,
                                                          bool readOnly);

/**
 * @brief 使用 UTF-8 MIME 类型查询插件格式列表的兼容重载。
 * @param mimeType UTF-8 编码的 MIME 类型字符串。
 * @param readOnly true 表示仅查询可读插件；false 表示仅查询可写插件。
 * @return 新建 XStringList；未知 MIME 类型返回空列表。
 */
XStringList* XImagePluginRegistry_imageFormatsForMimeType_2(const char* mimeType,
                                                            bool readOnly);

#ifdef __cplusplus
}
#endif

#endif /* XIMAGEPLUGINREGISTRY_H */
