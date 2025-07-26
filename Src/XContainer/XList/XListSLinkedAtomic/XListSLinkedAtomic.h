#include"XDataStructConfig.h"
#if !defined(XLISTSLINKEDATOMIC_H)&& XListSLinkedAtomic_ON
#define XLISTSLINKEDATOMIC_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdio.h>
#include"XAtomic.h"
#include"XListSLinkedAtomic_iterator.h"
#include"XListBase.h"
#define XLISTSLINKED_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))       //XList容器虚函数表大小
// 链表节点结构
typedef struct XListSNodeAtomic {
    struct XListSNodeAtomic* next; // 指向下一个节点的指针
    void* data;                    // 节点存储的数据
} XListSNodeAtomic;

// 获取节点数据指针的宏
#define XListSNodeAtomic_DataPtr(Node)  (&(((XListSNodeAtomic*)Node)->data))
// 获取节点中特定类型数据的宏
#define XListSNodeAtomic_Data(Node,Type) (*((Type*)XListSNodeAtomic_DataPtr(Node)))

// 无锁单链表结构
typedef struct XListSLinkedAtomic
{
    XListBase m_parent;               // 继承自链表基类
    XAtomic_ptr_t m_head;             // 头节点指针（原子类型）
    XAtomic_ptr_t m_tail;             // 尾节点指针（原子类型）
} XListSLinkedAtomic;

// 类初始化函数
XVtable* XListSLinkedAtomic_class_init();
// 创建链表函数
XListSLinkedAtomic* XListSLinkedAtomic_create(size_t typeSize);
#define XListSLinkedAtomic_Create(Type) XListSLinkedAtomic_create(sizeof(Type))
// 初始化链表
void XListSLinkedAtomic_init(XListSLinkedAtomic* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
#define XListSLinkedAtomic_push_front_base				    XListBase_push_front_base
#define XListSLinkedAtomic_Push_Front_Base				    XListBase_Push_Front_Base
#define XListSLinkedAtomic_push_front_move_base			    XListBase_push_front_move_base
#define XListSLinkedAtomic_Push_Front_Move_Base			    XListBase_Push_Front_Move_Base
// 链表尾部增加一个元素X
#define XListSLinkedAtomic_push_back_base				    XListBase_push_back_base
#define XListSLinkedAtomic_Push_Back_Base				    XListBase_Push_Back_Base
#define XListSLinkedAtomic_push_back_move_base			    XListBase_push_back_move_base
#define XListSLinkedAtomic_Push_Back_Move_Base			    XListBase_Push_Back_Move_Base
//链表指定节点前插入1个数据
#define XListSLinkedAtomic_insert_base					    XListBase_insert_base
#define XListSLinkedAtomic_insert_move_base				    XListBase_insert_move_base
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
#define XListSLinked_insert_array_base				        XListBase_insert_array_base
#define XListSLinked_insert_array_move_base			        XListBase_insert_array_move_base
//删除函数
//删除链表中第一个元素
#define XListSLinkedAtomic_pop_front_base					XListBase_pop_front_base
//删除链表中最后一个元素
#define XListSLinkedAtomic_pop_back_base					XListBase_pop_back_base
//删除指定节点
#define XListSLinkedAtomic_erase_base						XListBase_erase_base
//删除指定元素
#define XListSLinkedAtomic_remove_base					    XListBase_remove_base
#define XListSLinkedAtomic_Remove_Base					    XListBase_Remove_Base
//遍历函数
//返回链表头
#define XListSLinkedAtomic_front_base						XListBase_front_base
#define XListSLinkedAtomic_Front_Base						XListBase_Front_Base
//返回链表尾
#define XListSLinkedAtomic_back_base						XListBase_back_base
#define XListSLinkedAtomic_Back_Base						XListBase_Back_Base
//查找数据，返回找到的节点，没有返回NULL
#define XListSLinkedAtomic_find_base						XListBase_find_base
//排序
#define XListSLinkedAtomic_sort_base						XListBase_sort
#define XListSLinkedAtomic_copy_base				        XListBase_copy_base	
#define XListSLinkedAtomic_move_base				        XListBase_move_base	
#define XListSLinkedAtomic_deinit_base			            XListBase_deinit_base	
#define XListSLinkedAtomic_delete_base			            XListBase_delete_base	
#define XListSLinkedAtomic_clear_base			            XListBase_clear_base	
#define XListSLinkedAtomic_isEmpty_base			            XListBase_isEmpty_base	
#define XListSLinkedAtomic_getSize_base			            XListBase_getSize_base	
#define XListSLinkedAtomic_getCapacity_base		            XListBase_getCapacity_base
#define XListSLinkedAtomic_swap_base				        XListBase_swap_base	
#define XListSLinkedAtomic_getTypeSize_base		            XListBase_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif