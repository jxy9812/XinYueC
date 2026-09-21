/**
 * @file       XTextBrowser.h
 * @brief      XTextBrowser 富文本浏览控件（对标 Qt 6.8 QTextBrowser
 *             核心公共 API）。
 * @details    继承 XTextEdit（对标 QTextBrowser 继承 QTextEdit），
 *             默认只读；增加源导航 API（setSource/source/backward/
 *             forward/home/reload）与导航信号。setSource 记录 URL 并
 *             触发 sourceChanged（无文件加载通道，内容经 setHtml 注入
 *             ——渲染子集只读富文本预览，含 anchor 点击/悬停信号）。
 *             导航历史以容量上限 50 的环形数组承载，压满后新条目
 *             环形覆盖最老条目（对标浏览器历史上限行为）。
 * @note       模块总开关 XTEXTBROWSER_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XTEXTBROWSER_H
#define XTEXTBROWSER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#if XPLAINTEXTEDIT_ON
#include "XTextEdit.h"
#endif
#include "XStringList.h"

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON

XCLASS_DEFINE_BEGING(XTextBrowser)
XCLASS_DEFINE_EXTEND_END(XTextBrowser, XTextEdit)

typedef struct XTextBrowser
{
    XTextEdit m_base;  /**< 基类成员；必须是第一个。 */
#if XTEXTDOCUMENT_ON
    XTextDocument* m_textDoc; /**< 富文本文档。 */
#endif
    XString** m_history;    /**< 历史环形数组（对象拥有；容量上限 50，
                                  满后环形覆盖最老条目）。 */
    int m_historyCount;
    int m_historyIndex;
    int m_historyCapacity;
    int m_historyStart;     /**< 环形数组中逻辑第 0 条的物理下标。 */
    XString* m_source;      /**< 当前源 URL（对象拥有）。 */
    XStringList* m_searchPaths; /**< 资源搜索路径列表（拥有；对标 searchPaths）。 */
    bool m_openLinks;       /**< 链接可点击（默认 true）。 */
    bool m_openExternalLinks; /**< 外链自动打开开关（默认 false；对标 openExternalLinks）。 */
    bool m_backwardAvailable; /**< 上次发射的后退可用状态（变化才发信号）。 */
    bool m_forwardAvailable;  /**< 上次发射的前进可用状态（变化才发信号）。 */
    XString* m_hoverAnchor; /**< 悬停锚点去重承载（对象拥有；NULL=无；
                                  对标 Qt 悬停高亮只在进出链接时发射）。 */
} XTextBrowser;

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XTextBrowser_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XTextBrowser_init(XTextBrowser* self, XWidget* parent, XWidgetFlags flags);
#define XTextBrowser_create(parent, flags) XTextBrowser_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XTextBrowser* XTextBrowser_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTextBrowser_deinit_base(self) XTextEdit_deinit_base((XTextEdit*)(self))
#define XTextBrowser_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      设置浏览源 URL（对标 setSource）。
 */
void XTextBrowser_setSource(XTextBrowser* self, const char* url);
/**
 * @brief      获取当前浏览源 URL（对标 source）。
 */
const char* XTextBrowser_source(const XTextBrowser* self);

/**
 * @brief      设置富文本内容（渲染子集；对标 QTextBrowser::setHtml）。
 * @details    委托基类 XTextEdit_setHtml：以渲染子集（b/i/u/s、
 *             font color/size、br、p align、a href，嵌套上限一层）
 *             解析并进入只读富文本预览；浏览器编辑器恒只读。链接悬停
 *             高亮/手型光标随预览绘制生效，点击发射 anchorClicked 并按
 *             openLinks/openExternalLinks 决定导航或外开。
 * @param      self 目标控件指针；NULL 无操作。
 * @param      html UTF-8 HTML 文本；NULL 视为空串。
 * @return     无返回值。
 */
void XTextBrowser_setHtml(XTextBrowser* self, const char* html);

/** @brief 浏览源类型（对标 QTextDocument::ResourceType 的简化子集）。 */
typedef enum XTextBrowserSourceType
{
    XTextBrowserSourceType_Unknown = -1, /**< 未知源（尚未加载任何源条目）。 */
    XTextBrowserSourceType_Url     = 0   /**< URL 文本源（当前唯一支持类型；0=Url）。 */
} XTextBrowserSourceType;

/**
 * @brief      返回当前源类型（对标 QTextBrowser::sourceType 的 C 适配）。
 * @details    对标 Qt 6.8 语义：Qt 按导航栈顶条目记录的资源类型返回，
 *             导航栈为空时返回 QTextDocument::UnknownResource；本适配
 *             第一版不做 HTML/Markdown 渲染，源恒为 URL 文本，故有导航
 *             条目时恒返回 XTextBrowserSourceType_Url（0=Url）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     源类型枚举：从未设置源（导航历史为空）或 self 为 NULL
 *             返回 XTextBrowserSourceType_Unknown；已设置源返回
 *             XTextBrowserSourceType_Url。
 * @note       简化项：类型不随 URL 扩展名推断（Qt 6.8 对 .md/.markdown
 *             推断为 MarkdownResource），本版一律视为 URL 文本源。
 */
XTextBrowserSourceType XTextBrowser_sourceType(const XTextBrowser* self);
/**
 * @brief      设置链接可点击（对标 setOpenLinks）。
 */
void XTextBrowser_setOpenLinks(XTextBrowser* self, bool open);
/**
 * @brief      获取链接可点击状态。
 */
bool XTextBrowser_openLinks(const XTextBrowser* self);
/**
 * @brief      导航后退（对标 backward）。
 */
void XTextBrowser_backward(XTextBrowser* self);
/**
 * @brief      导航前进（对标 forward）。
 */
void XTextBrowser_forward(XTextBrowser* self);
/**
 * @brief      回到首页（对标 home）。
 */
void XTextBrowser_home(XTextBrowser* self);
/**
 * @brief      刷新当前源（对标 reload）。
 */
void XTextBrowser_reload(XTextBrowser* self);

/**
 * @brief      清空导航历史（对标 QTextBrowser::clearHistory）。
 * @details    对标 Qt 6.8 语义：删除全部前进/后退条目，但保留当前条目
 *             （清空后历史栈仅剩当前 1 条），随后发射 historyChanged，
 *             后退/前进可用状态变化时发射对应信号。
 * @note       历史环形数组容量上限 50；本函数仅清条目，不释放数组。
 */
void XTextBrowser_clearHistory(XTextBrowser* self);
/**
 * @brief      当前条目之前（可后退）的历史条数（对标
 *             backwardHistoryCount）。
 * @return     无历史或已在最老条目时返回 0。
 */
int XTextBrowser_backwardHistoryCount(const XTextBrowser* self);
/**
 * @brief      当前条目之后（可前进）的历史条数（对标
 *             forwardHistoryCount）。
 * @return     无前进历史时返回 0。
 */
int XTextBrowser_forwardHistoryCount(const XTextBrowser* self);
/**
 * @brief      按相对偏移取历史项标题（对标 historyTitle）。
 * @param      index 相对当前条目的偏移（项目简化语义）：0 为当前条目，
 *             -1 为上一条，+1 为下一条，依此类推。
 * @return     对应条目标题；越界或无历史时返回空串。
 * @note       项目简化：历史条目未解析文档标题，标题返回对应条目的
 *             源 URL 字符串；返回内部缓存指针，条目变化前有效。
 */
const char* XTextBrowser_historyTitle(const XTextBrowser* self, int index);
/**
 * @brief      按相对偏移取历史项源 URL（对标 historyUrl）。
 * @param      index 语义同 historyTitle（0 当前、负后退、正前进）。
 * @return     对应条目 URL；越界或无历史时返回空串；返回内部缓存
 *             指针，条目变化前有效。
 */
const char* XTextBrowser_historyUrl(const XTextBrowser* self, int index);
/**
 * @brief      是否可后退（对标 isBackwardAvailable；实时按历史栈计算）。
 */
bool XTextBrowser_isBackwardAvailable(const XTextBrowser* self);
/**
 * @brief      是否可前进（对标 isForwardAvailable；实时按历史栈计算）。
 */
bool XTextBrowser_isForwardAvailable(const XTextBrowser* self);

/**
 * @brief      设置外部链接是否自动打开（对标 setOpenExternalLinks）。
 * @details    开启后外链交由桌面打开（XPlatformServices）而不只发
 *             anchorClicked；由链接点击路径（VX_browser_eventFilter）
 *             在按下命中锚点时消费。
 */
void XTextBrowser_setOpenExternalLinks(XTextBrowser* self, bool open);
/**
 * @brief      获取外部链接自动打开状态（对标 openExternalLinks；
 *             默认 false）。
 */
bool XTextBrowser_openExternalLinks(const XTextBrowser* self);
/**
 * @brief      设置资源搜索路径列表（对标 setSearchPaths）。
 * @param      paths 源列表；内部做深拷贝保存；NULL 表示清空。
 * @note       拷贝失败时保留原列表不变。
 */
void XTextBrowser_setSearchPaths(XTextBrowser* self, const XStringList* paths);
/**
 * @brief      获取资源搜索路径列表（对标 searchPaths）。
 * @return     返回新建的深拷贝 XStringList*，由调用方以
 *             XStringList_delete_base 释放；内部为空时返回新建空列表。
 */
XStringList* XTextBrowser_searchPaths(const XTextBrowser* self);

/**
 * @brief      查询当前光标竖线矩形（对标 QTextBrowser::cursorRect）。
 * @details    委托内嵌编辑器 XPlainTextEdit_cursorRect（与
 *             XTextEdit_cursorRect 同一套度量口径）：内嵌编辑器常驻
 *             (0,0) 并铺满本控件，其局部坐标即本控件局部坐标；行高
 *             16px、行左留白 2px，X 方向按控件字体测量光标前列宽
 *             （UTF-8 字节偏移口径），Y 方向随内嵌编辑器垂直滚动条
 *             取值偏移。
 * @param      self 目标控件指针；NULL 或内嵌编辑器缺失时返回零矩形。
 * @return     光标矩形（本控件局部坐标；宽度取编辑器 cursorWidth
 *             设定值，高度为一行行高）。
 */
XRect XTextBrowser_cursorRect(const XTextBrowser* self);
/**
 * @brief      返回坐标 pos 处的超链接锚点（对标 QTextBrowser::anchorAt）。
 * @details    委托 XTextEdit_anchorAt：以富文本文档（XTextDocument 片段
 *             fmt.anchorHref）为承载、与只读预览绘制路径同一套逐块几何
 *             命中（块高随字体度量、块宽实测、按对齐定位）；片段无锚点
 *             或未命中返回 0 长度字符串对象。
 * @note       返回值为堆上新建的 XString*（空串对象或锚点文本），由
 *             调用方以 XString_delete_base 释放；内存分配失败返回 NULL。
 * @param      self 目标控件指针；可为 NULL。
 * @param      pos 控件局部坐标点；可为 NULL。
 * @return     堆上新建的 XString*；语义见 @note。
 */
XString* XTextBrowser_anchorAt(const XTextBrowser* self, const XPoint* pos);

/**
 * @brief      源变化信号（真发射）。
 */
void* XTextBrowser_sourceChanged_signal(XTextBrowser* self, const char* url);
/**
 * @brief      后退可用信号（真发射）。
 */
void* XTextBrowser_backwardAvailable_signal(XTextBrowser* self, bool available);
/**
 * @brief      前进可用信号（真发射）。
 */
void* XTextBrowser_forwardAvailable_signal(XTextBrowser* self, bool available);

/**
 * @brief      historyChanged(const QString&) 信号地址（对标
 *             QTextBrowser::historyChanged）。
 * @details    导航历史变化（setSource 追加历史、clearHistory 清空）时
 *             真发射；self 非 NULL 且有已连接槽时经 XObject_emitSignal
 *             同步通知，self 为 NULL 或无连接时只返回信号标识。
 * @param      self 目标控件指针；可为 NULL。
 * @return     不透明的 historyChanged 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XTextBrowser_historyChanged_signal(XTextBrowser* self);

/** @brief anchorClicked(const char*) 信号（对标 QTextBrowser::anchorClicked；
 *         载荷：链接 URL UTF-8）。真实发射点：只读富文本预览态下鼠标
 *         点击命中锚点（基类 linkActivated → xtb_linkActivatedForward
 *         转发），默认 openLinks 语义下同时以该 URL 触发 setSource 导航，
 *         openExternalLinks 开启时改交平台服务按桌面方式打开。 */
void* XTextBrowser_anchorClicked_signal(XTextBrowser* self, const char* url);

/**
 * @brief      highlighted(const char*) 信号（对标
 *             QTextBrowser::highlighted）。
 * @details    载荷：高亮链接 URL（UTF-8）。真实发射点：预览态鼠标进入/
 *             切换/离开链接（URL 变化）时经基类 linkHovered →
 *             xtb_linkHoveredForward 转发发射（离开时载荷为空串）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      url 高亮链接 URL（UTF-8）；可为 NULL，视为空串。
 * @return     不透明的 highlighted 信号标识；返回值不指向可释放对象，
 *             也不得解引用。
 */
void* XTextBrowser_highlighted_signal(XTextBrowser* self, const char* url);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON && XTEXTBROWSER_ON */
/* ==================== 信号 ==================== */
#endif /* XTEXTBROWSER_H */