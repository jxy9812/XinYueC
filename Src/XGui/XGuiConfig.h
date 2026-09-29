/****************************************************************************
 * @file       XGuiConfig.h
 * @brief      XGui 模块总开关与子功能配置。
 * @details    CXinYueConfig.h 只保留 XGUI_ON 总开关入口；所有 GUI 子开关
 *             在本文件集中定义。嵌入式构建只需 -DXGUI_ON=0 即可裁剪整个
 *             XGui，同时保留各子开关名称供桌面构建做精细裁剪。
 ****************************************************************************/
#ifndef XGUICONFIG_H
#define XGUICONFIG_H

#include "CXinYueConfig.h"

#ifndef XGUI_ON
#define XGUI_ON 1
#endif

/* 图像与窗口基础模块。 */
#ifndef XIMAGECODEC_ON
#define XIMAGECODEC_ON 1
#endif
/* 解码图像字节预算 LRU 缓存（XImageCache）。默认 1（桌面与嵌入式
 * Linux 都受益：图标/SVG 反复加载免重复解码；预算见 XImageCache.h，
 * 桌面默认 1MB、裸机/RTOS 默认 0 编译期完全裁剪）；需要极限省 RAM 的
 * 构建显式置 0（load 直通，零运行时开销）。 */
#ifndef XIMAGECACHE_ON
#define XIMAGECACHE_ON 1
#endif
#ifndef XSCREEN_ON
#define XSCREEN_ON 1
#endif
#ifndef XSURFACEFORMAT_ON
#define XSURFACEFORMAT_ON 1
#endif
#ifndef XCURSOR_ON
#define XCURSOR_ON 1
#endif
#ifndef XWINDOW_ON
#define XWINDOW_ON 1
#endif
#ifndef XACCESSIBLE_ON
#define XACCESSIBLE_ON 1
#endif

/* 应用、样式、剪贴板与 MIME。 */
#ifndef XGUIAPPLICATION_ON
#define XGUIAPPLICATION_ON 1
#endif
#ifndef XSTYLEHINTS_ON
#define XSTYLEHINTS_ON 1
#endif
#ifndef XSTYLE_ON
#define XSTYLE_ON 1
#endif
#ifndef XCLIPBOARD_ON
#define XCLIPBOARD_ON 1
#endif
#ifndef XMIMEDATA_ON
#define XMIMEDATA_ON 1
#endif
#ifndef XPALETTE_ON
#define XPALETTE_ON 1
#endif
#ifndef XIMAGEIOPLUGIN_ON
#define XIMAGEIOPLUGIN_ON 1
#endif
/* 独立可裁剪的可选组件（guard-review-0020 批次 1/2）：置 0 裁剪对应
 * 模块公共 API 与实现；引用侧按 XBackingStore 回退模式退化——
 * XSVGICON_ON=0 裁剪 SVG 图标引擎/插件（全仓零外部引用）；
 * XPIXMAPCACHE_ON=0 时 XIconScaledPixmapCache 退化为永久未命中/
 * 拒绝插入/空清理；XMOVIE_ON=0 时 XLabel 的影片分支不参与尺寸
 * 计算与绘制，movie()/setMovie() 保留借用指针语义。 */
#ifndef XSVGICON_ON
#define XSVGICON_ON 1
#endif
#ifndef XPIXMAPCACHE_ON
#define XPIXMAPCACHE_ON 1
#endif
#ifndef XMOVIE_ON
#define XMOVIE_ON 1
#endif

/* 平台集成与平台资源。 */
#ifndef XPLATFORMINTEGRATION_ON
#define XPLATFORMINTEGRATION_ON 1
#endif
/* 统一 GPU 运行时（XGpu）总开关：很多嵌入式目标没有 GPU，置 0 可整体
 * 裁剪 XGpu 公共层与 XGuiApplication 的共享 GPU 入口（QGuiApplication::
 * rhi() 对齐物）；XGui 默认软件渲染（XBackingStore）不受影响，XPlatformGraphics
 * 的平台能力探测（isOpenGLAvailable 等）在无 GPU 平台由 Drive 存根返回 false。 */
#ifndef XGPU_ON
#define XGPU_ON 1
#endif
/* GPU 运行时默认口径：在没有任何外部设置（XGUI_RENDER_BACKEND /
 * XGPU_BACKEND 环境变量、--gpu/--software 命令行、宿主
 * addRequestedOverride 运行期覆盖）时，是否默认请求 GPU 直通。
 *   桌面系统（Windows/Linux/macOS/BSD）默认 1 —— 启动即请求 GPU，
 *     平台探测失败（无 GL/Vulkan 上下文、FBO 不完整）自动回退
 *     软件光栅（零回归契约，见 XGpuRenderBackend_requested 注）；
 *   裸机 / RTOS / 裁剪构建（XGPU_ON=0）默认 0 —— 纯软件渲染。
 * 外部覆盖：编译期 #define XGPU_RUNTIME_DEFAULT_ON 0/1（先于本
 * 头文件定义即生效）；运行期设 XGUI_RENDER_BACKEND=software 强制
 * 软件、=opengl 强制 GPU（运行期设置优先于本默认值）。 */
#ifndef XGPU_RUNTIME_DEFAULT_ON
#if XGPU_ON && XPLATFORMINTEGRATION_ON && XPLATFORM_DESKTOP
/* 暂时回退软件默认（2026-09-24 用户指令）：GPU 子矩形批量化 WIP
 * （87213b92）存在闪烁 + 帧率回退（数百帧），修复并发专项完成前
 * 桌面默认保持软件光栅；WIP 修完后恢复 1。 */
#define XGPU_RUNTIME_DEFAULT_ON 0
#else
#define XGPU_RUNTIME_DEFAULT_ON 0
#endif
#endif
#ifndef XPIXMAP_ON
#define XPIXMAP_ON 1
#endif
#ifndef XPLATFORMNATIVEINTERFACE_ON
#define XPLATFORMNATIVEINTERFACE_ON 1
#endif
#ifndef XPLATFORMWINDOW_ON
#define XPLATFORMWINDOW_ON 1
#endif
#ifndef XPLATFORMINPUTCTX_ON
#define XPLATFORMINPUTCTX_ON 1
#endif
#ifndef XBACKINGSTORE_ON
#define XBACKINGSTORE_ON 1
#endif
#ifndef XPLATFORMBACKINGSTORE_ON
#define XPLATFORMBACKINGSTORE_ON 1
#endif
/* 可复用软件后备存储模板开关（对标 Drive/Unsupported 存根的全功能替代）：
 * 置 1 时在非 Linux/Windows 平台编译 Drive/Software/Graphics/
 * XPlatformBackingStore_software.c，提供完整软件缓冲 + present 回调，
 * 新平台只需登记显示驱动回调即可上屏；置 0 时保持 Unsupported 空后端。 */
#ifndef XPLATFORMBACKINGSTORE_SOFTWARE_ON
#define XPLATFORMBACKINGSTORE_SOFTWARE_ON 0
#endif
/* 后备存储模式在下方即需根据原生窗口能力选择，因此默认值必须先于
 * 该选择定义；调用方通过编译选项预先置 0 时仍可裁剪原生窗口路径。 */
#ifndef XPLATFORMNATIVEWINDOW_ON
#define XPLATFORMNATIVEWINDOW_ON 1
#endif

/* 后备存储渲染模式（参考 LVGL 9 的 PARTIAL/DIRECT/FULL）。
 * PARTIAL：使用小块 tile buffer，逐片绘制并提交；DIRECT：整屏双缓冲，
 * 绘制脏区后交换；FULL：每次绘制并提交整屏。 */
#define XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL 0
#define XGUI_BACKINGSTORE_RENDER_MODE_DIRECT  1
#define XGUI_BACKINGSTORE_RENDER_MODE_FULL    2
#ifndef XGUI_BACKINGSTORE_RENDER_MODE
#if defined(_WIN32) || (defined(__linux__) && XPLATFORMNATIVEWINDOW_ON && \
                        !XPLATFORM_FBDEV_ON)
/* 桌面原生窗口需要持久整帧缓冲：先在完整帧上合成脏区，再一次提交，
 * 与 Qt QBackingStore 的可见帧边界一致。PARTIAL tile 会逐块 present，
 * 在 X11 的高频小区域更新中可见为闪烁。
 * 【合并裁决 2026-09-29 VK 夜战】桌面分支改回 FULL：DIRECT 桌面默认下
 * demo --benchmark 泵对所有后端（SW/GL/VK）空转挂死（CPU~0.2s 不泵帧，
 * 三复现），真机验证未覆盖桌面基准路径。桌面 DIRECT 的 Qt 对齐语义
 * 待上游修好桌面泵后再评估重落。 */
#define XGUI_BACKINGSTORE_RENDER_MODE XGUI_BACKINGSTORE_RENDER_MODE_FULL
#else
/* 嵌入式 fbdev 直写（XPLATFORM_FBDEV_ON）：与桌面同为 DIRECT——
 * 脏区绘制 + 硬件双缓冲轮换翻页（pan+FB_ACTIVATE_VBL 防撕裂）+
 * present 差带同步账本（只搬上一帧落笔行带，防两缓冲失步闪烁，
 * 成本 ∝ 脏区）+ 弹层遮挡剔除。真机定版记录（昆仑通态 A33）：
 * - 2026-09-26/28 曾定版 FULL：当时 DIRECT 的「交替闪烁/内容缺失」
 *   判据实为被污染的证据链——交互验证所用的触摸注入器 tap 缺失
 *   释放沿（按钮臂化后永无 clicked，页面从不切换，旧内容滞留被
 *   误读为渲染缺陷），叠加当时 present 每帧整搬的 CPU 成本；
 * - 2026-09-28 注入器修复 + 差带同步落地后 DIRECT+2 真机复验：
 *   九页切换/弹层/最大化/启动期交互全部内容完整，菜单打开态跨
 *   tick 帧差分为 0（防闪烁语义保持），小交互 CPU 42%→9%、
 *   菜单态 76%→1.8%、空闲 ~5%（原 FULL：37%/76%/7%）。
 *   PARTIAL tile 缓冲与整页静态场景 blit 管线不兼容（大块黑屏），
 *   维持否决。 */
#define XGUI_BACKINGSTORE_RENDER_MODE XGUI_BACKINGSTORE_RENDER_MODE_DIRECT
#endif

/* DIRECT/FULL 模式使用的整屏缓冲数量；至少 1，DIRECT 推荐 2。
 * fbdev 影子缓冲架构下用 1 块：FULL 每次提交整窗，单缓冲足够。 */
#ifndef XGUI_BACKINGSTORE_BUFFER_COUNT
#define XGUI_BACKINGSTORE_BUFFER_COUNT 1
#endif
#endif

/* PARTIAL 每块绘制缓冲的最大尺寸。窗口边缘的最后一块会按窗口尺寸
 * 裁剪，但缓冲按此上限分配，避免每片重复重建 XImage 描述。 */
#ifndef XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH
#define XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH 160
#endif
#ifndef XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT
#define XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT 80
#endif

/* 交互限帧（低性能设备调优参数；时间源一律 XDateTime_currentMSecs
 * SinceEpoch 单调毫秒，与 GPU 直通既有限频/PARTIAL 攒批帧界同源）。
 * - XGUI_RESIZE_REPAINT_MAX_FPS：窗口手势（装饰拖拽移动+改尺寸）的落
 *   地+重绘节奏上限（挂点 XWindowDecoration MOUSE_MOVE 两分支，整函数
 *   闸——被跳过的中间位移由「按下锚+总位移」无状态重算天然合并，松手
 *   补尾帧零丢失）。用户裁定 2026-09-29：拖拽移动与改尺寸同归本宏。
 *   只约束装饰拖拽路径；桌面 WM 管理的移动/改尺寸不经过该路径。
 * - XGUI_PRESENT_MAX_FPS：整体上屏节奏上限（挂点 XWidget_flushBacking
 *   Store 决策段，GPU/软件全部提交腿同闸；首绘/EXPOSE 整窗帧与降级
 *   恢复帧永不跳，skip 帧把提交矩形并回脏区账本、尾帧结构性必达）。
 *   默认 0=不限；运行期环境变量 XGPU_PRESENT_MAX_FPS 仍可覆盖 present
 *   限频值（未设用宏值，设了覆盖宏）。手势移动的直提上屏不经 flush，
 *   只受 RESIZE 宏约束（本宏管不到）。
 * 两宏取值 0=不限（RESIZE 侧使用面 #if 编译期裁掉限频代码；负值由下
 * 方钳制收敛为 0）。分工不可互替：PRESENT 限不住改尺寸的全窗脏帧
 * （coversFull 整窗帧永不跳），改尺寸/拖拽移动限帧必须用 RESIZE 宏。
 * 调法：默认 RESIZE=60（用户裁定 2026-09-30：交付包取 60 档——拖拽
 * 跟手不步进，CPU 由手势提交链收窄差带账本裁冗兜住；2026-09-29 深夜
 * 曾定 15（A33 实测每落地步 ~28ms 是全布局+合成+差带的固有成本，30
 * 档连续快拖仍 80%，15 档减半），60 档要求以账本差带裁冗为前提）。 */
#ifndef XGUI_RESIZE_REPAINT_MAX_FPS
#define XGUI_RESIZE_REPAINT_MAX_FPS 60
#endif
#ifndef XGUI_PRESENT_MAX_FPS
#define XGUI_PRESENT_MAX_FPS 0
#endif

/* 窗口表面（后备存储）像素格式的编译期选择器（对标 Qt QBackingStore
 * 随目标窗口/屏幕格式协商缓冲格式的行为：Qt 由平台窗口报告格式后按
 * 其分配缓冲；嵌入式目标的面板像素接口在出厂时固定，没有运行期协商
 * 的必要，因此这里用编译期开关等价表达）：
 * - 0（默认）：XImageFormat_ARGB32_Premultiplied，与既有行为逐位一致；
 * - 1：XImageFormat_RGB16（RGB565，每像素 2 字节），供无 Alpha 的
 *   16 位面板嵌入式目标把表面内存与带宽省一半。
 * 注意：本文件只提供 0/1 布尔选择器，不得 include XImageFormat.h——
 * 配置头必须保持叶子（XImageFormat.h 体系经本文件入口聚合，反向包含
 * 会形成包含环）；选择器到 XImageFormat 枚举值的映射由使用方
 * （XPlatformBackingStore.c 的 XPBS_IMAGE_FORMAT）完成。 */
#ifndef XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 0
#endif

/* 可选的平台启动帧缓冲。应用或板级配置可以在编译选项中把这三个
 * 宏替换为静态显存地址和容量；平台后端创建 XPlatformBackingStore
 * 时只登记一次，后续 resize 只重建 XImage 描述，不重复绑定缓冲。
 * 默认值为 0，表示由平台后端按需从内部内存分配。 */
#ifndef XGUI_BACKINGSTORE_BUFFER1
#define XGUI_BACKINGSTORE_BUFFER1 0
#endif
#ifndef XGUI_BACKINGSTORE_BUFFER2
#define XGUI_BACKINGSTORE_BUFFER2 0
#endif
#ifndef XGUI_BACKINGSTORE_BUFFER_SIZE
#define XGUI_BACKINGSTORE_BUFFER_SIZE 0
#endif

#if XGUI_BACKINGSTORE_RENDER_MODE < XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL || \
    XGUI_BACKINGSTORE_RENDER_MODE > XGUI_BACKINGSTORE_RENDER_MODE_FULL
#undef XGUI_BACKINGSTORE_RENDER_MODE
#define XGUI_BACKINGSTORE_RENDER_MODE XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
#endif
#if XGUI_BACKINGSTORE_BUFFER_COUNT < 1
#undef XGUI_BACKINGSTORE_BUFFER_COUNT
#define XGUI_BACKINGSTORE_BUFFER_COUNT 1
#endif
#if XGUI_BACKINGSTORE_BUFFER_COUNT > 2
/* 当前后备存储实现支持单缓冲或双缓冲；更大的值会造成配置与实际
 * 存储数量不一致，因此统一收敛到双缓冲。 */
#undef XGUI_BACKINGSTORE_BUFFER_COUNT
#define XGUI_BACKINGSTORE_BUFFER_COUNT 2
#endif
#if XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH < 1
#undef XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH
#define XGUI_BACKINGSTORE_PARTIAL_BUFFER_WIDTH 1
#endif
#if XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT < 1
#undef XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT
#define XGUI_BACKINGSTORE_PARTIAL_BUFFER_HEIGHT 1
#endif
/* 限帧宏防呆：负值（含笔误）收敛为 0=不限；0 本身合法，无上限钳制。 */
#if XGUI_RESIZE_REPAINT_MAX_FPS < 0
#undef XGUI_RESIZE_REPAINT_MAX_FPS
#define XGUI_RESIZE_REPAINT_MAX_FPS 0
#endif
#if XGUI_PRESENT_MAX_FPS < 0
#undef XGUI_PRESENT_MAX_FPS
#define XGUI_PRESENT_MAX_FPS 0
#endif
/* fbdev 影子缓冲豁免：fb 直写无翻页轮换，双缓冲只会多分配一块
 * 1.2MB 影子并触发无意义的交替+整搬（昆仑通态 A7 实测）。 */
#if XGUI_BACKINGSTORE_RENDER_MODE == XGUI_BACKINGSTORE_RENDER_MODE_DIRECT && \
    !XPLATFORM_FBDEV_ON && XGUI_BACKINGSTORE_BUFFER_COUNT < 2
#undef XGUI_BACKINGSTORE_BUFFER_COUNT
#define XGUI_BACKINGSTORE_BUFFER_COUNT 2
#endif

/* 绘图与窗口基础模块。 */
#ifndef XPAINTDEVICE_ON
#define XPAINTDEVICE_ON 1
#endif
#ifndef XPAINTER_ON
#define XPAINTER_ON 1
#endif
/* 目标格式渲染内核（XRenderKernel 族）逐格式编译开关：嵌入式只编译
 * 目标面板实际需要的格式，其余内核零代码生成（ROM 纪律）。总门控
 * XPAINTER_ON=0 时全部连带裁剪；XRenderKernel 注册中心与五接缝分派
 * 不受各格式开关影响（未编译格式的槽位为空，painter 自动回退逐像素
 * 路径——零回归语义与 XRenderKernel.h 契约一致）。
 * 各格式开关默认值对齐 fbdev 面板协商能力（XPlatformFramebuffer 的
 * formatFromVar 映射集）：16/24/32 位面板常用格式默认开；Grayscale8
 * （单色 OLED/墨水屏）与 RGB555（15 位面板）默认开但可关。 */
#ifndef XRENDERKERNEL_RGB565_ON
#define XRENDERKERNEL_RGB565_ON 1
#endif
#ifndef XRENDERKERNEL_RGB555_ON
#define XRENDERKERNEL_RGB555_ON 1
#endif
#ifndef XRENDERKERNEL_RGB888_ON
#define XRENDERKERNEL_RGB888_ON 1
#endif
#ifndef XRENDERKERNEL_BGR888_ON
#define XRENDERKERNEL_BGR888_ON 1
#endif
#ifndef XRENDERKERNEL_RGB32_ON
#define XRENDERKERNEL_RGB32_ON 1
#endif
#ifndef XRENDERKERNEL_RGBX8888_ON
#define XRENDERKERNEL_RGBX8888_ON 1
#endif
#ifndef XRENDERKERNEL_RGBA8888_ON
#define XRENDERKERNEL_RGBA8888_ON 1
#endif
#ifndef XRENDERKERNEL_ARGB32_ON
#define XRENDERKERNEL_ARGB32_ON 1
#endif
#ifndef XRENDERKERNEL_GRAYSCALE8_ON
#define XRENDERKERNEL_GRAYSCALE8_ON 1
#endif
#ifndef XPAINTER_SHAPE_ON
#define XPAINTER_SHAPE_ON 1
#endif
#ifndef XPAINTER_POLYGON_ON
#define XPAINTER_POLYGON_ON 1
#endif
#ifndef XPAINTER_PENSTYLE_ON
#define XPAINTER_PENSTYLE_ON 1
#endif
#ifndef XPAINTER_BRUSH_ON
#define XPAINTER_BRUSH_ON 1
#endif
#ifndef XPAINTER_BRUSH_ORIGIN_ON
#define XPAINTER_BRUSH_ORIGIN_ON 1
#endif
#ifndef XPAINTER_BACKGROUND_ON
#define XPAINTER_BACKGROUND_ON 1
#endif
#ifndef XPAINTER_PIXMAP_ON
#define XPAINTER_PIXMAP_ON 1
#endif
#ifndef XPAINTER_IMAGE_RECT_ON
#define XPAINTER_IMAGE_RECT_ON 1
#endif
#ifndef XPAINTER_TILED_PIXMAP_ON
#define XPAINTER_TILED_PIXMAP_ON 1
#endif
#ifndef XPAINTER_PATH_ON
#define XPAINTER_PATH_ON 1
#endif
#ifndef XPAINTER_TEXTLAYOUT_ON
#define XPAINTER_TEXTLAYOUT_ON 1
#endif
#ifndef XPAINTER_LAYOUT_DIRECTION_ON
#define XPAINTER_LAYOUT_DIRECTION_ON 1
#endif
#ifndef XPAINTER_RENDERHINT_ON
#define XPAINTER_RENDERHINT_ON 1
#endif
#ifndef XPAINTER_WORLD_MATRIX_ON
#define XPAINTER_WORLD_MATRIX_ON 1
#endif
#ifndef XPAINTER_VIEW_TRANSFORM_ON
#define XPAINTER_VIEW_TRANSFORM_ON 1
#endif
#ifndef XPAINTER_CLIP_ON
#define XPAINTER_CLIP_ON 1
#endif
#ifndef XPAINTER_CLIP_REGION_ON
#define XPAINTER_CLIP_REGION_ON 1
#endif

/* 控件、布局、输入法与窗口事件。 */
#ifndef XAPPLICATION_ON
#define XAPPLICATION_ON 1
#endif
#ifndef XWIDGET_ON
#define XWIDGET_ON 1
#endif
#ifndef XPROGRESSBAR_ON
#define XPROGRESSBAR_ON 1
#endif
#ifndef XGROUPBOX_ON
#define XGROUPBOX_ON 1
#endif
#ifndef XABSTRACTSLIDER_ON
#define XABSTRACTSLIDER_ON 1
#endif
#ifndef XSLIDER_ON
#define XSLIDER_ON 1
#endif
#ifndef XDIAL_ON
#define XDIAL_ON 1
#endif
#ifndef XCOMBOBOX_ON
#define XCOMBOBOX_ON 1
#endif
#ifndef XTABBAR_ON
#define XTABBAR_ON 1
#endif
#ifndef XTABWIDGET_ON
#define XTABWIDGET_ON 1
#endif
#ifndef XLCDNUMBER_ON
#define XLCDNUMBER_ON 1
#endif
#ifndef XSCROLLBAR_ON
#define XSCROLLBAR_ON 1
#endif
#ifndef XSTACKEDWIDGET_ON
#define XSTACKEDWIDGET_ON 1
#endif
#ifndef XBUTTONGROUP_ON
#define XBUTTONGROUP_ON 1
#endif
#ifndef XSTATUSBAR_ON
#define XSTATUSBAR_ON 1
#endif
#ifndef XDIALOGBUTTONBOX_ON
#define XDIALOGBUTTONBOX_ON 1
#endif
#ifndef XSPLITTER_ON
#define XSPLITTER_ON 1
#endif
#ifndef XSIZEGRIP_ON
#define XSIZEGRIP_ON 1
#endif
#ifndef XRUBBERBAND_ON
#define XRUBBERBAND_ON 1
#endif
#ifndef XFOCUSFRAME_ON
#define XFOCUSFRAME_ON 1
#endif
#ifndef XSPLASHSCREEN_ON
#define XSPLASHSCREEN_ON 1
#endif
#ifndef XMESSAGEBOX_ON
#define XMESSAGEBOX_ON 1
#endif
#ifndef XMDIAREA_ON
#define XMDIAREA_ON 1
#endif
#ifndef XCALENDARWIDGET_ON
#define XCALENDARWIDGET_ON 1
#endif
#ifndef XTEXTBROWSER_ON
#define XTEXTBROWSER_ON 1
#endif
#ifndef XKEYSEQUENCEEDIT_ON
#define XKEYSEQUENCEEDIT_ON 1
#endif
#ifndef XTEXTEDIT_ON
#define XTEXTEDIT_ON 1
#endif
#ifndef XPLATFORMFONTDATABASE_ON
#define XPLATFORMFONTDATABASE_ON 1
#endif
#ifndef XWIZARD_ON
#define XWIZARD_ON 1
#endif
#ifndef XERRORMESSAGE_ON
#define XERRORMESSAGE_ON 1
#endif
#ifndef XTEXTDOCUMENT_ON
#define XTEXTDOCUMENT_ON 1
#endif
#ifndef XTEXTUTF8_ON
#define XTEXTUTF8_ON 1
#endif
#ifndef XTEXTCLIPBOARD_ON
#define XTEXTCLIPBOARD_ON 1
#endif
#ifndef XTEXTMENU_ON
#define XTEXTMENU_ON 1
#endif
#ifndef XLINECONTROL_ON
#define XLINECONTROL_ON 1
#endif
#ifndef XTEXTCONTROL_ON
#define XTEXTCONTROL_ON 1
#endif
#ifndef XTABLEWIDGET_ON
#define XTABLEWIDGET_ON 1
#endif
#ifndef XCHARTS_ON
#define XCHARTS_ON 1
#endif
/* XChartView 静态层缓存与渲染剖析（§10.2 第一期，对标 Qt
 * QGraphicsItem::DeviceCoordinateCache）：STATIC_LAYER_ON 置 1 时把
 * 背景/背景笔/标题/坐标轴网格五件套渲进控件私有离屏层，入口指纹命中
 * 直接 blit、未命中重建；置 0 时渲染路径与无层现状逐位一致。
 * PROFILE 置 1 时在 xcv_renderToImage 埋五段计时，但运行期还须
 * XCHARTVIEW_PROFILE 环境变量非 0 才输出每秒均值（不污染正常基准）。
 * 两开关随 XGUI_ON=0 级联裁剪（见文件尾总开关区块）。 */
#ifndef XCHARTVIEW_STATIC_LAYER_ON
#define XCHARTVIEW_STATIC_LAYER_ON 1
#endif
#ifndef XCHARTVIEW_PROFILE
#define XCHARTVIEW_PROFILE 1
#endif
#ifndef XDIALOG_ON
#define XDIALOG_ON 1
#endif
#ifndef XDOCKWIDGET_ON
#define XDOCKWIDGET_ON 1
#endif
#ifndef XMAINWINDOW_ON
#define XMAINWINDOW_ON 1
#endif
#ifndef XTOOLBOX_ON
#define XTOOLBOX_ON 1
#endif
#ifndef XABSTRACTSCROLLAREA_ON
#define XABSTRACTSCROLLAREA_ON 1
#endif
#ifndef XSCROLLAREA_ON
#define XSCROLLAREA_ON 1
#endif
#ifndef XDATETIMEEDIT_ON
#define XDATETIMEEDIT_ON 1
#endif
#ifndef XFONTCOMBOBOX_ON
#define XFONTCOMBOBOX_ON 1
#endif
#ifndef XPLAINTEXTEDIT_ON
#define XPLAINTEXTEDIT_ON 1
#endif
#ifndef XMENUBAR_ON
#define XMENUBAR_ON 1
#endif
#ifndef XTOOLBAR_ON
#define XTOOLBAR_ON 1
#endif
#ifndef XLINEEDIT_ON
#define XLINEEDIT_ON 1
#endif
#ifndef XABSTRACTSPINBOX_ON
#define XABSTRACTSPINBOX_ON 1
#endif
#ifndef XSPINBOX_ON
#define XSPINBOX_ON 1
#endif
#ifndef XFRAME_ON
#define XFRAME_ON 1
#endif
#ifndef XLABEL_ON
#define XLABEL_ON 1
#endif
#ifndef XABSTRACTBUTTON_ON
#define XABSTRACTBUTTON_ON 1
#endif
#ifndef XPUSHBUTTON_ON
#define XPUSHBUTTON_ON 1
#endif
#ifndef XCHECKBOX_ON
#define XCHECKBOX_ON 1
#endif
#ifndef XRADIOBUTTON_ON
#define XRADIOBUTTON_ON 1
#endif
#ifndef XCOMMANDLINKBUTTON_ON
#define XCOMMANDLINKBUTTON_ON 1
#endif
#ifndef XMENU_ON
#define XMENU_ON 1
#endif
#ifndef XTOOLBUTTON_ON
#define XTOOLBUTTON_ON 1
#endif
#ifndef XLAYOUT_ON
#define XLAYOUT_ON 1
#endif
#ifndef XLAYOUT_STACKED_ON
#define XLAYOUT_STACKED_ON 1
#endif
#ifndef XINPUTMETHOD_ON
#define XINPUTMETHOD_ON 1
#endif
#ifndef XWINDOWEVENT_ON
#define XWINDOWEVENT_ON 1
#endif
#ifndef XWINDOWSYSTEMINTERFACE_ON
#define XWINDOWSYSTEMINTERFACE_ON 1
#endif

/* 性能悬浮层（基于 XLabel，供 demo 和平台验收使用）。桌面默认开启，
 * 嵌入式可通过 -DXGUI_PERFORMANCE_OVERLAY_ON=0 完全裁剪；各项指标也可
 * 独立关闭，避免引入不需要的格式化和采样开销。 */
#ifndef XGUI_PERFORMANCE_OVERLAY_ON
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
#define XGUI_PERFORMANCE_OVERLAY_ON 1
#else
#define XGUI_PERFORMANCE_OVERLAY_ON 0
#endif
#endif
#ifndef XGUI_PERFORMANCE_OVERLAY_FPS_ON
#define XGUI_PERFORMANCE_OVERLAY_FPS_ON 1
#endif
#ifndef XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
#define XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON 1
#endif
#ifndef XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
#define XGUI_PERFORMANCE_OVERLAY_NETWORK_ON 1
#endif
/* CPU/GPU 系统负载行（XSystem_cpuUsagePercent/gpuUsagePercent 采样；
 * 底层采集另受 XSYSTEM_CPU_USAGE_ON/XSYSTEM_GPU_USAGE_ON 约束）。 */
#ifndef XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
#define XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON 1
#endif
/* 内存使用量行（准确数+百分比，显示种类与数据来源可配；OS 平台经
 * XSystem_memoryInfo 采样，库内来源经 XMemory_statistics 聚合；底层
 * 采集另受 XSYSTEM_MEMORY_USAGE_ON/XMEMORY_STATISTICS_ON 约束）。 */
#ifndef XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
#define XGUI_PERFORMANCE_OVERLAY_MEMORY_ON 1
#endif
#ifndef XGUI_PERFORMANCE_OVERLAY_UPDATE_MS
#define XGUI_PERFORMANCE_OVERLAY_UPDATE_MS 250
#endif

/* 控件静态内容保留层（retained layer，融合 LVGL 静态内容缓存思想）字节
 * 预算：显式 XWidget_setContentRetained(true) 的控件把"自身+可见子树"的
 * 渲染输出缓存为离屏 ARGB32_Premultiplied 图像，后续帧脏区与控件相交时
 * 直接从缓存 blit 并跳过 paintEvent 派发。全部保留层缓存字节总量超过本
 * 预算时按 LRU（最近访问时间戳）淘汰最久未用的保留层；预算不足/分配
 * 失败的控件自动退回常规绘制（不阻塞）。默认 2MB；置 0 时任何保留层
 * 缓存都无法建立（开关退化为直通，等价整体关闭）。 */
#ifndef XGUI_RETAINED_LAYER_BUDGET_BYTES
#define XGUI_RETAINED_LAYER_BUDGET_BYTES (2u * 1024u * 1024u)
#endif

/* 窗口装饰与自定义标题条策略（框架自绘 CSD 标题栏体系，见
 * XWindowDecoration/XTitleBar/XPlatformThemeDecoration）。 */
/* 控件级自定义标题条开关：置 1 时提供 XWidget_setTitleBarWidget/
 * titleBarWidget 控件级通用槽（顶层窗口=装饰系统接管为窗口标题条，
 * XDockWidget=停靠标题条经宏复用同一 API）以及装饰模块的自定义条
 * 分支；置 0 时上述 API、struct 成员与宏映射整体裁剪，XDockWidget
 * 回归自有实现。框架默认标题条控件 XTitleBar 不受本开关门控（无
 * WM 设备的框架自绘条本身就用它）。 */
#ifndef XGUI_CUSTOM_TITLEBAR_ON
#define XGUI_CUSTOM_TITLEBAR_ON 1
#endif
/* 桌面 CSD 编译默认策略：桌面会话（有原生窗口管理器）下框架自绘
 * 标题栏（client-side decorations）默认开启（用户裁定 2026-09-29）=
 * 多平台标题栏统一——桌面与无 WM 设备（fbdev 直写面板）同走框架自
 * 绘，平台层按抑制位关掉原生 WM 装饰（无双栏，posix 管线见
 * XPlatformNativeWindow_posix.c）。运行期可用环境变量 XGUI_CSD 覆盖
 * 本编译默认：XGUI_CSD=0 强制交 WM（回到系统条模式）、XGUI_CSD=1
 * 强制框架自绘；一次性解析入口见 XPlatformThemeDecoration_
 * effectiveMode（优先级：运行时 setMode > 环境变量 XGUI_CSD > 本编
 * 译默认 > Auto 探测，完整链见该函数注）。 */
#ifndef XGUI_CSD_DEFAULT
#define XGUI_CSD_DEFAULT 1
#endif

/* 原生窗口与系统无障碍平台后端。 */
#ifndef XPLATFORMNATIVEWINDOW_X11_ON
#define XPLATFORMNATIVEWINDOW_X11_ON 1
#endif
#ifndef XPLATFORMNATIVEWINDOW_WIN32_ON
#define XPLATFORMNATIVEWINDOW_WIN32_ON 1
#endif
#ifndef XPLATFORMACCESSIBILITY_ATSPI_ON
#define XPLATFORMACCESSIBILITY_ATSPI_ON 1
#endif
#ifndef XPLATFORMACCESSIBILITY_UIA_ON
#define XPLATFORMACCESSIBILITY_UIA_ON 1
#endif

/* 保留原有模块依赖：布局和控件级应用不能脱离其承载对象单独存在。 */
#if !XWIDGET_ON || !XGUIAPPLICATION_ON || !XWINDOW_ON
#undef XLAYOUT_ON
#define XLAYOUT_ON 0
#undef XFRAME_ON
#define XFRAME_ON 0
#undef XLCDNUMBER_ON
#define XLCDNUMBER_ON 0
#undef XSCROLLBAR_ON
#define XSCROLLBAR_ON 0
#undef XSTACKEDWIDGET_ON
#define XSTACKEDWIDGET_ON 0
#undef XBUTTONGROUP_ON
#define XBUTTONGROUP_ON 0
#undef XSTATUSBAR_ON
#define XSTATUSBAR_ON 0
#undef XDIALOGBUTTONBOX_ON
#define XDIALOGBUTTONBOX_ON 0
#undef XMENUBAR_ON
#define XMENUBAR_ON 0
#undef XTOOLBAR_ON
#define XTOOLBAR_ON 0
#undef XSPLITTER_ON
#define XSPLITTER_ON 0
#undef XSIZEGRIP_ON
#define XSIZEGRIP_ON 0
#undef XRUBBERBAND_ON
#define XRUBBERBAND_ON 0
#undef XFOCUSFRAME_ON
#define XFOCUSFRAME_ON 0
#undef XSPLASHSCREEN_ON
#define XSPLASHSCREEN_ON 0
#undef XMESSAGEBOX_ON
#define XMESSAGEBOX_ON 0
#undef XMDIAREA_ON
#define XMDIAREA_ON 0
#undef XCALENDARWIDGET_ON
#define XCALENDARWIDGET_ON 0
#undef XTEXTBROWSER_ON
#define XTEXTBROWSER_ON 0
#undef XKEYSEQUENCEEDIT_ON
#define XKEYSEQUENCEEDIT_ON 0
#undef XTEXTEDIT_ON
#define XTEXTEDIT_ON 0
#undef XPLATFORMFONTDATABASE_ON
#define XPLATFORMFONTDATABASE_ON 0
#undef XWIZARD_ON
#define XWIZARD_ON 0
#undef XERRORMESSAGE_ON
#define XERRORMESSAGE_ON 0
#undef XTEXTDOCUMENT_ON
#define XTEXTDOCUMENT_ON 0
#undef XTABLEWIDGET_ON
#define XTABLEWIDGET_ON 0
#undef XCHARTS_ON
#define XCHARTS_ON 0
#undef XDIALOG_ON
#define XDIALOG_ON 0
#undef XDOCKWIDGET_ON
#define XDOCKWIDGET_ON 0
#undef XMAINWINDOW_ON
#define XMAINWINDOW_ON 0
#undef XTOOLBOX_ON
#define XTOOLBOX_ON 0
#undef XABSTRACTSCROLLAREA_ON
#define XABSTRACTSCROLLAREA_ON 0
#undef XSCROLLAREA_ON
#define XSCROLLAREA_ON 0
#undef XDATETIMEEDIT_ON
#define XDATETIMEEDIT_ON 0
#undef XFONTCOMBOBOX_ON
#define XFONTCOMBOBOX_ON 0
#undef XPLAINTEXTEDIT_ON
#define XPLAINTEXTEDIT_ON 0
#undef XLABEL_ON
#define XLABEL_ON 0
#undef XABSTRACTBUTTON_ON
#define XABSTRACTBUTTON_ON 0
#undef XPUSHBUTTON_ON
#define XPUSHBUTTON_ON 0
#undef XCHECKBOX_ON
#define XCHECKBOX_ON 0
#undef XRADIOBUTTON_ON
#define XRADIOBUTTON_ON 0
#undef XCOMMANDLINKBUTTON_ON
#define XCOMMANDLINKBUTTON_ON 0
#undef XMENU_ON
#define XMENU_ON 0
#undef XTOOLBUTTON_ON
#define XTOOLBUTTON_ON 0
#undef XPROGRESSBAR_ON
#define XPROGRESSBAR_ON 0
#undef XGROUPBOX_ON
#define XGROUPBOX_ON 0
#undef XABSTRACTSLIDER_ON
#define XABSTRACTSLIDER_ON 0
#undef XDIAL_ON
#define XDIAL_ON 0
#undef XCOMBOBOX_ON
#define XCOMBOBOX_ON 0
#undef XTABBAR_ON
#define XTABBAR_ON 0
#undef XTABWIDGET_ON
#define XTABWIDGET_ON 0
#undef XLINEEDIT_ON
#define XLINEEDIT_ON 0
#undef XABSTRACTSPINBOX_ON
#define XABSTRACTSPINBOX_ON 0
#undef XSPINBOX_ON
#define XSPINBOX_ON 0
#endif
/* XStackedLayout 依赖 XLayout；布局总开关裁剪时连带裁剪。 */
#if !XLAYOUT_ON
#undef XLAYOUT_STACKED_ON
#define XLAYOUT_STACKED_ON 0
#endif
/* XSlider 依赖 XAbstractSlider；抽象基类裁剪时连带裁剪 XSlider。 */
#if !XABSTRACTSLIDER_ON
#undef XSLIDER_ON
#define XSLIDER_ON 0
#endif
#if !XABSTRACTBUTTON_ON
#undef XPUSHBUTTON_ON
#define XPUSHBUTTON_ON 0
#undef XCHECKBOX_ON
#define XCHECKBOX_ON 0
#undef XRADIOBUTTON_ON
#define XRADIOBUTTON_ON 0
#undef XTOOLBUTTON_ON
#define XTOOLBUTTON_ON 0
#endif
#if !XPUSHBUTTON_ON
#undef XCOMMANDLINKBUTTON_ON
#define XCOMMANDLINKBUTTON_ON 0
#endif
/* XPushButton 的菜单关联字段依赖 XMenu；XMenu 裁剪时连带裁剪。 */
#if !XMENU_ON
#undef XPUSHBUTTON_ON
#define XPUSHBUTTON_ON 0
#undef XTOOLBUTTON_ON
#define XTOOLBUTTON_ON 0
#endif
#if !XFRAME_ON
#undef XLABEL_ON
#define XLABEL_ON 0
#endif
#if !XGUIAPPLICATION_ON
#undef XAPPLICATION_ON
#define XAPPLICATION_ON 0
#endif

/* XGui 总开关关闭时，所有 GUI 子模块统一裁剪。 */
#if !XGUI_ON
#undef XIMAGECODEC_ON
#define XIMAGECODEC_ON 0
#undef XIMAGECACHE_ON
#define XIMAGECACHE_ON 0
#undef XSCREEN_ON
#define XSCREEN_ON 0
#undef XSURFACEFORMAT_ON
#define XSURFACEFORMAT_ON 0
#undef XCURSOR_ON
#define XCURSOR_ON 0
#undef XWINDOW_ON
#define XWINDOW_ON 0
#undef XACCESSIBLE_ON
#define XACCESSIBLE_ON 0
#undef XGUIAPPLICATION_ON
#define XGUIAPPLICATION_ON 0
#undef XSTYLEHINTS_ON
#define XSTYLEHINTS_ON 0
#undef XCLIPBOARD_ON
#define XCLIPBOARD_ON 0
#undef XMIMEDATA_ON
#define XMIMEDATA_ON 0
#undef XPALETTE_ON
#define XPALETTE_ON 0
#undef XIMAGEIOPLUGIN_ON
#define XIMAGEIOPLUGIN_ON 0
#undef XSVGICON_ON
#define XSVGICON_ON 0
#undef XPIXMAPCACHE_ON
#define XPIXMAPCACHE_ON 0
#undef XMOVIE_ON
#define XMOVIE_ON 0
#undef XPLATFORMINTEGRATION_ON
#define XPLATFORMINTEGRATION_ON 0
#undef XPLATFORMNATIVEINTERFACE_ON
#define XPLATFORMNATIVEINTERFACE_ON 0
#undef XPLATFORMWINDOW_ON
#define XPLATFORMWINDOW_ON 0
#undef XPLATFORMINPUTCTX_ON
#define XPLATFORMINPUTCTX_ON 0
#undef XGPU_ON
#define XGPU_ON 0
#undef XBACKINGSTORE_ON
#define XBACKINGSTORE_ON 0
#undef XPLATFORMBACKINGSTORE_ON
#define XPLATFORMBACKINGSTORE_ON 0
#undef XGUI_BACKINGSTORE_RENDER_MODE
#define XGUI_BACKINGSTORE_RENDER_MODE XGUI_BACKINGSTORE_RENDER_MODE_PARTIAL
#undef XGUI_BACKINGSTORE_BUFFER_COUNT
#define XGUI_BACKINGSTORE_BUFFER_COUNT 1
#undef XGUI_BACKINGSTORE_BUFFER1
#define XGUI_BACKINGSTORE_BUFFER1 0
#undef XGUI_BACKINGSTORE_BUFFER2
#define XGUI_BACKINGSTORE_BUFFER2 0
#undef XGUI_BACKINGSTORE_BUFFER_SIZE
#define XGUI_BACKINGSTORE_BUFFER_SIZE 0
#undef XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
#define XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16 0
/* 限帧参数随总开关收敛为 0=不限（消费方本就随 XGui 裁剪，双保险）。 */
#undef XGUI_RESIZE_REPAINT_MAX_FPS
#define XGUI_RESIZE_REPAINT_MAX_FPS 0
#undef XGUI_PRESENT_MAX_FPS
#define XGUI_PRESENT_MAX_FPS 0
#undef XGUI_RETAINED_LAYER_BUDGET_BYTES
#define XGUI_RETAINED_LAYER_BUDGET_BYTES 0
#undef XPIXMAP_ON
#define XPIXMAP_ON 0
#undef XSTYLE_ON
#define XSTYLE_ON 0
#undef XPAINTDEVICE_ON
#define XPAINTDEVICE_ON 0
#undef XPAINTER_ON
#define XPAINTER_ON 0
/* 渲染内核逐格式开关随总开关连带裁剪。 */
#undef XRENDERKERNEL_RGB565_ON
#define XRENDERKERNEL_RGB565_ON 0
#undef XRENDERKERNEL_RGB555_ON
#define XRENDERKERNEL_RGB555_ON 0
#undef XRENDERKERNEL_RGB888_ON
#define XRENDERKERNEL_RGB888_ON 0
#undef XRENDERKERNEL_BGR888_ON
#define XRENDERKERNEL_BGR888_ON 0
#undef XRENDERKERNEL_RGB32_ON
#define XRENDERKERNEL_RGB32_ON 0
#undef XRENDERKERNEL_RGBX8888_ON
#define XRENDERKERNEL_RGBX8888_ON 0
#undef XRENDERKERNEL_RGBA8888_ON
#define XRENDERKERNEL_RGBA8888_ON 0
#undef XRENDERKERNEL_ARGB32_ON
#define XRENDERKERNEL_ARGB32_ON 0
#undef XRENDERKERNEL_GRAYSCALE8_ON
#define XRENDERKERNEL_GRAYSCALE8_ON 0
#undef XPAINTER_SHAPE_ON
#define XPAINTER_SHAPE_ON 0
#undef XPAINTER_POLYGON_ON
#define XPAINTER_POLYGON_ON 0
#undef XPAINTER_PENSTYLE_ON
#define XPAINTER_PENSTYLE_ON 0
#undef XPAINTER_BRUSH_ON
#define XPAINTER_BRUSH_ON 0
#undef XPAINTER_BRUSH_ORIGIN_ON
#define XPAINTER_BRUSH_ORIGIN_ON 0
#undef XPAINTER_BACKGROUND_ON
#define XPAINTER_BACKGROUND_ON 0
#undef XPAINTER_PIXMAP_ON
#define XPAINTER_PIXMAP_ON 0
#undef XPAINTER_IMAGE_RECT_ON
#define XPAINTER_IMAGE_RECT_ON 0
#undef XPAINTER_TILED_PIXMAP_ON
#define XPAINTER_TILED_PIXMAP_ON 0
#undef XPAINTER_PATH_ON
#define XPAINTER_PATH_ON 0
#undef XPAINTER_TEXTLAYOUT_ON
#define XPAINTER_TEXTLAYOUT_ON 0
#undef XPAINTER_LAYOUT_DIRECTION_ON
#define XPAINTER_LAYOUT_DIRECTION_ON 0
#undef XPAINTER_RENDERHINT_ON
#define XPAINTER_RENDERHINT_ON 0
#undef XPAINTER_WORLD_MATRIX_ON
#define XPAINTER_WORLD_MATRIX_ON 0
#undef XPAINTER_VIEW_TRANSFORM_ON
#define XPAINTER_VIEW_TRANSFORM_ON 0
#undef XPAINTER_CLIP_ON
#define XPAINTER_CLIP_ON 0
#undef XPAINTER_CLIP_REGION_ON
#define XPAINTER_CLIP_REGION_ON 0
#undef XPROGRESSBAR_ON
#define XPROGRESSBAR_ON 0
#undef XGROUPBOX_ON
#define XGROUPBOX_ON 0
#undef XABSTRACTSLIDER_ON
#define XABSTRACTSLIDER_ON 0
#undef XDIAL_ON
#define XDIAL_ON 0
#undef XCOMBOBOX_ON
#define XCOMBOBOX_ON 0
#undef XTABBAR_ON
#define XTABBAR_ON 0
#undef XTABWIDGET_ON
#define XTABWIDGET_ON 0
#undef XLINEEDIT_ON
#define XLINEEDIT_ON 0
#undef XABSTRACTSPINBOX_ON
#define XABSTRACTSPINBOX_ON 0
#undef XSPINBOX_ON
#define XSPINBOX_ON 0
#undef XLAYOUT_STACKED_ON
#define XLAYOUT_STACKED_ON 0
#undef XAPPLICATION_ON
#define XAPPLICATION_ON 0
#undef XWIDGET_ON
#define XWIDGET_ON 0
#undef XFRAME_ON
#define XFRAME_ON 0
#undef XLCDNUMBER_ON
#define XLCDNUMBER_ON 0
#undef XSCROLLBAR_ON
#define XSCROLLBAR_ON 0
#undef XSTACKEDWIDGET_ON
#define XSTACKEDWIDGET_ON 0
#undef XBUTTONGROUP_ON
#define XBUTTONGROUP_ON 0
#undef XSTATUSBAR_ON
#define XSTATUSBAR_ON 0
#undef XDIALOGBUTTONBOX_ON
#define XDIALOGBUTTONBOX_ON 0
#undef XMENUBAR_ON
#define XMENUBAR_ON 0
#undef XTOOLBAR_ON
#define XTOOLBAR_ON 0
#undef XSPLITTER_ON
#define XSPLITTER_ON 0
#undef XSIZEGRIP_ON
#define XSIZEGRIP_ON 0
#undef XRUBBERBAND_ON
#define XRUBBERBAND_ON 0
#undef XFOCUSFRAME_ON
#define XFOCUSFRAME_ON 0
#undef XSPLASHSCREEN_ON
#define XSPLASHSCREEN_ON 0
#undef XMESSAGEBOX_ON
#define XMESSAGEBOX_ON 0
#undef XMDIAREA_ON
#define XMDIAREA_ON 0
#undef XCALENDARWIDGET_ON
#define XCALENDARWIDGET_ON 0
#undef XTEXTBROWSER_ON
#define XTEXTBROWSER_ON 0
#undef XKEYSEQUENCEEDIT_ON
#define XKEYSEQUENCEEDIT_ON 0
#undef XTEXTEDIT_ON
#define XTEXTEDIT_ON 0
#undef XPLATFORMFONTDATABASE_ON
#define XPLATFORMFONTDATABASE_ON 0
#undef XWIZARD_ON
#define XWIZARD_ON 0
#undef XERRORMESSAGE_ON
#define XERRORMESSAGE_ON 0
#undef XTEXTDOCUMENT_ON
#define XTEXTDOCUMENT_ON 0
#undef XDIALOG_ON
#define XDIALOG_ON 0
#undef XTABLEWIDGET_ON
#define XTABLEWIDGET_ON 0
#undef XDOCKWIDGET_ON
#define XDOCKWIDGET_ON 0
#undef XCHARTS_ON
#define XCHARTS_ON 0
#undef XCHARTVIEW_STATIC_LAYER_ON
#define XCHARTVIEW_STATIC_LAYER_ON 0
#undef XCHARTVIEW_PROFILE
#define XCHARTVIEW_PROFILE 0
#undef XMAINWINDOW_ON
#define XMAINWINDOW_ON 0
#undef XTOOLBOX_ON
#define XTOOLBOX_ON 0
#undef XABSTRACTSCROLLAREA_ON
#define XABSTRACTSCROLLAREA_ON 0
#undef XSCROLLAREA_ON
#define XSCROLLAREA_ON 0
#undef XDATETIMEEDIT_ON
#define XDATETIMEEDIT_ON 0
#undef XFONTCOMBOBOX_ON
#define XFONTCOMBOBOX_ON 0
#undef XPLAINTEXTEDIT_ON
#define XPLAINTEXTEDIT_ON 0
#undef XLABEL_ON
#define XLABEL_ON 0
#undef XABSTRACTBUTTON_ON
#define XABSTRACTBUTTON_ON 0
#undef XPUSHBUTTON_ON
#define XPUSHBUTTON_ON 0
#undef XCHECKBOX_ON
#define XCHECKBOX_ON 0
#undef XRADIOBUTTON_ON
#define XRADIOBUTTON_ON 0
#undef XCOMMANDLINKBUTTON_ON
#define XCOMMANDLINKBUTTON_ON 0
#undef XMENU_ON
#define XMENU_ON 0
#undef XTOOLBUTTON_ON
#define XTOOLBUTTON_ON 0
#undef XSLIDER_ON
#define XSLIDER_ON 0
#undef XLAYOUT_ON
#define XLAYOUT_ON 0
#undef XINPUTMETHOD_ON
#define XINPUTMETHOD_ON 0
#undef XWINDOWEVENT_ON
#define XWINDOWEVENT_ON 0
#undef XWINDOWSYSTEMINTERFACE_ON
#define XWINDOWSYSTEMINTERFACE_ON 0
#undef XGUI_PERFORMANCE_OVERLAY_ON
#define XGUI_PERFORMANCE_OVERLAY_ON 0
#undef XGUI_PERFORMANCE_OVERLAY_FPS_ON
#define XGUI_PERFORMANCE_OVERLAY_FPS_ON 0
#undef XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
#define XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON 0
#undef XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
#define XGUI_PERFORMANCE_OVERLAY_NETWORK_ON 0
#undef XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
#define XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON 0
#undef XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
#define XGUI_PERFORMANCE_OVERLAY_MEMORY_ON 0
#undef XPLATFORMNATIVEWINDOW_ON
#define XPLATFORMNATIVEWINDOW_ON 0
#undef XPLATFORMNATIVEWINDOW_X11_ON
#define XPLATFORMNATIVEWINDOW_X11_ON 0
#undef XPLATFORMNATIVEWINDOW_WIN32_ON
#define XPLATFORMNATIVEWINDOW_WIN32_ON 0
#undef XPLATFORMACCESSIBILITY_ATSPI_ON
#define XPLATFORMACCESSIBILITY_ATSPI_ON 0
#undef XPLATFORMACCESSIBILITY_UIA_ON
#define XPLATFORMACCESSIBILITY_UIA_ON 0
#endif

#include "Graphics/XImageCodec/XImageCodec_config.h"

#endif /* XGUICONFIG_H */
