#include"CXinYueConfig.h"
#if !defined(XSIGNALSLOTMANAGER_H)
#define XSIGNALSLOTMANAGER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include"XEventType.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
// 前置声明
typedef struct XSignal XSignal;
typedef struct XConnection XConnection;
typedef (*XSlotFunc)(XObject* sender,XObject* receiver, void* args);
/**
 * @brief 信号发送模式枚举
 * 定义信号从发送到执行槽函数的不同处理方式
 */
typedef enum 
{
    XEVENT_SEND_INVALID,  //无效模式
    XEVENT_SEND_DIRECT,   // 直接发送模式：在发送线程中立即执行槽函数（同步调用）
    XEVENT_SEND_QUEUED    // 队列发送模式：将信号放入接收者的消息队列，由接收者线程异步执行槽函数
} XEventSendMode;
typedef enum XConnectionType
{
    XConnectionType_Auto = 0,
    XConnectionType_Direct = 1,
    XConnectionType_Queued = 2,
    XConnectionType_BlockingQueued = 3,
    XConnectionType_Unique = 0x80,
    XConnectionType_SingleShot = 0x100,
}XConnectionType;
//槽
typedef struct XConnection
{
    XConnectionType type;
    XSignal* signal;
    XObject* receiver;          // 接收者对象（槽函数所属的对象）
    XSlotFunc slot_func;        //槽函数
} XConnection;

//信号
typedef struct XSignal
{
    XObject* sender;//信号发送者对象
    size_t type;//信号类型(可以是函数也可以是枚举)
    XListBase* connList;//连接列表  <XConnection>
} XSignal;
//信号与槽管理器
typedef struct XSignalSlot
{
    XObject* obj;//管理者/发送者
    XEventSendMode sendMode;//发送模式
    XMap* signalMap;//信号列表
    XListBase* bindSignalList;//接收对象列表 绑定的其他对象信号
    XMutex* mutex;            //用于同步的互斥锁
}XSignalSlot;

XSignalSlot* XSignalSlot_create(XObject* obj);
void XSignalSlot_init(XSignalSlot* manager, XObject* obj);
void XSignalSlot_deinit(XSignalSlot* manager);
void XSignalSlot_delete(XSignalSlot* manager);

/**
 * @brief 为信号设置发送模式
 * @param manager 信号槽管理器实例（不能为空）
 * @param mode 要设置的发送模式（XSignalSendMode枚举值）
 * @return true表示设置成功，false表示失败（如manager为空、signal无效等）
 */
bool XSignalSlot_setSendMode(XSignalSlot* manager,XEventSendMode mode);
XEventSendMode XSignalSlot_getSendMode(XSignalSlot* manager);
/**
 * @brief 连接信号与槽
 * @param m_signal 信号指针
 * @param receiver 槽函数所属的接收者对象
 * @param slot_func 槽函数
 * @return 连接对象指针（可用于后续断开连接）
 */
XConnection* XSignalSlot_connect(XSignalSlot* manager,size_t signal, XObject* receiver, XSlotFunc slot_func, XConnectionType type);
bool XSignalSlot_disconnect(XSignalSlot* manager, size_t signal, XObject* receiver, XSlotFunc slot_func);
/**
 * @brief 断开信号与槽的连接
 * @param conn 要断开的连接（由signal_connect返回）
 */
bool XSignalSlot_disconnect_conn(XConnection* conn);

/**
 * @brief 触发信号，通知所有关联的槽函数
 * @param m_signal 信号指针
 * @param args 传递给槽函数的参数（通过void*传递任意类型）
 */
void XSignalSlot_emit(XSignalSlot* manager, size_t signal,void* args, XEventPriority priority);

/**
 * @brief 触发信号，通知所有关联的槽函数
 * @param m_signal 信号指针
 * @param args 传递给槽函数的参数（通过const XVariant*传递任意类型）调用所有槽后自动释放 只读不能修改
 */
void XSignalSlot_emit_variant(XSignalSlot* manager, size_t signal,XVariant* args, XEventPriority priority);

#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H