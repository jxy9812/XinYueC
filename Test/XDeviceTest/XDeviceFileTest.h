#ifndef XDEVICEFILETEST_H
#define XDEVICEFILETEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行 XDeviceFile 平台 API 模式回归测试。
 * @return 全部检查通过或当前配置不适用时返回 true，否则返回 false。
 */
bool XDeviceFileTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICEFILETEST_H */
