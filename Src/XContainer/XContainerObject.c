#include"XContainerObject.h"

bool isNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/)
{
	if (args == NULL)
	{
		printf("%s\n参数:%s是NULL\t函数名:%s\n文件路径:%s\n正在编译文件的行号:%d\n", str, argsName,funcName, filePath, line);
		return true;
	}
	return false;
}

bool XContainerObject_empty(const XContainerObject* Object)
{
	if (ISNULL(Object, "")|| ISNULL(Object->vtable, ""))
		return true;
	typedef bool (*funcPtr)(const XContainerObject* );
	//void* p = ObjectVirtualFunc(Object, XContainerObject_Empty, funcPtr);
	return ObjectVirtualFunc(Object, XContainerObject_Empty,funcPtr)(Object);
}

size_t XContainerObject_size(const struct XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, XContainerObject_Size, funcPtr)(Object);
}

size_t XContainerObject_capacity(const struct XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, XContainerObject_Capacity, funcPtr)(Object);
}
size_t XContainerObject_typeSize(const XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, XContainerObject_TypeSize, funcPtr)(Object);
}

void XContainerObject_swap( XContainerObject* ObjectOne,  XContainerObject* ObjectTwo)
{
	bool one = ISNULL(ObjectOne, "");
	bool two = ISNULL(ObjectTwo, "");
	typedef void(*funcPtr)(XContainerObject*, XContainerObject*);
	return ObjectVirtualFunc(ObjectOne, XContainerObject_Swap, funcPtr)(ObjectOne, ObjectTwo);
}

void XContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef void(*funcPtr)(XContainerObject*);
	return ObjectVirtualFunc(Object, XContainerObject_Free, funcPtr)(Object);
}


void XContainerObject_init(XContainerObject* Object,size_t type)
{
	Object->vtable = XContainerObjectVtable;
	Object->_data = NULL;
	Object->_capacity = 0;
	Object->_size = 0;
	Object->_typeSize = type;
}

