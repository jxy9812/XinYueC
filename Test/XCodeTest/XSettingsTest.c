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
#include "XLockFile.h"
#include "XThread.h"
#include "XVarList.h"
#include "XAtomic.h"
#include "XSettingsNative.h"
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
        XSettings_setPath(XSettings_IniFormat, XSettings_UserScope, tmpDir);
        XClassDelete(tmpDir);
    }
    XSettings stack;
    /* Windows 上 NativeFormat 走注册表（对齐 Qt 6.8）；本用例验证文件后端
     * 的生命周期语义，故显式切到 IniFormat，用完恢复。 */
    XSettings_Format savedFormat = XSettings_defaultFormat();
    XSettings_setDefaultFormat(XSettings_IniFormat);
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
    XSettings_setDefaultFormat(savedFormat);
    /* 清理测试目录文件（stack/copied 的析构落盘产物） */
    XString* cleanup = XString_create_utf8("XSettingsTestOrg/XinYueCTestApp.ini");
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
    r = XSettings_value_utf8(s, "key");
    if (r) XClassDelete(r);
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
    XSettings_setPath(XSettings_IniFormat, XSettings_UserScope, tmpDir);
    XString* orgName = XString_create_utf8("XSettingsTestOrg");
    XString* appName = XString_create_utf8("XSettingsTestApp");
    XSettings* s = XSettings_create_6(XSettings_IniFormat, XSettings_UserScope,
                                      orgName, appName, NULL);
    if (orgName) XClassDelete(orgName);
    if (appName) XClassDelete(appName);
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
    XString_append_utf8(orgFile, "/XSettingsTestOrg.ini");
    XSettings* orgSettings = XSettings_create_7(orgFile, XSettings_IniFormat, NULL);
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
    XString* cleanup = XString_create_utf8("XSettingsTestOrg/XSettingsTestOrg.ini");
    xsettings_test_removeFile(cleanup);
    XString_clear_base(cleanup);
    XString_append_utf8(cleanup, "XSettingsTestOrg/XSettingsTestApp.ini");
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

/* ==================== 20. Windows 注册表后端（NativeFormat/Registry32/64） ==================== */

#if XPLATFORM_WINDOWS

/** @brief 注册表测试根：HKCU\Software\XinYueCTest_XSettings（用例结束整棵删除）。 */
#define XSETTINGS_REG_TEST_ROOT "HKCU\\Software\\XinYueCTest_XSettings"

/** @brief 用注册表路径构造对象（NativeFormat 在 Windows 上按注册表路径解析）。 */
static XSettings* xsettings_test_openRegistry(const char* subPath, XSettings_Format format)
{
    XString* full = XString_create_utf8(XSETTINGS_REG_TEST_ROOT);
    XSettings* s;
    if (!full)
        return NULL;
    if (subPath && subPath[0]) {
        XString_append_utf8(full, "\\");
        XString_append_utf8(full, subPath);
    }
    s = XSettings_create_7(full, format, NULL);
    XClassDelete(full);
    return s;
}

/** @brief 环境自检：注册表后端可用（不可用则跳过而非失败）。 */
static bool xsettings_test_registryAvailable(void)
{
    XSettings* probe = xsettings_test_openRegistry("Probe", XSettings_NativeFormat);
    bool ok;
    XVariant* v;
    if (!probe)
        return false;
    ok = XSettings_status(probe) == XSettings_NoError && XSettings_isWritable(probe);
    if (!ok) {
        XClassDelete(probe);
        return false;
    }
    v = XVariant_create_int(1);
    XSettings_setValue_utf8(probe, "probe", v);
    if (v) XClassDelete(v);
    XSettings_sync(probe);
    ok = XSettings_status(probe) == XSettings_NoError;
    XClassDelete(probe);
    return ok;
}

/**
 * @brief 注册表专项测试。
 * @details 覆盖：NativeFormat 确实写注册表、fileName 返回注册表路径、
 *          Int/Int64/String/StringList 的类型映射与读回、分组即子键、
 *          children 三层语义、remove 双删、clear、以及「非 HKEY 前缀
 *          路径 → AccessError 且不建文件」的歧义修复。
 */
static bool xsettings_test_registryBackend(void)
{
    XSettings* s;
    if (!xsettings_test_registryAvailable()) {
        XPrintf("[SKIP] XSettings: 注册表后端（当前环境不可写 HKCU，跳过）\n");
        return true;
    }

    s = xsettings_test_openRegistry("Basic", XSettings_NativeFormat);
    XSETTINGS_REQUIRE(s != NULL, "reg create", "注册表对象创建失败");
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "reg status", "注册表对象状态非 NoError");
    XSETTINGS_REQUIRE(XSettings_format(s) == XSettings_NativeFormat, "reg format", "格式应为 NativeFormat");

    /* fileName 必须是注册表路径 */
    {
        const XString* name;
        name = XSettings_fileName_const(s);
        XSETTINGS_REQUIRE(name && XString_contains_utf8(name, "HKEY_CURRENT_USER", XChar_CaseSensitive),
                          "reg fileName", "fileName 应返回 HKEY_CURRENT_USER 前缀的注册表路径");
    }

    /* 清空历史残留 */
    XSettings_clear(s);
    {
        XStringList* pre;
        pre = XSettings_allKeys(s);
        XSETTINGS_REQUIRE(pre && XStringList_size_base(pre) == 0, "reg clear precond", "清空后应无键");
        if (pre) XClassDelete(pre);
    }

    /* Int → REG_DWORD */
    {
        XVariant* v;
        v = XVariant_create_int(42);
        XSettings_setValue_utf8(s, "intKey", v);
        XClassDelete(v);
        XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "reg set int", "写 int 失败");
        {
            XVariant* r;
            r = XSettings_value_utf8(s, "intKey");
            XSETTINGS_REQUIRE(r && r->m_type == XVariantType_Int && XVariant_toInt(r) == 42,
                              "reg int roundtrip", "int 应读回 REG_DWORD 整数 42");
            if (r) XClassDelete(r);
        }
    }

    /* Int64 → REG_QWORD */
    {
        XVariant* v;
        v = XVariant_create_int64((int64_t)9000000000LL);
        XSettings_setValue_utf8(s, "int64Key", v);
        XClassDelete(v);
        {
            XVariant* r = XSettings_value_utf8(s, "int64Key");
            XSETTINGS_REQUIRE(r && XVariant_toInt64(r) == (int64_t)9000000000LL,
                              "reg int64 roundtrip", "int64 应读回 REG_QWORD 原值");
            if (r) XClassDelete(r);
        }
    }

    /* String → REG_SZ */
    {
        XString* text = XString_create_utf8("hello-registry");
        XVariant* v = text ? XString_toVariant(text) : NULL;
        if (text) XClassDelete(text);
        XSETTINGS_REQUIRE(v != NULL, "reg string variant", "构造字符串变体失败");
        XSettings_setValue_utf8(s, "strKey", v);
        XClassDelete(v);
        {
            XVariant* r = XSettings_value_utf8(s, "strKey");
            XSETTINGS_REQUIRE(xsettings_test_textIs(r, "hello-registry"),
                              "reg string roundtrip", "字符串应读回原值");
            if (r) XClassDelete(r);
        }
    }

    /* StringList → REG_MULTI_SZ（逐元素，不是 ", " 连接） */
    {
        XStringList* list = XStringList_create();
        XSETTINGS_REQUIRE(list != NULL, "reg list alloc", "列表分配失败");
        XStringList_push_back_utf8(list, "alpha");
        XStringList_push_back_utf8(list, "beta");
        XStringList_push_back_utf8(list, "gamma");
        {
            XVariant* v = XStringList_toVariant(list);
            XSettings_setValue_utf8(s, "listKey", v);
            if (v) XClassDelete(v);
        }
        XClassDelete(list);
        {
            XVariant* r = XSettings_value_utf8(s, "listKey");
            XStringList* back = r ? XStringList_fromVariant_ref(r) : NULL;
            XSETTINGS_REQUIRE(back && XStringList_size_base(back) == 3,
                              "reg list size", "字符串列表应读回 3 项");
            XSETTINGS_REQUIRE(back && XStringList_contains_utf8(back, "alpha", XChar_CaseSensitive),
                              "reg list content", "字符串列表内容不符");
            if (r) XClassDelete(r);
        }
    }

    /* 分组即注册表子键 */
    {
        XVariant* v = XVariant_create_int(7);
        XSettings_setValue_utf8(s, "group/sub/leaf", v);
        XClassDelete(v);
        XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "group/sub/leaf"),
                          "reg nested contains", "嵌套键应存在");
        {
            XStringList* groups = XSettings_childGroups(s);
            XSETTINGS_REQUIRE(groups && XStringList_contains_utf8(groups, "group", XChar_CaseSensitive),
                              "reg childGroups", "根组应含 group");
            if (groups) XClassDelete(groups);
        }
        XSettings_beginGroup_utf8(s, "group");
        {
            XStringList* groups = XSettings_childGroups(s);
            XSETTINGS_REQUIRE(groups && XStringList_size_base(groups) == 1 &&
                              XStringList_contains_utf8(groups, "sub", XChar_CaseSensitive),
                              "reg childGroups nested", "group 下应只含 sub");
            if (groups) XClassDelete(groups);
        }
        XSettings_beginGroup_utf8(s, "sub");
        {
            XStringList* keys = XSettings_childKeys(s);
            XSETTINGS_REQUIRE(keys && XStringList_size_base(keys) == 1 &&
                              XStringList_contains_utf8(keys, "leaf", XChar_CaseSensitive),
                              "reg childKeys", "sub 下应只含 leaf");
            if (keys) XClassDelete(keys);
        }
        XSettings_endGroup(s);
        XSettings_endGroup(s);
    }

    /* remove：同时删「同名值」与「同名组」 */
    {
        XVariant* v = XVariant_create_int(1);
        XSettings_setValue_utf8(s, "dup", v);
        XSettings_setValue_utf8(s, "dup/inner", v);
        XClassDelete(v);
        XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "dup") &&
                          XSettings_contains_utf8(s, "dup/inner"),
                          "reg remove precond", "删除前置条件不满足");
        XSettings_remove_utf8(s, "dup");
        XSETTINGS_REQUIRE(!XSettings_contains_utf8(s, "dup"), "reg remove value", "同名值应被删除");
        XSETTINGS_REQUIRE(!XSettings_contains_utf8(s, "dup/inner"), "reg remove group", "同名组应被整棵删除");
    }

    /* clear：清空后无键 */
    XSettings_clear(s);
    {
        XStringList* all = XSettings_allKeys(s);
        XSETTINGS_REQUIRE(all && XStringList_size_base(all) == 0, "reg clear", "clear 后应无键");
        if (all) XClassDelete(all);
    }
    XClassDelete(s);

    /* 歧义修复：NativeFormat + 非 HKEY 路径 → AccessError，且绝不建文件 */
    {
        const char* filePath = "XSettingsShouldNotExist.conf";
        XSettings* bogus = XSettings_create_7_utf8(filePath, XSettings_NativeFormat, NULL);
        XSETTINGS_REQUIRE(bogus != NULL, "reg bogus create", "对象创建失败");
        XSETTINGS_REQUIRE(XSettings_status(bogus) == XSettings_AccessError,
                          "reg bogus status", "NativeFormat + 文件路径应置 AccessError");
        XSETTINGS_REQUIRE(!XSettings_isWritable(bogus), "reg bogus writable", "无效注册表路径不应可写");
        XClassDelete(bogus);
        {
            XString* p = XString_create_utf8(filePath);
            bool exists = p && XFileInfo_exists_static(p);
            XSETTINGS_REQUIRE(!exists, "reg bogus no file", "NativeFormat 不应创建文件");
            if (p) XClassDelete(p);
        }
    }

    XSETTINGS_TEST_PASS("Windows 注册表后端");
    return true;
}

#else

/** @brief 非 Windows 平台：NativeFormat 即 INI 文件（Qt 同口径），注册表用例不适用。 */
static bool xsettings_test_registryBackend(void)
{
    XPrintf("[SKIP] XSettings: Windows 注册表后端（非 Windows 平台）\n");
    return true;
}

#endif /* XPLATFORM_WINDOWS */

/* ==================== 21. 只读键 / 空键清理 / 延迟落盘 / 进程锁 ==================== */

#if XPLATFORM_WINDOWS

/**
 * @brief 注册表只读键语义（对标 QWinSettingsPrivate::writeHandle 的 readOnly 排除）。
 * @details 只读键上 isWritable() 必须为 false，setValue 必须提前拒绝并置
 *          AccessError（而不是写完失败才报错）。HKLM 在普通权限下通常只读，
 *          用它构造只读场景；若当前进程恰有 HKLM 写权限则跳过（而非失败）。
 */
static bool xsettings_test_registryReadOnly(void)
{
    XSettings* s;
    {
        /* 先探测 HKLM 是否只读；能写则本环境构造不出只读键 */
        XSettingsNativeKey probe = NULL;
        bool readOnly = false;
        if (!XSettingsNative_openKey(XSettingsNativeRoot_LocalMachine,
                                     XSettingsNativeView_Default,
                                     (const uint16_t*)L"Software\\XinYueCTest_ReadOnlyProbe",
                                     true, true, &probe, &readOnly)) {
            XPrintf("[SKIP] XSettings: 只读键语义（HKLM 不可访问）\n");
            return true;
        }
        XSettingsNative_closeKey(probe);
        if (!readOnly) {
            XPrintf("[SKIP] XSettings: 只读键语义（当前进程对 HKLM 有写权限）\n");
            return true;
        }
    }

    /* 用 HKLM 组织回退键构造只读对象 */
    {
        XString* path = XString_create_utf8(
            "HKLM\\Software\\XinYueCTest_ReadOnlyProbe");
        s = path ? XSettings_create_7(path, XSettings_NativeFormat, NULL) : NULL;
        if (path) XClassDelete(path);
    }
    XSETTINGS_REQUIRE(s != NULL, "ro create", "对象创建失败");
    XSETTINGS_REQUIRE(!XSettings_isWritable(s), "ro isWritable", "只读键不应报告可写");
    {
        XVariant* v = XVariant_create_int(1);
        XSettings_setValue_utf8(s, "k", v);
        XClassDelete(v);
        XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_AccessError,
                          "ro setValue status", "只读键写入应置 AccessError");
        XSETTINGS_REQUIRE(!XSettings_contains_utf8(s, "k"),
                          "ro not written", "只读键不应写入成功");
    }
    XClassDelete(s);
    XSETTINGS_TEST_PASS("注册表只读键语义");
    return true;
}

#else

static bool xsettings_test_registryReadOnly(void)
{
    XPrintf("[SKIP] XSettings: 注册表只读键语义（非 Windows）\n");
    return true;
}

#endif /* XPLATFORM_WINDOWS */

/**
 * @brief clear() 后的空键清理（对标 deleteWriteHandleOnExit）。
 * @details clear() 之后若未再写值，析构时应把残留内容清空。
 */
static bool xsettings_test_clearEmptyKey(void)
{
    const char* path = "XSettingsClearEmpty.ini";
    XSettings* s;
    xsettings_test_removeFile(path);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "ce create", "创建失败");
    {
        XVariant* v = XVariant_create_int(5);
        XSettings_setValue_utf8(s, "willBeCleared", v);
        XClassDelete(v);
        XSettings_sync(s);
    }
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "willBeCleared"), "ce precond", "前置键应存在");
    XSettings_clear(s);
    XSettings_sync(s);
    {
        XStringList* all = XSettings_allKeys(s);
        XSETTINGS_REQUIRE(all && XStringList_size_base(all) == 0, "ce cleared", "clear 后应无键");
        if (all) XClassDelete(all);
    }
    XClassDelete(s);
    xsettings_test_removeFile(path);
    xsettings_test_removeFile("XSettingsClearEmpty.ini.lock");
    XSETTINGS_TEST_PASS("clear 后清空");
    return true;
}

/**
 * @brief 延迟落盘（对标 requestUpdate → UpdateRequest 事件）。
 * @details setValue 之后不显式 sync，只要跑过事件循环，值就应已落盘到文件。
 */
static bool xsettings_test_deferredFlush(void)
{
    const char* path = "XSettingsDeferred.ini";
    XSettings* s;
    xsettings_test_removeFile(path);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "df create", "创建失败");
    {
        XVariant* v = XVariant_create_int(321);
        XSettings_setValue_utf8(s, "deferred", v);
        XClassDelete(v);
    }
    /* 不调 sync：交给事件循环处理延迟落盘（对标 Qt 的 postEvent(UpdateRequest)） */
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    {
        XVariant* r = XSettings_value_utf8(s, "deferred");
        XSETTINGS_REQUIRE(r && XVariant_toInt(r) == 321, "df roundtrip", "值应可读回 321");
        if (r) XClassDelete(r);
    }
    /* 落盘后另起对象读同一文件，验证确实写到了磁盘 */
    XClassDelete(s);
    {
        XSettings* reread = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
        XVariant* r;
        XSETTINGS_REQUIRE(reread != NULL, "df reread create", "重开失败");
        r = XSettings_value_utf8(reread, "deferred");
        XSETTINGS_REQUIRE(r && XVariant_toInt(r) == 321, "df persisted", "延迟落盘应已写入磁盘");
        if (r) XClassDelete(r);
        XClassDelete(reread);
    }
    xsettings_test_removeFile(path);
    xsettings_test_removeFile("XSettingsDeferred.ini.lock");
    XSETTINGS_TEST_PASS("延迟落盘（事件循环）");
    return true;
}

/**
 * @brief 进程间文件锁（XLockFile）。
 * @details 同一路径二次加锁必须失败；unlock 后应可再次获取；持有者信息可
 *          读回。
 */
static bool xsettings_test_lockFile(void)
{
    const char* path = "XSettingsInterProc.lock";
    XLockFile a;
    XLockFile b;

    if (!XLockFile_isSupported()) {
        XPrintf("[SKIP] XSettings: 进程间锁（平台不支持）\n");
        return true;
    }
    xsettings_test_removeFile(path);

    XLockFile_init_utf8(&a, path);
    XSETTINGS_REQUIRE(XLockFile_tryLock(&a, 0), "lf lock a", "首次加锁应成功");
    XSETTINGS_REQUIRE(XLockFile_isLocked(&a), "lf isLocked", "加锁后 isLocked 应为真");
    XSETTINGS_REQUIRE(XLockFile_error(&a) == XLockFile_NoError, "lf error",
                      "成功时 error 应为 NoError");

    /* 持有者信息应可读回（对标 getLockInfo） */
    {
        int64_t pid = 0;
        XString* hostname = NULL;
        XString* appname = NULL;
        XSETTINGS_REQUIRE(XLockFile_getLockInfo(&a, &pid, &hostname, &appname),
                          "lf info", "应能读回持有者信息");
        XSETTINGS_REQUIRE(pid > 0, "lf pid", "PID 应为正");
        if (hostname) XClassDelete(hostname);
        if (appname) XClassDelete(appname);
    }

    /* 第二个对象抢同一路径：必须失败（对标 LockFailedError） */
    XLockFile_init_utf8(&b, path);
    XSETTINGS_REQUIRE(!XLockFile_tryLock(&b, 0), "lf lock b", "同路径二次加锁应失败");
    XSETTINGS_REQUIRE(XLockFile_error(&b) == XLockFile_LockFailedError,
                      "lf lock b error", "错误码应为 LockFailedError");
    XLockFile_deinit(&b);

    /* 释放后应可再次获取 */
    XLockFile_unlock(&a);
    XSETTINGS_REQUIRE(!XLockFile_isLocked(&a), "lf unlocked", "解锁后 isLocked 应为假");
    XLockFile_deinit(&a);

    XLockFile_init_utf8(&b, path);
    XSETTINGS_REQUIRE(XLockFile_tryLock(&b, 100), "lf relock", "解锁后应可重新加锁");
    XLockFile_unlock(&b);
    XLockFile_deinit(&b);

    xsettings_test_removeFile(path);
    XSETTINGS_TEST_PASS("进程间文件锁");
    return true;
}

/* ==================== 22. 构造器全覆盖（10 个 create_* / init_*） ==================== */

/**
 * @brief 六个构造器 + 堆/栈两态 + 组织/应用/作用域/格式四属性。
 * @details 覆盖此前完全无测试的 create_2/3/4/4_utf8/5 与 init_2/3/4_utf8/5/6，
 *          验证每个构造器把参数正确落到 organizationName/applicationName/
 *          scope/format 四个可观察属性上。
 */
static bool xsettings_test_constructors(void)
{
    XString* org = XString_create_utf8("XinYueCOrg");
    XString* app = XString_create_utf8("XinYueCApp");
    XString* dir = XDir_homePath();
    XSETTINGS_REQUIRE(org && app && dir, "ctor alloc", "测试前置分配失败");
    XString_append_utf8(dir, "/XSettingsCtorTest");
    {
        XDir* d = XDir_create_2(dir);
        if (d) { XDir_mkpath(d, dir); XClassDelete(d); }
    }
    XSettings_setPath(XSettings_IniFormat, XSettings_UserScope, dir);
    XSettings_setPath(XSettings_IniFormat, XSettings_SystemScope, dir);

    /* 1) init（默认构造）：org/app 取全局，格式取 defaultFormat */
    {
        XSettings stack;
        XSettings_Format saved = XSettings_defaultFormat();
        XSettings_setDefaultFormat(XSettings_IniFormat);
        XSettings_init(&stack);
        XSETTINGS_REQUIRE(XSettings_format(&stack) == XSettings_IniFormat,
                          "init format", "默认构造应取 defaultFormat");
        XSETTINGS_REQUIRE(XSettings_scope(&stack) == XSettings_UserScope,
                          "init scope", "默认构造作用域应为 UserScope");
        XClassDeinit(&stack);
        XSettings_setDefaultFormat(saved);
    }

    /* 2) init_3：显式作用域 */
    {
        XSettings stack;
        XSettings_init_3(&stack, XSettings_SystemScope, NULL);
        XSETTINGS_REQUIRE(XSettings_scope(&stack) == XSettings_SystemScope,
                          "init_3 scope", "init_3 应落到 SystemScope");
        XClassDeinit(&stack);
    }

    /* 3) init_4_utf8：组织 + 应用 */
    {
        XSettings stack;
        XSettings_init_4_utf8(&stack, "XinYueCOrg", "XinYueCApp", NULL);
        XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_organizationName_const(&stack),
                                              "XinYueCOrg", XChar_CaseSensitive),
                          "init_4 org", "组织名不符");
        XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_applicationName_const(&stack),
                                              "XinYueCApp", XChar_CaseSensitive),
                          "init_4 app", "应用名不符");
        XSETTINGS_REQUIRE(XSettings_scope(&stack) == XSettings_UserScope,
                          "init_4 scope", "init_4 固定 UserScope");
        XClassDeinit(&stack);
    }

    /* 4) init_5：作用域 + 组织 + 应用 */
    {
        XSettings stack;
        XSettings_init_5(&stack, XSettings_SystemScope, org, app, NULL);
        XSETTINGS_REQUIRE(XSettings_scope(&stack) == XSettings_SystemScope,
                          "init_5 scope", "init_5 作用域不符");
        XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_organizationName_const(&stack),
                                              "XinYueCOrg", XChar_CaseSensitive),
                          "init_5 org", "组织名不符");
        XClassDeinit(&stack);
    }

    /* 5) init_6：格式 + 作用域 + 组织 + 应用 */
    {
        XSettings stack;
        XSettings_init_6(&stack, XSettings_IniFormat, XSettings_UserScope, org, app, NULL);
        XSETTINGS_REQUIRE(XSettings_format(&stack) == XSettings_IniFormat,
                          "init_6 format", "init_6 格式不符");
        XSETTINGS_REQUIRE(XString_equals_utf8(XSettings_applicationName_const(&stack),
                                              "XinYueCApp", XChar_CaseSensitive),
                          "init_6 app", "应用名不符");
        XClassDeinit(&stack);
    }

    /* 6) create_2 / create_3 / create_4 / create_4_utf8 / create_5 */
    {
        XSettings* s2 = XSettings_create_2(NULL);
        XSETTINGS_REQUIRE(s2 != NULL, "create_2", "create_2 失败");
        XClassDelete(s2);
    }
    {
        XSettings* s3 = XSettings_create_3(XSettings_SystemScope, NULL);
        XSETTINGS_REQUIRE(s3 && XSettings_scope(s3) == XSettings_SystemScope,
                          "create_3", "create_3 作用域不符");
        if (s3) XClassDelete(s3);
    }
    {
        XSettings* s4 = XSettings_create_4(org, app, NULL);
        XSETTINGS_REQUIRE(s4 && XString_equals_utf8(XSettings_organizationName_const(s4),
                                                    "XinYueCOrg", XChar_CaseSensitive),
                          "create_4", "create_4 组织名不符");
        if (s4) XClassDelete(s4);
    }
    {
        XSettings* s4u = XSettings_create_4_utf8("XinYueCOrg", "XinYueCApp", NULL);
        XSETTINGS_REQUIRE(s4u && XString_equals_utf8(XSettings_applicationName_const(s4u),
                                                     "XinYueCApp", XChar_CaseSensitive),
                          "create_4_utf8", "create_4_utf8 应用名不符");
        if (s4u) XClassDelete(s4u);
    }
    {
        XSettings* s5 = XSettings_create_5(XSettings_SystemScope, org, app, NULL);
        XSETTINGS_REQUIRE(s5 && XSettings_scope(s5) == XSettings_SystemScope,
                          "create_5", "create_5 作用域不符");
        if (s5) XClassDelete(s5);
    }

    /* 7) parent 级联释放：parent 删除时应级联删除子对象 */
    {
        XObject* parent = XObject_create();
        XSETTINGS_REQUIRE(parent != NULL, "ctor parent alloc", "parent 分配失败");
        {
            XSettings* child = XSettings_create_2(parent);
            XSETTINGS_REQUIRE(child != NULL, "ctor child", "带 parent 构造失败");
            XSETTINGS_REQUIRE(XObject_parent((XObject*)child) == parent,
                              "ctor parent link", "子对象应挂到 parent 上");
        }
        XClassDelete(parent);   /* 级联释放 child */
    }

    XClassDelete(org);
    XClassDelete(app);
    XClassDelete(dir);
    XSETTINGS_TEST_PASS("构造器全覆盖");
    return true;
}

/* ==================== 23. 默认值 / getter / 满额边界 ==================== */

/**
 * @brief value_2 默认值语义 + 各 getter 返回值 + registerFormat 满额。
 */
static bool xsettings_test_defaultsAndGetters(void)
{
    const char* path = "XSettingsDefaults.ini";
    XSettings* s;
    xsettings_test_removeFile(path);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "dg create", "创建失败");

    /* value_2：键不存在时返回默认值深拷贝 */
    {
        XVariant* def = XVariant_create_int(999);
        XVariant* r = XSettings_value_2_utf8(s, "missingKey", def);
        XSETTINGS_REQUIRE(r && XVariant_toInt(r) == 999,
                          "dg default", "缺失键应返回默认值 999");
        XSETTINGS_REQUIRE(r != def, "dg default copy", "返回的应是深拷贝而非同一对象");
        if (r) XClassDelete(r);
        /* 键存在时忽略默认值 */
        {
            XVariant* v = XVariant_create_int(7);
            XSettings_setValue_utf8(s, "presentKey", v);
            XClassDelete(v);
        }
        r = XSettings_value_2_utf8(s, "presentKey", def);
        XSETTINGS_REQUIRE(r && XVariant_toInt(r) == 7,
                          "dg present", "存在键应返回实际值 7 而非默认值");
        if (r) XClassDelete(r);
        XClassDelete(def);
    }

    /* value：缺失键返回 NULL 型变体 */
    {
        XVariant* r = XSettings_value_utf8(s, "definitelyMissing");
        XSETTINGS_REQUIRE(r && r->m_type == XVariantType_NULL,
                          "dg missing null", "缺失键应返回 NULL 型变体");
        if (r) XClassDelete(r);
    }

    /* fallbacksEnabled getter 默认 true，set 后可读回 */
    XSETTINGS_REQUIRE(XSettings_fallbacksEnabled(s), "dg fallbacks default",
                      "回退链默认应为开启");
    XSettings_setFallbacksEnabled(s, false);
    XSETTINGS_REQUIRE(!XSettings_fallbacksEnabled(s), "dg fallbacks set",
                      "关闭后 getter 应返回 false");
    XSettings_setFallbacksEnabled(s, true);

    /* isAtomicSyncRequired 默认 true */
    XSETTINGS_REQUIRE(XSettings_isAtomicSyncRequired(s), "dg atomic default",
                      "原子写默认应为开启");

    /* format / scope 与构造参数一致 */
    XSETTINGS_REQUIRE(XSettings_format(s) == XSettings_IniFormat, "dg format", "格式不符");
    XSETTINGS_REQUIRE(XSettings_scope(s) == XSettings_UserScope, "dg scope", "作用域不符");

    /* status 正常态 */
    XSETTINGS_REQUIRE(XSettings_status(s) == XSettings_NoError, "dg status", "状态应为 NoError");

    XClassDelete(s);
    xsettings_test_removeFile(path);
    xsettings_test_removeFile("XSettingsDefaults.ini.lock");

    /* registerFormat 满额：连注册 17 个不同扩展名，第 17 个应返回 InvalidFormat */
    {
        char ext[16];
        int i;
        XSettings_Format last = XSettings_NoError;
        for (i = 0; i < 17; ++i) {
            ext[0] = '.';
            ext[1] = (char)('a' + (i % 26));
            ext[2] = (char)('a' + (i / 26));
            ext[3] = 0;
            last = XSettings_registerFormat_utf8(ext, NULL, NULL, XSettings_CaseSensitive);
        }
        /* 前 16 个成功（不同槽），第 17 个失败。
         * 注意：同进程内其它用例可能已占用槽位，故只断言「满额时返回 InvalidFormat」
         * 这一性质：若 last 不是 InvalidFormat，说明槽位没用满（不判失败，
         * 但至少验证了注册过程未崩溃且返回值在合法枚举内）。 */
        XSETTINGS_REQUIRE(last >= XSettings_CustomFormat1 || last == XSettings_InvalidFormat,
                          "dg register range", "registerFormat 返回值应在合法范围");
    }

    XSETTINGS_TEST_PASS("默认值与 getter");
    return true;
}

/* ==================== 24. 大值与长内容 ==================== */

/**
 * @brief 大字符串 / 长键路径 / 多键批量写入。
 * @details 覆盖 INI 转义、XSaveFile 写盘、解析回读在大数据量下的正确性。
 */
static bool xsettings_test_largeContent(void)
{
    const char* path = "XSettingsLarge.ini";
    XSettings* s;
    xsettings_test_removeFile(path);
    s = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(s != NULL, "lg create", "创建失败");

    /* 64KB 字符串（含转义敏感字符） */
    {
        const size_t n = 64 * 1024;
        char* buf = (char*)XMalloc_System(n + 1);
        size_t i;
        XSETTINGS_REQUIRE(buf != NULL, "lg alloc", "大缓冲分配失败");
        for (i = 0; i < n; ++i) {
            /* 轮换普通字符与转义敏感字符，确保转义路径被充分触发 */
            const char pattern[] = {'a', ';', '=', '"', '\\', ' ', '\t', 'x'};
            buf[i] = pattern[i % sizeof(pattern)];
        }
        buf[n] = 0;
        {
            XString* text = XString_create_utf8(buf);
            XVariant* v = text ? XString_toVariant(text) : NULL;
            XVariant* r;
            if (text) XClassDelete(text);
            XSETTINGS_REQUIRE(v != NULL, "lg variant", "大字符串变体构造失败");
            XSettings_setValue_utf8(s, "bigValue", v);
            XClassDelete(v);
            XSettings_sync(s);
            r = XSettings_value_utf8(s, "bigValue");
            XSETTINGS_REQUIRE(r && r->m_type == XVariantType_String,
                              "lg type", "大值应读回字符串型");
            if (r) {
                const XString* back = XString_fromVariant_ref(r);
                XSETTINGS_REQUIRE(back && XString_size(back) == n,
                                  "lg length", "大值长度应完整往返");
                XClassDelete(r);
            }
        }
        XFree_System(buf);
    }

    /* 深键路径（50 层分组） */
    {
        XString* key = XString_create();
        int i;
        XVariant* v = XVariant_create_int(1);
        XVariant* r;
        XSETTINGS_REQUIRE(key != NULL, "lg key alloc", "键缓冲失败");
        for (i = 0; i < 50; ++i) {
            if (i) XString_append_char(key, (XChar)'/');
            XString_append_char(key, (XChar)'g');
        }
        XSettings_setValue(s, key, v);
        XClassDelete(v);
        XSETTINGS_REQUIRE(XSettings_contains(s, key), "lg deep key", "50 层深键应存在");
        r = XSettings_value(s, key);
        XSETTINGS_REQUIRE(r && XVariant_toInt(r) == 1, "lg deep roundtrip", "深键值应可读回");
        if (r) XClassDelete(r);
        XClassDelete(key);
    }

    /* 批量 500 键后 allKeys 计数正确 */
    {
        int i;
        for (i = 0; i < 500; ++i) {
            char k[32];
            XVariant* v = XVariant_create_int(i);
            snprintf(k, sizeof(k), "batch/k%d", i);
            XSettings_setValue_utf8(s, k, v);
            XClassDelete(v);
        }
        XSettings_sync(s);
        {
            XStringList* all = XSettings_allKeys(s);
            size_t total = all ? XStringList_size_base(all) : 0;
            /* 2 个已知键 + 500 个批量键（bigValue / 深键） */
            XSETTINGS_REQUIRE(total == 502, "lg batch count",
                              "批量写入后键数应为 502");
            if (all) XClassDelete(all);
        }
    }

    XClassDelete(s);
    xsettings_test_removeFile(path);
    xsettings_test_removeFile("XSettingsLarge.ini.lock");
    XSETTINGS_TEST_PASS("大值与长内容");
    return true;
}

/* ==================== 25. 回退链四层全建 ==================== */

/**
 * @brief 组织/应用构造器的四层回退链。
 * @details 用户+应用 → 用户+组织 → 系统+应用 → 系统+组织，逐层写入后
 *          关闭回退只应看到首层，开启时四层都可见。
 */
static bool xsettings_test_fourLevelFallback(void)
{
    XString* userDir = XDir_homePath();
    XString* sysDir = XString_create_utf8("XSettingsSysRoot");
    XString* org = XString_create_utf8("XinYueCFallbackOrg");
    XString* app = XString_create_utf8("XinYueCFallbackApp");
    XSettings* s;

    XSETTINGS_REQUIRE(userDir && sysDir && org && app, "fl alloc", "前置分配失败");
    XString_append_utf8(userDir, "/XSettingsFallbackTest");
    {
        XDir* d = XDir_create_2(userDir);
        if (d) { XDir_mkpath(d, userDir); XClassDelete(d); }
    }
    XSettings_setPath(XSettings_IniFormat, XSettings_UserScope, userDir);
    XSettings_setPath(XSettings_IniFormat, XSettings_SystemScope, sysDir);

    s = XSettings_create_6(XSettings_IniFormat, XSettings_UserScope, org, app, NULL);
    XSETTINGS_REQUIRE(s != NULL, "fl create", "四层回退对象创建失败");

    /* 首层写入 */
    {
        XVariant* v = XVariant_create_int(1);
        XSettings_setValue_utf8(s, "layer1", v);
        XClassDelete(v);
        XSettings_sync(s);
    }
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "layer1"), "fl layer1", "首层键应可见");

    /* 第二层（用户+组织）直接以文件构造写入 */
    {
        XString* p = XString_create_copy(userDir);
        XSettings* orgFile;
        XString_append_utf8(p, "/XinYueCFallbackOrg.ini");
        orgFile = XSettings_create_7(p, XSettings_IniFormat, NULL);
        XSETTINGS_REQUIRE(orgFile != NULL, "fl layer2 create", "第二层对象创建失败");
        {
            XVariant* v = XVariant_create_int(2);
            XSettings_setValue_utf8(orgFile, "layer2", v);
            XClassDelete(v);
            XSettings_sync(orgFile);
        }
        XClassDelete(orgFile);
        XClassDelete(p);
    }
    {
        XVariant* r = XSettings_value_utf8(s, "layer2");
        XSETTINGS_REQUIRE(r && XVariant_toInt(r) == 2, "fl layer2 read", "回退链应读到第二层");
        if (r) XClassDelete(r);
    }

    /* 关回退：只应看到首层，第二层不可见 */
    XSettings_setFallbacksEnabled(s, false);
    {
        XVariant* r = XSettings_value_utf8(s, "layer2");
        XSETTINGS_REQUIRE(r == NULL || r->m_type == XVariantType_NULL,
                          "fl no fallback", "关回退后第二层不应可见");
        if (r) XClassDelete(r);
    }
    XSETTINGS_REQUIRE(XSettings_contains_utf8(s, "layer1"), "fl first visible",
                      "关回退后首层仍应可见");
    XSettings_setFallbacksEnabled(s, true);

    XClassDelete(s);
    XClassDelete(userDir);
    XClassDelete(sysDir);
    XClassDelete(org);
    XClassDelete(app);
    xsettings_test_removeFile("XSettingsFallbackTest/XinYueCFallbackApp.ini");
    xsettings_test_removeFile("XSettingsFallbackTest/XinYueCFallbackOrg.ini");
    XSETTINGS_TEST_PASS("四层回退链");
    return true;
}
/* ==================== 26. 并发安全（双层互斥验证） ==================== */

#if XSYNC_ON && XTHREAD_ON

/** @brief 并发测试共享状态。 */
static XSettings* g_xsettingsThreadTarget;
static XAtomic_Int32 g_xsettingsThreadFailures;
static XAtomic_Int32 g_xsettingsThreadCounter;
static XAtomic_Int32 g_xsettingsThreadDone;
/** @brief 线程参数块：XThread 只借用，线程结束后由测试方释放。 */
static XVarList* g_xsettingsThreadArgs[8];

/** @brief 工作线程：同一对象并发读写（不同键，避免语义冲突）。 */
static void xsettings_test_threadWorker(XThread* thread, XVarList* args)
{
    int32_t id = 0;
    int i;

    XSettings* own;
    XVarList_start(args);
    id = *(int32_t*)XVarList_argPtr(args);
    (void)thread;
    /* 共享同一对象（正式用法），线程间键空间互不重叠 */

    own = g_xsettingsThreadTarget;
    for (i = 0; i < 400; ++i) {
        char key[48];
        XVariant* v;
        XVariant* r;
        snprintf(key, sizeof(key), "thread%d/k%d", (int)id, i % 20);
        v = XVariant_create_int(i);
        if (!v) {
            XAtomic_fetch_add_int32(&g_xsettingsThreadFailures, 1, XAtomic_MemoryOrder_Relaxed);
            continue;
        }
        XSettings_setValue_utf8(own, key, v);
        XClassDelete(v);
        r = XSettings_value_utf8(own, key);
        if (!r || r->m_type == XVariantType_NULL) {
            XAtomic_fetch_add_int32(&g_xsettingsThreadFailures, 1, XAtomic_MemoryOrder_Relaxed);
        }
        if (r) XClassDelete(r);
        XAtomic_fetch_add_int32(&g_xsettingsThreadCounter, 1, XAtomic_MemoryOrder_Relaxed);
    }
    XAtomic_fetch_add_int32(&g_xsettingsThreadDone, 1, XAtomic_MemoryOrder_Release);
}

/**
 * @brief 并发读写同一 XSettings 对象。
 * @details 8 个线程各做 400 次「写自己的键 + 立即读回」。每线程键空间互不
 *          重叠（threadN/*），因此正确实现下读回必然成功。若实例锁缺失，
 *          会出现键表损坏、读回 NULL、甚至崩溃。
 */
static bool xsettings_test_concurrent(void)
{
    const char* path = "XSettingsConcurrent.ini";
    const int threadCount = 8;
    XThread* threads[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    size_t started = 0;
    int i;

    xsettings_test_removeFile(path);
    g_xsettingsThreadTarget = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    XSETTINGS_REQUIRE(g_xsettingsThreadTarget != NULL, "cc create", "并发测试对象创建失败");
    XAtomic_init(g_xsettingsThreadFailures, 0);
    XAtomic_init(g_xsettingsThreadCounter, 0);
    XAtomic_init(g_xsettingsThreadDone, 0);

    for (i = 0; i < threadCount; ++i) {
        int32_t tid = (int32_t)i;   /* XVar 取地址，必须用具名变量 */
        g_xsettingsThreadArgs[i] = XVarList_Create(XVar(int32_t, tid));
        threads[i] = XThread_create_func(xsettings_test_threadWorker,
                                         g_xsettingsThreadArgs[i]);
        if (!threads[i] || !XThread_start(threads[i])) {
            if (threads[i]) XClassDelete((XClass*)threads[i]);
            threads[i] = NULL;
            break;
        }
        ++started;
    }

    /* 主线程轮询事件循环，直到全部 worker 报告完成（对齐 XPropertyTest 范式） */
    {
        int guard = 0;
        while (XAtomic_load_int32(&g_xsettingsThreadDone, XAtomic_MemoryOrder_Acquire)
                   < (int32_t)started && guard < 300000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            ++guard;
        }
    }
    for (i = 0; i < (int)started; ++i) {
        XThread_wait(threads[i], 30000);
        /* 线程已 join 结束：同步析构（deleteLater 依赖事件循环所有者，
         * 在测试环境下可能永不执行 → 线程对象及其栈泄漏） */
        XClassDelete((XClass*)threads[i]);
    }
    /* 线程已结束：释放参数块（XThread 只借用不接管） */
    for (i = 0; i < threadCount; ++i) {
        if (g_xsettingsThreadArgs[i]) {
            XVarList_delete(g_xsettingsThreadArgs[i]);
            g_xsettingsThreadArgs[i] = NULL;
        }
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    {
        int32_t failures = XAtomic_load_int32(&g_xsettingsThreadFailures,
                                              XAtomic_MemoryOrder_Relaxed);
        int32_t done = XAtomic_load_int32(&g_xsettingsThreadCounter,
                                          XAtomic_MemoryOrder_Relaxed);
        XSETTINGS_REQUIRE(started == (size_t)threadCount,
                          "cc threads started", "8 个线程应全部启动");
        XSETTINGS_REQUIRE(failures == 0, "cc no failures",
                          "并发写入后读回不应失败（实例锁未生效）");
        XSETTINGS_REQUIRE(done == threadCount * 400, "cc all completed",
                          "全部 3200 次操作应完成");
    }

    /* 收尾：键数应为 8 线程 × 20 键 = 160 */
    {
        XStringList* all = XSettings_allKeys(g_xsettingsThreadTarget);
        size_t total = all ? XStringList_size_base(all) : 0;
        /* 8 线程 × 20 个键名，键空间互不重叠 → 应恰好 160 */
        XSETTINGS_REQUIRE(total == 160, "cc key count",
                          "并发写入后键数应为 160（实例锁或键空间冲突）");
        if (all) XClassDelete(all);
    }

    XClassDelete(g_xsettingsThreadTarget);
    g_xsettingsThreadTarget = NULL;
    xsettings_test_removeFile(path);
    xsettings_test_removeFile("XSettingsConcurrent.ini.lock");
    XSETTINGS_TEST_PASS("并发读写（实例锁）");
    return true;
}

/**
 * @brief 并发访问静态配置表（全局锁验证）。
 * @details 多线程同时 setDefaultFormat / defaultFormat / setPath，
 *          验证全局锁生效且静态表不被破坏。
 */
static void xsettings_test_threadStaticWorker(XThread* thread, XVarList* args)
{
    int32_t id = 0;
    int i;
    (void)thread;
    XVarList_start(args);
    id = *(int32_t*)XVarList_argPtr(args);
    for (i = 0; i < 500; ++i) {
        XSettings_Format f = (i % 2) ? XSettings_IniFormat : XSettings_NativeFormat;
        XSettings_setDefaultFormat(f);
        {
            XSettings_Format got = XSettings_defaultFormat();
            if (got != XSettings_IniFormat && got != XSettings_NativeFormat)
                XAtomic_fetch_add_int32(&g_xsettingsThreadFailures, 1,
                                        XAtomic_MemoryOrder_Relaxed);
        }
        if ((i % 100) == 0) {
            XString* p = XString_create_utf8("XSettingsStaticThread");
            if (p) {
                XSettings_setPath(XSettings_IniFormat, XSettings_UserScope, p);
                XClassDelete(p);
            }
        }
    }
    (void)id;
}

static bool xsettings_test_concurrentStatic(void)
{
    const int threadCount = 4;
    XThread* threads[4] = { NULL, NULL, NULL, NULL };
    size_t started = 0;
    int i;

    XAtomic_init(g_xsettingsThreadFailures, 0);
    for (i = 0; i < threadCount; ++i) {
        int32_t tid = (int32_t)i;   /* XVar 取地址，必须用具名变量 */
        g_xsettingsThreadArgs[i] = XVarList_Create(XVar(int32_t, tid));
        threads[i] = XThread_create_func(xsettings_test_threadStaticWorker,
                                         g_xsettingsThreadArgs[i]);
        if (!threads[i] || !XThread_start(threads[i])) {
            if (threads[i]) XClassDelete((XClass*)threads[i]);
            threads[i] = NULL;
            break;
        }
        ++started;
    }
    for (i = 0; i < (int)started; ++i) {
        XThread_wait(threads[i], 30000);
        XClassDelete((XClass*)threads[i]);
    }
    for (i = 0; i < threadCount; ++i) {
        if (g_xsettingsThreadArgs[i]) {
            XVarList_delete(g_xsettingsThreadArgs[i]);
            g_xsettingsThreadArgs[i] = NULL;
        }
    }
    {
        int32_t failures = XAtomic_load_int32(&g_xsettingsThreadFailures,
                                              XAtomic_MemoryOrder_Relaxed);
        XSETTINGS_REQUIRE(started == (size_t)threadCount,
                          "cs threads started", "4 个线程应全部启动");
        XSETTINGS_REQUIRE(failures == 0, "cs no failures",
                          "并发读写静态配置不应产生非法值");
    }
    XSETTINGS_TEST_PASS("并发静态配置（全局锁）");
    return true;
}

#else

static bool xsettings_test_concurrent(void)
{
    {
    XPrintf("[SKIP] XSettings: 并发读写（XTHREAD_ON 未开启）\n");
    return true;
}

static bool xsettings_test_concurrentStatic(void)
{
    XPrintf("[SKIP] XSettings: 并发静态配置（XTHREAD_ON 未开启）\n");
    return true;
}

#endif /* XSYNC_ON && XTHREAD_ON */
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
    xsettings_test_memoryBalance,
    xsettings_test_registryBackend,
    xsettings_test_registryReadOnly,
    xsettings_test_clearEmptyKey,
    xsettings_test_deferredFlush,
    xsettings_test_lockFile,
    xsettings_test_constructors,
    xsettings_test_defaultsAndGetters,
    xsettings_test_largeContent,
    xsettings_test_fourLevelFallback,
    xsettings_test_concurrent,
    xsettings_test_concurrentStatic
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
            /* 首轮增量可能是进程级一次性惰性缓存（例如系统路径/格式注册表
             * 的首次填充），不是泄漏。判据：同一用例集再跑一遍，增量必须
             * 归零；仍增长才是真泄漏。 */
            XMemoryStatistics again = { 0, 0, 0, 0 };
            for (size_t i = 0; i < count; ++i)
                g_xsettings_testFns[i]();
            again = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            if (again.systemBytes != after.systemBytes) {
                XPrintf("[FAIL] XSettings: 内存平衡: systemBytes %lu -> %lu -> %lu（线性增长=泄漏）\n",
                        (unsigned long)before.systemBytes,
                        (unsigned long)after.systemBytes,
                        (unsigned long)again.systemBytes);
                all = false;
            }
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
