/**
 * @file       XGpioDigitalInput.h
 * @brief      GPIO 数字量输入（DI）通道适配器公开 API。
 * @details    把 Src/XPlatform/XGpio.h 的 GPIO 平台抽象挂接到
 *             XDigitalInput 通道接口：适配器以 XObject 自带定时器周期调用
 *             XGpio_readActive 轮询引脚有效态，值翻转时发射
 *             XDigitalInput_inputChanged 信号，并在首个成功轮询拍至少推送
 *             一次初值供业务层播种状态。底层 GPIO 句柄为借用语义（不拥有）：
 *             由调用方创建、open 并保证生命周期覆盖适配器对象，适配器
 *             deinit 只停定时器，不关闭、不释放 GPIO。
 *             Windows 下无 win32 XGpio 后端，本文件整体编译为空。
 *             通道对象禁止拷贝/移动语义（见 XIoChannel.h @details）。
 */
#ifndef XGPIODIGITALINPUT_H
#define XGPIODIGITALINPUT_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XDigitalInput.h"
#include "XGpio.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON
#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(USE_STDPERIPH_DRIVER) || defined(ESP_PLATFORM)

/* ==================== 虚函数表定义（仅继承接口，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XGpioDigitalInput)
XCLASS_DEFINE_EXTEND_END(XGpioDigitalInput, XDigitalInput)

/**
 * @brief XGpioDigitalInput GPIO 数字量输入适配器。
 * @details 继承 XDigitalInput 接口并重载 Read 虚槽（读引脚有效态）、
 *          XObject 的 TimerEvent 虚槽（周期轮询）与 EXClass_Deinit
 *          （停定时器、清除借用指针）。成员禁止直接访问，一律经 API 操作。
 */
typedef struct XGpioDigitalInput {
    XDigitalInput m_class; /**< 第一个成员，基类 XDigitalInput 接口，由 XClass 管理，禁止手工修改。 */
    XGpio* m_gpio;         /**< 借用的 GPIO 句柄（不拥有）；由调用方创建并保证生命周期覆盖本对象；NULL 表示未挂接。仅供实现使用。 */
    XTimerId m_pollTimer;  /**< 轮询定时器 id（XObject 自带定时器）；XTIMER_INVALID_ID 表示未启动。仅供实现使用。 */
    bool m_lastValue;      /**< 上次推送的逻辑值（true=有效态）。仅供实现使用。 */
    bool m_hasLastValue;   /**< 是否已推送过初值；false 表示首拍必须推送一次初值。仅供实现使用。 */
} XGpioDigitalInput;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XGpioDigitalInput 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XGpioDigitalInput_class_init(void);

/**
 * @brief 栈上初始化 GPIO 数字量输入适配器（未挂接状态）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；挂接底层资源用 XGpioDigitalInput_attach，
 *       堆上一步创建用 XGpioDigitalInput_create_ex。
 */
void XGpioDigitalInput_init(XGpioDigitalInput* obj);

/**
 * @brief 堆上创建并挂接 GPIO 数字量输入适配器。
 * @param gpio 借用的 GPIO 句柄（不拥有）；必须已由调用方 XGpio_create 创建
 *             （建议已 open）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param pollIntervalMs 轮询周期（毫秒）；必须大于 0。
 * @return 新适配器对象；内存分配失败、gpio 为 NULL 或 pollIntervalMs 为 0
 *         返回 NULL。不再使用时必须调用 XClassDelete 释放。
 * @note 成功即启动周期轮询定时器；首个成功轮询拍推送一次初值。
 */
XGpioDigitalInput* XGpioDigitalInput_create_ex(XGpio* gpio, uint32_t pollIntervalMs);

/* ==================== 功能函数 ==================== */

/**
 * @brief 挂接（或重新挂接）底层 GPIO 并启动周期轮询。
 * @param obj 目标适配器；传入 NULL 返回 false。
 * @param gpio 借用的 GPIO 句柄（不拥有）；不能为 NULL；生命周期须覆盖适配器对象。
 * @param pollIntervalMs 轮询周期（毫秒）；必须大于 0。
 * @return 挂接成功返回 true；参数非法或定时器启动失败返回 false，原挂接
 *         状态保持不变（若此前已挂接则旧定时器已被停止）。
 * @note 重复挂接按新参数重启轮询并清空上次推送值（首拍重新推送初值）；
 *       挂接本身不置 valid 标志，首个成功轮询拍才置位。
 */
bool XGpioDigitalInput_attach(XGpioDigitalInput* obj, XGpio* gpio, uint32_t pollIntervalMs);

#endif /* 平台守卫：无 win32 XGpio 后端时编译为空 */

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_GPIO_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGPIODIGITALINPUT_H */
