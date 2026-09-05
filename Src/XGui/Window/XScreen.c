/******************************************************************************
 * @file       XScreen.c
 * @brief      XScreen 屏幕信息类实现（对标 Qt 6.8 QScreen）。
 * @details    本文件实现 XScreen 的全部属性、注册表、方向换算、平台抓屏
 *             与 9 个通知信号。行为语义逐一与 Qt 6.8.3 qscreen.cpp /
 *             qplatformscreen.cpp 对齐：
 *              - angleBetween / transformBetween / mapBetween 与
 *                QPlatformScreen 同名算法完全一致（先解析 Primary）；
 *              - primaryOrientation 默认由几何宽高推导（宽>=高为横屏），
 *                显式设置后锁定；
 *              - 可用几何未显式设置时跟随几何变化（与 QPlatformScreen
 *                availableGeometry 缺省实现一致）；
 *              - 虚拟几何为兄弟屏幕 geometry 的并集；
 *              - 信号发射时机（geometry -> availableGeometry ->
 *                virtualGeometry -> physicalDpi -> primaryOrientation）
 *                与 QScreenPrivate::UpdateEmitter 一致。
 * @note       模块总开关 XSCREEN_ON 定义于 XGuiConfig.h；置 0 时本文件
 *             实现体整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XScreen.h"
#include "XVarList.h"
#if XPLATFORMNATIVEWINDOW_ON
#include "XPlatformNativeWindow.h"
#endif /* XPLATFORMNATIVEWINDOW_ON */
#include <string.h>

#if XSCREEN_ON

/* ==================== 私有实现 ==================== */

/** @brief XScreen 私有属性快照；仅本文件访问。 */
struct XScreenPrivate
{
    XString* m_name;                      /**< 屏幕名称；对象拥有。 */
    XString* m_manufacturer;              /**< 厂商；对象拥有。 */
    XString* m_model;                     /**< 型号；对象拥有。 */
    XString* m_serialNumber;              /**< 序列号；对象拥有。 */
    int m_depth;                          /**< 每像素位数；默认 32。 */
    XRect m_geometry;                     /**< 屏幕几何（设备无关像素）。 */
    XRect m_availableGeometry;            /**< 可用几何；未显式设置时跟随几何。 */
    bool m_hasAvailableGeometry;          /**< 可用几何是否被显式设置。 */
    XSizeF m_physicalSize;                /**< 物理尺寸（毫米）。 */
    float m_logicalDotsPerInchX;          /**< 水平逻辑 DPI；默认 96。 */
    float m_logicalDotsPerInchY;          /**< 垂直逻辑 DPI；默认 96。 */
    float m_devicePixelRatio;             /**< 设备像素比；默认 1.0。 */
    XScreenOrientation m_primaryOrientation; /**< 主方向；默认按几何推导。 */
    XScreenOrientation m_orientation;     /**< 当前显示方向；默认 Primary。 */
    XScreenOrientation m_nativeOrientation;  /**< 原生方向；默认 Primary。 */
    float m_refreshRate;                  /**< 刷新率（Hz）；默认 60。 */
    XScreenPlatform* m_platform;          /**< 平台屏幕句柄；借用不拥有。 */
    XVector* m_virtualSiblings;           /**< 显式兄弟列表（XScreen* 借用）；NULL 表示默认语义。 */
    bool m_primaryForced;                 /**< 主方向是否被显式锁定。 */
    float m_lastPhysicalDpi;              /**< 最近一次发出的平均物理 DPI 缓存。 */
    XRect m_lastVirtualGeometry;          /**< 最近一次发出的虚拟几何缓存。 */
};

/** @brief 进程内屏幕注册表（元素为 XScreen* 借用指针，不持有所有权）。 */
static XVector* g_screens = NULL;

/** @brief 当前主屏幕（借用指针）。 */
static XScreen* g_primary = NULL;

/** @brief 拷贝字符串的辅助函数。 @param value 源字符串；可为 NULL。 @return 深拷贝或 NULL。 */
static XString* XScreen_copyString(const XString* value)
{
    return value ? XString_create_copy(value) : NULL;
}

/** @brief 释放私有块内的所有引用（字符串与兄弟列表），保留块本身。 */
static void XScreen_clearPrivateData(XScreenPrivate* data)
{
    if (!data) return;
    if (data->m_name) XString_delete_base((XClass*)data->m_name);
    if (data->m_manufacturer) XString_delete_base((XClass*)data->m_manufacturer);
    if (data->m_model) XString_delete_base((XClass*)data->m_model);
    if (data->m_serialNumber) XString_delete_base((XClass*)data->m_serialNumber);
    if (data->m_virtualSiblings) XVector_delete_base((XClass*)data->m_virtualSiblings);
    data->m_name = NULL;
    data->m_manufacturer = NULL;
    data->m_model = NULL;
    data->m_serialNumber = NULL;
    data->m_virtualSiblings = NULL;
}

/** @brief 替换私有块内的字符串字段（深拷贝语义）。 @param dst 目标字段。 @param value 源字符串；可为 NULL 清空。 */
static void XScreen_setString(XString** dst, const XString* value)
{
    XString* copy = value ? XString_create_copy(value) : NULL;
    if (*dst) XString_delete_base((XClass*)*dst);
    *dst = copy;
}

/** @brief 浮点近似相等判断（用于 DPI/尺寸/刷新率变化检测）。 */
static bool XScreen_floatNear(float a, float b)
{
    float delta = a - b;
    float bound = 1e-4f;
    return delta < 0.0f ? -delta <= bound : delta <= bound;
}

/** @brief 矩形逐字段相等判断。 */
static bool XScreen_rectEquals(const XRect* a, const XRect* b)
{
    if (!a || !b) return a == b;
    return a->x == b->x && a->y == b->y &&
           a->width == b->width && a->height == b->height;
}

/** @brief 向量内是否已包含指定屏幕（借用指针相等判断）。 */
static bool XScreen_vectorContains(const XVector* vector, XScreen* screen)
{
    int64_t i;
    int64_t n;
    if (!vector || !screen) return false;
    n = (int64_t)XVector_size_base((const XContainer*)vector);
    for (i = 0; i < n; ++i) {
        if (XVector_At_Base(vector, i, XScreen*) == screen) return true;
    }
    return false;
}

/** @brief 向量内查找指定屏幕下标。 @return 下标；未找到或入参非法返回 -1。 */
static int64_t XScreen_vectorIndexOf(const XVector* vector, XScreen* screen)
{
    int64_t i;
    int64_t n;
    if (!vector || !screen) return -1;
    n = (int64_t)XVector_size_base((const XContainer*)vector);
    for (i = 0; i < n; ++i) {
        if (XVector_At_Base(vector, i, XScreen*) == screen) return i;
    }
    return -1;
}

/** @brief 把方向解析为实际方向；Primary 解析为主方向（无数据时按横屏处理）。 */
static XScreenOrientation XScreen_resolveOrientation(const XScreen* self,
                                                     XScreenOrientation orientation)
{
    if (orientation == XScreenOrientation_Primary)
        return XScreen_primaryOrientation(self);
    return orientation;
}

/** @brief 方向到 log2 序号表（1->0、2->1、4->2、8->3）。非法返回 -1。 */
static int XScreen_orientationLog2(XScreenOrientation orientation)
{
    switch (orientation) {
    case XScreenOrientation_Portrait:          return 0;
    case XScreenOrientation_Landscape:         return 1;
    case XScreenOrientation_InvertedPortrait:  return 2;
    case XScreenOrientation_InvertedLandscape: return 3;
    default:                                   return -1;
    }
}

/** @brief 按几何宽高推导主方向：宽>=高为横屏，否则竖屏（与 Qt 一致）。 */
static XScreenOrientation XScreen_derivePrimary(const XScreenPrivate* data)
{
    if (!data) return XScreenOrientation_Landscape;
    return data->m_geometry.width >= data->m_geometry.height
        ? XScreenOrientation_Landscape : XScreenOrientation_Portrait;
}

/** @brief 水平物理 DPI：几何宽 / 物理宽 x 25.4；物理宽非正返回 0。 */
static float XScreen_calcDpiX(const XScreenPrivate* data)
{
    if (!data || data->m_physicalSize.width <= 0.0f) return 0.0f;
    return (float)data->m_geometry.width / data->m_physicalSize.width * 25.4f;
}

/** @brief 垂直物理 DPI：几何高 / 物理高 x 25.4；物理高非正返回 0。 */
static float XScreen_calcDpiY(const XScreenPrivate* data)
{
    if (!data || data->m_physicalSize.height <= 0.0f) return 0.0f;
    return (float)data->m_geometry.height / data->m_physicalSize.height * 25.4f;
}

/** @brief 平均物理 DPI 变化时发射 physicalDotsPerInchChanged（Qt NOTIFY 语义）。 */
static void XScreen_notifyPhysicalDpiChanged(XScreen* self)
{
    XScreenPrivate* data;
    float dpi;
    if (!self || !(data = self->m_data)) return;
    dpi = XScreen_physicalDotsPerInch(self);
    if (!XScreen_floatNear(dpi, data->m_lastPhysicalDpi)) {
        data->m_lastPhysicalDpi = dpi;
        XScreen_physicalDotsPerInchChanged_signal(self, dpi);
    }
}

/** @brief 发射信号并管理参数列表生命周期。 */
static void XScreen_emit(XScreen* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/** @brief 更新受几何/可用几何变化影响的屏幕的虚拟几何并发射变化信号。 */
static void XScreen_refreshVirtualGeometries(XScreen* changed)
{
    XVector* affected;
    XScreenPrivate* data;
    int64_t i;
    int64_t n;
    affected = XVector_Create(XScreen*);
    if (!affected) return;
    /* 受影响的候选集：注册表全部屏幕 + 变化屏幕自身 + 显式兄弟列表。 */
    if (g_screens) {
        n = (int64_t)XVector_size_base((const XContainer*)g_screens);
        for (i = 0; i < n; ++i) {
            XScreen* screen = XVector_At_Base(g_screens, i, XScreen*);
            if (screen && !XScreen_vectorContains(affected, screen))
                XVector_Push_Back_Base(affected, XScreen*, screen);
        }
    }
    if (changed) {
        if (!XScreen_vectorContains(affected, changed))
            XVector_Push_Back_Base(affected, XScreen*, changed);
        data = changed->m_data;
        if (data && data->m_virtualSiblings) {
            n = (int64_t)XVector_size_base((const XContainer*)data->m_virtualSiblings);
            for (i = 0; i < n; ++i) {
                XScreen* screen = XVector_At_Base(data->m_virtualSiblings, i, XScreen*);
                if (screen && !XScreen_vectorContains(affected, screen))
                    XVector_Push_Back_Base(affected, XScreen*, screen);
            }
        }
    }
    n = (int64_t)XVector_size_base((const XContainer*)affected);
    for (i = 0; i < n; ++i) {
        XScreen* screen;
        XRect virtualGeometry;
        screen = XVector_At_Base(affected, i, XScreen*);
        if (!screen || !screen->m_data) continue;
        virtualGeometry = XScreen_virtualGeometry(screen);
        if (!XScreen_rectEquals(&virtualGeometry,
                                &screen->m_data->m_lastVirtualGeometry)) {
            screen->m_data->m_lastVirtualGeometry = virtualGeometry;
            XScreen_virtualGeometryChanged_signal(screen, &virtualGeometry);
        }
    }
    XVector_delete_base((XClass*)affected);
}

/* ==================== 虚函数表与生命周期 ==================== */

static void VXScreen_deinit(XScreen* self);
static void VXScreen_copy(XScreen* self, const XScreen* other);
static void VXScreen_move(XScreen* self, XScreen* other);

XVtable* XScreen_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScreen)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXScreen_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXScreen_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXScreen_move);
    return XVTABLE_DEFAULT;
}

void XScreen_init(XScreen* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XScreen));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XScreen);
    self->m_data = (XScreenPrivate*)XMalloc_System(sizeof(XScreenPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XScreenPrivate));
    self->m_data->m_depth = 32;
    self->m_data->m_logicalDotsPerInchX = 96.0f;
    self->m_data->m_logicalDotsPerInchY = 96.0f;
    self->m_data->m_devicePixelRatio = 1.0f;
    self->m_data->m_refreshRate = 60.0f;
    self->m_data->m_primaryOrientation =
        XScreen_derivePrimary(self->m_data);
    self->m_data->m_orientation = XScreenOrientation_Primary;
    self->m_data->m_nativeOrientation = XScreenOrientation_Primary;
    self->m_data->m_lastPhysicalDpi = 0.0f;
}

XScreen* XScreen_create_ex(XMemoryType memory)
{
    XScreen* self = (XScreen*)XMemory_malloc(sizeof(XScreen), memory);
    if (!self) return NULL;
    XScreen_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XScreen* XScreen_create_copy(const XScreen* other)
{
    XScreen* self;
    if (!other) return NULL;
    self = XScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XCopy(self, other);
    return self;
}

XScreen* XScreen_create_move(XScreen* other)
{
    XScreen* self;
    if (!other) return NULL;
    self = XScreen_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XMove(self, other);
    return self;
}

static void VXScreen_deinit(XScreen* self)
{
    if (!self) return;
    /* 销毁时自动退出进程内注册表，避免悬挂借用指针。 */
    XScreen_unregister(self);
    if (self->m_data) {
        XScreen_clearPrivateData(self->m_data);
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXScreen_copy(XScreen* self, const XScreen* other)
{
    XScreenPrivate* source;
    if (!self || !other || self == other || !(source = other->m_data)) return;
    if (XClassIsVtableNull(self)) XScreen_init(self);
    if (!self->m_data) return;
    XScreen_clearPrivateData(self->m_data);
    self->m_data->m_name = XScreen_copyString(source->m_name);
    self->m_data->m_manufacturer = XScreen_copyString(source->m_manufacturer);
    self->m_data->m_model = XScreen_copyString(source->m_model);
    self->m_data->m_serialNumber = XScreen_copyString(source->m_serialNumber);
    self->m_data->m_depth = source->m_depth;
    self->m_data->m_geometry = source->m_geometry;
    self->m_data->m_availableGeometry = source->m_availableGeometry;
    self->m_data->m_hasAvailableGeometry = source->m_hasAvailableGeometry;
    self->m_data->m_physicalSize = source->m_physicalSize;
    self->m_data->m_logicalDotsPerInchX = source->m_logicalDotsPerInchX;
    self->m_data->m_logicalDotsPerInchY = source->m_logicalDotsPerInchY;
    self->m_data->m_devicePixelRatio = source->m_devicePixelRatio;
    self->m_data->m_primaryOrientation = source->m_primaryOrientation;
    self->m_data->m_orientation = source->m_orientation;
    self->m_data->m_nativeOrientation = source->m_nativeOrientation;
    self->m_data->m_refreshRate = source->m_refreshRate;
    self->m_data->m_platform = source->m_platform; /* 平台句柄借用。 */
    if (source->m_virtualSiblings)
        self->m_data->m_virtualSiblings =
            XVector_create_copy(source->m_virtualSiblings);
    self->m_data->m_primaryForced = source->m_primaryForced;
    self->m_data->m_lastPhysicalDpi = source->m_lastPhysicalDpi;
    self->m_data->m_lastVirtualGeometry = source->m_lastVirtualGeometry;
}

static void VXScreen_move(XScreen* self, XScreen* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XScreen_init(self);
    if (!self->m_data) return;
    XScreen_clearPrivateData(self->m_data);
    XFree_System(self->m_data);
    self->m_data = other->m_data;
    other->m_data = NULL;
}

/* ==================== 注册表（对标 QGuiApplication 屏幕列表语义） ==================== */

void XScreen_register(XScreen* screen)
{
    XVector* list;
    if (!screen || !screen->m_data) return;
    list = g_screens;
    if (!list) {
        g_screens = XVector_Create(XScreen*);
        if (!g_screens) return;
        list = g_screens;
    }
    if (XScreen_vectorIndexOf(list, screen) >= 0) return; /* 重复注册 no-op。 */
    XVector_Push_Back_Base(list, XScreen*, screen);
    /* 新成员加入后各屏幕默认兄弟集合可能扩大，复查虚拟几何。 */
    XScreen_refreshVirtualGeometries(screen);
}

void XScreen_unregister(XScreen* screen)
{
    int64_t index;
    if (!screen || !g_screens) return;
    if (g_primary == screen) g_primary = NULL;
    index = XScreen_vectorIndexOf(g_screens, screen);
    if (index < 0) return; /* 未注册是 no-op。 */
    XVector_remove_base(g_screens, index, 1);
    if (XVector_size_base((const XContainer*)g_screens) <= 0) {
        XVector_delete_base((XClass*)g_screens);
        g_screens = NULL;
    } else {
        /* 成员退出后其余屏幕默认兄弟集合缩小，复查虚拟几何。 */
        XScreen_refreshVirtualGeometries(screen);
    }
}

XScreen* XScreen_primaryScreen(void)
{
    return g_primary;
}

void XScreen_setPrimary(XScreen* screen)
{
    g_primary = screen;
}

XVector* XScreen_screens(void)
{
    if (g_screens) return XVector_create_copy(g_screens);
    return XVector_Create(XScreen*);
}

/* ==================== 平台句柄 ==================== */

XScreenPlatform* XScreen_handle(const XScreen* self)
{
    return self && self->m_data ? self->m_data->m_platform : NULL;
}

void XScreen_setHandle(XScreen* self, XScreenPlatform* handle)
{
    if (!self || !self->m_data) return;
    self->m_data->m_platform = handle;
}

/* ==================== 标识属性 ==================== */

XString* XScreen_name(const XScreen* self)
{
    const XString* value = XScreen_name_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XScreen_name_const(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_name : NULL; }
const char* XScreen_name_2(const XScreen* self)
{ return XString_toUtf8(XScreen_name_const(self)); }
void XScreen_setName(XScreen* self, const XString* name)
{ if (self && self->m_data) XScreen_setString(&self->m_data->m_name, name); }
void XScreen_setName_2(XScreen* self, const char* name)
{
    XString* value = name ? XString_create_utf8(name) : NULL;
    XScreen_setName(self, value);
    if (value) XString_delete_base((XClass*)value);
}

XString* XScreen_manufacturer(const XScreen* self)
{
    const XString* value = XScreen_manufacturer_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XScreen_manufacturer_const(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_manufacturer : NULL; }
const char* XScreen_manufacturer_2(const XScreen* self)
{ return XString_toUtf8(XScreen_manufacturer_const(self)); }
void XScreen_setManufacturer(XScreen* self, const XString* manufacturer)
{ if (self && self->m_data) XScreen_setString(&self->m_data->m_manufacturer, manufacturer); }
void XScreen_setManufacturer_2(XScreen* self, const char* manufacturer)
{
    XString* value = manufacturer ? XString_create_utf8(manufacturer) : NULL;
    XScreen_setManufacturer(self, value);
    if (value) XString_delete_base((XClass*)value);
}

XString* XScreen_model(const XScreen* self)
{
    const XString* value = XScreen_model_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XScreen_model_const(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_model : NULL; }
const char* XScreen_model_2(const XScreen* self)
{ return XString_toUtf8(XScreen_model_const(self)); }
void XScreen_setModel(XScreen* self, const XString* model)
{ if (self && self->m_data) XScreen_setString(&self->m_data->m_model, model); }
void XScreen_setModel_2(XScreen* self, const char* model)
{
    XString* value = model ? XString_create_utf8(model) : NULL;
    XScreen_setModel(self, value);
    if (value) XString_delete_base((XClass*)value);
}

XString* XScreen_serialNumber(const XScreen* self)
{
    const XString* value = XScreen_serialNumber_const(self);
    return value ? XString_create_copy(value) : XString_create();
}
const XString* XScreen_serialNumber_const(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_serialNumber : NULL; }
const char* XScreen_serialNumber_2(const XScreen* self)
{ return XString_toUtf8(XScreen_serialNumber_const(self)); }
void XScreen_setSerialNumber(XScreen* self, const XString* serialNumber)
{ if (self && self->m_data) XScreen_setString(&self->m_data->m_serialNumber, serialNumber); }
void XScreen_setSerialNumber_2(XScreen* self, const char* serialNumber)
{
    XString* value = serialNumber ? XString_create_utf8(serialNumber) : NULL;
    XScreen_setSerialNumber(self, value);
    if (value) XString_delete_base((XClass*)value);
}

/* ==================== 深度 ==================== */

int XScreen_depth(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_depth : 0; }

void XScreen_setDepth(XScreen* self, int depth)
{ if (self && self->m_data) self->m_data->m_depth = depth; }

/* ==================== 几何与尺寸 ==================== */

XRect XScreen_geometry(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_geometry : (XRect){0, 0, 0, 0}; }

void XScreen_setGeometry(XScreen* self, const XRect* geometry)
{
    XScreenPrivate* data;
    bool availableChanged;
    if (!self || !(data = self->m_data) || !geometry) return;
    if (XScreen_rectEquals(geometry, &data->m_geometry)) return;
    data->m_geometry = *geometry;
    /* 可用几何未显式设置时跟随几何变化。 */
    availableChanged = !XScreen_rectEquals(&data->m_availableGeometry, geometry);
    if (availableChanged) data->m_availableGeometry = *geometry;
    /* 发射顺序与 Qt QScreenPrivate::UpdateEmitter 一致。 */
    XScreen_geometryChanged_signal(self, &data->m_geometry);
    if (availableChanged && !data->m_hasAvailableGeometry)
        XScreen_availableGeometryChanged_signal(self, &data->m_availableGeometry);
    XScreen_refreshVirtualGeometries(self);
    XScreen_notifyPhysicalDpiChanged(self);
    if (!data->m_primaryForced) {
        XScreenOrientation derived = XScreen_derivePrimary(data);
        if (derived != data->m_primaryOrientation) {
            data->m_primaryOrientation = derived;
            XScreen_primaryOrientationChanged_signal(self, derived);
        }
    }
}

XSize XScreen_size(const XScreen* self)
{
    XRect geometry = XScreen_geometry(self);
    return (XSize){geometry.width, geometry.height};
}

XSizeF XScreen_physicalSize(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_physicalSize : (XSizeF){0.0f, 0.0f}; }

void XScreen_setPhysicalSize(XScreen* self, const XSizeF* size)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data) || !size) return;
    if (XScreen_floatNear(data->m_physicalSize.width, size->width) &&
        XScreen_floatNear(data->m_physicalSize.height, size->height)) return;
    data->m_physicalSize = *size;
    XScreen_physicalSizeChanged_signal(self, &data->m_physicalSize);
    XScreen_notifyPhysicalDpiChanged(self);
}

float XScreen_physicalDotsPerInchX(const XScreen* self)
{ return XScreen_calcDpiX(self && self->m_data ? self->m_data : NULL); }

float XScreen_physicalDotsPerInchY(const XScreen* self)
{ return XScreen_calcDpiY(self && self->m_data ? self->m_data : NULL); }

float XScreen_physicalDotsPerInch(const XScreen* self)
{
    return (XScreen_physicalDotsPerInchX(self) +
            XScreen_physicalDotsPerInchY(self)) * 0.5f;
}

float XScreen_logicalDotsPerInchX(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_logicalDotsPerInchX : 0.0f; }

float XScreen_logicalDotsPerInchY(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_logicalDotsPerInchY : 0.0f; }

float XScreen_logicalDotsPerInch(const XScreen* self)
{
    return (XScreen_logicalDotsPerInchX(self) +
            XScreen_logicalDotsPerInchY(self)) * 0.5f;
}

void XScreen_setLogicalDotsPerInch(XScreen* self, float x, float y)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (XScreen_floatNear(data->m_logicalDotsPerInchX, x) &&
        XScreen_floatNear(data->m_logicalDotsPerInchY, y)) return;
    data->m_logicalDotsPerInchX = x;
    data->m_logicalDotsPerInchY = y;
    XScreen_logicalDotsPerInchChanged_signal(self,
        XScreen_logicalDotsPerInch(self));
}

float XScreen_devicePixelRatio(const XScreen* self)
{ return self && self->m_data ? self->m_data->m_devicePixelRatio : 1.0f; }

void XScreen_setDevicePixelRatio(XScreen* self, float ratio)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (XScreen_floatNear(data->m_devicePixelRatio, ratio)) return;
    data->m_devicePixelRatio = ratio;
    /* Qt 中 devicePixelRatio 属性的 NOTIFY 是 physicalDotsPerInchChanged。 */
    XScreen_physicalDotsPerInchChanged_signal(self,
        XScreen_physicalDotsPerInch(self));
}

XRect XScreen_availableGeometry(const XScreen* self)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data)) return (XRect){0, 0, 0, 0};
    /* 未显式设置时返回几何本身，与 QPlatformScreen 缺省实现一致。 */
    if (!data->m_hasAvailableGeometry) return data->m_geometry;
    return data->m_availableGeometry;
}

void XScreen_setAvailableGeometry(XScreen* self, const XRect* geometry)
{
    XScreenPrivate* data;
    XRect old;
    if (!self || !(data = self->m_data) || !geometry) return;
    old = data->m_availableGeometry;
    if (data->m_hasAvailableGeometry && XScreen_rectEquals(&old, geometry))
        return;
    data->m_hasAvailableGeometry = true;
    if (!XScreen_rectEquals(geometry, &old)) {
        data->m_availableGeometry = *geometry;
        XScreen_availableGeometryChanged_signal(self, &data->m_availableGeometry);
        XScreen_refreshVirtualGeometries(self);
    }
}

XSize XScreen_availableSize(const XScreen* self)
{
    XRect available = XScreen_availableGeometry(self);
    return (XSize){available.width, available.height};
}

/* ==================== 虚拟桌面（兄弟屏幕） ==================== */

XVector* XScreen_virtualSiblings(const XScreen* self)
{
    XVector* result;
    XScreenPrivate* data;
    int64_t i;
    int64_t n;
    if (!self || !(data = self->m_data)) return XVector_Create(XScreen*);
    /* 显式兄弟列表优先；否则使用默认语义：自身 + 注册表其它屏幕。 */
    if (data->m_virtualSiblings) return XVector_create_copy(data->m_virtualSiblings);
    result = XVector_Create(XScreen*);
    if (!result) return NULL;
    XVector_Push_Back_Base(result, XScreen*, (XScreen*)self);
    if (g_screens) {
        n = (int64_t)XVector_size_base((const XContainer*)g_screens);
        for (i = 0; i < n; ++i) {
            XScreen* screen = XVector_At_Base(g_screens, i, XScreen*);
            if (screen != self && !XScreen_vectorContains(result, screen))
                XVector_Push_Back_Base(result, XScreen*, screen);
        }
    }
    return result;
}

void XScreen_setVirtualSiblings(XScreen* self, XScreen* const* siblings,
                                int count)
{
    XScreenPrivate* data;
    XVector* list;
    int i;
    if (!self || !(data = self->m_data)) return;
    if (data->m_virtualSiblings) {
        XVector_delete_base((XClass*)data->m_virtualSiblings);
        data->m_virtualSiblings = NULL;
    }
    if (siblings && count > 0) {
        list = XVector_Create(XScreen*);
        if (list) {
            for (i = 0; i < count; ++i)
                XVector_Push_Back_Base(list, XScreen*, siblings[i]);
            data->m_virtualSiblings = list;
        }
    }
    XScreen_refreshVirtualGeometries(self);
}

XScreen* XScreen_virtualSiblingAt(const XScreen* self, XPoint point)
{
    XVector* siblings;
    XScreen* hit = NULL;
    int64_t i;
    int64_t n;
    if (!self || !self->m_data) return NULL;
    siblings = XScreen_virtualSiblings(self);
    if (!siblings) return NULL;
    n = (int64_t)XVector_size_base((const XContainer*)siblings);
    for (i = 0; i < n; ++i) {
        XScreen* screen = XVector_At_Base(siblings, i, XScreen*);
        XRect geometry;
        if (!screen) continue;
        geometry = XScreen_geometry(screen);
        if (XRect_contains(&geometry, point.x, point.y)) {
            hit = screen;
            break;
        }
    }
    XVector_delete_base((XClass*)siblings);
    return hit;
}

XSize XScreen_virtualSize(const XScreen* self)
{
    XRect virtualGeometry = XScreen_virtualGeometry(self);
    return (XSize){virtualGeometry.width, virtualGeometry.height};
}

XRect XScreen_virtualGeometry(const XScreen* self)
{
    XVector* siblings;
    XRect result = {0, 0, 0, 0};
    int64_t i;
    int64_t n;
    if (!self || !self->m_data) return result;
    siblings = XScreen_virtualSiblings(self);
    if (!siblings) return result;
    n = (int64_t)XVector_size_base((const XContainer*)siblings);
    for (i = 0; i < n; ++i) {
        XScreen* screen = XVector_At_Base(siblings, i, XScreen*);
        XRect geometry;
        if (!screen) continue;
        geometry = XScreen_geometry(screen);
        result = XRect_united(&result, &geometry);
    }
    XVector_delete_base((XClass*)siblings);
    return result;
}

XSize XScreen_availableVirtualSize(const XScreen* self)
{
    XRect virtualGeometry = XScreen_availableVirtualGeometry(self);
    return (XSize){virtualGeometry.width, virtualGeometry.height};
}

XRect XScreen_availableVirtualGeometry(const XScreen* self)
{
    XVector* siblings;
    XRect result = {0, 0, 0, 0};
    int64_t i;
    int64_t n;
    if (!self || !self->m_data) return result;
    siblings = XScreen_virtualSiblings(self);
    if (!siblings) return result;
    n = (int64_t)XVector_size_base((const XContainer*)siblings);
    for (i = 0; i < n; ++i) {
        XScreen* screen = XVector_At_Base(siblings, i, XScreen*);
        XRect available;
        if (!screen) continue;
        available = XScreen_availableGeometry(screen);
        result = XRect_united(&result, &available);
    }
    XVector_delete_base((XClass*)siblings);
    return result;
}

/* ==================== 方向与刷新率 ==================== */

XScreenOrientation XScreen_primaryOrientation(const XScreen* self)
{
    /* 无数据时按 0x0 几何 => 横屏处理，与 init 语义一致。 */
    return self && self->m_data
        ? self->m_data->m_primaryOrientation
        : XScreenOrientation_Landscape;
}

void XScreen_setPrimaryOrientation(XScreen* self, XScreenOrientation orientation)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (orientation == XScreenOrientation_Primary) return; /* Primary 视为无效并忽略。 */
    data->m_primaryForced = true; /* 显式设置后不再随几何自动推导。 */
    if (orientation == data->m_primaryOrientation) return;
    data->m_primaryOrientation = orientation;
    XScreen_primaryOrientationChanged_signal(self, orientation);
}

XScreenOrientation XScreen_orientation(const XScreen* self)
{
    return self && self->m_data
        ? self->m_data->m_orientation
        : XScreenOrientation_Primary;
}

void XScreen_setOrientation(XScreen* self, XScreenOrientation orientation)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (orientation == data->m_orientation) return;
    data->m_orientation = orientation;
    XScreen_orientationChanged_signal(self, orientation);
}

XScreenOrientation XScreen_nativeOrientation(const XScreen* self)
{
    return self && self->m_data
        ? self->m_data->m_nativeOrientation
        : XScreenOrientation_Primary;
}

void XScreen_setNativeOrientation(XScreen* self, XScreenOrientation orientation)
{
    if (!self || !self->m_data) return;
    self->m_data->m_nativeOrientation = orientation;
}

float XScreen_refreshRate(const XScreen* self)
{
    return self && self->m_data ? self->m_data->m_refreshRate : 60.0f;
}

void XScreen_setRefreshRate(XScreen* self, float refreshRate)
{
    XScreenPrivate* data;
    if (!self || !(data = self->m_data)) return;
    if (XScreen_floatNear(data->m_refreshRate, refreshRate)) return;
    data->m_refreshRate = refreshRate;
    XScreen_refreshRateChanged_signal(self, refreshRate);
}

/* ==================== 方向换算（算法与 Qt 6.8.3 一致） ==================== */

int XScreen_angleBetween(const XScreen* self, XScreenOrientation a,
                         XScreenOrientation b)
{
    static const int angles[4] = { 0, 90, 180, 270 };
    int ia;
    int ib;
    int delta;
    a = XScreen_resolveOrientation(self, a);
    b = XScreen_resolveOrientation(self, b);
    if (a == b) return 0;
    ia = XScreen_orientationLog2(a);
    ib = XScreen_orientationLog2(b);
    if (ia < 0 || ib < 0) return 0; /* 非法方向。 */
    delta = ia - ib;
    if (delta < 0) delta += 4;
    return angles[delta];
}

XImageTransform XScreen_transformBetween(const XScreen* self,
                                         XScreenOrientation a,
                                         XScreenOrientation b,
                                         const XRect* target)
{
    XImageTransform result;
    XRect t;
    int angle;
    t = target ? *target : (XRect){0, 0, 0, 0};
    /* 初始化为单位矩阵。 */
    result.m11 = 1.0f; result.m12 = 0.0f;
    result.m21 = 0.0f; result.m22 = 1.0f;
    result.dx = 0.0f;  result.dy = 0.0f;
    result.m13 = 0.0f; result.m23 = 0.0f; result.m33 = 1.0f;
    a = XScreen_resolveOrientation(self, a);
    b = XScreen_resolveOrientation(self, b);
    if (a == b) return result;
    angle = XScreen_angleBetween(self, a, b);
    switch (angle) {
    case 90: /* x'=w-y、y'=x */
        result.m11 = 0.0f;  result.m12 = 1.0f;
        result.m21 = -1.0f; result.m22 = 0.0f;
        result.dx = (float)t.width;
        result.dy = 0.0f;
        break;
    case 180: /* x'=w-x、y'=h-y */
        result.m11 = -1.0f; result.m12 = 0.0f;
        result.m21 = 0.0f;  result.m22 = -1.0f;
        result.dx = (float)t.width;
        result.dy = (float)t.height;
        break;
    case 270: /* x'=y、y'=h-x */
        result.m11 = 0.0f;  result.m12 = -1.0f;
        result.m21 = 1.0f;  result.m22 = 0.0f;
        result.dx = 0.0f;
        result.dy = (float)t.height;
        break;
    default: /* 0 度或非法输入保持单位矩阵。 */
        break;
    }
    return result;
}

XRect XScreen_mapBetween(const XScreen* self, XScreenOrientation a,
                         XScreenOrientation b, const XRect* rect)
{
    XRect r = rect ? *rect : (XRect){0, 0, 0, 0};
    a = XScreen_resolveOrientation(self, a);
    b = XScreen_resolveOrientation(self, b);
    if (a == b) return r;
    if (XScreen_isPortrait(self, a) != XScreen_isPortrait(self, b))
        return (XRect){r.y, r.x, r.height, r.width};
    return r;
}

bool XScreen_isPortrait(const XScreen* self, XScreenOrientation orientation)
{
    if (orientation == XScreenOrientation_Portrait ||
        orientation == XScreenOrientation_InvertedPortrait)
        return true;
    if (orientation == XScreenOrientation_Primary)
        return XScreen_primaryOrientation(self) == XScreenOrientation_Portrait;
    return false;
}

bool XScreen_isLandscape(const XScreen* self, XScreenOrientation orientation)
{
    if (orientation == XScreenOrientation_Landscape ||
        orientation == XScreenOrientation_InvertedLandscape)
        return true;
    if (orientation == XScreenOrientation_Primary)
        return XScreen_primaryOrientation(self) == XScreenOrientation_Landscape;
    return false;
}

/* ==================== 抓屏 ==================== */

XPixmap* XScreen_grabWindow(XScreen* self, XWindowId window,
                            int x, int y, int w, int h)
{
#if XPLATFORMNATIVEWINDOW_ON
    XPixmap* captured = XPlatformNativeWindow_grabWindow(window, x, y, w, h);
    if (captured) return captured;
#else
    (void)self; (void)window; (void)x; (void)y; (void)w; (void)h;
#endif /* XPLATFORMNATIVEWINDOW_ON */
    /* 无显示服务器或嵌入式平台不可抓取时，保持 Qt 空 QPixmap 退化。 */
    return XPixmap_create();
}

/* ==================== 通知信号（对标 QScreen 全部 9 个信号） ==================== */

void* XScreen_geometryChanged_signal(XScreen* self, const XRect* geometry)
{
    if (!self) return (void*)(size_t)XScreen_geometryChanged_signal;
    XRect value = geometry ? *geometry : (XRect){0, 0, 0, 0};
    XScreen_emit(self, (size_t)XScreen_geometryChanged_signal,
                 XVarList_Create(XVar(XRect, value)));
    return (void*)(size_t)XScreen_geometryChanged_signal;
}

void* XScreen_availableGeometryChanged_signal(XScreen* self, const XRect* geometry)
{
    if (!self) return (void*)(size_t)XScreen_availableGeometryChanged_signal;
    XRect value = geometry ? *geometry : (XRect){0, 0, 0, 0};
    XScreen_emit(self, (size_t)XScreen_availableGeometryChanged_signal,
                 XVarList_Create(XVar(XRect, value)));
    return (void*)(size_t)XScreen_availableGeometryChanged_signal;
}

void* XScreen_physicalSizeChanged_signal(XScreen* self, const XSizeF* size)
{
    if (!self) return (void*)(size_t)XScreen_physicalSizeChanged_signal;
    XSizeF value = size ? *size : (XSizeF){0.0f, 0.0f};
    XScreen_emit(self, (size_t)XScreen_physicalSizeChanged_signal,
                 XVarList_Create(XVar(XSizeF, value)));
    return (void*)(size_t)XScreen_physicalSizeChanged_signal;
}

void* XScreen_physicalDotsPerInchChanged_signal(XScreen* self, float dpi)
{
    if (!self) return (void*)(size_t)XScreen_physicalDotsPerInchChanged_signal;
    XScreen_emit(self, (size_t)XScreen_physicalDotsPerInchChanged_signal,
                 XVarList_Create(XVar(float, dpi)));
    return (void*)(size_t)XScreen_physicalDotsPerInchChanged_signal;
}

void* XScreen_logicalDotsPerInchChanged_signal(XScreen* self, float dpi)
{
    if (!self) return (void*)(size_t)XScreen_logicalDotsPerInchChanged_signal;
    XScreen_emit(self, (size_t)XScreen_logicalDotsPerInchChanged_signal,
                 XVarList_Create(XVar(float, dpi)));
    return (void*)(size_t)XScreen_logicalDotsPerInchChanged_signal;
}

void* XScreen_virtualGeometryChanged_signal(XScreen* self, const XRect* rect)
{
    if (!self) return (void*)(size_t)XScreen_virtualGeometryChanged_signal;
    XRect value = rect ? *rect : (XRect){0, 0, 0, 0};
    XScreen_emit(self, (size_t)XScreen_virtualGeometryChanged_signal,
                 XVarList_Create(XVar(XRect, value)));
    return (void*)(size_t)XScreen_virtualGeometryChanged_signal;
}

void* XScreen_primaryOrientationChanged_signal(XScreen* self,
                                               XScreenOrientation orientation)
{
    if (!self) return (void*)(size_t)XScreen_primaryOrientationChanged_signal;
    XScreen_emit(self, (size_t)XScreen_primaryOrientationChanged_signal,
                 XVarList_Create(XVar(XScreenOrientation, orientation)));
    return (void*)(size_t)XScreen_primaryOrientationChanged_signal;
}

void* XScreen_orientationChanged_signal(XScreen* self,
                                        XScreenOrientation orientation)
{
    if (!self) return (void*)(size_t)XScreen_orientationChanged_signal;
    XScreen_emit(self, (size_t)XScreen_orientationChanged_signal,
                 XVarList_Create(XVar(XScreenOrientation, orientation)));
    return (void*)(size_t)XScreen_orientationChanged_signal;
}

void* XScreen_refreshRateChanged_signal(XScreen* self, float refreshRate)
{
    if (!self) return (void*)(size_t)XScreen_refreshRateChanged_signal;
    XScreen_emit(self, (size_t)XScreen_refreshRateChanged_signal,
                 XVarList_Create(XVar(float, refreshRate)));
    return (void*)(size_t)XScreen_refreshRateChanged_signal;
}

#endif /* XSCREEN_ON */
