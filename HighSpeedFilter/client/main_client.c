
/**
 * @file       main_client.c
 * @brief      高速过滤器客户端入口：XGuiApplication GUI 事件循环。
 * @details    用法：HsfClient [自动退出秒数] [--shot <png>] [--size WxH]
 *             [--page N] [--goto N] [--no-settings] [--no-login]
 *             [--login user:pass@host:port] [--login-shot]
 *             登录口径：缺省弹登录窗口（用户名/密码 + 服务器地址 + 本机
 *             快连下拉）；--login 无头登录（CI/联调）；--no-login 离线
 *             演示（变频器页回落本地模拟源）。UI 框架见 HsfUi.c。
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
    HsfUiOptions opts;
    XGuiApplication* app;
    int useSettings = 1;
    int i;

    memset(&opts, 0, sizeof(opts));
    opts.startPage = -1;
    opts.thenPage = -1;

    /* 参数：[自动退出秒数] [--shot <png>] [--size <W>x<H>] [--no-settings]
     * --shot：离屏截图模式（窗口开屏外静默出图后退出，不占桌面）。
     * --no-settings：不加载也不保存界面设置——供 CI 截图取确定性基线。
     * --login user:pass@host:port：无头登录（联调/CI）。
     * --no-login：跳过登录（离线演示）。
     * --login-shot：截图对象为登录窗（而非主窗）。 */
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            opts.shotPath = argv[++i];
        } else if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
            opts.startPage = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--goto") == 0 && i + 1 < argc) {
            opts.thenPage = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-settings") == 0) {
            useSettings = 0;
        } else if (strcmp(argv[i], "--no-login") == 0) {
            opts.noLogin = 1;
        } else if (strcmp(argv[i], "--login") == 0 && i + 1 < argc) {
            opts.loginSpec = argv[++i];
        } else if (strcmp(argv[i], "--login-shot") == 0) {
            opts.loginShot = 1;
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            const char* sz = argv[++i];
            int w = 0;
            int h = 0;
            if (sscanf(sz, "%dx%d", &w, &h) == 2) {
                opts.shotWidth = w;
                opts.shotHeight = h;
            }
        } else {
            opts.autoSeconds = atoi(argv[i]);
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

    /* 界面设置持久化：打开配置文件（悬浮窗设置/窗口设置/登录记忆的落盘
     * 与恢复）。打开失败不阻断启动——程序按出厂默认运行。 */
    if (useSettings) {
        HsfSettingsStore_open();
    } else {
        HsfSettingsStore_setEnabled(false);
    }

    /* 登录 → 建窗并进入 GUI 事件循环（阻塞）。 */
    return HsfUi_start(&opts);
}
