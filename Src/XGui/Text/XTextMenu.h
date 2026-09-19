/**
 * @file       XTextMenu.h
 * @brief      XTextMenu 标准编辑右键菜单构建器（XGui 文本共享服务层）。
 * @details    把 XLineEdit_createStandardContextMenu 与
 *             XPlainTextEdit_createStandardContextMenu 两份同型实现收敛
 *             为控件无关的构建器：菜单结构、条目文案、分隔线位置、
 *             "qt_edit_menu" 对象名与启用态灰化判定完全沿用两版基准的
 *             并集，差异点（是否含"删除"项、readOnly 收缩结构）由回调
 *             表条目裁剪表达：
 *             - 动作回调（undo/redo/cut/copy/paste/del/selectAll）为
 *               NULL 时对应菜单条目整体省略——两版基准中 readOnly 时
 *               收缩为"复制/全选"、PlainTextEdit 无"删除"项，均按此
 *               机制由调用方裁剪；
 *             - 判定回调（can*）返回 false 时条目灰化（setEnabled(false)，
 *               与基准的 XAction_setEnabled 判定一致）；判定回调为 NULL
 *               时条目默认启用；
 *             - 条目顺序：撤销、重做、分隔线、剪切、复制、粘贴、删除、
 *               分隔线、全选；任一动作已加入才补全选前的分隔线（基准
 *               防御分支：菜单为空时直接返回，不再补分隔线与全选）。
 *             弹出与销毁方式与两版基准 contextMenuEvent 一致：调用方对
 *             返回菜单 XWidget_setAttribute(menu,
 *             XWidgetAttribute_DeleteOnClose, true) 后 XMenu_popup 弹出
 *             到上下文事件全局坐标；菜单关闭时自删并级联释放动作与内部
 *             回调上下文。
 * @note       模块开关 XTEXTMENU_ON 由构建配置定义；关闭时本头文件全部
 *             声明被裁剪。ops 指向的表在 createStandard 内部深拷贝，
 *             调用方可传栈上临时对象；ops->ud 为控件 self，动作触发时
 *             原样透传给回调。全部接口在 GUI 线程调用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTMENU_H
#define XTEXTMENU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XMenu.h"

#if XTEXTMENU_ON

/**
 * @brief      标准编辑菜单控件回调表。
 * @details    由文本控件填写：ud 为控件 self；动作回调在对应菜单条目被
 *             点击时触发；判定回调在构建菜单时调用一次，用于设置条目
 *             启用态（灰化）。两版基准中 readOnly 时不出现的条目，以
 *             对应动作回调置 NULL 的方式整体省略。
 */
typedef struct XTextMenuOps
{
    void* ud;                 /**< 控件 self 透传，回调首参原样传回。 */
    void (*undo)(void* ud);       /**< 撤销动作；NULL 省略"撤销"条目。 */
    bool (*canUndo)(void* ud);    /**< 撤销可用判定；NULL 时默认启用。 */
    void (*redo)(void* ud);       /**< 重做动作；NULL 省略"重做"条目。 */
    bool (*canRedo)(void* ud);    /**< 重做可用判定；NULL 时默认启用。 */
    void (*cut)(void* ud);        /**< 剪切动作；NULL 省略"剪切"条目。 */
    bool (*canCut)(void* ud);     /**< 剪切可用判定（基准：有选区且
                                       回显正常）；NULL 时默认启用。 */
    void (*copy)(void* ud);       /**< 复制动作；NULL 省略"复制"条目
                                       （两版基准均始终提供）。 */
    bool (*canCopy)(void* ud);    /**< 复制可用判定（基准：有选区且
                                       回显正常）；NULL 时默认启用。 */
    void (*paste)(void* ud);      /**< 粘贴动作；NULL 省略"粘贴"条目。 */
    bool (*canPaste)(void* ud);   /**< 粘贴可用判定（基准：剪贴板非空，
                                       XTextClipboard_getText 判定）；
                                       NULL 时默认启用。 */
    void (*del)(void* ud);        /**< 删除选中文本动作（对标
                                       QWidgetLineControl::_q_deleteSelected，
                                       仅 XLineEdit 版基准提供）；NULL
                                       省略"删除"条目。 */
    bool (*canDel)(void* ud);     /**< 删除可用判定（基准：有文本且有
                                       选区）；NULL 时默认启用。 */
    void (*selectAll)(void* ud);  /**< 全选动作；NULL 省略"全选"条目。 */
    bool (*canSelectAll)(void* ud); /**< 全选可用判定（基准：有文本且
                                       尚未全选/无选区）；NULL 时默认
                                       启用。 */
} XTextMenuOps;

/**
 * @brief      构建标准编辑右键菜单（对标两版基准的
 *             createStandardContextMenu 并集）。
 * @details    菜单经 XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
 *             NULL) 创建并设对象名 "qt_edit_menu"；按 XTextMenuOps 中
 *             非空的动作回调依序添加条目，启用态取对应判定回调结果；
 *             条目触发时经 Direct 连接调用 动作回调(ops->ud)（形态与
 *             基准的 XObject_connect_1 + XAction_triggered_signal 一致，
 *             接收者为本函数持有的内部回调表上下文，随菜单析构级联
 *             释放）。表内无任何动作回调时返回不带条目的空菜单；ops
 *             为 NULL 或菜单对象分配失败时返回 NULL。
 * @param      ops 控件回调表借用指针；不可为 NULL。
 * @return     新建的菜单指针（调用方拥有，XMenu_delete_base 或
 *             DeleteOnClose 关闭自删释放）；菜单对象创建失败返回 NULL。
 */
XMenu* XTextMenu_createStandard(const XTextMenuOps* ops);

#endif /* XTEXTMENU_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEXTMENU_H */
