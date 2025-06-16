#include"XStringVector.h"
#if XStringVector_ON
static void VXStringVector_delete(XStringVector* this_stringVector);
static void VXStringVector_push_front(XStringVector* this_stringVector, XString* string);
static void VXStringVector_push_back(XStringVector* this_stringVector, XString* string);
static void VXStringVector_insert(XStringVector* this_stringVector, int64_t index, XString* string);
// 返回元素字符串
static XString* VXStringVector_at(const XStringVector* this_stringVector, int64_t index);
static XString* VXStringVector_front(const XStringVector* this_stringVector);
static XString* VXStringVector_back(const XStringVector* this_stringVector);
XVtable* XStringVector_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XStringVector))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
	XVTABLE_INHERIT_DEFAULT(XVector_class_init());
	//void* table[] = { };
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	//重写的函数
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXStringVector_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Push_Front, VXStringVector_push_front);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Push_Back, VXStringVector_push_back);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Insert, VXStringVector_insert);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_At, VXStringVector_at);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Front, VXStringVector_front);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Back, VXStringVector_back);
#if SHOWCONTAINERSIZE
	printf("XStringVector size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}










#endif

void VXStringVector_delete(XStringVector* this_stringVector)
{
	
	//调用父类释放
	XVtableGetFunc(XVector_class_init(), EXClass_Delete, void (*)(XVector*))(this_stringVector);
}

void VXStringVector_push_front(XStringVector* this_stringVector, XString* string)
{
	XVtableGetFunc(XVector_class_init(),EXVector_Push_Front,void(*)(XVector*,void*))(this_stringVector,&string);
}

void VXStringVector_push_back(XStringVector* this_stringVector, XString* string)
{
	XVtableGetFunc(XVector_class_init(), EXVector_Push_Back, void(*)(XVector*, void*))(this_stringVector, &string);
}

void VXStringVector_insert(XStringVector* this_stringVector, int64_t index, XString* string)
{
	XVtableGetFunc(XVector_class_init(), EXVector_Insert, void(*)(XVector*,int64_t, void*))(this_stringVector, index, &string);
}

XString* VXStringVector_at(const XStringVector* this_stringVector, int64_t index)
{
	void* pv = XVtableGetFunc(XVector_class_init(), EXVector_At, void* (*)(XVector*, int64_t))(this_stringVector, index);
	if(pv==NULL)
		return NULL;
	return *((XString**)pv);
}

XString* VXStringVector_front(const XStringVector* this_stringVector)
{
	void* pv = XVtableGetFunc(XVector_class_init(), EXVector_Front, void* (*)(XVector*))(this_stringVector);
	if (pv == NULL)
		return NULL;
	return *((XString**)pv);
}

XString* VXStringVector_back(const XStringVector* this_stringVector)
{
	void* pv = XVtableGetFunc(XVector_class_init(), EXVector_Back, void* (*)(XVector*))(this_stringVector);
	if (pv == NULL)
		return NULL;
	return *((XString**)pv);
}

