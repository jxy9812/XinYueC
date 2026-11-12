/**
 * @file       XTextCursor.h
 * @brief      XTextCursor 可编辑文本光标（对标 Qt 6.8 QTextCursor，有界子集）。
 * @details    QTextCursor 是「文档句柄 + position/anchor + currentCharFormat
 *             + 垂直移动 sticky x」的轻量对象（qtextcursor_p.h:64-77 的
 *             QTextCursorPrivate 平铺），本类以**值对象**承载同一状态：
 *             - 持有 XTextDocument*（**借用，不拥有**——Qt 的 d->priv 同位；
 *               文档析构后光标不得再使用，isNull 防不了这一手，见 @warning）；
 *             - m_position / m_anchor：文档绝对 UTF-8 字节位置（文档口径：
 *               每块文本字节 + 1 个块分隔符；合法区间 [0, characterCount-1]，
 *               Qt length()-1 同口径）；
 *             - m_insertFormat：插入格式缓存（Qt currentCharFormat 索引的
 *               C 承载——**按值存内联 XTDCharFormat**，勘察结论：插入格式要
 *               直接落到片段内联字段，池索引属片段层去重、光标层持有索引
 *               反而要经两次转换，按值拷贝无往返损失；m_hasInsertFormat=0
 *               时插入按 charFormat() 继承规则取格式）；
 *             - m_x：垂直移动 sticky x（Qt x 字段；-1=脏，下次 Up/Down 按当前
 *               列重算——Qt setX 的惰性等价，qtextcursor.cpp:96-112）。
 *
 *             **Qt 6.8 语义逐条对齐**（行号为 v6.8.3 原文；行为经本机
 *             Qt 6.8.0 offscreen 探针取证）：
 *             - 位置/选区：setPosition 越界忽略（qtextcursor.cpp:1121-1144
 *               探针：pos==characterCount 与负值均不动）；KeepAnchor 只动
 *               position 不动 anchor；selectionStart/End 取 min/max
 *               （:1651-1669）；clearSelection 折叠 anchor 到 position
 *               （:1620-1626）；跨块 selectedText 的块分隔符为 **U+2029**
 *               （:1691-1696 文档注 + 探针 P1 [0062 2029 0063 0064]——
 *               Qt 真语义优先于任务书的 '\n' 表述，冲突点已在台账登记）；
 *             - 移动：movePosition(op, mode, n) 逐条对齐
 *               QTextCursorPrivate::movePosition（:304-633）——Start/
 *               StartOfLine/End/EndOfLine 强制 n=1（:1226-1234）；返回值
 *               =「position 实际移动」（qtextcursor_p.h:69-76 的 moved），
 *               边界（块 0 上移/末块下移/越界）返回 false 且不动；Left/
 *               Right 在有选区时 MoveAnchor 折叠到选区两端（:384-391/
 *               :507-514 探针 P4）；Up/Down 经行布局 xToCursor 保持列，
 *               sticky x 一次计算跨多次垂直移动保持（:412-447/:520-557，
 *               adjustX=false）；词移动按 Qt 词边界（空白/词分隔符集合
 *               qtextengine.cpp:2845-2886）块内跳词、块界跨到分隔符位
 *               （qtextdocument_p.cpp:1332-1357）；
 *             - 插入：insertText 有选区先删选区（:1408-1414）；无显式格式
 *               继承 charFormat()——块中/尾继承前一字符、块首继承块首字符、
 *               空块继承前块末字符（:1857-1889 + 探针 P2/P2b/P2d/P13）；
 *               '\n'/U+2029/'\r\n' 拆块插入（:1441-1457 探针 P3/N4）；
 *               插入后光标落在插入文本末尾（探针 P3 pos=5）；
 *             - 删除：removeSelectedText 后光标与锚点折叠到**选区起点**
 *               （:114-142 + 探针 P6/P6b 两方向同值）；deleteChar 删光标后
 *               一字符、光标不动（:1473-1489 探针 P5a）；deletePreviousChar
 *               删前一字符、光标回退一位（:1497-1526）；边界（文档首回格/
 *               文档尾前删）no-op 不占撤销步；
 *             - 格式：setCharFormat 无选区只设插入格式（:1898-1907 +
 *               探针 P2c/N5——移动后缓存失效重算），有选区替换选区内全部
 *               片段（探针 P11 三段格式）；mergeCharFormat 在属性空间合并
 *               （W1 XTextFormat_merge 同款语义）；块格式作用于选区内
 *               全部块（:743-784 探针 P12 三块同改）；一律经文档格式池
 *               upsert，保持内联 XTDCharFormat 与池影子一致（渲染路径仍读
 *               内联字段）；
 *             - 撤销事务：beginEditBlock/endEditBlock 块内编辑合并一步撤销、
 *               信号合并一区间（qtextdocument_p.cpp:1149-1175 + 探针 P8/N8）；
 *               undo/redo 后**文档级活动光标**（m_cursorPosition）按 Qt
 *               adjustPosition 口径映射（qtextcursor.cpp:57-94；探针 P7/
 *               N1/N2a/N2c/N2d：undo 插入→光标落插入点、undo 前向删除→
 *               原位不动、undo 回格→退到删除前位置、redo→落到重插入尾）。
 *
 *             **与 Qt 的已知差异**（有界子集，头注释即契约）：
 *             - QTextCursor 是注册制共享句柄（文档持有全部活光标并在编辑/
 *               撤销时逐个调整）；本类是值对象，文档级自动跟随由**活动光标**
 *               m_cursorPosition 承载（XTextDocument_cursorPosition），本
 *               光标对象自身的 position 不被外部编辑自动修正——需要 Qt 的
 *               「活光标」语义时重读文档活动光标再 setPosition；
 *             - 表格（hasComplexSelection/selectedTableCells）不在子集：
 *               hasComplexSelection 恒 false（无表格时这是 Qt 的真值，
 *               非占位）；
 *             - 撤销为快照制：undo/redo 恢复纯文本（格式不回放，既有
 *               xtd_restoreSnapshot 口径）；相邻同类编辑 Qt 会 tryMerge
 *               成一命令（qtextdocument_p.cpp:1050-1061），快照制不合并
 *               ——一步撤销粒度为单编辑/单编辑块（台账既有偏差）。
 * @note       模块无独立开关宏：随 XGUI_ON / XTEXTDOCUMENT_ON 裁剪
 *             （XTextFormat.h 同款）。C99。
 * @warning    新增字段一律**尾部追加**并用 init 显式清零（结构体初值铁律：
 *             MSVC /RTC1 下未清零字段读栈垃圾）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTCURSOR_H
#define XTEXTCURSOR_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XTextDocument.h"

#if XTEXTDOCUMENT_ON

/**
 * @brief 移动操作（对标 QTextCursor::MoveOperation，数值逐一对齐；
 *        qtextcursor.h:61-90）。表格族（NextCell/PreviousCell/NextRow/
 *        PreviousRow）与逻辑/视觉分设的 WordLeft/WordRight 不在子集。
 */
typedef enum XTextCursorMoveOperation
{
    XTextCursorMove_NoMove             = 0,  /**< 不动（恒返回 true）。 */
    XTextCursorMove_Start              = 1,  /**< 文档首。 */
    XTextCursorMove_Up                 = 2,  /**< 视觉上一行（经行布局保持列）。 */
    XTextCursorMove_StartOfLine        = 3,  /**< 行首（无折行子集=块首）。 */
    XTextCursorMove_StartOfBlock       = 4,  /**< 块首。 */
    XTextCursorMove_StartOfWord        = 5,  /**< 所在词词首。 */
    XTextCursorMove_PreviousBlock      = 6,  /**< 上一块块首。 */
    XTextCursorMove_PreviousCharacter  = 7,  /**< 前一字符（跨块界）。 */
    XTextCursorMove_PreviousWord       = 8,  /**< 上一词首。 */
    XTextCursorMove_Left               = 9,  /**< 逻辑左（有选区时折叠到选区端）。 */
    XTextCursorMove_End                = 11, /**< 文档尾（length()-1）。 */
    XTextCursorMove_Down               = 12, /**< 视觉下一行（经行布局保持列）。 */
    XTextCursorMove_EndOfLine          = 13, /**< 行尾（无折行子集=块尾）。 */
    XTextCursorMove_EndOfWord          = 14, /**< 所在词词尾。 */
    XTextCursorMove_EndOfBlock         = 15, /**< 块尾（分隔符前）。 */
    XTextCursorMove_NextBlock          = 16, /**< 下一块块首。 */
    XTextCursorMove_NextCharacter      = 17, /**< 后一字符（跨块界）。 */
    XTextCursorMove_NextWord           = 18, /**< 下一词首。 */
    XTextCursorMove_Right              = 19  /**< 逻辑右（有选区时折叠到选区端）。 */
} XTextCursorMoveOperation;

/** @brief 移动模式（对标 QTextCursor::MoveMode，数值一致；qtextcursor.h:47-50）。 */
typedef enum XTextCursorMoveMode
{
    XTextCursorMode_MoveAnchor = 0, /**< 移动并折叠选区（anchor 跟随）。 */
    XTextCursorMode_KeepAnchor = 1  /**< 保持 anchor（拖出选区）。 */
} XTextCursorMoveMode;

/**
 * @brief      可编辑文本光标（值对象，持有堆资源——m_insertFormat 的
 *             字符串字段；init/copy/move/deinit 四件套必须配对）。
 * @warning    新增字段一律**尾部追加** + init 显式清零（结构体初值铁律）。
 */
typedef struct XTextCursor
{
    XTextDocument* m_document; /**< 目标文档（**借用，不拥有**；NULL=无效光标）。 */
    int m_position;            /**< 光标位置（文档字符位置；0 起）。 */
    int m_anchor;              /**< 选区锚点（== position 即无选区）。 */
    XTDCharFormat m_insertFormat; /**< 插入格式缓存（Qt currentCharFormat；
                                       字符串字段归本对象拥有）。 */
    int m_hasInsertFormat;     /**< 插入格式缓存有效位（0=按 charFormat 继承
                                    规则现算；移动/重定位后复位 0——Qt
                                    currentCharFormat=-1 同款失效口径）。 */
    int m_x;                   /**< 垂直移动 sticky x px（-1=脏；Up/Down 使用
                                    并保持，横向移动后置脏——Qt x 字段口径）。 */
} XTextCursor;

/* ==================== 生命周期（值对象四件套） ==================== */

/**
 * @brief      初始化光标（对标 QTextCursor(doc) 构造，qtextcursor.cpp
 *             QTextCursorPrivate::fromPosition(d, 0)：**恒定位于文档首**，
 *             不跟随文档活动光标——需要衔接既有位置时调用方 setPosition
 *             显式定位），
 *             position=anchor=0、插入格式缓存失效、x 脏。
 * @param      self 调用方提供的未初始化存储；NULL 忽略。
 * @param      doc 目标文档（借用）；NULL 合（isNull 光标——仍可 init，
 *             非编辑类查询按 Qt null cursor 返回缺省值）。
 */
void XTextCursor_init(XTextCursor* self, XTextDocument* doc);

/** @brief 值构造（XTextCursor_init 的按值便利版；失败返回 isNull 光标）。 */
XTextCursor XTextCursor_make(XTextDocument* doc);

/** @brief 深拷贝（对标 QTextCursor 拷贝：状态四字段全拷；插入格式的
 *         字符串字段深拷贝；dst 旧载荷先释放）。 */
void XTextCursor_copy(XTextCursor* dst, const XTextCursor* src);

/** @brief 移动（m_insertFormat 载荷转移，src 复位为空壳——空壳仍由调用方
 *         按分配方式释放；本结构是栈/内嵌值，无自释放语义）。 */
void XTextCursor_move(XTextCursor* dst, XTextCursor* src);

/** @brief 释放插入格式字符串字段并复位空壳（幂等；NULL 忽略）。 */
void XTextCursor_deinit(XTextCursor* self);

/* ==================== 状态 ==================== */

/** @brief 是否无效光标（对标 isNull：文档指针为空即 null；Qt !d || !d->priv）。 */
bool XTextCursor_isNull(const XTextCursor* self);

/** @brief 光标位置（文档字符位置；null 光标返回 -1，Qt 同）。 */
int XTextCursor_position(const XTextCursor* self);

/** @brief 选区锚点（null 光标返回 -1）。 */
int XTextCursor_anchor(const XTextCursor* self);

/** @brief 光标所在块号（对标 blockNumber；越尾钳末块；null 返回 0）。 */
int XTextCursor_blockNumber(const XTextCursor* self);

/**
 * @brief      设置光标位置（对标 setPosition(pos, mode)）。
 * @details    越界（pos<0 或 pos>=characterCount）**忽略并保持原位**
 *             （qtextcursor.cpp:1126-1129 qWarning 后 return；本机探针
 *             P10：set 3/set -1 均不动）。MoveAnchor 折叠选区；KeepAnchor
 *             只动 position（探针 P1/P6/N6）。移动使插入格式缓存与 sticky x
 *             失效（Qt currentCharFormat=-1 / setX 口径）。
 * @param      self 目标光标；NULL 或 null 光标忽略。
 * @param      pos 目标位置。
 * @param      mode XTextCursorMoveMode。
 */
void XTextCursor_setPosition(XTextCursor* self, int pos, int mode);

/** @brief 选区起点（无选区=position；null 返回 -1；对标 selectionStart）。 */
int XTextCursor_selectionStart(const XTextCursor* self);

/** @brief 选区终点（无选区=position；null 返回 -1；对标 selectionEnd）。 */
int XTextCursor_selectionEnd(const XTextCursor* self);

/** @brief 是否有选区（position != anchor；对标 hasSelection）。 */
bool XTextCursor_hasSelection(const XTextCursor* self);

/**
 * @brief      是否复杂选区（对标 hasComplexSelection）。
 * @details    恒返回 false：复杂选区=跨表格单元格选区，本子集无表格——
 *             无表格文档上这是 Qt 的真值（qtextcursor.cpp:1583-1589
 *             complexSelectionTable 的空表分支），不是占位。
 */
bool XTextCursor_hasComplexSelection(const XTextCursor* self);

/**
 * @brief      读取选区文本（对标 selectedText）。
 * @details    跨块选区的块分隔符为 **U+2029**（UTF-8 E2 80 A9；Qt 文档注
 *             qtextcursor.cpp:1691-1696 + 探针 P1——任务书 '\n' 表述与
 *             Qt 真语义的冲突点，见台账；re-insert 时 U+2029 与 '\n' 同样
 *             触发拆块，往返无损）。无选区返回 NULL（Qt 空串）。
 * @param      self 目标光标。
 * @return     堆分配 UTF-8 文本（调用方 XFree_System 释放）；无选区/失败 NULL。
 */
char* XTextCursor_selectedText(const XTextCursor* self);

/**
 * @brief      清除选区（anchor 折叠到 position；不删文本；对标 clearSelection）。
 */
void XTextCursor_clearSelection(XTextCursor* self);

/* ==================== 移动 ==================== */

/**
 * @brief      按 MoveOperation 移动光标（对标 movePosition(op, mode, n)）。
 * @details    语义逐条对齐 qtextcursor.cpp:304-633（取证摘要见类头注）：
 *             - Start/StartOfLine/End/EndOfLine 强制 n=1（:1226-1234）；
 *             - 返回值 = position 是否实际移动（qtextcursor_p.h:69-76）——
 *               已在目标位（含边界拒绝）返回 false 且**不折叠选区**；
 *             - Up/Down 经 XTextLayout 行表（lineForTextPosition/colToX/
 *               xToCol）做视觉行移动：sticky x（m_x==-1 时按当前列
 *               colToX 现算）映射到目标块同 x 列（xToCol），一次计算跨
 *               多次垂直移动保持、横向移动/重定位后失效（Qt setX/adjustX
 *               口径；块首上移/末块下移返回 false）；
 *             - 词移动（StartOfWord/EndOfWord/PreviousWord/NextWord）按
 *               Qt 词边界（空白 + qtextengine.cpp:2845 词分隔符集合）在
 *               块内跳词，块界跨到分隔符位（qtextdocument_p.cpp:1332-1357）；
 *             - Left/Right/PreviousCharacter/NextCharacter 有选区且
 *               MoveAnchor 时折叠到选区端（左移取 min、右移取 max，
 *               qtextcursor.cpp:378-391/501-514）。
 * @param      self 目标光标；NULL 或 null 光标返回 false。
 * @param      op XTextCursorMoveOperation。
 * @param      mode XTextCursorMoveMode。
 * @param      n 重复次数（>=1；上述四种定位操作强制 1）。
 * @return     position 实际移动返回 true；否则 false。
 */
bool XTextCursor_movePosition(XTextCursor* self, int op, int mode, int n);

/* ==================== 插入 / 删除 ==================== */

/**
 * @brief      在光标处插入 UTF-8 文本（对标 insertText(text)）。
 * @details    有选区先删选区（同一编辑块，qtextcursor.cpp:1408-1414）；
 *             格式：插入格式缓存有效用缓存，否则按 charFormat() 继承规则
 *             现算（块中/尾继承前一字符、块首继承块首字符、空块继承前块
 *             末字符、文档首空块继承文档缺省——经文档格式池登记，内联与
 *             池影子一致）；'\n'/U+2029/'\r\n' 拆块；插入后光标（与锚点）
 *             落在插入文本末尾并同步文档活动光标（发
 *             cursorPositionChanged）。
 * @param      self 目标光标；NULL/null 光标/空文本（且无选区）忽略。
 * @param      utf8 UTF-8 文本；NULL 按空串（有选区时仍删选区——Qt
 *             insertText("") 探针 N7 口径）。
 */
void XTextCursor_insertText(XTextCursor* self, const char* utf8);

/**
 * @brief      在光标处插入带显式属性格式的文本（对标
 *             insertText(text, format)；Qt 6.8）。
 * @details    属性格式经 XTextDocument_textFormatToCharFormat 转内联落片段
 *             并入池；**本调用同时把该格式记为插入格式缓存**（Qt：插入后
 *             charFormat() 即该格式——片段继承链的等价承载）。
 * @param      self 目标光标。
 * @param      utf8 UTF-8 文本。
 * @param      fmt 字符格式（XTextCharFormat；NULL 等价 insertText）。
 */
void XTextCursor_insertTextFormat(XTextCursor* self, const char* utf8,
                                  const XTextCharFormat* fmt);

/**
 * @brief      在光标处插入块分隔符（对标 insertBlock()）。
 * @details    有选区先删选区（Qt qtextcursor.cpp:2040-2042 的无条件
 *             beginEditBlock → d->remove() → insertBlock 编排；无选区
 *             remove 为 no-op）；原块按光标拆成两块，两块继承原块段属性
 *             （探针 N3b）；光标（与锚点）落**新块块首**——无选区时
 *             pos = 原位+1（探针 P9/N3），有选区时 pos = 选区起点+1
 *             （Qt 探针 select(1,4)+insertBlock ⇒ "a\\nef"、pos=2）。
 * @param      self 目标光标；NULL/null 光标忽略。
 */
void XTextCursor_insertBlock(XTextCursor* self);

/**
 * @brief      删除选区内容（对标 removeSelectedText）；无选区无操作。
 * @details    删除后光标与锚点折叠到选区**起点**（两方向同值，探针 P6/P6b；
 *             op 口径：position 在选区尾时按 MoveCursor——undo 后光标落
 *             起点，探针 N1）。
 * @param      self 目标光标。
 */
void XTextCursor_removeSelectedText(XTextCursor* self);

/**
 * @brief      删除光标后一字符（对标 deleteChar）；有选区时删选区。
 * @details    光标**不动**（op=KeepCursor，探针 P5a；undo 后光标原位，
 *             探针 N2c）；文档尾 no-op 不占撤销步。
 * @param      self 目标光标。
 */
void XTextCursor_deleteChar(XTextCursor* self);

/**
 * @brief      删除光标前一字符（对标 deletePreviousChar）；有选区时删选区。
 * @details    光标回退一位（op=MoveCursor，探针 P5a；undo 后光标回到
 *             删除前位置，探针 N2d）；文档首 no-op 不占撤销步。
 * @param      self 目标光标。
 */
void XTextCursor_deletePreviousChar(XTextCursor* self);

/* ==================== 格式 ==================== */

/**
 * @brief      读取光标处字符格式（对标 charFormat()，属性格式口径）。
 * @details    派生规则（qtextcursor.cpp:1857-1889）：块中/尾=前一字符的
 *             片段格式；块首且块非空=块首字符的片段格式；空块=前块末字符
 *             格式（Qt 分隔符片段格式口径）；文档首空块=文档缺省格式。
 *             本函数不消费插入格式缓存（Qt currentCharFormat 缓存在
 *             QPrivate 层；值光标以 m_hasInsertFormat 承载，仅插入路径
 *             消费）。
 * @param      self 目标光标。
 * @param      out 输出属性格式（先 deinit 旧载荷再接收；NULL 忽略）。
 */
void XTextCursor_charFormat(const XTextCursor* self, XTextCharFormat* out);

/**
 * @brief      读取光标处字符格式（内联 XTDCharFormat 口径——与片段内联
 *             字段同构，渲染/W3 直用；派生规则同 charFormat）。
 * @param      out 输出内联格式；**字符串字段（fontFamily/anchorHref）由本
 *             函数新建**，调用方对旧实例先清（或提供空壳）并负责释放。
 */
void XTextCursor_charFormatInline(const XTextCursor* self, XTDCharFormat* out);

/**
 * @brief      设置字符格式（对标 setCharFormat）。
 * @details    无选区：只记插入格式缓存（后续插入用；不动文档，探针 P2c）；
 *             有选区：选区两端先按字节位拆片段，区间内全部片段替换为该
 *             格式（探针 P11 三段口径），经格式池 upsert 同步影子；
 *             contentsChange 精确区间 (选区起点, 选区长度, 选区长度)
 *             （Qt CharFormatChanged 的 documentChange 口径）。
 * @param      self 目标光标。
 * @param      fmt 字符格式（属性格式）；NULL 忽略。
 */
void XTextCursor_setCharFormat(XTextCursor* self, const XTextCharFormat* fmt);

/**
 * @brief      合并字符格式（对标 mergeCharFormat(modifier)）。
 * @details    有选区：区间内每个片段的格式在**属性空间**与 modifier 合并
 *             （XTextFormat_merge：冲突 modifier 优先，Qt merge 同款）；
 *             无选区：合并进插入格式缓存（Qt MergeFormat 的空选区分支）。
 * @param      self 目标光标。
 * @param      modifier 修饰格式；NULL 忽略。
 */
void XTextCursor_mergeCharFormat(XTextCursor* self, const XTextCharFormat* modifier);

/**
 * @brief      读取光标所在块的段落格式（对标 blockFormat()）。
 * @details    由 XTDBlock 标量字段落属性键：alignment→BlockAlignment、
 *             indentLevel→BlockIndent、headingLevel→HeadingLevel；列表
 *             归属与 CSS 类串不在属性承载内（子集边界）。
 * @param      self 目标光标。
 * @param      out 输出块格式（先 deinit 旧载荷再接收；NULL 忽略）。
 */
void XTextCursor_blockFormat(const XTextCursor* self, XTextBlockFormat* out);

/**
 * @brief      设置段落格式（对标 setBlockFormat；作用于选区内**所有块**，
 *             探针 P12 三块同改）。
 * @details    整值替换口径：alignment←BlockAlignment（未设按缺省）、
 *             indentLevel←BlockIndent、headingLevel←HeadingLevel；列表
 *             归属与 CSS 类串原样保留（属性表不承载，子集边界）。
 * @param      self 目标光标。
 * @param      fmt 块格式；NULL 忽略。
 */
void XTextCursor_setBlockFormat(XTextCursor* self, const XTextBlockFormat* fmt);

/* ==================== 撤销事务 ==================== */

/** @brief 开启编辑块（转发 XTextDocument_beginEditBlock；块内多次编辑合并
 *         一步撤销、信号合并一区间）。 */
void XTextCursor_beginEditBlock(XTextCursor* self);

/** @brief 关闭编辑块（转发 XTextDocument_endEditBlock）。 */
void XTextCursor_endEditBlock(XTextCursor* self);

#ifdef __cplusplus
}
#endif

#endif /* XTEXTDOCUMENT_ON */

#endif /* XGUI_ON */
#endif /* XTEXTCURSOR_H */
