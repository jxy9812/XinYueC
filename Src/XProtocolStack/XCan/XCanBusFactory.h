#ifndef XCANBUSFACTORY_H
#define XCANBUSFACTORY_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XCanBusDevice.h"
#include "XCanBusDeviceInfo.h"
#include "XStringList.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanBusFactory.h
 * @brief CAN 总线插件工厂接口头文件（对齐 Qt6 QCanBusFactory）
 * @details 定义 CAN 总线插件的工厂接口。每个 CAN 插件（如 socketcan、
 *          virtualcan、peakcan 等）必须实现此接口以注册自身。
 *          这是一个纯虚接口，不继承 XClass。
 *
 * @par 功能特性
 * - 创建 CAN 设备实例
 * - 枚举可用设备
 *
 * @par 使用示例
 * @code
 * // 插件实现
 * typedef struct {
 *     XCanBusFactory base;
 *     // 插件特定数据...
 * } MyCanFactory;
 *
 * static XCanBusDevice* myCreateDevice(const XCanBusFactory* factory,
 *     const char* interfaceName, char** errorMessage)
 * {
 *     return (XCanBusDevice*)MyCanBackend_create(interfaceName);
 * }
 *
 * static XVector* myAvailableDevices(const XCanBusFactory* factory,
 *     char** errorMessage)
 * {
 *     // 返回 XVector<XCanBusDeviceInfo>
 * }
 *
 * void MyCanFactory_init(MyCanFactory* factory) {
 *     factory->base.createDevice = myCreateDevice;
 *     factory->base.availableDevices = myAvailableDevices;
 * }
 * @endcode
 */

/******************************************************************************************
 * CAN 总线工厂接口结构体
 ******************************************************************************************/

/**
 * @brief CAN 总线工厂接口结构体
 * @details 定义插件工厂的函数指针表。每个 CAN 插件实现此接口。
 *          对齐 Qt6 QCanBusFactory。
 */
typedef struct XCanBusFactory {
    /**
     * @brief 创建 CAN 设备
     * @param factory 工厂指针
     * @param interfaceName 接口名称（如 "can0"、"vcan0"）
     * @param errorMessage 输出参数，错误描述字符串（由调用者释放）
     * @return 成功返回 XCanBusDevice 指针，失败返回 NULL
     */
    XCanBusDevice* (*createDevice)(const struct XCanBusFactory* factory,
        const char* interfaceName, char** errorMessage);

    /**
     * @brief 获取可用设备列表
     * @param factory 工厂指针
     * @param errorMessage 输出参数，错误描述字符串（由调用者释放）
     * @return 可用设备信息列表（XVector<XCanBusDeviceInfo>*），由调用者释放
     */
    XVector* (*availableDevices)(const struct XCanBusFactory* factory,
        char** errorMessage);

    /**
     * @brief 销毁工厂
     * @param factory 工厂指针
     */
    void (*destroy)(struct XCanBusFactory* factory);
} XCanBusFactory;

/******************************************************************************************
 * 工厂接口辅助函数
 ******************************************************************************************/

/**
 * @brief 通过工厂创建 CAN 设备
 * @param factory 工厂指针（非 NULL）
 * @param interfaceName 接口名称
 * @param errorMessage 输出参数，错误描述字符串（由调用者释放）
 * @return 成功返回 XCanBusDevice 指针，失败返回 NULL
 */
static inline XCanBusDevice* XCanBusFactory_createDevice(
    const XCanBusFactory* factory, const char* interfaceName, char** errorMessage)
{
    if (factory && factory->createDevice)
        return factory->createDevice(factory, interfaceName, errorMessage);
    return NULL;
}

/**
 * @brief 通过工厂获取可用设备列表
 * @param factory 工厂指针（非 NULL）
 * @param errorMessage 输出参数，错误描述字符串（由调用者释放）
 * @return 可用设备信息列表，由调用者释放
 */
static inline XVector* XCanBusFactory_availableDevices(
    const XCanBusFactory* factory, char** errorMessage)
{
    if (factory && factory->availableDevices)
        return factory->availableDevices(factory, errorMessage);
    return NULL;
}

/**
 * @brief 销毁工厂
 * @param factory 工厂指针
 */
static inline void XCanBusFactory_destroy(XCanBusFactory* factory)
{
    if (factory && factory->destroy)
        factory->destroy(factory);
}

#ifdef __cplusplus
}
#endif

#endif // XCANBUSFACTORY_H
