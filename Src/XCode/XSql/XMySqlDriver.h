/**
 * @file       XMySqlDriver.h
 * @brief      MySQL/MariaDB 源码驱动。
 * @details    驱动通过 XSqlMySqlClientApi 访问客户端实现，公共头文件不
 *             暴露第三方客户端类型；默认客户端使用 XinYueC 网络抽象。
 */
#ifndef XMYSQLDRIVER_H
#define XMYSQLDRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlDriver.h"
#include "XSqlMySqlClient.h"

/**
 * @brief 创建 MySQL/MariaDB 源码驱动。
 * @return 新驱动对象，调用者取得所有权并必须使用 XSqlDriver_delete_base
 *         释放；内存不足或默认客户端不可用时返回 NULL。
 */
XSqlDriver* XMySqlDriver_create(void);

/**
 * @brief 注册 MySQL 内置源码驱动。
 * @return 注册成功返回 true；内存不足或注册失败返回 false。
 * @note 注册表接管创建器；该函数可重复调用。
 */
bool XMySqlDriver_register(void);

/**
 * @brief 设置 MySQL 客户端实现函数表。
 * @param api 客户端函数表，借用，必须包含驱动实际使用的全部函数；传入
 *            NULL 恢复默认实现。
 * @return 设置成功返回 true；参数不完整返回 false。
 * @note 应在创建 MySQL 连接前调用；已有连接仍使用创建时的函数表。
 */
bool XMySqlDriver_setClientApi(const XSqlMySqlClientApi* api);

/**
 * @brief 获取当前 MySQL 客户端实现函数表。
 * @return 进程内共享函数表；调用方只可读取，不得释放或修改。
 */
const XSqlMySqlClientApi* XMySqlDriver_clientApi(void);

#ifdef __cplusplus
}
#endif

#endif /* XMYSQLDRIVER_H */
