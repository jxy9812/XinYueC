/**
 * @file       XAbstractButtonAlignTest.c
 * @brief      XAbstractButton × Qt 6.8 QAbstractButton 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qabstractbutton.h
 *             与 Qt 6.8.3 源码 …/src/widgets/widgets/qabstractbutton.cpp，
 *             关键行为用本机 Qt 6.8 探针（QT_QPA_PLATFORM=offscreen）实测：
 *             - 构造默认值：text 空、icon 空、iconSize 取样式
 *               PM_ButtonIconSize（qabstractbutton.cpp:1234-1241，缺省
 *               16x16）、不可选中、未选中、未按下、
 *               autoRepeat/autoExclusive=false、
 *               autoRepeatDelay=300 / autoRepeatInterval=100、
 *               shortcut 未设置、group==NULL；
 *             - click() 的信号顺序（qabstractbutton.cpp:825-841，
 *               探针实测「pressed → toggled(1) → released → clicked(1)」），
 *               非可选中按钮无 toggled（探针：「pressed, released,
 *               clicked(0)」）；
 *             - setCheckable(false) 静默清 checked、不发 toggled
 *               （qabstractbutton.cpp:560-568，探针 events 为空）；
 *             - 独占组内当前选中项不可被点掉 / 不可 setChecked(false)
 *               （qabstractbutton.cpp:351-362 + 593-604，changeState 闸）；
 *             - 鼠标按下→释放于按钮内=一次 click，释放于按钮外=不发 clicked；
 *             - Space 按下/释放=一次 click；Escape 只释放不点击；
 *             - Key_Select（Qt ButtonPressKeys 缺省集合第二项，
 *               qplatformtheme.cpp:637-638）与 Space 同权；
 *             - autoExclusive 候选集递归（Qt queryButtonList 用
 *               parent->findChildren，qabstractbutton.cpp:153-174）：
 *               直接子按钮的候选集含深层容器里的按钮，反向不一定对称；
 *             - 组内方向键 moveFocus（qabstractbutton.cpp:213-300）：
 *               焦点迁移 + 独占组内选中随焦点点击迁移；
 *             - 助记符 '&'/'&&'：显示/度量去标记、shortcut 写成 Alt+字符、
 *               无 '&' 的 setText 清空 shortcut（qabstractbutton.cpp:482-506
 *               + qkeysequence.cpp:935-967，本机 Qt 探针实测宽度/文本）；
 *             - 禁用按钮 click() 无任何信号；
 *             - 父子析构由顶层带走（泄漏门由运行器判定）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON

#include "XAbstractButton.h"
#include "XAbstractButton_Protected.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h" /* XObject_event_base（事件注入路径） */
#include "XEvent.h"
#include "XEventType.h"
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"
#include "XShortcut.h"

#include <string.h>

/*
 * Qt 6.8 Qt::Key_Select = 0x01010000（QtCore/qnamespace.h:1016，
 * 「Keypad navigation keys」段）。XEvent.h 的 XKey 枚举由核心事件批次
 * 维护、尚未收录该项；按键码以 int 承载，测试按同值注入以锁定
 * ButtonPressKeys 第二项的语义。
 */
#define XAB_KEY_SELECT 0x01010000

/** @brief 信号顺序记录（p/r/c/t 各一位，按到达顺序拼进 g_xabOrder）。 */
static char g_xabOrder[32];
static int g_xabOrderLen;
static bool g_xabLastChecked;
static int g_xabToggledCount;

static void xab_resetOrder(void)
{
    g_xabOrder[0] = '\0';
    g_xabOrderLen = 0;
    g_xabLastChecked = false;
    g_xabToggledCount = 0;
}

static void xab_push(char tag)
{
    if (g_xabOrderLen < (int)sizeof(g_xabOrder) - 1) {
        g_xabOrder[g_xabOrderLen++] = tag;
        g_xabOrder[g_xabOrderLen] = '\0';
    }
}

static void xab_pressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xab_push('p');
}

static void xab_releasedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    xab_push('r');
}

static void xab_clickedSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, bool, checked);

    (void)receiver;
    g_xabLastChecked = checked;
    xab_push('c');
}

static void xab_toggledSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, bool, checked);

    (void)receiver;
    g_xabLastChecked = checked;
    ++g_xabToggledCount;
    xab_push('t');
}

/** @brief 接满 pressed/released/clicked/toggled 四路，按到达顺序记录。 */
static void xab_hook(XAbstractButton* button)
{
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_pressed_signal(NULL),
                      (XObject*)button, xab_pressedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_released_signal(NULL),
                      (XObject*)button, xab_releasedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_clicked_signal(NULL, false),
                      (XObject*)button, xab_clickedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)button,
                      (size_t)XAbstractButton_toggled_signal(NULL, false),
                      (XObject*)button, xab_toggledSlot,
                      XConnectionType_Direct);
}

/** @brief 向控件直发一次鼠标事件（坐标=控件本地坐标）。 */
static void xab_mouse(XWidget* target, XEventType type, int x, int y,
                      XMouseButton button)
{
    XMouseEvent me;
    XPoint pos;

    if (!target)
        return;
    XPoint_init(&pos, x, y);
    XMouseEvent_init(&me, type, button, 0, pos);
    XObject_event_base((XObject*)target, (XEvent*)&me);
}

/** @brief 向控件直发一次键盘事件。 */
static void xab_key(XWidget* target, XEventType type, int key)
{
    XKeyEvent ke;

    if (!target)
        return;
    XKeyEvent_init(&ke, type, key, 0);
    XObject_event_base((XObject*)target, (XEvent*)&ke);
}

/** @brief 按钮文本的 UTF-8 借用指针（空串归一，XString 空串 toUtf8 为 NULL）。 */
static const char* xab_text(const XAbstractButton* button)
{
    const XString* text = XAbstractButton_text(button);
    const char* utf8 = text ? XString_toUtf8(text) : NULL;

    return utf8 ? utf8 : "";
}

/* ==================== ① 构造默认值 + 属性往返 + 父子所有权 ==================== */

XQ_TEST(XAbstractButton, constructorDefaultsAndSetters)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* button;
    const XVector* children;
    XIcon icon;
    XSize size;
    XString* text;

    XQ_REQUIRE(host != NULL);
    XWidget_setGeometry(host, 0, 0, 200, 120);
    button = XAbstractButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);

    /* 默认值（QAbstractButtonPrivate 构造 + 头文件缺省，
     * qabstractbutton.cpp:140-150 AUTO_REPEAT_DELAY/INTERVAL）。 */
    XQ_EQ_STR(xab_text(button), "");
    icon = XAbstractButton_icon(button);
    XQ_CHECK(XIcon_isNull(&icon));
    XClassDeinit((XClass*)&icon);
    size = XAbstractButton_iconSize(button);
    /* Qt 6.8.3 qabstractbutton.cpp:1234-1241：未显式设置时返回
     * style()->pixelMetric(PM_ButtonIconSize)（Fusion/本库缺省 16x16），
     * 而不是 (0,0)。 */
    XQ_EQ_I64(size.width, 16);
    XQ_EQ_I64(size.height, 16);
    XQ_CHECK(!XAbstractButton_isCheckable(button));
    XQ_CHECK(!XAbstractButton_isChecked(button));
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_CHECK(!XAbstractButton_autoRepeat(button));
    XQ_CHECK(!XAbstractButton_autoExclusive(button));
    XQ_EQ_I64(XAbstractButton_autoRepeatDelay(button), 300);
    XQ_EQ_I64(XAbstractButton_autoRepeatInterval(button), 100);
    XQ_CHECK(XAbstractButton_shortcut(button) == NULL);
    XQ_CHECK(XAbstractButton_group(button) == NULL);
    /* Qt 构造把焦点策略设为 StrongFocus（SH_Button_FocusPolicy）。 */
    XQ_EQ_I64(XWidget_focusPolicy((XWidget*)button),
              XWidgetFocusPolicy_StrongFocus);

    /* 文本/图标尺寸/自动重复属性往返。 */
    XAbstractButton_setText_2(button, "OK");
    XQ_EQ_STR(xab_text(button), "OK");
    text = XString_create_utf8("确定");
    XQ_REQUIRE(text != NULL);
    XAbstractButton_setText(button, text);
    XQ_EQ_STR(xab_text(button), "确定");
    XClassDelete((XClass*)text); /* 拷贝语义：源串随调用方释放 */
    XQ_EQ_STR(xab_text(button), "确定");

    XSize_init(&size, 24, 18);
    XAbstractButton_setIconSize(button, &size);
    size = XAbstractButton_iconSize(button);
    XQ_EQ_I64(size.width, 24);
    XQ_EQ_I64(size.height, 18);

    XAbstractButton_setAutoRepeat(button, true);
    XQ_CHECK(XAbstractButton_autoRepeat(button));
    XAbstractButton_setAutoRepeatDelay(button, 500);
    XAbstractButton_setAutoRepeatInterval(button, 40);
    XQ_EQ_I64(XAbstractButton_autoRepeatDelay(button), 500);
    XQ_EQ_I64(XAbstractButton_autoRepeatInterval(button), 40);
    XAbstractButton_setAutoRepeat(button, false);

    /* 可选中/选中往返；setChecked 只在可选中时生效。 */
    XAbstractButton_setChecked(button, true);
    XQ_CHECK(!XAbstractButton_isChecked(button));
    XAbstractButton_setCheckable(button, true);
    XQ_CHECK(XAbstractButton_isCheckable(button));
    XAbstractButton_setChecked(button, true);
    XQ_CHECK(XAbstractButton_isChecked(button));

    /* 父子所有权：按钮挂在 host 的子表里，父析构带走按钮。 */
    children = XObject_children((XObject*)host);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 1);

    XClassDelete((XClass*)host); /* button 随父释放（泄漏门判定） */
}

/* ==================== ② click() 信号顺序 + 选中态机 ==================== */

XQ_TEST(XAbstractButton, clickSignalOrder)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* button;

    XQ_REQUIRE(host != NULL);
    button = XAbstractButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    xab_hook(button);

    /* 可选中按钮：pressed → toggled(true) → released → clicked(true)
     * （本机 Qt 6.8 探针 click() 实测顺序同此）。 */
    XAbstractButton_setCheckable(button, true);
    xab_resetOrder();
    XAbstractButton_click(button);
    XQ_EQ_STR(g_xabOrder, "ptrc");
    XQ_CHECK(g_xabLastChecked);
    XQ_CHECK(XAbstractButton_isChecked(button));
    XQ_CHECK(!XAbstractButton_isDown(button));

    /* 再点一次：翻转回未选中，顺序不变。 */
    xab_resetOrder();
    XAbstractButton_click(button);
    XQ_EQ_STR(g_xabOrder, "ptrc");
    XQ_CHECK(!g_xabLastChecked);
    XQ_CHECK(!XAbstractButton_isChecked(button));

    /* toggle() 非虚槽：等价 setChecked(!checked)（qabstractbutton.cpp:843-851）。 */
    xab_resetOrder();
    XAbstractButton_toggle(button);
    XQ_EQ_STR(g_xabOrder, "t");
    XQ_CHECK(XAbstractButton_isChecked(button));

    /* 不可选中：无 toggled（探针「pressed, released, clicked(0)」）。 */
    XAbstractButton_setCheckable(button, false);
    XQ_CHECK(!XAbstractButton_isChecked(button));
    xab_resetOrder();
    XAbstractButton_click(button);
    XQ_EQ_STR(g_xabOrder, "prc");
    XQ_CHECK(!g_xabLastChecked);
    XQ_EQ_I64(g_xabToggledCount, 0);

    /* setCheckable(false) 静默清位：不发 toggled（探针 events 为空）。 */
    XAbstractButton_setCheckable(button, true);
    XAbstractButton_setChecked(button, true);
    xab_resetOrder();
    XAbstractButton_setCheckable(button, false);
    XQ_CHECK(!XAbstractButton_isChecked(button));
    XQ_EQ_STR(g_xabOrder, "");

    /* 禁用按钮 click() 无任何信号。 */
    XWidget_setEnabled((XWidget*)button, true);
    XAbstractButton_setCheckable(button, true);
    XWidget_setEnabled((XWidget*)button, false);
    xab_resetOrder();
    XAbstractButton_click(button);
    XQ_EQ_STR(g_xabOrder, "");

    XClassDelete((XClass*)host);
}

/* ==================== ③ 鼠标/键盘按压状态机 ==================== */

XQ_TEST(XAbstractButton, mouseAndKeyStateMachine)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* button;
    XAbstractButton* first;
    XAbstractButton* second;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 120);

    /* 按下→释放（同点内）：一次 click，down 先真后假。 */
    button = XAbstractButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    xab_hook(button);
    xab_resetOrder();
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 10, 10,
              XMouseButton_LeftButton);
    XQ_CHECK(XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "p");
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 10, 10,
              XMouseButton_LeftButton);
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "prc");

    /* 按下后在按钮外释放：只发 released，不点击。 */
    xab_resetOrder();
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 10, 10,
              XMouseButton_LeftButton);
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 200, 100,
              XMouseButton_LeftButton);
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "pr");

    /* 按住拖出再拖回：仍保持按下态（本实现有意偏离 Qt
     * qabstractbutton.cpp:985-1005 的「移出即 setDown(false)」——
     * 现场点动按钮长按不得因指针抖动被判释放，见 .c 内注释）。 */
    xab_resetOrder();
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 10, 10,
              XMouseButton_LeftButton);
    {
        XMouseEvent me;
        XPoint pos;

        XPoint_init(&pos, 200, 100);
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE,
                         XMouseButton_NoButton, 0, pos);
        me.m_buttons = XMouseButton_LeftButton;
        XObject_event_base((XObject*)button, (XEvent*)&me);
    }
    XQ_CHECK(XAbstractButton_isDown(button));
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, 10, 10,
              XMouseButton_LeftButton);
    XQ_EQ_STR(g_xabOrder, "prc");

    /* Space 按下/释放：press 阶段只发 pressed，release 阶段补 released+clicked。 */
    xab_resetOrder();
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_Space);
    XQ_CHECK(XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "p");
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_RELEASE, XKey_Space);
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "prc");

    /* Escape（Qt 的 QKeySequence::Cancel）只释放、不点击。 */
    xab_resetOrder();
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_Space);
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_Escape);
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "pr");

    /* 禁用按钮接受鼠标事件但不改状态（qabstractbutton.cpp:893-910）。 */
    XWidget_setEnabled((XWidget*)button, false);
    xab_resetOrder();
    xab_mouse((XWidget*)button, XEVENT_TYPE_MOUSE_BUTTON_PRESS, 10, 10,
              XMouseButton_LeftButton);
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "");
    XWidget_setEnabled((XWidget*)button, true);

    /* 独占组内当前选中项不可被点掉（Qt changeState 闸：
     * qabstractbutton.cpp:344-375 + 593-604）。 */
    first = XAbstractButton_create(host, 0);
    second = XAbstractButton_create(host, 0);
    XQ_REQUIRE(first != NULL && second != NULL);
    XWidget_setGeometry((XWidget*)first, 0, 40, 80, 24);
    XWidget_setGeometry((XWidget*)second, 0, 70, 80, 24);
    XAbstractButton_setCheckable(first, true);
    XAbstractButton_setCheckable(second, true);
    XAbstractButton_setAutoExclusive(first, true);
    XAbstractButton_setAutoExclusive(second, true);
    xab_hook(first);
    XAbstractButton_setChecked(first, true);
    xab_resetOrder();
    XAbstractButton_click(first);
    XQ_CHECK(XAbstractButton_isChecked(first));
    XQ_EQ_I64(g_xabToggledCount, 0);
    XQ_EQ_STR(g_xabOrder, "prc");

    /* 选中另一颗会取消前一颗（autoExclusive 联动）。 */
    XAbstractButton_setChecked(second, true);
    XQ_CHECK(!XAbstractButton_isChecked(first));
    XQ_CHECK(XAbstractButton_isChecked(second));

    XClassDelete((XClass*)host);
}

/* ==================== ④ autoExclusive 递归候选集（Qt queryButtonList） ==================== */

XQ_TEST(XAbstractButton, autoExclusiveRecursiveScope)
{
    XWidget* host = XWidget_create(NULL, 0);
    XWidget* nestedHost;
    XWidget* loneHost;
    XAbstractButton* direct;
    XAbstractButton* nested;
    XAbstractButton* plain;
    XAbstractButton* lone;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 260, 220);
    direct = XAbstractButton_create(host, 0);
    plain = XAbstractButton_create(host, 0);
    nestedHost = XWidget_create(host, 0);
    XQ_REQUIRE(direct != NULL && plain != NULL && nestedHost != NULL);
    nested = XAbstractButton_create(nestedHost, 0);
    XQ_REQUIRE(nested != NULL);
    XWidget_setGeometry((XWidget*)direct, 0, 0, 100, 24);
    XWidget_setGeometry((XWidget*)plain, 0, 30, 100, 24);
    XWidget_setGeometry(nestedHost, 0, 60, 120, 40);
    XWidget_setGeometry((XWidget*)nested, 0, 0, 100, 24);

    XAbstractButton_setCheckable(direct, true);
    XAbstractButton_setCheckable(nested, true);
    XAbstractButton_setCheckable(plain, true);
    XAbstractButton_setAutoExclusive(direct, true);
    XAbstractButton_setAutoExclusive(nested, true);
    /* plain 显式关闭 autoExclusive：不进候选集（Qt 过滤谓词）。 */

    /* 直接子按钮的候选表 = 父控件递归 findChildren ⇒ 含深层容器里的
     * nested（本机 Qt 6.8 探针 A：直接子 r2 选中把 c1 里的 r1 取消）。 */
    XAbstractButton_setChecked(nested, true);
    XQ_CHECK(XAbstractButton_isChecked(nested));
    XAbstractButton_setChecked(direct, true);
    XQ_CHECK(XAbstractButton_isChecked(direct));
    XQ_CHECK(!XAbstractButton_isChecked(nested));

    /* 反方向不对称：nested 的直接父是 nestedHost，候选表只有自己
     * ⇒ 选中它不会取消 direct（Qt 探针 B：r1.setChecked(true) 后 r2
     * 仍选中）。 */
    XAbstractButton_setChecked(nested, true);
    XQ_CHECK(XAbstractButton_isChecked(nested));
    XQ_CHECK(XAbstractButton_isChecked(direct));

    /* 非 autoExclusive 的 checkable 兄弟不在候选集 ⇒ 两者可同时选中
     * （Qt 探针 F/G）。 */
    XAbstractButton_setChecked(plain, true);
    XQ_CHECK(XAbstractButton_isChecked(plain));
    XQ_CHECK(XAbstractButton_isChecked(nested));

    XClassDelete((XClass*)host);

    /* 单成员 autoExclusive 组（候选表 size==1）可以反选（Qt 探针 E）。 */
    loneHost = XWidget_create(NULL, 0);
    XQ_REQUIRE(loneHost != NULL);
    lone = XAbstractButton_create(loneHost, 0);
    XQ_REQUIRE(lone != NULL);
    XAbstractButton_setCheckable(lone, true);
    XAbstractButton_setAutoExclusive(lone, true);
    XAbstractButton_setChecked(lone, true);
    XQ_CHECK(XAbstractButton_isChecked(lone));
    XAbstractButton_setChecked(lone, false);
    XQ_CHECK(!XAbstractButton_isChecked(lone));
    XClassDelete((XClass*)loneHost);
}

/* ==================== ⑤ 组内方向键移焦（Qt moveFocus） ==================== */

XQ_TEST(XAbstractButton, arrowKeyMovesFocusInGroup)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* first;
    XAbstractButton* second;
    XAbstractButton* third;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 240, 200);
    first = XAbstractButton_create(host, 0);
    second = XAbstractButton_create(host, 0);
    third = XAbstractButton_create(host, 0);
    XQ_REQUIRE(first != NULL && second != NULL && third != NULL);
    XWidget_setGeometry((XWidget*)first, 0, 0, 120, 24);
    XWidget_setGeometry((XWidget*)second, 0, 30, 120, 24);
    XWidget_setGeometry((XWidget*)third, 0, 60, 120, 24);
    XAbstractButton_setCheckable(first, true);
    XAbstractButton_setCheckable(second, true);
    XAbstractButton_setCheckable(third, true);
    XAbstractButton_setAutoExclusive(first, true);
    XAbstractButton_setAutoExclusive(second, true);
    XAbstractButton_setAutoExclusive(third, true);

    /* 未选中态：方向键只移焦、不点击（Qt moveFocus 的
     * `fb->checked` 条件不成立 ⇒ 不 click）。 */
    XWidget_setFocus_2((XWidget*)first, XFocusReason_Tab);
    XQ_CHECK(XWidget_hasFocus((XWidget*)first));
    xab_key((XWidget*)first, XEVENT_TYPE_KEY_PRESS, XKey_Down);
    XQ_CHECK(XWidget_hasFocus((XWidget*)second));
    XQ_CHECK(!XAbstractButton_isChecked(first));
    XQ_CHECK(!XAbstractButton_isChecked(second));

    /* 选中态：候选在独占组内且焦点按钮已选中 ⇒ 先 click 候选
     * （选中随焦点迁移，qabstractbutton.cpp:275-284）。 */
    XWidget_setFocus_2((XWidget*)first, XFocusReason_Tab);
    XAbstractButton_setChecked(first, true);
    XQ_CHECK(XAbstractButton_isChecked(first));
    xab_key((XWidget*)first, XEVENT_TYPE_KEY_PRESS, XKey_Down);
    XQ_CHECK(XWidget_hasFocus((XWidget*)second));
    XQ_CHECK(XAbstractButton_isChecked(second));
    XQ_CHECK(!XAbstractButton_isChecked(first));

    /* 反方向：Up 移焦到上方（Backtab 原因），选中一并迁移。 */
    xab_key((XWidget*)second, XEVENT_TYPE_KEY_PRESS, XKey_Up);
    XQ_CHECK(XWidget_hasFocus((XWidget*)first));
    XQ_CHECK(XAbstractButton_isChecked(first));
    XQ_CHECK(!XAbstractButton_isChecked(second));

    XClassDelete((XClass*)host);
}

/* ==================== ⑥ Key_Select 与 Space 同权 ==================== */

XQ_TEST(XAbstractButton, selectKeyPressCycle)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* button;

    XQ_REQUIRE(host != NULL);
    button = XAbstractButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    XAbstractButton_setCheckable(button, true);
    xab_hook(button);

    xab_resetOrder();
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XAB_KEY_SELECT);
    XQ_CHECK(XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "p");
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_RELEASE, XAB_KEY_SELECT);
    XQ_CHECK(!XAbstractButton_isDown(button));
    /* 可选中按钮的 keyboard click() 与鼠标/click() 同路：click() 内先
     * nextCheckState() 发 toggled、再 emitReleased/emitClicked
     * （qabstractbutton.cpp:825-841）⇒ 顺序是 "ptrc"，不是 "prc"。
     * 本机 Qt 6.8 探针（checkable QPushButton，Key_Select 与 Space
     * 各跑一遍）实测：press 后 "p"，release 后 "ptrc"、checked=1。 */
    XQ_EQ_STR(g_xabOrder, "ptrc");
    XQ_CHECK(XAbstractButton_isChecked(button));
    XQ_EQ_I64(g_xabToggledCount, 1);
    XQ_CHECK(g_xabLastChecked);

    /* Space 同权（Qt ButtonPressKeys 前两项）：同一路径、同一顺序，
     * 再按一次翻回未选中。 */
    xab_resetOrder();
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_Space);
    XQ_CHECK(XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "p");
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_RELEASE, XKey_Space);
    XQ_CHECK(!XAbstractButton_isDown(button));
    XQ_EQ_STR(g_xabOrder, "ptrc");
    XQ_CHECK(!XAbstractButton_isChecked(button));
    XQ_EQ_I64(g_xabToggledCount, 1);
    XQ_CHECK(!g_xabLastChecked);

    XClassDelete((XClass*)host);
}

/* ==================== ⑦ 助记符 '&' 与 shortcut 文本 ==================== */

XQ_TEST(XAbstractButton, mnemonicTextAndShortcut)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* button;
    XString* display = NULL;
    const char* text;
    const XString* shortcut;

    XQ_REQUIRE(host != NULL);
    button = XAbstractButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 120, 24);

    /* '&' 标记助记符：text() 保留原文，显示/度量去标记；shortcut 变成
     * Alt+大写字符（Qt 探针：&File → Alt+F，wShow == "File" 宽）。 */
    XAbstractButton_setText_2(button, "&File");
    XQ_EQ_STR(xab_text(button), "&File");
    display = NULL;
    text = XAbstractButton_displayTextUtf8(button, &display);
    XQ_EQ_STR(text ? text : "", "File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    shortcut = XAbstractButton_shortcut(button);
    XQ_CHECK(shortcut != NULL);
    XQ_EQ_STR(shortcut ? XString_toUtf8(shortcut) : "", "Alt+F");

    /* '&&' 显示为字面 '&'，不产生助记符 ⇒ shortcut 被清空
     * （Qt 探针：&&File → mnemonic 空、显示 "&File"）。 */
    XAbstractButton_setText_2(button, "&&File");
    display = NULL;
    text = XAbstractButton_displayTextUtf8(button, &display);
    XQ_EQ_STR(text ? text : "", "&File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    XQ_CHECK(XAbstractButton_shortcut(button) == NULL);

    /* 行尾孤立 '&' 既显示不出来也不生成助记符
     * （Qt 探针：File& → 宽 == "File"、mnemonic 空）。 */
    XAbstractButton_setText_2(button, "File&");
    display = NULL;
    text = XAbstractButton_displayTextUtf8(button, &display);
    XQ_EQ_STR(text ? text : "", "File");
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    XQ_CHECK(XAbstractButton_shortcut(button) == NULL);

    /* Qt setText 末尾无条件 setShortcut(mnemonic)：无 '&' 的新文本会
     * 把既有显式快捷键清掉（qabstractbutton.cpp:482-506）。 */
    XAbstractButton_setShortcut_2(button, "Ctrl+S");
    XQ_CHECK(XAbstractButton_shortcut(button) != NULL);
    /* 含修饰位的文本只保存承载、**不登记裸键**：把 "Ctrl+S" 的末段 `S`
     * 按裸键写进 XShortcut 全局注册表会让普通 S 键也点击该按钮
     * （XShortcut 注册表没有修饰位承载）。 */
    XQ_CHECK(XShortcut_match(XKey_S, XShortcutContext_WindowShortcut,
                             (XWidget*)button) == NULL);
    /* 对照：无修饰位的单键文本仍照常登记（不能把 setShortcut 一并废掉）。 */
    XAbstractButton_setShortcut_2(button, "S");
    XQ_CHECK(XShortcut_match(XKey_S, XShortcutContext_WindowShortcut,
                             (XWidget*)button) != NULL);
    XAbstractButton_setShortcut_2(button, "Ctrl+S");
    XQ_CHECK(XShortcut_match(XKey_S, XShortcutContext_WindowShortcut,
                             (XWidget*)button) == NULL);
    XAbstractButton_setText_2(button, "Plain");
    XQ_CHECK(XAbstractButton_shortcut(button) == NULL);

    /* 助记符不登记裸键绑定：按普通字母 key 不得点击按钮
     * （XShortcut 单键注册表无法表达 Alt 修饰位，注册裸键会误触）。 */
    XAbstractButton_setText_2(button, "&Act");
    XAbstractButton_setCheckable(button, true);
    XAbstractButton_setChecked(button, false);
    XClassDelete((XClass*)host);
}

/* ==================== ⑧ 助记符不误触：普通字母键 ==================== */

XQ_TEST(XAbstractButton, mnemonicDoesNotGrabPlainKey)
{
    XWidget* host = XWidget_create(NULL, 0);
    XAbstractButton* button;

    XQ_REQUIRE(host != NULL);
    button = XAbstractButton_create(host, 0);
    XQ_REQUIRE(button != NULL);
    XWidget_setGeometry((XWidget*)button, 0, 0, 80, 24);
    XAbstractButton_setCheckable(button, true);
    XAbstractButton_setChecked(button, false);
    xab_hook(button);
    XAbstractButton_setText_2(button, "&Act");

    /* 助记符只写 shortcut 承载、不向 XShortcut 注册表登记裸键：
     * `&Act` 的裸 A 键在全局表里查不到（注册了就会让普通字母键
     * 误触按钮，而 XShortcut 无法表达 Alt 修饰位）。 */
    XQ_CHECK(XShortcut_match(XKey_A, XShortcutContext_WindowShortcut,
                             (XWidget*)button) == NULL);

    xab_resetOrder();
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_PRESS, XKey_A);
    xab_key((XWidget*)button, XEVENT_TYPE_KEY_RELEASE, XKey_A);
    XQ_EQ_STR(g_xabOrder, "");
    XQ_CHECK(!XAbstractButton_isChecked(button));

    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XABSTRACTBUTTON_ON */
