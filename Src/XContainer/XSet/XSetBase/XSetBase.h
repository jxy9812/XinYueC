#include "XContainerObject.h"
#if !defined(XSETBASE_H) && XSet_ON
#define XSETBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XCompare.h"
#include "XSetBase_iterator.h"

/**
* @brief XSetBase容器虚函数表大小定义
* @note 基于XContainerObject的虚函数表大小扩展，用于确定Set基类虚函数表的容量
*/
#define XSETBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))

/**
* @brief XSetBase虚函数表枚举定义
* @note 用于标识XSetBase容器的各类虚函数，继承自XContainerObject，定义Set特有的操作接口
*/
XCLASS_DEFINE_BEGING(XSetBase)
XCLASS_DEFINE_ENUM(XSetBase, Insert) = XCLASS_VTABLE_GET_SIZE(XContainerObject),  // 插入元素（键）
XCLASS_DEFINE_ENUM(XSetBase, Erase),                                           // 通过迭代器删除元素
XCLASS_DEFINE_ENUM(XSetBase, Remove),                                          // 通过键删除元素
XCLASS_DEFINE_ENUM(XSetBase, Find),                                            // 通过键查找元素
XCLASS_DEFINE_ENUM(XSetBase, Keys),                                            // 获取所有键的集合
XCLASS_DEFINE_END(XSetBase)

/**
* @brief XSetBase结构体定义（集合容器基类）
* @note 继承自XContainerObject，存储不重复的键值，支持键的比较和基本集合操作
*/
typedef struct XSetBase
{
	XContainerObject m_class;  // 继承自容器基类，存储键类型信息及基础容器数据（如大小、容量等）
} XSetBase;

// ========================= 虚函数表与初始化 =========================

/**
* @brief 初始化XSetBase的虚函数表
* @return 初始化完成的XSetBase虚函数表指针，失败返回NULL
*/
XVtable* XSetBase_class_init();

/**
* @brief 初始化XSetBase实例
* @param this_set 待初始化的XSetBase指针
* @param keyTypeSize 键的类型大小（字节数）
* @param compare 键的比较函数（用于判断键的相等性和排序）
* @note 需确保this_set不为NULL，keyTypeSize大于0且compare不为NULL，否则初始化无效
*/
void XSetBase_init(XSetBase* this_set, const size_t keyTypeSize, XCompare compare);

// ========================= 插入操作 =========================

/**
* @brief 插入键（拷贝语义）
* @param this_set 目标XSetBase
* @param pvKey 待插入的键指针
* @return 插入成功返回true（键不存在时），失败返回false（参数无效或键已存在）
* @note 内部通过键的拷贝方法处理数据，确保容器拥有键的独立副本
*/
bool XSetBase_insert_base(XSetBase* this_set, const void* pvKey);

/**
* @brief 宏定义：插入指定类型的键（拷贝语义）
* @param this_set 目标XSetBase
* @param keyType 键的类型
* @param key 待插入的键值
* @note 内部通过创建临时键变量，调用XSetBase_insert_base实现类型安全的插入
*/
#define XSetBase_Insert_Base(this_map, keyType, key) { keyType k = key;XSetBase_insert_base(this_map, &k); }

/**
* @brief 插入键（移动语义）
* @param this_set 目标XSetBase
* @param pvKey 待插入的键指针（所有权转移）
* @return 插入成功返回true（键不存在时），失败返回false（参数无效或键已存在）
* @note 源键的资源将被转移，之后不应再访问原键
*/
bool XSetBase_insert_move_base(XSetBase* this_set, const void* pvKey);

/**
* @brief 宏定义：插入指定类型的键（移动语义）
* @param this_set 目标XSetBase
* @param keyType 键的类型
* @param key 待插入的键值
* @note 内部通过创建临时键变量，调用XSetBase_insert_move_base实现类型安全的移动插入
*/
#define XSetBase_Insert_Move_Base(this_map, keyType, key) { keyType k = key; XSetBase_insert_move_base(this_map, &k); }

// ========================= 删除操作 =========================

/**
* @brief 通过迭代器删除元素，并获取下一个迭代器
* @param this_set 目标XSetBase
* @param it 指向待删除元素的迭代器
* @param next 输出参数，存储删除后的下一个迭代器（可为NULL）
* @note 若it为无效迭代器，操作无效；删除后迭代器it失效
*/
void XSetBase_erase_base(XSetBase* this_set, const XSetBase_iterator* it, XSetBase_iterator* next);

/**
* @brief 通过键删除元素
* @param this_set 目标XSetBase
* @param pvKey 待删除的键指针
* @return 删除成功返回true，键不存在或失败返回false
*/
bool XSetBase_remove_base(XSetBase* this_set, const void* pvKey);

/**
* @brief 宏定义：删除指定类型的键对应的元素
* @param this_set 目标XSetBase
* @param keyType 键的类型
* @param key 待删除的键值
* @note 内部通过创建临时键变量，调用XSetBase_remove_base实现类型安全的删除
*/
#define XSetBase_Remove_Base(this_map, keyType, key) { keyType k = key; XSet_remove_base(this_map, &k); }

// ========================= 查找与包含 =========================

/**
* @brief 通过键查找元素，获取迭代器
* @param this_set 目标XSetBase
* @param pvKey 待查找的键指针
* @param it 输出参数，存储找到的元素的迭代器（可为NULL）
* @return 找到返回true，否则返回false
*/
bool XSetBase_find_base(XSetBase* this_set, const void* pvKey, XSetBase_iterator* it);

/**
* @brief 判断容器是否包含指定键
* @param this_set 目标XSetBase
* @param pvKey 待判断的键指针
* @return 包含返回true，否则返回false
* @note 内部通过XSetBase_find_base实现，忽略输出迭代器
*/
bool XSetBase_contains(XSetBase* this_set, const void* pvKey);

// ========================= 键集合 =========================

/**
* @brief 获取容器中所有键的集合（XVector）
* @param this_set 目标XSetBase
* @return 存储所有键的XVector指针，失败返回NULL
* @note 返回的XVector需由用户自行释放，内部元素为容器中键的副本
*/
XVector* XSetBase_keys_base(const XSetBase* this_set);

// ========================= 继承与工具方法 =========================

/**
* @brief 拷贝容器（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_copy_base，实现容器的深拷贝
*/
#define XSetBase_copy_base				    XContainerObject_copy_base	

/**
* @brief 移动容器资源（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_move_base，转移容器资源所有权
*/
#define XSetBase_move_base				    XContainerObject_move_base	

/**
* @brief 释放容器资源（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_deinit_base，释放内部资源但不释放容器本身
*/
#define XSetBase_deinit_base				XContainerObject_deinit_base	

/**
* @brief 删除容器实例（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_delete_base，释放资源并销毁容器
*/
#define XSetBase_delete_base				XContainerObject_delete_base	

/**
* @brief 清空容器元素（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_clear_base，删除所有元素但保留容器结构
*/
#define XSetBase_clear_base				    XContainerObject_clear_base	

/**
* @brief 判断容器是否为空（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_isEmpty_base，元素数量为0时返回true
*/
#define XSetBase_isEmpty_base				XContainerObject_isEmpty_base	

/**
* @brief 获取容器元素数量（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_size_base，返回当前元素个数
*/
#define XSetBase_size_base				    XContainerObject_size_base	

/**
* @brief 获取容器容量（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_capacity_base，返回当前可容纳的最大元素数
*/
#define XSetBase_capacity_base			    XContainerObject_capacity_base

/**
* @brief 交换两个容器内容（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_swap_base，交换两个容器的元素和状态
*/
#define XSetBase_swap_base				    XContainerObject_swap_base	

/**
* @brief 获取键的类型大小（继承自XContainerObject）
* @note 宏定义，等价于XContainerObject_typeSize_base，返回键的类型大小（字节数）
*/
#define XSetBase_typeSize_base			    XContainerObject_typeSize_base

#ifdef __cplusplus
}
#endif
#endif // !XSet_H