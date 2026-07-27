# XinYueC 内存管理文档

## 目录

- [概述](#概述)
- [XMemory 内存管理器](#xmemory-内存管理器)
- [XFixedPool 固定大小内存池](#xfixedpool-固定大小内存池)
- [XMultiPool 多级内存池](#xmultipool-多级内存池)
- [字节序处理](#字节序处理)
- [附录](#附录)

---

## 概述

XinYueC内存管理模块提供了灵活的内存管理机制，支持系统内存、内存池和混合模式。

### 核心特性

- **多种内存类型**：支持系统内存、内存池、混合模式
- **自定义内存管理器**：可自定义malloc/free/realloc/calloc
- **固定大小内存池**：O(1)时间复杂度的分配和释放
- **多级内存池**：支持多种块大小的内存管理
- **线程安全**：内存池操作是线程安全的
- **字节序处理**：支持跨平台的字节序转换

### 内存类型

| 类型 | 说明 |
|------|------|
| `XMEMORY_TYPE_SYSTEM` | 使用系统malloc/free |
| `XMEMORY_TYPE_MULTIPOOL` | 使用XMultiPool内存池 |
| `XMEMORY_TYPE_HYBRID` | 组合模式：小内存用内存池，大内存用系统 |

---

## XMemory 内存管理器

XMemory提供全局内存管理接口，支持自定义内存分配器。

### 头文件

```c
#include "XMemory.h"
```

### 结构体定义

```c
// 内存管理方法结构体
typedef struct XMemory {
    MallocMethod malloc;    // 内存申请函数
    FreeMethod free;        // 内存释放函数
    ReallocMethod realloc;  // 内存重分配函数
    CallocMethod calloc;    // 零初始化内存分配函数
} XMemory;

// 函数指针类型
typedef void* (*MallocMethod)(size_t size);
typedef void (*FreeMethod)(void* ptr);
typedef void* (*ReallocMethod)(void* ptr, size_t size);
typedef void* (*CallocMethod)(size_t count, size_t size);
```

### 内存分配函数

#### XMemory_malloc

```c
void* XMemory_malloc(size_t size, XMemoryType type)
```

申请内存。

**参数:**
- `size` - 申请的内存大小(字节)
- `type` - 内存类型

**返回值:** 成功返回内存块指针，失败返回NULL

---

#### XMemory_free

```c
void XMemory_free(void* ptr, XMemoryType type)
```

释放内存。

**参数:**
- `ptr` - 待释放的内存块指针
- `type` - 内存类型

**返回值:** 无

---

#### XMemory_realloc

```c
void* XMemory_realloc(void* ptr, size_t size, XMemoryType type)
```

重新分配内存。

**参数:**
- `ptr` - 原内存块指针
- `size` - 新的内存大小(字节)
- `type` - 内存类型

**返回值:** 成功返回新内存块指针，失败返回NULL

---

#### XMemory_calloc

```c
void* XMemory_calloc(size_t count, size_t size, XMemoryType type)
```

申请并初始化为零的内存。

**参数:**
- `count` - 元素数量
- `size` - 单个元素大小(字节)
- `type` - 内存类型

**返回值:** 成功返回内存块指针，失败返回NULL

---

### 快捷宏

| 宏 | 说明 |
|----|------|
| `XMalloc(size, type)` | 申请内存 |
| `XFree(ptr, type)` | 释放内存 |
| `XRealloc(ptr, size, type)` | 重分配内存 |
| `XCalloc(count, size, type)` | 零初始化分配 |
| `XNew(type)` | 申请指定类型对象的内存 |
| `XDelete(ptr)` | 释放内存 |

### 系统内存函数

```c
void* XMalloc_System(size_t size);
void XFree_System(void* ptr);
void* XRealloc_System(void* ptr, size_t size);
void* XCalloc_System(size_t count, size_t size);
```

### 内存池函数

```c
void* XMalloc_MultiPool(size_t size);
void XFree_MultiPool(void* ptr);
void* XRealloc_MultiPool(void* ptr, size_t size);
void* XCalloc_MultiPool(size_t count, size_t size);
```

### 混合模式函数

```c
void* XMalloc_Hybrid(size_t size);
void XFree_Hybrid(void* ptr);
void* XRealloc_Hybrid(void* ptr, size_t size);
void* XCalloc_Hybrid(size_t count, size_t size);
```

### 自定义内存管理器

#### XMemory_setMethod

```c
void XMemory_setMethod(const XMemory* method, XMemoryType type)
```

设置全局内存管理方法。

**参数:**
- `method` - 内存管理方法结构体指针
- `type` - 内存类型

---

#### XMemory_setMallocMethod

```c
void XMemory_setMallocMethod(MallocMethod method, XMemoryType type)
```

设置内存申请函数。

---

#### XMemory_setFreeMethod

```c
void XMemory_setFreeMethod(FreeMethod method, XMemoryType type)
```

设置内存释放函数。

---

### 示例：自定义内存管理器

```c
// 自定义内存函数（由应用程序提供具体后端）
extern void* my_backend_allocate(size_t size);
extern void my_backend_release(void* ptr);
extern void* my_backend_resize(void* ptr, size_t size);
extern void* my_backend_zero_allocate(size_t count, size_t size);

void* my_malloc(size_t size) {
    printf("Allocating %zu bytes\n", size);
    return my_backend_allocate(size);
}

void my_free(void* ptr) {
    printf("Freeing memory\n");
    my_backend_release(ptr);
}

// 设置自定义内存管理器
XMemory myMemory = {
    .malloc = my_malloc,
    .free = my_free,
    .realloc = my_backend_resize,
    .calloc = my_backend_zero_allocate
};
XMemory_setMethod(&myMemory, XMEMORY_TYPE_SYSTEM);

// 使用XNew/XDelete
MyStruct* obj = XNew(MyStruct);
// ... 使用对象 ...
XDelete(obj);
```

---

## XFixedPool 固定大小内存池

XFixedPool是固定大小的内存池，提供O(1)时间复杂度的分配和释放操作。

### 头文件

```c
#include "XFixedPool.h"
```

### 结构体定义

```c
typedef struct XFixedPool {
    XAtomic_size_t free_list_head_packed; // 打包头指针(索引+版本)
    size_t block_size;          // 对齐后的块大小
    size_t user_block_size;     // 用户视角的块大小
    void* raw_memory;           // 数据缓冲区指针
    size_t total_raw_size;      // 数据缓冲区总大小
    size_t num_blocks;          // 总块数
    size_t index_bits;          // 索引位数
    size_t index_mask;          // 索引掩码
    size_t version_mask;        // 版本号掩码
    bool owns_memory;           // 所有权标记
} XFixedPool;
```

### 特性

- **O(1)分配和释放**：时间复杂度恒定
- **线程安全**：无锁实现，支持多线程
- **内存对齐**：支持内存块对齐，优化CPU缓存
- **两种模式**：支持动态模式和静态/栈模式

### 动态模式

#### XFixedPool_create

```c
XFixedPool* XFixedPool_create(size_t block_size, size_t num_blocks)
```

创建固定大小内存池(动态模式)。

**参数:**
- `block_size` - 每个内存块的大小(字节)
- `num_blocks` - 池中预分配的块数量

**返回值:** 成功返回XFixedPool指针，失败返回NULL

**说明:** 此函数会在堆上分配结构体和数据缓冲区

---

#### XFixedPool_create_from_memory

```c
XFixedPool* XFixedPool_create_from_memory(void* memory, size_t total_bytes, size_t block_size)
```

从已有内存数组创建内存池(动态模式)。

**参数:**
- `memory` - 用户提供的内存块
- `total_bytes` - memory的总大小
- `block_size` - 每个内存块的大小

**返回值:** 成功返回XFixedPool指针，失败返回NULL

**说明:** 结构体在堆上分配，但数据缓冲区使用用户提供的memory

---

#### XFixedPool_delete

```c
void XFixedPool_delete(XFixedPool* pool)
```

销毁内存池(动态模式)。

**参数:**
- `pool` - XFixedPool指针

**说明:** 如果`pool->owns_memory`为true，会释放数据缓冲区

---

### 静态/栈模式

#### XFixedPool_init

```c
bool XFixedPool_init(XFixedPool* pool, void* memory, size_t total_bytes, size_t block_size)
```

初始化内存池(静态模式)。

**参数:**
- `pool` - XFixedPool结构体指针
- `memory` - 用户提供的内存缓冲区
- `total_bytes` - memory的总大小
- `block_size` - 每个内存块的大小

**返回值:** 成功返回true，失败返回false

**说明:** 不进行任何动态内存分配

---

#### XFixedPool_deinit

```c
void XFixedPool_deinit(XFixedPool* pool)
```

反初始化内存池(静态模式)。

**参数:**
- `pool` - XFixedPool指针

**说明:** 仅清零结构体成员，不释放内存

---

### 内存操作

#### XFixedPool_malloc

```c
void* XFixedPool_malloc(XFixedPool* pool)
```

从内存池分配一个内存块。

**参数:**
- `pool` - XFixedPool指针

**返回值:** 成功返回内存块指针，池空返回NULL

---

#### XFixedPool_free

```c
void XFixedPool_free(XFixedPool* pool, void* block)
```

将内存块归还给内存池。

**参数:**
- `pool` - XFixedPool指针
- `block` - 要归还的内存块指针

**返回值:** 无

---

#### XFixedPool_is_from_pool

```c
bool XFixedPool_is_from_pool(const XFixedPool* pool, const void* ptr)
```

检查指针是否由该内存池分配。

**参数:**

- `pool` - XFixedPool指针
- `ptr` - 要检查的指针

**返回值:** 属于该内存池返回true，否则返回false

---

### 示例：动态模式

```c
// 创建内存池：每个块64字节，共100个块
XFixedPool* pool = XFixedPool_create(64, 100);
if (pool) {
    // 分配内存
    void* block1 = XFixedPool_malloc(pool);
    void* block2 = XFixedPool_malloc(pool);
    
    // 使用内存...
    
    // 释放内存
    XFixedPool_free(pool, block1);
    XFixedPool_free(pool, block2);
    
    // 销毁内存池
    XFixedPool_delete(pool);
}
```

### 示例：静态模式

```c
// 在栈上分配
XFixedPool pool;
char buffer[64 * 100];

// 初始化
if (XFixedPool_init(&pool, buffer, sizeof(buffer), 64)) {
    void* block = XFixedPool_malloc(&pool);
    // 使用内存...
    XFixedPool_free(&pool, block);
    XFixedPool_deinit(&pool);
}
```

---

## XMultiPool 多级内存池

XMultiPool是基于XVector的多级固定大小内存池，支持多种块大小的内存管理。

### 头文件

```c
#include "XMultiPool.h"
```

### 结构体定义

```c
typedef struct XMultiPool {
    XVector* sub_pools;           // 存储XFixedPool指针的数组
    bool owns_memory;             // 所有权标记
    bool is_power_of_two_mode;    // 是否启用倍数模式
    size_t initial_size;          // 初始大小
    size_t next_expected_size;    // 下一个期望块大小
    size_t growth_multiplier;     // 增长倍数
} XMultiPool;
```

### 特性

- **多种块大小**：支持管理多种不同大小的内存块
- **自动选择**：自动选择最小能满足需求的子池
- **二分查找**：O(log N)的查找性能
- **倍数模式**：支持按倍数增长的内存池配置
- **全局内存池**：提供全局内存池实例

### 动态模式

#### XMultiPool_create

```c
XMultiPool* XMultiPool_create(void)
```

创建空的多级内存池。

**返回值:** 成功返回XMultiPool指针，失败返回NULL

---

#### XMultiPool_delete

```c
void XMultiPool_delete(XMultiPool* multi_pool)
```

销毁多级内存池。

**参数:**
- `multi_pool` - XMultiPool指针

**说明:** 如果`owns_memory`为true，会销毁所有子池

---

### 静态模式

#### XMultiPool_init

```c
bool XMultiPool_init(XMultiPool* multi_pool)
```

初始化多级内存池(静态模式)。

**参数:**
- `multi_pool` - XMultiPool结构体指针

**返回值:** 成功返回true，失败返回false

---

#### XMultiPool_deinit

```c
void XMultiPool_deinit(XMultiPool* multi_pool)
```

反初始化多级内存池。

---

### 添加子池

#### XMultiPool_add_pool

```c
bool XMultiPool_add_pool(XMultiPool* multi_pool, XFixedPool* pool)
```

添加子池到多级内存池。

**参数:**
- `multi_pool` - XMultiPool指针
- `pool` - XFixedPool指针

**返回值:** 成功返回true，失败返回false

**说明:** 添加后按`user_block_size`升序排列

---

#### XMultiPool_enable_power_of_two_mode

```c
bool XMultiPool_enable_power_of_two_mode(XMultiPool* multi_pool, size_t initial_size, size_t multiplier)
```

启用倍数模式。

**参数:**
- `multi_pool` - XMultiPool指针
- `initial_size` - 第一个池的大小(字节)
- `multiplier` - 增长倍数(必须大于1)

**返回值:** 成功返回true，失败返回false

---

### 内存操作

#### XMultiPool_malloc

```c
void* XMultiPool_malloc(XMultiPool* multi_pool, size_t size)
```

从多级内存池分配内存。

**参数:**
- `multi_pool` - XMultiPool指针
- `size` - 请求的内存大小(字节)

**返回值:** 成功返回指针，失败返回NULL

---

#### XMultiPool_calloc

```c
void* XMultiPool_calloc(XMultiPool* multi_pool, size_t count, size_t size)
```

分配并清零内存。

**参数:**
- `multi_pool` - XMultiPool指针
- `count` - 元素个数
- `size` - 每个元素的大小

**返回值:** 成功返回指针，失败返回NULL

---

#### XMultiPool_realloc

```c
void* XMultiPool_realloc(XMultiPool* multi_pool, void* ptr, size_t size)
```

重新分配内存。

**参数:**
- `multi_pool` - XMultiPool指针
- `ptr` - 原指针
- `size` - 新大小

**返回值:** 成功返回新指针，失败返回NULL

---

#### XMultiPool_free

```c
void XMultiPool_free(XMultiPool* multi_pool, void* ptr)
```

释放内存。

**参数:**
- `multi_pool` - XMultiPool指针
- `ptr` - 要释放的指针

---

#### XMultiPool_is_from_pool

```c
bool XMultiPool_is_from_pool(const XMultiPool* multi_pool, const void* ptr)
```

检查指针是否由该多级内存池分配。

---

#### XMultiPool_getMaxUserSize

```c
size_t XMultiPool_getMaxUserSize(XMultiPool* mp, void* ptr)
```

获取分配的内存块的原始大小。

---

### 全局内存池

#### XMultiPool_global

```c
XMultiPool* XMultiPool_global()
```

获取全局内存池实例。

---

#### XMultiPool_global_malloc

```c
void* XMultiPool_global_malloc(size_t size)
```

从全局池分配内存。

---

#### XMultiPool_global_calloc

```c
void* XMultiPool_global_calloc(size_t count, size_t size)
```

从全局池分配并清零内存。

---

#### XMultiPool_global_realloc

```c
void* XMultiPool_global_realloc(void* ptr, size_t size)
```

从全局池重分配内存。

---

#### XMultiPool_global_free

```c
void XMultiPool_global_free(void* ptr)
```

释放内存到全局池。

---

### 示例：创建多级内存池

```c
// 创建多级内存池
XMultiPool* mp = XMultiPool_create();

// 启用倍数模式：8, 16, 32, 64, 128, 256...
XMultiPool_enable_power_of_two_mode(mp, 8, 2);

// 添加子池
XFixedPool* pool8 = XFixedPool_create(8, 100);
XFixedPool* pool16 = XFixedPool_create(16, 100);
XFixedPool* pool32 = XFixedPool_create(32, 50);
XMultiPool_add_pool(mp, pool8);
XMultiPool_add_pool(mp, pool16);
XMultiPool_add_pool(mp, pool32);

// 分配内存
void* p1 = XMultiPool_malloc(mp, 5);   // 从pool8分配
void* p2 = XMultiPool_malloc(mp, 20);  // 从pool32分配

// 释放内存
XMultiPool_free(mp, p1);
XMultiPool_free(mp, p2);

// 销毁
XMultiPool_delete(mp);
```

### 示例：使用全局内存池

```c
// 使用全局内存池
void* ptr = XMultiPool_global_malloc(64);
// ... 使用内存 ...
XMultiPool_global_free(ptr);
```

---

## 字节序处理

XMemory模块提供了跨平台的字节序转换功能。

### 字节序枚举

```c
typedef enum {
    XBYTE_ORDER_LITTLE_ENDIAN = 0,  // 小端序：低字节在前
    XBYTE_ORDER_BIG_ENDIAN,         // 大端序：高字节在前
    XBYTE_ORDER_NATIVE              // 本机字节序
} XByteOrder;
```

### 比特序枚举

```c
typedef enum {
    XBIT_ORDER_MSB_FIRST = 0,  // 高位在前
    XBIT_ORDER_LSB_FIRST,      // 低位在前
    XBIT_ORDER_DEFAULT         // 默认使用LSB_FIRST
} XBitOrder;
```

### 字节序转换函数

#### XMemory_read_data

```c
bool XMemory_read_data(const uint8_t* src, XByteOrder readOrder, uint8_t* out, size_t size)
```

从字节流读取数据并根据字节序转换。

**参数:**
- `src` - 输入数据源指针
- `readOrder` - 输入数据的字节序
- `out` - 输出内存缓冲区
- `size` - 数据长度(字节)

**返回值:** 成功返回true，参数无效返回false

---

#### XMemory_write_data

```c
bool XMemory_write_data(uint8_t* write, XByteOrder writeOrder, const uint8_t* in, size_t size)
```

将内存数据按指定字节序写入数据流。

**参数:**
- `write` - 输出数据流指针
- `writeOrder` - 输出数据的字节序
- `in` - 输入内存缓冲区
- `size` - 数据长度(字节)

**返回值:** 成功返回true，参数无效返回false

---

### 便捷宏

#### XMemory_Read_Var

```c
#define XMemory_Read_Var(src, readOrder, varName, varType)
```

从字节流读取数据并转换为指定类型变量。

**参数:**
- `src` - 输入数据源指针
- `readOrder` - 输入数据的字节序
- `varName` - 输出变量名
- `varType` - 输出变量类型

---

#### XMemory_Write_Var

```c
#define XMemory_Write_Var(in, writeOrder, varName, varType)
```

将内存数据转换为指定字节序并写入变量。

---

### 示例：字节序转换

```c
// 从网络数据(大端序)读取uint32_t
uint8_t networkData[4] = {0x12, 0x34, 0x56, 0x78};

// 方法1：使用函数
uint32_t value;
XMemory_read_data(networkData, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&value, sizeof(value));

// 方法2：使用宏
XMemory_Read_Var(networkData, XBYTE_ORDER_BIG_ENDIAN, value2, uint32_t);

// 写入网络数据(大端序)
uint8_t output[4];
uint32_t outValue = 0x12345678;
XMemory_write_data(output, XBYTE_ORDER_BIG_ENDIAN, (uint8_t*)&outValue, sizeof(outValue));
```

---

## 附录

### 内存类型对比

| 内存类型 | 适用场景 | 性能 | 特点 |
|---------|---------|------|------|
| `XMEMORY_TYPE_SYSTEM` | 通用场景 | 一般 | 灵活，无大小限制 |
| `XMEMORY_TYPE_MULTIPOOL` | 频繁分配释放 | 高 | O(1)分配，固定块大小 |
| `XMEMORY_TYPE_HYBRID` | 混合场景 | 中 | 小块用池，大块用系统 |

### 内存池性能特点

| 特性 | XFixedPool | XMultiPool |
|------|-----------|------------|
| 分配时间复杂度 | O(1) | 二分模式O(log N) 倍数模式O(1) |
| 释放时间复杂度 | O(1) | O(1) |
| 线程安全 | 是 | 是 |
| 块大小 | 固定 | 多种 |
| 适用场景 | 单一大小对象 | 多种大小对象 |

### 内存对齐说明

XFixedPool会自动对内存块进行对齐，对齐大小通常为：
- 32位系统：4字节或8字节
- 64位系统：8字节或16字节

对齐可以：
- 提高CPU访问效率
- 避免false sharing(伪共享)
- 优化缓存命中率

### 最佳实践

1. **选择合适的内存类型**
   - 频繁分配释放小对象：使用内存池
   - 大块内存或不确定大小：使用系统内存
   - 混合场景：使用混合模式

2. **内存池配置**
   - 根据实际对象大小配置块大小
   - 根据并发量配置块数量
   - 考虑内存碎片问题

3. **线程安全**
   - XFixedPool和XMultiPool是线程安全的
   - 自定义内存管理器需要考虑线程安全

4. **内存泄漏检测**
   - 开发阶段可使用自定义内存管理器跟踪分配
   - 确保malloc/free配对使用

### 常见错误

| 错误 | 说明 |
|------|------|
| 忘记释放内存 | 导致内存泄漏 |
| 重复释放 | 导致未定义行为 |
| 释放错误指针 | 可能破坏内存池结构 |
| 大小不匹配 | XFixedPool分配时大小固定 |

### API速查表

#### 基础内存操作

| 函数/宏 | 说明 |
|--------|------|
| `XMalloc(size, type)` | 申请内存 |
| `XFree(ptr, type)` | 释放内存 |
| `XRealloc(ptr, size, type)` | 重分配 |
| `XCalloc(count, size, type)` | 零初始化分配 |
| `XNew(type)` | 申请类型对象 |
| `XDelete(ptr)` | 释放对象 |

#### XFixedPool操作

| 函数 | 说明 |
|------|------|
| `XFixedPool_create()` | 创建内存池 |
| `XFixedPool_delete()` | 销毁内存池 |
| `XFixedPool_init()` | 初始化(静态) |
| `XFixedPool_deinit()` | 反初始化 |
| `XFixedPool_malloc()` | 分配块 |
| `XFixedPool_free()` | 释放块 |

#### XMultiPool操作

| 函数 | 说明 |
|------|------|
| `XMultiPool_create()` | 创建多级池 |
| `XMultiPool_delete()` | 销毁多级池 |
| `XMultiPool_add_pool()` | 添加子池 |
| `XMultiPool_malloc()` | 分配内存 |
| `XMultiPool_free()` | 释放内存 |
| `XMultiPool_global()` | 获取全局池 |
