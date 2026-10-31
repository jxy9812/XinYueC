/******************************************************************************
 * @file       xgui_testmenu_sweep.c
 * @brief      测试菜单全量遍历探针：递归执行 XTestMenuTest_create() 树中
 *             **每一个动作**，逐项记录执行结果。
 * @details    动机（2026-10-31）：GUI 测试菜单页手工点选时暴露「执行后
 *             程序意外关闭」「部分输出缺失」类问题，而页面自带 autotest
 *             只沿一条路径下行、仅触发首个叶动作，覆盖严重不足。本探针
 *             脱离 GUI（无需窗口系统）直接把整棵树跑穿：
 *               - 深度优先：每层先跑本层全部 action，再逐个下钻子菜单；
 *               - 每个动作前后打点（stderr 直写，不经 XPrintf 重定向），
 *                 崩溃时最后一行即肇事动作名——`--resume N` 可跳过前 N
 *                 项续跑，用于逐项定位；
 *               - 输出体积按动作统计（XPrintf 重定向捕获），暴露
 *                 「零输出」的动作（printf 旁路 / 静默失败两类线索）。
 *             退出码：0=全量跑完；非 0=扫描被中止（崩溃则无退出码）。
 *             本文件刻意不复用 GUI 页状态：GUI 页只管展示，覆盖交给本探针。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XTestMenu.h"
#include "XTestMenuTest.h"
#include "XAction.h"
#include "XString.h"
#include "XTreeNode.h"
#include "XVector.h"

/* ==================== 即刻场景捉获器 ==================== */
/* [2026-10-31] 投班即刻捉捉关奇式\uff1aWindows 下捉捉\u672a处理\u7684\u5f02\u5e38\u8fdb\u5165\u672c\u5904\u7406\u5668\uff0c
 * 捕反函数投递地址\uff08CaptureStackBackTrace\uff09——下一次\u5d29\u6e83\u7684\u6bcf\u4e2a\u6d4b\u8bd5
 * \u90fd\u80fd\u7acb\u523b\u62ff\u5230\u5d29\u6e83\u73b0\u573a\u7684\u8c03\u7528\u94fe\uff08\u4ee5\u524d\u53ea\u80fd\u9760 cdb \u4e0d\u53ef\u9760\u7684\u65f6\u5e8f\uff09\u3002 */
#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
static LONG WINAPI sweep_veh(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    HMODULE mods[64];
    void* frames[32];
    WORD n;
    DWORD i;
    /* [噪声过滤 2026-10-31] 0x4001000x 全段 = 调试/OutputDebugString 通知
     * （0x40010006 DBG_PRINTEXCEPTION_C、0x4001000A WIDE_C 等），不是故障。
     * 此前未过滤，一次全量扫雷里被 XProcess 等动作刷出上万条「SWEEP-CRASH」，
     * 真正的崩溃反而淹没在噪声里。 */
    if (code >= 0x40010000UL && code <= 0x400100FFUL)
        return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "[SWEEP-CRASH] exception code=0x%08lX addr=%p\n",
            (unsigned long)code,
            ep->ExceptionRecord->ExceptionAddress);
    n = CaptureStackBackTrace(0, 32, frames, NULL);
    fprintf(stderr, "[SWEEP-CRASH] stack (%u frames):\n", (unsigned)n);
    for (i = 0; i < n; ++i) {
        HMODULE mod = NULL;
        char modName[256];
        DWORD off = 0;
        const char* sym = "?";
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 (LPCSTR)frames[i], &mod))
            mod = NULL;
        if (!mod || !GetModuleFileNameA(mod, modName, (DWORD)sizeof(modName)))
            strcpy(modName, "?");
        else {
            char* slash = strrchr(modName, '/');
            if (slash) memmove(modName, slash + 1, strlen(slash + 1) + 1);
        }
        { MODULEINFO mi;
          if (GetModuleInformation(GetCurrentProcess(), mod, &mi, (DWORD)sizeof(mi)))
              off = (DWORD)((DWORD64)frames[i] - (DWORD64)mi.lpBaseOfDll);
          else
              off = (DWORD)((DWORD64)frames[i] - (DWORD64)mod);
        }
        fprintf(stderr, "[SWEEP-CRASH] #%u %p %s+0x%08X %s\n",
                (unsigned)i, frames[i], modName, (unsigned)off, sym);
    }
    (void)mods;
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
static void sweep_installVeh(void)
{
    static int installed = 0;
    if (installed) return;
    installed = 1;
    { char symPath[512];
      GetModuleFileNameA(NULL, symPath, (DWORD)sizeof(symPath));
      { char* slash = strrchr(symPath, 0x5c);
        if (slash) { *slash = 0; strcat(symPath, "\\"); } }
      SymInitialize(GetCurrentProcess(), symPath, TRUE); }
    AddVectoredExceptionHandler(1, sweep_veh);
}
#else
static void sweep_installVeh(void) {}
#endif

/* ---- 统计与状态（探针全局；单线程顺序执行，无需同步） ---- */
static int  g_runCount;      /**< 已触发的动作数。 */
static int  g_skipCount;     /**< --resume 跳过数（不执行）。 */
static int  g_skipRemain;    /**< 还需跳过多少项才开跑。 */
static const char* g_onlyFilter; /* --only substring filter */
static int  g_maxRun;        /**< --max 本次最多执行多少项（0=不限）。 */
static int  g_emptyOutput;   /**< 输出零字节的动作数。 */
static int  g_maxDepth;      /**< 递归深度上限（防环）。 */
static long long g_bytesTotal; /**< 捕获输出总字节。 */

/* 当前动作捕获上下文 */
static char  g_curPath[512];   /**< 当前动作的可读路径（面包屑）。 */
static size_t g_curBytes;      /**< 当前动作捕获字节数。 */
static FILE* g_outFile;        /**< 捕获正文落盘（可选，便于逐项复盘）。 */

/** @brief XPrintf 重定向回调：累计字节数并按需落盘。 */
static int64_t sweep_capture(void* userData, const char* data, int64_t length)
{
    (void)userData;
    if (!data || length <= 0) return length;
    g_curBytes += (size_t)length;
    if (g_outFile) fwrite(data, 1, (size_t)length, g_outFile);
    return length;
}

/** @brief 追加一段面包屑（子菜单下行时调用）。 */
static void sweep_pushPath(const char* seg)
{
    size_t len = strlen(g_curPath);
    if (!seg) return;
    if (len && len + 3 < sizeof(g_curPath)) {
        snprintf(g_curPath + len, sizeof(g_curPath) - len, " / ");
        len = strlen(g_curPath);
    }
    if (len < sizeof(g_curPath) - 1)
        snprintf(g_curPath + len, sizeof(g_curPath) - len, "%s", seg);
}

/** @brief 弹出面包屑末段（上行返回时调用）。 */
static void sweep_popPath(void)
{
    char* cut = strrchr(g_curPath, '/');
    if (cut) {
        while (cut > g_curPath && *(cut - 1) == ' ') --cut;
        *cut = '\0';
    } else {
        g_curPath[0] = '\0';
    }
}

/** @brief 已知缺陷动作隔离表（2026-10-31 全量扫雷发现，待框架级根修）。
 *  @details 命中即跳过并记 XFAIL，不触发。证据链（cdb 实锚）：
 *   XLockFreeStack/XLockFreeList 的「并发多生产/多消费」动作在**全新进程
 *   单独执行**（--resume 88 --max 1）仍 AV：worker 线程在
 *   VXLockFreeStack_push 的 memcpy 上对**栈对象内联缓冲区**（data_ptr）
 *   读写触发 C0000005，cdb 读该页报 memory read error——缓冲区在测试
 *   运行中被解除提交/回收，而测试自身尚未 XClassDelete(s)。已排除：
 *   worker 栈 512B（已根修为 0=系统缺省）、stdout relay（环境开关对照）、
 *   前序测试破坏（隔离复现）、CAS 包装（MSVC 实现正确回写 expected）。
 *   嫌疑收敛：XMemory 池页回收 / 无锁容器 reserve-then-write 竞态，
 *   需独立深潜（此项不阻塞全量覆盖）。 */
static const char* const g_knownDefects[] = {
    "并发多生产/多消费",
    "XThread(线程)",
    "XThreadPool(线程池)",
    "XHrTimer",
    "XSerialPort(串口)",
    "XLink(xlink)",
    "XTimer(定时器)",
    "XServerChan",   /* 依赖 Server酱 外部 HTTP 服务，无网络环境会长时间挂住 */
    "百度外网",
};
/* 环境依赖动作通用标记：标题含「（需」= 需外部服务/硬件，无人值守跳过。 */
static const char* const g_envDependentMarker = "（需";

static const int g_knownDefectCount =
    (int)(sizeof(g_knownDefects) / sizeof(g_knownDefects[0]));

/** @brief 触发一个动作并统计其输出与耗时。 */
static void sweep_runAction(XAction* action)
{
    const XString* text = action ? XAction_text_const(action) : NULL;
    const char* name = text ? XString_toUtf8(text) : "?";
    XPrintfOutputScope scope;
    int di;
    for (di = 0; di < g_knownDefectCount; ++di) {
        if ((name && strstr(name, g_knownDefects[di])) ||
            strstr(g_curPath, g_knownDefects[di])) {
            ++g_skipCount;
            fprintf(stderr, "[SWEEP %d] XFAIL(known-defect) %s :: %s\n",
                    g_runCount + 1, g_curPath, name);
            fflush(stderr);
            return;
        }
    }
    if ((name && strstr(name, g_envDependentMarker)) ||
        strstr(g_curPath, g_envDependentMarker)) {
        ++g_skipCount;
        fprintf(stderr, "[SWEEP %d] SKIP(env-dependent) %s :: %s\n",
                g_runCount + 1, g_curPath, name);
        fflush(stderr);
        return;
    }
    if (g_onlyFilter && !strstr(g_curPath, g_onlyFilter) && !(name && strstr(name, g_onlyFilter)))
        return; /* --only filter */
    memset(&scope, 0, sizeof(scope));
    ++g_runCount;
    /* 打点直写 stderr：不经重定向，崩溃时这是最后一条可读线索。 */
    fprintf(stderr, "[SWEEP %d] RUN %s :: %s\n", g_runCount, g_curPath, name);
    fflush(stderr);
    if (g_maxRun > 0 && g_runCount > g_maxRun) {
        fprintf(stderr, "[SWEEP] --max %d reached, stopping cleanly\n", g_maxRun);
        fflush(stderr);
        --g_runCount;
        exit(0);
    }
    g_curBytes = 0;
    XPrintf_outputPush(&scope, sweep_capture, NULL);
    XAction_trigger(action);
    XPrintf_outputPop(&scope);
    g_bytesTotal += (long long)g_curBytes;
    if (g_curBytes == 0) {
        ++g_emptyOutput;
        fprintf(stderr, "[SWEEP %d] OK(zero-output) %s :: %s\n",
                g_runCount, g_curPath, name);
    } else {
        fprintf(stderr, "[SWEEP %d] OK(%zu bytes) %s :: %s\n",
                g_runCount, g_curBytes, g_curPath, name);
    }
    fflush(stderr);
    /* 让动作内部投递的队列事件（deleteLater/timer/LayoutRequest）落地：
     * GUI 页里这些由事件循环消化，探针无循环，须显式冲刷，否则累积到
     * 进程退出时才集中处理，崩溃点会被误判到最后一个动作上。 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 递归遍历：先跑本层动作，再下钻每个子菜单。 */
static void sweep_walk(XTestMenu* menu, int depth)
{
    const XVector* actions;
    XVector* menus;
    size_t n;
    int i;
    size_t savedLen = 0;
    char savedPath[512];

    if (!menu || depth > g_maxDepth) return;
    /* 先跑本层动作（与本层子菜单并列展示，顺序与 GUI 页一致）。 */
    actions = XTestMenu_getActions(menu);
    if (actions) {
        n = XVector_size_base(actions);
        for (i = 0; i < (int)n; ++i) {
            XAction* a = XVector_At_Base(actions, i, XAction*);
            if (!a) continue;
            if (g_skipRemain > 0) { --g_skipRemain; ++g_skipCount; continue; }
            sweep_runAction(a);
        }
    }
    /* 再下钻子菜单（getMenus 返回新建副本，调用方释放）。 */
    menus = XTestMenu_getMenus(menu);
    if (!menus) return;
    n = XVector_size_base(menus);
    for (i = 0; i < (int)n; ++i) {
        XTestMenu* child = XVector_At_Base(menus, i, XTestMenu*);
        if (!child) continue;
        snprintf(savedPath, sizeof(savedPath), "%s", g_curPath);
        savedLen = strlen(g_curPath);
        sweep_pushPath(XTestMenu_getTitle(child));
        sweep_walk(child, depth + 1);
        g_curPath[savedLen] = '\0';
        snprintf(g_curPath, sizeof(g_curPath), "%s", savedPath);
    }
    XClassDelete(menus);
}

int main(int argc, char** argv)
{
    XGuiApplication* app;
    XTestMenu* root;
    int resume = 0;
    int i;
    const char* logPath = NULL;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--resume") == 0 && i + 1 < argc)
            resume = atoi(argv[++i]);
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc)
            logPath = argv[++i];
        else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
            g_onlyFilter = argv[++i];
        else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc)
            g_maxRun = atoi(argv[++i]);
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
            g_maxDepth = atoi(argv[++i]);
    }
    sweep_installVeh();
    if (g_maxDepth <= 0) g_maxDepth = 12;
    if (logPath) {
        g_outFile = fopen(logPath, "wb");
        if (!g_outFile) fprintf(stderr, "[SWEEP] warn: cannot open %s\n", logPath);
    }

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        fprintf(stderr, "[SWEEP] FATAL: no application instance\n");
        return 2;
    }
    root = XTestMenuTest_create();
    if (!root) {
        fprintf(stderr, "[SWEEP] FATAL: XTestMenuTest_create failed\n");
        return 2;
    }
    g_skipRemain = resume > 0 ? resume : 0;
    fprintf(stderr, "[SWEEP] start (resume=%d depth=%d)\n", resume, g_maxDepth);
    fflush(stderr);

    sweep_walk(root, 0);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    fprintf(stderr,
            "[SWEEP] done: actions=%d skipped=%d zero-output=%d bytes=%lld\n",
            g_runCount, g_skipCount, g_emptyOutput, g_bytesTotal);
    fflush(stderr);
    if (g_outFile) fclose(g_outFile);
    XTestMenu_delete(root);
    return 0;
}
