# ==================== XinYueC · arm-none-eabi-gcc 交叉工具链 ====================
# 用法（仓库根目录，生成器按本机可用工具选择 MinGW Makefiles/Ninja）：
#   cmake -B build-arm -G "MinGW Makefiles" -DCMAKE_TOOLCHAIN_FILE=Drive/STM32/arm_none_eabi_gcc.toolchain.cmake
#   cmake --build build-arm --target XinYueCS
#
# 行为说明：
# - CMAKE_SYSTEM_NAME=Generic（裸机）：UNIX/WIN32/ANDROID 全为假，桌面平台
#   源文件经自身预处理守卫整段裁剪，X11/DBus/libusb/pcap 等宿主探测全部跳过；
# - 自动置位 XINYUE_PLATFORM_FREERTOS=ON：主 CMakeLists 据此把 FreeRTOS 内核
#   （Source + 内存堆 + port）编入 XinYueCS 并全局定义 __FreeRTOS__，使
#   Sync/Core 适配层激活、XMemory 分配回落 pvPortMalloc/vPortFree；
# - MCU 默认按 STM32F407（Cortex-M4F，仓库现有 STM32 驱动的目标族）；
#   换芯片重设 XINYUE_ARM_CPU/XINYUE_ARM_FPU/XINYUE_ARM_FLOAT_ABI 与
#   XINYUE_FREERTOS_PORT（如 ARM_CM7）即可；
# - 启动文件/链接脚本/中断向量接线（PendSV/SVC/SysTick）属板级应用工程，
#   不在本库范围；本库只产出静态库 XinYueCS。

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---- 目标 MCU（-D 覆盖缓存变量即可换芯片） ----
set(XINYUE_ARM_CPU       "cortex-m4"   CACHE STRING "ARM CPU（-mcpu 值）")
set(XINYUE_ARM_FPU       "fpv4-sp-d16" CACHE STRING "FPU（-mfpu 值；无 FPU 内核置空）")
set(XINYUE_ARM_FLOAT_ABI "hard"        CACHE STRING "浮点 ABI（hard/softfp/soft）")

# ---- 编译器定位：先 PATH，再常见的 EIDE 工具链安装位置 ----
find_program(XINYUE_ARM_GCC NAMES arm-none-eabi-gcc
    PATHS "$ENV{USERPROFILE}/.eide/tools/gcc_arm/bin"
          "$ENV{USERPROFILE}/.eide/tools"
    PATH_SUFFIXES bin gcc_arm/bin
    DOC "arm-none-eabi-gcc 可执行文件")
if(NOT XINYUE_ARM_GCC)
    message(FATAL_ERROR "未找到 arm-none-eabi-gcc：请将 GNU Arm Embedded 工具链"
        "加入 PATH，或 -DXINYUE_ARM_GCC=<路径> 显式指定")
endif()
set(CMAKE_C_COMPILER "${XINYUE_ARM_GCC}")

# 裸机无可执行链接环境，编译器探测只做静态库
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---- 全局架构旗标（经 *_INIT 注入缓存，构建系统追加不覆盖） ----
set(XINYUE_ARM_ARCH_FLAGS "-mcpu=${XINYUE_ARM_CPU} -mthumb")
if(XINYUE_ARM_FPU)
    string(APPEND XINYUE_ARM_ARCH_FLAGS
        " -mfpu=${XINYUE_ARM_FPU} -mfloat-abi=${XINYUE_ARM_FLOAT_ABI}")
endif()
set(CMAKE_C_FLAGS_INIT
    "${XINYUE_ARM_ARCH_FLAGS} -ffunction-sections -fdata-sections")
# 链接初始旗标：newlib-nano + nosys 为裸机常见口径，最终 ELF 由应用工程链接
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${XINYUE_ARM_ARCH_FLAGS} --specs=nano.specs --specs=nosys.specs -Wl,--gc-sections")

# 激活主 CMakeLists 的 FreeRTOS 平台分支（内核入列 + __FreeRTOS__ + 宿主产物跳过）
set(XINYUE_PLATFORM_FREERTOS ON CACHE BOOL
    "FreeRTOS/裸机平台（由本工具链文件自动置位）" FORCE)
