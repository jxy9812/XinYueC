#include"XDataStructConfig.h"
#if !defined(XHASHMAP_ITERATOR_H)&& XHashMap_ON
#define XHASHMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase_iterator.h"
XContainerTypeDeclare(XHashMap);
//typedef  XMapBase_iterator  XMapBase_iterator;
typedef struct XHashMap_iterator
{
    union //访问方便实际两个变量是一个
    {
        void* node;   // 当前节点
        XMapBase_iterator parent;
    };
    size_t index; // 当前index
} XHashMap_iterator;

XHashMap_iterator XHashMap_begin(XHashMap* this_map);
XHashMap_iterator XHashMap_end(XHashMap* this_map);
bool XHashMap_iterator_isEnd( const XHashMap_iterator* it);
void XHashMap_iterator_add(XHashMap* this_map, XHashMap_iterator* it);
bool XHashMap_iterator_equality(const XHashMap_iterator* itFirst, const XHashMap_iterator* itSecond);
void XHashMap_iterator_for_each(XHashMap* this_map, XFor_each ForFunction, void* args);
XPair* XHashMap_iterator_data(XHashMap_iterator* it);
#ifdef __cplusplus
}
#endif
#endif