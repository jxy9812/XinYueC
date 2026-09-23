# XGui × XI2 触摸/输入设备调研

> 2026-09-23 调研代理产出 · 决策材料（未立项，供项目所有者定夺）
> 结论先行：值得立项且方案 A（最小 XI2 接入）先行——控件层触摸管线已 100% 就位，缺的只是平台事件源；A 是 B 的真子集，先 A 后 B 无返工。

## 一、现状摘要

**平台层（Drive/Posix/Graphics/XPlatformNativeWindow_posix.c）**：纯 X11 核心协议。`xpwn_dispatchEvent` 分发：ButtonPress 映射按钮 1–3 为鼠标 press/双击（时间窗+距离阈值识别），按钮 4–7 转滚轮 ±120/格；ButtonRelease/MotionNotify 走 `handleMouseEvent_ex`（含全局坐标与毫秒时间戳）；EnterNotify/LeaveNotify 转 enter/leave。事件掩码为核心协议掩码。**全仓库零处 XInput2 引用，CMake 仅链 ${X11_LIBRARIES}**。

**公共层**：事件枚举已备齐——XEVENT_TYPE_TOUCH_BEGIN/UPDATE/END/CANCEL、TABLET 系列、WHEEL、SCROLL_PREPARE/SCROLL（后两者定义了但全仓库无生产者与消费者）。`XTouchEvent`（Src/XGui/Window/XWindowEvent.h）为最小单点负载：首触点局部/屏幕坐标 + pointCount，**无触点列表、无 tracking id、无压力**（§8.1 登记偏差 Task 2.20）。`XWheelEvent` 字段完整对标 Qt6.8（angleDelta/pixelDelta/phase/inverted/source），但 pixelDelta 恒 (0,0)、phase 恒 NoScrollPhase——**有载荷接口、无数据源**。

**控件层（Src/XGui/Widget/XWidget.c）**：触摸派发闭环已完整（`XWidget_dispatchTouchEvent`）：主点命中 → TouchBegin 被接受即隐式抓取（含跨顶层转投）→ 未被接受走 touch→mouse 仿真（默认开，合成事件已带 m_synthesized 标志并透传触摸时间戳）；touchEvent 虚槽已挂 vtable。

**关键缺口**：`XWindowSystemInterface_handleTouchEvent(_ex)` 在 Drive/、Test/、main.c 中**零调用者**——整条触摸管线"建好但未接电"，X11 平台层不产任何触摸事件。（注：XGui.md §8.1"合成事件无来源标志字段"表述已滞后于代码，synthesized 标志已实装。）

## 二、方案对比

| 维度 | A：最小 XI2 接入 | B：完整触摸事件流 | C：核心协议模拟（现状兜底） |
|---|---|---|---|
| 做法 | XIQueryDevice/XISelectEvents/XIGetClientPointer 接入；XI_TouchBegin/Update/End 主点转译接上 handleTouchEvent_ex 零调用者；XI_Motion 滚动 valuator → pixelDelta/phase | XI2 触摸按 detail(tracking id) 分组 → XTouchEvent 扩展多点列表（id/pressure/state）→ WSI 签名升级 → 控件层按触点遍历派发 | 不改代码：X 服务器把触摸屏映射为虚拟鼠标，只报第一触点 |
| 工作量 | 小：平台单文件+CMake 链 Xi，约 1–2 天 | 中大：跨平台/公共/控件三层，约 3–5 天+回归 | 零 |
| 收益 | 触摸即插即用（单点）；平滑/高分辨率滚轮落地（XWheelEvent 字段激活）；触摸滚动（§8.0f 挂账项）可收口；XIPointerEmulated 过滤防触摸双投 | 多点数据齐备、双指并发可感知，为将来手势留底 | 单点点击已可用 |
| 风险 | 低：XI2 与核心事件并存需去重（选 XI 掩码后按 Qt 方式以 XI 事件为准） | 中：per-point 抓取/部分接受语义对齐 Qt 细；结构体扩展动既有创建签名；且 §8.1 已声明手势体系不做，多点即时消费方少 | 天花板硬：无 tracking id/压力/平滑滚轮，无法区分触摸与鼠标来源，触摸滚动永久不做，多指丢失 |
| 兼容性 | 无 XI2 环境可运行时回退 C 形态 | 同 A，回退路径保留 | 无依赖 |

## 三、推荐结论

**值得立项，且 A 先行**。理由：控件层触摸管线已 100% 就位，XGui 缺的只是平台事件源；A 是 B 的真子集（设备枚举、事件选择、分发接线全部复用），先 A 后 B 无返工。B 不急于上——单触点+touch→mouse 仿真已覆盖当前 demo 与桌面场景，多点列表在"手势不做"的既定裁剪下收益滞后，宜待 A 验收后按真实需求再立。C 不作为项目（它就是现状），仅作为 A 的运行时回退分支保留（XQueryExtension("XInputExtension") 检测失败即走核心协议，与 Qt xcb 同策略）。

## 四、后续动作清单

1. **CMake**：find_package(X11) 后补链 X11_Xi_LIB（现为纯核心库）。
2. **平台初始化**：XQueryExtension → XIQueryVersion（≥2.2，多点触摸下限）→ XIGetClientPointer → XIQueryDevice 枚举触摸屏（触点容量）与滚动 valuator 设备。
3. **事件选择与去重**：XISelectEvents（TouchBegin/Update/End + Button/Motion）；XI 事件优先，核心 Button4/5 类重复按 XIPointerEmulated 标志过滤。
4. **接线**：XI_Touch* → handleTouchEvent_ex（detail 作 tracking id，先走通道透传，B 阶段入列表）；滚动 valuator → handleWheelEvent + XWheelEvent_setPixelDelta/setPhase，顺带收口"触摸滚动不做"挂账。
5. **验证**：Xvfb（XI2 支持）+ xinput test-xi2 注入做无头回归；demo 页接 touchEvent 虚槽目验。
6. **文档收口**：XGui.md §8.1（synthesized 标志已实装）、§5.4/§8.2 多点列表与触摸滚动条目随立项更新。

**关键文件**：Drive/Posix/Graphics/XPlatformNativeWindow_posix.c、Src/XGui/Window/XWindowEvent.h、Src/XGui/Window/XWindowSystemInterface.h（触摸注入入口）、Src/XGui/Widget/XWidget.c（触摸派发）、Src/XCode/XEvent/XEventType.h（触摸/滚动枚举）。
