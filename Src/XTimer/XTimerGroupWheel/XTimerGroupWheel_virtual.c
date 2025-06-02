#include"XTimerGroupWheel.h"
static void VXTimerGroupWheel_free(XTimerGroupWheel* group);
static bool VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerBase* timer);
static bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerBase* timer);
static void VXTimerGroupBase_poll(XTimerGroupWheel* group);
static void VXTimerGroupWheel_addTimeWheel(XTimerGroupWheel* group, size_t slotsCount);
static void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group);
XVtable* XTimerGroupWheel_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XTIMEGROUPWHEEL_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
	XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
		VXTimerGroupBase_addTimer,VXTimerGroupBase_removeTimer,
		VXTimerGroupBase_poll,VXTimerGroupWheel_addTimeWheel,
		VXTimerGroupWheel_removeTimeWheel,
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free, VXTimerGroupWheel_free);
#if SHOWCONTAINERSIZE
	printf("XTimerGroupWheel size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXTimerGroupWheel_free(XTimerGroupWheel* group)
{
}

bool VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerBase* timer)
{
	return false;
}

bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerBase* timer)
{
	return false;
}

void VXTimerGroupBase_poll(XTimerGroupWheel* group)
{
}

void VXTimerGroupWheel_addTimeWheel(XTimerGroupWheel* group, size_t slotsCount)
{

}

void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group)
{
}
