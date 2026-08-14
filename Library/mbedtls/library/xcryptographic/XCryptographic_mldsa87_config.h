/**
 * @file XCryptographic_mldsa87_config.h
 * @brief mldsa-native 在 XCryptographic 单编译单元中的适配配置。
 */

#ifndef XCRYPTOGRAPHIC_MLDSA87_CONFIG_H
#define XCRYPTOGRAPHIC_MLDSA87_CONFIG_H

#include <stddef.h>
#include <stdint.h>

/* mldsa-native 的中间缓冲区必须在返回前清零。 */
#define MLD_CONFIG_CUSTOM_ZEROIZE
static void XCryptographic_mldsa87Zeroize(void* data, size_t size)
{
    volatile uint8_t* bytes = (volatile uint8_t*)data;
    while (bytes && size-- != 0u) {
        *bytes++ = 0u;
    }
}
#define mld_zeroize XCryptographic_mldsa87Zeroize

/* XCryptographic 只暴露自己的包装函数，不泄漏 mldsa-native 符号。 */
#define MLD_CONFIG_EXTERNAL_API_QUALIFIER static
#define MLD_CONFIG_INTERNAL_API_QUALIFIER static
#define MLD_CONFIG_NO_RANDOMIZED_API
#define MLD_CONFIG_NO_SUPERCOP
#define MLD_CONFIG_NAMESPACE_PREFIX XCryptographic_mldsa87

#endif /* XCRYPTOGRAPHIC_MLDSA87_CONFIG_H */
