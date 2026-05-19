/**
* @file XString_reverse_iterator.h
* @brief XString反向迭代器头文件
* @details 定义XString容器的反向迭代器结构及相关操作函数，用于从后向前遍历字符串中的字符
*/
#include"CXinYueConfig.h"
#if !defined(XString_REVERSE_ITERATOR_H)&& XString_ON
#define XString_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XChar.h"

/**
* @brief 声明XString类型
*/
XContainerTypeDeclare(XString);

/**
* @brief XString反向迭代器结构体
* @details 用于遍历XString中的字符，支持从后向前的单向遍历
*/
typedef struct XString_reverse_iterator
{
	void* data; ///< 当前指向的字符数据指针
}XString_reverse_iterator;

/**
* @brief 获取指向XString最后一个字符的反向迭代器
* @param str XString实例指针
* @return 指向最后一个字符的反向迭代器，若字符串为空则返回结束迭代器
*/
XString_reverse_iterator XString_rbegin(XString* str);

/**
* @brief 获取XString的反向结束迭代器（哨兵位置）
* @param str XString实例指针
* @return 反向结束迭代器（指向字符串第一个字符的前一个位置）
*/
XString_reverse_iterator XString_rend(XString* str);

/**
* @brief 判断反向迭代器是否已到达开头
* @param it 反向迭代器指针
* @return 若反向迭代器已到达开头返回true，否则返回false
*/
bool XString_reverse_iterator_isRend(const XString_reverse_iterator* it);

/**
* @brief 将反向迭代器移动到前一个字符位置
* @param str XString实例指针
* @param it 反向迭代器指针（会被修改）
*/
void XString_reverse_iterator_add(XString* str, XString_reverse_iterator* it);

/**
* @brief 判断两个反向迭代器是否相等
* @param itFirst 第一个反向迭代器指针
* @param itSecond 第二个反向迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XString_reverse_iterator_equality(XString_reverse_iterator* itFirst, XString_reverse_iterator* itSecond);

/**
* @brief 使用回调函数反向遍历XString中的所有字符
* @param str XString实例指针
* @param ForFunction 对每个字符调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XString_reverse_iterator_for_each(XString* str, XFor_each ForFunction, void* args);

/**
* @brief 获取反向迭代器当前指向的字符数据指针
* @param it 反向迭代器指针
* @return 指向当前字符的XChar指针
*/
XChar* XString_reverse_iterator_data(XString_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H