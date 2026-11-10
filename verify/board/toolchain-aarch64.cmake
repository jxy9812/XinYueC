# verify/board/toolchain-aarch64.cmake —— RK3568 板（Debian 11 aarch64,
# glibc 2.31）交叉编译工具链，配套 verify/board/build_board.sh 使用。
#
# 前置（WSL Ubuntu 宿主）：
#   - /usr/bin/aarch64-linux-gnu-gcc（Ubuntu 交叉工具链）
#   - /root/board-sysroot（板端 sysroot，含 lib/ 与 usr/）
#   - /root/isoc23_shim.o（__isoc23_* 兜底垫片：Ubuntu 交叉 gcc15 按 C23 语义
#     把 scanf/strtol 家族重定向到 glibc 2.38+ 才有的 __isoc23_* 符号，目标板
#     glibc 2.31 没有；垫片用 __asm__ 别名强制转发到板子实名符号，绕过头文件
#     __REDIRECT——源文件与踩坑记录见 WSL /root/isoc23_shim.c）
#
# 踩坑记录（2026-10/11 逐项实测，v5 原版迁入，勿轻改）：
# 1) Ubuntu 交叉 gcc 内建搜索目录（/usr/aarch64-linux-gnu/...）是绝对路径，
#    --sysroot 不会重写，头文件和 libc.so 都会命中宿主 2.42 —— 必须用
#    -I 压头文件、-B 压库文件（-L 和 LIBRARY_PATH 都排不进内建目录之前）。
# 2) 板子镜像裁剪：libpthread.so 等是真实 ELF 而非 ld 脚本，其 DT_NEEDED
#    落在 sysroot 顶层 /lib，需显式 -L/-rpath-link 覆盖。
# 3) 板子 dev 软链多为绝对路径，入 sysroot 即断链，需修为相对（fixsysroot.sh）。
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)
set(ENV{PKG_CONFIG_LIBDIR} "/root/board-sysroot/usr/lib/aarch64-linux-gnu/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "/root/board-sysroot")
set(CMAKE_C_FLAGS_INIT "-B/root/board-sysroot/usr/lib/aarch64-linux-gnu/ -I/root/board-sysroot/usr/include --sysroot=/root/board-sysroot")
set(CMAKE_CXX_FLAGS_INIT "-B/root/board-sysroot/usr/lib/aarch64-linux-gnu/ -I/root/board-sysroot/usr/include --sysroot=/root/board-sysroot")
set(CMAKE_EXE_LINKER_FLAGS_INIT "/root/isoc23_shim.o -B/root/board-sysroot/usr/lib/aarch64-linux-gnu/ --sysroot=/root/board-sysroot -L/root/board-sysroot/usr/lib/aarch64-linux-gnu -L/root/board-sysroot/lib/aarch64-linux-gnu -L/root/board-sysroot/lib -Wl,-rpath-link,/root/board-sysroot/lib/aarch64-linux-gnu -Wl,-rpath-link,/root/board-sysroot/lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-B/root/board-sysroot/usr/lib/aarch64-linux-gnu/ --sysroot=/root/board-sysroot -L/root/board-sysroot/usr/lib/aarch64-linux-gnu -L/root/board-sysroot/lib/aarch64-linux-gnu")
set(CMAKE_SYSROOT /root/board-sysroot)
# glibc 2.34 才把 librt（shm_open/sem_open，XDeviceFile_posix_api.c、
# XMySqlSharedMemory.c 在用）与 libdl（dlopen/dlsym，XPlatformGraphics_egl.c、
# sqlite3.c 在用）并入 libc；板子 2.31 仍是独立库。CMake 生成的链接行里
# -lrt/-ldl 若只出现在静态库之前，group 之后的归档副本（如 Drive 库在
# --start-group 之后的第三次出现）新拉入的成员就解析不到——2026-11-10
# HsfServer 链接实测 undefined reference to `shm_open`。此变量落在链接行
# **最末尾**（CMake 默认链接规则的收尾段），迟拉入的成员也能解析；
# Ubuntu ld 默认 --as-needed，用不到时自动丢弃，零负担。
set(CMAKE_C_STANDARD_LIBRARIES "-lrt -ldl")
set(CMAKE_FIND_ROOT_PATH /root/board-sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
