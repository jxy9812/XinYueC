#include"XContainerObject.h"
#include"XAlgorithm.h"
#include"XVtable.h"
#include<stdlib.h>
//声明 
static void VXContainerObject_free(XContainerObject* Object);
static bool VXContainerObject_empty(const XContainerObject* Object);
static size_t VXContainerObject_size(const XContainerObject* Object);
static size_t VXContainerObject_capacity(const  XContainerObject* Object);
static size_t VXContainerObject_type(const XContainerObject* Object);
static void VXContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
static void VXContainerObject_clear(XContainerObject* Object);
XVtable* XContainerObjectVtable = NULL;

void XContainerObject_class_init()
{
	//虚函数表初始化
	XContainerObjectVtable = XVtable_new();
	void* vtable[] = { VXContainerObject_free, VXContainerObject_empty,VXContainerObject_size,VXContainerObject_capacity,VXContainerObject_type,VXContainerObject_swap,VXContainerObject_clear };
	XVtable_append_array(XContainerObjectVtable,vtable,sizeof(vtable)/sizeof(vtable[0]));
}
//虚函数表定义
//void* XContainerObjectVtable[] = { VXContainerObject_free, VXContainerObject_empty,VXContainerObject_size,VXContainerObject_capacity,VXContainerObject_type,VXContainerObject_swap,VXContainerObject_clear };

bool VXContainerObject_empty(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return true;
	return Object->_size == 0;
}


size_t VXContainerObject_size(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->_size;
}

size_t VXContainerObject_capacity(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->_capacity;
}
size_t VXContainerObject_type(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->_typeSize;
}

void VXContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo)
{
	bool one = ISNULL(ObjectOne, "");
	bool two = ISNULL(ObjectTwo, "");
	if (!(one || two))
	{
		XSwap(&ObjectOne->_data, &ObjectTwo->_data, sizeof(void*));
		XSwap(&ObjectOne->_capacity, &ObjectTwo->_capacity, sizeof(size_t));
		XSwap(&ObjectOne->_size, &ObjectTwo->_size, sizeof(size_t));
	}
}

void VXContainerObject_clear(XContainerObject* Object)
{
	Object->_size = 0;
}

void VXContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	ObjectVtable(Object) = NULL;
	Object->_data = NULL;
	Object->_capacity = 0;
	Object->_size = 0;
	Object->_typeSize = 0;
	if (Object->_data);
		free(Object->_data);
	free(Object);
}