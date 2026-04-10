

# XinYueC (欣悦C)

一个使用纯C语言实现的泛型容器和算法库。

## 项目简介

本项目使用**纯C语言**实现了各种容器和算法，**均为泛型实现**。初衷是锻炼编程思维，同时为C语言提供一套完整的容器库以便日后使用。容器成员函数名尽可能与C++保持一致，便于快速上手。

**注意**：使用此代码需要对**指针**有一定的掌握，有时会用到二级指针，但一级指针必须熟练使用。

## 项目分支

| 分支 | 说明 |
|------|------|
| [master](https://gitee.com/xin___yue/c-language-container/tree/master) | 稳定版分支 |
| [develop](https://gitee.com/xin___yue/c-language-container/tree/develop) | 开发版分支 |
| [new](https://gitee.com/xin___yue/c-language-container/tree/new) | 最新重构分支 |

## 主要特性

- ✅ **纯C实现** - 无任何C++依赖
- ✅ **泛型容器** - 支持任意数据类型
- ✅ **C++风格API** - 命名规范与现代容器库一致
- ✅ **跨平台支持** - 支持Windows、Linux、嵌入式系统
- ✅ **丰富算法** - 排序、查找、树、迷宫等

## 容器

### 基础容器

| 容器 | 说明 |
|------|------|
| XContainerObject | 所有容器的基类 |
| XList | 双向链表 |
| XListSLinked | 单向链表 |
| XListSLinkedAtomic | 原子操作单向链表 |
| XVector | 动态数组 |
| XString | 字符串 |
| XStack | 栈 |
| XQueue | 队列 |
| XCircularQueue | 循环队列 |
| XPriorityQueue | 优先队列 |
| XPriorityMapQueue | 优先级映射队列 |

### 关联容器

| 容器 | 说明 |
|------|------|
| XMap | 映射 |
| XHashMap | 哈希映射 |
| XSet | 集合 |
| XHashSet | 哈希集合 |

### 特殊容器

| 容器 | 说明 |
|------|------|
| XBitArray | 位数组 |
| XByteArray | 字节数组 |
| XStringList | 字符串列表 |
| XVariantList | 变体列表 |

## 算法

### 排序算法

- 直接插入排序
- 希尔排序
- 直接选择排序
- 堆排序
- 冒泡排序
- 快速排序
- 归并排序
- 随机打乱顺序

### 查找算法

- 二分查找

### 树结构

- XBinaryTree - 二叉树
- XBalancedBinaryTree - 平衡二叉树
- XRedBlackTree - 红黑树
- XTwoThreeTree - 2-3树
- XHuffmanTree - 哈夫曼树
- XHierarchicalTree - 层级树

### 迷宫算法

- 迷宫生成（深度优先）
- 迷宫寻路
  - 深度优先寻路
  - 广度优先寻路
  - A*寻路算法

## 数据结构

### JSON/BSON

- XJsonDocument
- XJsonArray
- XJsonObject
- XJsonValue
- XBsonDocument
- XBsonArray
- XBsonValue

### 其他数据类型

- XChar - 字符处理
- XVariant - 变体类型
- XPair - 键值对
- XPoint - 坐标点

## 核心框架

### 事件系统

| 组件 | 说明 |
|------|------|
| XObject | 基础对象类 |
| XSignalSlot | 信号槽机制 |
| XEvent | 事件 |
| XEventLoop | 事件循环 |
| XEventDispatcher | 事件分发器 |
| XStateMachine | 状态机 |

### 线程与同步

| 组件 | 说明 |
|------|------|
| XThread | 线程 |
| XMutex | 互斥锁 |
| XRecursiveMutex | 递归互斥锁 |
| XReadWriteLock | 读写锁 |
| XSemaphore | 信号量 |
| XWaitCondition | 等待条件 |

### 定时器

| 组件 | 说明 |
|------|------|
| XTimer | 定时器 |
| XTimerBase | 定时器基类 |
| XTimerGroup | 定时器组 |
| XTimerGroupWheel | 时间轮定时器 |

## 通信协议

| 协议 | 说明 |
|------|------|
| XSerialPort | 串口通信 |
| XSocket | 网络套接字 |
| XModbus | Modbus协议 |
| XDataFrameComm | 数据帧通信 |
| XTJCHMIComm | 触摸屏通信 |
| XCommunicatorBase | 通信基类 |
| XPLC | PLC通信 |

## IO设备

| 设备 | 说明 |
|------|------|
| XIODeviceBase | IO设备基类 |
| XSerialPort | 串口 |
| XSocket | 网络 |
| XPWMDevice | PWM设备 |
| XStepMotor | 步进电机 |
| XSwitchDevice | 开关设备 |
| XESP8266 | WiFi模块 |

## 快速开始

### 使用Visual Studio克隆

1. 选择对应分支
2. 复制HTTPS地址
3. 在VS中打开 "克隆储存库"
4. 填入仓库位置和本地路径
5. 点击克隆

### CMake构建

在当前工程文件夹下新建`SourceFile`文件夹，将`.c`和`.h`文件放入该文件夹中（可嵌套子文件夹），每次增删文件后右键CMakeLists.txt重新配置。

```cmake
cmake_minimum_required(VERSION 3.5)
project(Container VERSION 0.1 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
```

### 链表使用示例

```c
#include "XList.h"
#include "XEquality.h"
#include "XCompare.h"
#include