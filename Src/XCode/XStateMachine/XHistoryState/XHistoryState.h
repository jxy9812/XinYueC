#ifndef XHISTORYSTATE_H
#define XHISTORYSTATE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XAbstractState.h"

XCLASS_DEFINE_BEGING(XHistoryState)
XCLASS_DEFINE_EXTEND_END(XHistoryState, XAbstractState);

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
typedef struct XHistoryState
{
    XAbstractState m_class;          // 继承基类
    XHistoryStateType m_historyType;  // 历史类型
    XAbstractState* m_defaultState;   // 默认恢复状态（无历史时使用）
    XAbstractState* m_storedShallow;  // 浅层历史存储的直接子状态
    XVector* m_storedDeep;           // 深层历史存储的状态链（XAbstractState*列表）
} XHistoryState;

XVtable* XHistoryState_class_init();

/**
 * @brief 创建历史状态实例
 * @param type 历史状态类型（浅层/深层）
 * @return 新创建的历史状态实例，失败返回NULL
 */
XHistoryState* XHistoryState_create(XHistoryStateType type);

/**
 * @brief 初始化历史状态
 * @param state 历史状态实例
 * @param type 历史状态类型（浅层/深层）
 */
void XHistoryState_init(XHistoryState* state, XHistoryStateType type);

/**
 * @brief 销毁历史状态
 */
#define XHistoryState_delete_base       XAbstractState_delete_base
#define XHistoryState_deinit_base       XAbstractState_deinit_base

 /**
  * @brief 获取历史状态类型
  */
XHistoryStateType XHistoryState_historyType(const XHistoryState* state);

/**
 * @brief 设置默认状态（无历史记录时恢复此状态）
 * @param defaultState 必须是父状态的子状态
 */
void XHistoryState_setDefaultState(XHistoryState* state, XAbstractState* defaultState);

/**
 * @brief 获取默认状态
 */
XAbstractState* XHistoryState_defaultState(const XHistoryState* state);

/**
 * @brief 手动存储指定状态作为历史记录
 * @param storedState 必须是父状态的子状态
 */
void XHistoryState_storeState(XHistoryState* state, XAbstractState* storedState);

/**
 * @brief 自动存储当前父状态下的活跃子状态作为历史记录
 */
void XHistoryState_storeCurrentState(XHistoryState* state);

/**
 * @brief 激活历史状态（计算并存储实际要恢复的目标状态）
 */
#define  XHistoryState_activate_base       XAbstractState_activate_base

/**
 * @brief 获取激活后实际要恢复的目标状态
 */
XAbstractState* XHistoryState_getActivatedTarget(XHistoryState* state);

/**
 * @brief 获取深层历史存储的完整状态链
 * @return 状态链向量（XAbstractState*列表）
 */
XVector* XHistoryState_storedDeep(const XHistoryState* state);

#ifdef __cplusplus
}
#endif
#endif // XHISTORYSTATE_H
