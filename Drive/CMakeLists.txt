# ==================== Drive（平台组合模块 = 主库 XinYueCS/XinYueC） ====================
# 模块只含自己的头文件与源文件（2026-11-01 模块化）：主库双目标的源集 =
# 本目录平台源树（Posix/Windows/Android/FreeRTOS/STM32/TinyUSB 移植层）；
# 核心框架经 XinYueCore（Src/XCore 模块）挂接、GUI 经 XGuiLayer（Src/XGui
# 模块，根定义胶水）挂接——消费目标链接 ${EXPORT_NAME}S 即随链获得全部
# 组成（链接行零改动）。
# 平台/第三方系统库链接（X11/DBus/libusb/Vulkan/Win32/Android/armel
# atomic/freetype/mbedtls/...）统一在根落地——跨目录改本模块目标经
# CMP0079 NEW（单条精确开启，见根 CMakeLists）。
#
# glob 纪律（主库同款）：file(GLOB_RECURSE) 配置期固化，新增/删除源文件后
# 须触发重新配置（改动本文件即可）。
# CXinYueConfig.h 居 ../Src 根：强制包含走根函数（绝对路径）。

# ---- 头文件目录自动采集（本模块自管 + 抬升根作用域）----
FIND_INCLUDE_DIR(INCLUDE_DRIVE_LIST ".")
set(INCLUDE_DRIVE_LIST "${INCLUDE_DRIVE_LIST}" PARENT_SCOPE)
# 本目录由根在 XCore 之后接线，目录级快照须自给。
include_directories(${INCLUDE_DRIVE_LIST})

# ---- 源文件自动搜集 ----
file(GLOB_RECURSE DRIVE_FILE "${CMAKE_CURRENT_LIST_DIR}/*.c")
# 桌面（非交叉）构建排除嵌入式移植层（2026-10-05 回归门修复）：FreeRTOS
# portmacro.h/tasks.c 含 ARM CMSIS 内联汇编（mrs/msr basepri、isb），x86
# cc1 直接汇编报错；STM32 cmsis 头无器件宏同样 #error。历史上门禁 build/
# 目录的 glob 快照未含这两个目录（配置早于其入库），全量重配置即暴露。
# 交叉构建同理排除（perf9 上屏部署修复）：非 FreeRTOS 平台的交叉构建
# （如 armel/A33）glob 到 ARM_CM4F port 同样汇编失败（msr psp/basepri 为
# Cortex-M 专有寄存器）；仅当未来恢复 XINYUE_PLATFORM_FREERTOS 分支时
# 才放开由其自管源集。
if(NOT CMAKE_CROSSCOMPILING OR NOT XINYUE_PLATFORM_FREERTOS)
    list(FILTER DRIVE_FILE EXCLUDE REGEX ".*/FreeRTOS/.*")
    list(FILTER DRIVE_FILE EXCLUDE REGEX ".*/STM32/.*")
endif()
# Espressif/ESP8266 移植层（2026-10-16 入库）：源文件依赖 ESP-IDF SDK 头
# （c_types.h 等），桌面/MSVC 编译期即缺头报错；其构建入口是 platforms/esp8266/
# 子工程（独立根 CMakeLists + 自带 toolchain），不与本 glob 共存。
list(FILTER DRIVE_FILE EXCLUDE REGEX ".*/Espressif/.*")
# android_main.c 是 demo APK 的 NativeActivity 壳接线（引用 demo 入口
# xgui_window_demo_main，定义 ANativeActivity_onCreate），归 xguidemo
# 目标所有（该目标已显式登记此文件）；留在框架库里会让动态库
# --no-undefined 链接因找不到 demo 入口失败，且与壳目标重复编译。
if(ANDROID)
    list(REMOVE_ITEM DRIVE_FILE "${CMAKE_CURRENT_LIST_DIR}/Android/android_main.c")
endif()

add_library(${EXPORT_NAME}S  STATIC  ${DRIVE_FILE} ${LIBRARY_FILE})
add_library(${EXPORT_NAME}  SHARED  ${DRIVE_FILE} ${LIBRARY_FILE} ${PUBLIC_HDRS})

# 静态库必须 -fPIC：它会被 HighSpeedFilter 的 xlsx_bridge（SHARED）链接，
# 未开 PIC 时 x86-64 报 "relocation R_X86_64_PC32 against symbol 'stderr'
# can not be used when making a shared object"。Src/XCore、Src/XGui、
# Src/Industry 的静态库均已显式开 ON（各自 CMakeLists），本模块是唯一
# 漏项；见 2026-11-11 全量构建门禁的实证（Drive/libXinYueCSd.a 内
# XDateTime_posix.c.o / XDeviceNetwork_posix.c.o 引用 stderr/stdin）。
set_target_properties(${EXPORT_NAME}S PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ESP8266 移植给大量 .h/.c 补了 #if MODULE_ON 文件顶部守卫，开关默认值在
# CXinYueConfig.h（及其拉入的模块 config）。桌面原先没有强制包含，TU 首行
# 的 #if 因宏未定义按 0 求值，整文件被裁空（实测 mbedtls 引用
# XCryptographic 类型失败）。与 ESP8266 的 -include ESP8266Minimal.h 对齐，
# 对核心库目标强制包含 CXinYueConfig.h；第三方纯 C 库不受影响。
# （函数定义在根 CMakeLists——函数只在其定义作用域及子目录可见，Test 模块
# 的可执行目标也要用。）
xinyue_force_include_config(${EXPORT_NAME}S)
xinyue_force_include_config(${EXPORT_NAME})

# ---- 头目录随主库 PUBLIC 传播（本模块自管目录 + Src 根） ----
# Test/Industry/HighSpeedFilter 模块的可执行链接 ${EXPORT_NAME}S 即获得
# Drive 头目录；XCore 头经 XinYueCore 挂接传递。动态库是导出目标
# （install(EXPORT)）：源码树绝对路径必须包 $<BUILD_INTERFACE:>，否则
# generate 期报"prefixed in the source directory"（装出的 Targets.cmake
# 不得泄漏构建机路径；安装态消费者仍走 XinYueCConfig.cmake 的 Src/include
# 口径，与拆分前一致）。
set(XINYUE_LIB_INC_DIRS
    ${INCLUDE_DRIVE_LIST}
    "${CMAKE_CURRENT_LIST_DIR}/..")
target_include_directories(${EXPORT_NAME}S PUBLIC ${XINYUE_LIB_INC_DIRS})
list(TRANSFORM XINYUE_LIB_INC_DIRS PREPEND "\$<BUILD_INTERFACE:")
list(TRANSFORM XINYUE_LIB_INC_DIRS APPEND ">")
target_include_directories(${EXPORT_NAME} PUBLIC ${XINYUE_LIB_INC_DIRS})

# ---- 组合挂接（本模块目标在此目录改，XinYueCore 跨目录经 CMP0079 NEW） ----
# XinYueCS PUBLIC 挂 XinYueCore：消费目标链接 ${EXPORT_NAME}S 即随链获得
# 核心库；XinYueC 动态库 PRIVATE 挂接：原实现核心源码就在动态库里，编入
# .so 保持产物口径（XinYueCore 已开 PIC，XGui 模块同款）。
# 反向边（XinYueCore PUBLIC ${EXPORT_NAME}S）成环：Core 的平台接口与
# Drive 的平台实现互调，CMake 静态库环依赖算法按需在链接行重复两库
# （与测试目标"链接行双列主库"同一 idiom；MSVC 无库序问题）。
target_link_libraries(${EXPORT_NAME}S PUBLIC XinYueCore)
target_link_libraries(${EXPORT_NAME} PRIVATE XinYueCore)
target_link_libraries(XinYueCore PUBLIC ${EXPORT_NAME}S)
# XGuiLayer 预挂（根在 Src/XGui 注册之后定义并接通 XinYueGui；纯目标名
# generate 期全局解析）。XGUI_ON=0/嵌入式：XinYueGui 不建，XGuiLayer 恒
# 存在但为空 INTERFACE，零链接残留。动态库同挂 PRIVATE：原实现 XGui 源码
# 就在 XinYueC 动态库里，保持该产物口径。
target_link_libraries(${EXPORT_NAME}S PUBLIC XGuiLayer)
target_link_libraries(${EXPORT_NAME} PRIVATE XGuiLayer)

# ---- XinYueIndustry 挂接（Src/Industry 模块，2026-11-01 收编进主库组合） ----
# 工业设备层测试去 main 收编进 TEST_FILE 后，主程序可执行需要 Industry 符号
# 与头目录——XGui 同款组合。彼时 Industry 目标未定义（模块在本模块之后接
# 线），纯目标名 generate 期解析；Industry 恒存在（无开关裁剪），无需
# XGuiLayer 式空胶水。动态库 PRIVATE 编入 .so（已开 PIC）；反向边
# （XinYueIndustry PUBLIC ${EXPORT_NAME}S）在 Industry 模块声明。
target_link_libraries(${EXPORT_NAME}S PUBLIC XinYueIndustry)
target_link_libraries(${EXPORT_NAME} PRIVATE XinYueIndustry)

# ==================== 动态库属性 + 动静态库安装 ====================
# install(TARGETS) 只能作用于本目录创建的目标，库的安装随目标落在本模块。
# 公开头 = Src 树全部头文件（XCore + XGui + CXinYueConfig.h，与
# CMake_Install.cmake 的 Src/*.h 安装口径一致）。
file(GLOB_RECURSE XINYUE_PUBLIC_HDRS
    "${CMAKE_CURRENT_LIST_DIR}/../*.h" "${CMAKE_CURRENT_LIST_DIR}/../*.hpp")
# 裸机分支无动态库目标，相关属性/安装整体跳过
if(NOT XINYUE_EMBEDDED)
set_target_properties(${EXPORT_NAME} PROPERTIES
    OUTPUT_NAME ${EXPORT_NAME}
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
    PUBLIC_HEADER "${XINYUE_PUBLIC_HDRS}"
)
#安装动态库（导出集供根 CMake_Install.cmake 的 install(EXPORT) 消费）
install(TARGETS ${EXPORT_NAME}
        EXPORT ${EXPORT_NAME}Targets # 导出
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
 )
endif() # NOT XINYUE_EMBEDDED

# 安装静态库（仅安装，不导出）
install(TARGETS ${EXPORT_NAME}S
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
 )
