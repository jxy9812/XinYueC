#ifndef XCLASS_H
#define XCLASS_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVtable.h"
#include"XDataStructConfig.h"
typedef struct  XVtable;
#define XCLASS_VTABLE_SIZE   1      //虚函数表大小
//XClass虚函数表枚举
enum XClassVtableEnum
{
	EXClass_Free,
};
//容器基类
typedef struct XClass
{
	XVtable* m_vtable;//虚函数表
}XClass;
#define XVtableGetFunc(Vtable,Offset,Type) ((Type)((((XVtable*)Vtable)->data)[Offset]))//用虚函数表获取函数
#define XClassGetVtable(Object) ((XClass*)Object)->m_vtable  //用获取类中的虚函数表
//#define XClassGetVirtualFunc(Object,Offset,Type) ((Type)((((XClass*)Object)->m_vtable->data)[Offset]))
#define XClassGetVirtualFunc(Object,Offset,Type)      XVtableGetFunc((XClassGetVtable(Object)),Offset,Type)//用XClassObject及其子类获取虚函数
#define isNULLInfo(args,str) args,#args,str ,__FUNCTION__,__FILE__,__LINE__
#define ISNULL(args,str)(ArgIsNULL(isNULLInfo(args,str)))
bool ArgIsNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/);


//定义虚函数表
#define XVTABLE_CREAT(Vtable)  \
	 static XVtable* Vtable = NULL;\
	 if (Vtable)return Vtable;
//虚函数表在堆上初始化
#define XVTABLE_HEAP_INIT(Vtable)\
	Vtable = XVtable_create();
//虚函数表在栈上初始化
#define XVTABLE_STACK_INIT(Vtable,Size)\
{\
	static XVtable vtable;\
	static void* vtable_data[Size];\
	Vtable = &vtable;\
	XVtable_init_stack(Vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));\
}
//虚函数表继承
#define XVTABLE_INHERIT(Vtable,VtableBase)			XVtable_append_vtable(Vtable,VtableBase)
//虚函数表函数重载
#define XVTABLE_OVERLOAD(Vtable,Type,Func)			XVtable_At(Vtable,Type)=Func
//虚函数表追加函数列表
#define XVTABLE_ADD_FUNC_LIST(Vtable,table)\
{ \
	XVtable_append_array(Vtable, table, sizeof(table) / sizeof(table[0]));\
 }
/*								  以下是重新封装的默认参数						*/			
//默认虚函数表
#define XVTABLE_DEFAULT								XClassVtable
//定义虚函数表 默认
#define XVTABLE_CREAT_DEFAULT						XVTABLE_CREAT(XVTABLE_DEFAULT)
//堆上初始化表   默认
#define XVTABLE_HEAP_INIT_DEFAULT					XVTABLE_HEAP_INIT(XVTABLE_DEFAULT)
//栈上初始化表
#define XVTABLE_STACK_INIT_DEFAULT(Size)			XVTABLE_STACK_INIT(XVTABLE_DEFAULT,Size)
//继承表
#define XVTABLE_INHERIT_DEFAULT(VtableBase)			XVTABLE_INHERIT(XVTABLE_DEFAULT,VtableBase)
//虚函数表函数重载
#define XVTABLE_OVERLOAD_DEFAULT(Type,Func)			XVTABLE_OVERLOAD(XVTABLE_DEFAULT,Type,Func)
//虚函数表追加函数列表
#define XVTABLE_ADD_FUNC_LIST_DEFAULT(table)	XVTABLE_ADD_FUNC_LIST(XVTABLE_DEFAULT,table)
/*								  类的创建主要用这些						*/
XVtable* XClass_class_init();
void XClass_init(XClass* Object);
void XClass_free_base(XClass* Object);
#ifdef __cplusplus
}
#endif
#endif // !XVirtual_H
