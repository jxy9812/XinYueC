/**
 * @file       XGpioDigitalOutput.h
 * @brief      GPIO 数字量输出（DO）通道适配器公开 API。
 * @details    把 Src/XPlatform/XGpio.h 的 GPIO 平台抽象挂接到
 *             XDigitalOutput 通道接口：write 直接调用 XGpio_writeActive
 *             驱动引脚，成功即在 write 调用栈内同步发射 writeDone(true)
 *             后返回 true（同步通道语义）；失败经保护接口报错后返回 false。
 *             底层 GPIO 句柄为借用语义（不拥有）：由调用方创建、open 并
 *             保证生命周期覆盖适配器对象，适配器 deinit 只清除引用，不关闭、
 *             不释放 GPIO。Windows 下无 win32 XGpio 后端，本文件整体编译为空。
 *             通道对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 */
#ifndef XGPIODIGITALOUTPUT_H
#define XGPIODIGITALOUTPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XDigitalOutput.h"
#include "XGpio.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON
#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(USE_STDPERIPH_DRIVER) || defined(ESP_PLATFORM)

/* ==================== 虚函数表定义（仅继承接口，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XGpioDigitalOutput)
XCLASS_DEFINE_EXTEND_END(XGpioDigitalOutput, XDigitalOutput)

/**
 * @brief XGpioDigitalOutput GPIO 数字量输出适配器。
 * @details 继承 XDigitalOutput 接口并重载 Write 虚槽（驱动引脚有效态）与
 *          EXClass_Deinit（清除借用指针）。本适配器无轮询需求，不使用定时器。
 *          成员禁止直接访问，一律经 API 操作。
 */
typedef struct XGpioDigitalOutput {
    XDigitalOutput m_class; /**< 第一个成员，基类 XDigitalOutput 接口，由 XClass 管理，禁止手工修改。 */
    XGpio* m_gpio;          /**< 借用的 GPIO 句柄（不拥有）；由调用方创建并保证生命周期覆盖本对象；NULL 表示未挂接。仅供实现使用。 */
} XGpioDigitalOutput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XGpioDigitalOutput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XGpioDigitalOutput_class_init(void);

/**
 * @brief 栈上初始化 GPIO 数字量输出适配器（未挂接状态）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；挂接底层资源用 XGpioDigitalOutput_attach，
 *       堆上一步创建用 XGpioDigitalOutput_create_ex。
 */
void XGpioDigitalOutput_init(XGpioDigitalOutput* obj);

/**
 * @brief 堆上创建并挂接 GPIO 数字量输出适配器。
 * @param gpio 借用的 GPIO 句柄（不拥有）；必须已由调用方 XGpio_create 创建
 *             （建议已 open 为输出方向）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param pollIntervalMs 保留参数（与 DI 适配器签名保持一致）；DO 写入为同步
 *                       直驱，本适配器不使用定时器，传任意值均被忽略。
 * @return 新适配器对象；内存分配失败或 gpio 为 NULL 返回 NULL。不再使用时
 *         必须调用 XClassDelete 释放。
 * @note 挂接本身不置 valid 标志；首次写入成功后置位。
 */
XGpioDigitalOutput* XGpioDigitalOutput_create_ex(XGpio* gpio, uint32_t pollIntervalMs);

/* ==================== 功能函数 ==================== */

/**
 * @brief 挂接（或重新挂接）底层 GPIO。
 * @param obj 目标适配器；传入 NULL 返回 false。
 * @param gpio 借用的 GPIO 句柄（不拥有）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param pollIntervalMs 保留参数（与 DI 适配器签名保持一致）；本适配器不使用
 *                       定时器，传任意值均被忽略。
 * @return 挂接成功返回 true；参数非法返回 false，原挂接状态保持不变。
 * @note 挂接本身不置 valid 标志；首次写入成功后置位。
 */
bool XGpioDigitalOutput_attach(XGpioDigitalOutput* obj, XGpio* gpio, uint32_t pollIntervalMs);

#endif /* 平台守卫：无 win32 XGpio 后端时编译为空 */

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGPIODIGITALOUTPUT_H */
