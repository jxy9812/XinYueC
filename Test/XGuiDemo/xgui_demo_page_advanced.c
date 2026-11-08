/******************************************************************************
 * @file       xgui_demo_page_advanced.c
 * @brief      XGuiWindowDemo 高级控件页（对标 Qt 示例中富文本/补全/快捷键/
 *             主窗口-停靠族的可视化演示，独立翻译单元）。
 * @details    实现契约头 xgui_demo_pages.h 中的
 *             demo_page_advanced_build / demo_page_advanced_autotest：
 *             - build：页根挂垂直盒布局器容器化装配（2026-10-17，
 *               原页内 setGeometry 手写排布退场；固定高复刻原行高，
 *               行距用单条目行盒底边距近似），子控件逐一 XWidget_show：
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
 *             - adapt：宽度分档唯一入口（2026-10-25）——根宽 <480 竖屏
 *               单列档：双列行盒翻垂直、右列边距收段间距、可点击钮抬
 *               40 触控高、超宽说明行换短文案（档位留档防重入）；宽档
 *               逐项还原装配口径，桌面视觉不变。
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
#include <stdlib.h>
/* [Windows 链接修复 2026-11-03] 拖动扫掠探针用 POSIX 名 popen/pclose
 * 做抓帧比对（板侧 Linux 工具路径）；MSVC CRT 只导出 _popen/_pclose
 * （POSIX 名 deprecated 未导出）→ LNK2019。本文件内经宏映射对齐
 * （glibc/MinGW 的 popen 即 _popen，行为一致）。 */
#if defined(_WIN32) && !defined(UNDER_CE)
#define popen  _popen
#define pclose _pclose
#endif
#include "XThread.h"   /* XThread_sleep：自检相位间的抓帧静默期。 */
#include "XCursor.h"
#include "XWindowSystemInterface.h" /* 触摸注入（真机路径等价：触摸→合成鼠标）。 */
#include "XWindowEvent.h"        /* XTouchPoint 触点载荷。 */
#include "XWindow.h"             /* 浮动态顶层窗口句柄（触摸投递目标切换）。 */
#include "XCoreApplication.h"
#include "XSystem.h"   /* XSystem_environment：设备端拖动自检相位文件路径。 */
#include "XObject_Protected.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "XPrintf.h"
#include "xgui_demo_pages.h"
#include "XBoxLayout.h" /* 页内布局容器化：根垂直盒+双列/行嵌套盒近似原手写排布。 */
#include "XWidget.h"    /* 底部弹簧控件（XWidget_create_ex，推 SizeGrip 贴右缘）。 */

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
#include "XGuiApplication.h"   /* 主屏查询（启动画面居屏） */
#include "XDateTime.h"         /* 扫掠探针每步耗时（框架自有单调时钟）。 */
#include "XScreen.h"           /* 屏幕几何 */
#endif
#if ADV_MAINWIN_ON
#include "XMainWindow.h"
#include "XMainWindow_Protected.h" /* probeDropGeom 的 hoverDrop（拖动落点预览内部接口）。 */
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
    XDockWidget* dockRight;            /**< 独立主窗口右侧停靠面板（浮动态自检用）。 */
    XDockWidget* dockLeft;             /**< 独立主窗口左侧停靠面板（真机自检用）。 */
#endif
#if ADV_LABEL_ON
    XLabel* completerStatus;           /**< 补全状态行。 */
    XLabel* kseStatus;                 /**< 捕获状态行。 */
    XLabel* shortcutStatus;            /**< 快捷键触发状态行。 */
#endif
    /* 2026-10-03 自适应重排登记（原为 build 局部变量；2026-10-17 页内
     * 布局容器化后几何全交页根 XBoxLayout 分配；2026-10-25 竖屏分档在
     * adapt 按根宽跨档套用，见 adv_applyLayoutTier）。 */
    XPushButton* insertButton;         /**< 「键入文本」按钮（左列首行右）。 */
    XLabel* topHint;                   /**< 橡皮筋提示行（左列）。 */
    XLabel* completerCaption;          /**< 「补全输入」标题。 */
    XLabel* kseCaption;                /**< 「快捷键捕获」标题。 */
    XLabel* shortcutCaption;           /**< 全局快捷键标题（右列）。 */
    XPushButton* splashButton;         /**< 「显示启动画面」按钮（右列）。 */
    XPushButton* mainWinButton;        /**< 「打开主窗口」按钮（右列）。 */
    XLabel* rightHint;                 /**< 主窗口说明（右列）。 */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 2026-10-17 页内布局容器化：布局器对象随页常驻（addLayout 借用
     * 不取所有权，栈/临时对象会悬挂；demo 单实例 build 一次装配，
     * 随进程退出回收，与 s_adv 其余静态件口径一致）。 */
    XBoxLayout rootLayout;   /**< 页根垂直布局器（双列区伸展+底部行贴底）。 */
    XBoxLayout contentRow;   /**< 双列水平行盒（左列+右列，45/55 伸缩）。 */
    XBoxLayout leftCol;      /**< 左列垂直布局器（富文本/补全/快捷键捕获）。 */
    XBoxLayout topRow;       /**< 左列首行（编辑器伸展+键入钮顶对齐列）。 */
    XBoxLayout btnCol;       /**< 「键入文本」按钮单条目列盒（左缘 12px）。 */
    XBoxLayout rightCol;     /**< 右列垂直布局器（左缘 24/顶缘 36 边距）。 */
    XBoxLayout bottomRow;    /**< 底部行盒（弹簧推 SizeGrip 贴右下角）。 */
    XBoxLayout colRows[14];  /**< 单条目行盒池（左 7+右 6 简单行，底边距=行距）。 */
    int rowCount;            /**< 行盒池游标（adv_rowAdd 递增）。 */
    bool singleCol;          /**< 当前宽度分档留档：build 装配即宽档口径，
                                  false 与装配一致 → 首拍同档 adapt 短路
                                  零开销（拖拽期每像素一轮 adapt）。 */
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
    XClassDelete((XClass*)tip);
}
#endif

/* ==================== 页内布局容器化装配工具 ====================== */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
/** @brief 显式首选尺寸（与 setFixed 对齐——部分控件缓存 sizeHint 缺席
 *         或偏离固定尺寸，行盒协商高度会失真；显式 hint 后 qGeomCalc
 *         首选=固定，行高像素级确定）。 */
static void adv_setHint(XWidget* w, int width, int height)
{
    XSize hint;
    if (!w) return;
    XSize_init(&hint, width, height);
    XWidget_setSizeHint(w, &hint);
}

/** @brief 列尾弹簧：水平可扩展/垂直最小。双职责——①交叉轴（宽度）
 *         max 经可扩展子项放开（否则列 max=最窄固定行宽，双列伸缩
 *         分配退化为「窄列钉在最小值」）；②stretch=1 吸收列内全部
 *         竖向多余空间（qGeomCalc 无伸展条目时按条目均摊，固定尺
 *         寸件在摊薄格内垂直居中，行距观感失真）。 */
static void adv_springAdd(XBoxLayout* col)
{
    XWidget* spring;
    if (!col) return;
    spring = XWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               (XWidget*)s_adv.page, 0);
    if (!spring) return;
    XWidget_setSizePolicy(spring, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Minimum);
    XWidget_show(spring); /* 隐藏件会被布局按空条目跳过。 */
    XBoxLayout_addWidget(col, spring);
    XBoxLayout_setStretchFactor(col, spring, 1);
}

/** @brief 单条目行盒装配：控件装入行盒（底边距=行距——布局器无逐条
 *         目间距 API，用 setContentsMargins 近似原手写行距），行盒经
 *         addLayout 借挂列布局器。行盒对象存 s_adv.colRows（addLayout
 *         借用不取所有权，须随页常驻；池满静默丢弃，池容量按装配
 *         上限预留）。 */
static void adv_rowAdd(XBoxLayout* col, XWidget* w, int rowGap)
{
    XBoxLayout* row;
    if (!col || !w) return;
    if (s_adv.rowCount >= (int)(sizeof(s_adv.colRows) /
                                sizeof(s_adv.colRows[0])))
        return;
    row = &s_adv.colRows[s_adv.rowCount++];
    XBoxLayout_init(row, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)row, 0, 0, 0, rowGap);
    XBoxLayout_addWidget(row, w);
    XBoxLayout_addLayout(col, (XLayout*)row);
}

/* ==================== 竖屏分档（宽度分档自适应） ================== */
/* 断点/触控高与远程客户端页（RC_STACK_MAX_W=480/RC_TOUCH_H=40）同口径：
 * 页根有效宽 <480 视为手机竖屏单列档；可点击控件统一抬到 40 逻辑像素
 * （手机触控目标下限）。 */
#define ADV_STACK_MAX_W 480
#define ADV_TOUCH_H 40

#if ADV_LABEL_ON
/** @brief 文本行显式首选宽=当前文案自然宽（高保持单行 18）。
 *  @details 文本行的布局条目最小宽/首选宽均为 0——XLabel 不向控件字段
 *           回写自身度量（XWidget_minimumSizeHint 直读存储字段，标签类
 *           未发布），行宽全由列宽裁决，文案比行宽长就画出控件右缘、
 *           被绘制裁剪吃掉（XLabel 绘制按自身矩形设裁剪，宽屏实测丢
 *           「藏）」）。给足自然宽后行盒协商保住整行；换档文案后须重
 *           调（adv_applyLayoutTier）。 */
static void adv_setNatHint(XLabel* label)
{
    XSize nat;
    if (!label) return;
    nat = XLabel_sizeHint(label);
    adv_setHint((XWidget*)label, nat.width, 18);
}
#endif

/** @brief 竖屏分档套用（仅跨档时由 adapt 调用；逐项幂等可重入）。
 *  @details [2026-10-25 竖屏分档] 真机 400 逻辑宽截图体检三账：右列
 *           「全局快捷键」段沿用宽屏双列坐标，在 400 宽下被窗口右缘
 *           硬裁；说明行文本比行宽长画出控件右缘（见 adv_setNatHint）；
 *           按钮行高 24~30 低于手机触控下限。单列档收敛：
 *           - 双列行盒翻垂直（setDirection 条目顺序不变，右列段落整
 *             体落到左列之下独占整行宽，不再与左列争 400 宽）；
 *           - 右列左缘 24/顶缘 36 宽档口径（列间距+列首下沉）收成段
 *             间距 8，整行宽尽数让给文本；
 *           - 可点击按钮统一抬 ADV_TOUCH_H；富文本预览固定高 232 收
 *             180——预览态内部可滚，纵轴预算让给新落的右列段，底部
 *             SizeGrip 行不被顶出窗口；
 *           - 会超宽的说明行换竖屏短文案并重给自然首选宽；
 *             shortcutStatus 仅在仍是另一档初始文案时换档——已被
 *             adv_shortcutSlot 改写为触发反馈时不回写，反馈不随分档
 *             丢失。
 *           宽档逐项还原 build 装配口径（含原文案），桌面 1000x700
 *           视觉不变。 */
static void adv_applyLayoutTier(bool single)
{
    int h;
    XBoxLayout_setDirection(&s_adv.contentRow,
                            single ? XBoxLayoutDirection_TopToBottom
                                   : XBoxLayoutDirection_LeftToRight);
    XLayout_setContentsMargins((XLayout*)&s_adv.rightCol,
                               single ? 0 : 24, single ? 8 : 36, 0, 0);
#if ADV_TEXTEDIT_ON
    if (s_adv.textEdit) {
        h = single ? 180 : 232;
        XWidget_setFixedHeight((XWidget*)s_adv.textEdit, h);
        adv_setHint((XWidget*)s_adv.textEdit, 196, h);
    }
#endif
#if ADV_LABEL_ON
    if (s_adv.topHint) {
        XLabel_setText_2(s_adv.topHint,
                         single ? "按住拖拽 → 橡皮筋选框"
                                : "按住左键拖拽 → 橡皮筋选框（松开隐藏）");
        adv_setNatHint(s_adv.topHint);
    }
    if (s_adv.kseCaption)
        XLabel_setText_2(s_adv.kseCaption,
                         single ? "快捷键捕获（点击后按键）:"
                                : "快捷键捕获（点击后按键，试 Ctrl+O）:");
    if (s_adv.shortcutCaption) {
        XLabel_setText_2(s_adv.shortcutCaption,
                         single ? "全局快捷键: 按 T 触发"
                                : "全局快捷键: 按 T 触发 (对标 Ctrl+T)");
        adv_setNatHint(s_adv.shortcutCaption);
    }
    if (s_adv.shortcutStatus) {
        const char* wideInit = "XShortcut 未触发 (键 T, 单键码承载)";
        const char* narrowInit = "XShortcut 未触发（键 T）";
        if (XString_equals_utf8(XLabel_text(s_adv.shortcutStatus),
                                single ? narrowInit : wideInit,
                                XChar_CaseSensitive))
            XLabel_setText_2(s_adv.shortcutStatus,
                             single ? wideInit : narrowInit);
        adv_setNatHint(s_adv.shortcutStatus);
    }
#endif
    h = single ? ADV_TOUCH_H : 26;
    if (s_adv.insertButton) {
        XWidget_setFixedHeight((XWidget*)s_adv.insertButton, h);
        adv_setHint((XWidget*)s_adv.insertButton, 100, h);
    }
#if ADV_FOCUSFRAME_ON
    h = single ? ADV_TOUCH_H : 30;
    if (s_adv.focusTarget) {
        XWidget_setFixedHeight((XWidget*)s_adv.focusTarget, h);
        adv_setHint((XWidget*)s_adv.focusTarget, 150, h);
    }
#endif
    h = single ? ADV_TOUCH_H : 28;
    if (s_adv.splashButton) {
        XWidget_setFixedHeight((XWidget*)s_adv.splashButton, h);
        adv_setHint((XWidget*)s_adv.splashButton, 130, h);
    }
    if (s_adv.mainWinButton) {
        XWidget_setFixedHeight((XWidget*)s_adv.mainWinButton, h);
        adv_setHint((XWidget*)s_adv.mainWinButton, 150, h);
    }
}
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

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
            XClassDelete((XClass*)current);
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
        /* 顶层窗口（parent=NULL）；演示期常驻复用，随进程退出回收。
         * 窗口类型对标 QSplashScreen 默认 Qt::SplashScreen：框架
         * 「瞬态类型永不装饰」策略按类型位生效（真·无边框）；传 0
         * 会被 XWidget_init 补成普通 Window，桌面 WM 下错误地带
         * 原生标题栏。 */
        s_adv.splash = XSplashScreen_create(NULL, XWindowType_SplashScreen);
        if (!s_adv.splash) return;
        /* 对标 QSplashScreen 惯用法：先 setPixmap 背景图（按图定尺寸，
         * 实现内为深拷贝，栈上临时随即释放），再 showMessage 叠字。
         * 加载失败时保持无图黑底路径（浅灰字），行为不劣于旧版。 */
        {
            XPixmap logo;
            bool hasLogo;
            XPixmap_init(&logo);
            hasLogo = XPixmap_load_2(&logo, "assets/运行.png", "PNG", 0);
            if (hasLogo)
                XSplashScreen_setPixmap(s_adv.splash, &logo);
            XClassDeinit(&logo);
            XSplashScreen_showMessage(s_adv.splash,
                                      "XGui 高级控件页启动画面…",
                                      XAlignment_Left | XAlignment_Bottom,
                                      hasLogo ? 0xFF1A1A1Au
                                              : 0xFFE6E6E6u);
        }
    }
    /* 对标 Qt 惯用法（QSplashScreen 不自动居屏，由应用 move 到屏幕
     * 中央，同 XFileDialog 无父窗口分支的居屏公式）：每次显示前
     * 统一居屏，避免落 (0,0) 角落。 */
    {
        XScreen* screen = XGuiApplication_primaryScreen();
        if (screen) {
            XRect g = XScreen_geometry(screen);
            XWidget* sp = (XWidget*)s_adv.splash;
            int sw = XWidget_width(sp);
            int sh = XWidget_height(sp);
            XWidget_move(sp,
                         g.x + (g.width > sw ? (g.width - sw) / 2 : 0),
                         g.y + (g.height > sh ? (g.height - sh) / 2 : 0));
        }
    }
    XWidget_show((XWidget*)s_adv.splash);
    /* 对标 Qt "show 后立即上屏"：演示主循环限帧（HUD 2~10fps）且高
     * 负载下 PAINT 延迟 0.7~4s，异步首绘可能晚于 1.5s finish 而
     * 全程不可见（"点击无反应"）；repaint 同步绘制+提交兜底。 */
    XSplashScreen_repaint(s_adv.splash);
    XTimer_singleShot1(1500, (XObject*)s_adv.splash, adv_splashTimeoutSlot,
                       XConnectionType_Direct);
    adv_status("XSplashScreen: 启动画面显示中（1.5s 后自动关闭）");
}
#endif /* ADV_SPLASH_ON */

/* ==================== 槽：XMainWindow 独立主窗口 ================== */
#if ADV_MAINWIN_ON
/* 独立主窗口子类：仅重挂钩关闭事件槽实现「关闭即销毁」（对标 Qt
 * QMainWindow 的 closeEvent(QCloseEvent*) protected 虚槽——应用侧覆写
 * 它做关闭后的资源释放；本框架的等价位= EXWidget_CloseEvent 虚槽，
 * 由 XWidget_close 派发）。关闭即销毁的必要性：框架驱动侧在关闭被
 * 接受后销毁原生窗（X11 WM_DELETE 路径：hide + XWindow_destroy，
 * XPlatformNativeWindow_posix.c），旧实例再 show 复现不出来（实测
 * 「关闭后再点打开主窗口」毫无反应——原生窗已毁、控件却还活着）。
 * 销毁走延迟释放（XObject_deleteLater，对标 Qt deleteLater）：避开
 * 关闭事件派发栈上的悬垂访问。 */
typedef struct AdvMainWindow
{
    XMainWindow m_base;   /**< 基类成员；必须是第一个。 */
} AdvMainWindow;

XCLASS_DEFINE_BEGING(AdvMainWindow)
XCLASS_DEFINE_EXTEND_END(AdvMainWindow, XMainWindow)

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

/** @brief 关闭事件槽：接受关闭并销毁本窗口对象（关闭即销毁，下次重建）。
 * @details 关闭被接受后先摘全局登记（避免下次「打开主窗口」复现已死
 *          实例），再延迟释放对象（XObject_deleteLater 对标 Qt
 *          deleteLater：避开关闭事件派发栈）。对象析构级联释放菜单/
 *          停靠面板/中央标签（均为子控件）。
 * @param  self 目标控件。
 * @param  event 关闭事件。
 * @return 无返回值。
 */
static void VAdvMainWindow_closeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XMainWindow, EXWidget_CloseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    if (s_adv.mainWindow == (XMainWindow*)self)
        s_adv.mainWindow = NULL;
    XObject_deleteLater((XObject*)self);
    XEvent_accept(event);
}

/** @brief 独立主窗口子类虚表：仅重载关闭事件槽。 */
static XVtable* AdvMainWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(AdvMainWindow)
    XVTABLE_INHERIT_XCLASS(XMainWindow);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent,
                             VAdvMainWindow_closeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建独立主窗口对象并套用子类虚表（堆对象，父链级联析构）。 */
static XMainWindow* AdvMainWindow_create(XWidget* parent)
{
    AdvMainWindow* self = (AdvMainWindow*)XMemory_malloc(
        sizeof(AdvMainWindow), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(AdvMainWindow));
    XMainWindow_init(&self->m_base, parent, (XWidgetFlags)XWindowType_Window);
    XClassSetVtable(self, AdvMainWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return &self->m_base;
}

/** @brief 惰性创建演示主窗口：菜单栏 + 菜单、中央标签、左右停靠面板。
 * @note  主窗口以演示主窗为父（XWindowType_Window 窗口态子窗口）：
 *        首显按父窗口客户区上下左右居中（XWidget 首显定位的父窗口
 *        分支，对标 QDialogPrivate::adjustPosition），随父窗口级联
 *        析构；页面根沿父链上溯即演示主窗（顶层窗口）。
 *        菜单对象以主窗口为父对象（对标 XMainWindow_createPopupMenu
 *        的既有语义：随主窗口析构级联销毁）；停靠面板/内容标签均为
 *        子控件级联销毁。 */
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

    if (s_adv.mainWindow) {
        /* 活实例复用（对标 Qt：同一实例可反复 show/hide）。已关闭的
         * 陈旧实例（原生窗已在关闭路径被销毁）必须销毁后重建——留着
         * 只会「点开毫无反应」。 */
        if (XWidget_isVisible((XWidget*)s_adv.mainWindow)) return;
        XClassDelete((XClass*)s_adv.mainWindow);
        s_adv.mainWindow = NULL;
    }
    s_adv.mainWindow = AdvMainWindow_create(
        (XWidget*)XWidget_topLevelWidget((XWidget*)s_adv.page));
    if (!s_adv.mainWindow) return;
    XWidget_resize((XWidget*)s_adv.mainWindow, 560, 420);
    {
        XString* title = XString_create_utf8("高级控件演示 - XMainWindow");
        if (title) {
            XWidget_setWindowTitle((XWidget*)s_adv.mainWindow, title);
            XClassDelete((XClass*)title);
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
        s_adv.dockRight = dockRight;
        s_adv.dockLeft = dockLeft;
    }
}

/** @brief "打开主窗口"按钮：复用已建实例（show + activateWindow）。
 * @note  每次点击（首显与复用）都在 show 之前按父窗口（演示主窗）
 *        几何将弹出主窗口居中一次：x = 父x + (父w - 560) / 2、
 *        y = 父y + (父h - 420) / 2（下限 0），覆盖 show 后落 WM
 *        默认位置的行为。 */
static void adv_btnMainWindowSlot(XObject* receiver, XVarList* args)
{
    XWidget* parent;
    int x;
    int y;

    (void)receiver;
    (void)args;
    adv_ensureMainWindow();
    if (!s_adv.mainWindow) return;
    parent = (XWidget*)XWidget_topLevelWidget((XWidget*)s_adv.page);
    if (parent) {
        /* 按父窗口几何计算居中位置（与 560x420 的创建尺寸对应），负值钳到 0。 */
        x = XWidget_x(parent) + (XWidget_width(parent) - 560) / 2;
        y = XWidget_y(parent) + (XWidget_height(parent) - 420) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        XWidget_move((XWidget*)s_adv.mainWindow, x, y);
    }
    XWidget_show((XWidget*)s_adv.mainWindow);
    XWidget_activateWindow((XWidget*)s_adv.mainWindow);
    adv_status("XMainWindow: 独立主窗口已显示（关闭后销毁，下次重建）");
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

/** @brief 构建高级控件页（页根挂垂直盒布局器容器化装配）。 */
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
        /* 原手写 (12,8,300,180)→adapt (12,8,左列宽-124,232)：原点交
         * 根布局边距、宽交行盒伸展、高固定 232 复刻 adapt 落位值
         * （样例 7 行完整呈现所需，目验二轮挂项口径）。 */
        XWidget_setFixedHeight((XWidget*)s_adv.textEdit, 232);
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
        s_adv.insertButton = insertButton;
        if (insertButton) {
            XPushButton_setText_2(insertButton, "键入文本");
            /* 原手写 (324,8,110,26)→adapt (100x26)：固定尺寸复刻 adapt
             * 落位值；原点/顶对齐交 btnCol 行盒（编辑器右缘 +12px）。 */
            XWidget_setFixedWidth((XWidget*)insertButton, 100);
            XWidget_setFixedHeight((XWidget*)insertButton, 26);
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
        s_adv.topHint = hint;
        if (hint) {
            /* 原手写 (12,196,430,18)，高固定 18 复刻原行高；宽交行盒。
             * 文案[2026-10-25 收短]：「页面空白处」前缀退场——自然宽比
             * 宽档左列伸缩分配宽长出两字，尾「藏）」被控件右缘裁掉
             * （XLabel 绘制按自身矩形设裁剪，旧注「不裁剪溢出文本」与
             * 实测不符）；收短后自然宽进行盒首选即可整行呈现（自然首
             * 选宽见装配段 adv_setNatHint）。 */
            XLabel_setText_2(hint,
                             "按住左键拖拽 → 橡皮筋选框（松开隐藏）");
            XWidget_setFixedHeight((XWidget*)hint, 18);
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
        /* 原手写 (12,244,220,26)：固定尺寸复刻原行宽行高（左缘交列
         * 布局器——盒式布局交叉轴无对齐位时水平方向默认左贴齐）。 */
        XWidget_setFixedWidth((XWidget*)s_adv.completerEdit, 220);
        XWidget_setFixedHeight((XWidget*)s_adv.completerEdit, 26);
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
        s_adv.completerCaption = caption;
        if (caption) {
            XLabel_setText_2(caption, "补全输入:");
            XWidget_setFixedHeight((XWidget*)caption, 18);
            XWidget_show((XWidget*)caption);
        }
    }
#endif /* ADV_COMPLETER_ON */

#if ADV_LABEL_ON && ADV_COMPLETER_ON
    s_adv.completerStatus = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             (XWidget*)page, 0);
    if (s_adv.completerStatus) {
        XLabel_setText_2(s_adv.completerStatus, "补全: 输入前缀查看候选");
        XWidget_setFixedHeight((XWidget*)s_adv.completerStatus, 18);
        XWidget_show((XWidget*)s_adv.completerStatus);
    }
#endif

#if ADV_KSE_ON
    /* ---- XKeySequenceEdit：点击聚焦后按键捕获。 ---- */
    {
        XLabel* caption = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)page, 0);
        s_adv.kseCaption = caption;
        if (caption) {
            XLabel_setText_2(caption, "快捷键捕获（点击后按键，试 Ctrl+O）:");
            XWidget_setFixedHeight((XWidget*)caption, 18);
            XWidget_show((XWidget*)caption);
        }
    }
    s_adv.kse = XKeySequenceEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)page, 0);
    if (s_adv.kse) {
        /* 原手写 (12,324,240,28)：固定尺寸复刻原行宽行高。 */
        XWidget_setFixedWidth((XWidget*)s_adv.kse, 240);
        XWidget_setFixedHeight((XWidget*)s_adv.kse, 28);
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
        XWidget_setFixedHeight((XWidget*)s_adv.kseStatus, 18);
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
        s_adv.shortcutCaption = caption;
        if (caption) {
            /* 文案按实测字形宽（约 21px/汉字）控制在一行之内的口径
             * 保持（XLabel 不裁剪溢出文本）；备注 "单键码承载" 挪至
             * 状态行初始文案。原手写 (324,44,320,18)：高固定 18，原点
             * 交右列布局器（列顶 36px 边距复刻原列首下沉）。 */
            XLabel_setText_2(caption,
                             "全局快捷键: 按 T 触发 (对标 Ctrl+T)");
            XWidget_setFixedHeight((XWidget*)caption, 18);
            XWidget_show((XWidget*)caption);
        }
    }
    s_adv.shortcutStatus = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XWidget*)page, 0);
    if (s_adv.shortcutStatus) {
        XLabel_setText_2(s_adv.shortcutStatus,
                         "XShortcut 未触发 (键 T, 单键码承载)");
        XWidget_setFixedHeight((XWidget*)s_adv.shortcutStatus, 18);
        XWidget_show((XWidget*)s_adv.shortcutStatus);
    }
#endif

#if ADV_FOCUSFRAME_ON
    /* ---- XFocusFrame：环绕目标按钮绘制焦点框（外扩 2px）。 ---- */
    s_adv.focusTarget = XPushButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              (XWidget*)page, 0);
    if (s_adv.focusTarget) {
        XPushButton_setText_2(s_adv.focusTarget, "焦点框目标按钮");
        /* 原手写 (324,96,150,30)：固定尺寸复刻原行宽行高。 */
        XWidget_setFixedWidth((XWidget*)s_adv.focusTarget, 150);
        XWidget_setFixedHeight((XWidget*)s_adv.focusTarget, 30);
#if ADV_TOOLTIP_ON
        adv_setToolTip((XWidget*)s_adv.focusTarget,
                       "XFocusFrame 环绕的目标控件");
#endif
        XWidget_show((XWidget*)s_adv.focusTarget);
    }
    s_adv.focusFrame = XFocusFrame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             (XWidget*)page, 0);
    if (s_adv.focusFrame) {
        /* 原手写几何 (322,94,154,34) 退场：setWidget 后框架自贴合
         * 目标几何外扩 2px（XFOCUSFRAME_MARGIN），并经 30ms 跟随定时
         * 器随布局重排贴移（XFocusFrame.c 轮询定式），不参与布局。 */
        XFocusFrame_setWidget(s_adv.focusFrame, (XWidget*)s_adv.focusTarget);
        XWidget_show((XWidget*)s_adv.focusFrame);
    }
#endif /* ADV_FOCUSFRAME_ON */

#if ADV_BUTTON_ON && ADV_SPLASH_ON
    {
        XPushButton* splashButton = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)page, 0);
        s_adv.splashButton = splashButton;
        if (splashButton) {
            XPushButton_setText_2(splashButton, "显示启动画面");
            /* 原手写 (500,96,130,28)→adapt (rcX,136,130,28)：固定尺寸
             * 复刻 adapt 落位值，原点交右列布局器（焦点框行下 10px）。 */
            XWidget_setFixedWidth((XWidget*)splashButton, 130);
            XWidget_setFixedHeight((XWidget*)splashButton, 28);
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
        s_adv.mainWinButton = mainWinButton;
        if (mainWinButton) {
            XPushButton_setText_2(mainWinButton, "打开主窗口");
            /* 原手写 (324,140,150,28)→adapt (rcX,176,150,28)：固定尺寸
             * 复刻 adapt 落位值，原点交右列布局器（启动画面钮下 12px）。 */
            XWidget_setFixedWidth((XWidget*)mainWinButton, 150);
            XWidget_setFixedHeight((XWidget*)mainWinButton, 28);
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
        s_adv.rightHint = hint;
        if (hint) {
            /* 文案口径保持：无空格短句（wordWrap 仅空格断行，原手写
             * (486,136,250,54) 三行带 → 布局器铺满右列宽后一行可容，
             * 自然行高交布局器 heightForWidth 协商）。 */
            XLabel_setText_2(hint, "菜单/中央标签/左右停靠面板");
            XLabel_setWordWrap(hint, true);
            XWidget_show((XWidget*)hint);
        }
    }
#endif

#if ADV_SIZEGRIP_ON
    /* ---- XSizeGrip：页面右下角（真实拖拽由真人验证）。 ---- */
    /* 原手写贴角 (760,456,16,16)→adapt (rootW-16,rootH-16)：2026-10-17
     * 容器化后由底部行盒贴底 + 弹簧推右复刻贴角（16x16 固定尺寸，
     * autotest 尺寸断言口径不变）；与根控件 FPS 浮层的重叠关系不变
     * （2026-09-25 页7 SizeGrip 攻坚：沉入浮层之下被盖压旧账）。 */
    s_adv.sizeGrip = XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         (XWidget*)page);
    if (s_adv.sizeGrip) {
        XWidget_setFixedWidth((XWidget*)s_adv.sizeGrip, 16);
        XWidget_setFixedHeight((XWidget*)s_adv.sizeGrip, 16);
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

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* ---- 页内布局容器化（2026-10-17）：页根挂垂直盒布局器统一分配
     * 几何（原页内 setGeometry 手写排布全部退场）。观感对齐改造前
     * 截图：根边距 (12,8,12,8) 复刻原手写原点 (12,8)；行距用单条目
     * 行盒底边距近似（kbd 页同款定式）；左列 45/右列 55 伸缩近似原
     * adapt 的 45% 宽左列；右列左缘 24px/顶缘 36px 边距复刻原列间距
     * 与列首下沉；SizeGrip 由底部行盒（弹簧 stretch=1 推右）贴右下
     * 角。XFocusFrame/XRubberBand 为瞬态覆盖件，不参与布局（前者随
     * 目标自贴合，后者由鼠标事件驱动）。 ---- */
    /* 显式首选尺寸与 setFixed 对齐（理由见 adv_setHint @details）。 */
    adv_setHint((XWidget*)s_adv.textEdit, 196, 232);
    adv_setHint((XWidget*)s_adv.insertButton, 100, 26);
    /* 会超宽的文本行首选宽=文案自然宽（理由见 adv_setNatHint）；其余
     * 说明行文案短于列宽下限，首选 0 交列伸缩即可。换档文案后的自然
     * 首选宽由 adv_applyLayoutTier 重给。 */
#if ADV_LABEL_ON
    adv_setNatHint(s_adv.topHint);
    adv_setNatHint(s_adv.shortcutCaption);
    adv_setNatHint(s_adv.shortcutStatus);
#else
    adv_setHint((XWidget*)s_adv.topHint, 0, 18);
    adv_setHint((XWidget*)s_adv.shortcutCaption, 0, 18);
    adv_setHint((XWidget*)s_adv.shortcutStatus, 0, 18);
#endif
    adv_setHint((XWidget*)s_adv.completerCaption, 0, 18);
    adv_setHint((XWidget*)s_adv.completerEdit, 220, 26);
    adv_setHint((XWidget*)s_adv.completerStatus, 0, 18);
    adv_setHint((XWidget*)s_adv.kseCaption, 0, 18);
    adv_setHint((XWidget*)s_adv.kse, 240, 28);
    adv_setHint((XWidget*)s_adv.kseStatus, 0, 18);
    adv_setHint((XWidget*)s_adv.focusTarget, 150, 30);
    adv_setHint((XWidget*)s_adv.splashButton, 130, 28);
    adv_setHint((XWidget*)s_adv.mainWinButton, 150, 28);
    adv_setHint((XWidget*)s_adv.rightHint, 0, 18);
    adv_setHint((XWidget*)s_adv.sizeGrip, 16, 16);
    XBoxLayout_init(&s_adv.rootLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_adv.rootLayout, 12, 8, 12, 8);
    /* 双列区：左列 45 / 右列 55（≈原 adapt leftColW=rootW*45%）。 */
    XBoxLayout_init(&s_adv.contentRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_init(&s_adv.leftCol, XBoxLayoutDirection_TopToBottom);
    XBoxLayout_init(&s_adv.rightCol, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_adv.rightCol, 24, 36, 0, 0);
    XBoxLayout_addLayout(&s_adv.contentRow, (XLayout*)&s_adv.leftCol);
    XBoxLayout_addLayout(&s_adv.contentRow, (XLayout*)&s_adv.rightCol);
    XBoxLayout_setStretch(&s_adv.contentRow, 0, 45);
    XBoxLayout_setStretch(&s_adv.contentRow, 1, 55);
    /* 左列首行：编辑器伸展铺宽 + 键入钮经单条目列盒顶对齐（盒式布
     * 局交叉轴垂直方向默认居中，列盒把余量留在主轴末尾=钮下方）。 */
    XBoxLayout_init(&s_adv.topRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_init(&s_adv.btnCol, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_adv.btnCol, 12, 0, 0, 0);
    if (s_adv.textEdit) {
        XBoxLayout_addWidget(&s_adv.topRow, (XWidget*)s_adv.textEdit);
        XBoxLayout_setStretch(&s_adv.topRow, 0, 1);
    }
    if (s_adv.insertButton)
        XBoxLayout_addWidget(&s_adv.btnCol, (XWidget*)s_adv.insertButton);
    XBoxLayout_addLayout(&s_adv.topRow, (XLayout*)&s_adv.btnCol);
    XLayout_setContentsMargins((XLayout*)&s_adv.topRow, 0, 0, 0, 8);
    XBoxLayout_addLayout(&s_adv.leftCol, (XLayout*)&s_adv.topRow);
    /* 左列其余行（行距≈原 adapt y 差：248/276/296/328/356/378/410）。 */
    s_adv.rowCount = 0;
    if (s_adv.topHint)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.topHint, 8);
    if (s_adv.completerCaption)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.completerCaption, 4);
    if (s_adv.completerEdit)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.completerEdit, 6);
    if (s_adv.completerStatus)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.completerStatus, 8);
    if (s_adv.kseCaption)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.kseCaption, 4);
    if (s_adv.kse)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.kse, 4);
    if (s_adv.kseStatus)
        adv_rowAdd(&s_adv.leftCol, (XWidget*)s_adv.kseStatus, 0);
    adv_springAdd(&s_adv.leftCol);
    /* 右列（行距≈原 adapt y 差：44/66/96/136/176）。 */
    if (s_adv.shortcutCaption)
        adv_rowAdd(&s_adv.rightCol, (XWidget*)s_adv.shortcutCaption, 4);
    if (s_adv.shortcutStatus)
        adv_rowAdd(&s_adv.rightCol, (XWidget*)s_adv.shortcutStatus, 12);
    if (s_adv.focusTarget)
        adv_rowAdd(&s_adv.rightCol, (XWidget*)s_adv.focusTarget, 10);
    if (s_adv.splashButton)
        adv_rowAdd(&s_adv.rightCol, (XWidget*)s_adv.splashButton, 12);
    if (s_adv.mainWinButton)
        adv_rowAdd(&s_adv.rightCol, (XWidget*)s_adv.mainWinButton, 8);
    if (s_adv.rightHint)
        adv_rowAdd(&s_adv.rightCol, (XWidget*)s_adv.rightHint, 0);
    adv_springAdd(&s_adv.rightCol);
    /* 底部行：弹簧推 SizeGrip 贴右缘（行高=手柄 16 固定）。 */
    XBoxLayout_init(&s_adv.bottomRow, XBoxLayoutDirection_LeftToRight);
    adv_springAdd(&s_adv.bottomRow);
    if (s_adv.sizeGrip)
        XBoxLayout_addWidget(&s_adv.bottomRow, (XWidget*)s_adv.sizeGrip);
    /* 根装配：双列区伸展铺满（列内多余高度由列尾弹簧吸收，行盒保
     * 持首选高），底部行固定高贴底——挂页根即激活（attachWidget→
     * activate），此后随 XStackedLayout 重贴根几何经 resize→
     * XLayout_activate 自动重排。 */
    XBoxLayout_addLayout(&s_adv.rootLayout, (XLayout*)&s_adv.contentRow);
    XBoxLayout_setStretch(&s_adv.rootLayout, 0, 1);
    XBoxLayout_addLayout(&s_adv.rootLayout, (XLayout*)&s_adv.bottomRow);
    XWidget_setLayout((XWidget*)page, (XLayout*)&s_adv.rootLayout);
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

    return (XWidget*)page;
}

/** @brief 自适应重排（xgui_demo_pages.h 契约）：宽度分档唯一入口。
 * @details 几何分配全权在页根 XBoxLayout（build 一次装配；XStackedLayout
 *          重贴根几何时经 resize→XLayout_activate 自动重排，左列 45/
 *          右列 55 伸缩、SizeGrip 贴右下角等装配语义由布局器持续保持），
 *          本函数只负责跨档：按页根有效宽分档（<ADV_STACK_MAX_W=竖屏
 *          单列档；主文件 demo_layout_content 在切页/resize/startup/
 *          停靠全路径的几何分配之后调用，根尺寸保证新鲜），档位留档
 *          s_adv.singleCol，同档短路零开销；跨档套用后显式 activate
 *          一次让改档立即落几何——adapt 不在事件循环泵内，等
 *          LayoutRequest 投递会晚一拍。宽 0（占位壳未定尺）时等下一
 *          轮有效宽再套档。 */
void demo_page_advanced_adapt(XWidget* page)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    int w;
    bool single;
    if (!s_adv.page || page != (XWidget*)s_adv.page)
        return; /* 占位壳/错页（防错口径与 autotest 的 -1 同源，静默）。 */
    w = XWidget_width(page);
    if (w <= 0) return;
    single = w < ADV_STACK_MAX_W;
    if (single == s_adv.singleCol) return;
    s_adv.singleCol = single;
    adv_applyLayoutTier(single);
    XLayout_activate((XLayout*)&s_adv.rootLayout);
#else
    (void)page;
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
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
            XClassDelete((XClass*)plain);
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
            XClassDelete((XClass*)plain);
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
            XClassDelete((XClass*)current);
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
            XClassDelete((XClass*)current);
        current = XCompleter_pathFromIndex(s_adv.completer, 0);
        ADV_EXPECT(current && XString_equals_utf8(current, "Open File",
                                                  XChar_CaseSensitive),
                   "XCompleter pathFromIndex(0) 取词 Open File");
        if (current)
            XClassDelete((XClass*)current);
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
                XClassDelete((XClass*)tipText);
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
        XSplashScreen* splash = XSplashScreen_create(NULL,
                                                     XWindowType_SplashScreen);
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
            XClassDelete(splash); /* 临时顶层销毁 */
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

            /* ---- 7b. 浮动拖动跟指：停靠面板拖出成浮动态，面板原点 =
             *       指针全局坐标 − 抓取偏移（对标 Qt QDockWidget::
             *       startDrag 的 dragOffset 语义）。合成事件按平台后端
             *       口径投递全局坐标（XMouseEvent_setGlobalPosition）；
             *       移动事件的**局部坐标刻意给陈旧值**——真机复现的正是
             *       「脱靠后事件局部坐标仍停留在旧宿主窗坐标系」那条路径
             *       （桌面端跟指跑飞根因：旧实现把局部坐标加面板原点、
             *       每步再叠一次面板原点，位置逐事件放大数百倍飞出屏幕）。
             *       断言浮动态原点=停靠原点+指针位移，且不受陈旧局部
             *       坐标影响。 ---- */
            if (dockRight) {
                XPoint zero;
                XPoint origin;
                XPoint pressGlobal;
                XPoint moveGlobal;
                XPoint pressLocal;
                XPoint moveLocal;
                XMouseEvent me;
                XPoint_init(&zero, 0, 0);
                origin = XWidget_mapToGlobal((XWidget*)dockRight, &zero);
                /* 按下：面板标题条内（面板局部 x=70 命中标题条带）。 */
                XPoint_init(&pressLocal, 70, 0);
                XPoint_init(&pressGlobal, origin.x + 70, origin.y);
                XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                                 XMouseButton_LeftButton,
                                 XKeyboardModifier_NoModifier, pressLocal);
                XMouseEvent_setGlobalPosition(&me, &pressGlobal);
                XObject_event_base((XObject*)dockRight, (XEvent*)&me);
                /* 移动：指针位移 (+40,+30)；局部坐标给**陈旧值**（旧宿主
                 * 窗口系的坐标——真机上脱靠后正是这样失效的），跟指必须
                 * 只吃全局坐标。 */
                /* 位移 (+300,+500)：落点在宿主主窗之外——释放点若落在
                 * 宿主内，Qt 语义是「拖回停靠」（XMainWindow_finishDrop
                 * 落位），面板会被重新停靠而非保持浮动。 */
                XPoint_init(&moveLocal, origin.x + 370, origin.y + 500);
                XPoint_init(&moveGlobal, pressGlobal.x + 300,
                            pressGlobal.y + 500);
                XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE,
                                 XMouseButton_NoButton,
                                 XKeyboardModifier_NoModifier, moveLocal);
                XMouseEvent_setGlobalPosition(&me, &moveGlobal);
                XMouseEvent_setButtons(&me, XMouseButton_LeftButton);
                XObject_event_base((XObject*)dockRight, (XEvent*)&me);
                /* 释放：结束拖动（解鼠标抓取）。 */
                XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                                 XMouseButton_LeftButton,
                                 XKeyboardModifier_NoModifier, moveLocal);
                XMouseEvent_setGlobalPosition(&me, &moveGlobal);
                XObject_event_base((XObject*)dockRight, (XEvent*)&me);
                {
                    XPoint now;
                    XPoint_init(&zero, 0, 0);
                    now = XWidget_mapToGlobal((XWidget*)dockRight, &zero);
                    ADV_EXPECT(XDockWidget_isFloating(dockRight),
                               "XDockWidget 拖出后转浮动态");
                    ADV_EXPECT(XDockWidget_isFloating(dockRight) &&
                               now.x == origin.x + 300 && now.y == origin.y + 500,
                               "XDockWidget 浮动态原点=停靠原点+指针位移"
                               "（全局坐标跟指，不受陈旧局部坐标影响）");
                    /* 复位：回归停靠（随主窗级联销毁，不留顶层实例）。 */
                    if (XDockWidget_isFloating(dockRight))
                        XDockWidget_setFloating(dockRight, false);
                }
            }

            XClassDelete(win); /* 级联销毁菜单/停靠/标签 */
        }
    }

#endif /* ADV_MAINWIN_ON */


#undef ADV_EXPECT
    XPrintf("XGuiAutoTest: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures;
}


/**
 * @brief 抽事件若干轮（每轮之间小睡：绘制/上屏事件需在后续轮次处理）。
 * @details 绘制请求经独立 PAINT 事件异步派发（全局重绘帧率闸已移除；
 *          仅 resize 跟手路径仍受 XGUI_RESIZE_REPAINT_MAX_FPS 限频），
 *          只抽一次事件不够，必须「抽事件 + 小睡」多轮，屏幕才是最新态。
 * @param rounds 抽事件轮数。
 * @param msleepMs 每轮之间休眠毫秒数。
 * @return 无返回值。
 */
static void adv_probePump(int rounds, uint32_t msleepMs)
{
    int k;
    for (k = 0; k < rounds; ++k)
    {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(msleepMs);
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/**
 * @brief 写相位标记文件（屏外抓帧器的同步点）。
 * @param phase 相位文件路径。
 * @param value 相位值（1=停靠态 / 2=跟手拖动中 / 3=松手后 / 4=强制重绘
 *             基准帧 / 5=光标走位后）。
 * @return 无返回值。
 */
static void adv_probePhase(const char* phase, int value)
{
    FILE* pf = fopen(phase, "w");
    if (pf) { fprintf(pf, "%d\n", value); fclose(pf); }
}

/**
 * @brief 往几何文件追加一行（让位区/宿主/面板矩形，供屏外逐像素量化）。
 * @param geomPath 几何文件路径。
 * @param tag 行首标签。
 * @param x 左。
 * @param y 上。
 * @param w 宽。
 * @param h 高。
 * @return 无返回值。
 */
static void adv_probeGeom(const char* geomPath, const char* tag, int x,
                          int y, int w, int h)
{
    FILE* gf = fopen(geomPath, "a");
    if (gf)
    {
        fprintf(gf, "%s %d %d %d %d\n", tag ? tag : "", x, y, w, h);
        fclose(gf);
    }
}

/**
 * @brief 可见浮动态拖动自检（设备端残影量化；独立轻量路径）。
 * @details 用真实主窗 + 真实右侧停靠面板做一次「脱靠 + 跟手拖动」，并写
 *          相位标记文件：1=停靠态（拖动前基准）、2=跟手拖动中、3=松手后、
 *          4=强制宿主整窗重绘（让位区「应有内容」基准帧）、5=光标走位后。
 *          屏外用 fbsnap 抓帧，比对「让位区＝旧停靠位矩形」在相位 2/3 与
 *          相位 4 的逐像素差异——判据与离屏回归锁 XGuiDockDrag_Test 同款
 *          （相位 2/3 必须当场呈现宿主当前内容，不能等宿主被别的路径重绘
 *          才补上）。无需触摸注入（设备无 /dev/uinput，注入器不可用）。
 *          独立于 autotest（设备内存余量极小，autotest 多窗叠加会触发
 *          OOM kill）。
 * @param phase 相位标记文件路径；为空则不执行。
 * @return 实际执行了拖动返回 true；否则 false。
 */
/** @brief 触摸投递目标：手指落在哪个顶层窗口上（停靠态=宿主窗；脱靠成
 *         浮动态后=浮动面板自身——真机口径：触摸按坐标命中顶层窗投递）。 */
static XWindow* adv_probeTouchTarget(XMainWindow* host, XDockWidget* dock)
{
    if (dock && XDockWidget_isFloating(dock))
    {
        XWindow* w = (XWindow*)XWidget_windowHandle((XWidget*)dock);
        if (w) return w;
    }
    return host ? (XWindow*)XWidget_windowHandle((XWidget*)host) : NULL;
}

/** @brief 注入一帧触摸（设备全局坐标 → 目标窗局部坐标）。 */
static void adv_probeTouch(XWindow* win, int devX, int devY, int state,
                            XEventType type)
{
    XTouchPoint p;
    XRect wg;
    if (!win) return;
    wg = XWindow_geometry(win);
    p.m_id = 1;
    p.m_state = state;
    p.m_position.x = (short)(devX - wg.x);
    p.m_position.y = (short)(devY - wg.y);
    p.m_globalPosition = p.m_position;
    p.m_pressure = 1.0f;
    XWindowSystemInterface_handleTouchPoints_ex(win, type, &p, 1, 0);
}

static int adv_probeDragImpl(const char* phase, int useTouch)
{
#if ADV_MAINWIN_ON
    XMouseEvent me;
    XPoint p;
    XPoint zero;
    XPoint g0;
    XPoint dockOrigin;
    char gp[512];
    int i;
    XMainWindow* host = NULL;
    XDockWidget* dock = NULL;
    XLabel* central = NULL;
    XLabel* content = NULL;
    int pressDevX = 0;
    int pressDevY = 0;
    if (!phase || !phase[0]) return false;
    XPoint_init(&zero, 0, 0);
    /* [真机复验 2026-10-31] 探针自建**顶层**宿主主窗（parent=NULL）：
     * 与离屏回归锁 XGuiDockDrag_Test 同构（宿主=顶层窗 ⇒ 脱靠前面板的
     * 像素在宿主自己的后备缓冲里 ⇒ 面板离位时让位区归位只能搬回**旧
     * 快照** = 残留成因）。demo 页的「打开主窗口」是顶层窗的**子窗口**
     * （XWindowType_Window 子控件）：宿主重排会把让位区连同中央控件
     * 一起重画 ⇒ 真机上让位区当场被补画，修复前/修复后逐像素一致，
     * 根本复现不出残留（已实测：两版二进制同帧同像素）。 */
    /* [真机复验 2026-10-31] 隐藏 demo 自己的顶层窗：让位区归位按「每个
     * **可见**顶层的后备缓冲」逐个直搬（XWindowDecoration.c
     * xwd_restoreExposeStrips 遍历 XGuiApplication_topLevelWindows）⇒
     * demo 的全屏顶层也覆盖让位区，它后备缓冲里是 demo 页面底图（与宿主
     * 底色同色）⇒ 修复前面板的旧像素被这层底图盖掉，真机复现不出残留。
     * 隐藏它＝回到「单顶层应用」的真实场景（离屏回归锁 XGuiDockDrag_Test
     * 同款：宿主是唯一顶层）。探针收尾再显示回来。 */
    {
        XWidget* demoTop = XWidget_topLevelWidget((XWidget*)s_adv.page);
        if (demoTop)
            XWidget_hide(demoTop);
    }
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return false;
    XWidget_setGeometry((XWidget*)host, 232, 90, 560, 420);
    central = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)host, 0);
    if (central) {
        XLabel_setText_2(central, "中央显示区（XLabel）");
        XMainWindow_setCentralWidget(host, (XWidget*)central);
    }
    dock = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右",
                                 (XWidget*)host, 0);
    if (dock) {
        content = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   (XWidget*)dock, 0);
        if (content)
            XLabel_setText_2(content, "右面板内容（可拖出浮动）");
        XDockWidget_setWidget(dock, (XWidget*)content);
        XMainWindow_addDockWidget(host, XDockWidgetArea_Right,
                                  (XWidget*)dock);
    }
    if (!dock) return false;
    XWidget_show((XWidget*)host);
    XWidget_update((XWidget*)host);
    /* [真机复验 2026-10-31] 探针跑在事件循环**之前**：不主动抽事件则窗口
     * 从未绘制（相位 1 抓到的还是 demo 页面底图，停靠面板根本没上屏 ⇒
     * 让位区无从比对）。抽事件 + 小睡若干轮让限频重绘真正落到屏上。 */
    adv_probePump(8, 100);
    /* 让位区（旧停靠位矩形，设备全局坐标）与宿主矩形落盘：屏外按此矩形
     * 做逐像素量化，不再靠肉眼估位。 */
    snprintf(gp, sizeof(gp), "%s.geom", phase);
    {
        FILE* gf = fopen(gp, "w");
        if (gf) fclose(gf);
    }
    g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
    dockOrigin = g0;
    adv_probeGeom(gp, "dockOld", g0.x, g0.y,
                  XWidget_width((XWidget*)dock),
                  XWidget_height((XWidget*)dock));
    g0 = XWidget_mapToGlobal((XWidget*)host, &zero);
    adv_probeGeom(gp, "host", g0.x, g0.y,
                  XWidget_width((XWidget*)host),
                  XWidget_height((XWidget*)host));
    /* 相位 1 = 停靠态（拖动前基准帧）。 */
    adv_probePhase(phase, 1);
    XThread_sleep(2); /* 给屏外抓帧留窗口（相位 1 → 相位 2）。 */
    /* 与真机操作同路径：按**停靠态**面板的自绘标题条 → 移动（越过阈值
     * 即脱靠成浮动态）→ 继续跟手移动 → 抬起。浮动态面板走 CSD 装饰、
     * 没有自绘标题条，故必须从停靠态起手（先前探针「先浮动再按」根本
     * 落不到停靠面板的 mousePressEvent ⇒ 合成拖动从未生效）。
     * 位移刻意取「水平向左出宿主」：①释放点落在宿主停靠区外 ⇒ 不触发
     * 回停靠，宿主布局在相位 2/3/4 三帧保持一致（基准帧可比）；②拖动
     * 途中指针不落在停靠区内 ⇒ 无落点指示器像素干扰比对。 */
    /* 按下点取**停靠面板**标题条（面板全局原点 + 面板内 (70,4)）——不是
     * 宿主原点：宿主原点 + (70,4) 落在中央控件上，触摸命中中央控件 ⇒
     * 合成 press 到不了停靠面板 ⇒ 拖动根本不起步（探针自身坐标错，
     * 2026-10-31 真机复验修正）。 */
    pressDevX = dockOrigin.x + 70;
    pressDevY = dockOrigin.y + 4;
    if (useTouch) {
        /* 真机路径等价：注入触摸帧 → 框架手势状态机合成左键 press →
         * 命中派发（与真人手指完全同链，含 CSD 条带拦截）。 */
        adv_probeTouch(adv_probeTouchTarget(host, dock), pressDevX, pressDevY,
                       XTOUCHPOINT_STATE_PRESSED, XEVENT_TYPE_TOUCH_BEGIN);
        /* 触摸帧走「自发事件投递」= 入队派发（真机 fbinput 同一路径）：
         * 不抽事件则整帧滞留队列 ⇒ 合成 press 永不发生 ⇒ 拖动不起步。 */
        adv_probePump(1, 0);
    } else {
    XPoint_init(&p, 70, 4);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier, p);
    XMouseEvent_setButtons(&me, XMouseButton_LeftButton);
    XObject_event_base((XObject*)dock, (XEvent*)&me);
    }
    for (i = 1; i <= 8; ++i) {
        if (useTouch) {
            adv_probeTouch(adv_probeTouchTarget(host, dock),
                           pressDevX + i * 5, pressDevY + i,
                           XTOUCHPOINT_STATE_UPDATED, XEVENT_TYPE_TOUCH_UPDATE);
            adv_probePump(1, 0);
        } else {
        XPoint_init(&p, 70 + i * 5, 4 + i);
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                         XKeyboardModifier_NoModifier, p);
        XMouseEvent_setButtons(&me, XMouseButton_LeftButton);
        XObject_event_base((XObject*)dock, (XEvent*)&me);
        }
        /* [真机复验 2026-10-31] 步间只**睡**不抽事件：①跟指有 16ms
         * 移动压缩（XDockWidget.c「距上次跟随 <16ms 直接丢弃」），步间
         * 必须 ≥16ms 否则中间位置被丢弃；②不抽事件 ⇒ 宿主重排的绘制
         * 事件在拖动期间不落地，归属顶层后备缓冲仍是旧快照 ⇒ 面板离位
         * 时让位区归位只能搬回**旧像素**（残留成因，与真人拖动一致：
         * 指针快、宿主重排还没来得及落屏）。离屏回归锁 XGuiDockDrag_Test
         * 同款手法（步间 XThread_sleep(24) + 移动后 processEvents）。 */
        XThread_msleep(24);
    }
    g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
    adv_probeGeom(gp, "panelAfterDrag", g0.x, g0.y,
                  XWidget_width((XWidget*)dock),
                  XWidget_height((XWidget*)dock));
    adv_probeGeom(gp, "floatingAfterDrag",
                  XDockWidget_isFloating(dock) ? 1 : 0, 0, 0, 0);
    adv_probeGeom(gp, "draggingAfterDrag", dock->m_dragging, 0, 0, 0);
    /* 相位 2 = 面板刚离位（鼠标仍按下、宿主重排尚未上屏）：让位区必须
     * 当场已是宿主当前内容（不得留旧面板像素）。此刻不抽事件 ⇒ 屏面冻结
     * 在离位瞬间，屏外抓到的就是「离位后第一刻」。 */
    adv_probePhase(phase, 2);
    XThread_sleep(2); /* 给屏外抓帧留窗口（相位 2 → 相位 3）。 */
    /* 相位 3 = 拖动中、宿主重排已上屏（抽事件后）：让位区仍须是宿主内容。 */
    adv_probePump(8, 100);
    adv_probePhase(phase, 3);
    XThread_sleep(2); /* 给屏外抓帧留窗口（相位 3 → 相位 4）。 */
    /* 松手：释放点落在宿主停靠区外 ⇒ 面板保持浮动态（宿主布局不变）。 */
    if (useTouch) {
        adv_probeTouch(adv_probeTouchTarget(host, dock),
                       pressDevX + 8 * 5, pressDevY + 8,
                       XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
        adv_probePump(1, 0);
    } else {
    XPoint_init(&p, 70 + 8 * 5, 4 + 8);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier, p);
    XMouseEvent_setButtons(&me, XMouseButton_NoButton);
    XObject_event_base((XObject*)dock, (XEvent*)&me);
    }
    adv_probePump(4, 100);
    g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
    adv_probeGeom(gp, "panelAfterRelease", g0.x, g0.y,
                  XWidget_width((XWidget*)dock),
                  XWidget_height((XWidget*)dock));
    adv_probeGeom(gp, "floatingAfterRelease",
                  XDockWidget_isFloating(dock) ? 1 : 0, 0, 0, 0);
    /* 相位 4 = 松手后（用户症状「脱手也没重新渲染」）。 */
    adv_probePhase(phase, 4);
    XThread_sleep(2); /* 给屏外抓帧留窗口（相位 4 → 相位 5）。 */
    /* 相位 5 = 强制宿主整窗重绘：得到「让位区应有的内容」基准帧（离屏
     * 回归锁 XGuiDockDrag_Test 同款基准帧：强制宿主整窗重绘后的让位区
     * 内容 = 宿主当前内容）。 */
    XWidget_repaint((XWidget*)host);
    adv_probePump(8, 100);
    adv_probePhase(phase, 5);
    XThread_sleep(2); /* 给屏外抓帧留窗口（相位 5 → 相位 6）。 */
    /* [残留自检] 程序化移动软件光标走一条路径——fbdev 无触摸注入依赖，
     * 光标移动即触发「精灵原位搬运」链；屏外抓帧比对路径上是否留下
     * 光标鬼影（多箭头残留 = 旧位未还原/被跨缓冲翻页带出）。
     * 相位 5 = 光标走完路径（屏外此刻抓帧）。 */
    {
        int k;
        for (k = 0; k < 12; ++k) {
            XCursor_setPos(300 + k * 20, 200 + k * 12);
            XCoreApplication_processEvents(XEventLoop_AllEvents);
        }
        for (k = 11; k >= 0; --k) {
            XCursor_setPos(300 + k * 20, 200 + k * 12);
            XCoreApplication_processEvents(XEventLoop_AllEvents);
        }
        XCursor_setPos(300, 200);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        adv_probePhase(phase, 6);
    }
    /* 探针宿主收尾：销毁顶层窗（连带中央控件/停靠面板/浮动窗）。 */
    XClassDelete((XClass*)host);
    {
        XWidget* demoTop = XWidget_topLevelWidget((XWidget*)s_adv.page);
        if (demoTop)
            XWidget_show(demoTop);
    }
    return true;
#else
    (void)phase;
    (void)useTouch;
    return false;
#endif /* ADV_MAINWIN_ON */
}

/** @brief 真机拖动自检（鼠标直投路径；历史行为，保留）。 */
int demo_page_advanced_probeDrag(const char* phase)
{
    return adv_probeDragImpl(phase, 0);
}

/** @brief 真机拖动自检（**触摸**路径：注入触摸帧 → 框架手势状态机合成
 *         左键 → 命中派发，与真人手指同链，含 fbdev 框架自绘 CSD 标题
 *         条带的事件拦截）。 */
int demo_page_advanced_probeDragTouch(const char* phase)
{
    return adv_probeDragImpl(phase, 1);
}

/** @brief 真机「预停靠位置 = 实际停靠位置」自检（用户症状：拖动时预览框
 *         画在一处、松手后落位到另一处）。
 *  @details 在**真实 demo 主窗**上把右侧面板往宿主内的目标区拖，拖动中
 *           记录落点指示器矩形（预览承诺），松手后记录面板实际矩形（兑
 *           现），把两者与区码一并落盘到 <phase>.geom。屏外判据按
 *           `previewArea`/`dropArea` 与两个矩形比对（见 wf_mcgs_dock3_ab.sh
 *           的 DROP_PREVIEW 段）。
 *           相位约定（复用 cap.sh 抓帧器）：
 *             1 = 停靠态基准；2 = 拖动中（指示器可见，预览帧）；
 *             3 = 松手落位后；4 = 强制重绘基准；5 = 指示器残留检查点。
 *  @param phase 相位/几何文件前缀路径；为空则不执行。
 *  @return 执行了拖动返回 true。
 *  @note  释放点刻意落在宿主**内**的停靠区（演示「预览→兑现」链，与
 *         probeDragReal 的「拖到宿主外保持浮动」互补）。 */
int demo_page_advanced_probeDropGeom(const char* phase)
{
#if ADV_MAINWIN_ON
    XMainWindow* host = NULL;
    XDockWidget* dock = NULL;
    XPoint zero;
    XPoint dorg;
    XPoint horg;
    XRect hostRect;
    XRect preview;
    XRect landed;
    char gp[512];
    int pressX;
    int pressY;
    int targetX;
    int targetY;
    int hoverArea = 0;
    int step;
    if (!phase || !phase[0]) return false;
    XPoint_init(&zero, 0, 0);
    adv_ensureMainWindow();
    host = s_adv.mainWindow;
    dock = s_adv.dockRight;
    if (!host || !dock) return false;
    XWidget_show((XWidget*)host);
    XWidget_update((XWidget*)host);
    adv_probePump(8, 100);
    snprintf(gp, sizeof(gp), "%s.geom", phase);
    { FILE* gf = fopen(gp, "w"); if (gf) fclose(gf); }
    /* 保证起手是**右侧停靠态**（上一轮探针可能把它拖出浮动）。 */
    if (XDockWidget_isFloating(dock)) XDockWidget_setFloating(dock, false);
    XMainWindow_addDockWidget(host, XDockWidgetArea_Right, (XWidget*)dock);
    adv_probePump(4, 50);
    horg = XWidget_mapToGlobal((XWidget*)host, &zero);
    XRect_init(&hostRect, horg.x, horg.y, XWidget_width((XWidget*)host),
               XWidget_height((XWidget*)host));
    adv_probeGeom(gp, "host", hostRect.x, hostRect.y, hostRect.width,
                  hostRect.height);
    dorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
    adv_probeGeom(gp, "dockDocked", dorg.x, dorg.y,
                  XWidget_width((XWidget*)dock),
                  XWidget_height((XWidget*)dock));
    adv_probePhase(phase, 1);
    XThread_sleep(2);
    /* 目标：宿主**左侧**停靠区（拖回宿主内 ⇒ 落位可兑现）。 */
    targetX = hostRect.x + hostRect.width / 8;
    targetY = hostRect.y + hostRect.height / 2;
    pressX = dorg.x + XWidget_width((XWidget*)dock) / 4;
    pressY = dorg.y + 4;
    adv_probeTouch(adv_probeTouchTarget(host, dock), pressX, pressY,
                   XTOUCHPOINT_STATE_PRESSED, XEVENT_TYPE_TOUCH_BEGIN);
    adv_probePump(1, 0);
    for (step = 1; step <= 8; ++step)
    {
        XThread_msleep(24);
        adv_probeTouch(adv_probeTouchTarget(host, dock),
                       pressX + (targetX - pressX) * step / 8,
                       pressY + (targetY - pressY) * step / 8,
                       XTOUCHPOINT_STATE_UPDATED, XEVENT_TYPE_TOUCH_UPDATE);
        adv_probePump(1, 0);
    }
    /* 拖动中：记录落点指示器矩形（预览承诺）与区码。 */
    {
        XPoint g;
        XPoint_init(&g, targetX, targetY);
        hoverArea = XMainWindow_hoverDrop(host, (XWidget*)dock, &g);
        if (host->m_dropIndicator)
            preview = XWidget_geometry(host->m_dropIndicator);
        else
            XRect_init(&preview, 0, 0, 0, 0);
        adv_probeGeom(gp, "previewArea", hoverArea, 0, 0, 0);
        adv_probeGeom(gp, "previewRect", preview.x, preview.y, preview.width,
                      preview.height);
        adv_probeGeom(gp, "previewVisible",
                      (host->m_dropIndicator &&
                       XWidget_isVisible(host->m_dropIndicator)) ? 1 : 0,
                      0, 0, 0);
    }
    adv_probePhase(phase, 2);
    XThread_sleep(2);
    /* 松手 → 落位。 */
    adv_probeTouch(adv_probeTouchTarget(host, dock), targetX, targetY,
                   XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
    adv_probePump(6, 60);
    {
        XPoint lorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
        XRect_init(&landed, lorg.x - hostRect.x, lorg.y - hostRect.y,
                   XWidget_width((XWidget*)dock),
                   XWidget_height((XWidget*)dock));
        adv_probeGeom(gp, "landedRect", landed.x, landed.y, landed.width,
                      landed.height);
        adv_probeGeom(gp, "dropArea",
                      XMainWindow_dockWidgetArea(host, (XWidget*)dock), 0, 0, 0);
        adv_probeGeom(gp, "floatingAfterDrop",
                      XDockWidget_isFloating(dock) ? 1 : 0, 0, 0, 0);
        adv_probeGeom(gp, "draggingAfterDrop", dock->m_dragging, 0, 0, 0);
        adv_probeGeom(gp, "indicatorAfterDrop",
                      (host->m_dropIndicator &&
                       XWidget_isVisible(host->m_dropIndicator)) ? 1 : 0,
                      0, 0, 0);
        /* 指示器矩形（隐藏后仍在同一位置；判据取该矩形检查是否有残留像素）。 */
        if (host->m_dropIndicator)
        {
            XRect ir = XWidget_geometry(host->m_dropIndicator);
            adv_probeGeom(gp, "indicatorRect", ir.x, ir.y, ir.width,
                          ir.height);
        }
    }
    adv_probePhase(phase, 3);
    XThread_sleep(2);
    /* 相位 4 = 强制整窗重绘基准（让位区/指示器区「应有内容」）。 */
    XWidget_repaint((XWidget*)host);
    adv_probePump(8, 100);
    adv_probePhase(phase, 4);
    XThread_sleep(2);
    /* 相位 5 = 再睡一拍后抓一帧：检查指示器区是否残留（异步失效兜底）。 */
    adv_probePump(8, 100);
    adv_probePhase(phase, 5);
    XThread_sleep(2);
    return true;
#else
    (void)phase;
    return false;
#endif /* ADV_MAINWIN_ON */
}

/* ==================== 连续扫掠探针（真机连续逐帧验证） ==================== */

#if ADV_MAINWIN_ON
/** @brief 扫掠路径航点（宿主客户区相对坐标，千分比）。
 *  @details 一条穿过全部四个停靠带 + 中央带的长路径：右区起手 → 横穿
 *           中央 → 左区 → 顶带 → 右沿顶 → 右区下潜 → 底带 → 左区收尾
 *           （释放点落在左区 ⇒ 回停靠）。覆盖用户真实拖动的全部落点
 *           预览跨区切换与让位条带形态。 */
static const int kSweepWpts[][2] = {
    { 550, 500 }, { 300, 500 }, { 120, 500 },  /* 右→中→左 */
    { 120, 100 },                              /* 左区上潜（进顶带） */
    { 500, 80 },  { 880, 120 },                /* 顶带横扫 → 右上 */
    { 880, 500 }, { 860, 900 },                /* 右区下潜 → 底带 */
    { 500, 920 }, { 120, 850 }, { 120, 500 }   /* 底带横扫 → 左区收尾 */
};
#define SWEEP_WPT_N (int)(sizeof(kSweepWpts) / sizeof(kSweepWpts[0]))
#define SWEEP_LEG_STEPS 4   /**< 每段航点间插值步数。 */
#define SWEEP_FBSNAP "/root/xguidemo/fbsnap"
#define SWEEP_CMP    "/root/xguidemo/mcgs_fb_cmp"
#define SWEEP_DIR    "/root/xguidemo/dock3"
#endif /* ADV_MAINWIN_ON */

/** @brief 真机连续扫掠自检（用户症状「拖动卡顿 + 残影」的连续逐帧验证）。
 *  @details 与 probeDragReal/probeDropGeom 的**相位抽帧**本质不同：本探针
 *           对扫掠的**每一步**都做一次「现状帧 vs 全屏强制重绘基准帧」的
 *           屏上逐像素比对（fbsnap 抓屏 → 遍历可见顶层强制重绘 → 再抓
 *           一帧 → 板侧 mcgs_fb_cmp 扣除面板足迹后全屏比对），任何一步
 *           有陈旧像素立即记账——连续验证，不依赖稀疏相位抽样。每步另
 *           记录「注入→事件沉降」的耗时（真机卡顿的框架侧成本）。
 *           每隔 8 步把现状帧留档为 strip_N.raw（拉回宿主目验用）。
 *           路径见 kSweepWpts：右区起手，横穿中央/左区/顶带/底带，释放
 *           点落在左区 ⇒ 回停靠。几何与汇总写 <phase>.geom / stdout：
 *           `[SWEEP] steps=N avgMs=A maxMs=M totalMismatch=T worst=k`。
 *  @param phase 相位/几何文件前缀路径；为空则不执行。
 *  @return 完整跑完返回 true。
 *  @note   fbsnap/mcgs_fb_cmp 经 system() 调用（板侧外部工具）；缺失时
 *          比对退化为「只计时」，残影判据由门脚本按 CMP 行存在性把关。 */
int demo_page_advanced_probeDragSweep(const char* phase)
{
#if ADV_MAINWIN_ON
    XMainWindow* host = NULL;
    XDockWidget* dock = NULL;
    XPoint zero;
    XPoint dorg;
    XPoint horg;
    XRect hostRect;
    char gp[512];
    char cmd[768];
    FILE* lf = NULL;
    FILE* gf = NULL;
    int pressX;
    int pressY;
    int step;
    int totalSteps = 0;
    int64_t totalMs = 0;
    int64_t maxMs = 0;
    int worstStep = 0;
    long totalMismatch = 0;
    int haveTools = 0;
    int exRects[3][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    int exN = 0;
    int i;
    char exArgs[128];
    if (!phase || !phase[0]) return false;
    XPoint_init(&zero, 0, 0);
    adv_ensureMainWindow();
    host = s_adv.mainWindow;
    dock = s_adv.dockRight;
    if (!host || !dock) return false;
    XWidget_show((XWidget*)host);
    XWidget_update((XWidget*)host);
    adv_probePump(8, 100);
    snprintf(gp, sizeof(gp), "%s.geom", phase);
    gf = fopen(gp, "w");
    lf = fopen(SWEEP_DIR "/sweep.log", "w");
    /* 起手：右侧停靠态。 */
    if (XDockWidget_isFloating(dock)) XDockWidget_setFloating(dock, false);
    XMainWindow_addDockWidget(host, XDockWidgetArea_Right, (XWidget*)dock);
    adv_probePump(4, 50);
    horg = XWidget_mapToGlobal((XWidget*)host, &zero);
    XRect_init(&hostRect, horg.x, horg.y, XWidget_width((XWidget*)host),
               XWidget_height((XWidget*)host));
    if (gf) {
        fprintf(gf, "host %d %d %d %d\n", hostRect.x, hostRect.y,
                hostRect.width, hostRect.height);
        fclose(gf);
        gf = NULL;
    }
    dorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
    pressX = dorg.x + XWidget_width((XWidget*)dock) / 2;
    pressY = dorg.y + 8;
    /* 工具齐备性：fbsnap + cmp 任一缺失 ⇒ 只计时（残影判据由门判 CMP 缺行）。
     * XGUI_SWEEP_NOTOOLS=1 ⇒ 显式只计时（timing 模式：无 fbsnap 阻塞，
     * 排除抓帧 IO 对事件环的干扰）。 */
    if (XSystem_environment("XGUI_SWEEP_NOTOOLS")) {
        haveTools = 0;
    } else {
        FILE* t = fopen(SWEEP_FBSNAP, "rb");
        FILE* c = fopen(SWEEP_CMP, "rb");
        haveTools = (t != NULL && c != NULL);
        if (t) fclose(t);
        if (c) fclose(c);
    }
    /* [时钟/HUD 自更新区排除 2026-11-03] demo 页右上角时钟与右下角性能
     * HUD 在两帧之间**自己会更新**（走秒/统计刷新），逐步 A/B 比对会把
     * 这类正常更新误记为失配（实测首失配恒为 (975,40)=时钟角落、每步
     * ~700px 全部落在两块 chrome 内）。排除矩形经
     * XGUI_SWEEP_EXCLUDE="x,y,w,h;x,y,w,h" 注入（屏面设备坐标），默认
     * 给本机 chrome 两块；最多 2 块（+面板足迹 ≤4，见 mcgs_fb_cmp）。 */
    {
        const char* ex = XSystem_environment("XGUI_SWEEP_EXCLUDE");
        if (ex && ex[0]) {
            int vals[8];
            int n = 0;
            const char* q = ex;
            while (*q && n < 12) {
                char* end;
                long v = strtol(q, &end, 10);
                if (end == q) break;
                vals[n++] = (int)v;
                q = (*end == ',' || *end == ';') ? end + 1 : end;
            }
            exN = n / 4;
            if (exN > 3) exN = 3;
            for (i = 0; i < exN; ++i) {
                exRects[i][0] = vals[i * 4];
                exRects[i][1] = vals[i * 4 + 1];
                exRects[i][2] = vals[i * 4 + 2];
                exRects[i][3] = vals[i * 4 + 3];
            }
        }
    }
    /* 按下 → 逐航点插值扫掠。每步：注入（含 3 子步模拟真实高速流）→
     * 沉降 → 抓现状帧 → 全屏强制重绘 → 抓基准帧 → 板侧逐像素比对。 */
    adv_probeTouch(adv_probeTouchTarget(host, dock), pressX, pressY,
                   XTOUCHPOINT_STATE_PRESSED, XEVENT_TYPE_TOUCH_BEGIN);
    adv_probePump(1, 0);
    for (step = 0; step < SWEEP_WPT_N - 1; ++step) {
        int leg;
        for (leg = 1; leg <= SWEEP_LEG_STEPS; ++leg) {
            int j;
            int64_t t0;
            int64_t procMs;
            long mm = -1;
            int fx = -1;
            int fy = -1;
            int px = 0;
            int py = 0;
            /* 本步 3 个子步注入（模拟真实手指的高速事件流，部分子步
             * 落进 16ms 跟指节流窗被丢弃——与真人拖动同构）。 */
            for (j = 0; j < 3; ++j) {
                int tx = hostRect.x + hostRect.width *
                         (kSweepWpts[step][0] + (kSweepWpts[step + 1][0] -
                          kSweepWpts[step][0]) * (leg - 1 + (j + 1) / 3.0f)
                          / SWEEP_LEG_STEPS) / 1000;
                int ty = hostRect.y + hostRect.height *
                         (kSweepWpts[step][1] + (kSweepWpts[step + 1][1] -
                          kSweepWpts[step][1]) * (leg - 1 + (j + 1) / 3.0f)
                          / SWEEP_LEG_STEPS) / 1000;
                XThread_msleep(8);
                adv_probeTouch(adv_probeTouchTarget(host, dock), tx, ty,
                               XTOUCHPOINT_STATE_UPDATED,
                               XEVENT_TYPE_TOUCH_UPDATE);
            }
            t0 = XDateTime_currentMSecsSinceEpoch();
            adv_probePump(2, 0);    /* 紧抽：纯事件处理耗时（卡顿判据） */
            procMs = XDateTime_currentMSecsSinceEpoch() - t0;
            adv_probePump(3, 25);   /* 宽沉降：≥4 个帧间隔，限频重绘落屏 */
            {
                XPoint p = XWidget_mapToGlobal((XWidget*)dock, &zero);
                px = p.x;
                py = p.y;
            }
            if (haveTools) {
                int cx0 = px - 1;
                int cy0 = py - 1;
                int cw = XWidget_width((XWidget*)dock) + 2;
                int ch = XWidget_height((XWidget*)dock) + 2;
                snprintf(cmd, sizeof(cmd),
                         "%s " SWEEP_DIR "/sw_a.raw " SWEEP_DIR "/sw_b.raw",
                         SWEEP_FBSNAP);
                if (system(cmd) != 0) mm = -2;
                if (mm != -2) {
                    /* 全屏强制重绘（所有可见顶层）= 基准帧。 */
                    {
                        XVector* tops = XGuiApplication_allWindows();
                        int64_t ti;
                        int64_t tn = tops
                            ? XVector_size_base((const XContainer*)tops) : 0;
                        for (ti = 0; ti < tn; ++ti) {
                            XWindow* w = *(XWindow**)XVector_at_base(
                                tops, ti);
                            XWidget* tw = w ? XWidget_widgetForWindow(w)
                                            : NULL;
                            if (tw && XWidget_isVisible(tw))
                                XWidget_repaint(tw);
                        }
                    }
                    adv_probePump(4, 30);
                    snprintf(cmd, sizeof(cmd),
                             "%s " SWEEP_DIR "/sw_b.raw", SWEEP_FBSNAP);
                    if (system(cmd) != 0) mm = -2;
                }
                if (mm != -2) {
                    /* 面板足迹（外扩 1px）作为排除矩形：面板自身像素在
                     * 两帧间本就同内容，排除后剩余失配=屏面陈旧像素。 */
                    FILE* pf;
                    int ai;
                    exArgs[0] = 0;
                    for (ai = 0; ai < exN; ++ai) {
                        char one[64];
                        snprintf(one, sizeof(one), " %d %d %d %d",
                                 exRects[ai][0], exRects[ai][1],
                                 exRects[ai][2], exRects[ai][3]);
                        strncat(exArgs, one, sizeof(exArgs) -
                                            strlen(exArgs) - 1);
                    }
                    snprintf(cmd, sizeof(cmd),
                             "cd " SWEEP_DIR " && ../mcgs_fb_cmp sw_a.raw "
                             "sw_b.raw %d %d %d %d%s 2>/dev/null",
                             cx0, cy0, cw, ch, exArgs);
                    pf = popen(cmd, "r");
                    if (pf) {
                        char line[256];
                        while (fgets(line, sizeof(line), pf)) {
                            if (strncmp(line, "CMP total=", 10) == 0) {
                                sscanf(line + 10, "%ld first=(%d,%d)",
                                       &mm, &fx, &fy);
                            }
                        }
                        pclose(pf);
                    }
                    if (mm < 0) mm = -3;
                }
            }
            ++totalSteps;
            totalMs += procMs;
            if (procMs > maxMs) { maxMs = procMs; worstStep = totalSteps; }
            if (mm > 0) totalMismatch += mm;
            if (lf) {
                fprintf(lf, "STEP %d px=%d py=%d ms=%lld mm=%ld first=(%d,%d)\n",
                        totalSteps, px, py, (long long)procMs, mm, fx, fy);
                fflush(lf);
            }
            if (gf) {
                fprintf(gf, "step%d %d %d %d %d\n", totalSteps, px, py,
                        XWidget_width((XWidget*)dock),
                        XWidget_height((XWidget*)dock));
                fflush(gf);
            }
            /* 胶片帧：每 8 步留档一帧（目验用；上限 8 帧 ~10MB）。 */
            if (haveTools && totalSteps % 8 == 1 && totalSteps <= 57) {
                snprintf(cmd, sizeof(cmd),
                         "cp " SWEEP_DIR "/sw_a.raw " SWEEP_DIR
                         "/strip_%02d.raw", totalSteps);
                (void)system(cmd);
            }
        }
    }
    /* 收尾：释放点=最后航点（左区）⇒ 回停靠。 */
    adv_probeTouch(adv_probeTouchTarget(host, dock),
                   hostRect.x + hostRect.width * kSweepWpts[SWEEP_WPT_N - 1][0]
                                / 1000,
                   hostRect.y + hostRect.height * kSweepWpts[SWEEP_WPT_N - 1][1]
                                / 1000,
                   XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
    adv_probePump(6, 60);
    {
        XPoint lorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
        gf = fopen(gp, "a");
        if (gf) {
            fprintf(gf, "landedRect %d %d %d %d\n",
                    lorg.x - hostRect.x, lorg.y - hostRect.y,
                    XWidget_width((XWidget*)dock),
                    XWidget_height((XWidget*)dock));
            fprintf(gf, "dropArea %d 0 0 0\n",
                    XMainWindow_dockWidgetArea(host, (XWidget*)dock));
            fprintf(gf, "floatingAfterDrop %d 0 0 0\n",
                    XDockWidget_isFloating(dock) ? 1 : 0);
            fprintf(gf, "draggingAfterDrop %d 0 0 0\n", dock->m_dragging);
            fclose(gf);
        }
    }
    XPrintf("[SWEEP] steps=%d avgMs=%lld maxMs=%lld worst=%d "
            "totalMismatch=%ld tools=%d\n",
            totalSteps,
            (long long)(totalSteps > 0 ? totalMs / totalSteps : 0),
            (long long)maxMs, worstStep, totalMismatch, haveTools);
    if (lf) fclose(lf);
    adv_probePhase(phase, 5);
    XThread_sleep(2);
    return true;
#else
    (void)phase;
    return false;
#endif /* ADV_MAINWIN_ON */
}

/** @brief 真机内存自检：反复「拖出右面板 → 拖回」N 轮，每轮打印本进程
 *         RSS（/proc/self/status）与窗口数，定位「拖几次后卡死」是否内存
 *         累积（昆仑通态 54MB 小板：demo 基线已 ~15MB，几 MB 泄漏即 OOM
 *         kill ⇒ 现象=进程被杀=屏黑=用户侧「卡死」）。 */
int demo_page_advanced_probeDragMem(const char* phase, int rounds)
{
#if ADV_MAINWIN_ON
    XMainWindow* host = NULL;
    XDockWidget* dock = NULL;
    XPoint zero;
    XPoint g0;
    int pressDevX;
    int pressDevY;
    int round;
    if (!phase || !phase[0] || rounds <= 0) return false;
    XPoint_init(&zero, 0, 0);
    adv_ensureMainWindow();
    host = s_adv.mainWindow;
    dock = s_adv.dockRight;
    if (!host || !dock) return false;
    XWidget_show((XWidget*)host);
    XWidget_update((XWidget*)host);
    adv_probePump(8, 100);
    for (round = 1; round <= rounds; ++round)
    {
        int step;
        XWindow* w;
        g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
        pressDevX = g0.x + 40;
        pressDevY = g0.y + 4;
        w = (XWindow*)XWidget_windowHandle((XWidget*)host);
        adv_probeTouch(w, pressDevX, pressDevY, XTOUCHPOINT_STATE_PRESSED,
                       XEVENT_TYPE_TOUCH_BEGIN);
        adv_probePump(1, 0);
        for (step = 1; step <= 4; ++step)
        {
            XThread_msleep(24);
            w = XDockWidget_isFloating(dock)
                    ? (XWindow*)XWidget_windowHandle((XWidget*)dock)
                    : (XWindow*)XWidget_windowHandle((XWidget*)host);
            adv_probeTouch(w, pressDevX + step * 20, pressDevY + step * 2,
                           XTOUCHPOINT_STATE_UPDATED,
                           XEVENT_TYPE_TOUCH_UPDATE);
            adv_probePump(1, 0);
        }
        w = XDockWidget_isFloating(dock)
                ? (XWindow*)XWidget_windowHandle((XWidget*)dock)
                : (XWindow*)XWidget_windowHandle((XWidget*)host);
        adv_probeTouch(w, pressDevX + 4 * 20, pressDevY + 4 * 2,
                       XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
        adv_probePump(2, 0);
        /* 拖回：面板已浮动态，从浮动面板的 CSD 标题条按住拖回宿主停靠区。
         * （探针注：浮动面板的 CSD 条带由装饰接管，指针拖不回停靠区，
         *   故本探针的「出→回」循环靠**释放点落在宿主停靠区**实现回停靠：
         *   拖出距离取 4×20=80，释放点 = 停靠位右移 120 ⇒ 落在宿主右侧
         *   停靠区（最右 160px）⇒ finishDrop 回停靠 ⇒ 下一轮从停靠态起手，
         *   构成与真人一致的「出→回→出→回」循环。） */
        if (0 && XDockWidget_isFloating(dock))
        {
            g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
            pressDevX = g0.x + 40;
            pressDevY = g0.y + 4;
            w = (XWindow*)XWidget_windowHandle((XWidget*)dock);
            adv_probeTouch(w, pressDevX, pressDevY, XTOUCHPOINT_STATE_PRESSED,
                           XEVENT_TYPE_TOUCH_BEGIN);
            adv_probePump(1, 0);
            for (step = 1; step <= 4; ++step)
            {
                XThread_msleep(24);
                w = (XWindow*)XWidget_windowHandle((XWidget*)dock);
                adv_probeTouch(w, pressDevX - step * 30, pressDevY + step * 2,
                               XTOUCHPOINT_STATE_UPDATED,
                               XEVENT_TYPE_TOUCH_UPDATE);
                adv_probePump(1, 0);
            }
            w = (XWindow*)XWidget_windowHandle((XWidget*)dock);
            adv_probeTouch(w, pressDevX - 4 * 30, pressDevY + 4 * 2,
                           XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
            adv_probePump(2, 0);
        }
        {
            FILE* sf = fopen("/proc/self/status", "r");
            char line[256];
            long rss = 0;
            long vsz = 0;
            if (sf) {
                while (fgets(line, (int)sizeof(line), sf)) {
                    if (strncmp(line, "VmRSS:", 6) == 0)
                        sscanf(line + 6, "%ld", &rss);
                    else if (strncmp(line, "VmSize:", 7) == 0)
                        sscanf(line + 7, "%ld", &vsz);
                }
                fclose(sf);
            }
            XPrintf("[MEM] round=%d rss=%ldkB vsz=%ldkB floating=%d "
                    "drag=%d\n", round, rss, vsz,
                    XDockWidget_isFloating(dock) ? 1 : 0, dock->m_dragging);
        }
        adv_probePump(4, 50);
    }
    return true;
#else
    (void)phase; (void)rounds;
    return false;
#endif /* ADV_MAINWIN_ON */
}

/** @brief 真机拖动自检（**demo 真实主窗 + 真实面板**场景）：用「打开主窗
 *         口」那条路径创建的独立主窗（宿主=演示主窗的**子窗口**，带菜单
 *         栏、中央标签、左右两个真实停靠面板），触摸路径把**右侧**面板
 *         拖出浮动态，落盘两面板/菜单栏/宿主的几何，供屏外逐像素比对与
 *         几何核对（用户现场症状：另一侧面板标题条被拉成整宽、压住菜单
 *         栏；拖动后旧位留画面）。
 *  @note  与 probeDragTouch 的差别：后者自建顶层宿主（离屏锁同构），本
 *         函数用 demo 真实场景（子窗口宿主 + 菜单栏 + 双面板）。 */
int demo_page_advanced_probeDragReal(const char* phase)
{
#if ADV_MAINWIN_ON
    XMainWindow* host = NULL;
    XDockWidget* dock = NULL;
    XDockWidget* left = NULL;
    XPoint zero;
    XPoint g0;
    XRect mg;
    char gp[512];
    int pressDevX;
    int pressDevY;
    int step;
    XWindow* target;
    if (!phase || !phase[0]) return false;
    XPoint_init(&zero, 0, 0);
    adv_ensureMainWindow();
    host = s_adv.mainWindow;
    dock = s_adv.dockRight;
    left = s_adv.dockLeft;
    if (!host || !dock) return false;
    XWidget_show((XWidget*)host);
    XWidget_update((XWidget*)host);
    adv_probePump(8, 100);
    snprintf(gp, sizeof(gp), "%s.geom", phase);
    g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
    adv_probeGeom(gp, "dockOld", g0.x, g0.y, XWidget_width((XWidget*)dock),
                  XWidget_height((XWidget*)dock));
    if (left)
    {
        g0 = XWidget_mapToGlobal((XWidget*)left, &zero);
        adv_probeGeom(gp, "leftOld", g0.x, g0.y, XWidget_width((XWidget*)left),
                      XWidget_height((XWidget*)left));
    }
    g0 = XWidget_mapToGlobal((XWidget*)host, &zero);
    adv_probeGeom(gp, "host", g0.x, g0.y, XWidget_width((XWidget*)host),
                  XWidget_height((XWidget*)host));
    mg = XWidget_geometry((XWidget*)XMainWindow_menuBar(host));
    adv_probeGeom(gp, "menuBar", mg.x, mg.y, mg.width, mg.height);
    adv_probePhase(phase, 1);
    XThread_sleep(2);
    /* 触摸拖出右侧面板（脱靠后手指落在浮动面板自身 ⇒ 投递目标切换）。 */
    pressDevX = g0.x + 0; /* 占位，下面按面板原点重算 */
    pressDevX = XWidget_mapToGlobal((XWidget*)dock, &zero).x + 40;
    pressDevY = XWidget_mapToGlobal((XWidget*)dock, &zero).y + 4;
    target = (XWindow*)XWidget_windowHandle((XWidget*)host);
    adv_probeTouch(target, pressDevX, pressDevY, XTOUCHPOINT_STATE_PRESSED,
                   XEVENT_TYPE_TOUCH_BEGIN);
    adv_probePump(1, 0);
    for (step = 1; step <= 6; ++step)
    {
        XWindow* w;
        XThread_msleep(24);
        w = XDockWidget_isFloating(dock)
                ? (XWindow*)XWidget_windowHandle((XWidget*)dock)
                : (XWindow*)XWidget_windowHandle((XWidget*)host);
        adv_probeTouch(w, pressDevX + step * 30, pressDevY + step * 2,
                       XTOUCHPOINT_STATE_UPDATED, XEVENT_TYPE_TOUCH_UPDATE);
        adv_probePump(1, 0);
    }
    {
        XWindow* w = XDockWidget_isFloating(dock)
                         ? (XWindow*)XWidget_windowHandle((XWidget*)dock)
                         : (XWindow*)XWidget_windowHandle((XWidget*)host);
        adv_probeTouch(w, pressDevX + 6 * 30, pressDevY + 6 * 2,
                       XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
        adv_probePump(1, 0);
    }
    g0 = XWidget_mapToGlobal((XWidget*)dock, &zero);
    adv_probeGeom(gp, "panelAfterDrag", g0.x, g0.y,
                  XWidget_width((XWidget*)dock), XWidget_height((XWidget*)dock));
    adv_probeGeom(gp, "floatingAfterDrag",
                  XDockWidget_isFloating(dock) ? 1 : 0, 0, 0, 0);
    adv_probeGeom(gp, "draggingAfterDrag", dock->m_dragging, 0, 0, 0);
    if (left)
    {
        g0 = XWidget_mapToGlobal((XWidget*)left, &zero);
        adv_probeGeom(gp, "leftAfter", g0.x, g0.y, XWidget_width((XWidget*)left),
                      XWidget_height((XWidget*)left));
    }
    adv_probePhase(phase, 2);
    XThread_sleep(2);
    adv_probePump(8, 100);
    adv_probePhase(phase, 3);
    XThread_sleep(2);
    XWidget_repaint((XWidget*)host);
    adv_probePump(8, 100);
    adv_probePhase(phase, 4);
    XThread_sleep(2);
    return true;
#else
    (void)phase;
    return false;
#endif /* ADV_MAINWIN_ON */
}

#else /* !XWIDGET_ON */

/* 整文件裁剪哨兵：XWidget 模块关闭时本翻译单元为空实现。 */
typedef int xgui_demo_page_advanced_disabled_t;

#endif /* XWIDGET_ON */
