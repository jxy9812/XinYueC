#include"XVectorTwo_func.h"
#if XVectorTwo_ON
#include"XVector.h"
#include"XPoint.h"
#include<string.h>
XVector* XVectorTwoMatrix_create(const size_t TypeSize, const size_t row, const size_t list, const void* initVal)
{
	XVector* VTwo = XVector_create(sizeof(struct XVector*));//二维数组
	for (size_t i = 0; i < row; i++)
	{
		XVector* RowVector = XVector_create( TypeSize);//每一行的一维数组		
		for (size_t j = 0; j < list; j++)
		{
			XVector_push_back_base(RowVector, initVal);
		}
		XVector_push_back_base(VTwo, &RowVector);
	}
	return VTwo;
}

XVector* XVectorTwo_create()
{
	struct XVector* VTwo = XVector_create( sizeof(struct XVector*));//二维数组
	return VTwo;
}

XVector* XVectorTwo_copy(const XVector* this_vector)
{
	size_t row = XVectorTwo_Row(this_vector);
	if (row == 0)
		return NULL;
	size_t TypeSize = XVectorTwo_TypeSize(this_vector);
	XVector* temp = XVector_create( sizeof(struct XVector*));//二维数组
	for (size_t i = 0; i < row; i++)
	{
		size_t List = XVectorTwo_List(this_vector,i);
		XVector*LTemp= XVector_create( TypeSize);
		for (size_t j = 0; j < List; j++) 
		{
			XVector_push_back_base(LTemp,XVectorTwo_at(this_vector, i, j));
		}
		XVector_push_back_base(temp, &LTemp);
	}
	return temp;
}

void* XVectorTwo_at(const XVector* this_vector, const size_t row, const size_t list)
{
	//获取当前位置
	struct XVector* RowVector = *(struct XVector**)XVector_at_base(this_vector, row);
	return XVector_at_base(RowVector, list);
}

void* XVectorTwo_at_XPoint(const XVector* this_vector, const XPoint point)
{
	return XVectorTwo_at(this_vector,point.y, point.x);
}

const size_t XVectorTwo_Row(const XVector* this_vector)
{
	return XVector_size_base(this_vector);//行
}

const size_t XVectorTwo_List(const XVector* this_vector, const size_t row)
{
	return XVector_size_base(*(struct XVector**)XVector_at_base(this_vector, row));//列
}

size_t XVectorTwo_TypeSize(XVector* this_vector)
{
	return XVector_typeSize_base(*(struct XVector**)XContainerDataPtr(this_vector));
}

void XVectorTwo_clear(const XVector* this_vector)
{
	//for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	for_each_iterator(this_vector, XVector, it)
	{
		struct XVector* RowVector = *(struct XVector**)XVector_iterator_data(&it);
		XVector_delete_base(RowVector);
	}
	XVector_clear_base(this_vector);
}

void XVectorTwo_delete(const XVector* this_vector)
{
	XVectorTwo_clear(this_vector);
	XVector_delete_base(this_vector);
}
#endif