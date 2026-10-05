# ==================== 神舟号 STM32F407ZGT6 板级固件 ====================
# 由主 CMakeLists 的 FreeRTOS 分支经 XINYUE_BOARD_SHENZHOU_F407=ON 引入。
# 产物：XGuiDemoF407.elf / .bin / .hex（bin 目录下），J-Link/STM32 烧录；
# 板级源文件（app/cmsis）不在 XinYueCS 里（主 CMakeLists 已全局排除），
# 演示源复用 Test/XGuiDemo 的 XGUI_DEMO_FILE glob（剔除自带 main 的
# xgui_demo_remote.c）。
# 家族通用件（CMSIS 启动/系统、newlib 存根）在父目录 Drive/STM32/。
set(BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(STM32_DIR "${BOARD_DIR}" DIRECTORY)

# 工程主 project() 只声明了 C；启动文件 startup_stm32f40_41xxx.s 需要 ASM。
enable_language(ASM)

file(GLOB BOARD_APP_FILE "${BOARD_DIR}/app/*.c")
set(BOARD_CMSIS_FILE
    "${STM32_DIR}/cmsis/startup_stm32f40_41xxx.s"
    "${STM32_DIR}/cmsis/system_stm32f4xx.c")
set(BOARD_NEWLIB_STUBS "${STM32_DIR}/newlib_stubs.c")
set(BOARD_LD "${BOARD_DIR}/link/stm32f407zg.ld")

set(BOARD_DEMO_FILE ${XGUI_DEMO_FILE})
list(FILTER BOARD_DEMO_FILE EXCLUDE REGEX "xgui_demo_remote\\.c$")

add_executable(XGuiDemoF407.elf
    ${BOARD_APP_FILE}
    ${BOARD_CMSIS_FILE}
    ${BOARD_NEWLIB_STUBS}
    ${BOARD_DEMO_FILE})
set_property(TARGET XGuiDemoF407.elf PROPERTY SUFFIX "")

target_include_directories(XGuiDemoF407.elf PRIVATE
    "${STM32_DIR}/cmsis"
    "${BOARD_DIR}/app")
# CMSIS 设备族选择 + 板上晶振 8MHz（时钟树 PLL_M=8 → SYSCLK 168MHz）。
target_compile_definitions(XGuiDemoF407.elf PRIVATE
    STM32F40_41xxx
    HSE_VALUE=8000000
    XGUI_DEMO_APITEST_ON=0)
target_link_libraries(XGuiDemoF407.elf XinYueCS)
# 归档单遍扫描的循环依赖（psa_crypto → XCryptographic 桥接，桌面端用
# 「首尾各链一次」解法，CMake 目标去重无法复刻）：
# -u 强制首轮拉入 XCryptographic.c.obj（全部桥接符号所在成员），
# 其余不可达代码由 --gc-sections 裁掉。
target_link_options(XGuiDemoF407.elf PRIVATE
    "-Wl,-u,XCryptographic_hkdfExpandInto")
# 链接脚本 + 浮点格式化（demo 的 %.0f 屏幕尺寸打印）+ 符号表。
target_link_options(XGuiDemoF407.elf PRIVATE
    "-T${BOARD_LD}"
    "-u_printf_float"
    "-Wl,-Map=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/XGuiDemoF407.map,--cref")
set_target_properties(XGuiDemoF407.elf PROPERTIES LINK_DEPENDS "${BOARD_LD}")

find_program(XINYUE_ARM_OBJCOPY arm-none-eabi-objcopy
    HINTS "$ENV{USERPROFILE}/.eide/tools/gcc_arm/bin")
if(NOT XINYUE_ARM_OBJCOPY)
    message(FATAL_ERROR "未找到 arm-none-eabi-objcopy（生成 bin/hex 用）")
endif()
find_program(XINYUE_ARM_SIZE arm-none-eabi-size
    HINTS "$ENV{USERPROFILE}/.eide/tools/gcc_arm/bin")
add_custom_command(TARGET XGuiDemoF407.elf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND ${XINYUE_ARM_OBJCOPY} -O binary
        $<TARGET_FILE:XGuiDemoF407.elf>
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/XGuiDemoF407.bin"
    COMMAND ${XINYUE_ARM_OBJCOPY} -O ihex
        $<TARGET_FILE:XGuiDemoF407.elf>
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/XGuiDemoF407.hex"
    COMMAND ${XINYUE_ARM_SIZE} -A
        $<TARGET_FILE:XGuiDemoF407.elf>
    COMMENT "objcopy → XGuiDemoF407.bin/.hex")
