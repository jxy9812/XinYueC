/**
 * @file       XSmallDefectsAlignTest.c
 * @brief      矩阵小缺陷收口域（SMALL）回归锁：XMenu 撕离菜单（tear-off）
 *             与 XDir_Hidden 语义。
 * @details    对齐依据（Qt 6.8.3 源码 /home/xinyue/Qt-src/6.8.3/Src/qtbase/src）：
 *             - QMenu 撕离：QTornOffMenu（qmenu.cpp:52-155，独立常驻窗 +
 *               动作借用注入 + 触发不关窗）、tearoff 条带布局
 *               （qmenu.cpp:360-361 base_y 计入 tearoffHeight；
 *               PM_MenuTearoffHeight=10，qcommonstyle.cpp:4746-4748）、
 *               把手 release（qmenu.cpp:1354-1367）、show/hide/
 *               isTearOffMenuVisible（qmenu.cpp:2110-2161）、
 *               setTearOffEnabled 关闭先隐藏（qmenu.cpp:2083-2093）、
 *               syncWithMenu 主菜单动作增删镜像（qmenu.cpp:125-136）；
 *             - QDir::Hidden = **额外包含**隐藏条目（qdir.cpp:1219 文档
 *               口径「List hidden files」；不含该位时隐藏条目被排除——
 *               2026-11-15 收口，旧实现为语义相反的「只列隐藏条目」）。
 *             同批其余收口的行为锁分布：XFrameStyleOption→XStyleOptionFrame
 *             合并的断言更新在 XFrameAlignTest.c（sharedEdits 同批）；
 *             P0-E13 m_textModeEnabled 物理删除以「全仓 grep 零命中」为
 *             回归口径（无字段偏移锁，见域结果 openNotes）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XMENU_ON && XACTION_ON

#include "XMenu.h"
#include "XAction.h"
#include "XWidget.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XString.h"
#include "XVector.h"
#include "XImage.h"
#include "XPainter.h"
#include "XCommonStyle.h"

/* ==================== 撕离菜单（tear-off） ==================== */

/** @brief 动作表元素个数。 */
static int64_t xsml_menuCount(const XMenu* menu)
{
    const XVector* list = XMenu_actions(menu);
    return list ? (int64_t)XVector_size_base((const XContainer*)list) : 0;
}

/** @brief tearOffEnabled 前后的布局契约：把手条带高 10 计入条目基线与
 *         sizeHint 高度；把手条带不参与动作命中（qmenu.cpp:360-361、
 *         qcommonstyle.cpp:4746-4748、qmenu.cpp:1348-1369）。 */
XQ_TEST(XMenu, tearOffLayoutContract)
{
    XMenu* menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "T");
    XAction* a1;
    XAction* a2;
    XRect geo;
    XSize hint;
    XPoint p;

    XQ_REQUIRE(menu != NULL);
    a1 = XMenu_addAction_2(menu, "One");
    a2 = XMenu_addAction_2(menu, "Two");
    XQ_REQUIRE(a1 != NULL);
    XQ_REQUIRE(a2 != NULL);

    /* 缺省：无撕离（qmenu.cpp:2079「By default, this property is false」）。 */
    XQ_CHECK(!XMenu_isTearOffEnabled(menu));
    XQ_CHECK(!XMenu_isTearOffMenuVisible(menu));

    /* 未启用撕离：首行 y=0、sizeHint 高 = 行数 × 条目高(20)。 */
    geo = XMenu_actionGeometry(menu, a1);
    XQ_EQ_I64(geo.y, 0);
    hint = XWidget_sizeHint_base((const XWidget*)menu);
    XQ_EQ_I64(hint.height, 2 * 20);

    /* 启用撕离：条目区整体下移把手条带高 10（PM_MenuTearoffHeight）。 */
    XMenu_setTearOffEnabled(menu, true);
    XQ_CHECK(XMenu_isTearOffEnabled(menu));
    geo = XMenu_actionGeometry(menu, a1);
    XQ_EQ_I64(geo.y, 10);
    geo = XMenu_actionGeometry(menu, a2);
    XQ_EQ_I64(geo.y, 10 + 20);
    hint = XWidget_sizeHint_base((const XWidget*)menu);
    XQ_EQ_I64(hint.height, 10 + 2 * 20);

    /* 把手条带 [0,10) 不参与动作命中；条目区命中与未启用时一致。 */
    p.x = 5;
    p.y = 5;
    XQ_CHECK(XMenu_actionAt(menu, &p) == NULL);
    p.x = 5;
    p.y = 10 + 5;
    XQ_CHECK(XMenu_actionAt(menu, &p) == a1);
    p.y = 10 + 20 + 5;
    XQ_CHECK(XMenu_actionAt(menu, &p) == a2);

    /* 同值早退：重复设置不触发重绘/布局副作用（仅可观察为状态不变）。 */
    XMenu_setTearOffEnabled(menu, true);
    XQ_CHECK(XMenu_isTearOffEnabled(menu));
    geo = XMenu_actionGeometry(menu, a1);
    XQ_EQ_I64(geo.y, 10);

    XClassDelete((XClass*)menu);
}

/** @brief 撕离窗口生命周期与动作镜像：show/hide/visible 往返、
 *         setTearOffEnabled(false) 先隐藏（qmenu.cpp:2083-2093）、
 *         主菜单增删/clear 镜像到撕离窗（syncWithMenu，qmenu.cpp:125-136）、
 *         撕离副本无把手条带（m_tearOffTorn ⇒ tearoffHeight=0）、
 *         主菜单析构时撕离窗随 deinit 显式回收。 */
XQ_TEST(XMenu, tearOffWindowLifecycle)
{
    XMenu* menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "L");
    XAction* a1;
    XMenu* torn;
    XPoint pos;
    XSize hint;

    XQ_REQUIRE(menu != NULL);
    a1 = XMenu_addAction_2(menu, "Alpha");
    XQ_REQUIRE(a1 != NULL);
    XMenu_setTearOffEnabled(menu, true);
    pos.x = 30;
    pos.y = 40;

    /* showTearOffMenu(const QPoint&)：惰性建撕离窗、定位显示
     * （qmenu.cpp:2119-2131）；显示后 isTearOffMenuVisible 为 true
     * （qmenu.cpp:2110-2113 tornPopup && isVisible()）。 */
    XQ_CHECK(menu->m_tarnPopup == NULL);
    XMenu_showTearOffMenu_2(menu, &pos);
    torn = menu->m_tarnPopup;
    XQ_REQUIRE(torn != NULL);
    XQ_CHECK(XMenu_isTearOffMenuVisible(menu));

    /* 撕离窗是**独立同族实例**（QTornOffMenu : QMenu）：副本形态位
     * 置位 ⇒ 无把手条带（sizeHint 高只含动作行；对比主菜单同条数
     * 尺寸差 10）。 */
    XQ_CHECK(torn != menu);
    XQ_CHECK(torn->m_tearOffTorn);
    hint = XWidget_sizeHint_base((const XWidget*)torn);
    XQ_EQ_I64(hint.height, 1 * 20);
    {
        XMenu* probe = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
        XQ_REQUIRE(probe != NULL);
        XMenu_addAction_2(probe, "Alpha");
        XMenu_setTearOffEnabled(probe, true);
        hint = XWidget_sizeHint_base((const XWidget*)probe);
        XQ_EQ_I64(hint.height, 10 + 1 * 20);
        XClassDelete((XClass*)probe);
    }

    /* 动作增删镜像（syncWithMenu）：主菜单追加后撕离窗同步出现。 */
    XQ_EQ_I64(xsml_menuCount(torn), 1);
    XMenu_addAction_2(menu, "Beta");
    XQ_EQ_I64(xsml_menuCount(torn), 2);
    XMenu_removeAction(menu, a1);
    /* removeAction 把所有权归还调用方（Qt 同语义）：测试自此持有 a1，
     * 必须自行回收（撕离窗借用条目已随 syncWithMenu 摘除，可安全删）。 */
    XClassDelete((XClass*)a1);
    XQ_EQ_I64(xsml_menuCount(torn), 1);

    /* clear 先清撕离窗借用条目再销毁拥有动作（时序锁：撕离窗内不得
     * 残留悬垂借用指针）。 */
    XMenu_clear(menu);
    XQ_EQ_I64(xsml_menuCount(torn), 0);
    XQ_EQ_I64(xsml_menuCount(menu), 0);

    /* hideTearOffMenu：隐藏后 visible=false（X 侧实例复用不销毁——
     * Qt WA_DeleteOnClose 即删，对外语义等价，见 XMenu.h hide 契约）。 */
    XMenu_showTearOffMenu_2(menu, &pos);
    XQ_CHECK(XMenu_isTearOffMenuVisible(menu));
    XMenu_hideTearOffMenu(menu);
    XQ_CHECK(!XMenu_isTearOffMenuVisible(menu));

    /* setTearOffEnabled(false)：先强制隐藏撕离窗（qmenu.cpp:2085-2086
     * `if (!b) hideTearOffMenu()`）。 */
    XMenu_setTearOffEnabled(menu, true);
    XMenu_showTearOffMenu_2(menu, &pos);
    XQ_CHECK(XMenu_isTearOffMenuVisible(menu));
    XMenu_setTearOffEnabled(menu, false);
    XQ_CHECK(!XMenu_isTearOffEnabled(menu));
    XQ_CHECK(!XMenu_isTearOffMenuVisible(menu));

    /* 主菜单析构：撕离窗随 VXMenu_deinit 显式回收（无父独立窗不被
     * 父子链级联）；ASan 堆计数回归由统一门禁守护。收尾先 hide，
     * 对齐「不再可见后回收」的常规窗口销毁路径。 */
    if (menu->m_tarnPopup)
        XWidget_hide((XWidget*)menu->m_tarnPopup);
    XClassDelete((XClass*)menu);
}

/** @brief 把手虚线可见性（像素级，复核 2026-11-15）：把手绘制必须在
 *         整矩形背景填充**之后**（后画者胜；首版画在 fillRect 之前被
 *         0xFFF0F0F0 背景无条件盖掉，视觉永不可见——Qt 文档口径
 *         「often shown as a dashed line at the top」，qmenu.cpp:2073-2075）。
 *         离屏绘制逐像素断言把手条带 y∈[1,10) 存在深灰(0x50)短划像素；
 *         自绘路径与 XCommonStyle 样式接管路径都验。离屏模式先例
 *         xgui_regression_test.c:27895 菜单像素回归。 */
XQ_TEST(XMenu, tearOffHandleVisible)
{
    int path;

    for (path = 0; path < 2; ++path) {
        XMenu* menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "V");
        XImage image;
        XPainter painter;
        XCommonStyle* cs = NULL; /* 提升到循环体作用域：收尾段要删。 */
        int x, y;
        int handleNonbg = 0;
        int itemNonbg = 0;

        XQ_REQUIRE(menu != NULL);
        XQ_REQUIRE(XMenu_addAction_2(menu, "One") != NULL);
        XMenu_setTearOffEnabled(menu, true);
        if (path == 1) {
            cs = XCommonStyle_create();
            XQ_REQUIRE(cs != NULL);
            XWidget_setStyle((XWidget*)menu, (XStyle*)cs);
        }
        XWidget_resize((XWidget*)menu, 160, 60);

        XMemset(&image, 0, sizeof(image));
        XImage_init_ex(&image, 160, 60, XImageFormat_ARGB32);
        XImage_fill(&image, 0xFFF0F0F0u);
        XPainter_init(&painter, NULL);
        XQ_REQUIRE(XPainter_begin_image(&painter, &image));
        XMenu_drawContents(menu, &painter);
        XPainter_end(&painter);
        XPainter_deinit(&painter);

        /* 把手条带 [1,10)：应含虚线深灰短划（修复前恒为背景 0xF0）。 */
        for (y = 1; y < 10; ++y)
            for (x = 0; x < 160; ++x) {
                uint32_t p = XImage_pixel(&image, x, y);
                if (((p >> 16) & 0xffu) < 200u)
                    ++handleNonbg;
            }
        XQ_CHECK(handleNonbg > 0);

        /* 条目区（≥20）：动作文本应已渲染（防止把手修复挤掉条目）。 */
        for (y = 20; y < 40; ++y)
            for (x = 0; x < 160; ++x) {
                uint32_t p = XImage_pixel(&image, x, y);
                if (((p >> 16) & 0xffu) < 200u)
                    ++itemNonbg;
            }
        XQ_CHECK(itemNonbg > 0);

        XClassDeinit(&image);
        if (path == 1) {
            XWidget_setStyle((XWidget*)menu, NULL);
            /* setStyle 不接管样式所有权：本用例自建的 XCommonStyle 必须
             * 自删（漏删即复核遍 +128，运行器判 [LEAK]）。 */
            XClassDelete((XClass*)cs);
        }
        XClassDelete((XClass*)menu);
    }
}

#endif /* XGUI_ON && XWIDGET_ON && XMENU_ON && XACTION_ON */

/* ==================== XDir_Hidden 语义（Qt 对齐） ==================== */

#include "XFileSystem_config.h" /* XDIR_ON 缺省定义 */
#include "XDir.h"
#include "XStringList.h"
#include "XDeviceFile.h"
#include "XMemory.h"

#if XDIR_ON

/** @brief 列表内是否含指定名字。 */
static bool xsml_listHas(const XStringList* list, const char* utf8)
{
    int64_t i;
    int64_t n;
    if (!list) return false;
    n = (int64_t)XStringList_size_base(list);
    for (i = 0; i < n; ++i) {
        XString* item = (XString*)XStringList_at_base(list, i);
        if (item && XString_equals_utf8(item, utf8, XChar_CaseSensitive))
            return true;
    }
    return false;
}

/** @brief 建一个空文件（WriteOnly|Truncate 即创建）。 */
static bool xsml_touchFile(const char* dirUtf8, const char* nameUtf8)
{
    XDeviceOpenOptions options;
    XString* joined;
    XFd fd;
    int err = 0;
    bool ok;
    XMemset(&options, 0, sizeof(options));
    /* joined = dirUtf8 + "/" + nameUtf8（直接拼接，避免依赖 XDir
     * filepath API 形态差异）。 */
    joined = XString_create_utf8(dirUtf8);
    if (!joined) return false;
    XString_append_utf8(joined, "/");
    XString_append_utf8(joined, nameUtf8);
    options.m_openMode = (uint32_t)(XIODevice_WriteOnly | XIODevice_Truncate);
    options.m_target = joined;
    fd = XDevice_open(XDeviceType_File, &options, &err);
    ok = (fd != XFD_INVALID);
    if (ok) XDevice_close(fd);
    XClassDelete((XClass*)joined);
    return ok;
}

/** @brief QDir::Hidden = 额外包含（qdir.cpp:1219）：不含该位时隐藏条目
 *         被排除；含则隐藏与可见条目一并照列（旧「只列隐藏」语义已收口）。
 *         依赖 sharedEdits（XDir.c 过滤分支）已应用——统一门禁在收口员
 *         应用 sharedEdits 之后运行。
 * @note [2026-11-17 合并修正·平台前提] 本用例的夹具用「点号前缀」当隐藏
 *       文件（POSIX 约定：名字以 '.' 开头即隐藏）。**Windows 上「隐藏」是
 *       FILE_ATTRIBUTE_HIDDEN**（本仓 XDeviceFile_win32_api.c:363/820 的
 *       stat->isHidden 口径），点号文件在 Windows 上**不是**隐藏文件 ⇒
 *       「不含 Hidden 时隐藏条目应被排除」这条断言在 Windows 主机上必然
 *       失败（远端自带该用例，其在 POSIX 上的绿不能外推到 Windows）。
 *       本仓无「设置隐藏属性」的 API（XFile/XDeviceFile 均无 setHidden），
 *       故夹具无法在 Windows 上造出真隐藏文件 ⇒ 该用例按平台前提整段
 *       只在 POSIX 平台执行（与本目录既有 `#if !defined(_WIN32)` 惯例
 *       一致，如 XTimeZoneCalendarAlignTest.c:152）。 */
#if !defined(_WIN32)
XQ_TEST(XDir, hiddenFilterIncludesExtra)
{
    const char* tmp = "xq_dir_hidden_tmp";
    XDir* dir;
    XStringList* entries;
    XString* tmpStr;

    tmpStr = XString_create_utf8(tmp);
    XQ_REQUIRE(tmpStr != NULL);

    /* 环境重建：删旧测试目录 → mkpath 建目录 → 建隐藏/可见两个空文件。
     * （XDir_mkdir 的 dirName 语义有已知偏差——XLinkQualityTest.c:973
     *  同款结论——目录创建走 XDir_mkpath 路径版。） */
    {
        XDir* stale = XDir_create_2(tmpStr);
        if (stale && XDir_exists_1(stale))
            XDir_removeRecursively(stale);
        XClassDelete((XClass*)stale);
    }
    {
        XDir* cwd = XDir_create_1();
        XQ_REQUIRE(cwd != NULL);
        XQ_CHECK(XDir_mkpath(cwd, tmpStr));
        XClassDelete((XClass*)cwd);
    }
    dir = XDir_create_2(tmpStr);
    XQ_REQUIRE(dir != NULL);
    XQ_CHECK(XDir_exists_1(dir));
    XQ_REQUIRE(xsml_touchFile(tmp, ".hidden_file"));
    XQ_REQUIRE(xsml_touchFile(tmp, "visible_file"));

    /* 不含 Hidden：隐藏条目被排除、可见条目照列。 */
    entries = XDir_entryList_1(dir,
                               (XDirFilters)(XDir_Files | XDir_NoDotAndDotDot),
                               (XDirSortFlags)XDir_Name);
    XQ_REQUIRE(entries != NULL);
    XQ_CHECK(!xsml_listHas(entries, ".hidden_file"));
    XQ_CHECK(xsml_listHas(entries, "visible_file"));
    XClassDelete((XClass*)entries);

    /* 含 Hidden：额外包含——隐藏与可见一并照列（与旧语义的判别点：
     * 旧实现此处 visible_file 被剔、只剩隐藏条目）。 */
    entries = XDir_entryList_1(dir,
                               (XDirFilters)(XDir_Files | XDir_Hidden |
                                             XDir_NoDotAndDotDot),
                               (XDirSortFlags)XDir_Name);
    XQ_REQUIRE(entries != NULL);
    XQ_CHECK(xsml_listHas(entries, ".hidden_file"));
    XQ_CHECK(xsml_listHas(entries, "visible_file"));
    XClassDelete((XClass*)entries);

    XDir_removeRecursively(dir);
    XClassDelete((XClass*)dir);
    XClassDelete((XClass*)tmpStr);
}
#endif /* !defined(_WIN32) —— 隐藏语义为 POSIX 约定（点号前缀），见用例头注 */

#endif /* XDIR_ON */
