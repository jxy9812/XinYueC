#ifndef XALGORITHMTEST_H
#define XALGORITHMTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST
	void XTestMenu_XLibraryTest(XTestMenu* root);
	//查询算法
	void XTestMenu_FindTest(XTestMenu* root);
	void XTestMenu_XBinarySearchTest(XTestMenu* root);
	//树
	void XTestMenu_XTreeTest(XTestMenu* root);
	void XTestMenu_XBinaryTreeTest(XTestMenu* root);
	void XTestMenu_XBalancedBinaryTreeTest(XTestMenu* root);
	void XTestMenu_XRedBlackTreeTest(XTestMenu* root);
	//CJson
	void XTestMenu_CJsonTest(XTestMenu* root);
	//Base64
	void XTestMenu_XBase64Test(XTestMenu* root);
	//zlib
	void XTestMenu_zlibTest(XTestMenu* root);
	void cJsonTest();
	void cJsonXContainerTest();
	void XBase64Test();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif