#include "zlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int zlib_compress(const unsigned char* input, size_t input_size, unsigned char* output, size_t output_size)
{
    // z_stream结构体：zlib库用于管理压缩/解压缩状态的核心结构
    z_stream stream;
    int ret;  // 用于接收zlib函数的返回状态码

    // 初始化z_stream结构体成员
    stream.zalloc = Z_NULL;  // 内存分配函数（使用默认NULL表示由zlib内部处理）
    stream.zfree = Z_NULL;   // 内存释放函数（同上）
    stream.opaque = Z_NULL;  // 传递给zalloc/zfree的用户参数（通常为NULL）

    // 初始化压缩器：deflateInit(压缩状态, 压缩级别)
    // 压缩级别：-1（默认）、0（无压缩）~9（最高压缩比，最慢）
    ret = deflateInit(&stream, Z_DEFAULT_COMPRESSION);
    if (ret != Z_OK) {  // 初始化失败（如内存不足）
        fprintf(stderr, "压缩初始化失败: 错误码=%d\n", ret);
        return -1;
    }

    // 设置输入数据信息
    stream.avail_in = input_size;       // 可用的输入数据长度（字节）
    stream.next_in = (Bytef*)input;    // 下一个待处理的输入数据指针

    // 设置输出缓冲区信息
    stream.avail_out = output_size;     // 输出缓冲区的可用空间（字节）
    stream.next_out = (Bytef*)output;  // 下一个写入输出的位置指针

    // 执行压缩：deflate(压缩状态, 刷新模式)
    // Z_FINISH：表示所有输入数据已提供，压缩到结束状态（不再接受新数据）
    ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {  // 压缩未正常结束（如缓冲区不足、数据错误）
        fprintf(stderr, "压缩失败: 错误码=%d\n", ret);
        deflateEnd(&stream);    // 释放压缩器资源
        return -1;
    }

    // 获取实际压缩后的总长度（stream.total_out记录累计输出字节数）
    size_t compressed_size = stream.total_out;

    // 释放压缩器资源（必须调用，否则会内存泄漏）
    deflateEnd(&stream);
    return compressed_size;
}

int zlib_decompress(const unsigned char* input, size_t input_size, unsigned char* output, size_t output_size)
{
    z_stream stream;
    int ret;

    // 初始化解压缩流（与压缩类似，但需额外清零avail_in和next_in）
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = 0;       // 初始无输入数据
    stream.next_in = Z_NULL;   // 输入指针置空

    // 初始化解压缩器
    ret = inflateInit(&stream);
    if (ret != Z_OK) {
        fprintf(stderr, "解压缩初始化失败: 错误码=%d\n", ret);
        return -1;
    }

    // 设置输入数据（压缩数据）
    stream.avail_in = input_size;
    stream.next_in = (Bytef*)input;

    // 设置输出缓冲区（存储解压结果）
    stream.avail_out = output_size;
    stream.next_out = (Bytef*)output;

    // 执行解压缩：inflate(解压缩状态, 刷新模式)
    // Z_FINISH：表示所有压缩数据已提供，解压缩到结束
    ret = inflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {  // 解压缩未正常结束（如数据损坏、缓冲区不足）
        fprintf(stderr, "解压缩失败: 错误码=%d\n", ret);
        inflateEnd(&stream);    // 释放解压缩器资源
        return -1;
    }

    // 获取实际解压缩后的总长度
    size_t decompressed_size = stream.total_out;

    // 释放解压缩器资源
    inflateEnd(&stream);
    return decompressed_size;
}

int zlib_compress_chunk(const unsigned char* input, size_t input_size, unsigned char* output, size_t* output_size, int step)
{
    // 静态变量：保持压缩状态（跨函数调用有效）
    static z_stream stream;       // 压缩状态结构体（需跨调用保存）
    static int initialized = 0;   // 初始化标记（0=未初始化，1=已初始化）
    static size_t prev_total_out = 0;  // 上一次压缩后的总输出长度（用于计算本次输出）
    int ret;

    // 步骤0：初始化压缩器（第一次调用时）
    if (step == 0) {
        // 重置压缩状态（避免上次调用的残留数据影响）
        memset(&stream, 0, sizeof(stream));
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;

        // 初始化压缩器
        ret = deflateInit(&stream, Z_DEFAULT_COMPRESSION);
        if (ret != Z_OK) {
            fprintf(stderr, "分块压缩初始化失败: 错误码=%d\n", ret);
            return -1;
        }

        initialized = 1;          // 标记为已初始化
        prev_total_out = 0;       // 重置输出计数
    }

    // 检查是否已初始化（未初始化则不能执行step=1/2）
    if (!initialized) {
        fprintf(stderr, "错误：请先调用step=0初始化压缩器\n");
        return -1;
    }

    // 设置输入输出参数（本次分块的数据和缓冲区）
    stream.avail_in = input_size;
    stream.next_in = (Bytef*)input;  // 若step=2，input可为NULL（此时input_size=0）
    stream.avail_out = *output_size;  // 本次输出缓冲区的可用空间
    stream.next_out = (Bytef*)output;

    // 根据步骤选择压缩模式（刷新策略）
    int flush;
    if (step == 2) {
        flush = Z_FINISH;  // 最后一步：压缩所有剩余数据并结束
    }
    else {
        flush = Z_SYNC_FLUSH;  // 中间步骤：刷新输出缓冲区（保留压缩状态）
    }

    // 执行本次分块压缩
    ret = deflate(&stream, flush);

    // 检查压缩结果
    if ((step == 2 && ret != Z_STREAM_END) ||  // 最后一步需返回Z_STREAM_END
        (step != 2 && ret != Z_OK)) {          // 中间步骤需返回Z_OK
        fprintf(stderr, "分块压缩失败: 步骤=%d, 错误码=%d\n", step, ret);
        return -1;
    }

    // 计算本次压缩的实际输出长度（当前总输出 - 上次总输出）
    *output_size = stream.total_out - prev_total_out;
    prev_total_out = stream.total_out;  // 更新上次总输出为当前值

    // 步骤2：结束压缩，释放资源
    if (step == 2) {
        deflateEnd(&stream);
        initialized = 0;  // 重置初始化标记（允许下次重新开始）
    }

    return 0;
}

int zlib_decompress_chunk(const unsigned char* input, size_t input_size, unsigned char* output, size_t* output_size, int step)
{
    // 静态变量：保持解压缩状态（跨函数调用有效）
    static z_stream stream;       // 解压缩状态结构体
    static int initialized = 0;   // 初始化标记（0=未初始化，1=已初始化）
    static size_t prev_total_out = 0;  // 上一次解压缩后的总输出长度
    int ret;

    // 步骤0：初始化解压缩器（第一次调用时）
    if (step == 0) {
        // 重置解压缩状态
        memset(&stream, 0, sizeof(stream));
        stream.zalloc = Z_NULL;
        stream.zfree = Z_NULL;
        stream.opaque = Z_NULL;
        stream.avail_in = 0;      // 初始无输入数据
        stream.next_in = Z_NULL;  // 输入指针置空

        // 初始化解压缩器
        ret = inflateInit(&stream);
        if (ret != Z_OK) {
            fprintf(stderr, "分块解压缩初始化失败: 错误码=%d\n", ret);
            return -1;
        }

        initialized = 1;          // 标记为已初始化
        prev_total_out = 0;       // 重置输出计数
    }

    // 检查是否已初始化（未初始化则不能执行step=1/2）
    if (!initialized) {
        fprintf(stderr, "错误：请先调用step=0初始化解压缩器\n");
        return -1;
    }

    // 设置输入数据（本次分块的压缩数据）
    stream.avail_in = input_size;
    stream.next_in = (Bytef*)input;  // 若step=2，input可为NULL（input_size=0）

    // 设置输出缓冲区（存储本次解压缩结果）
    stream.avail_out = *output_size;
    stream.next_out = (Bytef*)output;

    // 根据步骤选择解压缩模式（刷新策略）
    int flush = Z_SYNC_FLUSH;  // 解压缩默认使用Z_SYNC_FLUSH，确保输出缓冲区数据可用
    if (step == 2) {
        flush = Z_FINISH;  // 最后一步：尝试解压缩所有剩余数据
    }

    // 执行本次分块解压缩
    ret = inflate(&stream, flush);

    // 检查解压缩结果（允许中间步骤返回Z_OK，最后一步需返回Z_STREAM_END）
    if ((step == 2 && ret != Z_STREAM_END) ||  // 最后一步必须完成所有解压缩
        (step != 2 && ret != Z_OK && ret != Z_BUF_ERROR)) {  // 中间步骤允许缓冲区不足
        fprintf(stderr, "Chunk decompression failed: step=%d, error code=%d\n", step, ret);
        return -1;
    }

    // 计算本次解压缩的实际输出长度（当前总输出 - 上次总输出）
    *output_size = stream.total_out - prev_total_out;
    prev_total_out = stream.total_out;  // 更新上次总输出为当前值

    // 步骤2：结束解压缩，释放资源
    if (step == 2) {
        inflateEnd(&stream);
        initialized = 0;  // 重置初始化标记（允许下次重新开始）
    }

    return 0;
}
