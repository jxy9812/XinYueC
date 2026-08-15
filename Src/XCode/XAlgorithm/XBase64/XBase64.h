#include"CXinYueConfig.h"
#if !defined(XBASE64_H)&& XBase64_ON
#define XBASE64_H

/**
 * @file XBase64.h
 * @brief Base64 编码和解码 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 计算 Base64 编码结果所需的缓冲区大小。
 * @param input_len 输入二进制数据的字节数。
 * @return 编码结果所需字节数，包括字符串终止符 '\0'。
 * @note 返回值适用于 XBase64_encode 的 output 缓冲区。
 */
size_t XBase64_encoded_size(size_t input_len);

/**
 * @brief 计算 Base64 解码结果所需的最大字节数。
 * @param input 输入 Base64 字符串；当 input_len 大于 0 时不能为 NULL。
 * @param input_len 输入字符串长度；不要求包含 '\0'。
 * @return 根据长度和末尾填充符估算的解码字节数；input_len 为 0 时返回 0。
 * @note 函数只根据长度和填充符计算大小，不负责完整校验输入格式。
 */
size_t XBase64_decoded_size(const char* input, size_t input_len);

/**
 * @brief 将二进制数据编码为 Base64 字符串。
 * @param input 输入二进制数据；input_len 为 0 时仍应传入有效地址。
 * @param input_len 输入数据的字节数。
 * @param output 输出缓冲区，成功时写入以 '\0' 结尾的 Base64 字符串。
 * @param output_len 入参为 output 容量，出参更新为实际写入字节数，包含 '\0'。
 * @return 成功返回 0；参数无效返回 -1；缓冲区不足返回 -2。
 */
int XBase64_encode(const uint8_t* input, size_t input_len, char* output, size_t* output_len);

/**
 * @brief 将 Base64 字符串解码为二进制数据。
 * @param input 输入 Base64 字符串；input_len 大于 0 时不能为 NULL。
 * @param input_len 输入字符数；不要求包含 '\0'。
 * @param output 输出缓冲区，用于存储解码后的二进制数据。
 * @param output_len 入参为 output 容量，出参更新为实际写入字节数，不含终止符。
 * @return 成功返回 0；参数无效返回 -1；缓冲区不足返回 -2；包含非法字符返回 -3。
 */
int XBase64_decode(const char* input, size_t input_len, uint8_t* output, size_t* output_len);

#ifdef __cplusplus
}
#endif
#endif /* XBASE64_H */
