#include"XContainerObject_virtual.h"
#include"XContainerObject.h"
#include"XAlgorithm.h"
//虚函数表定义
void* XContainerObjectVtable[] = { XVContainerObject_empty,XVContainerObject_size,XVContainerObject_capacity,XVContainerObject_type,XVContainerObject_swap,XVContainerObject_free };

bool XVContainerObject_empty(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return true;
	return Object->_size == 0;
}


size_t XVContainerObject_size(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->_size;
}

size_t XVContainerObject_capacity(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->_capacity;
}
size_t XVContainerObject_type(const XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	return Object->_type;
}

void XVContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo)
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

void XVContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, ""))
		return 0;
	Object->vtable = NULL;
	Object->_data = NULL;
	Object->_capacity = 0;
	Object->_size = 0;
	Object->_type = 0;
}