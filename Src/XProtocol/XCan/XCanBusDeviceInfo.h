#ifndef XCANBUSDEVICEINFO_H
#define XCANBUSDEVICEINFO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanBusDeviceInfo.h
 * @brief CAN 总线设备信息头文件（对齐 Qt6 QCanBusDeviceInfo）
 * @details 提供 CAN 总线接口的信息。每个插件可能支持一个或多个具有不同
 *          能力的接口。此类提供关于可用功能的信息。
 *          这是一个值类型结构体，通过 XCanBusDevice_createDeviceInfo 创建。
 *
 * @par 功能特性
 * - 插件名称标识（如 "socketcan"、"peakcan"）
 * - 接口名称（如 "can0"、"vcan0"）
 * - 设备描述和序列号
 * - 用户可定义别名
 * - 通道号（多通道设备）
 * - CAN FD 能力查询
 * - 虚拟设备标识
 *
 * @par 使用示例
 * @code
 * // 通过设备创建
 * XCanBusDeviceInfo info;
 * XCanBusDevice_createDeviceInfo(&info, "socketcan", "can0", false, true);
 * // 查询属性
 * XString* name = XCanBusDeviceInfo_name(&info);
 * bool isVirtual = XCanBusDeviceInfo_isVirtual(&info);
 * XString_delete_base(name);
 * XCanBusDeviceInfo_deinit(&info);
 * @endcode
 */

/******************************************************************************************
 * CAN 总线设备信息结构体
 ******************************************************************************************/

/**
 * @brief CAN 总线设备信息结构体
 * @details 封装 CAN 总线接口的完整信息。这是一个值类型结构体，
 *          内部使用 XString* 管理字符串成员。
 *          通过 XCanBusDevice_createDeviceInfo 工厂方法创建。
 */
typedef struct XCanBusDeviceInfo {
    XString* m_plugin;              ///< 插件名称（如 "socketcan"、"peakcan"）
    XString* m_name;                ///< 接口名称（如 "can0"、"vcan0"）
    XString* m_description;         ///< 设备描述（如 "PCAN USB Pro FD"）
    XString* m_serialNumber;        ///< 设备序列号
    XString* m_alias;               ///< 用户可定义别名
    int m_channel;                  ///< 通道号（从 0 开始）
    bool m_hasFlexibleDataRate;     ///< 是否支持 CAN FD
    bool m_isVirtual;               ///< 是否为虚拟设备
} XCanBusDeviceInfo;

/******************************************************************************************
 * 初始化与清理
 ******************************************************************************************/

/**
 * @brief 初始化设备信息结构体（栈上使用）
 * @param info 待初始化的设备信息指针（不可为 NULL）
 * @note 栈上使用的设备信息必须调用此函数初始化，使用完毕后调用 XCanBusDeviceInfo_deinit
 */
void XCanBusDeviceInfo_init(XCanBusDeviceInfo* info);

/**
 * @brief 销毁设备信息（释放内部资源）
 * @param info 待销毁的设备信息指针（可为 NULL）
 */
void XCanBusDeviceInfo_deinit(XCanBusDeviceInfo* info);

/**
 * @brief 复制设备信息（深拷贝）
 * @param dest 目标设备信息指针（不可为 NULL，需已初始化）
 * @param src 源设备信息指针（不可为 NULL）
 */
void XCanBusDeviceInfo_copy(XCanBusDeviceInfo* dest, const XCanBusDeviceInfo* src);

/******************************************************************************************
 * 属性访问（对齐 Qt QCanBusDeviceInfo）
 ******************************************************************************************/

/**
 * @brief 获取插件名称
 * @param info 设备信息指针（不可为 NULL）
 * @return 插件名称字符串的深拷贝，调用者负责释放
 * @details 对应 QCanBus::createDevice() 的 plugin 参数，如 "peakcan"
 */
XString* XCanBusDeviceInfo_plugin(const XCanBusDeviceInfo* info);

/**
 * @brief 获取接口名称
 * @param info 设备信息指针（不可为 NULL）
 * @return 接口名称字符串的深拷贝，调用者负责释放
 * @details 对应 QCanBus::createDevice() 的 interfaceName 参数，如 "can0"
 */
XString* XCanBusDeviceInfo_name(const XCanBusDeviceInfo* info);

/**
 * @brief 获取设备描述
 * @param info 设备信息指针（不可为 NULL）
 * @return 描述字符串的深拷贝，调用者负责释放；无描述返回空字符串
 * @details 示例输出："PCAN USB Pro FD"
 */
XString* XCanBusDeviceInfo_description(const XCanBusDeviceInfo* info);

/**
 * @brief 获取设备序列号
 * @param info 设备信息指针（不可为 NULL）
 * @return 序列号字符串的深拷贝，调用者负责释放；无序列号返回空字符串
 * @sa XCanBusDeviceInfo_alias
 */
XString* XCanBusDeviceInfo_serialNumber(const XCanBusDeviceInfo* info);

/**
 * @brief 获取用户可定义别名
 * @param info 设备信息指针（不可为 NULL）
 * @return 别名字符串的深拷贝，调用者负责释放；无别名返回空字符串
 * @details 某些 CAN 硬件可通过厂商工具设置别名，用于标识特定硬件。
 *          与 serialNumber() 不同，别名不保证唯一。
 * @sa XCanBusDeviceInfo_serialNumber
 */
XString* XCanBusDeviceInfo_alias(const XCanBusDeviceInfo* info);

/**
 * @brief 获取通道号
 * @param info 设备信息指针（不可为 NULL）
 * @return 通道号（从 0 开始）。单通道设备或无通道信息时返回 0
 * @details 例如双通道 CAN 接口可能有通道 0 和 1
 */
int XCanBusDeviceInfo_channel(const XCanBusDeviceInfo* info);

/**
 * @brief 检查设备是否支持 CAN FD（灵活数据速率）
 * @param info 设备信息指针（不可为 NULL）
 * @return 支持返回 true，否则返回 false
 * @details 如果此信息不可用，返回 false
 */
bool XCanBusDeviceInfo_hasFlexibleDataRate(const XCanBusDeviceInfo* info);

/**
 * @brief 检查设备是否为虚拟设备
 * @param info 设备信息指针（不可为 NULL）
 * @return 虚拟设备返回 true，否则返回 false
 * @details 虚拟设备未连接到真实 CAN 硬件。如果此信息不可用，返回 false
 */
bool XCanBusDeviceInfo_isVirtual(const XCanBusDeviceInfo* info);

#ifdef __cplusplus
}
#endif

#endif // XCANBUSDEVICEINFO_H
