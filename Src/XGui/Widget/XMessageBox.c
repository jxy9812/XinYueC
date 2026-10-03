/**
 * @file       XMessageBox.c
 * @brief      消息对话框控件实现（对标 Qt 6.8 QMessageBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMessageBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XCoreApplication.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XWidget_Protected.h"
#include "XDialog.h"
#include "XApplication.h"
#include "XGuiApplication.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XIcon.h"
#include "XImage.h"
#include "XPixmap.h"
#include "XPainter.h"
#if XCLIPBOARD_ON
#include "XClipboard.h"
#endif
#if XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#endif
#if XCHECKBOX_ON
#include "XCheckBox.h"
#endif

#if XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON

/* ==================== 内部工具 ==================== */

/** @brief 图标区是否存在（对标 Qt QMessageBoxPrivate::setupLayout 的
 *         hasIcon：iconPixmap 优先，其次非 NoIcon 的分级标准图标）。 */
static bool xmsg_hasIconArea(const XMessageBox* self)
{
    if (!self) return false;
    if (self->m_iconPixmap) return true;
    return self->m_icon != (int)XMessageBoxIcon_NoIcon;
}

/** @brief 内容区顶部 y（对标 Qt：标题栏以下为内容区起点）。
 *  @details 仅「自绘标题带」形态需要内容让位：XDialog 面板顶部带在
 *           「子控件形态 + 已设标题」时绘制（与 XDialog 的标题带 gate
 *           同口径）；窗口形态对话框由平台标题栏承载标题，内容恒从
 *           12px 起。CSD 追加避让（用户实测：合并框架自绘标题栏后独
 *           立顶层消息盒首行内容被遮半截）：框架自绘标题条画在客户区
 *           顶部（系统标题栏在客户区外），窗口形态消息盒在 CSD 激活
 *           时从「基线 + 装饰条高」起——偏移经
 *           XDialog_decorationTopOffset 查询（XWindowDecoration_
 *           marginsFor 同源），系统标题栏/CSD 抑制态恒 0 布局零变化；
 *           子控件形态对话框装饰判定恒假，28/12 历史口径不变。
 *           updateSize 高度按同一 top 累加（按钮盒仍贴底 h-40），
 *           resizeEvent 重排与本文件图标区绘制（xmsg_drawIcon）同源
 *           自动随动。 */
static int xmsg_contentTop(const XMessageBox* self)
{
    const XWidget* selfw;
    const XString* title;
    const char* utf8;
    int base;
    if (!self) return 12;
    selfw = (const XWidget*)self;
    base = 12;
    if (!selfw->m_isWindow) {
        title = XWidget_windowTitle(selfw);
        utf8 = title ? XString_toUtf8(title) : NULL;
        if (utf8 && utf8[0]) base = 28;
    }
    return base + XDialog_decorationTopOffset((const XDialog*)self);
}

/** @brief  文本标签在给定可用宽下的实测块高（行数×行高；账本 r2#2）。
 *  @details 单一几何事实源：XLabel_heightForWidth 统一承载显式 '\n'
 *           行与 wordWrap 折行（label_sizeForWidth 同一布局器），杜绝
 *           旧版 setupText 硬编码 60px 高与 updateSize 行模型两套口径
 *           的漂移——r2 猎捕实证：主题字模行高 30 时 60px 矩形恰容 2
 *           行，第 3 行起垂直裁剪（a-91），且矩形底 y=102 与 120 高框
 *           按钮行顶 y=87 重叠 15px，childAt 把按钮上半带的按压抢给
 *           文本标签=标准按钮「间歇性点击失效」（r2#1，DBGPRESS 实锚
 *           hit 目标=(56,42 108x60)）。字模未落控件建窗前实测可为
 *           0：退 sizeHint，再退 1 行行高（r1#4 同款兜底），SHOW 复
 *           算（updateSize in showEvent）按真字模纠正。 */
static int xmsg_textBlockHeight(const XMessageBox* self,
                               const XLabel* label, int availW)
{
    int h;
    (void)self;
    if (!label) return 0;
    h = availW > 0 ? XLabel_heightForWidth(label, availW) : -1;
    if (h <= 0) {
        XSize hint = XLabel_sizeHint(label);
        h = hint.height;
    }
    if (h <= 0) h = 18; /* 字模全未就绪的建窗前下限（1 行基线）。 */
    return h;
}

static void xmsg_setupText(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int top;
    int x;
    int availW;
    if (!self || !self->m_textLabel) return;
    top = xmsg_contentTop(self);
    /* 对标 Qt QMessageBox 布局：图标列在文本左侧（indentSpacer 7px +
     * 图标区），无图标时文本占满内容行。高度=实测块高（不再硬编码
     * 60：60 是两行主题字模的历史口径，多行文本被裁、且矩形侵按钮
     * 行——r2#1/#2 根因），标签鼠标穿透（纯展示件，压按钮带时不得
     * 抢按压，r2#1 防回归）。 */
    x = xmsg_hasIconArea(self) ? 56 : 16;
    availW = w > x + 16 ? w - x - 16 : 0;
    XRect_init(&r, x, top,
               availW, xmsg_textBlockHeight(self, self->m_textLabel, availW));
    XWidget_setAttribute((XWidget*)self->m_textLabel,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    XWidget_setGeometry((XWidget*)self->m_textLabel,
                        r.x, r.y, r.width, r.height);
}

/** @brief 补充文本行几何（对标 setupLayout：informativeText 在消息文
 *         本下一行；行高按 60 让位口径，随详细区压缩）。
 *  @details y 随主文本实测块高（r2#2 同源：旧固定 top+64 在多行主文
 *           本下与文本矩形重叠），高=自身实测块高；标签鼠标穿透同
 *           xmsg_setupText。 */
static void xmsg_setupInformative(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int top;
    int x;
    int availW;
    int textH;
    if (!self || !self->m_informativeLabel) return;
    top = xmsg_contentTop(self);
    x = xmsg_hasIconArea(self) ? 56 : 16;
    availW = w > x + 16 ? w - x - 16 : 0;
    textH = xmsg_textBlockHeight(self, self->m_textLabel, availW);
    XRect_init(&r, x, top + textH + 8,
               availW,
               xmsg_textBlockHeight(self, self->m_informativeLabel, availW));
    XWidget_setAttribute((XWidget*)self->m_informativeLabel,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    XWidget_setGeometry((XWidget*)self->m_informativeLabel,
                        r.x, r.y, r.width, r.height);
}

static void xmsg_setupCheckBox(XMessageBox* self)
{
#if XCHECKBOX_ON
    int w = XWidget_width((XWidget*)self);
    int top;
    int x;
    int availW;
    int row;
    if (!self || !self->m_checkBox) return;
    top = xmsg_contentTop(self);
    x = xmsg_hasIconArea(self) ? 56 : 16;
    availW = w > x + 16 ? w - x - 16 : 0;
    /* 复选框行位于消息文本/补充文本实测块高之后（r2#2 同源：旧固定
     * 64/128 偏移随多行文本漂移）、按钮盒（底部 40）之前。 */
    row = top + xmsg_textBlockHeight(self, self->m_textLabel, availW) + 8;
    if (self->m_informativeLabel)
        row += xmsg_textBlockHeight(self, self->m_informativeLabel,
                                    availW) + 8;
    if (XWidget_height((XWidget*)self) > row + 20 + 40)
        XWidget_setGeometry((XWidget*)self->m_checkBox, 16, row,
                            w > 32 ? w - 32 : 0, 20);
#else
    (void)self;
#endif
}

static void xmsg_setupButtonBox(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_buttonBox) return;
    XRect_init(&r, 0, h > 40 ? h - 40 : 0, w, 40);
    XWidget_setGeometryRect((XWidget*)self->m_buttonBox, &r);
}

#if XPLAINTEXTEDIT_ON
/** @brief 详细文本区几何（对标 setupLayout：detailsText 在按钮盒之下，
 *         未见展开时零高隐藏）。 */
static void xmsg_setupDetailsText(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_detailsText) return;
    if (self->m_detailsVisible) {
        XRect_init(&r, 8, h > 40 + 120 ? h - 40 - 120 : 0,
                   w > 16 ? w - 16 : 0, 120);
        XWidget_setGeometry((XWidget*)self->m_detailsText,
                            r.x, r.y, r.width, r.height);
        XWidget_show((XWidget*)self->m_detailsText);
    } else {
        XWidget_hide((XWidget*)self->m_detailsText);
    }
}
#endif /* XPLAINTEXTEDIT_ON */

/** @brief Show Details... 切换按钮几何（按钮盒上方右缘；无详细区时
 *         隐藏）。 */
static void xmsg_setupDetailsButton(XMessageBox* self)
{
#if XPLAINTEXTEDIT_ON
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_detailsButton) return;
    if (w > 160 && h > 40 + 24) {
        XWidget_setGeometry((XWidget*)self->m_detailsButton,
                            w - 150, h - 40 - 26, 140, 24);
        XWidget_show((XWidget*)self->m_detailsButton);
    } else {
        XWidget_hide((XWidget*)self->m_detailsButton);
    }
#else
    (void)self;
#endif
}

/** @brief 尺寸收口（对标 QMessageBoxPrivate::updateSize，qmessagebox.cpp:
 *         349-420）：软限 min(屏宽/2,500)、硬限 min(屏宽-480,1000)（小
 *         屏=屏宽），超软限先整词换行（XLabel wordWrap），超硬限以硬
 *         限封顶；窗口标题宽度参与计算；结果以 setFixedSize 固定。 */
static void xmsg_updateSize(XMessageBox* self)
{
    int width;
    int height;
    int top;
    int textW;
    int screenW = 0;
    int softLimit;
    int hardLimit;
    XSize hint;
    if (!self || !self->m_textLabel) return; /* 无文本标签无从测量。 */
#if XGUIAPPLICATION_ON && XSCREEN_ON
    XScreen* screen = XGuiApplication_primaryScreen();
    if (screen) {
        XRect avail = XScreen_availableGeometry(screen);
        screenW = avail.width;
    }
#endif
    if (screenW <= 0) screenW = 800; /* 无屏幕信息的退化基线。 */
    hardLimit = screenW - 480;
    if (hardLimit > 1000) hardLimit = 1000;
    if (screenW <= 1024) hardLimit = screenW;
    softLimit = screenW / 2;
    if (softLimit > 500) softLimit = 500;
    if (softLimit < 1) softLimit = 1;
    if (hardLimit < softLimit) hardLimit = softLimit;

    /* 主文本最小宽（未换行 sizeHint）+ 图标列/边距构成布局最小宽。 */
    hint = XLabel_sizeHint(self->m_textLabel);
    textW = hint.width > 0 ? hint.width : 0;
    width = textW + (xmsg_hasIconArea(self) ? 56 : 16) + 16;
    if (self->m_informativeLabel) {
        XSize ih = XLabel_sizeHint(self->m_informativeLabel);
        int w2 = (ih.width > 0 ? ih.width : 0) +
                 (xmsg_hasIconArea(self) ? 56 : 16) + 16;
        if (w2 > width) width = w2;
    }
    if (width > softLimit) {
        /* 超软限：整词换行（对标 label->setWordWrap(true) +
         * qMax(softLimit, layoutMinimumWidth())）；行数不再估算，块高
         * 由 xmsg_textBlockHeight（heightForWidth）按折行实测（r2#2：
         * 旧「最长行宽/折行宽」整除估算与 label 贪心断行两套口径在
         * 中英混排下偏差可达数行）。 */
        if (self->m_textLabel) {
            XLabel_setWordWrap(self->m_textLabel, true);
            width = softLimit;
        }
    }
    if (width > hardLimit) width = hardLimit; /* 硬限封顶（对标 WrapAnywhere）。 */

    /* 窗口标题宽度参与（qmessagebox.cpp:408-411）：近似以 8px/字符
     * 估算标题单行宽 +50 余量，同样受硬限约束。 */
    {
        const XString* title = XWidget_windowTitle((const XWidget*)self);
        const char* utf8 = title ? XString_toUtf8(title) : NULL;
        if (utf8 && utf8[0]) {
            int titleW = (int)XStrlen(utf8) * 8 + 50;
            if (titleW > hardLimit) titleW = hardLimit;
            if (titleW > width) width = titleW;
        }
    }

    /* 高度：主文本实测块高（显式 '\n' 行 + 折行统一由 label 布局器
     * 承载，r2#2：旧「行数估算×行高」模型在多行长文本下只按 2 行
     * 收口、且与 setupText 的 60px 矩形两套口径漂移）+ 补充文本/复
     * 选框实测 + 按钮盒 40（+ 展开的详细区 120）。单一事实源与
     * setupText/setupInformative/setupCheckBox 同一 helper。 */
    top = xmsg_contentTop(self);
    {
        int contentW = width - (xmsg_hasIconArea(self) ? 56 : 16) - 16;
        height = top +
                 xmsg_textBlockHeight(self, self->m_textLabel, contentW);
        if (self->m_informativeLabel)
            height += 8 +
                      xmsg_textBlockHeight(self, self->m_informativeLabel,
                                           contentW);
#if XCHECKBOX_ON
        if (self->m_checkBox) height += 20 + 8;
#endif
    }
    height += 40 + 8;
#if XPLAINTEXTEDIT_ON
    if (self->m_detailsVisible) height += 120;
#endif
    if (width < 180) width = 180;  /* 按钮盒/图标的最小可用宽。 */
    if (height < 120) height = 120;
    XWidget_setFixedSize((XWidget*)self, width, height);
    xmsg_setupText(self);
    xmsg_setupInformative(self);
    xmsg_setupCheckBox(self);
    xmsg_setupButtonBox(self);
#if XPLAINTEXTEDIT_ON
    xmsg_setupDetailsText(self);
    xmsg_setupDetailsButton(self);
#endif
}

/** @brief 探测转义按钮（对标 QMessageBoxPrivate::detectEscapeButton，
 *         qmessagebox.cpp:1084-1136）：显式设置 > Cancel > 唯一按钮 >
 *         两按钮含 Show Details 取另一个 > 恰一个 RejectRole > 恰一个
 *         NoRole > 探测失败（NULL，Esc 无效果）。 */
static XAbstractButton* xmsg_detectEscapeButton(const XMessageBox* self)
{
    XAbstractButton* detected;
    const XVector* buttons;
    int64_t i;
    int64_t n;
    if (!self || !self->m_buttonBox) return NULL;
    if (self->m_escapeButton) return self->m_escapeButton;
    detected = (XAbstractButton*)XDialogButtonBox_button(
        self->m_buttonBox, XDialogButtonBoxStandard_Cancel);
    if (detected) return detected;
    buttons = XDialogButtonBox_buttons(self->m_buttonBox);
    if (!buttons) return NULL;
    n = XVector_size_base((const XContainer*)buttons);
    if (n == 1)
        return *(XAbstractButton* const*)XVector_At_Base(buttons, 0,
                                                         XAbstractButton*);
#if XPLAINTEXTEDIT_ON
    if (n == 2 && self->m_detailsButton) {
        for (i = 0; i < n; ++i) {
            XAbstractButton* b = *(XAbstractButton* const*)
                XVector_At_Base(buttons, i, XAbstractButton*);
            if (b == self->m_detailsButton)
                return *(XAbstractButton* const*)XVector_At_Base(
                    buttons, 1 - i, XAbstractButton*);
        }
    }
#endif
    /* 恰一个 RejectRole 按钮 → 转义按钮；多于一个则探测失败。 */
    detected = NULL;
    for (i = 0; i < n; ++i) {
        XAbstractButton* b = *(XAbstractButton* const*)
            XVector_At_Base(buttons, i, XAbstractButton*);
        if (XDialogButtonBox_buttonRole(self->m_buttonBox, b) ==
            XDialogButtonBoxRole_RejectRole) {
            if (detected) { detected = NULL; break; }
            detected = b;
        }
    }
    if (detected) return detected;
    /* 恰一个 NoRole 按钮 → 转义按钮；多于一个则探测失败。 */
    for (i = 0; i < n; ++i) {
        XAbstractButton* b = *(XAbstractButton* const*)
            XVector_At_Base(buttons, i, XAbstractButton*);
        if (XDialogButtonBox_buttonRole(self->m_buttonBox, b) ==
            XDialogButtonBoxRole_NoRole) {
            if (detected) { detected = NULL; break; }
            detected = b;
        }
    }
    return detected;
}

/** @brief 按钮点击的 exec 返回码（对标 QMessageBoxPrivate::
 *         execReturnCode，qmessagebox.cpp:448-469）：标准按钮返回位
 *         值；自定义按钮返回 Accepted+1+索引（保持对话框码域之外）；
 *         未找到返回 -1。 */
static int xmsg_execReturnCode(const XMessageBox* self,
                               XAbstractButton* button)
{
    int standard;
    const XVector* buttons;
    int64_t i;
    int64_t n;
    if (!self || !self->m_buttonBox) return -1;
    standard = (int)XDialogButtonBox_standardButton(self->m_buttonBox,
                                                    button);
    if (standard)
        return standard;
    if (!button) return -1;
    buttons = XDialogButtonBox_buttons(self->m_buttonBox);
    if (!buttons) return -1;
    n = XVector_size_base((const XContainer*)buttons);
    for (i = 0; i < n; ++i) {
        XAbstractButton* b = *(XAbstractButton* const*)
            XVector_At_Base(buttons, i, XAbstractButton*);
        if (b == button)
            return (int)XDialogCode_Accepted + 1 + (int)i;
    }
    return -1;
}

/* ==================== 事件处理 ==================== */

static void VX_messageBox_resizeEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    (void)event;
    if (!box) return;
    xmsg_setupText(box);
    xmsg_setupInformative(box);
    xmsg_setupCheckBox(box);
    xmsg_setupButtonBox(box);
#if XPLAINTEXTEDIT_ON
    xmsg_setupDetailsText(box);
    xmsg_setupDetailsButton(box);
#endif
}

/* R-80/R-26 收口：对标 Qt 6.8 QMessageBox——按钮盒 accepted/rejected
   信号不参与消息盒收口（QMessageBox 只订阅 buttonBox 的 clicked，自行
   done(execReturnCode(button))，qmessagebox.cpp:274-275、516-525）。
   此处删去 accepted/rejected 桥接：clicked → buttonClicked 信号 →
   done(标准按钮位值)，done 内按角色映射发射 accepted/rejected 后再发
   finished——顺序对标验收（buttonClicked→accepted→finished），与 Esc
   路径（keyPressEvent → click(escapeButton) → 同一收口）对称。 */

/** @brief open(receiver,member) 收口清理：断开临时连接并清空记录字段。
 *  @details 对标 qmessagebox.cpp:506-512 的断开收尾，并按头文件契约
 *           「对话框关闭时自动断开」补齐（Qt 关闭路径自身不清理，此
 *           处为 X 侧健壮性收口）。按钮点击与 [×]/reject 关闭两条收口
 *           路径共用；未 open 过或已清理时为空操作（幂等）。 */
static void xmsg_openCleanup(XMessageBox* box)
{
    if (!box) return;
    if (box->m_openReceiver && box->m_openMember) {
        XObject_disconnect_1(
            (XObject*)box,
            (size_t)(box->m_openButtonPayload
                         ? XMessageBox_buttonClicked_signal
                         : XDialog_finished_signal),
            box->m_openReceiver, box->m_openMember);
    }
    box->m_openReceiver = NULL;
    box->m_openMember = NULL;
    box->m_openButtonPayload = false;
}

/** @brief 按钮盒 clicked 收口：Show Details... 切换分支 + 记录点击、
 *         发射 buttonClicked、done(标准位值)。
 *  @details 对标 QMessageBoxPrivate::buttonClicked/setClickedButton
 *           （qmessagebox.cpp:493-525）。 */
static void xmsg_clickedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    if (!box || !args) return;
    /* XVarList_args_1 宏自带变量声明（name1），不可再前置声明。 */
    XVarList_args_1(args, XAbstractButton*, button);
#if XPLAINTEXTEDIT_ON
    if (button == box->m_detailsButton) {
        /* Show Details... 切换详细区显隐并重算尺寸（对标 Qt DetailButton
         * 分支：detailsButton->setLabel + detailsText->setHidden +
         * updateSize）。 */
        box->m_detailsVisible = !box->m_detailsVisible;
        if (box->m_detailsText) {
            XWidget_setVisible((XWidget*)box->m_detailsText,
                               box->m_detailsVisible);
        }
        xmsg_updateSize(box);
        XWidget_update((XWidget*)box);
        return;
    }
#endif
    box->m_clicked = (XAbstractButton*)button;
    if (((XObject*)box)->m_signalSlot) {
        XVarList* out = XVarList_Create(XVar(XAbstractButton*, button));
        if (out) {
            XObject_emitSignal((XObject*)box,
                               (size_t)XMessageBox_buttonClicked_signal,
                               out, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
    /* open(receiver,member) 的临时连接在收口时自动断开（对标
     * receiverToDisconnectOnClose）。时序对标 qmessagebox.cpp:503-512：
     * setClickedButton 先行、disconnect 收尾——finished/accepted/
     * rejected 仅由 XDialog_done 发射，须先送达 open-receiver 再断开
     * （此前断开在 done 之前，finished 载荷的 receiver 永远收不到
     * 收口回调）。 */
    XDialog_done(&box->m_base, xmsg_execReturnCode(box, button));
    xmsg_openCleanup(box);
}

/* ==================== 生命周期与虚表 ==================== */

static void VXMessageBox_deinit(XMessageBox* self)
{
    if (!self) return;
    if (self->m_text) {
        XClassDelete(self->m_text);
        self->m_text = NULL;
    }
    if (self->m_detailedText) {
        XClassDelete(self->m_detailedText);
        self->m_detailedText = NULL;
    }
    if (self->m_informativeText) {
        XClassDelete(self->m_informativeText);
        self->m_informativeText = NULL;
    }
    /* 补充标签/详细区/Show Details 按钮为消息框子控件，随父子链级联
       释放，此处不重复 delete（对标 Qt deleteLater 语义由析构兜住）。 */
    self->m_informativeLabel = NULL;
#if XPLAINTEXTEDIT_ON
    self->m_detailsText = NULL;
#endif
    self->m_detailsButton = NULL;
    if (self->m_iconPixmap) {
        XClassDelete((XClass*)self->m_iconPixmap);
        self->m_iconPixmap = NULL;
    }
#if XCHECKBOX_ON
    if (self->m_checkBox) {
        XClassDelete((XClass*)self->m_checkBox);
        self->m_checkBox = NULL;
    }
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

/** @brief 键盘：Esc=点击探测转义按钮（探测失败时无任何效果，不
 *         reject 不关闭——QMessageBox 覆盖了 QDialog 的 Esc→reject）；
 *         Ctrl+C=复制消息文本到剪贴板（Windows 语义）；Enter/Return=
 *         显式默认按钮（无显式值时交基类 autoDefault 探测）。
 *         （对标 QMessageBox::keyPressEvent，qmessagebox.cpp:1564-
 *         1636。） */
static void VXMessageBox_keyPressEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    if (box && event &&
        XEvent_type(event) == XEVENT_TYPE_KEY_PRESS) {
        int key = ((XKeyEvent*)event)->m_key;
        int mods = (int)((XKeyEvent*)event)->m_modifiers;
        if (key == (int)XKey_Escape) {
            /* 对标 qmessagebox.cpp:1568-1577：有探测转义按钮则点击，
               否则直接消费（Esc 无效果），一律不落回 reject。 */
            XAbstractButton* escape = xmsg_detectEscapeButton(box);
            if (escape) {
                XAbstractButton_click(escape);
            }
            XEvent_accept(event);
            return;
        }
#if XCLIPBOARD_ON
        if (key == (int)XKey_C &&
            (mods & (int)XKeyboardModifier_ControlModifier)) {
            /* 对标 qmessagebox.cpp:1595-1616（Q_OS_WIN）：Ctrl+C 复制
               「分隔线+标题+主文本+补充文本+按钮文本列表」到剪贴板。 */
            const XVector* buttons =
                box->m_buttonBox
                    ? XDialogButtonBox_buttons(box->m_buttonBox) : NULL;
            XString* payload = XString_create();
            int64_t i;
            int64_t n = buttons ? XVector_size_base(
                (const XContainer*)buttons) : 0;
            const XString* title =
                XWidget_windowTitle((const XWidget*)box);
            const char* sep = "---------------------------\n";
            if (payload) {
                XString_assign_utf8(payload, sep);
                XString_append_utf8(
                    payload,
                    title ? XString_toUtf8(title) : "");
                XString_append_utf8(payload, "\n");
                XString_append_utf8(payload, sep);
                XString_append_utf8(payload,
                                    XMessageBox_text(box));
                XString_append_utf8(payload, "\n");
                XString_append_utf8(payload, sep);
                if (box->m_informativeText) {
                    XString_append_utf8(
                        payload,
                        XString_toUtf8(box->m_informativeText));
                    XString_append_utf8(payload, "\n");
                    XString_append_utf8(payload, sep);
                }
                for (i = 0; i < n; ++i) {
                    XAbstractButton* b = *(XAbstractButton* const*)
                        XVector_At_Base(buttons, i, XAbstractButton*);
                    const XString* text =
                        b ? XAbstractButton_text(b) : NULL;
                    XString_append_utf8(
                        payload, text ? XString_toUtf8(text) : "");
                    XString_append_utf8(payload, "   ");
                }
                XString_append_utf8(payload, "\n");
                XString_append_utf8(payload, sep);
                {
                    XClipboard* clipboard = XGuiApplication_clipboard();
                    if (clipboard)
                        XClipboard_setText(clipboard, payload,
                                           XClipboardMode_Clipboard);
                }
                XClassDelete((XClass*)payload);
            }
            XEvent_accept(event);
            return;
        }
#endif /* XCLIPBOARD_ON */
        if (key == (int)XKey_Return || key == (int)XKey_Enter) {
            if (box->m_defaultButton) {
                XAbstractButton_click(box->m_defaultButton);
                XEvent_accept(event);
                return;
            }
            /* 无显式默认：下落基类 QDialog::keyPressEvent 语义（首个
               可见可用 autoDefault 按钮）。 */
        }
    }
    /* 其余按键静态转发父类 XDialog 实现（对标 QMessageBox::keyPress
       Event 末尾 QDialog::keyPressEvent(e)）：经 XClass_Parent 取
       XDialog 类虚表 keyPress 槽位。此前经 XWidget_keyPressEvent_base
       转发：该 _base 入口按对象虚表再分派回最派生重载，未处理的按键
       即无界自递归栈溢出（复扫 P0-1）。 */
    XClass_Parent(XDialog, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 显示事件：显示时刻收尾装配（对标 QMessageBoxPrivate::
 *         setVisible 的 last-minute setup + QMessageBox::showEvent）。
 *         ①零按钮自动补 Ok（autoAddOkButton）；②探测转义按钮；③清
 *         空最近点击；④尺寸收口；⑤末尾交 QDialog::showEvent 居中。 */
static void VXMessageBox_showEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    if (box && event && XEvent_type(event) == XEVENT_TYPE_SHOW) {
        box->m_clicked = NULL; /* 对标 QMessageBox::showEvent。 */
        if (box->m_autoAddOkButton && box->m_buttonBox &&
            XContainer_isEmpty_base(
                (const XContainer*)XDialogButtonBox_buttons(
                    box->m_buttonBox))) {
            /* 对标 autoAddOkButton（qmessagebox.cpp:1663-1664）：
               显示时刻一个按钮都没有则自动加 Ok。 */
            XMessageBox_addButton_3(box,
                                    (int)XDialogButtonBoxStandard_Ok);
        }
        (void)xmsg_detectEscapeButton(box); /* 对标 detectEscapeButton。 */
        xmsg_updateSize(box); /* 对标 updateSize（setFixedSize 收口）。 */
    }
    /* 末尾静态父调用 QDialog::showEvent：未显式定位时按父窗口居中。 */
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 关闭事件：无转义按钮则忽略关闭（X 不可关）；有则等价点击
 *         转义按钮——基类 QDialog::closeEvent 走 reject 收口后回填
 *         clickedButton/result 为转义按钮位值。（对标 QMessageBox::
 *         closeEvent，qmessagebox.cpp:1513-1525。） */
static void VXMessageBox_closeEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    if (box && event && XEvent_type(event) == XEVENT_TYPE_CLOSE) {
        XAbstractButton* escape = xmsg_detectEscapeButton(box);
        if (!escape) {
            XEvent_ignore(event);
            return;
        }
        XClass_Parent(XDialog, EXWidget_CloseEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        if (!box->m_clicked) {
            box->m_clicked = escape;
            XDialog_setResult(&box->m_base,
                              xmsg_execReturnCode(box, escape));
        }
        /* 超出 Qt 严格对等（Qt 关闭路径同样不断开 open 连接）：按头
         * 文件契约「对话框关闭时自动断开」补的健壮性收口——关闭经
         * reject→done 发射 finished 时连接仍在（receiver 恰回调一次），
         * 收口后断开，防止连接与记录字段跨关闭存活、下次 open 叠连。 */
        xmsg_openCleanup(box);
        return;
    }
    XClass_Parent(XDialog, EXWidget_CloseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 图标区绘制（对标 Qt 6.8 qmessagebox.cpp
 *         QMessageBoxPrivate::standardIcon + setupLayout：图标按
 *         PM_MessageBoxIconSize 尺寸显示于文本左侧、内容区顶部对
 *         齐。此前 setIcon 仅存 m_icon、全文件无绘制调用，四种级别
 *         标准图标全部不渲染——夜间台账 #19）。图标来源优先级与
 *         Qt 一致：setIconPixmap 自定义位图优先，其次按 m_icon 分
 *         级取样式标准图标（XMessageBox_standardIcon → 样式
 *         SP_MessageBox* 图标，调用方持有，画后即删）。
 *  @details 历史（复扫-5 #31附1）：此前 XPainter drawImage 逐像素
 *           兜底路径丢源补偿量 sx0/sy0，图标目标被脏区/表面裁剪钳
 *           位后内容平移钳位偏移量（真机实测伪影 (391,341)=真身
 *           (259,262)+(132,79)，5 轮复现），当时以"脏区完全包含图
 *           标矩形才盖章"绕行。现根修已落在 XPainter.c
 *           painterRaster_drawImage：兜底循环按 dst(bx+i,by+j) ←
 *           src(sx0+i,sy0+j) 映射、双级裁剪源偏移累积，与
 *           blitImageRegion 行内核一致（对标 Qt drawImage 源/目标矩
 *           形映射 + systemClip 只限写入域不平移内容），绕行门撤
 *           除、直画恢复；裁剪语义交给 painter 统一处理，部分相交
 *           批内图标按裁剪截断、不越域不平移。 */
static void xmsg_drawIcon(XMessageBox* box, XEvent* event)
{
    XImage* image;
    XPainter painter;
    XPoint offset;
    XIcon* icon = NULL;
    int top;
    XRect dirty;
    XRect iconRect;
    int w;
    int h;
    if (!box || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    if (!xmsg_hasIconArea(box)) return;
    top = xmsg_contentTop(box);
    /* 事件脏区钳到控件自身矩形（paintTree 已按子树裁剪，此处防御
     * 性再裁一次；对标 qwidget.cpp paintEvent 内 dirty 区域语义）。 */
    dirty = XPaintEvent_rect((XPaintEvent*)event);
    w = XWidget_width((XWidget*)box);
    h = XWidget_height((XWidget*)box);
    if (dirty.x < 0) { dirty.width += dirty.x; dirty.x = 0; }
    if (dirty.y < 0) { dirty.height += dirty.y; dirty.y = 0; }
    if (dirty.x + dirty.width > w) dirty.width = w - dirty.x;
    if (dirty.y + dirty.height > h) dirty.height = h - dirty.y;
    if (dirty.width <= 0 || dirty.height <= 0) return;
    XRect_init(&iconRect, 16, top, 32, 32);
    /* 脏区包含判定绕行门已撤（XPainter drawImage 源补偿根修，见上
     * @details）：恢复直画，由 painter 裁剪统一保证不越脏区、不平
     * 移内容。 */
    image = XWidget_paintImage((XWidget*)box);
    if (!image) return;
    if (box->m_iconPixmap) {
        XPixmap pm;
        icon = XIcon_create();
        if (!icon) return;
        XPixmap_init(&pm);
        XPixmap_init_image(&pm, (const XImage*)box->m_iconPixmap, 0);
        XIcon_init_pixmap(icon, &pm);
        XClassDeinit(&pm);
    } else {
        icon = XMessageBox_standardIcon(box->m_icon);
    }
    if (!icon) return;
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, image)) {
        offset = XWidget_paintOffset((XWidget*)box);
        if (offset.x != 0 || offset.y != 0)
            XPainter_translate(&painter, (float)offset.x, (float)offset.y);
        XIcon_paint(icon, &painter, iconRect.x, iconRect.y,
                    iconRect.width, iconRect.height,
                    (uint32_t)(XAlignment_Left | XAlignment_Top),
                    XIconMode_Normal, XIconState_Off);
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    XClassDelete(icon);
}

/** @brief 绘制：先静态父调用 XDialog 面板绘制，再叠画图标区。 */
static void VXMessageBox_paintEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    if (!self || !event) return;
    /* 面板底色/描边/标题沿用 XDialog 实现（XClass_Parent 静态父调
       用，避免经对象虚表再分派回本重载）。 */
    XClass_Parent(XDialog, EXWidget_PaintEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    xmsg_drawIcon(box, event);
}

XVtable* XMessageBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMessageBox)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_messageBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXMessageBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXMessageBox_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXMessageBox_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VXMessageBox_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMessageBox_deinit);
    return XVTABLE_DEFAULT;
}

void XMessageBox_init(XMessageBox* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 对标 QMessageBox 构造（qmessagebox.cpp:838/865 + qdialog.cpp:
     * 374-378）：默认窗口标志 = Dialog | MSWindowsFixedSizeDialogHint |
     * WindowTitleHint | WindowSystemMenuHint | WindowCloseButtonHint。
     * Dialog 类型使消息盒恒为独立顶层窗口（win32 后端映射
     * WS_OVERLAPPEDWINDOW：系统标题栏+系统菜单+关闭钮；固定尺寸 hint
     * 去厚边框），四个 hint 无条件叠加（Qt 亦然）。 */
    if ((flags & (XWidgetFlags)XWindowType_TypeMask) == 0)
        flags |= (XWidgetFlags)XWindowType_Dialog;
    flags |= (XWidgetFlags)XWindowType_MSWindowsFixedSizeDialogHint |
             (XWidgetFlags)XWindowType_WindowTitleHint |
             (XWidgetFlags)XWindowType_WindowSystemMenuHint |
             (XWidgetFlags)XWindowType_WindowCloseButtonHint;
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMessageBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_icon = (int)XMessageBoxIcon_NoIcon;
    self->m_options = 0;
    self->m_autoAddOkButton = true; /* 对标 autoAddOkButton(true)。 */
    self->m_detailsVisible = false;
    /* 对标 QMessageBoxPrivate::init 的 q->setModal(true)
       （qmessagebox.cpp:281）：消息盒构造即应用模态（modal 属性）。 */
    XDialog_setModal(&self->m_base, true);
    self->m_text = XString_create();
    self->m_detailedText = XString_create();
    self->m_informativeText = XString_create();
#if XDIALOGBUTTONBOX_ON
    self->m_buttonBox = XDialogButtonBox_create(self, 0);
    if (self->m_buttonBox) {
        /* 对标 setupLayout：SH_MessageBox_CenterButtons 在 Windows 上
           为真——消息盒按钮居中。 */
        XDialogButtonBox_setCenterButtons(self->m_buttonBox, true);
        /* 对标 qmessagebox.cpp:274-275：QMessageBox 只订阅按钮盒的
           clicked 自行收口 done(标准位值)；不经 accepted/rejected
           桥接（那只能产出 0/1，破坏 exec 返回标准按钮位值语义）。 */
        XObject_connect_1((XObject*)self->m_buttonBox,
            (size_t)XDialogButtonBox_clicked_signal(
                self->m_buttonBox, NULL),
            (XObject*)self, xmsg_clickedSlot, XConnectionType_Direct);
    }
#endif
#if XLABEL_ON
    self->m_textLabel = XLabel_create(self, 0);
    if (self->m_textLabel)
        XLabel_setText_2(self->m_textLabel, "");
#endif
    XWidget_resize(self, 320, 140);
}

XMessageBox* XMessageBox_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XMessageBox* self =
        (XMessageBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMessageBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本与图标 ==================== */

void XMessageBox_setText(XMessageBox* self, const char* utf8)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setText_2(self->m_textLabel, utf8 ? utf8 : "");
    if (!self->m_text) self->m_text = XString_create();
    if (self->m_text)
        XString_assign_utf8(self->m_text, utf8 ? utf8 : "");
    /* 对标 QMessageBox::setText 末尾 updateSize：可见时按新文本宽度
       重算固定尺寸（不可见时 showEvent 会再算）。 */
    if (XWidget_isVisible((XWidget*)self))
        xmsg_updateSize(self);
}

const char* XMessageBox_text(const XMessageBox* self)
{
    if (!self || !self->m_textLabel)
        return "";
    return self->m_text ? XString_toUtf8(self->m_text) : "";
}

void XMessageBox_setIcon(XMessageBox* self, XMessageBoxIcon icon)
{
    if (!self) return;
    if (self->m_icon == (int)icon) return;
    self->m_icon = (int)icon;
    /* 图标区存在性/内容随分级变化（对标 Qt setIcon → updateIcon）。 */
    xmsg_setupText(self);
    XWidget_update((XWidget*)self);
}

XMessageBoxIcon XMessageBox_icon(const XMessageBox* self)
{
    return self ? (XMessageBoxIcon)self->m_icon
                : XMessageBoxIcon_NoIcon;
}

/* ==================== 按钮管理 ==================== */

void XMessageBox_setStandardButtons(XMessageBox* self, int buttons)
{
    if (!self || !self->m_buttonBox) return;
    XDialogButtonBox_setStandardButtons(self->m_buttonBox, buttons);
    /* 对标 QMessageBox::setStandardButtons：显式设置清除自动补 Ok 标志
       （qmessagebox.cpp:954）。 */
    self->m_autoAddOkButton = false;
    xmsg_setupButtonBox(self);
}

int XMessageBox_standardButtons(const XMessageBox* self)
{
    if (!self || !self->m_buttonBox) return 0;
    return XDialogButtonBox_standardButtons(self->m_buttonBox);
}

XAbstractButton* XMessageBox_button(const XMessageBox* self,
                                    XDialogButtonBoxStandardButton which)
{
    if (!self || !self->m_buttonBox) return NULL;
    return (XAbstractButton*)XDialogButtonBox_button(
        self->m_buttonBox, which);
}

XAbstractButton* XMessageBox_clickedButton(const XMessageBox* self)
{
    return self ? self->m_clicked : NULL;
}

/* ==================== 模态执行 ==================== */

XDialogButtonBoxStandardButton XMessageBox_exec(XMessageBox* self)
{
    /* 单一收口：委托 QDialog::exec 语义的 XDialog_exec（应用模态、
       WaitForMoreEvents 防忙等、递归检测 -1）。返回值为 result()——
       按钮点击经 done(标准位值) 回填，即被点标准按钮位值（对标
       Qt 6.8 QMessageBox 无 exec 重写、result 被回填的运行时形态）。 */
    if (!self) return XDialogButtonBoxStandard_NoButton;
    return (XDialogButtonBoxStandardButton)
        XDialog_exec(&self->m_base);
}

/* ==================== 非阻塞打开（对标 QMessageBox::open） ==================== */

void XMessageBox_open_2(XMessageBox* self, XObject* receiver,
                               XSlotFunc1 member, bool buttonPayload)
{
    if (!self || !member) return;
    /* 重复 open 防叠连：旧记录尚存时先按旧载荷断开旧连接（disconnect_1
     * 只移除首个匹配，直接叠连会残留旧连接，receiver 每次收口被多路
     * 误触发）。 */
    if (self->m_openReceiver || self->m_openMember)
        xmsg_openCleanup(self);
    self->m_openReceiver = receiver;
    self->m_openMember = member;
    self->m_openButtonPayload = buttonPayload;
    if (receiver) {
        XObject_connect_1(
            (XObject*)self,
            (size_t)(buttonPayload ? XMessageBox_buttonClicked_signal
                                   : XDialog_finished_signal),
            receiver, member, XConnectionType_Direct);
    }
    /* 对标 QDialog::open：窗口模态显示并立即返回。 */
    XDialog_open(&self->m_base);
}

/* ==================== 静态便捷方法 ==================== */

/** @brief 静态便捷统一实现（对标 showNewMessageBox，qmessagebox.cpp:
 *         1724-1762）：装配按钮并按 defaultButton/首个 AcceptRole 选
 *         默认按钮，exec 阻塞返回被点标准按钮位值。 */
static XDialogButtonBoxStandardButton xmsg_runStatic(
    XWidget* parent, const char* title, const char* text, int buttons,
    int icon, XDialogButtonBoxStandardButton defaultButton)
{
    XMessageBox* box = XMessageBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
    XDialogButtonBoxStandardButton result;
    if (!box) return XDialogButtonBoxStandard_NoButton;
    if (title && title[0]) {
        XString* windowTitle = XString_create_utf8(title);
        XWidget_setWindowTitle((XWidget*)box, windowTitle);
        if (windowTitle) XClassDelete((XClass*)windowTitle);
    }
    XMessageBox_setText(box, text);
    XMessageBox_setIcon(box, (XMessageBoxIcon)icon);
    /* 按位序装配标准按钮；defaultButton==NoButton 时取第一个
       AcceptRole 按钮为默认（qmessagebox.cpp:1745-1757）。 */
    {
        uint32_t mask = (uint32_t)XDialogButtonBoxStandard_FirstButton;
        while (mask <= (uint32_t)XDialogButtonBoxStandard_LastButton) {
            uint32_t sb = (uint32_t)buttons & mask;
            mask <<= 1;
            if (!sb) continue;
            {
                XAbstractButton* button =
                    XMessageBox_addButton_3(box, (int)sb);
                if (box->m_defaultButton) continue;
                if ((defaultButton == XDialogButtonBoxStandard_NoButton &&
                     button &&
                     XMessageBox_buttonRole(box, button) ==
                         XMessageBoxButtonRole_AcceptRole) ||
                    (XDialogButtonBoxStandardButton)sb == defaultButton)
                    XMessageBox_setDefaultButton(box, button);
            }
        }
    }
    result = XMessageBox_exec(box);
    /* 递归 exec 的 -1 防护（对标 exec()==-1 → Cancel）。 */
    if ((int)result == -1)
        result = XDialogButtonBoxStandard_Cancel;
    XClassDelete(box);
    return result;
}

XDialogButtonBoxStandardButton XMessageBox_information(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return XMessageBox_information_2(parent, title, text, buttons,
                                     XDialogButtonBoxStandard_NoButton);
}

XDialogButtonBoxStandardButton XMessageBox_information_2(
    XWidget* parent, const char* title, const char* text, int buttons,
    XDialogButtonBoxStandardButton defaultButton)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Information, defaultButton);
}

XDialogButtonBoxStandardButton XMessageBox_warning(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return XMessageBox_warning_2(parent, title, text, buttons,
                                 XDialogButtonBoxStandard_NoButton);
}

XDialogButtonBoxStandardButton XMessageBox_warning_2(
    XWidget* parent, const char* title, const char* text, int buttons,
    XDialogButtonBoxStandardButton defaultButton)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Warning, defaultButton);
}

XDialogButtonBoxStandardButton XMessageBox_critical(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return XMessageBox_critical_2(parent, title, text, buttons,
                                  XDialogButtonBoxStandard_NoButton);
}

XDialogButtonBoxStandardButton XMessageBox_critical_2(
    XWidget* parent, const char* title, const char* text, int buttons,
    XDialogButtonBoxStandardButton defaultButton)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Critical, defaultButton);
}

XDialogButtonBoxStandardButton XMessageBox_question(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return XMessageBox_question_2(parent, title, text, buttons,
                                  XDialogButtonBoxStandard_NoButton);
}

XDialogButtonBoxStandardButton XMessageBox_question_2(
    XWidget* parent, const char* title, const char* text, int buttons,
    XDialogButtonBoxStandardButton defaultButton)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Question, defaultButton);
}

void XMessageBox_about(XWidget* parent, const char* title,
                       const char* text)
{
    xmsg_runStatic(parent, title, text,
                   (int)XDialogButtonBoxStandard_Ok,
                   (int)XMessageBoxIcon_Information,
                   XDialogButtonBoxStandard_NoButton);
}







































/* ==================== 补充文本 ==================== */

void XMessageBox_setDetailedText(XMessageBox* self, const char* utf8)
{
    if (!self) return;
    if (!utf8 || !utf8[0]) {
        /* 对标 setDetailedText(空串)：移除详细区与 Show Details 按钮
           （qmessagebox.cpp:2608-2622）。按钮必须经 removeButton 从按
           钮盒摘除（仅置 NULL 会残留僵尸按钮：保持几何可见可点，点击
           走普通收口误关对话框），再隐藏本体；控件为消息框子控件，
           随父子链级联释放，此处不 delete。复位后再次 setDetailedText
           (非空) 按需重建，不累积重复按钮。 */
        if (self->m_detailedText)
            XString_assign_utf8(self->m_detailedText, "");
#if XPLAINTEXTEDIT_ON
        if (self->m_detailsButton) {
            XAbstractButton* detailsBtn = self->m_detailsButton;
            XMessageBox_removeButton(self, detailsBtn);
            XWidget_hide((XWidget*)detailsBtn);
        }
        if (self->m_detailsText)
            XWidget_hide((XWidget*)self->m_detailsText);
        self->m_detailsText = NULL;
        self->m_detailsButton = NULL;
        self->m_detailsVisible = false;
#endif
        xmsg_updateSize(self);
        XWidget_update((XWidget*)self);
        return;
    }
    if (!self->m_detailedText) self->m_detailedText = XString_create();
    if (self->m_detailedText)
        XString_assign_utf8(self->m_detailedText, utf8);
#if XPLAINTEXTEDIT_ON
    if (!self->m_detailsText) {
        /* 对标 setDetailedText(非空)：自动追加只读详细区（默认隐藏）
           与 Show Details... 切换按钮（ActionRole；追加按钮不改变
           autoAddOkButton 语义，QTBUG-39334 口径）。 */
        self->m_detailsText = XPlainTextEdit_create(self, 0);
        if (self->m_detailsText) {
            XPlainTextEdit_setPlainText(self->m_detailsText, utf8);
            XPlainTextEdit_setReadOnly(self->m_detailsText, true);
            XWidget_hide((XWidget*)self->m_detailsText);
        }
        if (!self->m_detailsButton) {
            XPushButton* details = XPushButton_create(self, 0);
            if (details) {
                bool autoAdd = self->m_autoAddOkButton;
                XPushButton_setText_2(details, "Show Details...");
                XMessageBox_addButton(self,
                                      (XAbstractButton*)details,
                                      (int)XMessageBoxButtonRole_ActionRole);
                self->m_autoAddOkButton = autoAdd; /* QTBUG-39334。 */
                self->m_detailsButton = (XAbstractButton*)details;
                XWidget_show((XWidget*)details);
            }
        }
    } else if (self->m_detailsText) {
        XPlainTextEdit_setPlainText(self->m_detailsText, utf8);
    }
#endif
    xmsg_updateSize(self);
    XWidget_update((XWidget*)self);
}

const char* XMessageBox_detailedText(const XMessageBox* self)
{
    if (!self || !self->m_detailedText) return "";
    return XString_toUtf8(self->m_detailedText);
}

void XMessageBox_setInformativeText(XMessageBox* self, const char* utf8)
{
    if (!self) return;
    if (!utf8 || !utf8[0]) {
        /* 对标 setInformativeText(空串)：移除补充标签
           （qmessagebox.cpp:2668-2673）。 */
        if (self->m_informativeText)
            XString_assign_utf8(self->m_informativeText, "");
        self->m_informativeLabel = NULL;
        xmsg_updateSize(self);
        XWidget_update((XWidget*)self);
        return;
    }
    if (!self->m_informativeText)
        self->m_informativeText = XString_create();
    if (self->m_informativeText)
        XString_assign_utf8(self->m_informativeText, utf8);
#if XLABEL_ON
    if (!self->m_informativeLabel) {
        /* 对标：补充文本标签对象（qt_msgbox_informativelabel）按需
           创建，位于消息文本下一行。 */
        self->m_informativeLabel = XLabel_create(self, 0);
        if (self->m_informativeLabel) {
            XLabel_setWordWrap(self->m_informativeLabel, true);
            XWidget_show((XWidget*)self->m_informativeLabel);
        }
    }
    if (self->m_informativeLabel)
        XLabel_setText_2(self->m_informativeLabel, utf8);
#endif
    xmsg_updateSize(self);
    XWidget_update((XWidget*)self);
}

const char* XMessageBox_informativeText(const XMessageBox* self)
{
    if (!self || !self->m_informativeText) return "";
    return XString_toUtf8(self->m_informativeText);
}

/* ==================== 按钮管理 ==================== */

void XMessageBox_addButton(XMessageBox* self, XAbstractButton* button,
                           int role)
{
    if (!self || !self->m_buttonBox || !button) return;
    XDialogButtonBox_addButton(self->m_buttonBox, button, role);
    /* 对标 QMessageBox::addButton：显式加钮清除自动补 Ok 标志
       （qmessagebox.cpp:908）。 */
    self->m_autoAddOkButton = false;
}

XAbstractButton* XMessageBox_addButton_2(XMessageBox* self,
                                         const char* text, int role)
{
    XPushButton* btn;
    if (!self || !self->m_buttonBox || !text) return NULL;
    btn = XDialogButtonBox_addButton_2(self->m_buttonBox, text, role);
    self->m_autoAddOkButton = false;
    return (XAbstractButton*)btn;
}

XAbstractButton* XMessageBox_addButton_3(XMessageBox* self, int button)
{
    XAbstractButton* btn;
    if (!self || !self->m_buttonBox) return NULL;
    btn = (XAbstractButton*)XDialogButtonBox_addButton_3(
        self->m_buttonBox, button);
    self->m_autoAddOkButton = false;
    return btn;
}

void XMessageBox_setDefaultButton(XMessageBox* self,
                                  XAbstractButton* button)
{
    if (!self) return;
    self->m_defaultButton = button;
    /* 同步按钮 default 标志：对话框 Enter 探测（XDialog keyPress 的
       autoDefault/default 扫描）与消息盒显式默认双通道一致。 */
#if XPUSHBUTTON_ON
    if (button && XClassGetVtable((XObject*)button) ==
                      XPushButton_class_init())
        XPushButton_setDefault((XPushButton*)button, true);
#endif
}

void XMessageBox_setDefaultButton_2(XMessageBox* self, int button)
{
    XAbstractButton* btn;
    if (!self) return;
    btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                    button);
    if (!btn && self->m_buttonBox) {
        btn = XMessageBox_addButton_3(self, button);
    }
    if (btn) XMessageBox_setDefaultButton(self, btn);
}

XAbstractButton* XMessageBox_defaultButton(const XMessageBox* self)
{ return self ? self->m_defaultButton : NULL; }

void XMessageBox_setEscapeButton(XMessageBox* self, XAbstractButton* button)
{
    if (self) self->m_escapeButton = button;
}

void XMessageBox_setEscapeButton_2(XMessageBox* self, int button)
{
    XAbstractButton* btn;
    if (!self) return;
    btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                    button);
    if (!btn && self->m_buttonBox) {
        XDialogButtonBox_addButton_3(self->m_buttonBox, button);
        btn = (XAbstractButton*)XDialogButtonBox_button(self->m_buttonBox,
                                                        button);
    }
    if (btn) self->m_escapeButton = btn;
}

XAbstractButton* XMessageBox_escapeButton(const XMessageBox* self)
{ return self ? self->m_escapeButton : NULL; }

XVector* XMessageBox_buttons(const XMessageBox* self)
{
    const XVector* src;
    XVector* out;
    size_t i;
    size_t n;
    if (!self || !self->m_buttonBox) return NULL;
    src = XDialogButtonBox_buttons(self->m_buttonBox);
    if (!src) return NULL;
    n = XVector_size_base((const XContainer*)src);
    out = XVector_Create(XAbstractButton*);
    if (!out) return NULL;
    for (i = 0; i < n; ++i) {
        XAbstractButton* b = XVector_At_Base(src, (int64_t)i,
                                             XAbstractButton*);
        XVector_push_back_1_base(out, &b);
    }
    return out;
}

int XMessageBox_standardButton(const XMessageBox* self,
                               XAbstractButton* button)
{
    /* 对标 Qt QMessageBox::standardButton：标准值登记在按钮盒的
     * m_buttons/m_standards 平行表里，直接委托反查（自定义按钮返回
     * NoButton）。此前走自持 m_standards 向量——该向量 init 创建后
     * 从未写入，恒为空，任何按钮都反查成 NoButton。 */
    if (!self || !self->m_buttonBox)
        return (int)XDialogButtonBoxStandard_NoButton;
    return (int)XDialogButtonBox_standardButton(self->m_buttonBox, button);
}

void XMessageBox_setOptions(XMessageBox* self, XMessageBoxOptions options)
{
    if (self) self->m_options = options;
}

XMessageBoxOptions XMessageBox_options(const XMessageBox* self)
{ return self ? self->m_options : 0; }

bool XMessageBox_testOption(const XMessageBox* self, XMessageBoxOption option)
{
    return self ? ((self->m_options & (int)option) != 0) : false;
}

void XMessageBox_setOption(XMessageBox* self, XMessageBoxOption option,
                           bool on)
{
    if (!self) return;
    if (on) self->m_options |= (int)option;
    else self->m_options &= ~(int)option;
}

void* XMessageBox_buttonClicked_signal(XMessageBox* self,
                                       XAbstractButton* button)
{
    (void)self; (void)button;
    return (void*)(size_t)XMessageBox_buttonClicked_signal;
}

/* ==================== 复选框 / 图标位图 / 文本呈现 ==================== */

#if XCHECKBOX_ON
void XMessageBox_setCheckBox(XMessageBox* self, XCheckBox* checkBox)
{
    if (!self || self->m_checkBox == checkBox) return;
    if (self->m_checkBox) {
        XClassDelete((XClass*)self->m_checkBox);
        self->m_checkBox = NULL;
    }
    self->m_checkBox = checkBox;
    if (self->m_checkBox) {
        /* 收编：改为消息框子控件并显示在文本与按钮盒之间。 */
        XWidget_setParentPlain((XWidget*)self->m_checkBox, (XWidget*)self);
        XWidget_show((XWidget*)self->m_checkBox);
        xmsg_setupCheckBox(self);
    }
}

XCheckBox* XMessageBox_checkBox(const XMessageBox* self)
{ return self ? self->m_checkBox : NULL; }
#endif /* XCHECKBOX_ON */

void XMessageBox_setIconPixmap(XMessageBox* self, const XImage* pixmap)
{
    if (!self) return;
    if (!pixmap) {
        if (self->m_iconPixmap) {
            XClassDelete((XClass*)self->m_iconPixmap);
            self->m_iconPixmap = NULL;
            /* 清除后图标列可能消失，文本回填让位（对标 Qt setPixmap
               (QPixmap()) → updateIcon 布局刷新）。 */
            xmsg_setupText(self);
            XWidget_update((XWidget*)self);
        }
        return;
    }
    if (!self->m_iconPixmap) {
        self->m_iconPixmap = XImage_create();
        if (!self->m_iconPixmap) return;
    }
    XClassCopy(self->m_iconPixmap, (const XClass*)pixmap);
    /* 对标 Qt setPixmap：设置后 icon() 回读为 NoIcon
       （qmessagebox.cpp:1423-1429 口径），图标列布局按位图让位。 */
    self->m_icon = (int)XMessageBoxIcon_NoIcon;
    xmsg_setupText(self);
    XWidget_update((XWidget*)self);
}

const XImage* XMessageBox_iconPixmap(const XMessageBox* self)
{ return self ? self->m_iconPixmap : NULL; }

void XMessageBox_setTextFormat(XMessageBox* self, XLabelTextFormat format)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setTextFormat(self->m_textLabel, format);
}

XLabelTextFormat XMessageBox_textFormat(const XMessageBox* self)
{
    if (!self || !self->m_textLabel) return XLabelTextFormat_AutoText;
    return XLabel_textFormat(self->m_textLabel);
}

void XMessageBox_setTextInteractionFlags(XMessageBox* self,
                                         XLabelTextInteractionFlags flags)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setTextInteractionFlags(self->m_textLabel, flags);
}

XLabelTextInteractionFlags XMessageBox_textInteractionFlags(
    const XMessageBox* self)
{
    if (!self || !self->m_textLabel) return 0;
    return XLabel_textInteractionFlags(self->m_textLabel);
}

/* ==================== 按钮角色 / 移除 / 文本 ==================== */

XMessageBoxButtonRole XMessageBox_buttonRole(const XMessageBox* self,
                                             XAbstractButton* button)
{
    if (!self || !self->m_buttonBox || !button)
        return XMessageBoxButtonRole_InvalidRole;
    return (XMessageBoxButtonRole)XDialogButtonBox_buttonRole(
        self->m_buttonBox, button);
}

void XMessageBox_removeButton(XMessageBox* self, XAbstractButton* button)
{
    if (!self || !self->m_buttonBox || !button) return;
    XDialogButtonBox_removeButton(self->m_buttonBox, button);
    if (self->m_defaultButton == button) self->m_defaultButton = NULL;
    if (self->m_escapeButton == button) self->m_escapeButton = NULL;
    if (self->m_clicked == button) self->m_clicked = NULL;
#if XPLAINTEXTEDIT_ON
    if (self->m_detailsButton == button) self->m_detailsButton = NULL;
#endif
    xmsg_setupButtonBox(self);
}

/** @brief 按标准按钮值取按钮盒中对应按钮（内部工具）。 */
static XAbstractButton* xmsg_standardButtonAt(XMessageBox* self, int button)
{
    if (!self || !self->m_buttonBox) return NULL;
    return (XAbstractButton*)XDialogButtonBox_button(
        self->m_buttonBox, (XDialogButtonBoxStandardButton)button);
}

XString* XMessageBox_buttonText(const XMessageBox* self, int button)
{
    XString* out;
    const XString* text;
    XAbstractButton* btn = xmsg_standardButtonAt((XMessageBox*)self, button);
    out = XString_create();
    if (!out) return NULL;
    text = btn ? XAbstractButton_text(btn) : NULL;
    if (text) XString_assign(out, text);
    else XString_assign_utf8(out, "");
    return out;
}

void XMessageBox_setButtonText(XMessageBox* self, int button,
                               const XString* text)
{
    XAbstractButton* btn = xmsg_standardButtonAt(self, button);
    if (!btn) return;
    if (text) XAbstractButton_setText(btn, text);
    else XAbstractButton_setText_2(btn, "");
}

void XMessageBox_setButtonText_2(XMessageBox* self, int button,
                                 const char* utf8)
{
    XAbstractButton* btn = xmsg_standardButtonAt(self, button);
    if (!btn) return;
    XAbstractButton_setText_2(btn, utf8 ? utf8 : "");
}

/* ==================== 静态便捷 ==================== */

void XMessageBox_aboutQt(XWidget* parent, const XString* title)
{
    /* 对标 QMessageBox::aboutQt：弹出模态 About Qt 消息盒（Ok 单按
       钮、Information 图标）。Qt 原文含 Qt 版本号与官网链接；XGui 无
       Qt 运行时信息，以固定文档化文案等价呈现（对标 about 呈现形态：
       分级图标 + 正文 + Ok）。 */
    XString* boxTitle = XString_create_utf8(
        (title && XString_toUtf8(title) && XString_toUtf8(title)[0])
            ? XString_toUtf8(title) : "About Qt");
    if (!boxTitle) return;
    {
        const char* utf8 = XString_toUtf8(boxTitle);
        XMessageBox_about(parent, utf8 ? utf8 : "About Qt",
                          "<h3>About Qt</h3>\n"
                          "XGui 对标 Qt 6.8 QMessageBox 的等效实现。"
                          "Qt 为跨平台应用框架，更多信息参见 "
                          "qt.io。本对话框用于演示 aboutQt 便捷入口。");
    }
    XClassDelete((XClass*)boxTitle);
}

XIcon* XMessageBox_standardIcon(int icon)
{
    XStyle* style;
    int sp;
    switch (icon) {
    case XMessageBoxIcon_Information:
        sp = XStyleSP_MessageBoxInformation; break;
    case XMessageBoxIcon_Warning:
        sp = XStyleSP_MessageBoxWarning; break;
    case XMessageBoxIcon_Critical:
        sp = XStyleSP_MessageBoxCritical; break;
    case XMessageBoxIcon_Question:
        sp = XStyleSP_MessageBoxQuestion; break;
    default:
        return NULL;
    }
    style = XApplication_style();
    if (!style) return NULL;
    return XStyle_standardIcon(style, sp, NULL, NULL);
}

#endif /* XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON */