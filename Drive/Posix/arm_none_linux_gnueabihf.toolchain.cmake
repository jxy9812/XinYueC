# ==================== XinYueC · arm-none-linux-gnueabihf 交叉工具链 ====================
# 用法（仓库根目录；产物输出目录必须显式指向仓库外/私有目录——见主 CMakeLists
# 的交叉构建 bin/ 守卫，严禁覆盖桌面门禁二进制）：
#   cmake -B build-rk3506/out -G Ninja ^
#     -DCMAKE_TOOLCHAIN_FILE=Drive/Posix/arm_none_linux_gnueabihf.toolchain.cmake ^
#     -DXARM_LINUX_GNUEABIHF_ROOT="<arm-none-linux-gnueabihf 工具链根>" ^
#     -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=<私有目录> ^
#     -DXPLATFORM_FBDEV_ON=ON -DXPLATFORM_DRM_ON=ON -DXPLATFORM_FBINPUT_ON=ON ^
#     -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=ON -DCMAKE_BUILD_TYPE=Release
#
# 行为说明：
# - CMAKE_SYSTEM_NAME=Linux + CMAKE_SYSTEM_PROCESSOR=arm：驱动 Posix 平台层
#   （fbdev/DRM 直写 + evdev 输入）参与编译，X11/DBus 等宿主探测经交叉根
#   限定自动落空（XINYUE_C_HAS_X11 不定义，屏幕信息走直写驱动侧实现）；
# - CPU 默认按 RK3506/全志 A33 一族 Cortex-A7（32 位 ARMv7-A，NEON-VFPv4，
#   hard float ABI=armhf，对位 glibc gnueabihf 根文件系统）；换 SoC 重设
#   XARM_LINUX_CPU/XARM_LINUX_FPU 即可；
# - 工具链定位：先 -DXARM_LINUX_GNUEABIHF_ROOT 显式指定，再 PATH，再常见
#   下载解压位置（Arm GNU Toolchain AArch32 GNU/Linux target，
#   arm-none-linux-gnueabihf-*）。

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# ---- 目标 SoC（-D 覆盖缓存变量即可换芯片族） ----
set(XARM_LINUX_CPU       "cortex-a7"    CACHE STRING "ARM CPU（-mcpu 值）")
set(XARM_LINUX_FPU       "neon-vfpv4"   CACHE STRING "FPU（-mfpu 值）")
set(XARM_LINUX_FLOAT_ABI "hard"         CACHE STRING "浮点 ABI（hard/softfp/soft）")

# ---- 编译器定位：先显式根，再 PATH，再常见解压位置 ----
set(XARM_LINUX_GNUEABIHF_ROOT ""
    CACHE PATH "arm-none-linux-gnueabihf 工具链根（bin/ 的上级目录）")
find_program(XARM_LINUX_GCC NAMES arm-none-linux-gnueabihf-gcc
                               arm-linux-gnueabihf-gcc
    PATHS "${XARM_LINUX_GNUEABIHF_ROOT}"
          "$ENV{USERPROFILE}/.eide/tools/arm-gnueabihf"
    PATH_SUFFIXES bin
    DOC "arm-none-linux-gnueabihf-gcc 可执行文件")
if(NOT XARM_LINUX_GCC)
    message(FATAL_ERROR "未找到 arm-linux-gnueabihf-gcc：请安装发行版交叉工具链"
        "（如 WSL/Ubuntu: sudo apt install gcc-arm-linux-gnueabihf）或下载 "
        "Arm GNU Toolchain（AArch32 GNU/Linux target）并 "
        "-DXARM_LINUX_GNUEABIHF_ROOT=<解压根> 显式指定")
endif()
set(CMAKE_C_COMPILER "${XARM_LINUX_GCC}")
# ar/ranlib 不手工指定：CMake 依编译器前缀自动探测（Windows 宿主带 .exe、
# Linux 宿主无后缀，自动探测两侧都成立）。

set(CMAKE_C_FLAGS_INIT
    "-mcpu=${XARM_LINUX_CPU} -mfpu=${XARM_LINUX_FPU} -mfloat-abi=${XARM_LINUX_FLOAT_ABI}")

# 库/头文件只在工具链目标根内找（libc sysroot 由编译器内建指定）
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
