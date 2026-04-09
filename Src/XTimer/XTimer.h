#ifndef XTIMER_H
#define XTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerBase.h"
/**
* @brief XTimer虚函数表大小宏定义
* 基于XTimerTimeWheel的虚函数表大小
*/
#define XTIMER_VTABLE_SIZE (XTIMERTIMEWHEEL_VTABLE_SIZE)
typedef struct XTimer
{
	XTimerBase m_class;
} XTimer;
// === 类初始化与构造相关接口 ===
/**
* @brief 初始化XTimer类的虚函数表
* @return 成功返回虚函数表指针，失败返回NULL
*/
XVtable* XTimer_class_init();
/**
* @brief 创建XTimer实例
* @return 成功返回XTimer实例指针，失败返回NULL
*/
XTimer* XTimer_create();
/**
* @brief 初始化XTimer实例
* @param timer 要初始化的XTimer实例指针
*/
void XTimer_init(XTimer* timer);
// === 销毁相关接口 ===
/**
* @brief 基类删除函数宏定义
* 复用XTimerBase的删除函数
*/
#define XTimer_delete_base			XTimerBase_delete_base
// === 启动与停止相关接口 ===
/**
* @brief 启动定时器的基类实现宏
* 复用XTimerBase的start_base方法
*/
#define XTimer_start_base			XTimerBase_start_base
/**
* @brief 停止定时器的基类实现宏
* 复用XTimerBase的stop_base方法
*/
#define XTimer_stop_base			XTimerBase_stop_base
// === 属性设置相关接口 ===
#define XTimer_setSingleShot		XTimerBase_setSingleShot
/**
* @brief 设置定时器超时时间的基类实现宏
* 复用XTimerBase的setTimeout_base方法
*/
#define XTimer_setTimeout_base		XTimerBase_setTimeout_base
/**
* @brief 设置定时器周期间隔的基类实现宏
* 复用XTimerBase的setInterval_base方法
*/
#define XTimer_setInterval_base		XTimerBase_setInterval_base
/**
* @brief 设置用户自定义数据的宏
* 复用XTimerBase的setUserData_base方法
*/
#define XTimer_setUserData			XTimerBase_setUserData_base
/**
* @brief 设置定时器回调函数的宏
* 复用XTimerBase的setTimerCallback_base方法
*/
#define XTimer_setTimerCallback		XTimerBase_setTimerCallback_base
/**
* @brief 设置定时器所属组的宏
* 复用XTimerBase的setTimerId方法（将组标识作为timerId存储）
*/
#define XTimer_setTimerId				XTimerBase_setTimerId
// === 属性获取相关接口 ===
#define XTimer_isSingleShot			XTimerBase_isSingleShot
/**
* @brief 判断定时器是否为周期性任务的宏
* 复用XTimerBase的isPeriodic方法
* @return true：非周期性（单次），false：周期性
*/
#define XTimer_isPeriodic			XTimerBase_isPeriodic
/**
* @brief 判断定时器是否正在运行的宏
* 复用XTimerBase的isRunning方法
* @return true：运行中，false：已停止
*/
#define XTimer_isRunning			XTimerBase_isRunning
/**
* @brief 获取定时器超时时间的宏
* 复用XTimerBase的getTimeout方法
* @return 超时时间（毫秒）
*/
#define XTimer_timeout			XTimerBase_timeout
/**
* @brief 获取定时器周期间隔的宏
* 复用XTimerBase的getInterval方法
* @return 周期间隔（毫秒）
*/
#define XTimer_interval			XTimerBase_interval
/**
* @brief 获取定时器所属组的宏
* 复用XTimerBase的getTimerId方法（组标识存储在timerId中）
* @return 所属组标识
*/
#define XTimer_timerId			XTimerBase_timerId
/**
* @brief 获取用户自定义数据的宏
* 复用XTimerBase的getUserData方法
* @return 用户数据指针
*/
#define XTimer_getUserData			XTimerBase_getUserData
// === 超时处理相关接口 ===
/**
* @brief 定时器超时处理的基类实现宏
* 复用XTimerBase的out_base方法
*/
#define XTimer_out_base				XTimerBase_out_base
/**
* @brief 定时器超时触发信号
* 用于触发定时器超时相关的信号回调
* @param timer 触发超时的XTimer实例指针
* @return 无实际返回值（内部信号处理）
*/
void* XTimer_timeout_signal(XTimer* timer);
/**
* @brief 连接定时器超时信号到槽函数
* 绑定定时器超时事件到指定接收者的槽函数
* @param timer 要连接的XTimer实例指针
* @param receiver 信号接收者对象指针
* @param slot_func 要调用的槽函数
* @param type 连接类型（XConnectionType）
*/
void XTimer_callOnTimeout(XTimer* timer, XObject* receiver, XSlotFunc slot_func, XConnectionType type);
/**
* @brief 创建单次触发的定时器
* 定时msec毫秒后触发一次槽函数，之后自动释放
* @param msec 超时时间（毫秒）
* @param receiver 信号接收者对象指针
* @param slot_func 要调用的槽函数
* @param type 连接类型（XConnectionType）
*/
void XTimer_singleShot(size_t msec, XObject* receiver, XSlotFunc slot_func, XConnectionType type);
#ifdef __cplusplus
}
#endif
#endif // !XTimers_H