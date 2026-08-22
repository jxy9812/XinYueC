/******************************************************************************
 * @file       XCursor.c
 * @brief      XCursor 光标类实现（对标 Qt 6.8 QCursor）。
 * @details    本文件实现 XCursor 的光标形状、位图/掩码/像素图自定义光标、
 *             热点与进程级光标位置。复制语义与 Qt 隐式共享的 QCursor 对齐：
 *             XCursor_copy_base 深拷贝全部自定义资源（位图/掩码/像素图），
 *             移动语义转移资源并使源对象回到默认空光标。进程级光标位置
 *             保存在静态变量中，无平台输入后端时由 XCursor_setPos 维护。
 * @note       模块总开关 XCURSOR_ON 定义于 XGuiConfig.h；置 0 时本文件
 *             实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCursor.h"
#include "XBitmap.h"
#include "XPixmap.h"
#include <string.h>

#if XCURSOR_ON

/* ==================== 进程级状态 ==================== */

/** @brief 进程级当前光标位置；无平台后端时由 setPos 维护。 */
static XPoint g_cursorPos = {0, 0};

/* ==================== 资源复制辅助 ==================== */

/** @brief 深拷贝位图；源为 NULL 返回 NULL。 */
static struct XBitmap* XCursor_copyBitmap(const struct XBitmap* source)
{
    struct XBitmap* copy;
    if (!source) return NULL;
    copy = XBitmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XBitmap_copy_base(copy, source);
    return copy;
}

/** @brief 深拷贝像素图；源为 NULL 返回 NULL。 */
static struct XPixmap* XCursor_copyPixmap(const struct XPixmap* source)
{
    struct XPixmap* copy;
    if (!source) return NULL;
    copy = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XPixmap_copy_base(copy, source);
    return copy;
}

/** @brief 释放光标自有资源并复位默认值，保留对象外壳（对标 QCursor 复位为 Arrow）。 */
static void XCursor_resetResources(XCursor* self)
{
    if (!self) return;
    if (self->m_bitmap) XBitmap_delete_base(self->m_bitmap);
    if (self->m_mask) XBitmap_delete_base(self->m_mask);
    if (self->m_pixmap) XPixmap_delete_base(self->m_pixmap);
    self->m_bitmap = NULL;
    self->m_mask = NULL;
    self->m_pixmap = NULL;
    self->m_shape = XCursor_Arrow;
    self->m_hotSpot.x = -1;
    self->m_hotSpot.y = -1;
    self->m_hasHotSpot = false;
}

/* ==================== 虚函数表与生命周期 ==================== */

static void VXCursor_deinit(XCursor* self);
static void VXCursor_copy(XCursor* self, const XCursor* other);
static void VXCursor_move(XCursor* self, XCursor* other);

XVtable* XCursor_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCursor)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCursor_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXCursor_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXCursor_move);
    return XVTABLE_DEFAULT;
}

void XCursor_init(XCursor* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XCursor));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XCursor);
    self->m_shape = XCursor_Arrow;
    self->m_hotSpot.x = -1;
    self->m_hotSpot.y = -1;
    self->m_hasHotSpot = false;
}

XCursor* XCursor_create_ex(XMemoryType memory)
{
    XCursor* self = (XCursor*)XMemory_malloc(sizeof(XCursor), memory);
    if (!self) return NULL;
    XCursor_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XCursor* XCursor_create_shape(XCursorShape shape)
{
    XCursor* self = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    self->m_shape = shape;
    return self;
}

XCursor* XCursor_create_bitmap(const struct XBitmap* bitmap,
                               const struct XBitmap* mask,
                               int hotX, int hotY)
{
    XCursor* self = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    self->m_bitmap = XCursor_copyBitmap(bitmap);
    self->m_mask = XCursor_copyBitmap(mask);
    self->m_shape = XCursor_Bitmap;
    self->m_hotSpot.x = hotX;
    self->m_hotSpot.y = hotY;
    self->m_hasHotSpot = (hotX >= 0 && hotY >= 0);
    return self;
}

XCursor* XCursor_create_pixmap(const struct XPixmap* pixmap,
                               int hotX, int hotY)
{
    XCursor* self = XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    self->m_pixmap = XCursor_copyPixmap(pixmap);
    self->m_shape = XCursor_Custom;
    self->m_hotSpot.x = hotX;
    self->m_hotSpot.y = hotY;
    self->m_hasHotSpot = (hotX >= 0 && hotY >= 0);
    return self;
}

static void VXCursor_deinit(XCursor* self)
{
    if (!self) return;
    XCursor_resetResources(self);
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXCursor_copy(XCursor* self, const XCursor* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XCursor_init(self);
    XCursor_resetResources(self);
    self->m_shape = other->m_shape;
    self->m_hotSpot = other->m_hotSpot;
    self->m_hasHotSpot = other->m_hasHotSpot;
    self->m_bitmap = XCursor_copyBitmap(other->m_bitmap);
    self->m_mask = XCursor_copyBitmap(other->m_mask);
    self->m_pixmap = XCursor_copyPixmap(other->m_pixmap);
}

static void VXCursor_move(XCursor* self, XCursor* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XCursor_init(self);
    XCursor_resetResources(self);
    self->m_shape = other->m_shape;
    self->m_hotSpot = other->m_hotSpot;
    self->m_hasHotSpot = other->m_hasHotSpot;
    self->m_bitmap = other->m_bitmap;
    self->m_mask = other->m_mask;
    self->m_pixmap = other->m_pixmap;
    /* 置空源对象资源，使源回到默认空光标。 */
    other->m_bitmap = NULL;
    other->m_mask = NULL;
    other->m_pixmap = NULL;
    other->m_shape = XCursor_Arrow;
    other->m_hotSpot.x = -1;
    other->m_hotSpot.y = -1;
    other->m_hasHotSpot = false;
}

/* ==================== 形状 ==================== */

XCursorShape XCursor_shape(const XCursor* self)
{
    if (!self) return XCursor_Arrow;
    return self->m_shape;
}

void XCursor_setShape(XCursor* self, XCursorShape shape)
{
    if (!self) return;
    XCursor_resetResources(self);
    self->m_shape = shape;
}

/* ==================== 自定义资源 ==================== */

struct XBitmap* XCursor_bitmap(const XCursor* self)
{
    return self ? self->m_bitmap : NULL;
}

struct XBitmap* XCursor_mask(const XCursor* self)
{
    return self ? self->m_mask : NULL;
}

struct XPixmap* XCursor_pixmap(const XCursor* self)
{
    return self ? self->m_pixmap : NULL;
}

/* ==================== 热点 ==================== */

XPoint XCursor_hotSpot(const XCursor* self)
{
    XPoint invalid = {-1, -1};
    if (!self || !self->m_hasHotSpot) return invalid;
    return self->m_hotSpot;
}

void XCursor_setHotSpot(XCursor* self, int x, int y)
{
    if (!self) return;
    self->m_hotSpot.x = x;
    self->m_hotSpot.y = y;
    self->m_hasHotSpot = (x >= 0 && y >= 0);
}

/* ==================== 进程级光标位置 ==================== */

XPoint XCursor_pos(void)
{
    return g_cursorPos;
}

void XCursor_setPos(int x, int y)
{
    g_cursorPos.x = x;
    g_cursorPos.y = y;
}

void XCursor_setPos_point(const XPoint* pos)
{
    g_cursorPos = pos ? *pos : (XPoint){0, 0};
}

/* ==================== 形状判断 ==================== */

bool XCursor_isShapeCursor(const XCursor* self)
{
    if (!self) return true;
    return self->m_shape < XCursor_Bitmap;
}

#endif /* XCURSOR_ON */
