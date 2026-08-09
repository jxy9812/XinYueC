#ifndef XCANBUS_H
#define XCANBUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XObject.h"
#include "XCanBusDevice.h"
#include "XCanBusDeviceInfo.h"
#include "XCanBusFactory.h"
#include "XStringList.h"
#include "XMap.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanBus.h
 * @brief CAN 总线单例工厂头文件（对齐 Qt6 QCanBus）
 * @details 处理 CAN 总线插件的注册和创建。QCanBus 在运行时加载
 *          Qt CAN 总线插件。序列总线插件的所有权转移给加载器。
 *          使用单例设计模式。
 *
 * @par 功能特性
 * - 单例模式访问
 * - 插件列表查询
 * - 可用设备枚举
 * - 设备创建
 *
 * @par 使用示例
 * @code
 * // 获取单例
 * XCanBus* canBus = XCanBus_instance();
 *
 * // 查询可用插件
 * XStringList* plugins = XCanBus_plugins(canBus);
 *
 * // 获取可用设备
 * XVector* devices = XCanBus_availableDevices(canBus, "socketcan", NULL);
 *
 * // 创建设备
 * XCanBusDevice* device = XCanBus_createDevice(canBus,
 *     "socketcan", "can0", NULL);
 * if (device) {
 *     device->connectDevice(device);
 * }
 * @endcode
 */

/******************************************************************************************
 * 插件注册条目
 ******************************************************************************************/

/**
 * @brief CAN 插件注册信息
 * @details 存储已注册的插件元数据和工厂实例
 */
typedef struct XCanBus_PluginEntry {
    XString* m_key;                 ///< 插件键名（如 "socketcan"）
    XCanBusFactory* m_factory;      ///< 工厂实例指针
    bool m_loaded;                  ///< 是否已加载
} XCanBus_PluginEntry;

/******************************************************************************************
 * XCanBus 结构体
 ******************************************************************************************/

/**
 * @brief CAN 总线单例工厂结构体
 * @details 继承自 XObject，管理 CAN 总线插件的注册和创建。
 *          使用单例设计模式，通过 XCanBus_instance() 获取全局实例。
 */
typedef struct XCanBus {
    XObject m_class;                ///< 继承自 XObject 基类
    XMap* m_plugins;                ///< 插件映射表（XMap<XString, XCanBus_PluginEntry>）
} XCanBus;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化 XCanBus 的虚函数表
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XCanBus_class_init(void);

/**
 * @brief 初始化 XCanBus 实例
 * @param canBus 待初始化的 XCanBus 对象指针（非 NULL）
 */
void XCanBus_init(XCanBus* canBus);

/******************************************************************************************
 * 单例访问（对齐 QCanBus::instance）
 ******************************************************************************************/

/**
 * @brief 获取 XCanBus 单例
 * @return XCanBus 全局实例指针
 * @note 如果实例尚未创建，会自动创建。使用单例设计模式。
 */
XCanBus* XCanBus_instance(void);

/******************************************************************************************
 * 插件管理 API（对齐 QCanBus）
 ******************************************************************************************/

/**
 * @brief 获取所有已加载插件的标识符列表
 * @param canBus XCanBus 实例指针
 * @return 插件标识符列表（XStringList*），调用者负责释放
 */
XStringList* XCanBus_plugins(const XCanBus* canBus);

/**
 * @brief 注册一个 CAN 总线插件
 * @param canBus XCanBus 实例指针（非 NULL）
 * @param plugin 插件名称
 * @param factory 工厂实例指针（所有权转移给 XCanBus）
 * @return 注册成功返回 true，失败返回 false
 */
bool XCanBus_registerPlugin(XCanBus* canBus, const char* plugin, XCanBusFactory* factory);

/**
 * @brief 获取指定插件的可用设备列表
 * @param canBus XCanBus 实例指针
 * @param plugin 插件名称
 * @param errorMessage 输出参数，错误描述字符串（由调用者释放，可为 NULL）
 * @return 可用设备信息列表（XVector<XCanBusDeviceInfo>*），调用者负责释放
 */
XVector* XCanBus_availableDevices(const XCanBus* canBus, const char* plugin, char** errorMessage);

/**
 * @brief 获取所有插件的可用设备列表
 * @param canBus XCanBus 实例指针
 * @param errorMessage 输出参数，错误描述字符串（由调用者释放，可为 NULL）
 * @return 可用设备信息列表（XVector<XCanBusDeviceInfo>*），调用者负责释放
 */
XVector* XCanBus_availableDevices_all(const XCanBus* canBus, char** errorMessage);

/**
 * @brief 创建 CAN 总线设备
 * @param canBus XCanBus 实例指针
 * @param plugin 插件名称（如 "socketcan"）
 * @param interfaceName 接口名称（如 "can0"、"vcan0"）
 * @param errorMessage 输出参数，错误描述字符串（由调用者释放，可为 NULL）
 * @return 成功返回 XCanBusDevice 指针（所有权转移给调用者），失败返回 NULL
 *
 * @par 示例
 * @code
 * char* errorStr = NULL;
 * XCanBusDevice* device = XCanBus_createDevice(
 *     XCanBus_instance(), "socketcan", "vcan0", &errorStr);
 * if (!device) {
 *     printf("Error: %s\n", errorStr);
 *     XFree_System(errorStr);
 * } else {
 *     device->connectDevice(device);
 * }
 * @endcode
 */
XCanBusDevice* XCanBus_createDevice(const XCanBus* canBus,
    const char* plugin, const char* interfaceName, char** errorMessage);

#ifdef __cplusplus
}
#endif

#endif // XCANBUS_H
