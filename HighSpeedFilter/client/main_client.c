/**
 * @file       main_client.c
 * @brief      高速过滤器客户端入口：XGuiApplication GUI 事件循环。
 * @details    用法：HsfClient [自动退出秒数]（缺省 0=常驻，等待窗口关闭；
 *             >0 供 CI/截图验收自动退出）。UI 框架见 HsfUi.c：
 *             标题栏（菜单图标+标题）/ 堆栈布局内容区 / 黑色状态栏，
 *             根容器垂直布局。缺省窗口取**横屏基准 1280x720**（宽屏形态：
 *             左侧导航常驻 + 内容区横排），屏小则等比收敛到屏内——
 *             窗口尺寸的单一事实源在 HsfUi.c（hsf_defaultWinSize）。
 */
#include "XGuiApplication.h"
#include "HsfUi.h"
#include "HsfTheme.h"
#include "HsfSettingsStore.h"
#include "XPrintf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    XGuiApplication* app;
    int autoSeconds = 0;
    const char* shotPath = NULL;
    int shotW = 0;
    int shotH = 0;
    int startPage = -1;
    int thenPage = -1;
    int useSettings = 1;
    int i;

    /* 参数：[自动退出秒数] [--shot <png>] [--size <W>x<H>] [--no-settings]
     * --shot：离屏截图模式（窗口开屏外静默出图后退出，不占桌面）。
     * --no-settings：不加载也不保存界面设置（悬浮窗设置/窗口设置）——
     *   供 CI 截图取确定性基线，避免本机交互设置污染出图。 */
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shotPath = argv[++i];
        } else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
            startPage = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--goto") == 0 && i + 1 < argc) {
            thenPage = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-settings") == 0) {
            useSettings = 0;
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            const char* sz = argv[++i];
            int w = 0;
            int h = 0;
            if (sscanf(sz, "%dx%d", &w, &h) == 2) {
                shotW = w;
                shotH = h;
            }
        } else {
            autoSeconds = atoi(argv[i]);
        }
    }

    /* stdout 无缓冲：崩溃时已打印的诊断行不丢（定位现场用）。 */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* 缺省主题：Fusion 样式 + 主题样式表（须在控件创建前安装）。 */
    HsfTheme_install();

    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, argc, argv);
    if (!app) {
        XPrintf("HsfClient: GUI 事件循环创建失败\n");
        return 1;
    }

    /* 界面设置持久化：打开配置文件（悬浮窗设置/窗口设置的落盘与恢复）。
     * 打开失败不阻断启动——程序按出厂默认运行。 */
    if (useSettings) {
        HsfSettingsStore_open();
    } else {
        HsfSettingsStore_setEnabled(false);
    }

    /* 建窗并进入 GUI 事件循环（阻塞）。 */
    return HsfUi_start(autoSeconds, shotPath, shotW, shotH, startPage,
                       thenPage);
}
