/**
 * @file       XTextBrowserAlignTest.c
 * @brief      XTextBrowser 与 Qt 6.8 QTextBrowser 的对齐契约测试。
 * @details    覆盖：
 *             - 构造默认值（qtextbrowser.cpp 的 QTextBrowserPrivate::init：
 *               只读浏览、撤销重做关闭、openLinks true、openExternalLinks
 *               false、导航历史空 ⇒ backward/forward 计数 0、
 *               sourceType 为 UnknownResource）；
 *             - setSource 的导航历史推进与 **相对下标历史查询**
 *               （Qt 6.8 qtextbrowser.cpp:1220-1244 的 historyTitle(i)/
 *               historyUrl(i)：i<0 后退历史、0 当前、i>0 前进历史）；
 *             - **sourceType 逐条记录 ResourceType**（扩展名推断 .md ⇒
 *               Markdown，其余 Html；setSource_2 显式类型；后退/前进恢复）
 *               与 loadResource 的绝对路径/searchPaths 文件读取；
 *             - backward/forward/clearHistory 与 available 信号状态；
 *             - setHtml 自动进入只读富文本预览（浏览器正文为富文本视图）；
 *             - openLinks/openExternalLinks/searchPaths 往返；
 *             - 父子析构。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XTextBrowser.h"
#include "XTextEdit.h"
#include "XStringList.h"
#include "XWidget.h"
#include "XClass.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XDeviceFile.h"
#include "XIODevice.h"

#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

/** @brief 取 toPlainText 堆结果的借用视图（用完 XFree_System）。 */
static char* xtb_plain(const XTextBrowser* b)
{
    XString* s = XTextEdit_toPlainText((const XTextEdit*)b);
    const char* u8;
    char* out;
    size_t n;
    if (!s) return NULL;
    u8 = XString_toUtf8(s);
    if (!u8) u8 = "";
    n = XStrlen(u8);
    out = (char*)XMalloc_System(n + 1);
    if (out) {
        if (n) XMemcpy(out, u8, n);
        out[n] = '\0';
    }
    XClassDelete((XClass*)s);
    return out;
}

XQ_TEST(XTextBrowser, defaultsAndNavigation)
{
    XTextBrowser* b = XTextBrowser_create(NULL, 0);
    XQ_REQUIRE(b != NULL);

    /* 浏览器恒只读浏览（qtextbrowser.cpp:669 QTextBrowserPrivate::init）。 */
    XQ_CHECK(XTextEdit_isReadOnly_2((const XTextEdit*)b));
    XQ_CHECK(!XTextEdit_isUndoRedoEnabled_2((const XTextEdit*)b));
    XQ_EQ_STR(XTextBrowser_source(b), "");
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Unknown);
    XQ_CHECK(XTextBrowser_openLinks(b));
    XQ_CHECK(!XTextBrowser_openExternalLinks(b));
    XQ_EQ_I64(XTextBrowser_backwardHistoryCount(b), 0);
    XQ_EQ_I64(XTextBrowser_forwardHistoryCount(b), 0);
    XQ_CHECK(!XTextBrowser_isBackwardAvailable(b));
    XQ_CHECK(!XTextBrowser_isForwardAvailable(b));

    XTextBrowser_setSource(b, "a.html");
    XQ_EQ_STR(XTextBrowser_source(b), "a.html");
    /* sourceType 记录栈顶条目资源类型（Qt：setSource 按扩展名推断，
       非 .md/.mkd/.markdown 一律 HtmlResource，qtextbrowser.cpp:277-287）。 */
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Html);
    XQ_EQ_I64(XTextBrowser_backwardHistoryCount(b), 0);

    XTextBrowser_setSource(b, "b.html");
    XQ_EQ_STR(XTextBrowser_source(b), "b.html");
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Html);
    XQ_EQ_I64(XTextBrowser_backwardHistoryCount(b), 1);
    XQ_CHECK(XTextBrowser_isBackwardAvailable(b));
    XQ_EQ_I64(XTextBrowser_forwardHistoryCount(b), 0);

    /* 相对下标：0=当前、-1=上一条（Qt historyUrl 表格语义）。 */
    XQ_EQ_STR(XTextBrowser_historyUrl(b, 0), "b.html");
    XQ_EQ_STR(XTextBrowser_historyUrl(b, -1), "a.html");
    XQ_EQ_STR(XTextBrowser_historyTitle(b, 0), "b.html");

    XTextBrowser_backward(b);
    XQ_EQ_STR(XTextBrowser_source(b), "a.html");
    XQ_EQ_I64(XTextBrowser_backwardHistoryCount(b), 0);
    XQ_EQ_I64(XTextBrowser_forwardHistoryCount(b), 1);
    XQ_CHECK(XTextBrowser_isForwardAvailable(b));

    XTextBrowser_forward(b);
    XQ_EQ_STR(XTextBrowser_source(b), "b.html");
    XQ_EQ_I64(XTextBrowser_backwardHistoryCount(b), 1);

    XTextBrowser_clearHistory(b);
    XQ_EQ_I64(XTextBrowser_backwardHistoryCount(b), 0);
    XQ_EQ_I64(XTextBrowser_forwardHistoryCount(b), 0);
    XQ_CHECK(!XTextBrowser_isBackwardAvailable(b));
    XQ_CHECK(!XTextBrowser_isForwardAvailable(b));

    XClassDelete((XClass*)b);
}

XQ_TEST(XTextBrowser, htmlPreviewAndSwitches)
{
    XTextBrowser* b = XTextBrowser_create(NULL, 0);
    XStringList* paths;
    char* plain;
    XQ_REQUIRE(b != NULL);

    /* setHtml：文档收富文本、编辑缓冲剥离标签、自动进入只读预览。 */
    XTextBrowser_setHtml(b, "<b>x</b>");
    XQ_CHECK(XTextEdit_isRichPreview((const XTextEdit*)b));
    plain = xtb_plain(b);
    XQ_REQUIRE(plain != NULL);
    XQ_EQ_STR(plain, "x");
    XFree_System(plain);
    XQ_CHECK(XTextEdit_isReadOnly_2((const XTextEdit*)b));

    /* 链接开关往返。 */
    XTextBrowser_setOpenLinks(b, false);
    XQ_CHECK(!XTextBrowser_openLinks(b));
    XTextBrowser_setOpenLinks(b, true);
    XQ_CHECK(XTextBrowser_openLinks(b));
    XTextBrowser_setOpenExternalLinks(b, true);
    XQ_CHECK(XTextBrowser_openExternalLinks(b));
    XTextBrowser_setOpenExternalLinks(b, false);
    XQ_CHECK(!XTextBrowser_openExternalLinks(b));

    /* searchPaths：set 深拷贝、get 深拷贝（调用方释放）。 */
    paths = XStringList_create();
    XQ_REQUIRE(paths != NULL);
    XStringList_push_back_utf8(paths, "/tmp");
    XTextBrowser_setSearchPaths(b, paths);
    XClassDelete((XClass*)paths);
    paths = XTextBrowser_searchPaths(b);
    XQ_REQUIRE(paths != NULL);
    XQ_EQ_SIZE(XStringList_size_base((const XContainer*)paths), 1);
    XClassDelete((XClass*)paths);

    XClassDelete((XClass*)b);
}

XQ_TEST(XTextBrowser, sourceTypeAndLoadResource)
{
    XTextBrowser* b = XTextBrowser_create(NULL, 0);
    XQ_REQUIRE(b != NULL);

    /* setSource(const QUrl&, ResourceType)：显式类型原样记录。 */
    XTextBrowser_setSource_2(b, "notes.md", XTextBrowserSourceType_Markdown);
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Markdown);
    XTextBrowser_setSource_2(b, "page.html", XTextBrowserSourceType_Html);
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Html);

    /* 扩展名推断：.md ⇒ Markdown（qtextbrowser.cpp:277-287）。 */
    XTextBrowser_setSource(b, "readme.md");
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Markdown);
    XTextBrowser_setSource(b, "page.htm");
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Html);

    /* 导航后退/前进恢复各自条目的类型。 */
    XTextBrowser_backward(b);
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Markdown);
    XTextBrowser_forward(b);
    XQ_EQ_I64(XTextBrowser_sourceType(b), XTextBrowserSourceType_Html);

    /* loadResource：不可读名称返回 NULL（等价无效 QVariant）。 */
    XQ_CHECK(XTextBrowser_loadResource(
                 b, XTextBrowserSourceType_Html,
                 "/tmp/xq_align_textbrowser_missing.html") == NULL);

    XClassDelete((XClass*)b);
}

#if XDEVICE_ON && XDEVICE_FILE_ON
/** @brief 写一个临时文本文件（失败返回 false）。 */
static bool xtb_writeFile(const char* path, const char* text)
{
    XDeviceOpenOptions opts;
    XString* target;
    XFd fd;
    int err = 0;
    bool ok = false;
    if (!path || !text) return false;
    target = XString_create_utf8(path);
    if (!target) return false;
    XMemset(&opts, 0, sizeof(opts));
    opts.m_openMode =
        (uint32_t)(XIODevice_WriteOnly | XIODevice_Truncate);
    opts.m_target = target;
    fd = XDevice_open(XDeviceType_File, &opts, &err);
    XClassDelete((XClass*)target);
    if (fd == XFD_INVALID) return false;
    ok = XDevice_write(fd, text, (int64_t)XStrlen(text)) ==
         (int64_t)XStrlen(text);
    XDevice_close(fd);
    return ok;
}

XQ_TEST(XTextBrowser, loadResourceReadsFile)
{
    static const char* const kPath = "/tmp/xq_align_textbrowser_res.html";
    static const char* const kBody = "<b>res</b>";
    XTextBrowser* b = XTextBrowser_create(NULL, 0);
    XVariant* res;
    XQ_REQUIRE(b != NULL);

    if (!xtb_writeFile(kPath, kBody)) {
        /* 无写权限环境：跳过读文件断言（记录用 XQ_CHECK 明确是跳过原因）。 */
        XQ_CHECK_MSG(false, "无法创建临时文件 %s（环境限制）", kPath);
        XClassDelete((XClass*)b);
        return;
    }

    /* 绝对路径直接读取（Qt findFile 的 isAbsolute 分支）。 */
    res = XTextBrowser_loadResource(b, XTextBrowserSourceType_Html, kPath);
    XQ_REQUIRE(res != NULL);
    {
        const XString* text = XString_fromVariant_const(res);
        XQ_REQUIRE(text != NULL);
        XQ_EQ_STR(XString_toUtf8(text), kBody);
    }
    XClassDelete((XClass*)res);

    /* searchPaths 相对名解析（qtextbrowser.cpp:163-169：拼 path+"/"+name）。 */
    {
        XStringList* paths = XStringList_create();
        XQ_REQUIRE(paths != NULL);
        XStringList_push_back_utf8(paths, "/tmp");
        XTextBrowser_setSearchPaths(b, paths);
        XClassDelete((XClass*)paths);
    }
    res = XTextBrowser_loadResource(b, XTextBrowserSourceType_Html,
                                    "xq_align_textbrowser_res.html");
    XQ_REQUIRE(res != NULL);
    {
        const XString* text = XString_fromVariant_const(res);
        XQ_REQUIRE(text != NULL);
        XQ_EQ_STR(XString_toUtf8(text), kBody);
    }
    XClassDelete((XClass*)res);

    /* setSource 读取成功 ⇒ 正文按 HTML 呈现（浏览器只读富文本预览）。 */
    XTextBrowser_setSource(b, kPath);
    XQ_CHECK(XTextEdit_isRichPreview((const XTextEdit*)b));
    {
        char* plain = xtb_plain(b);
        XQ_REQUIRE(plain != NULL);
        XQ_EQ_STR(plain, "res");
        XFree_System(plain);
    }

    {
        XString* target = XString_create_utf8(kPath);
        XQ_REQUIRE(target != NULL);
        (void)XDeviceFile_removePermanent(target);
        XClassDelete((XClass*)target);
    }
    XClassDelete((XClass*)b);
}
#endif /* XDEVICE_ON && XDEVICE_FILE_ON */

XQ_TEST(XTextBrowser, parentChild)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XTextBrowser* b;
    XQ_REQUIRE(parent != NULL);
    b = XTextBrowser_create(parent, 0);
    XQ_REQUIRE(b != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)b) == parent);
    XQ_CHECK(XTextEdit_isReadOnly_2((const XTextEdit*)b));
    XClassDelete((XClass*)parent);
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON */
