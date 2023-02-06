#include"Test.h"
#include"XBinaryTreeObject.h"
#include"XBalancedBinaryTree.h"
#include"XVector.h"
#include"XLess.h"
#include"XEquality.h"
//打印节点的数据
static void printTreeNode(void* LPVal)
{
	struct TreeNodeBalance* currentNode = *(struct TreeNodeBalance**)LPVal;
	printf("%d ", *(int*)currentNode->data);
}
void TreeNodeBalanceTest()
{
	int a[] = { 4,5,6,7,0,1,2,3,10,0,12,456,13,465,123,8748,4,6 };
	int* LPa = a;

	TreeNodeBalance* root = TreeNodeBalance_insert(NULL, XLess_int,LPa++, sizeof(int));
	for (size_t i = 0; i < sizeof(a)/sizeof(a[0])-1; i++)
	{
		TreeNodeBalance_insert(&root, XLess_int, LPa++, sizeof(int));
	}
	int findVal = 456;
	TreeNodeBalance* findRet = TreeNodeBalance_find(root, XLess_int, XEquality_int,&findVal);
	if(findRet!=NULL)
	printf("找到的:%d\n", *(int*)findRet->data);

	//前序测试
	XVector* TreePreorder = BinaryTreeTraversingToXVector(root, BinaryTreePreorder);
	printf("前序遍历:%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//中序测试
	TreePreorder = BinaryTreeTraversingToXVector(root, BinaryTreeInorder);
	printf("中序遍历:%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//后序测试
	TreePreorder = BinaryTreeTraversingToXVector(root, BinaryTreePostorder);
	printf("后序遍历::%d\n", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);
	printf("高度%d\n", root->maxLayer);
}