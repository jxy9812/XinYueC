#include"XTimerGroupWheel.h"
#include"XListSLinked.h"
#include"XMemory.h"
#include"XStack.h"
#include"XMutex.h"
#include"XThreadData.h"
#include"XCoreApplication.h"
// 单个时间轮结构
typedef struct XTimeWheel {
    XVector m_slots;					// 槽数组，每个槽是一个链表头 /XVector<XListSLinked<XTimerWheelData*>>
    size_t m_tick;						// 当前滴答计数
} XTimeWheel;
// 定时器时间轮
typedef struct XTimerWheelData
{
    XTimerData m_data;
    XAtomic_bool m_isRun;				 ///< 定时器是否正在运行
    size_t m_expire_ticks;     // 到期时间戳（毫秒）
    //XListSLinked* m_list;//加入的链表
    //XTimerGroupBase* m_group;//加入的组
} XTimerWheelData;
static XTimerWheelData* XTimerWheelData_create(XTimerData* data);
static void XTimerWheelData_delete(XTimerWheelData* data);

static size_t calculate_max_time_range(XTimerGroupWheel* group);
static void VXTimerGroupWheel_deinit(XTimerGroupWheel* group);
static XTimerData* VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerData data);
static bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerWheelData* timer);
static void VXTimerGroupBase_handler(XTimerGroupWheel* group);
static void VXTimerGroupWheel_addTimeWheel(XTimerGroupWheel* group, size_t slotsCount);
static void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group);
static void deleteData(XTimerGroupWheel* group,XTimerWheelData* data);
XTimerWheelData* XTimerWheelData_create(XTimerData* data)
{
    XTimerWheelData* wData = XCalloc(1, sizeof(XTimerWheelData));
    if (wData)
    {
        *((XTimerData*)wData) = *data;
        XAtomic_init(wData->m_isRun, true);
        wData->m_data.m_autoDelete = true;
    }
    return wData;
}

void XTimerWheelData_delete(XTimerWheelData* data)
{
    if (data)XFree(data);
}

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
        XVTABLE_INHERIT_DEFAULT(XObject_class_init());
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

    //if (&group->m_timeWheel != NULL)
    {
        while (!XVector_isEmpty_base(&group->m_timeWheel))
            XTimerGroupWheel_removeTimeWheel_base(group);
        //XVector_delete_base(&&group->m_timeWheel);
        XVector_deinit_base(&group->m_timeWheel);
        //&group->m_timeWheel = NULL;
    }

    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);

    if (group->m_mutex)
    {
        XMutex_delete(group->m_mutex);
        group->m_mutex = NULL;
    }

    // 释放父对象
    XVtableGetFunc(XIODevice_class_init(), EXClass_Deinit, void(*)(XIODevice*))(group);
}

static void add_timer_to_wheel_node(XTimeWheel* wheel, XListSNode* node, size_t ticks)
{
    size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(wheel);
    XListSNode** pvList = XVector_at_base(wheel, expire_slot);
    XListSNode* head = *pvList;
    if (head == NULL)
    {
        // 当前不存在链表
        head = node;
        *pvList = head;
        head->next = NULL;
    }
    else//直接头插
    {
        node->next = head;
        *pvList = node;
    }
    //XListSNode_Data(node, XTimerWheelData*)->m_list = pvList;
}
static void add_timer_to_wheel(XTimeWheel* wheel, XTimerWheelData* timer, size_t ticks)
{
    size_t expire_slot = (wheel->m_tick + ticks) % XContainerSize(wheel);
    XListSNode** pvList = XVector_at_base(wheel, expire_slot);
    XListSNode* head = *pvList;
    XListSNode* node= XListSNode_Create(XMalloc,XTimerWheelData*);
    XListSNode_Data(node, XTimerWheelData*) = timer;
    if (head == NULL)
    {
        // 当前不存在链表
        head = node;
        *pvList = head;
        head->next = NULL;
    }
    else//直接头插
    {
        node->next = head;
        *pvList = node;
    }
    
    //((XTimerWheelData*)timer)->m_list = pvList;
}
static bool addTimer_node(XTimerGroupWheel* group, XListSNode* node, size_t timeout_ticks)
{
    // 根据超时时间决定放入哪个时间轮
    XTimeWheel* wheel = NULL;
    size_t ticksCompare = 1, wheelSize = 1;
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i)
    {
        wheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(&group->m_timeWheel)) + i * XContainerTypeSize(&group->m_timeWheel));
        ticksCompare *= XContainerSize(wheel);
        if (timeout_ticks < ticksCompare)
        {
            add_timer_to_wheel_node(wheel, node, timeout_ticks / wheelSize - (i ? 1 : 0));
            return true;
        }
        wheelSize *= XContainerSize(wheel);
    }
    return false;
}
static bool addTimer(XTimerGroupWheel* group, XTimerWheelData* timer, size_t timeout_ticks)
{
    // 根据超时时间决定放入哪个时间轮
    XTimeWheel* wheel = NULL;
    size_t ticksCompare = 1, wheelSize = 1;
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i)
    {
        wheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(&group->m_timeWheel)) + i * XContainerTypeSize(&group->m_timeWheel));
        ticksCompare *= XContainerSize(wheel);
        if (timeout_ticks < ticksCompare)
        {
            add_timer_to_wheel(wheel, timer, timeout_ticks / wheelSize - (i ? 1 : 0));
            return true;
        }
        wheelSize *= XContainerSize(wheel);
    }
    return false;
}

XTimerData* VXTimerGroupBase_addTimer(XTimerGroupWheel* group, XTimerData data)
{
    if (data.m_timerCallback == NULL || (data.m_interval == 0)&& (data.m_timeout == 0))
        return NULL;
    if (data.m_timeout == 0)
        data.m_isSingleShot = true;
    XTimerWheelData* wData = XTimerWheelData_create(&data);
    //校准下时间
    group->m_class.m_current_tick = XTimer_getCurrentTime() / group->m_class.m_precision;
    // 计算超时时间（转换为）
    size_t timeout_ticks = data.m_timeout / group->m_class.m_precision;
    if(timeout_ticks==0)timeout_ticks= data.m_interval/ group->m_class.m_precision;//如果超时时间是0就直接使用周期时间
    ((XTimerWheelData*)wData)->m_expire_ticks = group->m_class.m_current_tick + timeout_ticks;
    //XObject_setParent(timerData, group);

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    bool result = addTimer(group, wData, timeout_ticks);
    if (result)
    {
        XAtomic_fetch_add_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Relaxed);
    }
    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);

    return wData;
}

bool VXTimerGroupBase_removeTimer(XTimerGroupWheel* group, XTimerWheelData* timerData)
{
   /* if (timerData->m_list == NULL)
        return false;*/
   
    /*if (group->m_mutex)
        XMutex_lock(group->m_mutex);*/
    XAtomic_store_bool(&timerData->m_isRun, false, XAtomic_MemoryOrder_Relaxed);
    //timerData->m_isRun = false;
    //XListSNode** pvHead = ((XTimerWheelData*)timerData)->m_list;
    //if (pvHead)
    //{
    //    XListSNode* head = *pvHead;
    //    XListSNode* prev = NULL;//前一个节点
    //    XListSNode* curNode = head;//当前节点
    //    while (curNode)
    //    {
    //        if (XListSNode_Data(curNode, XTimerWheelData*)==timerData)
    //        {//找到了
    //            if (prev)
    //                prev->next = curNode->next;//上一个节点直接链接下一个
    //            else
    //                *pvHead = curNode->next;//证明当前是头节点
    //            XMemory_free(curNode);
    //            ((XTimerWheelData*)timerData)->m_list = NULL;
    //            (timerData)->m_isRun = false;
    //            --group->m_count;
    //            break;
    //        }
    //        prev = curNode;
    //        curNode = curNode->next;
    //    }
    //}
    //if ((timerData)->m_autoDelete)
    //    deleteData(group,timerData);
    
    /*if (group->m_mutex)
        XMutex_unlock(group->m_mutex);*/

    return true;
}
// 计算时间轮的最大管理时间
size_t calculate_max_time_range(XTimerGroupWheel* group)
{
    if (XContainerSize(&group->m_timeWheel) == 0)
        return 0;

    // 获取精度（毫秒）
    uint16_t precision = ((XTimerGroupBase*)group)->m_precision;

    // 计算各级时间轮的总容量
    size_t total_slots = 1;
    for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i) {
        XTimeWheel* wheel = XVector_at_base(&group->m_timeWheel, i);
        total_slots *= XContainerSize(wheel);
    }

    // 最大时间范围 = 总槽数 * 精度
    return total_slots * precision;
}

void VXTimerGroupWheel_addTimeWheel(XTimerGroupWheel* group, size_t slotsCount)
{
    XTimeWheel wheel = { 0 };
    wheel.m_tick = 0;
    //wheel.m_slots = XVector_Create(XListSLinked*);
    /*if (wheel.m_slots == NULL)
        return;*/
    XVector_init(&wheel.m_slots,sizeof(XListSNode*));
    XVector_resize_base(&wheel.m_slots, slotsCount);

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    XVector_push_back_base(&group->m_timeWheel, &wheel);
    ((XTimerGroupBase*)group)->m_max_time=calculate_max_time_range(group);
    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);
}

void VXTimerGroupWheel_removeTimeWheel(XTimerGroupWheel* group)
{
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;

    if (group->m_mutex)
        XMutex_lock(group->m_mutex);

    XTimeWheel* wheel = XVector_back_base(&group->m_timeWheel);
    // 遍历槽数组
    for (size_t i = 0; i < XContainerSize(wheel); ++i)
    {
        XListSNode** pvHead = (XListSLinked**)(((uint8_t*)XContainerDataPtr(wheel)) + i * XContainerTypeSize(wheel));
        if (*pvHead != NULL)
        {
            XListSNode* node = *pvHead, * prev = NULL;
            while (node)
            {
                XTimerWheelData* timer = XListSNode_Data(node, XTimerWheelData*);
                prev = node;
                node = node->next;
                if (XTimerData_isAutoDelete(timer))//查看是否有内存管理权限
                    deleteData(group,timer);
                XMemory_free(prev);
            }
            //XListSLinked_delete_base(list);
        }
    }
    // 删除最后一个轮
    XVector_pop_back_base(&group->m_timeWheel);
    ((XTimerGroupBase*)group)->m_max_time = calculate_max_time_range(group);
    if (group->m_mutex)
        XMutex_unlock(group->m_mutex);
}
static void deleteCall(XVarList* argList)
{
    XVarList_args_1(argList, XTimerWheelData*, data);
    XTimerWheelData_delete(data);
}
void deleteData(XTimerGroupWheel* group,XTimerWheelData* data)
{
    XEventFunc* event = XEventFunc_create(deleteCall,XVarList_Create(XVar(XTimerWheelData*,data)),NULL);
    if (event)
        XCoreApplication_postEvent(group,event, XEVENT_PRIORITY_LOWEST);
}

static void cascade_timers(XTimerGroupWheel* group, XTimeWheel* higher_level, int slot_index, size_t higher_level_idx)
{
    if (higher_level_idx == 0) return; // 已到最底层，无需降级
    XListSNode** pvHead = XVector_at_base(&higher_level->m_slots, slot_index);
    if (pvHead==NULL||*pvHead == NULL ) return;

    XListSNode* prev, * next, * node =*pvHead;//获取头节点
    *pvHead = NULL;
    while (node)
    {
        prev = node;
        next = node->next;
        XTimerWheelData* timer = XListSNode_Data(node, XTimerWheelData*);

        // 计算剩余ticks（距离到期的滴答数）
        size_t remaining_ticks = ((XTimerWheelData*)timer)->m_expire_ticks - group->m_class.m_current_tick;
        bool isdelete = false;
        if (((XTimerWheelData*)timer)->m_expire_ticks <= ((XTimerGroupBase*)group)->m_current_tick && XAtomic_load_bool(&timer->m_isRun, XAtomic_MemoryOrder_Relaxed))
        {
            // 已到期，直接触发
            if (group->m_mutex) XMutex_unlock(group->m_mutex);
            XTimerData_out(timer);
            if (group->m_mutex) XMutex_lock(group->m_mutex);
            if ((!((XTimerData*)timer)->m_isSingleShot) && (((XTimerData*)timer)->m_interval > 0))
            {
                // 有定时间隔的重新添加
                size_t timeout_ticks = ((XTimerData*)timer)->m_interval / group->m_class.m_precision;
                ((XTimerWheelData*)timer)->m_expire_ticks = ((XTimerGroupBase*)group)->m_current_tick + timeout_ticks;
                addTimer_node(group, node, timeout_ticks);
            }
            else if ((((XTimerData*)timer)->m_isSingleShot && ((XTimerData*)timer)->m_autoDelete) ||
                (!XAtomic_load_bool(&timer->m_isRun, XAtomic_MemoryOrder_Relaxed) && ((XTimerData*)timer)->m_autoDelete))
            {
                isdelete = true;
            }
        }
        else  if (((XTimerWheelData*)timer)->m_expire_ticks <= ((XTimerGroupBase*)group)->m_current_tick && !XAtomic_load_bool(&timer->m_isRun, XAtomic_MemoryOrder_Relaxed))
        {
            isdelete = true;
        }
        else {
            // 降级到下一级轮（higher_level_idx - 1），并检查是否需要继续降级
            XTimeWheel* lower_level = XVector_at_base(&group->m_timeWheel, higher_level_idx - 1);
            size_t lower_slots = XContainerSize(&lower_level->m_slots);
            size_t lower_max_ticks = lower_slots; // 下一级轮的最大ticks

            // 若剩余ticks仍大于下一级轮的最大范围，只放入下一级轮（等待下一级轮再降级）
            // 否则直接放入匹配的低级轮
            if (remaining_ticks < lower_max_ticks) {
                // 直接放入下一级轮（已满足精度）
                add_timer_to_wheel_node(lower_level, node, remaining_ticks);
            }
            else {
                // 先放入下一级轮，等待其后续降级
                size_t relative_ticks = remaining_ticks / lower_max_ticks;
                add_timer_to_wheel_node(lower_level, node, relative_ticks);
            }
        }
        if (isdelete && ((XTimerData*)timer)->m_autoDelete)
        {
            deleteData(group,timer);
        }
        node = next;
        if (isdelete)
        {
            XMemory_free(prev);
            XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Relaxed);
        }
    }
}

void VXTimerGroupBase_handler(XTimerGroupWheel* group)
{
    if (XVector_isEmpty_base(&group->m_timeWheel))
        return;
    //XPrintf("轮询定时器中\n");
    XTimerGroupBase* groupBase = ((XTimerGroupBase*)group);
    size_t tick = XTimer_getCurrentTime() / groupBase->m_precision; // 当前滴答数
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
        XTimeWheel* wheel = XVector_front_base(&group->m_timeWheel);
        int current_slot = wheel->m_tick % XVector_size_base(wheel);
        // 处理当前槽的所有定时器
        XListSNode** pvHead = XVector_at_base(wheel, current_slot);
        //XListSLinked* list = XVector_At_Base(wheel, current_slot, XListSLinked*);
        if (*pvHead != NULL)
        {
            XListSNode* head = *pvHead;
            *pvHead = NULL;
           /* if (group->m_mutex)
                XMutex_unlock(group->m_mutex);*/
            XListSNode* prev,*next,*node=head;
            while (node)
            {
                prev = node;
                next = node->next;
                XTimerWheelData* timer = XListSNode_Data(node, XTimerWheelData*);
                bool isdelete = true;
                if (((XTimerWheelData*)timer)->m_expire_ticks <= groupBase->m_current_tick && XAtomic_load_bool(&timer->m_isRun, XAtomic_MemoryOrder_Relaxed))
                {
                    //((XTimerWheelData*)timer)->m_list = NULL;
                    if (group->m_mutex) XMutex_unlock(group->m_mutex);
                    XTimerData_out(timer);
                    if (group->m_mutex) XMutex_lock(group->m_mutex);
                    if ((!((XTimerData*)timer)->m_isSingleShot) && (((XTimerData*)timer)->m_interval > 0) && XAtomic_load_bool(&timer->m_isRun, XAtomic_MemoryOrder_Relaxed))
                    {
                        // 有定时间隔的重新添加
                        size_t timeout_ticks = ((XTimerData*)timer)->m_interval / group->m_class.m_precision;
                        ((XTimerWheelData*)timer)->m_expire_ticks = groupBase->m_current_tick + timeout_ticks;
                       /* if (group->m_mutex)
                            XMutex_lock(group->m_mutex);*/
                        addTimer_node(group, node, timeout_ticks);
                        /*if (group->m_mutex)
                            XMutex_unlock(group->m_mutex);*/
                        isdelete = false;
                    }
                }
                else if(XAtomic_load_bool(&timer->m_isRun, XAtomic_MemoryOrder_Relaxed))
                {
                    /*if (group->m_mutex)
                        XMutex_lock(group->m_mutex);*/
                    size_t timeout_ticks = ((XTimerWheelData*)timer)->m_expire_ticks- groupBase->m_current_tick ;
                    addTimer_node(group, node, timeout_ticks);
                    /*if (group->m_mutex)
                        XMutex_unlock(group->m_mutex);*/
                    isdelete = false;
                }
                if (isdelete&&((XTimerData*)timer)->m_autoDelete)
                {
                    deleteData(group,timer);
                }
                //else // 也不释放的定时器更新状态
                //{
                //    ((XTimer*)timerData)->m_isRun = false;
                //    timerData->m_list = NULL;
                //}
                node =next;
                if(isdelete)
                {
                    XMemory_free(prev);
                    XAtomic_fetch_sub_size_t(&group->m_count, 1, XAtomic_MemoryOrder_Relaxed);
                }
            }
            /*if (group->m_mutex)
                XMutex_lock(group->m_mutex);*/
        }
        // 低级轮向前推进
        ++wheel->m_tick;
        

        // 检查是否需要触发中级轮降级
        XTimeWheel* currentWheel = NULL;
        size_t ticksCompare = 1;
        for (size_t i = 0; i < XContainerSize(&group->m_timeWheel); ++i)
        {
            currentWheel = (XTimeWheel*)(((uint8_t*)XContainerDataPtr(&group->m_timeWheel)) + i * XContainerTypeSize(&group->m_timeWheel));
            ticksCompare *= XContainerSize(currentWheel);
            if (groupBase->m_current_tick % ticksCompare == 0)
            {
                if (i + 1 >= XContainerSize(&group->m_timeWheel))
                    break; // 下一个时间轮不存在
                XTimeWheel* nextWheel = ((uint8_t*)currentWheel) + XContainerTypeSize(&group->m_timeWheel);
                int next_slot = (nextWheel->m_tick % XContainerSize(nextWheel));
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

XTimerGroupWheel* XTimerGroupWheel_create(uint16_t precision)
{
    if (precision == 0)
        return NULL;
    XTimerGroupWheel* group = XMemory_malloc(sizeof(XTimerGroupWheel));
    if (group == NULL)
        return group;
    XTimerGroupWheel_init(group, precision);
    Set_Class_MemoryFree(group, XFree);
    return group;
}

void XTimerGroupWheel_init(XTimerGroupWheel* group, uint16_t precision)
{
    if (group == NULL || precision == 0)
        return;
    //初始化父类以外的数据
    memset(((XTimerGroupBase*)group) + 1, 0, sizeof(XTimerGroupWheel) - sizeof(XTimerGroupBase));
    XTimerGroupBase_init(group, precision);
    XClassGetVtable(group) = XTimerGroupWheel_class_init();
    //初始化数据
    XVector_init(&group->m_timeWheel, sizeof(XTimeWheel));
    XContainerSetDataDeinitMethod(&group->m_timeWheel, XVector_deinit_base);
    //group->m_timeWheel = XVector_Create(XTimeWheel);
    group->m_class.m_current_tick = XTimer_getCurrentTime() / group->m_class.m_precision;
    XAtomic_init(group->m_count, 0);
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
        if (mutex)
            XMutex_delete(mutex);
        return;
    }
    if (group->m_mutex)
        XMutex_delete(group->m_mutex);
    group->m_mutex = mutex;
}
size_t XTimerGroupWheel_count(XTimerGroupWheel* group)
{
    return group ? XAtomic_load_size_t(&group->m_count, XAtomic_MemoryOrder_Relaxed) : 0;
}
static XTimerGroupWheel* global_XTimerGroupWheel = NULL;
static void XTimerGroupWheel_global_init()
{
    if (global_XTimerGroupWheel)return;
    global_XTimerGroupWheel = XTimerGroupWheel_create(1);
    XObject_moveToThread(global_XTimerGroupWheel, XThreadData_mainThread()->m_thread);
    XTimerGroupWheel_addTimeWheel_base(global_XTimerGroupWheel, 20);
    XTimerGroupWheel_addTimeWheel_base(global_XTimerGroupWheel, 10);
    XTimerGroupWheel_addTimeWheel_base(global_XTimerGroupWheel, 10);
    XTimerGroupWheel_setMutex(global_XTimerGroupWheel, XMutex_create(XLock_Spin));
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
