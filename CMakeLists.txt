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

#引入外部库
include_directories("Library/zlib")
file(GLOB_RECURSE TEMP_LIBRARY_FILE "Library/zlib/*.c")
list(APPEND LIBRARY_FILE ${TEMP_LIBRARY_FILE})
include_directories("Library/Fatfs")
file(GLOB_RECURSE TEMP_LIBRARY_FILE "Library/Fatfs/*.c")
list(APPEND LIBRARY_FILE ${TEMP_LIBRARY_FILE})

include_directories("Library/lwip")
include_directories("Library/lwip/include")
file(GLOB_RECURSE TEMP_LIBRARY_FILE "Library/lwip/*.c")
list(APPEND LIBRARY_FILE ${TEMP_LIBRARY_FILE})

# ==================== mbedTLS（XSsl SSL/TLS 后端）====================
# 显式清单集成：按三层库（crypto / x509 / TLS）组织，仅编必需源码；
# 后量子密码 pqcp 体积大（约2.3MB），默认关闭，按需开启。
# mbedTLS 平台依赖在 XSsl_mbedtls.c 中桥接到 XinYueC：随机数->XRandomGenerator，
# 内存->XMemory(XMalloc_System/XMalloc_MultiPool)，文件->XFile，网络BIO->XAbstractSocket。
option(MBEDTLS_ENABLE_PQCP "编译后量子密码 pqcp 驱动（嵌入式默认关）" OFF)

# 头文件路径：TLS/X.509 公开头 + 加密头 + 内部头 + builtin 驱动头
include_directories("Library/mbedtls/include")
include_directories("Library/mbedtls/tf-psa-crypto/include")
include_directories("Library/mbedtls/library")
include_directories("Library/mbedtls/tf-psa-crypto/core")
include_directories("Library/mbedtls/tf-psa-crypto/drivers/builtin/include")
# mbedTLS 内部私有头（对应原生 CMake 的 TF_PSA_CRYPTO_PRIVATE_INCLUDE_DIRS）
include_directories("Library/mbedtls/tf-psa-crypto/utilities")
include_directories("Library/mbedtls/tf-psa-crypto/platform")
include_directories("Library/mbedtls/tf-psa-crypto/extras")
include_directories("Library/mbedtls/tf-psa-crypto/drivers/builtin/src")
include_directories("Library/mbedtls/tf-psa-crypto/dispatch")

# 1) 加密核心（tf-psa-crypto，独立库）+ 经典算法 builtin
file(GLOB TEMP_LIBRARY_FILE
    "Library/mbedtls/tf-psa-crypto/core/*.c"
    "Library/mbedtls/tf-psa-crypto/utilities/*.c"
    "Library/mbedtls/tf-psa-crypto/platform/*.c"
    "Library/mbedtls/tf-psa-crypto/extras/*.c"
    "Library/mbedtls/tf-psa-crypto/drivers/builtin/src/*.c")
list(APPEND LIBRARY_FILE ${TEMP_LIBRARY_FILE})

# 2) X.509 + TLS（顶层 library）
file(GLOB TEMP_LIBRARY_FILE "Library/mbedtls/library/*.c")
list(APPEND LIBRARY_FILE ${TEMP_LIBRARY_FILE})

# 3) 后量子密码（可选，默认关）
if(MBEDTLS_ENABLE_PQCP)
    include_directories("Library/mbedtls/tf-psa-crypto/drivers/pqcp/include")
    file(GLOB_RECURSE TEMP_LIBRARY_FILE "Library/mbedtls/tf-psa-crypto/drivers/pqcp/*.c")
    list(APPEND LIBRARY_FILE ${TEMP_LIBRARY_FILE})
endif()

# Enable the XSsl mbedTLS backend (XSsl_mbedtls.c is guarded by XSSL_USE_MBEDTLS)
add_compile_definitions(XSSL_USE_MBEDTLS)

add_library(${EXPORT_NAME}S  STATIC  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE})
add_library(${EXPORT_NAME}  SHARED  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE} ${PUBLIC_HDRS})
# Windows(MSVC): mbedtls_platform_get_entropy() 调用 BCryptGenRandom（需 bcrypt.lib）。
# 已开启 MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG，TLS/PSA 运行时随机数走 XRandomGenerator；
# 但 platform_util.c 仍被编译并链接，故 Windows 上必须链接 bcrypt 解析符号。
# Linux 用 getrandom()/urandom，无需额外库。
if(WIN32)
    target_link_libraries(${EXPORT_NAME}S bcrypt)
    target_link_libraries(${EXPORT_NAME}  bcrypt)
endif()

#添加参加编译的文件
add_executable(${PROJECT_NAME}
${TEST_FILE}
"./main.c"
 )
# Windows(MSVC): 数学函数内置于 C 运行时，线程使用 Drive/windows 的 Win32 实现，无需链接 m/pthread
# Unix(Linux/macOS/BSD): 需链接数学库 m 与线程库 pthread（对应 Drive/Posix 实现，源码已用
#                         __linux__/__APPLE__/__BSD__ 宏守护，仅在这些平台编译）
if(UNIX)
    target_link_libraries(${PROJECT_NAME} ${EXPORT_NAME} m pthread)
else()
    target_link_libraries(${PROJECT_NAME} ${EXPORT_NAME})
endif()
#安装
include(CMake_Install.cmake)