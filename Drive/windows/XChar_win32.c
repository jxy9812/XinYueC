#ifdef _WIN32
#include "XChar.h"
#include "XMemory.h"
#include <windows.h>
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

/* 计算以'\0'结尾的字节串长度 */
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

/* 计算XChar数组长度 */
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

int64_t XCharPlatform_fromGbkStream(const char* gbk, size_t input_size, XChar* out, size_t max_out)
{
    if (!gbk) return -1;
    size_t actual_len = xchar_input_len_char(gbk, input_size);
    if (actual_len == 0) { if (out && max_out > 0) out[0] = 0; return 0; }

    int wchar_count = MultiByteToWideChar(CP_ACP, 0, gbk, (int)(actual_len + 1), NULL, 0);
    if (wchar_count <= 0) return -1;
    int64_t required = (int64_t)(wchar_count - 1);
    if (!out || max_out == 0) return required;
    if (max_out < (size_t)wchar_count) return -1;
    if (MultiByteToWideChar(CP_ACP, 0, gbk, (int)(actual_len + 1), (wchar_t*)out, (int)max_out) <= 0) return -1;
    out[wchar_count - 1] = 0;
    return required;
}

int64_t XCharPlatform_toGbkStream(const XChar* ch, size_t input_count, char* gbk, size_t max_gbk)
{
    if (!ch) return -1;
    size_t actual_count = xchar_input_len(ch, input_count);
    if (actual_count == 0) { if (gbk && max_gbk > 0) gbk[0] = '\0'; return 0; }

    int gbk_len = WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)ch, (int)(actual_count + 1), NULL, 0, NULL, NULL);
    if (gbk_len <= 0) return -1;
    int64_t required = (int64_t)(gbk_len - 1);
    if (!gbk || max_gbk == 0) return required;
    if (max_gbk < (size_t)gbk_len) return -1;
    if (WideCharToMultiByte(CP_ACP, 0, (const wchar_t*)ch, (int)(actual_count + 1), gbk, (int)max_gbk, NULL, NULL) <= 0) return -1;
    gbk[gbk_len - 1] = '\0';
    return required;
}

int64_t XCharPlatform_utf8ToGbkStream(const char* utf8_str, size_t input_size, char* gbk_buf, size_t max_len)
{
    if (!utf8_str) return -1;
    size_t utf8_len = xchar_input_len_char(utf8_str, input_size);

    int wchar_len = MultiByteToWideChar(CP_UTF8, 0, utf8_str, (int)utf8_len, NULL, 0);
    if (wchar_len <= 0) return -1;
    wchar_t* wstr = (wchar_t*)XMalloc_System(sizeof(wchar_t) * (wchar_len + 1));
    if (!wstr) return -1;
    if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, (int)utf8_len, wstr, wchar_len) != wchar_len) {
        XFree_System(wstr); return -1;
    }
    wstr[wchar_len] = L'\0';

    int gbk_len = WideCharToMultiByte(CP_ACP, 0, wstr, wchar_len, NULL, 0, NULL, NULL);
    if (gbk_len <= 0) { XFree_System(wstr); return -1; }
    if (!gbk_buf || max_len == 0) { XFree_System(wstr); return (int64_t)gbk_len; }
    if (max_len < (size_t)gbk_len + 1) { XFree_System(wstr); return -1; }
    if (WideCharToMultiByte(CP_ACP, 0, wstr, wchar_len, gbk_buf, gbk_len, NULL, NULL) != gbk_len) {
        XFree_System(wstr); return -1;
    }
    gbk_buf[gbk_len] = '\0';
    XFree_System(wstr);
    return (int64_t)gbk_len;
}

int64_t XCharPlatform_gbkToUtf8Stream(const char* gbk_str, size_t input_size, char* utf8_buf, size_t max_len)
{
    if (!gbk_str) return -1;
    size_t gbk_len = xchar_input_len_char(gbk_str, input_size);

    int wchar_len = MultiByteToWideChar(CP_ACP, 0, gbk_str, (int)gbk_len, NULL, 0);
    if (wchar_len <= 0) return -1;
    wchar_t* wstr = (wchar_t*)XMalloc_System(sizeof(wchar_t) * (wchar_len + 1));
    if (!wstr) return -1;
    if (MultiByteToWideChar(CP_ACP, 0, gbk_str, (int)gbk_len, wstr, wchar_len) != wchar_len) {
        XFree_System(wstr); return -1;
    }
    wstr[wchar_len] = L'\0';

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wstr, wchar_len, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) { XFree_System(wstr); return -1; }
    if (!utf8_buf || max_len == 0) { XFree_System(wstr); return (int64_t)utf8_len; }
    if (max_len < (size_t)utf8_len + 1) { XFree_System(wstr); return -1; }
    if (WideCharToMultiByte(CP_UTF8, 0, wstr, wchar_len, utf8_buf, utf8_len, NULL, NULL) != utf8_len) {
        XFree_System(wstr); return -1;
    }
    utf8_buf[utf8_len] = '\0';
    XFree_System(wstr);
    return (int64_t)utf8_len;
}

#endif /* _WIN32 */