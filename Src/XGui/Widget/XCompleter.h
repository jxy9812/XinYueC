/**
 * @file       XCompleter.h
 * @brief      XCompleter 补全对象（对标 Qt 6.8 QCompleter 核心公共 API；
 *             QCompleter : QObject，本类 XObject 派生）。
 * @details    功能范围：
 *             - 模型：model/setModel（XAbstractItemModel* 借用，不拥有）；
 *             - 补全属性：completionMode/setCompletionMode、
 *               completionPrefix/setCompletionPrefix、
 *               completionColumn/setCompletionColumn、
 *               caseSensitivity/setCaseSensitivity、
 *               filterMode/setFilterMode、maxVisibleItems/setMaxVisibleItems、
 *               completionRole/setCompletionRole（仅存储）、
 *               modelSorting/setModelSorting（声明排序模型后，StartsWith
 *               前缀过滤可用二分定位候选区间）、
 *               wrapAround/setWrapAround（默认 true，setCurrentRow 越界
 *               按环绕归一）、popup/setPopup（弹出列表为自绘非部件承载，
 *               仅保存借用指针，@note）、widget/setWidget（借用）；
 *             - 结果：complete() 重建完成列表、completionCount()、
 *               completionModel()（无代理模型，返回底层模型借用，@note）、
 *               setCurrentRow()（返回是否成功）、currentCompletion/
 *               currentRow/currentIndex、pathFromIndex(row)（取该行
 *               补全文本拷贝）、splitPath(path)（拆目录/文件名，
 *               签名差异见声明处 @note）；
 *             - 信号：activated(text)/textActivated(text)/
 *               highlighted(text)/textHighlighted(text)/
 *               highlightedRow(int)/activatedIndex(row,col) 简化。
 * @note       枚举数值裁决：Qt 6.8.3 qcompleter.h 中 CompletionMode
 *             实际顺序为 PopupCompletion=0、UnfilteredPopupCompletion=1、
 *             InlineCompletion=2（本头文件按 Qt 源码对齐，任务书初稿
 *             顺序与之相反，以 Qt 源码为准）。FilterMode 在 Qt 6.8.3
 *             尚不存在（Qt 6.9 引入，Qt 6.8 用 Qt::MatchFlags 位掩码），
 *             本类按任务书三值枚举简化并提供 @note。
 * @note       XAbstractItemModel 被 XWIDGET_ON&&XTABLEWIDGET_ON 门控，
 *             本类使用相同门控条件编译。
 * @author     XinYueC 团队
 */
#ifndef XCOMPLETER_H
#define XCOMPLETER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XString.h"
#include "XChar.h"
#include "XWidget.h"
#include "XAbstractItemModel.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 枚举 ==================== */

/**
 * @brief      补全模式（对标 Qt 6.8 QCompleter::CompletionMode，数值
 *             按 Qt 源码逐项一致）。
 * @details    PopupCompletion 弹出补全列表；UnfilteredPopupCompletion
 *             弹出不过滤的列表；InlineCompletion 在编辑行内联补全。
 *             本实现仅存储枚举，弹出/内联 UI 行为由 XGui 控件层接入
 *             （当前仅存储与结果计算，@note）。
 */
typedef enum XCompleterCompletionMode
{
    XCompleterCompletionMode_PopupCompletion = 0,           /**< 弹出补全（对标 PopupCompletion）。 */
    XCompleterCompletionMode_UnfilteredPopupCompletion = 1, /**< 弹出不过滤补全（对标 UnfilteredPopupCompletion）。 */
    XCompleterCompletionMode_InlineCompletion = 2           /**< 行内补全（对标 InlineCompletion）。 */
} XCompleterCompletionMode;

/**
 * @brief      过滤模式（任务书三值枚举简化）。
 * @details    Qt 6.8.3 使用 Qt::MatchFlags 位掩码（MatchStartsWith=1/
 *             MatchContains=2/MatchEndsWith=4，可组合）；本类简化为
 *             三值互斥枚举，数值 0/1/2 为自定义顺序（非 Qt 位值，
 *             @note）。
 */
typedef enum XCompleterFilterMode
{
    XCompleterFilterMode_StartsWith = 0, /**< 前缀匹配（简化 FilterModeStartsWith）。 */
    XCompleterFilterMode_Contains = 1,   /**< 包含匹配（简化 FilterModeContains）。 */
    XCompleterFilterMode_EndsWith = 2    /**< 后缀匹配（简化 FilterModeEndsWith）。 */
} XCompleterFilterMode;

/**
 * @brief      模型排序假设（对标 Qt 6.8 QCompleter::ModelSorting，数值
 *             逐项一致）。
 * @details    UnsortedModel 默认，不假设模型有序，前缀过滤逐行线性扫描；
 *             CaseSensitivelySortedModel / CaseInsensitivelySortedModel
 *             声明 completionColumn 列按升序排列，此时 StartsWith 前缀
 *             过滤改用二分定位候选区间（大幅降低大模型扫描量）。
 *             二分仅在 filterMode 为 StartsWith、前缀非空、且声明的大小写
 *             排序方式与 caseSensitivity 一致时生效（对齐 Qt 关于
 *             caseSensitivity 不一致时无法加速的约束）；Contains/EndsWith
 *             或空前缀回退线性扫描。模型实际无序时结果未定义（与 Qt
 *             相同，排序由调用方保证）。
 */
typedef enum XCompleterModelSorting
{
    XCompleterModelSorting_UnsortedModel = 0,               /**< 模型无序（默认）。 */
    XCompleterModelSorting_CaseSensitivelySortedModel = 1,  /**< 按大小写敏感升序排序。 */
    XCompleterModelSorting_CaseInsensitivelySortedModel = 2 /**< 按大小写不敏感升序排序。 */
} XCompleterModelSorting;

/* ==================== 类虚函数表 ==================== */

/**
 * @brief      XCompleter 类虚函数表。
 * @details    不新增虚函数槽位，直接继承 XObject 的事件槽位，并重载
 *             XClass 的 Deinit 以释放补全文本资源。
 */
XCLASS_DEFINE_BEGING(XCompleter)
XCLASS_DEFINE_EXTEND_END(XCompleter, XObject)

/* ==================== 补全对象（对标 Qt 6.8 QCompleter） ==================== */

/**
 * @brief      XCompleter 补全对象。
 * @details    m_base 是第一个成员；m_model/m_widget/m_popup 为借用指针；
 *             m_prefix/m_currentCompletion 为对象拥有的 XString；
 *             m_matches 为命中行号数组（int 元素，对象拥有）。
 */
/* 前置声明（默认弹层借用指针；完整类型见 XListWidget.h）。 */
typedef struct XListWidget XListWidget;

typedef struct XCompleter
{
    XObject                    m_base;            /**< 基类成员；必须是第一个，由 XClass 管理。 */
    XAbstractItemModel*        m_model;           /**< 补全数据模型（借用，不拥有）。 */
    XWidget*                   m_widget;          /**< 关联编辑控件（借用，不拥有）。 */
    XWidget*                   m_popup;           /**< 弹出视图借用指针（默认 NULL；自绘弹出列表不使用，不拥有）。 */
    XListWidget*               m_defaultPopup;    /**< 内建默认弹层（懒建于首次匹配；挂编辑框顶层窗口，随顶层析构，本类不拥有）。 */
    XTimerId                   m_popupGuardTimer; /**< 弹层守护巡检定时器（仅弹层可见期间运行：编辑框隐藏/焦点离场→收层；无巡检为 XTIMER_INVALID_ID）。 */
    XCompleterCompletionMode   m_completionMode;  /**< 补全模式（默认 PopupCompletion）。 */
    XCompleterFilterMode       m_filterMode;      /**< 过滤模式（默认 StartsWith）。 */
    XCompleterModelSorting     m_modelSorting;    /**< 模型排序假设（默认 UnsortedModel）。 */
    int                        m_completionColumn;/**< 补全列（默认 0）。 */
    int                        m_completionRole;  /**< 补全角色（仅存储；默认 -1）。 */
    XChar_CaseSensitivity      m_caseSensitivity; /**< 大小写敏感性（默认 CaseSensitive）。 */
    int                        m_maxVisibleItems; /**< 最大可见条目（默认 7）。 */
    bool                       m_wrapAround;      /**< 补全列表首尾环绕（默认 true，对齐 Qt）。 */
    XString*                   m_prefix;          /**< 补全前缀（对象拥有）。 */
    XVector*                   m_matches;         /**< 命中源模型行号数组（int 元素，对象拥有）。 */
    int                        m_currentRow;      /**< 完成列表中的当前行（0 基索引；无命中为 -1）。 */
    XString*                   m_currentCompletion; /**< 当前补全文本（对象拥有）。 */
} XCompleter;

/* ==================== 生命周期（对标 QCompleter 构造） ==================== */

/**
 * @brief      初始化并返回 XCompleter 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；失败返回 NULL。
 */
XVtable* XCompleter_class_init(void);

/**
 * @brief      默认初始化嵌入式 XCompleter 对象。
 * @param      self 待初始化的可写对象存储；不可为 NULL。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     无返回值。
 */
void XCompleter_init(XCompleter* self, XObject* parent);

/**
 * @brief      使用默认内存类型创建补全对象（对标 QCompleter(parent)）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。成功后必须
 *             XCompleter_delete_base 释放。
 */
#define XCompleter_create(parent) \
    XCompleter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
XCompleter* XCompleter_create_ex(XMemoryType memory, XObject* parent);

/**
 * @brief      带模型创建补全对象（对标 QCompleter(model,parent)）。
 * @param      model 数据模型借用指针；可为 NULL（之后 setModel）。
 * @param      parent 父对象借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。
 */
#define XCompleter_create_2(model, parent) \
    XCompleter_create_2_ex(XCLASS_DEFAULT_MEMORY_TYPE, (model), (parent))
XCompleter* XCompleter_create_2_ex(XMemoryType memory,
                                   XAbstractItemModel* model,
                                   XObject* parent);

#define XCompleter_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XCompleter_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 模型与属性（对标 QCompleter） ==================== */

/**
 * @brief      设置补全数据模型（对标 QCompleter::setModel）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      model 模型借用指针；可为 NULL（清空）。
 * @return     无返回值。
 */
void XCompleter_setModel(XCompleter* self, XAbstractItemModel* model);

/**
 * @brief      查询补全数据模型（对标 QCompleter::model）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     模型借用指针；self 为 NULL 或未设置返回 NULL。
 */
XAbstractItemModel* XCompleter_model(const XCompleter* self);

/**
 * @brief      设置补全模式（对标 QCompleter::setCompletionMode）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      mode XCompleterCompletionMode 枚举值。
 * @return     无返回值。
 */
void XCompleter_setCompletionMode(XCompleter* self,
                                  XCompleterCompletionMode mode);

/**
 * @brief      查询补全模式（对标 QCompleter::completionMode）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     补全模式枚举值；self 为 NULL 返回 PopupCompletion。
 */
XCompleterCompletionMode XCompleter_completionMode(const XCompleter* self);

/**
 * @brief      设置补全前缀（对标 QCompleter::setCompletionPrefix）。
 * @details    深拷贝前缀并重建完成列表；命中首项变化时按 Qt 发射点
 *             发射 highlighted(text)/textHighlighted(text)。
 * @param      self 目标补全对象；可为 NULL。
 * @param      prefix 源前缀借用指针；可为 NULL 表示空前缀。
 * @return     无返回值。
 */
void XCompleter_setCompletionPrefix(XCompleter* self,
                                    const XString* prefix);

/**
 * @brief      设置补全前缀（UTF-8 兼容重载）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      utf8 以 '\0' 结尾的 UTF-8 前缀；可为 NULL。
 * @return     无返回值。
 */
void XCompleter_setCompletionPrefix_2(XCompleter* self, const char* utf8);

/**
 * @brief      查询补全前缀的拷贝（对标 QCompleter::completionPrefix）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self 为 NULL 或空前缀返回 NULL。
 */
XString* XCompleter_completionPrefix(const XCompleter* self);

/**
 * @brief      设置补全列（对标 QCompleter::setCompletionColumn）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      column 列号。
 * @return     无返回值。
 */
void XCompleter_setCompletionColumn(XCompleter* self, int column);

/**
 * @brief      查询补全列（对标 QCompleter::completionColumn）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     列号；self 为 NULL 返回 0。
 */
int XCompleter_completionColumn(const XCompleter* self);

/**
 * @brief      设置补全角色（对标 QCompleter::setCompletionRole）。
 * @details    本类仅存储不参与读取（XAbstractItemModel_data 无 role
 *             参数，@note）；默认 -1。
 * @param      self 目标补全对象；可为 NULL。
 * @param      role 角色号。
 * @return     无返回值。
 */
void XCompleter_setCompletionRole(XCompleter* self, int role);

/**
 * @brief      查询补全角色（对标 QCompleter::completionRole）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     角色号；self 为 NULL 返回 -1。
 */
int XCompleter_completionRole(const XCompleter* self);

/**
 * @brief      设置大小写敏感性（对标 QCompleter::setCaseSensitivity）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      cs XChar_CaseSensitive / XChar_CaseInsensitive。
 * @return     无返回值。
 */
void XCompleter_setCaseSensitivity(XCompleter* self,
                                   XChar_CaseSensitivity cs);

/**
 * @brief      查询大小写敏感性（对标 QCompleter::caseSensitivity）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     大小写敏感性枚举；self 为 NULL 返回 XChar_CaseSensitive。
 */
XChar_CaseSensitivity XCompleter_caseSensitivity(const XCompleter* self);

/**
 * @brief      设置过滤模式（任务书三值枚举）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      mode XCompleterFilterMode 枚举值。
 * @return     无返回值。
 */
void XCompleter_setFilterMode(XCompleter* self, XCompleterFilterMode mode);

/**
 * @brief      查询过滤模式。
 * @param      self 目标补全对象；可为 NULL。
 * @return     过滤模式枚举；self 为 NULL 返回 StartsWith。
 */
XCompleterFilterMode XCompleter_filterMode(const XCompleter* self);

/**
 * @brief      设置模型排序假设（对标 QCompleter::setModelSorting）。
 * @details    切换排序假设后立即重建完成列表：声明为按大小写敏感/不敏感
 *             升序时，StartsWith 前缀过滤改用二分查找定位候选区间，
 *             降低大模型扫描量；其余情况回退线性扫描（@note 见枚举
 *             注释与实现）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      sorting XCompleterModelSorting 枚举值。
 * @return     无返回值。
 */
void XCompleter_setModelSorting(XCompleter* self,
                                XCompleterModelSorting sorting);

/**
 * @brief      查询模型排序假设（对标 QCompleter::modelSorting）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     排序假设枚举；self 为 NULL 返回 UnsortedModel。
 */
XCompleterModelSorting XCompleter_modelSorting(const XCompleter* self);

/**
 * @brief      设置补全列表是否首尾环绕（对标 QCompleter::setWrapAround）。
 * @details    Qt 默认 true（补全列表在末项继续到首行）；本实现把该语义
 *             落在 setCurrentRow 的越界归一上（XGui 无弹出视图承载
 *             键盘导航，@note）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      wrap true 启用环绕，false 关闭。
 * @return     无返回值。
 */
void XCompleter_setWrapAround(XCompleter* self, bool wrap);

/**
 * @brief      查询补全列表是否首尾环绕（对标 QCompleter::wrapAround）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     启用返回 true；self 为 NULL 返回 true（Qt 默认值）。
 */
bool XCompleter_wrapAround(const XCompleter* self);

/**
 * @brief      设置最大可见条目数（对标 QCompleter::setMaxVisibleItems）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      maxItems 最大条目数（<=0 视为 0）。
 * @return     无返回值。
 */
void XCompleter_setMaxVisibleItems(XCompleter* self, int maxItems);

/**
 * @brief      查询最大可见条目数（对标 QCompleter::maxVisibleItems）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     最大条目数；self 为 NULL 返回 7。
 */
int XCompleter_maxVisibleItems(const XCompleter* self);

/**
 * @brief      设置关联编辑控件（对标 QCompleter::setWidget）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      widget 控件借用指针；可为 NULL。
 * @return     无返回值。
 */
void XCompleter_setWidget(XCompleter* self, XWidget* widget);

/**
 * @brief      查询关联编辑控件（对标 QCompleter::widget）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     控件借用指针；self 为 NULL 或未设置返回 NULL。
 */
XWidget* XCompleter_widget(const XCompleter* self);

/**
 * @brief      设置补全弹出视图（对标 QCompleter::setPopup）。
 * @details    XGui 的补全弹出列表为自绘非部件承载（无 QAbstractItemView
 *             部件体系），本函数仅把借用指针保存进对象，不参与绘制与
 *             交互，也不获得所有权；传入的视图指针在补全器存活期内必须
 *             保持有效。传 NULL 清除。@note 与 Qt 的差异：Qt 会把视图
 *             接管为弹出部件并同步代理模型，本实现不做部件化接管。
 * @param      self 目标补全对象；可为 NULL。
 * @param      popup 弹出视图借用指针；可为 NULL（清除）。
 * @return     无返回值。
 */
void XCompleter_setPopup(XCompleter* self, XWidget* popup);

/* ==================== 补全结果（对标 QCompleter） ==================== */

/**
 * @brief      重建完成列表并更新当前补全（对标 QCompleter::complete()）。
 * @details    从模型 completionColumn 列读取文本，按 filterMode 过滤
 *             前缀（声明 modelSorting 为有序且满足 StartsWith/大小写
 *             一致性时用二分定位候选区间，否则逐行线性扫描）；命中首项
 *             写入 currentCompletion，currentRow 置 0（无命中置 -1）；
 *             命中首项变化时发射 highlighted(text)/textHighlighted(text)。
 * @param      self 目标补全对象；可为 NULL。
 * @return     无返回值。
 */
void XCompleter_complete(XCompleter* self);
/** @brief      隐藏补全弹层（内建默认弹层或外接弹层；对标 QCompleter
 *              popup 在 Esc/失焦时的隐藏语义）。
 * @details     收层联动解除弹层鼠标抓取并停守护巡检（幂等，可对已
 *              收层状态重复调用）。
 * @param      self 补全对象；可为 NULL。
 * @return     无返回值。
 */
void XCompleter_hidePopup(XCompleter* self);

/**
 * @brief      查询当前补全文本的拷贝（对标 QCompleter::currentCompletion）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；无命中或 self 为 NULL 返回 NULL。
 */
XString* XCompleter_currentCompletion(const XCompleter* self);

/**
 * @brief      查询完成列表中的当前行（对标 QCompleter::currentRow）。
 * @details    Qt 语义：currentRow 是补全模型（完成列表）中的行，
 *             0 基索引；源模型行号经 currentIndex 获取。
 * @param      self 目标补全对象；可为 NULL。
 * @return     完成列表行号（0 基）；无命中或 self 为 NULL 返回 -1。
 */
int XCompleter_currentRow(const XCompleter* self);

/**
 * @brief      查询当前命中索引（对标 QCompleter::currentIndex 简化）。
 * @details    返回当前完成项对应的源模型行号，列恒为
 *             completionColumn（模型扁平，@note 简化）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     源模型行号；无命中或 self 为 NULL 返回 -1。
 */
int XCompleter_currentIndex(const XCompleter* self);

/**
 * @brief      查询当前前缀的补全候选数量（对标 QCompleter::completionCount）。
 * @details    返回 m_matches 的元素个数（complete()/setCompletionPrefix()
 *             已按当前前缀重建）；空前缀时为模型全部有效行数。Qt 在
 *             大模型上按需过滤，本实现已在重建时一次性算完，故为常数
 *             时间查询（@note）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     候选数量（>=0）；self 为 NULL 或未命中返回 0。
 */
int XCompleter_completionCount(const XCompleter* self);

/**
 * @brief      设置完成列表中的当前行（对标 QCompleter::setCurrentRow）。
 * @details    成功时把 currentRow 置为 row，并把 currentCompletion 更新
 *             为该项的补全文本（等价 Qt 由 currentIndex/pathFromIndex
 *             派生 currentCompletion 的效果）。
 *             越界语义：wrapAround 为 true（默认）时按候选数量取模归一
 *             （row<0 从末项往前、row>=count 回到首项），对齐 Qt 补全列表
 *             首尾环绕的导航语义；wrapAround 为 false 时越界返回 false
 *             且不改动状态。@note Qt 的 setCurrentRow 本身不环绕，环绕
 *             发生在弹出视图的按键导航中；XGui 无弹出视图，故把环绕
 *             语义直接落在本函数（见 wrapAround）。
 * @param      self 目标补全对象；可为 NULL。
 * @param      row 目标完成列表行号（0 基；可越界，按 wrapAround 处理）。
 * @return     设置成功返回 true；self 为 NULL、无候选或未启用环绕且
 *             越界返回 false。
 */
bool XCompleter_setCurrentRow(XCompleter* self, int row);

/**
 * @brief      查询补全模型（对标 QCompleter::completionModel）。
 * @details    Qt 返回补全过滤后的内部代理模型（QAbstractProxyModel）；
 *             XGui 不新建代理模型，直接返回底层 setModel() 的模型借用
 *             指针（@note 差异：调用方看到的仍是完整源模型，候选过滤
 *             结果请用 completionCount()/currentCompletion() 或
 *             currentIndex() 获取）。返回指针不转移所有权。
 * @param      self 目标补全对象；可为 NULL。
 * @return     底层模型借用指针；self 为 NULL 或未设置返回 NULL。
 */
XAbstractItemModel* XCompleter_completionModel(const XCompleter* self);

/**
 * @brief      取指定模型行的补全文本（对标 QCompleter::pathFromIndex）。
 * @details    读取底层模型第 row 行、completionColumn 列的单元格文本
 *             并返回其拷贝（等价 Qt 默认实现取 completionRole 数据）。
 *             @note 签名差异：Qt 参数为 QModelIndex（可含父级/列），XGui
 *             无索引体系，改用源模型行号 int；row 与 currentIndex() 的
 *             返回值同一坐标系。
 * @param      self 目标补全对象；可为 NULL。
 * @param      row 源模型行号（0 基）。
 * @return     新建的 XString 拷贝，由调用方拥有，使用后必须
 *             XString_delete_base；self/模型/单元格无效或 row 越界返回
 *             NULL。
 */
XString* XCompleter_pathFromIndex(const XCompleter* self, int row);

/**
 * @brief      把路径拆分为目录与文件名（对标 QCompleter::splitPath）。
 * @details    按 POSIX 分隔符 '/' 拆分：最后一个 '/' 之前的全部字节为
 *             目录（不含分隔符），之后为文件名；无 '/' 时目录为空串、
 *             文件名为整个路径；以 '/' 结尾时文件名为空串。两个输出均为
 *             新建 XString，调用方拥有并负责 XString_delete_base。
 *             @note 签名差异：Qt 返回 QStringList（逐级匹配段，且文件
 *             系统模型下按本机分隔符拆分）；XGui 无 QFileSystemModel，
 *             改为 (目录, 文件名) 双输出参数形式，仅识别 '/'。
 * @param      self 目标补全对象；当前实现不使用（保留 Qt 成员函数
 *             形态）；可为 NULL。
 * @param      path 待拆分路径（XString）；可为 NULL（等价空路径）。
 * @param      dirOut 输出目录（新建 XString）；可为 NULL 表示不接收。
 * @param      fileOut 输出文件名（新建 XString）；可为 NULL 表示不接收。
 * @return     无返回值；输出分配失败时对应指针置 NULL。
 */
void XCompleter_splitPath(const XCompleter* self, const XString* path,
                          XString** dirOut, XString** fileOut);

/**
 * @brief      把路径拆分为目录与文件名（UTF-8 兼容重载）。
 * @details    仅创建临时 XString 并转发 XCompleter_splitPath，语义与
 *             输出所有权完全相同。
 * @param      self 目标补全对象；当前实现不使用；可为 NULL。
 * @param      path 待拆分路径（UTF-8，NUL 结尾）；可为 NULL。
 * @param      dirOut 输出目录（新建 XString）；可为 NULL 表示不接收。
 * @param      fileOut 输出文件名（新建 XString）；可为 NULL 表示不接收。
 * @return     无返回值；输出分配失败时对应指针置 NULL。
 */
void XCompleter_splitPath_2(const XCompleter* self, const char* path,
                            XString** dirOut, XString** fileOut);

/**
 * @brief      查询补全弹出控件（对标 QCompleter::popup）。
 * @details    返回 setPopup() 保存的借用指针（默认 NULL）。XGui 弹出
 *             列表为自绘非部件承载，返回的指针不参与绘制/交互，仅为
 *             调用方回读而保存（@note）；未调用 setPopup 时恒为 NULL，
 *             与既有回归断言一致。
 * @param      self 目标补全对象；可为 NULL。
 * @return     setPopup 保存的视图借用指针；未设置或 self 为 NULL 返回
 *             NULL。
 */
XWidget* XCompleter_popup(const XCompleter* self);

/* ==================== 信号（对标 QCompleter signals，简化命名） ==================== */

/**
 * @brief      补全激活信号（对标 QCompleter::activated(const QString&)）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      text 激活的补全文本借用指针。
 * @return     不透明的 activated 信号标识。
 */
void* XCompleter_activated_signal(XCompleter* self, const XString* text);

/**
 * @brief      补全文本激活信号（与 activated 等价，兼容旧命名习惯）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      text 激活的补全文本借用指针。
 * @return     不透明的 textActivated 信号标识。
 */
void* XCompleter_textActivated_signal(XCompleter* self, const XString* text);

/**
 * @brief      补全高亮信号（对标 QCompleter::highlighted(const QString&)）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      text 高亮的补全文本借用指针。
 * @return     不透明的 highlighted 信号标识。
 */
void* XCompleter_highlighted_signal(XCompleter* self, const XString* text);

/**
 * @brief      补全文本高亮信号（与 highlighted 等价，兼容旧命名习惯）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      text 高亮的补全文本借用指针。
 * @return     不透明的 textHighlighted 信号标识。
 */
void* XCompleter_textHighlighted_signal(XCompleter* self,
                                        const XString* text);

/**
 * @brief      高亮行变化信号（对标 highlighted(const QModelIndex&) 的行
 *             简化）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      row 高亮的模型行号。
 * @return     不透明的 highlightedRow 信号标识。
 */
void* XCompleter_highlightedRow_signal(XCompleter* self, int row);

/**
 * @brief      激活索引信号（对标 activated(const QModelIndex&) 的
 *             (row,col) 简化）。
 * @param      self 发射信号的对象；可为 NULL。
 * @param      row 激活的模型行号。
 * @param      col 激活的模型列号。
 * @return     不透明的 activatedIndex 信号标识。
 */
void* XCompleter_activatedIndex_signal(XCompleter* self, int row, int col);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XCOMPLETER_H */
