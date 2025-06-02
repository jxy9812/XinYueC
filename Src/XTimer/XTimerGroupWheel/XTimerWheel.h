#ifndef XTIMERWHEEL_H
#define XTIMERWHEEL_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerBase.h"
typedef struct XTimerGroupWheel XTimerGroupWheel;
typedef struct XListSLinked XListSLinked;
#define XTIMERWHEEL_VTABLE_SIZE (XTIMERBASE_VTABLE_SIZE)       //XTimerWheel虚函数表大小
// 定时器节点结构
typedef struct XTimerWheel 
{
	XTimerBase m_parent;
	size_t m_expire_ticks;     // 到期时间戳（毫秒）
	XListSLinked* m_list;//加入的链表
} XTimerWheel;
XVtable* XTimerWheel_class_init();
XTimerWheel* XTimerWheel_new();
void XTimerWheel_init(XTimerWheel* timer);

#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
