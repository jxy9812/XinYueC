/****************************************************************************
 * @file       XPlatformFontDatabase.h
 * @brief      平台字体数据库不透明接口。
 * @details    公共层只保存字体家族快照；Linux/Windows 的字体枚举由 Drive
 *             后端完成，嵌入式可保留空快照而不依赖系统字体 API。
 ****************************************************************************/
#ifndef XPLATFORMFONTDATABASE_H
#define XPLATFORMFONTDATABASE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XGuiConfig.h"
#include "XMemory.h"
#include "XVector.h"

#if XPLATFORMINTEGRATION_ON
typedef struct XPlatformFontDatabase XPlatformFontDatabase;

XPlatformFontDatabase* XPlatformFontDatabase_create_ex(XMemoryType memory);
#define XPlatformFontDatabase_create() \
    XPlatformFontDatabase_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
void XPlatformFontDatabase_destroy(XPlatformFontDatabase* self);
bool XPlatformFontDatabase_isValid(const XPlatformFontDatabase* self);
/** @brief 返回新建的字体家族列表；元素为新建 XString*，调用方负责释放。 */
XVector* XPlatformFontDatabase_families(const XPlatformFontDatabase* self);
bool XPlatformFontDatabase_hasFamily(const XPlatformFontDatabase* self,
                                     const char* family);

/* Drive 后端入口；不得在 Src 中包含平台字体头文件。 */
bool XPlatformFontDatabaseDriver_collect(XVector* families);
#endif

#ifdef __cplusplus
}
#endif
#endif
