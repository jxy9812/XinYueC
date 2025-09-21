#ifndef XSIGNALTRANSITION_H
#define XSIGNALTRANSITION_H

#include "XAbstractTransition.h"
typedef struct XStateMachine XStateMachine;
/**
 * @brief 信号触发的转换类
 */
typedef struct XSignalTransition {
    XAbstractTransition m_class;  // 继承XAbstractTransition
    XObject* m_sender;             // 信号发送者
    size_t m_signal;          // 信号
    XConnection* m_connection;     // 信号连接
} XSignalTransition;

/**
 * @brief 创建信号转换实例
 * @param m_sender 信号发送者
 * @param m_signal 信号名称
 * @return 新创建的信号转换实例，失败返回NULL
 */
XSignalTransition* XSignalTransition_create(XObject* sender, const char* signal);

/**
 * @brief 初始化信号转换
 * @param transition 信号转换实例
 * @param m_sender 信号发送者
 * @param m_signal 信号名称
 */
void XSignalTransition_init(XSignalTransition* transition, XObject* sender, const char* signal);

/**
 * @brief 销毁信号转换
 * @param transition 信号转换实例
 */
void XSignalTransition_destroy(XSignalTransition* transition);

/**
 * @brief 获取信号发送者
 * @param transition 信号转换实例
 * @return 信号发送者指针
 */
XObject* XSignalTransition_sender(const XSignalTransition* transition);

/**
 * @brief 获取信号名称
 * @param transition 信号转换实例
 * @return 信号名称
 */
const char* XSignalTransition_signal(const XSignalTransition* transition);

//链接信号
bool XSignalTransition_connect(XSignalTransition* transition, XObject* sender, size_t signal, XStateMachine* machine,XConnectionType type);

#endif // XSIGNALTRANSITION_H