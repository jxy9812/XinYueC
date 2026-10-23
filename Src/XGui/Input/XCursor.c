/******************************************************************************
 * @file       XCursor.c
 * @brief      XCursor 光标类实现（对标 Qt 6.8 QCursor）。
 * @details    本文件实现 XCursor 的光标形状、位图/掩码/像素图自定义光标、
 *             热点与进程级光标位置。复制语义与 Qt 隐式共享的 QCursor 对齐：
 *             XClassCopy 深拷贝全部自定义资源（位图/掩码/像素图），
 *             移动语义转移资源并使源对象回到默认空光标。进程级光标位置
 *             保存在静态变量中，无平台输入后端时由 XCursor_setPos 维护。
 * @note       模块总开关 XCURSOR_ON 定义于 XGuiConfig.h；置 0 时本文件
 *             实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XCursor.h"
#include "XMemory.h"

#include <stdio.h> /* [SWC 探针] fprintf（XGUI_SWCURSOR_DEBUG 诊断）。 */
#include <stdlib.h> /* [SWC 探针] getenv。 */

#include "XAlgorithm.h"
#include "XBitmap.h"
#include "XPixmap.h"

#if XGUIAPPLICATION_ON
#include "XGuiApplication.h" /* overrideCursor：软件精灵有效形状查询。 */
#endif

#if XCURSOR_ON

/* ==================== 进程级状态 ==================== */

/** @brief 进程级当前光标位置；无平台后端时由 setPos 维护。 */
static XPoint g_cursorPos = {0, 0};

/** @brief 平台光标后端（由平台原生窗口后端注册；NULL 表示无后端）。 */
static const XCursorPlatformBackend* g_cursorBackend = NULL;

/* ==================== 软件光标精灵（无硬件光标平台触摸跟随） ======== */

/** @brief 软件精灵运行时开关（默认开；板级可用 setSoftware... 关闭）。 */
static bool g_cursorSwEnabled = true;

/** @brief 是否已收到过触摸位置（XCursor_setPos 首次调用即置位；精灵
 *  激活条件之一——位置未知的精灵画在 (0,0) 属视觉垃圾）。 */
static bool g_cursorSwHasPos = false;

/** @brief 最近一次 XCursor_applyToWindow 记录的形状（XWidget/window
 *  setCursor 接线在 fbdev 无后端时仅存储不生效，这里补一份形状账，
 *  供无 override 时的有效形状回退；clearForWindow 复位为 Arrow）。 */
static XCursorShape g_cursorSwLastShape = XCursor_Arrow;

/** @brief 软件光标位置参考坐标系（XCursor_swSetPosFrame 维护，全 0 初始
 *  即恒等系；语义详见 XCursor.h 同名注释）。pos 与本系必须成对更新：
 *  呈现层按本系把 pos 正变换到面板像素，任一侧单独换新即精灵错位/多
 *  路径跳变。 */
static int g_cursorSwFrameRot = 0;
static int g_cursorSwFrameLogW = 0;
static int g_cursorSwFrameLogH = 0;
static XPoint g_cursorSwFrameOrigin = {0, 0};

/**
 * @brief  内建经典箭头位图（12x19，黑描边白填充，热点约 (1,1)）。
 * @details 行主序，每像素 2bit 值域按字节存储：0=透明/1=黑/2=白。
 *          形状为经典 X11 left_ptr 口径：左侧竖直黑描边、内部白色
 *          三角箭头、右下小尾巴；平台呈现层按面板位深把 1/2 映射为
 *          黑/白像素值（RGB565 黑=0x0000 白=0xFFFF 等）。
 */
static const uint8_t g_cursorSwArrowBits[XCURSOR_SW_ARROW_W *
                                         XCURSOR_SW_ARROW_H] = {
    /* #........... */
    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* ##.......... */
    1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* #o#......... */
    1, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* #oo#........ */
    1, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    /* #ooo#....... */
    1, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0, 0,
    /* #oooo#...... */
    1, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0, 0,
    /* #ooooo#..... */
    1, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0,
    /* #oooooo#.... */
    1, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0,
    /* #ooooooo#... */
    1, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0,
    /* #oooooooo#.. */
    1, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0,
    /* #ooooooooo#. */
    1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0,
    /* #ooooo#####. */
    1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 0,
    /* #oo#oo#..... */
    1, 2, 2, 1, 2, 2, 1, 0, 0, 0, 0, 0,
    /* #o##oo#..... */
    1, 2, 1, 1, 2, 2, 1, 0, 0, 0, 0, 0,
    /* ##.#oo#..... */
    1, 1, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0,
    /* ...#oo#..... */
    0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0, 0,
    /* ....#oo#.... */
    0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0,
    /* ....#oo#.... */
    0, 0, 0, 0, 1, 2, 2, 1, 0, 0, 0, 0,
    /* .....##..... */
    0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0};

void XCursor_installPlatformBackend(const XCursorPlatformBackend* backend)
{
    g_cursorBackend = backend;
}

bool XCursor_applyToWindow(uintptr_t nativeWindowId, const XCursor* cursor)
{
    /* 软件精灵形状账：apply 即记录（先于无后端早退——fbdev 无后端时
       本调用恒 false，但形状账正是为该态服务的）。NULL 光标=清除语义，
       账复位 Arrow（与 unsetCursor 恢复默认箭头同义）。 */
    if (cursor)
        g_cursorSwLastShape = cursor->m_shape;
    else
        g_cursorSwLastShape = XCursor_Arrow;
    {
        /* [SWC 探针 2026-10-23] 形状账变更一行账（精灵隐藏/变 形 的排查
         * 入口；XGUI_SWCURSOR_DEBUG=1 开）。 */
        static signed char swcDbg = -1;
        if (swcDbg < 0) swcDbg = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
        if (swcDbg)
            fprintf(stderr, "[SWC] shape-account -> %d (win=%p hasCursor=%d)\n",
                    (int)g_cursorSwLastShape, (void*)nativeWindowId,
                    cursor ? 1 : 0);
    }
    /* 平台应用光标（XDefineCursor 路径）；无后端/窗口无效时静默 false。 */
    if (!g_cursorBackend || !g_cursorBackend->m_applyWindowCursor ||
        nativeWindowId == 0)
        return false;
    return g_cursorBackend->m_applyWindowCursor(nativeWindowId, cursor);
}

bool XCursor_clearForWindow(uintptr_t nativeWindowId)
{
    g_cursorSwLastShape = XCursor_Arrow; /* 清除即回默认箭头账。 */
    {
        /* [SWC 探针 2026-10-23] 形状账变更一行账（同 applyToWindow）。 */
        static signed char swcDbg = -1;
        if (swcDbg < 0) swcDbg = (getenv("XGUI_SWCURSOR_DEBUG") != NULL);
        if (swcDbg)
            fprintf(stderr, "[SWC] shape-account -> 0 (clear win=%p)\n",
                    (void*)nativeWindowId);
    }
    if (!g_cursorBackend || !g_cursorBackend->m_clearWindowCursor ||
        nativeWindowId == 0)
        return false;
    return g_cursorBackend->m_clearWindowCursor(nativeWindowId);
}

/* ---- 软件精灵公共 API ---- */

void XCursor_setSoftwareCursorEnabled(bool enabled)
{
    g_cursorSwEnabled = enabled;
}

bool XCursor_softwareCursorEnabled(void)
{
    return g_cursorSwEnabled;
}

/**
 * @brief      软件精灵当前有效形状（override 优先，形状账次之）。
 * @details    除 Blank 隐藏外，其它形状（含 Bitmap/Custom）绘制一律
 *             回落内建箭头位图——嵌入式软件精灵不做逐形状位图集。
 */
static XCursorShape xcursor_swEffectiveShape(void)
{
#if XGUIAPPLICATION_ON
    const XCursor* overrideCursor = XGuiApplication_overrideCursor();
    if (overrideCursor) return XCursor_shape(overrideCursor);
#endif
    return g_cursorSwLastShape;
}

bool XCursor_swActive(void)
{
    if (!g_cursorSwEnabled) return false;
    if (g_cursorBackend) return false; /* 硬件后端在位：软件路径整体休眠。 */
    if (!g_cursorSwHasPos) return false; /* 未知位置不画（防 (0,0) 垃圾）。 */
    return xcursor_swEffectiveShape() != XCursor_Blank;
}

XRect XCursor_swRect(void)
{
    XRect rect;
    XPoint pos;
    XPoint hotSpot;
    XRect_init(&rect, 0, 0, 0, 0);
    if (!XCursor_swActive()) return rect; /* 未激活：全零空矩形。 */
    pos = XCursor_pos();
    hotSpot = XCursor_swArrowHotSpot();
    XRect_init(&rect, pos.x - hotSpot.x, pos.y - hotSpot.y,
               XCURSOR_SW_ARROW_W, XCURSOR_SW_ARROW_H);
    return rect;
}

const uint8_t* XCursor_swArrowBits(void)
{
    return g_cursorSwArrowBits;
}

int XCursor_swArrowWidth(void)
{
    return XCURSOR_SW_ARROW_W;
}

int XCursor_swArrowHeight(void)
{
    return XCURSOR_SW_ARROW_H;
}

XPoint XCursor_swArrowHotSpot(void)
{
    XPoint hotSpot;
    XPoint_init(&hotSpot, 1, 1);
    return hotSpot;
}

void XCursor_swSetPosFrame(int rot, int logW, int logH, const XPoint* origin)
{
    g_cursorSwFrameRot = rot;
    g_cursorSwFrameLogW = logW;
    g_cursorSwFrameLogH = logH;
    if (origin)
        g_cursorSwFrameOrigin = *origin;
    else
        XPoint_init(&g_cursorSwFrameOrigin, 0, 0);
}

void XCursor_swPosFrame(int* outRot, int* outLogW, int* outLogH,
                        XPoint* outOrigin)
{
    if (outRot) *outRot = g_cursorSwFrameRot;
    if (outLogW) *outLogW = g_cursorSwFrameLogW;
    if (outLogH) *outLogH = g_cursorSwFrameLogH;
    if (outOrigin) *outOrigin = g_cursorSwFrameOrigin;
}

/* ==================== 资源复制辅助 ==================== */

/** @brief 深拷贝位图；源为 NULL 返回 NULL。 */
static struct XBitmap* XCursor_copyBitmap(const struct XBitmap* source)
{
    struct XBitmap* copy;
    if (!source) return NULL;
    copy = XBitmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XClassCopy(copy, source);
    return copy;
}

/** @brief 深拷贝像素图；源为 NULL 返回 NULL。 */
static struct XPixmap* XCursor_copyPixmap(const struct XPixmap* source)
{
    struct XPixmap* copy;
    if (!source) return NULL;
    copy = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!copy) return NULL;
    XClassCopy(copy, source);
    return copy;
}

/** @brief 释放光标自有资源并复位默认值，保留对象外壳（对标 QCursor 复位为 Arrow）。 */
static void XCursor_resetResources(XCursor* self)
{
    if (!self) return;
    if (self->m_bitmap) XClassDelete(self->m_bitmap);
    if (self->m_mask) XClassDelete(self->m_mask);
    if (self->m_pixmap) XClassDelete(self->m_pixmap);
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
    XMemset(self, 0, sizeof(XCursor));
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
    int x, y;
    /* 对标 QCursor::pos：平台后端可用时优先取真实系统光标位置并同步
       进程级缓存；查询失败/无后端时回落缓存值（静默）。 */
    if (g_cursorBackend && g_cursorBackend->m_queryPos &&
        g_cursorBackend->m_queryPos(&x, &y)) {
        g_cursorPos.x = x;
        g_cursorPos.y = y;
    }
    return g_cursorPos;
}

void XCursor_setPos(int x, int y)
{
    g_cursorPos.x = x;
    g_cursorPos.y = y;
    /* 软件精灵「已收到位置」资格：首次调用即置位（对外的 pos/setPos
     * 语义不变，只多记一份内部账）。 */
    g_cursorSwHasPos = true;
    /* 平台后端转发原生定位（X11 XWarpPointer）；失败静默，缓存已同步。 */
    if (g_cursorBackend && g_cursorBackend->m_warpPos)
        (void)g_cursorBackend->m_warpPos(x, y);
}

void XCursor_setPos_point(const XPoint* pos)
{
    int x, y;
    if (pos) {
        x = pos->x;
        y = pos->y;
    } else {
        x = 0;
        y = 0;
    }
    XCursor_setPos(x, y); /* 与整型版本同一平台转发路径。 */
}

/* ==================== 形状判断 ==================== */

bool XCursor_isShapeCursor(const XCursor* self)
{
    if (!self) return true;
    return self->m_shape < XCursor_Bitmap;
}

#endif /* XCURSOR_ON */

void XCursor_swap(XCursor* a, XCursor* b)
{
#if XCURSOR_ON
    XCursor tmp;
    if (!a || !b || a == b) return;
    /* 经移动语义交换资源：临时对象先初始化，资源只转移指针不复制，
     * 不触碰 XObject 头（结构体赋值会连 m_memory/m_is_heap 一起互换，
     * 冒充所有权转移）。 */
    XCursor_init(&tmp);
    XClassMove(&tmp, a);  /* tmp ← a 资源；a 复位为空光标 */
    XClassMove(a, b);     /* a ← b 资源 */
    XClassMove(b, &tmp);  /* b ← 原 a 资源；tmp 已空 */
    XClassDeinit(&tmp);   /* 创建者收尾：资源已转出，此处为空操作 */
#else
    (void)a;
    (void)b;
#endif /* XCURSOR_ON */
}

bool XCursor_equals(const XCursor* a, const XCursor* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_shape != b->m_shape) return false;
    if (a->m_hasHotSpot != b->m_hasHotSpot) return false;
    if (a->m_hasHotSpot &&
        (a->m_hotSpot.x != b->m_hotSpot.x ||
         a->m_hotSpot.y != b->m_hotSpot.y))
        return false;
    if (a->m_bitmap != b->m_bitmap) return false;
    if (a->m_mask != b->m_mask) return false;
    if (a->m_pixmap != b->m_pixmap) return false;
    return true;
}

#endif /* XGUI_ON */
