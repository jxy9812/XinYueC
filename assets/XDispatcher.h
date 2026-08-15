#ifndef XDISPATCHER_H
#define XDISPATCHER_H

#include "XClass.h"
/**
 * @brief 调度策略枚举
 * 定义调度器支持的任务调度算法
 */
typedef enum {
    X_SCHED_FIFO,    // 实时先进先出：高优先级任务一旦运行，直到完成或阻塞才让出CPU（不被同优先级任务抢占）
    X_SCHED_RR,      // 实时时间片轮转：同优先级任务按固定时间片轮流执行（时间片用完则切换）
    X_SCHED_CFS      // 完全公平调度：按任务优先级比例分配CPU时间，通过虚拟运行时间（vruntime）保证公平性
} XSchedulePolicy;

/**
 * @brief 调度实体状态枚举
 * 描述调度实体（如线程、任务）的运行状态
 */
typedef enum {
    X_ENTITY_RUNNING,  // 正在运行：当前占用CPU
    X_ENTITY_READY,    // 就绪：已准备好运行，等待CPU调度
    X_ENTITY_BLOCKED,  // 阻塞：因等待资源（如锁、I/O）暂停运行
    X_ENTITY_STOPPED   // 停止：已被移除调度队列，不再参与调度
} XEntityState;

/**
 * @brief 调度实体基类
 * 封装被调度对象的基本信息，是调度器的最小调度单元
 */
typedef struct XScheduleEntity {
    XClass m_base;                // 基类（继承XClass，支持面向对象特性）
    int priority;                 // 优先级：值越小优先级越高（实时策略中决定调度顺序）
    XEntityState state;           // 状态：当前实体的运行状态（运行/就绪/阻塞/停止）
    XSchedulePolicy policy;       // 调度策略：实体绑定的调度算法（FIFO/RR/CFS）
    uint64_t vruntime;            // 虚拟运行时间（CFS策略专用）：用于衡量任务"应该"运行的时间，决定调度优先级
    uint64_t runtime;             // 实际运行时间：任务累计占用CPU的实际时间
    uint64_t slice;               // 时间片大小（RR策略专用）：每次分配的CPU时间上限
    void (*run)(void*);           // 实体执行函数
    void* data;                   // 执行函数参数
} XScheduleEntity;

/**
 * @brief 调度器基类虚函数表定义
 * 声明调度器的核心操作接口，通过虚函数实现多态
 */
XCLASS_DEFINE_BEGING(XDispatcher)
EXDispatcher_AddEntity = XCLASS_VTABLE_GET_SIZE(XClass),  // 添加调度实体到就绪队列
EXDispatcher_RemoveEntity,                                // 从就绪队列移除调度实体
EXDispatcher_Schedule,                                    // 执行一次调度（选择下一个运行的实体）
EXDispatcher_Yield,                                       // 当前实体主动让出CPU
EXDispatcher_SetPolicy,                                   // 设置调度器的全局调度策略
EXDispatcher_GetNextEntity,                               // 获取下一个应运行的实体（根据策略选择）
EXDispatcher_WakeUp,                                      // 唤醒阻塞的实体（将其状态改为就绪并加入队列）
EXDispatcher_Block,                                       // 阻塞当前实体（将其状态改为阻塞并移出队列）
XCLASS_DEFINE_END(XDispatcher)

/**
 * @brief 调度器基类
 * 管理调度实体，时间相关字段单位均为毫秒
 */
typedef struct XDispatcher {
    XClass m_class;                  // 父类（继承XClass）
    XSchedulePolicy policy;           // 全局调度策略
    union {
        XQueueBase* fifo_queue;       // FIFO/RR就绪队列（队列结构，保证顺序）
        XSet* cfs_tree;               // CFS就绪队列（红黑树，按vruntime排序）
    } ready_entities;                 // 按策略复用不同数据结构
    XScheduleEntity* current_entity;  // 当前运行实体
    XMutex* mutex;                    // 线程安全锁
    uint64_t last_sched_time;         // 上次调度时间（毫秒，用于计算运行时长）
    // CFS相关参数
    uint64_t min_vruntime;            // 最小虚拟运行时间（毫秒）
    int nr_running;                   // 就绪+运行实体总数
} XDispatcher;

/**
 * @brief 初始化XDispatcher类的虚函数表
 * @return 初始化后的虚函数表指针（XVtable*），供所有XDispatcher实例共享
 */
XVtable* XDispatcher_class_init();

/**
 * @brief 创建调度器实例
 * @param policy 调度器的初始调度策略（X_SCHED_FIFO/X_SCHED_RR/X_SCHED_CFS）
 * @return 新创建的调度器指针（XDispatcher*），失败时返回NULL
 */
XDispatcher* XDispatcher_create_ex(XMemoryType memory,  XSchedulePolicy policy);

/**
 * @brief 初始化调度器实例（与create配合使用，完成成员变量初始化）
 * @param dispatcher 待初始化的调度器指针（不可为NULL）
 * @param policy 调度器的初始调度策略
 */
void XDispatcher_init(XDispatcher* dispatcher, XSchedulePolicy policy);

/**
 * @brief 创建调度实体实例
 * @param data 关联的数据指针（如线程对象、任务数据等）
 * @param priority 实体的优先级（值越小优先级越高）
 * @param policy 实体绑定的调度策略
 * @return 新创建的调度实体指针（XScheduleEntity*），失败时返回NULL
 */
XScheduleEntity* XScheduleEntity_create(void* data, int priority, XSchedulePolicy policy);

/**
 * @brief 初始化调度实体实例（与create配合使用，完成成员变量初始化）
 * @param entity 待初始化的调度实体指针（不可为NULL）
 * @param data 关联的数据指针
 * @param priority 实体的优先级
 * @param policy 实体绑定的调度策略
 */
void XScheduleEntity_init(XScheduleEntity* entity, void* data, int priority, XSchedulePolicy policy);

/**
 * @brief 向调度器添加调度实体（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @param entity 待添加的调度实体（不可为NULL，状态将被设为就绪）
 * @return 成功添加返回true，失败（如实体已存在）返回false
 */
bool XDispatcher_addEntity(XDispatcher* dispatcher, XScheduleEntity* entity);

/**
 * @brief 从调度器移除调度实体（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @param entity 待移除的调度实体（不可为NULL）
 * @return 成功移除返回true，失败（如实体不存在）返回false
 */
bool XDispatcher_removeEntity(XDispatcher* dispatcher, XScheduleEntity* entity);

/**
 * @brief 执行一次调度（选择下一个运行的实体并切换）
 * @param dispatcher 调度器指针（不可为NULL）
 * @note 会更新当前实体的运行时间，根据策略调整实体状态，并切换到下一个实体
 */
void XDispatcher_schedule(XDispatcher* dispatcher);

/**
 * @brief 当前实体主动让出CPU（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @note 将当前运行实体重新加入就绪队列，立即触发调度
 */
void XDispatcher_yield(XDispatcher* dispatcher);

/**
 * @brief 设置调度器的全局调度策略（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @param policy 新的调度策略（X_SCHED_FIFO/X_SCHED_RR/X_SCHED_CFS）
 */
void XDispatcher_setPolicy(XDispatcher* dispatcher, XSchedulePolicy policy);

/**
 * @brief 获取下一个应运行的实体（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @return 下一个要运行的实体指针（XScheduleEntity*），若无就绪实体返回NULL
 * @note 选择逻辑由调度器当前策略决定（如FIFO取第一个，CFS取vruntime最小的）
 */
XScheduleEntity* XDispatcher_getNextEntity(XDispatcher* dispatcher);

/**
 * @brief 唤醒阻塞的实体（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @param entity 待唤醒的实体（不可为NULL，状态需为阻塞）
 * @note 将实体状态改为就绪并加入就绪队列，使其可参与调度
 */
void XDispatcher_wakeUp(XDispatcher* dispatcher, XScheduleEntity* entity);

/**
 * @brief 阻塞指定实体（虚函数包装器）
 * @param dispatcher 调度器指针（不可为NULL）
 * @param entity 待阻塞的实体（不可为NULL，通常为当前运行实体）
 * @note 将实体状态改为阻塞并移出就绪队列，暂停其调度
 */
void XDispatcher_block(XDispatcher* dispatcher, XScheduleEntity* entity);

/**
 * @brief 销毁调度器实例（宏定义，封装基类销毁逻辑）
 * @param dispatcher 待销毁的调度器指针
 * @note 会释放内部资源（就绪队列、互斥锁等），但不负责销毁关联的调度实体
 */
#define XDispatcher_delete(dispatcher) XClass_delete_base((XClass*)dispatcher)

 /**
  * @brief 销毁调度实体实例（宏定义，封装基类销毁逻辑）
  * @param entity 待销毁的调度实体指针
  */
#define XScheduleEntity_delete(entity) XClass_delete_base((XClass*)entity)


/* XClass create API default-memory wrappers. */
#undef XDispatcher_create
#define XDispatcher_create(...) XDispatcher_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif