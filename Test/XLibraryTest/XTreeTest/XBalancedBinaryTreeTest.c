#include"XDataStructTest.h"
#if DEMOTEST
#include"XBalancedBinaryTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	XPrintf("%d ", XTreeNode_GetData(*(XTreeNode**)LPVal,int));
}
void traverse(void* LPVal, void* args)
{
	struct XBBTreeNode* currentNode = *(struct XBBTreeNode**)LPVal;
	//if (*XTreeNode_getChildRef(currentNode,XTreeParent) == NULL)
	if(XTreeNode_GetParent(currentNode)==NULL)
		return;
	if(XTreeNode_getChildrenParentRef(currentNode)==NULL)
		XPrintf("找不到：%d \n\n\n\n\n\n", *(int*)currentNode->XBTNode.data);
}
void XBalancedBinaryTreeTest()
{
#if XVector_ON
	int a[] = { 4,5,6,7,0,1,2,3,10,0,12,456,13,465,123,8748,4,6 };
	int* LPa = a;

	XBBTreeNode* root = XBBTree_insert(NULL, XCompare_int,XCompareRuleTwo_BinaryTree, LPa++, sizeof(int));
	for (size_t i = 0; i < sizeof(a)/sizeof(a[0])-1; i++)
	{
		if (i == 11)
			i = 11;
		XBBTree_insert(&root, XCompare_int, XCompareRuleTwo_BinaryTree, LPa++, sizeof(int));
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
		XVector_iterator_for_each(TreePreorder, traverse, NULL);
		XVector_delete_base(TreePreorder);
	}
	int findVal = 456;
	XBBTreeNode* findRet = XBBTree_findNode(root, XCompare_int,XCompareRuleOne_BinaryTree,&findVal);
	if(findRet!=NULL)
	XPrintf("找到的:%d\n", XTreeNode_GetData(findRet,int));

	//前序测试
	XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
	XPrintf("前序遍历:%d\n", XVector_size_base(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	XPrintf("\n");
	XVector_delete_base(TreePreorder);

	//中序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
	XPrintf("中序遍历:%d\n", XVector_size_base(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	XPrintf("size:%d\n",XVector_size_base(TreePreorder));
	XVector_delete_base(TreePreorder);

	//后序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreePostorder);
	XPrintf("后序遍历::%d\n", XVector_size_base(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	XPrintf("\n");
	XVector_delete_base(TreePreorder);
	XPrintf("高度%d\n", root->maxLayer);

	//删除测试遍历插入的数组一个个查找删除，直至清空二叉树
	for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); i++)
	{
		XBBTree_erase(&root, XCompare_int,XCompareRuleOne_BinaryTree, a+i, sizeof(int));
	}
	
	//中序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
	if(TreePreorder!=NULL)
	{
		XPrintf("中序遍历:%d\n", XVector_size_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		XPrintf("\n");
		XPrintf("size:%d\n", XVector_size_base(TreePreorder));
		XVector_delete_base(TreePreorder);
		XPrintf("高度%d\n", root->maxLayer);
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
void XMenu_XBalancedBinaryTreeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XBalancedBinaryTree(平衡二叉树)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XBalancedBinaryTreeTest);
	}
}

#endif