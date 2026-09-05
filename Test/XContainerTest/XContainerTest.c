#include"XContainerTest.h"
#include"XTestMenu.h"
#include"XAction.h"

void XTestMenu_XContainerTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("容器");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XStringTest(menu);
	XTestMenu_VectorTest(menu);
	XTestMenu_ListTest(menu);
	XTestMenu_XStackTest(menu);
	XTestMenu_QueueTest(menu);
	XTestMenu_MapTest(menu);
	XTestMenu_SetTest(menu);
	XTestMenu_XJsonTest(menu);
	XTestMenu_XBsonTest(menu);

	XTestMenu_XRingChunkTest(menu);
	XTestMenu_XRingBufferTest(menu);

	// 视图测试
	XTestMenu_XViewTest(menu);
}
void XTestMenu_VectorTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("Vector(数组)");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XVectorTest(menu);
	XTestMenu_XStringListTest(menu);
	XTestMenu_XVariantListTest(menu);
	XTestMenu_XByteArrayTest(menu);
	XTestMenu_XBitArrayTest(menu);
	XTestMenu_XContainerMemoryTest(menu);
}
void XTestMenu_ListTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("List(链表)");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XListDLinkedTest(menu);
	XTestMenu_XListSLinkedTest(menu);
	XTestMenu_XLockFreeListTest(menu);
}
void XTestMenu_QueueTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("Queue(队列)");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XCircularQueueTest(menu);
	XTestMenu_XLockFreeQueueTest(menu);
	XTestMenu_XPriorityQueueTest(menu);
	XTestMenu_XQueueTest(menu);
}
void XTestMenu_MapTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("Map(映射)");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XMapTest(menu);
	XTestMenu_XHashMapTest(menu);
}

void XTestMenu_SetTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("Set(集合)");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XSetTest(menu);
	XTestMenu_XHashSetTest(menu);
}

void XTestMenu_XViewTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("View(视图)");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XByteArrayViewTest(menu);
	XTestMenu_XStringViewTest(menu);
	XTestMenu_XLatin1StringViewTest(menu);
	XTestMenu_XUtf8StringViewTest(menu);
	XTestMenu_XAnyStringViewTest(menu);
}
