/******************************************************************************
 * @file       xgui_demo_page_testmenu.c
 * @brief      XGuiWindowDemo「测试菜单」页（控制台 test 菜单树的 GUI 化）。
 * @details    页索引 22（第 18 扩展页，2026-10-29 收编；2026-10-29 二次
 *             整改：Shell 拆分至独立页 23，本页只留菜单树+输出区）。
 *             把控制台宿主的 `test` 交互菜单（XTestMenuTest_create 的
 *             「测试代码」树）搬进 GUI：子菜单进/返回上级，动作点击即
 *             运行——XPrintf 线程级输出重定向（XPrintf_outputPush）把
 *             测试打印捕获进缓冲，结束后经 XLabel 展示（测试内部自泵
 *             processEvents，运行中状态行可刷新）。
 *             [按需内存] 页面本身经扩展页懒分配（首显才建）；菜单树在
 *             页面构建时创建（常驻，量级=条目数×几十字节）；打印缓冲
 *             在首个动作运行时才分配（64KB 起、256KB 封顶），清屏即释放
 *             回系统。Shell 功能在独立页（xgui_demo_page_shell.c）。
 *             [横竖屏自适应] adapt 按页宽翻中栏方向：≥600px 宽屏
 *             「菜单 | 输出」左右双栏；<600px 竖屏上下单列（菜单在上、
 *             输出在下），控件全宽触控口径。
 *             autotest 口径：全程非阻塞——只断言装配（树非空、按钮已
 *             渲染、双栏方向与页宽一致）；不触发任何测试动作。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "CXinYueConfig.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XObject.h"
#include "XString.h"
#include "XCoreApplication.h"
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XBoxLayout.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XAction.h"
#include "XTestMenu.h"
#include "XTestMenuTest.h"
#include "xgui_demo_pages.h"

/*
 * 段宏守卫：菜单树需要 Test 目标链接（XTestMenuTest_create 与各域
 * builder 在 TEST_FILE，仅非嵌入式 demo 目标链入）+ 基础控件族。任一
 * 不满足时降级为契约桩（build 返回 NULL、autotest/adapt 空操作），
 * 主文件可无条件链接。
 */
#if !XINYUE_EMBEDDED && XWIDGET_ON && XLABEL_ON && XLAYOUT_ON && \
    XLAYOUT_BOX_ON && XSCROLLAREA_ON

/* ==================== 常量 ==================== */

#define TM_BTN_H           40    /**< 菜单按钮高（触控口径）。 */
#define TM_BTN_SPACING     4     /**< 菜单按钮行距（=容器布局 spacing）。 */
#define TM_HOST_MARGIN     4     /**< 菜单容器内容边距（=容器布局 margins）。 */
#define TM_WIDE_MIN_W      600   /**< 双栏门槛页宽（≥=横排，< =纵排）。 */
#define TM_BUF_INIT        (64u * 1024u)  /**< 输出缓冲初容量。 */
#define TM_BUF_MAX         (256u * 1024u) /**< 输出缓冲上限（超出丢增量）。 */
#define TM_PATH_MAX        256   /**< 面包屑路径缓冲。 */

/* ==================== 页面根控件子类 ====================
 * [横竖屏自适应 2026-10-29] 堆叠几何随 contentHost resize 自动重激活的
 * 路径不经过 adapt 契约漏斗（views 页同款结论）——翻档必须挂在根控件
 * 自身 resizeEvent 上兜住：初始定版、窗口拖拽、横竖屏旋转、页级滚动壳
 * 包裹引发的每一拍根宽变化都从此处过档。 */

XCLASS_DEFINE_BEGING(DemoTestMenuRootWidget)
XCLASS_DEFINE_EXTEND_END(DemoTestMenuRootWidget, XWidget)

/** @brief 页面根控件子类（m_base 必须是第一个成员）。 */
typedef struct DemoTestMenuRootWidget
{
    XWidget m_base; /**< 基类成员；必须是第一个。 */
} DemoTestMenuRootWidget;

static void tm_applyWidthTier(void);

/** @brief 根控件尺寸变化：按新根宽过双栏/单列档（宽 ≥600 双栏）。 */
static bool VDemoTestMenuRootWidget_resizeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void (*)(XWidget*, XEvent*))((XWidget*)self, event);
    tm_applyWidthTier();
    return false;
}

/** @brief 初始化 DemoTestMenuRootWidget 类虚函数表。 */
XVtable* DemoTestMenuRootWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DemoTestMenuRootWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VDemoTestMenuRootWidget_resizeEvent);
    return XVTABLE_DEFAULT;
}

/* ==================== 页面状态 ==================== */

/** @brief 菜单条目映射（每次渲染重建；按钮随 menuHost 整树退役）。 */
typedef struct {
    XPushButton* btn;     /**< 菜单按钮（点击查表分发）。 */
    XTestMenu*   menu;    /**< 非 NULL=子菜单条目（点击进入）。 */
    XAction*     action;  /**< 非 NULL=动作条目（点击运行）。 */
} TmItem;

/** @brief 页面单例状态（demo 页无析构钩子，随进程常驻，与同族页一致）。 */
static struct {
    XWidget*       pageScroll;    /**< 页宿主（滚动区，返回给堆叠的根）。 */
    XWidget*       root;          /**< 内容根（widgetResizable 接管几何）。 */
    XLabel         breadcrumb;    /**< 菜单路径行。 */
    XBoxLayout     col;           /**< 根纵布局（面包屑 + 中栏）。 */
    XBoxLayout     midRow;        /**< 中栏（宽=菜单|输出横排，窄=纵排）。 */
    XBoxLayout     hostLay;       /**< 菜单按钮容器布局（容器持久，见下）。 */
    XScrollArea*   menuScroll;    /**< 菜单滚动区。 */
    XWidget*       menuHost;      /**< 当前菜单按钮容器（渲染即重建）。 */
    XScrollArea*   outputScroll;  /**< 输出滚动区。 */
    XLabel         output;        /**< 输出区。 */
    char           path[TM_PATH_MAX]; /**< 面包屑路径（进入/返回时增删）。 */
    XTestMenu*     menuRoot;      /**< 测试菜单树（XTestMenuTest_create）。 */
    XTestMenu*     menuCur;       /**< 当前浏览的子菜单。 */
    TmItem         items[64];     /**< 当前层按钮映射。 */
    int            itemCount;     /**< 有效映射数。 */
    char*          buf;           /**< 打印捕获缓冲（首动作才分配）。 */
    size_t         bufLen;        /**< 已用字节。 */
    size_t         bufCap;        /**< 容量。 */
    bool           wide;          /**< 当前是否双栏（adapt 维护）。 */
    DemoPageStatusFn status;      /**< 主窗口状态栏回调（build 注入）。 */
    void*          statusUser;    /**< 上述回调上下文。 */
} s_tm;

/* ==================== 输出缓冲 ==================== */

/** @brief 追加带长文本；必要时扩容（256KB 上限，超出丢增量保头）。 */
static void tm_appendLen(const char* text, size_t len)
{
    if (!text || len == 0) return;
    if (!s_tm.buf) {
        s_tm.bufCap = TM_BUF_INIT;
        s_tm.buf = (char*)XMalloc_System(s_tm.bufCap);
        if (!s_tm.buf) return;
        s_tm.buf[0] = '\0';
        s_tm.bufLen = 0;
    }
    if (s_tm.bufLen + len + 1 > s_tm.bufCap) {
        size_t cap = s_tm.bufCap;
        char* grown;
        while (cap < s_tm.bufLen + len + 1 && cap < TM_BUF_MAX) cap *= 2u;
        if (cap >= s_tm.bufLen + len + 1) {
            grown = XRealloc_System(s_tm.buf, cap);
            if (grown) { s_tm.buf = grown; s_tm.bufCap = cap; }
        } else {
            return; /* 超 256KB 上限：丢增量（输出区已够复盘）。 */
        }
    }
    memcpy(s_tm.buf + s_tm.bufLen, text, len);
    s_tm.bufLen += len;
    s_tm.buf[s_tm.bufLen] = '\0';
    XLabel_setText_2(&s_tm.output, s_tm.buf);
}

/** @brief 追加 NUL 结尾文本。 */
static void tm_append(const char* text)
{
    if (text) tm_appendLen(text, strlen(text));
}

/** @brief XPrintf 重定向回调（测试动作运行期捕获打印，同线程）。 */
static int64_t tm_captureWrite(void* userData, const char* data,
                               int64_t length)
{
    (void)userData;
    if (data && length > 0) tm_appendLen(data, (size_t)length);
    return length;
}

/* ==================== 菜单渲染与分发 ==================== */

/** @brief 刷新面包屑（路径串由进入/返回分支维护，此处只回显）。 */
static void tm_updateBreadcrumb(void)
{
    XLabel_setText_2(&s_tm.breadcrumb,
                     s_tm.path[0] ? s_tm.path
                                  : "\xE6\xB5\x8B\xE8\xAF\x95\xE4\xBB\xA3\xE7\xA0\x81");
}

/** @brief 重建按钮容器并填当前层条目（返回上级/子菜单/动作）。 */
static void tm_rebuildHost(void);

/** @brief 面包屑路径追加一段。 */
static void tm_pathPush(const char* title)
{
    if (!title) return;
    if (s_tm.path[0])
        strncat(s_tm.path, " / ", TM_PATH_MAX - 1 - strlen(s_tm.path));
    strncat(s_tm.path, title, TM_PATH_MAX - 1 - strlen(s_tm.path));
}

/** @brief 面包屑路径弹出末段（返回上级）。 */
static void tm_pathPop(void)
{
    char* cut = strrchr(s_tm.path, '/');
    if (cut) {
        while (cut > s_tm.path && *(cut - 1) == ' ') --cut;
        *cut = '\0';
    } else {
        s_tm.path[0] = '\0';
    }
}

/** @brief 按钮点击统一分发：查映射，进子菜单或运行动作。 */
static void tm_buttonClicked(XObject* sender, XVarList* args)
{
    int i;
    (void)args;
    for (i = 0; i < s_tm.itemCount; ++i) {
        if ((XObject*)s_tm.items[i].btn != (XObject*)sender) continue;
        if (s_tm.items[i].menu) {
            XTestMenu* target = s_tm.items[i].menu;
            const char* title = XTestMenu_getTitle(target);
            /* 目标=当前层的父 ⇒ 返回（弹段）；否则前进（压段）。 */
            if (s_tm.menuCur &&
                XTreeNode_GetParent(s_tm.menuCur) == target)
                tm_pathPop();
            else
                tm_pathPush(title);
            s_tm.menuCur = target;
            tm_rebuildHost();
        } else if (s_tm.items[i].action) {
            XAction* action = s_tm.items[i].action;
            const XString* text = XAction_text_const(action);
            XPrintfOutputScope scope;
            tm_append("\n========== ");
            tm_append(text ? XString_toUtf8(text) : "?");
            tm_append(" ==========\n");
            if (s_tm.status)
                s_tm.status(s_tm.statusUser,
                            text ? XString_toUtf8(text) : "running");
            XPrintf_outputPush(&scope, tm_captureWrite, NULL);
            XAction_trigger(action);
            XPrintf_outputPop(&scope);
            tm_append("\n");
            if (s_tm.status) s_tm.status(s_tm.statusUser, "");
        }
        return;
    }
}

/** @brief 菜单按钮入容器布局（插在尾部可伸展空白之前）。
 *  @details [Qt 对齐 2026-10-30] 容器布局末尾恒有一条可伸展空白
 *           （XBoxLayout_addStretch(0)，对标 Qt QVBoxLayout::addStretch）：
 *           菜单容器被滚动区拉满高度时，纵向余量全部由尾部空白吸收，
 *           按钮保持自顶紧密排布。**不加**尾部空白时，Qt 的 qGeomCalc
 *           会把余量摊进条目间距（Qt 6.8 实测：412x552 容器内两枚
 *           40px 按钮落在 y=157/354；本框架同参数落点一致），按钮被
 *           拉散到容器上下两端——这正是第三层「两按钮看着叠在一处/
 *           实际隔了半个屏」的成因。加尾部空白后 Qt 落在 y=4/48
 *           （内容区坐标 (0,0)/(0,44)），与本框架一致。
 *           条目索引 = 条目数-1（空白恒为末位），保证按钮恒在空白前。 */
static void tm_addMenuButton(XPushButton* btn)
{
    int idx;
    if (!btn) return;
    idx = XLayout_count_base((XLayout*)&s_tm.hostLay) - 1;
    if (idx < 0) idx = 0;
    XBoxLayout_insertWidget(&s_tm.hostLay, idx, (XWidget*)btn, 0);
}

/** @brief 重建当前层条目（返回上级/子菜单/动作）。
 *  @details [点击安全 2026-10-29] 容器 menuHost 持久只建一次；旧按钮
 *           逐个「摘出布局 + deleteLater」——绝不走
 *           XScrollArea_setWidget 换容器：那会同步 XClassDelete 旧树，
 *           而旧树里可能正有本拍点击信号派发中的按钮（派发栈内
 *           free 派发中的对象 = 用户实测「一点击就中断」的根因）。
 *           deleteLater 让销毁推迟到本拍派发落地之后的清理点。 */
static void tm_rebuildHost(void)
{
    XVector* menus;
    const XVector* actions;
    int i;
    size_t n;
    char title[160];

    if (!s_tm.menuScroll || !s_tm.menuHost) return;
    /* 旧按钮延迟退役：removeWidget 摘掉包装条目（page_network 同款），
       按钮本体 deleteLater——点击派发中的按钮绝不同步释放。 */
    for (i = 0; i < s_tm.itemCount; ++i) {
        if (!s_tm.items[i].btn) continue;
        XLayout_removeWidget((XLayout*)&s_tm.hostLay,
                             (XWidget*)s_tm.items[i].btn);
        XObject_deleteLater((XObject*)s_tm.items[i].btn);
    }
    s_tm.itemCount = 0;

    /* 返回上级（根层无；路径段在点击分支增删，此处只读）。 */
    if (s_tm.menuCur && XTreeNode_GetParent(s_tm.menuCur)) {
        XPushButton* btn = XPushButton_create(s_tm.menuHost, 0);
        if (btn) {
            XPushButton_setText_2(btn,
                "<- \xE8\xBF\x94\xE5\x9B\x9E\xE4\xB8\x8A\xE7\xBA\xA7");
            XWidget_setFixedHeight((XWidget*)btn, TM_BTN_H);
            if (s_tm.itemCount < (int)(sizeof(s_tm.items) / sizeof(s_tm.items[0]))) {
                TmItem* item = &s_tm.items[s_tm.itemCount++];
                item->btn = btn;
                item->menu = (XTestMenu*)XTreeNode_GetParent(s_tm.menuCur);
                item->action = NULL;
                XObject_connect_2((XObject*)btn,
                                  XSignal(XAbstractButton_clicked_signal),
                                  tm_buttonClicked);
            }
            /* 框架约定：动态创建的控件须显式 show 才可见（对标 qrcode/
               advanced 等页：创建→配置→连接→XWidget_show）。
               漏 show 的症状=该层按钮整片空白（父已 show 过，
               showChildren 传播只发生在父首次 show 时）。 */
            XWidget_show((XWidget*)btn);
            tm_addMenuButton(btn);
        }
    }
    /* 子菜单。 */
    menus = XTestMenu_getMenus(s_tm.menuCur);
    if (menus) {
        n = XVector_size_base(menus);
        for (i = 0; i < (int)n; ++i) {
            XTestMenu* child = XVector_At_Base(menus, i, XTestMenu*);
            XPushButton* btn;
            if (!child) continue;
            btn = XPushButton_create(s_tm.menuHost, 0);
            if (!btn) continue;
            snprintf(title, sizeof(title), "%s", XTestMenu_getTitle(child));
            XPushButton_setText_2(btn, title);
            XWidget_setFixedHeight((XWidget*)btn, TM_BTN_H);
            if (s_tm.itemCount < (int)(sizeof(s_tm.items) / sizeof(s_tm.items[0]))) {
                TmItem* item = &s_tm.items[s_tm.itemCount++];
                item->btn = btn;
                item->menu = child;
                item->action = NULL;
                XObject_connect_2((XObject*)btn,
                                  XSignal(XAbstractButton_clicked_signal),
                                  tm_buttonClicked);
            }
            /* 框架约定：动态创建的控件须显式 show 才可见（对标 qrcode/
               advanced 等页：创建→配置→连接→XWidget_show）。
               漏 show 的症状=该层按钮整片空白（父已 show 过，
               showChildren 传播只发生在父首次 show 时）。 */
            XWidget_show((XWidget*)btn);
            tm_addMenuButton(btn);
        }
        XClassDelete(menus);
    }
    /* 动作（getActions 返回菜单内部向量的借用指针——绝不可释放；
       getMenus 返回新建副本，调用方拥有，读完释放）。 */
    actions = (const XVector*)XTestMenu_getActions(s_tm.menuCur);
    if (actions) {
        n = XVector_size_base(actions);
        for (i = 0; i < (int)n; ++i) {
            XAction* action = XVector_At_Base(actions, i, XAction*);
            const XString* text;
            XPushButton* btn;
            if (!action) continue;
            text = XAction_text_const(action);
            btn = XPushButton_create(s_tm.menuHost, 0);
            if (!btn) continue;
            snprintf(title, sizeof(title), "\xE2\x96\xB6 %s",
                     text ? XString_toUtf8(text) : "?");
            XPushButton_setText_2(btn, title);
            XWidget_setFixedHeight((XWidget*)btn, TM_BTN_H);
            if (s_tm.itemCount < (int)(sizeof(s_tm.items) / sizeof(s_tm.items[0]))) {
                TmItem* item = &s_tm.items[s_tm.itemCount++];
                item->btn = btn;
                item->menu = NULL;
                item->action = action;
                XObject_connect_2((XObject*)btn,
                                  XSignal(XAbstractButton_clicked_signal),
                                  tm_buttonClicked);
            }
            /* 框架约定：动态创建的控件须显式 show 才可见（对标 qrcode/
               advanced 等页：创建→配置→连接→XWidget_show）。
               漏 show 的症状=该层按钮整片空白（父已 show 过，
               showChildren 传播只发生在父首次 show 时）。 */
            XWidget_show((XWidget*)btn);
            tm_addMenuButton(btn);
        }
        /* actions 为借用指针（菜单内部向量），不释放。 */
    }
    tm_updateBreadcrumb();
}

/* ==================== 构建 / 自测 ==================== */

XWidget* demo_page_testmenu_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user)
{
    XScrollArea* pageScroll;

    memset(&s_tm, 0, sizeof(s_tm));
    s_tm.status = status;
    s_tm.statusUser = user;

    /* [页宿主=scroll 2026-10-29] 与 ATV61 页同口径：堆叠把 scroll 铺满，
     * 内容根挂 widgetResizable 由框架接管几何。 */
    pageScroll = XScrollArea_create(parent, 0);
    if (!pageScroll) return NULL;
    XScrollArea_setWidgetResizable(pageScroll, true);
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)pageScroll, XScrollBarPolicy_AsNeeded);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)pageScroll, XScrollBarPolicy_AlwaysOff);

    s_tm.root = (XWidget*)XMemory_malloc(sizeof(DemoTestMenuRootWidget),
                                         XCLASS_DEFAULT_MEMORY_TYPE);
    if (!s_tm.root) {
        XObject_deleteLater((XObject*)pageScroll);
        return NULL;
    }
    XMemset(s_tm.root, 0, sizeof(DemoTestMenuRootWidget));
    XWidget_init(s_tm.root, (XWidget*)pageScroll, 0);
    XClassSetVtable(s_tm.root, DemoTestMenuRootWidget);
    Set_Class_Memory(s_tm.root, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(s_tm.root, true);
    s_tm.pageScroll = (XWidget*)pageScroll;
    XScrollArea_setWidget(pageScroll, s_tm.root);

    /* 布局：根纵布局（面包屑 + 中栏）；中栏方向随横竖屏翻转——值成员
       布局（与 ATV61/update 页同款）。 */
    XBoxLayout_init(&s_tm.col, XBoxLayoutDirection_TopToBottom);
    XWidget_setLayout(s_tm.root, (XLayout*)&s_tm.col);
    XLayout_setContentsMargins((XLayout*)&s_tm.col, 8, 8, 8, 8);
    XLayout_setSpacing((XLayout*)&s_tm.col, 6);
    XBoxLayout_init(&s_tm.midRow, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_tm.midRow, 6);

    /* 面包屑行（恒踞顶部，横竖屏都通栏）。 */
    XLabel_init(&s_tm.breadcrumb, s_tm.root, 0);
    XLabel_setText_2(&s_tm.breadcrumb,
                     "\xE6\xB5\x8B\xE8\xAF\x95\xE4\xBB\xA3\xE7\xA0\x81");
    XWidget_setFixedHeight((XWidget*)&s_tm.breadcrumb, 24);
    XBoxLayout_addWidget(&s_tm.col, (XWidget*)&s_tm.breadcrumb);

    /* 菜单滚动区。 */
    s_tm.menuScroll = XScrollArea_create(s_tm.root, 0);
    if (!s_tm.menuScroll) goto fail;
    XScrollArea_setWidgetResizable(s_tm.menuScroll, true);
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)s_tm.menuScroll, XScrollBarPolicy_AsNeeded);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_tm.menuScroll, XScrollBarPolicy_AlwaysOff);
    XWidget_setMinimumHeight((XWidget*)s_tm.menuScroll, 160);
    XBoxLayout_addWidgetEx(&s_tm.midRow, (XWidget*)s_tm.menuScroll, 5, 0);

    /* 输出滚动区。 */
    s_tm.outputScroll = XScrollArea_create(s_tm.root, 0);
    if (!s_tm.outputScroll) goto fail;
    XScrollArea_setWidgetResizable(s_tm.outputScroll, true);
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)s_tm.outputScroll, XScrollBarPolicy_AsNeeded);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_tm.outputScroll, XScrollBarPolicy_AlwaysOff);
    XWidget_setMinimumHeight((XWidget*)s_tm.outputScroll, 140);
    {
        XWidget* host = XWidget_create(NULL, 0);
        XBoxLayout* hostLay;
        if (!host) goto fail;
        hostLay = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, host);
        if (!hostLay) { XObject_deleteLater((XObject*)host); goto fail; }
        XLayout_setContentsMargins((XLayout*)hostLay, 4, 4, 4, 4);
        XLabel_init(&s_tm.output, host, 0);
        XLabel_setText_2(&s_tm.output,
                         "\xE8\xBE\x93\xE5\x87\xBA\xE5\x8C\xBA");
        XBoxLayout_addWidget(hostLay, (XWidget*)&s_tm.output);
        XScrollArea_setWidget(s_tm.outputScroll, host);
    }
    XBoxLayout_addWidgetEx(&s_tm.midRow, (XWidget*)s_tm.outputScroll, 4, 0);
    /* 中栏挂根纵布局（吃余高）；方向由 tm_applyWidthTier 随根宽翻转。 */
    XBoxLayout_addLayoutEx(&s_tm.col, (XLayout*)&s_tm.midRow, 1);

    /* 菜单按钮容器（持久只建一次；翻层只换按钮，见 tm_rebuildHost）。 */
    s_tm.menuHost = XWidget_create(NULL, 0);
    if (!s_tm.menuHost) goto fail;
    XBoxLayout_init(&s_tm.hostLay, XBoxLayoutDirection_TopToBottom);
    XWidget_setLayout(s_tm.menuHost, (XLayout*)&s_tm.hostLay);
    XLayout_setContentsMargins((XLayout*)&s_tm.hostLay,
                               TM_HOST_MARGIN, TM_HOST_MARGIN,
                               TM_HOST_MARGIN, TM_HOST_MARGIN);
    XLayout_setSpacing((XLayout*)&s_tm.hostLay, TM_BTN_SPACING);
    /* [Qt 对齐 2026-10-30] 尾部可伸展空白（对标 Qt QVBoxLayout::addStretch(0)）：
       菜单容器被滚动区拉满高度时，纵向余量全部由尾部空白吸收——按钮
       保持自顶紧密排布（内容区坐标 (0,0)/(0,44)）。不加空白时余量会被
       qGeomCalc 摊进条目间距，按钮被拉散（Qt 6.8 实测 y=157/354，
       本框架同参数一致）——见 tm_addMenuButton 注释。 */
    XBoxLayout_addStretch(&s_tm.hostLay, 0);
    XScrollArea_setWidget(s_tm.menuScroll, s_tm.menuHost);

    /* 菜单树：控制台「测试代码」全量树（TEST_FILE 随 demo 目标链接）。 */
    s_tm.menuRoot = XTestMenuTest_create();
    s_tm.menuCur = s_tm.menuRoot;
    if (!s_tm.menuRoot) {
        XLabel_setText_2(&s_tm.breadcrumb,
            "\xE6\x9C\xAC\xE6\x9E\x84\xE5\xBB\xBA\xE6\x9C\xAA\xE9\x93\xBE"
            "\xE6\x8E\xA5\xE6\xB5\x8B\xE8\xAF\x95\xE8\x8F\x9C\xE5\x8D\x95"
            "\xE6\xA0\x91");
    } else {
        s_tm.wide = XWidget_width(s_tm.root) >= TM_WIDE_MIN_W;
        XBoxLayout_setDirection(&s_tm.midRow,
                                s_tm.wide ? XBoxLayoutDirection_LeftToRight
                                          : XBoxLayoutDirection_TopToBottom);
        tm_rebuildHost();
    }
    return (XWidget*)pageScroll;

fail:
    if (s_tm.root) XObject_deleteLater((XObject*)s_tm.root);
    return NULL;
}

int demo_page_testmenu_autotest(XWidget* page)
{
    int fails = 0;
    XTestMenu* back;
    if (!page) return -1;
    if (!s_tm.root || !s_tm.pageScroll) ++fails;
    if (page != s_tm.pageScroll) ++fails;
    if (!s_tm.menuScroll || !s_tm.outputScroll) ++fails;
    if (!s_tm.menuRoot || !s_tm.menuCur) ++fails;
    if (s_tm.itemCount <= 0) ++fails;    /* 根层子菜单按钮已渲染。 */
    /* 双栏方向与页宽口径一致（横竖屏适配锁）。 */
    if ((XWidget_width(s_tm.root) >= TM_WIDE_MIN_W) != s_tm.wide) ++fails;

    /* [点击链路回归锁 2026-10-29] 编程式 click 直击信号→槽链路：
       进子菜单 → 返回上级。历史缺陷回归锁——翻层曾用
       XScrollArea_setWidget 换容器，其同步 XClassDelete 旧树会在
       点击派发栈内释放派发中的按钮（桌面实测一点击即中断）；
       现行「摘出布局 + deleteLater」必须活着穿过本用例。 */
    if (s_tm.itemCount > 0 && s_tm.items[0].btn) {
        XAbstractButton_click((XAbstractButton*)s_tm.items[0].btn);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (s_tm.menuCur == s_tm.menuRoot) ++fails;  /* 已进第一子层 */
        if (!s_tm.path[0]) ++fails;                  /* 面包屑已压段 */
        if (s_tm.itemCount <= 0) ++fails;            /* 子层按钮已渲染 */
    } else {
        ++fails;
    }
    /* [几何回归锁 2026-10-30] 子层按钮必须自顶紧密排布：首枚按钮落在
       容器内容区原点（= 容器边距），相邻两枚的行距 = 按钮高 + 行距。
       容器被滚动区拉满高度时，纵向余量须由容器布局**尾部可伸展空白**
       吸收；若余量被摊进条目间距（qGeomCalc 的 extraspace 分摊），按钮
       会被拉散到容器上下两端（Qt 6.8 实测：无尾部空白 y=157/354，
       有尾部空白 y=4/48）——即交接单 §4 记的「几何叠/散」症状。
       注意取位用 XWidget_geometry（父坐标）；XWidget_rect 是内容区
       矩形（对标 QWidget::rect()，原点恒为自身左上角），读不到位置。 */
    if (s_tm.itemCount > 1 && s_tm.items[0].btn && s_tm.items[1].btn) {
        XRect g0 = XWidget_geometry((XWidget*)s_tm.items[0].btn);
        XRect g1 = XWidget_geometry((XWidget*)s_tm.items[1].btn);
        if (g0.y != TM_HOST_MARGIN) ++fails;                /* 首枚贴顶 */
        if (g1.y - g0.y != TM_BTN_H + TM_BTN_SPACING) ++fails; /* 相邻 */
        if (g0.width <= 0 || g1.width <= 0) ++fails;         /* 已铺开 */
    }
    /* [动作运行回归锁 2026-10-30] 沿子菜单下行到首个**叶动作**并真实
       触发到结束（用户实测「测试菜单每运行一个项目结束即崩」——此前
       回归锁只覆盖导航，动作运行+输出捕获路径零覆盖）。判据：动作
       触发后进程存活 + 输出缓冲增长（捕获链路活着）。层深限 4。 */
    {
        int depth;
        size_t bufBefore = s_tm.bufLen;
        int ran = 0;
        XTestMenu* startLayer = s_tm.menuCur; /* 锁起始层（原流程的「库」） */
        for (depth = 0; depth < 4 && !ran; ++depth) {
            int k;
            int sub = -1;
            /* 每层重算父节点：返回钮的 menu=本层之父，随层变化。 */
            XTestMenu* parentMenu =
                (XTestMenu*)XTreeNode_GetParent(s_tm.menuCur);
            fprintf(stderr, "[TMLOCK] depth=%d layer='%s' items=%d buf=%zu\n",
                    depth, XTestMenu_getTitle(s_tm.menuCur) ?
                    XTestMenu_getTitle(s_tm.menuCur) : "?",
                    s_tm.itemCount, s_tm.bufLen);
            fflush(stderr);
            for (k = 0; k < s_tm.itemCount; ++k) {
                if (s_tm.items[k].action) {
                    size_t outBefore = s_tm.bufLen;
                    const XString* at = XAction_text_const(s_tm.items[k].action);
                    fprintf(stderr, "[TMLOCK] run action='%s'\n",
                            at ? XString_toUtf8(at) : "?");
                    fflush(stderr);
                    XAbstractButton_click((XAbstractButton*)s_tm.items[k].btn);
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    fprintf(stderr, "[TMLOCK] done buf %zu -> %zu\n",
                            outBefore, s_tm.bufLen);
                    fflush(stderr);
                    if (s_tm.bufLen <= outBefore) ++fails; /* 捕获零增长 */
                    ran = 1;
                    break;
                }
                /* 首个「非返回钮」的子菜单（返回钮的 menu=父节点）。 */
                if (s_tm.items[k].menu && s_tm.items[k].menu != parentMenu &&
                    sub < 0)
                    sub = k;
            }
            if (!ran && sub >= 0) {
                XAbstractButton_click((XAbstractButton*)s_tm.items[sub].btn);
                XCoreApplication_processEvents(XEventLoop_AllEvents);
            } else if (!ran) {
                break; /* 本层无动作无子菜单（仅返回钮）：树到头。 */
            }
        }
        if (!ran) ++fails; /* 未找到任何可运行动作 */
        if (s_tm.bufLen == bufBefore && ran) ++fails;
        /* 回锁起始层（原流程后续「返回上级」用例期望停在本层），不污染
           后续用例状态。 */
        {
            int k;
            for (k = 0; k < 4 && s_tm.menuCur != startLayer; ++k) {
                if (s_tm.itemCount > 0 && s_tm.items[0].btn &&
                    s_tm.items[0].menu) {
                    XAbstractButton_click((XAbstractButton*)s_tm.items[0].btn);
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                }
            }
        }
    }
    /* 返回上级（子层 items[0] 恒为返回钮）。 */
    back = (XTestMenu*)XTreeNode_GetParent(s_tm.menuCur);
    if (s_tm.itemCount > 0 && s_tm.items[0].btn &&
        s_tm.items[0].menu == back) {
        XAbstractButton_click((XAbstractButton*)s_tm.items[0].btn);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (s_tm.menuCur != s_tm.menuRoot) ++fails;  /* 已回根层 */
        if (s_tm.path[0]) ++fails;                   /* 面包屑已弹段 */
    } else {
        ++fails;
    }
    return fails;
}

/** @brief 宽/单列分档统一入口（幂等）：根宽 ≥600 双栏（菜单|输出横排），
 *         <600 单列竖排。翻档后显式 invalidate+activate 让新装配本拍落
 *         几何（根 resizeEvent 派发于根布局 activate 之前，同 views 页
 *         纪律）；adapt 契约漏斗与根 resizeEvent 双入口共用（重复无害）。 */
static void tm_applyWidthTier(void)
{
    bool wide;
    if (!s_tm.root) return;
    wide = XWidget_width(s_tm.root) > 0 &&
           XWidget_width(s_tm.root) >= TM_WIDE_MIN_W;
    if (wide == s_tm.wide) return;
    s_tm.wide = wide;
    XBoxLayout_setDirection(&s_tm.midRow,
                            wide ? XBoxLayoutDirection_LeftToRight
                                 : XBoxLayoutDirection_TopToBottom);
    XLayout_invalidate((XLayout*)&s_tm.midRow);
    XLayout_activate((XLayout*)&s_tm.midRow);
}

/** @brief adapt 契约入口（漏斗路径；根 resizeEvent 为另一兜底入口）。 */
void demo_page_testmenu_adapt(XWidget* page)
{
    if (!page || page != (XWidget*)s_tm.pageScroll) return;
    tm_applyWidthTier();
}

#else /* 裁剪桩 */

XWidget* demo_page_testmenu_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    return NULL;
}

int demo_page_testmenu_autotest(XWidget* page)
{
    (void)page;
    return -1;
}

void demo_page_testmenu_adapt(XWidget* page)
{
    (void)page;
}

#endif /* !XINYUE_EMBEDDED && 控件齐 */
