# XI2 方案 B 多点触摸设计草案（评审稿）

> 2026-09-23 设计代理产出 · 实现前评审材料（未立项；启动条件见文末）
> 结论先行：XTouchEvent 尾部追加触点列表（不新增事件类型）+ 平台层聚合 + per-id 抓取表；约 5~6 人日，分 B1/B2 两步；当前桌面单点+仿真已覆盖，按需再立。

## 一、现状与约束

**已落地（方案 A，§8.0g24）**：
- 平台层 Drive/Posix/Graphics/XPlatformNativeWindow_posix.c：XI2 探测（XQueryExtension→XIQueryVersion，主设备 Touch 三类掩码）、XGuiXIDeviceEvent 逐字声明、cookie 分派 → xpwn_dispatchXi2TouchEvent 主点转译（pointCount=1，**detail 即 tracking id，现被丢弃**，无 pressure valuator 读取）。
- 公共层 Src/XGui/Window/XWindowEvent.h：XTouchEvent 为最小负载——首触点局部/屏幕坐标 + m_pointCount，无触点列表/id/pressure/state（Task 2.20 登记偏差）。事件类走 m_class 首成员继承，动态成员有先例：XExposeEvent 的 XRegion 深拷贝 + Deinit/Clone 虚槽。
- 注入点 Src/XGui/Window/XWindowSystemInterface.c：handleTouchEvent_ex(window, type, position, globalPosition, pointCount, timestamp)，同步自发投递，timestamp 走静态通道。
- 控件层 Src/XGui/Widget/XWidget.c 的 XWidget_dispatchTouchEvent：单点命中 → **整序列单抓取**（g_touchGrabWidget，被接受才抓）→ 未接受走 touch→mouse 仿真（主点、默认开）→ END/CANCEL 清理；含跨顶层转投；控件销毁时清抓取。

**约束**：(1) XI2 每个 touch point 独立发 XIDeviceEvent，Qt6.8 在平台聚合为"每时刻一条 QTouchEvent + QEventPoint 列表（变更点+其余 Stationary）"，抓取是 **per-point**（QEventPoint::grabber）；(2) XEVENT_TYPE_TOUCH_* 枚举已被模态拦截表、vtable 分派等消费，不宜新增事件类型；(3) 无触屏硬件，验证只能靠 WSI 注入与 Xvfb+xinput。

## 二、设计要点方案

**a. XTouchEvent ABI 策略——尾部追加 + 列表指针（推荐），不新增事件类型**
- 新增 XTouchPoint 值类型：int32_t m_id（=XI2 detail）、int m_state（对标 Qt6 QEventPoint::State 四态 Pressed/Updated/Stationary/Released，数值按 Qt6 头逐字抄）、XPoint m_position/m_globalPosition、float m_pressure（0~1，沿用 XTabletEvent 的 float 先例）。
- XTouchEvent **尾部追加** XTouchPoint* m_points（事件拥有，堆分配），现有 m_pointCount 语义升级为列表长度；m_position/m_globalPosition 保留为主点（points[0]）兼容视图——对标 Qt6 将 QTouchEvent 并入 QSinglePointEvent 后 position() 仍可用的形态。
- 生命周期套用 XExposeEvent 先例：XTouchEvent 增挂 Deinit/Clone 虚槽（释放/深拷贝列表）。源码兼容：旧 XTouchEvent_init/create_ex 签名不动（内部造 1 点列表），新增 XTouchEvent_initPoints/createPoints_ex；旧读代码继续读主点字段。WSI 新增 handleTouchPoints_ex(window, type, const XTouchPoint*, count, timestamp)，旧 handleTouchEvent_ex 变 1 点包装。

**b. tracking id 分组状态机与 per-point 抓取**
- **聚合放平台层**（对标 QXcbConnection::TouchDeviceData）：主设备维护 detail→{x,y,pressure,state} 活跃表；每个原始事件更新后合成**一条** XTouchEvent：变更点置新态，其余活跃点置 Stationary，pointCount=活跃数；END 点随本次事件携带 Released 后出表。窗口销毁时冲刷该表。
- 控件层把 g_touchGrabWidget 升级为 **per-id 抓取表** {id, grabber, mouseSynth}（容量 8~10）：BEGIN 按该点 childAt 命中，被接受→该 id 抓取；UPDATE/END 按 id 路由到 grabber（含跨顶层），无抓取的点重新命中（对标 Qt 未抓取点跟随 point 下窗口）；END 投递 Released 后释放槽位；CANCEL 清全部。**单点序列退化为现状路径**（回归保障）。touch→mouse 仿真绑定"首个未被接受 BEGIN 的 id"且仅主点合成，防止第二指干扰已合成的鼠标按压态。

**c. 平台层转译**
- dev->detail → m_id 逐字透传；event_x/y、root_x/y → 局部/全局坐标。
- pressure：初始化时 XIQueryDevice 缓存 valuator 编号（按 XInternAtom("Abs MT Pressure") 等标签匹配），事件时从 valuators.mask 取值归一化；无该 valuator 恒 1.0（Qt 按下默认）。**建议分两步**：B1 常量压力，B2 再接 valuator（驱动差异是主要不确定性）。
- 保留 A 的 XIPointerEmulated 过滤，多指下继续压制核心协议重复投递。

**d. 控件层派发**
- **逐变更点独立命中+派发**（per-point grab 的 widget 粒度）：每点目标=其 grabber 或重新命中控件；向每个目标投递携带**完整列表**的 XTouchEvent，列表全部点坐标折算到接收者坐标系（对标 Qt 把 points 翻译到目标窗口坐标），主点字段=该变更点。两指按两钮可各得独立序列。模态拦截表、TransparentForMouseEvents、控件销毁清抓取（改为按 widget 清多条 id）均沿用。

**e. 回归验证（无硬件）**
- 新增 Test/XGuiTest/XTouchMultiPointTest，纯 WSI 注入（handleTouchPoints_ex，无需 X display）：① id=1/id=2 交错 BEGIN(两控件)→交替 UPDATE→逆序 END，断言各控件收齐自身序列且互不串扰；② 单点序列与现状行为逐位一致（老用例不回归）；③ 未接受主点恰好合成 1 组 press/move/release，第二指接受不追加合成；④ 跨顶层抓取转投；⑤ CANCEL 清表；⑥ clone/deinit 无泄漏（挂现有内存池检查）。平台聚合函数抽成无 X11 依赖的纯 helper 供单测；Xvfb+xinput test-xi2 作目验补充。

## 三、工作量与风险

**工作量约 5~6 人日**：B1（结构体扩展+WSI 新入口+控件层 per-id 抓取+注入测试+老用例回归）3 人日；B2（平台聚合+pressure valuator+XI2 去重）1.5~2 人日；全套件回归+XGui.md §5.4/§8.1/§8.2 文档收口 1 人日。与调研书 3–5 天+回归一致。

**风险**：① 部分接受语义与 Qt 细节偏差（多点各自独立接受/忽略的排列）——以注入用例 ③ 锁定；② 多抓取下控件销毁悬挂——扩展现有清理点并补用例；③ pressure valuator 各驱动标签不一——B2 独立分期、失败回退常量；④ 平台聚合表泄漏（序列中途窗口销毁/断触）——销毁冲刷+END 兜底清理；⑤ XPoint short 溢出沿用现状限制，不在本批扩。

## 四、启动条件建议

**不建议现在做**：手势体系（捏合/旋转，§8.1 既定裁剪）、QEventPoint 的 velocity/rotation/ellipseDiameters 全字段、压力驱动控件（依赖 B2 且无硬件）。C 形态继续作为 XI2 缺失时的运行时回退，不立项。

**建议触发启动 B 的条件（满足其一即立项）**：① 出现真实多点需求——多指并发操作两个及以上控件、多点绘图/签名类页面；② §8.0f 触摸滚动或双指手势挂账被重新启动（Stationary 语义与触点列表是其前置）；③ 目标平台切换到 evdev/tslib 嵌入式，需要 tracking id 做防抖与漂移修正；④ 真实触屏硬件到位且 A 已验收（A 已落地，此项实为"有硬件可验"即启动）。当前桌面 demo 单点+仿真已覆盖，B 维持"按需再立"。
