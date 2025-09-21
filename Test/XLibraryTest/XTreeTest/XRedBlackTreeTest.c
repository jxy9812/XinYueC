#include"XDataStructTest.h"
#if DEMOTEST
#include"XRedBlackTree.h"
#include"XCompare.h"
#include<stdio.h>
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	XPrintf("%-3d ", XTreeNode_GetData(*(XTreeNode**)LPVal,int));
}
static void traverse(void* LPVal, void* args)
{
	struct XRBTreeNode* currentNode = *(struct XRBTreeNode**)LPVal;
	//if (*XTreeNode_getChildRef(currentNode, XTreeParent) == NULL)
	if (XTreeNode_GetParent(currentNode) == NULL)
		return;
	if (XTreeNode_getChildrenParentRef(currentNode) == NULL)
		XPrintf("找不到：%d \n\n\n\n\n\n", XTreeNode_GetData(currentNode,int));
	
}
void XRedBlackTreeTest()
{
#if XVector_ON
	int a[] = { 40,5,6,7,0,1,2,3,10,0,12,456,13,465,123,8748,4,6 };
	int* LPa = a;
	XPrintf("插入测试\n");
	XRBTreeNode* root = XRBTree_insert(NULL, XCompare_int,XCompareRuleTwo_BinaryTree, LPa++, sizeof(int));
	for (size_t i = 0; i < sizeof(a) / sizeof(a[0]) - 1; i++)
	{
		if (i == 11)
			i = 11;
		XRBTree_insert(&root, XCompare_int, XCompareRuleTwo_BinaryTree, LPa++, sizeof(int));
	}
	if (root != NULL)
	{
		//中序测试
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
		XPrintf("中序遍历:%d\n", XVector_size_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		XPrintf("size:%d\n", XVector_size_base(TreePreorder));
		XVector_delete_base(TreePreorder);
	}
	else
	{
		XPrintf("二叉树是空的\n");
	}
	XPrintf("删除测试\n");
	//删除测试遍历插入的数组一个个查找删除，直至清空二叉树
	for (size_t i = 0; i <sizeof(a) / sizeof(a[0]); i++)
	{
		XRBTree_remove(&root,XCompare_int,XCompareRuleOne_BinaryTree, a + i,NULL,NULL);
	}
	if (root != NULL)
	{
		//中序测试
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
		XPrintf("中序遍历:%d\n", XVector_size_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		XPrintf("size:%d\n", XVector_size_base(TreePreorder));
		XVector_delete_base(TreePreorder);
	}
	else
	{
		XPrintf("二叉树是空的\n");
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}
void XMenu_XRedBlackTreeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XRedBlackTree(红黑树)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XRedBlackTreeTest);
	}
}
#endif