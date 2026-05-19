/**
* @file XVariantList_reverse_iterator.h
* @brief XVariantList反向迭代器头文件
* @details 定义XVariantList容器的反向迭代器类型，复用XVector_reverse_iterator的实现
*/
#include"CXinYueConfig.h"
#if !defined(XVARIANTLIST_REVERSE_ITERATOR_H)&& XVariantList_ON
#define XVARIANTLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XVector_reverse_iterator.h"

/**
* @brief 声明XVariantList类型
*/
XContainerTypeDeclare(XVariantList);

/**
* @brief XVariantList反向迭代器类型定义
* @details 复用XVector_reverse_iterator的实现，用于反向遍历XVariantList中的变体元素
*/
typedef  XVector_reverse_iterator XVariantList_reverse_iterator;

/**
* @brief 获取指向XVariantList最后一个元素的反向迭代器
* @details 复用XVector_rbegin实现
*/
#define XVariantList_rbegin								XVector_rbegin

/**
* @brief 获取XVariantList的反向结束迭代器
* @details 复用XVector_rend实现
*/
#define XVariantList_rend								XVector_rend

/**
* @brief 判断反向迭代器是否已到达开头
* @details 复用XVector_reverse_iterator_isRend实现
*/
#define XVariantList_reverse_iterator_isRend			XVector_reverse_iterator_isRend

/**
* @brief 将反向迭代器移动到前一个元素位置
* @details 复用XVector_reverse_iterator_add实现
*/
#define XVariantList_reverse_iterator_add				XVector_reverse_iterator_add

/**
* @brief 判断两个反向迭代器是否相等
* @details 复用XVector_reverse_iterator_equality实现
*/
#define XVariantList_reverse_iterator_equality          XVector_reverse_iterator_equality

/**
* @brief 使用回调函数反向遍历XVariantList中的所有元素
* @details 复用XVector_reverse_iterator_for_each实现
*/
#define XVariantList_reverse_iterator_for_each			XVector_reverse_iterator_for_each

/**
* @brief 获取反向迭代器当前指向的元素数据指针
* @details 复用XVector_reverse_iterator_data实现
*/
#define XVariantList_reverse_iterator_data				XVector_reverse_iterator_data

#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H