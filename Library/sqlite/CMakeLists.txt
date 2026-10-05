# SQLite amalgamation
#
# sqlite3.c is kept as a separately replaceable source component. Embedded
# targets can replace this directory or provide a platform VFS without
# changing the XinYueC SQL adapter.
set(SQLITE_SOURCES
    sqlite3.c
    sqlite3_xin_memory.c
    sqlite3_xin_vfs.c
    sqlite3_xin_mutex.c
)

add_library(sqlite STATIC ${SQLITE_SOURCES})
set_target_properties(sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sqlite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(sqlite PRIVATE ${PROJECT_SOURCE_DIR}/Src/XMemory)
# 全平台统一互斥层：SQLITE_MUTEX_APPDEF 内核不带 pthread/Win32 互斥，
# 由 sqlite3_xin_mutex.c 经 sqlite3_config(SQLITE_CONFIG_MUTEX) 安装
# XMutex 桥接（XSqliteMemory_initialize 统一初始化链内完成）。
target_compile_definitions(sqlite PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_MUTEX_APPDEF
)
if(XINYUE_EMBEDDED)
# 裸机（FreeRTOS/ARM-GCC 交叉分支）：SQLITE_OS_OTHER=1 使 amalgamation 的
# Unix/Win OS 层（os_unix.c 的 ioctl、pthread 互斥）整体退位；VFS 与内存
# 由 XinYueC 适配器接管——XSqliteMemory_initialize 注册 SQLITE_CONFIG_MALLOC
# （XMemory）与 XinYueC VFS（桥接 XFile，sqlite3_xin_vfs.c）。
target_compile_definitions(sqlite PRIVATE
    SQLITE_OS_OTHER=1
)
endif()
target_compile_definitions(sqlite PRIVATE
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_ENABLE_COLUMN_METADATA
)
