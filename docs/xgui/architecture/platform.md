# 平台层架构

> 归属：Src/XGui/Platform（XPlatformIntegration/NativeWindow/
> InputContext/NativeInterface）+ Src/XGui/Window（XWindow/
> XWindowSystemInterface/XScreen）+ Drive/Posix、Drive/windows。
> 对标：QPA（QPlatformIntegration/QPlatformWindow/QPlatformScreen）。

## 1. 主循环（双源统一等待）

- POSIX：poll 双 fd——X11 连接 fd + `XAbstractNetIoRing` ring 事件 fd
  （io_uring/epoll 经 getEventFd 抽象）；谁就绪处理谁。
- Windows：MsgWaitForMultipleObjects(IOCP 句柄 + QS_ALLINPUT)；
  IOCP 就绪先批量 drain 再泵消息（防高频消息饿死网络）。
- ring 关闭时退化单源；20ms 心跳保留（时间轮心跳，非延迟来源）。
- 定时器精度：时间轮最近到期 + 高精度红黑树截止并入
  `XDeviceTimer_nextPreciseDeadline`（仅主线程；空轮不惰性创建），
  1ms 向上取整——普通定时器 20ms→1ms；无定时器维持心跳兜底。

## 2. 窗口与 flags

- XWindow ↔ 平台窗口桥接：XWidgetWindow（XWindow 子类）承载控件
  顶层；setFlags 已创建时转发平台 `XPlatformNativeWindow_setFlags`。
- X11 落地：StaysOnTop/Bottom→_NET_WM_STATE、BypassWindowManager→
  SKIP_TASKBAR+PAGER（近似，Qt xcb 为 re-create）、
  DoesNotAcceptFocus→_NET_WM_HINTS.input、装饰→_MOTIF_WM_HINTS；
  未映射读-改-写、已映射发 ClientMessage（映射态属性归 WM）。
- SHOW/HIDE 语义：showEvent 由 XWidget_sendShowHide 按可见翻转恰好
  发一次；桥接窗口自身映射事件不转发控件（防双发）。

## 3. 屏幕与 DPI

- RandR 1.5 XRRGetMonitors 逐监视器一屏（回落 XScreenOfDisplay）；
  经 WSI handleScreenAdded 差分注册；热插拔差分增删+主屏重选
  （含 (0,0) 判定）+驻留窗口钳位迁移。
- logicalDPI 直读根窗口 RESOURCE_MANAGER + XrmGetStringDatabase
  （XGetDefault 有连接级缓存不可用于运行期刷新）；缺省 96；
  physical=pixels/(mm/25.4)；devicePixelRatio 恒 1.0（X11 无缩放）。
- RRScreenChangeNotify→XRRUpdateConfiguration→差分回填（值不变不发）。

## 4. 事件合成面（WSI）

key（含 scanCode/timestamp _ex）/mouse（含 globalPosition/timestamp
_ex）/wheel/enter/leave/contextMenu/drop/DnD 全套/IME/touch/tablet/
screen 四态/theme/locale/applicationState/windowState。
注入即统一入口；自发同步投递；命中派发经桥接窗口转译。

## 5. 光标

模块级后端钩子表（queryPos/warpPos/applyWindowCursor/clearForWindow，
对标 QPlatformCursor）+ X11 实现：24 形状→cursorfont 字形（Blank
空像素图；部分字体近似）；位图/像素图经 XCreatePixmapCursor（全彩
RENDER 待做）；XWindow/XWidget 两级 setCursor 均接平台。

## 6. fbdev（嵌入式，默认关）

六钩子契约（XPlatformDisplayDriver.h）：probe/formatNegotiate/pan/
cacheSync/waitVsync/stride；/dev/fb0 open→fb_var/fix→mmap；565 等
面板格式识别；FBIOPAN_DISPLAY+FB_ACTIVATE_VBL（厂商 VSYNC #ifdef
预留）；msync 尽力写回（非一致内存板级注册覆盖）；与 X11 后端注册式
互斥共存；默认零 ABI 面。

## 7. theme/locale/applicationState

handleThemeChanged→XStyleHints_setColorScheme（深浅→调色板联动，
显式 setPalette 守卫）；handleLocaleChange（BCP 47，RTL 语言驱动
布局方向重解析）；handleApplicationStateChanged（转发既有状态机）。

## 8. 已知时序/边界

- WM 对刚映射数百 ms 内的 _NET_WM_STATE ClientMessage 可能丢弃
  （WM 侧行为；应用 show 后稍晚设置不受影响）。
- 窗口创建前 screens() 为空（事件循环启动后可用的既定语义）；
  多监视器真实热插拔受虚拟驱动限制未端到端验证。
- 触摸 XI2 合成、touch→mouse 的来源标志字段、完整多点触点列表
  为登记偏差。
