#ifndef XSTRINGUTILS_H
#define XSTRINGUTILS_H

/**
 * @file XStringUtils.h
 * @brief 字符串与字符工具：数字↔字符串转换 + 通用字符串/字符原语。
 *
 * @details 本模块整合两类无平台 API 的工具：
 *          1) 整数/浮点数与十进制字符串互转（原 XNumStrConv）；
 *          2) 通用字符串/字符原语（原 XAlgorithm 中的 XStrlen/XStrcmp/
 *             XIsSpace 等，XGui 等上层模块不允许包含 <string.h>/<ctype.h>，
 *             统一由此提供）。
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

/* ==================== 通用字符串/字符原语 ==================== */

/*
 * XGui 等上层模块不允许包含 <string.h>/<ctype.h> 等 C 平台头；本节提供
 * 等价原语。字符分类为显式 ASCII 实现（不依赖 locale）；忽略大小写比较
 * 为 ASCII 折叠。内存原语（XMemcpy/XMemset/XMemmove/XMemcmp）属
 * XMemory 模块（Src/XMemory/XMemory.h）。
 */

/**
 * @brief 字符串长度（语义同 strlen，不含终止符；NULL 返回 0）。
 * @param str 以 '\0' 结尾的字符串。
 * @return 字符字节数。
 */
size_t XStrlen(const char* str);

/**
 * @brief 字符串比较（语义同 strcmp，按无符号字节序）。
 * @param lhs 左侧字符串。
 * @param rhs 右侧字符串。
 * @return lhs 小于/等于/大于 rhs 时分别返回负值/0/正值。
 */
int XStrcmp(const char* lhs, const char* rhs);

/**
 * @brief 定长字符串比较（语义同 strncmp）。
 * @param lhs 左侧字符串。
 * @param rhs 右侧字符串。
 * @param n 最多比较字节数。
 * @return 差值语义同 XStrcmp；前 n 字节相等返回 0。
 */
int XStrncmp(const char* lhs, const char* rhs, size_t n);

/**
 * @brief 子串查找（语义同 strstr；needle 空串返回 haystack）。
 * @param haystack 被搜索的字符串。
 * @param needle 要查找的子串。
 * @return 首次出现位置指针；未找到返回 NULL。
 */
const char* XStrstr(const char* haystack, const char* needle);

/**
 * @brief 首个指定字符查找（语义同 strchr，含终止符位置）。
 * @param str 被搜索的字符串。
 * @param ch 目标字符。
 * @return 首次出现位置指针；未找到返回 NULL。
 */
const char* XStrchr(const char* str, int ch);

/**
 * @brief 末个指定字符查找（语义同 strrchr）。
 * @param str 被搜索的字符串。
 * @param ch 目标字符。
 * @return 最后一次出现位置指针；未找到返回 NULL。
 */
const char* XStrrchr(const char* str, int ch);

/**
 * @brief 字符串复制（语义同 strcpy；调用方保证目标容量）。
 * @param dest 目标缓冲区。
 * @param src 源字符串。
 * @return dest。
 */
char* XStrcpy(char* dest, const char* src);

/**
 * @brief 定长字符串复制（语义同 strncpy：不足 n 字节补零）。
 * @param dest 目标缓冲区，须至少有 n 字节。
 * @param src 源字符串；NULL 时全部补零。
 * @param n 目标字节数。
 * @return dest。
 */
char* XStrncpy(char* dest, const char* src, size_t n);

/**
 * @brief 可重入分词器（C 标准 strtok 的库内等价物，无静态状态，线程安全）。
 * @param str 首次调用传入待分词缓冲区；后续调用传 NULL 沿用上次位置。
 * @param delim 分隔符字符集合（其中任一字符均视为分隔符）。
 * @param savePtr 分词状态，由调用方持有；首次调用前初始化为 NULL。
 * @return 下一个 token；无更多 token 返回 NULL。
 * @note 语义与 strtok 对齐：跳过前导分隔符、不产生空 token、会向缓冲区
 *       写入 '\0'（请传入可写的副本缓冲）。相比 strtok 无静态状态，
 *       可安全用于多线程上下文。Src 禁用 C 标准 strtok（外部依赖约束）。
 */
char* XStrtokReentrant(char* str, const char* delim, char** savePtr);

/**
 * @brief 字符串追加（语义同 strcat；调用方保证目标容量）。
 * @param dest 目标字符串，须以 '\0' 结尾。
 * @param src 追加内容。
 * @return dest。
 */
char* XStrcat(char* dest, const char* src);

/**
 * @brief 定长字符串追加（语义同 strncat：最多追加 n 字符并补终止符）。
 * @param dest 目标字符串。
 * @param src 追加内容。
 * @param n 最多追加字符数。
 * @return dest。
 */
char* XStrncat(char* dest, const char* src, size_t n);

/**
 * @brief ASCII 空白判定（空格与 \t\n\v\f\r，语义同 isspace）。
 * @param ch 待判定字符。
 * @return 是空白返回非 0，否则返回 0。
 */
int XIsSpace(int ch);

/**
 * @brief ASCII 数字判定（'0'-'9'，语义同 isdigit）。
 * @param ch 待判定字符。
 * @return 是数字返回非 0，否则返回 0。
 */
int XIsDigit(int ch);

/**
 * @brief ASCII 字母判定（语义同 isalpha）。
 * @param ch 待判定字符。
 * @return 是字母返回非 0，否则返回 0。
 */
int XIsAlpha(int ch);

/**
 * @brief ASCII 字母或数字判定（语义同 isalnum）。
 * @param ch 待判定字符。
 * @return 是字母或数字返回非 0，否则返回 0。
 */
int XIsAlnum(int ch);

/**
 * @brief ASCII 小写折叠（语义同 tolower，非大写原样返回）。
 * @param ch 待转换字符。
 * @return 小写字符。
 */
int XToLower(int ch);

/**
 * @brief ASCII 大写折叠（语义同 toupper，非小写原样返回）。
 * @param ch 待转换字符。
 * @return 大写字符。
 */
int XToUpper(int ch);

/**
 * @brief 忽略大小写字符串比较（语义同 strcasecmp，ASCII 折叠）。
 * @param lhs 左侧字符串。
 * @param rhs 右侧字符串。
 * @return 折叠后比较结果：负值/0/正值。
 */
int XStrcasecmp(const char* lhs, const char* rhs);

/**
 * @brief 定长忽略大小写比较（语义同 strncasecmp，ASCII 折叠）。
 * @param lhs 左侧字符串。
 * @param rhs 右侧字符串。
 * @param n 最多比较字节数。
 * @return 折叠后比较结果：负值/0/正值。
 */
int XStrncasecmp(const char* lhs, const char* rhs, size_t n);

/**
 * @brief 长整型解析（对标 strtol：指定进制 2-36、支持正负号、跳过前导空白）。
 * @param str 数字文本。
 * @param endptr 可选输出，接收解析结束位置（可为 NULL）。
 * @param base 进制，2-36；0 视为 10。
 * @return 解析出的长整型；无有效数字时返回 0。
 * @note 无溢出检测；十进制转换优先使用 str_to_int32/int64。
 */
long XStrtol(const char* str, char** endptr, int base);

/**
 * @brief 双精度解析（strtod 十进制子集：整数/小数/指数；前缀解析）。
 * @param str 数字文本。
 * @param endptr 可选输出，接收解析结束位置（可为 NULL）。
 * @return 解析出的双精度值；无有效数字时返回 0.0。
 */
double XStrtod(const char* str, char** endptr);



/**
 * @brief 格式化写入缓冲区（snprintf 语义）。
 *
 * @details 当前内部委托标准库 vsnprintf 实现（行为与 snprintf 完全一致）；
 *         保留独立命名以便未来在不依赖标准库的目标上替换为自研引擎，
 *         调用方无需改动。
 * @param buf 目标缓冲区。
 * @param size 缓冲区容量；内容总是以 '\0' 结尾（size>0 时）。
 * @param format 格式串（同 printf 族）。
 * @return 应写入的字符数（不含 '\0'）；缓冲区不足时返回所需长度。
 */
int XSnprintf(char* buf, size_t size, const char* format, ...);


/**
 * @brief 格式化输入（sscanf 语义）。
 *
 * @details 当前内部委托标准库 vsscanf 实现（行为与 sscanf 完全一致）；
 *         保留独立命名以便未来在不依赖标准库的目标上替换为自研引擎，
 *         调用方无需改动。
 * @param str 输入字符串。
 * @param format 格式串（同 scanf 族）。
 * @return 成功赋值的输入项数；输入或格式为空返回 EOF（-1）。
 */
int XSscanf(const char* str, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif /* !XSTRINGUTILS_H */
