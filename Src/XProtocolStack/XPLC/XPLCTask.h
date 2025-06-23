#ifndef XPLCTASK_H
#define XPLCTASK_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#define XPLCTASK_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XClass))       //XPLCTask虚函数表大小
//XPLCTask虚函数表枚举
XCLASS_DEFINE_BEGING(XPLCTask)
XCLASS_DEFINE_ENUM(XPLCTask, AddTaskState) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XPLCTask, RemoveTaskState),
XCLASS_DEFINE_ENUM(XPLCTask, ClearTaskState),
XCLASS_DEFINE_ENUM(XPLCTask, SetState),
XCLASS_DEFINE_ENUM(XPLCTask, Poll),
XCLASS_DEFINE_ENUM(XPLCTask, Start),
XCLASS_DEFINE_ENUM(XPLCTask, Finish),
XCLASS_DEFINE_END(XPLCTask)
//绑定的状态 函数
typedef void (*XPLCTaskStateFunc)(XPLCTask* task,void* taskData,void* stateData);
//任务开始函数 判断是否可以执行
typedef bool (*XPLCTaskStartFunc)(XPLCTask* task, void* taskData);
//任务结束函数 清理资源
typedef void (*XPLCTaskFinishFunc)(XPLCTask* task, void* taskData);
typedef enum
{
	XPLCTaskState_Start=-0xFF,//开始
	XPLCTaskState_Finish,//结束
	XPLCTaskState_ExitTask//已经退出了
}XPLCTaskState;
typedef struct XPLCTask
{
	XClass m_parent;//继承类
	int32_t m_lastTaskState;//上一次状态 用来缓存
	int32_t m_runTaskState;//任务运行中的状态
	void* m_taskNode;//缓存的任务节点
	XMapBase* m_taskStateMap;//状态切换函数表
	XPLCTaskStartFunc m_startFunc;//开始函数
	XPLCTaskFinishFunc m_finishFunc;//结束函数
	void* m_taskData;//任务参数
}XPLCTask;
XVtable* XPLCTask_class_init();
XPLCTask* XPLCTask_create();
void XPLCTask_init(XPLCTask* task);
void XPLCTask_setStartFunc(XPLCTask* task, XPLCTaskStartFunc func);
void XPLCTask_setFinishFunc(XPLCTask* task, XPLCTaskFinishFunc func);
void XPLCTask_setTaskData(XPLCTask* task, void* taskData);
int  XPLCTask_getState(XPLCTask* task);
bool XPLCTask_addState_base(XPLCTask* task,uint16_t state, XPLCTaskStateFunc func, void* stateData);
bool XPLCTask_removeState_base(XPLCTask* task, uint16_t state);
void XPLCTask_clearState_base(XPLCTask* task);
void XPLCTask_setState_base(XPLCTask* task, uint16_t state);
void XPLCTask_poll_base(XPLCTask* task);
void XPLCTask_start_base(XPLCTask* task);//开始任务
void XPLCTask_finish_base(XPLCTask* task);//结束任务
#define XPLCTask_delete_base	XClass_delete_base
#ifdef __cplusplus
}
#endif
#endif // !XPLC_H
