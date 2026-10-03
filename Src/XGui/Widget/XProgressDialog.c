/******************************************************************************
 * @file       XProgressDialog.c
 * @brief      进度对话框控件实现（对标 Qt 6.8 QProgressDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setValue 钳位后按 Qt 语义
 *             处理 autoReset/autoClose；cancel() 发射 canceled() 并复位；
 *             forceShow 直接 XWidget_show。setBar 仅存储借用指针。
 *             open_2 对标 QDialog::open(receiver,member)：窗口模态非阻
 *             塞显示 + finished 临时连接 + 关闭事件收口自动断开。
 * @note       本文件不依赖任何平台 API；子控件布局与原生进度面板为后续扩展。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XString.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON && XDIALOG_ON

#include "XProgressDialog.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XFont.h"
#include "XFont8x16.h"
#include "XStringUtils.h"
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif

/* ==================== 内部辅助 ==================== */

/** @brief 对象字模行高（字库位图行高 × 像素缩放；XLabel label_lineHeight
 *         同源口径：scale = pixelSize>0 ? pixelSize : 位图行高）。 */
static int xprogressdialog_fontLineHeight(const XProgressDialog* self)
{
    XFont font;
    const XFontFace* face;
    XFontFaceInfo info;
    int base = XFONT8X16_HEIGHT;
    int scaleNum;
    if (!self) return base;
    font = XWidget_font((XWidget*)self);
    XMemset(&info, 0, sizeof(info));
    face = XFont_face(&font);
    if (face && XFontFace_info_base(face, &font, &info) &&
        info.m_kind == XFontFace_Bitmap && info.m_bitmap.m_height > 0)
        base = info.m_bitmap.m_height;
    scaleNum = XFont_pixelSize(&font) > 0 ? XFont_pixelSize(&font) : base;
    XClassDeinit(&font);
    /* 行高 = 位图行高 × (pixelSize / 位图行高) = 像素字号（同 XLabel
     * label_scaledMetric 的 base×scale 取整口径，恒 ≥1）。 */
    return scaleNum < 1 ? 1 : scaleNum;
}

/** @brief 按对象字模测 UTF-8 单行文本宽（XPainter_textWidthRange 同源，
 *         供内建标签文本参与宽度计算）。 */
static int xprogressdialog_textWidth(const XProgressDialog* self,
                                     const XString* text)
{
    const char* utf8;
    XFont font;
    int width;
    if (!self || !text) return 0;
    utf8 = XString_toUtf8(text);
    if (!utf8 || !utf8[0]) return 0;
    font = XWidget_font((XWidget*)self);
    width = XPainter_textWidthRange(&font, utf8, 0, (int)XStrlen(utf8));
    XClassDeinit(&font);
    return width > 0 ? width : 0;
}

/** @brief 释放并清空拥有型 XString 字段。 */
static void xprogressdialog_freeString(XString** slot)
{
    if (slot && *slot) {
        XClassDelete((XClass*)*slot);
        *slot = NULL;
    }
}

/** @brief 深拷贝 XString；NULL 视为空串。 */
static XString* xprogressdialog_dupString(const XString* src)
{
    if (!src) return XString_create();
    return XString_create_copy(src);
}

/** @brief 发射空参信号（canceled）。 */
static void xprogressdialog_emitVoid(XProgressDialog* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 把值钳位到 [min,max]（min>max 时视为 [max,min]）。 */
static int xprogressdialog_clamp(const XProgressDialog* self, int value)
{
    int lo, hi;
    if (!self) return value;
    lo = self->m_minimum <= self->m_maximum ? self->m_minimum : self->m_maximum;
    hi = self->m_minimum <= self->m_maximum ? self->m_maximum : self->m_minimum;
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/* ==================== 类与实例生命周期 ==================== */

/** @brief 按当前尺寸重排自定义标签与取消按钮（顶部文本行 + 右下按钮，
 *         对标 Qt 进度对话框的子控件布局）。
 *  @details CSD 让位自洽（账本 #2 修复配套）：装饰条画在窗口客户区
 *           顶部，xdlg_applyContentAvoidance 会在 SHOW 时把显式几何的
 *           直接子控件整体再下移「装饰高-已套用」差值。为使本函数在
 *           「init/resize（避让未套用）」与「显示中 setLabel/setCancel
 *           Button（避让已套用）」两种时机都得到同一最终位置，坐标按
 *           已套用/待套用分量拆分：
 *           - 标签顶 y = 12 + m_csdAppliedTop（避让后终位；待套用差值
 *             由避让链补足，不在此预加）；
 *           - 取消钮底锚 y = h - 34 - 待套用差值（避让下移后恰好落到
 *             h-34；已套用时差值 0 即终位，固定尺寸下不会被裁出窗）。 */
static void xprogressdialog_relayout(XProgressDialog* self)
{
    int w;
    int h;
    int applied;
    int pending;
    if (!self) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    applied = self->m_base.m_csdAppliedTop;
    pending = XDialog_decorationTopOffset((const XDialog*)self) - applied;
    if (applied < 0) applied = 0;
    if (pending < 0) pending = 0;
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        int lw = w - 32;
        if (lw < 0) lw = 0;
        XWidget_setGeometry((XWidget*)self->m_label, 16, 12 + applied, lw, 20);
    }
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    if (self->m_cancelButton)
        XWidget_setGeometry((XWidget*)self->m_cancelButton,
                            w - 84, h - 34 - pending, 80, 26);
#endif
}

/** @brief 内容驱动尺寸收口（对标 qprogressdialog.cpp
 *         ensureSizeIsAtLeastSizeHint 的 expandedTo 语义：目标=
 *         sizeHint 与当前尺寸取大后 resize，不设固定约束）。
 *  @details 【口径勘误 2026-10-03 对齐盘点】原实现误按「不可拉伸」
 *           setFixedSize——Qt 源码事实相反：QProgressDialog 无任何
 *           min/max 约束，layout() 注释原文「It is important that a
 *           progress dialog can be made very small if the user
 *           demands it」+ resizeEvent 重排，设计为用户可拉伸对话框。
 *           本类 resizeEvent→relayout 重排链既有，放开约束即对齐。
 *  @details 行模型：标签行（自定义标签按 XLabel_sizeHint 实测；内建
 *           m_labelText 当前无承载控件、按对象字模实测宽度/行高参与，
 *           原生进度面板为后续扩展、尺寸口径先行对齐）+ 行距 8 + 取消
 *           按钮行 26（relayout 的按钮行高；setBar 为简化借用存储、
 *           无承载渲染，不参与高度）+ 上下边距 12。
 *           宽 = max(360, 标签宽 + 32)；高 = max(160, 内容高) +
 *           XDialog_decorationTopOffset——CSD 平台装饰条占客户区顶部，
 *           显式几何子控件整体下移同值，窗高不同步追加即取消钮裁出
 *           窗外。360x160 下限依据：Qt QProgressDialog 常规默认约
 *           350x120 的上沿取整 + demo 进度页实证子控件几何（标签
 *           16,16,328,60 → 宽需 344+16；取消钮 248,120,96,32 → 高需
 *           152+8；账本 #2/#8 量化 100x30 缺 244x122）——hint 作为
 *           expandedTo 的下限参与取大，用户拖小不小于内容下限。 */
static void xprogressdialog_updateSize(XProgressDialog* self)
{
    int width;
    int height;
    int rowH;
    int labelW = 0;
    if (!self) return;
    rowH = xprogressdialog_fontLineHeight(self);
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        XSize hint = XLabel_sizeHint(self->m_label);
        if (hint.width > labelW) labelW = hint.width;
        if (hint.height > rowH) rowH = hint.height;
    } else
#endif
    {
        labelW = xprogressdialog_textWidth(self, self->m_labelText);
    }
    width = labelW + 32; /* 左右边距 16+16（relayout 横向口径同源）。 */
    if (width < 360) width = 360;
    height = 12 + rowH + 8 + 26 + 12;
    if (height < 160) height = 160;
    height += XDialog_decorationTopOffset((const XDialog*)self);
    /* expandedTo(当前)：可见期用户拖大后文本变化不回缩（Qt 同款）。
     * 门禁=isVisible（账本 dialogs-r4 #1/#7 尺寸回归根修）：Qt 原文
     * ensureSizeIsAtLeastSizeHint 即 `if (q->isVisible()) size =
     * size.expandedTo(q->size())`——不可见期（init/setText 装配阶段）
     * 当前尺寸是基类默认 640x480 而非用户意愿，参与取大会把基类默认
     * 永久钉死（r4 实测 640x480+120,100：宽超 280/高超 290，右下
     * ~296x294 空白）；可见期才尊重用户拖大。 */
    if (XWidget_isVisible((XWidget*)self)) {
        if (XWidget_width((XWidget*)self) > width)
            width = XWidget_width((XWidget*)self);
        if (XWidget_height((XWidget*)self) > height)
            height = XWidget_height((XWidget*)self);
    }
    XWidget_resize((XWidget*)self, width, height);
    /* 子控件重排交给 resizeEvent（等值时无操作、不触发，已被避让链
       摆好的子控件位置不被惊动）。 */
}

/** @brief 尺寸变化后重排自定义子控件（对标 Qt 的布局更新）。 */
static void VXProgressDialog_resizeEvent(XWidget* self, XEvent* event)
{
    XProgressDialog* dlg = (XProgressDialog*)self;
    (void)event;
    if (!dlg) return;
    xprogressdialog_relayout(dlg);
}

#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
/** @brief 自定义取消按钮 clicked → cancel()（对标 Qt 的
 *         clicked→canceled→cancel 链路，XGui 合并为一次 cancel 调用）。 */
static void xprogressdialog_cancelClickedSlot(XObject* receiver, XVarList* args)
{
    XProgressDialog* self = (XProgressDialog*)receiver;
    (void)args;
    if (self) XProgressDialog_cancel(self);
}
#endif

/* ==================== 最小时长自动显示定时器（复扫 R-82） ==================== */

/** @brief 定时器状态槽表容量（同屏进度对话框超过该数时新对象退化为一
 *         律不自动显示，行为安全）。 */
#define XPD_TIMER_SLOTS 8

/**
 * @brief 每对话框定时器状态槽（复扫 R-82 配套）。
 * @details 头文件结构体不在本批修复所有权内、无法新增字段，以文件级
 *          静态表按对象指针登记（init 登记/deinit 摘除；init 前先按
 *          同指针清旧，防重复 init 复用内存时残留旧定时器）。
 */
typedef struct XProgressDialogTimerSlot
{
    XProgressDialog* owner; /**< 登记的对话框（NULL=空槽）。 */
    XTimerId timerId;       /**< forceShow 定时器；XTIMER_INVALID_ID=未起。 */
    bool shownOnce;         /**< 对标 Qt shownOnce：已显示过则超时不再强显。 */
    bool durationArmed;     /**< 对标 Qt setValueCalled：首个有效 setValue 已起计时。 */
} XProgressDialogTimerSlot;

static XProgressDialogTimerSlot g_progressTimerSlots[XPD_TIMER_SLOTS];

/** @brief 查对象状态槽；未登记返回 NULL。 */
static XProgressDialogTimerSlot* xpd_slotFind(XProgressDialog* self)
{
    int i;
    if (!self) return NULL;
    for (i = 0; i < XPD_TIMER_SLOTS; ++i) {
        if (g_progressTimerSlots[i].owner == self)
            return &g_progressTimerSlots[i];
    }
    return NULL;
}

/** @brief 登记对象状态槽（重复 init 复用同内存时先清旧定时器）。 */
static void xpd_slotAcquire(XProgressDialog* self)
{
    XProgressDialogTimerSlot* slot;
    int i;
    if (!self) return;
    slot = xpd_slotFind(self);
    if (!slot) {
        for (i = 0; i < XPD_TIMER_SLOTS; ++i) {
            if (!g_progressTimerSlots[i].owner) {
                slot = &g_progressTimerSlots[i];
                break;
            }
        }
    }
    if (!slot) return; /* 表满：退化为不自动显示。 */
    if (slot->owner == self && slot->timerId != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, slot->timerId);
    slot->owner = self;
    slot->timerId = XTIMER_INVALID_ID;
    slot->shownOnce = false;
    slot->durationArmed = false;
}

/** @brief 摘除对象状态槽（停定时器并归还槽位）。 */
static void xpd_slotRelease(XProgressDialog* self)
{
    XProgressDialogTimerSlot* slot = xpd_slotFind(self);
    if (!slot) return;
    if (slot->timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, slot->timerId);
        slot->timerId = XTIMER_INVALID_ID;
    }
    slot->owner = NULL;
}

/** @brief 起最小时长计时（对标 Qt forceTimer->start(showTime)）。 */
static void xpd_timerArm(XProgressDialog* self,
                         XProgressDialogTimerSlot* slot)
{
    if (!self || !slot) return;
    if (slot->timerId != XTIMER_INVALID_ID)
        return; /* 已在计时（对标 Qt restart 前先 stop 的幂等口径）。 */
    if (self->m_minimumDuration <= 0) {
        /* 时长 0：对标 Qt 的 0 间隔定时器即时超时——首个 setValue 立即显示。 */
        XProgressDialog_forceShow(self);
        return;
    }
    slot->timerId = XObject_startTimer_ms(
        (XObject*)self, (uint64_t)self->m_minimumDuration,
        XTimerType_PreciseTimer);
}

/** @brief 停最小时长计时。 */
static void xpd_timerStop(XProgressDialog* self,
                          XProgressDialogTimerSlot* slot)
{
    if (!self || !slot) return;
    if (slot->timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, slot->timerId);
        slot->timerId = XTIMER_INVALID_ID;
    }
}

/** @brief 定时器超时 → 对标 Qt forceShow 槽：自动显示对话框。 */
static void VXProgressDialog_timerEvent(XObject* object, XTimerEvent* event)
{
    XProgressDialog* self = (XProgressDialog*)object;
    XProgressDialogTimerSlot* slot =
        self ? xpd_slotFind(self) : NULL;

    if (slot && slot->timerId != XTIMER_INVALID_ID &&
        XTimerEvent_timerId(event) == slot->timerId) {
        xpd_timerStop(self, slot);
        XProgressDialog_forceShow(self);
        return;
    }
    XClass_Parent(XDialog, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

/** @brief 显示事件：按当前装饰/文本态重算固定尺寸（对标 XMessageBox
 *         showEvent 末尾 updateSize 同型；P0 消息族链路收口点），一经
 *         显示停表（对标 Qt showEvent 的 forceTimer->stop()——超时强显
 *         不再触发）。
 *  @details 顺序敏感：updateSize（尺寸变化才触发 resizeEvent→relayout，
 *           坐标按「待套用避让」预摆）先于父类 showEvent（CSD 避让链
 *           补足差值），两段拼出最终位置，见 xprogressdialog_relayout
 *           的分量拆分注。 */
static void VXProgressDialog_showEvent(XWidget* self, XEvent* event)
{
    XProgressDialog* dlg = (XProgressDialog*)self;
    XProgressDialogTimerSlot* slot;
    if (dlg && event && XEvent_type(event) == XEVENT_TYPE_SHOW)
        xprogressdialog_updateSize(dlg);
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    slot = dlg ? xpd_slotFind(dlg) : NULL;
    if (slot)
        xpd_timerStop(dlg, slot);
}

/** @brief open_2 收口清理：断开临时连接并清空记录字段（对标
 *         QDialog::open 的 receiverToDisconnectOnClose 断开收尾；
 *         XMessageBox openCleanup 同型）。未 open 过或已清理时为空
 *         操作（幂等）。 */
static void xpd_openCleanup(XProgressDialog* self)
{
    if (!self) return;
    if (self->m_openReceiver && self->m_openMember) {
        XObject_disconnect_1((XObject*)self,
                             (size_t)XDialog_finished_signal,
                             self->m_openReceiver, self->m_openMember);
    }
    self->m_openReceiver = NULL;
    self->m_openMember = NULL;
}

/** @brief 关闭事件：先走父类收口（[×] 经 reject→done 发射 finished——
 *         open_2 的 receiver 恰回调一次），随后断开 open_2 临时连接
 *         （对标 XMessageBox closeEvent 的健壮性收口：Qt 关闭路径自身
 *         不断开，此处按头文件契约「关闭时自动断开」补齐；事件被
 *         ignore 的未关闭不发 finished、连接保持武装）。 */
static void VXProgressDialog_closeEvent(XWidget* self, XEvent* event)
{
    XClass_Parent(XDialog, EXWidget_CloseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (self && event && event->accepted)
        xpd_openCleanup((XProgressDialog*)self);
}

/** @brief 释放对话框自有拥有字段，再委托父类。 */
static void VXProgressDialog_deinit(XProgressDialog* self)
{
    if (!self) return;
    /* 复扫 R-82 配套：停掉自动显示定时器并摘除状态槽登记。 */
    xpd_slotRelease(self);
    xprogressdialog_freeString(&self->m_labelText);
    xprogressdialog_freeString(&self->m_cancelButtonText);
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        XClassDelete((XClass*)self->m_label);
        self->m_label = NULL;
    }
#endif
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    if (self->m_cancelButton) {
        XClassDelete((XClass*)self->m_cancelButton);
        self->m_cancelButton = NULL;
    }
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XProgressDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProgressDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXProgressDialog_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXProgressDialog_resizeEvent);
    /* 复扫 R-82：最小时长自动显示定时器接线（超时强显 + 显示即停表）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXProgressDialog_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXProgressDialog_showEvent);
    /* open_2 收口：关闭事件接受后自动断开临时连接。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VXProgressDialog_closeEvent);
    return XVTABLE_DEFAULT;
}

void XProgressDialog_init(XProgressDialog* self, XWidget* parent,
                          XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* P0 消息族口径（对标 QProgressDialog 内容驱动不可拉伸；同型先例
     * XMessageBox_init 的 MSWindowsFixedSizeDialogHint）：补固定尺寸
     * 窗口提示位，平台装饰/窗口管理器按不可拉伸对话框处理。窗口类型
     * 位不在此设——XDialog_init 对无类型位叠加 Dialog，尊重调用方。 */
    flags |= (XWidgetFlags)XWindowType_MSWindowsFixedSizeDialogHint;
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XProgressDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minimum = 0;
    self->m_maximum = 100;
    self->m_value = 0;
    self->m_minimumDuration = 4000;
    self->m_wasCanceled = false;
    self->m_autoReset = true;
    self->m_autoClose = true;
    self->m_labelText = NULL;
    self->m_cancelButtonText = NULL;
    self->m_bar = NULL;
    /* 账本 dialogs-r2 #5：默认窗口标题「正在执行」——CSD 无 WM 环境
     * 标题条只余 4 钮无文本（a-50），同页消息盒均有标题观感不统一。
     * 对标偏差登记：Qt 6.8.3 qprogressdialog.cpp 全文无 setWindowTitle
     * （标题留空），此处为 XGui CSD 形态可辨识性补齐，setWindowTitle
     * 随时可覆盖。 */
    {
        XString* title = XString_create_utf8("正在执行");
        if (title) {
            XWidget_setWindowTitle((XWidget*)self, title);
            XClassDelete((XClass*)title);
        }
    }
    /* 账本 #2/#8 根修：创建即按内容收口固定尺寸（XWidget_init 的有父
     * 分支预置子控件默认 100x30 曾让本对话框落成 CSD 空壳），显示时
     * showEvent 再按真实装饰态重算。 */
    xprogressdialog_updateSize(self);
    /* 复扫 R-82 配套：登记自动显示定时器状态槽。 */
    xpd_slotAcquire(self);
}

void XProgressDialog_init_full(XProgressDialog* self, const XString* labelText,
                               const XString* cancelButtonText, int minimum,
                               int maximum, XWidget* parent)
{
    XProgressDialog_init(self, parent, 0);
    if (!self) return;
    XProgressDialog_setRange(self, minimum, maximum);
    XProgressDialog_setLabelText(self, labelText);
    XProgressDialog_setCancelButtonText(self, cancelButtonText);
}

XProgressDialog* XProgressDialog_create_ex(XMemoryType memory, XWidget* parent,
                                           XWidgetFlags flags)
{
    XProgressDialog* self =
        (XProgressDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XProgressDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 范围与值 ==================== */

void XProgressDialog_setRange(XProgressDialog* self, int minimum, int maximum)
{
    int tmp;
    if (!self) return;
    if (minimum > maximum) {
        tmp = minimum;
        minimum = maximum;
        maximum = tmp;
    }
    self->m_minimum = minimum;
    self->m_maximum = maximum;
    self->m_value = xprogressdialog_clamp(self, self->m_value);
}

void XProgressDialog_setMinimum(XProgressDialog* self, int minimum)
{
    if (!self) return;
    XProgressDialog_setRange(self, minimum, self->m_maximum);
}

void XProgressDialog_setMaximum(XProgressDialog* self, int maximum)
{
    if (!self) return;
    XProgressDialog_setRange(self, self->m_minimum, maximum);
}

int XProgressDialog_minimum(const XProgressDialog* self)
{ return self ? self->m_minimum : 0; }

int XProgressDialog_maximum(const XProgressDialog* self)
{ return self ? self->m_maximum : 100; }

void XProgressDialog_setValue(XProgressDialog* self, int progress)
{
    XProgressDialogTimerSlot* slot;
    if (!self) return;
    slot = xpd_slotFind(self);
    self->m_value = xprogressdialog_clamp(self, progress);
    /* 复扫 R-82：minimumDuration 此前为纯存储。对标 Qt
     * QProgressDialog::setValue——首个有效调用起按 minimumDuration
     * 计时，超时经 forceShow 槽自动显示（Qt 6.8 的
     * forceTimer->start(d->showTime) 分支）。已取消（wasCanceled）
     * 后不再自动显示，与 Qt 的 cancellationFlag 门禁同口径；reset()
     * 复位后恢复。 */
    if (slot && !slot->durationArmed && !self->m_wasCanceled) {
        slot->durationArmed = true;
        xpd_timerArm(self, slot);
    }
    /* Qt：入参等于 maximum 且 autoReset 时复位。 */
    if (progress == self->m_maximum && self->m_autoReset)
        XProgressDialog_reset(self);
}

int XProgressDialog_value(const XProgressDialog* self)
{ return self ? self->m_value : 0; }

void XProgressDialog_reset(XProgressDialog* self)
{
    XProgressDialogTimerSlot* slot;
    if (!self) return;
    /* 复扫 R-82 配套：对标 Qt reset——停计时并清 shownOnce/
     * setValueCalled，下一轮 setValue 重新起计时、超时可再强显。 */
    slot = xpd_slotFind(self);
    if (slot) {
        xpd_timerStop(self, slot);
        slot->shownOnce = false;
        slot->durationArmed = false;
    }
    if (self->m_autoClose) {
        XWidget_setVisible((XWidget*)self, false);
        /* autoClose 隐藏后标脏原矩形：子控件形态对话框隐藏不调度
           重绘，屏幕残留最后一帧鬼影（同 XDialog_done 注）。 */
        XWidget_update((XWidget*)self);
    }
    self->m_value = self->m_minimum;
    self->m_wasCanceled = false;
}

/* ==================== 文本与取消 ==================== */

void XProgressDialog_setLabelText(XProgressDialog* self, const XString* text)
{
    if (!self) return;
#if XFRAME_ON && XLABEL_ON
    /* 对标 Qt：存在自定义标签时文本转发给标签。 */
    if (self->m_label) {
        XLabel_setText(self->m_label, text);
        /* 文本变化重算固定尺寸（标签 sizeHint 随文本变化）。 */
        xprogressdialog_updateSize(self);
        return;
    }
#endif
    xprogressdialog_freeString(&self->m_labelText);
    self->m_labelText = xprogressdialog_dupString(text);
    /* 对标 QMessageBox::setText 末尾 updateSize：文本变化重算固定尺寸
     * （内建标签文本宽度参与宽度计算，见 xprogressdialog_updateSize）。 */
    xprogressdialog_updateSize(self);
}

XString* XProgressDialog_labelText(const XProgressDialog* self)
{
    if (!self) return XString_create();
#if XFRAME_ON && XLABEL_ON
    if (self->m_label)
        return xprogressdialog_dupString(XLabel_text(self->m_label));
#endif
    return xprogressdialog_dupString(self->m_labelText);
}

void XProgressDialog_setCancelButtonText(XProgressDialog* self,
                                         const XString* text)
{
    if (!self) return;
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    /* 对标 Qt：存在自定义取消按钮时文本转发给按钮。 */
    if (self->m_cancelButton) {
        XAbstractButton_setText((XAbstractButton*)self->m_cancelButton, text);
        return;
    }
#endif
    xprogressdialog_freeString(&self->m_cancelButtonText);
    self->m_cancelButtonText = xprogressdialog_dupString(text);
}

#if XFRAME_ON && XLABEL_ON
void XProgressDialog_setLabel(XProgressDialog* self, XLabel* label)
{
    if (!self || self->m_label == label) return;
    /* 对标 Qt setLabel：删除旧标签并接管新标签所有权。 */
    if (self->m_label) {
        XClassDelete((XClass*)self->m_label);
        self->m_label = NULL;
    }
    self->m_label = label;
    if (self->m_label) {
        XWidget_setParentPlain((XWidget*)self->m_label, (XWidget*)self);
        XWidget_show((XWidget*)self->m_label);
        /* 子控件变化重算固定尺寸（sizeHint 实测参与）+ 重排。 */
        xprogressdialog_updateSize(self);
        xprogressdialog_relayout(self);
    }
}

XLabel* XProgressDialog_label(const XProgressDialog* self)
{ return self ? self->m_label : NULL; }
#endif /* XFRAME_ON && XLABEL_ON */

#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
void XProgressDialog_setCancelButton(XProgressDialog* self,
                                     XPushButton* button)
{
    if (!self || self->m_cancelButton == button) return;
    /* 对标 Qt setCancelButton：删除旧按钮、接管新按钮所有权并接线
     * clicked → cancel()。 */
    if (self->m_cancelButton) {
        XObject_disconnect_1((XObject*)self->m_cancelButton,
                             XSignal(XAbstractButton_clicked_signal),
                             (XObject*)self, xprogressdialog_cancelClickedSlot);
        XClassDelete((XClass*)self->m_cancelButton);
        self->m_cancelButton = NULL;
    }
    self->m_cancelButton = button;
    if (self->m_cancelButton) {
        XWidget_setParentPlain((XWidget*)self->m_cancelButton, (XWidget*)self);
        XWidget_show((XWidget*)self->m_cancelButton);
        if (self->m_cancelButtonText) {
            XAbstractButton_setText((XAbstractButton*)self->m_cancelButton,
                                    self->m_cancelButtonText);
        }
        XObject_connect_1((XObject*)self->m_cancelButton,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)self,
                          xprogressdialog_cancelClickedSlot,
                          XConnectionType_Direct);
        /* 子控件变化重算固定尺寸 + 重排（取消行为尺寸模型的常备行）。 */
        xprogressdialog_updateSize(self);
        xprogressdialog_relayout(self);
    }
}

XPushButton* XProgressDialog_cancelButton(const XProgressDialog* self)
{ return self ? self->m_cancelButton : NULL; }
#endif /* XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */

void XProgressDialog_setBar(XProgressDialog* self, XProgressBar* bar)
{ if (self) self->m_bar = bar; }

void XProgressDialog_cancel(XProgressDialog* self)
{
    if (!self) return;
    /* Qt 可观察效果：canceled() 发射 → cancel() → 复位 + wasCanceled=true；
     * Qt 经 forceHide 保证取消时无条件隐藏（即使 autoClose=false）。 */
    xprogressdialog_emitVoid(self, (size_t)XProgressDialog_canceled_signal);
    XProgressDialog_reset(self);
    XWidget_setVisible((XWidget*)self, false);
    /* 无条件隐藏后标脏原矩形，清除屏幕残影（同 XDialog_done 注）。 */
    XWidget_update((XWidget*)self);
    self->m_wasCanceled = true;
}

bool XProgressDialog_wasCanceled(const XProgressDialog* self)
{ return self ? self->m_wasCanceled : false; }

/* ==================== 行为属性 ==================== */

void XProgressDialog_setAutoReset(XProgressDialog* self, bool reset)
{ if (self) self->m_autoReset = reset; }

bool XProgressDialog_autoReset(const XProgressDialog* self)
{ return self ? self->m_autoReset : true; }

void XProgressDialog_setAutoClose(XProgressDialog* self, bool close)
{ if (self) self->m_autoClose = close; }

bool XProgressDialog_autoClose(const XProgressDialog* self)
{ return self ? self->m_autoClose : true; }

void XProgressDialog_setMinimumDuration(XProgressDialog* self, int ms)
{
    XProgressDialogTimerSlot* slot;
    if (!self) return;
    self->m_minimumDuration = ms;
    /* 复扫 R-82 配套：对标 Qt setMinimumDuration——尚未产生进度
     * （值仍在 minimum）且计时已起时，改时长即按新时长重启。 */
    slot = xpd_slotFind(self);
    if (slot && slot->durationArmed && self->m_value == self->m_minimum) {
        xpd_timerStop(self, slot);
        if (!self->m_wasCanceled)
            xpd_timerArm(self, slot);
    }
}

int XProgressDialog_minimumDuration(const XProgressDialog* self)
{ return self ? self->m_minimumDuration : 4000; }

void XProgressDialog_forceShow(XProgressDialog* self)
{
    XProgressDialogTimerSlot* slot;
    if (!self) return;
    slot = xpd_slotFind(self);
    if (slot) {
        /* 对标 Qt forceShow：先停表；已显示过（shownOnce）则不再强显
         * （reset 复位 shownOnce 后可再次显示）。 */
        xpd_timerStop(self, slot);
        if (slot->shownOnce)
            return;
        slot->shownOnce = true;
    }
    XWidget_show((XWidget*)self);
}

/* ==================== 非阻塞打开（对标 QDialog::open） ==================== */

void XProgressDialog_open_2(XProgressDialog* self, XObject* receiver,
                            XSlotFunc1 member)
{
    if (!self || !member) return;
    /* 重复 open 防叠连：旧记录尚存时先按旧记录断开旧连接（XMessageBox
     * open_2 同型；disconnect_1 只移除首个匹配，直接叠连会残留旧连接，
     * receiver 每次收口被多路误触发）。 */
    if (self->m_openReceiver || self->m_openMember)
        xpd_openCleanup(self);
    self->m_openReceiver = receiver;
    self->m_openMember = member;
    if (receiver) {
        XObject_connect_1((XObject*)self, (size_t)XDialog_finished_signal,
                          receiver, member, XConnectionType_Direct);
    }
    /* 对标 QDialog::open：窗口模态显示并立即返回。 */
    XDialog_open(&self->m_base);
}

/* ==================== 信号 ==================== */

void* XProgressDialog_canceled_signal(XProgressDialog* self)
{
    xprogressdialog_emitVoid(self, (size_t)XProgressDialog_canceled_signal);
    return (void*)(size_t)XProgressDialog_canceled_signal;
}

#endif /* XWIDGET_ON && XDIALOG_ON */
