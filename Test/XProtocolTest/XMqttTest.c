#include "XProtocolTest.h"
#include "XMqttGlobal.h"
#include "XMqttType.h"
#include "XMqttTopicName.h"
#include "XMqttTopicFilter.h"
#include "XMqttMessage.h"
#include "XMqttPublishProperties.h"
#include "XMqttConnectionProperties.h"
#include "XMqttSubscriptionProperties.h"
#include "XMqttAuthenticationProperties.h"
#include "XMqttClient.h"
#include "XMqttSubscription.h"
#include "XMqttServer.h"
#include "XMemory.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XObject.h"
#include <string.h>

/* ========================================================================
 * XMqttTopicName 单元测试
 * ======================================================================== */
void XMqttTopicNameTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttTopicName 单元测试开始 ==========\n");

    /* ---------- 1. 创建和基本属性 ---------- */
    {
        XMqttTopicName* tn = XMqttTopicName_create("sensor/temperature");
        if (tn) {
            const XString* name = XMqttTopicName_name_const(tn);
            if (name && XString_equals_utf8(name, "sensor/temperature", XChar_CaseSensitive)) {
                XPrintf("  [通过] create + name_const 正确\n");
                pass++;
            } else {
                XPrintf("  [失败] create + name_const 期望 sensor/temperature\n");
                fail++;
            }
            XMqttTopicName_delete_base(tn);
        } else {
            XPrintf("  [失败] create 返回 NULL\n");
            fail++;
        }
    }

    /* ---------- 2. 空主题创建 ---------- */
    {
        XMqttTopicName* tn = XMqttTopicName_create(NULL);
        if (tn) {
            const XString* name = XMqttTopicName_name_const(tn);
            if (name && XString_length_base(name) == 0) {
                XPrintf("  [通过] create(NULL) 返回空字符串值\n");
                pass++;
            } else {
                XPrintf("  [失败] create(NULL) 名称应为空字符串值\n");
                fail++;
            }
            XMqttTopicName_delete_base(tn);
        } else {
            XPrintf("  [失败] create(NULL) 返回 NULL\n");
            fail++;
        }
    }

    /* ---------- 3. setName ---------- */
    {
        XMqttTopicName* tn = XMqttTopicName_create("old");
        XMqttTopicName_setName(tn, "new/topic");
        const XString* name = XMqttTopicName_name_const(tn);
        if (name && XString_equals_utf8(name, "new/topic", XChar_CaseSensitive)) {
            XPrintf("  [通过] setName 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setName 期望 new/topic\n");
            fail++;
        }
        XMqttTopicName_delete_base(tn);
    }

    /* ---------- 4. isValid ---------- */
    {
        XMqttTopicName* valid = XMqttTopicName_create("sensor/temp");
        XMqttTopicName* invalid = XMqttTopicName_create("sensor/+");
        XMqttTopicName* empty = XMqttTopicName_create("");

        if (XMqttTopicName_isValid(valid)) {
            XPrintf("  [通过] isValid 正确检测有效主题\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid 应返回 true\n");
            fail++;
        }

        if (!XMqttTopicName_isValid(invalid)) {
            XPrintf("  [通过] isValid 正确检测含通配符的无效主题\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid 应返回 false（含 +）\n");
            fail++;
        }

        if (!XMqttTopicName_isValid(empty)) {
            XPrintf("  [通过] isValid 正确检测空主题\n");
            pass++;
        } else {
            XPrintf("  [失败] isValid 应返回 false（空字符串）\n");
            fail++;
        }

        XMqttTopicName_delete_base(valid);
        XMqttTopicName_delete_base(invalid);
        XMqttTopicName_delete_base(empty);
    }

    /* ---------- 5. levelCount ---------- */
    {
        XMqttTopicName* tn1 = XMqttTopicName_create("a/b/c/d");
        XMqttTopicName* tn2 = XMqttTopicName_create("single");
        XMqttTopicName* tn3 = XMqttTopicName_create(NULL);

        int c1 = XMqttTopicName_levelCount(tn1);
        int c2 = XMqttTopicName_levelCount(tn2);
        int c3 = XMqttTopicName_levelCount(tn3);

        if (c1 == 4) { XPrintf("  [通过] levelCount 4 层级正确\n"); pass++; }
        else { XPrintf("  [失败] levelCount 期望 4, 实际 %d\n", c1); fail++; }

        if (c2 == 1) { XPrintf("  [通过] levelCount 1 层级正确\n"); pass++; }
        else { XPrintf("  [失败] levelCount 期望 1, 实际 %d\n", c2); fail++; }

        if (c3 == 0) { XPrintf("  [通过] levelCount NULL 返回 0\n"); pass++; }
        else { XPrintf("  [失败] levelCount NULL 期望 0, 实际 %d\n", c3); fail++; }

        XMqttTopicName_delete_base(tn1);
        XMqttTopicName_delete_base(tn2);
        XMqttTopicName_delete_base(tn3);
    }

    /* ---------- 6. levels ---------- */
    {
        XMqttTopicName* tn = XMqttTopicName_create("a/b/c");
        XVector* levels = XMqttTopicName_levels(tn);
        if (levels) {
            size_t count = XVector_size(levels);
            if (count == 3) {
                XString** s0 = (XString**)XVector_at_base(levels, 0);
                XString** s1 = (XString**)XVector_at_base(levels, 1);
                XString** s2 = (XString**)XVector_at_base(levels, 2);
                if (s0 && s1 && s2 &&
                    XString_equals_utf8(*s0, "a", XChar_CaseSensitive) &&
                    XString_equals_utf8(*s1, "b", XChar_CaseSensitive) &&
                    XString_equals_utf8(*s2, "c", XChar_CaseSensitive)) {
                    XPrintf("  [通过] levels 返回正确层级\n");
                    pass++;
                } else {
                    XPrintf("  [失败] levels 内容不正确\n");
                    fail++;
                }
            } else {
                XPrintf("  [失败] levels 数量期望 3, 实际 %zu\n", count);
                fail++;
            }
            XVector_delete_base(levels);
        } else {
            XPrintf("  [失败] levels 返回 NULL\n");
            fail++;
        }
        XMqttTopicName_delete_base(tn);
    }

    /* ---------- 7. equal ---------- */
    {
        XMqttTopicName* a = XMqttTopicName_create("test/topic");
        XMqttTopicName* b = XMqttTopicName_create("test/topic");
        XMqttTopicName* c = XMqttTopicName_create("other");

        if (XMqttTopicName_equal(a, b)) { XPrintf("  [通过] equal 相同主题正确\n"); pass++; }
        else { XPrintf("  [失败] equal 应返回 true\n"); fail++; }

        if (!XMqttTopicName_equal(a, c)) { XPrintf("  [通过] equal 不同主题正确\n"); pass++; }
        else { XPrintf("  [失败] equal 应返回 false\n"); fail++; }

        if (!XMqttTopicName_equal(a, NULL)) { XPrintf("  [通过] equal NULL 参数正确\n"); pass++; }
        else { XPrintf("  [失败] equal NULL 应返回 false\n"); fail++; }

        XMqttTopicName_delete_base(a);
        XMqttTopicName_delete_base(b);
        XMqttTopicName_delete_base(c);
    }

    /* ---------- 8. less ---------- */
    {
        XMqttTopicName* a = XMqttTopicName_create("a");
        XMqttTopicName* b = XMqttTopicName_create("b");

        if (XMqttTopicName_less(a, b)) { XPrintf("  [通过] less 排序正确\n"); pass++; }
        else { XPrintf("  [失败] less 'a' < 'b' 应返回 true\n"); fail++; }

        if (!XMqttTopicName_less(b, a)) { XPrintf("  [通过] less 'b' > 'a' 正确\n"); pass++; }
        else { XPrintf("  [失败] less 'b' < 'a' 应返回 false\n"); fail++; }

        XMqttTopicName_delete_base(a);
        XMqttTopicName_delete_base(b);
    }

    /* ---------- 9. hash ---------- */
    {
        XMqttTopicName* tn = XMqttTopicName_create("hash/test");
        size_t h1 = XMqttTopicName_hash(tn, 0);
        size_t h2 = XMqttTopicName_hash(tn, 0);
        size_t h3 = XMqttTopicName_hash(tn, 42);

        if (h1 == h2 && h1 != 0) { XPrintf("  [通过] hash 一致性正确\n"); pass++; }
        else { XPrintf("  [失败] hash 不一致\n"); fail++; }

        if (h3 != h1) { XPrintf("  [通过] hash 不同 seed 产生不同值\n"); pass++; }
        else { XPrintf("  [失败] hash 不同 seed 应不同\n"); fail++; }

        XMqttTopicName_delete_base(tn);
    }

    /* ---------- 10. name 深拷贝 ---------- */
    {
        XMqttTopicName* tn = XMqttTopicName_create("deep/copy");
        XString* nameCopy = XMqttTopicName_name(tn);
        if (nameCopy && XString_equals_utf8(nameCopy, "deep/copy", XChar_CaseSensitive)) {
            XPrintf("  [通过] name 深拷贝正确\n");
            pass++;
        } else {
            XPrintf("  [失败] name 深拷贝失败\n");
            fail++;
        }
        if (nameCopy) XString_delete_base(nameCopy);
        XMqttTopicName_delete_base(tn);
    }

    /* ---------- 11. create_copy ---------- */
    {
        XMqttTopicName* orig = XMqttTopicName_create("copy/test");
        XMqttTopicName* copy = XMqttTopicName_create_copy(orig);
        if (copy && XMqttTopicName_equal(orig, copy)) {
            XPrintf("  [通过] create_copy 深拷贝正确\n");
            pass++;
        } else {
            XPrintf("  [失败] create_copy 失败\n");
            fail++;
        }
        XMqttTopicName_delete_base(orig);
        XMqttTopicName_delete_base(copy);
    }

    XPrintf("========== XMqttTopicName 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttTopicFilter 单元测试
 * ======================================================================== */
void XMqttTopicFilterTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttTopicFilter 单元测试开始 ==========\n");

    /* ---------- 1. 创建和基本属性 ---------- */
    {
        XMqttTopicFilter* tf = XMqttTopicFilter_create("sensor/+/temperature");
        if (tf) {
            const XString* filter = XMqttTopicFilter_filter_const(tf);
            if (filter && XString_equals_utf8(filter, "sensor/+/temperature", XChar_CaseSensitive)) {
                XPrintf("  [通过] create + filter_const 正确\n");
                pass++;
            } else {
                XPrintf("  [失败] create + filter_const 失败\n");
                fail++;
            }
            XMqttTopicFilter_delete_base(tf);
        } else {
            XPrintf("  [失败] create 返回 NULL\n");
            fail++;
        }
    }

    /* ---------- 2. setFilter ---------- */
    {
        XMqttTopicFilter* tf = XMqttTopicFilter_create("old");
        XMqttTopicFilter_setFilter(tf, "new/filter");
        const XString* f = XMqttTopicFilter_filter_const(tf);
        if (f && XString_equals_utf8(f, "new/filter", XChar_CaseSensitive)) {
            XPrintf("  [通过] setFilter 正确\n");
            pass++;
        } else {
            XPrintf("  [失败] setFilter 失败\n");
            fail++;
        }
        XMqttTopicFilter_delete_base(tf);
    }

    /* ---------- 3. isValid ---------- */
    {
        XMqttTopicFilter* valid1 = XMqttTopicFilter_create("sensor/#");
        XMqttTopicFilter* valid2 = XMqttTopicFilter_create("sensor/+/temp");
        XMqttTopicFilter* invalid1 = XMqttTopicFilter_create("sensor/#/more");
        XMqttTopicFilter* invalid2 = XMqttTopicFilter_create("sensor/+extra");
        XMqttTopicFilter* empty = XMqttTopicFilter_create("");

        if (XMqttTopicFilter_isValid(valid1)) { XPrintf("  [通过] isValid '#' 正确\n"); pass++; }
        else { XPrintf("  [失败] isValid '#' 应返回 true\n"); fail++; }

        if (XMqttTopicFilter_isValid(valid2)) { XPrintf("  [通过] isValid '+' 正确\n"); pass++; }
        else { XPrintf("  [失败] isValid '+' 应返回 true\n"); fail++; }

        if (!XMqttTopicFilter_isValid(invalid1)) { XPrintf("  [通过] isValid '#' 不在末尾检测正确\n"); pass++; }
        else { XPrintf("  [失败] isValid '#' 不在末尾应返回 false\n"); fail++; }

        if (!XMqttTopicFilter_isValid(invalid2)) { XPrintf("  [通过] isValid '+' 格式错误检测正确\n"); pass++; }
        else { XPrintf("  [失败] isValid '+' 格式错误应返回 false\n"); fail++; }

        if (!XMqttTopicFilter_isValid(empty)) { XPrintf("  [通过] isValid 空字符串检测正确\n"); pass++; }
        else { XPrintf("  [失败] isValid 空字符串应返回 false\n"); fail++; }

        XMqttTopicFilter_delete_base(valid1);
        XMqttTopicFilter_delete_base(valid2);
        XMqttTopicFilter_delete_base(invalid1);
        XMqttTopicFilter_delete_base(invalid2);
        XMqttTopicFilter_delete_base(empty);
    }

    /* ---------- 4. match 通配符 + ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("sensor/+/temp");
        XMqttTopicName* name1 = XMqttTopicName_create("sensor/room1/temp");
        XMqttTopicName* name2 = XMqttTopicName_create("sensor/room2/temp");
        XMqttTopicName* name3 = XMqttTopicName_create("sensor/room1/humidity");

        if (XMqttTopicFilter_match(filter, name1, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match '+' 匹配 room1/temp\n"); pass++;
        } else { XPrintf("  [失败] match '+' 应匹配 room1/temp\n"); fail++; }

        if (XMqttTopicFilter_match(filter, name2, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match '+' 匹配 room2/temp\n"); pass++;
        } else { XPrintf("  [失败] match '+' 应匹配 room2/temp\n"); fail++; }

        if (!XMqttTopicFilter_match(filter, name3, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match '+' 不匹配 humidity\n"); pass++;
        } else { XPrintf("  [失败] match '+' 不应匹配 humidity\n"); fail++; }

        XMqttTopicFilter_delete_base(filter);
        XMqttTopicName_delete_base(name1);
        XMqttTopicName_delete_base(name2);
        XMqttTopicName_delete_base(name3);
    }

    /* ---------- 5. match 通配符 # ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("sensor/#");
        XMqttTopicName* name1 = XMqttTopicName_create("sensor/temp");
        XMqttTopicName* name2 = XMqttTopicName_create("sensor/room1/temp");
        XMqttTopicName* name3 = XMqttTopicName_create("other");

        if (XMqttTopicFilter_match(filter, name1, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match '#' 匹配 sensor/temp\n"); pass++;
        } else { XPrintf("  [失败] match '#' 应匹配 sensor/temp\n"); fail++; }

        if (XMqttTopicFilter_match(filter, name2, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match '#' 匹配 sensor/room1/temp\n"); pass++;
        } else { XPrintf("  [失败] match '#' 应匹配 sensor/room1/temp\n"); fail++; }

        if (!XMqttTopicFilter_match(filter, name3, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match '#' 不匹配 other\n"); pass++;
        } else { XPrintf("  [失败] match '#' 不应匹配 other\n"); fail++; }

        XMqttTopicFilter_delete_base(filter);
        XMqttTopicName_delete_base(name1);
        XMqttTopicName_delete_base(name2);
        XMqttTopicName_delete_base(name3);
    }

    /* ---------- 6. match 精确匹配 ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("a/b/c");
        XMqttTopicName* name = XMqttTopicName_create("a/b/c");
        XMqttTopicName* name2 = XMqttTopicName_create("a/b/c/d");

        if (XMqttTopicFilter_match(filter, name, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match 精确匹配正确\n"); pass++;
        } else { XPrintf("  [失败] match 精确匹配应返回 true\n"); fail++; }

        if (!XMqttTopicFilter_match(filter, name2, XMqttTopicFilter_NoMatchOption)) {
            XPrintf("  [通过] match 不匹配子层级正确\n"); pass++;
        } else { XPrintf("  [失败] match 不应匹配子层级\n"); fail++; }

        XMqttTopicFilter_delete_base(filter);
        XMqttTopicName_delete_base(name);
        XMqttTopicName_delete_base(name2);
    }

    /* ---------- 7. match $ 主题不匹配通配符 ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("+/monitor");
        XMqttTopicName* name = XMqttTopicName_create("$SYS/monitor");

        if (!XMqttTopicFilter_match(filter, name, XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption)) {
            XPrintf("  [通过] match $ 主题不匹配通配符正确\n"); pass++;
        } else { XPrintf("  [失败] match $ 主题不应匹配通配符\n"); fail++; }

        XMqttTopicFilter_delete_base(filter);
        XMqttTopicName_delete_base(name);
    }

    /* ---------- 8. sharedSubscriptionName ---------- */
    {
        XMqttTopicFilter* shared = XMqttTopicFilter_create("$share/group1/sensor/temp");
        XMqttTopicFilter* normal = XMqttTopicFilter_create("sensor/temp");

        XString* name = XMqttTopicFilter_sharedSubscriptionName(shared);
        if (name && XString_equals_utf8(name, "group1", XChar_CaseSensitive)) {
            XPrintf("  [通过] sharedSubscriptionName 提取正确\n"); pass++;
        } else { XPrintf("  [失败] sharedSubscriptionName 期望 group1\n"); fail++; }
        if (name) XString_delete_base(name);

        XString* name2 = XMqttTopicFilter_sharedSubscriptionName(normal);
        if (name2 == NULL) { XPrintf("  [通过] sharedSubscriptionName 非共享返回 NULL\n"); pass++; }
        else { XPrintf("  [失败] sharedSubscriptionName 非共享应返回 NULL\n"); fail++; XString_delete_base(name2); }

        XMqttTopicFilter_delete_base(shared);
        XMqttTopicFilter_delete_base(normal);
    }

    /* ---------- 9. equal / less / hash ---------- */
    {
        XMqttTopicFilter* a = XMqttTopicFilter_create("filter/a");
        XMqttTopicFilter* b = XMqttTopicFilter_create("filter/a");
        XMqttTopicFilter* c = XMqttTopicFilter_create("filter/b");

        if (XMqttTopicFilter_equal(a, b)) { XPrintf("  [通过] equal 相同过滤器正确\n"); pass++; }
        else { XPrintf("  [失败] equal 应返回 true\n"); fail++; }

        if (!XMqttTopicFilter_equal(a, c)) { XPrintf("  [通过] equal 不同过滤器正确\n"); pass++; }
        else { XPrintf("  [失败] equal 应返回 false\n"); fail++; }

        if (XMqttTopicFilter_less(a, c)) { XPrintf("  [通过] less 排序正确\n"); pass++; }
        else { XPrintf("  [失败] less 'a' < 'b' 应返回 true\n"); fail++; }

        size_t h = XMqttTopicFilter_hash(a, 0);
        if (h != 0) { XPrintf("  [通过] hash 正确\n"); pass++; }
        else { XPrintf("  [失败] hash 不应为 0\n"); fail++; }

        XMqttTopicFilter_delete_base(a);
        XMqttTopicFilter_delete_base(b);
        XMqttTopicFilter_delete_base(c);
    }

    /* ---------- 10. create_copy ---------- */
    {
        XMqttTopicFilter* orig = XMqttTopicFilter_create("copy/filter");
        XMqttTopicFilter* copy = XMqttTopicFilter_create_copy(orig);
        if (copy && XMqttTopicFilter_equal(orig, copy)) {
            XPrintf("  [通过] create_copy 正确\n"); pass++;
        } else { XPrintf("  [失败] create_copy 失败\n"); fail++; }
        XMqttTopicFilter_delete_base(orig);
        XMqttTopicFilter_delete_base(copy);
    }

    XPrintf("========== XMqttTopicFilter 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttStringPair 单元测试
 * ======================================================================== */
void XMqttStringPairTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttStringPair 单元测试开始 ==========\n");

    /* ---------- 1. 创建和基本属性 ---------- */
    {
        XMqttStringPair* pair = XMqttStringPair_create("key1", "value1");
        if (pair) {
            const XString* name = XMqttStringPair_name_const(pair);
            const XString* value = XMqttStringPair_value_const(pair);
            if (name && value &&
                XString_equals_utf8(name, "key1", XChar_CaseSensitive) &&
                XString_equals_utf8(value, "value1", XChar_CaseSensitive)) {
                XPrintf("  [通过] create + name/value_const 正确\n"); pass++;
            } else { XPrintf("  [失败] create 属性不正确\n"); fail++; }
            XMqttStringPair_delete_base(pair);
        } else { XPrintf("  [失败] create 返回 NULL\n"); fail++; }
    }

    /* ---------- 2. setName / setValue ---------- */
    {
        XMqttStringPair* pair = XMqttStringPair_create("old", "oldv");
        XMqttStringPair_setName(pair, "new");
        XMqttStringPair_setValue(pair, "newv");
        XString* name = XMqttStringPair_name(pair);
        XString* value = XMqttStringPair_value(pair);
        if (name && value &&
            XString_equals_utf8(name, "new", XChar_CaseSensitive) &&
            XString_equals_utf8(value, "newv", XChar_CaseSensitive)) {
            XPrintf("  [通过] setName/setValue 正确\n"); pass++;
        } else { XPrintf("  [失败] setName/setValue 失败\n"); fail++; }
        if (name) XString_delete_base(name);
        if (value) XString_delete_base(value);
        XMqttStringPair_delete_base(pair);
    }

    /* ---------- 3. equal ---------- */
    {
        XMqttStringPair* a = XMqttStringPair_create("k", "v");
        XMqttStringPair* b = XMqttStringPair_create("k", "v");
        XMqttStringPair* c = XMqttStringPair_create("k", "v2");

        if (XMqttStringPair_equal(a, b)) { XPrintf("  [通过] equal 相同键值对正确\n"); pass++; }
        else { XPrintf("  [失败] equal 应返回 true\n"); fail++; }

        if (!XMqttStringPair_equal(a, c)) { XPrintf("  [通过] equal 不同值正确\n"); pass++; }
        else { XPrintf("  [失败] equal 应返回 false\n"); fail++; }

        XMqttStringPair_delete_base(a);
        XMqttStringPair_delete_base(b);
        XMqttStringPair_delete_base(c);
    }

    XPrintf("========== XMqttStringPair 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttUserProperties 单元测试
 * ======================================================================== */
void XMqttUserPropertiesTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttUserProperties 单元测试开始 ==========\n");

    /* ---------- 1. 创建和添加元素（按值存储，容器管理内存） ---------- */
    {
        XMqttUserProperties* props = XMqttUserProperties_create();
        if (props) {
            /* 栈上初始化，按值推入，容器通过回调深拷贝 */
            XMqttStringPair pair1, pair2;
            XMqttStringPair_init(&pair1, "k1", "v1");
            XMqttStringPair_init(&pair2, "k2", "v2");
            XVector_push_back_1_base(props, &pair1);
            XVector_push_back_1_base(props, &pair2);
            /* 容器已拥有深拷贝副本，释放原栈对象 */
            XMqttStringPair_deinit_base(&pair1);
            XMqttStringPair_deinit_base(&pair2);

            size_t sz = XVector_size(props);
            if (sz == 2) {
                XMqttStringPair* got1 = (XMqttStringPair*)XVector_at_base(props, 0);
                XMqttStringPair* got2 = (XMqttStringPair*)XVector_at_base(props, 1);
                if (got1 && got2 &&
                    XString_equals_utf8(&got1->m_name, "k1", XChar_CaseSensitive) &&
                    XString_equals_utf8(&got2->m_name, "k2", XChar_CaseSensitive)) {
                    XPrintf("  [通过] UserProperties 创建和添加正确\n"); pass++;
                } else { XPrintf("  [失败] UserProperties 内容不正确\n"); fail++; }
            } else { XPrintf("  [失败] UserProperties size 期望 2, 实际 %zu\n", sz); fail++; }

            XVector_delete_base(props);
        } else { XPrintf("  [失败] UserProperties_create 返回 NULL\n"); fail++; }
    }

    XPrintf("========== XMqttUserProperties 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttMessage 单元测试
 * ======================================================================== */
void XMqttMessageTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttMessage 单元测试开始 ==========\n");

    /* ---------- 1. 创建空消息 ---------- */
    {
        XMqttMessage* msg = XMqttMessage_create();
        if (msg) {
            if (XMqttMessage_qos(msg) == 0 &&
                XMqttMessage_id(msg) == 0 &&
                !XMqttMessage_duplicate(msg) &&
                !XMqttMessage_retain(msg)) {
                XPrintf("  [通过] create 空消息默认值正确\n"); pass++;
            } else { XPrintf("  [失败] create 空消息默认值不正确\n"); fail++; }
            XMqttMessage_delete_base(msg);
        } else { XPrintf("  [失败] create 返回 NULL\n"); fail++; }
    }

    /* ---------- 2. 创建完整消息 ---------- */
    {
        uint8_t payload[] = "hello mqtt";
        XMqttMessage* msg = XMqttMessage_create_full("test/topic", payload, 10, 42, 1, false, true);
        if (msg) {
            const XMqttTopicName* topic = XMqttMessage_topic_const(msg);
            const XByteArray* pl = XMqttMessage_payload_const(msg);
            bool ok = topic && pl &&
                      XMqttMessage_qos(msg) == 1 &&
                      XMqttMessage_id(msg) == 42 &&
                      !XMqttMessage_duplicate(msg) &&
                      XMqttMessage_retain(msg);
            if (ok) {
                XPrintf("  [通过] create_full 属性正确\n"); pass++;
            } else { XPrintf("  [失败] create_full 属性不正确\n"); fail++; }
            XMqttMessage_delete_base(msg);
        } else { XPrintf("  [失败] create_full 返回 NULL\n"); fail++; }
    }

    /* ---------- 3. 深拷贝 ---------- */
    {
        uint8_t payload[] = "copy test";
        XMqttMessage* orig = XMqttMessage_create_full("copy/topic", payload, 9, 100, 2, true, false);
        XMqttMessage* copy = XMqttMessage_create_copy(orig);
        if (copy && XMqttMessage_equal(orig, copy)) {
            XPrintf("  [通过] create_copy 深拷贝正确\n"); pass++;
        } else { XPrintf("  [失败] create_copy 失败\n"); fail++; }
        XMqttMessage_delete_base(orig);
        XMqttMessage_delete_base(copy);
    }

    /* ---------- 4. payload 深拷贝 ---------- */
    {
        uint8_t payload[] = "payload";
        XMqttMessage* msg = XMqttMessage_create_full("t", payload, 7, 0, 0, false, false);
        XByteArray* pl = XMqttMessage_payload(msg);
        if (pl && XByteArray_size_base(pl) == 7) {
            XPrintf("  [通过] payload 深拷贝正确\n"); pass++;
        } else { XPrintf("  [失败] payload 深拷贝失败\n"); fail++; }
        if (pl) XByteArray_delete_base(pl);
        XMqttMessage_delete_base(msg);
    }

    /* ---------- 5. topic 深拷贝 ---------- */
    {
        XMqttMessage* msg = XMqttMessage_create_full("topic/deep", NULL, 0, 0, 0, false, false);
        XMqttTopicName* topic = XMqttMessage_topic(msg);
        if (topic) {
            const XString* name = XMqttTopicName_name_const(topic);
            if (name && XString_equals_utf8(name, "topic/deep", XChar_CaseSensitive)) {
                XPrintf("  [通过] topic 深拷贝正确\n"); pass++;
            } else { XPrintf("  [失败] topic 深拷贝内容不正确\n"); fail++; }
            XMqttTopicName_delete_base(topic);
        } else { XPrintf("  [失败] topic 深拷贝返回 NULL\n"); fail++; }
        XMqttMessage_delete_base(msg);
    }

    /* ---------- 6. equal 不同消息 ---------- */
    {
        XMqttMessage* a = XMqttMessage_create_full("t", NULL, 0, 1, 0, false, false);
        XMqttMessage* b = XMqttMessage_create_full("t", NULL, 0, 2, 0, false, false);
        if (!XMqttMessage_equal(a, b)) {
            XPrintf("  [通过] equal 不同 id 返回 false\n"); pass++;
        } else { XPrintf("  [失败] equal 不同 id 应返回 false\n"); fail++; }
        XMqttMessage_delete_base(a);
        XMqttMessage_delete_base(b);
    }

    XPrintf("========== XMqttMessage 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttPublishProperties 单元测试
 * ======================================================================== */
void XMqttPublishPropertiesTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttPublishProperties 单元测试开始 ==========\n");

    /* ---------- 1. 创建和默认值 ---------- */
    {
        XMqttPublishProperties* prop = XMqttPublishProperties_create();
        if (prop) {
            if (XMqttPublishProperties_availableProperties(prop) == 0) {
                XPrintf("  [通过] create 默认 availableProperties 为 0\n"); pass++;
            } else { XPrintf("  [失败] create 默认 availableProperties 应为 0\n"); fail++; }
            XMqttPublishProperties_delete_base(prop);
        } else { XPrintf("  [失败] create 返回 NULL\n"); fail++; }
    }

    /* ---------- 2. 设置和获取属性 ---------- */
    {
        XMqttPublishProperties* prop = XMqttPublishProperties_create();

        XMqttPublishProperties_setPayloadFormatIndicator(prop, XMqtt_PayloadFormatIndicator_UTF8Encoded);
        XMqttPublishProperties_setMessageExpiryInterval(prop, 3600);
        XMqttPublishProperties_setTopicAlias(prop, 100);
        XMqttPublishProperties_setResponseTopic(prop, "response/topic");
        XMqttPublishProperties_setContentType(prop, "application/json");

        uint8_t corr[] = {0x01, 0x02, 0x03};
        XMqttPublishProperties_setCorrelationData(prop, corr, 3);

        uint32_t avail = XMqttPublishProperties_availableProperties(prop);
        uint8_t pfi = XMqttPublishProperties_payloadFormatIndicator(prop);
        uint32_t expiry = XMqttPublishProperties_messageExpiryInterval(prop);
        uint16_t alias = XMqttPublishProperties_topicAlias(prop);

        bool ok = (avail & XMqttPublishProperties_PayloadFormatIndicator) &&
                  (avail & XMqttPublishProperties_MessageExpiryInterval) &&
                  (avail & XMqttPublishProperties_TopicAlias) &&
                  (avail & XMqttPublishProperties_ResponseTopic) &&
                  (avail & XMqttPublishProperties_ContentType) &&
                  (avail & XMqttPublishProperties_CorrelationData) &&
                  pfi == XMqtt_PayloadFormatIndicator_UTF8Encoded &&
                  expiry == 3600 && alias == 100;

        if (ok) { XPrintf("  [通过] set/get 多个属性正确\n"); pass++; }
        else { XPrintf("  [失败] set/get 属性不正确\n"); fail++; }

        XMqttPublishProperties_delete_base(prop);
    }

    /* ---------- 3. 深拷贝 ---------- */
    {
        XMqttPublishProperties* orig = XMqttPublishProperties_create();
        XMqttPublishProperties_setPayloadFormatIndicator(orig, XMqtt_PayloadFormatIndicator_UTF8Encoded);
        XMqttPublishProperties* copy = XMqttPublishProperties_create_copy(orig);
        if (copy) {
            if (XMqttPublishProperties_payloadFormatIndicator(copy) == XMqtt_PayloadFormatIndicator_UTF8Encoded) {
                XPrintf("  [通过] create_copy 正确\n"); pass++;
            } else { XPrintf("  [失败] create_copy 属性不一致\n"); fail++; }
            XMqttPublishProperties_delete_base(copy);
        } else { XPrintf("  [失败] create_copy 返回 NULL\n"); fail++; }
        XMqttPublishProperties_delete_base(orig);
    }

    XPrintf("========== XMqttPublishProperties 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttMessageStatusProperties 单元测试
 * ======================================================================== */
void XMqttMessageStatusPropertiesTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttMessageStatusProperties 单元测试开始 ==========\n");

    {
        XMqttMessageStatusProperties* prop = XMqttMessageStatusProperties_create();
        if (prop) {
            if (XMqttMessageStatusProperties_reasonCode(prop) == 0 &&
                XMqttMessageStatusProperties_reason_const(prop) == NULL) {
                XPrintf("  [通过] create 默认值正确\n"); pass++;
            } else { XPrintf("  [失败] create 默认值不正确\n"); fail++; }
            XMqttMessageStatusProperties_delete_base(prop);
        } else { XPrintf("  [失败] create 返回 NULL\n"); fail++; }
    }

    XPrintf("========== XMqttMessageStatusProperties 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttConnectionProperties 单元测试
 * ======================================================================== */
void XMqttConnectionPropertiesTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttConnectionProperties 单元测试开始 ==========\n");

    /* ---------- 1. XMqttLastWillProperties ---------- */
    {
        XMqttLastWillProperties* lw = XMqttLastWillProperties_create();
        if (lw) {
            XMqttLastWillProperties_setWillDelayInterval(lw, 60);
            XMqttLastWillProperties_setPayloadFormatIndicator(lw, XMqtt_PayloadFormatIndicator_UTF8Encoded);
            XMqttLastWillProperties_setMessageExpiryInterval(lw, 86400);
            XMqttLastWillProperties_setContentType(lw, "text/plain");
            XMqttLastWillProperties_setResponseTopic(lw, "will/response");
            uint8_t corr[] = {0xAA, 0xBB};
            XMqttLastWillProperties_setCorrelationData(lw, corr, 2);

            if (XMqttLastWillProperties_willDelayInterval(lw) == 60 &&
                XMqttLastWillProperties_payloadFormatIndicator(lw) == XMqtt_PayloadFormatIndicator_UTF8Encoded &&
                XMqttLastWillProperties_messageExpiryInterval(lw) == 86400) {
                XPrintf("  [通过] LastWillProperties set/get 正确\n"); pass++;
            } else { XPrintf("  [失败] LastWillProperties set/get 不正确\n"); fail++; }

            XMqttLastWillProperties* copy = XMqttLastWillProperties_create_copy(lw);
            if (copy && XMqttLastWillProperties_willDelayInterval(copy) == 60) {
                XPrintf("  [通过] LastWillProperties create_copy 正确\n"); pass++;
                XMqttLastWillProperties_delete_base(copy);
            } else { XPrintf("  [失败] LastWillProperties create_copy 失败\n"); fail++; }

            XMqttLastWillProperties_delete_base(lw);
        } else { XPrintf("  [失败] LastWillProperties_create 返回 NULL\n"); fail++; }
    }

    /* ---------- 2. XMqttConnectionProperties ---------- */
    {
        XMqttConnectionProperties* cp = XMqttConnectionProperties_create();
        if (cp) {
            XMqttConnectionProperties_setSessionExpiryInterval(cp, 3600);
            XMqttConnectionProperties_setMaximumPacketSize(cp, 4096);
            XMqttConnectionProperties_setMaximumReceive(cp, 100);
            XMqttConnectionProperties_setMaximumTopicAlias(cp, 50);
            XMqttConnectionProperties_setRequestProblemInformation(cp, true);
            XMqttConnectionProperties_setRequestResponseInformation(cp, false);

            if (XMqttConnectionProperties_sessionExpiryInterval(cp) == 3600 &&
                XMqttConnectionProperties_maximumPacketSize(cp) == 4096 &&
                XMqttConnectionProperties_maximumReceive(cp) == 100 &&
                XMqttConnectionProperties_maximumTopicAlias(cp) == 50 &&
                XMqttConnectionProperties_requestProblemInformation(cp) &&
                !XMqttConnectionProperties_requestResponseInformation(cp)) {
                XPrintf("  [通过] ConnectionProperties set/get 正确\n"); pass++;
            } else { XPrintf("  [失败] ConnectionProperties set/get 不正确\n"); fail++; }

            XMqttConnectionProperties_delete_base(cp);
        } else { XPrintf("  [失败] ConnectionProperties_create 返回 NULL\n"); fail++; }
    }

    /* ---------- 3. XMqttServerConnectionProperties ---------- */
    {
        XMqttServerConnectionProperties* sp = XMqttServerConnectionProperties_create();
        if (sp) {
            if (!XMqttServerConnectionProperties_isValid(sp)) {
                XPrintf("  [通过] ServerConnectionProperties 默认无效\n"); pass++;
            } else { XPrintf("  [失败] ServerConnectionProperties 默认应无效\n"); fail++; }

            XMqttServerConnectionProperties_delete_base(sp);
        } else { XPrintf("  [失败] ServerConnectionProperties_create 返回 NULL\n"); fail++; }
    }

    XPrintf("========== XMqttConnectionProperties 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttSubscriptionProperties 单元测试
 * ======================================================================== */
void XMqttSubscriptionPropertiesTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttSubscriptionProperties 单元测试开始 ==========\n");

    /* ---------- 1. XMqttSubscriptionProperties ---------- */
    {
        XMqttSubscriptionProperties* sp = XMqttSubscriptionProperties_create();
        if (sp) {
            XMqttSubscriptionProperties_setSubscriptionIdentifier(sp, 12345);
            XMqttSubscriptionProperties_setNoLocal(sp, true);

            if (XMqttSubscriptionProperties_subscriptionIdentifier(sp) == 12345 &&
                XMqttSubscriptionProperties_noLocal(sp)) {
                XPrintf("  [通过] SubscriptionProperties set/get 正确\n"); pass++;
            } else { XPrintf("  [失败] SubscriptionProperties set/get 不正确\n"); fail++; }

            XMqttSubscriptionProperties* copy = XMqttSubscriptionProperties_create_copy(sp);
            if (copy && XMqttSubscriptionProperties_subscriptionIdentifier(copy) == 12345) {
                XPrintf("  [通过] SubscriptionProperties create_copy 正确\n"); pass++;
                XMqttSubscriptionProperties_delete_base(copy);
            } else { XPrintf("  [失败] SubscriptionProperties create_copy 失败\n"); fail++; }

            XMqttSubscriptionProperties_delete_base(sp);
        } else { XPrintf("  [失败] SubscriptionProperties_create 返回 NULL\n"); fail++; }
    }

    /* ---------- 2. XMqttUnsubscriptionProperties ---------- */
    {
        XMqttUnsubscriptionProperties* up = XMqttUnsubscriptionProperties_create();
        if (up) {
            XMqttUnsubscriptionProperties* copy = XMqttUnsubscriptionProperties_create_copy(up);
            if (copy) {
                XPrintf("  [通过] UnsubscriptionProperties create_copy 正确\n"); pass++;
                XMqttUnsubscriptionProperties_delete_base(copy);
            } else { XPrintf("  [失败] UnsubscriptionProperties create_copy 失败\n"); fail++; }
            XMqttUnsubscriptionProperties_delete_base(up);
        } else { XPrintf("  [失败] UnsubscriptionProperties_create 返回 NULL\n"); fail++; }
    }

    XPrintf("========== XMqttSubscriptionProperties 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttAuthenticationProperties 单元测试
 * ======================================================================== */
void XMqttAuthenticationPropertiesTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttAuthenticationProperties 单元测试开始 ==========\n");

    {
        XMqttAuthenticationProperties* ap = XMqttAuthenticationProperties_create();
        if (ap) {
            XMqttAuthenticationProperties_setAuthenticationMethod(ap, "SCRAM-SHA-256");
            uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
            XMqttAuthenticationProperties_setAuthenticationData(ap, data, 4);
            XMqttAuthenticationProperties_setReason(ap, "auth required");

            const XString* method = XMqttAuthenticationProperties_authenticationMethod_const(ap);
            const XByteArray* authData = XMqttAuthenticationProperties_authenticationData_const(ap);
            const XString* reason = XMqttAuthenticationProperties_reason_const(ap);

            if (method && XString_equals_utf8(method, "SCRAM-SHA-256", XChar_CaseSensitive) &&
                authData && XByteArray_size_base(authData) == 4 &&
                reason && XString_equals_utf8(reason, "auth required", XChar_CaseSensitive)) {
                XPrintf("  [通过] AuthenticationProperties set/get 正确\n"); pass++;
            } else { XPrintf("  [失败] AuthenticationProperties set/get 不正确\n"); fail++; }

            XMqttAuthenticationProperties* copy = XMqttAuthenticationProperties_create_copy(ap);
            if (copy) {
                const XString* cm = XMqttAuthenticationProperties_authenticationMethod_const(copy);
                if (cm && XString_equals_utf8(cm, "SCRAM-SHA-256", XChar_CaseSensitive)) {
                    XPrintf("  [通过] AuthenticationProperties create_copy 正确\n"); pass++;
                } else { XPrintf("  [失败] AuthenticationProperties create_copy 内容不一致\n"); fail++; }
                XMqttAuthenticationProperties_delete_base(copy);
            } else { XPrintf("  [失败] AuthenticationProperties create_copy 返回 NULL\n"); fail++; }

            XMqttAuthenticationProperties_delete_base(ap);
        } else { XPrintf("  [失败] AuthenticationProperties_create 返回 NULL\n"); fail++; }
    }

    XPrintf("========== XMqttAuthenticationProperties 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttSubscription 单元测试
 * ======================================================================== */
void XMqttSubscriptionTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttSubscription 单元测试开始 ==========\n");

    /* ---------- 1. 创建和基本属性 ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("test/#");
        XMqttSubscription* sub = XMqttSubscription_create(filter, 2);
        if (sub) {
            if (XMqttSubscription_state(sub) == XMqttSubscription_Unsubscribed &&
                XMqttSubscription_qos(sub) == 2) {
                XPrintf("  [通过] create 默认状态和 QoS 正确\n"); pass++;
            } else { XPrintf("  [失败] create 默认状态/QoS 不正确\n"); fail++; }

            const XMqttTopicFilter* gotFilter = XMqttSubscription_topic_const(sub);
            if (gotFilter && XMqttTopicFilter_equal(gotFilter, filter)) {
                XPrintf("  [通过] topic_const 正确\n"); pass++;
            } else { XPrintf("  [失败] topic_const 不正确\n"); fail++; }

            XMqttSubscription_deleteLater(sub);
        } else { XPrintf("  [失败] create 返回 NULL\n"); fail++; }
        XMqttTopicFilter_delete_base(filter);
    }

    /* ---------- 2. setState 和状态变更信号 ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("s/t");
        XMqttSubscription* sub = XMqttSubscription_create(filter, 0);

        XMqttSubscription_setState(sub, XMqttSubscription_Subscribed);
        if (XMqttSubscription_state(sub) == XMqttSubscription_Subscribed) {
            XPrintf("  [通过] setState 正确\n"); pass++;
        } else { XPrintf("  [失败] setState 不正确\n"); fail++; }

        XMqttSubscription_setQos(sub, 1);
        if (XMqttSubscription_qos(sub) == 1) {
            XPrintf("  [通过] setQos 正确\n"); pass++;
        } else { XPrintf("  [失败] setQos 不正确\n"); fail++; }

        XMqttSubscription_deleteLater(sub);
        XMqttTopicFilter_delete_base(filter);
    }

    /* ---------- 3. unsubscribe_base ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("s/t");
        XMqttSubscription* sub = XMqttSubscription_create(filter, 0);
        XMqttSubscription_setState(sub, XMqttSubscription_Subscribed);
        XMqttSubscription_unsubscribe_base(sub);
        if (XMqttSubscription_state(sub) == XMqttSubscription_Subscribed) {
            XPrintf("  [通过] 无关联客户端时 unsubscribe_base 保持状态\n"); pass++;
        } else { XPrintf("  [失败] 无关联客户端时不应伪造取消成功\n"); fail++; }
        XMqttSubscription_deleteLater(sub);
        XMqttTopicFilter_delete_base(filter);
    }

    /* ---------- 4. topic 深拷贝 ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("deep/copy");
        XMqttSubscription* sub = XMqttSubscription_create(filter, 0);
        XMqttTopicFilter* topicCopy = XMqttSubscription_topic(sub);
        if (topicCopy && XMqttTopicFilter_equal(topicCopy, filter)) {
            XPrintf("  [通过] topic 深拷贝正确\n"); pass++;
        } else { XPrintf("  [失败] topic 深拷贝不正确\n"); fail++; }
        if (topicCopy) XMqttTopicFilter_delete_base(topicCopy);
        XMqttSubscription_deleteLater(sub);
        XMqttTopicFilter_delete_base(filter);
    }

    XPrintf("========== XMqttSubscription 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * XMqttClient 单元测试
 * ======================================================================== */
void XMqttClientTest(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqttClient 单元测试开始 ==========\n");

    /* ---------- 1. 创建和默认值 ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        if (client) {
            if (XMqttClient_state(client) == XMqttClient_Disconnected &&
                XMqttClient_error(client) == XMqttClient_NoError &&
                XMqttClient_port(client) == 0 &&
                XMqttClient_keepAlive(client) == 60 &&
                XMqttClient_protocolVersion(client) == XMqttClient_MQTT_3_1_1 &&
                XMqttClient_cleanSession(client) &&
                XMqttClient_autoKeepAlive(client)) {
                XPrintf("  [通过] create 默认值正确\n"); pass++;
            } else { XPrintf("  [失败] create 默认值不正确\n"); fail++; }
            XMqttClient_deleteLater(client);
        } else { XPrintf("  [失败] create 返回 NULL\n"); fail++; }
    }

    /* ---------- 2. setHostname / hostname ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setHostname(client, "mqtt.eclipseprojects.io");
        const XString* hn = XMqttClient_hostname_const(client);
        if (hn && XString_equals_utf8(hn, "mqtt.eclipseprojects.io", XChar_CaseSensitive)) {
            XPrintf("  [通过] setHostname/hostname_const 正确\n"); pass++;
        } else { XPrintf("  [失败] setHostname 不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 3. setPort / port ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setPort(client, 8883);
        if (XMqttClient_port(client) == 8883) {
            XPrintf("  [通过] setPort 正确\n"); pass++;
        } else { XPrintf("  [失败] setPort 期望 8883\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 4. setClientId / clientId ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setClientId(client, "my-client-001");
        const XString* cid = XMqttClient_clientId_const(client);
        if (cid && XString_equals_utf8(cid, "my-client-001", XChar_CaseSensitive)) {
            XPrintf("  [通过] setClientId 正确\n"); pass++;
        } else { XPrintf("  [失败] setClientId 不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 5. setKeepAlive / keepAlive ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setKeepAlive(client, 120);
        if (XMqttClient_keepAlive(client) == 120) {
            XPrintf("  [通过] setKeepAlive 正确\n"); pass++;
        } else { XPrintf("  [失败] setKeepAlive 期望 120\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 6. setProtocolVersion / protocolVersion ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setProtocolVersion(client, XMqttClient_MQTT_5_0);
        if (XMqttClient_protocolVersion(client) == XMqttClient_MQTT_5_0) {
            XPrintf("  [通过] setProtocolVersion 正确\n"); pass++;
        } else { XPrintf("  [失败] setProtocolVersion 期望 MQTT 5.0\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 7. setUsername / setPassword ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setUsername(client, "admin");
        XMqttClient_setPassword(client, "secret");
        const XString* user = XMqttClient_username_const(client);
        const XString* pass = XMqttClient_password_const(client);
        if (user && pass &&
            XString_equals_utf8(user, "admin", XChar_CaseSensitive) &&
            XString_equals_utf8(pass, "secret", XChar_CaseSensitive)) {
            XPrintf("  [通过] setUsername/setPassword 正确\n"); pass++;
        } else { XPrintf("  [失败] setUsername/setPassword 不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 8. setCleanSession / cleanSession ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setCleanSession(client, false);
        if (!XMqttClient_cleanSession(client)) {
            XPrintf("  [通过] setCleanSession 正确\n"); pass++;
        } else { XPrintf("  [失败] setCleanSession 期望 false\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 9. 遗嘱消息设置 ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setWillTopic(client, "client/lastwill");
        XMqttClient_setWillQoS(client, 1);
        uint8_t willPayload[] = "offline";
        XMqttClient_setWillMessage(client, willPayload, 7);
        XMqttClient_setWillRetain(client, true);

        const XString* wt = XMqttClient_willTopic_const(client);
        if (wt && XString_equals_utf8(wt, "client/lastwill", XChar_CaseSensitive) &&
            XMqttClient_willQoS(client) == 1 &&
            XMqttClient_willRetain(client)) {
            XPrintf("  [通过] 遗嘱消息设置正确\n"); pass++;
        } else { XPrintf("  [失败] 遗嘱消息设置不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 10. setAutoKeepAlive / autoKeepAlive ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_setAutoKeepAlive(client, false);
        if (!XMqttClient_autoKeepAlive(client)) {
            XPrintf("  [通过] setAutoKeepAlive 正确\n"); pass++;
        } else { XPrintf("  [失败] setAutoKeepAlive 期望 false\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 11. subscribe/unsubscribe 虚函数调度 ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttTopicFilter* filter = XMqttTopicFilter_create("test/#");
        // 默认实现返回 NULL（未连接）
        XMqttSubscription* sub = XMqttClient_subscribe(client, filter, 0);
        if (sub == NULL) {
            XPrintf("  [通过] subscribe 默认实现返回 NULL（未连接）\n"); pass++;
        } else { XPrintf("  [失败] subscribe 未连接时应返回 NULL\n"); fail++; }

        XMqttClient_unsubscribe(client, filter); // 不应崩溃
        XPrintf("  [通过] unsubscribe 未连接时安全调用\n"); pass++;

        XMqttTopicFilter_delete_base(filter);
        XMqttClient_deleteLater(client);
    }

    /* ---------- 12. publish 虚函数调度 ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttTopicName* topic = XMqttTopicName_create("test/pub");
        uint8_t payload[] = "hello";
        int32_t id = XMqttClient_publish(client, topic, payload, 5, 0, false);
        if (id == -1) {
            XPrintf("  [通过] publish 默认实现返回 -1（未连接）\n"); pass++;
        } else { XPrintf("  [失败] publish 未连接时应返回 -1\n"); fail++; }
        XMqttTopicName_delete_base(topic);
        XMqttClient_deleteLater(client);
    }

    /* ---------- 13. connectToHost / disconnectFromHost 虚函数调度 ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_connectToHost_base(client);
        if (XMqttClient_state(client) == XMqttClient_Disconnected &&
            XMqttClient_error(client) == XMqttClient_TransportInvalid) {
            XPrintf("  [通过] 空主机连接报告传输无效\n"); pass++;
        } else { XPrintf("  [失败] 空主机连接错误状态不正确\n"); fail++; }

        XMqttClient_disconnectFromHost_base(client);
        if (XMqttClient_state(client) == XMqttClient_Disconnected) {
            XPrintf("  [通过] disconnectFromHost 默认实现设置 Disconnected 状态\n"); pass++;
        } else { XPrintf("  [失败] disconnectFromHost 状态不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 14. setTransport / transport ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XTcpSocket* device = XTcpSocket_create();
        XMqttClient_setTransport(client, device, XMqttClient_AbstractSocket);
        XObject_setParent((XObject*)device, client);
        if (XMqttClient_transport(client) == device) {
            XPrintf("  [通过] setTransport/transport 正确\n"); pass++;
        } else { XPrintf("  [失败] setTransport 不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }

    /* ---------- 15. setConnectionProperties / connectionProperties ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttConnectionProperties* cp = XMqttConnectionProperties_create();
        XMqttConnectionProperties_setSessionExpiryInterval(cp, 7200);
        XMqttClient_setConnectionProperties(client, cp);
        const XMqttConnectionProperties* got = XMqttClient_connectionProperties_const(client);
        if (got && XMqttConnectionProperties_sessionExpiryInterval(got) == 7200) {
            XPrintf("  [通过] setConnectionProperties 正确\n"); pass++;
        } else { XPrintf("  [失败] setConnectionProperties 不正确\n"); fail++; }
        XMqttConnectionProperties_delete_base(cp);
        XMqttClient_deleteLater(client);
    }

    /* ---------- 16. setLastWillProperties / lastWillProperties ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttLastWillProperties* lw = XMqttLastWillProperties_create();
        XMqttLastWillProperties_setWillDelayInterval(lw, 300);
        XMqttClient_setLastWillProperties(client, lw);
        const XMqttLastWillProperties* got = XMqttClient_lastWillProperties_const(client);
        if (got && XMqttLastWillProperties_willDelayInterval(got) == 300) {
            XPrintf("  [通过] setLastWillProperties 正确\n"); pass++;
        } else { XPrintf("  [失败] setLastWillProperties 不正确\n"); fail++; }
        XMqttLastWillProperties_delete_base(lw);
        XMqttClient_deleteLater(client);
    }

    /* ---------- 17. 信号发送（不连接接收器，仅验证不崩溃） ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        XMqttClient_connected_signal(client);
        XMqttClient_disconnected_signal(client);
        XMqttClient_stateChanged_signal(client, XMqttClient_Connected);
        XMqttClient_errorChanged_signal(client, XMqttClient_NoError);
        XMqttClient_messageReceived_signal(client, NULL, NULL);
        XMqttClient_pingResponseReceived_signal(client);
        XPrintf("  [通过] 信号发送均不崩溃\n"); pass++;
        XMqttClient_deleteLater(client);
    }

    /* ---------- 18. connectToHostEncrypted ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        /* 不传 SSL 配置且未设置主机，验证错误状态 */
        XMqttClient_connectToHostEncrypted(client, NULL);
        if (XMqttClient_state(client) == XMqttClient_Disconnected &&
            XMqttClient_error(client) == XMqttClient_TransportInvalid) {
            XPrintf("  [通过] connectToHostEncrypted 空主机错误状态正确\n"); pass++;
        } else { XPrintf("  [失败] connectToHostEncrypted 空主机错误状态不正确\n"); fail++; }
        XMqttClient_deleteLater(client);
    }


    XPrintf("========== XMqttClient 测试完成: %d 通过, %d 失败 ==========\n", pass, fail);
}

/* ========================================================================
 * 服务器相关测试的菜单包装（bool 返回值转菜单动作）
 * ======================================================================== */
static void XMqttServerUnitTestMenu(void)
{
    if (XMqttServerUnitTest_run())
        XPrintf("========== XMqttServer 单元测试菜单执行: 通过 ==========\n");
    else
        XPrintf("========== XMqttServer 单元测试菜单执行: 失败 ==========\n");
}

static void XMqttTcpServerApiUnitTestMenu(void)
{
    if (XMqttTcpServerApiUnitTest_run())
        XPrintf("========== XMqttTcpServer API 单元测试菜单执行: 通过 ==========\n");
    else
        XPrintf("========== XMqttTcpServer API 单元测试菜单执行: 失败 ==========\n");
}

static void XMqttTcpInteropTestMenu(void)
{
    if (XMqttTcpInteropTest_run())
        XPrintf("========== MQTT 双进程联调菜单执行: 通过 ==========\n");
    else
        XPrintf("========== MQTT 双进程联调菜单执行: 失败 ==========\n");
}

/* ========================================================================
 * 位域与联合体数据结构优化测试
 * ========================================================================
 * @return 全部通过返回 true（供自动化回归入口直接调用）。
 * ======================================================================== */
bool XMqttDataLayoutTest_run(void)
{
    int pass = 0, fail = 0;

    XPrintf("========== XMqtt 位域/联合体数据结构测试开始 ==========\n");

    /* ---------- 1. XMqttFixedHeader 固定头联合体布局与往返 ---------- */
    {
        if (sizeof(XMqttFixedHeader) == 1) {
            XPrintf("  [通过] XMqttFixedHeader 联合体占用 1 字节\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttFixedHeader sizeof 期望 1, 实际 %zu\n",
                    sizeof(XMqttFixedHeader)); fail++;
        }
    }
    {
        /* 构造 PUBLISH QoS1 + RETAIN：线上字节应为 0x33 */
        XMqttFixedHeader fh;
        fh.byte = 0;
        fh.bits.type = 3;
        fh.bits.qos = 1;
        fh.bits.retain = 1;
        fh.bits.dup = 0;
        if (fh.byte == 0x33) {
            XPrintf("  [通过] 联合体构造 PUBLISH QoS1+RETAIN 字节正确\n"); pass++;
        } else {
            XPrintf("  [失败] 构造字节期望 0x33, 实际 0x%02X\n", fh.byte); fail++;
        }
        /* 反向解析同一字节 */
        XMqttFixedHeader r = { fh.byte };
        if (r.bits.type == 3 && r.bits.qos == 1 && r.bits.retain == 1 && r.bits.dup == 0) {
            XPrintf("  [通过] 联合体反向解析 PUBLISH 固定头正确\n"); pass++;
        } else {
            XPrintf("  [失败] 联合体反向解析结果不正确\n"); fail++;
        }
    }
    {
        /* 全组合遍历：type 0..15, qos 0..3, dup/retain 0/1 */
        int total = 16 * 4 * 2 * 2;
        int bad = 0;
        for (int type = 0; type < 16 && !bad; type++)
            for (int qos = 0; qos < 4 && !bad; qos++)
                for (int dup = 0; dup < 2 && !bad; dup++)
                    for (int retain = 0; retain < 2 && !bad; retain++) {
                        XMqttFixedHeader w;
                        w.byte = 0;
                        w.bits.type = (uint8_t)type;
                        w.bits.qos = (uint8_t)qos;
                        w.bits.dup = (uint8_t)dup;
                        w.bits.retain = (uint8_t)retain;
                        uint8_t expect = (uint8_t)((type << 4) | (dup << 3) | (qos << 1) | retain);
                        XMqttFixedHeader rt = { w.byte };
                        if (w.byte != expect || rt.bits.type != (uint8_t)type ||
                            rt.bits.qos != (uint8_t)qos || rt.bits.dup != (uint8_t)dup ||
                            rt.bits.retain != (uint8_t)retain) bad++;
                    }
        if (!bad) {
            XPrintf("  [通过] 联合体 %d 种组合字节往返一致\n", total); pass++;
        } else {
            XPrintf("  [失败] 联合体往返存在 %d 处不一致\n", bad); fail++;
        }
    }

    /* ---------- 2. XMqttMessage 位域往返 ---------- */
    {
        int bad = 0;
        for (int qos = 0; qos <= 2; qos++)
            for (int dup = 0; dup < 2; dup++)
                for (int retain = 0; retain < 2; retain++) {
                    XMqttMessage* msg = XMqttMessage_create_full(
                        "layout/topic", (const uint8_t*)"x", 1, 7,
                        (uint8_t)qos, dup != 0, retain != 0);
                    if (!msg || XMqttMessage_qos(msg) != (uint8_t)qos ||
                        XMqttMessage_duplicate(msg) != (dup != 0) ||
                        XMqttMessage_retain(msg) != (retain != 0)) bad++;
                    if (msg) XMqttMessage_delete_base(msg);
                }
        if (!bad) {
            XPrintf("  [通过] XMqttMessage qos/dup/retain 位域往返一致\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttMessage 位域往返存在错误\n"); fail++;
        }
    }

    /* ---------- 3. XMqttClient 位域往返 ---------- */
    {
        XMqttClient* client = XMqttClient_create();
        int bad = 0;
        if (!client) bad++;
        if (client) {
            XMqttClient_setCleanSession(client, false);
            if (XMqttClient_cleanSession(client) != false) bad++;
            XMqttClient_setCleanSession(client, true);
            if (XMqttClient_cleanSession(client) != true) bad++;
            XMqttClient_setWillQoS(client, 2);
            if (XMqttClient_willQoS(client) != 2) bad++;
            XMqttClient_setWillQoS(client, 0);
            if (XMqttClient_willQoS(client) != 0) bad++;
            XMqttClient_setWillRetain(client, true);
            if (XMqttClient_willRetain(client) != true) bad++;
            XMqttClient_setAutoKeepAlive(client, false);
            if (XMqttClient_autoKeepAlive(client) != false) bad++;
        }
        if (!bad) {
            XPrintf("  [通过] XMqttClient 清洁会话/遗嘱/自动保活位域往返一致\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttClient 位域往返存在错误\n"); fail++;
        }
        if (client) XMqttClient_deleteLater(client);
    }

    /* ---------- 4. XMqttSubscription 位域往返 ---------- */
    {
        XMqttTopicFilter* filter = XMqttTopicFilter_create("layout/#");
        XMqttSubscription* sub = XMqttSubscription_create(filter, 1);
        int bad = 0;
        if (!sub) bad++;
        if (sub) {
            XMqttSubscription_setState(sub, XMqttSubscription_Subscribed);
            if (XMqttSubscription_state(sub) != XMqttSubscription_Subscribed) bad++;
            XMqttSubscription_setQos(sub, 2);
            if (XMqttSubscription_qos(sub) != 2) bad++;
            sub->m_reasonCode = 0xFF;
            if (XMqttSubscription_reasonCode(sub) != 0xFF) bad++;
            sub->m_sharedSubscription = 1;
            if (!XMqttSubscription_isSharedSubscription(sub)) bad++;
            sub->m_sharedSubscription = 0;
            if (XMqttSubscription_isSharedSubscription(sub)) bad++;
        }
        if (!bad) {
            XPrintf("  [通过] XMqttSubscription 状态/QoS/原因码/共享标记位域往返一致\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttSubscription 位域往返存在错误\n"); fail++;
        }
        if (sub) XMqttSubscription_deleteLater(sub);
        if (filter) XMqttTopicFilter_delete_base(filter);
    }

    /* ---------- 5. XMqttServer 位域往返 ---------- */
    {
        XMqttServer* server = XMqttServer_create();
        int bad = 0;
        if (!server) bad++;
        if (server) {
            XMqttServer_setMaximumQoS(server, 1);
            if (XMqttServer_maximumQoS(server) != 1) bad++;
            XMqttServer_setMaximumQoS(server, 2);
            if (XMqttServer_maximumQoS(server) != 2) bad++;
            XMqttServer_setRetainAvailable(server, false);
            if (XMqttServer_retainAvailable(server) != false) bad++;
            XMqttServer_setWildcardAvailable(server, false);
            if (XMqttServer_wildcardAvailable(server) != false) bad++;
            XMqttServer_setSubscriptionIdAvailable(server, false);
            if (XMqttServer_subscriptionIdAvailable(server) != false) bad++;
            XMqttServer_setSharedAvailable(server, false);
            if (XMqttServer_sharedAvailable(server) != false) bad++;
        }
        if (!bad) {
            XPrintf("  [通过] XMqttServer 最大 QoS/能力开关位域往返一致\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttServer 位域往返存在错误\n"); fail++;
        }
        if (server) XClass_delete_base((XClass*)server);
    }

    /* ---------- 6. XMqttConnectionProperties 位域往返 ---------- */
    {
        XMqttConnectionProperties* cp = XMqttConnectionProperties_create();
        int bad = 0;
        if (!cp) bad++;
        if (cp) {
            XMqttConnectionProperties_setRequestResponseInformation(cp, true);
            if (XMqttConnectionProperties_requestResponseInformation(cp) != true) bad++;
            XMqttConnectionProperties_setRequestResponseInformation(cp, false);
            if (XMqttConnectionProperties_requestResponseInformation(cp) != false) bad++;
            XMqttConnectionProperties_setRequestProblemInformation(cp, true);
            if (XMqttConnectionProperties_requestProblemInformation(cp) != true) bad++;
            XMqttConnectionProperties_setRequestProblemInformation(cp, false);
            if (XMqttConnectionProperties_requestProblemInformation(cp) != false) bad++;
        }
        if (!bad) {
            XPrintf("  [通过] XMqttConnectionProperties 请求信息位域往返一致\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttConnectionProperties 位域往返存在错误\n"); fail++;
        }
        if (cp) XMqttConnectionProperties_delete_base(cp);
    }

    /* ---------- 7. XMqttServerConnectionProperties 位域往返 ---------- */
    {
        XMqttServerConnectionProperties* sp = XMqttServerConnectionProperties_create();
        int bad = 0;
        if (!sp) bad++;
        if (sp) {
            sp->m_valid = 1;
            if (!XMqttServerConnectionProperties_isValid(sp)) bad++;
            sp->m_maximumQoS = 1;
            if (XMqttServerConnectionProperties_maximumQoS(sp) != 1) bad++;
            sp->m_maximumQoS = 2;
            if (XMqttServerConnectionProperties_maximumQoS(sp) != 2) bad++;
            sp->m_retainAvailable = 1;
            if (!XMqttServerConnectionProperties_retainAvailable(sp)) bad++;
            sp->m_clientIdAssigned = 1;
            if (!XMqttServerConnectionProperties_clientIdAssigned(sp)) bad++;
            sp->m_reasonCode = 0x9B;
            if (XMqttServerConnectionProperties_reasonCode(sp) != 0x9B) bad++;
            sp->m_wildcardSupported = 1;
            if (!XMqttServerConnectionProperties_wildcardSupported(sp)) bad++;
            sp->m_subscriptionIdentifierSupported = 1;
            if (!XMqttServerConnectionProperties_subscriptionIdentifierSupported(sp)) bad++;
            sp->m_sharedSubscriptionSupported = 1;
            if (!XMqttServerConnectionProperties_sharedSubscriptionSupported(sp)) bad++;
        }
        if (!bad) {
            XPrintf("  [通过] XMqttServerConnectionProperties 能力/原因码位域往返一致\n"); pass++;
        } else {
            XPrintf("  [失败] XMqttServerConnectionProperties 位域往返存在错误\n"); fail++;
        }
        if (sp) XMqttServerConnectionProperties_delete_base(sp);
    }

    XPrintf("========== XMqtt 位域/联合体数据结构测试完成: %d 通过, %d 失败 ==========\n",
            pass, fail);
    return fail == 0;
}

/* 菜单包装：bool 返回值转菜单动作 */
static void XMqttDataLayoutTestMenu(void)
{
    if (XMqttDataLayoutTest_run())
        XPrintf("========== XMqtt 位域/联合体数据结构测试菜单执行: 通过 ==========\n");
    else
        XPrintf("========== XMqtt 位域/联合体数据结构测试菜单执行: 失败 ==========\n");
}

/* ========================================================================
 * 菜单注册
 * ======================================================================== */
void XTestMenu_XMqttTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XMqtt(mqtt)");
    {
        XAction* action = XTestMenu_addAction(menu, "TopicName单元测试");
        XTestMenu_setActionFunction(action, XMqttTopicNameTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "TopicFilter单元测试");
        XTestMenu_setActionFunction(action, XMqttTopicFilterTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "StringPair单元测试");
        XTestMenu_setActionFunction(action, XMqttStringPairTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "UserProperties单元测试");
        XTestMenu_setActionFunction(action, XMqttUserPropertiesTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Message单元测试");
        XTestMenu_setActionFunction(action, XMqttMessageTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "PublishProperties单元测试");
        XTestMenu_setActionFunction(action, XMqttPublishPropertiesTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "MessageStatusProperties单元测试");
        XTestMenu_setActionFunction(action, XMqttMessageStatusPropertiesTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "ConnectionProperties单元测试");
        XTestMenu_setActionFunction(action, XMqttConnectionPropertiesTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "SubscriptionProperties单元测试");
        XTestMenu_setActionFunction(action, XMqttSubscriptionPropertiesTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "AuthenticationProperties单元测试");
        XTestMenu_setActionFunction(action, XMqttAuthenticationPropertiesTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Subscription单元测试");
        XTestMenu_setActionFunction(action, XMqttSubscriptionTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Client单元测试");
        XTestMenu_setActionFunction(action, XMqttClientTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "位域/联合体数据结构测试");
        XTestMenu_setActionFunction(action, XMqttDataLayoutTestMenu);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "Qt6.8公开API与协议回归");
        XTestMenu_setActionFunction(action, XMqttPublicApiTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "XMqttServer 协议引擎单元测试");
        XTestMenu_setActionFunction(action, XMqttServerUnitTestMenu);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "XMqttTcpServer API 单元测试");
        XTestMenu_setActionFunction(action, XMqttTcpServerApiUnitTestMenu);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "双进程 TCP 客户端/服务器联调测试");
        XTestMenu_setActionFunction(action, XMqttTcpInteropTestMenu);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "真实 TCP MQTT 服务器（固定端口 18885）");
        XTestMenu_setActionFunction(action, XMqttTcpServerIntegrationTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "真实 TCP MQTT 客户端联调");
        XTestMenu_setActionFunction(action, XMqttTcpClientIntegrationTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "MQTT 生命周期泄漏回归");
        XTestMenu_setActionFunction(action, XMqttMemoryLifecycleTest);
    }
    XTestMenu_addMenu(root, menu);
}
