/**
 * @file       XVariantList.h
 * @brief      变体列表容器公开 API。
 * @details    对齐 Qt QVariantList；继承 XVector，存储 XVariant 类型元素，
 *             提供与 XVariant 的互转接口。实现只依赖 XinYueC 抽象层，禁止调用
 *             Win32、POSIX、Qt 或其他平台 API。
 */
#include"CXinYueConfig.h"
#if !defined(XVARIANTLIST_H)&& XVariantList_ON
#define XVARIANTLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XVector.h"
#include "XVariant.h"
#include "XVariantList_iterator.h"
#include "XVariantList_reverse_iterator.h"
/**
* @brief XVariantList容器虚函数表大小定义
* @note 基于XVector基类的虚函数表大小扩展，确保接口兼容性与多态支持
*/
#define XVARIANTLIST_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XVariantList))
/**
* @brief XVariantList虚函数表枚举定义
* @note 声明XVariantList的虚函数表结构，继承自XVector
*/
XCLASS_DEFINE_BEGING(XVariantList)
XCLASS_DEFINE_EXTEND_END(XVariantList, XVector)
/**
* @brief XVariantList结构体定义（变体列表容器）
* @details 继承自XVector，专门用于存储XVariant类型元素，支持动态扩容与变体元素操作
* @param m_vector 继承自XVector的基础数据成员，包含元素存储、大小、容量等信息
*/
typedef struct XVariantList
{
	XVector m_vector; ///< 基础数据成员，继承自XVector
} XVariantList;
// ------------------------------ 类初始化与虚函数表 ------------------------------
/**
* @brief 初始化XVariantList的虚函数表
* @return 初始化完成的虚函数表指针XVtable*，失败返回NULL
* @note 绑定XVariantList的虚函数实现，继承并扩展XVector的接口
*/
XVtable* XVariantList_class_init();
// ------------------------------ 实例创建与初始化 ------------------------------
/**
* @brief 创建XVariantList实例
* @param memory 实例绑定的内存池类型（XMemoryType，如XMEMORY_TYPE_SYSTEM），决定实例内存使用哪族分配器
* @return 创建成功的XVariantList实例指针，失败返回NULL
* @note 动态分配内存并调用XVariantList_init完成初始化，初始化为空列表
*/
XVariantList* XVariantList_create_ex(XMemoryType memory);
/**
* @brief 拷贝构造创建XVariantList实例
* @param other 待拷贝的XVariantList实例指针
* @return 新创建的XVariantList实例指针（深拷贝），失败返回NULL
* @note 复制other中的所有元素到新实例，新实例与原实例独立
*/
XVariantList* XVariantList_create_copy(const XVariantList* other);
/**
* @brief 移动构造创建XVariantList实例
* @param other 待移动的XVariantList实例指针
* @return 新创建的XVariantList实例指针（转移资源所有权），失败返回NULL
* @note 转移other的资源到新实例，other之后变为空状态
*/
XVariantList* XVariantList_create_move(XVariantList* other);
/**
* @brief 初始化XVariantList实例
* @param list 待初始化的XVariantList实例指针（需提前分配内存）
* @note 初始化基础数据成员、绑定虚函数表，设置元素类型为XVariant
*/
void XVariantList_init(XVariantList* list);

/**
* @brief 深复制创建存储 XVariantList 的 XVariant
* @param list 源XVariantList实例指针（借用，只读，函数不取得其所有权也不修改它）
* @return 新创建的XVariant堆对象指针（内部持有list的深拷贝），list为NULL或分配失败返回NULL
* @note 返回的XVariant需由调用方通过XClassDelete释放；list保持不变
*/
XVariant* XVariantList_toVariant(const XVariantList* list);
/**
* @brief 移动创建存储 XVariantList 的 XVariant
* @param list 源XVariantList实例指针（其元素资源被移入新XVariant）
* @return 新创建的XVariant堆对象指针（内部持有转移而来的资源），list为NULL或分配失败返回NULL
* @note 返回的XVariant需由调用方通过XClassDelete释放；移动后list变为空状态，list结构体本身仍由调用方管理
*/
XVariant* XVariantList_toVariant_move(XVariantList* list);
/**
* @brief 将已有 XVariantList 直接交给 XVariant 管理
* @param list 待移交的XVariantList实例指针（成功后其生命周期由返回的XVariant管理）
* @return 新创建的XVariant堆对象指针（不拷贝，直接引用list），list为NULL或分配失败返回NULL
* @note 返回的XVariant需由调用方通过XClassDelete释放；移交后调用方不得再自行释放list
*/
XVariant* XVariantList_toVariant_ref(XVariantList* list);
/**
* @brief 从 XVariant 深复制取得 XVariantList
* @param var 源XVariant指针（借用，只读）；类型须为XVariantType_List
* @return 新创建的XVariantList堆对象指针（var内部数据的深拷贝），var为NULL、类型不匹配或分配失败返回NULL
* @note 返回的XVariantList需由调用方通过XClassDelete释放；var保持不变
*/
XVariantList* XVariantList_fromVariant(const XVariant* var);
/**
* @brief 从 XVariant 借用取得 XVariantList
* @param var 源XVariant指针；类型须为XVariantType_List
* @return 借用指针，指向var内部存储的XVariantList（不拷贝）；var为NULL、类型不匹配或数据为空返回NULL
* @note 返回指针归var所有，不得释放；var被重设或销毁后失效
*/
XVariantList* XVariantList_fromVariant_ref(const XVariant* var);
/**
* @brief 深复制设置 XVariant 的 XVariantList 值
* @param var 目标XVariant指针（借用，可修改）；类型将被置为XVariantType_List
* @param list 源XVariantList实例指针（借用，只读，函数不取得其所有权）
* @return 无；var或list为NULL时不执行任何操作，var保持不变
* @note var原持有的数据资源会被释放并替换为list的深拷贝；list保持不变
*/
void XVariantList_setVariant(XVariant* var, const XVariantList* list);
/**
* @brief 移动设置 XVariant 的 XVariantList 值
* @param var 目标XVariant指针（借用，可修改）；类型将被置为XVariantType_List
* @param list 源XVariantList实例指针（其元素资源被移入var）
* @return 无；var或list为NULL时不执行任何操作
* @note var原持有的数据资源会被释放并替换为转移而来的资源；移动后list变为空状态，list结构体本身仍由调用方管理
*/
void XVariantList_setVariant_move(XVariant* var, XVariantList* list);
/**
* @brief 将已有 XVariantList 直接交给 XVariant 管理
* @param var 目标XVariant指针（借用，可修改）
* @param list 待移交的XVariantList实例指针（成功后其生命周期由var管理）
* @return 无；var或list为NULL时不执行任何操作
* @note 不拷贝，var直接引用list；移交后调用方不得再自行释放list
*/
void XVariantList_setVariant_ref(XVariant* var, XVariantList* list);

/* 兼容旧的 XVariant 扩展 API 名称；实际实现归属 XVariantList。 */
#define XVariant_create_list       XVariantList_toVariant
#define XVariant_create_list_move  XVariantList_toVariant_move
#define XVariant_create_list_ref   XVariantList_toVariant_ref
#define XVariant_toList             XVariantList_fromVariant
#define XVariant_toList_ref         XVariantList_fromVariant_ref
#define XVariant_setValue_list      XVariantList_setVariant
#define XVariant_setValue_list_move XVariantList_setVariant_move
#define XVariant_setValue_list_ref  XVariantList_setVariant_ref
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 向前端插入元素（拷贝语义，基础版本）
* @note 基于XVector的接口，拷贝XVariant元素到列表前端
*/
#define XVariantList_push_front_base		XVector_push_front_1_base
/**
* @brief 向前端插入元素（移动语义，基础版本）
* @note 基于XVector的接口，转移XVariant元素所有权到列表前端
*/
#define XVariantList_push_front_move_base XVector_push_front_move_1_base
/**
* @brief 向后端插入元素（拷贝语义，基础版本）
* @note 基于XVector的接口，拷贝XVariant元素到列表后端
*/
#define XVariantList_push_back_base		XVector_push_back_1_base
/**
* @brief 向后端插入元素（移动语义，基础版本）
* @note 基于XVector的接口，转移XVariant元素所有权到列表后端
*/
#define XVariantList_push_back_move_base XVector_push_back_move_1_base
/**
* @brief 在指定位置插入元素（拷贝语义）
* @note 基于XVector的接口，拷贝XVariant元素到指定索引位置
*/
#define XVariantList_insert				XVector_insert_2
/**
* @brief 在指定位置插入元素（移动语义）
* @note 基于XVector的接口，转移XVariant元素所有权到指定索引位置
*/
#define XVariantList_insert_move		XVector_insert_move_2
// ------------------------------ 访问操作 ------------------------------
/**
* @brief 获取指定索引的元素（基础版本）
* @note 基于XVector的接口，返回指定索引处的XVariant元素指针
*/
#define XVariantList_at_base			XVector_at_base
/**
* @brief 获取前端元素（基础版本）
* @note 基于XVector的接口，返回列表第一个XVariant元素指针
*/
#define XVariantList_front_base			XVector_front_base
/**
* @brief 获取后端元素（基础版本）
* @note 基于XVector的接口，返回列表最后一个XVariant元素指针
*/
#define XVariantList_back_base			XVector_back_base
// ------------------------------ 查找与删除操作 ------------------------------
/**
* @brief 查找元素（基础版本）
* @note 基于XVector的接口，查找指定XVariant元素，返回迭代器
*/
#define XVariantList_find_base			XVector_find_base
/**
* @brief 移除指定元素（基础版本）
* @note 基于XVector的接口，删除与指定XVariant元素匹配的第一个元素
*/
#define XVariantList_remove_base		XVector_remove_base
/**
* @brief 移除指定位置元素（基础版本）
* @note 基于XVector的接口，删除指定索引位置的XVariant元素
*/
#define XVariantList_erase_base			XVector_erase_base
/**
* @brief 移除后端元素（基础版本）
* @note 基于XVector的接口，删除列表最后一个XVariant元素
*/
#define XVariantList_pop_back_base		XVector_pop_back_base
/**
* @brief 移除前端元素（基础版本）
* @note 基于XVector的接口，删除列表第一个XVariant元素
*/
#define XVariantList_pop_front_base		XVector_pop_front_base
// ------------------------------ 容器大小调整 ------------------------------
/**
* @brief 调整列表大小（基础版本）
* @note 基于XVector的接口，修改列表元素数量，新增元素初始化为默认XVariant
*/
#define XVariantList_resize_base		XVector_resize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 清空列表元素（基础版本）
* @note 基于XVector的接口，删除所有元素但保留列表结构
*/
#define XVariantList_clear_base			XVector_clear_base
// ------------------------------ 容器属性查询 ------------------------------
/**
* @brief 判断列表是否为空（基础版本）
* @note 基于XVector的接口，元素数量为0时返回true
*/
#define XVariantList_isEmpty_base		XVector_isEmpty_base
/**
* @brief 获取列表元素数量（基础版本）
* @note 基于XVector的接口，返回当前存储的XVariant元素个数
*/
#define XVariantList_size_base			XVector_size_base
/**
* @brief 获取列表容量（基础版本）
* @note 基于XVector的接口，返回当前可容纳的最大元素数
*/
#define XVariantList_capacity_base		XVector_capacity_base
/**
* @brief 交换两个列表内容（基础版本）
* @note 基于XVector的接口，快速交换两个列表的元素和状态
*/
#define XVariantList_swap_base			XVector_swap_base
/**
* @brief 获取元素类型大小（基础版本）
* @note 基于XVector的接口，返回XVariant类型的大小（字节数）
*/
#define XVariantList_typeSize_base		XVector_typeSize_base
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVariantList_create
#define XVariantList_create() XVariantList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // !XVARIANTLIST_H
