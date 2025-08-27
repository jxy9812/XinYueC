#include"XPrintf.h"
#include"XString.h"
int XPrintf_string(const XString* str)
{
    if (str == NULL)
        return 0;
    printf("%s", XString_toLocal(str));
    return XString_length_base(str);
}

int XPrintf_utf8(const char* utf8_str)
{
    if (!utf8_str) return 0;  // 空指针安全处理

#ifdef _WIN32
    // Windows平台：UTF-8 -> GBK 转换后输出
    // 1. 计算所需GBK缓冲区大小
    int64_t gbk_len = XUTF8_to_gbk_stream(utf8_str, 0, NULL, 0);
    if (gbk_len <= 0) return 0;  // 转换失败

    // 2. 分配GBK缓冲区（+1用于终止符）
    char* gbk_buf = (char*)XMemory_malloc(gbk_len + 1);
    if (!gbk_buf) return 0;

    // 3. 执行UTF-8到GBK的转换
    if (XUTF8_to_gbk_stream(utf8_str, 0, gbk_buf, gbk_len + 1) <= 0) {
        XMemory_free(gbk_buf);
        return 0;
    }

    // 4. 输出GBK字符串并释放资源
    int result = printf("%s", gbk_buf);
    XMemory_free(gbk_buf);
    return result;

#else
    // Linux平台：直接输出UTF-8（系统默认支持）
    return printf("%s", utf8_str);
#endif
}

int XPrintf(const char* format, ...)
{
    if (!format) return 0;

    va_list args;
    va_start(args, format);

    // 步骤1：先将所有内容格式化为UTF-8字符串
    // 1.1 计算所需UTF-8缓冲区大小
    int utf8_len = vsnprintf(NULL, 0, format, args) + 1;  // +1 包含终止符
    if (utf8_len <= 0)
    {
        va_end(args);
        return 0;
    }

    // 1.2 分配缓冲区并格式化UTF-8内容
    char* utf8_buf = (char*)XMemory_malloc(utf8_len);
    if (!utf8_buf)
    {
        va_end(args);
        return 0;
    }
    vsnprintf(utf8_buf, utf8_len, format, args);
    va_end(args);

    // 步骤2：根据平台转换为本地编码并输出
    int result = 0;
#ifdef _WIN32
    // Windows：UTF-8 → GBK
    int gbk_len = XUTF8_to_gbk_stream(utf8_buf, 0, NULL, 0);  // 获取所需GBK长度
    if (gbk_len <= 0)
    {
        XMemory_free(utf8_buf);
        return 0;
    }

    char* gbk_buf = (char*)XMemory_malloc(gbk_len + 1);  // +1 终止符
    if (!gbk_buf)
    {
        XMemory_free(utf8_buf);
        return 0;
    }

    if (XUTF8_to_gbk_stream(utf8_buf, 0, gbk_buf, gbk_len + 1) > 0)
    {
        result = printf("%s", gbk_buf);  // 输出GBK
    }
    XMemory_free(gbk_buf);
#else
    // Linux：直接输出UTF-8（本地编码兼容）
    result = printf("%s", utf8_buf);
#endif

    XMemory_free(utf8_buf);
    return result;
}

int XPrintf_char(XChar* ch)
{
    if (!ch) return 0; // 空指针安全处理

    XChar chs[] = { *ch,0 };
    char buff[5] = { 0 };
    XChar_to_local_stream(&chs, 1, buff, 5);
    return printf(buff);
}