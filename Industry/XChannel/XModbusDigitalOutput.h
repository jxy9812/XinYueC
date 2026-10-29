/**
 * @file       XModbusDigitalOutput.h
 * @brief      Modbus 写单线圈（FC05）通道适配器公开 API。
 * @details    把 XModbusClient 的异步写请求挂接到 XDigitalOutput 通道接口：
 *             write 组装单线圈数据单元（XModbusCoils，quantity=1，值由协议层
 *             自动映射为 0xFF00/0x0000）发出 FC05，不等应答即返回 true
 *             （受理语义）；应答到达/超时后经 finished 信号回收，成功发射
 *             writeDone(true) 并置 valid，失败报错并发射 writeDone(false)。
 *             从站地址 0 按广播处理：受理即完成（对齐 XVfdModbusLink 广播
 *             语义）。同一适配器同一时刻只允许一个在途写应答，在途期间的新
 *             写入按 Busy 拒绝。底层 XModbusClient 为借用语义（不拥有）；
 *             适配器负责自己发出的 XModbusReply 的释放（deleteLater）。
 *             通道对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 */
#ifndef XMODBUSDIGITALOUTPUT_H
#define XMODBUSDIGITALOUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XDigitalOutput.h"
#include "XModbusClient.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

/* ==================== 虚函数表定义（仅继承接口，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XModbusDigitalOutput)
XCLASS_DEFINE_EXTEND_END(XModbusDigitalOutput, XDigitalOutput)

/**
 * @brief XModbusDigitalOutput Modbus 写单线圈（FC05）适配器。
 * @details 继承 XDigitalOutput 接口并重载 Write 虚槽（发 FC05）与
 *          EXClass_Deinit（回收在途应答）。本适配器为写入驱动，不使用定时器。
 *          成员禁止直接访问，一律经 API 操作。
 */
typedef struct XModbusDigitalOutput {
    XDigitalOutput m_class;       /**< 第一个成员，基类 XDigitalOutput 接口，由 XClass 管理，禁止手工修改。 */
    XModbusClient* m_client;      /**< 借用的 Modbus 主站（不拥有）；由调用方创建并保证生命周期覆盖本对象；NULL 表示未挂接。仅供实现使用。 */
    int m_serverAddress;          /**< 从站地址（0..247；0=广播，受理即完成）。仅供实现使用。 */
    int m_address;                /**< 线圈地址（0..65535）。仅供实现使用。 */
    XModbusReply* m_pendingReply; /**< 在途写应答（由本对象负责释放）；NULL 表示无在途请求。仅供实现使用。 */
} XModbusDigitalOutput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XModbusDigitalOutput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XModbusDigitalOutput_class_init(void);

/**
 * @brief 栈上初始化 Modbus 写单线圈适配器（未挂接状态）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；挂接主站用 XModbusDigitalOutput_attach，
 *       堆上一步创建用 XModbusDigitalOutput_create_ex。
 */
void XModbusDigitalOutput_init(XModbusDigitalOutput* obj);

/**
 * @brief 堆上创建并挂接 Modbus 写单线圈适配器。
 * @param client 借用的 Modbus 主站（不拥有）；不能为 NULL；生命周期须覆盖
 *               适配器对象。
 * @param serverAddress 从站地址；取值 0..247（0=广播，write 受理即完成）。
 * @param address 线圈地址；取值 0..65535。
 * @return 新适配器对象；内存分配失败或参数非法返回 NULL。不再使用时必须
 *         调用 XClassDelete 释放。
 * @note 挂接本身不置 valid 标志；首次写应答成功后置位。
 */
XModbusDigitalOutput* XModbusDigitalOutput_create_ex(XModbusClient* client, int serverAddress,
                                                     int address);

/* ==================== 功能函数 ==================== */

/**
 * @brief 挂接（或重新挂接）Modbus 主站。
 * @param obj 目标适配器；传入 NULL 返回 false。
 * @param client 借用的 Modbus 主站（不拥有）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param serverAddress 从站地址；取值 0..247（0=广播）。
 * @param address 线圈地址；取值 0..65535。
 * @return 挂接成功返回 true；参数非法返回 false，原挂接状态保持不变。
 * @note 重复挂接时在途旧应答被丢弃（不再发射 writeDone）；挂接本身不置
 *       valid，首次写应答成功后才置位。
 */
bool XModbusDigitalOutput_attach(XModbusDigitalOutput* obj, XModbusClient* client,
                                 int serverAddress, int address);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSDIGITALOUTPUT_H */
