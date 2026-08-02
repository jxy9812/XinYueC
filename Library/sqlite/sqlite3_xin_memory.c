#include "sqlite3.h"

#include "XMemory.h"

#include <limits.h>
#include <stddef.h>

int XSqliteVfs_register(void);

typedef union XSqliteAllocationHeader {
    struct {
        int m_size;
    } m_data;
    void* m_pointerAlignment;
    long double m_longDoubleAlignment;
} XSqliteAllocationHeader;

static void* XSqliteMemory_malloc(int size)
{
    XSqliteAllocationHeader* header;
    if (size <= 0 || size > INT_MAX - (int)sizeof(XSqliteAllocationHeader)) return NULL;
    header = (XSqliteAllocationHeader*)XMalloc_System(sizeof(*header) + (size_t)size);
    if (!header) return NULL;
    header->m_data.m_size = size;
    return header + 1;
}

static void XSqliteMemory_free(void* memory)
{
    if (memory) XFree_System(((XSqliteAllocationHeader*)memory) - 1);
}

static void* XSqliteMemory_realloc(void* memory, int size)
{
    XSqliteAllocationHeader* header;
    if (!memory) return XSqliteMemory_malloc(size);
    if (size <= 0 || size > INT_MAX - (int)sizeof(XSqliteAllocationHeader)) {
        XSqliteMemory_free(memory);
        return NULL;
    }
    header = ((XSqliteAllocationHeader*)memory) - 1;
    header = (XSqliteAllocationHeader*)XRealloc_System(
        header, sizeof(*header) + (size_t)size);
    if (!header) return NULL;
    header->m_data.m_size = size;
    return header + 1;
}

static int XSqliteMemory_size(void* memory)
{
    return memory ? (((XSqliteAllocationHeader*)memory) - 1)->m_data.m_size : 0;
}

static int XSqliteMemory_roundup(int size)
{
    const int remainder = size & 7;
    return remainder ? size + 8 - remainder : size;
}

static int XSqliteMemory_init(void* appData)
{
    (void)appData;
    return SQLITE_OK;
}

static void XSqliteMemory_shutdown(void* appData)
{
    (void)appData;
}

int XSqliteMemory_initialize(void)
{
    static int state;
    static sqlite3_mem_methods methods = {
        XSqliteMemory_malloc,
        XSqliteMemory_free,
        XSqliteMemory_realloc,
        XSqliteMemory_size,
        XSqliteMemory_roundup,
        XSqliteMemory_init,
        XSqliteMemory_shutdown,
        NULL
    };
    int result;

    if (state == 1) return SQLITE_OK;
    if (state == -1) return SQLITE_ERROR;
    result = sqlite3_config(SQLITE_CONFIG_MALLOC, &methods);
    if (result != SQLITE_OK && result != SQLITE_MISUSE) {
        state = -1;
        return result;
    }
    result = XSqliteVfs_register();
    if (result != SQLITE_OK) {
        state = -1;
        return result;
    }
    result = sqlite3_initialize();
    state = result == SQLITE_OK ? 1 : -1;
    return result;
}
