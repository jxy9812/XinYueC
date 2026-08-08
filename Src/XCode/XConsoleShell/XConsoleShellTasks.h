/**
 * @file       XConsoleShellTasks.h
 * @brief      XConsoleShell `tasks` 命令和任务快照提供者接口。
 * @details    本模块没有直接对应的 Qt 类型。XinYueC 核心通过
 *             XPlatform/XTask.h 维护 XThread 注册表，并以统一快照输出。产品
 *             可通过同步提供者回调覆盖或补充 FreeRTOS、Zephyr 原生任务；
 *             Shell 只负责校验和格式化输出，不包含 RTOS 头文件、不访问平台
 *             API，也不分配堆内存。回调及其 userData 由产品拥有，Shell 只在
 *             调用期间借用。
 */

#ifndef XCONSOLE_SHELL_TASKS_H
#define XCONSOLE_SHELL_TASKS_H

#include "XConsoleShellCommand.h"
#include "XPlatform/XTask.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_TASKS_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XConsoleShell 不完整类型；任务提供者仅在回调期间借用该对象。 */
typedef struct XConsoleShell XConsoleShell;

/** @brief XConsoleShellSession 不完整类型；任务提供者不得保存或释放会话对象。 */
typedef struct XConsoleShellSession XConsoleShellSession;

/** @brief Shell 任务状态兼容类型；枚举值和语义完全复用 XTaskState。 */
typedef XTaskState XConsoleShellTaskState;

/**
 * @brief Shell 单任务快照兼容类型。
 * @details 成员、单位和借用期限完全复用 XTaskInfo；Shell 不拥有其中的 name。
 */
typedef XTaskInfo XConsoleShellTaskInfo;

/** @brief 状态未知或后端无法映射；兼容 XTaskState_Unknown。 */
#define XConsoleShellTaskState_Unknown XTaskState_Unknown
/** @brief 任务就绪并等待调度；兼容 XTaskState_Ready。 */
#define XConsoleShellTaskState_Ready XTaskState_Ready
/** @brief 任务当前正在运行；兼容 XTaskState_Running。 */
#define XConsoleShellTaskState_Running XTaskState_Running
/** @brief 任务等待同步对象或消息；兼容 XTaskState_Blocked。 */
#define XConsoleShellTaskState_Blocked XTaskState_Blocked
/** @brief 任务处于延时或睡眠状态；兼容 XTaskState_Sleeping。 */
#define XConsoleShellTaskState_Sleeping XTaskState_Sleeping
/** @brief 任务已被显式挂起；兼容 XTaskState_Suspended。 */
#define XConsoleShellTaskState_Suspended XTaskState_Suspended
/** @brief 任务已结束但仍保留于快照；兼容 XTaskState_Finished。 */
#define XConsoleShellTaskState_Finished XTaskState_Finished

/**
 * @brief 输出一条任务快照的回调。
 * @param userData 提供者收到的 emitUserData；可为 NULL，Shell 不保存也不释放。
 * @param info 当前任务的只读快照；不能为 NULL，仅在本次调用期间有效；回调
 *             不得保存该指针或其中的 name 指针。
 * @return 继续枚举返回 `XConsoleResult_Ok`；输出失败、取消或达到限制时返回
 *         对应错误码，提供者应立即停止枚举并原样返回。
 */
typedef XConsoleResult (*XConsoleShellTaskEmitFn)(
    void* userData, const XConsoleShellTaskInfo* info);

/**
 * @brief 产品任务快照提供者。
 * @param userData 注册提供者时传入的产品上下文；可为 NULL。Shell 保存该借用
 *                 指针直至提供者被替换或清除，但不复制或释放其指向的资源。
 * @param shell 当前 Shell；不能为 NULL，仅在调用期间借用，不得保存或释放。
 * @param session 当前会话；不能为 NULL，仅在调用期间借用，不得保存或释放。
 * @param emit Shell 提供的逐条输出回调；不能为 NULL，只能同步调用且不得保存。
 * @param emitUserData 原样传给 emit 的输出上下文；可为 NULL，不得保存或释放。
 * @return 完成枚举返回 XConsoleResult_Ok；不支持返回 XConsoleResult_NotSupported；
 *         取消、参数无效或输出失败返回对应的 XConsoleResult。emit 返回错误后，
 *         提供者必须停止枚举并原样返回该错误。
 */
typedef XConsoleResult (*XConsoleShellTaskProviderFn)(
    void* userData, XConsoleShell* shell, XConsoleShellSession* session,
    XConsoleShellTaskEmitFn emit, void* emitUserData);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_TASKS_ON */
#endif /* XCONSOLE_SHELL_TASKS_H */
