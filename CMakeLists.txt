#请求CAMKE的最小构建版本
cmake_minimum_required(VERSION 3.5)
#设置项目名称
project(Xin_Yue_C VERSION 0.1 LANGUAGES C)
set(EXPORT_NAME XinYueC)
#set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -O0  -Wall")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0  -Wall")

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_DEBUG_POSTFIX "d")

# Linux 平台需要 _GNU_SOURCE 以使用 PTHREAD_MUTEX_NORMAL 等
if(UNIX AND NOT APPLE)
    add_compile_definitions(_GNU_SOURCE)
endif()

#设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)

# ==================== GBK转换实现切换 ====================
# 启用文件读写方式实现GBK转换（适用于嵌入式），注释此行则使用平台系统API
#add_compile_definitions(XCHAR_USE_FILE_GBK)
# 映射表文件路径（干净格式，无注释，固定14字节/行，支持文件二分）
add_compile_definitions(XCHAR_COMPACT_PATH="${CMAKE_CURRENT_LIST_DIR}/Library/XCHAR_COMPACT.BIN")
include(CMake_Config.cmake)
#查找include目录下的所有*.hpp,*.h头文件,并路径列表保存到变量中
FIND_INCLUDE_DIR(INCLUDE_SRC_LIST "Src")			#调用函数，指定参数
FIND_INCLUDE_DIR(INCLUDE_DRIVE_LIST "Drive")			#调用函数，指定参数
FIND_INCLUDE_DIR(INCLUDE_TEST_LIST "Test")			#调用函数，指定参数
#将INCLUDE中路径列表加入工程		
include_directories(${INCLUDE_SRC_LIST})
include_directories(${INCLUDE_DRIVE_LIST})
include_directories(${INCLUDE_TEST_LIST})
#递归搜索目录下的文件添加到变量中
file(GLOB_RECURSE SRC_FILE "Src/*.c" )
file(GLOB_RECURSE TEST_FILE "Test/*.c" )
file(GLOB_RECURSE DRIVE_FILE "Drive/*.c")
add_library(${EXPORT_NAME}S  STATIC  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE})
add_library(${EXPORT_NAME}  SHARED  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE} ${PUBLIC_HDRS})

# ==================== zlib（压缩库）====================
# 以子目录静态库方式集成，详见 Library/zlib/CMakeLists.txt
add_subdirectory(Library/zlib)
# ==================== FatFs（FAT/exFAT 文件系统）====================
# 以子目录静态库方式集成，详见 Library/Fatfs/CMakeLists.txt
add_subdirectory(Library/Fatfs)

# ==================== lwIP（轻量级 TCP/IP 协议栈）====================
# 以子目录静态库方式集成，详见 Library/lwip/CMakeLists.txt
add_subdirectory(Library/lwip)

# ==================== mbedTLS（XSsl SSL/TLS 后端）====================
# 以子目录静态库方式集成，详见 Library/mbedtls/CMakeLists.txt
add_subdirectory(Library/mbedtls)



# 链接 mbedTLS 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE mbedtls)
target_link_libraries(${EXPORT_NAME} PRIVATE mbedtls)
# 链接 lwIP 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE lwip)
target_link_libraries(${EXPORT_NAME} PRIVATE lwip)
# 链接 zlib 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE zlib)
target_link_libraries(${EXPORT_NAME} PRIVATE zlib)
# 链接 FatFs 静态库
target_link_libraries(${EXPORT_NAME}S PRIVATE fatfs)
target_link_libraries(${EXPORT_NAME} PRIVATE fatfs)
# Windows(MSVC): mbedtls_platform_get_entropy() 调用 BCryptGenRandom（需 bcrypt.lib）。
# 已开启 MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG，TLS/PSA 运行时随机数走 XRandomGenerator；
# 但 platform_util.c 仍被编译并链接，故 Windows 上必须链接 bcrypt 解析符号。
# Linux 用 getrandom()/urandom，无需额外库。

#添加参加编译的文件
add_executable(${PROJECT_NAME}
${TEST_FILE}
"./main.c"
 )
# Windows(MSVC): 数学函数内置于 C 运行时，线程使用 Drive/windows 的 Win32 实现，无需链接 m/pthread
# Unix(Linux/macOS/BSD): 需链接数学库 m 与线程库 pthread（对应 Drive/Posix 实现，源码已用
#                         __linux__/__APPLE__/__BSD__ 宏守护，仅在这些平台编译）
if(UNIX)
    target_link_libraries(${PROJECT_NAME} ${EXPORT_NAME}S m pthread mbedtls lwip zlib fatfs)
else()
    target_link_libraries(${PROJECT_NAME} ${EXPORT_NAME}S mbedtls lwip zlib fatfs)
endif()
#安装
include(CMake_Install.cmake)