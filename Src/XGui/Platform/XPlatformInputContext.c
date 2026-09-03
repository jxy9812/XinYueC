/******************************************************************************
 * @file       XPlatformInputContext.c
 * @brief      XPlatformInputContext 平台输入上下文类实现（对标 Qt 6.8
 *             QPlatformInputContext 全部公共 API）。
 * @details    本文件实现 XPlatformInputContext 的嵌入式空后端：
 *             - 能力/有效性：isValid()/hasCapability() 恒 true；
 *             - 输入法动作：reset/commit/update/invokeAction/filterEvent
 *               按 Qt 基类默认语义（重置/无操作/不消费事件）；
 *             - 虚拟键盘矩形/动画/输入面板可见性：setter + emit 系列，
 *               状态保存在私有块并转发到绑定的 XInputMethod 同名信号；
 *             - 区域/方向：locale 默认 "C"，方向按 QLocale 文本方向推导，
 *               变化时转发 localeChanged / inputDirectionChanged；
 *             - 焦点对象：借用指针保存，inputMethodAccepted 由
 *               XInputMethod::update(ImEnabled) 的查询结果驱动；未注册
 *               查询回调时保持 Qt 的 false 默认语义。
 *             模块不依赖任何平台 API，未连接系统输入法框架。
 * @note       模块总开关 XPLATFORMINPUTCTX_ON 定义于 XGuiConfig.h；
 *             置 0 时本文件实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPlatformInputContext.h"
#include "XMemory.h"
#include <string.h>
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif /* XGUIAPPLICATION_ON */

#if XPLATFORMINPUTCTX_ON

/** @brief 根据 IETF locale 的语言子标签判断 Qt QLocale::textDirection。 */
static bool xplatform_localeIsRtl(const char* locale)
{
    char a;
    char b;
    char c;
    char d;
    if (!locale || !locale[0] || !locale[1]) return false;
    a = locale[0] >= 'A' && locale[0] <= 'Z' ? (char)(locale[0] + ('a' - 'A')) : locale[0];
    b = locale[1] >= 'A' && locale[1] <= 'Z' ? (char)(locale[1] + ('a' - 'A')) : locale[1];
    c = locale[2] >= 'A' && locale[2] <= 'Z' ? (char)(locale[2] + ('a' - 'A')) : locale[2];
    d = locale[3] >= 'A' && locale[3] <= 'Z' ? (char)(locale[3] + ('a' - 'A')) : locale[3];
    if ((a == 'a' && b == 'r') || (a == 'f' && b == 'a') ||
        (a == 'h' && b == 'e') || (a == 'i' && b == 'w') ||
        (a == 'u' && b == 'r') || (a == 'p' && b == 's') ||
        (a == 'd' && b == 'v') || (a == 'y' && b == 'i') ||
        (a == 's' && b == 'd') || (a == 'u' && b == 'g'))
        return true;
    /* Kurdish has both scripts; the Arabic-script locale uses ku-Arab. */
    return a == 'k' && b == 'u' && (c == '-' || c == '_') && d == 'a';
}

/** @brief XPlatformInputContext 私有数据块。 */
struct XPlatformInputContextPrivate
{
    XObject* m_focusObject;               /**< 焦点对象（借用）。 */
    XInputMethod* m_inputMethod;          /**< 绑定的输入法对象（借用，中转信号）。 */
    XString* m_locale;                    /**< 区域语言标签（拥有，默认 "C"）。 */
    XRectF m_keyboardRect;                /**< 虚拟键盘矩形。 */
    XInputMethodLayoutDirection m_inputDirection; /**< 当前输入方向。 */
    bool m_inputPanelVisible;             /**< 输入面板可见性。 */
    bool m_animating;                     /**< 键盘动画状态。 */
    bool m_inputMethodAccepted;           /**< 焦点对象查询得到的接受状态。 */
};

static void VXPlatformInputContext_deinit(XPlatformInputContext* self)
{
    if (!self) return;
    if (self->m_data) {
        if (self->m_data->m_locale) {
            XString_delete_base(self->m_data->m_locale);
            self->m_data->m_locale = NULL;
        }
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/** @brief 用输入项变换的逆矩阵把窗口坐标还原到焦点控件坐标。 */
static XPointF xplatform_mapPointToInputItem(const XInputMethodTransform* t,
                                             XPointF point)
{
    float a, b, c, d, e, f, g, h, i, det, w;
    XPointF result = point;
    if (!t) return result;
    a = t->xm11; b = t->xm21; c = t->xm13;
    d = t->xm12; e = t->xm22; f = t->xm23;
    g = t->xm31; h = t->xm32; i = t->xm33;
    det = a * (e * i - f * h) - b * (d * i - f * g) +
          c * (d * h - e * g);
    if (det > -1e-7f && det < 1e-7f) return result;
    result.x = ((e * i - f * h) * point.x +
                (c * h - b * i) * point.y + (b * f - c * e)) / det;
    result.y = ((f * g - d * i) * point.x +
                (a * i - c * g) * point.y + (c * d - a * f)) / det;
    w = (d * h - e * g) * point.x +
        (b * g - a * h) * point.y + (a * e - b * d);
    if (w > -1e-7f && w < 1e-7f) return point;
    result.x /= w;
    result.y /= w;
    return result;
}

/** @brief 用输入项变换把焦点控件坐标映射到窗口坐标。 */
static XPointF xplatform_mapPointFromInputItem(const XInputMethodTransform* t,
                                               XPointF point)
{
    XPointF result = point;
    float w;
    if (!t) return result;
    w = t->xm31 * point.x + t->xm32 * point.y + t->xm33;
    result.x = t->xm11 * point.x + t->xm21 * point.y + t->xm13;
    result.y = t->xm12 * point.x + t->xm22 * point.y + t->xm23;
    if (w != 0.0f) {
        result.x /= w;
        result.y /= w;
    }
    return result;
}

/** @brief 变换矩形四角并返回轴对齐外接矩形。 */
static XRectF xplatform_mapRectFromInputItem(const XInputMethodTransform* t,
                                             const XRectF* rect)
{
    XPointF points[4];
    XRectF result;
    float minX, minY, maxX, maxY;
    int i;
    if (!rect) return (XRectF){ 0.0f, 0.0f, 0.0f, 0.0f };
    points[0] = xplatform_mapPointFromInputItem(t, (XPointF){rect->x, rect->y});
    points[1] = xplatform_mapPointFromInputItem(t,
                                                (XPointF){rect->x + rect->width, rect->y});
    points[2] = xplatform_mapPointFromInputItem(t,
                                                (XPointF){rect->x, rect->y + rect->height});
    points[3] = xplatform_mapPointFromInputItem(t,
                                                (XPointF){rect->x + rect->width,
                                                          rect->y + rect->height});
    minX = maxX = points[0].x;
    minY = maxY = points[0].y;
    for (i = 1; i < 4; ++i) {
        if (points[i].x < minX) minX = points[i].x;
        if (points[i].x > maxX) maxX = points[i].x;
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
    }
    result.x = minX;
    result.y = minY;
    result.width = maxX - minX;
    result.height = maxY - minY;
    return result;
}

XVtable* XPlatformInputContext_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPlatformInputContext)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPlatformInputContext_deinit);
    return XVTABLE_DEFAULT;
}

void XPlatformInputContext_init(XPlatformInputContext* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XPlatformInputContext));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XPlatformInputContext);
    self->m_data = (XPlatformInputContextPrivate*)XMalloc_System(sizeof(XPlatformInputContextPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XPlatformInputContextPrivate));
    self->m_data->m_locale = XString_create_utf8("C");
    self->m_data->m_inputDirection = XInputMethodLayoutDirection_LeftToRight;
}

XPlatformInputContext* XPlatformInputContext_create_ex(XMemoryType memory)
{
    XPlatformInputContext* self;
    self = (XPlatformInputContext*)XMemory_malloc(sizeof(XPlatformInputContext), memory);
    if (!self) return NULL;
    XPlatformInputContext_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 能力与有效性 ==================== */

bool XPlatformInputContext_isValid(const XPlatformInputContext* self)
{
    (void)self;
    /* 嵌入式内置后端恒有效。 */
    return true;
}

bool XPlatformInputContext_hasCapability(
        const XPlatformInputContext* self,
        XPlatformInputContextCapability capability)
{
    (void)self; (void)capability;
    /* 与 Qt 默认实现一致：恒支持隐藏文本能力位。 */
    return true;
}

/* ==================== 输入法动作（Qt 基类默认语义） ==================== */

void XPlatformInputContext_reset(XPlatformInputContext* self)
{
    /* 空后端无组合状态：no-op。 */
    (void)self;
}

void XPlatformInputContext_commit(XPlatformInputContext* self)
{
    /* 空后端无组合词句：no-op。 */
    (void)self;
}

void XPlatformInputContext_update(XPlatformInputContext* self,
                                  XInputMethodQueries queries)
{
    /* 空后端无编辑状态变更协议：no-op。 */
    (void)self; (void)queries;
}

void XPlatformInputContext_invokeAction(XPlatformInputContext* self,
                                        XInputMethodAction action,
                                        int cursorPosition)
{
    /* 与 Qt 基类默认一致：Click 动作等价 reset()。 */
    (void)cursorPosition;
    if (action == XInputMethodAction_Click)
        XPlatformInputContext_reset(self);
}

bool XPlatformInputContext_filterEvent(const XPlatformInputContext* self,
                                       const XEvent* event)
{
    /* 空后端不消费任何输入事件。 */
    (void)self; (void)event;
    return false;
}

/* ==================== 虚拟键盘矩形 ==================== */

XRectF XPlatformInputContext_keyboardRect(const XPlatformInputContext* self)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!self || !self->m_data) return zero;
    return self->m_data->m_keyboardRect;
}

void XPlatformInputContext_setKeyboardRect(XPlatformInputContext* self,
                                           const XRectF* rect)
{
    XRectF value = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!self || !self->m_data) return;
    if (rect) value = *rect;
    if (self->m_data->m_keyboardRect.x == value.x &&
        self->m_data->m_keyboardRect.y == value.y &&
        self->m_data->m_keyboardRect.width == value.width &&
        self->m_data->m_keyboardRect.height == value.height)
        return;
    self->m_data->m_keyboardRect = value;
    XPlatformInputContext_emitKeyboardRectChanged(self);
}

void XPlatformInputContext_emitKeyboardRectChanged(XPlatformInputContext* self)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputMethod)
        XInputMethod_keyboardRectangleChanged_signal(self->m_data->m_inputMethod);
}

/* ==================== 动画状态 ==================== */

bool XPlatformInputContext_isAnimating(const XPlatformInputContext* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_animating;
}

void XPlatformInputContext_setAnimating(XPlatformInputContext* self,
                                        bool animating)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_animating == animating) return;
    self->m_data->m_animating = animating;
    XPlatformInputContext_emitAnimatingChanged(self);
}

void XPlatformInputContext_emitAnimatingChanged(XPlatformInputContext* self)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputMethod)
        XInputMethod_animatingChanged_signal(self->m_data->m_inputMethod);
}

/* ==================== 输入面板显隐 ==================== */

void XPlatformInputContext_showInputPanel(XPlatformInputContext* self)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputPanelVisible) return;
    self->m_data->m_inputPanelVisible = true;
    XPlatformInputContext_emitInputPanelVisibleChanged(self);
}

void XPlatformInputContext_hideInputPanel(XPlatformInputContext* self)
{
    if (!self || !self->m_data) return;
    if (!self->m_data->m_inputPanelVisible) return;
    self->m_data->m_inputPanelVisible = false;
    XPlatformInputContext_emitInputPanelVisibleChanged(self);
}

bool XPlatformInputContext_isInputPanelVisible(const XPlatformInputContext* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_inputPanelVisible;
}

void XPlatformInputContext_emitInputPanelVisibleChanged(XPlatformInputContext* self)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputMethod)
        XInputMethod_visibleChanged_signal(self->m_data->m_inputMethod);
}

/* ==================== 区域与方向 ==================== */

XString* XPlatformInputContext_locale(const XPlatformInputContext* self)
{
    if (!self || !self->m_data) return NULL;
    return XString_create_copy(self->m_data->m_locale);
}

void XPlatformInputContext_setLocale(XPlatformInputContext* self,
                                     const char* locale)
{
    XString* old;
    XString* value = NULL;
    if (!self || !self->m_data) return;
    value = locale ? XString_create_utf8(locale)
                   : XString_create_utf8("C");
    if (!value) return;
    if (self->m_data->m_locale &&
        XString_equals_utf8(self->m_data->m_locale, locale ? locale : "C",
                            XChar_CaseSensitive)) {
        XString_delete_base(value);
        return;
    }
    old = self->m_data->m_locale;
    self->m_data->m_locale = value;
    if (old) XString_delete_base(old);
    XPlatformInputContext_emitLocaleChanged(self);
    /* 按 Qt QLocale::textDirection 语义在区域变化时重估输入方向。 */
    XPlatformInputContext_setInputDirection(
        self, xplatform_localeIsRtl(locale ? locale : "C")
            ? XInputMethodLayoutDirection_RightToLeft
            : XInputMethodLayoutDirection_LeftToRight);
}

void XPlatformInputContext_emitLocaleChanged(XPlatformInputContext* self)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputMethod)
        XInputMethod_localeChanged_signal(self->m_data->m_inputMethod);
}

XInputMethodLayoutDirection XPlatformInputContext_inputDirection(
        const XPlatformInputContext* self)
{
    if (!self || !self->m_data) return XInputMethodLayoutDirection_LeftToRight;
    return self->m_data->m_inputDirection;
}

void XPlatformInputContext_setInputDirection(
        XPlatformInputContext* self, XInputMethodLayoutDirection direction)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputDirection == direction) return;
    self->m_data->m_inputDirection = direction;
    XPlatformInputContext_emitInputDirectionChanged(self, direction);
#if XGUIAPPLICATION_ON
    /* 由 GUI 应用层根据 Auto 请求解析有效方向，平台后端不触碰布局策略。 */
    XGuiApplication_notifyPlatformInputDirectionChanged();
#endif /* XGUIAPPLICATION_ON */
}

void XPlatformInputContext_emitInputDirectionChanged(
        XPlatformInputContext* self, XInputMethodLayoutDirection newDirection)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_inputMethod)
        XInputMethod_inputDirectionChanged_signal(self->m_data->m_inputMethod,
                                                  newDirection);
}

/* ==================== 焦点对象 ==================== */

void XPlatformInputContext_setFocusObject(XPlatformInputContext* self,
                                          XObject* object)
{
    if (!self || !self->m_data) return;
    self->m_data->m_focusObject = object;
    self->m_data->m_inputMethodAccepted = false;
}

XObject* XPlatformInputContext_focusObject(const XPlatformInputContext* self)
{
    if (!self || !self->m_data) return NULL;
    return self->m_data->m_focusObject;
}

bool XPlatformInputContext_inputMethodAccepted(const XPlatformInputContext* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_inputMethodAccepted;
}

void XPlatformInputContext_setInputMethodAccepted(XPlatformInputContext* self,
                                                  bool accepted)
{
    if (!self || !self->m_data) return;
    self->m_data->m_inputMethodAccepted = accepted;
}

void XPlatformInputContext_setInputMethod(XPlatformInputContext* self,
                                          XInputMethod* inputMethod)
{
    if (!self || !self->m_data) return;
    self->m_data->m_inputMethod = inputMethod;
}

/* ==================== 静态辅助 ==================== */

void XPlatformInputContext_setSelectionOnFocusObject(const XPointF* anchorPos,
                                                     const XPointF* cursorPos)
{
    /* 空后端无焦点对象选区写入协议：no-op。 */
    (void)anchorPos; (void)cursorPos;
}

XVariant* XPlatformInputContext_queryFocusObject(XInputMethodQuery query,
                                                 XPointF position)
{
    XVariant* argument = NULL;
    XVariant* result = NULL;
#if XGUIAPPLICATION_ON && XINPUTMETHOD_ON
    XInputMethod* inputMethod = XGuiApplication_inputMethod();
    XInputMethodTransform transform;
    /* Qt 先把窗口坐标逆变换到焦点控件坐标，再发送查询事件。 */
    transform = inputMethod ? XInputMethod_inputItemTransform(inputMethod)
                            : (XInputMethodTransform){ 1.0f, 0.0f, 0.0f,
                                                       0.0f, 1.0f, 0.0f,
                                                       0.0f, 0.0f, 1.0f };
    position = xplatform_mapPointToInputItem(&transform, position);
    /* 用 User 变体传递转换后的浮点坐标，回调可按 data/dataSize 读取 XPointF。 */
    argument = XVariant_create((void*)&position, sizeof(position),
                                XVariantType_User);
    result = XInputMethod_queryFocusObject(query, argument);
    if (argument) XVariant_delete_base((XClass*)argument);
#else
    (void)query; (void)position;
#endif /* XGUIAPPLICATION_ON && XINPUTMETHOD_ON */
    return result;
}

XRectF XPlatformInputContext_inputItemRectangle(void)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
#if XGUIAPPLICATION_ON && XINPUTMETHOD_ON
    {
        XInputMethod* inputMethod = XGuiApplication_inputMethod();
        if (inputMethod) {
            XInputMethodTransform transform =
                XInputMethod_inputItemTransform(inputMethod);
            XRectF item = XInputMethod_inputItemRectangle(inputMethod);
            return xplatform_mapRectFromInputItem(&transform, &item);
        }
        return zero;
    }
#else
    return zero;
#endif /* XGUIAPPLICATION_ON && XINPUTMETHOD_ON */
}

XRectF XPlatformInputContext_inputItemClipRectangle(void)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
#if XGUIAPPLICATION_ON && XINPUTMETHOD_ON
    {
        XInputMethod* inputMethod = XGuiApplication_inputMethod();
        return inputMethod ? XInputMethod_inputItemClipRectangle(inputMethod) : zero;
    }
#else
    return zero;
#endif /* XGUIAPPLICATION_ON && XINPUTMETHOD_ON */
}

XRectF XPlatformInputContext_cursorRectangle(void)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
#if XGUIAPPLICATION_ON && XINPUTMETHOD_ON
    {
        XInputMethod* inputMethod = XGuiApplication_inputMethod();
        return inputMethod ? XInputMethod_cursorRectangle(inputMethod) : zero;
    }
#else
    return zero;
#endif /* XGUIAPPLICATION_ON && XINPUTMETHOD_ON */
}

XRectF XPlatformInputContext_anchorRectangle(void)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
#if XGUIAPPLICATION_ON && XINPUTMETHOD_ON
    {
        XInputMethod* inputMethod = XGuiApplication_inputMethod();
        return inputMethod ? XInputMethod_anchorRectangle(inputMethod) : zero;
    }
#else
    return zero;
#endif /* XGUIAPPLICATION_ON && XINPUTMETHOD_ON */
}

XRectF XPlatformInputContext_keyboardRectangle_static(void)
{
    XRectF zero = { 0.0f, 0.0f, 0.0f, 0.0f };
#if XGUIAPPLICATION_ON && XINPUTMETHOD_ON
    {
        XInputMethod* inputMethod = XGuiApplication_inputMethod();
        return inputMethod ? XInputMethod_keyboardRectangle(inputMethod) : zero;
    }
#else
    return zero;
#endif /* XGUIAPPLICATION_ON && XINPUTMETHOD_ON */
}

#endif /* XPLATFORMINPUTCTX_ON */
