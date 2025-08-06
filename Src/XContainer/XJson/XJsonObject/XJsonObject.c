//#include "XJsonObject.h"
//#include "XMemory.h"
//#include "XMap.h"
//
//XJsonObject* XJsonObject_create(void) {
//    XJsonObject* object = (XJsonObject*)XMemory_malloc(sizeof(XJsonObject));
//    if (object) {
//        // 假设XMap的键是XString*，值是XJsonValue*
//        object->members = XMap_create(sizeof(XString*), sizeof(XJsonValue*));
//        if (!object->members) {
//            XMemory_free(object);
//            return NULL;
//        }
//    }
//    return object;
//}
//
//void XJsonObject_delete(XJsonObject* object) 
//{
//    if (!object) return;
//
//    if (object->members) {
//        // 遍历并删除所有键值对
//        XMap_iterator it = XMap_begin(object->members);
//        while (!XMapIterator_isEnd(&it)) {
//            XString* key = *(XString**)XMapIterator_key(&it);
//            XJsonValue* value = *(XJsonValue**)XMapIterator_value(&it);
//
//            XString_delete_base(key);
//            XJsonValue_delete(value);
//
//            XMapIterator_next(&it);
//        }
//
//        XMap_delete_base(object->members);
//    }
//
//    XMemory_free(object);
//}
//
//int XJsonObject_size(const XJsonObject* object) {
//    return object && object->members ? XMap_size_base(object->members) : 0;
//}
//
//bool XJsonObject_isEmpty(const XJsonObject* object) {
//    return XJsonObject_size(object) == 0;
//}
//
//void XJsonObject_clear(XJsonObject* object) {
//    if (!object || !object->members) return;
//
//    // 先删除所有键值对
//    XMapIterator it = XMap_begin(object->members);
//    while (!XMapIterator_isEnd(&it)) {
//        XString* key = *(XString**)XMapIterator_key(&it);
//        XJsonValue* value = *(XJsonValue**)XMapIterator_value(&it);
//
//        XString_delete_base(key);
//        XJsonValue_delete(value);
//
//        XMapIterator_next(&it);
//    }
//
//    XMap_clear_base(object->members);
//}
//
//bool XJsonObject_contains(const XJsonObject* object, const XString* key) {
//    if (!object || !object->members || !key) return false;
//    return XMap_contains(object->members, &key);
//}
//
//XJsonValue* XJsonObject_value(XJsonObject* object, const XString* key) {
//    if (!object || !object->members || !key) return NULL;
//
//    XMapIterator it = XMap_find(object->members, &key);
//    if (XMapIterator_isEnd(&it)) {
//        return NULL;
//    }
//
//    return *(XJsonValue**)XMapIterator_value(&it);
//}
//
//const XJsonValue* XJsonObject_value_const(const XJsonObject* object, const XString* key) {
//    return XJsonObject_value((XJsonObject*)object, key);
//}
//
//bool XJsonObject_insert(XJsonObject* object, const XString* key, XJsonValue* value) {
//    if (!object || !object->members || !key || !value) return false;
//
//    // 先检查是否已存在该键，如果存在则删除旧值
//    XJsonValue* oldValue = XJsonObject_value(object, key);
//    if (oldValue) {
//        XJsonValue_delete(oldValue);
//
//        // 同时删除旧键
//        XMap_erase(object->members, &key);
//    }
//
//    // 复制键并插入新值
//    XString* keyCopy = XString_create(key);
//    if (!keyCopy) return false;
//
//    return XMap_insert(object->members, &keyCopy, &value);
//}
//
//bool XJsonObject_remove(XJsonObject* object, const XString* key) {
//    if (!object || !object->members || !key) return false;
//
//    XMapIterator it = XMap_find(object->members, &key);
//    if (XMapIterator_isEnd(&it)) {
//        return false;
//    }
//
//    // 删除键和值
//    XString* keyToDelete = *(XString**)XMapIterator_key(&it);
//    XJsonValue* valueToDelete = *(XJsonValue**)XMapIterator_value(&it);
//
//    XString_delete_base(keyToDelete);
//    XJsonValue_delete(valueToDelete);
//
//    return XMap_erase(object->members, &key);
//}
//
//void XJsonObject_removeAll(XJsonObject* object) {
//    XJsonObject_clear(object);
//}
//
//XVector* XJsonObject_keys(const XJsonObject* object) {
//    if (!object || !object->members) return NULL;
//
//    XVector* keys = XVector_create(sizeof(XString*));
//    if (!keys) return NULL;
//
//    XMapIterator it = XMap_begin(object->members);
//    while (!XMapIterator_isEnd(&it)) {
//        XString* key = *(XString**)XMapIterator_key(&it);
//        XString* keyCopy = XString_create(key);
//        if (keyCopy) {
//            XVector_push_back_base(keys, &keyCopy);
//        }
//
//        XMapIterator_next(&it);
//    }
//
//    return keys;
//}
//
//XString* XJsonObject_toString(const XJsonObject* object) {
//    // 实际实现需要序列化对象为JSON字符串
//    // 这里仅作为框架示例
//    XString_Init_Utf8(result, "{...json object...}");
//    return XString_create(result);
//}
//
//XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object) {
//    if (!object) return NULL;
//
//    XVariantMap* map = XVariantMap_create();
//    if (!map) return NULL;
//
//    XMapIterator it = XMap_begin(object->members);
//    while (!XMapIterator_isEnd(&it)) {
//        XString* key = *(XString**)XMapIterator_key(&it);
//        XJsonValue* jsonVal = *(XJsonValue**)XMapIterator_value(&it);
//
//        XVariant* var = XJsonValue_toVariant(jsonVal);
//        if (var) {
//            XVariantMap_insert(map, key, var);
//        }
//
//        XMapIterator_next(&it);
//    }
//
//    return map;
//}
//
//XJsonObject* XJsonObject_fromVariantMap(const XVariantMap* map) {
//    if (!map) return NULL;
//
//    XJsonObject* object = XJsonObject_create();
//    if (!object) return NULL;
//
//    XMapIterator it = XMap_begin((XMap*)map);
//    while (!XMapIterator_isEnd(&it)) {
//        XString* key = *(XString**)XMapIterator_key(&it);
//        XVariant* var = *(XVariant**)XMapIterator_value(&it);
//
//        XJsonValue* jsonVal = XJsonValue_fromVariant(var);
//        if (jsonVal) {
//            XJsonObject_insert(object, key, jsonVal);
//        }
//
//        XMapIterator_next(&it);
//    }
//
//    return object;
//}