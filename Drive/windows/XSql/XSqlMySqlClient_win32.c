/**
 * @file XSqlMySqlClient_win32.c
 * @brief Windows MySQL 共享内存传输实现。
 */
#include "XSqlMySqlClient_platform.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XMemory.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define XSQL_MYSQL_SHARED_MEMORY_BUFFER_SIZE 16000u

struct XSqlMySqlSharedMemory {
    HANDLE m_fileMap;
    uint8_t* m_map;
    HANDLE m_eventServerWrote;
    HANDLE m_eventServerRead;
    HANDLE m_eventClientWrote;
    HANDLE m_eventClientRead;
    HANDLE m_eventConnectionClosed;
    uint8_t* m_position;
    size_t m_remaining;
};

static DWORD xsql_mysql_shared_memory_timeout(int timeoutMs)
{
    return timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs;
}

static uint32_t xsql_mysql_shared_memory_read_u32(const uint8_t* data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static void xsql_mysql_shared_memory_write_u32(uint8_t* data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

void XSqlMySqlSharedMemory_close(XSqlMySqlSharedMemory* shared)
{
    if (!shared) return;
    if (shared->m_eventConnectionClosed)
        SetEvent(shared->m_eventConnectionClosed);
    if (shared->m_map) UnmapViewOfFile(shared->m_map);
    if (shared->m_eventServerWrote) CloseHandle(shared->m_eventServerWrote);
    if (shared->m_eventServerRead) CloseHandle(shared->m_eventServerRead);
    if (shared->m_eventClientWrote) CloseHandle(shared->m_eventClientWrote);
    if (shared->m_eventClientRead) CloseHandle(shared->m_eventClientRead);
    if (shared->m_eventConnectionClosed) CloseHandle(shared->m_eventConnectionClosed);
    if (shared->m_fileMap) CloseHandle(shared->m_fileMap);
    XFree_System(shared);
}

XSqlMySqlSharedMemory* XSqlMySqlSharedMemory_open(const char* baseName, int timeoutMs)
{
    XSqlMySqlSharedMemory* shared = NULL;
    HANDLE eventConnectRequest = NULL;
    HANDLE eventConnectAnswer = NULL;
    HANDLE connectFileMap = NULL;
    uint8_t* connectMap = NULL;
    HANDLE fileMap = NULL;
    uint8_t* map = NULL;
    HANDLE eventServerWrote = NULL;
    HANDLE eventServerRead = NULL;
    HANDLE eventClientWrote = NULL;
    HANDLE eventClientRead = NULL;
    HANDLE eventConnectionClosed = NULL;
    const char* prefix = NULL;
    const char* prefixes[] = { "", "Global\\" };
    char name[1024];
    char connectionNumber[32];
    uint32_t number;
    int i;

    if (!baseName) return NULL;
    for (i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); ++i) {
        if (snprintf(name, sizeof(name), "%s%s_CONNECT_REQUEST", prefixes[i], baseName)
                >= (int)sizeof(name))
            return NULL;
        eventConnectRequest = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
        if (eventConnectRequest) {
            prefix = prefixes[i];
            break;
        }
    }
    if (!eventConnectRequest || !prefix) goto fail;

    if (snprintf(name, sizeof(name), "%s%s_CONNECT_ANSWER", prefix, baseName)
            >= (int)sizeof(name)) goto fail;
    eventConnectAnswer = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!eventConnectAnswer) goto fail;
    if (snprintf(name, sizeof(name), "%s%s_CONNECT_DATA", prefix, baseName)
            >= (int)sizeof(name)) goto fail;
    connectFileMap = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!connectFileMap) goto fail;
    connectMap = (uint8_t*)MapViewOfFile(connectFileMap, FILE_MAP_READ | FILE_MAP_WRITE,
                                         0, 0, sizeof(uint32_t));
    if (!connectMap) goto fail;
    if (!SetEvent(eventConnectRequest)) goto fail;
    if (WaitForSingleObject(eventConnectAnswer, xsql_mysql_shared_memory_timeout(timeoutMs))
            != WAIT_OBJECT_0)
        goto fail;

    number = xsql_mysql_shared_memory_read_u32(connectMap);
    if (snprintf(connectionNumber, sizeof(connectionNumber), "%u", number)
            >= (int)sizeof(connectionNumber)) goto fail;
    if (snprintf(name, sizeof(name), "%s%s_%s_DATA", prefix, baseName, connectionNumber)
            >= (int)sizeof(name)) goto fail;
    fileMap = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!fileMap) goto fail;
    map = (uint8_t*)MapViewOfFile(fileMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                                  XSQL_MYSQL_SHARED_MEMORY_BUFFER_SIZE + 4u);
    if (!map) goto fail;

    if (snprintf(name, sizeof(name), "%s%s_%s_SERVER_WROTE", prefix, baseName,
                 connectionNumber) >= (int)sizeof(name)) goto fail;
    eventServerWrote = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!eventServerWrote) goto fail;
    if (snprintf(name, sizeof(name), "%s%s_%s_SERVER_READ", prefix, baseName,
                 connectionNumber) >= (int)sizeof(name)) goto fail;
    eventServerRead = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!eventServerRead) goto fail;
    if (snprintf(name, sizeof(name), "%s%s_%s_CLIENT_WROTE", prefix, baseName,
                 connectionNumber) >= (int)sizeof(name)) goto fail;
    eventClientWrote = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!eventClientWrote) goto fail;
    if (snprintf(name, sizeof(name), "%s%s_%s_CLIENT_READ", prefix, baseName,
                 connectionNumber) >= (int)sizeof(name)) goto fail;
    eventClientRead = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!eventClientRead) goto fail;
    if (snprintf(name, sizeof(name), "%s%s_%s_CONNECTION_CLOSED", prefix, baseName,
                 connectionNumber) >= (int)sizeof(name)) goto fail;
    eventConnectionClosed = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name);
    if (!eventConnectionClosed) goto fail;
    if (!SetEvent(eventServerRead)) goto fail;

    shared = (XSqlMySqlSharedMemory*)XCalloc_System(1, sizeof(*shared));
    if (!shared) goto fail;
    shared->m_fileMap = fileMap;
    shared->m_map = map;
    shared->m_eventServerWrote = eventServerWrote;
    shared->m_eventServerRead = eventServerRead;
    shared->m_eventClientWrote = eventClientWrote;
    shared->m_eventClientRead = eventClientRead;
    shared->m_eventConnectionClosed = eventConnectionClosed;
    fileMap = NULL;
    map = NULL;
    eventServerWrote = NULL;
    eventServerRead = NULL;
    eventClientWrote = NULL;
    eventClientRead = NULL;
    eventConnectionClosed = NULL;

fail:
    if (connectMap) UnmapViewOfFile(connectMap);
    if (connectFileMap) CloseHandle(connectFileMap);
    if (eventConnectRequest) CloseHandle(eventConnectRequest);
    if (eventConnectAnswer) CloseHandle(eventConnectAnswer);
    if (map) UnmapViewOfFile(map);
    if (fileMap) CloseHandle(fileMap);
    if (eventServerWrote) CloseHandle(eventServerWrote);
    if (eventServerRead) CloseHandle(eventServerRead);
    if (eventClientWrote) CloseHandle(eventClientWrote);
    if (eventClientRead) CloseHandle(eventClientRead);
    if (eventConnectionClosed) CloseHandle(eventConnectionClosed);
    if (!shared) return NULL;
    return shared;
}

bool XSqlMySqlSharedMemory_read(XSqlMySqlSharedMemory* shared, void* data,
                                size_t size, int timeoutMs)
{
    uint8_t* current = (uint8_t*)data;
    size_t remaining = size;
    HANDLE events[2];
    DWORD status;
    uint32_t chunkSize;
    size_t length;
    if (!shared || !data) return false;
    events[0] = shared->m_eventServerWrote;
    events[1] = shared->m_eventConnectionClosed;
    while (remaining > 0) {
        if (shared->m_remaining == 0) {
            status = WaitForMultipleObjects(2, events, FALSE,
                                            xsql_mysql_shared_memory_timeout(timeoutMs));
            if (status != WAIT_OBJECT_0) return false;
            chunkSize = xsql_mysql_shared_memory_read_u32(shared->m_map);
            if (chunkSize > XSQL_MYSQL_SHARED_MEMORY_BUFFER_SIZE) return false;
            shared->m_position = shared->m_map + 4;
            shared->m_remaining = chunkSize;
            if (chunkSize == 0 && !SetEvent(shared->m_eventClientRead)) return false;
            continue;
        }
        length = shared->m_remaining < remaining ? shared->m_remaining : remaining;
        memcpy(current, shared->m_position, length);
        current += length;
        remaining -= length;
        shared->m_position += length;
        shared->m_remaining -= length;
        if (shared->m_remaining == 0 && !SetEvent(shared->m_eventClientRead)) return false;
    }
    return true;
}

bool XSqlMySqlSharedMemory_write(XSqlMySqlSharedMemory* shared, const void* data,
                                 size_t size, int timeoutMs)
{
    const uint8_t* current = (const uint8_t*)data;
    size_t remaining = size;
    size_t length;
    HANDLE events[2];
    DWORD status;
    if (!shared || !data) return false;
    events[0] = shared->m_eventServerRead;
    events[1] = shared->m_eventConnectionClosed;
    while (remaining > 0) {
        status = WaitForMultipleObjects(2, events, FALSE,
                                        xsql_mysql_shared_memory_timeout(timeoutMs));
        if (status != WAIT_OBJECT_0) return false;
        length = remaining > XSQL_MYSQL_SHARED_MEMORY_BUFFER_SIZE
            ? XSQL_MYSQL_SHARED_MEMORY_BUFFER_SIZE : remaining;
        xsql_mysql_shared_memory_write_u32(shared->m_map, (uint32_t)length);
        memcpy(shared->m_map + 4, current, length);
        current += length;
        remaining -= length;
        if (!SetEvent(shared->m_eventClientWrote)) return false;
    }
    return true;
}

#endif /* _WIN32 */
