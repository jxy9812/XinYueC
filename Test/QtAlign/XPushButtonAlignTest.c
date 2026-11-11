/**
 * @file       XPushButtonAlignTest.c
 * @brief      XPushButton × Qt 6.8 QPushButton 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qpushbutton.h
 *             与 Qt 6.8.3 源码 …/src/widgets/widgets/qpushbutton.cpp，
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：flat=false、isDefault=false、menu=nullptr、
 *               尺寸策略 (Minimum, Fixed)（QPushButtonPrivate 的
 *               QSizePolicy::PushButton）、sizeHint==minimumSizeHint
 *               （qpushbutton.cpp:326-329）；
 *             - autoDefault 三态：无对话框父链时为 false，处于对话框内
 *               缺省（Auto）为 true（qpushbutton.cpp:307-313，探针
 *               autoDefault=1）；setAutoDefault(false) 后为 false；
 *             - 焦点迁移默认按钮：autoDefault 生效的按钮获得焦点即
 *               isDefault=true，且同对话框其它按钮默认位被清；焦点再
 *               迁移时默认位随之迁移（qpushbutton.cpp:438-476，探针
 *               f1~f4：0→1、setDefault 抢占、再聚焦互换）；
 *             - Enter/Return 在 autoDefault/isDefault 时触发 click
 *               （qpushbutton.cpp:419-433）；
 *             - hitButton 走 style()->subElementRect(SE_PushButtonBevel)
 *               （qpushbutton.cpp:499-505）：默认样式下等于控件矩形；
 *               测试用自定义样式把 bevel 四边各收 10px，锁定公共函数与
 *               事件路径（mousePress 的 down 判定）都查询样式 bevel；
 *             - 助记符 '&'：显示/度量去标记、shortcut 文本 Alt+字符、
 *               sizeHint 按显示文本度量（Qt TextShowMnemonic）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON

#include "XPushButton.h"
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XString.h"
#include "XClass.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"

#include <string.h>

/** @brief 向控件直发一次键盘事件（Enter/Return 路径）。 */
static void xpb_key(XWidget* target, XEventType type, int key)
{
    XKeyEvent ke;

    if (!target)
        return;
    XKeyEvent_init(&ke, type, key, 0);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

/** @brief 向控件直发一次鼠标按键事件（本地坐标）。 */
static void xpb_mouse(XWidget* target, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;

    if (!target)
        return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/*
 * 自定义样式：只重载 SubElementRect，把 SE_PushButtonBevel 从控件矩形
 * 四边各收 10px，用于锁定「XPushButton 的命中/事件路径确实查询
 * style()->subElementRect(SE_PushButtonBevel)」——默认 QCommonStyle 的
 * bevel == opt->rect，没有可观测差异（qcommonstyle.cpp:2495-2499）。
 */
XCLASS_DEFINE_BEGING(XPushButtonBevelTestStyle)
XCLASS_DEFINE_EXTEND_END(XPushButtonBevelTestStyle, XStyle)

typedef struct XPushButtonBevelTestStyle
{
    XStyle m_base; /**< 基类；必须第一个。 */
} XPushButtonBevelTestStyle;

static XRect xpb_bevelTestSubElementRect(XStyle* self, int subElement,
                                         const XStyleOption* option,
                                         const XWidget* widget)
{
    XRect rect;

    (void)self;
    (void)widget;
    if (option) {
        rect = option->m_rect;
        if (subElement == XStyleSE_PushButtonBevel)
            rect = XRect_adjusted(&option->m_rect, 10, 10, -10, -10);
        return rect;
    }
    XRect_init(&rect, 0, 0, 0, 0);
    return rect;
}

static XVtable* XPushButtonBevelTestStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPushButtonBevelTestStyle)
    XVTABLE_INHERIT_XCLASS(XStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_SubElementRect,
                             xpb_bevelTestSubElementRect);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上创建一个 bevel 收窄 10px 的测试样式（调用方 XClassDelete）。 */
static XPushButtonBevelTestStyle* xpb_createBevelTestStyle(void)
{
    XPushButtonBevelTestStyle* style = (XPushButtonBevelTestStyle*)
        XMemory_malloc(sizeof(XPushButtonBevelTestStyle),
                       XCLASS_DEFAULT_MEMORY_TYPE);

    if (!style)
        return NULL;
    XStyle_init(&style->m_base);
    XClassSetVtable(style, XPushButtonBevelTestStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/* ==================== ① 构造默认值 + 尺寸 + 命中 ==================== */

XQ_TEST(XPushButton, constructorDefaults)
{
    XWidget* host = XWidget_create(NULL, 0);
    XPushButton* button;
    XWidgetSizePolicy policy;
    XSize hint;
    XSize minimumHint;
    XPoint inside;
    XPoint outside;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 160);
    button = XPushButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 90, 26);

    /* 默认值（QPushButtonPrivate 构造 + QAbstractButton 基类）。 */
    XQ_CHECK(!XPushButton_isFlat(button));
    XQ_CHECK(!XPushButton_isDefault(button));
    XQ_CHECK(XPushButton_menu(button) == NULL);
    /* 无对话框父链：Auto 三态判定为 false（qpushbutton.cpp:307-313）。 */
    XQ_CHECK(!XPushButton_autoDefault(button));
    /* QPushButtonPrivate(QSizePolicy::PushButton) + init：
     * (Minimum, Fixed)（qpushbutton.cpp:630-635，探针实测）。 */
    policy = XWidget_sizePolicy((XWidget*)button);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              XWidgetSizePolicy_Minimum);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              XWidgetSizePolicy_Fixed);
    /* Qt 焦点策略 StrongFocus（SH_Button_FocusPolicy，qcommonstyle.cpp）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)button),
              XWidgetFocusPolicy_StrongFocus);

    /* sizeHint 有内容占位（空文本用 "XXXX"），minimumSizeHint 直接
     * 返回 sizeHint（qpushbutton.cpp:326-329）。 */
    hint = XPushButton_sizeHint(button);
    minimumHint = XPushButton_minimumSizeHint(button);
    XQ_CHECK(hint.width > 0 && hint.height > 0);
    XQ_EQ_I64(minimumHint.width, hint.width);
    XQ_EQ_I64(minimumHint.height, hint.height);
    XQ_EQ_I64(XWidget_sizeHint((const XWidget*)button).width, hint.width);

    /* hitButton 缺省为控件矩形包含（QPushButton 的 bevel 区域近似）。 */
    XPoint_init(&inside, 10, 10);
    XPoint_init(&outside, 200, 200);
    XQ_CHECK(XPushButton_hitButton(button, &inside));
    XQ_CHECK(!XPushButton_hitButton(button, &outside));
    XQ_CHECK(!XPushButton_hitButton(button, NULL));

    /* 文本变化经 contentChanged 槽刷新 sizeHint 存储位。 */
    XPushButton_setText_2(button, "确定");
    {
        XSize after = XPushButton_sizeHint(button);

        XQ_CHECK(after.width > 0 && after.height > 0);
        XQ_EQ_I64(XWidget_sizeHint((const XWidget*)button).width,
                  after.width);
    }

    XClassDelete((XClass*)host); /* button 随父释放（泄漏门判定） */
}

/* ==================== ② 对话框内 autoDefault/default 与焦点迁移 ==================== */

XQ_TEST(XPushButton, dialogDefaultFollowsFocus)
{
    /* Qt 的 dialogParent 判据是「父链上第一个 QDialog」（qpushbutton.cpp:
     * 253-263）；本库等价物是窗口类型为 Dialog 的顶层控件。 */
    XWidget* dialog = XWidget_create(NULL, XWindowType_Dialog);
    XPushButton* first;
    XPushButton* second;

    XQ_REQUIRE(dialog != NULL);
    XWidget_resize(dialog, 320, 200);
    first = XPushButton_create(dialog, 0);
    second = XPushButton_create(dialog, 0);
    XQ_REQUIRE(first != NULL && second != NULL);
    XWidget_setGeometry((XWidget*)first, 10, 10, 100, 26);
    XWidget_setGeometry((XWidget*)second, 10, 50, 100, 26);

    /* Auto 三态在对话框内解析为 true（探针 f0：autoDefault=1）。 */
    XQ_CHECK(XPushButton_autoDefault(first));
    XQ_CHECK(XPushButton_autoDefault(second));
    XQ_CHECK(!XPushButton_isDefault(first));
    XQ_CHECK(!XPushButton_isDefault(second));

    /* 显式关闭：Auto → Off，之后焦点迁移不再抢默认位
     * （探针 f2：autoDefault=false 的按钮聚焦后 isDefault=0）。 */
    XPushButton_setAutoDefault(second, false);
    XQ_CHECK(!XPushButton_autoDefault(second));

    /* 聚焦 autoDefault 生效的按钮 ⇒ 成为对话框默认按钮
     * （qpushbutton.cpp:441-453；探针 f1：isDefault 0→1）。 */
    XWidget_setFocus_2((XWidget*)first, XFocusReason_Tab);
    XQ_CHECK(XWidget_hasFocus((XWidget*)first));
    XQ_CHECK(XPushButton_isDefault(first));

    /* 程序化 setDefault(true) 抢占：同对话框内其它默认位被清
     * （QDialogPrivate::setMainDefault；探针 f3：b1.isDefault=0）。 */
    XPushButton_setDefault(second, true);
    XQ_CHECK(XPushButton_isDefault(second));
    XQ_CHECK(!XPushButton_isDefault(first));

    /* 焦点迁到 second（autoDefault=Off）：Qt 只在 autoDefault() 成立时
     * 于失焦侧交还默认位（qpushbutton.cpp:455-468），故 second 的显式
     * 默认位保留（探针 f3 之后 b2 仍为默认）。 */
    XWidget_setFocus_2((XWidget*)second, XFocusReason_Tab);
    XQ_CHECK(XWidget_hasFocus((XWidget*)second));
    XQ_CHECK(!XPushButton_isDefault(first));
    XQ_CHECK(XPushButton_isDefault(second));

    /* 焦点回迁 first：聚焦侧抢占 + 清同对话框其它默认位，默认位随之
     * 迁移（探针 f4：b1=1、b2=0）。 */
    XWidget_setFocus_2((XWidget*)first, XFocusReason_Tab);
    XQ_CHECK(XWidget_hasFocus((XWidget*)first));
    XQ_CHECK(XPushButton_isDefault(first));
    XQ_CHECK(!XPushButton_isDefault(second));

    /* 焦点再回到 autoDefault=Off 的 second：失焦侧 first 交还默认位，
     * 聚焦侧不抢，谁都不是默认（Qt 同口径）。 */
    XWidget_setFocus_2((XWidget*)second, XFocusReason_Tab);
    XQ_CHECK(!XPushButton_isDefault(first));
    XQ_CHECK(!XPushButton_isDefault(second));

    XClassDelete((XClass*)dialog);
}

/* ==================== ③ flat/menu 属性 + 继承的点击路径 ==================== */

XQ_TEST(XPushButton, flatMenuAndClick)
{
    XWidget* host = XWidget_create(NULL, 0);
    XPushButton* button;

    XQ_REQUIRE(host != NULL);
    button = XPushButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 100, 26);

    /* flat 往返；setMenu(NULL) 是无操作（qpushbutton.cpp:519-522 的
     * `menu == d->menu` 早退），menu() 仍为空。 */
    XQ_CHECK(!XPushButton_isFlat(button));
    XPushButton_setFlat(button, true);
    XQ_CHECK(XPushButton_isFlat(button));
    XPushButton_setFlat(button, false);
    XQ_CHECK(!XPushButton_isFlat(button));
    XPushButton_setMenu(button, NULL);
    XQ_CHECK(XPushButton_menu(button) == NULL);

    /* 继承的可选中/点击：程序化 click 翻转 selected 并发 clicked(bool)
     * （QAbstractButton 语义，顺序见 XAbstractButtonAlignTest）。 */
    XPushButton_setCheckable(button, true);
    XPushButton_click(button);
    XQ_CHECK(XPushButton_isChecked(button));
    XPushButton_click(button);
    XQ_CHECK(!XPushButton_isChecked(button));

    /* Enter/Return 在 autoDefault 生效时触发 click（qpushbutton.cpp:
     * 395-408）；本按钮无对话框父链且未设 default ⇒ 不触发。
     * 设成 default 后 Enter 触发一次。 */
    XPushButton_setCheckable(button, true);
    XPushButton_setChecked(button, false);
    xpb_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_Return);
    XQ_CHECK(!XPushButton_isChecked(button));
    XPushButton_setDefault(button, true);
    xpb_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_Return);
    XQ_CHECK(XPushButton_isChecked(button));

    /* 文本空串不影响可用性；禁用后 click 无效。 */
    XPushButton_setText_2(button, "");
    XWidget_setEnabled((XWidget*)button, false);
    XPushButton_setChecked(button, false);
    XPushButton_click(button);
    XQ_CHECK(!XPushButton_isChecked(button));
    XWidget_setEnabled((XWidget*)button, true);

    XClassDelete((XClass*)host);
}

/* ==================== ④ 命中区走样式 SE_PushButtonBevel ==================== */

XQ_TEST(XPushButton, hitButtonUsesStyleBevel)
{
    XWidget* host = XWidget_create(NULL, 0);
    XPushButton* button;
    XPushButtonBevelTestStyle* style;
    XPoint corner;
    XPoint center;
    XPoint outside;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 160);
    button = XPushButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 100, 40);
    style = xpb_createBevelTestStyle();
    XQ_REQUIRE(style != NULL);
    XWidget_setStyle((XWidget*)button, (XStyle*)style);

    XPoint_init(&corner, 2, 2);     /* 控件内、bevel(10,10,80x20) 外 */
    XPoint_init(&center, 50, 20);   /* bevel 内 */
    XPoint_init(&outside, 150, 60); /* 控件外 */

    /* 公共命中入口按样式 bevel 判定（QPushButton::hitButton）。 */
    XQ_CHECK(!XPushButton_hitButton(button, &corner));
    XQ_CHECK(XPushButton_hitButton(button, &center));
    XQ_CHECK(!XPushButton_hitButton(button, &outside));

    /* 事件路径必须走虚表覆写（VXPushButton_hitButton）：bevel 外的
     * 按下不进入 down；bevel 内按下进入 down。 */
    xpb_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 2, 2);
    XQ_CHECK(!XAbstractButton_isDown((XAbstractButton*)button));
    xpb_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 50, 20);
    XQ_CHECK(XAbstractButton_isDown((XAbstractButton*)button));
    xpb_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 50, 20);
    XQ_CHECK(!XAbstractButton_isDown((XAbstractButton*)button));

    XWidget_setStyle((XWidget*)button, NULL);
    XClassDelete((XClass*)style);
    XClassDelete((XClass*)host);
}

/* ==================== ⑤ 助记符显示/度量 + shortcut 文本 ==================== */

XQ_TEST(XPushButton, mnemonicDisplayAndSizeHint)
{
    XWidget* host = XWidget_create(NULL, 0);
    XPushButton* withMnemonic;
    XPushButton* plain;
    XSize withHint;
    XSize plainHint;
    XString* display = NULL;
    const char* text;
    const XString* shortcut;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 320, 160);
    withMnemonic = XPushButton_create(host, 0);
    plain = XPushButton_create(host, 0);
    XQ_REQUIRE(withMnemonic != NULL && plain != NULL);
    XWidget_setGeometry((XWidget*)withMnemonic, 0, 0, 100, 26);
    XWidget_setGeometry((XWidget*)plain, 0, 40, 100, 26);

    XPushButton_setText_2(withMnemonic, "&File");
    XPushButton_setText_2(plain, "File");

    /* 显示文本去 '&'（Qt TextShowMnemonic）；text() 保留原文。 */
    text = XAbstractButton_displayTextUtf8((XAbstractButton*)withMnemonic,
                                           &display);
    XQ_EQ_STR(text ? text : "", "File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    {
        const XString* raw = XAbstractButton_text(
            (const XAbstractButton*)withMnemonic);

        XQ_EQ_STR(raw ? XString_toUtf8(raw) : "", "&File");
    }

    /* sizeHint 按显示文本度量：'&' 不占宽 ⇒ 与同名无 '&' 文本一致。 */
    withHint = XPushButton_sizeHint(withMnemonic);
    plainHint = XPushButton_sizeHint(plain);
    XQ_EQ_I64(withHint.width, plainHint.width);
    XQ_EQ_I64(withHint.height, plainHint.height);

    /* shortcut 文本 = Alt+大写字符（qabstractbutton.cpp:482-506）。 */
    shortcut = XAbstractButton_shortcut((XAbstractButton*)withMnemonic);
    XQ_CHECK(shortcut != NULL);
    XQ_EQ_STR(shortcut ? XString_toUtf8(shortcut) : "", "Alt+F");

    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */
