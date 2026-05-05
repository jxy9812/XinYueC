#include"XTimeWheelGroup.h"
#include"XListSLinked.h"
#include"XMemory.h"
#include"XStack.h"
#include"XThreadData.h"
#include"XCoreApplication.h"
#include<string.h>
// 单个时间轮结构
typedef struct XTimeWheel {
    XVector m_slots;					// 槽数组，每个槽是一个链表头 /XVector<XListSLinked<XTimerWheelData*>>
    size_t m_tick;						// 当前滴答计数
} XTimeWheel;
// 定时器时间轮
typedef struct XTimerWheelData
{
    XTimerData m_data;
    XAtomic_bool m_deleted;				 ///< 定时器是否正在运行
    size_t m_expire_ticks;                  // 到期时间戳（毫秒）
} XTimerWheelData;
// 内联函数：分配包含 XTimerWheelData 的链表节点
static inline XListSNode* alloc_timer_node(void)
{
    //XPrintf("XListSNode准备创建\n");
    XListSNode* node= XListSNode_Create(XMalloc_MultiPool, XTimerWheelData);
    //XPrintf("XListSNode:%p 创建\n", node);
    return node;
}

// 内联函数：释放链表节点（仅供 delete_timer_node 使用）
static inline void free_timer_node(XListSNode* node)
{
    if (node) {
        //XFree_System(node);
        XFree_MultiPool(node);
    }
}

// 事件处理函数：释放链表节点
static void delete_timer_node_event(XVarList* argList)
{
    XVarList_args_1(argList, XListSNode*, node);
    free_timer_node(node);
}

// 专用函数：通过事件延迟释放链表节点
static void delete_timer_node(XTimeWheelGroup* group, XListSNode* node)
{
    XEventFunc* event = XEventFunc_create(delete_timer_node_event,XVarList_Create(XVar(XListSNode*, node)),NULL);
    if (event)
    {
        XCoreApplication_postEvent(group, event, XEVENT_PRIORITY_LOWEST);
       /* XTimerWheelData* data = (XTimerWheelData*)XListSNode_DataPtr(node);
        if (!XAtomic_load_bool(&data->m_deleted, XAtomic_MemoryOrder_Relaxed))
           
        else
            XEvent_delete_base(event);*/
    }
}
// 辅助函数：向上取整除法
static inline size_t ceil_div(size_t dividend, size_t divisor)
{
    if (divisor == 0) return 0;
    return (dividend + divisor - 1) / divisor;
}
// 专用函数：创建并初始化包含 XTimerWheelData 的链表节点
static XListSNode* create_timer_node(XTimerData* data, size_t expire_ticks);

static size_t calculate_max_time_range(XTimeWheelGroup* group);
static void VXTimeWheelGroup_deinit(XTimeWheelGroup* group);
static XHandle VXTimerGroupBase_addTimer(XTimeWheelGroup* group, XTimerData data);
static bool VXTimerGroupBase_removeTimer(XTimeWheelGroup* group, XHandle handle);
static void VXTimeWheelGroup_tick(XTimeWheelGroup* group);
static void VXTimeWheelGroup_addTimeWheel(XTimeWheelGroup* group, size_t slotsCount);
static void VXTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group);
// 专用函数：创建包含 XTimerWheelData 的链表节点
static XListSNode* create_timer_node(XTimerData* data, size_t expire_ticks)
{
    XListSNode* node = alloc_timer_node();
    if (!node) return NULL;
    memset(node,0,sizeof(XListSNode));
    // 获取节点中的数据指针
    XTimerWheelData* timer_data = (XTimerWheelData*)XListSNode_DataPtr(node);

    // 拷贝基础定时器数据
    timer_data->m_data = *data;

    // 设置额外字段
    timer_data->m_expire_ticks = expire_ticks;
    XAtomic_init(timer_data->m_deleted, false); // 初始化为运行中状态
    timer_data->m_data.m_autoDelete = true;

    return node;
}

XVtable* XTimeWheelGroup_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XTIMEWHEELGROUP_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    // 继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXTimerGroupBase_addTimer, VXTimerGroupBase_removeTimer,VXTimeWheelGroup_tick,
        VXTimeWheelGroup_addTimeWheel, VXTimeWheelGroup_removeTimeWheel,
    };
    // 追加虚函数
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTimeWheelGroup_deinit);
    //XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXTimerGroupBase_handler);
    return XVTABLE_DEFAULT;
}

void VXTimeWheelGroup_deinit(XTimeWheelGroup* group)
{
    while (!XVector_isEmpty_base(&group->m_timeWheel))
        XTimeWheelGroup_removeTimeWheel_base(group);
    //XVector_delete_base(&&group->m_timeWheel);
    XVector_deinit_base(&group->m_timeWheel);
    // 释放父对象
    XVtableGetFunc(XIODevice_class_init(), EXClass_Deinit, void(*)(XIODevice*))(group);
}
// 无锁添加到时间轮槽位 - 使用 MPSC 链表
static bool add_timer_to_wheel_lockfree(XTimeWheel* wheel, XListSNode* timer_node, size_t ticks)
{
    size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(&wheel->m_slots);
    XAtomic_uintptr_t* slot_ptr = XVector_at_base(&wheel->m_slots, expire_slot);

    // MPSC 链表头部插入 - 使用 CAS 确保原子性
    void* expected;
    void* desired = (void*)timer_node;

    do {
        expected = XAtomic_load_uintptr_t(slot_ptr, XAtomic_MemoryOrder_Relaxed);
        // 设置新节点的 next 指针为当前头节点
        timer_node->next = (XListSNode*)expected;
    } while (!XAtomic_compare_exchange_strong_uintptr_t(
        slot_ptr, &expected, desired,
        XAtomic_MemoryOrder_Release,
        XAtomic_MemoryOrder_Relaxed
    ));

    return true;
}

static bool addTimer_lockfree(XTimeWheelGroup* group, XListSNode* timer_node, size_t timeout_ticks)
{
    // 根据超时时间决定放入哪个时间轮
    XTimeWheel* wheel = NULL;
    size_t ticksCompare = 1, wheelSize = 1;
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i)
    {
        wheel = XVector_at_base(&group->m_timeWheel, i);
        ticksCompare *= XContainerSize(&wheel->m_slots);
        if (timeout_ticks < ticksCompare)
        {
            return add_timer_to_wheel_lockfree(wheel, timer_node, timeout_ticks / wheelSize - (i ? 1 : 0));
        }
        wheelSize *= XContainerSize(&wheel->m_slots);
    }
    return false;
}

XHandle VXTimerGroupBase_addTimer(XTimeWheelGroup* group, XTimerData data)
{
    if (data.m_timerCallback == NULL || ((data.m_interval == 0) && (data.m_timeout == 0)))
        return NULL;
   /* if (data.m_timeout == 0)
        data.m_isSingleShot = true;*/

    //校准下时间
    group->m_class.m_current_tick = XTimer_getCurrentTime() / group->m_class.m_precision;
    // 计算超时时间（转换为）
    size_t timeout_ticks = ceil_div(data.m_timeout, group->m_class.m_precision);
    if (timeout_ticks == 0)timeout_ticks = ceil_div(data.m_interval, group->m_class.m_precision);//如果超时时间是0就直接使用周期时间
    if (timeout_ticks == 0) timeout_ticks = 1; // 确保至少有一个节拍
    size_t expire_ticks = group->m_class.m_current_tick + timeout_ticks;

    // 创建包含 XTimerWheelData 的链表节点（一次性分配）
    XListSNode* timer_node = create_timer_node(&data, expire_ticks);
    if (!timer_node) return NULL;

    XMutex_lock(group->m_mutex);
    // 无锁添加 - 不需要任何互斥锁
    bool result = addTimer_lockfree(group, timer_node, timeout_ticks);
    if (result)
    {
        // m_isRun 已移除，m_deleted=false 表示运行中
        // 原子增加计数器
        XAtomic_fetch_add_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
    }
    else
    {
        // 通过事件释放节点
        delete_timer_node(group, timer_node);
        XMutex_unlock(group->m_mutex);
        return NULL;
    }
    XMutex_unlock(group->m_mutex);
    return timer_node;
}

bool VXTimerGroupBase_removeTimer(XTimeWheelGroup* groupBase, XHandle handle)
{
    XTimeWheelGroup* group = (XTimeWheelGroup*)groupBase;
    XListSNode* timer_node = (XListSNode*)handle;
    XTimerWheelData* timer_data = (XTimerWheelData*)XListSNode_DataPtr(timer_node);
    XMutex_lock(group->m_mutex);
    // 只做标记，不立即从链表中删除
    // m_deleted = true 表示已删除/不运行
    XAtomic_store_bool(&timer_data->m_deleted, true, XAtomic_MemoryOrder_Relaxed);
    XMutex_unlock(group->m_mutex);
    // 注意：实际内存清理在 handler 中进行
    return true;
}
// 计算时间轮的最大管理时间
size_t calculate_max_time_range(XTimeWheelGroup* group)
{
    if (XContainerSize(&group->m_timeWheel) == 0)
        return 0;

    // 获取精度（毫秒）
    uint16_t precision = ((XTimerGroupBase*)group)->m_precision;

    // 计算各级时间轮的总容量
    size_t total_slots = 1;
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i) {
        XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, i);
        total_slots *= XContainerSize(&wheel->m_slots);
    }

    // 最大时间范围 = 总槽数 * 精度
    return total_slots * precision;
}

void VXTimeWheelGroup_addTimeWheel(XTimeWheelGroup* group, size_t slotsCount)
{
    XTimeWheel wheel = { 0 };
    wheel.m_tick = 0;

    // 创建原子指针数组
    XVector_init(&wheel.m_slots, sizeof(XAtomic_uintptr_t));
    XVector_resize_base(&wheel.m_slots, slotsCount);

    // 初始化每个槽位为 NULL
    for (size_t i = 0; i < slotsCount; ++i) {
        XAtomic_uintptr_t* ptr = XVector_at_base(&wheel.m_slots, i);
        XAtomic_init(*ptr, NULL);
    }

    // 无锁添加时间轮（假设只在初始化时调用）
    XVector_push_back_base(&group->m_timeWheel, &wheel);
    ((XTimerGroupBase*)group)->m_max_time = calculate_max_time_range(group);
}

void VXTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group)
{
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;

    XTimeWheel* wheel = XVector_back_base(&group->m_timeWheel);
    // 遍历槽数组
    for (size_t i = 0; i < XContainerSize(&wheel->m_slots); ++i)
    {
        XAtomic_uintptr_t* slot_ptr = XVector_at_base(&wheel->m_slots, i);
        void* head = XAtomic_exchange_uintptr_t(slot_ptr, NULL, XAtomic_MemoryOrder_Acquire);

        if (head != NULL)
        {
            XListSNode* node = (XListSNode*)head;
            while (node)
            {
                XListSNode* next = node->next;
                // 通过事件释放节点
                delete_timer_node(group, node);
                node = next;
            }
        }
    }
    // 删除最后一个轮
    XVector_pop_back_base(&group->m_timeWheel);
    ((XTimerGroupBase*)group)->m_max_time = calculate_max_time_range(group);
}
//时间轮降级
static void cascade_timers(XTimeWheelGroup* group, XTimeWheel* higher_level, int slot_index, size_t higher_level_idx)
{
    if (higher_level_idx == 0) return; // 已到最底层，无需降级
    XAtomic_uintptr_t* slot_ptr = XVector_at_base(&higher_level->m_slots, slot_index);
    void* head = XAtomic_exchange_uintptr_t(slot_ptr, NULL, XAtomic_MemoryOrder_Acquire);

    if (head == NULL) return;

    XListSNode* node = (XListSNode*)head;
    while (node)
    {
        XListSNode* next = node->next;
        XTimerWheelData* timer = (XTimerWheelData*)XListSNode_DataPtr(node);

        // 检查是否已被标记删除（m_deleted == true 表示已删除/不运行）
        if (XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Relaxed)) {
            // 通过事件释放节点
            delete_timer_node(group, node);
            XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
            node = next;
            continue;
        }

        // 计算剩余ticks（距离到期的滴答数）
        size_t remaining_ticks = timer->m_expire_ticks - group->m_class.m_current_tick;
        bool isdelete = false;

        if (timer->m_expire_ticks <= ((XTimerGroupBase*)group)->m_current_tick)
        {
            // 已到期，直接触发（此时 m_deleted == false，表示还在运行）
            XTimerData_out(timer);

            if ((!timer->m_data.m_isSingleShot) && (timer->m_data.m_interval > 0))
            {
                // 有定时间隔的重新添加
                size_t timeout_ticks = timer->m_data.m_interval / group->m_class.m_precision;
                timer->m_expire_ticks = ((XTimerGroupBase*)group)->m_current_tick + timeout_ticks;

                // 创建新的链表节点
                XListSNode* new_node = create_timer_node(&timer->m_data, timer->m_expire_ticks);
                if (new_node) {
                    addTimer_lockfree(group, new_node, timeout_ticks);
                }
            }
            else if (timer->m_data.m_isSingleShot && timer->m_data.m_autoDelete)
            {
                isdelete = true;
            }
        }
        else {
            // 降级到下一级轮（higher_level_idx - 1）
            XTimeWheel* lower_level = XVector_at_base(&group->m_timeWheel, higher_level_idx - 1);
            size_t lower_slots = XContainerSize(&lower_level->m_slots);
            size_t lower_max_ticks = lower_slots;

            if (remaining_ticks < lower_max_ticks) {
                // 直接放入下一级轮
                add_timer_to_wheel_lockfree(lower_level, node, remaining_ticks);
                // 节点已重新链接，不需要释放
                node = next;
                continue;
            }
            else {
                // 先放入下一级轮，等待其后续降级
                size_t relative_ticks = remaining_ticks / lower_max_ticks;
                add_timer_to_wheel_lockfree(lower_level, node, relative_ticks);
                // 节点已重新链接，不需要释放
                node = next;
                continue;
            }
        }

        if (isdelete)
        {
            // 通过事件释放节点
            delete_timer_node(group, node);
            XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
        }

        node = next;
    }
}
void VXTimeWheelGroup_tick(XTimeWheelGroup* group)
{
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;

    XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);

    // 处理当前节拍 - 使用第一个（最低级）时间轮
    XTimeWheel* wheel = XVector_front_base(&group->m_timeWheel);
    int current_slot = wheel->m_tick % XVector_size_base(&wheel->m_slots);

    // 原子获取并清空当前槽的链表
    XAtomic_uintptr_t* slot_ptr = XVector_at_base(&wheel->m_slots, current_slot);
    void* head = XAtomic_exchange_uintptr_t(slot_ptr, NULL, XAtomic_MemoryOrder_Acquire);
    XMutex_lock(group->m_mutex);
    if (head != NULL)
    {
        XListSNode* node = (XListSNode*)head;
        while (node)
        {
            XListSNode* next = node->next;
            XTimerWheelData* timer = (XTimerWheelData*)XListSNode_DataPtr(node);
            bool isdelete = true;

            // 检查是否已被标记删除（轮询时清理）
            // m_deleted == true 表示已删除/不运行
            if (XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Relaxed)) {
                goto cleanup;
            }

            // m_deleted == false 表示还在运行
            if (timer->m_expire_ticks <= groupBase->m_current_tick)
            {
                if (!XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Relaxed))
                    XTimerData_out(timer);
                else
                    isdelete = false;
                if ((!timer->m_data.m_isSingleShot) && (timer->m_data.m_interval > 0))
                {
                    // 有定时间隔的重新添加 - 直接重用节点而不是创建新节点
                    size_t timeout_ticks = ceil_div(timer->m_data.m_interval, group->m_class.m_precision);
                    if (timeout_ticks == 0)timeout_ticks = 1;
                    timer->m_expire_ticks = groupBase->m_current_tick + timeout_ticks;

                    // 关键修改：直接重用当前节点，而不是创建新节点

                    if (addTimer_lockfree(group, node, timeout_ticks))
                    {
                        isdelete = false;
                    }
                }
            }
            else
            {
                size_t timeout_ticks = timer->m_expire_ticks - groupBase->m_current_tick;
                // 重新添加到当前时间轮
                add_timer_to_wheel_lockfree(wheel, node, timeout_ticks);
                // 节点已重新链接，不需要释放
                node = next;
                continue;
            }

        cleanup:
            if (isdelete)
            {
                // 通过事件释放节点
                delete_timer_node(group, node);
                XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
            }

            node = next;
        }
    }

    // 低级轮向前推进
    ++wheel->m_tick;

    // 检查是否需要触发中级轮降级
    XTimeWheel* currentWheel = NULL;
    size_t ticksCompare = 1;
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i)
    {
        currentWheel = XVector_at_base(&group->m_timeWheel, i);
        ticksCompare *= XContainerSize(&currentWheel->m_slots);
        if (groupBase->m_current_tick % ticksCompare == 0)
        {
            if (i + 1 >= XContainerSize(&group->m_timeWheel))
                break; // 下一个时间轮不存在
            XTimeWheel* nextWheel = XVector_at_base(&group->m_timeWheel, i + 1);
            int next_slot = (nextWheel->m_tick % XContainerSize(&nextWheel->m_slots));
            cascade_timers(group, nextWheel, next_slot, i + 1);
            ++nextWheel->m_tick;
            continue;
        }
        break;
    }
    XMutex_unlock(group->m_mutex);
    // 当前节拍完成，增加全局节拍计数
    ++groupBase->m_current_tick;
}

XTimeWheelGroup* XTimeWheelGroup_create(uint16_t precision)
{
    if (precision == 0)
        return NULL;
    XTimeWheelGroup* group = XMalloc_System(sizeof(XTimeWheelGroup));
    if (group == NULL)
        return group;
    XTimeWheelGroup_init(group, precision);
    Set_Class_MemoryFree(group, XFree_System);
    return group;
}

void XTimeWheelGroup_init(XTimeWheelGroup* group, uint16_t precision)
{
    if (group == NULL || precision == 0)
        return;
    //初始化父类以外的数据
    memset(((XTimerGroupBase*)group) + 1, 0, sizeof(XTimeWheelGroup) - sizeof(XTimerGroupBase));
    XTimerGroupBase_init(group, precision);
    XClassGetVtable(group) = XTimeWheelGroup_class_init();
    //初始化数据
    XVector_init(&group->m_timeWheel, sizeof(XTimeWheel));
    XContainerSetDataDeinitMethod(&group->m_timeWheel, XVector_deinit_base);
    //group->m_timeWheel = XVector_Create(XTimeWheel);
    group->m_class.m_current_tick = XTimer_getCurrentTime() / group->m_class.m_precision;
    XAtomic_init(group->m_count, 0);
    group->m_mutex=XMutex_create(XLock_Spin);
}

void XTimeWheelGroup_addTimeWheel_base(XTimeWheelGroup* group, size_t slotsCount)
{
    if (ISNULL(group, "") || ISNULL(slotsCount, "") || ISNULL(XClassGetVtable(group), ""))
        return;
    XClassGetVirtualFunc(group, EXTimeWheelGroup_Add_TimeWheel, void(*)(XTimeWheelGroup*, size_t))(group, slotsCount);
}

void XTimeWheelGroup_removeTimeWheel_base(XTimeWheelGroup* group)
{
    if (ISNULL(group, "") || ISNULL(XClassGetVtable(group), ""))
        return;
    XClassGetVirtualFunc(group, EXTimeWheelGroup_Remove_TimeWheel, void(*)(XTimeWheelGroup*))(group);
}

size_t XTimeWheelGroup_count(XTimeWheelGroup* group)
{
    return group ? XAtomic_load_size_t(&group->m_count, XAtomic_MemoryOrder_Relaxed) : 0;
}
static XTimeWheelGroup* global_XTimeWheelGroup = NULL;
static void XTimeWheelGroup_global_init()
{
    if (global_XTimeWheelGroup)return;
    global_XTimeWheelGroup = XTimeWheelGroup_create(1);
    XObject_moveToThread(global_XTimeWheelGroup, XThreadData_mainThread()->m_thread);
    XTimeWheelGroup_addTimeWheel_base(global_XTimeWheelGroup, 30);
    XTimeWheelGroup_addTimeWheel_base(global_XTimeWheelGroup, 10);
    XTimeWheelGroup_addTimeWheel_base(global_XTimeWheelGroup, 10);
}
XTimeWheelGroup* XTimeWheelGroup_global()
{
    if (!global_XTimeWheelGroup)
        XTimeWheelGroup_global_init();
    return global_XTimeWheelGroup;
}

bool XTimeWheelGroup_GlobalExists(void)
{
    return global_XTimeWheelGroup;
}
