#ifndef XBINARYTREE_H
#define XBINARYTREE_H

/**
 * @file XBinaryTree.h
 * @brief 二叉树节点、遍历和旋转 API。
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "XTreeObject.h"

/** @brief 二叉树的子节点槽位。 */
enum XBTreeNodeType {
	XBTreeLChild, /**< 左孩子。 */
	XBTreeRChild  /**< 右孩子。 */
};

/** @brief 二叉树遍历顺序。 */
enum XBTreeTraversing {
	XBTreePreorder,  /**< 前序：根、左子树、右子树。 */
	XBTreeInorder,   /**< 中序：左子树、根、右子树。 */
	XBTreePostorder  /**< 后序：左子树、右子树、根。 */
};

/**
 * @brief 二叉树节点。
 * @details m_class 必须是第一个成员，以便与 XTreeNode API 兼容。
 */
typedef struct XBTreeNode {
	XTreeNode m_class; /**< 二叉树公共节点部分。 */
} XBTreeNode;

/**
 * @brief 获取二叉树节点头部大小。
 * @return 节点对象和两个子节点指针槽位的总字节数，不含用户数据。
 */
size_t XBTreeNode_typeSize();

/**
 * @brief 使用默认内存管理器创建二叉树节点。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param typeSize 用户数据字节数，必须大于 0。
 * @return 成功返回新节点，参数无效或分配失败返回 NULL。
 */
XBTreeNode* XBTreeNode_create(const char* pvData,const size_t typeSize);

/**
 * @brief 初始化调用者提供的二叉树节点存储。
 * @param node 待初始化的节点内存。
 * @param treeNodeSize 节点头部大小，通常取 XBTreeNode_typeSize 的结果。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param typeSize 用户数据字节数，必须大于 0。
 * @note node 指向的内存必须至少能容纳 treeNodeSize + typeSize 字节。
 */
void XBTreeNode_init(XBTreeNode* node, size_t treeNodeSize, const char* pvData, const size_t typeSize);

/**
 * @brief 按指定顺序遍历二叉树并返回节点指针向量。
 * @param this_root 遍历起点；不能为 NULL。
 * @param Traversing 遍历顺序。
 * @param buffer 可选的目标向量；为 NULL 时函数创建新向量，非 NULL 时清空并复用。
 * @return 节点指针向量；参数无效、遍历方式未知或分配失败时返回 NULL。
 * @note 返回的新向量由调用者释放；buffer 非 NULL 时返回同一对象。
 */
XVector* XBTree_TraversingToXVector(XTreeNode* this_root, const enum XBTreeTraversing Traversing, XVector*buffer);

/**
 * @brief 对指定节点执行右旋。
 * @param this_root 指向树根指针的地址；根发生变化时会被更新。
 * @param nodes 待旋转节点，必须存在左孩子。
 * @return 旋转后成为该子树根的新节点；参数无效或无法旋转时返回 NULL。
 */
XTreeNode* XBTree_SpinRR(XTreeNode** this_root, XTreeNode* nodes);

/**
 * @brief 对指定节点执行左旋。
 * @param this_root 指向树根指针的地址；根发生变化时会被更新。
 * @param nodes 待旋转节点，必须存在右孩子。
 * @return 旋转后成为该子树根的新节点；参数无效或无法旋转时返回 NULL。
 */
XTreeNode* XBTree_SpinLL(XTreeNode** this_root, XTreeNode* nodes);

/**
 * @brief 获取二叉树节点的父节点。
 * @param this_root 目标二叉树节点。
 * @return 父节点指针；根节点返回 NULL。
 */
#define XBTreeNode_GetParent(this_root) XTreeNode_GetParent(this_root)//二叉树-获取父节点(继承的子类均可以使用)
/**
 * @brief 获取二叉树节点的左孩子。
 * @param this_root 目标二叉树节点。
 * @return 左孩子指针；没有左孩子时返回 NULL。
 */
#define XBTreeNode_GetLChild(this_root) XTreeNode_GetChild(this_root,XBTreeLChild)//二叉树-获取左孩子(继承的子类均可以使用)
/**
 * @brief 获取二叉树节点的右孩子。
 * @param this_root 目标二叉树节点。
 * @return 右孩子指针；没有右孩子时返回 NULL。
 */
#define XBTreeNode_GetRChild(this_root) XTreeNode_GetChild(this_root,XBTreeRChild)//二叉树-获取右孩子(继承的子类均可以使用)

/**
 * @brief 设置二叉树节点的父节点。
 * @param this_root 目标二叉树节点。
 * @param node 新父节点，可为 NULL。
 */
#define XBTreeNode_SetParent(this_root,node) XTreeNode_SetParent(this_root,node)//二叉树-设置父节点(继承的子类均可以使用)
/**
 * @brief 设置二叉树节点的左孩子。
 * @param this_root 目标二叉树节点。
 * @param node 新左孩子，可为 NULL。
 */
#define XBTreeNode_SetLChild(this_root,node) XTreeNode_SetChild(this_root,XBTreeLChild,node)//二叉树-设置左孩子(继承的子类均可以使用)
/**
 * @brief 设置二叉树节点的右孩子。
 * @param this_root 目标二叉树节点。
 * @param node 新右孩子，可为 NULL。
 */
#define XBTreeNode_SetRChild(this_root,node) XTreeNode_SetChild(this_root,XBTreeRChild,node)//二叉树-设置右孩子(继承的子类均可以使用)

/** @brief 获取二叉树节点用户数据地址的宏别名。 */
#define XBTreeNode_GetDataPtr						XTreeNode_GetDataPtr
/** @brief 按指定类型读取二叉树节点用户数据的宏别名。 */
#define XBTreeNode_GetData							XTreeNode_GetData

/** @brief 释放单个二叉树节点的宏别名。 */
#define XBTreeNode_delete							XTreeNode_delete

/**
 * @brief 递归释放二叉树，并调用用户数据回调。
 * @param this_root 二叉树根节点。
 * @param method 用户数据释放回调，可为 NULL。
 * @param args 传递给 method 的上下文参数。
 * @param memory 节点释放所使用的内存管理器。
 */
#define XBTree_delete(this_root,method,args,memory)		XTree_delete_base(this_root,XBTreeNode_delete,method,args,memory)

#ifdef __cplusplus
}
#endif

#endif /* XBINARYTREE_H */
