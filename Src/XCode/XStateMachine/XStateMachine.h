#ifndef XSTATEMACHINE_H
#define XSTATEMACHINE_H

#include "../XObject.h"
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

/**
 * @brief 状态机类，管理状态和转换
 */
typedef struct XStateMachine {
    XObject parent;                  // 继承XObject
    XAbstractState* initialState;    // 初始状态
    XAbstractState** activeStates;   // 当前激活的状态列表
    size_t activeStateCount;         // 激活状态数量
    size_t activeStateCapacity;      // 激活状态容量
    XStateMachineStatus status;      // 状态机状态
    void* userData;                  // 用户数据
} XStateMachine;

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
void XStateMachine_destroy(XStateMachine* machine);

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
 * @brief 处理事件
 * @param machine 状态机实例
 * @param event 要处理的事件
 * @return 事件被处理返回true，否则返回false
 */
bool XStateMachine_handleEvent(XStateMachine* machine, const XEvent* event);

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

#endif // XSTATEMACHINE_H