#include"XContainerObject_virtual.h"
#include"XContainerObject.h"
#include"XAlgorithm.h"
//虚函数表定义
void* XContainerObjectVtable[] = { VXContainerObject_empty,VXContainerObject_size,VXContainerObject_capacity,VXContainerObject_type,VXContainerObject_swap,VXContainerObject_free };

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
	return Object->_type;
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

void VXContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	Object->vtable = NULL;
	Object->_data = NULL;
	Object->_capacity = 0;
	Object->_size = 0;
	Object->_type = 0;
}