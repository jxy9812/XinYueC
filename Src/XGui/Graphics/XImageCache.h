/******************************************************************************
 * @file       XImageCache.h
 * @brief      解码图像字节预算 LRU 缓存。
 * @details    嵌入式 HMI 的图标/背景图反复显示时，逐次 XImage_load 会重复
 *             文件 IO + 解码（PNG/JPEG 每次毫秒级）。本缓存以「源标识 →
 *             解码位图」为条目，字节预算上限内按 LRU 淘汰。
 *             对标关系（为什么没有直接叫 QPixmapCache）：
 *             - 接线位置对标 LVGL lv_image_cache：load 内部透明缓存，
 *               调用方零改动；Qt 的 QImage::load 本身【不缓存】（每次
 *               真读真解码），Qt 世界里缓存发生在应用层显式调
 *               QPixmapCache 或 QIcon 引擎内部——本实现把这一层下沉到
 *               load 漏斗，使 XLabel/XSvgIconEngine 等不经改造即受益；
 *             - 淘汰语义对标 Qt QPixmapCache：字节成本 + LRU；
 *             - 键模型与 QPixmapCache 不同：QPixmapCache 由应用显式给
 *               QString key（适合缓存任意渲染结果），本缓存键固定为
 *               path+format（文件源缓存）；「缓存任意渲染结果」的场景
 *               由 XWidget 保留层承担，二者互补不重叠；
 *             - 嵌入式纪律：字节预算 + 条目数双上限、主线程无锁、
 *               预算 0 编译期完全裁剪（Qt QPixmapCache 无条目上限且
 *               内部加锁，裸机不适用）。
 *             缓存条目成本按解码位图实际内存计（bytesPerLine*height +
 *             簿记），超预算插入时从最旧端淘汰直至放得下；单条目超
 *             预算整体拒绝；失效走显式 invalidate（文件重载场景）与
 *             全清 clear。
 *             线程纪律：GUI 主线程专用（与 painter 静态缓存同纪律），
 *             无锁。
 * @note       命中返回的 XImage 是缓存条目的浅共享（COW 语义，与
 *             XImage 深拷贝机制配合——调用方写入会自动分离，不污染缓存）。
 *             首批受益者：XSvgIconEngine（此前每次 pixmap 请求都重新
 *             解析 SVG + 光栅化）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XIMAGECACHE_H
#define XIMAGECACHE_H

#include "XGuiConfig.h"
#include <stdbool.h>
#include <stddef.h>

#if XIMAGECACHE_ON

/** @brief XImage 前向声明（lookup 的出参类型；定义见 XImage.h）。 */
typedef struct XImage XImage;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 缓存字节预算上限（解码位图实际内存之和）。
 *         桌面默认 1MB（约 50-200 张常用图标位图）；裸机/RTOS（非
 *         _WIN32/__APPLE__/__linux__ 目标）默认 0 = 完全裁剪，嵌入式
 *         Linux 按板级 RAM 覆盖（如 -DXIMAGECACHE_MAX_BYTES=262144）。 */
#ifndef XIMAGECACHE_MAX_BYTES
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
#define XIMAGECACHE_MAX_BYTES (1024u * 1024u)
#else
#define XIMAGECACHE_MAX_BYTES 0
#endif
#endif

/** @brief 单格式名上限（含结束符；对齐 XCLIPBOARD_FORMAT_NAME_MAX 惯例）。 */
#define XIMAGECACHE_KEY_MAX 512

/**
 * @brief      查缓存：按（路径 + 格式名）取解码位图的共享视图。
 * @param      path   文件路径（缓存键一部分；NULL/空视为不命中）。
 * @param      format 格式名（与 XImage_load_2 的 format 语义一致；可 NULL）。
 * @param      out    命中时接收位图浅共享（COW；调用方不得 deinit）。
 * @return     true 命中（*out 有效，命中即提升为最近使用）；false 未命中。
 */
bool XImageCache_lookup(const char* path, const char* format, XImage* out);

/**
 * @brief      入缓存：解码位图按（路径 + 格式名）登记（LRU 插入）。
 * @details    深拷贝 image 进缓存（调用方之后可随意处置自己的副本）；
 *             键重复时替换旧条目；单条目字节成本超预算时整体拒绝；
 *             预算不足时从最旧端淘汰。budget=0 时为 no-op。
 * @param      path   文件路径（键）。
 * @param      format 格式名（键；可 NULL——键内以空串区分）。
 * @param      image  已成功解码的位图（只读借用；内部深拷贝）。
 */
void XImageCache_insert(const char* path, const char* format,
                        const XImage* image);

/**
 * @brief      失效：删除指定键（文件重载后调用；未命中安全 no-op）。
 */
void XImageCache_invalidate(const char* path, const char* format);

/**
 * @brief      全清（主题切换/RAM 紧张时调用；释放全部解码位图）。
 */
void XImageCache_clear(void);

/**
 * @brief      统计：当前条目数与字节占用（诊断/保留层预算联动用）。
 */
void XImageCache_stats(size_t* outBytes, int* outCount);

#else /* !XIMAGECACHE_ON：模块裁剪态仍保留外部链接原型（gcc ≥14 对隐式
         函数声明硬报错；XImage.c 无条件调用，裁剪态由空实现回落——
         见 XImageCache.c 的裁剪分支，行为为直通 no-op）。签名与 ON
         分支逐字一致（XImage 前向声明足够表达指针形参）。 */
typedef struct XImage XImage;
bool XImageCache_lookup(const char* path, const char* format,
                        XImage* out);
void XImageCache_insert(const char* path, const char* format,
                        const XImage* image);
void XImageCache_invalidate(const char* path, const char* format);
void XImageCache_clear(void);
void XImageCache_stats(size_t* outBytes, int* outCount);

#endif /* XIMAGECACHE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XIMAGECACHE_H */
