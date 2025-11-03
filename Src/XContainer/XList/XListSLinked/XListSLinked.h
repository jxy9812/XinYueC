#include"CXinYueConfig.h"
#if !defined(XLISTSLINKED_H)&& XListSLinked_ON
#define XLISTSLINKED_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
#include"XListSLinked_iterator.h"
#include"XListBase.h"
/**
* @brief 单链表容器虚函数表大小定义
* @note 基于链表基类XListBase的虚函数表大小确定，确保虚函数索引不冲突
*/
#define XLISTSLINKED_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))
/**
* @brief 单链表节点结构体定义
* @details 存储单链表中的单个元素，包含指向下一个节点的指针和数据存储区
*/
typedef struct XListSNode
{
	struct XListSNode* next; // 指向下一个节点的指针
	void* data; // 存储元素数据的指针
}XListSNode;
/**
* @brief 创建指定类型的单链表节点
* @param type 节点存储的数据类型
* @return 新创建的节点指针，失败返回NULL
* @note 内部通过内存分配函数创建节点，包含数据存储区
*/
#define XListSNode_Create(type)   (XMemory_malloc(sizeof(XListSNode*)+sizeof(type)))
/**
* @brief 获取单链表节点中数据的指针
* @param Node 目标节点指针
* @return 节点中数据的指针（void*类型）
* @note 需配合类型转换使用，获取实际数据类型的指针
*/
#define XListSNode_DataPtr(Node)  (&(((XListSNode*)Node)->data))
/**
* @brief 获取单链表节点中指定类型的数据
* @param Node 目标节点指针
* @param Type 数据类型（如int、float等）
* @return 节点中存储的指定类型数据值
* @note 内部通过XListSNode_DataPtr进行指针转换，直接返回数据值
*/
#define XListSNode_Data(Node,Type) (*((Type*)XListSNode_DataPtr(Node)))
/**
* @brief 单链表结构体定义
* @details 单链表容器实现，继承自链表基类XListBase，包含尾节点指针优化尾插操作
*/
typedef struct XListSLinked
{
	XListBase m_class; // 继承自链表基类，包含容器基本属性
	XListSNode* m_tail; // 尾节点指针，用于优化尾部操作效率
}XListSLinked;
/**
* @brief 初始化单链表类的虚函数表
* @return 初始化完成的虚函数表指针
* @note 用于设置单链表的虚函数实现，继承并扩展自XListBase的虚函数表
*/
XVtable* XListSLinked_class_init();
/**
* @brief 创建单链表实例
* @param typeSize 单个元素的类型大小（字节数）
* @return 新创建的单链表实例指针，失败返回NULL
* @note 内部会分配内存并初始化链表，需通过XListSLinked_delete_base释放
*/
XListSLinked* XListSLinked_create(size_t typeSize);
/**
* @brief 类型安全的单链表创建宏
* @param Type 元素数据类型（如int、float等）
* @return 新创建的单链表实例指针，失败返回NULL
* @note 内部调用XListSLinked_create，自动计算类型大小，简化使用
*/
#define XListSLinked_Create(Type) XListSLinked_create(sizeof(Type))
/**
* @brief 初始化单链表实例
* @param this_list 待初始化的单链表指针
* @param typeSize 单个元素的类型大小（字节数）
* @note 需手动分配链表实例内存后调用，初始化基类成员和内部状态
*/
void XListSLinked_init(XListSLinked* this_list, size_t typeSize);
// 插入函数
/**
* @brief 在链表头部插入已有节点（转移节点所有权）
* @param this_list 链表实例指针
* @param node 待插入的节点指针
* @return 插入成功返回true，失败返回false
* @note 直接插入已有节点，避免重复分配内存，内部调用XListBase实现
*/
#define XListSLinked_push_front_node_base			XListBase_push_front_node_base
/**
* @brief 在链表头部插入元素（拷贝语义）
* @param this_list 链表实例指针
* @param pvData 待插入元素的数据指针
* @return 插入成功返回新节点指针，失败返回NULL
* @note 拷贝pvData指向的数据到新节点，插入到链表头部，内部调用XListBase实现
*/
#define XListSLinked_push_front_base				XListBase_push_front_base
/**
* @brief 类型安全的链表头部插入（拷贝语义）
* @param this_list 链表实例指针
* @param type 元素数据类型
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，通过XListSLinked_push_front_base实现插入
*/
#define XListSLinked_Push_Front_Base				XListBase_Push_Front_Base
/**
* @brief 在链表头部插入元素（移动语义）
* @param this_list 链表实例指针
* @param pvData 待移动元素的数据指针（所有权转移）
* @return 插入成功返回新节点指针，失败返回NULL
* @note 移动pvData指向的数据到新节点，减少拷贝开销，内部调用XListBase实现
*/
#define XListSLinked_push_front_move_base			XListBase_push_front_move_base
/**
* @brief 类型安全的链表头部插入（移动语义）
* @param this_list 链表实例指针
* @param type 元素数据类型
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，通过XListSLinked_push_front_move_base实现插入
*/
#define XListSLinked_Push_Front_Move_Base			XListBase_Push_Front_Move_Base
/**
* @brief 在链表尾部插入已有节点（转移节点所有权）
* @param this_list 链表实例指针
* @param node 待插入的节点指针
* @return 插入成功返回true，失败返回false
* @note 直接插入已有节点，利用尾指针优化插入效率，内部调用XListBase实现
*/
#define XListSLinked_push_back_node_base			XListBase_push_back_node_base
/**
* @brief 在链表尾部插入元素（拷贝语义）
* @param this_list 链表实例指针
* @param pvData 待插入元素的数据指针
* @return 插入成功返回新节点指针，失败返回NULL
* @note 拷贝pvData指向的数据到新节点，插入到链表尾部，内部调用XListBase实现
*/
#define XListSLinked_push_back_base					XListBase_push_back_base
/**
* @brief 类型安全的链表尾部插入（拷贝语义）
* @param this_list 链表实例指针
* @param type 元素数据类型
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，通过XListSLinked_push_back_base实现插入
*/
#define XListSLinked_Push_Back_Base					XListBase_Push_Back_Base
/**
* @brief 在链表尾部插入元素（移动语义）
* @param this_list 链表实例指针
* @param pvData 待移动元素的数据指针（所有权转移）
* @return 插入成功返回新节点指针，失败返回NULL
* @note 移动pvData指向的数据到新节点，减少拷贝开销，内部调用XListBase实现
*/
#define XListSLinked_push_back_move_base			XListBase_push_back_move_base
/**
* @brief 类型安全的链表尾部插入（移动语义）
* @param this_list 链表实例指针
* @param type 元素数据类型
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，通过XListSLinked_push_back_move_base实现插入
*/
#define XListSLinked_Push_Back_Move_Base			XListBase_Push_Back_Move_Base
/**
* @brief 在指定节点前插入元素（拷贝语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（新元素插入到此节点之前）
* @param pvData 待插入元素的数据指针
* @return 插入成功返回true，失败返回false
* @note 拷贝数据到新节点，插入到curNode之前，内部调用XListBase实现
*/
#define XListSLinked_insert_base					XListBase_insert_base
/**
* @brief 在指定节点前插入元素（移动语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（新元素插入到此节点之前）
* @param pvData 待移动元素的数据指针（所有权转移）
* @return 插入成功返回true，失败返回false
* @note 移动数据到新节点，插入到curNode之前，内部调用XListBase实现
*/
#define XListSLinked_insert_move_base				XListBase_insert_move_base
/**
* @brief 在指定节点前插入数组元素（拷贝语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（数组插入到此节点之前）
* @param array 待插入的数组指针
* @param count 数组元素数量
* @return 实际插入的元素数量（成功时等于count，失败时可能小于count）
* @note 拷贝数组中所有元素到新节点，依次插入到curNode之前，内部调用XListBase实现
*/
#define XListSLinked_insert_array_base				XListBase_insert_array_base
/**
* @brief 在指定节点前插入数组元素（移动语义）
* @param this_list 链表实例指针
* @param curNode 目标节点（数组插入到此节点之前）
* @param array 待移动的数组指针（所有权转移）
* @param count 数组元素数量
* @return 实际插入的元素数量（成功时等于count，失败时可能小于count）
* @note 移动数组中所有元素到新节点，依次插入到curNode之前，内部调用XListBase实现
*/
#define XListSLinked_insert_array_move_base			XListBase_insert_array_move_base
// 删除函数
/**
* @brief 删除链表中第一个元素
* @param this_list 链表实例指针
* @return 删除成功返回true，失败（如链表为空）返回false
* @note 移除并释放链表头部的第一个元素，内部调用XListBase实现
*/
#define XListSLinked_pop_front_base					XListBase_pop_front_base
/**
* @brief 删除链表中最后一个元素
* @param this_list 链表实例指针
* @return 删除成功返回true，失败（如链表为空）返回false
* @note 移除并释放链表尾部的最后一个元素，内部调用XListBase实现
*/
#define XListSLinked_pop_back_base					XListBase_pop_back_base
/**
* @brief 删除指定节点
* @param this_list 链表实例指针
* @param it 指向待删除节点的迭代器
* @param next 输出参数，指向删除节点的下一个节点的迭代器
* @note 移除并释放it指向的节点，通过next返回下一个节点位置，内部调用XListBase实现
*/
#define XListSLinked_erase_base						XListBase_erase_base
/**
* @brief 删除链表中首个与指定值匹配的元素
* @param this_list 链表实例指针
* @param pvData 待删除元素的数据指针
* @return 删除成功返回true，未找到元素返回false
* @note 基于数据值匹配删除元素，内部调用XListBase实现
*/
#define XListSLinked_remove_base					XListBase_remove_base
/**
* @brief 类型安全的删除首个匹配元素
* @param this_list 链表实例指针
* @param type 元素数据类型
* @param value 待删除的元素值
* @note 自动创建临时变量存储value，通过XListSLinked_remove_base实现删除
*/
#define XListSLinked_Remove_Base					XListBase_Remove_Base
// 遍历函数
/**
* @brief 获取链表头部元素的数据指针
* @param this_list 链表实例指针
* @return 头部元素的数据指针，链表为空返回NULL
* @note 仅返回指针，不检查有效性，内部调用XListBase实现
*/
#define XListSLinked_front_base						XListBase_front_base
/**
* @brief 类型安全的获取链表头部元素
* @param this_list 链表实例指针
* @param type 元素数据类型
* @return 头部元素的值
* @note 内部通过XListSLinked_front_base获取指针并转换为指定类型
*/
#define XListSLinked_Front_Base						XListBase_Front_Base
/**
* @brief 获取链表尾部元素的数据指针
* @param this_list 链表实例指针
* @return 尾部元素的数据指针，链表为空返回NULL
* @note 仅返回指针，不检查有效性，利用尾指针优化获取效率，内部调用XListBase实现
*/
#define XListSLinked_back_base						XListBase_back_base
/**
* @brief 类型安全的获取链表尾部元素
* @param this_list 链表实例指针
* @param type 元素数据类型
* @return 尾部元素的值
* @note 内部通过XListSLinked_back_base获取指针并转换为指定类型
*/
#define XListSLinked_Back_Base						XListBase_Back_Base
/**
* @brief 查找链表中与指定值匹配的元素
* @param this_list 链表实例指针
* @param findVal 待查找元素的数据指针
* @param it 输出参数，指向找到的元素的迭代器（未找到时指向末尾）
* @return 找到元素返回true，未找到返回false
* @note 支持自定义比较函数，未指定时使用memcmp，内部调用XListBase实现
*/
#define XListSLinked_find_base						XListBase_find_base
/**
* @brief 判断链表是否包含指定元素
* @param this_list 链表实例指针
* @param value 待判断的元素数据指针
* @return 包含元素返回true，否则返回false
* @note 内部调用XListBase_find_base实现，仅判断存在性，不返回位置
*/
#define XListSLinked_contains_base					XListBase_contains
// 其他操作
/**
* @brief 对链表元素进行排序
* @param this_list 链表实例指针
* @param order 排序顺序（升序或降序）
* @note 基于元素类型大小进行比较排序，内部调用XListBase实现
*/
#define XListSLinked_sort_base						XListBase_sort_base
/**
* @brief 拷贝链表（深拷贝）
* @param this_list 目标链表指针
* @param src 源链表指针
* @note 拷贝源链表所有元素到目标链表，内部调用XListBase实现
*/
#define XListSLinked_copy_base				        XListBase_copy_base	
/**
* @brief 移动链表（所有权转移）
* @param this_list 目标链表指针
* @param src 源链表指针
* @note 将源链表的元素和状态转移到目标链表，源链表清空，内部调用XListBase实现
*/
#define XListSLinked_move_base				        XListBase_move_base	
/**
* @brief 反初始化链表
* @param this_list 链表实例指针
* @note 释放链表内部资源（节点和数据），保留链表实例本身内存，内部调用XListBase实现
*/
#define XListSLinked_deinit_base			        XListBase_deinit_base	
/**
* @brief 删除链表实例
* @param this_list 链表实例指针
* @note 先反初始化链表释放内部资源，再释放链表实例本身内存，内部调用XListBase实现
*/
#define XListSLinked_delete_base			        XListBase_delete_base	
/**
* @brief 清空链表所有元素
* @param this_list 链表实例指针
* @note 移除并释放链表中所有元素，链表变为空，内部调用XListBase实现
*/
#define XListSLinked_clear_base			            XListBase_clear_base	
/**
* @brief 判断链表是否为空
* @param this_list 链表实例指针
* @return 链表为空返回true，否则返回false
* @note 基于链表元素数量判断，内部调用XListBase实现
*/
#define XListSLinked_isEmpty_base			        XListBase_isEmpty_base	
/**
* @brief 获取链表当前元素数量
* @param this_list 链表实例指针
* @return 链表中元素的数量（size_t类型）
* @note 返回当前存储的元素个数，内部调用XListBase实现
*/
#define XListSLinked_size_base						XListBase_size_base	
/**
* @brief 获取链表当前容量
* @param this_list 链表实例指针
* @return 链表的容量（size_t类型）
* @note 对于单链表，容量通常等于元素数量，内部调用XListBase实现
*/
#define XListSLinked_capacity_base					XListBase_capacity_base
/**
* @brief 交换两个链表的内容
* @param this_list 第一个链表指针
* @param other 第二个链表指针
* @note 交换两个链表的所有元素和状态，内部调用XListBase实现
*/
#define XListSLinked_swap_base				        XListBase_swap_base	
/**
* @brief 获取链表中元素的类型大小
* @param this_list 链表实例指针
* @return 单个元素的类型大小（字节数）
* @note 返回初始化时指定的元素类型大小，内部调用XListBase实现
*/
#define XListSLinked_typeSize_base					XListBase_typeSize_base
#ifdef __cplusplus
}
#endif
#endif // 