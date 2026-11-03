/**
 * @file       XLockFileNative.h
 * @brief      XLockFile 的进程间锁平台后端契约。
 * @details    对标 Qt 6.8.3 qtbase/src/corelib/io/qlockfile.cpp 所依赖的
 *             OS 原语集合：以「独占创建」方式打开锁文件、写入持有者信息、
 *             查询 PID 是否存活、删除锁文件。本文件只声明能力与所有权
 *             契约，不含任何平台头文件；Windows 实现在
 *             Drive/windows/Core/XLockFileNative_win32.c，其余平台由
 *             Drive/Unsupported/Core/XLockFileNative_unsupported.c 提供存根。
 *
 *             Qt 的做法是把这些原语分散在 qlockfile_win.cpp /
 *             qlockfile_unix.cpp 里，由 QLockFilePrivate 直接调用。本实现
 *             按项目平台分层纪律下沉到 Drive/，公共层不感知 Win32/POSIX。
 *
 *             所有权契约：
 *             - 锁句柄 XLockFileNativeHandle 由 openExclusive 产出，必须用
 *               closeHandle 释放；
 *             - 所有字符串参数均为 UTF-16（uint16_t，与 XChar 同宽），
 *               以 NUL 结尾。
 * @note       仅供 XLockFile 对象层与 Drive 平台文件包含。
 * @author     XinYueC 团队
 */
#ifndef XLOCKFILENATIVE_H
#define XLOCKFILENATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if XLOCKFILE_ON

/**
 * @brief 锁文件打开结果（对标 QLockFile::LockError）。
 */
typedef enum XLockFileNativeResult
{
    XLockFileNativeResult_Ok = 0,             /**< 成功独占创建（持有锁）。 */
    XLockFileNativeResult_AlreadyExists = 1,  /**< 锁文件已存在（对标 LockFailedError）。 */
    XLockFileNativeResult_PermissionDenied = 2,/**< 无权限（对标 PermissionError）。 */
    XLockFileNativeResult_Failed = 3           /**< 其它错误（对标 UnknownError）。 */
} XLockFileNativeResult;

/** @brief 不透明锁句柄（Windows 后端为 HANDLE，POSIX 后端为 fd）。 */
typedef void* XLockFileNativeHandle;

/**
 * @brief 独占创建锁文件（对标 QLockFilePrivate::tryLock_sys）。
 * @details 必须以「不存在则创建、已存在则失败」的原子语义打开——这是整个
 *          进程互斥的基础。Windows 用 CreateFileW + CREATE_NEW，POSIX 用
 *          open(O_CREAT|O_EXCL)。
 * @param path 锁文件完整路径（UTF-16）。
 * @param outHandle 输出句柄；失败置 NULL。
 * @return 打开结果。
 */
XLockFileNativeResult XLockFileNative_openExclusive(const uint16_t* path,
                                                    XLockFileNativeHandle* outHandle);

/**
 * @brief 关掉锁句柄但不删文件（对标 closeHandle）。
 */
void XLockFileNative_closeHandle(XLockFileNativeHandle handle);

/**
 * @brief 向已持有的锁句柄写入持有者信息（对标 lockFileContents 的写入段）。
 * @param handle 已打开的句柄。
 * @param data 待写入字节。
 * @param size 字节数。
 * @return 全部写入返回 true。
 */
bool XLockFileNative_writeContents(XLockFileNativeHandle handle,
                                   const char* data, size_t size);

/**
 * @brief 读取锁文件内容（对标 getLockInfo_helper 的读取段）。
 * @param path 锁文件路径（UTF-16）。
 * @param buffer 输出缓冲（调用方提供）。
 * @param capacity 缓冲容量。
 * @param sizeOut 输出实际读到的字节数。
 * @return 读成功返回 true；文件不存在/打不开返回 false。
 */
bool XLockFileNative_readContents(const uint16_t* path, char* buffer,
                                  size_t capacity, size_t* sizeOut);

/**
 * @brief 删除锁文件（对标 removeStaleLock / unlock）。
 * @return 删除成功返回 true。Windows 上文件若仍被其它进程持有会失败
 *         （这正好用来判断「不是僵死锁」）。
 */
bool XLockFileNative_removeFile(const uint16_t* path);

/** @brief 文件是否存在（对标 fileExists）。 */
bool XLockFileNative_fileExists(const uint16_t* path);

/**
 * @brief 查询 PID 对应的进程是否仍在运行（对标 isProcessRunning 的前半段）。
 * @param pid 进程 ID。
 * @return 存活返回 true。
 */
bool XLockFileNative_isProcessRunning(int64_t pid);

/**
 * @brief 取 PID 对应进程的可执行文件名（对标 processNameByPid）。
 * @param pid 进程 ID。
 * @param buffer 输出缓冲（UTF-16，调用方提供）。
 * @param capacityCodes 缓冲容量（UTF-16 码元数，含 NUL）。
 * @return 成功返回 true；进程不存在或无权限返回 false。
 */
bool XLockFileNative_processNameByPid(int64_t pid, uint16_t* buffer, size_t capacityCodes);

/**
 * @brief 当前进程 ID（对标 QCoreApplication::applicationPid）。
 */
int64_t XLockFileNative_currentPid(void);

/**
 * @brief 本机主机名（对标 qlockfile.cpp 的 machineName）。
 * @param buffer 输出缓冲（UTF-16）。
 * @param capacityCodes 容量（码元数，含 NUL）。
 * @return 成功返回 true。
 */
bool XLockFileNative_machineName(uint16_t* buffer, size_t capacityCodes);

/**
 * @brief 本机唯一标识（对标 QSysInfo::machineUniqueId）。
 * @param buffer 输出缓冲（ASCII/UTF-8 字节）。
 * @param capacity 容量。
 * @param sizeOut 输出实际长度。
 * @return 取得返回 true；平台不支持返回 false（调用方按空处理）。
 */
bool XLockFileNative_machineUniqueId(char* buffer, size_t capacity, size_t* sizeOut);

/**
 * @brief 本次启动唯一标识（对标 QSysInfo::bootUniqueId）。
 * @details 用于检测「机器重启过 → 锁必然已僵死」。平台不提供时返回 false。
 */
bool XLockFileNative_bootUniqueId(char* buffer, size_t capacity, size_t* sizeOut);

/**
 * @brief 文件最后修改时刻距现在多少毫秒（对标 QFileInfo::lastModified 差值）。
 * @param path 文件路径（UTF-16）。
 * @param msecsOut 输出毫秒数（正数 = 过去）。
 * @return 取得文件时间返回 true。
 */
bool XLockFileNative_fileAgeMsecs(const uint16_t* path, int64_t* msecsOut);

/**
 * @brief 让当前线程休眠指定毫秒（对标 QThread::sleep）。
 */
void XLockFileNative_sleepMsecs(int64_t msecs);

#ifdef __cplusplus
}
#endif

#endif /* XLOCKFILE_ON */

#endif /* XLOCKFILENATIVE_H */
