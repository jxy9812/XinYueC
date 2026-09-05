#include"XContainer.h"
#if !defined(XMAP_H)&& XMap_ON
#define XMAP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XMapBase.h"                  ///< 包含映射基类XMapBase的定义，XMap继承自此基类
#include "XMap_iterator.h"             ///< 包含XMap正向迭代器定义
#include "XMap_reverse_iterator.h"     ///< 包含XMap反向迭代器定义
/**
* @brief 向前声明
* @note 声明XVector和XPair类型，避免循环依赖，具体定义在对应头文件中
*/
typedef struct XVector XVector;        ///< 向量容器类型向前声明
typedef struct XPair XPair;            ///< 键值对结构体类型向前声明
/**
* @brief XMap容器虚函数表大小定义
* @note 基于映射基类XMapBase的虚函数表大小扩展，确定XMap所需的虚函数表容量
*/
#define XMAP_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XMapBase))       // XMap容器虚函数表大小
/**
* @brief XMap结构体定义（映射容器）
* @details 继承自映射基类XMapBase，封装键值对存储功能，支持有序映射操作
*/
typedef struct XMap
{
	XMapBase m_class;  ///< 继承自映射基类，包含键值类型信息、比较函数及基础容器数据
} XMap;
// ------------------------------ 类初始化与虚函数表 ------------------------------
/**
* @brief 初始化XMap的虚函数表
* @return 初始化完成的虚函数表指针（XVtable*）
* @note 为XMap注册各类操作的虚函数（插入、删除、查找等），实现多态特性
*/
XVtable* XMap_class_init();
// ------------------------------ 创建与初始化 ------------------------------
/**
 * @brief 创建 XMap 实例（底层实现，可指定 COW 模式）
 * @param keyTypeSize 键的类型大小
 * @param valTypeSize 值的类型大小
 * @param compare 键的比较函数
 * @param useCow 是否启用 COW（true=COW，false=非COW）
 * @return 成功返回 XMap 指针，失败返回 NULL
 */
XMap* XMap_create_ex(XMemoryType memory, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare, bool useCow);

/**
 * @brief 创建 XMap 实例（默认使用 COW 模式）
 * @note 宏定义，展开为 XMap_create_ex(keyTypeSize, valTypeSize, compare, true)
 */
#define XMap_create(keyTypeSize, valTypeSize, compare) \
    XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, keyTypeSize, valTypeSize, compare, true)
/**
* @brief 拷贝创建XMap实例
* @param other 待拷贝的XMap实例指针
* @return 成功返回新创建的XMap指针（拷贝了other的内容），失败返回NULL
* @note 若other为NULL，直接返回NULL
*/
XMap* XMap_create_copy(const XMap* other);
/**
* @brief 移动创建XMap实例（转移所有权）
* @param other 待移动的XMap实例指针
* @return 成功返回新创建的XMap指针（接管other的资源），失败返回NULL
* @note 移动后other的资源被转移，不应再访问
*/
XMap* XMap_create_move(XMap* other);
/**
* @brief 简化创建指定类型的XMap（类型安全宏）
* @param keyType 键的类型（如int、XString等）
* @param valType 值的类型（如float、XVariant等）
* @param compare 键的比较函数
* @return 调用XMap_create创建的XMap指针
* @note 自动推导键和值的类型大小，避免手动计算sizeof
*/
#define XMap_Create(keyType, valType, compare) XMap_create(sizeof(keyType), sizeof(valType), compare)
/**
* @brief 初始化已分配内存的XMap实例
* @param this_map 待初始化的XMap指针（需提前分配内存）
* @param keyTypeSize 键的类型大小（字节数）
* @param valTypeSize 值的类型大小（字节数）
* @param compare 键的比较函数（非NULL）
* @note 需确保this_map不为NULL，keyTypeSize和valTypeSize大于0，compare不为NULL
*/
void XMap_init(XMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare, bool useCow);

/** @brief 深复制创建存储 XMap 的 XVariant。 */
XVariant* XMap_toVariant(const XMap* map);
/** @brief 移动创建存储 XMap 的 XVariant。 */
XVariant* XMap_toVariant_move(XMap* map);
/** @brief 将已有 XMap 直接交给 XVariant 管理。 */
XVariant* XMap_toVariant_ref(XMap* map);
/** @brief 从 XVariant 深复制取得 XMap。 */
XMap* XMap_fromVariant(const XVariant* var);
/** @brief 从 XVariant 借用取得 XMap。 */
XMap* XMap_fromVariant_ref(const XVariant* var);
/** @brief 深复制设置 XVariant 的 XMap 值。 */
void XMap_setVariant(XVariant* var, const XMap* map);
/** @brief 移动设置 XVariant 的 XMap 值。 */
void XMap_setVariant_move(XVariant* var, XMap* map);
/** @brief 将已有 XMap 直接交给 XVariant 管理。 */
void XMap_setVariant_ref(XVariant* var, XMap* map);

/* 兼容旧的 XVariant 扩展 API 名称；实际实现归属 XMap。 */
#define XVariant_create_map        XMap_toVariant
#define XVariant_create_map_move   XMap_toVariant_move
#define XVariant_create_map_ref    XMap_toVariant_ref
#define XVariant_toMap             XMap_fromVariant
#define XVariant_toMap_ref         XMap_fromVariant_ref
#define XVariant_setValue_map      XMap_setVariant
#define XVariant_setValue_map_move XMap_setVariant_move
#define XVariant_setValue_map_ref  XMap_setVariant_ref

/**
 * @brief 强制分离共享的 COW 存储。
 * @param this_map 需要分离的 XMap 实例。
 * @note 与 XVector_detach 语义一致；未启用 COW 或已独占时为空操作。
 */
void XMap_detach(XMap* this_map);

/**
 * @brief 判断 XMap 是否独占底层存储。
 * @param this_map 要检查的 XMap 实例。
 * @return 未启用 COW、存储为空或引用计数为 1 时返回 true。
 */
bool XMap_isDetached(const XMap* this_map);
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 插入键值对（拷贝语义，基础版本）
* @note 继承自XMapBase的插入操作，通过宏重命名实现接口统一
*/
#define XMap_insert_base              XMapBase_insert_base
/**
* @brief 插入键值对（移动语义，键和值均移动，基础版本）
* @note 继承自XMapBase的插入操作，减少数据拷贝开销
*/
#define XMap_insert_move_base         XMapBase_insert_move_base
/**
* @brief 插入键值对（移动语义，仅键移动，基础版本）
* @note 继承自XMapBase的插入操作，键转移所有权，值使用拷贝
*/
#define XMap_insert_keyMove_base      XMapBase_insert_keyMove_base
/**
* @brief 插入键值对（移动语义，仅值移动，基础版本）
* @note 继承自XMapBase的插入操作，值转移所有权，键使用拷贝
*/
#define XMap_insert_valueMove_base    XMapBase_insert_valueMove_base
/**
* @brief 插入指定类型的键值对（拷贝语义，类型安全宏）
* @note 继承自XMapBase的类型安全插入操作，自动处理类型转换
*/
#define XMap_Insert_Base              XMapBase_Insert_Base
// ------------------------------ 删除操作 ------------------------------
/**
* @brief 通过迭代器删除元素（基础版本）
* @note 继承自XMapBase的删除操作，删除后迭代器失效，可获取下一个迭代器
*/
#define XMap_erase_base               XMapBase_erase_base
/**
* @brief 通过键删除元素（拷贝语义，基础版本）
* @note 继承自XMapBase的删除操作，删除与指定键匹配的元素
*/
#define XMap_remove_base              XMapBase_remove_base
/**
* @brief 删除指定类型的键对应的元素（类型安全宏，拷贝语义）
* @note 继承自XMapBase的类型安全删除操作，自动处理类型转换
*/
#define XMap_Remove_Base              XMapBase_Remove_Base
// ------------------------------ 元素访问 ------------------------------
/**
* @brief 通过键获取值的地址（基础版本）
* @note 继承自XMapBase的访问操作，返回与指定键匹配的值的指针
*/
#define XMap_value_base               XMapBase_value_base
/**
* @brief 通过键获取指定类型的值（类型安全宏）
* @note 继承自XMapBase的类型安全访问操作，自动解引用值指针
*/
#define XMap_Value_Base               XMapBase_Value_Base
// ------------------------------ 查找与包含 ------------------------------
/**
* @brief 通过键查找元素（基础版本）
* @note 继承自XMapBase的查找操作，返回找到的元素迭代器（未找到返回end迭代器）
*/
#define XMap_find_base                XMapBase_find_base
/**
* @brief 判断容器是否包含指定键
* @note 继承自XMapBase的包含判断操作，存在返回true，否则返回false
*/
#define XMap_contains                 XMapBase_contains
// ------------------------------ 键集合 ------------------------------
/**
* @brief 获取容器中所有键的集合（XVector）
* @note 继承自XMapBase的键集合操作，返回存储所有键的向量（需用户自行释放）
*/
#define XMap_keys_base                XMapBase_keys_base
// ------------------------------ 其他操作 ------------------------------
/**
* @brief 反初始化容器（基础版本）
* @note 继承自XMapBase的反初始化操作，释放资源但不释放容器本身
*/
#define XMap_deinit_base              XMapBase_deinit_base
/**
* @brief 删除容器实例（基础版本）
* @note 继承自XMapBase的删除操作，释放资源并销毁容器实例
*/
#define XMap_delete_base              XMapBase_delete_base
/**
* @brief 清空容器元素（基础版本）
* @note 继承自XMapBase的清空操作，删除所有元素但保留容器结构
*/
#define XMap_clear_base               XMapBase_clear_base
/**
* @brief 判断容器是否为空（基础版本）
* @note 继承自XMapBase的判空操作，无元素返回true，否则返回false
*/
#define XMap_isEmpty_base             XMapBase_isEmpty_base
/**
* @brief 获取容器元素数量（基础版本）
* @note 继承自XMapBase的大小操作，返回当前存储的元素个数
*/
#define XMap_size_base                XMapBase_size_base
/**
* @brief 获取容器容量（基础版本）
* @note 继承自XMapBase的容量操作，返回容器可容纳的元素上限
*/
#define XMap_capacity_base            XMapBase_capacity_base
/**
* @brief 交换两个容器的内容（基础版本）
* @note 继承自XMapBase的交换操作，快速交换两个容器的内部数据
*/
#define XMap_swap_base                XMapBase_swap_base
/**
* @brief 获取容器存储值的类型大小（基础版本）
* @note 继承自XMapBase的类型大小操作，返回值类型的字节数
*/
#define XMap_typeSize_base            XMapBase_typeSize_base
// ------------------------------ 特殊创建 ------------------------------
/**
* @brief 创建存储XString键和XVariant值的XMap实例
* @return 成功返回XVariantMap指针（XMap的特化类型），失败返回NULL
* @note 自动配置键和值的拷贝、移动、释放方法，适用于字符串到变体的映射
*/
XVariantMap* XMap_create_XVariantMap();
/**
* @brief C++兼容性声明结束
*/

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：本文件核心 API 已与 Qt QMap 天然同名；此处仅补齐 count/empty/constFind/
 *       removeIf/erase_if 的命名，并对 reserve/squeeze 提供 no-op 桩以对齐 API
 *       （QMap 底层为红黑树，Qt QMap 本身亦无 reserve/squeeze，此处保留以便与
 *       QHash 一致地测试同名 API）。
 * ============================================================================== */

/** @brief 元素个数——Qt 别名，等价于 XMap_size_base @note Qt: QMap::count() */
#define XMap_count_base           XMap_size_base
/** @brief 判空——Qt 别名，等价于 XMap_isEmpty_base @note Qt: QMap::empty() */
#define XMap_empty_base           XMap_isEmpty_base
/** @brief 只读语义查找——Qt 别名，等价于 XMap_find_base @note Qt: QMap::constFind() */
#define XMap_constFind_base       XMap_find_base
/** @brief 条件删除——Qt 别名，等价于 XMapBase_removeIf_base @note Qt: QMap::removeIf(Pred) */
#define XMap_removeIf_base        XMapBase_removeIf_base
/** @brief 条件删除自由函数别名——Qt: erase_if(QMap<K,V>&, Pred) */
#define XMap_erase_if_base        XMapBase_removeIf_base
/** @brief 谓词函数指针类型——Qt 别名，等价于 XMapBase_predicate */
typedef XMapBase_predicate XMap_predicate;

/**
 * @brief 预分配容量——红黑树实现下为无操作
 * @param this_map XMap 实例指针
 * @param size 期望容量（本实现忽略此值）
 * @return 恒返回 true
 * @note QMap 底层为红黑树，节点按需分配，Qt 本身未提供 reserve；此桩仅用于跨容器统一测试。
 */
static inline bool XMap_reserve_base(XMap* this_map, size_t size) { (void)this_map; (void)size; return true; }

/**
 * @brief 收缩容量——红黑树实现下为无操作
 * @note 同 reserve，红黑树无冗余桶可收缩。
 */
static inline void XMap_squeeze_base(XMap* this_map) { (void)this_map; }

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XMap_create
#define XMap_create(keyTypeSize, valTypeSize, compare) \
	XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, keyTypeSize, valTypeSize, compare, true)

#endif // !XMAP_H
