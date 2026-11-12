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
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XImage.h"

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

#if XSTYLE_ON
/* ==================== ④ P3-02 initStyleOption：CommandLinkButton 特性位 ==================== */

/*
 * F 批把 commandlink_initStyleOption 做成 static（XCommandLinkButton.c:351）。
 * 生产 .c 经 Src/XGui/CMakeLists.txt:30-33 的 GLOB_RECURSE 编进静态库
 * XinYueGui；本测试经 Test/CMakeLists.txt:44 的 GLOB_RECURSE 收进
 * TEST_FILE → 可执行 XinYueC_Static（:278-281），二者靠 CMakeLists.txt:
 * 869-871 的 XGuiLayer(INTERFACE)→XinYueGui 链接链连通，并非同一个目标。
 * 故测试 TU 既不能跨 TU 直调 static（本 TU 无其声明/定义），也不能
 * #include 生产 .c（会与库内 XCommandLinkButton.o 的全局符号重复定义）。
 * 唯一公开入口 XCommandLinkButton_drawContents（XCommandLinkButton.h:270）在
 * XCommandLinkButton.c:443 填充选项、:444 按 Qt 口径清 m_base.m_text、:447
 * 把 &opt.m_base 交给 style()->drawControl(CE_PushButton)。
 *
 * 故挂一个只重载 EXStyle_DrawControl 的探针样式：绘制时截获传给样式层的
 * 选项，按「m_base 是 XStyleOptionButton 首成员」强转回完整结构（C99
 * 6.7.2.1p15；Qt 侧 qstyleoption_cast<QStyleOptionButton*> 同款前提，
 * qcommonstyle.cpp:1333），读 m_features —— 这是 initStyleOption 产出里在
 * 公开路径唯一可观察、且能锁定「本类填充体 + 置 CommandLinkButton 位」的字段。
 */
XCLASS_DEFINE_BEGING(XCommandLinkCaptureStyle)
XCLASS_DEFINE_EXTEND_END(XCommandLinkCaptureStyle, XStyle)

typedef struct XCommandLinkCaptureStyle
{
    XStyle m_base;            /**< 基类；必须第一个。 */
    int m_callCount;          /**< CE_PushButton 分派次数。 */
    uint32_t m_features;      /**< 截获的 m_features。 */
    int32_t m_optionType;     /**< 截获的 m_base.m_optionType。 */
    bool m_textNull;          /**< 分派时 m_base.m_text 是否为 NULL。 */
    const XWidget* m_widget;  /**< 分派携带的控件指针。 */
} XCommandLinkCaptureStyle;

static void xcl_captureDrawControl(XStyle* self, int ce,
                                   const XStyleOption* option,
                                   XPainter* painter, const XWidget* widget)
{
    XCommandLinkCaptureStyle* capture = (XCommandLinkCaptureStyle*)self;
    const XStyleOptionButton* btn;

    (void)painter;
    if (!capture || !option || ce != XStyleCE_PushButton) return;
    ++capture->m_callCount;
    capture->m_widget = widget;
    /* &opt.m_base 与 &opt 同址（首成员），强转回派生选项读 m_features。 */
    btn = (const XStyleOptionButton*)option;
    capture->m_features = btn->m_features;
    capture->m_optionType = btn->m_base.m_optionType;
    capture->m_textNull = (btn->m_base.m_text == NULL);
}

static XVtable* XCommandLinkCaptureStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCommandLinkCaptureStyle)
    XVTABLE_INHERIT_XCLASS(XStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_DrawControl, xcl_captureDrawControl);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上创建探针样式（调用方 XClassDelete）。 */
static XCommandLinkCaptureStyle* xcl_createCaptureStyle(void)
{
    XCommandLinkCaptureStyle* style = (XCommandLinkCaptureStyle*)
        XMemory_malloc(sizeof(XCommandLinkCaptureStyle),
                       XCLASS_DEFAULT_MEMORY_TYPE);

    if (!style) return NULL;
    memset(style, 0, sizeof(*style));
    XStyle_init(&style->m_base);
    XClassSetVtable(style, XCommandLinkCaptureStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/**
 * @brief P3-02 回归锁：initStyleOption 置 CommandLinkButton 位。
 * @details 断言链：
 *   - 数据源：按钮文本 = XString_toUtf8(XAbstractButton_text)（填充体
 *     XCommandLinkButton.c:381-382 取的就是它）；
 *   - 公开绘制：drawContents 经自定义样式分派一次 CE_PushButton，
 *     截获选项 m_optionType==Button、widget==本按钮；
 *   - 核心：m_features 精确等于 XStyleOptionButtonFeature_CommandLinkButton
 *     （0x10，qstyleoption.h:221-222 / XStyleOption.h:1072 / 生产 :372），
 *     同时锁住「含该位」并排除其余 Qt 位被误置；
 *   - Qt 口径：分派时 m_base.m_text 已按 qcommandlinkbutton.cpp:328
 *     置空（生产 :444），故公开路径只断言「已清空」，不伪造「m_text==
 *     按钮文本」的直接相等断言 —— 见 openNotes。
 */
XQ_TEST(XCommandLinkButton, initStyleOptionCommandLinkFeature)
{
    XWidget* host = XWidget_create(NULL, 0);
    XCommandLinkButton* button;
    XCommandLinkCaptureStyle* style;
    XImage image;
    XPainter painter;
    const XString* text;
    bool began = false;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 320, 160);
    button = XCommandLinkButton_create(host, 0);
    if (!button) {
        /* 失败提前返回前先回收 host（含子对象），保持零泄漏。 */
        xq_fail_at(__FILE__, __LINE__, "XCommandLinkButton_create 返回 NULL");
        XClassDelete((XClass*)host);
        return;
    }
    XWidget_setGeometry((XWidget*)button, 0, 0, 200, 60);
    XCommandLinkButton_setText_2(button, "链接标题");

    /* 数据源核对：commandlink_initStyleOption :381-382 以
       XString_toUtf8(ab->m_text) 填 m_base.m_text，先锁按钮文本侧。 */
    text = XAbstractButton_text((const XAbstractButton*)button);
    XQ_CHECK(text != NULL);
    XQ_EQ_STR(text ? XString_toUtf8(text) : "", "链接标题");

    style = xcl_createCaptureStyle();
    if (!style) {
        /* 失败提前返回前先回收 host（button 随父释放），零泄漏。 */
        xq_fail_at(__FILE__, __LINE__, "探针样式分配失败");
        XClassDelete((XClass*)host);
        return;
    }
    XWidget_setStyle((XWidget*)button, (XStyle*)style);

    /* 离屏绘制触发一次真实 drawContents 样式分派。 */
    XImage_init_ex(&image, 240, 80, XImageFormat_ARGB32);
    if (!XImage_isNull(&image)) {
        XImage_fill(&image, 0u);
        XPainter_init(&painter, NULL);
        began = XPainter_begin_image(&painter, &image);
        if (began) {
            XCommandLinkButton_drawContents(button, &painter);
            XPainter_end(&painter);
        }
        XPainter_deinit(&painter);
    }
    XQ_CHECK(began);

    /* 公开路径确实走过 initStyleOption → drawControl 序列。 */
    XQ_EQ_I64(style->m_callCount, 1);
    XQ_CHECK(style->m_widget == (const XWidget*)button);
    /* 核心欠断言：CommandLinkButton 位（0x10）必须在。 */
    XQ_CHECK((style->m_features &
              XStyleOptionButtonFeature_CommandLinkButton) != 0);
    /* 精确等于 0x10：既锁位序，又排除 Flat/HasMenu/Default/
       AutoDefault 四个 Qt 位在构造默认态被误置（本文件 ① 同构造下
       已证 flat=false/menu=NULL/!autoDefault/!isDefault）。 */
    XQ_EQ_I64(style->m_features,
              XStyleOptionButtonFeature_CommandLinkButton);
    /* 选项类型是 Button（initStyleOption :360 设置）。 */
    XQ_EQ_I64(style->m_optionType, XStyleOptionType_Button);
    /* Qt 6.8 qcommandlinkbutton.cpp:328 同款：分派前 m_base.m_text 置空
       （生产 :444）。这是对齐口径，非填充体漏填。 */
    XQ_CHECK(style->m_textNull);

    XWidget_setStyle((XWidget*)button, NULL);
    XClassDelete((XClass*)style);
    XClassDeinit(&image);
    XClassDelete((XClass*)host);
}
#endif /* XSTYLE_ON */

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && \
          XCOMMANDLINKBUTTON_ON */
