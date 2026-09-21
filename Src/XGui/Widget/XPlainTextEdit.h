/**
 * @file       XPlainTextEdit.h
 * @brief      XPlainTextEdit 多行纯文本编辑控件壳（对标 Qt 6.8
 *             QPlainTextEdit 核心公共 API）。
 * @details    功能范围：
 *             - 继承 XAbstractScrollArea：视口 + 双滚动条 + 内容尺寸
 *               联动（行数 x 行高，随控制器字体度量刷新）；
 *             - 编辑能力面整体迁入私有文本控制器 XTextControl（对标
 *               Qt QWidgetTextControl）：行模型/光标与选区锚点/
 *               撤销重做/IME/命中测试/标准菜单数据源；壳仅保留
 *               滚动联动、视口绘制、占位文本、frame/面板与信号转发；
 *             - 事件入口（键盘/鼠标/IME/焦点）换算视口→内容坐标后
 *               经 XTextControl_processEvent 统一路由；
 *             - 绘制正文/选区高亮/光标/IME 下划线由控制器
 *               XTextControl_draw 承担，壳画背景/边框/占位；
 *             - 文本：setPlainText/toPlainText、appendPlainText、
 *               insertPlainText、clear（控制器文档承载）；
 *             - 光标：行/列查询（控制器绝对位置换算），Left/Right/
 *               Up/Down/Home/End、Shift 扩展选区、拖选与双击选词、
 *               ensureCursorVisible；
 *             - 剪贴板：copy/cut/paste（XTextClipboard，选区语义）；
 *             - selectAll（锚点/位置模型）；undo/redo（命令差量栈）；
 *             - 只读 setReadOnly/isReadOnly（映射控制器可编辑标志）；
 *             - LineWrapMode 枚举（NoWrap/WidgetWidth，数值对齐）；
 *               WidgetWidth 软换行由控制器 XTextControl 承载（对标
 *               QTextLayout 行内 wrap）：视口宽经 setTextWidth 下发作
 *               折行宽度，滚动范围/光标/选区/命中按可视行口径；
 *             - maximumBlockCount（块数上限，超限丢弃最旧块）；
 *             - placeholderText 占位文本（空内容灰显）；
 *             - 信号 textChanged/selectionChanged/cursorPositionChanged/
 *               undoAvailable/redoAvailable/copyAvailable 等（发射点
 *               为控制器信号，经壳转发）。
 *             与 Qt 差异：富文本/QTextDocument/HTML 子集由控制器
 *             HTML 子集承载（剥标签 + 锚点提取）。
 * @note       模块总开关 XPLAINTEXTEDIT_ON 定义于 XGuiConfig.h；文本
 *             控制器总开关 XTEXTCONTROL_ON 定义于 XTextControl.h。
 * @author     XinYueC 团队
 */
#ifndef XPLAINTEXTEDIT_H
#define XPLAINTEXTEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif
#include "XTextControl.h"
#if !XTEXTCONTROL_ON
/* 控制器模块裁剪时的占位前向声明：壳退化为仅滚动/绘制职责，
   控制器指针恒空，全部编辑 API 无操作。 */
typedef struct XTextControl XTextControl;
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON

/* XVariant 前置声明（loadResource 返回载体；定义见 XData/XVariant）。 */
typedef struct XVariant XVariant;

/** @brief 换行模式（对标 QPlainTextEdit::LineWrapMode，数值一致）。 */
typedef enum XPlainTextEditMode
{
    XPlainTextEditMode_NoWrap = 0,      /**< 不换行。 */
    XPlainTextEditMode_WidgetWidth = 1  /**< 按控件宽度换行。 */
} XPlainTextEditMode;

/**
 * @brief      额外选择集条目（对标 QTextEdit::ExtraSelection 的平铺承载）。
 * @details    描述一处与光标选区无关的独立高亮区间：行号 + 行内字节
 *             偏移 + 长度 + 颜色。由 setExtraSelections 整体写入、
 *             extraSelections 借用读出。
 * @note       承载经换算委托控制器 XTextControlExtraSelection（文档
 *             绝对字节区间），绘制联动由 XTextControl_draw 渲染；
 *             跨 '\n' 的长度按文档绝对区间承载（@note 对标差异：
 *             Qt 条目携带 QTextCursor，可跨块）。
 */
typedef struct XPlainTextEditExtraSelection
{
    int line;       /**< 行号（0 起）。 */
    int col;        /**< 起始列（行内 UTF-8 字节偏移）。 */
    int length;     /**< 高亮长度（字节；0 表示空选区）。 */
    uint32_t color; /**< 高亮颜色（ARGB32）。 */
} XPlainTextEditExtraSelection;

XCLASS_DEFINE_BEGING(XPlainTextEdit)
XCLASS_DEFINE_EXTEND_END(XPlainTextEdit, XAbstractScrollArea)

/**
 * @brief      XPlainTextEdit 控件对象；m_base 必须是第一个成员。
 * @details    壳化后仅保留滚动联动/绘制/焦点编排等控件侧状态；
 *             行存储、光标/选区、撤销重做、IME 与命中测试全部由
 *             m_control（XTextControl，拥有）承载。
 */
typedef struct XPlainTextEdit
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XTextControl* m_control;    /**< 私有文本控制器（拥有；init 创建/
                                     deinit 销毁；文档/光标/选区/撤销
                                     承载，对标 Qt control 私有指针）。 */
    XVector* m_lines;           /**< @compat 行文本镜像（char* 数组，拥
                                     有；权威在控制器，textChanged 时
                                     整体重建）。仅供不可修改的既有
                                     消费方 XTextEdit 只读借用。 */
    bool m_readOnly;            /**< 只读（映射控制器可编辑交互标志）。 */
    int m_wrapMode;             /**< 换行模式（默认 WidgetWidth）。 */
    int m_maxBlockCount;        /**< 块数上限（0 = 无限制）。 */
    XString* m_placeholder;    /**< 占位文本（对象拥有）。 */
    bool m_undoEnabled;         /**< 撤销开关（镜像控制器，默认 true）。 */
    XVector* m_undoStack;       /**< @compat 撤销可用哨兵栈（char*；仅以
                                     空/非空镜像控制器 undoAvailable，
                                     内容不访问）。仅供 XTextEdit 查询。 */
    XVector* m_redoStack;       /**< @compat 重做可用哨兵栈（同上）。 */
    bool m_backgroundVisible;   /**< 背景可见（默认 true）。 */
    bool m_centerCursor;        /**< 光标居中滚动。 */
    bool m_centerOnScroll;      /**< 滚动跟随光标。 */
    bool m_tabChangesFocus;     /**< Tab 切焦点（默认 false）。 */
    int  m_tabStopDistance;     /**< Tab 步进（px，默认 40）。 */
    int  m_wordWrapMode;        /**< 断行规则（对标 QTextOption::WrapMode；
                                     默认 WordWrap，下发控制器承载）。 */
    XString* m_documentTitle;   /**< 文档标题（对象拥有）。 */
    int  m_textInteractionFlags;/**< 文本交互标志位集（壳存储镜像）。 */
    XTimerId m_autoScrollTimer; /**< 拖选边缘自动滚动定时器（100ms 启动；
                                     XTIMER_INVALID_ID = 未启动）。 */
    int  m_autoScrollDir;       /**< 自动滚动方向（+1 向下 / -1 向上）。 */
    bool m_inTrim;              /**< 块数上限裁剪进行中（防重入）。 */
    XVector* m_extraSelCache;   /**< 额外选择集 (行,列,长度) 换算缓存
                                     （XPlainTextEditExtraSelection 数组，
                                     拥有；承载换算内部用）。 */
} XPlainTextEdit;

/** @brief XPlain文本Editclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XPlainTextEdit_class_init(void);
void XPlainTextEdit_init(XPlainTextEdit* self, XWidget* parent,
                         XWidgetFlags flags);
#define XPlainTextEdit_create(parent, flags) XPlainTextEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XPlainTextEdit* XPlainTextEdit_create_ex(XMemoryType memory,
                                         XWidget* parent, XWidgetFlags flags);
#define XPlainTextEdit_deinit_base(self) XAbstractScrollArea_deinit_base((XAbstractScrollArea*)(self))
#define XPlainTextEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 文本（对标 QPlainTextEdit public API） ========== */

/**
 * @brief      设置纯文本。
 */
void XPlainTextEdit_setPlainText(XPlainTextEdit* self, const char* utf8);
/** @brief 导出纯文本（\n 连接全部块；返回堆拷贝，调用方 XFree_System）。 */
/**
 * @brief      导出纯文本。
 */
char* XPlainTextEdit_toPlainText(const XPlainTextEdit* self);
/**
 * @brief      追加纯文本行。
 */
void XPlainTextEdit_appendPlainText(XPlainTextEdit* self, const char* utf8);
/**
 * @brief      在光标处插入文本。
 */
void XPlainTextEdit_insertPlainText(XPlainTextEdit* self, const char* utf8);
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XPlainTextEdit_clear(XPlainTextEdit* self);

/* ==================== 编辑与属性 ==================== */

/**
 * @brief      获取只读状态。
 */
bool XPlainTextEdit_isReadOnly(const XPlainTextEdit* self);
/**
 * @brief      设置只读。
 */
void XPlainTextEdit_setReadOnly(XPlainTextEdit* self, bool readOnly);
/**
 * @brief      获取换行模式。
 */
int XPlainTextEdit_lineWrapMode(const XPlainTextEdit* self);
/**
 * @brief      设置换行模式。
 */
void XPlainTextEdit_setLineWrapMode(XPlainTextEdit* self, int mode);
/**
 * @brief      获取最大块数。
 */
int XPlainTextEdit_maximumBlockCount(const XPlainTextEdit* self);
/**
 * @brief      设置最大块数。
 */
void XPlainTextEdit_setMaximumBlockCount(XPlainTextEdit* self, int maximum);
/**
 * @brief      设置占位文本。
 */
void XPlainTextEdit_setPlaceholderText(XPlainTextEdit* self, const char* utf8);
/**
 * @brief      获取占位文本。
 */
const char* XPlainTextEdit_placeholderText(const XPlainTextEdit* self);
/**
 * @brief      获取撤销重做开关。
 */
bool XPlainTextEdit_isUndoRedoEnabled(const XPlainTextEdit* self);
/**
 * @brief      设置撤销重做开关。
 */
void XPlainTextEdit_setUndoRedoEnabled(XPlainTextEdit* self, bool enable);
/**
 * @brief      获取光标行。
 */
int XPlainTextEdit_cursorLine(const XPlainTextEdit* self);
/**
 * @brief      获取光标列。
 */
int XPlainTextEdit_cursorColumn(const XPlainTextEdit* self);

/* ==================== 光标几何与查找（对标 QPlainTextEdit public API） ==== */

/**
 * @brief      查询光标竖线矩形（对标 QPlainTextEdit::cursorRect）。
 * @details    矩形取自控制器 XTextControl_cursorRect（内容坐标：行高随
 *             控制器字体度量、X 按控制器字体测量光标前列宽），壳做视口
 *             换算（行左留白 2px、扣除垂直/水平滚动取值）后输出控件
 *             局部坐标。
 * @param      self 目标控件指针；NULL 时返回零矩形。
 * @return     光标矩形（宽度取控制器 cursorWidth，高度为一行行高）。
 */
XRect XPlainTextEdit_cursorRect(const XPlainTextEdit* self);

/**
 * @brief      返回坐标 pos 处的超链接锚点（对标 QPlainTextEdit::anchorAt）。
 * @details    委托控制器 anchorAt：平铺纯文本无锚点时恒返回 0 长度
 *             字符串（经 appendHtml 等路径登记锚点后返回命中 href）。
 * @param      self 目标控件指针；可为 NULL。
 * @param      pos 控件局部坐标点；可为 NULL。
 * @return     堆上新建的 XString*，调用方以 XString_delete_base 释放；
 *             分配失败返回 NULL。
 */
XString* XPlainTextEdit_anchorAt(const XPlainTextEdit* self,
                                 const XPoint* pos);

/**
 * @brief      从当前光标处查找文本（对标 QPlainTextEdit::find）。
 * @details    委托控制器 find：flags 支持 FindBackward（最低位，与
 *             QTextDocument::FindFlag 数值一致），控制器另支持区分
 *             大小写/全词标志。命中后选区即命中串（Qt 语义），未命中
 *             保持原位。
 * @param      self 目标控件指针；NULL 时直接返回 false。
 * @param      text UTF-8 查找串；NULL 或空串返回 false。
 * @param      flags 查找方向标志：1 = FindBackward（向后查找），
 *             0 = 向前查找。
 * @return     命中返回 true；未命中返回 false。
 */
bool XPlainTextEdit_find(XPlainTextEdit* self, const char* text, int flags);

/**
 * @brief      设置光标位置（对标 QPlainTextEdit::setTextCursor 的行列简化）。
 * @details    行列换算为文档绝对位置后委托控制器 setTextCursor（无选区
 *             收拢）；行列越界时钳位到有效范围。cursorLine/cursorColumn
 *             为同名既有查询接口；本组 textCursor* 命名与 Qt 对齐，
 *             两组查询语义一致。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      line 目标行（0 起）。
 * @param      col 目标列（行内 UTF-8 字节偏移）。
 * @return     无返回值。
 */
void XPlainTextEdit_setTextCursor(XPlainTextEdit* self, int line, int col);

/**
 * @brief      查询光标行（cursorLine 的 Qt textCursor 对齐命名别名）。
 * @param      self 目标控件指针；NULL 时返回 0。
 * @return     光标行（0 起）。
 */
int XPlainTextEdit_textCursorLine(const XPlainTextEdit* self);

/**
 * @brief      查询光标列（cursorColumn 的 Qt textCursor 对齐命名别名）。
 * @param      self 目标控件指针；NULL 时返回 0。
 * @return     光标列（行内 UTF-8 字节偏移）。
 */
int XPlainTextEdit_textCursorColumn(const XPlainTextEdit* self);

/* ==================== 编辑槽（对标 public slots） ==================== */

void XPlainTextEdit_undo(XPlainTextEdit* self);

/* ==================== 状态族与信号（2026-09-18 批次） ==================== */

int XPlainTextEdit_blockCount(const XPlainTextEdit* self);
/**
 * @brief      可视行数（对标 QPlainTextEdit::lineCount，可视行口径）。
 * @details    WidgetWidth 软换行时逻辑行折为多可视行，本查询返回折行
 *             后的总行数；NoWrap 时与 blockCount 相等。
 */
int XPlainTextEdit_lineCount(const XPlainTextEdit* self);
bool XPlainTextEdit_canPaste(const XPlainTextEdit* self);
void XPlainTextEdit_setCursorWidth(XPlainTextEdit* self, int width);
int XPlainTextEdit_cursorWidth(const XPlainTextEdit* self);
void XPlainTextEdit_setCenterCursor(XPlainTextEdit* self, bool center);
bool XPlainTextEdit_centerCursor(const XPlainTextEdit* self);
void XPlainTextEdit_setCenterOnScroll(XPlainTextEdit* self, bool on);
bool XPlainTextEdit_centerOnScroll(const XPlainTextEdit* self);
void XPlainTextEdit_setBackgroundVisible(XPlainTextEdit* self, bool visible);
bool XPlainTextEdit_backgroundVisible(const XPlainTextEdit* self);
void XPlainTextEdit_setTabChangesFocus(XPlainTextEdit* self, bool change);
bool XPlainTextEdit_tabChangesFocus(const XPlainTextEdit* self);
void XPlainTextEdit_setTabStopDistance(XPlainTextEdit* self, int distance);
int XPlainTextEdit_tabStopDistance(const XPlainTextEdit* self);
void XPlainTextEdit_setOverwriteMode(XPlainTextEdit* self, bool overwrite);
bool XPlainTextEdit_overwriteMode(const XPlainTextEdit* self);
void XPlainTextEdit_setWordWrapMode(XPlainTextEdit* self, int mode);
int XPlainTextEdit_wordWrapMode(const XPlainTextEdit* self);
void XPlainTextEdit_setTextInteractionFlags(XPlainTextEdit* self, int flags);
int XPlainTextEdit_textInteractionFlags(const XPlainTextEdit* self);
void XPlainTextEdit_setDocumentTitle(XPlainTextEdit* self, const XString* title);
void XPlainTextEdit_setDocumentTitle_2(XPlainTextEdit* self, const char* utf8);
XString* XPlainTextEdit_documentTitle(const XPlainTextEdit* self);
void XPlainTextEdit_moveCursor(XPlainTextEdit* self, int operation, int mode);
void XPlainTextEdit_appendHtml(XPlainTextEdit* self, const char* html);
void* XPlainTextEdit_undoAvailable_signal(XPlainTextEdit* self, bool available);
void* XPlainTextEdit_redoAvailable_signal(XPlainTextEdit* self, bool available);
void* XPlainTextEdit_copyAvailable_signal(XPlainTextEdit* self, bool available);
void* XPlainTextEdit_modificationChanged_signal(XPlainTextEdit* self, bool changed);
void* XPlainTextEdit_blockCountChanged_signal(XPlainTextEdit* self, int newCount);
/** @brief XPlain文本Editredo（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XPlainTextEdit_redo(XPlainTextEdit* self);
/**
 * @brief      复制。
 */
void XPlainTextEdit_copy(XPlainTextEdit* self);
/**
 * @brief      剪切。
 */
void XPlainTextEdit_cut(XPlainTextEdit* self);
/**
 * @brief      粘贴。
 */
void XPlainTextEdit_paste(XPlainTextEdit* self);
/**
 * @brief      全选。
 */
void XPlainTextEdit_selectAll(XPlainTextEdit* self);
/**
 * @brief      确保光标可见。
 */
void XPlainTextEdit_ensureCursorVisible(XPlainTextEdit* self);

/* ==================== 光标/格式/文档/额外选区/缩放（2026-09-17 批次） ==== */

/**
 * @brief      返回坐标 pos 处的光标位置（对标 QPlainTextEdit::cursorForPosition）。
 * @details    壳做视口→内容坐标平移（行左留白 2px、加回垂直滚动值）
 *             后委托控制器命中测试，再换算为 (行, 列) 承载。与
 *             cursorRect/paintEvent 同一控制器度量口径。
 * @param      self 目标控件指针；NULL 时返回 {0,0}。
 * @param      pos 视口局部坐标点；NULL 时返回 {0,0}。
 * @return     光标位置承载：x = 行号（0 起），y = 列（行内 UTF-8 字节
 *             偏移）。Qt 返回 QTextCursor，此处以 XPoint 平铺承载。
 */
XPoint XPlainTextEdit_cursorForPosition(const XPlainTextEdit* self,
                                        const XPoint* pos);

#if XMENU_ON
/**
 * @brief      创建标准右键菜单（对标 QPlainTextEdit::createStandardContextMenu）。
 * @details    委托控制器 XTextControl_createStandardContextMenu：动作
 *             集合（撤销/重做/剪切/复制/粘贴/删除/全选）与灰化条件
 *             （撤销/重做栈、选区、剪贴板）全部读取控制器状态；
 *             只读时仅提供复制/全选。弹出（popup）与 DeleteOnClose 由
 *             调用方负责（参照 contextMenuEvent 用法）。
 * @param      self 目标控件指针；可为 NULL（返回 NULL）。
 * @return     新建的 XMenu*；所有权转移给调用方（用 XMenu_delete_base
 *             释放）；创建失败返回 NULL。
 */
XMenu* XPlainTextEdit_createStandardContextMenu(XPlainTextEdit* self);
#endif /* XMENU_ON */

/**
 * @brief      获取当前字符格式（对标 QPlainTextEdit::currentCharFormat）。
 * @details    委托控制器：平铺模型以 int 位值承载字符格式。
 * @param      self 目标控件指针；NULL 时返回 0。
 * @return     当前字符格式位值。
 */
int XPlainTextEdit_currentCharFormat(const XPlainTextEdit* self);

/**
 * @brief      设置当前字符格式（对标 QPlainTextEdit::setCurrentCharFormat）。
 * @details    委托控制器：格式位值作为后续插入文本的格式约定。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      format 字符格式位值。
 * @return     无返回值。
 */
void XPlainTextEdit_setCurrentCharFormat(XPlainTextEdit* self, int format);

/**
 * @brief      合并字符格式到当前格式（对标 QPlainTextEdit::mergeCurrentCharFormat）。
 * @note       简化承载：Qt 按属性粒度合并 QTextCharFormat（仅覆盖修饰
 *             中显式置位的属性），此处简化为位值按位或覆盖。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      format 待合并的格式位值。
 * @return     无返回值。
 */
void XPlainTextEdit_mergeCurrentCharFormat(XPlainTextEdit* self, int format);

#if XTEXTDOCUMENT_ON
/**
 * @brief      返回内部富文本文档（对标 QPlainTextEdit::document）。
 * @details    委托控制器：文档镜像由控制器拥有（init 创建），本接口
 *             返回借用指针。
 * @note       若经 setDocument 换入外部文档，借用指针直至控件析构或
 *             再次 setDocument 前有效。
 * @param      self 目标控件指针；NULL 时返回 NULL。
 * @return     借用的 XTextDocument*；调用方不得释放。
 */
XTextDocument* XPlainTextEdit_document(const XPlainTextEdit* self);

/**
 * @brief      设置富文本文档（对标 QPlainTextEdit::setDocument 简化版）。
 * @details    委托控制器 setDocument（桥接口径与既有实现一致）：换入
 *             时接管所有权并将文档纯文本镜像到控制器行模型；传 NULL
 *             时新建空内部文档（对标 Qt 的空文档回退）。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      doc 新文档；传入后所有权归控制器，调用方不得再释放。
 * @return     无返回值。
 */
void XPlainTextEdit_setDocument(XPlainTextEdit* self, XTextDocument* doc);
#endif /* XTEXTDOCUMENT_ON */

/**
 * @brief      读取额外选择集（对标 QPlainTextEdit::extraSelections）。
 * @details    读控制器额外选择集并换算为 (行, 列, 长度) 承载输出。
 * @param      self 目标控件指针；NULL 时返回 0 且 *selections 置 NULL。
 * @param      selections 输出借用数组首地址；可为 NULL（只要条目数）。
 * @return     条目数（无额外选择时为 0）。
 */
int XPlainTextEdit_extraSelections(const XPlainTextEdit* self,
                                   const XPlainTextEditExtraSelection** selections);

/**
 * @brief      设置额外选择集（对标 QPlainTextEdit::setExtraSelections）。
 * @details    换算为文档绝对字节区间后整体写入控制器（拷贝语义），
 *             并请求重绘；高亮由 XTextControl_draw 渲染。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      selections 条目数组；NULL 视为清空。
 * @param      count 条目数；负值按 0 处理。
 * @return     无返回值。
 */
void XPlainTextEdit_setExtraSelections(XPlainTextEdit* self,
                                       const XPlainTextEditExtraSelection* selections,
                                       int count);

/**
 * @brief      载入资源（对标 QPlainTextEdit::loadResource 简化版）。
 * @details    委托控制器 loadResource：平铺模型无资源存储，与 Qt
 *             默认实现"找不到返回无效 QVariant"对齐，恒返回 NULL。
 * @param      self 目标控件指针；可为 NULL。
 * @param      name 资源名称；可为 NULL。
 * @return     恒返回 NULL（未找到）。
 */
XVariant* XPlainTextEdit_loadResource(XPlainTextEdit* self, int type,
                                      const char* name);

/**
 * @brief      返回文本光标（对标 QPlainTextEdit::textCursor 承载版）。
 * @details    控制器光标绝对位置换算为 XPoint 承载：x = 行号（0 起），
 *             y = 列（行内 UTF-8 字节偏移）；与 setTextCursor(line, col)、
 *             cursorLine/cursorColumn 同源。
 * @param      self 目标控件指针；NULL 时返回 {0,0}。
 * @return     光标位置承载（x = 行，y = 列）。
 */
XPoint XPlainTextEdit_textCursor(const XPlainTextEdit* self);

/**
 * @brief      放大字体（对标 QPlainTextEdit::zoomIn）。
 * @details    控件字体点大小与像素字号同步增加 range（下限钳位 1），
 *             像素字号同步增减以保证点阵渲染路径视觉生效；字体变更
 *             同步下发控制器（行高/基线度量随字体刷新）。
 * @param      self 目标控件指针；NULL 或 range 为 0 时无操作。
 * @param      range 增量（点数）；可为 0。
 * @return     无返回值。
 */
void XPlainTextEdit_zoomIn(XPlainTextEdit* self, int range);

/**
 * @brief      缩小字体（对标 QPlainTextEdit::zoomOut）。
 * @details    等价 zoomIn(-range)：点大小与像素字号同步减少 range。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      range 减量（点数）。
 * @return     无返回值。
 */
void XPlainTextEdit_zoomOut(XPlainTextEdit* self, int range);

/* ==================== 选区查询（2026-09-17 补齐批次） ==================== */

/**
 * @brief      查询是否存在选区（对标 hasSelectedText；Qt 经
 *             textCursor().hasSelection() 承载）。
 * @details    委托控制器：光标位置与锚点不等即存在选区（锚点/位置
 *             模型，可跨行）。
 * @param      self 目标控件指针；NULL 时返回 false。
 * @return     存在选区返回 true。
 */
bool XPlainTextEdit_hasSelectedText(const XPlainTextEdit* self);

/**
 * @brief      导出选中文本（对标 selectedText 的平铺承载）。
 * @details    委托控制器选区导出（锚点到位置的文档区间，可跨行），
 *             返回堆拷贝；无选区返回 NULL。
 * @param      self 目标控件指针；NULL 或无选区时返回 NULL。
 * @return     堆拷贝（UTF-8，NUL 结尾），调用方以 XFree_System 释放；
 *             分配失败返回 NULL。
 */
char* XPlainTextEdit_selectedText(const XPlainTextEdit* self);

/* ==================== 信号 ==================== */

/**
 * @brief      文本变化信号（真发射；发射源为控制器 textChanged 转发）。
 */
void* XPlainTextEdit_textChanged_signal(XPlainTextEdit* self);
/**
 * @brief      光标位置变化信号（真发射；发射源为控制器
 *             cursorPositionChanged 转发）。
 */
void* XPlainTextEdit_cursorPositionChanged_signal(XPlainTextEdit* self);

/**
 * @brief      视口重绘请求信号（对标 QPlainTextEdit::updateRequest）。
 * @details    滚动或内容变化触发视口重绘时真发射，载荷为需要重绘的
 *             视口矩形与垂直滚动增量 dy（内容上移为正，0 表示全量）；
 *             self 非 NULL 且有已连接槽时经 XObject_emitSignal 同步
 *             通知，否则只返回信号标识。
 * @param      self 目标控件指针；可为 NULL。
 * @param      rect 需要重绘的视口区域；NULL 视为整个视口。
 * @param      dy 垂直滚动增量（像素）。
 * @return     不透明的 updateRequest 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XPlainTextEdit_updateRequest_signal(XPlainTextEdit* self,
                                          const XRect* rect, int dy);

/**
 * @brief      选区变化信号（对标 QPlainTextEdit::selectionChanged）。
 * @details    真发射：发射源为控制器 selectionChanged 转发（拖选/
 *             键盘扩选/双击选词/选区收拢等路径）。
 * @param      self 目标控件指针；可为 NULL。
 * @return     不透明的 selectionChanged 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */

#endif /* XPLAINTEXTEDIT_H */
