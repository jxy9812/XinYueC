/**
* @file XSetBase_iterator.h
* @brief XSetBase迭代器头文件
* @details 定义XSetBase容器的迭代器结构，为XSet、XHashSet等派生类提供统一的迭代器基础
*/
#include"CXinYueConfig.h"
#if !defined(XSETBASE_ITERATOR_H)
#define XSETBASE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XFunctionCallback.h"

/**
* @brief 声明XSetBase类型
*/
XContainerTypeDeclare(XSetBase);

/**
* @brief XSetBase迭代器结构体
* @details 用于遍历集合容器中的元素，基于节点指针实现
*/
typedef struct XSetBase_iterator
{
    void* node;   ///< 当前节点指针（指向红黑树节点或哈希桶节点）
} XSetBase_iterator;

//XSetBase_iterator XSetBase_begin(XSetBase* this_set);
//XSetBase_iterator XSetBase_end(XSetBase* this_set);
//bool XSetBase_iterator_isEnd(const XSetBase_iterator* it);
//void XSetBase_iterator_add(XSet* this_set, XSetBase_iterator* it);
//bool XSetBase_iterator_equality(XSetBase_iterator* itFirst, XSetBase_iterator* itSecond);
//void XSetBase_iterator_for_each(XSet* this_set, XFor_each ForFunction, void* argList);
//void* XSetBase_iterator_data(XSetBase_iterator* it);

#ifdef __cplusplus
}
#endif
#endif