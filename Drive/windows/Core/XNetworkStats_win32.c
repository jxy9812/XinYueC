/**
 * @file XNetworkStats_win32.c
 * @brief Windows 平台网络收发累计字节数实现。
 * @details 使用 IP Helper 的 GetIfTable 汇总所有非回环接口的累计
 *          RX/TX 字节数；平台专用依赖只存在于本文件，不泄漏到业务层。
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#include <stdlib.h>

#include "XNetworkStats.h"

bool XNetworkStats_readCounters(uint64_t* rxBytes, uint64_t* txBytes)
{
    PMIB_IFTABLE table;
    DWORD tableBytes = 0;
    DWORD result;
    DWORD i;

    if (!rxBytes || !txBytes) return false;
    *rxBytes = 0;
    *txBytes = 0;

    (void)GetIfTable(NULL, &tableBytes, FALSE);
    if (tableBytes < sizeof(*table))
        return false;

    table = (PMIB_IFTABLE)malloc(tableBytes);
    if (!table)
        return false;

    result = GetIfTable(table, &tableBytes, FALSE);
    if (result != NO_ERROR)
    {
        free(table);
        return false;
    }

    for (i = 0; i < table->dwNumEntries; ++i)
    {
        const MIB_IFROW* row = &table->table[i];
        if (row->dwType == MIB_IF_TYPE_LOOPBACK)
            continue;
        *rxBytes += (uint64_t)row->dwInOctets;
        *txBytes += (uint64_t)row->dwOutOctets;
    }

    free(table);
    return true;
}
#endif /* _WIN32 */
