#ifndef XCLASS_H
#define XCLASS_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVtable.h"
#include"CXinYueConfig.h"
#include"XPrintf.h"
#include<stdlib.h>
#define XCLASS_VTABLE_SIZE   XCLASS_VTABLE_GET_SIZE(XClass)      //虚函数表大小
//XClass虚函数表枚举
XCLASS_DEFINE_BEGING(XClass)
XCLASS_DEFINE_ENUM(XClass, Copy),
XCLASS_DEFINE_ENUM(XClass, Move),
XCLASS_DEFINE_ENUM(XClass, Deinit),
XCLASS_DEFINE_END(XClass)
//容器基类
typedef struct XClass
{
	XVtable* m_vtable;//虚函数表
	FreeMethod m_free;//释放方法
}XClass;
#define XVtableGetFunc(Vtable,Offset,Type) ((Type)((((XVtable*)Vtable)->data)[Offset]))//用虚函数表获取函数
#define XClassGetVtable(Object) ((XClass*)Object)->m_vtable  //用获取类中的虚函数表
#define XClassSetVtable(Obj,Type) (XClassGetVtable(Obj)=Type##_class_init())
#define XClassIsVtableNull(Obj) (XClassGetVtable(Obj) == NULL)  // 检查虚函数表是否为空
#define XClassEnsureVtable(Obj,Type) do { if (XClassIsVtableNull(Obj)) XClassSetVtable(Obj, Type); } while(0)  // 确保虚函数表已初始化
//#define XClassGetVirtualFunc(Object,Offset,Type) ((Type)((((XClass*)Object)->m_vtable->data)[Offset]))
#define XClassGetVirtualFunc(Object,Offset,Type)      XVtableGetFunc((XClassGetVtable(Object)),Offset,Type)//用XClassObject及其子类获取虚函数
#define isNULLInfo(args,str) args,#args,str ,__FUNCTION__,__FILE__,__LINE__
#define ISNULL(args,str)(ArgIsNULL(isNULLInfo(args,str)))
bool ArgIsNULL(const void* args/*参数数值*/, const char* argsName/*参数名字*/, const char* str/*附加参数*/, const char* funcName/*函数名字*/, const char* filePath/*所在文件路径*/, int line/*所在行号*/);
#define XAssert(args,str) {if(ISNULL(args,str))exit(-1);}

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
#define XVTABLE_OVERLOAD(Vtable,Type,Func)\
{\
		if (Type >= Vtable->capacity)\
		{\
			XERROR_PRINTF("文件:%s 函数:%s 行号:%d 重载索引超出范围了 索引:%d 容量:%d个\n", __FILE__, __func__, __LINE__, Type, Vtable->capacity); \
			exit(-1); \
		}\
			XVtable_At(Vtable, Type) = Func; \
}
//虚函数表追加函数列表
#define XVTABLE_ADD_FUNC_LIST(Vtable,table)\
{ \
	if(Vtable->isStack&&((Vtable->size)+(sizeof(table) / sizeof(table[0])))>Vtable->capacity)\
	{\
		XERROR_PRINTF("文件:%s 函数:%s 行号:%d 追加的函数超出最大容量了,超出:%d个\n",__FILE__,__func__,__LINE__,(Vtable->size)+(sizeof(table) / sizeof(table[0]))-Vtable->capacity);\
		exit(-1);\
	}\
	XVtable_append_array(Vtable, table, sizeof(table) / sizeof(table[0]));\
 }
/*								  以下是重新封装的默认参数						*/			
//默认虚函数表
#define XVTABLE_DEFAULT								XClassVtable
//定义虚函数表 默认
#define XVTABLE_CREAT_DEFAULT						XVTABLE_CREAT(XVTABLE_DEFAULT)
//堆上初始化表   默认
#define XVTABLE_HEAP_INIT_DEFAULT					XVTABLE_HEAP_INIT(XVTABLE_DEFAULT)
//栈上初始化表，按类名自动获取虚函数表大小
#define XVTABLE_STACK_INIT_DEFAULT(Type)			XVTABLE_STACK_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(Type))
//栈上初始化表，保留对额外容量或自定义大小表达式的支持
#define XVTABLE_STACK_INIT_DEFAULT_SIZE(Size)		XVTABLE_STACK_INIT(XVTABLE_DEFAULT,Size)
//继承表
#define XVTABLE_INHERIT_DEFAULT(VtableBase)			XVTABLE_INHERIT(XVTABLE_DEFAULT,VtableBase)
//继承类
#define XVTABLE_INHERIT_XCLASS(Type)				XVTABLE_INHERIT(XVTABLE_DEFAULT,Type##_class_init())
//虚函数表函数重载
#define XVTABLE_OVERLOAD_DEFAULT(Type,Func)			XVTABLE_OVERLOAD(XVTABLE_DEFAULT,Type,Func)
//虚函数表追加函数列表
#define XVTABLE_ADD_FUNC_LIST_DEFAULT(table)	XVTABLE_ADD_FUNC_LIST(XVTABLE_DEFAULT,table)
/*								  类的创建主要用这些						*/
XVtable* XClass_class_init();
void XClass_init(XClass* object);
void XClass_copy_base(XClass* object, const XClass* src);
void XClass_move_base(XClass* object, XClass* src);
void XClass_deinit_base(XClass* object);
void XClass_delete_base(XClass* object);

//标记保护权限
#define Protected 

//类是否在堆上
#define Class_MemoryFree(obj)   (((XClass*)obj)->m_free)
//设置标志类在堆上
#define Set_Class_MemoryFree(obj,method)  (((XClass*)obj)->m_free=method)

// 释放父对象
#define XClass_Parent(objType,funcEnum,funcType)  (XVtableGetFunc(objType##_class_init(), funcEnum, funcType))
#define XClass_Deinit_Parent(Type,obj)			(XClass_Parent(Type,EXClass_Deinit,void(*)(Type*))(obj))

#ifdef __cplusplus
}
#endif
#endif // !XVirtual_H
