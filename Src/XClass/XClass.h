#ifndef XCLASS_H
#define XCLASS_H
#include"XVtable.h"

typedef struct XVtable XVtable;
//容器基类
typedef struct XClassObject
{
	XVtable* vtable;//虚函数表
}XClassObject;
#define VtableFunc(Vtable,Offset,Type) ((Type)(((void**)Vtable)[Offset]))//用虚函数表获取函数
#define ObjectVtable(Object) ((XClassObject*)Object)->vtable  //用获取类中的虚函数表
#define ObjectVirtualFunc(Object,Offset,Type) ((Type)((((XClassObject*)Object)->vtable->data)[Offset]))//用XContainerObject及其子类获取虚函数

#define isNULLInfo(args,str) args,#args,str ,__FUNCTION__,__FILE__,__LINE__
#define ISNULL(args,str)(isNULL(isNULLInfo(args,str)))
bool isNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/);
#endif // !XVirtual_H
