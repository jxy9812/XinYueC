#include"XDataStructConfig.h"
#if !defined(XVECTORTWO_FUNC_H)&& XVectorTwo_ON
#define XVECTORTWO_FUNC_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
struct XVector;
struct XPoint;
//开辟一个二维动态数组矩阵,初始化row行 list列
struct XVector* XVectorTwoMatrix_new(const size_t TypeSize, const size_t row,const size_t list,const void* initVal);
//开辟一个二维动态数组,初始化row行 list列
struct XVector* XVectorTwo_new();
//拷贝复制一个二维数组
struct XVector* XVectorTwo_copy(const struct XVector* this_vector);
// 返回元素的指针
void* XVectorTwo_at(const struct XVector* this_vector,const size_t row, const size_t list);
// 返回元素的指针
void* XVectorTwo_at_XPoint(const struct XVector* this_vector, const struct XPoint point);
//二维数组行数
const size_t XVectorTwo_Row(const struct XVector* this_vector);
//二维数组列数
const size_t XVectorTwo_List(const struct XVector* this_vector,const size_t row);
//二维数组返回元素类型字节大小
size_t XVectorTwo_TypeSize(struct XVector* this_vector);
//清空二位数组
void XVectorTwo_clear(const struct XVector* this_vector);
//释放二维数组内存
void XVectorTwo_free(const struct XVector* this_vector);
#ifdef __cplusplus
}
#endif
#endif // !XVECTORTWO_FUNC_H
