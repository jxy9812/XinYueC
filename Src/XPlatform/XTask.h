/**
 * @file       XTask.h
 * @brief      跨平台任务诊断快照契约。
 * @details    本文件没有直接对应的 Qt 类型；它将 XThread 以及产品任务管理器
 *             提供的任务信息统一为只读快照。本文件不包含 Linux、Windows、
 *             FreeRTOS 或 Zephyr 头文件，也不直接枚举平台任务。各平台适配层
 *             将本平台字段映射到该契约后，由上层同步消费。任务名称仅在访问
 *             回调期间借用；栈统计为 0 表示后端不支持对应字段。注册表不拥有
 *             XThread，也不改变线程生命周期。
 */

#ifndef XTASK_H
#define XTASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 跨平台任务运行状态。
 * @details 枚举值互斥、不可按位组合；无法映射的平台状态使用
 *          XTaskState_Unknown。
 */
typedef enum XTaskState {
    XTaskState_Unknown = 0, /**< 状态未知或后端未提供。 */
    XTaskState_Ready,       /**< 就绪，等待调度。 */
    XTaskState_Running,     /**< 当前正在运行。 */
    XTaskState_Blocked,     /**< 等待同步对象或消息。 */
    XTaskState_Sleeping,    /**< 延时或睡眠。 */
    XTaskState_Suspended,   /**< 被显式挂起。 */
    XTaskState_Finished     /**< 已结束但仍在快照中。 */
} XTaskState;

/**
 * @brief 单个任务的跨平台诊断快照。
 * @details
 * 平台适配层只在枚举回调期间提供该结构体；调用方不得保存 name 指针或
 * 通过该结构体控制任务。stackSize 和 stackFree 为 0 时表示对应统计不可用。
 */
typedef struct XTaskInfo {
    const char* name;       /**< 零结尾 UTF-8 任务名；不能为 NULL，仅在当前访问回调期间借用。 */
    uint32_t id;            /**< 平台或产品分配的任务标识；0 表示后端没有可用标识。 */
    int32_t priority;       /**< 平台任务优先级；数值方向由后端定义，未知时为 0。 */
    uint32_t stackSize;     /**< 任务栈总容量，单位为字节；未知时为 0。 */
    uint32_t stackFree;     /**< 任务栈当前可用空间，单位为字节；未知时为 0。 */
    XTaskState state;       /**< 当前统一任务状态；不能识别时为 XTaskState_Unknown。 */
} XTaskInfo;

/**
 * @brief XThread 任务注册表的编译期槽位数。
 * @details 单位为线程个数；达到上限后，XTask_registerThread 对新线程返回
 *          false，已有注册项保持不变。产品可在编译参数中覆盖该宏。
 */
#ifndef XTASK_REGISTRY_CAPACITY
#define XTASK_REGISTRY_CAPACITY 16u
#endif

/**
 * @brief XThread 的不完整类型声明。
 * @details 注册表只保存借用指针，不取得线程对象所有权；对象销毁前必须先
 *          调用 XTask_unregisterThread。
 */
typedef struct XThread XThread;

/**
 * @brief 任务快照遍历回调。
 * @param userData XTask_enumerateThreads 原样传入的调用方上下文；可为 NULL，
 *                 注册表不保存也不释放。
 * @param info 当前 XThread 的只读快照；不能为 NULL，仅在本次回调期间有效，
 *             调用方不得保存该指针或其中的 name 指针。
 * @return 继续枚举返回 true；调用方取消、输出失败或不再需要快照时返回 false。
 */
typedef bool (*XTaskVisitor)(void* userData, const XTaskInfo* info);

/**
 * @brief 将一个 XThread 加入任务诊断注册表。
 * @param thread 要注册的线程对象；不能为 NULL，注册表仅借用且不会释放。
 * @return 首次注册或对象已经注册时返回 true；thread 为 NULL 或注册表已满时
 *         返回 false，失败时注册表和线程对象均保持不变。
 * @note 该函数可以与枚举、注销并发调用；对象销毁仍必须由调用方在注销完成后进行。
 */
bool XTask_registerThread(XThread* thread);

/**
 * @brief 从任务诊断注册表移除一个 XThread。
 * @param thread 要注销的线程对象；可为 NULL，注册表不释放该对象。
 * @return 无。thread 为 NULL 或尚未注册时不执行任何操作。
 * @warning 调用返回后才可销毁线程对象；不得让其他代码在销毁后再次注册同一指针。
 */
void XTask_unregisterThread(XThread* thread);

/**
 * @brief 同步枚举所有已注册 XThread 的只读快照。
 * @param visitor 逐项访问回调；不能为 NULL，函数不会保存该回调。
 * @param userData 原样传给 visitor 的调用方上下文；可为 NULL，函数不取得所有权。
 * @return visitor 有效且所有快照均完成访问时返回 true，包括注册表为空；visitor
 *         为 NULL 或任一次 visitor 返回 false 时返回 false，并立即停止枚举。
 * @note 函数先复制固定容量快照，再在注册表锁外调用 visitor；不会访问
 *       Linux、Windows 或 RTOS 的原生任务列表，也不会分配堆内存。
 */
bool XTask_enumerateThreads(XTaskVisitor visitor, void* userData);

#ifdef __cplusplus
}
#endif

#endif /* XTASK_H */
