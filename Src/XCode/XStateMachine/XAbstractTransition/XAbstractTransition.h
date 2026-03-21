#ifndef XABSTRACTTRANSITION_H
#define XABSTRACTTRANSITION_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XObject.h"
#include "XAbstractState.h"

// 前向声明
typedef struct XEventMin XEventMin;
typedef struct XStateMachine XStateMachine;
typedef struct XAbstractTransition XAbstractTransition;
XCLASS_DEFINE_BEGING(XAbstractTransition)
XCLASS_DEFINE_EXTEND_END(XAbstractTransition, XObject);
//XCLASS_DEFINE_ENUM(XAbstractTransition, OnEntered) = XCLASS_VTABLE_GET_SIZE(XClass),
//XCLASS_DEFINE_END(XAbstractTransition)
/**
 * @brief 转换类型枚举，标识不同的转换子类
 */
typedef enum {
    XAbstractTransitionType,    // 抽象转换基类
    XSignalTransitionType,      // 信号转换类型
    XEventTransitionType        // 事件转换类型
} XTransitionType;

/**
 * @brief 转换触发条件函数
 * @param transition 转换实例
 * @param event 事件
 * @return 满足条件返回true，否则返回false
 */
typedef bool (*XAbstractTransitionCondition)(const XAbstractTransition* transition);

/**
 * @brief 抽象转换类，所有转换的基类
 */
typedef struct XAbstractTransition {
    XObject m_class;                 // 继承XObject
    XTransitionType m_type;          // 转换类型
    XAbstractState* m_sourceState;   // 源状态
    XAbstractState* m_targetState;   // 目标状态
    XAbstractTransitionCondition m_condition; // 转换条件
    void* m_userData;                // 用户数据
} XAbstractTransition;
XVtable* XAbstractTransition_class_init();
/**
 * @brief 初始化抽象转换
 * @param transition 转换实例
 * @param m_type 转换类型
 */
void XAbstractTransition_init(XAbstractTransition* transition, XTransitionType type);

/**
 * @brief 销毁抽象转换
 * @param transition 转换实例
 */
#define XAbstractTransition_delete_base       XClass_delete_base
#define XAbstractTransition_deinit_base       XClass_deinit_base

/**
 * @brief 获取源状态
 * @param transition 转换实例
 * @return 源状态指针
 */
XAbstractState* XAbstractTransition_sourceState(const XAbstractTransition* transition);

/**
 * @brief 设置源状态
 * @param transition 转换实例
 * @param state 源状态
 */
void XAbstractTransition_setSourceState(XAbstractTransition* transition, XAbstractState* state);

/**
 * @brief 获取目标状态
 * @param transition 转换实例
 * @return 目标状态指针
 */
XAbstractState* XAbstractTransition_targetState(const XAbstractTransition* transition);

/**
 * @brief 设置目标状态
 * @param transition 转换实例
 * @param state 目标状态
 */
void XAbstractTransition_setTargetState(XAbstractTransition* transition, XAbstractState* state);

/**
 * @brief 设置转换条件
 * @param transition 转换实例
 * @param m_condition 条件函数
 */
void XAbstractTransition_setCondition(XAbstractTransition* transition, XAbstractTransitionCondition condition);

/**
 * @brief 检查转换条件是否满足
 * @param transition 转换实例
 * @return 满足返回true，否则返回false
 */
bool XAbstractTransition_checkCondition(const XAbstractTransition* transition);

/**
 * @brief 执行转换
 * @param transition 转换实例
 * @param m_machine 状态机
 * @return 成功返回true，否则返回false
 */
bool XAbstractTransition_execute(XAbstractTransition* transition, XStateMachine* machine);

/**
 * @brief 设置用户数据
 * @param transition 转换实例
 * @param data 用户数据指针
 */
void XAbstractTransition_setUserData(XAbstractTransition* transition, void* data);

/**
 * @brief 获取用户数据
 * @param transition 转换实例
 * @return 用户数据指针
 */
void* XAbstractTransition_userData(const XAbstractTransition* transition);
#ifdef __cplusplus
}
#endif
#endif // XABSTRACTTRANSITION_H
