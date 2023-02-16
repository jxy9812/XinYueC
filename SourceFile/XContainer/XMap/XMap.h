#ifndef XMAP_H
#define XMAP_H
#include "XContainerObject.h"
#include"XMap_func.h"
#include"XMap_Iterator.h"
#include"XMap_reverse_iterator.h"
typedef struct XVector XVector;
typedef struct XMap
{
	XContainerObject object;//基本数据
	size_t keyTypeSize;//第二组数据类型大小
	XEquality KeyEquality;//key的相等比较函数
	XLess KeyLess;//key小于比较函数
	bool isModify;//是否修改了
	XVector* itArray;//迭代器数组
}XMap;

#endif // !XMap_H
