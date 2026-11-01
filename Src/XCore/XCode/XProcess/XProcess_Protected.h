/**
 * @file XProcess_Protected.h
 * @brief XProcess 实现与 Drive 后端之间的内部契约。
 * @details
 * 本头文件不是 Qt 私有头文件，也不属于安装后的公共 API。它只声明 XinYueC
 * XProcess 实现需要的后端钩子；平台后端必须通过这些函数回写 XProcess 的
 * 状态、缓冲和错误，不能让公共层直接包含 POSIX、Win32 或 RTOS 头文件。
 */

#ifndef XPROCESS_PROTECTED_H
#define XPROCESS_PROTECTED_H

#include "XProcess.h"

#if XProcess_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 后端私有句柄释放。
 * @param self 进程对象；不能为空，调用后 m_backend 必须为 NULL。
 */
void XProcess_backend_deinit(XProcess* self);
/** @brief 后端启动已配置程序。
 * @param self 进程对象；配置已完成且不能处于运行状态。
 * @param mode XIODevice 打开模式。
 * @param detached 是否分离进程并立即返回。
 * @return 启动成功返回 true；失败时必须设置 XProcess 错误。
 */
bool XProcess_backend_start(XProcess* self, XIODeviceBaseMode mode, bool detached);
/** @brief 后端轮询管道、进程状态和输出缓冲。
 * @param self 进程对象；不能为空。
 * @param timeoutMsecs 等待毫秒数，负数表示后端默认等待。
 * @return 状态仍可继续轮询返回 true，后端不可用返回 false。
 */
bool XProcess_backend_poll(XProcess* self, int timeoutMsecs);
/** @brief 后端返回指定输出通道当前缓冲区可读字节数。
 * @param self 进程对象；不能为空。
 * @param channel stdout 或 stderr 通道。
 * @return 可读取字节数，失败返回 0。
 */
int64_t XProcess_backend_bytesAvailable(const XProcess* self, XProcessChannel channel);
/** @brief 后端返回标准输入待写字节数。
 * @param self 进程对象；不能为空。
 * @return 待写字节数，失败返回 0。
 */
int64_t XProcess_backend_bytesToWrite(const XProcess* self);
/** @brief 后端关闭指定输出管道并保留已有缓冲。
 * @param self 进程对象；不能为空。
 * @param channel 要关闭的 stdout 或 stderr 通道。
 */
void XProcess_backend_closeReadChannel(XProcess* self, XProcessChannel channel);
/** @brief 后端关闭标准输入管道。
 * @param self 进程对象；不能为空。
 */
void XProcess_backend_closeWriteChannel(XProcess* self);
/** @brief 后端通知 XProcess 进程已经退出并交付退出结果。
 * @param self 进程对象；不能为空。
 * @param exitCode 平台退出码。
 * @param status 正常退出或崩溃状态。
 * @param crashed 是否由异常终止。
 */
void XProcess_backend_notifyFinished(XProcess* self, int exitCode,
                                     XProcessExitStatus status,
                                     bool crashed);
/** @brief 后端把平台错误转换为 XProcessError 并写入错误文本。
 * @param self 进程对象；不能为空。
 * @param error 统一错误枚举。
 * @param text UTF-8 错误文本；调用期间借用，可为 NULL。
 */
void XProcess_backend_setError(XProcess* self, XProcessError error,
                               const char* text);
/** @brief 后端读取当前 stdout/stderr 到 XIODevice 缓冲。
 * @param self 进程对象；不能为空。
 * @param channel 要读取的通道。
 * @param data 输出缓冲；容量为 maxlen。
 * @param maxlen 输出缓冲容量，不能为负数。
 * @return 实际读取字节数，失败返回 -1。
 */
int64_t XProcess_backend_read(XProcess* self, XProcessChannel channel,
                              char* data, int64_t maxlen);
/** @brief 后端写入子进程 stdin。
 * @param self 进程对象；不能为空。
 * @param data 输入数据；len 非零时不能为空。
 * @param len 输入字节数。
 * @return 实际写入字节数，失败返回 -1。
 */
int64_t XProcess_backend_write(XProcess* self, const char* data, int64_t len);
/** @brief 后端等待输入写缓冲排空。
 * @param self 进程对象；不能为空。
 * @param msecs 等待毫秒数，-1 表示无限等待。
 * @return 缓冲排空返回 true，超时或失败返回 false。
 */
bool XProcess_backend_waitForBytesWritten(XProcess* self, int msecs);
/** @brief 后端请求正常终止。
 * @param self 进程对象；不能为空。
 */
void XProcess_backend_terminate(XProcess* self);
/** @brief 后端强制终止。
 * @param self 进程对象；不能为空。
 */
void XProcess_backend_kill(XProcess* self);
/** @brief 后端静态分离启动。
 * @param program 程序名；调用期间借用。
 * @param arguments 参数列表；调用期间借用，可为 NULL。
 * @param workingDirectory 工作目录；调用期间借用，可为 NULL。
 * @param pid 输出进程 ID；可为 NULL。
 * @return 启动成功返回 true。
 */
bool XProcess_backend_startDetached(const XString* program,
                                    const XStringList* arguments,
                                    const XString* workingDirectory,
                                    XProcessId* pid);
/** @brief 后端返回当前平台空设备路径。
 * @return 新 XString；调用方必须释放，失败返回 NULL。
 */
XString* XProcess_backend_nullDevice(void);
/** @brief 后端返回系统环境列表。
 * @return 新 XStringList；调用方必须释放，失败返回 NULL。
 */
XStringList* XProcess_backend_systemEnvironment(void);

#ifdef __cplusplus
}
#endif

#endif /* XProcess_ON */
#endif /* XPROCESS_PROTECTED_H */
