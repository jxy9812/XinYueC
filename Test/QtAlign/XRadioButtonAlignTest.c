/**
 * @file       XRadioButtonAlignTest.c
 * @brief      XRadioButton × Qt 6.8 QRadioButton 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qradiobutton.h
 *             与 Qt 6.8.3 源码 …/src/widgets/widgets/qradiobutton.cpp，
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：checkable=true、autoExclusive=true
 *               （qradiobutton.cpp:32-41；探针 h0：1/1）、鼠标跟踪开、
 *               前景角色 WindowText；
 *             - 同一父控件内的可选中单选按钮自动互斥：选中 B 会取消 A
 *               （探针 h1：A=0、B=1）；
 *             - 独占组内当前选中项再点击不改变状态（Qt 的 changeState
 *               闸，qabstractbutton.cpp:344-375 + 593-604；探针 h2：
 *               A=0、B=1）；
 *             - 关闭 autoExclusive 后可经 setChecked(false) 反选
 *               （探针 h3：B=0）；
 *             - hitButton 命中控件矩形（Qt 为 SE_RadioButtonClickRect）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON

#include "XRadioButton.h"
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget.h"
#include "XObject.h"
#include "XPalette.h"
#include "XClass.h"

/* ==================== ① 构造默认值 ==================== */

XQ_TEST(XRadioButton, constructorDefaults)
{
    XWidget* host = XWidget_create(NULL, 0);
    XRadioButton* radio;
    XSize hint;
    XPoint inside;
    XPoint outside;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 120);
    radio = XRadioButton_create(host, 0);
    XQ_REQUIRE(radio != NULL);
    XWidget_setGeometry((XWidget*)radio, 0, 0, 100, 24);

    /* QRadioButtonPrivate::init（qradiobutton.cpp:32-41）。 */
    XQ_CHECK(XRadioButton_isCheckable(radio));
    XQ_CHECK(XRadioButton_autoExclusive(radio));
    XQ_CHECK(!XRadioButton_isChecked(radio));
    XQ_CHECK(!XRadioButton_isDown(radio));
    XQ_EQ_I64(XWidget_foregroundRole((XWidget*)radio),
              XPaletteColorRole_WindowText);
    XQ_CHECK(XWidget_hasMouseTracking((XWidget*)radio));
    /* 基类缺省：autoRepeat 关、延迟 300 / 间隔 100。 */
    XQ_CHECK(!XRadioButton_autoRepeat(radio));
    XQ_EQ_I64(XRadioButton_autoRepeatDelay(radio), 300);
    XQ_EQ_I64(XRadioButton_autoRepeatInterval(radio), 100);

    /* 尺寸提示（indicator 13 + 间距 + 文本占位）有效。 */
    hint = XWidget_sizeHint_base((const XWidget*)radio);
    XQ_CHECK(hint.width > 0 && hint.height > 0);

    /* hitButton 命中控件矩形（Qt SE_RadioButtonClickRect 近似）。 */
    XPoint_init(&inside, 50, 12);
    XPoint_init(&outside, 150, 100);
    XQ_CHECK(XRadioButton_hitButton(radio, &inside));
    XQ_CHECK(!XRadioButton_hitButton(radio, &outside));
    XQ_CHECK(!XRadioButton_hitButton(radio, NULL));

    XClassDelete((XClass*)host);
}

/* ==================== ② 同父自动互斥 + 独占项不可点掉 ==================== */

XQ_TEST(XRadioButton, autoExclusiveSiblings)
{
    XWidget* host = XWidget_create(NULL, 0);
    XRadioButton* first;
    XRadioButton* second;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 160);
    first = XRadioButton_create(host, 0);
    second = XRadioButton_create(host, 0);
    XQ_REQUIRE(first != NULL && second != NULL);
    XWidget_setGeometry((XWidget*)first, 0, 0, 100, 24);
    XWidget_setGeometry((XWidget*)second, 0, 30, 100, 24);

    /* 选中 B ⇒ A 被取消（Qt autoExclusive 联动；探针 h1）。 */
    XRadioButton_setChecked(first, true);
    XQ_CHECK(XRadioButton_isChecked(first));
    XRadioButton_setChecked(second, true);
    XQ_CHECK(XRadioButton_isChecked(second));
    XQ_CHECK(!XRadioButton_isChecked(first));

    /* 再点击已选中的独占项：状态不变（Qt changeState 闸；探针 h2）。 */
    XRadioButton_click(second);
    XQ_CHECK(XRadioButton_isChecked(second));

    /* 点未选中的那颗 ⇒ 交换选中项。 */
    XRadioButton_click(first);
    XQ_CHECK(XRadioButton_isChecked(first));
    XQ_CHECK(!XRadioButton_isChecked(second));

    /* 独占项不可被 setChecked(false)：Qt 只保护组内当前选中项。 */
    XRadioButton_setChecked(first, false);
    XQ_CHECK(XRadioButton_isChecked(first));

    /* 关闭 autoExclusive 后允许反选（探针 h3）。 */
    XRadioButton_setAutoExclusive(first, false);
    XQ_CHECK(!XRadioButton_autoExclusive(first));
    XRadioButton_setChecked(first, false);
    XQ_CHECK(!XRadioButton_isChecked(first));

    /* 关闭 autoExclusive 的那颗不再参与互斥：选 B 不会再被 A 拉走，
     * 而 B 自己仍按独占规则保持选中。 */
    XRadioButton_setChecked(second, true);
    XQ_CHECK(XRadioButton_isChecked(second));
    XRadioButton_setChecked(first, true);
    XQ_CHECK(XRadioButton_isChecked(first));
    XQ_CHECK(XRadioButton_isChecked(second));

    /* 父子所有权：两颗都在 host 子表里，父析构带走。 */
    {
        const XVector* children = XObject_children((XObject*)host);

        XQ_CHECK_PTR(children);
        if (children)
            XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 2);
    }

    XClassDelete((XClass*)host);
}

/* ==================== ③ 助记符 '&'：显示/度量 + shortcut 文本 ==================== */

XQ_TEST(XRadioButton, mnemonicDisplayAndSizeHint)
{
    XWidget* host = XWidget_create(NULL, 0);
    XRadioButton* withMnemonic;
    XRadioButton* plain;
    XSize withHint;
    XSize plainHint;
    XString* display = NULL;
    const char* text;
    const XString* shortcut;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 260, 120);
    withMnemonic = XRadioButton_create(host, 0);
    plain = XRadioButton_create(host, 0);
    XQ_REQUIRE(withMnemonic != NULL && plain != NULL);
    XWidget_setGeometry((XWidget*)withMnemonic, 0, 0, 120, 24);
    XWidget_setGeometry((XWidget*)plain, 0, 30, 120, 24);

    XRadioButton_setText_2(withMnemonic, "&File");
    XRadioButton_setText_2(plain, "File");

    text = XAbstractButton_displayTextUtf8((XAbstractButton*)withMnemonic,
                                           &display);
    XQ_EQ_STR(text ? text : "", "File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    withHint = XWidget_sizeHint_base((const XWidget*)withMnemonic);
    plainHint = XWidget_sizeHint_base((const XWidget*)plain);
    XQ_EQ_I64(withHint.width, plainHint.width);
    XQ_EQ_I64(withHint.height, plainHint.height);

    shortcut = XAbstractButton_shortcut((XAbstractButton*)withMnemonic);
    XQ_CHECK(shortcut != NULL);
    XQ_EQ_STR(shortcut ? XString_toUtf8(shortcut) : "", "Alt+F");

    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON */
