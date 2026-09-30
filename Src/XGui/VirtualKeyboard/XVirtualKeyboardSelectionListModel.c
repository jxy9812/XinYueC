/**
 * @file       XVirtualKeyboardSelectionListModel.c
 * @brief      XVirtualKeyboardSelectionListModel 候选列表数据模型实现
 *             （引擎持有；视图=XVirtualKeyboard 候选带直接消费）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardSelectionListModel.h"
#include "XVirtualKeyboardSelectionListModel_Protected.h"
#include "XVirtualKeyboardAbstractInputMethod_Protected.h"
#include "XMemory.h"

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardSelectionListModelPrivate
{
    XVirtualKeyboardAbstractInputMethod* m_dataSource; /**< 数据源（借用）。 */
    XVirtualKeyboardSelectionListModelType m_type;     /**< 模型类型。 */
    int m_count;                                       /**< 缓存行数（-1=未同步）。 */
    int m_activeItem;                                  /**< 高亮候选下标（-1=无）。 */
} XVirtualKeyboardSelectionListModelPrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardSelectionListModelPrivate* xvksl_priv(
        const XVirtualKeyboardSelectionListModel* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardSelectionListModelPrivate*)self->m_data
               : NULL;
}

/** @brief 发射信号并管理参数表生命周期。 */
static void xvksl_emit(XVirtualKeyboardSelectionListModel* self,
                       size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args)
        XVarList_delete(args);
}

/* 前向声明（class_init 注册与创建入口用）。 */
static void XVksl_deinit(XVirtualKeyboardSelectionListModel* self);
void XVirtualKeyboardSelectionListModel_init(
        XVirtualKeyboardSelectionListModel* self);
XVirtualKeyboardSelectionListModel*
XVirtualKeyboardSelectionListModel_create_engine(
        XVirtualKeyboardSelectionListModelType type);

XVtable* XVirtualKeyboardSelectionListModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardSelectionListModel)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVksl_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：释放私有块后调父类。 */
static void XVksl_deinit(XVirtualKeyboardSelectionListModel* self)
{
    if (!self) return;
    if (self->m_data) {
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/**
 * @brief      引擎 TU 专用创建入口（Qt 私有构造+friend 口径）。
 * @param      type 模型类型。
 * @return     新对象指针；失败返回 NULL。调用方用 *_delete_base 释放。
 */
XVirtualKeyboardSelectionListModel*
XVirtualKeyboardSelectionListModel_create_engine(
        XVirtualKeyboardSelectionListModelType type)
{
    XVirtualKeyboardSelectionListModel* self =
        (XVirtualKeyboardSelectionListModel*)XMalloc_System(sizeof(*self));
    XVirtualKeyboardSelectionListModelPrivate* priv;
    if (!self) return NULL;
    XVirtualKeyboardSelectionListModel_init(self);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    priv = xvksl_priv(self);
    if (priv) priv->m_type = type;
    return self;
}

void XVirtualKeyboardSelectionListModel_init(
        XVirtualKeyboardSelectionListModel* self)
{
    XVirtualKeyboardSelectionListModelPrivate* priv;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardSelectionListModel);
    self->m_data = XMalloc_System(
        sizeof(XVirtualKeyboardSelectionListModelPrivate));
    if (!self->m_data) return;
    priv = (XVirtualKeyboardSelectionListModelPrivate*)self->m_data;
    XMemset(priv, 0, sizeof(*priv));
    priv->m_count = -1;
    priv->m_activeItem = -1;
}

int XVirtualKeyboardSelectionListModel_rowCount(
        const XVirtualKeyboardSelectionListModel* self)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv || !priv->m_dataSource) return 0;
    return XVirtualKeyboardAbstractInputMethod_selectionListItemCount_base(
        priv->m_dataSource, (int)priv->m_type);
}

int XVirtualKeyboardSelectionListModel_count(
        const XVirtualKeyboardSelectionListModel* self)
{
    return XVirtualKeyboardSelectionListModel_rowCount(self);
}

XVariant* XVirtualKeyboardSelectionListModel_data(
        const XVirtualKeyboardSelectionListModel* self, int index, int role)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv || !priv->m_dataSource || index < 0) return NULL;
    if (index >= XVirtualKeyboardSelectionListModel_rowCount(self))
        return NULL;
    return XVirtualKeyboardAbstractInputMethod_selectionListData_base(
        priv->m_dataSource, (int)priv->m_type, index, role);
}

XVariant* XVirtualKeyboardSelectionListModel_dataAt(
        const XVirtualKeyboardSelectionListModel* self, int index, int role)
{
    return XVirtualKeyboardSelectionListModel_data(self, index, role);
}

XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardSelectionListModel_dataSource(
        const XVirtualKeyboardSelectionListModel* self)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    return priv ? priv->m_dataSource : NULL;
}

XVirtualKeyboardSelectionListModelType
XVirtualKeyboardSelectionListModel_type(
        const XVirtualKeyboardSelectionListModel* self)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    return priv ? priv->m_type
                : XVirtualKeyboardSelectionListModelType_WordCandidateList;
}

bool XVirtualKeyboardSelectionListModel_selectItem(
        XVirtualKeyboardSelectionListModel* self, int index)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv || !priv->m_dataSource || index < 0) return false;
    if (index >= XVirtualKeyboardSelectionListModel_rowCount(self))
        return false;
    XVirtualKeyboardAbstractInputMethod_selectionListItemSelected_base(
        priv->m_dataSource, (int)priv->m_type, index);
    xvksl_emit(self, (size_t)
                   XVirtualKeyboardSelectionListModel_itemSelected_signal(
                       NULL, 0),
               XVarList_Create(XVar(int, index)));
    return true;
}

bool XVirtualKeyboardSelectionListModel_removeItem(
        XVirtualKeyboardSelectionListModel* self, int index)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv || !priv->m_dataSource || index < 0) return false;
    if (index >= XVirtualKeyboardSelectionListModel_rowCount(self))
        return false;
    return XVirtualKeyboardAbstractInputMethod_selectionListRemoveItem_base(
        priv->m_dataSource, (int)priv->m_type, index);
}

/* ==================== 引擎装配与转发槽（保护头契约） ==================== */

void XVirtualKeyboardSelectionListModel_setDataSource(
        XVirtualKeyboardSelectionListModel* self,
        XVirtualKeyboardAbstractInputMethod* dataSource,
        XVirtualKeyboardSelectionListModelType type)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv) return;
    priv->m_dataSource = dataSource;
    priv->m_type = type;
    priv->m_count = XVirtualKeyboardSelectionListModel_rowCount(self);
}

void XVirtualKeyboardSelectionListModel_selectionListChanged(
        XVirtualKeyboardSelectionListModel* self, int type)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    int newCount;
    if (!priv || type != (int)priv->m_type) return;
    newCount = XVirtualKeyboardSelectionListModel_rowCount(self);
    if (newCount == priv->m_count) return;
    priv->m_count = newCount;
    if (priv->m_activeItem >= newCount) priv->m_activeItem = newCount - 1;
    xvksl_emit(self, (size_t)
                   XVirtualKeyboardSelectionListModel_countChanged_signal(
                       NULL),
               NULL);
}

void XVirtualKeyboardSelectionListModel_selectionListActiveItemChanged(
        XVirtualKeyboardSelectionListModel* self, int type, int index)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv || type != (int)priv->m_type) return;
    if (priv->m_activeItem == index) return;
    priv->m_activeItem = index;
    xvksl_emit(self, (size_t)
                   XVirtualKeyboardSelectionListModel_activeItemChanged_signal(
                       NULL, 0),
               XVarList_Create(XVar(int, index)));
}

void XVirtualKeyboardSelectionListModel_dataSourceDestroyed(
        XVirtualKeyboardSelectionListModel* self)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    if (!priv) return;
    priv->m_dataSource = NULL;
    priv->m_count = -1;
    priv->m_activeItem = -1;
    xvksl_emit(self, (size_t)
                   XVirtualKeyboardSelectionListModel_countChanged_signal(
                       NULL),
               NULL);
}

int XVirtualKeyboardSelectionListModel_activeItem(
        const XVirtualKeyboardSelectionListModel* self)
{
    XVirtualKeyboardSelectionListModelPrivate* priv = xvksl_priv(self);
    return priv ? priv->m_activeItem : -1;
}

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboardSelectionListModel_countChanged_signal(
        XVirtualKeyboardSelectionListModel* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardSelectionListModel_countChanged_signal;
}

void* XVirtualKeyboardSelectionListModel_activeItemChanged_signal(
        XVirtualKeyboardSelectionListModel* self, int index)
{
    (void)self; (void)index;
    return (void*)(size_t)
        XVirtualKeyboardSelectionListModel_activeItemChanged_signal;
}

void* XVirtualKeyboardSelectionListModel_itemSelected_signal(
        XVirtualKeyboardSelectionListModel* self, int index)
{
    (void)self; (void)index;
    return (void*)(size_t)
        XVirtualKeyboardSelectionListModel_itemSelected_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
