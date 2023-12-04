#include"XVector_virtual.h"
#include"XVector.h"
#include"XContainerObject_virtual.h"
#include"XSort.h"
#include<string.h>
#include<stdlib.h>
#define VECTORNUM 4//初始数组大小
//虚函数表定义
void* XVectorVtable[] = {
	//继承的函数
	VXContainerObject_empty,VXContainerObject_size,VXContainerObject_capacity,VXContainerObject_type,VXContainerObject_swap,VXVector_clear,VXContainerObject_free,
	VXVector_resize,
	//插入
	VXVector_push_front,VXVector_push_back,VXVector_inserts,VXVector_insert,VXVector_insertArray,
	//删除
	VXVector_pop_front,VXVector_pop_back,VXVector_erase,VXVector_remove,
	//拷贝
	VXVector_copy,VXVector_rcopy,
	//遍历
	VXVector_at,VXVector_front,VXVector_back,VXVector_find,
	//排序
	VXVector_sort
};
//检测是否需要扩容
void VXVectorEnlargeCapacity(XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return;
	if (ObjectCapacity(this_vector) == 0)
	{
		ObjectDataPtr(this_vector) = malloc(ObjectTypeSize(this_vector) * VECTORNUM);
		if (ObjectDataPtr(this_vector) == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			ObjectCapacity(this_vector)= VECTORNUM;
		}
	}
	else if (ObjectCapacity(this_vector) == ObjectSize(this_vector))//空间已满需要扩容
	{
		void* _data = realloc(ObjectDataPtr(this_vector), ObjectCapacity(this_vector) * ObjectTypeSize(this_vector) * 1.5);
		if (_data == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		else
		{
			ObjectDataPtr(this_vector)=_data;
			ObjectCapacity(this_vector) *= 1.5;
		}
	}
}
void VXVector_resize(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, ""))
		return;
	size_t capacity =ObjectCapacity(this_vector);//当前容器的最大数量
	size_t count = ObjectSize(this_vector);//当前容器使用的数量
	size_t TypeSize = ObjectTypeSize(this_vector);//数据类型大小
	//XContainerObject* object = this_vector;//数据父类
	if (size <= count)
	{
		for (size_t i = 0; i < count - size; i++)
		{
			XVector_pop_back(this_vector);
		}
		return;
	}
	//char* lpData = ObjectDataPtr(this_vector);
	if (size > capacity)//大于最大容量
	{
		ObjectDataPtr(this_vector) = realloc(ObjectDataPtr(this_vector), size * TypeSize);
		if (ObjectDataPtr(this_vector) == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		ObjectCapacity(this_vector) = size;
	}

	char* LPstart = ObjectDataPtr(this_vector) + count * TypeSize;//最后一个元素的下一个元素
	memset(LPstart, 0, (size - count) * TypeSize);
	ObjectSize(this_vector) = size;//设置当前容器元素数量
	return;
}
void VXVector_push_front(XVector* this_vector, void* LpValue)
{
	if (VXContainerObject_empty(this_vector))
		VXVector_push_back(this_vector, LpValue);
	else
		VXVector_insert(this_vector, 0, LpValue);
}
void VXVector_push_back(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, ""))
		return;
	VXVectorEnlargeCapacity(this_vector);
	char* ptr = (char*)ObjectDataPtr(this_vector) + ObjectTypeSize(this_vector) * ObjectSize(this_vector);
	memcpy(ptr, LpValue, ObjectTypeSize(this_vector));
	ObjectSize(this_vector)++;
}
void VXVector_insert(XVector* this_vector, int64_t index, const void* LpValue)
{
	VXVector_inserts(this_vector,index,LpValue,1);
}
void VXVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n)// 向量中指向元素p前增加n个相同的元素x
{
	if (ISNULL(this_vector, ""))
		return;
	const void* ptr = VXVector_at(this_vector, index);
	size_t typeSize = ObjectTypeSize(this_vector);
	if (ptr&&ptr >= VXVector_front(this_vector) && ptr <= VXVector_back(this_vector))
	{
		int64_t size = (char*)VXVector_back(this_vector) - (char*)ptr + typeSize;
		void* temp = malloc(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)VXVector_front(this_vector)) / typeSize;
		for (size_t i = 0; i < n; i++)
		{
			VXVectorEnlargeCapacity(this_vector);
			memcpy(VXVector_at(this_vector, sizen), LpValue, typeSize);
			sizen++;
			ObjectSize(this_vector)++;
		}
		memcpy(VXVector_at(this_vector, sizen), temp, size);
		free(temp);
	}
}
void VXVector_insertArray(XVector* this_vector, int64_t index, const void* begin, size_t n)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	if (ISNULL(this_vector, ""))
		return;
	const void* ptr = VXVector_at(this_vector, index);
	size_t typeSize = ObjectTypeSize(this_vector);
	if (ptr >= VXVector_front(this_vector) && ptr <= VXVector_back(this_vector))
	{
		VXVectorEnlargeCapacity(this_vector);
		int64_t size = (char*)VXVector_back(this_vector) - (char*)ptr + typeSize;
		void* temp = malloc(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)VXVector_front(this_vector)) / typeSize;
		for (size_t i = 0; i < n; i++)
		{
			VXVectorEnlargeCapacity(this_vector);
			memcpy(VXVector_at(this_vector, sizen), (char*)begin + i * typeSize, typeSize);
			sizen++;
			ObjectSize(this_vector)++;
		}
		memcpy(VXVector_at(this_vector, sizen), temp, size);
		free(temp);
	}
}
void VXVector_pop_front(XVector* this_vector)//删除向量中第一个元素
{
	VXVector_remove(this_vector, 0, 1);
	//if (VXContainerObject_empty(this_vector))
	//	return NULL;
	//--ObjectSize(this_vector);
	//if (ObjectSize(this_vector) > 0)
	//{//不止一个的时候
	//	char* ptr = ObjectDataPtr(this_vector);//data 的指针
	//	size_t typeSize = ObjectTypeSize(this_vector);
	//	for (size_t i = 0; i < ObjectSize(this_vector); i++)
	//	{
	//		memcpy(ptr +i* typeSize, ptr + (i+1) * typeSize, typeSize);
	//	}
	//}

}
void VXVector_pop_back(XVector* this_vector)//删除向量中最后一个元素
{
	if (VXContainerObject_empty(this_vector))
		return NULL;
	--ObjectSize(this_vector);
}
void VXVector_erase(XVector* this_vector, void* LpValue)//删除指针数据
{
	if (VXContainerObject_empty(this_vector))
		return NULL;
	if (VXVector_front(this_vector) <= LpValue && LpValue <= VXVector_back(this_vector) && ((char*)LpValue - (char*)VXVector_front(this_vector)) % ObjectTypeSize(this_vector) == 0)
	{
		size_t typeSize = ObjectTypeSize(this_vector);
		memcpy(LpValue, (char*)LpValue + typeSize, (size_t)((char*)VXVector_back(this_vector) - (char*)LpValue - typeSize));
		--ObjectSize(this_vector);
	}
}
void VXVector_remove(XVector* this_vector, int64_t index, int64_t n)//删除数据 n<0 后面全部删除
{
	if (VXContainerObject_empty(this_vector))
		return;
	size_t size = ObjectSize(this_vector);

	if (index < 0 || index >= size)
		return;
	char* ptr = ObjectDataPtr(this_vector);//data 的指针
	size_t typeSize = ObjectTypeSize(this_vector);
	//修正n大小
	if (index + n > size|| n < 0)
		n = size-index;
	for (size_t i = 0; i < size - index - n; i++)
	{
		memcpy(ptr + (i + index) * typeSize, ptr + (i + index + n) * typeSize, typeSize);
	}
	ObjectSize(this_vector) -= n;
}
void VXVector_clear(XVector* this_vector)//清空vector的数组
{
	if (VXContainerObject_empty(this_vector))
		return;
	ObjectSize(this_vector) = 0;
	/*if (object->_data != NULL)
	{
		free(object->_data);
		object->_data = NULL;
		object->_capacity = 0;
		object->_size = 0;
	}*/
}
void VXVector_copy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_One, ""))
		return;
	if(ObjectDataPtr(this_One))
		free(ObjectDataPtr(this_One));
	size_t size = ObjectSize(this_Two);
	size_t typeSize = ObjectTypeSize(this_Two);
	if (size > 0)
	{
		ObjectDataPtr(this_One) = malloc(size * typeSize);
		memcpy(ObjectDataPtr(this_One), ObjectDataPtr(this_Two), size * typeSize);
	}
	else
	{
		ObjectDataPtr(this_One) = NULL;
	}
	ObjectCapacity(this_One) = size;
	ObjectSize(this_One) = size;
	ObjectTypeSize(this_One) = typeSize;
}
void VXVector_rcopy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_One, ""))
		return;
	if (ObjectDataPtr(this_One))
		free(ObjectDataPtr(this_One));
	size_t size = ObjectSize(this_Two);
	size_t typeSize = ObjectTypeSize(this_Two);
	if (size > 0)
	{
		ObjectDataPtr(this_One) = malloc(size * typeSize);
		for (char* pst2 = ObjectDataPtr(this_Two) + (size - 1) * typeSize, *pst1 = ObjectDataPtr(this_One); pst2 >= ObjectDataPtr(this_Two); pst2 -= typeSize, pst1 += typeSize)
		{
			memcpy(pst1, pst2, typeSize);
		}
	}
	else
	{
		ObjectDataPtr(this_One) = NULL;
	}
	ObjectCapacity(this_One) = size;
	ObjectSize(this_One) = size;
	ObjectTypeSize(this_One) = typeSize;
}
void* VXVector_at(const XVector* this_vector, int64_t index)// 返回元素的指针
{
	if (ISNULL(this_vector, ""))
		return NULL;
	if (index<0||index + 1 > ObjectSize(this_vector))
	{
		return NULL;
	}
	return (void*)(ObjectDataPtr(this_vector) + ObjectTypeSize(this_vector) * index);
}
void* VXVector_front(const XVector* this_vector)//返回向量头指针，指向第一个元素
{
	/*if (ISNULL(this_vector, ""))
		return NULL;*/
	if (VXContainerObject_empty(this_vector))
		return NULL;
	return ObjectDataPtr(this_vector);
}
void* VXVector_back(const XVector* this_vector)//返回向量尾指针，指向向量最后一个元素
{
	if (VXContainerObject_empty(this_vector))
		return NULL;
	return VXVector_at(this_vector,ObjectSize(this_vector)-1);
}
void* VXVector_find(const XVector* this_vector, const void* findVal)//查找数据，返回找到的指针，没有返回NULL
{
	if (ISNULL(this_vector, "")|| ISNULL(this_vector->equality, ""))
		return NULL;
	for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	{
		if (this_vector->equality(it, findVal))
			return it;
	}
	return NULL;
}
void VXVector_sort(XVector* this_vector, XCompare compare)//排序
{
	if (ObjectSize(this_vector)>1)
		XQuicPitSort_Stack(ObjectDataPtr(this_vector),ObjectSize(this_vector), ObjectTypeSize(this_vector), compare);
}

