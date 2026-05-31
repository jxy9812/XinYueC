

# XinYueC - 纯 C 语言面向对象库

[![许可证](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

------

## 目录

- [基本介绍](#基本介绍)
- [核心特性](#核心特性)
- [项目结构](#项目结构)
- [📚 API 文档](#-api-文档)
- [快速开始](#快速开始)
- [许可证](#许可证)

------

## 基本介绍

XinYueC 是一个使用纯 C 语言实现的、功能丰富的面向对象库。它旨在为 C 语言开发者提供一套强大、高效且易于使用的泛型容器、算法和跨平台接口，以弥补 C 语言在现代软件开发中的一些不足。

本项目最初是为了锻炼编程思维而创建，现已发展成为一个可用于实际项目的成熟库。其设计哲学深受 C++ STL 的影响，容器的成员函数命名尽可能与 C++ 保持一致，以降低学习成本。

### 核心特性

- **纯 C 实现**: 不依赖任何第三方库，仅使用标准 C 库。
- **泛型容器**: 提供多种数据结构（如链表、动态数组、映射等），支持任意数据类型。
- **面向对象风格**: 通过结构体和函数指针模拟类、继承和多态。
- **跨平台**: 提供了针对不同操作系统的抽象层，确保代码可移植性。
- **内存安全**: 容器支持自定义的元素拷贝、移动和析构回调，有效管理复杂对象的生命周期。

### 许可证

本项目采用 **MIT 开源许可协议**。

------

## 项目结构

```
XinYueC/
├── Src/                      # 源代码目录
│   ├── XClass/               # 面向对象基础模块
│   ├── XCode/                # 核心代码模块
│   ├── XContainer/           # 容器模块
│   ├── XData/                # 数据处理模块
│   ├── XEvent/              # 事件系统模块
│   ├── XMemory/             # 内存管理模块
│   └── XTimer/              # 定时器模块
├── Drive/                   # 平台驱动实现
│   ├── msvc/                # MSVC编译器实现（原子操作）
│   ├── gcc/                 # GCC编译器实现（原子操作）
│   ├── windows/             # Windows平台实现
│   ├── Posix/              # POSIX平台实现
│   ├── FreeRTOS/           # FreeRTOS实时操作系统实现
│   ├── keil/               # Keil编译器实现
│   └── STM32/              # STM32单片机实现
├── Test/                    # 测试代码目录
│   ├── XMenuTest.c/h       # 菜单测试框架
│   ├── XCodeTest/          # XCode模块测试
│   ├── XContainerTest/    # XContainer模块测试
│   ├── XTimerTest/         # XTimer模块测试
│   └── ...
├── CMakeLists.txt         # CMake配置文件
└── README.md               # 项目说明文档
```

> ⚠️ **平台支持说明**：因一人精力有限，目前完整的实现只有 **Windows 平台**。其他平台（Linux、macOS、FreeRTOS、STM32等）的部分功能可能尚未完成或未经充分测试，欢迎贡献代码！

------

## 📚 API 文档

XinYueC 由多个独立模块组成，每个模块都有详细的文档说明。

| 模块 | 说明 |
|------|------|
| **XClass** | 面向对象基础（类、继承、多态、虚函数表） |
| **XCode** | 核心代码（原子操作、信号槽、线程、线程池、同步原语） |
| **XContainer** | 泛型容器（Vector、List、Map、HashMap、Set等） |
| **XData** | 数据处理（String、ByteArray、DateTime、JSON、BSON） |
| **XEvent** | 事件系统（Event、EventLoop、EventDispatcher、XObject） |
| **XMemory** | 内存管理（内存池、智能指针、分配器） |
| **XTimer** | 定时器（单次定时器、周期定时器、高精度定时器） |

### XClass: 面向对象的基石

XClass 是所有类的基类，实现了 C 语言中的"类"、"继承"和"多态"。

**核心 API:**
- `XClass_init(self)`: 初始化实例
- `XClassGetVtable(self)`: 获取虚函数表指针
- `XClassGetVirtualFunc(self, func_index, func_type)`: 通过虚函数表索引调用虚函数

### XObject: 事件驱动的核心

XObject 是所有可以参与事件循环和信号槽通信的对象的基类。

**核心 API:**
- `XObject_event(self, event)`: 对象的事件处理器（虚函数）
- `XObject_moveToThread(object, target_thread)`: 将对象移动到指定线程
- `xobject_emit_signal(sender, signal_index, ...)`: 发射信号

### 容器系统

所有容器都继承自 `XContainer` 基类，支持泛型操作。

**主要容器：**
- **XVector**: 动态数组
- **XList**: 双向链表
- **XMap**: 红黑树映射
- **XHashMap**: 哈希映射
- **XSet**: 集合
- **XStack**: 栈
- **XQueue**: 队列

### 事件循环与信号槽系统

**核心组件：**
1. **XEvent**: 事件基类
2. **XEventLoop**: 事件循环管理者
3. **XSignalSlot**: 信号槽连接管理器
4. **XThread**: 线程封装

------

## 快速开始

### CMake 集成

```cmake
cmake_minimum_required(VERSION 3.5)
project(YourProject VERSION 0.1 LANGUAGES C)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 查找源文件和头文件
file(GLOB_RECURSE SOURCE_FILES "SourceFile/*.c" "SourceFile/*.h")
include_directories(${INCLUDE_DIR_LIST})

add_executable(${PROJECT_NAME} ${SOURCE_FILES})
```

### 运行测试

项目提供了交互式菜单测试系统：

1. 编译项目后运行可执行文件
2. 程序启动后会显示交互式菜单
3. 通过输入数字选择要测试的模块
4. 测试完成后自动返回上级菜单

------

## 许可证

```
MIT License

Copyright (c) 2024 XinYueC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

------

## 贡献

欢迎提交 Issue 和 Pull Request！