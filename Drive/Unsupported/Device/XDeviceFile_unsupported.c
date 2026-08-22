/**
 * @file XDeviceFile_unsupported.c
 * @brief 不具备终端控制能力的平台的标准输入回显存根。
 * @details
 * 本文件只为未知平台提供终端回显命令的内部实现。
 * POSIX、Windows 和 FatFS 分别由各自后端实现；预处理条件保证这些后端
 * 不会与本存根同时定义同名符号。未知平台没有统一的终端控制契约时，
 * 存根返回 false 且不改变任何状态，调用方应继续使用默认输入行为，或
 * 通过产品自己的 XDeviceFile 后端替换本文件。
 */

#include "XDeviceFile.h"
#include "XDeviceDir.h"

#if !defined(__linux__) && !defined(__APPLE__) && !defined(__BSD__) && \
    !defined(_WIN32) && !defined(XFILE_USE_FATFS)

/**
 * @brief 报告当前平台不支持标准输入回显控制。
 * @param fd 标准输入描述符；存根不解引用，仅保留统一 API 形状。
 * @param enabled 目标回显状态；存根不修改调用方数据。
 * @return 始终返回 false，表示没有可用的终端回显后端。
 */
bool XDeviceFile_legacySetStandardInputEcho(XFd fd, bool enabled)
{
    (void)fd;
    (void)enabled;
    return false;
}

/**
 * @brief 报告当前平台不支持命名共享内存段。
 * @param name 共享内存段名称；存根不解引用。
 * @param create 是否创建新段；存根忽略。
 * @param maxSize 创建时的段大小；存根忽略。
 * @param error 可选的错误码输出；写入 0（不支持的占位语义）。
 * @return 始终返回 XFD_INVALID，表示本平台没有共享内存后端。
 */
XFd XDeviceFile_openSharedMemory(const XString* name, bool create, int64_t maxSize, int* error)
{
    (void)name;
    (void)create;
    (void)maxSize;
    if (error) *error = 0;
    return XFD_INVALID;
}

/**
 * @brief 未知平台的目录打开存根。
 * @return 始终返回 NULL，表示没有可用的目录后端。
 */
void* XDeviceDir_platformOpen(const XString* path)
{
    (void)path;
    return NULL;
}

/** @brief 未知平台的目录读取存根，始终报告失败。 */
bool XDeviceDir_platformRead(void* backendHandle, XDirEntry* entry)
{
    (void)backendHandle;
    (void)entry;
    return false;
}

/** @brief 未知平台的目录关闭存根。 */
void XDeviceDir_platformClose(void* backendHandle)
{
    (void)backendHandle;
}

#endif /* 非 POSIX、非 Windows 且未启用 FatFS */
