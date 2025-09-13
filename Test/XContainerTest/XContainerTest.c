#include"XContainerTest.h"
#include"XMenu.h"
#include"XAction.h"

void XMenu_XContainerTest(XMenu* root)
{
	XMenu* menu = XMenu_create("容器");
	XMenu_addMenu(root, menu);
	XMenu_XStringTest(menu);
	XMenu_VectorTest(menu);
	XMenu_ListTest(menu);
	XMenu_XStackTest(menu);
	XMenu_QueueTest(menu);
	XMenu_MapTest(menu);
	XMenu_SetTest(menu);
	XMenu_XJsonTest(menu);
	XMenu_XBsonTest(menu);
}
void XMenu_VectorTest(XMenu* root)
{
	XMenu* menu = XMenu_create("Vector(数组)");
	XMenu_addMenu(root, menu);
	XMenu_XVectorTest(menu);
	XMenu_XStringListTest(menu);
	XMenu_XVariantListTest(menu);
}
void XMenu_ListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("List(链表)");
	XMenu_addMenu(root, menu);
	XMenu_XListDLinkedTest(menu);
	XMenu_XListSLinkedTest(menu);
	XMenu_XListSLinkedAtomicTest(menu);
}
void XMenu_QueueTest(XMenu* root)
{
	XMenu* menu = XMenu_create("Queue(队列)");
	XMenu_addMenu(root, menu);
	XMenu_XCircularQueueTest(menu);
	XMenu_XCircularQueueAtomicTest(menu);
	XMenu_XPriorityQueueTest(menu);
	XMenu_XQueueTest(menu);
	XMenu_XPriorityMapQueueTest(menu);
}
void XMenu_MapTest(XMenu* root)
{
	XMenu* menu = XMenu_create("Map(映射)");
	XMenu_addMenu(root, menu);
	XMenu_XMapTest(menu);
	XMenu_XHashMapTest(menu);
}

void XMenu_SetTest(XMenu* root)
{
	XMenu* menu = XMenu_create("Set(集合)");
	XMenu_addMenu(root, menu);
	XMenu_XSetTest(menu);
	XMenu_XHashSetTest(menu);
}