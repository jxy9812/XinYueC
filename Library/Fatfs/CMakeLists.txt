# ==================== FatFs 文件系统 ====================
# 通用的 FAT/exFAT 文件系统模块，适用于嵌入式系统。
# 包含 XinYueC 文件系统适配层（XFileSystem_Fatfs.c / XFileSystem_Fatfs_platform.h）。

# --- 公开头文件路径（传播给链接方）---
set(FATFS_PUBLIC_INCS
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
)

# 收集所有源文件
file(GLOB FATFS_SRCS "*.c")

# 创建静态库
add_library(fatfs STATIC ${FATFS_SRCS})

# 传播头文件路径
target_include_directories(fatfs PUBLIC ${FATFS_PUBLIC_INCS})
