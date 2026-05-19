/**
* @file XSet_reverse_iterator.h
* @brief XSet反向迭代器头文件
* @details 定义XSet容器的反向迭代器类型及相关操作函数，用于从后向前遍历集合中的元素
*/
#include"CXinYueConfig.h"
#if !defined(XSET_REVERSE_ITERATOR_H)&& XSet_ON
#define XSET_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSet_iterator.h"

/**
* @brief 声明XSet类型
*/
XContainerTypeDeclare(XSet);

/**
* @brief XSet反向迭代器类型定义
* @details 复用XSetBase_iterator的实现，用于反向遍历XSet中的元素
*/
typedef  XSetBase_iterator  XSet_reverse_iterator;

/**
* @brief 获取指向XSet最后一个元素的反向迭代器
* @param this_set XSet实例指针
* @return 指向最后一个元素的反向迭代器，若集合为空则返回结束迭代器
*/
XSet_reverse_iterator XSet_rbegin(XSet* this_set);

/**
* @brief 获取XSet的反向结束迭代器（哨兵位置）
* @param this_set XSet实例指针
* @return 反向结束迭代器（指向集合第一个元素的前一个位置）
*/
XSet_reverse_iterator XSet_rend(XSet* this_set);

/**
* @brief 判断反向迭代器是否已到达开头
* @param it 反向迭代器指针
* @return 若反向迭代器已到达开头返回true，否则返回false
*/
bool XSet_reverse_iterator_isRend(const XSet_reverse_iterator* it);

/**
* @brief 将反向迭代器移动到前一个元素位置
* @param this_set XSet实例指针
* @param it 反向迭代器指针（会被修改）
*/
void XSet_reverse_iterator_add(XSet* this_set, XSet_reverse_iterator* it);

/**
* @brief 判断两个反向迭代器是否相等
* @param itFirst 第一个反向迭代器指针
* @param itSecond 第二个反向迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XSet_reverse_iterator_equality(XSet_reverse_iterator* itFirst, XSet_reverse_iterator* itSecond);

/**
* @brief 使用回调函数反向遍历XSet中的所有元素
* @param this_set XSet实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XSet_reverse_iterator_for_each(XSet* this_set, XFor_each ForFunction, void* args);

/**
* @brief 获取反向迭代器当前指向的元素数据指针
* @param it 反向迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XSet_reverse_iterator_data(XSet_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !XSET_REVERSE_ITERATOR_H

