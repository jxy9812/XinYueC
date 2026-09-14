#include "XStringUtils.h"
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

/* ==================== 通用字符串/字符原语实现 ==================== */

size_t XStrlen(const char* str)
{
	return str ? __builtin_strlen(str) : (size_t)0;
}

int XStrcmp(const char* lhs, const char* rhs)
{
	return __builtin_strcmp(lhs, rhs);
}

int XStrncmp(const char* lhs, const char* rhs, size_t n)
{
	return __builtin_strncmp(lhs, rhs, n);
}

const char* XStrstr(const char* haystack, const char* needle)
{
	size_t nl;
	if (!haystack || !needle) return haystack;
	nl = XStrlen(needle);
	if (nl == 0) return haystack;
	for (; *haystack; ++haystack) {
		size_t i;
		for (i = 0; i < nl; ++i)
			if (haystack[i] != needle[i]) break;
		if (i == nl) return haystack;
	}
	return NULL;
}

const char* XStrchr(const char* str, int ch)
{
	if (!str) return NULL;
	for (; *str; ++str)
		if (*str == (char)ch) return str;
	return (ch == '\0') ? str : NULL;
}

const char* XStrrchr(const char* str, int ch)
{
	const char* last = NULL;
	if (!str) return NULL;
	for (; *str; ++str)
		if (*str == (char)ch) last = str;
	if (ch == '\0') last = str;
	return last;
}

char* XStrcpy(char* dest, const char* src)
{
	char* d = dest;
	if (!dest || !src) return dest;
	while ((*d++ = *src++) != '\0') {}
	return dest;
}

char* XStrncpy(char* dest, const char* src, size_t n)
{
	size_t i = 0;
	if (!dest) return dest;
	if (src) {
		for (; i < n && src[i]; ++i) dest[i] = src[i];
	}
	for (; i < n; ++i) dest[i] = '\0';
	return dest;
}

char* XStrcat(char* dest, const char* src)
{
	char* d = dest;
	if (!dest || !src) return dest;
	while (*d) ++d;
	while ((*d++ = *src++) != '\0') {}
	return dest;
}

char* XStrncat(char* dest, const char* src, size_t n)
{
	char* d = dest;
	size_t i = 0;
	if (!dest || !src) return dest;
	while (*d) ++d;
	while (i < n && src[i]) {
		d[i] = src[i];
		++i;
	}
	d[i] = '\0';
	return dest;
}

int XIsSpace(int ch)
{
	return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

int XIsDigit(int ch)
{
	return ch >= '0' && ch <= '9';
}

int XIsAlpha(int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

int XIsAlnum(int ch)
{
	return XIsAlpha(ch) || XIsDigit(ch);
}

int XToLower(int ch)
{
	return (ch >= 'A' && ch <= 'Z') ? ch + ('a' - 'A') : ch;
}

int XToUpper(int ch)
{
	return (ch >= 'a' && ch <= 'z') ? ch - ('a' - 'A') : ch;
}

int XStrcasecmp(const char* lhs, const char* rhs)
{
	if (!lhs || !rhs) return lhs ? 1 : (rhs ? -1 : 0);
	while (*lhs && *rhs) {
		int d = XToLower((unsigned char)*lhs) -
			XToLower((unsigned char)*rhs);
		if (d) return d;
		++lhs;
		++rhs;
	}
	return (unsigned char)*lhs - (unsigned char)*rhs;
}

int XStrncasecmp(const char* lhs, const char* rhs, size_t n)
{
	if (!lhs || !rhs) return lhs ? 1 : (rhs ? -1 : 0);
	while (n > 0 && *lhs && *rhs) {
		int d = XToLower((unsigned char)*lhs) -
			XToLower((unsigned char)*rhs);
		if (d) return d;
		++lhs;
		++rhs;
		--n;
	}
	return n ? (unsigned char)*lhs - (unsigned char)*rhs : 0;
}


long XStrtol(const char* str, char** endptr, int base)
{
	long sign = 1;
	long value = 0;
	const char* start;
	if (!str) {
		if (endptr) *endptr = NULL;
		return 0;
	}
	while (XIsSpace((unsigned char)*str)) ++str;
	if (*str == '-') {
		sign = -1;
		++str;
	} else if (*str == '+') {
		++str;
	}
	if (base == 0) base = 10;
	start = str;
	while (*str) {
		int d;
		if (XIsDigit((unsigned char)*str)) d = *str - '0';
		else if (XIsAlpha((unsigned char)*str))
			d = XToLower((unsigned char)*str) - 'a' + 10;
		else break;
		if (d >= base) break;
		value = value * base + d;
		++str;
	}
	if (endptr) *endptr = (char*)(str == start ? start : str);
	return sign * value;
}


double XStrtod(const char* str, char** endptr)
{
	double sign = 1.0;
	double value = 0.0;
	double frac = 0.0;
	double scale = 1.0;
	int expSign = 1;
	int expVal = 0;
	const char* start;
	if (!str) {
		if (endptr) *endptr = NULL;
		return 0.0;
	}
	while (XIsSpace((unsigned char)*str)) ++str;
	if (*str == '-') {
		sign = -1.0;
		++str;
	} else if (*str == '+') {
		++str;
	}
	start = str;
	while (XIsDigit((unsigned char)*str)) {
		value = value * 10.0 + (*str - '0');
		++str;
	}
	if (*str == '.') {
		++str;
		while (XIsDigit((unsigned char)*str)) {
			frac = frac * 10.0 + (*str - '0');
			scale *= 10.0;
			++str;
		}
	}
	value += frac / scale;
	if (*str == 'e' || *str == 'E') {
		const char* e = str + 1;
		if (*e == '-' || *e == '+') {
			expSign = (*e == '-') ? -1 : 1;
			++e;
		}
		if (XIsDigit((unsigned char)*e)) {
			str = e;
			while (XIsDigit((unsigned char)*str)) {
				expVal = expVal * 10 + (*str - '0');
				++str;
			}
			while (expVal-- > 0)
				value = (expSign < 0) ? value / 10.0 : value * 10.0;
		}
	}
	if (endptr) *endptr = (char*)(str == start ? start : str);
	return sign * value;
}


/* ==================== XSnprintf（包装标准库 vsnprintf） ==================== */

#include <stdarg.h>
#include <stdio.h>

int XSnprintf(char* buf, size_t size, const char* format, ...)
{
	va_list args;
	int n;
	if (!buf || size == 0 || !format) {
		/* 无缓冲区时也需消耗参数并返回所需长度（对标 snprintf）。 */
		va_start(args, format);
		n = vsnprintf(NULL, 0, format, args);
		va_end(args);
		return n;
	}
	va_start(args, format);
	n = vsnprintf(buf, size, format, args);
	va_end(args);
	return n;
}


/* ==================== XSscanf（包装标准库 vsscanf） ==================== */

int XSscanf(const char* str, const char* format, ...)
{
	va_list args;
	int n;
	if (!str || !format) return -1;
	va_start(args, format);
	n = vsscanf(str, format, args);
	va_end(args);
	return n;
}
