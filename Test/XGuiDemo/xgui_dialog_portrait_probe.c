/**
 * @file       xgui_dialog_portrait_probe.c
 * @brief      对话框族竖屏适配探针（无主窗、纯对话框序列、文件驱动相位）。
 * @details    [2026-10-26 对话框竖屏适配战役] 在竖屏 Xvfb（如 480x800）下
 *             逐个打开 xguidemo 对话框族的代表对话框，供外部 xwd 截图目验：
 *               0 文件打开对话框   1 文件另存为      2 目录选择对话框
 *               3 输入对话框       4 颜色对话框      5 进度对话框
 *               6 消息框           7 退出
 *             协议：探针起定时器轮询控制文件 /tmp/xfdlg/go——驱动每截完一帧
 *             touch 一次 go，探针关闭当前对话框、打开下一个并打
 *             "READY <name>"；驱动等 READY 行再截下一帧。
 *             全部对话框无父（顶层窗口），居屏公式走 XDialog 既有漏斗。
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "XGuiApplication.h"
#include "XScreen.h"
#include "XTimer.h"
#include "XObject.h"
#include "XDialog.h"
#include "XWindow.h"
#include "XMessageBox.h"
#include "XInputDialog.h"
#include "XFileDialog.h"
#include "XColorDialog.h"
#include "XProgressDialog.h"
#include "XString.h"
#include "XPrintf.h"
#include "XWidget.h"

#define PHASE_COUNT 8

static XWidget* g_mainWin = NULL;   /* 主窗（对话框 parent 语境）。 */
static XWidget* probeMain(void) { return g_mainWin; }
static const char* g_phase = "fileopen";
static int g_go = 0;

static void onQuitTick(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    if (g_go) return;
    {
        FILE* f = fopen("/tmp/xfdlg/go", "rb");
        if (f) { fclose(f); g_go = 1; }
    }
    if (!g_go) return;
    printf("QUIT-ON-GO\n"); fflush(stdout);
    XGuiApplication_quit();
}

/* 单相位模式：argv[1]=相位名。go 文件出现即退出（exec 内的
 * quitNow 感知收口阻塞便捷函数；实例 open 相由主循环收口）。 */
int main(int argc, char* argv[])
{
    XWidget* mainWin;
    XTimer* tick;
    if (argc > 1) g_phase = argv[1];
    if (!XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL)) {
        printf("probe: app create failed\n");
        return 1;
    }
    mainWin = (XWidget*)XMemory_malloc(sizeof(XWidget), XCLASS_DEFAULT_MEMORY_TYPE);
    if (mainWin) {
        XWidget_init(mainWin, NULL, (XWidgetFlags)XWindowType_Window);
        g_mainWin = mainWin;
        XWidget_resize(mainWin, (argc > 2) ? atoi(argv[2]) : 480,
                       (argc > 3) ? atoi(argv[3]) : 800);
        XWidget_setWindowTitle(mainWin, XString_create_utf8("竖屏对话框探针"));
        XWidget_show(mainWin);
    }
    tick = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTimer_setInterval(tick, 100);
    XTimer_setTimerCallback(tick, onQuitTick);
    XTimer_start_base(tick);

    if (strcmp(g_phase, "fileopen") == 0) {
        XString* r;
        int fi = 0;
        printf("OPEN fileopen\n"); fflush(stdout);
        r = XFileDialog_getOpenFileName_2(probeMain(), "打开文件", "/tmp",
                                          "文本文件 (*.txt);;所有文件 (*)", &fi);
        printf("DONE fileopen\n"); fflush(stdout);
        if (r) XClassDelete((XClass*)r);
    } else if (strcmp(g_phase, "filesave") == 0) {
        XString* r;
        int fi = 0;
        printf("OPEN filesave\n"); fflush(stdout);
        r = XFileDialog_getSaveFileName_2(probeMain(), "另存为", "/tmp/未命名.txt",
                                          "文本文件 (*.txt);;所有文件 (*)", &fi);
        printf("DONE filesave\n"); fflush(stdout);
        if (r) XClassDelete((XClass*)r);
    } else if (strcmp(g_phase, "dir") == 0) {
        XString* r;
        printf("OPEN dir\n"); fflush(stdout);
        r = XFileDialog_getExistingDirectory_2(probeMain(), "选择文件夹", "/tmp");
        printf("DONE dir\n"); fflush(stdout);
        if (r) XClassDelete((XClass*)r);
    } else if (strcmp(g_phase, "input") == 0) {
        /* 静态便捷路径（实例模式无内容装配）。 */
        XString* r;
        printf("OPEN input\n"); fflush(stdout);
        r = XInputDialog_getText_3(probeMain(), "输入", "请输入名称：",
                                   XInputDialogEchoMode_Normal, "默认文本", NULL, NULL);
        printf("DONE input\n"); fflush(stdout);
        if (r) XClassDelete((XClass*)r);
    } else if (strcmp(g_phase, "color") == 0) {
        XColor c;
        XColor_setNamedColor_2(&c, "#336699");
        printf("OPEN color\n"); fflush(stdout);
        c = XColorDialog_getColor_2(c, probeMain(), "设置颜色", 0);
        printf("DONE color\n"); fflush(stdout);
    } else if (strcmp(g_phase, "progress") == 0) {
        XProgressDialog* pd = XProgressDialog_create(probeMain(), 0);
        if (!pd) return 2;
        XProgressDialog_setRange(pd, 0, 100);
        XProgressDialog_setValue(pd, 40);
        XProgressDialog_setLabelText(pd, XString_create_utf8("正在处理，请稍候……"));
        XDialog_open(&pd->m_base);
        printf("READY progress\n"); fflush(stdout);
        XGuiApplication_exec();
    } else if (strcmp(g_phase, "msgbox") == 0) {
        XMessageBox* box = XMessageBox_create(probeMain(), 0);
        if (!box) return 2;
        XMessageBox_setText(box, "竖屏适配目验：这是一个信息消息框。");
        XMessageBox_setIcon(box, XMessageBoxIcon_Information);
        XMessageBox_setStandardButtons(box, (int)XDialogButtonBoxStandard_Ok |
                                            (int)XDialogButtonBoxStandard_Cancel);
        XDialog_open(&box->m_base);
        printf("READY msgbox\n"); fflush(stdout);
        XGuiApplication_exec();
    } else {
        printf("unknown phase %s\n", g_phase);
        return 3;
    }
    return 0;
}
