#include "XContainer.h"
#if !defined(XHASHSET_H) && XHashSet_ON
#define XHASHSET_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XFunctionCallback.h"
#include "XSetBase.h"
#include "XHashFunc.h"
#include "XHashSet_iterator.h"
/**
* @brief 哈希集合默认初始容量
* @note 初始哈希表的桶数量，默认为16
*/
#define DEFAULT_CAPACITY 16
/**
* @brief 哈希集合默认负载因子阈值
* @note 当元素数量与容量的比值超过此阈值（0.75）时，触发哈希表扩容
*/
#define DEFAULT_LOAD_FACTOR 0.75f
/**
* @brief XHashSet容器虚函数表大小定义
* @note 基于XSetBase基类的虚函数表大小扩展，确保接口兼容性与多态支持
*/
#define XHASHSET_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSetBase))
/**
* @brief XHashSet结构体定义（哈希集合容器）
* @details 继承自XSetBase，通过哈希表实现高效的键存储，支持快速插入、删除和查找
* @param m_class 继承自XSetBase的基础数据成员，包含键类型信息、大小、容量等
* @param m_hash 哈希函数，用于计算键的哈希值，确定元素在哈希表中的存储位置
*/
typedef struct XHashSet
{
	XSetBase      m_class;   ///< 基础数据成员，继承自XSetBase
	XHashFunc     m_hash;    ///< 哈希函数，用于计算键的哈希值
} XHashSet;
// ------------------------------ 类初始化与虚函数表 ------------------------------
/**
* @brief 初始化XHashSet的虚函数表
* @return 初始化完成的虚函数表指针XVtable*，失败返回NULL
* @note 绑定XHashSet的虚函数实现（插入、删除、查找等），继承并扩展XSetBase接口
*/
XVtable* XHashSet_class_init();
// ------------------------------ 实例创建与初始化 ------------------------------
/**
* @brief 创建XHashSet实例
* @param keyTypeSize 键的类型大小（字节数）
* @param hash 哈希函数（用于计算键的哈希值，非NULL）
* @param compare 键的比较函数（用于冲突处理和相等性判断，非NULL）
* @return 创建成功的XHashSet实例指针，失败返回NULL
* @note 动态分配内存并调用XHashSet_init完成初始化，需确保参数有效
*/
XHashSet* XHashSet_create_ex(const size_t keyTypeSize, XHashFunc hash, XCompare compare, bool useCow);
#define XHashSet_create(keyTypeSize, hash, compare) XHashSet_create_ex(keyTypeSize, hash, compare, true)
/**
* @brief 类型安全的XHashSet创建宏
* @param keyType 键的数据类型（如int、XString）
* @param compare 键的比较函数
* @return 创建成功的XHashSet实例指针，失败返回NULL
* @note 自动推导键的类型大小，哈希函数默认使用XHashMap_murmur3_32
*/
#define XHashSet_Create(keyType, compare) XHashSet_create(sizeof(keyType), XHash_xxhash64, compare);
/**
* @brief 初始化XHashSet实例
* @param this_set 待初始化的XHashSet实例指针（需提前分配内存）
* @param keyTypeSize 键的类型大小（字节数）
* @param hash 哈希函数（非NULL）
* @param compare 键的比较函数（非NULL）
* @note 初始化基础数据成员、绑定虚函数表，设置初始容量和哈希函数
*/
void XHashSet_init(XHashSet* this_set, const size_t keyTypeSize, XHashFunc hash, XCompare compare, bool useCow);
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 插入键（拷贝语义，基础版本）
* @note 基于XSetBase的接口，拷贝键值到集合中，键已存在则插入失败
*/
#define XHashSet_insert_base                XSetBase_insert_base
/**
* @brief 插入键（移动语义，基础版本）
* @note 基于XSetBase的接口，转移键的所有权到集合中，键已存在则插入失败
*/
#define XHashSet_insert_move_base           XSetBase_insert_move_base
// ------------------------------ 删除操作 ------------------------------
/**
* @brief 通过迭代器删除元素
* @note 基于XSetBase的接口，删除迭代器指向的元素，返回下一个有效迭代器
*/
#define XHashSet_erase_base                 XSetBase_erase_base
/**
* @brief 通过键删除元素（基础版本）
* @note 基于XSetBase的接口，删除与指定键匹配的元素，成功返回true
*/
#define XHashSet_remove_base                XSetBase_remove_base
// ------------------------------ 查找与包含判断 ------------------------------
/**
* @brief 通过键查找元素（基础版本）
* @note 基于XSetBase的接口，查找与指定键匹配的元素，返回迭代器
*/
#define XHashSet_find_base                  XSetBase_find_base
/**
* @brief 判断集合是否包含指定键
* @note 基于XSetBase的接口，包含指定键返回true，否则返回false
*/
#define XHashSet_contains                   XSetBase_contains
// ------------------------------ 键集合获取 ------------------------------
/**
* @brief 获取集合中所有键的向量（XVector）
* @note 基于XSetBase的接口，返回存储所有键副本的XVector，需用户自行释放
*/
#define XHashSet_keys_base                  XSetBase_keys_base
// ------------------------------ 容器属性查询 ------------------------------
/**
* @brief 判断集合是否为空
* @note 基于XSetBase的接口，元素数量为0时返回true
*/
#define XHashSet_isEmpty_base               XSetBase_isEmpty_base
/**
* @brief 获取集合中元素的数量
* @note 基于XSetBase的接口，返回当前存储的键的个数
*/
#define XHashSet_size_base                  XSetBase_size_base
/**
* @brief 获取集合的容量
* @note 基于XSetBase的接口，返回当前哈希表的桶数量（可容纳的最大元素数阈值）
*/
#define XHashSet_capacity_base              XSetBase_capacity_base
/**
* @brief 获取键的类型大小
* @note 基于XSetBase的接口，返回键的类型大小（字节数）
*/
#define XHashSet_typeSize_base              XSetBase_typeSize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 拷贝集合（深拷贝）
* @note 基于XSetBase的接口，复制源集合的所有元素和状态到当前集合
*/
#define XHashSet_copy_base                  XSetBase_copy_base
/**
* @brief 移动集合资源
* @note 基于XSetBase的接口，转移源集合的资源所有权到当前集合
*/
#define XHashSet_move_base                  XSetBase_move_base
/**
* @brief 反初始化集合
* @note 基于XSetBase的接口，释放内部资源但不销毁实例本身
*/
#define XHashSet_deinit_base                XSetBase_deinit_base
/**
* @brief 删除集合实例
* @note 基于XSetBase的接口，释放内部资源并销毁实例
*/
#define XHashSet_delete_base                XSetBase_delete_base
/**
* @brief 清空集合元素
* @note 基于XSetBase的接口，删除所有元素但保留集合结构
*/
#define XHashSet_clear_base                 XSetBase_clear_base
/**
* @brief 交换两个集合的内容
* @note 基于XSetBase的接口，快速交换两个集合的元素和状态
*/
#define XHashSet_swap_base                  XSetBase_swap_base

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：核心 API(insert/remove/find/contains/clear/swap)已与 Qt QSet 同名，天然对齐；
 *       此处仅补齐 QSet 的 count()/empty()/values() 命名。所有别名仅做命名映射，
 *       不新增行为，语义与被映射函数完全等价。Qt 参考: QSet<T>。
 *       语义提示：Set 中 key 即 value，故 values() 对应本项目 keys()。
 * ============================================================================== */

/**
 * @brief 元素个数——Qt 别名，等价于 XHashSet_size_base
 * @note Qt 映射: QSet::count()（与 size() 完全等价，仅命名不同）
 */
#define XHashSet_count_base            XHashSet_size_base

/**
 * @brief 判空——Qt 别名，等价于 XHashSet_isEmpty_base
 * @note Qt 映射: QSet::empty()（QSet 同时提供 isEmpty）
 */
#define XHashSet_empty_base            XHashSet_isEmpty_base

/**
 * @brief 获取所有元素副本——Qt 别名，等价于 XHashSet_keys_base
 * @note Qt 映射: QSet::values() 返回 QList<T>；本项目返回 XVector。
 */
#define XHashSet_values_base           XHashSet_keys_base

/**
 * @brief 只读语义查找——Qt 别名，等价于 XHashSet_find_base
 * @note Qt 映射: QSet::constFind()
 */
#define XHashSet_constFind_base          XHashSet_find_base

/**
 * @brief 按谓词条件删除元素——Qt 别名，等价于 XSetBase_removeIf_base
 * @note Qt 映射: QSet::removeIf(Pred)。返回被删除元素数量
 */
#define XHashSet_removeIf_base           XSetBase_removeIf_base

/**
 * @brief 条件删除自由函数别名——Qt 别名，等价于 XSetBase_removeIf_base
 * @note Qt 映射: erase_if(QSet<T>&, Pred)
 */
#define XHashSet_erase_if_base           XSetBase_removeIf_base

/**
 * @brief 谓词函数指针类型——Qt 别名，等价于 XSetBase_predicate
 * @note Qt 映射: QSet::removeIf 使用的 Pred 函数对象
 */
typedef XSetBase_predicate XHashSet_predicate;

/**
 * @brief 预分配桶容量（对齐 Qt QSet::reserve）
 * @param this_set XHashSet实例指针
 * @param size 期望容纳的元素数量下限
 * @return 分配成功返回 true；参数无效或分配失败返回 false
 * @note Qt 映射: QSet::reserve(qsizetype)。仅在期望容量大于当前桶数时触发扩容；
 *       扩容后桶数 >= size（哈希表按需保持负载因子上限，此处不缩小）。
 */
bool XHashSet_reserve_base(XHashSet* this_set, size_t size);

/**
 * @brief 释放多余桶空间（对齐 Qt QSet::squeeze）
 * @param this_set XHashSet实例指针
 * @note Qt 映射: QSet::squeeze()。将桶数下调至与元素数量相当的容量（不低于默认桶数 16），
 *       用于长期使用后释放冗余内存。
 */
void XHashSet_squeeze_base(XHashSet* this_set);


#ifdef __cplusplus
}
#endif
#endif // !XHashSet_H