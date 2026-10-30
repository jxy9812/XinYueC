/******************************************************************************
 * @file       XIconEngine.h
 * @brief      XIconEngine 图标引擎抽象类（对标 Qt 6.8 QIconEngine）。
 * @note       引擎对象由传给 XIcon_init_engine 的指针转移所有权；派生类
 *             通过 XClass vtable 重载具体绘制、资源和序列化行为。
 ******************************************************************************/
#ifndef XICONENGINE_H
#define XICONENGINE_H

#if XGUI_ON

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XClass.h"
#include "XString.h"
#include "XTypes.h"
#include "XIcon.h"
#include "XVector.h"
#include "XIODevice.h"

XCLASS_DEFINE_BEGING(XIconEngine)
XCLASS_DEFINE_ENUM(XIconEngine, Paint) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XIconEngine, ActualSize),
XCLASS_DEFINE_ENUM(XIconEngine, Pixmap),
XCLASS_DEFINE_ENUM(XIconEngine, AddPixmap),
XCLASS_DEFINE_ENUM(XIconEngine, AddFile),
XCLASS_DEFINE_ENUM(XIconEngine, Key),
XCLASS_DEFINE_ENUM(XIconEngine, Clone),
XCLASS_DEFINE_ENUM(XIconEngine, Read),
XCLASS_DEFINE_ENUM(XIconEngine, Write),
XCLASS_DEFINE_ENUM(XIconEngine, AvailableSizes),
XCLASS_DEFINE_ENUM(XIconEngine, IconName),
XCLASS_DEFINE_ENUM(XIconEngine, IsNull),
XCLASS_DEFINE_ENUM(XIconEngine, ScaledPixmap),
XCLASS_DEFINE_ENUM(XIconEngine, VirtualHook),
XCLASS_DEFINE_END(XIconEngine)

typedef struct XIconEngine
{
    XClass m_class; /**< 继承的 XClass 基类成员。 */
} XIconEngine;

/**
 * @brief 图标引擎扩展钩子编号（对标 Qt 6.8 QIconEngine::IconEngineHook）。
 *
 * @details 编号用于保持二进制兼容，调用方通过
 * XIconEngine_virtualHook_base() 传递扩展参数；未知编号必须被安全忽略。
 */
typedef enum XIconEngineHook
{
    XIconEngine_IsNullHook = 3, /**< 查询引擎是否表示空图标。 */
    XIconEngine_ScaledPixmapHook = 4 /**< 按设备像素比获取像素图。 */
} XIconEngineHook;

/**
 * @brief 缩放图标参数结构。
 * @note 用于 scaledPixmap 虚函数传递目标尺寸、状态和缩放比例。
 */
typedef struct XIconEngineScaledPixmapArgument
{
    XPixmap* pixmap; /**< 输出图像指针。 */
    XSize size; /**< 目标像素尺寸。 */
    float scale; /**< 设备像素比或缩放因子。 */
    uint8_t mode; /**< 图标显示模式（XIconMode，0-3）。 */
    uint8_t state; /**< 图标状态（XIconState，0-1）。 */
} XIconEngineScaledPixmapArgument;

/**
 * @brief 初始化 XIconEngine 虚函数表。
 * @return 图标引擎类虚函数表指针。
 */
XVtable* XIconEngine_class_init(void);

/**
 * @brief 按指定内存类型创建图标引擎。
 * @param memory 对象使用的内存类型。
 * @return 新建图标引擎指针；失败时返回 NULL。
 */
XIconEngine* XIconEngine_create_ex(XMemoryType memory);

/**
 * @brief 初始化图标引擎实例。
 * @param self 待初始化的图标引擎指针。
 */
void XIconEngine_init(XIconEngine* self);


/**
 * @brief 释放图标引擎实例资源的基类调度接口。
 * @param self 待释放的图标引擎指针。
 */

/**
 * @brief 删除堆上分配的图标引擎实例。
 * @param self 待删除的图标引擎指针。
 */

/**
 * @brief 绘制图标。
 * @param self 图标引擎指针。
 * @param painter 目标绘制器指针，具体类型由 GUI 绘制后端决定。
 * @param rect 绘制目标矩形。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 */
void XIconEngine_paint_base(const XIconEngine* self, void* painter, const XRect* rect,
                            XIconMode mode, XIconState state);

/**
 * @brief 计算图标在指定约束下的实际尺寸。
 * @param self 图标引擎指针。
 * @param size 尺寸约束。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 * @param out 输出实际尺寸。
 */
void XIconEngine_actualSize_base(const XIconEngine* self, const XSize* size,
                                 XIconMode mode, XIconState state, XSize* out);

/**
 * @brief 获取指定尺寸和状态的图标像素图。
 * @param self 图标引擎指针。
 * @param size 请求尺寸。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 * @param out 输出像素图对象。
 */
void XIconEngine_pixmap_base(const XIconEngine* self, const XSize* size,
                             XIconMode mode, XIconState state, XPixmap* out);

/**
 * @brief 向引擎添加已有像素图。
 * @param self 图标引擎指针。
 * @param pixmap 待添加像素图。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 */
void XIconEngine_addPixmap_base(XIconEngine* self, const XPixmap* pixmap,
                                XIconMode mode, XIconState state);

/**
 * @brief 从文件向引擎添加图标资源。
 * @param self 图标引擎指针。
 * @param fileName UTF-8 已转换为 XString 的文件名。
 * @param size 期望尺寸；可为 NULL 表示使用原始尺寸。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 */
void XIconEngine_addFile_base(XIconEngine* self, const XString* fileName,
                              const XSize* size, XIconMode mode, XIconState state);

/**
 * @brief 获取引擎的缓存键（对标 Qt 6.8 QIconEngine::key()，
 *        qiconengine.h:27——返回 QString 值语义）。
 * @param self 图标引擎指针。
 * @return 新建的引擎键字符串；调用方使用 XClassDelete() 释放。
 * @note 所有权：Qt 的 QString 为值语义，C 侧对应形态是**返回新副本**
 *       （调用方负责释放）；本接口与 XIconEngine_iconName_base 同口径。
 */
XString* XIconEngine_key_base(const XIconEngine* self);

/**
 * @brief 克隆图标引擎。
 * @param self 源图标引擎指针。
 * @return 新克隆的引擎对象；失败时返回 NULL。
 */
XIconEngine* XIconEngine_clone_base(const XIconEngine* self);

/**
 * @brief 从设备读取引擎数据（对标 Qt 6.8 QIconEngine::read(QDataStream &)，
 *        qiconengine.h:29）。
 * @param self 图标引擎指针。
 * @param device 输入设备指针。
 * @return 读取成功返回 true，否则返回 false。
 * @note 序列化后端：Qt 用 QDataStream（QtCore 的二进制流），X 侧无
 *       QDataStream 等价类型，改用 XIODevice（XIO 的设备抽象）承载
 *       同一条读写通道——承载类型差异，语义（读回引擎自身状态）一致。
 */
bool XIconEngine_read_base(XIconEngine* self, XIODevice* device);

/**
 * @brief 将引擎数据写入设备（对标 Qt 6.8 QIconEngine::write(QDataStream &) const，
 *        qiconengine.h:30）。
 * @param self 图标引擎指针。
 * @param device 输出设备指针。
 * @return 写入成功返回 true，否则返回 false。
 * @note 序列化后端差异同 XIconEngine_read_base（QDataStream ↔ XIODevice）。
 */
bool XIconEngine_write_base(const XIconEngine* self, XIODevice* device);

/**
 * @brief 获取指定模式和状态下的可用尺寸列表。
 * @param self 图标引擎指针。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 * @param out 输出 XVector；元素类型为 XSize。
 */
void XIconEngine_availableSizes_base(const XIconEngine* self, XIconMode mode,
                                     XIconState state, XVector* out);

/**
 * @brief 获取图标名称。
 * @param self 图标引擎指针。
 * @return 新建的图标名称字符串；调用方使用 XClassDelete() 释放。
 */
XString* XIconEngine_iconName_base(const XIconEngine* self);

/**
 * @brief 判断引擎是否为空。
 * @param self 图标引擎指针。
 * @return 为空返回 true，否则返回 false。
 */
bool XIconEngine_isNull_base(const XIconEngine* self);

/**
 * @brief 获取指定缩放因子的像素图。
 * @param self 图标引擎指针。
 * @param size 请求尺寸。
 * @param mode 图标显示模式。
 * @param state 图标状态。
 * @param scale 设备像素比或缩放因子。
 * @param out 输出像素图对象。
 */
void XIconEngine_scaledPixmap_base(const XIconEngine* self, const XSize* size,
                                   XIconMode mode, XIconState state, float scale,
                                   XPixmap* out);

/**
 * @brief 调用引擎扩展虚函数。
 * @param self 图标引擎指针。
 * @param id 扩展操作标识。
 * @param data 扩展操作参数指针。
 */
void XIconEngine_virtualHook_base(const XIconEngine* self, int id, void* data);

#ifdef __cplusplus
}
#endif

#undef XIconEngine_create
#define XIconEngine_create() XIconEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XGUI_ON */

#endif /* XICONENGINE_H */
