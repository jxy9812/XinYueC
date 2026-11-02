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

/** @brief 套缺省字体：exeDir/../Library/XFont 直连轮廓字库，失败回落内置。 */
void HsfTheme_applyDefaultFont(XWidget* widget)
{
    const XString* exeDir;
    char fontPath[512];
    XFont font;
    if (!widget)
        return;
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
    XFont_init(&font);
    XFont_setFamily(&font, XFONT_DEFAULT_FAMILY);
    XWidget_setFont(widget, &font);
    XClassDeinit(&font);
}

/** @brief 安装缺省主题（Fusion 样式 + 主题样式表）。 */
void HsfTheme_install(void)
{
#if XSTYLE_ON
    XFusionStyle_installDefault();
    XStyle_installStyleSheet(hsf_theme_css);
#endif
}
