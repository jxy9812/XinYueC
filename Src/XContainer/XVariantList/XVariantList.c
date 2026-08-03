#include "XVariantList.h"
#include "XVariant.h"
#include "XVariantTypeOps.h"

XVARIANT_TYPE_OPS_DEFINE(XVariantList, sizeof(XVariantList), XVariantList_copy_base,
	XVariantList_move_base, XVariantList_clear_base, XVariantList_deinit_base,
	NULL, "XVariantList");

XVariant* XVariantList_toVariant(const XVariantList* list)
{
	XVariant* var;
	if (!list)
		return NULL;
	var = XVariant_create(NULL, sizeof(XVariantList), XVariantType_List);
	if (!var)
		return NULL;
	XVariantList_init((XVariantList*)XVariant_data(var));
	XVariantList_copy_base(XVariant_data(var), list);
	return var;
}

XVariant* XVariantList_toVariant_move(XVariantList* list)
{
	XVariant* var;
	if (!list)
		return NULL;
	var = XVariant_create(NULL, sizeof(XVariantList), XVariantType_List);
	if (!var)
		return NULL;
	XVariantList_init((XVariantList*)XVariant_data(var));
	XVariantList_move_base(XVariant_data(var), list);
	return var;
}

XVariant* XVariantList_toVariant_ref(XVariantList* list)
{
	XVariant* var;
	if (!list)
		return NULL;
	var = XVariant_create(NULL, 0, XVariantType_List);
	if (!var)
		return NULL;
	XVariant_setDataRef(var, list, sizeof(XVariantList), XVariantType_List);
	return var;
}

XVariantList* XVariantList_fromVariant(const XVariant* var)
{
	return XVariantList_create_copy(XVariantList_fromVariant_ref(var));
}

XVariantList* XVariantList_fromVariant_ref(const XVariant* var)
{
	return (XVariantList*)XVariant_toRef(var, XVariantType_List);
}

static bool XVariantList_prepareVariant(XVariant* var)
{
	if (!var)
		return false;
	if (var->m_type != XVariantType_List)
	{
		XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XVariantList));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XVariantList);
		XVariantList_init((XVariantList*)var->m_data);
		var->m_type = XVariantType_List;
	}
	else if (!var->m_data || var->m_dataSize != sizeof(XVariantList))
	{
		if (var->m_data)
			XVariant_deinit_base(var);
		var->m_data = XMalloc_System(sizeof(XVariantList));
		if (!var->m_data)
		{
			var->m_dataSize = 0;
			return false;
		}
		var->m_dataSize = sizeof(XVariantList);
		XVariantList_init((XVariantList*)var->m_data);
	}
	return true;
}

void XVariantList_setVariant(XVariant* var, const XVariantList* list)
{
	if (!list || !XVariantList_prepareVariant(var))
		return;
	XVariantList_copy_base(XVariant_data(var), list);
}

void XVariantList_setVariant_move(XVariant* var, XVariantList* list)
{
	if (!list || !XVariantList_prepareVariant(var))
		return;
	XVariantList_move_base(XVariant_data(var), list);
}

void XVariantList_setVariant_ref(XVariant* var, XVariantList* list)
{
	if (!var || !list)
		return;
	XVariant_setDataRef(var, list, sizeof(XVariantList), XVariantType_List);
}

XVtable* XVariantList_class_init()
{
	return XVector_class_init();
}

XVariantList* XVariantList_create()
{
	XVariantList* vector = XMalloc_System(sizeof(XVariantList));
	XVariantList_init(vector);
	Set_Class_MemoryFree(vector, XFree_System);
	return vector;
}
XVariantList* XVariantList_create_copy(const XVariantList* other)
{
	if (other == NULL)
		return NULL;
	XVariantList* list = XVariantList_create();
	if (list == NULL)
		return NULL;
	XVariantList_copy_base(list, other);
	return list;
}
XVariantList* XVariantList_create_move(XVariantList* other)
{
	if (other == NULL)
		return NULL;
	XVariantList* list = XVariantList_create();
	if (list == NULL)
		return NULL;
	XVariantList_move_base(list, other);
	return list;
}
void XVariantList_init(XVariantList* list)
{
	if (list == NULL)
		return;
	XVector_init(list, sizeof(XVariant),true);
	XClassGetVtable(list) = XVariantList_class_init();
	XContainerSetDataCopyMethod(list, XVariant_copy_base);
	XContainerSetDataMoveMethod(list, XVariant_move_base);
	XContainerSetDataDeinitMethod(list, XVariant_deinit_base);
	XContainerSetCompare(list, uintptr_t_compare);
}
