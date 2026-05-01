#include"XContainer.h"
#if !defined(XMAPBASE_H)&& XMap_ON
#define XMAPBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XCompare.h"
#include"XPair.h"
#include"XMapBase_iterator.h"

/**
* @brief XMapBase容器虚函数表大小定义
* @note 基于XContainer的虚函数表大小扩展
*/
#define XMAPBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))

/**
* @brief XMapBase虚函数表枚举定义
* @note 用于标识XMapBase容器的各类虚函数，继承自XContainer
*/
XCLASS_DEFINE_BEGING(XMapBase)
XCLASS_DEFINE_ENUM(XMapBase, Insert) = XCLASS_VTABLE_GET_SIZE(XContainer),  // 插入键值对
XCLASS_DEFINE_ENUM(XMapBase, Erase),                                           // 通过迭代器删除元素
XCLASS_DEFINE_ENUM(XMapBase, Remove),                                          // 通过键删除元素
XCLASS_DEFINE_ENUM(XMapBase, Value),                                           // 通过键获取值
XCLASS_DEFINE_ENUM(XMapBase, Find),                                            // 通过键查找元素
XCLASS_DEFINE_ENUM(XMapBase, Keys),                                            // 获取所有键的集合
XCLASS_DEFINE_ENUM(XMapBase, Values),                                          // 获取所有值的集合
XCLASS_DEFINE_END(XMapBase)

/**
* @brief XMapBase结构体定义（映射容器基类）
* @note 继承自XContainer，存储键值对数据，支持键的类型管理和数据操作方法
*/
typedef struct XMapBase
{
	XContainer m_class;                // 继承自容器基类，存储值类型信息及基础容器数据
	size_t m_keyTypeSize;                    // 键的类型大小（字节数）
	XCDataCopyMethod m_keyCopyMethod;        // 键的拷贝方法
	XCDataMoveMethod m_keyMoveMethod;        // 键的移动方法
	XCDataDeinitMethod m_keyDeinitMethod;    // 键的释放方法
} XMapBase;

// ========================= 虚函数表与初始化 =========================

/**
* @brief 初始化XMapBase的虚函数表
* @return 初始化完成的XMapBase虚函数表指针，失败返回NULL
*/
XVtable* XMapBase_class_init();

/**
* @brief 初始化XMapBase实例
* @param this_map 待初始化的XMapBase指针
* @param keyTypeSize 键的类型大小（字节数）
* @param valTypeSize 值的类型大小（字节数）
* @param compare 键的比较函数（用于排序和查找）
* @note 需确保this_map不为NULL，keyTypeSize和valTypeSize大于0，否则初始化无效
*/
void XMapBase_init(XMapBase* this_map, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare);

// ========================= 插入操作 =========================

/**
* @brief 插入键值对（拷贝语义）
* @param this_map 目标XMapBase
* @param key 待插入的键指针
* @param pvValue 待插入的值指针
* @return 插入成功返回true，失败返回false（参数无效或键已存在时可能失败）
* @note 内部通过键的拷贝方法和值的拷贝方法处理数据
*/
bool XMapBase_insert_base(XMapBase* this_map, const void* key, const void* pvValue);

/**
* @brief 宏定义：插入指定类型的键值对（拷贝语义）
* @param this_map 目标XMapBase
* @param keyType 键的类型
* @param key 待插入的键值
* @param valType 值的类型
* @param Value 待插入的值
* @note 内部通过创建临时键值变量，调用XMapBase_insert_base实现
*/
#define XMapBase_Insert_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMap_insert_base(this_map,&k,&v);}

/**
* @brief 插入键值对（移动语义，键和值均移动）
* @param this_map 目标XMapBase
* @param key 待插入的键指针（所有权转移）
* @param pvValue 待插入的值指针（所有权转移）
* @return 插入成功返回true，失败返回false
* @note 源键和值的资源将被转移，之后不应再访问
*/
bool XMapBase_insert_move_base(XMapBase* this_map, void* key, void* pvValue);

/**
* @brief 宏定义：插入指定类型的键值对（移动语义，键和值均移动）
* @param this_map 目标XMapBase
* @param keyType 键的类型
* @param key 待插入的键值
* @param valType 值的类型
* @param Value 待插入的值
* @note 内部通过创建临时键值变量，调用XMapBase_insert_move_base实现
*/
#define XMapBase_Insert_Move_Base(this_map,keyType,key,valType,Value) {keyType k=key;valType v=Value; XMapBase_insert_move_base(this_map,&k,&v);}

/**
* @brief 插入键值对（移动语义，仅键移动）
* @param this_map 目标XMapBase
* @param key 待插入的键指针（所有权转移）
* @param pvValue 待插入的值指针（拷贝语义）
* @return 插入成功返回true，失败返回false
*/
bool XMapBase_insert_keyMove_base(XMapBase* this_map, void* key, const void* pvValue);

/**
* @brief 插入键值对（移动语义，仅值移动）
* @param this_map 目标XMapBase
* @param key 待插入的键指针（拷贝语义）
* @param pvValue 待插入的值指针（所有权转移）
* @return 插入成功返回true，失败返回false
*/
bool XMapBase_insert_valueMove_base(XMapBase* this_map, const void* key, void* pvValue);

// ========================= 删除操作 =========================

/**
* @brief 通过迭代器删除元素，并获取下一个迭代器
* @param this_map 目标XMapBase
* @param it 指向待删除元素的迭代器
* @param next 输出参数，存储删除后的下一个迭代器（可为NULL）
* @note 若it为无效迭代器，操作无效；删除后迭代器it失效
*/
void XMapBase_erase_base(XMapBase* this_map, const XMapBase_iterator* it, XMapBase_iterator* next);

/**
* @brief 通过键删除元素
* @param this_map 目标XMapBase
* @param key 待删除的键指针
* @return 删除成功返回true，键不存在或失败返回false
*/
bool XMapBase_remove_base(XMapBase* this_map, const void* key);

/**
* @brief 宏定义：删除指定类型的键对应的元素
* @param this_map 目标XMapBase
* @param keyType 键的类型
* @param key 待删除的键值
* @note 内部通过创建临时键变量，调用XMapBase_remove_base实现
*/
#define XMapBase_Remove_Base(this_map,keyType,key) {keyType k=key;XMapBase_remove_base(this_map,&k);}

// ========================= 元素访问 =========================

/**
* @brief 通过键获取值的地址
* @param this_map 目标XMapBase
* @param key 键指针
* @return 指向对应值的指针，键不存在或失败返回NULL
*/
void* XMapBase_value_base(XMapBase* this_map, const void* key);

/**
* @brief 宏定义：通过键获取指定类型的值
* @param this_map 目标XMapBase
* @param key 键值
* @param valueType 值的类型
* @return 键对应的valueType类型值
* @note 内部通过XMapBase_value_base获取地址并解引用
*/
#define XMapBase_Value_Base(this_map,key,valueType) (*(valueType*)XMapBase_value_base(this_map,&(key)))

// ========================= 查找与包含 =========================

/**
* @brief 通过键查找元素，获取迭代器
* @param this_map 目标XMapBase
* @param key 待查找的键指针
* @param it 输出参数，存储找到的元素的迭代器（可为NULL）
* @return 找到返回true，否则返回false
*/
bool XMapBase_find_base(const XMapBase* this_map, const void* key, XMapBase_iterator* it);

/**
* @brief 判断容器是否包含指定键
* @param this_map 目标XMapBase
* @param key 待判断的键指针
* @return 包含返回true，否则返回false
*/
bool XMapBase_contains(const XMapBase* this_map, const void* key);

// ========================= 键值集合 =========================

/**
* @brief 获取容器中所有键的集合（XVector）
* @param this_map 目标XMapBase
* @return 存储所有键的XVector指针，失败返回NULL
* @note 返回的XVector需由用户自行释放
*/
XVector* XMapBase_keys_base(const XMapBase* this_map);

/**
* @brief 获取容器中所有值的集合（XVector）
* @param this_map 目标XMapBase
* @return 存储所有值的XVector指针，失败返回NULL
* @note 返回的XVector需由用户自行释放
*/
XVector* XMapBase_values_base(const XMapBase* this_map);

// ========================= 继承与工具方法 =========================

/**
* @brief 拷贝容器（继承自XContainer）
* @note 宏定义，等价于XContainer_copy_base
*/
#define XMapBase_copy_base				XContainer_copy_base	

/**
* @brief 移动容器资源（继承自XContainer）
* @note 宏定义，等价于XContainer_move_base
*/
#define XMapBase_move_base				XContainer_move_base	

/**
* @brief 释放容器资源（继承自XContainer）
* @note 宏定义，等价于XContainer_deinit_base
*/
#define XMapBase_deinit_base			XContainer_deinit_base	

/**
* @brief 删除容器实例（继承自XContainer）
* @note 宏定义，等价于XContainer_delete_base
*/
#define XMapBase_delete_base			XContainer_delete_base	

/**
* @brief 清空容器元素（继承自XContainer）
* @note 宏定义，等价于XContainer_clear_base
*/
#define XMapBase_clear_base				XContainer_clear_base	

/**
* @brief 判断容器是否为空（继承自XContainer）
* @note 宏定义，等价于XContainer_isEmpty_base
*/
#define XMapBase_isEmpty_base			XContainer_isEmpty_base	

/**
* @brief 获取容器元素数量（继承自XContainer）
* @note 宏定义，等价于XContainer_size_base
*/
#define XMapBase_size_base				XContainer_size_base	

/**
* @brief 获取容器容量（继承自XContainer）
* @note 宏定义，等价于XContainer_capacity_base
*/
#define XMapBase_capacity_base			XContainer_capacity_base

/**
* @brief 交换两个容器内容（继承自XContainer）
* @note 宏定义，等价于XContainer_swap_base
*/
#define XMapBase_swap_base				XContainer_swap_base	

/**
* @brief 获取值的类型大小（继承自XContainer）
* @note 宏定义，等价于XContainer_typeSize_base
*/
#define XMapBase_typeSize_base			XContainer_typeSize_base

// ========================= 键方法设置与获取 =========================

/**
* @brief 获取键的拷贝方法
* @param Map XMapBase指针
* @return 键的拷贝方法（XCDataCopyMethod）
*/
#define XMapBaseKeyCopyMethod(Map) (((XMapBase*)(Map))->m_keyCopyMethod)

/**
* @brief 设置键的拷贝方法
* @param Map XMapBase指针
* @param method 新的键拷贝方法（XCDataCopyMethod）
*/
#define XMapBaseSetKeyCopyMethod(Map,method) (((XMapBase*)(Map))->m_keyCopyMethod=method)

/**
* @brief 获取键的移动方法
* @param Map XMapBase指针
* @return 键的移动方法（XCDataMoveMethod）
*/
#define XMapBaseKeyMoveMethod(Map) (((XMapBase*)(Map))->m_keyMoveMethod)

/**
* @brief 设置键的移动方法
* @param Map XMapBase指针
* @param method 新的键移动方法（XCDataMoveMethod）
*/
#define XMapBaseSetKeyMoveMethod(Map,method) (((XMapBase*)(Map))->m_keyMoveMethod=method)

/**
* @brief 获取键的释放方法
* @param Map XMapBase指针
* @return 键的释放方法（XCDataDeinitMethod）
*/
#define XMapBaseKeyDeinitMethod(Map) (((XMapBase*)(Map))->m_keyDeinitMethod)

/**
* @brief 设置键的释放方法
* @param Map XMapBase指针
* @param method 新的键释放方法（XCDataDeinitMethod）
*/
#define XMapBaseSetKeyDeinitMethod(Map,method) (((XMapBase*)(Map))->m_keyDeinitMethod=method)

// ========================= 节点数据释放 =========================

/**
* @brief 释放XMapBase节点中的键值对数据
* @param pair 指向XPair指针的指针（存储键值对）
* @param this_map 关联的XMapBase容器
* @note 内部调用键和值的释放方法，最终删除XPair实例
*/
void XMapBase_deleteNodeData(XPair** pair, XMapBase* this_map);

#ifdef __cplusplus
}
#endif
#endif// !XMap_H