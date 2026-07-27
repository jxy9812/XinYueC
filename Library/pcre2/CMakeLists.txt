# ==================== PCRE2 正则表达式库 ====================
# PCRE2 10.45，来源为 Qt 6.8.3 内置的 PCRE2 源码。
#
# XinYueC 与 Qt 6.8 一样固定使用 16-bit API，匹配输入为 UTF-16 code unit。
# pcre2_jit_compile.c 会按需内联 deps/sljit 中的 SLJIT 实现，因此不需要
# 将 SLJIT 单独构建为目标。

set(PCRE2_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)

set(PCRE2_SRCS
    src/config.h
    src/pcre2.h
    src/pcre2_auto_possess.c
    src/pcre2_chartables.c
    src/pcre2_chkdint.c
    src/pcre2_compile.c
    src/pcre2_compile.h
    src/pcre2_compile_class.c
    src/pcre2_config.c
    src/pcre2_context.c
    src/pcre2_dfa_match.c
    src/pcre2_error.c
    src/pcre2_extuni.c
    src/pcre2_find_bracket.c
    src/pcre2_internal.h
    src/pcre2_intmodedep.h
    src/pcre2_jit_compile.c
    src/pcre2_match.c
    src/pcre2_match_data.c
    src/pcre2_newline.c
    src/pcre2_ord2utf.c
    src/pcre2_pattern_info.c
    src/pcre2_script_run.c
    src/pcre2_serialize.c
    src/pcre2_string_utils.c
    src/pcre2_study.c
    src/pcre2_substitute.c
    src/pcre2_substring.c
    src/pcre2_tables.c
    src/pcre2_ucd.c
    src/pcre2_ucp.h
    src/pcre2_valid_utf.c
    src/pcre2_xclass.c
    pcre2_xin_memory.c
    pcre2_xin_memory.h
)

option(XIN_PCRE2_ENABLE_JIT "启用 PCRE2 JIT 优化（不影响匹配结果）" ON)

add_library(pcre2 STATIC ${PCRE2_SRCS})
set_target_properties(pcre2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(pcre2 PUBLIC ${PCRE2_PUBLIC_INCS})
target_include_directories(pcre2 PRIVATE
    "${PROJECT_SOURCE_DIR}/Src"
    "${PROJECT_SOURCE_DIR}/Src/XMemory"
)
target_compile_definitions(pcre2
    PRIVATE
        HAVE_CONFIG_H
    PUBLIC
        PCRE2_CODE_UNIT_WIDTH=16
)

if(WIN32)
    # pcre2.h 默认按 DLL 导入符号处理，XinYueC 集成的是静态库。
    target_compile_definitions(pcre2 PUBLIC PCRE2_STATIC)
endif()

if(NOT XIN_PCRE2_ENABLE_JIT)
    target_compile_definitions(pcre2 PRIVATE PCRE2_DISABLE_JIT)
endif()

# 与 Qt 6.8 保持相同的 JIT 平台边界：这些平台先使用解释执行。
if(QNX OR IOS OR TVOS)
    target_compile_definitions(pcre2 PRIVATE PCRE2_DISABLE_JIT)
endif()

if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM|ARM64|arm|arm64|aarch64)")
    target_compile_definitions(pcre2 PRIVATE PCRE2_DISABLE_JIT)
endif()

if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)")
    target_compile_definitions(pcre2 PRIVATE PCRE2_DISABLE_JIT)
endif()
