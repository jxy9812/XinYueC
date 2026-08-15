#ifndef XNUMSTRCONV_H
#define XNUMSTRCONV_H

/**
 * @file XNumStrConv.h
 * @brief 整数和浮点数与十进制字符串之间的转换 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 转换状态枚举，用于指示转换操作的结果
 */
typedef enum {
    CONV_OK = 0,            /**< 转换成功。 */
    CONV_NULL_INPUT,        /**< 输入字符串或输入地址为空。 */
    CONV_NULL_OUTPUT,       /**< 输出缓冲区或结果地址为空。 */
    CONV_BUFFER_TOO_SMALL,  /**< 输出缓冲区不足以容纳结果和 '\0'。 */
    CONV_INVALID_CHAR,      /**< 字符串中包含不允许的字符。 */
    CONV_OVERFLOW,          /**< 转换结果超出目标类型的表示范围。 */
    CONV_UNDERFLOW,         /**< 转换结果低于目标类型的可表示精度范围。 */
    CONV_INVALID_FORMAT     /**< 字符串格式不符合目标数值类型要求。 */
} ConvStatus;

/**
 * @brief 将int64_t类型整数转换为字符串
 * @param num 要转换的整数
 * @param buf 存储转换结果的字符缓冲区，成功时以 '\0' 结尾。
 * @param buf_size 缓冲区容量，单位为字节，包括终止符 '\0'。
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus int64_to_str(int64_t num, char* buf, size_t buf_size);

/**
 * @brief 将uint64_t类型无符号整数转换为字符串
 * @param num 要转换的无符号整数
 * @param buf 存储转换结果的字符缓冲区，成功时以 '\0' 结尾。
 * @param buf_size 缓冲区容量，单位为字节，包括终止符 '\0'。
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus uint64_to_str(uint64_t num, char* buf, size_t buf_size);

/**
 * @brief 将int32_t类型整数转换为字符串
 * @param num 要转换的整数
 * @param buf 存储转换结果的字符缓冲区，成功时以 '\0' 结尾。
 * @param buf_size 缓冲区容量，单位为字节，包括终止符 '\0'。
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus int32_to_str(int32_t num, char* buf, size_t buf_size);

/**
 * @brief 将uint32_t类型无符号整数转换为字符串
 * @param num 要转换的无符号整数
 * @param buf 存储转换结果的字符缓冲区，成功时以 '\0' 结尾。
 * @param buf_size 缓冲区容量，单位为字节，包括终止符 '\0'。
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus uint32_to_str(uint32_t num, char* buf, size_t buf_size);

/**
 * @brief 将字符串转换为int64_t类型整数
 * @param str 以 '\0' 结尾的十进制字符串，可带一个前导 '+' 或 '-'。
 * @param result 输出转换结果的地址。
 * @return 转换状态；成功返回 CONV_OK，格式非法或数值溢出时返回对应错误。
 */
ConvStatus str_to_int64(const char* str, int64_t* result);

/**
 * @brief 将字符串转换为uint64_t类型无符号整数
 * @param str 以 '\0' 结尾的十进制字符串，可带一个前导 '+'，不允许负号。
 * @param result 输出转换结果的地址。
 * @return 转换状态；成功返回 CONV_OK，格式非法或数值溢出时返回对应错误。
 */
ConvStatus str_to_uint64(const char* str, uint64_t* result);

/**
 * @brief 将字符串转换为int32_t类型整数
 * @param str 以 '\0' 结尾的十进制字符串。
 * @param result 输出转换结果的地址。
 * @return 转换状态；超出 int32_t 范围返回 CONV_OVERFLOW。
 */
ConvStatus str_to_int32(const char* str, int32_t* result);

/**
 * @brief 将字符串转换为uint32_t类型无符号整数
 * @param str 以 '\0' 结尾的十进制字符串。
 * @param result 输出转换结果的地址。
 * @return 转换状态；超出 uint32_t 范围返回 CONV_OVERFLOW。
 */
ConvStatus str_to_uint32(const char* str, uint32_t* result);

/**
 * @brief 将float类型浮点数转换为字符串
 * @param num 要转换的浮点数
 * @param buf 存储转换结果的字符缓冲区，成功时以 '\0' 结尾。
 * @param buf_size 缓冲区容量，单位为字节，包括终止符 '\0'。
 * @param precision 非负时为小数位数（最多 9 位）；负数启用自动格式。
 * @return 转换状态；NaN 和 Infinity 也会转换为对应文本。
 */
ConvStatus float_to_str(float num, char* buf, size_t buf_size, int precision);

/**
 * @brief 将double类型浮点数转换为字符串
 * @param num 要转换的浮点数
 * @param buf 存储转换结果的字符缓冲区，成功时以 '\0' 结尾。
 * @param buf_size 缓冲区容量，单位为字节，包括终止符 '\0'。
 * @param precision 非负时为小数位数（最多 15 位）；负数启用自动格式。
 * @return 转换状态；NaN 和 Infinity 也会转换为对应文本。
 */
ConvStatus double_to_str(double num, char* buf, size_t buf_size, int precision);

/**
 * @brief 将字符串转换为float类型浮点数
 * @param str 以 '\0' 结尾的十进制字符串，可带符号和小数部分，也支持 NaN/Infinity。
 * @param result 输出转换结果的地址。
 * @return 转换状态；超出范围或发生下溢时返回对应错误。
 */
ConvStatus str_to_float(const char* str, float* result);

/**
 * @brief 将字符串转换为double类型浮点数
 * @param str 以 '\0' 结尾的十进制字符串，可带符号和小数部分，也支持 NaN/Infinity。
 * @param result 输出转换结果的地址。
 * @return 转换状态；超出范围或发生下溢时返回对应错误。
 */
ConvStatus str_to_double(const char* str, double* result);

/**
 * @brief 计算存储int64_t类型整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的整数
 * @return 所需缓冲区大小，单位为字节，包括 '\0'。
 */
size_t int64_required_buf_size(int64_t num);

/**
 * @brief 计算存储uint64_t类型无符号整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的无符号整数
 * @return 所需缓冲区大小，单位为字节，包括 '\0'。
 */
size_t uint64_required_buf_size(uint64_t num);

/**
 * @brief 计算存储int32_t类型整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的整数
 * @return 所需缓冲区大小，单位为字节，包括 '\0'。
 */
size_t int32_required_buf_size(int32_t num);

/**
 * @brief 计算存储uint32_t类型无符号整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的无符号整数
 * @return 所需缓冲区大小，单位为字节，包括 '\0'。
 */
size_t uint32_required_buf_size(uint32_t num);

/**
 * @brief 计算存储float类型浮点数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的浮点数
 * @param precision 固定格式下的小数位数；负数按 0 位估算。
 * @return 所需缓冲区大小，单位为字节，包括 '\0'。
 */
size_t float_required_buf_size(float num, int precision);

/**
 * @brief 计算存储double类型浮点数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的浮点数
 * @param precision 固定格式下的小数位数；负数按 0 位估算。
 * @return 所需缓冲区大小，单位为字节，包括 '\0'。
 */
size_t double_required_buf_size(double num, int precision);
#ifdef __cplusplus
}
#endif
#endif// !XALGORITHM_H

