/******************************************************************************
 * @file       xgui_demo_apitest_labels.c
 * @brief      控件 API 测试族：labels（显示族）。
 * @details    覆盖 XLabel / XFrame / XGroupBox / XLcdNumber / XProgressBar
 *             五个控件类的公开 API（对标 Qt 6.8.3 同名控件）：
 *             - 属性 setter/getter 往返一致；
 *             - Qt 6.8.3 对齐默认值（有文档依据的直接断言，无依据的写
 *               注释不硬断言防误报）；
 *             - 信号发射与状态迁移（valueChanged/toggled/clicked/
 *               overflow/linkActivated/linkHovered 经 XObject_event_base
 *               直发合成事件或槽调用触发，与真实输入同路径）；
 *             - 边界（空串/NULL/0/极大值/重复 set/未 show 直接调 API）。
 *             全套件无头运行：所有控件栈上构造、从不 show（契约
 *             「控件不 show 也可调绝大多数 API」口径），坐标一律控件
 *             本地坐标。
 * @note       文件所有权：仅本翻译单元，不改契约头、主文件与 Src/。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>

#include "xgui_demo_apitest.h"

#if XWIDGET_ON && XFRAME_ON
#include "XObject.h"
#include "XEvent.h"
#include "XAlignment.h"
#include "XFrame.h"
#endif /* XWIDGET_ON && XFRAME_ON */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif

#if XWIDGET_ON && XGROUPBOX_ON
#include "XGroupBox.h"
#endif

#if XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON
#include "XLcdNumber.h"
#endif

#if XWIDGET_ON && XPROGRESSBAR_ON
#include "XProgressBar.h"
#endif

/* ==================== 共享：合成鼠标事件注入（与真实输入同路径） ==================== */

#if XWIDGET_ON && XFRAME_ON && (XLABEL_ON || XGROUPBOX_ON)
/** @brief 向目标控件直发一次左键合成鼠标事件（pos 为控件本地坐标）。 */
static void labels_injectMouse(XWidget* target, XEventType type, int x, int y)
{
    XMouseEvent me;
    XPoint pos;
    if (!target) return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}
#endif /* XWIDGET_ON && XFRAME_ON && (XLABEL_ON || XGROUPBOX_ON) */

/* ==================== XFrame：框架样式（对标 QFrame） ==================== */

#if XWIDGET_ON && XFRAME_ON

static int labels_frame(void)
{
    int failures = 0;
    XFrame frame;
    XRect rect;
    XSize hint;
    XFrameStyleOption option;

    XFrame_init(&frame, NULL, 0);

    /* 默认值（Qt QFrame 构造默认 NoFrame|Plain、lineWidth=1、
     * midLineWidth=0、frameWidth=0，qframe.cpp 构造口径）。 */
    XAPI_EXPECT(XFrame_frameStyle(&frame) ==
                    (int)(XFrameShape_NoFrame | XFrameShadow_Plain),
                "XFrame 默认样式=NoFrame|Plain(16)");
    XAPI_EXPECT(XFrame_lineWidth(&frame) == 1, "XFrame 默认线宽=1");
    XAPI_EXPECT(XFrame_midLineWidth(&frame) == 0, "XFrame 默认中间线宽=0");
    XAPI_EXPECT(XFrame_frameWidth(&frame) == 0, "XFrame NoFrame 综合边框宽=0");

    /* Shape|Shadow 组合读写（Qt frameStyle()=Shape|Shadow 组合值）。 */
    XFrame_setFrameStyle(&frame,
                         (int)XFrameShape_Box | (int)XFrameShadow_Raised);
    XAPI_EXPECT(XFrame_frameStyle(&frame) ==
                    ((int)XFrameShape_Box | (int)XFrameShadow_Raised) &&
                    XFrame_frameShape(&frame) == XFrameShape_Box &&
                    XFrame_frameShadow(&frame) == XFrameShadow_Raised,
                "XFrame setFrameStyle(Box|Raised) 形状/阴影往返");

    /* frameWidth 综合规则（Qt QFramePrivate::updateFrameWidth：
     * Box 立体=2*lineWidth+midLineWidth、Plain=lineWidth、
     * StyledPanel/WinPanel=2、Panel=lineWidth、NoFrame=0）。 */
    XAPI_EXPECT(XFrame_frameWidth(&frame) == 2,
                "XFrame Box|Raised 边框宽=2*1+0");
    XFrame_setLineWidth(&frame, 3);
    XAPI_EXPECT(XFrame_frameWidth(&frame) == 6,
                "XFrame Box 立体边框宽随线宽=2*3");
    XFrame_setMidLineWidth(&frame, 2);
    XAPI_EXPECT(XFrame_frameWidth(&frame) == 8,
                "XFrame Box 立体边框宽含中间线宽=2*3+2");
    XFrame_setFrameShadow(&frame, XFrameShadow_Plain);
    XAPI_EXPECT(XFrame_frameShape(&frame) == XFrameShape_Box &&
                    XFrame_frameWidth(&frame) == 3,
                "XFrame setFrameShadow 保留形状且 Plain 边框宽=线宽");
    XFrame_setFrameShape(&frame, XFrameShape_StyledPanel);
    XAPI_EXPECT(XFrame_frameShadow(&frame) == XFrameShadow_Plain &&
                    XFrame_frameWidth(&frame) == 2,
                "XFrame setFrameShape 保留阴影且 StyledPanel 边框宽=2");
    XFrame_setFrameStyle(&frame,
                         (int)XFrameShape_Panel | (int)XFrameShadow_Raised);
    XFrame_setLineWidth(&frame, 5);
    XAPI_EXPECT(XFrame_frameWidth(&frame) == 5,
                "XFrame Panel 边框宽=线宽");
    XFrame_setFrameShape(&frame, XFrameShape_NoFrame);
    XAPI_EXPECT(XFrame_frameWidth(&frame) == 0,
                "XFrame NoFrame 边框宽=0");

    /* 线宽 short 存储语义（Qt setFrameLineWidth 以 (short)width 保存，
     * 不钳位；70000 截断后=4464）。 */
    XFrame_setLineWidth(&frame, 70000);
    XAPI_EXPECT(XFrame_lineWidth(&frame) == 4464,
                "XFrame 线宽 70000 按 short 存储截断=4464");
    XFrame_setMidLineWidth(&frame, 1);
    XAPI_EXPECT(XFrame_midLineWidth(&frame) == 1,
                "XFrame setMidLineWidth(1) 往返");

    /* frameRect 往返（Qt setFrameRect 经 contentsMargins 保持
     * frameRect() 读回一致；先给几何 100x50）。 */
    XWidget_setGeometry((XWidget*)&frame.m_base, 0, 0, 100, 50);
    XRect_init(&rect, 10, 8, 70, 30);
    XFrame_setFrameRect(&frame, &rect);
    {
        XRect back = XFrame_frameRect(&frame);
        XAPI_EXPECT(back.x == 10 && back.y == 8 && back.width == 70 &&
                        back.height == 30,
                    "XFrame setFrameRect/frameRect 往返一致");
    }
    /* 无效矩形回退整个控件矩形（Qt setFrameRect(QRect()) 回退
     * QWidget::rect()）。 */
    XFrame_setFrameRect(&frame, NULL);
    {
        XRect back = XFrame_frameRect(&frame);
        XAPI_EXPECT(back.x == 0 && back.y == 0 && back.width == 100 &&
                        back.height == 50,
                    "XFrame setFrameRect(NULL) 回退控件矩形");
    }

    /* sizeHint 形状特例（Qt QFrame::sizeHint：HLine=(-1,3)、
     * VLine=(3,-1)）。 */
    XFrame_setFrameStyle(&frame, (int)XFrameShape_HLine |
                                     (int)XFrameShadow_Plain);
    hint = XFrame_sizeHint(&frame);
    XAPI_EXPECT(hint.width == -1 && hint.height == 3,
                "XFrame HLine sizeHint=(-1,3)");
    XFrame_setFrameStyle(&frame, (int)XFrameShape_VLine |
                                     (int)XFrameShadow_Plain);
    hint = XFrame_sizeHint(&frame);
    XAPI_EXPECT(hint.width == 3 && hint.height == -1,
                "XFrame VLine sizeHint=(3,-1)");

    /* initStyleOption 样式选项填充（对标 QFrame::initStyleOption：
     * 形状/阴影/线宽/外形矩形/凸起态）。 */
    XFrame_setFrameStyle(&frame,
                         (int)XFrameShape_Box | (int)XFrameShadow_Raised);
    XFrame_setLineWidth(&frame, 3);
    XFrame_initStyleOption(&frame, &option);
    XAPI_EXPECT(option.m_frameShape == XFrameShape_Box &&
                    option.m_frameShadow == XFrameShadow_Raised &&
                    option.m_raised && !option.m_sunken &&
                    option.m_lineWidth == 3,
                "XFrame initStyleOption 填充形状/阴影/线宽/凸起态");

    /* 离屏绘制扩展入口对 NULL painter 为无操作（头文件契约；
     * 健壮性冒烟调用，渲染效果不在本套件断言职责内）。 */
    XFrame_drawFrame(&frame, NULL);

    /* NULL 口径（头文件：NULL 返回默认值而非崩溃）。 */
    XAPI_EXPECT(XFrame_frameStyle((const XFrame*)NULL) ==
                    (int)(XFrameShape_NoFrame | XFrameShadow_Plain) &&
                    XFrame_frameWidth((const XFrame*)NULL) == 0 &&
                    XFrame_lineWidth((const XFrame*)NULL) == 1,
                "XFrame NULL 查询返回默认值");

    XFrame_deinit_base(&frame);
    return failures;
}

#endif /* XWIDGET_ON && XFRAME_ON */

/* ==================== XLabel：标签（对标 QLabel） ==================== */

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/* ---- 链接信号捕获（XVarList 释放前拷出内容） ---- */
static char g_labels_lastLink[128];
static int g_labels_linkActivatedCount = 0;
static int g_labels_linkHoveredCount = 0;

static void labels_linkActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, link);
    snprintf(g_labels_lastLink, sizeof(g_labels_lastLink), "%s",
             link ? xapi_u8(link) : "");
    ++g_labels_linkActivatedCount;
}

static void labels_linkHoveredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, link);
    snprintf(g_labels_lastLink, sizeof(g_labels_lastLink), "%s",
             link ? xapi_u8(link) : "");
    ++g_labels_linkHoveredCount;
}

/* 富文本资源回调样例（仅验证 setter/getter 往返，不参与解析）。 */
static int g_labels_providerCtx = 0;
static XString* labels_resourceProvider(XLabel* label, const XString* url,
                                        void* userData)
{
    (void)label;
    (void)url;
    (void)userData;
    return NULL;
}

static int labels_label(void)
{
    int failures = 0;
    XLabel label;
    XLabel selectable;
    XLabel wrap;
    XLabel linkLabel;
    XWidget buddy;

    /* 默认值（Qt QLabel 构造默认：空文本、AutoText、Left|VCenter、
     * margin=0、indent=-1、wordWrap/scaledContents/openExternalLinks=
     * false、textInteractionFlags=LinksAccessibleByMouse、无 pixmap）。 */
    XLabel_init(&label, NULL, 0);
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "",
                                    XChar_CaseSensitive),
                "XLabel 默认文本为空");
    XAPI_EXPECT(XLabel_textFormat(&label) == XLabelTextFormat_AutoText,
                "XLabel 默认格式=AutoText");
    XAPI_EXPECT(XLabel_alignment(&label) ==
                    (XAlignment_Left | XAlignment_VCenter),
                "XLabel 默认对齐=Left|VCenter");
    XAPI_EXPECT(XLabel_indent(&label) == -1, "XLabel 默认缩进=-1");
    XAPI_EXPECT(XLabel_margin(&label) == 0, "XLabel 默认边距=0");
    XAPI_EXPECT(!XLabel_wordWrap(&label), "XLabel 默认不自动换行");
    XAPI_EXPECT(!XLabel_hasScaledContents(&label), "XLabel 默认不缩放内容");
    XAPI_EXPECT(!XLabel_openExternalLinks(&label), "XLabel 默认不打开外链");
    XAPI_EXPECT(XLabel_textInteractionFlags(&label) ==
                    (XLabelTextInteractionFlags)
                        XLabelTextInteraction_LinksAccessibleByMouse,
                "XLabel 默认交互标志=LinksAccessibleByMouse");
    {
        XPixmap pm = XLabel_pixmap(&label);
        XAPI_EXPECT(XPixmap_isNull(&pm), "XLabel 默认像素图为空");
    }

    /* ---- 文本读写（Qt setText/text 往返；setNum(int)/(double)）。 ---- */
    XLabel_setText_2(&label, "Hello 世界");
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "Hello 世界",
                                    XChar_CaseSensitive),
                "XLabel setText_2/text 往返（UTF-8）");
    XLabel_setText_2(&label, NULL);
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "",
                                    XChar_CaseSensitive),
                "XLabel setText_2(NULL) 清空文本");
    XLabel_setNum(&label, 42);
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "42",
                                    XChar_CaseSensitive),
                "XLabel setNum(int) 十进制文本=42");
    XLabel_setNum_2(&label, 1.5);
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "1.5",
                                    XChar_CaseSensitive),
                "XLabel setNum(double) %g 表示=1.5");

    /* 文本格式（Qt setTextFormat/textFormat 往返）。 */
    XLabel_setTextFormat(&label, XLabelTextFormat_RichText);
    XAPI_EXPECT(XLabel_textFormat(&label) == XLabelTextFormat_RichText,
                "XLabel setTextFormat(RichText) 往返");
    XLabel_setTextFormat(&label, XLabelTextFormat_MarkdownText);
    XAPI_EXPECT(XLabel_textFormat(&label) == XLabelTextFormat_MarkdownText,
                "XLabel setTextFormat(MarkdownText) 往返");
    XLabel_setTextFormat(&label, XLabelTextFormat_AutoText);

    /* clear 清空全部内容（Qt QLabel::clear）。 */
    XLabel_setText_2(&label, "待清空");
    XLabel_clear(&label);
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "",
                                    XChar_CaseSensitive),
                "XLabel clear 清空文本");

    /* setPixmap(NULL) 仅清空（Qt setPixmap 切换内容模式；NULL 仅清空，
     * 退出文本模式后 text() 为空）。 */
    XLabel_setText_2(&label, "将被像素图清空");
    XLabel_setPixmap(&label, NULL);
    XAPI_EXPECT(XString_equals_utf8(XLabel_text(&label), "",
                                    XChar_CaseSensitive),
                "XLabel setPixmap(NULL) 清空文本内容");

    /* ---- 布局属性往返（Qt setAlignment/setIndent/setMargin/
     * setWordWrap/setScaledContents/setOpenExternalLinks）。 ---- */
    XLabel_setAlignment(&label, XAlignment_Right | XAlignment_Bottom);
    XAPI_EXPECT(XLabel_alignment(&label) ==
                    (XAlignment_Right | XAlignment_Bottom),
                "XLabel setAlignment(Right|Bottom) 往返");
    /* 头文件口径：仅保留水平/垂直两组掩码位（越界位被剥除）。 */
    XLabel_setAlignment(&label,
                        (XAlignments)(XAlignment_Right | 0x0200));
    XAPI_EXPECT(XLabel_alignment(&label) == XAlignment_Right,
                "XLabel setAlignment 剥除非对齐掩码位");
    XLabel_setIndent(&label, 6);
    XAPI_EXPECT(XLabel_indent(&label) == 6, "XLabel setIndent(6) 往返");
    XLabel_setIndent(&label, -1);
    XAPI_EXPECT(XLabel_indent(&label) == -1, "XLabel 缩进 -1 表自动计算");
    XLabel_setMargin(&label, 7);
    XAPI_EXPECT(XLabel_margin(&label) == 7, "XLabel setMargin(7) 往返");
    XLabel_setWordWrap(&label, true);
    XAPI_EXPECT(XLabel_wordWrap(&label), "XLabel setWordWrap(true) 往返");
    XLabel_setScaledContents(&label, true);
    XAPI_EXPECT(XLabel_hasScaledContents(&label),
                "XLabel setScaledContents(true) 往返");
    XLabel_setOpenExternalLinks(&label, true);
    XAPI_EXPECT(XLabel_openExternalLinks(&label),
                "XLabel setOpenExternalLinks(true) 往返");

    /* 资源回调往返（Qt setResourceProvider/resourceProvider）。 */
    XLabel_setResourceProvider(&label, labels_resourceProvider,
                               &g_labels_providerCtx);
    XAPI_EXPECT(XLabel_resourceProvider(&label) == labels_resourceProvider &&
                    XLabel_resourceProviderUserData(&label) ==
                        (void*)&g_labels_providerCtx,
                "XLabel setResourceProvider 回调/上下文往返");
    XLabel_setResourceProvider(&label, NULL, NULL);

    /* 伙伴控件往返（Qt setBuddy/buddy；助记符快捷键为受限未实现项，
     * 仅验证关系读写）。 */
    XWidget_init(&buddy, NULL, 0);
    XLabel_setBuddy(&label, &buddy);
    XAPI_EXPECT(XLabel_buddy(&label) == &buddy, "XLabel setBuddy/buddy 往返");
    XLabel_setBuddy(&label, NULL);
    XAPI_EXPECT(XLabel_buddy(&label) == NULL, "XLabel setBuddy(NULL) 清除");

    /* ---- 交互标志与焦点策略联动（Qt setTextInteractionFlags：
     * 键盘链接→StrongFocus、可选中→ClickFocus、否则→NoFocus）。 ---- */
    XLabel_setTextInteractionFlags(
        &label, (XLabelTextInteractionFlags)
                    XLabelTextInteraction_TextSelectableByMouse);
    XAPI_EXPECT(XLabel_textInteractionFlags(&label) ==
                    (XLabelTextInteractionFlags)
                        XLabelTextInteraction_TextSelectableByMouse &&
                    XWidget_focusPolicy((XWidget*)&label.m_base) ==
                        XWidgetFocusPolicy_ClickFocus,
                "XLabel 可选中标志置 ClickFocus");
    XLabel_setTextInteractionFlags(
        &label, (XLabelTextInteractionFlags)(
                    XLabelTextInteraction_LinksAccessibleByMouse |
                    XLabelTextInteraction_LinksAccessibleByKeyboard));
    XAPI_EXPECT(XWidget_focusPolicy((XWidget*)&label.m_base) ==
                    XWidgetFocusPolicy_StrongFocus,
                "XLabel 键盘链接标志置 StrongFocus");
    XLabel_setTextInteractionFlags(
        &label, (XLabelTextInteractionFlags)
                    XLabelTextInteraction_NoTextInteraction);
    XAPI_EXPECT(XWidget_focusPolicy((XWidget*)&label.m_base) ==
                    XWidgetFocusPolicy_NoFocus,
                "XLabel 无交互标志置 NoFocus");

    /* ---- 程序化选择（Qt setSelection/hasSelectedText/selectedText/
     * selectionStart；UTF-16 码元契约）。 ---- */
    /* 边界：纯文本 + 无可选中标志 + NoFocus 时程序化选区被拒
     * （镜像 Qt needTextControl 的创建条件，见 XLabel.c 注释）。 */
    XLabel_init(&selectable, NULL, 0);
    XLabel_setText_2(&selectable, "Hello");
    XLabel_setSelection(&selectable, 0, 1);
    XAPI_EXPECT(!XLabel_hasSelectedText(&selectable) &&
                    XLabel_selectionStart(&selectable) == -1,
                "XLabel 不可交互纯文本程序化选区被拒");
    /* 置可选中后选区生效。 */
    XLabel_setTextInteractionFlags(
        &selectable, (XLabelTextInteractionFlags)
                         XLabelTextInteraction_TextSelectableByMouse);
    XLabel_setSelection(&selectable, 1, 3);
    XAPI_EXPECT(XLabel_hasSelectedText(&selectable) &&
                    XLabel_selectionStart(&selectable) == 1,
                "XLabel setSelection(1,3) 起点往返");
    {
        XString* sel = XLabel_selectedText(&selectable);
        XAPI_EXPECT(XString_equals_utf8(sel, "ell", XChar_CaseSensitive),
                    "XLabel selectedText=UTF-16 码元切片 ell");
        XString_delete_base((XClass*)sel);
    }
    /* 任一参数 -1 清除选择（Qt setSelection 语义）。 */
    XLabel_setSelection(&selectable, -1, 0);
    XAPI_EXPECT(!XLabel_hasSelectedText(&selectable),
                "XLabel setSelection(-1,·) 清除选择");
    /* 超长长度钳位到文本末（Qt setSelection 钳位）。 */
    XLabel_setSelection(&selectable, 0, 99);
    {
        XString* sel = XLabel_selectedText(&selectable);
        XAPI_EXPECT(XLabel_selectionStart(&selectable) == 0 &&
                        XString_equals_utf8(sel, "Hello",
                                            XChar_CaseSensitive),
                    "XLabel 选区超长钳位到文本末 Hello");
        XString_delete_base((XClass*)sel);
    }

    /* ---- 尺寸提示（Qt hasHeightForWidth/heightForWidth：wordWrap 时
     * 高度随宽度变化；heightForWidth(-1) 与 sizeHint 同口径）。 ---- */
    XLabel_init(&wrap, NULL, 0);
    XLabel_setWordWrap(&wrap, true);
    XLabel_setText_2(&wrap, "AAAA BBBB CCCC DDDD EEEE FFFF GGGG");
    XAPI_EXPECT(XLabel_heightForWidth(&wrap, 40) >
                    XLabel_heightForWidth(&wrap, 400),
                "XLabel wordWrap 窄宽换行更高（hasHeightForWidth）");
    {
        XSize hint = XLabel_sizeHint(&wrap);
        XAPI_EXPECT(XLabel_heightForWidth(&wrap, -1) == hint.height,
                    "XLabel heightForWidth(-1) 与 sizeHint 同口径");
        hint = XLabel_minimumSizeHint(&wrap);
        XAPI_EXPECT(hint.width > 0 && hint.height > 0,
                    "XLabel 文本最小尺寸提示非零");
    }

    /* ---- 链接信号（Qt linkHovered/linkActivated；经合成鼠标事件
     * 直发，坐标为控件本地坐标；未 show 直调）。 ---- */
    XLabel_init(&linkLabel, NULL, 0);
    XWidget_setGeometry((XWidget*)&linkLabel.m_base, 0, 0, 160, 32);
    XLabel_setText_2(&linkLabel, "<a href=\"xy://demo\">CLICK</a>");
    /* 只读字段校验：富文本链接表已记录 1 条 <a href>（对标 Qt
     * 链接区间记录；无公开计数 getter，故只读访问）。 */
    XAPI_EXPECT(linkLabel.m_linkCount == 1,
                "XLabel 富文本 <a href> 记录 1 条链接");
    g_labels_linkActivatedCount = 0;
    g_labels_linkHoveredCount = 0;
    g_labels_lastLink[0] = '\0';
    XObject_connect_1((XObject*)&linkLabel,
                      (size_t)XLabel_linkHovered_signal(NULL, NULL),
                      (XObject*)&linkLabel, labels_linkHoveredSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&linkLabel,
                      (size_t)XLabel_linkActivated_signal(NULL, NULL),
                      (XObject*)&linkLabel, labels_linkActivatedSlot,
                      XConnectionType_Direct);
    /* 悬停于链接字形上（Left|VCenter 单行文本带覆盖 y=16，行首字形
     * 覆盖 x∈[0,advance)）：发射 linkHovered(href)。 */
    labels_injectMouse((XWidget*)&linkLabel.m_base, XEVENT_TYPE_MOUSE_MOVE, 4, 16);
    XAPI_EXPECT(g_labels_linkHoveredCount == 1 &&
                    strcmp(g_labels_lastLink, "xy://demo") == 0,
                "XLabel 悬停链接发射 linkHovered(href)");
    /* 链接上按下再释放于同一链接：发射 linkActivated(href)（Qt
     * pressed/released 命中同一锚点语义）。 */
    labels_injectMouse((XWidget*)&linkLabel.m_base, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                       4, 16);
    labels_injectMouse((XWidget*)&linkLabel.m_base, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                       4, 16);
    XAPI_EXPECT(g_labels_linkActivatedCount == 1 &&
                    strcmp(g_labels_lastLink, "xy://demo") == 0,
                "XLabel 链接点击发射 linkActivated(href)");

    /* 离屏绘制扩展入口对 NULL painter 为无操作（头文件契约冒烟；
     * 渲染效果不在本套件断言职责内）。 */
    XLabel_drawContents(&linkLabel, NULL);

    XLabel_deinit_base(&linkLabel);
    XLabel_deinit_base(&wrap);
    XLabel_deinit_base(&selectable);
    XLabel_deinit_base(&label);
    XWidget_deinit_base(&buddy);
    return failures;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

/* ==================== XGroupBox：分组框（对标 QGroupBox） ==================== */

#if XWIDGET_ON && XGROUPBOX_ON

/* ---- toggled/clicked 信号捕获 ---- */
static int g_labels_gbToggledCount = 0;
static int g_labels_gbClickedCount = 0;
static bool g_labels_gbLastToggled = false;
static bool g_labels_gbLastClicked = false;

static void labels_gbToggledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checked);
    ++g_labels_gbToggledCount;
    g_labels_gbLastToggled = checked;
}

static void labels_gbClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, bool, checked);
    ++g_labels_gbClickedCount;
    g_labels_gbLastClicked = checked;
}

static int labels_groupbox(void)
{
    int failures = 0;
    XGroupBox box;
    XGroupBox box2;
    XLabel child;

    XGroupBox_init(&box, NULL, 0);
    XWidget_setGeometry((XWidget*)&box.m_base, 0, 0, 200, 120);

    /* 默认值（Qt QGroupBox 构造：无标题、左对齐、非 flat、
     * 不可勾选、未勾选）。 */
    XAPI_EXPECT(strcmp(xapi_cstr(XGroupBox_title(&box)), "") == 0,
                "XGroupBox 默认无标题");
    XAPI_EXPECT(XGroupBox_alignment(&box) == XAlignment_Left,
                "XGroupBox 默认标题左对齐");
    XAPI_EXPECT(!XGroupBox_isFlat(&box), "XGroupBox 默认非 flat");
    XAPI_EXPECT(!XGroupBox_isCheckable(&box), "XGroupBox 默认不可勾选");
    XAPI_EXPECT(!XGroupBox_isChecked(&box), "XGroupBox 默认未勾选");

    /* 标题与对齐（Qt setTitle/title、setAlignment 往返）。 */
    XGroupBox_setTitle(&box, "网络设置");
    XAPI_EXPECT(strcmp(xapi_cstr(XGroupBox_title(&box)), "网络设置") == 0,
                "XGroupBox setTitle/title 往返");
    /* 本库几何口径：标题区高 14+2*2=18，contentsRect 从标题下方
     * 1px 边框内开始（对标 QGroupBox::contentsRect 概念；Qt 无固定
     * 数值，不硬断言 Qt 值）。 */
    {
        XRect cr = XGroupBox_contentsRect(&box);
        XAPI_EXPECT(cr.y == 19 && cr.x == 1,
                    "XGroupBox 有标题内容区自标题区下方开始");
    }
    XGroupBox_setTitle(&box, NULL);
    XAPI_EXPECT(strcmp(xapi_cstr(XGroupBox_title(&box)), "") == 0,
                "XGroupBox setTitle(NULL) 清空标题");
    {
        XRect cr = XGroupBox_contentsRect(&box);
        XAPI_EXPECT(cr.y == 1,
                    "XGroupBox 无标题内容区仅留 1px 边框");
    }
    XGroupBox_setAlignment(&box, XAlignment_HCenter);
    XAPI_EXPECT(XGroupBox_alignment(&box) == XAlignment_HCenter,
                "XGroupBox setAlignment(HCenter) 往返");
    /* 头文件口径：仅水平分量（Left/HCenter/Right）生效。 */
    XGroupBox_setAlignment(&box, XAlignment_Right | XAlignment_VCenter);
    XAPI_EXPECT(XGroupBox_alignment(&box) == XAlignment_Right,
                "XGroupBox 标题对齐仅保留水平分量");
    XGroupBox_setAlignment(&box, XAlignment_Top);
    XAPI_EXPECT(XGroupBox_alignment(&box) == XAlignment_Left,
                "XGroupBox 非法标题对齐回退 Left");
    XGroupBox_setFlat(&box, true);
    XAPI_EXPECT(XGroupBox_isFlat(&box), "XGroupBox setFlat(true) 往返");
    XGroupBox_setFlat(&box, false);
    XAPI_EXPECT(!XGroupBox_isFlat(&box), "XGroupBox setFlat(false) 往返");

    /* 非勾选分组 setChecked 无效（Qt：checkable=false 时
     * setChecked 保持原状态）。 */
    XGroupBox_init(&box2, NULL, 0);
    XGroupBox_setChecked(&box2, true);
    XAPI_EXPECT(!XGroupBox_isChecked(&box2),
                "XGroupBox 非勾选分组 setChecked 无效");

    /* checkable 状态迁移（Qt setCheckable(true)：初始勾选并发射
     * toggled(true)、取 StrongFocus）。 */
    g_labels_gbToggledCount = 0;
    g_labels_gbClickedCount = 0;
    XLabel_init(&child, (XWidget*)&box.m_base, 0);
    XGroupBox_setTitle(&box, "设备组");
    XObject_connect_1((XObject*)&box,
                      (size_t)XGroupBox_toggled_signal(NULL, false),
                      (XObject*)&box, labels_gbToggledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&box,
                      (size_t)XGroupBox_clicked_signal(NULL, false),
                      (XObject*)&box, labels_gbClickedSlot,
                      XConnectionType_Direct);
    XGroupBox_setCheckable(&box, true);
    XAPI_EXPECT(XGroupBox_isChecked(&box) && g_labels_gbToggledCount == 1 &&
                    g_labels_gbLastToggled,
                "XGroupBox 启用 checkable 即初始勾选并发 toggled(true)");
    XAPI_EXPECT((XWidget_focusPolicy((XWidget*)&box.m_base) &
                 XWidgetFocusPolicy_StrongFocus) != 0,
                "XGroupBox 启用 checkable 取 StrongFocus");

    /* 勾选切换联动子控件可用性（Qt _q_setChildrenEnabled：未勾选
     * 递归禁用、勾选恢复）。 */
    XGroupBox_setChecked(&box, false);
    XAPI_EXPECT(!XWidget_isEnabled((XWidget*)&child.m_base) &&
                    g_labels_gbToggledCount == 2 && !g_labels_gbLastToggled,
                "XGroupBox 取消勾选禁用子控件并发 toggled(false)");
    XGroupBox_setChecked(&box, true);
    XAPI_EXPECT(XWidget_isEnabled((XWidget*)&child.m_base) &&
                    g_labels_gbToggledCount == 3,
                "XGroupBox 重新勾选恢复子控件可用");
    /* 重复 set 同值不发射（Qt setChecked 同值无 toggled）。 */
    XGroupBox_setChecked(&box, true);
    XAPI_EXPECT(g_labels_gbToggledCount == 3,
                "XGroupBox 重复 setChecked 同值不再发射 toggled");
    XAPI_EXPECT(g_labels_gbClickedCount == 0,
                "XGroupBox 程序化 setChecked 不发射 clicked");

    /* 鼠标点击标题区切换（Qt clicked(bool)/toggled(bool)；本库命中
     * 整个标题区 y∈[0,18)，行为近似 Qt 勾选框+标题行）。 */
    labels_injectMouse((XWidget*)&box.m_base, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 60, 8);
    labels_injectMouse((XWidget*)&box.m_base, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 60, 8);
    XAPI_EXPECT(!XGroupBox_isChecked(&box) &&
                    g_labels_gbToggledCount == 4 && !g_labels_gbLastToggled &&
                    g_labels_gbClickedCount == 1 && !g_labels_gbLastClicked,
                "XGroupBox 点击标题区切换并发 clicked(false)");
    XAPI_EXPECT(!XWidget_isEnabled((XWidget*)&child.m_base),
                "XGroupBox 点击取消勾选后子控件禁用");
    /* 标题区外释放不切换（Qt 指示器点击语义：按压与释放须同区）。 */
    labels_injectMouse((XWidget*)&box.m_base, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 60, 8);
    labels_injectMouse((XWidget*)&box.m_base, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 60, 100);
    XAPI_EXPECT(!XGroupBox_isChecked(&box) &&
                    g_labels_gbClickedCount == 1,
                "XGroupBox 标题区外释放不切换不发射");

    /* 关闭 checkable（本库口径：若此前已勾选，取消选中并发射
     * toggled(false)、恢复子控件可用；已未勾选则不重复发射。Qt 6.8.3
     * qgroupbox.cpp setCheckable(false) 仅恢复子控件可用且不发射
     * toggled——见函数 XGroupBox_setCheckable 注释。先重新勾选以覆盖
     * 「已勾选→关闭」的补发路径）。 */
    XGroupBox_setChecked(&box, true);
    XAPI_EXPECT(XGroupBox_isChecked(&box) && g_labels_gbToggledCount == 5,
                "XGroupBox 重新勾选回计数（进入关闭 checkable 前置）");
    XGroupBox_setCheckable(&box, false);
    XAPI_EXPECT(!XGroupBox_isCheckable(&box) && !XGroupBox_isChecked(&box) &&
                    g_labels_gbToggledCount == 6 && !g_labels_gbLastToggled,
                "XGroupBox 关闭 checkable 取消勾选并发 toggled(false)");
    XAPI_EXPECT(XWidget_isEnabled((XWidget*)&child.m_base),
                "XGroupBox 关闭 checkable 恢复子控件可用");

    /* NULL 口径（头文件：NULL 返回默认值）。 */
    XAPI_EXPECT(strcmp(xapi_cstr(XGroupBox_title(NULL)), "") == 0 &&
                    XGroupBox_alignment(NULL) == XAlignment_Left &&
                    !XGroupBox_isFlat(NULL) && !XGroupBox_isCheckable(NULL) &&
                    !XGroupBox_isChecked(NULL),
                "XGroupBox NULL 查询返回默认值");

    XLabel_deinit_base(&child);
    XGroupBox_deinit_base(&box2);
    XGroupBox_deinit_base(&box);
    return failures;
}

#endif /* XWIDGET_ON && XGROUPBOX_ON */

/* ==================== XLcdNumber：LCD 数码管（对标 QLCDNumber） ==================== */

#if XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON

static int g_labels_lcdOverflowCount = 0;

static void labels_lcdOverflowSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args; /* overflow() 无参数。 */
    ++g_labels_lcdOverflowCount;
}

static int labels_lcd(void)
{
    int failures = 0;
    XLcdNumber lcd;

    XLcdNumber_init(&lcd, NULL, 0);

    /* 默认值（Qt QLCDNumber 构造：5 位、Dec、Filled、非小数点并入、
     * setFrameStyle(Box|Raised)、值 0）。 */
    XAPI_EXPECT(XLcdNumber_digitCount(&lcd) == 5, "XLcdNumber 默认位数=5");
    XAPI_EXPECT(XLcdNumber_mode(&lcd) == XLcdNumberMode_Dec,
                "XLcdNumber 默认进制=Dec");
    XAPI_EXPECT(XLcdNumber_segmentStyle(&lcd) ==
                XLcdNumberSegmentStyle_Filled,
                "XLcdNumber 默认段风格=Filled");
    XAPI_EXPECT(!XLcdNumber_smallDecimalPoint(&lcd),
                "XLcdNumber 默认小数点独占位");
    XAPI_EXPECT(XLcdNumber_frameStyle(&lcd) ==
                    ((int)XFrameShape_Box | (int)XFrameShadow_Raised),
                "XLcdNumber 默认边框=Box|Raised");
    XAPI_EXPECT(XLcdNumber_value(&lcd) == 0.0 &&
                    XLcdNumber_intValue(&lcd) == 0,
                "XLcdNumber 默认数值=0");

    /* 位数钳位（Qt setDigitCount 0..99 越界钳位 + 警告）。 */
    XLcdNumber_setDigitCount(&lcd, 8);
    XAPI_EXPECT(XLcdNumber_digitCount(&lcd) == 8,
                "XLcdNumber setDigitCount(8) 往返");
    XLcdNumber_setDigitCount(&lcd, 200);
    XAPI_EXPECT(XLcdNumber_digitCount(&lcd) == 99,
                "XLcdNumber 位数上界钳位 99");
    XLcdNumber_setDigitCount(&lcd, -3);
    XAPI_EXPECT(XLcdNumber_digitCount(&lcd) == 0,
                "XLcdNumber 位数下界钳位 0");

    /* 段风格与小数点模式（Qt setSegmentStyle/setSmallDecimalPoint）。 */
    XLcdNumber_setDigitCount(&lcd, 5);
    XLcdNumber_setSegmentStyle(&lcd, XLcdNumberSegmentStyle_Flat);
    XAPI_EXPECT(XLcdNumber_segmentStyle(&lcd) == XLcdNumberSegmentStyle_Flat,
                "XLcdNumber setSegmentStyle(Flat) 往返");
    XLcdNumber_setSegmentStyle(&lcd, XLcdNumberSegmentStyle_Filled);
    XLcdNumber_setSmallDecimalPoint(&lcd, true);
    XAPI_EXPECT(XLcdNumber_smallDecimalPoint(&lcd),
                "XLcdNumber setSmallDecimalPoint(true) 往返");
    XLcdNumber_setSmallDecimalPoint(&lcd, false);

    /* 进制便捷槽（Qt setHexMode/setDecMode/setOctMode/setBinMode）。 */
    XLcdNumber_setHexMode(&lcd);
    XAPI_EXPECT(XLcdNumber_mode(&lcd) == XLcdNumberMode_Hex,
                "XLcdNumber setHexMode 切换 Hex");
    XLcdNumber_setOctMode(&lcd);
    XAPI_EXPECT(XLcdNumber_mode(&lcd) == XLcdNumberMode_Oct,
                "XLcdNumber setOctMode 切换 Oct");
    XLcdNumber_setBinMode(&lcd);
    XAPI_EXPECT(XLcdNumber_mode(&lcd) == XLcdNumberMode_Bin,
                "XLcdNumber setBinMode 切换 Bin");
    XLcdNumber_setDecMode(&lcd);
    XAPI_EXPECT(XLcdNumber_mode(&lcd) == XLcdNumberMode_Dec,
                "XLcdNumber setDecMode 切回 Dec");

    /* 溢出预估（Qt checkOverflow(int)/checkOverflow(double)：
     * 返回 true 表示无法按当前位数/进制显示。浮点 Dec 口径对标
     * qlcdnumber.cpp double2string：%*.*g 精度自 ndigits 逐级收缩，
     * 固定表示装不下时回退指数表示（123456→"1e+05" 5 位可显、不算
     * 溢出；负值 "-1e+05" 6 位装不下才算溢出）。 */
    XLcdNumber_setDigitCount(&lcd, 2);
    XAPI_EXPECT(!XLcdNumber_checkOverflowInt(&lcd, 99) &&
                    XLcdNumber_checkOverflowInt(&lcd, 100),
                "XLcdNumber checkOverflowInt 2 位 99 可显 100 溢出");
    XLcdNumber_setDigitCount(&lcd, 5);
    XAPI_EXPECT(!XLcdNumber_checkOverflowDouble(&lcd, 12345.0) &&
                    !XLcdNumber_checkOverflowDouble(&lcd, 123456.0) &&
                    XLcdNumber_checkOverflowDouble(&lcd, -123456.0),
                "XLcdNumber checkOverflowDouble 5 位 12345/123456(1e+05) 可显 负值溢出");

    /* 显示槽与数值（Qt display(int)/display(double) + value/intValue
     * qRound 语义）。 */
    XLcdNumber_display_2(&lcd, 42);
    XAPI_EXPECT(XLcdNumber_value(&lcd) == 42.0 &&
                    XLcdNumber_intValue(&lcd) == 42,
                "XLcdNumber display(int) 同步 value/intValue");
    XLcdNumber_display_3(&lcd, 1.5);
    XAPI_EXPECT(XLcdNumber_value(&lcd) == 1.5,
                "XLcdNumber display(double) 同步 value");
    XLcdNumber_display_3(&lcd, 2.6);
    XAPI_EXPECT(XLcdNumber_intValue(&lcd) == 3,
                "XLcdNumber intValue 四舍五入 2.6→3");

    /* 字符串显示（P2 批次起对标 Qt 6.8 整串解析：QString::toDouble
     * 失败置 0，"12.5px" 整串不可解析→0.0）。 */
    XLcdNumber_display(&lcd, "12.5px");
    XAPI_EXPECT(XLcdNumber_value(&lcd) == 0.0,
                "XLcdNumber display 整串解析失败置 0（对标 Qt toDouble）");
    XLcdNumber_display(&lcd, "xyz");
    XAPI_EXPECT(XLcdNumber_value(&lcd) == 0.0,
                "XLcdNumber display 不可解析字符串置 0");

    /* 溢出信号（Qt overflow()：显示超容量时发射并保留旧显示；
     * 显示串经公开字段只读校验——QLCDNumber 同样无显示串 getter）。 */
    XLcdNumber_setDigitCount(&lcd, 2);
    XLcdNumber_display_2(&lcd, 99);
    g_labels_lcdOverflowCount = 0;
    XObject_connect_1((XObject*)&lcd,
                      (size_t)XLcdNumber_overflow_signal(NULL),
                      (XObject*)&lcd, labels_lcdOverflowSlot,
                      XConnectionType_Direct);
    XLcdNumber_display_2(&lcd, 100);
    XAPI_EXPECT(g_labels_lcdOverflowCount == 1 &&
                    strcmp(lcd.m_digitStr, "99") == 0,
                "XLcdNumber 溢出发射 overflow 并保留旧显示 99");

    /* 进制切换按当前值重显（Qt setMode 后以 d->val 重新格式化）。 */
    XLcdNumber_display_2(&lcd, 255);
    XLcdNumber_setHexMode(&lcd);
    XAPI_EXPECT(XLcdNumber_value(&lcd) == 255.0 &&
                    XLcdNumber_intValue(&lcd) == 255,
                "XLcdNumber 切 Hex 后按当前值保留数值语义");

    /* NULL 口径（头文件：NULL 返回默认值）。 */
    XAPI_EXPECT(XLcdNumber_digitCount(NULL) == 0 &&
                    XLcdNumber_mode(NULL) == XLcdNumberMode_Dec &&
                    XLcdNumber_value(NULL) == 0.0,
                "XLcdNumber NULL 查询返回默认值");

    XLcdNumber_deinit_base(&lcd);
    return failures;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON */

/* ==================== XProgressBar：进度条（对标 QProgressBar） ==================== */

#if XWIDGET_ON && XPROGRESSBAR_ON

static int g_labels_pbValueChangedCount = 0;
static int g_labels_pbLastValue = -1;

static void labels_pbValueChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, value);
    ++g_labels_pbValueChangedCount;
    g_labels_pbLastValue = value;
}

static int labels_progress(void)
{
    int failures = 0;
    XProgressBar bar;
    XProgressBar bar2;
    XProgressBar bar3;
    char text[64];

    XProgressBar_init(&bar, NULL, 0);

    /* 默认值（Qt QProgressBar 构造：范围 0..100、值 0、文本可见、
     * 格式 "%p%"、居中对齐、水平、非翻转、TopToBottom）。 */
    XAPI_EXPECT(XProgressBar_minimum(&bar) == 0, "XProgressBar 默认下限=0");
    XAPI_EXPECT(XProgressBar_maximum(&bar) == 100, "XProgressBar 默认上限=100");
    XAPI_EXPECT(XProgressBar_value(&bar) == 0, "XProgressBar 默认值=0");
    XAPI_EXPECT(XProgressBar_isTextVisible(&bar), "XProgressBar 默认文本可见");
    XAPI_EXPECT(strcmp(xapi_cstr(XProgressBar_format(&bar)), "%p%") == 0,
                "XProgressBar 默认格式=%p%");
    XAPI_EXPECT(XProgressBar_alignment(&bar) ==
                    (XAlignment_HCenter | XAlignment_VCenter),
                "XProgressBar 默认文本居中");
    XAPI_EXPECT(XProgressBar_orientation(&bar) ==
                XProgressBarOrientation_Horizontal,
                "XProgressBar 默认水平方向");
    XAPI_EXPECT(!XProgressBar_invertedAppearance(&bar),
                "XProgressBar 默认不翻转外观");
    XAPI_EXPECT(XProgressBar_textDirection(&bar) ==
                XProgressBarDirection_TopToBottom,
                "XProgressBar 默认文本自上而下");

    /* ---- 值迁移与 valueChanged（Qt setValue：实际变化才发射，
     * 越界钳位到 [min,max]）。 ---- */
    g_labels_pbValueChangedCount = 0;
    g_labels_pbLastValue = -1;
    XObject_connect_1((XObject*)&bar,
                      (size_t)XProgressBar_valueChanged_signal(NULL, 0),
                      (XObject*)&bar, labels_pbValueChangedSlot,
                      XConnectionType_Direct);
    XProgressBar_setValue(&bar, 50);
    XAPI_EXPECT(XProgressBar_value(&bar) == 50 &&
                    g_labels_pbValueChangedCount == 1 &&
                    g_labels_pbLastValue == 50,
                "XProgressBar setValue(50) 发 valueChanged(50)");
    XProgressBar_setValue(&bar, 50);
    XAPI_EXPECT(g_labels_pbValueChangedCount == 1,
                "XProgressBar 重复 setValue 同值不再发射");
    XProgressBar_setValue(&bar, 150);
    XAPI_EXPECT(XProgressBar_value(&bar) == 100 &&
                    g_labels_pbLastValue == 100,
                "XProgressBar setValue 越界钳位到上限 100");
    XProgressBar_setValue(&bar, -10);
    XAPI_EXPECT(XProgressBar_value(&bar) == 0 &&
                    g_labels_pbValueChangedCount == 3,
                "XProgressBar setValue 越界钳位到下限 0");

    /* 格式化文本（Qt %p 百分比 / %v 当前值 / %m 最大值 + setFormat/
     * resetFormat/format 往返）。 */
    XProgressBar_text(&bar, text, sizeof(text));
    XAPI_EXPECT(strcmp(text, "0%") == 0,
                "XProgressBar %p 百分比文本=0%");
    XProgressBar_setFormat(&bar, "%v/%m");
    XAPI_EXPECT(strcmp(xapi_cstr(XProgressBar_format(&bar)), "%v/%m") == 0,
                "XProgressBar setFormat 往返");
    XProgressBar_setValue(&bar, 30);
    XProgressBar_text(&bar, text, sizeof(text));
    XAPI_EXPECT(strcmp(text, "30/100") == 0,
                "XProgressBar %v/%m 数值文本=30/100");
    XProgressBar_resetFormat(&bar);
    XAPI_EXPECT(strcmp(xapi_cstr(XProgressBar_format(&bar)), "%p%") == 0,
                "XProgressBar resetFormat 恢复 %p%");
    XProgressBar_setFormat(&bar, NULL);
    XAPI_EXPECT(strcmp(xapi_cstr(XProgressBar_format(&bar)), "%p%") == 0,
                "XProgressBar setFormat(NULL) 恢复默认 %p%");

    /* reset 复位到下限且不发信号（Qt reset：value==minimum 而无
     * valueChanged）。 */
    XProgressBar_reset(&bar);
    XAPI_EXPECT(XProgressBar_value(&bar) == 0 &&
                    g_labels_pbValueChangedCount == 4,
                "XProgressBar reset 复位到下限且不发 valueChanged");

    /* ---- 范围端点联动（Qt setRange = setMinimum(min) +
     * setMaximum(qMax(max, min))：min>max 收敛为 (min,min)，不交换；
     * setMinimum 抬升上限 qMax、setMaximum 下压下限 qMin）。 ---- */
    XProgressBar_init(&bar2, NULL, 0);
    XProgressBar_setRange(&bar2, 50, 10);
    XAPI_EXPECT(XProgressBar_minimum(&bar2) == 50 &&
                    XProgressBar_maximum(&bar2) == 50,
                "XProgressBar setRange(50,10) 收敛为 (50,50)");
    XProgressBar_setMinimum(&bar2, 200);
    XAPI_EXPECT(XProgressBar_minimum(&bar2) == 200 &&
                    XProgressBar_maximum(&bar2) == 200,
                "XProgressBar setMinimum 抬升上限到新下限");
    XProgressBar_init(&bar3, NULL, 0);
    XProgressBar_setMaximum(&bar3, 5);
    XAPI_EXPECT(XProgressBar_maximum(&bar3) == 5 &&
                    XProgressBar_minimum(&bar3) == 0,
                "XProgressBar setMaximum(5) 下限保持 0");

    /* ---- 外观属性（Qt setOrientation/setInvertedAppearance/
     * setTextDirection/setTextVisible/setAlignment；本库口径：方向
     * 与文本方向的非法值忽略）。 ---- */
    XProgressBar_setOrientation(&bar, XProgressBarOrientation_Vertical);
    XAPI_EXPECT(XProgressBar_orientation(&bar) ==
                XProgressBarOrientation_Vertical,
                "XProgressBar setOrientation(Vertical) 往返");
    XProgressBar_setOrientation(&bar, 99);
    XAPI_EXPECT(XProgressBar_orientation(&bar) ==
                XProgressBarOrientation_Vertical,
                "XProgressBar 非法方向值忽略");
    XProgressBar_setOrientation(&bar, XProgressBarOrientation_Horizontal);
    XProgressBar_setInvertedAppearance(&bar, true);
    XAPI_EXPECT(XProgressBar_invertedAppearance(&bar),
                "XProgressBar setInvertedAppearance(true) 往返");
    XProgressBar_setTextDirection(&bar, XProgressBarDirection_BottomToTop);
    XAPI_EXPECT(XProgressBar_textDirection(&bar) ==
                XProgressBarDirection_BottomToTop,
                "XProgressBar setTextDirection(BottomToTop) 往返");
    XProgressBar_setTextDirection(&bar, 99);
    XAPI_EXPECT(XProgressBar_textDirection(&bar) ==
                XProgressBarDirection_BottomToTop,
                "XProgressBar 非法文本方向值忽略");
    XProgressBar_setTextVisible(&bar, false);
    XAPI_EXPECT(!XProgressBar_isTextVisible(&bar),
                "XProgressBar setTextVisible(false) 往返");
    XProgressBar_setAlignment(&bar, XAlignment_Left | XAlignment_VCenter);
    XAPI_EXPECT(XProgressBar_alignment(&bar) ==
                    (XAlignment_Left | XAlignment_VCenter),
                "XProgressBar setAlignment(Left|VCenter) 往返");

    /* NULL 口径（头文件：NULL 返回默认值）。 */
    XAPI_EXPECT(XProgressBar_value(NULL) == 0 &&
                    XProgressBar_minimum(NULL) == 0 &&
                    XProgressBar_maximum(NULL) == 100 &&
                    XProgressBar_isTextVisible(NULL),
                "XProgressBar NULL 查询返回默认值");

    XProgressBar_deinit_base(&bar3);
    XProgressBar_deinit_base(&bar2);
    XProgressBar_deinit_base(&bar);
    return failures;
}

#endif /* XWIDGET_ON && XPROGRESSBAR_ON */

/* ==================== 族入口 ==================== */

int xapi_labels_run(void)
{
    int failures = 0;

#if XWIDGET_ON && XFRAME_ON
    failures += labels_frame();
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    failures += labels_label();
#endif
#if XWIDGET_ON && XGROUPBOX_ON
    failures += labels_groupbox();
#endif
#if XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON
    failures += labels_lcd();
#endif
#if XWIDGET_ON && XPROGRESSBAR_ON
    failures += labels_progress();
#endif

    XPrintf("XGuiApiTest: [显示族 labels] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
