/**
 * @file XSqlMySqlClient_posix.c
 * @brief POSIX MySQL 客户端特殊传输的默认实现。
 */
#include "XSqlMySqlClient_platform.h"

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__) \
    || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

XSqlMySqlSharedMemory* XSqlMySqlSharedMemory_open(const char* baseName, int timeoutMs)
{
    (void)baseName;
    (void)timeoutMs;
    return NULL;
}

void XSqlMySqlSharedMemory_close(XSqlMySqlSharedMemory* shared)
{
    (void)shared;
}

bool XSqlMySqlSharedMemory_read(XSqlMySqlSharedMemory* shared, void* data,
                                size_t size, int timeoutMs)
{
    (void)shared;
    (void)data;
    (void)size;
    (void)timeoutMs;
    return false;
}

bool XSqlMySqlSharedMemory_write(XSqlMySqlSharedMemory* shared, const void* data,
                                 size_t size, int timeoutMs)
{
    (void)shared;
    (void)data;
    (void)size;
    (void)timeoutMs;
    return false;
}

#endif
