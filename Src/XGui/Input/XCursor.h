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
    XCursorShape m_shape; /**< 当前形状（对标 QCursor::shape）。 */
    XPoint m_hotSpot;  /**< 热点坐标；(-1,-1) 表示使用中心（未显式设置）。 */
    bool m_hasHotSpot; /**< 是否显式设置了热点。 */
    struct XBitmap* m_bitmap; /**< 位图光标源；对象拥有，可为 NULL。 */
    struct XBitmap* m_mask;   /**< 位图光标掩码；对象拥有，可为 NULL。 */
    struct XPixmap* m_pixmap; /**< 像素图光标源；对象拥有，可为 NULL。 */
} XCursor;

/**
 * @brief      初始化默认 XCursor（对标 QCursor() 默认构造，ArrowCursor）。
 * @param      self 待初始化的对象指针；生命周期结束时必须成对调用
 *             XCursor_deinit_base。
 */
void XCursor_init(XCursor* self);

/**
 * @brief      使用默认内存类型在堆上创建默认 XCursor。
 * @return     新对象指针；失败返回 NULL，调用方用 XCursor_delete_base 释放。
 */
#define XCursor_create() XCursor_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建默认 XCursor。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL，调用方用 XCursor_delete_base 释放。
 */
XCursor* XCursor_create_ex(XMemoryType memory);

/**
 * @brief      以指定形状创建 XCursor（对标 QCursor(Qt::CursorShape)）。
 * @param      shape 内置光标形状。
 * @return     新对象指针；分配失败返回 NULL，调用方用 XCursor_delete_base 释放。
 */
XCursor* XCursor_create_shape(XCursorShape shape);

/**
 * @brief      以位图+掩码创建自定义光标（对标 QCursor(QBitmap,QBitmap,hotX,hotY)）。
 * @details    位图/掩码由新对象深拷贝持有，调用方仍需自行释放传入对象。
 * @param      bitmap 光标位图；可为 NULL。
 * @param      mask 光标掩码；可为 NULL。
 * @param      hotX 热点 X；-1 表示中心。
 * @param      hotY 热点 Y；-1 表示中心。
 * @return     新对象指针；分配失败返回 NULL，调用方用 XCursor_delete_base 释放。
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
 * @return     新对象指针；分配失败返回 NULL，调用方用 XCursor_delete_base 释放。
 */
XCursor* XCursor_create_pixmap(const struct XPixmap* pixmap,
                               int hotX, int hotY);

/** @brief 通过 XClass 虚表释放 XCursor 资源（栈/外部存储对象使用）。 */
#define XCursor_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XCursor 对象。 */
#define XCursor_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝 XCursor（对标 QCursor 拷贝构造）；未初始化目标自动初始化。 */
#define XCursor_copy_base(self, other)     XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动 XCursor；移动后源对象为空光标。 */
#define XCursor_move_base(self, other)     XClass_move_base((XClass*)(self), (XClass*)(other))

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
 * @details    无平台输入后端时由 XCursor_setPos 维护，初始为 (0,0)；未来
 *             输入后端会在收到指针移动事件时同步更新。
 * @return     当前光标位置。
 */
XPoint XCursor_pos(void);

/**
 * @brief      设置进程级当前光标位置（对标 QCursor::setPos(int,int)）。
 * @details    无平台后端时仅记录坐标；平台后端接入后转发原生光标定位。
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
} XCursor;
#endif /* !XCURSOR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCURSOR_H */
