#ifndef XHISTORYSTATE_H
#define XHISTORYSTATE_H

#include "XAbstractState.h"

/**
 * @brief 历史状态类型枚举
 */
typedef enum {
    XHistoryStateType_Shallow,  // 浅层历史：仅恢复直接子状态
    XHistoryStateType_Deep      // 深层历史：恢复所有层级的子状态
} XHistoryStateType;

/**
 * @brief 历史状态类，用于保存父状态的历史激活状态
 */
typedef struct XHistoryState {
    XAbstractState parent;          // 继承XAbstractState
    XHistoryStateType historyType;  // 历史状态类型
    XAbstractState* defaultState;   // 默认状态
    XAbstractState* storedState;    // 存储的历史状态
} XHistoryState;

/**
 * @brief 创建历史状态实例
 * @param type 历史状态类型
 * @return 新创建的历史状态实例，失败返回NULL
 */
XHistoryState* XHistoryState_create(XHistoryStateType type);

/**
 * @brief 初始化历史状态
 * @param state 历史状态实例
 * @param type 历史状态类型
 */
void XHistoryState_init(XHistoryState* state, XHistoryStateType type);

/**
 * @brief 销毁历史状态
 * @param state 历史状态实例
 */
void XHistoryState_destroy(XHistoryState* state);

/**
 * @brief 获取历史状态类型
 * @param state 历史状态实例
 * @return 历史状态类型
 */
XHistoryStateType XHistoryState_historyType(const XHistoryState* state);

/**
 * @brief 设置默认状态
 * @param state 历史状态实例
 * @param defaultState 默认状态
 */
void XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState);

/**
 * @brief 获取默认状态
 * @param state 历史状态实例
 * @return 默认状态指针
 */
XAbstractState* XHistoryState_defaultState(const XHistoryState* state);

/**
 * @brief 存储当前激活状态
 * @param state 历史状态实例
 * @param storedState 要存储的状态
 */
void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState);

/**
 * @brief 激活历史状态
 * @param state 历史状态实例
 * @param machine 所属状态机
 */
void XHistoryState_activate(XHistoryState* state, XStateMachine* machine);

#endif // XHISTORYSTATE_H