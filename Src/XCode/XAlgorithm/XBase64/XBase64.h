#include"CXinYueConfig.h"
#if !defined(XBASE64_H)&& XBase64_ON
#define XBASE64_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "XTypes.h"
/**
 * 计算Base64编码后的最大长度
 * @param input_len 输入数据的长度
 * @return Base64编码后的最大长度（包括终止符）
 */
size_t XBase64_encoded_size(size_t input_len);

/**
 * 计算Base64解码后的最大长度
 * @param input_len 输入Base64字符串的长度
 * @return 解码后的最大长度
 */
size_t XBase64_decoded_size(const char* input, size_t input_len);

/**
 * 将二进制数据编码为Base64字符串
 * @param input 输入的二进制数据
 * @param input_len 输入数据的长度
 * @param output 输出缓冲区，用于存储Base64编码结果
 * @param output_len 输出缓冲区的长度，函数返回时更新为实际写入的长度
 * @return 成功返回0，失败返回非0
 */
int XBase64_encode(const uint8_t* input, size_t input_len, char* output, size_t* output_len);

/**
 * 将Base64字符串解码为二进制数据
 * @param input 输入的Base64字符串
 * @param input_len 输入字符串的长度
 * @param output 输出缓冲区，用于存储解码后的二进制数据
 * @param output_len 输出缓冲区的长度，函数返回时更新为实际写入的长度
 * @return 成功返回0，失败返回非0
 */
int XBase64_decode(const char* input, size_t input_len, uint8_t* output, size_t* output_len);

#ifdef __cplusplus
}
#endif
#endif /* BASE64_H */
