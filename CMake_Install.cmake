file(GLOB_RECURSE PUBLIC_HDRS  "Src/*.h" "Src/*.hpp" )
file(GLOB_RECURSE TEST_HDRS  "Test/*.h" "Test/*.hpp" )
#安装库头文件
install(FILES ${PUBLIC_HDRS} DESTINATION "Src/include")
#安装库源文件
install(FILES ${SRC_FILE} DESTINATION "Src")
#安装测试头文件
install(FILES ${TEST_HDRS} DESTINATION "XDataStructTest/include")
#安装测试源文件
install(FILES ${TEST_FILE} DESTINATION "XDataStructTest")

set_target_properties(${EXPORT_NAME} PROPERTIES
    OUTPUT_NAME ${EXPORT_NAME}
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
    PUBLIC_HEADER "${PUBLIC_HDRS}"
)

#安装库
 install(TARGETS ${EXPORT_NAME} ${EXPORT_NAME}static
        EXPORT ${EXPORT_NAME}Targets # 导出
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
 )

# 生成 xxxTargets.cmake文件
install(
	EXPORT ${EXPORT_NAME}Targets
	DESTINATION lib/cmake/${EXPORT_NAME}
    FILE ${EXPORT_NAME}Targets.cmake
    #NAMESPACE ${EXPORT_NAME}::
)
#======================生成 xxxConfig.cmake===============================
# 该变量会通过xxxConfig.cmake.in用于在生成的xxxConfig.cmake中
set(INCLUDE_DIRS include/${EXPORT_NAME})
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
