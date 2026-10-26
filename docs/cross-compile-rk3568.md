# RK3568 板（hd-rk3568）aarch64 交叉编译与部署指南

> 2026-10-26 首次打通。目标：在 Windows/WSL 上交叉编译 XinYueC 的 aarch64 产物并部署到
> RK3568 平板（192.168.0.10），替代板端原生编译（板端 4 核 A55 全量构建约 40 分钟，
> 交叉构建约 3 分钟）。本文记录完整的踩坑过程与可复制的命令序列。

## 0. 目标设备画像

| 项 | 值 |
| --- | --- |
| 主机名 / SoC | hd-rk3568 / RK3568（4×Cortex-A55） |
| 系统 | Debian 11.11（aarch64），glibc **2.31**，裁剪镜像（部分 -dev 包缺失） |
| 桌面 | Xfce + LightDM（DISPLAY=:0），触摸屏 ILITEK ILITEK-TP（DSI-1 1280x800）+ HDMI-1 1920x1080 |
| 板端工具链 | gcc 10.2 / cmake 3.18（可用但极慢） |
| 自启程序 | `run_test.service → /usr/bin/run.sh → /usr/bin/RK3568-IP-V2`（标题"万象奥科平板整机外设测试程序"） |
| 凭据 | root / root |

## 1. 总路线

```
Windows 仓库 (F:\Code\CMake\XinYueC)
  └─ tar 源码 → WSL Ubuntu 26.04 (/root/xinyuec-src)
       └─ aarch64-linux-gnu-gcc 15 + 板子 sysroot (/root/board-sysroot)
            └─ 产物 out-arm/XGuiWindowDemo_Test (glibc 上限 2.29)
                 └─ scp → 板子 /root/Desktop/XinYueC/bin/ → run_demo.sh
```

关键结论先行：**Ubuntu 26.04 的交叉 gcc15 自带 glibc 2.42，与板子 glibc 2.31 差距过大，
直接编译产物无法运行；必须用板子自身的库做 sysroot，且要绕过交叉 gcc 内建搜索目录的
多个陷阱**（见 §4）。

## 2. 环境准备（一次性）

```bash
# WSL2 + Ubuntu（需管理员，首次安装后要重启一次 Windows 完成组件启用）
wsl --install -d Ubuntu --no-launch      # UAC 提权后执行；重启后 ubuntu.exe install --root 注册发行版

# WSL 内（root）
apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu cmake make pkg-config
```

注意：仓库守卫**禁止交叉产物写入仓库 bin/**（CMakeLists.txt 配置期直接报错），
必须用 `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=` 指到私有目录。

## 3. 制作板子 sysroot（一次性，板子系统不变则无需重做）

在板子上打包三类内容（注意：**tar 创建归档不展开通配符**，必须让 shell 先展开，
即进入目录后用裸 glob，不要加引号）：

```bash
ssh root@192.168.0.10
# 1) 全量头文件
cd / && tar czf /tmp/sysroot-include.tgz usr/include
# 2) 运行库全量（含 .so.N 符号链接——缺失会导致链接器静默回落到宿主库！）
cd / && tar czf /tmp/sysroot-lib.tgz lib/aarch64-linux-gnu/lib*
# 3) usr/lib 下的 dev 软链与 .pc 文件（板子缺包时按需补）
cd /usr/lib/aarch64-linux-gnu && tar czf /tmp/sysroot-dev.tgz \
    libc* libm* libpthread* libdl* librt* libdbus* libfontconfig* libusb* \
    libsystemd* liblz4* libzstd* libexpat* libfreetype* libpng* libxml2* \
    libX11* libxcb* pkgconfig dbus-1.0 fontconfig
```

传到 WSL 解开并**修复断链**（板子的 dev 软链多为绝对路径 `/lib/...`，进 sysroot 必断）：

```bash
wsl -d Ubuntu -u root
mkdir -p /root/board-sysroot && tar xzf <各tgz> -C /root/board-sysroot
# 修复断链：目标在 sysroot 内的改为相对链接
find /root/board-sysroot -xtype l | while read -r l; do
    d=$(dirname "$l"); b=$(basename "$(readlink "$l")")
    [ -e "$d/$b" ] && ln -sf "$b" "$l" || \
    { [ -e "/root/board-sysroot/lib/aarch64-linux-gnu/$b" ] && \
      ln -sf "../../lib/aarch64-linux-gnu/$b" "$l"; }
done
```

## 4. 踩坑实录（问题 → 根因 → 解法）

### 坑①：aarch64 编译器不认 `-mfpu=neon`
- 现象：`cc: error: unrecognized command-line option '-mfpu=neon'`（板端与交叉同样报错）。
- 根因：CMakeLists 给 `XGuiRemoteCodec.c` 无条件追加 `-mfpu=neon`（ARM32 专用参数，
  NEON 在 aarch64 是标配且 gcc 直接拒绝该参数），条件里 `^(arm|aarch64)` 把 aarch64 也匹配了。
- 解法：条件改为 `MATCHES "^arm"`（已修在仓库 CMakeLists.txt，见 §7 未提交清单）。
  临时绕过：`-DXGUI_REMOTE_NEON=0`。

### 坑②：链接大面积 undefined reference（原子/时间/设备文件符号）
- 现象：`XAtomic_load_uintptr_t`、`XDateTime_currentMSecsSinceEpoch`、`XDeviceFile_remove` 等未定义。
- 根因：这些符号实现在 `Drive/`（`Drive/Compiler/gcc/XAtomic_GCC.c`、`Drive/Posix/**`），
  源码打包时漏传了 Drive 目录。
- 解法：补齐 `Drive/`（CMakeLists 会 GLOB `Drive/*.c`）。

### 坑③：产物要求 GLIBC_2.38+，板子 2.31 拒载
- 现象：`/lib/.../libc.so.6: version 'GLIBC_2.43' not found`。
- 根因：默认链接到 Ubuntu 交叉 glibc 2.42/2.43，符号版本被烘焙进产物。
- 解法：整条链路以板子 sysroot 为准（坑④⑤⑥⑦都是这一步的细化）。
- **验收红线**：`aarch64-linux-gnu-readelf --dyn-syms -W <产物> | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1`
  输出必须 ≤ 2.31（实测 2.29）。

### 坑④：`--sysroot` 管不住 Ubuntu 交叉 gcc 的内建搜索目录（本案最大坑）
- 现象：`-H` 显示 stdlib.h 来自 `/usr/aarch64-linux-gnu/include`（宿主 2.42 头），
  链接 `-Wl,-t` 显示 libc.so 来自 `/usr/aarch64-linux-gnu/lib`（宿主 2.42 库），
  产物出现 `__isoc23_*`、`atan2f@GLIBC_2.43`。
- 根因：Ubuntu 交叉工具链的内建搜索目录是**绝对路径**，`--sysroot` 只重写 `=` 前缀
  目录；用户 `-L`、`LIBRARY_PATH` 都排在内建目录之后。
- 解法：头文件用 `-I/root/board-sysroot/usr/include`（排在系统目录前），
  库用 **`-B/root/board-sysroot/usr/lib/aarch64-linux-gnu/`**（`-B` 是唯一能压过
  gcc 内建库目录的手段，实测有效）。

### 坑⑤：板子裁剪镜像的 libpthread.so 是真实 ELF 而非 ld 脚本
- 现象：`libdl.so.2: undefined reference to '_dl_sym@GLIBC_PRIVATE'`、
  `__libc_fcntl64@GLIBC_PRIVATE` 等一串私有符号未定义。
- 根因：正常发行版 `/usr/lib/libpthread.so` 是链接脚本（GROUP .so.0 + nonshared.a）；
  该板子镜像里它就是 ELF 本体，其 DT_NEEDED（`ld-linux-aarch64.so.1`、`libc.so.6`）
  位于 sysroot **顶层 `/lib`**，而交叉 ld 内建搜索目录不覆盖该处，回落到宿主 2.42
  ——2.42 已删除 `__libc_fcntl64` 等私有符号，于是雪崩。
- 解法：链接参数显式加
  `-L/root/board-sysroot/lib/aarch64-linux-gnu -L/root/board-sysroot/lib`
  与 `-Wl,-rpath-link,<同前两目录>`。
  **注意顺序**：usr/lib（有 .so 链接脚本）必须排在 /lib 之前，否则 `-lrt` 会先摸到
  `/lib` 里的静态 `librt.a`，报 `__shm_directory@@GLIBC_PRIVATE`。

### 坑⑥：板子 dev 软链是绝对路径，入 sysroot 即断链
- 现象：`cannot find /root/board-sysroot/usr/lib/aarch64-linux-gnu/libdbus-1.so`（文件明明在）。
- 根因：软链目标 `/lib/aarch64-linux-gnu/libdbus-1.so.3.19.17` 是绝对路径，指向 WSL 自己的根。
- 解法：见 §3 的断链修复脚本；板子缺 dev 包的（libusb-1.0.so、libsystemd.so）按
  运行库文件名手工补 `../../lib/aarch64-linux-gnu/<name>.<N>` 相对软链。

### 坑⑦：C23 的 `__isoc23_*` 符号
- 现象：`undefined reference to '__isoc23_strtol/__isoc23_sscanf/...'`。
- 根因：gcc15 默认 gnu23，glibc≥2.38 头文件把 strtol/sscanf 家族重定向到
  `__isoc23_*`（2.38 新符号），板子 2.31 头文件无此重定向、2.31 库无此符号。
- 解法：垫片对象 `/root/isoc23_shim.c`（以 sysroot 头编译，纯转发到实名函数），
  作为首个输入文件挂进 `CMAKE_EXE_LINKER_FLAGS_INIT`。语义差异仅 C23 新增的
  `0b` 二进制字面量前缀解析，demo 场景可忽略。

### 坑⑧：pkg-config 抓宿主路径
- 现象：`fatal error: dbus/dbus.h`（XPlatformNativeWindow_posix.c 的 fcitx DBus 前端
  无条件 include；CMakeLists 的 dbus 发现走 pkg-config）。
- 根因：交叉时宿主 pkg-config 返回 x86_64 路径；WSL 甚至没装 pkg-config 时
  `find_package(PkgConfig)` 直接跳过发现。
- 解法：装宿主 pkg-config，并在工具链文件里
  `set(ENV{PKG_CONFIG_LIBDIR} "<sysroot>/usr/lib/aarch64-linux-gnu/pkgconfig")`
  与 `set(ENV{PKG_CONFIG_SYSROOT_DIR} "<sysroot>")`，让所有 .pc 解析到目标侧。

### 坑⑨：新代码 POSIX 平台编译错误（与交叉无关，板端同样会挂）
- `XPlatformNativeWindow_posix.c:2661` `XAlignment_Center` 未声明：图标接线代码漏
  `#include "XAlignment.h"`（Win32 经其他头间接带入，POSIX 暴露）。已修在仓库。

### 附加：WSL 运维注意
- `wsl.exe` 退出会带走同会话 nohup 后台进程，WSL 内长任务要么前台同步跑，要么 `setsid`；
- WSL 的 `/tmp` 会在实例空闲回收后被清空，跨命令的中间文件放 `/root`；
- Git Bash 调 `wsl -- cp /root/...` 会被 MSYS 路径转换改写，用 `bash -c '...'` 包一层。

## 5. 工具链文件（/root/toolchain-aarch64.cmake，当前可用版本）

```cmake
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
set(CMAKE_FIND_ROOT_PATH /root/board-sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

## 6. 日常构建与部署

```bash
# —— WSL 内构建（源码更新后：先从 Windows 侧同步改动文件到 /root/xinyuec-src）——
cd /root/xinyuec-src && mkdir -p build-x && cd build-x
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_TOOLCHAIN_FILE=/root/toolchain-aarch64.cmake \
         -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/root/xinyuec-src/out-arm
make -j$(nproc) XGuiWindowDemo_Test        # 增量构建约 1~3 分钟

# 验收：GLIBC 上限必须 ≤ 2.31
aarch64-linux-gnu-readelf --dyn-syms -W ../out-arm/XGuiWindowDemo_Test \
  | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1

# —— 部署到板子 ——
scp ../out-arm/XGuiWindowDemo_Test root@192.168.0.10:/root/Desktop/XinYueC/bin/

# —— 板端运行（布局见下；参数=自动退出秒数，不传则常驻）——
ssh root@192.168.0.10 '/root/Desktop/XinYueC/run_demo.sh 6'
```

板子上的部署布局（**必须保持 bin + Library 兄弟关系**：demo 按
`<exeDir>/../Library/XFont/XFontOutlineCommon` 绝对路径探测字体；码表按进程 cwd
相对路径加载，故需 `cd bin` 再启动——run_demo.sh 已封装）：

```
/root/Desktop/XinYueC/
├── bin/XGuiWindowDemo_Test    # aarch64 产物
├── bin/*.TXT *.BIN            # XCHAR_COMPACT.BIN 等码表
├── Library/XFont/             # XFontOutlineCommon.otf/.ttc 等
├── Library/VirtualKeyboard/
├── run_demo.sh                # cd bin && export DISPLAY=:0 && exec ./XGuiWindowDemo_Test "$@"
└── build-info.txt
```

运行正常的判据（stderr 观测）：

```
[0.105] phase: app created
XGuiWindowDemo: switch page=0 (按钮演示)
[0.510] phase: DemoWin_create done
xpwn: XI2 touch enabled (v2.2)
```

`[ime-dbus] CreateInputContext 失败: ... Fcitx ...` 为无害告警（板上无 fcitx portal，
自动回退），不影响运行。

## 7. 遗留事项

1. **仓库未提交修改**（下次更新代码前先提交，否则会被覆盖）：
   - `CMakeLists.txt`：`XGUI_REMOTE_NEON` 条件 `^(arm|aarch64)` → `^arm`（坑①根修）；
   - `Drive/Posix/Graphics/XPlatformNativeWindow_posix.c`：补 `#include "XAlignment.h"`（坑⑨根修）。
2. 垫片方案的 C23 数值解析差异（`0b` 前缀）：demo 无影响；若后续板端升级 glibc ≥2.38 可删除垫片。
3. X11/OpenGL：sysroot 内未含 OpenGL，`find_package(OpenGL)` 不命中走软件路径；
   如需 GL 加速，需向 sysroot 补 Mesa arm64 dev 库。
4. 若板子系统升级（glibc 变化），需重做 §3 sysroot 并重验 §6 验收红线。

## 8. 快速排障速查

| 症状 | 指向 |
| --- | --- |
| `unrecognized command-line option '-mfpu=neon'` | 坑①（工具链是 aarch64，NEON 选项未关/未修） |
| `undefined reference to 'XAtomic_*' / 'XDateTime_*'` | 坑②（缺 Drive/ 源码） |
| `version 'GLIBC_2.xx' not found`（板端运行时） | 坑③④（产物链接到了宿主 glibc，重查工具链 -I/-B） |
| `undefined reference to '_dl_sym@GLIBC_PRIVATE'` 等私有符号 | 坑⑤⑥（-L 顺序 / sysroot 断链） |
| `undefined reference to '__isoc23_*'` | 坑⑦（垫片缺失或未挂链接行） |
| `fatal error: dbus/dbus.h` | 坑⑧（pkg-config 未指向板子 .pc） |
| 板端启动即退、无窗口 | 检查 DISPLAY=:0、bin/Library 布局、码表是否在 cwd |
