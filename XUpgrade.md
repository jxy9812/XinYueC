# XUpgrade 跨平台升级模块设计

> 状态：设计评审稿（2026-10-06）
> 已定决策：升级范围=主程序+资源包（字库等），嵌入式含整镜像；通道=HTTP 为主（FTP 预留）；
> 嵌入式支持 bootloader A/B；三平台统一目录方案为基线；策略=强制/可跳过/静默后台全支持；灰度支持。

## 目录

1. [总体架构](#1-总体架构)
2. [安装根与目录布局（三平台统一）](#2-安装根与目录布局三平台统一)
3. [组件模型](#3-组件模型)
4. [manifest 格式与签名](#4-manifest-格式与签名)
5. [.pak 包格式](#5-pak-包格式)
6. [引擎状态机](#6-引擎状态机)
7. [存储后端契约（XUpgradeStore）](#7-存储后端契约xupgradestore)
8. [apply 桩与切换流程](#8-apply-桩与切换流程)
9. [嵌入式 A/B 与 bootloader 契约](#9-嵌入式-ab-与-bootloader-契约)
10. [策略引擎（强制/可跳过/静默）](#10-策略引擎强制可跳过静默)
11. [灰度发布](#11-灰度发布)
12. [UI 集成](#12-ui-集成)
13. [模块落位与配置裁剪](#13-模块落位与配置裁剪)
14. [公共 API 契约草案](#14-公共-api-契约草案)
15. [依赖复用与禁令](#15-依赖复用与禁令)
16. [崩溃安全不变式与断电测试矩阵](#16-崩溃安全不变式与断电测试矩阵)
17. [前置确认项](#17-前置确认项)
18. [分期计划](#18-分期计划)

---

## 1. 总体架构

自升级的根本矛盾：**运行中的程序无法可靠覆盖自身**（Windows 文件锁死；POSIX 换 exe 尚可，
但 .so/资源文件存在窗口期）。因此核心原则：

- **整版本目录落盘，激活=切指针**，绝不原地覆盖运行中的文件；
- **切换动作由运行中的自己之外的执行者完成**（桌面=apply 桩，嵌入式=bootloader）；
- **旧版本目录在确认成功前永不删除**，回滚=指针写回，天然免费。

```
┌──────────────────────────────────────────────────────────────────┐
│  UI（关于页/更新弹窗/强制升级遮罩/进度） ← 只做观察者与用户决策      │
├──────────────────────────────────────────────────────────────────┤
│  XUpgradeEngine（状态机，Src/XCode/XUpgrade）                     │
│    check → download → verify → stage → apply → confirm/rollback   │
│    ├ XUpgradeManifest（XJson 解析 + canonical 签名校验）           │
│    ├ XUpgradePackage（.pak 读写，zlib）                           │
│    ├ XUpgradeVerify（ECDSA P-256 + SHA-256，mbedtls）             │
│    └ XUpgradeGray（灰度命中判定）                                 │
├──────────────────────────────────────────────────────────────────┤
│  XUpgradeStore 契约（Src/XPlatform/XUpgrade）                     │
│    ├ XUpgradeStoreDir  目录后端：versions/<comp>/<ver>/ + 指针文件 │
│    │                   （Win32 / Linux / 带 SD 的嵌入式，三平台统一）│
│    └ XUpgradeStoreAb   A/B 后端：流式写非活动槽 + boot 标志        │
│                        （嵌入式整镜像，Drive/ 平台后端配合）        │
├──────────────────────────────────────────────────────────────────┤
│  复用底座：XHttp(XNetworkAccessManager) / XJson / zlib / mbedtls  │
│            XSaveFile(原子写) / XDir / XTask(后台) / XStateMachine  │
│            XProcess(拉起 apply 桩) / XSystem_reboot(Bootloader)   │
└──────────────────────────────────────────────────────────────────┘
```

## 2. 安装根与目录布局（三平台统一）

**安装根推导**：`XSystem_executableFilePath()` 取自身路径，向上取到约定层级
（桌面：exe 所在目录的上级；嵌入式：由 `XUpgradeConfig` 显式给定挂载点）。
禁止用工作目录/相对路径推导。

```
<root>/                                ← 安装根
├── versions/
│   ├── app/
│   │   ├── 1.2.2/                     ← 旧版本完整目录（回滚保底）
│   │   │   ├── bin/  res/  manifest.version
│   │   └── 1.2.3/                     ← staging 完成后整目录就绪
│   └── fontres/
│       └── 2.0/                       ← 字库资源（XCHAR_COMPACT.BIN 等）
├── pointers/
│   ├── app                            ← 单行文本 "1.2.3"（无扩展名）
│   └── fontres                        ← "2.0"
├── update/                            ← 引擎工作区（可整目录删除重来）
│   ├── partial/                       ← 下载中（.pak.part + .part.meta）
│   ├── state.json                     ← 状态机持久化（原子写+CRC）
│   ├── lock                           ← 引擎互斥锁（含持有者 PID，启动时清死锁）
│   └── update.log                     ← 升级专用日志（与主日志分流）
└── bin/ or 可执行入口                  ← 桌面 launcher（见 §8）
```

要点：

- **指针文件统一为文本**，不用 symlink（Fatfs 没有；Windows symlink 要特权）。
  写入一律走 `XSaveFile`（临时文件+原子替换），读失败/内容非法=视作未激活，
  走启动自检回滚路径。
- 版本目录名=组件版本串（`XUpgradeVersion_compare` 比较用，不参与路径拼接校验：
  版本串白名单 `[0-9A-Za-z._-]`，防路径逃逸）。
- `update/` 任意时刻可整目录删除，不损坏已安装版本。
- 旧版本目录保留策略：默认保留最近 2 个版本，`XUpgradeEngine_gc()` 在启动自检
  通过后延迟清理。

## 3. 组件模型

升级对象拆成**组件（component）**，每个组件独立版本线、独立指针、独立回滚：

| 组件 id   | kind        | 内容                              | 存储后端           |
|-----------|-------------|-----------------------------------|--------------------|
| `app`     | `dir`       | 主程序（bin、依赖、随版资源）      | DirStore（三平台） |
| `fontres` | `resources` | 字库等大资源（跨版本复用，独立演进）| DirStore（三平台） |
| `image`   | `image`     | 嵌入式整镜像（仅嵌入式目标）       | AbStore            |

- `fontres` 独立成线的原因：字库体积大、更新频率远低于程序，避免每版复制一份。
  组件间依赖用 manifest 的 `requires` 字段表达（如 `app 1.2.3` requires
  `fontres >= 2.0`），staging 完成时校验配套关系，不满足则整单不激活。
- `image` 是原子单位：一个镜像内已含 app+资源，**与 `app`/`fontres` 组件互斥**
  （镜像目标只走 A/B 线；目录目标只走文件线）。manifest 按目标 `os` 字段区分。

## 4. manifest 格式与签名

服务端只需**静态文件服务**（HTTP 目录 + manifest.json + 若干 .pak）。

### 4.1 manifest schema v1

```json
{
  "schema": 1,
  "channel": "stable",
  "version": "1.2.3",
  "build": "2026.10.06-01",
  "min_version": "1.0.0",
  "rollback_to": "1.2.2",
  "policy": "optional",
  "policy_forced_below": "1.0.5",
  "gray": { "percent": 10, "salt": "r1", "allow": ["SN001"], "deny": ["SN002"] },
  "components": [
    { "id": "app", "os": "win64",  "version": "1.2.3",
      "file": "app-1.2.3-win64.pak", "size": 18483211, "sha256": "…",
      "requires": { "fontres": ">=2.0" } },
    { "id": "app", "os": "linux64", "version": "1.2.3", "file": "…", … },
    { "id": "fontres", "os": "any", "version": "2.0", "file": "fontres-2.0.pak", … },
    { "id": "image", "os": "emb-arm", "version": "3.1.0",
      "file": "image-3.1.0-arm.img", "size": 8388608, "sha256": "…" }
  ],
  "sig": "base64( ECDSA-P256-SHA256( canonical(以上全部字段) ) )"
}
```

- `os` 目标标识由设备侧配置（`XUpgradeConfig.targetOs`），如
  `win64 / linux64 / emb-arm`；设备只取匹配自身 + `os=="any"` 的组件条目。
- `policy`：`forced | optional | silent`（见 §10）。
- `policy_forced_below`：低于此版本（或低于 `min_version`）无视组件策略，
  按 `forced` 处理——强制升级以版本为锚，不以单条 manifest 为锚。
- `rollback_to`：签名的回滚窗口。防降级守卫允许回退到的地板版本
  （用于"新版有问题批量回退"场景）；本地记录的"历史最高版本"低于该值时不再拦截。

### 4.2 签名与信任链

1. manifest 解析后按 **canonical 规则**重序列化：键名字典序递归排序、
   无空白、UTF-8、数字十进制无前导零；对该字节串做 SHA-256 后 ECDSA P-256 验签。
2. 公钥编译期注入 `Src/XCode/XUpgrade/XUpgrade_trust_pubkey.h`（发布流程换钥=重编译，
   或经 `XUpgrade_setTrustKey` 在产品初始化时注入，二选一，默认内嵌）。
3. **包完整性**：manifest 内每组件 `sha256`，下载/解包两次校验，分块校验断点续传。
4. **防降级**：`update/state.json` 持久记录每组件历史最高已激活版本
   `maxVersion[comp]`；待升级版本 < maxVersion 且 > `rollback_to` → 拒绝并记日志。
   状态文件被清（恢复出厂）视为全新设备重新信任当前运行版本为基线。
5. 通道安全：HTTPS 优先（XHttp 走 XSsl_platform→mbedtls）；**纯 HTTP 内网也成立**，
   因为信任锚是签名而非传输层——这是选 P-256 验签而非只靠 HTTPS 的原因。

## 5. .pak 包格式

不引入 tar/zip 依赖，自定义极简容器（嵌入端按条目流式解包直写 Fatfs）：

```
偏移   字段
0      magic[4]        "XUPK"
4      u32 format      =1
8      u32 entryCount
12     u32 headerSize  （头+条目表总长，数据区起点=headerSize）
16     u64 payloadSize （数据区总字节数）
24     u8  payloadSha256[32]  （数据区整段哈希）
56     条目表 entryCount × {
         u16 nameLen; u8 name[nameLen]     ← 相对路径，UTF-8
         u8  flags                          ← bit0=zlib 压缩
         u64 rawSize; u64 compSize
         u64 offset                         ← 相对数据区起点
         u8  sha256[32]                     ← 解压后内容哈希
       }
…      数据区
```

安全规则：

- 条目名禁止绝对路径、盘符、`..`、空段；解包目标必须落在版本目录内，越界即整包拒绝。
- 解压前后双哈希（条目 sha256 + 数据区 payloadSha256）。
- 顺序读一次性流式校验，嵌入端不需要随机访问。

## 6. 引擎状态机

用现有 `XStateMachine`（Src/XCode/XStateMachine）承载，状态可持久化到
`update/state.json`，任意一步断电/崩溃后重启收敛（不变式见 §16）。

```
Idle
 └→ Checking ──(网络/解析/签名失败)──→ CheckFailed（退避重试，周期可配）
      │(命中灰度且有新版)
      ↓
 Downloading ──(断点续传 Range; state.json 记 offset/分块哈希游标)
      ↓                          │(失败保留 partial,可续)
 Verifying(整包 sha256+验签) ──→ DownloadFailed
      ↓
 Staging(解包 .pak → versions/<comp>/<ver>/, 逐条目哈希)
      ↓(全部组件就绪+requires 配套校验通过)
 Ready(写 staged 标记进 state.json) ──(按策略触发)──→ ApplyPending
      ↓
 Applying(桌面: 退出→apply 桩切指针; 嵌入式: 置 boot 标志→reboot)
      ↓
 BootConfirming(启动自检: 版本+关键文件哈希)
      ├→ Active(确认成功; A/B: 回写 boot 成功标记; 触发 GC 旧版本)
      └→ RolledBack(自检失败: 指针/槽位写回旧版本, 记日志, 上报事件)
```

- 下载顺序按 manifest 组件依赖序（fontres 先于依赖它的 app 激活即可，下载可并行）。
- 全程经 `XTask` 后台执行；UI 通过 listener 观察事件（§12），引擎不依赖 UI。
- 引擎单实例：`update/lock` 文件锁（写 PID，启动发现陈旧锁即接管）。

## 7. 存储后端契约（XUpgradeStore）

按仓库驱动契约惯例（参照 XPlatform/XSql），契约在 `Src/XPlatform/XUpgrade`，
实现按构建目标静态选型。C 风格 vtable：

```c
/* XUpgradeStore.h —— 存储后端契约（契约头，实现在 XCode/Drive） */

typedef struct XUpgradeStore XUpgradeStore;

typedef struct XUpgradeStoreVtable {
    /** 开始为组件 staging 一个新版本。返回写入槽（DirStore=新版本目录；AbStore=非活动槽）。 */
    int (*stageBegin)(XUpgradeStore* self, const char* componentId,
                      const char* version, uint64_t expectedSize,
                      const char** errText);
    /** 流式写入一段（下载/解包共用此入口；内部可不做校验，校验在引擎层）。 */
    int (*stageWrite)(XUpgradeStore* self, const void* data, uint32_t len);
    /** staging 完成：持久落盘并做后端级校验（Dir=逐条目在引擎层完成后调用；Ab=整槽回读哈希可选）。 */
    int (*stageEnd)(XUpgradeStore* self, const char** errText);
    /** 原子激活。返回后新版本必须在下次启动生效（或由桩/bootloader 完成）。 */
    int (*activate)(XUpgradeStore* self, const char* componentId,
                    const char* version, const char** errText);
    /** 查询当前激活版本；无激活返回 false。 */
    bool (*currentVersion)(XUpgradeStore* self, const char* componentId,
                           char* out, uint32_t cap);
    /** 回滚到指定版本（自检失败路径；要求目标版本目录/槽位仍完好）。 */
    int (*rollback)(XUpgradeStore* self, const char* componentId,
                    const char* version, const char** errText);
    /** 枚举本后端保留的历史版本（GC 用）。 */
    int (*listVersions)(XUpgradeStore* self, const char* componentId,
                        int (*emit)(void* ud, const char* version), void* ud);
    void (*deinit)(XUpgradeStore* self);
} XUpgradeStoreVtable;

struct XUpgradeStore {
    XClass m_class;                 /* 基类必须第一位 */
    const XUpgradeStoreVtable* m_vtable;
};
```

- `XUpgradeStoreDir`（Src/XCode/XUpgrade）：纯库内实现（XSaveFile/XDir/XFile），
  三平台共用，无 Drive 代码。
- `XUpgradeStoreAb`（Src/XCode/XUpgrade）：流程编排在此；扇区级读写的平台原语
  （分区定位、块读写、元数据块擦写）通过 **Drive 注册回调**接入
  （模式同 `XSystem_setRebootHandler`，见 §9）。
- 两个实现都不得调用 `stdio.h`/`unlink` 等（风格指南禁令），一律 XFile 族。

## 8. apply 桩与切换流程

### 8.1 桌面（Win32 / Linux，DirStore）

主程序收到 `ApplyPending` 后按策略择机：

1. 保存用户现场 → 通知 listener → 优雅退出（exit code 约定 `74` = 有待应用升级）。
2. launcher/桩接管。**桩形态二选一（M1 先做 b，M2 评估 a）**：
   - a. 独立 stub：`bin/xupd`（Win32=`xupd.exe`），常驻安装根，几十 KB、极少变化；
     退出码 74 时由主程序 `XProcess::startDetached` 拉起。
   - b. 自重启参数：launcher 以 `--xupgrade-apply` 重启主程序自身，主程序
     main 入口最早处分流到 apply 分支（不初始化 GUI），完成后再启动正式版本。
3. apply 分支动作（顺序固定）：
   1. 读 `state.json` 确认 `Ready`；
   2. 逐组件 `activate()`：DirStore=原子写 `pointers/<comp>`（XSaveFile）；
   3. 清 `Ready` → `BootConfirming`；
   4. 启动正式程序，退出自身。
4. **Win32 退路**（备用不主用）：运行中的 exe 允许被改名，若未来做单文件热替
   可用 `app.exe → app.exe.old + 新文件 → app.exe` 的 rename 链；目录方案
   已覆盖资源/.so，此技巧仅作应急文档记录。

### 8.2 嵌入式（AbStore）

激活=置 boot 标志 + `XSystem_reboot(XSystemRebootMode_Bootloader/Normal)`，
无桩（见 §9）。

### 8.3 启动自检（三平台统一，引擎静态函数）

正式程序启动早期（GUI 之前）调用 `XUpgrade_selfCheck()`：

1. 解析 `pointers/<comp>` → 版本目录存在且 `manifest.version` 吻合；
2. 关键文件抽查哈希（清单随包 stage 时写入 `versions/<comp>/<ver>/.verify`，
   条目数可配，默认含主程序+字库索引）；
3. 通过 → A/B 场景回写 boot 成功标记（§9），否则记 `Active`；
4. 失败 → `rollback()` 写回上一版本 → 重启生效 → 事件上报 `RolledBack`。
   连续自检失败保护：同一版本回滚超过 2 次后停用自动升级并强报故障
   （防止旧版本已损坏的死循环）。

## 9. 嵌入式 A/B 与 bootloader 契约

`XSystemRebootMode_Bootloader` 原语已存在于 XSystem.h，本节约定 bootloader 侧
需要配合的**元数据块**与规则（需与 bootloader 团队评审签字）。

### 9.1 分区布局约定

```
flash: [ bootloader | 元数据块A | 元数据块B | slot_a(镜像) | slot_b(镜像) | 资源分区/数据分区 ]
```

- slot 大小、起始扇区由板级配置给出，经 Drive 回调注册给 AbStore；
- `image` 组件下载时**边下边流式写非活动槽**（不落临时文件，Fatfs 空间可能不足），
  收尾做整槽 SHA-256 回读校验（可配，慢但稳，默认开）。

### 9.2 元数据块（双份冗余，扇区对齐，写前擦除）

```c
typedef struct XUpgradeBootMeta {
    uint32_t magic;            /* "XUBM" */
    uint32_t crc32;            /* 本结构除 crc 外的 CRC32 */
    uint32_t activeSlot;       /* 0=A 1=B：上次确认成功的槽 */
    uint32_t requestedSlot;    /* 本次要求启动的槽 */
    uint32_t bootAttempts;     /* requestedSlot 的已尝试启动次数 */
    uint32_t maxAttempts;      /* 超过即由 bootloader 回切 activeSlot */
    uint8_t  imageSha256[32];  /* requestedSlot 镜像哈希，bootloader 可选校验 */
    uint32_t sequence;         /* 单调递增，双块取舍依据（大者且 CRC 合法者胜） */
} XUpgradeBootMeta;
```

### 9.3 启动规则（bootloader 实现）

1. 读 A/B 两块，取 `sequence` 大且 CRC 合法者；两块皆坏 → 启动 `activeSlot` 老槽
   （永不两个都动）。
2. `bootAttempts >= maxAttempts`（建议 3）→ 置 `requestedSlot = activeSlot`，
   `bootAttempts = 0`，写回，启动旧槽（**硬件级自动回滚**）。
3. 否则 `bootAttempts++` 写回，启动 `requestedSlot`。

### 9.4 应用侧（AbStore + 引擎）

- `activate()`：写完非活动槽校验通过后，以**双写+sequence 递增**方式更新元数据块
  （先写影子块再切，保证任意断电时刻至少一块合法），置
  `requestedSlot=新槽, bootAttempts=0` → `XSystem_reboot`。
- `XUpgrade_confirmBoot()`：自检通过后调用——`activeSlot=requestedSlot`，
  `bootAttempts=0`，sequence 递增双写。在此之前 bootloader 的计数回退机制兜底。
- 断电不变式：**任意时刻断电，设备要么启动旧槽，要么启动新槽并仍可回旧槽；
  不存在两槽同时不可用的状态**（因为从不覆写 activeSlot）。

## 10. 策略引擎（强制/可跳过/静默）

实际策略 = 组合判定（优先级从高到低）：

| 条件                                | 生效策略                          |
|-------------------------------------|-----------------------------------|
| 本地运行版本 < `min_version` 或 `policy_forced_below` | `forced`（无视组件策略与用户跳过）|
| 用户对 `(componentId, version)` 选过"跳过此版本"       | 不提示（仅此版本；`forced` 仍生效）|
| 其余                                 | manifest `policy`                 |
| 本地配置总开关关闭自动检查            | 仅手动检查可用                    |

三种策略行为：

- **forced（强制）**：UI 全屏遮罩（XGui 页），不可关闭，显示进度；下载+staging
  完成即自动进入 apply；宽限期 `forcedGraceHours`（manifest 可配，默认 0）内
  允许用户点"稍后"但每次启动重新提示。嵌入式无 UI 时等价于 silent+立即重启。
- **optional（可跳过）**：普通弹窗，`立即升级 / 稍后 / 跳过此版本`。
  跳过记录进 `state.json` 的 skip 表；下一版本重置。
- **silent（静默后台）**：仅通知一次（悬浮提示/通知栏），后台完成下载+staging，
  应用时机按本地配置 `applyWindow`：
  - 桌面：退出时应用（默认）或空闲 N 分钟后自动重启应用（可配）；
  - 嵌入式：**维护窗口**（如 `03:00-05:00`，XDateTime 判定），窗口内自动
    reboot 应用；错过窗口顺延，不7×24抢重启。

本地策略配置（用户可在设置页改）：`autoCheckOn / checkIntervalHours /
applyWindow / allowMeteredNetwork`（更新服务器地址与 channel 也在此，
可被产品初始化覆盖）。

## 11. 灰度发布

服务端保持纯静态文件，**灰度判定全部在端上执行 manifest 规则**：

```
deviceKey = XUpgradeIdentity 提供的序列号（缺省回退 MAC/安装时生成的 UUID）
hit = allow 列表命中 ? true
    : deny  列表命中 ? false
    : SHA256(salt | deviceKey) 前 4 字节（大端） mod 100 < percent
```

- 未命中灰度 → 引擎停在 `CheckFailed` 语义的"暂无更新"（本地记录本次判定，
  不重复请求，下次检查周期再判）；
- 多阶段放量：改 manifest 的 `percent` 重新签名上传即可，客户端下次检查自然生效；
- `channel`（stable/beta/…）：更新服务器 URL 模板含 `{channel}` 占位
  （`http://srv/{channel}/manifest.json`），本地配置选择；服务端即普通目录。
- 灰度命中哈希算法入契约（XUpgradeGray.c），保证跨版本升级后判定一致。

## 12. UI 集成

引擎与 UI 完全解耦：`XUpgradeEngine_setListener(callback, userData)` 推事件，
UI 侧（XGuiDemo 接入点）：

| 事件                  | UI 行为                                   |
|-----------------------|-------------------------------------------|
| `UpdateAvailable`     | 按策略弹窗/遮罩/静默提示                   |
| `Progress`（阶段+百分比） | 下载/校验/解包/写入槽 四段进度条         |
| `ReadyNeedRestart`    | optional/silent 的"重启以完成升级"提示     |
| `RolledBack`          | 故障提示（含回滚前后版本号）               |
| `CheckFailed`         | 静默记日志（手动检查时才提示用户）         |

设置页新增"关于 → 软件更新"：当前版本列表（各组件版本+更新时间）、手动检查、
策略配置、历史日志入口。嵌入式无 UI 构建下引擎照常运行（silent+维护窗口），
UI 层代码经裁剪宏隔离。

## 13. 模块落位与配置裁剪

```
Src/XPlatform/XUpgrade/            ← 契约（含 config，模式同 XProtocol/XSql）
├── XUpgrade_config.h
├── XUpgrade.h                     ← 引擎公共入口 + 策略/事件/错误枚举
├── XUpgradeManifest.h             ← manifest 结构与解析契约
├── XUpgradePackage.h              ← .pak 常量与读写契约
├── XUpgradeStore.h                ← 存储后端契约（§7）
└── XUpgradeIdentity.h             ← 设备标识/版本提供者回调

Src/XCode/XUpgrade/                ← 通用实现（平台无关）
├── XUpgradeEngine.c/.h            ← 状态机（复用 XStateMachine）
├── XUpgradeManifest.c             ← XJson 解析 + canonical 序列化
├── XUpgradePackage.c              ← .pak 读写（zlib）
├── XUpgradeVerify.c               ← ECDSA P-256 验签 + SHA-256（mbedtls）
├── XUpgradeGray.c                 ← 灰度命中
├── XUpgradeStoreDir.c/.h          ← 目录后端（三平台统一基线）
├── XUpgradeStoreAb.c/.h           ← A/B 后端编排
└── XUpgradeApplyStub.c            ← apply 分支入口（--xupgrade-apply）

Drive/<Platform>/                  ← 仅 A/B 需要的平台原语（回调注册，模式同 XSystem）
└── xupgrade_partition_<platform>  ← 分区定位/块读写/元数据块擦写/reboot 接线
```

`XUpgrade_config.h`（**宏一律带 `XUPGRADE_` 全前缀**，防 XMemory 战役里
`#if 短名宏静默=0` 的陷阱）：

```c
#define XUPGRADE_ON            1   /* 总开关（随 CXinYueConfig 汇总） */
#define XUPGRADE_HTTP_ON       1   /* XHttp 通道 */
#define XUPGRADE_FTP_ON        0   /* XFtp 通道（预留） */
#define XUPGRADE_AB_ON         0   /* A/B 后端（仅嵌入式目标编入） */
#define XUPGRADE_GRAY_ON       1
#define XUPGRADE_VERIFY_ON     1   /* 签名校验（仅内部调试可关，发布恒 1） */
#define XUPGRADE_DELTA_ON      0   /* delta 升级（预留，M4） */
#define XUPGRADE_UI_HINTS_ON   1   /* 无 UI 嵌入式构建置 0 */
```

## 14. 公共 API 契约草案

```c
/* XUpgrade.h —— 公共入口（契约级草案，签名以实现评审为准） */

typedef enum XUpgradePolicy {
    XUpgradePolicy_Forced = 0,     /* 强制：遮罩+自动重启 */
    XUpgradePolicy_Optional = 1,   /* 可跳过：弹窗三选 */
    XUpgradePolicy_Silent = 2      /* 静默后台+按窗口应用 */
} XUpgradePolicy;

typedef enum XUpgradeState {
    XUpgradeState_Idle = 0,
    XUpgradeState_Checking,
    XUpgradeState_Downloading,
    XUpgradeState_Verifying,
    XUpgradeState_Staging,
    XUpgradeState_Ready,           /* 待应用 */
    XUpgradeState_Applying,
    XUpgradeState_BootConfirming,
    XUpgradeState_Active,
    XUpgradeState_RolledBack,
    XUpgradeState_CheckFailed,
    XUpgradeState_DownloadFailed
} XUpgradeState;

typedef enum XUpgradePhase {          /* Progress 事件的阶段 */
    XUpgradePhase_Download = 0, XUpgradePhase_Verify,
    XUpgradePhase_Stage,             XUpgradePhase_Activate
} XUpgradePhase;

typedef struct XUpgradeEvent {
    XUpgradeState m_state;
    XUpgradePhase m_phase;
    int  m_percent;                /* 当前阶段 [0,100]，非进度态为 -1 */
    char m_component[32];
    char m_version[32];
    char m_detail[256];            /* 错误文本/回滚说明 */
} XUpgradeEvent;

typedef void (*XUpgradeListener)(void* userData, const XUpgradeEvent* event);

typedef struct XUpgradeConfig {
    const char* m_updateUrl;       /* 含 {channel} 占位模板 */
    const char* m_channel;
    const char* m_targetOs;        /* win64/linux64/emb-arm… */
    const char* m_installRoot;     /* 缺省由可执行路径推导 */
    uint32_t    m_checkIntervalHours;
    bool        m_autoCheck;
} XUpgradeConfig;

/* 身份/版本提供者（产品在初始化阶段注册，模式同 XSystem_setXxxHandler） */
typedef void (*XUpgradeVersionProvider)(void* ud, char* out, uint32_t cap);
typedef void (*XUpgradeSerialProvider)(void* ud, char* out, uint32_t cap);

void XUpgrade_setVersionProvider(XUpgradeVersionProvider handler, void* userData);
void XUpgrade_setSerialProvider(XUpgradeSerialProvider handler, void* userData);

bool XUpgrade_init(const XUpgradeConfig* config);   /* 幂等；启动引擎（XTask） */
void XUpgrade_deinit(void);
bool XUpgrade_checkNow(void);                        /* 手动检查 */
bool XUpgrade_startUpgrade(void);                    /* 用户确认后启动下载 */
void XUpgrade_skipVersion(const char* componentId, const char* version);
bool XUpgrade_applyNow(void);                        /* 绕过窗口立即应用（配合重启） */
bool XUpgrade_selfCheck(void);                       /* 启动早期调用（§8.3） */
bool XUpgrade_confirmBoot(void);                     /* A/B：自检通过回写（§9.4） */
void XUpgrade_setListener(XUpgradeListener listener, void* userData);
```

## 15. 依赖复用与禁令

| 需求         | 使用                         | 备注                                   |
|--------------|------------------------------|----------------------------------------|
| HTTP 下载    | XHttp `XNetworkAccessManager`| Range 续传需落地首日验证（§17）         |
| TLS          | XHttp → XSsl_platform → mbedtls | 纯 HTTP+签名亦成立（§4.2）           |
| JSON         | `XData/XJson`（现成）        | canonical 化由 XUpgradeManifest.c 实现 |
| 压缩         | Library/zlib                 | .pak 条目级 deflate                    |
| 签名/哈希    | Library/mbedtls              | ECDSA P-256 + SHA-256                  |
| 原子写       | `XSaveFile`（现成）          | 指针文件/state.json                    |
| 目录/文件    | XDir/XFile/XFileInfo         | 禁 stdio/remove，风格指南红线           |
| 后台执行     | XTask                        | 引擎全程后台                           |
| 状态机       | `XCode/XStateMachine`（现成）| 引擎状态迁移                            |
| 拉起桩       | XProcess                     | 桌面退出码 74 → detached 拉起           |
| 重启/进 bl   | `XSystem_reboot(Bootloader)` | 原语已存在                              |
| 时间/超时    | `XDateTime_currentMSecsSinceEpoch` | 单调钟，风格指南 2026-09-27 裁定  |
| 内存         | XMemory / XClass_Malloc      | 全模块走统一分配器                      |

新增外部依赖：**无**（bspatch 若 M4 做 delta 再按「第三方库集成规范」立项）。

## 16. 崩溃安全不变式与断电测试矩阵

不变式（实现评审逐条对账）：

1. `staged` 标记只在整包校验+解包+requires 校验全过后写入；未见标记=无事发生。
2. 指针/state.json/元数据块三种持久写全部原子（XSaveFile 临时替换 / 双块 sequence）。
3. 旧版本目录在 `BootConfirming` 通过前永不删除；A/B 从不覆写 activeSlot。
4. 自检失败必回滚；同一版本回滚计数超限停用自动升级并强报故障。
5. `update/` 任意时刻可整目录删除重建，不损坏已安装版本。
6. 引擎锁防双实例；陈旧锁（PID 不存在/失去心跳）启动时接管。

断电注入矩阵（每格=在持久点前后立即断电，重启后必须收敛到括号内终态）：

| # | 注入点                                   | 期望终态                         |
|---|------------------------------------------|----------------------------------|
| 1 | 下载中（任意进度）                        | partial 保留→续传（Idle 可续）   |
| 2 | 整包校验中                               | partial 作废→重下（Idle）        |
| 3 | 解包中（部分条目已写）                    | 版本目录半成品→重 stage（Idle）  |
| 4 | stage 完成写 staged 标记瞬间              | 标记在/不在均可（Ready 或 Idle） |
| 5 | 桩写指针前                                | Ready（下次启动桩续做）          |
| 6 | 指针写入原子替换瞬间                       | 新/旧指针均合法（BootConfirming）|
| 7 | 桩清 Ready 前                             | 重复 apply 幂等                  |
| 8 | A/B：流式写槽中                           | requested 未变→仍启旧槽          |
| 9 | A/B：元数据块单块写入中                    | 双块至少一块合法→按 §9.3 规则     |
| 10| A/B：新槽首次启动后 confirm 前             | boot 计数超限→bootloader 回旧槽  |
| 11| 自检失败回滚中                             | 指针/槽位最终指向完好旧版本      |

测试载体：桌面用 e2e 脚本（本地静态 HTTP 服务 + 断电注入桩，参照
`ftp_test_server.py`/`ftp_e2e_test.c` 模式）；嵌入式上电拉电工装跑 1–11 全矩阵。

## 17. 前置确认项

1. **XHttp Range 断点续传**：`XNetworkRequest` 头能力已具备，`XNetworkReply`
   是否暴露 206/Content-Range 语义需首日验证；缺则在 XHttp 补（改动收敛在其模块内）。
2. **设备序列号来源**：XSystem 目前无 SN 契约；M1 先用 `XUpgrade_setSerialProvider`
   由产品注册，是否下沉 XSystem 待产品线定。
3. **当前版本号注入方式**：桌面=编译定义（CMake 注入 `XUPGRADE_PRODUCT_VERSION`）；
   嵌入式=镜像头内嵌版本，自检时读镜像头比对。
4. **bootloader 配合项评审签字**：§9.1–9.3（元数据块布局、计数回退、双块规则）。
5. **Fatfs 长文件名**：确认 LFN 已启用（目录方案依赖）；8.3 兜底映射表可后补。
6. **Win32 安装位置**：默认用户可写目录（免 UAC）；若产品要求 Program Files，
   apply 桩需带提权清单（M2 评估）。
7. **更新服务器地址/证书分发**：产品配置项；纯 HTTP 场景安全锚=签名公钥，
   公钥轮换流程（双钥过渡期）需运维方案。

## 18. 分期计划

| 期  | 范围                                                                 |
|-----|----------------------------------------------------------------------|
| M1  | XUpgradeStoreDir + HTTP 整包 + 手动检查 + optional 策略 + 自检回滚；Win32 先通，Linux 跟上；断电矩阵 1–7 |
| M2  | P-256 验签 + 防降级 + forced/silent 策略 + apply 桩定型 + fontres 组件线 + 灰度；设置页 UI 接入 |
| M3  | XUpgradeStoreAb + Drive 分区后端 + bootloader 契约联调；断电矩阵 8–11 上电工装全绿 |
| M4（可选） | delta（bsdiff）+ 多渠道运维工具 + 公钥轮换方案               |
