#include "XVectorTwo_func.h"
#include"XVector.h"
#include"XPoint.h"
#include<string.h>
XVector* XVectorTwoMatrix_init(const size_t TypeSize, const size_t row, const size_t list, const void* initVal)
{
	struct XVector* VTwo = XVector_init(sizeof(struct XVector*));//二维数组
	for (size_t i = 0; i < row; i++)
	{
		struct XVector* RowVector = XVector_init( TypeSize);//每一行的一维数组
		for (size_t j = 0; j < list; j++)
		{
			XVector_push_back(RowVector, initVal);
		}
		XVector_push_back(VTwo, &RowVector);
	}
	return VTwo;
}

XVector* XVectorTwo_init()
{
	struct XVector* VTwo = XVector_init( sizeof(struct XVector*));//二维数组
	return VTwo;
}

XVector* XVectorTwo_copy(const XVector* this_vector)
{
	size_t row = XVectorTwo_Row(this_vector);
	if (row == 0)
		return NULL;
	size_t TypeSize = XVectorTwo_TypeSize(this_vector);
	XVector* temp = XVector_init( sizeof(struct XVector*));//二维数组
	for (size_t i = 0; i < row; i++)
	{
		size_t List = XVectorTwo_List(this_vector,i);
		XVector*LTemp= XVector_init( TypeSize);
		for (size_t j = 0; j < List; j++) 
		{
			XVector_push_back(LTemp,XVectorTwo_at(this_vector, i, j));
		}
		XVector_push_back(temp, &LTemp);
	}
	return temp;
}

void* XVectorTwo_at(const XVector* this_vector, const size_t row, const size_t list)
{
	//获取当前位置
	struct XVector* RowVector = *(struct XVector**)XVector_at(this_vector, row);
	return XVector_at(RowVector, list);
}

void* XVectorTwo_at_XPoint(const XVector* this_vector, const XPoint point)
{
	return XVectorTwo_at(this_vector,point.y, point.x);
}

const size_t XVectorTwo_Row(const XVector* this_vector)
{
	return XVector_size(this_vector);//行
}

const size_t XVectorTwo_List(const XVector* this_vector, const size_t row)
{
	return XVector_size(*(struct XVector**)XVector_at(this_vector, row));//列
}

size_t XVectorTwo_TypeSize(XVector* this_vector)
{
	return XVector_TypeSize(*(struct XVector**)XVector_begin(this_vector));
}

void XVectorTwo_clear(const XVector* this_vector)
{
	for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	{
		struct XVector* RowVector = *(struct XVector**)it;
		XVector_free(RowVector);
	}
	XVector_clear(this_vector);
}

void XVectorTwo_free(const XVector* this_vector)
{
	XVectorTwo_clear(this_vector);
	XVector_free(this_vector);
}
