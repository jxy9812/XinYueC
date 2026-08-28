/******************************************************************************
 * @file       XIconScaledPixmapCache.h
 * @brief      XIcon 缩放像素图缓存内部辅助（对标 Qt 6.8 QPixmapCache 中的
 *             QIcon 缩放缓存键）。
 * @author     XinYueC 团队
 * @note       本模块不对外暴露公共 API，仅供 XIcon 默认像素图引擎和
 *             XIconThemeEngine 复用同一缓存键规则。
 ******************************************************************************/
#ifndef XICONSCALEDPIXMAPCACHE_H
#define XICONSCALEDPIXMAPCACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XPixmap.h"
#include "XIcon.h"

/**
 * @brief 缩放图标缓存查找。
 * @param prefix        缓存键前缀，用于区分默认引擎与主题引擎。
 * @param sourceKey     源资源键；默认引擎传像素图缓存键，主题引擎传图标名称。
 * @param mode          图标显示模式。
 * @param width         缓存的实际宽度（物理像素）。
 * @param height        缓存的实际高度（物理像素）。
 * @param dprThousand   设备像素比放大 1000 后的整数，例如 2.0 传 2000。
 * @param out           输出像素图指针；未命中时保持调用前状态。
 * @return 命中返回 true，否则返回 false。
 */
bool XIconScaledPixmapCache_find(const char* prefix, const char* sourceKey,
                                 uint64_t paletteKey, XIconMode mode,
                                 int width, int height, int dprThousand,
                                 XPixmap* out);

/**
 * @brief 缩放图标缓存插入。
 * @param prefix        缓存键前缀，与查找时保持一致。
 * @param sourceKey     源资源键，与查找时保持一致。
 * @param mode          图标显示模式。
 * @param width         缓存的实际宽度（物理像素）。
 * @param height        缓存的实际高度（物理像素）。
 * @param dprThousand   设备像素比放大 1000 后的整数。
 * @param pixmap        待缓存的像素图指针。
 * @return 插入成功返回 true，否则返回 false。
 */
bool XIconScaledPixmapCache_insert(const char* prefix, const char* sourceKey,
                                   uint64_t paletteKey, XIconMode mode,
                                   int width, int height, int dprThousand,
                                   const XPixmap* pixmap);

/**
 * @brief 清空全部图标缩放缓存。
 * @note 主题名称或搜索路径变化时应调用本函数，避免旧解析结果被复用。
 */
void XIconScaledPixmapCache_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* XICONSCALEDPIXMAPCACHE_H */
