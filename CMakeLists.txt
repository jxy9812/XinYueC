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

#设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)
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

add_library(${EXPORT_NAME}S  STATIC  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE})
add_library(${EXPORT_NAME}  SHARED  ${SRC_FILE} ${DRIVE_FILE} ${LIBRARY_FILE} ${PUBLIC_HDRS})

#添加参加编译的文件
add_executable(${PROJECT_NAME}
${TEST_FILE}
"./main.c"
 )
 # 区分系统：仅在 Linux 下链接数学库
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Linux 系统需要显式链接数学库 libm
    target_link_libraries(${PROJECT_NAME} ${EXPORT_NAME} m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    # Windows 系统通常不需要链接额外的数学库（数学函数在 msvcrt 等默认库中）
    # 若有特殊需求可在此添加 Windows 特定链接逻辑
    target_link_libraries(${PROJECT_NAME} ${EXPORT_NAME})

    # 操作物理磁盘需要管理员权限，通过清单文件请求UAC提权
    #set_target_properties(${PROJECT_NAME} PROPERTIES
     #   LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

#安装
include(CMake_Install.cmake)