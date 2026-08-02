# SQLite source component

This directory contains the SQLite amalgamation used by the XinYueC SQL
source adapter. The current source is SQLite 3.49.1, copied from the Qt 6.8.3
source tree at `Src/qtbase/src/3rdparty/sqlite`.

SQLite is public-domain software. The upstream source headers contain the
complete SQLite license notice. The XinYueC-specific file
`sqlite3_xin_memory.c` routes SQLite allocations through `XMemory` before
SQLite is initialized.

The amalgamation is a replaceable source component. File-backed SQLite
connections use the `xin_xfile` VFS in `sqlite3_xin_vfs.c`. Its file operations
call the platform-neutral `XFileSystem_*` API from XinYueC's XFile module;
there is no POSIX, Win32, or FatFs call in the SQLite adapter. An embedded
target may keep the same `XSqliteDriver` adapter and replace the SQLite source,
compile options, or XFile backend without changing the public SQL classes.

The VFS supports both SQLite's rollback-journal path and WAL shared mapping.
WAL regions are opened as the `-shm` sidecar and mapped through
`XFileSystem_map`/`XFileSystem_unmap`. The VFS uses an `XMutex`-protected
path registry and one `XReadWriteLock` for each SQLite WAL lock slot, so
connections in the same process share the lock group. Targets that need
multiple processes must add inter-process locking to the XFile abstract API.
