/**
 * @file xgui_esp8266_ssd1306.h
 * @brief ESP8266 + SSD1306 128x64 XGui 裁剪档案（build/arduino_gui 专用）。
 * @details 以 build/esp8266_console/esp8266_console_minimal.h 为底（已验证
 *          的 NONOS3 + 仓库 FreeRTOS V9 + Xtensa 编译配方），叠加架构师的
 *          XGui 裁剪方案：
 *          - BACKINGSTORE 组：PARTIAL + 1 缓冲 + RGB16 + 128x16 tile +
 *            关攒批（XDateTime 在板级存根返回 -1，攒批 16ms 帧界失效）；
 *          - 后端：XPLATFORMBACKINGSTORE_SOFTWARE_ON=1（unsupported 驱动
 *            create 返回 true，上屏全交 present 回调）；
 *          - 资源：保留层预算 0 / 无 CSD 标题条 / 无限帧叠加；
 *          - 裁剪：图像编解码/缓存/SVG/动画/GPU/剪贴板/无障碍/光标/图表/
 *            文档/输入法/虚拟键盘与全部大控件置 0；渲染内核仅 RGB565；
 *          - 字体：XFONT_FREETYPE_ON=1 是 XFont 模块总闸（陷阱：置 0 连
 *            点阵字库一起裁），轮廓/文件关闭，内置 8x16 点阵显式开启。
 *          由构建脚本以 -include 强制包含，必须早于 CXinYueConfig.h 与
 *          任何模块 Config.h 被预处理。所有开关在各模块配置头里都是
 *          #ifndef 保护的，因此在此处先定义即可生效。
 */

#ifndef XGUI_ESP8266_SSD1306_H
#define XGUI_ESP8266_SSD1306_H

/* size_t 前置：本档案 -include 进每个 TU（早于一切模块头）。关闭
 * XMEMORY_MULTIPOOL_ON 会裁掉 XFixedPool/XMultiPool 的 include 链，而
 * XMemory.h:28 / XFIXEDPOOL_DEFINE 的 sizeof(size_t) 依赖该链带入
 * stddef.h——统一在此前置，消除「unknown type name size_t」级联。 */
#include <stddef.h>

/* ==================================================================== */
/* 1. 模块级总开关（CXinYueConfig.h 的 #ifndef 总开关）                   */
/* ==================================================================== */

/* 图形界面：开（本档案存在的意义）。 */
#ifndef XGUI_ON
#define XGUI_ON 1
#endif

/* 网络协议栈 / 应用层协议：裁。 */
#ifndef XNETWORK_ON
#define XNETWORK_ON 0
#endif
#ifndef XPROTOCOL_ON
#define XPROTOCOL_ON 0
#endif

/* 动作系统：必须保留——XMenu.c 的系统菜单（XWindowDecoration 无条件
 * 调用）引用 XAction_triggered_signal，置 0 即裁剪残留编译错。 */
#ifndef XACTION_ON
#define XACTION_ON 1
#endif
#ifndef XPROPERTY_ON
#define XPROPERTY_ON 0
#endif

/* TUI：裁。 */
#ifndef XTUI_ON
#define XTUI_ON 0
#endif

/* 同步原语：保留——XCoreApplication 事件分发/XThreadData 走 FreeRTOS 路径。 */
#ifndef XSYNC_ON
#define XSYNC_ON 1
#endif

/* 文件系统对象层：保守置 1（XDeviceFile.h 公共类型可用；未引用部分由
 * --gc-sections 丢弃）。字体文件通道另行 XFONT_FILE_ON=0 关死。 */
#ifndef XFILE_ON
#define XFILE_ON 1
#endif

/* SQL / Excel / Xml / DOM / QRCode / 升级：全部裁。 */
#ifndef XSQL_ON
#define XSQL_ON 0
#endif
#ifndef XEXCEL_ON
#define XEXCEL_ON 0
#endif
#ifndef XXmlStream_ON
#define XXmlStream_ON 0
#endif
#ifndef XDOM_ON
#define XDOM_ON 0
#endif
#ifndef XRCODE_ON
#define XRCODE_ON 0
#endif
#ifndef XUPGRADE_ON
#define XUPGRADE_ON 0
#endif

/* 加密：不可裁——XHashMap/XString 硬依赖 XCryptographicHash_XxHash64。
 * 自带全部算法实现（不链 mbedtls），未引用算法由 --gc-sections 丢弃。 */
#ifndef XCryptographic_ON
#define XCryptographic_ON 1
#endif

/* 正则：XContainerConfig.h 内无条件 #define 1（无 #ifndef 保护）——
 * 先 include 触发其 include guard，再 #undef/#define 0（与 console
 * 档案同法，见 esp8266_console_minimal.h 第 1 节注释）。 */
#include "XContainerConfig.h"
#undef XRegularExpression_ON
#define XRegularExpression_ON 0

/* XByteArray zlib 压缩：裁。 */
#ifndef XByteArray_USE_ZLIB
#define XByteArray_USE_ZLIB 0
#endif

/* XBase64 保持开启：XByteArray.c 无守卫引用 XBase64_encode/decode。 */
#ifndef XBase64_ON
#define XBase64_ON 1
#endif

/* I/O 事件环：保持开启——XAbstractEventDispatcher 17 处引用；平台侧由
 * XPlatformStub_esp8266.c 提供返回 NULL 的 createPlatform，已判空降级。 */
#ifndef XAbstractNetIoRing_ON
#define XAbstractNetIoRing_ON 1
#endif

/* FreeRTOS：只服务仓库 V9.0.0 内核，不开 V7 兼容层。 */
#ifndef XFREERTOS_V7_COMPAT
#define XFREERTOS_V7_COMPAT 0
#endif

/* 调试/测试/错误输出：全关。 */
#ifndef DEBUG_ON
#define DEBUG_ON 0
#endif
#ifndef DEMOTEST_ON
#define DEMOTEST_ON 0
#endif
#ifndef XERROR_ON
#define XERROR_ON 0
#endif

/* ==================================================================== */
/* 2. 控制台命令：整体裁（GUI 冒烟固件无 Shell）                          */
/* ==================================================================== */
#ifndef XCONSOLE_SHELL_ON
#define XCONSOLE_SHELL_ON 0
#endif
#ifndef XCONSOLE_SHELL_CONSOLE_ON
#define XCONSOLE_SHELL_CONSOLE_ON 0
#endif
#ifndef XCONSOLE_SHELL_ASYNC_ON
#define XCONSOLE_SHELL_ASYNC_ON 0
#endif
#ifndef XDEVICE_SERIALPORT_ON
#define XDEVICE_SERIALPORT_ON 1
#endif
#ifndef XProcess_ON
#define XProcess_ON 0
#endif

/* ==================================================================== */
/* 3. XGui BACKINGSTORE 组（架构师方案，见 XGuiConfig.h:147-313）         */
/* ==================================================================== */

/* PARTIAL（=0）：必须显式——非桌面默认 DIRECT 且防呆会强升双缓冲 32KB；
 * PARTIAL 仅按 tile 分配（XPlatformBackingStore.c:502-511）。 */
#ifndef XGUI_BACKINGSTORE_RENDER_MODE
#define XGUI_BACKINGSTORE_RENDER_MODE 0
#endif
#ifndef XGUI_BACKINGSTORE_BUFFER_COUNT
#define XGUI_BACKINGSTORE_BUFFER_COUNT 1
#endif
/* RGB565（2B/px）：单色屏无需 Alpha，表面内存减半。 */
#ifndef XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 1
#endif
/* tile 128x8：对齐 SSD1306 单页（8 行），零寻址换算，2KB tile 缓冲
 * （降到 8 行省 2KB 静态——ESP8266 刀刃；present 回调逐 tile 处理，
 * 更多 tile 只是多几次 flush，功能不变）。 */
#ifndef XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH
#define XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH 128
#endif
#ifndef XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT
#define XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT 8
#endif
/* tile 缓冲静态化（XPlatformBackingStore.c:710 在 store 创建时经
 * XPlatformBackingStore_setBuffers 注入，避开 create 时刻堆分配——
 * ESP8266 上 window+show 把堆吃尽时 XBackingStore_create_ex 因 tile
 * 分配失败返回 NULL，实测「backing store handle NULL」根因）。
 * 数组本体定义在 gui_main.c（128x8 RGB565 = 2048B，2 字节对齐）。
 * 本档案被 -include 进每个 TU，extern 声明在 XPlatformBackingStore.c
 * 可见。BUFFER_COUNT=1 → BUFFER2 必须为 NULL（setBuffers 校验）。 */
extern unsigned char xguiEsp8266TileBuffer[];
#ifndef XGUI_BACKINGSTORE_BUFFER1
#define XGUI_BACKINGSTORE_BUFFER1 0
#endif
#ifndef XGUI_BACKINGSTORE_BUFFER2
#define XGUI_BACKINGSTORE_BUFFER2 0
#endif
/* 静态 tile 关闭（=0）：实测外部缓冲（m_externalBuffers）会让 flush 跳过
 * present 回调（presents=0）。XCoreApplication + multipool-off 后堆余
 * ~6.8KB 充足，tile 走正常堆分配 + 正常 present 路径。 */
#ifndef XGUI_BACKINGSTORE_BUFFER_SIZE
#define XGUI_BACKINGSTORE_BUFFER_SIZE 0
#endif
/* 关攒批：攒批缓冲半屏预算=8KB 付不起，且其 16ms 帧界依赖
 * XDateTime_currentMSecsSinceEpoch（板级存根返回 -1）；关后逐 tile
 * 即时 present（契约见 XPlatformBackingStore.h:55-61）。 */
#ifndef XGUI_BACKINGSTORE_TILE_BATCHING_ON
#define XGUI_BACKINGSTORE_TILE_BATCHING_ON 0
#endif

/* ==================================================================== */
/* 4. XGui 后端形态组                                                    */
/* ==================================================================== */

/* ESP8266 非 Linux/Win32 → Drive/Unsupported/.../XPlatformBackingStore_
 * unsupported.c:33-47，SOFTWARE_ON=1 时 create 返回 true 软件后端。 */
#ifndef XPLATFORMBACKINGSTORE_SOFTWARE_ON
#define XPLATFORMBACKINGSTORE_SOFTWARE_ON 1
#endif
/* fbdev 契约 Linux 专用。 */
#ifndef XPLATFORM_FBDEV_ON
#define XPLATFORM_FBDEV_ON 0
#endif
#ifndef XPLATFORM_FBINPUT_ON
#define XPLATFORM_FBINPUT_ON 0
#endif

/* ==================================================================== */
/* 5. XGui 资源组                                                        */
/* ==================================================================== */
#ifndef XGUI_RETAINED_LAYER_BUDGET_BYTES
#define XGUI_RETAINED_LAYER_BUDGET_BYTES 0
#endif
#ifndef XGUI_CSD_DEFAULT
#define XGUI_CSD_DEFAULT 0
#endif
#ifndef XGUI_CUSTOM_TITLEBAR_ON
#define XGUI_CUSTOM_TITLEBAR_ON 0
#endif
#ifndef XGUI_PERFORMANCE_OVERLAY_ON
#define XGUI_PERFORMANCE_OVERLAY_ON 0
#endif
#ifndef XGUI_PRESENT_MAX_FPS
#define XGUI_PRESENT_MAX_FPS 0
#endif
#ifndef XGUI_RESIZE_REPAINT_MAX_FPS
#define XGUI_RESIZE_REPAINT_MAX_FPS 30
#endif

/* ==================================================================== */
/* 6. XGui 裁剪组                                                        */
/* ==================================================================== */
#ifndef XIMAGECODEC_ON
#define XIMAGECODEC_ON 0
#endif
#ifndef XIMAGECACHE_ON
#define XIMAGECACHE_ON 0
#endif
#ifndef XIMAGEIOPLUGIN_ON
#define XIMAGEIOPLUGIN_ON 0
#endif
#ifndef XSVGICON_ON
#define XSVGICON_ON 0
#endif
#ifndef XPIXMAPCACHE_ON
#define XPIXMAPCACHE_ON 0
#endif
#ifndef XMOVIE_ON
#define XMOVIE_ON 0
#endif
/* XGPU_ON 必须为 1：XPainter.c 有 103 处 m_gpuActive/m_gpuBackend 引用
 * 无 #if 守卫（结构体成员随 XGPU_ON 消失，XPainter.h:589-592）——置 0
 * 整个绘制内核编译不过。运行期无 GL 驱动（gui_stubs 提供 gl_procs 返回
 * NULL 的存根），GPU 会话创建失败自动落软件路径，行为等价纯软件。 */
#ifndef XGPU_ON
#define XGPU_ON 1
#endif
#ifndef XCLIPBOARD_ON
#define XCLIPBOARD_ON 0
#endif
#ifndef XMIMEDATA_ON
#define XMIMEDATA_ON 0
#endif
#ifndef XACCESSIBLE_ON
#define XACCESSIBLE_ON 0
#endif
/* 光标类必须保留——XLabel.c:679/XWindowDecoration.c:442 的悬停/缩放光标
 * 引用 XCursor_* 枚举无守卫，置 0 即裁剪残留编译错（类本体 340 行，小）。 */
#ifndef XCURSOR_ON
#define XCURSOR_ON 1
#endif
#ifndef XCHARTS_ON
#define XCHARTS_ON 0
#endif
#ifndef XTEXTDOCUMENT_ON
#define XTEXTDOCUMENT_ON 0
#endif
#ifndef XKEYBOARD_ON
#define XKEYBOARD_ON 0
#endif
#ifndef XINPUTMETHOD_ON
#define XINPUTMETHOD_ON 0
#endif
#ifndef XPLATFORMINPUTCTX_ON
#define XPLATFORMINPUTCTX_ON 0
#endif
#ifndef XVIRTUALKEYBOARD_ON
#define XVIRTUALKEYBOARD_ON 0
#endif

/* 大控件：全部裁。 */
#ifndef XCOMBOBOX_ON
#define XCOMBOBOX_ON 0
#endif
#ifndef XTABLEWIDGET_ON
#define XTABLEWIDGET_ON 0
#endif
#ifndef XTABWIDGET_ON
#define XTABWIDGET_ON 0
#endif
#ifndef XTABBAR_ON
#define XTABBAR_ON 0
#endif
#ifndef XDOCKWIDGET_ON
#define XDOCKWIDGET_ON 0
#endif
#ifndef XMDIAREA_ON
#define XMDIAREA_ON 0
#endif
#ifndef XCALENDARWIDGET_ON
#define XCALENDARWIDGET_ON 0
#endif
#ifndef XWIZARD_ON
#define XWIZARD_ON 0
#endif
#ifndef XDIALOG_ON
#define XDIALOG_ON 0
#endif
#ifndef XMAINWINDOW_ON
#define XMAINWINDOW_ON 0
#endif
#ifndef XLINEEDIT_ON
#define XLINEEDIT_ON 0
#endif
#ifndef XTEXTEDIT_ON
#define XTEXTEDIT_ON 0
#endif
#ifndef XPLAINTEXTEDIT_ON
#define XPLAINTEXTEDIT_ON 0
#endif
#ifndef XTEXTBROWSER_ON
#define XTEXTBROWSER_ON 0
#endif
#ifndef XSCROLLAREA_ON
#define XSCROLLAREA_ON 0
#endif
#ifndef XABSTRACTSCROLLAREA_ON
#define XABSTRACTSCROLLAREA_ON 0
#endif
#ifndef XLISTVIEW_ON
#define XLISTVIEW_ON 0
#endif
#ifndef XLISTWIDGET_ON
#define XLISTWIDGET_ON 0
#endif
#ifndef XTREEVIEW_ON
#define XTREEVIEW_ON 0
#endif
#ifndef XTREEWIDGET_ON
#define XTREEWIDGET_ON 0
#endif
#ifndef XTABLEVIEW_ON
#define XTABLEVIEW_ON 0
#endif
#ifndef XSLIDER_ON
#define XSLIDER_ON 0
#endif
#ifndef XABSTRACTSLIDER_ON
#define XABSTRACTSLIDER_ON 0
#endif
#ifndef XDIAL_ON
#define XDIAL_ON 0
#endif
#ifndef XSPINBOX_ON
#define XSPINBOX_ON 0
#endif
#ifndef XABSTRACTSPINBOX_ON
#define XABSTRACTSPINBOX_ON 0
#endif
#ifndef XMESSAGEBOX_ON
#define XMESSAGEBOX_ON 0
#endif
#ifndef XERRORMESSAGE_ON
#define XERRORMESSAGE_ON 0
#endif
#ifndef XINPUTDIALOG_ON
#define XINPUTDIALOG_ON 0
#endif
#ifndef XCOLORDIALOG_ON
#define XCOLORDIALOG_ON 0
#endif
#ifndef XFILEDIALOG_ON
#define XFILEDIALOG_ON 0
#endif
#ifndef XPROGRESSDIALOG_ON
#define XPROGRESSDIALOG_ON 0
#endif
#ifndef XTOOLBAR_ON
#define XTOOLBAR_ON 0
#endif
#ifndef XMENUBAR_ON
#define XMENUBAR_ON 0
#endif
#ifndef XSTATUSBAR_ON
#define XSTATUSBAR_ON 0
#endif
#ifndef XSPLITTER_ON
#define XSPLITTER_ON 0
#endif
#ifndef XGROUPBOX_ON
#define XGROUPBOX_ON 0
#endif
#ifndef XCHECKBOX_ON
#define XCHECKBOX_ON 0
#endif
#ifndef XRADIOBUTTON_ON
#define XRADIOBUTTON_ON 0
#endif
#ifndef XTOOLBUTTON_ON
#define XTOOLBUTTON_ON 0
#endif
#ifndef XFONTCOMBOBOX_ON
#define XFONTCOMBOBOX_ON 0
#endif
#ifndef XDATETIMEEDIT_ON
#define XDATETIMEEDIT_ON 0
#endif
#ifndef XDATEEDIT_ON
#define XDATEEDIT_ON 0
#endif
#ifndef XTIMEEDIT_ON
#define XTIMEEDIT_ON 0
#endif
#ifndef XABSTRACTITEMVIEW_ON
#define XABSTRACTITEMVIEW_ON 0
#endif
#ifndef XABSTRACTITEMMODEL_ON
#define XABSTRACTITEMMODEL_ON 0
#endif
#ifndef XHEADERVIEW_ON
#define XHEADERVIEW_ON 0
#endif
#ifndef XCOMPLETER_ON
#define XCOMPLETER_ON 0
#endif
/* 按钮组必须保留——XAbstractButton.c:610 互斥组判定引用 XButtonGroup_*
 * 无守卫（类本体 641 行，小；未引用对象由 --gc-sections 回收）。 */
#ifndef XBUTTONGROUP_ON
#define XBUTTONGROUP_ON 1
#endif
#ifndef XKEYSEQUENCEEDIT_ON
#define XKEYSEQUENCEEDIT_ON 0
#endif
#ifndef XRUBBERBAND_ON
#define XRUBBERBAND_ON 0
#endif
#ifndef XSIZEGRIP_ON
#define XSIZEGRIP_ON 0
#endif
#ifndef XFOCUSFRAME_ON
#define XFOCUSFRAME_ON 0
#endif
#ifndef XSPLASHSCREEN_ON
#define XSPLASHSCREEN_ON 0
#endif
#ifndef XTOOLBOX_ON
#define XTOOLBOX_ON 0
#endif
/* 图形特效基类必须保留——XWidget 的 graphicsEffect API 无守卫引用
 * XGraphicsEffect_drawWidget/isEnabled（子类模糊/阴影/透明仍裁）。 */
#ifndef XGRAPHICSEFFECT_ON
#define XGRAPHICSEFFECT_ON 1
#endif
#ifndef XOFFSCREENSURFACE_ON
#define XOFFSCREENSURFACE_ON 0
#endif
/* 标题条必须保留——XWindowDecoration.c 无条件 include XTitleBar.h 并
 * 创建默认条实例（CSD 关闭只是不显示，类引用仍在）。 */
#ifndef XTITLEBAR_ON
#define XTITLEBAR_ON 1
#endif

/* 保留的小控件级联（架构师方案）：XFRAME/XLABEL/XABSTRACTBUTTON/
 * XPUSHBUTTON/XPROGRESSBAR/XSTACKEDWIDGET/XLAYOUT。注意 XMENU_ON=0 会
 * 级联裁掉 XPUSHBUTTON（XGuiConfig.h:919-925），故 XMENU=1 靠
 * --gc-sections 回收 XMenu 本体。 */
#ifndef XMENU_ON
#define XMENU_ON 1
#endif
#ifndef XLCDNUMBER_ON
#define XLCDNUMBER_ON 0
#endif
#ifndef XSCROLLBAR_ON
#define XSCROLLBAR_ON 0
#endif

/* ==================================================================== */
/* 7. 渲染内核：仅 RGB565                                                 */
/* ==================================================================== */
#ifndef XRENDERKERNEL_RGB565_ON
#define XRENDERKERNEL_RGB565_ON 1
#endif
#ifndef XRENDERKERNEL_RGB555_ON
#define XRENDERKERNEL_RGB555_ON 0
#endif
#ifndef XRENDERKERNEL_RGB888_ON
#define XRENDERKERNEL_RGB888_ON 0
#endif
#ifndef XRENDERKERNEL_BGR888_ON
#define XRENDERKERNEL_BGR888_ON 0
#endif
#ifndef XRENDERKERNEL_RGB32_ON
#define XRENDERKERNEL_RGB32_ON 0
#endif
#ifndef XRENDERKERNEL_RGBX8888_ON
#define XRENDERKERNEL_RGBX8888_ON 0
#endif
#ifndef XRENDERKERNEL_RGBA8888_ON
#define XRENDERKERNEL_RGBA8888_ON 0
#endif
#ifndef XRENDERKERNEL_ARGB32_ON
#define XRENDERKERNEL_ARGB32_ON 0
#endif
#ifndef XRENDERKERNEL_GRAYSCALE8_ON
#define XRENDERKERNEL_GRAYSCALE8_ON 0
#endif

/* ==================================================================== */
/* 8. Painter 子开关：全部保持默认（1）                                   */
/*    实测教训：XPainter.c 内 GPU/画刷路径引用大量无守卫成员             */
/*    （m_gpuActive×103、m_state.m_brush），关任何一档即编译残留；       */
/*    开着编译、未引用函数由 --gc-sections 回收，flash 零和。            */
/* ==================================================================== */

/* 字形备忘缓存槽位：默认 1024 槽 × 24B = 24KB 静态 .bss——ESP8266 dram
 * 段只有 80KB（0x3FFE8000..0x3FFFC000），实测直接溢出（.bss 起点
 * 0x3FFFE7D8 越界）。收到 16 槽（384B）：单屏 8x16 字形集 ≤128 个，
 * 冒烟场景逐 tile 重绘的缓存命中率损失可忽略（XPainter.c:13559
 * #ifndef 保护，编译期可覆盖）。 */
#ifndef XPAINTER_GLYPH_MEMO_SLOTS
#define XPAINTER_GLYPH_MEMO_SLOTS 16
#endif

/* 静态 RAM 收缩（ESP8266 80KB dram 段，heap 只剩 ~14KB，逐 KB 抠）：
 * 无文件系统/无网络/单线程 GUI——这些容量表按最小可用面收缩。 */
#ifndef XLFL_HP_MAX_THREADS
#define XLFL_HP_MAX_THREADS 16
#endif
#ifndef XLFL_RETIRE_CAPACITY
#define XLFL_RETIRE_CAPACITY 32
#endif
#ifndef XNETIORING_PROF_FD_SLOTS
#define XNETIORING_PROF_FD_SLOTS 16
#endif
#ifndef XFILE_MMAP_TRACK_MAX
#define XFILE_MMAP_TRACK_MAX 4
#endif
#ifndef XDEVICE_MAX_DEVICES
#define XDEVICE_MAX_DEVICES 8
#endif
/* XMultiPool / VariablePool 关闭：ESP8266 裸机堆上，XGui 的 XVarList
 * （信号/区域）走多级池的 power-of-two arena 增长在 create/paint 时刻
 * 按需触发——堆碎片化时 1536B 连续分配失败（实测 [mem] OOM n=1536/
 * 960 + StoreProhibited）。关闭后 XMultiPool_global 返回 NULL
 * （gui_stubs 打桩），XVarList.c:36 经 NULL 判空回落 SYSTEM 槽
 * （pvPortZalloc → 首次适应+合并释放的简单堆），对碎片鲁棒。
 * XCLASS 默认本就是 SYSTEM 槽；size_t 前置见文件头注释。 */
#ifndef XMEMORY_MULTIPOOL_ON
#define XMEMORY_MULTIPOOL_ON 0
#endif
#ifndef XMEMORY_VARIABLEPOOL_ON
#define XMEMORY_VARIABLEPOOL_ON 0
#endif
/* 固定块池保留（XMEMORY_FIXEDPOOL_ON 级联到 MULTIPOOL_ON，会连带裁掉
 * XFileDescriptor 的 XFIXEDPOOL_DEFINE —— 显式置 1 保住 fd 表）。 */
#ifndef XMEMORY_FIXEDPOOL_ON
#define XMEMORY_FIXEDPOOL_ON 1
#endif
/* MULTIPOOL_ON=0 时 XMultiPool.h 不声明 global_* 原型，但 XVarList.c:40
 * 无守卫取 XMultiPool_global_free 函数指针 —— 此处补声明（定义在
 * gui_stubs.c，返回 NULL 让 XVarList 走 SYSTEM 槽）。 */
#if !XMEMORY_MULTIPOOL_ON
struct XMultiPool;
struct XMultiPool* XMultiPool_global(void);
void* XMultiPool_global_malloc(unsigned size);
void XMultiPool_global_free(void* ptr);
void* XMultiPool_global_realloc(void* ptr, unsigned size);
void* XMultiPool_global_calloc(unsigned count, unsigned size);
int XMultiPool_global_isInited(void);
int XMultiPool_is_from_pool(struct XMultiPool* pool, void* ptr);
unsigned long XMultiPool_totalSize(struct XMultiPool* pool);
#endif
/* 全局池档位：仅 MULTIPOOL_ON=1 消费；关闭后无效，保留最小值兜底。 */
#ifndef XMP_GLOBAL_C32
#define XMP_GLOBAL_C32 8
#endif
#ifndef XMP_GLOBAL_C64
#define XMP_GLOBAL_C64 4
#endif
#ifndef XMP_GLOBAL_C128
#define XMP_GLOBAL_C128 2
#endif
#ifndef XMP_GLOBAL_C256
#define XMP_GLOBAL_C256 1
#endif
#ifndef XMP_GLOBAL_C512
#define XMP_GLOBAL_C512 1
#endif

/* fd 固定池：128→16（2092B→~262B；无文件系统，stdio/串口只用 3 个 fd）。
 * 裸机直入模式回收静态 RAM 给 XGui（XGuiApplication 单例即占 ~25KB，
 * ESP8266 每 KB 关键）。 */
#ifndef XFD_TABLE_SIZE
#define XFD_TABLE_SIZE 16
#endif

/* CONT 专用栈：4KB = 实测完整首绘链路组合（SDK 启动链后 boot 栈仅
 * ~2KB 余量；更大栈的实验因 irom 取指异常回退，见 build_gui.sh 注释）。 */
#ifndef CONT_STACKSIZE
#define CONT_STACKSIZE 4096
#endif

/* ==================================================================== */
/* 9. XFont 字体组（Src/XData/XFont/XFont_config.h）                     */
/* ==================================================================== */

/* XFONT_FREETYPE_ON 是 XFont 模块总闸（陷阱：置 0 连点阵字库一起裁，
 * XFont.c:6 / XFont8x16.c:2 顶部守卫）；它默认 1，这里显式钉住。 */
#ifndef XFONT_FREETYPE_ON
#define XFONT_FREETYPE_ON 1
#endif
/* 轮廓字与字体文件通道关闭（级联 XFONT_FT_FACE_ON=0，
 * XFont_config.h:181-191）。 */
#ifndef XFONT_OUTLINE_ON
#define XFONT_OUTLINE_ON 0
#endif
#ifndef XFONT_FILE_ON
#define XFONT_FILE_ON 0
#endif
/* 内置 8x16 点阵：非桌面默认 0，须显式开。 */
#ifndef XFONT_BUILTIN_8X16_ON
#define XFONT_BUILTIN_8X16_ON 1
#endif
#ifndef XFONT_BUILTIN_16X16_ON
#define XFONT_BUILTIN_16X16_ON 0
#endif
#ifndef XFONT_BUILTIN_32X32_ON
#define XFONT_BUILTIN_32X32_ON 0
#endif
#ifndef XFONT_MAX_BITMAP_PROVIDERS
#define XFONT_MAX_BITMAP_PROVIDERS 1
#endif

/* ==================================================================== */
/* 10. 容量收缩                                                         */
/* ==================================================================== */
#ifndef XTUI_SCREEN_MAX_COLUMNS
#define XTUI_SCREEN_MAX_COLUMNS 21
#endif
#ifndef XTUI_SCREEN_MAX_ROWS
#define XTUI_SCREEN_MAX_ROWS 8
#endif

/* UTF-8 直通串口，不做 GBK 转换：不定义 SYSTEM，改选 CODE 模式
 * （XCharFreeRTOS.c 用 defined() 判断，定义成 0 仍命中 #error 分支）。 */
#ifndef XCHAR_USE_SYSTEM_GBK
#undef XCHAR_USE_SYSTEM_GBK
#endif
#ifndef XCHAR_USE_CODE_GBK
#define XCHAR_USE_CODE_GBK 1
#endif

/* ==================================================================== */
/* 11. FreeRTOS V9 内核调参                                              */
/* ==================================================================== */

/* 本固件不用 FreeRTOS 堆（heap_4 不入构建，pvPortMalloc 由板级
 * gui_stubs.c 的 [_heap_start,0x3FFFC000) 首次适应堆承担，带 free），
 * configTOTAL_HEAP_SIZE 不消费；保留定义仅为 FreeRTOSConfig.h 兜底。 */
#ifndef configTOTAL_HEAP_SIZE
#define configTOTAL_HEAP_SIZE ((size_t)(16 * 1024))
#endif

#include "FreeRTOSConfig.h"
#undef configMAX_PRIORITIES
#define configMAX_PRIORITIES (8)
#undef configUSE_PORT_OPTIMISED_TASK_SELECTION
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0

#endif /* XGUI_ESP8266_SSD1306_H */
