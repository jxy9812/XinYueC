#include"Test.h"
#include"XRedBlackTree.h"
#include"XLess.h"
#include"XEquality.h"
//打印节点的数据
static void printTreeNode(void* LPVal, void* args)
{
	printf("%d ", XBTree_GetData(LPVal,int));
}
void XRedBlackTreeTest()
{
	int a[] = { 40,5,6,7,0,1,2,3,10,0,12,456,13,465,123,8748,4,6 };
	int* LPa = a;

	XRBTreeNode* root = XRBTree_insert(NULL, XLess_int, LPa++, sizeof(int));
	for (size_t i = 0; i < sizeof(a) / sizeof(a[0]) - 1; i++)
	{
		if (i == 11)
			i = 11;
		XRBTree_insert(&root, XLess_int, LPa++, sizeof(int));
	}
	//删除测试遍历插入的数组一个个查找删除，直至清空二叉树
	for (size_t i = 0; i < 5/*sizeof(a) / sizeof(a[0])*/; i++)
	{
		if (i == 5)
			i = 5;
		XRBTree_erase(&root, XLess_int, XEquality_int, a + i, sizeof(int));
	}
	if (root != NULL)
	{
		//中序测试
		XVector* TreePreorder = XBTree_TraversingToXVector(root, XBTreeInorder);
		printf("中序遍历:%d\n", XVector_size(TreePreorder));
		XVector_iterator_for_each(TreePreorder, printTreeNode, NULL);
		printf("size:%d\n", XVector_size(TreePreorder));
		XVector_free(TreePreorder);
	}
	else
	{
		printf("二叉树是空的\n");
	}
	
}