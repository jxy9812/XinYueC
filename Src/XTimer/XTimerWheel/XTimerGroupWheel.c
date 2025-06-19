#include"XTimerGroupWheel.h"
#include"XMemory.h"
#include"XVector.h"
#include"XMutexBase.h"
#include<string.h>
XTimerGroupWheel* XTimerGroupWheel_create(uint16_t precision)
{
	if (precision == 0)
		return NULL;
	XTimerGroupWheel* group= XMemory_malloc(sizeof(XTimerGroupWheel));
	if (group == NULL)
		return group;
	XTimerGroupWheel_init(group, precision);
	return group;
}

void XTimerGroupWheel_init(XTimerGroupWheel* group, uint16_t precision)
{
	if (group == NULL|| precision==0)
		return;
	//初始化父类以外的数据
	memset(((XTimerGroupBase*)group) + 1, 0, sizeof(XTimerGroupWheel) - sizeof(XTimerGroupBase));
	XTimerGroupBase_init(group,NULL,precision);
	XClassGetVtable(group) = XTimerGroupWheel_class_init();
	//初始化数据
	group->m_timeWheel = XVector_Create(XTimeWheel);
	group->m_parent.m_current_tick = XTimerBase_getCurrentTime() / group->m_parent.m_precision;
}

void XTimerGroupWheel_addTimeWheel_base(XTimerGroupWheel* group, size_t slotsCount)
{
	if (ISNULL(group, "") || ISNULL(slotsCount, "") || ISNULL(XClassGetVtable(group), ""))
		return;
	XClassGetVirtualFunc(group, EXTimeGroupWheel_Add_TimeWheel, void(*)(XTimerGroupWheel*, size_t))(group, slotsCount);
}

void XTimerGroupWheel_removeTimeWheel_base(XTimerGroupWheel* group)
{
	if (ISNULL(group, "") || ISNULL(XClassGetVtable(group), ""))
		return;
	XClassGetVirtualFunc(group, EXTimeGroupWheel_Remove_TimeWheel, void(*)(XTimerGroupWheel*))(group);
}

void XTimerGroupWheel_setMutex(XTimerGroupWheel* group, XMutexBase* mutex)
{
	if (group == NULL)
	{
		if(mutex)
			XMutexBase_delete_base(mutex);
		return;
	}
	if (group->m_mutex)
		XMutexBase_delete_base(group->m_mutex);
	group->m_mutex = mutex;
}
#ifdef WIN32
#include"XMutexWin32.h"
#endif
void XTimerGroupWheel_setGlobal()
{
	if (XTimerGroupBase_global() != NULL)
		return;
	XTimerGroupWheel* group = XTimerGroupWheel_create(1);
	XTimerGroupWheel_addTimeWheel_base(group,100);
	XTimerGroupWheel_addTimeWheel_base(group,100);
	XTimerGroupWheel_addTimeWheel_base(group,100);
	XTimerGroupBase_setGlobal(group);
#ifdef WIN32
	XTimerGroupWheel_setMutex(group,XMutexWin32_create());
#endif
}
