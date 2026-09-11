#include "XWizard.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPainter.h"
#include "XLabel.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <string.h>
#include <stdio.h>

#if XWIDGET_ON && XDIALOG_ON && XWIZARD_ON

/* ==================== XWizardPage ==================== */

XVtable* XWizardPage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWizardPage)
    XVTABLE_INHERIT_XCLASS(XWidget);
    return XVTABLE_DEFAULT;
}

void XWizardPage_init(XWizardPage* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XWizardPage);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_complete = true;
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

void XWizardPage_setTitle(XWizardPage* self, const char* utf8)
{
    if (!self) return;
    strncpy(self->m_title, utf8 ? utf8 : "", sizeof(self->m_title) - 1);
    self->m_title[sizeof(self->m_title) - 1] = '\0';
    XWidget_update((XWidget*)self);
}

const char* XWizardPage_title(const XWizardPage* self)
{
    return self ? self->m_title : "";
}

void XWizardPage_setSubTitle(XWizardPage* self, const char* utf8)
{
    if (!self) return;
    strncpy(self->m_subTitle, utf8 ? utf8 : "", sizeof(self->m_subTitle) - 1);
    self->m_subTitle[sizeof(self->m_subTitle) - 1] = '\0';
}

const char* XWizardPage_subTitle(const XWizardPage* self)
{
    return self ? self->m_subTitle : "";
}

void XWizardPage_setComplete(XWizardPage* self, bool complete)
{
    if (!self) return;
    self->m_complete = complete;
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
    if (!self) return;
#if XPUSHBUTTON_ON
    if (self->m_btnBack) {
        XWidget_setVisible((XWidget*)self->m_btnBack, !isFirst);
        const char* txt = self->m_buttonTexts[XWizardButton_BackButton][0]
            ? self->m_buttonTexts[XWizardButton_BackButton]
            : xwiz_defaultButtonText(XWizardButton_BackButton);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnBack, txt);
    }
    if (self->m_btnNext) {
        XWidget_setVisible((XWidget*)self->m_btnNext, !isLast);
        const char* txt = self->m_buttonTexts[XWizardButton_NextButton][0]
            ? self->m_buttonTexts[XWizardButton_NextButton]
            : xwiz_defaultButtonText(XWizardButton_NextButton);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnNext, txt);
    }
    if (self->m_btnFinish) {
        XWidget_setVisible((XWidget*)self->m_btnFinish, isLast);
    }
#endif
}

/** @brief 切换到指定页面（隐藏旧页、显示新页、更新按钮、发信号）。 */
static void xwiz_switchTo(XWizard* self, int index)
{
    XWizardPage* oldPage;
    XWizardPage* newPage;
    if (!self || index < 0 || index >= self->m_pageCount) return;
    if (index == self->m_currentIndex) return;
    oldPage = self->m_pages[self->m_currentIndex];
    newPage = self->m_pages[index];
    if (oldPage) XWidget_setVisible((XWidget*)oldPage, false);
    if (newPage) {
        XRect r;
        XWidget_setVisible((XWidget*)newPage, true);
        XRect_init(&r, 0, 0,
                   XWidget_width((XWidget*)self),
                   XWidget_height((XWidget*)self) - 40);
        XWidget_setGeometryRect((XWidget*)newPage, &r);
    }
    self->m_currentIndex = index;
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
#endif

/* ==================== XWizard 生命周期与虚表 ==================== */

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
    image = XWidget_paintDevice(self);
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
    /* 白色背景。 */
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    /* 顶部标题栏。 */
    page = XWizard_currentPage(wiz);
    if (page) {
        XRect head = { 0, 0, r.width, 32 };
        XPainter_fillRect(&painter, &head, highlight);
        snprintf(buf, sizeof(buf), "%s", page->m_title);
        XPainter_drawText(&painter, 8, 20, buf, 0xFFFFFFFFu);
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
    if (!self) return;
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XWizard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWizard)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_wizard_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_wizard_paintEvent);
    return XVTABLE_DEFAULT;
}

void XWizard_init(XWizard* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XWizard);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_pageCount = 0;
    self->m_currentIndex = 0;
    self->m_startIndex = 0;
    self->m_style = (int)XWizardStyle_ClassicStyle;
    self->m_options = 0;
    XWidget_resize(self, 480, 320);
#if XPUSHBUTTON_ON
    {
        int bw = 80, bh = 28, by, gap = 6;
        int h = XWidget_height(self);
        by = h - bh - 8;
        /* 取消在最右。 */
        self->m_btnCancel = XPushButton_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        XAbstractButton_setText_2((XAbstractButton*)self->m_btnCancel,
            xwiz_defaultButtonText(XWizardButton_CancelButton));
        XWidget_setGeometry((XWidget*)self->m_btnCancel,
                            480 - bw - 8, by, bw, bh);
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
        XWidget_setGeometry((XWidget*)self->m_btnFinish,
                            480 - bw * 2 - gap - 8, by, bw, bh);
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
        XWidget_setGeometry((XWidget*)self->m_btnNext,
                            480 - bw * 3 - gap * 2 - 8, by, bw, bh);
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
        XWidget_setGeometry((XWidget*)self->m_btnBack,
                            480 - bw * 4 - gap * 3 - 8, by, bw, bh);
        XWidget_show((XWidget*)self->m_btnBack);
        XObject_connect_1((XObject*)self->m_btnBack,
            (size_t)XAbstractButton_clicked_signal(
                (XAbstractButton*)self->m_btnBack, false),
            (XObject*)self, xwiz_btnBackSlot, XConnectionType_Direct);
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
    XWidget_setParent((XWidget*)page, (XWidget*)self, 0);
    if (idx == 0) {
        XWidget_setVisible((XWidget*)page, true);
        self->m_visited[0] = true;
    } else {
        XWidget_setVisible((XWidget*)page, false);
    }
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_pageAdded_signal, idx);
    return idx;
}

void XWizard_setPage(XWizard* self, int index, XWizardPage* page)
{
    if (!self || !page || index < 0 || index >= XWIZARD_MAX_PAGES) return;
    self->m_pages[index] = page;
    if (index >= self->m_pageCount) self->m_pageCount = index + 1;
    XWidget_setParent((XWidget*)page, (XWidget*)self, 0);
}

void XWizard_removePage(XWizard* self, int index)
{
    int i;
    if (!self || index < 0 || index >= self->m_pageCount) return;
    for (i = index; i < self->m_pageCount - 1; ++i) {
        self->m_pages[i] = self->m_pages[i + 1];
        self->m_visited[i] = self->m_visited[i + 1];
    }
    self->m_pageCount--;
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
    if (!self || self->m_currentIndex >= self->m_pageCount - 1) return;
    xwiz_switchTo(self, self->m_currentIndex + 1);
}

void XWizard_back(XWizard* self)
{
    if (!self || self->m_currentIndex <= 0) return;
    xwiz_switchTo(self, self->m_currentIndex - 1);
}

void XWizard_restart(XWizard* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_pageCount; ++i) self->m_visited[i] = false;
    self->m_currentIndex = self->m_startIndex;
    self->m_visited[self->m_startIndex] = true;
    xwiz_switchTo(self, self->m_startIndex);
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
    strncpy(self->m_buttonTexts[which], utf8 ? utf8 : "",
            sizeof(self->m_buttonTexts[which]) - 1);
    self->m_buttonTexts[which][sizeof(self->m_buttonTexts[which]) - 1] = '\0';
    xwiz_updateButtons(self);
}

const char* XWizard_buttonText(const XWizard* self, XWizardButton which)
{
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons) return "";
    return self->m_buttonTexts[which];
}

/* ==================== 信号 ==================== */

void* XWizard_currentIdChanged_signal(XWizard* self, int index)
{ (void)self; (void)index; return (void*)(size_t)XWizard_currentIdChanged_signal; }
void* XWizard_pageAdded_signal(XWizard* self, int index)
{ (void)self; (void)index; return (void*)(size_t)XWizard_pageAdded_signal; }
void* XWizard_pageRemoved_signal(XWizard* self, int index)
{ (void)self; (void)index; return (void*)(size_t)XWizard_pageRemoved_signal; }


void* XWizard_completeChanged_signal(XWizard* self)
{
    (void)self;
    return (void*)(size_t)XWizard_completeChanged_signal;
}
void* XWizard_customButtonClicked_signal(XWizard* self)
{
    (void)self;
    return (void*)(size_t)XWizard_customButtonClicked_signal;
}

void XWizard_setPixmap(XWizard* self, int which, const char* path) { (void)self; (void)which; (void)path; }
const char* XWizard_pixmap(const XWizard* self, int which) { (void)self; (void)which; return ""; }
void XWizard_setField_2(XWizard* self, const char* name, const char* value) { (void)self; (void)name; (void)value; }
const char* XWizard_field(const XWizard* self, const char* name) { (void)self; (void)name; return ""; }
void XWizard_setSideWidget(XWizard* self, XWidget* widget) { (void)self; (void)widget; }
XWidget* XWizard_sideWidget(const XWizard* self) { (void)self; return NULL; }
int XWizard_visitedIds_count(const XWizard* self)
{ int i; int c=0; if(!self) return 0; for(i=0;i<self->m_pageCount;++i) if(self->m_visited[i]) ++c; return c; }
bool XWizard_validateCurrentPage(XWizard* self)
{ XWizardPage* p; if(!self) return false; p=XWizard_currentPage(self); return p?XWizardPage_isComplete(p):true; }
void XWizard_setButtonLayout(XWizard* self, const int* layout, int count) { (void)self; (void)layout; (void)count; }
void XWizard_setButton_2(XWizard* self, XWizardButton which, XPushButton* button) { (void)self; (void)which; (void)button; }
void XWizard_setTitleFormat(XWizard* self, int format) { (void)self; (void)format; }
int XWizard_titleFormat(const XWizard* self) { (void)self; return 0; }
void XWizard_setSubTitleFormat(XWizard* self, int format) { (void)self; (void)format; }
int XWizard_subTitleFormat(const XWizard* self) { (void)self; return 0; }
#endif /* XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */