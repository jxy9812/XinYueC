#include "XBalancedBinaryTree.h"
#include"XContainerObject.h"
#include"XStack.h"
XBBTreeNode* XBBTree_create(const size_t TypeSize)
{
	//struct XBBTreeNode* nodes = XBTree_creationNode(sizeof(XBBTreeNode),3,1,TypeSize);
	XBBTreeNode* nodes = XMemory_malloc(sizeof(XBBTreeNode));
	if (nodes == NULL)
		return NULL;
	XBTreeNode_init(nodes, 3, 1, TypeSize);
	nodes->maxLayer = 1;
	return nodes;
}

XBBTreeNode* XBBTree_findData(XBBTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne Rule, void* pvData)
{
	/*if (ISNULL(this_root,"")))
		return NULL; */
	if (this_root == NULL)//树是空的
		return NULL;
	XBBTreeNode* CurNode = this_root;//当前节点指针
	while (CurNode!=NULL)
	{
		//printf("%d \n", XBTreeNode_GetParent(CurNode,0,int));
		//if (equality(CurNode->XBTNode.data, pvData))
		if(Rule(equality, CurNode, pvData))
		{
			return CurNode;
		}
		//else if (less(CurNode->XBTNode.data, pvData))
		else if(Rule(less, CurNode, pvData))
		{
			CurNode = XBTreeNode_GetRChild(CurNode);
		}
		else
		{
			CurNode = XBTreeNode_GetLChild(CurNode);
		}
	}
	return NULL;
}
/*                                                  删除函数                                                       */
//删除的是叶子节点
static void* leaf_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{

	if (XBTreeNode_GetParent(eraseNode) == NULL)//是叶子也是根
	{
		XBTreeNode_delete(eraseNode);
		*this_root = NULL;
		return;
	}
	XBBTreeNode* temp = XBTreeNode_GetParent(eraseNode);

	XBBTreeNode** parent = XBTreeNode_getChildrenParentRef(eraseNode);
	if (parent)
		*parent = NULL;
	XBTreeNode_delete(eraseNode);
	XBBTree_SetLayerNumberAll(this_root, temp);
}
//删除的是只有一个孩子
static void* OneChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* ChildNode = NULL;//孩子节点
	XBBTreeNode* eraseLeft = XBTreeNode_GetLChild(eraseNode);
	XBBTreeNode* eraseRight = XBTreeNode_GetRChild(eraseNode);
	if (eraseLeft != NULL)
		ChildNode = eraseLeft;
	if (eraseRight != NULL)
		ChildNode = eraseRight;

	if (eraseNode == *this_root)//也是根
	{
		XBBTreeNode** parent = XBTreeNode_getChildrenParentRef(eraseNode);
		if (parent)
			*parent = NULL;
		XBTreeNode_delete(eraseNode);
		*this_root = ChildNode;
		return;
	}
	XBBTreeNode* currentParent = XBTreeNode_GetParent(eraseNode);//要删除节点的父节点
	XBBTreeNode** temp = XBTreeNode_getChildrenParentRef(eraseNode);

	XBBTreeNode** parent = XBTreeNode_getChildrenParentRef(eraseNode);
	if (parent)
		*parent = NULL;
	XBTreeNode_delete(eraseNode);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	XBTreeNode_GetParent(ChildNode) = currentParent;//孩子的指针也指向新的父节点
	XBBTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{
	XBBTreeNode* LeftChildNode = XBTreeNode_GetLChild(eraseNode);//左孩子节点
	XBBTreeNode* rightChildNode = XBTreeNode_GetRChild(eraseNode);//右孩子节点
	XBBTreeNode* preCursor = LeftChildNode;
	XBBTreeNode* LPparent = eraseNode;


	if (XBTreeNode_GetRChild(preCursor) == NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		XMemory_free(eraseNode->XBTNode.values);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.values = preCursor->XBTNode.values;
		preCursor->XBTNode.values = NULL;

		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeNode_GetLChild(preCursor);//左子树的左子树
		XBTreeNode_delete(freeNode);
		XBTreeNode_SetLChild(LPparent, preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeNode_SetParent(preCursor, LPparent);
	}
	else
	{
		while (XBTreeNode_GetRChild(preCursor) != NULL)//向右边找
		{
			preCursor = XBTreeNode_GetRChild(preCursor);
		}
		XMemory_free(eraseNode->XBTNode.values);//释放其数据
		//与左子树数据交换
		eraseNode->XBTNode.values = preCursor->XBTNode.values;
		preCursor->XBTNode.values = NULL;

		LPparent = XBTreeNode_GetParent(preCursor);
		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeNode_GetLChild(preCursor);//左子树的左子树
		XBTreeNode_delete(freeNode);
		XBTreeNode_SetRChild(LPparent, preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeNode_SetParent(preCursor, LPparent);
	}

	XBBTree_SetLayerNumberAll(this_root, LPparent);

}

void* XBBTree_erase(XBBTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule, const void* pvData, const size_t TypeSize)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XBBTreeNode* findRet = XBBTree_findData(*this_root, less, equality, Rule, pvData);
	if (findRet == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTreeNode_GetLChild(findRet) != NULL)
		++count;
	if (XBTreeNode_GetRChild(findRet) != NULL)
		++count;
	if (count == 0)//叶子
		leaf_erase(this_root, findRet);
	if (count == 1)//一个孩子
		OneChild_erase(this_root, findRet);
	if (count == 2)//两个孩子
		TwoChild_erase(this_root, findRet);
}
/*                                              插入                                           */
bool XBBTree_insertAlign(XBBTreeNode** this_root, XBBTreeNode* insertNode, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize)
{
	//if (!XBTree_insertData(insertNode, pvData, 0))//插入数据
	if (!XBTreeNode_setData(insertNode, 0, pvData))
	{
		XBTreeNode_delete(insertNode);//插入失败释放创建的节点
		return false;
	}
	if (this_root == NULL || *this_root == NULL)//如果没有根节点或根为空
	{
		return true;
	}
	//开始遍历,插入节点
	XBBTreeNode* currentNode = *this_root;

	while (currentNode != NULL)
	{
		//满足小于往左边放
		if (lessRule(less, insertNode, currentNode))
			//if (less(insertNode->XBTNode.data, currentNode->XBTNode.data))
		{
			XBTreeNode** ppLChild = XBTreeNode_getNodeRef(currentNode, XBTreeLChild);
			if (*ppLChild == NULL)//建立关系
			{
				*ppLChild = insertNode;
				XBTreeNode_SetParent(insertNode, currentNode);
				return true;
			}
			else
			{
				currentNode = *ppLChild;
			}
		}
		else//满足大于等于的情况
		{
			XBTreeNode** ppRChild = XBTreeNode_getNodeRef(currentNode, XBTreeRChild);
			if (*ppRChild == NULL)//建立关系
			{
				*ppRChild = insertNode;
				XBTreeNode_SetParent(insertNode, currentNode);
				return true;
			}
			else
			{
				currentNode = *ppRChild;
			}
		}
	}
	XBTreeNode_delete(insertNode);
	return false;
}
XBBTreeNode* XBBTree_insert(XBBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize)
{
	if (ISNULL(less, ""))
		return NULL;
	if (ISNULL(pvData, ""))
		return NULL;
	if (ISNULL(TypeSize, ""))
		return NULL;
	//创建一个新的节点
	XBBTreeNode* NewNode = XBBTree_create(TypeSize);
	if (ISNULL(NewNode, ""))
		return NULL;
	bool flag = XBBTree_insertAlign(this_root, NewNode, less, lessRule, pvData, TypeSize);
	if (!flag)
	{
		printf("节点插入失败\n");
		return NULL;
	}
	XBBTree_SetLayerNumberAll(this_root, XBTreeNode_GetParent(NewNode));
	return NewNode;
}


const size_t XBBTree_GetLayerNumberThis(const XBBTreeNode* this_root)
{
	if (this_root != NULL)
		return this_root->maxLayer;
	return 0;
}
const size_t XBBTree_GetLayerNumberChild(const XBBTreeNode* this_root)
{
	size_t left = XBBTree_GetLayerNumberThis(XBTreeNode_GetLChild(this_root));
	size_t right = XBBTree_GetLayerNumberThis(XBTreeNode_GetRChild(this_root));
	return (left > right) ? left : right;
}
const size_t XBBTree_SetLayerNumberThis(XBBTreeNode* this_root)
{
	return this_root->maxLayer = 1 + XBBTree_GetLayerNumberChild(this_root);;
}
const size_t XBBTree_SetLayerNumberAll(XBBTreeNode** this_root, XBBTreeNode* currentNode)
{
	size_t count = 0;//向上调整一共经历了多少个节点
	//循环返回父节点设置层数
	while (currentNode != NULL)
	{
		currentNode->maxLayer = 1 + XBBTree_GetLayerNumberChild(currentNode);
		currentNode = XBBTree_Spin(this_root, currentNode);
		currentNode = XBTreeNode_GetParent(currentNode);
		++count;
	}
	return count;
}


//右旋
XBBTreeNode* XBBTree_SpinRR(XBBTreeNode** this_root, XBBTreeNode* nodes)
{
	XBBTreeNode* NewNode = XBTree_SpinRR(this_root, nodes);
	if (ISNULL(NewNode, ""))
		return NULL;
	XBBTree_SetLayerNumberThis(nodes);
	XBBTree_SetLayerNumberThis(NewNode);
	return NewNode;
}
//左旋
XBBTreeNode* XBBTree_SpinLL(XBBTreeNode** this_root, XBBTreeNode* nodes)
{
	XBBTreeNode* NewNode = XBTree_SpinLL(this_root, nodes);
	if (ISNULL(NewNode, ""))
		return NULL;
	XBBTree_SetLayerNumberThis(nodes);
	XBBTree_SetLayerNumberThis(NewNode);
	return NewNode;
}
//右左旋
XBBTreeNode* XBBTree_SpinRL(XBBTreeNode** this_root, XBBTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	XBBTreeNode** ppThisRootRightChild = XBTreeNode_getNodeRef(nodes, XBTreeRChild);
	/**ppThisRootRightChild = */XBBTree_SpinRR(this_root, *ppThisRootRightChild);
	return XBBTree_SpinLL(this_root, nodes);
}
//左右旋
XBBTreeNode* XBBTree_SpinLR(XBBTreeNode** this_root, XBBTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	XBBTreeNode** ppThisRootLeftChild = XBTreeNode_getNodeRef(nodes, XBTreeLChild);
	/**ppThisRootLeftChild =*/ XBBTree_SpinLL(this_root, *ppThisRootLeftChild);
	return XBBTree_SpinRR(this_root, nodes);
}

XBBTreeNode* XBBTree_Spin(XBBTreeNode** this_root, XBBTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	size_t leftLayer = XBBTree_GetLayerNumberThis(XBTreeNode_GetLChild(nodes));
	size_t rightLayer = XBBTree_GetLayerNumberThis(XBTreeNode_GetRChild(nodes));
	XBBTreeNode* newNode = nodes;
	if (abs(leftLayer - rightLayer) > 1)
	{
		XBBTreeNode* centre = NULL;
		if (leftLayer > rightLayer)//左边比右边高度大于1(右旋/左右旋)
		{
			centre = XBTreeNode_GetLChild(nodes);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTreeNode_GetLChild(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTreeNode_GetRChild(centre));
			if (centreLeft > centreRight)//左大于右(右旋)
				newNode = XBBTree_SpinRR(this_root, nodes);
			else
				newNode = XBBTree_SpinLR(this_root, nodes);
		}
		else//左旋/右左旋
		{
			centre = XBTreeNode_GetRChild(nodes);
			size_t centreLeft = XBBTree_GetLayerNumberThis(XBTreeNode_GetLChild(centre));
			size_t centreRight = XBBTree_GetLayerNumberThis(XBTreeNode_GetRChild(centre));
			if (centreLeft < centreRight)//右大于左(左旋)
				newNode = XBBTree_SpinLL(this_root, nodes);
			else
				newNode = XBBTree_SpinRL(this_root, nodes);
		}

	}
	return newNode;
}
