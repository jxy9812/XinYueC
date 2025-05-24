#include"XStringVector.h"
#if XStringVector_ON&&0
//虚函数表定义
XVtable* XStringVectorVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[25];//虚函数数据
#endif
//void VXStringVector_push_front(XStringVector* this_stringVector, XString* string);
//void VXStringVector_push_back(XStringVector* this_stringVector, XString* string);
//void VXStringVector_insert(XStringVector* this_stringVector, int64_t index, XString* string);
void XStringVector_class_init()
{
	if (XStringVectorVtable)
		return;
	void* table[] = {
	};
#if !VTABLE_ISSTACK
	XStringVectorVtable = XVtable_new();
#else
	XStringVectorVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XStringVectorVtable, XVectorVtable);
	//追加函数
	//XVtable_append_array(XStringVectorVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	/*XVtable_At(XStringVectorVtable, EXVector_Push_Front) = VXStringVector_push_front;
	XVtable_At(XStringVectorVtable, EXVector_Push_Back) = VXStringVector_push_back;
	XVtable_At(XStringVectorVtable, EXVector_Insert) = VXStringVector_insert;*/
#if SHOWCONTAINERSIZE
	printf("XStringVector size:%d\n", XVtable_size(XStringVectorVtable));
#endif // SHOWCONTAINERSIZE
}

//void VXStringVector_push_front(XStringVector* this_stringVector, XString* string)
//{
//	typedef void (*funcPtr)(XVector*, void*);
//	XVtableGetFunc(XVectorVtable, EXVector_Push_Front, funcPtr)(this_stringVector,&string);
//}
//
//void VXStringVector_push_back(XStringVector* this_stringVector, XString* string)
//{
//	typedef void (*funcPtr)(XVector*, void*);
//	XVtableGetFunc(XVectorVtable, EXVector_Push_Back, funcPtr)(this_stringVector, &string);
//}
//
//void VXStringVector_insert(XStringVector* this_stringVector, int64_t index, XString* string)
//{
//	typedef void (*funcPtr)(XVector*, int64_t, void*);
//	XVtableGetFunc(XVectorVtable, EXVector_Insert, funcPtr)(this_stringVector,index, &string);
//}













#endif

