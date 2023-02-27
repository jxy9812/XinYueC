#ifndef XREDBLACKTREE_MACRO_H
#define XREDBLACKTREE_MACRO_H
//辅助宏
#define XRBTree_GetColor(this_root)  ((this_root)->color)//红黑树-获取颜色
#define XRBTree_SetColor(this_root,Color)  ((this_root)->color=Color)//红黑树-设置颜色
#define XRBTree_SetBlack(this_root) ((this_root)->color=XRBTreeBlack)//红黑树-设置颜色黑色
#define XRBTree_SetRed(this_root) ((this_root)->color=XRBTreeRed)//红黑树-设置颜色红色
#define XRBTree_IsBlack(this_root) ((this_root)->color==XRBTreeBlack)//红黑树-是否是黑色
#define XRBTree_IsRed(this_root) ((this_root)->color==XRBTreeRed)//红黑树-是否是红色
//数据
#define XRBTree_GetData(this_root,Type) XBTree_GetData(this_root,Type)//红黑树-获取数据

#endif