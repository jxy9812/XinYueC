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
#define DLGPG_ROW_COUNT    9    /* 触发按钮行数。 */
#define DLGPG_ROW_HEIGHT   50   /* 行距。 */
#define DLGPG_BTN_X        16
#define DLGPG_BTN_WIDTH    150
#define DLGPG_BTN_HEIGHT   34
#define DLGPG_NOTE_X       176
#define DLGPG_NOTE_WIDTH   568

/* ==================== 页面内部控件登记表（demo 单实例） ==================== */

typedef struct DlgPgUi
{
    XWidget* m_root;               /* 页面根控件（堆对象，build 登记）。 */
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
    (void)args;
    if (!box)
        return;
    snprintf(buf, sizeof(buf), "消息框[%s]：接受（result=%d）",
             XMessageBox_title(box), XDialog_result(&box->m_base));
    dlgpg_status(buf);
}

static void dlgpg_msgRejectedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    char buf[128];
    (void)args;
    if (!box)
        return;
    snprintf(buf, sizeof(buf), "消息框[%s]：拒绝（result=%d）",
             XMessageBox_title(box), XDialog_result(&box->m_base));
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
    /* 非阻塞实例路径：仅构造（不 exec），打开经 XDialog_open。 */
    box = XMessageBox_create(s_dlgpg.m_root, 0);
    if (!box)
        return NULL;
    XMessageBox_setTitle(box, title);
    XMessageBox_setText(box, text);
    XMessageBox_setIcon(box, icon);
    XMessageBox_setStandardButtons(box,
                                   (int)XDialogButtonBoxStandard_Ok |
                                   (int)XDialogButtonBoxStandard_Cancel);
    /* 标准按钮按角色自动发 accepted/rejected → 中继到对话框
       accept()/reject()（Qt QDialogButtonBox 接线惯例）。 */
    XObject_connect_1((XObject*)box->m_buttonBox,
                      (size_t)XDialogButtonBox_accepted_signal(NULL),
                      (XObject*)box, dlgpg_boxAcceptSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)box->m_buttonBox,
                      (size_t)XDialogButtonBox_rejected_signal(NULL),
                      (XObject*)box, dlgpg_boxRejectSlot,
                      XConnectionType_Direct);
    /* 对话框关闭结果 → 状态栏。 */
    XObject_connect_1((XObject*)box,
                      (size_t)XDialog_accepted_signal(NULL),
                      (XObject*)box, dlgpg_msgAcceptedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)box,
                      (size_t)XDialog_rejected_signal(NULL),
                      (XObject*)box, dlgpg_msgRejectedSlot,
                      XConnectionType_Direct);
    /* 居中于页面内容区（XMessageBox_init 默认尺寸 320x140）。 */
    XWidget_setGeometry((XWidget*)box, 220, 160, 320, 140);
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
    /* 便捷函数内部 exec 阻塞（应用模态），真人可交互；autotest 不覆盖。 */
    text = XInputDialog_getText_2(
        s_dlgpg.m_root,
        "输入对话框",
        "请输入名称：",
        XInputDialogEchoMode_Normal,
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
    XWidget_setGeometry((XWidget*)progress, 200, 150, 360, 170);
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
    XWidget_setGeometry((XWidget*)dialog, 210, 150, 340, 170);
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
/** @brief 装配一行：触发按钮 + 说明标签（手工几何并 show）。 */
static void dlgpg_addRow(int row, const char* btnText,
                         const char* noteText, XSlotFunc1 trigger)
{
    XPushButton* button = XPushButton_create(s_dlgpg.m_root, 0);
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
    memset(&s_dlgpg, 0, sizeof(s_dlgpg));
    s_dlgpg.m_status = status;
    s_dlgpg.m_statusUser = user;

    /* 页面根控件：堆对象，父子链级联析构（契约所有权约定）。 */
    s_dlgpg.m_root = XWidget_create(parent, 0);
    if (!s_dlgpg.m_root)
        return NULL;
    XWidget_setGeometry(s_dlgpg.m_root, 0, 0,
                        DLGPG_PAGE_WIDTH, DLGPG_PAGE_HEIGHT);

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
#endif /* DLGPG_BUTTONS_ON */

    return s_dlgpg.m_root;
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

    /* 防错页调用：与 build 登记的根控件不符返回 -1。 */
    if (!page || page != s_dlgpg.m_root)
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
        DLGPG_EXPECT(box != NULL, "消息框堆构造成功");
        if (box) {
            XMessageBox_setTitle(box, "Autotest");
            XMessageBox_setText(box, "消息文本");
            XMessageBox_setIcon(box, XMessageBoxIcon_Information);
            XMessageBox_setStandardButtons(
                box, (int)XDialogButtonBoxStandard_Ok |
                     (int)XDialogButtonBoxStandard_Cancel);
            DLGPG_EXPECT(strcmp(XMessageBox_text(box), "消息文本") == 0,
                         "消息框 text getter 回读");
            DLGPG_EXPECT(strcmp(XMessageBox_title(box), "Autotest") == 0,
                         "消息框 title getter 回读");
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
