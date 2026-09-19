/**
 * @file       XTextMenu.c
 * @brief      XTextMenu 标准编辑右键菜单构建器实现。
 * @details    菜单结构、条目文案（"撤销(&U)/重做(&R)/剪切(&T)/复制(&C)/
 *             粘贴(&P)/删除/全选(&A)"）、分隔线位置、对象名
 *             "qt_edit_menu"、启用态灰化与 Direct 连接触发形态，逐点
 *             对齐 XLineEdit_createStandardContextMenu 与
 *             XPlainTextEdit_createStandardContextMenu 两版基准的并集；
 *             控件差异（readOnly 收缩、无"删除"项）由 XTextMenuOps 条目
 *             置 NULL 表达。动作触发经由 XObject_connect_1 接到
 *             XAction_triggered_signal，接收者为随菜单级联释放的内部
 *             回调表上下文（XObject 派生存储，trampoline 分发到控件
 *             回调并透传 ud）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTextMenu.h"

#if XTEXTMENU_ON

#include "XMemory.h"
#include "XObject.h"
#include "XString.h"
#include "XVector.h"

/* ==================== 内部回调表上下文 ==================== */

/**
 * @brief      菜单动作到控件回调表的分发上下文。
 * @details    m_base 必须是第一个成员：上下文以真实 XObject 身份充当
 *             信号连接的接收者（参与 XSignalSlot 的连接登记与析构清理），
 *             并作为菜单的子对象登记——菜单（DeleteOnClose 关闭自删）
 *             析构时按堆对象级联删除，回调表拷贝随之释放，无泄漏。
 */
typedef struct XTextMenuContext
{
    XObject      m_base; /**< 基类成员；必须是第一个，由 XClass 管理。 */
    XTextMenuOps ops;    /**< 控件回调表深拷贝（允许调用方传栈量表）。 */
} XTextMenuContext;

/** @brief 菜单动作槽：撤销（trampoline 透传 ops->ud）。 */
static void xtextmenu_undoSlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.undo) ctx->ops.undo(ctx->ops.ud);
}

/** @brief 菜单动作槽：重做。 */
static void xtextmenu_redoSlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.redo) ctx->ops.redo(ctx->ops.ud);
}

/** @brief 菜单动作槽：剪切。 */
static void xtextmenu_cutSlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.cut) ctx->ops.cut(ctx->ops.ud);
}

/** @brief 菜单动作槽：复制。 */
static void xtextmenu_copySlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.copy) ctx->ops.copy(ctx->ops.ud);
}

/** @brief 菜单动作槽：粘贴。 */
static void xtextmenu_pasteSlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.paste) ctx->ops.paste(ctx->ops.ud);
}

/** @brief 菜单动作槽：删除选中文本（对标
 *         QWidgetLineControl::_q_deleteSelected）。 */
static void xtextmenu_delSlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.del) ctx->ops.del(ctx->ops.ud);
}

/** @brief 菜单动作槽：全选。 */
static void xtextmenu_selectAllSlot(XObject* receiver, XVarList* args)
{
    XTextMenuContext* ctx = (XTextMenuContext*)receiver;
    (void)args;
    if (ctx && ctx->ops.selectAll) ctx->ops.selectAll(ctx->ops.ud);
}

/* ==================== 内部辅助 ==================== */

/**
 * @brief      创建并登记动作分发上下文（作为菜单子对象级联释放）。
 * @param      ops 源回调表借用指针；不可为 NULL。
 * @param      menu 挂靠的菜单对象；不可为 NULL。
 * @return     新建上下文指针；分配失败返回 NULL。
 */
static XTextMenuContext* xtextmenu_contextCreate(const XTextMenuOps* ops,
                                                 XMenu* menu)
{
    XTextMenuContext* ctx = (XTextMenuContext*)XMalloc_System(sizeof(*ctx));
    if (!ctx) return NULL;
    XObject_init(&ctx->m_base);
    /* 与 XObject_create_ex 一致：登记内存方法与堆所有权位，菜单析构
       时 XClass_delete_base 按同一分配器释放整个上下文存储。 */
    Set_Class_Memory(&ctx->m_base, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(&ctx->m_base, true);
    ctx->ops = *ops;
    XObject_setParent(&ctx->m_base, (XObject*)menu);
    return ctx;
}

/**
 * @brief      添加菜单项并接通触发槽（对标基准的 addMenuAction）。
 * @details    动作回调为 NULL 时整条省略（对应两版基准中 readOnly 时
 *             不出现的条目）；Direct 连接形态与基准一致。
 * @param      menu 目标菜单；不为 NULL。
 * @param      utf8 条目文本（UTF-8）。
 * @param      slot 分发槽；不可为 NULL。
 * @param      ctx 分发上下文；可为 NULL（仅建条目不接线）。
 * @param      actionFn 控件动作回调；可为 NULL（表示省略条目）。
 * @return     新建动作指针（由菜单拥有）；条目省略或创建失败返回 NULL。
 */
static XAction* xtextmenu_addItem(XMenu* menu, const char* utf8,
                                  XSlotFunc1 slot, XTextMenuContext* ctx,
                                  void (*actionFn)(void*))
{
    XAction* action;
    if (!actionFn) return NULL;
    action = XMenu_addAction_2(menu, utf8);
    if (!action) return NULL;
    if (ctx)
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          &ctx->m_base, slot, XConnectionType_Direct);
    return action;
}

/**
 * @brief      取条目启用态（判定回调为 NULL 时默认启用）。
 * @param      ctx 分发上下文借用指针；可为 NULL。
 * @param      canFn 判定回调；可为 NULL。
 * @return     判定回调返回其结果；无判定回调返回 true。
 */
static bool xtextmenu_itemEnabled(const XTextMenuContext* ctx,
                                  bool (*canFn)(void*))
{
    if (!canFn) return true;
    return canFn(ctx ? ctx->ops.ud : NULL);
}

/* ==================== 公开接口 ==================== */

XMenu* XTextMenu_createStandard(const XTextMenuOps* ops)
{
    XMenu* menu;
    XString* name;
    XTextMenuContext* ctx;
    XAction* action;
    if (!ops) return NULL;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!menu) return NULL;
    /* 对标 Qt/两版基准：qt_edit_menu 对象名供测试与样式查找。 */
    name = XString_create_utf8("qt_edit_menu");
    if (name) {
        XObject_setObjectName((XObject*)menu, name);
        XString_delete_base((XClass*)name);
    }
    ctx = xtextmenu_contextCreate(ops, menu);
    if (!ctx) return menu; /* 分配失败：返回无条目菜单（不接回调）。 */

    /* 组一：撤销/重做（readOnly 的基准变体以 undo/redo 置 NULL 裁剪，
       连同其后的分隔线一起消失，与原结构逐项一致）。 */
    action = xtextmenu_addItem(menu, "撤销(&U)", xtextmenu_undoSlot,
                               ctx, ops->undo);
    XAction_setEnabled(action, xtextmenu_itemEnabled(ctx, ops->canUndo));
    action = xtextmenu_addItem(menu, "重做(&R)", xtextmenu_redoSlot,
                               ctx, ops->redo);
    XAction_setEnabled(action, xtextmenu_itemEnabled(ctx, ops->canRedo));
    if (ops->undo || ops->redo) XMenu_addSeparator(menu);

    /* 组二：剪切/复制/粘贴/删除（PlainTextEdit 基准无删除项：del 置
       NULL 即整体省略，分隔线结构不受影响）。 */
    action = xtextmenu_addItem(menu, "剪切(&T)", xtextmenu_cutSlot,
                               ctx, ops->cut);
    XAction_setEnabled(action, xtextmenu_itemEnabled(ctx, ops->canCut));
    action = xtextmenu_addItem(menu, "复制(&C)", xtextmenu_copySlot,
                               ctx, ops->copy);
    XAction_setEnabled(action, xtextmenu_itemEnabled(ctx, ops->canCopy));
    action = xtextmenu_addItem(menu, "粘贴(&P)", xtextmenu_pasteSlot,
                               ctx, ops->paste);
    XAction_setEnabled(action, xtextmenu_itemEnabled(ctx, ops->canPaste));
    action = xtextmenu_addItem(menu, "删除", xtextmenu_delSlot,
                               ctx, ops->del);
    XAction_setEnabled(action, xtextmenu_itemEnabled(ctx, ops->canDel));

    /* 基准防御分支（XLineEdit 版）：菜单尚无任何动作时直接返回，不再
       补分隔线与全选。 */
    if (!XMenu_actions(menu) ||
        XVector_size_base((const XContainer*)XMenu_actions(menu)) == 0)
        return menu;
    XMenu_addSeparator(menu);
    action = xtextmenu_addItem(menu, "全选(&A)", xtextmenu_selectAllSlot,
                               ctx, ops->selectAll);
    XAction_setEnabled(action,
                       xtextmenu_itemEnabled(ctx, ops->canSelectAll));
    return menu;
}

#endif /* XTEXTMENU_ON */
