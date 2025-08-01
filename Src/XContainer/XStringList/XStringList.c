#include"XStringList.h"
#if XStringList_ON
#include<string.h>
#include"XString.h"
XVtable* XStringList_class_init()
{
	return XVector_class_init();
//	XVTABLE_CREAT_DEFAULT
//		//虚函数表初始化
//#if VTABLE_ISSTACK
//		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XStringList))
//#else
//		XVTABLE_HEAP_INIT_DEFAULT
//#endif
//		//继承类
//		XVTABLE_INHERIT_DEFAULT(XVector_class_init());
//	//void* table[] = { };
//	//追加虚函数
//	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
//
//	//重写的函数
//#if SHOWCONTAINERSIZE
//	printf("XStringList size:%d\n", XVtable_size(XVTABLE_DEFAULT));
//#endif // SHOWCONTAINERSIZE
//	return XVTABLE_DEFAULT;
}
XStringList* XStringList_create()
{
	XStringList* vector=XMemory_malloc(sizeof(XStringList));
	XStringList_init(vector);
	
	return vector;
}
void XStringList_init(XStringList* this_stringVector)
{
	if (this_stringVector == NULL)
		return;
	XVector_init(this_stringVector,sizeof(XString));
	XClassGetVtable(this_stringVector) = XStringList_class_init();
	XContainerSetDataCopyMethod(this_stringVector, XClass_copy_base);
	XContainerSetDataMoveMethod(this_stringVector, XClass_move_base);
	XContainerSetDataDeinitMethod(this_stringVector, XClass_deinit_base);
}
void XStringList_push_front_c_str(XStringList* this_stringVector, const char* str)
{
	XString_Init(string,str);
	XStringList_push_front_move_base(this_stringVector, string);
}
void XStringList_push_back_c_str(XStringList* this_stringVector, const char* str)
{
	XString_Init(string, str);
	XStringList_push_back_move_base(this_stringVector, string);
}
void XStringList_insert_c_str(XStringList* this_stringVector, int64_t index, const char* str)
{
	XString_Init(string, str);
	XStringList_insert_move_base(this_stringVector,index, string);
}

XString* XStringList_join(const XStringList* this_stringVector, const char* separator)
{
	if(this_stringVector==NULL|| separator==NULL)
		return NULL;
	size_t len = strlen(separator);
	if (len == 0 || XString_isEmpty_base(this_stringVector))
		return NULL;
	XString* str = XString_create_utf8(NULL);
	for_each_iterator(this_stringVector, XStringList, it)
	{
		XString* s = XStringList_iterator_data(&it);
		if (!XString_isEmpty_base(s))
		{
			XString_append_utf8(str, XString_c_str(s));
			XString_append_utf8(str, separator);
		}
	}
	XContainerSize(str) -= len;
	((char*)XContainerDataPtr(str))[XContainerSize(str)]=0;
	return str;
}

#endif

