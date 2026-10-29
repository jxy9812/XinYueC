/**
 * @file       XAnalogInput.h
 * @brief      模拟量输入通道（AI）接口公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场 AI 测点）。本类是通道
 *             接口：读取语义由子类（适配器）实现，如 Modbus 适配器轮询
 *             输入/保持寄存器（FC04/FC03）、ADC 适配器采样引脚。接口一律
 *             使用 **int32_t 定点工程值**（如 253 表示 25.3°C，小数位属于
 *             站点标定信息）；寄存器原始码与工程值之间的标度换算全部下沉
 *             在适配器配置中，业务层不接触"原始码"。输入推送经
 *             inputChanged 信号；推送时机与变化死区（deadband）由适配器
 *             配置决定。通道对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 */
#ifndef XANALOGINPUT_H
#define XANALOGINPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoChannel.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* ==================== 虚函数表定义 ==================== */
XCLASS_DEFINE_BEGING(XAnalogInput)
XCLASS_DEFINE_ENUM(XAnalogInput, Read) = XCLASS_VTABLE_GET_SIZE(XIoChannel),
XCLASS_DEFINE_END(XAnalogInput)

/**
 * @brief XAnalogInput 模拟量输入通道接口。
 * @details 基类 Read 虚槽注册的默认实现返回 false（接口未由任何适配器
 *          实现），子类适配器必须重载。
 */
typedef struct XAnalogInput {
    XIoChannel m_class; /**< 第一个成员，基类 XIoChannel，由 XClass 管理，禁止手工修改。 */
} XAnalogInput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XAnalogInput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XAnalogInput_class_init(void);

/**
 * @brief 栈上初始化模拟量输入通道接口。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；适配器子类的 init 在设置自身虚表前调用本函数。
 */
void XAnalogInput_init(XAnalogInput* obj);

/**
 * @brief 堆上创建模拟量输入通道接口对象。
 * @return 新通道对象；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete 释放。
 * @note 接口对象直接创建时读操作为空实现（返回 false）；实际使用应创建适配器子类。
 */
XAnalogInput* XAnalogInput_create(void);

/* ==================== 虚函数调度入口 ==================== */

/**
 * @brief 读取模拟量输入当前工程值（只经虚表分派，不写业务逻辑）。
 * @param self 目标通道；传入 NULL 返回 false。
 * @param value 调用方提供的输出变量；成功时写入当前定点工程值，不能为 NULL。
 * @return 读取成功返回 true；通道未由适配器实现、未挂接底层传输或读取
 *         失败返回 false，*value 保持不变。
 * @note 该入口只做 ISNULL/虚表检查并分派到对象虚表槽位；实现见各适配器。
 */
bool XAnalogInput_read_base(XAnalogInput* self, int32_t* value);

/* ==================== 信号 ==================== */

/**
 * @brief inputChanged 信号：输入工程值变化时发射。
 * @param self 发射信号的通道；经 XSignal() 宏取地址连接，禁止直调。
 * @param value 变化后的定点工程值。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 推送时机与死区阈值由适配器配置决定（如 Modbus 轮询周期、变化
 *       死区）；首次挂接后适配器应至少推送一次初值，供业务层播种状态。
 */
void* XAnalogInput_inputChanged_signal(XAnalogInput* self, int32_t value);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XANALOGINPUT_H */
