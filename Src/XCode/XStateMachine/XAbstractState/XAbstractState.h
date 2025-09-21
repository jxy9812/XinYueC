#ifndef XABSTRACTSTATE_H
#define XABSTRACTSTATE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XClass.h"

// 前向声明
typedef struct XStateMachine XStateMachine;
typedef struct XState XState;
XCLASS_DEFINE_BEGING(XAbstractState)
XCLASS_DEFINE_ENUM(XAbstractState, OnEntered) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XAbstractState, OnExited),
XCLASS_DEFINE_ENUM(XAbstractState, SetMachine),
XCLASS_DEFINE_ENUM(XAbstractState, SetParentState),
XCLASS_DEFINE_END(XAbstractState)
/**
 * @brief 状态类型枚举
 */
typedef enum {
    XStateType_Basic,      // 基本状态
    XStateType_Final,      // 最终状态
    XStateType_History,    // 历史状态
    XStateType_Parallel    // 并行状态
} XStateType;

/**
 * @brief 抽象状态类，所有状态的基类
 */
typedef struct XAbstractState 
{
    XClass m_class;                // 继承XObject
    XStateType m_type;               // 状态类型
    XState* m_parentState;           // 父状态
    XStateMachine* m_machine;        // 所属状态机
    bool m_isRunning;                // 是否处于激活状态
    void* m_userData;                // 用户数据
    void* m_privateData;             // 私有数据(内部使用)
} XAbstractState;

/**
 * @brief 状态进入事件回调函数
 * @param state 状态实例
 * @param m_machine 所属状态机
 */
typedef void (*XStateEnteredCallback)(XAbstractState* state);

/**
 * @brief 状态退出事件回调函数
 * @param state 状态实例
 * @param m_machine 所属状态机
 */
typedef void (*XStateExitedCallback)(XAbstractState* state);
XVtable* XAbstractState_class_init();
/**
 * @brief 初始化抽象状态
 * @param state 状态实例
 * @param m_type 状态类型
 */
void XAbstractState_init(XAbstractState* state, XStateType type);

/**
 * @brief 销毁抽象状态
 * @param state 状态实例
 */
#define XAbstractState_delete_base       XClass_delete_base
#define XAbstractState_deinit_base       XClass_deinit_base

/**
 * @brief 获取状态类型
 * @param state 状态实例
 * @return 状态类型
 */
XStateType XAbstractState_type(const XAbstractState* state);

/**
 * @brief 获取父状态
 * @param state 状态实例
 * @return 父状态指针
 */
XState* XAbstractState_parentState(const XAbstractState* state);

/**
 * @brief 设置父状态
 * @param state 状态实例
 * @param m_class 父状态
 */
void XAbstractState_setParentState_base(XAbstractState* state, XAbstractState* parent);

void XAbstractState_setMachine_base(XAbstractState* state, XStateMachine* machine);
/**
 * @brief 获取所属状态机
 * @param state 状态实例
 * @return 状态机指针
 */
XStateMachine* XAbstractState_machine(const XAbstractState* state);

/**
 * @brief 检查状态是否处于激活中
 * @param state 状态实例
 * @return 激活返回true，否则返回false
 */
bool XAbstractState_isRunning(const XAbstractState* state);

/**
 * @brief 设置用户数据
 * @param state 状态实例
 * @param data 用户数据指针
 */
void XAbstractState_setUserData(XAbstractState* state, void* data);

/**
 * @brief 获取用户数据
 * @param state 状态实例
 * @return 用户数据指针
 */
void* XAbstractState_userData(const XAbstractState* state);

/**
 * @brief 状态进入事件
 * @param state 状态实例
 */
void XAbstractState_onEntered_base(XAbstractState* state);

/**
 * @brief 状态退出事件
 * @param state 状态实例
 */
void XAbstractState_onExited_base(XAbstractState* state);

/**
 * @brief 设置状态进入回调
 * @param state 状态实例
 * @param callback 回调函数
 */
void XAbstractState_setEnteredCallback(XAbstractState* state, XStateEnteredCallback callback);

/**
 * @brief 设置状态退出回调
 * @param state 状态实例
 * @param callback 回调函数
 */
void XAbstractState_setExitedCallback(XAbstractState* state, XStateExitedCallback callback);
#ifdef __cplusplus
}
#endif
#endif // XABSTRACTSTATE_H