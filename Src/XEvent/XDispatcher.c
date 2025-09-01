#include "XDispatcher.h"
#include "XSet.h"
#include "XMutex.h"
#include "XMemory.h"
#include "XTimerBase.h"
// 比较函数（用于红黑树排序）
// 实体比较函数（先按策略，再按优先级，最后按虚拟运行时间）
static int XScheduleEntity_compare(const void* a, const void* b) 
{
    const XScheduleEntity* e1 = (const XScheduleEntity*)a;
    const XScheduleEntity* e2 = (const XScheduleEntity*)b;

    // 实时任务优先于CFS任务
    if (e1->policy != e2->policy) {
        return (e1->policy == X_SCHED_CFS) ? 1 : -1;
    }

    // 同策略下比较优先级
    if (e1->priority != e2->priority) {
        return e1->priority - e2->priority;
    }

    // 相同优先级比较虚拟运行时间（CFS）
    if (e1->policy == X_SCHED_CFS) {
        if (e1->vruntime < e2->vruntime) return -1;
        if (e1->vruntime > e2->vruntime) return 1;
    }

    return 0;
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
        XVTABLE_INHERIT_DEFAULT(XClass_class_init());

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
    XDispatcher* dispatcher = XMemory_malloc(sizeof(XDispatcher));
    if (dispatcher) {
        XDispatcher_init(dispatcher, policy);
    }
    return dispatcher;
}

// 初始化调度器
void XDispatcher_init(XDispatcher* dispatcher, XSchedulePolicy policy) {
    if (!dispatcher) return;

    XClass_init(&dispatcher->m_parent);
    XClassGetVtable(dispatcher) = XDispatcher_class_init();

    dispatcher->policy = policy;
    dispatcher->ready_entities = XSet_create(sizeof(XScheduleEntity*),
        ScheduleEquality, ScheduleLess);
    dispatcher->current_entity = NULL;
    dispatcher->mutex = XMutex_create();
    dispatcher->clock = 0;
    dispatcher->min_vruntime = 0;
    dispatcher->nr_running = 0;
}

// 创建调度实体
XScheduleEntity* XScheduleEntity_create(void* data, int priority, XSchedulePolicy policy) {
    XScheduleEntity* entity = XMemory_malloc(sizeof(XScheduleEntity));
    if (entity) {
        XScheduleEntity_init(entity, data, priority, policy);
    }
    return entity;
}

// 初始化调度实体
void XScheduleEntity_init(XScheduleEntity* entity, void* data, int priority, XSchedulePolicy policy) {
    if (!entity) return;

    XClass_init(&entity->m_base);
    entity->data = data;
    entity->priority = priority;
    entity->policy = policy;
    entity->state = X_ENTITY_READY;
    entity->vruntime = 0;
    entity->runtime = 0;
    entity->slice = (policy == X_SCHED_RR) ? 100000 : 0; // 100ms时间片
}

// 添加调度实体
static bool VXDispatcher_addEntity(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity) return false;

    XMutex_lock(dispatcher->mutex);
    entity->state = X_ENTITY_READY;

    // 对于CFS，初始化虚拟运行时间
    if (entity->policy == X_SCHED_CFS) {
        entity->vruntime = dispatcher->min_vruntime;
    }

    bool ret = XSet_insert_base(dispatcher->ready_entities, &entity);
    if (ret) {
        dispatcher->nr_running++;
    }
    XMutex_unlock(dispatcher->mutex);
    return ret;
}

// 移除调度实体
static bool VXDispatcher_removeEntity(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity) return false;

    XMutex_lock(dispatcher->mutex);
    bool ret = XSet_remove_base(dispatcher->ready_entities, &entity);
    if (ret) {
        dispatcher->nr_running--;
        entity->state = X_ENTITY_STOPPED;
    }
    XMutex_unlock(dispatcher->mutex);
    return ret;
}

// 获取下一个要运行的实体
static XScheduleEntity* VXDispatcher_getNextEntity(XDispatcher* dispatcher) {
    if (!dispatcher || XSet_isEmpty_base(dispatcher->ready_entities)) {
        return NULL;
    }
    XSet_iterator it = XSet_begin(dispatcher->ready_entities);
    // 不同策略的调度逻辑
    switch (dispatcher->policy) {
    case X_SCHED_FIFO:
    case X_SCHED_RR:
        // 实时调度：取优先级最高的第一个实体
        return *(XScheduleEntity**)XSet_iterator_data(&it);

    case X_SCHED_CFS:
        // 公平调度：取虚拟运行时间最小的实体
        return *(XScheduleEntity**)XSet_iterator_data(&it);
    }
    return NULL;
}

// 执行调度
static void VXDispatcher_schedule(XDispatcher* dispatcher) {
    if (!dispatcher) return;

    XMutex_lock(dispatcher->mutex);
    XScheduleEntity* prev = dispatcher->current_entity;
    XScheduleEntity* next = VXDispatcher_getNextEntity(dispatcher);

    if (next) {
        // 更新当前实体状态
        if (prev) {
            prev->state = X_ENTITY_READY;
            // 更新运行时间
            uint64_t now = XTimerBase_getCurrentTime();
            prev->runtime += now - dispatcher->clock;

            // CFS更新虚拟运行时间
            if (prev->policy == X_SCHED_CFS) {
                // 简化的虚拟时间计算：实际时间 * 权重（这里用优先级作为权重）
                prev->vruntime += (now - dispatcher->clock) * (100 - prev->priority);
                dispatcher->min_vruntime = prev->vruntime;
            }

            // RR策略：时间片用完则移到队尾
            if (prev->policy == X_SCHED_RR &&
                prev->runtime >= prev->slice) {
                XSet_remove_base(dispatcher->ready_entities, &prev);
                prev->runtime = 0;
                XSet_insert_base(dispatcher->ready_entities, &prev);
            }
        }

        // 切换到下一个实体
        XSet_remove_base(dispatcher->ready_entities, &next);
        next->state = X_ENTITY_RUNNING;
        dispatcher->current_entity = next;
        dispatcher->clock = XTimerBase_getCurrentTime();
    }
    XMutex_unlock(dispatcher->mutex);
}

// 让出CPU
static void VXDispatcher_yield(XDispatcher* dispatcher) {
    if (!dispatcher || !dispatcher->current_entity) return;

    XMutex_lock(dispatcher->mutex);
    XScheduleEntity* current = dispatcher->current_entity;

    // 将当前实体重新加入就绪队列
    current->state = X_ENTITY_READY;
    XSet_insert_base(dispatcher->ready_entities, &current);

    // 立即调度
    dispatcher->current_entity = NULL;
    XMutex_unlock(dispatcher->mutex);
    VXDispatcher_schedule(dispatcher);
}

// 设置调度策略
static void VXDispatcher_setPolicy(XDispatcher* dispatcher, XSchedulePolicy policy) {
    if (dispatcher) {
        XMutex_lock(dispatcher->mutex);
        dispatcher->policy = policy;
        XMutex_unlock(dispatcher->mutex);
    }
}

// 唤醒实体
static void VXDispatcher_wakeUp(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity) return;

    XMutex_lock(dispatcher->mutex);
    if (entity->state == X_ENTITY_BLOCKED) {
        entity->state = X_ENTITY_READY;
        XSet_insert_base(dispatcher->ready_entities, &entity);
        dispatcher->nr_running++;
    }
    XMutex_unlock(dispatcher->mutex);
}

// 阻塞实体
static void VXDispatcher_block(XDispatcher* dispatcher, XScheduleEntity* entity) {
    if (!dispatcher || !entity) return;

    XMutex_lock(dispatcher->mutex);
    if (entity->state == X_ENTITY_RUNNING) {
        dispatcher->current_entity = NULL;
    }
    entity->state = X_ENTITY_BLOCKED;
    XSet_remove_base(dispatcher->ready_entities, &entity);
    dispatcher->nr_running--;
    XMutex_unlock(dispatcher->mutex);
}

// 销毁调度器
static void VXDispatcher_deinit(XDispatcher* dispatcher) {
    if (!dispatcher) return;

    XSet_clear_base(dispatcher->ready_entities);
    XSet_delete_base(dispatcher->ready_entities);
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