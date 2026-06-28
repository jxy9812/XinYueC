#ifndef XNUMSTRCONV_H
#define XNUMSTRCONV_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/**
 * @brief 转换状态枚举，用于指示转换操作的结果
 */
typedef enum {
    CONV_OK = 0,               // 转换成功
    CONV_NULL_INPUT,           // 输入字符串为空指针
    CONV_NULL_OUTPUT,          // 输出缓冲区为空指针
    CONV_BUFFER_TOO_SMALL,     // 输出缓冲区大小不足
    CONV_INVALID_CHAR,         // 字符串中包含无效字符
    CONV_OVERFLOW,             // 转换结果超出目标类型的最大值
    CONV_UNDERFLOW,            // 转换结果小于目标类型的最小值（仅适用于有符号类型和浮点数）
    CONV_INVALID_FORMAT        // 字符串格式不符合数值类型要求
} ConvStatus;

/**
 * @brief 将int64_t类型整数转换为字符串
 * @param num 要转换的整数
 * @param buf 存储转换结果的缓冲区
 * @param buf_size 缓冲区大小（包括终止符'\0'）
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus int64_to_str(int64_t num, char* buf, size_t buf_size);

/**
 * @brief 将uint64_t类型无符号整数转换为字符串
 * @param num 要转换的无符号整数
 * @param buf 存储转换结果的缓冲区
 * @param buf_size 缓冲区大小（包括终止符'\0'）
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus uint64_to_str(uint64_t num, char* buf, size_t buf_size);

/**
 * @brief 将int32_t类型整数转换为字符串
 * @param num 要转换的整数
 * @param buf 存储转换结果的缓冲区
 * @param buf_size 缓冲区大小（包括终止符'\0'）
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus int32_to_str(int32_t num, char* buf, size_t buf_size);

/**
 * @brief 将uint32_t类型无符号整数转换为字符串
 * @param num 要转换的无符号整数
 * @param buf 存储转换结果的缓冲区
 * @param buf_size 缓冲区大小（包括终止符'\0'）
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus uint32_to_str(uint32_t num, char* buf, size_t buf_size);

/**
 * @brief 将字符串转换为int64_t类型整数
 * @param str 要转换的字符串
 * @param result 存储转换结果的指针
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus str_to_int64(const char* str, int64_t* result);

/**
 * @brief 将字符串转换为uint64_t类型无符号整数
 * @param str 要转换的字符串
 * @param result 存储转换结果的指针
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus str_to_uint64(const char* str, uint64_t* result);

/**
 * @brief 将字符串转换为int32_t类型整数
 * @param str 要转换的字符串
 * @param result 存储转换结果的指针
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus str_to_int32(const char* str, int32_t* result);

/**
 * @brief 将字符串转换为uint32_t类型无符号整数
 * @param str 要转换的字符串
 * @param result 存储转换结果的指针
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus str_to_uint32(const char* str, uint32_t* result);

/**
 * @brief 将float类型浮点数转换为字符串
 * @param num 要转换的浮点数
 * @param buf 存储转换结果的缓冲区
 * @param buf_size 缓冲区大小（包括终止符'\0'）
 * @param precision 小数部分保留的位数（0-9）
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus float_to_str(float num, char* buf, size_t buf_size, int precision);

/**
 * @brief 将double类型浮点数转换为字符串
 * @param num 要转换的浮点数
 * @param buf 存储转换结果的缓冲区
 * @param buf_size 缓冲区大小（包括终止符'\0'）
 * @param precision 小数部分保留的位数（0-9）
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus double_to_str(double num, char* buf, size_t buf_size, int precision);

/**
 * @brief 将字符串转换为float类型浮点数
 * @param str 要转换的字符串
 * @param result 存储转换结果的指针
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus str_to_float(const char* str, float* result);

/**
 * @brief 将字符串转换为double类型浮点数
 * @param str 要转换的字符串
 * @param result 存储转换结果的指针
 * @return 转换状态，CONV_OK表示成功
 */
ConvStatus str_to_double(const char* str, double* result);

/**
 * @brief 计算存储int64_t类型整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的整数
 * @return 所需缓冲区大小
 */
size_t int64_required_buf_size(int64_t num);

/**
 * @brief 计算存储uint64_t类型无符号整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的无符号整数
 * @return 所需缓冲区大小
 */
size_t uint64_required_buf_size(uint64_t num);

/**
 * @brief 计算存储int32_t类型整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的整数
 * @return 所需缓冲区大小
 */
size_t int32_required_buf_size(int32_t num);

/**
 * @brief 计算存储uint32_t类型无符号整数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的无符号整数
 * @return 所需缓冲区大小
 */
size_t uint32_required_buf_size(uint32_t num);

/**
 * @brief 计算存储float类型浮点数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的浮点数
 * @param precision 小数部分保留的位数
 * @return 所需缓冲区大小
 */
size_t float_required_buf_size(float num, int precision);

/**
 * @brief 计算存储double类型浮点数所需的最小缓冲区大小（包括终止符）
 * @param num 要计算的浮点数
 * @param precision 小数部分保留的位数
 * @return 所需缓冲区大小
 */
size_t double_required_buf_size(double num, int precision);
#ifdef __cplusplus
}
#endif
#endif// !XALGORITHM_H

