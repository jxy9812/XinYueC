#ifndef XDEVICENETWORKTEST_H
#define XDEVICENETWORKTEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 运行 XDeviceNetwork 的 UDP 绑定、属性和控制回归测试。 */
bool XDeviceNetworkTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICENETWORKTEST_H */
