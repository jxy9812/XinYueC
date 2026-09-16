/****************************************************************************
 * @file       XPlatformTheme.h
 * @brief      平台主题快照接口。
 * @details    主题名称与深色偏好由 Drive 探测，公共层不暴露系统主题类型。
 ****************************************************************************/
#ifndef XPLATFORMTHEME_H
#define XPLATFORMTHEME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XMemory.h"
#include "XString.h"
#include "XFont.h"
typedef struct XStringList XStringList;
#if XPLATFORMINTEGRATION_ON
/** @brief 色彩方案（对标 Qt::ColorScheme，数值一致）。 */
typedef enum XPlatformThemeColorScheme
{
    XPlatformThemeColorScheme_Unknown = 0, /**< 未知。 */
    XPlatformThemeColorScheme_Light = 1,   /**< 浅色。 */
    XPlatformThemeColorScheme_Dark = 2     /**< 深色。 */
} XPlatformThemeColorScheme;

typedef struct XPlatformTheme XPlatformTheme;
/** @brief 创建主题快照（XString 主版本；name 为 NULL 时用驱动探测名）。 */
XPlatformTheme* XPlatformTheme_create_ex(XMemoryType memory, const XString* name);
/** @brief 创建主题快照（UTF-8 兼容重载，转发主版本）。 */
XPlatformTheme* XPlatformTheme_create_ex_2(XMemoryType memory, const char* name);
#define XPlatformTheme_create(name) \
    XPlatformTheme_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (name))
void XPlatformTheme_destroy(XPlatformTheme* self);
/** @brief 主题名（内部借用 XString*；未设置时返回 NULL，不得释放）。 */
const XString* XPlatformTheme_name(const XPlatformTheme* self);
/** @brief 主题名（UTF-8 借用；未设置时返回 NULL，不得释放）。 */
const char* XPlatformTheme_name_2(const XPlatformTheme* self);
bool XPlatformTheme_isDark(const XPlatformTheme* self);
/** @brief 色彩方案（对标 QPlatformTheme::colorScheme）。 */
int XPlatformTheme_colorScheme(const XPlatformTheme* self);
/** @brief 设置色彩方案（XPlatformThemeColorScheme）。 */
void XPlatformTheme_setColorScheme(XPlatformTheme* self, int scheme);
/** @brief 平台默认字体快照（对标 QPlatformTheme::font）。 */
XFont XPlatformTheme_font(const XPlatformTheme* self);
/** @brief 设置平台默认字体快照。 */
void XPlatformTheme_setFont(XPlatformTheme* self, const XFont* font);
/** @brief 主题提示查询（对标 QPlatformTheme::themeHint；最小快照恒 0）。 */
int64_t XPlatformTheme_themeHint(const XPlatformTheme* self, int hint);
bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity);
/**
 * @brief 获取平台默认的图标搜索路径。
 * @param fallback 是否获取独立回退图标路径；否则获取主题根路径。
 * @param out 接收路径的字符串列表；调用者拥有列表，平台只追加副本。
 * @return 平台提供至少一条路径时返回 true，否则返回 false。
 */
bool XPlatformThemeDriver_iconSearchPaths(bool fallback, XStringList* out);
/**
 * @brief 获取平台默认的系统图标主题名称。
 * @param fallback 是否获取系统回退主题名称。
 * @param name 接收 UTF-8 名称的缓冲区。
 * @param capacity 缓冲区字节容量，包含结尾空字符。
 * @return 成功写入非空名称时返回 true，否则返回 false。
 */
bool XPlatformThemeDriver_iconThemeName(bool fallback, char* name, size_t capacity);
#endif
#ifdef __cplusplus
}
#endif
#endif
