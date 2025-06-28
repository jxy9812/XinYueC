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
    int argc;
    char** argv;
    XEventDispatcher* m_eventDispatcher;//事件调度器
}XCoreApplication;//
XVtable* XCoreApplication_class_init();
XCoreApplication* XCoreApplication_create(int argc, char** argv);
void XCoreApplication_init(XCoreApplication* app, int argc, char** argv);
//获取事件调度器
XEventDispatcher* XCoreApplication_getEventDispatcher();

int XCoreApplication_exec();
#ifdef __cplusplus
}
#endif
#endif