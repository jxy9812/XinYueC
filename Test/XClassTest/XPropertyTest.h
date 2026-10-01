#ifndef XPROPERTYTEST_H
#define XPROPERTYTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#if DEMOTEST && XPROPERTY_ON
	void XTestMenu_XPropertyTest(XTestMenu* root);
	void XPropertyTestAll(void);
#endif /* DEMOTEST && XPROPERTY_ON */

#ifdef __cplusplus
}
#endif
#endif
