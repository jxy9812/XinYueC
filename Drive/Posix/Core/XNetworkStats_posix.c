/**
 * @file XNetworkStats_posix.c
 * @brief Linux 平台网络收发累计字节数实现。
 * @details 读取 /proc/net/dev 并汇总除回环接口外的累计收发字节数；
 *          非 Linux 的类 Unix 平台由 Unsupported 存根兜底。
 */
#if defined(__linux__)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XNetworkStats.h"

bool XNetworkStats_readCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    FILE* file;
    char line[512];
    bool found = false;

    if (!rxBytes || !txBytes) return false;
    *rxBytes = 0;
    *txBytes = 0;

    file = fopen("/proc/net/dev", "r");
    if (!file) return false;

    while (fgets(line, sizeof(line), file))
    {
        char* colon = strchr(line, ':');
        char* name;
        char* cursor;
        char* end;
        int field;
        uint64_t rx = 0;
        uint64_t tx = 0;

        if (!colon) continue;
        *colon = '\0';
        name = line;
        while (*name == ' ' || *name == '\t') ++name;
        if (strcmp(name, "lo") == 0) continue;

        cursor = colon + 1;
        for (field = 0; field < 16; ++field)
        {
            unsigned long long value;
            while (*cursor == ' ' || *cursor == '\t') ++cursor;
            value = strtoull(cursor, &end, 10);
            if (end == cursor) break;
            if (field == 0) rx = (uint64_t)value;
            if (field == 8) tx = (uint64_t)value;
            cursor = end;
        }

        if (field == 16)
        {
            *rxBytes += rx;
            *txBytes += tx;
            found = true;
        }
    }

    fclose(file);
    return found;
}
#endif /* __linux__ */
