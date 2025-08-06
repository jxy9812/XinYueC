#include "XJsonValue.h"
#include "XJsonArray.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XAlgorithm.h"
#include "XMap.h"
#include <string.h>
XJsonValue* XJsonValue_create_null(void) {
    XJsonValue* value = (XJsonValue*)XMemory_malloc(sizeof(XJsonValue));
    if (value) {
        value->type = XJsonValue_Null;
        memset(&value->data, 0, sizeof(value->data));
    }
    return value;
}

XJsonValue* XJsonValue_create_bool(bool value) {
    XJsonValue* val = XJsonValue_create_null();
    if (val) {
        val->type = XJsonValue_Bool;
        val->data.boolean = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_double(double value) {
    XJsonValue* val = XJsonValue_create_null();
    if (val) {
        val->type = XJsonValue_Double;
        val->data.number = value;
    }
    return val;
}

XJsonValue* XJsonValue_create_string(const XString* string) {
    XJsonValue* val = XJsonValue_create_null();
    if (val && string) {
        val->type = XJsonValue_String;
        val->data.string = XString_create(string);
    }
    return val;
}

XJsonValue* XJsonValue_create_array(XJsonArray* array) {
    XJsonValue* val = XJsonValue_create_null();
    if (val && array) {
        val->type = XJsonValue_Array;
        val->data.array = array;
    }
    return val;
}

XJsonValue* XJsonValue_create_object(XJsonObject* object) {
    XJsonValue* val = XJsonValue_create_null();
    if (val && object) {
        val->type = XJsonValue_Object;
        val->data.object = object;
    }
    return val;
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
    case XJsonValue_String:var->data.string=XString_create(src->data.string); break;
    case XJsonValue_Array: var->data.array = XJsonArray_create(); XJsonArray_copy_base(var->data.array, src->data.array); break;
    case XJsonValue_Object: break;
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
            // XJsonObject_delete(value->data.object);
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

const XString* XJsonValue_toString(const XJsonValue* value) {
    return (value && value->type == XJsonValue_String) ? value->data.string : NULL;
}

XJsonArray* XJsonValue_toArray(const XJsonValue* value) {
    return (value && value->type == XJsonValue_Array) ? value->data.array : NULL;
}

XJsonObject* XJsonValue_toObject(const XJsonValue* value) {
    return (value && value->type == XJsonValue_Object) ? value->data.object : NULL;
}

void XJsonValue_setNull(XJsonValue* value) {
    if (!value) return;

    // 释放当前值
    XJsonValue temp = *value;
    *value = (XJsonValue){ .type = XJsonValue_Null };

    switch (temp.type) {
    case XJsonValue_String:
        XString_delete_base(temp.data.string);
        break;
  /*  case XJsonValue_Array:
        XJsonArray_delete(temp.data.array);
        break;
    case XJsonValue_Object:
        XJsonObject_delete(temp.data.object);
        break;*/
    default:
        break;
    }
}

void XJsonValue_setBool(XJsonValue* value, bool b) {
    if (!value) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_Bool;
    value->data.boolean = b;
}

void XJsonValue_setDouble(XJsonValue* value, double d) {
    if (!value) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_Double;
    value->data.number = d;
}

void XJsonValue_setString(XJsonValue* value, const XString* s) 
{
    if (!value) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_String;
    value->data.string = XString_create(s);
}

void XJsonValue_setString_move(XJsonValue* value, const XString* s)
{
    if (!value) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_String;
    value->data.string = s;
}

void XJsonValue_setString_utf8(XJsonValue* value, const char* utf8)
{
    if (!value) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_String;
    value->data.string = XString_create_utf8(utf8);
}

void XJsonValue_setArray(XJsonValue* value, XJsonArray* a)
{
    if (!value || !a) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_Array;
    value->data.array = XJsonArray_create();
    XJsonArray_copy_base(value->data.array,a);
}

void XJsonValue_setArray_move(XJsonValue* value, XJsonArray* a)
{
    if (!value || !a) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_Array;
    value->data.array = a;
}

void XJsonValue_setObject(XJsonValue* value, XJsonObject* o) {
    if (!value || !o) return;

    XJsonValue_setNull(value);
    value->type = XJsonValue_Object;
    value->data.object = o;
}

XVariant* XJsonValue_toVariant(const XJsonValue* value) 
{
   /* if (!value) return NULL;

    XVariant* variant = XVariant_create();
    if (!variant) return NULL;

    switch (value->type) 
    {
    case XJsonValue_Null:
        XVariant_setNull(variant);
        break;
    case XJsonValue_Bool:
        XVariant_setBool(variant, value->data.boolean);
        break;
    case XJsonValue_Double:
        XVariant_setDouble(variant, value->data.number);
        break;
    case XJsonValue_String:
        if (value->data.string) {
            XVariant_setString(variant, value->data.string);
        }
        break;
    case XJsonValue_Array:
        if (value->data.array) {
            XVariantList* list = XJsonArray_toVariantList(value->data.array);
            XVariant_setList(variant, list);
        }
        break;
    case XJsonValue_Object:
        if (value->data.object) {
            XVariantMap* map = XJsonObject_toVariantMap(value->data.object);
            XVariant_setMap(variant, map);
        }
        break;
    default:
        XVariant_delete(variant);
        return NULL;
    }

    return variant;*/
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
    //case XVariantType_MapBase:
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