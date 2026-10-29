# ==================== Industry（工业设备层）====================
# 通道接口（XChannel：XIoChannel + DI/DO/AI/AO 四接口 + 传输适配器）与
# 工业设备（XSolenoidValve 电磁阀等）以子目录静态库方式集成（与 Library/
# 同款规矩）。依赖方向：XinYueIndustry → XinYueC 主库（XObject/XMemory 等
# 符号在链接期由消费者按库序带主库解析），故本库不反链主库。

# 收集源文件；test/ 是自带 main() 的测试可执行，不入库
# （XGuiDemo 同款规矩：自带 main 的 TU 不得卷进库目标）。
file(GLOB_RECURSE INDUSTRY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
file(GLOB_RECURSE INDUSTRY_TEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/test/*.c")
if(INDUSTRY_TEST_FILE)
    list(REMOVE_ITEM INDUSTRY_FILE ${INDUSTRY_TEST_FILE})
endif()

# 创建静态库
add_library(XinYueIndustry STATIC ${INDUSTRY_FILE})
set_target_properties(XinYueIndustry PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 传播头文件路径（XChannel/XSolenoidValve 子目录供裸文件名 include）
target_include_directories(XinYueIndustry PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/XChannel"
    "${CMAKE_CURRENT_SOURCE_DIR}/XSolenoidValve")

# ==================== 电磁阀状态机测试 ====================
# mock 通道离线测试：None/DualLimit 到位、超时转故障、双限位矛盾、
# 写拒绝、解挂隔离、接口默认槽与 XIoChannel 公共状态。无硬件依赖。
add_executable(XIndustryValve_Test
    "${CMAKE_CURRENT_SOURCE_DIR}/test/xindustry_valve_test.c"
)
if(UNIX)
    target_link_libraries(XIndustryValve_Test
        XinYueIndustry ${EXPORT_NAME}S
        ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XIndustryValve_Test
        XinYueIndustry ${EXPORT_NAME}S
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== Modbus 适配器端到端测试 ====================
# 同进程 TCP 环回（XModbusTcpServer 自动端口作从站）：DI 轮询/翻转推送、
# AI 标度换算、AO/DO writeDone + 服务端读回、AI(holding) 全链读回闭环。
# 骨架对齐 XModbusPublicApiTest 的 test_tcp_loopback 先例。
add_executable(XIndustryModbus_Test
    "${CMAKE_CURRENT_SOURCE_DIR}/test/xindustry_modbus_test.c"
)
if(UNIX)
    target_link_libraries(XIndustryModbus_Test
        XinYueIndustry ${EXPORT_NAME}S
        ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS}
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XIndustryModbus_Test
        XinYueIndustry ${EXPORT_NAME}S
        mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
