#include"XClass.h"
#include"XVtable.h"
#include"XContainerObject.h"
#include"XVector.h"
#include"XList.h"
XClass_init()
{
	XContainerObject_class_init();
	XVector_class_init();
	XList_class_init();
	//测试
	/*void* array[] = {1,2,3,4};
	XVtable* vtable= XVtable_new();
	XVtable_insert_array(vtable,0,array,4);
	XVtable_insert_array(vtable, 0, array, 4);
	for (size_t i = 0; i < XVtable_size(vtable); i++)
	{
		printf("%d\t", XVtable_at(vtable, i));
	}
	printf("\n");*/
}

bool isNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/)
{
	if (args == NULL)
	{
		printf("%s\n参数:%s是NULL\t函数名:%s\n文件路径:%s\n正在编译文件的行号:%d\n", str, argsName, funcName, filePath, line);
		return true;
	}
	return false;
}