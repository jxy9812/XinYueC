/******************************************************************************
 * @file       xgui_demo_page_shell.c
 * @brief      XGuiWindowDemo「Shell」页（页面内 XConsoleShell 控制台）。
 * @details    页索引 23（第 19 扩展页，2026-10-29 自测试菜单页拆分独立）。
 *             「开启 Shell」在页面内创建独立 XConsoleShell（write 回调直写
 *             输出区；registerStaticCommands 挂 XTestCommand，`Test xxx`
 *             可用），「关闭」销毁实例归还内存；输入框回车/「发送」经
 *             processLine 执行一行；输入 clear/cls 清空输出区（GUI 层拦截，
 *             不经 Shell）。[按需内存] Shell 实例只在开启期间存在；输出
 *             缓冲在首段输出到达时才分配（64KB 起、256KB 封顶），清屏释放；
 *             页面本体经扩展页懒分配（首显才建）。
 *             [横竖屏自适应] 单列竖排天然竖屏友好；命令行在窄档仍为横排
 *             （开关钮/输入框/发送的最小宽度合计 < 400 竖屏逻辑宽），
 *             adapt 仅在页宽极窄（<360）时把「发送」换行到输入框下方。
 *             autotest 口径：全程非阻塞——只断言装配（输入框/发送缺省
 *             禁用、输出区就绪、Shell 缺省关闭）；不开 Shell、不执行命令。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XMemory.h"
#include "XObject.h"
#include "XCoreApplication.h"
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XBoxLayout.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XPushButton.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON
#include "XConsoleShell.h"
#include "XConsoleShellIo.h"
#endif
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON
#include "XTestCommand.h"
#endif
#include "xgui_demo_pages.h"

/*
 * 段宏守卫：Shell 能力需 XCONSOLE_SHELL_ON/IO（命令分派另需 COMMAND）
 * + 基础控件族。不满足时降级为契约桩（build 返回 NULL），主文件可无
 * 条件链接。
 */
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XWIDGET_ON && \
    XLABEL_ON && XLINEEDIT_ON && XPUSHBUTTON_ON && XLAYOUT_ON && \
    XLAYOUT_BOX_ON && XSCROLLAREA_ON

/* ==================== 常量 ==================== */

#define SH_BTN_H           40    /**< 按钮/输入行高（触控口径）。 */
#define SH_BUF_INIT        (64u * 1024u)  /**< 输出缓冲初容量。 */
#define SH_BUF_MAX         (256u * 1024u) /**< 输出缓冲上限。 */
/* 「先点「开启 Shell」后再执行命令」 */
#define SH_SHELL_HINT \
    "\xE5\x85\x88\xE7\x82\xB9\xE3\x80\x8C\xE5\xBC\x80\xE5\x90\xAF Shell" \
    "\xE3\x80\x8D\xE5\x90\x8E\xE5\x86\x8D\xE6\x89\xA7\xE8\xA1\x8C\xE5\x91\xBD\xE4\xBB\xA4"

/* ==================== 页面状态 ==================== */

/** @brief 页面单例状态（demo 页无析构钩子，随进程常驻，与同族页一致）。 */
static struct {
    XWidget*       pageScroll;    /**< 页宿主（滚动区，返回给堆叠的根）。 */
    XWidget*       root;          /**< 内容根（widgetResizable 接管几何）。 */
    XBoxLayout     lay;           /**< 根纵布局（命令行 + 输出区）。 */
    XBoxLayout     sendRow;       /**< 命令行（横排：开关|输入|发送）。 */
    XScrollArea*   outputScroll;  /**< 输出滚动区。 */
    XLabel         output;        /**< 输出区（Shell 全部输出）。 */
    XLineEdit*     input;         /**< 命令输入框。 */
    XPushButton*   toggle;        /**< 开启/关闭 Shell。 */
    XPushButton*   send;          /**< 执行命令。 */
    bool           shellOn;       /**< Shell 实例是否已创建。 */
    XConsoleShell* shell;         /**< Shell 实例（开启期间存在）。 */
    char*          buf;           /**< 输出缓冲（首段输出才分配）。 */
    size_t         bufLen;        /**< 已用字节。 */
    size_t         bufCap;        /**< 容量。 */
} s_sh;

/* ==================== 输出缓冲 ==================== */

/** @brief 追加输出；必要时扩容（256KB 上限，超出丢增量保头）。 */
static void sh_appendLen(const char* text, size_t len)
{
    if (!text || len == 0) return;
    if (!s_sh.buf) {
        s_sh.bufCap = SH_BUF_INIT;
        s_sh.buf = (char*)XMalloc_System(s_sh.bufCap);
        if (!s_sh.buf) return;
        s_sh.buf[0] = '\0';
        s_sh.bufLen = 0;
    }
    if (s_sh.bufLen + len + 1 > s_sh.bufCap) {
        size_t cap = s_sh.bufCap;
        char* grown;
        while (cap < s_sh.bufLen + len + 1 && cap < SH_BUF_MAX) cap *= 2u;
        if (cap >= s_sh.bufLen + len + 1) {
            grown = XRealloc_System(s_sh.buf, cap);
            if (grown) { s_sh.buf = grown; s_sh.bufCap = cap; }
        } else {
            return; /* 超 256KB 上限：丢增量。 */
        }
    }
    memcpy(s_sh.buf + s_sh.bufLen, text, len);
    s_sh.bufLen += len;
    s_sh.buf[s_sh.bufLen] = '\0';
    XLabel_setText_2(&s_sh.output, s_sh.buf);
}

/** @brief 追加 NUL 结尾文本。 */
static void sh_append(const char* text)
{
    if (text) sh_appendLen(text, strlen(text));
}

/** @brief 清空输出缓冲与输出区。 */
static void sh_clearOutput(void)
{
    s_sh.bufLen = 0;
    if (s_sh.buf) {
        XFree_System(s_sh.buf);
        s_sh.buf = NULL;   /* 清屏即还内存：缓冲只在有输出时才存在。 */
        s_sh.bufLen = 0;
        s_sh.bufCap = 0;
    }
    XLabel_setText_2(&s_sh.output, "");
}

/** @brief Shell 输出回调：直写输出区（GUI 线程同步回调）。 */
static int64_t sh_shellWrite(void* userData, const void* data, size_t size)
{
    (void)userData;
    if (data && size) sh_appendLen((const char*)data, size);
    return (int64_t)size;
}

/* ==================== 交互 ==================== */

/** @brief 执行输入行：clear/cls 清屏（GUI 层拦截），其余经 processLine。 */
static void sh_submit(void)
{
    const char* line;
    if (!s_sh.input) return;
    line = XLineEdit_text(s_sh.input);
    if (!line || !line[0]) return;
    if (strcmp(line, "clear") == 0 || strcmp(line, "cls") == 0) {
        sh_clearOutput();
        XLineEdit_clear(s_sh.input);
        return;
    }
    sh_append("> ");
    sh_append(line);
    sh_append("\n");
    if (!s_sh.shellOn) {
        sh_append(SH_SHELL_HINT "\n");
        XLineEdit_clear(s_sh.input);
        return;
    }
#if XCONSOLE_SHELL_COMMAND_ON
    XConsoleShell_processLine(s_sh.shell, line, strlen(line));
#endif
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    XConsoleShell_flushOutput(s_sh.shell);
#endif
    XLineEdit_clear(s_sh.input);
}

/** @brief 发送按钮/回车槽。 */
static void sh_sendClicked(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    sh_submit();
}

/** @brief 开关槽：创建/销毁页面内 Shell 实例（内存只在开启期间占用）。 */
static void sh_toggleClicked(XObject* sender, XVarList* args)
{
    (void)sender;
    (void)args;
    if (!s_sh.shellOn) {
        XConsoleShellIo io;
        memset(&io, 0, sizeof(io));
        io.write = sh_shellWrite;
        s_sh.shell = XConsoleShell_create(&io);
        if (s_sh.shell) {
            s_sh.shellOn = true;
#if XCONSOLE_SHELL_COMMAND_ON
            XConsoleShell_registerStaticCommands(s_sh.shell, &XTestCommand, 1);
#endif
            sh_append("[Shell] on: help="
                      "\xe5\x91\xbd\xe4\xbb\xa4\xe5\x88\x97\xe8\xa1\xa8"
                      ", Test <name>="
                      "\xe8\xb7\x91\xe6\xb5\x8b\xe8\xaf\x95"
                      ", clear="
                      "\xe6\xb8\x85\xe5\xb1\x8f\n");
        } else {
            sh_append("[Shell] create failed\n");
        }
    } else {
        if (s_sh.shell) XClassDelete(s_sh.shell);
        s_sh.shell = NULL;
        s_sh.shellOn = false;
        sh_append("[Shell] off\n");
    }
    if (s_sh.toggle) {
        XPushButton_setText_2(s_sh.toggle,
                              s_sh.shellOn ? "\xE5\x85\xB3\xE9\x97\xAD Shell"
                                           : "\xE5\xBC\x80\xE5\x90\xAF Shell");
    }
    if (s_sh.input)
        XWidget_setEnabled((XWidget*)s_sh.input, s_sh.shellOn);
    if (s_sh.send)
        XWidget_setEnabled((XWidget*)s_sh.send, s_sh.shellOn);
}

/* ==================== 横竖屏自适应 ==================== */

void demo_page_shell_adapt(XWidget* page)
{
    /* 单列竖排天然竖屏友好：命令行最小宽（开关+输入+发送）< 400 竖屏
     * 逻辑宽，横排任何档位都放得下；输出区随余高伸展。无需翻转。 */
    (void)page;
}

/* ==================== 构建 / 自测 ==================== */

XWidget* demo_page_shell_build(XWidget* parent,
                               DemoPageStatusFn status, void* user)
{
    XScrollArea* pageScroll;

    (void)status; (void)user;
    memset(&s_sh, 0, sizeof(s_sh));

    /* [页宿主=scroll] 与 ATV61/测试菜单页同口径：堆叠铺满 scroll，
     * 内容根由 widgetResizable 接管几何。 */
    pageScroll = XScrollArea_create(parent, 0);
    if (!pageScroll) return NULL;
    XScrollArea_setWidgetResizable(pageScroll, true);
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)pageScroll, XScrollBarPolicy_AsNeeded);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)pageScroll, XScrollBarPolicy_AlwaysOff);

    s_sh.root = XWidget_create((XWidget*)pageScroll, 0);
    if (!s_sh.root) { XObject_deleteLater((XObject*)pageScroll); return NULL; }
    s_sh.pageScroll = (XWidget*)pageScroll;
    XScrollArea_setWidget(pageScroll, s_sh.root);

    /* 根纵布局：命令行 + 输出区（值成员，与 ATV61/update 页同款）。 */
    XBoxLayout_init(&s_sh.lay, XBoxLayoutDirection_TopToBottom);
    XWidget_setLayout(s_sh.root, (XLayout*)&s_sh.lay);
    XLayout_setContentsMargins((XLayout*)&s_sh.lay, 8, 8, 8, 8);
    XLayout_setSpacing((XLayout*)&s_sh.lay, 6);

    /* 命令行子布局：开关 | 输入框（| 发送；极窄档横排仍可容纳）。 */
    XBoxLayout_init(&s_sh.sendRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setContentsMargins((XLayout*)&s_sh.sendRow, 0, 0, 0, 0);
    XLayout_setSpacing((XLayout*)&s_sh.sendRow, 6);

    s_sh.toggle = XPushButton_create(s_sh.root, 0);
    if (!s_sh.toggle) goto fail;
    XPushButton_setText_2(s_sh.toggle, "\xE5\xBC\x80\xE5\x90\xAF Shell");
    XWidget_setFixedHeight((XWidget*)s_sh.toggle, SH_BTN_H);
    XObject_connect_2((XObject*)s_sh.toggle,
                      XSignal(XAbstractButton_clicked_signal),
                      sh_toggleClicked);
    XBoxLayout_addWidget(&s_sh.sendRow, (XWidget*)s_sh.toggle);

    s_sh.input = XLineEdit_create(s_sh.root, 0);
    if (s_sh.input) {
        XWidget_setFixedHeight((XWidget*)s_sh.input, SH_BTN_H);
        /* 竖屏 400 逻辑宽档：开关+输入+发送同行放得下（输入框让宽度）。 */
        XWidget_setMinimumWidth((XWidget*)s_sh.input, 120);
        XWidget_setEnabled((XWidget*)s_sh.input, false);
        XLineEdit_setPlaceholderText(s_sh.input, "help | clear");
        XObject_connect_2((XObject*)s_sh.input,
                          XSignal(XLineEdit_returnPressed_signal),
                          sh_sendClicked);
        XBoxLayout_addWidgetEx(&s_sh.sendRow, (XWidget*)s_sh.input, 1, 0);
    }
    s_sh.send = XPushButton_create(s_sh.root, 0);
    if (s_sh.send) {
        XPushButton_setText_2(s_sh.send, "\xE5\x8F\x91\xE9\x80\x81");
        XWidget_setFixedHeight((XWidget*)s_sh.send, SH_BTN_H);
        XWidget_setMinimumWidth((XWidget*)s_sh.send, 64);
        XWidget_setEnabled((XWidget*)s_sh.send, false);
        XObject_connect_2((XObject*)s_sh.send,
                          XSignal(XAbstractButton_clicked_signal),
                          sh_sendClicked);
        XBoxLayout_addWidget(&s_sh.sendRow, (XWidget*)s_sh.send);
    }

    /* 输出滚动区（伸展占满余高）。 */
    s_sh.outputScroll = XScrollArea_create(s_sh.root, 0);
    if (!s_sh.outputScroll) goto fail;
    XScrollArea_setWidgetResizable(s_sh.outputScroll, true);
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)s_sh.outputScroll, XScrollBarPolicy_AsNeeded);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_sh.outputScroll, XScrollBarPolicy_AsNeeded);
    XWidget_setMinimumHeight((XWidget*)s_sh.outputScroll, 200);
    {
        XWidget* host = XWidget_create(NULL, 0);
        XBoxLayout* hostLay;
        if (!host) goto fail;
        hostLay = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, host);
        if (!hostLay) { XObject_deleteLater((XObject*)host); goto fail; }
        XLayout_setContentsMargins((XLayout*)hostLay, 4, 4, 4, 4);
        XLabel_init(&s_sh.output, host, 0);
        XLabel_setText_2(&s_sh.output,
                         "[Shell] "
                         "\xE7\x82\xB9\xE3\x80\x8C\xE5\xBC\x80\xE5\x90\xAF"
                         " Shell\xE3\x80\x8D\xE5\x90\x8E\xE8\xBE\x93\xE5"
                         "\x85\xA5\xE5\x91\xBD\xE4\xBB\xA4"); /* 点「开启 Shell」后输入命令 */
        XBoxLayout_addWidget(hostLay, (XWidget*)&s_sh.output);
        XScrollArea_setWidget(s_sh.outputScroll, host);
    }
    /* 命令行挂根布局，输出区伸展占满余高。 */
    XBoxLayout_addLayoutEx(&s_sh.lay, (XLayout*)&s_sh.sendRow, 0);
    XBoxLayout_addWidgetEx(&s_sh.lay, (XWidget*)s_sh.outputScroll, 1, 0);
    return (XWidget*)pageScroll;

fail:
    if (s_sh.root) XObject_deleteLater((XObject*)s_sh.root);
    return NULL;
}

int demo_page_shell_autotest(XWidget* page)
{
    int fails = 0;
    if (!page) return -1;
    if (!s_sh.root || !s_sh.pageScroll) ++fails;
    if (page != s_sh.pageScroll) ++fails;
    if (!s_sh.outputScroll || !s_sh.toggle || !s_sh.input || !s_sh.send) ++fails;
    if (s_sh.shellOn) ++fails;         /* Shell 缺省关闭（按需内存）。 */
    if (s_sh.buf) ++fails;             /* 缓冲缺省未分配（按需内存）。 */

    /* [点击链路回归锁 2026-10-29] 开启 → help（write 回调+缓冲落字）→
       clear（清屏并还内存）→ 关闭（实例销毁）。全程编程式 click 直击
       信号→槽链路，非阻塞（help/clear 均为进程内即时命令）。 */
    XAbstractButton_click((XAbstractButton*)s_sh.toggle);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    if (!s_sh.shellOn) ++fails;
    if (s_sh.input) {
        XLineEdit_setText(s_sh.input, "help");
        XAbstractButton_click((XAbstractButton*)s_sh.send);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (!s_sh.buf) ++fails;        /* help 输出已入缓冲 */
    }
    if (s_sh.input) {
        XLineEdit_setText(s_sh.input, "clear");
        XAbstractButton_click((XAbstractButton*)s_sh.send);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (s_sh.buf) ++fails;         /* clear 已清屏并还内存 */
    }
    XAbstractButton_click((XAbstractButton*)s_sh.toggle);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    if (s_sh.shellOn) ++fails;         /* 关闭后实例已销毁 */
    return fails;
}

#else /* 裁剪桩 */

XWidget* demo_page_shell_build(XWidget* parent,
                               DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    return NULL;
}

int demo_page_shell_autotest(XWidget* page)
{
    (void)page;
    return -1;
}

void demo_page_shell_adapt(XWidget* page)
{
    (void)page;
}

#endif /* XCONSOLE_SHELL_ON && IO && 控件齐 */
