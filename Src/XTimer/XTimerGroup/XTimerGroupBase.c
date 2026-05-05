#include"XTimerGroupBase.h"
#include"XTimerData.h"
#include<string.h>
void XTimerGroupBase_init(XTimerGroupBase* group, uint16_t precision)
{
	if (group == NULL|| precision==0)
		return;
	XObject_init(group);
	memset(((XObject*)group)+1,0,sizeof(XTimerGroupBase)-sizeof(XObject));
	group->m_precision = precision;
	group->m_min_time = precision;
	group->m_max_time = 0;
	/*XClassGetVtable(group) = vtable;*/
}

XHandle XTimerGroupBase_addTimer_base(XTimerGroupBase* group, XTimerData  data)
{
	if (ISNULL(group, "") || ISNULL(XClassGetVtable(group), ""))
		return false;
	return XClassGetVirtualFunc(group, EXTimerGroupBase_Add_Timer, XHandle (*)(XTimerGroupBase* , XTimerData))(group, data);
}

bool XTimerGroupBase_removeTimer_base(XTimerGroupBase* group, XHandle handle)
{
	if (ISNULL(group, "") || ISNULL(handle, "") || ISNULL(XClassGetVtable(group), ""))
		return false;
	return XClassGetVirtualFunc(group, EXTimerGroupBase_Remove_Timer, bool(*)(XTimerGroupBase*, XHandle))(group, handle);
}
bool XTimerGroupBase_timeRange(XTimerGroupBase* group, size_t* min_time, size_t* max_time)
{
	if (ISNULL(group, "") || ISNULL(min_time, "") || ISNULL(max_time, "") || ISNULL(XClassGetVtable(group), ""))
		return false;
	if (min_time)
		*min_time = group->m_min_time;
	if (max_time)
		*max_time = group->m_max_time;
}
size_t XTimerGroupBase_min_time(XTimerGroupBase* group)
{
	return group? group->m_min_time:0;
}
size_t XTimerGroupBase_max_time(XTimerGroupBase* group)
{
	return group? group->m_max_time:0;
}
void XTimerGroupBase_tick_base(XTimerGroupBase* group)
{
	if (ISNULL(group, "") || ISNULL(XClassGetVtable(group), ""))
		return;
	XClassGetVirtualFunc(group, EXTimerGroupBase_Tick,
		void (*)(XTimerGroupBase*))(group);
}

void XTimerGroupBase_handler(XTimerGroupBase* group)
{
	XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);
	size_t tick = XTimer_getCurrentTime() / groupBase->m_precision; // 当前滴答数

	if (tick <= groupBase->m_current_tick)
	{
		groupBase->m_current_tick = tick;
		return;
	}

	// 处理时间跳跃：循环推进多个滴答
	while (tick > groupBase->m_current_tick)
	{
		XTimerGroupBase_tick_base(group);
	}
}
