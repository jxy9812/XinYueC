/**
 * @file       XTextBrowser.c
 * @brief      富文本浏览控件实现（对标 Qt 6.8 QTextBrowser 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XTextBrowser.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XPlatformServices.h"
/* loadResource 的文件读取走框架 XDeviceFile（open/read/close）。 */
#if XDEVICE_ON && XDEVICE_FILE_ON
#include "XDeviceFile.h"
#endif

#include "XAlgorithm.h"
#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

/** @brief 历史环形数组容量上限；压满后新条目环形覆盖最老条目。 */
#define XTB_HISTORY_MAX 50

/* 资源类型数值（与 XTDResourceType 逐项同值；本模块不依赖
 * XTEXTDOCUMENT_ON，故用本地字面量，见 XTextBrowserSourceType 注释）。 */
#define XTB_RES_UNKNOWN   0  /**< UnknownResource。 */
#define XTB_RES_HTML      1  /**< HtmlResource。 */
#define XTB_RES_MARKDOWN  4  /**< MarkdownResource。 */

static void xtb_emitStr(XTextBrowser* self, size_t signal, const char* text);
static void xtb_emitBool(XTextBrowser* self, size_t signal, bool v);
static void xtb_emitVoid(XTextBrowser* self, size_t signal);
static void xtb_emitText(XTextBrowser* self, size_t signal, const char* text);
static bool VX_browser_eventFilter(XObject* self, XObject* watched,
                                   XEvent* event);
static void xtb_updateNavigationState(XTextBrowser* self);
static void xtb_setSourceInternal(XTextBrowser* self, const char* url,
                                  int type, bool addHistory);
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
    int* types;
    if (!self) return false;
    if (self->m_history) return true;
    slots = (XString**)XMemory_malloc(
        sizeof(XString*) * (size_t)XTB_HISTORY_MAX,
        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!slots) return false;
    types = (int*)XMemory_malloc(sizeof(int) * (size_t)XTB_HISTORY_MAX,
                                 XCLASS_DEFAULT_MEMORY_TYPE);
    if (!types) {
        XFree_System(slots);
        return false;
    }
    for (i = 0; i < XTB_HISTORY_MAX; ++i) {
        slots[i] = NULL;
        types[i] = 0; /* UnknownResource */
    }
    self->m_history = slots;
    self->m_historyTypes = types;
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

/** @brief 读取某逻辑下标条目的资源类型（未分配/越界按 UnknownResource）。 */
static int xtb_history_type(const XTextBrowser* self, int logical)
{
    if (!self || !self->m_historyTypes || self->m_historyCapacity <= 0 ||
        logical < 0 || logical >= self->m_historyCapacity)
        return (int)XTB_RES_UNKNOWN;
    return self->m_historyTypes
        [(self->m_historyStart + logical) % self->m_historyCapacity];
}

/** @brief 写入某逻辑下标的资源类型（与 xtb_history_set 同环形映射）。 */
static void xtb_history_setType(XTextBrowser* self, int logical, int type)
{
    if (!self || !self->m_historyTypes || self->m_historyCapacity <= 0 ||
        logical < 0 || logical >= self->m_historyCapacity)
        return;
    self->m_historyTypes
        [(self->m_historyStart + logical) % self->m_historyCapacity] = type;
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
    if (val) XClassDelete((XClass*)val);
}

static void xtb_emitStr(XTextBrowser* self, size_t signal, const char* text)
{
    XVarList* args;
    XString* val;
    if (!self) return;
    val = XString_create_utf8(text ? text : "");
    if (!val) return;
    args = XVarList_Create(XVar(XString*, val));
    if (!args) { XClassDelete((XClass*)val); return; }
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
        XClassDelete(self->m_hoverAnchor);
        self->m_hoverAnchor = NULL;
    }
    if (self->m_source) {
        XClassDelete((XClass*)self->m_source);
        self->m_source = NULL;
    }
    /* 历史环形数组：按逻辑下标遍历释放全部条目后释放槽位数组。 */
    for (i = 0; i < self->m_historyCount; ++i) {
        XString* entry = xtb_history_entry(self, i);
        if (entry) XClassDelete((XClass*)entry);
    }
    if (self->m_history) {
        XFree_System(self->m_history);
        self->m_history = NULL;
    }
    if (self->m_historyTypes) {
        XFree_System(self->m_historyTypes);
        self->m_historyTypes = NULL;
    }
    self->m_historyCount = 0;
    self->m_historyIndex = -1;
    self->m_historyCapacity = 0;
    self->m_historyStart = 0;
    if (self->m_searchPaths) {
        XClassDelete((XClass*)self->m_searchPaths);
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
 * @details 对标 QTextBrowserPrivate::activateAnchor（qtextbrowser.cpp:
 *          202-213）：恒先发 anchorClicked(url)（openLinks=false 只发不
 *          导航，anchorClicked 与导航解耦）；openLinks 开启时才按
 *          openExternalLinks 外开 / setSource 导航分派（与编辑器事件
 *          过滤器路径 VX_browser_eventFilter 同一套决策，两路一致）。 */
static void xtb_linkActivatedForward(XObject* receiver, XVarList* args)
{
    XTextBrowser* browser = (XTextBrowser*)receiver;
    XVarList_args_1(args, XString*, link);
    const char* url;
    if (!browser || !args) return;
    url = (link && XString_toUtf8(link)) ? XString_toUtf8(link) : "";
    if (!url[0]) return;
    xtb_emitText(browser, (size_t)XTextBrowser_anchorClicked_signal, url);
    if (!browser->m_openLinks) return; /* Qt：!openLinks 发完即返回。 */
    if (browser->m_openExternalLinks) {
        XPlatformServices* svc = XPlatformServices_create();
        if (svc) {
            XPlatformServices_openUrl_2(svc, url);
            /* 非 XClass 裸结构体：XClassDelete 读越界堆标记后不释放 ⇒
               泄漏；用 create 配对的 XPlatformServices_destroy。 */
            XPlatformServices_destroy(svc);
        }
    } else {
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
        anchor = XTextEdit_anchorAt((const XTextEdit*)browser,
                                    &me->m_position);
        if (!anchor) return false;
        url = XString_toUtf8(anchor);
        if (url && url[0]) {
            /* 对标 QTextBrowserPrivate::activateAnchor（qtextbrowser.cpp:
             * 202-213）：anchorClicked 与导航解耦——openLinks=false 时仍
             * 发射 anchorClicked 后返回，不丢弃观察点。 */
            xtb_emitText(browser,
                         (size_t)XTextBrowser_anchorClicked_signal(browser,
                                                                   url),
                         url);
            if (browser->m_openLinks) {
                if (browser->m_openExternalLinks) {
                    /* 外链开关：交平台服务按桌面方式打开（不作为源导航）。 */
                    XPlatformServices* svc = XPlatformServices_create();
                    if (svc) {
                        XPlatformServices_openUrl_2(svc, url);
                        /* 非 XClass 裸结构体：必须用配对 destroy（同上）。 */
                        XPlatformServices_destroy(svc);
                    }
                } else {
                    /* 对标 openLinks 默认：链接作为浏览源触发导航。 */
                    XTextBrowser_setSource(browser, url);
                }
            }
        }
        XClassDelete(anchor);
    } else if (XEvent_type(event) == XEVENT_TYPE_MOUSE_MOVE) {
        /* 悬停高亮：进入/离开/切换链接（URL 变化）时发射 highlighted；
           离开时载荷为空串（Qt 语义）。 */
        XMouseEvent* me = (XMouseEvent*)event;
        XString* anchor = XTextEdit_anchorAt((const XTextEdit*)browser,
                                             &me->m_position);
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
                XClassDelete(browser->m_hoverAnchor);
                browser->m_hoverAnchor = NULL;
            }
            if (hasUrl)
                browser->m_hoverAnchor = XString_create_utf8(url);
            xtb_emitText(browser,
                         (size_t)XTextBrowser_highlighted_signal(browser,
                                                                 url),
                         hasUrl ? url : "");
        }
        if (anchor) XClassDelete(anchor);
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
    /* 对标 QTextBrowserPrivate::init（qtextbrowser.cpp:669）：浏览器
       恒只读浏览语义——撤销重做关闭、交互标志 TextBrowserInteraction
       （鼠标可选 + 链接鼠标/键盘可达，不含可编辑）。 */
    XTextEdit_setUndoRedoEnabled(&self->m_base, false);
    XTextEdit_setTextInteractionFlags(
        &self->m_base, (int)XTextControlInteraction_TextBrowserInteraction);
#if XTEXTDOCUMENT_ON
    /* XTextEdit_init 已创建 m_textDoc：覆盖前释放旧对象（否则泄漏）。 */
    if (self->m_base.m_textDoc) {
        XClassDelete((XClass*)self->m_base.m_textDoc);
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
    XObject_connect((XObject*)self,
                      (size_t)XTextEdit_linkActivated_signal,
                      (XObject*)self, xtb_linkActivatedForward,
                      XConnectionType_Direct);
    XObject_connect((XObject*)self,
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

/** @brief 资源文件读取上限（4 MiB；Qt 无上限，见 gaps）。 */
#define XTB_RESOURCE_MAX_BYTES (4u * 1024u * 1024u)

/** @brief 读取整个文件为 NUL 结尾的堆文本（调用方 XFree_System）。
 *  @return 读取成功返回缓冲区；不可读/空/超限返回 NULL。 */
static char* xtb_readFile(const char* path)
{
#if XDEVICE_ON && XDEVICE_FILE_ON
    XDeviceOpenOptions options;
    XString* target;
    XFd fd;
    int err = 0;
    int64_t size;
    int64_t got = 0;
    char* buf;
    if (!path || !path[0]) return NULL;
    target = XString_create_utf8(path);
    if (!target) return NULL;
    XMemset(&options, 0, sizeof(options));
    options.m_openMode = (uint32_t)XIODevice_ReadOnly;
    options.m_target = target;
    fd = XDevice_open(XDeviceType_File, &options, &err);
    XClassDelete((XClass*)target);
    if (fd == XFD_INVALID) return NULL;
    size = XDevice_seek(fd, 0, XDeviceSeekWhence_End);
    if (size <= 0 || (uint64_t)size > (uint64_t)XTB_RESOURCE_MAX_BYTES ||
        XDevice_seek(fd, 0, XDeviceSeekWhence_Begin) < 0) {
        XDevice_close(fd);
        return NULL;
    }
    buf = (char*)XMalloc_System((size_t)size + 1);
    if (!buf) {
        XDevice_close(fd);
        return NULL;
    }
    while (got < size) {
        int64_t n = XDevice_read(fd, buf + got, size - got);
        if (n <= 0) break;
        got += n;
    }
    buf[got] = '\0';
    XDevice_close(fd);
    return buf;
#else
    (void)path;
    return NULL;
#endif
}

/** @brief 按 Qt 的 findFile 规则解析资源路径（qtextbrowser.cpp:141-172）：
 *         绝对路径直接使用，相对路径依次拼 searchPaths。 */
static char* xtb_resolveFile(const XTextBrowser* self, const char* name)
{
    char* text;
    size_t i;
    if (!self || !name || !name[0]) return NULL;
    if (name[0] == '/' || (name[0] != '\0' && name[1] == ':'))
        return xtb_readFile(name); /* 绝对路径（POSIX / Windows 盘符）。 */
    if (self->m_searchPaths) {
        size_t count = XStringList_size_base(
            (const XContainer*)self->m_searchPaths);
        for (i = 0; i < count; ++i) {
            const XString* path = (const XString*)XStringList_at_base(
                (const XVector*)self->m_searchPaths, i);
            const char* base = path ? XString_toUtf8(path) : NULL;
            size_t len;
            char* joined;
            if (!base) continue;
            len = XStrlen(base) + XStrlen(name) + 2;
            joined = (char*)XMalloc_System(len);
            if (!joined) continue;
            XSnprintf(joined, len, "%s%s%s", base,
                      (base[0] && base[XStrlen(base) - 1] == '/') ? "" : "/",
                      name);
            text = xtb_readFile(joined);
            XFree_System(joined);
            if (text) return text;
        }
    }
    text = xtb_readFile(name); /* 相对当前工作目录。 */
    return text;
}

/** @brief 按扩展名推断资源类型（qtextbrowser.cpp:277-287）。 */
static int xtb_guessType(const char* name)
{
    size_t len = name ? XStrlen(name) : 0;
    if (len >= 3) {
        const char* ext = name + len;
        size_t n = 0;
        while (ext > name && ext[-1] != '.') { --ext; ++n; }
        if (ext > name && ext[-1] == '.') {
            if ((n == 2 && (XStrcmp(ext, "md") == 0)) ||
                (n == 3 && (XStrcmp(ext, "mkd") == 0)) ||
                (n == 8 && (XStrcmp(ext, "markdown") == 0)))
                return (int)XTB_RES_MARKDOWN;
        }
    }
    return (int)XTB_RES_HTML;
}

void XTextBrowser_setSource(XTextBrowser* self, const char* url)
{
    xtb_setSourceInternal(self, url, (int)XTB_RES_UNKNOWN, true);
}

void XTextBrowser_setSource_2(XTextBrowser* self, const char* url, int type)
{
    xtb_setSourceInternal(self, url, type, true);
}

const char* XTextBrowser_source(const XTextBrowser* self)
{
    const char* text;
    if (!self || !self->m_source) return "";
    text = XString_toUtf8(self->m_source);
    return text ? text : "";
}

XVariant* XTextBrowser_loadResource(const XTextBrowser* self, int type,
                                    const char* name)
{
    char* text;
    XVariant* var;
    (void)type; /* Qt 的 override 亦不使用 type（qtextbrowser.cpp:1138）。 */
    if (!self || !name || !name[0]) return NULL;
    text = xtb_resolveFile(self, name);
    if (!text) return NULL;
    var = XString_toVariant_utf8(text);
    XFree_System(text);
    return var;
}

XTextBrowserSourceType XTextBrowser_sourceType(const XTextBrowser* self)
{
    /* 对标 QTextBrowser::sourceType（qtextbrowser.cpp:751-758）：导航栈顶
       条目的资源类型；栈空返回 UnknownResource。类型在 setSource 时按
       Qt 规则判定（扩展名推断或 setSource_2 显式指定）并逐条存历史。 */
    if (!self || self->m_historyCount <= 0 || self->m_historyIndex < 0)
        return XTextBrowserSourceType_Unknown;
    return (XTextBrowserSourceType)xtb_history_type(self,
                                                    self->m_historyIndex);
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
 * @param      type 资源类型（UnknownResource ⇒ 按扩展名推断，Qt 规则）。
 * @param      addHistory true 追加一条历史（用户导航），false 只在历史内
 *             移动（backward/forward/home），对标 QTextBrowser 语义。
 * @return     无返回值。
 * @note       追加历史时先截断前进分支，再写入容量上限 XTB_HISTORY_MAX
 *             （50）的环形数组；压满后新条目环形覆盖最老条目，当前索引
 *             相应左移，保证最老可后退条目始终不被当前索引越过。
 *             加载：同 Qt setSource 会在 doSetText 分支读资源并呈现；
 *             本实现只在**成功读到资源**时替换正文（qtextbrowser.cpp:
 *             289-341），读不到（相对路径不存在）时保留现有正文——
 *             项目偏差：避免一次笔误路径就把已显示内容清空，见 gaps。
 */
static void xtb_setSourceInternal(XTextBrowser* self, const char* url,
                                  int type, bool addHistory)
{
    XString* copy;
    int li;

    if (!self) return;
    if (type == (int)XTB_RES_UNKNOWN)
        type = xtb_guessType(url);
    if (!self->m_source) self->m_source = XString_create();
    if (self->m_source)
        XString_assign_utf8(self->m_source, url ? url : "");
    /* 加载资源并呈现（对标 qtextbrowser.cpp:289-341：HtmlResource ⇒
       setHtml、MarkdownResource ⇒ setMarkdown）。读不到资源时保留正文。 */
    {
        XVariant* resource = XTextBrowser_loadResource(self, type, url);
        if (resource) {
            const XString* content = XString_fromVariant_const(resource);
            const char* text = content ? XString_toUtf8(content) : NULL;
            if (text && text[0]) {
                if (type == (int)XTB_RES_MARKDOWN)
                    XTextEdit_setMarkdown(&self->m_base, text);
                else
                    XTextBrowser_setHtml(self, text);
            }
            XClassDelete((XClass*)resource);
        }
    }
    xtb_emitStr(self, (size_t)XTextBrowser_sourceChanged_signal,
                XString_toUtf8(self->m_source));
    /* 对标 QTextBrowserPrivate::setSource 收尾（qtextbrowser.cpp:356）：
       导航后视口复位到顶部（无 fragment 锚即重置垂直/水平滚动取值）。 */
    {
        XScrollBar* vsb = XAbstractScrollArea_verticalScrollBar(
            (const XAbstractScrollArea*)self);
        XScrollBar* hsb = XAbstractScrollArea_horizontalScrollBar(
            (const XAbstractScrollArea*)self);
        if (vsb) XScrollBar_setValue(vsb, 0);
        if (hsb) XScrollBar_setValue(hsb, 0);
    }
    if (!addHistory) {
        /* 历史内移动：同步当前条目的类型（backward/forward/home 已移动
           索引），供 sourceType 读取。 */
        if (self->m_historyIndex >= 0)
            xtb_history_setType(self, self->m_historyIndex, type);
        xtb_updateNavigationState(self);
        return;
    }
    /* 对标 QTextBrowser::doSetSource 同源早退（qtextbrowser.cpp:748-751）：
     * 栈顶已是同 URL（reload 场景）时重走加载但不重复压历史条目、不截
     * forward 分支——reload 完整重载而历史栈保持稳定。 */
    if (self->m_historyIndex >= 0) {
        const XString* top = xtb_history_entry(self, self->m_historyIndex);
        if (top && XString_equals(top, self->m_source, XChar_CaseSensitive)) {
            xtb_updateNavigationState(self);
            return;
        }
    }
    /* 新导航清掉 forward 历史（对标 Qt：重新访问旧地址时
       forward 条目被截断并释放，避免环形覆盖泄漏）。 */
    for (li = self->m_historyIndex + 1; li < self->m_historyCount; ++li) {
        XString* stale = xtb_history_entry(self, li);
        if (stale) XClassDelete((XClass*)stale);
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
        XClassDelete((XClass*)copy);
        xtb_updateNavigationState(self);
        return;
    }
    /* 容量已满：环形覆盖最老条目（逻辑第 0 条）。 */
    if (self->m_historyCount >= self->m_historyCapacity) {
        XString* oldest = xtb_history_entry(self, 0);
        if (oldest) XClassDelete((XClass*)oldest);
        xtb_history_set(self, 0, NULL);
        self->m_historyStart =
            (self->m_historyStart + 1) % self->m_historyCapacity;
        self->m_historyCount--;
        if (self->m_historyIndex > 0) self->m_historyIndex--;
    }
    xtb_history_set(self, self->m_historyCount, copy);
    xtb_history_setType(self, self->m_historyCount, type);
    self->m_historyIndex = self->m_historyCount;
    self->m_historyCount++;
    xtb_emitVoid(self, (size_t)XTextBrowser_historyChanged_signal);
    xtb_updateNavigationState(self);
}

void XTextBrowser_backward(XTextBrowser* self)
{
    XString* entry;
    int type;
    if (!self || self->m_historyIndex <= 0) return;
    entry = xtb_history_entry(self, self->m_historyIndex - 1);
    if (!entry) return;
    type = xtb_history_type(self, self->m_historyIndex - 1);
    self->m_historyIndex--;
    xtb_setSourceInternal(self, XString_toUtf8(entry), type, false);
}
void XTextBrowser_forward(XTextBrowser* self)
{
    XString* entry;
    int type;
    if (!self || self->m_historyIndex >= self->m_historyCount - 1) return;
    entry = xtb_history_entry(self, self->m_historyIndex + 1);
    if (!entry) return;
    type = xtb_history_type(self, self->m_historyIndex + 1);
    self->m_historyIndex++;
    xtb_setSourceInternal(self, XString_toUtf8(entry), type, false);
}
void XTextBrowser_home(XTextBrowser* self)
{
    XString* entry;
    int type;
    if (!self || self->m_historyCount <= 0) return;
    entry = xtb_history_entry(self, 0);
    if (!entry) return;
    type = xtb_history_type(self, 0);
    self->m_historyIndex = 0;
    xtb_setSourceInternal(self, XString_toUtf8(entry), type, false);
}
void XTextBrowser_reload(XTextBrowser* self)
{
    XString* urlCopy;
    int type;
    if (!self) return;
    if (!self->m_source) return;
    urlCopy = XString_create_copy(self->m_source);
    if (!urlCopy) return;
    /* 对标 QTextBrowser::reload（qtextbrowser.cpp:785-791）：取当前源后
     * 重走 setSource 完整加载路径（重读资源 + 重发 sourceChanged + 视口
     * 复位 + 历史结算），而非仅重发信号空转；同源早退（内部 doSetSource
     * 口径）保证栈顶同 URL 不重复压历史条目。 */
    type = (self->m_historyIndex >= 0)
               ? xtb_history_type(self, self->m_historyIndex)
               : (int)XTB_RES_UNKNOWN;
    xtb_setSourceInternal(self, XString_toUtf8(urlCopy), type, true);
    XClassDelete((XClass*)urlCopy);
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
        XClassDelete((XClass*)val);
        return (void*)(size_t)XTextBrowser_sourceChanged_signal;
    }
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self,
                           (size_t)XTextBrowser_sourceChanged_signal, args,
                           xtb_str_args_del, NULL, XEVENT_PRIORITY_NORMAL);
    else {
        /* 无连接路径同样须经 argsDel 释放 XString 载荷（防泄漏）。 */
        XVarList_setArgsDel(args, xtb_str_args_del);
        XVarList_delete(args);
    }
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
            if (stale) XClassDelete((XClass*)stale);
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
        XClassDelete((XClass*)self->m_searchPaths);
        self->m_searchPaths = NULL;
    }
    self->m_searchPaths = copy;
}

/**
 * @brief      获取资源搜索路径列表（对标 QTextBrowser::searchPaths）。
 * @param      self 目标控件；可为 NULL。
 * @return     新建的深拷贝 XStringList*，由调用方以
 *             XClassDelete 释放；内部为空（从未设置）时返回
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
 * @details    【P2-29 删除 2026-11-13】原 XTextBrowser_cursorRect 只是纯
 *             转调 XPlainTextEdit_cursorRect（而基类 XTextEdit_cursorRect
 *             已提供同口径同名口）⇒ 包装删除，调用方直接调基类
 *             XTextEdit_cursorRect。下方为保留的可读说明：
 *             内嵌编辑器常驻 (0,0) 铺满本控件，其局部坐标即本控件局部
 *             坐标；NULL 逐层兜底返回零矩形。
 */

/**
 * @brief      返回坐标 pos 处的超链接锚点（对标 QTextBrowser::anchorAt）。
 * @details    【P2-30 删除 2026-11-13】原 XTextBrowser_anchorAt 纯转调
 *             XTextEdit_anchorAt ⇒ 包装删除，调用方（含本文件 2 处内部
 *             调用）直接调基类 XTextEdit_anchorAt。
 *             锚点承载：以富文本文档（XTextDocument 片段 fmt.anchorHref）
 *             为承载、与富绘制路径同口径几何命中；片段无锚点或块带未命中
 *             返回 0 长度字符串对象。
 * @note       返回值为堆上新建的 XString*（空串对象或锚点文本），由
 *             调用方以 XClassDelete 释放；内存分配失败返回 NULL。
 */

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

#endif /* XGUI_ON */
