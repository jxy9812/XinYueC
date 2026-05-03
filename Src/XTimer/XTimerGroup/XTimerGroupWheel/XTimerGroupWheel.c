#include"XTimerGroupWheel.h"
#include"XMemory.h"
#include"XListSLinked.h"
#include"XMutex.h"
#include"XThreadData.h"
#include<string.h>
XTimerGroupWheel* XTimerGroupWheel_create(uint16_t precision)
{
	if (precision == 0)
		return NULL;
	XTimerGroupWheel* group= XMemory_malloc(sizeof(XTimerGroupWheel));
	if (group == NULL)
		return group;
	XTimerGroupWheel_init(group, precision);
	Set_Class_MemoryFree(group, XFree);
	return group;
}

void XTimerGroupWheel_init(XTimerGroupWheel* group, uint16_t precision)
{
	if (group == NULL|| precision==0)
		return;
	//初始化父类以外的数据
	memset(((XTimerGroupBase*)group) + 1, 0, sizeof(XTimerGroupWheel) - sizeof(XTimerGroupBase));
	XTimerGroupBase_init(group,precision);
	XClassGetVtable(group) = XTimerGroupWheel_class_init();
	//初始化数据
	XVector_init(&group->m_timeWheel,sizeof(XTimeWheel));
	XContainerSetDataDeinitMethod(&group->m_timeWheel,XVector_deinit_base);
	//group->m_timeWheel = XVector_Create(XTimeWheel);
	group->m_class.m_current_tick = XTimer_getCurrentTime() / group->m_class.m_precision;
	group->m_count = 0;
}

void XTimerGroupWheel_addTimeWheel_base(XTimerGroupWheel* group, size_t slotsCount)
{
	if (ISNULL(group, "") || ISNULL(slotsCount, "") || ISNULL(XClassGetVtable(group), ""))
		return;
	XClassGetVirtualFunc(group, EXTimerGroupWheel_Add_TimeWheel, void(*)(XTimerGroupWheel*, size_t))(group, slotsCount);
}

void XTimerGroupWheel_removeTimeWheel_base(XTimerGroupWheel* group)
{
	if (ISNULL(group, "") || ISNULL(XClassGetVtable(group), ""))
		return;
	XClassGetVirtualFunc(group, EXTimerGroupWheel_Remove_TimeWheel, void(*)(XTimerGroupWheel*))(group);
}

void XTimerGroupWheel_setMutex(XTimerGroupWheel* group, XMutex* mutex)
{
	if (group == NULL)
	{
		if(mutex)
			XMutex_delete(mutex);
		return;
	}
	if (group->m_mutex)
		XMutex_delete(group->m_mutex);
	group->m_mutex = mutex;
}
size_t XTimerGroupWheel_count(XTimerGroupWheel* group)
{
	return group? group->m_count:0;
}
bool XTimerGroupWheel_hasActiveTimers(const XTimerGroupWheel* group)
{
	if (group == NULL )
		return false;

	// 加锁保证线程安全
	if (group->m_mutex)
		XMutex_lock(group->m_mutex);

	bool hasActive = false;
	size_t wheelCount = XVector_size_base(&group->m_timeWheel);

	// 遍历所有时间轮
	for (size_t i = 0; i < wheelCount; ++i)
	{
		XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, i);
		if (wheel == NULL )
			continue;

		// 遍历当前时间轮的所有槽
		size_t slotCount = XVector_size_base(wheel);
		for (size_t j = 0; j < slotCount; ++j)
		{
			XListSLinked** slotList = XVector_at_base(wheel, j);
			if (slotList == NULL || *slotList == NULL)
				continue;

			// 检查槽内链表是否有活跃定时器
			if (XContainerSize(*slotList) > 0)
			{
				hasActive = true;
				goto end_check; // 找到活跃定时器，提前退出
			}
		}
	}

end_check:
	// 解锁
	if (group->m_mutex)
		XMutex_unlock(group->m_mutex);

	return hasActive;
}
uint64_t XTimerGroupWheel_getNextTimeout(const XTimerGroupWheel* group)
{
	if (group == NULL )
		return 0;

	// 加锁保证线程安全
	if (group->m_mutex)
		XMutex_lock(group->m_mutex);

	uint64_t next_timeout = 0;
	uint64_t min_expire_ticks = UINT64_MAX;
	const size_t current_tick = group->m_class.m_current_tick;
	const uint16_t precision = group->m_class.m_precision;

	// 遍历所有时间轮
	const size_t wheel_count = XVector_size_base(&group->m_timeWheel);
	for (size_t i = 0; i < wheel_count; ++i)
	{
		XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, i);
		if (wheel == NULL )
			continue;

		// 遍历当前时间轮的所有槽
		const size_t slot_count = XVector_size_base(wheel);
		for (size_t j = 0; j < slot_count; ++j)
		{
			XListSLinked** slot_list_ptr = XVector_at_base(wheel, j);
			if (slot_list_ptr == NULL || *slot_list_ptr == NULL)
				continue;

			// 遍历槽内的定时器链表
			XListSLinked* list = *slot_list_ptr;
			XListSLinked_iterator it = XListSLinked_begin(list);
			while (XListSLinked_iterator_isEnd(&it))
			{
				XTimerWheelData* data = *((XTimerTimeWheel**)XListSLinked_iterator_data(&it));
				// 只考虑运行中的定时器
				if (data != NULL && XTimerData_isRunning(data))
				{
					// 记录最小的到期滴答数
					if (((XTimerWheelData*)data)->m_expire_ticks < min_expire_ticks)
					{
						min_expire_ticks = ((XTimerWheelData*)data)->m_expire_ticks;
					}
				}
				XListSLinked_iterator_add(&it,&it);
			}
		}
	}

	// 计算下一个超时时间（毫秒）
	if (min_expire_ticks != UINT64_MAX)
	{
		if (min_expire_ticks > current_tick)
		{
			next_timeout = (min_expire_ticks - current_tick) * precision;
		}
		else
		{
			// 已经超时的定时器，返回0表示立即处理
			next_timeout = 0;
		}
	}

	// 解锁
	if (group->m_mutex)
		XMutex_unlock(group->m_mutex);

	return next_timeout;
}
static XTimerGroupWheel* global_XTimerGroupWheel = NULL;
//static XMutex* global_mutex = NULL;
static void XTimerGroupWheel_global_init()
{
	if (global_XTimerGroupWheel)return;
	global_XTimerGroupWheel = XTimerGroupWheel_create(1);
	XObject_moveToThread(global_XTimerGroupWheel, XThreadData_mainThread()->m_thread);
	XTimerGroupWheel_addTimeWheel_base(global_XTimerGroupWheel, 20);
	XTimerGroupWheel_addTimeWheel_base(global_XTimerGroupWheel, 10);
	XTimerGroupWheel_addTimeWheel_base(global_XTimerGroupWheel, 10);
	XTimerGroupWheel_setMutex(global_XTimerGroupWheel,XMutex_create(XLock_Spin));
}
XTimerGroupWheel* XTimerGroupWheel_global()
{
	if (!global_XTimerGroupWheel)
		XTimerGroupWheel_global_init();
	return global_XTimerGroupWheel;
}

bool XTimerGroupWheel_GlobalExists(void)
{
	return global_XTimerGroupWheel;
}
