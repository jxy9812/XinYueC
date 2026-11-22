/**
 * @file       HsfMultiSelect.c
 * @brief      公共「多选下拉」组件实现：按钮 + 勾选浮层（机制唯一一份）。
 * @details    原为 HsfDataBar.c 的私有实现（HsfColPopup 浮层 +
 *             HsfBarMultiSel 组件），2026-11-12 抽成公共模块：数据工具条
 *             的「列筛选 / 图例」与远端设置页共用同一份机制（项目铁律：
 *             仓库已有的绝不手搓第二份）。
 *             结构逐件对齐 Python 客户端 data_toolbar.py 的
 *             MultiCheckCombo：QPushButton + QFrame(Qt.Popup) +
 *             QScrollArea + 每行 QCheckBox（cb.toggled 只改勾选、不收浮层，
 *             点浮层外自动收）。C 侧：XPushButton + XFrame(Popup) +
 *             XScrollArea + 每行 XCheckBox。**不用 XListView**（其 viewport
 *             会吞掉本体的鼠标事件 ⇒ 点行没反应；grabMouse 又会让子滚动条
 *             收不到点击）。
 *             浮层机制（本结构、虚表、弹出/收起/几何/平台抓取）全库只有这
 *             一份——谁弹出挂谁，不复制第二份。
 *             组件本体的口径（汇总文案、至少留几项、限长）见各函数注记。
 *             [tooltip 2026-11-13 删] 汇总旁路的「完整清单进提示框」与
 *             常驻静态提示整体移除（msSetTip/m_baseTip/m_noneTip 零残留，
 *             用户定案不要悬浮气泡）。
 */
#include "HsfMultiSelect.h"

#include "XObject.h"
#include "XMemory.h"
#include "XString.h"
#include "XFrame.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XCheckBox.h"
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XBoxLayout.h"
#include "XLayout.h"
#include "XWindow.h"
#include "XGuiApplication.h"      /* XGuiApplication_primaryScreen（浮层贴边钳制）。 */
#include "XScreen.h"              /* XScreen_availableGeometry（可用区）。 */
#include "XWidget_Protected.h"    /* XWidget_flushBackingStore（浮层上屏即刷）。 */
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XCHECKBOX_ON && XSCROLLAREA_ON && \
    XABSTRACTSCROLLAREA_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_BOX_ON

/* ---- 浮层几何常量（组件内部口径；按钮自身的宽高由调用方设定） ---- */
/** 每行高度（触摸屏口径；与 Python ITEM_H=34 同量级）。 */
#define HSF_MULTISELECT_ITEM_H 34
/** 浮层最大高度（超出后由 XScrollArea 滚动承载，同 Python POPUP_MAX_H）。 */
#define HSF_MULTISELECT_POPUP_MAX_H 420
/** 浮层最小宽度（照 Python _open_popup：宽度取按钮宽与 200 的较大值）。 */
#define HSF_MULTISELECT_POPUP_MIN_W 200
/** 汇总文字的字节上限（按钮在工具条行 1 里，长文字会把整行的最小宽顶爆
 *  ⇒ 布局从「两行横排」塌成「一行一个控件」；约 10 个汉字）。 */
#define HSF_MULTISELECT_SUMMARY_MAX 30

XCLASS_DEFINE_BEGING(HsfMultiSelectPopup)
XCLASS_DEFINE_EXTEND_END(HsfMultiSelectPopup, XFrame)

/** @brief 勾选浮层：XFrame(Popup) + XScrollArea + 每行一个 XCheckBox。
 *  @note  与 Python 的 QFrame(Qt.Popup)+QScrollArea+QCheckBox 行逐件对应。
 *         控件全部是**子控件**（有父），点浮层外的点击转投到本浮层
 *         （mousePressEvent 越界分支收起）靠**框架「窗口域」鼠标抓取**
 *         （XWidget_grabMouseWindowScope：跨顶层改道 + 窗内照常命中分派）
 *         与 1ms 延迟平台抓取双通道；不挂控件级 XWidget_grabMouse，
 *         否则 checkbox / 滚动条这些子控件收不到事件（实测两个坑）。
 *         [2026-11-11 泛化] 属主从「工具条」改为「多选组件」：列筛选与
 *         图例各一份组件实例，浮层只有一份机制，弹出时挂到谁身上就读写
 *         谁的掩码/名字表。 */
struct HsfMultiSelectPopup
{
    XFrame         m_base;        /**< 浮层窗口；必须是第一个成员。 */
    HsfMultiSelect* m_sel;        /**< 属主多选组件（借用；弹出时挂上）。 */
    XScrollArea*   m_scroll;      /**< 滚动区（子控件，拥有）。 */
    XWidget*       m_inner;       /**< 行容器（setWidget 接管所有权）。 */
    XBoxLayout     m_box;         /**< 行容器里的竖向布局。 */
    XCheckBox*     m_checks[HSF_MULTISELECT_MAX_ITEMS]; /**< 每行复选框（子控件）。 */
    int            m_checkCount;  /**< 已建复选框数。 */
    XTimerId       m_grabTimer;   /**< 延迟建立平台抓取的 1ms 定时器。 */
    bool           m_active;      /**< 弹出态（幂等收起用）。 */
    bool           m_syncing;     /**< 程序化 setChecked 期间抑制 toggled 回弹。 */
};

static void VHsfMultiSelectPopup_mousePressEvent(XWidget* self, XEvent* event);
static void VHsfMultiSelectPopup_keyPressEvent(XWidget* self, XEvent* event);
static void VHsfMultiSelectPopup_timerEvent(XObject* object, XTimerEvent* event);
static void VHsfMultiSelectPopup_deinit(HsfMultiSelectPopup* popup);
static XVtable* HsfMultiSelectPopup_class_init(void);
static void msPopupClose(HsfMultiSelectPopup* popup);
static void msToggle(HsfMultiSelect* sel, int idx, bool checked);
static void msSummary(HsfMultiSelect* sel);
static void msBtnSlot(XObject* receiver, XVarList* args);

/* ==================== 内部小件 ==================== */

/** @brief 勾选汇总文字：全勾=全勾文案，全不勾=全不勾文案，部分=被勾项名
 *         （顿号分隔，30 字节限长 + 截断加省略号）。
 *  @note  与 Python MultiCheckCombo._summary_text 同构（Python 端靠 Qt
 *         的省略号限宽，本框架没有 ⇒ 按**字节上限**截断、不截半个 UTF-8
 *         字符）。各组件的文案差异全在字段（m_allText/m_noneText），
 *         本函数一份实现多用。
 *  [限长 2026-11-11] 汇总文字**必须限长**：按钮在工具条行1 里，长文字会把
 *         整行的最小宽顶爆 ⇒ 布局从「两行横排」塌成「一行一个控件」（实测：
 *         勾掉一列后工具条全竖排）。
 *  [下拉箭头 2026-11-11 对齐 Python] MultiCheckCombo 的按钮文字是
 *         「汇总 + 两空格 + 下拉符」（没有内建下拉框 ⇒ 自己补箭头）；
 *         全勾/全不勾文案字段里已带箭头，部分勾选在此补。
 *  [字形 2026-11-12 实锅] 原用「▾」(U+25BE)——**内置字库根本没这个
 *         字形**（Noto Sans CJK 子集，U+25BE/U+25BC 均缺）⇒ 箭头恒为空
 *         白，「多选钮」看着与普通按钮无异（与 Python 截图对照才发现）。
 *         现统一改用「∨」(U+2228，字库有、宽度相近)：凡「文字尾部补箭头」
 *         的写法（本文件/工具条/状态栏账户钮）全部同一口径。
 *  [tooltip 2026-11-13 删] 汇总旁路的「完整清单进提示框」三态机制整体
 *         移除（msSetTip 连同 m_baseTip/m_noneTip 配置字段）：远端设置
 *         自动模式卡用户定案不要悬浮气泡，数据工具条两颗多选钮同机制
 *         一并收口（零残留：不留"配了不兑现"的提示字段）。 */
static void msSummary(HsfMultiSelect* sel)
{
    char buf[160];
    size_t used = 0;
    int i;
    int all = 1;
    int cut = 0;
    if (!sel) return;
    if (sel->m_count <= 0) {
        /* 空表=留占位文案（「列筛选」/「图例选择」）。 */
        XPushButton_setText_2(&sel->m_btn, sel->m_emptyText);
        return;
    }
    for (i = 0; i < sel->m_count; ++i) {
        if (!(sel->m_mask & (1u << i))) { all = 0; break; }
    }
    if (all) {
        XPushButton_setText_2(&sel->m_btn, sel->m_allText);
        return;
    }
    if (sel->m_mask == 0) {
        /* 全不勾：图例的常态可达态（min_checked=0）；列筛选经
         * 编程式设掩码也可达——其文案字段按旧实现口径配置
         * （「全部列  ∨」），行为仅箭头字形变化。 */
        XPushButton_setText_2(&sel->m_btn, sel->m_noneText);
        return;
    }
    buf[0] = '\0';
    for (i = 0; i < sel->m_count; ++i) {
        const char* nm;
        size_t len;
        if (!(sel->m_mask & (1u << i))) continue;
        nm = sel->m_names[i] ? XString_toUtf8(sel->m_names[i]) : NULL;
        if (!nm) continue;
        len = strlen(nm);
        /* 按钮文字：限 30 字节（≈10 个汉字），超了截断加省略号。 */
        if (used + len + 4 > HSF_MULTISELECT_SUMMARY_MAX) { cut = 1; break; }
        if (used) { memcpy(buf + used, "、", 3); used += 3; }
        memcpy(buf + used, nm, len); used += len;
        buf[used] = '\0';
    }
    if (cut) { memcpy(buf + used, "…", 3); used += 3; buf[used] = '\0'; }
    if (used) {
        /* 末尾补下拉符（同 Python 口径；字形见文件头「字形」注）。 */
        memcpy(buf + used, "  ∨", 5); used += 5; buf[used] = '\0';
    }
    /* 有掩码但名字全空（页面传空名表的病态输入）⇒ 落回全不勾文案字段。 */
    XPushButton_setText_2(&sel->m_btn, used ? buf : sel->m_noneText);
}

/** @brief 把 m_mask 同步回浮层里每个 XCheckBox（程序化回填，抑制回弹）。 */
static void msPopupRefresh(HsfMultiSelectPopup* popup)
{
    int i;
    if (!popup || !popup->m_sel) return;
    popup->m_syncing = true;
    for (i = 0; i < popup->m_checkCount && i < popup->m_sel->m_count; ++i) {
        if (popup->m_checks[i])
            XCheckBox_setChecked(popup->m_checks[i],
                (popup->m_sel->m_mask & (1u << i)) != 0);
    }
    popup->m_syncing = false;
}

/** @brief 翻转第 idx 项的勾选，立即生效，浮层保持打开。
 *  @note  由每行的 XCheckBox_toggled_signal 驱动（与 Python
 *         cb.toggled.connect(_on_check_toggled) 逐条对应），**不做坐标换算**。
 *         「至少留一项」守卫只给 m_minChecked>0 的组件（列筛选，Python
 *         min_checked=1）；图例 min_checked=0，允许取消到 0（全不勾=一条
 *         线都不画，合法态，页面层兜底）。 */
static void msToggle(HsfMultiSelect* sel, int idx, bool checked)
{
    int i;
    int count = 0;
    if (!sel || idx < 0 || idx >= sel->m_count) return;
    if (!checked) {
        if (sel->m_minChecked > 0) {
            for (i = 0; i < sel->m_count; ++i)
                if (sel->m_mask & (1u << i)) ++count;
            if (count <= sel->m_minChecked) {
                /* [至少留一项] Python min_checked=1：把这一格回弹成勾上。 */
                if (sel->m_popup) msPopupRefresh(sel->m_popup);
                return;
            }
        }
        sel->m_mask &= ~(uint32_t)(1u << idx);
    } else {
        sel->m_mask |= (uint32_t)(1u << idx);
    }
    msSummary(sel);
    /* 「列」是**视图设置**（看哪些列），不是筛选条件 ⇒ 勾选变化立即生效，
     * 不用按「筛选」（Python 端同口径；图例同理——只重画不重取，页面自理）。 */
    if (sel->m_fn)
        sel->m_fn(sel->m_userData, sel->m_mask);
}

/** @brief 某行复选框 toggled：只改勾选、不收浮层（Python cb.toggled 同款）。
 *  @note  框架信号槽契约（XClass.md「信号与槽」/ XSignalSlot.c）：
 *         connect_1 的 receiver 必须是**真 XObject**（框架在 directEmit
 *         里按 receiver 压 sender 栈、disconnect 时也按它摘连接），
 *         槽签名是 `void (XObject* receiver, XVarList* args)`，信号参数
 *         由框架**自动打包进 XVarList**（用 XVarList_args_1 解包）。
 *         这里 receiver = 浮层（真 XObject），项号靠 XObject_sender(receiver)
 *         取回被点的 XCheckBox（框架 pushSender 压的正是
 *         (conn->receiver, conn->signal->sender)），再用 m_checks[] 映射项号。 */
static void msCheckSlot(XObject* receiver, XVarList* args)
{
    HsfMultiSelectPopup* popup = (HsfMultiSelectPopup*)receiver;
    XObject* sender;
    int i;
    bool checked = false;
    if (!popup || popup->m_syncing) return;
    /* [XVarList_args_1 自带声明] 宏展开即 `const bool const checked = ...`
     * ⇒ 此处不能再声明同名变量（会 C2373）。用等价的逐项读取。 */
    if (args) {
        XVarList_start(args);
        checked = XVarList_arg(args, bool);
    }
    sender = XObject_sender((XObject*)popup);
    for (i = 0; i < popup->m_checkCount; ++i) {
        if (popup->m_checks[i] && (XObject*)popup->m_checks[i] == sender) {
            msToggle(popup->m_sel, i, checked);
            return;
        }
    }
}

/** @brief 按下：浮层**窗口外**点击 ⇒ 收起；窗内交 XFrame 正常命中分派。
 *  @note  窗口外点击经两条通道转投到本浮层（越界本地坐标）：框架
 *         「窗口域」抓取的跨顶层改道（XWidget_dispatchPointerEvent，
 *         立即生效、不依赖窗口映射时序）与平台 XGrabPointer/SetCapture
 *         （1ms 延迟建立），收层判定同 XComboPopupView/XDateTimePopup。
 *         **不挂控件级 XWidget_grabMouse**：那会让所有事件直投本窗口、
 *         绕过 childAt ⇒ 子控件（XCheckBox / 滚动条）永远收不到点击
 *         （实锅：「勾选没反应」「滚动条拖不动」）。 */
static void VHsfMultiSelectPopup_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfMultiSelectPopup* popup = (HsfMultiSelectPopup*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!popup || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XClass_Parent(XFrame, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (pos.x < 0 || pos.y < 0 ||
        pos.x >= XWidget_width(self) || pos.y >= XWidget_height(self)) {
        msPopupClose(popup);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XFrame, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief Esc 收起浮层（键盘平台抓取后直达本窗口，对标 XMenu）。 */
static void VHsfMultiSelectPopup_keyPressEvent(XWidget* self, XEvent* event)
{
    HsfMultiSelectPopup* popup = (HsfMultiSelectPopup*)self;
    if (popup && event && XEvent_type(event) == XEVENT_TYPE_KEY_PRESS &&
        XKeyEvent_key((XKeyEvent*)event) == XKey_Escape) {
        msPopupClose(popup);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XFrame, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 延迟 1ms 建立平台抓取（窗口需先完成映射，XComboBox/XMenu 同款）。 */
static void VHsfMultiSelectPopup_timerEvent(XObject* object, XTimerEvent* event)
{
    HsfMultiSelectPopup* popup = (HsfMultiSelectPopup*)object;
    XTimerId id;
    XWindow* handle;
    if (popup && event &&
        XTimerEvent_timerId(event) == popup->m_grabTimer) {
        id = popup->m_grabTimer;
        popup->m_grabTimer = XTIMER_INVALID_ID;
        XObject_killTimer((XObject*)popup, id);
        handle = XWidget_windowHandle((XWidget*)popup);
        if (handle) {
            XWindow_setMouseGrabEnabled(handle, true);
            XWindow_setKeyboardGrabEnabled(handle, true);
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 析构：撤销在途抓取定时器 / 解框架窗口域抓取与平台抓取 / 回指清空。
 *  @note  XScrollArea / XCheckBox 都是本浮层的**子控件**，随 XFrame
 *         控件树级联释放，不在此显式删除（避免二次释放）。 */
static void VHsfMultiSelectPopup_deinit(HsfMultiSelectPopup* popup)
{
    XWindow* handle;
    if (!popup) return;
    if (popup->m_grabTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)popup, popup->m_grabTimer);
        popup->m_grabTimer = XTIMER_INVALID_ID;
    }
    if (popup->m_active) {
        XWidget_releaseMouseWindowScope((XWidget*)popup);
        handle = XWidget_windowHandle((XWidget*)popup);
        if (handle) {
            XWindow_setMouseGrabEnabled(handle, false);
            XWindow_setKeyboardGrabEnabled(handle, false);
        }
        popup->m_active = false;
    }
    if (popup->m_sel && popup->m_sel->m_popup == popup)
        popup->m_sel->m_popup = NULL;
    popup->m_sel = NULL;
    XClass_Deinit_Parent(XFrame, (XFrame*)popup);
}

/** @brief 浮层虚表：覆写按下/按键/定时器/析构，其余继承 XFrame。 */
static XVtable* HsfMultiSelectPopup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfMultiSelectPopup)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VHsfMultiSelectPopup_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VHsfMultiSelectPopup_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent,
                             VHsfMultiSelectPopup_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfMultiSelectPopup_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfMultiSelectPopup);
    return XVTABLE_DEFAULT;
}

/** @brief 收起浮层：解框架窗口域抓取 + 解平台抓取 + 隐藏 + 复位按钮按下态；
 *         幂等（可复用）。 */
static void msPopupClose(HsfMultiSelectPopup* popup)
{
    XWindow* handle;
    if (!popup || !popup->m_active) return;
    popup->m_active = false;
    if (popup->m_grabTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)popup, popup->m_grabTimer);
        popup->m_grabTimer = XTIMER_INVALID_ID;
    }
    /* 框架窗口域抓取成对解除（XMenu 双通道成对解抓同款；后挂者不被
     * 早收者误放——release 只按当前抓取者匹配清位）。 */
    XWidget_releaseMouseWindowScope((XWidget*)popup);
    handle = XWidget_windowHandle((XWidget*)popup);
    if (handle) {
        XWindow_setMouseGrabEnabled(handle, false);
        XWindow_setKeyboardGrabEnabled(handle, false);
    }
    XWidget_hide((XWidget*)popup);
    if (popup->m_sel)
        XPushButton_setDown(&popup->m_sel->m_btn, false);
}

/** @brief 按当前项名集重建浮层里的 XCheckBox 行（每次弹出复用同一浮层）。
 *  @note  逐件对应 Python _open_popup：inner 容器 + 竖向布局 + 每行
 *         QCheckBox + 末尾 stretch。XScrollArea_setWidget 接管 inner
 *         所有权；行数变化时先 takeWidget 释放旧 inner 再重建。 */
static void msPopupBuild(HsfMultiSelectPopup* popup, HsfMultiSelect* sel)
{
    int i;
    if (!popup || !sel) return;
    if (popup->m_inner) {
        XScrollArea_takeWidget(popup->m_scroll);
        XClassDelete((XClass*)popup->m_inner);
        popup->m_inner = NULL;
        memset(popup->m_checks, 0, sizeof(popup->m_checks));
        popup->m_checkCount = 0;
    }
    popup->m_inner = XWidget_create(NULL, 0);
    if (!popup->m_inner) return;
    XBoxLayout_init(&popup->m_box, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&popup->m_box, 0);
    XLayout_setContentsMargins((XLayout*)&popup->m_box, 6, 6, 6, 6);
    XWidget_setLayout(popup->m_inner, (XLayout*)&popup->m_box);
    popup->m_syncing = true;
    for (i = 0; i < sel->m_count && i < HSF_MULTISELECT_MAX_ITEMS; ++i) {
        const char* nm = sel->m_names[i]
                             ? XString_toUtf8(sel->m_names[i]) : "";
        XCheckBox* cb = XCheckBox_create(popup->m_inner, 0);
        if (!cb) continue;
        XCheckBox_setText_2(cb, nm ? nm : "");
        XWidget_setFixedHeight((XWidget*)cb, HSF_MULTISELECT_ITEM_H);
        XCheckBox_setChecked(cb, (sel->m_mask & (1u << i)) != 0);
        XObject_connect((XObject*)cb,
                          XSignal(XAbstractButton_toggled_signal),
                          (XObject*)popup, msCheckSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)cb);
        XBoxLayout_addWidget(&popup->m_box, (XWidget*)cb);
        popup->m_checks[i] = cb;
        popup->m_checkCount = i + 1;
    }
    popup->m_syncing = false;
    XBoxLayout_addStretch(&popup->m_box, 1);
    XScrollArea_setWidget(popup->m_scroll, popup->m_inner);
    XWidget_show(popup->m_inner);
}

/** @brief 弹出（或复用）多选勾选浮层：真复选框行、点行不收、点外面才收。
 *  @note  几何口径照 Python _open_popup：按钮下沿全局坐标，高度按行数算
 *         并钳到 POPUP_MAX_H，宽度取按钮宽与 200 的较大值。
 *         每个多选组件各有一份浮层实例（首次弹出时建），弹出时挂接到本
 *         组件身上——机制一份，状态各归各家。 */
static void msPopupOpen(HsfMultiSelect* sel)
{
    HsfMultiSelectPopup* popup;
    XPoint local;
    XPoint pos;
    XRect r;
    XSize sz;
    XScreen* screen;
    XRect avail;
    int wantH;
    int width;
    if (!sel || sel->m_count <= 0) return;
    popup = sel->m_popup;
    if (!popup) {
        popup = (HsfMultiSelectPopup*)XMemory_malloc(sizeof(HsfMultiSelectPopup),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (!popup) return;
        memset(popup, 0, sizeof(*popup));
        /* [构造即 Popup 2026-11-11 真锅] 与 XMenu_init_2 逐字同款：在
         * XWidget_init 这一层就把窗口类型定成 Popup。曾用 flags=0 让
         * XWidget_init 自动补成 Window，后续 setWindowFlags 只改字段、
         * 平台窗口已按 Window 建柄 ⇒ 日志有 surface 却永不映射上屏。 */
        XFrame_init(&popup->m_base, NULL,
                    (XWidgetFlags)XWindowType_Popup);
        XClassSetVtable(popup, HsfMultiSelectPopup);
        popup->m_sel = sel;
        popup->m_grabTimer = XTIMER_INVALID_ID;
        Set_Class_Memory(popup, XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(popup, true);
        /* [逐件对齐 Python QScrollArea] 竖条按需、横条永不（列名单列）。 */
        popup->m_scroll = XScrollArea_create((XWidget*)popup, 0);
        if (!popup->m_scroll) { XClassDelete((XClass*)popup); return; }
        XScrollArea_setWidgetResizable(popup->m_scroll, true);
        XAbstractScrollArea_setHorizontalScrollBarPolicy(
            (XAbstractScrollArea*)popup->m_scroll,
            XScrollBarPolicy_AlwaysOff);
        XWidget_show((XWidget*)popup->m_scroll);
        {
            XBoxLayout* root = XBoxLayout_create(
                XBoxLayoutDirection_TopToBottom, (XWidget*)popup);
            XLayout_setContentsMargins((XLayout*)root, 0, 0, 0, 0);
            XBoxLayout_addWidget(root, (XWidget*)popup->m_scroll);
            XWidget_setLayout((XWidget*)popup, (XLayout*)root);
        }
        sel->m_popup = popup;
    }
    msPopupBuild(popup, sel);

    sz = XWidget_sizeHint((XWidget*)&sel->m_btn);
    local.x = 0;
    local.y = sz.height;
    pos = XWidget_mapToGlobal((XWidget*)&sel->m_btn, &local);
    width = XWidget_width((XWidget*)&sel->m_btn);
    if (width < HSF_MULTISELECT_POPUP_MIN_W) width = HSF_MULTISELECT_POPUP_MIN_W;
    wantH = sel->m_count * HSF_MULTISELECT_ITEM_H + 14;
    if (wantH > HSF_MULTISELECT_POPUP_MAX_H) wantH = HSF_MULTISELECT_POPUP_MAX_H;
    screen = XGuiApplication_primaryScreen();
    if (screen) {
        avail = XScreen_availableGeometry(screen);
        if (pos.x + width > avail.x + avail.width)
            pos.x = avail.x + avail.width - width;
        if (pos.x < avail.x) pos.x = avail.x;
        if (pos.y + wantH > avail.y + avail.height) {
            int above = pos.y - sz.height - wantH;
            if (above >= avail.y) pos.y = above;
            else {
                wantH = avail.y + avail.height - pos.y;
                if (wantH < HSF_MULTISELECT_ITEM_H + 2)
                    wantH = HSF_MULTISELECT_ITEM_H + 2;
            }
        }
        if (pos.y < avail.y) pos.y = avail.y;
    }
    XRect_init(&r, pos.x, pos.y, width, wantH);
    XWidget_setGeometryRect((XWidget*)popup, &r);

    popup->m_active = true;
    XPushButton_setDown(&sel->m_btn, true);
    XWidget_show((XWidget*)popup);
    XWidget_activateWindow((XWidget*)popup);
    XWidget_raise((XWidget*)popup);
    XWidget_flushBackingStore((XWidget*)popup, NULL);
    /* [窗口域抓取 2026-11-13 点外收层根修] 立即挂框架「窗口域」鼠标抓取：
     * 其它窗口（宿主主窗等）收到的点击经 XWidget_dispatchPointerEvent
     * 的跨顶层改道进本浮层（越界 → mousePressEvent 收层）。此前只靠
     * 延迟平台抓取（win32 SetCapture），Windows 上点浮层外不收层（实测）；
     * XMenu/XComboBox 收层走的正是框架抓取这条改道（窗口映射前即生效）。
     * 窗口域只改道跨窗事件，浮层窗内照常 childAt 命中复选框/滚动条
     * （窗口域不进「直投抓取控件」分支——控件级 grabMouse 才会绕过
     * childAt 饿死子控件，见文件头注记）。 */
    XWidget_grabMouseWindowScope((XWidget*)popup);
    /* 平台抓取补 1ms 延迟建立（窗口完成映射后 SetCapture/XGrabPointer：
     * 覆盖「点击落在本进程窗口之外」的场景与键盘路由；XMenu/XComboBox
     * 同款双通道——框架抓取管收层，平台抓管全量输入重定向）。 */
    if (popup->m_grabTimer == XTIMER_INVALID_ID)
        popup->m_grabTimer = XObject_startTimer(
            (XObject*)popup, 1u, XTimerType_PreciseTimer);
}

/** @brief 多选钮点击：已弹则收，未弹则弹（Python _toggle_popup 同款）。
 *  @note  槽的 receiver 就是按钮本身（组件的 m_btn 是首成员 ⇒ 槽首参直转
 *         即组件本体，同 HsfDataBar 的 m_base 首成员口径），故无需再按
 *         sender 分派到哪一份组件实例。 */
static void msBtnSlot(XObject* receiver, XVarList* args)
{
    HsfMultiSelect* sel = (HsfMultiSelect*)receiver;
    (void)args;
    if (!sel) return;
    if (sel->m_count <= 0) return;
    if (sel->m_popup && sel->m_popup->m_active) {
        msPopupClose(sel->m_popup);
        return;
    }
    msPopupOpen(sel);
}

/* ==================== 公共 API ==================== */

bool HsfMultiSelect_create(HsfMultiSelect* sel, XWidget* parent,
                           const HsfMultiSelectConfig* cfg)
{
    int i;
    if (!sel || !parent || !cfg) return false;
    /* [调用方不必事先清零] 组件内含按钮与项名表，清零一次即可（栈上局部
     * 结构体交给框架前必须清零：MSVC /RTC1 下读未初始化字段直接
     * DebugBreak 冻进程）。 */
    memset(sel, 0, sizeof(*sel));
    /* 文案与回调：借用（调用方保证存活到组件销毁）。
     * 三个按钮文案兜底成空串（NULL 会让 setText 拿到空指针）。 */
    sel->m_emptyText = cfg->emptyText ? cfg->emptyText : "";
    sel->m_allText   = cfg->allText   ? cfg->allText   : "";
    sel->m_noneText  = cfg->noneText  ? cfg->noneText  : "";
    sel->m_fn        = cfg->fn;
    sel->m_userData  = cfg->userData;
    /* [至少留几项] allowNone=false ⇒ 下限抬到 1（Python min_checked=1 的
     * 「至少留一项」）；allowNone=true ⇒ 用调用方给的 minChecked（图例
     * min_checked=0，允许全不勾）。 */
    sel->m_minChecked = cfg->allowNone ? (cfg->minChecked > 0 ? cfg->minChecked : 0)
                                       : (cfg->minChecked > 1 ? cfg->minChecked : 1);
    /* 按钮：父控件的子对象（随父控件级联销毁；组件不负责删它）。
     * **尺寸/字号不在组件内写死**：调用方拿 HsfMultiSelect_button() 自行
     * 设置（见头文件契约）。 */
    XPushButton_init(&sel->m_btn, parent, 0);
    XPushButton_setText_2(&sel->m_btn, cfg->emptyText ? cfg->emptyText : "");
    /* 点开=勾选浮层（槽的 receiver 就是按钮，首成员直转得组件本体）。 */
    XObject_connect((XObject*)&sel->m_btn,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&sel->m_btn, msBtnSlot,
                      XConnectionType_Direct);
    sel->m_created = true;   /* destroy 据此解连接（幂等）。 */
    /* 项名表（深拷贝）+ 初始掩码。 */
    if (cfg->items) {
        for (i = 0; i < cfg->count && i < HSF_MULTISELECT_MAX_ITEMS; ++i) {
            sel->m_names[i] = XString_create_utf8(cfg->items[i] ? cfg->items[i] : "");
            if (!sel->m_names[i]) continue;
            sel->m_count = i + 1;
        }
    }
    sel->m_mask = (sel->m_count > 0) ? (cfg->initialMask & (uint32_t)
                                        ((1ull << sel->m_count) - 1ull)) : 0u;
    /* 汇总文案按当前掩码刷新（无项时留占位文案）。 */
    msSummary(sel);
    return true;
}

void HsfMultiSelect_destroy(HsfMultiSelect* sel)
{
    int i;
    if (!sel) return;
    /* [解信号连接] 按钮的 clicked 槽 receiver 就是按钮本身（首成员直转得
     * 组件本体）⇒ 组件结构体一旦释放，按钮上残留的连接就是悬垂 receiver
     * （再点一下即 UAF）。父控件析构时按钮还活着（destroy 由调用方在
     * destroyed 槽里调），故必须在这里显式摘掉连接；幂等靠 m_created 位。 */
    if (sel->m_created) {
        XObject_disconnect((XObject*)&sel->m_btn,
                             XSignal(XAbstractButton_clicked_signal),
                             (XObject*)&sel->m_btn, msBtnSlot);
        sel->m_created = false;
    }
    /* 浮层是无父顶层的 Popup（不随控件树级联）⇒ 显式释放；先摘回指，
     * 免得浮层析构时再回写已释放的组件。 */
    if (sel->m_popup) {
        sel->m_popup->m_sel = NULL;
        XClassDelete((XClass*)sel->m_popup);
        sel->m_popup = NULL;
    }
    /* 项名表（堆 XString，组件拥有）。 */
    for (i = 0; i < HSF_MULTISELECT_MAX_ITEMS; ++i) {
        if (sel->m_names[i]) {
            XClassDelete((XClass*)sel->m_names[i]);
            sel->m_names[i] = NULL;
        }
    }
    sel->m_count = 0;
    sel->m_mask = 0u;
}

XWidget* HsfMultiSelect_button(const HsfMultiSelect* sel)
{
    return sel ? (XWidget*)&sel->m_btn : NULL;
}

uint32_t HsfMultiSelect_mask(const HsfMultiSelect* sel)
{
    return sel ? sel->m_mask : 0u;
}

void HsfMultiSelect_setMask(HsfMultiSelect* sel, uint32_t mask)
{
    if (!sel) return;
    if (sel->m_mask == mask) return;   /* 同值早退。 */
    sel->m_mask = mask;
    msSummary(sel);
    /* [联动] 设置勾选集合 = 勾选变了 ⇒ 与真实勾选同口径回调，调用方据此
     * 重排可见列/序列（否则调用方改了掩码、视图却不联动）。 */
    if (sel->m_fn)
        sel->m_fn(sel->m_userData, sel->m_mask);
}

void HsfMultiSelect_setItems(HsfMultiSelect* sel, const char* const* items,
                             int count, bool defaultAll)
{
    int i;
    if (!sel) return;
    /* 清掉旧项名（拥有）。 */
    for (i = 0; i < HSF_MULTISELECT_MAX_ITEMS; ++i) {
        if (sel->m_names[i]) {
            XClassDelete((XClass*)sel->m_names[i]);
            sel->m_names[i] = NULL;
        }
    }
    sel->m_count = 0;
    if (items) {
        for (i = 0; i < count && i < HSF_MULTISELECT_MAX_ITEMS; ++i) {
            sel->m_names[i] = XString_create_utf8(items[i] ? items[i] : "");
            if (!sel->m_names[i]) continue;
            sel->m_count = i + 1;
        }
    }
    sel->m_mask = (defaultAll && sel->m_count > 0)
        ? (uint32_t)((1ull << sel->m_count) - 1ull) : 0u;
    /* 改名后刷新汇总（按钮文字）。**不发**变更回调（初始化口径）。 */
    msSummary(sel);
}

int HsfMultiSelect_itemCount(const HsfMultiSelect* sel)
{
    return sel ? sel->m_count : 0;
}

void HsfMultiSelect_setChangedCallback(HsfMultiSelect* sel,
                                       HsfMultiSelectChangedFn fn,
                                       void* userData)
{
    if (!sel) return;
    sel->m_fn = fn;
    sel->m_userData = userData;
}

#endif /* 守卫 */
