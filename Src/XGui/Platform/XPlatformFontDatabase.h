/****************************************************************************
 * @file       XPlatformFontDatabase.h
 * @brief      平台字体数据库不透明接口。
 * @details    公共层只保存字体家族快照；Linux/Windows 的字体枚举由 Drive
 *             后端完成，嵌入式可保留空快照而不依赖系统字体 API。
 ****************************************************************************/
#ifndef XPLATFORMFONTDATABASE_H
#define XPLATFORMFONTDATABASE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XString.h"
#include "XFont.h"
#include "XGuiConfig.h"
#include "XMemory.h"
#include "XVector.h"

#if XPLATFORMINTEGRATION_ON
typedef struct XPlatformFontDatabase XPlatformFontDatabase;
/** @brief 平台默认字体（对标 QPlatformFontDatabase::defaultFont）。 */
XFont XPlatformFontDatabase_defaultFont(const XPlatformFontDatabase* self);
/** @brief 标准字号列表（对标 QPlatformFontDatabase::standardSizes；
 *        元素为 int 的 XVector，调用方释放）。 */
XVector* XPlatformFontDatabase_standardSizes(
        const XPlatformFontDatabase* self);

/**
 * @brief      使用指定内存类型创建平台字体数据库并收集家族快照。
 * @details    创建时经 Drive 后端一次性枚举系统字体家族；嵌入式后端
 *             可保留空快照而不依赖系统字体 API。
 * @param      memory 对象内存类型。
 * @return     新对象指针；分配失败返回 NULL，调用方用
 *             XPlatformFontDatabase_destroy 释放。
 */
XPlatformFontDatabase* XPlatformFontDatabase_create_ex(XMemoryType memory);
/**
 * @brief      使用默认内存类型创建平台字体数据库。
 * @return     新对象指针；失败返回 NULL，调用方用
 *             XPlatformFontDatabase_destroy 释放。
 */
#define XPlatformFontDatabase_create() \
    XPlatformFontDatabase_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      释放平台字体数据库及其家族快照。
 * @param      self 目标对象；可为 NULL（不执行任何操作）。
 * @return     无。
 */
void XPlatformFontDatabase_destroy(XPlatformFontDatabase* self);
/**
 * @brief      查询字体家族快照是否收集成功。
 * @param      self 目标对象；可为 NULL。
 * @return     后端收集成功返回 true；入参非法或收集失败返回 false。
 */
bool XPlatformFontDatabase_isValid(const XPlatformFontDatabase* self);
/** @brief 返回新建的字体家族列表；元素为新建 XString*，调用方负责释放。 */
XVector* XPlatformFontDatabase_families(const XPlatformFontDatabase* self);
/**
 * @brief      判断字体家族是否存在于快照中（区分大小写精确匹配）。
 * @param      self 目标对象；可为 NULL。
 * @param      family 家族名；不能为 NULL。
 * @return     存在精确匹配返回 true；入参非法返回 false。
 */
bool XPlatformFontDatabase_hasFamily(const XPlatformFontDatabase* self,
                                     const XString* family);
/**
 * @brief      hasFamily 的 UTF-8 兼容重载（family 按 UTF-8 解码）。
 * @param      self 目标对象；可为 NULL。
 * @param      family 家族名（UTF-8 编码）；可为 NULL。
 * @return     存在精确匹配返回 true；family 为 NULL 返回 false。
 */
bool XPlatformFontDatabase_hasFamily_2(const XPlatformFontDatabase* self,
                                       const char* family);

/* Drive 后端入口；不得在 Src 中包含平台字体头文件。 */
/**
 * @brief      枚举平台字体家族并写入输出向量（Drive 后端实现）。
 * @param      families 输出向量；元素为新建 XString*，归向量所有；
 *             不能为 NULL。
 * @return     收集成功返回 true；平台无字体 API 时返回 false（空快照）。
 */
bool XPlatformFontDatabaseDriver_collect(XVector* families);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif
