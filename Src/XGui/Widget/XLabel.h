/******************************************************************************
 * @file       XLabel.h
 * @brief      XLabel 标签控件（对标 Qt 6.8 QLabel，公开 API 的 C 适配）。
 * @details    XLabel 继承 XFrame（QLabel : QFrame），在控件矩形内显示
 *             文本、像素图、绘图记录或影片的单内容单元：
 *             - 文本显示：纯文本完整支持（换行、wordWrap 自动断行、对齐、
 *               边距/缩进、程序化选择渲染）；AutoText 自动检测 HTML；
 *               RichText/MarkdownText 支持最小子集（剥标签/标记、5 种
 *               HTML 实体、<a href> 与 [label](url) 链接记录、<br>/<p>
 *               与 Markdown 段落换行）。完整 HTML/Markdown 渲染引擎
 *               不在范围内，注释中列明不支持项；
 *             - 文本绘制使用内置 8x16 等宽点阵字体（XPainter_drawText8x16
 *               系列），不依赖平台字体/资源，嵌入式可用；非 ASCII 用
 *               空心方框替换字形，因此不建议用于正文级中文显示；
 *             - 像素图按 alignment 在内容区内对齐，setScaledContents 时
 *               拉伸填满；绘图记录（XPicture）与影片（XMovie）语义一致；
 *             - 链接交互（LinksAccessibleByMouse）：悬停发 linkHovered、
 *               按下再释放于链接上发 linkActivated；openExternalLinks
 *               为 true 时本实现不打开外部链接（无平台浏览器 API），
 *               仍发信号交由应用处理；
 *             - 尺寸提示对标 QLabelPrivate::sizeForWidth：文本尺寸 =
 *               (文本宽高 + 2*margin +/- indent) + 边框 contentsMargins；
 *               wordWrap 时高度随宽度变化（hasHeightForWidth）；
 *             - 默认属性与 Qt 一致：初始文本空、AutoText、Left|VCenter、
 *               margin=0、indent=-1、wordWrap=false、scaledContents=false、
 *               openExternalLinks=false、交互标志 LinksAccessibleByMouse、
 *               尺寸策略 Preferred/Preferred + ControlType=Label。
 *             本模块不依赖任何平台 API，嵌入式可用；绘制全部走 XPainter。
 * @note       模块总开关 XLABEL_ON 定义于 XGuiConfig.h；XLABEL_ON=0 时
 *             裁剪整个 XLabel 公共 API。XLabel 依赖 XWIDGET_ON 与
 *             XFRAME_ON（父类能力）以及 XSTRING_ON（文本存储）；XPixmap
 *             /XPicture/XMovie 为可选显示内容，均可用空对象表达。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XLABEL_H
#define XLABEL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XFrame.h"
#include "XString.h"
#include "XPixmap.h"
#include "XAlignment.h"

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/* ==================== 对象前向声明 ==================== */

/** @brief XPicture 绘图记录前向声明（XLabel 以指针持有；完整 API 见 XPicture.h）。 */
typedef struct XPicture XPicture;
/** @brief XMovie 影片前向声明（借用指针；完整 API 见 XMovie.h）。 */
typedef struct XMovie XMovie;
/** @brief XLabel 标签控件前向声明（完整类型见后文）。 */
typedef struct XLabel XLabel;

/* ==================== 虚函数表（覆盖 XWidget 派生槽位） ==================== */

/**
 * @brief XLabel 虚函数表枚举。
 * @details 槽位数量与 XWidget 完全一致；XLabel 不新增槽位，仅重载
 *          XClass 的 Copy/Move/Deinit 与 XObject 的 Event、XWidget 的
 *          PaintEvent/ChangeEvent/Mouse*Event/Focus*Event。
 */
XCLASS_DEFINE_BEGING(XLabel)
XCLASS_DEFINE_EXTEND_END(XLabel, XFrame)

/* ==================== 文本格式（对标 Qt 6.8 Qt::TextFormat） ==================== */

/** @brief 文本解释格式（对标 Qt::TextFormat，数值完全一致）。 */
typedef enum XLabelTextFormat
{
    XLabelTextFormat_PlainText    = 0, /**< 纯文本：原样显示。 */
    XLabelTextFormat_RichText     = 1, /**< 富文本：HTML 子集（见文件头注释）。 */
    XLabelTextFormat_AutoText     = 2, /**< 自动检测（默认）：像 HTML 则按富文本，否则纯文本。 */
    XLabelTextFormat_MarkdownText = 3  /**< Markdown 子集（链接/强调标记/标题）。 */
} XLabelTextFormat;

/* ==================== 文本交互标志（对标 Qt 6.8 Qt::TextInteractionFlag） ==================== */

/** @brief 文本交互标志位（对标 Qt::TextInteractionFlag，数值完全一致）。 */
typedef enum XLabelTextInteractionFlag
{
    XLabelTextInteraction_NoTextInteraction          = 0x00, /**< 无交互。 */
    XLabelTextInteraction_TextSelectableByMouse      = 0x01, /**< 鼠标可选（选择渲染仅程序化）。 */
    XLabelTextInteraction_TextSelectableByKeyboard   = 0x02, /**< 键盘可选（未实现的受限项）。 */
    XLabelTextInteraction_LinksAccessibleByMouse     = 0x04, /**< 鼠标可访问链接（默认）。 */
    XLabelTextInteraction_LinksAccessibleByKeyboard  = 0x08, /**< 键盘可访问链接（未实现的受限项）。 */
    XLabelTextInteraction_TextEditable               = 0x10  /**< 可编辑（未实现的受限项）。 */
} XLabelTextInteractionFlag;
/** @brief 文本交互标志组合类型（多个标志位按位或）。 */
typedef uint32_t XLabelTextInteractionFlags;

/** @brief 富文本资源加载回调（对标 QTextDocument::ResourceProvider 的 C 适配）。
 * @details 返回新建 XString*（如读取到的资源文本），所有权转移给调用方；
 *          返回 NULL 表示未提供。当前富文本子集不渲染外链资源图片，
 *          该回调仅作为扩展点保存，供后续富文本资源解析使用。 */
typedef XString* (*XLabelResourceProvider)(XLabel* label,
                                           const XString* url,
                                           void* userData);

/**
 * @brief      链接范围（内部使用的记录单元）。
 * @details    字段为 UTF-8 字节偏移，位于显示文本（m_displayText）上；
 *             m_start 包含、m_end 排他；m_href 为链接目标（拥有）。
 *             调用方不得手工修改 XLabel 结构体内的链接数组。
 */
typedef struct XLabelLinkRange
{
    int      m_start;   /**< 显示文本起始字节偏移（UTF-8，含）。 */
    int      m_end;     /**< 显示文本结束字节偏移（UTF-8，排他）。 */
    XString* m_href;    /**< 链接目标（拥有）。 */
} XLabelLinkRange;

/**
 * @brief      XLabel 标签控件对象；m_base 必须是第一个成员（嵌 XFrame）。
 * @details    字段含义：
 *             - m_text：原始文本（拥有）；setText/setNum 写入；
 *             - m_textFormat / m_effectiveTextFormat：用户格式与生效格式
 *               （AutoText 检测后的结果），默认 AutoText/PlainText；
 *             - m_alignment：内容对齐（默认 Left|VCenter，读写经掩码）；
 *             - m_isTextLabel：文本模式标志（setText/setNum 置真，
 *               setPixmap/setPicture/setMovie/clear 置假）；
 *             - m_wordWrap/m_scaledContents/m_openExternalLinks：
 *               属性开关，默认全部 false；
 *             - m_indent：文本缩进（默认 -1，自动计算）；m_margin：
 *               内容边距（默认 0）；
 *             - m_textInteractionFlags：交互标志（默认
 *               LinksAccessibleByMouse=4）；
 *             - m_pixmap：像素图（值类型，隐式共享）；m_picture：
 *               绘图记录（拥有，setPicture 深拷贝入）；m_movie：
 *               影片（借用）；m_buddy：助记符伙伴控件（借用，快捷键
 *               关联未实现的受限项）；
 *             - m_selectionStart/m_selectionLength：程序化选择范围，
 *               按显示文本（剥离标记后的可见文本）的 UTF-16 码元计；
 *               m_selectionStart=-1 表示无选择；
 *             - m_displayText：显示文本（拥有，富文本剥离标记后重生成）；
 *             - m_links：链接范围数组（拥有）；m_linkCount/m_linkCapacity；
 *             - m_pressedLink/m_hoverLink：鼠标按下/悬停命中的链接索引
 *               （-1 表示无，仅内部维护）；m_textSelecting 与
 *               m_selectionAnchor 用于鼠标拖选与键盘扩展选择的内部状态；
 *             - m_resourceProvider/m_resourceProviderUserData：资源回调
 *               与其上下文（借用）。
 *             调用方不得手工修改任何字段；属性读写一律走公开 API。
 */
typedef struct XLabel
{
    XFrame                          m_base;      /**< 基类成员；必须是第一个。 */
    XString*                        m_text;      /**< 原始文本（拥有）。 */
    XLabelTextFormat                m_textFormat;/**< 用户文本格式。 */
    XLabelTextFormat                m_effectiveTextFormat; /**< 生效文本格式。 */
    XAlignments                     m_alignment; /**< 内容对齐。 */
    bool                            m_isTextLabel;/**< 是否文本标签。 */
    bool                            m_wordWrap;  /**< 自动换行。 */
    bool                            m_scaledContents; /**< 内容缩放。 */
    bool                            m_openExternalLinks; /**< 外链打开标志。 */
    int                             m_indent;    /**< 文本缩进（-1 自动）。 */
    int                             m_margin;    /**< 内容边距。 */
    XLabelTextInteractionFlags      m_textInteractionFlags; /**< 交互标志。 */
    XPixmap                         m_pixmap;    /**< 像素图（值类型）。 */
    XPicture*                       m_picture;   /**< 绘图记录（拥有）。 */
    XMovie*                         m_movie;     /**< 影片（借用）。 */
    XWidget*                        m_buddy;     /**< 伙伴控件（借用）。 */
    int                             m_selectionStart;  /**< 选择起点（UTF-16，-1 无）。 */
    int                             m_selectionLength; /**< 选择长度（UTF-16 码元）。 */
    XString*                        m_displayText;/**< 显示文本（拥有）。 */
    XLabelLinkRange*                m_links;     /**< 链接范围数组（拥有）。 */
    int                             m_linkCount; /**< 链接数量。 */
    int                             m_linkCapacity; /**< 链接数组容量。 */
    int                             m_pressedLink;  /**< 按下链接索引（-1 无）。 */
    int                             m_hoverLink;    /**< 悬停链接索引（-1 无）。 */
    bool                            m_textSelecting; /**< 鼠标拖选进行中。 */
    int                             m_selectionAnchor; /**< 选择锚点（UTF-16，-1 无）。 */
    XLabelResourceProvider           m_resourceProvider;       /**< 资源回调（借用）。 */
    void*                           m_resourceProviderUserData;/**< 资源回调上下文（借用）。 */
} XLabel;

/* ==================== 生命周期（对标 QLabel 构造/析构） ==================== */

/** @brief XLabel 类虚函数表初始化（重载 Event/Paint/Change/Mouse/Focus/Copy/Move/Deinit）。 */
XVtable* XLabel_class_init(void);

/**
 * @brief      初始化 XLabel（对标 QLabel(QWidget*) 构造）。
 * @details    先初始化 XFrame 基类（parent/flags 语义同 XFrame_init），
 *             再挂 XLabel 虚表并设置默认值：空文本、AutoText、Left|VCenter、
 *             margin=0、indent=-1、wordWrap=false、scaledContents=false、
 *             openExternalLinks=false、LinksAccessibleByMouse，
 *             尺寸策略 Preferred/Preferred + ControlType=Label。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XLabel_init(XLabel* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建标签控件（语义同 XWidget_create）。 */
#define XLabel_create(parent, flags) XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建标签控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XLabel* XLabel_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XLabel 资源（栈/外部存储对象使用）。 */
#define XLabel_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XLabel 对象。 */
#define XLabel_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝（文本/绘图记录/链接深拷，像素图共享；不复制父/窗口句柄）。 */
#define XLabel_copy_base(self, other) XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动语义（转移资源所有权；源对象内容字段归构造默认值）。 */
#define XLabel_move_base(self, other) XClass_move_base((XClass*)(self), (XClass*)(other))

/* ==================== 文本（对标 QLabel text/setText/setNum） ==================== */

/** @brief 查询标签文本（对标 QLabel::text；返回借用指针，生命周期同对象）。 */
const XString* XLabel_text(const XLabel* self);
/**
 * @brief      设置标签文本（对标 QLabel::setText）。
 * @details    与当前文本相同则为无操作；否则清空旧内容进入文本模式，
 *             按 textFormat 解释（AutoText 自动检测 HTML）生成显示文本
 *             与链接表，随后刷新提示/几何并重绘。
 * @param      text 新文本；NULL 视为空。
 */
void XLabel_setText(XLabel* self, const XString* text);
/** @brief 设置标签文本（UTF-8 C 字符串便利版本，含 '\0' 结尾）。 */
void XLabel_setText_2(XLabel* self, const char* utf8);

/** @brief 查询文本格式（对标 QLabel::textFormat，默认 AutoText）。 */
XLabelTextFormat XLabel_textFormat(const XLabel* self);
/**
 * @brief      设置文本格式（对标 QLabel::setTextFormat）。
 * @details    仅当格式变化时生效；已有文本重新按新格式解释。
 */
void XLabel_setTextFormat(XLabel* self, XLabelTextFormat format);

/** @brief 设置标签内容为整数文本（对标 QLabel::setNum(int)，十进制表示）。 */
void XLabel_setNum(XLabel* self, int num);
/** @brief 设置标签内容为浮点文本（对标 QLabel::setNum(double)，%g 表示）。 */
void XLabel_setNum_2(XLabel* self, double num);

/**
 * @brief      清空标签全部内容（对标 QLabel::clear）。
 * @details    文本、像素图、绘图记录、影片与链接全部释放，退出文本模式，
 *             然后刷新提示/几何并重绘。
 */
void XLabel_clear(XLabel* self);

/* ==================== 图像/绘图记录/影片（对标 QLabel） ==================== */

/** @brief 返回像素图（对标 QLabel::pixmap；按值返回共享数据，可再传入绘制）。 */
XPixmap XLabel_pixmap(const XLabel* self);
/**
 * @brief      设置像素图（对标 QLabel::setPixmap）。
 * @details    像素图数据指针相同则为无操作；否则清空旧内容（含文本）后
 *             存储共享副本（隐式共享）；pixmap 为 NULL 时仅清空。
 */
void XLabel_setPixmap(XLabel* self, const XPixmap* pixmap);

/** @brief 返回绘图记录（对标 QLabel::picture；借用指针，无记录返回 NULL）。 */
XPicture* XLabel_picture(const XLabel* self);
/**
 * @brief      设置绘图记录（对标 QLabel::setPicture）。
 * @details    深拷贝记录并由标签持有；picture 为 NULL 时仅清空。
 *             与 Qt 按值复制语义等价，调用方保留自己的对象所有权。
 */
void XLabel_setPicture(XLabel* self, const XPicture* picture);

/** @brief 返回影片（对标 QLabel::movie；借用指针，无影片返回 NULL）。 */
XMovie* XLabel_movie(const XLabel* self);
/**
 * @brief      设置影片（对标 QLabel::setMovie；借用，标签不持有）。
 * @details    清空旧内容（含文本）；movie 为 NULL 时仅清空。
 *             影片当前帧像素图经 XMovie_currentPixmap 获取并绘制。
 */
void XLabel_setMovie(XLabel* self, XMovie* movie);

/** @brief 查询富文本资源回调（对标 QLabel::resourceProvider）。 */
XLabelResourceProvider XLabel_resourceProvider(const XLabel* self);
/** @brief 查询富文本资源回调上下文。 */
void* XLabel_resourceProviderUserData(const XLabel* self);
/**
 * @brief      设置富文本资源回调（对标 QLabel::setResourceProvider）。
 * @details    回调与上下文均为借用，标签不持有/不释放；当前富文本子集
 *             不自动取用外部资源，回调作为扩展点保存。
 */
void XLabel_setResourceProvider(XLabel* self, XLabelResourceProvider provider,
                                void* userData);

/* ==================== 布局属性（对标 QLabel） ==================== */

/** @brief 查询内容对齐（对标 QLabel::alignment，默认 Left|VCenter）。 */
XAlignments XLabel_alignment(const XLabel* self);
/** @brief 设置内容对齐（对标 QLabel::setAlignment；仅保留水平/垂直两组掩码位）。 */
void XLabel_setAlignment(XLabel* self, XAlignments alignment);

/** @brief 查询自动换行（对标 QLabel::wordWrap，默认 false）。 */
bool XLabel_wordWrap(const XLabel* self);
/** @brief 设置自动换行（对标 QLabel::setWordWrap；同步更新 hasHeightForWidth）。 */
void XLabel_setWordWrap(XLabel* self, bool on);

/** @brief 查询文本缩进（对标 QLabel::indent，默认 -1）。 */
int XLabel_indent(const XLabel* self);
/** @brief 设置文本缩进（对标 QLabel::setIndent；负值表示自动计算）。 */
void XLabel_setIndent(XLabel* self, int indent);

/** @brief 查询内容边距（对标 QLabel::margin，默认 0）。 */
int XLabel_margin(const XLabel* self);
/** @brief 设置内容边距（对标 QLabel::setMargin）。 */
void XLabel_setMargin(XLabel* self, int margin);

/**
 * @brief      返回标签当前字体像素高度（XinYueC 便携扩展）。
 * @details    读取控件当前 XFont 的像素高度。Qt QLabel 没有同名公共
 *             函数，严格 Qt 代码可改用 XWidget_font 查询字体对象。
 * @param      self 标签对象；NULL 返回 0。
 * @return     当前字体像素高度；无有效字体时返回 0。
 */
int XLabel_textPixelSize(const XLabel* self);

/**
 * @brief      设置标签字体像素高度（XinYueC 便携扩展）。
 * @details    更新当前 XFont 的像素高度，并触发布局提示和重绘；用于
 *             无字体数据库的嵌入式点阵渲染。Qt QLabel 通常通过
 *             QWidget::setFont 设置字体。
 * @param      self 标签对象；NULL 时忽略。
 * @param      pixelHeight 目标像素高度；小于等于 0 时恢复默认字号。
 */
void XLabel_setTextPixelSize(XLabel* self, int pixelHeight);

/** @brief 查询内容缩放（对标 QLabel::hasScaledContents，默认 false）。 */
bool XLabel_hasScaledContents(const XLabel* self);
/**
 * @brief      设置内容缩放（对标 QLabel::setScaledContents）。
 * @details    置真后像素图/绘图记录/影片按内容区尺寸等比拉伸填满。
 */
void XLabel_setScaledContents(XLabel* self, bool on);

/* ==================== 尺寸（对标 QLabel sizeHint/minimumSizeHint/heightForWidth） ==================== */

/** @brief 尺寸提示（对标 QLabel::sizeHint；文本时按 sizeForWidth(-1) 计算）。 */
XSize XLabel_sizeHint(const XLabel* self);
/**
 * @brief 最小尺寸提示（对标 QLabel::minimumSizeHint）。
 * @details 非文本标签 == sizeHint；文本标签高为一行的尺寸、
 *         宽为不换行/最大单词的尺寸。
 */
XSize XLabel_minimumSizeHint(const XLabel* self);
/**
 * @brief      按宽度计算高度（对标 QLabel::heightForWidth）。
 * @details    文本标签返回 sizeForWidth(w).height()；非文本标签回退
 *             基类 XWidget_heightForWidth。w 传 -1 时按 sizeHint 语义。
 */
int XLabel_heightForWidth(const XLabel* self, int width);

/* ==================== 伙伴与链接（对标 QLabel） ==================== */

/** @brief 返回伙伴控件（对标 QLabel::buddy；借用指针，无返回 NULL）。 */
XWidget* XLabel_buddy(const XLabel* self);
/**
 * @brief      设置伙伴控件（对标 QLabel::setBuddy）。
 * @details    存储借用指针；文本 '&' 助记符生成快捷键为受限未实现项，
 *             仅保留伙伴关系与 Qt 一致的读写语义。
 */
void XLabel_setBuddy(XLabel* self, XWidget* buddy);

/** @brief 查询外链打开标志（对标 QLabel::openExternalLinks，默认 false）。 */
bool XLabel_openExternalLinks(const XLabel* self);
/**
 * @brief      设置外链打开标志（对标 QLabel::setOpenExternalLinks）。
 * @details    受限项：本实现不内置平台 URI 打开能力，置真时点击链接
 *             仍只发出 linkActivated 信号，由应用负责打开。
 */
void XLabel_setOpenExternalLinks(XLabel* self, bool open);

/** @brief 查询文本交互标志（对标 QLabel::textInteractionFlags）。 */
XLabelTextInteractionFlags XLabel_textInteractionFlags(const XLabel* self);
/**
 * @brief      设置文本交互标志（对标 QLabel::setTextInteractionFlags）。
 * @details    置 LinksAccessibleByKeyboard 时焦点策略自动置 StrongFocus；
 *             置可选中（Mouse/Keyboard）时置 ClickFocus；否则 NoFocus。
 */
void XLabel_setTextInteractionFlags(XLabel* self,
                                    XLabelTextInteractionFlags flags);

/* ==================== 选择（对标 QLabel；程序化子集） ==================== */

/**
 * @brief      设置文本选择范围（对标 QLabel::setSelection）。
 * @details    参数为显示文本（剥离富文本标记后的可见文本）的 UTF-16
 *             代码单元偏移：start 为起点、length 为长度；start 与
 *             length 任一为 -1 时清除选择。选择仅影响绘制（Highlight /
 *             HighlightedText 着色），mouse 拖选与键盘选择为受限未实现项。
 * @param      start  起点（UTF-16 码元，从 0 开始）。
 * @param      length 长度（UTF-16 码元）。
 */
void XLabel_setSelection(XLabel* self, int start, int length);
/** @brief 查询是否已有选择（对标 QLabel::hasSelectedText）。 */
bool XLabel_hasSelectedText(const XLabel* self);
/**
 * @brief      返回选中文本（对标 QLabel::selectedText）。
 * @return     新建 XString*（UTF-16 码元 slice），所有权转移给调用方；
 *             无选择时返回空字符串（同样需要释放）。
 */
XString* XLabel_selectedText(const XLabel* self);
/** @brief 查询选择起点（UTF-16 码元；无选择返回 -1，对标 QLabel::selectionStart）。 */
int XLabel_selectionStart(const XLabel* self);

/* ==================== 信号（对标 QLabel::linkActivated/linkHovered） ==================== */

/**
 * @brief      链接激活信号（对标 QLabel::linkActivated）。
 * @details    链接上按下并释放时发射；参数为链接目标（借用指针，发射
 *             期间有效）。self 为 NULL 时返回信号函数地址（用于连接）。
 */
void* XLabel_linkActivated_signal(XLabel* self, const XString* link);
/**
 * @brief      链接悬停信号（对标 QLabel::linkHovered）。
 * @details    鼠标在链接上移入/移出或切换链接时发射，发射空字符串表示
 *             离开所有链接。self 为 NULL 时返回信号函数地址（用于连接）。
 */
void* XLabel_linkHovered_signal(XLabel* self, const XString* link);

/* ==================== 离屏绘制（扩展入口） ==================== */

/**
 * @brief      把标签内容绘制到给定绘制器（XFrame_drawFrame 的对应扩展）。
 * @details    先按 XFrame 当前样式画边框，再在客户区（减 margin/indent）
 *             内绘制文本/像素图/绘图记录/影片。供离屏渲染与回归测试
 *             直接调用；与重绘事件中的绘制路径完全一致，经 XPainter
 *             软件光栅输出，不依赖平台 API。
 * @param      self    标签对象；不可为 NULL。
 * @param      painter 绘制器；NULL 或未绑定设备时为无操作。
 */
void XLabel_drawContents(XLabel* self, XPainter* painter);

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLABEL_H */
