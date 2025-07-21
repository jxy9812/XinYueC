#ifndef XMENUTEST_H
#define XMENUTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
XMenu* XMenuTest_create();
void XMenuTest_show(XMenu* menu,int column);
void XMenuTest_run();
#ifdef __cplusplus
}
#endif	
#endif