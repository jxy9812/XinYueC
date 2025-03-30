#include"XVector.h"
#if XVector_ON
#include"XSort.h"
#include"XVtable.h"
#include<string.h>
#include<stdlib.h>
#include"XList.h"
//声明
#define VECTORNUM 4//初始数组大小
//虚函数表定义
XVtable* XVectorVtable=NULL;
static void VXVector_resize(XVector* this_vector, size_t size);
static void VXVector_push_front(XVector* this_vector, void* LpValue);
static void VXVector_push_back(XVector* this_vector, void* LpValue);
static void VXVector_inserts(XVector* this_vector, int64_t index, void* LpValue, size_t n);
static void VXVector_insert(XVector* this_vector, int64_t index, const void* LpValue);
static void VXVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n);
static void VXVector_append_array(XVector* this_vector, const void* begin, size_t n);
static void VXVector_pop_front(XVector* this_vector);
static void VXVector_pop_back(XVector* this_vector);
static void VXVector_erase(XVector* this_vector, void* LpValue);
static void VXVector_remove(XVector* this_vector, int64_t index, int64_t n);//删除数据 n<0 后面全部删除
static void VXVector_clear(XVector* this_vector);
static void VXVector_copy(XVector* this_One, const XVector* this_Two);
static void VXVector_rcopy(XVector* this_One, const XVector* this_Two);
static void* VXVector_at(const XVector* this_vector, int64_t index);
static void* VXVector_front(const XVector* this_vector);
static void* VXVector_back(const XVector* this_vector);
static void* VXVector_find(const XVector* this_vector, const void* findVal);//查找数据，返回找到的指针，没有返回NULL
static void VXVector_sort(XVector* this_vector, XCompare compare);//排序
#if VTABLEISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[25];//虚函数数据
#endif
void XVector_class_init()
{
	if (XVectorVtable)
		return;
	void* table[] = {
		VXVector_resize,
		//插入
	VXVector_push_front,VXVector_push_back,VXVector_inserts,VXVector_insert,VXVector_insert_array,VXVector_append_array,
		//删除
		VXVector_pop_front,VXVector_pop_back,VXVector_erase,VXVector_remove,
		//拷贝
		VXVector_copy,VXVector_rcopy,
		//遍历
		VXVector_at,VXVector_front,VXVector_back,VXVector_find,
		//排序
		VXVector_sort
	};
#if !VTABLEISSTACK
	XVectorVtable = XVtable_new();
#else
	XVectorVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XVectorVtable,XContainerObjectVtable);
	//追加函数
	XVtable_append_array(XVectorVtable, table, sizeof(table)/sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XVector size:%d\n", XVtable_size(XVectorVtable));
#endif // SHOWCONTAINERSIZE
}

//检测是否需要扩容
static void VXVectorEnlargeCapacity(XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return;
	if (XContainerCapacity(this_vector) == 0)
	{
		XContainerDataPtr(this_vector) = XMemory_malloc(XContainerTypeSize(this_vector) * VECTORNUM);
		if (XContainerDataPtr(this_vector) == NULL)
		{
			perror("初始化vector失败");
			exit(-1);
		}
		else
		{
			XContainerCapacity(this_vector)= VECTORNUM;
		}
	}
	else if (XContainerCapacity(this_vector) == XContainerSize(this_vector))//空间已满需要扩容
	{
		void* _data = NULL;
		if (XMemory_realloc_isNULL())
		{
			void* ptr = XContainerDataPtr(this_vector);
			uint32_t typeSize = XContainerTypeSize(this_vector);
			_data = XMemory_malloc(XContainerCapacity(this_vector) * typeSize * 1.5);
			if (_data && ptr)
				memcpy(_data, ptr, typeSize * XContainerCapacity(this_vector));
			if (ptr)
				XMemory_free(ptr);
		}
		else
		{
			_data = XMemory_realloc(XContainerDataPtr(this_vector), XContainerCapacity(this_vector) * XContainerTypeSize(this_vector) * 1.5);
		}
		XContainerDataPtr(this_vector) = _data;
		if (_data == NULL)
		{
			perror("扩容失败vector");
			XContainerCapacity(this_vector) =0;
			XContainerSize(this_vector) = 0;
		}
		else
		{
			XContainerCapacity(this_vector) *= 1.5;
		}
	}
}
void VXVector_resize(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, ""))
		return;
	size_t capacity =XContainerCapacity(this_vector);//当前容器的最大数量
	size_t count = XContainerSize(this_vector);//当前容器使用的数量
	size_t TypeSize = XContainerTypeSize(this_vector);//数据类型大小
	//XContainerObject* object = this_vector;//数据父类
	if (size <= count)
	{
		for (size_t i = 0; i < count - size; i++)
		{
			XVector_pop_back(this_vector);
		}
		return;
	}
	//char* lpData = XContainerDataPtr(this_vector);
	if (size > capacity)//大于最大容量
	{
		void* _data = NULL;
		if (XMemory_realloc_isNULL())
		{
			void* ptr = XContainerDataPtr(this_vector);
			_data = XMemory_malloc(size * TypeSize);
			if (_data && ptr)
				memcpy(_data, ptr, size * TypeSize);
			if (ptr)
				XMemory_free(ptr);
		}
		else
		{
			_data = XMemory_realloc(XContainerDataPtr(this_vector), size * TypeSize);
		}
		XContainerDataPtr(this_vector) = _data;
		if (_data == NULL)
		{
			perror("扩容失败vector");
			//exit(-1);
			XContainerCapacity(this_vector) = 0;
			XContainerSize(this_vector) = 0;
		}
		else
		{
			XContainerCapacity(this_vector) = size;
		}
		
	}

	char* LPstart = (char*)XContainerDataPtr(this_vector) + count * TypeSize;//最后一个元素的下一个元素
	memset(LPstart, 0, (size - count) * TypeSize);
	//XContainerSize(this_vector) = size;//设置当前容器元素数量
	//return;
}
void VXVector_push_front(XVector* this_vector, void* LpValue)
{
	if (XContainerObject_empty(this_vector))
		VXVector_push_back(this_vector, LpValue);
	else
		VXVector_insert(this_vector, 0, LpValue);
}
void VXVector_push_back(XVector* this_vector, void* LpValue)
{
	if (ISNULL(this_vector, ""))
		return;
	VXVectorEnlargeCapacity(this_vector);
	char* ptr = (char*)XContainerDataPtr(this_vector) + XContainerTypeSize(this_vector) * XContainerSize(this_vector);
	memcpy(ptr, LpValue, XContainerTypeSize(this_vector));
	XContainerSize(this_vector)++;
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
	size_t typeSize = XContainerTypeSize(this_vector);
	if (ptr&&ptr >= VXVector_front(this_vector) && ptr <= VXVector_back(this_vector))
	{
		int64_t size = (char*)VXVector_back(this_vector) - (char*)ptr + typeSize;
		void* temp = XMemory_malloc(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)VXVector_front(this_vector)) / typeSize;
		for (size_t i = 0; i < n; i++)
		{
			VXVectorEnlargeCapacity(this_vector);
			memcpy(VXVector_at(this_vector, sizen), (char*)LpValue+i*typeSize, typeSize);
			sizen++;
			XContainerSize(this_vector)++;
		}
		memcpy(VXVector_at(this_vector, sizen), temp, size);
		XMemory_free(temp);
	}
	/*else if (XContainerObject_empty(this_vector))
	{
		for (size_t i = 0; i < n; i++)
		{
			XVector_push_back(this_vector, LpValue);
		}
	}*/
}
void VXVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
	if (ISNULL(this_vector, "")|| ISNULL(index, "")|| ISNULL(begin, "")|| ISNULL(n, ""))
		return;
	const void* ptr = VXVector_at(this_vector, index);
	size_t typeSize = XContainerTypeSize(this_vector);
	if (ptr&&ptr >= VXVector_front(this_vector) && ptr <= VXVector_back(this_vector))
	{
		int64_t size = (char*)VXVector_back(this_vector) - (char*)ptr + typeSize;
		void* temp = XMemory_malloc(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)VXVector_front(this_vector)) / typeSize;
		for (size_t i = 0; i < n; i++)
		{
			VXVectorEnlargeCapacity(this_vector);
			memcpy(VXVector_at(this_vector, sizen), (char*)begin + i * typeSize, typeSize);
			sizen++;
			XContainerSize(this_vector)++;
		}
		memcpy(VXVector_at(this_vector, sizen), temp, size);
		XMemory_free(temp);
	}
	/*else if(XContainerObject_empty(this_vector))
	{
		for (size_t i = 0; i < n; i++)
		{
			XVector_push_back(this_vector, (char*)begin+i*XContainerTypeSize(this_vector));
		}
	}*/
}
void VXVector_append_array(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") ||  ISNULL(begin, "") || ISNULL(n, ""))
		return;
	if(XContainerSize(this_vector) + n> XContainerCapacity(this_vector))
		VXVector_resize(this_vector,XContainerSize(this_vector)+n);
	memcpy((char*)XContainerDataPtr(this_vector) + (XContainerSize(this_vector))* XContainerTypeSize(this_vector),begin,n* XContainerTypeSize(this_vector));
	XContainerSize(this_vector)+=n;
}
void VXVector_pop_front(XVector* this_vector)//删除向量中第一个元素
{
	VXVector_remove(this_vector, 0, 1);
	//if (VXContainerObject_empty(this_vector))
	//	return NULL;
	//--XContainerSize(this_vector);
	//if (XContainerSize(this_vector) > 0)
	//{//不止一个的时候
	//	char* ptr = XContainerDataPtr(this_vector);//data 的指针
	//	size_t typeSize = XContainerTypeSize(this_vector);
	//	for (size_t i = 0; i < XContainerSize(this_vector); i++)
	//	{
	//		memcpy(ptr +i* typeSize, ptr + (i+1) * typeSize, typeSize);
	//	}
	//}

}
void VXVector_pop_back(XVector* this_vector)//删除向量中最后一个元素
{
	if (XContainerObject_empty(this_vector))
		return ;
	--XContainerSize(this_vector);
}
void VXVector_erase(XVector* this_vector, void* LpValue)//删除指针数据
{
	if (XContainerObject_empty(this_vector))
		return ;
	if (VXVector_front(this_vector) <= LpValue && LpValue <= VXVector_back(this_vector) && ((char*)LpValue - (char*)VXVector_front(this_vector)) % XContainerTypeSize(this_vector) == 0)
	{
		size_t typeSize = XContainerTypeSize(this_vector);
		memcpy(LpValue, (char*)LpValue + typeSize, (size_t)((char*)VXVector_back(this_vector) - (char*)LpValue - typeSize));
		--XContainerSize(this_vector);
	}
}
void VXVector_remove(XVector* this_vector, int64_t index, int64_t n)//删除数据 n<0 后面全部删除
{
	if (XContainerObject_empty(this_vector))
		return;
	size_t size = XContainerSize(this_vector);

	if (index < 0 || index >= size)
		return;
	char* ptr = XContainerDataPtr(this_vector);//data 的指针
	size_t typeSize = XContainerTypeSize(this_vector);
	//修正n大小
	if (index + n > size|| n < 0)
		n = size-index;
	for (size_t i = 0; i < size - index - n; i++)
	{
		memcpy(ptr + (i + index) * typeSize, ptr + (i + index + n) * typeSize, typeSize);
	}
	XContainerSize(this_vector) -= n;
}
void VXVector_clear(XVector* this_vector)//清空vector的数组
{
	if (XContainerObject_empty(this_vector))
		return;
	XContainerSize(this_vector) = 0;
	/*if (object->_data != NULL)
	{
		XMemory_free(object->_data);
		object->_data = NULL;
		object->_capacity = 0;
		object->_size = 0;
	}*/
}
void VXVector_copy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_One, ""))
		return;
	if(XContainerDataPtr(this_One))
		XMemory_free(XContainerDataPtr(this_One));
	size_t size = XContainerSize(this_Two);
	size_t typeSize = XContainerTypeSize(this_Two);
	if (size > 0)
	{
		XContainerDataPtr(this_One) = XMemory_malloc(size * typeSize);
		memcpy(XContainerDataPtr(this_One), XContainerDataPtr(this_Two), size * typeSize);
	}
	else
	{
		XContainerDataPtr(this_One) = NULL;
	}
	XContainerCapacity(this_One) = size;
	XContainerSize(this_One) = size;
	XContainerTypeSize(this_One) = typeSize;
}
void VXVector_rcopy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_One, ""))
		return;
	if (XContainerDataPtr(this_One))
		XMemory_free(XContainerDataPtr(this_One));
	size_t size = XContainerSize(this_Two);
	size_t typeSize = XContainerTypeSize(this_Two);
	if (size > 0)
	{
		XContainerDataPtr(this_One) = XMemory_malloc(size * typeSize);
		for (char* pst2 = (char*)XContainerDataPtr(this_Two) + (size - 1) * typeSize, *pst1 = XContainerDataPtr(this_One); pst2 >= XContainerDataPtr(this_Two); pst2 -= typeSize, pst1 += typeSize)
		{
			memcpy(pst1, pst2, typeSize);
		}
	}
	else
	{
		XContainerDataPtr(this_One) = NULL;
	}
	XContainerCapacity(this_One) = size;
	XContainerSize(this_One) = size;
	XContainerTypeSize(this_One) = typeSize;
}
void* VXVector_at(const XVector* this_vector, int64_t index)// 返回元素的指针
{
	if (ISNULL(this_vector, ""))
		return NULL;
	if (index<0||index + 1 > XContainerSize(this_vector))
	{
		return NULL;
	}
	return (void*)((char*)XContainerDataPtr(this_vector) + XContainerTypeSize(this_vector) * index);
}
void* VXVector_front(const XVector* this_vector)//返回向量头指针，指向第一个元素
{
	/*if (ISNULL(this_vector, ""))
		return NULL;*/
	if (XContainerObject_empty(this_vector))
		return NULL;
	return XContainerDataPtr(this_vector);
}
void* VXVector_back(const XVector* this_vector)//返回向量尾指针，指向向量最后一个元素
{
	if (XContainerObject_empty(this_vector))
		return NULL;
	if (XContainerSize(this_vector) == 1)
		return VXVector_front(this_vector);
	return VXVector_at(this_vector,XContainerSize(this_vector)-1);
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
	if (XContainerSize(this_vector)>1)
		XQuicPitSort_Stack(XContainerDataPtr(this_vector),XContainerSize(this_vector), XContainerTypeSize(this_vector), compare);
}

#endif