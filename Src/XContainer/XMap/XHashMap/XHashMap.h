/**
* @file       XHashMap.h
* @brief      XHashMap 哈希映射容器公开 API。
* @details    对齐 Qt 6.8 QHash<K,V>；继承 XMapBase，基于哈希桶数组实现键值对
*             存储，提供插入、删除、查找、扩容与收缩等操作。实现只依赖
*             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
*/
#ifndef XHASHMAP_H
#define XHASHMAP_H
#ifdef __cplusplus
extern "C" {
#endif

/* 条件编译开关（第③段）：XHashMap_ON 由 CXinYueConfig.h 统一定义，
 * 此处仅兜底默认值，保证开关先于包含依赖生效。 */
#ifndef XHashMap_ON
#define XHashMap_ON 1
#endif

#if XHashMap_ON

/**
* @brief 包含必要的头文件
* @note 提供函数回调、映射基类、哈希函数、迭代器等核心功能支持
*/
#include "XContainer.h"           ///< 全局配置与容器基类（经其引入 CXinYueConfig.h）
#include "XFunctionCallback.h"    ///< 包含函数回调相关定义
#include "XMapBase.h"             ///< 包含映射基类XMapBase的定义，XHashMap继承自此基类
#include "XCryptographic.h"       ///< 包含统一哈希算法定义
#include "XHashMap_iterator.h"    ///< 包含XHashMap迭代器定义
/**
* @brief 宏定义：默认初始容量
* @note XHashMap的默认初始桶数量，用于哈希表初始化
*/
#define DEFAULT_CAPACITY 16
/**
* @brief 宏定义：默认负载因子阈值
* @note 当元素数量与容量的比值超过此阈值时，触发哈希表扩容
*/
#define DEFAULT_LOAD_FACTOR 0.75f
/**
* @brief XHashMap 虚函数表枚举定义（第⑥段）
* @note 仅继承 XMapBase、不新增虚函数，按规范使用 EXTEND_END 直接以父类收尾
*/
XCLASS_DEFINE_BEGING(XHashMap)
XCLASS_DEFINE_EXTEND_END(XHashMap, XMapBase)
/**
* @brief XHashMap结构体定义（哈希映射容器）
* @details 继承自映射基类XMapBase，通过哈希表实现键值对存储，支持高效的插入、删除和查找操作
*/
typedef struct XHashMap
{
	XMapBase m_class;  ///< 第一个成员，由 XClass 统一管理，禁止手工修改；包含键值类型信息、比较函数及基础容器数据
	XHashFunc m_hash;  ///< 哈希函数，用于计算键的哈希值，确定元素存储位置
} XHashMap;
// ------------------------------ 类初始化与虚函数表 ------------------------------
/**
* @brief 初始化XHashMap的虚函数表
* @return 初始化完成的虚函数表指针（XVtable*）
* @note 为XHashMap注册各类操作的虚函数（插入、删除、查找等），实现多态特性
*/
XVtable* XHashMap_class_init();
// ------------------------------ 创建与初始化 ------------------------------
/**
* @brief 创建XHashMap实例
* @param keyTypeSize 键的类型大小（字节数，如sizeof(int)）
* @param valTypeSize 值的类型大小（字节数，如sizeof(float)）
* @param hash 哈希函数（用于计算键的哈希值，非NULL）
* @param compare 键的比较函数（用于冲突处理和查找，非NULL）
* @return 成功返回创建的XHashMap指针（XHashMap*），失败返回NULL
* @note 需确保keyTypeSize、valTypeSize大于0，hash和compare不为NULL
*/
XHashMap* XHashMap_create_ex(XMemoryType memory, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XCompare compare, bool useCow);
#define XHashMap_create(keyTypeSize, valTypeSize, hash, compare) \
	XHashMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, keyTypeSize, valTypeSize, hash, compare, true)
/**
* @brief 拷贝创建XHashMap实例
* @param other 待拷贝的XHashMap实例指针
* @return 成功返回新创建的XHashMap指针（拷贝了other的内容），失败返回NULL
* @note 若other为NULL，直接返回NULL；新实例与原实例内容独立
*/
XHashMap* XHashMap_create_copy(const XHashMap* other);
/**
* @brief 移动创建XHashMap实例（转移所有权）
* @param other 待移动的XHashMap实例指针
* @return 成功返回新创建的XHashMap指针（接管other的资源），失败返回NULL
* @note 移动后other的资源被转移，不应再访问
*/
XHashMap* XHashMap_create_move(XHashMap* other);
/**
* @brief 简化创建指定类型的XHashMap（类型安全宏）
* @param keyType 键的类型（如int、XString等）
* @param valType 值的类型（如float、XVariant等）
* @param compare 键的比较函数
* @return 调用XHashMap_create_ex创建的XHashMap指针，哈希函数默认使用XCryptographicHash_XxHash64（经XCryptographicHash_function获取）
* @note 自动推导键和值的类型大小，避免手动计算sizeof
*/
#define XHashMap_Create(keyType, valType, compare) \
    XHashMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(keyType), sizeof(valType), \
        XCryptographicHash_function(XCryptographicHash_XxHash64), compare, true)
/**
* @brief 初始化已分配内存的XHashMap实例
* @param this_map 待初始化的XHashMap指针（需提前分配内存）
* @param keyTypeSize 键的类型大小（字节数）
* @param valTypeSize 值的类型大小（字节数）
* @param hash 哈希函数（非NULL）
* @param compare 键的比较函数（非NULL）
* @note 需确保this_map不为NULL，keyTypeSize、valTypeSize大于0，hash和compare不为NULL
*/
void XHashMap_init(XHashMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XCompare compare, bool useCow);

/** @brief 深复制创建存储 XHashMap 的 XVariant。 */
XVariant* XHashMap_toVariant(const XHashMap* map);
/** @brief 移动创建存储 XHashMap 的 XVariant。 */
XVariant* XHashMap_toVariant_move(XHashMap* map);
/** @brief 将已有 XHashMap 直接交给 XVariant 管理。 */
XVariant* XHashMap_toVariant_ref(XHashMap* map);
/** @brief 从 XVariant 深复制取得 XHashMap。 */
XHashMap* XHashMap_fromVariant(const XVariant* var);
/** @brief 从 XVariant 借用取得 XHashMap。 */
XHashMap* XHashMap_fromVariant_ref(const XVariant* var);
/** @brief 深复制设置 XVariant 的 XHashMap 值。 */
void XHashMap_setVariant(XVariant* var, const XHashMap* map);
/** @brief 移动设置 XVariant 的 XHashMap 值。 */
void XHashMap_setVariant_move(XVariant* var, XHashMap* map);
/** @brief 将已有 XHashMap 直接交给 XVariant 管理。 */
void XHashMap_setVariant_ref(XVariant* var, XHashMap* map);

/* 兼容旧的 XVariant 扩展 API 名称；实际实现归属 XHashMap。 */
#define XVariant_create_hash        XHashMap_toVariant
#define XVariant_create_hash_move   XHashMap_toVariant_move
#define XVariant_create_hash_ref    XHashMap_toVariant_ref
#define XVariant_toHash             XHashMap_fromVariant
#define XVariant_toHash_ref         XHashMap_fromVariant_ref
#define XVariant_setValue_hash      XHashMap_setVariant
#define XVariant_setValue_hash_move XHashMap_setVariant_move
#define XVariant_setValue_hash_ref  XHashMap_setVariant_ref
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 插入键值对（拷贝语义，基础版本）
* @note 继承自XMapBase的插入操作，通过宏重命名实现接口统一
* @note 失败返回false时（参数无效或内存分配失败）哈希表不接管键/值，调用方保留所有权并负责释放；插入成功后键/值由哈希表接管，随条目按注册的释放回调（XMapBaseSetKeyDeinitMethod/XContainerSetDataDeinitMethod）释放。键已存在时覆盖旧值并返回true，不视为失败
*/
#define XHashMap_insert_base XMapBase_insert_base
// ------------------------------ 删除操作 ------------------------------
/**
* @brief 通过迭代器删除元素（基础版本）
* @note 继承自XMapBase的删除操作，删除后迭代器失效，可获取下一个迭代器
*/
#define XHashMap_erase_base XMapBase_erase_base
/**
* @brief 通过键删除元素（拷贝语义，基础版本）
* @note 继承自XMapBase的删除操作，删除与指定键匹配的元素
*/
#define XHashMap_remove_base XMapBase_remove_base
// ------------------------------ 元素访问 ------------------------------
/**
* @brief 通过键获取值的地址（基础版本）
* @note 继承自XMapBase的访问操作，返回与指定键匹配的值的指针
*/
#define XHashMap_value_base XMapBase_value_base
// ------------------------------ 查找操作 ------------------------------
/**
* @brief 通过键查找元素（基础版本）
* @note 继承自XMapBase的查找操作，返回找到的元素迭代器（未找到返回end迭代器）
*/
#define XHashMap_find_base XMapBase_find_base
// ------------------------------ 容器管理 ------------------------------
/* 反初始化/删除已统一：直接使用 XClassDeinit/XClassDelete，
 * 派生容器不再提供 *_deinit_base/*_delete_base 别名（见风格指南迁移说明）。 */
/**
* @brief 清空容器元素（基础版本）
* @note 继承自XMapBase的清空操作，删除所有元素但保留容器结构
*/
#define XHashMap_clear_base XMapBase_clear_base
/**
* @brief 判断容器是否为空（基础版本）
* @note 继承自XMapBase的判空操作，无元素返回true，否则返回false
*/
#define XHashMap_isEmpty_base XMapBase_isEmpty_base
/**
* @brief 获取容器元素数量（基础版本）
* @note 继承自XMapBase的大小操作，返回当前存储的元素个数
*/
#define XHashMap_size_base XMapBase_size_base
/**
* @brief 获取容器容量（基础版本）
* @note 继承自XMapBase的容量操作，返回容器当前的桶数量（哈希表容量）
*/
#define XHashMap_capacity_base XMapBase_capacity_base
/**
* @brief 交换两个容器的内容（基础版本）
* @note 继承自XMapBase的交换操作，快速交换两个容器的内部数据
*/
#define XHashMap_swap_base XMapBase_swap_base
/**
* @brief 获取容器存储值的类型大小（基础版本）
* @note 继承自XMapBase的类型大小操作，返回值类型的字节数
*/
#define XHashMap_typeSize_base XMapBase_typeSize_base
// ------------------------------ 特殊创建 ------------------------------
/**
* @brief 创建存储XString键和XVariant值的XHashMap实例
* @return 成功返回XVariantHashMap指针（XHashMap的特化类型），失败返回NULL
* @note 自动配置键（XString）和值（XVariant）的拷贝、移动、释放方法，适用于字符串到变体的映射
*/
XVariantHashMap* XHashMap_create_XVariantHashMap();
/**
* @brief C++兼容性声明结束
*/

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：核心 API 已与 Qt QHash 天然同名；此处补齐 count/empty/constFind/removeIf/
 *       erase_if 命名，并新增 reserve/squeeze 与 QHash::reserve()/squeeze() 对齐。
 *       Qt 参考: qtbase/src/corelib/tools/qhash.h（QHash<K,V>）
 * ============================================================================== */

/** @brief 元素个数——Qt 别名，等价于 XHashMap_size_base @note Qt: QHash::count() */
#define XHashMap_count_base           XHashMap_size_base
/** @brief 判空——Qt 别名，等价于 XHashMap_isEmpty_base @note Qt: QHash::empty() */
#define XHashMap_empty_base           XHashMap_isEmpty_base
/** @brief 只读语义查找——Qt 别名，等价于 XHashMap_find_base @note Qt: QHash::constFind() */
#define XHashMap_constFind_base       XHashMap_find_base
/** @brief 条件删除——Qt 别名，等价于 XMapBase_removeIf_base @note Qt: QHash::removeIf(Pred) */
#define XHashMap_removeIf_base        XMapBase_removeIf_base
/** @brief 条件删除自由函数别名——Qt: erase_if(QHash<K,V>&, Pred) */
#define XHashMap_erase_if_base        XMapBase_removeIf_base
/** @brief 谓词函数指针类型——Qt 别名，等价于 XMapBase_predicate */
typedef XMapBase_predicate XHashMap_predicate;

/**
 * @brief 预分配桶容量（对齐 Qt QHash::reserve）
 * @param this_map XHashMap 实例指针
 * @param size 期望容纳的元素数量下限
 * @return 分配成功返回 true；参数无效或分配失败返回 false
 * @note Qt 映射: QHash::reserve(qsizetype)。仅在期望容量大于当前桶数时触发扩容；
 *       桶数按 2 的幂上取，负载因子上限为 DEFAULT_LOAD_FACTOR。
 */
bool XHashMap_reserve(XHashMap* this_map, size_t size);

/**
 * @brief 释放多余桶空间（对齐 Qt QHash::squeeze）
 * @param this_map XHashMap 实例指针
 * @note Qt 映射: QHash::squeeze()。将桶数下调至与元素数量相当（不低于默认桶数 16）。
 */
void XHashMap_squeeze(XHashMap* this_map);

/* XClass create API default-memory wrappers. */
#undef XHashMap_create
#define XHashMap_create(keyTypeSize, valTypeSize, hash, compare) \
	XHashMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, keyTypeSize, valTypeSize, hash, compare, true)

#endif /* XHashMap_ON */

#ifdef __cplusplus
}
#endif

#endif /* XHASHMAP_H */
