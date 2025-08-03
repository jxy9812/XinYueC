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
void XStringList_init(XStringList* strList)
{
	if (strList == NULL)
		return;
	XVector_init(strList,sizeof(XString));
	XClassGetVtable(strList) = XStringList_class_init();
	XContainerSetDataCopyMethod(strList, XClass_copy_base);
	XContainerSetDataMoveMethod(strList, XClass_move_base);
	XContainerSetDataDeinitMethod(strList, XClass_deinit_base);
}
void XStringList_push_front_utf8(XStringList* strList, const char* utf8_str)
{
	XString_Init_Utf8(str,utf8_str);
	XStringList_push_front_move_base(strList, str);
	XString_deinit_base(str);
}
void XStringList_push_back_utf8(XStringList* strList, const char* utf8_str)
{
	XString_Init_Utf8(str, utf8_str);
	XStringList_push_back_move_base(strList, str);
	XString_deinit_base(str);
}
void XStringList_insert_utf8(XStringList* strList, int64_t index, const char* utf8_str)
{
	XString_Init_Utf8(str, utf8_str);
	XStringList_insert_move_base(strList,index, str);
	XString_deinit_base(str);
}

XString* XStringList_join(const XStringList* strList, const XString* separator)
{
	if (strList == NULL || separator == NULL)
		return NULL;
	if (XString_isEmpty_base(separator))
		return NULL;
	XString* str = XString_create(NULL);
	for_each_iterator(strList, XStringList, it)
	{
		XString* s = XStringList_iterator_data(&it);
		if (!XString_isEmpty_base(s))
		{
			XString_append(str, s);
			XString_append(str, separator);
		}
	}
	if (XString_ends_with(str, separator, XCharCaseInsensitive))
	{
		XString_remove_base(str, XString_last_index_of(str, separator, 0, XCharCaseInsensitive),XString_length_base(separator));
	}
	return str;
}

XString* XStringList_join_utf8(const XStringList* strList, const char* separator)
{
	if(strList==NULL|| separator==NULL)
		return NULL;
	size_t len = strlen(separator);
	if (len == 0 )
		return NULL;
	XString* temp = XString_create_utf8(separator);
	XString* str = XStringList_join(strList, temp);
	XString_delete_base(temp);
	return str;
}

#endif

