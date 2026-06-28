#include"XPrintf.h"
#include"XString.h"
#include"XByteArray.h"
int XPrintf_2(const XString* str)
{
    if (str == NULL)
        return 0;
    printf("%s", XString_toLocal(str));
    return XString_length_base(str);
}

int XPrintf_3(const char* utf8_str)
{
    if (!utf8_str) return 0;  // 空指针安全处理

#ifdef _WIN32
    // Windows平台：UTF-8 -> GBK 转换后输出
    // 1. 计算所需GBK缓冲区大小
    int64_t gbk_len = XChar_utf8ToGbkStream(utf8_str, 0, NULL, 0);
    if (gbk_len <= 0) return 0;  // 转换失败

    // 2. 分配GBK缓冲区（+1用于终止符）
    char* gbk_buf = (char*)XMalloc_System(gbk_len + 1);
    if (!gbk_buf) return 0;

    // 3. 执行UTF-8到GBK的转换
    if (XChar_utf8ToGbkStream(utf8_str, 0, gbk_buf, gbk_len + 1) <= 0) {
        XFree_System(gbk_buf);
        return 0;
    }

    // 4. 输出GBK字符串并释放资源
    int result = printf("%s", gbk_buf);
    XFree_System(gbk_buf);
    return result;

#else
    // Linux平台：直接输出UTF-8（系统默认支持）
    return printf("%s", utf8_str);
#endif
}

int XPrintf_4(const XByteArray* array)
{
    if(!array||XByteArray_isEmpty_base(array))
        return 0;
    size_t len = XByteArray_size_base(array);
    for_each_iterator(array, XByteArray,it)
    {
        char c = XByteArray_iterator_data(&it);
        putchar(c);
    }
    return len;
}

int XPrintf(const char* format, ...)
{
    if (!format) return 0;

    va_list args;
    va_start(args, format);  // 初始化原始参数列表

    // 步骤1：计算所需UTF-8缓冲区大小（使用复制的参数列表）
    va_list args_copy;       // 定义用于复制的参数列表
    va_copy(args_copy, args); // 复制原始参数列表
    int utf8_len = vsnprintf(NULL, 0, format, args_copy) + 1;  // 用复制的列表计算长度
    va_end(args_copy);       // 释放复制的列表（必须在使用后结束）

    if (utf8_len <= 0)
    {
        va_end(args);        // 释放原始列表
        return 0;
    }

    // 1.2 分配缓冲区并格式化UTF-8内容（使用原始参数列表）
    char* utf8_buf = (char*)XMalloc_System(utf8_len);
    if (!utf8_buf)
    {
        va_end(args);
        return 0;
    }

    // 第二次调用使用原始参数列表（未被消耗）
    vsnprintf(utf8_buf, utf8_len, format, args);
    va_end(args);  // 释放原始列表

    // 步骤2：根据平台转换为本地编码并输出（后续逻辑不变）
    int result = 0;
#ifdef _WIN32
    // Windows：UTF-8 → GBK（保持不变）
    int gbk_len = XChar_utf8ToGbkStream(utf8_buf, 0, NULL, 0);
    if (gbk_len <= 0)
    {
        XFree_System(utf8_buf);
        return 0;
    }

    char* gbk_buf = (char*)XMalloc_System(gbk_len + 1);
    if (!gbk_buf)
    {
        XFree_System(utf8_buf);
        return 0;
    }

    if (XChar_utf8ToGbkStream(utf8_buf, 0, gbk_buf, gbk_len + 1) > 0)
    {
        result = printf("%s", gbk_buf);
    }
    XFree_System(gbk_buf);
#else
    // Linux：直接输出UTF-8
    result = printf("%s", utf8_buf);
#endif

    XFree_System(utf8_buf);
    return result;
}

int XPrintf_5(XChar* ch)
{
    if (!ch) return 0; // 空指针安全处理

    XChar chs[] = { *ch,0 };
    char buff[5] = { 0 };
    XChar_toLocalStream(&chs, 1, buff, 5);
    return printf(buff);
}