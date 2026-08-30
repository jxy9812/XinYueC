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
#include "XString.h"

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
/*
 * 按指定逻辑尺寸选择主题条目，并判断被选中的条目是否为可缩放目录。
 * 与只检查主题中是否存在任意 Scalable 目录的旧接口不同，该接口会遵循
 * Qt QIconLoaderEngine::entryForSize() 的精确匹配、距离比较和格式优先级，
 * 供 actualSize() 在固定 PNG 与 SVG 同时登记时返回正确的尺寸语义。
 * @param name 主题图标名称；不得为 NULL 或空字符串。
 * @param size 请求的逻辑边长；非正值表示没有可选条目。
 * @return 被选条目为 Scalable 时返回 true；未命中或选中固定/阈值条目时
 *         返回 false。
 */
bool XIconInternal_themeUsesScalableEntry(const char* name, int size);
/*
 * 仅判断主题引擎是否登记了图标条目，不读取或解码像素数据。
 * 该查询对应 Qt QIconLoaderEngine::isNull() 的延迟加载语义：文件存在
 * 即视为已有条目，损坏内容留给实际绘制或 pixmap() 阶段报告失败。
 */
bool XIconInternal_themeHasIcon(const char* name);
/* 解析主题引擎实际命中的图标名称；返回新建字符串，调用方负责删除。
 * 未命中时返回 NULL，调用方可保留原始请求名称。 */
XString* XIconInternal_resolveThemeIconName(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* XICONTHEMEINTERNAL_H */
