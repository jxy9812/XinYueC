#ifndef XCANBUSDEVICE_PROTECTED_H
#define XCANBUSDEVICE_PROTECTED_H

#include "XCanBusDevice.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanBusDevice_Protected.h
 * @brief CAN 总线设备受保护接口头文件（供子类使用）
 * @details 提供 XCanBusDevice 子类需要访问的内部接口，包括状态设置、
 *          错误设置、帧队列管理和虚函数调用等。
 */

/******************************************************************************************
 * 受保护接口（供子类使用）
 ******************************************************************************************/

/**
 * @brief 设置设备状态（供子类使用）
 * @param dev 设备指针（非 NULL）
 * @param newState 新状态
 * @note 状态改变时会发射 stateChanged 信号
 */
void XCanBusDevice_setState(XCanBusDevice* dev, XCanBusDevice_State newState);

/**
 * @brief 设置设备错误（供子类使用）
 * @param dev 设备指针（非 NULL）
 * @param error 错误码
 * @param errorText 错误描述文本（可为 NULL，使用默认描述）
 * @note 设置错误时会发射 errorOccurred 信号
 */
void XCanBusDevice_setError(XCanBusDevice* dev, XCanBusDevice_Error error, const char* errorText);

/**
 * @brief 清除设备错误（供子类使用）
 * @param dev 设备指针（非 NULL）
 */
void XCanBusDevice_clearError(XCanBusDevice* dev);

/**
 * @brief 将接收到的帧入队（供子类使用）
 * @param dev 设备指针（非 NULL）
 * @param newFrames 新接收的帧列表（XVector<XCanBusFrame*>）
 * @note 帧会被深拷贝后入队，并发射 framesReceived 信号
 */
void XCanBusDevice_enqueueReceivedFrames(XCanBusDevice* dev, const XVector* newFrames);

/**
 * @brief 将待发送帧入队（供子类使用）
 * @param dev 设备指针（非 NULL）
 * @param newFrame 待发送的帧
 * @note 帧会被深拷贝后入队
 */
void XCanBusDevice_enqueueOutgoingFrame(XCanBusDevice* dev, const XCanBusFrame* newFrame);

/**
 * @brief 从发送队列取出一个帧（供子类使用）
 * @param dev 设备指针（非 NULL）
 * @return 出队的帧指针，调用者负责释放；无帧返回 NULL
 */
XCanBusFrame* XCanBusDevice_dequeueOutgoingFrame(XCanBusDevice* dev);

/**
 * @brief 检查发送队列是否有待发送帧（供子类使用）
 * @param dev 设备指针（非 NULL）
 * @return 有待发送帧返回 true，否则返回 false
 */
bool XCanBusDevice_hasOutgoingFrames(const XCanBusDevice* dev);

/******************************************************************************************
 * 静态工厂方法（供子类使用）
 ******************************************************************************************/

/**
 * @brief 创建设备信息（简化版本，供子类使用）
 * @param info 输出参数，设备信息结构体（需已初始化）
 * @param plugin 插件名称
 * @param name 接口名称
 * @param isVirtual 是否为虚拟设备
 * @param isFlexibleDataRateCapable 是否支持 CAN FD
 */
void XCanBusDevice_createDeviceInfo(XCanBusDeviceInfo* info,
    const char* plugin, const char* name,
    bool isVirtual, bool isFlexibleDataRateCapable);

/**
 * @brief 创建设备信息（完整版本，供子类使用）
 * @param info 输出参数，设备信息结构体（需已初始化）
 * @param plugin 插件名称
 * @param name 接口名称
 * @param serialNumber 序列号
 * @param description 描述
 * @param alias 别名
 * @param channel 通道号
 * @param isVirtual 是否为虚拟设备
 * @param isFlexibleDataRateCapable 是否支持 CAN FD
 */
void XCanBusDevice_createDeviceInfo_full(XCanBusDeviceInfo* info,
    const char* plugin, const char* name,
    const char* serialNumber, const char* description,
    const char* alias, int channel,
    bool isVirtual, bool isFlexibleDataRateCapable);

/******************************************************************************************
 * 虚函数调用接口
 ******************************************************************************************/

/**
 * @brief 打开设备（虚函数调用）
 * @param dev 设备指针（非 NULL）
 * @return 成功返回 true，失败返回 false
 * @note 通过虚函数表调用，由子类实现具体逻辑
 */
bool XCanBusDevice_open_base(XCanBusDevice* dev);

/**
 * @brief 关闭设备（虚函数调用）
 * @param dev 设备指针（非 NULL）
 * @note 通过虚函数表调用，由子类实现具体逻辑
 */
void XCanBusDevice_close_base(XCanBusDevice* dev);

#ifdef __cplusplus
}
#endif

#endif // XCANBUSDEVICE_PROTECTED_H
