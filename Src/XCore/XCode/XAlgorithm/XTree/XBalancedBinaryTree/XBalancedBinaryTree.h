//平衡二叉树
#ifndef XBALANCEDBINARYTREE_H
#define XBALANCEDBINARYTREE_H

/**
 * @file XBalancedBinaryTree.h
 * @brief AVL 风格平衡二叉树 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XBinaryTree.h"
#include "XFunctionCallback.h"

/**
 * @brief 平衡二叉树节点。
 * @details maxLayer 保存从当前节点向下的最大层数，根节点初始层数为 1。
 */
typedef struct XBBTreeNode {
	XBTreeNode XBTNode; /**< 普通二叉树节点部分。 */
	size_t maxLayer;   /**< 左右子树高度中的较大值加 1。 */
} XBBTreeNode;

/**
 * @brief 获取平衡二叉树节点头部大小。
 * @return 节点对象、两个子节点槽位和扩展字段的总字节数，不含用户数据。
 */
size_t XBBTreeNode_typeSize();

/**
 * @brief 使用默认内存管理器创建平衡二叉树节点。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param TypeSize 用户数据字节数。
 * @return 成功返回新节点，参数无效或分配失败返回 NULL。
 */
XBBTreeNode* XBBTree_create(const char* pvData, const size_t TypeSize);

/**
 * @brief 使用指定内存管理器创建平衡二叉树节点。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param TypeSize 用户数据字节数。
 * @param memory 内存管理器；为 NULL 时使用默认管理器。
 * @return 成功返回新节点，参数无效、管理器不可用或分配失败返回 NULL。
 */
XBBTreeNode* XBBTree_create_ex(const char* pvData, const size_t TypeSize, XMemory* memory);

/**
 * @brief 将已有节点按比较规则插入平衡二叉树。
 * @param this_root 指向树根指针的地址；空树插入成功后会写入根节点。
 * @param insertNode 待插入的节点；失败时由函数按 memory 释放。
 * @param compare 基础对象比较器。
 * @param lessRule 决定左右子树方向的双对象比较规则。
 * @param pvData 要复制到 insertNode 的用户数据；为 NULL 时保留节点现有数据。
 * @param dataSize 要复制的用户数据字节数。
 * @param memory 删除失败节点时使用的内存管理器。
 * @return 插入成功返回 true；参数无效、比较失败或数据写入失败返回 false。
 * @note 函数会更新高度并执行必要的旋转。
 */
bool XBBTree_insertAlign(XBBTreeNode** this_root, XBBTreeNode* insertNode, XCompare compare, XCompareRuleTwo lessRule, const void* pvData, const size_t dataSize, XMemory* memory);

/**
 * @brief 创建节点、插入数据并自动维护平衡。
 * @param this_root 指向树根指针的地址。
 * @param compare 基础对象比较器。
 * @param lessRule 决定左右子树方向的双对象比较规则。
 * @param pvData 要复制到新节点的用户数据。
 * @param dataSize 用户数据字节数。
 * @param memory 节点分配和释放使用的内存管理器；为 NULL 时使用默认管理器。
 * @return 成功返回插入的节点，参数无效或分配/插入失败返回 NULL。
 */
XBBTreeNode* XBBTree_insert(XBBTreeNode** this_root, XCompare compare, XCompareRuleTwo lessRule, const void* pvData, const size_t dataSize, XMemory* memory);

/**
 * @brief 按比较规则删除平衡二叉树中的节点。
 * @param this_root 指向树根指针的地址。
 * @param compare 基础对象比较器。
 * @param Rule 根据当前节点数据和目标值进行比较的单对象规则。
 * @param pvData 待删除节点的数据。
 * @param dataSize 节点数据字节数；删除双子节点时用于复制替代数据。
 * @param memory 释放节点使用的内存管理器。
 * @return 当前实现保留 void* 返回类型，删除成功或失败均返回 NULL。
 */
void* XBBTree_erase(XBBTreeNode** this_root, XCompare compare, XCompareRuleOne Rule, const void* pvData, size_t dataSize, XMemory* memory);

/**
 * @brief 查找与给定数据匹配的平衡二叉树节点。
 * @param this_root 树根节点。
 * @param compare 基础对象比较器。
 * @param rule 单对象查找比较规则。
 * @param pvData 待查找数据。
 * @return 匹配节点指针；树为空、参数无效或未找到时返回 NULL。
 */
XBBTreeNode* XBBTree_findNode(XBBTreeNode* this_root, XCompare compare, XCompareRuleOne rule, void* pvData);

/**
 * @brief 获取节点自身记录的层数。
 * @param this_root 目标节点。
 * @return 节点层数；节点为空时返回 0。
 */
const size_t XBBTree_GetLayerNumberThis(const XBBTreeNode* this_root);

/**
 * @brief 获取左右子树中记录的最大层数。
 * @param this_root 目标节点。
 * @return 左右子树最大层数；节点为空或无子节点时返回 0。
 */
const size_t XBBTree_GetLayerNumberChild(const XBBTreeNode* this_root);

/**
 * @brief 根据子树高度更新当前节点层数。
 * @param this_root 要更新的节点。
 * @return 更新后的节点层数；节点为空时返回 0。
 */
const size_t XBBTree_SetLayerNumberThis(XBBTreeNode* this_root);

/**
 * @brief 从指定节点向上更新至树根的层数。
 * @param this_root 指向树根指针的地址。
 * @param currentNode 开始更新的节点。
 * @return 更新后的根节点层数；参数无效时返回 0。
 */
const size_t XBBTree_SetLayerNumberAll(XBBTreeNode** this_root, XBBTreeNode* currentNode);

/**
 * @brief 根据当前节点的平衡情况选择并执行旋转。
 * @param this_root 指向树根指针的地址。
 * @param nodes 待检查和旋转的节点。
 * @return 旋转后的子树根；无需旋转或参数无效时返回原节点或 NULL。
 */
XBBTreeNode* XBBTree_Spin(XBBTreeNode** this_root, XBBTreeNode* nodes);

/**
 * @brief 对节点执行右旋。
 * @param this_root 指向树根指针的地址。
 * @param nodes 待旋转节点，必须存在左孩子。
 * @return 旋转后的子树根；无法旋转时返回 NULL。
 */
XBBTreeNode* XBBTree_SpinRR(XBBTreeNode** this_root, XBBTreeNode* nodes);

/**
 * @brief 对节点执行左旋。
 * @param this_root 指向树根指针的地址。
 * @param nodes 待旋转节点，必须存在右孩子。
 * @return 旋转后的子树根；无法旋转时返回 NULL。
 */
XBBTreeNode* XBBTree_SpinLL(XBBTreeNode** this_root, XBBTreeNode* nodes);

/**
 * @brief 对节点执行右左双旋。
 * @param this_root 指向树根指针的地址。
 * @param nodes 待旋转节点。
 * @return 旋转后的子树根；无法旋转时返回 NULL。
 */
XBBTreeNode* XBBTree_SpinRL(XBBTreeNode** this_root, XBBTreeNode* nodes);

/**
 * @brief 对节点执行左右双旋。
 * @param this_root 指向树根指针的地址。
 * @param nodes 待旋转节点。
 * @return 旋转后的子树根；无法旋转时返回 NULL。
 */
XBBTreeNode* XBBTree_SpinLR(XBBTreeNode** this_root, XBBTreeNode* nodes);

#ifdef __cplusplus
}
#endif

#endif /* XBALANCEDBINARYTREE_H */
