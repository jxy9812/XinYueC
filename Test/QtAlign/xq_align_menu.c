/**
 * @file       xq_align_menu.c
 * @brief      Qt 6.8 对齐测试的 Test 菜单挂载（子进程方式）。
 * @details    菜单项不把 547 个用例跑进 GUI 进程——对齐套件按「每用例前后
 *             堆计数 + 焦点/窗口独立」设计，在已运行的 demo 里原地跑会被
 *             demo 的后台定时器/悬浮层/字体缓存污染（实测大量假 [LEAK]、
 *             焦点断言失败，退出时还会因残留对象崩溃）。因此菜单动作改为
 *             启动同一 bin 目录下的控制台宿主：
 *               `<applicationDirPath>/XinYueC_Static --test qt-align`
 *             等子进程结束后把 stdout/stderr 原样打印到控制台。
 *
 *             与控制台入口 `--test qt-align` 完全同一运行器/同一判据；
 *             demo 进程本身不创建对齐用例对象，退出保持干净。
 * @note       本文件与 xq_align_run.c 均无 main；Test/QtAlign 的用例文件
 *             只许用 XQ_TEST 自注册。
 * @author     XinYueC 团队
 */
#if XGUI_ON

#include "xq_align_menu.h"
#include "xq_align_run.h"

#include "XByteArray.h"
#include "XContainer.h"
#include "XCoreApplication.h"
#include "XProcess.h"
#include "XPrintf.h"
#include "XString.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/** @brief 把子进程输出块原样写到控制台（可含 NUL 的二进制保护）。 */
static void xqAlignDumpBytes(XByteArray* bytes)
{
    uint8_t* data;
    size_t size;
    if (!bytes) return;
    data = XByteArray_data(bytes);
    size = (size_t)XContainer_size_base((const XContainer*)bytes);
    if (data && size > 0) {
        (void)fwrite(data, 1, size, stdout);
        (void)fflush(stdout);
    }
}

/** @brief 菜单动作包装：启动控制台宿主跑全部对齐用例（子进程隔离）。 */
static void xqAlignMenuRun(XVariant* data)
{
    XProcess* proc;
    const XString* dir;
    const char* dirUtf8;
    char program[1024];
    const char* args[2];
    XByteArray* out;
    (void)data;

    dir = XCoreApplication_applicationDirPath();
    dirUtf8 = dir ? XString_toUtf8(dir) : NULL;
    if (!dirUtf8 || !dirUtf8[0]) {
        XPrintf("[qt-align] 取不到应用程序目录，无法启动控制台宿主；"
                "请直接运行 `XinYueC_Static --test qt-align`。\n");
        return;
    }
#ifdef _WIN32
    (void)snprintf(program, sizeof program, "%s/XinYueC_Static.exe", dirUtf8);
#else
    (void)snprintf(program, sizeof program, "%s/XinYueC_Static", dirUtf8);
#endif
    /* applicationDirPath 返回新 XString（调用方负责释放），UTF-8 已拷进 program。 */
    if (dir) XClassDelete((XClass*)dir);
    dir = NULL;

    proc = XProcess_create();
    if (!proc) {
        XPrintf("[qt-align] XProcess 创建失败。\n");
        return;
    }
    args[0] = "--test";
    args[1] = "qt-align";
    XPrintf("[qt-align] 启动子进程：%s --test qt-align（GUI 会等待其结束）\n",
            program);
    if (!XProcess_start_utf8(proc, program, args, 2, XIODevice_ReadOnly)) {
        XPrintf("[qt-align] 无法启动 %s；请直接运行 "
                "`XinYueC_Static --test qt-align`。\n", program);
        XClassDelete((XClass*)proc);
        return;
    }
    (void)XProcess_waitForFinished(proc, -1);
    out = XProcess_readAllStandardOutput(proc);
    xqAlignDumpBytes(out);
    if (out) XClassDelete((XClass*)out);
    out = XProcess_readAllStandardError(proc);
    xqAlignDumpBytes(out);
    if (out) XClassDelete((XClass*)out);
    XPrintf("\n[qt-align] 子进程退出码=%d（0=全部通过）\n",
            XProcess_exitCode(proc));
    XClassDelete((XClass*)proc);
}

void XTestMenu_XQtAlignTest(XTestMenu* root)
{
    XTestMenu* menu;
    XAction* action;
    if (!root) return;
    menu = XTestMenu_create("Qt6.8 对齐(Test/QtAlign)");
    if (!menu) return;
    XTestMenu_addMenu(root, menu);
    action = XTestMenu_addAction(menu, "运行全部对齐用例（子进程）");
    if (action) XTestMenu_setActionFunction(action, xqAlignMenuRun);
}

#endif /* XGUI_ON */
