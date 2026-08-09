/**
 * @file XFileSystem_unsupported.c
 * @brief 不具备终端控制能力的平台的标准输入回显存根。
 * @details
 * 本文件只为未知平台提供 XFileSystem_setStandardInputEcho 的可链接实现。
 * POSIX、Windows 和 FatFS 分别由各自后端实现；预处理条件保证这些后端
 * 不会与本存根同时定义同名符号。未知平台没有统一的终端控制契约时，
 * 存根返回 false 且不改变任何状态，调用方应继续使用默认输入行为，或
 * 通过产品自己的 XFileSystem 后端替换本文件。
 */

#include "XFileSystem.h"

#if !defined(__linux__) && !defined(__APPLE__) && !defined(__BSD__) && \
    !defined(_WIN32) && !defined(XFILE_USE_FATFS)

/**
 * @brief 报告当前平台不支持标准输入回显控制。
 * @param fd 标准输入描述符；存根不解引用，仅保留统一 API 形状。
 * @param enabled 目标回显状态；存根不修改调用方数据。
 * @return 始终返回 false，表示没有可用的终端回显后端。
 */
bool XFileSystem_setStandardInputEcho(XFd fd, bool enabled)
{
    (void)fd;
    (void)enabled;
    return false;
}

#endif /* 非 POSIX、非 Windows 且未启用 FatFS */
