#include"XTimeWheelGroup.h"
#include"XListSLinked.h"
#include"XMemory.h"
#include"XStack.h"
#include"XThreadData.h"
#include"XCoreApplication.h"
#include"XDateTime.h"
#include"XPrintf.h" /* [WHEEL 2026-10-29 诊断] 刻度滞后对账打点。 */
#include"XIODevice.h" /* XIODevice_class_init（防 C4013 隐式声明，指针截断）。 */
#include<stdlib.h> /* [WHEELCLK 2026-10-29 诊断] getenv。 */
#include<stdio.h> /* [WHEELCLK 2026-10-29 诊断] fprintf(stderr) 无缓冲对账流。 */
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
    XAtomic_Bool m_deleted;				 ///< 定时器是否正在运行
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
    /* 2026-10-22 ESP8266 崩溃定位打点（定位后删）：进入 sweep 与 exchange
     * 前后各一条，区分崩在 exchange 本身还是后续槽遍历。 */
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
            XAtomic_Uintptr* slot = XVector_at_base(&wheel->m_slots, slot_index);
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
    //XClassDelete(&&group->m_timeWheel);
    XClassDeinit(&group->m_timeWheel);
    reclaim_retired_nodes(group);
    // 释放父对象
    XClass_Deinit_Parent(XObject, group);
}
// 无锁添加到时间轮槽位 - 使用 MPSC 链表
static bool add_timer_to_wheel_lockfree(XTimeWheelGroup* group, XTimeWheel* wheel,
    XListSNode* timer_node, size_t ticks)
{
    size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(&wheel->m_slots);
    XAtomic_Uintptr* slot_ptr = XVector_at_base(&wheel->m_slots, expire_slot);

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

/* [级联重落槽根修 2026-10-29] cascade_wheel 非 NULL 表示处于 cascade_timers
 * 重分配上下文：该轮本边界的槽已弹空、本调用返回后 tick 立即 ++m_tick——
 * offset=0 指向的正是刚被弹空越过的槽，节点将滞留整圈（L1 30 边界=900 刻
 * 度、L2=27000 刻度）才被再扫（板上 50ms 轮询定时器 921ms 冻结根因）。
 * 故仅当目标轮==级联源轮且 offset==0 时顺延到 m_tick+1 槽（下一可用边界，
 * 兑现最多晚 1 拍）；offset≥1 本就指向未消费槽、更高级轮的本边界在本 tick
 * 尚未消费，均不得顺延（一律 +1 反令 remaining∈(unit,2·unit] 的节点多等
 * 一圈，并为"低级级联落更高级轮"制造新滞留）。 */
static bool addTimer_lockfree(XTimeWheelGroup* group, XListSNode* timer_node,
                              size_t timeout_ticks, XTimeWheel* cascade_wheel)
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
            size_t offset_ticks;
            if (i == 0) {
                // L0 每 tick 消费一次，槽偏移=全局刻度数，无相位问题
                offset_ticks = timeout_ticks;
            } else {
                /* [相位落槽修正 2026-10-28] 高层轮的槽只在全局刻度到达本级
                 * 边界时被级联（L1 每 30 刻度、L2 每 900 刻度一次），且第 k
                 * 个边界级联的是槽 (m_tick+k-1)。旧实现 offset = timeout /
                 * 本级槽跨度并放到 (m_tick+offset) 槽，等价于"从下个边界再
                 * 数 offset 个边界"——周期中段注册的定时器要多等一整圈：
                 * 1000ms 定时器（L2 offset=1）实测固定 1800ms 触发、每次
                 * 晚 ~800ms（顶栏时钟 1s 刷新变 1.8s 实证，桌面与刻度无关
                 * 纯数学缺陷）。修正按目标边界序号落槽：距下一边界 d 刻度，
                 * 第 k=(timeout-d)/unit+1 个边界级联时残余 (0,unit] 交由低
                 * 级轮/到期直兑现路径完成，实际触发时刻回到 ±1 槽精度。 */
                size_t unit = wheelSize; /* 本级每槽对应的全局刻度跨度 */
                size_t d = unit - (group->m_class.m_current_tick % unit);
                size_t k = ((timeout_ticks > d) ?
                            (timeout_ticks - d) / unit : 0) + 1;
                offset_ticks = k - 1; /* 0 合法=下一边界级联（残余即到期） */
                if (cascade_wheel == wheel && offset_ticks == 0)
                    offset_ticks = 1; /* 本边界槽已被本次级联消费 */
            }
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
    if (((XTimerGroupBase*)group)->m_high_res_time_func == NULL)
        return NULL;/* 未设置时间源无法校准刻度，按添加失败处理（与 handler 判空惯例一致） */
    /* [刻度轴统一 2026-10-29] 刻度一律按 size_t 宽度取值：handler 的
     * 当前刻度计算是 size_t（32 位平台上截断），本行原样全宽写入
     * m_current_tick（uint64）——注入源返回值超过 2^32 时（纪元毫秒
     * 轴 / 乱源高字）添加侧与消费侧各在一条轴上，轮永不兑现。两侧同
     * 宽后时间轮有效量程 = size_t 满量程刻度，远超轮自身 max_time。 */
    {
        size_t now_ticks = (size_t)(((XTimerGroupBase*)group)->m_high_res_time_func() /
                                    ((XTimerGroupBase*)group)->m_precision);
        size_t cur_ticks = (size_t)((XTimerGroupBase*)group)->m_current_tick;
        /* 单调轴护栏：只前进不回拨。乱源/对时导致的低读数一律不采纳，
         * 刻度信任最近一次合法读数（handler 同护栏，见其注记）。 */
        if (now_ticks > cur_ticks)
            ((XTimerGroupBase*)group)->m_current_tick = now_ticks;
    }
    // 计算超时时间（转换为）
    size_t timeout_ticks = ceil_div(data.m_timeout, group->m_class.m_precision);
    if (timeout_ticks == 0)timeout_ticks = ceil_div(data.m_interval, group->m_class.m_precision);//如果超时时间是0就直接使用周期时间
    if (timeout_ticks == 0) timeout_ticks = 1; // 确保至少有一个节拍
    size_t expire_ticks = group->m_class.m_current_tick + timeout_ticks;

    // 创建包含 XTimerWheelData 的链表节点（一次性分配）
    XListSNode* timer_node = create_timer_node(&data, expire_ticks);
    if (!timer_node) return NULL;

    bool result = addTimer_lockfree(group, timer_node, timeout_ticks, NULL);
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
    if (group == NULL)
        return;
    XTimeWheel wheel = { 0 };
    wheel.m_tick = 0;

    // 创建原子指针数组
    XVector_init(&wheel.m_slots, sizeof(XAtomic_Uintptr),false);
    if (!XVector_resize_base(&wheel.m_slots, slotsCount))
    {
        // 扩容失败：槽数组缓冲所有权未转移，就地释放后再返回
        XClassDeinit(&wheel.m_slots);
        return;
    }

    // 初始化每个槽位为 NULL
    for (size_t i = 0; i < slotsCount; ++i) {
        XAtomic_Uintptr* ptr = XVector_at_base(&wheel.m_slots, i);
        XAtomic_init(*ptr, NULL);
    }

    // 无锁添加时间轮（假设只在初始化时调用）
    if (!XVector_push_back_1_base(&group->m_timeWheel, &wheel))
    {
        // 入队失败：槽缓冲仍在栈临时对象上，就地释放避免泄漏
        XClassDeinit(&wheel.m_slots);
        return;
    }
    ((XTimerGroupBase*)group)->m_max_time = calculate_max_time_range(group);
}

void XTimeWheelGroup_removeTimeWheel(XTimeWheelGroup* group)
{
    if (group == NULL)
        return;
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;

    XTimeWheel* wheel = XVector_back_base(&group->m_timeWheel);
    // 遍历槽数组
    for (size_t i = 0; i < XContainerSize(&wheel->m_slots); ++i)
    {
        XAtomic_Uintptr* slot_ptr = XVector_at_base(&wheel->m_slots, i);
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
            /* 2026-10-22 ESP8266 崩溃定位打点：到期回调前后各打一条。
             * XPrintf 在板上走 os_printf→putc1→UART0，与 uart0_puts 同通道。 */
            XTimerData_out(&timer->m_data);

            if ((!timer->m_data.m_isSingleShot) && (timer->m_data.m_interval > 0))
            {
                // 循环定时器：复用当前节点
                size_t timeout_ticks = ceil_div(timer->m_data.m_interval, group->m_class.m_precision);
                if (timeout_ticks == 0) timeout_ticks = 1;
                timer->m_expire_ticks = current_tick + timeout_ticks;

                if (addTimer_lockfree(group, node, timeout_ticks, NULL)) {
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
            if (addTimer_lockfree(group, node, remaining_ticks, NULL)) {
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
    XAtomic_Uintptr* slot_ptr = XVector_at_base(&higher_level->m_slots, slot_index);
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
            // （传本轮：级联上下文重落槽，见 addTimer_lockfree 注记）
            node->next = NULL;
            if (!addTimer_lockfree(group, node, remaining_global_ticks, higher_level)) {
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
    XAtomic_Uintptr* slot_ptr = XVector_at_base(&wheel->m_slots, current_slot);
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

    /* [WHEELCLK 2026-10-29 诊断] 刻度源轴对账：XGUI_WHEEL_TRACE=1 时每
     * 500ms 至多一条，打印注入刻度源原始返回值（64 位拆高低 32 位用
     * %lu，绕开板上 libc %zu 错位）与轮刻度/有符号滞后 behind（负值=
     * 刻度源倒退，乱源直接证据）。behind 用 size_t 同宽回绕转有符号，
     * 与消费轴（size_t 截断）严格同轴。走 fprintf(stderr) 与 MBTRACE
     * 同流同缓冲语义（stderr 无缓冲，时间对账可信；XPrintf 走 stdout
     * 在板上重定向下是全缓冲，会成簇延迟）。handler 仅主线程消费，
     * 静态量无竞争。 */
    {
        static int s_wheelClkEn = -1;
        static uint32_t s_wheelClkLastLo = 0;
        if (s_wheelClkEn < 0)
            s_wheelClkEn = getenv("XGUI_WHEEL_TRACE") ? 1 : 0;
        if (s_wheelClkEn)
        {
            uint64_t rawMs = ((XTimerGroupBase*)group)->m_high_res_time_func();
            uint32_t rawLo = (uint32_t)rawMs;
            if ((uint32_t)(rawLo - s_wheelClkLastLo) >= 500u)
            {
                size_t curTick = (size_t)groupBase->m_current_tick;
                long behind = (long)(tick - curTick);
                s_wheelClkLastLo = rawLo;
                fprintf(stderr, "[WHEELCLK] src_hi=%lu src_lo=%lu axis=%lu cur=%lu behind=%ld\n",
                        (unsigned long)(uint32_t)(rawMs >> 32),
                        (unsigned long)rawLo,
                        (unsigned long)(uint32_t)tick,
                        (unsigned long)(uint32_t)curTick,
                        behind);
            }
        }
    }

    /* [WHEEL 2026-10-29 诊断] 刻度滞后对账：handler 进口处真实刻度与
     * 轮刻度差 >40 拍（20ms 精度即 >800ms）打点——判主循环 handler
     * 调用断流（lag 巨大）vs 落槽/兑现 bug（lag 小仍不兑现）。 */
    if (tick > groupBase->m_current_tick &&
        tick - groupBase->m_current_tick > 40)
        /* [复核修正 2026-10-29] 三个实参经 size_t-uint64_t 运算提升为
         * uint64_t，%zu 在 32 位 armel 按 32 位取参必错位——与 [WHEELCLK]
         * 的"绕开板上 %zu、%lu 直出"既有口径同规（armel/LP64 上
         * unsigned long 与 size_t 同宽，转换无截断）。 */
        XPrintf("[WHEEL] lag=%lu cur=%lu tick=%lu\n",
                (unsigned long)(tick - groupBase->m_current_tick),
                (unsigned long)groupBase->m_current_tick,
                (unsigned long)tick);

    if (tick <= groupBase->m_current_tick)
    {
        /* [单调轴护栏 2026-10-29] 原实现此处 m_current_tick = tick 回拨：
         * 轴契约是单调源（全局轮注入 CLOCK_MONOTONIC 毫秒，449bec6d 起），
         * 低读数只会来自乱源/对时——回拨会把整轮刻度拖回、已落槽定时器
         * 永不兑现（板上实证：刻度源读数在 0 与 (乱数<<32)|uptime_ms 间
         * 跳变，回拨+每次添加全宽重校准令轮彻底失速，Coarse 类定时器全
         * 靠 HrTimer 回退承载）。低读数不采纳，信任最近合法刻度。 */
        return;
    }

    /* [有界追赶 2026-10-29] 单次 handler 调用的推进封顶为时间轮全量程
     * （max_time/precision 刻度）：冷启动/长停滞后一次性追几十万拍会让
     * 主循环被 while 循环挂住（毫秒轴冷启动即 ~数百万拍）；封顶后每圈
     * 推进一段、圈圈有进展，若干圈内收敛，且到期判断用推进后的刻度，
     * 封顶窗口内的到期定时器照常兑现。 */
    {
        size_t span_ticks;
        size_t behind;
        if (groupBase->m_precision == 0)
            span_ticks = tick - groupBase->m_current_tick;
        else
            span_ticks = (size_t)(groupBase->m_max_time / groupBase->m_precision);
        if (span_ticks == 0)
            span_ticks = 1;
        behind = tick - groupBase->m_current_tick;
        if (behind > span_ticks)
            tick = groupBase->m_current_tick + span_ticks;
    }

    // 处理时间跳跃：循环推进多个滴答
    while (tick > groupBase->m_current_tick)
    {
        XTimerGroupBase_tick_base(group);
    }
}
XTimeWheelGroup* XTimeWheelGroup_create_ex(XMemoryType memory, uint16_t precision)
{
    if (precision == 0)
        return NULL;
    XTimeWheelGroup* group = XMemory_malloc(sizeof(XTimeWheelGroup), memory);
    if (group == NULL)
        return group;
    XTimeWheelGroup_init(group, precision);
    Set_Class_Memory(group, memory); Set_Class_IsHeap(group, true);
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
    XContainerSetDataDeinitMethod(&group->m_timeWheel, XClass_deinit_base);
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
/*
 * 最近到期查询（§23.4 规划 5）：把时间轮内最近的普通定时器到期并入事件
 * 分发器阻塞等待的超时计算，替代固定 20ms 心跳量化。对标 Qt 6.8 事件分发
 * 器"阻塞时长 = 最近一个定时器的剩余时间"的语义（对标/为什么：时间轮原本
 * 只被周期性 handler 消费，等待循环不感知它的最近截止，普通定时器精度被
 * 钳制在心跳节拍上）。
 * 并发安全性：只读遍历，任意线程可调用。
 *  1) 与槽头生产者共用 m_activeProducers 护栏：消费者（时间轮 handler 所在
 *     线程）的 reclaim_retired_nodes 在计数非零时推迟回收退休节点，遍历
 *     不会解引用已释放节点；
 *  2) 消费者清扫/降级先原子摘整条链再改写节点 next，并发遍历可能看到瞬时
 *     旧链或提前遇到 NULL，最坏结果是漏看个别节点（等待超时近似，下一次
 *     查询自愈），不产生悬垂访问。
 */
uint64_t XTimeWheelGroup_getNextExpireTime(XTimeWheelGroup* group)
{
    uint64_t min_ticks = UINT64_MAX;
    size_t wheel_count, i;
    if (!group) return UINT64_MAX;
    wheel_count = XContainerSize(&group->m_timeWheel);
    XAtomic_fetch_add_size_t(&group->m_activeProducers, 1, XAtomic_MemoryOrder_SeqCst);
    for (i = 0; i < wheel_count; ++i)
    {
        XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, i);
        size_t slot_count = XContainerSize(&wheel->m_slots);
        size_t j;
        for (j = 0; j < slot_count; ++j)
        {
            XAtomic_Uintptr* slot = XVector_at_base(&wheel->m_slots, j);
            XListSNode* node = (XListSNode*)XAtomic_load_uintptr_t(
                slot, XAtomic_MemoryOrder_SeqCst);
            while (node)
            {
                XTimerWheelData* timer = (XTimerWheelData*)XListSNode_DataPtr(node);
                /* 已取消但尚未被消费者清扫的节点不参与最近到期，
                 * 避免阻塞等待被幽灵定时器提前唤醒 */
                if (!XAtomic_load_bool(&timer->m_deleted, XAtomic_MemoryOrder_Acquire)
                    && timer->m_expire_ticks < min_ticks)
                    min_ticks = timer->m_expire_ticks;
                node = node->next;
            }
        }
    }
    XAtomic_fetch_sub_size_t(&group->m_activeProducers, 1, XAtomic_MemoryOrder_SeqCst);
    if (min_ticks == UINT64_MAX) return UINT64_MAX;
    /* 刻度(纪元毫秒) × 精度(ms) × 1e6 → 纳秒；全局轮精度为 1ms
     * （见 XTimeWheelGroup_global_init 的 XTimeWheelGroup_create(1)），
     * 换算不丢精度，与 XDateTime_currentNSecsSinceEpoch 同为纪元时间轴 */
    return min_ticks * group->m_class.m_precision * 1000000ULL;
}
static XTimeWheelGroup* global_XTimeWheelGroup = NULL;
/* 全局轮刻度源：XDateTime 单调毫秒。XHighResTimeFunc 契约返回 uint64_t，
 * 而 XDateTime_currentMSecsSinceEpoch 返回 int64_t——纪元毫秒在 uint64
 * 量程内恒正，包一层显式定宽转换消函数签名失配（消费侧按 size_t 截断，
 * 见 addTimerNs 刻度轴统一注记）。 */
static uint64_t monotonic_msecs_source(void)
{
    return (uint64_t)XDateTime_currentMSecsSinceEpoch();
}
static void XTimeWheelGroup_global_init()
{
    if (global_XTimeWheelGroup)return;
    global_XTimeWheelGroup = XTimeWheelGroup_create(1);
    if (global_XTimeWheelGroup == NULL)
        return;/* 分配失败：保持未初始化，等待下次 XTimeWheelGroup_global 重试 */
    XObject_moveToThread(global_XTimeWheelGroup, XThreadData_mainThread()->m_thread);
    XTimerGroupBase_setHighResTimeFunc(global_XTimeWheelGroup, monotonic_msecs_source);
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
