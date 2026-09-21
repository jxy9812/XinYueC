# 剪贴板与输入架构

> 归属：Src/XGui/Input（XClipboard/XMimeData/XInputMethod/XCursor）+
> Drive/Posix/Graphics/XPlatformNativeWindow_posix.c（X11 Selection 后端）。
> 对标：QClipboard/QMimeData/QInputMethod/QXcbClipboard。

## 1. 分层契约

```
控件层   XLineEdit/XTextControl —— paste(mode)/copy(mode)
服务层   XClipboard（单例；模式=Clipboard/Selection/FindBuffer）
             ├ 进程内镜像（XMimeData，二进制走 data_bytes）
             └ XClipboardBackend 契约（可注入；未注册=进程内语义）
平台层   XPlatformNativeWindow_posix.c（X11 Selection 全协议）
```

- 后端契约回调：text/setText/clear + supportsSelection +
  selectionRevoked（所有权被夺反向通知）+ formats/mimeData(借用)/
  setMimeData（多格式）。未注册回调零回归。
- FindBuffer 模式后端显式拒绝（仅进程内镜像）。

## 2. X11 Selection 协议（对标 ICCCM/QXcbClipboard）

- **双选择区**：CLIPBOARD + PRIMARY 各自独立状态（镜像/所有权/
  时间戳/多格式条目）；事件按 selection 原子分流。
- **认领**：专用 1×1 窗口（不随业务窗口生死）；真实服务器时间戳
  （对专用窗口零长度属性变更取 PropertyNotify），作为 TIMESTAMP
  目标应答。
- **serve**：TARGETS=实际持有集合（text/plain→UTF8_STRING+STRING）；
  SAVE_TARGETS 轻量应答；MULTIPLE 单往返逐对 serve（失败对
  property=None 标记）；数据目标 ≤阈值 直写，>阈值 INCR 分片
  （会话表 8 槽、PropertyNotify Delete 驱动、零长度终结、三路清理）。
- **request**：专用请求窗口；先 UTF8_STRING，被拒回退 XA_STRING；
  属性实际 type=INCR 时进入增量收集（终结="NewValue+空读"，5s 超时）。
- **必答纪律**：SelectionNotify 无论能否满足必须回复（拒绝=
  property=None），否则请求方阻塞超时。
- **所有权变更**：SelectionClear→selectionRevoked 回调→owns 复位+
  数据清空+信号（Clipboard→dataChanged / Selection→selectionChanged）。
- 非致命 Xlib 错误处理器：INCR 对端死亡的 BadWindow 不杀进程。

## 3. 数据模型与字节纪律

- XMimeData：文本（text/plain、text/html）XString 承载；自定义条目
  **XByteArray 承载**（二进制透明，setData_bytes/data_bytes）。
- 多格式镜像（posix）以字节流存储（memcpy+format=8），天然透明；
  读方向按需 XConvertSelection（不走 UTF-8 转换路径）。
- 图像：入站 png/bmp/jpeg 经 XImageCodec 解码（png 优先，对标 Qt）；
  出站 setImage/setPixmap→XImageCodec_encode(Png) 推平台镜像。
- text_subtype in/out 语义：空请求 plain→html 回退；显式请求不回退。

## 4. 中键粘贴

Button2 按下 + supportsSelection：光标落位（命中测试）→
paste(Selection)；Selection 空 + 系统后端可用 → 不动作（不回退
进程内共享层，对标 Qt）；无后端保持进程内回退。

## 5. 输入法

- 注入：XIM/XIC + DBus portal → `XInputMethodEvent`（preedit/commit
  深拷贝归事件所有，消费方必须 deinit）。
- 查询链：XGuiApplication 自动注册默认 query handler → 焦点控件
  `XWidget_inputMethodQuery` 虚槽（ImCursorRectangle 经
  inputItemTransform 映射；ImEnabled/ImHints 实时；文本类控件可重载
  提供环绕文本——待办）。
- filterEvent：键派发前置钩子（默认放行）；位于 IME consumed 之后。

## 6. 光标

24 形状→X11 cursorfont 映射（Blank=空像素图；Forbidden/Busy/手型/
拖拽为字体近似，Qt 官方位图自绘）；XCreatePixmapCursor 位图通道
（1bit 双色，热点钳制；全彩需 RENDER 待做）；XWarpPointer；
XWindow_setCursor 与 XWidget_setCursor 均接平台后端。

## 7. 触摸/平板

WSI handleTouchEvent/handleTabletEvent → 命中派发 → 虚槽；
BEGIN 接受=隐式抓取（UPDATE/END 直达）；未接受=touch→mouse 仿真
（默认开，属性 12 可关，synthesized 标志置位）；END/CANCEL 清理。
已知偏差：单触点承载；XI2 合成与多触点列表待做。
