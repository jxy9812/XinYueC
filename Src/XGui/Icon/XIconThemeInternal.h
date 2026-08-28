#ifndef XICONTHEMEINTERNAL_H
#define XICONTHEMEINTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XPixmap.h"
#include "XVector.h"

/* XIcon 内部主题资源解析入口，供主题引擎和已有主题 API 复用。
 * out 在调用前必须是未初始化或已由 XPixmap_deinit_base 清理的状态；
 * 成功时填入实际尺寸的像素图，失败时保持空像素图并返回 false。 */
bool XIconInternal_resolveThemePixmap(const char* name, XPixmap* out);
bool XIconInternal_resolveThemePixmapSize(const char* name, int size, XPixmap* out);
/* 按逻辑尺寸和整数目录倍率选择主题资源，并缩放到指定物理尺寸。 */
bool XIconInternal_resolveThemePixmapSizeScale(const char* name, int size,
                                               int iconScale, int outputSize,
                                               XPixmap* out);
/* 按主题选择源图标但不放大或缩小，用于 actualSize 获取固定资源尺寸。 */
bool XIconInternal_resolveThemePixmapSourceSize(const char* name, int size,
                                                XPixmap* out);
/* 枚举主题中实际存在的图标逻辑尺寸；out 必须是 XSize 元素向量。 */
bool XIconInternal_availableThemeSizes(const char* name, XVector* out);
/* 判断主题图标是否命中可缩放目录，用于 actualSize 的矩形语义。 */
bool XIconInternal_themeHasScalable(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* XICONTHEMEINTERNAL_H */
