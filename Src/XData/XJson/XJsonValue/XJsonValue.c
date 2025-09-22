#include "XJsonValue.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XAlgorithm.h"
#include "XMap.h"
#include <string.h>
XJsonValue* XJsonValue_create_null(void) 
{
    XJsonValue* value = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(value, XJsonValue_Null);
    return value;
}

XJsonValue* XJsonValue_create_bool(bool value)
{
    XJsonValue* val = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(val, XJsonValue_Bool);
    if (val) 
    {
        val->data.boolean = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_double(double value) 
{
    XJsonValue* val = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(val, XJsonValue_Double);
    if (val)
    {
        val->data.number = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_int(int64_t value)
{
    XJsonValue* val = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(val, XJsonValue_Int);
    if (val)
    {
        val->data.integer = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_string(const XString* string) 
{
    if (string == NULL)
        return NULL;
    XJsonValue* val = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(val, XJsonValue_String);
    if (val)
    {
        val->data.string = XString_create_copy(string);
    }
    return val;
}

XJsonValue* XJsonValue_create_array(XJsonArray* array) 
{
    if (array == NULL)
        return NULL;
    XJsonValue* val = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(val, XJsonValue_Array);
    if (val)
    {
        val->data.array = XJsonArray_create_copy(array);
    }
    return val;
}

XJsonValue* XJsonValue_create_object(XJsonObject* object) 
{
    XJsonValue* val = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    XJsonValue_init(val, XJsonValue_Object);
    if (val)
    {
        val->data.object = XJsonObject_create_copy(object);
    }
    return val;
}

XJsonValue* XJsonValue_create_copy(XJsonValue* copy)
{
    XJsonValue* value = XJsonValue_create_null();
    if (value && copy)
        XJsonValue_copy(value, copy);
    return value;
}

XJsonValue* XJsonValue_create_move(XJsonValue* move)
{
    XJsonValue* value = XJsonValue_create_null();
    if (value && move)
        XJsonValue_move(value, move);
    return value;
}

void XJsonValue_init(XJsonValue* var, XJsonValueType type)
{
    if (var) 
    {
        var->type = type;
        memset(&var->data, 0, sizeof(var->data));
    }
}

void XJsonValue_copy(XJsonValue* var, const XJsonValue* src)
{
    if (var == NULL || src == NULL)
        return;
    XJsonValue_deinit(var);
    if (!var) return;

    switch (src->type)
    {
    case XJsonValue_Bool:var->data.boolean = src->data.boolean; break;
    case XJsonValue_Double:var->data.number = src->data.number; break;
    case XJsonValue_Int:var->data.integer = src->data.integer; break;
    case XJsonValue_String:var->data.string=XString_create_copy(src->data.string); break;
    case XJsonValue_Array: var->data.array = XJsonArray_create_copy(src->data.array); break;
    case XJsonValue_Object: var->data.object = XJsonObject_create_copy(src->data.object); break;
    default:
        break;
    };
    var->type = src->type;
}

void XJsonValue_move(XJsonValue* var, XJsonValue* src)
{
    XJsonValue_deinit(var);
    if (!var) return;
    XSwap(var,src,sizeof(XJsonValue));
}

void XJsonValue_deinit(XJsonValue* value)
{
    if (!value) return;

     switch (value->type) 
     {
     case XJsonValue_String:
         if (value->data.string) 
         {
             XString_delete_base(value->data.string);
         }
         break;
     case XJsonValue_Array:
         if (value->data.array) 
         {
             XJsonArray_delete_base(value->data.array);
         }
         break;
     case XJsonValue_Object:
         if (value->data.object) 
         {
             XJsonObject_delete_base(value->data.object);
         }
         break;
     default:
         break;
     }
     memset(value,0,sizeof(XJsonValue));
     value->type = XJsonValue_Invalid;
}

void XJsonValue_delete(XJsonValue* value) 
{
    if (!value) return;
    XJsonValue_deinit(value);
    XMemory_free(value);
}

void XJsonValue_clear(XJsonValue* value)
{
    if (!value) return;

    switch (value->type)
    {
    case XJsonValue_Invalid:
    case XJsonValue_Null:break;
    case XJsonValue_String:
        if (value->data.string)
        {
            XString_clear_base(value->data.string);
        }
        break;
    case XJsonValue_Array:
        if (value->data.array)
        {
            XJsonArray_clear_base(value->data.array);
        }
        break;
    case XJsonValue_Object:
        if (value->data.object)
        {
            XJsonObject_clear_base(value->data.object);
        }
        break;
    default:
        value->data.integer = 0;
        break;
    }
}

XJsonValueType XJsonValue_type(const XJsonValue* value) {
    return value ? value->type : XJsonValue_Invalid;
}

bool XJsonValue_isNull(const XJsonValue* value) {
    return XJsonValue_type(value) == XJsonValue_Null;
}

bool XJsonValue_isBool(const XJsonValue* value) {
    return XJsonValue_type(value) == XJsonValue_Bool;
}

bool XJsonValue_isDouble(const XJsonValue* value) {
    return XJsonValue_type(value) == XJsonValue_Double;
}

bool XJsonValue_isInt(const XJsonValue* value)
{
    return XJsonValue_type(value) == XJsonValue_Int;
}

bool XJsonValue_isString(const XJsonValue* value) {
    return XJsonValue_type(value) == XJsonValue_String;
}

bool XJsonValue_isArray(const XJsonValue* value) {
    return XJsonValue_type(value) == XJsonValue_Array;
}

bool XJsonValue_isObject(const XJsonValue* value) {
    return XJsonValue_type(value) == XJsonValue_Object;
}

bool XJsonValue_toBool(const XJsonValue* value, bool defaultValue) {
    return (value && value->type == XJsonValue_Bool) ? value->data.boolean : defaultValue;
}

double XJsonValue_toDouble(const XJsonValue* value, double defaultValue) {
    return (value && value->type == XJsonValue_Double) ? value->data.number : defaultValue;
}

int64_t XJsonValue_toInt(const XJsonValue* value, int64_t defaultValue)
{
    return (value && value->type == XJsonValue_Int) ? value->data.integer : defaultValue;
}

const XString* XJsonValue_toString(const XJsonValue* value) 
{
    return (value && value->type == XJsonValue_String) ? value->data.string : NULL;
}

XJsonArray* XJsonValue_toArray(const XJsonValue* value) {
    return (value && value->type == XJsonValue_Array) ? value->data.array : NULL;
}

XJsonObject* XJsonValue_toObject(const XJsonValue* value) {
    return (value && value->type == XJsonValue_Object) ? value->data.object : NULL;
}

void XJsonValue_setNull(XJsonValue* value)
{
    if (!value) return;
    XJsonValue_deinit(value);
    value->type = XJsonValue_Null;
}

void XJsonValue_setBool(XJsonValue* value, bool b) 
{
    if (!value) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Bool;
    value->data.boolean = b;
}

void XJsonValue_setDouble(XJsonValue* value, double d) 
{
    if (!value) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Double;
    value->data.number = d;
}

void XJsonValue_setInt(XJsonValue* value, int64_t i)
{
    if (!value) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Int;
    value->data.integer = i;
}

void XJsonValue_setString(XJsonValue* value, const XString* s) 
{
    if (!value) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_String;
    value->data.string = XString_create_copy(s);
}

void XJsonValue_setString_move(XJsonValue* value, const XString* s)
{
    if (!value) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_String;
    value->data.string = XString_create_move(s);
}

void XJsonValue_setString_utf8(XJsonValue* value, const char* utf8)
{
    if (!value) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_String;
    value->data.string = XString_create_utf8(utf8);
}

void XJsonValue_setArray(XJsonValue* value, XJsonArray* a)
{
    if (!value || !a) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Array;
    value->data.array = XJsonArray_create_copy(a);
}

void XJsonValue_setArray_move(XJsonValue* value, XJsonArray* a)
{
    if (!value || !a) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Array;
    value->data.array = XJsonArray_create_move(a);
}

void XJsonValue_setObject(XJsonValue* value, XJsonObject* o) 
{
    if (!value || !o) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Object;
    value->data.object = XJsonObject_create_copy(o);
}
void XJsonValue_setObject_move(XJsonValue* value, XJsonObject* o)
{
    if (!value || !o) return;

    XJsonValue_deinit(value);
    value->type = XJsonValue_Object;
    value->data.object = XJsonObject_create_move(o);
}
XVariant* XJsonValue_toVariant(const XJsonValue* val)
{
    if (val == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonValue), XVariantType_JsonValue);
    XJsonValue_init(var->m_data, val->type);
    XJsonValue_copy(var->m_data, val);
    return var;
}

XVariant* XJsonValue_toVariant_move(XJsonValue* val)
{
    if (val == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonValue), XVariantType_JsonValue);
    XJsonValue_init(var->m_data, val->type);
    XJsonValue_move(var->m_data, val);
    return var;
}

XVariant* XJsonValue_toVariant_ref(XJsonValue* val)
{
    if (val == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, 0, XVariantType_JsonValue);
    if (var == NULL)
        return NULL;
    var->m_data = val;
    var->m_dataSize = sizeof(XJsonValue);
    return var;
}

XJsonValue* XJsonValue_fromVariant(const XVariant* variant) {
    if (!variant) return NULL;

    XJsonValue* value = XJsonValue_create_null();
    //if (!value) return NULL;

    //XVariantType type = XVariant_type(variant);
    //switch (type) {
    //case XVariantType_NULL:
    //    // 已经是null
    //    break;
    //case XVariantType_Bool:
    //    XJsonValue_setBool(value, XVariant_toBool(variant));
    //    break;
    //case XVariantType_Int:
    //case XVariantType_Double:
    //    XJsonValue_setDouble(value, XVariant_toDouble(variant));
    //    break;
    //case XVariantType_String:
    //    XJsonValue_setString(value, XVariant_toString(variant));
    //    break;
    //case XVariantType_List:
    //{
    //    XJsonArray* array = XJsonArray_fromVariantList(XVariant_toList(variant));
    //    XJsonValue_setArray(value, array);
    //    break;
    //}
    //case XVariantType_Map:
    //{
    //    XJsonObject* object = XJsonObject_fromVariantMap(XVariant_toMap(variant));
    //    XJsonValue_setObject(value, object);
    //    break;
    //}
    //default:
    //    XJsonValue_delete(value);
    //    return NULL;
    //}

    return value;
}