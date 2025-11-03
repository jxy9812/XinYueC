#include"CXinYueConfig.h"
#if !defined(XLISTDLINKED_H)&& XListDLinked_ON
#define XLISTDLINKED_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>						///< 提供bool、true、false等布尔类型定义
#include <stdio.h>							///< 提供标准输入输出函数声明
#include "XListDLinked_iterator.h"			///< 包含双向链表正向迭代器定义
#include "XListDLinked_reverse_iterator.h"	///< 包含双向链表反向迭代器定义
#include "XListBase.h"						///< 包含链表基类XListBase的定义，双向链表继承自此基类
/**
* @brief 双向链表虚函数表大小定义
* @note 基于链表基类XListBase的虚函数表大小扩展，确定双向链表所需的虚函数表容量
*/
#define XLISTDLINKED_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))       //XList容器虚函数表大小
/**
* @brief 双向链表节点结构体定义
* @details 存储链表中的单个元素，包含前驱指针、后继指针和数据指针
*/
typedef struct XListDNode
{
	struct XListDNode* prev;///< 指向前驱节点的指针
	struct XListDNode* next;///< 指向后继节点的指针
	void* data;             ///< 指向节点存储的实际数据的指针
} XListDNode;
/**
* @brief 获取节点中数据的指针（地址）
* @param Node 节点指针（XListDNode*类型）
* @return 数据存储的地址（void**类型）
*/
#define XListDNode_DataPtr(Node)  (&(((XListDNode*)Node)->data))
/**
* @brief 获取节点中指定类型的数据（解引用数据指针）
* @param Node 节点指针（XListDNode*类型）
* @param Type 数据的类型（如int、float等）
* @return 节点中存储的Type类型数据（值）
*/
#define XListDNode_Data(Node, Type) (*((Type*)XListDNode_DataPtr(Node)))
/**
* @brief 双向循环链表结构体定义
* @details 继承自链表基类XListBase，封装双向循环链表的核心属性
*/
typedef struct XListDLinked
{
	XListBase m_class;///< 继承自链表基类，包含链表通用属性（大小、容量、虚函数表等）
} XListDLinked;
/**
* @brief 初始化双向链表的虚函数表
* @return 初始化完成的虚函数表指针（XVtable*）
* @note 为双向链表注册各类操作的虚函数（插入、删除、遍历等），实现多态特性
*/
XVtable* XListDLinked_class_init();
/**
* @brief 创建双向链表实例
* @param TypeSize 链表存储元素的类型大小（字节数，如sizeof(int)）
* @return 成功返回创建的链表指针（XListDLinked*），失败返回NULL
*/
XListDLinked* XListDLinked_create(size_t TypeSize);
/**
* @brief 简化创建指定类型的双向链表（类型安全宏）
* @param Type 链表存储元素的类型（如int、float等）
* @return 调用XListDLinked_create创建的链表指针
* @note 自动推导类型大小，避免手动计算sizeof(Type)
*/
#define XListDLinked_Create(Type) XListDLinked_create(sizeof(Type))
/**
* @brief 初始化已分配内存的双向链表实例
* @param this_list 待初始化的链表指针（需提前分配内存）
* @param typeSize 链表存储元素的类型大小（字节数）
*/
void XListDLinked_init(XListDLinked* this_list, size_t typeSize);
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 链表头部插入元素（拷贝语义，基础版本）
* @note 继承自XListBase的头插操作，通过宏重命名实现接口统一
*/
#define XListDLinked_push_front_base              XListBase_push_front_base
/**
* @brief 链表头部插入元素（类型安全宏，拷贝语义）
* @note 继承自XListBase的类型安全头插操作，自动处理类型转换
*/
#define XListDLinked_Push_Front_Base              XListBase_Push_Front_Base
/**
* @brief 链表头部插入元素（移动语义，基础版本）
* @note 继承自XListBase的头插操作（移动语义），减少数据拷贝开销
*/
#define XListDLinked_push_front_move_base         XListBase_push_front_move_base
/**
* @brief 链表头部插入元素（类型安全宏，移动语义）
* @note 继承自XListBase的类型安全头插操作（移动语义），自动处理类型转换
*/
#define XListDLinked_Push_Front_Move_Base         XListBase_Push_Front_Move_Base
/**
* @brief 链表尾部插入元素（拷贝语义，基础版本）
* @note 继承自XListBase的尾插操作，通过宏重命名实现接口统一
*/
#define XListDLinked_push_back_base               XListBase_push_back_base
/**
* @brief 链表尾部插入元素（类型安全宏，拷贝语义）
* @note 继承自XListBase的类型安全尾插操作，自动处理类型转换
*/
#define XListDLinked_Push_Back_Base               XListBase_Push_Back_Base
/**
* @brief 链表尾部插入元素（移动语义，基础版本）
* @note 继承自XListBase的尾插操作（移动语义），减少数据拷贝开销
*/
#define XListDLinked_push_back_move_base          XListBase_push_back_move_base
/**
* @brief 链表尾部插入元素（类型安全宏，移动语义）
* @note 继承自XListBase的类型安全尾插操作（移动语义），自动处理类型转换
*/
#define XListDLinked_Push_Back_Move_Base          XListBase_Push_Back_Move_Base
/**
* @brief 在指定节点前插入元素（拷贝语义，基础版本）
* @note 继承自XListBase的指定位置插入操作，通过宏重命名实现接口统一
*/
#define XListDLinked_insert_base                  XListBase_insert_base
/**
* @brief 在指定节点前插入元素（移动语义，基础版本）
* @note 继承自XListBase的指定位置插入操作（移动语义），减少数据拷贝开销
*/
#define XListDLinked_insert_move_base             XListBase_insert_move_base
/**
* @brief 在指定节点前插入数组元素（拷贝语义，基础版本）
* @note 继承自XListBase的数组插入操作，需指定数组元素数量
*/
#define XListDLinked_insert_array_base            XListBase_insert_array_base
/**
* @brief 在指定节点前插入数组元素（移动语义，基础版本）
* @note 继承自XListBase的数组插入操作（移动语义），需指定数组元素数量
*/
#define XListDLinked_insert_array_move_base       XListBase_insert_array_move_base
// ------------------------------ 删除操作 ------------------------------
/**
* @brief 删除链表第一个元素（基础版本）
* @note 继承自XListBase的头删操作，通过宏重命名实现接口统一
*/
#define XListDLinked_pop_front_base               XListBase_pop_front_base
/**
* @brief 删除链表最后一个元素（基础版本）
* @note 继承自XListBase的尾删操作，通过宏重命名实现接口统一
*/
#define XListDLinked_pop_back_base                XListBase_pop_back_base
/**
* @brief 删除指定节点（基础版本）
* @note 继承自XListBase的指定节点删除操作，通过宏重命名实现接口统一
*/
#define XListDLinked_erase_base                   XListBase_erase_base
/**
* @brief 删除指定值的元素（拷贝语义，基础版本）
* @note 继承自XListBase的按值删除操作，通过宏重命名实现接口统一
*/
#define XListDLinked_remove_base                  XListBase_remove_base
/**
* @brief 删除指定值的元素（类型安全宏，拷贝语义）
* @note 继承自XListBase的类型安全按值删除操作，自动处理类型转换
*/
#define XListDLinked_Remove_Base                  XListBase_Remove_Base
// ------------------------------ 遍历操作 ------------------------------
/**
* @brief 获取链表头部元素（基础版本）
* @note 继承自XListBase的获取头元素操作，通过宏重命名实现接口统一
*/
#define XListDLinked_front_base                   XListBase_front_base
/**
* @brief 获取链表头部元素（类型安全宏）
* @note 继承自XListBase的类型安全获取头元素操作，自动处理类型转换
*/
#define XListDLinked_Front_Base                   XListBase_Front_Base
/**
* @brief 获取链表尾部元素（基础版本）
* @note 继承自XListBase的获取尾元素操作，通过宏重命名实现接口统一
*/
#define XListDLinked_back_base                    XListBase_back_base
/**
* @brief 获取链表尾部元素（类型安全宏）
* @note 继承自XListBase的类型安全获取尾元素操作，自动处理类型转换
*/
#define XListDLinked_Back_Base                    XListBase_Back_Base
/**
* @brief 查找指定值的元素（基础版本）
* @note 继承自XListBase的查找操作，返回找到的节点指针（未找到返回NULL）
*/
#define XListDLinked_find_base                    XListBase_find_base
// ------------------------------ 其他操作 ------------------------------
/**
* @brief 对链表进行排序（基础版本）
* @note 继承自XListBase的排序操作，通过宏重命名实现接口统一
*/
#define XListDLinked_sort_base                    XListBase_sort_base
/**
* @brief 拷贝链表（基础版本）
* @note 继承自XListBase的拷贝操作，通过宏重命名实现接口统一
*/
#define XListDLinked_copy_base                    XListBase_copy_base
/**
* @brief 移动链表（基础版本，转移所有权）
* @note 继承自XListBase的移动操作，通过宏重命名实现接口统一
*/
#define XListDLinked_move_base                    XListBase_move_base
/**
* @brief 反初始化链表（基础版本）
* @note 继承自XListBase的反初始化操作，释放资源但不释放链表本身
*/
#define XListDLinked_deinit_base                  XListBase_deinit_base
/**
* @brief 删除链表（基础版本）
* @note 继承自XListBase的删除操作，释放资源并销毁链表实例
*/
#define XListDLinked_delete_base                  XListBase_delete_base
/**
* @brief 清空链表（基础版本）
* @note 继承自XListBase的清空操作，删除所有元素但保留链表结构
*/
#define XListDLinked_clear_base                   XListBase_clear_base
/**
* @brief 判断链表是否为空（基础版本）
* @note 继承自XListBase的判空操作，通过宏重命名实现接口统一
*/
#define XListDLinked_isEmpty_base                 XListBase_isEmpty_base
/**
* @brief 获取链表元素数量（基础版本）
* @note 继承自XListBase的获取大小操作，通过宏重命名实现接口统一
*/
#define XListDLinked_size_base                    XListBase_size_base
/**
* @brief 获取链表容量（基础版本）
* @note 继承自XListBase的获取容量操作，链表容量通常与元素数量一致
*/
#define XListDLinked_capacity_base                XListBase_capacity_base
/**
* @brief 交换两个链表的内容（基础版本）
* @note 继承自XListBase的交换操作，通过宏重命名实现接口统一
*/
#define XListDLinked_swap_base                    XListBase_swap_base
/**
* @brief 获取链表存储元素的类型大小（基础版本）
* @note 继承自XListBase的获取类型大小操作，通过宏重命名实现接口统一
*/
#define XListDLinked_typeSize_base                XListBase_typeSize_base
/**
* @brief C++兼容性声明结束
*/
#ifdef __cplusplus
}
#endif
#endif // XLISTDLINKED_H