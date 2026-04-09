#ifndef XTIMERBASE_H
#define XTIMERBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XObject.h"

/**
* @brief 定时器超时回调函数类型
* @param userData 用户自定义数据
*/
typedef void (*XTimerBaseCallback)(void* userData);
/**
* @brief 定时器基类结构体前向声明
*/
typedef struct XTimerBase XTimerBase;
/**
* @brief 定时器组基类结构体前向声明
*/
typedef struct XTimerGroupBase XTimerGroupBase;
/**
* @brief XTimerBase虚函数表大小宏定义
* 基于XObject的虚函数表大小扩展
*/
#define XTIMERBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XTimerBase))
/**
* @brief XTimerBase虚函数表枚举定义开始
* 用于标识虚函数表中的函数索引
*/
XCLASS_DEFINE_BEGING(XTimerBase)
XCLASS_DEFINE_ENUM(XTimerBase, Start) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XTimerBase, Stop),
XCLASS_DEFINE_END(XTimerBase)
/**
* @brief 定时器基类结构体定义
* 提供定时器的基础属性和行为
*/
typedef struct XTimerBase
{
	XObject m_class;               ///< 继承自XObject基类
	uint32_t m_autoDelete : 1;	   ///< 定时器超时后是否自动释放        
	uint32_t m_isRun:1;            ///< 定时器是否正在运行
	uint32_t m_isSingleShot:1;       ///< 是否为单次定时器（true：只触发一次，false：周期性触发）
	uint32_t m_firstTrigger : 1;   //首次触发
	size_t m_timeout;              ///< 首次超时时间（毫秒）
	size_t m_interval;             ///< 周期性触发时间间隔（毫秒）
	size_t timerId;                ///< 定时器唯一标识ID
	void* m_userData;              ///< 用户自定义数据
	XTimerBaseCallback m_timerCallback; ///< 定时器超时回调函数
	//size_t number;                 ///< 超时触发次数计数
}XTimerBase;
// === 构造与析构相关接口 ===
/**
* @brief 创建XTimerBase实例
* @param vtable 虚函数表指针
* @return 成功返回XTimerBase实例指针，失败返回NULL
*/
XTimerBase* XTimerBase_create(XVtable* vtable);
/**
* @brief 初始化XTimerBase实例
* @param timer 要初始化的XTimerBase实例指针
* @param vtable 虚函数表指针
*/
void XTimerBase_init(XTimerBase* timer, XVtable* vtable);
/**
* @brief 基类删除函数宏定义
* 复用XObject的删除函数
*/
#define XTimerBase_delete_base    XObject_deleteLater
/**
* @brief 基类反初始化函数宏定义
* 复用XObject的反初始化函数
*/
#define XTimerBase_deinit_base    XObject_deinitLater
// === 启动与停止相关接口 ===
/**
* @brief 启动定时器（基类实现）
* @param timer XTimerBase实例指针
*/
void XTimerBase_start_base(XTimerBase* timer);
/**
* @brief 停止定时器（基类实现）
* @param timer XTimerBase实例指针
*/
void XTimerBase_stop_base(XTimerBase* timer);
// === 属性设置相关接口 ===
/**
* @brief 设置定时器超时时间（基类实现）
* @param timer XTimerBase实例指针
* @param value 超时时间（毫秒）
*/
void XTimerBase_setTimeout(XTimerBase* timer, size_t value);
/**
* @brief 设置定时器周期间隔（基类实现）
* @param timer XTimerBase实例指针
* @param value 周期间隔（毫秒）
*/
void XTimerBase_setInterval(XTimerBase* timer, size_t value);
/**
* @brief 设置用户自定义数据（基类实现）
* @param timer XTimerBase实例指针
* @param userData 用户数据指针
*/
void XTimerBase_setUserData(XTimerBase* timer, void* userData);
/**
* @brief 设置定时器回调函数（基类实现）
* @param timer XTimerBase实例指针
* @param callback 回调函数指针
*/
void XTimerBase_setTimerCallback(XTimerBase* timer, XTimerBaseCallback callback);
/**
* @brief 设置定时器ID
* @param timer XTimerBase实例指针
* @param timerId 要设置的ID值
*/
void XTimerBase_setTimerId(XTimerBase* timer, size_t timerId);
/**
* @brief 设置定时器是否自动释放
* @param timer XTimerBase实例指针
* @param del true：自动释放，false：不自动释放
*/
void XTimerBase_setAutoDelete(XTimerBase* timer, bool del);
/**
* @brief 设置定时器是否为单次触发模式
* @param timer XTimerBase实例指针
* @param ss true：单次触发，false：周期性触发
*/
void XTimerBase_setSingleShot(XTimerBase* timer, bool ss);
// === 属性获取相关接口 ===
bool XTimerBase_isSingleShot(XTimerBase* timer);
/**
* @brief 判断定时器是否为周期性任务
* @param timer XTimerBase实例指针
* @return true：非周期性（单次），false：周期性
*/
bool XTimerBase_isPeriodic(XTimerBase* timer);
/**
* @brief 判断定时器是否正在运行
* @param timer XTimerBase实例指针
* @return true：运行中，false：已停止
*/
bool XTimerBase_isRunning(XTimerBase* timer);
/**
* @brief 获取定时器超时时间
* @param timer XTimerBase实例指针
* @return 超时时间（毫秒）
*/
size_t XTimerBase_timeout(XTimerBase* timer);
/**
* @brief 获取定时器周期间隔
* @param timer XTimerBase实例指针
* @return 周期间隔（毫秒）
*/
size_t XTimerBase_interval(XTimerBase* timer);
/**
* @brief 获取定时器ID
* @param timer XTimerBase实例指针
* @return 定时器ID
*/
size_t XTimerBase_timerId(XTimerBase* timer);
/**
* @brief 获取用户自定义数据
* @param timer XTimerBase实例指针
* @return 用户数据指针
*/
void* XTimerBase_getUserData(XTimerBase* timer);
/**
* @brief 判断定时器是否自动释放
* @param timer XTimerBase实例指针
* @return true：自动释放，false：不自动释放
*/
bool   XTimerBase_isAutoDelete(XTimerBase* timer);
// === 超时处理相关接口 ===
/**
* @brief 定时器超时处理函数（基类实现）
* @param timer XTimerBase实例指针
*/
void XTimerBase_out(XTimerBase* timer);
// === 时间戳相关接口 ===
/**
* @brief 累加当前时间戳（以毫秒为单位）
* @param tick_period 要累加的时间（毫秒）
*/
void XTimerBase_inc(size_t tick_period);
/**
* @brief 设置当前时间戳（以毫秒为单位）
* @param time 要设置的时间戳
*/
void XTimerBase_setCurrentTime(size_t time);
/**
* @brief 获取当前时间戳（以毫秒为单位）
* @return 当前时间戳（毫秒）
*/
size_t XTimerBase_getCurrentTime();
/**
* @brief 设置获取当前时间戳的函数
* @param get 自定义的时间获取函数指针
*/
void XTimerBase_setCurrentTimeFunc(size_t(*get)());
#ifdef __cplusplus
}
#endif
#endif // !XTIMERS_H