#if XGUI_ON

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
#if XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON && XDIALOG_ON
#include "XDialog.h" /* CSD 装饰条让位查询（同 XMessageBox 先例）。 */
#define XWIZ_CSD_QUERY_ON 1
#else
#define XWIZ_CSD_QUERY_ON 0
#endif

#if XWIDGET_ON && XDIALOG_ON && XWIZARD_ON

/* ==================== XWizardPage ==================== */

/* 前向声明：页标题/副标题运行期变更需联动向导横幅（定义在后）。 */
static int xwiz_bannerHeight(const XWizard* self);
static void xwiz_layoutCurrentPage(XWizard* self);
static void xwiz_layoutButtons(XWizard* self);
static const char* xwiz_wizardButtonText(const XWizard* self,
                                         XWizardButton which);
/* 向导级虚槽默认实现（定义在文件末，class_init 里登记）。 */
static void VXWizard_initializePage(XWizard* self, int id);
static void VXWizard_cleanupPage(XWizard* self, int id);

/* ==================== 字段表（对标 QWizardPrivate::fields） ==================== */

/** @brief 按字段名查字段项（未登记返回 NULL；Qt 同义的 fieldIndexMap）。 */
static XWizardFieldEntry* xwiz_fieldFind(XWizard* self, const char* name)
{
    int i;
    if (!self || !name) return NULL;
    for (i = 0; i < self->m_fieldCount; ++i) {
        const char* n = self->m_fields[i].name
                            ? XString_toUtf8(self->m_fields[i].name)
                            : NULL;
        if (n && XStrcmp(n, name) == 0)
            return &self->m_fields[i];
    }
    return NULL;
}

/** @brief 新建字段项（容量满返回 NULL）。调用方填 name/page 等。 */
static XWizardFieldEntry* xwiz_fieldCreate(XWizard* self)
{
    XWizardFieldEntry* f;
    if (!self || self->m_fieldCount >= XWIZARD_MAX_FIELDS) return NULL;
    f = &self->m_fields[self->m_fieldCount++];
    XMemset(f, 0, sizeof(*f));
    return f;
}

/** @brief 读字段当前值（UTF-8 借用）。
 *  @details 有 widget+property 时**实时读控件动态属性**（等价 Qt 的
 *           object->property(property)，qwizard.cpp:3609），否则读字段表
 *           值。数值型属性渲染成十进制文本（scratch 承接，借用返回）。
 *  @param f 字段项；NULL 返回空串。
 *  @param scratch 调用方提供的临时缓冲（可为 NULL，数值型时返回空串）。
 *  @return 借用 UTF-8 串（属性字符串直接借用，随属性失效）。
 */
static const char* xwiz_fieldValue(XWizardFieldEntry* f, XString* scratch)
{
    XVariant* var;
    const char* out;
    if (!f) return "";
    if (f->widget && f->property) {
        var = XObject_property_const((const XObject*)f->widget, f->property);
        if (var) {
            const XString* s = XString_fromVariant_const(var);
            if (s) {
                out = XString_toUtf8(s);
                return out ? out : "";
            }
            if (scratch) {
                char buf[64];
                XSnprintf(buf, sizeof(buf), "%lld",
                          (long long)XVariant_toInt64(var));
                if (XString_assign_utf8(scratch, buf)) {
                    out = XString_toUtf8(scratch);
                    return out ? out : "";
                }
            }
            return "";
        }
    }
    out = (f->value && XString_toUtf8(f->value))
              ? XString_toUtf8(f->value)
              : "";
    return out ? out : "";
}

/** @brief 写字段值（字段表 + 控件动态属性双写，Qt setField 同义）。
 *  @details 控件属性以 XString 型 XVariant 承载；`XObject_setProperty`
 *           按 2026-11-11 口径**总是接管 value**（返回值只表达是否命中
 *           静态属性），调用方不再释放。 */
static void xwiz_fieldWrite(XWizardFieldEntry* f, const char* utf8)
{
    const char* value = utf8 ? utf8 : "";
    if (!f) return;
    if (!f->value) f->value = XString_create();
    if (f->value) XString_assign_utf8(f->value, value);
    if (f->widget && f->property) {
        XVariant* v = XString_toVariant_utf8(value);
        if (v)
            (void)XObject_setProperty((XObject*)f->widget, f->property, v);
    }
}

/** @brief 本页必填字段是否全部离开初值（对标 QWizardPage::isComplete 的
 *         必填分支，qwizard.cpp:3598-3629）。
 *  @return 有任一必填字段仍等于初值返回 false；否则 true。 */
static bool xwiz_pageFieldsComplete(XWizard* wiz, XWizardPage* page)
{
    int i;
    bool complete = true;
    if (!wiz) return true;
    for (i = 0; i < wiz->m_fieldCount; ++i) {
        XWizardFieldEntry* f = &wiz->m_fields[i];
        XString scratch;
        const char* cur;
        const char* init;
        bool same;
        if (f->page != page || !f->mandatory) continue;
        XString_init(&scratch);
        cur = xwiz_fieldValue(f, &scratch);
        init = (f->initialValue && XString_toUtf8(f->initialValue))
                   ? XString_toUtf8(f->initialValue)
                   : "";
        same = (XStrcmp(cur, init) == 0);
        XClassDeinit(&scratch);
        if (same) { complete = false; break; }
    }
    return complete;
}

/** @brief 把本页全部字段恢复为登记初值（对标 QWizardPage::cleanupPage 的
 *         默认实现，qwizard.cpp:3550-3569）。 */
static void xwiz_pageRestoreFields(XWizardPage* page)
{
    XWizard* wiz = page ? page->m_wizard : NULL;
    int i;
    if (!wiz) return;
    for (i = 0; i < wiz->m_fieldCount; ++i) {
        XWizardFieldEntry* f = &wiz->m_fields[i];
        if (f->page != page) continue;
        xwiz_fieldWrite(f, (f->initialValue && XString_toUtf8(f->initialValue))
                               ? XString_toUtf8(f->initialValue)
                               : "");
    }
}

bool XWizardPage_isComplete(const XWizardPage* self);

/** @brief 字段变化槽：重估完备态并在**实变**时发射 completeChanged
 *         （对标 QWizardPagePrivate::_q_maybeEmitCompleteChanged）。
 *  @details 必填字段的 changedSignal 连到本槽；缓存最近一次评估值，
 *           避免每拍信号都触发向导按钮重排（Qt 同款去抖）。 */
static void xwizpage_fieldChangedSlot(XObject* receiver, XVarList* args)
{
    XWizardPage* page = (XWizardPage*)receiver;
    bool now;
    (void)args;
    if (!page) return;
    now = XWizardPage_isComplete(page);
    if (page->m_lastCompleteValid && page->m_lastComplete == now)
        return;
    page->m_lastComplete = now;
    page->m_lastCompleteValid = true;
    if (((XObject*)page)->m_signalSlot) {
        XObject_emitSignal((XObject*)page,
                           (size_t)XWizardPage_completeChanged_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

static void VXWizardPage_deinit(XWizardPage* self)
{
    int i;
    if (!self) return;
    if (self->m_title) {
        XClassDelete(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_subTitle) {
        XClassDelete(self->m_subTitle);
        self->m_subTitle = NULL;
    }
    for (i = 0; i < XWizardPixmap_NPixmaps; ++i) {
        if (self->m_pixmaps[i]) {
            XClassDelete(self->m_pixmaps[i]);
            self->m_pixmaps[i] = NULL;
        }
    }
    for (i = 0; i < XWizardButton_NStandardButtons; ++i) {
        if (self->m_buttonTexts[i]) {
            XClassDelete(self->m_buttonTexts[i]);
            self->m_buttonTexts[i] = NULL;
        }
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/* -------------------- 五虚槽默认实现（对标 Qt 6.8.3） -------------------- */

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
 * @details    Qt 6.8 默认实现把本页登记的全部字段恢复为初值
 *             （qwizard.cpp:3550-3569：`field.object->setProperty(
 *             field.property, field.initialValue)`）。本库字段带
 *             初值快照，同口径恢复（账本 290）。
 * @param      self 目标页面指针；可为 NULL。
 * @return     无返回值。
 */
static void VXWizardPage_cleanupPage(XWizardPage* self)
{
    if (!self) return;
    xwiz_pageRestoreFields(self);
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
 * @details    仿 Qt（qwizard.cpp:3690-3712）：无所属向导返回 -1；否则
 *             在页表中定位本页，返回**其后的下一页 id**（Qt 按 pageMap
 *             的键序取下一项；本库按页表顺序，id 单调分配故同序），
 *             末页返回 -1。
 * @param      self 目标页面指针；可为 NULL。
 * @return     下一页 id；无后续页返回 -1。
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
            return (i + 1 < wiz->m_pageCount) ? wiz->m_pageIds[i + 1] : -1;
    }
    return -1;
}

/**
 * @brief      isComplete 默认实现（对标 QWizardPage::isComplete）。
 * @details    Qt 6.8 默认实现（qwizard.cpp:3598-3629）：
 *             ① 未加入向导 → true；
 *             ② 遍历本页登记的**必填**字段，若某字段值仍等于初值 →
 *                false；否则 true。
 *             本库在 `setComplete` 完备位之外叠加必填字段判定（完备位是
 *             本库扩展，语义为"页面自述的额外阻塞条件"）。
 * @param      self 目标页面指针；可为 NULL。
 * @return     完备返回 true；self 为 NULL 返回 false。
 */
static bool VXWizardPage_isComplete(const XWizardPage* self)
{
    if (!self) return false;
    if (!self->m_complete) return false;
    return xwiz_pageFieldsComplete(self->m_wizard, (XWizardPage*)self);
}

XVtable* XWizardPage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWizardPage)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXWizardPage_deinit);
    /* 五个公开虚槽默认实现注册（子类可 XVTABLE_OVERLOAD_DEFAULT 覆盖）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_InitializePage,
                             VXWizardPage_initializePage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_CleanupPage,
                             VXWizardPage_cleanupPage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_ValidatePage,
                             VXWizardPage_validatePage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_NextId, VXWizardPage_nextId);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_IsComplete,
                             VXWizardPage_isComplete);
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

/* -------------------- 五虚槽分派函数（查虚表调用，子类覆盖即生效） -------------------- */

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

/**
 * @brief      页标题/副标题变更后联动向导横幅（仅本页为当前页时生效）。
 * @details    根因（二次复扫 R-26）：横幅（标题/副标题）画在向导自身
 *             的 paintEvent 里且按事件脏区裁剪，页控件失效产生的脏区
 *             位于横幅之下、不含横幅——运行期 setTitle/setSubTitle 仅
 *             update 页自身时横幅永远不上屏。此处按 xwiz_bannerHeight
 *             使向导横幅区失效；副标题有无还决定横幅高度（32/56），
 *             relayout 为真时先按新横幅高度重铺当前页几何（对标
 *             QWizard 横幅伸缩后 updateLayout 的重排语义）。
 */
static void xwiz_pageTextChanged(XWizardPage* self, bool relayout)
{
    XWizard* wiz;
    XRect r;
    if (!self || !self->m_wizard) return;
    wiz = self->m_wizard;
    if (wiz->m_currentIndex < 0 || wiz->m_currentIndex >= wiz->m_pageCount)
        return;
    if (wiz->m_pages[wiz->m_currentIndex] != self) return;
    if (relayout) xwiz_layoutCurrentPage(wiz);
    XRect_init(&r, 0, 0, XWidget_width((XWidget*)wiz),
               xwiz_bannerHeight(wiz));
    XWidget_updateRect((XWidget*)wiz, &r);
}

void XWizardPage_setTitle(XWizardPage* self, const char* utf8)
{
    if (!self) return;
    if (!self->m_title) self->m_title = XString_create();
    if (self->m_title)
        XString_assign_utf8(self->m_title, utf8 ? utf8 : "");
    XWidget_update((XWidget*)self);
    /* 横幅标题由向导 paintEvent 绘制，须补向导横幅区失效才会上屏。 */
    xwiz_pageTextChanged(self, false);
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
    /* 副标题有无决定横幅高度（32/56）且横幅由向导绘制：重铺当前页
       几何并补向导横幅区失效，变更才完整上屏。 */
    xwiz_pageTextChanged(self, true);
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
            XClassDelete(self->m_buttonTexts[which]);
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
    /* 对齐 Qt（qwizard.cpp:3742-3752）：页级自定义文本优先；未设置则
     * 回落 wizard()->buttonText(which)（向导级自定义/缺省文本）；无向导
     * 返回空串。此前未设页级文本恒返回空串，绕过了向导级设置。 */
    if (self->m_buttonTexts[which]) {
        text = XString_toUtf8(self->m_buttonTexts[which]);
        return text ? text : "";
    }
    if (self->m_wizard)
        return XWizard_buttonText(self->m_wizard, which);
    return "";
}

void XWizardPage_setCommitPage(XWizardPage* self, bool commitPage)
{ if (self) self->m_commitPage = commitPage; }
bool XWizardPage_isCommitPage(const XWizardPage* self)
{ return self ? self->m_commitPage : false; }

void XWizardPage_setFinalPage(XWizardPage* self, bool finalPage)
{
    if (!self) return;
    self->m_finalPage = finalPage;
    /* 对齐 Qt 的 explicitlyFinal（qwizard.cpp:3643-3650）：
     * `d->explicitlyFinal = finalPage` ⇒ false 时回落到 nextId()==-1 的
     * 推导，而不是"强制非末页"。此前 m_finalPageSet 在 false 时也置位，
     * 导致末页 setFinalPage(false) 后 isFinalPage()==0（Qt 为 1）。 */
    self->m_finalPageSet = finalPage;
}

bool XWizardPage_isFinalPage(const XWizardPage* self)
{
    const XWizard* wiz;
    if (!self) return false;
    /* 对齐 QWizardPage::isFinalPage（qwizard.cpp:3664-3680）：
     * ① 显式 setFinalPage(true) ⇒ true；
     * ② 当前页走 wizard->nextId()，非当前页走本页 nextId()（本库页 id
     *    稳定、nextId 按页表顺序取下一 id，二者同值）；
     * ③ **未加入向导**的独立页同样走 `nextId() == -1`（默认实现无向导
     *    返回 -1）⇒ true（本机 Qt 6.8.3 实测 standalone==1；r2 对齐，
     *    账本 239：旧实现直接 return false）。 */
    if (self->m_finalPageSet) return self->m_finalPage;
    wiz = self->m_wizard;
    if (!wiz) return XWizardPage_nextId(self) < 0;
    return XWizardPage_nextId(self) < 0;
}

void XWizardPage_setPixmap(XWizardPage* self, int which, const XString* path)
{
    XString** slot;
    if (!self || which < 0 || which >= XWizardPixmap_NPixmaps) return;
    slot = &self->m_pixmaps[which];
    if (!*slot) *slot = XString_create();
    if (*slot) {
        if (path)
            XString_assign(*slot, path);
        else
            XString_assign_utf8(*slot, "");
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
    XClassDeinit(&tmp);
}

const XString* XWizardPage_pixmap(const XWizardPage* self, int which)
{
    const XString* pagePixmap;
    if (!self || which < 0 || which >= XWizardPixmap_NPixmaps) return NULL;
    pagePixmap = self->m_pixmaps[which];
    if (pagePixmap && XString_size((const XContainer*)pagePixmap) > 0)
        return pagePixmap;
    /* 对齐 QWizardPage::pixmap（qwizard.cpp:3503-3515）：本槽页图为空
     * 时回落向导同槽 pixmap(which)；二者皆空返回 **NULL**（Qt 返回
     * isNull()==true 的空 QPixmap，setPixmap(which, QPixmap()) 后读回
     * 同样是 null，不保留"空串对象"这一中间态）。 */
    if (self->m_wizard) {
        const XString* wizardPixmap = XWizard_pixmap(self->m_wizard, which);
        if (wizardPixmap &&
            XString_size((const XContainer*)wizardPixmap) > 0)
            return wizardPixmap;
    }
    return NULL;
}

void XWizardPage_registerField(XWizardPage* self, const char* name,
                               XWidget* widget, const char* property,
                               size_t changedSignal)
{
    XWizard* wiz;
    XWizardFieldEntry* f;
    char buf[128];
    size_t n;
    bool mandatory = false;
    if (!self || !name || !name[0]) return;
    wiz = self->m_wizard;
    if (!wiz) return; /* 无向导无字段表（Qt 存 pendingFields，本库无此缓冲）。 */
    n = XStrlen(name);
    if (n >= sizeof(buf)) return; /* 字段名过长忽略（本库定长缓冲防截断）。 */
    XMemcpy(buf, name, n + 1);
    /* 必填标记：Qt 的字段名以 '*' **结尾**（qwizard.cpp:173-176）。 */
    if (n > 0 && buf[n - 1] == '*') {
        mandatory = true;
        buf[n - 1] = '\0';
    }
    if (!buf[0]) return;
    if (xwiz_fieldFind(wiz, buf)) return; /* Qt 打印 Duplicate field 并忽略。 */
    f = xwiz_fieldCreate(wiz);
    if (!f) return;
    f->name = XString_create_utf8(buf);
    f->value = XString_create();
    f->initialValue = XString_create();
    f->page = self;
    f->widget = widget;
    f->mandatory = mandatory;
    f->changedSignal = changedSignal;
    if (property && property[0]) f->property = XString_create_utf8(property);
    /* 解析缺省属性（对标 QWizardField::resolve：property 为空时按类名
     * 查 defaultPropertyTable，qwizard.cpp:179-197）。 */
    if (!f->property && widget) {
        int i;
        for (i = 0; i < wiz->m_defaultPropCount; ++i) {
            XWizardDefaultProperty* dp = &wiz->m_defaultProps[i];
            const char* cn = dp->className ? XString_toUtf8(dp->className) : NULL;
            if (!cn || !dp->property) continue;
            if (XObject_inherits((const XObject*)widget, cn)) {
                f->property = XString_create_copy(dp->property);
                if (!f->changedSignal) f->changedSignal = dp->changedSignal;
                break;
            }
        }
    }
    /* 捕获初值：当前控件属性值（Qt 的 initialValue =
     * object->property(property)）。 */
    {
        XString scratch;
        const char* cur;
        XString_init(&scratch);
        cur = xwiz_fieldValue(f, &scratch);
        XString_assign_utf8(f->initialValue, cur);
        XString_assign_utf8(f->value, cur);
        XClassDeinit(&scratch);
    }
    /* 必填 + 变化信号：值变即重估 completeChanged（对标
     * QWizardPrivate::addField 的 connect(changedSignal,
     * _q_maybeEmitCompleteChanged)，qwizard.cpp:734-735）。 */
    if (f->mandatory && f->changedSignal && widget) {
        XObject_connect_1((XObject*)widget, f->changedSignal,
                          (XObject*)self, xwizpage_fieldChangedSlot,
                          XConnectionType_Direct);
    }
}

void XWizardPage_setComplete(XWizardPage* self, bool complete)
{
    if (!self) return;
    if (self->m_complete == complete) return;
    self->m_complete = complete;
    /* 对标 Qt：complete 状态变化发射 completeChanged()，向导据此更新
       Next/Finish 使能（此前仅存值，向导无从感知）。 */
    self->m_lastComplete = XWizardPage_isComplete(self);
    self->m_lastCompleteValid = true;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XWizardPage_completeChanged_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

bool XWizardPage_isComplete(const XWizardPage* self)
{
    /* 对标 QWizardPage::isComplete 的虚函数语义（qwizard.cpp:3598）：
     * 查虚表分派，子类覆盖即生效；默认实现读完备位。 */
    if (!self || !XClassGetVtable(self)) return false;
    return XClassGetVirtualFunc(self, EXWizardPage_IsComplete,
                                bool (*)(const XWizardPage*))(self);
}

const XString* XWizardPage_field(const XWizardPage* self, const char* name)
{
    XString* key;
    const XString* value;
    if (!self || !self->m_wizard || !name) return NULL;
    key = XString_create_utf8(name);
    if (!key) return NULL;
    value = XWizard_field(self->m_wizard, key);
    XClassDelete((XClass*)key);
    return value; /* 借用向导字段表里的值，随字段表失效。 */
}

void XWizardPage_setField(XWizardPage* self, const char* name,
                          const char* value)
{
    if (!self || !self->m_wizard) return;
    XWizard_setField_2(self->m_wizard, name, value);
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

/** @brief 缺省按钮文本（对标 qwizard.cpp:620-641 buttonDefaultText）。
 *  @details Qt 的缺省文本是 `QWizard::tr("< &Back")` 等源串（C locale
 *           实测 `< &Back`/`&Next >`/`Commit`/`&Finish`/`Cancel`/`&Help`，
 *           自定义槽为空串）；本库无翻译设施，按同一语义给中文缺省
 *           （与 XDialogButtonBox 的标准按钮中文文案同惯例）。 */
static const char* xwiz_defaultButtonText(XWizardButton which)
{
    switch (which) {
    case XWizardButton_BackButton: return "< 上一页";
    case XWizardButton_NextButton: return "下一页 >";
    case XWizardButton_CommitButton: return "提交";
    case XWizardButton_FinishButton: return "完成";
    case XWizardButton_CancelButton: return "取消";
    case XWizardButton_HelpButton: return "帮助";
    default: return "";
    }
}

/** @brief 向导级按钮文本（自定义层优先，缺省层兜底；对标 QWizard::
 *         buttonText，qwizard.cpp:2623-2648）。
 *  @details Qt 用 buttonCustomTexts 映射区分"显式设过（含显式设空）"与
 *           "未设"，本库以 m_buttonTextCustom 位同义区分：设过即返回
 *           存储值（可以为空串），未设返回缺省文本。 */
static const char* xwiz_wizardButtonText(const XWizard* self,
                                         XWizardButton which)
{
    const char* text;
    if (!self || which < 0 || which >= XWizardButton_NStandardButtons)
        return "";
    if (self->m_buttonTextCustom[which] && self->m_buttonTexts[which]) {
        text = XString_toUtf8(self->m_buttonTexts[which]);
        return text ? text : "";
    }
    return xwiz_defaultButtonText(which);
}

/** @brief 查页 id 对应的页表下标（未登记返回 -1）。 */
static int xwiz_indexOfId(const XWizard* self, int id)
{
    int i;
    if (!self || id < 0) return -1;
    for (i = 0; i < self->m_pageCount; ++i) {
        if (self->m_pageIds[i] == id) return i;
    }
    return -1;
}

/** @brief 当前页 id（无当前页返回 -1，对标 QWizard::currentId）。 */
static int xwiz_currentId(const XWizard* self)
{
    if (!self || self->m_currentIndex < 0 ||
        self->m_currentIndex >= self->m_pageCount)
        return -1;
    return self->m_pageIds[self->m_currentIndex];
}

/** @brief 起始页 id（对标 QWizardPrivate::start：维护为已解析值）。
 *         无页面返回 -1。 */
static int xwiz_resolveStartId(const XWizard* self)
{
    if (!self) return -1;
    if (self->m_pageCount <= 0) return -1;
    return self->m_startId;
}

/** @brief 按钮是否在当前布局序列里（对标 QWizardPrivate::
 *         buttonLayoutContains：无自定义布局时恒 true）。 */
static bool xwiz_layoutContains(const XWizard* self, XWizardButton which)
{
    int i;
    if (!self || !self->m_hasCustomLayout) return true;
    for (i = 0; i < self->m_buttonLayoutCount; ++i) {
        if (self->m_buttonLayoutSeq[i] == (int)which) return true;
    }
    return false;
}

/** @brief 取槽位当前活动按钮（登记替换优先，否则预建按钮）。 */
static struct XAbstractButton* xwiz_slotButton(const XWizard* self, int which)
{
    if (!self || which < 0 || which >= XWizardButton_NButtons) return NULL;
    if (self->m_customButtons[which]) return self->m_customButtons[which];
    return self->m_buttons[which];
}

#if XPUSHBUTTON_ON
/** @brief 同值早退的显隐设置。
 *  @details XWidget_setVisible 承载布局激活、框架**刻意**不做同值早退
 *           （见 AGENTS.md「推送里每拍都调的 setter 必须自己判同值」），
 *           而 xwiz_updateButtons 每次导航/completeChanged 都会跑一遍，
 *           故先比显式隐藏位，状态没变就不调。 */
static void xwiz_setShown(XWidget* widget, bool shown)
{
    if (!widget) return;
    if (XWidget_isHidden(widget) == !shown) return;
    XWidget_setVisible(widget, shown);
}

/** @brief 槽位显隐/使能/文本收口（只作用于预建按钮；登记替换的按钮由
 *         setButton 自行 show，Qt 同款：自定义按钮总是显示）。 */
static void xwiz_applySlot(XWizard* self, XWizardButton which, bool shown,
                           bool enabled)
{
    XWidget* btn = (XWidget*)self->m_buttons[which];
    const char* txt;
    if (!btn) return;
    /* 槽位被 setButton 登记的自定义按钮占用时预建按钮隐藏（Qt 在
       setButton 里把旧按钮移出布局并删除；本库借用承载下改为隐藏）。 */
    if (self->m_customButtons[which]) {
        xwiz_setShown(btn, false);
        XWidget_setEnabled(btn, enabled);
        return;
    }
    xwiz_setShown(btn, shown);
    XWidget_setEnabled(btn, enabled);
    txt = xwiz_wizardButtonText(self, which);
    if (txt && txt[0])
        XAbstractButton_setText_2((XAbstractButton*)btn, txt);
}
#endif /* XPUSHBUTTON_ON */

/** @brief 更新按钮的可见性/启用/文本（对标 QWizardPrivate::
 *         _q_updateButtonStates，qwizard.cpp:1500-1560，本机 6.8.3 逐态实测）。
 *  @details 公式与 Qt 逐条同构（canContinue = nextId()!=-1、
 *           canFinish = isFinalPage）：
 *           - Back 使能 = history>1 && 上一页不是提交页 &&
 *             (!canFinish || !DisabledBackButtonOnLastPage)；
 *           - Back 可见 = layoutContains && (history>1 ||
 *             !NoBackButtonOnStartPage) && (canContinue ||
 *             !NoBackButtonOnLastPage)；
 *           - Next 使能 = canContinue && complete；可见 = layoutContains
 *             && !commitPage && (canContinue || HaveNextButtonOnLastPage)；
 *           - Commit：提交页替代 Next（可见 = layoutContains && commitPage
 *             && canContinue），使能同 Next；
 *           - Finish 使能 = canFinish && complete；可见 = layoutContains &&
 *             (canFinish || HaveFinishButtonOnEarlyPages)；
 *           - Cancel 可见 = !NoCancelButton && layoutContains &&
 *             (canContinue || !NoCancelButtonOnLastPage)；
 *           - Help 可见 = HaveHelpButton（Qt 的布局序列按 HelpButtonOnRight
 *             决定左右，可见性由布局序列包含与否决定）；
 *           - Custom1..3 可见 = HaveCustomButton1..3。
 *  @param self 目标向导；NULL 时为空操作。 */
static void xwiz_updateButtons(XWizard* self)
{
    XWizardPage* current;
    bool currentComplete = true;
    bool canContinue = false;
    bool canFinish = false;
    bool commitPage = false;
    bool backVisible;
    bool backEnabled;
    bool nextVisible;
    bool nextEnabled;
    bool finishVisible;
    bool finishEnabled;
    bool cancelVisible;
    bool helpVisible;
    bool customVisible[XWizardButton_NButtons] = { false, false, false };
    if (!self) return;
    current = (self->m_currentIndex >= 0 &&
               self->m_currentIndex < self->m_pageCount)
                  ? self->m_pages[self->m_currentIndex]
                  : NULL;
    /* Qt：complete = page && page->isComplete()；canContinue = nextId()!=-1；
     * canFinish = page && page->isFinalPage()（switchToPage 里算好）。 */
    currentComplete = current ? XWizardPage_isComplete(current) : false;
    canContinue = current && XWizardPage_nextId(current) != -1;
    canFinish = current && XWizardPage_isFinalPage(current);
    commitPage = current && XWizardPage_isCommitPage(current);
    backVisible = xwiz_layoutContains(self, XWizardButton_BackButton) &&
                  (self->m_historyCount > 0 ||
                   !(self->m_options &
                     (int)XWizardOption_NoBackButtonOnStartPage)) &&
                  (canContinue ||
                   !(self->m_options &
                     (int)XWizardOption_NoBackButtonOnLastPage));
    /* 本库历史栈只存**旧页 id**（长度 >0 即有上一页），Qt 的 history 含
     * 当前页（size>1 才有上一页）——故此处用 m_historyCount>0。上一页是
     * 提交页时 Back 禁用（qwizard.cpp:1509-1511）。 */
    {
        bool prevIsCommit = false;
        if (self->m_historyCount > 0) {
            int prevIdx = xwiz_indexOfId(
                self, self->m_history[self->m_historyCount - 1]);
            if (prevIdx >= 0 && self->m_pages[prevIdx])
                prevIsCommit = XWizardPage_isCommitPage(self->m_pages[prevIdx]);
        }
        backEnabled = self->m_historyCount > 0 && !prevIsCommit &&
                      (!canFinish ||
                       !(self->m_options &
                         (int)XWizardOption_DisabledBackButtonOnLastPage));
    }
    nextVisible = xwiz_layoutContains(self, XWizardButton_NextButton) &&
                  !commitPage &&
                  (canContinue ||
                   (self->m_options &
                    (int)XWizardOption_HaveNextButtonOnLastPage));
    nextEnabled = canContinue && currentComplete;
    finishVisible = xwiz_layoutContains(self, XWizardButton_FinishButton) &&
                    (canFinish ||
                     (self->m_options &
                      (int)XWizardOption_HaveFinishButtonOnEarlyPages));
    finishEnabled = canFinish && currentComplete;
    cancelVisible = !(self->m_options & (int)XWizardOption_NoCancelButton) &&
                    xwiz_layoutContains(self, XWizardButton_CancelButton) &&
                    (canContinue ||
                     !(self->m_options &
                       (int)XWizardOption_NoCancelButtonOnLastPage));
    helpVisible = (self->m_options & (int)XWizardOption_HaveHelpButton) != 0;
    customVisible[XWizardButton_CustomButton1] =
        (self->m_options & (int)XWizardOption_HaveCustomButton1) != 0;
    customVisible[XWizardButton_CustomButton2] =
        (self->m_options & (int)XWizardOption_HaveCustomButton2) != 0;
    customVisible[XWizardButton_CustomButton3] =
        (self->m_options & (int)XWizardOption_HaveCustomButton3) != 0;
#if XPUSHBUTTON_ON
    xwiz_applySlot(self, XWizardButton_BackButton, backVisible, backEnabled);
    xwiz_applySlot(self, XWizardButton_NextButton, nextVisible, nextEnabled);
    xwiz_applySlot(self, XWizardButton_CommitButton,
                   xwiz_layoutContains(self, XWizardButton_CommitButton) &&
                       commitPage && canContinue,
                   nextEnabled);
    xwiz_applySlot(self, XWizardButton_FinishButton, finishVisible,
                   finishEnabled);
    xwiz_applySlot(self, XWizardButton_CancelButton, cancelVisible, true);
    xwiz_applySlot(self, XWizardButton_HelpButton, helpVisible, true);
    xwiz_applySlot(self, XWizardButton_CustomButton1,
                   customVisible[XWizardButton_CustomButton1], true);
    xwiz_applySlot(self, XWizardButton_CustomButton2,
                   customVisible[XWizardButton_CustomButton2], true);
    xwiz_applySlot(self, XWizardButton_CustomButton3,
                   customVisible[XWizardButton_CustomButton3], true);
    (void)nextEnabled; (void)finishEnabled;
#else
    (void)backVisible; (void)backEnabled; (void)nextVisible; (void)nextEnabled;
    (void)finishVisible; (void)finishEnabled; (void)cancelVisible;
    (void)helpVisible; (void)customVisible; (void)currentComplete;
    (void)canContinue; (void)canFinish; (void)commitPage;
#endif
    /* 显隐刚变过，按钮行必须重排（隐藏按钮不占位、新显示的按钮需要
     * 几何；init 时按钮全隐藏，几何只能在此后第一次显示时铺开）。 */
    xwiz_layoutButtons(self);
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
 * @brief      切换到指定页 id（隐藏旧页、触发虚槽、显示新页、更新按钮、
 *             发射 currentIdChanged）。
 * @details    对标 QWizardPrivate::switchToPage（qwizard.cpp:755-812）的
 *             虚槽时序：Backward 且未开启 IndependentPages 时对**旧页**
 *             调用 cleanupPage 并复位其 m_initialized；进入新页时若
 *             m_initialized 为 false 则置位并调用 initializePage（每页
 *             首次显示触发一次）。清理/初始化都走**向导级虚槽**
 *             （XWizard_cleanupPage_2/XWizard_initializePage_2，默认转发
 *             到页级虚槽，子类可定制）。
 *             前进（Forward）时把旧页 id 压入导航历史栈（对标
 *             QWizardPrivate::history：Qt 的 back() 退的是历史栈顶而不是
 *             上一个 id；本机 6.8.3 实测 0→2 直接跳页后 back() 回到 0）。
 * @param      self 目标向导指针；NULL 或未知 id 时忽略。
 * @param      id 目标页 id。
 * @param      direction 导航方向。
 * @return     无返回值。
 */
static void xwiz_switchTo(XWizard* self, int id,
                          XWizardNavDirection direction)
{
    XWizardPage* oldPage;
    XWizardPage* newPage;
    bool independent;
    int index;
    int oldId;
    if (!self) return;
    index = xwiz_indexOfId(self, id);
    if (index < 0) return;
    if (index == self->m_currentIndex) return;
    independent = (self->m_options & (int)XWizardOption_IndependentPages) != 0;
    oldPage = (self->m_currentIndex >= 0 &&
               self->m_currentIndex < self->m_pageCount)
                  ? self->m_pages[self->m_currentIndex]
                  : NULL;
    oldId = xwiz_currentId(self);
    newPage = self->m_pages[index];
    if (oldPage) {
        XWidget_setVisible((XWidget*)oldPage, false);
        if (direction == XWizardNavDirection_Backward && !independent) {
            XWizard_cleanupPage_2(self, oldId);
            oldPage->m_initialized = false;
        }
    }
    /* 前进压栈（后退由 XWizard_back 弹栈，不再压入）。 */
    if (direction == XWizardNavDirection_Forward && oldPage &&
        self->m_historyCount < (uint8_t)XWIZARD_MAX_PAGES) {
        self->m_history[self->m_historyCount++] = oldId;
    }
    if (newPage) {
        if (!newPage->m_initialized) {
            newPage->m_initialized = true;
            XWizard_initializePage_2(self, id);
        }
        XWidget_setVisible((XWidget*)newPage, true);
    }
    self->m_currentIndex = (int8_t)index;
    xwiz_layoutCurrentPage(self);
    self->m_visited[index] = true;
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_currentIdChanged_signal, id);
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

#if XABSTRACTBUTTON_ON
/** @brief 登记/预建按钮 clicked 分发（对标 QWizardPrivate::connectButton）。
 *  @details 标准槽按槽位语义执行导航动作（Back→back、Next/Commit→next、
 *           Finish→accept、Cancel→reject、Help→helpRequested），自定义槽
 *           发射 customButtonClicked(which)（qwizard.cpp:813-831 的
 *           buttonSlots 表 + _q_emitCustomButtonClicked）。同一槽挂全部
 *           登记按钮，经 XObject_sender 反查槽位；查表未命中静默忽略。 */
static void xwiz_customButtonClickedSlot(XObject* receiver, XVarList* args)
{
    XWizard* self = (XWizard*)receiver;
    XObject* sender = XObject_sender(receiver);
    int which;
    (void)args;
    if (!self || !sender) return;
    for (which = 0; which < XWizardButton_NButtons; ++which) {
        if ((XObject*)self->m_customButtons[which] == sender) {
            switch (which) {
            case XWizardButton_BackButton:
                XWizard_back(self);
                break;
            case XWizardButton_NextButton:
            case XWizardButton_CommitButton:
                XWizard_next(self);
                break;
            case XWizardButton_FinishButton:
                XDialog_accept((XDialog*)self);
                break;
            case XWizardButton_CancelButton:
                XDialog_reject((XDialog*)self);
                break;
            case XWizardButton_HelpButton:
                xwiz_emitVoid(self, (size_t)XWizard_helpRequested_signal);
                break;
            default:
                xwiz_emitInt(self,
                             (size_t)XWizard_customButtonClicked_signal, which);
                break;
            }
            return;
        }
    }
}

/** @brief 断开自定义按钮的点击转发（setButton 替换/清除与析构摘链共用）。 */
static void xwiz_customButtonDisconnect(XWizard* self,
                                        struct XAbstractButton* btn)
{
    if (!self || !btn) return;
    XObject_disconnect_1(
        (XObject*)btn,
        (size_t)XAbstractButton_clicked_signal((XAbstractButton*)btn, false),
        (XObject*)self, xwiz_customButtonClickedSlot);
}
#endif /* XABSTRACTBUTTON_ON */

#if XPUSHBUTTON_ON && XABSTRACTBUTTON_ON
/** @brief 预建自定义按钮（CustomButton1..3）点击 → customButtonClicked(which)。
 *  @details Qt 对预建自定义按钮连接 _q_emitCustomButtonClicked
 *           （qwizard.cpp:1385-1390）；本库同口径。 */
static void xwiz_prebuiltCustomSlot(XObject* receiver, XVarList* args)
{
    XWizard* self = (XWizard*)receiver;
    XObject* sender = XObject_sender(receiver);
    int which;
    (void)args;
    if (!self || !sender) return;
    for (which = 0; which < XWizardButton_NButtons; ++which) {
        if ((XObject*)self->m_buttons[which] == sender) {
            xwiz_emitInt(self,
                         (size_t)XWizard_customButtonClicked_signal, which);
            return;
        }
    }
}
#endif /* XPUSHBUTTON_ON && XABSTRACTBUTTON_ON */

/* ==================== XWizard 生命周期与虚表 ==================== */

static void VX_wizard_paintEvent(XWidget* self, XEvent* event);

/** @brief  内容顶偏移：CSD 框架装饰条占客户区顶部时的让位像素。
 *  @details 框架自绘标题条（XWindowDecoration）画在客户区顶部（系统
 *           标题栏在客户区外），向导横幅此前从 y=0 起绘、页几何与按
 *           钮行按整高推导，首行内容被条带遮住、底部门按钮带被通用
 *           避让的整窗位移推出窗外（demo 480x320 按钮半裁实测）。修
 *           法对标 XMessageBox：XDialog 的通用避让对向导只记账不免除
 *           （XWizard 例外已登记），横幅/页几何/按钮行经本查询自管
 *           理让位——系统标题栏/CSD 抑制/子控件形态恒 0，布局零变
 *           化；未建窗预测期 marginsFor 回退默认条高，SHOW 重排按真
 *           值收敛。 */
static int xwiz_contentTop(const XWizard* self)
{
#if XWIZ_CSD_QUERY_ON
    if (!self) return 0;
    return XDialog_decorationTopOffset((const XDialog*)self);
#else
    (void)self;
    return 0;
#endif
}

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
    bannerH = xwiz_contentTop(self) + xwiz_bannerHeight(self);
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
    /* 顶部标题栏（有副标题时两行，对标 QWizard 横幅结构）；横幅顶从
       CSD 装饰条下缘起（xwiz_contentTop，系统条/抑制态恒 0）。 */
    page = XWizard_currentPage(wiz);
    if (page) {
        int top = xwiz_contentTop(wiz);
        int bannerH = xwiz_bannerHeight(wiz);
        XRect head = { 0, top, r.width, bannerH };
        const char* sub;
        XPainter_fillRect(&painter, &head, highlight);
        XSnprintf(buf, sizeof(buf), "%s", XWizardPage_title(page));
        XPainter_drawText(&painter, 8, top + bannerH - (bannerH >= 56 ? 34 : 12),
                          buf, 0xFFFFFFFFu);
        sub = XWizardPage_subTitle(page);
        if (sub && sub[0])
            XPainter_drawText(&painter, 8, top + bannerH - 12, sub, 0xFFD8E8F8u);
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
            XClassDelete(self->m_buttonTexts[bi]);
            self->m_buttonTexts[bi] = NULL;
        }
    }
    {
        int fi;
        for (fi = 0; fi < self->m_fieldCount; ++fi) {
            if (self->m_fields[fi].name)
                XClassDelete(self->m_fields[fi].name);
            if (self->m_fields[fi].value)
                XClassDelete(self->m_fields[fi].value);
            if (self->m_fields[fi].property)
                XClassDelete(self->m_fields[fi].property);
            if (self->m_fields[fi].initialValue)
                XClassDelete(self->m_fields[fi].initialValue);
            self->m_fields[fi].name = NULL;
            self->m_fields[fi].value = NULL;
            self->m_fields[fi].property = NULL;
            self->m_fields[fi].initialValue = NULL;
        }
        self->m_fieldCount = 0;
    }
    {
        int pi;
        for (pi = 0; pi < XWizardPixmap_NPixmaps; ++pi) {
            if (self->m_pixmaps[pi]) {
                XClassDelete(self->m_pixmaps[pi]);
                self->m_pixmaps[pi] = NULL;
            }
        }
    }
    {
        /* 默认属性登记表（className/property 拥有；信号为标识不释放）。 */
        int di;
        for (di = 0; di < self->m_defaultPropCount; ++di) {
            if (self->m_defaultProps[di].className)
                XClassDelete(self->m_defaultProps[di].className);
            if (self->m_defaultProps[di].property)
                XClassDelete(self->m_defaultProps[di].property);
            self->m_defaultProps[di].className = NULL;
            self->m_defaultProps[di].property = NULL;
            self->m_defaultProps[di].changedSignal = 0;
        }
        self->m_defaultPropCount = 0;
    }
#if XABSTRACTBUTTON_ON
    {
        /* 复扫 R-84 配套：登记按钮为借用承载——析构级联删子前先断开
         * 点击转发并摘除父链，按钮归还调用方（头文件契约：向导不删除）。
         * 若不摘链，VXObject_deinit 会把借用按钮当普通子控件级联删除，
         * 调用方再删除即双重释放。 */
        int ci;
        for (ci = 0; ci < XWizardButton_NButtons; ++ci) {
            XWidget* cb = (XWidget*)self->m_customButtons[ci];
            if (!cb) continue;
            xwiz_customButtonDisconnect(self, self->m_customButtons[ci]);
            if (XWidget_parentWidget(cb) == (XWidget*)self)
                XWidget_setParent(cb, NULL, 0);
            self->m_customButtons[ci] = NULL;
        }
    }
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

/* ==================== 布局（按钮行随尺寸重排） ==================== */

/** @brief 单个按钮槽位布局：setButton 登记的自定义按钮优先占用槽位
 *         （复扫 R-84），无登记时落回内建按钮；槽位无任何按钮跳过。 */
static void xwiz_layoutButtonSlot(XWizard* self, XWizardButton which,
                                  int x, int by, int bw, int bh)
{
    XWidget* btn;
    if (!self) return;
    btn = (XWidget*)xwiz_slotButton(self, (int)which);
    if (btn)
        XWidget_setGeometry(btn, x, by, bw, bh);
}

/** @brief 构造按钮行布局序列（对标 QWizardPrivate::updateButtonLayout 的
 *         缺省序列，qwizard.cpp:1462-1490）。
 *  @details 缺省序列（12 槽）：
 *           `[Help(左)] [Stretch] [Custom1] [Custom2] [Custom3]
 *            [Cancel(左)] Back Next Commit Finish [Cancel(右)]
 *            [Help(右)]`
 *           Help 由 HaveHelpButton 开关、左右由 HelpButtonOnRight 决定；
 *           Cancel 由 NoCancelButton 开关、左右由 CancelButtonOnLeft 决定；
 *           Custom1..3 由 HaveCustomButton1..3 开关；Back/Next/Commit/
 *           Finish 恒在序列里（可见性由 xwiz_updateButtons 决定）。
 *           自定义布局（setButtonLayout）优先。
 *  @return 序列长度（写入 out）。 */
static int xwiz_layoutSequence(const XWizard* self, int* out, int cap)
{
    int n = 0;
    if (!self || !out || cap <= 0) return 0;
    if (self->m_hasCustomLayout) {
        for (n = 0; n < self->m_buttonLayoutCount && n < cap; ++n)
            out[n] = self->m_buttonLayoutSeq[n];
        return n;
    }
#define XWIZ_PUT(v) do { if (n < cap) out[n++] = (v); } while (0)
    if ((self->m_options & (int)XWizardOption_HaveHelpButton) &&
        !(self->m_options & (int)XWizardOption_HelpButtonOnRight))
        XWIZ_PUT((int)XWizardButton_HelpButton);
    XWIZ_PUT((int)XWizardButton_Stretch);
    if (self->m_options & (int)XWizardOption_HaveCustomButton1)
        XWIZ_PUT((int)XWizardButton_CustomButton1);
    if (self->m_options & (int)XWizardOption_HaveCustomButton2)
        XWIZ_PUT((int)XWizardButton_CustomButton2);
    if (self->m_options & (int)XWizardOption_HaveCustomButton3)
        XWIZ_PUT((int)XWizardButton_CustomButton3);
    if (!(self->m_options & (int)XWizardOption_NoCancelButton) &&
        (self->m_options & (int)XWizardOption_CancelButtonOnLeft))
        XWIZ_PUT((int)XWizardButton_CancelButton);
    XWIZ_PUT((int)XWizardButton_BackButton);
    XWIZ_PUT((int)XWizardButton_NextButton);
    XWIZ_PUT((int)XWizardButton_CommitButton);
    XWIZ_PUT((int)XWizardButton_FinishButton);
    if (!(self->m_options & (int)XWizardOption_NoCancelButton) &&
        !(self->m_options & (int)XWizardOption_CancelButtonOnLeft))
        XWIZ_PUT((int)XWizardButton_CancelButton);
    if ((self->m_options & (int)XWizardOption_HaveHelpButton) &&
        (self->m_options & (int)XWizardOption_HelpButtonOnRight))
        XWIZ_PUT((int)XWizardButton_HelpButton);
#undef XWIZ_PUT
    return n;
}

/** @brief 底部按钮行布局（从当前控件尺寸推导；init/resize/选项变化共用）。
 * @details 对标 Qt 的 QHBoxLayout 排布语义：序列里第一个 `Stretch` 之前
 *          的按钮**左对齐**（从 x=8 累加），之后的按钮**右对齐**（从右
 *          边界回退累加）；无 Stretch 时全部左对齐。**隐藏按钮不占位**
 *          （Qt 布局跳过隐藏控件）。bw=80、gap=6、边距 8。
 * @param self 目标向导。 */
static void xwiz_layoutButtons(XWizard* self)
{
    int seq[XWIZARD_MAX_BUTTON_LAYOUT + 4];
    int n;
    int i;
    int bw = 80;
    int bh = 28;
    int gap = 6;
    int w;
    int h;
    int by;
    int firstStretch = -1;
    int leftX;
    int rightX;
    if (!self) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    by = h - bh - 8;
    n = xwiz_layoutSequence(self, seq, (int)(sizeof(seq) / sizeof(seq[0])));
    for (i = 0; i < n; ++i) {
        if (seq[i] == (int)XWizardButton_Stretch) { firstStretch = i; break; }
    }
    leftX = 8;
    for (i = 0; i < n; ++i) {
        XWidget* btn;
        if (seq[i] == (int)XWizardButton_Stretch ||
            seq[i] == (int)XWizardButton_NoButton)
            continue;
        if (firstStretch >= 0 && i > firstStretch) break;
        btn = (XWidget*)xwiz_slotButton(self, seq[i]);
        if (!btn || XWidget_isHidden(btn)) continue;
        XWidget_setGeometry(btn, leftX, by, bw, bh);
        leftX += bw + gap;
    }
    rightX = w - bw - 8;
    for (i = n - 1; i > firstStretch; --i) {
        XWidget* btn;
        if (seq[i] == (int)XWizardButton_Stretch ||
            seq[i] == (int)XWizardButton_NoButton)
            continue;
        btn = (XWidget*)xwiz_slotButton(self, seq[i]);
        if (!btn || XWidget_isHidden(btn)) continue;
        if (firstStretch < 0) {
            /* 无 Stretch：全部按左对齐（Qt 无弹簧时按钮贴左）。 */
            XWidget_setGeometry(btn, leftX, by, bw, bh);
            leftX += bw + gap;
            continue;
        }
        XWidget_setGeometry(btn, rightX, by, bw, bh);
        rightX -= bw + gap;
    }
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

#if XWIZ_CSD_QUERY_ON
/** @brief 显示：按真值装饰条高重排（CSD 让位收口）。
 *  @details init 期建窗未发，xwiz_contentTop 走 marginsFor 的默认条
 *           高回退；首显建窗（createWindow→syncWindow 落盘真实
 *           frameMargins）先于 SHOW 事件派发，此处重排把横幅/页几何
 *           收敛到真值。XDialog 通用避让对向导只记账（XWizard 例外
 *           已登记），位移/布局两条路径均不介入，无双重让位。 */
static void VX_wizard_showEvent(XWidget* self, XEvent* event)
{
    XWizard* wiz = (XWizard*)self;
    if (wiz && event && XEvent_type(event) == XEVENT_TYPE_SHOW) {
        xwiz_layoutButtons(wiz);
        xwiz_layoutCurrentPage(wiz);
    }
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}
#endif /* XWIZ_CSD_QUERY_ON */

/* ==================== 选项 → 按钮兑现（HaveHelpButton 等） ==================== */

/** @brief 选项变化后同步按钮态与布局。
 *  @details 九个按钮**构造即预建**（对标 QWizardPrivate::init 的
 *           ensureButton + updateButtonLayout），选项只开关显隐：
 *           HaveHelpButton 显示/隐藏 Help、HaveCustomButton1..3 显示/隐藏
 *           自定义槽、HelpButtonOnRight/CancelButtonOnLeft 改变布局序列中
 *           的左右位置（qwizard.cpp:1492-1520）。旧实现"打开才建、关闭即
 *           销毁"与 Qt 的常驻按钮不符（账本 242/243），已撤销。 */
static void xwiz_syncOptionButtons(XWizard* self)
{
    if (!self) return;
    xwiz_updateButtons(self);
    xwiz_layoutButtons(self);
    XWidget_update((XWidget*)self);
}

XVtable* XWizard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWizard)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_wizard_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_wizard_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_wizard_resizeEvent);
    /* 向导级受保护虚槽（对标 QWizard::initializePage(int)/
     * cleanupPage(int)；默认转发到页级虚槽，子类可覆盖；账本 238）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWizard_InitializePage,
                             VXWizard_initializePage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizard_CleanupPage, VXWizard_cleanupPage);
#if XWIZ_CSD_QUERY_ON
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VX_wizard_showEvent);
#endif
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
    {
        int pi;
        for (pi = 0; pi < XWizardPixmap_NPixmaps; ++pi)
            self->m_pixmaps[pi] = XString_create();
    }
    /* 标题/子标题格式缺省 AutoText==2（Qt::TextFormat：PlainText=0、
     * RichText=1、AutoText=2；本机 Qt 6.8.3 实测 titleFormat()==2；
     * r2 对齐，账本 240）。 */
    self->m_titleFormat = 2;
    self->m_subTitleFormat = 2;
    {
        int bi;
        for (bi = 0; bi < XWizardButton_NStandardButtons; ++bi)
            self->m_buttonTexts[bi] = XString_create();
    }
    Set_Class_IsHeap(self, false);
    self->m_pageCount = 0;
    self->m_nextPageId = 0;
    /* 无页面时当前页 -1、startId -1（对标 QWizard 构造：currentId==-1、
     * startId==-1，本机 6.8.3 实测）。 */
    self->m_currentIndex = -1;
    self->m_startId = -1;
    self->m_startSetByUser = false;
    self->m_historyCount = 0;
    self->m_style = (int)XWizardStyle_ClassicStyle;
    self->m_options = 0;
    XWidget_resize((XWidget*)self, 480, 320);
#if XPUSHBUTTON_ON
    {
        /* 九个按钮**构造即预建**（对标 QWizardPrivate::init 的
         * ensureButton(Back/Next/Commit/Finish) + updateButtonLayout 的
         * setButtonLayout 会对序列里每个槽位 ensureButton——Help 与
         * CustomButton1..3 因此也在构造时存在、默认隐藏；本机 6.8.3
         * 实测 button(HelpButton)/button(CustomButton1) 非空；账本 242/243）。
         * 全部隐藏，由 xwiz_updateButtons 按选项/页态决定显隐。 */
        int i;
        for (i = 0; i < XWizardButton_NButtons; ++i) {
            XPushButton* btn = XPushButton_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
            if (!btn) continue;
            self->m_buttons[i] = (struct XAbstractButton*)btn;
            if (i < XWizardButton_NStandardButtons)
                XAbstractButton_setText_2((XAbstractButton*)btn,
                                          xwiz_defaultButtonText((XWizardButton)i));
            XWidget_hide((XWidget*)btn);
        }
        self->m_btnBack = (XPushButton*)self->m_buttons[XWizardButton_BackButton];
        self->m_btnNext = (XPushButton*)self->m_buttons[XWizardButton_NextButton];
        self->m_btnCommit =
            (XPushButton*)self->m_buttons[XWizardButton_CommitButton];
        self->m_btnFinish =
            (XPushButton*)self->m_buttons[XWizardButton_FinishButton];
        self->m_btnCancel =
            (XPushButton*)self->m_buttons[XWizardButton_CancelButton];
        self->m_btnHelp =
            (XPushButton*)self->m_buttons[XWizardButton_HelpButton];
        /* 槽位语义接线（对标 QWizardPrivate::connectButton 的
         * buttonSlots 表）：Back→back、Next/Commit→next、
         * Finish→accept、Cancel→reject、Help→helpRequested；
         * Custom1..3 的点击由 customButtonClicked 承载（预建自定义按钮
         * 也发该信号，Qt 同：_q_emitCustomButtonClicked）。 */
        if (self->m_btnBack)
            XObject_connect_1((XObject*)self->m_btnBack,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xwiz_btnBackSlot,
                              XConnectionType_Direct);
        if (self->m_btnNext)
            XObject_connect_1((XObject*)self->m_btnNext,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xwiz_btnNextSlot,
                              XConnectionType_Direct);
        if (self->m_btnCommit)
            XObject_connect_1((XObject*)self->m_btnCommit,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xwiz_btnNextSlot,
                              XConnectionType_Direct);
        if (self->m_btnFinish)
            XObject_connect_1((XObject*)self->m_btnFinish,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xwiz_btnFinishSlot,
                              XConnectionType_Direct);
        if (self->m_btnCancel)
            XObject_connect_1((XObject*)self->m_btnCancel,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xwiz_btnCancelSlot,
                              XConnectionType_Direct);
        if (self->m_btnHelp)
            XObject_connect_1((XObject*)self->m_btnHelp,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)self, xwiz_btnHelpSlot,
                              XConnectionType_Direct);
        /* 预建自定义按钮：点击发射 customButtonClicked(which)。 */
        {
            int ci;
            for (ci = XWizardButton_CustomButton1;
                 ci < XWizardButton_NButtons; ++ci) {
                XObject* cb = (XObject*)self->m_buttons[ci];
                if (!cb) continue;
                XObject_connect_1(cb, XSignal(XAbstractButton_clicked_signal),
                                  (XObject*)self,
                                  xwiz_prebuiltCustomSlot,
                                  XConnectionType_Direct);
            }
        }
        /* 统一布局 + 按钮态（按当前控件尺寸/选项推导）。 */
        xwiz_syncOptionButtons(self);
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

/** @brief 登记页并接线（addPage/setPage 共用）。
 *  @details `m_nextPageId` 单调分配新 id（Qt：`id = 末项键 + 1`，不回收），
 *           页表按插入序排列；id 与下标从此**分离**（账本 236）。 */
static void xwiz_registerPage(XWizard* self, int idx, int id,
                              XWizardPage* page)
{
    if (!self || !page || idx < 0 || idx >= XWIZARD_MAX_PAGES) return;
    self->m_pages[idx] = page;
    self->m_pageIds[idx] = id;
    self->m_visited[idx] = false;
    page->m_wizard = self;
    XWidget_setParent((XWidget*)page, (XWidget*)self, 0);
    /* 页完备性变化 → 按钮使能实时刷新（对标 Qt 内部 completeChanged
       到 QWizardPrivate::updateButtonLayout 的联动）。 */
    XObject_connect_1((XObject*)page,
                      (size_t)XWizardPage_completeChanged_signal(page),
                      (XObject*)self, xwizard_pageCompleteChanged,
                      XConnectionType_Direct);
}

int XWizard_addPage(XWizard* self, XWizardPage* page)
{
    int idx;
    int id;
    if (!self || !page) return -1;
    if (self->m_pageCount >= XWIZARD_MAX_PAGES) return -1;
    idx = self->m_pageCount;
    id = self->m_nextPageId++;
    self->m_pageCount++;
    xwiz_registerPage(self, idx, id, page);
    /* 起始页：缺省（未显式 setStartId）且新 id 成为最小 id 时更新
     * （对标 QWizard::setPage 的 `if (!startSetByUser &&
     * pageMap.constBegin().key() == theid) start = theid;`）。 */
    if (!self->m_startSetByUser &&
        (self->m_startId == -1 || id < self->m_startId))
        self->m_startId = id;
    if (idx == 0) {
        XWidget_setVisible((XWidget*)page, true);
        self->m_visited[0] = true;
        /* 首页挂载即成为当前页（本库便利语义：对标 QWizard::showEvent →
         * restart() 进入起始页；项目有意偏差，见头文件/apitest 锁）。
         * 显式置 0 以覆盖"移除全部页后重加"时 m_currentIndex == -1 的
         * 现场；统一走 xwiz_layoutCurrentPage（横幅之下、按钮带之上）。 */
        self->m_currentIndex = 0;
        xwiz_layoutCurrentPage(self);
        if (!page->m_initialized) {
            page->m_initialized = true;
            XWizard_initializePage_2(self, id);
        }
    } else {
        XWidget_setVisible((XWidget*)page, false);
    }
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_pageAdded_signal, id);
    return id;
}

/** @brief 从字段表移除某页的全部字段（removePage 用；对标 Qt 把字段退回
 *         pendingFields 后 removeFieldAt，本库直接释放）。 */
static void xwiz_fieldDropForPage(XWizard* self, XWizardPage* page)
{
    int i;
    int out = 0;
    if (!self || !page) return;
    for (i = 0; i < self->m_fieldCount; ++i) {
        XWizardFieldEntry* f = &self->m_fields[i];
        if (f->page == page) {
            if (f->name) XClassDelete(f->name);
            if (f->value) XClassDelete(f->value);
            if (f->property) XClassDelete(f->property);
            if (f->initialValue) XClassDelete(f->initialValue);
            XMemset(f, 0, sizeof(*f));
            continue;
        }
        if (out != i) self->m_fields[out] = self->m_fields[i];
        out++;
    }
    self->m_fieldCount = (uint8_t)out;
}

void XWizard_setPage(XWizard* self, int id, XWizardPage* page)
{
    int idx;
    if (!self || !page || id < 0) return;
    /* 对标 QWizard::setPage（qwizard.cpp）：页 id 已存在 ⇒ 整份忽略
     * （"Page with duplicate ID %d ignored"）。替换页面须先 removePage。 */
    if (xwiz_indexOfId(self, id) >= 0) return;
    if (self->m_pageCount >= XWIZARD_MAX_PAGES) return;
    idx = self->m_pageCount;
    self->m_pageCount++;
    xwiz_registerPage(self, idx, id, page);
    XWidget_setVisible((XWidget*)page, false);
    if (id >= self->m_nextPageId) self->m_nextPageId = id + 1;
    if (!self->m_startSetByUser &&
        (self->m_startId == -1 || id < self->m_startId))
        self->m_startId = id;
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_pageAdded_signal, id);
}

void XWizard_removePage(XWizard* self, int id)
{
    int i;
    int index;
    XWizardPage* removed;
    if (!self) return;
    index = xwiz_indexOfId(self, id);
    if (index < 0) return;
    /* 起始页随首个页变化调整（对标 QWizard::removePage 的 start 段）。 */
    if (self->m_startId == id) {
        int firstId = -1;
        for (i = 0; i < self->m_pageCount; ++i) {
            if (i == index) continue;
            if (firstId == -1 || self->m_pageIds[i] < firstId)
                firstId = self->m_pageIds[i];
        }
        self->m_startId = firstId;
        self->m_startSetByUser = false;
    }
    removed = self->m_pages[index];
    if (removed && removed->m_initialized) {
        /* 对标 Qt：被移除页若已初始化，先 cleanupPage(id)（此时页仍在
         * 页表里，向导级默认实现才能定位到它）。 */
        XWizard_cleanupPage_2(self, id);
        removed->m_initialized = false;
    }
    for (i = index; i < self->m_pageCount - 1; ++i) {
        self->m_pages[i] = self->m_pages[i + 1];
        self->m_pageIds[i] = self->m_pageIds[i + 1];
        self->m_visited[i] = self->m_visited[i + 1];
    }
    self->m_pageCount--;
    self->m_pages[self->m_pageCount] = NULL;
    self->m_pageIds[self->m_pageCount] = -1;
    /* 导航历史存的是**页 id**，删除按 id 摘除该条（其余 id 不变、无需
     * 重映射；对标 Qt 的 removePage 从 history 移除该 id）。 */
    {
        int h;
        int out = 0;
        for (h = 0; h < (int)self->m_historyCount; ++h) {
            if (self->m_history[h] == id) continue;
            self->m_history[out++] = self->m_history[h];
        }
        self->m_historyCount = (uint8_t)out;
    }
    if (removed) {
        /* 对标 Qt：移除的页面解除向导归属；本页字段一并从字段表摘除
         * （Qt 退回 pendingFields）。 */
        removed->m_wizard = NULL;
        xwiz_fieldDropForPage(self, removed);
    }
    if (self->m_currentIndex >= self->m_pageCount)
        self->m_currentIndex = (int8_t)(self->m_pageCount - 1);
    xwiz_updateButtons(self);
    xwiz_emitInt(self, (size_t)XWizard_pageRemoved_signal, id);
}

XWizardPage* XWizard_page(XWizard* self, int id)
{
    int index;
    if (!self) return NULL;
    index = xwiz_indexOfId(self, id);
    if (index < 0) return NULL;
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
    return xwiz_currentId(self);
}

void XWizard_next(XWizard* self)
{
    XWizardPage* page;
    int target;
    if (!self || self->m_currentIndex < 0) return;
    /* 对标 QWizard::next：先 validateCurrentPage()（分派 validatePage
       虚槽），再取 nextId()（分派 nextId 虚槽）作为目标页 id。 */
    if (!XWizard_validateCurrentPage(self)) return;
    page = XWizard_currentPage(self);
    target = page ? XWizardPage_nextId(page) : -1;
    if (target < 0) return;
    xwiz_switchTo(self, target, XWizardNavDirection_Forward);
}

void XWizard_back(XWizard* self)
{
    int target;
    if (!self || self->m_historyCount == 0) return;
    /* 对标 QWizard::back：退到导航历史栈顶（不是 id-1——本机 6.8.3
     * 实测 0→2 直接跳页后 back() 回到 0；线性后退只在顺序前进时等价）。 */
    target = self->m_history[--self->m_historyCount];
    xwiz_switchTo(self, target, XWizardNavDirection_Backward);
}

void XWizard_setCurrentIndex(XWizard* self, int id)
{
    if (!self) return;
    xwiz_switchTo(self, id, XWizardNavDirection_Forward);
}

void XWizard_restart(XWizard* self)
{
    int i;
    int start;
    if (!self || self->m_pageCount <= 0) return;
    /* 对标 QWizard::restart = reset() + switchToPage(startId, Forward)：
       复位全部页面的 initialized 标记与访问标记、清空导航历史，再前进
       进入起始页 id；m_currentIndex 先置 -1，保证起始页即使等于当前页
       也会重新触发 initializePage。startId 为 -1 时取首个页 id。 */
    start = xwiz_resolveStartId(self);
    for (i = 0; i < self->m_pageCount; ++i) {
        self->m_visited[i] = false;
        if (self->m_pages[i]) self->m_pages[i]->m_initialized = false;
    }
    self->m_historyCount = 0;
    self->m_currentIndex = -1;
    if (xwiz_indexOfId(self, start) < 0) return;
    xwiz_switchTo(self, start, XWizardNavDirection_Forward);
}

void XWizard_setStartIndex(XWizard* self, int id)
{
    int newStart;
    if (!self) return;
    /* 对标 QWizard::setStartId（qwizard.cpp）：-1 解析为首个页 id
     * （无页面 -1）；同值仅更新 startSetByUser；未知 id 打印警告并忽略。 */
    newStart = id;
    if (id == -1) {
        int i;
        int firstId = -1;
        for (i = 0; i < self->m_pageCount; ++i) {
            if (firstId == -1 || self->m_pageIds[i] < firstId)
                firstId = self->m_pageIds[i];
        }
        newStart = firstId;
    }
    if (self->m_startId == newStart) {
        self->m_startSetByUser = (id != -1);
        return;
    }
    if (xwiz_indexOfId(self, newStart) < 0)
        return; /* 未知 id 忽略（Qt 打印 "Invalid page ID"）。 */
    self->m_startId = newStart;
    self->m_startSetByUser = (id != -1);
}

int XWizard_startIndex(const XWizard* self)
{
    return xwiz_resolveStartId(self);
}

bool XWizard_hasVisitedPage(const XWizard* self, int id)
{
    int index;
    if (!self) return false;
    index = xwiz_indexOfId(self, id);
    if (index < 0) return false;
    return self->m_visited[index];
}

/**
 * @brief      汇总已访问页 id 列表（对标 QWizard::visitedIds）。
 * @details    输出**稳定页 id**（不是页表下标；多页删除后二者不同，
 *             账本 236）；顺序按页表顺序（Qt 的 visitedIds 按 pageMap
 *             键序即 id 升序）。outIds 为 NULL 或 maxCount <= 0 时为
 *             "计数查询"模式，只统计不写入。
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
        if (self->m_visited[i]) outIds[count++] = self->m_pageIds[i];
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
        outIds[i] = self->m_pageIds[i]; /* 稳定页 id（非下标）。 */
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
    int before;
    if (!self) return;
    before = self->m_options;
    if (on) self->m_options |= (int)option;
    else self->m_options &= ~(int)option;
    /* 选项兑现：HaveHelpButton 等按钮存在性/显隐随选项立即生效
     * （对标 Qt 的 setOption → updateButtonStates）。 */
    if (before != self->m_options)
        xwiz_syncOptionButtons(self);
}

bool XWizard_testOption(const XWizard* self, XWizardOption option)
{
    return self ? (self->m_options & (int)option) != 0 : false;
}

void XWizard_setOptions(XWizard* self, int options)
{
    int before;
    if (!self) return;
    before = self->m_options;
    self->m_options = options;
    if (before != options)
        xwiz_syncOptionButtons(self);
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
    /* 显式设过即记 custom 位（显式设空也返回空串，网 Qt 的
     * buttonCustomTexts 映射语义）。 */
    self->m_buttonTextCustom[which] = true;
    xwiz_updateButtons(self);
}

const char* XWizard_buttonText(const XWizard* self, XWizardButton which)
{
    return xwiz_wizardButtonText(self, which);
}

struct XAbstractButton* XWizard_button(const XWizard* self, int which)
{
    if (!self || which < 0 || which >= XWizardButton_NButtons)
        return NULL;
    /* setButton 登记的自定义按钮优先；否则返回**预建按钮**（九槽构造
     * 即建，Qt 同：ensureButton/updateButtonLayout；账本 242）。 */
    return xwiz_slotButton(self, which);
}

void XWizard_setButton(XWizard* self, int which, struct XAbstractButton* btn)
{
    struct XAbstractButton* old;
    if (!self || which < 0 || which >= XWizardButton_NButtons) return;
    old = self->m_customButtons[which];
    if (old == (struct XAbstractButton*)btn) return;
#if XABSTRACTBUTTON_ON
    if (old) {
        /* 借用承载（头文件契约）：旧登记按钮不删除；断开点击转发，
         * 曾代挂到本向导的摘父链归还调用方（Qt 此处 delete 旧按钮，
         * 本库偏差已登记）。 */
        xwiz_customButtonDisconnect(self, old);
        if (XWidget_parentWidget((XWidget*)old) == (XWidget*)self)
            XWidget_setParent((XWidget*)old, NULL, 0);
    }
    self->m_customButtons[which] = btn;
    if (btn) {
        /* 对标 QWizard::setButton（qwizard.cpp:2695-2723）：登记按钮接管
         * 呈现——挂为向导子控件、入按钮行布局、点击按槽位语义接线
         * （标准槽执行导航动作，自定义槽发 customButtonClicked）。 */
        XWidget_setParent((XWidget*)btn, (XWidget*)self, 0);
        XWidget_show((XWidget*)btn);
        XObject_connect_1(
            (XObject*)btn,
            (size_t)XAbstractButton_clicked_signal(
                (XAbstractButton*)btn, false),
            (XObject*)self, xwiz_customButtonClickedSlot,
            XConnectionType_Direct);
    }
#else
    /* 无按钮子系统：保留原登记语义（仅 button() 查询可见）。 */
    self->m_customButtons[which] = btn;
#endif
    xwiz_updateButtons(self);
    xwiz_layoutButtons(self);
    XWidget_update((XWidget*)self);
}

void XWizard_setDefaultProperty(XWizard* self, const char* className,
                                const char* property, size_t changedSignal)
{
    int i;
    if (!self || !className || !className[0] || !property || !property[0])
        return;
    /* 同 (className, property) 重复登记覆盖旧信号（对标 Qt 的按类注册
     * 覆盖语义）。 */
    for (i = 0; i < self->m_defaultPropCount; ++i) {
        const char* cn;
        const char* pn;
        if (!self->m_defaultProps[i].className ||
            !self->m_defaultProps[i].property)
            continue;
        cn = XString_toUtf8(self->m_defaultProps[i].className);
        pn = XString_toUtf8(self->m_defaultProps[i].property);
        if (cn && pn && XStrcmp(cn, className) == 0 &&
            XStrcmp(pn, property) == 0) {
            self->m_defaultProps[i].changedSignal = changedSignal;
            return;
        }
    }
    if (self->m_defaultPropCount >= XWIZARD_MAX_DEFAULT_PROPERTIES) return;
    self->m_defaultProps[self->m_defaultPropCount].className =
        XString_create_utf8(className);
    self->m_defaultProps[self->m_defaultPropCount].property =
        XString_create_utf8(property);
    if (!self->m_defaultProps[self->m_defaultPropCount].className ||
        !self->m_defaultProps[self->m_defaultPropCount].property) {
        if (self->m_defaultProps[self->m_defaultPropCount].className)
            XClassDelete(self->m_defaultProps[self->m_defaultPropCount].className);
        if (self->m_defaultProps[self->m_defaultPropCount].property)
            XClassDelete(self->m_defaultProps[self->m_defaultPropCount].property);
        self->m_defaultProps[self->m_defaultPropCount].className = NULL;
        self->m_defaultProps[self->m_defaultPropCount].property = NULL;
        return;
    }
    self->m_defaultProps[self->m_defaultPropCount].changedSignal =
        changedSignal;
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
    XString** slot;
    if (!self || which < 0 || which >= XWizardPixmap_NPixmaps) return;
    slot = &self->m_pixmaps[which];
    if (!*slot) *slot = XString_create();
    if (*slot) {
        if (path)
            XString_assign(*slot, path);
        else
            XString_assign_utf8(*slot, "");
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
    if (tmp) XClassDelete(tmp);
}

const XString* XWizard_pixmap(const XWizard* self, int which)
{
    if (!self || which < 0 || which >= XWizardPixmap_NPixmaps) return NULL;
    return self->m_pixmaps[which];
}

const XString* XWizard_field(const XWizard* self, const XString* name)
{
    XWizardFieldEntry* f;
    const char* key;
    if (!self || !name) return NULL;
    key = XString_toUtf8(name);
    if (!key) return NULL;
    f = xwiz_fieldFind((XWizard*)self, key);
    if (!f) return NULL; /* Qt：未知字段返回无效 QVariant。 */
    /* 有控件绑定时实时取值并镜像进字段表（返回借用指针需稳定存储）。 */
    if (f->widget && f->property) {
        XString scratch;
        const char* cur;
        XString_init(&scratch);
        cur = xwiz_fieldValue(f, &scratch);
        if (f->value) XString_assign_utf8(f->value, cur);
        XClassDeinit(&scratch);
    }
    return f->value;
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
    XClassDelete(tmp);
    return out;
}

void XWizard_setField(XWizard* self, const XString* name,
                      const XString* value)
{
    XWizardFieldEntry* f;
    const char* key;
    if (!self || !name) return;
    key = XString_toUtf8(name);
    if (!key) return;
    f = xwiz_fieldFind(self, key);
    if (!f) {
        /* Qt 6.8 口径（qwizard.cpp:2397-2405 setField / :2433-2441 field）：
         * 未登记字段**告警并忽略**（"No such field"，field() 返回空
         * QVariant）。【2026-11-11 二修】本函数曾短期恢复「自动建项」
         * 以救旧契约测试的 XStrcmp(NULL) 崩——但那与 Qt 相反、且与
         * qt-align 权威锁（XWizardPageAlignTest fieldDelegation：
         * 未登记 setField 空操作、field()==NULL）冲突，按 Qt 复原；
         * 旧契约测试已同步改为 registerField 正规用法。 */
        return;
    }
    xwiz_fieldWrite(f, value ? XString_toUtf8(value) : "");
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
        if (!tv) { if (tn) XClassDelete(tn); return; }
    }
    XWizard_setField(self, tn, tv);
    if (tv) XClassDelete(tv);
    if (tn) XClassDelete(tn);
}

void XWizard_setSideWidget(XWizard* self, XWidget* widget)
{ if (self) self->m_sideWidget = widget; }
XWidget* XWizard_sideWidget(const XWizard* self)
{ return self ? self->m_sideWidget : NULL; }

void XWizard_setButtonLayout(XWizard* self, const int* layout, int count)
{
    int i;
    int j;
    if (!self || !layout || count <= 0 ||
        count > XWIZARD_MAX_BUTTON_LAYOUT)
        return;
    /* 对标 QWizard::setButtonLayout（qwizard.cpp:2658-2684）：序列内
     * 重复按钮（Stretch/NoButton 除外）整份拒绝并告警。 */
    for (i = 0; i < count; ++i) {
        if (layout[i] == (int)XWizardButton_NoButton ||
            layout[i] == (int)XWizardButton_Stretch)
            continue;
        if (layout[i] < 0 || layout[i] >= XWizardButton_NButtons) return;
        for (j = 0; j < i; ++j) {
            if (layout[j] == layout[i]) return; /* Duplicate button in layout */
        }
    }
    for (i = 0; i < count; ++i)
        self->m_buttonLayoutSeq[i] = layout[i];
    self->m_buttonLayoutCount = count;
    self->m_hasCustomLayout = true;
    xwiz_updateButtons(self);
    xwiz_layoutButtons(self);
    XWidget_update((XWidget*)self);
}
void XWizard_setTitleFormat(XWizard* self, int format)
{ if (self) self->m_titleFormat = format; }
int XWizard_titleFormat(const XWizard* self)
{ return self ? self->m_titleFormat : 0; }
void XWizard_setSubTitleFormat(XWizard* self, int format)
{ if (self) self->m_subTitleFormat = format; }
int XWizard_subTitleFormat(const XWizard* self)
{ return self ? self->m_subTitleFormat : 0; }

/* ---- 向导级虚槽分派（对标 QWizard::initializePage(int)/cleanupPage(int)） ---- */

/** @brief 向导级 initializePage 默认实现（对标 QWizard::initializePage，
 *         默认转发到该页的 QWizardPage::initializePage）。 */
static void VXWizard_initializePage(XWizard* self, int id)
{
    XWizardPage* page;
    if (!self) return;
    page = XWizard_page(self, id);
    if (page) XWizardPage_initializePage(page);
}

/** @brief 向导级 cleanupPage 默认实现（对标 QWizard::cleanupPage）。 */
static void VXWizard_cleanupPage(XWizard* self, int id)
{
    XWizardPage* page;
    if (!self) return;
    page = XWizard_page(self, id);
    if (page) XWizardPage_cleanupPage(page);
}

void XWizard_cleanupPage_2(XWizard* self, int id)
{
    if (!self || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXWizard_CleanupPage,
                         void (*)(XWizard*, int))(self, id);
}

void XWizard_initializePage_2(XWizard* self, int id)
{
    if (!self || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXWizard_InitializePage,
                         void (*)(XWizard*, int))(self, id);
}

void XWizard_cleanupPage(XWizard* self)
{
    if (!self) return;
    XWizard_cleanupPage_2(self, xwiz_currentId(self));
}

void XWizard_initializePage(XWizard* self)
{
    if (!self) return;
    XWizard_initializePage_2(self, xwiz_currentId(self));
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
    /* 对标 QWizard::nextId：分派当前页 nextId 虚槽（默认下一页 id）。 */
    return page ? XWizardPage_nextId(page) : -1;
}

void XWizard_done(XWizard* self, int result)
{
    if (!self) return;
    /* 对标 QWizard::done（qwizard.cpp:3218-3230）：
     *   if (result == Rejected) d->reset();
     *   else if (!validateCurrentPage()) return;
     *   QDialog::done(result);
     * `d->reset()`（qwizard.cpp:688-704）的完整语义：
     *   ① 隐藏当前页；
     *   ② 对**历史里每一页**调用 cleanupPage（逆序）；
     *   ③ 清空 history；全部页 initialized=false；**current = -1**；
     *   ④ 发射 currentIdChanged(-1)。
     * 因此取消后 currentId == -1、visitedIds 为空（本机 6.8.3 实测）。
     * r1 实现的"重启回起始页"与之不符（账本 237），已改。 */
    if (result == (int)XDialogCode_Rejected) {
        int i;
        XWizardPage* cur = XWizard_currentPage(self);
        if (cur) XWidget_setVisible((XWidget*)cur, false);
        for (i = (int)self->m_historyCount - 1; i >= 0; --i)
            XWizard_cleanupPage_2(self, self->m_history[i]);
        self->m_historyCount = 0;
        for (i = 0; i < self->m_pageCount; ++i) {
            if (self->m_pages[i]) self->m_pages[i]->m_initialized = false;
            self->m_visited[i] = false;
        }
        if (self->m_currentIndex != -1) {
            self->m_currentIndex = -1;
            xwiz_updateButtons(self);
            xwiz_emitInt(self, (size_t)XWizard_currentIdChanged_signal, -1);
        }
    } else if (result == (int)XDialogCode_Accepted &&
               !XWizard_validateCurrentPage(self)) {
        return;
    }
    XDialog_done((XDialog*)self, result);
}

#endif /* XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */

#endif /* XGUI_ON */
