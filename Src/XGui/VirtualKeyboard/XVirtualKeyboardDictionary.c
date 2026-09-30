/**
 * @file       XVirtualKeyboardDictionary.c
 * @brief      XVirtualKeyboardDictionary 键盘词典实现（内存面；持久化
 *             /学习 N-A）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardDictionary.h"
#include "XVirtualKeyboardDictionaryManager_Protected.h"
#include "XStringUtils.h"
#include "XMemory.h"

/** @brief 词条容量上限（单词典；防御性钳制）。 */
#define XVKD_WORD_MAX 1024

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardDictionaryPrivate
{
    char* m_name;       /**< 词典名（堆拷贝）。 */
    char** m_contents;  /**< 词条数组（堆拷贝）。 */
    int m_count;        /**< 词条数。 */
} XVirtualKeyboardDictionaryPrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardDictionaryPrivate* xvkd_priv(
        const XVirtualKeyboardDictionary* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardDictionaryPrivate*)self->m_data : NULL;
}

/** @brief 发射信号。 */
static void xvkd_emit(XVirtualKeyboardDictionary* self, size_t signal,
                      XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args)
        XVarList_delete(args);
}

/** @brief 释放词条数组。 */
static void xvkd_clearContents(XVirtualKeyboardDictionaryPrivate* priv)
{
    int i;
    if (!priv) return;
    for (i = 0; i < priv->m_count; ++i) {
        if (priv->m_contents[i]) XFree_System(priv->m_contents[i]);
    }
    if (priv->m_contents) XFree_System(priv->m_contents);
    priv->m_contents = NULL;
    priv->m_count = 0;
}

/* 前向声明。 */
static void XVkd_deinit(XVirtualKeyboardDictionary* self);
void XVirtualKeyboardDictionary_init(XVirtualKeyboardDictionary* self);
XVirtualKeyboardDictionary* XVirtualKeyboardDictionary_create_named(
        const char* name);

XVtable* XVirtualKeyboardDictionary_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardDictionary)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkd_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：释放名字/词条/私有块后调父类。 */
static void XVkd_deinit(XVirtualKeyboardDictionary* self)
{
    XVirtualKeyboardDictionaryPrivate* priv;
    if (!self) return;
    priv = xvkd_priv(self);
    if (priv) {
        if (priv->m_name) XFree_System(priv->m_name);
        xvkd_clearContents(priv);
        XFree_System(priv);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XVirtualKeyboardDictionary_init(XVirtualKeyboardDictionary* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardDictionary);
    self->m_data = XMalloc_System(sizeof(XVirtualKeyboardDictionaryPrivate));
    if (self->m_data) XMemset(self->m_data, 0,
                              sizeof(XVirtualKeyboardDictionaryPrivate));
}

/**
 * @brief      管理器 TU 专用创建入口（Qt friend 私有构造口径）。
 * @param      name 词典名（UTF-8 借用；拷贝存储）。
 * @return     新词典对象；失败返回 NULL。
 */
XVirtualKeyboardDictionary* XVirtualKeyboardDictionary_create_named(
        const char* name)
{
    XVirtualKeyboardDictionary* self =
        (XVirtualKeyboardDictionary*)XMalloc_System(
            sizeof(XVirtualKeyboardDictionary));
    XVirtualKeyboardDictionaryPrivate* priv;
    if (!self) return NULL;
    XVirtualKeyboardDictionary_init(self);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    priv = xvkd_priv(self);
    if (priv && name && name[0]) priv->m_name = XStrdup(name);
    return self;
}

const char* XVirtualKeyboardDictionary_name(
        const XVirtualKeyboardDictionary* self)
{
    XVirtualKeyboardDictionaryPrivate* priv = xvkd_priv(self);
    return priv ? priv->m_name : NULL;
}

int XVirtualKeyboardDictionary_contentsCount(
        const XVirtualKeyboardDictionary* self)
{
    XVirtualKeyboardDictionaryPrivate* priv = xvkd_priv(self);
    return priv ? priv->m_count : 0;
}

const char* XVirtualKeyboardDictionary_contentsAt(
        const XVirtualKeyboardDictionary* self, int index)
{
    XVirtualKeyboardDictionaryPrivate* priv = xvkd_priv(self);
    if (!priv || index < 0 || index >= priv->m_count) return NULL;
    return priv->m_contents[index];
}

void XVirtualKeyboardDictionary_setContents(XVirtualKeyboardDictionary* self,
                                            const char* const* contents,
                                            int count)
{
    XVirtualKeyboardDictionaryPrivate* priv = xvkd_priv(self);
    int i;
    int n = 0;
    if (!priv) return;
    if (count > XVKD_WORD_MAX) count = XVKD_WORD_MAX;
    xvkd_clearContents(priv);
    if (contents && count > 0) {
        priv->m_contents =
            (char**)XMalloc_System((size_t)count * sizeof(char*));
        if (priv->m_contents) {
            for (i = 0; i < count && contents[i]; ++i) {
                priv->m_contents[n] = XStrdup(contents[i]);
                if (priv->m_contents[n]) ++n;
            }
        }
    }
    priv->m_count = n;
    xvkd_emit(self, (size_t)
                  XVirtualKeyboardDictionary_contentsChanged_signal(NULL),
              NULL);
}

void XVirtualKeyboardDictionary_resetContents(
        XVirtualKeyboardDictionary* self)
{
    XVirtualKeyboardDictionaryPrivate* priv = xvkd_priv(self);
    if (!priv) return;
    if (priv->m_count == 0) return;
    xvkd_clearContents(priv);
    xvkd_emit(self, (size_t)
                  XVirtualKeyboardDictionary_contentsChanged_signal(NULL),
              NULL);
}

void* XVirtualKeyboardDictionary_contentsChanged_signal(
        XVirtualKeyboardDictionary* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardDictionary_contentsChanged_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
