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

void XContainerObject_init(XContainerObject* Object, size_t typeSize)
{
	if (ISNULL(Object, "") || ISNULL(typeSize, ""))
		return;
	Object->vtable = XContainerObjectVtable;
	Object->_data = NULL;
	Object->_capacity = 0;
	Object->_size = 0;
	Object->_typeSize = typeSize;
}

void XContainerObject_free(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	ObjectVirtualFunc(Object, EXContainerObject_Free, funcPtr)(Object);
}

bool XContainerObject_empty(const XContainerObject* Object)
{
	if (ISNULL(Object, "")|| ISNULL(Object->vtable, ""))
		return true;
	typedef bool (*funcPtr)(const XContainerObject* );
	//void* p = ObjectVirtualFunc(Object, XContainerObject_Empty, funcPtr);
	return ObjectVirtualFunc(Object, EXContainerObject_Empty,funcPtr)(Object);
}

size_t XContainerObject_size(const struct XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, EXContainerObject_Size, funcPtr)(Object);
}

size_t XContainerObject_capacity(const struct XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, EXContainerObject_Capacity, funcPtr)(Object);
}
size_t XContainerObject_typeSize(const XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return 0;
	typedef size_t(*funcPtr)(const XContainerObject*);
	return ObjectVirtualFunc(Object, EXContainerObject_TypeSize, funcPtr)(Object);
}

void XContainerObject_swap( XContainerObject* ObjectOne,  XContainerObject* ObjectTwo)
{
	if (ISNULL(ObjectOne, "") || ISNULL(ObjectTwo, ""))
		return;
	typedef void(*funcPtr)(XContainerObject*, XContainerObject*);
	ObjectVirtualFunc(ObjectOne, EXContainerObject_Swap, funcPtr)(ObjectOne, ObjectTwo);
}

void XContainerObject_clear(XContainerObject* Object)
{
	if (ISNULL(Object, "") || ISNULL(Object->vtable, ""))
		return ;
	typedef void(*funcPtr)(XContainerObject*);
	ObjectVirtualFunc(Object, EXContainerObject_Clear, funcPtr)(Object);
}





