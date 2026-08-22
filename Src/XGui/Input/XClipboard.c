/******************************************************************************
 * @file       XClipboard.c
 * @brief      XClipboard 剪贴板类实现（对标 Qt 6.8 QClipboard）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XClipboard.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"
#include <string.h>

#if XCLIPBOARD_ON

#include "XPixmap.h"
/** @brief 单个剪贴板模式的数据单元。 */
typedef struct XClipboardModeData
{
    XString*   m_text; /**< 纯文本（深拷贝）。 */
    XMimeData* m_mime; /**< MIME 数据（拥有）。 */
    bool       m_owns; /**< 数据是否由本进程写入。 */
} XClipboardModeData;

/** @brief XClipboard 私有数据块。 */
struct XClipboardPrivate
{
    XClipboardModeData m_modes[XClipboardMode_LastMode + 1]; /**< 三种模式各自的数据。 */
};

/** @brief 释放单个模式单元的全部资源。 */
static void clipboard_clearModeData(XClipboardModeData* data)
{
    if (!data)
        return;
    if (data->m_text) { XString_delete_base(data->m_text); data->m_text = NULL; }
    if (data->m_mime) { XMimeData_delete_base(data->m_mime); data->m_mime = NULL; }
    data->m_owns = false;
}

static void VXClipboard_deinit(XClipboard* self)
{
    int mode;
    if (!self) return;
    if (self->m_data) {
        for (mode = 0; mode <= (int)XClipboardMode_LastMode; ++mode)
            clipboard_clearModeData(&self->m_data->m_modes[mode]);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XClipboard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XClipboard)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClipboard_deinit);
    return XVTABLE_DEFAULT;
}

void XClipboard_init(XClipboard* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XClipboard));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XClipboard);
    self->m_data = (XClipboardPrivate*)XMalloc_System(sizeof(XClipboardPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XClipboardPrivate));
}

XClipboard* XClipboard_create_ex(XMemoryType memory)
{
    XClipboard* self = (XClipboard*)XMemory_malloc(sizeof(XClipboard), memory);
    if (!self) return NULL;
    XClipboard_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief 校验模式取值范围；越界归一到 Clipboard。 */
static XClipboardMode clipboard_normalizeMode(XClipboardMode mode)
{
    if (mode < XClipboardMode_Clipboard || mode > XClipboardMode_LastMode)
        return XClipboardMode_Clipboard;
    return mode;
}

/** @brief 发射信号并管理参数列表生命周期（与 XScreen/XWindow 相同模式）。 */
static void clipboard_emit(XClipboard* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/** @brief 数据变更统一出口：先发模式专用信号，再发 changed（与 Qt 顺序一致）。 */
static void clipboard_emitChanged(XClipboard* self, XClipboardMode mode)
{
    if (!self)
        return;
    if (mode == XClipboardMode_Clipboard)
        XClipboard_dataChanged_signal(self);
    else if (mode == XClipboardMode_Selection)
        XClipboard_selectionChanged_signal(self);
    else if (mode == XClipboardMode_FindBuffer)
        XClipboard_findBufferChanged_signal(self);
    XClipboard_changed_signal(self, mode);
}

bool XClipboard_supportsSelection(const XClipboard* self)
{
    (void)self;
    return false;
}

bool XClipboard_supportsFindBuffer(const XClipboard* self)
{
    (void)self;
    return false;
}

bool XClipboard_ownsClipboard(const XClipboard* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_modes[XClipboardMode_Clipboard].m_owns;
}

bool XClipboard_ownsSelection(const XClipboard* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_modes[XClipboardMode_Selection].m_owns;
}

bool XClipboard_ownsFindBuffer(const XClipboard* self)
{
    if (!self || !self->m_data) return false;
    return self->m_data->m_modes[XClipboardMode_FindBuffer].m_owns;
}

void XClipboard_clear(XClipboard* self, XClipboardMode mode)
{
    XClipboardModeData* data;
    if (!self || !self->m_data)
        return;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    clipboard_clearModeData(data);
    clipboard_emitChanged(self, mode);
}

XString* XClipboard_text(XClipboard* self, XClipboardMode mode)
{
    XClipboardModeData* data;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    if (!data->m_text)
        return NULL;
    return XString_create_copy(data->m_text);
}

XString* XClipboard_text_2(XClipboard* self, XString** subtype, XClipboardMode mode)
{
    XClipboardModeData* data;
    XString* text;
    if (subtype)
        *subtype = NULL;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    if (!data->m_text)
        return NULL;
    text = XString_create_copy(data->m_text);
    if (subtype)
        *subtype = XString_create_utf8("plain");
    return text;
}

void XClipboard_setText(XClipboard* self, const XString* text, XClipboardMode mode)
{
    XClipboardModeData* data;
    if (!self || !self->m_data)
        return;
    mode = clipboard_normalizeMode(mode);
    data = &self->m_data->m_modes[mode];
    clipboard_clearModeData(data);
    data->m_text = XString_create_copy(text);
    if (!data->m_text)
        data->m_text = XString_create_utf8("");
    data->m_owns = true;
    clipboard_emitChanged(self, mode);
}

const XMimeData* XClipboard_mimeData(const XClipboard* self, XClipboardMode mode)
{
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    return self->m_data->m_modes[mode].m_mime;
}

void XClipboard_setMimeData(XClipboard* self, XMimeData* data, XClipboardMode mode)
{
    XClipboardModeData* slot;
    if (!self || !self->m_data)
        return;
    mode = clipboard_normalizeMode(mode);
    slot = &self->m_data->m_modes[mode];
    clipboard_clearModeData(slot);
    slot->m_mime = data; /* 接管所有权 */
    slot->m_owns = data != NULL;
    clipboard_emitChanged(self, mode);
}

XImage* XClipboard_image(const XClipboard* self, XClipboardMode mode)
{
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    if (!self->m_data->m_modes[mode].m_mime)
        return NULL;
    return XMimeData_imageData(self->m_data->m_modes[mode].m_mime);
}

void XClipboard_setImage(XClipboard* self, const XImage* image, XClipboardMode mode)
{
#if XMIMEDATA_ON
    XMimeData* mime;
    if (!self)
        return;
    mime = XMimeData_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!mime)
        return;
    XMimeData_setImageData(mime, image);
    XClipboard_setMimeData(self, mime, mode);
#else
    (void)self; (void)image; (void)mode;
#endif /* XMIMEDATA_ON */
}

XPixmap* XClipboard_pixmap(const XClipboard* self, XClipboardMode mode)
{
    XImage* image;
    XPixmap* pixmap;
    if (!self || !self->m_data)
        return NULL;
    mode = clipboard_normalizeMode(mode);
    image = XClipboard_image(self, mode);
    if (!image)
        return NULL;
    pixmap = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (pixmap)
        XPixmap_fromImage(image, 0, pixmap);
    XImage_delete_base((XClass*)image);
    return pixmap;
}

void XClipboard_setPixmap(XClipboard* self, const XPixmap* pixmap, XClipboardMode mode)
{
    XImage* image;
    XPixmap* copy;
    if (!self || !pixmap)
        return;
    image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!image)
        return;
    copy = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (copy) {
        XPixmap_copy_base(copy, pixmap);
        XPixmap_toImage(copy, image);
        XPixmap_delete_base((XClass*)copy);
    }
    XClipboard_setImage(self, image, mode);
    XImage_delete_base((XClass*)image);
}

/* ==================== 信号（4 个，对标 QClipboard 全部信号） ==================== */

void* XClipboard_changed_signal(XClipboard* self, XClipboardMode mode)
{
    if (!self) return (void*)(size_t)XClipboard_changed_signal;
    clipboard_emit(self, (size_t)XClipboard_changed_signal,
                   XVarList_Create(XVar(XClipboardMode, mode)));
    return (void*)(size_t)XClipboard_changed_signal;
}

void* XClipboard_selectionChanged_signal(XClipboard* self)
{
    if (!self) return (void*)(size_t)XClipboard_selectionChanged_signal;
    clipboard_emit(self, (size_t)XClipboard_selectionChanged_signal, NULL);
    return (void*)(size_t)XClipboard_selectionChanged_signal;
}

void* XClipboard_findBufferChanged_signal(XClipboard* self)
{
    if (!self) return (void*)(size_t)XClipboard_findBufferChanged_signal;
    clipboard_emit(self, (size_t)XClipboard_findBufferChanged_signal, NULL);
    return (void*)(size_t)XClipboard_findBufferChanged_signal;
}

void* XClipboard_dataChanged_signal(XClipboard* self)
{
    if (!self) return (void*)(size_t)XClipboard_dataChanged_signal;
    clipboard_emit(self, (size_t)XClipboard_dataChanged_signal, NULL);
    return (void*)(size_t)XClipboard_dataChanged_signal;
}

#endif /* XCLIPBOARD_ON */
