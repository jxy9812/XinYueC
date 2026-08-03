#ifndef XMemoryTest_H
#define XMemoryTest_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XMenu_XMemoryTest(XMenu* root);
	void XMenu_XMultiPoolTest(XMenu* root);
	void XVariablePoolTest(void);
	void XMenu_XVariablePoolTest(XMenu* root);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
