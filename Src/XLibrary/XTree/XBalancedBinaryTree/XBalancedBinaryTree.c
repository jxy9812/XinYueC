#include "XBalancedBinaryTree.h"
#include "XContainer.h"
#include "XStack.h"
#include <string.h>
size_t XBBTreeNode_typeSize()
{
	return sizeof(XBBTreeNode)-sizeof(XBTreeNode) + XBTreeNode_typeSize();
}
XBBTreeNode* XBBTree_create(const char* pvData, const size_t TypeSize)
{
	//struct XBBTreeNode* nodes = XBTree_creationNode(sizeof(XBBTreeNode),3,1,TypeSize);
	XBBTreeNode* nodes = XMemory_malloc(sizeof(XBBTreeNode));
	if (nodes == NULL)
		return NULL;
	XBTreeNode_init(nodes, XBBTreeNode_typeSize(2), pvData, TypeSize);
	nodes->maxLayer = 1;
	return nodes;
}

/*                                                  删除函数                                                       */
//删除的是叶子节点
static void* leaf_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode)
{

	if (XBTreeNode_GetParent(eraseNode) == NULL)//是叶子也是根
	{
		XTreeNode_delete(eraseNode);
		*this_root = NULL;
		return;
	}
	XBBTreeNode* temp = XBTreeNode_GetParent(eraseNode);

	XBBTreeNode** parent = XTreeNode_getChildrenParentRef(eraseNode);
	if (parent)
		*parent = NULL;
	XTreeNode_delete(eraseNode);
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
		XBBTreeNode** parent = XTreeNode_getChildrenParentRef(eraseNode);
		if (parent)
			*parent = NULL;
		XTreeNode_delete(eraseNode);
		*this_root = ChildNode;
		return;
	}
	XBBTreeNode* currentParent = XBTreeNode_GetParent(eraseNode);//要删除节点的父节点
	XBBTreeNode** temp = XTreeNode_getChildrenParentRef(eraseNode);

	XBBTreeNode** parent = XTreeNode_getChildrenParentRef(eraseNode);
	if (parent)
		*parent = NULL;
	XTreeNode_delete(eraseNode);
	*temp = ChildNode;//将父节点的指针指向新的孩子;
	XBTreeNode_GetParent(ChildNode) = currentParent;//孩子的指针也指向新的父节点
	XBBTree_SetLayerNumberAll(this_root, currentParent);
}
//删除的是有两个孩子
static void* TwoChild_erase(XBBTreeNode** this_root, XBBTreeNode* eraseNode, size_t dataSize)
{
	XBBTreeNode* LeftChildNode = XBTreeNode_GetLChild(eraseNode);//左孩子节点
	XBBTreeNode* rightChildNode = XBTreeNode_GetRChild(eraseNode);//右孩子节点
	XBBTreeNode* preCursor = LeftChildNode;
	XBBTreeNode* LPparent = eraseNode;


	if (XBTreeNode_GetRChild(preCursor) == NULL)//LeftChildNode的孩子不存在右子树的情况
	{
		//size_t dataSize = ((XTreeNode*)eraseNode)->dataSize;
		//XMemory_free(XTreeNode_GetDataPtr(eraseNode));//释放其数据
		//与左子树数据交换
		//XTreeNode_SetDataPtr(eraseNode, XTreeNode_GetDataPtr(preCursor));
		//XTreeNode_SetDataPtr(preCursor,NULL);
		memcpy(XTreeNode_GetDataPtr(eraseNode), XTreeNode_GetDataPtr(preCursor), dataSize);

		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeNode_GetLChild(preCursor);//左子树的左子树
		XTreeNode_delete(freeNode);
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
		//XMemory_free(XTreeNode_GetDataPtr(eraseNode));//释放其数据
		//与左子树数据交换
		//size_t dataSize = ((XTreeNode*)eraseNode)->dataSize;
		if (dataSize > 0)
		{
			//char* tempBuffer = XMalloc(dataSize); //创建临时缓冲区
			//memcpy(tempBuffer, XTreeNode_GetDataPtr(eraseNode), dataSize);
			memcpy(XTreeNode_GetDataPtr(eraseNode), XTreeNode_GetDataPtr(preCursor), dataSize);
			//memcpy(XTreeNode_GetDataPtr(LPreplace), tempBuffer, dataSize);
			//XFree(dataSize);
		}
		//XTreeNode_SetDataPtr(eraseNode, XTreeNode_GetDataPtr(preCursor));
		//XTreeNode_SetDataPtr(preCursor,NULL);

		LPparent = XBTreeNode_GetParent(preCursor);
		XBBTreeNode* freeNode = preCursor;
		preCursor = XBTreeNode_GetLChild(preCursor);//左子树的左子树
		XTreeNode_delete(freeNode);
		XBTreeNode_SetRChild(LPparent, preCursor);//开始建立新的父子关系
		if (preCursor != NULL)
			XBTreeNode_SetParent(preCursor, LPparent);
	}

	XBBTree_SetLayerNumberAll(this_root, LPparent);

}

void* XBBTree_erase(XBBTreeNode** this_root, XCompare compare, XCompareRuleOne Rule, const void* pvData, size_t dataSize)
{
	if (ISNULL(this_root, ""))
		return NULL;
	XBBTreeNode* findRet = XBBTree_findNode(*this_root, compare, Rule, pvData);
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
		TwoChild_erase(this_root, findRet,dataSize);
}
XBBTreeNode* XBBTree_findNode(XBBTreeNode* this_root, XCompare compare, XCompareRuleOne rule, void* pvData)
{
	if (this_root == NULL)//树是空的
		return NULL;
	XBBTreeNode* CurNode = this_root;//当前节点指针
	while (CurNode != NULL)
	{
		if (rule(compare, XTreeNode_GetDataPtr(CurNode), pvData)==XCompare_Equality)
		{
			return CurNode;
		}
		else if (rule(compare, XTreeNode_GetDataPtr(CurNode), pvData)==XCompare_Less)
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
/*                                              插入                                           */
bool XBBTree_insertAlign(XBBTreeNode** this_root, XBBTreeNode* insertNode, XCompare compare, XCompareRuleTwo lessRule, const void* pvData, const size_t dataSize)
{
	//if (!XBTree_insertData(insertNode, pvData, 0))//插入数据

	if (pvData&&!XTreeNode_setData(insertNode,pvData, dataSize))
	{
		XTreeNode_delete(insertNode);//插入失败释放创建的节点
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
		if (lessRule(compare, XTreeNode_GetDataPtr(insertNode), XTreeNode_GetDataPtr(currentNode))== XCompare_Less)
			//if (less(insertNode->XBTNode.data, currentNode->XBTNode.data))
		{
			XTreeNode** ppLChild = XTreeNode_getChildRef(currentNode, XBTreeLChild);
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
			XTreeNode** ppRChild = XTreeNode_getChildRef(currentNode, XBTreeRChild);
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
	XTreeNode_delete(insertNode);
	return false;
}
XBBTreeNode* XBBTree_insert(XBBTreeNode** this_root, XCompare compare, XCompareRuleTwo lessRule, const void* pvData, const size_t dataSize)
{
	if (ISNULL(compare, ""))
		return NULL;
	if (ISNULL(pvData, ""))
		return NULL;
	//创建一个新的节点
	XBBTreeNode* NewNode = XBBTree_create(pvData, dataSize);
	if (ISNULL(NewNode, ""))
		return NULL;
	bool flag = XBBTree_insertAlign(this_root, NewNode, compare, lessRule, pvData, dataSize);
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
	XBBTreeNode** ppThisRootRightChild = XTreeNode_getChildRef(nodes, XBTreeRChild);
	/**ppThisRootRightChild = */XBBTree_SpinRR(this_root, *ppThisRootRightChild);
	return XBBTree_SpinLL(this_root, nodes);
}
//左右旋
XBBTreeNode* XBBTree_SpinLR(XBBTreeNode** this_root, XBBTreeNode* nodes)
{
	if (ISNULL(nodes, ""))
		return NULL;
	XBBTreeNode** ppThisRootLeftChild = XTreeNode_getChildRef(nodes, XBTreeLChild);
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
