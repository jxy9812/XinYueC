 #安装头文件
install(FILES ${PUBLIC_HDRS} DESTINATION "include/${EXPORT_NAME}")

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
