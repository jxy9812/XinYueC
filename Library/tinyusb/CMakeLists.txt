# ==================== TinyUSB（嵌入式 USB 协议栈）====================
# 开源轻量级 USB Host/Device 协议栈，面向 MCU/嵌入式环境。
# 源码位置：Library/tinyusb/src/（已从 tinyusb-0.21.0 解压）
# 配置文件：tusb_config.h（在本目录，按项目需求裁剪）
#
# 默认不编译（桌面平台用不到），仅在定义 XINYUE_C_HAS_TINYUSB 时启用。
# 需要同时指定 TINYUSB_PORT_DIRS（硬件驱动目录，相对于 src/portable/）。

if(NOT DEFINED XINYUE_C_HAS_TINYUSB)
    # 桌面构建默认不启用 TinyUSB
    return()
endif()

# 检查源码是否存在
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/tusb.c")
    message(FATAL_ERROR "TinyUSB source not found at Library/tinyusb/src/. "
                        "Extract TinyUSB 0.21.0 src/ into Library/tinyusb/src/.")
endif()

# --- 公开头文件路径 ---
set(TINYUSB_PUBLIC_INCS
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# --- 核心源文件 ---
set(TINYUSB_SRCS
    src/tusb.c
    src/common/tusb_fifo.c
)

# --- Device 栈 ---
if(DEFINED TINYUSB_DEVICE AND TINYUSB_DEVICE)
    list(APPEND TINYUSB_SRCS
        src/device/usbd.c
        src/device/usbd_control.c
    )

    # 类驱动：只编译配置中启用的
    if(DEFINED TINYUSB_CLASS_CDC AND TINYUSB_CLASS_CDC)
        list(APPEND TINYUSB_SRCS src/class/cdc/cdc_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_MSC AND TINYUSB_CLASS_MSC)
        list(APPEND TINYUSB_SRCS src/class/msc/msc_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_HID AND TINYUSB_CLASS_HID)
        list(APPEND TINYUSB_SRCS src/class/hid/hid_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_DFU AND TINYUSB_CLASS_DFU)
        list(APPEND TINYUSB_SRCS
            src/class/dfu/dfu_device.c
            src/class/dfu/dfu_rt_device.c
        )
    endif()
    if(DEFINED TINYUSB_CLASS_MIDI AND TINYUSB_CLASS_MIDI)
        list(APPEND TINYUSB_SRCS
            src/class/midi/midi_device.c
            src/class/midi/midi2_device.c
        )
    endif()
    if(DEFINED TINYUSB_CLASS_AUDIO AND TINYUSB_CLASS_AUDIO)
        list(APPEND TINYUSB_SRCS src/class/audio/audio_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_VIDEO AND TINYUSB_CLASS_VIDEO)
        list(APPEND TINYUSB_SRCS src/class/video/video_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_NET AND TINYUSB_CLASS_NET)
        list(APPEND TINYUSB_SRCS
            src/class/net/ecm_rndis_device.c
            src/class/net/ncm_device.c
        )
    endif()
    if(DEFINED TINYUSB_CLASS_BTH AND TINYUSB_CLASS_BTH)
        list(APPEND TINYUSB_SRCS src/class/bth/bth_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_MTP AND TINYUSB_CLASS_MTP)
        list(APPEND TINYUSB_SRCS src/class/mtp/mtp_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_PRINTER AND TINYUSB_CLASS_PRINTER)
        list(APPEND TINYUSB_SRCS src/class/printer/printer_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_USBTMC AND TINYUSB_CLASS_USBTMC)
        list(APPEND TINYUSB_SRCS src/class/usbtmc/usbtmc_device.c)
    endif()
    if(DEFINED TINYUSB_CLASS_VENDOR AND TINYUSB_CLASS_VENDOR)
        list(APPEND TINYUSB_SRCS src/class/vendor/vendor_device.c)
    endif()
endif()

# --- Host 栈 ---
if(DEFINED TINYUSB_HOST AND TINYUSB_HOST)
    list(APPEND TINYUSB_SRCS
        src/host/usbh.c
        src/host/hub.c
    )

    if(DEFINED TINYUSB_CLASS_CDC AND TINYUSB_CLASS_CDC)
        list(APPEND TINYUSB_SRCS src/class/cdc/cdc_host.c)
    endif()
    if(DEFINED TINYUSB_CLASS_MSC AND TINYUSB_CLASS_MSC)
        list(APPEND TINYUSB_SRCS src/class/msc/msc_host.c)
    endif()
    if(DEFINED TINYUSB_CLASS_HID AND TINYUSB_CLASS_HID)
        list(APPEND TINYUSB_SRCS src/class/hid/hid_host.c)
    endif()
    if(DEFINED TINYUSB_CLASS_MIDI AND TINYUSB_CLASS_MIDI)
        list(APPEND TINYUSB_SRCS
            src/class/midi/midi_host.c
            src/class/midi/midi2_host.c
        )
    endif()
    if(DEFINED TINYUSB_CLASS_VENDOR AND TINYUSB_CLASS_VENDOR)
        list(APPEND TINYUSB_SRCS src/class/vendor/vendor_host.c)
    endif()
endif()

# --- OS 抽象层 ---
if(DEFINED TINYUSB_OS)
    if(TINYUSB_OS STREQUAL "none")
        list(APPEND TINYUSB_SRCS src/osal/osal_none.c)
    elseif(TINYUSB_OS STREQUAL "freertos")
        list(APPEND TINYUSB_SRCS src/osal/osal_freertos.c)
    elseif(TINYUSB_OS STREQUAL "rtthread")
        list(APPEND TINYUSB_SRCS src/osal/osal_rtthread.c)
    endif()
endif()

# --- 硬件驱动层（portable）---
# 由调用方通过 TINYUSB_PORT_DIRS 指定硬件驱动目录（相对于 src/portable/）
# 例如 set(TINYUSB_PORT_DIRS "st/stm32_fsdev" "synopsys/dwc2")
if(DEFINED TINYUSB_PORT_DIRS)
    foreach(port_dir ${TINYUSB_PORT_DIRS})
        file(GLOB port_srcs "${CMAKE_CURRENT_SOURCE_DIR}/src/portable/${port_dir}/*.c")
        list(APPEND TINYUSB_SRCS ${port_srcs})
    endforeach()
endif()

# --- 过滤不存在的文件（防配置错误）---
set(TINYUSB_VALID_SRCS "")
foreach(src ${TINYUSB_SRCS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        list(APPEND TINYUSB_VALID_SRCS "${src}")
    endif()
endforeach()

# --- 创建静态库 ---
add_library(tinyusb STATIC ${TINYUSB_VALID_SRCS})
set_target_properties(tinyusb PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- 头文件路径 ---
target_include_directories(tinyusb PUBLIC ${TINYUSB_PUBLIC_INCS})


# --- XinYueC 时间对接 ---
# 启用后用 XDateTime 提供 TinyUSB 的毫秒时间基准。
# 注意：XDateTime 是墙上时间，不是严格单调时间。
# 无 NTP 校时的嵌入式场景可用，有 NTP 的系统建议用单调时钟。
if(DEFINED TINYUSB_USE_XINYUE_DATETIME AND TINYUSB_USE_XINYUE_DATETIME)
    target_sources(tinyusb PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tusb_time_xinyue.c")
    target_compile_definitions(tinyusb PUBLIC CFG_TUSB_OSAL_TIME_XINYUE=1)
endif()

# --- 功能宏定义（由 tusb_config.h 控制，这里只传开关）---
if(DEFINED TINYUSB_DEVICE AND TINYUSB_DEVICE)
    target_compile_definitions(tinyusb PUBLIC CFG_TUD_ENABLED=1)
else()
    target_compile_definitions(tinyusb PUBLIC CFG_TUD_ENABLED=0)
endif()
if(DEFINED TINYUSB_HOST AND TINYUSB_HOST)
    target_compile_definitions(tinyusb PUBLIC CFG_TUH_ENABLED=1)
else()
    target_compile_definitions(tinyusb PUBLIC CFG_TUH_ENABLED=0)
endif()

# --- 内存管理说明 ---
# TinyUSB 0.21.0 为纯静态分配，全程不使用 malloc/free，
# 因此无需接入 XinYueC 的 XMemory 内存管理。
# 所有缓冲区大小由 tusb_config.h 中的宏在编译时确定。
