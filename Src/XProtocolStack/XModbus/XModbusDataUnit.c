#include"XModbusDataUnit.h"
#include"XVector.h"
#include"XBitArray.h"
#include"XMemory.h"
#include<string.h>

static void VXModbusDataUnit_move(XModbusDataUnit* unit, XModbusDataUnit* src);
static void VXModbusDataUnit_copy(XModbusDataUnit* unit, const XModbusDataUnit* src);
static void VXModbusDataUnit_deinit(XModbusDataUnit* unit);

// 辅助函数：判断是否使用位存储
static inline bool XModbusDataUnit_isBitType(XModbusRegisterType type) {
	return type == XModbusDiscreteInputs || type == XModbusCoils;
}

// 辅助函数：根据类型创建容器
static XContainer* XModbusDataUnit_createContainer(XModbusRegisterType type, size_t count) {
	if (XModbusDataUnit_isBitType(type)) {
		return (XContainer*)XBitArray_create(count);
	} else {
		XVector* vec = XVector_Create(uint16_t);
		if (vec && count > 0) {
			XVector_resize_base(vec, count);
		}
		return (XContainer*)vec;
	}
}

// 辅助函数：调整容器大小
static void XModbusDataUnit_resizeContainer(XModbusRegisterType type, XContainer* container, size_t newCount) {
	if (!container) return;
	if (XModbusDataUnit_isBitType(type)) {
		XBitArray_resize((XBitArray*)container, newCount);
	} else {
		XVector_resize_base((XVector*)container, newCount);
	}
}

// 辅助函数：复制容器
static XContainer* XModbusDataUnit_copyContainer(XModbusRegisterType type, const XContainer* container) {
	if (!container) return NULL;
	if (XModbusDataUnit_isBitType(type)) {
		return (XContainer*)XBitArray_create_copy((const XBitArray*)container);
	} else {
		return (XContainer*)XVector_create_copy((const XVector*)container);
	}
}

XVtable* XModbusDataUnit_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XClass))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	// 继承 XModbusDevice
	XVTABLE_INHERIT_XCLASS(XClass);
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
	if (!unit) return NULL;
	XModbusDataUnit_init(unit);
	Set_Class_MemoryFree(unit, XFree_System);
	return unit;
}

XModbusDataUnit* XModbusDataUnit_create_copy(const XModbusDataUnit* unit)
{
	if (!unit) return NULL;
	XModbusDataUnit* newUnit = XModbusDataUnit_create();
	if (!newUnit) return NULL;
	XModbusDataUnit_copy_base(newUnit,unit);
	return newUnit;
}

XModbusDataUnit* XModbusDataUnit_create_move(const XModbusDataUnit* unit)
{
	if (!unit) return NULL;
	XModbusDataUnit* newUnit = XModbusDataUnit_create();
	if (!newUnit) return NULL;
	XModbusDataUnit_move_base(newUnit, unit);
	return newUnit;
}

XModbusDataUnit* XModbusDataUnit_create_ex(XModbusRegisterType type, uint16_t startAddress, size_t valueCount)
{
	XModbusDataUnit* unit = (XModbusDataUnit*)XMalloc_System(sizeof(XModbusDataUnit));
	if (!unit) return NULL;
	XModbusDataUnit_init_ex(unit, type, startAddress, valueCount);
	Set_Class_MemoryFree(unit, XFree_System);
	return unit;
}

void XModbusDataUnit_init_ex(XModbusDataUnit* unit, XModbusRegisterType type, uint16_t startAddress, size_t valueCount)
{
	XModbusDataUnit_init(unit);
	if (!unit) return;
	
	unit->m_type = type;
	unit->m_startAddress = startAddress;
	//unit->m_valueCount = valueCount;
	unit->m_data = XModbusDataUnit_createContainer(type, valueCount);
}

void XModbusDataUnit_init(XModbusDataUnit* unit)
{
	if (!unit) return;
	XClass_init(unit);
	XClassGetVtable(unit) = XModbusDataUnit_class_init();
	memset(((XClass*)unit) + 1, 0, sizeof(XModbusDataUnit) - sizeof(XClass));
	unit->m_startAddress = 0xFFFF;
	//unit->m_valueCount = 0;
	unit->m_data = NULL;
}

// 辅助函数：判断容器类型是否相同（位类型或寄存器类型）
static inline bool XModbusDataUnit_sameContainerType(XModbusRegisterType a, XModbusRegisterType b) {
	return XModbusDataUnit_isBitType(a) == XModbusDataUnit_isBitType(b);
}

void XModbusDataUnit_setRegisterType(XModbusDataUnit* unit, XModbusRegisterType type)
{
	if (!unit) return;
	
	// 只有容器类型变化时才需要重建（位类型 <-> 寄存器类型）
	if (!XModbusDataUnit_sameContainerType(unit->m_type, type)) 
	{
		size_t count = 0;
		// 释放旧容器
		if (unit->m_data) {
			count = XContainerSize(unit->m_data);
			XContainer_delete_base(unit->m_data);
		}
		// 创建新容器
		unit->m_data = XModbusDataUnit_createContainer(type, count);
	}
	unit->m_type = type;
}

void XModbusDataUnit_setStartAddress(XModbusDataUnit* unit, uint16_t startAddress)
{
	if (unit)
		unit->m_startAddress = startAddress;
}

bool XModbusDataUnit_setValue(XModbusDataUnit* unit, size_t index, uint16_t value)
{
	if (!unit || !(unit->m_data) || index >= XContainerSize(unit->m_data))
		return false;
	
	if (XModbusDataUnit_isBitType(unit->m_type)) {
		return XBitArray_setBit(unit->m_bitArray, index, value != 0);
	} else {
		if (index >= XVector_size_base(unit->m_vector))
			return false;
		XVector_At_Base(unit->m_vector, index, uint16_t) = value;
		return true;
	}
}

void XModbusDataUnit_setValueCount(XModbusDataUnit* unit, size_t newCount)
{
	if (!unit) return;
	
	//unit->m_valueCount = newCount;
	
	if (!unit->m_data) {
		unit->m_data = XModbusDataUnit_createContainer(unit->m_type, newCount);
	} else {
		XModbusDataUnit_resizeContainer(unit->m_type, unit->m_data, newCount);
	}
}
	
bool XModbusDataUnit_setValues(XModbusDataUnit* unit, XVector* values)
{
	if (!unit || !values) return false;
	
	// 仅对寄存器类型有效
	if (XModbusDataUnit_isBitType(unit->m_type)) {
		return false; // 位类型应使用 setBitArray
	}
	
	if (!unit->m_vector) {
		unit->m_vector = XVector_create_copy(values);
	} else {
		XVector_clear_base(unit->m_vector);
		XVector_push_back_2(unit->m_vector, XVector_constData(values), XContainerSize(values));
	}
	//unit->m_valueCount = XContainerSize(values);
	return true;
}

bool XModbusDataUnit_isValid(const XModbusDataUnit* unit)
{
	if (!unit) return false;
	return unit->m_type != XModbusInvalid && unit->m_startAddress != 0xFFFF;
}

XModbusRegisterType XModbusDataUnit_registerType(const XModbusDataUnit* unit)
{
	if (!unit)
		return false;
	return unit->m_type;
}

int XModbusDataUnit_startAddress(const XModbusDataUnit* unit)
{
	if (!unit) return -1;
	return (int)unit->m_startAddress;
}

uint16_t XModbusDataUnit_value(const XModbusDataUnit* unit, size_t index)
{
	if (!unit || !unit->m_data || index >= XContainerSize(unit->m_data))
		return 0;
	
	if (XModbusDataUnit_isBitType(unit->m_type)) {
		return XBitArray_getBit(unit->m_bitArray, index) ? 1 : 0;
	} else {
		void* ptr = XVector_at_base(unit->m_vector, index);
		return ptr ? *((uint16_t*)ptr) : 0;
	}
}

size_t XModbusDataUnit_valueCount(const XModbusDataUnit* unit)
{
	if (!unit || !(unit->m_data)) return 0;
	return XContainerSize(unit->m_data);
}

XVector* XModbusDataUnit_values1(const XModbusDataUnit* unit)
{
	if (!unit || !(unit->m_data)|| XContainerSize(unit->m_data)==0) return NULL;
	
	size_t count = XContainerSize(unit->m_data);
	// 为了保持向后兼容，将数据转换为XVector
	if (XModbusDataUnit_isBitType(unit->m_type)) {
		// 位类型：转换为uint16_t向量
		XVector* vec = XVector_Create(uint16_t);
		if (!vec) return NULL;
		XVector_resize_base(vec, count);
		for (size_t i = 0; i < count; i++) {
			uint16_t val = XBitArray_getBit(unit->m_bitArray, i) ? 1 : 0;
			XVector_At_Base(vec, i, uint16_t) = val;
		}
		return vec;
	} else {
		return XVector_create_copy(unit->m_vector);
	}
}

const XVector* XModbusDataUnit_values1_const(const XModbusDataUnit* unit)
{
	if (!unit || XModbusDataUnit_isBitType(unit->m_type))
		return NULL;
	return unit->m_vector;
}

XBitArray* XModbusDataUnit_values2(const XModbusDataUnit* unit)
{
	if (!unit || !XModbusDataUnit_isBitType(unit->m_type))
		return NULL;
	return XBitArray_create_copy(unit->m_bitArray);
}

const XBitArray* XModbusDataUnit_values2_const(const XModbusDataUnit* unit)
{
	if (!unit || !XModbusDataUnit_isBitType(unit->m_type))
		return NULL;
	return unit->m_bitArray;
}

bool XModbusDataUnit_setBitArray(XModbusDataUnit* unit, const XBitArray* bits)
{
	if (!unit || !bits || !XModbusDataUnit_isBitType(unit->m_type))
		return false;
	
	if (unit->m_bitArray) {
		XContainer_delete_base(unit->m_data);
	}
	unit->m_bitArray = XBitArray_create_copy(bits);
	//unit->m_valueCount = XBitArray_size_base(bits);
	return true;
}

XModbusDataUnitMap* XModbusDataUnitMap_create()
{
	XModbusDataUnitMap* map = XMap_Create(XModbusRegisterType, XModbusDataUnit,int_compare);
	if (map)
	{
		XContainerSetDataCopyMethod(map, XModbusDataUnit_copy_base);
		XContainerSetDataMoveMethod(map, XModbusDataUnit_move_base);
		XContainerSetDataDeinitMethod(map, XModbusDataUnit_deinit_base);
	}
	return map;
}

void VXModbusDataUnit_move(XModbusDataUnit* unit, XModbusDataUnit* src)
{
	if (!unit || !src) return;
	
	// 检查是否需要初始化
	if (XClassIsVtableNull(unit)) {
		unit->m_type = src->m_type;
		unit->m_startAddress = src->m_startAddress;
		//unit->m_valueCount = src->m_valueCount;
		unit->m_data = src->m_data;
	} else {
		// 目标已初始化，先清理再移动
		if (unit->m_data) {
			XContainer_delete_base(unit->m_data);
		}
		unit->m_type = src->m_type;
		unit->m_startAddress = src->m_startAddress;
		//unit->m_valueCount = src->m_valueCount;
		unit->m_data = src->m_data;
	}
	
	// 重置源
	src->m_data = NULL;
	src->m_type = XModbusInvalid;
	src->m_startAddress = 0xFFFF;
	//src->m_valueCount = 0;
}

void VXModbusDataUnit_copy(XModbusDataUnit* unit, const XModbusDataUnit* src)
{
	if (!unit || !src) return;
	
	// 检查是否需要初始化
	if (XClassIsVtableNull(unit)) {
		XModbusDataUnit_init(unit);
	}
	
	// 先清理旧容器
	if (unit->m_data) {
		XContainer_delete_base(unit->m_data);
	}
	
	// 复制基本字段
	unit->m_type = src->m_type;
	unit->m_startAddress = src->m_startAddress;
	//unit->m_valueCount = src->m_valueCount;
	
	// 复制数据容器
	unit->m_data = XModbusDataUnit_copyContainer(src->m_type, src->m_data);
}

void VXModbusDataUnit_deinit(XModbusDataUnit* unit)
{
	if (!unit) return;
	
	// 释放数据容器（使用父类删除函数）
	if (unit->m_data) {
		XContainer_delete_base(unit->m_data);
		unit->m_data = NULL;
	}
	
	unit->m_startAddress = 0xFFFF;
	unit->m_type = XModbusInvalid;
	//unit->m_valueCount = 0;
}
