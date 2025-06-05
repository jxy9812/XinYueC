#include"XModbusFunctionHandler.h"
#include"XVector.h"
static const bool equality(const XModbusFunctionHandler* Value, const XModbusFunctionHandler* CompareValue)
{
	return Value->code == CompareValue->code;
}
XModbusFunctionHandlerList* XModbusFuncCodeList_create()
{
	XModbusFunctionHandlerList* list = XVector_Create(XModbusFunctionHandler);
	list->m_equality = equality;
	return list;
}

void XModbusFuncCodeList_push(XModbusFunctionHandlerList* list, XModbusFunctionHandler* data)
{
	if (list == NULL || data == NULL)
		return;	
	XModbusFuncCodeList_remove(list, data->code);
	
	XVector_push_back_base(list, data);
}

void XModbusFuncCodeList_remove(XModbusFunctionHandlerList* list, uint8_t code)
{
	if (list == NULL)
		return;
	XModbusFunctionHandler* find = XModbusFuncCodeList_findFuncCode(list,code);
	
	//if (XContainerDataFreeMethod(list) == NULL)
	//{//没有设置自动释放手动释放
	//	//if(find->data)
	//		
	//}
	XVector_erase_base(list, find);
}

XModbusFunctionHandler* XModbusFuncCodeList_findFuncCode(XModbusFunctionHandlerList* list, uint8_t code)
{
	return XVector_find_base(list,&code);
}
