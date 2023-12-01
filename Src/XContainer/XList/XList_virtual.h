#ifndef XLIST_VIRTUAL_H
#define XLIST_VIRTUAL_H
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XFunctionCallback.h"
typedef struct XListNode XListNode;
typedef struct XList XList;
//插入
XListNode* VXList_push_front(XList* this_list, void* LpValue);
XListNode* VXList_push_back(XList* this_list, void* LpValue);
void VXList_inserts(XList* this_list, XListNode* curNode, void* LpValue,size_t n);
void VXList_insert(XList* this_list, XListNode* curNode, void* LpValue);
void VXList_insertArray(XList* this_list, XListNode* curNode, const void* begin, size_t n);
//删除
void VXList_pop_front(XList* this_list); 
void VXList_pop_back(XList* this_list);
void VXList_erase(XList* this_list, XListNode* node); 
void VXList_remove(XList* this_list, void* LpValue); 
void VXList_clear(XList* this_list);
//遍历
XListNode* VXList_at(const XList* this_list, void* LpValue);
XListNode* VXList_front(XList* this_list);
XListNode* VXList_back(XList* this_list);
XListNode* VXList_find(const XList* this_list, void* LpValue);
//其他
void VXList_sort(XList* this_list, XCompare compare);
void VXList_free(XList* this_list);
#endif // !XList_virtual_H
