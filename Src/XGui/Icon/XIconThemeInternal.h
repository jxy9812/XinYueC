#ifndef XICONTHEMEINTERNAL_H
#define XICONTHEMEINTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XPixmap.h"

/* XIcon 内部主题资源解析入口，供主题引擎和已有主题 API 复用。
 * out 在调用前必须是未初始化或已由 XPixmap_deinit_base 清理的状态；
 * 成功时填入实际尺寸的像素图，失败时保持空像素图并返回 false。 */
bool XIconInternal_resolveThemePixmap(const char* name, XPixmap* out);
bool XIconInternal_resolveThemePixmapSize(const char* name, int size, XPixmap* out);

#ifdef __cplusplus
}
#endif

#endif /* XICONTHEMEINTERNAL_H */
