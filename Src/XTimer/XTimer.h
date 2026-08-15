#ifndef XTIMER_H
#define XTIMER_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XObject.h"
#include"XTimerData.h"
/**
* @brief XTimerData虚函数表大小宏定义
* 基于XObject的虚函数表大小扩展
*/
#define XTIMER_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimer))
/**
* @brief XTimerData虚函数表枚举定义开始
* 用于标识虚函数表中的函数索引
*/
XCLASS_DEFINE_BEGING(XTimer)
XCLASS_DEFINE_ENUM(XTimer, Start) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XTimer, Stop),
XCLASS_DEFINE_END(XTimer)
typedef struct XTimer
{
	XObject m_class;
	uint32_t m_isRun : 1;				 ///< 定时器是否正在运行
	uint32_t m_firstTrigger : 1;		//首次触发
	XTimerData m_timerData;
	XTimerType m_type;
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
XTimer* XTimer_create_ex(XMemoryType memory);
/**
* @brief 初始化XTimer实例
* @param timer 要初始化的XTimer实例指针
*/
void XTimer_init(XTimer* timer);
// === 销毁相关接口 ===
/**
* @brief 基类删除函数宏定义
* 复用XTimer的删除函数
*/
#define XTimer_deleteLater			XObject_deleteLater
// === 启动与停止相关接口 ===
/**
* @brief 启动定时器（基类实现）
* @param timer XTimerData实例指针
*/
void XTimer_start_base(XTimer* timer);
/**
* @brief 停止定时器（基类实现）
* @param timer XTimer实例指针
*/
void XTimer_stop_base(XTimer* timer);
// === 属性设置相关接口 ===
/**
* @brief 设置定时器超时时间（基类实现）
* @param timer XTimer实例指针
* @param value 超时时间（毫秒）
*/
void XTimer_setTimeout(XTimer* timer, size_t value);
/**
* @brief 设置定时器周期间隔（基类实现）
* @param timer XTimer实例指针
* @param value 周期间隔（毫秒）
*/
void XTimer_setInterval(XTimer* timer, size_t value);
/**
* @brief 设置用户自定义数据（基类实现）
* @param timer XTimer实例指针
* @param userData 用户数据指针
*/
void XTimer_setUserData(XTimer* timer, void* userData);
/**
* @brief 设置定时器回调函数（基类实现）
* @param timer XTimer实例指针
* @param callback 回调函数指针
*/
void XTimer_setTimerCallback(XTimer* timer, XTimerCallback callback);
/**
* @brief 设置定时器ID
* @param timer XTimer实例指针
* @param timerId 要设置的ID值
*/
void XTimer_setTimerId(XTimer* timer, size_t timerId);
/**
* @brief 设置定时器是否自动释放
* @param timer XTimer实例指针
* @param del true：自动释放，false：不自动释放
*/
void XTimer_setAutoDelete(XTimer* timer, bool del);
/**
* @brief 设置定时器是否为单次触发模式
* @param timer XTimer实例指针
* @param ss true：单次触发，false：周期性触发
*/
void XTimer_setSingleShot(XTimer* timer, bool ss);
// === 属性获取相关接口 ===
bool XTimer_isSingleShot(XTimer* timer);
/**
* @brief 判断定时器是否为周期性任务
* @param timer XTimer实例指针
* @return true：非周期性（单次），false：周期性
*/
bool XTimer_isPeriodic(XTimer* timer);
/**
* @brief 判断定时器是否正在运行
* @param timer XTimer实例指针
* @return true：运行中，false：已停止
*/
bool XTimer_isRunning(XTimer* timer);
/**
* @brief 获取定时器超时时间
* @param timer XTimer实例指针
* @return 超时时间（毫秒）
*/
size_t XTimer_timeout(XTimer* timer);
/**
* @brief 获取定时器周期间隔
* @param timer XTimer实例指针
* @return 周期间隔（毫秒）
*/
size_t XTimer_interval(XTimer* timer);
/**
* @brief 获取定时器ID
* @param timer XTimer实例指针
* @return 定时器ID
*/
size_t XTimer_timerId(XTimer* timer);
/**
* @brief 获取用户自定义数据
* @param timer XTimer实例指针
* @return 用户数据指针
*/
void* XTimer_userData(XTimer* timer);
/**
* @brief 判断定时器是否自动释放
* @param timer XTimer实例指针
* @return true：自动释放，false：不自动释放
*/
bool   XTimer_isAutoDelete(XTimer* timer);
// === 超时处理相关接口 ===
/**
* @brief 定时器超时处理函数（基类实现）
* @param timer XTimer实例指针
*/
void XTimer_out(XTimer* timer);

void XTimer_setTimerType(XTimer* timer,XTimerType type);
XTimerType XTimer_timerType(XTimer* timer);
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
void XTimer_callOnTimeout1(XTimer* timer, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type);
/**
* @brief 连接定时器超时信号到槽函数
* 绑定定时器超时事件到指定接收者的槽函数
* @param timer 要连接的XTimer实例指针
* @param slot_func 要调用的槽函数
*/
void XTimer_callOnTimeout2(XTimer* timer, XSlotFunc2 slot_func);
/**
* @brief 创建单次触发的定时器
* 定时msec毫秒后触发一次槽函数，之后自动释放
* @param msec 超时时间（毫秒）
* @param receiver 信号接收者对象指针
* @param slot_func 要调用的槽函数
* @param type 连接类型（XConnectionType）
*/
void XTimer_singleShot1(size_t msec, XObject* receiver, XSlotFunc1 slot_func, XConnectionType type);
/**
* @brief 创建单次触发的定时器
* 定时msec毫秒后触发一次槽函数，之后自动释放
* @param msec 超时时间（毫秒）
* @param slot_func 要调用的槽函数
*/
void XTimer_singleShot2(size_t msec,XSlotFunc1 slot_func);
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTimer_create
#define XTimer_create() XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // !XTimers_H