#include"XClass.h"
#include"XVtable.h"
#include"XMemory.h"
bool ArgIsNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/)
{
	if (args == NULL)
	{
		printf("%s\n参数:%s是NULL\t函数名:%s\n文件路径:%s\n正在编译文件的行号:%d\n", str, argsName, funcName, filePath, line);
		return true;
	}
	return false;
}

void XClass_deinit_base(XClass* Object)
{
	if (ISNULL(Object, "") || ISNULL(XClassGetVtable(Object), ""))
		return;
	XClassGetVirtualFunc(Object, EXClass_Deinit, void(*)(XClass*))(Object);
}
void XClass_delete_base(XClass* Object)
{
	XClass_deinit_base(Object);
	XMemory_free(Object);
}