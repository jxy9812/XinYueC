/**
 * @file       XJsonBsonCoverageTest.c
 * @brief      XJson / XBson 全 API 组合与所有权回归测试。
 * @details    自动化测试：`--test xjson-bson`（失败数作为返回码）。
 *             目标是把 XJson/XBson 的公开 API 按「类型 × 创建/拷贝/移动/
 *             设置/查询/删除/序列化/跨库转换」组合铺满，并用堆在用字节数
 *             回基线做所有权校验——任何漏释放/多重释放都会在这里暴露。
 *
 *             所有权口径（与库实现逐条核对）：
 *               - XJsonValue_create_* / XBsonValue_create_* 返回堆对象，
 *                 XJsonValue_delete / XBsonValue_delete 释放；
 *               - XJsonObject_value_keyUtf8 / XBsonDocument_value_keyUtf8
 *                 同名同义：**借用**，缺失返回 NULL，调用方不得释放；
 *               - value_copy_keyUtf8（两库同名）：返回堆拷贝，调用方释放；
 *               - XJsonObject_take_keyUtf8 / XJsonArray_first/last/takeAt
 *                 返回**拷贝**，调用方释放；
 *               - *_move 系列统一「转移内容、留下空壳」：源对象仍由调用方
 *                 释放（含 XJsonObject/XBsonDocument 的 string_move）；
 *               - *_ref 系列统一「指针接管、零分配」：调用即失去所有权。
 *
 *             运行模型：先跑一轮预热（让类虚表/注册表等一次性分配落定），
 *             再跑一轮计量；计量轮每个场景前后各取一次 XMemory_statistics，
 *             差值非 0 即失败。
 */
#include "XJsonBsonCoverageTest.h"

#include "XJsonDocument.h"
#include "XJsonArray.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XBsonDocument.h"
#include "XBsonArray.h"
#include "XBsonValue.h"
#include "XVariant.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XClass.h"
#include "XPrintf.h"

#include <string.h>
#include <stdint.h>
#include <math.h>

/* ==================== 测试账本与泄漏门禁 ==================== */

static int g_pass = 0;
static int g_fail = 0;
static bool g_counting = true;   /* 预热轮只执行不计数 */

#define CHECK(desc, cond) \
    do { \
        if (g_counting) { \
            if (cond) { ++g_pass; } \
            else { ++g_fail; XPrintf("[FAIL] %s (line %d)\n", desc, __LINE__); } \
        } \
    } while (0)

/** @brief 场景包装：计量轮做「堆在用字节数回基线」断言。 */
static void runScenario(const char* name, void (*fn)(void))
{
    XMemoryStatistics before;
    XMemoryStatistics after;
    if (g_counting) {
        XCoreApplication_processEvents(0);
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    }
    fn();
    if (g_counting) {
        after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
        if (XMemory_statisticsEnabled() && after.systemBytes != before.systemBytes) {
            ++g_fail;
            XPrintf("[FAIL] 泄漏[%s] 基线=%llu 现在=%llu（差 %lld）\n",
                    name,
                    (unsigned long long)before.systemBytes,
                    (unsigned long long)after.systemBytes,
                    (long long)after.systemBytes - (long long)before.systemBytes);
        } else {
            ++g_pass;
        }
    }
}

/* ==================== 断言辅助 ==================== */

static bool utf8Eq(const XString* s, const char* expected)
{
    return s != NULL && XString_equals_utf8(s, expected, XChar_CaseSensitive);
}

static bool bytesEq(const XByteArray* b, const char* expected)
{
    size_t n;
    if (!b || !expected)
        return false;
    n = strlen(expected);
    if (XByteArray_size_base(b) != n)
        return false;
    return memcmp(XByteArray_constData(b), expected, n) == 0;
}

static bool bytesTextEq(const XByteArray* b, const XString* s)
{
    if (!b || !s)
        return false;
    return XByteArray_size_base(b) == XString_toUtf8_length(s) &&
           memcmp(XByteArray_constData(b), XString_toUtf8(s),
                  XByteArray_size_base(b)) == 0;
}

static XJsonDocument* parseJsonUtf8(const char* text)
{
    XString* s = XString_create_utf8(text);
    XJsonDocument* doc = s ? XJsonDocument_fromString(s) : NULL;
    if (s) XClassDelete((XClass*)s);
    return doc;
}

static bool jsonObjectHasInt(const XJsonObject* object, const char* key, int64_t expected)
{
    const XJsonValue* v = XJsonObject_value_keyUtf8(object, key);  /* 借用 */
    return v && XJsonValue_isInt(v) && XJsonValue_toInt(v, -1) == expected;
}

static bool jsonObjectHasUtf8(const XJsonObject* object, const char* key, const char* expected)
{
    const XJsonValue* v = XJsonObject_value_keyUtf8(object, key);
    return v && XJsonValue_isString(v) && utf8Eq(XJsonValue_toString(v), expected);
}

static bool jsonObjectHasArraySize(const XJsonObject* object, const char* key, size_t expectedSize)
{
    const XJsonValue* v = XJsonObject_value_keyUtf8(object, key);
    return v && XJsonValue_isArray(v) &&
           XJsonArray_size_base(XJsonValue_toArray(v)) == expectedSize;
}

static bool jsonObjectHasObject(const XJsonObject* object, const char* key)
{
    const XJsonValue* v = XJsonObject_value_keyUtf8(object, key);
    return v && XJsonValue_isObject(v) && XJsonValue_toObject(v) != NULL;
}

static bool jsonObjectNestedHasInt(const XJsonObject* object, const char* key,
                                   const char* innerKey, int64_t expected)
{
    const XJsonValue* v = XJsonObject_value_keyUtf8(object, key);
    return v && XJsonValue_isObject(v) &&
           jsonObjectHasInt(XJsonValue_toObject(v), innerKey, expected);
}

/* ==================== XJsonValue ==================== */

static void scenario_json_value_create(void)
{
    XString* text = XString_create_utf8("abc");
    XJsonArray* array = XJsonArray_create();
    XJsonObject* object = XJsonObject_create();
    XJsonValue* vnull = XJsonValue_create_null();
    XJsonValue* vundef = XJsonValue_create_undefined();
    XJsonValue* vbool = XJsonValue_create_bool(true);
    XJsonValue* vdouble = XJsonValue_create_double(3.5);
    XJsonValue* vint = XJsonValue_create_int(-42);
    XJsonValue* vstr = XJsonValue_create_string(text);
    XJsonValue* varr = XJsonValue_create_array(array);
    XJsonValue* vobj = XJsonValue_create_object(object);

    CHECK("value: create_null", vnull && XJsonValue_isNull(vnull) &&
          XJsonValue_type(vnull) == XJsonValue_Null);
    CHECK("value: create_undefined", vundef && XJsonValue_isUndefined(vundef) &&
          XJsonValue_type(vundef) == XJsonValue_Undefined);
    CHECK("value: create_bool", vbool && XJsonValue_isBool(vbool) &&
          XJsonValue_toBool(vbool, false));
    CHECK("value: create_double", vdouble && XJsonValue_isDouble(vdouble) &&
          XJsonValue_toDouble(vdouble, 0.0) == 3.5);
    CHECK("value: create_int", vint && XJsonValue_isInt(vint) &&
          XJsonValue_toInt(vint, 0) == -42);
    CHECK("value: create_string", vstr && XJsonValue_isString(vstr) &&
          utf8Eq(XJsonValue_toString(vstr), "abc"));
    CHECK("value: create_array", varr && XJsonValue_isArray(varr) &&
          XJsonValue_toArray(varr) != NULL);
    CHECK("value: create_object", vobj && XJsonValue_isObject(vobj) &&
          XJsonValue_toObject(vobj) != NULL);

    /* 互斥类型判断：每个 is* 对其它类型必须为 false。 */
    CHECK("value: is* 互斥", !XJsonValue_isNull(vbool) && !XJsonValue_isBool(vnull) &&
          !XJsonValue_isString(vint) && !XJsonValue_isArray(vobj) &&
          !XJsonValue_isObject(varr));

    /* 取值缺省语义。 */
    CHECK("value: toBool 缺省", XJsonValue_toBool(vint, true) &&
          !XJsonValue_toBool(vnull, false));
    CHECK("value: toDouble 数值互转", XJsonValue_toDouble(vint, 0.0) == -42.0 &&
          XJsonValue_toDouble(vnull, 1.25) == 1.25);
    CHECK("value: toInt 数值互转", XJsonValue_toInt(vdouble, 0) == 0 &&
          XJsonValue_toInt(vstr, 9) == 9);
    CHECK("value: toString 类型不符", XJsonValue_toString(vint) == NULL &&
          XJsonValue_toArray(vobj) == NULL && XJsonValue_toObject(varr) == NULL);

    /* NULL 防呆。 */
    CHECK("value: NULL 防呆", XJsonValue_type(NULL) == XJsonValue_Invalid &&
          !XJsonValue_isNull(NULL) && XJsonValue_toBool(NULL, true) &&
          XJsonValue_toDouble(NULL, 2.0) == 2.0 && XJsonValue_toInt(NULL, 5) == 5 &&
          XJsonValue_toString(NULL) == NULL && XJsonValue_toArray(NULL) == NULL &&
          XJsonValue_toObject(NULL) == NULL && XJsonValue_equals(NULL, NULL));
    XJsonValue* vundef2 = XJsonValue_create_undefined();
    CHECK("value: equals 语义", XJsonValue_equals(vint, vint) &&
          !XJsonValue_equals(vint, vstr) && XJsonValue_equals(vundef, vundef2));
    XJsonValue_delete(vundef2);

    XJsonValue_delete(vnull);
    XJsonValue_delete(vundef);
    XJsonValue_delete(vbool);
    XJsonValue_delete(vdouble);
    XJsonValue_delete(vint);
    XJsonValue_delete(vstr);
    XJsonValue_delete(varr);
    XJsonValue_delete(vobj);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)object);
    XClassDelete((XClass*)text);
}

static void scenario_json_value_setters(void)
{
    XJsonValue value;
    XString* text;
    XJsonArray* array;
    XJsonObject* object;

    XJsonValue_init(&value, XJsonValue_Invalid);
    CHECK("value: init Invalid", XJsonValue_type(&value) == XJsonValue_Invalid);
    {
        XJsonValue probe;
        XJsonValue_init(&probe, XJsonValue_String);
        /* 与 XBsonValue_init 统一：init 只设类型标签，不分配载荷。 */
        CHECK("value: init 不分配载荷", XJsonValue_toString(&probe) == NULL);
        XJsonValue_deinit(&probe);
    }

    XJsonValue_setInt(&value, 11);
    CHECK("value: setInt", XJsonValue_isInt(&value) && XJsonValue_toInt(&value, 0) == 11);
    XJsonValue_setString(&value, NULL);
    CHECK("value: setString(NULL) 不生效", XJsonValue_isInt(&value) &&
          XJsonValue_toInt(&value, 0) == 11);
    XJsonValue_setString_utf8(&value, NULL);
    CHECK("value: setString_utf8(NULL) 不生效", XJsonValue_isInt(&value) &&
          XJsonValue_toInt(&value, 0) == 11);
    XJsonValue_setDouble(&value, 1.5);
    CHECK("value: setDouble", XJsonValue_isDouble(&value) &&
          XJsonValue_toDouble(&value, 0.0) == 1.5);
    XJsonValue_setBool(&value, true);
    CHECK("value: setBool", XJsonValue_isBool(&value) && XJsonValue_toBool(&value, false));
    XJsonValue_setNull(&value);
    CHECK("value: setNull", XJsonValue_isNull(&value));
    XJsonValue_setUndefined(&value);
    CHECK("value: setUndefined", XJsonValue_isUndefined(&value));

    text = XString_create_utf8("copy-me");
    XJsonValue_setString(&value, text);
    CHECK("value: setString 拷贝", XJsonValue_isString(&value) &&
          utf8Eq(XJsonValue_toString(&value), "copy-me"));
    XClassDelete((XClass*)text);

    XJsonValue_setString_utf8(&value, "utf8-直接设置");
    CHECK("value: setString_utf8", XJsonValue_isString(&value) &&
          utf8Eq(XJsonValue_toString(&value), "utf8-直接设置"));

    /* setString_move：create_move 语义 → 源字符串留空壳，由调用方释放。 */
    text = XString_create_utf8("moved");
    XJsonValue_setString_move(&value, text);
    CHECK("value: setString_move", XJsonValue_isString(&value) &&
          utf8Eq(XJsonValue_toString(&value), "moved"));
    XClassDelete((XClass*)text);

    array = XJsonArray_create();
    {
        XJsonValue item;
        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 5);
        XJsonArray_append_move_base((XVector*)array, &item);
    }
    XJsonValue_setArray(&value, array);
    CHECK("value: setArray 拷贝", XJsonValue_isArray(&value) &&
          XJsonArray_size_base(XJsonValue_toArray(&value)) == 1);
    {
        /* 拷贝语义：改源数组不影响已设置的值。 */
        XJsonValue item;
        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 6);
        XJsonArray_append_move_base((XVector*)array, &item);
        CHECK("value: setArray 深拷贝独立",
              XJsonArray_size_base(XJsonValue_toArray(&value)) == 1);
    }
    XClassDelete((XClass*)array);

    array = XJsonArray_create();
    XJsonValue_setArray_move(&value, array);
    CHECK("value: setArray_move", XJsonValue_isArray(&value) &&
          XJsonArray_size_base(XJsonValue_toArray(&value)) == 0);
    XClassDelete((XClass*)array);   /* 源壳 */

    object = XJsonObject_create();
    XJsonObject_insert_keyUtf8_int(object, "k", 1);
    XJsonValue_setObject(&value, object);
    CHECK("value: setObject 拷贝", XJsonValue_isObject(&value) &&
          jsonObjectHasInt(XJsonValue_toObject(&value), "k", 1));
    XJsonObject_insert_keyUtf8_int(object, "k2", 2);
    CHECK("value: setObject 深拷贝独立",
          XJsonObject_size_base(XJsonValue_toObject(&value)) == 1);
    XClassDelete((XClass*)object);

    object = XJsonObject_create();
    XJsonValue_setObject_move(&value, object);
    CHECK("value: setObject_move", XJsonValue_isObject(&value) &&
          XJsonObject_size_base(XJsonValue_toObject(&value)) == 0);
    XClassDelete((XClass*)object);  /* 源壳 */

    XJsonValue_deinit(&value);
    CHECK("value: deinit 后为 Invalid", XJsonValue_type(&value) == XJsonValue_Invalid);
}

static void scenario_json_value_copy_move(void)
{
    XJsonValue* original = XJsonValue_create_int(77);
    XJsonValue* copy = XJsonValue_create_copy(original);
    XJsonValue* moved;
    XJsonValue stack;
    XJsonValue target;

    CHECK("value: create_copy 等值", copy && XJsonValue_equals(copy, original));
    CHECK("value: NULL 拷贝/移动返回 NULL",
          XJsonValue_create_copy(NULL) == NULL &&
          XJsonValue_create_move(NULL) == NULL);
    XJsonValue_setInt(copy, 88);
    CHECK("value: create_copy 独立", XJsonValue_toInt(original, 0) == 77 &&
          XJsonValue_toInt(copy, 0) == 88);

    moved = XJsonValue_create_move(copy);
    CHECK("value: create_move 转移", moved && XJsonValue_isInt(moved) &&
          XJsonValue_toInt(moved, 0) == 88 &&
          XJsonValue_type(copy) == XJsonValue_Invalid);
    XJsonValue_delete(copy);   /* 源空壳 */

    XJsonValue_init(&stack, XJsonValue_Invalid);
    XJsonValue_copy(&stack, moved);
    CHECK("value: 栈上 copy", XJsonValue_equals(&stack, moved));
    XJsonValue_init(&target, XJsonValue_Invalid);
    XJsonValue_move(&target, &stack);
    CHECK("value: 栈上 move 源清零", XJsonValue_isInt(&target) &&
          XJsonValue_type(&stack) == XJsonValue_Invalid);
    XJsonValue_clear(&target);
    CHECK("value: clear 标量", XJsonValue_isInt(&target));
    XJsonValue_deinit(&target);
    XJsonValue_deinit(&stack);

    XJsonValue_delete(moved);
    XJsonValue_delete(original);
}

static void scenario_json_value_container_semantics(void)
{
    XJsonArray* array = XJsonArray_create();
    XJsonObject* object = XJsonObject_create();
    XJsonValue value;

    /* 数组/对象值：deinit 必须释放内部容器。 */
    {
        XJsonValue item;
        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 1);
        XJsonArray_append_move_base((XVector*)array, &item);
    }
    XJsonObject_insert_keyUtf8_int(object, "x", 9);

    XJsonValue_init(&value, XJsonValue_Invalid);
    XJsonValue_setArray(&value, array);
    XJsonValue_deinit(&value);
    CHECK("value: deinit 数组值", XJsonValue_type(&value) == XJsonValue_Invalid);

    XJsonValue_init(&value, XJsonValue_Invalid);
    XJsonValue_setObject(&value, object);
    XJsonValue_clear(&value);
    CHECK("value: clear 对象值", XJsonValue_isObject(&value));

    XJsonValue_deinit(&value);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)object);
}

/* ==================== XJsonObject ==================== */

static void scenario_json_object_inserts(void)
{
    XJsonObject* object = XJsonObject_create();
    XString* key = XString_create_utf8("stringCopy");
    XString* text = XString_create_utf8("value");
    XJsonValue* value = XJsonValue_create_int(123);
    XJsonArray* array = XJsonArray_create();
    XJsonObject* nested = XJsonObject_create();

    CHECK("object: create 空", object && XJsonObject_isEmpty_base(object) &&
          XJsonObject_size_base(object) == 0);

    CHECK("object: insert int", XJsonObject_insert_keyUtf8_int(object, "int", 42));
    CHECK("object: insert double", XJsonObject_insert_keyUtf8_double(object, "double", 2.25));
    CHECK("object: insert bool", XJsonObject_insert_keyUtf8_bool(object, "bool", true));
    CHECK("object: insert null", XJsonObject_insert_keyUtf8_null(object, "null"));
    CHECK("object: insert utf8", XJsonObject_insert_keyUtf8_utf8(object, "utf8", "文本"));
    CHECK("object: insert string 拷贝", XJsonObject_insert_keyUtf8_string(object, "stringCopy", text));
    CHECK("object: insert value 拷贝", XJsonObject_insert_keyUtf8_value(object, "value", value));
    CHECK("object: insert array 拷贝", XJsonObject_insert_keyUtf8_array(object, "array", array));
    CHECK("object: insert object 拷贝", XJsonObject_insert_keyUtf8_object(object, "nested", nested));
    CHECK("object: insert_value_move", XJsonObject_insert_value_move(object, key, value));

    /* insert_value_move 的键与已有 stringCopy 同名：覆盖旧值，大小保持 9。 */
    CHECK("object: 大小 9", XJsonObject_size_base(object) == 9);
    CHECK("object: contains", XJsonObject_contains_keyUtf8(object, "int") &&
          !XJsonObject_contains_keyUtf8(object, "missing"));
    CHECK("object: 回读 int", jsonObjectHasInt(object, "int", 42));
    CHECK("object: 回读 utf8", jsonObjectHasUtf8(object, "utf8", "文本"));
    CHECK("object: 覆盖后 stringCopy 为 int", jsonObjectHasInt(object, "stringCopy", 123));
    CHECK("object: 回读 array", jsonObjectHasArraySize(object, "array", 0));
    CHECK("object: 回读 nested", jsonObjectHasObject(object, "nested"));

    /* 借用接口：缺失键返回 NULL；拷贝接口同样返回 NULL。 */
    CHECK("object: 缺失键借出 NULL",
          XJsonObject_value_keyUtf8(object, "missing") == NULL);
    CHECK("object: 缺失键拷贝 NULL",
          XJsonObject_value_copy_keyUtf8(object, "missing") == NULL);
    /* 拷贝接口：返回独立堆值，必须释放；对象修改后仍有效。 */
    {
        XJsonValue* copied = XJsonObject_value_copy_keyUtf8(object, "int");
        XJsonObject_insert_keyUtf8_int(object, "newKey", 1);
        CHECK("object: copy 接口独立", copied && XJsonValue_isInt(copied) &&
              XJsonValue_toInt(copied, 0) == 42);
        if (copied) XJsonValue_delete(copied);
    }

    XJsonValue_delete(value);
    XClassDelete((XClass*)key);
    XClassDelete((XClass*)text);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)nested);
    XClassDelete((XClass*)object);
}

static void scenario_json_object_move_inserts(void)
{
    XJsonObject* object = XJsonObject_create();
    XString* text = XString_create_utf8("owned-by-object");
    XJsonArray* array = XJsonArray_create();
    XJsonObject* nested = XJsonObject_create();
    XJsonValue item;

    XJsonValue_init(&item, XJsonValue_Int);
    XJsonValue_setInt(&item, 1);
    XJsonArray_append_move_base((XVector*)array, &item);

    /* string_move / array_move / object_move 统一 create_move 语义：
       内容转移，源留空壳，壳由调用方释放。 */
    CHECK("object: insert string_move",
          XJsonObject_insert_keyUtf8_string_move(object, "s", text));
    CHECK("object: string_move 回读", jsonObjectHasUtf8(object, "s", "owned-by-object"));
    CHECK("object: string_move 源已清空", XString_toUtf8_length(text) == 0);
    XClassDelete((XClass*)text);

    CHECK("object: insert array_move",
          XJsonObject_insert_keyUtf8_array_move(object, "a", array));
    CHECK("object: array_move 回读", jsonObjectHasArraySize(object, "a", 1));
    XClassDelete((XClass*)array);

    XJsonObject_insert_keyUtf8_int(nested, "n", 2);
    CHECK("object: insert object_move",
          XJsonObject_insert_keyUtf8_object_move(object, "o", nested));
    CHECK("object: object_move 回读", jsonObjectNestedHasInt(object, "o", "n", 2));
    XClassDelete((XClass*)nested);

    /* 覆盖同名键：旧值必须被释放，大小不增长。 */
    CHECK("object: 覆盖同名键", XJsonObject_insert_keyUtf8_int(object, "s2", 1) &&
          XJsonObject_insert_keyUtf8_int(object, "s2", 2) &&
          XJsonObject_size_base(object) == 4);
    CHECK("object: 覆盖后取新值", jsonObjectHasInt(object, "s2", 2));

    XClassDelete((XClass*)object);
}

static void scenario_json_object_query_remove(void)
{
    XJsonObject* object = XJsonObject_create();
    XJsonValue* taken;
    XVector* keys;

    XJsonObject_insert_keyUtf8_int(object, "a", 1);
    XJsonObject_insert_keyUtf8_int(object, "b", 2);
    XJsonObject_insert_keyUtf8_utf8(object, "c", "three");

    CHECK("object: contains 全量", XJsonObject_contains_keyUtf8(object, "a") &&
          XJsonObject_contains_keyUtf8(object, "b") &&
          XJsonObject_contains_keyUtf8(object, "c"));

    keys = XJsonObject_keys_base(object);
    CHECK("object: keys 数量", keys && XVector_size_base(keys) == 3);
    if (keys) XClassDelete((XClass*)keys);

    CHECK("object: remove a", XJsonObject_remove_keyUtf8(object, "a") &&
          !XJsonObject_contains_keyUtf8(object, "a") &&
          XJsonObject_size_base(object) == 2);
    CHECK("object: remove 缺失键", !XJsonObject_remove_keyUtf8(object, "a"));

    taken = XJsonObject_take_keyUtf8(object, "b");
    CHECK("object: take 值+移除", taken && XJsonValue_toInt(taken, 0) == 2 &&
          !XJsonObject_contains_keyUtf8(object, "b") &&
          XJsonObject_size_base(object) == 1);
    if (taken) XJsonValue_delete(taken);
    taken = XJsonObject_take_keyUtf8(object, "b");
    CHECK("object: take 缺失键返回 Undefined", taken && XJsonValue_isUndefined(taken));
    if (taken) XJsonValue_delete(taken);

    XJsonObject_clear_base(object);
    CHECK("object: clear 后为空", XJsonObject_isEmpty_base(object) &&
          XJsonObject_size_base(object) == 0);
    XClassDelete((XClass*)object);
}

static void scenario_json_object_copy_move_equals(void)
{
    XJsonObject* object = XJsonObject_create();
    XJsonObject* copy;
    XJsonObject* moved;
    XJsonObject stack;
    XJsonObject* nested = XJsonObject_create();

    XJsonObject_insert_keyUtf8_int(object, "k", 1);
    XJsonObject_insert_keyUtf8_utf8(object, "text", "v");
    XJsonObject_insert_keyUtf8_int(nested, "n", 2);
    XJsonObject_insert_keyUtf8_object(object, "nested", nested);
    XClassDelete((XClass*)nested);

    copy = XJsonObject_create_copy(object);
    CHECK("object: create_copy 等值", copy && XJsonObject_equals(copy, object));
    CHECK("object: insert copy 后不等", XJsonObject_insert_keyUtf8_int(copy, "new", 3) &&
          !XJsonObject_equals(copy, object) &&
          XJsonObject_size_base(object) == 3);

    /* COW 独立性：改副本的嵌套对象不得影响源。 */
    XJsonObject_insert_keyUtf8_int(copy, "k", 9);
    CHECK("object: 副本改值不影响源", jsonObjectHasInt(object, "k", 1) &&
          jsonObjectHasInt(copy, "k", 9));

    moved = XJsonObject_create_move(copy);
    CHECK("object: create_move 转移", moved && XJsonObject_size_base(copy) == 0 &&
          XJsonObject_size_base(moved) == 4);
    XClassDelete((XClass*)copy);

    XJsonObject_init(&stack);
    XClassCopy(&stack, moved);
    CHECK("object: 栈上 copy", XJsonObject_equals(&stack, moved));
    XClassDeinit(&stack);

    CHECK("object: NULL equals", XJsonObject_equals(NULL, NULL) &&
          !XJsonObject_equals(object, NULL));

    XClassDelete((XClass*)moved);
    XClassDelete((XClass*)object);
}

/* ==================== XJsonArray ==================== */

static void scenario_json_array_ops(void)
{
    XJsonArray* array = XJsonArray_create();
    XJsonValue item;
    XJsonValue* at;
    XJsonValue* first;
    XJsonValue* last;
    XJsonValue* taken;

    CHECK("array: create 空", array && XJsonArray_isEmpty_base(array) &&
          XJsonArray_size_base(array) == 0);
    CHECK("array: 越界访问", XJsonArray_at(array, 0) == NULL &&
          XJsonArray_at(array, -1) == NULL &&
          XJsonArray_at_const(array, 99) == NULL);

    XJsonValue_init(&item, XJsonValue_Int);
    XJsonValue_setInt(&item, 1);
    CHECK("array: append 拷贝", XJsonArray_append_base((XVector*)array, &item));
    XJsonValue_setInt(&item, 2);
    CHECK("array: append_move", XJsonArray_append_move_base((XVector*)array, &item));
    XJsonValue_setInt(&item, 3);
    CHECK("array: prepend", XJsonArray_prepend_base((XVector*)array, &item));
    XJsonValue_setInt(&item, 4);
    CHECK("array: prepend_move", XJsonArray_prepend_move_base((XVector*)array, &item));

    /* 现在顺序：4,3,1,2 */
    CHECK("array: 大小 4", XJsonArray_size_base(array) == 4);
    at = XJsonArray_at(array, 0);
    CHECK("array: at", at && XJsonValue_toInt(at, 0) == 4);
    CHECK("array: at 负索引", XJsonArray_at(array, -1) &&
          XJsonValue_toInt(XJsonArray_at(array, -1), 0) == 2);
    first = XJsonArray_first(array);
    last = XJsonArray_last(array);
    CHECK("array: first/last", first && last &&
          XJsonValue_toInt(first, 0) == 4 && XJsonValue_toInt(last, 0) == 2);
    if (first) XJsonValue_delete(first);
    if (last) XJsonValue_delete(last);

    {
        XJsonValue contains;
        XJsonValue_init(&contains, XJsonValue_Int);
        XJsonValue_setInt(&contains, 3);
        CHECK("array: contains", XJsonArray_contains(array, &contains));
        XJsonValue_setInt(&contains, 99);
        CHECK("array: contains 缺失", !XJsonArray_contains(array, &contains));
    }

    XJsonValue_setInt(&item, 7);
    CHECK("array: insert", XJsonArray_insert(array, 1, &item));
    CHECK("array: insert 后位置", XJsonValue_toInt(XJsonArray_at(array, 1), 0) == 7);
    XJsonValue_setInt(&item, 8);
    CHECK("array: replace", XJsonArray_replace(array, 1, &item));
    CHECK("array: replace 后位置", XJsonValue_toInt(XJsonArray_at(array, 1), 0) == 8);

    taken = XJsonArray_takeAt(array, 0);
    CHECK("array: takeAt", taken && XJsonValue_toInt(taken, 0) == 4 &&
          XJsonArray_size_base(array) == 4);
    if (taken) XJsonValue_delete(taken);
    taken = XJsonArray_takeAt(array, 99);
    CHECK("array: takeAt 越界返回 Undefined",
          taken && XJsonValue_isUndefined(taken) &&
          XJsonArray_size_base(array) == 4);
    if (taken) XJsonValue_delete(taken);

    XJsonArray_removeAt_base((XVector*)array, 0);
    CHECK("array: removeAt", XJsonArray_size_base(array) == 3);
    XJsonArray_removeAt_base((XVector*)array, 99);
    CHECK("array: removeAt 越界安全", XJsonArray_size_base(array) == 3);

    XJsonArray_clear_base((XVector*)array);
    CHECK("array: clear", XJsonArray_isEmpty_base(array));

    XJsonValue_deinit(&item);
    XClassDelete((XClass*)array);
}

static void scenario_json_array_copy_move_equals(void)
{
    XJsonArray* array = XJsonArray_create();
    XJsonArray* copy;
    XJsonArray* moved;
    XJsonArray stack;
    XJsonValue item;

    XJsonValue_init(&item, XJsonValue_Int);
    XJsonValue_setInt(&item, 1);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setInt(&item, 2);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_deinit(&item);

    copy = XJsonArray_create_copy(array);
    CHECK("array: create_copy 等值", copy && XJsonArray_equals(copy, array));
    {
        XJsonValue extra;
        XJsonValue_init(&extra, XJsonValue_Int);
        XJsonValue_setInt(&extra, 3);
        XJsonArray_append_move_base((XVector*)copy, &extra);
        CHECK("array: copy 独立", XJsonArray_size_base(array) == 2 &&
              XJsonArray_size_base(copy) == 3);
    }

    moved = XJsonArray_create_move(copy);
    CHECK("array: create_move", moved && XJsonArray_size_base(copy) == 0 &&
          XJsonArray_size_base(moved) == 3);
    XClassDelete((XClass*)copy);

    XJsonArray_init(&stack);
    XClassCopy(&stack, moved);
    CHECK("array: 栈上 copy", XJsonArray_equals(&stack, moved));
    XClassDeinit(&stack);

    CHECK("array: NULL equals", XJsonArray_equals(NULL, NULL) &&
          !XJsonArray_equals(array, NULL));

    XClassDelete((XClass*)moved);
    XClassDelete((XClass*)array);
}

static void scenario_json_array_nested(void)
{
    XJsonArray* outer = XJsonArray_create();
    XJsonArray* inner = XJsonArray_create();
    XJsonObject* object = XJsonObject_create();
    XJsonValue value;

    XJsonObject_insert_keyUtf8_int(object, "k", 1);
    XJsonValue_init(&value, XJsonValue_Invalid);
    XJsonValue_setObject(&value, object);
    CHECK("array: append 对象", XJsonArray_append_base((XVector*)outer, &value));
    XJsonValue_deinit(&value);

    XJsonValue_init(&value, XJsonValue_Invalid);
    XJsonValue_setArray(&value, inner);
    CHECK("array: append 数组", XJsonArray_append_base((XVector*)outer, &value));
    XJsonValue_deinit(&value);

    CHECK("array: 嵌套大小", XJsonArray_size_base(outer) == 2);
    CHECK("array: 嵌套对象回读", XJsonValue_isObject(XJsonArray_at_const(outer, 0)) &&
          jsonObjectHasInt(XJsonValue_toObject(XJsonArray_at_const(outer, 0)), "k", 1));
    CHECK("array: 嵌套数组回读", XJsonValue_isArray(XJsonArray_at_const(outer, 1)));

    XClassDelete((XClass*)object);
    XClassDelete((XClass*)inner);
    XClassDelete((XClass*)outer);
}

/* ==================== XJsonDocument ==================== */

static void scenario_json_document_create(void)
{
    XJsonObject* object = XJsonObject_create();
    XJsonArray* array = XJsonArray_create();
    XJsonDocument* doc;
    XJsonDocument* copy;
    XJsonDocument* moved;

    XJsonObject_insert_keyUtf8_int(object, "k", 1);
    {
        XJsonValue item;
        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 2);
        XJsonArray_append_move_base((XVector*)array, &item);
    }

    doc = XJsonDocument_create_object(object);
    CHECK("document: create_object 拷贝", doc && XJsonDocument_isObject(doc) &&
          jsonObjectHasInt(XJsonDocument_object(doc), "k", 1));
    XJsonObject_insert_keyUtf8_int(object, "k2", 2);
    CHECK("document: create_object 深拷贝独立",
          XJsonObject_size_base(XJsonDocument_object(doc)) == 1);
    XClassDelete((XClass*)doc);

    doc = XJsonDocument_create_object_move(object);
    CHECK("document: create_object_move", doc && XJsonDocument_isObject(doc) &&
          XJsonObject_size_base(XJsonDocument_object(doc)) == 2);
    XClassDelete((XClass*)object);   /* 源壳 */
    XClassDelete((XClass*)doc);

    doc = XJsonDocument_create_array(array);
    CHECK("document: create_array 拷贝", doc && XJsonDocument_isArray(doc) &&
          XJsonArray_size_base(XJsonDocument_array(doc)) == 1);
    XClassDelete((XClass*)doc);

    doc = XJsonDocument_create_array_move(array);
    CHECK("document: create_array_move", doc && XJsonDocument_isArray(doc) &&
          XJsonArray_size_base(XJsonDocument_array(doc)) == 1);
    XClassDelete((XClass*)array);    /* 源壳 */

    copy = XJsonDocument_create_copy(doc);
    CHECK("document: create_copy", copy && XJsonDocument_isArray(copy) &&
          XJsonArray_equals(XJsonDocument_array(copy), XJsonDocument_array(doc)));
    moved = XJsonDocument_create_move(copy);
    CHECK("document: create_move", moved && XJsonDocument_isArray(moved) &&
          XJsonDocument_isEmpty(copy));
    XClassDelete((XClass*)copy);
    XClassDelete((XClass*)moved);
    XClassDelete((XClass*)doc);

    doc = XJsonDocument_create();
    CHECK("document: create 为空", doc && XJsonDocument_isEmpty(doc) &&
          XJsonDocument_isNull(doc) && !XJsonDocument_isObject(doc) &&
          !XJsonDocument_isArray(doc) && XJsonDocument_root(doc) != NULL &&
          XJsonDocument_object(doc) == NULL && XJsonDocument_array(doc) == NULL);
    XClassDelete((XClass*)doc);
}

static void scenario_json_document_root_api(void)
{
    XJsonDocument* doc = XJsonDocument_create();
    XJsonObject* object = XJsonObject_create();
    XJsonArray* array = XJsonArray_create();
    XJsonValue value;

    XJsonObject_insert_keyUtf8_int(object, "a", 1);
    CHECK("document: setObject", XJsonDocument_setObject(doc, object) &&
          XJsonDocument_isObject(doc) &&
          jsonObjectHasInt(XJsonDocument_object(doc), "a", 1));
    CHECK("document: object 后 array 为空", XJsonDocument_array(doc) == NULL);

    CHECK("document: setArray", XJsonDocument_setArray(doc, array) &&
          XJsonDocument_isArray(doc));
    CHECK("document: array 后 object 为空", XJsonDocument_object(doc) == NULL);

    XJsonDocument_setObject_move(doc, object);
    CHECK("document: setObject_move", XJsonDocument_isObject(doc) &&
          XJsonObject_size_base(XJsonDocument_object(doc)) == 1);
    XClassDelete((XClass*)object);

    XJsonDocument_setArray_move(doc, array);
    CHECK("document: setArray_move", XJsonDocument_isArray(doc));
    XClassDelete((XClass*)array);

    XJsonValue_init(&value, XJsonValue_Int);
    XJsonValue_setInt(&value, 5);
    XJsonDocument_setRoot(doc, &value);
    CHECK("document: setRoot 标量", XJsonDocument_isNull(doc) == false &&
          XJsonDocument_root_const(doc) &&
          XJsonValue_toInt(XJsonDocument_root_const(doc), 0) == 5 &&
          !XJsonDocument_isObject(doc) && !XJsonDocument_isArray(doc));

    XJsonValue_setInt(&value, 6);
    XJsonDocument_setRoot_move(doc, &value);
    CHECK("document: setRoot_move", XJsonValue_toInt(XJsonDocument_root_const(doc), 0) == 6 &&
          XJsonValue_type(&value) == XJsonValue_Invalid);
    XJsonValue_deinit(&value);

    XJsonDocument_clear(doc);
    CHECK("document: clear", XJsonDocument_isEmpty(doc) &&
          XJsonDocument_isNull(doc));
    XClassDelete((XClass*)doc);
}

static XJsonObject* makeRichObject(void)
{
    XJsonObject* object = XJsonObject_create();
    XJsonArray* array = XJsonArray_create();
    XJsonObject* nested = XJsonObject_create();
    XJsonValue item;

    XJsonValue_init(&item, XJsonValue_Int);
    XJsonValue_setInt(&item, 1);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setDouble(&item, 2.5);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setBool(&item, true);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setNull(&item);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setString_utf8(&item, "元素");
    XJsonArray_append_move_base((XVector*)array, &item);

    XJsonObject_insert_keyUtf8_int(nested, "n", 7);
    XJsonObject_insert_keyUtf8_int(object, "int", 42);
    XJsonObject_insert_keyUtf8_double(object, "double", 1.5);
    XJsonObject_insert_keyUtf8_bool(object, "true", true);
    XJsonObject_insert_keyUtf8_bool(object, "false", false);
    XJsonObject_insert_keyUtf8_null(object, "null");
    XJsonObject_insert_keyUtf8_utf8(object, "text", "hello 中文 \"quoted\" \\ \n\t");
    XJsonObject_insert_keyUtf8_array(object, "array", array);
    XJsonObject_insert_keyUtf8_object(object, "nested", nested);

    XClassDelete((XClass*)array);
    XClassDelete((XClass*)nested);
    return object;
}

static void scenario_json_document_serialize(void)
{
    XJsonObject* object = makeRichObject();
    XJsonDocument* doc = XJsonDocument_create_object(object);
    XByteArray* compact = XJsonDocument_toJson(doc, XJsonDocument_Compact);
    XByteArray* indented = XJsonDocument_toJson(doc, XJsonDocument_Indented);
    XString* text = XJsonDocument_toString(doc, XJsonDocument_Compact);
    XJsonDocument* parsed;
    XJsonValue* rootValue;

    CHECK("document: toJson 紧凑无换行", compact &&
          XByteArray_size_base(compact) > 0 &&
          memchr(XByteArray_constData(compact), '\n',
                 XByteArray_size_base(compact)) == NULL);
    CHECK("document: toJson 缩进含换行", indented &&
          XByteArray_size_base(indented) > XByteArray_size_base(compact) &&
          memchr(XByteArray_constData(indented), '\n',
                 XByteArray_size_base(indented)) != NULL);

    parsed = XJsonDocument_fromJson(compact);
    CHECK("document: fromJson 往返", parsed && XJsonDocument_isObject(parsed) &&
          XJsonObject_equals(XJsonDocument_object(parsed), object));
    XClassDelete((XClass*)parsed);

    parsed = XJsonDocument_fromString(text);
    CHECK("document: fromString 往返", parsed && XJsonDocument_isObject(parsed) &&
          XJsonObject_equals(XJsonDocument_object(parsed), object));
    XClassDelete((XClass*)parsed);

    CHECK("document: toString 与 toJson 等价", bytesTextEq(compact, text));

    /* 根为标量/数组/空文档的序列化。 */
    rootValue = XJsonDocument_root(doc);
    XJsonValue_setInt(rootValue, 9);
    {
        XString* scalar = XJsonDocument_toString(doc, XJsonDocument_Compact);
        CHECK("document: 标量根序列化", scalar && utf8Eq(scalar, "9"));
        if (scalar) XClassDelete((XClass*)scalar);
    }
    XJsonDocument_clear(doc);
    {
        XString* empty = XJsonDocument_toString(doc, XJsonDocument_Compact);
        CHECK("document: 空文档序列化", empty != NULL);
        if (empty) XClassDelete((XClass*)empty);
    }

    XClassDelete((XClass*)text);
    XClassDelete((XClass*)compact);
    XClassDelete((XClass*)indented);
    XClassDelete((XClass*)doc);
    XClassDelete((XClass*)object);
}

static void scenario_json_document_parse_errors(void)
{
    struct ParseCase {
        const char* text;
        XJsonParseErrorCode code;
    };
    static const struct ParseCase cases[] = {
        { "{", XJsonParseError_UnterminatedObject },
        { "[", XJsonParseError_UnterminatedArray },
        { "{\"a\" 1}", XJsonParseError_MissingNameSeparator },
        { "[1 2]", XJsonParseError_MissingValueSeparator },
        { "x", XJsonParseError_IllegalValue },
        { "{\"a\":1e}", XJsonParseError_IllegalNumber },
        { "{\"a\":\"\\q\"}", XJsonParseError_IllegalEscapeSequence },
        { "{\"a\":\"text", XJsonParseError_UnterminatedString },
        { "{}x", XJsonParseError_GarbageAtEnd }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        XString* s = XString_create_utf8(cases[i].text);
        XJsonParseError error;
        XJsonDocument* doc = s ? XJsonDocument_fromString_ex(s, &error) : NULL;
        CHECK("document: 解析错误码", doc == NULL && error.error == cases[i].code &&
              error.offset >= 0);
        if (doc) XClassDelete((XClass*)doc);
        if (s) XClassDelete((XClass*)s);
    }

    /* NULL / 空输入。 */
    {
        XJsonParseError error;
        CHECK("document: NULL 输入", XJsonDocument_fromString(NULL) == NULL &&
              XJsonDocument_fromString_ex(NULL, &error) == NULL);
    }
    {
        XString* empty = XString_create_utf8("");
        XJsonDocument* doc = empty ? XJsonDocument_fromString(empty) : NULL;
        CHECK("document: 空输入非法", doc == NULL);
        if (doc) XClassDelete((XClass*)doc);
        if (empty) XClassDelete((XClass*)empty);
    }

    /* 合法边界：空对象/空数组/深嵌套。 */
    {
        XJsonDocument* doc = parseJsonUtf8("{}");
        CHECK("document: 空对象解析", doc && XJsonDocument_isObject(doc) &&
              XJsonObject_size_base(XJsonDocument_object(doc)) == 0);
        if (doc) XClassDelete((XClass*)doc);
    }
    {
        XJsonDocument* doc = parseJsonUtf8("[]");
        CHECK("document: 空数组解析", doc && XJsonDocument_isArray(doc) &&
              XJsonArray_size_base(XJsonDocument_array(doc)) == 0);
        if (doc) XClassDelete((XClass*)doc);
    }
    {
        char deep[1024 + 1];
        size_t i2;
        XJsonDocument* doc;
        for (i2 = 0; i2 < 512; ++i2) deep[i2] = '[';
        for (i2 = 0; i2 < 512; ++i2) deep[512 + i2] = ']';
        deep[1024] = '\0';
        doc = parseJsonUtf8(deep);
        CHECK("document: 512 层嵌套", doc && XJsonDocument_isArray(doc));
        if (doc) XClassDelete((XClass*)doc);
    }
}

/* ==================== XBsonValue ==================== */

/** @brief BSON 值 → JSON → BSON 后类型保持不变。 */
static bool bsonJsonRoundtripKeepsType(const XBsonValue* value)
{
    XJsonValue* json = XBsonValue_to_json(value);
    XBsonValue* back = json ? XBsonValue_from_json(json) : NULL;
    bool ok = back != NULL && XBsonValue_type(back) == XBsonValue_type(value);
    if (back) XBsonValue_delete(back);
    if (json) XJsonValue_delete(json);
    return ok;
}

/** @brief 把堆 BSON 值移动插入文档，并释放移动后的空壳。 */
static bool bsonDocInsertValueMove(XBsonDocument* doc, const char* key,
                                   XBsonValue* value)
{
    bool ok = XBsonDocument_insert_keyUtf8_value_move(doc, key, value);
    if (value) XBsonValue_delete(value);
    return ok;
}

/** @brief 追加 BSON 值（拷贝语义），并释放调用方创建的堆值。 */
static bool bsonArrayAppendValue(XBsonArray* array, XBsonValue* value)
{
    bool ok = XBsonArray_append_base((XVector*)array, value);
    if (value) XBsonValue_delete(value);
    return ok;
}

static void scenario_bson_value_all_types(void)
{
    const uint8_t oid[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    uint8_t decimal[16] = {0};
    XByteArray* binary = XByteArray_create_with_data("\x01\x02\x03", 3);
    XString* text = XString_create_utf8("bson-text");
    XString* symbol = XString_create_utf8("bson-symbol");
    XString* pattern = XString_create_utf8("^ab.*");
    XString* options = XString_create_utf8("im");
    XString* ns = XString_create_utf8("db.coll");
    XString* code = XString_create_utf8("function(){}");
    XBsonDocument* subdoc = XBsonDocument_create();
    XBsonArray* subarr = XBsonArray_create();
    XBsonDocument* scope = XBsonDocument_create();
    XBsonValue* vnull;
    XBsonValue* vundef;
    XBsonValue* vbool;
    XBsonValue* vdouble;
    XBsonValue* vint32;
    XBsonValue* vint64;
    XBsonValue* vstring;
    XBsonValue* vsymbol;
    XBsonValue* vdoc;
    XBsonValue* varr;
    XBsonValue* vbinary;
    XBsonValue* void_;
    XBsonValue* vdate;
    XBsonValue* vregex;
    XBsonValue* vdbptr;
    XBsonValue* vjs;
    XBsonValue* vscope;
    XBsonValue* vts;
    XBsonValue* vdec;
    XBsonValue* vmin;
    XBsonValue* vmax;

    /* 子文档/作用域先填内容，再被 XBsonValue_create_document 深拷贝。 */
    XBsonDocument_insert_keyUtf8_int32(subdoc, "k", 1);
    XBsonDocument_insert_keyUtf8_int32(scope, "s", 2);

    vnull = XBsonValue_create_null();
    vundef = XBsonValue_create_undefined();
    vbool = XBsonValue_create_bool(true);
    vdouble = XBsonValue_create_double(2.5);
    vint32 = XBsonValue_create_int32(-7);
    vint64 = XBsonValue_create_int64(INT64_C(1) << 40);
    vstring = XBsonValue_create_string(text);
    vsymbol = XBsonValue_create_symbol(symbol);
    vdoc = XBsonValue_create_document(subdoc);
    varr = XBsonValue_create_array(subarr);
    vbinary = XBsonValue_create_binary(XBSON_BINARY_GENERIC, binary);
    void_ = XBsonValue_create_object_id(oid);
    vdate = XBsonValue_create_datetime(1700000000123);
    vregex = XBsonValue_create_regex(pattern, options);
    vdbptr = XBsonValue_create_dbpointer(ns, oid);
    vjs = XBsonValue_create_javascript(code);
    vscope = XBsonValue_create_javascript_scope(code, scope);
    vts = XBsonValue_create_timestamp(9, 123456);
    vdec = XBsonValue_create_decimal128(decimal);
    vmin = XBsonValue_create_min_key();
    vmax = XBsonValue_create_max_key();

    CHECK("bsonvalue: null", vnull && XBsonValue_isNull(vnull) &&
          XBsonValue_type(vnull) == XBSON_TYPE_NULL);
    CHECK("bsonvalue: undefined", vundef && XBsonValue_isUndefined(vundef));
    CHECK("bsonvalue: bool", vbool && XBsonValue_isBool(vbool) &&
          XBsonValue_toBool(vbool, false));
    CHECK("bsonvalue: double", vdouble && XBsonValue_isDouble(vdouble) &&
          XBsonValue_toDouble(vdouble, 0.0) == 2.5);
    CHECK("bsonvalue: int32", vint32 && XBsonValue_isInt32(vint32) &&
          XBsonValue_toInt32(vint32, 0) == -7);
    CHECK("bsonvalue: int64", vint64 && XBsonValue_isInt64(vint64) &&
          XBsonValue_toInt64(vint64, 0) == (INT64_C(1) << 40));
    CHECK("bsonvalue: string", vstring && XBsonValue_isString(vstring) &&
          utf8Eq(XBsonValue_toString(vstring), "bson-text"));
    CHECK("bsonvalue: symbol", vsymbol && XBsonValue_isSymbol(vsymbol) &&
          utf8Eq(XBsonValue_toString(vsymbol), "bson-symbol"));
    CHECK("bsonvalue: document", vdoc && XBsonValue_isDocument(vdoc) &&
          XBsonValue_toDocument(vdoc) &&
          XBsonDocument_contains_keyUtf8(XBsonValue_toDocument(vdoc), "k"));
    CHECK("bsonvalue: array", varr && XBsonValue_isArray(varr) &&
          XBsonValue_toArray(varr) &&
          XBsonArray_size_base(XBsonValue_toArray(varr)) == 0);
    {
        XBsonBinarySubtype subtype = XBSON_BINARY_GENERIC;
        const XByteArray* data = vbinary ? XBsonValue_toBinary(vbinary, &subtype) : NULL;
        CHECK("bsonvalue: binary", vbinary && XBsonValue_isBinary(vbinary) && data &&
              subtype == XBSON_BINARY_GENERIC && XByteArray_size_base(data) == 3);
    }
    CHECK("bsonvalue: objectId", void_ && XBsonValue_isObjectId(void_) &&
          XBsonValue_toObjectId(void_) &&
          memcmp(XBsonValue_toObjectId(void_), oid, 12) == 0);
    CHECK("bsonvalue: datetime", vdate && XBsonValue_isDatetime(vdate) &&
          XBsonValue_toDatetime(vdate, 0) == 1700000000123);
    CHECK("bsonvalue: regex", vregex && XBsonValue_isRegex(vregex) &&
          utf8Eq(XBsonValue_toRegexPattern(vregex), "^ab.*") &&
          utf8Eq(XBsonValue_toRegexOptions(vregex), "im"));
    CHECK("bsonvalue: dbpointer", vdbptr && XBsonValue_isDbpointer(vdbptr) &&
          utf8Eq(XBsonValue_toDbpointerNamespace(vdbptr), "db.coll") &&
          memcmp(XBsonValue_toDbpointerObjectId(vdbptr), oid, 12) == 0);
    CHECK("bsonvalue: javascript", vjs && XBsonValue_isJavascript(vjs) &&
          utf8Eq(XBsonValue_toString(vjs), "function(){}"));
    CHECK("bsonvalue: javascript_scope", vscope && XBsonValue_isJavascriptScope(vscope) &&
          utf8Eq(XBsonValue_toString(vscope), "function(){}") &&
          XBsonValue_toJavascriptScope(vscope) &&
          XBsonDocument_contains_keyUtf8(XBsonValue_toJavascriptScope(vscope), "s"));
    CHECK("bsonvalue: timestamp", vts && XBsonValue_isTimestamp(vts) &&
          XBsonValue_timestampIncrement(vts) == 9 &&
          XBsonValue_timestamp(vts) == 123456);
    CHECK("bsonvalue: decimal128", vdec && XBsonValue_isDecimal128(vdec) &&
          XBsonValue_toDecimal128(vdec) &&
          memcmp(XBsonValue_toDecimal128(vdec), decimal, 16) == 0);
    CHECK("bsonvalue: min_key", vmin && XBsonValue_isMinKey(vmin));
    CHECK("bsonvalue: max_key", vmax && XBsonValue_isMaxKey(vmax));
    CHECK("bsonvalue: is_type", XBsonValue_is_type(vbool, XBSON_TYPE_BOOL) &&
          !XBsonValue_is_type(vbool, XBSON_TYPE_INT32));
    CHECK("bsonvalue: 非法类型", XBsonValue_create((XBsonType)0x42) == NULL);

    /* 类型不匹配的取值缺省语义。 */
    CHECK("bsonvalue: 取值缺省", XBsonValue_toBool(vint32, true) &&
          XBsonValue_toDouble(vint32, 1.5) == -7.0 &&
          XBsonValue_toDouble(vint64, 0.0) == (double)(INT64_C(1) << 40) &&
          XBsonValue_toDouble(vnull, 1.5) == 1.5 &&
          XBsonValue_toInt32(vnull, 7) == 7 &&
          XBsonValue_toInt64(vnull, 9) == 9 &&
          XBsonValue_toString(vint32) == NULL &&
          XBsonValue_toDocument(vnull) == NULL &&
          XBsonValue_toArray(vnull) == NULL &&
          XBsonValue_toBinary(vnull, NULL) == NULL &&
          XBsonValue_toObjectId(vnull) == NULL &&
          XBsonValue_toDatetime(vnull, -1) == -1);

    /* JSON 桥接：每种类型都能「BSON→JSON→BSON」保持类型。 */
    CHECK("bsonvalue: JSON 桥接 null", bsonJsonRoundtripKeepsType(vnull));
    CHECK("bsonvalue: JSON 桥接 undefined", bsonJsonRoundtripKeepsType(vundef));
    CHECK("bsonvalue: JSON 桥接 bool", bsonJsonRoundtripKeepsType(vbool));
    CHECK("bsonvalue: JSON 桥接 double", bsonJsonRoundtripKeepsType(vdouble));
    CHECK("bsonvalue: JSON 桥接 int32", bsonJsonRoundtripKeepsType(vint32));
    CHECK("bsonvalue: JSON 桥接 int64", bsonJsonRoundtripKeepsType(vint64));
    CHECK("bsonvalue: JSON 桥接 string", bsonJsonRoundtripKeepsType(vstring));
    CHECK("bsonvalue: JSON 桥接 symbol", bsonJsonRoundtripKeepsType(vsymbol));
    CHECK("bsonvalue: JSON 桥接 document", bsonJsonRoundtripKeepsType(vdoc));
    CHECK("bsonvalue: JSON 桥接 array", bsonJsonRoundtripKeepsType(varr));
    CHECK("bsonvalue: JSON 桥接 binary", bsonJsonRoundtripKeepsType(vbinary));
    CHECK("bsonvalue: JSON 桥接 objectId", bsonJsonRoundtripKeepsType(void_));
    CHECK("bsonvalue: JSON 桥接 datetime", bsonJsonRoundtripKeepsType(vdate));
    CHECK("bsonvalue: JSON 桥接 regex", bsonJsonRoundtripKeepsType(vregex));
    CHECK("bsonvalue: JSON 桥接 dbpointer", bsonJsonRoundtripKeepsType(vdbptr));
    CHECK("bsonvalue: JSON 桥接 javascript", bsonJsonRoundtripKeepsType(vjs));
    CHECK("bsonvalue: JSON 桥接 javascript_scope", bsonJsonRoundtripKeepsType(vscope));
    CHECK("bsonvalue: JSON 桥接 timestamp", bsonJsonRoundtripKeepsType(vts));
    CHECK("bsonvalue: JSON 桥接 decimal128", bsonJsonRoundtripKeepsType(vdec));
    CHECK("bsonvalue: JSON 桥接 min_key", bsonJsonRoundtripKeepsType(vmin));
    CHECK("bsonvalue: JSON 桥接 max_key", bsonJsonRoundtripKeepsType(vmax));

    XBsonValue_delete(vnull);
    XBsonValue_delete(vundef);
    XBsonValue_delete(vbool);
    XBsonValue_delete(vdouble);
    XBsonValue_delete(vint32);
    XBsonValue_delete(vint64);
    XBsonValue_delete(vstring);
    XBsonValue_delete(vsymbol);
    XBsonValue_delete(vdoc);
    XBsonValue_delete(varr);
    XBsonValue_delete(vbinary);
    XBsonValue_delete(void_);
    XBsonValue_delete(vdate);
    XBsonValue_delete(vregex);
    XBsonValue_delete(vdbptr);
    XBsonValue_delete(vjs);
    XBsonValue_delete(vscope);
    XBsonValue_delete(vts);
    XBsonValue_delete(vdec);
    XBsonValue_delete(vmin);
    XBsonValue_delete(vmax);
    XClassDelete((XClass*)subdoc);
    XClassDelete((XClass*)subarr);
    XClassDelete((XClass*)scope);
    XClassDelete((XClass*)binary);
    XClassDelete((XClass*)text);
    XClassDelete((XClass*)symbol);
    XClassDelete((XClass*)pattern);
    XClassDelete((XClass*)options);
    XClassDelete((XClass*)ns);
    XClassDelete((XClass*)code);
}

static void scenario_bson_value_setters(void)
{
    uint8_t oid[12] = {9,8,7,6,5,4,3,2,1,0,1,2};
    uint8_t decimal[16] = {1};
    XBsonValue value;
    XBsonValue copied;
    XBsonValue moved;
    XString* text;
    XString* pattern;
    XString* options;
    XString* ns;
    XByteArray* binary;
    XBsonDocument* doc;
    XBsonArray* array;

    XBsonValue_init(&value, XBSON_TYPE_NULL);
    CHECK("bsonvalue: init null", XBsonValue_isNull(&value));
    {
        XBsonValue probe;
        XBsonValue* created;
        XBsonValue_init(&probe, XBSON_TYPE_STRING);
        /* 与 XJsonValue_init 统一：init 只设类型标签，不分配载荷。 */
        CHECK("bsonvalue: init 不分配载荷", XBsonValue_toString(&probe) == NULL);
        XBsonValue_clear(&probe);   /* NULL 载荷下 clear 必须安全 */
        XBsonValue_deinit(&probe);
        created = XBsonValue_create(XBSON_TYPE_STRING);
        CHECK("bsonvalue: create 分配空载荷", created &&
              XBsonValue_toString(created) != NULL &&
              XString_toUtf8_length(XBsonValue_toString(created)) == 0);
        XBsonValue_delete(created);
    }
    XBsonValue_setUndefined(&value);
    CHECK("bsonvalue: setUndefined", XBsonValue_isUndefined(&value));
    XBsonValue_setBool(&value, true);
    CHECK("bsonvalue: setBool", XBsonValue_isBool(&value) && XBsonValue_toBool(&value, false));
    XBsonValue_setDouble(&value, 1.25);
    CHECK("bsonvalue: setDouble", XBsonValue_isDouble(&value) &&
          XBsonValue_toDouble(&value, 0.0) == 1.25);
    XBsonValue_setInt32(&value, 5);
    CHECK("bsonvalue: setInt32", XBsonValue_isInt32(&value) &&
          XBsonValue_toInt32(&value, 0) == 5);
    XBsonValue_setInt64(&value, INT64_C(5) << 33);
    CHECK("bsonvalue: setInt64", XBsonValue_isInt64(&value) &&
          XBsonValue_toInt64(&value, 0) == (INT64_C(5) << 33));

    XBsonValue_setString_utf8(&value, "直接设置");
    CHECK("bsonvalue: setString_utf8", XBsonValue_isString(&value) &&
          utf8Eq(XBsonValue_toString(&value), "直接设置"));
    text = XString_create_utf8("拷贝");
    XBsonValue_setString(&value, text);
    CHECK("bsonvalue: setString 拷贝", XBsonValue_isString(&value) &&
          utf8Eq(XBsonValue_toString(&value), "拷贝"));
    XClassDelete((XClass*)text);
    text = XString_create_utf8("移动");
    XBsonValue_setString_move(&value, text);
    CHECK("bsonvalue: setString_move", XBsonValue_isString(&value) &&
          utf8Eq(XBsonValue_toString(&value), "移动"));
    XClassDelete((XClass*)text);

    text = XString_create_utf8("sym");
    XBsonValue_setSymbol(&value, text);
    CHECK("bsonvalue: setSymbol", XBsonValue_isSymbol(&value) &&
          utf8Eq(XBsonValue_toString(&value), "sym"));
    XClassDelete((XClass*)text);

    doc = XBsonDocument_create();
    XBsonDocument_insert_keyUtf8_int32(doc, "k", 1);
    XBsonValue_setDocument(&value, doc);
    CHECK("bsonvalue: setDocument 拷贝", XBsonValue_isDocument(&value) &&
          XBsonDocument_contains_keyUtf8(XBsonValue_toDocument(&value), "k"));
    XBsonDocument_insert_keyUtf8_int32(doc, "k2", 2);
    CHECK("bsonvalue: setDocument 深拷贝独立",
          XBsonDocument_size_base(XBsonValue_toDocument(&value)) == 1);
    XClassDelete((XClass*)doc);

    doc = XBsonDocument_create();
    XBsonValue_setDocument_move(&value, doc);
    CHECK("bsonvalue: setDocument_move", XBsonValue_isDocument(&value) &&
          XBsonDocument_size_base(XBsonValue_toDocument(&value)) == 0);
    XClassDelete((XClass*)doc);

    array = XBsonArray_create();
    XBsonValue_setArray(&value, array);
    CHECK("bsonvalue: setArray 拷贝", XBsonValue_isArray(&value));
    XClassDelete((XClass*)array);
    array = XBsonArray_create();
    XBsonValue_setArray_move(&value, array);
    CHECK("bsonvalue: setArray_move", XBsonValue_isArray(&value));
    XClassDelete((XClass*)array);

    binary = XByteArray_create_with_data("\x0a\x0b", 2);
    XBsonValue_setBinary(&value, XBSON_BINARY_MD5, binary);
    CHECK("bsonvalue: setBinary", XBsonValue_isBinary(&value) &&
          XBsonValue_toBinary(&value, NULL) &&
          XByteArray_size_base(XBsonValue_toBinary(&value, NULL)) == 2);
    XClassDelete((XClass*)binary);
    binary = XByteArray_create_with_data("\x0c", 1);
    XBsonValue_setBinary_move(&value, XBSON_BINARY_UUID, binary);
    CHECK("bsonvalue: setBinary_move", XBsonValue_isBinary(&value));
    XClassDelete((XClass*)binary);
    {
        XBsonBinarySubtype subtype = XBSON_BINARY_GENERIC;
        XBsonValue_toBinary(&value, &subtype);
        CHECK("bsonvalue: setBinary_move 子类型", subtype == XBSON_BINARY_UUID);
    }

    XBsonValue_setObjectId(&value, oid);
    CHECK("bsonvalue: setObjectId", XBsonValue_isObjectId(&value) &&
          memcmp(XBsonValue_toObjectId(&value), oid, 12) == 0);
    XBsonValue_setDatetime(&value, -55);
    CHECK("bsonvalue: setDatetime", XBsonValue_isDatetime(&value) &&
          XBsonValue_toDatetime(&value, 0) == -55);

    pattern = XString_create_utf8("p");
    options = XString_create_utf8("i");
    XBsonValue_setRegex(&value, pattern, options);
    CHECK("bsonvalue: setRegex", XBsonValue_isRegex(&value) &&
          utf8Eq(XBsonValue_toRegexPattern(&value), "p") &&
          utf8Eq(XBsonValue_toRegexOptions(&value), "i"));
    XClassDelete((XClass*)pattern);
    XClassDelete((XClass*)options);
    ns = XString_create_utf8("ns");
    XBsonValue_setDbpointer(&value, ns, oid);
    CHECK("bsonvalue: setDbpointer", XBsonValue_isDbpointer(&value) &&
          utf8Eq(XBsonValue_toDbpointerNamespace(&value), "ns"));
    XClassDelete((XClass*)ns);
    text = XString_create_utf8("js");
    XBsonValue_setJavascript(&value, text);
    CHECK("bsonvalue: setJavascript", XBsonValue_isJavascript(&value) &&
          utf8Eq(XBsonValue_toString(&value), "js"));
    XClassDelete((XClass*)text);
    doc = XBsonDocument_create();
    text = XString_create_utf8("js2");
    XBsonValue_setJavascript_scope(&value, text, doc);
    CHECK("bsonvalue: setJavascript_scope", XBsonValue_isJavascriptScope(&value) &&
          XBsonValue_toJavascriptScope(&value) != NULL);
    XClassDelete((XClass*)text);
    XClassDelete((XClass*)doc);
    XBsonValue_setTimestamp(&value, 3, 4);
    CHECK("bsonvalue: setTimestamp", XBsonValue_isTimestamp(&value) &&
          XBsonValue_timestampIncrement(&value) == 3 &&
          XBsonValue_timestamp(&value) == 4);
    XBsonValue_setDecimal128(&value, decimal);
    CHECK("bsonvalue: setDecimal128", XBsonValue_isDecimal128(&value) &&
          XBsonValue_toDecimal128(&value) != NULL);
    XBsonValue_setMin_key(&value);
    CHECK("bsonvalue: setMin_key", XBsonValue_isMinKey(&value));
    XBsonValue_setMax_key(&value);
    CHECK("bsonvalue: setMax_key", XBsonValue_isMaxKey(&value));

    /* 拷贝/移动（栈上 + 堆上）。 */
    XBsonValue_init(&copied, XBSON_TYPE_NULL);
    XBsonValue_copy(&copied, &value);
    CHECK("bsonvalue: copy", XBsonValue_isMaxKey(&copied));
    XBsonValue_init(&moved, XBSON_TYPE_NULL);
    XBsonValue_move(&moved, &copied);
    CHECK("bsonvalue: move 源清零", XBsonValue_isMaxKey(&moved) &&
          XBsonValue_type(&copied) == 0);
    XBsonValue_deinit(&moved);
    XBsonValue_deinit(&copied);

    XBsonValue_setString_utf8(&value, "heap-copy");
    {
        XBsonValue* heapCopy = XBsonValue_create_copy(&value);
        XBsonValue* heapMove;
        CHECK("bsonvalue: create_copy", heapCopy &&
              utf8Eq(XBsonValue_toString(heapCopy), "heap-copy"));
        heapMove = XBsonValue_create_move(heapCopy);
        CHECK("bsonvalue: create_move", heapMove &&
              XBsonValue_type(heapCopy) == 0 &&
              utf8Eq(XBsonValue_toString(heapMove), "heap-copy"));
        XBsonValue_delete(heapCopy);
        XBsonValue_delete(heapMove);
    }

    XBsonValue_clear(&value);
    CHECK("bsonvalue: clear 保留类型", XBsonValue_isString(&value) &&
          XString_toUtf8_length(XBsonValue_toString(&value)) == 0);
    XBsonValue_deinit(&value);
    CHECK("bsonvalue: deinit 清零", XBsonValue_type(&value) == 0);
}

/* ==================== XBsonDocument ==================== */

static XBsonDocument* makeRichBsonDocument(void)
{
    const uint8_t oid[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
    const uint8_t decimal[16] = {0x10};
    XBsonDocument* doc = XBsonDocument_create();
    XBsonDocument* nested = XBsonDocument_create();
    XBsonArray* array = XBsonArray_create();
    XByteArray* binary = XByteArray_create_with_data("\xde\xad", 2);
    XString* text = XString_create_utf8("string-value");
    XString* pattern = XString_create_utf8("^x$");
    XString* options = XString_create_utf8("i");
    XString* ns = XString_create_utf8("db.c");
    XString* code = XString_create_utf8("code");

    XBsonDocument_insert_keyUtf8_double(doc, "double", 2.5);
    XBsonDocument_insert_keyUtf8_int32(doc, "int32", -3);
    XBsonDocument_insert_keyUtf8_int64(doc, "int64", INT64_C(1) << 40);
    XBsonDocument_insert_keyUtf8_string(doc, "string", text);
    XBsonDocument_insert_keyUtf8_utf8(doc, "utf8", "utf8-value");
    XBsonDocument_insert_keyUtf8_bool(doc, "bool", true);
    XBsonDocument_insert_keyUtf8_null(doc, "null");
    XBsonDocument_insert_keyUtf8_int32(nested, "nested", 1);
    XBsonDocument_insert_keyUtf8_document(doc, "doc", nested);
    bsonArrayAppendValue(array, XBsonValue_create_int32(1));
    XBsonDocument_insert_keyUtf8_array(doc, "array", array);
    bsonDocInsertValueMove(doc, "binary",
                           XBsonValue_create_binary(XBSON_BINARY_GENERIC, binary));
    bsonDocInsertValueMove(doc, "oid", XBsonValue_create_object_id(oid));
    bsonDocInsertValueMove(doc, "date", XBsonValue_create_datetime(42));
    bsonDocInsertValueMove(doc, "regex",
                           XBsonValue_create_regex(pattern, options));
    bsonDocInsertValueMove(doc, "dbpointer",
                           XBsonValue_create_dbpointer(ns, oid));
    bsonDocInsertValueMove(doc, "javascript",
                           XBsonValue_create_javascript(code));
    bsonDocInsertValueMove(doc, "symbol", XBsonValue_create_symbol(text));
    bsonDocInsertValueMove(doc, "timestamp",
                           XBsonValue_create_timestamp(1, 2));
    bsonDocInsertValueMove(doc, "decimal128",
                           XBsonValue_create_decimal128(decimal));
    bsonDocInsertValueMove(doc, "minKey", XBsonValue_create_min_key());
    bsonDocInsertValueMove(doc, "maxKey", XBsonValue_create_max_key());
    bsonDocInsertValueMove(doc, "undefined", XBsonValue_create_undefined());

    XClassDelete((XClass*)nested);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)binary);
    XClassDelete((XClass*)text);
    XClassDelete((XClass*)pattern);
    XClassDelete((XClass*)options);
    XClassDelete((XClass*)ns);
    XClassDelete((XClass*)code);
    return doc;
}

static bool bytesSame(const XByteArray* left, const XByteArray* right)
{
    if (!left || !right)
        return left == right;
    return XByteArray_size_base(left) == XByteArray_size_base(right) &&
           memcmp(XByteArray_constData(left), XByteArray_constData(right),
                  XByteArray_size_base(left)) == 0;
}

static void scenario_bson_document_inserts(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XString* key = XString_create_utf8("key");
    XBsonValue* value = XBsonValue_create_int32(7);
    XBsonDocument* nested = XBsonDocument_create();
    XBsonArray* array = XBsonArray_create();
    XString* text = XString_create_utf8("str");
    XVector* keys;

    XBsonDocument_insert_keyUtf8_int32(nested, "n", 1);
    bsonArrayAppendValue(array, XBsonValue_create_int32(2));

    CHECK("bsondoc: append 拷贝", XBsonDocument_append(doc, key, value));
    CHECK("bsondoc: insert int32", XBsonDocument_insert_keyUtf8_int32(doc, "int32", 1));
    CHECK("bsondoc: insert int64", XBsonDocument_insert_keyUtf8_int64(doc, "int64", 2));
    CHECK("bsondoc: insert double", XBsonDocument_insert_keyUtf8_double(doc, "double", 3.5));
    CHECK("bsondoc: insert bool", XBsonDocument_insert_keyUtf8_bool(doc, "bool", false));
    CHECK("bsondoc: insert null", XBsonDocument_insert_keyUtf8_null(doc, "null"));
    CHECK("bsondoc: insert utf8", XBsonDocument_insert_keyUtf8_utf8(doc, "utf8", "u"));
    CHECK("bsondoc: insert string", XBsonDocument_insert_keyUtf8_string(doc, "string", text));
    CHECK("bsondoc: insert document", XBsonDocument_insert_keyUtf8_document(doc, "doc", nested));
    CHECK("bsondoc: insert array", XBsonDocument_insert_keyUtf8_array(doc, "array", array));
    CHECK("bsondoc: insert value", XBsonDocument_insert_keyUtf8_value(doc, "value", value));

    CHECK("bsondoc: 大小 11", XBsonDocument_size_base(doc) == 11);
    CHECK("bsondoc: contains", XBsonDocument_contains_keyUtf8(doc, "int32") &&
          !XBsonDocument_contains_keyUtf8(doc, "missing"));
    CHECK("bsondoc: count", XBsonDocument_count_keyUtf8(doc, "int32") == 1 &&
          XBsonDocument_count_keyUtf8(doc, "missing") == 0);
    CHECK("bsondoc: indexOf", XBsonDocument_indexOf(doc, key) == 0);

    {
        XBsonValue* v = XBsonDocument_value_keyUtf8(doc, "string");
        CHECK("bsondoc: value_keyUtf8", v && XBsonValue_isString(v) &&
              utf8Eq(XBsonValue_toString(v), "str"));   /* 借用，不释放 */
    }
    {
        XBsonValue* copied = XBsonDocument_value_copy_keyUtf8(doc, "string");
        XBsonDocument_insert_keyUtf8_int32(doc, "afterCopy", 1);  /* 修改文档 */
        CHECK("bsondoc: value_copy_keyUtf8 独立", copied &&
              XBsonValue_isString(copied) &&
              utf8Eq(XBsonValue_toString(copied), "str"));
        if (copied) XBsonValue_delete(copied);
        CHECK("bsondoc: 缺失键拷贝 NULL",
              XBsonDocument_value_copy_keyUtf8(doc, "missing") == NULL);
        XBsonDocument_remove_keyUtf8(doc, "afterCopy");
    }
    {
        const XBsonElement* element = XBsonDocument_at_const(doc, 0);
        CHECK("bsondoc: at_const", element &&
              utf8Eq(XBsonElement_key_const(element), "key") &&
              XBsonElement_value_const(element) &&
              XBsonValue_toInt32(XBsonElement_value_const(element), 0) == 7);
        CHECK("bsondoc: at_base", XBsonDocument_at_base(doc, 1) != NULL &&
              XBsonDocument_at_base(doc, 99) == NULL);
    }
    keys = XBsonDocument_keys_base(doc);
    CHECK("bsondoc: keys", keys && XVector_size_base(keys) == 11);
    if (keys) XClassDelete((XClass*)keys);

    XClassDelete((XClass*)key);
    XBsonValue_delete(value);
    XClassDelete((XClass*)nested);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)text);
    XClassDelete((XClass*)doc);
}

static void scenario_bson_document_move_inserts(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XString* moveKey = XString_create_utf8("moveKey");
    XBsonValue* moveValue = XBsonValue_create_int32(8);
    XBsonDocument* moveDoc = XBsonDocument_create();
    XBsonArray* moveArray = XBsonArray_create();
    XString* moveText = XString_create_utf8("moveText");

    XBsonDocument_insert_keyUtf8_int32(moveDoc, "m", 5);
    bsonArrayAppendValue(moveArray, XBsonValue_create_int32(6));

    CHECK("bsondoc: append_move",
          XBsonDocument_append_move(doc, moveKey, moveValue));
    XBsonValue_delete(moveValue);
    XClassDelete((XClass*)moveKey);
    CHECK("bsondoc: insert_document_move",
          XBsonDocument_insert_keyUtf8_document_move(doc, "docMove", moveDoc));
    XClassDelete((XClass*)moveDoc);
    CHECK("bsondoc: insert_array_move",
          XBsonDocument_insert_keyUtf8_array_move(doc, "arrayMove", moveArray));
    XClassDelete((XClass*)moveArray);
    CHECK("bsondoc: insert_string_move",
          XBsonDocument_insert_keyUtf8_string_move(doc, "stringMove", moveText));
    /* create_move 语义：字符串内容转移，源 XString 留空壳由调用方释放。 */
    XClassDelete((XClass*)moveText);

    CHECK("bsondoc: move 后大小 4", XBsonDocument_size_base(doc) == 4);
    {
        XBsonValue* v = XBsonDocument_value_keyUtf8(doc, "docMove");
        CHECK("bsondoc: docMove 内容", v && XBsonValue_isDocument(v) &&
              XBsonDocument_contains_keyUtf8(XBsonValue_toDocument(v), "m"));
    }
    {
        XBsonValue* v = XBsonDocument_value_keyUtf8(doc, "stringMove");
        CHECK("bsondoc: stringMove 内容", v && XBsonValue_isString(v) &&
              utf8Eq(XBsonValue_toString(v), "moveText"));
    }

    XClassDelete((XClass*)doc);
}

static void scenario_bson_document_stack(void)
{
    XBsonDocument stackDoc;
    XBsonDocument_init(&stackDoc);
    XBsonDocument_insert_keyUtf8_int32(&stackDoc, "x", 1);
    CHECK("bsondoc: 栈上 init", XBsonDocument_size_base(&stackDoc) == 1 &&
          XBsonDocument_contains_keyUtf8(&stackDoc, "x"));
    XClassDeinit(&stackDoc);
}

static void scenario_bson_document_query_remove(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XString* key = XString_create_utf8("dup");
    XBsonValue* value = XBsonValue_create_int32(3);

    XBsonDocument_insert_keyUtf8_int32(doc, "a", 1);
    CHECK("bsondoc: 重复键 append", XBsonDocument_append(doc, key, value) &&
          XBsonDocument_append(doc, key, value));
    CHECK("bsondoc: 重复键计数", XBsonDocument_count_keyUtf8(doc, "dup") == 2 &&
          XBsonDocument_size_base(doc) == 3);
    CHECK("bsondoc: 重复键 indexOf", XBsonDocument_indexOf(doc, key) == 1);
    CHECK("bsondoc: remove_keyUtf8 单个", XBsonDocument_remove_keyUtf8(doc, "dup") &&
          XBsonDocument_count_keyUtf8(doc, "dup") == 1 &&
          XBsonDocument_size_base(doc) == 2);
    CHECK("bsondoc: removeAll_keyUtf8", XBsonDocument_removeAll_keyUtf8(doc, "dup") == 1 &&
          XBsonDocument_count_keyUtf8(doc, "dup") == 0 &&
          XBsonDocument_size_base(doc) == 1);
    CHECK("bsondoc: remove 缺失键", !XBsonDocument_remove_keyUtf8(doc, "dup"));

    XBsonDocument_insert_keyUtf8_int32(doc, "b", 2);
    XBsonDocument_insert_keyUtf8_int32(doc, "c", 3);
    CHECK("bsondoc: removeAt", XBsonDocument_removeAt(doc, 0) &&
          XBsonDocument_size_base(doc) == 2);
    CHECK("bsondoc: removeAt 越界", !XBsonDocument_removeAt(doc, 99) &&
          !XBsonDocument_removeAt(doc, -1));
    XBsonDocument_clear_base((XBsonDocument*)doc);
    CHECK("bsondoc: clear", XBsonDocument_size_base(doc) == 0);

    XClassDelete((XClass*)key);
    XBsonValue_delete(value);
    XClassDelete((XClass*)doc);
}

static void scenario_bson_document_serialize(void)
{
    XBsonDocument* doc = makeRichBsonDocument();
    XByteArray* bytes = XBsonDocument_toBson(doc);
    XByteArray* bytes2;
    XBsonDocument* parsed;
    XBsonDocument* parsed2;

    CHECK("bsondoc: 富文档大小 21", XBsonDocument_size_base(doc) == 21);
    CHECK("bsondoc: toBson", bytes && XByteArray_size_base(bytes) > 4);

    parsed = bytes ? XBsonDocument_fromBson(bytes) : NULL;
    CHECK("bsondoc: fromBson", parsed &&
          XBsonDocument_size_base(parsed) == XBsonDocument_size_base(doc));
    bytes2 = parsed ? XBsonDocument_toBson(parsed) : NULL;
    CHECK("bsondoc: toBson 往返字节一致", bytesSame(bytes, bytes2));

    parsed2 = XBsonDocument_create();
    CHECK("bsondoc: from_bytes", bytes &&
          XBsonDocument_from_bytes(parsed2, XByteArray_constData(bytes),
                                   XByteArray_size_base(bytes)) &&
          XBsonDocument_size_base(parsed2) == 21);

    /* JSON 桥接：对象逐键等值（键序会按 XMap 排序，不比较字节序）。 */
    {
        XJsonObject* jsonObject = XBsonDocument_toJsonObject(doc);
        XBsonDocument* back = jsonObject ? XBsonDocument_fromJsonObject(jsonObject) : NULL;
        XJsonObject* jsonBack = back ? XBsonDocument_toJsonObject(back) : NULL;
        CHECK("bsondoc: JSON 对象桥接", jsonObject && back &&
              jsonBack && XJsonObject_equals(jsonObject, jsonBack));
        if (jsonBack) XClassDelete((XClass*)jsonBack);
        if (back) XClassDelete((XClass*)back);
        if (jsonObject) XClassDelete((XClass*)jsonObject);
    }
    {
        XByteArray* json = XBsonDocument_toJson(doc, XJsonDocument_Compact);
        XString* jsonString = XBsonDocument_toJson_string(doc, XJsonDocument_Compact);
        CHECK("bsondoc: toJson/toJson_string 一致", bytesTextEq(json, jsonString));
        XClassDelete((XClass*)json);
        XClassDelete((XClass*)jsonString);
    }

    XClassDelete((XClass*)bytes2);
    XClassDelete((XClass*)parsed);
    XClassDelete((XClass*)parsed2);
    XClassDelete((XClass*)bytes);
    XClassDelete((XClass*)doc);
}

static void scenario_bson_array_ops(void)
{
    XBsonArray* array = XBsonArray_create();
    XBsonValue item;
    XJsonArray* jsonArray;

    CHECK("bsonarray: create 空", array && XBsonArray_isEmpty_base(array));
    XBsonValue_init(&item, XBSON_TYPE_INT32);
    XBsonValue_setInt32(&item, 1);
    CHECK("bsonarray: append 拷贝", XBsonArray_append_base((XVector*)array, &item));
    XBsonValue_setInt32(&item, 2);
    CHECK("bsonarray: append_move", XBsonArray_append_move_base((XVector*)array, &item));
    XBsonValue_setInt32(&item, 3);
    CHECK("bsonarray: prepend", XBsonArray_prepend_base((XVector*)array, &item));
    XBsonValue_setInt32(&item, 4);
    CHECK("bsonarray: prepend_move", XBsonArray_prepend_move_base((XVector*)array, &item));

    CHECK("bsonarray: 大小 4", XBsonArray_size_base(array) == 4);
    CHECK("bsonarray: at", XBsonValue_toInt32(
              (const XBsonValue*)XBsonArray_at_base((const XVector*)array, 0), 0) == 4 &&
          XBsonArray_at_base((const XVector*)array, -1) == NULL);

    XBsonValue_setInt32(&item, 7);
    CHECK("bsonarray: insert", XBsonArray_insert(array, 1, &item) &&
          XBsonValue_toInt32((const XBsonValue*)XBsonArray_at_base((const XVector*)array, 1), 0) == 7);
    XBsonValue_setInt32(&item, 8);
    CHECK("bsonarray: replace", XBsonArray_replace(array, 1, &item) &&
          XBsonValue_toInt32((const XBsonValue*)XBsonArray_at_base((const XVector*)array, 1), 0) == 8);
    XBsonArray_removeAt_base((XVector*)array, 0);
    CHECK("bsonarray: removeAt", XBsonArray_size_base(array) == 4);
    XBsonArray_removeAt_base((XVector*)array, 99);
    CHECK("bsonarray: removeAt 越界安全", XBsonArray_size_base(array) == 4);
    XBsonArray_clear_base((XBsonArray*)array);
    CHECK("bsonarray: clear", XBsonArray_isEmpty_base(array));
    XBsonValue_deinit(&item);

    /* 移动/拷贝容器。 */
    bsonArrayAppendValue(array, XBsonValue_create_int32(1));
    {
        XString* str = XString_create_utf8("x");
        bsonArrayAppendValue(array, XBsonValue_create_string(str));
        XClassDelete((XClass*)str);
    }
    {
        XBsonArray* copy = XBsonArray_create_copy(array);
        XBsonArray* moved;
        CHECK("bsonarray: create_copy", copy &&
              XBsonArray_size_base(copy) == XBsonArray_size_base(array));
        moved = XBsonArray_create_move(copy);
        CHECK("bsonarray: create_move", moved &&
              XBsonArray_size_base(copy) == 0 &&
              XBsonArray_size_base(moved) == XBsonArray_size_base(array));
        XClassDelete((XClass*)copy);
        XClassDelete((XClass*)moved);
    }

    {
        XByteArray* bytes = XBsonArray_toBson(array);
        XBsonArray* parsed = bytes ? XBsonArray_fromBson(bytes) : NULL;
        XByteArray* bytes2 = parsed ? XBsonArray_toBson(parsed) : NULL;
        CHECK("bsonarray: BSON 往返", bytes && parsed &&
              XBsonArray_size_base(parsed) == XBsonArray_size_base(array) &&
              bytesSame(bytes, bytes2));
        XClassDelete((XClass*)bytes2);
        XClassDelete((XClass*)parsed);
        XClassDelete((XClass*)bytes);
    }
    {
        XByteArray* bytes = XBsonArray_to_bytes(array);
        XBsonArray* parsed = XBsonArray_create();
        CHECK("bsonarray: to_bytes/from_bytes", bytes && parsed &&
              XBsonArray_from_bytes(parsed, XByteArray_constData(bytes),
                                    XByteArray_size_base(bytes)) &&
              XBsonArray_size_base(parsed) == XBsonArray_size_base(array));
        XClassDelete((XClass*)parsed);
        XClassDelete((XClass*)bytes);
    }
    {
        jsonArray = XBsonArray_toJsonArray(array);
        XBsonArray* back = jsonArray ? XBsonArray_fromJsonArray(jsonArray) : NULL;
        CHECK("bsonarray: JSON 桥接", jsonArray && back &&
              XBsonArray_size_base(back) == XBsonArray_size_base(array));
        if (back) XClassDelete((XClass*)back);
        if (jsonArray) XClassDelete((XClass*)jsonArray);
    }

    XClassDelete((XClass*)array);
}

/* ==================== XJson ↔ XBson 交叉转换 ==================== */

static void scenario_json_bson_cross(void)
{
    XJsonObject* object = makeRichObject();
    XJsonDocument* jsonDoc = XJsonDocument_create_object(object);
    XByteArray* bson = XJsonDocument_toBson(jsonDoc);
    XJsonDocument* back = bson ? XJsonDocument_fromBson_document(bson) : NULL;
    XByteArray* bson2;

    CHECK("cross: JSON→BSON", bson && XByteArray_size_base(bson) > 4);
    CHECK("cross: BSON→JSON 往返等值", back && XJsonDocument_isObject(back) &&
          XJsonObject_equals(XJsonDocument_object(back), object));
    bson2 = back ? XJsonDocument_toBson(back) : NULL;
    CHECK("cross: JSON→BSON→JSON→BSON 字节一致", bytesSame(bson, bson2));

    XClassDelete((XClass*)bson2);
    XClassDelete((XClass*)back);
    XClassDelete((XClass*)bson);
    XClassDelete((XClass*)jsonDoc);
    XClassDelete((XClass*)object);

    /* 数组根。 */
    {
        XJsonArray* array = XJsonArray_create();
        XJsonValue item;
        XJsonDocument* doc;
        XByteArray* bytes;
        XJsonDocument* parsed;
        XByteArray* bytes2;

        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 1);
        XJsonArray_append_move_base((XVector*)array, &item);
        XJsonValue_setDouble(&item, 2.5);
        XJsonArray_append_move_base((XVector*)array, &item);
        XJsonValue_setString_utf8(&item, "s");
        XJsonArray_append_move_base((XVector*)array, &item);
        XJsonValue_deinit(&item);

        doc = XJsonDocument_create_array(array);
        bytes = XJsonDocument_toBson(doc);
        parsed = bytes ? XJsonDocument_fromBson_array(bytes) : NULL;
        bytes2 = parsed ? XJsonDocument_toBson(parsed) : NULL;
        CHECK("cross: 数组 JSON↔BSON", bytes && parsed &&
              XJsonDocument_isArray(parsed) &&
              XJsonArray_equals(XJsonDocument_array(parsed), array) &&
              bytesSame(bytes, bytes2));
        XClassDelete((XClass*)bytes2);
        XClassDelete((XClass*)parsed);
        XClassDelete((XClass*)bytes);
        XClassDelete((XClass*)doc);
        XClassDelete((XClass*)array);
    }
}

/* ==================== XVariant 桥接 ==================== */

static void scenario_variant_bridge(void)
{
    XJsonValue* value = XJsonValue_create_int(5);
    XJsonObject* object = makeRichObject();
    XJsonArray* array = XJsonArray_create();
    XJsonDocument* doc = XJsonDocument_create_object(object);
    XBsonDocument* bdoc = makeRichBsonDocument();
    XVariant* var;
    XJsonValue* valueBack;
    XJsonObject* objectBack;
    XJsonArray* arrayBack;
    XJsonDocument* docBack;
    XBsonDocument* bdocBack;
    XVariantMap* map;
    XVariantHashMap* hash;
    XVariantList* list;
    XJsonValue item;

    var = XJsonValue_toVariant(value);
    valueBack = XJsonValue_fromVariant(var);
    CHECK("variant: JsonValue 往返", valueBack && XJsonValue_equals(valueBack, value));
    XJsonValue_delete(valueBack);
    XClassDelete((XClass*)var);

    var = XJsonObject_toVariant(object);
    objectBack = XJsonObject_fromVariant(var);
    CHECK("variant: JsonObject 往返", objectBack && XJsonObject_equals(objectBack, object));
    XClassDelete((XClass*)objectBack);
    XClassDelete((XClass*)var);

    map = XJsonObject_toVariantMap(object);
    objectBack = map ? XJsonObject_fromVariantMap(map) : NULL;
    CHECK("variant: JsonObject Map 往返", objectBack && XJsonObject_equals(objectBack, object));
    XClassDelete((XClass*)objectBack);
    XClassDelete((XClass*)map);

    hash = XJsonObject_toVariantHash(object);
    objectBack = hash ? XJsonObject_fromVariantHash(hash) : NULL;
    CHECK("variant: JsonObject Hash 往返", objectBack && XJsonObject_equals(objectBack, object));
    XClassDelete((XClass*)objectBack);
    XClassDelete((XClass*)hash);

    XJsonValue_init(&item, XJsonValue_Int);
    XJsonValue_setInt(&item, 1);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setString_utf8(&item, "v");
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_deinit(&item);

    var = XJsonArray_toVariant(array);
    arrayBack = XJsonArray_fromVariant(var);
    CHECK("variant: JsonArray 往返", arrayBack && XJsonArray_equals(arrayBack, array));
    XClassDelete((XClass*)arrayBack);
    XClassDelete((XClass*)var);

    list = XJsonArray_toVariantList(array);
    arrayBack = list ? XJsonArray_fromVariantList(list) : NULL;
    CHECK("variant: JsonArray List 往返", arrayBack && XJsonArray_equals(arrayBack, array));
    XClassDelete((XClass*)arrayBack);
    XClassDelete((XClass*)list);

    var = XJsonDocument_toVariant(doc);
    docBack = XJsonDocument_fromVariant(var);
    CHECK("variant: JsonDocument 往返", docBack && XJsonDocument_isObject(docBack) &&
          XJsonObject_equals(XJsonDocument_object(docBack), object));
    XClassDelete((XClass*)docBack);
    XClassDelete((XClass*)var);

    var = XBsonDocument_toVariant(bdoc);
    bdocBack = XBsonDocument_fromVariant(var);
    CHECK("variant: BsonDocument 往返", bdocBack &&
          XBsonDocument_size_base(bdocBack) == XBsonDocument_size_base(bdoc));
    XClassDelete((XClass*)bdocBack);
    XClassDelete((XClass*)var);

    /* 引用型：不转移所有权，删除包装前脱钩。 */
    var = XJsonObject_toVariant_ref(object);
    CHECK("variant: JsonObject ref", var &&
          XJsonObject_fromVariant_ref(var) == object);
    if (var) {
        var->m_data = NULL;
        XClassDelete((XClass*)var);
    }

    XClassDelete((XClass*)doc);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)object);
    XClassDelete((XClass*)bdoc);
    XJsonValue_delete(value);
}

/* ==================== 独立序列化器 / 数值边界 / 直接 BSON 序列化 ==================== */

static void scenario_json_direct_serializers(void)
{
    XJsonObject* object = makeRichObject();
    XJsonArray* array = XJsonArray_create();
    XString* objectText = XJsonObject_toString(object, XJsonDocument_Indented);
    XString* objectCompact = XJsonObject_toString(object, XJsonDocument_Compact);
    XByteArray* objectJson = XJsonObject_toJson(object, XJsonDocument_Compact);
    XString* arrayText;
    XJsonDocument* parsed;
    XJsonValue item;

    CHECK("direct: object toString", objectText && objectCompact &&
          XString_toUtf8_length(objectText) > XString_toUtf8_length(objectCompact));
    CHECK("direct: object toJson", objectJson && XByteArray_size_base(objectJson) > 0);
    parsed = objectJson ? XJsonDocument_fromJson(objectJson) : NULL;
    CHECK("direct: object 序列化往返", parsed && XJsonDocument_isObject(parsed) &&
          XJsonObject_equals(XJsonDocument_object(parsed), object));
    if (parsed) XClassDelete((XClass*)parsed);

    XJsonValue_init(&item, XJsonValue_Int);
    XJsonValue_setInt(&item, 1);
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_setString_utf8(&item, "a");
    XJsonArray_append_move_base((XVector*)array, &item);
    XJsonValue_deinit(&item);

    arrayText = XJsonArray_toString(array, XJsonDocument_Compact);
    CHECK("direct: array toString", arrayText &&
          utf8Eq(arrayText, "[1,\"a\"]"));
    parsed = arrayText ? parseJsonUtf8(XString_toUtf8(arrayText)) : NULL;
    CHECK("direct: array 序列化往返", parsed && XJsonDocument_isArray(parsed) &&
          XJsonArray_equals(XJsonDocument_array(parsed), array));
    if (parsed) XClassDelete((XClass*)parsed);

    /* 字符串列表 → JSON 数组（借用转拷贝）。 */
    {
        XStringList* list = XStringList_create();
        XString* first = XString_create_utf8("x");
        XString* second = XString_create_utf8("y");
        XJsonArray* fromList;
        XStringList_push_back_move_base(list, first);
        XClassDelete((XClass*)first);   /* move 留壳 */
        XStringList_push_back_move_base(list, second);
        XClassDelete((XClass*)second);
        fromList = XJsonArray_fromStringList(list);
        CHECK("direct: fromStringList", fromList &&
              XJsonArray_size_base(fromList) == 2 &&
              utf8Eq(XJsonValue_toString(XJsonArray_at_const(fromList, 1)), "y"));
        XClassDelete((XClass*)fromList);
        XClassDelete((XClass*)list);
    }

    XClassDelete((XClass*)arrayText);
    XClassDelete((XClass*)objectJson);
    XClassDelete((XClass*)objectCompact);
    XClassDelete((XClass*)objectText);
    XClassDelete((XClass*)array);
    XClassDelete((XClass*)object);
}

static void scenario_json_number_edges(void)
{
    XJsonDocument* doc = parseJsonUtf8(
        "{\"max\":9223372036854775807,\"min\":-9223372036854775808,"
        "\"exp\":1e3,\"small\":1.5e-2,\"negzero\":-0.0,"
        "\"unicode\":\"\\u4e2d\\u6587\",\"esc\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}");
    XJsonObject* object = doc ? XJsonDocument_object(doc) : NULL;

    CHECK("json: 数值边界解析", object != NULL);
    {
        const XJsonValue* v = XJsonObject_value_keyUtf8(object, "max");
        CHECK("json: int64 最大类型", v && XJsonValue_isInt(v) &&
              XJsonValue_toInt(v, 0) == INT64_MAX);
        v = XJsonObject_value_keyUtf8(object, "min");
        CHECK("json: int64 最小类型", v && XJsonValue_isInt(v) &&
              XJsonValue_toInt(v, 0) == INT64_MIN);
        v = XJsonObject_value_keyUtf8(object, "exp");
        CHECK("json: 指数记法", v && XJsonValue_isDouble(v) &&
              XJsonValue_toDouble(v, 0.0) == 1000.0);
        v = XJsonObject_value_keyUtf8(object, "small");
        CHECK("json: 负指数", v && XJsonValue_isDouble(v) &&
              XJsonValue_toDouble(v, 0.0) == 1.5e-2);
        v = XJsonObject_value_keyUtf8(object, "negzero");
        CHECK("json: 负零", v && XJsonValue_isDouble(v) &&
              XJsonValue_toDouble(v, 1.0) == 0.0);
        v = XJsonObject_value_keyUtf8(object, "unicode");
        CHECK("json: unicode 转义", v && XJsonValue_isString(v) &&
              utf8Eq(XJsonValue_toString(v), "中文"));
        v = XJsonObject_value_keyUtf8(object, "esc");
        CHECK("json: 控制字符转义", v && XJsonValue_isString(v) &&
              XString_toUtf8_length(XJsonValue_toString(v)) == 8 &&
              XString_toUtf8(XJsonValue_toString(v))[0] == '"' &&
              XString_toUtf8(XJsonValue_toString(v))[1] == '\\' &&
              XString_toUtf8(XJsonValue_toString(v))[7] == '\t');
    }

    /* 序列化再解析必须等值。 */
    if (doc) {
        XByteArray* json = XJsonDocument_toJson(doc, XJsonDocument_Compact);
        XJsonDocument* back = json ? XJsonDocument_fromJson(json) : NULL;
        CHECK("json: 数值边界往返", back && XJsonDocument_isObject(back) &&
              XJsonObject_equals(XJsonDocument_object(back), object));
        XClassDelete((XClass*)back);
        XClassDelete((XClass*)json);
    }

    XClassDelete((XClass*)doc);
}

static void scenario_bson_direct_serialize(void)
{
    XByteArray* buffer = XByteArray_create();
    XBsonValue* value = XBsonValue_create_int32(7);
    const uint8_t* ptr;
    const uint8_t* end;
    XString* key = NULL;
    XBsonValue* back;

    CHECK("bson: serialize 单元素", buffer && value &&
          XBsonValue_serialize(value, "num", buffer) &&
          XByteArray_size_base(buffer) > 0);
    ptr = XByteArray_constData(buffer);
    end = ptr + XByteArray_size_base(buffer);
    back = XBsonValue_deserialize(&ptr, end, &key);
    CHECK("bson: deserialize 单元素", back && XBsonValue_isInt32(back) &&
          XBsonValue_toInt32(back, 0) == 7 &&
          key && utf8Eq(key, "num") && ptr == end);
    if (key) XClassDelete((XClass*)key);
    if (back) XBsonValue_delete(back);
    XClassDelete((XClass*)buffer);
    XBsonValue_delete(value);

    /* 空文档往返。 */
    {
        XBsonDocument* empty = XBsonDocument_create();
        XByteArray* bytes = XBsonDocument_toBson(empty);
        XBsonDocument* parsed = bytes ? XBsonDocument_fromBson(bytes) : NULL;
        CHECK("bson: 空文档往返", bytes && parsed &&
              XBsonDocument_size_base(parsed) == 0);
        XClassDelete((XClass*)parsed);
        XClassDelete((XClass*)bytes);
        XClassDelete((XClass*)empty);
    }

    /* 非法字节流必须拒绝而不是崩溃。 */
    {
        XByteArray* bad = XByteArray_create_with_data("\x01\x02\x03\x04\x05", 5);
        XBsonDocument* parsed = bad ? XBsonDocument_fromBson(bad) : NULL;
        CHECK("bson: 非法字节流拒绝", parsed == NULL);
        if (parsed) XClassDelete((XClass*)parsed);
        XClassDelete((XClass*)bad);
        CHECK("bson: NULL 字节流", XBsonDocument_fromBson(NULL) == NULL &&
              XBsonArray_fromBson(NULL) == NULL);
    }
}

static void scenario_variant_setters(void)
{
    XJsonValue source;
    XVariant* var = XVariant_create_null();

    XJsonValue_init(&source, XJsonValue_Int);
    XJsonValue_setInt(&source, 11);
    XJsonValue_setVariant(var, &source);
    {
        XJsonValue* back = XJsonValue_fromVariant(var);
        CHECK("variant: setVariant 拷贝", back && XJsonValue_toInt(back, 0) == 11);
        if (back) XJsonValue_delete(back);
    }
    XJsonValue_setVariant_move(var, &source);
    CHECK("variant: setVariant_move 源清零",
          XJsonValue_type(&source) == XJsonValue_Invalid);
    {
        XJsonValue* back = XJsonValue_fromVariant(var);
        CHECK("variant: setVariant_move 值", back && XJsonValue_toInt(back, 0) == 11);
        if (back) XJsonValue_delete(back);
    }
    XJsonValue_setInt(&source, 12);
    XJsonValue_setVariant_ref(var, &source);
    {
        XJsonValue* back = XJsonValue_fromVariant(var);
        CHECK("variant: setVariant_ref", back && XJsonValue_toInt(back, 0) == 12);
        if (back) XJsonValue_delete(back);
    }
    /* 引用型包装不拥有源：删除包装前脱钩。 */
    var->m_data = NULL;
    XClassDelete((XClass*)var);
    XJsonValue_deinit(&source);

    /* JSON 文档变体：深拷贝 + 移动。 */
    {
        XJsonObject* object = makeRichObject();
        XJsonDocument* doc = XJsonDocument_create_object(object);
        XVariant* docVar = XVariant_create_null();
        XJsonDocument* docBack;

        XJsonDocument_setVariant(docVar, doc);
        docBack = XJsonDocument_fromVariant(docVar);
        CHECK("variant: JsonDocument setVariant", docBack &&
              XJsonDocument_isObject(docBack) &&
              XJsonObject_equals(XJsonDocument_object(docBack), object));
        XClassDelete((XClass*)docBack);
        XClassDelete((XClass*)docVar);

        docVar = XVariant_create_null();
        XJsonDocument_setVariant_move(docVar, doc);
        docBack = XJsonDocument_fromVariant(docVar);
        CHECK("variant: JsonDocument setVariant_move", docBack &&
              XJsonDocument_isObject(docBack) &&
              XJsonObject_equals(XJsonDocument_object(docBack), object));
        XClassDelete((XClass*)docBack);
        XClassDelete((XClass*)docVar);

        XClassDelete((XClass*)doc);
        XClassDelete((XClass*)object);
    }

    /* BSON 文档变体。 */
    {
        XBsonDocument* bdoc = makeRichBsonDocument();
        XVariant* bdocVar = XVariant_create_null();
        XBsonDocument* bdocBack;

        XBsonDocument_setVariant(bdocVar, bdoc);
        bdocBack = XBsonDocument_fromVariant(bdocVar);
        CHECK("variant: BsonDocument setVariant", bdocBack &&
              XBsonDocument_size_base(bdocBack) == XBsonDocument_size_base(bdoc));
        XClassDelete((XClass*)bdocBack);
        XClassDelete((XClass*)bdocVar);

        bdocVar = XVariant_create_null();
        XBsonDocument_setVariant_move(bdocVar, bdoc);
        bdocBack = XBsonDocument_fromVariant(bdocVar);
        CHECK("variant: BsonDocument setVariant_move", bdocBack &&
              XBsonDocument_size_base(bdocBack) == 21);
        XClassDelete((XClass*)bdocBack);
        XClassDelete((XClass*)bdocVar);

        XClassDelete((XClass*)bdoc);
    }

    /* 移动转换：源容器清空。 */
    {
        XJsonObject* object = makeRichObject();
        XVariant* mapVar = XJsonObject_toVariant_move(object);
        CHECK("variant: toVariant_move 源清空", mapVar &&
              XJsonObject_size_base(object) == 0);
        XClassDelete((XClass*)mapVar);
        XClassDelete((XClass*)object);
    }
    {
        XJsonObject* object = makeRichObject();
        XVariantMap* mapVar = XJsonObject_toVariantMap_move(object);
        CHECK("variant: toVariantMap_move 源清空", mapVar &&
              XJsonObject_size_base(object) == 0);
        XClassDelete((XClass*)mapVar);
        XClassDelete((XClass*)object);
    }
    {
        XBsonDocument* bdoc = makeRichBsonDocument();
        XVariantMap* mapVar = XBsonDocument_toVariantMap_move(bdoc);
        CHECK("variant: bson toVariantMap_move 源清空", mapVar &&
              XBsonDocument_size_base(bdoc) == 0);
        XClassDelete((XClass*)mapVar);
        XClassDelete((XClass*)bdoc);
    }
    {
        XJsonArray* array = XJsonArray_create();
        XJsonValue item;
        XVariant* listVar;
        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 1);
        XJsonArray_append_move_base((XVector*)array, &item);
        XJsonValue_deinit(&item);
        listVar = XJsonArray_toVariantList_move(array);
        /* 与 XBsonArray_toVariantList_move 一致：移动后源容器为空。 */
        CHECK("variant: toVariantList_move 源清空", listVar &&
              XJsonArray_size_base(array) == 0);
        XClassDelete((XClass*)listVar);
        XClassDelete((XClass*)array);
    }
}

/* ==================== _ref 指针接管（零分配） ==================== */

static void scenario_json_ref_apis(void)
{
    XJsonValue value;
    XString* s = XString_create_utf8("ref-string");
    XJsonArray* a = XJsonArray_create();
    XJsonObject* o = XJsonObject_create();

    XJsonValue_init(&value, XJsonValue_Invalid);
    XJsonValue_setString_ref(&value, s);
    CHECK("ref: value setString_ref", XJsonValue_isString(&value) &&
          utf8Eq(XJsonValue_toString(&value), "ref-string"));
    /* s 已由 value 接管，不得再释放。 */

    XJsonValue_setArray_ref(&value, a);
    CHECK("ref: value setArray_ref", XJsonValue_isArray(&value));
    /* a 已由 value 接管，不得再释放。 */

    XJsonObject_insert_keyUtf8_int(o, "k", 1);
    XJsonValue_setObject_ref(&value, o);
    CHECK("ref: value setObject_ref", XJsonValue_isObject(&value) &&
          jsonObjectHasInt(XJsonValue_toObject(&value), "k", 1));

    XJsonValue_setInt(&value, 0);   /* 释放旧对象 */
    CHECK("ref: value 重设释放旧容器", XJsonValue_isInt(&value));
    XJsonValue_deinit(&value);

    /* XJsonObject 三个 insert_*_ref：成功即接管，不释放源。 */
    {
        XJsonObject* object = XJsonObject_create();
        XString* str = XString_create_utf8("obj-ref");
        XJsonArray* arr = XJsonArray_create();
        XJsonObject* nested = XJsonObject_create();
        XJsonValue item;

        XJsonValue_init(&item, XJsonValue_Int);
        XJsonValue_setInt(&item, 2);
        XJsonArray_append_move_base((XVector*)arr, &item);
        XJsonValue_deinit(&item);
        XJsonObject_insert_keyUtf8_int(nested, "n", 3);

        CHECK("ref: object insert string_ref",
              XJsonObject_insert_keyUtf8_string_ref(object, "s", str));
        CHECK("ref: object insert array_ref",
              XJsonObject_insert_keyUtf8_array_ref(object, "a", arr));
        CHECK("ref: object insert object_ref",
              XJsonObject_insert_keyUtf8_object_ref(object, "o", nested));
        CHECK("ref: object 回读", jsonObjectHasUtf8(object, "s", "obj-ref") &&
              jsonObjectHasArraySize(object, "a", 1) &&
              jsonObjectNestedHasInt(object, "o", "n", 3));
        XClassDelete((XClass*)object);
    }

    /* XJsonDocument 的 set*_ref：根容器指针接管。 */
    {
        XJsonObject* object = XJsonObject_create();
        XJsonArray* array = XJsonArray_create();
        XJsonDocument* doc = XJsonDocument_create();
        XJsonObject_insert_keyUtf8_int(object, "k", 4);
        CHECK("ref: document setObject_ref",
              XJsonDocument_setObject_ref(doc, object) &&
              XJsonDocument_isObject(doc) &&
              jsonObjectHasInt(XJsonDocument_object(doc), "k", 4));
        CHECK("ref: document setArray_ref",
              XJsonDocument_setArray_ref(doc, array) &&
              XJsonDocument_isArray(doc));
        XClassDelete((XClass*)doc);
    }

    /* NULL 参数不接管：源仍归调用方。 */
    {
        XString* keep = XString_create_utf8("keep");
        XJsonObject* object = XJsonObject_create();
        CHECK("ref: json NULL 对象不接管",
              !XJsonObject_insert_keyUtf8_string_ref(NULL, "k", keep));
        CHECK("ref: json NULL 键不接管",
              !XJsonObject_insert_keyUtf8_string_ref(object, NULL, keep));
        XClassDelete((XClass*)object);
        XClassDelete((XClass*)keep);
    }
}

static void scenario_bson_ref_apis(void)
{
    XBsonValue value;
    XString* s = XString_create_utf8("bson-ref");
    XByteArray* bytes = XByteArray_create_with_data("\x01\x02", 2);
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* arr = XBsonArray_create();

    XBsonValue_init(&value, XBSON_TYPE_NULL);
    XBsonValue_setString_ref(&value, s);
    CHECK("ref: bson setString_ref", XBsonValue_isString(&value) &&
          utf8Eq(XBsonValue_toString(&value), "bson-ref"));
    XBsonValue_setBinary_ref(&value, XBSON_BINARY_GENERIC, bytes);
    CHECK("ref: bson setBinary_ref", XBsonValue_isBinary(&value) &&
          XBsonValue_toBinary(&value, NULL) &&
          XByteArray_size_base(XBsonValue_toBinary(&value, NULL)) == 2);
    XBsonDocument_insert_keyUtf8_int32(doc, "k", 1);
    XBsonValue_setDocument_ref(&value, doc);
    CHECK("ref: bson setDocument_ref", XBsonValue_isDocument(&value) &&
          XBsonDocument_contains_keyUtf8(XBsonValue_toDocument(&value), "k"));
    XBsonValue_setArray_ref(&value, arr);
    CHECK("ref: bson setArray_ref", XBsonValue_isArray(&value));
    XBsonValue_deinit(&value);

    /* 文档级 ref 插入：成功即接管，不释放源。 */
    {
        XBsonDocument* target = XBsonDocument_create();
        XString* str = XString_create_utf8("doc-ref");
        XBsonArray* arr2 = XBsonArray_create();
        XBsonDocument* nested = XBsonDocument_create();
        XBsonValue* v;

        XBsonDocument_insert_keyUtf8_int32(nested, "n", 2);
        bsonArrayAppendValue(arr2, XBsonValue_create_int32(3));

        CHECK("ref: bson doc insert string_ref",
              XBsonDocument_insert_keyUtf8_string_ref(target, "s", str));
        CHECK("ref: bson doc insert array_ref",
              XBsonDocument_insert_keyUtf8_array_ref(target, "a", arr2));
        CHECK("ref: bson doc insert document_ref",
              XBsonDocument_insert_keyUtf8_document_ref(target, "d", nested));
        v = XBsonDocument_value_keyUtf8(target, "s");
        CHECK("ref: bson doc 回读 string", v && XBsonValue_isString(v) &&
              utf8Eq(XBsonValue_toString(v), "doc-ref"));
        v = XBsonDocument_value_keyUtf8(target, "a");
        CHECK("ref: bson doc 回读 array", v && XBsonValue_isArray(v) &&
              XBsonArray_size_base(XBsonValue_toArray(v)) == 1);
        v = XBsonDocument_value_keyUtf8(target, "d");
        CHECK("ref: bson doc 回读 document", v && XBsonValue_isDocument(v) &&
              XBsonDocument_contains_keyUtf8(XBsonValue_toDocument(v), "n"));
        XClassDelete((XClass*)target);
    }

    /* NULL 参数不接管。 */
    {
        XString* keep = XString_create_utf8("keep");
        XBsonDocument* target = XBsonDocument_create();
        CHECK("ref: bson NULL 文档不接管",
              !XBsonDocument_insert_keyUtf8_string_ref(NULL, "k", keep));
        CHECK("ref: bson NULL 键不接管",
              !XBsonDocument_insert_keyUtf8_string_ref(target, NULL, keep));
        XClassDelete((XClass*)target);
        XClassDelete((XClass*)keep);
    }
}

/* ==================== 汇总入口 ==================== */

/* ==================== 汇总入口 ==================== */

/* ==================== 汇总入口 ==================== */

int XJsonBsonCoverageTest_run(void)
{
    XCoreApplication* app;
    bool owned;

    owned = (XCoreApplication_instance() == NULL);
    app = XCoreApplication_create(0, NULL);
    if (!app) {
        XPrintf("[FAIL] 事件循环创建失败\n");
        return 1;
    }

    /* 预热轮：类虚表/内存池等一次性分配落定，且不计数。 */
    g_counting = false;
    scenario_json_value_create();
    scenario_json_value_setters();
    scenario_json_value_copy_move();
    scenario_json_value_container_semantics();
    scenario_json_object_inserts();
    scenario_json_object_move_inserts();
    scenario_json_object_query_remove();
    scenario_json_object_copy_move_equals();
    scenario_json_array_ops();
    scenario_json_array_copy_move_equals();
    scenario_json_array_nested();
    scenario_json_document_create();
    scenario_json_document_root_api();
    scenario_json_document_serialize();
    scenario_json_document_parse_errors();
    scenario_bson_value_all_types();
    scenario_bson_value_setters();
    scenario_bson_document_inserts();
    scenario_bson_document_move_inserts();
    scenario_bson_document_stack();
    scenario_bson_document_query_remove();
    scenario_bson_document_serialize();
    scenario_bson_array_ops();
    scenario_json_bson_cross();
    scenario_variant_bridge();
    scenario_json_direct_serializers();
    scenario_json_number_edges();
    scenario_bson_direct_serialize();
    scenario_variant_setters();
    scenario_json_ref_apis();
    scenario_bson_ref_apis();

    /* 计量轮：逐场景做泄漏断言。 */
    g_counting = true;
    runScenario("json_value_create", scenario_json_value_create);
    runScenario("json_value_setters", scenario_json_value_setters);
    runScenario("json_value_copy_move", scenario_json_value_copy_move);
    runScenario("json_value_container", scenario_json_value_container_semantics);
    runScenario("json_object_inserts", scenario_json_object_inserts);
    runScenario("json_object_move_inserts", scenario_json_object_move_inserts);
    runScenario("json_object_query_remove", scenario_json_object_query_remove);
    runScenario("json_object_copy_move", scenario_json_object_copy_move_equals);
    runScenario("json_array_ops", scenario_json_array_ops);
    runScenario("json_array_copy_move", scenario_json_array_copy_move_equals);
    runScenario("json_array_nested", scenario_json_array_nested);
    runScenario("json_document_create", scenario_json_document_create);
    runScenario("json_document_root", scenario_json_document_root_api);
    runScenario("json_document_serialize", scenario_json_document_serialize);
    runScenario("json_document_parse", scenario_json_document_parse_errors);
    runScenario("bson_value_all_types", scenario_bson_value_all_types);
    runScenario("bson_value_setters", scenario_bson_value_setters);
    runScenario("bson_document_inserts", scenario_bson_document_inserts);
    runScenario("bson_document_move_inserts", scenario_bson_document_move_inserts);
    runScenario("bson_document_stack", scenario_bson_document_stack);
    runScenario("bson_document_query_remove", scenario_bson_document_query_remove);
    runScenario("bson_document_serialize", scenario_bson_document_serialize);
    runScenario("bson_array_ops", scenario_bson_array_ops);
    runScenario("json_bson_cross", scenario_json_bson_cross);
    runScenario("variant_bridge", scenario_variant_bridge);
    runScenario("json_direct_serializers", scenario_json_direct_serializers);
    runScenario("json_number_edges", scenario_json_number_edges);
    runScenario("bson_direct_serialize", scenario_bson_direct_serialize);
    runScenario("variant_setters", scenario_variant_setters);
    runScenario("json_ref_apis", scenario_json_ref_apis);
    runScenario("bson_ref_apis", scenario_bson_ref_apis);

    XPrintf("\n===== XJsonBsonCoverageTest: %d passed, %d failed =====\n",
            g_pass, g_fail);
    if (owned) XClassDelete((XClass*)app);
    return g_fail;
}
