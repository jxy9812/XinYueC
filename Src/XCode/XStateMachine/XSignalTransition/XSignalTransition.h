#ifndef XSIGNALTRANSITION_H
#define XSIGNALTRANSITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractTransition.h"

XCLASS_DEFINE_BEGING(XSignalTransition)
XCLASS_DEFINE_EXTEND_END(XSignalTransition, XAbstractTransition);

/** @brief 由 XObject 信号触发的转换，对应 Qt 6.8 的 QSignalTransition。 */
typedef struct XSignalTransition {
    XAbstractTransition m_class; ///< 继承 XAbstractTransition。
    const XObject* m_senderObject; ///< 信号发送对象，不取得所有权。
    size_t m_signal;               ///< 信号标识。
    XConnection* m_connection;     ///< 活动期间引用的共享内部连接。
    XStateMachine* m_registeredMachine; ///< 当前注册连接所属的状态机。
} XSignalTransition;

/**
 * @brief 初始化 XSignalTransition 虚函数表。
 * @return XSignalTransition 的共享虚函数表。
 */
XVtable* XSignalTransition_class_init(void);
/**
 * @brief 创建未绑定发送者和源状态的信号转换。
 * @return 新信号转换；内存分配失败时返回 NULL。
 */
/**
 * @brief 创建指定发送者、信号和源状态的信号转换。
 * @param sender 信号发送对象，不转移所有权，可为 NULL。
 * @param signal 由 XSignal 取得的信号标识，0 表示尚未绑定。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 * @return 新信号转换；内存分配失败时返回 NULL。
 */
XSignalTransition* XSignalTransition_create_ex(XMemoryType memory, const XObject* sender, size_t signal, XState* sourceState);
/**
 * @brief 初始化空信号转换。
 * @param transition 调用者提供的未初始化存储。
 */
void XSignalTransition_init(XSignalTransition* transition);
/**
 * @brief 初始化指定发送者、信号和源状态的信号转换。
 * @param transition 调用者提供的未初始化存储。
 * @param sender 信号发送对象，不转移所有权，可为 NULL。
 * @param signal 信号标识，0 表示尚未绑定。
 * @param sourceState 源状态，可为 NULL；非 NULL 时取得转换所有权。
 */
void XSignalTransition_init_ex(XSignalTransition* transition, const XObject* sender, size_t signal, XState* sourceState);

#define XSignalTransition_delete_base XAbstractTransition_delete_base
#define XSignalTransition_deinit_base XAbstractTransition_deinit_base

/**
 * @brief 获取信号发送对象。
 * @param transition 信号转换。
 * @return 当前发送对象；未绑定或 transition 为 NULL 时返回 NULL。
 */
const XObject* XSignalTransition_senderObject(const XSignalTransition* transition);
/**
 * @brief 设置信号发送对象，状态位于 configuration 时自动重新注册连接。
 * @param transition 信号转换。
 * @param sender 新发送对象，不转移所有权；NULL 表示取消绑定。
 */
void XSignalTransition_setSenderObject(XSignalTransition* transition, const XObject* sender);
/**
 * @brief 获取信号标识。
 * @param transition 信号转换。
 * @return 当前信号标识；未绑定或 transition 为 NULL 时返回 0。
 */
size_t XSignalTransition_signal(const XSignalTransition* transition);
/**
 * @brief 设置信号标识，状态位于 configuration 时自动重新注册连接。
 * @param transition 信号转换。
 * @param signal 新信号标识；0 表示取消绑定。
 */
void XSignalTransition_setSignal(XSignalTransition* transition, size_t signal);

/**
 * @brief 信号发送对象变化信号。
 * @param transition senderObject 已发生变化的转换。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XSignalTransition_senderObjectChanged_signal(XSignalTransition* transition);
/**
 * @brief 信号标识变化信号。
 * @param transition signal 已发生变化的转换。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XSignalTransition_signalChanged_signal(XSignalTransition* transition);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XSignalTransition_create
#define XSignalTransition_create() \
	XSignalTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0, NULL)

#endif // XSIGNALTRANSITION_H
