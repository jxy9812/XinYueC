/**
 * @file       HsfTheme.c
 * @brief      客户端 UI 共享主题实现（缺省字体装配）。
 */
#include "HsfTheme.h"
#include "HsfThemeCss.h"

#include "XFusionStyle.h" /* Fusion 缺省样式安装 */
#include "XStyle.h"       /* 全局样式表装载 */
#include "XFont.h"
#include "XCoreApplication.h" /* applicationDirPath（exe 同级字库定位） */
#include "XString.h"
#include <string.h>
#include <stdio.h>

/** @brief 套缺省字体：exeDir/../Library/XFont 直连轮廓字库，失败回落内置。
 *
 *  @details 安卓走**家族名**而非 exe 同级直连路径（2026-11-09）：
 *   桌面上 applicationDirPath() 是 exe 目录，故 "<exeDir>/../Library/XFont/
 *   XFontOutlineCommon" 能命中工作副本的字库；安卓上 argv[0] 无目录 →
 *   applicationDirPath() 落到 XSystem_executableFilePath（读 /proc/self/exe）
 *   → 得到的是 /system/bin（安卓进程的 exe 是 app_process），拼出的
 *   "/system/Library/XFont/..." 永远不存在 → FT 探测落空 → **静默**回退
 *   位图字库 XFont8x16（只有 96 个汉字）→ 中文整片空白。
 *   安卓上字库的真源是编译期目录 XFONT_EXTERNAL_FT_FONT_DIR
 *   （根 CMakeLists 的安卓分支注入 = /data/data/<pkg>/files/fonts，由安卓
 *   壳启动时从 APK assets 释放），故此处用**家族名**让 FT 走目录探测链
 *   ——与 XGuiWindowDemo 的安卓分支同款口径（xgui_window_demo.c 的
 *   XGUI_DEMO_DEFAULT_FONT_FAMILY）。
 */
void HsfTheme_applyDefaultFont(XWidget* widget)
{
    XFont font;
    if (!widget)
        return;
#if defined(__ANDROID__)
    /* 安卓：家族名 → FT 按编译期目录探测 <dir>/<family>.{ttf,otf,ttc}。 */
    XFont_init(&font);
    XFont_setFamily(&font, XFONT_DEFAULT_FAMILY);
    XWidget_setFont(widget, &font);
    XClassDeinit(&font);
#else
    {
        const XString* exeDir;
        char fontPath[512];
        exeDir = XCoreApplication_applicationDirPath();
        if (exeDir &&
            snprintf(fontPath, sizeof(fontPath),
                     "%s/../Library/XFont/XFontOutlineCommon",
                     XString_toUtf8(exeDir)) > 0 &&
            strlen(fontPath) < sizeof(fontPath)) {
            XFont_init(&font);
            XFont_setFamily(&font, fontPath); /* setFamily 深拷贝路径串。 */
            XWidget_setFont(widget, &font);
            XClassDeinit(&font);
            XClassDelete((XClass*)exeDir);
            return;
        }
        if (exeDir)
            XClassDelete((XClass*)exeDir);
    }
    XFont_init(&font);
    XFont_setFamily(&font, XFONT_DEFAULT_FAMILY);
    XWidget_setFont(widget, &font);
    XClassDeinit(&font);
#endif
}

/** @brief 安装缺省主题（Fusion 样式 + 主题样式表）。 */
void HsfTheme_install(void)
{
#if XSTYLE_ON
    XFusionStyle_installDefault();
    XStyle_installStyleSheet(hsf_theme_css);
#endif
}
