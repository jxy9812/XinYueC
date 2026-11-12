/******************************************************************************
 * @file       XProgressDialog.c
 * @brief      进度对话框控件实现（对标 Qt 6.8 QProgressDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setValue 越界入参不改变
 *             当前值（qprogressbar.cpp:277-294），达峰且 autoReset 时
 *             复位为 minimum-1；cancel() 发射 canceled() 并复位；
 *             forceShow 直接 XWidget_show。setBar 接管进度条所有权（旧
 *             进度条删除、新进度条成为子控件；NULL 为无操作）。
 *             open_2 对标 QDialog::open(receiver,member)：窗口模态非阻
 *             塞显示 + finished 临时连接 + 关闭事件收口自动断开。
 * @note       本文件不依赖任何平台 API；子控件布局与原生进度面板为后续扩展。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include <limits.h>
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
#include "XDateTime.h"        /* m_startTime 起表：单调毫秒（同源
                               * XFileDialog.c xff_nowMs 的时间源纪律）。 */
#include "XCoreApplication.h" /* 模态 setValue 泵事件（P0-03 对标
                               * QCoreApplication::processEvents）。 */
#if XPROGRESSBAR_ON
#include "XProgressBar.h"
#endif
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

/** @brief 值状态的忙态哨兵（对标 QProgressBar::reset：value = minimum-1，
 *         qprogressbar.cpp:220-228；minimum==INT_MIN 时 Qt 保留 INT_MIN）。 */
static int xprogressdialog_busyValue(int minimum)
{
    if (minimum == INT_MIN) return INT_MIN;
    return minimum - 1;
}

/** @brief 检查值是否落在 Qt 认可的范围外（QProgressBar::setValue 的
 *         越界拒绝条件：`(value > max || value < min) && (max != 0 ||
 *         min != 0)`——即 0..0 的"未定态"范围接受任意值）。 */
static bool xprogressdialog_valueOutOfRange(const XProgressDialog* self,
                                            int value)
{
    if (!self) return false;
    if (self->m_maximum == 0 && self->m_minimum == 0) return false;
    return value > self->m_maximum || value < self->m_minimum;
}

/** @brief estimate 启发式的最短观察窗（ms）。
 *  @details 对标 qprogressdialog.cpp:33 的 `minWaitTime = 50ms`：
 *           操作预期耗时的预测至少要等 50ms 观察窗才做（常量必须与
 *           Qt 同值，否则提前显示的时机又偏）。 */
#define XPROGRESSDIALOG_MIN_WAIT_MS 50

/** @brief 把范围/值同步到自有进度条（setBar 接管后，Qt 的进度条是状态
 *         载体；本库状态在对话框自身字段，故按字段回灌保持可视一致）。 */
static void xprogressdialog_syncBar(const XProgressDialog* self)
{
    if (!self || !self->m_bar) return;
#if XPROGRESSBAR_ON
    XProgressBar_setRange(self->m_bar, self->m_minimum, self->m_maximum);
    /* 越界值（含 minimum-1 忙态）按 reset 灌入：XProgressBar 的
     * setValue 不接受越界值，reset 才是 minimum-1 的入口。 */
    if (xprogressdialog_valueOutOfRange(self, self->m_value))
        XProgressBar_reset(self->m_bar);
    else
        XProgressBar_setValue(self->m_bar, self->m_value);
#endif
}

/* ==================== 类与实例生命周期 ==================== */

/** @brief 进度条行高（对标 QProgressBar 的 sizeHint 高度量级；XProgressBar
 *         实测首选高 20，见 xprogressdialog_sizeForContent 的 Qt 公式映射）。 */
#define XPD_BAR_ROW_H 20

/** @brief 按当前尺寸重排自定义标签/进度条/取消按钮（顶部文本行 +
 *         进度条行 + 右下按钮，对标 Qt 进度对话框的子控件布局）。
 *  @details CSD 让位自洽（账本 #2 修复配套）：装饰条画在窗口客户区
 *           顶部，xdlg_applyContentAvoidance 会在 SHOW 时把显式几何的
 *           直接子控件整体再下移「装饰高-已套用」差值。为使本函数在
 *           「init/resize（避让未套用）」与「显示中 setLabel/setCancel
 *           Button（避让已套用）」两种时机都得到同一最终位置，坐标按
 *           已套用/待套用分量拆分：
 *           - 标签顶 y = 12 + m_csdAppliedTop（避让后终位；待套用差值
 *             由避让链补足，不在此预加）；
 *           - 进度条行紧随标签行（Qt 的 QVBoxLayout 顺序：label → bar →
 *             按钮行）；
 *           - 取消钮底锚 y = h - 34 - 待套用差值（避让下移后恰好落到
 *             h-34；已套用时差值 0 即终位，固定尺寸下不会被裁出窗）。 */
static void xprogressdialog_relayout(XProgressDialog* self)
{
    int w;
    int h;
    int applied;
    int pending;
    int rowH;
    int barY;
    if (!self) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    applied = self->m_base.m_csdAppliedTop;
    pending = XDialog_decorationTopOffset((const XDialog*)self) - applied;
    if (applied < 0) applied = 0;
    if (pending < 0) pending = 0;
    rowH = xprogressdialog_fontLineHeight(self);
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        int lw = w - 32;
        if (lw < 0) lw = 0;
        XWidget_setGeometry((XWidget*)self->m_label, 16,
                            12 + applied, lw, rowH);
    }
#endif
    barY = 12 + applied + rowH + 8;
    if (self->m_bar) {
        int bw = w - 32;
        if (bw < 0) bw = 0;
        XWidget_setGeometry((XWidget*)self->m_bar, 16, barY, bw,
                            XPD_BAR_ROW_H);
    }
#if XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
    if (self->m_cancelButton)
        XWidget_setGeometry((XWidget*)self->m_cancelButton,
                            w - 84, h - 34 - pending, 80, 26);
#endif
}

/** @brief 按内容模型算首选尺寸（updateSize 与 sizeHint 槽同源同参）。
 *  @details 公式对齐 QProgressDialog::sizeHint（qprogressdialog.cpp:
 *           668-687，本机 Qt 6.8.3 实测空标签 200x102）：
 *           `width  = max(200, labelW + marginLeft + marginRight)`
 *           `height = marginBottom*2 + barH + labelH + spacing
 *                     + (cancel ? cancelH + spacing : 0)`
 *           本库无 QStyle 度量，取与自排布模型同源的常量：左右边距
 *           16+16（relayout 横向口径）、上下边距 12（PM_LayoutBottomMargin
 *           量级）、行距 8（PM_LayoutVerticalSpacing 量级）、进度条行 20
 *           （QProgressBar sizeHint 高）、取消行 26（relayout 按钮高）。
 *           取消行**恒计**：Qt 构造即建默认取消按钮（Qt 语言包 "Cancel"），
 *           其 sizeHint 因此恒含该行；本库无内建按钮但保留同一预留模型，
 *           空标签时即 12*2 + 20 + 16 + 8 + 26 + 8 = 102。
 *           **不含** CSD 装饰让位（那是窗几何口径，由 updateSize 追加）。
 *  @param self 目标对话框；NULL 时输出 0。
 *  @param outW 输出宽（非 NULL）。
 *  @param outH 输出高（非 NULL）。 */
static void xprogressdialog_sizeForContent(const XProgressDialog* self,
                                           int* outW, int* outH)
{
    int width;
    int height;
    int rowH;
    int labelW = 0;
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (!self) return;
    rowH = xprogressdialog_fontLineHeight(self);
#if XFRAME_ON && XLABEL_ON
    if (self->m_label) {
        XSize hint = XWidget_sizeHint_base((const XWidget*)self->m_label);
        if (hint.width > labelW) labelW = hint.width;
        if (hint.height > rowH) rowH = hint.height;
    } else
#endif
    {
        labelW = xprogressdialog_textWidth(self, self->m_labelText);
    }
    width = labelW + 32; /* 左右边距 16+16（relayout 横向口径同源）。 */
    if (width < 200) width = 200;
    height = 12 * 2 + XPD_BAR_ROW_H + rowH + 8 + 26 + 8;
    if (outW) *outW = width;
    if (outH) *outH = height;
}

/** @brief 内容驱动尺寸收口（对标 qprogressdialog.cpp
 *         ensureSizeIsAtLeastSizeHint 的 expandedTo 语义：目标=
 *         sizeHint 与当前尺寸取大后 resize，不设固定约束）。
 *  @details Qt 6.8 源码事实：QProgressDialog 无任何 min/max 约束，
 *           `layout()` 注释原文「It is important that a progress dialog
 *           can be made very small if the user demands it」+ resizeEvent
 *           重排，是用户可拉伸对话框（本机实测 minimum 0 / maximum
 *           16777215、resize 生效）；故本类不 setFixedSize、也不补
 *           MSWindowsFixedSizeDialogHint（r2 撤销 r1 的固定尺寸位）。
 *  @details 尺寸模型见 xprogressdialog_sizeForContent；窗高再追加
 *           XDialog_decorationTopOffset——CSD 平台装饰条占客户区顶部，
 *           显式几何子控件整体下移同值，窗高不同步追加即取消钮裁出
 *           窗外。 */
static void xprogressdialog_updateSize(XProgressDialog* self)
{
    int width;
    int height;
    if (!self) return;
    xprogressdialog_sizeForContent(self, &width, &height);
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

/** @brief 首选尺寸虚槽（对标 QProgressDialog::sizeHint 重写）。
 *  @details 按本类内容模型（xprogressdialog_sizeForContent，公式对齐
 *           qprogressdialog.cpp:668-687）算出首选尺寸并写入 m_sizeHint
 *           缓存，再回读基类缓存——查询即重算，等价 Qt「sizeHint 是
 *           虚函数、每次查询按内容求值」。
 *  @note   空标签时量值 200x102，与本机 Qt 6.8.3 实测一致（r2 对齐，
 *           账本 233）；首选尺寸只是提示，不约束实际几何（Qt 明确允许
 *           进度对话框被拖得很小）。 */
static XSize VXProgressDialog_sizeHintSlot(const XWidget* self)
{
    XSize out;
    XSize hint;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    XSize_init(&hint, 0, 0);
    xprogressdialog_sizeForContent((const XProgressDialog*)self,
                                   &hint.width, &hint.height);
    XWidget_setSizeHint((XWidget*)self, &hint);
    out = XClass_Parent(XWidget, EXWidget_SizeHint,
                        XSize (*)(const XWidget*))(self);
    return out;
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

/* ==================== 最小时长自动显示（对标 forceTimer/shownOnce） ==================== */

/** @brief 起最小时长计时（对标 Qt forceTimer->start(showTime)）。
 *  @details 计时器标识存在对象字段（m_forceTimer），对齐 Qt 的
 *           QProgressDialogPrivate::forceTimer。旧实现用文件级静态槽表
 *           （上限 8 个对话框，表满后新对话框退化为"永不自动显示"），
 *           属于结构体不便扩展时代的权宜计；本批头文件可改后已回归
 *           每对象一字段，去掉全局表与容量上限。 */
static void xpd_timerArm(XProgressDialog* self)
{
    if (!self) return;
    if (self->m_forceTimer != XTIMER_INVALID_ID)
        return; /* 已在计时（对标 Qt restart 前先 stop 的幂等口径）。 */
    if (self->m_minimumDuration <= 0) {
        /* 时长 <=0：对标 Qt 的 0 间隔定时器即时超时——首个 setValue
         * 立即显示（Qt 的 minimumDuration 允许负值原样存储，起定时器时
         * 按 0 处理）。 */
        XProgressDialog_forceShow(self);
        return;
    }
    self->m_forceTimer = XObject_startTimer(
        (XObject*)self, (uint64_t)self->m_minimumDuration,
        XTimerType_PreciseTimer);
}

/** @brief 停最小时长计时。 */
static void xpd_timerStop(XProgressDialog* self)
{
    if (!self) return;
    if (self->m_forceTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_forceTimer);
        self->m_forceTimer = XTIMER_INVALID_ID;
    }
}

/** @brief 定时器超时 → 对标 Qt forceShow 槽：自动显示对话框。 */
static void VXProgressDialog_timerEvent(XObject* object, XTimerEvent* event)
{
    XProgressDialog* self = (XProgressDialog*)object;

    if (self && self->m_forceTimer != XTIMER_INVALID_ID &&
        XTimerEvent_timerId(event) == self->m_forceTimer) {
        xpd_timerStop(self);
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
    if (dlg && event && XEvent_type(event) == XEVENT_TYPE_SHOW)
        xprogressdialog_updateSize(dlg);
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (dlg)
        xpd_timerStop(dlg);
}

/** @brief open_2 收口清理：断开临时连接并清空记录字段（对标
 *         QDialog::open 的 receiverToDisconnectOnClose 断开收尾；
 *         XMessageBox openCleanup 同型）。未 open 过或已清理时为空
 *         操作（幂等）。 */
static void xpd_openCleanup(XProgressDialog* self)
{
    if (!self) return;
    if (self->m_openReceiver && self->m_openMember) {
        XObject_disconnect((XObject*)self,
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
    if (self && event && event->m_accepted)
        xpd_openCleanup((XProgressDialog*)self);
}

/** @brief 释放对话框自有拥有字段，再委托父类。 */
static void VXProgressDialog_deinit(XProgressDialog* self)
{
    if (!self) return;
    /* 停掉自动显示定时器（字段随对象销毁，无需再摘槽位）。 */
    xpd_timerStop(self);
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
    /* setBar 接管的进度条归本对话框所有（对标 Qt 析构 delete d->bar；
     * r2 对齐，账本 232）。 */
    if (self->m_bar) {
        XClassDelete((XClass*)self->m_bar);
        self->m_bar = NULL;
    }
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XProgressDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProgressDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXProgressDialog_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXProgressDialog_resizeEvent);
    /* 对标 QProgressDialog::sizeHint 重写（查询即按内容重算）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXProgressDialog_sizeHintSlot);
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
    /* Qt 6.8.3 QProgressDialog 无固定尺寸约束（minimum 0 / maximum
     * 16777215、resize 生效、layout 注释要求可拖得很小）⇒ 不补
     * MSWindowsFixedSizeDialogHint（r2 撤销 r1 的固定尺寸位，账本 233）。
     * 窗口类型位不在此设——XDialog_init 对无类型位叠加 Dialog，尊重调用方。 */
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XProgressDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minimum = 0;
    self->m_maximum = 100;
    /* 忙态哨兵 = minimum-1（对标 QProgressBar 构造 value=-1 /
     * QProgressDialog::value() 构造即 -1，本机 6.8.3 实测）。 */
    self->m_value = xprogressdialog_busyValue(self->m_minimum);
    self->m_minimumDuration = 4000;
    /* 对标 Qt init 的 starttime.start()（qprogressdialog.cpp:88）：构造
     * 即起表，首个有效 setValue 走 estimate 分支时 elapsed 从构造起算。 */
    self->m_startTime = XDateTime_currentMSecsSinceEpoch();
    self->m_forceTimer = XTIMER_INVALID_ID;
    self->m_wasCanceled = false;
    self->m_autoReset = true;
    self->m_autoClose = true;
    self->m_shownOnce = false;
    self->m_durationArmed = false;
    self->m_processingEvents = false;
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
    if (!self) return;
    /* 对标 QProgressBar::setRange（qprogressbar.cpp:315-327）：
     *   d->minimum = minimum;
     *   d->maximum = qMax(minimum, maximum);
     *   if (d->value < qint64(d->minimum) - 1 || d->value > d->maximum)
     *       reset();   // value = minimum-1
     * 即 min>max 时把 **max 钳到 min**（不是交换），值越界按忙态复位。
     * 本机 Qt 6.8.3 实测 setRange(50,20) → min=max=50、value=49。 */
    if (minimum == self->m_minimum && maximum == self->m_maximum) {
        /* Qt 同值不做任何事（但值越界检查也一并跳过——与 Qt 一致）。 */
        return;
    }
    self->m_minimum = minimum;
    self->m_maximum = minimum > maximum ? minimum : maximum;
    if (self->m_value < xprogressdialog_busyValue(self->m_minimum) ||
        self->m_value > self->m_maximum)
        self->m_value = xprogressdialog_busyValue(self->m_minimum);
    xprogressdialog_syncBar(self);
}

void XProgressDialog_setMinimum(XProgressDialog* self, int minimum)
{
    if (!self) return;
    /* 对标 QProgressBar::setMinimum：setRange(minimum, qMax(maximum, minimum))。 */
    XProgressDialog_setRange(self, minimum,
                             self->m_maximum > minimum ? self->m_maximum
                                                       : minimum);
}

void XProgressDialog_setMaximum(XProgressDialog* self, int maximum)
{
    if (!self) return;
    /* 对标 QProgressBar::setMaximum：setRange(qMin(minimum, maximum), maximum)。 */
    XProgressDialog_setRange(self, self->m_minimum < maximum ? self->m_minimum
                                                             : maximum,
                             maximum);
}

int XProgressDialog_minimum(const XProgressDialog* self)
{ return self ? self->m_minimum : 0; }

int XProgressDialog_maximum(const XProgressDialog* self)
{ return self ? self->m_maximum : 100; }

void XProgressDialog_setValue(XProgressDialog* self, int progress)
{
    if (!self) return;
    /* 对标 QProgressDialog::setValue（qprogressdialog.cpp:613-664）：
     * ① `if (d->setValueCalled && progress == d->bar->value()) return;`
     *    ——本库以 m_durationArmed 承载 setValueCalled；
     * ② QProgressBar::setValue：越界值**不改变当前值**
     *    （`(value > max || value < min) && (max != 0 || min != 0)` 时
     *    直接返回；0..0 未定态范围例外）；
     * ③ shownOnce 已显示 + 模态：processEvents() 泵事件（Qt :621-627），
     *    带 m_processingEvents 重入闸（对标 QScopedValueRollback 守卫）；
     * ④ 计时起步：`(!setValueCalled && progress == 0) ||
     *    progress == minimum()` 分支重置起始时刻、重启定时器后**直接
     *    return**（Qt 原文如此——因此 setValue(minimum) 不会走到
     *    estimate 判定与 autoReset 判定）；
     * ⑤ 其余情况起计时并按 elapsed/estimate 启发式提前显示
     *    （Qt :632-656；minWaitTime=50ms 与 Qt 同值）；
     * ⑥ `if (progress == maximum && autoReset) reset();`
     *    ——判据是**原始入参**而非落定后的值。 */
    if (self->m_durationArmed && progress == self->m_value)
        return;
    if (!xprogressdialog_valueOutOfRange(self, progress)) {
        self->m_value = progress;
        xprogressdialog_syncBar(self);
    }
    /* ③ 已显示过（shownOnce）+ 模态 → 泵一遍事件队列（Qt :621-627）。
     *    重入闸：processEvents 会派发用户代码，用户代码再进 setValue 时
     *    不再递归泵（Qt 的 d->processingEvents + QScopedValueRollback）。 */
    if (self->m_shownOnce) {
        if (XDialog_isModal(&self->m_base) && !self->m_processingEvents) {
            self->m_processingEvents = true;
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            self->m_processingEvents = false;
        }
    } else if (progress == self->m_minimum ||
               (!self->m_durationArmed && progress == 0)) {
        /* ④ 计时起步（Qt :627-631）：重置起始时刻 + 重启 forceTimer 后
         *    直接 return ⇒ setValue(minimum) 永不走到 estimate/autoReset
         *    判定（退化范围 min==max 时本机实测值停在 max 而非复位成
         *    minimum-1）。QTimer::start 对活跃定时器即重启 ⇒ 先停再起。 */
        self->m_durationArmed = true;
        self->m_startTime = XDateTime_currentMSecsSinceEpoch();
        if (!self->m_wasCanceled) {
            xpd_timerStop(self);
            xpd_timerArm(self);
        }
        return;
    } else {
        if (!self->m_durationArmed) {
            self->m_durationArmed = true;
            if (!self->m_wasCanceled)
                xpd_timerArm(self);
        }
        /* ⑤ estimate 启发式（Qt :632-656）：操作耗时按已耗时/已步数
         *    线性外推，预估总时长 >= minimumDuration 就提前 show——
         *    长任务不再一律等满 minimumDuration 才弹窗。 */
        {
            bool needShow = false;
            int64_t elapsed = XDateTime_currentMSecsSinceEpoch() -
                              self->m_startTime;
            if (elapsed < 0) elapsed = 0;
            if (elapsed >= (int64_t)self->m_minimumDuration) {
                needShow = true;
            } else if (elapsed > XPROGRESSDIALOG_MIN_WAIT_MS) {
                const int totalSteps = self->m_maximum - self->m_minimum;
                const int delta = progress - self->m_minimum;
                const int myProgress = delta > 1 ? delta : 1;
                const int remainingSteps = totalSteps - myProgress;
                int64_t estimate;
                /* Qt 同款溢出分档：remainingSteps 大到乘法可能溢出时
                 * 先整除再乘（误差换安全）。elapsed>50ms ⇒ 除法安全。 */
                if (remainingSteps >= INT_MAX / (int)elapsed)
                    estimate = (int64_t)(remainingSteps / myProgress) * elapsed;
                else
                    estimate = elapsed * (int64_t)remainingSteps /
                               (int64_t)myProgress;
                needShow = estimate >= (int64_t)self->m_minimumDuration;
            }
            if (needShow) {
                /* 对标 ensureSizeIsAtLeastSizeHint + show + shownOnce=true
                 * （Qt :652-656）；showEvent 停 forceTimer。 */
                xprogressdialog_updateSize(self);
                XWidget_show((XWidget*)self);
                self->m_shownOnce = true;
            }
        }
    }
    /* ⑥ Qt：入参等于 maximum 且 autoReset 时复位。 */
    if (progress == self->m_maximum && self->m_autoReset)
        XProgressDialog_reset(self);
}

int XProgressDialog_value(const XProgressDialog* self)
{ return self ? self->m_value : 0; }

void XProgressDialog_reset(XProgressDialog* self)
{
    if (!self) return;
    /* 复扫 R-82 配套：对标 Qt reset——停计时并清 shownOnce/
     * setValueCalled，下一轮 setValue 重新起计时、超时可再强显。 */
    xpd_timerStop(self);
    self->m_shownOnce = false;
    self->m_durationArmed = false;
    if (self->m_autoClose) {
        XWidget_setVisible((XWidget*)self, false);
        /* autoClose 隐藏后标脏原矩形：子控件形态对话框隐藏不调度
           重绘，屏幕残留最后一帧鬼影（同 XDialog_done 注）。 */
        XWidget_update((XWidget*)self);
    }
    /* 对标 QProgressBar::reset：value = minimum-1（忙态哨兵，缺省 -1）。
     * r1 曾复位到 minimum，与 Qt 6.8.3 实测不符（账本 230）。 */
    self->m_value = xprogressdialog_busyValue(self->m_minimum);
    xprogressdialog_syncBar(self);
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
        XWidget_setParent_2((XWidget*)self->m_label, (XWidget*)self);
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
        XObject_disconnect((XObject*)self->m_cancelButton,
                             XSignal(XAbstractButton_clicked_signal),
                             (XObject*)self, xprogressdialog_cancelClickedSlot);
        XClassDelete((XClass*)self->m_cancelButton);
        self->m_cancelButton = NULL;
    }
    self->m_cancelButton = button;
    if (self->m_cancelButton) {
        XWidget_setParent_2((XWidget*)self->m_cancelButton, (XWidget*)self);
        XWidget_show((XWidget*)self->m_cancelButton);
        if (self->m_cancelButtonText) {
            XAbstractButton_setText((XAbstractButton*)self->m_cancelButton,
                                    self->m_cancelButtonText);
        }
        XObject_connect((XObject*)self->m_cancelButton,
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
{
    /* 对标 QProgressDialog::setBar（qprogressdialog.cpp:425-444）：
     *   if (!bar) { qWarning("Cannot set a null progress bar"); return; }
     *   if (bar == d->bar) return;
     *   delete d->bar;  d->bar = bar;  d->adoptChildWidget(bar);
     * ——NULL 是**无操作**（不清除已有进度条），新进度条接管所有权、
     * 成为对话框子控件并参与尺寸/重排（r2 对齐，账本 232）。 */
    if (!self || !bar || bar == self->m_bar) return;
    if (self->m_bar)
        XClassDelete((XClass*)self->m_bar);
    self->m_bar = bar;
    XWidget_setParent_2((XWidget*)self->m_bar, (XWidget*)self);
    XWidget_show((XWidget*)self->m_bar);
    /* Qt 的进度条是范围/值的状态载体；本库状态在对话框字段，设置后
     * 回灌保持一致。 */
    xprogressdialog_syncBar(self);
    xprogressdialog_updateSize(self);
    xprogressdialog_relayout(self);
}

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
    if (!self) return;
    self->m_minimumDuration = ms;
    /* 复扫 R-82 配套：对标 Qt setMinimumDuration——尚未产生进度
     * （值仍在 minimum）且计时已起时，改时长即按新时长重启。
     * Qt 的 int 属性原样保存（含 0/负值，本机 6.8.3 实测 -5→-5）。 */
    if (self->m_durationArmed && self->m_value == self->m_minimum) {
        xpd_timerStop(self);
        if (!self->m_wasCanceled)
            xpd_timerArm(self);
    }
}

int XProgressDialog_minimumDuration(const XProgressDialog* self)
{ return self ? self->m_minimumDuration : 4000; }

void XProgressDialog_forceShow(XProgressDialog* self)
{
    if (!self) return;
    /* 对标 Qt forceShow：先停表；已显示过（shownOnce）则不再强显
     * （reset 复位 shownOnce 后可再次显示）。 */
    xpd_timerStop(self);
    if (self->m_shownOnce)
        return;
    self->m_shownOnce = true;
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
        XObject_connect((XObject*)self, (size_t)XDialog_finished_signal,
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

#endif /* XGUI_ON */
