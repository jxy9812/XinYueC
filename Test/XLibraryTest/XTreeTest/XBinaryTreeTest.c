#include"XDataStructTest.h"
#if DEMOTEST
#include"XBinaryTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	//int* val = XVector_at_base(((XTreeNode*)LPVal)->value, 0);
	XPrintf("%d ", XTreeNode_GetData((*(XTreeNode**)LPVal),int));
}
void XBinaryTreeTest()
{
	//while (true)
	{
#if XVector_ON
		int a[] = { 0,1,2,3,4,5,6,7 };
		int* LPa = a;

		XTreeNode* root = XBTreeNode_create(LPa++, sizeof(int));
		XTreeNode* curNode = root;
		*XTreeNode_getChildRef(curNode, XBTreeLChild) = XBTreeNode_create(LPa++, sizeof(int));
		//curNode->leftChild = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
		curNode = *XTreeNode_getChildRef(curNode, XBTreeLChild);
		*XTreeNode_getChildRef(curNode, XBTreeLChild) = XBTreeNode_create(LPa++, sizeof(int));
		*XTreeNode_getChildRef(curNode, XBTreeRChild) = XBTreeNode_create(LPa++,sizeof(int));

		*XTreeNode_getChildRef(root, XBTreeRChild) = XBTreeNode_create(LPa++, sizeof(int));
		curNode = *XTreeNode_getChildRef(root, XBTreeRChild);
		*XTreeNode_getChildRef(curNode, XBTreeLChild) = XBTreeNode_create(LPa++, sizeof(int));
		*XTreeNode_getChildRef(curNode, XBTreeRChild) = XBTreeNode_create(LPa++, sizeof(int));

		//前序测试
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
		XPrintf("前序遍历:", XVector_size_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		XPrintf("\n");
		XVector_delete_base(TreePreorder);

		//中序测试
		TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
		XPrintf("中序遍历:", XVector_size_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		XPrintf("\n");
		XVector_delete_base(TreePreorder);

		//后序测试
		TreePreorder = XBTree_TraversingToXVector(root, XBTreePostorder);
		XPrintf("后序遍历:", XVector_size_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		XPrintf("\n");
		XVector_delete_base(TreePreorder);
		XTree_delete(root,NULL,NULL);
#else
		IS_ON_DEBUG(XVector_ON);
#endif
	}
	XCoreApplication_quit();
}
void XMenu_XBinaryTreeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XBinaryTree(二叉树)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XBinaryTreeTest);
	}
}
#endif