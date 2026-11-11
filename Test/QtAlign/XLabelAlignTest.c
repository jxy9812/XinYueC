/**
 * @file       XLabelAlignTest.c
 * @brief      XLabel 与 Qt 6.8 QLabel 对齐契约测试。
 * @details    覆盖公开 API 语义与行为：
 *             - 构造默认值（Qt 6.8 qlabel.cpp 构造 + qlabel_p.h 默认位）；
 *             - 文本/数字/富文本自动检测/clear 的内容模式切换；
 *             - sizeHint / minimumSizeHint / heightForWidth / wordWrap
 *               的尺寸链条与 margin（含负值）/indent（含任意负值）；
 *             - 图片内容模式的尺寸提示与 scaledContents 无关性；
 *             - 伙伴助记符：'&' 从显示文本移除（"&&" 折叠）、显示宽与
 *               纯文本 "Name" 一致、助记键登记与焦点转交、伙伴析构自摘；
 *               助记键字符口径对齐 QKeySequence::mnemonic（任意可打印
 *               字符/大写字母/CJK 码位/空格不登记，见 mnemonicPrintableKeys）；
 *             - CJK 断行：minimumSizeHint=单字宽、字间可断、句读粘前字
 *               的行首禁则、拉丁词断词回归（cjkLineBreaking）；
 *             - 交互标志 → 焦点策略映射、程序化选区（含跨行 U+2029 与
 *               越界终点无选区）；
 *             - 焦点链（Tab 顺序）与事件过滤器登记；
 *             - linkActivated 信号经真实鼠标按下/释放路径发射；
 *             - 父子析构（父析构带走标签，泄漏门由运行器判定）。
 *
 *             对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qlabel.h 与
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/widgets/qlabel.cpp；
 *             关键行为用本机 Qt 6.8 探针实测（见各断言注释）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XLabel.h"
#include "XWidget.h"
#include "XShortcut.h"
#include "XString.h"
#include "XObject.h"
#include "XObject_Protected.h" /* XObject_event_base / eventFilter_base */
#include "XEvent.h"
#include "XEventType.h"
#include "XVarList.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XMemory.h"
#if XLAYOUT_ON
#include "XLayout.h"
#include "XBoxLayout.h"
#endif /* XLAYOUT_ON */

#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/* ==================== 通用注入工具（与真实输入同路径） ==================== */

/** @brief 向控件直发一次鼠标事件（坐标=控件本地坐标）。 */
static void xql_mouse(XWidget* target, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 标签文本的 UTF-8 借用指针（XString 是内码缓冲，必须 toUtf8）。
 * @note 空串归一：`XString_toUtf8` 对**空串**返回 NULL（XString.c:511
 *       `if (!str || XString_isEmpty_base(str)) return NULL;`），而 Qt 的
 *       `QString::toUtf8().constData()` 永远给合法指针（Qt 6.8 探针：
 *       默认 QLabel 文本 isNull=1/isEmpty=1，constData 非空 ⇒ [""]）。
 *       这里统一成 ""，让「空文本」断言比较语义而不是底层指针约定。 */
static const char* xql_text(const XLabel* label)
{
    const XString* text = XLabel_text(label);
    const char* utf8 = text ? XString_toUtf8(text) : NULL;
    return utf8 ? utf8 : "";
}

/* ==================== 事件过滤器探针（XObject 子类） ==================== */

XCLASS_DEFINE_BEGING(XLabelFilterProbe)
XCLASS_DEFINE_EXTEND_END(XLabelFilterProbe, XObject)

typedef struct XLabelFilterProbe
{
    XObject m_base;   /**< 基类；必须是第一个成员。 */
    int     m_calls;  /**< EventFilter 被调用次数。 */
} XLabelFilterProbe;

static bool VXLabelFilterProbe_eventFilter(XObject* self, XObject* watched,
                                           XEvent* event)
{
    XLabelFilterProbe* probe = (XLabelFilterProbe*)self;
    (void)watched;
    (void)event;
    if (probe) ++probe->m_calls;
    return false; /* 不消费：继续正常派发 */
}

static XVtable* XLabelFilterProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLabelFilterProbe)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VXLabelFilterProbe_eventFilter);
    return XVTABLE_DEFAULT;
}

static XLabelFilterProbe* XLabelFilterProbe_create(void)
{
    XLabelFilterProbe* probe = (XLabelFilterProbe*)XMemory_malloc(
        sizeof(XLabelFilterProbe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return NULL;
    XMemset(probe, 0, sizeof(*probe));
    XObject_init(&probe->m_base);
    XClassSetVtable(probe, XLabelFilterProbe);
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    return probe;
}

/* ==================== ① 默认值（Qt 6.8 构造默认） ==================== */

XQ_TEST(XLabel, defaults)
{
    XLabel label;
    XWidgetSizePolicy policy;
    XSize hint;
    XPixmap pixmap;

    XLabel_init(&label, NULL, 0);

    XQ_CHECK_PTR(XLabel_text(&label));
    XQ_EQ_STR(xql_text(&label), "");
    XQ_EQ_I64(XLabel_textFormat(&label), XLabelTextFormat_AutoText);
    XQ_EQ_I64(XLabel_alignment(&label),
              XAlignment_Left | XAlignment_VCenter);
    XQ_EQ_I64(XLabel_indent(&label), -1);
    XQ_EQ_I64(XLabel_margin(&label), 0);
    XQ_CHECK(!XLabel_wordWrap(&label));
    XQ_CHECK(!XLabel_hasScaledContents(&label));
    XQ_CHECK(!XLabel_openExternalLinks(&label));
    XQ_EQ_I64(XLabel_textInteractionFlags(&label),
              XLabelTextInteraction_LinksAccessibleByMouse);
    XQ_CHECK(XLabel_buddy(&label) == NULL);

    /* Qt 6.8 探针：QLabel 构造只设 sizePolicy(Preferred,Preferred,Label)，
     * 不调 setTextInteractionFlags ⇒ 默认 focusPolicy 保持 QWidget 的
     * NoFocus（探针输出 default.focusPolicy=0、default.ct=64）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)&label),
              XWidgetFocusPolicy_NoFocus);
    policy = XWidget_sizePolicy((XWidget*)&label);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              XWidgetSizePolicy_Preferred);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              XWidgetSizePolicy_Preferred);
    XQ_EQ_I64(XWidgetSizePolicy_controlType(&policy),
              XWidgetSizePolicyControl_Label);
    XQ_CHECK(!XWidgetSizePolicy_hasHeightForWidth(&policy));

    /* 空标签 = 平均字宽 x 行高（qlabel.cpp:598-600 br=QSize(averageCharWidth,
     * lineSpacing)），非零。 */
    hint = XLabel_sizeHint(&label);
    XQ_CHECK_MSG(hint.width > 0 && hint.height > 0,
                 "空标签 sizeHint 应为平均字宽×行高，实际 %dx%d",
                 hint.width, hint.height);

    pixmap = XLabel_pixmap(&label);
    XQ_CHECK(XPixmap_isNull(&pixmap));
    XClassDeinit(&pixmap);

    XClassDeinit(&label);
}

/* ==================== ② 文本/数字/格式/clear 内容模式 ==================== */

XQ_TEST(XLabel, textFormats)
{
    XLabel label;
    XLabel rich;
    XLabel plain;
    XSize richHint;
    XSize plainHint;

    XLabel_init(&label, NULL, 0);

    XLabel_setText_2(&label, "Hello 世界");
    XQ_EQ_STR(xql_text(&label), "Hello 世界");
    XLabel_setText_2(&label, NULL);
    XQ_EQ_STR(xql_text(&label), "");
    XLabel_setNum(&label, -42);
    XQ_EQ_STR(xql_text(&label), "-42");
    XLabel_setNum_2(&label, 1.5);
    XQ_EQ_STR(xql_text(&label), "1.5");

    XLabel_setTextFormat(&label, XLabelTextFormat_RichText);
    XQ_EQ_I64(XLabel_textFormat(&label), XLabelTextFormat_RichText);
    XLabel_setTextFormat(&label, XLabelTextFormat_MarkdownText);
    XQ_EQ_I64(XLabel_textFormat(&label), XLabelTextFormat_MarkdownText);
    XLabel_setTextFormat(&label, XLabelTextFormat_AutoText);
    XQ_EQ_I64(XLabel_textFormat(&label), XLabelTextFormat_AutoText);

    /* AutoText 自动检测 HTML（Qt::mightBeRichText）：同样输入
     * "<b>bold</b>"，Autotext 显示 "bold"（4 字形），PlainText 显示
     * 11 个字面字符 ⇒ 尺寸提示必然更小。 */
    XLabel_init(&rich, NULL, 0);
    XLabel_init(&plain, NULL, 0);
    XLabel_setTextFormat(&rich, XLabelTextFormat_AutoText);
    XLabel_setText_2(&rich, "<b>bold</b>");
    XLabel_setTextFormat(&plain, XLabelTextFormat_PlainText);
    XLabel_setText_2(&plain, "<b>bold</b>");
    richHint = XLabel_sizeHint(&rich);
    plainHint = XLabel_sizeHint(&plain);
    XQ_CHECK_MSG(richHint.width < plainHint.width,
                 "AutoText 应识别 HTML：rich=%d plain=%d",
                 richHint.width, plainHint.width);

    /* clear 退出文本模式：heightForWidth 回退非文本分支（Qt 探针
     * empty.hfw(50) = -1 ⇒ QLabel::heightForWidth 走 QWidget 实现）。 */
    XLabel_clear(&rich);
    XQ_EQ_STR(xql_text(&rich), "");
    XQ_EQ_I64(XLabel_heightForWidth(&rich, 100), -1);

    /* 富文本链接表在显示文本中记录（只读字段核对，后续信号用例复用）。 */
    XLabel_setText_2(&rich, "<a href=\"xq://target\">GO</a>");
    XQ_EQ_I64(rich.m_linkCount, 1);

    /* 带初始文本的构造重载（对标 QLabel(const QString&, QWidget*, flags)）。 */
    {
        XLabel withText;
        XLabel_init_2(&withText, "ctor text", NULL, 0);
        XQ_EQ_STR(xql_text(&withText), "ctor text");
        XQ_EQ_I64(XLabel_textFormat(&withText), XLabelTextFormat_AutoText);
        XClassDeinit(&withText);
    }

    XClassDeinit(&rich);
    XClassDeinit(&plain);
    XClassDeinit(&label);
}

/* ==================== ③ 尺寸链条：sizeHint/margins/indent/wrap ==================== */

XQ_TEST(XLabel, sizing)
{
    XLabel label;
    XLabel wrap;
    XSize base;
    XSize changed;
    XSize minimum;
    XWidgetSizePolicy policy;

    XLabel_init(&label, NULL, 0);
    XLabel_setText_2(&label, "abcdef");
    base = XLabel_sizeHint(&label);
    XQ_CHECK(base.width > 0 && base.height > 0);

    /* Qt sizeForWidth 的边距口径（qlabel.cpp `int hextra = 2 * margin`）：
     * 2*margin **只加一次**到宽和高。Qt 6.8 探针（QLabel "abcdef"）实测
     * margin 0 → 39x18、setMargin(3) → 45x24（+6/+6，不是 +12/+12）、
     * setMargin(-2) → 35x14（-4/-4）。 */
    XLabel_setMargin(&label, 3);
    changed = XLabel_sizeHint(&label);
    XQ_EQ_I64(changed.width, base.width + 6);
    XQ_EQ_I64(changed.height, base.height + 6);
    XQ_EQ_I64(XLabel_margin(&label), 3);

    /* Qt 6.8 探针：setMargin(-5) 回读 -5，sizeHint 21x18 → 11x8
     * （负边距向外扩内容并收缩尺寸提示）。 */
    XLabel_setMargin(&label, -2);
    changed = XLabel_sizeHint(&label);
    XQ_EQ_I64(changed.width, base.width - 4);
    XQ_EQ_I64(changed.height, base.height - 4);
    XQ_EQ_I64(XLabel_margin(&label), -2);
    XLabel_setMargin(&label, 0);

    /* Qt 的 indent 是 int：任意负值都表示自动计算且原样回读
     * （探针 indent=300 read=300；负值同理）。正值按对齐侧加缩进。 */
    XLabel_setIndent(&label, -7);
    XQ_EQ_I64(XLabel_indent(&label), -7);
    XLabel_setIndent(&label, 5);
    changed = XLabel_sizeHint(&label);
    XQ_CHECK_MSG(changed.width > base.width,
                 "Left 对齐下正缩进应加宽 sizeHint：%d vs %d",
                 changed.width, base.width);
    XLabel_setIndent(&label, -1);
    XQ_EQ_I64(XLabel_sizeHint(&label).width, base.width);

    /* wordWrap 同步策略位的 hasHeightForWidth（qlabel.cpp:1086-1093）。 */
    XLabel_init(&wrap, NULL, 0);
    XLabel_setText_2(&wrap, "AAAA BBBB CCCC DDDD EEEE FFFF");
    XQ_CHECK(!XLabel_wordWrap(&wrap));
    XQ_EQ_I64(XLabel_heightForWidth(&wrap, 40),
              XLabel_heightForWidth(&wrap, 400));
    XLabel_setWordWrap(&wrap, true);
    XQ_CHECK(XLabel_wordWrap(&wrap));
    policy = XWidget_sizePolicy((XWidget*)&wrap);
    XQ_CHECK(XWidgetSizePolicy_hasHeightForWidth(&policy));
    XQ_CHECK_MSG(XLabel_heightForWidth(&wrap, 40) >
                     XLabel_heightForWidth(&wrap, 400),
                 "换行标签窄宽应更高：40→%d 400→%d",
                 XLabel_heightForWidth(&wrap, 40),
                 XLabel_heightForWidth(&wrap, 400));
    XQ_EQ_I64(XLabel_heightForWidth(&wrap, -1),
              XLabel_sizeHint(&wrap).height);

    minimum = XLabel_minimumSizeHint(&wrap);
    XQ_CHECK_MSG(minimum.width > 0 && minimum.width <=
                     XLabel_sizeHint(&wrap).width,
                 "最小宽应为最长词宽（≤ sizeHint 宽）：%d vs %d",
                 minimum.width, XLabel_sizeHint(&wrap).width);
    XQ_CHECK(minimum.height > 0);

    XLabel_setWordWrap(&wrap, false);
    policy = XWidget_sizePolicy((XWidget*)&wrap);
    XQ_CHECK(!XWidgetSizePolicy_hasHeightForWidth(&policy));

    XClassDeinit(&wrap);
    XClassDeinit(&label);
}

/* ==================== ④ 图片内容模式 ==================== */

XQ_TEST(XLabel, pixmapContent)
{
    XLabel label;
    XImage image;
    XPixmap pixmap;
    XSize hint;

    XImage_init_ex(&image, 40, 10, XImageFormat_ARGB32);
    XImage_fill(&image, 0xffff0000u);
    XPixmap_init(&pixmap);
    XPixmap_fromImage(&image, 0u, &pixmap);
    XQ_CHECK(!XPixmap_isNull(&pixmap));

    XLabel_init(&label, NULL, 0);
    XLabel_setText_2(&label, "将被像素图替换");
    XLabel_setPixmap(&label, &pixmap);
    /* Qt setPixmap → clearContents：文本被清空（qlabel.cpp:304-313）。 */
    XQ_EQ_STR(xql_text(&label), "");
    hint = XLabel_sizeHint(&label);
    XQ_EQ_I64(hint.width, 40);
    XQ_EQ_I64(hint.height, 10);

    /* Qt 探针：setScaledContents(true) 不改变 sizeHint（40x10 不变），
     * 缩放只影响绘制；非文本标签 heightForWidth 走 QWidget → -1。 */
    XLabel_setScaledContents(&label, true);
    XQ_CHECK(XLabel_hasScaledContents(&label));
    hint = XLabel_sizeHint(&label);
    XQ_EQ_I64(hint.width, 40);
    XQ_EQ_I64(hint.height, 10);
    XQ_EQ_I64(XLabel_heightForWidth(&label, 100), -1);

    /* setPixmap(NULL) 仅清空（Qt：清内容后 pixmap 为空）。 */
    XLabel_setPixmap(&label, NULL);
    XQ_EQ_STR(xql_text(&label), "");
    hint = XLabel_sizeHint(&label);
    XQ_CHECK(hint.width > 0 && hint.height > 0);

    XClassDeinit(&label);
    XClassDeinit(&pixmap);
    XClassDeinit(&image);
}

/* ==================== ⑤ 伙伴助记符（'&' 语义 + 焦点转交） ==================== */

XQ_TEST(XLabel, mnemonicBuddy)
{
    XWidget* top = XWidget_create(NULL, 0);
    XLabel* label = XLabel_create(top, 0);
    XLabel* reference = XLabel_create(NULL, 0);
    XWidget* buddy = XWidget_create(top, 0);
    XWidget* other = XWidget_create(top, 0);
    XShortcut* shortcut;
    XString* selected;
    XSize withAmp;
    XSize stripped;
    XSize restored;

    XQ_REQUIRE(top != NULL && label != NULL && reference != NULL &&
               buddy != NULL && other != NULL);

    XWidget_setGeometry((XWidget*)label, 0, 0, 200, 40);
    XWidget_setFocusPolicy(buddy, XWidgetFocusPolicy_StrongFocus);
    XWidget_setFocusPolicy(other, XWidgetFocusPolicy_StrongFocus);
    XWidget_show(buddy);
    XWidget_show(other);

    XLabel_setText_2(label, "&Name");
    withAmp = XLabel_sizeHint(label);
    XLabel_setText_2(reference, "Name");
    stripped = XLabel_sizeHint(reference);
    XQ_CHECK_MSG(withAmp.width > stripped.width,
                 "无伙伴时 '&' 参与显示宽度：%d vs %d",
                 withAmp.width, stripped.width);

    XLabel_setBuddy(label, buddy);
    XQ_CHECK(XLabel_buddy(label) == buddy);
    /* Qt 6.8 探针：setBuddy 后显示文本去掉 '&'（selectedText 为 "Name"），
     * 因此尺寸提示与字面 "Name" 完全一致。 */
    restored = XLabel_sizeHint(label);
    XQ_EQ_I64(restored.width, stripped.width);
    /* text() 保留原始文本（Qt 探针 mnemonic.text=[&Name]）。 */
    XQ_EQ_STR(xql_text(label), "&Name");

    /* 选区坐标按显示文本（已去 '&'）：Qt 探针 setSelection(0,4) →
     * "Name"；setSelection(0,5) 终点越界，QTextCursor 拒绝 KeepAnchor
     * ⇒ 无选区（探针 hasSelected=0 / selectionStart=-1）。 */
    XLabel_setTextInteractionFlags(
        label, (XLabelTextInteractionFlags)
                   XLabelTextInteraction_TextSelectableByMouse);
    XLabel_setSelection(label, 0, 4);
    XQ_CHECK(XLabel_hasSelectedText(label));
    XQ_EQ_I64(XLabel_selectionStart(label), 0);
    selected = XLabel_selectedText(label);
    XQ_EQ_STR(selected ? XString_toUtf8(selected) : "(null)", "Name");
    if (selected) XClassDelete((XClass*)selected);
    XLabel_setSelection(label, 0, 5);
    XQ_CHECK_MSG(!XLabel_hasSelectedText(label),
                 "越界终点应得到无选区（Qt QTextCursor::setPosition 拒绝）");
    XQ_EQ_I64(XLabel_selectionStart(label), -1);

    /* 助记键登记与焦点转交。键码口径对齐 Qt QKeySequence::mnemonic：
     * 字母取 QChar::toUpper() 后的码位（Qt 6.8 探针 mnemonic("&Name")
     * → Key 0x4e / ALT），与平台按键归一一致——X11/Win32 后端都把
     * 字母键上报为大写码位（XPlatformNativeWindow_posix.c 问题 #32
     * 归一、win32 translateKey 的 'A'..'Z' 分支）。 */
    XWidget_setFocus(other);
    XQ_CHECK(XWidget_hasFocus(other));
    shortcut = XShortcut_match((int)XKey_N, XShortcutContext_WindowShortcut,
                               other);
    XQ_CHECK_MSG(shortcut != NULL, "伙伴助记符应登记 'N' 快捷键");
    /* 小写键码不是平台上报口径：X11/Win32 后端把字母键归一为大写
     * （XEvent.h XKey_T=0x54 即大写口径），故小写裸键不命中。 */
    XQ_CHECK(XShortcut_match((int)XKey_n, XShortcutContext_WindowShortcut,
                             other) == NULL);
    if (shortcut)
        XShortcut_activate(shortcut);
    XQ_CHECK_MSG(XWidget_hasFocus(buddy),
                 "助记键激活应把焦点交给伙伴（qlabel.cpp:926-940）");

    /* 清除伙伴：显示文本恢复字面 '&'，快捷键注销。 */
    XLabel_setBuddy(label, NULL);
    XQ_CHECK(XLabel_buddy(label) == NULL);
    restored = XLabel_sizeHint(label);
    XQ_EQ_I64(restored.width, withAmp.width);
    XQ_CHECK(XShortcut_match((int)XKey_N, XShortcutContext_WindowShortcut,
                             other) == NULL);

    /* 伙伴析构：自摘连接生效（buddy() 不悬垂）且快捷键注销。 */
    XLabel_setBuddy(label, buddy);
    XQ_CHECK(XLabel_buddy(label) == buddy);
    XClassDelete((XClass*)buddy);
    XQ_CHECK_MSG(XLabel_buddy(label) == NULL,
                 "伙伴销毁后 buddy() 应自动置空（QPointer 语义）");
    XQ_CHECK(XShortcut_match((int)XKey_N, XShortcutContext_WindowShortcut,
                             other) == NULL);
    XQ_EQ_STR(xql_text(label), "&Name");

    /* 父控件析构带走标签与其余子控件（泄漏门由运行器判定）。 */
    XClassDelete((XClass*)reference);
    XClassDelete((XClass*)top);
}

/* ==================== ⑤b 助记键可打印字符口径（QKeySequence::mnemonic） ==================== */

XQ_TEST(XLabel, mnemonicPrintableKeys)
{
    XWidget* top = XWidget_create(NULL, 0);
    XLabel* punct = XLabel_create(top, 0);
    XLabel* cjk = XLabel_create(top, 0);
    XLabel* doubled = XLabel_create(top, 0);
    XLabel* trailing = XLabel_create(top, 0);
    XLabel* spaceMn = XLabel_create(top, 0);
    XWidget* other = XWidget_create(top, 0);

    XQ_REQUIRE(top != NULL && punct != NULL && cjk != NULL &&
               doubled != NULL && trailing != NULL && spaceMn != NULL &&
               other != NULL);
    XWidget_setFocusPolicy(other, XWidgetFocusPolicy_StrongFocus);
    XWidget_show(other);
    XWidget_setFocus(other);
    XQ_CHECK(XWidget_hasFocus(other));

    /* Qt 6.8 探针（本机 Qt 6.8.0 offscreen）：
     *   mnemonic("&!File") → Key 0x21（'!'）      —— 任意可打印字符
     *   mnemonic("&1Open") → Key 0x31（'1'）
     *   mnemonic("&中abc") → Key 0x4e2d（'中'）
     *   mnemonic("&&Name") → 空序列（'&' 后的 '&' 不是助记字符）
     *   mnemonic("name&")  → 空序列（'&' 后无字符）
     *   mnemonic("& ab")   → Key 0x20（空格也是 isPrint）
     * 本层不承载修饰位（缺口 52 deferred），只登记裸键码；除空格外
     * 任意可打印字符都应登记，显示文本按 ensureTextPopulated 去 '&'。 */

    /* 标点助记符。 */
    XLabel_setText_2(punct, "&!File");
    XLabel_setBuddy(punct, other);
    XQ_CHECK_MSG(XShortcut_match((int)XKey_Exclam,
                                 XShortcutContext_WindowShortcut,
                                 other) != NULL,
                 "标点助记符 '!' 应登记快捷键（Qt Key_Exclam 0x21）");
    XQ_EQ_STR(XString_toUtf8(punct->m_displayText), "!File");

    /* CJK 助记符：XKey 编码与 Qt 相同（<0x01000000 即 Unicode 码位），
     * 登记 U+4E2D 而不是只画下划线。 */
    XLabel_setText_2(cjk, "&\xE4\xB8\xAD" "abc");
    XLabel_setBuddy(cjk, other);
    XQ_CHECK_MSG(XShortcut_match(0x4E2D,
                                 XShortcutContext_WindowShortcut,
                                 other) != NULL,
                 "CJK 助记符应登记 U+4E2D 键码");
    XQ_EQ_STR(XString_toUtf8(cjk->m_displayText), "\xE4\xB8\xAD" "abc");

    /* "&&Name"：折叠出的字面 '&' 不是助记符（Qt mnemonic 空序列），
     * 既不登记 'N' 也不登记 '&'。 */
    XLabel_setText_2(doubled, "&&Name");
    XLabel_setBuddy(doubled, other);
    XQ_CHECK(XShortcut_match((int)XKey_N, XShortcutContext_WindowShortcut,
                             other) == NULL);
    XQ_CHECK(XShortcut_match((int)XKey_Ampersand,
                             XShortcutContext_WindowShortcut, other) == NULL);
    XQ_EQ_STR(XString_toUtf8(doubled->m_displayText), "&Name");

    /* 末尾单个 '&'：显示文本删除它，且无助记字符可登记。 */
    XLabel_setText_2(trailing, "name&");
    XLabel_setBuddy(trailing, other);
    XQ_EQ_STR(XString_toUtf8(trailing->m_displayText), "name");
    XQ_CHECK(XShortcut_match((int)XKey_N, XShortcutContext_WindowShortcut,
                             other) == NULL);
    XQ_CHECK(trailing->m_mnemonicStart < 0);

    /* 空格助记符：Qt 生成 Alt+Space；本层无修饰位，裸空格会被按钮的
     * 空格激活/文本输入吞掉，故不登记（intentional：见文件头受限项
     * 与缺口 52）。hasShortcut 的下划线位置仍按 Qt 记录。 */
    XLabel_setText_2(spaceMn, "& ab");
    XLabel_setBuddy(spaceMn, other);
    XQ_EQ_STR(XString_toUtf8(spaceMn->m_displayText), " ab");
    XQ_EQ_I64(spaceMn->m_mnemonicStart, 0);
    XQ_EQ_I64(spaceMn->m_mnemonicLen, 1);
    XQ_CHECK_MSG(XShortcut_match((int)XKey_Space,
                                 XShortcutContext_WindowShortcut,
                                 other) == NULL,
                 "空格助记符不登记裸键（Qt 需 Alt+Space）");

    XClassDelete((XClass*)top); /* 全部标签随父释放，快捷键随绑定注销 */
}

/* ==================== ③b CJK 断行（Qt QTextLayout 断行机会） ==================== */

XQ_TEST(XLabel, cjkLineBreaking)
{
    XLabel cjk;
    XLabel single;
    XLabel two;
    XLabel punct;
    XLabel latinWord;
    XLabel latinRef;
    XLabel longWord;
    XLabel latinA;
    XSize cjkHint;
    XSize cjkMin;
    XSize singleHint;
    XSize twoHint;
    XSize punctMin;
    XSize latinMin;
    XSize latinRefHint;
    int lineH;
    int charW;
    int advA;

    XLabel_init(&cjk, NULL, 0);
    XLabel_init(&single, NULL, 0);
    XLabel_init(&two, NULL, 0);
    XLabel_init(&punct, NULL, 0);
    XLabel_init(&latinWord, NULL, 0);
    XLabel_init(&latinRef, NULL, 0);
    XLabel_init(&longWord, NULL, 0);
    XLabel_init(&latinA, NULL, 0);

    /* ---- CJK 无空格文本：断行机会在字与字之间 ---- */
    XLabel_setWordWrap(&cjk, true);
    XLabel_setText_2(&cjk, "中文中文中文中文"); /* 8 字、无空格 */
    XLabel_setText_2(&single, "中");
    cjkHint = XLabel_sizeHint(&cjk);
    cjkMin = XLabel_minimumSizeHint(&cjk);
    singleHint = XLabel_sizeHint(&single);
    charW = singleHint.width;
    lineH = singleHint.height;
    XQ_CHECK(charW > 0 && lineH > 0);
    XQ_CHECK_MSG(cjkHint.width >= 4 * charW,
                 "无空格 CJK 串 sizeHint 应整行宽：%d（单字 %d）",
                 cjkHint.width, charW);
    /* Qt 6.8 探针：QLabel("中文中文中文中文", wordWrap=true) 的
     * sizeHint=96x18、minimumSizeHint=12x18 —— 最小宽 = 单字宽，
     * 修复前本实现按「整串无空格 = 一个词」给出 96 宽。 */
    XQ_EQ_I64(cjkMin.width, charW);
    XQ_EQ_I64(cjkMin.height, lineH);
    XQ_CHECK_MSG(cjkMin.width < cjkHint.width,
                 "CJK minimumSizeHint 宽应为单字宽而非整串宽：%d vs %d",
                 cjkMin.width, cjkHint.width);
    /* 换行行数：4 字宽一行 → 2 行；2 字宽一行 → 4 行；整串宽 → 1 行
     * （Qt 探针同口径：hfw(40)=54 三行、hfw(20)=144 八行，只是字体
     * 度量不同）。 */
    XQ_EQ_I64(XLabel_heightForWidth(&cjk, charW * 4), lineH * 2);
    XQ_EQ_I64(XLabel_heightForWidth(&cjk, charW * 2), lineH * 4);
    XQ_EQ_I64(XLabel_heightForWidth(&cjk, cjkHint.width), lineH);
    /* 极窄宽度（小于单字宽）：每单元一行且不追加空行（Qt 探针
     * hfw(1..14) 恒 144 = 8 行 × 18）。 */
    XQ_EQ_I64(XLabel_heightForWidth(&cjk, 1), lineH * 8);

    /* ---- 行首禁则：句读/闭括号粘前字，不得落行首 ---- */
    XLabel_setWordWrap(&punct, true);
    XLabel_setText_2(&punct, "你好，世界。");
    XLabel_setText_2(&two, "好，");
    punctMin = XLabel_minimumSizeHint(&punct);
    twoHint = XLabel_sizeHint(&two);
    /* Qt 6.8 探针（宽度 30px）："你好，世界。测试" 切分为
     * [你][好，][世][界。][测试] —— '，'/'。' 都跟在前字之后。 */
    XQ_EQ_I64(punctMin.width, twoHint.width);
    XQ_CHECK_MSG(punctMin.width > singleHint.width,
                 "闭标点应粘前字（最小宽=两字宽 %d，单字 %d）",
                 punctMin.width, singleHint.width);
    XQ_EQ_I64(XLabel_heightForWidth(&punct, 2 * charW), lineH * 4);

    /* Unicode SY：路径斜线不落行首（Qt 探针 "菜单/中央标签/左右停靠面板"
     * @12px → [菜][单/][中]…，'/' 恒粘前字）。 */
    {
        XLabel slash;
        XSize slashMin;
        XLabel_init(&slash, NULL, 0);
        XLabel_setWordWrap(&slash, true);
        XLabel_setText_2(&slash, "菜单/中央");
        slashMin = XLabel_minimumSizeHint(&slash);
        XQ_CHECK_MSG(slashMin.width > singleHint.width,
                     "斜线应粘前字（最小宽 %d，单字 %d）",
                     slashMin.width, singleHint.width);
        XClassDeinit(&slash);
    }

    /* 行尾禁则：开括号不落行尾、闭括号不落行首（双向禁则）。
     * Qt 探针 "测试（中文）结束" @30px 切成 [测试][（中][文）][结束]。
     * 断言口径与字形宽度无关：粘连后的单元宽 = 括号宽 + 字宽，必然
     * **严格大于**两者各自的宽度（修复前逐字断行时最小宽只是单字宽）。 */
    {
        XLabel openParen;
        XLabel closeParen;
        XLabel openGlued;
        XLabel closeGlued;
        XSize openParenHint;
        XSize closeParenHint;
        XSize openGluedMin;
        XSize closeGluedMin;
        int64_t openFloor;
        int64_t closeFloor;
        XLabel_init(&openParen, NULL, 0);
        XLabel_init(&closeParen, NULL, 0);
        XLabel_init(&openGlued, NULL, 0);
        XLabel_init(&closeGlued, NULL, 0);
        XLabel_setText_2(&openParen, "（");
        XLabel_setText_2(&closeParen, "）");
        XLabel_setWordWrap(&openGlued, true);
        XLabel_setText_2(&openGlued, "（中");
        XLabel_setWordWrap(&closeGlued, true);
        XLabel_setText_2(&closeGlued, "中）");
        openParenHint = XLabel_sizeHint(&openParen);
        closeParenHint = XLabel_sizeHint(&closeParen);
        openGluedMin = XLabel_minimumSizeHint(&openGlued);
        closeGluedMin = XLabel_minimumSizeHint(&closeGlued);
        openFloor = openParenHint.width > singleHint.width
                        ? openParenHint.width : singleHint.width;
        closeFloor = closeParenHint.width > singleHint.width
                         ? closeParenHint.width : singleHint.width;
        XQ_CHECK_MSG((int64_t)openGluedMin.width > openFloor,
                     "开括号应粘后字：最小宽 %d 应大于单字/括号宽 %lld",
                     openGluedMin.width, (long long)openFloor);
        XQ_CHECK_MSG((int64_t)closeGluedMin.width > closeFloor,
                     "闭括号应粘前字：最小宽 %d 应大于单字/括号宽 %lld",
                     closeGluedMin.width, (long long)closeFloor);
        XClassDeinit(&closeGlued);
        XClassDeinit(&openGlued);
        XClassDeinit(&closeParen);
        XClassDeinit(&openParen);
    }

    /* CJK 与拉丁相邻处也是断行机会（Qt 探针 "ab中文cd" @30px →
     * [ab中][文cd]）：最小宽不再是整串宽。 */
    {
        XSize mixedHint;
        XSize mixedMin;
        XLabel_setText_2(&cjk, "ab\xE4\xB8\xAD\xE6\x96\x87" "cd");
        mixedHint = XLabel_sizeHint(&cjk);
        mixedMin = XLabel_minimumSizeHint(&cjk);
        XQ_CHECK_MSG(mixedMin.width < mixedHint.width,
                     "CJK/拉丁相邻可断（最小宽 %d < 整串宽 %d）",
                     mixedMin.width, mixedHint.width);
        XQ_CHECK(mixedMin.width > 0);
    }

    /* ---- 拉丁文本回归：仍按空格断词、最小宽 = 最长词宽 ---- */
    XLabel_setWordWrap(&latinWord, true);
    XLabel_setText_2(&latinWord, "AAAA BBBB");
    /* 参考词必须取**最长**的那个：缺省字体下 'B' 比 'A' 宽（实测
     * "BBBB"=44、"AAAA"=40；2026-11-11 探针 XLabel_sizeHint 逐词核对）。
     * 原先参考串写 "AAAA" ⇒ 误判最小宽 44 为偏差。 */
    XLabel_setText_2(&latinRef, "BBBB");
    latinMin = XLabel_minimumSizeHint(&latinWord);
    latinRefHint = XLabel_sizeHint(&latinRef);
    XQ_EQ_I64(latinMin.width, latinRefHint.width);
    /* 超宽单词仍逐码点硬切（WordWrap 下 Qt 让长词溢出的既有偏差，
     * 本实现保留以免绘制越出控件矩形）。 */
    XLabel_setWordWrap(&longWord, true);
    XLabel_setText_2(&longWord, "AAAAAAAA");
    XLabel_setText_2(&latinA, "A");
    advA = XLabel_sizeHint(&latinA).width;
    XQ_CHECK(advA > 0);
    XQ_EQ_I64(XLabel_heightForWidth(&longWord, advA * 4), lineH * 2);

    XClassDeinit(&latinA);
    XClassDeinit(&longWord);
    XClassDeinit(&latinRef);
    XClassDeinit(&latinWord);
    XClassDeinit(&punct);
    XClassDeinit(&two);
    XClassDeinit(&single);
    XClassDeinit(&cjk);
}

/* ==================== ⑥ 交互标志 / 焦点策略 / 选区 ==================== */

XQ_TEST(XLabel, interactionAndSelection)
{
    XLabel label;
    XString* selected;

    XLabel_init(&label, NULL, 0);
    XLabel_setText_2(&label, "AB\nCD");

    /* Qt 探针：TextSelectableByMouse → ClickFocus(2)；LinksAccessibleBy
     * Keyboard → StrongFocus(11)；NoTextInteraction → NoFocus(0)；
     * 仅 LinksAccessibleByMouse → NoFocus。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)&label),
              XWidgetFocusPolicy_NoFocus);
    XLabel_setTextInteractionFlags(
        &label, (XLabelTextInteractionFlags)
                    XLabelTextInteraction_TextSelectableByMouse);
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)&label),
              XWidgetFocusPolicy_ClickFocus);
    XLabel_setTextInteractionFlags(
        &label, (XLabelTextInteractionFlags)
                    XLabelTextInteraction_LinksAccessibleByKeyboard);
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)&label),
              XWidgetFocusPolicy_StrongFocus);
    XLabel_setTextInteractionFlags(&label,
                                   (XLabelTextInteractionFlags)
                                       XLabelTextInteraction_NoTextInteraction);
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)&label),
              XWidgetFocusPolicy_NoFocus);

    /* 程序化选区：UTF-16 码元偏移；跨行以 U+2029 返回
     * （qlabel.cpp:752-756 的 QTextCursor::selectedText 约定）。 */
    XLabel_setTextInteractionFlags(
        &label, (XLabelTextInteractionFlags)
                    XLabelTextInteraction_TextSelectableByKeyboard);
    XLabel_setSelection(&label, 0, 5);
    XQ_CHECK(XLabel_hasSelectedText(&label));
    XQ_EQ_I64(XLabel_selectionStart(&label), 0);
    selected = XLabel_selectedText(&label);
    XQ_EQ_STR(selected ? XString_toUtf8(selected) : "(null)",
              "AB\xE2\x80\xA9" "CD");
    if (selected) XClassDelete((XClass*)selected);

    XLabel_setSelection(&label, -1, -1);
    XQ_CHECK(!XLabel_hasSelectedText(&label));
    XQ_EQ_I64(XLabel_selectionStart(&label), -1);

    /* setText 同值早退（qlabel.cpp:231-232 `if (d->text == text) return;`）：
     * 再次设置同一文本不得清掉既有选区。 */
    XLabel_setSelection(&label, 0, 2);
    XQ_CHECK(XLabel_hasSelectedText(&label));
    XLabel_setText_2(&label, "AB\nCD");
    XQ_CHECK_MSG(XLabel_hasSelectedText(&label),
                 "setText 同值应早退、保留既有选区");

    XClassDeinit(&label);
}

/* ==================== ⑦ 焦点链与事件过滤器 ==================== */

XQ_TEST(XLabel, focusChainAndFilter)
{
    XWidget* top = XWidget_create(NULL, 0);
    XLabel* first = XLabel_create(top, 0);
    XWidget* second = XWidget_create(top, 0);
    XLabelFilterProbe* filter = XLabelFilterProbe_create();
    XEvent event;

    XQ_REQUIRE(top != NULL && first != NULL && second != NULL &&
               filter != NULL);

    XWidget_setGeometry((XWidget*)first, 0, 0, 100, 30);
    XWidget_setGeometry(second, 0, 40, 100, 30);
    XWidget_setFocusPolicy((XWidget*)first,
                           XWidgetFocusPolicy_StrongFocus);
    XWidget_setFocusPolicy(second, XWidgetFocusPolicy_StrongFocus);
    XWidget_show((XWidget*)first);
    XWidget_show(second);

    /* Tab 链：setTabOrder 建立显式单跳；focusNextChild 落到下一候选。 */
    XWidget_setTabOrder((XWidget*)first, second);
    XQ_CHECK(XWidget_nextInFocusChain((XWidget*)first) == second);
    XWidget_setFocus((XWidget*)first);
    XQ_CHECK(XWidget_hasFocus((XWidget*)first));
    XQ_CHECK(XWidget_focusNextChild((XWidget*)first));
    XQ_CHECK(XWidget_focusWidget(top) == second);

    /* 事件过滤器：登记进 m_filters，过滤器对象的 EventFilter 槽被调用，
     * removeEventFilter 后列表清空（Qt installEventFilter 语义）。 */
    XObject_installEventFilter((XObject*)first, (XObject*)filter);
    XQ_CHECK(((XObject*)first)->m_filters != NULL);
    if (((XObject*)first)->m_filters) {
        XQ_EQ_SIZE(XContainer_size_base(
                       (const XContainer*)((XObject*)first)->m_filters),
                   1);
    }
    XEvent_init(&event, XEVENT_TYPE_USER);
    XQ_CHECK(!XObject_eventFilter_base((XObject*)filter, (XObject*)first,
                                       &event));
    XQ_EQ_I64(filter->m_calls, 1);
    XClassDeinit((XClass*)&event);
    XObject_removeEventFilter((XObject*)first, (XObject*)filter);
    XQ_CHECK_MSG(((XObject*)first)->m_filters == NULL ||
                     XContainer_size_base((const XContainer*)
                                              ((XObject*)first)->m_filters) == 0u,
                 "removeEventFilter 后过滤器列表应为空");

    XClassDelete((XClass*)filter);
    XClassDelete((XClass*)top); /* first/second 随父释放 */
}

/* ==================== ⑦b 布局接管 / 摘挂 ==================== */

#if XLAYOUT_ON
XQ_TEST(XLabel, layoutAdoption)
{
    XWidget* top = XWidget_create(NULL, 0);
    XLabel* label = XLabel_create(top, 0);
    XBoxLayout* borrowed = XBoxLayout_create(XBoxLayoutDirection_TopToBottom,
                                             NULL);

    XQ_REQUIRE(top != NULL && label != NULL && borrowed != NULL);
    XWidget_setGeometry(top, 0, 0, 200, 120);

    /* 借用版挂接：布局归调用方，标签被布局纳管（不改变父子关系）。 */
    XBoxLayout_addWidget(borrowed, (XWidget*)label);
    XWidget_setLayout(top, (XLayout*)borrowed);
    XQ_CHECK(XWidget_layout_const(top) == (const XLayout*)borrowed);
    XQ_CHECK(XWidget_parentWidget((XWidget*)label) == top);

    /* 摘挂：Qt 6.8 的 setLayout(NULL) **不摘挂**（qwidget.cpp:10289-10294
     * 打印 "QWidget::setLayout: Cannot set layout to 0" 并保持原布局；
     * takeLayout 是私有入口）。本框架逐条对齐该告警分支 ⇒ 断言布局仍在；
     * 随后释放借用布局，其析构经 XLayout_detachWidget 反清宿主槽位
     * （VXLayout_deinit，XLayout.c:640-643）。 */
    XWidget_setLayout(top, NULL);
    XQ_CHECK(XWidget_layout_const(top) == (const XLayout*)borrowed);
    XClassDelete((XClass*)borrowed);
    XQ_CHECK(XWidget_layout_const(top) == NULL);

    /* 接管版：setLayout_ref 把所有权交给控件，父析构时释放（泄漏门判定）。 */
    {
        XBoxLayout* owned = XBoxLayout_create(
            XBoxLayoutDirection_TopToBottom, NULL);
        XQ_CHECK_PTR(owned);
        if (owned) {
            XWidget_setLayout_ref(top, (XLayout*)owned);
            XQ_CHECK(XWidget_layout_const(top) == (const XLayout*)owned);
        }
    }

    XClassDelete((XClass*)top); /* label 与接管的布局随父释放 */
}
#endif /* XLAYOUT_ON */

/* ==================== ⑧ linkActivated 信号 + 父子析构 ==================== */

static int g_xqlLinkActivated;
static char g_xqlLastLink[64];

static void xql_linkActivatedSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, XString*, link);
    (void)receiver;
    ++g_xqlLinkActivated;
    g_xqlLastLink[0] = '\0';
    if (link) {
        const char* utf8 = XString_toUtf8(link);
        if (utf8) {
            strncpy(g_xqlLastLink, utf8, sizeof(g_xqlLastLink) - 1u);
            g_xqlLastLink[sizeof(g_xqlLastLink) - 1u] = '\0';
        }
    }
}

XQ_TEST(XLabel, signalAndOwnership)
{
    XWidget* top = XWidget_create(NULL, 0);
    XLabel* label = XLabel_create(top, 0);
    const XVector* children;

    XQ_REQUIRE(top != NULL && label != NULL);

    XWidget_setGeometry((XWidget*)label, 0, 0, 160, 32);
    XLabel_setText_2(label, "<a href=\"xq://signal\">CLICK</a>");
    XQ_EQ_I64(label->m_linkCount, 1);

    g_xqlLinkActivated = 0;
    g_xqlLastLink[0] = '\0';
    XObject_connect_1((XObject*)label,
                      (size_t)XLabel_linkActivated_signal(NULL, NULL),
                      (XObject*)label, xql_linkActivatedSlot,
                      XConnectionType_Direct);

    /* 链接上按下再释放于同一链接 ⇒ linkActivated(href)（qlabel.cpp:836-840
     * 的事件路径；坐标取首个字形带 y=16，与既有 label apitest 同口径）。 */
    xql_mouse((XWidget*)label, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 4, 16);
    xql_mouse((XWidget*)label, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 4, 16);
    XQ_EQ_I64(g_xqlLinkActivated, 1);
    XQ_EQ_STR(g_xqlLastLink, "xq://signal");

    /* 父子所有权：标签挂在 top 的 children 里，父析构带走子对象。 */
    children = XObject_children((XObject*)top);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1);

    XClassDelete((XClass*)top); /* label 随父释放（泄漏门判定） */
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
