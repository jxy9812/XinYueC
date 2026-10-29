/**
 * @file       XModbusAnalogOutput.h
 * @brief      Modbus 写单保持寄存器（FC06）通道适配器公开 API。
 * @details    把 XModbusClient 的异步写请求挂接到 XAnalogOutput 通道接口：
 *             write 把定点工程值按 raw = (value - offset) * scaleDen / scaleNum
 *             反向换算为 16 位原始码（int64 中间量防溢出；余数向最近值取整；
 *             scaleNum 为 0 视为 1；结果钳位到 0..65535），组装单保持寄存器
 *             数据单元（XModbusHoldingRegisters，quantity=1）发出 FC06，不等
 *             应答即返回 true（受理语义）；应答到达/超时后经 finished 信号
 *             回收，成功发射 writeDone(true) 并置 valid，失败报错并发射
 *             writeDone(false)。从站地址 0 按广播处理：受理即完成（对齐
 *             XVfdModbusLink 广播语义）。同一适配器同一时刻只允许一个在途
 *             写应答，在途期间的新写入按 Busy 拒绝。底层 XModbusClient 为
 *             借用语义（不拥有）；适配器负责自己发出的 XModbusReply 的释放
 *             （deleteLater）。通道对象禁止拷贝/移动语义（见 XIoChannel.h
 *             @details）。
 */
#ifndef XMODBUSANALOGOUTPUT_H
#define XMODBUSANALOGOUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XAnalogOutput.h"
#include "XModbusClient.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON

/* ==================== 虚函数表定义（仅继承接口，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XModbusAnalogOutput)
XCLASS_DEFINE_EXTEND_END(XModbusAnalogOutput, XAnalogOutput)

/**
 * @brief XModbusAnalogOutput Modbus 写单保持寄存器（FC06）适配器。
 * @details 继承 XAnalogOutput 接口并重载 Write 虚槽（工程值反换算后发 FC06）
 *          与 EXClass_Deinit（回收在途应答）。本适配器为写入驱动，不使用
 *          定时器。成员禁止直接访问，一律经 API 操作。
 */
typedef struct XModbusAnalogOutput {
    XAnalogOutput m_class;        /**< 第一个成员，基类 XAnalogOutput 接口，由 XClass 管理，禁止手工修改。 */
    XModbusClient* m_client;      /**< 借用的 Modbus 主站（不拥有）；由调用方创建并保证生命周期覆盖本对象；NULL 表示未挂接。仅供实现使用。 */
    int m_serverAddress;          /**< 从站地址（0..247；0=广播，受理即完成）。仅供实现使用。 */
    int m_address;                /**< 保持寄存器地址（0..65535）。仅供实现使用。 */
    int32_t m_scaleNum;           /**< 标度分子：与 XModbusAnalogInput 同一套标度约定，raw = (value - offset) * scaleDen / scaleNum。仅供实现使用。 */
    int32_t m_scaleDen;           /**< 标度分母；0 视为 1（作为乘数）。仅供实现使用。 */
    int32_t m_offset;             /**< 工程值偏移量（定点工程值单位）。仅供实现使用。 */
    XModbusReply* m_pendingReply; /**< 在途写应答（由本对象负责释放）；NULL 表示无在途请求。仅供实现使用。 */
} XModbusAnalogOutput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XModbusAnalogOutput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XModbusAnalogOutput_class_init(void);

/**
 * @brief 栈上初始化 Modbus 写保持寄存器适配器（未挂接状态）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；挂接主站用 XModbusAnalogOutput_attach，
 *       堆上一步创建用 XModbusAnalogOutput_create_ex。
 */
void XModbusAnalogOutput_init(XModbusAnalogOutput* obj);

/**
 * @brief 堆上创建并挂接 Modbus 写保持寄存器适配器。
 * @param client 借用的 Modbus 主站（不拥有）；不能为 NULL；生命周期须覆盖
 *               适配器对象。
 * @param serverAddress 从站地址；取值 0..247（0=广播，write 受理即完成）。
 * @param address 保持寄存器地址；取值 0..65535。
 * @param scaleNum 标度分子（反向换算分母；0 视为 1）。
 * @param scaleDen 标度分母（反向换算分子）。
 * @param offset 工程值偏移量（定点工程值单位）。
 * @return 新适配器对象；内存分配失败或参数非法返回 NULL。不再使用时必须
 *         调用 XClassDelete 释放。
 * @note 挂接本身不置 valid 标志；首次写应答成功后置位。
 */
XModbusAnalogOutput* XModbusAnalogOutput_create_ex(XModbusClient* client, int serverAddress,
                                                   int address, int32_t scaleNum,
                                                   int32_t scaleDen, int32_t offset);

/* ==================== 功能函数 ==================== */

/**
 * @brief 挂接（或重新挂接）Modbus 主站。
 * @param obj 目标适配器；传入 NULL 返回 false。
 * @param client 借用的 Modbus 主站（不拥有）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param serverAddress 从站地址；取值 0..247（0=广播）。
 * @param address 保持寄存器地址；取值 0..65535。
 * @param scaleNum 标度分子（反向换算分母；0 视为 1）。
 * @param scaleDen 标度分母（反向换算分子）。
 * @param offset 工程值偏移量。
 * @return 挂接成功返回 true；参数非法返回 false，原挂接状态保持不变。
 * @note 重复挂接时在途旧应答被丢弃（不再发射 writeDone）；挂接本身不置
 *       valid，首次写应答成功后才置位。
 */
bool XModbusAnalogOutput_attach(XModbusAnalogOutput* obj, XModbusClient* client,
                                int serverAddress, int address,
                                int32_t scaleNum, int32_t scaleDen, int32_t offset);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSANALOGOUTPUT_H */
