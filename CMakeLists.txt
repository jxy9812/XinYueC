#请求CAMKE的最小构建版本
cmake_minimum_required(VERSION 3.5)
#设置项目名称
project(Container VERSION 0.1 LANGUAGES C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)


#自动查找头文件路径函数(没有去重)
macro(FIND_INCLUDE_DIR result curdir)  #定义函数,2个参数:存放结果result；指定路径curdir；
    file(GLOB_RECURSE children "${curdir}/*.hpp" "${curdir}/*.h" )	#遍历获取{curdir}中*.hpp和*.h文件列表
    message(STATUS "children= ${children}")								#打印*.hpp和*.h的文件列表
    set(dirlist "")														#定义dirlist中间变量，并初始化
    foreach(child ${children})											#for循环
        string(REGEX REPLACE "(.*)/.*" "\\1" LIB_NAME ${child})			#字符串替换,用/前的字符替换/*h
        if(IS_DIRECTORY ${LIB_NAME})									#判断是否为路径
            LIST(APPEND dirlist ${LIB_NAME})							#将合法的路径加入dirlist变量中
        endif()															#结束判断
    endforeach()														#结束for循环
    set(${result} ${dirlist})											#dirlist结果放入result变量中
endmacro()																#函数结束

#查找include目录下的所有*.hpp,*.h头文件,并路径列表保存到 INCLUDE_DIR_LIST 变量中
FIND_INCLUDE_DIR(INCLUDE_DIR_LIST "CodeFile")			#调用函数，指定参数
#将INCLUDE_DIR_LIST中路径列表加入工程		
include_directories(${INCLUDE_DIR_LIST})
#递归搜索目录下的文件添加到变量中
file(GLOB_RECURSE FILE "CodeFile/*.c" "CodeFile/*.h")

#设置图标
#aux_source_directory(. MY_SCOURCES)
#设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/bin)
#添加参加编译的文件
add_executable(${PROJECT_NAME}
${FILE}
 )