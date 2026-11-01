/**
* @file XStringList_Iterator.h
* @brief XStringList正向迭代器头文件
* @details 定义XStringList容器的正向迭代器类型，复用XVector_iterator的实现
*/
#include"CXinYueConfig.h"
#if !defined(XSTRINGVECTOR_ITERATOR_H)&& XStringList_ON
#define XSTRINGVECTOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XVector_iterator.h"

/**
* @brief 声明XStringList类型
*/
XContainerTypeDeclare(XStringList);

/**
* @brief XStringList正向迭代器类型定义
* @details 复用XVector_iterator的实现，用于遍历XStringList中的字符串元素
*/
typedef XVector_iterator XStringList_iterator ;

/**
* @brief 声明XString类型
*/
typedef struct XString XString;

/**
* @brief 获取指向XStringList第一个元素的迭代器
* @details 复用XVector_begin实现
*/
#define XStringList_begin						XVector_begin

/**
* @brief 获取XStringList的结束迭代器
* @details 复用XVector_end实现
*/
#define XStringList_end							XVector_end

/**
* @brief 判断迭代器是否已到达末尾
* @details 复用XVector_iterator_isEnd实现
*/
#define XStringList_iterator_isEnd				XVector_iterator_isEnd

/**
* @brief 将迭代器移动到下一个元素位置
* @details 复用XVector_iterator_add实现
*/
#define XStringList_iterator_add				XVector_iterator_add

/**
* @brief 判断两个迭代器是否相等
* @details 复用XVector_iterator_equality实现
*/
#define XStringList_iterator_equality			XVector_iterator_equality

/**
* @brief 使用回调函数遍历XStringList中的所有元素
* @details 复用XVector_iterator_for_each实现
*/
#define XStringList_iterator_for_each			XVector_iterator_for_each

/**
* @brief 获取迭代器当前指向的元素数据指针
* @details 复用XVector_iterator_data实现
*/
#define XStringList_iterator_data				XVector_iterator_data

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
