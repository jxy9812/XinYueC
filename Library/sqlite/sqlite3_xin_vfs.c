#include "sqlite3.h"

#include "XDateTime.h"
#include "XAtomic.h"
#include "XFileSystem.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XRandomGenerator.h"
#include "XReadWriteLock.h"
#include "XThread.h"

#include <stdint.h>
#include <string.h>

typedef struct XSqliteShmRegion {
    void* m_address;
    int64_t m_size;
} XSqliteShmRegion;

#define XSQLITE_LOCK_GROUP_MAX 16

typedef struct XSqliteLockGroup {
    bool m_active;
    XString* m_path;
    size_t m_refs;
    XReadWriteLock* m_shmLocks[SQLITE_SHM_NLOCK];
} XSqliteLockGroup;

static XMutex* g_xsqlite_lockRegistryMutex;
static XSqliteLockGroup g_xsqlite_lockGroups[XSQLITE_LOCK_GROUP_MAX];

/*
 * SQLite calls this VFS for all file-backed databases.  The implementation
 * deliberately stops at XFileSystem.h so the SQLite source does not
 * know whether the target uses POSIX, Win32, FatFs, or another XFile backend.
 */
typedef struct XSqliteFile {
    sqlite3_file m_parent;
    XFd m_fd;
    XString* m_path;
    int m_lockLevel;
    bool m_deleteOnClose;
    bool m_readOnly;
    XFd m_shmFd;
    XString* m_shmPath;
    XSqliteShmRegion* m_shmRegions;
    size_t m_shmRegionCount;
    size_t m_shmRegionCapacity;
    XSqliteLockGroup* m_lockGroup;
    int m_shmLockState[SQLITE_SHM_NLOCK];
} XSqliteFile;

static int xsqlite_file_close(sqlite3_file* file);
static int xsqlite_file_read(sqlite3_file* file, void* buffer, int amount, sqlite3_int64 offset);
static int xsqlite_file_write(sqlite3_file* file, const void* buffer, int amount, sqlite3_int64 offset);
static int xsqlite_file_truncate(sqlite3_file* file, sqlite3_int64 size);
static int xsqlite_file_sync(sqlite3_file* file, int flags);
static int xsqlite_file_size(sqlite3_file* file, sqlite3_int64* size);
static int xsqlite_file_lock(sqlite3_file* file, int level);
static int xsqlite_file_unlock(sqlite3_file* file, int level);
static int xsqlite_file_check_reserved_lock(sqlite3_file* file, int* result);
static int xsqlite_file_control(sqlite3_file* file, int operation, void* argument);
static int xsqlite_file_sector_size(sqlite3_file* file);
static int xsqlite_file_device_characteristics(sqlite3_file* file);
static int xsqlite_file_shm_map(sqlite3_file* file, int page, int pageSize,
                                int extend, void volatile** result);
static int xsqlite_file_shm_lock(sqlite3_file* file, int offset, int count, int flags);
static void xsqlite_file_shm_barrier(sqlite3_file* file);
static int xsqlite_file_shm_unmap(sqlite3_file* file, int deleteFlag);
static int xsqlite_file_fetch(sqlite3_file* file, sqlite3_int64 offset, int amount, void** result);
static int xsqlite_file_unfetch(sqlite3_file* file, sqlite3_int64 offset, void* result);

static void xsqlite_file_shm_clear(XSqliteFile* sqliteFile);
static XSqliteLockGroup* xsqlite_lock_group_acquire(const XString* path);
static void xsqlite_lock_group_release(XSqliteLockGroup* group);

static const sqlite3_io_methods g_xsqlite_io_methods = {
    3,
    xsqlite_file_close,
    xsqlite_file_read,
    xsqlite_file_write,
    xsqlite_file_truncate,
    xsqlite_file_sync,
    xsqlite_file_size,
    xsqlite_file_lock,
    xsqlite_file_unlock,
    xsqlite_file_check_reserved_lock,
    xsqlite_file_control,
    xsqlite_file_sector_size,
    xsqlite_file_device_characteristics,
    xsqlite_file_shm_map,
    xsqlite_file_shm_lock,
    xsqlite_file_shm_barrier,
    xsqlite_file_shm_unmap,
    xsqlite_file_fetch,
    xsqlite_file_unfetch
};

static XSqliteFile* xsqlite_file_cast(sqlite3_file* file)
{
    return (XSqliteFile*)file;
}

static int xsqlite_file_is_open(const XSqliteFile* file)
{
    return file && file->m_fd != XFD_INVALID;
}

static int xsqlite_file_open_mode(int flags)
{
    int mode = XIODevice_ReadOnly;
    if (flags & SQLITE_OPEN_READWRITE) mode = XIODevice_ReadWrite;
    if (flags & SQLITE_OPEN_CREATE) mode |= XIODevice_Create;
    if (flags & SQLITE_OPEN_EXCLUSIVE) mode |= XIODevice_NewOnly;
    if (!(flags & SQLITE_OPEN_CREATE) && (flags & SQLITE_OPEN_READONLY))
        mode |= XIODevice_Existing;
    return mode;
}

static XString* xsqlite_file_create_temp_name(const sqlite3_file* file)
{
    return XString_create_fmt_utf8(".xsqlite-temp-%p", (const void*)file);
}

static XSqliteLockGroup* xsqlite_lock_group_acquire(const XString* path)
{
    XSqliteLockGroup* freeGroup = NULL;
    size_t index;
    if (!path || !g_xsqlite_lockRegistryMutex) return NULL;
    XMutex_lock(g_xsqlite_lockRegistryMutex);
    for (index = 0; index < XSQLITE_LOCK_GROUP_MAX; ++index) {
        XSqliteLockGroup* group = &g_xsqlite_lockGroups[index];
        if (group->m_active && group->m_path
            && XString_equals(group->m_path, path, XChar_CaseSensitive)) {
            ++group->m_refs;
            XMutex_unlock(g_xsqlite_lockRegistryMutex);
            return group;
        }
        if (!group->m_active && !freeGroup) freeGroup = group;
    }
    if (!freeGroup) {
        XMutex_unlock(g_xsqlite_lockRegistryMutex);
        return NULL;
    }
    freeGroup->m_path = XString_create_copy(path);
    if (!freeGroup->m_path) {
        XMutex_unlock(g_xsqlite_lockRegistryMutex);
        return NULL;
    }
    for (index = 0; index < SQLITE_SHM_NLOCK; ++index) {
        freeGroup->m_shmLocks[index] = XReadWriteLock_create(XLock_NonRecursive);
        if (!freeGroup->m_shmLocks[index]) {
            while (index > 0) {
                --index;
                XReadWriteLock_delete(freeGroup->m_shmLocks[index]);
                freeGroup->m_shmLocks[index] = NULL;
            }
            XString_delete_base(freeGroup->m_path);
            freeGroup->m_path = NULL;
            XMutex_unlock(g_xsqlite_lockRegistryMutex);
            return NULL;
        }
    }
    freeGroup->m_refs = 1;
    freeGroup->m_active = true;
    XMutex_unlock(g_xsqlite_lockRegistryMutex);
    return freeGroup;
}

static void xsqlite_lock_group_release(XSqliteLockGroup* group)
{
    size_t index;
    if (!group || !g_xsqlite_lockRegistryMutex) return;
    XMutex_lock(g_xsqlite_lockRegistryMutex);
    if (!group->m_active || group->m_refs == 0) {
        XMutex_unlock(g_xsqlite_lockRegistryMutex);
        return;
    }
    --group->m_refs;
    if (group->m_refs != 0) {
        XMutex_unlock(g_xsqlite_lockRegistryMutex);
        return;
    }
    group->m_active = false;
    for (index = 0; index < SQLITE_SHM_NLOCK; ++index) {
        XReadWriteLock_delete(group->m_shmLocks[index]);
        group->m_shmLocks[index] = NULL;
    }
    if (group->m_path) XString_delete_base(group->m_path);
    group->m_path = NULL;
    XMutex_unlock(g_xsqlite_lockRegistryMutex);
}

static int xsqlite_file_close(sqlite3_file* file)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    int result = SQLITE_OK;
    if (!sqliteFile) return SQLITE_IOERR_CLOSE;
    if (xsqlite_file_shm_unmap(file, sqliteFile->m_deleteOnClose) != SQLITE_OK)
        result = SQLITE_IOERR_CLOSE;
    if (sqliteFile->m_fd != XFD_INVALID) {
        XFileSystem_close(sqliteFile->m_fd);
        sqliteFile->m_fd = XFD_INVALID;
    }
    if (sqliteFile->m_deleteOnClose && sqliteFile->m_path
        && !XFileSystem_remove(sqliteFile->m_path)) {
        result = SQLITE_IOERR_DELETE;
    }
    if (sqliteFile->m_path) {
        XString_delete_base(sqliteFile->m_path);
        sqliteFile->m_path = NULL;
    }
    if (sqliteFile->m_lockGroup) {
        xsqlite_lock_group_release(sqliteFile->m_lockGroup);
        sqliteFile->m_lockGroup = NULL;
    }
    sqliteFile->m_parent.pMethods = NULL;
    return result;
}

static int xsqlite_file_read(sqlite3_file* file, void* buffer, int amount,
                             sqlite3_int64 offset)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    int64_t readSize;
    if (!xsqlite_file_is_open(sqliteFile) || !buffer || amount < 0 || offset < 0)
        return SQLITE_IOERR_READ;
    if (!XFileSystem_seek(sqliteFile->m_fd, offset)) return SQLITE_IOERR_SEEK;
    readSize = XFileSystem_read(sqliteFile->m_fd, buffer, amount);
    if (readSize == amount) return SQLITE_OK;
    if (readSize >= 0 && readSize < amount) {
        memset((uint8_t*)buffer + readSize, 0, (size_t)amount - (size_t)readSize);
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_IOERR_READ;
}

static int xsqlite_file_write(sqlite3_file* file, const void* buffer, int amount,
                              sqlite3_int64 offset)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    int64_t written;
    if (!xsqlite_file_is_open(sqliteFile) || !buffer || amount < 0 || offset < 0)
        return SQLITE_IOERR_WRITE;
    if (!XFileSystem_seek(sqliteFile->m_fd, offset)) return SQLITE_IOERR_SEEK;
    written = XFileSystem_write(sqliteFile->m_fd, buffer, amount);
    return written == amount ? SQLITE_OK : SQLITE_IOERR_WRITE;
}

static int xsqlite_file_truncate(sqlite3_file* file, sqlite3_int64 size)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    if (!xsqlite_file_is_open(sqliteFile) || size < 0) return SQLITE_IOERR_TRUNCATE;
    return XFileSystem_resize(sqliteFile->m_fd, size) ? SQLITE_OK : SQLITE_IOERR_TRUNCATE;
}

static int xsqlite_file_sync(sqlite3_file* file, int flags)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    (void)flags;
    if (!xsqlite_file_is_open(sqliteFile)) return SQLITE_IOERR_FSYNC;
    return XFileSystem_flush(sqliteFile->m_fd) ? SQLITE_OK : SQLITE_IOERR_FSYNC;
}

static int xsqlite_file_size(sqlite3_file* file, sqlite3_int64* size)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    XFileStat stat;
    if (!xsqlite_file_is_open(sqliteFile) || !size) return SQLITE_IOERR_FSTAT;
    if (!XFileSystem_fstat(sqliteFile->m_fd, &stat)) return SQLITE_IOERR_FSTAT;
    *size = (sqlite3_int64)stat.size;
    return SQLITE_OK;
}

/*
 * XFile currently exposes no portable inter-process byte-range lock.  Keep
 * SQLite's lock state per opened handle so normal single-connection and
 * single-process use follows SQLite's state machine.  A target that needs
 * multi-process locking can extend XFileSystem.h without changing
 * the SQL public layer.
 */
static int xsqlite_file_lock(sqlite3_file* file, int level)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    if (!xsqlite_file_is_open(sqliteFile)) return SQLITE_IOERR_LOCK;
    if (level > sqliteFile->m_lockLevel) sqliteFile->m_lockLevel = level;
    return SQLITE_OK;
}

static int xsqlite_file_unlock(sqlite3_file* file, int level)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    if (!xsqlite_file_is_open(sqliteFile)) return SQLITE_IOERR_UNLOCK;
    if (level < SQLITE_LOCK_NONE) level = SQLITE_LOCK_NONE;
    sqliteFile->m_lockLevel = level;
    return SQLITE_OK;
}

static int xsqlite_file_check_reserved_lock(sqlite3_file* file, int* result)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    if (!xsqlite_file_is_open(sqliteFile) || !result)
        return SQLITE_IOERR_CHECKRESERVEDLOCK;
    *result = sqliteFile->m_lockLevel >= SQLITE_LOCK_RESERVED;
    return SQLITE_OK;
}

static int xsqlite_file_control(sqlite3_file* file, int operation, void* argument)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    if (operation == SQLITE_FCNTL_LOCKSTATE && argument) {
        *(int*)argument = sqliteFile ? sqliteFile->m_lockLevel : SQLITE_LOCK_NONE;
        return SQLITE_OK;
    }
    return SQLITE_NOTFOUND;
}

static int xsqlite_file_sector_size(sqlite3_file* file)
{
    (void)file;
    return 4096;
}

static int xsqlite_file_device_characteristics(sqlite3_file* file)
{
    (void)file;
    return 0;
}

static bool xsqlite_file_shm_reserve(XSqliteFile* sqliteFile, size_t wanted)
{
    size_t capacity;
    XSqliteShmRegion* regions;
    if (!sqliteFile) return false;
    if (wanted <= sqliteFile->m_shmRegionCapacity) return true;
    capacity = sqliteFile->m_shmRegionCapacity ? sqliteFile->m_shmRegionCapacity : 4;
    while (capacity < wanted) {
        if (capacity > SIZE_MAX / 2) return false;
        capacity *= 2;
    }
    regions = (XSqliteShmRegion*)XRealloc_System(
        sqliteFile->m_shmRegions, capacity * sizeof(*regions));
    if (!regions) return false;
    memset(regions + sqliteFile->m_shmRegionCapacity, 0,
           (capacity - sqliteFile->m_shmRegionCapacity) * sizeof(*regions));
    sqliteFile->m_shmRegions = regions;
    sqliteFile->m_shmRegionCapacity = capacity;
    return true;
}

static int xsqlite_file_shm_open(XSqliteFile* sqliteFile)
{
    int error = 0;
    int mode;
    if (!sqliteFile || !sqliteFile->m_path) return SQLITE_IOERR_SHMOPEN;
    if (sqliteFile->m_shmFd != XFD_INVALID) return SQLITE_OK;
    sqliteFile->m_shmPath = XString_create_copy(sqliteFile->m_path);
    if (!sqliteFile->m_shmPath) return SQLITE_NOMEM;
    XString_append_utf8(sqliteFile->m_shmPath, "-shm");
    mode = sqliteFile->m_readOnly
        ? (XIODevice_ReadOnly | XIODevice_Existing)
        : (XIODevice_ReadWrite | XIODevice_Create);
    sqliteFile->m_shmFd = XFileSystem_open(sqliteFile->m_shmPath, mode, &error);
    if (sqliteFile->m_shmFd == XFD_INVALID) {
        XString_delete_base(sqliteFile->m_shmPath);
        sqliteFile->m_shmPath = NULL;
        return sqliteFile->m_readOnly ? SQLITE_READONLY_CANTINIT : SQLITE_IOERR_SHMOPEN;
    }
    return SQLITE_OK;
}

static int xsqlite_file_shm_map(sqlite3_file* file, int page, int pageSize,
                                int extend, void volatile** result)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    XFileStat stat;
    sqlite3_int64 offset;
    sqlite3_int64 wantedSize;
    void* address;
    int code;
    if (!sqliteFile || !result || page < 0 || pageSize <= 0)
        return SQLITE_IOERR_SHMMAP;
    if (result) *result = NULL;
    if (!xsqlite_file_shm_reserve(sqliteFile, (size_t)page + 1)) return SQLITE_NOMEM;
    if ((size_t)page < sqliteFile->m_shmRegionCount
        && sqliteFile->m_shmRegions[page].m_address) {
        if (sqliteFile->m_shmRegions[page].m_size != pageSize)
            return SQLITE_IOERR_SHMMAP;
        *result = (void volatile*)sqliteFile->m_shmRegions[page].m_address;
        return SQLITE_OK;
    }
    code = xsqlite_file_shm_open(sqliteFile);
    if (code != SQLITE_OK) return code;
    offset = (sqlite3_int64)page * (sqlite3_int64)pageSize;
    wantedSize = offset + pageSize;
    if (!XFileSystem_fstat(sqliteFile->m_shmFd, &stat)) return SQLITE_IOERR_FSTAT;
    if ((sqlite3_int64)stat.size < wantedSize) {
        if (!extend || sqliteFile->m_readOnly) return SQLITE_OK;
        if (!XFileSystem_resize(sqliteFile->m_shmFd, wantedSize))
            return SQLITE_IOERR_SHMSIZE;
    }
    address = XFileSystem_map(sqliteFile->m_shmFd, offset, pageSize,
                              sqliteFile->m_readOnly ? 0 : 0x2);
    if (!address) return SQLITE_IOERR_MMAP;
    sqliteFile->m_shmRegions[page].m_address = address;
    sqliteFile->m_shmRegions[page].m_size = pageSize;
    if ((size_t)page >= sqliteFile->m_shmRegionCount)
        sqliteFile->m_shmRegionCount = (size_t)page + 1;
    *result = (void volatile*)address;
    return SQLITE_OK;
}

static int xsqlite_file_shm_lock(sqlite3_file* file, int offset, int count, int flags)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    XReadWriteLock* lock;
    bool acquired[SQLITE_SHM_NLOCK] = { false };
    bool lockedNow[SQLITE_SHM_NLOCK] = { false };
    int index;
    if (!sqliteFile || offset < 0 || count <= 0
        || offset + count > SQLITE_SHM_NLOCK || !sqliteFile->m_lockGroup)
        return SQLITE_IOERR_SHMLOCK;
    if (flags & SQLITE_SHM_LOCK) {
        for (index = offset; index < offset + count; ++index) {
            lock = sqliteFile->m_lockGroup->m_shmLocks[index];
            if (!lock) return SQLITE_IOERR_SHMLOCK;
            if ((flags & SQLITE_SHM_SHARED) && sqliteFile->m_shmLockState[index] > 0) {
                acquired[index] = true;
                continue;
            }
            if ((flags & SQLITE_SHM_EXCLUSIVE) && sqliteFile->m_shmLockState[index] < 0) {
                acquired[index] = true;
                continue;
            }
            if (flags & SQLITE_SHM_EXCLUSIVE)
                acquired[index] = XReadWriteLock_tryLockForWrite(lock);
            else if (flags & SQLITE_SHM_SHARED)
                acquired[index] = XReadWriteLock_tryLockForRead(lock);
            lockedNow[index] = acquired[index];
            if (!acquired[index]) {
                int rollback;
                for (rollback = offset; rollback < index; ++rollback) {
                    if (lockedNow[rollback])
                    {
                        XReadWriteLock_unlock(sqliteFile->m_lockGroup->m_shmLocks[rollback]);
                        sqliteFile->m_shmLockState[rollback] = 0;
                    }
                }
                return SQLITE_BUSY;
            }
            if (flags & SQLITE_SHM_EXCLUSIVE) sqliteFile->m_shmLockState[index] = -1;
            else if (flags & SQLITE_SHM_SHARED) sqliteFile->m_shmLockState[index] = 1;
        }
    } else if (flags & SQLITE_SHM_UNLOCK) {
        for (index = offset; index < offset + count; ++index) {
            lock = sqliteFile->m_lockGroup->m_shmLocks[index];
            if (!lock) return SQLITE_IOERR_SHMLOCK;
            if (sqliteFile->m_shmLockState[index] > 1) {
                --sqliteFile->m_shmLockState[index];
            } else if (sqliteFile->m_shmLockState[index] != 0) {
                XReadWriteLock_unlock(lock);
                sqliteFile->m_shmLockState[index] = 0;
            }
        }
    }
    return SQLITE_OK;
}

static void xsqlite_file_shm_barrier(sqlite3_file* file)
{
    (void)file;
    XAtomic_memory_barrier();
}

static int xsqlite_file_shm_unmap(sqlite3_file* file, int deleteFlag)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    int index;
    int result = SQLITE_OK;
    if (!sqliteFile) return SQLITE_IOERR_SHMOPEN;
    if (sqliteFile->m_lockGroup) {
        for (index = 0; index < SQLITE_SHM_NLOCK; ++index) {
            XReadWriteLock* lock = sqliteFile->m_lockGroup->m_shmLocks[index];
            if (lock && sqliteFile->m_shmLockState[index] != 0) {
                XReadWriteLock_unlock(lock);
                sqliteFile->m_shmLockState[index] = 0;
            }
        }
    }
    xsqlite_file_shm_clear(sqliteFile);
    if (sqliteFile->m_shmFd != XFD_INVALID) {
        XFileSystem_close(sqliteFile->m_shmFd);
        sqliteFile->m_shmFd = XFD_INVALID;
    }
    if (deleteFlag && sqliteFile->m_shmPath
        && XFileSystem_exists(sqliteFile->m_shmPath)
        && !XFileSystem_remove(sqliteFile->m_shmPath)) {
        result = SQLITE_IOERR_DELETE;
    }
    if (sqliteFile->m_shmPath) {
        XString_delete_base(sqliteFile->m_shmPath);
        sqliteFile->m_shmPath = NULL;
    }
    return result;
}

static void xsqlite_file_shm_clear(XSqliteFile* sqliteFile)
{
    size_t index;
    if (!sqliteFile) return;
    for (index = 0; index < sqliteFile->m_shmRegionCount; ++index) {
        if (sqliteFile->m_shmRegions[index].m_address) {
            XFileSystem_unmap(sqliteFile->m_shmRegions[index].m_address,
                              sqliteFile->m_shmRegions[index].m_size);
        }
    }
    if (sqliteFile->m_shmRegions) XFree_System(sqliteFile->m_shmRegions);
    sqliteFile->m_shmRegions = NULL;
    sqliteFile->m_shmRegionCount = 0;
    sqliteFile->m_shmRegionCapacity = 0;
}

static int xsqlite_file_fetch(sqlite3_file* file, sqlite3_int64 offset,
                              int amount, void** result)
{
    (void)file;
    (void)offset;
    (void)amount;
    if (result) *result = NULL;
    return SQLITE_OK;
}

static int xsqlite_file_unfetch(sqlite3_file* file, sqlite3_int64 offset, void* result)
{
    (void)file;
    (void)offset;
    (void)result;
    return SQLITE_OK;
}

static int xsqlite_vfs_open(sqlite3_vfs* vfs, sqlite3_filename name,
                            sqlite3_file* file, int flags, int* outFlags)
{
    XSqliteFile* sqliteFile = xsqlite_file_cast(file);
    XString* path;
    int error = 0;
    int mode;
    (void)vfs;
    if (!sqliteFile) return SQLITE_CANTOPEN;
    memset(sqliteFile, 0, sizeof(*sqliteFile));
    sqliteFile->m_fd = XFD_INVALID;
    sqliteFile->m_shmFd = XFD_INVALID;
    sqliteFile->m_parent.pMethods = &g_xsqlite_io_methods;
    path = name ? XString_create_utf8(name) : xsqlite_file_create_temp_name(file);
    if (!path) {
        sqliteFile->m_parent.pMethods = NULL;
        return SQLITE_NOMEM;
    }
    mode = xsqlite_file_open_mode(flags);
    sqliteFile->m_fd = XFileSystem_open(path, mode, &error);
    if (sqliteFile->m_fd == XFD_INVALID) {
        XString_delete_base(path);
        sqliteFile->m_parent.pMethods = NULL;
        return (flags & SQLITE_OPEN_MAIN_DB) ? SQLITE_CANTOPEN : SQLITE_CANTOPEN;
    }
    sqliteFile->m_path = path;
    sqliteFile->m_deleteOnClose = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
    sqliteFile->m_readOnly = (flags & SQLITE_OPEN_READWRITE) == 0;
    sqliteFile->m_lockLevel = SQLITE_LOCK_NONE;
    if (flags & SQLITE_OPEN_MAIN_DB) {
        sqliteFile->m_lockGroup = xsqlite_lock_group_acquire(path);
        if (!sqliteFile->m_lockGroup) {
            XFileSystem_close(sqliteFile->m_fd);
            sqliteFile->m_fd = XFD_INVALID;
            XString_delete_base(sqliteFile->m_path);
            sqliteFile->m_path = NULL;
            sqliteFile->m_parent.pMethods = NULL;
            return SQLITE_NOMEM;
        }
    }
    if (outFlags) {
        *outFlags = (flags & SQLITE_OPEN_READWRITE) ? SQLITE_OPEN_READWRITE : SQLITE_OPEN_READONLY;
    }
    return SQLITE_OK;
}

static int xsqlite_vfs_delete(sqlite3_vfs* vfs, const char* name, int syncDir)
{
    XString* path;
    (void)vfs;
    (void)syncDir;
    if (!name) return SQLITE_IOERR_DELETE;
    path = XString_create_utf8(name);
    if (!path) return SQLITE_NOMEM;
    if (!XFileSystem_exists(path)) {
        XString_delete_base(path);
        return SQLITE_OK;
    }
    if (!XFileSystem_remove(path)) {
        XString_delete_base(path);
        return SQLITE_IOERR_DELETE;
    }
    XString_delete_base(path);
    return SQLITE_OK;
}

static int xsqlite_vfs_access(sqlite3_vfs* vfs, const char* name, int flags, int* result)
{
    XString* path;
    XFileStat stat;
    (void)vfs;
    if (!name || !result) return SQLITE_IOERR_ACCESS;
    *result = 0;
    path = XString_create_utf8(name);
    if (!path) return SQLITE_NOMEM;
    if (!XFileSystem_stat(path, &stat)) {
        XString_delete_base(path);
        return SQLITE_OK;
    }
    if (flags == SQLITE_ACCESS_EXISTS) *result = stat.exists != 0;
    else if (flags == SQLITE_ACCESS_READWRITE) *result = stat.isReadable && stat.isWritable;
    else if (flags == SQLITE_ACCESS_READ) *result = stat.isReadable != 0;
    XString_delete_base(path);
    return SQLITE_OK;
}

static int xsqlite_vfs_full_pathname(sqlite3_vfs* vfs, const char* name,
                                     int outputSize, char* output)
{
    size_t length;
    (void)vfs;
    if (!name || !output || outputSize <= 0) return SQLITE_CANTOPEN;
    length = strlen(name);
    if (length >= (size_t)outputSize) return SQLITE_CANTOPEN;
    memcpy(output, name, length + 1);
    return SQLITE_OK;
}

static void* xsqlite_vfs_dl_open(sqlite3_vfs* vfs, const char* filename)
{
    (void)vfs;
    (void)filename;
    return NULL;
}

static void xsqlite_vfs_dl_error(sqlite3_vfs* vfs, int size, char* message)
{
    (void)vfs;
    if (message && size > 0) {
        const char* text = "dynamic loading is not supported";
        size_t length = strlen(text);
        if (length >= (size_t)size) length = (size_t)size - 1;
        memcpy(message, text, length);
        message[length] = 0;
    }
}

static void (*xsqlite_vfs_dl_sym(sqlite3_vfs* vfs, void* handle, const char* symbol))(void)
{
    (void)vfs;
    (void)handle;
    (void)symbol;
    return NULL;
}

static void xsqlite_vfs_dl_close(sqlite3_vfs* vfs, void* handle)
{
    (void)vfs;
    (void)handle;
}

static int xsqlite_vfs_randomness(sqlite3_vfs* vfs, int size, char* output)
{
    int index;
    (void)vfs;
    if (size <= 0 || !output) return 0;
    if (XRandomGenerator_fillSecure(output, (size_t)size)) return size;
    for (index = 0; index < size; ++index) {
        uint64_t value = (uint64_t)XDateTime_currentNSecsSinceEpoch();
        value ^= (uint64_t)(uintptr_t)output;
        value ^= (uint64_t)(unsigned)index * 0x9e3779b9U;
        output[index] = (char)(value >> ((index & 7) * 8));
    }
    return size;
}

static int xsqlite_vfs_sleep(sqlite3_vfs* vfs, int microseconds)
{
    (void)vfs;
    if (microseconds > 0) XThread_usleep((uint32_t)microseconds);
    return microseconds;
}

static int xsqlite_vfs_current_time_int64(sqlite3_vfs* vfs, sqlite3_int64* result);

static int xsqlite_vfs_current_time(sqlite3_vfs* vfs, double* result)
{
    sqlite3_int64 milliseconds;
    (void)vfs;
    if (!result) return SQLITE_ERROR;
    if (xsqlite_vfs_current_time_int64(vfs, &milliseconds) != SQLITE_OK) return SQLITE_ERROR;
    *result = (double)milliseconds / 86400000.0;
    return SQLITE_OK;
}

static int xsqlite_vfs_last_error(sqlite3_vfs* vfs, int size, char* message)
{
    (void)vfs;
    if (message && size > 0) message[0] = 0;
    return SQLITE_OK;
}

static int xsqlite_vfs_current_time_int64(sqlite3_vfs* vfs, sqlite3_int64* result)
{
    (void)vfs;
    if (!result) return SQLITE_ERROR;
    /* Julian day milliseconds = Unix epoch milliseconds + 2440587.5 days. */
    *result = (sqlite3_int64)XDateTime_currentMSecsSinceEpoch() + 210866760000000LL;
    return SQLITE_OK;
}

static sqlite3_vfs g_xsqlite_vfs = {
    2,
    (int)sizeof(XSqliteFile),
    4096,
    NULL,
    "xin_xfile",
    NULL,
    xsqlite_vfs_open,
    xsqlite_vfs_delete,
    xsqlite_vfs_access,
    xsqlite_vfs_full_pathname,
    xsqlite_vfs_dl_open,
    xsqlite_vfs_dl_error,
    xsqlite_vfs_dl_sym,
    xsqlite_vfs_dl_close,
    xsqlite_vfs_randomness,
    xsqlite_vfs_sleep,
    xsqlite_vfs_current_time,
    xsqlite_vfs_last_error,
    xsqlite_vfs_current_time_int64
};

int XSqliteVfs_register(void)
{
    static int state;
    int result;
    if (state == 1) return SQLITE_OK;
    if (state == -1) return SQLITE_ERROR;
    g_xsqlite_lockRegistryMutex = XMutex_create(XLock_NonRecursive);
    if (!g_xsqlite_lockRegistryMutex) {
        state = -1;
        return SQLITE_NOMEM;
    }
    result = sqlite3_vfs_register(&g_xsqlite_vfs, 0);
    if (result == SQLITE_OK) {
        state = 1;
        return SQLITE_OK;
    }
    state = -1;
    return result;
}
