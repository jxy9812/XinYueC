#include"XTimerGroupWheel.h"
#include"XListSLinked.h"
#include"XTimerWheel.h"
#include"XVector.h"
#include"XMemory.h"
#include"XStack.h"
#include"XEquality.h"
#include"XMutex.h"

static void VXTimerGroupWheel_deinit(XTimerGroupWheel* group);
static bool VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerWheel* timer);
static bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerWheel* timer);
static void VXTimerGroupBase_handler(XTimerGroupWheel* group);
static void VXTimerGroupWheel_addTimeWheel(XTimerGroupWheel* group, size_t slotsCount);
static void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group);

XVtable* XTimerGroupWheel_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XTIMEGROUPWHEEL_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());
    void* table[] = {
        VXTimerGroupBase_addTimer, VXTimerGroupBase_removeTimer,VXTimerGroupBase_handler,
        VXTimerGroupWheel_addTimeWheel, VXTimerGroupWheel_removeTimeWheel,
    };
    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimerGroupWheel_deinit);
    //XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXTimerGroupBase_handler);
    return XVTABLE_DEFAULT;
}

void VXTimerGroupWheel_deinit(XTimerGroupWheel* group)
{
    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    if (group->m_timeWheel != NULL)
    {
        while (!XVector_isEmpty_base(group->m_timeWheel))
            XTimerGroupWheel_removeTimeWheel_base(group);
        XVector_delete_base(group->m_timeWheel);
        group->m_timeWheel = NULL;
    }

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);

    if (group->m_mutex)
    {
        XMutex_delete(group->m_mutex);
        group->m_mutex = NULL;
    }

    // 释放父对象
    XVtableGetFunc(XIODeviceBase_class_init(), EXClass_Deinit, void(*)(XIODeviceBase*))(group);
}

static void add_timer_to_wheel(XTimeWheel* wheel, XTimerWheel* timer, size_t ticks)
{
    size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(wheel->m_slots);
    XListSLinked** pvList = XVector_at_base(wheel->m_slots, expire_slot);
    if (*pvList == NULL)
    {
        // 当前不存在链表
        *pvList = XListSLinked_Create(XTimerWheel*);
        (*pvList)->m_parent.m_equality = XEquality_size_t;
    }
    XListSLinked_push_front_base(*pvList, &timer);
    timer->m_list = *pvList;
}

static bool addTimer(XTimerGroupWheel* group, XTimerWheel* timer, size_t timeout_ticks)
{
    // 根据超时时间决定放入哪个时间轮
    XTimeWheel* wheel = NULL;
    size_t ticksCompare = 1, wheelSize = 1;
    for (size_t i = 0; i < XContainerSize(group->m_timeWheel); ++i)
    {
        wheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(group->m_timeWheel)) + i * XContainerTypeSize(group->m_timeWheel));
        ticksCompare *= XContainerSize(wheel->m_slots);
        if (timeout_ticks < ticksCompare)
        {
            add_timer_to_wheel(wheel, timer, timeout_ticks / wheelSize - (i ? 1 : 0));
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
    //校准下时间
    group->m_parent.m_current_tick = XTimerBase_getCurrentTime() / group->m_parent.m_precision;
    // 计算超时时间（转换为）
    size_t timeout_ticks = parent->m_timeout / group->m_parent.m_precision;
    timer->m_expire_ticks = group->m_parent.m_current_tick + timeout_ticks;
    parent->m_timerGroup = group;

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    bool result = addTimer(group, timer, timeout_ticks);

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);

    return result;
}

bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerWheel* timer)
{
    if (timer->m_list == NULL)
        return false;

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    XListSLinked_remove_base(timer->m_list, &timer);
    timer->m_list = NULL;

    if (timer->m_parent.m_autoDelete)
        XTimerBase_delete_base(timer);

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);

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
        XMutex_lock(group->m_mutex);

    XVector_push_back_base(group->m_timeWheel, &wheel);

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);
}

void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group)
{
    if (group->m_timeWheel == NULL || XVector_isEmpty_base(group->m_timeWheel))
        return;

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    XTimeWheel* wheel = XVector_back_base(group->m_timeWheel);
    if (wheel->m_slots != NULL)
    {
        // 遍历槽数组
        for (size_t i = 0; i < XContainerSize(wheel->m_slots); ++i)
        {
            XListSLinked* list = (XListSLinked*)(((uint8_t*)XContainerDataPtr(wheel->m_slots)) + i * XContainerTypeSize(wheel->m_slots));
            if (list != NULL)
            {
                XListSNode* node = XContainerDataPtr(list);
                while (node)
                {
                    XTimerWheel* timer = XListSNode_Data(node, XTimerWheel*);
                    if (group->m_mutex)
                        XMutex_unlock(group->m_mutex);
                    XTimerBase_delete_base(timer);
                    if (group->m_mutex)
                        XMutex_lock(group->m_mutex);
                    node = node->next;
                }
                XListSLinked_delete_base(list);
            }
        }
    }
    // 删除最后一个轮
    XVector_pop_back_base(group->m_timeWheel);

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);
}

static void cascade_timers(XTimerGroupWheel* group, XTimeWheel* higher_level, int slot_index, size_t higher_level_idx)
{
    if (higher_level_idx == 0) return; // 已到最底层，无需降级

    XListSLinked* list = XVector_At_Base(higher_level->m_slots, slot_index, XListSLinked*);
    if (list == NULL || XContainerSize(list) == 0) return;

    // 临时保存当前槽的定时器，避免遍历中修改链表
    XListSLinked temp_list;
    XListSLinked_init(&temp_list, sizeof(XTimerWheel));
    XListSLinked_swap_base(list, &temp_list); // 交换数据，清空原链表

    // 遍历所有需要降级的定时器
    XListSLinked_iterator* it = XContainerDataPtr(&temp_list);
    while (it != NULL) {
        XTimerWheel* timer = XListSNode_Data(it, XTimerWheel*);
        XListSLinked_iterator* next = ((XListSNode*)it)->next; // 提前保存下一个节点

        // 计算剩余ticks（距离到期的滴答数）
        size_t remaining_ticks = timer->m_expire_ticks - group->m_parent.m_current_tick;
        if (remaining_ticks == 0) {
            // 已到期，直接触发
            if (group->m_mutex) XMutex_unlock(group->m_mutex);
            XTimerBase_out_base(&timer->m_parent);
            if (group->m_mutex) XMutex_lock(group->m_mutex);
        }
        else {
            // 降级到下一级轮（higher_level_idx - 1），并检查是否需要继续降级
            XTimeWheel* lower_level = XVector_at_base(group->m_timeWheel, higher_level_idx - 1);
            size_t lower_slots = XContainerSize(lower_level->m_slots);
            size_t lower_max_ticks = lower_slots; // 下一级轮的最大ticks

            // 若剩余ticks仍大于下一级轮的最大范围，只放入下一级轮（等待下一级轮再降级）
            // 否则直接放入匹配的低级轮
            if (remaining_ticks < lower_max_ticks) {
                // 直接放入下一级轮（已满足精度）
                add_timer_to_wheel(lower_level, timer, remaining_ticks);
            }
            else {
                // 先放入下一级轮，等待其后续降级
                size_t relative_ticks = remaining_ticks / lower_max_ticks;
                add_timer_to_wheel(lower_level, timer, relative_ticks);
            }
        }

        it = next;
    }

    XListSLinked_deinit_base(&temp_list); // 清理临时链表
}

void VXTimerGroupBase_handler(XTimerGroupWheel* group)
{
    if (group->m_timeWheel == NULL || XVector_isEmpty_base(group->m_timeWheel))
        return;

    XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);
    size_t tick = XTimerBase_getCurrentTime() / groupBase->m_precision; // 当前滴答数
    if (tick <= groupBase->m_current_tick)
    {
        groupBase->m_current_tick = tick;
        return;
    }

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    // 处理时间跳跃：循环推进多个滴答
    while (tick > groupBase->m_current_tick)
    {
        XTimeWheel* wheel = XVector_front_base(group->m_timeWheel);
        int current_slot = wheel->m_tick % XVector_size_base(wheel->m_slots);
        // 处理当前槽的所有定时器
        XListSLinked* list = XVector_At_Base(wheel->m_slots, current_slot, XListSLinked*);
        if (list != NULL)
        {
            // 转移链表数据
            XListSLinked cList = *list;
            list->m_tail = NULL;
            XContainerDataPtr(list) = NULL;
            XContainerSize(list) = 0;
            XContainerCapacity(list) = 0;

            if (group->m_mutex)
                XMutex_unlock(group->m_mutex);

            for (XListSLinked_iterator* it = XContainerDataPtr(&cList); it != NULL; it = ((XListSNode*)it)->next)
            {
                XTimerWheel* timer = XListSNode_Data(it, XTimerWheel*);
                bool isdelete = true;
                if (timer->m_expire_ticks <= groupBase->m_current_tick && XTimerBase_isRunning(timer))
                {
                    timer->m_list = NULL;
                    XTimerBase_out_base(timer);

                    if ((!((XTimerBase*)timer)->m_singleShot) && (((XTimerBase*)timer)->m_interval > 0) && ((XTimerBase*)timer)->m_isRun)
                    {
                        // 有定时间隔的重新添加
                        size_t timeout_ticks = ((XTimerBase*)timer)->m_interval / group->m_parent.m_precision;
                        timer->m_expire_ticks = groupBase->m_current_tick + timeout_ticks;
                        if (group->m_mutex)
                            XMutex_lock(group->m_mutex);
                        addTimer(group, timer, timeout_ticks);
                        if (group->m_mutex)
                            XMutex_unlock(group->m_mutex);
                        isdelete = false;
                    }
                }
                else if(XTimerBase_isRunning(timer))
                {
                    if (group->m_mutex)
                        XMutex_lock(group->m_mutex);
                    size_t timeout_ticks = timer->m_expire_ticks- groupBase->m_current_tick ;
                    addTimer(group, timer, timeout_ticks);
                    if (group->m_mutex)
                        XMutex_unlock(group->m_mutex);
                    isdelete = false;
                }
                if (isdelete&&((XTimerBase*)timer)->m_autoDelete)
                {
                    XTimerBase_delete_base(timer);
                }
                //else // 也不释放的定时器更新状态
                //{
                //    ((XTimerBase*)timer)->m_isRun = false;
                //    timer->m_list = NULL;
                //}
            }

            // 清空当前槽的链表
            XListSLinked_clear_base(&cList);

            if (group->m_mutex)
                XMutex_lock(group->m_mutex);
        }
        // 低级轮向前推进
        ++wheel->m_tick;
        

        // 检查是否需要触发中级轮降级
        XTimeWheel* currentWheel = NULL;
        size_t ticksCompare = 1;
        for (size_t i = 0; i < XContainerSize(group->m_timeWheel); ++i)
        {
            currentWheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(group->m_timeWheel)) + i * XContainerTypeSize(group->m_timeWheel));
            ticksCompare *= XContainerSize(currentWheel->m_slots);
            if (groupBase->m_current_tick % ticksCompare == 0)
            {
                if (i + 1 >= XContainerSize(group->m_timeWheel))
                    break; // 下一个时间轮不存在
                XTimeWheel* nextWheel = ((uint8_t*)currentWheel) + XContainerTypeSize(group->m_timeWheel);
                int next_slot = (nextWheel->m_tick % XContainerSize(nextWheel->m_slots));
                cascade_timers(group, nextWheel, next_slot,i+1);
                ++nextWheel->m_tick;
                continue;
            }
            break;
        }
        ++groupBase->m_current_tick;
    }

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);
}