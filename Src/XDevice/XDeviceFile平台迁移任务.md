# XDeviceFile 平台化 + 移除 XFileSystem 任务交接文档

> 记录时间：2026-08-16
> 当前分支：develop
> 交接目的：供另一 AI/开发者接手，继续完成“XDeviceFile 各平台实现替换 XFileSystem 平台实现，最终删除 XFileSystem”。

---

## 1. 用户最终目标

1. 将 `Src/XDevice/XDeviceFile.c` 中依赖 `XFileSystem` 的虚函数实现下沉到各平台：
   - `Drive/Posix/File/XDeviceFile_posix.c`
   - `Drive/windows/File/XDeviceFile_win32.c`
2. 各平台实现直接用平台 API（POSIX / Win32），不再经过 `XFileSystem`。
3. 成功并测试通过后，**删除 `XFileSystem` 全部内容**：
   - 声明：`Src/XPlatform/XFileSystem.h`
   - 平台实现：`Drive/Posix/File/XFileSystem_posix.c`、`Drive/windows/File/XFileSystem_win32.c`
   - 占位实现：`Drive/XFileSystem_unsupported.c`
   - FatFs 适配：`Library/Fatfs/XFileSystem_Fatfs.c` 等（评估后处理）
   - 相关配置：`Src/XCode/XFile/XFileSystem_config.h`（其打开模式常量被 `XDeviceOpenOptions.m_openMode` 引用，需先解决依赖）
4. **必须保留所有原本功能**：`XFileSystem` 当前提供的核心文件操作、文件属性、文件系统操作、目录操作、路径操作、特殊路径、链接、权限、内存映射、文件时间、驱动器列表、存储信息、磁盘格式化等能力不能因删除而丢失，需由 XDevice 体系或迁移后的模块承接。

---

## 2. 重要提示（立刻要做的事）

- 用户明确要求“先写入文档，我要换个 AI 继续任务”。**当前交接文档就是第一步交付物。**
- 后续接手者先读本文件，再依照 `Src/XDevice/XDevice统一设备抽象设计.md` 了解 XDevice 设计。
- 用户的下一步意图很可能仍是“继续实现 XDeviceFile 平台化 + 删除 XFileSystem”，但**必须先把 XFileSystem 调用面摸清，再决定一次性还是分阶段迁移**（见第 6 节）。

---

## 3. 当前仓库状态（截至交接前）

### 3.1 已完成的代码改动（未提交、未推送，用户要求“不要自己推送”）

`XFileDescriptor` 字段改名/精简已完成：

```c
typedef struct XFileDescriptor {
    void*    m_deviceCtx;   /**< 统一设备打开上下文：XDevice 流程为 XDeviceContext*；旧子系统暂时为各自后端对象/原生句柄（借用） */
    void*    object;        /**< 所属 XObject* / 平台后端上下文（借用，可为 NULL）：IoRing 事件分发用 owner XObject*，共享内存等平台后端保存平台私有上下文（其首个成员为 XObject） */
    uint8_t  m_type;        /**< XFdType 枚举值 */
} XFileDescriptor;
```

同步改名的 API：
- `XFd_ctx()` → `XFd_object()`
- `XFd_setCtx()` → `XFd_setObject()`
- `XFd_alloc(type, handle, ctx)` → `XFd_alloc(type, handle, object)`（参数名同步）
- `XFd_handle()` 保留原名（兼容旧调用方），返回 `m_deviceCtx`

已更新的调用方（见 `git status` 修改列表）：
- `Drive/Posix/File/XFileSystem_posix.c`
- `Drive/Posix/XNetIoRingPosix.c`
- `Drive/windows/File/XFileSystem_win32.c`
- `Drive/windows/XNetIoRingWin32.c`
- `Drive/windows/XNetwork/XNetwork_win32.c`
- `Src/XCode/XConsoleShell/XConsoleShell.c`
- `Src/XCode/XEvent/XAbstractEventDispatcher.c`
- `Src/XCode/XEvent/XAbstractNetIoRing/XAbstractNetIoRing.c`
- `Src/XCode/XFile/XFile/XFile.h`
- `Src/XCode/XFile/XFileDevice/XFileDevice.c`
- `Src/XCode/XFileDescriptor/XFileDescriptor.c/h`
- `Test/XIOTest/XFileDescriptorTest.c`
- 设计文档 `Src/XDevice/XDevice统一设备抽象设计.md`

### 3.2 已验证结果

- 全量构建通过（VS2022 MSVC + Ninja，`XinYueC_Dynamic/Static.exe` 链接成功）。
- XDevice 文件设备冒烟测试通过：`open → write → flush → seek → read → queryProperty → close`，输出 `PASS`，退出码 0。
  - 冒烟程序：`C:\Users\jxy\.codex\visualizations\2026\08\16\01a00abc-73d6-7822-ad38-ee0455b69848\smoke.c`

### 3.3 新增/未跟踪文件（XDevice 模块）

- `Src/XDevice/XDevice.c/h`
- `Src/XDevice/XDeviceFile.c/h`
- `Src/XDevice/XDevice统一设备抽象设计.md`
- 本交接文档

---

## 4. XDeviceFile 现状（当前仍依赖 XFileSystem）

`Src/XDevice/XDeviceFile.c` 当前虚函数实现仍调用 `XFileSystem`：

| 虚函数 | 当前实现路径 | 目标 |
|---|---|---|
| `VXDeviceFile_open` | `XFileSystem_open` | 平台直接创建原生句柄 |
| `VXDeviceFile_close` | `XFileSystem_close` | 平台销毁原生句柄 |
| `VXDeviceFile_read` | `XFileSystem_read` | POSIX read / Windows ReadFile |
| `VXDeviceFile_write` | `XFileSystem_write` | POSIX write / Windows WriteFile |
| `VXDeviceFile_seek` | `XFileSystem_seek` | POSIX lseek / SetFilePointerEx |
| `VXDeviceFile_flush` | `XFileSystem_flush` | fsync / FlushFileBuffers |
| `VXDeviceFile_resize` | `XFileSystem_resize` | truncate / SetEndOfFile |
| `VXDeviceFile_getProperty` | `XFileSystem_fstat` 等 | 平台 stat 接口 |
| `VXDeviceFile_queryProperty` | `XFileSystem_fstat` 等 | 平台 stat 接口 |

### 4.1 XDeviceFile 打开选项结构

```c
typedef struct XDeviceOpenOptions {
    int m_openMode;     /* 打开模式位组合，见 XFileInfo 的 XFileSystem_* 打开模式。0 表示设备默认模式。 */
    uint32_t m_flags;   /* XDeviceOpenFlag 位组合。 */
    int64_t m_timeoutMs;/* 打开操作超时毫秒；0 表示设备默认或无限等待。 */
} XDeviceOpenOptions;

typedef struct XDeviceFileOpenOptions {
    XDeviceOpenOptions m_base; /**< 第一个成员，基础打开选项。 */
    const XString* m_path;     /**< 文件路径；借用指针，不能为 NULL。 */
    uint64_t m_bufferSize;     /**< 请求的读写缓冲字节数；0 表示设备默认。 */
} XDeviceFileOpenOptions;
```

- 打开选项**只在该次 Open 调用期间有效**，Open 成功后选项交由打开上下文（`XDeviceFileCtx`）持有记录。
- 用户前面强调过：`VXDeviceFile_open` 成功之后把 `opts` 交给 XDevice 管理；子类可按需继承/扩展 `XDeviceOpenOptions`。

---

## 5. XFileSystem 接口面（要保留的能力清单）

`Src/XPlatform/XFileSystem.h` 声明约 **33 个平台函数 + 3 个内联便捷函数**，类别如下：

### 一、核心文件操作（10 个）
- `XFileSystem_open`
- `XFileSystem_openStandardInput`
- `XFileSystem_readStandardInput`
- `XFileSystem_setStandardInputEcho`
- `XFileSystem_close`
- `XFileSystem_seek`（`pos` 是内联便捷函数）
- `XFileSystem_read`
- `XFileSystem_write`
- `XFileSystem_flush`
- `XFileSystem_resize`

### 二、文件属性操作（2 个 + 内联 exists）
- `XFileSystem_stat`
- `XFileSystem_fstat`
- `XFileSystem_exists`（内联）

### 三、文件系统操作（3 个）
- `XFileSystem_remove`（含 `removePermanent` 内联）
- `XFileSystem_rename`
- `XFileSystem_copy`

### 四、目录操作（5 个）
- `XFileSystem_mkdir`
- `XFileSystem_rmdir`
- `XFileSystem_opendir`
- `XFileSystem_readdir`
- `XFileSystem_closedir`

### 五、路径操作（1 个）
- `XFileSystem_resolvePath`

### 六、特殊路径（2 个）
- `XFileSystem_getSpecialPath`
- `XFileSystem_setCurrentPath`

### 七、链接操作（2 个）
- `XFileSystem_link`
- `XFileSystem_readLink`

### 八、权限操作（1 个）
- `XFileSystem_setPermissions`

### 九、内存映射 / 共享内存（3 个）
- `XFileSystem_openSharedMemory`
- `XFileSystem_map`
- `XFileSystem_unmap`

### 十、文件时间（1 个）
- `XFileSystem_setFileTime`

### 十一、驱动器列表（1 个）
- `XFileSystem_enumerateDrives`

### 十二、存储设备信息（1 个）
- `XFileSystem_getStorageInfo`

### 十三、磁盘格式化（1 个）
- `XFileSystem_format`

**结论：XFileSystem 不仅是“文件读写”，还包含目录、路径、链接、权限、共享内存、驱动器、存储信息、格式化等能力。删除它不能只替换 XDeviceFile，还必须处理全部下游模块。**

---

## 6. XFileSystem 调用面分析（关键风险）

删除 `XFileSystem.h` 会立刻造成大面积编译失败。主要调用方和约略调用次数：

| 调用方 | 约略次数 | 说明 |
|---|---|---|
| `Test/XCodeTest/XConsoleShellTest.c` | ~117 | 测试用例 |
| `Src/XCode/XConsoleShell/XConsoleShellFileSystem.c` | ~114 | ConsoleShell 文件系统接入 |
| `Drive/windows/File/XFileSystem_win32.c` | ~72 | 平台实现（自用的内部调用） |
| `Drive/Posix/File/XFileSystem_posix.c` | ~60 | 平台实现（自用） |
| `Library/Fatfs/XFileSystem_Fatfs.c` | ~47 | FatFs 适配 |
| `Src/XCode/XFile/XDir/XDir.c` | ~24 | 目录模块 |
| `Library/sqlite/sqlite3_xin_vfs.c` | ~22 | sqlite VFS |
| `Src/XCode/XSql/XMySqlSharedMemory.c` | ~20 | 共享内存 |
| `Test/XIOTest/XFileTest.c` | ~20 | 文件测试 |
| `Src/XProtocol/XSsh/XSshServer.c` | ~18 | SSH |
| 其它：XSaveFile、XFile、XStorageInfo、XProcess、XConsoleShell 等 | 数十处 | 分散 |

**直接删除 XFileSystem 不可行，除非：**
1. 所有下游模块全部迁移到 XDevice 统一接口，或
2. 以兼容层/移植文件先承接同名函数，后续再逐步收敛。

---

## 7. 建议推进方案（必须先给用户确认或按此分阶段自主推进）

### 方案 A（推荐，风险最低）：分阶段

**阶段 1：XDeviceFile 独立平台化（本任务的核心交付）**
- 新建 `Drive/Posix/File/XDeviceFile_posix.c`，实现 XDeviceFile 全部虚函数（open/close/read/write/seek/flush/resize/getProperty/queryProperty）。
- 新建 `Drive/windows/File/XDeviceFile_win32.c`，同左。
- 取消 `Src/XDevice/XDeviceFile.c` 中直接依赖 `XFileSystem` 的实现，改为调用平台实现的实现层接口（定义在 `XDeviceFile.h` 或平台专属声明头中，采用“不同平台用不同平台文件”的方式接入 CMake）。
- 修改构建脚本，按平台编译对应的 `XDeviceFile_*.c`。
- 保留 `XFileSystem` 暂不删除，保证其余模块仍能编译。
- 冒烟 + 全量构建通过后，把 XDeviceFile 平台化部分交付。

**阶段 2：评估其余模块迁移**
- XDir、XFile、XFileInfo、XSaveFile、XStorageInfo、XProcess、XSql、XSsh、XConsoleShell、FatFs、sqlite VFS 逐模块替换。
- 待全部替换完，再删除 `XFileSystem` 声明和平台实现文件。

### 方案 B（一次性，当前不推荐）
- 一次性迁移全部调用方 + 删除 XFileSystem。
- 工作量极大（20+ 模块、数百处调用），单 AI 单轮很难完成且回归风险高，容易丢失“原本功能”。

### 对“保留所有原本功能”的落实
- 迁移每个模块时，必须产出等价行为测试（或复用现有 `XFileTest` / `XConsoleShellTest` 用例）。
- XFileSystem 平台实现中的平台错误码转换、打开模式映射、文本/二进制区分等细节要在 XDeviceFile 平台实现中保留。

---

## 8. 平台实现参考（供接手者直接用）

### POSIX（`Drive/Posix/File/XDeviceFile_posix.c` 参考素材）
- `open(path, flags, mode)` / `close` / `read` / `write` / `lseek` / `fsync` / `ftruncate` / `fstat`
- 打开模式映射（从 `XFileSystem_config.h` 的 `XFileSystem_ReadOnly/WriteOnly/ReadWrite/Append/Truncate/Create/Exclusive` 等映射到 `O_RDONLY/O_WRONLY/O_RDWR/O_APPEND/O_TRUNC/O_CREAT/O_EXCL`）
- 属性查询：`fstat` 的 `st_size/st_mode/st_mtime/st_atime/st_ctime`
- 参考现有 `Drive/Posix/File/XFileSystem_posix.c`

### Windows（`Drive/windows/File/XDeviceFile_win32.c` 参考素材）
- `CreateFileW` / `ReadFile` / `WriteFile` / `SetFilePointerEx` / `FlushFileBuffers` / `SetEndOfFile` / `GetFileSizeEx` / `DeleteFileW` / `MoveFileExW` 等
- 路径为 UTF-16（`XString` 内部为 UTF-16 代码单元）；注意 `\\?\` 长路径前缀逻辑，参考现有 `XFileSystem_win32.c`
- 打开模式映射到 `GENERIC_READ/GENERIC_WRITE`、`OPEN_EXISTING/CREATE_ALWAYS/CREATE_NEW/TRUNCATE_EXISTING`、`FILE_APPEND_DATA`、共享/安全标志
- 参考现有 `Drive/windows/File/XFileSystem_win32.c`

---

## 9. 删除 XFileSystem 的完整清单（仅在阶段 1 完成后执行）

1. 处理 `XFileSystem_config.h`：
   - 打开模式常量（`XFileSystem_ReadOnly` 等）被 `XDeviceOpenOptions.m_openMode` 与大量模块引用，先确认迁移后由哪个头文件提供，例如迁入 `XDevice.h` 或用 XFileInfo 常量。
2. 移除 `Src/XDevice/XDeviceFile.c` 中对 XFileSystem 的 include 与调用。
3. 移除调用方对 `XFileSystem.h` 的 include 并替换为新接口：
   - `Src/XCode/XConsoleShell/...`
   - `Src/XCode/XFile/XFile/XFile.h`、`XFileDevice.c`、`XDir.c`
   - `Src/XCode/XSql/XMySqlSharedMemory.c`
   - `Src/XProtocol/XSsh/XSshServer.c`
   - `Library/sqlite/sqlite3_xin_vfs.c`
   - `Test/XIOTest/XFileTest.c`、`Test/XCodeTest/XConsoleShellTest.c`
   - 其它经 `git grep XFileSystem` 查到的位置
4. 删除平台实现：
   - `Drive/Posix/File/XFileSystem_posix.c`
   - `Drive/windows/File/XFileSystem_win32.c`
   - `Drive/XFileSystem_unsupported.c`（若存在）
   - `Library/Fatfs/XFileSystem_Fatfs.c` 及相关 FatFs 适配
5. 删除 `Src/XPlatform/XFileSystem.h`。
6. 更新 CMakeLists：移除已删除源文件，按平台加入 `XDeviceFile_posix.c` / `XDeviceFile_win32.c`。
7. 全量构建 + 运行 XDeviceFile 冒烟测试 + 相关既有测试（XFileTest、XConsoleShellTest 等）。

---

## 10. 构建与测试命令

### 全量构建（PowerShell + MSVC）
```powershell
& C:\WINDOWS\system32\cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build D:\code\CMake\XinYueC\out\build\x64-Debug --config Debug 2>&1'
```

### XDeviceFile 冒烟测试（编译/链接）
```powershell
# 需先进入 VsDevCmd 环境
cl @compile.rsp
link /nologo /OUT:smoke.exe smoke.obj `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug\Library\Fatfs `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug\Library\lwip `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug\Library\mbedtls `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug\Library\pcre2 `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug\Library\sqlite `
  /LIBPATH:D:\code\CMake\XinYueC\out\build\x64-Debug\Library\zlib `
  XinYueCSd.lib mbedtlsd.lib lwipd.lib zlibd.lib fatfsd.lib pcre2d.lib sqlited.lib `
  advapi32.lib user32.lib bcrypt.lib ws2_32.lib
smoke.exe
```

---

## 11. 用户硬性约束（接手者必读）

1. 源码文件必须 **UTF-8 带 BOM + CRLF**。
2. 工程是 **C99**，禁止 C11 匿名联合/匿名结构。
3. 头文件注释必须详细；参数注释全中文。
4. 命名风格：
   - 类型：`X + 大驼峰`
   - 成员：`m_ + 小驼峰`
   - 内部虚函数前：`V + 类名`（例如 `VXDeviceFile_open`）
5. 完整约束以 `D:\code\CMake\XinYueC\代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md` 为准。
6. **不要自己 push**（用户明确要求“不要自己推送”）；commit 除非用户要求，否则保持现有未提交状态。
7. Microsoft 构建：PowerShell 中必须先用 VsDevCmd.bat 激活 MSVC 环境，否则出现 C1083。

---

## 12. 下一步建议（接手者第一步）

1. 先提交/保留本交接文档。
2. 读 `Src/XDevice/XDevice统一设备抽象设计.md` 和 `Src/XDevice/XDeviceFile.c`。
3. 跑一次全量构建确认当前基线通过。
4. 向用户确认采用“方案 A 分阶段”（先 XDeviceFile 平台化、保留 XFileSystem），再开始实现 `Drive/Posix/File/XDeviceFile_posix.c` 与 `Drive/windows/File/XDeviceFile_win32.c`。
5. 同时开始写 `XDeviceFile_posix.c` / `XDeviceFile_win32.c` 的代码（直接平台 API），用新冒烟程序回归所有文件操作。
