//二叉树基类
#ifndef XBINARYTREEOBJECT_H
#define XBINARYTREEOBJECT_H

/**
 * @file XTreeObject.h
 * @brief 多叉树节点的公共布局、关系操作和生命周期 API。
 *
 * 节点采用连续内存布局：[节点对象][子节点指针数组][用户数据]。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "XTypes.h"
#include "XVector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 用户数据释放回调类型。
 * @param value 当前节点的用户数据地址；数据以内嵌形式存储在节点后部。
 * @param args 调用者传入的上下文参数，可为 NULL。
 */
typedef void (*XTreeNodeDataDeleteMethod)(void* value, void* args);

/**
 * @brief 节点释放回调类型。
 * @param node 要释放的节点。
 * @param memory 节点创建时使用的内存管理器；为 NULL 时由实现选择默认管理器。
 */
typedef void (*XTreeNodeDeleteMethod)(XTreeNode* node, XMemory* memory);

/**
 * @brief 树节点公共基类布局。
 * @details nodeSize 是从节点首地址到用户数据的偏移量，nodeCount 是子节点槽位数。
 *          具体树类型可将该结构作为第一个成员进行布局兼容。
 */
typedef struct XTreeNode //内存布局 [XTreeNode(及派生类)] [XTreeNode* nodes[](存储其他节点)] [char data[](用户数据)]
{
	uint32_t nodeSize:24; /**< 节点及子节点指针区域的大小，不含用户数据。 */
	uint32_t nodeCount:8; /**< 子节点指针槽位数量。 */
	struct XTreeNode* parentNode; /**< 父节点；根节点为 NULL。 */
} XTreeNode;

/**
 * @brief 计算指定子节点数量所需的节点头部大小。
 * @param nodeCount 子节点指针槽位数量。
 * @return 节点对象和子节点指针数组的总字节数，不含用户数据。
 */
size_t XTreeNode_typeSize(const uint8_t nodeCount);

/**
 * @brief 使用默认内存管理器创建并初始化一个树节点。
 * @param nodeCount 子节点指针槽位数量，必须大于 0。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataSize 用户数据字节数，必须大于 0。
 * @return 成功返回新节点，参数无效或分配失败返回 NULL；节点由调用者释放。
 */
XTreeNode* XTreeNode_create(const uint8_t nodeCount, const char* pvData, const size_t dataSize);

/**
 * @brief 使用指定内存管理器创建并初始化一个树节点。
 * @param nodeCount 子节点指针槽位数量，必须大于 0。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataSize 用户数据字节数，必须大于 0。
 * @param memory 内存管理器；为 NULL 时使用默认管理器。
 * @return 成功返回新节点，参数无效、管理器不可用或分配失败返回 NULL。
 */
XTreeNode* XTreeNode_create_ex(const uint8_t nodeCount, const char* pvData,
	const size_t dataSize, XMemory* memory);

/**
 * @brief 初始化调用者提供的树节点存储。
 * @param node 待初始化的节点内存。
 * @param nodeCount 子节点指针槽位数量，必须大于 0。
 * @param treeNodeSize 节点头部及子节点指针区域大小，通常取 XTreeNode_typeSize 的结果。
 * @param pvData 初始用户数据地址；为 NULL 时将用户数据清零。
 * @param dataSize 用户数据字节数，必须大于 0。
 * @note node 指向的内存必须至少能容纳 treeNodeSize + dataSize 字节。
 */
void XTreeNode_init(XTreeNode* node,const uint8_t nodeCount, size_t treeNodeSize,const char* pvData, const size_t dataSize);

/**
 * @brief 覆盖节点的用户数据。
 * @param this_root 目标节点。
 * @param pvData 新用户数据地址，不能为 NULL。
 * @param dataSize 要复制的字节数，不能超过创建节点时的用户数据容量。
 * @return 成功复制返回 true；节点或数据地址无效返回 false。
 */
bool XTreeNode_setData(XTreeNode* this_root, const void* pvData, size_t dataSize);

/**
 * @brief 获取节点用户数据的可写地址。
 * @param this_root 目标节点。
 * @return 用户数据地址；this_root 为 NULL 时返回 NULL。
 */
void* XTreeNode_getData(XTreeNode* this_root);

/**
 * @brief 设置指定子节点槽位。
 * @param this_root 拥有该子节点槽位的节点。
 * @param nodeType 子节点槽位索引，范围为 [0, nodeCount)。
 * @param node 要写入的子节点，可为 NULL 以清空槽位。
 * @return 成功写入返回 true；节点为空或索引越界返回 false。
 * @note 函数不会自动更新 node 的 parentNode。
 */
bool XTreeNode_setNode(XTreeNode* this_root, const uint8_t nodeType, XTreeNode* node);

/**
 * @brief 获取指定子节点指针。
 * @param this_root 父节点。
 * @param nodeType 子节点槽位索引，调用者必须保证索引有效。
 * @return 子节点指针；节点为空或对应槽位为空时返回 NULL。
 */
XTreeNode* XTreeNode_getChild(XTreeNode* this_root, const uint8_t nodeType);

/**
 * @brief 获取指定子节点槽位的指针引用。
 * @param this_root 父节点。
 * @param nodeType 子节点槽位索引，范围为 [0, nodeCount)。
 * @return 指向子节点槽位的地址；参数无效或索引越界返回 NULL。
 */
XTreeNode** XTreeNode_getChildRef(XTreeNode* this_root, const uint8_t nodeType);//

/**
 * @brief 获取节点 parentNode 成员的地址。
 * @param this_root 目标节点。
 * @return 父节点指针成员的地址；this_root 为 NULL 时返回 NULL。
 */
XTreeNode** XTreeNode_getParentRef(XTreeNode* this_root);

/**
 * @brief 在父节点中用新节点替换旧子节点。
 * @param formerChild 已挂接到父节点的旧子节点。
 * @param freshChild 要挂接的新子节点。
 * @return 替换成功返回 true；任一节点为空、旧节点没有父节点或关系不存在返回 false。
 * @note 旧节点不会被释放；新节点原有的父子关系由调用者负责处理。
 */
bool XTree_ReplacementChildNode(XTreeNode* formerChild/*旧的*/, XTreeNode* freshChild/*新的*/);

/**
 * @brief 获取父节点中指向当前节点的子节点槽位地址。
 * @param this_root 要查询的子节点。
 * @return 父节点对应槽位的地址；节点是根节点、关系不存在或参数无效时返回 NULL。
 */
XTreeNode** XTreeNode_getChildrenParentRef(XTreeNode* this_root);//

/**
 * @brief 释放单个节点的内存。
 * @param node 要释放的节点；为 NULL 时无操作。
 * @param memory 释放节点时使用的内存管理器；为 NULL 时使用默认管理器。
 * @note 只释放节点内存，不调用用户数据回调，也不释放子节点。
 */
void XTreeNode_delete(XTreeNode* node, XMemory* memory);

/**
 * @brief 使用自定义节点释放器递归释放整棵树。
 * @param this_root 树根节点；为 NULL 时无操作。
 * @param nodeMethod 每个节点的释放回调，不能为 NULL。
 * @param dataMethod 用户数据释放回调，可为 NULL。
 * @param args 传递给 dataMethod 的上下文参数，可为 NULL。
 * @param memory 节点释放所使用的内存管理器，可为 NULL。
 */
void XTree_delete_base(XTreeNode* this_root, XTreeNodeDeleteMethod nodeMethod,
	XTreeNodeDataDeleteMethod dataMethod, void* args, XMemory* memory);

/**
 * @brief 使用默认节点释放器递归释放整棵树。
 * @param this_root 树根节点；为 NULL 时无操作。
 * @param method 用户数据释放回调，可为 NULL。
 * @param args 传递给 method 的上下文参数，可为 NULL。
 * @param memory 节点释放所使用的内存管理器，可为 NULL。
 */
void XTree_delete(XTreeNode* this_root, XTreeNodeDataDeleteMethod method,
	void* args, XMemory* memory);

/**
 * @brief 获取节点用户数据的原始地址。
 * @param this_root 目标节点。
 * @return 用户数据地址。
 */
#define XTreeNode_GetDataPtr(this_root)				(((uint8_t*)this_root)+((XTreeNode*)this_root)->nodeSize)

/**
 * @brief 按指定类型读取节点用户数据。
 * @param this_root 目标节点。
 * @param Type 用户数据类型。
 * @return 用户数据的左值，可读写。
 */
#define XTreeNode_GetData(this_root,Type)			(*((Type*)(XTreeNode_GetDataPtr(this_root))))//树-获取数据(继承的子类均可以使用)

/**
 * @brief 获取节点的子节点指针数组。
 * @param this_root 目标节点。
 * @return 子节点槽位数组首地址。
 */
#define XTreeNode_GetNodes(this_root)				((XTreeNode**)((uint8_t*)XTreeNode_GetDataPtr(this_root)-((XTreeNode*)this_root)->nodeCount*sizeof(XTreeNode*)))

/**
 * @brief 获取节点的父节点指针。
 * @param this_root 目标节点。
 * @return 父节点指针，根节点返回 NULL。
 */
#define XTreeNode_GetParent(this_root)				(((XTreeNode*)this_root)->parentNode)//树-获取父节点(继承的子类均可以使用)

/**
 * @brief 获取指定子节点指针。
 * @param this_root 父节点。
 * @param type 子节点槽位索引。
 * @return 对应子节点指针。
 */
#define XTreeNode_GetChild(this_root,type)			(XTreeNode_GetNodes(this_root)[type])//树-获取孩子(继承的子类均可以使用)

/**
 * @brief 设置节点的父节点指针。
 * @param this_root 目标节点。
 * @param node 新父节点，可为 NULL。
 */
#define XTreeNode_SetParent(this_root,node)			((((XTreeNode*)this_root)->parentNode)=node)//树-设置父节点(继承的子类均可以使用)

/**
 * @brief 设置指定子节点槽位。
 * @param this_root 父节点。
 * @param type 子节点槽位索引。
 * @param node 新子节点，可为 NULL。
 */
#define XTreeNode_SetChild(this_root,type,node)		(XTreeNode_GetNodes(this_root)[type]=node)//树-设置孩子(继承的子类均可以使用)

#ifdef __cplusplus
}
#endif
#endif // !XBINARYTREEOBJECT_H
