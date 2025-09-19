#ifndef XALGORITHMTEST_H
#define XALGORITHMTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XMenu_XLibraryTest(XMenu* root);
	//查询算法
	void XMenu_FindTest(XMenu* root);
	void XMenu_XBinarySearchTest(XMenu* root);
	//树
	void XMenu_XTreeTest(XMenu* root);
	void XMenu_XBinaryTreeTest(XMenu* root);
	void XMenu_XBalancedBinaryTreeTest(XMenu* root);
	void XMenu_XHuffmanTreeTest(XMenu* root);
	void XMenu_XRedBlackTreeTest(XMenu* root);
	//CJson
	void XMenu_CJsonTest(XMenu* root);
	//Base64
	void XMenu_XBase64Test(XMenu* root);
	//zlib
	void XMenu_zlibTest(XMenu* root);
	void cJsonTest();
	void cJsonXContainerTest();
	void XBase64Test();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif