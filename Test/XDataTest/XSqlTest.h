/**
 * @file       XSqlTest.h
 * @brief      SQL 抽象驱动和公共类回归测试声明。
 * @details    测试只使用进程内 SQLite 和公共 XSql API；不读取远程连接配置。
 */
#ifndef XSQLTEST_H
#define XSQLTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行 SQL 公共 API 与 SQLite 回归测试。
 * @return 所有断言通过返回 0；任一断言、初始化或内存分配失败返回非 0。
 * @note 本函数创建并清理测试临时资源，不转移任何对象所有权给调用方。
 */
int XSqlTest_run(void);

#ifdef __cplusplus
}
#endif

#endif /* XSQLTEST_H */
