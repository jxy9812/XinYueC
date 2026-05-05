#include "XDispatcher.h"
#include "XQueue.h"
#include "XSet.h"
#include "XMutex.h"
#include "XMemory.h"
#include "XTimer.h"
// 比较函数（用于红黑树排序）
// 实体比较函数（先按策略，再按优先级，最后按虚拟运行时间）
static int XScheduleEntity_compare(const void* a, const void* b) 
{
    const XScheduleEntity* e1 = *(const XScheduleEntity**)a;
    const XScheduleEntity* e2 = *(const XScheduleEntity**)b;

    // 优先比较调度策略（实时策略优先于CFS）
    if (e1->policy != e2->policy) {
        return (e1->policy == X_SCHED_CFS) ? 1 : -1;
    }

    // 同策略下比较优先级（值越小优先级越高）
    if (e1->priority != e2->priority) {
        return e1->priority - e2->priority;
    }

    // CFS策略补充比较虚拟运行时间
    if (e1->policy == X_SCHED_CFS) {
        if (e1->vruntime < e2->vruntime) return -1;
        if (e1->vruntime > e2->vruntime) return 1;
    }

    return 0; // 完全相同
}
static bool ScheduleEquality(const void* a, const void* b)
{
    return XScheduleEntity_compare(a,b)==0;
}
static bool ScheduleLess(const void* a, const void* b)
{
    return XScheduleEntity_compare(a, b)<0;
}
// 虚函数实现
static bool VXDispatcher_addEntity(XDispatcher* dispatcher, XScheduleEntity* entity);
static bool VXDispatcher_removeEntity(XDispatcher* dispatcher, XScheduleEntity* entity);
static void VXDispatcher_schedule(XDispatcher* dispatcher);
static void VXDispatcher_yield(XDispatcher* dispatcher);
static void VXDispatcher_setPolicy(XDispatcher* dispatcher, XSchedulePolicy policy);
static XScheduleEntity* VXDispatcher_getNextEntity(XDispatcher* dispatcher);
static void VXDispatcher_wakeUp(XDispatcher* dispatcher, XScheduleEntity* entity);
static void VXDispatcher_block(XDispatcher* dispatcher, XScheduleEntity* entity);
static void VXDispatcher_deinit(XDispatcher* dispatcher);

// 初始化虚函数表
XVtable* XDispatcher_class_init() {
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XDispatcher))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XClass);

    void* table[] = {
        VXDispatcher_addEntity,
        VXDispatcher_removeEntity,
        VXDispatcher_schedule,
        VXDispatcher_yield,
        VXDispatcher_setPolicy,
        VXDispatcher_getNextEntity,
        VXDispatcher_wakeUp,
        VXDispatcher_block
    };

    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDispatcher_deinit);
    return XVTABLE_DEFAULT;
}

// 创建调度器
XDispatcher* XDispatcher_create(XSchedulePolicy policy) {
    XDispatcher* dispatcher = XMalloc_System(sizeof(XDispatcher));
    if (dispatcher) {
        XDispatcher_init(dispatcher, policy);
        Set_Class_MemoryFree(dispatcher, XFree_System);
    }
    return dispatcher;
}

// 初始化调度器
void XDispatcher_init(XDispatcher* dispatcher, XSchedulePolicy policy)
{
    if (!dispatcher) return;

    // 初始化基类
    XClass_init(&dispatcher->m_class);
    XClassGetVtable(dispatcher) = XDispatcher_class_init();

    // 初始化成员变量
    dispatcher->policy = policy;
    dispatcher->current_entity = NULL;
    dispatcher->mutex = XMutex_create(XLock_NonRecursive);
    dispatcher->last_sched_time = XTimer_getCurrentTime(); // 初始化为当前毫秒时间
    dispatcher->min_vruntime = 0;
    dispatcher->nr_running = 0;

    // 根据策略初始化就绪队列
    if (policy == X_SCHED_FIFO || policy == X_SCHED_RR) {
        dispatcher->ready_entities.fifo_queue = XQueue_create(sizeof(XScheduleEntity*));
        //((XListBase*)dispatcher->ready_entities.fifo_queue)->m_equality = ScheduleEquality;
        XContainerSetCompare(dispatcher->ready_entities.fifo_queue, XScheduleEntity_compare);
    }
    else {
        dispatcher->ready_entities.cfs_tree = XSet_create(sizeof(XScheduleEntity*),
            XScheduleEntity_compare);
    }
}

// 创建调度实体
XScheduleEntity* XScheduleEntity_create(void* data, int priority, XSchedulePolicy policy) {
    XScheduleEntity* entity = XMalloc_System(sizeof(XScheduleEntity));
    if (entity) {
        XScheduleEntity_init(entity, data, priority, policy);
    }
    return entity;
}

// 初始化调度实体
void XScheduleEntity_init(XScheduleEntity* entity, void* data, int priority, XSchedulePolicy policy) {
    if (!entity) return;

    XClass_init(&entity->m_base);
    // 初始化成员变量
    entity->data = data;
    entity->priority = (priority < 0) ? 0 : priority; // 确保优先级非负
    entity->policy = policy;
    entity->state = X_ENTITY_READY;
    entity->vruntime = 0;
    entity->runtime = 0;
    // RR策略默认时间片为100ms（与XTimer_getCurrentTime单位一致）
    entity->slice = (policy == X_SCHED_RR) ? 100 : 0;
    entity->run = NULL; // 初始化为空，由用户设置
}

// 添加调度实体
static bool VXDispatcher_addEntity(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity || entity->state != X_ENTITY_READY) {
        return false;
    }

    XMutex_lock(dispatcher->mutex);

    // 按策略加入对应队列
    if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
        XQueueBase_push_base(dispatcher->ready_entities.fifo_queue, &entity);
    }
    else {
        // CFS实体初始化vruntime为全局最小虚拟时间
        entity->vruntime = dispatcher->min_vruntime;
        XSet_insert_base(dispatcher->ready_entities.cfs_tree, &entity);
    }

    entity->state = X_ENTITY_READY;
    dispatcher->nr_running++;

    XMutex_unlock(dispatcher->mutex);
    return true;
}

// 移除调度实体
static bool VXDispatcher_removeEntity(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity) {
        return false;
    }

    XMutex_lock(dispatcher->mutex);
    bool ret = false;

    // 按策略从对应队列移除
    if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
        ret = XVtableGetFunc(XListSLinked_class_init(), EXListBase_Remove, bool(*)(XListBase*, void*))(dispatcher->ready_entities.fifo_queue, &entity);
        //ret = XQueueBase_remove(dispatcher->ready_entities.fifo_queue, &entity);
    }
    else {
        ret = XSet_remove_base(dispatcher->ready_entities.cfs_tree, &entity);
    }

    if (ret) {
        entity->state = X_ENTITY_STOPPED;
        dispatcher->nr_running--;
    }

    XMutex_unlock(dispatcher->mutex);
    return ret;
}

// 获取下一个要运行的实体
static XScheduleEntity* VXDispatcher_getNextEntity(XDispatcher* dispatcher) {
    if (!dispatcher || dispatcher->nr_running == 0) {
        return NULL;
    }

    XMutex_lock(dispatcher->mutex);
    XScheduleEntity* next = NULL;

    switch (dispatcher->policy) {
    case X_SCHED_FIFO:
    case X_SCHED_RR:
        // FIFO/RR取队列头部
        next = *(XScheduleEntity**)XQueueBase_top_base(dispatcher->ready_entities.fifo_queue);
        break;
    case X_SCHED_CFS:
    {
        XSet_iterator it = XSet_begin(dispatcher->ready_entities.cfs_tree);
        // CFS取vruntime最小的实体（红黑树最小值）
        next = *(XScheduleEntity**)XSet_iterator_data(&it);;
    }
        break;
    }

    XMutex_unlock(dispatcher->mutex);
    return next;
}
// CFS策略专用：更新虚拟运行时间
static void XDispatcher_updateCFSVruntime(XDispatcher* dispatcher, XScheduleEntity* entity, uint64_t run_duration) {
    if (!dispatcher || !entity || entity->policy != X_SCHED_CFS) {
        return;
    }
    // 权重计算：优先级越高（值越小），权重越大（100为基准）
    uint32_t weight = 100 - entity->priority;
    if (weight == 0) weight = 1; // 避免除零

    // 虚拟时间 = 实际运行时间 * 基准权重 / 实体权重（确保高优先级增长慢）
    entity->vruntime += (run_duration * 100) / weight;

    // 更新全局最小虚拟时间
    if (entity->vruntime > dispatcher->min_vruntime) {
        dispatcher->min_vruntime = entity->vruntime;
    }
}

// 执行调度
static void VXDispatcher_schedule(XDispatcher* dispatcher) {
    if (!dispatcher) return;

    XMutex_lock(dispatcher->mutex);
    uint64_t current_time = XTimer_getCurrentTime(); // 当前时间（毫秒）
    XScheduleEntity* prev = dispatcher->current_entity;
    XScheduleEntity* next = VXDispatcher_getNextEntity(dispatcher);

    // 处理上一个运行的实体
    if (prev && prev->state == X_ENTITY_RUNNING) {
        uint64_t run_duration = current_time - dispatcher->last_sched_time; // 运行时长（毫秒）
        prev->runtime += run_duration;

        // 按策略更新状态
        if (prev->policy == X_SCHED_RR) {
            // RR策略：时间片耗尽则放回队列
            if (prev->runtime >= prev->slice) {
                prev->state = X_ENTITY_READY;
                XQueueBase_push_base(dispatcher->ready_entities.fifo_queue, &prev);
                prev->runtime = 0; // 重置时间片计数
            }
        }
        else if (prev->policy == X_SCHED_CFS) {
            // CFS策略：更新虚拟时间并放回红黑树
            XDispatcher_updateCFSVruntime(dispatcher, prev, run_duration);
            prev->state = X_ENTITY_READY;
            XSet_insert_base(dispatcher->ready_entities.cfs_tree, &prev);
        }
        // FIFO策略：运行至完成，不主动放回队列
    }

    // 切换到下一个实体
    if (next) {
        // 从就绪队列移除
        if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
            XQueueBase_pop_base(dispatcher->ready_entities.fifo_queue);
        }
        else {
            XSet_remove_base(dispatcher->ready_entities.cfs_tree, &next);
        }

        // 更新状态和调度时间
        next->state = X_ENTITY_RUNNING;
        dispatcher->current_entity = next;
        dispatcher->last_sched_time = current_time;

        // 执行实体的运行函数
        if (next->run) {
            next->run(next->data);
        }
    }
    else {
        // 无就绪实体，清空当前实体
        dispatcher->current_entity = NULL;
    }

    XMutex_unlock(dispatcher->mutex);
}

// 让出CPU
static void VXDispatcher_yield(XDispatcher* dispatcher) {
    if (!dispatcher || !dispatcher->current_entity) {
        return;
    }

    XMutex_lock(dispatcher->mutex);
    XScheduleEntity* current = dispatcher->current_entity;

    // 将当前实体放回就绪队列
    current->state = X_ENTITY_READY;
    if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
        XQueueBase_push_base(dispatcher->ready_entities.fifo_queue, &current);
    }
    else {
        XSet_insert_base(dispatcher->ready_entities.cfs_tree, &current);
    }

    // 清空当前实体，触发重新调度
    dispatcher->current_entity = NULL;
    XMutex_unlock(dispatcher->mutex);

    // 立即执行调度
    VXDispatcher_schedule(dispatcher);
}

// 设置调度策略
static void VXDispatcher_setPolicy(XDispatcher* dispatcher, XSchedulePolicy policy) {
    if (!dispatcher || dispatcher->policy == policy) {
        return;
    }

    XMutex_lock(dispatcher->mutex);
    // 切换策略时需重建就绪队列（简化处理，实际可优化为迁移实体）
    if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
        XQueueBase_delete_base(dispatcher->ready_entities.fifo_queue);
    }
    else {
        XSet_delete_base(dispatcher->ready_entities.cfs_tree);
    }

    // 初始化新策略的就绪队列
    dispatcher->policy = policy;
    if (policy == X_SCHED_FIFO || policy == X_SCHED_RR) {
        dispatcher->ready_entities.fifo_queue = XQueue_create(sizeof(XScheduleEntity*));
        //((XListBase*)dispatcher->ready_entities.fifo_queue)->m_equality = ScheduleEquality;
        XContainerSetCompare(dispatcher->ready_entities.fifo_queue, XScheduleEntity_compare);
    }
    else {
        dispatcher->ready_entities.cfs_tree = XSet_create(sizeof(XScheduleEntity*),
            XScheduleEntity_compare);
    }
    XMutex_unlock(dispatcher->mutex);
}

// 唤醒实体
static void VXDispatcher_wakeUp(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity || entity->state != X_ENTITY_BLOCKED) {
        return;
    }

    XMutex_lock(dispatcher->mutex);
    entity->state = X_ENTITY_READY;
    // 加入就绪队列
    if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
        XQueueBase_push_base(dispatcher->ready_entities.fifo_queue, &entity);
    }
    else {
        XSet_insert_base(dispatcher->ready_entities.cfs_tree, &entity);
    }
    dispatcher->nr_running++;
    XMutex_unlock(dispatcher->mutex);
}

// 阻塞实体
static void VXDispatcher_block(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity || (entity->state != X_ENTITY_RUNNING && entity->state != X_ENTITY_READY)) {
        return;
    }

    XMutex_lock(dispatcher->mutex);
    // 从就绪队列移除
    if (entity->state == X_ENTITY_READY) {
        if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
           XVtableGetFunc(XListSLinked_class_init(), EXListBase_Remove, bool(*)(XListBase*, void*))(dispatcher->ready_entities.fifo_queue, &entity);
            //XQueueBase_remove(dispatcher->ready_entities.fifo_queue, entity);
        }
        else {
            XSet_remove_base(dispatcher->ready_entities.cfs_tree, &entity);
        }
    }
    else {
        // 如果是当前运行实体，清空当前实体
        dispatcher->current_entity = NULL;
    }

    entity->state = X_ENTITY_BLOCKED;
    dispatcher->nr_running--;
    XMutex_unlock(dispatcher->mutex);
}

// 销毁调度器
static void VXDispatcher_deinit(XDispatcher* dispatcher) {
    if (!dispatcher) return;

    // 释放就绪队列
    if (dispatcher->policy == X_SCHED_FIFO || dispatcher->policy == X_SCHED_RR) {
        XQueueBase_delete_base(dispatcher->ready_entities.fifo_queue);
    }
    else {
        XSet_delete_base(dispatcher->ready_entities.cfs_tree);
    }

    // 释放互斥锁
    XMutex_delete(dispatcher->mutex);
}

// 虚函数包装器实现
bool XDispatcher_addEntity(XDispatcher* dispatcher, XScheduleEntity* entity) {
    return XClassGetVirtualFunc(dispatcher, EXDispatcher_AddEntity,
        bool(*)(XDispatcher*, XScheduleEntity*))(dispatcher, entity);
}

bool XDispatcher_removeEntity(XDispatcher* dispatcher, XScheduleEntity* entity) {
    return XClassGetVirtualFunc(dispatcher, EXDispatcher_RemoveEntity,
        bool(*)(XDispatcher*, XScheduleEntity*))(dispatcher, entity);
}

void XDispatcher_schedule(XDispatcher* dispatcher) {
    XClassGetVirtualFunc(dispatcher, EXDispatcher_Schedule,
        void(*)(XDispatcher*))(dispatcher);
}

void XDispatcher_yield(XDispatcher* dispatcher) {
    XClassGetVirtualFunc(dispatcher, EXDispatcher_Yield,
        void(*)(XDispatcher*))(dispatcher);
}

void XDispatcher_setPolicy(XDispatcher* dispatcher, XSchedulePolicy policy) {
    XClassGetVirtualFunc(dispatcher, EXDispatcher_SetPolicy,
        void(*)(XDispatcher*, XSchedulePolicy))(dispatcher, policy);
}

XScheduleEntity* XDispatcher_getNextEntity(XDispatcher* dispatcher) {
    return XClassGetVirtualFunc(dispatcher, EXDispatcher_GetNextEntity,
        XScheduleEntity * (*)(XDispatcher*))(dispatcher);
}

void XDispatcher_wakeUp(XDispatcher* dispatcher, XScheduleEntity* entity) {
    XClassGetVirtualFunc(dispatcher, EXDispatcher_WakeUp,
        void(*)(XDispatcher*, XScheduleEntity*))(dispatcher, entity);
}

void XDispatcher_block(XDispatcher* dispatcher, XScheduleEntity* entity) {
    XClassGetVirtualFunc(dispatcher, EXDispatcher_Block,
        void(*)(XDispatcher*, XScheduleEntity*))(dispatcher, entity);
}