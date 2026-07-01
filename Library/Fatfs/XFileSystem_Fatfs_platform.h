/**
 * @file XFileSystem_Fatfs_platform.h
 * @brief Fatfs 平台抽象层 —— 声明各平台需要实现的 API
 * 
 * 每个平台（Win32/STM32/Posix）需提供此头文件中声明的所有函数实现。
 * diskio 扇区 I/O 接口沿用 diskio.h 的声明，本头文件仅声明扩展的平台 API。
 */

#ifndef XFILESYSTEM_FATFS_PLATFORM_H
#define XFILESYSTEM_FATFS_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 前向声明 */
struct XString;
typedef struct XString XString;

/* ============================================================================
 * 一、驱动器信息（平台实现）
 * ============================================================================ */

/**
 * @brief 获取可用的驱动器/挂载点数量
 * @return 驱动器数量，失败返回 0
 */
int XFatfsDrives_count(void);

/**
 * @brief 获取驱动器/挂载点路径
 * @param index 驱动器索引（0 ~ count-1）
 * @param path 输出 XString（需调用者预先创建）
 * @return 成功返回 true
 */
bool XFatfsDrives_at(int index, XString* path);

/* ============================================================================
 * 二、当前工作目录（平台实现）
 * ============================================================================ */

/**
 * @brief 获取当前工作目录
 * @param path 输出 XString（需调用者预先创建）
 * @return 成功返回 true
 */
bool XFatfsPath_current(XString* path);

/**
 * @brief 设置当前工作目录
 * @param path 目标路径
 * @return 成功返回 true
 */
bool XFatfsPath_setCurrent(const XString* path);

/* ============================================================================
 * 三、特殊路径（平台实现）
 * ============================================================================ */

/**
 * @brief 获取用户主目录路径
 * @param path 输出 XString（需调用者预先创建）
 * @return 成功返回 true
 */
bool XFatfsPath_home(XString* path);

/**
 * @brief 获取根目录路径
 * @param path 输出 XString（需调用者预先创建）
 * @return 成功返回 true
 */
bool XFatfsPath_root(XString* path);

/**
 * @brief 获取临时目录路径
 * @param path 输出 XString（需调用者预先创建）
 * @return 成功返回 true
 */
bool XFatfsPath_temp(XString* path);

#ifdef __cplusplus
}
#endif

#endif /* XFILESYSTEM_FATFS_PLATFORM_H */