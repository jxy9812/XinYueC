/**
 * @file XFileDescriptorTest.c
 * @brief 通用文件描述符表（XFileDescriptor）测试
 *
 * 测试项：
 *   1. 初始化与基本分配/释放
 *   2. 连续分配直到表满
 *   3. 释放后重新分配（验证回收）
 *   4. 类型正确性检查
 *   5. 句柄/上下文存取
 *   6. 压力测试（高频分配释放）
 *   7. XFixedPool 完整性验证
 */

#include "XIOTest.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XFileDescriptor.h"
#include "XFixedPool.h"
#include "XPrintf.h"
#include <string.h>

/* ============================================================================
 * 测试 1：初始化 + 基本分配
 * ============================================================================ */

static void XFdTest_basic(void)
{
    XPrintf_3("\n--- XFileDescriptor 测试 1: 基本分配/释放 ---\n\n");

    XFd_init();  /* 确保初始化 */

    /* 分配三种类型的 fd */
    intptr_t fd1 = XFd_alloc(XFD_TYPE_FILE,   (void*)0x100, (void*)0xA00);
    intptr_t fd2 = XFd_alloc(XFD_TYPE_SOCKET, (void*)0x200, (void*)0xB00);
    intptr_t fd3 = XFd_alloc(XFD_TYPE_TIMER,  (void*)0x300, (void*)0xC00);

    XPrintf("分配 fd1=%lld (FILE),  fdType=%d\n", (long long)fd1, XFd_type(fd1));
    XPrintf("分配 fd2=%lld (SOCKET), fdType=%d\n", (long long)fd2, XFd_type(fd2));
    XPrintf("分配 fd3=%lld (TIMER),  fdType=%d\n", (long long)fd3, XFd_type(fd3));

    /* fd1=0 是合法的（类似 Linux stdin） */
    if (fd1 < 0 || fd2 < 0 || fd3 < 0) {
        XPrintf_3("❌ 分配失败！\n");
        return;
    }

    /* 验证句柄 */
    XPrintf("fd1 handle=0x%p (期望 0x100), ctx=0x%p (期望 0xA00)\n",
            XFd_handle(fd1), XFd_ctx(fd1));

    bool ok = (XFd_handle(fd1) == (void*)0x100)
           && (XFd_ctx(fd1)    == (void*)0xA00)
           && (XFd_type(fd1)   == XFD_TYPE_FILE)
           && (XFd_handle(fd2) == (void*)0x200)
           && (XFd_type(fd2)   == XFD_TYPE_SOCKET)
           && (XFd_handle(fd3) == (void*)0x300)
           && (XFd_type(fd3)   == XFD_TYPE_TIMER);

    /* 释放后 XFd_get 应返回 NULL */
    XFd_free(fd1);
    ok = ok && (XFd_get(fd1) == NULL);

    XFd_free(fd2);
    XFd_free(fd3);

    XPrintf_3(ok ? "✅ 测试 1 通过\n" : "❌ 测试 1 失败\n");
}

/* ============================================================================
 * 测试 2：填满表
 * ============================================================================ */

static void XFdTest_fill(void)
{
    XPrintf_3("\n--- XFileDescriptor 测试 2: 填满 fd 表 ---\n\n");

    XFd_init();

    int count = 0;
    for (int i = 0; i < XFD_TABLE_SIZE; i++) {
        intptr_t fd = XFd_alloc(XFD_TYPE_FILE, (void*)(intptr_t)i, NULL);
        if (fd < 0) {
            XPrintf("  第 %d 次分配失败（表满），符合预期\n", i);
            break;
        }
        count++;
    }

    XPrintf("成功分配 %d / %d 个 fd\n", count, XFD_TABLE_SIZE);

    bool ok = (count == XFD_TABLE_SIZE);

    /* 释放全部 */
    for (int i = 0; i < count; i++) {
        XFd_free(i);
    }

    XPrintf_3(ok ? "✅ 测试 2 通过\n" : "❌ 测试 2 失败（填充数量不符）\n");
}

/* ============================================================================
 * 测试 3：回收重用
 * ============================================================================ */

static void XFdTest_recycle(void)
{
    XPrintf_3("\n--- XFileDescriptor 测试 3: 回收重用 ---\n\n");

    XFd_init();

    /* 先分配 3 个 */
    intptr_t a = XFd_alloc(XFD_TYPE_FILE, (void*)1, NULL);
    intptr_t b = XFd_alloc(XFD_TYPE_FILE, (void*)2, NULL);
    intptr_t c = XFd_alloc(XFD_TYPE_FILE, (void*)3, NULL);

    XPrintf("分配 a=%lld, b=%lld, c=%lld\n", (long long)a, (long long)b, (long long)c);

    /* 释放 b */
    XFd_free(b);
    XPrintf("释放 b=%lld, b 的类型变为 %d\n", (long long)b, XFd_type(b));

    /* 重新分配，应该拿到 b 的位置 */
    intptr_t d = XFd_alloc(XFD_TYPE_SOCKET, (void*)4, NULL);
    XPrintf("重新分配 d=%lld (期望 = %lld)\n", (long long)d, (long long)b);

    bool ok = (d == b) && (XFd_type(d) == XFD_TYPE_SOCKET);

    XFd_free(a);
    XFd_free(c);
    XFd_free(d);

    XPrintf_3(ok ? "✅ 测试 3 通过\n" : "❌ 测试 3 失败\n");
}

/* ============================================================================
 * 测试 4：高频压力
 * ============================================================================ */

static void XFdTest_stress(void)
{
    XPrintf_3("\n--- XFileDescriptor 测试 4: 高频压力（1000 次 alloc/free） ---\n\n");

    XFd_init();

    bool ok = true;
    for (int round = 0; round < 1000; round++) {
        intptr_t f1 = XFd_alloc(XFD_TYPE_FILE,   (void*)(intptr_t)round, NULL);
        intptr_t f2 = XFd_alloc(XFD_TYPE_SOCKET, (void*)(intptr_t)round, NULL);
        intptr_t f3 = XFd_alloc(XFD_TYPE_TIMER,  (void*)(intptr_t)round, NULL);

        if (f1 < 0 || f2 < 0 || f3 < 0) {
            XPrintf("  Round %d 分配失败\n", round);
            ok = false;
            break;
        }

        XFd_free(f1);
        XFd_free(f2);
        XFd_free(f3);
    }

    XPrintf_3(ok ? "✅ 测试 4 通过（1000 轮）\n" : "❌ 测试 4 失败\n");
}

/* ============================================================================
 * 测试 5：类型安全边界检查
 * ============================================================================ */

static void XFdTest_bounds(void)
{
    XPrintf_3("\n--- XFileDescriptor 测试 5: 边界/异常情况 ---\n\n");

    XFd_init();

    bool ok = true;

    /* 无效 fd */
    ok = ok && (XFd_get(-1) == NULL);
    ok = ok && (XFd_get(XFD_TABLE_SIZE) == NULL);
    ok = ok && (XFd_type(-5) == XFD_TYPE_FREE);
    ok = ok && (XFd_handle(99999) == NULL);

    /* 双重释放（不应崩溃） */
    intptr_t fd = XFd_alloc(XFD_TYPE_FILE, NULL, NULL);
    XFd_free(fd);
    XFd_free(fd); /* 第二次释放应安全 */
    ok = ok && (XFd_get(fd) == NULL);

    XPrintf_3(ok ? "✅ 测试 5 通过\n" : "❌ 测试 5 失败\n");
}

/* ============================================================================
 * 主入口：XFileDescriptor 完整测试
 * ============================================================================ */

void XFileDescriptorTest(void)
{
    XPrintf_3("=== XFileDescriptor 完整测试 ===\n");

    XFdTest_basic();
    XFdTest_fill();
    XFdTest_recycle();
    XFdTest_stress();
    XFdTest_bounds();

    XPrintf_3("\n=== XFileDescriptor 所有测试完成 ===\n");
}

/* ============================================================================
 * 注册到菜单
 * ============================================================================ */

void XMenu_XFileDescriptorTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XFileDescriptor(描述符表)");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "完整测试");
        XAction_setAction(action, XFileDescriptorTest);
    }
}