#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
#include"XString.h"
#include"XPrintf.h"
#include<string.h>
bool ArgIsNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/)
{
	if (args == NULL)
	{
		XERROR_PRINTF("%s\n参数:%s是NULL\t函数名:%s\n文件路径:%s\n正在编译文件的行号:%d\n", str, argsName, funcName, filePath, line);
		return true;
	}
	return false;
}

void XClass_deinit_base(XClass* object)
{
	if (ISNULL(object, "") || ISNULL(XClassGetVtable(object), ""))
		return;
	XClassGetVirtualFunc(object, EXClass_Deinit, void(*)(XClass*))(object);
}
void XClass_copy_base(XClass* object, const XClass* src)
{
	if (ISNULL(src, "") || ISNULL(XClassGetVtable(src), ""))
		return;
	XClassGetVirtualFunc(src, EXClass_Copy, void(*)(XClass*,const XClass*))(object,src);
}
void XClass_move_base(XClass* object, XClass* src)
{
	if (ISNULL(src, "") || ISNULL(XClassGetVtable(src), ""))
		return;
	XClassGetVirtualFunc(src, EXClass_Move, void(*)(XClass*, XClass*))(object,src);
}
void XClass_delete_base(XClass* object)
{
	if (!object)return;
	bool is_heap = Class_IsHeap(object);
	XMemory* memory = Class_Memory(object);
	if (!memory)
		memory = XMemory_method(XCLASS_DEFAULT_MEMORY_TYPE);
	XClass_deinit_base(object);
	if (!is_heap)
		return;
	if (memory && memory->free)
		memory->free(object);
	else
		XFree_System(object);
}
