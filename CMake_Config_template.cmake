if(MSVC)
#在MSVC上必须指定 /Zc:__cplusplus 选项    
    add_compile_options(/Zc:__cplusplus)
endif()
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)#windows系统动态库生成lib文件命令
    set(CMAKE_PREFIX_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/install/${CMAKE_BUILD_TYPE}") # Qt Kit Dir
    set(CMAKE_INSTALL_PREFIX "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/install/${CMAKE_BUILD_TYPE}") # 安装目录
elseif(UNIX)
    set(CMAKE_PREFIX_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/install/${CMAKE_BUILD_TYPE}") # Qt Kit Dir
    set(CMAKE_INSTALL_PREFIX "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/install/${CMAKE_BUILD_TYPE}") # 安装目录
endif()
#显示
message(STATUS "CMAKE_BUILD_TYPE= ${CMAKE_BUILD_TYPE}")	
message(STATUS "CMAKE_PREFIX_PATH= ${CMAKE_PREFIX_PATH}")	
message(STATUS "CMAKE_INSTALL_PREFIX= ${CMAKE_INSTALL_PREFIX}")	
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