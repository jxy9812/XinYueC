#include"XStringList.h"
#if XStringList_ON
static void VXStringList_deinit(XStringList* this_stringVector);
static void VXStringList_push_front(XStringList* this_stringVector, XString* string);
static void VXStringList_push_back(XStringList* this_stringVector, XString* string);
static void VXStringList_insert(XStringList* this_stringVector, int64_t index, XString* string);
// 返回元素字符串
static XString* VXStringList_at(const XStringList* this_stringVector, int64_t index);
static XString* VXStringList_front(const XStringList* this_stringVector);
static XString* VXStringList_back(const XStringList* this_stringVector);
XVtable* XStringList_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XStringList))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
	XVTABLE_INHERIT_DEFAULT(XVector_class_init());
	//void* table[] = { };
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	//重写的函数
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStringList_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Push_Front, VXStringList_push_front);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Push_Back, VXStringList_push_back);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Insert, VXStringList_insert);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_At, VXStringList_at);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Front, VXStringList_front);
	XVTABLE_OVERLOAD_DEFAULT(EXVector_Back, VXStringList_back);
#if SHOWCONTAINERSIZE
	printf("XStringList size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}










#endif

void VXStringList_deinit(XStringList* this_stringVector)
{
	//调用父类释放
	XVtableGetFunc(XVector_class_init(), EXClass_Deinit, void (*)(XVector*))(this_stringVector);
}

void VXStringList_push_front(XStringList* this_stringVector, XString* string)
{
	XVtableGetFunc(XVector_class_init(),EXVector_Push_Front,void(*)(XVector*,void*))(this_stringVector,&string);
}

void VXStringList_push_back(XStringList* this_stringVector, XString* string)
{
	XVtableGetFunc(XVector_class_init(), EXVector_Push_Back, void(*)(XVector*, void*))(this_stringVector, &string);
}

void VXStringList_insert(XStringList* this_stringVector, int64_t index, XString* string)
{
	XVtableGetFunc(XVector_class_init(), EXVector_Insert, void(*)(XVector*,int64_t, void*))(this_stringVector, index, &string);
}

XString* VXStringList_at(const XStringList* this_stringVector, int64_t index)
{
	void* pv = XVtableGetFunc(XVector_class_init(), EXVector_At, void* (*)(XVector*, int64_t))(this_stringVector, index);
	if(pv==NULL)
		return NULL;
	return *((XString**)pv);
}

XString* VXStringList_front(const XStringList* this_stringVector)
{
	void* pv = XVtableGetFunc(XVector_class_init(), EXVector_Front, void* (*)(XVector*))(this_stringVector);
	if (pv == NULL)
		return NULL;
	return *((XString**)pv);
}

XString* VXStringList_back(const XStringList* this_stringVector)
{
	void* pv = XVtableGetFunc(XVector_class_init(), EXVector_Back, void* (*)(XVector*))(this_stringVector);
	if (pv == NULL)
		return NULL;
	return *((XString**)pv);
}

