/******************************************************************************
 * @file       XPlatformBackingStore.h
 * @brief      XPlatformBackingStore 平台后备存储契约（对标 Qt 6.8
 *             QPlatformBackingStore）。
 * @details    XPlatformBackingStore 是 XBackingStore 公共类与 Drive 平台
 *             后端之间的最小平台边界，采用「不透明句柄 + 纯函数」形式：
 *             - 全部软件缓冲逻辑（双缓冲 XImage、resize/滚动/静态内容/
 *               tile 遍历/外部缓冲）在公共层共享实现
 *               （Src/XGui/Platform/XPlatformBackingStore.c）一次性完成，
 *               句柄类型 XPlatformBackingStore 为不透明结构，公共层不得
 *               直接访问其字段；
 *             - 平台差异收敛为 XPlatformBackingStoreDriver_* 提交钩子
 *               （见文件尾），Drive 后端只需提供这几个钩子，不再重复
 *               维护缓冲逻辑：Windows 用 GDI DIB + BitBlt、Linux 用
 *               XPlatformNativeWindow_present（XPutImage）、嵌入式软件
 *               模板经 present 回调交给显示驱动；
 *             - paintDevice() 直接返回内部 XImage（可被 XPainter 以
 *               XPainter_begin_image 绘制），toImage() 返回其深拷贝；
 *             - flush() 把脏矩形区域提交到窗口，present 回调（显示驱动）
 *               由公共层统一深拷贝触发；
 *             - resize() 重建缓冲；scroll() 做缓冲内快速位移；
 *               beginPaint()/endPaint()/setStaticContents()/
 *               staticContents()/hasStaticContents() 与 Qt 语义一致。
 *             公共实现/头文件不包含任何平台 API 头；平台差异全部隔离在
 *             Drive/Posix/Graphics、Drive/windows/Graphics、
 *             Drive/Software/Graphics（可复用软件模板）与
 *             Drive/Unsupported/Graphics 中，确保嵌入式可裁剪、可链接。
 * @note       模块开关 XBACKINGSTORE_ON 与 XPLATFORMBACKINGSTORE_ON 定义于
 *             XGuiConfig.h；任一处 0 时本契约整体裁剪，XBackingStore
 *             公共类不可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMBACKINGSTORE_H
#define XPLATFORMBACKINGSTORE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XTypes.h"
#include "XGeometry.h"
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON

/** @brief XWindow 前向声明（公共层只持有借用指针，不引用其内部）。 */
typedef struct XWindow XWindow;
/** @brief XImage 前向声明（paintDevice/toImage 返回或输出的图像类型）。 */
typedef struct XImage XImage;
/** @brief 平台后备存储不透明句柄；具体定义只存在于 Drive 平台后端。 */
typedef struct XPlatformBackingStore XPlatformBackingStore;

/**
 * @brief      平台层 present 回调函数指针类型（平台契约扩展，非 Qt API）。
 * @details    Qt 的 QPlatformBackingStore 把"提交给窗口系统"抽象在无窗口
 *             系统连接的本集成层中不存在等价实体；对嵌入式/软件后端，
 *             flush() 完成缓冲内脏区合并后，按需调用本回调通知显示驱动
 *             或窗口侧缓冲提交各区。回调借用，函数地址与 userData 由
 *             XPlatformBackingStore_setPresentCallback 登记，不释放。
 * @param      userData 登记时的用户数据（借用）。
 * @param      store    触发回调的平台后备存储句柄（借用）。
 * @param      flushedRegion 本次 flush 的脏区（窗口坐标，集合借用）。
 * @param      offset    后备存储相对窗口的偏移（可为 NULL 按零点处理）。
 */
typedef void (*XPlatformBackingStorePresentFn)(
        void* userData, XPlatformBackingStore* store,
        const XRegion* flushedRegion, const XPoint* offset);

/* ==================== 生命周期（平台后端提供） ==================== */

/**
 * @brief      创建绑定指定窗口的平台后备存储。
 * @note       由 XPlatformIntegration_createPlatformBackingStore 调用；
 *             创建初始尺寸为空（size 为 0×0），需 resize 后使用。
 * @param      window 目标 XWindow 借用指针；可为 NULL（纯离屏缓冲）。
 * @return     新句柄；内存不足或后端起用条件不满足时返回 NULL。
 */
XPlatformBackingStore* XPlatformBackingStore_create(XWindow* window);

/**
 * @brief      释放平台后备存储及其全部资源（缓冲/原生表面）。
 * @param      self 目标句柄；可为 NULL，重复调用安全。
 */
void XPlatformBackingStore_delete(XPlatformBackingStore* self);

/* ==================== 访问器 ==================== */

/**
 * @brief      返回绑定的窗口指针（对标 QPlatformBackingStore 所属窗口）。
 * @param      self 目标句柄；可为 NULL。
 * @return     创建时登记的窗口借用指针；未登记返回 NULL。
 */
XWindow* XPlatformBackingStore_window(const XPlatformBackingStore* self);

/**
 * @brief      返回可绘制设备（对标 QPlatformBackingStore::paintDevice）。
 * @details    本实现直接返回内部 XImage 软件缓冲，调用方可配合 XPainter
 *             绘制；只能在 beginPaint/endPaint 之间安全使用（Qt 语义）。
 * @param      self 目标句柄；可为 NULL。
 * @return     内部 XImage 借用指针；未 resize 或创建失败返回 NULL。
 */
XImage* XPlatformBackingStore_paintDevice(XPlatformBackingStore* self);

/**
 * @brief      返回下一块待绘制 tile，并准备当前 tile buffer。
 * @details    PARTIAL 模式按配置的 tile 尺寸遍历 beginPaint() 登记的
 *             窗口坐标脏区；其它模式只返回一次整幅窗口区域。成功后
 *             paintDevice() 指向当前 tile，调用方绘制完成后必须调用
 *             XPlatformBackingStore_flushTile()。
 */
bool XPlatformBackingStore_nextTile(XPlatformBackingStore* self,
                                     XRect* tileRect);

/** @brief 查询当前 tile 在窗口坐标中的原点；未开始 tile 时返回 (0,0)。 */
XPoint XPlatformBackingStore_paintOrigin(
        const XPlatformBackingStore* self);

/**
 * @brief      返回后备存储内容的图像深拷贝（对标 toImage()）。
 * @details    out 必须由调用方持有；函数内部把内部缓冲按像素逐一复制到
 *             out（out 现有内容被释放）。格式与内部缓冲一致。
 * @param      self 目标句柄；可为 NULL。
 * @param      out  输出 XImage 指针。
 * @return     成功返回 true；入参非法或尚未 resize 返回 false。
 */
bool XPlatformBackingStore_toImage(XPlatformBackingStore* self, XImage* out);

/* ==================== 绘制流程 ==================== */

/**
 * @brief      把脏区提交到窗口（对标 QPlatformBackingStore::flush）。
 * @details    仅当 self 非空且 size 有效时执行；region 为 NULL 或空时按
 *             全缓冲区处理。Windows 后端优先经 GDI BitBlt 合成到目标
 *             设备环境；无法取得目标 DC 时与 Linux 后端一致，仅触发
 *             present 回调，保证嵌入式/离屏语义一致。offset 为后备存储
 *             相对窗口的偏移（对标 flush 的 offset 参数）。
 * @param      self   目标句柄；可为 NULL。
 * @param      window 目标窗口借用指针；为 NULL 时用绑定窗口。
 * @param      region 脏区（窗口坐标）；NULL/空表示全缓冲。
 * @param      offset 缓冲相对窗口偏移；可为 NULL 按零点处理。
 */
void XPlatformBackingStore_flush(XPlatformBackingStore* self,
                                 XWindow* window,
                                 const XRegion* region,
                                 const XPoint* offset);
/**
 * @brief      提交当前 tile buffer 到窗口。
 * @param      tileRect 当前 tile 在窗口坐标中的完整矩形；不能为空。
 * @note       PARTIAL 模式下源图像坐标从 (0,0) 开始，平台提交目标从
 *             tileRect 的窗口坐标开始；提交完成后可复用下一片 buffer。
 */
void XPlatformBackingStore_flushTile(XPlatformBackingStore* self,
                                     XWindow* window,
                                     const XRect* tileRect,
                                     const XPoint* offset);

/**
 * @brief      按新尺寸重建后备缓冲（对标 QPlatformBackingStore::resize）。
 * @details    新尺寸必须有效（宽高 >= 0）。缩小后保留左上内容；放大后
 *             新增区域为零（透明/黑）。静态内容区域在尺寸变化后自动向
 *             Qt 语义收敛（与 Qt 一致：resize 后静态内容区域被修剪到
 *             新尺寸内；Qt 的 QPlatformBackingStore::resize 直接接收
 *             staticContents，本契约以 setStaticContents 维护）。
 * @param      self 目标句柄；可为 NULL。
 * @param      size 新尺寸（可为 NULL 按无效处理）。
 */
void XPlatformBackingStore_resize(XPlatformBackingStore* self,
                                  const XSize* size);

/**
 * @brief      在缓冲内快速移动区域（对标 QPlatformBackingStore::scroll）。
 * @details    只处理 area 与缓冲的交集；内容按 (dx, dy) 位移，原区域
 *             清空；移动后返回 true。不支持的部分情况（位移越界/重叠
 *             区域复杂）退化为先复制后清空，保证结果正确。
 * @param      self 目标句柄；可为 NULL。
 * @param      area 滚动区域（窗口坐标）；为 NULL/空时恒返回 false。
 * @param      dx   水平位移（像素）。
 * @param      dy   垂直位移（像素）。
 * @return     成功返回 true；入参非法或区域为空返回 false。
 */
bool XPlatformBackingStore_scroll(XPlatformBackingStore* self,
                                  const XRegion* area, int dx, int dy);

/**
 * @brief      进入绘制阶段（对标 QPlatformBackingStore::beginPaint）。
 * @details    记录绘制矩形集合；endPaint 前 paintDevice 内容有效。
 * @param      self   目标句柄；可为 NULL。
 * @param      region 本次绘制的矩形集合；为 NULL/空按全缓冲处理。
 */
void XPlatformBackingStore_beginPaint(XPlatformBackingStore* self,
                                      const XRegion* region);

/**
 * @brief      结束绘制阶段（对标 QPlatformBackingStore::endPaint）。
 * @param      self 目标句柄；可为 NULL。
 */
void XPlatformBackingStore_endPaint(XPlatformBackingStore* self);

/* ==================== 静态内容 ==================== */

/**
 * @brief      设置静态内容区域（对标 setStaticContents）。
 * @details    静态内容在 resize/滚动时受保护（不参与自动清空）。
 * @param      self   目标句柄；可为 NULL。
 * @param      region 静态内容区域集合；为 NULL/空表示清除静态内容标记。
 */
void XPlatformBackingStore_setStaticContents(XPlatformBackingStore* self,
                                             const XRegion* region);

/**
 * @brief      返回静态内容区域（对标 staticContents）。
 * @note       返回值为深拷贝集合（容量归属调用方），使用后必须调用
 *             XRegion_deinit 释放。
 * @param      self 目标句柄；可为 NULL。
 * @return     静态内容区域副本；未设置或入参非法返回空集合。
 */
XRegion XPlatformBackingStore_staticContents(
        const XPlatformBackingStore* self);

/**
 * @brief      查询是否存在静态内容（对标 hasStaticContents）。
 * @param      self 目标句柄；可为 NULL。
 * @return     已设置非空静态内容区域返回 true。
 */
bool XPlatformBackingStore_hasStaticContents(
        const XPlatformBackingStore* self);

/* ==================== 平台扩展（非 Qt API，仅平台/驱动层） ==================== */

/**
 * @brief      登记原生目标窗口（平台契约扩展，非 Qt API）。
 * @details    Windows 后端把 nativeWindow 视为 HWND，flush 时经
 *             BitBlt 把后备缓冲合成到该窗口设备环境；其它平台仅记录
 *             借用指针，不影响软件提交流程。用于对接平台集成层的原生
 *             窗口句柄链（XPlatformWindow 持有真实 WId 的平台）。
 * @param      self          目标句柄；可为 NULL。
 * @param      nativeWindow  原生窗口句柄（Windows 为 HWND，借用）；可为 NULL。
 */
void XPlatformBackingStore_setNativeTargetWindow(
        XPlatformBackingStore* self, void* nativeWindow);

/**
 * @brief      登记 present 回调（平台契约扩展，公共 XBackingStore 不暴露）。
 * @details    Windows 后端经 BitBlt 合成后、Linux/嵌入式后端在拷贝完脏区
 *             后均会触发。重复调用覆盖旧登记；传 NULL 回调可取消登记。
 * @param      self     目标句柄；可为 NULL。
 * @param      callback 回调函数地址（借用）；可为 NULL 取消。
 * @param      userData 回调用户数据（借用）；可为 NULL。
 */
void XPlatformBackingStore_setPresentCallback(
        XPlatformBackingStore* self,
        XPlatformBackingStorePresentFn callback, void* userData);

/**
 * @brief      登记调用方提供的原始帧缓冲。
 * @details    buffer1/buffer2 只在后备存储使用期间借用，平台不会释放或
 *             扩容它们。bufferSize 是每块缓冲的字节容量；当前 XGui 控件
 *             绘制使用整屏坐标，因此容量必须覆盖 resize() 后的整幅
 *             ARGB32 预乘图像。传入三个空值可恢复平台内部自动分配。
 * @param      self       目标句柄；可为 NULL。
 * @param      buffer1    第一块缓冲；清除外部缓冲时传 NULL。
 * @param      buffer2    第二块缓冲；双缓冲配置下不能为空。
 * @param      bufferSize 每块缓冲容量（字节）；使用内部分配时传 0。
 * @return     参数和当前尺寸均可用并登记成功返回 true；同样参数的
 *             重复登记返回 true，不同参数的重复绑定返回 false。
 */
bool XPlatformBackingStore_setBuffers(XPlatformBackingStore* self,
                                       void* buffer1, void* buffer2,
                                       size_t bufferSize);

/** @brief 计算指定尺寸所需的单块整屏 ARGB32 缓冲字节数。 */
size_t XPlatformBackingStore_requiredBufferSize(const XSize* size);

/* ==================== 平台驱动契约（Drive 平台后端提供） ==================== */

/**
 * @brief      创建平台提交状态（对标 XPlatformGraphicsDriver_* 的 opaque
 *             nativeState 惯例）。公共层把全部软件缓冲逻辑收敛在本模块，
 *             平台后端只负责「把脏区提交到真实显示目标」：
 *             - Posix/X11：present 经 XPlatformNativeWindow_present
 *               （XPutImage）上屏；
 *             - Win32/GDI：把脏矩形同步进自顶向下 DIB 后 BitBlt /
 *               SetDIBitsToDevice 合成到窗口 DC；
 *             - 嵌入式软件模板（XPLATFORMBACKINGSTORE_SOFTWARE_ON）：
 *               提交完全交给公共层统一触发的 present 回调（显示驱动）；
 *             - Unsupported 存根：create 返回 false，XBackingStore 保持
 *               空后端（公共 create 返回 NULL）。
 * @param      outState 输出平台提交状态（由 Driver 拥有，经 destroy 释放）；
 *                      失败/不支持时置 NULL 并返回 false。
 * @param      window   绑定窗口借用指针；可为 NULL。
 * @return     true 平台可提供提交能力；false 保持空后端。
 */
bool XPlatformBackingStoreDriver_create(void** outState, XWindow* window);

/** @brief 释放平台提交状态（create 成功返回的 nativeState；可 NULL/重复安全）。 */
void XPlatformBackingStoreDriver_destroy(void* nativeState);

/**
 * @brief      登记原生目标窗口（对标 setNativeTargetWindow）。
 * @details    Win32 后端把它视为 HWND，flush 时作为 BitBlt 目标；其它
 *             平台仅记录借用指针。nativeWindow 为 NULL 表示清除。
 */
void XPlatformBackingStoreDriver_setNativeTarget(void* nativeState,
                                                 void* nativeWindow);

/**
 * @brief      通知平台缓冲尺寸已重建（resize 后调用）。
 * @details    Win32 后端按新尺寸重建 DIB/内存 DC；软件/Posix 后端 no-op。
 * @param      width/height 当前缓冲尺寸（PARTIAL 模式为 tile 缓冲尺寸）；
 *             非正尺寸表示缓冲已清空。
 */
void XPlatformBackingStoreDriver_surfaceResized(void* nativeState,
                                                int width, int height);

/**
 * @brief      把 flush 的脏区提交到真实显示目标。
 * @details    由公共层在完成脏区合并（含 FULL 整屏）与双缓冲同步后调用；
 *             平台后端只需把 image 的 region 区域上屏，不必处理缓冲维护。
 *             present 回调（显示驱动）由公共层统一在返回后深拷贝触发。
 * @param      nativeState 平台提交状态。
 * @param      window      目标窗口借用指针；可为 NULL（纯离屏）。
 * @param      image       当前软件帧缓冲（ARGB32 预乘）。
 * @param      region      脏区集合（窗口坐标，非空）。
 * @param      offset      缓冲相对窗口的偏移；可为 NULL 按零点处理。
 * @param      full        是否为 FULL 整屏提交模式。
 */
void XPlatformBackingStoreDriver_present(void* nativeState, XWindow* window,
                                         const XImage* image,
                                         const XRegion* region,
                                         const XPoint* offset, bool full);

/**
 * @brief      提交当前 tile buffer（flushTile 用）。
 * @details    tile 的源图像坐标始终从 (0,0) 起，offset 即 tile 原点；
 *             平台后端经 XPlatformNativeWindow_present 或等效路径上屏。
 */
void XPlatformBackingStoreDriver_presentTile(void* nativeState,
                                             XWindow* window,
                                             const XImage* image,
                                             const XRegion* region,
                                             const XPoint* offset);

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMBACKINGSTORE_H */
