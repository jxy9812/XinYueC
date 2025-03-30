#include"XDataStructTest.h"
#if DEMOTEST
#include"XBalancedBinaryTree.h"
#include"XLess.h"
#include"XEquality.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	printf("%d ", XBTree_GetData(*(XBTreeNode**)LPVal,0, int));
}
void traverse(void* LPVal, void* args)
{
	struct XBBTreeNode* currentNode = *(struct XBBTreeNode**)LPVal;
	if (*XBTree_GetTreeNode(currentNode,XBTreeParent) == NULL)
		return;
	if(XBTree_findChildisParent(currentNode)==NULL)
		printf("找不到：%d \n\n\n\n\n\n", *(int*)currentNode->XBTNode.values);
}
void XBalancedBinaryTreeTest()
{
#if XVector_ON
	int a[] = { 4,5,6,7,0,1,2,3,10,0,12,456,13,465,123,8748,4,6 };
	int* LPa = a;

	XBBTreeNode* root = XBBTree_insert(NULL, XLess_int,XCompareRuleTwo_BinaryTree, LPa++, sizeof(int));
	for (size_t i = 0; i < sizeof(a)/sizeof(a[0])-1; i++)
	{
		if (i == 11)
			i = 11;
		XBBTree_insert(&root, XLess_int, XCompareRuleTwo_BinaryTree, LPa++, sizeof(int));
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
		XVector_iterator_for_each(TreePreorder, traverse, NULL);
		XVector_free(TreePreorder);
	}
	int findVal = 456;
	XBBTreeNode* findRet = XBBTree_findData(root, XLess_int, XEquality_int,XCompareRuleOne_BinaryTree,&findVal);
	if(findRet!=NULL)
	printf("找到的:%d\n", XBTree_GetData(findRet,0,int));

	//前序测试
	XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreePreorder);
	printf("前序遍历:%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//中序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
	printf("中序遍历:%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("size:%d\n",XVector_size(TreePreorder));
	XVector_free(TreePreorder);

	//后序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreePostorder);
	printf("后序遍历::%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);
	printf("高度%d\n", root->maxLayer);

	//删除测试遍历插入的数组一个个查找删除，直至清空二叉树
	for (size_t i = 0; i < sizeof(a) / sizeof(a[0]); i++)
	{
		XBBTree_erase(&root, XLess_int, XEquality_int,XCompareRuleOne_BinaryTree, a+i, sizeof(int));
	}
	
	//中序测试
	TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
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
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
#endif