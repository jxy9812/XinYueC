#include "XVariant.h"
#include "XMemory.h"
#include "XClass.h"
#include "XString.h"
#include <string.h>
#include <stdlib.h>


#define XVariant_DataPtr(Var)    (((XVariant*)Var)->m_data)
//#define XVariant_DataPtr(Var)  (&(((XVariant*)Var)->data))
#define XVariant_Data(Var,Type)   (*((Type*)XVariant_DataPtr(Var)))

#define to_value(var,t) \
switch (var->m_type)\
{\
case XVariantType_Uint8:	return (t)XVariant_Data(var, uint8_t);\
case XVariantType_Uint16:	return (t)XVariant_Data(var, uint16_t);\
case XVariantType_Uint32:	return (t)XVariant_Data(var, uint32_t);\
case XVariantType_Uint64:	return (t)XVariant_Data(var, uint64_t);\
case XVariantType_Int8:		return (t)XVariant_Data(var, int8_t);\
case XVariantType_Int16:	return (t)XVariant_Data(var, int16_t);\
case XVariantType_Int32:	return (t)XVariant_Data(var, int32_t);\
case XVariantType_Int64:	return (t)XVariant_Data(var, int64_t);\
case XVariantType_Bool:		return (t)XVariant_Data(var, bool);\
case XVariantType_Char:		return (t)XVariant_Data(var, char);\
case XVariantType_UChar:	return (t)XVariant_Data(var, unsigned char);\
case XVariantType_Int:		return (t)XVariant_Data(var, int);\
case XVariantType_Size_t:	return (t)XVariant_Data(var, size_t);\
case XVariantType_Ptr:		return (t)XVariant_Data(var, void*);\
case XVariantType_Float:	return (t)XVariant_Data(var, float);\
case XVariantType_Double:	return (t)XVariant_Data(var, double);\
default:return 0;\
}
static void VXVariant_move(XVariant* var, XVariant* src);
static void VXVariant_copy(XVariant* var, const XVariant* src);
static void VXVariant_deinit(XVariant* var);
XVtable* XVariant_class_init()
{
	XVTABLE_INIT_DEFAULT(XVariant)
	// 继承类
	XVTABLE_INHERIT_XCLASS(XClass);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXVariant_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXVariant_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVariant_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XVariant);
	return XVTABLE_DEFAULT;
}

XVariant* XVariant_create_ex(XMemoryType memory, void* data, size_t dataSize, int type)
{
	XVariant* var = (XVariant*)XMemory_malloc(sizeof(XVariant), memory);
	if (!var) return NULL;
	/* 重要：必须先把 var 清零，避免 XVariant_init 读到未初始化的 m_data 等指针 */
	memset(var, 0, sizeof(XVariant));
	XVariant_init(var, data, dataSize, type);
	Set_Class_Memory(var, memory); Set_Class_IsHeap(var, true);
	return var;
}

XVariant* XVariant_create_copy(const XVariant* copy)
{
	if (!copy)return NULL;
	XVariant* var = XVariant_create_null();
	if (var && copy)
		XVariant_copy_base(var, copy);
	return var;
}

XVariant* XVariant_create_move(XVariant* move)
{
	XVariant* var = XVariant_create_null();
	if (var && move)
		XVariant_move_base(var, move);
	return var;
}

void XVariant_init(XVariant* var, void* data, size_t dataSize, int type)
{
	if (var == NULL)
		return;
	/* 1) 初始化基类虚函数表 */
	XClass_init(var);
	XClassGetVtable(var) = XVariant_class_init();
	/* 2) 调整 type/data 之前先把旧的 m_data 释放（避免泄漏） */
	if (var->m_data && var->m_dataSize > 0)
	{
		XFree_System(var->m_data);
		var->m_data = NULL;
		var->m_dataSize = 0;
	}
	if (dataSize > 0)
	{
		var->m_data = XMalloc_System(dataSize);
		if (var->m_data == NULL)
		{
			/* 分配失败：保持 m_data=NULL，绝不释放 var —— 它是调用方传入的 */
			var->m_dataSize = 0;
			var->m_type = type;
			return;
		}
		if (data != NULL)
			memcpy(var->m_data, data, dataSize);
	}
	else
	{
		var->m_data = NULL;
	}
	var->m_type = type;
	var->m_dataSize = dataSize;
}

XVariant* XVariant_create_null()
{
	return XVariant_create(NULL, 0,XVariantType_NULL);
}

XVariant* XVariant_create_uint8(uint8_t val)
{
	return XVariant_Create(val, XVariantType_Uint8);
}

XVariant* XVariant_create_uint16(uint16_t val)
{
	return XVariant_Create(val, XVariantType_Uint16);
}

XVariant* XVariant_create_uint32(uint32_t val)
{
	return XVariant_Create(val, XVariantType_Uint32);
}

XVariant* XVariant_create_uint64(uint64_t val)
{
	return XVariant_Create(val, XVariantType_Uint64);
}

XVariant* XVariant_create_int8(int8_t val)
{
	return XVariant_Create(val, XVariantType_Int8);
}

XVariant* XVariant_create_int16(int16_t val)
{
	return XVariant_Create(val, XVariantType_Int16);
}

XVariant* XVariant_create_int32(int32_t val)
{
	return XVariant_Create(val, XVariantType_Int32);
}

XVariant* XVariant_create_int64(int64_t val)
{
	return XVariant_Create(val, XVariantType_Int64);
}

XVariant* XVariant_create_bool(bool val)
{
	return XVariant_Create(val, XVariantType_Bool);
}

XVariant* XVariant_create_char(char val)
{
	return XVariant_Create(val, XVariantType_Char);
}

XVariant* XVariant_create_uchar(unsigned char val)
{
	return XVariant_Create(val, XVariantType_UChar);
}

XVariant* XVariant_create_int(int val)
{
	return XVariant_Create(val, XVariantType_Int);
}

XVariant* XVariant_create_size_t(size_t val)
{
	return XVariant_Create(val, XVariantType_Size_t);
}

XVariant* XVariant_create_ptr(void* val)
{
	return XVariant_Create(val, XVariantType_Ptr);
}

XVariant* XVariant_create_float(float val)
{
	return XVariant_Create(val, XVariantType_Float);
}

XVariant* XVariant_create_double(double val)
{
	return XVariant_Create(val, XVariantType_Double);
}

//转引用
void* XVariant_toRef(const XVariant* var, XVariantType type)
{
	if (var == NULL || var->m_type != type || var->m_dataSize == 0)
		return NULL;
	return XVariant_DataPtr(var);
}
uint8_t XVariant_toUint8(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toUShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint8_t);
}

uint8_t* XVariant_toUint8_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint8);
}

uint16_t XVariant_toUint16(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toUShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint16_t);
}

uint16_t* XVariant_toUint16_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint16);
}

uint32_t XVariant_toUint32(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toULong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint32_t);
}

uint32_t* XVariant_toUint32_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint32);
}

uint64_t XVariant_toUint64(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toULongLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, uint64_t);
}

uint64_t* XVariant_toUint64_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Uint64);
}

int8_t XVariant_toInt8(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int8_t);
}

int8_t* XVariant_toInt8_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int8);
}

int16_t XVariant_toInt16(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toShort(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int16_t);
}

int16_t* XVariant_toInt16_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int16);
}

int32_t XVariant_toInt32(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int32_t);
}

int32_t* XVariant_toInt32_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int32);
}

int64_t XVariant_toInt64(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toLongLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, int64_t);
}

int64_t* XVariant_toInt64_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int64);
}

bool XVariant_toBool(const XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, bool);
}

bool* XVariant_toBool_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Bool);
}

char XVariant_toChar(const XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, char);
}

char* XVariant_toChar_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Char);
}

unsigned char XVariant_toUChar(const XVariant* var)
{
	if (var == NULL)
		return 0;
	to_value(var, unsigned char);
}

unsigned char* XVariant_toUChar_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_UChar);
}

int XVariant_toInt(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toInt(XVariant_DataPtr(var),NULL,10);
	to_value(var, int);
}

int* XVariant_toInt_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Int);
}

size_t XVariant_toSize_t(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toULongLong(XVariant_DataPtr(var), NULL, 10);
	to_value(var, size_t);
}

size_t* XVariant_toSize_t_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Size_t);
}

void* XVariant_toPtr(const XVariant* var)
{
	return (void*)XVariant_toSize_t(var);
}

void** XVariant_toPtr_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Ptr);
}

float XVariant_toFloat(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toFloat(XVariant_DataPtr(var), NULL);
	switch (var->m_type) {
	case XVariantType_Uint8: return (float)(*((uint8_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint16: return (float)(*((uint16_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint32: return (float)(*((uint32_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint64: return (float)(*((uint64_t*)(((XVariant*)var)->m_data))); case XVariantType_Int8: return (float)(*((int8_t*)(((XVariant*)var)->m_data))); case XVariantType_Int16: return (float)(*((int16_t*)(((XVariant*)var)->m_data))); case XVariantType_Int32: return (float)(*((int32_t*)(((XVariant*)var)->m_data))); case XVariantType_Int64: return (float)(*((int64_t*)(((XVariant*)var)->m_data))); case XVariantType_Bool: return (float)(*((_Bool*)(((XVariant*)var)->m_data))); case XVariantType_Char: return (float)(*((char*)(((XVariant*)var)->m_data))); case XVariantType_UChar: return (float)(*((unsigned char*)(((XVariant*)var)->m_data))); case XVariantType_Int: return (float)(*((int*)(((XVariant*)var)->m_data))); case XVariantType_Size_t: return (float)(*((size_t*)(((XVariant*)var)->m_data))); case XVariantType_Float: return (float)(*((float*)(((XVariant*)var)->m_data))); case XVariantType_Double: return (float)(*((double*)(((XVariant*)var)->m_data))); default:return 0;
	};
}

float* XVariant_toFloat_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Float);
}

double XVariant_toDouble(const XVariant* var)
{
	if (var == NULL)
		return 0;
	if (var->m_type == XVariantType_String)
		return XString_toDouble(XVariant_DataPtr(var), NULL);
	switch (var->m_type) {
	case XVariantType_Uint8: return (double)(*((uint8_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint16: return (double)(*((uint16_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint32: return (double)(*((uint32_t*)(((XVariant*)var)->m_data))); case XVariantType_Uint64: return (double)(*((uint64_t*)(((XVariant*)var)->m_data))); case XVariantType_Int8: return (double)(*((int8_t*)(((XVariant*)var)->m_data))); case XVariantType_Int16: return (double)(*((int16_t*)(((XVariant*)var)->m_data))); case XVariantType_Int32: return (double)(*((int32_t*)(((XVariant*)var)->m_data))); case XVariantType_Int64: return (double)(*((int64_t*)(((XVariant*)var)->m_data))); case XVariantType_Bool: return (double)(*((_Bool*)(((XVariant*)var)->m_data))); case XVariantType_Char: return (double)(*((char*)(((XVariant*)var)->m_data))); case XVariantType_UChar: return (double)(*((unsigned char*)(((XVariant*)var)->m_data))); case XVariantType_Int: return (double)(*((int*)(((XVariant*)var)->m_data))); case XVariantType_Size_t: return (double)(*((size_t*)(((XVariant*)var)->m_data))); case XVariantType_Float: return (double)(*((float*)(((XVariant*)var)->m_data))); case XVariantType_Double: return (double)(*((double*)(((XVariant*)var)->m_data))); default:return 0;
	};
}

double* XVariant_toDouble_ref(const XVariant* var)
{
	return XVariant_toRef(var, XVariantType_Double);
}

static void setValue(XVariant* var, void* data, size_t size, int type)
{
	if (var == NULL)
		return;
	if (var->m_data && var->m_type != type)
	{
		XVariant_deinit_base(var);
	}
	if (var->m_data == NULL || var->m_dataSize != size)
	{
		if(size>0&& var->m_data==NULL)
		{
			var->m_data = XMalloc_System(size);
		}
		if (var->m_data == NULL&&size>0)
		{//失败
			var->m_dataSize = 0;
			return;
		}
		var->m_dataSize = size;
	}
	if(XVariant_DataPtr(var)&&data&&size)
		memcpy(XVariant_DataPtr(var), data, size);
	var->m_type = type;
}
void XVariant_setValue(XVariant* var,const XVariant* newVar)
{
	/*if (var == NULL || newVar == NULL||newVar->m_data==NULL||newVar->m_dataSize==0)
		return;*/
	return XVariant_copy_base(var, newVar);
		//setValue(var,NULL,);
}

void XVariant_setValue_null(XVariant* var)
{
	setValue(var, NULL, 0, XVariantType_NULL);
}
void XVariant_setValue_uint8(XVariant* var, uint8_t val)
{
	setValue(var,&val,sizeof(uint8_t), XVariantType_Uint8);
}

void XVariant_setValue_uint16(XVariant* var, uint16_t val)
{
	setValue(var, &val, sizeof(uint16_t), XVariantType_Uint16);
}

void XVariant_setValue_uint32(XVariant* var, uint32_t val)
{
	setValue(var, &val, sizeof(uint32_t), XVariantType_Uint32);
}

void XVariant_setValue_uint64(XVariant* var, uint64_t val)
{
	setValue(var, &val, sizeof(uint64_t), XVariantType_Uint64);
}

void XVariant_setValue_int8(XVariant* var, int8_t val)
{
	setValue(var, &val, sizeof(int8_t), XVariantType_Int8);
}

void XVariant_setValue_int16(XVariant* var, int16_t val)
{
	setValue(var, &val, sizeof(int16_t), XVariantType_Int16);
}

void XVariant_setValue_int32(XVariant* var, int32_t val)
{
	setValue(var, &val, sizeof(int32_t), XVariantType_Int32);
}

void XVariant_setValue_int64(XVariant* var, int64_t val)
{
	setValue(var, &val, sizeof(int64_t), XVariantType_Int64);
}

void XVariant_setValue_bool(XVariant* var, bool val)
{
	setValue(var, &val, sizeof(bool), XVariantType_Bool);
}

void XVariant_setValue_char(XVariant* var, char val)
{
	setValue(var, &val, sizeof(char), XVariantType_Char);
}

void XVariant_setValue_uchar(XVariant* var, unsigned char val)
{
	setValue(var, &val, sizeof( unsigned char), XVariantType_UChar);
}

void XVariant_setValue_int(XVariant* var, int val)
{
	setValue(var, &val, sizeof(int), XVariantType_Int);
}

void XVariant_setValue_size_t(XVariant* var, size_t val)
{
	setValue(var, &val, sizeof(size_t), XVariantType_Size_t);
}

void XVariant_setValue_ptr(XVariant* var, void* val)
{
	setValue(var, &val, sizeof(void*), XVariantType_Ptr);
}

void XVariant_setValue_float(XVariant* var, float val)
{
	setValue(var, &val, sizeof(float), XVariantType_Float);
}

void XVariant_setValue_double(XVariant* var, double val)
{
	setValue(var, &val, sizeof(double), XVariantType_Double);
}

void VXVariant_copy(XVariant* var, const XVariant* src)
{
	const XVariantTypeOps* ops;
	if (var == NULL || src == NULL)
		return;
	if (XClassIsVtableNull(var))
	{
		XVariant_init(var, NULL,0, XVariantType_NULL);
	}
	else if (var->m_type != src->m_type || var->m_dataSize != src->m_dataSize)
	{
		XVariant_deinit_base(var);//
	}
	if (src->m_dataSize == 0)
	{
		if (var->m_data)
			XVariant_deinit_base(var);
		var->m_data = NULL;
		var->m_dataSize = 0;
		var->m_type = src->m_type;
		return;
	}
	if (XVariant_DataPtr(var) == NULL)
	{
		var->m_data = XCalloc_System(1, src->m_dataSize);
		var->m_dataSize = src->m_dataSize;
		var->m_type = src->m_type;
	}
	ops = XVariantTypeOps_forType(src->m_type);
	if (ops)
	{
		if (ops->copy)
			ops->copy(XVariant_DataPtr(var), XVariant_DataPtr(src));
		else
			memcpy(XVariant_DataPtr(var), XVariant_DataPtr(src), src->m_dataSize);
		return;
	}
	memcpy(XVariant_DataPtr(var), XVariant_DataPtr(src), src->m_dataSize);
}

void VXVariant_move(XVariant* var, XVariant* src)
{
	if (var == NULL || src == NULL)
		return;
	if (var == src)
		return;
	if (XClassIsVtableNull(var))
	{
		XVariant_init(var, NULL, 0, XVariantType_NULL);
	}
	/* Variant 的移动是整个数据对象的所有权转移，不需要逐字段移动。 */
	if (var->m_data != NULL && var->m_data != src->m_data)
		XVariant_deinit_base(var);
	if (var->m_class.m_vtable == NULL)
		var->m_class = src->m_class;
	var->m_data = src->m_data;
	src->m_data = NULL;
	var->m_dataSize = src->m_dataSize;
	src->m_dataSize = 0;
	var->m_type = src->m_type;
	src->m_type = XVariantType_NULL;
}

void VXVariant_deinit(XVariant* var)
{
	const XVariantTypeOps* ops;
	if (var == NULL|| XVariant_DataPtr(var)==NULL)
		return;
	ops = XVariantTypeOps_forType(var->m_type);
	if (ops)
	{
		if (ops->deinit)
			ops->deinit(XVariant_DataPtr(var));
	}
	XFree_System(XVariant_DataPtr(var));
	XVariant_DataPtr(var) = NULL;
}

bool XVariant_isValid(const XVariant* var)
{
	if (var == NULL) return false;
	return var->m_type != XVariantType_NULL;
}

void XVariant_clear(XVariant* var)
{
	const XVariantTypeOps* ops;
	if (var == NULL || XVariant_DataPtr(var) == NULL || var->m_dataSize == 0)
		return;
	ops = XVariantTypeOps_forType(var->m_type);
	if (ops)
	{
		if (ops->clear)
			ops->clear(XVariant_DataPtr(var));
		else
			memset(XVariant_DataPtr(var), 0, var->m_dataSize);
		return;
	}
	memset(XVariant_DataPtr(var), 0, var->m_dataSize);
}

void XVariant_swap(XVariant* var, XVariant* other)
{
	if (var == NULL || other == NULL)
		return;
	XSwap(var,other,sizeof(XVariant));
}

int XVariant_type(XVariant* var)
{
	return var->m_type;
}

const char* XVariant_typeName(XVariant* var)
{
	const char* extensionName;
	if (var == NULL)
		return NULL;
	extensionName = XVariantTypeOps_nameForType(var->m_type);
	if (extensionName)
		return extensionName;
	switch (var->m_type)
	{
	case XVariantType_Uint8: return     "uint8_t";
	case XVariantType_Uint16:return     "uint16_t";
	case XVariantType_Uint32:return     "uint32_t";
	case XVariantType_Uint64:return     "uint64_t";
	case XVariantType_Int8: return      "int8_t";
	case XVariantType_Int16:return      "int16_t";
	case XVariantType_Int32:return      "int32_t";
	case XVariantType_Int64:return      "int64_t";
	case XVariantType_Bool:return       "bool";
	case XVariantType_Char:return       "char";
	case XVariantType_UChar:return	    "unsigned char";
	case XVariantType_Int:return	    "int";
	case XVariantType_Size_t:return	    "size_t";
	case XVariantType_Ptr:return        "void*";
	case XVariantType_Float:return	    "float";
	case XVariantType_Double:return	    "double";
	default:return NULL;
	}
}

int32_t XVariant_compare(XVariant* var, XVariant* cmp)
{
	const XVariantTypeOps* ops;
	if (var == NULL || cmp == NULL ||
		var->m_type != cmp->m_type ||
		var->m_dataSize != cmp->m_dataSize)
		return false;
	switch (var->m_type)
	{
	case XVariantType_Uint8:return uint8_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Uint16:return uint16_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Uint32:return uint32_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Uint64:return uint64_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int8:return int8_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int16:return int16_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int32:return int32_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Int64:return int64_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Bool:return bool_compare(var->m_data, cmp->m_data);
	case XVariantType_Char:return char_compare(var->m_data, cmp->m_data);
	case XVariantType_UChar:return unsigned_char_compare(var->m_data, cmp->m_data);
	case XVariantType_Int:return int_compare(var->m_data, cmp->m_data);
	case XVariantType_Size_t:return size_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Ptr:return uintptr_t_compare(var->m_data, cmp->m_data);
	case XVariantType_Float:return float_compare(var->m_data, cmp->m_data);
	case XVariantType_Double:return double_compare(var->m_data, cmp->m_data);
	default:
	{
		ops = XVariantTypeOps_forType(var->m_type);
		if (ops && ops->compare)
			return ops->compare(var->m_data, cmp->m_data);
		return false;
	}
	}
}

void* XVariant_data(XVariant* var)
{
	if(var)
		return XVariant_DataPtr(var);
	return NULL;
}

size_t XVariant_dataSize(XVariant* var)
{
	if(var)
		return var->m_dataSize;
	return 0;
}

void XVariant_setDataRef(XVariant* var, void* data, size_t dataSize, int type)
{
	if (!var)
		return;
	if (var->m_data == data) {
		var->m_dataSize = data ? dataSize : 0;
		var->m_type = type;
		return;
	}
	if (var->m_data)
		XVariant_deinit_base(var);
	var->m_data = data;
	var->m_dataSize = data ? dataSize : 0;
	var->m_type = type;
}
