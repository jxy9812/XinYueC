/**
 * @file       XPlainTextEdit.h
 * @brief      XPlainTextEdit 多行纯文本编辑控件（对标 Qt 6.8
 *             QPlainTextEdit 核心公共 API）。
 * @details    功能范围：
 *             - 继承 XAbstractScrollArea：视口 + 双滚动条 + 内容尺寸
 *               联动（行数 x 行高）；
 *             - 文本：setPlainText/toPlainText、appendPlainText、
 *               insertPlainText、clear（
 分块存储，逐行绘制）；
 *             - 光标：行/列内部光标，Left/Right/Up/Down/Home/End 移动，
 *               ensureCursorVisible；
 *             - 编辑：可打印字符插入、Backspace/Delete、Enter 分行；
 *             - 剪贴板：copy/cut/paste（XClipboard，对标 Qt 剪贴板交互）；
 *             - selectAll（对标）；undo/redo（快照栈）；
 *             - 只读 setReadOnly/isReadOnly；
 *             - LineWrapMode 枚举（NoWrap/WidgetWidth，数值对齐；第一版
 *               存储不换行绘制）；
 *             - maximumBlockCount（块数上限，超限丢弃最旧块）；
 *             - placeholderText 占位文本（空内容灰显）；
 *             - 信号 textChanged()。
 *             与 Qt 差异：富文本/QTextDocument/HTML 子集暂不涉及。
 * @note       模块总开关 XPLAINTEXTEDIT_ON 定义于 XGuiConfig.h。
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
#include "XTextDocument.h"
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
 * @note       对标差异：Qt 的 ExtraSelection 携带 QTextCharFormat（可含
 *             背景/前景/属性），此处简化为单一 ARGB32 高亮色；绘制联动
 *             （paintEvent 高亮渲染）暂未接入。
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

typedef struct XPlainTextEdit
{
    XAbstractScrollArea m_base;
#if XTEXTDOCUMENT_ON
    XTextDocument* m_textDoc; /**< 富文本文档。 */
#endif
    XVector* m_lines;           /**< 文本行数组（char*，拥有）。 */
    int m_cursorLine;           /**< 光标行（0 起）。 */
    int m_cursorCol;            /**< 光标列（字节偏移）。 */
    bool m_readOnly;            /**< 只读。 */
    int m_wrapMode;             /**< 换行模式（默认 WidgetWidth）。 */
    int m_maxBlockCount;        /**< 块数上限（0 = 无限制）。 */
    XString* m_placeholder;    /**< 占位文本（对象拥有）。 */
    bool m_undoEnabled;         /**< 撤销开关（默认 true）。 */
    XVector* m_undoStack;       /**< 撤销快照栈（char*）。 */
    XVector* m_redoStack;       /**< 重做快照栈（char*）。 */
    bool m_selectionActive;     /**< 选区激活（selectAll 置位）。 */
    bool m_backgroundVisible;   /**< 背景可见（默认 true）。 */
    int  m_cursorWidth;         /**< 光标宽度（px，默认 1）。 */
    bool m_centerCursor;        /**< 光标居中滚动。 */
    bool m_centerOnScroll;      /**< 滚动跟随光标。 */
    bool m_tabChangesFocus;     /**< Tab 切焦点（默认 false）。 */
    int  m_tabStopDistance;     /**< Tab 步进（px，默认 40）。 */
    bool m_overwriteMode;       /**< 覆盖模式。 */
    int  m_wordWrapMode;        /**< 换行模式（对标 QTextOption::WrapMode）。 */
    XString* m_documentTitle;   /**< 文档标题（对象拥有）。 */
    int  m_textInteractionFlags;/**< 文本交互标志位集。 */
    bool m_modified;            /**< 修改标志。 */
    int  m_charFormat;          /**< 当前字符格式（位值承载，简化 QTextCharFormat）。 */
    XVector* m_extraSelections; /**< 额外选择集（XPlainTextEditExtraSelection 数组，拥有）。 */
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
 * @details    与 paintEvent 自绘使用同一套度量口径：行高为
 *             XPE_LINE_HEIGHT（16px）、行左留白 2px；X 方向按控件字体
 *             测量光标前列宽（XPainter_textWidthRange，UTF-8 字节偏移
 *             口径），Y 方向随垂直滚动条取值偏移。矩形为控件局部坐标
 *             （不含绘制偏移变换）。
 * @param      self 目标控件指针；NULL 时返回零矩形。
 * @return     光标矩形（宽度取 setCursorWidth 设定值，高度为一行行高）。
 */
XRect XPlainTextEdit_cursorRect(const XPlainTextEdit* self);

/**
 * @brief      返回坐标 pos 处的超链接锚点（对标 QPlainTextEdit::anchorAt）。
 * @details    XPlainTextEdit 为平铺纯文本控件，不含任何锚点；本函数仅
 *             为对标 Qt 接口存在性而提供，恒返回 0 长度字符串。
 * @note       纯文本无锚点：pos 仅用于保持签名一致，不被使用。
 * @param      self 目标控件指针；可为 NULL。
 * @param      pos 控件局部坐标点；可为 NULL，不被使用。
 * @return     堆上新建的 0 长度 XString*，调用方以 XString_delete_base
 *             释放；分配失败返回 NULL。
 */
XString* XPlainTextEdit_anchorAt(const XPlainTextEdit* self,
                                 const XPoint* pos);

/**
 * @brief      从当前光标处查找文本（对标 QPlainTextEdit::find 简化版）。
 * @details    查找按 UTF-8 字节偏移逐行进行；flags 仅支持最低位，
 *             与 QTextDocument::FindFlag::FindBackward 数值一致。
 *             命中后置 cursorLine/cursorCol 并请求重绘（Qt 的选中文本
 *             语义简化为仅移动光标：向前查找光标落在命中结束处，
 *             向后查找落在命中起始处，便于连续查找）；未命中保持原位。
 * @param      self 目标控件指针；NULL 时直接返回 false。
 * @param      text UTF-8 查找串；NULL 或空串返回 false。
 * @param      flags 查找方向标志：1 = FindBackward（向后查找），
 *             0 = 向前查找。
 * @return     命中返回 true；未命中返回 false。
 */
bool XPlainTextEdit_find(XPlainTextEdit* self, const char* text, int flags);

/**
 * @brief      设置光标位置（对标 QPlainTextEdit::setTextCursor 的行列简化）。
 * @details    行列越界时钳位到有效范围（行 [0, blockCount-1]、列
 *             [0, 该行字节数]），并请求重绘。cursorLine/cursorColumn
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
 * @details    基于平铺度量口径反查：行 = (pos->y + 垂直滚动值) / 行高，
 *             钳位到 [0, blockCount-1]；列按控件字体自行首（左留白 2px
 *             之后）逐码点累加字形宽（XPainter_textWidthRange，UTF-8
 *             字节偏移口径），定位到 pos->x 落点前的码点边界，超出行宽
 *             钳位到行尾。与 cursorRect/paintEvent 使用同一套度量。
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
 * @details    菜单包含撤销/重做/剪切/复制/粘贴/全选动作，动作触发槽
 *             直连控件自身同名槽；启用态按当前状态计算（撤销/重做看
 *             快照栈、剪切/复制看选区激活、粘贴看剪贴板文本、全选看
 *             存在文本且未全选），只读时不加入编辑类动作。
 * @note       对标差异：Qt 菜单另含"删除"与 IME 相关项，此处为任务
 *             指定的六项简化子集；弹出（popup）与 DeleteOnClose 由
 *             调用方负责（参照 XLineEdit::contextMenuEvent 用法）。
 * @param      self 目标控件指针；可为 NULL（返回 NULL）。
 * @return     新建的 XMenu*；所有权转移给调用方（用 XMenu_delete_base
 *             释放）；创建失败返回 NULL。
 */
XMenu* XPlainTextEdit_createStandardContextMenu(XPlainTextEdit* self);
#endif /* XMENU_ON */

/**
 * @brief      获取当前字符格式（对标 QPlainTextEdit::currentCharFormat）。
 * @details    平铺模型以 int 位值承载字符格式（粗体/斜体/下划线等按位
 *             自定义），与富文本 QTextCharFormat 无对应转换。
 * @param      self 目标控件指针；NULL 时返回 0。
 * @return     当前字符格式位值。
 */
int XPlainTextEdit_currentCharFormat(const XPlainTextEdit* self);

/**
 * @brief      设置当前字符格式（对标 QPlainTextEdit::setCurrentCharFormat）。
 * @details    平铺模型下仅记录格式位值，作为后续插入文本的格式约定；
 *             不追溯改写既有文本的格式。
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
 * @details    控件初始化即持有内部 XTextDocument（appendHtml 等简化
 *             路径的承载），本接口返回借用指针，所有权仍归控件。
 * @note       平铺模型正文绘制走内部行数组，文档内容不反向同步；
 *             若经 setDocument 换入外部文档，借用指针直至控件析构或
 *             再次 setDocument 前有效。
 * @param      self 目标控件指针；NULL 时返回 NULL。
 * @return     借用的 XTextDocument*；调用方不得释放。
 */
XTextDocument* XPlainTextEdit_document(const XPlainTextEdit* self);

/**
 * @brief      设置富文本文档（对标 QPlainTextEdit::setDocument 简化版）。
 * @details    控件接管 doc 所有权（与内部 m_textDoc 的拥有语义一致；
 *             Qt 中文档所有权不转移，为 @note 对标差异）：换入时以
 *             XTextDocument_toPlainText 将文档纯文本镜像到平铺行数组；
 *             传 NULL 时新建空内部文档（对标 Qt 的空文档回退）。
 * @param      self 目标控件指针；NULL 时无操作。
 * @param      doc 新文档；传入后所有权归控件，调用方不得再释放。
 * @return     无返回值。
 */
void XPlainTextEdit_setDocument(XPlainTextEdit* self, XTextDocument* doc);
#endif /* XTEXTDOCUMENT_ON */

/**
 * @brief      读取额外选择集（对标 QPlainTextEdit::extraSelections）。
 * @details    返回内部数组借用视图与条目数；指针在下次
 *             setExtraSelections 或控件析构前有效，不得释放或修改。
 * @note       承载结构见 XPlainTextEditExtraSelection；绘制联动
 *             （paintEvent 高亮渲染）暂未接入，@note 状态承载。
 * @param      self 目标控件指针；NULL 时返回 0 且 *selections 置 NULL。
 * @param      selections 输出借用数组首地址；可为 NULL（只要条目数）。
 * @return     条目数（无额外选择时为 0）。
 */
int XPlainTextEdit_extraSelections(const XPlainTextEdit* self,
                                   const XPlainTextEditExtraSelection** selections);

/**
 * @brief      设置额外选择集（对标 QPlainTextEdit::setExtraSelections）。
 * @details    整体替换内部额外选择集（拷贝 entries 内容），并请求
 *             重绘。
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
 * @details    Qt 按类型（QTextDocument::ResourceType）与名称从资源
 *             缓存载入并以 QVariant 返回；平铺模型无资源存储，与 Qt
 *             默认实现"找不到返回无效 QVariant"对齐，恒返回 NULL。
 * @note       状态承载：如需资源注入，待引入 addResource 类接口后
 *             再扩展本函数查表实现。
 * @param      self 目标控件指针；可为 NULL。
 * @param      name 资源名称；可为 NULL。
 * @return     恒返回 NULL（未找到）。
 */
XVariant* XPlainTextEdit_loadResource(XPlainTextEdit* self, int type,
                                      const char* name);

/**
 * @brief      返回文本光标（对标 QPlainTextEdit::textCursor 承载版）。
 * @details    平铺模型无 QTextCursor 类，以 XPoint 承载光标位置：
 *             x = 行号（0 起），y = 列（行内 UTF-8 字节偏移）；与
 *             setTextCursor(line, col)、cursorLine/cursorColumn 同源。
 * @param      self 目标控件指针；NULL 时返回 {0,0}。
 * @return     光标位置承载（x = 行，y = 列）。
 */
XPoint XPlainTextEdit_textCursor(const XPlainTextEdit* self);

/**
 * @brief      放大字体（对标 QPlainTextEdit::zoomIn）。
 * @details    控件字体点大小与像素字号同步增加 range（下限钳位 1），
 *             像素字号同步增减以保证点阵渲染路径视觉生效。
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
 * @brief      查询是否存在激活选区（对标选区访问器 hasSelectedText；
 *             Qt 中 QPlainTextEdit 经 textCursor().hasSelection() 承载）。
 * @details    平铺模型以 m_selectionActive 单标志承载选区激活态：
 *             selectAll 置位，文本整体重建/撤销/重做/剪切清除。
 * @param      self 目标控件指针；NULL 时返回 false。
 * @return     存在激活选区返回 true。
 */
bool XPlainTextEdit_hasSelectedText(const XPlainTextEdit* self);

/**
 * @brief      导出选中文本（对标选区访问器 selectedText 的平铺承载）。
 * @details    基于 m_selectionActive/m_cursorLine/Col：平铺模型下选区
 *             为当前行起点至光标，即当前行行内 [0, cursorCol) 字节区间，
 *             返回该片段的堆拷贝。
 * @note       简化实现：Qt 选区由 QTextCursor 锚点与位置构成、可跨行；
 *             此处仅承载"当前行起点至光标"的单行片段。selectAll 会将
 *             光标复位 (0,0)，按此口径无片段可导出（返回 NULL），
 *             全量文本语义由 copy 路径单独承载。
 * @param      self 目标控件指针；NULL 或无激活选区时返回 NULL。
 * @return     堆拷贝（UTF-8，NUL 结尾），调用方以 XFree_System 释放；
 *             分配失败返回 NULL。
 */
char* XPlainTextEdit_selectedText(const XPlainTextEdit* self);

/* ==================== 信号 ==================== */

/**
 * @brief      文本变化信号（真发射）。
 */
void* XPlainTextEdit_textChanged_signal(XPlainTextEdit* self);
/**
 * @brief      光标位置变化信号（真发射）。
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
 * @details    真发射：选区激活状态翻转的路径（selectAll 置位、文本
 *             整体重建/撤销/重做/剪切清除选区）经 XObject_emitSignal
 *             通知已连接槽；无参数。
 * @param      self 目标控件指针；可为 NULL。
 * @return     不透明的 selectionChanged 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XPlainTextEdit_selectionChanged_signal(XPlainTextEdit* self);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XPLAINTEXTEDIT_ON */

#endif /* XPLAINTEXTEDIT_H */