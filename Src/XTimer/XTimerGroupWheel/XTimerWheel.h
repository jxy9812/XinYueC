#ifndef XTIMERWHEEL_H
#define XTIMERWHEEL_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
#include"XTimerBase.h"
// 定时器节点结构
typedef struct XTimerWheel 
{
	XTimerBase m_parent;
} XTimerWheel;

#ifdef __cplusplus
}
#endif
#endif // !XTimers_H
