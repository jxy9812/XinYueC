#include "XNumStrConv.h"
#include <string.h>
#include <math.h>
static float pow10_float(int n);
// 内部辅助函数：反转字符串
static void reverse_str(char* start, char* end) {
    while (start < end) {
        char temp = *start;
        *start++ = *end;
        *end-- = temp;
    }
}

// 内部辅助函数：检查字符是否为数字
static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

// 内部辅助函数：计算10的n次方（整数）
static uint64_t pow10_uint(uint32_t n) {
    uint64_t result = 1;
    for (uint32_t i = 0; i < n; i++) {
        result *= 10;
    }
    return result;
}

// 内部辅助函数：计算10的n次方（浮点数）
static double pow10_double(int n) {
    double result = 1.0;
    if (n >= 0) {
        for (int i = 0; i < n; i++) {
            result *= 10.0;
        }
    }
    else {
        for (int i = 0; i < -n; i++) {
            result /= 10.0;
        }
    }
    return result;
}

// int64_t转字符串
ConvStatus int64_to_str(int64_t num, char* buf, size_t buf_size) {
    if (buf == NULL) return CONV_NULL_OUTPUT;
    if (buf_size < 1) return CONV_BUFFER_TOO_SMALL;

    // 处理INT64_MIN特殊情况
    if (num == INT64_MIN) {
        const char* min_str = "-9223372036854775808";
        if (buf_size <= strlen(min_str)) return CONV_BUFFER_TOO_SMALL;
        strcpy(buf, min_str);
        return CONV_OK;
    }

    char* ptr = buf;
    bool is_negative = false;

    // 处理负数
    if (num < 0) {
        is_negative = true;
        num = -num;
        if (buf_size < 2) return CONV_BUFFER_TOO_SMALL;
        *ptr++ = '-';
    }

    // 处理0的情况
    if (num == 0) {
        if (ptr + 1 >= buf + buf_size) return CONV_BUFFER_TOO_SMALL;
        *ptr++ = '0';
        *ptr = '\0';
        return CONV_OK;
    }

    // 提取数字（逆序）
    char* digit_start = ptr;
    while (num > 0) {
        if (ptr + 1 >= buf + buf_size) return CONV_BUFFER_TOO_SMALL;
        *ptr++ = '0' + (num % 10);
        num /= 10;
    }

    // 反转数字部分
    reverse_str(digit_start, ptr - 1);

    // 添加终止符
    *ptr = '\0';
    return CONV_OK;
}

// uint64_t转字符串
ConvStatus uint64_to_str(uint64_t num, char* buf, size_t buf_size) {
    if (buf == NULL) return CONV_NULL_OUTPUT;
    if (buf_size < 1) return CONV_BUFFER_TOO_SMALL;

    // 处理0的情况
    if (num == 0) {
        if (buf_size < 2) return CONV_BUFFER_TOO_SMALL;
        buf[0] = '0';
        buf[1] = '\0';
        return CONV_OK;
    }

    char* ptr = buf;

    // 提取数字（逆序）
    char* digit_start = ptr;
    while (num > 0) {
        if (ptr + 1 >= buf + buf_size) return CONV_BUFFER_TOO_SMALL;
        *ptr++ = '0' + (num % 10);
        num /= 10;
    }

    // 反转数字部分
    reverse_str(digit_start, ptr - 1);

    // 添加终止符
    *ptr = '\0';
    return CONV_OK;
}

// int32_t转字符串
ConvStatus int32_to_str(int32_t num, char* buf, size_t buf_size) {
    return int64_to_str((int64_t)num, buf, buf_size);
}

// uint32_t转字符串
ConvStatus uint32_to_str(uint32_t num, char* buf, size_t buf_size) {
    return uint64_to_str((uint64_t)num, buf, buf_size);
}

// 字符串转int64_t
ConvStatus str_to_int64(const char* str, int64_t* result) {
    if (str == NULL) return CONV_NULL_INPUT;
    if (result == NULL) return CONV_NULL_OUTPUT;

    *result = 0;
    size_t i = 0;
    bool is_negative = false;

    // 处理空字符串
    if (str[0] == '\0') return CONV_INVALID_FORMAT;

    // 处理符号
    if (str[0] == '-') {
        is_negative = true;
        i++;
    }
    else if (str[0] == '+') {
        i++;
    }

    // 检查是否有数字
    if (!is_digit(str[i])) return CONV_INVALID_CHAR;

    // 处理INT64_MIN特殊情况
    if (is_negative && strcmp(str, "-9223372036854775808") == 0) {
        *result = INT64_MIN;
        return CONV_OK;
    }

    // 转换数字
    while (is_digit(str[i])) {
        int digit = str[i] - '0';

        // 检查溢出
        if (*result > (INT64_MAX - digit) / 10) {
            return CONV_OVERFLOW;
        }

        *result = *result * 10 + digit;
        i++;
    }

    // 检查是否有剩余的无效字符
    if (str[i] != '\0') return CONV_INVALID_CHAR;

    // 应用符号
    if (is_negative) {
        *result = -(*result);
    }

    return CONV_OK;
}

// 字符串转uint64_t
ConvStatus str_to_uint64(const char* str, uint64_t* result) {
    if (str == NULL) return CONV_NULL_INPUT;
    if (result == NULL) return CONV_NULL_OUTPUT;

    *result = 0;
    size_t i = 0;

    // 处理空字符串
    if (str[0] == '\0') return CONV_INVALID_FORMAT;

    // 处理正号（允许但忽略）
    if (str[0] == '+') {
        i++;
    }
    else if (str[0] == '-') {  // 不允许负数
        return CONV_INVALID_FORMAT;
    }

    // 检查是否有数字
    if (!is_digit(str[i])) return CONV_INVALID_CHAR;

    // 转换数字
    while (is_digit(str[i])) {
        int digit = str[i] - '0';

        // 检查溢出
        if (*result > (UINT64_MAX - digit) / 10) {
            return CONV_OVERFLOW;
        }

        *result = *result * 10 + digit;
        i++;
    }

    // 检查是否有剩余的无效字符
    if (str[i] != '\0') return CONV_INVALID_CHAR;

    return CONV_OK;
}

// 字符串转int32_t
ConvStatus str_to_int32(const char* str, int32_t* result) {
    if (str == NULL) return CONV_NULL_INPUT;
    if (result == NULL) return CONV_NULL_OUTPUT;

    int64_t temp;
    ConvStatus status = str_to_int64(str, &temp);

    if (status != CONV_OK) {
        return status;
    }

    // 检查是否在int32_t范围内
    if (temp < INT32_MIN || temp > INT32_MAX) {
        return CONV_OVERFLOW;
    }

    *result = (int32_t)temp;
    return CONV_OK;
}

// 字符串转uint32_t
ConvStatus str_to_uint32(const char* str, uint32_t* result) {
    if (str == NULL) return CONV_NULL_INPUT;
    if (result == NULL) return CONV_NULL_OUTPUT;

    uint64_t temp;
    ConvStatus status = str_to_uint64(str, &temp);

    if (status != CONV_OK) {
        return status;
    }

    // 检查是否在uint32_t范围内
    if (temp > UINT32_MAX) {
        return CONV_OVERFLOW;
    }

    *result = (uint32_t)temp;
    return CONV_OK;
}

// float转字符串（precision为负数时启用自动位数处理，类似%g）
ConvStatus float_to_str(float num, char* buf, size_t buf_size, int precision) {
    if (buf == NULL) return CONV_NULL_OUTPUT;
    if (buf_size < 1) return CONV_BUFFER_TOO_SMALL;

    // 标记是否启用自动模式（precision为负数时）
    bool auto_precision = (precision < 0);
    // 自动模式下默认精度为6，与%g一致；否则使用指定精度（最小0）
    int effective_precision = auto_precision ? 6 : (precision < 0 ? 0 : precision);
    // 限制有效精度范围（0-9之间，float有效位数约6-7位）
    if (effective_precision > 9) effective_precision = 9;
    if (effective_precision < 0) effective_precision = 0;

    // 处理特殊值
    if (isnan(num)) {
        if (buf_size < 4) return CONV_BUFFER_TOO_SMALL;
        strcpy(buf, "NaN");
        return CONV_OK;
    }

    if (isinf(num)) {
        if (num > 0) {
            if (buf_size < 9) return CONV_BUFFER_TOO_SMALL;
            strcpy(buf, "Infinity");
        }
        else {
            if (buf_size < 10) return CONV_BUFFER_TOO_SMALL;
            strcpy(buf, "-Infinity");
        }
        return CONV_OK;
    }

    // 处理零值特殊情况
    if (num == 0.0f) {
        if (buf_size < 2) return CONV_BUFFER_TOO_SMALL;
        if (effective_precision > 0) {
            // 需要包含小数点和指定小数位
            size_t required = 2 + effective_precision; // "0." + 小数位 + '\0'
            if (buf_size < required) return CONV_BUFFER_TOO_SMALL;

            char* ptr = buf;
            *ptr++ = '0';
            *ptr++ = '.';
            for (int i = 0; i < effective_precision; i++) {
                *ptr++ = '0';
            }
            *ptr = '\0';
        }
        else {
            strcpy(buf, "0");
        }
        return CONV_OK;
    }

    char* ptr = buf;
    bool is_negative = false;

    // 处理负数
    if (num < 0) {
        is_negative = true;
        num = -num;
        if (buf_size < 2) return CONV_BUFFER_TOO_SMALL;
        *ptr++ = '-';
    }

    // 自动模式下计算是否使用科学计数法
    float abs_num = num;
    int exponent = 0;
    bool use_scientific = false;

    if (auto_precision && abs_num > 0) {
        // 计算指数（用于判断是否使用科学计数法）
        if (abs_num < 1.0f) {
            while (abs_num < 1.0f && exponent > -126) { // float最小指数约-126
                abs_num *= 10.0f;
                exponent--;
            }
        }
        else {
            while (abs_num >= 10.0f && exponent < 127) { // float最大指数约127
                abs_num /= 10.0f;
                exponent++;
            }
        }

        // 类似%g的判断：指数太大或太小则使用科学计数法
        if (exponent < -4 || exponent >= effective_precision) {
            use_scientific = true;
        }
    }

    char temp_buf[64];  // 临时缓冲区
    char* temp_ptr = temp_buf;
    size_t frac_len = 0;

    if (auto_precision && use_scientific) {
        // 自动模式-科学计数法处理
        // 添加四舍五入处理
        float rounding = pow10_float(-effective_precision);
        abs_num += rounding / 2.0f;

        uint32_t int_part = (uint32_t)abs_num;
        float frac_part = abs_num - int_part;

        // 处理整数部分（只有一位）
        *temp_ptr++ = '0' + (int)int_part;

        // 处理小数部分
        if (effective_precision > 1) {
            *temp_ptr++ = '.';
            for (int i = 1; i < effective_precision; i++) {
                frac_part *= 10.0f;
                // 处理精度误差导致的9.999999情况
                if (frac_part >= 9.999999f) {
                    frac_part = 0.0f;
                    // 进位处理
                    *(temp_ptr - 2) += 1;
                    // 检查是否需要进一步进位
                    char* carry_ptr = temp_ptr - 2;
                    while (*carry_ptr > '9') {
                        *carry_ptr = '0';
                        if (carry_ptr > temp_buf) {
                            carry_ptr--;
                            *carry_ptr += 1;
                        }
                        else {
                            // 最高位进位，需要插入新数字
                            memmove(temp_buf + 1, temp_buf, temp_ptr - temp_buf);
                            temp_buf[0] = '1';
                            temp_ptr++;
                            break;
                        }
                    }
                }
                int digit = (int)floorf(frac_part);
                *temp_ptr++ = '0' + digit;
                frac_part -= digit;
                frac_len++;
            }
        }

        // 添加指数部分
        *temp_ptr++ = 'e';
        if (exponent >= 0) {
            *temp_ptr++ = '+';
        }
        // 转换指数为字符串
        char exp_buf[16];
        int exp_abs = abs(exponent);
        size_t exp_len = 0;
        if (exp_abs == 0) {
            exp_buf[exp_len++] = '0';
        }
        else {
            while (exp_abs > 0) {
                exp_buf[exp_len++] = '0' + (exp_abs % 10);
                exp_abs /= 10;
            }
            reverse_str(exp_buf, exp_buf + exp_len - 1);
        }
        // 复制指数到临时缓冲区
        for (size_t i = 0; i < exp_len; i++) {
            *temp_ptr++ = exp_buf[i];
        }
    }
    else {
        // 普通格式处理（自动模式或指定精度模式）
        float integer_part;
        float fractional_part = modff(num, &integer_part);

        // 处理四舍五入
        if (effective_precision > 0) {
            float rounding = pow10_float(-effective_precision);
            fractional_part += rounding / 2.0f;

            // 检查是否需要进位到整数部分
            if (fractional_part >= 1.0f) {
                fractional_part -= 1.0f;
                integer_part += 1.0f;
            }
        }

        uint32_t int_val = (uint32_t)integer_part;

        // 处理整数部分为0的情况
        if (int_val == 0) {
            *temp_ptr++ = '0';
        }
        else {
            // 提取整数部分（逆序）
            char* digit_start = temp_ptr;
            while (int_val > 0) {
                *temp_ptr++ = '0' + (int_val % 10);
                int_val /= 10;
            }
            // 反转整数部分
            reverse_str(digit_start, temp_ptr - 1);
        }

        // 处理小数部分（精度>0时）
        if (effective_precision > 0) {
            // 自动模式下忽略极小的小数部分，指定精度模式则强制保留
            if (auto_precision && fractional_part <= 1e-7f) { // float精度约1e-7
                // 自动模式且小数部分接近零，不显示小数
            }
            else {
                *temp_ptr++ = '.';
                for (int i = 0; i < effective_precision; i++) {
                    fractional_part *= 10.0f;
                    // 处理精度误差导致的9.999999情况
                    if (fractional_part >= 9.999999f) {
                        fractional_part = 0.0f;
                        // 进位处理
                        char* carry_ptr = temp_ptr - 1; // 指向小数点
                        carry_ptr--; // 指向整数部分最后一位

                        while (carry_ptr >= temp_buf && *carry_ptr == '9') {
                            *carry_ptr = '0';
                            carry_ptr--;
                        }

                        if (carry_ptr < temp_buf) {
                            // 整数部分全是9，需要在开头加1
                            memmove(temp_buf + 1, temp_buf, temp_ptr - temp_buf);
                            temp_buf[0] = '1';
                            temp_ptr++;
                        }
                        else {
                            *carry_ptr += 1;
                        }
                    }
                    int digit = (int)floorf(fractional_part);
                    *temp_ptr++ = '0' + digit;
                    fractional_part -= digit;
                    frac_len++;
                }
            }
        }
    }

    // 仅自动模式下去除尾随零
    if (auto_precision && frac_len > 0) {
        // 从后往前找到第一个非零小数位
        while (frac_len > 0 && *(temp_ptr - 1) == '0') {
            temp_ptr--;
            frac_len--;
        }
        // 如果小数部分全是零，去除小数点
        if (frac_len == 0) {
            temp_ptr--;  // 移除小数点
        }
    }

    // 计算所需缓冲区大小
    size_t total_len = (is_negative ? 1 : 0) + (temp_ptr - temp_buf);
    if (total_len + 1 > buf_size) {  // +1 是终止符
        return CONV_BUFFER_TOO_SMALL;
    }

    // 复制临时缓冲区内容到输出缓冲区
    if (is_negative) {
        *ptr++ = '-';
    }
    memcpy(ptr, temp_buf, temp_ptr - temp_buf);
    ptr += temp_ptr - temp_buf;
    *ptr = '\0';  // 添加终止符

    return CONV_OK;
}

// double转字符串（precision为负数时启用自动位数处理，类似%g）
ConvStatus double_to_str(double num, char* buf, size_t buf_size, int precision) {
    if (buf == NULL) return CONV_NULL_OUTPUT;
    if (buf_size < 1) return CONV_BUFFER_TOO_SMALL;

    // 标记是否启用自动模式（precision为负数时）
    bool auto_precision = (precision < 0);
    // 自动模式下默认精度为6，与%g一致；否则使用指定精度（最小0）
    int effective_precision = auto_precision ? 6 : (precision < 0 ? 0 : precision);
    // 限制有效精度范围（0-15之间，double有效位数约15-17位）
    if (effective_precision > 15) effective_precision = 15;
    if (effective_precision < 0) effective_precision = 0;

    // 处理特殊值
    if (isnan(num)) {
        if (buf_size < 4) return CONV_BUFFER_TOO_SMALL;
        strcpy(buf, "NaN");
        return CONV_OK;
    }

    if (isinf(num)) {
        if (num > 0) {
            if (buf_size < 9) return CONV_BUFFER_TOO_SMALL;
            strcpy(buf, "Infinity");
        }
        else {
            if (buf_size < 10) return CONV_BUFFER_TOO_SMALL;
            strcpy(buf, "-Infinity");
        }
        return CONV_OK;
    }

    // 处理零值特殊情况
    if (num == 0.0) {
        if (buf_size < 2) return CONV_BUFFER_TOO_SMALL;
        if (effective_precision > 0) {
            // 需要包含小数点和指定小数位
            size_t required = 2 + effective_precision; // "0." + 小数位 + '\0'
            if (buf_size < required) return CONV_BUFFER_TOO_SMALL;

            char* ptr = buf;
            *ptr++ = '0';
            *ptr++ = '.';
            for (int i = 0; i < effective_precision; i++) {
                *ptr++ = '0';
            }
            *ptr = '\0';
        }
        else {
            strcpy(buf, "0");
        }
        return CONV_OK;
    }

    char* ptr = buf;
    bool is_negative = false;

    // 处理负数
    if (num < 0) {
        is_negative = true;
        num = -num;
        if (buf_size < 2) return CONV_BUFFER_TOO_SMALL;
        *ptr++ = '-';
    }

    // 自动模式下计算是否使用科学计数法
    double abs_num = num;
    int exponent = 0;
    bool use_scientific = false;

    if (auto_precision && abs_num > 0) {
        // 计算指数（用于判断是否使用科学计数法）
        if (abs_num < 1.0) {
            while (abs_num < 1.0 && exponent > -308) { // 避免无限循环
                abs_num *= 10.0;
                exponent--;
            }
        }
        else {
            while (abs_num >= 10.0 && exponent < 308) { // 避免无限循环
                abs_num /= 10.0;
                exponent++;
            }
        }

        // 类似%g的判断：指数太大或太小则使用科学计数法
        if (exponent < -4 || exponent >= effective_precision) {
            use_scientific = true;
        }
    }

    char temp_buf[64];  // 临时缓冲区
    char* temp_ptr = temp_buf;
    size_t frac_len = 0;

    if (auto_precision && use_scientific) {
        // 自动模式-科学计数法处理
        // 添加四舍五入处理
        double rounding = pow10_double(-effective_precision);
        abs_num += rounding / 2.0;

        uint64_t int_part = (uint64_t)abs_num;
        double frac_part = abs_num - int_part;

        // 处理整数部分（只有一位）
        *temp_ptr++ = '0' + (int)int_part;

        // 处理小数部分
        if (effective_precision > 1) {
            *temp_ptr++ = '.';
            for (int i = 1; i < effective_precision; i++) {
                frac_part *= 10.0;
                // 处理精度误差导致的9.9999999999情况
                if (frac_part >= 9.9999999999) {
                    frac_part = 0.0;
                    // 进位处理
                    *(temp_ptr - 2) += 1;
                    // 检查是否需要进一步进位
                    char* carry_ptr = temp_ptr - 2;
                    while (*carry_ptr > '9') {
                        *carry_ptr = '0';
                        if (carry_ptr > temp_buf) {
                            carry_ptr--;
                            *carry_ptr += 1;
                        }
                        else {
                            // 最高位进位，需要插入新数字
                            memmove(temp_buf + 1, temp_buf, temp_ptr - temp_buf);
                            temp_buf[0] = '1';
                            temp_ptr++;
                            break;
                        }
                    }
                }
                int digit = (int)floor(frac_part);
                *temp_ptr++ = '0' + digit;
                frac_part -= digit;
                frac_len++;
            }
        }

        // 添加指数部分
        *temp_ptr++ = 'e';
        if (exponent >= 0) {
            *temp_ptr++ = '+';
        }
        // 转换指数为字符串
        char exp_buf[16];
        int exp_abs = abs(exponent);
        size_t exp_len = 0;
        if (exp_abs == 0) {
            exp_buf[exp_len++] = '0';
        }
        else {
            while (exp_abs > 0) {
                exp_buf[exp_len++] = '0' + (exp_abs % 10);
                exp_abs /= 10;
            }
            reverse_str(exp_buf, exp_buf + exp_len - 1);
        }
        // 复制指数到临时缓冲区
        for (size_t i = 0; i < exp_len; i++) {
            *temp_ptr++ = exp_buf[i];
        }
    }
    else {
        // 普通格式处理（自动模式或指定精度模式）
        double integer_part;
        double fractional_part = modf(num, &integer_part);

        // 处理四舍五入
        if (effective_precision > 0) {
            double rounding = pow10_double(-effective_precision);
            fractional_part += rounding / 2.0;

            // 检查是否需要进位到整数部分
            if (fractional_part >= 1.0) {
                fractional_part -= 1.0;
                integer_part += 1.0;
            }
        }

        uint64_t int_val = (uint64_t)integer_part;

        // 处理整数部分为0的情况
        if (int_val == 0) {
            *temp_ptr++ = '0';
        }
        else {
            // 提取整数部分（逆序）
            char* digit_start = temp_ptr;
            while (int_val > 0) {
                *temp_ptr++ = '0' + (int_val % 10);
                int_val /= 10;
            }
            // 反转整数部分
            reverse_str(digit_start, temp_ptr - 1);
        }

        // 处理小数部分（精度>0时）
        if (effective_precision > 0) {
            // 自动模式下忽略极小的小数部分，指定精度模式则强制保留
            if (auto_precision && fractional_part <= 1e-15) {
                // 自动模式且小数部分接近零，不显示小数
            }
            else {
                *temp_ptr++ = '.';
                for (int i = 0; i < effective_precision; i++) {
                    fractional_part *= 10;
                    // 处理精度误差导致的9.9999999999情况
                    if (fractional_part >= 9.9999999999) {
                        fractional_part = 0.0;
                        // 进位处理
                        char* carry_ptr = temp_ptr - 1; // 指向小数点
                        carry_ptr--; // 指向整数部分最后一位

                        while (carry_ptr >= temp_buf && *carry_ptr == '9') {
                            *carry_ptr = '0';
                            carry_ptr--;
                        }

                        if (carry_ptr < temp_buf) {
                            // 整数部分全是9，需要在开头加1
                            memmove(temp_buf + 1, temp_buf, temp_ptr - temp_buf);
                            temp_buf[0] = '1';
                            temp_ptr++;
                        }
                        else {
                            *carry_ptr += 1;
                        }
                    }
                    int digit = (int)floor(fractional_part);
                    *temp_ptr++ = '0' + digit;
                    fractional_part -= digit;
                    frac_len++;
                }
            }
        }
    }

    // 仅自动模式下去除尾随零
    if (auto_precision && frac_len > 0) {
        // 从后往前找到第一个非零小数位
        while (frac_len > 0 && *(temp_ptr - 1) == '0') {
            temp_ptr--;
            frac_len--;
        }
        // 如果小数部分全是零，去除小数点
        if (frac_len == 0) {
            temp_ptr--;  // 移除小数点
        }
    }

    // 计算所需缓冲区大小
    size_t total_len = (is_negative ? 1 : 0) + (temp_ptr - temp_buf);
    if (total_len + 1 > buf_size) {  // +1 是终止符
        return CONV_BUFFER_TOO_SMALL;
    }

    // 复制临时缓冲区内容到输出缓冲区
    if (is_negative) {
        *ptr++ = '-';
    }
    memcpy(ptr, temp_buf, temp_ptr - temp_buf);
    ptr += temp_ptr - temp_buf;
    *ptr = '\0';  // 添加终止符

    return CONV_OK;
}

// 字符串转float
ConvStatus str_to_float(const char* str, float* result) {
    if (str == NULL) return CONV_NULL_INPUT;
    if (result == NULL) return CONV_NULL_OUTPUT;

    *result = 0.0f;
    size_t i = 0;
    bool is_negative = false;
    bool has_decimal = false;
    int decimal_places = 0;

    // 处理空字符串
    if (str[0] == '\0') return CONV_INVALID_FORMAT;

    // 处理特殊值
    if (strcmp(str, "NaN") == 0) {
        *result = NAN;
        return CONV_OK;
    }
    if (strcmp(str, "Infinity") == 0) {
        *result = INFINITY;
        return CONV_OK;
    }
    if (strcmp(str, "-Infinity") == 0) {
        *result = -INFINITY;
        return CONV_OK;
    }

    // 处理符号
    if (str[0] == '-') {
        is_negative = true;
        i++;
    }
    else if (str[0] == '+') {
        i++;
    }

    // 检查是否有数字或小数点
    if (!is_digit(str[i]) && str[i] != '.') return CONV_INVALID_CHAR;

    // 转换数字
    while (str[i] != '\0') {
        if (is_digit(str[i])) {
            float digit = (float)(str[i] - '0');

            if (has_decimal) {
                decimal_places++;
                *result += digit / pow10_float(decimal_places);
            }
            else {
                *result = *result * 10.0f + digit;
            }
            i++;
        }
        else if (str[i] == '.' && !has_decimal) {
            has_decimal = true;
            i++;
            // 确保小数点后有数字
            if (!is_digit(str[i])) return CONV_INVALID_FORMAT;
        }
        else {
            // 无效字符
            return CONV_INVALID_CHAR;
        }
    }

    // 应用符号
    if (is_negative) {
        *result = -(*result);
    }

    // 检查溢出/下溢
    if (isinf(*result)) {
        return CONV_OVERFLOW;
    }
    if (*result == 0.0f && (is_negative || has_decimal)) {
        return CONV_UNDERFLOW;
    }

    return CONV_OK;
}

// 字符串转double
ConvStatus str_to_double(const char* str, double* result) {
    if (str == NULL) return CONV_NULL_INPUT;
    if (result == NULL) return CONV_NULL_OUTPUT;

    *result = 0.0;
    size_t i = 0;
    bool is_negative = false;
    bool has_decimal = false;
    int decimal_places = 0;

    // 处理空字符串
    if (str[0] == '\0') return CONV_INVALID_FORMAT;

    // 处理特殊值
    if (strcmp(str, "NaN") == 0) {
        *result = NAN;
        return CONV_OK;
    }
    if (strcmp(str, "Infinity") == 0) {
        *result = INFINITY;
        return CONV_OK;
    }
    if (strcmp(str, "-Infinity") == 0) {
        *result = -INFINITY;
        return CONV_OK;
    }

    // 处理符号
    if (str[0] == '-') {
        is_negative = true;
        i++;
    }
    else if (str[0] == '+') {
        i++;
    }

    // 检查是否有数字或小数点
    if (!is_digit(str[i]) && str[i] != '.') return CONV_INVALID_CHAR;

    // 转换数字
    while (str[i] != '\0') {
        if (is_digit(str[i])) {
            double digit = (double)(str[i] - '0');

            if (has_decimal) {
                decimal_places++;
                *result += digit / pow10_double(decimal_places);
            }
            else {
                *result = *result * 10.0 + digit;
            }
            i++;
        }
        else if (str[i] == '.' && !has_decimal) {
            has_decimal = true;
            i++;
            // 确保小数点后有数字
            if (!is_digit(str[i])) return CONV_INVALID_FORMAT;
        }
        else {
            // 无效字符
            return CONV_INVALID_CHAR;
        }
    }

    // 应用符号
    if (is_negative) {
        *result = -(*result);
    }

    // 检查溢出/下溢
    if (isinf(*result)) {
        return CONV_OVERFLOW;
    }
    if (*result == 0.0 && (is_negative || has_decimal)) {
        return CONV_UNDERFLOW;
    }

    return CONV_OK;
}

// 计算int64_t所需缓冲区大小
size_t int64_required_buf_size(int64_t num) {
    if (num == 0) return 2;  // "0" + '\0'
    if (num == INT64_MIN) return 20;  // "-9223372036854775808" + '\0'

    size_t size = (num < 0) ? 2 : 1;  // 符号位（如果需要）+ 终止符
    uint64_t n = (num < 0) ? (uint64_t)(-num) : (uint64_t)num;

    while (n > 0) {
        size++;
        n /= 10;
    }

    return size;
}

// 计算uint64_t所需缓冲区大小
size_t uint64_required_buf_size(uint64_t num) {
    if (num == 0) return 2;  // "0" + '\0'

    size_t size = 1;  // 终止符
    uint64_t n = num;

    while (n > 0) {
        size++;
        n /= 10;
    }

    return size;
}

// 计算int32_t所需缓冲区大小
size_t int32_required_buf_size(int32_t num) {
    return int64_required_buf_size((int64_t)num);
}

// 计算uint32_t所需缓冲区大小
size_t uint32_required_buf_size(uint32_t num) {
    return uint64_required_buf_size((uint64_t)num);
}

// 计算float所需缓冲区大小
size_t float_required_buf_size(float num, int precision) {
    if (precision < 0) precision = 0;

    // 特殊值
    if (isnan(num)) return 4;  // "NaN" + '\0'
    if (isinf(num)) return (num > 0) ? 9 : 10;  // "Infinity" 或 "-Infinity" + '\0'

    size_t size = 1;  // 终止符

    // 符号位
    if (num < 0) {
        size++;
        num = -num;
    }

    // 整数部分
    float integer_part = floorf(num);
    if (integer_part == 0) {
        size++;  // 至少一个0
    }
    else {
        uint64_t int_val = (uint64_t)integer_part;
        while (int_val > 0) {
            size++;
            int_val /= 10;
        }
    }

    // 小数部分
    if (precision > 0) {
        size += 1 + precision;  // 小数点 + 小数位数
    }

    return size;
}

// 计算double所需缓冲区大小
size_t double_required_buf_size(double num, int precision) {
    if (precision < 0) precision = 0;

    // 特殊值
    if (isnan(num)) return 4;  // "NaN" + '\0'
    if (isinf(num)) return (num > 0) ? 9 : 10;  // "Infinity" 或 "-Infinity" + '\0'

    size_t size = 1;  // 终止符

    // 符号位
    if (num < 0) {
        size++;
        num = -num;
    }

    // 整数部分
    double integer_part = floor(num);
    if (integer_part == 0) {
        size++;  // 至少一个0
    }
    else {
        uint64_t int_val = (uint64_t)integer_part;
        while (int_val > 0) {
            size++;
            int_val /= 10;
        }
    }

    // 小数部分
    if (precision > 0) {
        size += 1 + precision;  // 小数点 + 小数位数
    }

    return size;
}

// 内部辅助函数：计算10的n次方（float）
static float pow10_float(int n) {
    float result = 1.0f;
    if (n >= 0) {
        for (int i = 0; i < n; i++) {
            result *= 10.0f;
        }
    }
    else {
        for (int i = 0; i < -n; i++) {
            result /= 10.0f;
        }
    }
    return result;
}
