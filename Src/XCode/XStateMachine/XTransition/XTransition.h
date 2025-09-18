#ifndef XTRANSITION_H
#define XTRANSITION_H

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XEvent.h"

// 前向声明
typedef struct XState XState;
typedef struct XStateMachine XStateMachine;
typedef struct XTransition XTransition;

/**
 * @brief 转换条件函数类型
 * @param transition 转换实例
 * @param machine 状态机实例
 * @param event 事件
 * @return 条件满足返回true，否则返回false
 */
typedef bool (*XTransitionCondition)(const XTransition* transition,
    XStateMachine* machine, const XEvent* event);

/**
 * @brief 转换动作函数类型
 * @param transition 转换实例
 * @param machine 状态机实例
 * @param event 事件
 */
typedef void (*XTransitionAction)(XTransition* transition,
    XStateMachine* machine, const XEvent* event);

/**
 * @brief 抽象转换类
 * 表示状态之间的转换，可由事件或信号触发
 */
typedef struct XTransition {
    XObject parent;                 // 继承XObject
    XState* source;                 // 源状态
    XState* target;                 // 目标状态
    XTransitionCondition condition; // 转换条件
    XTransitionAction action;       // 转换动作
    bool is_enabled;                // 转换是否启用
} XTransition;

/**
 * @brief 事件转换类
 * 由特定事件触发的转换
 */
typedef struct XEventTransition {
    XTransition parent;         // 继承XTransition
    XEventType event_type;      // 触发事件类型
} XEventTransition;

/**
 * @brief 信号转换类
 * 由特定信号触发的转换
 */
typedef struct XSignalTransition {
    XTransition parent;         // 继承XTransition
    XObject* sender;            // 信号发送者
    size_t signal;              // 信号标识
    XConnection* connection;    // 信号连接
} XSignalTransition;

/**
 * @brief 转换信号枚举
 */
typedef enum {
    XTransition_Signal_triggered  // 转换触发信号
} XTransitionSignal;

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * @brief 创建基础转换
     * @param source 源状态
     * @param target 目标状态
     * @return 新创建的转换，失败返回NULL
     */
    XTransition* XTransition_create(XState* source, XState* target);

    /**
     * @brief 初始化转换
     * @param transition 要初始化的转换
     * @param source 源状态
     * @param target 目标状态
     */
    void XTransition_init(XTransition* transition, XState* source, XState* target);

    /**
     * @brief 销毁转换
     * @param transition 要销毁的转换
     */
    void XTransition_destroy(XTransition* transition);

    /**
     * @brief 创建事件转换
     * @param source 源状态
     * @param target 目标状态
     * @param event_type 触发事件类型
     * @return 新创建的事件转换，失败返回NULL
     */
    XEventTransition* XEventTransition_create(XState* source, XState* target, XEventType event_type);

    /**
     * @brief 创建信号转换
     * @param source 源状态
     * @param target 目标状态
     * @param sender 信号发送者
     * @param signal 信号标识
     * @return 新创建的信号转换，失败返回NULL
     */
    XSignalTransition* XSignalTransition_create(XState* source, XState* target,
        XObject* sender, size_t signal);

    /**
     * @brief 设置转换条件
     * @param transition 转换实例
     * @param condition 条件函数
     */
    void XTransition_setCondition(XTransition* transition, XTransitionCondition condition);

    /**
     * @brief 设置转换动作
     * @param transition 转换实例
     * @param action 动作函数
     */
    void XTransition_setAction(XTransition* transition, XTransitionAction action);

    /**
     * @brief 设置转换是否启用
     * @param transition 转换实例
     * @param enabled 是否启用
     */
    void XTransition_setEnabled(XTransition* transition, bool enabled);

    /**
     * @brief 检查转换是否启用
     * @param transition 转换实例
     * @return 启用返回true，否则返回false
     */
    bool XTransition_isEnabled(const XTransition* transition);

    /**
     * @brief 获取源状态
     * @param transition 转换实例
     * @return 源状态
     */
    XState* XTransition_source(const XTransition* transition);

    /**
     * @brief 获取目标状态
     * @param transition 转换实例
     * @return 目标状态
     */
    XState* XTransition_target(const XTransition* transition);

    /**
     * @brief 检查转换是否可以触发
     * @param transition 转换实例
     * @param machine 状态机实例
     * @param event 事件
     * @return 可以触发返回true，否则返回false
     */
    bool XTransition_check(const XTransition* transition, XStateMachine* machine, const XEvent* event);

    /**
     * @brief 触发转换
     * @param transition 转换实例
     * @param machine 状态机实例
     * @param event 事件
     */
    void XTransition_trigger(XTransition* transition, XStateMachine* machine, const XEvent* event);

    /**
     * @brief 连接转换信号到槽函数
     * @param transition 转换实例
     * @param signal 信号枚举值
     * @param receiver 接收对象
     * @param slot 槽函数
     * @param type 连接类型
     * @return 连接对象，失败返回NULL
     */
    XConnection* XTransition_connect(XTransition* transition, XTransitionSignal signal,
        XObject* receiver, XSlotFunc slot, XConnectionType type);

#ifdef __cplusplus
}
#endif

#endif // XTRANSITION_H