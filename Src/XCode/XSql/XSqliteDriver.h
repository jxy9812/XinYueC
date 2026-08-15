/**
 * @file       XSqliteDriver.h
 * @brief      SQLite 源码驱动工厂。
 * @details    该头文件仅声明 XinYueC 内置 SQLite 驱动的创建和静态注册接口，
 *             不暴露 sqlite3 的第三方声明或平台相关 API。
 */
#ifndef XSQLITEDRIVER_H
#define XSQLITEDRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XSqlDriver.h"

/**
 * @brief 创建 SQLite 源码驱动。
 * @return 新驱动对象，调用者取得所有权并必须使用 XSqlDriver_delete_base 释放；失败返回 NULL。
 */
XSqlDriver* XSqliteDriver_create_ex(XMemoryType memory);

/**
 * @brief 注册 SQLite 内置源码驱动。
 * @return 注册成功返回 true；内存不足或注册失败返回 false。
 * @note 注册表接管创建器；该函数可重复调用。
 */
bool XSqliteDriver_register(void);

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSqliteDriver_create
#define XSqliteDriver_create(...) XSqliteDriver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XSQLITEDRIVER_H */
