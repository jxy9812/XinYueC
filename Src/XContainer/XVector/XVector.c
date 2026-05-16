#include"XVector.h"
#if XVector_ON
#include"XSort.h"
#include"XVtable.h"
#include<string.h>
#include<stdlib.h>
//声明
#define VECTORNUM 20//初始数组大小

// COW分离：如果数据被共享，创建独立副本
static bool VXVectorDetachIfNeeded(XVector* this_vector);
static void VXVectorDataDelete(void*data, XVector* this_vector);
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
	XVTABLE_INHERIT_XCLASS(XContainer);
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
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear,VXVector_clear);
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
	XContainer_init(this_vector, typeSize);
	XClassGetVtable(this_vector)=XVector_class_init();
	//this_vector->m_equality = NULL;
}
//检测是否需要扩容
static bool VXVectorEnlargeCapacity(XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return false;
	
	// 如果数据被共享，需要分离（Copy-On-Write）
	if (!VXVectorDetachIfNeeded(this_vector))
		return false;

	// 正常扩容逻辑
	size_t newCapacity;
	if (XContainerCapacity(this_vector) > 100)
		newCapacity = XContainerCapacity(this_vector) * 1.5;
	else
		newCapacity = XContainerCapacity(this_vector) * 2;
	
	size_t bytes = ALIGN_UP(newCapacity * XContainerTypeSize(this_vector), sizeof(void*));
	
	if (XContainerCapacity(this_vector) == 0)
	{
		// 初始分配
		void* newData = XMalloc_System(ALIGN_UP(XContainerTypeSize(this_vector) * VECTORNUM, sizeof(void*)));
		if (newData == NULL)
			return false;
		
		// 创建 XSharedData
		XSharedData* shared = XSharedData_create(newData);
		if (!shared)
		{
			XFree_System(newData);
			return false;
		}
		
		XContainerSharedData(this_vector) = shared;
		XContainerCapacity(this_vector) = VECTORNUM;
	}
	else if (XContainerCapacity(this_vector) == XContainerSize(this_vector)) // 空间已满需要扩容
	{
		void* oldData = XContainerDataPtr(this_vector);
		void* newData = NULL;
		
		if (XMemory_realloc_isNULL(XMEMORY_TYPE_SYSTEM))
		{
			newData = XMalloc_System(bytes);
			if (newData && oldData)
				memcpy(newData, oldData, XContainerTypeSize(this_vector) * XContainerCapacity(this_vector));
			if (oldData)
				XFree_System(oldData);
		}
		else
		{
			newData = XRealloc_System(oldData, bytes);
		}
		
		if (newData == NULL)
		{
			XContainerCapacity(this_vector) = 0;
			XContainerSize(this_vector) = 0;
			return false;
		}
		
		// 更新 XSharedData 的数据指针
		if (XContainerSharedData(this_vector))
			((XContainer*)this_vector)->m_data->data = newData;
		else
			XContainerSharedData(this_vector) = XSharedData_create(newData);
		
		XContainerCapacity(this_vector) = newCapacity;
	}
	return true;
}
bool VXVectorDetachIfNeeded(XVector* this_vector)
{
	if (!XContainerSharedData(this_vector) || !XSharedData_isShared(XContainerSharedData(this_vector)))
		return true; // 不共享，无需分离

	void* oldData = XContainerDataPtr(this_vector);
	size_t size = XContainerSize(this_vector);
	size_t capacity = XContainerCapacity(this_vector);
	size_t typeSize = XContainerTypeSize(this_vector);

	if (capacity == 0 || typeSize == 0)
		return true;

	size_t bytes = ALIGN_UP(capacity * typeSize, sizeof(void*));
	void* newData = XMalloc_System(bytes);
	if (!newData)
		return false;
	// 创建新的 XSharedData
	XSharedData* newShared = XSharedData_create(newData);
	if (!newShared)
	{
		XFree_System(newData);
		return false;
	}
	if (XContainerDataCopyMethod(this_vector))
	{
		for (size_t i = 0; i < XContainerSize(this_vector); i++)
		{
			XContainerDataCopyMethod(this_vector)((char*)newData + i * typeSize, (char*)oldData + i * typeSize);
		}
	}
	else
	{
		// 拷贝数据
		if (oldData && size > 0)
			memcpy(newData, oldData, size * typeSize);
	}
	// 减少旧引用，设置新引用
	XSharedData_release(XContainerSharedData(this_vector));
	XContainerSharedData(this_vector) = newShared;
	return true;
}
void VXVectorDataDelete(void* data, XVector* this_vector)
{
	//释放数据
	if (XContainerDataDeinitMethod(this_vector) != NULL)
	{
		for (size_t i = 0; i < XContainerSize(this_vector); i++)
		{
			XContainerDataDeinitMethod(this_vector)(((uint8_t*)XContainerDataPtr(this_vector)) + i * XContainerTypeSize(this_vector));
		}
	}
	XContainerSize(this_vector) = 0;
	XFree_System(data);
}
void VXClass_copy(XVector* object, const XVector* src)
{
	// 如果目标还未初始化，先初始化
	if (((XClass*)object)->m_vtable == NULL)
	{
		XVector_init(object, XContainerTypeSize(src));
	}
	else if (XContainerSharedData(object))// 释放目标原有数据
	{
		XSharedData_release_with(XContainerSharedData(object), VXVectorDataDelete, object);
	}

	// 复制回调函数
	XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
	XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
	XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

	// 共享源数据的 XSharedData（COW 机制）
	XContainerSharedData(object) = XContainerSharedData(src);
	if (XContainerSharedData(object))
	{
		XSharedData_addRef(XContainerSharedData(object));
	}
	
	XContainerSize(object) = XContainerSize(src);
	XContainerCapacity(object) = XContainerCapacity(src);
	XContainerTypeSize(object) = XContainerTypeSize(src);
}

void VXClass_move(XVector* object, XVector* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XVector_init(object, XContainerTypeSize(src));
	}
	else if (XContainerSharedData(object))
	{
		XSharedData_release_with(XContainerSharedData(object), VXVectorDataDelete, object);
	}

	// 转移所有权（指针拷贝）
	memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XVector) - sizeof(XClass));
	
	// 清空源对象
	XContainerSharedData(src) = NULL;
	XContainerCapacity(src) = 0;
	XContainerSize(src) = 0;
}
bool VXVector_resize(XVector* this_vector, size_t size)
{
	size_t capacity = XContainerCapacity(this_vector);
	size_t count = XContainerSize(this_vector);
	size_t typeSize = XContainerTypeSize(this_vector);
	size_t bytes = ALIGN_UP(size * typeSize, sizeof(void*));
	
	// 缩小：删除尾部元素
		if (size <= count)
		{
			if (!VXVectorDetachIfNeeded(this_vector))
				return false;
			// 批量析构被删除的元素
			if (XContainerDataDeinitMethod(this_vector))
			{
				for (size_t i = size; i < count; i++)
				{
					XContainerDataDeinitMethod(this_vector)((char*)XContainerDataPtr(this_vector) + i * typeSize);
				}
			}
			XContainerSize(this_vector) = size;
			return true;
		}
	
	// 扩大：需要检查 COW 并分配新内存
	if (size > capacity)
	{
		XSharedData* sd = ((XContainer*)this_vector)->m_data;
		bool isShared = sd && XSharedData_isShared(sd);
		void* oldData = sd ? sd->data : NULL;
		void* newData = NULL;
		
		if (isShared)
		{
			// COW：分配新内存，不修改共享数据
			newData = XMalloc_System(bytes);
			if (!newData) return false;
			if (oldData && count > 0)
				memcpy(newData, oldData, count * typeSize);
			
			// 创建新的 XSharedData
			XSharedData* newShared = XSharedData_create(newData);
			if (!newShared)
			{
				XFree_System(newData);
				return false;
			}
			// 减少旧引用，设置新引用
			XSharedData_release(sd);
			((XContainer*)this_vector)->m_data = newShared;
		}
		else
		{
			// 不共享，可以直接 realloc 并更新 data 指针
			if (XMemory_realloc_isNULL(XMEMORY_TYPE_SYSTEM))
			{
				newData = XMalloc_System(bytes);
				if (newData && oldData && count > 0)
					memcpy(newData, oldData, count * typeSize);
				if (oldData) XFree_System(oldData);
			}
			else
			{
				newData = XRealloc_System(oldData, bytes);
			}
			if (!newData)
			{
				XContainerCapacity(this_vector) = 0;
				XContainerSize(this_vector) = 0;
				if (sd) XSharedData_release(sd);
				XContainerSharedData(this_vector) = NULL;
				return false;
			}
			// 更新现有 XSharedData 的 data 指针，或创建新的
			if (sd)
				sd->data = newData;
			else
				XContainerSharedData(this_vector) = XSharedData_create(newData);
		}
		XContainerCapacity(this_vector) = size;
	}
	
	// 初始化新增元素为 0
	char* LPstart = (char*)XContainerDataPtr(this_vector) + count * typeSize;
	memset(LPstart, 0, (size - count) * typeSize);
	XContainerSize(this_vector) = size;
	return true;
}
bool VXVector_push_front(XVector* this_vector, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
	if (XContainer_isEmpty_base(this_vector))
		return XClassGetVirtualFunc(this_vector, EXVector_Push_Back, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, dataCreatMethod);
		//return XVector_push_back_base(this_vector, pvValue);
	else
		//return XVector_insert(this_vector, 0, pvValue);
		return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array, bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, 0, pvValue, 1, dataCreatMethod);
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
	// --- 1. 输入验证 ---
	if (!this_vector || !begin || n == 0) {
		return false;
	}
	size_t current_size = XContainerSize(this_vector);
	size_t typeSize = XContainerTypeSize(this_vector);

	// --- 2. 验证索引的有效性 ---
	// 合法的插入索引范围是 [0, current_size]
	if (index < 0 || index >(int64_t)current_size) {
		return false;
	}
	// --- 3. 处理空容器或在末尾追加的特殊情况 ---
	if (current_size == 0 || index == (int64_t)current_size) {
		// 直接使用 append 逻辑，这是最简单高效的方式
		return XClassGetVirtualFunc(this_vector, EXVector_append_Array, bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, dataCreatMethod);
	}
	const void* ptr = VXVector_at(this_vector, index);
	
	if (ptr&&ptr >= VXVector_front(this_vector) && ptr <= VXVector_back(this_vector))
	{
		int64_t size = (char*)VXVector_back(this_vector) - (char*)ptr + typeSize;
		void* temp = XMalloc_System(size);
		memcpy(temp, ptr, size);
		int64_t sizen = ((char*)ptr - (char*)VXVector_front(this_vector)) / typeSize;
		for (size_t i = 0; i < n; i++)
		{
			if (!VXVectorEnlargeCapacity(this_vector))
			{
				memcpy(VXVector_at(this_vector, sizen), temp, size);
				XFree_System(temp);
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
		XFree_System(temp);
	}
	return true;
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
	//if (VXContainer_isEmpty(this_vector))
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
	if (XContainer_isEmpty_base(this_vector))
		return;
	if (!VXVectorDetachIfNeeded(this_vector))
		return;
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
		return;
	}
	if (!VXVectorDetachIfNeeded(this_vector))
	{
		if (next)
			*next = XVector_end(this_vector);
		return;
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
	if (XContainer_isEmpty_base(this_vector))
		return;
	if (!VXVectorDetachIfNeeded(this_vector))
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
	if (XContainer_isEmpty_base(this_vector))
		return;
	
	if (XContainerSharedData(this_vector) && XSharedData_isShared(XContainerSharedData(this_vector)))
	{
		// 减少旧引用，设置新引用
		XSharedData_release(XContainerSharedData(this_vector));
		XContainerSharedData(this_vector) = NULL;
		XContainerCapacity(this_vector) = 0;
	}
	else
	{
		//释放数据
		if (XContainerDataDeinitMethod(this_vector) != NULL)
		{
			//for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
			for (size_t i = 0; i < XContainerSize(this_vector); i++)
			{
				XContainerDataDeinitMethod(this_vector)(((uint8_t*)XContainerDataPtr(this_vector)) + i * XContainerTypeSize(this_vector));
			}
		}
	}

	XContainerSize(this_vector) = 0;
	/*if (object->m_data != NULL)
	{
		XFree_System(object->m_data);
		object->m_data = NULL;
		object->m_capacity = 0;
		object->m_size = 0;
	}*/
}
void VXVector_rcopy(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
		return;
	if (((XClass*)this_One)->m_vtable == NULL)
	{
		XVector_init(this_One, XContainerTypeSize(this_Two));
	}
	else if (XContainerSharedData(this_One))// 释放目标原有数据
	{
		XSharedData_release_with(XContainerSharedData(this_One), VXVectorDataDelete, this_One);
	}

	// 复制回调函数
	XContainerSetDataCopyMethod(this_One, XContainerDataCopyMethod(this_Two));
	XContainerSetDataMoveMethod(this_One, XContainerDataMoveMethod(this_Two));
	XContainerSetDataDeinitMethod(this_One, XContainerDataDeinitMethod(this_Two));
	
	size_t size = XContainerSize(this_Two);
	size_t typeSize = XContainerTypeSize(this_Two);
	
	if (size > 0)
	{
		void* newData = XMalloc_System(size * typeSize);
		if (!newData) return;
		
		// 逆序拷贝
		for (char* pst2 = (char*)XContainerDataPtr(this_Two) + (size - 1) * typeSize, *pst1 = newData; pst2 >= XContainerDataPtr(this_Two); pst2 -= typeSize, pst1 += typeSize)
		{
			memcpy(pst1, pst2, typeSize);
		}
		
		// 创建新的 XSharedData
		XSharedData* newShared = XSharedData_create(newData);
		if (!newShared)
		{
			XFree_System(newData);
			return;
		}
		((XContainer*)this_One)->m_data = newShared;
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
	if (XContainer_isEmpty_base(this_vector))
		return NULL;
	return XContainerDataPtr(this_vector);
}
void* VXVector_back(const XVector* this_vector)//返回向量尾指针，指向向量最后一个元素
{
	if (XContainer_isEmpty_base(this_vector))
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
	if (XContainerSize(this_vector) <= 1)
		return;
	if (!VXVectorDetachIfNeeded(this_vector))
		return;
	XQuicPitSort_Stack(XContainerDataPtr(this_vector),XContainerSize(this_vector), XContainerTypeSize(this_vector),XContainerCompare(this_vector), order);
}
XVector* XVector_create(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XVector* this_vector = XMalloc_System(sizeof(XVector));
	XVector_init(this_vector, typeSize);
	Set_Class_MemoryFree(this_vector, XFree_System);
	return this_vector;
}

XVector* XVector_create_copy(const XVector* other)
{
	if (other == NULL)
		return NULL;
	XVector* v = XVector_create(XContainerTypeSize(other));
	XVector_copy_base(v, other);
	return v;
}

XVector* XVector_create_move(XVector* other)
{
	if (other == NULL)
		return NULL;
	XVector* v = XVector_create(XContainerTypeSize(other));
	XVector_move_base(v, other);
	return v;
}

bool XVector_resize_base(XVector* this_vector, size_t size)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Resize, bool (*)(XVector*, size_t))(this_vector, size);
}

bool XVector_push_front_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Front, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_front_move_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Front, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_push_back_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Back, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataCopyMethod(this_vector));
}

bool XVector_push_back_move_base(XVector* this_vector, void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Push_Back, bool (*)(XVector*, void*, XCDataCreatMethod))(this_vector, pvValue, XContainerDataMoveMethod(this_vector));
}

bool XVector_insert(XVector* this_vector, int64_t index, const void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XVector_insert_array_base(this_vector, index, pvValue, 1);
	//XClassGetVirtualFunc(this_vector, EXVector_Insert_Copy, void (*)(XVector*, int64_t, void*))(this_vector,index, pvValue);
}

bool XVector_insert_move(XVector* this_vector, int64_t index, const void* pvValue)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XVector_insert_array_move_base(this_vector, index, pvValue, 1);
	//XClassGetVirtualFunc(this_vector, EXVector_Insert_Move, void (*)(XVector*, int64_t, void*))(this_vector, index, pvValue);
}

bool XVector_insert_array_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array, bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_insert_array_move_base(XVector* this_vector, int64_t index, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Insert_Array, bool (*)(XVector*, int64_t, void*, size_t, XCDataCreatMethod))(this_vector, index, begin, n, XContainerDataMoveMethod(this_vector));
}

bool XVector_append_array_base(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_append_Array, bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, XContainerDataCopyMethod(this_vector));
}

bool XVector_append_array_move_base(XVector* this_vector, const void* begin, size_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(begin, "") || ISNULL(n, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_append_Array, bool (*)(XVector*, void*, size_t, XCDataCreatMethod))(this_vector, begin, n, XContainerDataMoveMethod(this_vector));
}

void XVector_pop_front_base(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	typedef void (*funcPtr)(XVector*);
	XClassGetVirtualFunc(this_vector, EXVector_Pop_Front, funcPtr)(this_vector);
}

void XVector_pop_back_base(XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	typedef void (*funcPtr)(XVector*);
	XClassGetVirtualFunc(this_vector, EXVector_Pop_Back, funcPtr)(this_vector);
}

void XVector_erase_base(XVector* this_vector, const XVector_iterator* it, XVector_iterator* next)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	XClassGetVirtualFunc(this_vector, EXVector_Erase, void(*)(XVector*, const XVector_iterator*, XVector_iterator*))(this_vector, it, next);
}

void XVector_remove_base(XVector* this_vector, int64_t index, int64_t n)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	typedef void (*funcPtr)(XVector*, int64_t, int64_t);
	XClassGetVirtualFunc(this_vector, EXVector_Remove, funcPtr)(this_vector, index, n);
}

//void XVector_clear_base(XVector* this_vector)
//{
//	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
//		return ;
//	typedef void (*funcPtr)(XVector*);
//	XClassGetVirtualFunc(this_vector, EXVector_Clear, funcPtr)(this_vector);
//}

void XVector_rcopy_base(XVector* this_One, const XVector* this_Two)
{
	if (ISNULL(this_One, "") || ISNULL(this_Two, ""))
		return;
	typedef void(*funcPtr)(XVector*, XVector*);
	XClassGetVirtualFunc(this_One, EXVector_Rcopy, funcPtr)(this_One, this_Two);
}

void* XVector_at_base(const XVector* this_vector, int64_t index)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	return XClassGetVirtualFunc(this_vector, EXVector_At, void* (*)(XVector*, int64_t))(this_vector, index);
}

void* XVector_front_base(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	return XClassGetVirtualFunc(this_vector, EXVector_Front, void* (*)(XVector*))(this_vector);
}

void* XVector_back_base(const XVector* this_vector)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return NULL;
	return XClassGetVirtualFunc(this_vector, EXVector_Back, void* (*)(XVector*))(this_vector);
}

bool XVector_find_base(const XVector* this_vector, const void* findVal, XVector_iterator* it)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), "") || ISNULL(findVal, ""))
		return false;
	return XClassGetVirtualFunc(this_vector, EXVector_Find, bool(*)(XVector*, const void*, XVector_iterator*))(this_vector, findVal, it);
}

bool XVector_contains(const XVector* this_vector, const void* value)
{
	return XVector_find_base(this_vector, value, NULL);
}

int64_t XVector_indexOf(const XVector* this_vector, const void* value, int64_t from)
{
	// 参数合法性检查
	if (ISNULL(this_vector, "XVector is NULL") ||
		ISNULL(value, "Value is NULL") ||
		from < 0 ||
		from >= (int64_t)XVector_size_base(this_vector))
	{
		return -1;
	}

	size_t typeSize = XContainerTypeSize(this_vector);
	size_t size = XVector_size_base(this_vector);
	const char* data = (const char*)XContainerDataPtr(this_vector);

	// 使用内存比较查找元素
	for (size_t i = from; i < size; ++i)
	{
		const void* element = &data[i * typeSize];
		if (XContainerCompare(this_vector))
		{
			if (XContainerCompare(this_vector)(element, value) == XCompare_Equality)
				return (int64_t)i;
		}
		else if (memcmp(element, value, typeSize) == 0)
		{
			return (int64_t)i;
		}
	}

	// 未找到元素
	return -1;
}

int64_t XVector_lastIndexOf(const XVector* this_vector, const void* value, int64_t from)
{
	// 参数合法性检查
	if (ISNULL(this_vector, "XVector is NULL") ||
		ISNULL(value, "Value is NULL"))
	{
		return -1;
	}

	size_t typeSize = XContainerTypeSize(this_vector);
	size_t size = XVector_size_base(this_vector);
	if (size == 0) return -1; // 空容器直接返回

	// 处理from参数（默认为-1表示从最后一个元素开始）
	int64_t startIndex;
	if (from < 0) {
		startIndex = (int64_t)size - 1; // 从最后一个元素开始
	}
	else {
		// 确保起始位置不超过容器范围
		startIndex = (from >= (int64_t)size) ? (int64_t)size - 1 : from;
	}

	const char* data = (const char*)XContainerDataPtr(this_vector);

	// 从起始位置开始向前查找
	for (int64_t i = startIndex; i >= 0; --i)
	{
		const void* element = &data[(size_t)i * typeSize];
		if (XContainerCompare(this_vector))
		{
			if (XContainerCompare(this_vector)(element, value) == XCompare_Equality)
				return (int64_t)i;
		}
		else if (memcmp(element, value, typeSize) == 0)
		{
			return (int64_t)i;
		}
	}

	// 未找到元素
	return -1;
}

XVector* XVector_last(const XVector* this_vector, int64_t n)
{
	return XVector_mid(this_vector, XContainerSize(this_vector) - n, -1);
}

XVector* XVector_mid(const XVector* this_vector, int64_t pos, int64_t length)
{
	// 参数合法性检查
	if (ISNULL(this_vector, "XVector is NULL"))
		return NULL;

	size_t typeSize = XContainerTypeSize(this_vector);
	size_t totalSize = XVector_size_base(this_vector);
	// 创建新向量
	XVector* result = XVector_create(typeSize);
	XContainerSetDataCopyMethod(result, XContainerDataCopyMethod(this_vector));
	XContainerSetDataMoveMethod(result, XContainerDataMoveMethod(this_vector));
	XContainerSetDataDeinitMethod(result, XContainerDataDeinitMethod(this_vector));
	if (ISNULL(result, "Failed to create XVector"))
		return NULL;

	// 处理起始位置越界情况
	if (pos < 0 || (size_t)pos >= totalSize) {
		return result; // 返回空向量
	}

	// 计算实际要获取的元素数量
	size_t remaining = totalSize - (size_t)pos;
	size_t actualLength;

	if (length < 0) {
		actualLength = remaining; // length为-1时取剩余所有元素
	}
	else if ((size_t)length >= remaining) {
		actualLength = remaining; // 超出剩余数量时取剩余所有元素
	}
	else {
		actualLength = (size_t)length; // 正常情况取指定长度
	}

	// 处理长度为0的情况
	if (actualLength == 0) {
		return result; // 返回空向量
	}

	// 获取源数据指针
	const char* srcData = (const char*)XContainerDataPtr(this_vector);

	// 使用push_back逐个添加子元素
	for (size_t i = 0; i < actualLength; ++i) {
		// 计算当前元素在源向量中的位置
		const void* element = srcData + (pos + i) * typeSize;

		// 使用push_back API添加元素
		if (!XVector_push_back_base(result, element)) {
			XVector_delete_base(result);
			return NULL;
		}
	}

	return result;
}

XVector* XVector_first(const XVector* this_vector, int64_t n)
{
	return XVector_mid(this_vector, 0, n);
}

void XVector_sort_base(XVector* this_vector, XSortOrder order)
{
	if (ISNULL(this_vector, "") || ISNULL(XClassGetVtable(this_vector), ""))
		return;
	XClassGetVirtualFunc(this_vector, EXVector_Sort, void (*)(XVector*, XSortOrder))(this_vector, order);
}
bool XVector_replace(XVector* this_vector, int64_t index, void* pvValue)
{
	if (this_vector == NULL || index < -1 || index >= XVector_count_base(this_vector) || pvValue == NULL)
		return false;
	if (!VXVectorDetachIfNeeded(this_vector))
		return false;
	void* oldValue = XVector_at_base(this_vector, index);
	if (oldValue == NULL)
		return false;
	if (XContainerDataDeinitMethod(this_vector))
		XContainerDataDeinitMethod(this_vector)(oldValue);
	if (XContainerDataCopyMethod(this_vector))
		XContainerDataCopyMethod(this_vector)(oldValue, pvValue);
	else
		memcpy(oldValue, pvValue, XContainerTypeSize(this_vector));

}
bool XVector_replace_move(XVector* this_vector, int64_t index, void* pvValue)
{
	if (this_vector == NULL || index < -1 || index >= XVector_count_base(this_vector) || pvValue == NULL)
		return false;
	if (!VXVectorDetachIfNeeded(this_vector))
		return false;
	void* oldValue = XVector_at_base(this_vector, index);
	if (oldValue == NULL)
		return false;
	if (XContainerDataDeinitMethod(this_vector))
		XContainerDataDeinitMethod(this_vector)(oldValue);
	if (XContainerDataMoveMethod(this_vector))
		XContainerDataMoveMethod(this_vector)(oldValue, pvValue);
	else
		memcpy(oldValue, pvValue, XContainerTypeSize(this_vector));
}
// 内部核心函数：格式化文本并追加到向量
bool XVector_format_text_core(XVector* vector, bool appendNull, const char* format, va_list args)
{
	if (vector == NULL || format == NULL)
		return false;

	// 复制 va_list 以便后续重用
	va_list args_copy;
	va_copy(args_copy, args);

	// 计算所需缓冲区大小
	int len = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	if (len <= 0) return false;

	// 调整向量大小
	const size_t newSize = len + 1;
	if (!XVector_resize_base(vector, newSize))
		return false;

	// 格式化文本到向量（重用 va_list）
	va_copy(args_copy, args);
	vsnprintf((char*)XContainerDataPtr(vector), len + 1, format, args_copy);
	va_end(args_copy);

	// 如果不保留\0，移除末尾字符
	if (!appendNull && newSize > 0) {
		XVector_pop_back_base(vector);
	}

	return true;
}
bool XVector_append_text_fmt(XVector* this_vector, bool appendNull, const char* format, ...)
{
	if (this_vector == NULL || format == NULL)
		return false;
	va_list args;
	va_start(args, format);
	bool result = XVector_format_text_core(this_vector, appendNull, format, args);
	va_end(args);

	return result;
}

XVector* XVector_create_text_fmt(bool appendNull, const char* format, ...)
{
	XVector* data = XVector_Create(uint8_t);
	if (data == NULL)
		return NULL;
	va_list args;
	va_start(args, format);
	bool result = XVector_format_text_core(data, appendNull, format, args);
	va_end(args);

	// 如果失败，释放内存并返回NULL
	if (!result) {
		XVector_delete_base(data);
		return NULL;
	}

	return data;
}

#endif