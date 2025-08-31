#include"XTimerGroupBase.h"
#include<string.h>
void XTimerGroupBase_init(XTimerGroupBase* group, uint16_t precision)
{
	if (group == NULL|| precision==0)
		return;
	XClass_init(group);
	memset(((XClass*)group)+1,0,sizeof(XTimerGroupBase)-sizeof(XClass));
	group->m_precision = precision;
	/*XClassGetVtable(group) = vtable;*/
}

bool XTimerGroupBase_addTimer_base(XTimerGroupBase* group, XTimerBase* timer)
{
	if (ISNULL(group, "") || ISNULL(timer, "") || ISNULL(XClassGetVtable(group), ""))
		return false;
	
	return XClassGetVirtualFunc(group, EXTimerGroupBase_Add_Timer, bool(*)(XTimerGroupBase* ,XTimerBase*))(group,timer);
}

bool XTimerGroupBase_removeTimer_base(XTimerGroupBase* group, XTimerBase* timer)
{
	if (ISNULL(group, "") || ISNULL(timer, "") || ISNULL(XClassGetVtable(group), ""))
		return false;
	return XClassGetVirtualFunc(group, EXTimerGroupBase_Remove_Timer, bool(*)(XTimerGroupBase*, XTimerBase*))(group, timer);
}
void XTimerGroupBase_handler_base(XTimerGroupBase* group)
{
	if (ISNULL(group, "") || ISNULL(XClassGetVtable(group), ""))
		return;
	XClassGetVirtualFunc(group, EXTimerGroupBase_Handler,
		void (*)(XTimerGroupBase*))(group);
}
static XTimerGroupBase* global_timerGroup=NULL;
void XTimerGroupBase_setGlobal(XTimerGroupBase* group)
{
	if (global_timerGroup != NULL)
		XTimerGroupBase_delete_base(global_timerGroup);
	global_timerGroup = group;
}

XTimerGroupBase* XTimerGroupBase_global()
{
	return global_timerGroup;
}

void XTimerGroupBase_global_poll()
{
	if (global_timerGroup)
		XTimerGroupBase_handler_base(global_timerGroup);
}
