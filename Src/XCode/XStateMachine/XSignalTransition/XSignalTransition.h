#ifndef XSIGNALTRANSITION_H
#define XSIGNALTRANSITION_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XAbstractTransition.h"
typedef struct XStateMachine XStateMachine;

XCLASS_DEFINE_BEGING(XSignalTransition)
XCLASS_DEFINE_EXTEND_END(XSignalTransition, XAbstractTransition);
//XCLASS_DEFINE_ENUM(XState, NULL) = XCLASS_VTABLE_GET_SIZE(XAbstractState),
//XCLASS_DEFINE_END(XState)
/**
 * @brief 信号触发的转换类
 */
typedef struct XSignalTransition {
    XAbstractTransition m_class;  // 继承XAbstractTransition
    XObject* m_sender;             // 信号发送者
    size_t m_signal;               // 信号
    void* m_signalArgs;            //来自信号的参数
    XConnection* m_connection;     // 信号连接
} XSignalTransition;
XVtable* XSignalTransition_class_init();
XSignalTransition* XSignalTransition_create();
/**
 * @brief 创建信号转换实例
 * @param sender 信号发送者
 * @param signal 信号名称
 * @return 新创建的信号转换实例，失败返回NULL
 */
XSignalTransition* XSignalTransition_create_signal(XObject* sender, size_t signal);

/**
 * @brief 初始化信号转换
 * @param transition 信号转换实例
 * @param m_sender 信号发送者
 * @param m_signal 信号名称
 */
void XSignalTransition_init(XSignalTransition* transition);

/**
 * @brief 销毁信号转换
 * @param transition 信号转换实例
 */
#define XSignalTransition_delete_base       XAbstractTransition_delete_base
#define XSignalTransition_deinit_base       XAbstractTransition_deinit_base

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
bool XSignalTransition_connect(XSignalTransition* transition, XObject* sender, size_t signal,XConnectionType type);
#ifdef __cplusplus
}
#endif
#endif // XSIGNALTRANSITION_H