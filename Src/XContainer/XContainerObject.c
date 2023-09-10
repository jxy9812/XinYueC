#include "XContainerObject.h"
#include"XAlgorithm.h"
 bool isNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/)
{
	if (args == NULL)
	{
		printf("%s\n参数:%s是NULL\t函数名:%s\n文件路径:%s\n正在编译文件的行号:%d\n", str, argsName,funcName, filePath, line);
		return true;
	}
	return false;
}
const bool XContainerObject_empty(const struct XContainerObject* Object)
{
	if (isNULL(isNULLInfo(Object, "")))
		return true;
	return Object->_size == 0;
}

const size_t XContainerObject_size(const struct XContainerObject* Object)
{
	
	if (isNULL(isNULLInfo(Object,"")))
		return 0;
	return Object->_size;
}

const size_t XContainerObject_capacity(const struct XContainerObject* Object)
{
	if (isNULL(isNULLInfo(Object, "")))
		return 0;
	return Object->_capacity;
}

const size_t XContainerObject_type(const XContainerObject* Object)
{
	if (isNULL(isNULLInfo(Object, "")))
		return 0;
	return Object->_type;
}

void XContainerObject_swap(struct XContainerObject* ObjectOne, struct XContainerObject* ObjectTwo)
{
	bool one = isNULL(isNULLInfo(ObjectOne, ""));
	bool two = isNULL(isNULLInfo(ObjectTwo, ""));
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
