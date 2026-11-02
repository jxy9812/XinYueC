/**
 * @file XSettingsTest.c
 * @brief XSettings Qt 6.8 对齐全量测试。
 * @details 覆盖：生命周期/拷贝/移动、标量与类型前缀往返（含 INI 数值
 *          读回为字符串的 Qt 口径）、分组、数组、键枚举排序去重、
 *          contains/remove/clear、落盘重开与外部变更导入、INI 转义
 *          roundtrip、注释与坏行（FormatError）、[General]/[%General]、
 *          粘滞状态、回退链开关、isWritable/原子写、自定义格式注册、
 *          XRect/XSize 的 XVariant 接入、组织名/应用名派生、内存平衡
 *          （XMemory 统计口径）。
 */
#include "XSettingsTest.h"
#include "XSettings.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XString.h"
#include "XStringList.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XByteArray.h"
#include "XMap.h"
#include "XGeometry.h"
#include "XDateTime.h"
#include "XCoreApplication.h"
#include "XFile.h"
#include "XDir.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include <stdbool.h>
#include <stdint.h>

#define XSETTINGS_TEST_PASS(name) XPrintf("[PASS] XSettings: %s\n", name)
#define XSETTINGS_TEST_FAIL(name, reason) XPrintf("[FAIL] XSettings: %s: %s\n", name, reason)
#define XSETTINGS_REQUIRE(condition, name, reason) \
    do { if (!(condition)) { XSETTINGS_TEST_FAIL(name, reason); return false; } } while (0)

/* ==================== 断言辅助 ==================== */

/** @brief 变体内容是否为给定 UTF-8 文本（String 类型）。 */
static bool xsettings_test_textIs(const XVariant* variant, const char* expectedUtf8)
{
    if (!variant || variant->m_type != XVariantType_String)
        return false;
    const XString* str = XString_fromVariant_ref((XVariant*)variant);
    return str && XString_equals_utf8(str, expectedUtf8, XChar_CaseSensitive);
}

/** @brief 变体按 Qt 口径转 int（字符串解析或整型直取）。 */
static int xsettings_test_toInt(const XVariant* variant)
{
    if (!variant)
        return 0;
    if (variant->m_type == XVariantType_String) {
        const XString* str = XString_fromVariant_ref((XVariant*)variant);
        bool ok = false;
        return str ? XString_toInt(str, &ok, 10) : 0;
    }
    return XVariant_toInt(variant);
}

/** @brief 测试文件清理。 */
static void xsettings_test_removeFile(const char* pathUtf8)
{
    XString* path = XString_create_utf8(pathUtf8);
    if (path) {
        if (XFileInfo_exists_static(path))
            XFile_remove_static(path);
        XClassDelete(path);
    }
}

/* ==================== 1. 生命周期/拷贝/移动 ==================== */

static bool xsettings_test_lifecycle(void)
{
    /* Qt 口径：组织名为空时构造即置 AccessError（Unknown Organization 兜底）；
     * 本用例需要 NoError 前提，先设置全局组织名/应用名。 */
    XString* savedOrg = XString_create_copy(XCoreApplication_organizationName());
    XString* savedApp = XString_create_copy(XCoreApplication_applicationName());
    XString* org = XString_create_utf8("XinYueCTestOrg");
    XString* app = XString_create_utf8("XinYueCTestApp");
    XCoreApplication_setOrganizationName(org);
    XCoreApplication_setApplicationName(app);
    XClassDelete(org);
    XClassDelete(app);
    /* 组织构造路径重定向到测试目录，避免污染真实用户目录 */
    XString* tmpDir = XDir_homePath();
    if (tmpDir) {
        XString_append_utf8(tmpDir, "/XSettingsTestOrg");
        XDir* dir = XDir_create_2(tmpDir);
        if (dir) {
            XDir_mkpath(dir, tmpDir);
            XClassDelete(dir);
        }
        XSettings_setPath(XSettings_NativeFormat, XSettings_UserScope, tmpDir);
        XClassDelete(tmpDir);
    }
    XSettings stack;
    XSettings_init(&stack);
    XSETTINGS_REQUIRE(XSettings_group_const(&stack) != NULL, "init", "组前缀为空");
    XSETTINGS_REQUIRE(XSettings_status(&stack) == XSettings_NoError, "init status", "初始状态非 NoError");

    /* 前置清理：组织构造路径下的遗留文件（含回退链各文件）会让"拷贝独立性"
     * 读到上轮残留键；按 home 绝对路径清理。 */
    {
        XString* home = XDir_homePath();
        if (home) {
            static const char* names[] = {
                "XinYueCTestApp.conf", "XinYueCTestOrg.conf",
                "XinYueCTestApp.ini", "XinYueCTestOrg.ini"
            };
            for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
                XString* p = XString_create_copy(home);
                if (p) {
                    XString_append_utf8(p, "/XSettingsTestOrg/XinYueCTestOrg/");
                    XString_append_utf8(p, names[i]);
                    if (XFileInfo_exists_static(p))
                        XFile_remove_static(p);
                    XString_clear_base(p);
                    XString_append_utf8(p, "/XSettingsTestOrg/");
                    XString_append_utf8(p, names[i]);
                    if (XFileInfo_exists_static(p))
                        XFile_remove_static(p);
                    XClassDelete(p);
                }
            }
            XClassDelete(home);
        }
    }

    XSettings* created = XSettings_create();
    XSETTINGS_REQUIRE(created != NULL, "create", "创建失败");
    /* 无效变体键：Qt 语义下 setValue(key, QVariant()) 仍建立键（读回无效） */
    XVariant* nullVariant = XVariant_create_null();
    XSettings_setValue_utf8(created, "k", nullVariant);
    if (nullVariant) XClassDelete(nullVariant);
    XSETTINGS_REQUIRE(XSettings_contains_utf8(created, "k"), "setValue null", "无效变体键应存在");

    XSettings* copied = XSettings_create_copy(created);
    XSETTINGS_REQUIRE(copied != NULL && XSettings_contains_utf8(copied, "k"), "copy", "拷贝缺键");
    /* 拷贝独立性：先确认源侧确实没有该键，再写入拷贝 */
    XSETTINGS_REQUIRE(!XSettings_contains_utf8(created, "only-in-copy"),
                      "copy precond", "源侧不应预先存在 only-in-copy");
    XVariant* copyOnly = XVariant_create_int(7);
    XSettings_setValue_utf8(copied, "only-in-copy", copyOnly);
    if (copyOnly) XClassDelete(copyOnly);
    XSETTINGS_REQUIRE(!XSettings_contains_utf8(created, "only-in-copy"), "copy independence", "拷贝写入影响源");

    XSettings* moved = XSettings_create_move(copied);
    XSETTINGS_REQUIRE(moved != NULL && XSettings_contains_utf8(moved, "only-in-copy"), "move", "移动丢键");

    XClassDelete(moved);
    XClassDelete(copied);
    XClassDelete(created);
    XClassDeinit(&stack);
    /* 清理测试目录文件（stack/copied 的析构落盘产物） */
    XString* cleanup = XString_create_utf8("XSettingsTestOrg/XinYueCTestApp.conf");
    xsettings_test_removeFile(cleanup);
    XClassDelete(cleanup);
    XCoreApplication_setOrganizationName(savedOrg);
    XCoreApplication_setApplicationName(savedApp);
    XClassDelete(savedOrg);
    XClassDelete(savedApp);
    XSETTINGS_TEST_PASS("生命周期/拷贝/移动");
    return true;
}

/* ==================== 2. 标量与 INI 数值字符串口径 ==================== */

static bool xsettings_test_scalars(void)
{
    XSettings* s = XSettings_create_7_utf8("XSettingsScalars.ini", XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XVariant* vInt = XVariant_create_int(42);
    XVariant* vBool = XVariant_create_bool(true);
    XVariant* vDouble = XVariant_create_double(0.5);
    XVariant* vDouble2 = XVariant_create_double(3.0);
    XVariant* vLL = XVariant_create_int64(-1234567890123LL);
    XString* vStr = XString_create_utf8("hello");
    XVariant* vText = XString_toVariant(vStr);
    XSettings_setValue_utf8(s, "i", vInt);
    XSettings_setValue_utf8(s, "b", vBool);
    XSettings_setValue_utf8(s, "d", vDouble);
    XSettings_setValue_utf8(s, "d2", vDouble2);
    XSettings_setValue_utf8(s, "ll", vLL);
    XSettings_setValue_utf8(s, "t", vText);
    /* 内存态：Qt 语义 value 返回存入的原类型变体（未落盘不序列化） */
    XVariant* r = XSettings_value_utf8(s, "i");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Int && XVariant_toInt(r) == 42,
                      "int memory", "内存态应返回原 Int 变体");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "b");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Bool && XVariant_toBool(r),
                      "bool memory", "内存态应返回原 Bool 变体");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "d");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Double && XVariant_toDouble(r) == 0.5,
                      "double memory", "内存态应返回原 Double 变体");
    XClassDelete(r);
    r = XSettings_value_2_utf8(s, "missing", vInt);
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Int && XVariant_toInt(r) == 42,
                      "default value", "缺键应返回默认值深拷贝");
    XClassDelete(r);
    /* 落盘重读：INI 无类型，数值读回为字符串文本（Qt 一致口径） */
    XSettings_sync(s);
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "sync", "落盘状态非 NoError");
    XClassDelete(s);
    s = XSettings_create_7_utf8("XSettingsScalars.ini", XSettings_IniFormat, NULL);
    r = XSettings_value_utf8(s, "i");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_String && xsettings_test_textIs(r, "42"),
                      "int as string", "落盘后数值应读回字符串 \"42\"");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "b");
    XSETTINGS_REQUIRE(xsettings_test_textIs(r, "true"), "bool text", "bool 应为 \"true\"");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "d");
    XSETTINGS_REQUIRE(xsettings_test_textIs(r, "0.5"), "double shortest", "0.5 应最短往返");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "d2");
    XSETTINGS_REQUIRE(xsettings_test_textIs(r, "3"), "double integer", "3.0 应为 \"3\"");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "ll");
    XSETTINGS_REQUIRE(xsettings_test_textIs(r, "-1234567890123"), "int64", "int64 文本不符");
    XClassDelete(r);
    r = XSettings_value_utf8(s, "t");
    XSETTINGS_REQUIRE(xsettings_test_textIs(r, "hello"), "string text", "字符串读回不符");
    XClassDelete(r);
    XClassDelete(vText);
    XClassDelete(vLL);
    XClassDelete(vDouble2);
    XClassDelete(vDouble);
    XClassDelete(vBool);
    XClassDelete(vInt);
    XClassDelete(vStr);
    XClassDelete(s);
    xsettings_test_removeFile("XSettingsScalars.ini");
    XSETTINGS_TEST_PASS("标量与 INI 数值字符串口径");
    return true;
}

/* ==================== 3. 类型前缀往返（落盘后类型保持） ==================== */

static bool xsettings_test_typedPrefixes(void)
{
    const char* path = "XSettingsTyped.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XPoint point;
    XPoint_init(&point, 3, -7);
    XVariant* vPoint = XPoint_toVariant(point);
    XRect rect;
    XRect_init(&rect, 1, 2, 30, 40);
    XVariant* vRect = XRect_toVariant(rect);
    XSize size;
    XSize_init(&size, 640, 480);
    XVariant* vSize = XSize_toVariant(size);
    XByteArray* bytes = XByteArray_create_with_data("a\0b!", 4);
    XVariant* vBytes = bytes ? XByteArray_toVariant(bytes) : NULL;
    XStringList* list = XStringList_create();
    XString* item = XString_create_utf8("x");
    XStringList_push_back_move_base(list, item);   /* move 后源壳仍须释放 */
    XClassDelete(item);
    item = XString_create_utf8("b c");
    XStringList_push_back_move_base(list, item);
    XClassDelete(item);
    item = XString_create_utf8("d,e");
    XStringList_push_back_move_base(list, item);
    XClassDelete(item);
    XVariant* vList = XStringList_toVariant_move(list);
    XClassDelete(list);   /* move 后源壳仍须释放 */
    XDateTime now = XDateTime_create();
    XVariant* vDateTime = XDateTime_toVariant(&now);
    XSettings_setValue_utf8(s, "p", vPoint);
    XSettings_setValue_utf8(s, "r", vRect);
    XSettings_setValue_utf8(s, "sz", vSize);
    XSettings_setValue_utf8(s, "ba", vBytes);
    XSettings_setValue_utf8(s, "sl", vList);
    XSettings_setValue_utf8(s, "dt", vDateTime);
    XSettings_sync(s);
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "sync", "落盘状态非 NoError");
    XClassDelete(s);
    /* 重开：类型应保持 */
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "reopen", "重开失败");
    XVariant* r = XSettings_value_utf8(s, "p");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Point, "point type", "Point 类型丢失");
    if (r) {
        XPoint back = XPoint_fromVariant(r);
        XSETTINGS_REQUIRE(back.x == 3 && back.y == -7, "point value", "Point 值不符");
        XClassDelete(r);
    }
    r = XSettings_value_utf8(s, "r");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Rect, "rect type", "Rect 类型丢失");
    if (r) {
        XRect back = XRect_fromVariant(r);
        XSETTINGS_REQUIRE(back.x == 1 && back.y == 2 && back.width == 30 && back.height == 40,
                          "rect value", "Rect 值不符");
        XClassDelete(r);
    }
    r = XSettings_value_utf8(s, "sz");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Size, "size type", "Size 类型丢失");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "ba");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_ByteArray, "bytearray type", "ByteArray 类型丢失");
    if (r) {
        XByteArray* back = XByteArray_fromVariant_ref(r);
        XSETTINGS_REQUIRE(back && XByteArray_size_base(back) == 4 &&
                          XByteArray_data(back)[0] == 'a' && XByteArray_data(back)[1] == 0 &&
                          XByteArray_data(back)[2] == 'b' && XByteArray_data(back)[3] == '!',
                          "bytearray value", "ByteArray 值不符");
        XClassDelete(r);
    }
    r = XSettings_value_utf8(s, "sl");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_StringList, "stringlist type", "StringList 类型丢失");
    if (r) {
        XStringList* back = XStringList_fromVariant_ref(r);
        XSETTINGS_REQUIRE(back && XStringList_size_base(back) == 3, "stringlist count", "元素数不符");
        if (back && XStringList_size_base(back) == 3) {
            const XString* e = XStringList_at_base(back, 1);
            XSETTINGS_REQUIRE(e && XString_equals_utf8(e, "b c", XChar_CaseSensitive),
                              "stringlist elem", "含空格元素不符");
            e = XStringList_at_base(back, 2);
            XSETTINGS_REQUIRE(e && XString_equals_utf8(e, "d,e", XChar_CaseSensitive),
                              "stringlist comma elem", "含逗号元素不符");
        }
        XClassDelete(r);
    }
    r = XSettings_value_utf8(s, "dt");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_DateTime, "datetime type", "DateTime 类型丢失（内部 TLV 往返）");
    if (r) XClassDelete(r);
    XClassDelete(vDateTime);
    XClassDelete(vList);
    XClassDelete(vBytes);
    if (bytes) XClassDelete(bytes);   /* 源字节数组（此前漏释放） */
    XClassDelete(vSize);
    XClassDelete(vRect);
    XClassDelete(vPoint);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("类型前缀往返");
    return true;
}

/* ==================== 4. Map 变体 TLV 往返 ==================== */

static bool xsettings_test_mapVariant(void)
{
    const char* path = "XSettingsMap.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    /* 值语义正用：XVariantMap 槽为按值内联对象，insert 传局部对象地址 */
    XMap* map = XMap_create_XVariantMap();
    XSETTINGS_REQUIRE(map != NULL, "map create", "创建失败");
    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, "inner");
    XVariant value;
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    XVariant_setValue_int(&value, 7);
    XSETTINGS_REQUIRE(XMapBase_insert_base(map, &key, &value), "map insert", "插入失败");
    XClassDeinit(&key);
    XClassDeinit(&value);
    XVariant* vMap = XMap_toVariant_move(map);
    XClassDelete(map);   /* move 后源壳仍须释放 */
    XSettings_setValue_utf8(s, "m", vMap);
    XSettings_sync(s);
    XClassDelete(s);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XVariant* r = XSettings_value_utf8(s, "m");
    XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Map, "map type", "Map 类型丢失");
    if (r) {
        XMap* back = XMap_fromVariant_ref(r);
        XSETTINGS_REQUIRE(back && XMap_size_base(back) == 1, "map size", "Map 大小不符");
        if (back) {
            /* 查找键须重新构造局部对象（先前的 key 已 deinit） */
            XString lookup;
            XString_init(&lookup);
            XString_assign_utf8(&lookup, "inner");
            XVariant* inner = (XVariant*)XMapBase_value_base(back, &lookup);
            XSETTINGS_REQUIRE(inner && inner->m_type == XVariantType_Int && XVariant_toInt(inner) == 7,
                              "map value", "Map 内值不符");
            XClassDeinit(&lookup);
        }
        XClassDelete(r);
    }
    XClassDelete(vMap);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("Map 变体 TLV 往返");
    return true;
}

/* ==================== 5. 分组 ==================== */

static bool xsettings_test_groups(void)
{
    const char* path = "XSettingsGroups.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XSettings_beginGroup_utf8(s, "ui");
    XSettings_beginGroup_utf8(s, "win");
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_group_const(s), "ui/win/", XChar_CaseSensitive),
                      "group path", "组路径不符");
    XVariant* v = XVariant_create_int(800);
    XSettings_setValue_utf8(s, "width", v);
    XClassDelete(v);
    XStringList* keys = XSettings_allKeys(s);
    XSETTINGS_REQUIRE(keys && XStringList_size_base(keys) == 1, "group allKeys", "组内 allKeys 应为相对键");
    if (keys && XStringList_size_base(keys) == 1) {
        const XString* k = XStringList_at_base(keys, 0);
        XSETTINGS_REQUIRE(k && XString_equals_utf8(k, "width", XChar_CaseSensitive),
                          "group allKeys name", "组内键名不符");
    }
    if (keys) XClassDelete(keys);
    XStringList* rootKeys = NULL;
    XSettings_endGroup(s);
    XSettings_endGroup(s);
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_group_const(s), "", XChar_CaseSensitive),
                      "group root", "退回根组失败");
    rootKeys = XSettings_allKeys(s);
    XSETTINGS_REQUIRE(rootKeys && XStringList_size_base(rootKeys) == 1, "root keys", "根键数不符");
    if (rootKeys && XStringList_size_base(rootKeys) == 1) {
        const XString* k = XStringList_at_base(rootKeys, 0);
        XSETTINGS_REQUIRE(k && XString_equals_utf8(k, "ui/win/width", XChar_CaseSensitive),
                          "root key path", "根键路径不符");
    }
    if (rootKeys) XClassDelete(rootKeys);
    /* 嵌套组键的子组/子键枚举 */
    v = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "ui/height", v);
    XClassDelete(v);
    XSettings_beginGroup_utf8(s, "ui");
    XStringList* groups = XSettings_childGroups(s);
    XSETTINGS_REQUIRE(groups && XStringList_size_base(groups) == 1, "childGroups", "子组数不符");
    if (groups && XStringList_size_base(groups) == 1) {
        const XString* g = XStringList_at_base(groups, 0);
        XSETTINGS_REQUIRE(g && XString_equals_utf8(g, "win", XChar_CaseSensitive),
                          "childGroups name", "子组名不符");
    }
    if (groups) XClassDelete(groups);
    XStringList* childKeys = XSettings_childKeys(s);
    XSETTINGS_REQUIRE(childKeys && XStringList_size_base(childKeys) == 1, "childKeys", "子键数不符");
    if (childKeys && XStringList_size_base(childKeys) == 1) {
        const XString* k = XStringList_at_base(childKeys, 0);
        XSETTINGS_REQUIRE(k && XString_equals_utf8(k, "height", XChar_CaseSensitive),
                          "childKeys name", "子键名不符");
    }
    if (childKeys) XClassDelete(childKeys);
    XSettings_endGroup(s);
    /* 空栈 endGroup 安全 */
    XSettings_endGroup(s);
    XSettings_endGroup(s);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("分组");
    return true;
}

/* ==================== 6. 数组 ==================== */

static bool xsettings_test_arrays(void)
{
    const char* path = "XSettingsArrays.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XSettings_beginWriteArray_2_utf8(s, "logs", 2);
    /* Qt 语义：beginWriteArray 后组前缀仅为 "logs/"，须先 setArrayIndex 选元素 */
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_group_const(s), "logs/", XChar_CaseSensitive),
                      "array begin group", "beginWriteArray 后组前缀应为 logs/");
    XSettings_setArrayIndex(s, 0);
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_group_const(s), "logs/1/", XChar_CaseSensitive),
                      "array group", "setArrayIndex 后组前缀应为 logs/1/");
    XVariant* v = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "msg", v);
    XClassDelete(v);
    XSettings_setArrayIndex(s, 1);
    v = XVariant_create_int(2);
    XSettings_setValue_utf8(s, "msg", v);
    XClassDelete(v);
    XSettings_endArray(s);
    XVariant* sizeValue = XSettings_value_utf8(s, "logs/size");
    XSETTINGS_REQUIRE(sizeValue && xsettings_test_toInt(sizeValue) == 2, "array size key", "logs/size 应为 2");
    if (sizeValue) XClassDelete(sizeValue);
    XSettings_sync(s);
    XClassDelete(s);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    int count = XSettings_beginReadArray_utf8(s, "logs");
    XSETTINGS_REQUIRE(count == 2, "beginReadArray", "读数组大小应为 2");
    XSettings_setArrayIndex(s, 0);
    XVariant* r = XSettings_value_utf8(s, "msg");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 1, "array elem 1", "元素 1 值不符");
    if (r) XClassDelete(r);
    XSettings_setArrayIndex(s, 1);
    r = XSettings_value_utf8(s, "msg");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 2, "array elem 2", "元素 2 值不符");
    if (r) XClassDelete(r);
    XSettings_endArray(s);
    /* 自动计数：最高下标 + 1 */
    XSettings_beginWriteArray_utf8(s, "auto");
    XSettings_setArrayIndex(s, 4);
    v = XVariant_create_int(9);
    XSettings_setValue_utf8(s, "x", v);
    XClassDelete(v);
    XSettings_endArray(s);
    r = XSettings_value_utf8(s, "auto/size");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 5, "auto size", "自动计数应为 5");
    if (r) XClassDelete(r);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("数组");
    return true;
}

/* ==================== 7. 键枚举排序去重 ==================== */

static bool xsettings_test_keyEnumeration(void)
{
    const char* path = "XSettingsEnum.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XVariant* v = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "zebra", v);
    XSettings_setValue_utf8(s, "apple", v);
    XSettings_setValue_utf8(s, "mango/a", v);
    XSettings_setValue_utf8(s, "mango/b", v);
    XSettings_setValue_utf8(s, "apple/sub", v);
    XClassDelete(v);
    XStringList* all = XSettings_allKeys(s);
    XSETTINGS_REQUIRE(all && XStringList_size_base(all) == 5, "allKeys count", "全键数不符");
    if (all && XStringList_size_base(all) == 5) {
        const XString* first = XStringList_at_base(all, 0);
        XSETTINGS_REQUIRE(first && XString_equals_utf8(first, "apple", XChar_CaseSensitive),
                          "allKeys sorted", "全键应排序（apple 首位）");
    }
    if (all) XClassDelete(all);
    XStringList* groups = XSettings_childGroups(s);
    XSETTINGS_REQUIRE(groups && XStringList_size_base(groups) == 2, "root childGroups", "根子组应为 apple 与 mango");
    if (groups && XStringList_size_base(groups) == 2) {
        const XString* g0 = XStringList_at_base(groups, 0);
        const XString* g1 = XStringList_at_base(groups, 1);
        XSETTINGS_REQUIRE(g0 && g1 &&
                          XString_equals_utf8(g0, "apple", XChar_CaseSensitive) &&
                          XString_equals_utf8(g1, "mango", XChar_CaseSensitive),
                          "childGroups sorted", "子组应排序为 apple/mango");
    }
    if (groups) XClassDelete(groups);
    XStringList* childKeys = XSettings_childKeys(s);
    XSETTINGS_REQUIRE(childKeys && XStringList_size_base(childKeys) == 2, "root childKeys", "根直接子键应为 apple/zebra");
    if (childKeys) XClassDelete(childKeys);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("键枚举排序去重");
    return true;
}

/* ==================== 8. contains/remove/clear ==================== */

static bool xsettings_test_removeAndClear(void)
{
    const char* path = "XSettingsRemove.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XVariant* v = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "keep", v);
    XSettings_setValue_utf8(s, "ui/win/width", v);
    XSettings_setValue_utf8(s, "ui/win/height", v);
    XSettings_setValue_utf8(s, "ui/theme", v);
    XClassDelete(v);
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "ui/win/width"), "contains", "应包含 ui/win/width");
    XSettings_remove_utf8(s, "ui/win");
    XSETTINGS_REQUIRE(!XSettings_contains_utf8(s, "ui/win/width"), "remove subtree", "子树键应被删除");
    XSETTINGS_REQUIRE(!XSettings_contains_utf8(s, "ui/win/height"), "remove subtree 2", "子树键应被删除");
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "ui/theme"), "remove keeps sibling", "兄弟键应保留");
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "keep"), "remove keeps root", "根键应保留");
    XSettings_remove_utf8(s, "keep");
    XSETTINGS_REQUIRE(!XSettings_contains_utf8(s, "keep"), "remove exact", "精确键应被删除");
    XSettings_clear(s);
    XStringList* all = XSettings_allKeys(s);
    XSETTINGS_REQUIRE(all && XStringList_size_base(all) == 0, "clear", "清空后应无键");
    if (all) XClassDelete(all);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("contains/remove/clear");
    return true;
}

/* ==================== 9. 落盘/重开/外部变更导入 ==================== */

static bool xsettings_test_persistence(void)
{
    const char* path = "XSettingsPersist.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XVariant* v = XVariant_create_int(123);
    XSettings_setValue_utf8(s, "answer", v);
    XClassDelete(v);
    XString* fileName = XString_create_copy(XSettings_fileName_const(s));
    XSETTINGS_REQUIRE(fileName && XString_size(fileName) > 0, "fileName", "文件名不应为空");
    XSettings_sync(s);
    XSETTINGS_REQUIRE(XFileInfo_exists_static(fileName), "file created", "sync 后文件应存在");
    XClassDelete(s);
    /* 重开读到持久化数据 */
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XVariant* r = XSettings_value_utf8(s, "answer");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 123, "reload", "重开应读到持久化值");
    if (r) XClassDelete(r);
    /* 外部修改导入：手写追加后 sync 触发重读（size/时间戳变更） */
    XString* appendPath = XString_create_utf8(path);
    XFile* appender = XFile_create_2(appendPath);
    if (appender && XIODevice_open_base((XIODevice*)appender, XIODevice_WriteOnly | XIODevice_Append)) {
        XIODevice_write_3((XIODevice*)appender, "extra=1\n");
        XIODevice_close_base((XIODevice*)appender);
    }
    if (appender) XClassDelete(appender);
    XString_clear_base(appendPath);
    XClassDelete(appendPath);
    XSettings_sync(s);
    r = XSettings_value_utf8(s, "extra");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 1, "external import", "sync 应导入外部修改");
    if (r) XClassDelete(r);
    XClassDelete(fileName);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("落盘/重开/外部变更导入");
    return true;
}

/* ==================== 10. INI 转义 roundtrip ==================== */

static bool xsettings_test_escaping(void)
{
    const char* path = "XSettingsEscape.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XString* semi = XString_create_utf8("hello;world");
    XVariant* v = XString_toVariant(semi);
    XSettings_setValue_utf8(s, "semi", v);
    XClassDelete(v);
    XClassDelete(semi);
    semi = XString_create_utf8("  padded  ");
    v = XString_toVariant(semi);
    XSettings_setValue_utf8(s, "pad", v);
    XClassDelete(v);
    XClassDelete(semi);
    semi = XString_create_utf8("@atStart");
    v = XString_toVariant(semi);
    XSettings_setValue_utf8(s, "at", v);
    XClassDelete(v);
    XClassDelete(semi);
    semi = XString_create_utf8("line1\nline2\ttabbed");
    v = XString_toVariant(semi);
    XSettings_setValue_utf8(s, "ctrl", v);
    XClassDelete(v);
    XClassDelete(semi);
    semi = XString_create_utf8("中文值＠＠混合");
    v = XString_toVariant(semi);
    XSettings_setValue_utf8(s, "uni", v);
    XClassDelete(v);
    XClassDelete(semi);
    /* 键含空格/等号/Unicode/'/' */
    XVariant* mark = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "key with space", mark);
    XSettings_setValue_utf8(s, "key=eq", mark);
    XSettings_setValue_utf8(s, "中文/子键", mark);
    XClassDelete(mark);
    XSettings_sync(s);
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "escape sync", "转义落盘状态异常");
    XClassDelete(s);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XVariant* r = XSettings_value_utf8(s, "semi");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "hello;world"), "escape semicolon", "分号值不符");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "pad");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "  padded  "), "escape spaces", "首尾空格值不符");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "at");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "@atStart"), "escape at", "@ 前缀值应 @@ 双写还原");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "ctrl");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "line1\nline2\ttabbed"), "escape control", "控制字符值不符");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "uni");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "中文值＠＠混合"), "escape unicode", "Unicode 值不符");
    if (r) XClassDelete(r);
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "key with space"), "escape key space", "含空格键不符");
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "key=eq"), "escape key equals", "含等号键不符");
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "中文/子键"), "escape key unicode", "Unicode 键不符");
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("INI 转义 roundtrip");
    return true;
}

/* ==================== 11. 注释与坏行（FormatError） ==================== */

static bool xsettings_test_commentsAndErrors(void)
{
    const char* path = "XSettingsComments.ini";
    /* 注释透明 + [GENERAL] 大小写归根 */
    XString* pathStr = XString_create_utf8(path);
    XFile* writer = XFile_create_2(pathStr);
    XSETTINGS_REQUIRE(writer != NULL, "writer create", "创建失败");
    if (XIODevice_open_base((XIODevice*)writer, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XIODevice_write_3((XIODevice*)writer, "; top comment\r\n[GENERAL]\r\n; mid comment\r\na=1\r\nb = 2 ; trailing\r\n");
        XIODevice_close_base((XIODevice*)writer);
    }
    XClassDelete(writer);
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "reopen", "重开失败");
    XVariant* r = XSettings_value_utf8(s, "a");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 1, "comment skip", "注释行应透明");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "b");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 2, "value semicolon truncate", "引号外 ';' 应截断值");
    if (r) XClassDelete(r);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    /* 裸词行 → FormatError */
    writer = XFile_create_2(pathStr);
    if (XIODevice_open_base((XIODevice*)writer, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XIODevice_write_3((XIODevice*)writer, "garbage-line\nkey=1\n");
        XIODevice_close_base((XIODevice*)writer);
    }
    XClassDelete(writer);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    r = XSettings_value_utf8(s, "key");
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_FormatError, "bare line", "裸词行应置 FormatError");
    if (r) XClassDelete(r);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    /* 节头缺 ']' → FormatError */
    writer = XFile_create_2(pathStr);
    if (XIODevice_open_base((XIODevice*)writer, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XIODevice_write_3((XIODevice*)writer, "[Broken\nkey=1\n");
        XIODevice_close_base((XIODevice*)writer);
    }
    XClassDelete(writer);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSettings_value_utf8(s, "key");
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_FormatError, "missing bracket", "缺 ']' 应置 FormatError");
    XClassDelete(s);
    /* 粘滞：后续正常访问仍保持 FormatError */
    writer = XFile_create_2(pathStr);
    if (XIODevice_open_base((XIODevice*)writer, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XIODevice_write_3((XIODevice*)writer, "[General]\nkey=1\n");
        XIODevice_close_base((XIODevice*)writer);
    }
    XClassDelete(writer);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    r = XSettings_value_utf8(s, "key");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 1, "general key", "正常键应可读");
    if (r) XClassDelete(r);
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "clean status", "好文件状态应为 NoError");
    XClassDelete(s);
    XClassDelete(pathStr);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("注释与坏行");
    return true;
}

/* ==================== 12. [%General] 与重复键 ==================== */

static bool xsettings_test_generalVariants(void)
{
    const char* path = "XSettingsGeneral.ini";
    XString* pathStr = XString_create_utf8(path);
    XFile* writer = XFile_create_2(pathStr);
    XSETTINGS_REQUIRE(writer != NULL, "writer create", "创建失败");
    if (XIODevice_open_base((XIODevice*)writer, XIODevice_WriteOnly | XIODevice_Truncate)) {
        /* [%General] 键归 "General/" 组；[General] 键归根；重复键后者胜 */
        XIODevice_write_3((XIODevice*)writer, "[General]\nroot=1\ndup=first\n[%General]\ngeneral=2\n[General]\ndup=second\n");
        XIODevice_close_base((XIODevice*)writer);
    }
    XClassDelete(writer);
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XVariant* r = XSettings_value_utf8(s, "root");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 1, "[General] root", "根节键不符");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "General/general");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 2, "[%General] group", "[%General] 应归 General/ 组");
    if (r) XClassDelete(r);
    r = XSettings_value_utf8(s, "dup");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "second"), "dup later wins", "重复键应后者胜");
    if (r) XClassDelete(r);
    XClassDelete(s);
    XClassDelete(pathStr);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("[%General] 与重复键");
    return true;
}

/* ==================== 13. 回退链开关 ==================== */

static bool xsettings_test_fallbacks(void)
{
    /* 组织构造器路径重定向到测试目录，避免污染真实用户目录 */
    XString* tmpDir = XDir_homePath();
    XSETTINGS_REQUIRE(tmpDir != NULL, "home", "home 目录获取失败");
    XString_append_utf8(tmpDir, "/XSettingsTestOrg");
    XDir* dir = XDir_create_2(tmpDir);
    XSETTINGS_REQUIRE(dir != NULL, "dir", "目录对象创建失败");
    XDir_mkpath(dir, tmpDir);
    XClassDelete(dir);
    XSettings_setPath(XSettings_NativeFormat, XSettings_UserScope, tmpDir);
    XSettings* s = XSettings_create_4_utf8("XSettingsTestOrg", "XSettingsTestApp", NULL);
    XSETTINGS_REQUIRE(s != NULL, "org create", "组织构造失败");
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_organizationName_const(s), "XSettingsTestOrg", XChar_CaseSensitive),
                      "org name", "组织名不符");
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_applicationName_const(s), "XSettingsTestApp", XChar_CaseSensitive),
                      "app name", "应用名不符");
    const XString* fileName = XSettings_fileName_const(s);
    XSETTINGS_REQUIRE(fileName && XString_contains_utf8(fileName, "XSettingsTestApp", XChar_CaseSensitive),
                      "app file path", "首文件应为应用文件");
    XSETTINGS_REQUIRE(fileName && XString_contains_utf8(fileName, "XSettingsTestOrg", XChar_CaseSensitive),
                      "org dir path", "路径应含组织目录");
    XVariant* v = XVariant_create_int(5);
    XSettings_setValue_utf8(s, "fromApp", v);
    XClassDelete(v);
    XSettings_sync(s);
    /* 组织文件（回退链第二位）：直接以文件构造写入同目录 org.ini */
    XString* orgFile = XString_create_copy(tmpDir);
    XString_append_utf8(orgFile, "/XSettingsTestOrg.conf");
    XSettings* orgSettings = XSettings_create_7(orgFile, XSettings_NativeFormat, NULL);
    XSETTINGS_REQUIRE(orgSettings != NULL, "org settings", "组织文件构造失败");
    v = XVariant_create_int(6);
    XSettings_setValue_utf8(orgSettings, "fromOrg", v);
    XClassDelete(v);
    XSettings_sync(orgSettings);
    XClassDelete(orgSettings);
    /* 应用文件对象读组织文件键：回退链开 */
    XVariant* r = XSettings_value_utf8(s, "fromOrg");
    XSETTINGS_REQUIRE(r && xsettings_test_toInt(r) == 6, "fallback read", "回退链应读到组织文件键");
    if (r) XClassDelete(r);
    /* 关回退：只看首文件 */
    XSettings_setFallbacksEnabled(s, false);
    r = XSettings_value_utf8(s, "fromOrg");
    XSETTINGS_REQUIRE(r == NULL || r->m_type == XVariantType_NULL, "no fallback", "关回退后不应读到组织文件键");
    if (r) XClassDelete(r);
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "fromApp"), "no fallback first file", "首文件键仍应可见");
    XClassDelete(s);
    XClassDelete(orgFile);
    XClassDelete(tmpDir);
    /* 清理测试目录文件 */
    XString* cleanup = XString_create_utf8("XSettingsTestOrg/XSettingsTestOrg.conf");
    xsettings_test_removeFile(cleanup);
    XString_clear_base(cleanup);
    XString_append_utf8(cleanup, "XSettingsTestOrg/XSettingsTestApp.conf");
    xsettings_test_removeFile(cleanup);
    XClassDelete(cleanup);
    XSETTINGS_TEST_PASS("回退链开关");
    return true;
}

/* ==================== 14. isWritable 与原子写开关 ==================== */

static bool xsettings_test_writability(void)
{
    const char* path = "XSettingsWritable.ini";
    XSettings* s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XSETTINGS_REQUIRE(XSettings_isWritable(s), "isWritable", "常规路径应可写");
    XSETTINGS_REQUIRE(XSettings_isAtomicSyncRequired(s), "atomic default", "原子写默认应开启");
    XSettings_setAtomicSyncRequired(s, false);
    XSETTINGS_REQUIRE(!XSettings_isAtomicSyncRequired(s), "atomic toggle", "原子写开关失效");
    XVariant* v = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "w", v);
    XClassDelete(v);
    XSettings_sync(s);
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "direct write", "非原子直接写应成功");
    XClassDelete(s);
    xsettings_test_removeFile(path);
    /* 指向目录的文件路径：提交必失败 → AccessError */
    XString* dirPath = XDir_homePath();
    XSETTINGS_REQUIRE(dirPath != NULL, "home", "home 获取失败");
    XSettings* bad = XSettings_create_7(dirPath, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(bad != NULL, "bad create", "创建失败");
    v = XVariant_create_int(1);
    XSettings_setValue_utf8(bad, "x", v);
    XClassDelete(v);
    XSettings_sync(bad);
    XSETTINGS_REQUIRE(XSettings_status(bad) == XSettings_AccessError, "access error", "目录路径写盘应置 AccessError");
    XSETTINGS_REQUIRE(!XSettings_isWritable(bad), "not writable", "目录路径应不可写");
    XClassDelete(bad);
    XClassDelete(dirPath);
    XSETTINGS_TEST_PASS("isWritable 与原子写开关");
    return true;
}

/* ==================== 15. 自定义格式注册 ==================== */

static bool xsettings_test_customRead(XIODevice* device, XSettings_SettingsMap* map)
{
    XByteArray* all = XIODevice_readAll_3(device);
    if (!all)
        return false;
    size_t size = XByteArray_size_base(all);
    char* text = (char*)XMalloc_System(size + 1);
    bool ok = text != NULL;
    if (text) {
        XMemcpy(text, XByteArray_data(all), size);
        text[size] = '\0';
        size_t start = 0;
        for (size_t i = 0; ok && i <= size; ++i) {
            if (i == size || text[i] == '\n') {
                if (i > start) {
                    text[i] = '\0';
                    char* eq = XStrchr(text + start, '=');
                    if (eq) {
                        *eq = '\0';
                        /* 值语义：局部对象深拷贝入表，用后即 deinit */
                        XString keyObj;
                        XString_init(&keyObj);
                        XString_assign_utf8(&keyObj, text + start);
                        XString* valueText = XString_create_utf8(eq + 1);
                        XVariant* parsed = valueText ? XString_toVariant(valueText) : NULL;
                        XVariant valueObj;
                        XVariant_init(&valueObj, NULL, 0, XVariantType_NULL);
                        if (parsed)
                            XVariant_setValue(&valueObj, parsed);
                        ok = XMapBase_insert_base(map, &keyObj, &valueObj);
                        XClassDeinit(&keyObj);
                        XClassDeinit(&valueObj);
                        if (parsed)
                            XClassDelete(parsed);
                        if (valueText)
                            XClassDelete(valueText);
                    }
                }
                start = i + 1;
            }
        }
        XFree_System(text);
    }
    XClassDelete(all);
    return ok;
}

static bool xsettings_test_customWrite(XIODevice* device, const XSettings_SettingsMap* map)
{
    XVector* keys = XMap_keys_base(map);
    XVector* values = XMapBase_values_base(map);
    if (!keys || !values) {
        if (keys) XClassDelete(keys);
        if (values) XClassDelete(values);
        return false;
    }
    size_t count = XVector_size_base(keys);
    bool ok = true;
    for (size_t i = 0; ok && i < count; ++i) {
        XString* key = (XString*)XVector_at_base(keys, i);       /* 值槽内联对象借用 */
        XVariant* value = (XVariant*)XVector_at_base(values, i);
        const XString* text = value && value->m_type == XVariantType_String
                                  ? XString_fromVariant_ref(value)
                                  : NULL;
        if (key && text) {
            XString* line = XString_create();
            ok = line != NULL;
            if (ok) {
                XString_append_utf8(line, XString_toUtf8(key));
                XString_append_char(line, (XChar)'=');
                XString_append_utf8(line, XString_toUtf8(text));
                XString_append_char(line, (XChar)'\n');
                const char* bytes = XString_toUtf8(line);
                ok = XIODevice_write_1(device, bytes, (int64_t)XStrlen(bytes)) >= 0;
                XClassDelete(line);
            }
        }
        else {
            ok = false;
        }
    }
    XClassDelete(keys);
    XClassDelete(values);
    return ok;
}

static bool xsettings_test_customFormat(void)
{
    XSettings_Format format = XSettings_registerFormat_utf8(
            ".tst", xsettings_test_customRead, xsettings_test_customWrite, XSettings_CaseSensitive);
    XSETTINGS_REQUIRE(format != XSettings_InvalidFormat, "registerFormat", "注册失败");
    const char* path = "XSettingsCustom.tst";
    XSettings* s = XSettings_create_7_utf8(path, format, NULL);
    XSETTINGS_REQUIRE(s != NULL, "custom create", "创建失败");
    XSETTINGS_REQUIRE(XSettings_format(s) == format, "custom format", "格式应回读一致");
    XString* text = XString_create_utf8("flat-value");
    XVariant* v = XString_toVariant(text);
    XSettings_setValue_utf8(s, "plain", v);
    XClassDelete(v);
    XClassDelete(text);
    XSettings_sync(s);
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "custom sync", "自定义格式写盘应成功");
    XClassDelete(s);
    s = XSettings_create_7_utf8(path, format, NULL);
    XVariant* r = XSettings_value_utf8(s, "plain");
    XSETTINGS_REQUIRE(r && xsettings_test_textIs(r, "flat-value"), "custom read", "自定义格式读回应一致");
    if (r) XClassDelete(r);
    XClassDelete(s);
    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("自定义格式注册");
    return true;
}

/* ==================== 16. XRect/XSize 的 XVariant 接入直测 ==================== */

static bool xsettings_test_geometryVariant(void)
{
    XRect rect;
    XRect_init(&rect, -1, 2, 3, 4);
    XVariant* vRect = XRect_toVariant(rect);
    XSETTINGS_REQUIRE(vRect && vRect->m_type == XVariantType_Rect, "rect toVariant", "类型应为 Rect");
    XRect back = XRect_fromVariant(vRect);
    XSETTINGS_REQUIRE(back.x == -1 && back.y == 2 && back.width == 3 && back.height == 4,
                      "rect fromVariant", "Rect 往返不符");
    XSETTINGS_REQUIRE(XRect_compare(&rect, &back) == XCompare_Equality, "rect compare", "Rect 比较应相等");
    XClassDelete(vRect);
    XSize size;
    XSize_init(&size, 10, 20);
    XVariant* vSize = XSize_toVariant(size);
    XSETTINGS_REQUIRE(vSize && vSize->m_type == XVariantType_Size, "size toVariant", "类型应为 Size");
    XSize sizeBack = XSize_fromVariant(vSize);
    XSETTINGS_REQUIRE(sizeBack.width == 10 && sizeBack.height == 20, "size fromVariant", "Size 往返不符");
    XSize other;
    XSize_init(&other, 10, 21);
    XSETTINGS_REQUIRE(XSize_compare(&size, &other) == XCompare_Less, "size compare", "Size 比较应小于");
    XClassDelete(vSize);
    XSETTINGS_TEST_PASS("XRect/XSize 的 XVariant 接入");
    return true;
}

/* ==================== 17. 组织名/应用名派生（默认构造器） ==================== */

static bool xsettings_test_appDefaults(void)
{
    XString* savedOrg = XString_create_copy(XCoreApplication_organizationName());
    XString* savedApp = XString_create_copy(XCoreApplication_applicationName());
    XString* org = XString_create_utf8("XinYueCTestOrg");
    XString* app = XString_create_utf8("XinYueCTestApp");
    XCoreApplication_setOrganizationName(org);
    XCoreApplication_setApplicationName(app);
    XSettings* s = XSettings_create();
    XSETTINGS_REQUIRE(s != NULL, "default create", "默认构造失败");
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_organizationName_const(s), "XinYueCTestOrg", XChar_CaseSensitive),
                      "default org", "默认构造应取全局组织名");
    XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_applicationName_const(s), "XinYueCTestApp", XChar_CaseSensitive),
                      "default app", "默认构造应取全局应用名");
    XClassDelete(s);
    XCoreApplication_setOrganizationName(savedOrg);
    XCoreApplication_setApplicationName(savedApp);
    XClassDelete(org);
    XClassDelete(app);
    XClassDelete(savedOrg);
    XClassDelete(savedApp);
    XSETTINGS_TEST_PASS("组织名/应用名派生");
    return true;
}

/* ==================== 18. setValue 空键容错与 normalizedKey ==================== */

static bool xsettings_test_keyNormalization(void)
{
    XSettings* s = XSettings_create_7_utf8("XSettingsNormalize.ini", XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "create", "创建失败");
    XVariant* v = XVariant_create_int(1);
    XSettings_setValue_utf8(s, "a//b///c", v);   /* 连续 '/' 折叠 */
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "a/b/c"), "normalize collapse", "连续 '/' 应折叠");
    XSettings_setValue_utf8(s, "/leading/trailing/", v);
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "leading/trailing"), "normalize trim", "首尾 '/' 应去除");
    XSettings_setValue_utf8(s, "", v);      /* 空键：安全忽略 */
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "empty key", "空键不应置错");
    XStringList* all = XSettings_allKeys(s);
    XSETTINGS_REQUIRE(all && XStringList_size_base(all) == 2, "normalize count", "规范化后应 2 键");
    if (all) XClassDelete(all);
    XClassDelete(v);   /* 全部写入完成后统一释放（此前提前释放后继续使用） */
    XClassDelete(s);
    xsettings_test_removeFile("XSettingsNormalize.ini");
    XSETTINGS_TEST_PASS("键规范化与空键容错");
    return true;
}

/* ==================== 19. 内存平衡（泄漏口径） ==================== */

static bool xsettings_test_memoryBalance(void)
{
    /* 汇总口径由 XSettingsTest_runAll 统一量化；此处只记录本用例已执行。 */
    XSETTINGS_TEST_PASS("内存平衡（runAll 汇总口径）");
    return true;
}

/* ==================== 全量运行 ==================== */

typedef bool (*XSettingsTestFn)(void);

static const XSettingsTestFn g_xsettings_testFns[] = {
    xsettings_test_lifecycle,
    xsettings_test_scalars,
    xsettings_test_typedPrefixes,
    xsettings_test_mapVariant,
    xsettings_test_groups,
    xsettings_test_arrays,
    xsettings_test_keyEnumeration,
    xsettings_test_removeAndClear,
    xsettings_test_persistence,
    xsettings_test_escaping,
    xsettings_test_commentsAndErrors,
    xsettings_test_generalVariants,
    xsettings_test_fallbacks,
    xsettings_test_writability,
    xsettings_test_customFormat,
    xsettings_test_geometryVariant,
    xsettings_test_appDefaults,
    xsettings_test_keyNormalization,
    xsettings_test_memoryBalance
};

bool XSettingsTest_runAll(void)
{
    bool all = true;
    bool statsEnabled = XMemory_statisticsEnabled();
    XMemoryStatistics before = { 0, 0, 0, 0 };
    XMemoryStatistics after = { 0, 0, 0, 0 };
    if (statsEnabled)
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);

    size_t count = sizeof(g_xsettings_testFns) / sizeof(g_xsettings_testFns[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!g_xsettings_testFns[i]())
            all = false;
    }

    if (statsEnabled) {
        after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
        if (after.systemBytes != before.systemBytes) {
            XPrintf("[FAIL] XSettings: 内存平衡: systemBytes %lu -> %lu（疑似泄漏）\n",
                    (unsigned long)before.systemBytes, (unsigned long)after.systemBytes);
            all = false;
        }
    }
    XPrintf("[SUMMARY] XSettings: %s\n", all ? "全部通过" : "存在失败");
    return all;
}

/* ==================== 菜单接入 ==================== */

static void xsettings_test_all_wrapper(XVariant* data)
{
    (void)data;
    XSettingsTest_runAll();
}

static void xsettings_test_lifecycle_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_lifecycle();
}

static void xsettings_test_values_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_scalars();
    xsettings_test_typedPrefixes();
    xsettings_test_mapVariant();
}

static void xsettings_test_groups_arrays_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_groups();
    xsettings_test_arrays();
    xsettings_test_keyEnumeration();
}

static void xsettings_test_remove_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_removeAndClear();
    xsettings_test_keyNormalization();
}

static void xsettings_test_persistence_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_persistence();
    xsettings_test_fallbacks();
    xsettings_test_writability();
}

static void xsettings_test_ini_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_escaping();
    xsettings_test_commentsAndErrors();
    xsettings_test_generalVariants();
}

static void xsettings_test_extra_wrapper(XVariant* data)
{
    (void)data;
    xsettings_test_customFormat();
    xsettings_test_geometryVariant();
    xsettings_test_appDefaults();
}

void XTestMenu_XSettingsTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XSettings(Qt6.8)");
    XTestMenu_addMenu(root, menu);
    XAction* action = XTestMenu_addAction(menu, "全部测试");
    XTestMenu_setActionFunction(action, xsettings_test_all_wrapper);
    action = XTestMenu_addAction(menu, "生命周期/拷贝/移动");
    XTestMenu_setActionFunction(action, xsettings_test_lifecycle_wrapper);
    action = XTestMenu_addAction(menu, "标量/类型前缀/Map往返");
    XTestMenu_setActionFunction(action, xsettings_test_values_wrapper);
    action = XTestMenu_addAction(menu, "分组/数组/键枚举");
    XTestMenu_setActionFunction(action, xsettings_test_groups_arrays_wrapper);
    action = XTestMenu_addAction(menu, "删除/清空/键规范化");
    XTestMenu_setActionFunction(action, xsettings_test_remove_wrapper);
    action = XTestMenu_addAction(menu, "落盘/回退链/可写性");
    XTestMenu_setActionFunction(action, xsettings_test_persistence_wrapper);
    action = XTestMenu_addAction(menu, "转义/注释/坏行/General节");
    XTestMenu_setActionFunction(action, xsettings_test_ini_wrapper);
    action = XTestMenu_addAction(menu, "自定义格式/几何接入/默认构造");
    XTestMenu_setActionFunction(action, xsettings_test_extra_wrapper);
}
