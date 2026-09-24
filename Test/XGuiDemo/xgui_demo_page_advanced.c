/******************************************************************************
 * @file       xgui_demo_page_advanced.c
 * @brief      XGuiWindowDemo 高级控件页（对标 Qt 示例中富文本/补全/快捷键/
 *             主窗口-停靠族的可视化演示，独立翻译单元）。
 * @details    实现契约头 xgui_demo_pages.h 中的
 *             demo_page_advanced_build / demo_page_advanced_autotest：
 *             - build：在 parent 下手工 setGeometry 装配（内容区约
 *               760x480，勿重叠，子控件逐一 XWidget_show）：
 *                 XTextEdit      富文本样例（粗体/斜体 <i> 合成倾斜
 *                                §8.0g11/font color/span 背景/无序列表，
 *                                setHtml + setRichPreview(true) 只读
 *                                预览直观呈现）+ "键入文本"程序化插入按钮；
 *                 XCompleter     挂 XAbstractItemModel 词条表，接
 *                                XLineEdit（textChanged → 补全前缀 →
 *                                候选数状态行；弹层 UI 为 XGui 控件层
 *                                未接入的声明边界，此处接通模型与结果）；
 *                 XKeySequenceEdit  点击聚焦后按键捕获（Ctrl+O），
 *                                状态行显示已捕获组数；
 *                 XShortcut      全局快捷键（键码 T 承载；XShortcut 头
 *                                文件 @note：单键码承载、组合键序列不在
 *                                范围，Qt 示例的 Ctrl+T 以 T 键对标），
 *                                activated → 状态反馈，无需按钮；
 *                 XSizeGrip      页面右下角（真实拖拽由真人验证）；
 *                 XFocusFrame    环绕"焦点框目标按钮"绘制焦点框；
 *                 XRubberBand    页面根控件 mouse 按下/移动/释放驱动
 *                                矩形选框（松开隐藏，对标 QRubberBand
 *                                瞬态用法）；
 *                 XToolTip       多个控件 setToolTip（悬停显示由真人；
 *                                getter 由 autotest 断言）；
 *                 XSplashScreen  "显示启动画面"按钮 → 顶层非阻塞 show，
 *                                XTimer_singleShot1 1.5s 后 finish 关闭；
 *                 XMainWindow    "打开主窗口"按钮 → 独立顶层主窗口
 *                                （XMenuBar+菜单、中央 XLabel、左右
 *                                XDockWidget）；重复点击 show 已有实例。
 *             - autotest：全程非阻塞（不进模态循环/事件等待），事件经
 *               XObject_event_base 直发（与真实输入同路径），输出
 *               "XGuiAutoTest: [PASS]/[FAIL] 中文描述"，返回失败断言数
 *               （0=全过；page 不符=-1）。autotest 创建的临时顶层窗口
 *               （启动画面/主窗口）结束前全部 delete_base 销毁，防
 *               ASan 泄漏。
 * @note       文件所有权：本文件为页面自包含实现，不改动契约头、主文件、
 *             CMakeLists 与 Src/；页面内部控件指针存文件级 static 结构
 *             （demo 单实例，build 登记、autotest 使用）。s_adv.splash /
 *             s_adv.mainWindow 为演示期常驻顶层对象（parent=NULL，重复
 *             点击复用），随进程退出回收，与主文件 DemoWin 的进程级生命
 *             期约定一致。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "XPrintf.h"
#include "xgui_demo_pages.h"

#if XWIDGET_ON

#include "XClass.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEvent.h"
#include "XString.h"
#include "XVector.h"

/* ==================== 页面子模块可用性聚合宏 ==================== */
/* 与各控件头文件的门控条件保持一致；控件装配段与 autotest 段共用，
 * 保证裁剪配置下整页仍可构建（页面根控件常在，其余按需缺席）。 */
#if XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTEDIT_ON
#define ADV_TEXTEDIT_ON 1
#else
#define ADV_TEXTEDIT_ON 0
#endif

#if XTABLEWIDGET_ON && XLINEEDIT_ON
#define ADV_COMPLETER_ON 1
#else
#define ADV_COMPLETER_ON 0
#endif

#if XKEYSEQUENCEEDIT_ON
#define ADV_KSE_ON 1
#else
#define ADV_KSE_ON 0
#endif

/* XShortcut/XToolTip 头文件仅依赖 XWIDGET_ON（无独立模块开关）。 */
#define ADV_SHORTCUT_ON 1
#define ADV_TOOLTIP_ON 1

#if XSIZEGRIP_ON
#define ADV_SIZEGRIP_ON 1
#else
#define ADV_SIZEGRIP_ON 0
#endif

#if XFOCUSFRAME_ON && XPUSHBUTTON_ON
#define ADV_FOCUSFRAME_ON 1
#else
#define ADV_FOCUSFRAME_ON 0
#endif

#if XRUBBERBAND_ON
#define ADV_BAND_ON 1
#else
#define ADV_BAND_ON 0
#endif

#if XSPLASHSCREEN_ON
#define ADV_SPLASH_ON 1
#else
#define ADV_SPLASH_ON 0
#endif

#if XMAINWINDOW_ON && XDOCKWIDGET_ON && XMENU_ON && XMENUBAR_ON && XLABEL_ON
#define ADV_MAINWIN_ON 1
#else
#define ADV_MAINWIN_ON 0
#endif

#if XPUSHBUTTON_ON
#define ADV_BUTTON_ON 1
#else
#define ADV_BUTTON_ON 0
#endif

#if XLABEL_ON
#define ADV_LABEL_ON 1
#else
#define ADV_LABEL_ON 0
#endif

/* ==================== 模块头文件（按需引入） ==================== */
#if ADV_LABEL_ON
#include "XLabel.h"
#endif
#if ADV_BUTTON_ON
#include "XPushButton.h"
#endif
#if XLINEEDIT_ON
#include "XLineEdit.h"
#endif
#if ADV_TEXTEDIT_ON
#include "XTextEdit.h"
#endif
#if ADV_COMPLETER_ON
#include "XCompleter.h"
#include "XAbstractItemModel.h"
#endif
#if ADV_KSE_ON
#include "XKeySequenceEdit.h"
#endif
#if ADV_SHORTCUT_ON
#include "XShortcut.h"
#endif
#if ADV_SIZEGRIP_ON
#include "XSizeGrip.h"
#endif
#if ADV_FOCUSFRAME_ON
#include "XFocusFrame.h"
#endif
#if ADV_BAND_ON
#include "XRubberBand.h"
#endif
#if ADV_TOOLTIP_ON
#include "XToolTip.h"
#endif
#if ADV_SPLASH_ON
#include "XSplashScreen.h"
#endif
#if ADV_MAINWIN_ON
#include "XMainWindow.h"
#include "XDockWidget.h"
#include "XMenu.h"
#include "XMenuBar.h"
#endif
/* 启动画面 1.5s 自动关闭的单次定时器（对标 QTimer::singleShot）。 */
#if ADV_SPLASH_ON
#include "XTimer.h"
#endif

/* ==================== 富文本样例（build 展示与 autotest 复用） ==== */
/* 渲染子集边界（XGui.md §8.1/§8.0g11）：b/i/u/s、font color/size、
 * span 仅承载 background-color、br、p align、a href、ul/ol/li；斜体
 * 为合成倾斜（固定 shear 0.22）。颜色字用 <font color> 承载（对标
 * Qt 富文本色字），<span> 的 style 仅解析背景色——任务书的"颜色
 * span"按框架能力拆为 font color（前景色）+ span background-color
 * （背景高亮）双样例呈现。 */
static const char* const ADV_RICH_HTML =
    "<p><b>XGui 高级控件页</b>（富文本渲染子集）</p>"
    "<p>斜体合成直观样例：<i>§8.0g11 斜体合成——XFont 合成倾斜</i></p>"
    "<p><font color=\"#C00000\">font color 红色文字</font>，"
    "<span style=\"background-color:#FFE680\">span 背景高亮</span></p>"
    "<ul><li>富文本无序列表项一</li><li>列表项二（方块标记）</li></ul>";

/* XCompleter 词条表（前缀 "Ope" 命中前两项，供模型/结果断言）。 */
static const char* const ADV_COMPLETER_WORDS[] = {
    "Open File", "Open Directory", "Close Editor",
    "Copy Path", "Recent Files", "Save All"
};
#define ADV_COMPLETER_WORD_COUNT \
    (int)(sizeof(ADV_COMPLETER_WORDS) / sizeof(ADV_COMPLETER_WORDS[0]))

/* ==================== 页面根控件子类（橡皮筋事件驱动） ============ */
/* 对标主文件 DemoWin 的子类化定式：XMemory 堆分配 + XWidget_init +
 * XClassSetVtable + Set_Class_Memory/IsHeap；页面根控件作为堆对象交由
 * parent 父子链级联析构（契约头所有权约定）。 */
typedef struct AdvPage
{
    XWidget m_base; /**< 基类成员；必须是第一个。 */
} AdvPage;

XCLASS_DEFINE_BEGING(AdvPage)
XCLASS_DEFINE_EXTEND_END(AdvPage, XWidget)

/* ==================== 页面内部控件指针（demo 单实例自持） ========= */
typedef struct AdvState
{
    AdvPage* page;                     /**< 页面根控件（build 登记）。 */
    DemoPageStatusFn status;           /**< 主窗口状态栏反馈回调。 */
    void* user;                        /**< 回调用户数据。 */
#if ADV_TEXTEDIT_ON
    XTextEdit* textEdit;               /**< 富文本编辑器。 */
#endif
#if ADV_COMPLETER_ON
    XLineEdit* completerEdit;          /**< 补全输入框。 */
    XCompleter* completer;             /**< 补全对象（父子链级联析构）。 */
    XAbstractItemModel* completerModel;/**< 补全词条模型（同上）。 */
#endif
#if ADV_KSE_ON
    XKeySequenceEdit* kse;             /**< 快捷键捕获控件。 */
#endif
#if ADV_SHORTCUT_ON
    XShortcut* shortcut;               /**< 全局快捷键对象。 */
#endif
#if ADV_SIZEGRIP_ON
    XSizeGrip* sizeGrip;               /**< 尺寸手柄。 */
#endif
#if ADV_FOCUSFRAME_ON
    XFocusFrame* focusFrame;           /**< 焦点框。 */
    XPushButton* focusTarget;          /**< 焦点框环绕的目标按钮。 */
#endif
#if ADV_BAND_ON
    XRubberBand* band;                 /**< 橡皮筋选框。 */
    bool bandActive;                   /**< 拖拽进行中。 */
    XPoint bandOrigin;                 /**< 拖拽起点（页面局部坐标）。 */
#endif
#if ADV_SPLASH_ON
    XSplashScreen* splash;             /**< 启动画面（演示期常驻复用）。 */
#endif
#if ADV_MAINWIN_ON
    XMainWindow* mainWindow;           /**< 独立主窗口（演示期常驻复用）。 */
#endif
#if ADV_LABEL_ON
    XLabel* completerStatus;           /**< 补全状态行。 */
    XLabel* kseStatus;                 /**< 捕获状态行。 */
    XLabel* shortcutStatus;            /**< 快捷键触发状态行。 */
#endif
} AdvState;

static AdvState s_adv;

/* ==================== 内部工具 ==================== */

/** @brief 向主窗口状态栏反馈交互结果（回调缺失时静默）。 */
static void adv_status(const char* text)
{
    if (s_adv.status)
        s_adv.status(s_adv.user, text);
}

#if ADV_LABEL_ON
/** @brief 更新页面本地状态行文本。 */
static void adv_setLabel(XLabel* label, const char* text)
{
    if (label)
        XLabel_setText_2(label, text);
}
#endif

#if ADV_TOOLTIP_ON
/** @brief 给控件设置工具提示（setToolTip 深拷贝，临时串即刻释放）。 */
static void adv_setToolTip(XWidget* widget, const char* text)
{
    XString* tip;
    if (!widget || !text) return;
    tip = XString_create_utf8(text);
    if (!tip) return;
    XWidget_setToolTip(widget, tip);
    XString_delete_base((XClass*)tip);
}
#endif

/* ==================== 槽：XTextEdit 程序化插入 ==================== */
#if ADV_TEXTEDIT_ON
/** @brief "键入文本"按钮：程序化插入一行（编辑类接口自动退出预览态，
 *         对标 QTextEdit::insertPlainText）。 */
static void adv_btnInsertSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (!s_adv.textEdit) return;
    XTextEdit_insertPlainText(s_adv.textEdit,
                              "XGui 程序化插入一行文本\n");
    adv_status("XTextEdit: 已程序化插入文本（预览态自动退出）");
}
#endif /* ADV_TEXTEDIT_ON */

/* ==================== 槽：XCompleter 接 XLineEdit ================= */
#if ADV_COMPLETER_ON
/** @brief 输入框 textChanged → 设置补全前缀并刷新候选状态行。
 * @note  XGui 的补全弹层 UI 由控件层另行接入（XCompleter.h @note：
 *        当前仅存储与结果计算），此处接通模型与前缀→候选结果链路。 */
static void adv_completerTextChangedSlot(XObject* receiver, XVarList* args)
{
    const char* text;
    int count;
    char buf[160];
    (void)receiver;
    (void)args;
    if (!s_adv.completer || !s_adv.completerEdit) return;
    text = XLineEdit_text(s_adv.completerEdit);
    XCompleter_setCompletionPrefix_2(s_adv.completer, text);
    count = XCompleter_completionCount(s_adv.completer);
    if (count > 0) {
        XString* current = XCompleter_currentCompletion(s_adv.completer);
        const char* currentUtf8 = current ? XString_toUtf8(current) : "";
        snprintf(buf, sizeof(buf),
                 "补全: 前缀'%s' → %d 个候选，当前: %s",
                 text, count, currentUtf8);
        if (current)
            XString_delete_base((XClass*)current);
    } else {
        snprintf(buf, sizeof(buf), "补全: 前缀'%s' → 无候选", text);
    }
#if ADV_LABEL_ON
    adv_setLabel(s_adv.completerStatus, buf);
#endif
}
#endif /* ADV_COMPLETER_ON */

/* ==================== 槽：XKeySequenceEdit 捕获反馈 =============== */
#if ADV_KSE_ON
/** @brief 快捷键序列变化 → 状态行显示已捕获组数。 */
static void adv_kseChangedSlot(XObject* receiver, XVarList* args)
{
    const XKeySequence* seq;
    int count;
    char buf[96];
    (void)receiver;
    (void)args;
    if (!s_adv.kse) return;
    seq = XKeySequenceEdit_keySequence(s_adv.kse);
    count = seq ? seq->count : 0;
    snprintf(buf, sizeof(buf),
             "已捕获 %d 组（Return 确认 / Esc 清空 / Backspace 回删）",
             count);
#if ADV_LABEL_ON
    adv_setLabel(s_adv.kseStatus, buf);
#endif
}
#endif /* ADV_KSE_ON */

/* ==================== 槽：XShortcut 触发反馈 ====================== */
#if ADV_SHORTCUT_ON
/** @brief 全局快捷键触发 → 状态反馈（对标 QShortcut::activated）。 */
static void adv_shortcutSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
#if ADV_LABEL_ON
    adv_setLabel(s_adv.shortcutStatus, "XShortcut 已触发（键 T）");
#endif
    adv_status("XShortcut: 全局快捷键触发（键 T）");
}
#endif /* ADV_SHORTCUT_ON */

/* ==================== 槽：XSplashScreen 显示/自动关闭 ============= */
#if ADV_SPLASH_ON
/** @brief 1.5s 单次定时器到期：finish 关闭启动画面（对标 QSplashScreen
 *         的非阻塞展示-收尾节奏）。 */
static void adv_splashTimeoutSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    XSplashScreen_finish((XSplashScreen*)receiver, NULL);
    adv_status("XSplashScreen: 启动画面已关闭");
}

/** @brief "显示启动画面"按钮：非阻塞 show + 1.5s 后自动 finish。 */
static void adv_btnSplashSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (!s_adv.splash) {
        /* 顶层窗口（parent=NULL）；演示期常驻复用，随进程退出回收。 */
        s_adv.splash = XSplashScreen_create(NULL, 0);
        if (!s_adv.splash) return;
        XSplashScreen_showMessage(s_adv.splash,
                                  "XGui 高级控件页启动画面…",
                                  XAlignment_Left | XAlignment_Bottom,
                                  0xFFE6E6E6u); /* 黑底浅灰字，保证对比度可读 */
    }
    XWidget_show((XWidget*)s_adv.splash);
    XTimer_singleShot1(1500, (XObject*)s_adv.splash, adv_splashTimeoutSlot,
                       XConnectionType_Direct);
    adv_status("XSplashScreen: 启动画面显示中（1.5s 后自动关闭）");
}
#endif /* ADV_SPLASH_ON */

/* ==================== 槽：XMainWindow 独立主窗口 ================== */
#if ADV_MAINWIN_ON
/** @brief 菜单动作反馈（对标 QMenu 触发路径）。 */
static void adv_mwOpenActionSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    adv_status("XMainWindow: 菜单动作 打开(O) 触发");
}

/** @brief 菜单动作"退出(X)"：隐藏演示主窗口（对象保留供复用）。 */
static void adv_mwExitActionSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XWidget_hide((XWidget*)receiver);
    adv_status("XMainWindow: 菜单动作 退出(X) 触发（窗口隐藏）");
}

/** @brief 惰性创建演示主窗口：菜单栏 + 菜单、中央标签、左右停靠面板。
 * @note  菜单对象以主窗口为父对象（对标 XMainWindow_createPopupMenu 的
 *        既有语义：随主窗口析构级联销毁）；停靠面板/内容标签均为子控件
 *        级联销毁。 */
static void adv_ensureMainWindow(void)
{
    XWidget* menuBar;
    XMenu* menuFile;
    XMenu* menuHelp;
    XAction* actionOpen;
    XAction* actionExit;
    XLabel* central;
    XDockWidget* dockLeft;
    XDockWidget* dockRight;
    XLabel* leftContent;
    XLabel* rightContent;

    if (s_adv.mainWindow) return;
    s_adv.mainWindow = XMainWindow_create(NULL, 0);
    if (!s_adv.mainWindow) return;
    XWidget_resize((XWidget*)s_adv.mainWindow, 560, 420);
    {
        XString* title = XString_create_utf8("高级控件演示 - XMainWindow");
        if (title) {
            XWidget_setWindowTitle((XWidget*)s_adv.mainWindow, title);
            XString_delete_base((XClass*)title);
        }
    }
    /* 菜单栏（惰性创建）+ 两个菜单。 */
    menuBar = XMainWindow_menuBar(s_adv.mainWindow);
    menuFile = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               (XWidget*)s_adv.mainWindow, "文件(&F)");
    menuHelp = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               (XWidget*)s_adv.mainWindow, "帮助(&H)");
    if (menuBar && menuFile && menuHelp) {
        XMenuBar_addMenu((XMenuBar*)menuBar, menuFile);
        XMenuBar_addMenu((XMenuBar*)menuBar, menuHelp);
        actionOpen = XMenu_addAction_2(menuFile, "打开(O)");
        actionExit = XMenu_addAction_2(menuFile, "退出(X)");
        XMenu_addAction_2(menuHelp, "关于(XGui 高级控件页)");
        if (actionOpen)
            XObject_connect_1((XObject*)actionOpen,
                              (size_t)XAction_triggered_signal(NULL, false),
                              (XObject*)s_adv.mainWindow,
                              adv_mwOpenActionSlot, XConnectionType_Direct);
        if (actionExit)
            XObject_connect_1((XObject*)actionExit,
                              (size_t)XAction_triggered_signal(NULL, false),
                              (XObject*)s_adv.mainWindow,
                              adv_mwExitActionSlot, XConnectionType_Direct);
    }
    /* 中央控件：XLabel。 */
    central = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               (XWidget*)s_adv.mainWindow, 0);
    if (central) {
        XLabel_setText_2(central, "中央显示区（XLabel）");
        XLabel_setAlignment(central, XAlignment_Center);
        XMainWindow_setCentralWidget(s_adv.mainWindow, (XWidget*)central);
    }
    /* 左右停靠面板（子控件级联销毁；可停靠区域默认全开）。 */
    dockLeft = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-左",
                                     (XWidget*)s_adv.mainWindow, 0);
    dockRight = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右",
                                      (XWidget*)s_adv.mainWindow, 0);
    leftContent = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   (XWidget*)dockLeft, 0);
    rightContent = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                    (XWidget*)dockRight, 0);
    if (leftContent)
        XLabel_setText_2(leftContent, "左面板内容");
    if (rightContent)
        XLabel_setText_2(rightContent, "右面板内容（可拖出浮动）");
    if (dockLeft) {
        XDockWidget_setWidget(dockLeft, (XWidget*)leftContent);
        XMainWindow_addDockWidget(s_adv.mainWindow, XDockWidgetArea_Left,
                                  (XWidget*)dockLeft);
    }
    if (dockRight) {
        XDockWidget_setWidget(dockRight, (XWidget*)rightContent);
        XMainWindow_addDockWidget(s_adv.mainWindow, XDockWidgetArea_Right,
                                  (XWidget*)dockRight);
    }
}

/** @brief "打开主窗口"按钮：复用已建实例（show + activateWindow）。 */
static void adv_btnMainWindowSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    adv_ensureMainWindow();
    if (!s_adv.mainWindow) return;
    XWidget_show((XWidget*)s_adv.mainWindow);
    XWidget_activateWindow((XWidget*)s_adv.mainWindow);
    adv_status("XMainWindow: 独立主窗口已显示（重复点击复用实例）");
}
#endif /* ADV_MAINWIN_ON */

/* ==================== 页面根控件虚表（橡皮筋事件驱动） ============ */
#if ADV_BAND_ON
/** @brief 按下：在页面空白处起始一个矩形选框（对标 QRubberBand 用法）。 */
static void VAdvPage_mousePressEvent(XWidget* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    (void)self;
    if (s_adv.band && me && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        XPoint pos = XMouseEvent_position(me);
        s_adv.bandActive = true;
        s_adv.bandOrigin = pos;
        XWidget_setGeometry((XWidget*)s_adv.band, pos.x, pos.y, 1, 1);
        XWidget_show((XWidget*)s_adv.band);
        XEvent_accept(event);
    }
}

/** @brief 移动：拖拽中按起点-当前点归一矩形并同步选框几何。 */
static void VAdvPage_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    (void)self;
    if (s_adv.band && s_adv.bandActive && me && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_MOVE) {
        XPoint pos = XMouseEvent_position(me);
        int x = pos.x < s_adv.bandOrigin.x ? pos.x : s_adv.bandOrigin.x;
        int y = pos.y < s_adv.bandOrigin.y ? pos.y : s_adv.bandOrigin.y;
        int w = pos.x - s_adv.bandOrigin.x;
        int h = pos.y - s_adv.bandOrigin.y;
        if (w < 0) w = -w;
        if (h < 0) h = -h;
        XWidget_setGeometry((XWidget*)s_adv.band, x, y,
                            w > 0 ? w : 1, h > 0 ? h : 1);
        XEvent_accept(event);
    }
}

/** @brief 释放：结束拖拽并隐藏选框（瞬态语义，对标 Qt 示例用法）。 */
static void VAdvPage_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XMouseEvent* me = (XMouseEvent*)event;
    (void)self;
    if (s_adv.band && me && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_RELEASE &&
        s_adv.bandActive) {
        s_adv.bandActive = false;
        XWidget_hide((XWidget*)s_adv.band);
        XEvent_accept(event);
    }
}
#endif /* ADV_BAND_ON */

/** @brief 页面根控件类虚表：仅重载鼠标三槽驱动橡皮筋。 */
static XVtable* AdvPage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(AdvPage)
    XVTABLE_INHERIT_XCLASS(XWidget);
#if ADV_BAND_ON
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VAdvPage_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VAdvPage_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VAdvPage_mouseReleaseEvent);
#endif
    return XVTABLE_DEFAULT;
}

/** @brief 创建页面根控件对象并套用子类虚表（堆对象，父链级联析构）。 */
static AdvPage* AdvPage_create(XWidget* parent)
{
    AdvPage* self = (AdvPage*)XMemory_malloc(sizeof(AdvPage),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(AdvPage));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, AdvPage);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面装配（契约接口） ======================== */

/** @brief 构建高级控件页（布局手工 setGeometry，内容区约 760x480）。 */
XWidget* demo_page_advanced_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user)
{
    AdvPage* page;
    if (!parent) return NULL;
    page = AdvPage_create(parent);
    if (!page) return NULL;
    memset(&s_adv, 0, sizeof(s_adv));
    s_adv.page = page;
    s_adv.status = status;
    s_adv.user = user;

#if ADV_TEXTEDIT_ON
    /* ---- XTextEdit：富文本样例（setHtml + 只读预览直观呈现）。 ---- */
    s_adv.textEdit = XTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         (XWidget*)page, 0);
    if (s_adv.textEdit) {
        XWidget_setGeometry((XWidget*)s_adv.textEdit, 12, 8, 300, 180);
        XTextEdit_setHtml(s_adv.textEdit, ADV_RICH_HTML);
        XTextEdit_setRichPreview(s_adv.textEdit, true);
#if ADV_TOOLTIP_ON
        adv_setToolTip((XWidget*)s_adv.textEdit,
                       "XTextEdit 富文本预览：粗体/斜体合成/"
                       "font color/span 背景/无序列表");
#endif
        XWidget_show((XWidget*)s_adv.textEdit);
    }
#endif

#if ADV_BUTTON_ON
    {
        XPushButton* insertButton = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)page, 0);
        if (insertButton) {
            XPushButton_setText_2(insertButton, "键入文本");
            XWidget_setGeometry((XWidget*)insertButton, 324, 8, 110, 26);
#if ADV_TEXTEDIT_ON
            XObject_connect_1((XObject*)insertButton,
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              (XObject*)page, adv_btnInsertSlot,
                              XConnectionType_Direct);
#endif
#if ADV_TOOLTIP_ON
            adv_setToolTip((XWidget*)insertButton,
                           "向 XTextEdit 程序化插入一行文本");
#endif
            XWidget_show((XWidget*)insertButton);
        }
    }
#endif

#if ADV_LABEL_ON
    {
        XLabel* hint = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XWidget*)page, 0);
        if (hint) {
            XLabel_setText_2(hint,
                             "页面空白处按住左键拖拽 → 橡皮筋选框（松开隐藏）");
            XWidget_setGeometry((XWidget*)hint, 12, 196, 430, 18);
            XWidget_show((XWidget*)hint);
        }
    }
#endif

#if ADV_COMPLETER_ON
    /* ---- XCompleter：词条模型 + XLineEdit 输入联动。 ---- */
    s_adv.completerModel = XAbstractItemModel_create();
    if (s_adv.completerModel) {
        int row;
        /* 模型挂到页面父对象：随页面父子链级联析构（防泄漏）。 */
        XObject_setParent((XObject*)s_adv.completerModel, (XObject*)page);
        XAbstractItemModel_setDimension(s_adv.completerModel,
                                        ADV_COMPLETER_WORD_COUNT, 1);
        for (row = 0; row < ADV_COMPLETER_WORD_COUNT; ++row)
            XAbstractItemModel_setData_2(s_adv.completerModel, row, 0,
                                         ADV_COMPLETER_WORDS[row]);
    }
    s_adv.completer = XCompleter_create((XObject*)page);
    if (s_adv.completer) {
        XCompleter_setModel(s_adv.completer, s_adv.completerModel);
        XCompleter_setCaseSensitivity(s_adv.completer,
                                      XChar_CaseSensitive);
        XCompleter_setCompletionMode(
            s_adv.completer, XCompleterCompletionMode_PopupCompletion);
    }
    s_adv.completerEdit = XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              (XWidget*)page, 0);
    if (s_adv.completerEdit) {
        XWidget_setGeometry((XWidget*)s_adv.completerEdit, 12, 244, 220, 26);
        XLineEdit_setPlaceholderText(s_adv.completerEdit,
                                     "输入前缀（试 Ope）");
        /* 接通补全器（对标 QLineEdit::setCompleter）：此前补全器为
           孤儿对象（无 setCompleter 安装），键入不触发任何补全链路
           ——安装后 XLineControl 键入即驱动前缀匹配+默认弹层。 */
        XLineEdit_setCompleter(s_adv.completerEdit, s_adv.completer);
        XObject_connect_1((XObject*)s_adv.completerEdit,
                          (size_t)XLineEdit_textChanged_signal(NULL),
                          (XObject*)page, adv_completerTextChangedSlot,
                          XConnectionType_Direct);
#if ADV_TOOLTIP_ON
        adv_setToolTip((XWidget*)s_adv.completerEdit,
                       "XLineEdit + XCompleter：输入前缀出候选");
#endif
        XWidget_show((XWidget*)s_adv.completerEdit);
    }
    {
        XLabel* caption = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)page, 0);
        if (caption) {
            XLabel_setText_2(caption, "补全输入:");
            XWidget_setGeometry((XWidget*)caption, 12, 224, 120, 18);
            XWidget_show((XWidget*)caption);
        }
    }
#endif /* ADV_COMPLETER_ON */

#if ADV_LABEL_ON && ADV_COMPLETER_ON
    s_adv.completerStatus = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             (XWidget*)page, 0);
    if (s_adv.completerStatus) {
        XLabel_setText_2(s_adv.completerStatus, "补全: 输入前缀查看候选");
        XWidget_setGeometry((XWidget*)s_adv.completerStatus, 12, 276, 430, 18);
        XWidget_show((XWidget*)s_adv.completerStatus);
    }
#endif

#if ADV_KSE_ON
    /* ---- XKeySequenceEdit：点击聚焦后按键捕获。 ---- */
    {
        XLabel* caption = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)page, 0);
        if (caption) {
            XLabel_setText_2(caption, "快捷键捕获（点击后按键，试 Ctrl+O）:");
            XWidget_setGeometry((XWidget*)caption, 12, 304, 320, 18);
            XWidget_show((XWidget*)caption);
        }
    }
    s_adv.kse = XKeySequenceEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)page, 0);
    if (s_adv.kse) {
        XWidget_setGeometry((XWidget*)s_adv.kse, 12, 324, 240, 28);
        XObject_connect_1(
            (XObject*)s_adv.kse,
            (size_t)XKeySequenceEdit_keySequenceChanged_signal(NULL, NULL),
            (XObject*)page, adv_kseChangedSlot, XConnectionType_Direct);
#if ADV_TOOLTIP_ON
        adv_setToolTip((XWidget*)s_adv.kse,
                       "XKeySequenceEdit：按键捕获序列，Return 确认");
#endif
        XWidget_show((XWidget*)s_adv.kse);
    }
#endif /* ADV_KSE_ON */

#if ADV_LABEL_ON && ADV_KSE_ON
    s_adv.kseStatus = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)page, 0);
    if (s_adv.kseStatus) {
        XLabel_setText_2(s_adv.kseStatus, "尚未捕获快捷键");
        XWidget_setGeometry((XWidget*)s_adv.kseStatus, 12, 358, 430, 18);
        XWidget_show((XWidget*)s_adv.kseStatus);
    }
#endif

#if ADV_SHORTCUT_ON
    /* ---- XShortcut：全局快捷键（键 T 承载；组合键序列见文件头边界）。 ---- */
    s_adv.shortcut = XShortcut_create_2(XKey_T, (XObject*)page);
    if (s_adv.shortcut) {
        XShortcut_setContext(s_adv.shortcut,
                             XShortcutContext_ApplicationShortcut);
        XObject_connect_1((XObject*)s_adv.shortcut,
                          (size_t)XShortcut_activated_signal(NULL),
                          (XObject*)s_adv.shortcut, adv_shortcutSlot,
                          XConnectionType_Direct);
    }
#endif

#if ADV_LABEL_ON && ADV_SHORTCUT_ON
    {
        XLabel* caption = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)page, 0);
        if (caption) {
            /* 右缘收口：说明文案按实测字形宽（约 21px/汉字）控制在
             * 起点 x=324 → 764px 之内（XLabel 不裁剪溢出文本）；备注
             * "单键码承载"挪至状态行初始文案。 */
            XLabel_setText_2(caption,
                             "全局快捷键: 按 T 触发 (对标 Ctrl+T)");
            XWidget_setGeometry((XWidget*)caption, 324, 44, 320, 18);
            XWidget_show((XWidget*)caption);
        }
    }
    s_adv.shortcutStatus = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XWidget*)page, 0);
    if (s_adv.shortcutStatus) {
        XLabel_setText_2(s_adv.shortcutStatus,
                         "XShortcut 未触发 (键 T, 单键码承载)");
        XWidget_setGeometry((XWidget*)s_adv.shortcutStatus, 324, 66, 320, 18);
        XWidget_show((XWidget*)s_adv.shortcutStatus);
    }
#endif

#if ADV_FOCUSFRAME_ON
    /* ---- XFocusFrame：环绕目标按钮绘制焦点框（外扩 2px）。 ---- */
    s_adv.focusTarget = XPushButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              (XWidget*)page, 0);
    if (s_adv.focusTarget) {
        XPushButton_setText_2(s_adv.focusTarget, "焦点框目标按钮");
        XWidget_setGeometry((XWidget*)s_adv.focusTarget, 324, 96, 150, 30);
#if ADV_TOOLTIP_ON
        adv_setToolTip((XWidget*)s_adv.focusTarget,
                       "XFocusFrame 环绕的目标控件");
#endif
        XWidget_show((XWidget*)s_adv.focusTarget);
    }
    s_adv.focusFrame = XFocusFrame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             (XWidget*)page, 0);
    if (s_adv.focusFrame) {
        XFocusFrame_setWidget(s_adv.focusFrame, (XWidget*)s_adv.focusTarget);
        XWidget_setGeometry((XWidget*)s_adv.focusFrame, 322, 94, 154, 34);
        XWidget_show((XWidget*)s_adv.focusFrame);
    }
#endif /* ADV_FOCUSFRAME_ON */

#if ADV_BUTTON_ON && ADV_SPLASH_ON
    {
        XPushButton* splashButton = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)page, 0);
        if (splashButton) {
            XPushButton_setText_2(splashButton, "显示启动画面");
            XWidget_setGeometry((XWidget*)splashButton, 500, 96, 130, 28);
            XObject_connect_1((XObject*)splashButton,
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              (XObject*)page, adv_btnSplashSlot,
                              XConnectionType_Direct);
#if ADV_TOOLTIP_ON
            adv_setToolTip((XWidget*)splashButton,
                           "XSplashScreen：非阻塞显示，1.5s 后自动关闭");
#endif
            XWidget_show((XWidget*)splashButton);
        }
    }
#endif

#if ADV_BUTTON_ON && ADV_MAINWIN_ON
    {
        XPushButton* mainWinButton = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)page, 0);
        if (mainWinButton) {
            XPushButton_setText_2(mainWinButton, "打开主窗口");
            XWidget_setGeometry((XWidget*)mainWinButton, 324, 140, 150, 28);
            XObject_connect_1((XObject*)mainWinButton,
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              (XObject*)page, adv_btnMainWindowSlot,
                              XConnectionType_Direct);
#if ADV_TOOLTIP_ON
            adv_setToolTip((XWidget*)mainWinButton,
                           "XMainWindow：菜单栏+中央标签+左右停靠面板");
#endif
            XWidget_show((XWidget*)mainWinButton);
        }
    }
#endif

#if ADV_LABEL_ON && ADV_MAINWIN_ON
    {
        XLabel* hint = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XWidget*)page, 0);
        if (hint) {
            /* 右缘收口：避免 XMainWindow 长词（wordWrap 仅空格断行）
             * 溢出，改用无空格短句（约 250px，止于 486+250=736）。 */
            XLabel_setText_2(hint, "菜单/中央标签/左右停靠面板");
            XLabel_setWordWrap(hint, true);
            XWidget_setGeometry((XWidget*)hint, 486, 136, 250, 54);
            XWidget_show((XWidget*)hint);
        }
    }
#endif

#if ADV_SIZEGRIP_ON
    /* ---- XSizeGrip：页面右下角（真实拖拽由真人验证）。 ---- */
    s_adv.sizeGrip = XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         (XWidget*)page);
    if (s_adv.sizeGrip) {
        XWidget_setGeometry((XWidget*)s_adv.sizeGrip, 736, 456, 16, 16);
#if ADV_TOOLTIP_ON
        adv_setToolTip((XWidget*)s_adv.sizeGrip,
                       "XSizeGrip：拖动调整顶层窗口尺寸");
#endif
        XWidget_show((XWidget*)s_adv.sizeGrip);
    }
#endif

#if ADV_BAND_ON
    /* ---- XRubberBand：最后创建（子级绘制顺序在最上，拖拽覆盖内容）。 ---- */
    s_adv.band = XRubberBand_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       XRubberBandShape_Rectangle,
                                       (XWidget*)page);
    if (s_adv.band)
        XWidget_hide((XWidget*)s_adv.band); /* 初始隐藏，按下拖拽时显示。 */
#endif

    return (XWidget*)page;
}

/* ==================== 页面自测（契约接口） ======================== */

/** @brief 高级控件页自动化验证（全程非阻塞；事件经
 *         XObject_event_base 直发，与真实输入同路径）。
 * @param  page 页面根控件；与 build 登记不符返回 -1（防错页调用）。
 * @return 失败断言数（0=全过）。 */
int demo_page_advanced_autotest(XWidget* page)
{
    int failures = 0;

/* 断言输出定式（与主文件 demo_input_autotest 的 DEMO_EXPECT 一致）。 */
#define ADV_EXPECT(cond, what) \
    do { \
        if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
        else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } \
    } while (0)

    if (!s_adv.page || page != (XWidget*)s_adv.page)
        return -1;

    /* ---- 0. XTextEdit：键盘注入键入 + setHtml 剥离/预览态断言。
     * 键入路径说明：XTextEdit 的编辑缓冲由内嵌 XPlainTextEdit 承载
     * （头文件结构体 m_editor），键事件直发内嵌编辑器与真实聚焦输入
     * 同走 XPlainTextEdit 键处理虚槽。 ---- */
#if ADV_TEXTEDIT_ON
    if (s_adv.textEdit && s_adv.textEdit->m_editor) {
        XPlainTextEdit* editor = s_adv.textEdit->m_editor;
        XString* plain;
        int ki;
        XWidget_setFocus((XWidget*)editor);
        XTextEdit_setPlainText(s_adv.textEdit, ""); /* 清空并退出预览态 */
        for (ki = 0; ki < 3; ++ki) {
            XKeyEvent ke;
            XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, 'a' + ki, 0);
            XObject_event_base((XObject*)editor, (XEvent*)&ke);
        }
        plain = XTextEdit_toPlainText(s_adv.textEdit);
        ADV_EXPECT(plain && XString_equals_utf8(plain, "abc",
                                                XChar_CaseSensitive),
                   "XTextEdit 实机键入 abc 后 toPlainText 一致");
        if (plain)
            XString_delete_base((XClass*)plain);
        /* setHtml：富文本文档 + 编辑缓冲同步剥离标签（所见即所存）。 */
        XTextEdit_setHtml(s_adv.textEdit, ADV_RICH_HTML);
        XTextEdit_setRichPreview(s_adv.textEdit, true); /* 只读预览呈现 */
        ADV_EXPECT(XTextEdit_isRichPreview(s_adv.textEdit),
                   "XTextEdit setHtml+setRichPreview 进入预览态");
        plain = XTextEdit_toPlainText(s_adv.textEdit);
        ADV_EXPECT(plain && XString_contains_utf8(plain, "XGui",
                                                  XChar_CaseSensitive) &&
                   XString_contains_utf8(plain,
                                         "\xE6\x96\x9C\xE4\xBD\x93",
                                         XChar_CaseSensitive),
                   "XTextEdit setHtml 内容剥离保留文本（含斜体样例文案）");
        ADV_EXPECT(plain && !XString_contains_utf8(plain, "<b>",
                                                   XChar_CaseSensitive),
                   "XTextEdit setHtml 后纯文本不含标签标记");
        if (plain)
            XString_delete_base((XClass*)plain);
    } else {
        ADV_EXPECT(false, "XTextEdit 实例缺失");
    }
#endif /* ADV_TEXTEDIT_ON */

    /* ---- 1. XCompleter：经 XLineEdit 直调前缀匹配路径并断言结果。 ---- */
#if ADV_COMPLETER_ON
    if (s_adv.completer && s_adv.completerEdit) {
        XString* current;
        /* 输入联动路径：setText 发 textChanged → 槽内 setCompletionPrefix。 */
        XLineEdit_setText(s_adv.completerEdit, "Ope");
        ADV_EXPECT(XCompleter_completionCount(s_adv.completer) == 2,
                   "XCompleter 输入联动：前缀 Ope 命中 2 个候选");
        current = XCompleter_currentCompletion(s_adv.completer);
        ADV_EXPECT(current && XString_equals_utf8(current, "Open File",
                                                  XChar_CaseSensitive),
                   "XCompleter 当前补全为 Open File");
        if (current)
            XString_delete_base((XClass*)current);
        /* 直接调用路径：setCurrentRow 环绕语义 + 行/索引/取词 getter。 */
        ADV_EXPECT(XCompleter_setCurrentRow(s_adv.completer, 1),
                   "XCompleter setCurrentRow(1) 成功");
        ADV_EXPECT(XCompleter_currentRow(s_adv.completer) == 1 &&
                   XCompleter_currentIndex(s_adv.completer) == 1,
                   "XCompleter 当前行/源模型索引均为 1");
        current = XCompleter_currentCompletion(s_adv.completer);
        ADV_EXPECT(current && XString_equals_utf8(current, "Open Directory",
                                                  XChar_CaseSensitive),
                   "XCompleter 行切换后当前补全为 Open Directory");
        if (current)
            XString_delete_base((XClass*)current);
        current = XCompleter_pathFromIndex(s_adv.completer, 0);
        ADV_EXPECT(current && XString_equals_utf8(current, "Open File",
                                                  XChar_CaseSensitive),
                   "XCompleter pathFromIndex(0) 取词 Open File");
        if (current)
            XString_delete_base((XClass*)current);
    } else {
        ADV_EXPECT(false, "XCompleter 实例缺失");
    }
#endif /* ADV_COMPLETER_ON */

    /* ---- 2. XKeySequenceEdit：修饰位注入 Ctrl+O → 序列 getter 断言。
     * 修饰位经 XKeyEvent_init 的 modifiers 参数直发（可行，无需
     * setter→getter 降级）；随后 Return 确认、clear 清空、setter
     * 回设供人工目验。 ---- */
#if ADV_KSE_ON
    if (s_adv.kse) {
        const XKeySequence* seq;
        XKeySequence manual;
        XKeyEvent ctrlO;
        XKeyEvent confirm;
        XWidget_setFocus((XWidget*)s_adv.kse);
        XKeySequenceEdit_clear(s_adv.kse);
        XKeyEvent_init(&ctrlO, XEVENT_TYPE_KEY_PRESS, XKey_O,
                       XKeyboardModifier_ControlModifier);
        XObject_event_base((XObject*)s_adv.kse, (XEvent*)&ctrlO);
        seq = XKeySequenceEdit_keySequence(s_adv.kse);
        ADV_EXPECT(seq && seq->count == 1 &&
                   seq->combos[0].modifiers ==
                       XKeyboardModifier_ControlModifier &&
                   seq->combos[0].key == XKey_O,
                   "XKeySequenceEdit 注入 Ctrl+O 捕获一组序列");
        XKeyEvent_init(&confirm, XEVENT_TYPE_KEY_PRESS, XKey_Return, 0);
        XObject_event_base((XObject*)s_adv.kse, (XEvent*)&confirm);
        seq = XKeySequenceEdit_keySequence(s_adv.kse);
        ADV_EXPECT(seq && seq->count == 1,
                   "XKeySequenceEdit Return 确认后序列保留");
        XKeySequenceEdit_clear(s_adv.kse);
        seq = XKeySequenceEdit_keySequence(s_adv.kse);
        ADV_EXPECT(seq && seq->count == 0,
                   "XKeySequenceEdit clear 清空序列");
        memset(&manual, 0, sizeof(manual));
        manual.count = 1;
        manual.combos[0].modifiers = XKeyboardModifier_ControlModifier;
        manual.combos[0].key = XKey_O;
        XKeySequenceEdit_setKeySequence(s_adv.kse, &manual);
        seq = XKeySequenceEdit_keySequence(s_adv.kse);
        ADV_EXPECT(seq && seq->count == 1 &&
                   seq->combos[0].key == XKey_O,
                   "XKeySequenceEdit setter→getter 回设 Ctrl+O");
    } else {
        ADV_EXPECT(false, "XKeySequenceEdit 实例缺失");
    }
#endif /* ADV_KSE_ON */

    /* ---- 3. XShortcut：对象构造 + 序列 getter 断言；match/activate
     * 为框架公开的匹配/触发入口（真实焦点体系触达仍属边界：触发
     * 路径依赖平台按键接入，声明为 XShortcut.h @note 范围）。 ---- */
#if ADV_SHORTCUT_ON
    if (s_adv.shortcut) {
        ADV_EXPECT(XShortcut_key(s_adv.shortcut) == XKey_T,
                   "XShortcut 键码 getter 为 T");
        ADV_EXPECT(XShortcut_isEnabled(s_adv.shortcut),
                   "XShortcut 默认启用");
        ADV_EXPECT(XShortcut_context(s_adv.shortcut) ==
                   XShortcutContext_ApplicationShortcut,
                   "XShortcut 上下文为应用级（全局快捷键）");
        ADV_EXPECT(XShortcut_match(XKey_T,
                                   XShortcutContext_ApplicationShortcut,
                                   (XWidget*)s_adv.page) == s_adv.shortcut,
                   "XShortcut_match 注册表命中本快捷键");
        XShortcut_activate(s_adv.shortcut);
#if ADV_LABEL_ON
        ADV_EXPECT(s_adv.shortcutStatus &&
                   XString_equals_utf8(XLabel_text(s_adv.shortcutStatus),
                                       "XShortcut 已触发（键 T）",
                                       XChar_CaseSensitive),
                   "XShortcut activate 驱动状态行反馈");
#endif
    } else {
        ADV_EXPECT(false, "XShortcut 实例缺失");
    }
#endif /* ADV_SHORTCUT_ON */

    /* ---- 4. XToolTip：控件级 getter 断言 + 静态 API 显隐往返。 ---- */
#if ADV_TOOLTIP_ON
    {
#if ADV_TEXTEDIT_ON
        const XString* tip = s_adv.textEdit
            ? XWidget_toolTip((XWidget*)s_adv.textEdit) : NULL;
        ADV_EXPECT(tip && XString_equals_utf8(tip,
                    "XTextEdit 富文本预览：粗体/斜体合成/"
                    "font color/span 背景/无序列表", XChar_CaseSensitive),
                   "XTextEdit setToolTip getter 一致");
#endif
        XToolTip_showText_2(24, 24, "XGui 自动测试提示",
                            (XWidget*)s_adv.page, NULL, -1);
        ADV_EXPECT(XToolTip_isVisible(), "XToolTip showText 后可见");
        {
            XString* tipText = XToolTip_text();
            ADV_EXPECT(tipText && XString_equals_utf8(
                       tipText, "XGui 自动测试提示", XChar_CaseSensitive),
                       "XToolTip text getter 一致");
            if (tipText)
                XString_delete_base((XClass*)tipText);
        }
        XToolTip_hideText();
        ADV_EXPECT(!XToolTip_isVisible(), "XToolTip hideText 后隐藏");
    }
#endif /* ADV_TOOLTIP_ON */

    /* ---- 5. XSizeGrip/XFocusFrame/XRubberBand：构造与交互态断言。
     * XSizeGrip 真实拖拽改窗由真人验证，此处仅断言构造；橡皮筋以
     * mouse 事件注入驱动按下-拖拽-释放全链路。 ---- */
#if ADV_SIZEGRIP_ON
    ADV_EXPECT(s_adv.sizeGrip != NULL &&
               XWidget_width((XWidget*)s_adv.sizeGrip) == 16 &&
               XWidget_height((XWidget*)s_adv.sizeGrip) == 16,
               "XSizeGrip 构造并就位（右下角 16x16）");
#endif
#if ADV_FOCUSFRAME_ON
    ADV_EXPECT(s_adv.focusFrame && s_adv.focusTarget &&
               XFocusFrame_widget(s_adv.focusFrame) ==
                   (XWidget*)s_adv.focusTarget,
               "XFocusFrame 关联目标按钮 getter 一致");
#endif
#if ADV_BAND_ON
    ADV_EXPECT(s_adv.band &&
               XRubberBand_shape(s_adv.band) ==
                   XRubberBandShape_Rectangle,
               "XRubberBand 形状为矩形");
    {
        XPoint pressPos;
        XPoint movePos;
        XMouseEvent press;
        XMouseEvent move;
        XMouseEvent release;
        XPoint_init(&pressPos, 40, 210);
        XPoint_init(&movePos, 200, 300);
        XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, pressPos);
        XMouseEvent_init(&move, XEVENT_TYPE_MOUSE_MOVE,
                         XMouseButton_LeftButton, 0, movePos);
        XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, 0, movePos);
        XObject_event_base((XObject*)s_adv.page, (XEvent*)&press);
        XObject_event_base((XObject*)s_adv.page, (XEvent*)&move);
        ADV_EXPECT(XWidget_isVisible((XWidget*)s_adv.band) &&
                   XWidget_width((XWidget*)s_adv.band) == 160 &&
                   XWidget_height((XWidget*)s_adv.band) == 90,
                   "XRubberBand 按下拖拽出 160x90 选框");
        XObject_event_base((XObject*)s_adv.page, (XEvent*)&release);
        ADV_EXPECT(!XWidget_isVisible((XWidget*)s_adv.band),
                   "XRubberBand 释放后隐藏（瞬态语义）");
    }
#endif /* ADV_BAND_ON */

    /* ---- 6. XSplashScreen：临时顶层画面构造+非阻塞 show+finish；
     * 结束前销毁（防 ASan 泄漏）。 ---- */
#if ADV_SPLASH_ON
    {
        XSplashScreen* splash = XSplashScreen_create(NULL, 0);
        ADV_EXPECT(splash != NULL, "XSplashScreen 临时实例创建");
        if (splash) {
            XSplashScreen_showMessage(splash, "自动测试启动画面",
                                      XAlignment_Left | XAlignment_Bottom,
                                      0xFF202020u);
            XWidget_show((XWidget*)splash);
            ADV_EXPECT(XWidget_isVisible((XWidget*)splash),
                       "XSplashScreen 非阻塞 show 后可见");
            XSplashScreen_finish(splash, NULL);
            ADV_EXPECT(!XWidget_isVisible((XWidget*)splash),
                       "XSplashScreen finish 后不可见");
            XSplashScreen_delete_base(splash); /* 临时顶层销毁 */
        }
    }
#endif /* ADV_SPLASH_ON */

    /* ---- 7. XMainWindow+XDockWidget：临时顶层主窗口构造 → 菜单栏/
     * 停靠登记 getter 断言 → delete_base 级联销毁（菜单以主窗口为
     * 父对象、停靠面板/内容标签为子控件，均随级联析构）。 ---- */
#if ADV_MAINWIN_ON
    {
        XMainWindow* win = XMainWindow_create(NULL, 0);
        ADV_EXPECT(win != NULL, "XMainWindow 临时实例创建");
        if (win) {
            XWidget* menuBar = XMainWindow_menuBar(win);
            XMenu* menuFile = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              (XWidget*)win, "文件(&F)");
            XDockWidget* dockLeft = XDockWidget_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, "停靠-左", (XWidget*)win, 0);
            XDockWidget* dockRight = XDockWidget_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右", (XWidget*)win, 0);
            XLabel* leftContent = dockLeft
                ? XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   (XWidget*)dockLeft, 0) : NULL;
            XLabel* rightContent = dockRight
                ? XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   (XWidget*)dockRight, 0) : NULL;
            ADV_EXPECT(menuBar != NULL, "XMainWindow 惰性菜单栏创建");
            if (menuBar && menuFile) {
                XMenuBar_addMenu((XMenuBar*)menuBar, menuFile);
                XMenu_addAction_2(menuFile, "打开(O)");
            }
            if (dockLeft) {
                XDockWidget_setWidget(dockLeft, (XWidget*)leftContent);
                XMainWindow_addDockWidget(win, XDockWidgetArea_Left,
                                          (XWidget*)dockLeft);
            }
            if (dockRight) {
                XDockWidget_setWidget(dockRight, (XWidget*)rightContent);
                XMainWindow_addDockWidget(win, XDockWidgetArea_Right,
                                          (XWidget*)dockRight);
            }
            ADV_EXPECT(XMainWindow_dockWidgetArea(win,
                      (XWidget*)dockLeft) == (int)XDockWidgetArea_Left,
                       "XMainWindow dockWidgetArea 左停靠登记");
            ADV_EXPECT(XMainWindow_dockWidgetArea(win,
                      (XWidget*)dockRight) == (int)XDockWidgetArea_Right,
                       "XMainWindow dockWidgetArea 右停靠登记");
            ADV_EXPECT(win->m_docks && XVector_size_base(
                       (const XContainer*)win->m_docks) == 2,
                       "XMainWindow 停靠面板数量为 2");
            ADV_EXPECT(dockLeft && !XDockWidget_isFloating(dockLeft) &&
                       dockRight && !XDockWidget_isFloating(dockRight),
                       "XDockWidget 登记后为停靠态（非浮动）");
            ADV_EXPECT(dockLeft && XDockWidget_widget(dockLeft) ==
                       (XWidget*)leftContent,
                       "XDockWidget 内容控件 getter 一致");
            XMainWindow_delete_base(win); /* 级联销毁菜单/停靠/标签 */
        }
    }
#endif /* ADV_MAINWIN_ON */

#undef ADV_EXPECT
    XPrintf("XGuiAutoTest: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* !XWIDGET_ON */

/* 整文件裁剪哨兵：XWidget 模块关闭时本翻译单元为空实现。 */
typedef int xgui_demo_page_advanced_disabled_t;

#endif /* XWIDGET_ON */
