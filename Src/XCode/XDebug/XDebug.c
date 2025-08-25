#include "XDebug.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

//#if DEBUG_ON || defined(_DEBUG)

// 初始化XDebug结构体
static void XDebug_init(XDebug* debug, const char* file, const char* function, int line) {
    XVector_init(&debug->buffer, sizeof(char));
    debug->target = XDEBUG_TARGET_STDOUT;
    debug->file_fd = 1;
    debug->auto_newline = true;
    debug->is_active = true;
    debug->show_location = false;
    debug->auto_space = false;  // 默认不自动添加空格（新增）
    debug->file = file;
    debug->function = function;
    debug->line = line;
}

// 创建XDebug实例（带位置信息）
XDebug* XDebug_create_with_location_(const char* file, const char* function, int line) {
    XDebug* debug = (XDebug*)XMemory_malloc(sizeof(XDebug));
    if (!debug) return NULL;

    XDebug_init(debug, file, function, line);
    return debug;
}

// 销毁XDebug实例
void XDebug_delete_(XDebug* debug) {
    if (!debug) return;

    XVector_deinit_base(&debug->buffer);
    XMemory_free(debug);
}

// 设置输出目标
XDebug* XDebug_setTarget_(XDebug* debug, XDebugTarget target, int fd) {
    if (!debug || !debug->is_active) return debug;

    debug->target = target;
    if (target == XDEBUG_TARGET_FILE) {
        debug->file_fd = fd;
    }
    return debug;
}

// 启用/禁用自动换行
XDebug* XDebug_setAutoNewline_(XDebug* debug, bool enable) {
    if (!debug || !debug->is_active) return debug;
    debug->auto_newline = enable;
    return debug;
}

// 启用/禁用位置信息显示
XDebug* XDebug_setShowLocation_(XDebug* debug, bool enable) {
    if (!debug || !debug->is_active) return debug;
    debug->show_location = enable;
    return debug;
}

XDebug* XDebug_setAutoSpace_(XDebug* debug, bool enable) {
    if (!debug || !debug->is_active) return debug;
    debug->auto_space = enable;
    return debug;
}

// 基础写入函数
XDebug* XDebug_write_(XDebug* debug, const char* data, size_t len) {
    if (!debug || !data || len == 0 || !debug->is_active) return debug;

    for (size_t i = 0; i < len; i++) {
        XVector_push_back_base(&debug->buffer, &data[i]);
    }
    return debug;
}

// 输出字符串
XDebug* XDebug_puts_(XDebug* debug, const char* str) {
    if (!str) return debug;
    return XDebug_write_(debug, str, strlen(str));
}

// 输出字符
XDebug* XDebug_putc_(XDebug* debug, char c) {
    return XDebug_write_(debug, &c, 1);
}

// 格式化输出（可变参数）
XDebug* XDebug_printf_(XDebug* debug, const char* format, ...) {
    if (!debug || !format || !debug->is_active) return debug;

    va_list args;
    va_start(args, format);
    XDebug_vprintf_(debug, format, args);
    va_end(args);
    return debug;
}

// 格式化输出（va_list）
XDebug* XDebug_vprintf_(XDebug* debug, const char* format, va_list args) {
    if (!debug || !format || !debug->is_active) return debug;

    // 计算所需缓冲区大小
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    if (len <= 0) return debug;

    // 分配临时缓冲区
    char* temp = (char*)XMemory_malloc(len + 1);
    if (!temp) return debug;

    // 格式化字符串
    vsnprintf(temp, len + 1, format, args);
    //
      // 步骤2：根据平台转换为本地编码并输出
#ifdef _WIN32
    // Windows：UTF-8 → GBK
    int gbk_len = XUTF8_to_gbk_stream(temp, 0, NULL, 0);  // 获取所需GBK长度
    if (gbk_len <= 0)
    {
        XMemory_free(temp);
        return 0;
    }

    char* gbk_buf = (char*)XMemory_malloc(gbk_len + 1);  // +1 终止符
    if (!gbk_buf)
    {
        XMemory_free(temp);
        return 0;
    }

    if (XUTF8_to_gbk_stream(temp, 0, gbk_buf, gbk_len + 1) > 0)
    {
        //result = printf("%s", gbk_buf);  // 输出GBK
    }
    XMemory_free(temp);
    temp = gbk_buf;
    len = gbk_len;
#else
    // Linux：直接输出UTF-8（本地编码兼容）
   /* result = printf("%s", utf8_buf);*/
#endif

    // 写入XDebug缓冲区
    XDebug_write_(debug, temp, len);
    XMemory_free(temp);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 布尔值输出
XDebug* XDebug_bool_(XDebug* debug, bool value) 
{
    XDebug_puts_(debug, value ? "true" : "false");
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 字符输出
XDebug* XDebug_char_(XDebug* debug, char value) {
    XDebug_printf_(debug, "%c", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 8位整数输出
XDebug* XDebug_int8_(XDebug* debug, int8_t value) {
    XDebug_printf_(debug, "%hhd", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_uint8_(XDebug* debug, uint8_t value) {
    XDebug_printf_(debug, "%hhu", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 16位整数输出
XDebug* XDebug_int16_(XDebug* debug, int16_t value) {
    XDebug_printf_(debug, "%hd", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_uint16_(XDebug* debug, uint16_t value) {
    XDebug_printf_(debug, "%hu", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 32位整数输出
XDebug* XDebug_int32_(XDebug* debug, int32_t value) {
    XDebug_printf_(debug, "%d", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_uint32_(XDebug* debug, uint32_t value) {
    XDebug_printf_(debug, "%u", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 64位整数输出
XDebug* XDebug_int64_(XDebug* debug, int64_t value) {
    XDebug_printf_(debug, "%lld", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_uint64_(XDebug* debug, uint64_t value) {
    XDebug_printf_(debug, "%lld", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 浮点数输出
XDebug* XDebug_float_(XDebug* debug, float value) {
    XDebug_printf_(debug, "%f", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_double_(XDebug* debug, double value) {
    XDebug_printf_(debug, "%lf", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 指针输出
XDebug* XDebug_ptr_(XDebug* debug, const void* ptr) {
    XDebug_printf_(debug, "%p", ptr);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// 十六进制输出
XDebug* XDebug_hex_(XDebug* debug, uint64_t value) {
    XDebug_printf_(debug, "0x%llx", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_hex8_(XDebug* debug, uint8_t value) {
    XDebug_printf_(debug, "0x%02x", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_hex16_(XDebug* debug, uint16_t value) {
    XDebug_printf_(debug, "0x%04x", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_hex32_(XDebug* debug, uint32_t value) {
    XDebug_printf_(debug, "0x%08x", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

XDebug* XDebug_hex64_(XDebug* debug, uint64_t value) {
    XDebug_printf_(debug, "0x%016llx", value);
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// XString输出
XDebug* XDebug_XString_(XDebug* debug, const XString* str) {
    if (!str) return XDebug_puts_(debug, "(null XString)");
    XDebug_printf_(debug, "\"%s\" (length: %zu)",
        XString_toUtf8(str),
        XString_length_base(str));
    if (debug->auto_space) {  // 新增
        XDebug_space_(debug);
    }
    return debug;
}

// XVector输出（需要元素打印函数）
XDebug* XDebug_XVector_(XDebug* debug, const XVector* vec,
    void (*print_elem)(XDebug*, const void*)) {
    if (!vec) return XDebug_puts_(debug, "(null XVector)");
    if (!print_elem) return XDebug_puts_(debug, "(no element printer)");

    XDebug_puts_(debug, "XVector [ ");
    XDebug_printf_(debug, "size: %zu, capacity: %zu, elements: [ ",
        XVector_size_base(vec),
        XVector_capacity_base(vec));

    size_t type_size = XVector_typeSize_base(vec);
    for (size_t i = 0; i < XVector_size_base(vec); i++) {
        const void* elem = (const char*)XContainerDataPtr(vec) + i * type_size;
        print_elem(debug, elem);
        if (i != XVector_size_base(vec) - 1) {
            XDebug_puts_(debug, ", ");
        }
    }

    XDebug_puts_(debug, " ] ]");
    return debug;
}

// XListSLinked输出（需要元素打印函数）
XDebug* XDebug_XListSLinked_(XDebug* debug, const XListSLinked* list,
    void (*print_elem)(XDebug*, const void*)) {
    if (!list) return XDebug_puts_(debug, "(null XListSLinked)");
    if (!print_elem) return XDebug_puts_(debug, "(no element printer)");

    XDebug_puts_(debug, "XListSLinked [ size: %zu, elements: [ ");

    // 遍历链表
  /*  XListSLinkedIterator it;
    XListSLinked_iterator_init(&it, (XListSLinked*)list);
    while (XListSLinked_iterator_valid(&it)) {
        const void* elem = XListSLinked_iterator_data(&it);
        print_elem(debug, elem);

        if (!XListSLinked_iterator_isLast(&it)) {
            XDebug_puts_(debug, ", ");
        }
        XListSLinked_iterator_next(&it);
    }

    XDebug_puts_(debug, " ] ]");*/
    return debug;
}

// 输出空格
XDebug* XDebug_space_(XDebug* debug) {
    return XDebug_putc_(debug, ' ');
}

// 取消后续空格（空实现，实际使用中可控制格式化）
XDebug* XDebug_nospace_(XDebug* debug) {
    return debug;  // 可根据需要实现空格控制逻辑
}

// 输出换行
XDebug* XDebug_newline_(XDebug* debug) {
    return XDebug_putc_(debug, '\n');
}

// 刷新输出
XDebug* XDebug_flush_(XDebug* debug) {
    if (!debug || !debug->is_active || XVector_isEmpty_base(&debug->buffer)) {
        return debug;
    }

    // 准备输出内容
    size_t buf_size = XVector_size_base(&debug->buffer);
    char* output = (char*)XMemory_malloc(buf_size + 2);  // +2 预留换行和结束符
    if (!output) return debug;

    // 复制缓冲区内容
    memcpy(output, XContainerDataPtr(&debug->buffer), buf_size);
    output[buf_size] = '\0';

    // 处理位置信息
    char location[256] = { 0 };
    if (debug->show_location) {
        snprintf(location, sizeof(location), "[%s:%s:%d] ",
            debug->file, debug->function, debug->line);
    }

    // 处理自动换行
    char* final_output = output;
    size_t final_len = buf_size;
    if (debug->auto_newline && buf_size > 0 && output[buf_size - 1] != '\n') {
        output[buf_size] = '\n';
        output[buf_size + 1] = '\0';
        final_len = buf_size + 1;
    }

    // 根据目标输出
    switch (debug->target) {
    case XDEBUG_TARGET_STDOUT:
        printf("%s%s", location, final_output);
        fflush(stdout);
        break;
    case XDEBUG_TARGET_STDERR:
        fprintf(stderr, "%s%s", location, final_output);
        fflush(stderr);
        break;
    case XDEBUG_TARGET_FILE:
        if (debug->file_fd != -1) {
        /*    write(debug->file_fd, location, strlen(location));
            write(debug->file_fd, final_output, final_len);*/
        }
        break;
    case XDEBUG_TARGET_BUFFER:
        // 缓冲区模式下不输出，仅保留在buffer中
        break;
    }

    // 重置缓冲区
    XVector_clear_base(&debug->buffer);
    XMemory_free(output);
    return debug;
}

// 重置缓冲区
XDebug* XDebug_reset_(XDebug* debug) {
    if (!debug) return debug;
    XVector_clear_base(&debug->buffer);
    return debug;
}

//#endif  // DEBUG_ON || _DEBUG