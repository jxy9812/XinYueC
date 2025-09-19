#include"CXinYueConfig.h"
#if !defined(XCONTAINEROBJECT_ITERATOR_H)&& XContainerObject_ON
#define XCONTAINEROBJECT_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"
//容器类型声明
#define XContainerTypeDeclare(Container) typedef struct Container Container
//容器正向迭代器声明
#define XContainerIteratorDeclare(Container) typedef void Container##_iterator
//容器反向迭代器声明
#define XContainerReverseIteratorDeclare(Container) typedef void Container##_reverse_iterator
//正向迭代器遍历
#define for_each_iterator(container,type,it) for(type##_iterator it=type##_begin(container),endIt=type##_end(container);!type##_iterator_equality(&it,&endIt);type##_iterator_add(container,&it))
//反向迭代器遍历
#define for_each_reverse_iterator(container,type,it) for(type##_reverse_iterator it=type##_rbegin(container),endIt=type##_rend(container);!type##_reverse_iterator_equality(&it,&endIt);type##_reverse_iterator_add(container,&it))

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
