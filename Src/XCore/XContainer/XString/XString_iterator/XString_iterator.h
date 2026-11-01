/**
* @file XString_iterator.h
* @brief XString正向迭代器头文件
* @details 定义XString容器的正向迭代器结构及相关操作函数，用于从前向后遍历字符串中的字符
*/
#include"CXinYueConfig.h"
#if !defined(XSTRING_ITERATOR_H)&& XString_ON
#define XSTRING_ITERATOR_H
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
* @brief XString正向迭代器结构体
* @details 用于遍历XString中的字符，支持从前向后的单向遍历
*/
typedef struct XString_iterator
{
	void* data; ///< 当前指向的字符数据指针
}XString_iterator;

/**
* @brief 获取指向XString第一个字符的迭代器
* @param str XString实例指针
* @return 指向第一个字符的迭代器，若字符串为空则返回结束迭代器
*/
XString_iterator XString_begin(XString* str);

/**
* @brief 获取XString的结束迭代器（哨兵位置）
* @param str XString实例指针
* @return 结束迭代器（指向字符串末尾的下一个位置）
*/
XString_iterator XString_end(XString* str);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XString_iterator_isEnd(const XString_iterator* it);

/**
* @brief 将迭代器移动到下一个字符位置
* @param str XString实例指针
* @param it 迭代器指针（会被修改）
*/
void XString_iterator_add(XString* str, XString_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XString_iterator_equality(XString_iterator* itFirst, XString_iterator* itSecond);

/**
* @brief 使用回调函数遍历XString中的所有字符
* @param str XString实例指针
* @param ForFunction 对每个字符调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XString_iterator_for_each(XString* str, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的字符数据指针
* @param it 迭代器指针
* @return 指向当前字符的XChar指针
*/
XChar* XString_iterator_data(XString_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H