#include "XBalancedBinaryTree.h"
#include"XBinaryTreeObject.h"
#include"XContainerObject.h"
//删除的是根节点
static void* Root_erase(TreeNodeBalance** this_root)
{

}
void* XBalancedBinaryTree_erase(TreeNodeBalance** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize)
{
	if (isObjectNULL(this_root, "TreeNodeBalance_erase-this_root"))
		return NULL;
	TreeNodeBalance* findRet = XBalancedBinaryTree_find(*this_root, less, equality, LPData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到

}