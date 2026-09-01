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
typedef struct XStringList XStringList;
#if XPLATFORMINTEGRATION_ON
typedef struct XPlatformTheme XPlatformTheme;
XPlatformTheme* XPlatformTheme_create_ex(XMemoryType memory, const char* name);
#define XPlatformTheme_create(name) \
    XPlatformTheme_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (name))
void XPlatformTheme_destroy(XPlatformTheme* self);
const char* XPlatformTheme_name(const XPlatformTheme* self);
bool XPlatformTheme_isDark(const XPlatformTheme* self);
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
