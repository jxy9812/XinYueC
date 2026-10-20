/** @file       XDdnsAliyun.h
 * @brief      阿里云解析厂商实现的公开入口（操作表经 XDdnsProvider_find 获取）。
 * @details    本头文件仅暴露厂商名常量；操作表通过 XDdnsProvider_find("aliyun")
 *             获取。实现细节见 XDdnsAliyun.c 文件头（POP 签名流程）。
 *             受 XDDNS_ON && XDDNS_ALIYUN_ON 开关裁剪（见 XDdns_config.h）。
 */
#ifndef XDDNSALIYUN_H
#define XDDNSALIYUN_H

#ifdef __cplusplus
extern "C" {
#endif

/* include config before #if */
#include "XDdns_config.h"

#if XDDNS_ON && XDDNS_ALIYUN_ON

#include "XDdnsProvider.h"

/** @brief 阿里云厂商名（XDdnsProvider_find 的查找键 / setProvider 的取值）。 */
#define XDDNS_PROVIDER_ALIYUN "aliyun"

/**
 * @brief  获取阿里云厂商操作表。
 * @return 静态存储期操作表指针（不释放）。
 * @note   一般经 XDdnsProvider_find("aliyun") 间接获取；本入口供查找表登记。
 */
const XDdnsProviderOps* XDdnsProvider_aliyun(void);

#endif /* XDDNS_ON && XDDNS_ALIYUN_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDDNSALIYUN_H */
