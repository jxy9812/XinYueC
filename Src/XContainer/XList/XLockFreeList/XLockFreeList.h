#include"CXinYueConfig.h"
#if !defined(XLockFreeList_H)&& XLockFreeList_ON
#define XLockFreeList_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>                  ///< 提供bool、true、false等布尔类型定义
#include <stdio.h>                    ///< 提供标准输入输出函数声明
#include "XAtomic.h"                  ///< 提供原子操作相关定义，支持无锁机制
#include "XLockFreeList_iterator.h"  ///< 包含无锁单链表正向迭代器定义
#include "XListBase.h"                ///< 包含链表基类XListBase的定义，无锁单链表继承自此基类
/**
* @brief 无锁单链表虚函数表大小定义
* @note 基于链表基类XListBase的虚函数表大小扩展，确定无锁单链表所需的虚函数表容量
*/
#define XLISTSLINKED_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))  // 无锁单链表虚函数表大小
/**
* @brief 无锁单链表节点结构体定义
* @details 存储链表中的单个元素，包含后继指针和数据指针，用于无锁环境下的节点操作
*/
typedef struct XLockFreeListNode 
{
	struct XLockFreeListNode* next;  ///< 指向后继节点的指针（原子操作保护）
	char data[];                     ///< 指向节点存储的实际数据的指针
} XLockFreeListNode;
/**
* @brief 获取节点中数据的指针（地址）
* @param Node 节点指针（XLockFreeListNode*类型）
* @return 数据存储的地址（void**类型）
*/
#define XLockFreeListNode_DataPtr(Node)  (((XLockFreeListNode*)Node)->data)
/**
* @brief 获取节点中指定类型的数据（解引用数据指针）
* @param Node 节点指针（XLockFreeListNode*类型）
* @param Type 数据的类型（如int、float等）
* @return 节点中存储的Type类型数据（值）
*/
#define XLockFreeListNode_Data(Node, Type) (*((Type*)XLockFreeListNode_DataPtr(Node)))
/**
* @brief 无锁单链表结构体定义
* @details 继承自链表基类XListBase，封装无锁单链表的核心属性，使用原子指针确保线程安全
*/
typedef struct XLockFreeList 
{
	XListBase m_class;              ///< 继承自链表基类，包含链表通用属性（大小、容量、虚函数表等）
	// —— 真无锁 Michael-Scott 队列：始终存在一个哨兵节点 —— 
	// m_head 指向当前哨兵节点（空时 m_head==m_tail，均指向同一哨兵）；
	// m_tail 指向最后一个节点。push_back 追加到 m_tail->next 并 CAS 推进 m_tail；
	// pop_front 先读 head->next 的数据再 CAS 推进 m_head，旧哨兵进入 hazard-pointer 退休链，
	// 待所有观测线程释放对它的引用后才真正 free，避免 ABA 与 use-after-free。
	XCACHE_ALIGN XAtomic_size_t m_head;          ///< 当前哨兵节点指针（原子）
	XCACHE_ALIGN XAtomic_size_t m_tail;          ///< 最后一个节点指针（原子）；空表时等于 m_head
} XLockFreeList;
// ------------------------------ 类初始化与创建 ------------------------------
/**
* @brief 初始化无锁单链表的虚函数表
* @return 初始化完成的虚函数表指针（XVtable*）
* @note 为无锁单链表注册各类操作的虚函数（插入、删除、遍历等），实现多态特性
*/
XVtable* XLockFreeList_class_init();
/**
* @brief 创建无锁单链表实例
* @param typeSize 链表存储元素的类型大小（字节数，如sizeof(int)）
* @return 成功返回创建的链表指针（XLockFreeList*），失败返回NULL
*/
XLockFreeList* XLockFreeList_create_ex(XMemoryType memory,  size_t typeSize);
/**
* @brief 简化创建指定类型的无锁单链表（类型安全宏）
* @param Type 链表存储元素的类型（如int、float等）
* @return 调用XLockFreeList_create创建的链表指针
* @note 自动推导类型大小，避免手动计算sizeof(Type)
*/
#define XLockFreeList_Create(Type) XLockFreeList_create(sizeof(Type))
/**
* @brief 初始化已分配内存的无锁单链表实例
* @param this_list 待初始化的链表指针（需提前分配内存）
* @param typeSize 链表存储元素的类型大小（字节数）
*/
void XLockFreeList_init(XLockFreeList* this_list, size_t typeSize);
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 链表头部插入元素（拷贝语义，基础版本）
* @note 继承自XListBase的头插操作，通过宏重命名实现接口统一，支持无锁环境
*/
#define XLockFreeList_push_front_base              XListBase_push_front_base
/**
* @brief 链表头部插入元素（类型安全宏，拷贝语义）
* @note 继承自XListBase的类型安全头插操作，自动处理类型转换，支持无锁环境
*/
#define XLockFreeList_Push_Front_Base              XListBase_Push_Front_Base
/**
* @brief 链表头部插入元素（移动语义，基础版本）
* @note 继承自XListBase的头插操作（移动语义），减少数据拷贝开销，支持无锁环境
*/
#define XLockFreeList_push_front_move_base         XListBase_push_front_move_base
/**
* @brief 链表头部插入元素（类型安全宏，移动语义）
* @note 继承自XListBase的类型安全头插操作（移动语义），自动处理类型转换，支持无锁环境
*/
#define XLockFreeList_Push_Front_Move_Base         XListBase_Push_Front_Move_Base
/**
* @brief 链表尾部插入元素（拷贝语义，基础版本）
* @note 继承自XListBase的尾插操作，通过宏重命名实现接口统一，支持无锁环境
*/
#define XLockFreeList_push_back_base               XListBase_push_back_base
/**
* @brief 链表尾部插入元素（类型安全宏，拷贝语义）
* @note 继承自XListBase的类型安全尾插操作，自动处理类型转换，支持无锁环境
*/
#define XLockFreeList_Push_Back_Base               XListBase_Push_Back_Base
/**
* @brief 链表尾部插入元素（移动语义，基础版本）
* @note 继承自XListBase的尾插操作（移动语义），减少数据拷贝开销，支持无锁环境
*/
#define XLockFreeList_push_back_move_base          XListBase_push_back_move_base
/**
* @brief 链表尾部插入元素（类型安全宏，移动语义）
* @note 继承自XListBase的类型安全尾插操作（移动语义），自动处理类型转换，支持无锁环境
*/
#define XLockFreeList_Push_Back_Move_Base          XListBase_Push_Back_Move_Base
/**
* @brief 在指定节点前插入元素（拷贝语义，基础版本）
* @note 继承自XListBase的指定位置插入操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_insert_base                  XListBase_insert_base
/**
* @brief 在指定节点前插入元素（移动语义，基础版本）
* @note 继承自XListBase的指定位置插入操作（移动语义），减少数据拷贝开销
*/
#define XLockFreeList_insert_move_base             XListBase_insert_move_base
/**
* @brief 在指定节点前插入数组元素（拷贝语义，基础版本）
* @note 继承自XListBase的数组插入操作，需指定数组元素数量
*/
#define XLockFreeList_insert_array_base                 XListBase_insert_array_base
/**
* @brief 在指定节点前插入数组元素（移动语义，基础版本）
* @note 继承自XListBase的数组插入操作（移动语义），需指定数组元素数量
*/
#define XLockFreeList_insert_array_move_base            XListBase_insert_array_move_base
// ------------------------------ 删除操作 ------------------------------
/**
 * @brief 原子地删除链表头部元素，并将其数据拷贝到指定位置。
 * @param this_list 目标链表指针。
 * @param pvOutData 接收数据的目标地址，不能为NULL。
 * @return 成功返回true，失败（如链表为空）返回false。
 * @note 此操作是线程安全的（多消费者安全）。
 */
bool XLockFreeList_pop_and_copy_front(XLockFreeList* this_list, void* pvOutData);
/**
 * @brief 原子地删除链表头部元素，并将其数据移动到指定位置。
 * @param this_list 目标链表指针。
 * @param pvOutData 接收数据的目标地址，不能为NULL。
 * @return 成功返回true，失败（如链表为空）返回false。
 * @note 此操作是线程安全的（多消费者安全）。如果未设置移动方法，则退化为拷贝。
 */
bool XLockFreeList_pop_and_move_front(XLockFreeList* this_list, void* pvOutData);
/**
* @brief 删除链表第一个元素（基础版本）
* @note 继承自XListBase的头删操作，通过宏重命名实现接口统一，支持无锁环境
*/
#define XLockFreeList_pop_front_base               XListBase_pop_front_base
/**
* @brief 删除链表最后一个元素（基础版本）
* @note 继承自XListBase的尾删操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_pop_back_base                XListBase_pop_back_base
/**
* @brief 删除指定节点（基础版本）
* @note 继承自XListBase的指定节点删除操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_erase_base                   XListBase_erase_base
/**
* @brief 删除指定值的元素（拷贝语义，基础版本）
* @note 继承自XListBase的按值删除操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_remove_base                  XListBase_remove_base
/**
* @brief 删除指定值的元素（类型安全宏，拷贝语义）
* @note 继承自XListBase的类型安全按值删除操作，自动处理类型转换
*/
#define XLockFreeList_Remove_Base                  XListBase_Remove_Base
// ------------------------------ 遍历操作 ------------------------------
/**
* @brief 获取链表头部元素（基础版本）
* @note 继承自XListBase的获取头元素操作，通过宏重命名实现接口统一，支持无锁环境
*/
#define XLockFreeList_front_base                   XListBase_front_base
/**
* @brief 获取链表头部元素（类型安全宏）
* @note 继承自XListBase的类型安全获取头元素操作，自动处理类型转换
*/
#define XLockFreeList_Front_Base                   XListBase_Front_Base
/**
* @brief 获取链表尾部元素（基础版本）
* @note 继承自XListBase的获取尾元素操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_back_base                    XListBase_back_base
/**
* @brief 获取链表尾部元素（类型安全宏）
* @note 继承自XListBase的类型安全获取尾元素操作，自动处理类型转换
*/
#define XLockFreeList_Back_Base                    XListBase_Back_Base
/**
* @brief 查找指定值的元素（基础版本）
* @note 继承自XListBase的查找操作，返回找到的节点指针（未找到返回NULL）
*/
#define XLockFreeList_find_base                    XListBase_find_base
/**
* @brief 判断链表是否包含指定值（基础版本，Qt 6.8 对齐）
*/
#define XLockFreeList_contains                     XListBase_contains
/**
* @brief 查找指定值的首个索引（基础版本，Qt 6.8 对齐）
*/
#define XLockFreeList_indexOf_base(list, val, from, it)     XListBase_indexOf_base(list, val, from, it)
/**
* @brief 查找指定值的最后索引（基础版本，Qt 6.8 对齐）
*/
#define XLockFreeList_lastIndexOf_base(list, val, from, it) XListBase_lastIndexOf_base(list, val, from, it)
/**
* @brief 删除所有匹配指定值的元素（基础版本，Qt 6.8 对齐）
*/
#define XLockFreeList_removeAll_base               XListBase_removeAll_base
/**
* @brief 删除首个匹配指定值的元素（基础版本，Qt 6.8 对齐）
*/
#define XLockFreeList_removeOne_base               XListBase_removeOne_base
/**
* @brief 按谓词条件删除元素（基础版本，Qt 6.8 对齐）
*/
#define XLockFreeList_removeIf_base(list, pred, udata)      XListBase_removeIf_base(list, pred, udata)
// ------------------------------ 其他操作 ------------------------------
/**
* @brief 对链表进行排序（基础版本）
* @note 继承自XListBase的排序操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_sort_base                    XListBase_sort_base
/**
* @brief 拷贝链表（基础版本）
* @note 继承自XListBase的拷贝操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_copy_base                    XListBase_copy_base
/**
* @brief 移动链表（基础版本，转移所有权）
* @note 继承自XListBase的移动操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_move_base                    XListBase_move_base
/**
* @brief 反初始化链表（基础版本）
* @note 继承自XListBase的反初始化操作，释放资源但不释放链表本身
*/
#define XLockFreeList_deinit_base                  XListBase_deinit_base
/**
* @brief 删除链表（基础版本）
* @note 继承自XListBase的删除操作，释放资源并销毁链表实例
*/
void XLockFreeList_delete_base(XLockFreeList* this_list);
/**
* @brief 清空链表（基础版本）
* @note 继承自XListBase的清空操作，删除所有元素但保留链表结构
*/
#define XLockFreeList_clear_base                   XListBase_clear_base
/**
* @brief 判断链表是否为空（基础版本）
* @note 继承自XListBase的判空操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_isEmpty_base                 XListBase_isEmpty_base
/**
* @brief 获取链表元素数量（基础版本）
* @note 继承自XListBase的获取大小操作，通过宏重命名实现接口统一，支持无锁环境
*/
#define XLockFreeList_size_base                    XListBase_size_base
/**
* @brief 获取链表容量（基础版本）
* @note 继承自XListBase的获取容量操作，链表容量通常与元素数量一致，支持无锁环境
*/
#define XLockFreeList_capacity_base                XListBase_capacity_base
/**
* @brief 交换两个链表的内容（基础版本）
* @note 继承自XListBase的交换操作，通过宏重命名实现接口统一
*/
#define XLockFreeList_swap_base                    XListBase_swap_base
/**
* @brief 获取链表存储元素的类型大小（基础版本）
* @note 继承自XListBase的获取类型大小操作，通过宏重命名实现接口统一，支持无锁环境
*/
#define XLockFreeList_typeSize_base                XListBase_typeSize_base
/**
* @brief C++兼容性声明结束
*/
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XLockFreeList_create
#define XLockFreeList_create(...) XLockFreeList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XLockFreeList_H
