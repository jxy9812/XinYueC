#include"XRedBlackTree.h"
#include"XClass.h"
#include"XBalancedBinaryTree.h"
#include<string.h>
size_t XRBTree_typeSize()
{
	return sizeof(XRBTreeNode) - sizeof(XBTreeNode) + XBTreeNode_typeSize();
}
XRBTreeNode* XRBTree_create(const char* pvData, const size_t dataTypeSize)
{
	XRBTreeNode* node = XMalloc_System(XRBTree_typeSize()+ dataTypeSize);
	if (!node)return NULL;
	XRBTree_init(node, XRBTree_typeSize(),pvData, dataTypeSize);
	return node;
}
void XRBTree_init(XRBTreeNode* this_root, size_t treeNodeSize, const char* pvData, const size_t dataTypeSize)
{
	if (this_root == NULL)
		return;
	XBTreeNode_init(this_root, treeNodeSize,pvData, dataTypeSize);
	XRBTree_SetRed(this_root);
}
//当前节点的父节点是红色，且当前节点的祖父节点的另一一个子节点(叔叔节点)也是红色
static bool XRBTree_AdjustNoOne(XRBTreeNode** currentNode, XRBTreeNode* LPpater, XRBTreeNode* LPgrandpa, XRBTreeNode* LPuncle)
{
	if (LPuncle != NULL && XRBTree_IsRed(LPuncle))//叔叔节点是红色
	{
		XRBTree_SetBlack(LPpater);
		XRBTree_SetBlack(LPuncle);
		XRBTree_SetRed(LPgrandpa);
		*currentNode = LPgrandpa;
		return true;
	}
	return false;
}
/*                                                  删除函数                                          */
/**
 * @brief 删除节点后的平衡修复函数
 *
 * 此函数用于在删除一个黑色节点后，修复可能被破坏的红黑树性质。
 * 它通过检查被删除节点的兄弟节点及其子节点的颜色，执行一系列旋转和重新着色操作，
 * 将“缺少一个黑色”的问题沿着树向上传递，直到问题被解决或到达根节点。
 *
 * @param this_root 指向红黑树根节点指针的指针。
 * @param nodes 被删除后留下的“额外黑色”所在的节点（可能是 NULL）。
 * @param LPpater nodes 的父节点。
 */
static void eraseAdjustTree(XRBTreeNode** this_root, XRBTreeNode* nodes, XRBTreeNode* LPpater)
{
	XRBTreeNode* LPbrother = NULL;

	// 循环条件：nodes 是黑色（或为 NULL，视为双重黑色）且不是根节点
	while ((nodes == NULL || XRBTree_IsBlack(nodes)) && nodes != *this_root)
	{
		if (XBTreeNode_GetLChild(LPpater) == nodes)
		{
			// 情况 A: nodes 是其父节点的左孩子
			LPbrother = XBTreeNode_GetRChild(LPpater);

			// Case 1: 兄弟是红色
			// 目标：将其转换为兄弟是黑色的情况（Case 2, 3, 4）
			if (LPbrother != NULL && XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinLL(this_root, LPpater); // 对父节点进行左旋

				// 旋转后，原兄弟的左孩子成为新的兄弟，且必为黑色
				LPbrother = XBTreeNode_GetRChild(LPpater);
				// 此处 LPbrother 理论上不应为 NULL，但为了健壮性，后续逻辑会处理
			}

			// 此时 LPbrother 是黑色 (或 NULL)
			if (LPbrother != NULL)
			{
				XRBTreeNode* LPbrotherLChild = XBTreeNode_GetLChild(LPbrother);
				XRBTreeNode* LPbrotherRChild = XBTreeNode_GetRChild(LPbrother);

				// Case 2: 兄弟是黑色，且它的两个孩子都是黑色（或 NULL）
				// 目标：将“双重黑色”问题向上传递给父节点
				if ((LPbrotherLChild == NULL || XRBTree_IsBlack(LPbrotherLChild)) &&
					(LPbrotherRChild == NULL || XRBTree_IsBlack(LPbrotherRChild)))
				{
					XRBTree_SetRed(LPbrother); // 将兄弟设为红色
					// 将“双重黑色”问题向上传递
					nodes = LPpater;
					LPpater = XBTreeNode_GetParent(nodes); // <-- 关键：更新父节点！
					// 如果 nodes 现在是根，循环将在下一次检查时退出
				}
				else
				{
					// Case 3: 兄弟是黑色，左孩子是红色，右孩子是黑色
					// 目标：将其转换为 Case 4
					if (LPbrotherRChild == NULL || XRBTree_IsBlack(LPbrotherRChild))
					{
						if (LPbrotherLChild != NULL)
							XRBTree_SetBlack(LPbrotherLChild);
						XRBTree_SetRed(LPbrother);
						XBTree_SpinRR(this_root, LPbrother); // 对兄弟进行右旋

						// 更新兄弟为原兄弟的右孩子（现在是红色）
						LPbrother = XBTreeNode_GetRChild(LPpater);
						// 此时 LPbrother 不应为 NULL，且其右孩子为红色
					}

					// Case 4: 兄弟是黑色，右孩子是红色
					// 目标：通过一次旋转和变色彻底解决问题
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					if (LPbrother != NULL) {
						XRBTreeNode* temp = XBTreeNode_GetRChild(LPbrother);
						if (temp != NULL)
							XRBTree_SetBlack(temp);
					}
					XBTree_SpinLL(this_root, LPpater); // 对父节点进行左旋

					// 修复完成，将 nodes 设为根以退出循环
					nodes = *this_root;
				}
			}
			else
			{
				// 兄弟节点为 NULL，这通常意味着树结构已损坏，
				// 但按照算法逻辑，应将“双重黑色”问题向上传递
				nodes = LPpater;
				LPpater = XBTreeNode_GetParent(nodes); // <-- 关键：更新父节点！
			}
		}
		else
		{
			// 情况 B: nodes 是其父节点的右孩子 (与情况 A 对称)
			LPbrother = XBTreeNode_GetLChild(LPpater);

			// Case 1: 兄弟是红色
			if (LPbrother != NULL && XRBTree_IsRed(LPbrother))
			{
				XRBTree_SetBlack(LPbrother);
				XRBTree_SetRed(LPpater);
				XBTree_SpinRR(this_root, LPpater); // 对父节点进行右旋

				LPbrother = XBTreeNode_GetLChild(LPpater);
			}

			if (LPbrother != NULL)
			{
				XRBTreeNode* LPbrotherLChild = XBTreeNode_GetLChild(LPbrother);
				XRBTreeNode* LPbrotherRChild = XBTreeNode_GetRChild(LPbrother);

				// Case 2: 兄弟是黑色，且它的两个孩子都是黑色
				if ((LPbrotherLChild == NULL || XRBTree_IsBlack(LPbrotherLChild)) &&
					(LPbrotherRChild == NULL || XRBTree_IsBlack(LPbrotherRChild)))
				{
					XRBTree_SetRed(LPbrother);
					nodes = LPpater;
					LPpater = XBTreeNode_GetParent(nodes); // <-- 关键：更新父节点！
				}
				else
				{
					// Case 3: 兄弟是黑色，右孩子是红色，左孩子是黑色
					if (LPbrotherLChild == NULL || XRBTree_IsBlack(LPbrotherLChild))
					{
						if (LPbrotherRChild != NULL)
							XRBTree_SetBlack(LPbrotherRChild);
						XRBTree_SetRed(LPbrother);
						XBTree_SpinLL(this_root, LPbrother); // 对兄弟进行左旋

						LPbrother = XBTreeNode_GetLChild(LPpater);
					}

					// Case 4: 兄弟是黑色，左孩子是红色
					XRBTree_SetColor(LPbrother, XRBTree_GetColor(LPpater));
					XRBTree_SetBlack(LPpater);
					if (LPbrother != NULL) {
						XRBTreeNode* temp = XBTreeNode_GetLChild(LPbrother);
						if (temp != NULL)
							XRBTree_SetBlack(temp);
					}
					XBTree_SpinRR(this_root, LPpater); // 对父节点进行右旋

					nodes = *this_root;
				}
			}
			else
			{
				nodes = LPpater;
				LPpater = XBTreeNode_GetParent(nodes); // <-- 关键：更新父节点！
			}
		}
	}

	// 循环结束后，确保当前节点为黑色
	if (nodes != NULL)
	{
		XRBTree_SetBlack(nodes);
	}
}
//删除的是有一个孩子
static void OneChild_erase(XRBTreeNode** this_root, XRBTreeNode* eraseNode)
{
	XRBTreeNode* Pchild = XBTreeNode_GetLChild(eraseNode);//孩子节点
	if (Pchild == NULL)
	{
		Pchild = XBTreeNode_GetRChild(eraseNode);
	}
	XRBTreeNode* parent = XBTreeNode_GetParent(eraseNode);//获取父节点

	enum XRBTreeColor color = XRBTree_GetColor(eraseNode);//颜色
	if (parent != NULL)
	{
		XRBTreeNode** LPpaterToEraseNode = XTreeNode_getChildrenParentRef(eraseNode);//孩子在父节点位置
		*LPpaterToEraseNode = Pchild;
	}
	else
	{
		//删除节点为根节点
		*this_root = Pchild;
	}
	if (Pchild != NULL)
		XBTreeNode_SetParent(Pchild, parent);
	/*if (method)
		method(XTreeNode_GetDataPtr(eraseNode),args);
	XTreeNode_delete(eraseNode);*/
	if (color == XRBTreeBlack && *this_root != NULL)
	{
		//调整树：
		eraseAdjustTree(this_root, Pchild, parent);
	}

}
//删除的是有两个孩子
static XRBTreeNode* TwoChild_erase(XRBTreeNode** this_root, XRBTreeNode* eraseNode,size_t dataSize)
{
#if XVector_ON
	XRBTreeNode* LPreplace = NULL; // 后继节点（用于替换）

	// 1. 找到后继节点 (右子树中的最左节点)
	LPreplace = XBTreeNode_GetRChild(eraseNode);
	while (XBTreeNode_GetLChild(LPreplace) != NULL)
	{
		LPreplace = XBTreeNode_GetLChild(LPreplace);
	}

	// 2. === 关键修复：直接将 eraseNode 的数据指针替换为 LPreplace 的数据指针 ===
	//    注意：这里只交换数据指针，不交换颜色！
	//size_t dataSize = ((XTreeNode*)eraseNode)->dataSize;
	if (dataSize > 0) 
	{
		char* tempBuffer = (char*)XMalloc_System(dataSize); // 创建临时缓冲区
		if (tempBuffer == NULL) {
			// 处理内存分配失败的情况，例如直接返回或采取其他措施
			return;
		}
		memcpy(tempBuffer, XTreeNode_GetDataPtr(eraseNode), dataSize);
		memcpy(XTreeNode_GetDataPtr(eraseNode), XTreeNode_GetDataPtr(LPreplace), dataSize);
		memcpy(XTreeNode_GetDataPtr(LPreplace), tempBuffer, dataSize);
		XFree_System(tempBuffer); // 释放临时缓冲区
	}
	//void* tempData = XTreeNode_GetDataPtr(eraseNode);
	//memcpy();
	//XTreeNode_SetDataPtr(eraseNode, XTreeNode_GetDataPtr(LPreplace));
	//XTreeNode_SetDataPtr(LPreplace, tempData);

	// 3. === 核心思想：现在，LPreplace 节点持有需要被清理的旧数据 ===
	//    我们现在要做的就是像删除一个普通节点一样删除 LPreplace。
	//    由于 LPreplace 是后继节点，它最多只有一个右孩子，所以可以直接调用 OneChild_erase。
	OneChild_erase(this_root, LPreplace);
	return LPreplace;

#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
XRBTreeNode* XRBTree_remove(XRBTreeNode** this_root, XCompare compare, XCompareRuleOne Rule, const void* pvData, const size_t dataSize)
{
	if (!this_root||!compare||!Rule||!pvData|| !dataSize)
		return NULL;
	XRBTreeNode* findErase = XBBTree_findNode(*this_root, compare, Rule, pvData);//删除的节点
	//DEBUG_PRINTF("findErase=%p", findErase);
	if (findErase == NULL)
		return NULL;//要删除的节点没找到
	return XRBTree_removeNode(this_root, findErase, dataSize);	
}

XRBTreeNode* XRBTree_removeNode(XRBTreeNode** this_root, const XRBTreeNode* findErase, const size_t dataSize)
{
	if (!this_root || !findErase || !dataSize)
		return NULL;
	if (findErase == NULL)
		return NULL;//要删除的节点没找到
	size_t count = 0;
	if (XBTreeNode_GetLChild(findErase) != NULL)
		++count;
	if (XBTreeNode_GetRChild(findErase) != NULL)
		++count;
	if (count == 2) // 两个孩子
	{
		return TwoChild_erase(this_root, findErase, dataSize);
	}
	else // 零个或一个孩子
	{
		OneChild_erase(this_root, findErase);
	}
	return findErase;
}

XRBTreeNode* XRBTree_findNode(XRBTreeNode* this_root, XCompare compare, XCompareRuleOne rule, void* pvData)
{
	return XBBTree_findNode(this_root, compare, rule,pvData);
	//if (this_root == NULL)//树是空的
	//	return NULL;
	//XRBTreeNode* CurNode = this_root;//当前节点指针
	//while (CurNode != NULL)
	//{
	//	if (equalityRule(compare, CurNode, pvData))
	//	{
	//		return CurNode;
	//	}
	//	else if (equalityRule(less, CurNode, pvData))
	//	{
	//		CurNode = XBTreeNode_GetRChild(CurNode);
	//	}
	//	else
	//	{
	//		CurNode = XBTreeNode_GetLChild(CurNode);
	//	}
	//}
	//return NULL;
}

XRBTreeNode* XRBTree_findSuccessor(XRBTreeNode* node)
{
	if (!node) return NULL;

	// 情况1: 如果有右子树，后继是右子树的最左节点
	if (XBTreeNode_GetRChild(node) != NULL) {
		XRBTreeNode* successor = (XRBTreeNode*)XBTreeNode_GetRChild(node);
		while (XBTreeNode_GetLChild(successor) != NULL) {
			successor = (XRBTreeNode*)XBTreeNode_GetLChild(successor);
		}
		return successor;
	}

	// 情况2: 没有右子树，需要向上回溯
	XRBTreeNode* current = node;
	XRBTreeNode* parent = (XRBTreeNode*)XBTreeNode_GetParent(current);

	// 回溯直到找到一个节点，它是其父节点的左孩子
	while (parent != NULL && current == XBTreeNode_GetRChild(parent)) {
		current = parent;
		parent = (XRBTreeNode*)XBTreeNode_GetParent(parent);
	}

	// 此时，parent 就是后继节点（如果存在的话）
	return parent;
}
 
/*                                                  插入函数                                           */
//调整成为红黑树
static void XRBTree_insertAdjust(XRBTreeNode** this_root, XRBTreeNode* currentNode)
{
	XRBTreeNode* LPpater = NULL;    // 父节点
	XRBTreeNode* LPgrandpa = NULL;  // 祖父节点
	XRBTreeNode* LPuncle = NULL;    // 叔叔节点

	// 循环：只要当前节点有父节点，且父节点是红色，就需要调整
	while ((LPpater = XBTreeNode_GetParent(currentNode)) && XRBTree_IsRed(LPpater))
	{
		LPgrandpa = XBTreeNode_GetParent(LPpater);
		// --- 新增：安全检查 ---
		if (LPgrandpa == NULL) {
			// 父节点是根节点，这不应该发生，因为根必须是黑色。
			// 但为了安全，直接将父节点设为黑色并退出。
			XRBTree_SetBlack(LPpater);
			break;
		}

		if (LPpater == XBTreeNode_GetLChild(LPgrandpa)) // 父节点是祖父的左孩子
		{
			LPuncle = XBTreeNode_GetRChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle)) // 叔叔节点是红色
				continue;

			// Case 2: 当前节点是其父节点的右孩子
			if (currentNode == XBTreeNode_GetRChild(LPpater))
			{
				currentNode = LPpater;
				XBTree_SpinLL(this_root, LPpater); // 注意：旋转后 LPpater 已改变
				// 更新指针以反映旋转后的结构
				LPpater = XBTreeNode_GetParent(currentNode);
				LPgrandpa = XBTreeNode_GetParent(LPpater);
			}

			// Case 3: 当前节点是其父节点的左孩子
			if (LPpater && LPgrandpa) { // 再次检查
				XRBTree_SetBlack(LPpater);
				XRBTree_SetRed(LPgrandpa);
				XBTree_SpinRR(this_root, LPgrandpa);
			}
		}
		else // 父节点是祖父的右孩子
		{
			LPuncle = XBTreeNode_GetLChild(LPgrandpa);
			if (XRBTree_AdjustNoOne(&currentNode, LPpater, LPgrandpa, LPuncle)) // 叔叔节点是红色
				continue;

			// Case 2: 当前节点是其父节点的左孩子
			if (currentNode == XBTreeNode_GetLChild(LPpater))
			{
				currentNode = LPpater;
				XBTree_SpinRR(this_root, LPpater);
				// 更新指针
				LPpater = XBTreeNode_GetParent(currentNode);
				LPgrandpa = XBTreeNode_GetParent(LPpater);
			}

			// Case 3: 当前节点是其父节点的右孩子 <-- 修正注释
			if (LPpater && LPgrandpa) {
				XRBTree_SetBlack(LPpater);
				XRBTree_SetRed(LPgrandpa);
				XBTree_SpinLL(this_root, LPgrandpa);
			}
		}
	}

	// 确保根节点始终是黑色
	if (*this_root != NULL) {
		XRBTree_SetBlack(*this_root);
	}
}
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XCompare compare, XCompareRuleTwo lessRule, const void* pvData, const size_t dataSize)
{
	//DEBUG_PRINTF("less=%p pvData=%p dataTypeSize=%u\n",less,pvData,dataTypeSize);
	if (ISNULL(compare, ""))
		return NULL;
	if (ISNULL(pvData, ""))
		return NULL;
	if (ISNULL(dataSize, ""))
		return NULL;
	XRBTreeNode* nodes = XRBTree_create(pvData, dataSize);//创建一个红黑树节点并且初始化,默认红色
	if (ISNULL(nodes, ""))
		return NULL;
	//DEBUG_PRINTF("nodes=%p\n",nodes);
	XRBTree_insertNode(this_root, compare, lessRule, nodes);
}

XRBTreeNode* XRBTree_insertNode(XRBTreeNode** this_root, XCompare compare, XCompareRuleTwo lessRule, XRBTreeNode* insertNode)
{
	if (!compare || !lessRule || !insertNode)return;
	bool flag = XBBTree_insertAlign(this_root, insertNode, compare, lessRule, NULL, 0);//将数据插入到节点，并且链接
	if (!flag)
	{
		printf("节点插入失败\n");
		XTreeNode_delete((XTreeNode*)insertNode); // 
		return NULL;
	}
	if (this_root == NULL)//根节点，无内存开辟
	{
		XRBTree_SetBlack(insertNode);
		return insertNode;
	}
	else if (*this_root == NULL)//根节点为空
	{
		*this_root = insertNode;
		XRBTree_SetBlack(insertNode);
		return insertNode;
	}
	XRBTree_insertAdjust(this_root, insertNode);
	return insertNode;
}


