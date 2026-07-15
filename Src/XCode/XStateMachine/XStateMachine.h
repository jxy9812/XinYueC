#ifndef XSTATEMACHINE_H
#define XSTATEMACHINE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XObject.h"
#include "XAbstractState.h"
#include "XAbstractTransition.h"

/**
 * @brief 状态机状态枚举
 */
typedef enum {
    XStateMachineStopped,  // 已停止
    XStateMachineRunning,  // 运行中
    XStateMachinePaused    // 已暂停
} XStateMachineStatus;

// Qt 6.8: 错误类型枚举
typedef enum {
    XStateMachineNoError = 0,             // 无错误
    XStateMachineNoInitialStateError,     // 没有设置初始状态
    XStateMachineNoDefaultStateInHistoryStateError, // 历史状态没有默认状态
    XStateMachineNoCommonAncestorForTransitionError, // 转换没有公共祖先
    XStateMachineStateMachineChildModeSetToParallelError, // 尝试对并行状态机设置子模式
} XStateMachine_Error;

// Qt 6.8: 全局恢复策略
typedef enum {
    XStateMachineGlobalRestorePolicy = 0, // 全局恢复策略
    XStateMachineDoNotRestoreProperties,  // 不恢复属性
} XStateMachine_GlobalRestorePolicy;

/**
 * @brief 状态机类，管理状态和转换
 */
typedef struct XStateMachine 
{
    XObject m_class;                  // 继承XObject
    XAbstractState* m_initialState;    // 初始状态
    XAbstractState** m_activeStates;   // 当前激活的状态列表
    size_t m_activeStateCount;         // 激活状态数量
    size_t m_activeStateCapacity;      // 激活状态容量
    XStateMachineStatus m_status;      // 状态机状态
    void* m_userData;                  // 用户数据
    // Qt 6.8: 错误处理
    XStateMachine_Error m_error;       // 当前错误
    const char* m_errorString;         // 错误描述
    // Qt 6.8: 全局恢复策略
    XStateMachine_GlobalRestorePolicy m_globalRestorePolicy;
    // Qt 6.8: 动画支持
    bool m_animated;
} XStateMachine;
XVtable* XStateMachine_class_init();
/**
 * @brief 创建状态机实例
 * @return 新创建的状态机实例，失败返回NULL
 */
XStateMachine* XStateMachine_create();

/**
 * @brief 初始化状态机
 * @param machine 状态机实例
 */
void XStateMachine_init(XStateMachine* machine);

/**
 * @brief 销毁状态机
 * @param machine 状态机实例
 */
#define XStateMachine_delete_base       XObject_deleteLater
#define XStateMachine_deinit_base       XObject_deinitLater

/**
 * @brief 设置初始状态
 * @param machine 状态机实例
 * @param state 初始状态
 */
void XStateMachine_setInitialState(XStateMachine* machine, XAbstractState* state);

/**
 * @brief 获取初始状态
 * @param machine 状态机实例
 * @return 初始状态指针
 */
XAbstractState* XStateMachine_initialState(const XStateMachine* machine);

/**
 * @brief 添加状态
 * @param machine 状态机实例
 * @param state 要添加的状态
 * @return 成功返回true，失败返回false
 */
bool XStateMachine_addState(XStateMachine* machine, XAbstractState* state);

/**
 * @brief 移除状态
 * @param machine 状态机实例
 * @param state 要移除的状态
 * @return 成功返回true，失败返回false
 */
bool XStateMachine_removeState(XStateMachine* machine, XAbstractState* state);

/**
 * @brief 启动状态机
 * @param machine 状态机实例
 * @return 成功返回true，失败返回false
 */
bool XStateMachine_start(XStateMachine* machine);

/**
 * @brief 停止状态机
 * @param machine 状态机实例
 */
void XStateMachine_stop(XStateMachine* machine);

/**
 * @brief 暂停状态机
 * @param machine 状态机实例
 */
void XStateMachine_pause(XStateMachine* machine);

/**
 * @brief 恢复状态机运行
 * @param machine 状态机实例
 */
void XStateMachine_resume(XStateMachine* machine);

/**
 * @brief 获取状态机当前状态
 * @param machine 状态机实例
 * @return 状态机状态
 */
XStateMachineStatus XStateMachine_status(const XStateMachine* machine);

/**
 * @brief 事件过滤回调函数  
 * @param event 要处理的事件
 */
void XStateMachine_handleEventCB(const XEvent* event);

/**
 * @brief 执行状态转换
 * @param machine 状态机实例
 * @param source 源状态
 * @param target 目标状态
 * @return 成功返回true，失败返回false
 */
bool XStateMachine_transition(XStateMachine* machine, XAbstractState* source, XAbstractState* target);

/**
 * @brief 获取当前激活的状态数量
 * @param machine 状态机实例
 * @return 激活状态数量
 */
size_t XStateMachine_activeStateCount(const XStateMachine* machine);

/**
 * @brief 获取指定索引的激活状态
 * @param machine 状态机实例
 * @param index 索引
 * @return 激活状态指针，索引无效返回NULL
 */
XAbstractState* XStateMachine_activeState(const XStateMachine* machine, size_t index);

/**
 * @brief 检查状态机是否处于运行状态
 * @param machine 状态机实例
 * @return 运行中返回true，否则返回false
 */
bool XStateMachine_isRunning(const XStateMachine* machine);

/**
 * @brief 检查状态机是否已完成（到达最终状态）
 * @param machine 状态机实例
 * @return 已完成返回true，否则返回false
 */
bool XStateMachine_isFinished(const XStateMachine* machine);

/**
 * @brief 设置用户数据
 * @param machine 状态机实例
 * @param data 用户数据指针
 */
void XStateMachine_setUserData(XStateMachine* machine, void* data);

/**
 * @brief 获取用户数据
 * @param machine 状态机实例
 * @return 用户数据指针
 */
void* XStateMachine_userData(const XStateMachine* machine);

// Qt 6.8: 错误处理
XStateMachine_Error XStateMachine_error(const XStateMachine* machine);
const char* XStateMachine_errorString(const XStateMachine* machine);
void XStateMachine_clearError(XStateMachine* machine);
void XStateMachine_setError(XStateMachine* machine, XStateMachine_Error error, const char* errorString);

// Qt 6.8: 全局恢复策略
XStateMachine_GlobalRestorePolicy XStateMachine_globalRestorePolicy(const XStateMachine* machine);
void XStateMachine_setGlobalRestorePolicy(XStateMachine* machine, XStateMachine_GlobalRestorePolicy policy);

// Qt 6.8: 动画支持
bool XStateMachine_isAnimated(const XStateMachine* machine);
void XStateMachine_setAnimated(XStateMachine* machine, bool enabled);

/*                                                          信号                                                          */    
// 状态进入信号 (Qt 6.8: 保留用于向后兼容，新代码应使用 XState::entered_signal)
void* XStateMachine_entered_signal(XStateMachine* machine, XAbstractState* state);
// 状态退出信号 (Qt 6.8: 保留用于向后兼容，新代码应使用 XState::exited_signal)
void* XStateMachine_exited_signal(XStateMachine* machine, XAbstractState* state);

void* XStateMachine_start_signal(XStateMachine* machine);
void* XStateMachine_stop_signal(XStateMachine* machine);
void* XStateMachine_pause_signal(XStateMachine* machine);
void* XStateMachine_resume_signal(XStateMachine* machine);

// Qt 6.8: finished 信号 — 状态机到达顶层最终状态
void* XStateMachine_finished_signal(XStateMachine* machine);
// Qt 6.8: runningChanged 信号 — 运行状态变化
void* XStateMachine_runningChanged_signal(XStateMachine* machine, bool running);
// Qt 6.8: error 信号 — 发生错误
void* XStateMachine_error_signal(XStateMachine* machine, XStateMachine_Error error, const char* errorString);

#ifdef __cplusplus
}
#endif
#endif // XSTATEMACHINE_H
