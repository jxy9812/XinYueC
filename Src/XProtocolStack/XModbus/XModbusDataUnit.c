#include"XModbusDataUnit.h"
#include"XVector.h"
#include"XMemory.h"
#include<string.h>
static void VXModbusDataUnit_move(XModbusDataUnit* unit, XVariant* src);
static void VXModbusDataUnit_copy(XModbusDataUnit* unit, const XVariant* src);
static void VXModbusDataUnit_deinit(XModbusDataUnit* unit);

XVtable* XModbusDataUnit_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//	void* table[] = { VXClass_copy,VXClass_move,VXClass_deinit };
		//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
		//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXModbusDataUnit_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXModbusDataUnit_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXModbusDataUnit_deinit);
#if SHOWCONTAINERSIZE
	printf("XModbusDataUnit size:%d\n", XVtable_size(XClassVtable));
#endif
	return XVTABLE_DEFAULT;
}

XModbusDataUnit* XModbusDataUnit_create()
{
	XModbusDataUnit* unit = XMalloc_System(sizeof(XModbusDataUnit));
	XModbusDataUnit_init(unit);
	Set_Class_MemoryFree(unit, XFree_System);
	return unit;
}

XModbusDataUnit* XModbusDataUnit_create_copy(XModbusDataUnit* unit)
{
	XModbusDataUnit* new = XModbusDataUnit_create();
	new->m_type = unit->m_type;
	new->m_startAddress = unit->m_startAddress;
	new->m_valueCount = unit->m_valueCount;
	XVector_copy_base(new->m_values, unit->m_values);
	return new;
}

void XModbusDataUnit_init(XModbusDataUnit* unit)
{
	XClass_init(unit);
	XClassGetVtable(unit) = XModbusDataUnit_class_init();
	memset(((XClass*)unit) + 1, 0, sizeof(XModbusDataUnit) - sizeof(XClass));
	unit->m_startAddress = -1;
	unit->m_values = XVector_Create(int16_t);
}

void XModbusDataUnit_setRegisterType(XModbusDataUnit* unit, XModbusRegisterType type)
{
	if (unit)
		unit->m_type = type;
}

void XModbusDataUnit_setStartAddress(XModbusDataUnit* unit, uint16_t startAddress)
{
	if (unit)
		unit->m_startAddress = startAddress;
}

bool XModbusDataUnit_setValue(XModbusDataUnit* unit, size_t index, int16_t value)
{
	if(!unit)
		return false;
	if (!unit->m_values||XVector_isEmpty_base(unit->m_values) || index >= XVector_size_base(unit->m_values))
		return false;
	XVector_At_Base(unit->m_values,index,int16_t)=value;
	return true;
}

void XModbusDataUnit_setValueCount(XModbusDataUnit* unit, size_t newCount)
{
	if (!unit)
		return;
	unit->m_valueCount = newCount;
	if (!unit->m_values)
	{
		unit->m_values=XVector_Create(int16_t);
	}
	XVector_resize_base(unit->m_values, newCount);
}
	
bool XModbusDataUnit_setValues(XModbusDataUnit* unit, XVector* values)
{
	if (!unit|| !values)
		return false;
	if (!unit->m_values)
		unit->m_values = XVector_Create(int16_t);
	XVector_clear_base(unit->m_values);
	XVector_append_array_base(unit->m_values,XContainerSharedDataPtr(values),XContainerSize(values));
	return true;
}

bool XModbusDataUnit_isValid(const XModbusDataUnit* unit)
{
	if (!unit)
		return false;
	return unit->m_type != XModbusInvalid && unit->m_startAddress != -1;
}

XModbusRegisterType XModbusDataUnit_registerType(const XModbusDataUnit* unit)
{
	if (!unit)
		return false;
	return unit->m_type;
}

int XModbusDataUnit_startAddress(const XModbusDataUnit* unit)
{
	if (!unit)
		return -1;
	return unit->m_startAddress;
}

int16_t XModbusDataUnit_value(const XModbusDataUnit* unit, size_t index)
{
	if (!unit|| !unit->m_values)
		return 0;
	void* ptr=XVector_at_base(unit->m_values,index);
	if (ptr == NULL)
		return 0;
	return *((int16_t*)ptr);
}

size_t XModbusDataUnit_valueCount(const XModbusDataUnit* unit)
{
	if (!unit)
		return -1;
	return unit->m_valueCount;
}

XVector* XModbusDataUnit_values(const XModbusDataUnit* unit)
{
	if (!unit)
		return NULL;
	return XVector_create_copy(unit->m_values);
}

void VXModbusDataUnit_move(XModbusDataUnit* unit, XVariant* src)
{
	if (!unit || !src)
		return;

}

void VXModbusDataUnit_copy(XModbusDataUnit* unit, const XVariant* src)
{
	if (!unit || !src)
		return;
}

void VXModbusDataUnit_deinit(XModbusDataUnit* unit)
{
	if (!unit)
		return;
	if (unit->m_values)
	{
		XVector_deinit_base(unit->m_values);
		unit->m_values = NULL;
	}
	unit->m_startAddress = -1;
	unit->m_type =XModbusInvalid ;
	unit->m_valueCount = 0;
}