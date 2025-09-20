#ifndef XTIMERCONFIG_H
#define XTIMERCONFIG_H
#ifdef __cplusplus
extern "C" {
#endif
//XTimer类的配置文件

//XTimer定时器类实现方法设置
#define XTIMER_IS_TIMEWHEEL					0		//配置为XTimerTimeWheel 高优先级 通用跨平台,高效，响应速度快,有点占内存  
#if !XTIMER_IS_TIMEWHEEL
//Windows平台下XTimer下的实现方法
#ifdef WIN32
//以下二选一不然报错重定义
#define XTIMER_IS_THREADPOOLTIMER			1		//配置为XTimerWin32ThreadpoolTimer
#define XTIMER_IS_TIMESETEVENT				1		//配置为XTimerWin32TimeSetEvent
//Posix平台
#elif defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
//FreeRTOS嵌入式平台
#elif defined(__FreeRTOS__)

#endif 
#endif // 实现方法




#ifdef __cplusplus
}
#endif
#endif