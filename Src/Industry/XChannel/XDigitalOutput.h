/**
 * @file       XDigitalOutput.h
 * @brief      数字量输出通道（DO）接口公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场 DO 测点）。本类是通道
 *             接口：写入语义由子类（适配器）实现，如 GPIO 适配器驱动引脚、
 *             Modbus 适配器写线圈（FC05）。write 的返回值只表示"通道已
 *             受理"，不表示物理动作完成；完成确认经 writeDone 信号异步
 *             冒泡——同步通道（GPIO）在 write 返回前同步发射，异步通道
 *             （Modbus）在应答到达后发射。本接口只声明纯布尔语义；有效
 *             电平、地址映射等差异全部由适配器在构造/配置中吸收。
 *             通道对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 */
#ifndef XDIGITALOUTPUT_H
#define XDIGITALOUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoChannel.h"
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* ==================== 虚函数表定义 ==================== */
XCLASS_DEFINE_BEGING(XDigitalOutput)
XCLASS_DEFINE_ENUM(XDigitalOutput, Write) = XCLASS_VTABLE_GET_SIZE(XIoChannel),
XCLASS_DEFINE_END(XDigitalOutput)

/**
 * @brief XDigitalOutput 数字量输出通道接口。
 * @details 基类 Write 虚槽注册的默认实现返回 false（接口未由任何适配器
 *          实现），子类适配器必须重载。
 */
typedef struct XDigitalOutput {
    XIoChannel m_class; /**< 第一个成员，基类 XIoChannel，由 XClass 管理，禁止手工修改。 */
} XDigitalOutput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XDigitalOutput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XDigitalOutput_class_init(void);

/**
 * @brief 栈上初始化数字量输出通道接口。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；适配器子类的 init 在设置自身虚表前调用本函数。
 */
void XDigitalOutput_init(XDigitalOutput* obj);

/**
 * @brief 堆上创建数字量输出通道接口对象。
 * @return 新通道对象；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete 释放。
 * @note 接口对象直接创建时写操作为空实现（返回 false）；实际使用应创建适配器子类。
 */
XDigitalOutput* XDigitalOutput_create(void);

/* ==================== 虚函数调度入口 ==================== */

/**
 * @brief 写数字量输出（只经虚表分派，不写业务逻辑）。
 * @param self 目标通道；传入 NULL 返回 false。
 * @param active 要写入的逻辑值（true=有效态）。
 * @return 通道已受理返回 true（不代表物理动作完成，完成确认见 writeDone
 *         信号）；通道未由适配器实现、未挂接底层传输、底层忙碌或写入被
 *         拒绝返回 false，通道状态保持不变。
 * @note 该入口只做 ISNULL/虚表检查并分派到对象虚表槽位；实现见各适配器。
 */
bool XDigitalOutput_write_base(XDigitalOutput* self, bool active);

/* ==================== 信号 ==================== */

/**
 * @brief writeDone 信号：一次写入动作完成确认。
 * @param self 发射信号的通道；经 XSignal() 宏取地址连接，禁止直调。
 * @param ok true=底层确认写入成功；false=写入失败（超时/异常/拒绝）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 同步通道（GPIO）在 write 调用栈内同步发射；异步通道（Modbus）在
 *       应答到达或超时后发射。业务层若在 write 返回后才连接本信号，会
 *       错过同步通道的完成确认——应在发起动作前完成连接。
 */
void* XDigitalOutput_writeDone_signal(XDigitalOutput* self, bool ok);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDIGITALOUTPUT_H */
