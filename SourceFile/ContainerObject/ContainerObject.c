#include "ContainerObject.h"
#include"algorithm.h"
static bool isObjectNULL(const struct ContainerObject* Object,const char* str)
{
	if (Object == NULL)
	{
		perror("%s成员函数调用的对象为NULL",str);
		return true;
	}
	return false;
}
const bool ContainerObject_empty(const struct ContainerObject* Object)
{
	if(!isObjectNULL(Object, "empty"))
		return Object->_size==0;
	return true;
}

const size_t ContainerObject_size(const struct ContainerObject* Object)
{
	if (!isObjectNULL(Object, "size"))
		return Object->_size;
	return 0;
}

const size_t ContainerObject_capacity(const struct ContainerObject* Object)
{
	if (!isObjectNULL(Object, "capacity"))
		return Object->_currentSize;
	return 0;
}

void ContainerObject_swap(struct ContainerObject* ObjectOne, struct ContainerObject* ObjectTwo)
{
	bool one=isObjectNULL(ObjectOne, "swap");
	bool two=isObjectNULL(ObjectTwo, "swap");
	if (!(one || two))
	{
		swap(&ObjectOne->_data, &ObjectTwo->_data, sizeof(void*));
		swap(&ObjectOne->_currentSize, &ObjectTwo->_currentSize, sizeof(size_t));
		swap(&ObjectOne->_size, &ObjectTwo->_size, sizeof(size_t));
	}
}
