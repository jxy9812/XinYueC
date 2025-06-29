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

// 插入操作
XListSNodeAtomic* XListSLinkedAtomic_push_front(XListSLinkedAtomic* this_list, void* pvData);
XListSNodeAtomic* XListSLinkedAtomic_push_back(XListSLinkedAtomic* this_list, void* pvData);

// 删除操作
void XListSLinkedAtomic_pop_front(XListSLinkedAtomic* this_list);
void XListSLinkedAtomic_pop_back(XListSLinkedAtomic* this_list);
void XListSLinkedAtomic_erase(XListSLinkedAtomic* this_list, XListSNodeAtomic* node);
void XListSLinkedAtomic_remove(XListSLinkedAtomic* this_list, void* pvData);

// 遍历操作
void* XListSLinkedAtomic_front(XListSLinkedAtomic* this_list);
void* XListSLinkedAtomic_back(XListSLinkedAtomic* this_list);
XListSNodeAtomic* XListSLinkedAtomic_find(const XListSLinkedAtomic* this_list, const void* findVal);

// 其他操作
void XListSLinkedAtomic_sort(XListSLinkedAtomic* this_list, XCompare compare);
void XListSLinkedAtomic_delete(XListSLinkedAtomic* this_list);
void XListSLinkedAtomic_clear(XListSLinkedAtomic* this_list);
bool XListSLinkedAtomic_isEmpty(XListSLinkedAtomic* this_list);
size_t XListSLinkedAtomic_getSize(XListSLinkedAtomic* this_list);
size_t XListSLinkedAtomic_getCapacity(XListSLinkedAtomic* this_list);
void XListSLinkedAtomic_swap(XListSLinkedAtomic* list1, XListSLinkedAtomic* list2);
size_t XListSLinkedAtomic_getTypeSize(XListSLinkedAtomic* this_list);

#ifdef __cplusplus
}
#endif
#endif