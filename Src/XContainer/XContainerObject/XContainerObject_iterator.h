#include"XDataStructConfig.h"
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
#define for_each_iterator(container,type,it) for(type##_iterator*it=type##_begin(container);it!=type##_end(container);it=type##_iterator_add(container,it))
#define For_Each_Iterator(container,type,it) for(type##_iterator it=type##_begin(container),endIt=type##_end(container);!type##_iterator_equality(&it,&endIt);type##_iterator_add(container,&it))
//反向迭代器遍历
#define for_each_reverse_iterator(container,type,it) for(type##_reverse_iterator*it=type##_rbegin(container);it!=type##_rend(container);it=type##_reverse_iterator_add(container,it))
#define For_Each_Reverse_Iterator(container,type,it) for(type##_reverse_iterator it=type##_rbegin(container),endIt=type##_rend(container);!type##_reverse_iterator_equality(&it,&endIt);type##_reverse_iterator_add(container,&it))
#define XContainer_begin(Container) Container##_iterator* Container##_begin(Container* this_##Container)
#define XContainer_end(Container) Container##_iterator* Container##_end(Container* this_##Container)
#define XContainer_iterator_add(Container) Container##_iterator* Container##_iterator_add(Container* this_##Container, Container##_iterator*it)
#define XContainer_iterator_for_each(Container) void Container##_iterator_for_each(Container* this_##Container, XFor_each ForFunction, void* args)

#define XContainer_rbegin(Container) Container##_reverse_iterator* Container##_rbegin(Container* this_##Container)
#define XContainer_rend(Container) Container##_reverse_iterator* Container##_rend(Container* this_##Container)
#define XContainer_reverse_iterator_add(Container) Container##_reverse_iterator* Container##_reverse_iterator_add(Container* this_##Container, Container##_reverse_iterator*it)
#define XContainer_reverse_iterator_for_each(Container) void Container##_reverse_iterator_for_each(Container* this_##Container, XFor_each ForFunction, void* args)
#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
