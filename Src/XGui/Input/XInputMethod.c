/******************************************************************************
 * @file       XInputMethod.c
 * @brief      XInputMethod 输入法类实现（对标 Qt 6.8 QInputMethod）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XInputMethod.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"
#include <string.h>
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */

#if XINPUTMETHOD_ON

#if XPLATFORMINPUTCTX_ON
#include "XPlatformInputContext.h"
#endif /* XPLATFORMINPUTCTX_ON */

/** @brief XInputMethod 私有数据块。 */
struct XInputMethodPrivate
{
    XPlatformInputContext* m_context;        /**< 绑定的平台输入上下文（借用）。 */
    XInputMethodTransform  m_inputItemTransform; /**< 输入项变换。 */
    XRectF                 m_inputItemRect;      /**< 输入项几何。 */
    XInputMethodQueryHandler m_queryHandler;    /**< 焦点对象查询回调（借用）。 */
    void*                   m_queryUserData;    /**< 查询回调上下文（借用）。 */
};

/** @brief 发射信号并管理参数列表生命周期（与 XGuiApplication/XWindow 相同模式）。 */
static void xinput_emit(XInputMethod* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/** @brief 3×3 仿射矩阵与单位矩阵比较。 */
static bool xinput_transformEqual(const XInputMethodTransform* a,
                                  const XInputMethodTransform* b)
{
    return a->xm11 == b->xm11 && a->xm12 == b->xm12 && a->xm13 == b->xm13 &&
           a->xm21 == b->xm21 && a->xm22 == b->xm22 && a->xm23 == b->xm23 &&
           a->xm31 == b->xm31 && a->xm32 == b->xm32 && a->xm33 == b->xm33;
}

/** @brief 用仿射矩阵映射点 (x,y)，返回值写入 (outX,outY)。 */
static void xinput_mapPoint(const XInputMethodTransform* t,
                            float x, float y, float* outX, float* outY)
{
    float w = t->xm31 * x + t->xm32 * y + t->xm33;
    float px = t->xm11 * x + t->xm21 * y + t->xm13;
    float py = t->xm12 * x + t->xm22 * y + t->xm23;
    if (w != 0.0f) { px /= w; py /= w; }
    *outX = px;
    *outY = py;
}

/** @brief 用仿射矩阵映射矩形（四个角映射后取外接轴对齐矩形，对标 QTransform::mapRect）。 */
static XRectF xinput_mapRect(const XInputMethodTransform* t, const XRectF* r)
{
    XRectF out;
    float xs[4];
    float ys[4];
    float minX, minY, maxX, maxY;
    int i;
    xinput_mapPoint(t, r->x, r->y, &xs[0], &ys[0]);
    xinput_mapPoint(t, r->x + r->width, r->y, &xs[1], &ys[1]);
    xinput_mapPoint(t, r->x, r->y + r->height, &xs[2], &ys[2]);
    xinput_mapPoint(t, r->x + r->width, r->y + r->height, &xs[3], &ys[3]);
    minX = xs[0]; maxX = xs[0];
    minY = ys[0]; maxY = ys[0];
    for (i = 1; i < 4; ++i) {
        if (xs[i] < minX) minX = xs[i];
        if (xs[i] > maxX) maxX = xs[i];
        if (ys[i] < minY) minY = ys[i];
        if (ys[i] > maxY) maxY = ys[i];
    }
    out.x = minX;
    out.y = minY;
    out.width = maxX - minX;
    out.height = maxY - minY;
    return out;
}

/** @brief 从查询返回值提取 QRectF 等价的 XRectF。 */
static bool xinput_variantRect(const XVariant* value, XRectF* rect)
{
    int type;
    void* data;
    if (!value || !rect) return false;
    type = XVariant_type((XVariant*)value);
    data = XVariant_data((XVariant*)value);
    if (type == XVariantType_User &&
        XVariant_dataSize((XVariant*)value) == sizeof(XRectF) && data) {
        memcpy(rect, data, sizeof(XRectF));
        return true;
    }
    /* 允许平台回调返回借用的 XRectF 指针，等价于 Qt QVariant<QRectF>。 */
    if (type == XVariantType_Ptr && data && XVariant_toPtr(value)) {
        *rect = *(const XRectF*)XVariant_toPtr(value);
        return true;
    }
    return false;
}

/** @brief 查询焦点对象矩形并按输入项变换映射到窗口坐标。 */
static XRectF xinput_queryRect(const XInputMethod* self,
                               XInputMethodQuery query)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    XVariant* value;
    XRectF rect;
    if (!self || !self->m_data) return zero;
    value = XInputMethod_queryFocusObject(query, NULL);
    if (!xinput_variantRect(value, &rect)) {
        if (value) XVariant_delete_base((XClass*)value);
        return zero;
    }
    XVariant_delete_base((XClass*)value);
    return xinput_mapRect(&self->m_data->m_inputItemTransform, &rect);
}

void XInputMethodTransform_identity(XInputMethodTransform* t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->xm11 = 1.0f;
    t->xm22 = 1.0f;
    t->xm33 = 1.0f;
}

static void VXInputMethod_deinit(XInputMethod* self)
{
    if (!self) return;
    if (self->m_data) {
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XInputMethod_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XInputMethod)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXInputMethod_deinit);
    return XVTABLE_DEFAULT;
}

void XInputMethod_init(XInputMethod* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XInputMethod));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XInputMethod);
    self->m_data = (XInputMethodPrivate*)XMalloc_System(sizeof(XInputMethodPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XInputMethodPrivate));
    XInputMethodTransform_identity(&self->m_data->m_inputItemTransform);
}

XInputMethod* XInputMethod_create_ex(XMemoryType memory)
{
    XInputMethod* self = (XInputMethod*)XMemory_malloc(sizeof(XInputMethod), memory);
    if (!self) return NULL;
    XInputMethod_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XInputMethod_setPlatformContext(XInputMethod* self,
                                     XPlatformInputContext* context)
{
    if (!self || !self->m_data) return;
    self->m_data->m_context = context;
}

XPlatformInputContext* XInputMethod_platformContext(const XInputMethod* self)
{
    return (self && self->m_data) ? self->m_data->m_context : NULL;
}

void XInputMethod_setQueryHandler(XInputMethod* self,
                                  XInputMethodQueryHandler handler,
                                  void* userData)
{
    if (!self || !self->m_data) return;
    self->m_data->m_queryHandler = handler;
    self->m_data->m_queryUserData = userData;
}

/* ==================== 输入项状态 ==================== */

XInputMethodTransform XInputMethod_inputItemTransform(const XInputMethod* self)
{
    XInputMethodTransform identity;
    XInputMethodTransform_identity(&identity);
    if (!self || !self->m_data) return identity;
    return self->m_data->m_inputItemTransform;
}

void XInputMethod_setInputItemTransform(XInputMethod* self,
                                        const XInputMethodTransform* transform)
{
    XInputMethodTransform value;
    if (!self || !self->m_data) return;
    XInputMethodTransform_identity(&value);
    if (transform) value = *transform;
    if (xinput_transformEqual(&self->m_data->m_inputItemTransform, &value))
        return;
    self->m_data->m_inputItemTransform = value;
    XInputMethod_cursorRectangleChanged_signal(self);
    XInputMethod_anchorRectangleChanged_signal(self);
}

XRectF XInputMethod_inputItemRectangle(const XInputMethod* self)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!self || !self->m_data) return zero;
    return self->m_data->m_inputItemRect;
}

void XInputMethod_setInputItemRectangle(XInputMethod* self, const XRectF* rect)
{
    XRectF value = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!self || !self->m_data) return;
    if (rect) value = *rect;
    self->m_data->m_inputItemRect = value;
}

XRectF XInputMethod_inputItemClipRectangle(const XInputMethod* self)
{
    return xinput_queryRect(self, XInputMethodQuery_ImInputItemClipRectangle);
}

XRectF XInputMethod_cursorRectangle(const XInputMethod* self)
{
    return xinput_queryRect(self, XInputMethodQuery_ImCursorRectangle);
}

XRectF XInputMethod_anchorRectangle(const XInputMethod* self)
{
    return xinput_queryRect(self, XInputMethodQuery_ImAnchorRectangle);
}

XRectF XInputMethod_keyboardRectangle(const XInputMethod* self)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!self || !self->m_data) return zero;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        return XPlatformInputContext_keyboardRect(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
    return zero;
}

/* ==================== 可见性 / 动画 ==================== */

bool XInputMethod_isVisible(const XInputMethod* self)
{
    if (!self || !self->m_data) return false;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        return XPlatformInputContext_isInputPanelVisible(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
    return false;
}

void XInputMethod_setVisible(XInputMethod* self, bool visible)
{
    if (visible)
        XInputMethod_show(self);
    else
        XInputMethod_hide(self);
}

bool XInputMethod_isAnimating(const XInputMethod* self)
{
    if (!self || !self->m_data) return false;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        return XPlatformInputContext_isAnimating(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
    return false;
}

XString* XInputMethod_locale(const XInputMethod* self)
{
#if XPLATFORMINPUTCTX_ON
    if (self && self->m_data && self->m_data->m_context)
        return XPlatformInputContext_locale(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
    return XString_create_utf8("C");
}

XInputMethodLayoutDirection XInputMethod_inputDirection(const XInputMethod* self)
{
#if XPLATFORMINPUTCTX_ON
    if (self && self->m_data && self->m_data->m_context)
        return XPlatformInputContext_inputDirection(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
    return XInputMethodLayoutDirection_LeftToRight;
}

/* ==================== 静态查询 ==================== */

XVariant* XInputMethod_queryFocusObject(XInputMethodQuery query,
                                        const XVariant* argument)
{
    XInputMethod* inputMethod = NULL;
    XObject* focusObject = NULL;
#if XGUIAPPLICATION_ON
    inputMethod = XGuiApplication_inputMethod();
    focusObject = XGuiApplication_focusObject();
#endif /* XGUIAPPLICATION_ON */
    if (inputMethod && inputMethod->m_data &&
        inputMethod->m_data->m_queryHandler) {
#if XPLATFORMINPUTCTX_ON
        if (inputMethod->m_data->m_context) {
            XObject* contextFocus = XPlatformInputContext_focusObject(
                inputMethod->m_data->m_context);
            if (contextFocus) focusObject = contextFocus;
        }
#endif /* XPLATFORMINPUTCTX_ON */
        return inputMethod->m_data->m_queryHandler(
            focusObject, query, argument, inputMethod->m_data->m_queryUserData);
    }
    /* 无查询回调时与 Qt 的无效 QVariant 返回保持一致。 */
    return NULL;
}

/* ==================== 输入法动作 ==================== */

void XInputMethod_show(XInputMethod* self)
{
    if (!self || !self->m_data) return;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        XPlatformInputContext_showInputPanel(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
}

void XInputMethod_hide(XInputMethod* self)
{
    if (!self || !self->m_data) return;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        XPlatformInputContext_hideInputPanel(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
}

void XInputMethod_update(XInputMethod* self, XInputMethodQueries queries)
{
    XVariant* enabled = NULL;
    if (!self || !self->m_data) return;
    if (queries & XInputMethodQuery_ImEnabled) {
        enabled = XInputMethod_queryFocusObject(
            XInputMethodQuery_ImEnabled, NULL);
#if XPLATFORMINPUTCTX_ON
        XPlatformInputContext_setInputMethodAccepted(
            self->m_data->m_context, enabled && XVariant_toBool(enabled));
#endif /* XPLATFORMINPUTCTX_ON */
        if (enabled) XVariant_delete_base((XClass*)enabled);
    }
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        XPlatformInputContext_update(self->m_data->m_context, queries);
#endif /* XPLATFORMINPUTCTX_ON */
    if (queries & XInputMethodQuery_ImCursorRectangle)
        XInputMethod_cursorRectangleChanged_signal(self);
    if (queries & XInputMethodQuery_ImAnchorRectangle)
        XInputMethod_anchorRectangleChanged_signal(self);
    if (queries & XInputMethodQuery_ImInputItemClipRectangle)
        XInputMethod_inputItemClipRectangleChanged_signal(self);
}

void XInputMethod_reset(XInputMethod* self)
{
    if (!self || !self->m_data) return;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        XPlatformInputContext_reset(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
}

void XInputMethod_commit(XInputMethod* self)
{
    if (!self || !self->m_data) return;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        XPlatformInputContext_commit(self->m_data->m_context);
#endif /* XPLATFORMINPUTCTX_ON */
}

void XInputMethod_invokeAction(XInputMethod* self, XInputMethodAction action,
                               int cursorPosition)
{
    if (!self || !self->m_data) return;
#if XPLATFORMINPUTCTX_ON
    if (self->m_data->m_context)
        XPlatformInputContext_invokeAction(self->m_data->m_context, action,
                                           cursorPosition);
#else
    (void)action; (void)cursorPosition;
#endif /* XPLATFORMINPUTCTX_ON */
}

/* ==================== 信号（8 个） ==================== */

void* XInputMethod_cursorRectangleChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_cursorRectangleChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_cursorRectangleChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_cursorRectangleChanged_signal;
}

void* XInputMethod_anchorRectangleChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_anchorRectangleChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_anchorRectangleChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_anchorRectangleChanged_signal;
}

void* XInputMethod_keyboardRectangleChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_keyboardRectangleChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_keyboardRectangleChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_keyboardRectangleChanged_signal;
}

void* XInputMethod_inputItemClipRectangleChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_inputItemClipRectangleChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_inputItemClipRectangleChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_inputItemClipRectangleChanged_signal;
}

void* XInputMethod_visibleChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_visibleChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_visibleChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_visibleChanged_signal;
}

void* XInputMethod_animatingChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_animatingChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_animatingChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_animatingChanged_signal;
}

void* XInputMethod_localeChanged_signal(XInputMethod* self)
{
    if (!self) return (void*)(size_t)XInputMethod_localeChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_localeChanged_signal, NULL);
    return (void*)(size_t)XInputMethod_localeChanged_signal;
}

void* XInputMethod_inputDirectionChanged_signal(XInputMethod* self,
        XInputMethodLayoutDirection newDirection)
{
    if (!self) return (void*)(size_t)XInputMethod_inputDirectionChanged_signal;
    xinput_emit(self, (size_t)XInputMethod_inputDirectionChanged_signal,
                XVarList_Create(XVar(XInputMethodLayoutDirection, newDirection)));
    return (void*)(size_t)XInputMethod_inputDirectionChanged_signal;
}

#endif /* XINPUTMETHOD_ON */
