/******************************************************************************
 * @file       xgui_demo_page_dialogs.c
 * @brief      XGuiWindowDemo 扩展页：对话框页（对标 Qt 6.8 的
 *             QMessageBox/QInputDialog/QFileDialog/QColorDialog/
 *             QProgressDialog/QDialog+QDialogButtonBox 示例）。
 * @details    实现 xgui_demo_pages.h 契约的 demo_page_dialogs_build /
 *             demo_page_dialogs_autotest：
 *             - 页面内容区约 760x480，与主文件页面装配风格一致：全部
 *               手工 setGeometry（互不重叠），子控件逐一 XWidget_show；
 *             - 一列触发按钮（每个对话框一行）+ 每行说明标签；
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
#include <string.h>

#include "XGuiConfig.h"
#include "XObject.h"
#include "XPrintf.h"
#include "XString.h"
#include "XStringList.h"
#include "XColor.h"
#include "XWidget.h"
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

/* ==================== 页面几何常量（内容区约 760x480） ==================== */

#define DLGPG_PAGE_WIDTH   760
#define DLGPG_PAGE_HEIGHT  480
/* 触发按钮行数。必须与下方 dlgpg_addRow 调用次数严格一致（当前 10：
   消息框×4+输入/文件/颜色/进度/自定义/目录）——此值小一处，
   m_btn[10]/m_note[10] 末位越界写将砸中紧随其后的 m_msgInfo 等对话框
   槽位（实测：m_note[9] 越界把第 9 行说明标签指针写进消息框-信息槽，
   open 打在标签上＝信息框永不可见+错位残影+Esc 占死模态位）。
   （远端同位置备注：此值小一处将砸中相邻对话框槽位——新增行时同步
   更新本值。） */
#define DLGPG_ROW_COUNT    10
#define DLGPG_ROW_HEIGHT   50   /* 行距。 */
#define DLGPG_BTN_X        16
#define DLGPG_BTN_WIDTH    150
#define DLGPG_BTN_HEIGHT   34
#define DLGPG_NOTE_X       176
#define DLGPG_NOTE_WIDTH   568
/* 右缘 HUD 让位带（FPS 浮层自由拖动前的静态避让）：主窗右下角半透明
   FPS 性能浮层（XPerformanceOverlay 210x70+autoFit，见主文件
   demo_performance_init）压住滚动内容行尾文字（目验挂项）。根宽足够
   （>=DLGPG_HUD_YIELD_MIN_ROOT）时内容与说明列右缘收进浮层左缘内
   （带宽 224=浮层 210 + 14 呼吸，与 page12 图表让位 218 同口径）；
   根过窄不收——窄根浮层同样盖但行可滚动触达（收窄会挤压主列可读
   宽，得不偿失）。 */
#define DLGPG_HUD_YIELD_WIDTH   224
#define DLGPG_HUD_YIELD_MIN_ROOT 640

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

#if DLGPG_BUTTONS_ON
    XPushButton* m_btn[DLGPG_ROW_COUNT]; /* 触发按钮列。 */
#endif
#if DLGPG_LABELS_ON
    XLabel* m_note[DLGPG_ROW_COUNT];     /* 每行说明标签（模态/非阻塞口径）。 */
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
#endif
#if DLGPG_CUSTOM_ON
    XDialog* m_custom;             /* 自定义对话框（常驻复用）。 */
    XLabel* m_customLabel;         /* 自定义对话框提示标签。 */
    XDialogButtonBox* m_customBox; /* 自定义对话框按钮盒。 */
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
   （本文件 build 时窗口尺寸尚未定版，无法一次收口）；滚动区非
   widgetResizable 路径不回写内容尺寸（XScrollArea.c
   xsa_updateWidgetGeometry），故子类化在 resizeEvent/showEvent 重排：
   按根宽决定是否让位——收口内容定尺与说明列宽（行尾文字被控件矩形
   裁剪），并重新通告内容尺寸驱动滚动范围。XWidget_paintTree 对子控件
   逐级按自身矩形裁剪（XWidget.c:7146），收窄即截断行尾文字。 */

/** @brief 重排内容定尺与说明列宽（HUD 让位带档位判断的唯一入口）。 */
static void dlgpg_scrollRelayout(XScrollArea* scroll)
{
    XWidget* content;
    int rootW;
    int contentW;
    if (!scroll || !s_dlgpg.m_root)
        return;
    content = s_dlgpg.m_root;
    rootW = XWidget_width((XWidget*)scroll);
    /* 宽根让位：右缘收进 HUD 浮层左缘内；窄根保持满宽（浮层同样盖
       但滚动可达，见 DLGPG_HUD_YIELD_WIDTH 注）。 */
    contentW = DLGPG_PAGE_WIDTH;
    if (rootW >= DLGPG_HUD_YIELD_MIN_ROOT)
        contentW = DLGPG_PAGE_WIDTH - DLGPG_HUD_YIELD_WIDTH;
    if (contentW < 1)
        contentW = 1;
    XWidget_resize(content, contentW, DLGPG_PAGE_HEIGHT + 40);
#if DLGPG_LABELS_ON
    /* 说明列收窄 224：宽根缩至让位右缘，窄根恢复常量口径。 */
    {
        int i;
        int noteW = DLGPG_NOTE_WIDTH;
        if (rootW >= DLGPG_HUD_YIELD_MIN_ROOT)
            noteW = DLGPG_NOTE_WIDTH - DLGPG_HUD_YIELD_WIDTH;
        if (noteW < 1)
            noteW = 1;
        for (i = 0; i < DLGPG_ROW_COUNT; ++i) {
            if (!s_dlgpg.m_note[i])
                continue;
            XWidget_setGeometry((XWidget*)s_dlgpg.m_note[i], DLGPG_NOTE_X,
                                14 + i * DLGPG_ROW_HEIGHT, noteW,
                                DLGPG_BTN_HEIGHT);
        }
    }
#endif
    /* 收口后重新通告内容尺寸（内容尺寸驱动滚动范围，XScrollArea.c
       xsa_updateWidgetGeometry 尾注；窄根恢复满宽时水平条随之撤销）。 */
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)scroll,
                                       contentW, DLGPG_PAGE_HEIGHT + 40);
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
        if (windowTitle) XString_delete_base((XClass*)windowTitle);
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
        XString_delete_base((XClass*)text); /* 返回副本归调用方释放。 */
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
        XString_delete_base((XClass*)file); /* 返回副本归调用方释放。 */
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
        XString_delete_base((XClass*)dir);
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
        XString_delete_base((XClass*)text);
    }
    /* 进度对话框实例无内嵌 UI（库内现状）：demo 自配百分比标签。 */
    s_dlgpg.m_progressLabel = XLabel_create((XWidget*)progress, 0);
    if (s_dlgpg.m_progressLabel) {
        XLabel_setText_2(s_dlgpg.m_progressLabel, "0%");
        XLabel_setAlignment(s_dlgpg.m_progressLabel,
                            XAlignment_Left | XAlignment_VCenter);
        XWidget_setGeometry((XWidget*)s_dlgpg.m_progressLabel,
                            16, 16, 328, 60);
        XWidget_show((XWidget*)s_dlgpg.m_progressLabel);
    }
    /* 内嵌取消按钮：clicked → cancel() → canceled() 信号 → 状态栏。 */
    s_dlgpg.m_progressCancel = XPushButton_create((XWidget*)progress, 0);
    if (s_dlgpg.m_progressCancel) {
        XPushButton_setText_2(s_dlgpg.m_progressCancel, "取消");
        XWidget_setGeometry((XWidget*)s_dlgpg.m_progressCancel,
                            248, 120, 96, 32);
        XObject_connect_1((XObject*)s_dlgpg.m_progressCancel,
                          (size_t)XPushButton_clicked_signal(NULL, false),
                          (XObject*)progress, dlgpg_progressCancelSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)s_dlgpg.m_progressCancel);
    }
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
        XWidget_setGeometry((XWidget*)s_dlgpg.m_customLabel, 16, 16, 308, 70);
        XWidget_show((XWidget*)s_dlgpg.m_customLabel);
    }
    box = XDialogButtonBox_create((XWidget*)dialog, 0);
    if (box) {
        XDialogButtonBox_setStandardButtons(
            box, (int)XDialogButtonBoxStandard_Ok |
                 (int)XDialogButtonBoxStandard_Cancel);
        XWidget_setGeometry((XWidget*)box, 0, 130, 340, 40);
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
    XObject_connect_1((XObject*)dialog,
                      (size_t)XDialog_accepted_signal(NULL),
                      (XObject*)dialog, dlgpg_customAcceptedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)dialog,
                      (size_t)XDialog_rejected_signal(NULL),
                      (XObject*)dialog, dlgpg_customRejectedSlot,
                      XConnectionType_Direct);
    /* 内容定尺（r2 猎捕 defect#4/#9）：手工子控件几何的内容包络=
     *   标签 (16,16,308,70)   → 右缘 324、底缘 86
     *   按钮盒 (0,130,340,40) → 右缘 340、底缘 170
     * 即 340x170；再计 CSD 内容避让：XDialog show/exec 经
     * xdlg_applyContentAvoidance 把子控件按装饰条高整体下移
     * （XWindowDecoration_marginsFor().top = 标题条条带高，本主题
     * 活体实测 31px：定尺 190 首拍按钮盒整体位移后底缘 201 越出窗高
     * 被裁，提尺到 220 后按钮盒 161..201 完整入窗且留 ~19px 底呼吸
     * 边，与行内 16px 边距同数量级）；宽 340+20=360（按钮盒右缘 340
     * + 右呼吸边）。此前不显式定尺：基类无预置默认时沿用 XWidget
     * 子控件默认 100x30 空壳（r1 #3），基类有预置默认（640x480）时
     * 内容集中左上、右侧 ~300px/下方 ~310px 空置（r2 #4/#9）——
     * 显式定尺两种基线态下都收敛到内容实需，且显式定尺优先级高于
     * 基类默认（r1#3 断言同款口径）。
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
/** @brief 装配一行：触发按钮 + 说明标签（手工几何并 show）。
 *  @details 入口防御：row 越界直接忽略（历史上 ROW_COUNT=9 而装配
 *           10 行，越界写 m_btn[9]/m_note[9] 覆盖相邻 m_msgInfo 槽，
 *           把 XLabel 指针当 XMessageBox* open——见 DLGPG_ROW_COUNT
 *           注）。 */
static void dlgpg_addRow(int row, const char* btnText,
                         const char* noteText, XSlotFunc1 trigger)
{
    XPushButton* button;
    if (row < 0 || row >= DLGPG_ROW_COUNT)
        return;
    button = XPushButton_create(s_dlgpg.m_root, 0);
    int y = 14 + row * DLGPG_ROW_HEIGHT;
    if (button) {
        XPushButton_setText_2(button, btnText);
        XWidget_setGeometry((XWidget*)button, DLGPG_BTN_X, y,
                            DLGPG_BTN_WIDTH, DLGPG_BTN_HEIGHT);
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
            XLabel_setText_2(note, noteText);
            XLabel_setAlignment(note, XAlignment_Left | XAlignment_VCenter);
            XWidget_setGeometry((XWidget*)note, DLGPG_NOTE_X, y,
                                DLGPG_NOTE_WIDTH, DLGPG_BTN_HEIGHT);
            XWidget_show((XWidget*)note);
            s_dlgpg.m_note[row] = note;
        }
    }
#else
    (void)noteText;
#endif /* DLGPG_LABELS_ON */
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
       的 AsNeeded 滚动条语义）。内容容器保持设计定尺，m_root 指向
       内容容器：全部子控件的手工几何（行距 50px）仍按内容坐标布置，
       构建代码零改动；滚动区为返回的堆对象，父子链级联析构。 */
#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && \
    XSCROLLAREA_ON
    {
        DlgPgScroll* scroll = dlgpg_scrollCreate(parent);
        XWidget* content;
        if (!scroll)
            return NULL;
        content = XWidget_create((XWidget*)scroll, 0);
        if (!content) {
            XScrollArea_delete_base((XScrollArea*)scroll);
            return NULL;
        }
        /* 内容定尺 = 行区自然高度（14 + 10 行*50 + 底部余量），比
           旧 DLGPG_PAGE_HEIGHT=480 高 40——第 10 行「目录对话框」
           （y=464..500）此前在页面定尺与视口两处都被裁掉。
           宽度按右缘 HUD 让位带在 dlgpg_scrollRelayout 收口（build
           时窗口尺寸未定版，resize/show 联动，见子类注）。 */
        XWidget_setGeometry(content, 0, 0,
                            DLGPG_PAGE_WIDTH, DLGPG_PAGE_HEIGHT + 40);
        XScrollArea_setWidget((XScrollArea*)scroll, content);
        s_dlgpg.m_root = content;
        s_dlgpg.m_scroll = (XWidget*)scroll;
    }
#else
    /* 滚动区模块裁剪：保持基线直建内容容器（无滚动/HUD 让位联动）。 */
    {
        XWidget* content = XWidget_create(parent, 0);
        if (!content)
            return NULL;
        XWidget_setGeometry(content, 0, 0,
                            DLGPG_PAGE_WIDTH, DLGPG_PAGE_HEIGHT + 40);
        s_dlgpg.m_root = content;
        s_dlgpg.m_scroll = content;
    }
#endif

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
            XMessageBox_delete_base(box); /* 堆对象即测即毁防泄漏。 */
        }
        if (boxTitle)
            XString_delete_base((XClass*)boxTitle);
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
                XEvent_delete_base((XEvent*)keyEvent);
            }
            DLGPG_EXPECT(!XWidget_isVisible((XWidget*)box) &&
                         XDialog_result(&box->m_base) ==
                             (int)XDialogButtonBoxStandard_Ok &&
                         s_dlgpg.m_acceptedCount == acceptedBefore + 1,
                         "Return 直发命中默认按钮，result 回填 Ok 位值");
            XWidget_clearFocus((XWidget*)box);
            XMessageBox_delete_base(box);
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
            XInputDialog_delete_base(input);
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
                XString_delete_base((XClass*)filter); /* setter 深拷贝。 */
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
                XStringList_delete_base((XClass*)filters);
                filters = NULL;
            }
            probe = XString_create_utf8("demo.txt");
            if (probe) {
                XFileDialog_selectFile(file, probe);
                XString_delete_base((XClass*)probe);
            }
            probe = XFileDialog_selectedFile(file); /* 副本归调用方。 */
            DLGPG_EXPECT(probe != NULL &&
                         strcmp(XString_toUtf8(probe), "demo.txt") == 0,
                         "文件对话框 selectedFile 回读");
            if (probe)
                XString_delete_base((XClass*)probe);
            probe = XString_create_utf8(".");
            if (probe) {
                XFileDialog_setDirectory(file, probe);
                XString_delete_base((XClass*)probe);
            }
            probe = XFileDialog_directory(file); /* 副本归调用方。 */
            DLGPG_EXPECT(probe != NULL &&
                         strcmp(XString_toUtf8(probe), ".") == 0,
                         "文件对话框 directory 回读");
            if (probe)
                XString_delete_base((XClass*)probe);
            XDialog_accept(&file->m_base);
            DLGPG_EXPECT(XDialog_result(&file->m_base) == 1,
                         "文件对话框 accept 结果码");
            XDialog_reject(&file->m_base);
            DLGPG_EXPECT(XDialog_result(&file->m_base) == 0,
                         "文件对话框 reject 结果码");
            XFileDialog_delete_base(file);
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
            XColorDialog_delete_base(color);
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
            XProgressDialog_delete_base(progress);
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
            XDialog_delete_base(dialog); /* 按钮盒随父子链级联释放。 */
        }
    }
#endif /* DLGPG_CUSTOM_ON */

    XPrintf("XGuiAutoTest: 对话框页 %s（失败=%d）\n",
            failures == 0 ? "全部通过" : "存在失败", failures);
    return failures;
}

#undef DLGPG_EXPECT

#endif /* XWIDGET_ON */
