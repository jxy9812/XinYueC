/** @file XProtocolIo.h
 * @brief XProtocol 协议栈通用 I/O 与结果类型。
 * @details
 * 为 XProtocol 下各协议栈（SSH/Telnet 等）提供统一的宿主接口类型，
 * 通过 XObject 信号槽 + 公共方法 + setter 回填实现双向交互，
 * 避免协议栈直接依赖 Shell 或平台传输实现。
 * 所有协议栈源文件统一使用 UTF-8 带 BOM 编码。
 */

#ifndef XPROTOCOL_IO_H
#define XPROTOCOL_IO_H

#include "XProtocol_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** @brief 不透明 Shell 类型；宿主可强转为具体 Shell 实现。 */
typedef struct XProtocolShell XProtocolShell;

/** @brief 不透明会话类型；宿主可强转为具体会话实现。 */
typedef struct XProtocolSession XProtocolSession;

/** @brief 不透明命令/请求类型；用于扩展命令或协议请求。 */
typedef struct XProtocolCommand XProtocolCommand;

/**
 * @brief 协议处理结果。
 * @details 与 XConsoleResult 保持一致的语义，供协议栈状态机返回。
 */
typedef enum XProtocolResult {
    XProtocolResult_Ok = 0,             /**< 成功 */
    XProtocolResult_MoreOutput = 1,     /**< 还有更多输出待处理 */
    XProtocolResult_InvalidArgument = -1, /**< 参数无效 */
    XProtocolResult_UnknownCommand = -2,  /**< 未知命令 */
    XProtocolResult_InvalidSyntax = -3,   /**< 语法错误 */
    XProtocolResult_PermissionDenied = -4,/**< 权限不足 */
    XProtocolResult_NotSupported = -5,    /**< 不支持 */
    XProtocolResult_ResourceLimit = -6,   /**< 资源受限 */
    XProtocolResult_Cancelled = -7,       /**< 已取消 */
    XProtocolResult_IoError = -8,         /**< 传输错误 */
    XProtocolResult_Failed = -9           /**< 一般失败 */
} XProtocolResult;

#ifdef __cplusplus
}
#endif

#endif /* XPROTOCOL_IO_H */