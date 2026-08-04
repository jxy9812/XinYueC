#include"XTimeWheelGroup.h"
#include"XListSLinked.h"
#include"XMemory.h"
#include"XStack.h"
#include"XThreadData.h"
#include"XCoreApplication.h"
#include"XDateTime.h"
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

/*
 * 槽位是无锁 Treiber 链表。生产者在 CAS 前会暂存旧槽头，因此消费者
 * 不能立即释放已摘下的节点，否则内存池复用地址后会产生 ABA。先退休，
 * 等所有生产者离开槽访问临界区后再统一回收。
 */
static void reclaim_retired_nodes(XTimeWheelGroup* group)
{
    if (XAtomic_load_size_t(&group->m_activeProducers, XAtomic_MemoryOrder_SeqCst) != 0)
        return;

    XListSNode* node = group->m_retiredNodes;
    group->m_retiredNodes = NULL;
    while (node) {
        XListSNode* next = node->next;
        free_timer_node(node);
        node = next;
    }
}

static void delete_timer_node(XTimeWheelGroup* group, XListSNode* node)
{
    if (!node) return;
    node->next = group->m_retiredNodes;
    group->m_retiredNodes = node;
    XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
}

/*
 * removeTimer 只负责标记，避免生产者直接改写槽内链表。消费者在下一轮
 * 原子摘下各槽，过滤已取消节点，再把其余节点整体挂回。这样取消的长
 * 超时定时器可以及时回收，又不会破坏并发插入槽头的生产者。
 */
static void sweep_cancelled_timers(XTimeWheelGroup* group)
{
    if (!XAtomic_exchange_bool(&group->m_hasCancelledTimers, false,
                               XAtomic_MemoryOrder_AcqRel))
        return;

    for (size_t wheel_index = 0;
         wheel_index < XContainerSize(&group->m_timeWheel);
         ++wheel_index) {
        XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, wheel_index);

        for (size_t slot_index = 0;
             slot_index < XContainerSize(&wheel->m_slots);
             ++slot_index) {
            XAtomic_uintptr_t* slot = XVector_at_base(&wheel->m_slots, slot_index);
            XListSNode* node = (XListSNode*)XAtomic_exchange_uintptr_t(
                slot, (uintptr_t)NULL, XAtomic_MemoryOrder_SeqCst);
            XListSNode* keep_head = NULL;
            XListSNode* keep_tail = NULL;

            while (node) {
                XListSNode* next = node->next;
                XTimerWheelData* timer = (XTimerWheelData*)XListSNode_DataPtr(node);

                if (XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Acquire)) {
                    delete_timer_node(group, node);
                } else {
                    node->next = NULL;
                    if (keep_tail)
                        keep_tail->next = node;
                    else
                        keep_head = node;
                    keep_tail = node;
                }
                node = next;
            }

            if (keep_head) {
                uintptr_t expected;

                XAtomic_fetch_add_size_t(&group->m_activeProducers, 1,
                                         XAtomic_MemoryOrder_SeqCst);
                expected = XAtomic_load_uintptr_t(slot, XAtomic_MemoryOrder_SeqCst);
                do {
                    keep_tail->next = (XListSNode*)expected;
                } while (!XAtomic_compare_exchange_strong_uintptr_t(
                    slot, &expected, (uintptr_t)keep_head,
                    XAtomic_MemoryOrder_SeqCst,
                    XAtomic_MemoryOrder_SeqCst));
                XAtomic_fetch_sub_size_t(&group->m_activeProducers, 1,
                                         XAtomic_MemoryOrder_SeqCst);
            }
        }
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
static XHandle VXTimerGroupBase_addTimerNs(XTimeWheelGroup* group, XTimerData data);
static bool VXTimerGroupBase_removeTimer(XTimeWheelGroup* group, XHandle handle);
static void VXTimeWheelGroup_tick(XTimeWheelGroup* group);
static void VXTimerGroupBase_handler(XTimerGroupBase* group);
static void VXTimeWheelGroup_clear(XTimeWheelGroup* group);
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
    XVTABLE_INIT_DEFAULT(XTimeWheelGroup)
	XCLASS_SET_CLASS_NAME_DEFAULT("XTimeWheelGroup");
    // 继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = {
        VXTimerGroupBase_addTimerNs,VXTimerGroupBase_removeTimer,VXTimeWheelGroup_tick,VXTimerGroupBase_handler,
        VXTimeWheelGroup_clear
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
        XTimeWheelGroup_removeTimeWheel(group);
    //XVector_delete_base(&&group->m_timeWheel);
    XVector_deinit_base(&group->m_timeWheel);
    reclaim_retired_nodes(group);
    // 释放父对象
    XClass_Parent(XIODevice,EXClass_Deinit, void(*)(XIODevice*))(group);
}
// 无锁添加到时间轮槽位 - 使用 MPSC 链表
static bool add_timer_to_wheel_lockfree(XTimeWheelGroup* group, XTimeWheel* wheel,
    XListSNode* timer_node, size_t ticks)
{
    size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(&wheel->m_slots);
    XAtomic_uintptr_t* slot_ptr = XVector_at_base(&wheel->m_slots, expire_slot);

    XAtomic_fetch_add_size_t(&group->m_activeProducers, 1, XAtomic_MemoryOrder_SeqCst);
    uintptr_t expected = XAtomic_load_uintptr_t(slot_ptr, XAtomic_MemoryOrder_SeqCst);
    uintptr_t desired = (uintptr_t)timer_node;

    do {
        /* 节点已经是槽头时视为成功，绝不能释放仍由槽位持有的节点。 */
        if (expected == desired) {
            XAtomic_fetch_sub_size_t(&group->m_activeProducers, 1, XAtomic_MemoryOrder_SeqCst);
            return true;
        }
        timer_node->next = (XListSNode*)expected;
    } while (!XAtomic_compare_exchange_strong_uintptr_t(
        slot_ptr, &expected, desired,
        XAtomic_MemoryOrder_SeqCst,
        XAtomic_MemoryOrder_SeqCst));

    XAtomic_fetch_sub_size_t(&group->m_activeProducers, 1, XAtomic_MemoryOrder_SeqCst);
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
            // 计算在当前轮上应延迟的滴答数（绝对偏移）
            size_t offset_ticks = timeout_ticks / wheelSize;
            if (i > 0 && offset_ticks == 0) offset_ticks = 1; // 至少偏移 1 格，避免立即触发
            return add_timer_to_wheel_lockfree(group, wheel, timer_node, offset_ticks);
        }
        wheelSize *= XContainerSize(&wheel->m_slots);
    }
    return false;
}

XHandle VXTimerGroupBase_addTimerNs(XTimeWheelGroup* group, XTimerData data)
{
    if (data.m_timerCallback == NULL || ((data.m_interval == 0) && (data.m_timeout == 0)))
        return NULL;
    data.m_interval /= 1000000ULL;
    data.m_timeout /= 1000000ULL;
    //校准下时间
    group->m_class.m_current_tick =  ((XTimerGroupBase*)group)->m_high_res_time_func()/** 1000000ULL *// group->m_class.m_precision;
    // 计算超时时间（转换为）
    size_t timeout_ticks = ceil_div(data.m_timeout, group->m_class.m_precision);
    if (timeout_ticks == 0)timeout_ticks = ceil_div(data.m_interval, group->m_class.m_precision);//如果超时时间是0就直接使用周期时间
    if (timeout_ticks == 0) timeout_ticks = 1; // 确保至少有一个节拍
    size_t expire_ticks = group->m_class.m_current_tick + timeout_ticks;

    // 创建包含 XTimerWheelData 的链表节点（一次性分配）
    XListSNode* timer_node = create_timer_node(&data, expire_ticks);
    if (!timer_node) return NULL;

    bool result = addTimer_lockfree(group, timer_node, timeout_ticks);
    if (result)
    {
        // m_isRun 已移除，m_deleted=false 表示运行中
        // 原子增加计数器
        XAtomic_fetch_add_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Release);
    }
    else
    {
        /* 节点尚未计入 m_count，不能走会递减计数的删除路径。 */
        free_timer_node(timer_node);
        return NULL;
    }
    return timer_node;
}
bool VXTimerGroupBase_removeTimer(XTimeWheelGroup* groupBase, XHandle handle)
{
    XTimeWheelGroup* group = (XTimeWheelGroup*)groupBase;
    XListSNode* timer_node = (XListSNode*)handle;
    XTimerWheelData* timer_data = (XTimerWheelData*)XListSNode_DataPtr(timer_node);
    // 只做标记，不立即从链表中删除
    // m_deleted = true 表示已删除/不运行
    XAtomic_store_bool(&timer_data->m_deleted, true, XAtomic_MemoryOrder_Release);
    XAtomic_store_bool(&group->m_hasCancelledTimers, true, XAtomic_MemoryOrder_Release);
    // 实际内存清理由 handler 的消费者清扫完成
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

void XTimeWheelGroup_addTimeWheel(XTimeWheelGroup* group, size_t slotsCount)
{
    XTimeWheel wheel = { 0 };
    wheel.m_tick = 0;

    // 创建原子指针数组
    XVector_init(&wheel.m_slots, sizeof(XAtomic_uintptr_t),false);
    XVector_resize_base(&wheel.m_slots, slotsCount);

    // 初始化每个槽位为 NULL
    for (size_t i = 0; i < slotsCount; ++i) {
        XAtomic_uintptr_t* ptr = XVector_at_base(&wheel.m_slots, i);
        XAtomic_init(*ptr, NULL);
    }

    // 无锁添加时间轮（假设只在初始化时调用）
    XVector_push_back_1_base(&group->m_timeWheel, &wheel);
    ((XTimerGroupBase*)group)->m_max_time = calculate_max_time_range(group);
}

void XTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group)
{
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;

    XTimeWheel* wheel = XVector_back_base(&group->m_timeWheel);
    // 遍历槽数组
    for (size_t i = 0; i < XContainerSize(&wheel->m_slots); ++i)
    {
        XAtomic_uintptr_t* slot_ptr = XVector_at_base(&wheel->m_slots, i);
        uintptr_t head = XAtomic_exchange_uintptr_t(slot_ptr, (uintptr_t)NULL, XAtomic_MemoryOrder_SeqCst);

        if (head != (uintptr_t)NULL)
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
    reclaim_retired_nodes(group);
    ((XTimerGroupBase*)group)->m_max_time = calculate_max_time_range(group);
}
void VXTimeWheelGroup_clear(XTimeWheelGroup* group)
{
    while (!XVector_isEmpty_base(&group->m_timeWheel))
    {
        XTimeWheelGroup_removeTimeWheel(group);
    }
}
// 公共函数：处理一个定时器节点链表
static void process_timer_list(XTimeWheelGroup* group, XListSNode* head, size_t current_tick)
{
    if (!head) return;

    XListSNode* node = head;
    while (node)
    {
        XListSNode* next = node->next;
        XTimerWheelData* timer = (XTimerWheelData*)XListSNode_DataPtr(node);
        bool isdelete = true; // 默认需要删除，除非明确复用

                                // --- 统一的删除检查 ---
        if (XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Acquire)) {
            goto final_cleanup;
        }

        // --- 统一的到期判断和处理 ---
        if (timer->m_expire_ticks <= current_tick)
        {
            // 已到期，直接触发
            XTimerData_out(&timer->m_data);

            if ((!timer->m_data.m_isSingleShot) && (timer->m_data.m_interval > 0))
            {
                // 循环定时器：复用当前节点
                size_t timeout_ticks = ceil_div(timer->m_data.m_interval, group->m_class.m_precision);
                if (timeout_ticks == 0) timeout_ticks = 1;
                timer->m_expire_ticks = current_tick + timeout_ticks;

                if (addTimer_lockfree(group, node, timeout_ticks)) {
                    isdelete = false; // 成功复用，不需要删除
                }
                // 如果 addTimer_lockfree 失败，则 isdelete 保持为 true
            }
            // 对于单次定时器，isdelete 保持为 true，将在下方清理
        }
        else {
            // 未到期：不应出现在 L0 中，用 addTimer_lockfree 重新分配到正确的轮次
            size_t remaining_ticks = timer->m_expire_ticks - current_tick;
            node->next = NULL;
            if (addTimer_lockfree(group, node, remaining_ticks)) {
                isdelete = false;
                node = next;
                continue;
            }
            // addTimer_lockfree 失败，则 isdelete 保持 true，走 final_cleanup 删除
        }

    final_cleanup:
        if (isdelete) {
            delete_timer_node(group, node);
        }

        node = next;
    }
}
// 获取第 level 级时间轮每个滴答对应的全局 tick 数（0 表示第一级）
static size_t get_wheel_unit(XTimeWheelGroup* group, size_t level)
{
    size_t unit = 1;
    for (size_t i = 0; i < level; ++i) {
        XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, i);
        unit *= XContainerSize(&wheel->m_slots);
    }
    return unit;
}
//时间轮降级
static void cascade_timers(XTimeWheelGroup* group, XTimeWheel* higher_level, size_t slot_index, size_t higher_level_idx)
{
    if (higher_level_idx == 0) return;

    // 原子获取高级轮当前槽的所有定时器
    XAtomic_uintptr_t* slot_ptr = XVector_at_base(&higher_level->m_slots, slot_index);
    uintptr_t head = XAtomic_exchange_uintptr_t(slot_ptr, (uintptr_t)NULL, XAtomic_MemoryOrder_SeqCst);
    if (head == (uintptr_t)NULL) return;

    // 低级轮索引及每个滴答对应的全局 tick 数
    size_t lower_idx = higher_level_idx - 1;
    size_t lower_unit = get_wheel_unit(group, lower_idx);
    XTimeWheel* lower_level = XVector_at_base(&group->m_timeWheel, lower_idx);
    size_t lower_slots = XContainerSize(&lower_level->m_slots);

    XListSNode* node = (XListSNode*)head;
    while (node)
    {
        XListSNode* next = node->next;          // 先保存原链表的下一个节点
        XTimerWheelData* timer = (XTimerWheelData*)XListSNode_DataPtr(node);

                // 检查是否已被标记删除
        if (XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Acquire)) {
            delete_timer_node(group, node);
            node = next;
            continue;
        }

        // 计算剩余全局滴答数
        // 防御：如果定时器已到期（m_expire_ticks <= current_tick），直接放入低级轮第0槽，下次tick立即处理
        size_t current_tick = group->m_class.m_current_tick;
        if (timer->m_expire_ticks <= current_tick) {
            node->next = NULL;
            if(!add_timer_to_wheel_lockfree(group, lower_level, node, 0))
                delete_timer_node(group, node);
            node = next;
            continue;
        }
        size_t remaining_global_ticks = timer->m_expire_ticks - current_tick;
        // 计算在低级轮上还需要等待的滴答数
        size_t ticks_in_lower_wheel = remaining_global_ticks / lower_unit;
        if (ticks_in_lower_wheel >= lower_slots) {
            // 超出低级轮范围，使用 addTimer_lockfree 重新分配到正确的时间轮
            node->next = NULL;
            if (!addTimer_lockfree(group, node, remaining_global_ticks)) {
                delete_timer_node(group, node);
            }
            node = next;
            continue;
        }
        // ticks_in_lower_wheel == 0 是合法的，表示放入低级轮的当前槽(即将到期)
        // 重置节点的 next 指针，确保它不再指向原链表
        node->next = NULL;
        // 插入到低级轮
        if (!add_timer_to_wheel_lockfree(group, lower_level, node, ticks_in_lower_wheel)) {
            // 降级失败，释放节点
            delete_timer_node(group, node);
        }
        node = next;
    }
}
void VXTimeWheelGroup_tick(XTimeWheelGroup* group)
{
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;

    sweep_cancelled_timers(group);

    XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);
    size_t current_tick = groupBase->m_current_tick;
    XTimeWheel* wheel = XVector_front_base(&group->m_timeWheel);
    size_t current_slot = wheel->m_tick % XContainerSize(&wheel->m_slots);

    // 原子获取并清空当前槽的链表
    XAtomic_uintptr_t* slot_ptr = XVector_at_base(&wheel->m_slots, current_slot);
    uintptr_t head = XAtomic_exchange_uintptr_t(slot_ptr, (uintptr_t)NULL, XAtomic_MemoryOrder_SeqCst);

    // 处理当前槽的所有定时器
    process_timer_list(group, (XListSNode*)head, current_tick);

    // 低级轮向前推进
    ++wheel->m_tick;

    // 检查是否需要触发高级轮降级
    // 算法：当低级轮转完一圈（m_current_tick % 低级轮总槽数 == 0），触发下一级轮降级
    size_t wheel_unit = 1; // 当前级轮每个滴答对应的全局 tick 跨度
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i)
    {
        XTimeWheel* curWheel = XVector_at_base(&group->m_timeWheel, i);
        size_t cur_slots = XContainerSize(&curWheel->m_slots);
        wheel_unit *= cur_slots;

        // 当前 tick 是 wheel_unit 的整数倍，说明第 i 级轮转完了一圈
        // 需要将第 i+1 级轮的当前槽降级到第 i 级轮
        if ((current_tick + 1) % wheel_unit == 0)
        {
            if (i + 1 >= XContainerSize(&group->m_timeWheel))
                break; // 没有更高级的时间轮

            XTimeWheel* nextWheel = XVector_at_base(&group->m_timeWheel, i + 1);
            size_t next_slot = nextWheel->m_tick % XContainerSize(&nextWheel->m_slots);
            cascade_timers(group, nextWheel, next_slot, i + 1);
            ++nextWheel->m_tick;
        }
        else {
            // 当前级轮未转完一圈，无需处理更高级轮
            break;
        }
    }
    reclaim_retired_nodes(group);
    // 当前节拍完成，增加全局节拍计数
    ++groupBase->m_current_tick;
}
void VXTimerGroupBase_handler(XTimerGroupBase* group)
{
    XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);
    if (!groupBase->m_high_res_time_func)return;

    /* 即使当前毫秒尚未推进，也要及时回收上一轮取消的定时器。 */
    sweep_cancelled_timers((XTimeWheelGroup*)group);
    reclaim_retired_nodes((XTimeWheelGroup*)group);

    size_t tick = groupBase->m_high_res_time_func() / groupBase->m_precision; // 当前滴答数

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
    XVector_init(&group->m_timeWheel, sizeof(XTimeWheel),false);
    XContainerSetDataDeinitMethod(&group->m_timeWheel, XVector_deinit_base);
    //group->m_timeWheel = XVector_Create(XTimeWheel);
    //group->m_class.m_current_tick = ((XTimerGroupBase*)group)->m_high_res_time_func() / group->m_class.m_precision;
    XAtomic_init(group->m_count, 0);
    XAtomic_init(group->m_activeProducers, 0);
    XAtomic_init(group->m_hasCancelledTimers, false);
    group->m_retiredNodes = NULL;
    //group->m_mutex=XMutex_create(XLock_Spin);
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
    XTimerGroupBase_setHighResTimeFunc(global_XTimeWheelGroup,XDateTime_currentMSecsSinceEpoch );
    XTimeWheelGroup_addTimeWheel(global_XTimeWheelGroup, 30);
    XTimeWheelGroup_addTimeWheel(global_XTimeWheelGroup, 30);
    XTimeWheelGroup_addTimeWheel(global_XTimeWheelGroup, 30);
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
