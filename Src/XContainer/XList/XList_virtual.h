#ifndef XLIST_VIRTUAL_H
#define XLIST_VIRTUAL_H
#include<stdio.h>
#include<stdbool.h>
typedef struct XListNode XListNode;
typedef struct XList XList;
//插入
XListNode* XVList_push_front(XList* this_list, void* LPValue);
XListNode* XVList_push_back(XList* this_list, void* LPValue);
void XVList_insert_front_p(XList* this_list, XListNode* pval, ...);
void XVList_insert_front_int(XList* this_list, int i, ...);
void XVList_insert(XList* this_list, XListNode* pval, const void* p1, const void* p2);
#endif // !XList_virtual_H
