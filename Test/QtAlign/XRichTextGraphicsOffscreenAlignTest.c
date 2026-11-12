/**
 * @file       XRichTextGraphicsOffscreenAlignTest.c
 * @brief      §B16/§B18/§B19 有界子集 × Qt 6.8 对齐契约测试（XTextOption /
 *             XTextDocument 页面尺寸与文本选项与基线偏移 / XOffscreenSurface
 *             请求格式播种 / XProcess CreateProcess 参数修饰器）。
 * @details    对齐依据（本机 Qt 6.8.0 offscreen 探针 + Qt 6.8.3 原文）：
 *             - QTextOption 缺省（qtextoption.cpp 构造族）：QTextOption()=
 *               align AlignLeft(1)/wrap WordWrap(1)/dir Auto(2)/tab -1/
 *               design false/flags 0；QTextOption(Qt::AlignLeft) 的
 *               direction=QGuiApplication::layoutDirection()=LTR(0)；
 *             - QTextDocumentPrivate 构造把文档 defaultTextOption 改设为
 *               tabStopDistance=80、wrapMode=WrapAtWordBoundaryOrAnywhere(4)
 *               （探针文档缺省选项 align=1/wrap=4/tab=80/design=0/flags=0/
 *               dir=2）；useDesignMetrics 单源于该选项的 design 位
 *               （qtextdocument.cpp useDesignMetrics/setUseDesignMetrics；
 *               探针：setDefaultTextOption(design=1) ⇒ useDesignMetrics()==1，
 *               setUseDesignMetrics(true) ⇒ 选项 design==1）；
 *             - pageSize 与 textWidth 同源（qtextdocument.cpp：textWidth()
 *               返回 pageSize.width；setTextWidth(w) ⇒ pageSize=(w,-1)；
 *               setPageSize(w,h) ⇒ textWidth()==w；缺省 QSizeF(-1,-1)）；
 *             - 基线三属性缺省 50.0 / 100.0/6.0 / 0.0（qtextformat.h
 *               superScriptBaseline/subScriptBaseline/baselineOffset 的
 *               「属性未设」分支；探针 50/16.6667/0），且 **clone 不复制**
 *               （Qt clone 只回填 defaultFont；探针源 70/20/-5 ⇒ clone
 *               50/16.6667/0）；defaultTextOption 随 clone 复制（探针
 *               clone 后 design/tab/wrap 原样保留）；
 *             - QOffscreenSurfacePrivate 构造把 requestedFormat 播种为
 *               QSurfaceFormat::defaultFormat()（qoffscreensurface_p.h；
 *               探针 setDefaultFormat(depth16/red5/ver3.2) 后新表面对象的
 *               requestedFormat==defaultFormat）；format() 在无平台表面时
 *               恒返回 requestedFormat（qoffscreensurface.cpp format() 的
 *               第三分支；平台实际格式分支登记为 §B18 deferred）；
 *             - QProcess::setCreateProcessArgumentsModifier（qprocess.h
 *               Q_OS_WIN 专有段）：setter/getter 只登记/回读修饰回调；
 *               POSIX 上 Qt 根本不声明该 API ⇒ 本框架的 POSIX 入口为
 *               **无行为面**（登记回调、启动路径永不调用）。
 * @note       本文件与其它 QtAlign 用例一样自注册、无 main；泄漏门按
 *             xq_align_run.c 的零容忍口径执行（本文件全部用例无堆分配面）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif

#if XGUI_ON && XTEXTDOCUMENT_ON

#include "XTextOption.h"
#include "XTextDocument.h"
#include "XClass.h"
#include "XMemory.h"

/* ==================== §B16a：XTextOption 值类型 ==================== */

XQ_TEST(XTextOption, valueSemantics)
{
    XTextOption option;
    XTextOptionTab tab;
    XTextOptionTab tabB;
    double positions[3];
    double roundTrip[3];

    /* QTextOption() 缺省（探针 raw：align=1 wrap=1 tab=-1 design=0
     * flags=0 dir=2=Auto；tabCount=0）。 */
    XTextOption_init(&option);
    XQ_EQ_I64(option.m_alignment, 0x1);
    XQ_EQ_I64(option.m_wrapMode, (int)XTextOptionWrap_WordWrap);
    XQ_EQ_I64(option.m_textDirection, 2);
    XQ_EQ_I64((int)option.m_flags, 0);
    XQ_CHECK(!XTextOption_useDesignMetrics(&option));
    XQ_CHECK(XTextOption_tabStopDistance(&option) == -1.0);
    XQ_EQ_I64(XTextOption_tabCount(&option), 0);

    /* 标量属性往返（flags 含符号位 0x80000000，uint32 承载无损）。 */
    XTextOption_setAlignment(&option, 0x2 | 0x8);
    XTextOption_setTextDirection(&option, 1);
    XTextOption_setWrapMode(&option, (int)XTextOptionWrap_Anywhere);
    XTextOption_setFlags(&option, XTextOptionFlag_IncludeTrailingSpaces);
    XTextOption_setTabStopDistance(&option, 42.0);
    XTextOption_setUseDesignMetrics(&option, true);
    XQ_EQ_I64(XTextOption_alignment(&option), 0x2 | 0x8);
    XQ_EQ_I64(XTextOption_textDirection(&option), 1);
    XQ_EQ_I64(XTextOption_wrapMode(&option), (int)XTextOptionWrap_Anywhere);
    XQ_CHECK(XTextOption_flags(&option) ==
             (uint32_t)XTextOptionFlag_IncludeTrailingSpaces);
    XQ_CHECK(XTextOption_tabStopDistance(&option) == 42.0);
    XQ_CHECK(XTextOption_useDesignMetrics(&option));

    /* QTextOption(Qt::AlignLeft) 单参构造：direction 取应用布局方向
     * （探针 raw2：LTR=0；本框架 runner 只建 XCoreApplication，
     * XGuiApplication_layoutDirection() 缺省回落 LTR——同口径）。 */
    option = XTextOption_create_alignment(0x2);
    XQ_EQ_I64(option.m_alignment, 0x2);
#if XGUIAPPLICATION_ON
    XQ_EQ_I64(option.m_textDirection,
              (int)XGuiLayoutDirection_LeftToRight);
#else
    XQ_EQ_I64(option.m_textDirection, 0);
#endif
    XQ_EQ_I64(option.m_wrapMode, (int)XTextOptionWrap_WordWrap);

    /* setTabArray：整体替换 + 返回写入数；Tab 缺省 type=Left、delim=0。 */
    positions[0] = 20.0;
    positions[1] = 60.0;
    positions[2] = 120.0;
    XQ_EQ_I64(XTextOption_setTabArray(&option, positions, 3), 3);
    XQ_EQ_I64(XTextOption_tabCount(&option), 3);
    XQ_CHECK(XTextOption_tab(&option, 0, &tab));
    XQ_CHECK(tab.position == 20.0);
    XQ_EQ_I64(tab.type, (int)XTextOptionTab_Left);
    XQ_EQ_I64((int)tab.delimiter, 0);
    XQ_CHECK(XTextOption_tab(&option, 2, &tab));
    XQ_CHECK(tab.position == 120.0);
    XQ_CHECK(!XTextOption_tab(&option, 3, &tab)); /* 越界。 */

    XQ_CHECK(XTextOption_tabArray(&option, roundTrip, 3) == 3);
    XQ_CHECK(roundTrip[0] == 20.0 && roundTrip[1] == 60.0 &&
             roundTrip[2] == 120.0);
    XQ_EQ_I64(XTextOption_tabArray(&option, NULL, 0), 0);

    /* 定容截断（有界子集）：超出 XTEXTOPTION_MAX_TABS 的部分丢弃。 */
    {
        double many[XTEXTOPTION_MAX_TABS + 5];
        int i;
        for (i = 0; i < (int)(sizeof(many) / sizeof(many[0])); ++i)
            many[i] = (double)(i + 1);
        XQ_EQ_I64(XTextOption_setTabArray(&option, many,
                                          (int)(sizeof(many) / sizeof(many[0]))),
                  XTEXTOPTION_MAX_TABS);
        XQ_EQ_I64(XTextOption_tabCount(&option), XTEXTOPTION_MAX_TABS);
    }

    /* setTabs + Tab::operator== 口径（type/delimiter 相等 + position 模糊
     * 相等：|a-b|*1e12 <= min(|a|,|b|)）。 */
    tab.position = 80.0;
    tab.type = (int)XTextOptionTab_Center;
    tab.delimiter = 0x2Cu;
    tabB = tab;
    tabB.position = 80.0 + 1e-13;
    XQ_CHECK(XTextOption_tab_equals(&tab, &tabB));
    tabB.position = 81.0;
    XQ_CHECK(!XTextOption_tab_equals(&tab, &tabB));
    tabB = tab;
    tabB.delimiter = 0x2Du;
    XQ_CHECK(!XTextOption_tab_equals(&tab, &tabB));
    XQ_EQ_I64(XTextOption_setTabs(&option, &tab, 1), 1);
    XQ_CHECK(XTextOption_tab(&option, 0, &tabB));
    XQ_CHECK(XTextOption_tab_equals(&tabB, &tab));

    /* NULL 位置/列表 = 清空（整体替换语义）；NULL self 各入口安全。 */
    XQ_EQ_I64(XTextOption_setTabArray(&option, NULL, 3), 0);
    XQ_EQ_I64(XTextOption_tabCount(&option), 0);
    XQ_EQ_I64(XTextOption_setTabs(NULL, &tab, 1), 0);
    XQ_CHECK(!XTextOption_useDesignMetrics(NULL));
    XQ_CHECK(XTextOption_alignment(NULL) == 0x1);
}

/* ==================== §B16b：pageSize ↔ textWidth 同源 ==================== */

XQ_TEST(XTextDocument, pageSizeTextWidthCoupling)
{
    XTextDocument* doc = XTextDocument_create();
    XSizeF size;
    XTextDocument* copy;
    XQ_REQUIRE(doc != NULL);

    /* Qt 缺省：pageSize = QSizeF(-1,-1)，textWidth() = -1（探针
     * pageSize -1,-1、textWidth -1）。 */
    size = XTextDocument_pageSize(doc);
    XQ_CHECK(size.width == -1.0f);
    XQ_CHECK(size.height == -1.0f);
    XQ_CHECK(XTextDocument_textWidth(doc) == -1.0);

    /* setTextWidth ⇒ pageSize=(w,-1)（探针 setTextWidth(120) 后
     * pageSize 120x-1）。 */
    XTextDocument_setTextWidth(doc, 120.0);
    size = XTextDocument_pageSize(doc);
    XQ_CHECK(size.width == 120.0f);
    XQ_CHECK(size.height == -1.0f);
    XQ_CHECK(XTextDocument_textWidth(doc) == 120.0);

    /* setPageSize ⇒ textWidth 变为宽度（探针 setPageSize(300,400) 后
     * textWidth==300）。 */
    size.width = 300.0f;
    size.height = 400.0f;
    XTextDocument_setPageSize(doc, size);
    XQ_CHECK(XTextDocument_textWidth(doc) == 300.0);
    size = XTextDocument_pageSize(doc);
    XQ_CHECK(size.width == 300.0f);
    XQ_CHECK(size.height == 400.0f);

    /* pageSize(0,0)：宽度读数 0（Qt 探针 textWidth==0、isNull==true）。 */
    size.width = 0.0f;
    size.height = 0.0f;
    XTextDocument_setPageSize(doc, size);
    XQ_CHECK(XTextDocument_textWidth(doc) == 0.0);

    /* clone 复制 pageSize（qtextdocument.cpp clone 的 priv->pageSize；
     * 探针 clone pageSize 100x50 原样）。 */
    size.width = 100.0f;
    size.height = 50.0f;
    XTextDocument_setPageSize(doc, size);
    copy = XTextDocument_clone(doc);
    XQ_REQUIRE(copy != NULL);
    size = XTextDocument_pageSize(copy);
    XQ_CHECK(size.width == 100.0f);
    XQ_CHECK(size.height == 50.0f);
    XQ_CHECK(XTextDocument_textWidth(copy) == 100.0);
    XClassDelete((XClass*)copy);

    /* NULL 安全：缺省值路径。 */
    size = XTextDocument_pageSize(NULL);
    XQ_CHECK(size.width == -1.0f);
    XQ_CHECK(size.height == -1.0f);
    XTextDocument_setPageSize(NULL, size);
    XTextDocument_setTextWidth(NULL, 10.0);

    XClassDelete((XClass*)doc);
}

/* ==================== §B16c：defaultTextOption（含 useDesignMetrics 单源） == */

XQ_TEST(XTextDocument, defaultTextOptionCoupling)
{
    XTextDocument* doc = XTextDocument_create();
    XTextOption option;
    XTextDocument* copy;
    XQ_REQUIRE(doc != NULL);

    /* 文档缺省 = QTextOption() + QTextDocumentPrivate 改设（探针：
     * align=1 wrap=4 tab=80 design=0 flags=0 dir=2）。 */
    option = XTextDocument_defaultTextOption(doc);
    XQ_EQ_I64(option.m_alignment, 0x1);
    XQ_EQ_I64(option.m_wrapMode, (int)XTextOptionWrap_WordBoundaryOrAnywhere);
    XQ_CHECK(option.m_tabStopDistance == 80.0);
    /* useDesignMetrics 单源在文档级 getter（即选项 design 位，探针缺省 0）。 */
    XQ_CHECK(!XTextDocument_useDesignMetrics(doc));
    XQ_EQ_I64((int)option.m_flags, 0);
    XQ_EQ_I64(option.m_textDirection, 2);

    /* 整值替换：设入裸 QTextOption()（tab=-1、wrap=1）后逐字段生效、
     * 无合并（探针：setDefaultTextOption 后 tab=-1、wrap=0）。 */
    option = XTextOption_create();
    option.m_wrapMode = (int)XTextOptionWrap_NoWrap;
    option.m_alignment = 0x2;
    option.m_tabStopDistance = 42.0;
    option.m_textDirection = 1;
    option.m_useDesignMetrics = 1;
    XTextDocument_setDefaultTextOption(doc, &option);
    option = XTextDocument_defaultTextOption(doc);
    XQ_EQ_I64(option.m_wrapMode, (int)XTextOptionWrap_NoWrap);
    XQ_EQ_I64(option.m_alignment, 0x2);
    XQ_CHECK(option.m_tabStopDistance == 42.0);
    XQ_EQ_I64(option.m_textDirection, 1);
    /* useDesignMetrics 单源：文档 getter 就是选项 design 位（探针
     * setDefaultTextOption(design=1) ⇒ useDesignMetrics()==1）。 */
    XQ_CHECK(XTextDocument_useDesignMetrics(doc));

    /* 反向耦合：setUseDesignMetrics(false) 改写选项 design 位（Qt
     * setUseDesignMetrics 写 defaultTextOption；探针同口径）。 */
    XTextDocument_setUseDesignMetrics(doc, false);
    option = XTextDocument_defaultTextOption(doc);
    XQ_CHECK(!option.m_useDesignMetrics);
    XQ_CHECK(!XTextDocument_useDesignMetrics(doc));
    XTextDocument_setUseDesignMetrics(doc, true);
    option = XTextDocument_defaultTextOption(doc);
    XQ_CHECK(option.m_useDesignMetrics);
    XQ_CHECK(XTextDocument_useDesignMetrics(doc));

    /* clone 复制 defaultTextOption（qtextdocument.cpp clone 的
     * priv->defaultTextOption；探针 clone 后 design/tab/wrap 原样）。 */
    copy = XTextDocument_clone(doc);
    XQ_REQUIRE(copy != NULL);
    option = XTextDocument_defaultTextOption(copy);
    XQ_CHECK(option.m_useDesignMetrics);
    XQ_CHECK(option.m_tabStopDistance == 42.0);
    XQ_EQ_I64(option.m_wrapMode, (int)XTextOptionWrap_NoWrap);
    XQ_CHECK(XTextDocument_useDesignMetrics(copy));
    XClassDelete((XClass*)copy);

    /* NULL 安全。 */
    option = XTextDocument_defaultTextOption(NULL);
    XQ_EQ_I64(option.m_wrapMode, (int)XTextOptionWrap_WordWrap);
    XTextDocument_setDefaultTextOption(NULL, &option);
    XTextDocument_setDefaultTextOption(doc, NULL);

    XClassDelete((XClass*)doc);
}

/* ==================== §B16d：基线三属性（缺省 + clone 不复制） ============ */

XQ_TEST(XTextDocument, baselineOffsetsAndClone)
{
    XTextDocument* doc = XTextDocument_create();
    XTextDocument* copy;
    XQ_REQUIRE(doc != NULL);

    /* Qt 缺省 50.0 / 100.0/6.0 / 0.0（qtextformat.h 属性未设分支；探针
     * 50/16.6667/0）。 */
    XQ_CHECK(XTextDocument_superScriptBaseline(doc) == 50.0);
    XQ_CHECK(XTextDocument_subScriptBaseline(doc) - 100.0 / 6.0 < 1e-9);
    XQ_CHECK(XTextDocument_subScriptBaseline(doc) - 100.0 / 6.0 > -1e-9);
    XQ_CHECK(XTextDocument_baselineOffset(doc) == 0.0);

    /* 往返（含负偏移：Qt 文档口径「正值上移、负值下移」）。 */
    XTextDocument_setSuperScriptBaseline(doc, 70.0);
    XTextDocument_setSubScriptBaseline(doc, 20.0);
    XTextDocument_setBaselineOffset(doc, -5.0);
    XQ_CHECK(XTextDocument_superScriptBaseline(doc) == 70.0);
    XQ_CHECK(XTextDocument_subScriptBaseline(doc) == 20.0);
    XQ_CHECK(XTextDocument_baselineOffset(doc) == -5.0);

    /* clone **不复制**基线（Qt clone 只回填 defaultFont，defaultTextFormat
     * 的基线属性随 formats 集合重置；探针：源 70/20/-5 ⇒ clone 50/16.6667/0）。 */
    copy = XTextDocument_clone(doc);
    XQ_REQUIRE(copy != NULL);
    XQ_CHECK(XTextDocument_superScriptBaseline(copy) == 50.0);
    XQ_CHECK(XTextDocument_subScriptBaseline(copy) - 100.0 / 6.0 < 1e-9);
    XQ_CHECK(XTextDocument_baselineOffset(copy) == 0.0);
    XClassDelete((XClass*)copy);

    /* NULL 安全：返回各自缺省值。 */
    XQ_CHECK(XTextDocument_superScriptBaseline(NULL) == 50.0);
    XQ_CHECK(XTextDocument_baselineOffset(NULL) == 0.0);
    XTextDocument_setSuperScriptBaseline(NULL, 70.0);
    XTextDocument_setSubScriptBaseline(NULL, 20.0);
    XTextDocument_setBaselineOffset(NULL, -5.0);

    XClassDelete((XClass*)doc);
}

#endif /* XGUI_ON && XTEXTDOCUMENT_ON */

#if XGUI_ON && XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON

#include "XOffscreenSurface.h"
#include "XSurfaceFormat.h"

/* ==================== §B18：请求格式以进程默认格式播种 ==================== */

XQ_TEST(XOffscreenSurface, requestedFormatSeededFromProcessDefault)
{
    XSurfaceFormat seeded;
    XOffscreenSurface* surface;
    XSurfaceFormat plain;

    /* Qt：QOffscreenSurfacePrivate 构造 requestedFormat =
     * QSurfaceFormat::defaultFormat()（qoffscreensurface_p.h；探针
     * setDefaultFormat(depth16/red5/ver3.2) 后新表面对象 requestedFormat
     * == defaultFormat）。先验证值构造不吃进程默认（XSurfaceFormat.c
     * 的既有契约），再验证本类播种。 */
    plain = XSurfaceFormat_create();
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&plain), -1);

    seeded = XSurfaceFormat_create();
    XSurfaceFormat_setDepthBufferSize(&seeded, 16);
    XSurfaceFormat_setRedBufferSize(&seeded, 5);
    XSurfaceFormat_setMajorVersion(&seeded, 3);
    XSurfaceFormat_setMinorVersion(&seeded, 2);
    XSurfaceFormat_setDefaultFormat(&seeded);

    surface = XOffscreenSurface_create();
    XQ_REQUIRE(surface != NULL);
    seeded = XOffscreenSurface_requestedFormat(surface);
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&seeded), 16);
    XQ_EQ_I64(XSurfaceFormat_redBufferSize(&seeded), 5);
    XQ_EQ_I64(XSurfaceFormat_majorVersion(&seeded), 3);
    XQ_EQ_I64(XSurfaceFormat_minorVersion(&seeded), 2);
    /* format() 无平台表面时恒走 Qt 第三分支返回请求格式（qoffscreensurface.cpp
     * format()；created 后返回平台实际格式的偏差登记为 §B18 deferred）。 */
    seeded = XOffscreenSurface_format(surface);
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&seeded), 16);
    XClassDelete((XClass*)surface);

    /* 恢复进程默认格式为出厂默认（XSurfaceFormat_setDefaultFormat(NULL)
     * 置回 makeDefault，避免污染同进程的其它用例）。 */
    XSurfaceFormat_setDefaultFormat(NULL);
    plain = XSurfaceFormat_defaultFormat();
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&plain), -1);
}

#endif /* XGUI_ON && XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON */

#if XProcess_ON

#include "XProcess.h"
#include "XIODevice.h"

/* ==================== §B19：CreateProcess 参数修饰器（POSIX 无行为面） ==== */

/** @brief 调用计数器（验证 POSIX 启动路径永不触发修饰回调）。 */
static int g_cpArgsModifierCalls;

static void xpg_modifierProbe(void* userData,
                              XProcessCreateProcessArguments* arguments)
{
    (void)arguments;
    if (userData) ++*(int*)userData;
    ++g_cpArgsModifierCalls;
}

XQ_TEST(XProcess, createProcessArgumentsModifierContract)
{
    XProcess* proc = XProcess_create();
    void* userData = NULL;
    XProcessCreateProcessArgumentsModifier got = NULL;
    int marker = 7;
    XQ_REQUIRE(proc != NULL);

    /* 缺省：未登记 ⇒ getter 返回 NULL、userData 置 NULL。 */
    XQ_CHECK(XProcess_createProcessArgumentsModifier(proc, &userData) == NULL);
    XQ_CHECK(userData == NULL);

    /* 登记/回读（Qt setter/getter 的存储语义：d->modifyCreateProcessArgs）。 */
    XProcess_setCreateProcessArgumentsModifier(proc, xpg_modifierProbe,
                                               &marker);
    got = XProcess_createProcessArgumentsModifier(proc, &userData);
    XQ_CHECK(got == xpg_modifierProbe);
    XQ_CHECK(userData == &marker);

    /* 覆盖登记：换 userData 原样生效。 */
    XProcess_setCreateProcessArgumentsModifier(proc, xpg_modifierProbe, NULL);
    got = XProcess_createProcessArgumentsModifier(proc, &userData);
    XQ_CHECK(got == xpg_modifierProbe);
    XQ_CHECK(userData == NULL);

    /* NULL = 移除（Qt：传空函数移除）。 */
    XProcess_setCreateProcessArgumentsModifier(proc, NULL, NULL);
    XQ_CHECK(XProcess_createProcessArgumentsModifier(proc, &userData) == NULL);
    XQ_CHECK(userData == NULL);

    /* NULL self 安全。 */
    XProcess_setCreateProcessArgumentsModifier(NULL, xpg_modifierProbe, NULL);
    XQ_CHECK(XProcess_createProcessArgumentsModifier(NULL, &userData) == NULL);

#if !defined(_WIN32)
    /* POSIX 无行为面：登记修饰回调后真实启动子进程，回调**永不触发**、
     * 进程创建行为不变（Qt 在 POSIX 上根本不声明该 API）。 */
    {
        const char* args[2];
        int markerLocal = 0;
        args[0] = "-c";
        args[1] = "true";
        g_cpArgsModifierCalls = 0;
        XProcess_setCreateProcessArgumentsModifier(proc, xpg_modifierProbe,
                                                   &markerLocal);
        XQ_REQUIRE(XProcess_start_utf8(proc, "sh", args, 2,
                                       XIODevice_ReadOnly));
        XQ_REQUIRE(XProcess_waitForFinished(proc, 5000));
        XQ_EQ_I64(XProcess_state(proc), XProcessState_NotRunning);
        XQ_EQ_I64(g_cpArgsModifierCalls, 0);
        XQ_EQ_I64(markerLocal, 0);
        got = XProcess_createProcessArgumentsModifier(proc, &userData);
        XQ_CHECK(got == xpg_modifierProbe);
        XQ_CHECK(userData == &markerLocal);
        XProcess_setCreateProcessArgumentsModifier(proc, NULL, NULL);
    }
#else
    (void)g_cpArgsModifierCalls;
#endif

    XClassDelete((XClass*)proc);
}

#endif /* XProcess_ON */
