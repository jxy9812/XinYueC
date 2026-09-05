/**
 * @file XProcessTest.h
 * @brief XProcess 公开 API 的 Qt 6.8 行为回归测试入口。
 * @details
 * 测试只通过 XProcess、XProcessEnvironment、XString 和 XByteArray 公共 API
 * 驱动，不访问 Drive 后端结构。入口可挂到 XTestMenu，也可由非交互测试主程序调用。
 */

#ifndef XPROCESS_TEST_H
#define XPROCESS_TEST_H

#include <stdbool.h>
#include "XTestMenu.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 运行 XProcess 生命周期、环境、I/O、重定向、失败和 detached 测试。 */
bool XProcessTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XPROCESS_TEST_H */
