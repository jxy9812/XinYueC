#include"XDataStructTest.h"
#if DEMOTEST
#include"XBinaryTreeObject.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	//int* val = XVector_at(((XBTreeNode*)LPVal)->values, 0);
	printf("%d ", XBTree_GetData((*(XBTreeNode**)LPVal), 0,int));
}
void XBinaryTreeObjectTest()
{
#if XVector_ON
	int a[] = { 0,1,2,3,4,5,6,7 };
	int* LPa = a;
	
	XBTreeNode* root = XBTree_creationInsertData(LPa++,3,sizeof(int));
	XBTreeNode* curNode = root;
	*XBTree_GetTreeNode(curNode, XBTreeLChild) = XBTree_creationInsertData(LPa++,3, sizeof(int));
	//curNode->leftChild = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
	curNode = *XBTree_GetTreeNode(curNode, XBTreeLChild);
	*XBTree_GetTreeNode(curNode, XBTreeLChild)  = XBTree_creationInsertData(LPa++,3, sizeof(int));
	*XBTree_GetTreeNode(curNode, XBTreeRChild) = XBTree_creationInsertData(LPa++,3, sizeof(int));

	*XBTree_GetTreeNode(root, XBTreeRChild) =XBTree_creationInsertData(LPa++,3, sizeof(int));
	curNode = *XBTree_GetTreeNode(root, XBTreeRChild);
	*XBTree_GetTreeNode(curNode, XBTreeLChild) = XBTree_creationInsertData(LPa++,3, sizeof(int));
	*XBTree_GetTreeNode(curNode, XBTreeRChild) = XBTree_creationInsertData(LPa++,3, sizeof(int));

	//前序测试
	XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
	printf("前序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//中序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
	printf("中序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//后序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreePostorder);
	printf("后序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

#endif