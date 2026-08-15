#ifndef XHIERARCHICALTREE_H
#define XHIERARCHICALTREE_H

/**
 * @file XHierarchicalTree.h
 * @brief 使用“首孩子/下一个兄弟”表示法的层次树 API。
 */

#ifdef __cplusplus
extern "C" {
#endif
#include "XTreeObject.h"

/** @brief 层次树的子节点槽位。 */
enum XHTreeNodeType {
	XHTreeFirstChild,  /**< 第一个子节点指针。 */
	XHTreeNextSibling  /**< 下一个兄弟节点指针。 */
};

/**
 * @brief 层次树节点。
 * @details parent 是公共节点部分；子节点通过 first child/next sibling 链接。
 */
typedef struct XHTreeNode {
	XTreeNode parent; /**< 公共树节点部分。 */
} XHTreeNode;

/**
 * @brief 获取层次树节点头部大小。
 * @return 节点对象和两个子节点指针槽位的总字节数，不含用户数据。
 */
size_t XHTreeNode_typeSize();

/**
 * @brief 使用默认内存管理器创建层次树节点。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataTypeSize 用户数据字节数，必须大于 0。
 * @return 成功返回新节点，参数无效或分配失败返回 NULL。
 */
XHTreeNode* XHTreeNode_create(const char* pvData, const size_t dataTypeSize);

/**
 * @brief 初始化调用者提供的层次树节点存储。
 * @param node 待初始化的节点内存。
 * @param treeNodeSize 节点头部大小，通常取 XHTreeNode_typeSize 的结果。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataTypeSize 用户数据字节数，必须大于 0。
 * @note node 指向的内存必须至少能容纳 treeNodeSize + dataTypeSize 字节。
 */
void XHTreeNode_init(XHTreeNode* node, size_t treeNodeSize, const char* pvData, const size_t dataTypeSize);
/**
 * @brief 将已有节点追加到父节点的孩子链表末尾。
 * @param parent 父节点。
 * @param child 要追加的子节点。
 * @return 成功建立关系返回 true；任一参数为空返回 false。
 * @note 函数会更新 child 的父指针，但不会自动从其旧父节点脱离。
 */
bool XHTreeNode_addNode(XHTreeNode* parent, XHTreeNode* child);
// 添加子节点
/**
 * @brief 创建并追加一个子节点。
 * @param parent 父节点。
 * @param pvData 子节点初始用户数据；为 NULL 时清零。
 * @param dataTypeSize 用户数据字节数，必须大于 0。
 * @return 成功返回新子节点；参数无效、分配失败或追加失败返回 NULL。
 */
XHTreeNode* XHTreeNode_addChild(XHTreeNode* parent, const char* pvData, const size_t dataTypeSize);
//删除节点(从树中删除节点node)
/**
 * @brief 从父节点脱离并递归释放一个节点及其子树。
 * @param node 要删除的节点。
 * @param method 用户数据释放回调，可为 NULL。
 * @param args 传递给 method 的上下文参数。
 * @param memory 节点释放所使用的内存管理器，可为 NULL。
 * @return 删除成功返回 true；node 为 NULL 时返回 false。
 */
bool XHTreeNode_removeNode(XHTreeNode* node, XTreeNodeDataDeleteMethod method, void* args, XMemory* memory);
//删除子节点(只找儿子不找孙子)
/**
 * @brief 在直接子节点中查找匹配项并删除匹配子树。
 * @param parent 父节点。
 * @param equality 基础相等比较函数。
 * @param rule 将 equality 适配为单对象比较的规则。
 * @param pvData 待匹配用户数据。
 * @param method 用户数据释放回调，可为 NULL。
 * @param args 传递给 method 的上下文参数。
 * @param memory 节点释放所使用的内存管理器，可为 NULL。
 * @return 是否找到并删除匹配的直接子节点。
 * @note 只检查 parent 的直接孩子，不查找孙节点。
 */
bool XHTreeNode_removeChild(XHTreeNode* parent, XEquality equality, XCompareRuleOne rule, const void* pvData, XTreeNodeDataDeleteMethod method, void* args, XMemory* memory);
//查找节点
/**
 * @brief 在父节点的直接孩子中查找匹配用户数据的节点。
 * @param parent 父节点。
 * @param equality 基础相等比较函数。
 * @param rule 将 equality 适配为单对象比较的规则。
 * @param pvData 待查找用户数据。
 * @return 匹配的直接子节点；未找到或 parent 为空时返回 NULL。
 */
XHTreeNode* XHTreeNode_findData(XHTreeNode* parent, XEquality equality, XCompareRuleOne rule, void* pvData);
// 前序遍历（根-子树）
/**
 * @brief 以缩进形式递归打印层次结构。
 * @param this_root 要打印的子树根节点。
 * @param depth 当前缩进层数，通常根节点传 0。
 * @note 当前实现打印层次缩进，不打印用户数据内容。
 */
void XHTree_print(XHTreeNode* this_root, int depth);
/**
 * @brief 释放单个层次树节点的宏别名，不释放用户数据。
 * @param node 要释放的节点。
 * @param memory 释放节点使用的内存管理器；为 NULL 时使用默认管理器。
 */
#define XHTreeNode_delete								XTreeNode_delete

/**
 * @brief 递归释放层次树，并调用用户数据回调。
 * @param this_root 层次树根节点。
 * @param method 用户数据释放回调，可为 NULL。
 * @param args 传递给 method 的上下文参数。
 * @param memory 节点释放所使用的内存管理器。
 */
#define XHTree_delete(this_root,method,args,memory)			XTree_delete_base(this_root,XHTreeNode_delete,method,args,memory)

/**
 * @brief 获取层次树节点的父节点。
 * @param this_root 目标层次树节点。
 * @return 父节点指针；根节点返回 NULL。
 */
#define XHTreeNode_GetParent(this_root)					XTreeNode_GetParent(this_root)//树-获取父节点(继承的子类均可以使用)
/**
 * @brief 获取层次树节点的第一个子节点。
 * @param this_root 目标层次树节点。
 * @return 第一个子节点指针；没有子节点时返回 NULL。
 */
#define XHTreeNode_GetFirstChild(this_root)				XTreeNode_GetChild(this_root,XHTreeFirstChild)//树-获取第一个子节点指针(继承的子类均可以使用)
/**
 * @brief 获取层次树节点的下一个兄弟节点。
 * @param this_root 目标层次树节点。
 * @return 下一个兄弟节点指针；没有后继兄弟时返回 NULL。
 */
#define XHTreeNode_GetNextSibling(this_root)			XTreeNode_GetChild(this_root,XHTreeNextSibling)//树-获取下一个兄弟节点指针(继承的子类均可以使用)

/**
 * @brief 设置层次树节点的父节点。
 * @param this_root 目标层次树节点。
 * @param node 新父节点，可为 NULL。
 */
#define XHTreeNode_SetParent(this_root,node)			XTreeNode_SetParent(this_root,node)//树-设置父节点(继承的子类均可以使用)
/**
 * @brief 设置层次树节点的第一个子节点。
 * @param this_root 目标层次树节点。
 * @param node 新的第一个子节点，可为 NULL。
 */
#define XHTreeNode_SetFirstChild(this_root,node)		XTreeNode_SetChild(this_root,XHTreeFirstChild,node)//树-设置第一个子节点指针(继承的子类均可以使用)
/**
 * @brief 设置层次树节点的下一个兄弟节点。
 * @param this_root 目标层次树节点。
 * @param node 新的下一个兄弟节点，可为 NULL。
 */
#define XHTreeNode_SetNextSibling(this_root,node)		XTreeNode_SetChild(this_root,XHTreeNextSibling,node)//树-设置下一个兄弟节点指针(继承的子类均可以使用)
#ifdef __cplusplus
}
#endif
#endif /* XHIERARCHICALTREE_H */
