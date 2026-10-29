/******************************************************************************
 * @file       xgui_demo_page_dialogs.c
 * @brief      XGuiWindowDemo 扩展页：对话框页（对标 Qt 6.8 的
 *             QMessageBox/QInputDialog/QFileDialog/QColorDialog/
 *             QProgressDialog/QDialog+QDialogButtonBox 示例）。
 * @details    实现 xgui_demo_pages.h 契约的 demo_page_dialogs_build /
 *             demo_page_dialogs_autotest：
 *             - 页面内容区约 812x520，页内装配走布局容器（2026-10-17
 *               容器化）：内容根挂垂直盒布局器统一分配几何，子控件
 *               逐一 XWidget_show；
 *             - 一列触发按钮（每个对话框一行）+ 每行说明标签（行盒=
 *               水平盒：触发按钮固定 150x34 + 说明标签定高 34）；
 *             - 宽度分档（dlgpg_scrollRelayout 单一收口）：宽根维持
 *               上述桌面口径；窄根（竖屏手机口径）翻窄根单列档——行
 *               盒转纵向（按钮在上、说明满宽换行在下），按钮抬到触控
 *               目标下限 40，说明列解除单行定高按换行实测定高，内容
 *               自然高同步重算，触达交由纵向滚动（横滚根除）；
 *             - 消息框四键走【非阻塞】实例路径：XMessageBox 堆构造 +
 *               XDialog_open()（对标 QDialog::open：窗口模态显示但不
 *               进入本地事件循环），按钮盒 accepted/rejected → 
 *               XDialog_accept/reject 中继，结果回传状态栏回调；
 *             - 输入/文件/颜色对话框：库内便捷函数（getText_2/
 *               getOpenFileName_2/getColor_2）有应用实例时为【模态
 *               exec】阻塞执行（实例本身为纯属性袋，无内嵌 UI），
 *               demo 照常接线供真人交互，并在说明标签注明
 *               "（模态，autotest 不覆盖）"；
 *             - 进度对话框走【非阻塞】打开 + 每次点击推进 10%（达
 *               最大值按 Qt 语义 autoReset/autoClose 复位隐藏），并
 *               内嵌"取消"按钮演示 cancel() → canceled() 信号链；
 *             - 自定义对话框 = XDialog 堆对象 + XLabel + 
 *               XDialogButtonBox（确定/取消标准按钮），非阻塞打开；
 *             - autotest 全程非阻塞（严禁 exec()/模态等待）：堆构造
 *               → 初始状态 getter 断言 → 直调 accept()/reject() →
 *               结果码/信号计数断言 → delete_base 释放（防泄漏）。
 * @note       所有权：页面根控件为 parent 下的堆对象，随父子链级联
 *             析构（主文件不单独释放）；常驻对话框（消息框×4/进度/
 *             自定义）惰性创建、以页面根为父，随页面级联释放；
 *             autotest 内临时堆对话框即测即毁；getter 返回的
 *             XString/XStringList 副本按头文件注释由调用方释放。
 * @author     XinYueC 团队
 ******************************************************************************/

#include <stdio.h>
#include "XObject_Protected.h"
#include <string.h>

#include "XGuiConfig.h"
#include "XObject.h"
#include "XPrintf.h"
#include "XString.h"
#include "XStringList.h"
#include "XColor.h"
#include "XWidget.h"
#include "XBoxLayout.h" /* 页内布局容器化：内容根垂直盒 + 行盒复刻行距。 */
#include "XLabel.h"
#include "XPushButton.h"
#include "XDialog.h"
#include "XDialogButtonBox.h"
#include "XMessageBox.h"
#include "XInputDialog.h"
#include "XFileDialog.h"
#include "XColorDialog.h"
#include "XProgressDialog.h"
#include "XScrollArea.h"
#include "XPainter.h"
#include "xgui_demo_pages.h"

/* 整文件可能全空时保证非空翻译单元的哨兵。 */
typedef int xgui_demo_page_dialogs_nonempty_t;

#if XWIDGET_ON

/* ==================== 模块化裁剪开关（组合自 XGuiConfig.h） ==================== */

#define DLGPG_BUTTONS_ON   (XPUSHBUTTON_ON)
#define DLGPG_LABELS_ON    (XFRAME_ON && XLABEL_ON)
/* 消息框（实例路径依赖按钮盒 + 标签 + 按钮）。 */
#define DLGPG_MSGBOX_ON    (XDIALOG_ON && XDIALOGBUTTONBOX_ON && \
                            XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON)
/* 输入/文件/颜色对话框（模块总开关均为 XDIALOG_ON）。 */
#define DLGPG_INPUT_ON     (XDIALOG_ON)
#define DLGPG_FILE_ON      (XDIALOG_ON)
#define DLGPG_COLOR_ON     (XDIALOG_ON)
/* 进度对话框（demo 内嵌取消按钮与百分比标签）。 */
#define DLGPG_PROGRESS_ON  (XDIALOG_ON && XPUSHBUTTON_ON && XLABEL_ON)
/* 自定义对话框（XDialog + XLabel + XDialogButtonBox）。 */
#define DLGPG_CUSTOM_ON    (XDIALOG_ON && XDIALOGBUTTONBOX_ON && \
                            XPUSHBUTTON_ON && XLABEL_ON)

/* ==================== 页面几何常量（内容区约 812x520） ==================== */

/* [页宽根修 2026-10-11] 760→812：FT 直读字体（Noto 系）字形宽于设计期
 * 旧内置字库，最长说明文案实测 396（含度量余量）——176（说明列 X）+
 * 396 = 572，旧页宽 760 经 HUD 让位（-224）后 content 仅 536，说明列
 * 尾字被滚动区右缘裁半（「互」字实测）。812-224=588 ≥ 572+16 余量；
 * 窄根（<DLGPG_HUD_YIELD_MIN_ROOT）不做横滚兜底，改窄根单列档整列
 * 重排（见 dlgpg_scrollRelayout 窄根档注）——横滚默认首屏说明列尽失
 * （真机 400x800 实证 rootW=376 时 contentW=812）。 */
#define DLGPG_PAGE_WIDTH   812
#define DLGPG_PAGE_HEIGHT  480
/* 触发按钮行数。必须与下方 dlgpg_addRow 调用次数严格一致（当前 10：
   消息框×4+输入/文件/颜色/进度/自定义/目录）——此值小一处，
   m_btn[10]/m_note[10] 末位越界写将砸中紧随其后的 m_msgInfo 等对话框
   槽位（实测：m_note[9] 越界把第 9 行说明标签指针写进消息框-信息槽，
   open 打在标签上＝信息框永不可见+错位残影+Esc 占死模态位）。
   （远端同位置备注：此值小一处将砸中相邻对话框槽位——新增行时同步
   更新本值。） */
#define DLGPG_ROW_COUNT    10
#define DLGPG_ROW_HEIGHT   50   /* 行距（原手写口径，行盒底边距由此推）。 */
#define DLGPG_BTN_WIDTH    150
/* 触发按钮定高（宽根档口径，复刻桌面观感）；窄根档抬到
   DLGPG_BTN_TOUCH_HEIGHT（触控目标下限），见 dlgpg_scrollRelayout。 */
#define DLGPG_BTN_HEIGHT   34
#define DLGPG_NOTE_WIDTH   568  /* 说明列基准宽（relayout 按根宽/实测收放）。 */
/* [布局容器化 2026-10-17] 原点/列位手写坐标（BTN_X=16/NOTE_X=176）退场：
   改由布局器边距与间距表达——内容根垂直盒边距宏化于下（窄根单列档
   反推说明列宽/内容自然高也用它们，改值两处同步）：边距 (16,14,16,22)
   复刻原首行原点 (16,14) 与末行底余量（内容高 520-末行底 498）；行盒
   间距 10 复刻原说明列 x 偏移（16+150→176）；行盒底边距 16=行距-行高。 */
#define DLGPG_MARGIN_L     16
#define DLGPG_MARGIN_T     14
#define DLGPG_MARGIN_R     16
#define DLGPG_MARGIN_B     22
#define DLGPG_ROW_SPACING  10
#define DLGPG_ROW_GAP      (DLGPG_ROW_HEIGHT - DLGPG_BTN_HEIGHT)
/* 右缘 HUD 让位带（FPS 浮层自由拖动前的静态避让）：主窗右下角半透明
   FPS 性能浮层（XPerformanceOverlay 210x70+autoFit，见主文件
   demo_performance_init）压住滚动内容行尾文字（目验挂项）。根宽足够
   （>=DLGPG_HUD_YIELD_MIN_ROOT）时内容与说明列右缘收进浮层左缘内
   （带宽 224=浮层 210 + 14 呼吸，与 page12 图表让位 218 同口径）；
   根过窄不收——窄根浮层同样盖但行可滚动触达（收窄会挤压主列可读
   宽，得不偿失）。 */
#define DLGPG_HUD_YIELD_WIDTH   224
#define DLGPG_HUD_YIELD_MIN_ROOT 640
/* [窄根单列档 2026-10-25] 触控目标下限（手机口径 ~40 逻辑像素，低于
   此值指尖难点中）：窄根档触发按钮定高抬到此值，行内说明列随之改
   满宽换行（单行 34 定高在窄根下把文案推出视口右缘）。 */
#define DLGPG_BTN_TOUCH_HEIGHT  40
/* 窄根档内容宽让出的纵向滚动条带（与 views 页大表格可视宽让位 12 同
   口径）：内容宽=根宽-此带，纵条一出现也不致横溢出横向条。 */
#define DLGPG_VSCROLL_BAND      12

/* ==================== 页面内部控件登记表（demo 单实例） ==================== */

/* 滚动区子类：仅重挂钩 resize/show 事件槽实现右缘 HUD 让位带联动
   （子类化定式与 views/主文件一致：m_base 首成员 + 类虚表）。 */
#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
    XSCROLLAREA_ON
XCLASS_DEFINE_BEGING(DlgPgScroll)
XCLASS_DEFINE_EXTEND_END(DlgPgScroll, XScrollArea)

typedef struct DlgPgScroll
{
    XScrollArea m_base;            /**< 基类成员；必须是第一个。 */
} DlgPgScroll;
#endif

typedef struct DlgPgUi
{
    XWidget* m_root;               /* 页面根控件（堆对象，build 登记）。 */
    XWidget* m_scroll;             /* 页面滚动区（栈布局持有，返回值）。 */
    DemoPageStatusFn m_status;     /* 状态栏回调（user 为主窗口指针）。 */
    void* m_statusUser;

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 页内布局容器（2026-10-17 容器化）：布局器须随页常驻
       （XWidget_setLayout 仅存借用指针不取所有权），存入本静态登记
       表；行盒经 XBoxLayout_addLayout 借挂根盒（同样不取所有权）。 */
    XBoxLayout m_pageLayout;                  /* 内容根垂直盒（挂 m_root）。 */
    XBoxLayout m_rowLayouts[DLGPG_ROW_COUNT]; /* 行水平盒池（按钮+说明）。 */
#endif

#if DLGPG_BUTTONS_ON
    XPushButton* m_btn[DLGPG_ROW_COUNT]; /* 触发按钮列。 */
#endif
#if DLGPG_LABELS_ON
    XLabel* m_note[DLGPG_ROW_COUNT];     /* 每行说明标签（模态/非阻塞口径）。 */
    int m_noteTextW;                     /* 最长说明文案实测宽度+余量（字体度量自适应列宽）。 */
#endif

#if DLGPG_MSGBOX_ON
    XMessageBox* m_msgInfo;        /* 消息框-信息（惰性创建，常驻复用）。 */
    XMessageBox* m_msgWarn;        /* 消息框-警告。 */
    XMessageBox* m_msgError;       /* 消息框-错误。 */
    XMessageBox* m_msgAsk;         /* 消息框-询问。 */
#endif
#if DLGPG_PROGRESS_ON
    XProgressDialog* m_progress;   /* 进度对话框（常驻复用）。 */
    XLabel* m_progressLabel;       /* 百分比标签（对话框子控件）。 */
    XPushButton* m_progressCancel; /* 内嵌取消按钮（对话框子控件）。 */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 进度对话框内容布局器（惰性 ensure 时初始化，随登记表常驻）。 */
    XBoxLayout m_progressLayout;   /* 对话框内容垂直盒（挂 m_progress）。 */
    XBoxLayout m_progressActions;  /* 取消按钮行水平盒（右对齐）。 */
#endif
#endif
#if DLGPG_CUSTOM_ON
    XDialog* m_custom;             /* 自定义对话框（常驻复用）。 */
    XLabel* m_customLabel;         /* 自定义对话框提示标签。 */
    XDialogButtonBox* m_customBox; /* 自定义对话框按钮盒。 */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout m_customLayout;     /* 对话框内容垂直盒（挂 m_custom）。 */
#endif
#endif

    /* autotest 信号计数（Direct 连接同步递增）。 */
    int m_acceptedCount;
    int m_rejectedCount;
    int m_intChangedCount;
    int m_colorChangedCount;
    int m_canceledCount;
} DlgPgUi;

static DlgPgUi s_dlgpg;

/* ==================== 内部辅助 ==================== */

/** @brief 经主窗口回调向状态栏反馈交互结果。 */
static void dlgpg_status(const char* text)
{
    if (s_dlgpg.m_status)
        s_dlgpg.m_status(s_dlgpg.m_statusUser, text);
}

/* ==================== 滚动区子类：右缘 HUD 让位带联动 ==================== */

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
    XSCROLLAREA_ON
/* 页面根=滚动区，几何由主文件 demo_layout_content 的堆叠布局统一分配
   （本文件 build 时窗口尺寸尚未定版，无法一次收口）；子类在
   resizeEvent/showEvent 收口档位重排：按根宽决定是否让位——收口内容
   定尺与说明列宽（行尾文字被控件矩形裁剪），宽/高约束落定后经
   XScrollArea_updateWidgetGeometry 触发框架重排版（widgetResizable
   内容几何与滚动范围归框架，XScrollArea.c xsa_updateWidgetGeometry）。
   XWidget_paintTree 对子控件逐级按自身矩形裁剪（XWidget.c:7146），
   收窄即截断行尾文字。 */

/** @brief 重排内容定尺/说明列宽与行盒方向（宽度分档判断的唯一入口）。
 *  @details [布局容器化 2026-10-17] 原说明列逐行 setGeometry 手写排布
 *           退场：行几何由内容根垂直盒布局器分配（行盒=按钮+说明），
 *           本函数只收口两处定尺——内容宽（HUD 让位档）与说明列宽
 *           （档位/实测取大），均经 setFixedWidth 表达（min/max 收口
 *           即时触发控件 resize，进而自动重激活布局器，见
 *           XWidget_recomputeGeometry 的 resize→activate 联动）。
 *           [窄根单列档 2026-10-25] 窄根（<DLGPG_HUD_YIELD_MIN_ROOT，
 *           竖屏手机口径）不再维持 812 定宽横滚——横滚下默认首屏只见
 *           按钮列，说明列整体在视口右缘外（真机 400x800 实证
 *           rootW=376 时 contentW=812/measured=396，说明文案首屏尽
 *           失）。窄根改单列重排：内容宽收进根宽（扣纵条带，横滚根
 *           除），行盒翻纵向（按钮在上、说明满宽换行在下），说明列解
 *           除单行定高按换行实测定高（XLabel_heightForWidth），触发
 *           按钮抬到触控目标下限，内容自然高按各件实高重算通告滚动
 *           范围——触达交纵向滚动。宽根档维持桌面口径（旋转回横屏整
 *           列还原，单行 34/水平行盒原样）。 */
static void dlgpg_scrollRelayout(XScrollArea* scroll)
{
    XWidget* content;
    int rootW;
    int contentW;
    int contentH;
    int btnH;
    bool compact;
#if DLGPG_LABELS_ON
    int noteW;
#endif
    if (!scroll || !s_dlgpg.m_root)
        return;
    content = s_dlgpg.m_root;
    rootW = XWidget_width((XWidget*)scroll);
    /* 宽度分档：窄根（竖屏手机）单列重排收进根宽；宽根让位 HUD 浮层
       （右缘收进浮层左缘内，见 DLGPG_HUD_YIELD_WIDTH 注）。 */
    compact = rootW < DLGPG_HUD_YIELD_MIN_ROOT;
    if (compact)
        contentW = rootW - DLGPG_VSCROLL_BAND;
    else
        contentW = DLGPG_PAGE_WIDTH - DLGPG_HUD_YIELD_WIDTH;
    if (contentW < 1)
        contentW = 1;
    btnH = compact ? DLGPG_BTN_TOUCH_HEIGHT : DLGPG_BTN_HEIGHT;
#if DLGPG_BUTTONS_ON
    /* 触发按钮定高随档位：窄根抬到触控目标下限（手机指尖口径，34 难
       点中），宽根回 34 复刻桌面观感。 */
    {
        int i;
        for (i = 0; i < DLGPG_ROW_COUNT; ++i) {
            if (s_dlgpg.m_btn[i])
                XWidget_setFixedHeight((XWidget*)s_dlgpg.m_btn[i], btnH);
        }
    }
#endif
#if DLGPG_LABELS_ON
    /* 说明列分档：窄根满宽换行（内容宽扣根盒左右边距；列宽不再实测
       加宽——横滚已根除，实测口径只服务宽根单行模式）。宽根缩至让位
       右缘；字体度量自适应：ft2 直读字体（Noto）字形宽于旧内置字库，
       常量列宽会截断文案（真屏对话框页实证）——实测宽度超出时按实
       测加宽（上限=页右缘），文本完整性优先于 HUD 让位收窄。先定列
       宽再收内容宽：内容 resize 自动重激活布局器时即按最终列宽分配。 */
    if (compact) {
        noteW = contentW - (DLGPG_MARGIN_L + DLGPG_MARGIN_R);
    } else {
        noteW = DLGPG_NOTE_WIDTH;
        if (rootW >= DLGPG_HUD_YIELD_MIN_ROOT)
            noteW = DLGPG_NOTE_WIDTH - DLGPG_HUD_YIELD_WIDTH;
        if (s_dlgpg.m_noteTextW > noteW) {
            noteW = s_dlgpg.m_noteTextW;
            if (noteW > DLGPG_PAGE_WIDTH - (DLGPG_MARGIN_L +
                                            DLGPG_BTN_WIDTH +
                                            DLGPG_ROW_SPACING))
                noteW = DLGPG_PAGE_WIDTH - (DLGPG_MARGIN_L +
                                            DLGPG_BTN_WIDTH +
                                            DLGPG_ROW_SPACING);
        }
    }
    if (noteW < 1)
        noteW = 1;
    XPrintf("[dlgpg] rootW=%d contentW=%d measured=%d noteW=%d\n",
            rootW, contentW, s_dlgpg.m_noteTextW, noteW);
    {
        int i;
        for (i = 0; i < DLGPG_ROW_COUNT; ++i) {
            if (!s_dlgpg.m_note[i])
                continue;
            /* 换行档位随根切换：窄根开自动断行并按断行实测定高——换行
               后单行 34 定高会把后续行裁进下个条目带（定高须与断行同
               源）；宽根关回单行定高 34 复刻桌面。先定宽后取实测，实
               测即最终分配宽下的行数（列宽=行盒内宽，同源无偏差）。 */
            XLabel_setWordWrap(s_dlgpg.m_note[i], compact);
            XWidget_setFixedWidth((XWidget*)s_dlgpg.m_note[i], noteW);
            XWidget_setFixedHeight(
                (XWidget*)s_dlgpg.m_note[i],
                compact ? XLabel_heightForWidth(s_dlgpg.m_note[i], noteW)
                        : DLGPG_BTN_HEIGHT);
        }
    }
#endif
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 行盒方向随档位翻转（setDirection 幂等，内部失效布局）：窄根纵
       向单列（按钮在上、说明在下，件间距复用行盒 spacing），宽根水
       平复刻桌面。 */
    {
        int i;
        for (i = 0; i < DLGPG_ROW_COUNT; ++i) {
            XBoxLayout_setDirection(
                &s_dlgpg.m_rowLayouts[i],
                compact ? XBoxLayoutDirection_TopToBottom
                        : XBoxLayoutDirection_LeftToRight);
        }
    }
#endif
    /* 内容自然高分档：宽根恒为行区定尺（build 口径 520）；窄根按单列
       档各件实高累加（按钮触控高 + 行盒间距 + 说明断行实测高 + 行距
       DLGPG_ROW_GAP，末行免行距），根盒上下边距与 build 同口径——与
       布局器解算同源，行区恰满不裁尾行。 */
    if (compact) {
        int i;
        contentH = DLGPG_MARGIN_T + DLGPG_MARGIN_B;
        for (i = 0; i < DLGPG_ROW_COUNT; ++i) {
            contentH += btnH;
#if DLGPG_LABELS_ON
            if (s_dlgpg.m_note[i])
                contentH += DLGPG_ROW_SPACING +
                            XLabel_heightForWidth(s_dlgpg.m_note[i], noteW);
#endif
            if (i != DLGPG_ROW_COUNT - 1)
                contentH += DLGPG_ROW_GAP;
        }
    } else {
        contentH = DLGPG_PAGE_HEIGHT + 40;
    }
    XWidget_setFixedWidth(content, contentW);
    XWidget_setFixedHeight(content, contentH);
    /* [滚动壳下沉 2026-10-29] 内容几何与滚动范围归框架：上面落定的
       定尺约束在框架重排版里经内容 min/max 钳口生效（历史注：旧框架
       滚动区从不 resize 内容控件、约束「归布局不改几何」，须本页显式
       XWidget_resize+activate+setContentSize 收口——widgetResizable
       路径接管后由 XScrollArea_updateWidgetGeometry 统一收口，行盒
       激活在其 setGeometry→内容 resize 链内完成）。 */
    XScrollArea_updateWidgetGeometry(scroll);
}

/** @brief resizeEvent：先由基类重排视口/滚动条，再按新根宽让位收口。 */
static void VDlgPg_scrollResizeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    dlgpg_scrollRelayout((XScrollArea*)self);
}

/** @brief showEvent：对标 XScrollArea 内部「show 时补排版」语义——
 *  resize 派发先于最终定版时（隐藏页面切换瞬间），显示瞬间再按最终
 *  几何收口一次（与基类 VX_scrollArea_showEvent 同口径）。 */
static void VDlgPg_scrollShowEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XScrollArea, EXWidget_ShowEvent,
                  XWidgetEventSlot)(self, event);
    dlgpg_scrollRelayout((XScrollArea*)self);
}

/** @brief 初始化滚动区子类虚函数表（仅挂钩 resize/show 事件槽）。 */
XVtable* DlgPgScroll_class_init(void)
{
    XVTABLE_INIT_DEFAULT(DlgPgScroll)
    XVTABLE_INHERIT_XCLASS(XScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VDlgPg_scrollResizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VDlgPg_scrollShowEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 堆构造滚动区子类（内存口径与 XScrollArea_create 一致）。 */
static DlgPgScroll* dlgpg_scrollCreate(XWidget* parent)
{
    DlgPgScroll* scroll =
        (DlgPgScroll*)XMemory_malloc(sizeof(DlgPgScroll),
                                     XCLASS_DEFAULT_MEMORY_TYPE);
    if (!scroll)
        return NULL;
    XScrollArea_init(&scroll->m_base, parent, 0);
    Set_Class_Memory(scroll, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(scroll, true);
    XClassSetVtable(scroll, DlgPgScroll);
    return scroll;
}
#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && ... XSCROLLAREA_ON */

/* ==================== 信号槽（签名 void f(XObject*, XVarList*)） ==================== */

/* ---- 通用中继：按钮盒 accepted/rejected → 对话框 accept()/reject()
 *      （对标 connect(buttonBox, &QDialogButtonBox::accepted,
 *                    dialog,  &QDialog::accept) 惯用法）。 ---- */
#if DLGPG_MSGBOX_ON || DLGPG_CUSTOM_ON
static void dlgpg_boxAcceptSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XDialog_accept((XDialog*)receiver);
}

static void dlgpg_boxRejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XDialog_reject((XDialog*)receiver);
}
#endif /* DLGPG_MSGBOX_ON || DLGPG_CUSTOM_ON */

/* ---- 消息框关闭结果 → 状态栏 ---- */
#if DLGPG_MSGBOX_ON
static void dlgpg_msgAcceptedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    char buf[128];
    const XString* title;
    (void)args;
    if (!box)
        return;
    title = XWidget_windowTitle((XWidget*)box);
    snprintf(buf, sizeof(buf), "消息框[%s]：接受（result=%d）",
             title ? XString_toUtf8(title) : "",
             XDialog_result(&box->m_base));
    dlgpg_status(buf);
}

static void dlgpg_msgRejectedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    char buf[128];
    const XString* title;
    (void)args;
    if (!box)
        return;
    title = XWidget_windowTitle((XWidget*)box);
    snprintf(buf, sizeof(buf), "消息框[%s]：拒绝（result=%d）",
             title ? XString_toUtf8(title) : "",
             XDialog_result(&box->m_base));
    dlgpg_status(buf);
}
#endif /* DLGPG_MSGBOX_ON */

/* ---- 进度对话框：取消按钮 → cancel()；canceled() → 状态栏 ---- */
#if DLGPG_PROGRESS_ON
static void dlgpg_progressCancelSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XProgressDialog_cancel((XProgressDialog*)receiver);
}

static void dlgpg_progressCanceledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    dlgpg_status("进度对话框：已取消（canceled 信号）");
}
#endif /* DLGPG_PROGRESS_ON */

/* ---- 自定义对话框关闭结果 → 状态栏 ---- */
#if DLGPG_CUSTOM_ON
static void dlgpg_customAcceptedSlot(XObject* receiver, XVarList* args)
{
    char buf[96];
    (void)receiver;
    (void)args;
    snprintf(buf, sizeof(buf), "自定义对话框：已接受（result=%d）",
             s_dlgpg.m_custom ? XDialog_result(s_dlgpg.m_custom) : -1);
    dlgpg_status(buf);
}

static void dlgpg_customRejectedSlot(XObject* receiver, XVarList* args)
{
    char buf[96];
    (void)receiver;
    (void)args;
    snprintf(buf, sizeof(buf), "自定义对话框：已拒绝（result=%d）",
             s_dlgpg.m_custom ? XDialog_result(s_dlgpg.m_custom) : -1);
    dlgpg_status(buf);
}
#endif /* DLGPG_CUSTOM_ON */

/* ==================== 触发按钮槽：消息框（非阻塞实例路径） ==================== */

#if DLGPG_MSGBOX_ON

/** @brief 消息框演示类别（对标 QMessageBox 图标分级）。 */
typedef enum DlgPgMsgKind
{
    DlgPgMsg_Information = 0,
    DlgPgMsg_Warning = 1,
    DlgPgMsg_Critical = 2,
    DlgPgMsg_Question = 3
} DlgPgMsgKind;

/** @brief 惰性创建并配置消息框实例（确定/取消标准按钮，不弹窗）。 */
static XMessageBox* dlgpg_ensureMsgBox(DlgPgMsgKind kind)
{
    XMessageBox** slot = NULL;
    XMessageBox* box;
    const char* title;
    const char* text;
    XMessageBoxIcon icon;
    switch (kind) {
    case DlgPgMsg_Warning:
        slot = &s_dlgpg.m_msgWarn;
        title = "警告";
        text = "配置文件缺失，是否使用默认配置？";
        icon = XMessageBoxIcon_Warning;
        break;
    case DlgPgMsg_Critical:
        slot = &s_dlgpg.m_msgError;
        title = "错误";
        text = "设备打开失败，请检查连接后重试。";
        icon = XMessageBoxIcon_Critical;
        break;
    case DlgPgMsg_Question:
        slot = &s_dlgpg.m_msgAsk;
        title = "询问";
        text = "是否保存当前修改？";
        icon = XMessageBoxIcon_Question;
        break;
    case DlgPgMsg_Information:
    default:
        slot = &s_dlgpg.m_msgInfo;
        title = "信息";
        text = "操作已完成。";
        icon = XMessageBoxIcon_Information;
        break;
    }
    if (*slot)
        return *slot;
    /* 对标 Qt QMessageBox 独立顶层窗口：不带窗口类型位构造（库内
     * 自动叠加 Dialog 类型 + 默认 hint），即带系统标题栏/系统菜单/
     * 关闭钮的原生对话框窗（此前 Popup 无框形态与 Qt 偏离，已废弃；
     * 覆盖层形态双影/几何被父链改写问题随窗口形态一并消除）。
     * 打开经 XDialog_open（非阻塞，模态门照常生效）。 */
    box = XMessageBox_create(s_dlgpg.m_root, 0);
    if (!box)
        return NULL;
    /* 标题走继承的 QWidget::setWindowTitle 对齐（QMessageBox 无
       setTitle；XMessageBox_setTitle 重复入口已删）。 */
    {
        XString* windowTitle = XString_create_utf8(title);
        XWidget_setWindowTitle((XWidget*)box, windowTitle);
        if (windowTitle) XClassDelete((XClass*)windowTitle);
    }
    XMessageBox_setText(box, text);
    XMessageBox_setIcon(box, icon);
    XMessageBox_setStandardButtons(box,
                                   (int)XDialogButtonBoxStandard_Ok |
                                   (int)XDialogButtonBoxStandard_Cancel);
    /* 按钮收口由消息盒内部完成（对标 QMessageBox 订阅按钮盒 clicked
       自行 done(标准位值)）：accepted/rejected/finished 信号直接回
       状态栏，不再经按钮盒角色信号中继 accept/reject（Qt 无此桥接，
       双路收口会把位值结果二次覆盖为 0/1）。 */
    XObject_connect_1((XObject*)box,
                      (size_t)XDialog_accepted_signal(NULL),
                      (XObject*)box, dlgpg_msgAcceptedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)box,
                      (size_t)XDialog_rejected_signal(NULL),
                      (XObject*)box, dlgpg_msgRejectedSlot,
                      XConnectionType_Direct);
    /* 不显式定位：消息盒为独立顶层窗口（几何=屏幕坐标），open/exec
       每次显示自动居中于父级顶层窗口（对标 QDialog::adjustPosition；
       显式 setGeometry 会把页面坐标误当屏幕坐标并永久固定位置）。 */
    *slot = box;
    return box;
}

static void dlgpg_msgInfoTrigger(XObject* sender, XVarList* args)
{
    XMessageBox* box;
    (void)sender;
    (void)args;
    box = dlgpg_ensureMsgBox(DlgPgMsg_Information);
    if (!box)
        return;
    XDialog_open(&box->m_base); /* 对标 QDialog::open：模态显示、立即返回。 */
    dlgpg_status("消息框-信息：已非阻塞打开");
}

static void dlgpg_msgWarnTrigger(XObject* sender, XVarList* args)
{
    XMessageBox* box;
    (void)sender;
    (void)args;
    box = dlgpg_ensureMsgBox(DlgPgMsg_Warning);
    if (!box)
        return;
    XDialog_open(&box->m_base);
    dlgpg_status("消息框-警告：已非阻塞打开");
}

static void dlgpg_msgErrorTrigger(XObject* sender, XVarList* args)
{
    XMessageBox* box;
    (void)sender;
    (void)args;
    box = dlgpg_ensureMsgBox(DlgPgMsg_Critical);
    if (!box)
        return;
    XDialog_open(&box->m_base);
    dlgpg_status("消息框-错误：已非阻塞打开");
}

static void dlgpg_msgAskTrigger(XObject* sender, XVarList* args)
{
    XMessageBox* box;
    (void)sender;
    (void)args;
    box = dlgpg_ensureMsgBox(DlgPgMsg_Question);
    if (!box)
        return;
    XDialog_open(&box->m_base);
    dlgpg_status("消息框-询问：已非阻塞打开");
}
#endif /* DLGPG_MSGBOX_ON */

/* ==================== 触发按钮槽：输入对话框（模态便捷函数） ==================== */

#if DLGPG_INPUT_ON
static void dlgpg_inputTrigger(XObject* sender, XVarList* args)
{
    bool ok = false;
    XString* text;
    char buf[192];
    const char* shown;
    (void)sender;
    (void)args;
    /* 便捷函数内部 exec 阻塞（应用模态），真人可交互；autotest 不覆盖。
       初值改占位提示（getText_3）：「预置文本」仅空框灰显、输入即消失，
       确认/取消回传不携带占位串（对标 QInputDialog placeholder 语义）。 */
    text = XInputDialog_getText_3(
        s_dlgpg.m_root,
        "输入对话框",
        "请输入名称：",
        XInputDialogEchoMode_Normal,
        NULL,
        "预置文本",
        &ok);
    shown = (text && XString_toUtf8(text)) ? XString_toUtf8(text) : "";
    snprintf(buf, sizeof(buf), "输入对话框：%s，文本=\"%s\"",
             ok ? "确认" : "取消", shown);
    dlgpg_status(buf);
    if (text)
        XClassDelete((XClass*)text); /* 返回副本归调用方释放。 */
}
#endif /* DLGPG_INPUT_ON */

/* ==================== 触发按钮槽：文件对话框（模态便捷函数） ==================== */

#if DLGPG_FILE_ON
static void dlgpg_fileTrigger(XObject* sender, XVarList* args)
{
    int filterIndex = 0;
    XString* file;
    char buf[224];
    const char* shown;
    (void)sender;
    (void)args;
    /* 便捷函数内部 exec 阻塞（应用模态），真人可交互；autotest 不覆盖。
       多选 getOpenFileNames 未实化（XGui.md §8.1 已声明边界）。 */
    file = XFileDialog_getOpenFileName_2(
        s_dlgpg.m_root,
        "打开文件",
        ".",
        "文本文件 (*.txt);;所有文件 (*)",
        &filterIndex);
    shown = (file && XString_toUtf8(file)) ? XString_toUtf8(file) : "";
    snprintf(buf, sizeof(buf),
             "文件对话框：选中=\"%s\"，过滤器下标=%d", shown, filterIndex);
    dlgpg_status(buf);
    if (file)
        XClassDelete((XClass*)file); /* 返回副本归调用方释放。 */
}
#endif /* DLGPG_FILE_ON */

/* ==================== 触发按钮槽：目录对话框（模态便捷函数） ==================== */

#if DLGPG_FILE_ON
static void dlgpg_dirTrigger(XObject* sender, XVarList* args)
{
    XString* dir;
    const char* shown;
    char buf[128];
    (void)sender;
    (void)args;
    /* 便捷函数内部 exec 阻塞（应用模态），真人可交互；autotest 不覆盖。
       目录模式（ShowDirsOnly）：列表仅文件夹，确认钮「选择文件夹」。 */
    dir = XFileDialog_getExistingDirectory_2(
        s_dlgpg.m_root,
        "选择文件夹",
        ".");
    shown = (dir && XString_toUtf8(dir)) ? XString_toUtf8(dir) : "";
    snprintf(buf, sizeof(buf), "目录对话框：选中=\"%s\"", shown);
    dlgpg_status(buf);
    if (dir)
        XClassDelete((XClass*)dir);
}
#endif /* DLGPG_FILE_ON */

/* ==================== 触发按钮槽：颜色对话框（模态便捷函数） ==================== */

#if DLGPG_COLOR_ON
static void dlgpg_colorTrigger(XObject* sender, XVarList* args)
{
    XColor color;
    char buf[128];
    (void)sender;
    (void)args;
    /* 便捷函数内部 exec 阻塞（应用模态），真人可交互；autotest 不覆盖。
       无 Alpha 通道输入、48 标准色简化（XGui.md §8.1 已声明边界）。 */
    color = XColorDialog_getColor_2(XColor_create_rgb(70, 130, 180, 255),
                                    s_dlgpg.m_root,
                                    "选择颜色",
                                    0);
    snprintf(buf, sizeof(buf), "颜色对话框：选中 RGB(%d, %d, %d)",
             XColor_red(&color), XColor_green(&color), XColor_blue(&color));
    dlgpg_status(buf);
}
#endif /* DLGPG_COLOR_ON */

/* ==================== 触发按钮槽：进度对话框（非阻塞实例路径） ==================== */

#if DLGPG_PROGRESS_ON
/** @brief 惰性创建进度对话框（非阻塞打开 + 内嵌取消按钮 + 百分比标签）。 */
static XProgressDialog* dlgpg_ensureProgress(void)
{
    XProgressDialog* progress;
    XString* text;
    if (s_dlgpg.m_progress)
        return s_dlgpg.m_progress;
    progress = XProgressDialog_create(s_dlgpg.m_root, 0);
    if (!progress)
        return NULL;
    XProgressDialog_setRange(progress, 0, 100);
    /* setLabelText 深拷贝入参：临时串自建自释放。 */
    text = XString_create_utf8("模拟任务进度");
    if (text) {
        XProgressDialog_setLabelText(progress, text);
        XClassDelete((XClass*)text);
    }
    /* 进度对话框实例无内嵌 UI（库内现状）：demo 自配百分比标签。
       [布局容器化 2026-10-17] 原 setGeometry 手写排布（标签 16,16,328,60
       /取消钮 248,120,96,32）退场：对话框内容挂垂直盒（边距 16 复刻
       原标签原点；间距 44 复刻原按钮行 y=120-标签底 76），标签定高 60
       随内容宽（原 328=窗宽 360-2*16 同口径），取消钮定尺 96x32 经
       伸缩空白右对齐（原 x 248..344 贴内容右缘）。窗体尺寸由对话框
       自定尺（≥360x160，show 路径 resize 自动重激活布局器）。 */
    s_dlgpg.m_progressLabel = XLabel_create((XWidget*)progress, 0);
    if (s_dlgpg.m_progressLabel) {
        XLabel_setText_2(s_dlgpg.m_progressLabel, "0%");
        XLabel_setAlignment(s_dlgpg.m_progressLabel,
                            XAlignment_Left | XAlignment_VCenter);
        XWidget_setFixedHeight((XWidget*)s_dlgpg.m_progressLabel, 60);
        XWidget_show((XWidget*)s_dlgpg.m_progressLabel);
    }
    /* 内嵌取消按钮：clicked → cancel() → canceled() 信号 → 状态栏。 */
    s_dlgpg.m_progressCancel = XPushButton_create((XWidget*)progress, 0);
    if (s_dlgpg.m_progressCancel) {
        XPushButton_setText_2(s_dlgpg.m_progressCancel, "取消");
        XWidget_setFixedWidth((XWidget*)s_dlgpg.m_progressCancel, 96);
        /* 触控目标下限（手机口径 ~40 逻辑像素）：原 32 指尖难点中；
           布局包络随之 +8，对话框 show 路径自定尺吸收（≥360x160）。 */
        XWidget_setFixedHeight((XWidget*)s_dlgpg.m_progressCancel, 40);
        XObject_connect_1((XObject*)s_dlgpg.m_progressCancel,
                          (size_t)XPushButton_clicked_signal(NULL, false),
                          (XObject*)progress, dlgpg_progressCancelSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)s_dlgpg.m_progressCancel);
    }
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout_init(&s_dlgpg.m_progressLayout,
                    XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_dlgpg.m_progressLayout,
                               16, 16, 16, 16);
    XLayout_setSpacing((XLayout*)&s_dlgpg.m_progressLayout, 44);
    if (s_dlgpg.m_progressLabel)
        XBoxLayout_addWidget(&s_dlgpg.m_progressLayout,
                             (XWidget*)s_dlgpg.m_progressLabel);
    XBoxLayout_init(&s_dlgpg.m_progressActions,
                    XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addStretch(&s_dlgpg.m_progressActions, 1);
    if (s_dlgpg.m_progressCancel)
        XBoxLayout_addWidget(&s_dlgpg.m_progressActions,
                             (XWidget*)s_dlgpg.m_progressCancel);
    XBoxLayout_addLayout(&s_dlgpg.m_progressLayout,
                         (XLayout*)&s_dlgpg.m_progressActions);
    XWidget_setLayout((XWidget*)progress,
                      (XLayout*)&s_dlgpg.m_progressLayout);
#endif
    XObject_connect_1((XObject*)progress,
                      (size_t)XProgressDialog_canceled_signal(NULL),
                      (XObject*)progress, dlgpg_progressCanceledSlot,
                      XConnectionType_Direct);
    /* 不显式定位：对话框为独立顶层窗口（几何=屏幕坐标），open 每次
       显示自动居中于父级顶层窗口（对标 QDialog::adjustPosition）。 */
    s_dlgpg.m_progress = progress;
    return progress;
}

static void dlgpg_progressTrigger(XObject* sender, XVarList* args)
{
    XProgressDialog* progress;
    int value;
    char buf[96];
    (void)sender;
    (void)args;
    progress = dlgpg_ensureProgress();
    if (!progress)
        return;
    /* 每次点击推进 10%；越过最大值归零重跑（模拟反复执行的任务）。 */
    value = XProgressDialog_value(progress);
    value += 10;
    if (value > XProgressDialog_maximum(progress))
        value = XProgressDialog_minimum(progress);
    XProgressDialog_setValue(progress, value);
    if (s_dlgpg.m_progressLabel) {
        snprintf(buf, sizeof(buf), "%d%%", value);
        XLabel_setText_2(s_dlgpg.m_progressLabel, buf);
    }
    if (!XWidget_isVisible((XWidget*)progress)) {
        XDialog_open(&progress->m_base); /* 非阻塞模态显示。 */
        dlgpg_status("进度对话框：已非阻塞打开，再点推进 10%");
    } else if (value == XProgressDialog_minimum(progress)) {
        /* setValue(max) 命中 Qt 语义 autoReset/autoClose：复位+隐藏。 */
        dlgpg_status("进度对话框：达最大值已复位（autoReset）");
    } else {
        snprintf(buf, sizeof(buf), "进度对话框：当前 %d%%", value);
        dlgpg_status(buf);
    }
}
#endif /* DLGPG_PROGRESS_ON */

/* ==================== 触发按钮槽：自定义对话框（非阻塞实例路径） ==================== */

#if DLGPG_CUSTOM_ON
/** @brief 惰性创建自定义对话框：XDialog + XLabel + XDialogButtonBox。 */
static XDialog* dlgpg_ensureCustom(void)
{
    XDialog* dialog;
    XDialogButtonBox* box;
    if (s_dlgpg.m_custom)
        return s_dlgpg.m_custom;
    dialog = XDialog_create(s_dlgpg.m_root, 0);
    if (!dialog)
        return NULL;
    s_dlgpg.m_customLabel = XLabel_create((XWidget*)dialog, 0);
    if (s_dlgpg.m_customLabel) {
        XLabel_setText_2(s_dlgpg.m_customLabel, "自定义对话框\n确认或取消？");
        XLabel_setAlignment(s_dlgpg.m_customLabel,
                            XAlignment_Left | XAlignment_VCenter);
        XWidget_setFixedHeight((XWidget*)s_dlgpg.m_customLabel, 70);
        XWidget_show((XWidget*)s_dlgpg.m_customLabel);
    }
    box = XDialogButtonBox_create((XWidget*)dialog, 0);
    if (box) {
        XDialogButtonBox_setStandardButtons(
            box, (int)XDialogButtonBoxStandard_Ok |
                 (int)XDialogButtonBoxStandard_Cancel);
        XWidget_setFixedHeight((XWidget*)box, 40);
        XWidget_show((XWidget*)box);
        /* 按钮盒标准按钮按角色发 accepted/rejected → accept()/reject()。 */
        XObject_connect_1((XObject*)box,
                          (size_t)XDialogButtonBox_accepted_signal(NULL),
                          (XObject*)dialog, dlgpg_boxAcceptSlot,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)box,
                          (size_t)XDialogButtonBox_rejected_signal(NULL),
                          (XObject*)dialog, dlgpg_boxRejectSlot,
                          XConnectionType_Direct);
    }
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* [布局容器化 2026-10-17] 原 setGeometry 手写排布（标签 16,16,308,70
       /按钮盒 0,130,340,40）退场：内容挂垂直盒——边距 (16,16,16,50)
       复刻原标签原点与按钮盒下余量（220-170），间距 44 复刻原按钮盒
       y=130-标签底 86，标签/按钮盒定高 70/40、宽随内容宽。CSD 内容
       避让经布局器口径生效（xdlg_applyContentAvoidance 对挂布局器的
       对话框只加高顶边距，不再逐子控件位移，见 XDialog.c）。 */
    XBoxLayout_init(&s_dlgpg.m_customLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_dlgpg.m_customLayout,
                               16, 16, 16, 50);
    XLayout_setSpacing((XLayout*)&s_dlgpg.m_customLayout, 44);
    if (s_dlgpg.m_customLabel)
        XBoxLayout_addWidget(&s_dlgpg.m_customLayout,
                             (XWidget*)s_dlgpg.m_customLabel);
    if (box)
        XBoxLayout_addWidget(&s_dlgpg.m_customLayout, (XWidget*)box);
    XWidget_setLayout((XWidget*)dialog, (XLayout*)&s_dlgpg.m_customLayout);
#endif
    XObject_connect_1((XObject*)dialog,
                      (size_t)XDialog_accepted_signal(NULL),
                      (XObject*)dialog, dlgpg_customAcceptedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)dialog,
                      (size_t)XDialog_rejected_signal(NULL),
                      (XObject*)dialog, dlgpg_customRejectedSlot,
                      XConnectionType_Direct);
    /* 内容定尺（r2 猎捕 defect#4/#9）：布局器内容包络 = 顶边距 16 +
     *   标签 70 + 间距 44 + 按钮盒 40 + 底边距 50 = 220 高；
     * 再计 CSD 内容避让：XDialog show/exec 经 xdlg_applyContentAvoidance
     *   把挂布局器对话框的顶边距按装饰条高整体加高（本主题活体实测
     *   31px：定尺 220 时按钮盒越界部分收进底边距带 161..201 完整
     *   入窗且留 ~19px 底呼吸边，与行内 16px 边距同数量级）；宽
     *   340+20=360（按钮盒右缘 + 右呼吸边）。此前不显式定尺：基类无
     *   预置默认时沿用 XWidget 子控件默认 100x30 空壳（r1 #3），基类
     *   有预置默认（640x480）时内容集中左上、右侧 ~300px/下方 ~310px
     *   空置（r2 #4/#9）——显式定尺两种基线态下都收敛到内容实需。
     * 不显式定位：对话框为独立顶层窗口（几何=屏幕坐标），open 每次
     * 显示自动居中于父级顶层窗口（对标 QDialog::adjustPosition）。 */
    XWidget_resize((XWidget*)dialog, 360, 220);
    s_dlgpg.m_customBox = box;
    s_dlgpg.m_custom = dialog;
    return dialog;
}

static void dlgpg_customTrigger(XObject* sender, XVarList* args)
{
    XDialog* dialog;
    (void)sender;
    (void)args;
    dialog = dlgpg_ensureCustom();
    if (!dialog)
        return;
    XDialog_open(dialog);
    dlgpg_status("自定义对话框：已非阻塞打开");
}
#endif /* DLGPG_CUSTOM_ON */

/* ==================== autotest 信号计数槽 ==================== */

static void dlgpg_countAcceptedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_dlgpg.m_acceptedCount;
}

static void dlgpg_countRejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_dlgpg.m_rejectedCount;
}

#if DLGPG_INPUT_ON
static void dlgpg_countIntChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_dlgpg.m_intChangedCount;
}
#endif /* DLGPG_INPUT_ON */

#if DLGPG_COLOR_ON
static void dlgpg_countColorChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_dlgpg.m_colorChangedCount;
}
#endif /* DLGPG_COLOR_ON */

static void dlgpg_countCanceledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_dlgpg.m_canceledCount;
}

/* ==================== 页面构建（契约接口） ==================== */

#if DLGPG_BUTTONS_ON
/** @brief 装配一行：触发按钮 + 说明标签（几何交行盒布局器分配）。
 *  @details 入口防御：row 越界直接忽略（历史上 ROW_COUNT=9 而装配
 *           10 行，越界写 m_btn[9]/m_note[9] 覆盖相邻 m_msgInfo 槽，
 *           把 XLabel 指针当 XMessageBox* open——见 DLGPG_ROW_COUNT
 *           注）。
 *           [布局容器化 2026-10-17] 原手工 setGeometry（按钮 16,y,150,34
 *           /说明 176,y,568,34）退场：控件只留定尺（按钮 150x34、说明
 *           定高 34——均为宽根档口径，宽根档宽也由 relayout 按档位收
 *           放；窄根档由 dlgpg_scrollRelayout 翻纵向单列并改触控高/
 *           换行定高），原点/行距/列位由内容根垂直盒边距宏
 *           （DLGPG_MARGIN_L/T/R/B）、行盒间距 DLGPG_ROW_SPACING 与
 *           行盒底边距 DLGPG_ROW_GAP 复刻；行盒对象存 m_rowLayouts[row]
 *           （addLayout 借用不取所有权，随页常驻）。 */
static void dlgpg_addRow(int row, const char* btnText,
                         const char* noteText, XSlotFunc1 trigger)
{
    XPushButton* button;
    if (row < 0 || row >= DLGPG_ROW_COUNT)
        return;
    button = XPushButton_create(s_dlgpg.m_root, 0);
    if (button) {
        XPushButton_setText_2(button, btnText);
        XWidget_setFixedWidth((XWidget*)button, DLGPG_BTN_WIDTH);
        XWidget_setFixedHeight((XWidget*)button, DLGPG_BTN_HEIGHT);
        if (trigger)
            XObject_connect_1((XObject*)button,
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              (XObject*)button, trigger,
                              XConnectionType_Direct);
        XWidget_show((XWidget*)button);
        s_dlgpg.m_btn[row] = button;
    }
#if DLGPG_LABELS_ON
    if (noteText) {
        XLabel* note = XLabel_create(s_dlgpg.m_root, 0);
        if (note) {
            XFont noteFont;
            XLabel_setText_2(note, noteText);
            XLabel_setAlignment(note, XAlignment_Left | XAlignment_VCenter);
            XWidget_setFixedWidth((XWidget*)note, DLGPG_NOTE_WIDTH);
            XWidget_setFixedHeight((XWidget*)note, DLGPG_BTN_HEIGHT);
            XWidget_show((XWidget*)note);
            s_dlgpg.m_note[row] = note;
            /* 字体度量自适应：ft2 直读字体的字形宽于旧内置字库，常量
               列宽会截断文案（真屏对话框页实证）。按标签自身字体实测
               最长文案宽度，relayout 时列宽取「常量口径/实测」较大者。 */
            /* 字体度量自适应：ft2 直读字体的字形宽于旧内置字库，常量
               列宽会截断文案（真屏对话框页实证）。按标签自身字体实测
               最长文案宽度，relayout 时列宽取「常量口径/实测」较大者。
               [尾字裁半根修 2026-10-11] 余量固定 +8 不够：分数 DPR
               （本机 1.25）下绘制路径逐字形设备像素取整，长文案累计
               漂移 ~1 设备px/字（「互」字实测裁半，24 字 ≈ +21 设备px
               ≈ 17 逻辑px）——余量改按字号给（textHeight 覆盖同量级
               字数），随字体尺寸自适应。 */
            noteFont = XWidget_font((XWidget*)note);
            {
                int textW = XPainter_textWidth(&noteFont, noteText) +
                            XPainter_textHeight(&noteFont) + 8;
                if (textW > s_dlgpg.m_noteTextW)
                    s_dlgpg.m_noteTextW = textW;
            }
        }
    }
#else
    (void)noteText;
#endif /* DLGPG_LABELS_ON */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 行盒装配：按钮+说明水平排（间距 10 复刻原说明列 x 偏移），底
       边距=行距 DLGPG_ROW_GAP（末行不留，由内容根盒底边距收口）。 */
    {
        XBoxLayout* rowBox = &s_dlgpg.m_rowLayouts[row];
        XBoxLayout_init(rowBox, XBoxLayoutDirection_LeftToRight);
        XLayout_setSpacing((XLayout*)rowBox, DLGPG_ROW_SPACING);
        XLayout_setContentsMargins(
            (XLayout*)rowBox, 0, 0, 0,
            row == DLGPG_ROW_COUNT - 1 ? 0 : DLGPG_ROW_GAP);
        if (s_dlgpg.m_btn[row])
            XBoxLayout_addWidget(rowBox, (XWidget*)s_dlgpg.m_btn[row]);
#if DLGPG_LABELS_ON
        if (s_dlgpg.m_note[row])
            XBoxLayout_addWidget(rowBox, (XWidget*)s_dlgpg.m_note[row]);
#endif
        XBoxLayout_addLayout(&s_dlgpg.m_pageLayout, (XLayout*)rowBox);
    }
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */
}
#endif /* DLGPG_BUTTONS_ON */

XWidget* demo_page_dialogs_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user)
{
    if (!parent)
        return NULL;
    /* demo 单实例：重复 build 直接返回已登记根控件（契约见
       xgui_demo_pages.h「build 时登记、autotest 使用」口径）。守卫挡在
       memset 之前，防止登记表被静默清零后旧 m_scroll/m_root/常驻对话
       框指针悬垂、再次 build 泄漏整棵页面树。 */
    if (s_dlgpg.m_scroll)
        return s_dlgpg.m_scroll;
    memset(&s_dlgpg, 0, sizeof(s_dlgpg));
    s_dlgpg.m_status = status;
    s_dlgpg.m_statusUser = user;

    /* 页面根控件：滚动区（屏幕 600 高面板/窗口化视口都放不下 10 行
       内容，空间不足时按需出滚动条，行可滚动触达——对标 QScrollArea
       的 AsNeeded 滚动条语义）。内容容器定尺=行区自然高度（14 + 10
       行*50 + 底部余量，比旧 DLGPG_PAGE_HEIGHT=480 高 40——第 10 行
       「目录对话框」此前在页面定尺与视口两处都被裁掉），行内子控件
       几何交页内垂直盒布局器分配（见下方容器化段）；滚动区为返回的
       堆对象，父子链级联析构。 */
#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
    XSCROLLAREA_ON
    {
        DlgPgScroll* scroll = dlgpg_scrollCreate(parent);
        XWidget* content;
        if (!scroll)
            return NULL;
        content = XWidget_create((XWidget*)scroll, 0);
        if (!content) {
            XClassDelete((XScrollArea*)scroll);
            return NULL;
        }
        /* 内容定尺：宽度按右缘 HUD 让位带在 dlgpg_scrollRelayout 收口
           （build 时窗口尺寸未定版，resize/show 联动，见子类注）；
           widgetResizable(true) 后内容几何/滚动范围由框架按约束接管。 */
        XWidget_setFixedWidth(content, DLGPG_PAGE_WIDTH);
        XWidget_setFixedHeight(content, DLGPG_PAGE_HEIGHT + 40);
        XScrollArea_setWidget((XScrollArea*)scroll, content);
        XScrollArea_setWidgetResizable((XScrollArea*)scroll, true);
        s_dlgpg.m_root = content;
        s_dlgpg.m_scroll = (XWidget*)scroll;
    }
#else
    /* 滚动区模块裁剪：保持基线直建内容容器（无滚动/HUD 让位联动）。 */
    {
        XWidget* content = XWidget_create(parent, 0);
        if (!content)
            return NULL;
        XWidget_setFixedWidth(content, DLGPG_PAGE_WIDTH);
        XWidget_setFixedHeight(content, DLGPG_PAGE_HEIGHT + 40);
        s_dlgpg.m_root = content;
        s_dlgpg.m_scroll = content;
    }
#endif

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* ---- 页内布局容器化（2026-10-17）：内容根挂垂直盒布局器统一
       分配几何（原页内 setGeometry 手写排布全部退场）。观感对齐改造
       前截图：边距 (16,14,16,22) 复刻原首行原点 (16,14) 与末行底余量
       （520-498）；行盒（dlgpg_addRow 内装配）底边距 16=原行距 50-
       行高 34。SetNoConstraint：内容定尺由滚动区内容口径自持
       （relayout 按宽度分档收放宽/高，窄根单列档另翻行盒方向），
       布局激活不得反写最小尺寸。
       根盒初始化须先于 dlgpg_addRow（行盒经 addLayout 借挂根盒），
       挂接在行区装配完成后（attach 即激活解算一次）。 ---- */
    XBoxLayout_init(&s_dlgpg.m_pageLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_dlgpg.m_pageLayout,
                               DLGPG_MARGIN_L, DLGPG_MARGIN_T,
                               DLGPG_MARGIN_R, DLGPG_MARGIN_B);
    XLayout_setSizeConstraint((XLayout*)&s_dlgpg.m_pageLayout,
                              XLayoutSizeConstraint_SetNoConstraint);
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

#if DLGPG_BUTTONS_ON
    /* 一列触发按钮：每个对话框一行（口径见各行说明标签）。 */
#if DLGPG_MSGBOX_ON
    dlgpg_addRow(0, "消息框-信息", "非阻塞打开：确定/取消结果回传状态栏",
                 dlgpg_msgInfoTrigger);
    dlgpg_addRow(1, "消息框-警告", "非阻塞打开：确定/取消结果回传状态栏",
                 dlgpg_msgWarnTrigger);
    dlgpg_addRow(2, "消息框-错误", "非阻塞打开：确定/取消结果回传状态栏",
                 dlgpg_msgErrorTrigger);
    dlgpg_addRow(3, "消息框-询问", "非阻塞打开：确定/取消结果回传状态栏",
                 dlgpg_msgAskTrigger);
#endif
#if DLGPG_INPUT_ON
    dlgpg_addRow(4, "输入对话框",
                 "（模态，autotest 不覆盖）便捷函数 exec 阻塞交互",
                 dlgpg_inputTrigger);
#endif
#if DLGPG_FILE_ON
    dlgpg_addRow(5, "文件对话框",
                 "（模态，autotest 不覆盖）便捷函数 exec 阻塞交互",
                 dlgpg_fileTrigger);
#endif
#if DLGPG_COLOR_ON
    dlgpg_addRow(6, "颜色对话框",
                 "（模态，autotest 不覆盖）便捷函数 exec 阻塞交互",
                 dlgpg_colorTrigger);
#endif
#if DLGPG_PROGRESS_ON
    dlgpg_addRow(7, "进度对话框",
                 "非阻塞打开：再点推进 10%，内置取消按钮",
                 dlgpg_progressTrigger);
#endif
#if DLGPG_CUSTOM_ON
    dlgpg_addRow(8, "自定义对话框",
                 "XDialog+XLabel+XDialogButtonBox：非阻塞打开",
                 dlgpg_customTrigger);
#endif
#if DLGPG_FILE_ON
    dlgpg_addRow(9, "目录对话框",
                 "（模态，autotest 不覆盖）便捷函数 exec 阻塞交互",
                 dlgpg_dirTrigger);
#endif
#endif /* DLGPG_BUTTONS_ON */

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* 行区装配完成，根盒挂内容容器：此后行/按钮/说明几何全由布局器
       分配（主文件切页/resize 全路径经滚动区子类 resizeEvent/showEvent
       → dlgpg_scrollRelayout 收口列宽并重激活）。 */
    XWidget_setLayout(s_dlgpg.m_root, (XLayout*)&s_dlgpg.m_pageLayout);
#endif

    /* 返回滚动区（堆对象，栈布局接管）；m_root=内容容器仅作子控件
       挂载点，父子链级联析构覆盖。 */
    return (XWidget*)s_dlgpg.m_scroll;
}

/* ==================== 页面自测（契约接口，全程非阻塞） ==================== */

/** @brief 断言宏：PASS/FAIL 输出并计数（口径与主文件
 *         demo_input_autotest 一致）。 */
#define DLGPG_EXPECT(cond, what) \
    do { \
        if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", (what)); \
        else { XPrintf("XGuiAutoTest: [FAIL] %s\n", (what)); ++failures; } \
    } while (0)

int demo_page_dialogs_autotest(XWidget* page)
{
    int failures = 0;

    /* 防错页调用：与 build 登记的滚动区/内容根不符返回 -1。 */
    if (!page || (page != s_dlgpg.m_scroll && page != s_dlgpg.m_root))
        return -1;
    s_dlgpg.m_acceptedCount = 0;
    s_dlgpg.m_rejectedCount = 0;
    s_dlgpg.m_intChangedCount = 0;
    s_dlgpg.m_colorChangedCount = 0;
    s_dlgpg.m_canceledCount = 0;

#if DLGPG_MSGBOX_ON
    /* ---- 1. XMessageBox：不弹窗构造 + 文本/标题/图标/标准按钮
     *         getter + 非阻塞 accept/reject 结果码与信号计数。 ---- */
    {
        XMessageBox* box = XMessageBox_create(page, 0);
        XString* boxTitle = XString_create_utf8("Autotest");
        DLGPG_EXPECT(box != NULL, "消息框堆构造成功");
        DLGPG_EXPECT(box == NULL || ((XWidget*)box)->m_isWindow,
                     "消息框构造即为独立顶层窗口（对标 QDialog 窗口类型）");
        if (box) {
            XWidget_setWindowTitle((XWidget*)box, boxTitle);
            XMessageBox_setText(box, "消息文本");
            XMessageBox_setIcon(box, XMessageBoxIcon_Information);
            XMessageBox_setStandardButtons(
                box, (int)XDialogButtonBoxStandard_Ok |
                     (int)XDialogButtonBoxStandard_Cancel);
            DLGPG_EXPECT(strcmp(XMessageBox_text(box), "消息文本") == 0,
                         "消息框 text getter 回读");
            DLGPG_EXPECT(
                XWidget_windowTitle((XWidget*)box) != NULL &&
                strcmp(XString_toUtf8(
                           XWidget_windowTitle((XWidget*)box)),
                       "Autotest") == 0,
                "消息框 windowTitle getter 回读（对标 setWindowTitle）");
            DLGPG_EXPECT(XMessageBox_icon(box) == XMessageBoxIcon_Information,
                         "消息框 icon getter 回读");
            DLGPG_EXPECT(XMessageBox_standardButtons(box) ==
                             ((int)XDialogButtonBoxStandard_Ok |
                              (int)XDialogButtonBoxStandard_Cancel),
                         "消息框 standardButtons getter 回读");
            XObject_connect_1((XObject*)box,
                              (size_t)XDialog_accepted_signal(NULL),
                              (XObject*)box, dlgpg_countAcceptedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)box,
                              (size_t)XDialog_rejected_signal(NULL),
                              (XObject*)box, dlgpg_countRejectedSlot,
                              XConnectionType_Direct);
            XDialog_accept(&box->m_base); /* 非阻塞：不进 exec 循环。 */
            DLGPG_EXPECT(XDialog_result(&box->m_base) == 1 &&
                         s_dlgpg.m_acceptedCount == 1,
                         "消息框 accept 结果码与 accepted 计数");
            XDialog_reject(&box->m_base);
            DLGPG_EXPECT(XDialog_result(&box->m_base) == 0 &&
                         s_dlgpg.m_rejectedCount == 1,
                         "消息框 reject 结果码与 rejected 计数");
            XClassDelete(box); /* 堆对象即测即毁防泄漏。 */
        }
        if (boxTitle)
            XClassDelete((XClass*)boxTitle);
    }

    /* ---- 1b. 对话框 Enter 键派发（§8.0g14 锁定）：open 非阻塞显示
     *         后向对话框直发 Return 键事件——默认按钮（首个可见可用
     *         标准按钮=Ok）应被点击 → clicked 收口 done(Ok 位值) 关闭
     *         → result==Ok 位值（对标 QMessageBox：result/exec 返回被
     *         点标准按钮位值，非 DialogCode）且 accepted 计数（角色
     *         映射 AcceptRole→accepted）；结束后显式清焦点防堆对象
     *         销毁后应用焦点悬垂。 ---- */
    {
        XMessageBox* box = XMessageBox_create(page, 0);
        int acceptedBefore = s_dlgpg.m_acceptedCount;
        DLGPG_EXPECT(box != NULL, "Enter 派发消息框堆构造成功");
        if (box) {
            XKeyEvent* keyEvent;
            XMessageBox_setStandardButtons(
                box, (int)XDialogButtonBoxStandard_Ok |
                     (int)XDialogButtonBoxStandard_Cancel);
            XObject_connect_1((XObject*)box,
                              (size_t)XDialog_accepted_signal(NULL),
                              (XObject*)box, dlgpg_countAcceptedSlot,
                              XConnectionType_Direct);
            XDialog_open(&box->m_base);
            DLGPG_EXPECT(XWidget_isVisible((XWidget*)box),
                         "Enter 派发消息框 open 后可见");
            keyEvent = XKeyEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           XEVENT_TYPE_KEY_PRESS,
                                           (int)XKey_Return, 0);
            DLGPG_EXPECT(keyEvent != NULL, "Return 键事件构造成功");
            if (keyEvent) {
                XObject_event_base((XObject*)box, (XEvent*)keyEvent);
                XClassDelete((XEvent*)keyEvent);
            }
            DLGPG_EXPECT(!XWidget_isVisible((XWidget*)box) &&
                         XDialog_result(&box->m_base) ==
                             (int)XDialogButtonBoxStandard_Ok &&
                         s_dlgpg.m_acceptedCount == acceptedBefore + 1,
                         "Return 直发命中默认按钮，result 回填 Ok 位值");
            XWidget_clearFocus((XWidget*)box);
            XClassDelete(box);
        }
    }

    /* ---- 1c. 页面路径回归（OOB 根因锁定）：点击"消息框-信息"触发
     *         按钮（row 0），断言消息框对象真实创建且类型正确、独立
     *         窗口形态、open 期间窗口模态。历史上 DLGPG_ROW_COUNT=9
     *         而装配 10 行，addRow 越界写 m_note[9] 覆盖相邻 m_msgInfo
     *         槽——ensureMsgBox 把说明标签指针当 XMessageBox* 返回并
     *         open（屏幕只有一段浮字、无窗口、应用假死）。结束后
     *         reject 关闭并验证模态恢复，防残留窗口/模态门影响后续
     *         用例。 ---- */
    {
        XPushButton* trigger = (DLGPG_BUTTONS_ON && DLGPG_ROW_COUNT > 0)
                                   ? s_dlgpg.m_btn[0] : NULL;
        XMessageBox* box = NULL;
        DLGPG_EXPECT(trigger != NULL, "对话框页消息框触发按钮存在");
        if (trigger) {
            /* 与真实点击同一信号路径（clicked → 触发槽）。 */
            XAbstractButton_click((XAbstractButton*)trigger);
            box = s_dlgpg.m_msgInfo;
        }
        DLGPG_EXPECT(box != NULL &&
                         XClassGetVtable((XObject*)box) ==
                             XMessageBox_class_init(),
                     "点击消息框-信息后消息框对象真实创建（类型校验）");
        DLGPG_EXPECT(box && ((XWidget*)box)->m_isWindow &&
                         XWidget_isVisible((XWidget*)box),
                     "消息框为独立顶层窗口且已可见");
        DLGPG_EXPECT(XWidget_windowModality((XWidget*)box) ==
                         XWindowModality_WindowModal,
                     "消息框 open 期间为窗口模态（对标 QDialog::open）");
        if (box) {
            XDialog_reject(&box->m_base);
            /* 消息盒构造即 setModal(true)（对标 qmessagebox.cpp:281），
             * 而 setModal(true) 同步 windowModality=ApplicationModal
             * （对标 qwidget.cpp:11440-11445）——open 记录/恢复的原值
             * 即 ApplicationModal，不再回 NonModal。 */
            DLGPG_EXPECT(XWidget_windowModality((XWidget*)box) ==
                             XWindowModality_ApplicationModal &&
                         !XWidget_isVisible((XWidget*)box),
                         "消息框关闭后恢复应用模态（modal 属性自洽）且隐藏");
        }
    }
#endif /* DLGPG_MSGBOX_ON */

#if DLGPG_INPUT_ON
    /* ---- 2. XInputDialog：堆构造 → 输入模式/初值/范围 getter →
     *         intValueChanged 信号计数 → accept/reject 结果码 →
     *         销毁。 ---- */
    {
        XInputDialog* input = XInputDialog_create(page, 0);
        DLGPG_EXPECT(input != NULL, "输入对话框堆构造成功");
        if (input) {
            XInputDialog_setInputMode(input, XInputDialog_IntInput);
            XInputDialog_setIntRange(input, 0, 100);
            XInputDialog_setIntValue(input, 42);
            DLGPG_EXPECT(XInputDialog_inputMode(input) ==
                         XInputDialog_IntInput,
                         "输入对话框 inputMode getter 回读");
            DLGPG_EXPECT(XInputDialog_intValue(input) == 42,
                         "输入对话框 intValue 初值回读");
            DLGPG_EXPECT(XInputDialog_intMinimum(input) == 0 &&
                         XInputDialog_intMaximum(input) == 100,
                         "输入对话框整数范围 getter 回读");
            XInputDialog_setOption(input, XInputDialog_NoButtons, true);
            DLGPG_EXPECT(XInputDialog_testOption(input,
                                                 XInputDialog_NoButtons),
                         "输入对话框选项位 set/test 对称");
            XObject_connect_1((XObject*)input,
                              (size_t)XInputDialog_intValueChanged_signal(NULL, 0),
                              (XObject*)input, dlgpg_countIntChangedSlot,
                              XConnectionType_Direct);
            XInputDialog_setIntValue(input, 43);
            DLGPG_EXPECT(XInputDialog_intValue(input) == 43 &&
                         s_dlgpg.m_intChangedCount == 1,
                         "输入对话框 intValueChanged 信号计数");
            XDialog_accept(&input->m_base);
            DLGPG_EXPECT(XDialog_result(&input->m_base) == 1,
                         "输入对话框 accept 结果码");
            XDialog_reject(&input->m_base);
            DLGPG_EXPECT(XDialog_result(&input->m_base) == 0,
                         "输入对话框 reject 结果码");
            XClassDelete(input);
        }
    }
#endif /* DLGPG_INPUT_ON */

#if DLGPG_FILE_ON
    /* ---- 3. XFileDialog：堆构造 → 文件模式/接受模式/过滤器列表/
     *         选中文件/目录 getter → accept/reject 结果码 → 销毁。 ---- */
    {
        XFileDialog* file = XFileDialog_create(page, 0);
        DLGPG_EXPECT(file != NULL, "文件对话框堆构造成功");
        if (file) {
            XString* filter = XString_create_utf8("Images (*.png *.jpg)");
            XString* probe = NULL;
            XStringList* filters = NULL;
            XFileDialog_setFileMode(file, XFileDialog_ExistingFile);
            XFileDialog_setAcceptMode(file, XFileDialog_AcceptOpen);
            if (filter) {
                XFileDialog_setNameFilter(file, filter);
                XClassDelete((XClass*)filter); /* setter 深拷贝。 */
            }
            DLGPG_EXPECT(XFileDialog_fileMode(file) ==
                         XFileDialog_ExistingFile,
                         "文件对话框 fileMode getter 回读");
            DLGPG_EXPECT(XFileDialog_acceptMode(file) ==
                         XFileDialog_AcceptOpen,
                         "文件对话框 acceptMode getter 回读");
            filters = XFileDialog_nameFilters(file); /* 副本归调用方。 */
            DLGPG_EXPECT(filters != NULL &&
                         XStringList_size_base((const XContainer*)filters) == 1,
                         "文件对话框过滤器列表元素数");
            if (filters) {
                probe = (XString*)XStringList_at_base(
                    (const XVector*)filters, (int64_t)0);
                DLGPG_EXPECT(probe != NULL &&
                             strcmp(XString_toUtf8(probe),
                                    "Images (*.png *.jpg)") == 0,
                             "文件对话框过滤器内容回读");
                XClassDelete((XClass*)filters);
                filters = NULL;
            }
            probe = XString_create_utf8("demo.txt");
            if (probe) {
                XFileDialog_selectFile(file, probe);
                XClassDelete((XClass*)probe);
            }
            probe = XFileDialog_selectedFile(file); /* 副本归调用方。 */
            DLGPG_EXPECT(probe != NULL &&
                         strcmp(XString_toUtf8(probe), "demo.txt") == 0,
                         "文件对话框 selectedFile 回读");
            if (probe)
                XClassDelete((XClass*)probe);
            probe = XString_create_utf8(".");
            if (probe) {
                XFileDialog_setDirectory(file, probe);
                XClassDelete((XClass*)probe);
            }
            probe = XFileDialog_directory(file); /* 副本归调用方。 */
            DLGPG_EXPECT(probe != NULL &&
                         strcmp(XString_toUtf8(probe), ".") == 0,
                         "文件对话框 directory 回读");
            if (probe)
                XClassDelete((XClass*)probe);
            XDialog_accept(&file->m_base);
            DLGPG_EXPECT(XDialog_result(&file->m_base) == 1,
                         "文件对话框 accept 结果码");
            XDialog_reject(&file->m_base);
            DLGPG_EXPECT(XDialog_result(&file->m_base) == 0,
                         "文件对话框 reject 结果码");
            XClassDelete(file);
        }
    }
#endif /* DLGPG_FILE_ON */

#if DLGPG_COLOR_ON
    /* ---- 4. XColorDialog：堆构造 → 色值初值/currentColorChanged →
     *         colorSelected 手动触发（头文件注明的测试口径）→
     *         标准色表 → accept/reject 结果码 → 销毁。 ---- */
    {
        XColor blue = XColor_create_rgb(0, 0, 255, 255);
        XColor red = XColor_create_rgb(255, 0, 0, 255);
        XColor green = XColor_create_rgb(0, 128, 0, 255);
        XColorDialog* color =
            XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, blue,
                                   page, 0);
        DLGPG_EXPECT(color != NULL, "颜色对话框堆构造成功");
        if (color) {
            XColor got;
            got = XColorDialog_currentColor(color);
            DLGPG_EXPECT(XColor_equals(&got, &blue),
                         "颜色对话框 currentColor 初值回读");
            XObject_connect_1(
                (XObject*)color,
                (size_t)XColorDialog_currentColorChanged_signal(NULL, blue),
                (XObject*)color, dlgpg_countColorChangedSlot,
                XConnectionType_Direct);
            XColorDialog_setCurrentColor(color, red);
            got = XColorDialog_currentColor(color);
            DLGPG_EXPECT(s_dlgpg.m_colorChangedCount == 1 &&
                         XColor_equals(&got, &red),
                         "颜色对话框 setCurrentColor 与信号计数");
            /* colorSelected 由应用在"接受"动作处手动触发（头文件注明
               的测试口径）：置 selectedColor 并发射。 */
            XColorDialog_colorSelected_signal(color, red);
            got = XColorDialog_selectedColor(color);
            DLGPG_EXPECT(XColor_equals(&got, &red),
                         "颜色对话框 colorSelected 置选中色");
            XColorDialog_setStandardColor(color, 0, green);
            got = XColorDialog_standardColor(color, 0);
            DLGPG_EXPECT(XColor_equals(&got, &green),
                         "颜色对话框标准色表 setter/getter");
            XDialog_accept(&color->m_base);
            DLGPG_EXPECT(XDialog_result(&color->m_base) == 1,
                         "颜色对话框 accept 结果码");
            XDialog_reject(&color->m_base);
            DLGPG_EXPECT(XDialog_result(&color->m_base) == 0,
                         "颜色对话框 reject 结果码");
            XClassDelete(color);
        }
    }
#endif /* DLGPG_COLOR_ON */

#if DLGPG_PROGRESS_ON
    /* ---- 5. XProgressDialog：构造 → 默认范围/最小时长 → setRange/
     *         setValue → value getter → canceled 信号连接 → 取消语义
     *         → 达最大值 autoReset 复位 → 销毁。 ---- */
    {
        XProgressDialog* progress = XProgressDialog_create(page, 0);
        DLGPG_EXPECT(progress != NULL, "进度对话框堆构造成功");
        if (progress) {
            DLGPG_EXPECT(XProgressDialog_minimum(progress) == 0 &&
                         XProgressDialog_maximum(progress) == 100,
                         "进度对话框默认范围 [0,100]");
            DLGPG_EXPECT(XProgressDialog_minimumDuration(progress) == 4000,
                         "进度对话框 minimumDuration 默认 4000ms");
            XProgressDialog_setRange(progress, 0, 50);
            DLGPG_EXPECT(XProgressDialog_minimum(progress) == 0 &&
                         XProgressDialog_maximum(progress) == 50,
                         "进度对话框 setRange getter 回读");
            XProgressDialog_setValue(progress, 30);
            DLGPG_EXPECT(XProgressDialog_value(progress) == 30,
                         "进度对话框 setValue/value getter 回读");
            XObject_connect_1(
                (XObject*)progress,
                (size_t)XProgressDialog_canceled_signal(NULL),
                (XObject*)progress, dlgpg_countCanceledSlot,
                XConnectionType_Direct);
            XProgressDialog_cancel(progress);
            DLGPG_EXPECT(s_dlgpg.m_canceledCount == 1 &&
                         XProgressDialog_wasCanceled(progress),
                         "进度对话框 cancel → canceled 信号与 wasCanceled");
            DLGPG_EXPECT(XProgressDialog_value(progress) == 0,
                         "进度对话框取消后复位到 minimum");
            XProgressDialog_setAutoReset(progress, true);
            XProgressDialog_setValue(progress, 50); /* 等于最大值 → 复位。 */
            DLGPG_EXPECT(XProgressDialog_value(progress) == 0 &&
                         !XProgressDialog_wasCanceled(progress),
                         "进度对话框达最大值 autoReset 复位");
            XClassDelete(progress);
        }
    }
#endif /* DLGPG_PROGRESS_ON */

#if DLGPG_CUSTOM_ON
    /* ---- 6. 自定义对话框：accept 与 reject 两条路径（经按钮盒中继
     *         槽直调，对标 accepted→accept / rejected→reject 接线）。
     *         ---- */
    {
        XDialog* dialog = XDialog_create(page, 0);
        DLGPG_EXPECT(dialog != NULL, "自定义对话框堆构造成功");
        if (dialog) {
            XDialogButtonBox* box =
                XDialogButtonBox_create((XWidget*)dialog, 0);
            DLGPG_EXPECT(box != NULL, "自定义对话框按钮盒创建成功");
            if (box) {
                XDialogButtonBox_setStandardButtons(
                    box, (int)XDialogButtonBoxStandard_Ok |
                         (int)XDialogButtonBoxStandard_Cancel);
                DLGPG_EXPECT(
                    XDialogButtonBox_standardButtons(box) ==
                        ((int)XDialogButtonBoxStandard_Ok |
                         (int)XDialogButtonBoxStandard_Cancel),
                    "自定义对话框按钮盒标准按钮位回读");
                XObject_connect_1(
                    (XObject*)box,
                    (size_t)XDialogButtonBox_accepted_signal(NULL),
                    (XObject*)dialog, dlgpg_boxAcceptSlot,
                    XConnectionType_Direct);
                XObject_connect_1(
                    (XObject*)box,
                    (size_t)XDialogButtonBox_rejected_signal(NULL),
                    (XObject*)dialog, dlgpg_boxRejectSlot,
                    XConnectionType_Direct);
            }
            XObject_connect_1((XObject*)dialog,
                              (size_t)XDialog_accepted_signal(NULL),
                              (XObject*)dialog, dlgpg_countAcceptedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)dialog,
                              (size_t)XDialog_rejected_signal(NULL),
                              (XObject*)dialog, dlgpg_countRejectedSlot,
                              XConnectionType_Direct);
            dlgpg_boxAcceptSlot((XObject*)dialog, NULL); /* 确定路径。 */
            DLGPG_EXPECT(XDialog_result(dialog) == 1 &&
                         s_dlgpg.m_acceptedCount >= 1,
                         "自定义对话框 accept 路径结果码与计数");
            dlgpg_boxRejectSlot((XObject*)dialog, NULL); /* 取消路径。 */
            DLGPG_EXPECT(XDialog_result(dialog) == 0 &&
                         s_dlgpg.m_rejectedCount >= 1,
                         "自定义对话框 reject 路径结果码与计数");
            XClassDelete(dialog); /* 按钮盒随父子链级联释放。 */
        }
    }
#endif /* DLGPG_CUSTOM_ON */

    XPrintf("XGuiAutoTest: 对话框页 %s（失败=%d）\n",
            failures == 0 ? "全部通过" : "存在失败", failures);
    return failures;
}

#undef DLGPG_EXPECT

#endif /* XWIDGET_ON */
