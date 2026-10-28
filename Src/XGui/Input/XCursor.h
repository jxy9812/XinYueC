/******************************************************************************
 * @file       XCursor.h
 * @brief      XCursor 光标类（对标 Qt 6.8 QCursor）。
 * @details    XCursor 继承 XObject，描述窗口系统的鼠标光标：内置形状
 *             （对标 Qt::CursorShape 全部 24 个标准形状）、热点，以及
 *             位图/掩码/像素图自定义光标三种形态。本模块不依赖任何平台
 *             API：形状数据可程序化配置，进程级光标位置由 XCursor_pos/
 *             XCursor_setPos 维护，未来由 XGuiApplication/输入后端接入
 *             真实输入设备后同步。
 * @note       模块总开关 XCURSOR_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XCursor 公共 API，XWindow 的光标接口退化为空实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCURSOR_H
#define XCURSOR_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XObject.h"
#include "XGeometry.h"
#include "XMemory.h"

#if XCURSOR_ON

/** @brief 内置光标形状（对标 Qt 6.8 Qt::CursorShape）。 */
typedef enum XCursorShape
{
    XCursor_Arrow= 0,          /**< 默认箭头。 */
    XCursor_UpArrow,           /**< 上箭头。 */
    XCursor_Cross,             /**< 十字。 */
    XCursor_Wait,              /**< 等待（沙漏）。 */
    XCursor_IBeam,             /**< 文本 I 形。 */
    XCursor_SizeVer,           /**< 垂直尺寸调整。 */
    XCursor_SizeHor,           /**< 水平尺寸调整。 */
    XCursor_SizeBDiag,         /**< 主对角线尺寸调整。 */
    XCursor_SizeFDiag,         /**< 副对角线尺寸调整。 */
    XCursor_SizeAll,           /**< 全方位尺寸调整。 */
    XCursor_Blank,             /**< 隐藏光标。 */
    XCursor_SplitV,            /**< 垂直分割。 */
    XCursor_SplitH,            /**< 水平分割。 */
    XCursor_PointingHand,      /**< 手型。 */
    XCursor_Forbidden,         /**< 禁止。 */
    XCursor_WhatsThis,         /**< 这是什么。 */
    XCursor_Busy,              /**< 忙碌。 */
    XCursor_OpenHand,          /**< 张开的手掌。 */
    XCursor_ClosedHand,        /**< 握紧的手掌。 */
    XCursor_DragCopy,          /**< 拖拽复制。 */
    XCursor_DragMove,          /**< 拖拽移动。 */
    XCursor_DragLink,          /**< 拖拽链接。 */
    XCursor_Last = XCursor_DragLink, /**< 最后一个标准形状（与 Qt 一致）。 */
    XCursor_Bitmap = 24,       /**< 位图光标。 */
    XCursor_Custom = 25        /**< 自定义像素图光标。 */
} XCursorShape;

/** @brief XCursor 继承 XObject，不新增虚函数槽位。 */
XCLASS_DEFINE_BEGING(XCursor)
XCLASS_DEFINE_EXTEND_END(XCursor, XObject)

/**
 * @brief      XCursor 光标对象；m_class 必须是第一个成员且禁止手工修改。
 * @details    自定义资源（位图/掩码/像素图）由对象拥有，生命周期随对象。
 */
typedef struct XCursor
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理，禁止手工修改。 */
    XCursorShape m_shape; /**< 当前形状（对标 QCursor::shape；setter 原样往返，不收窄）。 */
    XPoint m_hotSpot;  /**< 热点坐标；(-1,-1) 表示使用中心（未显式设置）。 */
    bool m_hasHotSpot; /**< 是否显式设置了热点。 */
    struct XBitmap* m_bitmap; /**< 位图光标源；对象拥有，可为 NULL。 */
    struct XBitmap* m_mask;   /**< 位图光标掩码；对象拥有，可为 NULL。 */
    struct XPixmap* m_pixmap; /**< 像素图光标源；对象拥有，可为 NULL。 */
} XCursor;

/**
 * @brief      初始化 XCursor 类虚函数表并返回共享表指针。
 * @return     XCursor 类的共享 XVtable 指针。
 */
XVtable* XCursor_class_init(void);

/**
 * @brief      初始化默认 XCursor（对标 QCursor() 默认构造，ArrowCursor）。
 * @param      self 待初始化的对象指针；生命周期结束时必须成对调用
 *             XClassDeinit。
 */
void XCursor_init(XCursor* self);

/**
 * @brief      使用默认内存类型在堆上创建默认 XCursor。
 * @return     新对象指针；失败返回 NULL，调用方用 XClassDelete 释放。
 */
#define XCursor_create() XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建默认 XCursor。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL，调用方用 XClassDelete 释放。
 */
XCursor* XCursor_create_ex(XMemoryType memory);

/**
 * @brief      以指定形状创建 XCursor（对标 QCursor(Qt::CursorShape)）。
 * @param      shape 内置光标形状。
 * @return     新对象指针；分配失败返回 NULL，调用方用 XClassDelete 释放。
 */
XCursor* XCursor_create_shape(XCursorShape shape);

/**
 * @brief      以位图+掩码创建自定义光标（对标 QCursor(QBitmap,QBitmap,hotX,hotY)）。
 * @details    位图/掩码由新对象深拷贝持有，调用方仍需自行释放传入对象。
 * @param      bitmap 光标位图；可为 NULL。
 * @param      mask 光标掩码；可为 NULL。
 * @param      hotX 热点 X；-1 表示中心。
 * @param      hotY 热点 Y；-1 表示中心。
 * @return     新对象指针；分配失败返回 NULL，调用方用 XClassDelete 释放。
 */
XCursor* XCursor_create_bitmap(const struct XBitmap* bitmap,
                               const struct XBitmap* mask,
                               int hotX, int hotY);

/**
 * @brief      以像素图创建自定义光标（对标 QCursor(QPixmap,hotX,hotY)）。
 * @details    像素图由新对象深拷贝持有，调用方仍需自行释放传入对象。
 * @param      pixmap 光标像素图；可为 NULL。
 * @param      hotX 热点 X；-1 表示中心。
 * @param      hotY 热点 Y；-1 表示中心。
 * @return     新对象指针；分配失败返回 NULL，调用方用 XClassDelete 释放。
 */
XCursor* XCursor_create_pixmap(const struct XPixmap* pixmap,
                               int hotX, int hotY);

/** @brief 通过 XClass 虚表释放 XCursor 资源（栈/外部存储对象使用）。 */
/** @brief 删除堆上的 XCursor 对象。 */

/**
 * @brief      返回光标形状（对标 QCursor::shape）。
 * @param      self 目标光标；可为 NULL。
 * @return     光标形状；入参非法返回 Arrow。
 */
XCursorShape XCursor_shape(const XCursor* self);

/**
 * @brief      设置光标形状（对标 QCursor::setShape）。
 * @details    显式设置形状会使位图/掩码/像素图形态清空（与 Qt 一致）。
 * @param      self 目标光标；可为 NULL。
 * @param      shape 内置光标形状。
 */
void XCursor_setShape(XCursor* self, XCursorShape shape);

/**
 * @brief      返回光标位图源（对标 QCursor::bitmap，返回内部借用指针）。
 * @param      self 目标光标；可为 NULL。
 * @return     位图内部指针；无位图或入参非法返回 NULL，不转移所有权。
 */
struct XBitmap* XCursor_bitmap(const XCursor* self);

/**
 * @brief      返回光标掩码（对标 QCursor::mask，返回内部借用指针）。
 * @param      self 目标光标；可为 NULL。
 * @return     掩码内部指针；无掩码或入参非法返回 NULL，不转移所有权。
 */
struct XBitmap* XCursor_mask(const XCursor* self);

/**
 * @brief      返回光标像素图源（对标 QCursor::pixmap，返回内部借用指针）。
 * @param      self 目标光标；可为 NULL。
 * @return     像素图内部指针；无像素图或入参非法返回 NULL，不转移所有权。
 */
struct XPixmap* XCursor_pixmap(const XCursor* self);

/**
 * @brief      返回光标热点（对标 QCursor::hotSpot）。
 * @param      self 目标光标；可为 NULL。
 * @return     热点坐标；未显式设置或入参非法返回 (-1,-1)。
 */
XPoint XCursor_hotSpot(const XCursor* self);

/**
 * @brief      显式设置光标热点（对标 QCursor::setHotSpot）。
 * @param      self 目标光标；可为 NULL。
 * @param      x 热点 X；-1 表示中心。
 * @param      y 热点 Y；-1 表示中心。
 */
void XCursor_setHotSpot(XCursor* self, int x, int y);

/**
 * @brief      返回进程级当前光标位置（对标 QCursor::pos，无屏幕版本）。
 * @details    平台后端已注册时优先查询真实系统光标位置（XQueryPointer
 *             等）并同步进程级缓存；无后端时返回 XCursor_setPos 维护的
 *             缓存值，初始为 (0,0)。
 * @return     当前光标位置。
 */
XPoint XCursor_pos(void);

/**
 * @brief      设置进程级当前光标位置（对标 QCursor::setPos(int,int)）。
 * @details    平台后端已注册时转发原生光标定位（X11 XWarpPointer），
 *             同时更新进程级缓存；无后端/定位失败时仅记录坐标（静默，
 *             与 Qt 无平台插件时行为一致）。
 * @param      x 目标 X 坐标。
 * @param      y 目标 Y 坐标。
 */
void XCursor_setPos(int x, int y);

/**
 * @brief      按坐标点设置进程级当前光标位置（对标 QCursor::setPos(QPoint)）。
 * @param      pos 目标位置；可为 NULL 按 (0,0) 处理。
 */
void XCursor_setPos_point(const XPoint* pos);

/**
 * @brief      判断光标是否为内置形状（位图/像素图形态之外的普通形状）。
 * @param      self 目标光标；可为 NULL。
 * @return     shape 不在 Bitmap/Custom 范围内时返回 true。
 */
bool XCursor_isShapeCursor(const XCursor* self);

/**
 * @brief      平台光标后端钩子表（对标 Qt QPlatformCursor）。
 * @details    由平台原生窗口后端（如 Linux X11）在原生连接建立后经
 *             XCursor_installPlatformBackend 注册，XCursor 公共 API 与
 *             派生消费方（XWidget 光标接线）保持平台无关：查询/定位进程
 *             级光标位置（XQueryPointer/XWarpPointer）与把光标应用到
 *             原生窗口（XCreateFontCursor+XDefineCursor）。未注册后端或
 *             窗口未映射时所有钩子调用静默失败（返回 false），行为与
 *             无平台后端时一致。钩子表必须为静态存储期（后端保存指针，
 *             不拷贝）。
 */
typedef struct XCursorPlatformBackend
{
    bool (*m_queryPos)(int* x, int* y);
                      /**< 查询全局光标位置；失败返回 false。 */
    bool (*m_warpPos)(int x, int y);
                      /**< 定位全局光标（对标 QCursor::setPos 平台转发）。 */
    bool (*m_applyWindowCursor)(uintptr_t nativeWindowId,
                                const XCursor* cursor);
                      /**< 把光标（形状/自定义）应用到原生窗口；
                           nativeWindowId 为平台窗口 id（X11 Window）。 */
    bool (*m_clearWindowCursor)(uintptr_t nativeWindowId);
                      /**< 取消窗口光标，恢复默认（XUndefineCursor 语义）。 */
} XCursorPlatformBackend;

/**
 * @brief      注册平台光标后端（全进程单后端；重复注册覆盖前者）。
 * @param      backend 钩子表指针；可为 NULL 注销。必须为静态存储期。
 */
void XCursor_installPlatformBackend(const XCursorPlatformBackend* backend);

/**
 * @brief      把光标应用到原生窗口（XWidget::setCursor 生效路径；对标
 *             QWindowSystemInterface 内部的窗口光标应用）。
 * @details    内部转发平台后端 applyWindowCursor；无后端或窗口无效时
 *             静默返回 false（对标 Qt 平台插件不可用时光标仅存储）。
 * @param      nativeWindowId 平台窗口 id（X11 Window / Win32 HWND）。
 * @param      cursor 目标光标；可为 NULL（等价清除，恢复窗口默认）。
 * @return     平台应用成功返回 true。
 */
bool XCursor_applyToWindow(uintptr_t nativeWindowId, const XCursor* cursor);

/**
 * @brief      取消原生窗口光标（恢复默认箭头；XWidget::unsetCursor 路径）。
 * @param      nativeWindowId 平台窗口 id。
 * @return     平台清除成功返回 true；无后端或窗口无效时静默 false。
 */
bool XCursor_clearForWindow(uintptr_t nativeWindowId);

/* ==================== 软件光标精灵（无硬件光标平台触摸跟随） ============ */

/**
 * @brief      把**生效光标**下发到原生窗口（覆盖光标优先）。
 * @details    对标 Qt：QGuiApplication::setOverrideCursor 的覆盖光标
 *             优先于控件光标（hover 形状），栈空后回落控件光标。本
 *             函数是「谁该生效」的唯一判定口：
 *               1) 应用覆盖栈非空 → 下发栈顶覆盖光标（X11 XDefineCursor
 *                  / Win32 SetCursor 通道）；
 *               2) 覆盖栈空 → 下发调用方给的控件/窗口光标 fallback；
 *               3) fallback 为 NULL → 清除（恢复系统默认箭头）。
 *             控件光标（XWidget_setCursor/XWindow_setCursor）与应用
 *             覆盖栈变更（XGuiApplication_setOverrideCursor 等）都经
 *             此口，保证覆盖光标不被 hover 形状顶掉、栈空后能恢复
 *             原控件形状。此前覆盖栈仅被软件精灵路径读取，硬件光标
 *             平台（X11/Win32）完全收不到 ⇒ 换外形无效果
 *             （2026-10-28 板子实测）。
 * @param      nativeWindowId 原生窗口句柄（X11 Window / Win32 HWND）。
 * @param      fallback 覆盖栈为空时使用的控件/窗口光标；可为 NULL。
 * @return     true 已下发；false 无平台后端/窗口无效（静默）。
 */
bool XCursor_applyEffectiveToWindow(uintptr_t nativeWindowId,
                                    const XCursor* fallback);

/**
 * @brief      内建经典箭头位图尺寸（软件精灵恒回落此位图绘制）。
 * @details    精灵外接矩形在面板坐标里旋转 90/270 时宽高对调，上界
 *             XCURSOR_SW_ARROW_H（平台备份缓冲按此定容）。
 */
#define XCURSOR_SW_ARROW_W 12 /**< 内建箭头位图宽（逻辑像素）。 */
#define XCURSOR_SW_ARROW_H 19 /**< 内建箭头位图高（逻辑像素）。 */

/**
 * @brief      设置软件光标精灵运行时开关（对标 Qt fbdev/eglfs 无硬件
 *             光标平台的软件绘制路径开关）。
 * @details    默认开。只影响软件精灵是否绘制：平台硬件光标后端在位
 *             （X11/Win32）时软件路径本就整体休眠，本开关额外提供
 *             嵌入式板级逃生口。
 * @param      enabled true 开（默认），false 关。
 */
void XCursor_setSoftwareCursorEnabled(bool enabled);

/**
 * @brief      返回软件光标精灵运行时开关当前值。
 * @return     开返回 true（默认），关返回 false。
 */
bool XCursor_softwareCursorEnabled(void);

/**
 * @brief      当前是否应绘制软件光标精灵（激活总条件）。
 * @details    四条件同时成立才返回 true：软件使能（默认开）&& 未安装
 *             平台硬件光标后端（g_cursorBackend==NULL；fbdev 即此态，
 *             X11/Win32 装了后端本函数恒 false，软件路径整体休眠）&&
 *             已收到过触摸位置（XCursor_setPos 至少被调用一次）&&
 *             当前有效形状非 Blank（隐藏）。有效形状取值顺序：
 *             XGuiApplication_overrideCursor() 非空用之；否则用最近一次
 *             XCursor_applyToWindow 记录的形状；再否则 Arrow。除 Blank
 *             之外，其它形状绘制一律回落内建箭头位图（见
 *             XCursor_swArrowBits）。
 */
bool XCursor_swActive(void);

/**
 * @brief      当前软件光标精灵外接矩形（逻辑坐标）。
 * @details    左上角 = 光标位置 − 内建箭头热点，尺寸 = 内建箭头宽高；
 *             与 XCursor_swActive 同源（未激活返回全零空矩形）。平台
 *             呈现层按当前内容旋转把本矩形正变换到面板坐标落笔。
 * @return     精灵外接矩形；未激活为 (0,0,0,0)。
 */
XRect XCursor_swRect(void);

/**
 * @brief      按**指定形状**查内建精灵位图（平台后端造原生光标用）。
 * @details    与 swBits（按当前有效形状）不同：本函数按参数形状查内建
 *             位图表（Arrow/PointingHand/Cross/IBeam/Wait），供 X11/
 *             Win32 硬件光标后端把内建位图转成原生光标——**跨平台外形
 *             一致**（同一份 2bit 素材：软件精灵直接画、硬件平台转原生
 *             位图光标），而非各自映射系统形状。查无表（SizeVer 等功能
 *             形状/Bitmap/Custom）返回 NULL，调用方回落系统形状映射。
 * @param      shape 目标形状。
 * @param      outW 位图宽出参；可为 NULL。
 * @param      outH 位图高出参；可为 NULL。
 * @param      outHotX 热点 X 出参；可为 NULL。
 * @param      outHotY 热点 Y 出参；可为 NULL。
 * @return     位图数据指针（静态存储期，借用）；无内建位图返回 NULL。
 */
const uint8_t* XCursor_swSpriteBitsFor(XCursorShape shape, int* outW,
                                       int* outH, int* outHotX, int* outHotY);

/**
 * @brief      当前**有效形状**的内建精灵位图（软件精灵绘制取图口）。
 * @details    按软件精灵有效形状（override 优先/形状账次之，Blank 不
 *             会到达——swActive 已过滤）返回对应位图：Arrow/PointingHand/
 *             Cross/IBeam/Wait 各有内建 2bit 位图（0=透明/1=黑/2=白，
 *             行主序，与 swArrowBits 同口径）；其余形状回落箭头位图。
 *             静态存储期，调用方只借用。平台呈现层配合 swRect/swWidth/
 *             swHeight/swHotSpot 落笔，形状变化自动生效。
 * @return     位图数据指针；swActive 为 false 时返回箭头位图（调用方
 *             应先以 swActive 过滤）。
 */
const uint8_t* XCursor_swBits(void);

/** @brief 返回当前有效形状的内建精灵位图宽（逻辑像素）。 */
int XCursor_swWidth(void);

/** @brief 返回当前有效形状的内建精灵位图高（逻辑像素）。 */
int XCursor_swHeight(void);

/**
 * @brief      返回当前有效形状的精灵热点。
 * @return     热点坐标（逻辑像素；XCursor_swRect 已按其计算左上角）。
 */
XPoint XCursor_swHotSpot(void);

/**
 * @brief      内建经典箭头位图（约 12x19，黑描边白填充）。
 * @details    行主序 W*H 字节，每像素 2bit 值域按字节存储：
 *             0=透明 / 1=黑 / 2=白；颜色到面板像素值的映射（RGB565
 *             黑=0x0000 白=0xFFFF 等）由呈现层按位深实现。静态存储期，
 *             调用方只借用。
 * @return     位图数据指针（XCURSOR_SW_ARROW_W*XCURSOR_SW_ARROW_H 字节）。
 */
const uint8_t* XCursor_swArrowBits(void);

/** @brief 返回内建箭头位图宽（= XCURSOR_SW_ARROW_W）。 */
int XCursor_swArrowWidth(void);

/** @brief 返回内建箭头位图高（= XCURSOR_SW_ARROW_H）。 */
int XCursor_swArrowHeight(void);

/**
 * @brief      返回内建箭头热点（约 (1,1)，指尖正对箭头顶点）。
 * @return     热点坐标（逻辑像素）。
 */
XPoint XCursor_swArrowHotSpot(void);

/**
 * @brief      记录软件光标位置 XCursor_pos 的参考坐标系（fbdev 触摸
 *             喂给专用，对标 Qt 平台光标的位置屏幕归属）。
 * @details    XCursor_pos 存的是「最近命中窗口」的局部坐标——原点/旋转
 *             逐窗不同，软件精灵上屏必须按**喂给时的同一坐标系**正变换
 *             到面板像素。输入后端每次 XCursor_setPos 喂位时同步记录本
 *             系（窗口内容旋转角/整窗逻辑宽高/窗口全局原点）；fbdev 直
 *             写的三条精灵重画路径（present 尾部/blitPanelRects/
 *             blitSnapshotPanelRects/条带填色）经 XCursor_swPosFrame 取
 *             同一系换算，精灵面板位置只随喂给变化——多顶层窗口交替
 *             上屏、条带旁路直写不再把精灵画到换系错位处来回跳。未记
 *             录时为恒等系（rot=0、宽高 0、原点 (0,0)；0° 换算与宽高
 *             无关，面板坐标==逻辑坐标，与既有主窗全屏行为逐位一致）。
 * @param      rot 窗口内容旋转角（0/90/180/270；非法角由呈现层归一）。
 * @param      logW 整窗逻辑宽（旋转换算基准）。
 * @param      logH 整窗逻辑高。
 * @param      origin 窗口全局原点（面板像素坐标）；NULL 按 (0,0)。
 */
void XCursor_swSetPosFrame(int rot, int logW, int logH, const XPoint* origin);

/**
 * @brief      返回软件光标位置参考坐标系（见 XCursor_swSetPosFrame）。
 * @param      outRot 内容旋转角出参；可为 NULL。
 * @param      outLogW 整窗逻辑宽出参；可为 NULL。
 * @param      outLogH 整窗逻辑高出参；可为 NULL。
 * @param      outOrigin 窗口全局原点出参；可为 NULL。
 */
void XCursor_swPosFrame(int* outRot, int* outLogW, int* outLogH,
                        XPoint* outOrigin);

/** @brief 交换两个光标（对标 QCursor::swap）。
 * @param a 第一个光标；可为 NULL。
 * @param b 第二个光标；可为 NULL。
 * @return 无返回值。
 */
void XCursor_swap(XCursor* a, XCursor* b);
/** @brief 比较两个光标是否等价（对标 QCursor::operator==）。
 * @param a 第一个光标；可为 NULL。
 * @param b 第二个光标；可为 NULL。
 * @return 形状/资源一致返回 true。
 */
bool XCursor_equals(const XCursor* a, const XCursor* b);

#endif /* XCURSOR_ON */

#if !XCURSOR_ON
/* 依赖回退：XWidget::cursor() 按值返回 XCursor，即使光标公共 API 被裁剪
 * 也需要一个完整的零状态值类型来保持头文件可编译。 */
typedef enum XCursorShape
{
    XCursor_Arrow = 0
} XCursorShape;
typedef struct XCursor
{
    uint8_t m_disabled;
    /* 回退态补全成员:XCursor.c 的 equals/赋值路径仍会按完整布局访问,
       回退壳缺成员会在 XCURSOR_ON=0(含 XGUI_ON=0 连带)时编译失败。 */
    XCursorShape m_shape;
    bool m_hasHotSpot;
    XPoint m_hotSpot;
    void* m_bitmap;
    void* m_mask;
    void* m_pixmap;
} XCursor;
#endif /* !XCURSOR_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCURSOR_H */
