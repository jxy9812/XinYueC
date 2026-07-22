/******************************************************************************
 * @file       XPicture.c
 * @brief      XPicture 绘图指令记录与回放类实现（对标 Qt 6.8 QPicture）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XPicture.h"
#include "XCode/XAtomic/XAtomic.h"
#include "XClass/XClass.h"
#include "XClass/XVtable/XVtable.h"
#include "XMemory/XMemory.h"
#include <string.h>
#include <stdlib.h>

/**
 * @brief      XPicture 私有数据结构
 */
typedef struct XPicturePrivate
{
    XAtomic_int32_t  m_refCount;    /**< 引用计数 */
    char*            m_data;        /**< 绘图指令数据 */
    uint32_t         m_dataSize;    /**< 数据大小 */
    uint32_t         m_dataCapacity;/**< 数据容量 */
    bool             m_isNull;      /**< 是否为 null */
    int              m_formatVersion; /**< 格式版本号 */
    int              m_boundingX;   /**< 边界矩形 X */
    int              m_boundingY;   /**< 边界矩形 Y */
    int              m_boundingW;   /**< 边界矩形宽度 */
    int              m_boundingH;   /**< 边界矩形高度 */
}XPicturePrivate;

static XPicturePrivate* XPicturePrivate_create(int formatVersion)
{
    XPicturePrivate* d = (XPicturePrivate*)XMalloc_System(sizeof(XPicturePrivate));
    if (!d) return NULL;
    memset(d, 0, sizeof(XPicturePrivate));
    XAtomic_init(d->m_refCount, 1);
    d->m_isNull = true;
    d->m_formatVersion = formatVersion;
    return d;
}

static void XPicturePrivate_ref(XPicturePrivate* d) { if (d) XAtomic_fetch_add_int32(&d->m_refCount, 1, XAtomic_MemoryOrder_SeqCst); }

static void XPicturePrivate_unref(XPicturePrivate* d)
{
    if (!d) return;
    if (XAtomic_fetch_add_int32(&d->m_refCount, -1, XAtomic_MemoryOrder_SeqCst) == 1)
    {
        if (d->m_data) XFree_System(d->m_data);
        XFree_System(d);
    }
}

static void VXPicture_copy(XPicture* dest, const XPicture* src)
{
    if (ISNULL(dest, "XPicture") || ISNULL(src, "XPicture")) return;
    if (dest->m_data)
        XPicturePrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    XPicturePrivate_ref(dest->m_data);
}

static void VXPicture_move(XPicture* dest, XPicture* src)
{
    if (ISNULL(dest, "XPicture") || ISNULL(src, "XPicture")) return;
    if (dest->m_data)
        XPicturePrivate_unref(dest->m_data);
    dest->m_data = src->m_data;
    src->m_data = NULL;
}

static void VXPicture_deinit(XPicture* self)
{
    if (ISNULL(self, "XPicture")) return;
    if (self->m_data)
    {
        XPicturePrivate_unref(self->m_data);
        self->m_data = NULL;
    }
}

XVtable* XPicture_class_init()
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XPicture));
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPicture_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPicture_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPicture_deinit);
    return XVTABLE_DEFAULT;
}

XPicture* XPicture_create()
{
    XPicture* self = (XPicture*)XMalloc_System(sizeof(XPicture));
    if (!self) return NULL;
    XPicture_init(self, -1);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

void XPicture_init(XPicture* self, int formatVersion)
{
    if (ISNULL(self, "XPicture")) return;
    memset(self, 0, sizeof(XPicture));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XPicture);
    self->m_data = XPicturePrivate_create(formatVersion);
}

void XPicture_copy(XPicture* self, const XPicture* other)
{
    if (ISNULL(self, "XPicture") || ISNULL(other, "XPicture")) return;
    if (!XClassIsVtableNull(self))
        XPicture_deinit_base(self);
    XPicture_init(self, -1);
    XPicture_copy_base(self, other);
}
void XPicture_deinit(XPicture* self) { XPicture_deinit_base(self); }
void XPicture_copy_base(XPicture* dest, const XPicture* src)
{
    if (ISNULL(dest, "XPicture") || ISNULL(src, "XPicture")) return;
    VXPicture_copy(dest, src);
}
void XPicture_move(XPicture* self, XPicture* other)
{
    if (ISNULL(self, "XPicture") || ISNULL(other, "XPicture")) return;
    if (!XClassIsVtableNull(self))
        XPicture_deinit_base(self);
    XPicture_init(self, -1);
    XPicture_move_base(self, other);
}

void XPicture_move_base(XPicture* dest, XPicture* src)
{
    if (ISNULL(dest, "XPicture") || ISNULL(src, "XPicture")) return;
    VXPicture_move(dest, src);
}

void XPicture_deinit_base(XPicture* self)
{
    if (ISNULL(self, "XPicture")) return;
    VXPicture_deinit(self);
}



void XPicture_delete_base(XPicture* self)
{
    XClass_delete_base((XClass*)self);
}
bool XPicture_isNull(const XPicture* self) { return !self || !self->m_data || self->m_data->m_isNull; }
uint32_t XPicture_size(const XPicture* self) { return (self && self->m_data) ? self->m_data->m_dataSize : 0; }
const char* XPicture_data(const XPicture* self) { return (self && self->m_data) ? self->m_data->m_data : NULL; }

void XPicture_setData(XPicture* self, const char* data, uint32_t size)
{
    if (!self || !self->m_data) return;
    if (self->m_data->m_data) XFree_System(self->m_data->m_data);
    self->m_data->m_data = (char*)XMalloc_System(size);
    if (self->m_data->m_data)
    {
        memcpy(self->m_data->m_data, data, size);
        self->m_data->m_dataSize = size;
        self->m_data->m_dataCapacity = size;
        self->m_data->m_isNull = false;
    }
}

void XPicture_boundingRect(const XPicture* self, XRect* out)
{
    if (out)
    {
        out->x = (self && self->m_data) ? self->m_data->m_boundingX : 0;
        out->y = (self && self->m_data) ? self->m_data->m_boundingY : 0;
        out->width = (self && self->m_data) ? self->m_data->m_boundingW : 0;
        out->height = (self && self->m_data) ? self->m_data->m_boundingH : 0;
    }
}

void XPicture_setBoundingRect(XPicture* self, const XRect* rect)
{
    if (!self || !self->m_data || !rect) return;
    self->m_data->m_boundingX = rect->x;
    self->m_data->m_boundingY = rect->y;
    self->m_data->m_boundingW = rect->width;
    self->m_data->m_boundingH = rect->height;
}

bool XPicture_play(XPicture* self, void* painter) { (void)self; (void)painter; return false; }
bool XPicture_load(XPicture* self, const char* fileName) { (void)self; (void)fileName; return false; }
bool XPicture_save(const XPicture* self, const char* fileName) { (void)self; (void)fileName; return false; }

void XPicture_detach(XPicture* self)
{
    if (!self || !self->m_data) return;
    if (XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) > 1)
    {
        XPicturePrivate* newData = XPicturePrivate_create(self->m_data->m_formatVersion);
        if (newData)
        {
            newData->m_isNull = self->m_data->m_isNull;
            newData->m_boundingX = self->m_data->m_boundingX;
            newData->m_boundingY = self->m_data->m_boundingY;
            newData->m_boundingW = self->m_data->m_boundingW;
            newData->m_boundingH = self->m_data->m_boundingH;
            if (self->m_data->m_data && self->m_data->m_dataSize > 0)
                XPicture_setData((XPicture*)self, self->m_data->m_data, self->m_data->m_dataSize);
            XPicturePrivate_unref(self->m_data);
            self->m_data = newData;
        }
    }
}

bool XPicture_isDetached(const XPicture* self)
{
    return !self || !self->m_data || XAtomic_load_int32(&self->m_data->m_refCount, XAtomic_MemoryOrder_Relaxed) == 1;
}



