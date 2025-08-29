#include"XDataStructConfig.h"
#if !defined(XLISTBASE_H)&& XList_ON
#define XLISTBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
#include"XContainerObject.h"
#include"XFunctionCallback.h"
#include"XListBase_iterator.h"
#define XLISTBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))       //XList容器虚函数表大小
//XList虚函数表枚举
XCLASS_DEFINE_BEGING(XListBase)
XCLASS_DEFINE_ENUM(XListBase, Push_Front)=XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XListBase, Push_Back),
XCLASS_DEFINE_ENUM(XListBase, Insert),
XCLASS_DEFINE_ENUM(XListBase, Insert_Array),
XCLASS_DEFINE_ENUM(XListBase, Pop_Front),
XCLASS_DEFINE_ENUM(XListBase, Pop_Back),
XCLASS_DEFINE_ENUM(XListBase, Erase),
XCLASS_DEFINE_ENUM(XListBase, Remove),
XCLASS_DEFINE_ENUM(XListBase, Front),
XCLASS_DEFINE_ENUM(XListBase, Back),
XCLASS_DEFINE_ENUM(XListBase, Find),
XCLASS_DEFINE_ENUM(XListBase, Sort),
XCLASS_DEFINE_END(XListBase)
typedef struct XListBase
{
	XContainerObject m_parent;
	XEquality m_equality;//相等比较函数
}XListBase;
typedef struct XListBaseNode XListBaseNode;
//初始化 链表
void XListBase_init(XListBase* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
XListBaseNode* XListBase_push_front_base(XListBase* this_list, void* pvData);
#define XListBase_Push_Front_Base(this_list,type,value){type t=value;XListBase_push_front_base(this_list,&t);}
XListBaseNode* XListBase_push_front_move_base(XListBase* this_list, void* pvData);
#define XListBase_Push_Front_Move_Base(this_list,type,value){type t=value;XListBase_push_front_move_base(this_list,&t);}
// 链表尾部增加一个元素X
XListBaseNode* XListBase_push_back_base(XListBase* this_list, void* pvData);
#define XListBase_Push_Back_Base(this_list,type,value){type t=value;XListBase_push_back_base(this_list,&t);}
XListBaseNode* XListBase_push_back_move_base(XListBase* this_list, void* pvData);
#define XListBase_Push_Back_Move_Base(this_list,type,value){type t=value;XListBase_push_back_move_base(this_list,&t);}
//链表指定节点前插入1个数据
bool XListBase_insert_base(XListBase* this_list, XListBaseNode* curNode, void* pvData);
bool XListBase_insert_move_base(XListBase* this_list, XListBaseNode* curNode, void* pvData);
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小(元素数量)
size_t  XListBase_insert_array_base(XListBase* this_list, XListBaseNode* curNode,void* array, size_t count);
size_t  XListBase_insert_array_move_base(XListBase* this_list, XListBaseNode* curNode, void* array, size_t count);
//删除函数
//删除链表中第一个元素
bool  XListBase_pop_front_base(XListBase* this_list);
//删除链表中最后一个元素
bool  XListBase_pop_back_base(XListBase* this_list);
//删除指定节点
void  XListBase_erase_base(XListBase* this_list,const XListBase_iterator* it,XListBase_iterator* next);
//删除指定元素
bool  XListBase_remove_base(XListBase* this_list, void* pvData);
#define XListBase_Remove_Base(this_list,type,value){type t=value;XListBase_remove_base(this_list,&t);}
//遍历函数
//返回链表头
void* XListBase_front_base(XListBase* this_list);
#define XListBase_Front_Base(list,Type) (*(Type*)XListBase_front_base(list))
//返回链表尾
void* XListBase_back_base(XListBase* this_list);
#define XListBase_Back_Base(list,Type) (*(Type*)XListBase_back_base(list))
//查找数据，返回找到的节点，没有返回NULL
bool XListBase_find_base(const  XListBase* this_list, const void* findVal, XListBase_iterator* it);
bool XListBase_contains(const XListBase* this_list, const void* value);
void XListBase_sort_base(XListBase* this_list, XCompare compare);
#define XListBase_copy_base				XContainerObject_copy_base	
#define XListBase_move_base				XContainerObject_move_base	
#define XListBase_deinit_base			XContainerObject_deinit_base	
#define XListBase_delete_base			XContainerObject_delete_base	
#define XListBase_clear_base			XContainerObject_clear_base	
#define XListBase_isEmpty_base			XContainerObject_isEmpty_base	
#define XListBase_size_base				XContainerObject_size_base	
#define XListBase_capacity_base			XContainerObject_capacity_base
#define XListBase_swap_base				XContainerObject_swap_base	
#define XListBase_typeSize_base			XContainerObject_typeSize_base
#ifdef __cplusplus
}
#endif
#endif // 