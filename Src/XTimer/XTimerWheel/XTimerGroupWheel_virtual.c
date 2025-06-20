#include"XTimerGroupWheel.h"
#include"XListSLinked.h"
#include"XTimerWheel.h"
#include"XVector.h"
#include"XMemory.h"
#include"XStack.h"
#include"XEquality.h"
#include"XMutexBase.h"
static void VXTimerGroupWheel_delete(XTimerGroupWheel* group);
static bool VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerWheel* timer);
static bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerWheel* timer);
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
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXTimerGroupWheel_delete);
#if SHOWCONTAINERSIZE
	printf("XTimerGroupWheel size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXTimerGroupWheel_delete(XTimerGroupWheel* group)
{
	if (group->m_timeWheel != NULL)
	{
		while (!XVector_isEmpty_base(group->m_timeWheel))
			XTimerGroupWheel_removeTimeWheel_base(group);
		XVector_delete_base(group->m_timeWheel);
	}
	if (group->m_mutex)
		XMutexBase_delete_base(group->m_mutex);
	XMemory_free(group);
}
static void add_timer_to_wheel(XTimeWheel*wheel, XTimerWheel* timer, size_t ticks)
{
	size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(wheel->m_slots);
	XListSLinked** pvList=XVector_at_base(wheel->m_slots, expire_slot);
	if (*pvList == NULL)
	{//当前不存在链表
		*pvList = XListSLinked_Create(XTimerWheel*);
		(*pvList)->m_parent.m_equality = XEquality_size_t;
	}
	//printf("插入的链表:%p\n", *pvList);
	XListSLinked_push_front_base(*pvList,&timer);
	timer->m_list = *pvList;
}
static bool addTimer(XTimerGroupWheel* group, XTimerWheel* timer, size_t timeout_ticks)
{
	// 根据超时时间决定放入哪个时间轮
	XTimeWheel* wheel = NULL;
	size_t ticksCompare = 1,wheelSize=1;
	//for_each_iterator(group->m_timeWheel, XVector, it)
	for(size_t i=0;i<XContainerSize(group->m_timeWheel);++i)
	{
		//wheel = (XTimeWheel*)it;
		wheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(group->m_timeWheel))+i*XContainerTypeSize(group->m_timeWheel));
		ticksCompare *= XContainerSize(wheel->m_slots);
		if (timeout_ticks < ticksCompare)
		{
			add_timer_to_wheel(wheel, timer, timeout_ticks /wheelSize);
			return true;
		}
		wheelSize *= XContainerSize(wheel->m_slots);
	}
	return false;
}

bool VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerWheel* timer)
{
	XTimerBase* parent = (XTimerBase*)timer;
	if (timer == NULL || parent->m_timerCallback == NULL || ((parent->m_interval == 0) && (parent->m_timeout == 0)))
		return false;
	//printf("添加\n");
	// 计算超时时间（转换为）
	size_t timeout_ticks = parent->m_timeout / group->m_parent.m_precision;
	//size_t interval_ticks = parent->m_interval / group->m_parent.m_precision;
	timer->m_expire_ticks = group->m_parent.m_current_tick + timeout_ticks;
	parent->m_timerGroup = group;
	if (group->m_mutex)
		XMutexBase_lock_base(group->m_mutex);
	if(addTimer(group, timer, timeout_ticks))
	{
		if (group->m_mutex)
			XMutexBase_unlock_base(group->m_mutex);
		return true;
	}
	if (group->m_mutex)
		XMutexBase_unlock_base(group->m_mutex);
	return false;
}

bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerWheel* timer)
{
	if(timer->m_list==NULL)
		return false;
	if (group->m_mutex)
		XMutexBase_lock_base(group->m_mutex);
	XListSLinked_remove_base(timer->m_list,&timer);
	timer->m_list = NULL;
	if (timer->m_parent.m_autoDelete)
		XTimerBase_delete_base(timer);
	if (group->m_mutex)
		XMutexBase_unlock_base(group->m_mutex);
	return true;
}

void VXTimerGroupWheel_addTimeWheel(XTimerGroupWheel* group, size_t slotsCount)
{
	if (group->m_timeWheel == NULL)
		return;
	XTimeWheel wheel = { 0 };
	wheel.m_tick = 0;
	wheel.m_slots = XVector_Create(XListSLinked*);
	if (wheel.m_slots == NULL)
		return;
	XVector_resize_base(wheel.m_slots, slotsCount);

	if (group->m_mutex)
		XMutexBase_lock_base(group->m_mutex);
	XVector_push_back_base(group->m_timeWheel,&wheel);
	if (group->m_mutex)
		XMutexBase_unlock_base(group->m_mutex);
}

void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group)
{
	if (group->m_timeWheel == NULL|| XVector_isEmpty_base(group->m_timeWheel))
		return;
	if (group->m_mutex)
		XMutexBase_lock_base(group->m_mutex);
	XTimeWheel* wheel = XVector_back_base(group->m_timeWheel);
	if (wheel->m_slots != NULL)
	{
		//遍历槽数组
		//for_each_iterator(wheel->m_slots, XVector, vIt)
		for (size_t i = 0; i < XContainerSize(wheel->m_slots); ++i)
		{
			XListSLinked* list = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(wheel->m_slots)) + i * XContainerTypeSize(wheel->m_slots));
			//XListSLinked*list=*((XListSLinked**)vIt);
			if (list != NULL)
			{
				//for_each_iterator(list, XListSLinked, it)
				XListSNode* node = XContainerDataPtr(list);
				while(node)
				{
					XTimerWheel* timer = XListSNode_Data(node, XTimerWheel*);
					XTimerBase_delete_base(timer);
					node = node->next;
				}
				XListSLinked_delete_base(list);
			}
		}
	}
	//删除最后一个轮
	XVector_pop_back_base(group->m_timeWheel);
	if (group->m_mutex)
		XMutexBase_unlock_base(group->m_mutex);
}

// 将定时器从高级轮降级到低级轮
static void cascade_timers(XTimerGroupWheel* group, XTimeWheel* higher_level, int slot_index) 
{
	XListSLinked* list = XVector_At_Base(higher_level->m_slots, slot_index, XListSLinked*);
	if (list == NULL)
		return;
	//for_each_iterator(list, XListSLinked,it)
	for (XListSLinked_iterator* it = XContainerDataPtr(list); it != NULL; it = ((XListSNode*)it)->next)
	{
		XTimerWheel* timer = XListSNode_Data(it, XTimerWheel*);

		size_t remaining_ticks =0;
		if (timer->m_expire_ticks > group->m_parent.m_current_tick)
			remaining_ticks = timer->m_expire_ticks - group->m_parent.m_current_tick;
		XTimeWheel* wheel = NULL;
		size_t ticksCompare = 1, wheelSize=1;
		//for_each_iterator(group->m_timeWheel, XVector, it)
		for (size_t i = 0; i < XContainerSize(group->m_timeWheel); ++i)
		{
			//wheel = (XTimeWheel*)it;
			wheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(group->m_timeWheel)) + i * XContainerTypeSize(group->m_timeWheel));
			ticksCompare *= XContainerSize(wheel->m_slots);
			if (remaining_ticks < ticksCompare)
			{
				//printf("降级:%d\n", wheelSize);
				add_timer_to_wheel(wheel, timer, remaining_ticks/ wheelSize);
				break;
			}
			wheelSize *= XContainerSize(wheel->m_slots);
		}
	}
	//清空当前槽的链表
	XListSLinked_clear_base(list);
}

void VXTimerGroupBase_poll(XTimerGroupWheel* group)
{
	if (group->m_timeWheel == NULL || XVector_isEmpty_base(group->m_timeWheel))
		return;
	
	XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);
	size_t tick = XTimerBase_getCurrentTime() / groupBase->m_precision;//当前滴答数
	if (tick <=groupBase->m_current_tick)
	{
		groupBase->m_current_tick = tick;
		return;
	}
	if (group->m_mutex)
		XMutexBase_lock_base(group->m_mutex);
	  // 处理时间跳跃：循环推进多个滴答
    while (tick > groupBase->m_current_tick)
	{
		XTimeWheel* wheel = XVector_front_base(group->m_timeWheel);
		int current_slot = wheel->m_tick % XVector_getSize_base(wheel->m_slots);
		//处理当前槽的所有定时器
		XListSLinked* list = XVector_At_Base(wheel->m_slots, current_slot, XListSLinked*);
		if (list != NULL)
		{
			//转移链表数据
			XListSLinked cList = *list;
			list->m_tail = NULL;
			XContainerDataPtr(list) = NULL;
			XContainerSize(list) = 0;
			XContainerCapacity(list) = 0;
			//printf("%p\n", list);
			//for_each_iterator(&cList, XListSLinked, it)
			for (XListSLinked_iterator* it = XContainerDataPtr(&cList); it != NULL; it = ((XListSNode*)it)->next)
			{
				XTimerWheel* timer = XListSNode_Data(it, XTimerWheel*);
				if (timer->m_expire_ticks <= groupBase->m_current_tick && XTimerBase_isRunning(timer))
				{
					timer->m_list = NULL;
					//if (group->m_mutex)//防止同线程同任务死锁
					//	XMutexBase_unlock_base(group->m_mutex);
					XTimerBase_out(timer);
					/*if (group->m_mutex)
						XMutexBase_lock_base(group->m_mutex);*/
				}
				//转成父类指针
				XTimerBase* timerBase = (XTimerBase*)timer;
				if (timerBase->m_interval > 0)
				{//有定时间隔的重新添加
					size_t timeout_ticks = timerBase->m_interval / group->m_parent.m_precision;
					timer->m_expire_ticks = groupBase->m_current_tick + timeout_ticks;
					addTimer(group, timer, timeout_ticks);
				}
				else if (((XTimerBase*)timer) ->m_autoDelete)
				{//
					//printf("%p\n", list);
					XTimerBase_delete_base(timer);
				}
			}
			//清空当前槽的链表
			XListSLinked_clear_base(&cList);
		}
		// 低级轮向前推进
		++wheel->m_tick;
		++groupBase->m_current_tick;
		//return;
		// 检查是否需要触发中级轮降级
		XTimeWheel* currentWheel = NULL;
		size_t ticksCompare = 1;
		//for_each_iterator(group->m_timeWheel, XVector, it)
		for (size_t i = 0; i < XContainerSize(group->m_timeWheel); ++i)
		{
			//currentWheel = (XTimeWheel*)it;
			currentWheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(group->m_timeWheel)) + i * XContainerTypeSize(group->m_timeWheel));
			ticksCompare *= XContainerSize(wheel->m_slots);
			if (groupBase->m_current_tick % ticksCompare == 0)//注释这行可提高二级后的精度,但会增加cpu负载
			{//一圈跑完
				//XTimeWheel* nextWheel = XVector_iterator_add(group->m_timeWheel, currentWheel);
				
				if (i+1>= XContainerSize(group->m_timeWheel))
					break;//下一个时间轮不存在
				XTimeWheel* nextWheel = ((uint8_t*)currentWheel) + XContainerTypeSize(group->m_timeWheel);
				int next_slot = (nextWheel->m_tick % XContainerSize(nextWheel->m_slots));
				cascade_timers(group, nextWheel, next_slot);
				++nextWheel->m_tick;
				continue;
			}
			break;
		}
	}
	if (group->m_mutex)
		XMutexBase_unlock_base(group->m_mutex);
}
