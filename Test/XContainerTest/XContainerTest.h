#ifndef XCONTAINERTEST_H
#define XCONTAINERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	//容器主菜单
	void XMenu_XContainerTest(XMenu* root);

	//数组
	void XMenu_VectorTest(XMenu* root);
	void XMenu_XVectorTest(XMenu* root);
	void XMenu_XStringListTest(XMenu* root);
	void XMenu_XVariantListTest(XMenu* root);
	void XMenu_XByteArrayTest(XMenu* root);
	void XMenu_XBitArrayTest(XMenu* root);

	void XMenu_ListTest(XMenu* root);
	void XMenu_XListDLinkedTest(XMenu* root);
	void XMenu_XListSLinkedTest(XMenu* root);
	void XMenu_XLockFreeListTest(XMenu* root);

	//栈
	void XMenu_XStackTest(XMenu* root);
	//队列
	void XMenu_QueueTest(XMenu* root);
	void XMenu_XCircularQueueTest(XMenu* root);
	void XMenu_XLockFreeQueueTest(XMenu* root);
	void XMenu_XPriorityQueueTest(XMenu* root);
	void XMenu_XQueueTest(XMenu* root);
	void XMenu_XPriorityMapQueueTest(XMenu* root);
	//映射 Map
	void XMenu_MapTest(XMenu* root);
	void XMenu_XMapTest(XMenu* root);
	void XMenu_XHashMapTest(XMenu* root);
	//集合 set
	void XMenu_SetTest(XMenu* root);
	void XMenu_XSetTest(XMenu* root);
	void XMenu_XHashSetTest(XMenu* root);

	void XMenu_XRingChunkTest(XMenu* root);
	void XMenu_XRingBufferTest(XMenu* root);
	//字符串
	void XMenu_XStringTest(XMenu* root);

	void XMenu_XJsonTest(XMenu* root);
	void XMenu_XBsonTest(XMenu* root);

	//字符串数组测试
	void XStringListTest();
	//字符串测试
	void XStringTest();
	//map映射测试
	void  XMapTest();
	void XHashMapTest();
	
	void XLockFreeListTest();
	void XLockFreeListSwapTest();
	void XLockFreeListSortTest();
	void XLockFreeListIterator();
	void XHashSetTest();
	void XSetTest();
	void XVariantListTest();

	void XJsonArrayTest();
	void XJsonObjectTest();

	// ==================== 视图测试 ====================
	void XMenu_XViewTest(XMenu* root);
	void XMenu_XByteArrayViewTest(XMenu* root);
	void XMenu_XStringViewTest(XMenu* root);
	void XMenu_XLatin1StringViewTest(XMenu* root);
	void XMenu_XUtf8StringViewTest(XMenu* root);
	void XMenu_XAnyStringViewTest(XMenu* root);

	void XByteArrayViewTest(void);
	void XStringViewTest(void);
	void XLatin1StringViewTest(void);
	void XUtf8StringViewTest(void);
	void XAnyStringViewTest(void);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
