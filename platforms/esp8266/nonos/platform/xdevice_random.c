/**
 * @file xdevice_random.c
 * @brief 安全随机数填充的平台实现（ESP8266 硬件 RNG）
 *
 * @details `XRandomGenerator.c` 以外部符号方式要求平台提供
 *          `XRandomGenerator_platformFillSecure()`（注释里写的是由
 *          XRandomGenerator_platform.c 实现，但仓库里没有该文件——桌面
 *          平台一直没链接到这条依赖，ESP8266 一旦把 XRandom 拉进链接就会
 *          报 undefined reference）。
 *
 *          ESP8266 有硬件随机数发生器，寄存器 0x3FF20E44（RNG_DATA），
 *          每次读返回一个 32 位随机字；上电未就绪时前几个值可能为 0，
 *          这里用多次采样 + 与计数器异或做最低限度的去偏。
 *
 * @note 返回 false 时调用方会退回软件 PRNG 播种路径，不会崩。
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESP8266_RNG_DATA (*(volatile uint32_t*)0x3FF20E44u)

bool XRandomGenerator_platformFillSecure(void* buffer, size_t size)
{
    uint8_t* out = (uint8_t*)buffer;
    uint32_t acc;
    size_t i;
    unsigned guard = 0;

    if (!buffer || size == 0)
        return false;

    /* 等 RNG 出第一个非零样本（上电早期可能读到 0）。 */
    do {
        acc = ESP8266_RNG_DATA;
    } while (acc == 0 && ++guard < 1000u);

    if (acc == 0)
        return false;   /* 硬件 RNG 不可用，交给调用方的软件路径 */

    for (i = 0; i < size; ++i) {
        if ((i & 3u) == 0u) {
            acc ^= ESP8266_RNG_DATA + (uint32_t)i * 2654435761u;
            acc ^= acc << 13;
            acc ^= acc >> 17;
            acc ^= acc << 5;
        }
        out[i] = (uint8_t)(acc >> ((i & 3u) * 8u));
    }
    return true;
}
