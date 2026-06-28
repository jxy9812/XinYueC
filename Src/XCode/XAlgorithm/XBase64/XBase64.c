#include "XBase64.h"
#if XBase64_ON
#include <string.h>
#include <ctype.h>

/* Base64编码表 */
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64解码表，索引为ASCII值，值为对应的6位数据 */
static const uint8_t base64_reverse_table[256] = {
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
    64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
    64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
    64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
};

size_t XBase64_encoded_size(size_t input_len) {
    return ((input_len + 2) / 3) * 4 + 1; // +1 for null terminator
}

size_t XBase64_decoded_size(const char* input, size_t input_len) {
    if (input_len == 0) return 0;

    size_t padding = 0;
    if (input_len >= 2 && input[input_len - 1] == '=') {
        padding++;
        if (input[input_len - 2] == '=') {
            padding++;
        }
    }

    return (input_len / 4) * 3 - padding;
}

int XBase64_encode(const uint8_t* input, size_t input_len, char* output, size_t* output_len) {
    if (input == NULL || output == NULL || output_len == NULL) {
        return -1; // 参数错误
    }

    size_t encoded_len = XBase64_encoded_size(input_len);
    if ((*output_len) < encoded_len)
    {
        *output_len = encoded_len;
        return -2; // 输出缓冲区太小
    }

    size_t i, j;
    for (i = 0, j = 0; i < input_len;) {
        uint32_t octet_a = i < input_len ? input[i++] : 0;
        uint32_t octet_b = i < input_len ? input[i++] : 0;
        uint32_t octet_c = i < input_len ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = base64_table[(triple >> 6) & 0x3F];
        output[j++] = base64_table[triple & 0x3F];
    }

    // 处理填充
    switch (input_len % 3) {
    case 1:
        output[j - 2] = '=';
        output[j - 1] = '=';
        break;
    case 2:
        output[j - 1] = '=';
        break;
    }

    output[j] = '\0'; // 添加字符串终止符
    *output_len = j + 1; // 返回实际写入的长度（包括终止符）
    return 0; // 成功
}

int XBase64_decode(const char* input, size_t input_len, uint8_t* output, size_t* output_len) {
    if (input == NULL || output == NULL || output_len == NULL) {
        return -1; // 参数错误
    }

    // 计算解码后的大小
    size_t decoded_len = XBase64_decoded_size(input, input_len);
    if (*output_len < decoded_len) {
        *output_len = decoded_len;
        return -2; // 输出缓冲区太小
    }

    size_t i, j;
    for (i = 0, j = 0; i < input_len; i += 4) {
        // 检查是否到达字符串结束
        if (input[i] == '\0') {
            break; // 提前退出循环
        }

        // 读取四个Base64字符
        unsigned char a_char = (unsigned char)input[i];
        unsigned char b_char = (i + 1 < input_len && input[i + 1] != '\0') ?
            (unsigned char)input[i + 1] : 0;
        unsigned char c_char = (i + 2 < input_len && input[i + 2] != '\0') ?
            (unsigned char)input[i + 2] : 0;
        unsigned char d_char = (i + 3 < input_len && input[i + 3] != '\0') ?
            (unsigned char)input[i + 3] : 0;

        uint32_t a = base64_reverse_table[a_char];
        uint32_t b = (i + 1 < input_len && input[i + 1] != '\0') ?
            base64_reverse_table[b_char] : 64;
        uint32_t c = (i + 2 < input_len && input[i + 2] != '\0') ?
            base64_reverse_table[c_char] : 64;
        uint32_t d = (i + 3 < input_len && input[i + 3] != '\0') ?
            base64_reverse_table[d_char] : 64;

        // 检查是否有非法字符（特殊处理填充符）
        if ((a > 63 && a_char != '=') ||
            (b > 63 && b_char != '=') ||
            (c > 63 && c_char != '=') ||
            (d > 63 && d_char != '=')) {
            return -3; // 包含非法Base64字符
        }

        // 解码
        uint32_t triple = (a << 18) + (b << 12) + (c << 6) + d;

        // 写入解码结果
        if (j < decoded_len) output[j++] = (triple >> 16) & 0xFF;
        if (j < decoded_len) output[j++] = (triple >> 8) & 0xFF;
        if (j < decoded_len) output[j++] = triple & 0xFF;
    }

    *output_len = j; // 返回实际写入的长度
    return 0; // 成功
}
#endif