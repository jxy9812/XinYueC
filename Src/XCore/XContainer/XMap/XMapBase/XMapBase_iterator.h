/**
* @file XMapBase_iterator.h
* @brief XMapBase迭代器头文件
* @details 定义XMapBase容器的迭代器结构，为XMap、XHashMap等派生类提供统一的迭代器基础
*/
#include"CXinYueConfig.h"
#if !defined(XMAPBASE_ITERATOR_ITERATOR_H)
#define XMAPBASE_ITERATOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"
#include"XFunctionCallback.h"

/**
* @brief 声明XMapBase类型
*/
XContainerTypeDeclare(XMapBase);

/**
* @brief 声明XPair类型
*/
XContainerTypeDeclare(XPair);

/**
* @brief XMapBase迭代器结构体
* @details 用于遍历映射容器中的键值对，基于节点指针实现
*/
typedef struct XMapBase_iterator
{
    void* node;   ///< 当前节点指针（指向红黑树节点或哈希桶节点）
} XMapBase_iterator;

//XMapBase_iterator XMapBase_begin(XMapBase* this_set);
//XMapBase_iterator XMapBase_end(XMapBase* this_set);
//bool XMapBase_iterator_isEnd(const XMapBase_iterator* it);
//void XMapBase_iterator_add(XMapBase* this_set, XMapBase_iterator* it);
//bool XMapBase_iterator_equality(XMapBase_iterator* itFirst, XMapBase_iterator* itSecond);
//void XMapBase_iterator_for_each(XMapBase* this_set, XFor_each ForFunction, void* argList);
//void* XMapBase_iterator_data(XMapBase_iterator* it);

#ifdef __cplusplus
}
#endif
#endif