#include"XDataStructTest.h"
#if DEMOTEST
#include"XBinaryTreeObject.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	//int* val = XVector_at_base(((XBTreeNode*)LPVal)->value, 0);
	printf("%d ", XBTreeNode_GetData((*(XBTreeNode**)LPVal),int));
}
void XBinaryTreeObjectTest()
{
	while (true)
	{
#if XVector_ON
		int a[] = { 0,1,2,3,4,5,6,7 };
		int* LPa = a;

		XBTreeNode* root = XBTree_createInsertData(LPa++, 3, sizeof(int));
		XBTreeNode* curNode = root;
		*XBTreeNode_getNodeRef(curNode, XBTreeLChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		//curNode->leftChild = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
		curNode = *XBTreeNode_getNodeRef(curNode, XBTreeLChild);
		*XBTreeNode_getNodeRef(curNode, XBTreeLChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		*XBTreeNode_getNodeRef(curNode, XBTreeRChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));

		*XBTreeNode_getNodeRef(root, XBTreeRChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		curNode = *XBTreeNode_getNodeRef(root, XBTreeRChild);
		*XBTreeNode_getNodeRef(curNode, XBTreeLChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));
		*XBTreeNode_getNodeRef(curNode, XBTreeRChild) = XBTree_createInsertData(LPa++, 3, sizeof(int));

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
		XBTree_delete(root,NULL,NULL);
#else
		IS_ON_DEBUG(XVector_ON);
#endif
	}
}

#endif