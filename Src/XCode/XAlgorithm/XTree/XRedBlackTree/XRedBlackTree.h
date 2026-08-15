#ifndef XREDBLACKTREE_H
#define XREDBLACKTREE_H

/**
 * @file XRedBlackTree.h
 * @brief 红黑树节点、插入、删除和查找 API。
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "XBinaryTree.h"
#include"XFunctionCallback.h"

/**
 * @brief 获取红黑树节点颜色。
 * @param this_root 目标红黑树节点。
 * @return XRBTreeColor 枚举值。
 */
#define XRBTree_GetColor(this_root)				(((XRBTreeNode*)this_root)->color)//红黑树-获取颜色
/**
 * @brief 设置红黑树节点颜色。
 * @param this_root 目标红黑树节点。
 * @param Color 要设置的 XRBTreeColor 枚举值。
 */
#define XRBTree_SetColor(this_root,Color)		(((XRBTreeNode*)this_root)->color=Color)//红黑树-设置颜色
/**
 * @brief 将红黑树节点设置为黑色。
 * @param this_root 目标红黑树节点。
 */
#define XRBTree_SetBlack(this_root)				(((XRBTreeNode*)this_root)->color=XRBTreeBlack)//红黑树-设置颜色黑色
/**
 * @brief 将红黑树节点设置为红色。
 * @param this_root 目标红黑树节点。
 */
#define XRBTree_SetRed(this_root)				(((XRBTreeNode*)this_root)->color=XRBTreeRed)//红黑树-设置颜色红色
/**
 * @brief 判断红黑树节点是否为黑色。
 * @param this_root 目标红黑树节点。
 * @return 节点为黑色返回非 0，否则返回 0。
 */
#define XRBTree_IsBlack(this_root)				(((XRBTreeNode*)this_root)->color==XRBTreeBlack)//红黑树-是否是黑色
/**
 * @brief 判断红黑树节点是否为红色。
 * @param this_root 目标红黑树节点。
 * @return 节点为红色返回非 0，否则返回 0。
 */
#define XRBTree_IsRed(this_root)				(((XRBTreeNode*)this_root)->color==XRBTreeRed)//红黑树-是否是红色
/**
 * @brief 按指定类型读取红黑树节点用户数据。
 * @param this_root 目标红黑树节点。
 * @param Type 用户数据类型。
 * @return 用户数据的左值，可读写。
 */
#define XRBTree_GetData(this_root,Type)			XBTreeNode_GetData(this_root,Type)//红黑树-获取数据
/**
 * @brief 获取红黑树节点用户数据地址。
 * @param this_root 目标红黑树节点。
 * @return 用户数据首地址。
 */
#define XRBTree_getData(this_root)				XTreeNode_getData(this_root)//红黑树-获取数据指针

/** @brief 红黑树节点颜色。 */
enum XRBTreeColor {
	XRBTreeRed,   /**< 红色。 */
	XRBTreeBlack  /**< 黑色。 */
};

/**
 * @brief 红黑树节点。
 * @details XBTNode 必须是第一个成员，以便复用二叉树节点 API。
 */
typedef struct XRBTreeNode {
	XBTreeNode XBTNode; /**< 普通二叉树节点部分。 */
	char color;        /**< 当前节点颜色，取 XRBTreeColor。 */
} XRBTreeNode;

/**
 * @brief 获取红黑树节点头部大小。
 * @return 节点对象、子节点槽位和颜色字段的总字节数，不含用户数据。
 */
size_t XRBTree_typeSize();
/**
 * @brief 使用默认内存管理器创建红黑树节点。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataTypeSize 用户数据字节数。
 * @return 成功返回新节点，分配失败返回 NULL。
 */
XRBTreeNode* XRBTree_create(const char* pvData,const size_t dataTypeSize);

/**
 * @brief 使用指定内存管理器创建红黑树节点。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataTypeSize 用户数据字节数。
 * @param memory 内存管理器；为 NULL 时使用默认管理器。
 * @return 成功返回新节点，管理器不可用或分配失败返回 NULL。
 */
XRBTreeNode* XRBTree_create_ex(const char* pvData, const size_t dataTypeSize,
	XMemory* memory);

/**
 * @brief 初始化调用者提供的红黑树节点。
 * @param this_root 待初始化的节点内存。
 * @param treeNodeSize 节点头部大小，通常取 XRBTree_typeSize 的结果。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataTypeSize 用户数据字节数。
 * @note 新节点初始颜色为红色。
 */
void XRBTree_init(XRBTreeNode* this_root, size_t treeNodeSize, const char* pvData, const size_t dataTypeSize);

/**
 * @brief 创建节点、插入数据并维护红黑树平衡。
 * @param this_root 指向树根指针的地址。
 * @param compare 基础对象比较器。
 * @param lessRule 决定左右子树方向的双对象比较规则。
 * @param pvData 要复制到新节点的用户数据。
 * @param dataSize 用户数据字节数。
 * @param memory 节点分配和释放使用的内存管理器；为 NULL 时使用默认管理器。
 * @return 成功返回插入节点，参数无效或插入失败返回 NULL。
 */
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XCompare compare, XCompareRuleTwo lessRule,
	const void* pvData, const size_t dataSize, XMemory* memory);

/**
 * @brief 将已有红黑树节点插入树中并维护平衡。
 * @param this_root 指向树根指针的地址。
 * @param compare 基础对象比较器。
 * @param lessRule 决定左右子树方向的双对象比较规则。
 * @param insertNode 待插入的已初始化节点。
 * @param memory 插入失败时释放节点使用的内存管理器。
 * @return 成功返回插入节点，失败返回 NULL。
 */
XRBTreeNode* XRBTree_insertNode(XRBTreeNode** this_root, XCompare compare, XCompareRuleTwo lessRule,
	XRBTreeNode* insertNode, XMemory* memory);
/**
 * @brief 按数据查找并解除一个红黑树节点的关系。
 * @param this_root 指向树根指针的地址。
 * @param compare 基础对象比较器。
 * @param Rule 单对象查找比较规则。
 * @param pvData 待删除节点的数据。
 * @param dataSize 用户数据字节数；双子节点删除时用于交换数据。
 * @param memory 删除过程使用的内存管理器。
 * @return 已从树中解除关系、但尚未释放的节点；未找到或参数无效返回 NULL。
 * @note 调用者应处理返回节点的生命周期，通常随后调用 XRBTreeNode_delete。
 */
XRBTreeNode* XRBTree_remove(XRBTreeNode** this_root, XCompare compare, XCompareRuleOne Rule,
	const void* pvData, const size_t dataSize, XMemory* memory);

/**
 * @brief 按节点指针解除红黑树节点关系，但不释放该节点。
 * @param this_root 指向树根指针的地址。
 * @param node 要删除的节点。
 * @param dataSize 用户数据字节数；双子节点删除时用于交换数据。
 * @param memory 删除过程需要的内存管理器。
 * @return 已解除关系、等待调用者释放的节点；参数无效返回 NULL。
 */
XRBTreeNode* XRBTree_removeNode(XRBTreeNode** this_root, const XRBTreeNode*node,
	const size_t dataSize, XMemory* memory);

/**
 * @brief 查找与给定数据匹配的红黑树节点。
 * @param this_root 树根节点。
 * @param compare 基础对象比较器。
 * @param rule 单对象查找比较规则。
 * @param pvData 待查找数据。
 * @return 匹配节点指针；树为空或未找到时返回 NULL。
 */
XRBTreeNode* XRBTree_findNode(XRBTreeNode* this_root, XCompare compare, XCompareRuleOne rule, void* pvData);
/**
 * @brief 查找给定节点的中序后继节点。
 *
 * @param node 要查找后继的节点。
 * @return XRBTreeNode* 中序后继节点；不存在或 node 为 NULL 时返回 NULL。
 */
XRBTreeNode* XRBTree_findSuccessor(XRBTreeNode* node);
/**
 * @brief 释放一个红黑树节点。
 * @param node 要释放的节点，以公共 XTreeNode 指针传入。
 * @param memory 释放节点使用的内存管理器；为 NULL 时使用默认管理器。
 */
void XRBTreeNode_delete(XTreeNode* node, XMemory* memory);

/**
 * @brief 递归释放红黑树，并调用用户数据释放回调。
 * @param this_root 红黑树根节点。
 * @param method 用户数据释放回调，可为 NULL。
 * @param args 传递给 method 的上下文参数。
 * @param memory 节点释放所使用的内存管理器。
 */
#define XRBTree_delete(this_root,method,args,memory)		XTree_delete_base(this_root,XRBTreeNode_delete,method,args,memory)
#ifdef __cplusplus
}
#endif
#endif /* XREDBLACKTREE_H */
