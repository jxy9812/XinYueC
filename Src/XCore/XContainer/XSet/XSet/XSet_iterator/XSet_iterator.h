/**
* @file XSet_iterator.h
* @brief XSet正向迭代器头文件
* @details 定义XSet容器的正向迭代器类型及相关操作函数，用于从前向后遍历集合中的元素
*/
#include"CXinYueConfig.h"
#if !defined(XSET_ITERATOR_H)&& XSet_ON
#define XSET_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XSetBase_iterator.h"

/**
* @brief 声明XSet类型
*/
XContainerTypeDeclare(XSet);

/**
* @brief XSet正向迭代器类型定义
* @details 复用XSetBase_iterator的实现，用于遍历XSet中的元素
*/
typedef  XSetBase_iterator  XSet_iterator;

/**
* @brief 获取指向XSet第一个元素的迭代器
* @param this_set XSet实例指针
* @return 指向第一个元素的迭代器，若集合为空则返回结束迭代器
*/
XSet_iterator XSet_begin(XSet* this_set);

/**
* @brief 获取XSet的结束迭代器（哨兵位置）
* @param this_set XSet实例指针
* @return 结束迭代器（指向集合末尾的下一个位置）
*/
XSet_iterator XSet_end(XSet* this_set);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XSet_iterator_isEnd(const XSet_iterator* it);

/**
* @brief 将迭代器移动到下一个元素位置
* @param this_set XSet实例指针
* @param it 迭代器指针（会被修改）
*/
void XSet_iterator_add(XSet* this_set, XSet_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XSet_iterator_equality(XSet_iterator* itFirst, XSet_iterator* itSecond);

/**
* @brief 使用回调函数遍历XSet中的所有元素
* @param this_set XSet实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XSet_iterator_for_each(XSet* this_set, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的元素数据指针
* @param it 迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XSet_iterator_data(XSet_iterator* it);

#ifdef __cplusplus
}
#endif
#endif

