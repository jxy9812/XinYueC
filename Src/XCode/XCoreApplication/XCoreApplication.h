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
    XClass m_parent;//父对象
    bool m_quit;//是否退出
    int m_argc;
    char** m_argv;
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
#ifdef __cplusplus
}
#endif
#endif