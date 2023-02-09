#include"Test.h"
#include"XBinaryTreeObject.h"
//打印节点的数据
static void printTreeNode(void* LPVal)
{
	struct XBinaryTreeNode* currentNode = *(struct XBinaryTreeNode**)LPVal;
	printf("%d ", *(int*)currentNode->data);
}
void XBinaryTreeObjectTest()
{
	int a[] = { 0,1,2,3,4,5,6,7 };
	int* LPa = a;
	
	XBinaryTreeNode* root = XBinaryTreeObject_creationInsertData(LPa++,sizeof(int));
	XBinaryTreeNode* curNode = root;
	*XBinaryTreeObject_GetTreeNode(curNode, XBTreeLChild) = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
	//curNode->leftChild = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
	curNode = *XBinaryTreeObject_GetTreeNode(curNode, XBTreeLChild);
	*XBinaryTreeObject_GetTreeNode(curNode, XBTreeLChild)  = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
	*XBinaryTreeObject_GetTreeNode(curNode, XBTreeRChild) = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));

	*XBinaryTreeObject_GetTreeNode(root, XBTreeRChild) =XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
	curNode = *XBinaryTreeObject_GetTreeNode(root, XBTreeRChild);
	*XBinaryTreeObject_GetTreeNode(curNode, XBTreeLChild) = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));
	*XBinaryTreeObject_GetTreeNode(curNode, XBTreeRChild) = XBinaryTreeObject_creationInsertData(LPa++, sizeof(int));

	//前序测试
	XVector* TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreePreorder);
	printf("前序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//中序测试
	TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreeInorder);
	printf("中序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//后序测试
	TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreePostorder);
	printf("后序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);
}