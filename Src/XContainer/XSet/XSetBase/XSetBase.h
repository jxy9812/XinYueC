#include "XContainer.h"
#if !defined(XSETBASE_H) && XSet_ON
#define XSETBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XCompare.h"
#include "XSetBase_iterator.h"

/**
* @brief XSetBase容器虚函数表大小定义
* @note 基于XContainer的虚函数表大小扩展，用于确定Set基类虚函数表的容量
*/
#define XSETBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))

/**
* @brief XSetBase虚函数表枚举定义
* @note 用于标识XSetBase容器的各类虚函数，继承自XContainer，定义Set特有的操作接口
*/
XCLASS_DEFINE_BEGING(XSetBase)
XCLASS_DEFINE_ENUM(XSetBase, Insert) = XCLASS_VTABLE_GET_SIZE(XContainer),  // 插入元素（键）
XCLASS_DEFINE_ENUM(XSetBase, Erase),                                           // 通过迭代器删除元素
XCLASS_DEFINE_ENUM(XSetBase, Remove),                                          // 通过键删除元素
XCLASS_DEFINE_ENUM(XSetBase, Find),                                            // 通过键查找元素
XCLASS_DEFINE_ENUM(XSetBase, Keys),                                            // 获取所有键的集合
XCLASS_DEFINE_END(XSetBase)

/**
* @brief XSetBase结构体定义（集合容器基类）
* @note 继承自XContainer，存储不重复的键值，支持键的比较和基本集合操作
*/
typedef struct XSetBase
{
	XContainer m_class;  // 继承自容器基类，存储键类型信息及基础容器数据（如大小、容量等）
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
void XSetBase_init(XSetBase* this_set, const size_t keyTypeSize, XCompare compare, bool useCow);

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

// ========================= 条件删除 =========================

/**
* @brief 条件删除函数指针类型
* @param pvKey 当前元素键的只读指针
* @param args  透传给谓词的额外参数
* @return 返回 true 表示该键需要删除，false 表示保留
*/
typedef bool (*XSetBase_predicate)(const void* pvKey, void* args);

/**
* @brief 按谓词条件批量删除元素（Qt QSet::removeIf 对齐）
* @param this_set 目标XSetBase
* @param pred 谓词函数（对每个键调用；返回true则删除）
* @param args 透传给谓词的用户参数
* @return 被删除的元素数量；参数无效返回0
* @note Qt 映射: QSet::removeIf(Pred) 及自由函数 erase_if(set,pred)。
*       实现细节：先通过 keys_base 拷贝出全部键的副本，再逐个 remove_base，
*       避免在原容器上边迭代边删除引发迭代器失效。
*/
size_t XSetBase_removeIf_base(XSetBase* this_set, XSetBase_predicate pred, void* args);

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
* @brief 拷贝容器（继承自XContainer）
* @note 宏定义，等价于XContainer_copy_base，实现容器的深拷贝
*/
#define XSetBase_copy_base				    XContainer_copy_base	

/**
* @brief 移动容器资源（继承自XContainer）
* @note 宏定义，等价于XContainer_move_base，转移容器资源所有权
*/
#define XSetBase_move_base				    XContainer_move_base	

/**
* @brief 释放容器资源（继承自XContainer）
* @note 宏定义，等价于XContainer_deinit_base，释放内部资源但不释放容器本身
*/
#define XSetBase_deinit_base				XContainer_deinit_base	

/**
* @brief 删除容器实例（继承自XContainer）
* @note 宏定义，等价于XContainer_delete_base，释放资源并销毁容器
*/
#define XSetBase_delete_base				XContainer_delete_base	

/**
* @brief 清空容器元素（继承自XContainer）
* @note 宏定义，等价于XContainer_clear_base，删除所有元素但保留容器结构
*/
#define XSetBase_clear_base				    XContainer_clear_base	

/**
* @brief 判断容器是否为空（继承自XContainer）
* @note 宏定义，等价于XContainer_isEmpty_base，元素数量为0时返回true
*/
#define XSetBase_isEmpty_base				XContainer_isEmpty_base	

/**
* @brief 获取容器元素数量（继承自XContainer）
* @note 宏定义，等价于XContainer_size_base，返回当前元素个数
*/
#define XSetBase_size_base				    XContainer_size_base	

/**
* @brief 获取容器容量（继承自XContainer）
* @note 宏定义，等价于XContainer_capacity_base，返回当前可容纳的最大元素数
*/
#define XSetBase_capacity_base			    XContainer_capacity_base

/**
* @brief 交换两个容器内容（继承自XContainer）
* @note 宏定义，等价于XContainer_swap_base，交换两个容器的元素和状态
*/
#define XSetBase_swap_base				    XContainer_swap_base	

/**
* @brief 获取键的类型大小（继承自XContainer）
* @note 宏定义，等价于XContainer_typeSize_base，返回键的类型大小（字节数）
*/
#define XSetBase_typeSize_base			    XContainer_typeSize_base


/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：本文件的核心 API(insert/remove/find/contains/clear/swap)已与 Qt QSet 同名，
 *       天然对齐；此处仅补齐 QSet 的 count()/empty()/values() 命名。所有别名仅做
 *       命名映射，不新增行为，语义与被映射函数完全等价。
 *       Qt 参考: qtbase/src/corelib/tools/qset.h（QSet<T>）。
 *       语义提示：在 Set 中 key 即 value，故 values() 对应本项目的 keys()。
 * ============================================================================== */

/**
 * @brief 元素个数——Qt 别名，等价于 XSetBase_size_base
 * @param this_set 集合实例指针
 * @return 元素个数
 * @note Qt 映射: QSet::count()（与 size() 完全等价，仅命名不同）
 */
#define XSetBase_count_base           XSetBase_size_base

/**
 * @brief 判空——Qt 别名，等价于 XSetBase_isEmpty_base
 * @param this_set 集合实例指针
 * @return 集合为空返回 true，否则返回 false
 * @note Qt 映射: QSet::empty()（QSet 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XSetBase_empty_base           XSetBase_isEmpty_base

/**
 * @brief 获取所有元素副本——Qt 别名，等价于 XSetBase_keys_base
 * @param this_set 集合实例指针
 * @return 存储所有元素副本的 XVector 指针，失败返回 NULL；调用方负责释放
 * @note Qt 映射: QSet::values() 返回 QList<T>；本项目返回 XVector（元素类型为 key）。
 *       在 Set 语义下 key 即 value，此别名仅是命名对齐。
 */
#define XSetBase_values_base          XSetBase_keys_base

/**
 * @brief 只读语义查找——Qt 别名，等价于 XSetBase_find_base
 * @note Qt 映射: QSet::constFind()（返回 const_iterator 语义；本项目迭代器无 const 分身，故直接复用）
 */
#define XSetBase_constFind_base       XSetBase_find_base

/**
 * @brief 条件删除自由函数别名——Qt 别名，等价于 XSetBase_removeIf_base
 * @note Qt 映射: erase_if(QSet<T>&, Pred)（全局自由函数；与成员 removeIf 语义相同）
 */
#define XSetBase_erase_if_base        XSetBase_removeIf_base


#ifdef __cplusplus
}
#endif
#endif // !XSet_H