#include"CXinYueConfig.h"
#if !defined(XSIGNALSLOTMANAGER_H)
#define XSIGNALSLOTMANAGER_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include"XEventType.h"
#include"XVarList.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
// 前置声明
typedef struct XSignal XSignal;
typedef struct XConnection XConnection;
typedef void (*XSlotFunc)(XObject* receiver, XVarList* args);
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
    XVector* connList;//连接列表  <XConnection>
} XSignal;
//信号与槽管理器
typedef struct XSignalSlot
{
    XObject* obj;//管理者/发送者
    //XEventSendMode sendMode;//发送模式
    XMap* signalMap;//信号列表 <size_t,XSignal>
    XVector* bindSignalList;//接收对象列表 绑定的其他对象信号
    XMutex* mutex;            //用于同步的互斥锁
}XSignalSlot;

XSignalSlot* XSignalSlot_create(XObject* obj);
void XSignalSlot_init(XSignalSlot* manager, XObject* obj);
void XSignalSlot_deinit(XSignalSlot* manager);
void XSignalSlot_delete(XSignalSlot* manager);
// 检查指定的信号是否有任何连接。
bool XSignalSlot_isSignalConnected(const XSignalSlot* manager, size_t signal);

// 返回连接到此对象上某个信号的接收者数量。
int XSignalSlot_receivers(const XSignalSlot* manager, size_t signal);
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
 * @param argList 传递给槽函数的参数（通过void* args传递任意类型）调用所有槽后自动释放 建议只读不能修改
 * @param del  args的释放规则,可传入NULL
 * @param ref_count 优化参数,传入NULL,内部自己创建管理，外部传入可以与事件共享参数，而不用拷贝，谁最后谁释放参数
 * @param priority  信号与槽队列连接的时候的优先级(内部走的是事件投递)
 */
void XSignalSlot_emit(XSignalSlot* manager, size_t signal, XVarList* args, void(*del)(XVarList*), XAtomic_int32_t* ref_count, int priority);
void XSignalSlot_emit_queue(XSignalSlot* manager, size_t signal, void* args, void(*del)(void*), XAtomic_int32_t* ref_count, XEventPriority priority);

#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H