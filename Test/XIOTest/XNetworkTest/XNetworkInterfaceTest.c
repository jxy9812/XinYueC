#include "XNetworkTest.h"
#include "XNetworkInterface.h"
#include "XHostAddress.h"
#include "XString.h"
#include "XVector.h"
#include "XMenu.h"
#include "XAction.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

// ==================== 获取所有网络接口测试 ====================

static void XNetworkInterface_allInterfacesTest(void)
{
    XPrintf("=== 获取所有网络接口测试 ===\n");
    
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces) {
        XPrintf("  失败: 无法获取网络接口列表\n");
        return;
    }

    XPrintf("  发现 %zu 个网络接口\n\n", XVector_size_base(interfaces));
    
    for_each_iterator(interfaces, XVector, it) {
        XNetworkInterface* iface = XVector_iterator_data(&it);
        if (!iface) continue;
        
        XPrintf("  --- 接口 ---\n");
        
        /* 名称 */
        XString* name = XNetworkInterface_name_const(iface);
        if (name) {
            XPrintf("    名称: ");
            XPrintf_2(name);
            XPrintf("\n");
        }
        
        /* 可读名称 */
        XString* humanName = XNetworkInterface_humanReadableName_const(iface);
        if (humanName) {
            XPrintf("    可读名称: ");
            XPrintf_2(humanName);
            XPrintf("\n");
        }
        
        /* 硬件地址 */
        XString* hwAddr = XNetworkInterface_hardwareAddress_const(iface);
        if (hwAddr) {
            XPrintf("    硬件地址: ");
            XPrintf_2(hwAddr);
            XPrintf("\n");
        }
        
        /* 索引 */
        XPrintf("    索引: %d\n", XNetworkInterface_index(iface));
        
        /* MTU */
        XPrintf("    MTU: %d\n", XNetworkInterface_maximumTransmissionUnit(iface));
        
        /* 类型 */
        const char* typeStr = "未知";
        switch (XNetworkInterface_type(iface)) {
            case XNetworkInterface_Loopback: typeStr = "回环"; break;
            case XNetworkInterface_Ethernet: typeStr = "以太网"; break;
            case XNetworkInterface_Wifi: typeStr = "WiFi"; break;
            case XNetworkInterface_Ppp: typeStr = "点对点"; break;
            case XNetworkInterface_Virtual: typeStr = "虚拟"; break;
            default: break;
        }
        XPrintf("    类型: %s\n", typeStr);
        
        /* 标志 */
        XPrintf("    标志: ");
        if (XNetworkInterface_isUp(iface)) XPrintf("[UP] ");
        if (XNetworkInterface_isRunning(iface)) XPrintf("[RUNNING] ");
        if (XNetworkInterface_isLoopBack(iface)) XPrintf("[LOOPBACK] ");
        if (XNetworkInterface_canBroadcast(iface)) XPrintf("[BROADCAST] ");
        if (XNetworkInterface_canMulticast(iface)) XPrintf("[MULTICAST] ");
        if (XNetworkInterface_isPointToPoint(iface)) XPrintf("[P2P] ");
        XPrintf("\n");
        
        /* 地址条目 */
        XVector* entries = XNetworkInterface_addressEntries(iface);
        if (entries) {
            XPrintf("    地址条目数量: %zu\n", XVector_size_base(entries));
            
            for_each_iterator(entries, XVector, entryIt) {
                XNetworkAddressEntry* entry = XVector_iterator_data(&entryIt);
                if (!entry) continue;
                
                XPrintf("      IP: ");
                XString* ipStr = XHostAddress_toString(&entry->ip);
                if (ipStr) {
                    XPrintf_2(ipStr);
                    XString_delete_base(ipStr);
                }
                
                XPrintf(" / 掩码: ");
                XString* maskStr = XHostAddress_toString(&entry->netmask);
                if (maskStr) {
                    XPrintf_2(maskStr);
                    XString_delete_base(maskStr);
                }
                XPrintf("\n");
            }
        }
        
        XPrintf("\n");
    }
    
    XVector_delete_base(interfaces);
}

// ==================== 获取所有IP地址测试 ====================

static void XNetworkInterface_allAddressesTest(void)
{
    XPrintf("=== 获取所有IP地址测试 ===\n");
    
    XVector* addresses = XNetworkInterface_allAddresses();
    if (!addresses) {
        XPrintf("  失败: 无法获取地址列表\n");
        return;
    }
    
    XPrintf("  发现 %zu 个IP地址\n", XVector_size_base(addresses));
    
    for_each_iterator(addresses, XVector, it) {
        XHostAddress* addr = XVector_iterator_data(&it);
        if (addr) {
            XString* addrStr = XHostAddress_toString(addr);
            if (addrStr) {
                XPrintf("    - ");
                XPrintf_2(addrStr);
                XPrintf("\n");
                XString_delete_base(addrStr);
            }
        }
    }
    
    XVector_delete_base(addresses);
}

// ==================== 按名称查找接口测试 ====================

static void XNetworkInterface_interfaceFromNameTest(void)
{
    XPrintf("=== 按名称查找网络接口测试 ===\n");
    
    /* 先获取一个接口名称 */
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces || XVector_size_base(interfaces) == 0) {
        XPrintf("  没有可用的网络接口\n");
        if (interfaces) XVector_delete_base(interfaces);
        return;
    }
    
    /* 获取第一个接口的名称 */
    XVector_iterator firstIt = XVector_begin(interfaces);
    XNetworkInterface* first = XVector_iterator_data(&firstIt);
    XString* firstName = XNetworkInterface_name(first);
    
    XPrintf("  使用接口名称: ");
    XPrintf_2(firstName);
    XPrintf("\n");
    
    /* 清理列表 */
    XVector_delete_base(interfaces);
    
    /* 按名称查找 */
    XNetworkInterface* found = XNetworkInterface_interfaceFromName(firstName);
    if (found) {
        XPrintf("  成功找到接口\n");
        XPrintf("    索引: %d\n", XNetworkInterface_index(found));
        XPrintf("    MTU: %d\n", XNetworkInterface_maximumTransmissionUnit(found));
        XNetworkInterface_delete_base(found);
    } else {
        XPrintf("  未找到接口\n");
    }
    XString_delete_base(firstName);
}

// ==================== 按索引查找接口测试 ====================

static void XNetworkInterface_interfaceFromIndexTest(void)
{
    XPrintf("=== 按索引查找网络接口测试 ===\n");
    
    /* 先获取一个接口索引 */
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces || XVector_size_base(interfaces) == 0) {
        XPrintf("  没有可用的网络接口\n");
        if (interfaces) XVector_delete_base(interfaces);
        return;
    }
    
    /* 获取第一个接口的索引 */
    XVector_iterator firstIt = XVector_begin(interfaces);
    XNetworkInterface* first = XVector_iterator_data(&firstIt);
    int firstIndex = XNetworkInterface_index(first);
    
    XPrintf("  使用接口索引: %d\n", firstIndex);
    
    /* 清理列表 */
    XVector_delete_base(interfaces);
    
    /* 按索引查找 */
    XNetworkInterface* found = XNetworkInterface_interfaceFromIndex(firstIndex);
    if (found) {
        XPrintf("  成功找到接口\n");
        XString* name = XNetworkInterface_name_const(found);
        if (name) {
            XPrintf("    名称: ");
            XPrintf_2(name);
            XPrintf("\n");
        }
        XNetworkInterface_delete_base(found);
    } else {
        XPrintf("  未找到接口\n");
    }
}

// ==================== 名称与索引转换测试 ====================

static void XNetworkInterface_nameIndexConversionTest(void)
{
    XPrintf("=== 名称与索引转换测试 ===\n");
    
    /* 获取一个有效的接口名称 */
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces || XVector_size_base(interfaces) == 0) {
        XPrintf("  没有可用的网络接口\n");
        if (interfaces) XVector_delete_base(interfaces);
        return;
    }
    
    XVector_iterator firstIt = XVector_begin(interfaces);
    XNetworkInterface* first = XVector_iterator_data(&firstIt);
    XString* name = XNetworkInterface_name(first);
    int index = XNetworkInterface_index(first);
    
    XPrintf("  原始接口: ");
    XPrintf_2(name);
    XPrintf(" (索引: %d)\n", index);
    
    /* 清理列表 */
    XVector_delete_base(interfaces);
    
    /* 名称 -> 索引 */
    int foundIndex = XNetworkInterface_interfaceIndexFromName(name);
    XPrintf("  名称转索引: %d\n", foundIndex);
    
    /* 索引 -> 名称 */
    XString* foundName = XNetworkInterface_interfaceNameFromIndex(index);
    if (foundName) {
        XPrintf("  索引转名称: ");
        XPrintf_2(foundName);
        XPrintf("\n");
        XString_delete_base(foundName);
    }
    
    /* 验证 */
    if (foundIndex == index) {
        XPrintf("  OK 名称->索引转换正确\n");
    } else {
        XPrintf("  ERR 名称->索引转换错误\n");
    }
    XString_delete_base(name);
}

// ==================== 复制测试 ====================

static void XNetworkInterface_copyTest(void)
{
    XPrintf("=== 复制测试 ===\n");
    
    /* 获取一个接口 */
    XVector* interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces || XVector_size_base(interfaces) == 0) {
        XPrintf("  没有可用的网络接口\n");
        if (interfaces) XVector_delete_base(interfaces);
        return;
    }
    
    XVector_iterator origIt = XVector_begin(interfaces);
    XNetworkInterface* original = XVector_iterator_data(&origIt);
    
    /* 测试复制 */
    XNetworkInterface* copied = XNetworkInterface_create_copy(original);
    if (copied) {
        XPrintf("  复制成功\n");
        XPrintf("    原始索引: %d, 复制索引: %d\n", 
                XNetworkInterface_index(original),
                XNetworkInterface_index(copied));
        
        /* 验证名称复制 */
        XString* origName = XNetworkInterface_name_const(original);
        XString* copyName = XNetworkInterface_name_const(copied);
        if (origName && copyName) {
           if(XString_compare(origName , copyName)==XCompare_Equality)
           {
                XPrintf("    OK 名称复制正确\n");
            }
        }
        
        XNetworkInterface_delete_base(copied);
    }
    
    /* 清理 */
    XVector_delete_base(interfaces);
}

// ==================== 综合测试 ====================

static void XNetworkInterface_comprehensiveTest(void)
{
    XPrintf("\n========== XNetworkInterface 综合测试 ==========\n\n");
    //while(true)
    {
        XNetworkInterface_allInterfacesTest();
        XPrintf("\n");

        XNetworkInterface_allAddressesTest();
        XPrintf("\n");

        XNetworkInterface_interfaceFromNameTest();
        XPrintf("\n");

        XNetworkInterface_interfaceFromIndexTest();
        XPrintf("\n");

        XNetworkInterface_nameIndexConversionTest();
        XPrintf("\n");

        XNetworkInterface_copyTest();
    }
    
    XPrintf("\n========== 测试完成 ==========\n");
}

// ==================== 菜单注册 ====================

void XMenu_XNetworkInterfaceTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XNetworkInterface(网络接口)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "综合测试");
        XAction_setAction(action, XNetworkInterface_comprehensiveTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "获取所有网络接口");
        XAction_setAction(action, XNetworkInterface_allInterfacesTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "获取所有IP地址");
        XAction_setAction(action, XNetworkInterface_allAddressesTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "按名称查找接口");
        XAction_setAction(action, XNetworkInterface_interfaceFromNameTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "按索引查找接口");
        XAction_setAction(action, XNetworkInterface_interfaceFromIndexTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "名称与索引转换");
        XAction_setAction(action, XNetworkInterface_nameIndexConversionTest);
    }
    {
        XAction* action = XMenu_addAction(menu, "复制测试");
        XAction_setAction(action, XNetworkInterface_copyTest);
    }
}