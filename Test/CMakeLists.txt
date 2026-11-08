# ==================== Test（测试/工具/demo 可执行模块） ====================
# 自根 CMakeLists.txt 拆出（2026-11-01 模块化拆分续）：TEST_FILE 源清单
# （含自带 main 的独立可执行源剔除清单）、主程序双 exe、全部专项探针/回归
# 测试/demo/工具目标、add_test/CTest 注册、XUpgradePack、xguidemo 安卓 APK
# 壳与安装，全部收进本模块。
#
# 头文件目录自采（模块自己的头文件自己包含）：FIND_INCLUDE_DIR 宏由根
# include(CMake_Config.cmake) 提供。框架头（Src/Drive）经链接图获得——
# Drive 模块对 ${EXPORT_NAME}S target_include_directories PUBLIC 传播；XGui
# 头经 ${EXPORT_NAME}S PUBLIC → XGuiLayer → XinYueGui 链传递；目录级兜底
# 由根在 add_subdirectory(Src/XGui) 后 include_directories 落地。
#
# enable_testing() 留在根 CMakeLists（保证顶层 ctest 可见全部用例），
# add_test 在本模块注册。
#
# 依赖宿主变量：EXPORT_NAME、XINYUE_UNIX_THREAD_LIBS、
# XINYUE_UNIX_EXTRA_LIBS、X11_FOUND/X11_LIBRARIES（根探测段）、
# xinyue_force_include_config（根函数，XCore/Drive 模块同款调用）。

# ---- 头文件目录自动采集（本模块自管，原根 INCLUDE_TEST_LIST 迁入）----
FIND_INCLUDE_DIR(INCLUDE_TEST_LIST ".")
include_directories(${INCLUDE_TEST_LIST})

# [2026-11-04] HSF（高速过滤器）模块头目录：测试侧直接实例化 HsfLinkClient /
# HsfFlowMeterProvider（客户端侧）与 HsfServerIoFlowMeter（服务端侧），而这些
# 源不在主库（只随 HsfClient / XHsfServerIoLib 编译）。目录级引入一次，
# 本模块全部目标通用，避免逐目标接线遗漏（首个遗漏即 HsfVfdProbe 找不到
# XhsfLinkProto.h、XGuiTestMenuSweep 找不到 HsfFlowMeterProvider.h）。
include_directories(
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/common"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui/pages"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/server"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/server/io")

# ---- TEST_FILE 收集 + 自带 main 的独立可执行源剔除 ----
# ⚠ TEST_FILE 剔除三查纪律：新增"自带 main 的独立可执行"时必须同步加
#   REMOVE_ITEM，否则 XinYueC_Static/_Dynamic 与 demo 测试树收编
#   （XGUI_SWEEP_TESTTREE_SOURCES / XGUI_DEMO_TESTTREE_SOURCES）链接失败
#   （历史上同一坑已踩六次，见下方各条目注释）。
# XGuiDemo 是自带 main() 的独立演示可执行（XGuiWindowDemo_Test 目标），
# 不能被卷进 Static/Dynamic 主程序（会与 main.c 重复定义 main）。
file(GLOB_RECURSE TEST_FILE "${CMAKE_CURRENT_LIST_DIR}/*.c" )
file(GLOB_RECURSE XGUI_DEMO_FILE "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/*.c" )
if(XGUI_DEMO_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_DEMO_FILE})
endif()
# XGuiApiTest 同理：自带 main 的独立 API 测试套件（XGuiApi_Test 目标，
# 接线见文件尾注释），不得卷入主程序。
file(GLOB_RECURSE XGUI_API_TEST_FILE "${CMAKE_CURRENT_LIST_DIR}/XGuiApiTest/*.c" )
if(XGUI_API_TEST_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_API_TEST_FILE})
endif()
# xgui_font_scan_tool 同理：自带 main 的独立离屏字库巡检工具（
# xgui_font_scan_tool 目标），与主程序 main.c 双 main 冲突，必须剔除。
file(GLOB_RECURSE XGUI_FONT_SCAN_TOOL_FILE "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xgui_font_scan_tool.c" )
if(XGUI_FONT_SCAN_TOOL_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_FONT_SCAN_TOOL_FILE})
endif()
# xgui_glyph_coverage_diff_tool 同理：自带 main 的 P2.5 门禁独立工具
# （xgui_glyph_coverage_diff_tool 目标），与 main.c 双 main 冲突，必须剔除。
file(GLOB_RECURSE XGUI_COVDIFF_TOOL_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xgui_glyph_coverage_diff_tool.c" )
if(XGUI_COVDIFF_TOOL_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_COVDIFF_TOOL_FILE})
endif()
# XGuiRemoteAuthTest 同理：自带 main 的访问口令认证矩阵测试（
# XGuiRemoteAuth_Test 目标），不得卷入主程序。
file(GLOB_RECURSE XGUI_REMOTE_AUTH_TEST_FILE "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteAuthTest.c" )
if(XGUI_REMOTE_AUTH_TEST_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_REMOTE_AUTH_TEST_FILE})
endif()
# x_timer_coarse_repro 同理：自带 main 的 CoarseTimer（时间轮）最小复现
# 探针（2026-10-22，定位 ESP8266 上框架定时器路径崩溃），与主程序双
# main 冲突，必须剔除。
file(GLOB_RECURSE XGUI_COARSE_REPRO_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/x_timer_coarse_repro.c" )
if(XGUI_COARSE_REPRO_FILE)
    list(REMOVE_ITEM TEST_FILE ${XGUI_COARSE_REPRO_FILE})
endif()
# XGuiRemoteInputViewTest 同理：自带 main 的独立专项探针
# （XGuiRemoteInputView_Test 目标，mcgs 战役输入路 2026-10-04），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteInputViewTest.c")
# XWidgetApiTest 同理：自带 main 的 XWidget 公开 API 契约回归
# （XWidgetApi_Test 目标，test_gap#0；Qt 6.8 对齐改动的 API 护栏），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XWidgetApiTest.c")
# XGuiDialogMoveTest 同理：自带 main 的 fbdev 弹层拖动条带归位离屏测试
# （XGuiDialogMove_Test 目标，对话框拖动白块根修 2026-10-05 的回归锁；
# 须 -DXPLATFORM_FBDEV_ON=ON 构建才激活，见文件头注），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiDialogMoveTest.c")
# XGuiMirrorFidelityTest 同理：自带 main 的 fbdev 镜像保真回环离屏测试
# （XGuiMirrorFidelity_Test 目标，perf9 路2 白块时序口/换页脏区完整性/
#  热切换/FB_REQUEST 首帧实效的回归锁；须 -DXPLATFORM_FBDEV_ON=ON 构建
#  才激活，见文件头注），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiMirrorFidelityTest.c")
# XGuiRemoteAdaptTest 同理：自带 main 的 Xvfb 会话直跑测试（XGuiRemoteAdapt_
# Test 目标，perf9 路4 自适应降档阶梯单元+回环量化；见文件头注），不得
# 卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteAdaptTest.c")
# XGuiDockDragTest 同理：自带 main 的停靠面板拖出浮动让位区残留离屏
# 测试（XGuiDockDrag_Test 目标，2026-10-31 昆仑通态屏「浮动面板拖动
# 旧位残留」根修的回归锁；须 -DXPLATFORM_FBDEV_ON=ON 构建才激活，见
# 文件头注），不得卷入主程序（TEST_FILE 剔除三查同款坑）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiDockDragTest.c")
# XGuiKeyboardDragTest 同理：自带 main 的键盘紧凑拖动专项测试（
# XGuiKeyboardDrag_Test 目标，1eb98412 键盘紧凑拖动战役新增；当时漏加
# 本剔除致 XinYueC_Static/Dynamic 与 main.c 重复定义 main、all 目标
# 链接失败），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiKeyboardDragTest.c")
# bold_ab_tool 同理：自带 main 的粗体真字体 A/B 离屏验证（xgui_font_bold_ab
# 目标，2026-10-08 字重变体战役新增；当时漏加本剔除致 all 目标再次断链
# ——同一坑第二次踩， TEST_FILE 剔除三查已写入构建协议），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/bold_ab_tool.c")
# xntp_min_repro 同理：自带 main 的 XNtp 最小复现独立可执行
# （XNtpMinRepro_Test 目标，2026-10-22 NTP 应答超时根修车道诊断工具），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xntp_min_repro.c")
# xhsf_codec_test 同理：HSF 网络编解码专项单测自带 main（XHsfCodec_Test
# 目标），不得卷入主程序（剔除三查纪律）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xhsf_codec_test.c")
# xhsf_link_probe 同理：联调探针自带 main（XHsfLinkProbe 目标）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xhsf_link_probe.c")
# xmodbus_android_probe 同理：自带 main 的安卓真机 Modbus TCP 主站轮询
# 探针独立可执行（XModbusAndroidProbe 目标，2026-10-25 安卓 seccomp 拒
# io_uring_setup 根修车道诊断工具），不得卷入主程序（与 main.c 重复定义
# main）——TEST_FILE 剔除三查同款坑第五次。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xmodbus_android_probe.c")
# xfdv_m512_rtu_test 同理：自带 main 的**真机** Modbus RTU 闭环探针独立可执行
# （XModbusM512RtuProbe 目标，2026-11-03 512M 变频器真机联调：CH343 USB-485
# + COM17 @115200 8N1 + 站号 2），不得卷入主程序（与 main.c 重复定义 main）
# ——TEST_FILE 剔除三查同款坑。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xfdv_m512_rtu_test.c")
# hsf_vfd_probe 同理：自带 main 的 HSF 变频器页交互回归探针独立可执行
# （HsfVfdProbe 目标，2026-11-04 变频器页全量回归：单 TU 收录页/模拟源/
# provider 实现以直达内部），不得卷入主程序（与 main.c 重复定义 main）
# ——TEST_FILE 剔除三查同款坑。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/hsf_vfd_probe.c")
# XScrollTouchProbe / XGestureRecognizerTest 同理：自带 main 的独立可执行
# （XScrollTouchProbe_Test / XGestureRecognizer_Test 目标，2026-10-29 触摸
# 跟手滚动与手势识别器战役新增），不得卷入主程序（与 main.c 重复定义
# main）——TEST_FILE 剔除三查同款坑第六次（漏加即 XinYueC_Static/_Dynamic
# 链接失败）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XScrollTouchProbe.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGestureRecognizerTest.c")
# xmodbus_hub_test / xfdv_atv61_test / xfdv_m512_test / xfdv_bus485_test /
# xprotocol_campaign_test 五个闭环测试原先各自带 main、按独立可执行剔除；
# 2026-10-29 去 main 收编进主程序（菜单 + --test/Test 命令分派），随
# TEST_FILE 编译，不再需要剔除（无 main，不会与 main.c 重复定义）。
# XTimerWheelPhaseTest 同理：自带 main 的时间轮「相位落槽」回归测试独立
# 可执行（2026-10-28 顶栏时钟 1s 变 1.8s 根修的回归锁：错相注册 1000/
# 2500ms Coarse 定时器并逐次断言触发间隔），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XTimerTest/XTimerWheelPhaseTest.c")
# XTimeAxisEpochTest 同理：自带 main 的时间轴口径回归（2026-11-05 纪元化
# 战役的回归锁：桌面端毫秒/纳秒双轴同锚 Unix 纪元 + 时间轮冷启动对齐门，
# 防止「零点回到 0 附近」与「冷启动即把全部定时器立即兑现」两类回退），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XTimerTest/XTimeAxisEpochTest.c")
# XPlatformPowerTest 同理：自带 main 的平台电源契约回归测试独立可执行
# （2026-11-02 外部唤醒根修的回归锁：影子态卡在「熄」⇒ 再也无法休眠），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XPlatformPowerTest/XPlatformPowerTest.c")
# XPlatformPowerBoardProbe 同理：自带 main 的板端电源契约探针独立可执行
# （真实驱动路径的 A/B 实证工具），不得卷入主程序。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XPlatformPowerTest/XPlatformPowerBoardProbe.c")
# xgui_dialog_portrait_probe 同理：自带 main 的对话框族竖屏适配探针
# （XDialogPortraitProbe 目标，2026-10-26 对话框竖屏适配战役目验工具）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_dialog_portrait_probe.c")
# xdevice_wifi_probe 同理：自带 main 的无线契约探针独立可执行
# （XDeviceWiFiProbe 目标，2026-10-24 无线平台抽象 L0 实测工具），
# 不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XDeviceTest/xdevice_wifi_probe.c")
# xspinbox_geometry_probe 同理：自带 main 的步进列几何四方口径探针
# （XSpinBoxGeometryProbe 目标，2026-10-27 微调框步进列几何单源化战役
# 的可复现证据工具），不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xspinbox_geometry_probe.c")
# xscroll_clip_probe 同理：自带 main 的滚动区子控件越界裁剪探针
# （XScrollClipProbe 目标，2026-10-27 滚动区子控件越界绘制根修的可复现
# 证据工具），不得卷入主程序（与 main.c 重复定义 main）——TEST_FILE 剔除
# 同款坑。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xscroll_clip_probe.c")
# xcharts_suite_probe / xchart_newseries_probe 同理：自带 main 的图表对齐
# 验证探针独立可执行（XChartsSuiteProbe 目标；后者为图表新系列/坐标轴/
# 渐变/图例/极坐标的逐个目验工具，2026-11-03 图表时间轴批次新增），
# 不得卷入主程序（与 main.c 重复定义 main）——TEST_FILE 剔除同款坑。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xcharts_suite_probe.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xchart_newseries_probe.c")
# 高精度定时器组压测原为自带 main 的独立驱动（Test/XTimerTest/
# hr_stress_driver.c），2026-10-25 该文件已删除：压测改从测试菜单进入
# （「定时器 → XHrTimer(高精度定时器组) → 压测(随机增删+句柄回收)」，
# 实现见 Test/XTimerTest/XHrTimerTest.c），随 TEST_FILE 编译进主程序，
# 不再需要剔除（无 main，不会与 main.c 重复定义）。
# xdevice_wifi_posix_parsers_test 同理：自带 main 的 POSIX 无线后端解析器
# 单元测试独立可执行（XDeviceWiFiParsers_Test 目标，2026-10-25 Linux
# 无线适配补齐），不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XDeviceTest/xdevice_wifi_posix_parsers_test.c")
# xdevice_iou_passive_probe 同理：自带 main 的异步后端「被动可读 fd」通道
# 探针独立可执行（XDeviceIoUPassiveProbe 目标，2026-10-26 触摸唤醒通道
# io_uring 侧验证工具），不得卷入主程序（与 main.c 重复定义 main）。
list(REMOVE_ITEM TEST_FILE
    "${CMAKE_CURRENT_LIST_DIR}/XDeviceTest/xdevice_iou_passive_probe.c")
# xprotocol_campaign_test 的旧剔除项随 2026-10-29 去 main 收编一并移除
# （见上方五个闭环测试收编说明）。

# ==================== 静态链接的可执行程序 ====================
# 链接 XinYueCS 静态库，生成的可执行文件不依赖外部动态库（除系统库外）
# XGUI_ON=0（嵌入式全裁剪）时与回归测试同口径排除：demo（main.c）与
# Test/*.c 控件测试源引用 GUI 类型，裁剪构建无法编译。
if(XGUI_ON)
# [2026-11-04 客户端流量计单测] HsfLinkClient / HsfFlowMeterProvider 只随
# HsfClient 可执行编译（不在任何库里），测试要直接实例化它们，故把这两个
# 源一并编进测试目标。它们不依赖 GUI（纯 XLink + BSON），可以安全共用。
set(HSF_CLIENT_TEST_MODULE_SOURCES
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui/HsfLinkClient.c"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui/HsfLinkHistory.c"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui/HsfLinkConfig.c"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui/HsfFlowMeterProvider.c"
)
# 并入 TEST_FILE：凡引用 TEST_FILE 的目标（主程序双 exe / XGuiTestMenuSweep /
# XGuiWindowDemo 测试树）都自动带上这两个实现，避免逐目标重复接线漏掉符号。
list(APPEND TEST_FILE ${HSF_CLIENT_TEST_MODULE_SOURCES})
add_executable(${EXPORT_NAME}_Static
${TEST_FILE}
"${CMAKE_CURRENT_LIST_DIR}/../main.c"
)
set_target_properties(${EXPORT_NAME}_Static PROPERTIES
    OUTPUT_NAME "${EXPORT_NAME}_Static"
)
xinyue_force_include_config(${EXPORT_NAME}_Static)
# Windows(MSVC): 数学函数内置在 C 运行时，线程使用 Drive/windows 的 Win32 实现，无需链接 m/pthread
# Unix(Linux/macOS/BSD): 需链接数学库 m 与线程库 pthread（对应 Drive/Posix 实现，源码已用
#                         __linux__/__APPLE__/__BSD__ 宏守护，仅在这些平台编译）
# 追加 freetype 直链：测试菜单 XFontFtTest.c 在 XFONT_FT_FACE_ON 下直接调用
# FT_* API——共享库 ${EXPORT_NAME} 的 PRIVATE 依赖不进导入库（Windows 导入库
# 不携带 FT_* 符号，动态 exe 链接必缺）；静态库经 LINK_ONLY 本已传递，重复
# 列出无害。
if(UNIX)
    target_link_libraries(${EXPORT_NAME}_Static XHsfServerIoLib ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
else()
    target_link_libraries(${EXPORT_NAME}_Static XHsfServerIoLib ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
endif()
# [2026-11-04 服务端站单测] 测试直接实例化 HsfServerIoFlowMeter（见
# Test/XIndustryTest/hsf_server_io_flowmeter_test.c），需要服务端头目录。
target_include_directories(${EXPORT_NAME}_Static PRIVATE
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/server"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/server/io"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui")

# ==================== 动态链接的可执行程序 ====================
# 链接 XinYueC 动态库，生成的可执行文件运行时需附带 XinYueC.dll（Windows）或 libXinYueC.so（Linux）
add_executable(${EXPORT_NAME}_Dynamic
${TEST_FILE}
"${CMAKE_CURRENT_LIST_DIR}/../main.c"
)
set_target_properties(${EXPORT_NAME}_Dynamic PROPERTIES
    OUTPUT_NAME "${EXPORT_NAME}_Dynamic"
)
xinyue_force_include_config(${EXPORT_NAME}_Dynamic)
if(UNIX)
    target_link_libraries(${EXPORT_NAME}_Dynamic XHsfServerIoLib ${EXPORT_NAME} ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite freetype)
else()
    target_link_libraries(${EXPORT_NAME}_Dynamic XHsfServerIoLib ${EXPORT_NAME} mbedtls lwip zlib fatfs pcre2 sqlite freetype)
endif()
# [2026-11-04] 同 Static 目标：HSF 客户端/服务端头目录（测试直接实例化
# HsfLinkClient / HsfFlowMeterProvider / HsfServerIoFlowMeter）。
target_include_directories(${EXPORT_NAME}_Dynamic PRIVATE
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/server"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/server/io"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui")
endif() # XGUI_ON（demo 可执行排除）

# ==================== XNtp 最小复现程序（独立可执行） ====================
# 「应答超时」根修车道诊断工具：绕开 GUI 直连 ntp.aliyun.com，
# stderr 分阶段探针（DNS/裸 POSIX 对照/XNtpClient 全链路心跳）。
# 仅 UNIX 构建：工具主体是 POSIX 裸套接字对照组（netdb.h/unistd.h 等），
# Windows 下无对应头，2026-10-24 全量构建门修复（此前该目标在 Windows
# 恒编译失败：netdb.h 找不到）。
if(UNIX)
add_executable(XNtpMinRepro_Test
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xntp_min_repro.c"
)
target_link_libraries(XNtpMinRepro_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# HSF 网络编解码专项单测（变频器/电磁阀 ↔ JSON/BSON；HSF 服务端/客户端
# 联调的数据面回归锁）。双平台可构建（纯框架层，无平台依赖）。
add_executable(XHsfCodec_Test
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xhsf_codec_test.c"
)
target_link_libraries(XHsfCodec_Test XinYueIndustry mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
# HSF 服务端联调探针：登录→遥测快照→控制→角色门禁 全链验证（自带 main）。
add_executable(XHsfLinkProbe
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xhsf_link_probe.c"
)
target_include_directories(XHsfLinkProbe PRIVATE
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/common")
target_link_libraries(XHsfLinkProbe XinYueIndustry mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)

# ==================== 无线契约探针（独立可执行） ====================
# 无线平台抽象（L0）实测：枚举无线适配器 → 发起扫描 → 轮询扫描状态机 →
# 取回结果清单 →（可选）连接并回读连接状态。无 GUI、无事件循环依赖，
# 状态机以阻塞轮询驱动（与上层定时器轮询同形）。
add_executable(XDeviceWiFiProbe
    "${CMAKE_CURRENT_LIST_DIR}/XDeviceTest/xdevice_wifi_probe.c"
)
if(UNIX)
    target_link_libraries(XDeviceWiFiProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XDeviceWiFiProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# 异步后端「被动可读 fd」通道探针（独立可执行，2026-10-26 触摸唤醒通道）：
# 验证 Linux 异步后端（XNetIoRingPosix）在 io_uring / epoll 双引擎下的
# 被动 fd 注册—唤醒—重挂—注销链路（fbdev 触摸泵用的正是这条通道）。
# 用法：XDeviceIoUPassiveProbe [epoll|iouring]（不带参数=按引擎默认）。
# [平台门 2026-10-30] 本探针**本质属 POSIX**：源文件直接用 pipe/read/write/
# setenv（unistd.h）+ io_uring/epoll 引擎，Windows 无对应后端 ⇒ 目标只在
# UNIX 下创建。此前它无条件进 Windows 构建 ⇒ 编译必挂 C1083「unistd.h」，
# 是长期存在的 Windows 构建缺口（其 obj 目录一直为空 ⇒ 从未在 Windows
# 上编出来过），本轮收口。
if(UNIX)
    add_executable(XDeviceIoUPassiveProbe
        "${CMAKE_CURRENT_LIST_DIR}/XDeviceTest/xdevice_iou_passive_probe.c"
    )
    target_link_libraries(XDeviceIoUPassiveProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    message(STATUS "XDeviceIoUPassiveProbe：POSIX 专属（pipe/io_uring/epoll），Windows 构建跳过")
endif()

# ==================== 安卓真机 Modbus TCP 主站轮询探针（独立可执行） ====================
# [2026-10-25] 安卓真机（小米 25102RKBEC）上 Modbus 主站连真实远程 IO 设备
# 时 TCP 层 ESTABLISHED 但一个应答都收不回来——安卓 seccomp 拒
# io_uring_setup（EACCES）故须走 epoll 引擎，而桌面走 io_uring，两条路径
# 不同、桌面全绿掩盖了 epoll 分支缺陷。本探针无 GUI，直接驱动库：
# 连接 → 等 connected → 周期发读请求 → 打印每次结果，供复现与回归。
# 用法：./xmodbus_android_probe.exe <host> <port> <unitId> <pointCount>
# [跨平台 2026-10-30] 源文件的计时已从 POSIX 专属的 clock_gettime(CLOCK_
# MONOTONIC) 换成框架统一单调时钟 XDateTime_currentMSecsSinceEpoch()
# ⇒ 本探针在 Windows 上也能编译运行（Windows 侧走 IOCP 后端，本就是
# 「另一条异步路径」的对照组）。此前 Windows 构建因 CLOCK_MONOTONIC
# 未声明而编译必挂（C2065），是长期存在的 Windows 构建缺口，本轮收口。
add_executable(XModbusAndroidProbe
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xmodbus_android_probe.c"
)
if(UNIX)
    target_link_libraries(XModbusAndroidProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XModbusAndroidProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 512M 变频器真机 Modbus RTU 闭环探针（独立可执行） ====================
# [2026-11-03] 用框架自己的 Modbus RTU 栈（XModbusRtuSerialClient → XModbusHub
# → XModbusLink → XVfdM512）连**真实**的 511M/512M/513M 变频器（CH343 USB-485，
# COM17 @115200 8N1，从站地址 2）。与既有的 xfdv_m512_test 的区别：那份的从站是
# 进程内 TCP 仿真器，本探针的从站就是真机（不用仿真器）。
# 用法：./XModbusM512RtuProbe [--port COM17] [--unit 2]
# 安全：电机未接；程序退出前无条件发自由停机（0x2000=5）并回读 0x3000 确认停机。
add_executable(XModbusM512RtuProbe
    "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xfdv_m512_rtu_test.c"
)
if(UNIX)
    target_link_libraries(XModbusM512RtuProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XModbusM512RtuProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== HSF 变频器页交互回归探针（独立可执行） ====================
# [2026-11-04] 变频器监视页（HsfMonitorVfd）全量交互回归：控制闸联动/
# Slider↔SpinBox 双向同步/下发全链路/启停双态/方向下拉/故障复位（确定性
# 注入）/自动运行锁/趋势追点与视口仲裁/形态自适应，关键状态各存 PNG。
# 单 TU 收录 HsfMonitorVfd.c + HsfVfdProvider.c + HsfVfdDemoSim.c（直达
# s_vfdPage 与模拟器内部；勿再把这些源加进本目标——符号会重复）。
# 用法：./HsfVfdProbe （在仓库根运行，截图落在 out/vfdshot_*.png）
add_executable(HsfVfdProbe
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/hsf_vfd_probe.c"
)
target_include_directories(HsfVfdProbe PRIVATE
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/client/ui/pages"
    "${CMAKE_SOURCE_DIR}/HighSpeedFilter/common")
if(UNIX)
    target_link_libraries(HsfVfdProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(HsfVfdProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XSpinBox 步进按钮列几何四方口径探针（独立可执行） ====================
# [2026-10-27] XSpinBox 步进按钮列几何在本库有四处独立实现：样式几何
# （XStyle_subControlRect，Qt 口径）／底层绘制（恒 16px）／QSS 子控件镜像
# （随绘制）／命中判定（恒 16px）。四方不一致会让「箭头压输入框右边框」
# 一类观感问题在改一处、坏另一处。本探针在给定控件宽高下打印四方
# 数值，量化是否一致（诊断用，不参与门禁）。
# 用法：./xspinbox_geometry_probe [width] [height]
add_executable(XSpinBoxGeometryProbe
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xspinbox_geometry_probe.c"
)
if(UNIX)
    target_link_libraries(XSpinBoxGeometryProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XSpinBoxGeometryProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 图表对齐测试套件（独立验证目标） ====================
# 与 xgui_regression_test.c 内的 test_charts_alignment_suite 同源，独立可
# 执行以便在整体回归被他处 WIP 阻塞时仍能验证图表对齐与内存泄漏断言。
add_executable(XChartsSuiteProbe
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xcharts_suite_probe.c"
)
if(UNIX)
    target_link_libraries(XChartsSuiteProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XChartsSuiteProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 图表新系列/坐标轴/渐变/图例/极坐标探针（独立可执行） ====================
# 走 renderToImage（与 paintEvent 同管线）并统计非背景像素，逐个目验
# 箱线图/蜡烛图/日期轴/对数轴/颜色轴/线性渐变/极坐标等新系列的可见性。
# 用法：./xchart_newseries_probe
add_executable(XChartNewSeriesProbe
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xchart_newseries_probe.c"
)
if(UNIX)
    target_link_libraries(XChartNewSeriesProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XChartNewSeriesProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 滚动区子控件越界裁剪探针（独立可执行） ====================
# [2026-10-27 滚动越界根修] 滚动区内控件移出 viewport 边界后未被父容器
# 裁剪（越到品牌条/标题栏上）。本探针复刻结构并打印几何/全局坐标，
# 区分「几何越界」与「绘制未裁剪」（诊断用，不参与门禁）。
# 用法：./xscroll_clip_probe [scrollValue] [winW] [winH]
add_executable(XScrollClipProbe
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xscroll_clip_probe.c"
)
if(UNIX)
    target_link_libraries(XScrollClipProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XScrollClipProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 对话框族竖屏适配探针（独立可执行） ====================
# [2026-10-26 竖屏适配战役] 竖屏 Xvfb 下逐个打开 xguidemo 对话框族
# （文件打开/另存为/目录选择/输入/颜色/进度/消息框），文件驱动相位，
# 供外部 xwd 截图目验布局与美化效果。
add_executable(XDialogPortraitProbe
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_dialog_portrait_probe.c"
)
if(UNIX)
    target_link_libraries(XDialogPortraitProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XDialogPortraitProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 五个闭环测试：已去 main 收编主程序（2026-10-29） ====================
# 原 XModbusHubTest / XProtocolCampaignTest / XVfdAtv61Test / XVfdM512Test /
# XVfdBus485Test 五个独立 main 可执行目标在本节；现统一改为
#   int XxxTest_run(void)（--test/<name> 分派，返回失败数）
#   void XxxTest(XVariant*)（测试菜单动作）
# 随 TEST_FILE 编译进 XinYueC_Static/_Dynamic（声明在 XProtocolTest.h，
# 菜单接线在 XTestMenu_XModbusTest，命令分派在 main.c 与 XTestCommand.c）。
# 跑法不变：驱动脚本（mbtest_*_run.sh）起好虚拟从站后调
#   ./bin/XinYueC_Static --test xmodbus-hub|xprotocol-campaign|xvfd-m512|xvfd-bus485|xvfd-atv61

# ==================== 高精度定时器组压测（菜单入口） ====================
# [两孩子删除句柄身份压测 2026-10-25] 原先本节为独立可执行
# XHrTimerStress_Test（自带 main 的 hr_stress_driver.c，供 CI / 桌面闸门
# 直跑）。该驱动文件已删除、目标已下线：压测改从测试菜单进入
#   「测试代码 → 定时器 → XHrTimer(高精度定时器组) → 压测(随机增删+句柄回收)」
# 菜单入口见 Test/XTimerTest/XHrTimerTest.c，测试主体见
# XHrTimerStressTest.c，与 XGuiWindowDemo_Test 的 --hr-stress 入口同源共用。

# 无线后端解析器单元测试（纯函数，无需无线硬件）：WEXT 事件流（展开/紧凑
# 两种内核布局）、wpa_supplicant SCAN_RESULTS、nmcli -t 转义、STATUS 键值。
add_executable(XDeviceWiFiParsers_Test
    "${CMAKE_CURRENT_LIST_DIR}/XDeviceTest/xdevice_wifi_posix_parsers_test.c"
)
if(UNIX)
    target_link_libraries(XDeviceWiFiParsers_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XDeviceWiFiParsers_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XFtp 端到端真服务器联调测试 ====================
# 独立可执行文件 ftp_e2e_test.exe，搭配 ftp_test_server.py 跑完整 FTP 协议测试
add_executable(FtpE2E_Test
    "${CMAKE_CURRENT_LIST_DIR}/../ftp_e2e_test.c"
)
if(UNIX)
    target_link_libraries(FtpE2E_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(FtpE2E_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XUpgrade 端到端联调测试（独立可执行 xupgrade_e2e_test.exe，自升级模块全流程联调：23 用例含版本边界/manifest 拒绝矩阵/断电矩阵 1-7/回滚限流/引擎锁/A-B 模拟） ====================
if(NOT XINYUE_EMBEDDED)
add_executable(XUpgradeE2E_Test
    "${CMAKE_CURRENT_LIST_DIR}/../xupgrade_e2e_test.c"
)
if(UNIX)
    target_link_libraries(XUpgradeE2E_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XUpgradeE2E_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # NOT XINYUE_EMBEDDED

# ==================== XUpgrade 打包签名工具（服务端发版用，XUpgrade.md §4/§5） ====================
# pack（目录→.pak）/ seal（补 size+sha256 并按 canonical+P-256 签名+自验）/ verify（设备侧
# 同源验签+组件哈希核对）/ keygen（生成产品钥对，替换内嵌测试钥）。全程复用库内实现零口径漂移；
# 仅宿主平台可执行（嵌入式端只解包不打包）。
if(NOT XINYUE_EMBEDDED)
add_executable(XUpgradePack
    "${CMAKE_CURRENT_LIST_DIR}/../xupgrade_pack.c"
)
if(UNIX)
    target_link_libraries(XUpgradePack ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XUpgradePack ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # NOT XINYUE_EMBEDDED

# ==================== XGui 对齐回归测试 ====================
# XGUI_ON=0（嵌入式全裁剪）时 GUI 类型全部消失，回归测试(引用 GUI 类型)
# 与 GUI 控件测试源一并排除。
if(XGUI_ON)
add_executable(XGuiRegression_Test
    "${CMAKE_CURRENT_LIST_DIR}/../xgui_regression_test.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XLineEditTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XSliderTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XButtonHoldTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XSpinBoxTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGroupBoxTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XBrushPaletteTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XProgressBarTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XDialTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XComboBoxTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XTabBarTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XLcdNumberTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XScrollBarTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XStackedWidgetTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XButtonGroupTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XKeyboardTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XPinyinT9Test.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XTouchMultiPointTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XDataStreamTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XTableAlignTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XWidgetQtAlignTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRegression_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# 测试菜单全量遍历探针（2026-10-31）：把 XTestMenuTest_create() 整棵树的
# **每个动作**逐个触发一遍，逐项打点定位「执行即崩 / 零输出」的动作。
# GUI 测试菜单页的 autotest 只走一条路径，覆盖不足；本目标提供全量口径。
# 菜单树 builder 全在 TEST_FILE（与 demo 收编同口径）；自带 main 的
# 探针/测试源须剔除（防 main 重复定义）。
add_executable(XGuiTestMenuSweep
    "${CMAKE_CURRENT_LIST_DIR}/../xgui_testmenu_sweep.c"
)
if(NOT XINYUE_EMBEDDED)
    set(XGUI_SWEEP_TESTTREE_SOURCES ${TEST_FILE})
    list(REMOVE_ITEM XGUI_SWEEP_TESTTREE_SOURCES
        # 自带 main 的独立可执行（与 demo 收编剔除清单同口径）。
        "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XScrollTouchProbe.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGestureRecognizerTest.c")
    target_sources(XGuiTestMenuSweep PRIVATE ${XGUI_SWEEP_TESTTREE_SOURCES})
endif()
if(UNIX)
    target_link_libraries(XGuiTestMenuSweep XHsfServerIoLib ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiTestMenuSweep XHsfServerIoLib mbedtls lwip zlib fatfs pcre2 sqlite freetype dbghelp ${EXPORT_NAME}S)
endif()
xinyue_force_include_config(XGuiTestMenuSweep)

# Hover synthesis probe (2026-10-31): reproduces "delete the old hover
# target then hover the new button" - asserts ENTER/LEAVE still dispatch
# after the old target is destroyed (Qt QWidgetWindow::m_widget parity).
add_executable(XGuiHoverProbe
    "${CMAKE_CURRENT_LIST_DIR}/../xgui_hover_probe.c"
)
if(UNIX)
    target_link_libraries(XGuiHoverProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiHoverProbe mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
xinyue_force_include_config(XGuiHoverProbe)

# 图表时间轴交互探针（2026-11-03）：断言滚轮/拖拽只作用于横轴、缩放以
# 光标为锚、绘图区已发布到模型、视口变更信号载荷正确（现场反馈：变频器
# 页趋势图滚轮缩的是纵坐标、放大后拖不动）。自带 main 的独立探针：整包
# 回归 XGuiRegression_Test 当前链接受 XTableView API 改名影响，本探针单跑。
add_executable(XGuiChartProbe
    "${CMAKE_CURRENT_LIST_DIR}/../xgui_chart_probe.c"
)
if(UNIX)
    target_link_libraries(XGuiChartProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiChartProbe ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
xinyue_force_include_config(XGuiChartProbe)
endif() # XGUI_ON

# ==================== 滚动区触摸跟手+惯性独立探针 ====================
# 自带 main：注入式验证 XAbstractScrollArea 触摸滚动通道（跟手 1:1+方向/
# 惯性 fling 停稳/不可滚不认领/tap 零滚动/开关关闭回落滚轮量化），脱离
# 整包回归单独跑（整包回归含机器环境相关段，CI 定位诊断用）。
if(XGUI_ON)
add_executable(XScrollTouchProbe_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XScrollTouchProbe.c"
)
if(UNIX)
    target_link_libraries(XScrollTouchProbe_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XScrollTouchProbe_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif()

# ==================== 手势识别器回归测试 ====================
# 独立可执行(自带 main): 注入式验证 XGestureRecognizer 族（点按/长按/
# 双击/平移/轻扫/缩放/旋转/甩动）——原始触摸帧注入 → 识别 → 状态机迁移
# → XGestureEvent 经事件循环投递 → 控件 GestureEvent 槽消费。
if(XGUI_ON)
add_executable(XGestureRecognizer_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGestureRecognizerTest.c"
)
if(UNIX)
    target_link_libraries(XGestureRecognizer_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGestureRecognizer_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif()

# ==================== XGuiRemote 访问口令认证矩阵测试 ====================
# 独立可执行(自带 main): 回环对 + 127.0.0.1 真 TCP(MITM 抓包)跑 XGuiRemote.md
# §3.8 认证矩阵——匿名基线/无口令快速失败/错口令拒绝/对口令准入/运行期设清
# (存量会话不断)/nonce 逐连接随机/失败上限可配/口令明文不过网。
# Windows 无 POSIX 头（unistd.h/poll.h），认证矩阵测试为 POSIX CI 直跑口径，
# 仅在 UNIX 族配置（Xvfb 会话执行），避免拖垮全树 ninja。
if(XGUI_ON AND NOT WIN32)
add_executable(XGuiRemoteAuth_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteAuthTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteAuth_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteAuth_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && NOT WIN32（认证矩阵测试）


# ==================== XGuiRemote 自适应降档阶梯测试(perf9 路4) ====================
# 独立可执行(自带 main): (a) XGuiRemoteAdapt.h 纯状态机单元(阶梯映射/迟滞/
# 换档重置); (b) 回环量化——latency 档 60fps 持续伤害流下 XGUI_REMOTE_ADAPT_
# BUDGET_MS=1 强制超预算, 断言交付批率沉底 ~15fps; --adapt-baseline 反证组
# (关阶梯≈60fps)。需显示环境, 由 Xvfb 会话直跑口径执行(同认证矩阵测试)。
# 测试体用 setenv/unsetenv 等 POSIX 接口，仅 UNIX 族配置（Windows 跳过）。
if(XGUI_ON AND NOT WIN32)
add_executable(XGuiRemoteAdapt_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteAdaptTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteAdapt_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteAdapt_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && NOT WIN32（自适应降档阶梯测试）

# ==================== fbdev 弹层拖动条带归位测试 ====================
# 独立可执行(自带 main)：伪 fbdev 显示驱动（RAM 帧缓冲，双缓冲+行距
# 垫高）离屏跑「弹层出现→拖动→拖回→松手」全帧合成模型断言——对话框
# 拖动白块根修（2026-10-05）的回归锁。须 fbdev 构建才激活（驱动契约
# 与 XWindowDecoration fbdev 分支均受 XPLATFORM_FBDEV_ON 门控）；桌面
# 默认构建编出 SKIP 壳（恒 0 退出，不扰门禁）。BUFFER_COUNT=2 镜像真
# 机 DIRECT+2 双缓冲差带账本路径。
if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiDialogMove_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiDialogMoveTest.c"
)
if(UNIX)
    target_link_libraries(XGuiDialogMove_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiDialogMove_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（弹层拖动条带归位测试）

# ==================== fbdev 停靠面板拖出浮动让位区残留测试 ====================
# 独立可执行(自带 main)：伪 fbdev 显示驱动离屏跑「停靠面板按下→越过脱
# 靠阈值→脱靠浮动→移出宿主→松手」全链，让位区逐像素比对「拖动帧 vs
# 强制宿主重绘帧」（合成器语义判据）。构建组合同 XGuiDialogMove_Test
# （FBDEV+BUFFER_COUNT=2+RGB16 真机 DIRECT+2 trio）。
if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiDockDrag_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiDockDragTest.c"
)
if(UNIX)
    target_link_libraries(XGuiDockDrag_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiDockDrag_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（停靠面板拖出浮动让位区残留测试）

# ==================== fbdev 键盘紧凑悬浮拖移测试 ====================
# 独立可执行(自带 main)：伪 fbdev 显示驱动离屏跑「停靠弹出→悬浮切换→
# 工具栏空白拖移→拖回→松手」全帧合成模型断言——键盘拖动残影+逐 MOVE
# 重绘风暴（昆仑通态 A33 真机 2026-10-06，FPS 5.9）的回归锁。构建组合
# 同 XGuiDialogMove_Test（FBDEV+BUFFER_COUNT=2+RGB16 真机 DIRECT+2 trio）。
if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiKeyboardDrag_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiKeyboardDragTest.c"
)
if(UNIX)
    target_link_libraries(XGuiKeyboardDrag_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiKeyboardDrag_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（键盘紧凑悬浮拖移测试）

if(XGUI_ON AND XPLATFORM_FBDEV_ON)
add_executable(XGuiMirrorFidelity_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiMirrorFidelityTest.c"
)
if(UNIX)
    target_link_libraries(XGuiMirrorFidelity_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiMirrorFidelity_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON && XPLATFORM_FBDEV_ON（镜像保真回环测试）

if(XGUI_ON)
add_executable(XLineControl_Acceptance_Test
    "${CMAKE_CURRENT_LIST_DIR}/../xgui_linecontrol_acceptance_test.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XLineControlAcceptance.c"
)
if(UNIX)
    target_link_libraries(XLineControl_Acceptance_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XLineControl_Acceptance_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
add_test(NAME XLineControlAcceptance COMMAND XLineControl_Acceptance_Test)

# ==================== XWidget 公开 API 契约回归 ====================
# [test_gap#0 2026-11-05] Test/XGuiTest 下此前无 XWidget 专属测试文件——
# XWidget 公开声明 255 个、Test/ 已引用 191 个唯一名，本轮 Qt 6.8 对齐
# 改动（几何/焦点/命中/动作/窗口状态）全无护栏。本套件覆盖：API 存在性
# 与 NULL 入参、几何不变式（rect 全幅 / contentsRect 扣边距）、
# focusNextPrevChild / setVisible 虚槽覆写点、ensurePolished 链、
# winId 转发。无头可跑（不依赖平台窗口），进 ctest 门禁。
add_executable(XWidgetApi_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XWidgetApiTest.c"
)
if(UNIX)
    target_link_libraries(XWidgetApi_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XWidgetApi_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
add_test(NAME XWidgetApiTest COMMAND XWidgetApi_Test)

# 平台电源契约回归（外部唤醒根修的回归锁）：白盒桩驱动，跑得快、无屏
# 依赖，可进 CI 门禁。
add_test(NAME XPlatformPower COMMAND XPlatformPowerTest)

endif() # XGUI_ON

# 测试以相对路径访问仓库资源（assets/、../Library/XFont 等），其直跑口径
# 是仓库 bin/ 目录（./bin/XGuiRegression_Test）；CTest 显式对齐该工作目录，
# 避免构建目录层级（如 out/build/x64-Debug）破坏 ../ 回退。
# （enable_testing() 在根 CMakeLists——保证顶层 ctest 可见全部用例。）
add_test(NAME XGuiRegression COMMAND XGuiRegression_Test
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../bin")
# CTest 在受控运行环境下会以 ptrace 方式启动子进程；LeakSanitizer 无法在该
# 环境工作。地址越界检查仍由 ASan 保留，泄漏检查在可直接运行的环境单独执行。
# 显式钉 software：默认层口径 2026-11-01 起为「软件光栅，GPU 按需启用」
# （XGPU_RUNTIME_DEFAULT_ON=0，板端实测 GPU 直通进程 CPU 是软件光栅的
# 2~4 倍），但显式钉死仍是必要的——CI 若在带 env 的环境跑本用例，
# 须保证软件契约套件不被 GPU 快速路径的 skip 门跳过。
set_tests_properties(XGuiRegression PROPERTIES
    ENVIRONMENT "XGUI_RENDER_BACKEND=software;XGUI_CSD=0;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")
# GPU 后端下的完整回归变体：无 GL 的环境由后端自动回退软件（断言按
# 实际会话状态跳过降级项），有 GL 时逐用例覆盖 GPU 快速路径。
# 注：XGUI_GPU_SYNC=1 走「命令级同步读回」像素契约口径，该口径下斜线
# 快速路径（XGPU_FASTPATH_EXT 缺省开）按设计不走（保持画布路径的
# 逐像素一致），故**非 SYNC** 的 GPU 运行是覆盖该默认值的必需变体
# （板端/桌面手工口径：XGUI_RENDER_BACKEND=gpu 不带 SYNC）。
add_test(NAME XGuiRegressionGpu COMMAND XGuiRegression_Test
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../bin")
set_tests_properties(XGuiRegressionGpu PROPERTIES
    ENVIRONMENT "XGUI_RENDER_BACKEND=gpu;XGUI_GPU_SYNC=1;XGUI_CSD=0;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")
# 默认层端到端变体：不设任何后端 env，验证**默认口径**本身——当前默认层
# 为软件光栅（requested()==false），GPU 路径由上面的 XGuiRegressionGpu
# 变体覆盖。
add_test(NAME XGuiRegressionAuto COMMAND XGuiRegression_Test
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../bin")
set_tests_properties(XGuiRegressionAuto PROPERTIES
    ENVIRONMENT "XGUI_CSD=0;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")

# ==================== XGuiRemote 输入链路 + 视图适配缩放专项探针 ====================
# mcgs 战役输入路(2026-10-04)独立可执行: 回环零网络拓扑下自证客户端 FIT
# 信箱缩放、输入逆映射、键盘/滚轮链与服务端注入多顶层命中。不进 CTest
# (需图形显示, 由 Xvfb 会话直跑口径执行)。
if(XGUI_ON)
add_executable(XGuiRemoteInputView_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGuiRemoteInputViewTest.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteInputView_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteInputView_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON

# ==================== Linux X11 XDND 跨客户端端到端测试 ====================
# 用独立 Xlib 客户端模拟拖放源，验证 XDND 状态应答、Selection 数据传输和
# XDropEvent 负载。仅 Linux 且已找到 X11 时构建，嵌入式裁剪不引入平台依赖。
# X11_FOUND/X11_LIBRARIES 来自根探测段（目录作用域向下继承）。
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND X11_FOUND)
    add_executable(XGuiXdnd_Test
        "${CMAKE_CURRENT_LIST_DIR}/../xgui_xdnd_test.c"
    )
    target_link_libraries(XGuiXdnd_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${X11_LIBRARIES} ${EXPORT_NAME}S)
endif()

# ==================== XGui 统一可视化控件测试程序 ====================
# 桌面原生窗口控件验收入口：XWindow + XGuiApplication + XBackingStore 走完整的
# 「平台原生窗口（X11/Win32）-> 事件注入 -> 重绘 -> 上屏」链路。
# 运行：无参数常驻，带参数可传入自动退出秒数（供无窗口管理器环境截图）。
# 安卓排除：demo 由 xguidemo 共享库（NativeActivity 壳）承载，本可执行
# 是桌面窗口系统专用；安卓下 main 已改名 xgui_window_demo_main 且诊断
# 依赖壳里的 XGuiDemo_debugLog，独立可执行无入口可链。
if(XGUI_ON AND NOT ANDROID)
if(WIN32)
enable_language(RC)
# exe 图标（demo 品牌条棋盘格导出的多尺寸 ico），经 configure_file 注入
# rc 模板；绝对路径正斜杠形式，rc.exe 对引号内正斜杠路径接受。
set(XGUI_DEMO_ICON_FILE "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/res/xgui.ico")
configure_file("${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_window_demo.rc.in"
               "${CMAKE_CURRENT_BINARY_DIR}/xgui_window_demo.rc" @ONLY)
set(XGUI_WINDOW_DEMO_RC "${CMAKE_CURRENT_BINARY_DIR}/xgui_window_demo.rc")
else()
set(XGUI_WINDOW_DEMO_RC)
endif()
add_executable(XGuiWindowDemo_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_window_demo.c"
    ${XGUI_WINDOW_DEMO_RC}
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_views.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_dialogs.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_advanced.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_keyboard.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_effects.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_remote_server.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_remote_client.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_network.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_overlay_settings.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_qrcode.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_update.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_modbus.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_window.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_timeddns.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_atv61.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_testmenu.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_shell.c"
    # [两孩子删除句柄身份压测 2026-10-25] --hr-stress 入口
    "${CMAKE_CURRENT_LIST_DIR}/XTimerTest/XHrTimerStressTest.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_buttons.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_labels.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_input.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_text.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_views.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_containers.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_dialogs.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_menus.c"
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_core.c"
)
# [测试菜单页收编 2026-10-29] xgui_demo_page_testmenu 的菜单树来自
# XTestMenuTest_create（XTestMenuTest.c + 各域 builder，全在 TEST_FILE）。
# 非嵌入式构建把整棵测试树链进 GUI demo（与控制台宿主 XinYueC_Static
# 同口径）；XHrTimerStressTest 已显式在上方源列表，剔除防重复。嵌入式
# （F407）不链——页面自身按 XINYUE_EMBEDDED 降级为提示桩。
if(NOT XINYUE_EMBEDDED)
    set(XGUI_DEMO_TESTTREE_SOURCES ${TEST_FILE})
    list(REMOVE_ITEM XGUI_DEMO_TESTTREE_SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/XTimerTest/XHrTimerStressTest.c"
        # [触摸滚动探针 2026-10-29] 自带 main 的独立可执行（与
        # XHrTimerStressTest 同款剔除防重复）。
        "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XScrollTouchProbe.c"
        # [手势识别器测试 2026-10-29] 自带 main 的独立可执行。
        "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/XGestureRecognizerTest.c")
    target_sources(XGuiWindowDemo_Test PRIVATE ${XGUI_DEMO_TESTTREE_SOURCES})
endif()
if(UNIX)
    # [链接序对齐 2026-10-29] 与 XinYueC_Static 同口径（sqlite 夹在两份主
    # 库之间）：测试树收编后 demo 也从 sqlite3_xin_vfs 反向要 XReadWriteLock
    # 等主库符号——sqlite 排在末位时符号无法回解（链接序根修）。
    target_link_libraries(XGuiWindowDemo_Test XHsfServerIoLib ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiWindowDemo_Test XHsfServerIoLib ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 sqlite freetype ${EXPORT_NAME}S)
endif()
# Linux 桌面图标/启动器接线（安卓不装桌面文件；Windows 图标走 rc 资源）：
# exe 入 bin，hicolor 四档 PNG 入系统图标主题（freedesktop 惯例，文件名
# xguidemo 与 .desktop Icon= 一致），.desktop 入 applications——安装后
# 桌面环境应用列表/收藏栏即带品牌棋盘格图标；运行期窗口图标（任务栏/
# Alt-Tab）另由 demo 的 XWidget_setWindowIcon→_NET_WM_ICON 通道落地。
if(UNIX AND NOT ANDROID)
    install(TARGETS XGuiWindowDemo_Test RUNTIME DESTINATION bin)
    foreach(_sz 48 64 128 256)
        install(FILES "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/res/icons/xguidemo-${_sz}.png"
                DESTINATION "share/icons/hicolor/${_sz}x${_sz}/apps"
                RENAME "xguidemo.png")
    endforeach()
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/res/xguidemo.desktop"
            DESTINATION "share/applications")
endif()

# ==================== XGuiRemote 远程窗口双进程演示 ====================
# 单文件双角色（xgui_demo_remote.c 自带 main）：--server 起最小控件树 +
# XGuiServer 共存镜像（默认端口 46000），--client 另进程连接显示；
# --profile performance|resource 运行期切档，--tls 走 XSslSocket 加密档。
# 安卓排除同 XGuiWindowDemo_Test：demo 为桌面窗口系统专用独立可执行。
if(XGUI_ON AND NOT ANDROID)
add_executable(XGuiRemoteDemo_Test
    "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_remote.c"
)
if(UNIX)
    target_link_libraries(XGuiRemoteDemo_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiRemoteDemo_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON AND NOT ANDROID（XGuiRemoteDemo_Test）
endif() # XGUI_ON AND NOT ANDROID

# ==================== XGuiWindowDemo 安卓 APK 壳（共享库） ====================
# NativeActivity 壳：android_main.c 生命周期接线 + 平台层（窗口/输入）。
# demo main 重命名为 xgui_window_demo_main（__ANDROID__ 下）并入本目标，
# 导出 ANativeActivity_onCreate。打包脚本
# （platforms/android/build_apk.sh）以 libxguidemo.so 为载体组 APK。
#
# 注意：2026-10-24 安卓模板迁入 platforms/android/ 后，**只搬了平台差异层**
# （打包脚本 / 清单 / 资源 / 签名）。下面这些源文件是框架平台层与 demo 的
# 公共部分，仍由本模块统管编译——不要因为"模板在 platforms/"就把它们也
# 搬走，否则三个平台（桌面/安卓/ESP8266）的公共源码会分裂成多份副本。
if(XGUI_ON AND ANDROID)
    add_library(xguidemo SHARED
        "${CMAKE_CURRENT_LIST_DIR}/../Drive/Android/android_main.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_window_demo.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_views.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_dialogs.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_advanced.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_keyboard.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_effects.c"
        # [安卓 demo 页补齐 2026-10-24] 下列 9 个扩展页与桌面
        # XGuiWindowDemo_Test 目标同集：xgui_window_demo.c 无条件引用它们的
        # build/adapt/shutdown 符号（守卫是 XGUI_REMOTE_ON 等特性宏，不是
        # 平台宏），故安卓目标必须一并编译。此前本列表只列了 5 个基础页，
        # 链接期报 undefined symbol: demo_page_remote_client_adapt 等 20+
        # 个（arm64 全新配置目录必现；x86_64 侥幸能过是因为其 build.ninja
        # 生成于工作区曾有完整列表的窗口期，重新配置即会同样失败）。
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_remote_server.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_remote_client.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_network.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_overlay_settings.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_qrcode.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_update.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_modbus.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_window.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_timeddns.c"
        # [2026-10-26 补漏] ATV61 页（xgui_window_demo.c 无条件引用其
        # build/adapt/autotest 符号）此前只进了桌面目标列表，安卓目标
        # 漏列 → 链接期 undefined symbol: demo_page_atv61_build 等三个
        # （与上方 remote 页同款漏列的复发，按同一纪律补齐）。
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_page_atv61.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_buttons.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_labels.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_input.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_text.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_views.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_containers.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_dialogs.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_menus.c"
        "${CMAKE_CURRENT_LIST_DIR}/XGuiDemo/xgui_demo_apitest_core.c"
    )
    target_link_libraries(xguidemo ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} mbedtls lwip zlib fatfs pcre2 sqlite ${EXPORT_NAME}S)
    target_link_libraries(xguidemo android log)
    # [2026-10-24 安卓无障碍接入] 保活 JNI_OnLoad：链接参数含
    # -Wl,--gc-sections，未被引用的符号（含 JNI_OnLoad）会被裁掉，
    # ART 加载 .so 时找不到 JNI_OnLoad → RegisterNatives 不执行 →
    # Java 侧 native 方法报 "No implementation found"（真机实证崩溃）。
    # --undefined 把符号标记为外部引用，阻止裁剪。
    target_link_options(xguidemo PRIVATE
        "LINKER:--undefined=JNI_OnLoad")
endif()

# ==================== 安卓真机 Modbus 主站轮询探针 ====================
# [2026-10-25] 安卓真机 Modbus 主站「连上了但不轮询」的复现/回归工具：
# 无 GUI，直接驱动库（连接 → 等 connected → 周期发读请求 → 打印每次
# 结果）。安卓 seccomp 拒 io_uring_setup（实测 EACCES）故强制走
# XNetIoRingPosix 的 epoll 引擎，而桌面走 io_uring——两条路径不同，
# 桌面用例覆盖不到 epoll 分支的缺陷（Modbus 应答收不回来的根因所在）。
# 探针独立可执行，改完 epoll 引擎可直推手机验证，无需打包 APK 跑 UI。
#   用法：./xmodbus_android_probe <host> <port> <unitId> <pointCount>
if(ANDROID)
    add_executable(xmodbus_android_probe
        "${CMAKE_CURRENT_LIST_DIR}/XProtocolTest/xmodbus_android_probe.c"
    )
    target_link_libraries(xmodbus_android_probe ${EXPORT_NAME}S
        ${XINYUE_UNIX_THREAD_LIBS} mbedtls lwip zlib fatfs pcre2)
    target_link_libraries(xmodbus_android_probe android log)
endif()

# ==================== XGui 字库离屏巡检工具 ====================
# 全字库离屏扫描（ASCII + GB2312 汉字，16px 轮廓 XFont -> 32x32 内存
# XImage，走与窗口后备缓冲一致的 XPainterDevice_Image SW-AA 字形管线）：
# 三道判据（零墨/墨量塌陷/横画叠带）筛坏字，产出 out/scan_bad_1.txt 与
# out/scan_bad/*.png 样图；退出码 = min(坏字数, 250)。仅新增文件与目标，
# 不触碰现有源码逻辑。仅桌面可执行（无窗口依赖，任何平台均可构建）。
add_executable(xgui_font_scan_tool
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xgui_font_scan_tool.c"
)
# 粗体真字体 A/B 离屏验证（2026-10-08）：Normal vs Bold 字重同句渲染
# PPM，验证字重变体槽（XFontFt_ensureFamilyWeighted）命中 -Bold 文件。
add_executable(xgui_font_bold_ab
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/bold_ab_tool.c"
)
if(UNIX)
    target_link_libraries(xgui_font_scan_tool ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(xgui_font_scan_tool ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
if(UNIX)
    target_link_libraries(xgui_font_bold_ab ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(xgui_font_bold_ab ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== XGui GPU 阶段 1 冒烟测试 ====================
add_executable(XGuiGpu_Test
    "${CMAKE_CURRENT_LIST_DIR}/../xgui_gpu_test.c"
)
if(UNIX)
    target_link_libraries(XGuiGpu_Test ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XGuiGpu_Test ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
add_test(NAME XGuiGpu COMMAND XGuiGpu_Test)
set_tests_properties(XGuiGpu PROPERTIES
    ENVIRONMENT "XGUI_RENDER_BACKEND=gpu;ASAN_OPTIONS=detect_leaks=0:halt_on_error=1")

# ==================== 阶段二 P2.5 字形覆盖图逐像素 diff 工具 ====================
# 自带 main 的独立离屏工具（仿 xgui_font_scan_tool 口径，无窗口依赖）：
# A 段 provider 旧/新槽位逐位对照（改动前 vs 改动后）；
# B 段 SW 与 GPU 两消费点真实渲染逐像素 diff（字形光栅化两挂点——
# XFONT_FT_RASTER 双档门已移除，两挂点恒走 FT smooth 光栅化器）；
# 全部录音流与像素折叠成 FNV-1a digest——对改动前基线与改动后两次
# 运行做 digest 逐字节比对即换点逐位一致性证据（设计 §8 P2.5 门禁）。
add_executable(xgui_glyph_coverage_diff_tool
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/xgui_glyph_coverage_diff_tool.c"
)
if(UNIX)
    target_link_libraries(xgui_glyph_coverage_diff_tool ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(xgui_glyph_coverage_diff_tool ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== CoarseTimer（时间轮）路径最小复现探针 ====================
# 2026-10-22：定位 ESP8266 NONOS3 固件上"框架定时器路径"的崩溃
# （Fatal exception 0 / epc1=XAtomic_exchange_bool+3 / excvaddr 指向 rodata
#  数据符号 = 野指针/坏 vtable）。桌面端跑同一场景：
#   · 崩 → 纯框架 bug，可直接 gdb 定位；
#   · 不崩 → 与 32 位裸机路径相关。
# 详见 platforms/esp8266/docs/PROGRESS.md §0-Y.6。
add_executable(x_timer_coarse_repro
    "${CMAKE_CURRENT_LIST_DIR}/XGuiTest/x_timer_coarse_repro.c"
)
if(UNIX)
    target_link_libraries(x_timer_coarse_repro ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(x_timer_coarse_repro ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 时间轮相位落槽回归测试（独立可执行） ====================
# [2026-10-28 顶栏时钟战役] addTimer_lockfree 高层轮旧落槽忽视注册相位：
# 1000ms CoarseTimer 落 L2（槽距 900ms）后固定 1800ms 触发（顶栏时钟
# 「1s 刷新变 1.8s」实证）。本测试错相注册 1000/2500ms 定时器并逐次
# 断言触发间隔（容差 -120/+150ms），缺陷复现即红。
# 用法：XTimerWheelPhaseTest [运行毫秒]
add_executable(XTimerWheelPhaseTest
    "${CMAKE_CURRENT_LIST_DIR}/XTimerTest/XTimerWheelPhaseTest.c"
)
if(UNIX)
    target_link_libraries(XTimerWheelPhaseTest ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XTimerWheelPhaseTest ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 时间轴口径回归测试（独立可执行） ====================
# [2026-11-05 纪元化战役] 两条互为因果的缺陷的回归锁：
#  A/B 段：桌面端 XDateTime_currentMSecsSinceEpoch 零点不是 Unix 纪元
#          （Win32 进程首调 QPC / POSIX CLOCK_MONOTONIC 裸值）⇒ 进程刚起来
#          返回值在 0 附近甚至恒 0，被消费方当成"没有时刻"
#          （xindustry-flowmeter「收数后有时刻」曾 1/5~1/3 概率翻）；
#          纳秒轴直读 REALTIME 与毫秒轴不同锚，事件分发器两轴相减算错等待。
#  C 段：零点改纪元后，时间轮 m_current_tick 初值 0 与真实刻度差 ~1.7e12 拍，
#        原逐拍追赶循环把 200ms 定时器折算成"立即到期"。
# 用法：XTimeAxisEpochTest [观察窗毫秒]
add_executable(XTimeAxisEpochTest
    "${CMAKE_CURRENT_LIST_DIR}/XTimerTest/XTimeAxisEpochTest.c"
)
if(UNIX)
    target_link_libraries(XTimeAxisEpochTest ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XTimeAxisEpochTest ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 平台电源契约回归测试（独立可执行） ====================
# [2026-11-02 外部唤醒根修] 「被 VNC 唤醒后再也无法休眠」的回归锁：
# 影子态永久卡在「熄」⇒ 自动熄屏分支被 !m_screenOff 永久门死。
# 白盒编译：框架实现（Src/XGui/Platform/XPlatformPower.c）直接编入本
# 目标 + 测试内桩驱动（替掉平台层 6 个 Driver_ 钩子）⇒ 宿主上即可精确
# 驱动「影子态 ↔ 硬件实况」状态机。框架实现在本目标自带一份 ⇒ 链接时
# 库内同名成员不会被拉入（符号已由本目标提供），桩驱动同理。
# XGUI_ON=0（嵌入式全裁剪）时电源契约整段裁掉，本测试无对象 ⇒ 排除。
if(XGUI_ON)
add_executable(XPlatformPowerTest
    "${CMAKE_CURRENT_LIST_DIR}/XPlatformPowerTest/XPlatformPowerTest.c"
    "${XINYUE_SOURCE_DIR}/Src/XGui/Platform/XPlatformPower.c"
)
if(UNIX)
    target_link_libraries(XPlatformPowerTest ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
else()
    target_link_libraries(XPlatformPowerTest ${EXPORT_NAME}S mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()
endif() # XGUI_ON（电源契约回归测试）

# 板端电源契约探针（真实驱动路径）：X11/DPMS 专属，Windows 无此路径 ⇒
# 仅 UNIX 构建。用法：XPlatformPowerBoardProbe [熄屏超时毫秒]，
# 运行中用 `xset dpms force on` 模拟外部唤醒，观测「影子归位 + 能再休眠」。
if(UNIX AND XGUI_ON)
add_executable(XPlatformPowerBoardProbe
    "${CMAKE_CURRENT_LIST_DIR}/XPlatformPowerTest/XPlatformPowerBoardProbe.c"
)
target_link_libraries(XPlatformPowerBoardProbe ${EXPORT_NAME}S ${XINYUE_UNIX_THREAD_LIBS} ${XINYUE_UNIX_EXTRA_LIBS} mbedtls lwip zlib fatfs pcre2 ${EXPORT_NAME}S)
endif()

# ==================== 测试/工具目标统一强制包含配置头 ====================
# 布局分裂根修（2026-10-21）：核心库经 xinyue_force_include_config 强制
# /FI CXinYueConfig.h，而测试/工具可执行目标此前没有——它们的 TU 在首个
# XGuiConfig.h 到达时 XGUI_ON 未定义，整个缺省定义体被跳空，
# XPAINTDEVICE_ON/XLAYOUT_ON 等影响 XWidget 结构体布局的宏与库 TU 取值
# 不一致 → 同一二进制内两套 XWidget 布局（sizeof 552 vs 更小）。实测后果：
# 测试 TU 读 m_sssStyleCache（库视图偏移 544）命中别的字段，析构/拷贝把
# 运行期数据当指针 free → _CrtIsValidHeapPointer 断言崩溃（
# xgui_regression_test 的 abstractbutton/frame 契约测试实锚）。凡链接核心
# 库的可执行目标一律与库同口径强制包含，一次性闭环。
# （函数定义在根 CMakeLists：函数只在其定义作用域及子目录可见，Src/Test
#  两模块都要用；体内路径已改用根锚定变量 XINYUE_SOURCE_DIR。）
set(XINYUE_EXEC_TARGETS
    FtpE2E_Test
    XUpgradeE2E_Test
    XUpgradePack
    XGuiRegression_Test
    XGuiRemoteAuth_Test
    XGuiRemoteAdapt_Test
    XGuiDialogMove_Test
    XGuiKeyboardDrag_Test
    XGuiMirrorFidelity_Test
    XLineControl_Acceptance_Test
    XGuiRemoteInputView_Test
    XGuiWindowDemo_Test
    XGuiRemoteDemo_Test
    xgui_font_scan_tool
    xgui_font_bold_ab
    XGuiGpu_Test
    xgui_glyph_coverage_diff_tool
    x_timer_coarse_repro
    XTimerWheelPhaseTest
    XPlatformPowerTest
)
foreach(_tgt IN LISTS XINYUE_EXEC_TARGETS)
    if(TARGET ${_tgt})
        xinyue_force_include_config(${_tgt})
    endif()
endforeach()
if(TARGET XGuiXdnd_Test)
    xinyue_force_include_config(XGuiXdnd_Test)
endif()

# ==================== 主程序双 exe 安装 ====================
# [CMake_Install.cmake 迁入 2026-11-01] install(TARGETS) 只能作用于本目录
# 创建的目标，exe 安装随目标落在本模块。XGUI_ON=0 裁剪构建与 FreeRTOS/
# 嵌入式分支时目标被排除，随同跳过。
if(XGUI_ON AND NOT XINYUE_EMBEDDED)
install(TARGETS ${EXPORT_NAME}_Static
        RUNTIME DESTINATION bin
)

# 安装动态链接的可执行程序
install(TARGETS ${EXPORT_NAME}_Dynamic
        RUNTIME DESTINATION bin
)
endif() # XGUI_ON AND NOT XINYUE_EMBEDDED

# ==================== 静态归档环依赖收口（模块化拆分遗留）====================
# 判据与机制见根 CMakeLists 的 xinyue_link_rescan_group()：框架链与第三方
# 归档间存在双向符号依赖（zlib↔Core、Gui↔Core），静态链接单遍扫描无法解析，
# 需 LINK_GROUP:RESCAN（--start-group/--end-group）组内反复重扫。本模块
# 的全部可执行（含主程序双 exe、专项探针/回归测试）统一收口。
get_property(_test_exe_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _test_exe_targets)
    get_target_property(_tt ${_t} TYPE)
    if(_tt STREQUAL "EXECUTABLE")
        xinyue_link_rescan_group(${_t})
    endif()
endforeach()
