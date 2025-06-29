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
    XSetBase* m_Objects;//列表
    //XEventDispatcher* m_eventDispatcher;//事件调度器
}XCoreApplication;//
XVtable* XCoreApplication_class_init();
XCoreApplication* XCoreApplication_create(int argc, char** argv);
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);
//获取事件调度器
XEventDispatcher* XCoreApplication_getEventDispatcher();
void XCoreApplication_requestQuit();
int XCoreApplication_exec();

XSetBase* XCoreApplication_getObjects();
#ifdef __cplusplus
}
#endif
#endif