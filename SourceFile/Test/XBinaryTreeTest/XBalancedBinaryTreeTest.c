#include"Test.h"
#include"XBinaryTreeObject.h"
#include"XBalancedBinaryTree.h"
#include"XVector.h"
#include"XLess.h"
#include"XEquality.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	struct XBBTreeNode* currentNode = *(struct XBBTreeNode**)LPVal;
	printf("%d ", *(int*)currentNode->XBTNode.data);
}
void traverse(void* LPVal, void* args)
{
	struct XBBTreeNode* currentNode = *(struct XBBTreeNode**)LPVal;
	if (*XBinaryTreeObject_GetTreeNode(currentNode,XBTreeParent) == NULL)
		return;
	if(XBinaryTreeObject_findChildisParent(currentNode)==NULL)
		printf("找不到：%d \n\n\n\n\n\n", *(int*)currentNode->XBTNode.data);
}
void TreeNodeBalanceTest()
{
	int a[] = { 4,5,6,7,0,1,2,3,10,0,12,456,13,465,123,8748,4,6 };
	int* LPa = a;

	XBBTreeNode* root = XBalancedBinaryTree_insert(NULL, XLess_int,LPa++, sizeof(int));
	for (size_t i = 0; i < sizeof(a)/sizeof(a[0])-1; i++)
	{
		if (i == 11)
			i = 11;
		XBalancedBinaryTree_insert(&root, XLess_int, LPa++, sizeof(int));
		XVector* TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreePreorder);
		XVector_iterator_for_each(TreePreorder, traverse, NULL);
		XVector_free(TreePreorder);
	}
	int findVal = 456;
	XBBTreeNode* findRet = XBalancedBinaryTree_find(root, XLess_int, XEquality_int,&findVal);
	if(findRet!=NULL)
	printf("找到的:%d\n", *(int*)findRet->XBTNode.data);

	//前序测试
	XVector* TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreePreorder);
	printf("前序遍历:%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//中序测试
	TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreeInorder);
	printf("中序遍历:%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("size:%d\n",XVector_size(TreePreorder));
	XVector_free(TreePreorder);

	//后序测试
	TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreePostorder);
	printf("后序遍历::%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);
	printf("高度%d\n", root->maxLayer);

	//删除测试遍历插入的数组一个个查找删除，直至清空二叉树
	for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); i++)
	{
		XBalancedBinaryTree_erase(&root, XLess_int, XEquality_int, a+i, sizeof(int));
	}
	
	//中序测试
	TreePreorder = XBinaryTreeObject_TraversingToXVector(root, XBinaryTreeInorder);
	if(TreePreorder!=NULL)
	{
		printf("中序遍历:%d\n", XVector_size(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		printf("\n");
		printf("size:%d\n", XVector_size(TreePreorder));
		XVector_free(TreePreorder);
		printf("高度%d\n", root->maxLayer);
	}
	else
	{
		printf("二叉树是空的\n");
	}
}