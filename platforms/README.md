# platforms — 平台移植模板

本目录收纳**各平台的移植模板**：把 XGui/XinYueC 跑起来所需的平台差异层，
按平台一份，互不干扰。

## 边界：什么进这里，什么不进

这是本目录唯一需要记住的规则。

| 类别 | 归属 | 例子 |
|---|---|---|
| **平台差异层** | ✅ 进 `platforms/<平台>/` | 构建管线、烧录/打包脚本、链接脚本、OEM/裁剪配置、平台文档、外部依赖约定 |
| **框架平台层** | ❌ 留在 `Drive/<平台>/` | `Drive/Android/**`、`Drive/Posix/**`、`Drive/windows/**`、`Drive/Espressif/**` |
| **工程公共源码** | ❌ 留在 `Src/`、`Test/` | `Test/XGuiDemo/**`、`Src/XGui/**` |

理由：`Drive/**` 被根 `CMakeLists.txt` 的 `file(GLOB_RECURSE DRIVE_FILE "Drive/*.c")`
递归收进框架库 `XinYueCS/XD`，`Test/XGuiDemo/**` 被同一份配置编译成
`XGuiWindowDemo_Test` 与安卓的 `libxguidemo.so`。把它们复制进 `platforms/`
会立刻产生**两份会漂移的副本**——改一处忘另一处，症状是"这个平台好那个平台坏"
且极难定位。所以模板目录只装"这个平台独有、别处不存在"的东西。

ESP8266 是个例外，它的模板体量确实很大：它有自己的 `main()`、驱动、
裁剪档案和源清单，因为那块板子没有操作系统、没有 CMake 可用的交叉编译检测，
整套构建必须自持。这属于平台差异层，所以整体进来。

## 目录

```
platforms/
├── esp8266/                  ESP8266 NONOS3 固件模板（完整自持）
│   ├── build.py              一键构建/烧录（不需要 CMake）
│   ├── CMakeLists.txt        CMake 编排层（fw/flash/monitor 目标）
│   ├── CMakePresets.json     预设
│   ├── nonos/                固件模板本体
│   │   ├── main.c            固件入口
│   │   ├── app/              用户业务 + OLED 驱动
│   │   ├── drivers/          UART / OLED / 初始化
│   │   ├── platform/         WiFi 胶水 / lwipopts / 同步原语 / stub
│   │   ├── cfg/              裁剪档案 + 源清单
│   │   └── tools/            构建管线（bash + python）
│   ├── external/             外部依赖落点（工具链 / Arduino 包的约定位置）
│   └── docs/                 踩坑库 / 进度 / 交接
└── android/                  安卓打包层（薄）
    ├── CMakeLists.txt        CMake 编排层（lib/apk/install-apk 目标）
    ├── CMakePresets.json     预设（x86_64 / arm64-v8a）
    ├── build_apk.sh          APK 打包（bash，Linux/MSYS）
    ├── pack_apk.ps1          APK 打包（PowerShell，Windows 原生）
    ├── AndroidManifest.xml   NativeActivity 清单
    ├── res/                  资源
    └── README.md             详细文档（构建全流程 + 五个坑 + 字体路线）
```

## CMake 编排层：两个平台都有，形态一致

两个模板都提供 `CMakeLists.txt`，但**都不自己编译源码**，只做编排：

| 平台 | `project(...)` | CMake 做什么 | 真正的编译在哪 |
|---|---|---|---|
| ESP8266 | `project(... NONE)` | 调 bash 管线（`build_fw_final.sh`）→ 烧录 / 串口 | xtensa gcc，在 bash 脚本里 |
| Android | `project(... NONE)` | 配置并构建**根工程**的 `xguidemo` 目标 → 打包 → 安装 | NDK clang，走根 `CMakeLists.txt` |

这样做的原因：两个平台的编译都需要**跨平台工具链**，而根工程的 CMake 缓存是
桌面/MSVC 的，混进来会互相污染。编排层负责"用正确的工具链另起一个构建目录"。

```bat
:: ESP8266
cmake -S platforms/esp8266 -B build_esp
cmake --build build_esp --target fw

:: Android
cmake -S platforms/android -B build_android -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="<VS 自带 ninja 路径>"
cmake --build build_android --target apk
cmake --build build_android --target install-apk
```

也可以完全不用 CMake，直接跑脚本（两者等价）：

```bat
python platforms\esp8266\build.py
powershell -File platforms\android\pack_apk.ps1
```

## 各平台状态

| 平台 | 模板目录 | 状态 | 构建入口 |
|---|---|---|---|
| ESP8266 (NONOS3) | `platforms/esp8266/` | 真机验证通过（312 obj / 443KB 固件） | `python platforms/esp8266/build.py` |
| Android (NativeActivity) | `platforms/android/` | 模拟器验证通过（中文渲染正常） | `cmake --build build_android --target apk` |
| Windows | — | 主开发平台，直接用根 CMake | `Tools/windows/night_build_x64.bat` |
| Linux/POSIX | — | 主开发平台，直接用根 CMake | 常规 CMake |

## 新增一个平台模板

1. 建 `platforms/<平台>/`，只放该平台独有的构建/打包/配置/文档。
2. 平台 C 源码放 `Drive/<平台>/`（会被根 CMake 自动收进框架库）。
3. 若该平台需要**独立编译**（像 ESP8266 那样自持工具链），在模板里放自己的
   `CMakeLists.txt`，并确保它**不被根工程 `add_subdirectory`**——否则 VS 会出现
   两个同名 `.slnx` 且都点不动（见 `platforms/esp8266/docs/PITFALLS.md`）。
4. 路径推导一律从脚本自身位置往上推，**不要写死绝对路径**，层级数要和
   同目录其他脚本保持一致（多一层 `platforms/` 会让所有 `-I` 错位一档，
   症状是满屏"头文件缺失"）。
5. 在这份 README 的"各平台状态"表里加一行。

## 迁移历史

- **2026-10-24**：`esp8266/` → `platforms/esp8266/`；`Tools/esp` → `platforms/esp8266/external`；
  `Tools/android` → `platforms/android`。所有路径推导（bash 上溯级数、Python
  `Path.parent`、`.gitattributes`、CMakePresets `binaryDir`）已同步修正，
  ESP8266 真机链路复测通过。
