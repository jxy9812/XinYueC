/****************************************************************************
 * @file       XPlatformServices.h
 * @brief      平台桌面服务不透明接口。
 * @details    用于打开 URL/文件等外部资源；真正的 xdg-open/ShellExecute
 *             调用仅存在于 Drive，嵌入式后端安全返回 false。
 ****************************************************************************/
#ifndef XPLATFORMSERVICES_H
#define XPLATFORMSERVICES_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XMemory.h"
#if XPLATFORMINTEGRATION_ON
typedef struct XPlatformServices XPlatformServices;
XPlatformServices* XPlatformServices_create_ex(XMemoryType memory);
#define XPlatformServices_create() \
    XPlatformServices_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XPlatformServices_destroy(XPlatformServices* self);
bool XPlatformServices_isAvailable(const XPlatformServices* self);
bool XPlatformServices_openUrl(XPlatformServices* self, const char* url);
bool XPlatformServicesDriver_isAvailable(void);
bool XPlatformServicesDriver_openUrl(const char* url);
#endif
#ifdef __cplusplus
}
#endif
#endif
