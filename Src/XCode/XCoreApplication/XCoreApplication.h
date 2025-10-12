#ifndef XCOREAPPLICATION_H
#define XCOREAPPLICATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
#include"XEventType.h"
#include"XEventLoop.h"
//
typedef struct XCoreApplication
{
    XObject m_class;//父对象
    bool m_quit;//是否退出
    int m_argc;
    char** m_argv;
    //XCircularQueueAtomic* m_postQueue;//信号发送队列
    //XTimerGroupWheel* m_timerGroup;   // 定时器组
    XEventLoop* m_eventLoop;//事件调度器
}XCoreApplication;//
XVtable* XCoreApplication_class_init();
XCoreApplication* XCoreApplication_global();
XCoreApplication* XCoreApplication_create(int argc, char** argv);
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);
//获取事件调度器
XEventDispatcher* XCoreApplication_getDispatcher();
XEventLoop* XCoreApplication_getEventLoop();
XTimerGroupBase* XCoreApplication_getTimerGroup();
//请求退出
void XCoreApplication_quit();
void  XCoreApplication_processEvents(XEventLoopProcessEventsFlags flags);
//进入事件循环
int XCoreApplication_exec();
/**
 * @brief 投递信号发送（异步处理）
 * @param sendSignalFunc 信号发送函数
 * @param signalSlot 信号槽
 * @param m_signal 信号
 * @param args 参数
 * @return 是否成功加入队列
 */
bool XCoreApplication_postSendSignal(void(*sendFunc)(XSignalSlot*, size_t, void*), XSignalSlot* signalSlot, size_t signal, void* args, XAtomic_int32_t* ref_count, XEventPriority priority);
//投递函数(异步投递)
bool XCoreApplication_postFunc(XObject* receiver, void(*func)(void*), void* args, XEventPriority priority);

/*                                    信号                                        */
void* XCoreApplication_aboutToQuit_signal(XCoreApplication* app);
#ifdef __cplusplus
}
#endif
#endif