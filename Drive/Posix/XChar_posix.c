/**
 * @file XChar_posix.c
 * @brief GBK编码转换的系统API模式实现（Posix平台）
 *
 * Posix平台（Linux/macOS/BSD）本地编码为UTF-8，不原生支持GBK。
 * 此文件提供桩实现，返回失败。
 *
 * 在Posix平台上，建议使用：
 *   - XCHAR_USE_CODE_GBK：代码模式（静态数组）
 *   - XCHAR_USE_FILE_GBK：文件模式（读取外部文件）
 *
 * 使用方式：在编译配置中定义 XCHAR_USE_SYSTEM_GBK 宏启用此实现。
 * 
 * 三种模式优先级（从高到低）：
 *   1. XCHAR_USE_CODE_GBK   - 代码模式（静态数组）
 *   2. XCHAR_USE_FILE_GBK   - 文件模式（读取外部文件）
 *   3. XCHAR_USE_SYSTEM_GBK - 系统API模式（调用系统API）
 *
 * 注意：同一时间只能启用一种模式。
 */

#if defined(__linux__) || defined(__APPLE__) || defined(__BSD__)

/* 只有在未定义代码模式和文件模式时，才使用系统API模式 */
#if defined(XCHAR_USE_SYSTEM_GBK) || (!defined(XCHAR_USE_CODE_GBK) && !defined(XCHAR_USE_FILE_GBK))

#include "XChar.h"
/* ========================================================================== */
/*                      平台抽象函数（在对应平台目录实现）                         */
/* ========================================================================== */

/**
 * @brief 从GBK编码字符串转换为XChar数组（平台实现）
 * @param gbk GBK编码字符串
 * @param input_size 输入数据大小（字节），0则自动检测NULL结尾
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XCharPlatform_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为GBK编码字符串（平台实现）
 * @param ch XChar数组（以code=0为终止符）
 * @param input_count 输入XChar数量，0则自动检测
 * @param gbk 输出的GBK编码字符串缓冲区
 * @param max_gbk 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XCharPlatform_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk);

/**
 * @brief UTF-8转GBK编码（平台实现）
 * @param utf8_str 输入UTF-8字符串
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param gbk_buf 输出GBK缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回GBK字节数（不含终止符），失败返回-1
 */
int64_t XCharPlatform_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len);

/**
 * @brief GBK转UTF-8编码（平台实现）
 * @param gbk_str 输入GBK字符串
 * @param input_size 输入数据大小（字节），0则自动检测
 * @param utf8_buf 输出UTF-8缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回UTF-8字节数（不含终止符），失败返回-1
 */
int64_t XCharPlatform_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len);
/* Posix平台GBK桩实现，Linux下本地编码为UTF-8 */

int64_t XCharPlatform_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out)
{
    (void)gbk; (void)input_size; (void)out; (void)max_out;
    return -1; /* Posix平台不支持GBK，返回失败 */
}

int64_t XCharPlatform_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    (void)ch; (void)input_count; (void)gbk; (void)max_gbk;
    return -1; /* Posix平台不支持GBK，返回失败 */
}

int64_t XCharPlatform_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
{
    (void)utf8_str; (void)input_size; (void)gbk_buf; (void)max_len;
    return -1; /* Posix平台不支持GBK，返回失败 */
}

int64_t XCharPlatform_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
{
    (void)gbk_str; (void)input_size; (void)utf8_buf; (void)max_len;
    return -1; /* Posix平台不支持GBK，返回失败 */
}

#endif /* XCHAR_USE_SYSTEM_GBK || (!XCHAR_USE_CODE_GBK && !XCHAR_USE_FILE_GBK) */
#endif /* __linux__ || __APPLE__ || __BSD__ */
