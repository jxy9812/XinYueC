/**
 * @file       XVirtualKeyboardObserver.c
 * @brief      XVirtualKeyboardObserver 键盘布局观察器实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardObserver.h"
#include "XVirtualKeyboardObserver_Protected.h"
#include "XStringUtils.h"
#include "XString.h" /* XString_toVariant_utf8（防 C4013 隐式声明）。 */
#include "XMemory.h"

/** @brief 进程单例指针。 */
static XVirtualKeyboardObserver* s_xvkoInstance = NULL;

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardObserverPrivate
{
    char m_layoutType[32];  /**< 布局类型描述（默认 "main"）。 */
    char m_locale[32];      /**< 区域语言（默认 "zh_CN"）。 */
    int m_inputMode;        /**< 输入模式枚举值（默认 0=Latin）。 */
} XVirtualKeyboardObserverPrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardObserverPrivate* xvko_priv(
        const XVirtualKeyboardObserver* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardObserverPrivate*)self->m_data : NULL;
}

/** @brief 发射信号。 */
static void xvko_emit0(XVirtualKeyboardObserver* self, size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/* 前向声明。 */
static void XVko_deinit(XVirtualKeyboardObserver* self);
void XVirtualKeyboardObserver_init(XVirtualKeyboardObserver* self);

XVtable* XVirtualKeyboardObserver_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardObserver)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVko_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：释放私有块后调父类。 */
static void XVko_deinit(XVirtualKeyboardObserver* self)
{
    if (!self) return;
    if (self->m_data) {
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XVirtualKeyboardObserver_init(XVirtualKeyboardObserver* self)
{
    XVirtualKeyboardObserverPrivate* priv;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardObserver);
    self->m_data = XMalloc_System(sizeof(XVirtualKeyboardObserverPrivate));
    if (!self->m_data) return;
    priv = (XVirtualKeyboardObserverPrivate*)self->m_data;
    XMemset(priv, 0, sizeof(*priv));
    XStrncpy(priv->m_layoutType, "main", sizeof(priv->m_layoutType));
    XStrncpy(priv->m_locale, "zh_CN", sizeof(priv->m_locale));
    priv->m_inputMode = 0;
}

XVirtualKeyboardObserver* XVirtualKeyboardObserver_create_ex(
        XMemoryType memory)
{
    XVirtualKeyboardObserver* self =
        (XVirtualKeyboardObserver*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVirtualKeyboardObserver_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XVirtualKeyboardObserver* XVirtualKeyboardObserver_instance(void)
{
    if (!s_xvkoInstance) {
        XVirtualKeyboardObserver* self =
            (XVirtualKeyboardObserver*)XMalloc_System(
                sizeof(XVirtualKeyboardObserver));
        if (!self) return NULL;
        XVirtualKeyboardObserver_init(self);
        Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(self, true);
        s_xvkoInstance = self;
    }
    return s_xvkoInstance;
}

XVariant* XVirtualKeyboardObserver_layout(
        const XVirtualKeyboardObserver* self)
{
    XVirtualKeyboardObserverPrivate* priv = xvko_priv(self);
    char descriptor[96];
    if (!priv) return NULL;
    /* 描述符格式 "<layoutType>/<locale>/<inputMode>"。 */
    XStrncpy(descriptor, priv->m_layoutType, sizeof(descriptor));
    XStrcat(descriptor, "/");
    XStrcat(descriptor, priv->m_locale);
    XStrcat(descriptor, "/");
    {
        char modeText[12];
        char tmp[12];
        int value = priv->m_inputMode;
        int n = 0;
        int i;
        if (value < 0) value = 0;
        if (value == 0) {
            modeText[n++] = '0';
        } else {
            while (value > 0 && n < 11) {
                tmp[n++] = (char)('0' + (value % 10));
                value /= 10;
            }
            for (i = 0; i < n; ++i)
                modeText[i] = tmp[n - 1 - i];
        }
        modeText[n] = '\0';
        XStrcat(descriptor, modeText);
    }
    return XString_toVariant_utf8(descriptor);
}

void XVirtualKeyboardObserver_invalidateLayout(
        XVirtualKeyboardObserver* observer, const char* layoutType,
        const char* locale, int inputMode)
{
    XVirtualKeyboardObserverPrivate* priv = xvko_priv(observer);
    bool changed = false;
    if (!priv) return;
    if (layoutType && XStrcmp(priv->m_layoutType, layoutType) != 0) {
        XStrncpy(priv->m_layoutType, layoutType,
                 sizeof(priv->m_layoutType));
        changed = true;
    }
    if (locale && XStrcmp(priv->m_locale, locale) != 0) {
        XStrncpy(priv->m_locale, locale, sizeof(priv->m_locale));
        changed = true;
    }
    if (priv->m_inputMode != inputMode) {
        priv->m_inputMode = inputMode;
        changed = true;
    }
    if (changed)
        xvko_emit0(observer, (size_t)
                   XVirtualKeyboardObserver_layoutChanged_signal(NULL));
}

void* XVirtualKeyboardObserver_layoutChanged_signal(
        XVirtualKeyboardObserver* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardObserver_layoutChanged_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
