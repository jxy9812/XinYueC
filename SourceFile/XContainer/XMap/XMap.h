#ifndef XMAP_H
#define XMAP_H
#include "XContainerObject.h"
#include"XMap_func.h"
typedef struct XMap
{
	XContainerObject object;//基本数据
	size_t secondTypeSize;//第二组数据类型大小
	XEquality KeyEquality;//key的相等比较函数
	XLess KeyLess;//key小于比较函数
}XMap;

#endif // !XMap_H
