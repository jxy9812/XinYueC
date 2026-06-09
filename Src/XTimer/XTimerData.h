#ifndef XTimerData_H
#define XTimerData_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XObject.h"

/**
* @brief 定时器基类结构体前向声明
*/
typedef struct XTimerData XTimerData;
/**
* @brief 定时器超时回调函数类型
* @param userData 用户自定义数据
*/
typedef void (*XTimerCallback)(void* userData, XTimerData* timer);
/**
* @brief 定时器组基类结构体前向声明
*/
typedef struct XTimerGroupBase XTimerGroupBase;

typedef struct XTimerData
{
	uint32_t m_autoDelete : 1;			///< 定时器超时后是否自动释放  
	uint32_t m_isSingleShot : 1;		///< 是否为单次定时器（true：只触发一次，false：周期性触发）
	XTimerId timerId;					///< 定时器唯一标识ID
	void* m_userData;					///< 用户自定义数据
	XTimerCallback m_timerCallback; ///< 定时器超时回调函数
	uint64_t m_timeout;                 ///< 首次超时时间（单位由调用者决定，通常为毫秒或纳秒）
	uint64_t m_interval;                ///< 周期性触发时间间隔（单位由调用者决定，通常为毫秒或纳秒）
}XTimerData;
// === 构造与析构相关接口 ===
/**
* @brief 创建XTimerData实例
* @param vtable 虚函数表指针
* @return 成功返回XTimerData实例指针，失败返回NULL
*/
XTimerData* XTimerData_create(XVtable* vtable);
/**
* @brief 初始化XTimerData实例
* @param timer 要初始化的XTimerData实例指针
* @param vtable 虚函数表指针
*/
void XTimerData_init(XTimerData* timer, XVtable* vtable);
/**
* @brief 基类删除函数宏定义
* 复用XObject的删除函数
*/
void XTimerData_delete(XTimerData* timer);
// === 属性设置相关接口 ===
/**
* @brief 设置定时器超时时间（基类实现）
* @param timer XTimerData实例指针
* @param value 超时时间（毫秒）
*/
void XTimerData_setTimeout(XTimerData* timer, size_t value);
/**
* @brief 设置定时器周期间隔（基类实现）
* @param timer XTimerData实例指针
* @param value 周期间隔（毫秒）
*/
void XTimerData_setInterval(XTimerData* timer, size_t value);
/**
* @brief 设置用户自定义数据（基类实现）
* @param timer XTimerData实例指针
* @param userData 用户数据指针
*/
void XTimerData_setUserData(XTimerData* timer, void* userData);
/**
* @brief 设置定时器回调函数（基类实现）
* @param timer XTimerData实例指针
* @param callback 回调函数指针
*/
void XTimerData_setTimerCallback(XTimerData* timer, XTimerCallback callback);
/**
* @brief 设置定时器ID
* @param timer XTimerData实例指针
* @param timerId 要设置的ID值
*/
void XTimerData_setTimerId(XTimerData* timer, size_t timerId);
/**
* @brief 设置定时器是否自动释放
* @param timer XTimerData实例指针
* @param del true：自动释放，false：不自动释放
*/
void XTimerData_setAutoDelete(XTimerData* timer, bool del);
/**
* @brief 设置定时器是否为单次触发模式
* @param timer XTimerData实例指针
* @param ss true：单次触发，false：周期性触发
*/
void XTimerData_setSingleShot(XTimerData* timer, bool ss);
// === 属性获取相关接口 ===
bool XTimerData_isSingleShot(XTimerData* timer);
/**
* @brief 判断定时器是否为周期性任务
* @param timer XTimerData实例指针
* @return true：非周期性（单次），false：周期性
*/
bool XTimerData_isPeriodic(XTimerData* timer);
/**
* @brief 获取定时器超时时间
* @param timer XTimerData实例指针
* @return 超时时间（毫秒）
*/
size_t XTimerData_timeout(XTimerData* timer);
/**
* @brief 获取定时器周期间隔
* @param timer XTimerData实例指针
* @return 周期间隔（毫秒）
*/
size_t XTimerData_interval(XTimerData* timer);
/**
* @brief 获取定时器ID
* @param timer XTimerData实例指针
* @return 定时器ID
*/
size_t XTimerData_timerId(XTimerData* timer);
/**
* @brief 获取用户自定义数据
* @param timer XTimerData实例指针
* @return 用户数据指针
*/
void* XTimerData_userData(XTimerData* timer);
/**
* @brief 判断定时器是否自动释放
* @param timer XTimerData实例指针
* @return true：自动释放，false：不自动释放
*/
bool   XTimerData_isAutoDelete(XTimerData* timer);
// === 超时处理相关接口 ===
/**
* @brief 定时器超时处理函数（基类实现）
* @param timer XTimerData实例指针
*/
void XTimerData_out(XTimerData* timer);

#ifdef __cplusplus
}
#endif
#endif // !XTIMERS_H