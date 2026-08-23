# TinyUSB 集成说明

## 源码获取

本目录只包含 CMake 构建脚本和配置模板，不包含 TinyUSB 源码。
将 TinyUSB 源码放入 `src/` 目录，有两种方式：

### 方式一：git submodule（推荐）

```bash
git submodule add https://github.com/hathach/tinyusb.git Library/tinyusb/src-tmp
# 然后把 tinyusb 仓库里的 src/ 内容移到 Library/tinyusb/src/
```

或直接让整个 tinyusb 仓库作为子模块：

```bash
git submodule add https://github.com/hathach/tinyusb.git Library/tinyusb/repo
# 修改 CMakeLists.txt，把 src 路径指向 repo/src/
```

### 方式二：直接拷贝

从 https://github.com/hathach/tinyusb 下载 release，
把 `src/` 目录完整拷贝到 `Library/tinyusb/src/`。

最终目录结构应类似：

```
Library/tinyusb/
├── CMakeLists.txt
├── tusb_config.h
├── README.md
└── src/
    ├── tusb.c
    ├── tusb.h
    ├── common/
    ├── device/
    ├── host/
    ├── portable/
    └── osal/
```

## 启用

默认桌面构建不启用 TinyUSB。嵌入式构建时，在 CMake 配置中添加：

```cmake
set(XINYUE_C_HAS_TINYUSB ON)
set(TINYUSB_DEVICE ON)       # 需要 Device 栈
set(TINYUSB_HOST ON)         # 需要 Host 栈
set(TINYUSB_OS "none")       # 裸机 / freertos / rtthread

# 指定硬件驱动源文件（按你的 MCU 选择）
set(TINYUSB_PORT_SRCS
    Library/tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
    # ... 其他 port 文件
)

add_subdirectory(Library/tinyusb)
target_link_libraries(YourTarget PRIVATE tinyusb)
```

## 配置

通过 `tusb_config.h` 裁剪功能和缓冲区大小。
嵌入式项目可以自己提供一份 `tusb_config.h`，放在 include 路径更靠前的位置覆盖默认配置。

## 与 XinYueC USB 框架对接

XinYueC 的 `XDeviceUsbHost` 和 `XDeviceUsbGadget` 公共层通过平台后端接入 TinyUSB：

- `Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_host.c` —— Host 后端（待实现）
- `Drive/TinyUSB/Device/Usb/XDeviceUsb_tinyusb_gadget.c` —— Gadget 后端（待实现）

接入后公共 API（`XDeviceUsbHost_*` / `XDeviceUsbGadget_*`）在嵌入式和桌面平台上保持一致。
