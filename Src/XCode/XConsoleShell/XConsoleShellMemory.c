/**
 * @file XConsoleShellMemory.c
 * @brief XConsoleShell `mem` 内存池查询命令实现。
 * @details
 * 查询只调用 XMultiPool_global、XMultiPool_totalSize 和
 * XMultiPool_freeSize 等 XinYueC 公共 API，不调用平台堆接口，也不改变池状态。
 * 所有文本在固定栈缓冲中生成，适合串口、USB CDC 和 RTT 输出。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_MEMORY_ON && XCONSOLE_SHELL_MEMORY_POOL_ON

#include "XConsoleShellMemory.h"
#include "XMultiPool.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool xcs_memory_write_line(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text) &&
           XConsoleShell_writeUtf8(shell, "\n");
}

static unsigned xcs_memory_percent(size_t used, size_t total)
{
    uintmax_t usedValue = (uintmax_t)used;
    uintmax_t totalValue = (uintmax_t)total;
    if (totalValue == 0) return 0;
    if (usedValue <= UINTMAX_MAX / 100u)
        return (unsigned)((usedValue * 100u) / totalValue);
    /* 极大 size_t 下改用百分之一容量，避免乘法溢出。 */
    {
        uintmax_t unit = totalValue / 100u;
        unsigned percent = unit ? (unsigned)(usedValue / unit) : 0u;
        return percent > 100u ? 100u : percent;
    }
}

static int xcs_memory_pool(XConsoleShell* shell, XConsoleShellSession* session,
                           int argc, const char* const* argv, void* userData)
{
    XMultiPool* pool;
    size_t total;
    size_t freeSize;
    size_t used;
    unsigned percent;
    char line[160];
    bool verbose = false;
    size_t subPoolCount;
    size_t index;
    (void)session;
    (void)userData;
    if (!shell || argc > 1) return XConsoleResult_InvalidArgument;
    if (argc == 1) {
        if (!argv[0] || (strcmp(argv[0], "-v") != 0 &&
                         strcmp(argv[0], "--verbose") != 0)) {
            return XConsoleResult_InvalidArgument;
        }
        verbose = true;
    }
    pool = XMultiPool_global();
    total = XMultiPool_totalSize(pool);
    freeSize = XMultiPool_freeSize(pool);
    if (!pool || total == 0 || freeSize > total) {
        return xcs_memory_write_line(shell, "mem: 全局 XMultiPool 不可用")
                   ? XConsoleResult_Failed : XConsoleResult_IoError;
    }
    used = total - freeSize;
    percent = xcs_memory_percent(used, total);
    if (snprintf(line, sizeof(line), "%-12s %12s %12s %12s %8s",
                 "池", "总计(字节)", "已用(字节)", "空闲(字节)", "使用率") < 0 ||
        !xcs_memory_write_line(shell, line)) return XConsoleResult_IoError;
    if (snprintf(line, sizeof(line), "%-12s %12zu %12zu %12zu %7u%%",
                 "XMultiPool", total, used, freeSize, percent) < 0 ||
        !xcs_memory_write_line(shell, line)) return XConsoleResult_IoError;
    if (!verbose) return XConsoleResult_Ok;
    if (!xcs_memory_write_line(shell, "子池          总计(字节)     已用(字节)     空闲(字节)     使用率"))
        return XConsoleResult_IoError;
    subPoolCount = XMultiPool_subPoolCount(pool);
    for (index = 0; index < subPoolCount; ++index) {
        const XFixedPool* subPool = XMultiPool_subPoolAt(pool, index);
        size_t subTotal = XFixedPool_totalSize(subPool);
        size_t subFree = XFixedPool_freeSize(subPool);
        size_t subUsed;
        unsigned subPercent;
        if (!subPool || subFree > subTotal) return XConsoleResult_Failed;
        subUsed = subTotal - subFree;
        subPercent = xcs_memory_percent(subUsed, subTotal);
        if (snprintf(line, sizeof(line), "子池[%zu] %15zu %13zu %13zu %7u%%",
                     index, subTotal, subUsed, subFree, subPercent) < 0 ||
            !xcs_memory_write_line(shell, line)) return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

const XConsoleCommand XConsoleShellMemory_command = {
    "mem", NULL, "查询全局多级内存池使用情况", "mem [-v|--verbose]", 0, 1, 0,
    xcs_memory_pool, NULL, 0, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_MEMORY_ON && XCONSOLE_SHELL_MEMORY_POOL_ON */
