#include"XIOTest.h"
#include"XHostInfo.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XString.h"
#include"XCoreApplication.h"
#include"XThread.h"
#include"XThreadPool.h"
#include"XEventLoop.h"
//#include"XConsole.h"
#include<stdio.h>

// ==================== 同步DNS查询测试 ====================

static void XHostInfo_syncLookupTest(void)
{
	XPrintf("=== 同步DNS查询测试 ===\n");
	
	// 测试域名
	const char* hosts[] = {
		"www.baidu.com",
		"www.google.com",
		"localhost",
		"invalid.domain.test"
	};
	
	for (int i = 0; i < 4; i++) {
		XPrintf("\n查询: %s\n", hosts[i]);
		
		XHostInfo* info = XHostInfo_fromName2(hosts[i]);
		if (!info) {
			XPrintf("  失败: 无法创建 XHostInfo\n");
			continue;
		}
		
		// 检查错误
		XHostInfo_Error err = XHostInfo_error(info);
		if (err != XHostInfo_NoError) {
			XPrintf("  错误: %d\n", err);
			XString* errStr = XHostInfo_errorString(info);
			if (errStr) {
				XPrintf("  错误信息: ");
				XPrintf_2(errStr);
				XPrintf("\n");
			}
			XHostInfo_delete_base(info);
			continue;
		}
		
		// 打印主机名
		XString* hostName = XHostInfo_hostName(info);
		if (hostName) {
			XPrintf("  主机名: ");
			XPrintf_2(hostName);
			XPrintf("\n");
		}
		
		// 打印所有地址
		const XVector* addresses = XHostInfo_addresses_const(info);
		if (addresses) {
			XPrintf("  地址数量: %zu\n", XVector_size_base(addresses));
			for_each_iterator(addresses, XVector, it) {
				XHostAddress* addr = XVector_iterator_data(&it);
				XString* addrStr = XHostAddress_toString(addr);
				if (addrStr) {
					XPrintf("    - ");
					XPrintf_2(addrStr);
					XPrintf("\n");
					XString_delete_base(addrStr);
				}
			}
		}
		
		XHostInfo_delete_base(info);
	}
}

// ==================== 异步DNS查询测试 ====================

static void onHostLookupComplete(XHostInfo* info, void* userData)
{
	int lookupId = XHostInfo_lookupId(info);
	XPrintf("\n=== 异步查询完成 (ID=%d) ===\n", lookupId);
	
	// 检查错误
	XHostInfo_Error err = XHostInfo_error(info);
	if (err != XHostInfo_NoError) {
		XPrintf("  错误: %d\n", err);
		XHostInfo_delete_base(info);
		return;
	}
	
	// 打印主机名
	XString* hostName = XHostInfo_hostName(info);
	if (hostName) {
		XPrintf("  主机名: ");
		XPrintf_2(hostName);
		XPrintf("\n");
	}
	
	// 打印所有地址
	const XVector* addresses = XHostInfo_addresses_const(info);
	if (addresses) {
		for_each_iterator(addresses, XVector, it) {
			XHostAddress* addr = XVector_iterator_data(&it);
			XString* addrStr = XHostAddress_toString(addr);
			if (addrStr) {
				XPrintf("    - ");
				XPrintf_2(addrStr);
				XPrintf("\n");
				XString_delete_base(addrStr);
			}
		}
	}
	
	XHostInfo_delete_base(info);
}

static void XHostInfo_asyncLookupTest(void)
{
	XPrintf("=== 异步DNS查询测试 ===\n");
	
	XString* host1 = XString_create_utf8("www.baidu.com");
	XString* host2 = XString_create_utf8("www.github.com");
	XString* host3 = XString_create_utf8("www.qq.com");
	
	int id1 = XHostInfo_lookupHost(host1, onHostLookupComplete, NULL);
	int id2 = XHostInfo_lookupHost(host2, onHostLookupComplete, NULL);
	int id3 = XHostInfo_lookupHost(host3, onHostLookupComplete, NULL);
	
	XPrintf("已发起异步查询: ID=%d, ID=%d, ID=%d\n", id1, id2, id3);
	XPrintf("等待查询结果...\n");
	
	XString_delete_base(host1);
	XString_delete_base(host2);
	XString_delete_base(host3);
	
	// 运行事件循环等待结果
	for (int i = 0; i < 10; i++) {
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		XThread_msleep(100);
	}
}

// ==================== 本地主机信息测试 ====================

static void XHostInfo_localInfoTest(void)
{
	XPrintf("=== 本地主机信息测试 ===\n");
	
	// 获取本地主机名
	XString* localHost = XHostInfo_localHostName();
	if (localHost) {
		XPrintf("  本地主机名: ");
		XPrintf_2(localHost);
		XPrintf("\n");
		XString_delete_base(localHost);
	} else {
		XPrintf("  无法获取本地主机名\n");
	}
	
	// 获取本地域名
	XString* localDomain = XHostInfo_localDomainName();
	if (localDomain) {
		XPrintf("  本地域名: ");
		XPrintf_2(localDomain);
		XPrintf("\n");
		XString_delete_base(localDomain);
	} else {
		XPrintf("  无法获取本地域名\n");
	}
}

// ==================== 综合测试 ====================

static void XHostInfo_comprehensiveTest(void)
{
	XPrintf("\n========== XHostInfo 综合测试 ==========\n\n");
	
	// 本地信息
	XHostInfo_localInfoTest();
	XPrintf("\n");
	
	// 同步查询
	XHostInfo_syncLookupTest();
	XPrintf("\n");
	
	// 异步查询
	XHostInfo_asyncLookupTest();
	
	XPrintf("\n========== 测试完成 ==========\n");
}

// ==================== 菜单注册 ====================

void XMenu_XHostInfoTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XHostInfo(DNS查询)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "综合测试");
		XAction_setAction(action, XHostInfo_comprehensiveTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "本地主机信息");
		XAction_setAction(action, XHostInfo_localInfoTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "同步DNS查询");
		XAction_setAction(action, XHostInfo_syncLookupTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "异步DNS查询");
		XAction_setAction(action, XHostInfo_asyncLookupTest);
	}
}