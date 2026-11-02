# XVfd 变频器设备层设计文档

> 2026-11-02 零遍历重构。本文说明 XVfd 的分层、语义层为何与总线无关、如何新增品牌、以及零遍历实现。
> 读者对象：后续维护设备层、或需要接入新品牌变频器的工程师。

---

## 1. 分层

```
┌──────────────────────────────────────────────────────────┐
│ XVfdDevice（语义层，产品代码）                            │
│   60 个领域 API：start/stop/setFrequency/readParam/...    │
│   只认 role（语义槽号），按 role 作下标取槽 → O(1)        │
│   不认识 Modbus / CAN / EtherCAT，一个头文件都不包含      │
└──────────────────────────┬───────────────────────────────┘
                           │ 绑定点 / 信号
┌──────────────────────────▼───────────────────────────────┐
│ 品牌子类（XVfdM512 / XVfdAtv61）                          │
│   提供：语义表（role/type/div/caps/signed/tag）           │
│        参数表（名称/单位/范围/权限）                       │
│        命令码表 / 故障码表                                │
│   不提供：任何总线地址                                     │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│ 绑定层（各品牌的 *_Modbus.c）                             │
│   提供：点地址表、通信组表、从站约束                       │
│   换一条总线 → 新增一个 *_EtherCAT.c，品牌主文件不动      │
└──────────────────────────────────────────────────────────┘
```

---

## 2. 为什么设备层不认识任何总线

### 2.1 role 驱动的数据流

设备层与通道层的全部契约只有两个：

| 方向 | 接口 | 内容 |
|---|---|---|
| 绑定 | `XVfdDevice_bindRole(dev, role, point)` | 声明「这个点是我的第 N 号语义槽」 |
| 入向 | `inputChanged(role, value)` 信号 | 第 N 号槽有值了 |
| 出向 | `XIoChannelGroup_writeAnalogOutput(group, role, value)` | 我要写第 N 号槽 |

**没有一处传地址、传链路、传协议。** 设备层的 `m_slots[role]` 是一张按语义角色编号的表，`role` 是连续枚举（0..67），下标直达。

### 2.2 验证

```bash
grep -rn "XModbus" Src/Industry/XVfd/*.h Src/Industry/XVfd/*.c
```

命中项**全部在注释里**（说明"本层不认识 Modbus"的注释），没有一行代码引用。

---

## 3. 三层职责（2026-11-02 地址剥离后）

| 层 | 文件 | 只放什么 |
|---|---|---|
| 语义层 | `XVfdDevice.h/.c` | role 驱动的数据流、动作监督、状态快照。**无地址** |
| 语义表 | `XVfdM512.c` / `XVfdAtv61.c` | `XVfdPointSpec`（role/type/div/tag/caps/signed）+ 参数语义 + 命令码 + 故障码 |
| **绑定层** | `XVfdM512_Modbus.c` / `XVfdAtv61_Modbus.c` | **地址**：`XVfdPointBinding`（role→寄存器）、`XVfdParamBinding`、`XVfdCommGroup`、`XVfdUnitProtocol` |

关键结构（`XVfdBind.h`）：

```c
typedef struct XVfdPointBinding {
    uint16_t m_role;       /* 与语义表的 role 配对 */
    uint16_t m_address;    /* 总线原生地址 */
    uint8_t  m_bitOffset, m_bitWidth;
} XVfdPointBinding;

typedef struct XVfdBindTable {
    const XVfdPointBinding* m_points;  int m_pointCount;
    const XVfdParamBinding* m_params;  int m_paramCount;
    const XVfdCommGroup* m_groups;     int m_groupCount;
    const XVfdUnitProtocol* m_protocol;
} XVfdBindTable;

const XVfdBindTable* XVfdM512_modbusBinding(void);   /* 品牌 × 总线 */
```

**换总线只换这份表**：接 EtherCAT 就新增 `XVfdM512_EtherCAT.c` 提供另一份
`XVfdBindTable`，`XVfdM512.c` 一行不用改。

验证（品牌主文件不应含地址）：

```bash
grep -c "m_address\|0x[0-9A-F][0-9A-F][0-9A-F]" Src/Industry/XVfd/XVfdM512.c   # 应极少/仅注释
```

## 4. 如何新增一个变频器品牌

以新品牌 `XVfdXxx` 为例，需要做三件事：

### 4.1 语义表（品牌主文件）

```c
static const XVfdPointSpec s_xxxSpecs[] = {
    /* role                    type                  div  addr  tag   caps  signed */
    { XVfdRole_StatusWord,     XIoPoint_AnalogInput,   1,   -1,  "STATE", 0, 0 },
    { XVfdRole_OutputFreq,     XIoPoint_AnalogInput, 100,   -1,  "RFR",   0, 0 },
    { XVfdRole_FreqSet,        XIoPoint_AnalogOutput,100,   -1,  "FREQ%", 0, 1 },
    ...
};
```

注意：**`m_address` 字段不再承担地址职责**。地址在绑定层给出。

沿用 `XVfdPointRole` 的 68 个标准语义角色，不要自造。

### 4.2 绑定表（`XVfdXxx_Modbus.c`）

```c
static const XVfdPointBinding s_xxxPointBindings[] = {
    /* role                  addr    bitOffset  bitWidth */
    { XVfdRole_StatusWord,   0x3000, 0, 0 },
    { XVfdRole_OutputFreq,   0x1001, 0, 0 },
    { XVfdRole_FreqSet,      0x1000, 0, 0 },
    ...
};

static const XVfdCommGroup s_xxxGroups[] = {
    /* remoteStart  count  area */
    { 0x1000,  1,  XIoPoint_AnalogOutput },
    { 0x1001,  8,  XIoPoint_AnalogInput  },
    ...
};

static const XVfdUnitProtocol s_xxxProtocol = {
    12,     /* 单帧最多读 12 个寄存器 */
    true,   /* 读帧不跨组 */
    true,   /* 只支持 FC06 */
};

const XVfdBindTable* XVfdXxx_modbusBinding(void) { ... }
```

### 4.3 类骨架

```c
typedef struct XVfdXxx {
    XVfdCommandEnum m_base;   /* 或 XVfdCiA402，取决于命令模型 */
    /* 品牌私有状态 */
} XVfdXxx;

void XVfdXxx_init(XVfdXxx* dev)
{
    memset(dev, 0, sizeof(XVfdXxx));
    XVfdCommandEnum_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdXxx_class_init();
    XVfdDevice_setParamTable(&dev->m_base.m_base, s_xxxParams, PARAM_COUNT);
}
```

**不需要写**：任何地址、任何总线、任何轮询。这些都在 XChannel 层。

---

## 5. 零遍历设计

### 5.1 语义槽按 role 下标

```c
typedef struct XVfdSlot {
    const XVfdPointSpec* m_spec;  /* 完整规格（借用，仅供品牌自定义解码） */
    XIoPoint* m_point;            /* 通道点（借用） */
    int32_t   m_div;              /* ★ 内联：原生值→工程值除率 */
    uint8_t   m_signed;           /* ★ 内联：16 位补码标记 */
} XVfdSlot;
```

**为什么要内联**：旧实现只存 `m_spec` 指针，每次解码都要经指针再取 `div`/`signed`（多一次缓存访问）。解码是数据到达路径上的最高频操作，内联后省掉这一跳。

`m_spec` 仍保留，供品牌自定义 `DecodePoint` 读取其它字段（如 `m_caps`）。

### 5.2 参数绑定按 paramId 下标

```c
XIoPoint** m_paramPoints;   /* 长度 = m_paramCount，paramId 直接作下标 */
```

旧实现是 `XVfdParamBinding m_paramBinds[64]` + 线性查 id，O(64)。现在 O(1)。

**惰性分配**：表在首次 `bindParamPoint` 时才分配。理由：部分品牌在 `init` 期就设参数表（此时设备尚未挂接通道组），提前分配只是徒增一次堆交互；且「表设了但没有点」是一个合法状态。

### 5.3 绑定数用计数器

```c
int m_boundCount;   /* bind/unbind 时增减 */
```

旧实现 `XVfdDevice_boundCount()` 每次扫 96 个槽。

### 5.4 已删除的函数

| 函数 | 原复杂度 | 删除理由 |
|---|---|---|
| `xvfdSlotByPoint` | O(68) | 信号改带 role，不再需要按点反查 |
| `xvfdParamBinding` | O(64) | paramId 直接作下标 |
| `boundCount` 扫描 | O(96) | 改计数器 |

---

## 6. 与 XChannel 的接口

### 6.1 挂接

```c
XVfdDevice_attach(dev, group);   /* 订阅组的 inputChanged / writeDone */
```

**注意**：一个设备只挂一个组。多台设备（多从站）时**每台各挂自己的组**——role 是组内下标，共用组会导致同名语义槽冲突。

### 6.2 绑定点

```c
bool XVfdDevice_bindRole(dev, role, point);              /* 推荐 */
bool XVfdDevice_bindSpec(dev, spec, point);              /* 带规格（内联 div/signed/caps） */
bool XVfdDevice_bindPoint(dev, spec, point);             /* 旧签名，委托给 bindSpec */
```

### 6.3 读写

```c
/* 读：优先读组内缓存，缓存为空时回落链路直接读一次 */
XVfdDevice_readRole(dev, role, &value);

/* 写：经组的 role 口径写，标度与符号由 XIoPoint_toRaw 处理 */
XVfdDevice_writeRole(dev, role, nativeValue);
```

### 6.4 动作监督

异步链路的写要等应答，设备层用 `writeDone(role, ok)` 信号收口，超时由 `m_actionTimer` 判定。

**同步链路**（`XIoLink_isAsync` 返回 false）在 `WriteRole` 返回前就完成了，此时不能再 arm 定时器等信号——否则必然误判超时。

---

## 7. 踩坑记录

### 7.1 结构体增大导致增量编译踩堆

`XVfdSlot` 从 16B 增到 24B（加了 `m_div`/`m_signed`），`XVfdDevice` 结构体随之变大。

**现象**：进程在 `XVfdCommandEnum_init` 的 `memset` 处崩溃，报堆损坏。

**根因**：增量编译时部分 `.obj` 仍按旧结构体大小编译，`memset` 写越界踩到堆的 guard page。

**诊断**：用 `gflags +hpa` 启用 pageheap，cdb 抓到崩溃时寄存器 `r9=0xE0`（224）与实际 `sizeof` 对不上，证明是大小不一致。

**修复**：清空构建目录全量重编。

**教训**：Windows/Ninja 主库 obj 依赖记录不全，**改公共结构体后必须全量重编**。

### 7.2 多站共用通道组导致 role 冲突

两台 M512 都有 `StatusWord`(4)、`FaultCode`(5) 等同名语义槽。共用一个通道组时，`addPoint` 第二个就失败（role 位已占用）。

**修复**：每台设备一个通道组 + 一个链路（链路可共用同一个 Hub）。

### 7.3 读值回流

纯推送模型下，点只有被喂过才有值。但业务判断（「命令源未配置时拒绝 start」）依赖「首拍已回、值为默认 0」而非「完全无数据」。

**修复**：`xvfdReadPoint` 在缓存为空时回落 `XIoLink_readRoleBase` 向链路要一次。

### 7.4 有符号量两处归一（本次收口）

旧实现：点层 `xIoAnalogPointToRaw` 钳 0..65535 且抹负号；设备层 `m_signed` 分支做补码归一。**同一语义两处实现、行为不同、走哪条路径结果不同。**

新实现：符号语义只在 `XIoPoint_toRaw` 定义一次，设备层不再处理符号。

---

## 8. 框架语义对齐

本次**零框架改动**。所有改动在 `Src/Industry/XVfd/` 内。

---

## 9. 领域 API 清单（保持不变）

约 50 个领域操作全部保留签名与语义：

**生命周期**：`create_ex` / `init` / `attach` / `detach` / `isAttached`

**点位**：`bindRole` / `bindSpec` / `bindPoint` / `unbindPoint` / `hasPoint` / `point` / `boundCount` / `spec`

**参数**：`setParamTable` / `paramCount` / `paramDesc_const` / `bindParamPoint` / `paramPoint` / `readParam` / `writeParam`

**动作**：`start` / `stop` / `freeStop` / `quickStop` / `jog` / `setFrequency` / `setSpeedRpm` / `setAccelDecel` / `selectPresetSpeed` / `setPresetSpeedHz` / `setPidTarget` / `setCommandSource` / `setFrequencySource` / `resetFault` / `setDigitalOutput` / `startMotorTuning` / `factoryReset` / `unlockPassword` / `saveParameters`

**状态**：`status_const` / `isBusy` / `lastError` / `caps` / `hasCapability` / `setStatusInterval` / `statusInterval` / `setActionTimeout` / `actionTimeout`

**频率换算**：`maxFrequency` / `frequencyToRaw` / `rawToFrequency`

**信号**：`statusUpdated` / `commandCompleted` / `paramReadDone` / `paramWriteDone` / `nameplateUpdated` / `faultHistoryUpdated` / `errorOccurred`
