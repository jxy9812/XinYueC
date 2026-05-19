/**
* @file XVector_iterator.h
* @brief XVector正向迭代器头文件
* @details 定义XVector容器的正向迭代器结构及相关操作函数，用于从前向后遍历向量元素
*/
#include"CXinYueConfig.h"
#if !defined(XVECTOR_ITERATOR_H)&& XVector_ON
#define XVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"

/**
* @brief 声明XVector类型
*/
XContainerTypeDeclare(XVector);

/**
* @brief XVector正向迭代器结构体
* @details 用于遍历XVector中的元素，支持从前向后的单向遍历
*/
typedef struct XVector_iterator
{
	void* data; ///< 当前指向的元素数据指针
}XVector_iterator;

/**
* @brief 获取指向XVector第一个元素的迭代器
* @param this_vector XVector实例指针
* @return 指向第一个元素的迭代器，若向量为空则返回结束迭代器
*/
XVector_iterator XVector_begin(XVector* this_vector);

/**
* @brief 获取XVector的结束迭代器（哨兵位置）
* @param this_vector XVector实例指针
* @return 结束迭代器（指向向量末尾的下一个位置）
*/
XVector_iterator XVector_end(XVector* this_vector);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XVector_iterator_isEnd(const XVector_iterator* it);

/**
* @brief 将迭代器移动到下一个元素位置
* @param this_vector XVector实例指针
* @param it 迭代器指针（会被修改）
*/
void XVector_iterator_add(XVector* this_vector, XVector_iterator*it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XVector_iterator_equality(XVector_iterator* itFirst, XVector_iterator* itSecond);

/**
* @brief 使用回调函数遍历XVector中的所有元素
* @param this_vector XVector实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XVector_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的元素数据指针
* @param it 迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XVector_iterator_data(XVector_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
