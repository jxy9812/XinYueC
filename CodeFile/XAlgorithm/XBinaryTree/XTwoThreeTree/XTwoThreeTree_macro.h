#ifndef XTWOTHREETREE_MACRO_H
#define XTWOTHREETREE_MACRO_H

//23树-获取父节点
#define XTTTree_GetParent(this_root) (*XBTree_GetTreeNode(this_root, XBTreeParent))
//23树-获取节点,0是指向父节点
#define XTTTree_GetNode(this_root,nSel，node) (*XTTTree_Node(this_root,nSel))
//23树-设置父节点
#define XTTTree_SetParent(this_root,nodes) (*XBTree_GetTreeNode(this_root, XBTreeParent)=nodes)
//23树-设置节点,0是指向父节点
#define XTTTree_SetNode(this_root,nSel，node) (*XTTTree_Node(this_root,nSel)=nodes)

//数据,索引从0开始
#define XTTTree_GetData(this_root,nSel,Type) (*((Type*)XTTTree_data(this_root,nSel)))//23树-获取数据

#endif
