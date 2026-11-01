# ==================== 安装收尾（文件安装 + 包配置生成） ====================
# [2026-11-01 模块化拆分] install(TARGETS) 只能作用于本目录创建的目标——
# 动/静态库安装已迁 Src/CMakeLists.txt，主程序双 exe 安装迁
# Test/CMakeLists.txt。本文件保留与目标解耦的部分：头/源文件安装（就地
# 重采，原引用根作用域的 SRC_FILE/TEST_FILE 变量已下放各模块）与
# xxxTargets.cmake / xxxConfig.cmake 包配置生成。
file(GLOB_RECURSE PUBLIC_HDRS  "Src/*.h" "Src/*.hpp" )
file(GLOB_RECURSE TEST_HDRS  "Test/*.h" "Test/*.hpp" )
file(GLOB_RECURSE INSTALL_SRC_FILES  "Src/*.c" )
file(GLOB_RECURSE INSTALL_TEST_FILES  "Test/*.c" )
#安装库头文件
install(FILES ${PUBLIC_HDRS} DESTINATION "Src/include")
#安装库源文件
install(FILES ${INSTALL_SRC_FILES} DESTINATION "Src")
#安装测试头文件
install(FILES ${TEST_HDRS} DESTINATION "XCXinYueTest/include")
#安装测试源文件
install(FILES ${INSTALL_TEST_FILES} DESTINATION "XCXinYueTest")

# 生成 xxxTargets.cmake文件（导出集由动态库 install(TARGETS ... EXPORT)
# 注册——install(TARGETS ... EXPORT) 在 Drive 模块；嵌入式分支无动态库目标，
# 随同跳过）
if(NOT XINYUE_EMBEDDED)
install(
	EXPORT ${EXPORT_NAME}Targets
	DESTINATION lib/cmake/${EXPORT_NAME}
    FILE ${EXPORT_NAME}Targets.cmake
    #NAMESPACE ${EXPORT_NAME}::
)
endif() # NOT XINYUE_EMBEDDED
#======================生成 xxxConfig.cmake===============================
# 该变量会通过xxxConfig.cmake.in用于在生成的xxxConfig.cmake中
set(INCLUDE_DIRS Src/include)
set(LIBRARIES ${EXPORT_NAME})
set(LIB_DIR lib)

# 由cmake提供
include(CMakePackageConfigHelpers)

# 生成 xxxConfigVersion.cmake文件
write_basic_package_version_file(
	${PROJECT_BINARY_DIR}/${EXPORT_NAME}ConfigVersion.cmake
	VERSION ${VERSION}
	COMPATIBILITY SameMajorVersion
)
# 用于生成 xxxConfig.cmake文件
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/${EXPORT_NAME}Config.cmake.in
                              "${CMAKE_CURRENT_BINARY_DIR}/${EXPORT_NAME}Config.cmake"
                              INSTALL_DESTINATION lib/cmake/${EXPORT_NAME}
                              PATH_VARS INCLUDE_DIRS LIBRARIES LIB_DIR
                              INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
install(
	FILES ${PROJECT_BINARY_DIR}/${EXPORT_NAME}Config.cmake ${PROJECT_BINARY_DIR}/${EXPORT_NAME}ConfigVersion.cmake
	DESTINATION lib/cmake/${EXPORT_NAME}
)
