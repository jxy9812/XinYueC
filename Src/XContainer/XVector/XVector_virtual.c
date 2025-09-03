#include"XVector.h"
#if XVector_ON
#include"XSort.h"
#include"XVtable.h"
#include<string.h>
#include<stdlib.h>
//声明
#define VECTORNUM 4//初始数组大小
static void VXClass_copy(XVector* object, const XVector* src);
static void VXClass_move(XVector* object, XVector* src);
static bool VXVector_resize(XVector* this_vector, size_t size);
static bool VXVector_push_front(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod);
static bool VXVector_push_back(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod);
static bool VXVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod);
static bool VXVector_append_array(XVector* this_vector, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod);
static void VXVector_pop_front(XVector* this_vector);
static void VXVector_pop_back(XVector* this_vector);
static void VXVector_erase(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next);
static void VXVector_remove(XVector* this_vector, int64_t index, int64_t n);//删除数据 n<0 后面全部删除
static void VXVector_clear(XVector* this_vector);
static void VXVector_rcopy(XVector* this_One, const XVector* this_Two);
static void* VXVector_at(const XVector* this_vector, int64_t index);
static void* VXVector_front(const XVector* this_vector);
static void* VXVector_back(const XVector* this_vector);
static bool VXVector_find(const XVector* this_vector, const void* findVal, XVector_iterator* it);//查找数据,返回迭代器
static void VXVector_sort(XVector* this_vector, XSortOrder order);//排序
XVtable* XVector_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XVector))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = {
		VXVector_resize,
		//插入
		VXVector_push_front,VXVector_push_back,
		VXVector_insert_array,VXVector_append_array,
		//删除
		VXVector_pop_front,VXVector_pop_back,VXVector_erase,VXVector_remove,
		//拷贝
		VXVector_rcopy,
		//遍历
		VXVector_at,VXVector_front,VXVector_back,VXVector_find,
		//排序
		VXVector_sort
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重写的函数
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear,VXVector_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
#if SHOWCONTAINERSIZE
	printf("XVector size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
//初始化函数
void XVector_init(XVector* this_vector, size_t typeSize)
{
	if (ISNULL(this_vector, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_init(this_vector, typeSize);
	XClassGetVtable(this_vector)=XVector_class_init();
	//this_vector->m_equality = NULL;
}
//检测是否需要扩容
static bool VXVectorEnlargeCapacity(XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return false;
	size_t newCapacity;
	if (XContainerCapacity(this_vector) > 100)
		newCapacity = XContainerCapacity(this_vector) * 1.5;
	else
		newCapacity = XContainerCapacity(this_vector) * 2;
	if (XContainerCapacity(this_vector) == 0)
	{
		XContainerDataPtr(this_vector) = XMemory_malloc(XContainerTypeSize(this_vector) * VECTORNUM);
		if (XContainerDataPtr(this_vector) == NULL)
		{
			//perror("初始化vector失败");
			return false;
		}
		else
		{
			XContainerCapacity(this_vector)= VECTORNUM;
		}
	}
	else if (XContainerCapacity(this_vector) == XContainerSize(this_vector))//空间已满需要扩容
	{
		void* m_data = NULL;
		if (XMemory_realloc_isNULL())
		{
			void* ptr = XContainerDataPtr(this_vector);
			uint32_t typeSize = XContainerTypeSize(this_vector);
			m_data = XMemory_malloc(newCapacity * typeSize);
			if (m_data && ptr)
				memcpy(m_data, ptr, typeSize * XContainerCapacity(this_vector));
			if (ptr)
				XMemory_free(ptr);
		}
		else
		{
			m_data = XMemory_realloc(XContainerDataPtr(this_vector), newCapacity * XContainerTypeSize(this_vector));
		}
		XContainerDataPtr(this_vector) = m_data;
		if (m_data == NULL)
		{
			//perror("扩容失败vector");
			XContainerCapacity(this_vector) =0;
			XContainerSize(this_vector) = 0;
			return false;
		}
		else
		{
			XContainerCapacity(this_vector) = newCapacity;
		}
	}
	return true;
}
void VXClass_copy(XVector* object, const XVector* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XVector_init(object, XContainerTypeSize(src));
	}
	else if (!XVector_isEmpty_base(object))
	{
		XVector_clear_base(object);
	}
	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
	XVector_resize_base(object,XVector_size_base(src));
	XContainerSize(object)=0;
	for_each_iterator(src, XVector, it)
	{
		XVector_push_back_base(object, XVector_iterator_data(&it));
	}
	return;
	if (((XClass*)object)->m_vtable != NULL)
	{
		size_t size = XContainerSize(src);
		size_t typeSize = XContainerTypeSize(src);
		if (size > 0)
		{
			if (XContainerCapacity(object) * XContainerTypeSize(object) < size * typeSize)
			{//原先的容量不够扩容
				XMemory_free(XContainerDataPtr(object));
				XContainerDataPtr(object) = XMemory_malloc(size * typeSize);
				XContainerCapacity(object) = size;
			}
			else
			{
				XContainerCapacity(object) = XContainerCapacity(object) * XContainerTypeSize(object) / typeSize;
			}
			memcpy(XContainerDataPtr(object), XContainerDataPtr(src), size * typeSize);
		}
		else
		{
			XContainerDataPtr(object) = NULL;
		}
		XContainerSize(object) = size;
		XContainerTypeSize(object) = typeSize;
	}
	else
	{
		if (XContainerDataPtr(object))
			XMemory_free(XContainerDataPtr(object));
		memcpy(object, src, sizeof(XVector));
		XContainerDataPtr(object) = XMemory_malloc(XContainerSize(object) * XContainerTypeSize(object));
		memcpy(XContainerDataPtr(object), XContainerDataPtr(src), XContainerSize(object) * XContainerTypeSize(object));
		XContainerCapacity(object) = XContainerSize(object);
	}
}
void VXClass_move(XVector* object, XVector* src)
{
	if (XContainerDataPtr(object))
		XMemory_free(XContainerDataPtr(object));
	memcpy(object, src, sizeof(XVector));
	XContainerDataPtr(src) = NULL;
	XContainerCapacity(src) = 0;
	XContainerSize(src) = 0;
}
bool VXVector_resize(XVector* this_vector, size_t size)
{
	size_t capacity =XContainerCapacity(this_vector);//当前容器的最大数量
	size_t count = XContainerSize(this_vector);//当前容器使用的数量
	size_t TypeSize = XContainerTypeSize(this_vector);//数据类型大小
	//XContainerObject* object = this_vector;//数据父类
	if (size <= count)
	{
		for (size_t i = 0; i < count - size; i++)
		{
			XVector_pop_back_base(this_vector);
		}
		return true;
	}
	//char* lpData = XContainerDataPtr(this_vector);
	if (size > capacity)//大于最大容量
	{
		void* m_data = NULL;
		if (XMemory_realloc_isNULL())
		{
			void* ptr = XContainerDataPtr(this_vector);
			m_data = XMemory_malloc(size * TypeSize);
			if (m_data && ptr)
				memcpy(m_data, ptr, size * TypeSize);
			if (ptr)
				XMemory_free(ptr);
		}
		else
		{
			m_data = XMemory_realloc(XContainerDataPtr(this_vector), size * TypeSize);
		}
		XContainerDataPtr(this_vector) = m_data;
		if (m_data == NULL)
		{
			//perror("扩容失败vector");
			//exit(-1);
			XContainerCapacity(this_vector) = 0;
			XContainerSize(this_vector) = 0;
			return false;
		}
		else
		{
			XContainerCapacity(this_vector) = size;
			XContainerSize(this_vector) = size;
		}
		
	}

	char* LPstart = (char*)XContainerDataPtr(this_vector) + count * TypeSize;//最后一个元素的下一个元素
	memset(LPstart, 0, (size - count) * TypeSize);
	//XContainerSize(this_vector) = size;//设置当前容器元素数量
	return true;
}
bool VXVector_push_front(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
	if (XContainerObject_isEmpty_base(this_vector))
		return XVector_push_back_base(this_vector, pvValue);
	else
		return XVector_insert(this_vector, 0, pvValue);
}
bool VXVector_push_back(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
	if (!VXVectorEnlargeCapacity(this_vector))
		return false;
	char* ptr = (char*)XContainerDataPtr(this_vector) + XContainerTypeSize(this_vector) * XContainerSize(this_vector);
	if (dataCreatMethod)
	{
		memset(ptr, 0, XContainerTypeSize(this_vector));
		dataCreatMethod(ptr, pvValue);
	}
	else
	{
		memcpy(ptr, pvValue, XContainerTypeSize(this_vector));
	}
	XContainerSize(this_vector)++;
	return true;
}
bool VXVector_insert_array(XVector* this_vector, int64_t index, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod)// 向量中指向元素p前插入另一个相同类型向量的指针[p1,p2)间的数据
{
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
			if (!VXVectorEnlargeCapacity(this_vector))
			{
				memcpy(VXVector_at(this_vector, sizen), temp, size);
				XMemory_free(temp);
				return false;
			}
			if (dataCreatMethod)
			{
				memset(((char*)XContainerDataPtr(this_vector)) + typeSize * sizen, 0, XContainerTypeSize(this_vector));
				dataCreatMethod(((char*)XContainerDataPtr(this_vector)) + typeSize * sizen, (char*)begin + i * typeSize);
			}
			else
			{
				memcpy(((char*)XContainerDataPtr(this_vector)) + typeSize * sizen, (char*)begin + i * typeSize, typeSize);
			}
			sizen++;
			XContainerSize(this_vector)++;
		}
		memcpy(VXVector_at(this_vector, sizen), temp, size);
		XMemory_free(temp);
	}
	return true;
	/*else if(XContainerObject_isEmpty_base(this_vector))
	{
		for (size_t i = 0; i < n; i++)
		{
			XVector_push_back_base(this_vector, (char*)begin+i*XContainerTypeSize(this_vector));
		}
	}*/
}
bool VXVector_append_array(XVector* this_vector, const void* begin, size_t n, XCDataCreatMethod dataCreatMethod)
{
	size_t index = XContainerSize(this_vector);
	//printf("数组Size:%d Capacity:%d n:%d\n", index, XContainerCapacity(this_vector),n);
	if (XContainerSize(this_vector) + n > XContainerCapacity(this_vector))
	{
		if (!VXVector_resize(this_vector, XContainerSize(this_vector) + n))
			return false;
	}
	else
	{
		XContainerSize(this_vector) += n;
	}
	//printf("数组Size:%d Capacity:%d\n",XContainerSize(this_vector), XContainerCapacity(this_vector));
	if (dataCreatMethod)
	{
		memset((char*)XContainerDataPtr(this_vector) + index * XContainerTypeSize(this_vector), 0, n * XContainerTypeSize(this_vector));
		for (size_t i = 0; i < n; i++)
		{
			dataCreatMethod((char*)XContainerDataPtr(this_vector) + (index + i) * XContainerTypeSize(this_vector), ((char*)begin) + i * XContainerTypeSize(this_vector));
		}
	}
	else
	{
		memcpy((char*)XContainerDataPtr(this_vector) + index * XContainerTypeSize(this_vector), begin, n * XContainerTypeSize(this_vector));
	}
	return true;
}
void VXVector_pop_front(XVector* this_vector)//删除向量中第一个元素
{
	VXVector_remove(this_vector, 0, 1);
	//if (VXContainerObject_isEmpty(this_vector))
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
	if (XContainerObject_isEmpty_base(this_vector))
		return ;
	if (XContainerDataDeinitMethod(this_vector) != NULL)
		XContainerDataDeinitMethod(this_vector)(XVector_back_base(this_vector));
	--XContainerSize(this_vector);
}
void VXVector_erase(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next)//删除指针数据
{
	if (XVector_isEmpty_base(this_vector) || it->data == NULL)
	{
		if (next)
			*next = XVector_end(this_vector);
	}
	void* pvValue = XVector_iterator_data(it);
	void* front = XVector_front_base(this_vector),* back= XVector_back_base(this_vector);
	size_t typeSize = XContainerTypeSize(this_vector);
	if (front <= pvValue && pvValue <= back && ((char*)pvValue - (char*)front) % typeSize == 0)
	{
		if (XContainerSize(this_vector) == 1)
		{
			XContainerSize(this_vector) = 0;
			if (next)
				*next = XVector_end(this_vector);
		}
		else
		{
			if (XContainerDataDeinitMethod(this_vector) != NULL)
				XContainerDataDeinitMethod(this_vector)(pvValue);
			--XContainerSize(this_vector);
			if(pvValue== back&& next!=NULL)//不是最后一个
				*next = XVector_end(this_vector);
					
			memcpy(pvValue, (char*)pvValue + typeSize, (size_t)((char*)back - (char*)pvValue));
			if (next)
				*next = *it;
		}
	}
	if (next)
		*next = XVector_end(this_vector);
}
void VXVector_remove(XVector* this_vector, int64_t index, int64_t n)//删除数据 n<0 后面全部删除
{
	if (XContainerObject_isEmpty_base(this_vector))
		return;
	size_t size = XContainerSize(this_vector);

	if (index < 0 || index >= size)
		return;
	char* ptr = XContainerDataPtr(this_vector);//data 的指针
	size_t typeSize = XContainerTypeSize(this_vector);
	//修正n大小
	if (index + n > size|| n < 0)
		n = size-index;
	//释放数据
	if (XContainerDataDeinitMethod(this_vector) != NULL)
	{
		for (size_t i = 0; i < n; i++)
		{
			XContainerDataDeinitMethod(this_vector)(ptr+(i+index)* typeSize);
		}
	}
	for (size_t i = 0; i < size - index - n; i++)
	{
		char* p = ptr + (i + index) * typeSize;
		memcpy(p, p+n * typeSize, typeSize);
		//memcpy(ptr + (i + index) * typeSize, ptr + (i + index + n) * typeSize, typeSize);
	}
	XContainerSize(this_vector) -= n;
}
void VXVector_clear(XVector* this_vector)//清空vector的数组
{
	if (XContainerObject_isEmpty_base(this_vector))
		return;
	//释放数据
	if (XContainerDataDeinitMethod(this_vector) != NULL)
	{
		//for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
		for (size_t i = 0; i < XContainerSize(this_vector); i++)
		{
			XContainerDataDeinitMethod(this_vector)(((uint8_t*)XContainerDataPtr(this_vector)) + i * XContainerTypeSize(this_vector));
		}
	}
	XContainerSize(this_vector) = 0;
	/*if (object->m_data != NULL)
	{
		XMemory_free(object->m_data);
		object->m_data = NULL;
		object->m_capacity = 0;
		object->m_size = 0;
	}*/
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
	if (index<0||index + 1 > XContainerSize(this_vector))
	{
		return NULL;
	}
	return (void*)((char*)XContainerDataPtr(this_vector) + XContainerTypeSize(this_vector) * index);
}
void* VXVector_front(const XVector* this_vector)//返回向量头指针，指向第一个元素
{
	if (XContainerObject_isEmpty_base(this_vector))
		return NULL;
	return XContainerDataPtr(this_vector);
}
void* VXVector_back(const XVector* this_vector)//返回向量尾指针，指向向量最后一个元素
{
	if (XContainerObject_isEmpty_base(this_vector))
		return NULL;
	if (XContainerSize(this_vector) == 1)
		return VXVector_front(this_vector);
	return VXVector_at(this_vector,XContainerSize(this_vector)-1);
}
bool VXVector_find(const XVector* this_vector, const void* findVal, XVector_iterator* it)//查找数据，返回找到的指针，没有返回NULL
{
	if (ISNULL(this_vector, "")|| XVector_isEmpty_base(this_vector))
	{
		if (it)
			*it = XVector_end(this_vector);
		return false;
	}
	//for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	for (size_t i = 0; i < XContainerSize(this_vector); i++)
	{
		void* data = ((uint8_t*)XContainerDataPtr(this_vector)) + i * XContainerTypeSize(this_vector);
		if (XContainerCompare(this_vector))
		{
			if (XContainerCompare(this_vector)(data, findVal)==XCompare_Equality)
			{
				if (it)
					it->data = data;
				return true;
			}
		}
		else if (memcmp(data, findVal, XContainerTypeSize(this_vector)) == 0)
		{
			if (it)
				it->data = data;
			return true;
		}
		
	}
	if (it)
		*it = XVector_end(this_vector);
	return false;
}
void VXVector_sort(XVector* this_vector, XSortOrder order)//排序
{
	if (XContainerSize(this_vector)>1)
		XQuicPitSort_Stack(XContainerDataPtr(this_vector),XContainerSize(this_vector), XContainerTypeSize(this_vector),XContainerCompare(this_vector), order);
}

#endif