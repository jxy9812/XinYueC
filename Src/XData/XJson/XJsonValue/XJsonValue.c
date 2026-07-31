#include "XJsonValue.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XAlgorithm.h"
#include "XMap.h"
#include <math.h>
#include <string.h>

static XJsonValue* XJsonValue_create_with_type(XJsonValueType type)
{
    XJsonValue* value = (XJsonValue*)XMalloc_System(sizeof(XJsonValue));
    if (value)
        XJsonValue_init(value, type);
    return value;
}

XJsonValue* XJsonValue_create_null(void)
{
    return XJsonValue_create_with_type(XJsonValue_Null);
}

XJsonValue* XJsonValue_create_undefined(void)
{
    return XJsonValue_create_with_type(XJsonValue_Undefined);
}

XJsonValue* XJsonValue_create_bool(bool value)
{
    XJsonValue* val = XJsonValue_create_with_type(XJsonValue_Bool);
    if (val) 
    {
        val->data.boolean = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_double(double value) 
{
    XJsonValue* val = XJsonValue_create_with_type(XJsonValue_Double);
    if (val)
    {
        val->data.number = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_int(int64_t value)
{
    XJsonValue* val = XJsonValue_create_with_type(XJsonValue_Int);
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
    XJsonValue* val = XJsonValue_create_with_type(XJsonValue_String);
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
    XJsonValue* val = XJsonValue_create_with_type(XJsonValue_Array);
    if (val)
    {
        val->data.array = XJsonArray_create_copy(array);
    }
    return val;
}

XJsonValue* XJsonValue_create_object(XJsonObject* object) 
{
    if (object == NULL)
        return NULL;
    XJsonValue* val = XJsonValue_create_with_type(XJsonValue_Object);
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
    if (var == NULL || src == NULL || var == src)
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
    if (var == src) return;
    XJsonValue_deinit(var);
    memcpy(var, src, sizeof(XJsonValue));
    src->type = XJsonValue_Null;
    memset(&src->data, 0, sizeof(src->data));
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
    XFree_System(value);
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

bool XJsonValue_isUndefined(const XJsonValue* value)
{
    return XJsonValue_type(value) == XJsonValue_Undefined;
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
    if (!value)
        return defaultValue;
    if (value->type == XJsonValue_Double)
        return value->data.number;
    if (value->type == XJsonValue_Int)
        return (double)value->data.integer;
    return defaultValue;
}

int64_t XJsonValue_toInt(const XJsonValue* value, int64_t defaultValue)
{
    if (!value)
        return defaultValue;
    if (value->type == XJsonValue_Int)
        return value->data.integer;
    if (value->type == XJsonValue_Double && isfinite(value->data.number) &&
        value->data.number >= (double)INT64_MIN && value->data.number <= (double)INT64_MAX) {
        int64_t integer = (int64_t)value->data.number;
        if ((double)integer == value->data.number)
            return integer;
    }
    return defaultValue;
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

bool XJsonValue_equals(const XJsonValue* left, const XJsonValue* right)
{
    if (left == right)
        return true;
    if (!left || !right)
        return false;
    if ((left->type == XJsonValue_Int || left->type == XJsonValue_Double) &&
        (right->type == XJsonValue_Int || right->type == XJsonValue_Double))
        return XJsonValue_toDouble(left, 0.0) == XJsonValue_toDouble(right, 0.0);
    if (left->type != right->type)
        return false;
    switch (left->type) {
    case XJsonValue_Invalid:
    case XJsonValue_Undefined:
    case XJsonValue_Null:
        return true;
    case XJsonValue_Bool:
        return left->data.boolean == right->data.boolean;
    case XJsonValue_String:
        return left->data.string && right->data.string &&
            XString_equals(left->data.string, right->data.string, XChar_CaseSensitive);
    case XJsonValue_Array:
        return XJsonArray_equals(left->data.array, right->data.array);
    case XJsonValue_Object:
        return XJsonObject_equals(left->data.object, right->data.object);
    default:
        return false;
    }
}

void XJsonValue_setNull(XJsonValue* value)
{
    if (!value) return;
    XJsonValue_deinit(value);
    value->type = XJsonValue_Null;
}

void XJsonValue_setUndefined(XJsonValue* value)
{
    if (!value)
        return;
    XJsonValue_deinit(value);
    value->type = XJsonValue_Undefined;
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

void XJsonValue_setString_move(XJsonValue* value, XString* s)
{
    if (!value || !s) return;

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
        return XVariant_create_null();
    switch (val->type) {
    case XJsonValue_Bool:
        return XVariant_create_bool(val->data.boolean);
    case XJsonValue_Double:
        return XVariant_create_double(val->data.number);
    case XJsonValue_Int:
        return XVariant_create_int64(val->data.integer);
    case XJsonValue_String:
        return val->data.string ? XVariant_create_String(val->data.string) : XVariant_create_null();
    case XJsonValue_Array:
    {
        XVariantList* list = XJsonArray_toVariantList(val->data.array);
        XVariant* variant = list ? XVariant_create_list(list) : XVariant_create_null();
        if (list) XVariantList_delete_base(list);
        return variant;
    }
    case XJsonValue_Object:
    {
        XVariantMap* map = XJsonObject_toVariantMap(val->data.object);
        XVariant* variant = map ? XVariant_create_map(map) : XVariant_create_null();
        if (map) XMap_delete_base(map);
        return variant;
    }
    default:
        return XVariant_create_null();
    }
}

XVariant* XJsonValue_toVariant_move(XJsonValue* val)
{
    XVariant* variant = XJsonValue_toVariant(val);
    if (val)
        XJsonValue_setNull(val);
    return variant;
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
    if (!variant)
        return XJsonValue_create_null();
    switch (variant->m_type) {
    case XVariantType_NULL:
        return XJsonValue_create_null();
    case XVariantType_Bool:
        return XJsonValue_create_bool(XVariant_toBool(variant));
    case XVariantType_Uint8: return XJsonValue_create_int(*(uint8_t*)variant->m_data);
    case XVariantType_Uint16: return XJsonValue_create_int(*(uint16_t*)variant->m_data);
    case XVariantType_Uint32: return XJsonValue_create_int(*(uint32_t*)variant->m_data);
    case XVariantType_Int8: return XJsonValue_create_int(*(int8_t*)variant->m_data);
    case XVariantType_Int16: return XJsonValue_create_int(*(int16_t*)variant->m_data);
    case XVariantType_Int32: return XJsonValue_create_int(*(int32_t*)variant->m_data);
    case XVariantType_Int64: return XJsonValue_create_int(*(int64_t*)variant->m_data);
    case XVariantType_Int: return XJsonValue_create_int(*(int*)variant->m_data);
    case XVariantType_Size_t:
        return *(size_t*)variant->m_data <= INT64_MAX ?
            XJsonValue_create_int((int64_t)*(size_t*)variant->m_data) :
            XJsonValue_create_double((double)*(size_t*)variant->m_data);
    case XVariantType_Uint64:
        return *(uint64_t*)variant->m_data <= INT64_MAX ?
            XJsonValue_create_int((int64_t)*(uint64_t*)variant->m_data) :
            XJsonValue_create_double((double)*(uint64_t*)variant->m_data);
    case XVariantType_Float:
    case XVariantType_Double:
        return XJsonValue_create_double(XVariant_toDouble(variant));
    case XVariantType_String:
        return XJsonValue_create_string(XVariant_toString_const(variant));
    case XVariantType_List:
    {
        XJsonArray* array = XJsonArray_fromVariantList(XVariant_toList_ref(variant));
        XJsonValue* value = array ? XJsonValue_create_array(array) : XJsonValue_create_null();
        if (array) XJsonArray_delete_base(array);
        return value;
    }
    case XVariantType_Map:
    {
        XJsonObject* object = XJsonObject_fromVariantMap(XVariant_toMap_ref(variant));
        XJsonValue* value = object ? XJsonValue_create_object(object) : XJsonValue_create_null();
        if (object) XJsonObject_delete_base(object);
        return value;
    }
    case XVariantType_JsonValue:
        return XVariant_toJsonValue(variant);
    case XVariantType_JsonArray:
    {
        XJsonArray* array = XVariant_toJsonArray_ref(variant);
        return array ? XJsonValue_create_array(array) : XJsonValue_create_null();
    }
    case XVariantType_JsonObject:
    {
        XJsonObject* object = XVariant_toJsonObject_ref(variant);
        return object ? XJsonValue_create_object(object) : XJsonValue_create_null();
    }
    default:
        return XJsonValue_create_null();
    }
}
