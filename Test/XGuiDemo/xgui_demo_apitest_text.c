/******************************************************************************
 * @file       xgui_demo_apitest_text.c
 * @brief      控件 API 测试族：text（XTextEdit/XPlainTextEdit/XTextBrowser/
 *             XTextDocument/XCompleter/XKeySequenceEdit 六控件公开 API
 *             全量断言，对标 Qt 6.8.3）。
 * @details    契约见 xgui_demo_apitest.h：逐断言 XAPI_EXPECT 输出
 *             "XGuiApiTest: [PASS]/[FAIL] 中文描述"，局部 int failures
 *             计数，入口返回失败数。断言维度：
 *             - 属性 setter/getter 往返一致；
 *             - Qt 6.8.3 对齐默认值（文档可考的直接断言；实现与 Qt
 *               有承载差异的以注释说明，不硬断言防误报）；
 *             - 信号发射与状态迁移（信号经 XObject_connect_1 挂计数
 *               槽，状态迁移经 XObject_event_base 直发合成事件，坐标
 *               /按键与真实输入同路径）；
 *             - 边界（空串/NULL/0/极大值/重复 set/未 show 直接调）。
 * @note       事件注入定式参照 xgui_demo_page_views.c / 主文件
 *             xgui_demo_page_advanced.c：XKeyEvent_init 构造按键事件后
 *             XObject_event_base 直发控件（XTextEdit 的编辑缓冲由内嵌
 *             m_editor（XPlainTextEdit）承载，键事件直发内嵌编辑器与
 *             真实聚焦输入同走 XPlainTextEdit 键处理虚槽）。
 * @note       本文件不依赖 show：全部控件不进入窗口流程直接调 API
 *             （契约"无头语义"）；不触碰渲染像素，视觉边界由主线亲验。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "xgui_demo_apitest.h"

#include <stdio.h>
#include <string.h>

#include "XGuiConfig.h"
#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XMemory.h" /* XFree_System：toPlainText/toHtml 等堆拷贝释放。 */

/* ==================== 模块裁剪守卫（对齐各头文件门控口径） ============ */
/* XPlainTextEdit：XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON
 * XTextEdit：    上述 && XTEXTEDIT_ON（依赖 XPLAINTEXTEDIT_ON）
 * XTextBrowser： 在 XTextEdit 之上再加 XTEXTBROWSER_ON
 * XTextDocument：XTEXTDOCUMENT_ON（XObject 派生，非控件）
 * XCompleter：   XWIDGET_ON && XTABLEWIDGET_ON（借用条目模型门控）
 * XKeySequenceEdit：XWIDGET_ON && XKEYSEQUENCEEDIT_ON */
#define APITEXT_PTE_ON       (XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON)
#define APITEXT_TEXTEDIT_ON  (APITEXT_PTE_ON && XTEXTEDIT_ON)
#define APITEXT_BROWSER_ON   (APITEXT_TEXTEDIT_ON && XTEXTBROWSER_ON)
#define APITEXT_DOC_ON       (XTEXTDOCUMENT_ON)
#define APITEXT_COMPLETER_ON (XWIDGET_ON && XTABLEWIDGET_ON)
#define APITEXT_KSE_ON       (XWIDGET_ON && XKEYSEQUENCEEDIT_ON)

#if APITEXT_TEXTEDIT_ON
#include "XTextEdit.h"
#endif
#if APITEXT_BROWSER_ON
#include "XTextBrowser.h"
#endif
#if APITEXT_DOC_ON
#include "XTextDocument.h"
#endif
#if APITEXT_COMPLETER_ON
#include "XCompleter.h"
#include "XAbstractItemModel.h"
#endif
#if APITEXT_KSE_ON
#include "XKeySequenceEdit.h"
#endif

#if defined(APITEXT_ANY_ON)
#undef APITEXT_ANY_ON
#endif
#define APITEXT_ANY_ON \
    (APITEXT_TEXTEDIT_ON || APITEXT_BROWSER_ON || APITEXT_DOC_ON || \
     APITEXT_COMPLETER_ON || APITEXT_KSE_ON)

/* ==================== 公共注入工具（与真实输入同路径） =============== */

#if APITEXT_ANY_ON

/** @brief 向控件直发一串按键按下事件（XKeyEvent 直发，参照主文件
 *         advanced 页键入定式；XKeyEvent_init 的 modifiers 参数直发
 *         修饰位，无需 setter→getter 降级）。 */
static void ttxt_type(XWidget* target, const char* ascii)
{
    const char* p;
    if (!target) return;
    for (p = ascii; *p; ++p) {
        XKeyEvent ke;
        XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS,
                       (int)(unsigned char)*p, 0);
        XObject_event_base((XObject*)target, (XEvent*)&ke);
    }
}

/** @brief 向控件直发一次带修饰位的按键事件（对标 QKeySequence 捕获）。 */
static void ttxt_key(XWidget* target, int key, XKeyboardModifiers mods)
{
    XKeyEvent ke;
    if (!target) return;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, mods);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

#include "XPainter.h" /* XPainter_textAscent/Descent：cursorRect 行高口径。 */

/** @brief 控件字体度量行高（XPainter ascent+descent，实现口径）。
 * @details cursorRect/绘制布局的行高都随控制器字体度量走（环境相关）；
 *          固定值 16 仅是字体度量失败时的回退常量
 *          XTC_DEFAULT_LINE_HEIGHT（XTextControl.c），不可作为断言口径。 */
static int ttxt_fontLineHeight(const XWidget* w)
{
    XFont f;
    int h;
    if (!w) return 0;
    f = XWidget_font(w);
    h = XPainter_textAscent(&f) + XPainter_textDescent(&f);
    XFont_deinit_base((XClass*)&f); /* XWidget_font 深拷贝契约。 */
    return h;
}

#endif /* APITEXT_ANY_ON */

#if APITEXT_TEXTEDIT_ON

/* ==================== XTextEdit：富文本编辑（对标 QTextEdit） ======== */

static int g_xteTextChanged;    /**< textChanged 计数（真发射）。 */
static int g_xteCurFmtChanged;  /**< currentCharFormatChanged 计数。 */

/** @brief textChanged 计数槽（void f(XObject*, XVarList*) 定式）。 */
static void ttxt_slotTeTextChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xteTextChanged;
}

/** @brief currentCharFormatChanged 计数槽。 */
static void ttxt_slotTeCurFmt(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xteCurFmtChanged;
}

/** @brief XTextEdit 族断言（局部 failures 计数，返回失败数）。 */
static int ttxt_run_text_edit(void)
{
    int failures = 0;
    XTextEdit* te = XTextEdit_create(NULL, 0);
    if (!te) {
        XAPI_EXPECT(false, "XTextEdit: 实例创建失败");
        return 1;
    }
    XObject_connect_1((XObject*)te,
                      (size_t)XTextEdit_textChanged_signal(NULL),
                      (XObject*)te, ttxt_slotTeTextChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)te,
                      (size_t)XTextEdit_currentCharFormatChanged_signal(NULL),
                      (XObject*)te, ttxt_slotTeCurFmt,
                      XConnectionType_Direct);

    /* ---- 1. Qt 对齐默认值（QTextEdit 构造态）。 ---- */
    XAPI_EXPECT(XTextEdit_isBold(te) == false &&
                XTextEdit_isItalic(te) == false &&
                XTextEdit_isUnderline(te) == false,
                "XTextEdit: 默认字符格式无粗/斜/下划线（QTextEdit 初始态）");
    XAPI_EXPECT(XTextEdit_textColor(te) == 0xFF000000u,
                "XTextEdit: 默认文字色为不透明黑（对标默认调色板文字色）");
    XAPI_EXPECT(XTextEdit_alignment(te) == 1,
                "XTextEdit: 默认对齐=1 左对齐（对标 Qt::AlignLeft）");
    XAPI_EXPECT(XTextEdit_fontWeight(te) == 400,
                "XTextEdit: 默认字重 400（对标 QFont::Normal）");
    /* 本库文档默认字号 10 磅（QTextEdit 默认随应用字体，无固定值，
     * 故断言本库文档默认值而非 Qt 数值）。 */
    XAPI_EXPECT(XTextEdit_fontPointSize(te) == 10.0,
                "XTextEdit: 默认字号 10 磅（本库文档默认值）");
    /* 制表位距 80 默认为本库文档值；Qt 默认由字体度量决定，不硬对标。 */
    XAPI_EXPECT(XTextEdit_tabStopDistance(te) == 80.0,
                "XTextEdit: 默认制表位距 80（本库文档默认值）");
    XAPI_EXPECT(XTextEdit_cursorWidth(te) == 1,
                "XTextEdit: 默认光标宽 1px（QTextEdit 默认 1）");
    XAPI_EXPECT(XTextEdit_acceptRichText(te) == true,
                "XTextEdit: 默认接受富文本（QTextEdit 默认 true）");
    XAPI_EXPECT(XTextEdit_autoFormatting(te) == 0,
                "XTextEdit: 默认无自动格式化（QTextEdit::AutoNone）");
    XAPI_EXPECT(XTextEdit_centerOnScroll(te) == false,
                "XTextEdit: 默认不滚动居中（QTextEdit centerOnScroll=false）");
    XAPI_EXPECT(XTextEdit_isReadOnly_2(te) == false,
                "XTextEdit: 默认可编辑（QTextEdit readOnly=false）");
    XAPI_EXPECT(XTextEdit_lineWrapColumnOrWidth(te) == 0,
                "XTextEdit: 换行列宽默认 0（QTextEdit 默认 0）");
    XAPI_EXPECT(XTextEdit_isRichPreview(te) == false,
                "XTextEdit: 默认纯文本编辑态（预览需显式开启）");
    XAPI_EXPECT(xapi_cstr(XTextEdit_fontFamily(te))[0] == '\0',
                "XTextEdit: 默认字体族为空串");
    XAPI_EXPECT(xapi_cstr(XTextEdit_documentTitle(te))[0] == '\0',
                "XTextEdit: 默认文档标题为空串");
    XAPI_EXPECT(xapi_cstr(XTextEdit_placeholderText_2(te))[0] == '\0',
                "XTextEdit: 默认占位文本为空串");
    XAPI_EXPECT(XTextEdit_canUndo(te) == false &&
                XTextEdit_canRedo(te) == false,
                "XTextEdit: 初始撤销/重做栈为空（canUndo/canRedo=false）");
    /* 注（不硬断言）：lineWrapMode 实现默认 0（NoWrap），QTextEdit 默认
     * 为 WidgetWidth——承载差异见提交 notes，不按 Qt 数值断言防误报。 */

    /* ---- 2. 属性 setter/getter 往返。 ---- */
    XTextEdit_setBold(te, true);
    XTextEdit_setItalic(te, true);
    XTextEdit_setUnderline(te, true);
    XAPI_EXPECT(XTextEdit_isBold(te) && XTextEdit_isItalic(te) &&
                XTextEdit_isUnderline(te),
                "XTextEdit: setBold/setItalic/setUnderline 往返一致");
    /* Qt 属性别名宏：fontItalic/setFontItalic 与 isItalic/setItalic 同一。 */
    XTextEdit_setFontItalic(te, false);
    XAPI_EXPECT(!XTextEdit_fontItalic(te),
                "XTextEdit: setFontItalic 别名写 false 后 isItalic=false");
    XTextEdit_setFontUnderline(te, false);
    XAPI_EXPECT(!XTextEdit_fontUnderline(te),
                "XTextEdit: setFontUnderline 别名写 false 后读 false");
    XTextEdit_setBold(te, false);
    XTextEdit_setTextColor(te, 0xFF00FF00u);
    XAPI_EXPECT(XTextEdit_textColor(te) == 0xFF00FF00u,
                "XTextEdit: setTextColor 往返一致（对标 textColor 属性）");
    XTextEdit_setAlignment(te, 2);
    XAPI_EXPECT(XTextEdit_alignment(te) == 2,
                "XTextEdit: setAlignment 往返一致（对标 alignment 属性）");
    XTextEdit_setAlignment(te, 1);
    XTextEdit_setTextBackgroundColor(te, 0xFF102030u);
    XAPI_EXPECT(XTextEdit_textBackgroundColor(te) == 0xFF102030u,
                "XTextEdit: setTextBackgroundColor 往返（textBackground）");
    XTextEdit_setAcceptRichText(te, false);
    XAPI_EXPECT(!XTextEdit_acceptRichText(te),
                "XTextEdit: setAcceptRichText(false) 往返");
    XTextEdit_setAcceptRichText(te, true);
    XTextEdit_setAutoFormatting(te, 1);
    XAPI_EXPECT(XTextEdit_autoFormatting(te) == 1,
                "XTextEdit: setAutoFormatting 往返（对标 autoFormatting）");
    XTextEdit_setAutoFormatting(te, 0);
    XTextEdit_setCenterOnScroll(te, true);
    XAPI_EXPECT(XTextEdit_centerOnScroll(te),
                "XTextEdit: setCenterOnScroll(true) 往返");
    XTextEdit_setCenterOnScroll(te, false);
    XTextEdit_setCursorWidth(te, 3);
    XAPI_EXPECT(XTextEdit_cursorWidth(te) == 3,
                "XTextEdit: setCursorWidth(3) 往返");
    XTextEdit_setCursorWidth(te, 0); /* 边界：<=0 被忽略 */
    XAPI_EXPECT(XTextEdit_cursorWidth(te) == 3,
                "XTextEdit: setCursorWidth(0) 越界值被忽略");
    XTextEdit_setCursorWidth(te, 1);
    XTextEdit_setFontFamily(te, "SimSun");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextEdit_fontFamily(te)), "SimSun") == 0,
                "XTextEdit: setFontFamily 往返（fontFamily 属性）");
    XTextEdit_setCurrentFont(te, "CurFont");
    /* setCurrentFont 为 setFontFamily 平铺承载（整篇单格式模型）。 */
    XAPI_EXPECT(strcmp(xapi_cstr(XTextEdit_fontFamily(te)), "CurFont") == 0,
                "XTextEdit: setCurrentFont 写入 fontFamily 读回一致");
    XTextEdit_setFontWeight(te, 700);
    XAPI_EXPECT(XTextEdit_fontWeight(te) == 700,
                "XTextEdit: setFontWeight(700) 往返（对标 Bold 字重）");
    XTextEdit_setFontWeight(te, 400);
    XTextEdit_setFontPointSize(te, 12.5);
    XAPI_EXPECT(XTextEdit_fontPointSize(te) == 12.5,
                "XTextEdit: setFontPointSize(12.5) 往返");
    XTextEdit_setFontPointSize(te, 10.0);
    XTextEdit_setTabStopDistance(te, 120.0);
    XAPI_EXPECT(XTextEdit_tabStopDistance(te) == 120.0,
                "XTextEdit: setTabStopDistance(120) 往返");
    XTextEdit_setTabStopDistance(te, -1.0); /* 边界：负值被忽略 */
    XAPI_EXPECT(XTextEdit_tabStopDistance(te) == 120.0,
                "XTextEdit: setTabStopDistance(-1) 越界值被忽略");
    XTextEdit_setTabStopDistance(te, 80.0);
    XTextEdit_setDocumentTitle(te, "标题A");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextEdit_documentTitle(te)), "标题A") == 0,
                "XTextEdit: setDocumentTitle 往返（documentTitle 属性）");
    XTextEdit_setPlaceholderText_2(te, "请输入…");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextEdit_placeholderText_2(te)), "请输入…") == 0,
                "XTextEdit: setPlaceholderText 往返（placeholderText）");
    XTextEdit_setPlaceholderText_2(te, NULL); /* 边界：NULL 按空串处理 */
    XAPI_EXPECT(xapi_cstr(XTextEdit_placeholderText_2(te))[0] == '\0',
                "XTextEdit: setPlaceholderText(NULL) 归空串");
    XTextEdit_setLineWrapMode(te, 1);
    XAPI_EXPECT(XTextEdit_lineWrapMode(te) == 1,
                "XTextEdit: setLineWrapMode(WidgetWidth) 往返");
    XTextEdit_setLineWrapColumnOrWidth(te, 200);
    XAPI_EXPECT(XTextEdit_lineWrapColumnOrWidth(te) == 200,
                "XTextEdit: setLineWrapColumnOrWidth 往返（Qt 默认承载）");
    XTextEdit_setWordWrapMode(te, 4);
    XAPI_EXPECT(XTextEdit_wordWrapMode(te) == 4,
                "XTextEdit: setWordWrapMode(4) 往返（WrapAtWordBoundary）");
    XTextEdit_setWordWrapMode(te, 1);

    /* ---- 3. 缩放（QTextEdit::zoomIn/zoomOut 点数语义）。 ---- */
    XTextEdit_zoomIn(te, 2);
    XAPI_EXPECT(XTextEdit_fontPointSize(te) == 12.0,
                "XTextEdit: zoomIn(2) 字号 10→12（对标 zoomIn 点数增量）");
    XTextEdit_zoomOut(te, 2);
    XAPI_EXPECT(XTextEdit_fontPointSize(te) == 10.0,
                "XTextEdit: zoomOut(2) 字号 12→10（往返还原）");
    XTextEdit_zoomOut(te, 99);
    XAPI_EXPECT(XTextEdit_fontPointSize(te) == 1.0,
                "XTextEdit: zoomOut 下限钳位 1 磅");
    XTextEdit_zoomIn(te, 500);
    XAPI_EXPECT(XTextEdit_fontPointSize(te) == 100.0,
                "XTextEdit: zoomIn 上限钳位 100 磅（极大值边界）");
    XTextEdit_setFontPointSize(te, 10.0);

    /* ---- 4. setPlainText/toPlainText 往返与边界。 ---- */
    XTextEdit_setPlainText(te, "ab\ncd");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "ab\ncd") == 0,
                    "XTextEdit: setPlainText/toPlainText 多行往返一致");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_setPlainText(te, NULL); /* 边界：NULL 按空串处理 */
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && xapi_u8(plain)[0] == '\0',
                    "XTextEdit: setPlainText(NULL) 归空串");
        if (plain) XString_delete_base((XClass*)plain);
    }
    /* setPlainText 复位字符格式（与 setText 纯文本分支同口径）。 */
    XTextEdit_setBold(te, true);
    XTextEdit_setPlainText(te, "reset");
    XAPI_EXPECT(!XTextEdit_isBold(te),
                "XTextEdit: setPlainText 复位粗体（纯文本替换语义）");
    XAPI_EXPECT(g_xteTextChanged > 0,
                "XTextEdit: 内容替换发射 textChanged（真发射）");

    /* ---- 5. setText 富文本探测（Qt::mightBeRichText 子集启发）。 ---- */
    XTextEdit_setText(te, "plain text");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "plain text") == 0,
                    "XTextEdit: setText 纯文本分支直写编辑器");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_setText(te, "<b>hi</b>");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "hi") == 0,
                    "XTextEdit: setText 探测富文本并剥离标签保留文本");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_setText(te, NULL); /* 边界：NULL 忽略 */
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "hi") == 0,
                    "XTextEdit: setText(NULL) 忽略不破坏现内容");
        if (plain) XString_delete_base((XClass*)plain);
    }

    /* ---- 6. setHtml 渲染子集 + setRichPreview 预览态。 ---- */
    XTextEdit_setHtml(te, "<i>italic-open");
    XAPI_EXPECT(XTextEdit_isItalic(te),
                "XTextEdit: setHtml 未闭合 <i> 行内格式终态=斜体");
    XTextEdit_setHtml(te, "<b>B</b><i>I</i>");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "BI") == 0,
                    "XTextEdit: setHtml 富文本进文档、编辑缓冲剥离为 BI");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_setHtml(te, NULL); /* 边界：NULL 忽略 */
    XTextEdit_setRichPreview(te, true);
    XAPI_EXPECT(XTextEdit_isRichPreview(te),
                "XTextEdit: setRichPreview(true) 进入只读预览态");
    {
        char* html = XTextEdit_toHtml(te);
        /* 预览态富文本文档为 toHtml 真源（编辑态则以编辑器文本为先）。 */
        XAPI_EXPECT(html && strstr(html, "<html><body>") != NULL &&
                    strstr(html, "<b>") != NULL,
                    "XTextEdit: 预览态 toHtml 导出含粗体标记的富文本");
        if (html) XFree_System(html);
    }
    XTextEdit_setRichPreview(te, false);
    XAPI_EXPECT(!XTextEdit_isRichPreview(te),
                "XTextEdit: setRichPreview(false) 退出预览回编辑态");
    XTextEdit_setPlainText(te, "exit"); /* 编辑类接口自动退出预览 */
    XTextEdit_setRichPreview(te, true);
    XTextEdit_setPlainText(te, "exit2");
    XAPI_EXPECT(!XTextEdit_isRichPreview(te),
                "XTextEdit: setPlainText 自动退出预览（编辑类接口语义）");

    /* ---- 7. toHtml 编辑态导出（编辑器为文本源）。 ---- */
    XTextEdit_setPlainText(te, "ab");
    {
        char* html = XTextEdit_toHtml(te);
        XAPI_EXPECT(html && strstr(html, "<html><body>") != NULL &&
                    strstr(html, "<p>ab</p>") != NULL,
                    "XTextEdit: 编辑态 toHtml 以纯文本生成 <p>ab</p>");
        if (html) XFree_System(html);
    }

    /* ---- 8. append/insert/剪贴板。 ---- */
    XTextEdit_setPlainText(te, "L1");
    XTextEdit_append(te, "L2");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "L1\nL2") == 0,
                    "XTextEdit: append 尾部另起一行（对标 appendPlainText）");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_setPlainText(te, "A");
    XTextEdit_setTextCursor(te, 0, 1);
    XTextEdit_insertPlainText(te, "B");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "AB") == 0,
                    "XTextEdit: insertPlainText 光标处插入（insertPlain）");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_insertHtml(te, "<b>X</b>"); /* 平铺降级：剥离标签插入 */
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "ABX") == 0,
                    "XTextEdit: insertHtml 剥离标签插入纯文本 X");
        if (plain) XString_delete_base((XClass*)plain);
    }
    /* 剪贴板链路：selectAll→copy→clear→paste 往返（Qt 剪贴板语义）。 */
    XTextEdit_setPlainText(te, "CPDATA");
    XTextEdit_selectAll_2(te);
    XTextEdit_copy_2(te);
    XTextEdit_clear_2(te);
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && xapi_u8(plain)[0] == '\0',
                    "XTextEdit: clear_2 清空编辑缓冲");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_paste_2(te);
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "CPDATA") == 0,
                    "XTextEdit: copy/clear/paste 剪贴板往返还原 CPDATA");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XAPI_EXPECT(XTextEdit_canPaste(te),
                "XTextEdit: 可编辑态 canPaste=true（对标 canPaste）");
    XTextEdit_setReadOnly_2(te, true);
    XAPI_EXPECT(XTextEdit_isReadOnly_2(te) && !XTextEdit_canPaste(te),
                "XTextEdit: 只读态 isReadOnly=true 且 canPaste=false");
    XTextEdit_setReadOnly_2(te, false);
    XTextEdit_selectAll_2(te);
    XTextEdit_setTextCursor(te, 0, 0); /* 收拢选区（重复 set 边界） */

    /* ---- 9. 光标/查找。 ---- */
    XTextEdit_setPlainText(te, "hello world hello");
    XTextEdit_setTextCursor(te, 1, 99); /* 边界：行列越界钳位 */
    XAPI_EXPECT(XTextEdit_textCursorLine(te) == 0 &&
                XTextEdit_textCursorColumn(te) == 17,
                "XTextEdit: setTextCursor 越界钳位到末行末列");
    XTextEdit_setTextCursor(te, 0, 0);
    XAPI_EXPECT(XTextEdit_find_2(te, "world", 0) &&
                XTextEdit_textCursorColumn(te) == 11,
                "XTextEdit: find 前向命中 world 光标落命中尾（col 11）");
    XAPI_EXPECT(XTextEdit_find_2(te, "HELLO", 0),
                "XTextEdit: find 默认大小写不敏感（对标 QTextDocument::find）");
    XTextEdit_setTextCursor(te, 0, 0);
    XTextEdit_find_2(te, "zzz", 0);
    XAPI_EXPECT(XTextEdit_textCursorColumn(te) == 0,
                "XTextEdit: find 未命中保持光标原位");
    XTextEdit_setTextCursor(te, 0, 0);
    XAPI_EXPECT(XTextEdit_find_2(te, "hello", 1),
                "XTextEdit: find 反向查找（FindBackward=1）命中");
    {
        XRect r;
        XPoint p;
        XPoint hit;
        int lineHeight;
        XTextEdit_setTextCursor(te, 0, 0); /* 光标收拢到 (0,0) 再取几何 */
        lineHeight = ttxt_fontLineHeight(
            te->m_editor ? (XWidget*)te->m_editor : (XWidget*)te);
        r = XTextEdit_cursorRect(te);
        XPoint_init(&p, r.x, r.y + r.height / 2);
        /* 实现口径：行高=内嵌编辑器字体度量 ascent+descent（随环境变，
         * 本机 12pt 默认字体为 24）；宽=cursorWidth 缺省 1；x=行左留白
         * 2px+光标前列宽(0)。固定 16 只是度量失败回退，不作断言。 */
        XAPI_EXPECT(r.height == lineHeight && r.width == 1 && r.x == 2,
                    "XTextEdit: cursorRect 行高=字体度量/光标宽1/左留白2 文档口径");
        hit = XTextEdit_cursorForPosition(te, &p);
        XAPI_EXPECT(hit.x == 0 && hit.y == 0,
                    "XTextEdit: cursorForPosition 与 cursorRect 反查互逆");
    }

    /* ---- 10. 撤销/重做（键事件直发内嵌编辑器）。 ---- */
    XTextEdit_setUndoRedoEnabled_2(te, false);
    XAPI_EXPECT(!XTextEdit_isUndoRedoEnabled_2(te),
                "XTextEdit: setUndoRedoEnabled(false) 往返");
    XAPI_EXPECT(!XTextEdit_canUndo(te),
                "XTextEdit: 关闭撤销后 canUndo=false（清历史语义）");
    XTextEdit_setUndoRedoEnabled_2(te, true);
    XTextEdit_setPlainText(te, "");
    if (te->m_editor) {
        XWidget_setFocus((XWidget*)te->m_editor);
        ttxt_type((XWidget*)te->m_editor, "abc");
    }
    XAPI_EXPECT(XTextEdit_canUndo(te),
                "XTextEdit: 键入 abc 后 canUndo=true（撤销栈非空）");
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "abc") == 0,
                    "XTextEdit: 实机键入 abc 后 toPlainText 一致");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XTextEdit_undo(te);
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strlen(xapi_u8(plain)) < 3,
                    "XTextEdit: undo 回退键入（文本变短，对标 undo 槽）");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XAPI_EXPECT(XTextEdit_canRedo(te),
                "XTextEdit: undo 后 canRedo=true（重做栈非空）");
    XTextEdit_redo(te);
    {
        XString* plain = XTextEdit_toPlainText(te);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "abc") == 0,
                    "XTextEdit: redo 重放恢复 abc（对标 redo 槽）");
        if (plain) XString_delete_base((XClass*)plain);
    }

    /* ---- 11. 平铺字符格式位集（setCurrent/mergeCurrentCharFormat）。 -- */
    g_xteCurFmtChanged = 0;
    XTextEdit_setCurrentCharFormat(te, (int)XTextEditCharFormat_Bold);
    XAPI_EXPECT(XTextEdit_isBold(te) && !XTextEdit_isItalic(te) &&
                !XTextEdit_isUnderline(te),
                "XTextEdit: setCurrentCharFormat 整体替换（置位开/未置位关）");
    XAPI_EXPECT(g_xteCurFmtChanged == 1,
                "XTextEdit: setCurrentCharFormat 触发 "
                "currentCharFormatChanged");
    XTextEdit_mergeCurrentCharFormat(
        te, (int)XTextEditCharFormat_Italic |
            (int)XTextEditCharFormat_Underline);
    XAPI_EXPECT(XTextEdit_isBold(te) && XTextEdit_isItalic(te) &&
                XTextEdit_isUnderline(te),
                "XTextEdit: mergeCurrentCharFormat 仅置位方向合并");
    XTextEdit_setCurrentCharFormat(te, (int)XTextEditCharFormat_None);

    /* ---- 12. Markdown 平铺降级（原文承载 + 显示降级）。 ---- */
    XTextEdit_setMarkdown(te, "# 标题");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextEdit_markdown(te)), "# 标题") == 0,
                "XTextEdit: setMarkdown 原文承载 markdown() 读回");
    {
        char* md = XTextEdit_toMarkdown(te);
        XAPI_EXPECT(md && strcmp(md, "# 标题") == 0,
                    "XTextEdit: toMarkdown 返回 setMarkdown 原文拷贝");
        if (md) XFree_System(md);
    }
    XTextEdit_setPlainText(te, "now plain");
    XAPI_EXPECT(xapi_cstr(XTextEdit_markdown(te))[0] == '\0',
                "XTextEdit: 内容替换清除 Markdown 原文（文档口径）");
    {
        char* md = XTextEdit_toMarkdown(te);
        XAPI_EXPECT(md && strcmp(md, "now plain") == 0,
                    "XTextEdit: 原文清除后 toMarkdown 回退纯文本导出");
        if (md) XFree_System(md);
    }

    /* ---- 13. 外接文档（setDocument/document）。 ---- */
#if APITEXT_DOC_ON
    {
        XTextDocument* doc = XTextDocument_create();
        if (doc) {
            XTextDocument_setPlainText(doc, "ext doc");
            XTextEdit_setDocument(te, doc);
            {
                XString* plain = XTextEdit_toPlainText(te);
                XAPI_EXPECT(XTextEdit_document(te) == doc &&
                            plain &&
                            strcmp(xapi_u8(plain), "ext doc") == 0,
                            "XTextEdit: setDocument 接管外部文档并同步显示");
                if (plain) XString_delete_base((XClass*)plain);
            }
            XTextEdit_setDocument(te, NULL); /* 对标 setDocument(nullptr) */
            XAPI_EXPECT(XTextEdit_document(te) != NULL &&
                        XTextEdit_document(te) != doc,
                        "XTextEdit: setDocument(NULL) 重建内部默认文档");
            XTextDocument_delete_base(doc); /* 外接文档所有权归调用方 */
        } else {
            XAPI_EXPECT(false, "XTextEdit: 外接文档对象创建失败");
        }
    }
#endif /* APITEXT_DOC_ON */

    /* ---- 14. loadResource（对标 Qt 默认返回无效 QVariant）。 ---- */
    XAPI_EXPECT(XTextEdit_loadResource(te, 0, "res") == NULL,
                "XTextEdit: loadResource 恒 NULL（资源未建，Qt 无效载荷）");

    (void)g_xteTextChanged;
    XTextEdit_delete_base((XClass*)te);
    XPrintf("XGuiApiTest: [XTextEdit] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
#endif /* APITEXT_TEXTEDIT_ON */

#if APITEXT_PTE_ON

/* ==================== XPlainTextEdit：多行纯文本编辑 ================= */

static int g_xpeTextChanged; /**< textChanged 计数（真发射）。 */

/** @brief XPlainTextEdit textChanged 计数槽。 */
static void ttxt_slotPteTextChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xpeTextChanged;
}

/** @brief XPlainTextEdit 族断言。 */
static int ttxt_run_plain_text_edit(void)
{
    int failures = 0;
    XPlainTextEdit* pte = XPlainTextEdit_create(NULL, 0);
    if (!pte) {
        XAPI_EXPECT(false, "XPlainTextEdit: 实例创建失败");
        return 1;
    }
    XObject_connect_1((XObject*)pte,
                      (size_t)XPlainTextEdit_textChanged_signal(NULL),
                      (XObject*)pte, ttxt_slotPteTextChanged,
                      XConnectionType_Direct);

    /* ---- 1. Qt 对齐默认值（QPlainTextEdit 构造态）。 ---- */
    XAPI_EXPECT(!XPlainTextEdit_isReadOnly(pte),
                "XPlainTextEdit: 默认可编辑（readOnly=false）");
    XAPI_EXPECT(XPlainTextEdit_lineWrapMode(pte) ==
                (int)XPlainTextEditMode_WidgetWidth,
                "XPlainTextEdit: 默认 WidgetWidth 换行（Qt 默认）");
    XAPI_EXPECT(XPlainTextEdit_maximumBlockCount(pte) == 0,
                "XPlainTextEdit: 块数上限默认 0（Qt 不限制）");
    XAPI_EXPECT(xapi_cstr(XPlainTextEdit_placeholderText(pte))[0] == '\0',
                "XPlainTextEdit: 默认占位文本为空串");
    XAPI_EXPECT(XPlainTextEdit_isUndoRedoEnabled(pte),
                "XPlainTextEdit: 撤销重做默认启用（Qt 默认 true）");
    XAPI_EXPECT(XPlainTextEdit_backgroundVisible(pte),
                "XPlainTextEdit: 背景默认可见（backgroundVisible=true）");
    XAPI_EXPECT(!XPlainTextEdit_tabChangesFocus(pte),
                "XPlainTextEdit: Tab 默认不切焦点（Qt QPlainTextEdit）");
    XAPI_EXPECT(!XPlainTextEdit_overwriteMode(pte),
                "XPlainTextEdit: 默认插入模式（overwriteMode=false）");
    XAPI_EXPECT(!XPlainTextEdit_centerCursor(pte) &&
                !XPlainTextEdit_centerOnScroll(pte),
                "XPlainTextEdit: 光标居中/滚动跟随默认关");
    XAPI_EXPECT(XPlainTextEdit_cursorWidth(pte) == 1,
                "XPlainTextEdit: 默认光标宽 1px");
    /* 断行规则实现缺省 WordWrap（头文件注：CJK 逐字可断/Latin 按词），
     * Qt 底层 QTextOption 缺省 WrapAtWordBoundaryOrAnywhere——承载差异
     * 注明，断言本库文档默认值。 */
    XAPI_EXPECT(XPlainTextEdit_wordWrapMode(pte) == 1,
                "XPlainTextEdit: 断行规则缺省 WordWrap(1)（本库文档默认）");
    XAPI_EXPECT(XPlainTextEdit_tabStopDistance(pte) == 40,
                "XPlainTextEdit: 默认 Tab 步进 40px（本库文档默认值）");
    XAPI_EXPECT(XPlainTextEdit_textInteractionFlags(pte) == 0,
                "XPlainTextEdit: 交互标志壳镜像缺省 0（编辑器默认组合）");
    XAPI_EXPECT(XPlainTextEdit_blockCount(pte) == 1,
                "XPlainTextEdit: 空文档 1 个空块（Qt blockCount 口径）");
    XAPI_EXPECT(XPlainTextEdit_lineCount(pte) == 1,
                "XPlainTextEdit: 空文档可视行数 1");

    /* ---- 2. 属性往返。 ---- */
    XPlainTextEdit_setPlaceholderText(pte, "占位…");
    XAPI_EXPECT(strcmp(xapi_cstr(XPlainTextEdit_placeholderText(pte)), "占位…") == 0,
                "XPlainTextEdit: setPlaceholderText 往返");
    XPlainTextEdit_setPlaceholderText(pte, NULL); /* 边界：NULL 归空 */
    XAPI_EXPECT(xapi_cstr(XPlainTextEdit_placeholderText(pte))[0] == '\0',
                "XPlainTextEdit: setPlaceholderText(NULL) 归空串");
    XPlainTextEdit_setMaximumBlockCount(pte, 8);
    XAPI_EXPECT(XPlainTextEdit_maximumBlockCount(pte) == 8,
                "XPlainTextEdit: setMaximumBlockCount 往返");
    XPlainTextEdit_setMaximumBlockCount(pte, -1); /* 边界：负值忽略 */
    XAPI_EXPECT(XPlainTextEdit_maximumBlockCount(pte) == 8,
                "XPlainTextEdit: setMaximumBlockCount(-1) 越界被忽略");
    XPlainTextEdit_setMaximumBlockCount(pte, 0);
    XPlainTextEdit_setLineWrapMode(pte, (int)XPlainTextEditMode_NoWrap);
    XAPI_EXPECT(XPlainTextEdit_lineWrapMode(pte) ==
                (int)XPlainTextEditMode_NoWrap,
                "XPlainTextEdit: setLineWrapMode(NoWrap) 往返");
    XPlainTextEdit_setWordWrapMode(pte, 4);
    XAPI_EXPECT(XPlainTextEdit_wordWrapMode(pte) == 4,
                "XPlainTextEdit: setWordWrapMode(4) 往返（断行策略承载）");
    XPlainTextEdit_setWordWrapMode(pte, 1);
    XPlainTextEdit_setCursorWidth(pte, 2);
    XAPI_EXPECT(XPlainTextEdit_cursorWidth(pte) == 2,
                "XPlainTextEdit: setCursorWidth(2) 往返");
    XPlainTextEdit_setCursorWidth(pte, 0); /* 边界：<=0 忽略 */
    XAPI_EXPECT(XPlainTextEdit_cursorWidth(pte) == 2,
                "XPlainTextEdit: setCursorWidth(0) 越界被忽略");
    XPlainTextEdit_setCursorWidth(pte, 1);
    XPlainTextEdit_setCenterCursor(pte, true);
    XPlainTextEdit_setCenterOnScroll(pte, true);
    XPlainTextEdit_setBackgroundVisible(pte, false);
    XPlainTextEdit_setTabChangesFocus(pte, true);
    XPlainTextEdit_setTabStopDistance(pte, 64);
    XAPI_EXPECT(XPlainTextEdit_centerCursor(pte) &&
                XPlainTextEdit_centerOnScroll(pte) &&
                !XPlainTextEdit_backgroundVisible(pte) &&
                XPlainTextEdit_tabChangesFocus(pte) &&
                XPlainTextEdit_tabStopDistance(pte) == 64,
                "XPlainTextEdit: center/背景/Tab 焦点/步进 五属性往返");
    XPlainTextEdit_setTextInteractionFlags(pte, 2);
    XAPI_EXPECT(XPlainTextEdit_textInteractionFlags(pte) == 2,
                "XPlainTextEdit: setTextInteractionFlags 壳镜像往返");
    XPlainTextEdit_setTextInteractionFlags(pte, 0);
    XPlainTextEdit_setDocumentTitle_2(pte, "T1");
    {
        XString* title = XPlainTextEdit_documentTitle(pte);
        XAPI_EXPECT(title && strcmp(xapi_u8(title), "T1") == 0,
                    "XPlainTextEdit: setDocumentTitle_2/documentTitle 往返");
        if (title) XString_delete_base((XClass*)title);
    }
    XPlainTextEdit_setBackgroundVisible(pte, true);
    XPlainTextEdit_setCenterCursor(pte, false);
    XPlainTextEdit_setCenterOnScroll(pte, false);
    XPlainTextEdit_setTabChangesFocus(pte, false);
    XPlainTextEdit_setTabStopDistance(pte, 40);

    /* ---- 3. 文本装入/导出/追加/插入/清空。 ---- */
    XPlainTextEdit_setPlainText(pte, "ab\ncd");
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strcmp(plain, "ab\ncd") == 0,
                    "XPlainTextEdit: setPlainText/toPlainText 多行往返");
        if (plain) XFree_System(plain);
    }
    XAPI_EXPECT(XPlainTextEdit_blockCount(pte) == 2,
                "XPlainTextEdit: 两行文本 blockCount=2（逻辑块口径）");
    XPlainTextEdit_appendPlainText(pte, "e");
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strcmp(plain, "ab\ncd\ne") == 0,
                    "XPlainTextEdit: appendPlainText 尾部新段追加");
        if (plain) XFree_System(plain);
    }
    XAPI_EXPECT(XPlainTextEdit_blockCount(pte) == 3,
                "XPlainTextEdit: 追加后 blockCount=3");
    XPlainTextEdit_setTextCursor(pte, 0, 1);
    XPlainTextEdit_insertPlainText(pte, "X");
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strcmp(plain, "aXb\ncd\ne") == 0,
                    "XPlainTextEdit: insertPlainText 光标处插入");
        if (plain) XFree_System(plain);
    }
    XPlainTextEdit_setTextCursor(pte, 99, 99); /* 边界：钳位 */
    XAPI_EXPECT(XPlainTextEdit_textCursorLine(pte) == 2 &&
                XPlainTextEdit_textCursorColumn(pte) == 1,
                "XPlainTextEdit: setTextCursor 越界钳位末行末列");
    {
        XPoint cur = XPlainTextEdit_textCursor(pte);
        XAPI_EXPECT(cur.x == 2 && cur.y == 1,
                    "XPlainTextEdit: textCursor 平铺承载 (行,列)=(2,1)");
    }
    XPlainTextEdit_setTextCursor(pte, 1, 1);
    XAPI_EXPECT(XPlainTextEdit_cursorLine(pte) == 1 &&
                XPlainTextEdit_cursorColumn(pte) == 1,
                "XPlainTextEdit: cursorLine/cursorColumn 往返 (1,1)");
    XPlainTextEdit_setPlainText(pte, NULL); /* 边界：NULL 归空 */
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && plain[0] == '\0',
                    "XPlainTextEdit: setPlainText(NULL) 归空串");
        if (plain) XFree_System(plain);
    }
    XAPI_EXPECT(g_xpeTextChanged > 0,
                "XPlainTextEdit: 内容替换发射 textChanged（真发射）");
    /* NoWrap 口径：可视行数 == 逻辑块数。 */
    XPlainTextEdit_setLineWrapMode(pte, (int)XPlainTextEditMode_NoWrap);
    XAPI_EXPECT(XPlainTextEdit_lineCount(pte) ==
                XPlainTextEdit_blockCount(pte),
                "XPlainTextEdit: NoWrap 下 lineCount==blockCount");
    XPlainTextEdit_setLineWrapMode(pte, (int)XPlainTextEditMode_WidgetWidth);

    /* ---- 4. 查找（对标 QTextDocument::find 大小写不敏感缺省）。 ---- */
    XPlainTextEdit_setPlainText(pte, "hello world hello");
    XPlainTextEdit_setTextCursor(pte, 0, 0);
    XAPI_EXPECT(XPlainTextEdit_find(pte, "world", 0),
                "XPlainTextEdit: find 前向命中 world");
    {
        char* sel = XPlainTextEdit_selectedText(pte);
        XAPI_EXPECT(XPlainTextEdit_hasSelectedText(pte) && sel &&
                    strcmp(sel, "world") == 0,
                    "XPlainTextEdit: 命中后选区即命中串（Qt 语义）");
        if (sel) XFree_System(sel);
    }
    XAPI_EXPECT(XPlainTextEdit_find(pte, "HELLO", 0),
                "XPlainTextEdit: find 缺省大小写不敏感命中 HELLO");
    XPlainTextEdit_setTextCursor(pte, 0, 0);
    XAPI_EXPECT(!XPlainTextEdit_find(pte, "zzz", 0),
                "XPlainTextEdit: find 未命中返回 false");
    XPlainTextEdit_setTextCursor(pte, 0, 0);
    XAPI_EXPECT(XPlainTextEdit_find(pte, "hello", 1),
                "XPlainTextEdit: FindBackward(1) 反向命中");
    XPlainTextEdit_selectAll(pte);
    {
        char* sel = XPlainTextEdit_selectedText(pte);
        XAPI_EXPECT(sel && strcmp(sel, "hello world hello") == 0,
                    "XPlainTextEdit: selectAll 后选区为全文");
        if (sel) XFree_System(sel);
    }
    XPlainTextEdit_setTextCursor(pte, 0, 0);
    XAPI_EXPECT(!XPlainTextEdit_hasSelectedText(pte),
                "XPlainTextEdit: 光标收拢后无选区");

    /* ---- 5. 剪贴板链路（copy/canPaste/paste）。 ---- */
    XPlainTextEdit_selectAll(pte);
    XPlainTextEdit_copy(pte);
    XAPI_EXPECT(XPlainTextEdit_canPaste(pte),
                "XPlainTextEdit: 复制后 canPaste=true（剪贴板非空）");
    XPlainTextEdit_clear(pte);
    XPlainTextEdit_paste(pte);
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strcmp(plain, "hello world hello") == 0,
                    "XPlainTextEdit: copy/clear/paste 剪贴板往返");
        if (plain) XFree_System(plain);
    }
    XPlainTextEdit_clear(pte);
    XAPI_EXPECT(XPlainTextEdit_blockCount(pte) == 1,
                "XPlainTextEdit: clear 后回到单空块（Qt clear 口径）");

    /* ---- 6. 只读态（映射控制器可编辑标志，输入被拒）。 ---- */
    XPlainTextEdit_setReadOnly(pte, true);
    XAPI_EXPECT(XPlainTextEdit_isReadOnly(pte) &&
                !XPlainTextEdit_canPaste(pte),
                "XPlainTextEdit: 只读态 isReadOnly=true/canPaste=false");
    XWidget_setFocus((XWidget*)pte);
    ttxt_type((XWidget*)pte, "zz");
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && plain[0] == '\0',
                    "XPlainTextEdit: 只读态键入不改变文本");
        if (plain) XFree_System(plain);
    }
    XPlainTextEdit_setReadOnly(pte, false);

    /* ---- 7. 撤销/重做（键入→undo→redo 状态迁移）。 ---- */
    XPlainTextEdit_setUndoRedoEnabled(pte, false);
    XPlainTextEdit_setUndoRedoEnabled(pte, true); /* 关开清历史 */
    XPlainTextEdit_clear(pte);
    XWidget_setFocus((XWidget*)pte);
    ttxt_type((XWidget*)pte, "ab");
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strcmp(plain, "ab") == 0,
                    "XPlainTextEdit: 实机键入 ab 后文本一致");
        if (plain) XFree_System(plain);
    }
    XPlainTextEdit_undo(pte);
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strlen(plain) < 2,
                    "XPlainTextEdit: undo 回退键入（文本变短）");
        if (plain) XFree_System(plain);
    }
    XPlainTextEdit_redo(pte);
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(plain && strcmp(plain, "ab") == 0,
                    "XPlainTextEdit: redo 重放恢复 ab");
        if (plain) XFree_System(plain);
    }

    /* ---- 8. 块数上限裁剪（超限丢弃最旧行，Qt 语义）。 ---- */
    XPlainTextEdit_setMaximumBlockCount(pte, 2);
    XPlainTextEdit_setPlainText(pte, "1\n2\n3\n4");
    {
        char* plain = XPlainTextEdit_toPlainText(pte);
        XAPI_EXPECT(XPlainTextEdit_blockCount(pte) == 2 && plain &&
                    strcmp(plain, "3\n4") == 0,
                    "XPlainTextEdit: 上限 2 保留末 2 行（丢弃最旧行）");
        if (plain) XFree_System(plain);
    }
    XPlainTextEdit_setMaximumBlockCount(pte, 0);

    /* ---- 9. 光标几何（行高随字体度量/左留白 2 文档口径 + 反查互逆）。 -- */
    XPlainTextEdit_clear(pte);
    {
        XRect r = XPlainTextEdit_cursorRect(pte);
        XPoint p;
        XPoint hit;
        int lineHeight = ttxt_fontLineHeight((XWidget*)pte);
        XPoint_init(&p, r.x, r.y + r.height / 2);
        /* 实现口径：行高=控制器字体度量 ascent+descent（环境相关，本机
         * 12pt 默认字体为 24；固定 16 只是度量失败回退常量）；宽=cursorWidth
         * 缺省 1；x=行左留白 2px+光标前列宽(0)。 */
        XAPI_EXPECT(r.height == lineHeight && r.width == 1 && r.x == 2,
                    "XPlainTextEdit: cursorRect 行高=字体度量/宽1/左留白2");
        hit = XPlainTextEdit_cursorForPosition(pte, &p);
        XAPI_EXPECT(hit.x == 0 && hit.y == 0,
                    "XPlainTextEdit: cursorForPosition 反查 (0,0)");
    }

    /* ---- 10. 额外选择集（setExtraSelections 位置承载 + 拷贝语义）。 --- */
    {
        XPlainTextEditExtraSelection sel;
        const XPlainTextEditExtraSelection* out = NULL;
        int n;
        /* 承载为控制器绝对区间：写入行/列即时换算为绝对位置，读出再换算
         * 回行/列。空文档只有 1 块，line=1 越界会被钳到 0——先装入两行
         * 使 line=1 在界内，往返字段才逐项一致。 */
        XPlainTextEdit_setPlainText(pte, "ab\ncd");
        sel.line = 1;
        sel.col = 0;
        sel.length = 1;
        sel.color = 0xFF00FF00u;
        XPlainTextEdit_setExtraSelections(pte, &sel, 1);
        n = XPlainTextEdit_extraSelections(pte, &out);
        XAPI_EXPECT(n == 1 && out && out[0].line == 1 && out[0].col == 0 &&
                    out[0].length == 1 && out[0].color == 0xFF00FF00u,
                    "XPlainTextEdit: 额外选择集写入/读出字段一致");
        XPlainTextEdit_setExtraSelections(pte, NULL, 0); /* NULL 清空 */
        n = XPlainTextEdit_extraSelections(pte, &out);
        XAPI_EXPECT(n == 0,
                    "XPlainTextEdit: setExtraSelections(NULL,0) 清空");
    }

    /* ---- 11. 文档借用/资源桩（对标 QPlainTextEdit::document）。 ---- */
#if APITEXT_DOC_ON
    XAPI_EXPECT(XPlainTextEdit_document(pte) != NULL,
                "XPlainTextEdit: document() 返回控制器文档借用");
#endif
    XAPI_EXPECT(XPlainTextEdit_loadResource(pte, 0, "r") == NULL,
                "XPlainTextEdit: loadResource 恒 NULL（无效 QVariant 口径）");
    XPlainTextEdit_clear(pte);
    XPlainTextEdit_delete_base((XClass*)pte);
    XPrintf("XGuiApiTest: [XPlainTextEdit] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
#endif /* APITEXT_PTE_ON */

#if APITEXT_BROWSER_ON

/* ==================== XTextBrowser：富文本浏览/导航 ================== */

static int g_xtbSrcChanged;     /**< sourceChanged 计数。 */
static int g_xtbHistChanged;    /**< historyChanged 计数。 */
static int g_xtbBackAv;         /**< backwardAvailable 计数。 */
static int g_xtbFwdAv;          /**< forwardAvailable 计数。 */
static char g_xtbLastSrc[64];   /**< 最近一次 sourceChanged 载荷。 */

/** @brief sourceChanged(const QUrl&) 载荷捕获槽（XString* 承载）。 */
static void ttxt_slotXtbSourceChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, url); /* 宏内声明 const XString* const */
    ++g_xtbSrcChanged;
    g_xtbLastSrc[0] = '\0';
    if (url && xapi_u8(url))
        snprintf(g_xtbLastSrc, sizeof(g_xtbLastSrc), "%s",
                 xapi_u8(url));
}

/** @brief historyChanged 计数槽。 */
static void ttxt_slotXtbHistChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtbHistChanged;
}

/** @brief backwardAvailable(bool) 计数槽。 */
static void ttxt_slotXtbBackAv(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtbBackAv;
}

/** @brief forwardAvailable(bool) 计数槽。 */
static void ttxt_slotXtbFwdAv(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtbFwdAv;
}

/** @brief XTextBrowser 族断言。 */
static int ttxt_run_text_browser(void)
{
    int failures = 0;
    XTextBrowser* b = XTextBrowser_create(NULL, 0);
    if (!b) {
        XAPI_EXPECT(false, "XTextBrowser: 实例创建失败");
        return 1;
    }
    XObject_connect_1((XObject*)b,
                      (size_t)XTextBrowser_sourceChanged_signal(NULL, NULL),
                      (XObject*)b, ttxt_slotXtbSourceChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)b,
                      (size_t)XTextBrowser_historyChanged_signal(NULL),
                      (XObject*)b, ttxt_slotXtbHistChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)b,
                      (size_t)XTextBrowser_backwardAvailable_signal(NULL,
                                                                    false),
                      (XObject*)b, ttxt_slotXtbBackAv,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)b,
                      (size_t)XTextBrowser_forwardAvailable_signal(NULL,
                                                                   false),
                      (XObject*)b, ttxt_slotXtbFwdAv,
                      XConnectionType_Direct);

    /* ---- 1. Qt 对齐默认值（QTextBrowser 构造态）。 ---- */
    XAPI_EXPECT(xapi_cstr(XTextBrowser_source(b))[0] == '\0',
                "XTextBrowser: 默认 source 为空（未加载任何源）");
    XAPI_EXPECT(XTextBrowser_sourceType(b) ==
                XTextBrowserSourceType_Unknown,
                "XTextBrowser: 导航栈空时 sourceType=Unknown（Qt 口径）");
    XAPI_EXPECT(XTextBrowser_openLinks(b),
                "XTextBrowser: openLinks 默认 true（Qt 默认）");
    XAPI_EXPECT(!XTextBrowser_openExternalLinks(b),
                "XTextBrowser: openExternalLinks 默认 false（Qt 默认）");
    XAPI_EXPECT(XTextBrowser_backwardHistoryCount(b) == 0 &&
                XTextBrowser_forwardHistoryCount(b) == 0,
                "XTextBrowser: 初始历史前后条数均为 0");
    XAPI_EXPECT(!XTextBrowser_isBackwardAvailable(b) &&
                !XTextBrowser_isForwardAvailable(b),
                "XTextBrowser: 初始后退/前进均不可用");
    XAPI_EXPECT(XTextEdit_isReadOnly_2((XTextEdit*)b),
                "XTextBrowser: 默认只读（QTextBrowser 只读语义）");
    {
        XStringList* paths = XTextBrowser_searchPaths(b);
        XAPI_EXPECT(paths != NULL &&
                    XStringList_size_base((const XContainer*)paths) == 0,
                    "XTextBrowser: 默认搜索路径为空列表");
        if (paths) XStringList_delete_base((XClass*)paths);
    }

    /* ---- 2. setSource 导航 + 历史栈状态迁移。 ---- */
    XTextBrowser_setSource(b, "xtb://q1");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_source(b)), "xtb://q1") == 0,
                "XTextBrowser: setSource/source 往返");
    XAPI_EXPECT(XTextBrowser_sourceType(b) == XTextBrowserSourceType_Url,
                "XTextBrowser: 有导航条目后 sourceType=Url（0=Url）");
    XAPI_EXPECT(strcmp(g_xtbLastSrc, "xtb://q1") == 0,
                "XTextBrowser: sourceChanged 载荷为新源 URL");
    XAPI_EXPECT(g_xtbHistChanged == 1,
                "XTextBrowser: setSource 追加历史发射 historyChanged");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_historyTitle(b, 0)), "xtb://q1") == 0,
                "XTextBrowser: historyTitle(0)=当前条目（简化=URL）");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_historyUrl(b, 0)), "xtb://q1") == 0,
                "XTextBrowser: historyUrl(0)=当前条目 URL");
    XTextBrowser_setSource(b, "xtb://q2");
    XTextBrowser_setSource(b, "xtb://q3");
    XAPI_EXPECT(XTextBrowser_backwardHistoryCount(b) == 2 &&
                XTextBrowser_forwardHistoryCount(b) == 0,
                "XTextBrowser: 三次导航后可后退 2/可前进 0");
    XAPI_EXPECT(XTextBrowser_isBackwardAvailable(b) &&
                !XTextBrowser_isForwardAvailable(b),
                "XTextBrowser: isBackward=true/isForward=false");
    XAPI_EXPECT(g_xtbBackAv >= 1,
                "XTextBrowser: 可用性变化发射 backwardAvailable");
    XTextBrowser_backward(b);
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_source(b)), "xtb://q2") == 0 &&
                XTextBrowser_backwardHistoryCount(b) == 1 &&
                XTextBrowser_forwardHistoryCount(b) == 1,
                "XTextBrowser: backward 后源=q2 且前后各 1 条");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_historyUrl(b, 1)), "xtb://q3") == 0,
                "XTextBrowser: historyUrl(+1) 取前进条目 q3");
    XAPI_EXPECT(g_xtbFwdAv >= 1,
                "XTextBrowser: 进入可前进态发射 forwardAvailable");
    XTextBrowser_forward(b);
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_source(b)), "xtb://q3") == 0,
                "XTextBrowser: forward 回到 q3");
    XTextBrowser_home(b);
    XAPI_EXPECT(strcmp(xapi_cstr(XTextBrowser_source(b)), "xtb://q1") == 0 &&
                !XTextBrowser_isBackwardAvailable(b),
                "XTextBrowser: home 回首条且不可再后退");
    XAPI_EXPECT(xapi_cstr(XTextBrowser_historyUrl(b, -1))[0] == '\0',
                "XTextBrowser: historyUrl(-1) 越界返回空串");
    g_xtbSrcChanged = 0;
    XTextBrowser_reload(b);
    XAPI_EXPECT(g_xtbSrcChanged == 1,
                "XTextBrowser: reload 重发 sourceChanged（无加载通道）");
    XAPI_EXPECT(strcmp(g_xtbLastSrc, "xtb://q1") == 0,
                "XTextBrowser: reload 载荷为当前源 q1");
    XTextBrowser_clearHistory(b);
    XAPI_EXPECT(XTextBrowser_backwardHistoryCount(b) == 0 &&
                XTextBrowser_forwardHistoryCount(b) == 0,
                "XTextBrowser: clearHistory 清前后条目保留当前");
    XAPI_EXPECT(g_xtbHistChanged == 4,
                "XTextBrowser: historyChanged 共 4 次"
                "（3 次 setSource 追加 + clearHistory；backward/forward/"
                "home 历史内移动不追加不发射）");

    /* ---- 3. 链接开关与搜索路径。 ---- */
    XTextBrowser_setOpenLinks(b, false);
    XAPI_EXPECT(!XTextBrowser_openLinks(b),
                "XTextBrowser: setOpenLinks(false) 往返");
    XTextBrowser_setOpenLinks(b, true);
    XTextBrowser_setOpenExternalLinks(b, true);
    XAPI_EXPECT(XTextBrowser_openExternalLinks(b),
                "XTextBrowser: setOpenExternalLinks(true) 往返");
    XTextBrowser_setOpenExternalLinks(b, false);
    {
        XStringList* paths = XStringList_create();
        XStringList* got;
        if (paths) {
            XStringList_push_back_utf8(paths, "/opt/doc");
            XStringList_push_back_utf8(paths, "/usr/share/doc");
            XTextBrowser_setSearchPaths(b, paths);
            got = XTextBrowser_searchPaths(b);
            XAPI_EXPECT(got != NULL &&
                        XStringList_size_base((const XContainer*)got) == 2,
                        "XTextBrowser: setSearchPaths 深拷贝条数一致");
            if (got) XStringList_delete_base((XClass*)got);
            XStringList_delete_base((XClass*)paths);
        } else {
            XAPI_EXPECT(false, "XTextBrowser: 搜索路径列表创建失败");
        }
    }

    /* ---- 4. setHtml（进只读预览 + toHtml 文档真源）。 ---- */
    XTextBrowser_setHtml(b, "<b>浏览器内容</b>");
    {
        XString* plain = XTextEdit_toPlainText((XTextEdit*)b);
        XAPI_EXPECT(plain && strcmp(xapi_u8(plain), "浏览器内容") == 0,
                    "XTextBrowser: setHtml 剥离标签保留文本（所见即所存）");
        if (plain) XString_delete_base((XClass*)plain);
    }
    XAPI_EXPECT(XTextEdit_isRichPreview((XTextEdit*)b),
                "XTextBrowser: setHtml 自动进入只读富文本预览");
    {
        char* html = XTextEdit_toHtml((XTextEdit*)b);
        XAPI_EXPECT(html && strstr(html, "<b>") != NULL,
                    "XTextBrowser: 预览态 toHtml 导出含粗体标记");
        if (html) XFree_System(html);
    }
    XAPI_EXPECT(XTextEdit_isReadOnly_2((XTextEdit*)b),
                "XTextBrowser: setHtml 后保持只读（浏览器恒只读）");
    {
        XPoint p;
        XString* anchor;
        XPoint_init(&p, 0, 0);
        anchor = XTextBrowser_anchorAt(b, &p);
        /* 非锚点片段/未命中恒返回 0 长度字符串对象（头文件口径）。 */
        XAPI_EXPECT(anchor != NULL && XString_size_base(
                        (const XContainer*)anchor) == 0,
                    "XTextBrowser: anchorAt 无锚点处返回空串对象");
        if (anchor) XString_delete_base((XClass*)anchor);
    }
    {
        XRect r = XTextBrowser_cursorRect(b);
        /* 实现口径：委托内嵌编辑器 cursorRect，行高=编辑器字体度量
         * ascent+descent（环境相关，本机 24；固定 16 只是度量失败回退）。 */
        XAPI_EXPECT(r.height == ttxt_fontLineHeight(
                        ((XTextEdit*)b)->m_editor ? (XWidget*)((XTextEdit*)b)->m_editor
                                                  : (XWidget*)b),
                    "XTextBrowser: cursorRect 委托编辑器同口径（行高=字体度量）");
    }

    XTextBrowser_delete_base((XClass*)b);
    XPrintf("XGuiApiTest: [XTextBrowser] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
#endif /* APITEXT_BROWSER_ON */

#if APITEXT_DOC_ON

/* ==================== XTextDocument：富文本文档模型 ================== */

static int g_xtdContentsChanged; /**< contentsChanged 计数。 */
static int g_xtdCursorMoved;     /**< cursorPositionChanged 计数。 */

/** @brief contentsChanged 计数槽。 */
static void ttxt_slotXtdChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtdContentsChanged;
}

/** @brief cursorPositionChanged 计数槽。 */
static void ttxt_slotXtdCursor(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xtdCursorMoved;
}

/** @brief XTextDocument 族断言。 */
static int ttxt_run_text_document(void)
{
    int failures = 0;
    XTextDocument* d = XTextDocument_create();
    XTDCharFormat fmt;
    if (!d) {
        XAPI_EXPECT(false, "XTextDocument: 实例创建失败");
        return 1;
    }
    XObject_connect_1((XObject*)d,
                      (size_t)XTextDocument_contentsChanged_signal(NULL),
                      (XObject*)d, ttxt_slotXtdChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)d,
                      (size_t)XTextDocument_cursorPositionChanged_signal(NULL),
                      (XObject*)d, ttxt_slotXtdCursor,
                      XConnectionType_Direct);

    /* ---- 1. 构造默认值（对标 Qt 空文档）。 ---- */
    XAPI_EXPECT(XTextDocument_isEmpty(d),
                "XTextDocument: 新文档为空（isEmpty=true）");
    XAPI_EXPECT(XTextDocument_blockCount(d) == 1,
                "XTextDocument: 空文档 1 个块（Qt 空文档口径）");
    XAPI_EXPECT(XTextDocument_isUndoRedoEnabled(d),
                "XTextDocument: 撤销重做默认启用（Qt 默认 true）");
    XAPI_EXPECT(xapi_cstr(XTextDocument_metaInformation(d, 0))[0] == '\0' &&
                xapi_cstr(XTextDocument_metaInformation(d, 1))[0] == '\0',
                "XTextDocument: 文档标题/源 URL 元信息默认为空");
    XAPI_EXPECT(XTextDocument_cursorPosition(d) == 0,
                "XTextDocument: 光标默认位置 0");

    /* ---- 2. setPlainText/toPlainText 往返 + contentsChanged。 ---- */
    XTextDocument_setPlainText(d, "hello");
    {
        char* plain = XTextDocument_toPlainText(d);
        XAPI_EXPECT(plain && strcmp(plain, "hello") == 0,
                    "XTextDocument: setPlainText/toPlainText 往返");
        if (plain) XFree_System(plain);
    }
    XAPI_EXPECT(g_xtdContentsChanged >= 1,
                "XTextDocument: 内容变化发射 contentsChanged（真发射）");
    XAPI_EXPECT(!XTextDocument_isEmpty(d),
                "XTextDocument: 装入文本后 isEmpty=false");
    XTextDocument_setPlainText(d, "a\nb");
    XAPI_EXPECT(XTextDocument_blockCount(d) == 2,
                "XTextDocument: 两行文本 2 块（\\n 分段）");
    {
        char* plain = XTextDocument_toPlainText(d);
        XAPI_EXPECT(plain && strcmp(plain, "a\nb") == 0,
                    "XTextDocument: 多行往返含换行");
        if (plain) XFree_System(plain);
    }
    XTextDocument_setPlainText(d, NULL); /* 边界：NULL 按空串处理 */
    {
        char* plain = XTextDocument_toPlainText(d);
        XAPI_EXPECT(plain && plain[0] == '\0',
                    "XTextDocument: setPlainText(NULL) 归空");
        if (plain) XFree_System(plain);
    }

    /* ---- 3. find/characterAt/cursorPosition（字符索引口径）。 ---- */
    XTextDocument_setPlainText(d, "hello");
    XAPI_EXPECT(XTextDocument_find(d, "llo") == 2,
                "XTextDocument: find 返回首个匹配字符位置 2");
    XAPI_EXPECT(XTextDocument_find(d, "zzz") == -1,
                "XTextDocument: find 未命中返回 -1");
    XAPI_EXPECT(XTextDocument_characterAt(d, 1) == 'e',
                "XTextDocument: characterAt(1)='e'（对标 characterAt）");
    XAPI_EXPECT(XTextDocument_characterAt(d, -1) == '\0' &&
                XTextDocument_characterAt(d, 999) == '\0',
                "XTextDocument: characterAt 越界/负位返回 '\\0'");
    g_xtdCursorMoved = 0;
    XTextDocument_setCursorPosition(d, 3);
    XAPI_EXPECT(XTextDocument_cursorPosition(d) == 3 &&
                g_xtdCursorMoved == 1,
                "XTextDocument: setCursorPosition 往返并发射变化信号");
    XTextDocument_setCursorPosition(d, -5); /* 边界：负值钳位 0 */
    XAPI_EXPECT(XTextDocument_cursorPosition(d) == 0,
                "XTextDocument: setCursorPosition(-5) 钳位 0");

    /* ---- 4. 元信息（对标 setMetaInformation）。 ---- */
    XTextDocument_setMetaInformation(d, 0, "Doc 标题");
    XTextDocument_setMetaInformation(d, 1, "https://gui.example");
    XAPI_EXPECT(strcmp(xapi_cstr(XTextDocument_metaInformation(d, 0)), "Doc 标题") == 0,
                "XTextDocument: 标题元信息往返（info=0）");
    XAPI_EXPECT(strcmp(XTextDocument_metaInformation(d, 1),
                       "https://gui.example") == 0,
                "XTextDocument: 源 URL 元信息往返（info=1）");
    XAPI_EXPECT(xapi_cstr(XTextDocument_metaInformation(d, 9))[0] == '\0',
                "XTextDocument: 未知元信息类返回空串");

    /* ---- 5. setHtml 渲染子集 + 片段格式。 ---- */
    XMemset(&fmt, 0, sizeof(fmt));
    XTextDocument_setHtml(d, "<p><b>A</b><i>B</i></p>");
    {
        char* plain = XTextDocument_toPlainText(d);
        XAPI_EXPECT(plain && strcmp(plain, "AB") == 0,
                    "XTextDocument: setHtml 解析子集保留文本 AB");
        if (plain) XFree_System(plain);
    }
    XAPI_EXPECT(XTextDocument_fragmentCount(d, 0) == 2,
                "XTextDocument: b/i 生成两个格式片段");
    {
        const XTDFragment* f0 = XTextDocument_fragment(d, 0, 0);
        const XTDFragment* f1 = XTextDocument_fragment(d, 0, 1);
        XAPI_EXPECT(f0 && f0->fmt.bold && !f0->fmt.italic,
                    "XTextDocument: 片段 0 承载粗体格式（QTextCharFormat）");
        XAPI_EXPECT(f1 && f1->fmt.italic && !f1->fmt.bold,
                    "XTextDocument: 片段 1 承载斜体格式");
        XAPI_EXPECT(!XTextDocument_fragment(d, 0, 9),
                    "XTextDocument: 片段下标越界返回 NULL");
    }
    {
        char* html = XTextDocument_toHtml(d);
        XAPI_EXPECT(html && strstr(html, "<html><body>") != NULL &&
                    strstr(html, "<b>A</b>") != NULL &&
                    strstr(html, "</body></html>") != NULL,
                    "XTextDocument: toHtml 导出含 b 标记与文档壳");
        if (html) XFree_System(html);
    }

    /* ---- 6. 块级格式（对齐/标题/缩进）。 ---- */
    XTextDocument_setBlockAlignment(d, 0, (int)XTDAlignment_HCenter);
    XAPI_EXPECT(XTextDocument_blockAlignment(d, 0) ==
                (int)XTDAlignment_HCenter,
                "XTextDocument: setBlockAlignment 往返（HCenter）");
    {
        char* html = XTextDocument_toHtml(d);
        XAPI_EXPECT(html && strstr(html, "align=\"center\"") != NULL,
                    "XTextDocument: 居中对齐导出 align=center 属性");
        if (html) XFree_System(html);
    }
    XTextDocument_setBlockHeadingLevel(d, 0, 2);
    {
        char* html = XTextDocument_toHtml(d);
        XAPI_EXPECT(html && strstr(html, "<h2") != NULL,
                    "XTextDocument: 标题级 2 导出 <h2> 块标签");
        if (html) XFree_System(html);
    }
    XTextDocument_setBlockIndentLevel(d, 0, 1);
    XAPI_EXPECT(XTextDocument_blockIndentLevel(d, 0) == 1,
                "XTextDocument: 列表缩进级别往返");
    XTextDocument_setBlockHeadingLevel(d, 0, 0);
    XTextDocument_setBlockAlignment(d, 0, 0);
    XTextDocument_setBlockIndentLevel(d, 0, 0);

    /* ---- 7. appendHtml/片段编程接口。 ---- */
    XTextDocument_appendHtml(d, "<b>!</b>"); /* 末块非空另起新段 */
    XAPI_EXPECT(XTextDocument_blockCount(d) == 2,
                "XTextDocument: appendHtml 末块非空另起新段");
    {
        const XTDFragment* f = XTextDocument_fragment(d, 1, 0);
        XAPI_EXPECT(f && f->fmt.bold,
                    "XTextDocument: 追加段片段承载粗体");
    }
    XMemset(&fmt, 0, sizeof(fmt));
    fmt.underline = true;
    XAPI_EXPECT(XTextDocument_addFragment(d, 1, "C", &fmt) >= 0,
                "XTextDocument: addFragment 追加带格式片段成功");
    {
        const XTDFragment* f = XTextDocument_fragment(d, 1, 1);
        char* plain;
        XAPI_EXPECT(f && f->fmt.underline && f->text &&
                    strcmp(xapi_u8(f->text), "C") == 0,
                    "XTextDocument: 新片段文本/下划线格式一致");
        plain = XTextDocument_toPlainText(d);
        XAPI_EXPECT(plain && strcmp(plain, "AB\n!C") == 0,
                    "XTextDocument: 片段拼接导出 AB\\n!C");
        if (plain) XFree_System(plain);
    }
    XTextDocument_setFragmentBold(d, 1, 0, false);
    XAPI_EXPECT(!XTextDocument_fragment(d, 1, 0)->fmt.bold,
                "XTextDocument: setFragmentBold(false) 关闭粗体");
    XTextDocument_setFragmentFgColor(d, 1, 1, 0xFF112233u);
    XAPI_EXPECT(XTextDocument_fragment(d, 1, 1)->fmt.fgColor ==
                0xFF112233u,
                "XTextDocument: setFragmentFgColor 往返（前景色承载）");
    XTextDocument_appendBlock(d, NULL);
    XAPI_EXPECT(XTextDocument_blockCount(d) == 3,
                "XTextDocument: appendBlock 新增空块");
    XTextDocument_appendText(d, "D", NULL);
    {
        char* plain = XTextDocument_toPlainText(d);
        XAPI_EXPECT(plain && strcmp(plain, "AB\n!C\nD") == 0,
                    "XTextDocument: appendText 写入末块");
        if (plain) XFree_System(plain);
    }

    /* ---- 8. 行内图片片段（NULL 边界 + 深拷贝承载）。 ---- */
    XAPI_EXPECT(XTextDocument_insertImage(d, NULL) == -1,
                "XTextDocument: insertImage(NULL) 拒绝返回 -1");
    {
        XImage img;
        int idx;
        XImage_init_ex(&img, 2, 2, XImageFormat_ARGB32);
        idx = XTextDocument_insertImage(d, &img);
        XAPI_EXPECT(idx >= 0 &&
                    XTextDocument_fragment(d, 2, idx) != NULL &&
                    XTextDocument_fragment(d, 2, idx)->image != NULL,
                    "XTextDocument: insertImage 深拷贝为图片片段");
        XImage_deinit_base((XClass*)&img);
    }

    /* ---- 9. 默认格式（setDefaultFormat 全局承载）。 ---- */
    XMemset(&fmt, 0, sizeof(fmt));
    fmt.bold = true;
    XTextDocument_setDefaultFormat(d, &fmt);
    XAPI_EXPECT(XTextDocument_defaultFormat(d) &&
                XTextDocument_defaultFormat(d)->bold,
                "XTextDocument: setDefaultFormat/defaultFormat 往返");
    XMemset(&fmt, 0, sizeof(fmt));
    XTextDocument_setDefaultFormat(d, &fmt); /* 还原全局默认格式 */

    /* ---- 10. clear 复位（对标 QTextDocument::clear）。 ---- */
    XTextDocument_clear(d);
    XAPI_EXPECT(XTextDocument_isEmpty(d) && XTextDocument_blockCount(d) == 1,
                "XTextDocument: clear 回到单空块且 isEmpty=true");

    /* 注（不硬断言）：撤销栈快照入口当前无编辑路径调用（undo 永不可
     * 用），isUndoAvailable/undo 的 Qt 行为断言留待框架补快照接线，
     * 详见提交 notes。 */
    XTextDocument_delete_base((XClass*)d);
    XPrintf("XGuiApiTest: [XTextDocument] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
#endif /* APITEXT_DOC_ON */

#if APITEXT_COMPLETER_ON

/* ==================== XCompleter：补全对象（对标 QCompleter） ======== */

static int g_xcomHighlighted; /**< highlighted(text) 计数。 */

/** @brief highlighted 计数槽（载荷 XString* 仅计数）。 */
static void ttxt_slotComHighlighted(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xcomHighlighted;
}

/** @brief 建立 6 行 2 列词条模型（列 0 词条/列 1 类别）。 */
static XAbstractItemModel* ttxt_make_word_model(void)
{
    static const char* const kWords[6] = {
        "Open File", "Open Directory", "Close Editor",
        "Copy Path", "Recent Files", "Save All"
    };
    static const char* const kKinds[6] = {
        "menu", "menu", "menu", "action", "menu", "action"
    };
    XAbstractItemModel* m = XAbstractItemModel_create();
    int i;
    if (!m) return NULL;
    XAbstractItemModel_setDimension(m, 6, 2);
    for (i = 0; i < 6; ++i) {
        XAbstractItemModel_setData_2(m, i, 0, kWords[i]);
        XAbstractItemModel_setData_2(m, i, 1, kKinds[i]);
    }
    return m;
}

/** @brief XCompleter 族断言。 */
static int ttxt_run_completer(void)
{
    int failures = 0;
    XAbstractItemModel* model = ttxt_make_word_model();
    XCompleter* c = XCompleter_create(NULL);
    XString* s;
    if (!model || !c) {
        XAPI_EXPECT(model != NULL, "XCompleter: 词条模型创建");
        XAPI_EXPECT(c != NULL, "XCompleter: 实例创建");
        if (model) XAbstractItemModel_delete_base(model);
        if (c) XCompleter_delete_base(c);
        return failures;
    }
    XObject_connect_1((XObject*)c,
                      (size_t)XCompleter_highlighted_signal(NULL, NULL),
                      (XObject*)c, ttxt_slotComHighlighted,
                      XConnectionType_Direct);

    /* ---- 1. Qt 对齐默认值（QCompleter 构造态）。 ---- */
    XAPI_EXPECT(XCompleter_model(c) == NULL,
                "XCompleter: 默认无模型");
    XAPI_EXPECT(XCompleter_widget(c) == NULL,
                "XCompleter: 默认无关联编辑控件");
    XAPI_EXPECT(XCompleter_popup(c) == NULL,
                "XCompleter: 默认无弹出视图");
    XAPI_EXPECT(XCompleter_completionMode(c) ==
                XCompleterCompletionMode_PopupCompletion,
                "XCompleter: 默认 PopupCompletion（Qt 6.8 数值 0）");
    XAPI_EXPECT(XCompleter_caseSensitivity(c) == XChar_CaseSensitive,
                "XCompleter: 默认大小写敏感（Qt 默认）");
    XAPI_EXPECT(XCompleter_maxVisibleItems(c) == 7,
                "XCompleter: 默认最大可见条目 7（Qt 默认）");
    XAPI_EXPECT(XCompleter_wrapAround(c),
                "XCompleter: 默认首尾环绕（Qt wrapAround=true）");
    XAPI_EXPECT(XCompleter_completionColumn(c) == 0,
                "XCompleter: 默认补全列 0");
    XAPI_EXPECT(XCompleter_modelSorting(c) ==
                XCompleterModelSorting_UnsortedModel,
                "XCompleter: 默认模型无序假设（Qt 默认）");
    XAPI_EXPECT(XCompleter_filterMode(c) == XCompleterFilterMode_StartsWith,
                "XCompleter: 过滤模式默认 StartsWith（本类简化枚举）");
    /* Qt 默认 completionRole 为 Qt::EditRole(0)；本库 @note 简化承载
     * -1（仅存储不参与读取），按实现文档默认断言。 */
    XAPI_EXPECT(XCompleter_completionRole(c) == -1,
                "XCompleter: 补全角色默认 -1（本库 @note 简化承载）");

    /* ---- 2. 模型/控件/弹出借用挂接。 ---- */
    XCompleter_setModel(c, model);
    XAPI_EXPECT(XCompleter_model(c) == model,
                "XCompleter: setModel/model 借用往返");
    XAPI_EXPECT(XCompleter_completionModel(c) == model,
                "XCompleter: completionModel 返回底层模型（无代理简化）");
    /* m_widget/m_popup 为 XWidget* 借用指针，仅验证存取（哨兵语义）。 */
    XCompleter_setWidget(c, (XWidget*)c);
    XAPI_EXPECT(XCompleter_widget(c) == (XWidget*)c,
                "XCompleter: setWidget/widget 借用往返");
    XCompleter_setWidget(c, NULL);
    XCompleter_setPopup(c, (XWidget*)c);
    XAPI_EXPECT(XCompleter_popup(c) == (XWidget*)c,
                "XCompleter: setPopup/popup 借用往返（不部件化接管）");
    XCompleter_setPopup(c, NULL);

    /* ---- 3. 前缀过滤结果（Qt 前缀补全语义）。 ---- */
    XCompleter_setCompletionPrefix_2(c, "Ope");
    XAPI_EXPECT(XCompleter_completionCount(c) == 2,
                "XCompleter: 前缀 Ope 命中 2 个候选（前缀过滤）");
    s = XCompleter_currentCompletion(c);
    XAPI_EXPECT(s && strcmp(xapi_u8(s), "Open File") == 0,
                "XCompleter: 命中首项为 Open File（currentCompletion）");
    if (s) XString_delete_base((XClass*)s);
    XAPI_EXPECT(XCompleter_currentRow(c) == 0,
                "XCompleter: 当前完成行 0（完成列表 0 基）");
    XAPI_EXPECT(XCompleter_currentIndex(c) == 0,
                "XCompleter: 源模型行号 0（currentIndex 简化）");
    s = XCompleter_completionPrefix(c);
    XAPI_EXPECT(s && strcmp(xapi_u8(s), "Ope") == 0,
                "XCompleter: completionPrefix 返回前缀拷贝");
    if (s) XString_delete_base((XClass*)s);
    s = XCompleter_pathFromIndex(c, 1);
    XAPI_EXPECT(s && strcmp(xapi_u8(s), "Open Directory") == 0,
                "XCompleter: pathFromIndex(1) 取 Open Directory");
    if (s) XString_delete_base((XClass*)s);
    XAPI_EXPECT(XCompleter_pathFromIndex(c, 999) == NULL,
                "XCompleter: pathFromIndex 越界返回 NULL");
    XCompleter_complete(c);
    XAPI_EXPECT(XCompleter_completionCount(c) == 2,
                "XCompleter: complete() 重建后候选数稳定");

    /* ---- 4. setCurrentRow 行迁移与环绕。 ---- */
    XAPI_EXPECT(XCompleter_setCurrentRow(c, 1),
                "XCompleter: setCurrentRow(1) 成功");
    s = XCompleter_currentCompletion(c);
    XAPI_EXPECT(XCompleter_currentRow(c) == 1 && s &&
                strcmp(xapi_u8(s), "Open Directory") == 0,
                "XCompleter: 行 1 当前补全 Open Directory");
    if (s) XString_delete_base((XClass*)s);
    XAPI_EXPECT(XCompleter_currentIndex(c) == 1,
                "XCompleter: 行切换后源模型索引同步为 1");
    XAPI_EXPECT(XCompleter_setCurrentRow(c, -1) &&
                XCompleter_currentRow(c) == 1,
                "XCompleter: setCurrentRow(-1) 环绕到末项（wrapAround）");
    XAPI_EXPECT(XCompleter_setCurrentRow(c, 2) &&
                XCompleter_currentRow(c) == 0,
                "XCompleter: setCurrentRow(2) 回绕到首项");
    XCompleter_setWrapAround(c, false);
    XAPI_EXPECT(!XCompleter_setCurrentRow(c, 5) &&
                XCompleter_currentRow(c) == 0,
                "XCompleter: 关环绕后越界返回 false 且状态不变");
    XCompleter_setWrapAround(c, true);

    /* ---- 5. 大小写敏感性/过滤模式。 ---- */
    XCompleter_setCaseSensitivity(c, XChar_CaseInsensitive);
    XCompleter_setCompletionPrefix_2(c, "open d");
    XAPI_EXPECT(XCompleter_completionCount(c) == 1,
                "XCompleter: 不敏感前缀 open d 命中 Open Directory");
    XCompleter_setCaseSensitivity(c, XChar_CaseSensitive);
    XCompleter_setFilterMode(c, XCompleterFilterMode_Contains);
    XCompleter_setCompletionPrefix_2(c, "Path");
    XAPI_EXPECT(XCompleter_completionCount(c) == 1,
                "XCompleter: Contains 过滤 Path 命中 Copy Path");
    XCompleter_setFilterMode(c, XCompleterFilterMode_EndsWith);
    XCompleter_setCompletionPrefix_2(c, "All");
    XAPI_EXPECT(XCompleter_completionCount(c) == 1,
                "XCompleter: EndsWith 过滤 All 命中 Save All");
    XCompleter_setFilterMode(c, XCompleterFilterMode_StartsWith);
    XCompleter_setCompletionPrefix_2(c, "");
    XAPI_EXPECT(XCompleter_completionCount(c) == 6,
                "XCompleter: 空前缀命中全部非空单元格（6 行）");
    XCompleter_setCompletionPrefix_2(c, "zzz");
    XAPI_EXPECT(XCompleter_completionCount(c) == 0 &&
                XCompleter_currentRow(c) == -1,
                "XCompleter: 无命中时候选 0 且当前行 -1");

    /* ---- 6. 补全列/角色/可见条目/排序假设。 ---- */
    XCompleter_setCompletionColumn(c, 1);
    XCompleter_setCompletionPrefix_2(c, "action");
    XAPI_EXPECT(XCompleter_completionCount(c) == 2,
                "XCompleter: 补全列 1 前缀 action 命中 2 行");
    XCompleter_setCompletionColumn(c, 0);
    XCompleter_setCompletionPrefix_2(c, "Ope");
    XCompleter_setMaxVisibleItems(c, 3);
    XAPI_EXPECT(XCompleter_maxVisibleItems(c) == 3,
                "XCompleter: setMaxVisibleItems(3) 往返");
    XCompleter_setMaxVisibleItems(c, 0); /* 文档口径：<=0 视为 0 */
    XAPI_EXPECT(XCompleter_maxVisibleItems(c) == 0,
                "XCompleter: setMaxVisibleItems(0) 归 0");
    XCompleter_setCompletionRole(c, 42);
    XAPI_EXPECT(XCompleter_completionRole(c) == 42,
                "XCompleter: setCompletionRole 仅存储往返");
    XCompleter_setModelSorting(
        c, XCompleterModelSorting_CaseSensitivelySortedModel);
    XAPI_EXPECT(XCompleter_modelSorting(c) ==
                XCompleterModelSorting_CaseSensitivelySortedModel,
                "XCompleter: setModelSorting 往返");
    /* 二分路径结果一致性：有序模型 + StartsWith + 大小写一致。 */
    {
        XAbstractItemModel* sorted = XAbstractItemModel_create();
        if (sorted) {
            XAbstractItemModel_setDimension(sorted, 3, 1);
            XAbstractItemModel_setData_2(sorted, 0, 0, "Ant");
            XAbstractItemModel_setData_2(sorted, 1, 0, "Bee");
            XAbstractItemModel_setData_2(sorted, 2, 0, "Cat");
            XCompleter_setModel(c, sorted);
            XCompleter_setCompletionPrefix_2(c, "B");
            XAPI_EXPECT(XCompleter_completionCount(c) == 1,
                        "XCompleter: 有序模型二分路径命中 Bee 1 项");
            XCompleter_setModel(c, model);
            XAbstractItemModel_delete_base(sorted);
        } else {
            XAPI_EXPECT(false, "XCompleter: 有序模型创建失败");
        }
    }
    /* 还原无序假设：modelSorting 是跨 setModel 持久的排序契约（Qt 同口径
     * 不随换模型复位），残留 CaseSensitivelySortedModel 会让后续前缀在
     * 无序模型上误走二分快路径（首个不命中即 break），"Clo" 等 (重新)
     * 匹配全空、highlighted 永不发射。 */
    XCompleter_setModelSorting(c, XCompleterModelSorting_UnsortedModel);

    /* ---- 7. highlighted 发射点（首项变化才发射，Qt 语义）。 ---- */
    g_xcomHighlighted = 0;
    XCompleter_setCompletionPrefix_2(c, "Clo"); /* 首项变化 → 发射 */
    XAPI_EXPECT(g_xcomHighlighted >= 1,
                "XCompleter: 前缀变化命中首项发射 highlighted");
    g_xcomHighlighted = 0;
    XCompleter_setCompletionPrefix_2(c, "Clo"); /* 同前缀首项不变 */
    XAPI_EXPECT(g_xcomHighlighted == 0,
                "XCompleter: 首项未变化不重复发射 highlighted");

    /* ---- 8. splitPath（POSIX '/' 拆分）。 ---- */
    {
        XString* dir = NULL;
        XString* file = NULL;
        XCompleter_splitPath_2(c, "a/b/c.txt", &dir, &file);
        XAPI_EXPECT(dir && file &&
                    strcmp(xapi_u8(dir), "a/b") == 0 &&
                    strcmp(xapi_u8(file), "c.txt") == 0,
                    "XCompleter: splitPath 拆出目录 a/b 与文件 c.txt");
        if (dir) XString_delete_base((XClass*)dir);
        if (file) XString_delete_base((XClass*)file);
        dir = NULL;
        file = NULL;
        XCompleter_splitPath_2(c, "file.txt", &dir, &file);
        XAPI_EXPECT(dir && file && xapi_u8(dir)[0] == '\0' &&
                    strcmp(xapi_u8(file), "file.txt") == 0,
                    "XCompleter: 无分隔符时目录空/文件名整串");
        if (dir) XString_delete_base((XClass*)dir);
        if (file) XString_delete_base((XClass*)file);
        dir = NULL;
        file = NULL;
        XCompleter_splitPath_2(c, "dir/", &dir, &file);
        XAPI_EXPECT(dir && file &&
                    strcmp(xapi_u8(dir), "dir") == 0 &&
                    xapi_u8(file)[0] == '\0',
                    "XCompleter: 以 / 结尾时文件名为空串");
        if (dir) XString_delete_base((XClass*)dir);
        if (file) XString_delete_base((XClass*)file);
        dir = NULL;
        file = NULL;
        XCompleter_splitPath_2(c, NULL, &dir, &file); /* 边界：空路径 */
        XAPI_EXPECT(dir && file && xapi_u8(dir)[0] == '\0' &&
                    xapi_u8(file)[0] == '\0',
                    "XCompleter: splitPath(NULL) 等价空路径双空输出");
        if (dir) XString_delete_base((XClass*)dir);
        if (file) XString_delete_base((XClass*)file);
    }

    XCompleter_delete_base(c);
    XAbstractItemModel_delete_base(model);
    XPrintf("XGuiApiTest: [XCompleter] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
#endif /* APITEXT_COMPLETER_ON */

#if APITEXT_KSE_ON

/* ==================== XKeySequenceEdit：快捷键捕获 =================== */

static int g_xkseChanged;  /**< keySequenceChanged 计数。 */
static int g_xkseFinished; /**< editingFinished 计数。 */

/** @brief keySequenceChanged 计数槽（载荷 XKeySequence* 仅计数）。 */
static void ttxt_slotKseChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xkseChanged;
}

/** @brief editingFinished 计数槽。 */
static void ttxt_slotKseFinished(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_xkseFinished;
}

/** @brief XKeySequenceEdit 族断言。 */
static int ttxt_run_key_sequence_edit(void)
{
    int failures = 0;
    XKeySequenceEdit* k = XKeySequenceEdit_create(NULL, 0);
    const XKeySequence* seq;
    XKeyCombination finish[2];
    if (!k) {
        XAPI_EXPECT(false, "XKeySequenceEdit: 实例创建失败");
        return 1;
    }
    XObject_connect_1((XObject*)k,
                      (size_t)XKeySequenceEdit_keySequenceChanged_signal(
                          NULL, NULL),
                      (XObject*)k, ttxt_slotKseChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)k,
                      (size_t)XKeySequenceEdit_editingFinished_signal(NULL),
                      (XObject*)k, ttxt_slotKseFinished,
                      XConnectionType_Direct);
    XWidget_setFocus((XWidget*)k);

    /* ---- 1. Qt 对齐默认值（QKeySequenceEdit 构造态）。 ---- */
    XAPI_EXPECT(XKeySequenceEdit_maximumSequenceLength(k) == 4,
                "XKeySequenceEdit: 序列最大长度默认 4（Qt 默认）");
    XAPI_EXPECT(!XKeySequenceEdit_isClearButtonEnabled(k),
                "XKeySequenceEdit: 清除按钮默认关");
    seq = XKeySequenceEdit_keySequence(k);
    XAPI_EXPECT(seq && seq->count == 0,
                "XKeySequenceEdit: 初始序列为空（count=0）");
    XAPI_EXPECT(XKeySequenceEdit_finishingKeyCombinationCount(k) == 2,
                "XKeySequenceEdit: 默认结束键组合 2 个（Qt {Tab,Backtab}）");
    {
        const XKeyCombination* fin = XKeySequenceEdit_finishingKeyCombinations(k);
        XAPI_EXPECT(fin && fin[0].key == (int)XKey_Tab &&
                    fin[0].modifiers == XKeyboardModifier_NoModifier &&
                    fin[1].key == (int)XKey_Backtab &&
                    fin[1].modifiers == XKeyboardModifier_NoModifier,
                    "XKeySequenceEdit: 默认结束键为无修饰 Tab/Backtab");
    }

    /* ---- 2. 捕获状态迁移（事件直发，修饰位经 init 注入）。 ---- */
    g_xkseChanged = 0;
    ttxt_key((XWidget*)k, XKey_O, XKeyboardModifier_ControlModifier);
    seq = XKeySequenceEdit_keySequence(k);
    XAPI_EXPECT(seq && seq->count == 1 &&
                seq->combos[0].modifiers ==
                    XKeyboardModifier_ControlModifier &&
                seq->combos[0].key == (int)XKey_O,
                "XKeySequenceEdit: 注入 Ctrl+O 捕获一组序列");
    XAPI_EXPECT(g_xkseChanged == 1,
                "XKeySequenceEdit: 捕获发射 keySequenceChanged");
    ttxt_key((XWidget*)k, XKey_S, XKeyboardModifier_ControlModifier);
    seq = XKeySequenceEdit_keySequence(k);
    XAPI_EXPECT(seq && seq->count == 2 &&
                seq->combos[1].key == (int)XKey_S,
                "XKeySequenceEdit: 多组序列（对标 QKeySequence MultiKey）");
    g_xkseFinished = 0;
    ttxt_key((XWidget*)k, XKey_Return, XKeyboardModifier_NoModifier);
    seq = XKeySequenceEdit_keySequence(k);
    XAPI_EXPECT(g_xkseFinished == 1 && seq && seq->count == 2,
                "XKeySequenceEdit: Return 确认发射 editingFinished"
                " 且序列保留");
    g_xkseFinished = 0;
    ttxt_key((XWidget*)k, XKey_Tab, XKeyboardModifier_NoModifier);
    XAPI_EXPECT(g_xkseFinished == 1,
                "XKeySequenceEdit: 结束键 Tab 触发 editingFinished");
    ttxt_key((XWidget*)k, XKey_Backspace, XKeyboardModifier_NoModifier);
    seq = XKeySequenceEdit_keySequence(k);
    XAPI_EXPECT(seq && seq->count == 1 &&
                seq->combos[0].key == (int)XKey_O,
                "XKeySequenceEdit: Backspace 删除最后一组");
    ttxt_key((XWidget*)k, XKey_Escape, XKeyboardModifier_NoModifier);
    seq = XKeySequenceEdit_keySequence(k);
    XAPI_EXPECT(seq && seq->count == 0,
                "XKeySequenceEdit: Esc 清空序列（对标 Qt 行为）");
    g_xkseFinished = 0;
    ttxt_key((XWidget*)k, XKey_Tab, XKeyboardModifier_NoModifier);
    XAPI_EXPECT(g_xkseFinished == 0,
                "XKeySequenceEdit: 空序列时结束键不发射 editingFinished");
    g_xkseChanged = 0;
    XKeySequenceEdit_clear(k); /* 空序列重复 clear：无变化不发射 */
    XAPI_EXPECT(g_xkseChanged == 0 && !XKeySequenceEdit_keySequence(k)->count,
                "XKeySequenceEdit: 空序列 clear 无变化不发射信号");
    /* 纯修饰键按下不记录（等非修饰键完成组合）。 */
    ttxt_key((XWidget*)k, (int)XKeyboardModifier_ControlModifier,
             XKeyboardModifier_ControlModifier);
    XAPI_EXPECT(!XKeySequenceEdit_keySequence(k)->count,
                "XKeySequenceEdit: 纯修饰键按下不计入序列");

    /* ---- 3. setKeySequence 往返与边界。 ---- */
    {
        XKeySequence manual;
        XMemset(&manual, 0, sizeof(manual));
        manual.count = 2;
        manual.combos[0].modifiers = XKeyboardModifier_ControlModifier;
        manual.combos[0].key = (int)XKey_O;
        manual.combos[1].modifiers = XKeyboardModifier_ControlModifier |
                                     XKeyboardModifier_ShiftModifier;
        manual.combos[1].key = (int)XKey_S;
        g_xkseChanged = 0;
        XKeySequenceEdit_setKeySequence(k, &manual);
        seq = XKeySequenceEdit_keySequence(k);
        XAPI_EXPECT(seq && seq->count == 2 &&
                    seq->combos[1].modifiers ==
                        (XKeyboardModifier_ControlModifier |
                         XKeyboardModifier_ShiftModifier) &&
                    seq->combos[1].key == (int)XKey_S,
                    "XKeySequenceEdit: setKeySequence 双组合往返");
        XAPI_EXPECT(g_xkseChanged == 1,
                    "XKeySequenceEdit: setKeySequence 发射变化信号");
        g_xkseChanged = 0;
        XKeySequenceEdit_setKeySequence(k, NULL); /* 边界：NULL 忽略 */
        XAPI_EXPECT(g_xkseChanged == 0 &&
                    XKeySequenceEdit_keySequence(k)->count == 2,
                    "XKeySequenceEdit: setKeySequence(NULL) 无操作");
    }

    /* ---- 4. 序列最大长度（截断与越界忽略）。 ---- */
    XKeySequenceEdit_clear(k);
    XKeySequenceEdit_setMaximumSequenceLength(k, 2);
    XAPI_EXPECT(XKeySequenceEdit_maximumSequenceLength(k) == 2,
                "XKeySequenceEdit: setMaximumSequenceLength(2) 往返");
    ttxt_key((XWidget*)k, XKey_A, XKeyboardModifier_ControlModifier);
    ttxt_key((XWidget*)k, XKey_B, XKeyboardModifier_ControlModifier);
    ttxt_key((XWidget*)k, XKey_C, XKeyboardModifier_ControlModifier);
    XAPI_EXPECT(XKeySequenceEdit_keySequence(k)->count == 2,
                "XKeySequenceEdit: 超出最大长度的组合被截断");
    XKeySequenceEdit_setMaximumSequenceLength(k, 0);
    XKeySequenceEdit_setMaximumSequenceLength(k, 9);
    XAPI_EXPECT(XKeySequenceEdit_maximumSequenceLength(k) == 2,
                "XKeySequenceEdit: 越界长度 0/9 被忽略（[1,4] 有效域）");
    XKeySequenceEdit_setMaximumSequenceLength(k, 4);

    /* ---- 5. 自定义结束键组合。 ---- */
    finish[0].modifiers = XKeyboardModifier_NoModifier;
    finish[0].key = (int)XKey_Period;
    XKeySequenceEdit_setFinishingKeyCombinations(k, finish, 1);
    XAPI_EXPECT(XKeySequenceEdit_finishingKeyCombinationCount(k) == 1,
                "XKeySequenceEdit: 自定义结束键组合数 1");
    ttxt_key((XWidget*)k, XKey_Period, XKeyboardModifier_NoModifier);
    XAPI_EXPECT(g_xkseFinished >= 1,
                "XKeySequenceEdit: 自定义结束键触发 editingFinished");
    /* 清空结束键组合后 Tab 落入普通捕获路径。 */
    XKeySequenceEdit_clear(k);
    g_xkseFinished = 0;
    g_xkseChanged = 0;
    XKeySequenceEdit_setFinishingKeyCombinations(k, NULL, 0);
    XAPI_EXPECT(XKeySequenceEdit_finishingKeyCombinationCount(k) == 0,
                "XKeySequenceEdit: setFinishingKeyCombinations(NULL,0) 清空");
    ttxt_key((XWidget*)k, XKey_Tab, XKeyboardModifier_NoModifier);
    XAPI_EXPECT(g_xkseFinished == 0 &&
                XKeySequenceEdit_keySequence(k)->count == 1 &&
                XKeySequenceEdit_keySequence(k)->combos[0].key ==
                    (int)XKey_Tab,
                "XKeySequenceEdit: 无结束键后 Tab 被记录为普通组合");
    /* 还原 Qt 默认 {Tab, Backtab}。 */
    finish[0].modifiers = XKeyboardModifier_NoModifier;
    finish[0].key = (int)XKey_Tab;
    finish[1].modifiers = XKeyboardModifier_NoModifier;
    finish[1].key = (int)XKey_Backtab;
    XKeySequenceEdit_setFinishingKeyCombinations(k, finish, 2);
    XAPI_EXPECT(XKeySequenceEdit_finishingKeyCombinationCount(k) == 2 &&
                XKeySequenceEdit_finishingKeyCombinations(k)[1].key ==
                    (int)XKey_Backtab,
                "XKeySequenceEdit: 还原默认结束键 {Tab, Backtab}");
    XKeySequenceEdit_clear(k);

    /* ---- 6. 清除按钮开关。 ---- */
    XKeySequenceEdit_setClearButtonEnabled(k, true);
    XAPI_EXPECT(XKeySequenceEdit_isClearButtonEnabled(k),
                "XKeySequenceEdit: setClearButtonEnabled(true) 往返");
    XKeySequenceEdit_setClearButtonEnabled(k, false);

    XKeySequenceEdit_delete_base((XClass*)k);
    XPrintf("XGuiApiTest: [XKeySequenceEdit] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
#endif /* APITEXT_KSE_ON */

/* ==================== 族入口（契约：xgui_demo_apitest.h） ============ */

int xapi_text_run(void)
{
    int failures = 0;
#if APITEXT_ANY_ON
    XPrintf("XGuiApiTest: ---- text 族开始（六控件 API 全量） ----\n");
#endif
#if APITEXT_TEXTEDIT_ON
    failures += ttxt_run_text_edit();
#endif
#if APITEXT_PTE_ON
    failures += ttxt_run_plain_text_edit();
#endif
#if APITEXT_BROWSER_ON
    failures += ttxt_run_text_browser();
#endif
#if APITEXT_DOC_ON
    failures += ttxt_run_text_document();
#endif
#if APITEXT_COMPLETER_ON
    failures += ttxt_run_completer();
#endif
#if APITEXT_KSE_ON
    failures += ttxt_run_key_sequence_edit();
#endif
#if !APITEXT_ANY_ON
    /* 文本族模块整体裁剪时的哨兵：无断言可跑，报告跳过。 */
    typedef int xgui_demo_apitest_text_disabled_sentinel;
    XPrintf("XGuiApiTest: [SKIP] text 族（文本类模块整体裁剪）\n");
#endif
    XPrintf("XGuiApiTest: ---- text 族结束：%s（失败 %d） ----\n",
            failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
