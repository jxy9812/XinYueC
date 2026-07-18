#include"XContainer.h"
#if !defined(XMAP_H)&& XSet_ON
#define XMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XSetBase.h"
#include "XSet_iterator.h"
#include "XSet_reverse_iterator.h"
/**
* @brief 向前声明XVector结构体
*/
typedef struct XVector XVector;
/**
* @brief XSet容器虚函数表大小定义
* @note 基于XSetBase的虚函数表大小扩展，确保接口兼容性
*/
#define XSET_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))
/**
* @brief XSet结构体定义（集合容器）
* @details 继承自XSetBase，用于存储不重复的键值，支持键的比较和基本集合操作
* @param m_class 继承自XSetBase的基础数据成员，包含键类型信息、大小、容量等
*/
typedef struct XSet
{
	XSetBase m_class; ///< 基础数据成员，继承自XSetBase
} XSet;
// ------------------------------ 类初始化与虚函数表 ------------------------------
/**
* @brief 初始化XSet的虚函数表
* @return 初始化完成的虚函数表指针XVtable*，失败返回NULL
* @note 绑定XSet的虚函数实现，继承并扩展XSetBase的接口
*/
XVtable* XSet_class_init();
// ------------------------------ 实例创建与初始化 ------------------------------
/**
* @brief 创建XSet实例
* @param keyTypeSize 键的类型大小（字节数）
* @param compare 键的比较函数（用于判断键的相等性和排序）
* @return 创建成功的XSet实例指针，失败返回NULL
* @note 动态分配内存并调用XSet_init完成初始化，需确保keyTypeSize>0且compare不为NULL
*/
XSet* XSet_create_ex(const size_t keyTypeSize, XCompare compare, bool useCow);
#define XSet_create(keyTypeSize, compare) XSet_create_ex(keyTypeSize, compare, true)
/**
* @brief 类型安全的XSet创建宏
* @param keyType 键的数据类型（如int、float）
* @param compare 键的比较函数
* @return 创建成功的XSet实例指针，失败返回NULL
* @note 自动推导键的类型大小，简化XSet_create的调用
*/
#define XSet_Create(keyType, compare) XSet_create(sizeof(keyType), compare)
/**
* @brief 初始化XSet实例
* @param this_set 待初始化的XSet实例指针
* @param keyTypeSize 键的类型大小（字节数）
* @param compare 键的比较函数
* @note 初始化基础数据成员，绑定虚函数表，需确保参数有效
*/
void XSet_init(XSet* this_set, const size_t keyTypeSize, XCompare compare, bool useCow);
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 插入键（拷贝语义，基础版本）
* @note 基于XSetBase的接口，拷贝键值到集合中，键已存在则插入失败
*/
#define XSet_insert_base XSetBase_insert_base
/**
* @brief 插入指定类型的键（拷贝语义，类型安全）
* @note 基于XSetBase的接口，自动处理类型转换，拷贝键值到集合中
*/
#define XSet_Insert_Base XSetBase_Insert_Base
/**
* @brief 插入键（移动语义，基础版本）
* @note 基于XSetBase的接口，转移键的所有权到集合中，键已存在则插入失败
*/
#define XSet_insert_move_base XSetBase_insert_move_base
// ------------------------------ 删除操作 ------------------------------
/**
* @brief 通过迭代器删除元素
* @note 基于XSetBase的接口，删除迭代器指向的元素，返回下一个有效迭代器
*/
#define XSet_erase_base XSetBase_erase_base
/**
* @brief 通过键删除元素（基础版本）
* @note 基于XSetBase的接口，删除与指定键匹配的元素，成功返回true
*/
#define XSet_remove_base XSetBase_remove_base
/**
* @brief 删除指定类型的键对应的元素（类型安全）
* @note 基于XSetBase的接口，自动处理类型转换，删除匹配的元素
*/
#define XSet_Remove_Base XSetBase_Remove_Base
// ------------------------------ 查找与包含判断 ------------------------------
/**
* @brief 通过键查找元素（基础版本）
* @note 基于XSetBase的接口，查找与指定键匹配的元素，返回迭代器
*/
#define XSet_find_base XSetBase_find_base
/**
* @brief 判断集合是否包含指定键
* @note 基于XSetBase的接口，包含指定键返回true，否则返回false
*/
#define XSet_contains XSetBase_contains
// ------------------------------ 键集合获取 ------------------------------
/**
* @brief 获取集合中所有键的向量（XVector）
* @note 基于XSetBase的接口，返回存储所有键副本的XVector，需用户自行释放
*/
#define XSet_keys_base XSetBase_keys_base
// ------------------------------ 容器属性查询 ------------------------------
/**
* @brief 判断集合是否为空
* @note 基于XSetBase的接口，元素数量为0时返回true
*/
#define XSet_isEmpty_base XSetBase_isEmpty_base
/**
* @brief 获取集合中元素的数量
* @note 基于XSetBase的接口，返回当前存储的键的个数
*/
#define XSet_size_base XSetBase_size_base
/**
* @brief 获取集合的容量
* @note 基于XSetBase的接口，返回当前可容纳的最大元素数
*/
#define XSet_capacity_base XSetBase_capacity_base
/**
* @brief 获取键的类型大小
* @note 基于XSetBase的接口，返回键的类型大小（字节数）
*/
#define XSet_typeSize_base XSetBase_typeSize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 拷贝集合（深拷贝）
* @note 基于XSetBase的接口，复制源集合的所有元素和状态到当前集合
*/
#define XSet_copy_base XSetBase_copy_base
/**
* @brief 移动集合资源
* @note 基于XSetBase的接口，转移源集合的资源所有权到当前集合
*/
#define XSet_move_base XSetBase_move_base
/**
* @brief 反初始化集合
* @note 基于XSetBase的接口，释放内部资源但不销毁实例本身
*/
#define XSet_deinit_base XSetBase_deinit_base
/**
* @brief 删除集合实例
* @note 基于XSetBase的接口，释放内部资源并销毁实例
*/
#define XSet_delete_base XSetBase_delete_base
/**
* @brief 清空集合元素
* @note 基于XSetBase的接口，删除所有元素但保留集合结构
*/
#define XSet_clear_base XSetBase_clear_base
/**
* @brief 交换两个集合的内容
* @note 基于XSetBase的接口，快速交换两个集合的元素和状态
*/
#define XSet_swap_base XSetBase_swap_base

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：核心 API(insert/remove/find/contains/clear/swap)已与 Qt QSet 同名，天然对齐；
 *       此处仅补齐 QSet 的 count()/empty()/values() 命名。所有别名仅做命名映射，
 *       不新增行为，语义与被映射函数完全等价。Qt 参考: QSet<T>。
 *       语义提示：Set 中 key 即 value，故 values() 对应本项目 keys()。
 * ============================================================================== */

/**
 * @brief 元素个数——Qt 别名，等价于 XSet_size_base
 * @note Qt 映射: QSet::count()（与 size() 完全等价，仅命名不同）
 */
#define XSet_count_base            XSet_size_base

/**
 * @brief 判空——Qt 别名，等价于 XSet_isEmpty_base
 * @note Qt 映射: QSet::empty()（QSet 同时提供 isEmpty）
 */
#define XSet_empty_base            XSet_isEmpty_base

/**
 * @brief 获取所有元素副本——Qt 别名，等价于 XSet_keys_base
 * @note Qt 映射: QSet::values() 返回 QList<T>；本项目返回 XVector。
 */
#define XSet_values_base           XSet_keys_base

/**
 * @brief 只读语义查找——Qt 别名，等价于 XSet_find_base
 * @note Qt 映射: QSet::constFind()
 */
#define XSet_constFind_base          XSet_find_base

/**
 * @brief 按谓词条件删除元素——Qt 别名，等价于 XSetBase_removeIf_base
 * @note Qt 映射: QSet::removeIf(Pred)。返回被删除元素数量
 */
#define XSet_removeIf_base           XSetBase_removeIf_base

/**
 * @brief 条件删除自由函数别名——Qt 别名，等价于 XSet_removeIf_base
 * @note Qt 映射: erase_if(QSet<T>&, Pred)
 */
#define XSet_erase_if_base           XSetBase_removeIf_base

/**
 * @brief 谓词函数指针类型——Qt 别名，等价于 XSetBase_predicate
 * @note Qt 映射: QSet::removeIf 使用的 Pred 函数对象
 */
typedef XSetBase_predicate XSet_predicate;

/**
 * @brief 预分配容量——Qt 别名，红黑树实现下为无操作
 * @param this_set XSet实例指针
 * @param size 期望容量（本实现忽略此值）
 * @return 恒返回 true
 * @note Qt 映射: QSet::reserve(qsizetype)。本项目 XSet 底层为红黑树，节点按需分配，
 *       无预留桶概念，故此接口仅作 API 命名对齐，不做实际预分配。
 */
static inline bool XSet_reserve_base(XSet* this_set, size_t size) { (void)this_set; (void)size; return true; }

/**
 * @brief 收缩容量——Qt 别名，红黑树实现下为无操作
 * @note Qt 映射: QSet::squeeze()。同 reserve，红黑树无冗余桶可收缩。
 */
static inline void XSet_squeeze_base(XSet* this_set) { (void)this_set; }


#ifdef __cplusplus
}
#endif
#endif // !XSET_H