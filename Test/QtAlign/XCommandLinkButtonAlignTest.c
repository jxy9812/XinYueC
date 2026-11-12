/**
 * @file       XCommandLinkButtonAlignTest.c
 * @brief      XCommandLinkButton × Qt 6.8 QCommandLinkButton 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *             qcommandlinkbutton.h 与 Qt 6.8.3 源码 …/src/widgets/widgets/
 *             qcommandlinkbutton.cpp，关键行为用本机 Qt 6.8 探针
 *             （QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：description 空、iconSize 20x20
 *               （qcommandlinkbutton.cpp:189）、尺寸策略 Preferred/
 *               Preferred + PushButton 且 heightForWidth 位开启
 *               （:184-187）、flat=false；
 *             - sizeHint 双行：无描述 41 高、带描述 60 高（探针 g/g2：
 *               172x60 / 172x41；本库按实测字体算，只断言关系）；
 *             - **minimumSizeHint ≠ sizeHint**：宽沿用 sizeHint，高压到
 *               max(标题行 + 上下边距, 图标高 + 上边距)
 *               （qcommandlinkbutton.cpp:222-230，探针 minHint 172x37）；
 *             - heightForWidth 按可用宽度折行：窄宽比宽宽更高
 *               （qcommandlinkbutton.cpp:309-317，探针 hfw(200)=55、
 *               hfw(135)=73）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
    XCOMMANDLINKBUTTON_ON

#include "XCommandLinkButton.h"
#include "XPushButton.h"
#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget.h"
#include "XString.h"
#include "XClass.h"

/** @brief 描述文本的 UTF-8 借用指针（空串归一：XString 空串 toUtf8 为 NULL）。 */
static const char* xcl_desc(const XCommandLinkButton* button)
{
    const XString* text = XCommandLinkButton_description(button);
    const char* utf8 = text ? XString_toUtf8(text) : NULL;

    return utf8 ? utf8 : "";
}

/* ==================== ① 构造默认值 + 尺寸策略 ==================== */

XQ_TEST(XCommandLinkButton, constructorDefaults)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCommandLinkButton* button;
    XWidgetSizePolicy policy;
    XSize iconSize;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 320, 160);
    button = XCommandLinkButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 160, 48);

    /* 描述默认空串（拥有型 XString 承载，查询不返回 NULL）。 */
    XQ_EQ_STR(xcl_desc(button), "");
    /* 图标尺寸 20x20（qcommandlinkbutton.cpp:189）。 */
    iconSize = XCommandLinkButton_iconSize(button);
    XQ_EQ_I64(iconSize.width, 20);
    XQ_EQ_I64(iconSize.height, 20);
    /* 继承 QPushButton 的默认值。 */
    XQ_CHECK(!XCommandLinkButton_isFlat(button));
    XQ_CHECK(!XCommandLinkButton_isDefault(button));
    XQ_CHECK(!XCommandLinkButton_autoDefault(button));
    XQ_CHECK(XCommandLinkButton_menu(button) == NULL);
    /* 尺寸策略 Preferred/Preferred + heightForWidth（:184-187）。 */
    policy = XWidget_sizePolicy((XWidget*)button);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              XWidgetSizePolicy_Preferred);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              XWidgetSizePolicy_Preferred);
    XQ_CHECK(XWidgetSizePolicy_hasHeightForWidth(&policy));

    /* 描述文本往返（含 UTF-8 便利版本与 NULL 归一空串）。 */
    XCommandLinkButton_setDescription_2(button, "说明文本");
    XQ_EQ_STR(xcl_desc(button), "说明文本");
    XCommandLinkButton_setDescription_2(button, NULL);
    XQ_EQ_STR(xcl_desc(button), "");

    XClassDelete((XClass*)host);
}

/* ==================== ② 双行 sizeHint / minimumSizeHint / heightForWidth ==================== */

XQ_TEST(XCommandLinkButton, twoLineSizes)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCommandLinkButton* button;
    XSize oneLine;
    XSize twoLine;
    XSize minimumHint;
    int hWide;
    int hNarrow;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 400, 200);
    button = XCommandLinkButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 200, 60);
    XCommandLinkButton_setText_2(button, "标题");

    /* 无描述 ⇒ 单行高度。 */
    oneLine = XCommandLinkButton_sizeHint_base(button);
    XQ_CHECK(oneLine.width > 0 && oneLine.height > 0);

    /* 带描述 ⇒ 双行，高严格变大（qcommandlinkbutton.cpp:289-306）。 */
    XCommandLinkButton_setDescription_2(button, "这一行是按钮的说明文字");
    twoLine = XCommandLinkButton_sizeHint_base(button);
    XQ_CHECK(twoLine.height > oneLine.height);
    XQ_CHECK(twoLine.width > 0);

    /* minimumSizeHint：宽沿用 sizeHint，高被压到「标题行 + 上下边距」
     * 或「图标高 + 上边距」（qcommandlinkbutton.cpp:222-230；探针
     * minHint 172x37 < sizeHint 172x60）。 */
    minimumHint = XCommandLinkButton_minimumSizeHint_base(button);
    XQ_EQ_I64(minimumHint.width, twoLine.width);
    XQ_CHECK(minimumHint.height < twoLine.height);
    /* 图标高 20 + 单侧边距 8 = 28 是下限。 */
    XQ_CHECK(minimumHint.height >= 28);
    /* 存储位与查询同源（布局读 sizeHint/minimumSizeHint 存储位）。 */
    {
        XSize stored = XWidget_minimumSizeHint_base((const XWidget*)button);

        XQ_EQ_I64(stored.width, minimumHint.width);
        XQ_EQ_I64(stored.height, minimumHint.height);
    }

    /* heightForWidth：可用宽度越窄，描述折行越多，高度越大
     * （qcommandlinkbutton.cpp:309-317；探针 hfw(200)=55 < hfw(135)=73）。 */
    hWide = XCommandLinkButton_heightForWidth(button, 4000);
    hNarrow = XCommandLinkButton_heightForWidth(button, 60);
    XQ_CHECK(hWide > 0);
    XQ_CHECK(hNarrow > hWide);
    /* 无描述的按钮两者相等（描述折行高为 0）。 */
    XCommandLinkButton_setDescription_2(button, "");
    XQ_EQ_I64(XCommandLinkButton_heightForWidth(button, 60),
              XCommandLinkButton_heightForWidth(button, 4000));

    /* 描述变化经 contentChanged 槽刷新尺寸存储位。 */
    XCommandLinkButton_setDescription_2(button, "又一长段说明文字用于比较");
    {
        XSize stored = XWidget_sizeHint_base((const XWidget*)button);
        XSize hint = XCommandLinkButton_sizeHint_base(button);

        XQ_EQ_I64(stored.width, hint.width);
        XQ_EQ_I64(stored.height, hint.height);
    }

    XClassDelete((XClass*)host);
}

/* ==================== ③ 标题助记符 '&'：显示/度量 + shortcut 文本 ==================== */

XQ_TEST(XCommandLinkButton, mnemonicTitleDisplay)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCommandLinkButton* withMnemonic;
    XCommandLinkButton* plain;
    XSize withHint;
    XSize plainHint;
    XString* display = NULL;
    const char* text;
    const XString* shortcut;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 400, 200);
    withMnemonic = XCommandLinkButton_create(host, 0);
    plain = XCommandLinkButton_create(host, 0);
    XQ_REQUIRE(withMnemonic != NULL && plain != NULL);
    XWidget_setGeometry((XWidget*)withMnemonic, 0, 0, 180, 50);
    XWidget_setGeometry((XWidget*)plain, 0, 60, 180, 50);

    XCommandLinkButton_setText_2(withMnemonic, "&Open");
    XCommandLinkButton_setText_2(plain, "Open");

    text = XAbstractButton_displayTextUtf8((XAbstractButton*)withMnemonic,
                                           &display);
    XQ_EQ_STR(text ? text : "", "Open");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    withHint = XCommandLinkButton_sizeHint_base(withMnemonic);
    plainHint = XCommandLinkButton_sizeHint_base(plain);
    XQ_EQ_I64(withHint.width, plainHint.width);
    XQ_EQ_I64(withHint.height, plainHint.height);

    shortcut = XAbstractButton_shortcut((XAbstractButton*)withMnemonic);
    XQ_CHECK(shortcut != NULL);
    XQ_EQ_STR(shortcut ? XString_toUtf8(shortcut) : "", "Alt+O");

    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
          XCOMMANDLINKBUTTON_ON */
