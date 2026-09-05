#include "XPrintf.h"
#include "XDataStructTest.h"
#if DEMOTEST

#include "XAction.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XTestMenu.h"
#include "XString.h"
#include "XVariant.h"
#include "XVariantList.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct XBsonTestState
{
	int passed;
	int failed;
} XBsonTestState;

static XBsonTestState g_bsonTestState;

// 测试输出与断言辅助函数
static bool XBsonTest_check(bool condition, const char* description)
{
	if (condition) {
		++g_bsonTestState.passed;
		XPrintf("【通过】%s\n", description);
	} else {
		++g_bsonTestState.failed;
		XPrintf("【失败】%s\n", description);
	}
	return condition;
}

static void XBsonTest_begin(const char* group)
{
	XPrintf("\n========== %s ==========\n", group);
}

static void XBsonTest_summary(void)
{
	XPrintf("\n========== XBson 测试汇总 ==========\n");
	XPrintf("通过：%d 项，失败：%d 项\n",
		g_bsonTestState.passed, g_bsonTestState.failed);
}

static bool XBsonTest_bytesEqualData(const XByteArray* bytes,
		                                  const uint8_t* expected,
		                                  size_t expectedSize)
{
	return bytes && expected && XByteArray_size_base(bytes) == expectedSize &&
		memcmp(XContainerDataAddr(bytes), expected, expectedSize) == 0;
}

static bool XBsonTest_bytesEqual(const XByteArray* left,
		                              const XByteArray* right)
{
	return left && right && XByteArray_size_base(left) == XByteArray_size_base(right) &&
		memcmp(XContainerDataAddr(left), XContainerDataAddr(right),
			XByteArray_size_base(left)) == 0;
}

static bool XBsonTest_stringEquals(const XString* string, const char* utf8)
{
	return string && utf8 && strcmp(XString_toUtf8(string), utf8) == 0;
}

static void XBsonTest_printJsonObject(const char* title,
		                                  const XJsonObject* object)
{
	XString* text = object ? XJsonObject_toString(object, XJsonDocument_Indented) : NULL;
	XPrintf("【调试】%s：%s\n", title, text ? XString_toUtf8(text) : "<空>");
	XString_delete_base(text);
}

static bool XBsonTest_appendOwned(XBsonDocument* doc, const char* key,
		                               XBsonValue* value)
{
	if (!value) return false;
	bool result = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
	XBsonValue_delete(value);
	return result;
}

static bool XBsonTest_arrayAppendOwned(XBsonArray* array, XBsonValue* value)
{
	if (!value) return false;
	bool result = XBsonArray_append_move_base(array, value);
	XBsonValue_delete(value);
	return result;
}

static bool XBsonTest_documentRoundTrip(const XBsonDocument* doc,
		                                     XBsonDocument** parsedOut)
{
	XByteArray* first = XBsonDocument_toBson(doc);
	XBsonDocument* parsed = first ? XBsonDocument_fromBson(first) : NULL;
	XByteArray* second = parsed ? XBsonDocument_toBson(parsed) : NULL;
	bool result = XBsonTest_bytesEqual(first, second);
	XByteArray_delete_base(first);
	XByteArray_delete_base(second);
	if (parsedOut) *parsedOut = parsed;
	else XBsonDocument_delete_base(parsed);
	return result;
}

// 构造覆盖 BSON 1.1 所有标准元素类型的文档
static XBsonDocument* XBsonTest_createAllTypes(void)
{
	static const uint8_t oid[12] = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
		0xcd, 0xef, 0x10, 0x32, 0x54, 0x76
	};
	static const uint8_t decimal[16] = {
		0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
		0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
	};
	static const char binaryData[] = { 0x00, 0x7f, (char)0xff };

	XBsonDocument* doc = XBsonDocument_create();
	XBsonDocument* nested = XBsonDocument_create();
	XBsonDocument* scope = XBsonDocument_create();
	XBsonArray* array = XBsonArray_create();
	XByteArray* binary = XByteArray_create_with_data(binaryData, sizeof(binaryData));
	XString* text = XString_create_utf8("中文字符串");
	XString* pattern = XString_create_utf8("^a.*z$");
	XString* options = XString_create_utf8("im");
	XString* nameSpace = XString_create_utf8("测试.集合");
	XString* code = XString_create_utf8("return x + 1;");
	XString* symbol = XString_create_utf8("符号值");
	bool ok = doc && nested && scope && array && binary && text && pattern &&
		options && nameSpace && code && symbol;

	if (ok) ok = XBsonDocument_insert_keyUtf8_utf8(nested, "inside", "嵌套值");
	if (ok) ok = XBsonDocument_insert_keyUtf8_int32(scope, "x", 41);
	if (ok) ok = XBsonTest_arrayAppendOwned(array, XBsonValue_create_int32(-7));
	if (ok) ok = XBsonTest_arrayAppendOwned(array, XBsonValue_create_bool(true));

	if (ok) ok = XBsonTest_appendOwned(doc, "double", XBsonValue_create_double(3.25));
	if (ok) ok = XBsonTest_appendOwned(doc, "string", XBsonValue_create_string(text));
	if (ok) ok = XBsonTest_appendOwned(doc, "document", XBsonValue_create_document(nested));
	if (ok) ok = XBsonTest_appendOwned(doc, "array", XBsonValue_create_array(array));
	if (ok) ok = XBsonTest_appendOwned(doc, "binary", XBsonValue_create_binary(XBSON_BINARY_GENERIC, binary));
	if (ok) ok = XBsonTest_appendOwned(doc, "binaryOld", XBsonValue_create_binary(XBSON_BINARY_BINARY_OLD, binary));
	if (ok) ok = XBsonTest_appendOwned(doc, "undefined", XBsonValue_create_undefined());
	if (ok) ok = XBsonTest_appendOwned(doc, "objectId", XBsonValue_create_object_id(oid));
	if (ok) ok = XBsonTest_appendOwned(doc, "bool", XBsonValue_create_bool(true));
	if (ok) ok = XBsonTest_appendOwned(doc, "datetime", XBsonValue_create_datetime(-1234567890123LL));
	if (ok) ok = XBsonTest_appendOwned(doc, "null", XBsonValue_create_null());
	if (ok) ok = XBsonTest_appendOwned(doc, "regex", XBsonValue_create_regex(pattern, options));
	if (ok) ok = XBsonTest_appendOwned(doc, "dbPointer", XBsonValue_create_dbpointer(nameSpace, oid));
	if (ok) ok = XBsonTest_appendOwned(doc, "javascript", XBsonValue_create_javascript(code));
	if (ok) ok = XBsonTest_appendOwned(doc, "symbol", XBsonValue_create_symbol(symbol));
	if (ok) ok = XBsonTest_appendOwned(doc, "javascriptScope",
		XBsonValue_create_javascript_scope(code, scope));
	if (ok) ok = XBsonTest_appendOwned(doc, "int32", XBsonValue_create_int32(INT32_MIN));
	if (ok) ok = XBsonTest_appendOwned(doc, "timestamp",
		XBsonValue_create_timestamp(0x11223344U, 0xaabbccddU));
	if (ok) ok = XBsonTest_appendOwned(doc, "int64", XBsonValue_create_int64(INT64_MAX));
	if (ok) ok = XBsonTest_appendOwned(doc, "decimal128", XBsonValue_create_decimal128(decimal));
	if (ok) ok = XBsonTest_appendOwned(doc, "minKey", XBsonValue_create_min_key());
	if (ok) ok = XBsonTest_appendOwned(doc, "maxKey", XBsonValue_create_max_key());

	XString_delete_base(text);
	XString_delete_base(pattern);
	XString_delete_base(options);
	XString_delete_base(nameSpace);
	XString_delete_base(code);
	XString_delete_base(symbol);
	XByteArray_delete_base(binary);
	XBsonArray_delete_base(array);
	XBsonDocument_delete_base(nested);
	XBsonDocument_delete_base(scope);
	if (!ok) {
		XBsonDocument_delete_base(doc);
		return NULL;
	}
	return doc;
}

static void XBsonTest_standardAndTypes(void)
{
	static const uint8_t emptyBson[] = { 0x05, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t duplicateBson[] = {
		0x13, 0x00, 0x00, 0x00,
		0x10, 'a', 0x00, 0x01, 0x00, 0x00, 0x00,
		0x10, 'a', 0x00, 0x02, 0x00, 0x00, 0x00,
		0x00
	};
	static const XBsonType expectedTypes[] = {
		XBSON_TYPE_DOUBLE, XBSON_TYPE_STRING, XBSON_TYPE_DOCUMENT,
		XBSON_TYPE_ARRAY, XBSON_TYPE_BINARY, XBSON_TYPE_BINARY,
		XBSON_TYPE_UNDEFINED, XBSON_TYPE_OBJECT_ID, XBSON_TYPE_BOOL,
		XBSON_TYPE_DATETIME, XBSON_TYPE_NULL, XBSON_TYPE_REGEX,
		XBSON_TYPE_DBPOINTER, XBSON_TYPE_JAVASCRIPT, XBSON_TYPE_SYMBOL,
		XBSON_TYPE_JAVASCRIPT_SCOPE, XBSON_TYPE_INT32, XBSON_TYPE_TIMESTAMP,
		XBSON_TYPE_INT64, XBSON_TYPE_DECIMAL128, XBSON_TYPE_MIN_KEY,
		XBSON_TYPE_MAX_KEY
	};

	XBsonTest_begin("BSON 1.1 字节格式与全部类型");
	XBsonDocument* emptyDoc = XBsonDocument_create();
	XBsonArray* emptyArray = XBsonArray_create();
	XByteArray* emptyDocBytes = XBsonDocument_toBson(emptyDoc);
	XByteArray* emptyArrayBytes = XBsonArray_toBson(emptyArray);
	XBsonTest_check(XBsonTest_bytesEqualData(emptyDocBytes, emptyBson, sizeof(emptyBson)),
		"空文档编码为 5 字节 BSON");
	XBsonTest_check(XBsonTest_bytesEqualData(emptyArrayBytes, emptyBson, sizeof(emptyBson)),
		"空数组编码为 5 字节 BSON");
	XByteArray_delete_base(emptyDocBytes);
	XByteArray_delete_base(emptyArrayBytes);
	XBsonDocument_delete_base(emptyDoc);
	XBsonArray_delete_base(emptyArray);

	XBsonDocument* duplicate = XBsonDocument_create();
	XBsonDocument_insert_keyUtf8_int32(duplicate, "a", 1);
	XBsonDocument_insert_keyUtf8_int32(duplicate, "a", 2);
	XByteArray* duplicateBytes = XBsonDocument_toBson(duplicate);
	XBsonTest_check(XBsonTest_bytesEqualData(duplicateBytes, duplicateBson,
		sizeof(duplicateBson)), "重复键保持插入顺序并符合标准字节格式");
	XBsonDocument* duplicateParsed = XBsonDocument_fromBson(duplicateBytes);
	XBsonTest_check(duplicateParsed && XBsonDocument_count_keyUtf8(duplicateParsed, "a") == 2,
		"反序列化保留重复键");
	XByteArray_delete_base(duplicateBytes);
	XBsonDocument_delete_base(duplicateParsed);
	XBsonDocument_delete_base(duplicate);

	XBsonDocument* allTypes = XBsonTest_createAllTypes();
	XBsonDocument* parsed = NULL;
	XBsonTest_check(allTypes && XBsonDocument_size_base(allTypes) ==
		sizeof(expectedTypes) / sizeof(expectedTypes[0]), "创建全部 BSON 1.1 元素类型");
	XBsonTest_check(allTypes && XBsonTest_documentRoundTrip(allTypes, &parsed),
		"全部类型序列化、反序列化后字节完全一致");
	bool typesMatch = parsed && XBsonDocument_size_base(parsed) ==
		sizeof(expectedTypes) / sizeof(expectedTypes[0]);
	for (size_t i = 0; typesMatch && i < sizeof(expectedTypes) / sizeof(expectedTypes[0]); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(parsed, (int64_t)i);
		typesMatch = element && XBsonValue_type(XBsonElement_value_const(element)) == expectedTypes[i];
	}
	XBsonTest_check(typesMatch, "全部类型标签与顺序保持不变");

	const XBsonValue* stringValue = parsed ? XBsonDocument_value_keyUtf8(parsed, "string") : NULL;
	const XBsonValue* binaryValue = parsed ? XBsonDocument_value_keyUtf8(parsed, "binaryOld") : NULL;
	const XBsonValue* scopeValue = parsed ? XBsonDocument_value_keyUtf8(parsed, "javascriptScope") : NULL;
	const XBsonValue* timestampValue = parsed ? XBsonDocument_value_keyUtf8(parsed, "timestamp") : NULL;
	XBsonBinarySubtype subtype = XBSON_BINARY_GENERIC;
	const XByteArray* binaryPayload = XBsonValue_toBinary(binaryValue, &subtype);
	XBsonTest_check(XBsonTest_stringEquals(XBsonValue_toString(stringValue), "中文字符串"),
		"UTF-8 字符串值完整往返");
	XBsonTest_check(binaryPayload && subtype == XBSON_BINARY_BINARY_OLD &&
		XByteArray_size_base(binaryPayload) == 3, "旧二进制子类型包含并校验内层长度");
	XBsonTest_check(scopeValue && XBsonValue_isJavascriptScope(scopeValue) &&
		XBsonTest_stringEquals(XBsonValue_toString(scopeValue), "return x + 1;") &&
		XBsonValue_toJavascriptScope(scopeValue) != NULL,
		"JavaScript 代码和作用域均被保留");
	XBsonTest_check(timestampValue &&
		XBsonValue_timestampIncrement(timestampValue) == 0x11223344U &&
		XBsonValue_timestamp(timestampValue) == 0xaabbccddU,
		"时间戳增量和秒值顺序正确");

	XBsonDocument_delete_base(parsed);
	XBsonDocument_delete_base(allTypes);
}

// 文档、数组和值对象的创建、拷贝、移动和访问接口测试
static void XBsonTest_documentApi(void)
{
	XBsonTest_begin("XBsonDocument 与 XBsonArray API");
	XBsonDocument* doc = XBsonDocument_create();
	XBsonArray* arrayCopy = XBsonArray_create();
	XBsonArray* arrayMove = XBsonArray_create();
	XBsonDocument* documentCopy = XBsonDocument_create();
	XBsonDocument* documentMove = XBsonDocument_create();
	XString* copiedString = XString_create_utf8("拷贝字符串");
	XString* movedString = XString_create_utf8("移动字符串");
	XBsonTest_arrayAppendOwned(arrayCopy, XBsonValue_create_int32(1));
	XBsonTest_arrayAppendOwned(arrayMove, XBsonValue_create_int32(2));
	XBsonDocument_insert_keyUtf8_int32(documentCopy, "n", 3);
	XBsonDocument_insert_keyUtf8_int32(documentMove, "n", 4);

	bool inserted = doc &&
		XBsonDocument_insert_keyUtf8_double(doc, "double", 1.5) &&
		XBsonDocument_insert_keyUtf8_int32(doc, "int32", 32) &&
		XBsonDocument_insert_keyUtf8_int64(doc, "int64", 64) &&
		XBsonDocument_insert_keyUtf8_string(doc, "stringCopy", copiedString) &&
		XBsonDocument_insert_keyUtf8_string_move(doc, "stringMove", movedString) &&
		XBsonDocument_insert_keyUtf8_utf8(doc, "utf8", "中文") &&
		XBsonDocument_insert_keyUtf8_null(doc, "null") &&
		XBsonDocument_insert_keyUtf8_bool(doc, "bool", true) &&
		XBsonDocument_insert_keyUtf8_array(doc, "arrayCopy", arrayCopy) &&
		XBsonDocument_insert_keyUtf8_array_move(doc, "arrayMove", arrayMove) &&
		XBsonDocument_insert_keyUtf8_document(doc, "documentCopy", documentCopy) &&
		XBsonDocument_insert_keyUtf8_document_move(doc, "documentMove", documentMove);
	XBsonTest_check(inserted, "文档全部便捷插入 API 成功");
	XBsonTest_check(XString_toUtf8_length(movedString) == 0 &&
		XBsonArray_isEmpty_base(arrayMove) && XBsonDocument_isEmpty_base(documentMove),
		"字符串、数组和文档移动插入后源对象为空");

	XBsonValue* copiedValue = XBsonValue_create_int32(100);
	XBsonValue* movedValue = XBsonValue_create_int32(200);
	XString* keyCopy = XString_create_utf8("valueMoveByStringKey");
	XBsonTest_check(XBsonDocument_insert_keyUtf8_value(doc, "valueCopy", copiedValue) &&
		XBsonDocument_insert_keyUtf8_value_move(doc, "valueMove", movedValue) &&
		XBsonDocument_insert_value_move(doc, keyCopy, copiedValue),
		"值拷贝、值移动和 XString 键插入 API 成功");
	XBsonTest_check(XBsonValue_type(movedValue) == 0 && XBsonValue_type(copiedValue) == 0,
		"值移动插入后源值进入可释放空状态");
	XBsonValue_delete(copiedValue);
	XBsonValue_delete(movedValue);
	XString_delete_base(keyCopy);

	XString* appendKey = XString_create_utf8("append");
	XBsonValue* appendValue = XBsonValue_create_bool(false);
	XString* appendMoveKey = XString_create_utf8("appendMove");
	XBsonValue* appendMoveValue = XBsonValue_create_bool(true);
	XBsonTest_check(XBsonDocument_append(doc, appendKey, appendValue) &&
		XBsonDocument_append_move(doc, appendMoveKey, appendMoveValue),
		"append 拷贝与 append_move 接口成功");
	XBsonTest_check(XString_toUtf8_length(appendMoveKey) == 0 &&
		XBsonValue_type(appendMoveValue) == 0, "append_move 正确转移键和值");
	XString_delete_base(appendKey);
	XString_delete_base(appendMoveKey);
	XBsonValue_delete(appendValue);
	XBsonValue_delete(appendMoveValue);

	XBsonDocument_insert_keyUtf8_int32(doc, "重复", 1);
	XBsonDocument_insert_keyUtf8_int32(doc, "重复", 2);
	XBsonDocument_insert_keyUtf8_int32(doc, "重复", 3);
	XString* duplicateKey = XString_create_utf8("重复");
	int64_t duplicateIndex = XBsonDocument_indexOf(doc, duplicateKey);
	XBsonValue* firstDuplicate = XBsonDocument_value_base(doc, duplicateKey);
	XBsonTest_check(duplicateIndex >= 0 && firstDuplicate &&
		XBsonValue_toInt32(firstDuplicate, -1) == 1 &&
		XBsonDocument_count_keyUtf8(doc, "重复") == 3 &&
		XBsonDocument_contains_keyUtf8(doc, "重复"),
		"重复键查询返回首项并能统计全部同名元素");
	XString_delete_base(duplicateKey);

	XBsonElement* first = XBsonDocument_at_base(doc, 0);
	const XBsonElement* firstConst = XBsonDocument_at_const(doc, 0);
	XBsonTest_check(first && firstConst && XBsonElement_key_const(firstConst) &&
		XBsonElement_value(first) && XBsonElement_value_const(firstConst),
		"元素索引和键值访问 API 返回有效借用指针");
	XVector* keys = XBsonDocument_keys_base(doc);
	XBsonTest_check(keys && XVector_size_base(keys) == XBsonDocument_size_base(doc),
		"keys API 按元素数量返回有序键副本");
	XVector_delete_base(keys);

	XBsonDocument* copied = XBsonDocument_create_copy(doc);
	XBsonDocument* moved = copied ? XBsonDocument_create_move(copied) : NULL;
	XBsonTest_check(copied && moved && XBsonDocument_isEmpty_base(copied) &&
		XBsonDocument_size_base(moved) == XBsonDocument_size_base(doc),
		"文档深拷贝和移动构造保持正确生命周期");
	XBsonDocument_delete_base(copied);

	XBsonTest_check(XBsonDocument_remove_keyUtf8(moved, "重复") &&
		XBsonDocument_count_keyUtf8(moved, "重复") == 2,
		"remove 删除第一个同名键");
	XBsonTest_check(XBsonDocument_removeAll_keyUtf8(moved, "重复") == 2 &&
		!XBsonDocument_contains_keyUtf8(moved, "重复"),
		"removeAll 删除全部同名键");
	size_t beforeRemove = XBsonDocument_size_base(moved);
	XBsonTest_check(XBsonDocument_removeAt(moved, 0) &&
		XBsonDocument_size_base(moved) + 1 == beforeRemove,
		"removeAt 按索引删除元素");

	XBsonDocument* variantDoc = XBsonDocument_create();
	XBsonDocument_insert_keyUtf8_int32(variantDoc, "k", 1);
	XBsonDocument_insert_keyUtf8_int32(variantDoc, "k", 2);
	XVariantMap* map = XBsonDocument_toVariantMap(variantDoc);
	XBsonTest_check(map && XMap_size_base(map) == 1 &&
		XBsonDocument_size_base(variantDoc) == 2,
		"转 VariantMap 时重复键最后值覆盖且不修改源文档");
	XMap_delete_base(map);
	XVariantMap* movedMap = XBsonDocument_toVariantMap_move(variantDoc);
	XBsonTest_check(movedMap && XBsonDocument_isEmpty_base(variantDoc),
		"移动转 VariantMap 后源文档为空");
	XMap_delete_base(movedMap);
	XBsonDocument_delete_base(variantDoc);

	XVariant* docVariant = XBsonDocument_toVariant(doc);
	XBsonDocument* variantCopy = docVariant ? XVariant_toBsonDocument_ref(docVariant) : NULL;
	XBsonTest_check(variantCopy && variantCopy != doc &&
		XBsonDocument_size_base(variantCopy) == XBsonDocument_size_base(doc),
		"文档深拷贝转换为 XVariant");
	XVariant_delete_base(docVariant);
	XBsonDocument* moveVariantSource = XBsonDocument_create_copy(doc);
	XVariant* movedVariant = XBsonDocument_toVariant_move(moveVariantSource);
	XBsonTest_check(movedVariant && XBsonDocument_isEmpty_base(moveVariantSource),
		"文档移动转换为 XVariant 后源文档为空");
	XVariant_delete_base(movedVariant);
	XBsonDocument_delete_base(moveVariantSource);

	XBsonArray* apiArray = XBsonArray_create();
	XBsonValue* value = XBsonValue_create_int32(2);
	XBsonArray_append_base(apiArray, value);
	XBsonValue_setInt32(value, 1);
	XBsonArray_prepend_base(apiArray, value);
	XBsonValue_setInt32(value, 3);
	XBsonArray_append_move_base(apiArray, value);
	XBsonValue_setInt32(value, 9);
	XBsonArray_insert(apiArray, 1, value);
	XBsonValue_setInt32(value, 8);
	XBsonArray_replace(apiArray, 1, value);
	XBsonTest_check(XBsonArray_size_base(apiArray) == 4 &&
		XBsonValue_toInt32(XBsonArray_at_base(apiArray, 1), -1) == 8,
		"数组追加、前置、插入和替换 API 成功");
	XBsonArray_removeAt_base(apiArray, 1);
	XBsonTest_check(XBsonArray_size_base(apiArray) == 3,
		"数组按索引删除元素");
	XBsonValue_delete(value);

	XBsonArray* arrayClone = XBsonArray_create_copy(apiArray);
	XBsonArray* arrayMoved = XBsonArray_create_move(arrayClone);
	XBsonTest_check(arrayClone && arrayMoved && XBsonArray_isEmpty_base(arrayClone) &&
		XBsonArray_size_base(arrayMoved) == XBsonArray_size_base(apiArray),
		"数组深拷贝和移动构造成功");
	XBsonArray_delete_base(arrayClone);
	XBsonArray_delete_base(arrayMoved);
	XVariantList* list = XBsonArray_toVariantList(apiArray);
	XBsonTest_check(list && XVariantList_size_base(list) == XBsonArray_size_base(apiArray) &&
		!XBsonArray_isEmpty_base(apiArray), "数组深拷贝转换为 VariantList");
	XVariantList_delete_base(list);
	XBsonArray* listMoveSource = XBsonArray_create_copy(apiArray);
	XVariantList* movedList = XBsonArray_toVariantList_move(listMoveSource);
	XBsonTest_check(movedList && XBsonArray_isEmpty_base(listMoveSource),
		"数组移动转换为 VariantList 后源数组为空");
	XVariantList_delete_base(movedList);
	XBsonArray_delete_base(listMoveSource);
	XVariant* arrayVariant = XBsonArray_toVariant(apiArray);
	XBsonTest_check(arrayVariant && XVariant_toBsonArray_ref(arrayVariant) != apiArray,
		"数组深拷贝转换为 XVariant");
	XVariant_delete_base(arrayVariant);
	XBsonArray* arrayVariantSource = XBsonArray_create_copy(apiArray);
	XVariant* arrayMovedVariant = XBsonArray_toVariant_move(arrayVariantSource);
	XBsonTest_check(arrayMovedVariant && XBsonArray_isEmpty_base(arrayVariantSource),
		"数组移动转换为 XVariant 后源数组为空");
	XVariant_delete_base(arrayMovedVariant);
	XBsonArray_delete_base(arrayVariantSource);
	XBsonArray_delete_base(apiArray);

	XBsonDocument_delete_base(moved);
	XString_delete_base(copiedString);
	XString_delete_base(movedString);
	XBsonArray_delete_base(arrayCopy);
	XBsonArray_delete_base(arrayMove);
	XBsonDocument_delete_base(documentCopy);
	XBsonDocument_delete_base(documentMove);
	XBsonDocument_delete_base(doc);
}

static void XBsonTest_valueApi(void)
{
	XBsonTest_begin("XBsonValue 生命周期、访问器与移动语义");
	XBsonValue* value = XBsonValue_create_null();
	XBsonValue_setBool(value, true);
	XBsonTest_check(XBsonValue_isBool(value) && XBsonValue_toBool(value, false),
		"布尔设置、类型判断和读取");
	XBsonValue_setDouble(value, 12.5);
	XBsonTest_check(XBsonValue_isDouble(value) && XBsonValue_toDouble(value, 0.0) == 12.5,
		"double 设置、类型判断和读取");
	XBsonValue_setInt32(value, -123);
	XBsonTest_check(XBsonValue_isInt32(value) && XBsonValue_toInt32(value, 0) == -123,
		"int32 设置、类型判断和读取");
	XBsonValue_setInt64(value, INT64_MIN);
	XBsonTest_check(XBsonValue_isInt64(value) && XBsonValue_toInt64(value, 0) == INT64_MIN,
		"int64 设置、类型判断和读取");
	XBsonValue_setString_utf8(value, "可移动文本");
	XBsonValue* copied = XBsonValue_create_copy(value);
	XBsonValue* moved = XBsonValue_create_move(value);
	XBsonTest_check(copied && moved && XBsonValue_type(value) == 0 &&
		XBsonTest_stringEquals(XBsonValue_toString(copied), "可移动文本") &&
		XBsonTest_stringEquals(XBsonValue_toString(moved), "可移动文本"),
		"值深拷贝与移动构造保持字符串所有权");

	XVariant* variant = XBsonValue_toVariant(copied);
	XBsonValue* variantValue = variant ? XVariant_toBsonValue_ref(variant) : NULL;
	XBsonTest_check(variantValue && variantValue != copied && XBsonValue_isString(variantValue),
		"值深拷贝转换为 XVariant");
	XVariant_delete_base(variant);
	XVariant* movedVariant = XBsonValue_toVariant_move(moved);
	XBsonTest_check(movedVariant && XBsonValue_type(moved) == 0 &&
		XBsonValue_isString(XVariant_toBsonValue_ref(movedVariant)),
		"值移动转换为 XVariant 后源值为空");
	XVariant_delete_base(movedVariant);

	XBsonValue_setUndefined(copied);
	XBsonTest_check(XBsonValue_isUndefined(copied), "undefined 设置与类型判断");
	XBsonValue_setMin_key(copied);
	XBsonTest_check(XBsonValue_isMinKey(copied), "MinKey 设置与类型判断");
	XBsonValue_setMax_key(copied);
	XBsonTest_check(XBsonValue_isMaxKey(copied), "MaxKey 设置与类型判断");
	XBsonValue_setNull(copied);
	XBsonTest_check(XBsonValue_isNull(copied), "null 设置与类型判断");
	XBsonValue_clear(copied);
	XBsonTest_check(XBsonValue_isNull(copied), "clear 保留值类型并清空数据");

	XBsonValue_delete(value);
	XBsonValue_delete(copied);
	XBsonValue_delete(moved);
}

// XBson 与 XJson 的对象、数组和特殊类型双向转换测试
static void XBsonTest_jsonConversions(void)
{
	XBsonTest_begin("XBson 与 XJson 双向转换");
	XJsonObject* object = XJsonObject_create();
	XJsonArray* childArray = XJsonArray_create();
	XJsonObject* childObject = XJsonObject_create();
	XJsonValue* childValue = XJsonValue_create_int(7);
	XJsonArray_append_move_base(childArray, childValue);
	XJsonValue_delete(childValue);
	XJsonObject_insert_keyUtf8_utf8(childObject, "inside", "内容");
	bool jsonBuilt = object &&
		XJsonObject_insert_keyUtf8_utf8(object, "name", "测试") &&
		XJsonObject_insert_keyUtf8_int(object, "count", 42) &&
		XJsonObject_insert_keyUtf8_double(object, "ratio", 0.5) &&
		XJsonObject_insert_keyUtf8_bool(object, "enabled", true) &&
		XJsonObject_insert_keyUtf8_null(object, "nothing") &&
		XJsonObject_insert_keyUtf8_array(object, "array", childArray) &&
		XJsonObject_insert_keyUtf8_object(object, "object", childObject);
	XBsonTest_check(jsonBuilt, "构造包含全部 JSON 基础类型的对象");

	XBsonDocument* bsonDoc = XBsonDocument_fromJsonObject(object);
	XJsonObject* objectRoundTrip = bsonDoc ? XBsonDocument_toJsonObject(bsonDoc) : NULL;
	bool objectEqual = objectRoundTrip && XJsonObject_equals(object, objectRoundTrip);
	if (!objectEqual) {
		XBsonTest_printJsonObject("原始 JSON 对象", object);
		XBsonTest_printJsonObject("转换后的 JSON 对象", objectRoundTrip);
	}
	XBsonTest_check(objectEqual,
		"XJsonObject -> XBsonDocument -> XJsonObject 内容一致");
	XJsonObject_delete_base(objectRoundTrip);

	XJsonDocument* jsonDoc = XJsonDocument_create_object(object);
	XByteArray* bsonBytes = XJsonDocument_toBson(jsonDoc);
	XJsonDocument* jsonDocRoundTrip = bsonBytes ?
		XJsonDocument_fromBson_document(bsonBytes) : NULL;
	bool documentEqual = jsonDocRoundTrip && XJsonDocument_isObject(jsonDocRoundTrip) &&
		XJsonObject_equals(object, XJsonDocument_object(jsonDocRoundTrip));
	if (!documentEqual) {
		XBsonTest_printJsonObject("XJsonDocument 转换后的对象",
			jsonDocRoundTrip ? XJsonDocument_object(jsonDocRoundTrip) : NULL);
	}
	XBsonTest_check(documentEqual, "XJsonDocument 对象与 BSON 文档字节双向转换一致");
	XJsonDocument_delete(jsonDocRoundTrip);
	XByteArray_delete_base(bsonBytes);
	XJsonDocument_delete(jsonDoc);

	XJsonArray* rootArray = XJsonArray_create();
	XJsonValue* intValue = XJsonValue_create_int(-1);
	XString* arrayText = XString_create_utf8("数组文本");
	XJsonValue* stringValue = XJsonValue_create_string(arrayText);
	XJsonArray_append_move_base(rootArray, intValue);
	XJsonArray_append_move_base(rootArray, stringValue);
	XJsonValue_delete(intValue);
	XJsonValue_delete(stringValue);
	XString_delete_base(arrayText);
	XBsonArray* bsonArray = XBsonArray_fromJsonArray(rootArray);
	XJsonArray* arrayRoundTrip = bsonArray ? XBsonArray_toJsonArray(bsonArray) : NULL;
	XBsonTest_check(arrayRoundTrip && XJsonArray_equals(rootArray, arrayRoundTrip),
		"XJsonArray -> XBsonArray -> XJsonArray 内容一致");
	XJsonDocument* arrayDoc = XJsonDocument_create_array(rootArray);
	XByteArray* arrayBytes = XJsonDocument_toBson(arrayDoc);
	XJsonDocument* arrayDocRoundTrip = arrayBytes ? XJsonDocument_fromBson_array(arrayBytes) : NULL;
	XBsonTest_check(arrayDocRoundTrip && XJsonDocument_isArray(arrayDocRoundTrip) &&
		XJsonArray_equals(rootArray, XJsonDocument_array(arrayDocRoundTrip)),
		"XJsonDocument 数组与 BSON 数组字节双向转换一致");
	XJsonDocument_delete(arrayDocRoundTrip);
	XByteArray_delete_base(arrayBytes);
	XJsonDocument_delete(arrayDoc);
	XJsonArray_delete_base(arrayRoundTrip);
	XBsonArray_delete_base(bsonArray);
	XJsonArray_delete_base(rootArray);

	XBsonDocument* allTypes = XBsonTest_createAllTypes();
	bool specialRoundTrip = allTypes != NULL;
	for (size_t i = 0; specialRoundTrip && i < XBsonDocument_size_base(allTypes); ++i) {
		const XBsonElement* element = XBsonDocument_at_const(allTypes, (int64_t)i);
		const XBsonValue* source = XBsonElement_value_const(element);
		XJsonValue* jsonValue = XBsonValue_to_json(source);
		XBsonValue* restored = jsonValue ? XBsonValue_from_json(jsonValue) : NULL;
		XByteArray* sourceBytes = XByteArray_create();
		XByteArray* restoredBytes = XByteArray_create();
		bool itemOk = jsonValue && restored && sourceBytes && restoredBytes &&
			XBsonValue_serialize(source, "v", sourceBytes) &&
			XBsonValue_serialize(restored, "v", restoredBytes) &&
			XBsonTest_bytesEqual(sourceBytes, restoredBytes);
		specialRoundTrip = itemOk;
		XByteArray_delete_base(sourceBytes);
		XByteArray_delete_base(restoredBytes);
		XBsonValue_delete(restored);
		XJsonValue_delete(jsonValue);
	}
	XBsonTest_check(specialRoundTrip,
		"全部 BSON 类型经 XJsonValue 表示后可恢复为相同 BSON 字节");
	XBsonDocument_delete_base(allTypes);

	XBsonDocument* duplicate = XBsonDocument_create();
	XBsonDocument_insert_keyUtf8_int32(duplicate, "same", 1);
	XBsonDocument_insert_keyUtf8_int32(duplicate, "same", 2);
	XJsonObject* duplicateJson = XBsonDocument_toJsonObject(duplicate);
	XBsonDocument* duplicateBack = duplicateJson ?
		XBsonDocument_fromJsonObject(duplicateJson) : NULL;
	XBsonTest_check(duplicateJson && XJsonObject_size_base(duplicateJson) == 1 &&
		duplicateBack && XBsonDocument_count_keyUtf8(duplicateBack, "same") == 1 &&
		XBsonValue_toInt32(XBsonDocument_value_keyUtf8(duplicateBack, "same"), -1) == 2,
		"BSON 重复键转 JSON 时按约定保留最后一个值");
	XBsonDocument_delete_base(duplicateBack);
	XJsonObject_delete_base(duplicateJson);
	XBsonDocument_delete_base(duplicate);

	XBsonDocument_delete_base(bsonDoc);
	XJsonArray_delete_base(childArray);
	XJsonObject_delete_base(childObject);
	XJsonObject_delete_base(object);
}

static void XBsonTest_invalidInputs(void)
{
	static const uint8_t shortDocument[] = { 0x04, 0x00, 0x00, 0x00 };
	static const uint8_t wrongTerminator[] = { 0x05, 0x00, 0x00, 0x00, 0x01 };
	static const uint8_t stringLengthZero[] = {
		0x0c, 0x00, 0x00, 0x00, 0x02, 's', 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00
	};
	static const uint8_t invalidBool[] = {
		0x09, 0x00, 0x00, 0x00, 0x08, 'b', 0x00, 0x02, 0x00
	};
	static const uint8_t unsortedRegex[] = {
		0x0d, 0x00, 0x00, 0x00, 0x0b, 'r', 0x00,
		'a', 0x00, 'm', 'i', 0x00, 0x00
	};
	static const uint8_t badNestedLength[] = {
		0x0c, 0x00, 0x00, 0x00, 0x03, 'd', 0x00,
		0x04, 0x00, 0x00, 0x00, 0x00
	};
	static const uint8_t badArrayKey[] = {
		0x0c, 0x00, 0x00, 0x00, 0x10, '1', 0x00,
		0x01, 0x00, 0x00, 0x00, 0x00
	};
	static const uint8_t badOldBinaryLength[] = {
		0x13, 0x00, 0x00, 0x00, 0x05, 'x', 0x00,
		0x06, 0x00, 0x00, 0x00, 0x02,
		0x03, 0x00, 0x00, 0x00, 0xaa, 0xbb, 0x00
	};
	static const uint8_t unknownType[] = {
		0x08, 0x00, 0x00, 0x00, 0x20, 'x', 0x00, 0x00
	};

	XBsonTest_begin("非法 BSON 输入和边界校验");
	XBsonDocument* doc = XBsonDocument_create();
	XBsonDocument_insert_keyUtf8_int32(doc, "old", 1);
	XBsonTest_check(!XBsonDocument_from_bytes(doc, shortDocument, sizeof(shortDocument)) &&
		XBsonDocument_isEmpty_base(doc), "拒绝不足 5 字节的文档并清空目标");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, wrongTerminator, sizeof(wrongTerminator)),
		"拒绝缺少尾部 NUL 的文档");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, stringLengthZero, sizeof(stringLengthZero)),
		"拒绝长度为 0 的 BSON 字符串");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, invalidBool, sizeof(invalidBool)),
		"拒绝非 0/1 的 BSON 布尔值");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, unsortedRegex, sizeof(unsortedRegex)),
		"拒绝未按字母顺序排列的正则选项");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, badNestedLength, sizeof(badNestedLength)),
		"拒绝小于 5 字节的嵌套文档");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, badOldBinaryLength,
		sizeof(badOldBinaryLength)), "拒绝旧二进制子类型内外长度不一致");
	XBsonTest_check(!XBsonDocument_from_bytes(doc, unknownType, sizeof(unknownType)),
		"拒绝未知 BSON 类型标签");
	XBsonDocument_delete_base(doc);

	XBsonArray* array = XBsonArray_create();
	XBsonTest_check(!XBsonArray_from_bytes(array, badArrayKey, sizeof(badArrayKey)) &&
		XBsonArray_isEmpty_base(array), "拒绝不从 0 连续编号的 BSON 数组键");
	XBsonArray_delete_base(array);

	XByteArray* malformed = XByteArray_create_with_data((const char*)stringLengthZero,
		sizeof(stringLengthZero));
	XBsonTest_check(XBsonDocument_fromBson(malformed) == NULL,
		"fromBson 对非法完整字节流返回 NULL");
	XByteArray_delete_base(malformed);
	XBsonTest_check(XBsonValue_create_string(NULL) == NULL &&
		XBsonValue_create_document(NULL) == NULL &&
		XBsonValue_create_array(NULL) == NULL &&
		XBsonValue_create_object_id(NULL) == NULL &&
		XBsonValue_create_decimal128(NULL) == NULL,
		"固定数据构造 API 拒绝 NULL 参数");
}

// 菜单测试入口：每项执行结束后退出应用，便于自动化运行
static void XBsonTest_runSelected(bool standard, bool document,
		                               bool value, bool json, bool invalid)
{
	memset(&g_bsonTestState, 0, sizeof(g_bsonTestState));
	if (standard) XBsonTest_standardAndTypes();
	if (document) XBsonTest_documentApi();
	if (value) XBsonTest_valueApi();
	if (json) XBsonTest_jsonConversions();
	if (invalid) XBsonTest_invalidInputs();
	XBsonTest_summary();
	//XCoreApplication_quit();
}

static void XBsonTest_runAll(void)
{
	XBsonTest_runSelected(true, true, true, true, true);
}

static void XBsonTest_runStandard(void)
{
	XBsonTest_runSelected(true, false, false, false, false);
}

static void XBsonTest_runApi(void)
{
	XBsonTest_runSelected(false, true, true, false, false);
}

static void XBsonTest_runJson(void)
{
	XBsonTest_runSelected(false, false, false, true, false);
}

static void XBsonTest_runInvalid(void)
{
	XBsonTest_runSelected(false, false, false, false, true);
}

void XTestMenu_XBsonTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XBson(Bson)");
	XTestMenu_addMenu(root, menu);
	XAction* all = XTestMenu_addAction(menu, "运行全部 XBson 测试");
	XTestMenu_setActionFunction(all, XBsonTest_runAll);
	XAction* standard = XTestMenu_addAction(menu, "BSON 标准字节与全部类型");
	XTestMenu_setActionFunction(standard, XBsonTest_runStandard);
	XAction* api = XTestMenu_addAction(menu, "文档、数组和值 API");
	XTestMenu_setActionFunction(api, XBsonTest_runApi);
	XAction* json = XTestMenu_addAction(menu, "XBson 与 XJson 双向转换");
	XTestMenu_setActionFunction(json, XBsonTest_runJson);
	XAction* invalid = XTestMenu_addAction(menu, "非法 BSON 输入");
	XTestMenu_setActionFunction(invalid, XBsonTest_runInvalid);
}

#endif
