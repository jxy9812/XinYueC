/******************************************************************************
 * @file       XPlatformBackingStore.h
 * @brief      XPlatformBackingStore 平台后备存储契约（对标 Qt 6.8
 *             QPlatformBackingStore）。
 * @details    XPlatformBackingStore 是 XBackingStore 公共类与 Drive 平台
 *             后端之间的最小平台边界，采用「不透明句柄 + 纯函数」形式：
 *             - 句柄类型 XPlatformBackingStore 为不透明结构，具体字段只在
 *               Drive 下各平台后端的同名实现文件中定义，公共层不得访问；
 *             - 首字母为"平台后端提供"，XBackingStore 只通过本头文件声明
 *               的函数操作句柄，不关心句柄内部是软件缓冲、GDI DIB 还是
 *               其它硬件表面；
 *             - paintDevice() 直接返回内部 XImage（可被 XPainter 以
 *               XPainter_begin_image 绘制），toImage() 返回其深拷贝；
 *             - flush() 把脏矩形区域提交到窗口，Windows 后端在持有目标
 *               设备环境的条件下经 GDI BitBlt 合成，Linux 后端与嵌入式
 *               通过可选 present 回调把区域交给显示驱动；软件结果一致；
 *             - resize() 重建缓冲；scroll() 做缓冲内快速位移；
 *               beginPaint()/endPaint()/setStaticContents()/
 *               staticContents()/hasStaticContents() 与 Qt 语义一致。
 *             公共实现/头文件不包含任何平台 API 头；平台差异全部隔离在
 *             Drive/Posix/Graphics、Drive/windows/Graphics 与
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

#endif /* XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMBACKINGSTORE_H */
