#include "XWizard.h"
#include "XStringList.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XLabel.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include <stdio.h>

#if XWIDGET_ON && XDIALOG_ON && XWIZARD_ON

/* ==================== XWizardPage ==================== */

static void VXWizardPage_deinit(XWizardPage* self)
{
    int i;
    if (!self) return;
    if (self->m_title) {
        XString_delete_base(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_subTitle) {
        XString_delete_base(self->m_subTitle);
        self->m_subTitle = NULL;
    }
    if (self->m_pixmap) {
        XString_delete_base(self->m_pixmap);
        self->m_pixmap = NULL;
    }
    for (i = 0; i < XWizardButton_NStandardButtons; ++i) {
        if (self->m_buttonTexts[i]) {
            XString_delete_base(self->m_buttonTexts[i]);
            self->m_buttonTexts[i] = NULL;
        }
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* -------------------- 四虚槽默认实现（对标 Qt 6.8.3） -------------------- */

/**
 * @brief      initializePage 默认实现（对标 QWizardPage::initializePage）。
 * @details    Qt 默认为空操作；本实现的字段初始化无页面级托管对象，
 *             同样保持空操作。
 * @param      self 目标页面指针；可为 NULL。
 * @return     无返回值。
 */
static void VXWizardPage_initializePage(XWizardPage* self)
{
    (void)self;
}

/**
 * @brief      cleanupPage 默认实现（对标 QWizardPage::cleanupPage）。
 * @details    Qt 默认把本页字段恢复为初始值；本实现的字段表由向导持有
 *             且无初始值快照，故保持空操作。
 * @param      self 目标页面指针；可为 NULL。
 * @return     无返回值。
 */
static void VXWizardPage_cleanupPage(XWizardPage* self)
{
    (void)self;
}

/**
 * @brief      validatePage 默认实现（对标 QWizardPage::validatePage）。
 * @details    Qt 6.8.3 默认直接返回 true（推荐用 isComplete()/必填字段
 *             控制翻页而非重写本槽）。
 * @param      self 目标页面指针；可为 NULL。
 * @return     始终返回 true。
 */
static bool VXWizardPage_validatePage(XWizardPage* self)
{
    /* 对标 Qt 6.8.3：validatePage 默认直接返回 true（推荐用
     * isComplete() 控制 Next 按钮使能，而非重写本槽拦截翻页）。 */
    (void)self;
    return true;
}

/**
 * @brief      nextId 默认实现（对标 QWizardPage::nextId）。
 * @details    仿 Qt：无所属向导返回 -1；否则线性查找本页在向导页表中
 *             的索引，返回 index + 1；已是末页返回 -1。本项目页 id 即
 *             页索引（0 起）。
 * @param      self 目标页面指针；可为 NULL。
 * @return     下一页索引；无后续页返回 -1。
 */
static int VXWizardPage_nextId(const XWizardPage* self)
{
    const XWizard* wiz;
    int i;
    if (!self) return -1;
    wiz = (const XWizard*)self->m_wizard;
    if (!wiz) return -1;
    for (i = 0; i < wiz->m_pageCount; ++i) {
        if (wiz->m_pages[i] == self)
            return (i + 1 < wiz->m_pageCount) ? i + 1 : -1;
    }
    return -1;
}

XVtable* XWizardPage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWizardPage)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWizardPage_deinit);
    /* 四个公开虚槽默认实现注册（子类可 XVTABLE_OVERLOAD_DEFAULT 覆盖）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_InitializePage,
                             VXWizardPage_initializePage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_CleanupPage,
                             VXWizardPage_cleanupPage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_ValidatePage,
                             VXWizardPage_validatePage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_NextId, VXWizardPage_nextId);
    return XVTABLE_DEFAULT;
}

void XWizardPage_init(XWizardPage* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XWizardPage);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_complete = true;
    self->m_title = XString_create();
    self->m_subTitle = XString_create();
}

XWizardPage* XWizardPage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XWizardPage* self = (XWizardPage*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XWizardPage_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* -------------------- 四虚槽分派函数（查虚表调用，子类覆盖即生效） -------------------- */

void XWizardPage_initializePage(XWizardPage* self)
{
    if (!self || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXWizardPage_InitializePage,
                         void (*)(XWizardPage*))(self);
}

void XWizardPage_cleanupPage(XWizardPage* self)
{
    if (!self || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXWizardPage_CleanupPage,
                         void (*)(XWizardPage*))(self);
}

bool XWizardPage_validatePage(XWizardPage* self)
{
    if (!self || !XClassGetVtable(self)) return true;
    return XClassGetVirtualFunc(self, EXWizardPage_ValidatePage,
                                bool (*)(XWizardPage*))(self);
}

int XWizardPage_nextId(const XWizardPage* self)
{
    if (!self || !XClassGetVtable(self)) return -1;
    return XClassGetVirtualFunc(self, EXWizardPage_NextId,
                                int (*)(const XWizardPage*))(self);
}

struct XWizard* XWizardPage_wizard(const XWizardPage* self)
{
    return self ? self->m_wizard : NULL;
}

void XWizardPage_setTitle(XWizardPage* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_title) self->m_title = XString_create();
    if (self->m_title)
        XString_assign_utf8(self->m_title, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
}

const char* XWizardPage_title(const XWizardPage* self)
{
    const char* text;
    if (!self || !self->m_title) return "";
    text = XString_toUtf8(self->m_title);
    return text ? text : "";
}

void XWizardPage_setSubTitle(XWizardPage* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_subTitle) self->m_subTitle = XString_create();
    if (self->m_subTitle)
        XString_assign_utf8(self->m_subTitle, utf8 ? utf8 : "");
}

const char* XWizardPage_subTitle(const XWizardPage* self)
{
    const char* text;
    if (!self || !self->m_subTitle) return "";
    text = XString_toUtf8(self->m_subTitle);
    return text ? text : "";
}

void XWizardPage_setButtonText(XWizardPage* self, XWizardButton which,
                               const char* utf8)
{
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons) return;
    if (!utf8) {
        if (self->m_buttonTexts[which]) {
            XString_delete_base(self->m_buttonTexts[which]);
            self->m_buttonTexts[which] = NULL;
        }
        return;
    }
    if (!self->m_buttonTexts[which])
        self->m_buttonTexts[which] = XString_create();
    if (self->m_buttonTexts[which])
        XString_assign_utf8(self->m_buttonTexts[which], utf8);
}

const char* XWizardPage_buttonText(const XWizardPage* self,
                                   XWizardButton which)
{
    const char* text;
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons)
        return "";
    if (!self->m_buttonTexts[which]) return "";
    text = XString_toUtf8(self->m_buttonTexts[which]);
    return text ? text : "";
}

void XWizardPage_setCommitPage(XWizardPage* self, bool commitPage)
{ if (self) self->m_commitPage = commitPage; }
bool XWizardPage_isCommitPage(const XWizardPage* self)
{ return self ? self->m_commitPage : false; }

void XWizardPage_setFinalPage(XWizardPage* self, bool finalPage)
{ if (self) self->m_finalPage = finalPage; }
bool XWizardPage_isFinalPage(const XWizardPage* self)
{ return self ? self->m_finalPage : false; }

void XWizardPage_setPixmap(XWizardPage* self, int which, const XString* path)
{
    (void)which;
    if (!self) return;
    if (!self->m_pixmap) self->m_pixmap = XString_create();
    if (self->m_pixmap) {
        if (path)
            XString_assign(self->m_pixmap, path);
        else
            XString_assign_utf8(self->m_pixmap, "");
    }
}

void XWizardPage_setPixmap_2(XWizardPage* self, int which, const char* utf8)
{
    XString tmp;
    if (!self) return;
    if (!utf8) {
        XWizardPage_setPixmap(self, which, NULL);
        return;
    }
    XString_init(&tmp);
    XString_assign_utf8(&tmp, utf8);
    XWizardPage_setPixmap(self, which, &tmp);
    XString_deinit_base(&tmp);
}

const XString* XWizardPage_pixmap(const XWizardPage* self, int which)
{
    (void)which;
    return self ? self->m_pixmap : NULL;
}

void XWizardPage_setComplete(XWizardPage* self, bool complete)
{
    if (!self) return;
    if (self->m_complete == complete) return;
    self->m_complete = complete;
    /* 对标 Qt：complete 状态变化发射 completeChanged()，向导据此更新
       Next/Finish 使能（此前仅存值，向导无从感知）。 */
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XWizardPage_completeChanged_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

bool XWizardPage_isComplete(const XWizardPage* self)
{
    return self ? self->m_complete : false;
}

/* ==================== XWizard 内部工具 ==================== */

static void xwiz_emitInt(XWizard* self, size_t signal, int val)
{
    XVarList* args = XVarList_Create(XVar(int, val));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      发射无参信号（helpRequested/completeChanged 等）。
 * @param      self 目标向导；NULL 或无已连接槽时不发射。
 * @param      signal 信号标识。
 * @return     无返回值。
 */
static void xwiz_emitVoid(XWizard* self, size_t signal)
{
    XVarList* arguments = XVarList_create(0);
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void xwiz_updateButtons(XWizard* self);

/** @brief 页 completeChanged 槽：当前页完备性变化即刷新按钮使能。 */
static void xwizard_pageCompleteChanged(XObject* receiver, XVarList* args)
{
    XWizard* self = (XWizard*)receiver;
    (void)args;
    if (self) xwiz_updateButtons(self);
}

static const char* xwiz_defaultButtonText(XWizardButton which)
{
    switch (which) {
    case XWizardButton_BackButton: return "< 上一页";
    case XWizardButton_NextButton: return "下一页 >";
    case XWizardButton_FinishButton: return "完成";
    case XWizardButton_CancelButton: return "取消";
    default: return "";
    }
}

/** @brief 更新导航按钮的可见性/启用/文本。 */
static void xwiz_updateButtons(XWizard* self)
{
    bool isFirst = (self->m_currentIndex == 0);
    bool isLast = (self->m_currentIndex == self->m_pageCount - 1);
    XWizardPage* current;
    bool currentComplete;
    if (!self) return;
    current = (self->m_currentIndex >= 0 &&
               self->m_currentIndex < self->m_pageCount)
                  ? self->m_pages[self->m_currentIndex]
                  : NULL;
    /* 对标 Qt：当前页 isComplete=false 时 Next/Finish 禁用（响应页的
       completeChanged() 实时更新）。 */
    currentComplete = current ? XWizardPage_isComplete(current) : true;
#if XPUSHBUTTON_ON
    if (self->m_btnBack) {
        XWidget_setVisible((XWidget*)self->m_btnBack, !isFirst);
        const char* txt = XWizard_buttonText(self, XWizardButton_BackButton);
        if (!txt || !txt[0])
            txt = xwiz_defaultButtonText(XWizardButton_BackButton);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnBack, txt);
    }
    if (self->m_btnNext) {
        XWidget_setVisible((XWidget*)self->m_btnNext, !isLast);
        const char* txt = XWizard_buttonText(self, XWizardButton_NextButton);
        if (!txt || !txt[0])
            txt = xwiz_defaultButtonText(XWizardButton_NextButton);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnNext, txt);
        XWidget_setEnabled((XWidget*)self->m_btnNext,
                                   !isLast && currentComplete);
    }
    if (self->m_btnFinish) {
        XWidget_setVisible((XWidget*)self->m_btnFinish, isLast);
        XWidget_setEnabled((XWidget*)self->m_btnFinish,
                                   isLast && currentComplete);
    }
#endif
}

/**
 * @brief      内部导航方向（对标 QWizardPrivate::Direction 的 Backward/
 *             Forward 两态；Restart 在 XWizard_restart 内归一为 Forward）。
 */
typedef enum XWizardNavDirection
{
    XWizardNavDirection_Backward = 0, /**< 经 back() 返回上一页。 */
    XWizardNavDirection_Forward = 1   /**< 前进/跳转/重启进入目标页。 */
} XWizardNavDirection;

static int xwiz_bannerHeight(const XWizard* self);
static void xwiz_layoutCurrentPage(XWizard* self);

/**
 * @brief      切换到指定页面（隐藏旧页、触发虚槽、显示新页、更新按钮、
 *             发射 currentIdChanged）。
 * @details    对标 QWizardPrivate::switchToPage 的虚槽时序：Backward 且
 *             未开启 IndependentPages 时对旧页调用 cleanupPage 并复位其
 *             m_initialized；进入新页时若 m_initialized 为 false 则置位
 *             并调用 initializePage（即每页首次显示时触发一次，开启
 *             IndependentPages 时同样只触发一次）。
 * @param      self 目标向导指针；NULL 或越界索引时忽略。
 * @param      index 目标页索引（0 起）。
 * @param      direction 导航方向。
 * @return     无返回值。
 */
static void xwiz_switchTo(XWizard* self, int index,
                          XWizardNavDirection direction)
{
    XWizardPage* oldPage;
    XWizardPage* newPage;
    bool independent;
    if (!self || index < 0 || index >= self->m_pageCount) return;
    if (index == self->m_currentIndex) return;
    independent = (self->m_options & (int)XWizardOption_IndependentPages) != 0;
    oldPage = (self->m_currentIndex >= 0 &&
               self->m_currentIndex < self->m_pageCount)
                  ? self->m_pages[self->m_currentIndex]
                  : NULL;
    newPage = self->m_pages[index];
    if (oldPage) {
        XWidget_setVisible((XWidget*)oldPage, false);
        if (direction == XWizardNavDirection_Backward && !independent) {
            XWizardPage_cleanupPage(oldPage);
            oldPage->m_initialized = false;
        }
    }
    if (newPage) {
        if (!newPage->m_initialized) {
            newPage->m_initialized = true;
            XWizardPage_initializePage(newPage);
        }
        XWidget_setVisible((XWidget*)newPage, true);
    }
    self->m_currentIndex = index;
    xwiz_layoutCurrentPage(self);
    self->m_visited[index] = true;
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_currentIdChanged_signal, index);
    XWidget_update((XWidget*)self);
}

#if XPUSHBUTTON_ON
static void xwiz_btnBackSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XWizard_back((XWizard*)receiver);
}
static void xwiz_btnNextSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XWizard_next((XWizard*)receiver);
}
static void xwiz_btnFinishSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XDialog_accept((XDialog*)receiver);
}
static void xwiz_btnCancelSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    XDialog_reject((XDialog*)receiver);
}
static void xwiz_btnHelpSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    xwiz_emitVoid((XWizard*)receiver, (size_t)XWizard_helpRequested_signal);
}
#endif

/* ==================== XWizard 生命周期与虚表 ==================== */

static void VX_wizard_paintEvent(XWidget* self, XEvent* event);

/** @brief 当前横幅高度：当前页有副标题时两行（56），否则单行（32）。
 *  @note  对标 QWizard ModernStyle 横幅（标题 + 副标题随内容伸缩）。 */
static int xwiz_bannerHeight(const XWizard* self)
{
    XWizardPage* page;
    if (!self || self->m_currentIndex < 0 ||
        self->m_currentIndex >= self->m_pageCount)
        return 32;
    page = self->m_pages[self->m_currentIndex];
    if (page) {
        const char* sub = XWizardPage_subTitle(page);
        if (sub && sub[0]) return 56;
    }
    return 32;
}

/** @brief 当前页几何 = 横幅之下、底部按钮带（40）之上。
 *  @details 此前页面从 y=0 起盖住横幅区，页面内容（子控件）与横幅
 *           标题叠印（demo "Step 1" 标签压在横幅标题上呈 "Stepp1"）。 */
static void xwiz_layoutCurrentPage(XWizard* self)
{
    XRect r;
    int bannerH;
    int pageH;
    if (!self || self->m_currentIndex < 0 ||
        self->m_currentIndex >= self->m_pageCount)
        return;
    if (!self->m_pages[self->m_currentIndex]) return;
    bannerH = xwiz_bannerHeight(self);
    pageH = XWidget_height((XWidget*)self) - bannerH - 40;
    if (pageH < 1) pageH = 1;
    XRect_init(&r, 0, bannerH, XWidget_width((XWidget*)self), pageH);
    XWidget_setGeometryRect((XWidget*)self->m_pages[self->m_currentIndex],
                            &r);
}

static void VX_wizard_paintEvent(XWidget* self, XEvent* event)
{
    XWizard* wiz = (XWizard*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t highlight;
    uint32_t windowText;
    char buf[256];
    XWizardPage* page;
    if (!wiz || !event) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c;
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
#endif
    r.x = 0; r.y = 0;
    r.width = XWidget_width(self);
    r.height = XWidget_height(self);
    /* 绘制范围 = 事件脏区（非 PAINT 入口退化为整控件）：背景、横幅、
       分隔线全部限幅在脏区内，避免小区域刷新触发整页重绘。 */
    if (event && XEvent_type(event) == XEVENT_TYPE_PAINT) {
        XRect clip = XPaintEvent_rect((const XPaintEvent*)event);
        if (clip.x < 0) { clip.width += clip.x; clip.x = 0; }
        if (clip.y < 0) { clip.height += clip.y; clip.y = 0; }
        if (clip.x + clip.width > r.width) clip.width = r.width - clip.x;
        if (clip.y + clip.height > r.height)
            clip.height = r.height - clip.y;
        if (clip.width > 0 && clip.height > 0)
            XPainter_setClipRect(&painter, &clip,
                                 XPainterClipOperation_ReplaceClip);
    }
    /* 白色背景。 */
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    /* 顶部标题栏（有副标题时两行，对标 QWizard 横幅结构）。 */
    page = XWizard_currentPage(wiz);
    if (page) {
        int bannerH = xwiz_bannerHeight(wiz);
        XRect head = { 0, 0, r.width, bannerH };
        const char* sub;
        XPainter_fillRect(&painter, &head, highlight);
        XSnprintf(buf, sizeof(buf), "%s", XWizardPage_title(page));
        XPainter_drawText(&painter, 8, bannerH - (bannerH >= 56 ? 34 : 12),
                          buf, 0xFFFFFFFFu);
        sub = XWizardPage_subTitle(page);
        if (sub && sub[0])
            XPainter_drawText(&painter, 8, bannerH - 12, sub, 0xFFD8E8F8u);
    }
    /* 底部分隔线。 */
    {
        XRect sep = { 0, r.height - 40, r.width, 1 };
        XPainter_fillRect(&painter, &sep, highlight);
    }
    XPainter_deinit(&painter);
}

static void VX_wizard_deinit(XWizard* self)
{
    int bi;
    if (!self) return;
    /* 修正：原先字段表/横幅图清理块误嵌在按钮文本 if 体内（且在 for
       循环内），全部按钮文本为 NULL 时会漏释放；现提到循环外统一清理。 */
    for (bi = 0; bi < XWizardButton_NStandardButtons; ++bi) {
        if (self->m_buttonTexts[bi]) {
            XString_delete_base(self->m_buttonTexts[bi]);
            self->m_buttonTexts[bi] = NULL;
        }
    }
    {
        int fi;
        for (fi = 0; fi < self->m_fieldCount; ++fi) {
            if (self->m_fields[fi].name)
                XString_delete_base(self->m_fields[fi].name);
            if (self->m_fields[fi].value)
                XString_delete_base(self->m_fields[fi].value);
            self->m_fields[fi].name = NULL;
            self->m_fields[fi].value = NULL;
        }
        self->m_fieldCount = 0;
    }
    if (self->m_pixmap) {
        XString_delete_base(self->m_pixmap);
        self->m_pixmap = NULL;
    }
    {
        /* 默认属性登记表：释放属性名键（value 为不透明借用，不释放）。 */
        int di;
        for (di = 0; di < self->m_defaultPropCount; ++di) {
            if (self->m_defaultProps[di].name)
                XString_delete_base(self->m_defaultProps[di].name);
            self->m_defaultProps[di].name = NULL;
            self->m_defaultProps[di].value = NULL;
        }
        self->m_defaultPropCount = 0;
    }
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

/* ==================== 布局（按钮行随尺寸重排） ==================== */

/** @brief 底部按钮行布局（从当前控件尺寸推导；init 与 resizeEvent 共用）。
 * @param self 目标向导。
 * @note 布局规则与创建序一致：Help 最左（x=8）、取消最右、完成/
 *       下一页/上一页依次左移（bw=80、gap=6、边距 8）；按钮未创建
 *       （选项裁剪）跳过。 */
static void xwiz_layoutButtons(XWizard* self)
{
    int bw = 80;
    int bh = 28;
    int gap = 6;
    int w;
    int h;
    int by;
    if (!self) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    by = h - bh - 8;
    if (self->m_btnHelp)
        XWidget_setGeometry((XWidget*)self->m_btnHelp, 8, by, bw, bh);
    if (self->m_btnCancel)
        XWidget_setGeometry((XWidget*)self->m_btnCancel,
                            w - bw - 8, by, bw, bh);
    if (self->m_btnFinish)
        XWidget_setGeometry((XWidget*)self->m_btnFinish,
                            w - bw * 2 - gap - 8, by, bw, bh);
    if (self->m_btnNext)
        XWidget_setGeometry((XWidget*)self->m_btnNext,
                            w - bw * 3 - gap * 2 - 8, by, bw, bh);
    if (self->m_btnBack)
        XWidget_setGeometry((XWidget*)self->m_btnBack,
                            w - bw * 4 - gap * 3 - 8, by, bw, bh);
}

/** @brief 尺寸变化：重排底部按钮行与当前页几何（否则按钮仍停留在
 *         创建时坐标，控件缩小后被裁剪不可见——demo 440x220 实测）。 */
static void VX_wizard_resizeEvent(XWidget* self, XEvent* event)
{
    XWizard* wiz = (XWizard*)self;
    if (!wiz || !event) return;
    xwiz_layoutButtons(wiz);
    xwiz_layoutCurrentPage(wiz);
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

XVtable* XWizard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWizard)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_wizard_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_wizard_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_wizard_resizeEvent);
    return XVTABLE_DEFAULT;
}

void XWizard_init(XWizard* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XWizard);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    self->m_fieldCount = 0;
    self->m_pixmap = XString_create();
    self->m_buttonLayout = 0;
    self->m_titleFormat = 0;
    self->m_subTitleFormat = 0;
    {
        int bi;
        for (bi = 0; bi < XWizardButton_NStandardButtons; ++bi)
            self->m_buttonTexts[bi] = XString_create();
    }
    Set_Class_IsHeap(self, false);
    self->m_pageCount = 0;
    self->m_currentIndex = 0;
    self->m_startIndex = 0;
    self->m_style = (int)XWizardStyle_ClassicStyle;
    self->m_options = 0;
    XWidget_resize(self, 480, 320);
#if XPUSHBUTTON_ON
    {
        /* 按钮创建后统一布局（xwiz_layoutButtons 按当前尺寸推导，
         * resizeEvent 复用同一布局；此处不再内联坐标）。 */
        /* 帮助按钮在最左（HaveHelpButton 选项时创建，对标 QWizard
           布局：Help 在导航按钮区之外靠左）。 */
        if (self->m_options & (int)XWizardOption_HaveHelpButton) {
            self->m_btnHelp = XPushButton_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
            {
                const char* ht = XWizard_buttonText(self,
                                                   XWizardButton_HelpButton);
                XAbstractButton_setText_2((XAbstractButton*)self->m_btnHelp,
                    (ht && ht[0]) ? ht : "帮助");
            }
            XWidget_show((XWidget*)self->m_btnHelp);
            XObject_connect_1((XObject*)self->m_btnHelp,
                (size_t)XAbstractButton_clicked_signal(
                    (XAbstractButton*)self->m_btnHelp, false),
                (XObject*)self, xwiz_btnHelpSlot, XConnectionType_Direct);
        }
        /* 取消在最右。 */
        self->m_btnCancel = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnCancel,
            xwiz_defaultButtonText(XWizardButton_CancelButton));
        XWidget_show((XWidget*)self->m_btnCancel);
        XObject_connect_1((XObject*)self->m_btnCancel,
            (size_t)XAbstractButton_clicked_signal(
                (XAbstractButton*)self->m_btnCancel, false),
            (XObject*)self, xwiz_btnCancelSlot, XConnectionType_Direct);
        /* 完成。 */
        self->m_btnFinish = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnFinish,
            xwiz_defaultButtonText(XWizardButton_FinishButton));
        XWidget_show((XWidget*)self->m_btnFinish);
        XObject_connect_1((XObject*)self->m_btnFinish,
            (size_t)XAbstractButton_clicked_signal(
                (XAbstractButton*)self->m_btnFinish, false),
            (XObject*)self, xwiz_btnFinishSlot, XConnectionType_Direct);
        /* 下一页。 */
        self->m_btnNext = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnNext,
            xwiz_defaultButtonText(XWizardButton_NextButton));
        XWidget_show((XWidget*)self->m_btnNext);
        XObject_connect_1((XObject*)self->m_btnNext,
            (size_t)XAbstractButton_clicked_signal(
                (XAbstractButton*)self->m_btnNext, false),
            (XObject*)self, xwiz_btnNextSlot, XConnectionType_Direct);
        /* 上一页。 */
        self->m_btnBack = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnBack,
            xwiz_defaultButtonText(XWizardButton_BackButton));
        XWidget_show((XWidget*)self->m_btnBack);
        XObject_connect_1((XObject*)self->m_btnBack,
            (size_t)XAbstractButton_clicked_signal(
                (XAbstractButton*)self->m_btnBack, false),
            (XObject*)self, xwiz_btnBackSlot, XConnectionType_Direct);
        /* 统一布局（按当前控件尺寸推导按钮行几何）。 */
        xwiz_layoutButtons(self);
    }
#endif
}

XWizard* XWizard_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XWizard* self = (XWizard*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XWizard_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

int XWizard_addPage(XWizard* self, XWizardPage* page)
{
    int idx;
    if (!self || !page) return -1;
    if (self->m_pageCount >= XWIZARD_MAX_PAGES) return -1;
    idx = self->m_pageCount;
    self->m_pages[idx] = page;
    self->m_visited[idx] = false;
    self->m_pageCount++;
    page->m_wizard = self;
    XWidget_setParent((XWidget*)page, (XWidget*)self, 0);
    /* 页完备性变化 → 按钮使能实时刷新（对标 Qt 内部 completeChanged
       到 QWizardPrivate::updateButtonLayout 的联动）。 */
    XObject_connect_1((XObject*)page,
                      (size_t)XWizardPage_completeChanged_signal(page),
                      (XObject*)self, xwizard_pageCompleteChanged,
                      XConnectionType_Direct);
    if (idx == 0) {
        XRect r;
        XWidget_setVisible((XWidget*)page, true);
        self->m_visited[0] = true;
        /* 首页挂载即铺内容区（与 xwiz_switchTo 同口径：高 -40 留按钮带）；
           否则首次导航前页面保持 0 尺寸不可见。 */
        XRect_init(&r, 0, 0,
                   XWidget_width((XWidget*)self),
                   XWidget_height((XWidget*)self) - 40);
        XWidget_setGeometryRect((XWidget*)page, &r);
        /* 对标 QWizard::showEvent 里的 restart()：首页首次显示即触发
           initializePage 虚槽（默认空操作，子类覆盖后生效）。 */
        if (!page->m_initialized) {
            page->m_initialized = true;
            XWizardPage_initializePage(page);
        }
    } else {
        XWidget_setVisible((XWidget*)page, false);
    }
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_pageAdded_signal, idx);
    return idx;
}

void XWizard_setPage(XWizard* self, int index, XWizardPage* page)
{
    XWizardPage* old;
    if (!self || !page || index < 0 || index >= XWIZARD_MAX_PAGES) return;
    old = self->m_pages[index];
    if (old && old != page) {
        /* 被替换页面解除向导归属并复位初始化标记。 */
        old->m_wizard = NULL;
        old->m_initialized = false;
    }
    self->m_pages[index] = page;
    if (index >= self->m_pageCount) self->m_pageCount = index + 1;
    page->m_wizard = self;
    XWidget_setParent((XWidget*)page, (XWidget*)self, 0);
}

void XWizard_removePage(XWizard* self, int index)
{
    int i;
    XWizardPage* removed;
    if (!self || index < 0 || index >= self->m_pageCount) return;
    removed = self->m_pages[index];
    for (i = index; i < self->m_pageCount - 1; ++i) {
        self->m_pages[i] = self->m_pages[i + 1];
        self->m_visited[i] = self->m_visited[i + 1];
    }
    self->m_pageCount--;
    self->m_pages[self->m_pageCount] = NULL;
    if (removed) {
        /* 对标 Qt：移除的页面解除向导归属并复位 initialized。 */
        removed->m_wizard = NULL;
        removed->m_initialized = false;
    }
    if (self->m_currentIndex >= self->m_pageCount)
        self->m_currentIndex = self->m_pageCount - 1;
    xwiz_emitInt(self, (size_t)XWizard_pageRemoved_signal, index);
}

XWizardPage* XWizard_page(XWizard* self, int index)
{
    if (!self || index < 0 || index >= self->m_pageCount) return NULL;
    return self->m_pages[index];
}

int XWizard_pageCount(const XWizard* self)
{
    return self ? self->m_pageCount : 0;
}

XWizardPage* XWizard_currentPage(const XWizard* self)
{
    if (!self || self->m_currentIndex < 0 ||
        self->m_currentIndex >= self->m_pageCount) return NULL;
    return self->m_pages[self->m_currentIndex];
}

int XWizard_currentIndex(const XWizard* self)
{
    return self ? self->m_currentIndex : -1;
}

void XWizard_next(XWizard* self)
{
    XWizardPage* page;
    int target;
    if (!self || self->m_currentIndex < 0) return;
    /* 对标 QWizard::next：先 validateCurrentPage()（分派 validatePage
       虚槽），再取 nextId()（分派 nextId 虚槽）作为目标页。 */
    if (!XWizard_validateCurrentPage(self)) return;
    page = XWizard_currentPage(self);
    target = page ? XWizardPage_nextId(page) : -1;
    if (target < 0 || target >= self->m_pageCount) return;
    if (target == self->m_currentIndex) return;
    xwiz_switchTo(self, target, XWizardNavDirection_Forward);
}

void XWizard_back(XWizard* self)
{
    if (!self || self->m_currentIndex <= 0) return;
    xwiz_switchTo(self, self->m_currentIndex - 1,
                  XWizardNavDirection_Backward);
}

void XWizard_setCurrentIndex(XWizard* self, int index)
{
    if (!self) return;
    xwiz_switchTo(self, index, XWizardNavDirection_Forward);
}

void XWizard_restart(XWizard* self)
{
    int i;
    int start;
    if (!self || self->m_pageCount <= 0) return;
    /* 对标 QWizard::restart = reset() + switchToPage(startId, Forward)：
       复位全部页面的 initialized 标记与访问标记，再前进进入起始页；
       m_currentIndex 先置 -1，保证起始页即使等于当前页也会重新触发
       initializePage。 */
    start = (self->m_startIndex >= 0 && self->m_startIndex < self->m_pageCount)
                ? self->m_startIndex : 0;
    for (i = 0; i < self->m_pageCount; ++i) {
        self->m_visited[i] = false;
        if (self->m_pages[i]) self->m_pages[i]->m_initialized = false;
    }
    self->m_currentIndex = -1;
    xwiz_switchTo(self, start, XWizardNavDirection_Forward);
}

void XWizard_setStartIndex(XWizard* self, int index)
{
    if (!self || index < 0 || index >= self->m_pageCount) return;
    self->m_startIndex = index;
}

int XWizard_startIndex(const XWizard* self)
{
    return self ? self->m_startIndex : 0;
}

bool XWizard_hasVisitedPage(const XWizard* self, int index)
{
    if (!self || index < 0 || index >= self->m_pageCount) return false;
    return self->m_visited[index];
}

/**
 * @brief      汇总已访问页 id 列表（对标 QWizard::visitedIds）。
 * @details    本项目页 id 即页索引（0 起），输出按索引升序；访问标记
 *             由 xwiz_switchTo/addPage/restart 维护。outIds 为 NULL 或
 *             maxCount <= 0 时为"计数查询"模式，只统计不写入；否则
 *             最多写入 maxCount 个 id（超出部分截断）。
 * @param      self 目标向导指针；NULL 时返回 0。
 * @param      outIds 调用方提供的 int 缓冲；可为 NULL（仅计数）。
 * @param      maxCount 缓冲容量（个数）；outIds 为 NULL 时不生效。
 * @return     计数查询模式返回已访问页总数；写入模式返回实际写入个数。
 */
int XWizard_visitedIds(const XWizard* self, int* outIds, int maxCount)
{
    int i;
    int count = 0;
    if (!self) return 0;
    if (!outIds || maxCount <= 0) {
        for (i = 0; i < self->m_pageCount; ++i)
            if (self->m_visited[i]) count++;
        return count;
    }
    for (i = 0; i < self->m_pageCount && count < maxCount; ++i) {
        if (self->m_visited[i]) outIds[count++] = i;
    }
    return count;
}

/**
 * @brief      列出全部页 id（对标 QWizard::pageIds）。
 * @details    本项目页 id 即页索引（0 起），输出按索引升序，覆盖全部
 *             已登记页面（不限已访问）。outIds 为 NULL 或 maxCount <= 0
 *             时为"计数查询"模式，仅返回页面总数；否则最多写入
 *             maxCount 个 id（超出截断）并返回实际写入个数。
 * @param      self 目标向导指针；NULL 时返回 0。
 * @param      outIds 调用方提供的 int 缓冲；可为 NULL（仅计数）。
 * @param      maxCount 缓冲容量（个数）；outIds 为 NULL 时不生效。
 * @return     计数查询模式返回页面总数；写入模式返回实际写入个数。
 */
int XWizard_pageIds(const XWizard* self, int* outIds, int maxCount)
{
    int i;
    int count;
    if (!self) return 0;
    if (!outIds || maxCount <= 0)
        return self->m_pageCount;
    count = (self->m_pageCount < maxCount) ? self->m_pageCount : maxCount;
    for (i = 0; i < count; ++i)
        outIds[i] = i;
    return count;
}

void XWizard_setWizardStyle(XWizard* self, XWizardStyle style)
{
    if (!self) return;
    self->m_style = (int)style;
}

XWizardStyle XWizard_wizardStyle(const XWizard* self)
{
    return self ? (XWizardStyle)self->m_style : XWizardStyle_ClassicStyle;
}

void XWizard_setOption(XWizard* self, XWizardOption option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= (int)option;
    else self->m_options &= ~(int)option;
}

bool XWizard_testOption(const XWizard* self, XWizardOption option)
{
    return self ? (self->m_options & (int)option) != 0 : false;
}

void XWizard_setOptions(XWizard* self, int options)
{
    if (!self) return;
    self->m_options = options;
}

int XWizard_options(const XWizard* self)
{
    return self ? self->m_options : 0;
}

void XWizard_setButtonText(XWizard* self, XWizardButton which, const char* utf8)
{
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons) return;
    if (!self->m_buttonTexts[which])
        self->m_buttonTexts[which] = XString_create();
    if (self->m_buttonTexts[which])
        XString_assign_utf8(self->m_buttonTexts[which],
                            utf8 ? utf8 : "");
    xwiz_updateButtons(self);
}

const char* XWizard_buttonText(const XWizard* self, XWizardButton which)
{
    const char* text;
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons) return "";
    if (!self->m_buttonTexts[which]) return "";
    text = XString_toUtf8(self->m_buttonTexts[which]);
    return text ? text : "";
}

struct XAbstractButton* XWizard_button(const XWizard* self, int which)
{
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons)
        return NULL;
    /* setButton 登记的自定义按钮优先（对标 Qt：setButton 覆盖默认按钮）。 */
    if (self->m_customButtons[which])
        return self->m_customButtons[which];
#if XPUSHBUTTON_ON
    switch (which) {
    case XWizardButton_BackButton:
        return (XAbstractButton*)self->m_btnBack;
    case XWizardButton_NextButton:
        return (XAbstractButton*)self->m_btnNext;
    case XWizardButton_CommitButton:
        return NULL; /* Commit 专用按钮本版未创建。 */
    case XWizardButton_FinishButton:
        return (XAbstractButton*)self->m_btnFinish;
    case XWizardButton_CancelButton:
        return (XAbstractButton*)self->m_btnCancel;
    case XWizardButton_HelpButton:
        return (XAbstractButton*)self->m_btnHelp; /* 未开 Help 选项为 NULL。 */
    default:
        break;
    }
#endif
    return NULL;
}

void XWizard_setButton(XWizard* self, int which, struct XAbstractButton* btn)
{
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons) return;
    /* 借用承载：不删除旧按钮、不改父控件；NULL 表示清除登记。 */
    self->m_customButtons[which] = btn;
}

void XWizard_setDefaultProperty(XWizard* self, const char* name, void* value)
{
    int i;
    if (!self || !name || name[0] == '\0') return;
    /* 同名重复登记覆盖旧 value（对标 Qt 的按类注册覆盖语义）。 */
    for (i = 0; i < self->m_defaultPropCount; ++i) {
        const char* key;
        if (!self->m_defaultProps[i].name) continue;
        key = XString_toUtf8(self->m_defaultProps[i].name);
        if (key && XStrcmp(key, name) == 0) {
            self->m_defaultProps[i].value = value;
            return;
        }
    }
    if (self->m_defaultPropCount >= XWIZARD_MAX_DEFAULT_PROPERTIES) return;
    self->m_defaultProps[self->m_defaultPropCount].name =
        XString_create_utf8(name);
    if (!self->m_defaultProps[self->m_defaultPropCount].name) return;
    self->m_defaultProps[self->m_defaultPropCount].value = value;
    self->m_defaultPropCount++;
}

/* ==================== 信号 ==================== */

void* XWizard_currentIdChanged_signal(XWizard* self, int index)
{ (void)self; (void)index; return (void*)(size_t)XWizard_currentIdChanged_signal; }
void* XWizard_pageAdded_signal(XWizard* self, int index)
{ (void)self; (void)index; return (void*)(size_t)XWizard_pageAdded_signal; }
void* XWizard_pageRemoved_signal(XWizard* self, int index)
{ (void)self; (void)index; return (void*)(size_t)XWizard_pageRemoved_signal; }

void* XWizard_helpRequested_signal(XWizard* self)
{
    if (!self)
        return (void*)(size_t)XWizard_helpRequested_signal;
    xwiz_emitVoid(self, (size_t)XWizard_helpRequested_signal);
    return (void*)(size_t)XWizard_helpRequested_signal;
}



































void* XWizardPage_completeChanged_signal(XWizardPage* self)
{
    (void)self;
    return (void*)(size_t)XWizardPage_completeChanged_signal;
}

void* XWizard_customButtonClicked_signal(XWizard* self, int which)
{
    (void)self; (void)which;
    return (void*)(size_t)XWizard_customButtonClicked_signal;
}

/* ==================== Task 2.6：字段/侧边/布局/格式/导航 ==================== */

void XWizard_setPixmap(XWizard* self, int which, const XString* path)
{
    (void)which;
    if (!self) return;
    if (!self->m_pixmap) self->m_pixmap = XString_create();
    if (self->m_pixmap) {
        if (path)
            XString_assign(self->m_pixmap, path);
        else
            XString_assign_utf8(self->m_pixmap, "");
    }
}
void XWizard_setPixmap_2(XWizard* self, int which, const char* path)
{
    XString* tmp = NULL;
    if (path) {
        tmp = XString_create_utf8(path);
        if (!tmp) return;
    }
    XWizard_setPixmap(self, which, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XWizard_pixmap(const XWizard* self, int which)
{
    (void)which;
    return self ? self->m_pixmap : NULL;
}

const XString* XWizard_field(const XWizard* self, const XString* name)
{
    int i;
    if (!self || !name) return NULL;
    for (i = 0; i < self->m_fieldCount; ++i) {
        if (self->m_fields[i].name &&
            XString_equals(self->m_fields[i].name, name,
                           XChar_CaseSensitive))
            return self->m_fields[i].value;
    }
    return NULL;
}
const char* XWizard_field_2(const XWizard* self, const char* name)
{
    XString* tmp = NULL;
    const XString* v;
    const char* out;
    if (!name) return NULL;
    tmp = XString_create_utf8(name);
    if (!tmp) return NULL;
    v = XWizard_field(self, tmp);
    out = v ? XString_toUtf8(v) : NULL;
    XString_delete_base(tmp);
    return out;
}

void XWizard_setField(XWizard* self, const XString* name,
                      const XString* value)
{
    int i;
    if (!self || !name) return;
    for (i = 0; i < self->m_fieldCount; ++i) {
        if (self->m_fields[i].name &&
            XString_equals(self->m_fields[i].name, name,
                           XChar_CaseSensitive)) {
            if (value)
                XString_assign(self->m_fields[i].value, value);
            else
                XString_assign_utf8(self->m_fields[i].value, "");
            return;
        }
    }
    if (self->m_fieldCount >= 32) return;
    self->m_fields[self->m_fieldCount].name =
        XString_create_copy(name);
    self->m_fields[self->m_fieldCount].value =
        value ? XString_create_copy(value) : XString_create();
    self->m_fieldCount++;
}
void XWizard_setField_2(XWizard* self, const char* name, const char* value)
{
    XString* tn = NULL;
    XString* tv = NULL;
    if (name) {
        tn = XString_create_utf8(name);
        if (!tn) return;
    }
    if (value) {
        tv = XString_create_utf8(value);
        if (!tv) { if (tn) XString_delete_base(tn); return; }
    }
    XWizard_setField(self, tn, tv);
    if (tv) XString_delete_base(tv);
    if (tn) XString_delete_base(tn);
}

void XWizard_setSideWidget(XWizard* self, XWidget* widget)
{ if (self) self->m_sideWidget = widget; }
XWidget* XWizard_sideWidget(const XWizard* self)
{ return self ? self->m_sideWidget : NULL; }

void XWizard_setButtonLayout(XWizard* self, int layout)
{ if (self) self->m_buttonLayout = layout; }
void XWizard_setTitleFormat(XWizard* self, int format)
{ if (self) self->m_titleFormat = format; }
int XWizard_titleFormat(const XWizard* self)
{ return self ? self->m_titleFormat : 0; }
void XWizard_setSubTitleFormat(XWizard* self, int format)
{ if (self) self->m_subTitleFormat = format; }
int XWizard_subTitleFormat(const XWizard* self)
{ return self ? self->m_subTitleFormat : 0; }

void XWizard_cleanupPage(XWizard* self)
{
    XWizardPage* page;
    if (!self) return;
    page = XWizard_currentPage(self);
    if (page) XWizardPage_cleanupPage(page);
}

void XWizard_initializePage(XWizard* self)
{
    XWizardPage* page;
    if (!self) return;
    page = XWizard_currentPage(self);
    if (page) XWizardPage_initializePage(page);
}

bool XWizard_validateCurrentPage(const XWizard* self)
{
    XWizardPage* page;
    if (!self) return true;
    page = XWizard_currentPage(self);
    if (!page) return true;
    /* 对标 QWizard::validateCurrentPage：当前页 isComplete=false 时
       直接判负（先于 validatePage 虚槽，与 Next/Finish 使能一致）。 */
    if (!XWizardPage_isComplete(page)) return false;
    /* 分派当前页 validatePage 虚槽。 */
    return XWizardPage_validatePage(page);
}

int XWizard_nextId(const XWizard* self)
{
    XWizardPage* page;
    if (!self) return -1;
    page = XWizard_currentPage(self);
    /* 对标 QWizard::nextId：分派当前页 nextId 虚槽（默认顺序 +1）。 */
    return page ? XWizardPage_nextId(page) : -1;
}

void XWizard_done(XWizard* self, int result)
{
    if (!self) return;
    XDialog_done((XDialog*)self, result);
}

#endif /* XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */