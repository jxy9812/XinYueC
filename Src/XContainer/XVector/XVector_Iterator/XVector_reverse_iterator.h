/**
* @file XVector_reverse_iterator.h
* @brief XVector反向迭代器头文件
* @details 定义XVector容器的反向迭代器结构及相关操作函数，用于从后向前遍历向量元素
*/
#include"CXinYueConfig.h"
#if !defined(XVECTOR_REVERSE_ITERATOR_H)&& XVector_ON
#define XVECTOR_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"

/**
* @brief 声明XVector类型
*/
XContainerTypeDeclare(XVector);

/**
* @brief XVector反向迭代器结构体
* @details 用于遍历XVector中的元素，支持从后向前的单向遍历
*/
typedef struct XVector_reverse_iterator
{
	void* data; ///< 当前指向的元素数据指针
}XVector_reverse_iterator;

/**
* @brief 获取指向XVector最后一个元素的反向迭代器
* @param this_vector XVector实例指针
* @return 指向最后一个元素的反向迭代器，若向量为空则返回结束迭代器
*/
XVector_reverse_iterator XVector_rbegin(XVector* this_vector);

/**
* @brief 获取XVector的反向结束迭代器（哨兵位置）
* @param this_vector XVector实例指针
* @return 反向结束迭代器（指向向量第一个元素的前一个位置）
*/
XVector_reverse_iterator XVector_rend(XVector* this_vector);

/**
* @brief 判断反向迭代器是否已到达开头
* @param it 反向迭代器指针
* @return 若反向迭代器已到达开头返回true，否则返回false
*/
bool XVector_reverse_iterator_isRend(const XVector_reverse_iterator* it);

/**
* @brief 将反向迭代器移动到前一个元素位置
* @param this_vector XVector实例指针
* @param it 反向迭代器指针（会被修改）
*/
void XVector_reverse_iterator_add(XVector* this_vector,XVector_reverse_iterator* it);

/**
* @brief 判断两个反向迭代器是否相等
* @param itFirst 第一个反向迭代器指针
* @param itSecond 第二个反向迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XVector_reverse_iterator_equality(XVector_reverse_iterator* itFirst, XVector_reverse_iterator* itSecond);

/**
* @brief 使用回调函数反向遍历XVector中的所有元素
* @param this_vector XVector实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XVector_reverse_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args);

/**
* @brief 获取反向迭代器当前指向的元素数据指针
* @param it 反向迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XVector_reverse_iterator_data(XVector_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H