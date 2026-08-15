#include "XJsonDocument.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XStringList.h"
#include "XHashMap.h"
#include "XMap.h"
#include "XString.h"
#include "XByteArray.h"
#include "XClass.h"
#include <assert.h>
#include <string.h>

static void json_delete_variant(XVariant* variant)
{
    if (variant)
        XVariant_delete_base((XClass*)variant);
}

static void json_delete_ref_variant(XVariant* variant)
{
    /* *_toVariant_ref() is non-owning; detach before deleting the wrapper. */
    if (variant)
    {
        variant->m_data = NULL;
        XVariant_delete_base((XClass*)variant);
    }
}

static void json_assert_string(const XString* value, const char* expected)
{
    assert(value != NULL);
    assert(XString_equals_utf8(value, expected, XChar_CaseSensitive));
}

static void json_assert_parse_error(const char* data, size_t length, XJsonParseErrorCode expected)
{
    XByteArray* input = XByteArray_create_with_data(data, length);
    XJsonParseError error;
    XJsonDocument* document;
    assert(input != NULL);
    document = XJsonDocument_fromJson_ex(input, &error);
    assert(document == NULL);
    assert(error.error == expected);
    assert(error.offset >= 0);
    XByteArray_delete_base((XClass*)input);
}

static void test_json_parse_errors(void)
{
    static const char invalid_utf8[] = "{\"a\":\"\xc3\x28\"}";
    XByteArray* deeply_nested = XByteArray_create();
    XJsonParseError error;
    XJsonDocument* document;
    size_t index;

    json_assert_parse_error("{", 1, XJsonParseError_UnterminatedObject);
    json_assert_parse_error("{\"a\" 1}", 7, XJsonParseError_MissingNameSeparator);
    json_assert_parse_error("[", 1, XJsonParseError_UnterminatedArray);
    json_assert_parse_error("[1 2]", 5, XJsonParseError_MissingValueSeparator);
    json_assert_parse_error("x", 1, XJsonParseError_IllegalValue);
    json_assert_parse_error("[1", 2, XJsonParseError_TerminationByNumber);
    json_assert_parse_error("{\"a\":1e}", 8, XJsonParseError_IllegalNumber);
    json_assert_parse_error("{\"a\":\"\\q\"}", 10, XJsonParseError_IllegalEscapeSequence);
    json_assert_parse_error(invalid_utf8, sizeof(invalid_utf8) - 1, XJsonParseError_IllegalUtf8String);
    json_assert_parse_error("{\"a\":\"text", 10, XJsonParseError_UnterminatedString);
    json_assert_parse_error("[1,]", 4, XJsonParseError_MissingObject);
    json_assert_parse_error("{}x", 3, XJsonParseError_GarbageAtEnd);

    assert(deeply_nested != NULL);
    for (index = 0; index < 1025; ++index)
        assert(XByteArray_push_back_1(deeply_nested, '['));
    for (index = 0; index < 1025; ++index)
        assert(XByteArray_push_back_1(deeply_nested, ']'));
    document = XJsonDocument_fromJson_ex(deeply_nested, &error);
    assert(document == NULL && error.error == XJsonParseError_DeepNesting);
    XByteArray_delete_base((XClass*)deeply_nested);
}

static void test_json_value_api(void)
{
    XJsonValue* null_value = XJsonValue_create_null();
    XJsonValue* undefined = XJsonValue_create_undefined();
    XJsonValue* boolean = XJsonValue_create_bool(true);
    XJsonValue* number = XJsonValue_create_double(2.5);
    XJsonValue* integer = XJsonValue_create_int(42);
    XString* text = XString_create_utf8("value");
    XJsonValue* string = XJsonValue_create_string(text);
    XJsonArray* array = XJsonArray_create();
    XJsonObject* object = XJsonObject_create();
    XJsonValue local;
    XJsonValue* copy;
    XJsonValue* moved;
    XVariant* variant;
    XJsonValue* from_variant;
    XString* moved_text;

    assert(null_value && XJsonValue_isNull(null_value));
    assert(undefined && XJsonValue_isUndefined(undefined));
    assert(boolean && XJsonValue_isBool(boolean) && XJsonValue_toBool(boolean, false));
    assert(number && XJsonValue_isDouble(number) && XJsonValue_toDouble(number, 0) == 2.5);
    assert(integer && XJsonValue_isInt(integer) && XJsonValue_toInt(integer, 0) == 42);
    assert(text && string && XJsonValue_isString(string));
    json_assert_string(XJsonValue_toString(string), "value");
    assert(array && object);

    copy = XJsonValue_create_copy(integer);
    assert(copy && XJsonValue_equals(copy, integer));
    moved = XJsonValue_create_move(copy);
    assert(moved && XJsonValue_isInt(moved) && XJsonValue_isNull(copy));
    XJsonValue_delete(copy);
    XJsonValue_init(&local, XJsonValue_Invalid);
    XJsonValue_copy(&local, moved);
    assert(XJsonValue_equals(&local, moved));
    XJsonValue_move(&local, moved);
    assert(XJsonValue_isInt(&local) && XJsonValue_isNull(moved));
    XJsonValue_setBool(&local, false);
    assert(!XJsonValue_toBool(&local, true));
    XJsonValue_setDouble(&local, 3.0);
    assert(XJsonValue_toInt(&local, 0) == 3);
    XJsonValue_setInt(&local, 7);
    XJsonValue_setNull(&local);
    assert(XJsonValue_isNull(&local));
    XJsonValue_setUndefined(&local);
    assert(XJsonValue_isUndefined(&local));
    XJsonValue_setString_utf8(&local, "utf8");
    json_assert_string(XJsonValue_toString(&local), "utf8");
    moved_text = XString_create_utf8("moved");
    XJsonValue_setString_move(&local, moved_text);
    json_assert_string(XJsonValue_toString(&local), "moved");
    XString_delete_base((XClass*)moved_text);
    XJsonValue_clear(&local);
    assert(XJsonValue_isString(&local) && XString_isEmpty_base(local.data.string));

    XJsonArray_append_base(array, integer);
    XJsonValue_setArray(&local, array);
    assert(XJsonValue_isArray(&local) && XJsonValue_toArray(&local) != array);
    XJsonValue_setArray_move(&local, array);
    assert(XJsonValue_isArray(&local));
    XJsonObject_insert_keyUtf8_int(object, "n", 1);
    XJsonValue_setObject(&local, object);
    assert(XJsonValue_isObject(&local) && XJsonValue_toObject(&local) != object);
    XJsonValue_setObject_move(&local, object);
    assert(XJsonValue_isObject(&local));

    variant = XJsonValue_toVariant(&local);
    assert(variant != NULL);
    from_variant = XJsonValue_fromVariant(variant);
    assert(from_variant && XJsonValue_equals(from_variant, &local));
    XJsonValue_delete(from_variant);
    json_delete_variant(variant);
    variant = XJsonValue_toVariant_move(&local);
    assert(variant && XJsonValue_isNull(&local));
    json_delete_variant(variant);

    XJsonValue_setInt(&local, 9);
    variant = XJsonValue_toVariant_ref(&local);
    assert(variant && variant->m_type == XVariantType_JsonValue && variant->m_data == &local);
    json_delete_ref_variant(variant);
    XJsonValue_deinit(&local);
    XJsonValue_delete(null_value);
    XJsonValue_delete(undefined);
    XJsonValue_delete(boolean);
    XJsonValue_delete(number);
    XJsonValue_delete(integer);
    XJsonValue_delete(string);
    XString_delete_base((XClass*)text);
    XJsonArray_delete_base((XClass*)array);
    XJsonObject_delete_base((XClass*)object);
    XJsonValue_delete(moved);
}

static void test_json_array_api(void)
{
    XJsonArray* array = XJsonArray_create();
    XJsonArray* copy;
    XJsonArray* moved;
    XJsonArray* from_strings;
    XJsonArray* from_variants;
    XJsonArray* moved_list_source;
    XStringList* strings = XStringList_create();
    XVariantList* variants = XVariantList_create();
    XVariantList* converted;
    XVariant* variant;
    XJsonValue value;
    XJsonValue* item;
    XJsonValue move_value;
    XString* string;
    XVariant* number;
    XString* serialized;

    assert(array && XJsonArray_isEmpty_base(array));
    assert(XJsonArray_typeSize_base(array) == sizeof(XJsonValue));
    XJsonValue_init(&value, XJsonValue_Int);
    value.data.integer = 1;
    assert(XJsonArray_append_base(array, &value));
    value.data.integer = 2;
    assert(XJsonArray_prepend_base(array, &value));
    value.data.integer = 3;
    assert(XJsonArray_insert(array, 1, &value));
    value.data.integer = 4;
    assert(XJsonArray_replace(array, 0, &value));
    value.data.integer = 5;
    assert(XJsonArray_append_move_base(array, &value));
    XJsonValue_deinit(&value);
    XJsonValue_init(&move_value, XJsonValue_Bool);
    move_value.data.boolean = true;
    assert(XJsonArray_prepend_move_base(array, &move_value));
    XJsonValue_deinit(&move_value);
    XJsonValue_init(&move_value, XJsonValue_Int);
    move_value.data.integer = 6;
    assert(XJsonArray_insert_move(array, 0, &move_value));
    XJsonValue_deinit(&move_value);
    XJsonValue_init(&move_value, XJsonValue_Int);
    move_value.data.integer = 7;
    assert(XJsonArray_replace_move(array, 0, &move_value));
    XJsonValue_deinit(&move_value);
    assert(XJsonArray_size_base(array) == 6 && XJsonArray_count_base(array) == 6);
    assert(XJsonArray_at(array, 0) && XJsonArray_at_const(array, -1));
    item = XJsonArray_first(array);
    assert(item && XJsonValue_isInt(item));
    XJsonValue_delete(item);
    item = XJsonArray_last(array);
    assert(item && XJsonValue_isInt(item));
    XJsonValue_delete(item);
    item = XJsonArray_takeAt(array, 1);
    assert(item && XJsonValue_isBool(item));
    XJsonValue_delete(item);
    item = XJsonArray_takeAt(array, -1);
    assert(item && XJsonValue_isInt(item));
    XJsonValue_delete(item);
    XJsonArray_removeAt_base(array, 0);
    assert(XJsonArray_contains(array, XJsonArray_at_const(array, 0)));
    copy = XJsonArray_create_copy(array);
    assert(copy && XJsonArray_equals(array, copy));
    moved = XJsonArray_create_move(copy);
    assert(moved && XJsonArray_equals(array, moved));

    string = XString_create_utf8("one");
    XStringList_push_back_base(strings, string);
    XString_delete_base((XClass*)string);
    string = XString_create_utf8("two");
    XStringList_push_back_base(strings, string);
    XString_delete_base((XClass*)string);
    from_strings = XJsonArray_fromStringList(strings);
    assert(from_strings && XJsonArray_size_base(from_strings) == 2);
    number = XVariant_create_int(8);
    XVariantList_push_back_move_base(variants, number);
    json_delete_variant(number);
    from_variants = XJsonArray_fromVariantList(variants);
    assert(from_variants && XJsonArray_size_base(from_variants) == 1);
    moved_list_source = XJsonArray_create_copy(from_variants);
    assert(moved_list_source && XVector_isSharedWith((XVector*)from_variants, (XVector*)moved_list_source));
    converted = XJsonArray_toVariantList(from_variants);
    assert(converted && XVariantList_size_base(converted) == 1 &&
        XVector_isSharedWith((XVector*)from_variants, (XVector*)moved_list_source));
    XVariantList_delete_base(converted);
    converted = XJsonArray_toVariantList_move(moved_list_source);
    item = XJsonArray_at_const(from_variants, 0);
    assert(converted && item && XJsonValue_isInt(item) && XJsonValue_toInt(item, 0) == 8);
    XVariantList_delete_base(converted);
    XJsonArray_delete_base((XClass*)moved_list_source);
    variant = XJsonArray_toVariant(array);
    assert(variant && variant->m_type == XVariantType_JsonArray);
    json_delete_variant(variant);
    variant = XJsonArray_toVariant_move(moved);
    assert(variant && XJsonArray_isEmpty_base(moved));
    json_delete_variant(variant);
    variant = XJsonArray_toVariant_ref(array);
    assert(variant && variant->m_data == array);
    json_delete_ref_variant(variant);
    serialized = XJsonArray_toString(array, XJsonDocument_Compact);
    assert(serialized != NULL);
    XString_delete_base((XClass*)serialized);

    XStringList_delete_base((XClass*)strings);
    XVariantList_delete_base((XClass*)variants);
    XJsonArray_delete_base((XClass*)from_strings);
    XJsonArray_delete_base((XClass*)from_variants);
    XJsonArray_delete_base((XClass*)moved);
    XJsonArray_delete_base((XClass*)copy);
    XJsonArray_delete_base((XClass*)array);
}

static void test_json_object_api(void)
{
    XJsonObject* object = XJsonObject_create();
    XJsonObject* copy;
    XJsonObject* moved;
    XJsonObject* nested;
    XJsonArray* array = XJsonArray_create();
    XString* text = XString_create_utf8("text");
    XString* key;
    XJsonValue* value;
    XJsonValue* taken;
    XVariantMap* map;
    XVariantMap* moved_map;
    XVariantHashMap* hash;
    XVariant* variant;
    XString* serialized;
    XByteArray* json;
    XVector* keys;

    assert(object && XJsonObject_isEmpty_base(object));
    assert(XJsonObject_insert_keyUtf8_double(object, "double", 1.5));
    assert(XJsonObject_insert_keyUtf8_int(object, "int", 2));
    assert(XJsonObject_insert_keyUtf8_string(object, "copy", text));
    assert(XJsonObject_insert_keyUtf8_utf8(object, "utf8", "hello"));
    assert(XJsonObject_insert_keyUtf8_null(object, "null"));
    assert(XJsonObject_insert_keyUtf8_bool(object, "bool", true));
    value = XJsonValue_create_int(7);
    assert(XJsonObject_insert_keyUtf8_value(object, "value", value));
    XJsonValue_delete(value);
    value = XJsonValue_create_int(8);
    assert(XJsonObject_insert_keyUtf8_value_move(object, "value_move", value));
    XJsonValue_delete(value);
    value = XJsonValue_create_int(3);
    assert(value && XJsonArray_append_base(array, value));
    XJsonValue_delete(value);
    assert(XJsonObject_insert_keyUtf8_array(object, "array", array));
    assert(XJsonObject_insert_keyUtf8_array_move(object, "array_move", array));
    XJsonArray_delete_base((XClass*)array);
    nested = XJsonObject_create();
    assert(nested && XJsonObject_insert_keyUtf8_int(nested, "nested", 1));
    assert(XJsonObject_insert_keyUtf8_object(object, "object", nested));
    assert(XJsonObject_insert_keyUtf8_object_move(object, "object_move", nested));
    XJsonObject_delete_base((XClass*)nested);
    key = XString_create_utf8("key_move");
    value = XJsonValue_create_bool(false);
    assert(XJsonObject_insert_value_move(object, key, value));
    XString_delete_base((XClass*)key);
    XJsonValue_delete(value);
    assert(XJsonObject_size_base(object) == 13);

    value = XJsonObject_value_keyUtf8(object, "utf8");
    assert(value && XJsonValue_isString(value));
    XJsonValue_delete(value);
    assert(XJsonObject_contains_keyUtf8(object, "int"));
    taken = XJsonObject_take_keyUtf8(object, "value");
    assert(taken && XJsonValue_isInt(taken));
    XJsonValue_delete(taken);
    assert(XJsonObject_remove_keyUtf8(object, "null"));
    assert(!XJsonObject_contains_keyUtf8(object, "null"));
    copy = XJsonObject_create_copy(object);
    assert(copy && XJsonObject_equals(object, copy));
    moved = XJsonObject_create_move(copy);
    assert(moved && XJsonObject_equals(object, moved));
    keys = XJsonObject_keys_base(object);
    assert(keys && XVector_size_base(keys) == XJsonObject_size_base(object));
    XVector_delete_base(keys);

    map = XJsonObject_toVariantMap(object);
    hash = XJsonObject_toVariantHash(object);
    assert(map && hash);
    nested = XJsonObject_create_copy(object);
    moved_map = XJsonObject_toVariantMap_move(nested);
    assert(moved_map != NULL);
    XMap_delete_base((XClass*)moved_map);
    XJsonObject_delete_base((XClass*)nested);
    nested = XJsonObject_fromVariantMap(map);
    assert(nested && XJsonObject_equals(object, nested));
    XJsonObject_delete_base((XClass*)nested);
    nested = XJsonObject_fromVariantHash(hash);
    assert(nested && XJsonObject_equals(object, nested));
    XJsonObject_delete_base((XClass*)nested);
    XMap_delete_base((XClass*)map);
    XHashMap_delete_base((XClass*)hash);
    variant = XJsonObject_toVariant(object);
    assert(variant && variant->m_type == XVariantType_JsonObject);
    json_delete_variant(variant);
    variant = XJsonObject_toVariant_move(moved);
    assert(variant && XJsonObject_isEmpty_base(moved));
    json_delete_variant(variant);
    variant = XJsonObject_toVariant_ref(object);
    assert(variant && variant->m_data == object);
    json_delete_ref_variant(variant);
    serialized = XJsonObject_toString(object, XJsonDocument_Compact);
    json = XJsonObject_toJson(object, XJsonDocument_Compact);
    assert(serialized && json);
    XString_delete_base((XClass*)serialized);
    XByteArray_delete_base((XClass*)json);
    XString_delete_base((XClass*)text);
    XJsonObject_delete_base((XClass*)moved);
    XJsonObject_delete_base((XClass*)copy);
    XJsonObject_delete_base((XClass*)object);
}

static void test_json_document_api(void)
{
    const char* source = "{\"a\":1,\"u\":\"\\u0061\",\"list\":[true,null]}";
    XByteArray* input = XByteArray_create_with_data(source, strlen(source));
    XByteArray* unshared_input = XByteArray_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, false);
    XJsonParseError error;
    XJsonDocument* document = XJsonDocument_fromJson_ex(input, &error);
    XJsonDocument* unshared_document;
    XJsonDocument* copy;
    XJsonDocument* moved;
    XJsonDocument* from_variant;
    XJsonObject* object;
    XJsonObject* object_copy;
    XJsonArray* array;
    XJsonArray* array_copy;
    XJsonValue root;
    XJsonDocument local_document;
    XJsonValue* value;
    XString* text;
    XByteArray* output;
    XVariant* variant;
    XByteArray* bson;
    XJsonDocument* bson_document;
    XJsonDocument* object_document;
    XJsonDocument* object_document_move;
    XJsonObject* bson_object;
    XByteArray* object_bson;
    XString* serialized_text;

    assert(document && error.error == XJsonParseError_NoError);
    assert(unshared_input && XByteArray_append_utf8(unshared_input, "[1,2,3]"));
    assert(XByteArray_data(unshared_input) != NULL);
    unshared_document = XJsonDocument_fromJson_ex(unshared_input, &error);
    assert(unshared_document && error.error == XJsonParseError_NoError &&
        XJsonDocument_isArray(unshared_document));
    XJsonDocument_delete(unshared_document);
    XByteArray_delete_base((XClass*)unshared_input);
    assert(XJsonDocument_isObject(document) && !XJsonDocument_isArray(document));
    assert(XJsonDocument_root(document) == &document->root);
    assert(XJsonDocument_root_const(document) == &document->root);
    object = XJsonDocument_object(document);
    assert(object && XJsonDocument_array(document) == NULL);
    value = XJsonObject_value_keyUtf8(object, "a");
    assert(value && XJsonValue_toInt(value, 0) == 1);
    XJsonValue_delete(value);
    copy = XJsonDocument_create_copy(document);
    assert(copy && XJsonObject_equals(XJsonDocument_object(copy), object));
    moved = XJsonDocument_create_move(copy);
    assert(moved && XJsonDocument_isObject(moved));
    XJsonValue_init(&root, XJsonValue_Int);
    root.data.integer = 99;
    XJsonDocument_setRoot(document, &root);
    assert(XJsonDocument_root(document)->data.integer == 99);
    XJsonDocument_setRoot_move(document, &root);
    assert(XJsonValue_isInt(XJsonDocument_root(document)));
    XJsonValue_deinit(&root);

    object_copy = XJsonObject_create();
    assert(object_copy && XJsonObject_insert_keyUtf8_int(object_copy, "object", 1));
    assert(XJsonDocument_setObject(document, object_copy));
    assert(XJsonDocument_isObject(document));
    assert(XJsonDocument_setObject_move(document, object_copy));
    XJsonObject_delete_base((XClass*)object_copy);
    array = XJsonArray_create();
    value = XJsonValue_create_bool(true);
    assert(array && value && XJsonArray_append_move_base(array, value));
    XJsonValue_delete(value);
    array_copy = XJsonArray_create_copy(array);
    assert(array_copy && XJsonDocument_setArray(document, array_copy));
    assert(XJsonDocument_isArray(document));
    assert(XJsonDocument_setArray_move(document, array_copy));
    XJsonArray_delete_base((XClass*)array_copy);

    bson_object = XJsonObject_create();
    assert(bson_object && XJsonObject_insert_keyUtf8_int(bson_object, "object", 1));
    object_document = XJsonDocument_create_object(bson_object);
    assert(object_document && XJsonDocument_isObject(object_document));
    object_copy = XJsonObject_create_copy(bson_object);
    object_document_move = XJsonDocument_create_object_move(object_copy);
    assert(object_document_move && XJsonDocument_isObject(object_document_move));
    XJsonObject_delete_base((XClass*)object_copy);
    XJsonObject_delete_base((XClass*)bson_object);
    XJsonDocument_delete(object_document);
    XJsonDocument_delete(object_document_move);

    text = XString_create_utf8("[1,2,3]");
    value = NULL;
    XJsonDocument_delete(document);
    document = XJsonDocument_fromString_ex(text, &error);
    assert(document && error.error == XJsonParseError_NoError && XJsonDocument_isArray(document));
    output = XJsonDocument_toJson(document, XJsonDocument_Compact);
    assert(output && XByteArray_size_base((XContainer*)output) == 7);
    XByteArray_delete_base((XClass*)output);
    output = XJsonDocument_toJson(document, XJsonDocument_Indented);
    assert(output && XByteArray_size_base((XContainer*)output) > 7);
    XByteArray_delete_base((XClass*)output);
    serialized_text = XJsonDocument_toString(document, XJsonDocument_Compact);
    assert(serialized_text != NULL);
    XString_delete_base((XClass*)serialized_text);
    XString_delete_base((XClass*)text);
    XJsonDocument_delete(moved);
    text = XString_create_utf8("[1,2,3]");
    moved = XJsonDocument_fromString(text);
    assert(moved && XJsonDocument_isArray(moved));
    XString_delete_base((XClass*)text);
    XJsonDocument_delete(moved);

    XByteArray_delete_base((XClass*)input);
    input = XByteArray_create_with_data("[1,]", 4);
    moved = XJsonDocument_fromJson_ex(input, &error);
    assert(moved == NULL && error.error != XJsonParseError_NoError && error.offset >= 0);
    assert(strlen(XJsonParseError_errorString(&error)) > 0);
    XByteArray_delete_base((XClass*)input);
    XJsonParseError_init(&error);
    assert(error.offset == -1 && error.error == XJsonParseError_NoError);
    for (int code = XJsonParseError_NoError; code <= XJsonParseError_GarbageAtEnd; ++code)
    {
        error.error = (XJsonParseErrorCode)code;
        assert(strlen(XJsonParseError_errorString(&error)) > 0);
    }

    variant = XJsonDocument_toVariant(document);
    assert(variant);
    from_variant = XJsonDocument_fromVariant(variant);
    assert(from_variant && XJsonValue_equals(XJsonDocument_root(document),
                                             XJsonDocument_root(from_variant)));
    XJsonDocument_delete(from_variant);
    json_delete_variant(variant);
    variant = XJsonDocument_toVariant_move(document);
    assert(variant && XJsonDocument_isEmpty(document));
    json_delete_variant(variant);
    XJsonDocument_setArray(document, array);
    XJsonDocument_init(&local_document);
    XJsonDocument_copy(&local_document, document);
    assert(XJsonDocument_isArray(&local_document));
    XJsonDocument_move(&local_document, document);
    assert(XJsonDocument_isArray(&local_document) && XJsonDocument_isEmpty(document));
    XJsonDocument_move(document, &local_document);
    variant = XJsonDocument_toVariant_ref(document);
    assert(variant && variant->m_data == document);
    json_delete_ref_variant(variant);
    bson = XJsonDocument_toBson(document);
    assert(bson != NULL);
    bson_document = XJsonDocument_fromBson_array(bson);
    assert(bson_document && XJsonDocument_isArray(bson_document));
    XJsonDocument_delete(bson_document);
    XByteArray_delete_base((XClass*)bson);
    bson_object = XJsonObject_create();
    assert(bson_object && XJsonObject_insert_keyUtf8_int(bson_object, "bson", 1));
    object_document = XJsonDocument_create_object(bson_object);
    object_bson = XJsonDocument_toBson(object_document);
    assert(object_bson != NULL);
    bson_document = XJsonDocument_fromBson_document(object_bson);
    assert(bson_document && XJsonDocument_isObject(bson_document));
    XJsonDocument_delete(bson_document);
    XByteArray_delete_base((XClass*)object_bson);
    XJsonDocument_delete(object_document);
    XJsonObject_delete_base((XClass*)bson_object);
    XJsonDocument_clear(document);
    assert(XJsonDocument_isNull(document) && XJsonDocument_isEmpty(document));
    XJsonDocument_deinit(document);
    XJsonDocument_delete(document);
    XJsonDocument_delete(copy);
    XJsonArray_delete_base((XClass*)array);
}

int XJsonQtAlignmentTest(void)
{
    test_json_value_api();
    test_json_array_api();
    test_json_object_api();
    test_json_parse_errors();
    test_json_document_api();
    return 0;
}
