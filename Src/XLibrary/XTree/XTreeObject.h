//二叉树基类
#ifndef XBINARYTREEOBJECT_H
#define XBINARYTREEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XVector.h"
//数据释放方法
typedef void (*XTreeNodeDataDeleteMethod)(void* value,void* args);
//节点释放方法
typedef void (*XTreeNodeDeleteMethod)(XTreeNode* node);
//树节点
typedef struct XTreeNode
{
	uint8_t nodeCount;//节点数量
	struct XTreeNode* parentNode;//父节点
	struct XTreeNode** nodes;//节点数组
	void* data;//数据
}XTreeNode;
//初始化
XTreeNode* XTreeNode_create(const uint8_t nodeCount, const char* pvData, const size_t typeSize);
void XTreeNode_init(XTreeNode* node,const uint8_t nodeCount, const char* pvData, const size_t typeSize);
bool XTreeNode_setData(XTreeNode* this_root, const void* pvData, size_t typeSize);
//获取数据
void* XTreeNode_getData(XTreeNode* this_root);
//设置一个节点指针
bool XTreeNode_setNode(XTreeNode* this_root, const uint8_t nodeType, XTreeNode* node);
//获取节点指针
XTreeNode* XTreeNode_getChild(XTreeNode* this_root, const uint8_t nodeType);
//获取节点指针引用
XTreeNode** XTreeNode_getChildRef(XTreeNode* this_root, const uint8_t nodeType);//
XTreeNode** XTreeNode_getParentRef(XTreeNode* this_root);
//替换孩子节点(将原孩子在父节点的指向修改为新的节点，并建立新的父子关系,旧节点的父指针指向空)
bool XTree_ReplacementChildNode(XTreeNode* formerChild/*旧的*/, XTreeNode* freshChild/*新的*/);
//获取节点在父节点指针的位置
XTreeNode** XTreeNode_getChildrenParentRef(XTreeNode* this_root);//
//释放一个节点(仅释放自己也不释放数据)
void XTreeNode_delete(XTreeNode* node);
//递归释放整颗树派生类
void XTree_delete_base(XTreeNode* this_root, XTreeNodeDeleteMethod nodeMethod, XTreeNodeDataDeleteMethod dataMethod, void* args);
//递归释放整颗树
void XTree_delete(XTreeNode* this_root, XTreeNodeDataDeleteMethod method,void* args);

//获取节点
#define XTreeNode_GetParent(this_root)				(((XTreeNode*)this_root)->parentNode)//树-获取父节点(继承的子类均可以使用)
#define XTreeNode_GetChild(this_root,type)			(((XTreeNode**)(((XTreeNode*)this_root)->nodes))[type])//树-获取孩子(继承的子类均可以使用)
//设置节点
#define XTreeNode_SetParent(this_root,node)			((((XTreeNode*)this_root)->parentNode)=node)//树-设置父节点(继承的子类均可以使用)
#define XTreeNode_SetChild(this_root,type,node)		(((XTreeNode**)(((XTreeNode*)this_root)->nodes))[type]=node)//树-设置孩子(继承的子类均可以使用)
//数据
#define XTreeNode_SetDataPtr(this_root,ptr)			(((XTreeNode*)this_root)->data=ptr)
#define XTreeNode_SetData(this_root,data)			XTreeNode_setData(this_root,&data)//树-插入数据
#define XTreeNode_GetDataPtr(this_root)				(((XTreeNode*)this_root)->data)
#define XTreeNode_GetData(this_root,Type)			(*((Type*)(XTreeNode_GetDataPtr(this_root))))//树-获取数据(继承的子类均可以使用)
#define XTreeNode_GetDataTypeSize(this_root)		(((XTreeNode*)this_root)->typeSize)
#define XTreeNode_SetDataTypeSize(this_root,size)	(((XTreeNode*)this_root)->typeSize=size)
#ifdef __cplusplus
}
#endif
#endif // !XBINARYTREEOBJECT_H
