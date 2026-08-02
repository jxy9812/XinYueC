/**
 * @file XSqlMySqlClient_platform.h
 * @brief MySQL 客户端平台传输抽象接口。
 *
 * 本文件只声明 MySQL 协议层需要、但由操作系统提供的特殊传输能力。
 * 具体平台句柄保持不透明，避免 Src/XPlatform 和 Src/XCode/XSql 依赖
 * Windows、POSIX 或其他系统的头文件。
 */
#ifndef XSQL_MYSQL_CLIENT_PLATFORM_H
#define XSQL_MYSQL_CLIENT_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** MySQL 本地共享内存传输对象，具体布局由平台实现负责。 */
typedef struct XSqlMySqlSharedMemory XSqlMySqlSharedMemory;

/**
 * @brief 打开 MySQL 共享内存传输。
 * @param baseName MySQL 共享内存基础名称，借用，不能为 NULL。
 * @param timeoutMs 建立连接的等待时间（毫秒），负数表示不限制。
 * @return 成功返回传输对象；当前平台不支持或打开失败返回 NULL。
 */
XSqlMySqlSharedMemory* XSqlMySqlSharedMemory_open(const char* baseName, int timeoutMs);

/**
 * @brief 关闭 MySQL 共享内存传输并释放对象。
 * @param shared 传输对象，可为 NULL。
 */
void XSqlMySqlSharedMemory_close(XSqlMySqlSharedMemory* shared);

/**
 * @brief 从 MySQL 共享内存传输读取指定长度的数据。
 * @param shared 传输对象，不能为 NULL。
 * @param data 输出缓冲区，不能为 NULL。
 * @param size 要读取的字节数。
 * @param timeoutMs 单次等待时间（毫秒），负数表示不限制。
 * @return 成功返回 true；连接关闭、超时或平台错误返回 false。
 */
bool XSqlMySqlSharedMemory_read(XSqlMySqlSharedMemory* shared, void* data,
                                size_t size, int timeoutMs);

/**
 * @brief 向 MySQL 共享内存传输写入指定长度的数据。
 * @param shared 传输对象，不能为 NULL。
 * @param data 输入缓冲区，不能为 NULL。
 * @param size 要写入的字节数。
 * @param timeoutMs 单次等待时间（毫秒），负数表示不限制。
 * @return 成功返回 true；连接关闭、超时或平台错误返回 false。
 */
bool XSqlMySqlSharedMemory_write(XSqlMySqlSharedMemory* shared, const void* data,
                                 size_t size, int timeoutMs);

#ifdef __cplusplus
}
#endif

#endif /* XSQL_MYSQL_CLIENT_PLATFORM_H */
