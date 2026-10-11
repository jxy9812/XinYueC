# xxHash（上游 Yann Collet，BSD-2-Clause）
#
# xxhash.c/xxhash.h 保持上游原样、作为独立可替换源组件；
# XCryptographicHash_XxHash64 经 XCryptographicHash.c 桥接（#include "xxhash.h"，
# include 目录 PUBLIC 传播给上层目标）。
set(XXHASH_SOURCES
    xxhash.c
)

add_library(xxhash STATIC ${XXHASH_SOURCES})
set_target_properties(xxhash PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(xxhash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
