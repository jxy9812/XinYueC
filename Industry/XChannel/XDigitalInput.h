/**
 * @file       XDigitalInput.h
 * @brief      数字量输入通道（DI）接口公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场 DI 测点）。本类是通道
 *             接口：读取语义由子类（适配器）实现，如 GPIO 适配器读引脚
 *             电平、Modbus 适配器读离散量（FC02）。输入推送经 inputChanged
 *             信号；推送时机（中断/轮询）是实现细节，业务层无感知。通道
 *             对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 *             本接口只声明纯布尔语义，不出现电平/寄存器等传输概念；有效
 *             电平、地址映射等差异全部由适配器在构造/配置中吸收。
 */
#ifndef XDIGITALINPUT_H
#define XDIGITALINPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoChannel.h"
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* ==================== 虚函数表定义 ==================== */
XCLASS_DEFINE_BEGING(XDigitalInput)
XCLASS_DEFINE_ENUM(XDigitalInput, Read) = XCLASS_VTABLE_GET_SIZE(XIoChannel),
XCLASS_DEFINE_END(XDigitalInput)

/**
 * @brief XDigitalInput 数字量输入通道接口。
 * @details 基类 Read 虚槽注册的默认实现返回 false（接口未由任何适配器
 *          实现），子类适配器必须重载。
 */
typedef struct XDigitalInput {
    XIoChannel m_class; /**< 第一个成员，基类 XIoChannel，由 XClass 管理，禁止手工修改。 */
} XDigitalInput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XDigitalInput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XDigitalInput_class_init(void);

/**
 * @brief 栈上初始化数字量输入通道接口。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；适配器子类的 init 在设置自身虚表前调用本函数。
 */
void XDigitalInput_init(XDigitalInput* obj);

/**
 * @brief 堆上创建数字量输入通道接口对象。
 * @return 新通道对象；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete 释放。
 * @note 接口对象直接创建时读操作为空实现（返回 false）；实际使用应创建适配器子类。
 */
XDigitalInput* XDigitalInput_create(void);

/* ==================== 虚函数调度入口 ==================== */

/**
 * @brief 读取数字量输入当前值（只经虚表分派，不写业务逻辑）。
 * @param self 目标通道；传入 NULL 返回 false。
 * @param active 调用方提供的输出变量；成功时写入当前逻辑值（true=有效态），
 *               不能为 NULL。
 * @return 读取成功返回 true；通道未由适配器实现、未挂接底层传输或读取
 *         失败返回 false，*active 保持不变。
 * @note 该入口只做 ISNULL/虚表检查并分派到对象虚表槽位；实现见各适配器。
 */
bool XDigitalInput_read_base(XDigitalInput* self, bool* active);

/* ==================== 信号 ==================== */

/**
 * @brief inputChanged 信号：输入逻辑值变化时发射。
 * @param self 发射信号的通道；经 XSignal() 宏取地址连接，禁止直调。
 * @param active 变化后的逻辑值（true=有效态）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 推送时机由适配器决定（GPIO 可为中断/轮询，Modbus 为轮询比较）；
 *       首次挂接后适配器应至少推送一次初值，供业务层播种状态。
 */
void* XDigitalInput_inputChanged_signal(XDigitalInput* self, bool active);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDIGITALINPUT_H */
