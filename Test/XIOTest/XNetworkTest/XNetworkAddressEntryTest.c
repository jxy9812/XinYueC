#include "XNetworkTest.h"
#include "XNetworkAddressEntry.h"
#include "XHostAddress.h"
#include "XString.h"
#include "XMenu.h"
#include "XAction.h"
#include <stdio.h>

// ==================== 基本创建测试 ====================

static void XNetworkAddressEntry_createTest(void)
{
    XPrintf("=== XNetworkAddressEntry 创建测试 ===\n");
    
    /* 空创建 */
    XNetworkAddressEntry* entry1 = XNetworkAddressEntry_create();
    if (entry1) {
        XPrintf("  OK 空创建成功\n");
        XNetworkAddressEntry_delete_base(entry1);
    } else {
        XPrintf("  ERR 空创建失败\n");
    }
    
    /* 使用 IP 创建 */
    XHostAddress ip;
    XHostAddress_init(&ip);
    XHostAddress_setAddress(&ip, "192.168.1.100");
    
    XNetworkAddressEntry* entry2 = XNetworkAddressEntry_createWithIp(&ip);
    if (entry2) {
        XPrintf("  OK 使用IP创建成功\n");
        XString* ipStr = XHostAddress_toString(XNetworkAddressEntry_ip(entry2));
        if (ipStr) {
            XPrintf("    IP: ");
            XPrintf_2(ipStr);
            XPrintf("\n");
            XString_delete_base(ipStr);
        }
        XNetworkAddressEntry_delete_base(entry2);
    }
    XHostAddress_deinit_base(&ip);
    /* 完整创建 */
    XHostAddress netmask;
    XHostAddress_init(&netmask);
    XHostAddress_setAddress(&netmask, "255.255.255.0");
    XHostAddress broadcast;
    XHostAddress_init(&broadcast);
    XHostAddress_setAddress(&broadcast, "192.168.1.255");
    
    XNetworkAddressEntry* entry3 = XNetworkAddressEntry_createFull(&ip, &netmask, &broadcast);
    if (entry3) {
        XPrintf("  OK 完整创建成功\n");
        XNetworkAddressEntry_delete_base(entry3);
    }

    XHostAddress_deinit_base(&netmask);
    XHostAddress_deinit_base(&broadcast);
}

// ==================== 属性设置测试 ====================

static void XNetworkAddressEntry_propertyTest(void)
{
    XPrintf("=== 属性设置测试 ===\n");
    
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) {
        XPrintf("  ERR 创建失败\n");
        return;
    }
    
    /* 设置 IP */
    XHostAddress ip;
    XHostAddress_init(&ip);
    XHostAddress_setAddress(&ip, "10.0.0.1");
    XNetworkAddressEntry_setIp(entry, &ip);
    
    XString* ipStr = XHostAddress_toString(XNetworkAddressEntry_ip(entry));
    if (ipStr) {
        XPrintf("  IP: ");
        XPrintf_2(ipStr);
        XPrintf("\n");
        XString_delete_base(ipStr);
    }
    XHostAddress_deinit_base(&ip);
    
    /* 设置子网掩码 */
    XHostAddress netmask;
    XHostAddress_init(&netmask);
    XHostAddress_setAddress(&netmask, "255.0.0.0");
    XNetworkAddressEntry_setNetmask(entry, &netmask);
    
    XString* maskStr = XHostAddress_toString(XNetworkAddressEntry_netmask(entry));
    if (maskStr) {
        XPrintf("  子网掩码: ");
        XPrintf_2(maskStr);
        XPrintf("\n");
        XString_delete_base(maskStr);
    }
    XHostAddress_deinit_base(&netmask);
    
    /* 设置广播地址 */
    XHostAddress broadcast;
    XHostAddress_init(&broadcast);
    XHostAddress_setAddress(&broadcast, "10.255.255.255");
    XNetworkAddressEntry_setBroadcast(entry, &broadcast);
    
    if (XNetworkAddressEntry_isBroadcastValid(entry)) {
        XString* bcastStr = XHostAddress_toString(XNetworkAddressEntry_broadcast(entry));
        if (bcastStr) {
            XPrintf("  广播地址: ");
            XPrintf_2(bcastStr);
            XPrintf("\n");
            XString_delete_base(bcastStr);
        }
    }
    XHostAddress_deinit_base(&broadcast);
    
    /* 前缀长度 */
    int prefix = XNetworkAddressEntry_prefixLength(entry);
    XPrintf("  前缀长度: %d\n", prefix);
    
    /* 设置前缀长度 */
    XNetworkAddressEntry_setPrefixLength(entry, 24);
    prefix = XNetworkAddressEntry_prefixLength(entry);
    XPrintf("  设置前缀长度为24后: %d\n", prefix);
    
    XNetworkAddressEntry_delete_base(entry);
}

// ==================== 生命周期测试 ====================

static void XNetworkAddressEntry_lifetimeTest(void)
{
    XPrintf("=== 地址生命周期测试 ===\n");
    
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) {
        XPrintf("  ERR 创建失败\n");
        return;
    }
    
    /* 初始状态 */
    XPrintf("  初始生命周期已知: %s\n", 
            XNetworkAddressEntry_isLifetimeKnown(entry) ? "是" : "否");
    
    /* 设置生命周期 */
    XNetworkAddressEntry_setAddressLifetime(entry, 3600000, 7200000); // 1小时, 2小时
    
    XPrintf("  设置后生命周期已知: %s\n", 
            XNetworkAddressEntry_isLifetimeKnown(entry) ? "是" : "否");
    XPrintf("  首选生命周期: %lld ms\n", 
            (long long)XNetworkAddressEntry_preferredLifetime(entry));
    XPrintf("  有效生命周期: %lld ms\n", 
            (long long)XNetworkAddressEntry_validityLifetime(entry));
    
    /* 清除生命周期 */
    XNetworkAddressEntry_clearAddressLifetime(entry);
    XPrintf("  清除后生命周期已知: %s\n", 
            XNetworkAddressEntry_isLifetimeKnown(entry) ? "是" : "否");
    
    XNetworkAddressEntry_delete_base(entry);
}

// ==================== DNS资格测试 ====================

static void XNetworkAddressEntry_dnsTest(void)
{
    XPrintf("=== DNS资格测试 ===\n");
    
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) {
        XPrintf("  ERR 创建失败\n");
        return;
    }
    
    const char* statusStr[] = {
        "未知",
        "不符合资格",
        "符合资格"
    };
    
    /* 初始状态 */
    XNetworkAddressEntry_DnsEligibilityStatus status = XNetworkAddressEntry_dnsEligibility(entry);
    XPrintf("  初始DNS资格: %s\n", statusStr[status + 1]);
    
    /* 设置DNS资格 */
    XNetworkAddressEntry_setDnsEligibility(entry, XNetworkAddressEntry_DnsEligible);
    status = XNetworkAddressEntry_dnsEligibility(entry);
    XPrintf("  设置后DNS资格: %s\n", statusStr[status + 1]);
    
    XNetworkAddressEntry_delete_base(entry);
}

// ==================== 地址类型测试 ====================

static void XNetworkAddressEntry_typeTest(void)
{
    XPrintf("=== 地址类型测试 ===\n");
    
    XNetworkAddressEntry* entry = XNetworkAddressEntry_create();
    if (!entry) {
        XPrintf("  ERR 创建失败\n");
        return;
    }
    
    /* 初始状态 */
    XPrintf("  初始是否永久: %s\n", 
            XNetworkAddressEntry_isPermanent(entry) ? "是" : "否");
    XPrintf("  初始是否临时: %s\n", 
            XNetworkAddressEntry_isTemporary(entry) ? "是" : "否");
    
    /* 设置为永久 */
    XNetworkAddressEntry_setPermanent(entry, true);
    XPrintf("  设置后是否永久: %s\n", 
            XNetworkAddressEntry_isPermanent(entry) ? "是" : "否");
    XPrintf("  设置后是否临时: %s\n", 
            XNetworkAddressEntry_isTemporary(entry) ? "是" : "否");
    
    XNetworkAddressEntry_delete_base(entry);
}

// ==================== 比较测试 ====================

static void XNetworkAddressEntry_compareTest(void)
{
    XPrintf("=== 比较测试 ===\n");
    
    XHostAddress ip1, ip2, netmask;
    XHostAddress_init(&ip1);
    XHostAddress_init(&ip2);
    XHostAddress_init(&netmask);
    XHostAddress_setAddress(&ip1, "192.168.1.1");
    XHostAddress_setAddress(&ip2, "192.168.1.2");
    XHostAddress_setAddress(&netmask, "255.255.255.0");
    
    XNetworkAddressEntry* entry1 = XNetworkAddressEntry_createFull(&ip1, &netmask, NULL);
    XNetworkAddressEntry* entry2 = XNetworkAddressEntry_createFull(&ip1, &netmask, NULL);
    XNetworkAddressEntry* entry3 = XNetworkAddressEntry_createFull(&ip2, &netmask, NULL);
    
    if (entry1 && entry2 && entry3) {
        /* 相等测试 */
        if (XNetworkAddressEntry_equals(entry1, entry2)) {
            XPrintf("  OK entry1 == entry2\n");
        } else {
            XPrintf("  ERR entry1 应该等于 entry2\n");
        }
        
        /* 不相等测试 */
        if (XNetworkAddressEntry_notEquals(entry1, entry3)) {
            XPrintf("  OK entry1 != entry3\n");
        } else {
            XPrintf("  ERR entry1 应该不等于 entry3\n");
        }
    }
    
    if (entry1) XNetworkAddressEntry_delete_base(entry1);
    if (entry2) XNetworkAddressEntry_delete_base(entry2);
    if (entry3) XNetworkAddressEntry_delete_base(entry3);
    
    XHostAddress_deinit_base(&ip1);
    XHostAddress_deinit_base(&ip2);
    XHostAddress_deinit_base(&netmask);
}

// ==================== 复制测试 ====================

static void XNetworkAddressEntry_copyTest(void)
{
    XPrintf("=== 复制测试 ===\n");
    
    XHostAddress ip, netmask;
    XHostAddress_init(&ip);
    XHostAddress_init(&netmask);
    XHostAddress_setAddress(&ip, "172.16.0.1");
    XHostAddress_setAddress(&netmask, "255.255.0.0");
    
    XNetworkAddressEntry* original = XNetworkAddressEntry_createFull(&ip, &netmask, NULL);
    if (!original) {
        XPrintf("  ERR 创建失败\n");
        XHostAddress_deinit_base(&ip);
        XHostAddress_deinit_base(&netmask);
        return;
    }
    
    XNetworkAddressEntry_setAddressLifetime(original, 1000, 2000);
    XNetworkAddressEntry_setDnsEligibility(original, XNetworkAddressEntry_DnsEligible);
    
    /* 复制 */
    XNetworkAddressEntry* copied = XNetworkAddressEntry_create_copy(original);
    if (copied) {
        XPrintf("  OK 复制成功\n");
        
        /* 验证 */
        if (XNetworkAddressEntry_equals(original, copied)) {
            XPrintf("  OK 复制内容相等\n");
        }
        
        XNetworkAddressEntry_delete_base(copied);
    }
    
    XNetworkAddressEntry_delete_base(original);
    XHostAddress_deinit_base(&ip);
    XHostAddress_deinit_base(&netmask);
}

// ==================== 交换测试 ====================

static void XNetworkAddressEntry_swapTest(void)
{
    XPrintf("=== 交换测试 ===\n");
    
    XHostAddress ip1, ip2, netmask;
    XHostAddress_init(&ip1);
    XHostAddress_init(&ip2);
    XHostAddress_init(&netmask);
    XHostAddress_setAddress(&ip1, "10.0.0.1");
    XHostAddress_setAddress(&ip2, "10.0.0.2");
    XHostAddress_setAddress(&netmask, "255.0.0.0");
    
    XNetworkAddressEntry* entry1 = XNetworkAddressEntry_createFull(&ip1, &netmask, NULL);
    XNetworkAddressEntry* entry2 = XNetworkAddressEntry_createFull(&ip2, &netmask, NULL);
    
    if (entry1 && entry2) {
        XString* str1 = XHostAddress_toString(XNetworkAddressEntry_ip(entry1));
        XString* str2 = XHostAddress_toString(XNetworkAddressEntry_ip(entry2));
        
        XPrintf("  交换前: entry1=");
        if (str1) { XPrintf_2(str1); XString_delete_base(str1); }
        XPrintf(", entry2=");
        if (str2) { XPrintf_2(str2); XString_delete_base(str2); }
        XPrintf("\n");
        
        /* 交换 */
        XNetworkAddressEntry_swap(entry1, entry2);
        
        str1 = XHostAddress_toString(XNetworkAddressEntry_ip(entry1));
        str2 = XHostAddress_toString(XNetworkAddressEntry_ip(entry2));
        
        XPrintf("  交换后: entry1=");
        if (str1) { XPrintf_2(str1); XString_delete_base(str1); }
        XPrintf(", entry2=");
        if (str2) { XPrintf_2(str2); XString_delete_base(str2); }
        XPrintf("\n");
    }
    
    if (entry1) XNetworkAddressEntry_delete_base(entry1);
    if (entry2) XNetworkAddressEntry_delete_base(entry2);
    
    XHostAddress_deinit_base(&ip1);
    XHostAddress_deinit_base(&ip2);
    XHostAddress_deinit_base(&netmask);
}

// ==================== 综合测试 ====================

static void XNetworkAddressEntry_comprehensiveTest(void)
{
    XPrintf("\n========== XNetworkAddressEntry 综合测试 ==========\n\n");
    //while (true)
    {
 
    XNetworkAddressEntry_createTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_propertyTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_lifetimeTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_dnsTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_typeTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_compareTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_copyTest();
    XPrintf("\n");
    
    XNetworkAddressEntry_swapTest();
    }
    XPrintf("\n========== 测试完成 ==========\n");
}

// ==================== 菜单注册 ====================

void XMenu_XNetworkAddressEntryTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XNetworkAddressEntry(地址条目)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "综合测试");
        XAction_setAction(action, XNetworkAddressEntry_comprehensiveTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "创建测试");
        XAction_setAction(action, XNetworkAddressEntry_createTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "属性测试");
        XAction_setAction(action, XNetworkAddressEntry_propertyTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "生命周期测试");
        XAction_setAction(action, XNetworkAddressEntry_lifetimeTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "DNS资格测试");
        XAction_setAction(action, XNetworkAddressEntry_dnsTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "地址类型测试");
        XAction_setAction(action, XNetworkAddressEntry_typeTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "比较测试");
        XAction_setAction(action, XNetworkAddressEntry_compareTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "复制测试");
        XAction_setAction(action, XNetworkAddressEntry_copyTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "交换测试");
        XAction_setAction(action, XNetworkAddressEntry_swapTest);
    }
}