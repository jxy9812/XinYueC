/******************************************************************************
 * @file       XBackingStore.h
 * @brief      XBackingStore 后备存储类（对标 Qt 6.8 QBackingStore，实现
 *             全部公开 API）。
 * @details    XBackingStore 继承 XObject，是 XWindow 的离屏后备帧缓冲：
 *             - 构造时经 XGuiApplication 平台集成层创建平台后端
 *               （XPlatformBackingStore，实现位于 Drive 平台目录）；
 *             - paintDevice() 返回内部 XImage 软件缓冲，配合
 *               XPainter_begin_image 可直接绘制，无需任何平台图形 API；
 *             - resize() 记录逻辑尺寸并请求平台重建缓冲；beginPaint()/
 *               endPaint() 界定绘制区间；scroll() 在缓冲内位移；
 *               setStaticContents()/staticContents()/hasStaticContents() 保存
 *               静态内容区域，公共查询保留调用方区域，不被后端图像裁剪；
 *             - flush() 把脏区提交到窗口（对标 QBackingStore::flush 的
 *               region/window/offset 三参数），平台差异被后端隔离。
 *             本模块公共层不依赖任何平台 API，嵌入式直接内置于单后端
 *             集成层，不需要真实窗口系统即可完成全部绘制与提交流程。
 * @note       模块总开关 XBACKINGSTORE_ON 定义于 XGuiConfig.h；同时受
 *             XPLATFORMINTEGRATION_ON 与 XPLATFORMBACKINGSTORE_ON 约束，
 *             任一处关闭时本公共 API 被裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBACKINGSTORE_H
#define XBACKINGSTORE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XTypes.h"
#include "XGeometry.h"
#if XWINDOW_ON
#include "XWindow.h"
#else /* !XWINDOW_ON */
/** @brief XWINDOW_ON=0 时的 XWindow 前向声明，保持指针 API 可编译。 */
typedef struct XWindow XWindow;
#endif /* XWINDOW_ON */

/** @brief XImage 前向声明（paintDevice 返回值；具体 API 由 XImage.h 提供）。 */
typedef struct XImage XImage;
/** @brief 平台后备存储前向声明（handle 返回借用句柄）。 */
typedef struct XPlatformBackingStore XPlatformBackingStore;

#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XBackingStorePrivate XBackingStorePrivate;

/** @brief 声明 XBackingStore 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XBackingStore)
XCLASS_DEFINE_EXTEND_END(XBackingStore, XObject)



/**
 * @brief      XBackingStore 后备存储对象；m_class 必须为第一个成员。
 * @details    平台后端句柄与尺寸快照保存在 m_data 私有块中，调用方不得
 *             直接访问任何字段。
 */
typedef struct XBackingStore
{
    XObject                  m_class; /**< 第一个成员，由 XObject 管理。 */
    XBackingStorePrivate*    m_data;  /**< 私有数据块，由 XBackingStore 拥有。 */
} XBackingStore;

/**
 * @brief      初始化 XBackingStore 类虚函数表并返回共享表指针。
 * @return     XBackingStore 类的共享 XVtable 指针。
 */
XVtable* XBackingStore_class_init(void);

/**
 * @brief      初始化绑定指定窗口的后备存储（对标 QBackingStore 构造）。
 * @details    经 XGuiApplication 集成层创建平台后端；未初始化应用或后端
 *             不可用时 m_data->m_platform 为 NULL，后续 API 安全退化，但
 *             window()、size() 和静态内容快照仍保持公共对象状态。
 *             window 为借用指针，随其生命周期由调用方管理。
 * @param      self   待初始化对象；必须与 XBackingStore_deinit_base 成对调用。
 * @param      window 目标 XWindow 借用指针；可为 NULL（纯离屏缓冲）。
 */
void XBackingStore_init(XBackingStore* self, XWindow* window);

/**
 * @brief      使用默认内存类型创建绑定指定窗口的后备存储。
 * @param      window 目标 XWindow 借用指针；可为 NULL。
 * @return     新对象指针；失败返回 NULL，调用方用 XBackingStore_delete_base
 *             释放（释放空对象安全）。
 */
#define XBackingStore_create(window) \
    XBackingStore_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (window))

/**
 * @brief      使用指定内存类型创建绑定指定窗口的后备存储。
 * @param      memory 对象内存类型。
 * @param      window 目标 XWindow 借用指针；可为 NULL。
 * @return     新对象指针；失败返回 NULL。
 */
XBackingStore* XBackingStore_create_ex(XMemoryType memory, XWindow* window);

/** @brief 通过 XClass 虚表释放 XBackingStore 资源（栈/外部存储对象使用）。 */
#define XBackingStore_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XBackingStore 对象。 */
#define XBackingStore_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 窗口与可绘制设备（对标 QBackingStore） ==================== */

/**
 * @brief      返回绑定的窗口（对标 QBackingStore::window）。
 * @return     借用指针；未绑定返回 NULL。
 */
XWindow* XBackingStore_window(const XBackingStore* self);

/**
 * @brief      返回可绘制设备（对标 QBackingStore::paintDevice）。
 * @details    返回内部 XImage 软件缓冲，配合 XPainter_begin_image 绘制；
 *             只能在 beginPaint 之后、endPaint 之前使用。
 * @return     内部 XImage 借用指针；不可用返回 NULL（所有权归本对象）。
 */
XImage* XBackingStore_paintDevice(XBackingStore* self);

/**
 * @brief      返回平台后备存储句柄（对标 QBackingStore::handle）。
 * @details    句柄由 XBackingStore 持有，调用方只可借用，不能释放或修改
 *             其生命周期。平台后端不可用时返回 NULL；优先在构造阶段由
 *             XGuiApplication 集成层创建，构造时集成层尚未就绪时会在本
 *             函数首次调用时按 Qt 的懒创建语义再次尝试。
 * @param      self 目标对象；可为 NULL。
 * @return     平台后备存储借用句柄；不可用时返回 NULL。
 */
XPlatformBackingStore* XBackingStore_handle(const XBackingStore* self);

/**
 * @brief      返回后备缓冲内容副本（对标 QBackingStore::toImage）。
 * @details    out 由调用方初始化持有；函数释放 out 原有内容后按当前缓冲
 *             的像素格式深拷贝生成副本。内部缓冲未 resize 或后端不可用
 *             时返回 false，out 保持不变。
 * @param      self 目标对象；可为 NULL。
 * @param      out  输出 XImage 指针（调用方持有，用完 XImage_deinit_base）。
 * @return     true 成功；false 入参非法或缓冲不可用。
 */
bool XBackingStore_toImage(XBackingStore* self, XImage* out);

/* ==================== 尺寸与刷新（对标 QBackingStore） ==================== */

/**
 * @brief      按新尺寸重建后备缓冲（对标 QBackingStore::resize）。
 * @param      self 目标对象；可为 NULL。
 * @param      size 新尺寸；NULL 或负尺寸按无效忽略。有效尺寸即使平台
 *             后端当前不可用，也会被 size() 记录，符合 Qt 公共状态语义。
 */
void XBackingStore_resize(XBackingStore* self, const XSize* size);

/**
 * @brief      返回当前缓冲尺寸（对标 QBackingStore::size）。
 * @param      self 目标对象；可为 NULL。
 * @return     最近一次接受的有效 resize 尺寸；未 resize 返回 0×0。
 */
XSize XBackingStore_size(const XBackingStore* self);

/**
 * @brief      在缓冲内快速移动区域（对标 QBackingStore::scroll）。
 * @param      self 目标对象；可为 NULL。
 * @param      area 滚动区域（窗口坐标）；为 NULL/空时返回 false。
 * @param      dx   水平位移（像素）。
 * @param      dy   垂直位移（像素）。
 * @return     成功返回 true。
 */
bool XBackingStore_scroll(XBackingStore* self, const XRegion* area,
                          int dx, int dy);

/**
 * @brief      进入绘制阶段（对标 QBackingStore::beginPaint）。
 * @param      self   目标对象；可为 NULL。
 * @param      region 本次绘制矩形集合；为 NULL/空按全缓冲处理。
 */
void XBackingStore_beginPaint(XBackingStore* self, const XRegion* region);

/**
 * @brief      结束绘制阶段（对标 QBackingStore::endPaint）。
 * @param      self 目标对象；可为 NULL。
 */
void XBackingStore_endPaint(XBackingStore* self);

/**
 * @brief      把脏区提交到窗口（对标 QBackingStore::flush）。
 * @details    region 为窗口坐标脏区；offset 为后备存储原点相对窗口的偏移
 *             （对标 Qt flush 的 offset，默认零点）。
 * @param      self   目标对象；可为 NULL。
 * @param      region 脏区（窗口坐标）；NULL/空表示全缓冲。
 * @param      window 目标窗口借用指针；为 NULL 时用绑定窗口。
 * @param      offset 缓冲相对窗口偏移；可为 NULL 按零点处理。
 */
void XBackingStore_flush(XBackingStore* self, const XRegion* region,
                         XWindow* window, const XPoint* offset);

/* ==================== 静态内容（对标 QBackingStore） ==================== */

/**
 * @brief      设置静态内容区域（对标 QBackingStore::setStaticContents）。
 * @param      self   目标对象；可为 NULL。
 * @param      region 静态内容区域集合；为 NULL/空表示清除。查询接口返回
 *             的区域与本参数值一致，即使矩形超出当前后备图像范围。
 */
void XBackingStore_setStaticContents(XBackingStore* self,
                                     const XRegion* region);

/**
 * @brief      返回静态内容区域（对标 QBackingStore::staticContents）。
 * @note       返回值为深拷贝集合，使用后必须调用 XRegion_deinit 释放；
 *             返回值保持 setStaticContents 传入的区域，不读取平台裁剪提示。
 * @return     静态内容区域副本；未设置返回空集合（count==0）。
 */
XRegion XBackingStore_staticContents(const XBackingStore* self);

/**
 * @brief      查询是否存在静态内容（对标 QBackingStore::hasStaticContents）。
 * @return     已设置非空静态内容区域返回 true。
 */
bool XBackingStore_hasStaticContents(const XBackingStore* self);

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON */

#ifdef __cplusplus
}
#endif
#endif /* XBACKINGSTORE_H */
