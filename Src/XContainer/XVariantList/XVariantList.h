#include"CXinYueConfig.h"
#if !defined(XVARIANTLIST_H)&& XVariantList_ON
#define XVARIANTLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
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
* @return 创建成功的XVariantList实例指针，失败返回NULL
* @note 动态分配内存并调用XVariantList_init完成初始化，初始化为空列表
*/
XVariantList* XVariantList_create();
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
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 向前端插入元素（拷贝语义，基础版本）
* @note 基于XVector的接口，拷贝XVariant元素到列表前端
*/
#define XVariantList_push_front_base		XVector_push_front_base
/**
* @brief 向前端插入元素（移动语义，基础版本）
* @note 基于XVector的接口，转移XVariant元素所有权到列表前端
*/
#define XVariantList_push_front_move_base XVector_push_front_move_base
/**
* @brief 向后端插入元素（拷贝语义，基础版本）
* @note 基于XVector的接口，拷贝XVariant元素到列表后端
*/
#define XVariantList_push_back_base		XVector_push_back_base
/**
* @brief 向后端插入元素（移动语义，基础版本）
* @note 基于XVector的接口，转移XVariant元素所有权到列表后端
*/
#define XVariantList_push_back_move_base XVector_push_back_move_base
/**
* @brief 在指定位置插入元素（拷贝语义）
* @note 基于XVector的接口，拷贝XVariant元素到指定索引位置
*/
#define XVariantList_insert				XVector_insert
/**
* @brief 在指定位置插入元素（移动语义）
* @note 基于XVector的接口，转移XVariant元素所有权到指定索引位置
*/
#define XVariantList_insert_move		XVector_insert_move
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
* @brief 拷贝列表（深拷贝，基础版本）
* @note 基于XVector的接口，复制源列表的所有元素到当前列表
*/
#define XVariantList_copy_base			XVector_copy_base
/**
* @brief 移动列表资源（基础版本）
* @note 基于XVector的接口，转移源列表的资源所有权到当前列表
*/
#define XVariantList_move_base			XVector_move_base
/**
* @brief 反初始化列表（基础版本）
* @note 基于XVector的接口，释放内部资源但不销毁实例本身
*/
#define XVariantList_deinit_base		XVector_deinit_base
/**
* @brief 删除列表实例（基础版本）
* @note 基于XVector的接口，释放内部资源并销毁实例
*/
#define XVariantList_delete_base		XVector_delete_base
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
#endif // !XVARIANTLIST_H