#ifndef XLIST_VIRTUAL_H
#define XLIST_VIRTUAL_H
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XFunctionCallback.h"
typedef struct XListNode XListNode;
typedef struct XList XList;
//插入
XListNode* XVList_push_front(XList* this_list, void* LpValue);
XListNode* XVList_push_back(XList* this_list, void* LpValue);
void XVList_inserts(XList* this_list, XListNode* curNode, void* LpValue,size_t n);
void XVList_insert(XList* this_list, XListNode* curNode, void* LpValue);
void XVList_insertArray(XList* this_list, XListNode* curNode, const void* begin, size_t n);
//删除
void XVList_pop_front(XList* this_list); 
void XVList_pop_back(XList* this_list);
void XVList_erase(XList* this_list, XListNode* node); 
void XVList_remove(XList* this_list, void* LpValue); 
void XVList_clear(XList* this_list);
//遍历
XListNode* XVList_at(const XList* this_list, void* LpValue);
XListNode* XVList_front(XList* this_list);
XListNode* XVList_back(XList* this_list);
XListNode* XVList_find(const XList* this_list, void* LpValue);

#endif // !XList_virtual_H
