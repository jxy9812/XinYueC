# XChannel 通道层设计文档

> 2026-11-02 彻底重写。本文说明 XChannel 的分层职责、零遍历设计、以及如何接入一条新链路。
> 读者对象：后续维护本框架、或需要接入 CAN / EtherCAT / GPIO 等新总线的工程师。

---

## 1. 为什么重写

旧实现（2026-11-01 版）有三个结构性问题：

| 问题 | 表现 |
|---|---|
| **点身兼三职** | `XModbusPoint` 同时是数据容器、链路连接持有者、装配产物 |
| **链路类型硬编码进点类** | 点里存 `XModbusHub*`，加 CAN 就得写 `XCanPoint`，点类数 = 链路数 × 数据类型数 |
| **N 点 N 订阅** | 每个点各订阅一次 Hub 信号，值变化广播 N 次后各自比对地址丢弃 |

M512 单设备 66 个点即 66 条信号连接、每次值变化 66 次回调。

重写后的核心主张：**点是纯数据容器，链路能力全部收进 XIoLink，组负责分发。**
加一条链路只写一个 `XxxLink`，点类、通道组、设备层一行都不用改。

---

## 2. 分层与职责

```
┌─────────────────────────────────────────────────────────┐
│ 设备层（XVfdDevice 等）                                  │
│   只认 role（语义槽号），按 role 作下标取槽 → O(1)        │
└────────────────────────┬────────────────────────────────┘
                         │ 绑定点 / inputChanged(role,value)
┌────────────────────────▼────────────────────────────────┐
│ XIoChannelGroup（全组唯一 XObject）                      │
│   m_points[role]  ← role 作下标的密集点表，O(1)          │
│   m_link          ← 持有唯一链路（借用）                  │
│   职责：点管理 + 信号分发 + 链路挂接                       │
└────────────────────────┬────────────────────────────────┘
                         │ feedRole(role, raw) / completeWrite(role, ok)
┌────────────────────────▼────────────────────────────────┐
│ XIoLink（链路抽象基类）                                  │
│   虚槽：BindRole / ReadRole / WriteRole / IsAsync        │
│         Start / Stop                                     │
│   唯一知道「数据怎么进出总线」的层                         │
└────────────────────────┬────────────────────────────────┘
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
   XModbusLink      XCanLink(待)    XEtherCatLink(待)
```

### 2.1 XIoPoint —— 纯数据容器（数字量即基类）

```c
typedef struct XIoPoint {
    XClass m_class;
    int32_t m_value;       /* DI/DO 0/1；AI/AO 定点工程值 */
    const char* m_tag;
    uint16_t m_role;       /* 语义槽号 = 组内下标 */
    uint16_t m_type;       /* DI / DO / AI / AO */
    union { uint16_t m_raw; struct { ... } bits; } m_flags;
} XIoPoint;

/* [2026-11-03 数字/模拟拆分] 模拟量子类：基类 + 标度四件套 */
typedef struct XIoAnalogPoint {
    XIoPoint m_base;       /* 第一成员：基类 */
    int32_t m_scaleNum, m_scaleDen, m_offset, m_deadband;
} XIoAnalogPoint;
```

**没有虚槽、没有链路指针。** 它不"会读"，只是"被读写"。

关键点：
- `m_role` 是它在组里的位置，也是它和设备的约定
- **[2026-11-03 数字/模拟拆分]** 标度四件套只有 AI/AO 消费，拆入子类
  `XIoAnalogPoint`（沿用重写前历史类名）：数字点 64→48 字节（x64），
  模拟点不变。子类仍是纯数据容器（零新虚槽），**不复活**「点自己会读写」
  的行为点类；装配按类型选类（AI/AO → 子类），组登记闸口拒绝
  「AI/AO 类型但非 XIoAnalogPoint 实例」的装配错误
- 标度换算收进 `XIoPoint_toEngineering`（基类统一入口，按类型/虚表分发）
  / `XIoAnalogPoint_toRaw`（模拟量专属）
- **符号语义只在一处定义**：`XIoAnalogPoint_toRaw` 按 `m_signed` 分流，
  有符号点负值返回 16 位补码，无符号点钳 0

### 2.2 XIoChannelGroup —— 点管理 + 分发

```c
typedef struct XIoChannelGroup {
    XObject m_class;
    XIoPoint* m_points[XIOCHANNELGROUP_ROLE_CAP];  /* role 作下标 */
    XIoLink* m_link;
    int m_pointCount;                               /* 计数器，免遍历 */
} XIoChannelGroup;
```

**本文件内没有任何按点查找的循环**（唯一遍历在析构里，生命周期终点一次性开销）。

### 2.3 XIoLink —— 链路抽象

```c
bool XIoLink_bindRole_base(XIoLink*, uint16_t role, uint32_t nativeAddr, uint32_t aux);
bool XIoLink_readRole_base(XIoLink*, uint16_t role, int32_t* outRaw);
bool XIoLink_writeRole_base(XIoLink*, uint16_t role, int32_t raw);
bool XIoLink_isAsync_base(const XIoLink*);
bool XIoLink_start_base(XIoLink*);
void XIoLink_stop_base(XIoLink*);
```

**`isAsync` 的语义很重要**：同步链路（GPIO 写电平、进程内假从站）在 `WriteRole` 返回前就已经调完 `completeWrite`；异步链路（Modbus 要等应答）在应答到达时才调。上层据此判断要不要 arm 动作监督定时器——对同步链路 arm 定时器必然误判超时。

---

## 3. 零遍历设计

### 3.1 三处都是数组下标

| 位置 | 操作 | 复杂度 |
|---|---|---|
| 组内存放 | `m_points[role] = pt` | O(1) |
| 组内查找 | `m_points[role]` | O(1) |
| 设备层取槽 | `m_slots[role]` | O(1) |

旧实现的 `xvfdSlotByPoint`（线性扫 68 槽找点）与 `xvfdParamBinding`（线性扫 64 项）**已删除**。

### 3.2 XModbusLink 的双表设计

数据路径上有两个方向，各用一张表：

```
正向（数据到达）：地址 → role
  m_maps[area].m_roleByAddr[addr - m_base] = role
  下标 = addr - m_base，一次减法 + 一次判界 + 一次取址 = O(1)

反向（上层读写）：role → 地址
  m_roleArea[role], m_roleAddr[role]
  直接下标 = O(1)
```

**为什么不用二分**：Hub 层 `hubViewFind` 用二分（O(log N)），但对数据到达路径来说，直接映射还能再快一档。密集表的内存开销按**实际映射区间**分配（不是 0..65535 全域），M512 实测 22 段共约 200 个地址，每设备约 1KB。

```
m_base = 该区已绑定地址的最小值
m_span = 覆盖 [m_base, m_base+span)
m_roleByAddr[i] = role 或 XMODBUSLINK_ROLE_NONE(0xFFFF)
```

### 3.3 订阅数 66 → 1

旧：每个点各 `XObject_connect_1` 一次 Hub 信号。
新：`XModbusLink` 在 `Start` 里订阅一次 `valueChanged` + 一次 `writeCompleted`。

M512 单设备不论挂多少点，`XModbusLink_subscriptionCount()` 恒为 **2**（两条信号各一条）。

数据到达后的分发是「按区间顺序回填」（连续地址的缓存友好遍历），不是查找。

---

## 4. 如何接入一条新链路

以 CAN 为例，**只需要新增一个文件对**：

### 步骤 1：定义结构体

```c
XCLASS_DEFINE_BEGING(XCanLink)
XCLASS_DEFINE_ENUM(XCanLink, BindRole) = XCLASS_VTABLE_GET_SIZE(XIoLink),
XCLASS_DEFINE_ENUM(XCanLink, ReadRole),
XCLASS_DEFINE_ENUM(XCanLink, WriteRole),
XCLASS_DEFINE_ENUM(XCanLink, IsAsync),
XCLASS_DEFINE_ENUM(XCanLink, Start),
XCLASS_DEFINE_ENUM(XCanLink, Stop),
XCLASS_DEFINE_END(XCanLink)

typedef struct XCanLink {
    XIoLink m_base;              /* 第一成员 */
    XCanBusDevice* m_dev;        /* 借用 */
    /* 自己的 role ↔ CAN 寻址映射（按需设计，建议 O(1)） */
} XCanLink;
```

### 步骤 2：实现虚槽

| 虚槽 | 要做什么 |
|---|---|
| `BindRole(role, nativeAddr, aux)` | 把 role 与 `(canId, byteOffset, bitLen)` 关联起来，建立自己的映射 |
| `ReadRole(role, out)` | 从本地缓存取值，**不得发起阻塞传输** |
| `WriteRole(role, raw)` | 组装 CAN 帧发出；异步链路在应答到达后调 `XIoChannelGroup_completeWrite` |
| `IsAsync` | 同步链路返回 false（GPIO、内存假从站）；要等应答的返回 true |
| `Start` | 订阅总线的接收帧信号（**只订阅一次**） |
| `Stop` | 断开订阅，不释放总线 |

### 步骤 3：数据到达时喂给组

```c
static void onCanFrame(XObject* receiver, XVarList* args)
{
    XCanLink* self = (XCanLink*)receiver;
    XIoChannelGroup* group = (XIoChannelGroup*)XIoLink_group(&self->m_base);
    /* 按自己的索引把帧翻译成 (role, raw) */
    XIoChannelGroup_feedRole(group, role, raw);
}
```

### 步骤 4：装配

用 `XChannelAssembly_mount(group, link, spec, &result)`，或直接：

```c
/* AI/AO 用 XIoAnalogPoint（承载标度）；DI/DO 用基类 XIoPoint（瘦身）。 */
XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
XIoPoint* pt = &apt->m_base;
XIoPoint_setRole(pt, role);
XIoPoint_setType(pt, XIoPoint_AnalogInput);
XIoAnalogPoint_setScale(apt, 1, 10, 0, 0);
XIoChannelGroup_addPoint(group, pt, true);
XIoLink_bindRole_base(link, role, nativeAddr, aux);
XVfdDevice_bindRole(dev, role, pt);
```

**不需要改**：`XIoPoint`、`XIoAnalogPoint`、`XIoChannelGroup`、`XVfdDevice`、任何品牌子类。

---

## 5. 性能设计细节

### 5.1 密集表扩容

`areaMapBind` 在装配期扩容：新地址落在现有表外时，算出覆盖旧区间与新区间的最小范围，分配新表、`memcpy` 平移旧内容。

**扩容只发生在装配期**，数据路径上永远只是「减法 + 判界 + 取址」。

### 5.2 内存布局

```
XModbusLink (约 120 字节)  +  每区一张密集表
                              M512 实测约 1KB/设备（22 段、约 200 个地址）
```

对比全域密集表方案（4 区 × 65536 × 2B = 512KB/设备），基址偏移方案省下约 500 倍。

---

## 6. 踩坑记录

### 6.1 负数被静默吞掉（本次根修）

旧 `xIoAnalogPointToRaw` 的算法：

```c
quotient = numAbs / snAbs;        /* 取绝对值做除法 */
if (num < 0) quotient = -quotient; /* 恢复符号 */
if (quotient < 0) quotient = 0;    /* ← 立刻又抹掉 */
```

**每一个负给定都变成 0。** M512 的 FreqSet 靠设备层 `m_signed` 分支绕开，直走组层写 AO 则静默失效。

新实现把符号语义收进 `XIoAnalogPoint_toRaw` 一处（2026-11-03 拆分后
位于模拟量子类；函数体原样迁移）：

```c
if (point->m_flags.bits.m_signed) {
    return (uint16_t)((uint32_t)raw & 0xFFFFu);   /* 16 位补码 */
}
if (raw < 0) raw = 0;                              /* 无符号：钳 0 */
```

对比 Qt 6.8：`QVariant::toUInt()` 对负数返回 0 并置 `ok=false`（不静默），本框架选择按点语义分流，语义更明确。

### 6.2 改公共头必须全量重编

`XVfdSlot` 加了两个字段（16B → 24B），`XVfdDevice` 结构体随之变大。增量编译时部分 `.obj` 仍按旧大小编译，导致 `memset` 越界踩堆，进程在 `XVfdCommandEnum_init` 崩溃。

**pageheap 抓到的寄存器值是决定性证据**：`r9=0xE0`（224 字节）与实际 `sizeof` 对不上。

教训：Windows/Ninja 主库 obj 依赖不全，改公共结构体后**必须清构建目录重编**，不能依赖增量。

### 6.3 多站不能共用一个通道组

role 是组内下标，两台同型号变频器都有 `StatusWord`/`FaultCode` 等同名语义槽，共用一个组必然冲突（`addPoint` 返回 false）。

正确做法：**每个设备一个通道组 + 一个链路**（链路可以共用同一个 Hub），role 天然每设备独立。

### 6.4 读值需要「缓存为空时回落链路」

纯推送模型下，点只有被 `feedRole` 过才有值。但业务判断（如「命令源未配置时拒绝 start」）依赖「首拍已回、值为默认 0」而非「完全无数据」。

`xvfdReadPoint` 因此增加了回落：缓存无值时调 `XIoLink_readRole_base` 直接向链路要一次。

---

## 7. 框架语义对齐

本次**零框架改动**。XChannel 层的所有代码都在 `Src/Industry/XChannel/` 内，未修改 `Src/XCore` 下的框架代码。

验证方式：

```bash
git diff --stat HEAD -- Src/XCore Src/XGui
# 应无输出（除本次会话早前的 XLink/XModbus 修复，属另一战役）
```

唯一涉及框架的改动是 `XModbusHub` 里的临时诊断代码，已在最终提交前全部清除。

---

## 8. 容器用法纪律

本层大量使用 `XVector`（Hub 内部），沿用以下纪律：

| 规则 | 说明 |
|---|---|
| **借用指针不延长生命周期** | `XVector_at_base` / `XVector_data` 返回的是借用指针，容器扩容后失效 |
| **值语义 vs 指针语义** | `XVector` 存的是值拷贝；存指针时容量变化不影响指针本身，但存的指针可能悬垂 |
| **扩容后重新取址** | 扩容会让所有元素地址失效（同 `std::vector` 的 iterator invalidation） |
| **不用错语义** | 用错的表现是「偶发崩溃」「读到旧值」「写完没生效」——看起来像框架 bug |

本次重构中 `XModbusLink` 的密集表用 `XMemory_malloc` 裸分配而非 `XVector`，就是为了避开扩容失效问题：表是定长密集数组，扩容时由 `areaMapBind` 显式重建并平移。
