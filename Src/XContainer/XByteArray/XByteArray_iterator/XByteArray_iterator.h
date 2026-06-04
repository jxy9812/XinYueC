/**
* @file XByteArray_iterator.h
* @brief XByteArray正向迭代器头文件
* @details 定义XByteArray容器的正向迭代器类型，复用XVector_iterator的实现
*/
#include"CXinYueConfig.h"
#if !defined(XBYTEARRAY_ITERATOR_H)&& XByteArray_ON
#define XBYTEARRAY_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XVector_iterator.h"

/**
* @brief 声明XByteArray类型
*/
XContainerTypeDeclare(XByteArray);

/**
* @brief XByteArray正向迭代器类型定义
* @details 复用XVector_iterator的实现，用于遍历XByteArray中的字节数据
*/
typedef  XVector_iterator XByteArray_iterator;

/**
* @brief 获取指向XByteArray第一个元素的迭代器
* @details 复用XVector_begin实现
*/
#define XByteArray_begin						XVector_begin

/**
* @brief 获取XByteArray的结束迭代器
* @details 复用XVector_end实现
*/
#define XByteArray_end							XVector_end

/**
* @brief 将迭代器移动到下一个元素位置
* @details 复用XVector_iterator_add实现
*/
#define XByteArray_iterator_add					XVector_iterator_add

/**
* @brief 判断两个迭代器是否相等
* @details 复用XVector_iterator_equality实现
*/
#define XByteArray_iterator_equality          XVector_iterator_equality

/**
* @brief 使用回调函数遍历XByteArray中的所有字节
* @details 复用XVector_iterator_for_each实现
*/
#define XByteArray_iterator_for_each			XVector_iterator_for_each

/**
* @brief 获取迭代器当前指向的字节数据指针
* @details 复用XVector_iterator_data实现
*/
#define XByteArray_iterator_data(it)				*((uint8_t*)XVector_iterator_data(it))

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
