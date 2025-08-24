#include "XDebug.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>

#if DEBUG_ON || defined(_DEBUG)

// 初始化XDebug结构体
static void XDebug_init(XDebug* debug, const char* file, const char* function, int line) {
    XVector_init(&debug->buffer, sizeof(char));
    debug->target = XDEBUG_TARGET_STDOUT;
    //debug->file_fd = STDOUT_FILENO;
    debug->auto_newline = true;
    debug->is_active = true;
    debug->show_location = false;
    debug->file = file;
    debug->function = function;
    debug->line = line;
}

// 创建XDebug实例（带位置信息）
XDebug* XDebug_create_with_location(const char* file, const char* function, int line) {
    XDebug* debug = (XDebug*)XMemory_malloc(sizeof(XDebug));
    if (!debug) return NULL;

    XDebug_init(debug, file, function, line);
    return debug;
}

// 销毁XDebug实例
void XDebug_destroy(XDebug* debug) {
    if (!debug) return;

    XVector_deinit_base(&debug->buffer);
    XMemory_free(debug);
}

// 设置输出目标
XDebug* XDebug_setTarget(XDebug* debug, XDebugTarget target, int fd) {
    if (!debug || !debug->is_active) return debug;

    debug->target = target;
    if (target == XDEBUG_TARGET_FILE) {
        debug->file_fd = fd;
    }
    return debug;
}

// 启用/禁用自动换行
XDebug* XDebug_setAutoNewline(XDebug* debug, bool enable) {
    if (!debug || !debug->is_active) return debug;
    debug->auto_newline = enable;
    return debug;
}

// 启用/禁用位置信息显示
XDebug* XDebug_setShowLocation(XDebug* debug, bool enable) {
    if (!debug || !debug->is_active) return debug;
    debug->show_location = enable;
    return debug;
}

// 基础写入函数
XDebug* XDebug_write(XDebug* debug, const char* data, size_t len) {
    if (!debug || !data || len == 0 || !debug->is_active) return debug;

    for (size_t i = 0; i < len; i++) {
        XVector_push_back_base(&debug->buffer, &data[i]);
    }
    return debug;
}

// 输出字符串
XDebug* XDebug_puts(XDebug* debug, const char* str) {
    if (!str) return debug;
    return XDebug_write(debug, str, strlen(str));
}

// 输出字符
XDebug* XDebug_putc(XDebug* debug, char c) {
    return XDebug_write(debug, &c, 1);
}

// 格式化输出（可变参数）
XDebug* XDebug_printf(XDebug* debug, const char* format, ...) {
    if (!debug || !format || !debug->is_active) return debug;

    va_list args;
    va_start(args, format);
    XDebug_vprintf(debug, format, args);
    va_end(args);
    return debug;
}

// 格式化输出（va_list）
XDebug* XDebug_vprintf(XDebug* debug, const char* format, va_list args) {
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

    // 写入XDebug缓冲区
    XDebug_write(debug, temp, len);
    XMemory_free(temp);

    return debug;
}

// 布尔值输出
XDebug* XDebug_bool(XDebug* debug, bool value) {
    return XDebug_puts(debug, value ? "true" : "false");
}

// 字符输出
XDebug* XDebug_char(XDebug* debug, char value) {
    return XDebug_printf(debug, "%c", value);
}

// 8位整数输出
XDebug* XDebug_int8(XDebug* debug, int8_t value) {
    return XDebug_printf(debug, "%hhd", value);
}

XDebug* XDebug_uint8(XDebug* debug, uint8_t value) {
    return XDebug_printf(debug, "%hhu", value);
}

// 16位整数输出
XDebug* XDebug_int16(XDebug* debug, int16_t value) {
    return XDebug_printf(debug, "%hd", value);
}

XDebug* XDebug_uint16(XDebug* debug, uint16_t value) {
    return XDebug_printf(debug, "%hu", value);
}

// 32位整数输出
XDebug* XDebug_int32(XDebug* debug, int32_t value) {
    return XDebug_printf(debug, "%d", value);
}

XDebug* XDebug_uint32(XDebug* debug, uint32_t value) {
    return XDebug_printf(debug, "%u", value);
}

// 64位整数输出
XDebug* XDebug_int64(XDebug* debug, int64_t value) {
    return XDebug_printf(debug, "%lld", value);
}

XDebug* XDebug_uint64(XDebug* debug, uint64_t value) {
    return XDebug_printf(debug, "%llu", value);
}

// 浮点数输出
XDebug* XDebug_float(XDebug* debug, float value) {
    return XDebug_printf(debug, "%f", value);
}

XDebug* XDebug_double(XDebug* debug, double value) {
    return XDebug_printf(debug, "%lf", value);
}

// 指针输出
XDebug* XDebug_ptr(XDebug* debug, const void* ptr) {
    return XDebug_printf(debug, "%p", ptr);
}

// 十六进制输出
XDebug* XDebug_hex(XDebug* debug, uint64_t value) {
    return XDebug_printf(debug, "0x%llx", value);
}

XDebug* XDebug_hex8(XDebug* debug, uint8_t value) {
    return XDebug_printf(debug, "0x%02x", value);
}

XDebug* XDebug_hex16(XDebug* debug, uint16_t value) {
    return XDebug_printf(debug, "0x%04x", value);
}

XDebug* XDebug_hex32(XDebug* debug, uint32_t value) {
    return XDebug_printf(debug, "0x%08x", value);
}

XDebug* XDebug_hex64(XDebug* debug, uint64_t value) {
    return XDebug_printf(debug, "0x%016llx", value);
}

// XString输出
XDebug* XDebug_XString(XDebug* debug, const XString* str) {
    if (!str) return XDebug_puts(debug, "(null XString)");
    return XDebug_printf(debug, "\"%s\" (length: %zu)",
        XString_toUtf8(str),
        XString_toUtf8_length(str));
}

// XVector输出（需要元素打印函数）
XDebug* XDebug_XVector(XDebug* debug, const XVector* vec,
    void (*print_elem)(XDebug*, const void*)) {
    if (!vec) return XDebug_puts(debug, "(null XVector)");
    if (!print_elem) return XDebug_puts(debug, "(no element printer)");

    XDebug_puts(debug, "XVector [ ");
    XDebug_printf(debug, "size: %zu, capacity: %zu, elements: [ ",
        XVector_size_base(vec),
        XVector_capacity_base(vec));

    size_t type_size = XVector_typeSize_base(vec);
    for (size_t i = 0; i < XVector_size_base(vec); i++) {
        const void* elem = (const char*)XContainerDataPtr(vec) + i * type_size;
        print_elem(debug, elem);
        if (i != XVector_size_base(vec) - 1) {
            XDebug_puts(debug, ", ");
        }
    }

    XDebug_puts(debug, " ] ]");
    return debug;
}

// XListSLinked输出（需要元素打印函数）
XDebug* XDebug_XListSLinked(XDebug* debug, const XListSLinked* list,
    void (*print_elem)(XDebug*, const void*)) {
    if (!list) return XDebug_puts(debug, "(null XListSLinked)");
    if (!print_elem) return XDebug_puts(debug, "(no element printer)");

    XDebug_puts(debug, "XListSLinked [ size: %zu, elements: [ ");

    // 遍历链表
  /*  XListSLinkedIterator it;
    XListSLinked_iterator_init(&it, (XListSLinked*)list);
    while (XListSLinked_iterator_valid(&it)) {
        const void* elem = XListSLinked_iterator_data(&it);
        print_elem(debug, elem);

        if (!XListSLinked_iterator_isLast(&it)) {
            XDebug_puts(debug, ", ");
        }
        XListSLinked_iterator_next(&it);
    }

    XDebug_puts(debug, " ] ]");*/
    return debug;
}

// 输出空格
XDebug* XDebug_space(XDebug* debug) {
    return XDebug_putc(debug, ' ');
}

// 取消后续空格（空实现，实际使用中可控制格式化）
XDebug* XDebug_nospace(XDebug* debug) {
    return debug;  // 可根据需要实现空格控制逻辑
}

// 输出换行
XDebug* XDebug_newline(XDebug* debug) {
    return XDebug_putc(debug, '\n');
}

// 刷新输出
XDebug* XDebug_flush(XDebug* debug) {
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
            write(debug->file_fd, location, strlen(location));
            write(debug->file_fd, final_output, final_len);
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
XDebug* XDebug_reset(XDebug* debug) {
    if (!debug) return debug;
    XVector_clear_base(&debug->buffer);
    return debug;
}

#endif  // DEBUG_ON || _DEBUG