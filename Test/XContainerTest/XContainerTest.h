#ifndef XCONTAINERTEST_H
#define XCONTAINERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST
	//容器主菜单
	void XTestMenu_XContainerTest(XTestMenu* root);

	//数组
	void XTestMenu_VectorTest(XTestMenu* root);
	void XTestMenu_XVectorTest(XTestMenu* root);
	void XTestMenu_XStringListTest(XTestMenu* root);
	void XTestMenu_XVariantListTest(XTestMenu* root);
	void XTestMenu_XByteArrayTest(XTestMenu* root);
	void XTestMenu_XBitArrayTest(XTestMenu* root);
	void XTestMenu_XContainerMemoryTest(XTestMenu* root);

	void XTestMenu_ListTest(XTestMenu* root);
	void XTestMenu_XListDLinkedTest(XTestMenu* root);
	void XTestMenu_XListSLinkedTest(XTestMenu* root);
	void XTestMenu_XLockFreeListTest(XTestMenu* root);

	//栈
	void XTestMenu_XStackTest(XTestMenu* root);
	//队列
	void XTestMenu_QueueTest(XTestMenu* root);
	void XTestMenu_XCircularQueueTest(XTestMenu* root);
	void XTestMenu_XLockFreeQueueTest(XTestMenu* root);
	void XTestMenu_XPriorityQueueTest(XTestMenu* root);
	void XTestMenu_XQueueTest(XTestMenu* root);
	void XTestMenu_XPriorityMapQueueTest(XTestMenu* root);
	//映射 Map
	void XTestMenu_MapTest(XTestMenu* root);
	void XTestMenu_XMapTest(XTestMenu* root);
	void XTestMenu_XHashMapTest(XTestMenu* root);
	//集合 set
	void XTestMenu_SetTest(XTestMenu* root);
	void XTestMenu_XSetTest(XTestMenu* root);
	void XTestMenu_XHashSetTest(XTestMenu* root);

	void XTestMenu_XRingChunkTest(XTestMenu* root);
	void XTestMenu_XRingBufferTest(XTestMenu* root);
	//字符串
	void XTestMenu_XStringTest(XTestMenu* root);

	void XTestMenu_XJsonTest(XTestMenu* root);
	void XTestMenu_XBsonTest(XTestMenu* root);

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
	void XTestMenu_XViewTest(XTestMenu* root);
	void XTestMenu_XByteArrayViewTest(XTestMenu* root);
	void XTestMenu_XStringViewTest(XTestMenu* root);
	void XTestMenu_XLatin1StringViewTest(XTestMenu* root);
	void XTestMenu_XUtf8StringViewTest(XTestMenu* root);
	void XTestMenu_XAnyStringViewTest(XTestMenu* root);

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
