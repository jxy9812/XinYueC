#ifndef XMemoryTest_H
#define XMemoryTest_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST
	void XTestMenu_XMemoryTest(XTestMenu* root);
	void XTestMenu_XMultiPoolTest(XTestMenu* root);
	void XVariablePoolTest(void);
	void XTestMenu_XVariablePoolTest(XTestMenu* root);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
