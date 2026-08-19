# XDevice 统一设备抽象设计（XDevice + XFd + XDeviceContext）

> 本文档与《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》保持一致：
> 枚举值、成员变量、类创建、虚函数重载、API 命名和头文件注释均按该指南执行。

## 1. 文档目的

本文档定义 XinYueC 的统一设备抽象：

- `XFd`：所有设备统一使用的句柄（保留现有 `XFd` 概念）；
- `XDevice`：统一设备基类 + 公共门面（`XDevice_open/read/write/close/...`），
  继承 `XClass`，同时承担“后端基类”的职责（早期 `XDeviceBackend` 已合并进 `XDevice`）；
- `XDeviceContext`：每次 `XDevice_open` 独立分配的打开上下文基类，虚函数统一收到
  `XDeviceContext*`，具体设备子类在首位扩展自己的私有状态；
- `XDeviceFile`：内置文件设备示例，继承 `XDevice`，复用 `XDeviceFile` 平台实现；
- `XFileDescriptor`：极简统一句柄表槽位，只保存 `m_deviceCtx/object/m_type`。

本设计放弃之前未提交的 `XDrive.h/XDrive.c` 方向，从上一次提交重新开始。
`XFileDescriptor/XFd` 保留为统一句柄表，继续由 `XFixedPool` O(1) 管理。

## 2. 命名约定

| 概念 | 命名 | 说明 |
| --- | --- | --- |
| 统一句柄 | `XFd` | 既有，所有设备统一使用 |
| 统一设备门面/基类 | `XDevice_*` / `XDevice` | 公共 API 前缀；类名即设备类别 |
| 打开上下文基类 | `XDeviceContext` | 每次 open 唯一分配，子类首位扩展 |
| 具体设备类 | `XDeviceFile`、`XDeviceSocket`、`XDeviceSerialPort`、`XDeviceTimer`、`XDeviceDir`、`XDeviceConsole`、`XDeviceGpio`... | 按设备命名 |
| 内部描述符表 | `XFileDescriptor` | 保留，内部实现，不对外 |
| 类内部虚函数实现 | `VXDeviceFile_*` | `V` + 类名 + `_` + 函数名 |

命名规则依据风格指南：

- 结构体/类：`X` 前缀 + 大驼峰；
- 枚举类型：`X` 前缀 + 大驼峰；
- 枚举值：`类型名_大驼峰`（例如 `XDeviceType_File`、`XDeviceProperty_OpenMode`）；
- 函数：`X类型名_功能名`（小写下划线风格）；
- 成员变量：`m_` 前缀 + 小驼峰。

## 3. 分层架构

```text
对象层：XFile / XSocket / XSerialPort / XGpio...           ← XObject 多态（已有）
        ↓
XDevice 统一基类 + 公共门面                                 ← 合并后的 XDevice（继承 XClass）
        ↓
XFileDescriptor（XFixedPool 槽位表）+ XFd                   ← 统一句柄表
        ↓
XDeviceFile / XDeviceSocket / XDeviceGpio...                ← 具体设备类（继承 XDevice）
        ↓
XDeviceContext（每次 open 分配，子类扩展）                   ← 运行期打开上下文
        ↓
Windows / POSIX / lwIP / FatFS / STM32...                   ← 平台实现
```

原则：

1. **所有设备统一用 `XFd` 句柄**，创建/销毁/属性/错误都走 `XDevice_*`。
2. **新增设备只写一个设备子类文件**，不改核心、不加 switch。
3. 字节流/生命周期/属性走统一内核；设备专有能力保留在各自专用头，通过
   `XDevice_control` 或专用 API 访问。
4. `XFileDescriptor` 是 `XFixedPool` 里的普通槽位，不引入事件循环、信号槽等重机制。
5. 运行期状态（状态、I/O 模式、在途异步操作、最近错误）不再放回 `XFileDescriptor`，
   统一放进 `XDeviceContext`。

## 4. 设备多态

`XDevice` 继承 `XClass`，类名即设备类别名（`"file"`、`"socket"`、`"gpio"`...），
通过既有 `XCLASS_SET_CLASS_NAME_DEFAULT` 设置，不单独存储 `className` 变量。

### 4.1 设备类虚函数表

```c
/* Src/XDevice/XDevice.h（节选） */
XCLASS_DEFINE_BEGING(XDevice)
XCLASS_DEFINE_ENUM(XDevice, Open) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XDevice, Close),
XCLASS_DEFINE_ENUM(XDevice, Read),
XCLASS_DEFINE_ENUM(XDevice, Write),
XCLASS_DEFINE_ENUM(XDevice, Seek),
XCLASS_DEFINE_ENUM(XDevice, Flush),
XCLASS_DEFINE_ENUM(XDevice, Resize),
XCLASS_DEFINE_ENUM(XDevice, SetProperty),
XCLASS_DEFINE_ENUM(XDevice, GetProperty),
XCLASS_DEFINE_ENUM(XDevice, QueryProperty),
XCLASS_DEFINE_ENUM(XDevice, Control),
XCLASS_DEFINE_END(XDevice)
```

所有虚函数的第一参都统一改为 `XDeviceContext* ctx`（Open 返回 `XDeviceContext*`），
具体设备子类通过“基类在首位”向下转型访问自己的扩展字段。

### 4.2 设备基类结构体

```c
typedef struct XDevice
{
    XClass m_class;                  /* 第一个成员，由 XClass 管理，禁止手工修改。 */
    XDeviceType m_type;              /* 设备类型；外部注册设备为 XDeviceType_Class。 */
    XDeviceOpenOptions* m_defaultOpenOptions; /* 默认打开选项（借用）；子类可扩展。 */
    uint32_t m_capabilities;         /* XDeviceCap 位集合。 */
    bool m_registered;               /* 是否已注册到 XDevice 注册表。 */
    uint32_t m_refCount;             /* 引用计数；open +1、close -1。 */
} XDevice;
```

### 4.3 打开上下文基类（位域压缩）

```c
typedef struct XDeviceContext
{
    XDevice* m_device;                  /* 所属设备类对象（借用）。 */
    uint16_t m_state      : 3;          /* XDeviceState 生命周期状态。 */
    uint16_t m_ioMode     : 1;          /* XDeviceIoMode I/O 模式。 */
    uint16_t m_pendingOps : 8;          /* 在途异步操作数。 */
    uint16_t m_reserved   : 4;          /* 预留位，必须为 0。 */
    int16_t  m_lastError;               /* 最近一次错误码（XDeviceError）。 */
} XDeviceContext;
```

具体设备子类（如 `XDeviceFileCtx`）必须把 `XDeviceContext m_base` 作为第一个成员，
再按需追加设备专有字段：

```c
typedef struct XDeviceFileCtx
{
    XDeviceContext m_base;      /* 第一个成员，打开上下文基类。 */
    XFd m_fileFd;               /* XDeviceFile 返回的文件描述符。 */
    int m_openMode;             /* 打开模式。 */
    uint32_t m_flags;           /* 打开标志。 */
    uint64_t m_bufferSize;      /* 读写缓冲字节数；0 表示设备默认。 */
} XDeviceFileCtx;
```

## 5. 公共 API 设计

```c
/* 统一生命周期 */
XFd  XDevice_open(XDeviceType type, const XDeviceOpenOptions* opts, int* err);
XFd  XDevice_openClass(const char* className, const XDeviceOpenOptions* opts, int* err);
void XDevice_close(XFd fd);

/* 通用 IO */
int64_t XDevice_read(XFd fd, void* buffer, int64_t size);
int64_t XDevice_write(XFd fd, const void* data, int64_t size);
int64_t XDevice_seek(XFd fd, int64_t offset, XDeviceSeekWhence whence);
bool    XDevice_flush(XFd fd);
bool    XDevice_resize(XFd fd, int64_t size);

/* 属性 */
bool XDevice_setProperty(XFd fd, XDeviceProperty property, const XVariant* value);
bool XDevice_getProperty(XFd fd, XDeviceProperty property, XVariant* value);
bool XDevice_queryProperty(XFd fd, XDeviceProperty property, XVariant* value);

/* 设备命令 */
bool XDevice_control(XFd fd, uint32_t command, const XVariant* in, XVariant* out);

/* 错误与设备对象 */
int        XDevice_lastError(XFd fd);
XDevice*   XDevice_class(XFd fd);
XDeviceContext* XDevice_handle(XFd fd);
```

### 5.1 打开选项

```c
typedef struct XDeviceOpenOptions
{
    int      m_openMode;    /* 打开模式位组合，见 XFileInfo 的 XDeviceFile_* 模式；0 表示设备默认。 */
    uint32_t m_flags;       /* XDeviceOpenFlag 位组合。 */
    int64_t  m_timeoutMs;   /* 打开超时（毫秒）；0 表示设备默认或无限等待。 */
} XDeviceOpenOptions;
```

子类可扩展打开选项，第一个成员必须是 `XDeviceOpenOptions m_base`：

```c
typedef struct XDeviceFileOpenOptions
{
    XDeviceOpenOptions m_base;  /* 第一个成员，基础打开选项。 */
    const XString* m_path;      /* 文件路径（借用）。 */
    uint64_t m_bufferSize;      /* 读写缓冲字节数；0 表示设备默认。 */
} XDeviceFileOpenOptions;
```

## 6. XFileDescriptor 内部结构（极简 + 位域）

```c
/* Src/XCode/XFileDescriptor/XFileDescriptor.h（已实现） */
typedef struct XFileDescriptor
{
    void*   m_deviceCtx; /* 统一设备打开上下文：XDevice 流程为 XDeviceContext*；旧子系统暂时为各自后端对象/原生句柄（借用）。 */
    void*   object;    /* 所属 XObject* / 平台后端上下文（借用，可为 NULL）：IoRing 分发用 owner XObject*，共享内存等平台后端保存平台私有上下文（其首个成员为 XObject）。 */
    uint8_t m_type;   /* XFdType 枚举值。 */
} XFileDescriptor;
```

关键点：

- `XFileDescriptor` 保持 `XFixedPool` O(1) 槽位，不做成 `XObject`；
- 设备引擎字段（`m_device`、`m_state`、`m_ioMode`、`m_pendingOps`、`m_lastError`、
  引用计数、世代号等）已全部移入 `XDeviceContext`；`XFileDescriptor` 只保留 3 个字段，
  其中 `m_type` 用单字节 `uint8_t` 压缩；
- `XDevice` 流程中 `m_deviceCtx` 保存 Open 虚函数返回的 `XDeviceContext*`；
  `object` 保存统一句柄关联的所属对象：IoRing 事件回调的 owner XObject*，共享内存等平台后端保存平台私有上下文（其首个成员为 XObject）；
- 旧 API（`XFd_alloc/free/get/...`）保持兼容。

## 7. 统一分发

```c
static XDeviceContext* XDevice_getCtx(XFd fd)
{
    XFileDescriptor* desc = XFd_get(fd);
    if (!desc || (XFdType)desc->m_type != XFD_TYPE_CLASS) return NULL;
    XDeviceContext* deviceCtx = (XDeviceContext*)desc->m_deviceCtx;
    if (!deviceCtx || deviceCtx->m_state == XDeviceState_Closing) return NULL;
    return deviceCtx;
}

int64_t XDevice_read(XFd fd, void* buf, int64_t size)
{
    XDeviceContext* ctx = XDevice_getCtx(fd);
    ...
    return XDevice_dispatchRead(ctx->m_device, ctx, buf, size);
}
```

`XDevice` 核心不再有任何 `switch(type)`。新增设备 = 新增设备子类 + 注册类名。

## 8. 设备接入方式

### 8.1 内置设备

| 类型 | 设备类 | 复用实现 |
| --- | --- | --- |
| File | `XDeviceFile` | `XDeviceFile_win32/posix/Fatfs` |
| Console | `XDeviceConsole` | 标准输入平台实现（已接入 XDevice 门面） |
| Dir | `XDeviceDir` | 目录迭代器平台实现（已接入 XDevice 门面） |
| Serial | `XDeviceSerialPort` | `XSerialPortWin32/Posix/STM32`（待迁移） |
| Timer | `XDeviceTimer` | `XAbstractEventDispatcher` 定时器（待迁移） |
| Socket | `XDeviceSocket` | `XNetwork`（Bind/Connect/Listen/Accept）（待迁移） |

### 8.2 新设备

每个新设备写一个设备类文件（如 `Src/XDevice/XDeviceGpio.*`），
实现 `XDevice` 虚槽并注册类名，即可通过 `XDevice_openClass("gpio", ...)` 打开。

## 9. 迁移阶段

| 阶段 | 内容 | 验收 |
| --- | --- | --- |
| P0 | 删除旧 `XDrive.*` 和旧设计文档，新增 `XDevice/XDeviceFile` | 工作区干净，基线编译通过 |
| P1 | `XFileDescriptor` 精简为 m_deviceCtx/object/m_type；`XDeviceContext` 从 fd 剥离并作基类；虚函数统一传 `XDeviceContext*` | 编译通过，旧调用方不变 |
| P2 | 文件设备走 `XDeviceFile`，各平台替换 `XDeviceFile` 平台实现 | 文件设备回归通过，`XDeviceFile` 可移除 |
| P3 | 迁移 CONSOLE/DIR/MAPPING/SERIAL/TIMER/SOCKET 为 `XDevice` 子类 | 各设备回归通过 |
| P4 | 新设备接入（GPIO/ADC/PWM/CAN/I2C/SPI/USB）；补 mock 设备测试 | `XDevice_openClass` 可打开任意注册设备 |

## 10. 测试方案

- 通用测试：`XDevice_open/close`、重复 close、属性读写、错误码。
- 设备测试：文件、目录、共享内存、串口、定时器、Socket 回归。
- mock 设备：注册固定行为设备类，验证 `XDevice` 分发与生命周期。
- 平台测试：Windows IOCP、Linux io_uring、lwIP、FatFS、STM32 编译与运行回归。

## 11. 验收标准

1. 所有设备统一使用 `XFd` 句柄，`XDevice_*` 为唯一公共入口。
2. 新增设备只写设备类文件，不改 `XDevice` 核心。
3. 现有公共 API 在迁移期保持可用，迁移完成后删除旧 `XFd_alloc` 直调。
4. 异步销毁顺序正确：CLOSING → 取消在途 IO → 注销事件源 → 屏蔽迟到事件 →
   关闭原生句柄 → 释放扩展上下文 → 回收槽位。
5. 保持 C99，静态注册，不动态加载。

## 12. 头文件注释要求（按代码风格指南执行）

- 文件头包含 `@file`、`@brief`、`@details`，说明模块用途、对齐对象和平台约束。
- 每个 `typedef struct`、`typedef enum` 先写 `@brief`，需要时写 `@details`。
- 枚举类型逐项解释每个枚举值的含义、数值约定和是否可按位组合。
- 结构体每个公开成员用行尾 `/**< ... */` 注释，说明单位、有效范围、NULL 语义、
  所有权和调用者可否修改。
- `m_class` 注明“第一个成员、由 XClass 管理、禁止手工修改”。
- 每个公开函数使用完整块注释，`@param` 覆盖声明中的每一个参数，参数名与声明完全一致；
  `@return` 说明所有成功/失败/空结果/越界路径。
- 返回指针必须明确“新对象/借用对象/空句柄”和释放方式；布尔值说明成功条件。
- 注释全中文，与实际实现保持一致。

## 13. 约束与风险

- 保持 C99：不使用 C11 `_Static_assert`，vtable/固定数组均兼容。
- `XFileDescriptor` 不做成 `XObject`，避免把事件循环拖进热路径。
- 设备专有能力不进入 `XDevice` 公共接口，保留在各专用头。
- 小整型字段统一用位域或 `uint8_t/int16_t` 压缩，减少 `XFileDescriptor` 与
  `XDeviceContext` 的结构体体积。
- 迁移期旧 API 与设备类并存，分阶段切换，避免一次性大爆炸。
