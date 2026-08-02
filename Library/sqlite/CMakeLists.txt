# SQLite amalgamation
#
# sqlite3.c is kept as a separately replaceable source component. Embedded
# targets can replace this directory or provide a platform VFS without
# changing the XinYueC SQL adapter.
set(SQLITE_SOURCES
    sqlite3.c
    sqlite3_xin_memory.c
    sqlite3_xin_vfs.c
)

add_library(sqlite STATIC ${SQLITE_SOURCES})
set_target_properties(sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(sqlite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(sqlite PRIVATE ${PROJECT_SOURCE_DIR}/Src/XMemory)
target_compile_definitions(sqlite PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_ENABLE_COLUMN_METADATA
)
