#ifndef XBINARYTREEOBJECT_MACRO_H
#define XBINARYTREEOBJECT_MACRO_H
//获取节点
#define XBTree_GetParent(this_root) (*XBTree_GetTreeNode(this_root, XBTreeParent))//二叉树-获取父节点(继承的子类均可以使用)
#define XBTree_GetLChild(this_root) (*XBTree_GetTreeNode(this_root, XBTreeLChild))//二叉树-获取左孩子(继承的子类均可以使用)
#define XBTree_GetRChild(this_root) (*XBTree_GetTreeNode(this_root, XBTreeRChild))//二叉树-获取右孩子(继承的子类均可以使用)
//设置节点
#define XBTree_SetParent(this_root,node) (*XBTree_GetTreeNode(this_root, XBTreeParent)=node)//二叉树-设置父节点(继承的子类均可以使用)
#define XBTree_SetLChild(this_root,node) (*XBTree_GetTreeNode(this_root, XBTreeLChild)=node)//二叉树-设置左孩子(继承的子类均可以使用)
#define XBTree_SetRChild(this_root,node) (*XBTree_GetTreeNode(this_root, XBTreeRChild)=node)//二叉树-设置右孩子(继承的子类均可以使用)
//数据
#define  XBTree_InsertData(this_root,LPvalue) XBTree_insertData(this_root,&LPvalue,sizeof(LPvalue))//二叉树-插入数据
#define  XBTree_GetData(this_root,Type) (*((Type*)((*(XBTreeNode**)this_root)->LPvalue)))//二叉树-获取数据(继承的子类均可以使用)
#endif
