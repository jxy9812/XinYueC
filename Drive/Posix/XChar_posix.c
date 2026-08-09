/**
 * @file XChar_posix.c
 * @brief GBK编码转换的系统API模式实现（Posix平台）
 *
 * 使用iconv库实现GBK↔Unicode转换。
 * 依赖系统运行时iconv库支持。
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
#include "XChar_conf.h"
/* 只有在未定义文件模式时，才使用系统API模式 */
#if defined(XCHAR_USE_SYSTEM_GBK) && (!defined(XCHAR_USE_FILE_GBK))

#include "XChar.h"
#include "XMemory.h"
#include <iconv.h>
#include <string.h>
#include <errno.h>

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

/* ========================================================================== */
/*                           辅助函数                                           */
/* ========================================================================== */

/**
 * @brief 计算以'\0'结尾的字节串长度
 * @param data 输入数据
 * @param input_size 输入数据大小，0则自动检测NULL结尾
 * @return 实际数据长度
 */
static size_t xchar_input_len_char(const char* data, size_t input_size)
{
    if (input_size == 0) {
        size_t len = 0;
        while (data[len] != '\0') len++;
        return len;
    }
    for (size_t i = 0; i < input_size; i++) {
        if (data[i] == '\0') return i;
    }
    return input_size;
}

/**
 * @brief 计算XChar数组长度
 * @param data XChar数组
 * @param input_count 输入数量，0则自动检测终止符
 * @return 实际数组长度
 */
static size_t xchar_input_len(const XChar* data, size_t input_count)
{
    if (input_count == 0) {
        size_t len = 0;
        while (data[len] != 0) len++;
        return len;
    }
    for (size_t i = 0; i < input_count; i++) {
        if (data[i] == 0) return i;
    }
    return input_count;
}

/**
 * @brief 使用iconv进行编码转换
 * @param from_charset 源编码
 * @param to_charset 目标编码
 * @param inbuf 输入缓冲区
 * @param inbytesleft 输入字节数
 * @param outbuf 输出缓冲区（NULL时仅计算所需大小）
 * @param outbytesleft 输出缓冲区大小
 * @return 成功返回输出字节数，失败返回-1
 */
static int64_t xchar_iconv_convert(const char* from_charset, const char* to_charset,
                                   const char* inbuf, size_t inbytesleft,
                                   char* outbuf, size_t outbytesleft)
{
    iconv_t cd = iconv_open(to_charset, from_charset);
    if (cd == (iconv_t)-1) {
        return -1;
    }

    /* 重置errno */
    errno = 0;

    /* 创建输入缓冲区的副本（iconv会修改指针） */
    char* inptr = (char*)inbuf;
    size_t inbytes = inbytesleft;

    if (outbuf == NULL || outbytesleft == 0) {
        /* 计算所需输出缓冲区大小 */
        size_t estimated_size = inbytesleft * 4; /* 最坏情况：每个字节转换为4字节 */
        char* temp_buf = (char*)XMalloc_System(estimated_size);
        if (!temp_buf) {
            iconv_close(cd);
            return -1;
        }

        char* temp_out = temp_buf;
        size_t temp_outbytes = estimated_size;

        /* 重置输入指针 */
        inptr = (char*)inbuf;
        inbytes = inbytesleft;

        size_t result = iconv(cd, &inptr, &inbytes, &temp_out, &temp_outbytes);
        XFree_System(temp_buf);

        if (result == (size_t)-1 && errno != 0) {
            iconv_close(cd);
            return -1;
        }

        size_t written = estimated_size - temp_outbytes;
        iconv_close(cd);
        return (int64_t)written;
    }

    /* 实际转换 */
    char* outptr = outbuf;
    size_t outbytes = outbytesleft;

    size_t result = iconv(cd, &inptr, &inbytes, &outptr, &outbytes);

    if (result == (size_t)-1) {
        iconv_close(cd);
        return -1;
    }

    size_t written = outbytesleft - outbytes;
    iconv_close(cd);
    return (int64_t)written;
}

/**
 * @brief 从UTF-16LE编码转换为其他编码
 * @param utf16_data UTF-16LE数据
 * @param utf16_bytes UTF-16数据字节数
 * @param to_charset 目标编码
 * @param outbuf 输出缓冲区
 * @param outbytesleft 输出缓冲区大小
 * @return 成功返回输出字节数，失败返回-1
 */
static int64_t xchar_utf16le_to_charset(const XChar* utf16_data, size_t utf16_bytes,
                                        const char* to_charset, char* outbuf, size_t outbytesleft)
{
    return xchar_iconv_convert("UTF-16LE", to_charset,
                               (const char*)utf16_data, utf16_bytes,
                               outbuf, outbytesleft);
}

/**
 * @brief 从其他编码转换为UTF-16LE
 * @param from_charset 源编码
 * @param inbuf 输入缓冲区
 * @param inbytesleft 输入字节数
 * @param outbuf 输出缓冲区
 * @param outbytesleft 输出缓冲区大小
 * @return 成功返回输出字节数，失败返回-1
 */
static int64_t xchar_charset_to_utf16le(const char* from_charset,
                                        const char* inbuf, size_t inbytesleft,
                                        XChar* outbuf, size_t outbytesleft)
{
    return xchar_iconv_convert(from_charset, "UTF-16LE",
                               inbuf, inbytesleft,
                               (char*)outbuf, outbytesleft);
}

/* ========================================================================== */
/*                      平台抽象函数实现                                         */
/* ========================================================================== */

int64_t XCharPlatform_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out)
{
    if (!gbk) return -1;

    size_t actual_len = xchar_input_len_char(gbk, input_size);
    if (actual_len == 0) {
        if (out && max_out > 0) out[0] = 0;
        return 0;
    }

    /* 计算所需输出缓冲区大小 */
    int64_t required_bytes = xchar_charset_to_utf16le("GBK", gbk, actual_len, NULL, 0);
    if (required_bytes < 0) {
        /* GBK可能不被支持，尝试使用GB18030 */
        required_bytes = xchar_charset_to_utf16le("GB18030", gbk, actual_len, NULL, 0);
    }
    if (required_bytes < 0) {
        return -1;
    }

    /* UTF-16LE每个字符2字节，计算XChar数量 */
    int64_t required_chars = required_bytes / 2;

    if (!out || max_out == 0) {
        return required_chars;
    }

    if (max_out < (size_t)required_chars + 1) {
        return -1;
    }

    /* 执行转换 */
    int64_t written_bytes = xchar_charset_to_utf16le("GBK", gbk, actual_len, out, max_out * 2);
    if (written_bytes < 0) {
        written_bytes = xchar_charset_to_utf16le("GB18030", gbk, actual_len, out, max_out * 2);
    }
    if (written_bytes < 0) {
        return -1;
    }

    /* 设置终止符 */
    out[written_bytes / 2] = 0;
    return written_bytes / 2;
}

int64_t XCharPlatform_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    if (!ch) return -1;

    size_t actual_count = xchar_input_len(ch, input_count);
    if (actual_count == 0) {
        if (gbk && max_gbk > 0) gbk[0] = '\0';
        return 0;
    }

    /* UTF-16LE字节数 */
    size_t utf16_bytes = actual_count * 2;

    /* 计算所需输出缓冲区大小 */
    int64_t required = xchar_utf16le_to_charset(ch, utf16_bytes, "GBK", NULL, 0);
    if (required < 0) {
        required = xchar_utf16le_to_charset(ch, utf16_bytes, "GB18030", NULL, 0);
    }
    if (required < 0) {
        return -1;
    }

    if (!gbk || max_gbk == 0) {
        return required;
    }

    if (max_gbk < (size_t)required + 1) {
        return -1;
    }

    /* 执行转换 */
    int64_t written = xchar_utf16le_to_charset(ch, utf16_bytes, "GBK", gbk, max_gbk - 1);
    if (written < 0) {
        written = xchar_utf16le_to_charset(ch, utf16_bytes, "GB18030", gbk, max_gbk - 1);
    }
    if (written < 0) {
        return -1;
    }

    gbk[written] = '\0';
    return written;
}

int64_t XCharPlatform_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
{
    if (!utf8_str) return -1;

    size_t utf8_len = xchar_input_len_char(utf8_str, input_size);
    if (utf8_len == 0) {
        if (gbk_buf && max_len > 0) gbk_buf[0] = '\0';
        return 0;
    }

    /* 计算所需GBK缓冲区大小 */
    int64_t required = xchar_iconv_convert("UTF-8", "GBK", utf8_str, utf8_len, NULL, 0);
    if (required < 0) {
        required = xchar_iconv_convert("UTF-8", "GB18030", utf8_str, utf8_len, NULL, 0);
    }
    if (required < 0) {
        return -1;
    }

    if (!gbk_buf || max_len == 0) {
        return required;
    }

    if (max_len < (size_t)required + 1) {
        return -1;
    }

    /* 执行转换 */
    int64_t written = xchar_iconv_convert("UTF-8", "GBK", utf8_str, utf8_len, gbk_buf, max_len - 1);
    if (written < 0) {
        written = xchar_iconv_convert("UTF-8", "GB18030", utf8_str, utf8_len, gbk_buf, max_len - 1);
    }
    if (written < 0) {
        return -1;
    }

    gbk_buf[written] = '\0';
    return written;
}

int64_t XCharPlatform_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
{
    if (!gbk_str) return -1;

    size_t gbk_len = xchar_input_len_char(gbk_str, input_size);
    if (gbk_len == 0) {
        if (utf8_buf && max_len > 0) utf8_buf[0] = '\0';
        return 0;
    }

    /* 计算所需UTF-8缓冲区大小 */
    int64_t required = xchar_iconv_convert("GBK", "UTF-8", gbk_str, gbk_len, NULL, 0);
    if (required < 0) {
        required = xchar_iconv_convert("GB18030", "UTF-8", gbk_str, gbk_len, NULL, 0);
    }
    if (required < 0) {
        return -1;
    }

    if (!utf8_buf || max_len == 0) {
        return required;
    }

    if (max_len < (size_t)required + 1) {
        return -1;
    }

    /* 执行转换 */
    int64_t written = xchar_iconv_convert("GBK", "UTF-8", gbk_str, gbk_len, utf8_buf, max_len - 1);
    if (written < 0) {
        written = xchar_iconv_convert("GB18030", "UTF-8", gbk_str, gbk_len, utf8_buf, max_len - 1);
    }
    if (written < 0) {
        return -1;
    }

    utf8_buf[written] = '\0';
    return written;
}

#endif /* XCHAR_USE_SYSTEM_GBK && (!XCHAR_USE_FILE_GBK) */
#endif /* __linux__ || __APPLE__ || __BSD__ */