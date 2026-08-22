# Drive 驱动目录

驱动实现按平台和职责分层，公共抽象仍位于 `Src`，平台 API 只允许出现在本目录。

## 平台目录

- `Posix`：Linux/POSIX 驱动。
- `windows`：Win32 驱动。
- `FreeRTOS`：FreeRTOS 驱动。
- `STM32`：STM32 外设驱动。
- `Unsupported`：无对应平台能力时的安全存根。
- `Compiler`：编译器相关实现（GCC、Keil、MSVC）。

## 平台内职责目录

- `Core`：字符、日期时间、进程、随机数、系统以及通用异步 I/O 事件环等基础驱动。
- `Device`：文件、网络设备、GPIO、串口等设备驱动；设备族可继续分目录。
- `Graphics`：原生窗口、后备存储、GPU、拖放、主题、无障碍等图形驱动。
- `Sync`：互斥量、信号量、线程和等待条件。

CMake 使用 `Drive/*.c` 递归收集，因此新增驱动只需放入对应平台/职责目录，
不需要额外修改构建文件。
