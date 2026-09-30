# Tools 工具目录

开发、构建、诊断辅助工具的集中地。**按模块分子目录**，模块相关的脚本、
依赖、产物就近放在一起；根目录只留高频构建入口和跨模块的基础设施。
新工具请放进对应模块子目录（或新建一个模块目录），不要散落在根目录。

## 目录总览

```
Tools/
├── README.md                本文件
├── windows/                 Windows 平台入口与专属工具
│   ├── night_build_x64.bat  构建入口：x64-Debug 全树（VS18 环境 + ninja）
│   ├── night_build_x86.bat  构建入口：x86-Debug 全树
│   ├── git.bat              git 包装器（本机 git 不在 PATH，用 VS 自带）
│   ├── vulkan/              Vulkan 无 SDK 方案全套（见下）
│   └── diag/                帧诊断/基准（PowerShell + Win32，Windows 专属）
├── posix/                   POSIX/Linux 平台产物
│   └── xgui.lsan.supp       LeakSanitizer 抑制列表（Linux ASan）
├── codegen/                 源码生成模块
├── analysis/                静态分析模块
├── font/                    字体管线模块
└── ime/                     拼音词库管线模块
```

**平台约定**：平台专属的入口、依赖和工具按平台归目录——`windows/`
收 .bat 和仅 Windows 消费的资产（vulkan 全套、帧诊断三件套），
`posix/` 收 Linux 侧脚本与产物。跨平台的模块工具
（codegen/analysis/font）按模块归目录。

**Vulkan 的 Linux 侧说明**：Linux 无需任何预置资产——没有导入库概念，
装发行版包（如 `libvulkan-dev`）即得头文件与 libvulkan.so，
CMakeLists 的 Linux 分支 `find_library(NAMES vulkan)` 直接拾取。
若未来某 Linux 环境确实需要 vendored 方案，届时再在 posix/ 下生成。

## 构建入口（windows/）

两棵构建树都在 `out/build/` 下，输出共用 `bin\`，**不可并行构建**，
切架构即跑对应脚本；退出码即构建结果（不要在复合命令行里读
`%ERRORLEVEL%`，整行解析时会拿到旧值）。

| 脚本 | 说明 |
|---|---|
| `windows/night_build_x64.bat [target]` | x64-Debug 全树（无参=全部），vcvars64 + ninja |
| `windows/night_build_x86.bat [target]` | x86-Debug 全树，vcvarsall x86 |
| `windows/git.bat <args>` | git 转发到 VS18 自带的 git.exe |

## windows/vulkan/ — Vulkan 模块（无 SDK 方案，Windows 专属）

本机无 Vulkan SDK，通过「系统 loader DLL 导出 → 自建导入库」的方案
启用 Vulkan 后端。模块内三部分：

| 路径 | 内容 | git |
|---|---|---|
| `importlib/x64-msvc/vulkan-1.lib` | x64 导入库（System32 64 位 loader 导出） | **入库** |
| `importlib/x86-msvc/vulkan-1.lib` | x86 导入库（含 stdcall 跳板层） | **入库** |
| `headers/Vulkan-Headers-1.3.290/` | vendored 官方头文件（含下载缓存 zip） | 忽略 |
| `make_vulkan_lib.bat` | 生成脚本（从系统 DLL 一键重造两份库） | 入库 |
| `gen_vulkan_x86_def.ps1` | x86 stdcall 跳板名生成（被上者调用） | 入库 |

- **使用**：什么都不用做。CMakeLists 按 `CMAKE_SIZEOF_VOID_P` 自动选
  导入库；两份 .lib 随仓库分发，同平台（Windows + MSVC）设备拉代码即编。
- **x86 库的坑**（为什么它不是纯 def 生成）：32 位下 vulkan.h 把 vk
  函数声明为 `__stdcall`，链接器要 `_vkCreateInstance@12` 带装饰符号；
  `lib /def` 的别名语法无法解耦符号名与 DLL 导出名，故用 MASM 跳板
  `_vkX@N: jmp _vkX`（尾跳 ABI 正确）合并进裸名导入库。装饰名不手工
  换算——由 x86 编译器编译引用文件后从 obj 符号表读出。
- **重造**：换新机器或怀疑库文件损坏时跑 `make_vulkan_lib.bat`；
  中间产物落在 `importlib/` 根下（已忽略），清掉即可。
- **前提**：脚本里的 MSVC 路径钉在 VS18（14.51.36231）；升 VS 后需同步
  `make_vulkan_lib.bat`、`gen_vulkan_x86_def.ps1`、
  `windows/night_build_*.bat` 三处路径。

## codegen/ — 源码生成模块

| 脚本 | 说明 |
|---|---|
| `gen_spv.py` | 生成 Vulkan 驱动的三个 SPIR-V shader（C 数组头文件，手写 SPIR-V 二进制），产物 `Src/XGui/Graphics/XGpuRenderDriver_vulkan_shaders.h` |
| `test_gen_spv.py` | 上者的回归测试（重新生成并逐字节比对） |

改 shader 后：`python Tools/codegen/test_gen_spv.py`，EXIT=0 即产物一致。

## windows/diag/ — 帧诊断与基准（Windows 专属）

| 脚本 | 说明 |
|---|---|
| `night3_bench.ps1` | 第三夜基准 harness（FPS 矩阵/闪烁/空闲/撕裂多协议，参数见脚本头注释） |
| `capture_frames.ps1` | PrintWindow 连拍指定进程窗口 |
| `diff_frames.ps1` | 帧间像素差分，量化闪烁/残影 |

`night3_bench.ps1` 内部按脚本名调用 capture/diff 两件套（`$ToolsDir`
默认指向本目录）；输出目录（caps、bench-*-logs 等）按 .gitignore 约定
落在 Tools/ 根下、不入库。基准测量纪律：性能对比必须同负载背靠背连跑，
后台编译/工作流并发会污染 FPS。

## analysis/ — 静态分析与验收模块

| 脚本 | 说明 |
|---|---|
| `xgui_api_scan.py` | XGui → Qt 6.8.3 全模块 API 复扫（Phase 3.1 v2），对齐审计 |
| `scan_widget_signals.py` | 扫 Qt widgets 头文件的 Q_SIGNALS，与 XGui 信号宏对照出缺口清单（Qt 源码路径硬编码在脚本里，按需改） |
| `init_field_check.py` | 控件 init 必须「初始化全部结构体字段」的静态检查（源于 XLineEdit 野指针事故） |
| `final_gate_release.sh` | Release/-O2 终门：Debug 全绿 ≠ Release 健康的教训产物（Linux/bash） |

## font/ — 字体管线模块

| 脚本 | 说明 |
|---|---|
| `xfont_compile.py` | TTF/OTF → XFO1 紧凑轮廓格式（离线 fontTools，运行时无 TTF 解析器） |
| `xfont_merge_gb2312.py` | 向内置 CJK 字体增量补 GB2312 汉字（6763 字，不重生成已有字形） |
| `xfont_pack_outline_common.py` | 把内置 Latin+Cjk 两个分体 XFO1 合并为外挂加载单文件 |

三脚本同目录互相 import，依赖 `fontTools`（`pip install fonttools`）。
**用户在途工作**，用法见各脚本头注释。

## ime/ — 拼音词库管线

| 脚本 | 说明 |
|---|---|
| `ime_phrases_compile.py` | IME 词组 txt → XIPB 二进制（运行时零解码快路径；音节白名单从 XPinyinTable.c 提取，严格模式坏行即失败，CRC-32/ISO-HDLC + 音节指纹 + --verify 自检） |
| `test_ime_phrases_compile.py` | 上者的回归测试（重新生成 `Library/VirtualKeyboard/phrases_zh.bin` 与 `phrases_test_fixture.bin` 并逐字节比对，另校验 4 个坏件夹具；EXIT=0 即产物一致） |
| `style_check.py` | XVirtualKeyboard LVGL 风格自动化检查：静态断言（LVGL 9.2.2 规格现算对照）+ 无头截图像素采样 + 几何走查（被 xgui_demo_pages.h、xgui_demo_page_keyboard.c、xgui_window_demo.c 引用） |

产物在 `Library/VirtualKeyboard/`（bin 随库入库、不拷贝不入构建树）。
**改词库或改音节表后必须重编 bin**（音节表增删/重排会被载入指纹校验
拒绝——防线而非缺陷）：`python Tools/VirtualKeyboard/test_ime_phrases_compile.py`
，EXIT=0 即入库产物与源同步。

**资产变化的回归锚**（改词库/音节表后须全绿，见
`Library/VirtualKeyboard/README.md`）：

| 消费方 | 锚点 |
|---|---|
| `Test/XGuiTest/XKeyboardTest.c` | ⑥d 夹具解析、⑥e 黄金一致（txt/bin 双侧全双音节键扫描摘要 + 三音节组抽查表 328 条规模锁——**改词库须按新 phrases_zh.txt 重新机械提取同步**）、⑥ 系列状态机用例（412/2017 静态表规模锁） |
| `Test/XGuiDemo/xgui_demo_apitest_input.c` | §7 ImeTable/ImePhrase 资产锁（412/2017 编译期表 + 3143 运行期资产，isReady 守卫双分支） |
| `Test/XGuiDemo/xgui_demo_page_keyboard.c` | 阶段 6 词组演示（ready=词组上屏 +6 字节 / 缺资产=单字回退 +3 字节） |

## 其他

- 历史文档（XGui.md、docs/xgui/ 夜战报告）里引用的 `Tools/xxx` 旧路径
  多为记录性描述，以本文件当前布局为准。
- 已清理的目录（勿在历史路径上重建）：`night-backup/`（夜间快照与
  .local 变体，2026-09-27 经逐文件比对确认被工作区/树上版本全面取代
  后删除）、`diag/abtex/`（缺陷复现证据，复现方法见 XGui.md F 波节）。
