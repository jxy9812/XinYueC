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
#if XPLATFORMINTEGRATION_ON
typedef struct XPlatformTheme XPlatformTheme;
XPlatformTheme* XPlatformTheme_create_ex(XMemoryType memory, const char* name);
#define XPlatformTheme_create(name) \
    XPlatformTheme_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (name))
void XPlatformTheme_destroy(XPlatformTheme* self);
const char* XPlatformTheme_name(const XPlatformTheme* self);
bool XPlatformTheme_isDark(const XPlatformTheme* self);
bool XPlatformThemeDriver_detect(bool* dark, char* name, size_t capacity);
#endif
#ifdef __cplusplus
}
#endif
#endif
