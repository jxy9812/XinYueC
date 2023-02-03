#include "XContainerObject.h"
#include"XAlgorithm.h"
 bool isObjectNULL(const struct XContainerObject* Object,const char* str)
{
	if (Object == NULL)
	{
		printf("%s函数调用的对象为NULL\n",str);
		return true;
	}
	return false;
}
const bool XContainerObject_empty(const struct XContainerObject* Object)
{
	if (!isObjectNULL(Object, "empty"))
	{
		return Object->_size == 0;
	}
	return true;
}

const size_t XContainerObject_size(const struct XContainerObject* Object)
{
	
	if (!isObjectNULL(Object, "size"))
	{
		return Object->_size;
	}
	return 0;
}

const size_t XContainerObject_capacity(const struct XContainerObject* Object)
{
	if (!isObjectNULL(Object, "capacity"))
	{
		return Object->_capacity;
	}
	return 0;
}

void XContainerObject_swap(struct XContainerObject* ObjectOne, struct XContainerObject* ObjectTwo)
{
	bool one=isObjectNULL(ObjectOne, "swap");
	bool two=isObjectNULL(ObjectTwo, "swap");
	if (!(one || two))
	{
		swap(&ObjectOne->_data, &ObjectTwo->_data, sizeof(void*));
		swap(&ObjectOne->_capacity, &ObjectTwo->_capacity, sizeof(size_t));
		swap(&ObjectOne->_size, &ObjectTwo->_size, sizeof(size_t));
	}
}

void XContainerObject_init( struct XContainerObject* Object,size_t type)
{
	Object->_data = NULL;
	Object->_capacity = 0;
	Object->_size = 0;
	Object->_type = type;
	Object->capacity = XContainerObject_capacity;
	Object->empty = XContainerObject_empty;
	Object->size = XContainerObject_size;
	Object->swap = XContainerObject_swap;

}
