# ==================== zlib 压缩库 ====================
# 通用的数据压缩库（deflate/gzip 算法）。
# 部分文件引用了 XinYueC 的 XMemory.h / XCrc.h，需添加 Src 包含路径。

# --- 公开头文件路径（传播给链接方）---
set(ZLIB_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)

# 收集所有源文件
file(GLOB ZLIB_SRCS "*.c")

# 创建静态库
add_library(zlib STATIC ${ZLIB_SRCS})
set_target_properties(zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 传播头文件路径
target_include_directories(zlib PUBLIC ${ZLIB_PUBLIC_INCS})

