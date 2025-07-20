#include"XDataStructTest.h"
#if DEMOTEST
#include"XBinaryTree.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	//int* val = XVector_at_base(((XTreeNode*)LPVal)->value, 0);
	printf("%d ", XTreeNode_GetData((*(XTreeNode**)LPVal),int));
}
void XBinaryTreeObjectTest()
{
	while (true)
	{
#if XVector_ON
		int a[] = { 0,1,2,3,4,5,6,7 };
		int* LPa = a;

		XTreeNode* root = XBTree_createInsertData(LPa++, 3, sizeof(int));
		XTreeNode* curNode = root;
		*XTreeNode_getChildRef(curNode, XBTreeLChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		//curNode->leftChild = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
		curNode = *XTreeNode_getChildRef(curNode, XBTreeLChild);
		*XTreeNode_getChildRef(curNode, XBTreeLChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		*XTreeNode_getChildRef(curNode, XBTreeRChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));

		*XTreeNode_getChildRef(root, XBTreeRChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		curNode = *XTreeNode_getChildRef(root, XBTreeRChild);
		*XTreeNode_getChildRef(curNode, XBTreeLChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		*XTreeNode_getChildRef(curNode, XBTreeRChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));

		//前序测试
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
		printf("前序遍历:", XVector_getSize_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		printf("\n");
		XVector_delete_base(TreePreorder);

		//中序测试
		TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
		printf("中序遍历:", XVector_getSize_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		printf("\n");
		XVector_delete_base(TreePreorder);

		//后序测试
		TreePreorder = XBTree_TraversingToXVector(root, XBTreePostorder);
		printf("后序遍历:", XVector_getSize_base(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		printf("\n");
		XVector_delete_base(TreePreorder);
		XTree_delete(root,NULL,NULL);
#else
		IS_ON_DEBUG(XVector_ON);
#endif
	}
}

#endif