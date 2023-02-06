#include"Test.h"
#include"XBinaryTreeObject.h"
//打印节点的数据
static void printTreeNode(void* LPVal)
{
	struct TreeNode* currentNode = *(struct TreeNode**)LPVal;
	printf("%d ", *(int*)currentNode->data);
}
void XBinaryTreeObjectTest()
{
	int a[] = { 0,1,2,3,4,5,6,7 };
	int* LPa = a;
	
	TreeNode* root = TreeNode_creationInsertData(LPa++,sizeof(int));
	TreeNode* curNode = root;
	curNode->leftChild = TreeNode_creationInsertData(LPa++, sizeof(int));
	curNode = curNode->leftChild;
	curNode->leftChild = TreeNode_creationInsertData(LPa++, sizeof(int));
	curNode->rightChild = TreeNode_creationInsertData(LPa++, sizeof(int));

	root->rightChild=TreeNode_creationInsertData(LPa++, sizeof(int));
	curNode = root->rightChild;
	curNode->leftChild = TreeNode_creationInsertData(LPa++, sizeof(int));
	curNode->rightChild = TreeNode_creationInsertData(LPa++, sizeof(int));

	//前序测试
	XVector* TreePreorder = BinaryTreeTraversingToXVector(root, BinaryTreePreorder);
	printf("前序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//中序测试
	TreePreorder = BinaryTreeTraversingToXVector(root, BinaryTreeInorder);
	printf("中序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);

	//后序测试
	TreePreorder = BinaryTreeTraversingToXVector(root, BinaryTreePostorder);
	printf("后序遍历:", XVector_size(TreePreorder));
	XVector_iterator_for_each(TreePreorder, printTreeNode,NULL);
	printf("\n");
	XVector_free(TreePreorder);
}