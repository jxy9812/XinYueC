/**
 * @file       XModbusDigitalInput.h
 * @brief      Modbus 离散量输入（FC02）轮询通道适配器公开 API。
 * @details    把 XModbusClient 的异步读请求挂接到 XDigitalInput 通道接口：
 *             适配器以 XObject 自带定时器周期发送 FC02（读离散输入，对应
 *             XModbusDiscreteInputs 数据单元）单点轮询，应答经 finished
 *             信号回收：值翻转时发射 XDigitalInput_inputChanged 信号，首个
 *             成功应答拍推送一次初值；应答失败/超时经保护接口报错并清除
 *             valid，恢复成功后清错误并置 valid。底层 XModbusClient 为借用
 *             语义（不拥有）：由调用方创建、连接并保证生命周期覆盖适配器
 *             对象；适配器负责自己发出的 XModbusReply 的释放（deleteLater）。
 *             通道对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 */
#ifndef XMODBUSDIGITALINPUT_H
#define XMODBUSDIGITALINPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XDigitalInput.h"
#include "XModbusClient.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

/* ==================== 虚函数表定义（仅继承接口，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XModbusDigitalInput)
XCLASS_DEFINE_EXTEND_END(XModbusDigitalInput, XDigitalInput)

/**
 * @brief XModbusDigitalInput Modbus 离散量输入（FC02）轮询适配器。
 * @details 继承 XDigitalInput 接口并重载 Read 虚槽（读取最近一次轮询值）、
 *          XObject 的 TimerEvent 虚槽（周期发起 FC02 轮询）与 EXClass_Deinit
 *          （停定时器、回收在途应答）。成员禁止直接访问，一律经 API 操作。
 */
typedef struct XModbusDigitalInput {
    XDigitalInput m_class;        /**< 第一个成员，基类 XDigitalInput 接口，由 XClass 管理，禁止手工修改。 */
    XModbusClient* m_client;      /**< 借用的 Modbus 主站（不拥有）；由调用方创建并保证生命周期覆盖本对象；NULL 表示未挂接。仅供实现使用。 */
    int m_serverAddress;          /**< 从站地址（1..247）。仅供实现使用。 */
    int m_address;                /**< 离散输入地址（0..65535）。仅供实现使用。 */
    XTimerId m_pollTimer;         /**< 轮询定时器 id（XObject 自带定时器）；XTIMER_INVALID_ID 表示未启动。仅供实现使用。 */
    XModbusReply* m_pendingReply; /**< 在途轮询应答（由本对象负责释放）；NULL 表示无在途请求。仅供实现使用。 */
    bool m_lastValue;             /**< 上次推送的逻辑值（true=有效态）。仅供实现使用。 */
    bool m_hasLastValue;          /**< 是否已推送过初值；false 表示首拍必须推送一次初值。仅供实现使用。 */
} XModbusDigitalInput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XModbusDigitalInput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XModbusDigitalInput_class_init(void);

/**
 * @brief 栈上初始化 Modbus 离散量输入适配器（未挂接状态）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；挂接主站用 XModbusDigitalInput_attach，
 *       堆上一步创建用 XModbusDigitalInput_create_ex。
 */
void XModbusDigitalInput_init(XModbusDigitalInput* obj);

/**
 * @brief 堆上创建并挂接 Modbus 离散量输入适配器。
 * @param client 借用的 Modbus 主站（不拥有）；不能为 NULL；生命周期须覆盖
 *               适配器对象。
 * @param serverAddress 从站地址；取值 1..247（离散量轮询无广播语义，0 非法）。
 * @param address 离散输入地址；取值 0..65535。
 * @param pollIntervalMs 轮询周期（毫秒）；必须大于 0。
 * @return 新适配器对象；内存分配失败或参数非法返回 NULL。不再使用时必须
 *         调用 XClassDelete 释放。
 * @note 成功即启动周期轮询；首个成功应答拍推送一次初值。
 */
XModbusDigitalInput* XModbusDigitalInput_create_ex(XModbusClient* client, int serverAddress,
                                                   int address, uint32_t pollIntervalMs);

/* ==================== 功能函数 ==================== */

/**
 * @brief 挂接（或重新挂接）Modbus 主站并启动周期轮询。
 * @param obj 目标适配器；传入 NULL 返回 false。
 * @param client 借用的 Modbus 主站（不拥有）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param serverAddress 从站地址；取值 1..247。
 * @param address 离散输入地址；取值 0..65535。
 * @param pollIntervalMs 轮询周期（毫秒）；必须大于 0。
 * @return 挂接成功返回 true；参数非法或定时器启动失败返回 false，原挂接
 *         状态保持不变（若此前已挂接则旧定时器已被停止）。
 * @note 重复挂接按新参数重启轮询并清空上次推送值；在途旧应答被丢弃（不再
 *       发射 inputChanged）；挂接本身不置 valid，首个成功应答拍才置位。
 */
bool XModbusDigitalInput_attach(XModbusDigitalInput* obj, XModbusClient* client,
                                int serverAddress, int address, uint32_t pollIntervalMs);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSDIGITALINPUT_H */
