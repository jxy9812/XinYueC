/**
 * @file       XLockFile.h
 * @brief      基于文件的进程间锁（对标 Qt 6.8 QLockFile）。
 * @details    在两个或多个进程之间用「独占创建锁文件」建立互斥，用于保护
 *             共享资源（配置文件、端口、共享内存区等）。语义逐条对齐
 *             qtbase/src/corelib/io/qlockfile.cpp：
 *
 *             - **独占创建**：lock/tryLock 以「不存在则创建」的原子方式打开
 *               锁文件，已存在则视为「被别人持有」（对标 CREATE_NEW /
 *               O_CREAT|O_EXCL）；
 *             - **僵死锁检测**：持锁进程崩溃后锁文件会残留。本类按 PID +
 *               可执行文件名 + 主机标识 + 启动标识 + 文件年龄综合判断是否
 *               僵死，确认僵死后自动清除并重试（对标 isApparentlyStale）；
 *             - **写入持有者信息**：锁文件内容为五行——
 *               PID / 进程名 / 主机名 / 机器唯一 ID / 启动唯一 ID，
 *               供 getLockInfo 诊断（对标 lockFileContents）；
 *             - **删除竞争消解**：多个进程同时发现僵死锁时，用
 *               `<锁文件>.rmlock` 二级锁确保只有一个去删（对标 Qt 同款）；
 *             - **僵死阈值**：默认 30 秒；长持锁场景应 setStaleLockTime(0)
 *               关闭按年龄判僵死，改用 getLockInfo 提示用户。
 *
 *             平台原语在 Src/XCore/XPlatform/XLockFileNative.h；
 *             Windows 实现在 Drive/windows/Core/XLockFileNative_win32.c。
 *             实现只依赖 XinYueC 抽象层。
 *
 *             与 Qt 的裁剪边界：
 *             - 不提供 `staleLockTimeAsDuration()`（C 层无 std::chrono），
 *               保留 `setStaleLockTime(int msecs)` / `staleLockTime()` 以毫秒
 *               为单位的接口；
 *             - 不提供 `tryLock(std::chrono::milliseconds)` 重载，统一用
 *               `XLockFile_tryLock_2(timeoutMsecs)`，负值 = 无限等待（对齐 Qt
 *               对负超时的处理）。
 * @note       本类不是 XClass 派生对象（无虚函数需求，且需与 Qt 的
 *             Q_DISABLE_COPY 语义一致），用 init/create 成对管理。
 * @author     XinYueC 团队
 */
#ifndef XLOCKFILE_H
#define XLOCKFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"

/* 模块总开关：兜底默认值（对齐 XSettings.h 口径） */
#ifndef XLOCKFILE_ON
#define XLOCKFILE_ON 1
#endif

#if XLOCKFILE_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "XString.h"

/**
 * @brief 加锁结果（对标 QLockFile::LockError，取值一致）。
 */
typedef enum XLockFile_LockError
{
    XLockFile_NoError = 0,           /**< 成功取得锁。 */
    XLockFile_LockFailedError = 1,   /**< 锁被其它进程持有。 */
    XLockFile_PermissionError = 2,   /**< 父目录无权限创建锁文件。 */
    XLockFile_UnknownError = 3       /**< 其它错误（如磁盘满）。 */
} XLockFile_LockError;

/** @brief 不透明私有数据。 */
typedef struct XLockFilePrivate XLockFilePrivate;

/**
 * @brief 锁文件对象。
 * @details 与 Qt 的 QLockFile 同构：构造即绑定一个文件路径（此时未加锁），
 *          调用 lock/tryLock 才真正创建锁文件。
 */
typedef struct XLockFile
{
    XLockFilePrivate* m_data;   /**< 私有数据（对象拥有）。 */
} XLockFile;

/* ==================== 生命周期 ==================== */

/**
 * @brief 初始化锁文件对象（对标 QLockFile(const QString &fileName)）。
 * @details 只记录路径，不创建文件；对象处于未加锁状态。
 * @param self 待初始化的对象；不可为 NULL。
 * @param fileName 锁文件路径；不可为 NULL（NULL 时按空路径处理，lock 必失败）。
 */
void XLockFile_init(XLockFile* self, const XString* fileName);

/** @brief XLockFile_init 的 UTF-8 路径便捷版本。 */
void XLockFile_init_utf8(XLockFile* self, const char* fileNameUtf8);

/**
 * @brief 堆创建（对标 new QLockFile）。
 * @return 新建对象，调用方用 XLockFile_delete 释放；失败返回 NULL。
 */
XLockFile* XLockFile_create(const XString* fileName);

/** @brief XLockFile_create 的 UTF-8 路径便捷版本。 */
XLockFile* XLockFile_create_utf8(const char* fileNameUtf8);

/**
 * @brief 反初始化（对标 QLockFile::~QLockFile）。
 * @details 若此时仍持有锁，会先解锁（删除锁文件），再释放私有数据。
 */
void XLockFile_deinit(XLockFile* self);

/** @brief 反初始化并释放堆对象（对标 delete）。 */
void XLockFile_delete(XLockFile* self);

/* ==================== 加锁 / 解锁 ==================== */

/**
 * @brief 取得锁，必要时无限等待（对标 QLockFile::lock）。
 * @return 取得锁返回 true；不可恢复错误（无权限等）返回 false。
 * @note 同一对象重复 lock 而不 unlock 会死锁（Qt 同款约定）。
 */
bool XLockFile_lock(XLockFile* self);

/**
 * @brief 尝试取得锁，最多等 timeoutMsecs 毫秒（对标 QLockFile::tryLock）。
 * @param timeoutMsecs 等待上限；负值 = 无限等待（等价 lock）。
 * @return 取得锁返回 true。
 */
bool XLockFile_tryLock(XLockFile* self, int timeoutMsecs);

/**
 * @brief 释放锁（删除锁文件）（对标 QLockFile::unlock）。
 * @details 未持锁时为空操作。
 */
void XLockFile_unlock(XLockFile* self);

/**
 * @brief 当前对象是否持有锁（对标 QLockFile::isLocked）。
 */
bool XLockFile_isLocked(const XLockFile* self);

/**
 * @brief 最后一次加锁的错误码（对标 QLockFile::error）。
 */
XLockFile_LockError XLockFile_error(const XLockFile* self);

/* ==================== 诊断 ==================== */

/**
 * @brief 锁文件路径（对标 QLockFile::fileName）。
 * @return 借用指针，对象存活期间有效；self 非法返回 NULL。
 */
const XString* XLockFile_fileName_const(const XLockFile* self);

/**
 * @brief 读取当前锁持有者信息（对标 QLockFile::getLockInfo）。
 * @param pidOut 输出 PID；可为 NULL。
 * @param hostnameOut 输出主机名（新建 XString，调用方释放）；可为 NULL。
 * @param appnameOut 输出进程名（新建 XString，调用方释放）；可为 NULL。
 * @return 成功读取返回 true；锁文件不存在或内容不合法返回 false。
 */
bool XLockFile_getLockInfo(const XLockFile* self, int64_t* pidOut,
                           XString** hostnameOut, XString** appnameOut);

/**
 * @brief 强制删除锁文件（对标 QLockFile::removeStaleLockFile）。
 * @details 仅在未持锁时可调用；用于长持锁场景下用户确认清理。
 * @return 删除成功返回 true。
 */
bool XLockFile_removeStaleLockFile(XLockFile* self);

/* ==================== 僵死判定配置 ==================== */

/**
 * @brief 设置锁文件被视为僵死的年龄阈值（对标 QLockFile::setStaleLockTime）。
 * @param staleMsecs 毫秒；0 = 不按年龄判僵死（长持锁场景推荐）。
 * @note 默认 30000（30 秒）。
 */
void XLockFile_setStaleLockTime(XLockFile* self, int staleMsecs);

/** @brief 当前僵死年龄阈值（毫秒）（对标 QLockFile::staleLockTime）。 */
int XLockFile_staleLockTime(const XLockFile* self);

/**
 * @brief 当前平台是否支持进程间文件锁。
 * @details 无 OS 的裸机/RTOS 返回 false；此时 lock 恒失败，调用方应降级。
 */
bool XLockFile_isSupported(void);

#endif /* XLOCKFILE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XLOCKFILE_H */
