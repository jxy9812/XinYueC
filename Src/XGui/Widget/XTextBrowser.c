/**
 * @file       XTextBrowser.c
 * @brief      富文本浏览控件实现（对标 Qt 6.8 QTextBrowser 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XTextBrowser.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XPlatformServices.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

/** @brief 历史环形数组容量上限；压满后新条目环形覆盖最老条目。 */
#define XTB_HISTORY_MAX 50

static void xtb_emitStr(XTextBrowser* self, size_t signal, const char* text);
static void xtb_emitBool(XTextBrowser* self, size_t signal, bool v);
static void xtb_emitVoid(XTextBrowser* self, size_t signal);
static void xtb_emitText(XTextBrowser* self, size_t signal, const char* text);
static bool VX_browser_eventFilter(XObject* self, XObject* watched,
                                   XEvent* event);
static void xtb_updateNavigationState(XTextBrowser* self);
static void xtb_setSourceInternal(XTextBrowser* self, const char* url,
                                  bool addHistory);
static bool xtb_history_alloc(XTextBrowser* self);
static XString* xtb_history_entry(const XTextBrowser* self, int logical);
static void xtb_history_set(XTextBrowser* self, int logical, XString* value);
static const XString* xtb_history_relative(const XTextBrowser* self, int index);
static void xtb_linkActivatedForward(XObject* receiver, XVarList* args);
static void xtb_linkHoveredForward(XObject* receiver, XVarList* args);

/**
 * @brief      懒分配历史环形数组（容量 XTB_HISTORY_MAX，槽位清空）。
 * @param      self 目标控件；NULL 时失败。
 * @return     已可用（含此前已分配）返回 true；分配失败返回 false。
 */
static bool xtb_history_alloc(XTextBrowser* self)
{
    int i;
    XString** slots;
    if (!self) return false;
    if (self->m_history) return true;
    slots = (XString**)XMemory_malloc(
        sizeof(XString*) * (size_t)XTB_HISTORY_MAX,
        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!slots) return false;
    for (i = 0; i < XTB_HISTORY_MAX; ++i) slots[i] = NULL;
    self->m_history = slots;
    self->m_historyCapacity = XTB_HISTORY_MAX;
    self->m_historyStart = 0;
    return true;
}

/**
 * @brief      按逻辑下标读取历史条目（环形映射物理槽位）。
 * @param      self 目标控件；可为 NULL。
 * @param      logical 逻辑下标（0 为最老条目，m_historyCount-1 为最新）。
 * @return     有效条目指针；未分配、越界或空槽返回 NULL。
 */
static XString* xtb_history_entry(const XTextBrowser* self, int logical)
{
    if (!self || !self->m_history || self->m_historyCapacity <= 0 ||
        logical < 0 || logical >= self->m_historyCount)
        return NULL;
    return self->m_history
        [(self->m_historyStart + logical) % self->m_historyCapacity];
}

/**
 * @brief      按逻辑下标写入历史槽位（环形映射物理槽位）。
 * @param      self 目标控件；可为 NULL。
 * @param      logical 逻辑下标；调用方须保证 0 <= logical < 容量。
 * @param      value 待写入的 XString 指针（对象所有权转移给历史栈）。
 * @return     无返回值；越界或未分配时静默忽略（value 由调用方处置）。
 */
static void xtb_history_set(XTextBrowser* self, int logical, XString* value)
{
    if (!self || !self->m_history || self->m_historyCapacity <= 0 ||
        logical < 0 || logical >= self->m_historyCapacity)
        return;
    self->m_history
        [(self->m_historyStart + logical) % self->m_historyCapacity] = value;
}

/**
 * @brief      按相对偏移解析历史条目（对标 Qt history(i) 语义）。
 * @param      self 目标控件；可为 NULL。
 * @param      index 相对当前条目的偏移：0 当前、负后退、正前进。
 * @return     有效条目指针；无历史或越界返回 NULL。
 */
static const XString* xtb_history_relative(const XTextBrowser* self, int index)
{
    int logical;
    if (!self || self->m_historyIndex < 0) return NULL;
    logical = self->m_historyIndex + index;
    if (logical < 0 || logical >= self->m_historyCount) return NULL;
    return xtb_history_entry(self, logical);
}


/** @brief argsDel 回调：释放 XString 载荷（对齐 XObject 信号惯例）。 */
static void xtb_str_args_del(XVarList* list)
{
    XVarList_args_1(list, XString*, val);
    if (val) XString_delete_base((XClass*)val);
}

static void xtb_emitStr(XTextBrowser* self, size_t signal, const char* text)
{
    XVarList* args;
    XString* val;
    if (!self) return;
    val = XString_create_utf8(text ? text : "");
    if (!val) return;
    args = XVarList_Create(XVar(XString*, val));
    if (!args) { XString_delete_base((XClass*)val); return; }
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args,
                           xtb_str_args_del, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_setArgsDel(args, xtb_str_args_del);
        XVarList_delete(args);
    }
}

static void xtb_emitBool(XTextBrowser* self, size_t signal, bool v)
{
    XVarList* args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xtb_emitVoid(XTextBrowser* self, size_t signal)
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

/**
 * @brief      按当前历史索引重算后退/前进可用性并只在变化时发信号。
 * @details    对标 QTextBrowser 内部 navigation 面板状态刷新：状态翻转
 *             时分别发射 backwardAvailable(bool)/forwardAvailable(bool)。
 * @param      self 目标控件；NULL 时无操作。
 * @return     无返回值。
 */
static void xtb_updateNavigationState(XTextBrowser* self)
{
    bool back, forward;

    if (!self) return;
    back = (self->m_historyIndex > 0);
    forward = (self->m_historyIndex < self->m_historyCount - 1);
    if (back != self->m_backwardAvailable) {
        self->m_backwardAvailable = back;
        xtb_emitBool(self, (size_t)XTextBrowser_backwardAvailable_signal, back);
    }
    if (forward != self->m_forwardAvailable) {
        self->m_forwardAvailable = forward;
        xtb_emitBool(self, (size_t)XTextBrowser_forwardAvailable_signal, forward);
    }
}

static void VXTextBrowser_deinit(XTextBrowser* self)
{
    int i;
    if (!self) return;
    /* 先解挂编辑器事件过滤器并释放悬停锚点（编辑器随基类析构）。 */
    if (self->m_base.m_editor)
        XObject_removeEventFilter((XObject*)self->m_base.m_editor,
                                  (XObject*)self);
    if (self->m_hoverAnchor) {
        XString_delete_base(self->m_hoverAnchor);
        self->m_hoverAnchor = NULL;
    }
    if (self->m_source) {
        XString_delete_base((XClass*)self->m_source);
        self->m_source = NULL;
    }
    /* 历史环形数组：按逻辑下标遍历释放全部条目后释放槽位数组。 */
    for (i = 0; i < self->m_historyCount; ++i) {
        XString* entry = xtb_history_entry(self, i);
        if (entry) XString_delete_base((XClass*)entry);
    }
    if (self->m_history) {
        XFree_System(self->m_history);
        self->m_history = NULL;
    }
    self->m_historyCount = 0;
    self->m_historyIndex = -1;
    self->m_historyCapacity = 0;
    self->m_historyStart = 0;
    if (self->m_searchPaths) {
        XStringList_delete_base((XClass*)self->m_searchPaths);
        self->m_searchPaths = NULL;
    }
    XClass_Deinit_Parent(XTextEdit, (XTextEdit*)self);
}

/* ==================== 链接交互（事件过滤器挂内嵌编辑器） ==================== */

/** @brief 发射 const char* 载荷信号（anchorClicked/highlighted 共用）。 */
static void xtb_emitText(XTextBrowser* self, size_t signal, const char* text)
{
    XVarList* args;
    if (!text) text = "";
    if (!self || !((XObject*)self)->m_signalSlot) return;
    args = XVarList_Create(XVar(const char*, text));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 基类 linkActivated → 浏览器导航语义转发槽（预览态链接路径）。
 * @details 对标 QTextBrowser 链接点击链路：先发 anchorClicked(url)；
 *          openExternalLinks 开启时交平台服务按桌面方式打开，否则在
 *          openLinks 默认语义下以该 URL 触发 setSource 导航（与编辑器
 *          事件过滤器路径 VX_browser_eventFilter 同一套决策）。 */
static void xtb_linkActivatedForward(XObject* receiver, XVarList* args)
{
    XTextBrowser* browser = (XTextBrowser*)receiver;
    XVarList_args_1(args, XString*, link);
    const char* url;
    if (!browser || !args) return;
    url = (link && XString_toUtf8(link)) ? XString_toUtf8(link) : "";
    if (!url[0]) return;
    xtb_emitText(browser, (size_t)XTextBrowser_anchorClicked_signal, url);
    if (browser->m_openExternalLinks) {
        XPlatformServices* svc = XPlatformServices_create();
        if (svc) {
            XPlatformServices_openUrl_2(svc, url);
            XClass_delete_base((XClass*)svc);
        }
    } else if (browser->m_openLinks) {
        XTextBrowser_setSource(browser, url);
    }
}

/** @brief 基类 linkHovered → highlighted 转发槽（离开链接载荷空串）。 */
static void xtb_linkHoveredForward(XObject* receiver, XVarList* args)
{
    XTextBrowser* browser = (XTextBrowser*)receiver;
    XVarList_args_1(args, XString*, link);
    const char* url;
    if (!browser || !args) return;
    url = (link && XString_toUtf8(link)) ? XString_toUtf8(link) : "";
    xtb_emitText(browser, (size_t)XTextBrowser_highlighted_signal, url);
}

/** @brief 编辑器事件过滤器：链接按下→anchorClicked（+导航/外链开关），
 *         鼠标移动→进出链接发射 highlighted（对标 QTextBrowser）。
 * @note   编辑器局部坐标即浏览器局部坐标（编辑器常驻 (0,0) 铺满）；
 *         恒返回 false 不过滤，编辑器照常处理光标/选择。 */
static bool VX_browser_eventFilter(XObject* self, XObject* watched,
                                   XEvent* event)
{
    XTextBrowser* browser = (XTextBrowser*)self;
    if (!browser || !watched || !event) return false;
    if (watched != (XObject*)browser->m_base.m_editor) return false;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XMouseEvent* me = (XMouseEvent*)event;
        XString* anchor;
        const char* url;
        if (!browser->m_openLinks) return false;
        anchor = XTextBrowser_anchorAt(browser, &me->m_position);
        if (!anchor) return false;
        url = XString_toUtf8(anchor);
        if (url && url[0]) {
            xtb_emitText(browser,
                         (size_t)XTextBrowser_anchorClicked_signal(browser,
                                                                   url),
                         url);
            if (browser->m_openExternalLinks) {
                /* 外链开关：交平台服务按桌面方式打开（不作为源导航）。 */
                XPlatformServices* svc = XPlatformServices_create();
                if (svc) {
                    XPlatformServices_openUrl_2(svc, url);
                    XClass_delete_base((XClass*)svc);
                }
            } else {
                /* 对标 openLinks 默认：链接作为浏览源触发导航。 */
                XTextBrowser_setSource(browser, url);
            }
        }
        XString_delete_base(anchor);
    } else if (XEvent_type(event) == XEVENT_TYPE_MOUSE_MOVE) {
        /* 悬停高亮：进入/离开/切换链接（URL 变化）时发射 highlighted；
           离开时载荷为空串（Qt 语义）。 */
        XMouseEvent* me = (XMouseEvent*)event;
        XString* anchor = XTextBrowser_anchorAt(browser, &me->m_position);
        const char* url = anchor ? XString_toUtf8(anchor) : NULL;
        const char* hover = browser->m_hoverAnchor
                                ? XString_toUtf8(browser->m_hoverAnchor)
                                : NULL;
        bool hasUrl = (url && url[0]);
        bool hasHover = (hover && hover[0]);
        bool changed = (hasUrl != hasHover) ||
                       (hasUrl && hasHover && XStrcmp(url, hover) != 0);
        if (changed) {
            if (browser->m_hoverAnchor) {
                XString_delete_base(browser->m_hoverAnchor);
                browser->m_hoverAnchor = NULL;
            }
            if (hasUrl)
                browser->m_hoverAnchor = XString_create_utf8(url);
            xtb_emitText(browser,
                         (size_t)XTextBrowser_highlighted_signal(browser,
                                                                 url),
                         hasUrl ? url : "");
        }
        if (anchor) XString_delete_base(anchor);
    }
    return false;
}

XVtable* XTextBrowser_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextBrowser)
    XVTABLE_INHERIT_XCLASS(XTextEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextBrowser_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, VX_browser_eventFilter);
    return XVTABLE_DEFAULT;
}

void XTextBrowser_init(XTextBrowser* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTextEdit_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTextBrowser);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XPlainTextEdit_setReadOnly(self->m_base.m_editor, true);
#if XTEXTDOCUMENT_ON
    /* XTextEdit_init 已创建 m_textDoc：覆盖前释放旧对象（否则泄漏）。 */
    if (self->m_base.m_textDoc) {
        XClass_delete_base((XClass*)self->m_base.m_textDoc);
        self->m_base.m_textDoc = NULL;
    }
    self->m_base.m_textDoc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
#endif
    self->m_historyCount = 0;
    self->m_historyIndex = -1;
    self->m_historyCapacity = 0;
    self->m_historyStart = 0;
    self->m_source = XString_create();
    self->m_backwardAvailable = false;
    self->m_forwardAvailable = false;
    self->m_openExternalLinks = false;
    /* 编辑器填满浏览器。 */
    XWidget_setGeometry((XWidget*)self->m_base.m_editor, 0, 0,
                        XWidget_width((XWidget*)self),
                        XWidget_height((XWidget*)self));
    /* 编辑器显式 show（对标 QTextBrowser 的正文视图恒可见语义；
     * XTextEdit 编辑态约定「正文由内嵌编辑器子控件绘制」，壳的
     * paintEvent 只铺白底——若编辑器停留在隐藏态，整页只剩白底，
     * 即台账 #49 全白症状的一类根因；已可见时 show 为幂等 no-op）。 */
    XWidget_show((XWidget*)self->m_base.m_editor);
    self->m_openLinks = true;
    /* 链接交互：以过滤器监视编辑器鼠标事件（按下→anchorClicked、
     * 移动→highlighted；见 VX_browser_eventFilter）。 */
    XObject_installEventFilter((XObject*)self->m_base.m_editor,
                               (XObject*)self);
    /* 只读富文本预览路径：基类 XTextEdit 链接信号（预览态壳鼠标处理
     * 发射）→ 浏览器导航语义转发（anchorClicked/openLinks/openExternal
     * Links 与 highlighted）。编辑器隐藏后过滤器路径自然静默，无重复
     * 发射。 */
    XObject_connect_1((XObject*)self,
                      (size_t)XTextEdit_linkActivated_signal,
                      (XObject*)self, xtb_linkActivatedForward,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)self,
                      (size_t)XTextEdit_linkHovered_signal,
                      (XObject*)self, xtb_linkHoveredForward,
                      XConnectionType_Direct);
}

XTextBrowser* XTextBrowser_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XTextBrowser* self = (XTextBrowser*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextBrowser_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XTextBrowser_setSource(XTextBrowser* self, const char* url)
{
    xtb_setSourceInternal(self, url, true);
}

const char* XTextBrowser_source(const XTextBrowser* self)
{
    const char* text;
    if (!self || !self->m_source) return "";
    text = XString_toUtf8(self->m_source);
    return text ? text : "";
}

XTextBrowserSourceType XTextBrowser_sourceType(const XTextBrowser* self)
{
    /* 对标 QTextBrowser::sourceType：Qt 按导航栈顶条目记录的资源类型
       返回，栈空（d->stack.isEmpty()）返回 UnknownResource；本版第一版
       不做 HTML/Markdown 渲染，setSource 恒以 URL 文本承载源，故有
       导航条目时恒返回 Url（0=Url），不按扩展名推断类型。 */
    if (!self || self->m_historyCount <= 0 || self->m_historyIndex < 0)
        return XTextBrowserSourceType_Unknown;
    return XTextBrowserSourceType_Url;
}

void XTextBrowser_setOpenLinks(XTextBrowser* self, bool open)
{
    if (!self) return;
    self->m_openLinks = open;
}

void XTextBrowser_setHtml(XTextBrowser* self, const char* html)
{
    if (!self) return;
    /* 对标 QTextBrowser（继承 QTextEdit）::setHtml：以渲染子集解析并
     * 进入只读富文本预览（浏览器场景默认呈现富文本）；浏览器编辑器恒
     * 只读。链接点击/悬停经基类链接信号转发为 anchorClicked/
     * highlighted（见 xtb_linkActivatedForward）。 */
    XTextEdit_setHtml(&self->m_base, html);
    if (self->m_base.m_editor)
        XPlainTextEdit_setReadOnly(self->m_base.m_editor, true);
    XTextEdit_setRichPreview(&self->m_base, true);
}

bool XTextBrowser_openLinks(const XTextBrowser* self)
{
    return self ? self->m_openLinks : false;
}

/**
 * @brief      设置源并可选追加导航历史的内部实现。
 * @param      self 目标控件；NULL 时无操作。
 * @param      url 新源 URL；NULL 视为空串。
 * @param      addHistory true 追加一条历史（用户导航），false 只在历史内
 *             移动（backward/forward/home），对标 QTextBrowser 语义。
 * @return     无返回值。
 * @note       追加历史时先截断前进分支，再写入容量上限 XTB_HISTORY_MAX
 *             （50）的环形数组；压满后新条目环形覆盖最老条目，当前索引
 *             相应左移，保证最老可后退条目始终不被当前索引越过。
 */
static void xtb_setSourceInternal(XTextBrowser* self, const char* url,
                                  bool addHistory)
{
    XString* copy;
    int li;

    if (!self) return;
    if (!self->m_source) self->m_source = XString_create();
    if (self->m_source)
        XString_assign_utf8(self->m_source, url ? url : "");
    xtb_emitStr(self, (size_t)XTextBrowser_sourceChanged_signal,
                XString_toUtf8(self->m_source));
    if (!addHistory) {
        xtb_updateNavigationState(self);
        return;
    }
    /* 新导航清掉 forward 历史（对标 Qt：重新访问旧地址时
       forward 条目被截断并释放，避免环形覆盖泄漏）。 */
    for (li = self->m_historyIndex + 1; li < self->m_historyCount; ++li) {
        XString* stale = xtb_history_entry(self, li);
        if (stale) XString_delete_base((XClass*)stale);
        xtb_history_set(self, li, NULL);
    }
    if (self->m_historyCount > self->m_historyIndex + 1)
        self->m_historyCount = self->m_historyIndex + 1;
    copy = XString_create_copy(self->m_source);
    if (!copy) {
        xtb_updateNavigationState(self);
        return;
    }
    if (!xtb_history_alloc(self)) {
        XString_delete_base((XClass*)copy);
        xtb_updateNavigationState(self);
        return;
    }
    /* 容量已满：环形覆盖最老条目（逻辑第 0 条）。 */
    if (self->m_historyCount >= self->m_historyCapacity) {
        XString* oldest = xtb_history_entry(self, 0);
        if (oldest) XString_delete_base((XClass*)oldest);
        xtb_history_set(self, 0, NULL);
        self->m_historyStart =
            (self->m_historyStart + 1) % self->m_historyCapacity;
        self->m_historyCount--;
        if (self->m_historyIndex > 0) self->m_historyIndex--;
    }
    xtb_history_set(self, self->m_historyCount, copy);
    self->m_historyIndex = self->m_historyCount;
    self->m_historyCount++;
    xtb_emitVoid(self, (size_t)XTextBrowser_historyChanged_signal);
    xtb_updateNavigationState(self);
}

void XTextBrowser_backward(XTextBrowser* self)
{
    XString* entry;
    if (!self || self->m_historyIndex <= 0) return;
    entry = xtb_history_entry(self, self->m_historyIndex - 1);
    if (!entry) return;
    self->m_historyIndex--;
    xtb_setSourceInternal(self, XString_toUtf8(entry), false);
}
void XTextBrowser_forward(XTextBrowser* self)
{
    XString* entry;
    if (!self || self->m_historyIndex >= self->m_historyCount - 1) return;
    entry = xtb_history_entry(self, self->m_historyIndex + 1);
    if (!entry) return;
    self->m_historyIndex++;
    xtb_setSourceInternal(self, XString_toUtf8(entry), false);
}
void XTextBrowser_home(XTextBrowser* self)
{
    XString* entry;
    if (!self || self->m_historyCount <= 0) return;
    entry = xtb_history_entry(self, 0);
    if (!entry) return;
    self->m_historyIndex = 0;
    xtb_setSourceInternal(self, XString_toUtf8(entry), false);
}
void XTextBrowser_reload(XTextBrowser* self)
{
    if (!self) return;
    /* 修正：此前误将 XString* 直接当 const char* 传入。 */
    xtb_emitStr(self, (size_t)XTextBrowser_sourceChanged_signal,
                self->m_source ? XString_toUtf8(self->m_source) : "");
}

void* XTextBrowser_sourceChanged_signal(XTextBrowser* self, const char* url)
{
    XVarList* args;
    XString* val;

    if (!self)
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    val = XString_create_utf8(url ? url : "");
    if (!val)
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    args = XVarList_Create(XVar(XString*, val));
    if (!args) {
        XString_delete_base((XClass*)val);
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    }
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self,
                           (size_t)XTextBrowser_sourceChanged_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
    return (void*)(size_t)XTextBrowser_sourceChanged_signal;
}

void* XTextBrowser_backwardAvailable_signal(XTextBrowser* self, bool available)
{
    if (!self)
        return (void*)(size_t)XTextBrowser_backwardAvailable_signal;
    xtb_emitBool(self, (size_t)XTextBrowser_backwardAvailable_signal, available);
    return (void*)(size_t)XTextBrowser_backwardAvailable_signal;
}

void* XTextBrowser_forwardAvailable_signal(XTextBrowser* self, bool available)
{
    if (!self)
        return (void*)(size_t)XTextBrowser_forwardAvailable_signal;
    xtb_emitBool(self, (size_t)XTextBrowser_forwardAvailable_signal, available);
    return (void*)(size_t)XTextBrowser_forwardAvailable_signal;
}

void* XTextBrowser_historyChanged_signal(XTextBrowser* self)
{
    if (!self)
        return (void*)(size_t)XTextBrowser_historyChanged_signal;
    xtb_emitVoid(self, (size_t)XTextBrowser_historyChanged_signal);
    return (void*)(size_t)XTextBrowser_historyChanged_signal;
}





/**
 * @brief      清空导航历史（对标 QTextBrowser::clearHistory）。
 * @details    对标 Qt 6.8 语义：释放全部前进/后退历史条目，但保留当前
 *             条目（清空后历史栈仅剩当前 1 条，逻辑第 0 条），随后发射
 *             historyChanged；后退/前进可用状态仅在翻转时发射对应信号
 *             （等效 Qt 的无条件发射，避免重复的同值通知）。
 * @param      self 目标控件；NULL 时无操作。
 * @return     无返回值。
 * @note       历史环形数组本身保留（容量 XTB_HISTORY_MAX=50），仅清空
 *             条目；未分配过历史数组（从未 setSource）时同样发通知。
 */
void XTextBrowser_clearHistory(XTextBrowser* self)
{
    int li;
    int keepLogical;
    if (!self) return;
    if (self->m_history && self->m_historyCapacity > 0 &&
        self->m_historyIndex >= 0 && self->m_historyCount > 0) {
        keepLogical = self->m_historyIndex;
        for (li = 0; li < self->m_historyCount; ++li) {
            XString* stale;
            if (li == keepLogical) continue;
            stale = xtb_history_entry(self, li);
            if (stale) XString_delete_base((XClass*)stale);
            xtb_history_set(self, li, NULL);
        }
        /* 重设环形起点，使保留的当前条目成为逻辑第 0 条。 */
        self->m_historyStart =
            (self->m_historyStart + keepLogical) % self->m_historyCapacity;
        self->m_historyCount = 1;
        self->m_historyIndex = 0;
    }
    xtb_emitVoid(self, (size_t)XTextBrowser_historyChanged_signal);
    xtb_updateNavigationState(self);
}

/**
 * @brief      当前条目之前（可后退）的历史条数（对标
 *             QTextBrowser::backwardHistoryCount）。
 * @param      self 目标控件；NULL 时返回 0。
 * @return     当前逻辑索引即后退条数；已在最老条目或无历史时返回 0。
 */
int XTextBrowser_backwardHistoryCount(const XTextBrowser* self)
{
    return (self && self->m_historyIndex > 0) ? self->m_historyIndex : 0;
}

/**
 * @brief      当前条目之后（可前进）的历史条数（对标
 *             QTextBrowser::forwardHistoryCount）。
 * @param      self 目标控件；NULL 时返回 0。
 * @return     总条数减当前索引减一；无前进历史时返回 0。
 */
int XTextBrowser_forwardHistoryCount(const XTextBrowser* self)
{
    if (!self || self->m_historyIndex < 0)
        return 0;
    return self->m_historyCount - self->m_historyIndex - 1;
}

/**
 * @brief      按相对偏移取历史项源 URL（对标 QTextBrowser::historyUrl）。
 * @param      self 目标控件；可为 NULL。
 * @param      index 相对当前条目的偏移（项目简化语义，对齐 Qt：0 为
 *             当前条目，-1 为上一条，+1 为下一条，依此类推）。
 * @return     对应条目 URL 的内部缓存指针（条目变化前有效）；无历史、
 *             越界或转换失败时返回空串。
 */
const char* XTextBrowser_historyUrl(const XTextBrowser* self, int index)
{
    const XString* entry = xtb_history_relative(self, index);
    const char* url;
    if (!entry) return "";
    url = XString_toUtf8(entry);
    return url ? url : "";
}

/**
 * @brief      按相对偏移取历史项标题（对标 QTextBrowser::historyTitle）。
 * @param      self 目标控件；可为 NULL。
 * @param      index 语义同 XTextBrowser_historyUrl。
 * @return     对应条目标题；无历史或越界时返回空串。
 * @note       项目简化：第一版不解析文档，历史条目未记录文档标题，
 *             标题返回对应条目的源 URL 字符串。
 */
const char* XTextBrowser_historyTitle(const XTextBrowser* self, int index)
{
    return XTextBrowser_historyUrl(self, index);
}

/**
 * @brief      是否可后退（对标 QTextBrowser::isBackwardAvailable）。
 * @details    实时按历史栈计算（后退条数 > 0），与 backwardAvailable
 *             信号的最新发射状态一致。
 * @param      self 目标控件；NULL 时返回 false。
 * @return     可后退返回 true。
 */
bool XTextBrowser_isBackwardAvailable(const XTextBrowser* self)
{
    return XTextBrowser_backwardHistoryCount(self) > 0;
}

/**
 * @brief      是否可前进（对标 QTextBrowser::isForwardAvailable）。
 * @details    实时按历史栈计算（前进条数 > 0），与 forwardAvailable
 *             信号的最新发射状态一致。
 * @param      self 目标控件；NULL 时返回 false。
 * @return     可前进返回 true。
 */
bool XTextBrowser_isForwardAvailable(const XTextBrowser* self)
{
    return XTextBrowser_forwardHistoryCount(self) > 0;
}

/**
 * @brief      设置外部链接是否自动打开（对标
 *             QTextBrowser::setOpenExternalLinks）。
 * @details    开启后外部链接交由桌面打开而不只发 anchorClicked；
 *             本版本仅记录开关，供链接点击接线（Task 2.3）使用。
 * @param      self 目标控件；NULL 时无操作。
 * @param      open true 自动打开外链；默认 false。
 * @return     无返回值。
 */
void XTextBrowser_setOpenExternalLinks(XTextBrowser* self, bool open)
{
    if (!self) return;
    self->m_openExternalLinks = open;
}

/**
 * @brief      获取外部链接自动打开状态（对标
 *             QTextBrowser::openExternalLinks）。
 * @param      self 目标控件；NULL 时返回 false。
 * @return     外链自动打开返回 true（默认 false）。
 */
bool XTextBrowser_openExternalLinks(const XTextBrowser* self)
{
    return self ? self->m_openExternalLinks : false;
}

/**
 * @brief      设置资源搜索路径列表（对标
 *             QTextBrowser::setSearchPaths）。
 * @details    对 paths 做深拷贝保存，调用后调用方可自行释放源列表；
 *             NULL 表示清空路径。
 * @param      self 目标控件；NULL 时无操作。
 * @param      paths 源路径列表；可为 NULL。
 * @return     无返回值。
 * @note       深拷贝失败时保留原列表不变，避免丢数据。
 */
void XTextBrowser_setSearchPaths(XTextBrowser* self, const XStringList* paths)
{
    XStringList* copy;
    if (!self) return;
    copy = paths ? XStringList_create_copy(paths) : NULL;
    if (paths && !copy) return;
    if (self->m_searchPaths) {
        XStringList_delete_base((XClass*)self->m_searchPaths);
        self->m_searchPaths = NULL;
    }
    self->m_searchPaths = copy;
}

/**
 * @brief      获取资源搜索路径列表（对标 QTextBrowser::searchPaths）。
 * @param      self 目标控件；可为 NULL。
 * @return     新建的深拷贝 XStringList*，由调用方以
 *             XStringList_delete_base 释放；内部为空（从未设置）时返回
 *             新建空列表，分配失败返回 NULL。
 */
XStringList* XTextBrowser_searchPaths(const XTextBrowser* self)
{
    if (!self || !self->m_searchPaths)
        return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return XStringList_create_copy(self->m_searchPaths);
}

/**
 * @brief      查询当前光标竖线矩形（对标 QTextBrowser::cursorRect）。
 * @details    委托内嵌编辑器 XPlainTextEdit_cursorRect（与
 *             XTextEdit_cursorRect 同一套口径）：内嵌编辑器常驻 (0,0)
 *             并铺满本控件，其局部坐标即本控件局部坐标；NULL 逐层兜底
 *             返回零矩形。
 * @param      self 目标控件指针；NULL 或内嵌编辑器缺失时返回零矩形。
 * @return     光标矩形（本控件局部坐标）。
 */
XRect XTextBrowser_cursorRect(const XTextBrowser* self)
{
    /* 与内嵌编辑器 paintEvent 同口径：编辑器常驻 (0,0) 铺满本控件，
       其局部坐标即本控件局部坐标；NULL 逐层兜底返回零矩形。 */
    return XPlainTextEdit_cursorRect(self ? self->m_base.m_editor : NULL);
}

/**
 * @brief      返回坐标 pos 处的超链接锚点（对标 QTextBrowser::anchorAt）。
 * @details    委托 XTextEdit_anchorAt：以富文本文档（XTextDocument 片段
 *             fmt.anchorHref）为承载、与富绘制路径同口径几何命中；片段
 *             无锚点或块带未命中返回 0 长度字符串对象。
 * @note       返回值为堆上新建的 XString*（空串对象或锚点文本），由
 *             调用方以 XString_delete_base 释放；内存分配失败返回 NULL。
 * @param      self 目标控件指针；可为 NULL。
 * @param      pos 控件局部坐标点；可为 NULL，不被使用。
 * @return     堆上新建的 XString*；语义见 @note。
 */
XString* XTextBrowser_anchorAt(const XTextBrowser* self, const XPoint* pos)
{
    /* 浏览器 IS-A XTextEdit（C 上转型同地址）：富文档锚点命中以基类
     * 同一实现承载（内嵌纯文本编辑器无锚点概念，不再委托）。 */
    return XTextEdit_anchorAt((const XTextEdit*)self, pos);
}

void* XTextBrowser_anchorClicked_signal(XTextBrowser* self, const char* url)
{
    /* 标识入口：真实发射经 xtb_emitText（预览态基类 linkActivated 转
       发路径 xtb_linkActivatedForward，或编辑器事件过滤器路径）。 */
    (void)self; (void)url;
    return (void*)(size_t)XTextBrowser_anchorClicked_signal;
}

void* XTextBrowser_highlighted_signal(XTextBrowser* self, const char* url)
{
    /* 标识入口：真实发射经 xtb_emitText（预览态基类 linkHovered 转发
       路径 xtb_linkHoveredForward，离开链接载荷为空串）。 */
    (void)self; (void)url;
    return (void*)(size_t)XTextBrowser_highlighted_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON */