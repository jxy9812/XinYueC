#ifndef XCOREAPPLICATION_H
#define XCOREAPPLICATION_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
//
typedef struct XCoreApplication
{
    XClass m_class;//父对象
    bool m_quit;//是否退出
    int m_argc;
    char** m_argv;
    XCircularQueueAtomic* m_sendSignalQueue;//信号发送队列
    XTimerGroupWheel* m_timerGroup;   // 定时器组
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
void XCoreApplication_requestQuit();
//进入事件循环
int XCoreApplication_exec();
/**
 * @brief 投递信号发送（异步处理）
 * @param loop 事件循环调度器
 * @param sendFunc 信号发送函数
 * @param signalSlot 信号槽
 * @param signal 信号
 * @param args 参数
 * @return 是否成功加入队列
 */
bool XCoreApplication_postSendSignal(void(*sendFunc)(XSignalSlot*, size_t, void*), XSignalSlot* signalSlot, size_t signal, void* args);
#ifdef __cplusplus
}
#endif
#endif