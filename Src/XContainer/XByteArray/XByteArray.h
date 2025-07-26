#include"XDataStructConfig.h"
#if !defined(XBYTEARRAY_H)&& XByteArray_ON
#define XBYTEARRAY_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
typedef struct XByteArray
{
	XVector m_parent;
}XByteArray;
XByteArray* XByteArray_create(size_t size);
// 向量头部增加一个字节数据
void XByteArray_push_front_base(XByteArray* array,const uint8_t byte);
// 向量尾部增加一个字节数据
void XByteArray_push_back_base(XByteArray* array,const uint8_t byte);
// 向量中前增加一个元素
void XByteArray_insert_base(XByteArray* array, int64_t index, const  uint8_t byte);
// 向量中指向元素p前增加n个相同的元素x
void XByteArray_inserts_base(XByteArray* array, int64_t index, uint8_t byte, size_t n);
//查找数据，返回找到的指针，没有返回NULL
uint8_t* XByteArray_find_base(const XByteArray* array, const uint8_t findVal);
#define XByteArray_resize_base						XVector_resize_base
// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2]间的数据
#define XByteArray_insert_array_base				XVector_insert_array_base
#define XByteArray_append_array_base				XVector_append_array_base
#define XByteArray_pop_front_base					XVector_pop_front_base	
//删除向量中最后一个元素
#define XByteArray_pop_back_base					XVector_pop_back_base	
//删除指针处的数据(指针要在容器内)
#define XByteArray_erase_base						XVector_erase_base
//删除数据 n<0 后面全部删除
#define XByteArray_remove_base						XVector_remove_base
//将this_Two拷贝到this_One
#define XByteArray_copy_base						XVector_copy_base
//将this_Two逆序拷贝到this_One
#define XByteArray_rcopy_base						XVector_rcopy_base
// 返回元素的指针
#define XByteArray_at_base							XVector_at_base
#define XByteArray_At_Base(array,index)				XVector_At_Base(array,index,uint8_t)	
//返回向量头指针，指向第一个元素
#define XByteArray_front_base						XVector_front_base
#define XByteArray_Front_Base(array)				XVector_Front_Base(array,uint8_t)	
//返回向量尾指针，指向向量最后一个元素
#define XByteArray_back_base						XVector_back_base
#define XByteArray_Back_Base(array)					XVector_Back_Base(array,uint8_t)	
//排序
#define XByteArray_sort_base						XVector_sort_base
#define XByteArray_rcopy_base						XVector_rcopy_base
#define XByteArray_copy_base						XVector_copy_base	
#define XByteArray_move_base						XVector_move_base	
#define XByteArray_deinit_base						XVector_deinit_base	
#define XByteArray_delete_base						XVector_delete_base	
#define XByteArray_clear_base						XVector_clear_base	
#define XByteArray_isEmpty_base						XVector_isEmpty_base	
#define XByteArray_getSize_base						XVector_getSize_base	
#define XByteArray_getCapacity_base					XVector_getCapacity_base
#define XByteArray_swap_base						XVector_swap_base	
#define XByteArray_getTypeSize_base					XVector_getTypeSize_base


#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H
