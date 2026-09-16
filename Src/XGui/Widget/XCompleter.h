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
 *               widget/setWidget（借用）；
 *             - 结果：complete() 重建完成列表、currentCompletion/
 *               currentRow/currentIndex、popup()（返回 NULL，@note）；
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
 * @details    m_base 是第一个成员；m_model/m_widget 为借用指针；
 *             m_prefix/m_currentCompletion 为对象拥有的 XString；
 *             m_matches 为命中行号数组（int 元素，对象拥有）。
 */
typedef struct XCompleter
{
    XObject                    m_base;            /**< 基类成员；必须是第一个，由 XClass 管理。 */
    XAbstractItemModel*        m_model;           /**< 补全数据模型（借用，不拥有）。 */
    XWidget*                   m_widget;          /**< 关联编辑控件（借用，不拥有）。 */
    XCompleterCompletionMode   m_completionMode;  /**< 补全模式（默认 PopupCompletion）。 */
    XCompleterFilterMode       m_filterMode;      /**< 过滤模式（默认 StartsWith）。 */
    int                        m_completionColumn;/**< 补全列（默认 0）。 */
    int                        m_completionRole;  /**< 补全角色（仅存储；默认 -1）。 */
    XChar_CaseSensitivity      m_caseSensitivity; /**< 大小写敏感性（默认 CaseSensitive）。 */
    int                        m_maxVisibleItems; /**< 最大可见条目（默认 7）。 */
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

/* ==================== 补全结果（对标 QCompleter） ==================== */

/**
 * @brief      重建完成列表并更新当前补全（对标 QCompleter::complete()）。
 * @details    从模型第一列（completionColumn）逐行读取文本，按
 *             filterMode 过滤前缀；命中首项写入 currentCompletion，
 *             currentRow 置 0（无命中置 -1）；命中首项变化时发射
 *             highlighted(text)/textHighlighted(text)。
 * @param      self 目标补全对象；可为 NULL。
 * @return     无返回值。
 */
void XCompleter_complete(XCompleter* self);

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
 * @brief      查询补全弹出控件（对标 QCompleter::popup）。
 * @details    本子批未实现弹出控件，恒返回 NULL（@note 裁剪说明）。
 * @param      self 目标补全对象；可为 NULL。
 * @return     NULL。
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
