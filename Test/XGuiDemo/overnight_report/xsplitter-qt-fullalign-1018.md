# XSplitter Qt 全量对齐交付报告（2026-10-18）

**对象**：`Src/XGui/Widget/XSplitter.{h,c}`、新建 `Src/XGui/Widget/XSplitterHandle.{h,c}`、`Test/XGuiDemo/xgui_demo_apitest_containers.c`、`xgui_regression_test.c`（仅 :30627 一处）、`.zcode/wf-gates/`（门脚本与基线）。
**输入**：对齐设计稿（missingPlan/rename/hysteresis 三部分）、两阶段整改记录（小件/结构件）、目验记录、审计修正意见。
**行号时效声明**：本报告区分三类证据——【撰写轮实读】（本报告撰写时实际执行的 grep/sed/文件读取，2026-10-18 下午）、【整改会话实跑】（两阶段整改与目验会话的执行记录，本报告未重跑）、【蓝图行号】（设计稿/审计报告登记的 Qt 源码行号；本机无 Qt 源树，qsplitter.*/qwidget.* 行号无法复读，沿用登记值）。`XWidget.c`（并行 lane，mtime 14:45:34）与 `xgui_window_demo.c`（并行导航 lane，mtime 16:12:09）在整改期间被并行编辑，涉及二者的行号仅为撰写轮时点读数，交付前须停止并行改动后终锚（见 5.4）。

---

## 一、对齐总览

### 1.1 首版缺失 11 项逐项状态

**清单口径说明**：任务书首版 missing 为 11 条（上轮审计报告 1.1 节确认"首版 11 条 missing 全部保留有效"）；审计报告第五节将其归并为 10 条登记（PM_SplitterWidth 与 SH_Splitter_OpaqueResize 两条样式回落合并为一项）。任务书原文不在本次交付材料内，本表按"审计五节 10 条 + PM/SH 拆回两条（与设计稿 missingPlan#1/#2 分立追踪一致）"复原 11 条口径，逐项给出现状态。

| # | 缺失项（Qt 出处为蓝图行号） | 状态 | 落地方式与证据 |
|---|---|---|---|
| 1 | `PM_SplitterWidth` 样式回落（qsplitter.cpp:1614-1629） | ✅ 已完成 | `m_handleWidth` uint16_t→int16_t，-1=样式哨兵；getter `>=0?存储:XStyle_pixelMetric(PM_SplitterWidth)`；setter 允许 -1、拒 <-1、置后重排；init 保持显式 5（默认 Fusion PM=dpiScaled(4)、XCommonStyle=0，哨兵默认会丢把手宽，故不改默认值）；快照直写存储值（-1→"-01"），restore 3 字符有符号解析、-1 原样往返、<-1 钳 0。apitest 新增哨兵回落（与样式查询自比）与 <-1 拒绝两断言 PASS【整改会话实跑】 |
| 2 | `SH_Splitter_OpaqueResize` 样式回落（qsplitter.cpp:1472-1484） | ✅ 已完成 | 新增 `m_opaqueResizeSet` 位（自 m_splitterFlagsUnused 取），init 不置位；getter `set?m_opaque:XStyle_styleHint(SH_Splitter_OpaqueResize)`；既有"默认 opaqueResize=true"断言即回落路径实证【整改会话实跑】 |
| 3 | `CT_Splitter` sizeFromContents 接线（qsplitter.cpp:191-199） | ✅ 已完成 | 唯一接线点=`XSplitterHandle::sizeHint` 调 `XStyle_sizeFromContents(XStyleCT_Splitter, opt, {hw,hw})`；三样式无 case=与 Qt QCommonStyle 同构透传，样式侧零改动 |
| 4 | 4 个 Q_PROPERTY 登记（qsplitter.h:24-27） | ❌ 未做=用户裁定 | 观察项：控件层普遍不登记 Qt 属性（审计实测 9 控件中 8 个 `XObject_setProperty` 计数为 0），非 XSplitter 特有缺口；建议等框架级属性系统统一做（见 5.1） |
| 5 | `QSplitter::sizeHint` / `minimumSizeHint`（qsplitter.h:50-51） | ⚠️ 部分完成 | XSplitter 侧逐式计算已公开（隐藏页 skip、Σpick+max trans、xsp_smartMin+可见把手、换轴）；**XWidget vtable `EXWidget_SizeHint`/`EXWidget_MinimumSizeHint` 槽未做**——XWidget.h/XWidget.c 不在许可清单，vtable 派发路径未验证，登记待后续（见 5.2） |
| 6 | `createHandle` / `moveSplitter` / `closestLegalPosition` / `setRubberBand`（qsplitter.h:75/:83/:85/:84） | ✅ 已完成 | 新虚槽 `EXSplitter_CreateHandle`（槽型 `EXSplitter_CreateHandle_t`，类槽先例 XAbstractSocket.h，默认 new XSplitterHandle 回写 m_index）；`xsp_moveSplitter` 公开化为 `XSplitter_moveSplitter`；closestLegalPosition；setRubberBand=XRubberBand(Line) 子件、pos<0 deleteLater（对标 :1301-1327） |
| 7 | 缺省事件链 childEvent/event/changeEvent/resizeEvent（qsplitter.h:77/:79/:82/:80） | ⚠️ 主体完成 | `EXObject_Event` 重载：CHILD_ADDED 自动收录（对标 :1263-1293）/CHILD_REMOVED 摘槽+deleteLater 把手/LAYOUT_REQUEST→xsp_layout（m_inLayout 位防重入）/STYLE_CHANGE→updateHandles（对标 :987-990/:1634-1640）；resizeEvent 已有；CHILD_POLISHED 补 show 为防御性保留（框架无发送方）。**show 方向 setVisible 触发（missingPlan#5b，对标 qwidget.cpp:8344-8348+10520-10527）未做**——XWidget.c 不在许可清单；当前显隐两方向均需显式 refresh（apitest F3 注释按此实况改写），见 5.2 |
| 8 | QSplitterHandle 全类 13 项（qsplitter.h:96-125） | ✅ 已完成 | 真部件化，新文件 `XSplitterHandle.{h,c}`（mtime 13:30/13:31【撰写轮实读】），详见 1.3 |
| 9 | farMin/farMax 折叠迟滞 + collapsed 字段（qsplitter.cpp:577-628 + qsplitter_p.h:34/:39） | ✅ 已完成 | 详见 1.4 |
| 10 | restoreState 按实际页数钳位消费（qsplitter.cpp:1712-1732） | ✅ 已完成 | 删页数早退，逐实际页消费；回归契约 :30627 同步翻转（仅一处），详见 1.5 |
| 11 | 类型收紧 int 1/2 → `XOrientation`（仓库一致性建议，非 Qt 缺失） | ⚠️ 公开面完成 | XSplitter 公开面参数/返回已改 XOrientation（含把手）；14 处 int 调用点 C 隐式兼容零强制改动，实测零告警【整改会话实跑】；**typedef 迁 XGuiConfig.h 未做**（文件不在许可清单），现 XSplitter.h 包含 XGridLayout.h 并注释登记迁移后可删，见 5.2 |

### 1.2 rename 映射表

原则（设计稿）：公开面 Qt 名；调用点全改；不留别名。rename 前全仓普查 `rg -o 'XSplitter_[A-Za-z0-9_]+'` 计 33 符号。

| # | 旧形态 | 新形态（Qt 对标） | 调用点处置 |
|---|---|---|---|
| 1 | `bool XSplitter_handle(const XSplitter*, int, XRect*)` | `XSplitterHandle* XSplitter_handle(const XSplitter*, int)`（qsplitter.h:67） | 旧 out-rect 形态删除、无别名；矩形经 `XWidget_geometry(把手)` 读回；apitest 5 处改写、期望不变（材料记 :1544/:1548/:1550/:1745/:1761；撰写轮现读数 :1553/:1561/:1563/:1790/:1808，apitest 自 13:08 后未再改）。xsp_handleAt 与分割器侧 MousePress/Move/DblClick/Leave 重载一并删除 |
| 2 | `XSplitter_indexOf` 仅匹配页 | 扩把手匹配（`widget==widget \|\| handle==widget`，对标 qsplitter.cpp:1183-1186），命中把手返回其分隔点 | — |
| 3 | `init_2`/`create_2`/`create_ex_2` orient 参数 int | 名不改（_2 重载构造定式），参数改 `XOrientation` | 调用点 apitest:1488 一带、回归 :30624，C 隐式兼容零强制改动 |
| 4 | `setOrientation`/`orientation` 签名 int | `XOrientation`（qsplitter.h:38-39） | 14 处调用点零强制改动（回归 :30585/:30599-30601、apitest 一带、xgui_window_demo.c:1984-1985 一带）；**行为增补=遍历 m_handles 同步把手方向**（对标 qsplitter.cpp:987-990） |
| 5 | `insertWidget`/`refresh`/`setOpaqueResize`/`opaqueResize` | 名/签名 1:1 不改 | getter 补 SH 回落（见 1.1 #2） |
| 6 | `handleClicked_signal` | **保留 XGui 扩展**（用户裁定），名不改 | 信号标识定义留在 XSplitter；发射实现迁把手回调（见第四节） |
| 7 | `splitterMoved_signal` | 名 1:1 | index 保持分隔点域 0..count-2（有意偏差，Qt 为 n 槽域，登记） |
| 8 | `getRange` | 名同、bool+分隔点 index+近区间保留 | **index≥1 近区间双端 +hw·B_left 数值修正已落地**；测试面仅断 index0（材料记 apitest :1552/:1564/:1567），期望不变，修正后数值无断言覆盖面（登记） |
| 9 | —（无对应物） | 新类 `XSplitterHandle`，新文件 `Src/XGui/Widget/XSplitterHandle.{h,c}` | CMake GLOB 自动入列（CMakeLists.txt:161；新增源文件须 reconfigure，整改会话已重configure 并在构建日志核实 XSplitterHandle.c.o 编译行） |

### 1.3 QSplitterHandle 部件化

- **真子部件**：把手由"分割器内自算矩形"重构为 XWidget 直派生真子部件（基类 XFrame→XWidget 二次重构，派生定式对标 XRubberBand.h）；结构体对齐 QSplitterHandlePrivate（qsplitter_p.h:114-119 蓝图行号）+ XGui 增补（m_splitter/m_mouseOffset/m_pressPick/m_index/m_dragTarget/m_orient、hover/pressed/dragTargetValid 位）。
- **布局占位**：把手占 children 尾段块，几何=页 i 后把手宽条带布局直配（与旧 out-rect 逐值一致，故 apitest 几何期望不变）；可见性随 xsp_bandAfter（隐藏页把手条带防死态在部件模型下重新实现并注记）。
- **绘制**：CE_Splitter 自绘，state=Enabled/MouseOver(hover)/Sunken(pressed)+m_horizontal（对标 qsplitter.cpp:306-324 蓝图行号）；Enter/Leave 刷新 hover。
- **事件**：拖动四事件自收——mousePress（mouseOffset=pick(local)+pressed+grabMouse，对标 :273-281）/mouseMove（opaque→moveSplitter(父映射−offset)；非 opaque→closestLegalPosition+setRubberBand，对标 :255-268）/mouseRelease（非 opaque 落位+setRubberBand(-1)；空闲点击≤2px→handleClicked(m_index,0)）/双击→handleClicked(m_index,1)（对标 :800-809/:821-841）。
- **工厂**：`createHandle` 经 `EXSplitter_CreateHandle` 虚槽，默认实现 new XSplitterHandle 并单点回写 m_index。
- **过程缺陷两起（均已修复并复测收敛）**：
  1. 把手块入 children 后页数计"total=2P-1 反推"公式在插入中间态失真（2 页 0 把手被计 1 页，count/handle/getRange/replace/F3 连锁 +15 新失败）→ 先改"children 总数−m_handleCount"，二阶段再重构为 `m_pages` 权威序表（xsp_insertSlot/xsp_removeSlot/xsp_moveSlot 槽位单点同步，顺带修 XVector_move 不平移并行数组的错位隐患），把手数=页数-1 推导；
  2. 首轮 apitest 段错误经 gdb 栈回溯定位为 `VX_splitter_event↔XObject_event_base` 无限互递归（XObject.c:864 走 XClassGetVirtualFunc 虚分派）→ 改用 `XClass_Parent(XObject, EXObject_Event, ...)` 基表条目调用（XCoreApplication.c:761 同定式）。
- **审计新发现、本报告补登记的反向偏差**：CE_Splitter 的 m_horizontal 位映射与 Qt/XCommonStyle **相反**——`XSplitterHandle.c` 撰写轮实读 :111-112 `opt.m_horizontal = xh_horiz(h) ? false : true`（注释口径"true=把手是水平条带"）；而 Qt QSplitterHandle::paintEvent 在 orient==Horizontal 时置 State_Horizontal，XGui 消费端 XCommonStyle.c:2620-2632 按"m_horizontal=true→竖条带+竖排 3 点"实现。现映射下水平分割器的 5px 竖条带拿到 false→点线沿 X 排布、±4px 两点被裁仅中央 1 点可见，垂直分割器反向同理。该反向自旧实现照搬（HEAD XSplitter.c:318/:339 同款），与两阶段记录"paintEvent 对标 qsplitter.cpp:306-324"的表述冲突且此前未登记。建议：翻转为一处三元表达式并统一两处注释语义（见 5.4）。

### 1.4 farMin/farMax 折叠迟滞与 collapsed 模型

- **字段**：`m_collapsed`（int8_t*，仅 0=展开/1=折叠两态；创建/扩容补 0；与 m_collapsible 的 -1=未覆写哨兵不同域不互混）。m_sizes 即 Qt sizer。
- **近/远区间换算**（分隔点 b；hw=handleWidth；B_left(b)=#{j<b : xsp_bandAfter(j)}）：
  - nearMin'(b)=Σ_{i≤b,可见}xsp_smartMin(i)+hw·B_left(b)；nearMax'(b)=contentLen−Σ_{i>b,可见}xsp_smartMin(i)+hw·B_left(b)。
  - Qt 出处（蓝图行号）：可见把手宽计入 min/max（:545-548）、i<index/i≥index 循环（:596-599）经分隔点↔槽锚换算即 +hw·B_left 项；XGui 旧实现零把手贡献且 index≥1 漏计中间条带。
  - 算例（设计稿推导）：3 页各 100、hw=5（contentLen=300）：旧 index1=[0,300]，修正后 [5,305]——代码落地与推导一致【结构件记录】。
  - farMin=nearMin'−collapsibleSizeBefore（对标 :614-615）；farMax=nearMax'+collapsibleSizeAfter（:617-618）；collapsibleSize=近邻首个可见可折叠页 smartMin（:556-571），smartMin=0 邻页→far≡near。
  - index0 数值不变（B_left(0)=0）。
- **adjustPos**（对标 :630-659，Threshold=40）：近区间内原样；出区间 delta>width/2 且 delta>=min(40,width)→吸附 far 端，否则弹回近端。替换旧硬夹取。
- **moveSplitter 折叠状态机**：adjustPos→钳 smartMin/mayCollapse→xsp_applyMove→storePageState→placeHandles→splitterMoved；非 opaque 记把手 m_dragTarget+setRubberBand，释放落位。
- **落位/折叠模型**：collapsed 置位（拖 far 落 0/setSizes 0/restore 重建）**不看 smartMin>0**（有意差异登记：XGui 0=unset 哨兵需位标志区分归零页与未定页，视觉等价 Qt 0 尺寸页；门控在 mayCollapse/isCollapsible 上游）。Qt 负坐标出屏（:739）不可直搬（XGui 负几何 clampSize 归 0 致页永失）→ collapsed 页 0 几何+位标志。
- **布局四分支**（xsp_layout）：① isHidden→零几何+意图保留+不计 unset；② collapsed→零几何+条带保留+游标仅+band+不计 unset；③ sizer>0→权重；④ 未设定池→均分/stretch。unset 统计跳①②——②④互斥=收起页不复活钉子。
- **setSizes_helper**（XGui 版，无"可见"门，对标 :690-694）：sizer==0→collapsible?collapsed=1:sizer=smartMin；sizer>0 且 <smartMin→钳 smartMin。唯一边缘偏差：不可折叠页 smartMin==0 时 setSizes(0) 钳后仍 0→落④均分（Qt 留 0；0=unset 契约下无解，登记）。
- **setRubberBand**：XRubberBand(Line) 子件、pos<0 deleteLater（对标 :1301-1327）；rBord=3 内缩无对应物（XRubberBand 自绘，登记）。
- **saveState 不扩格式**：Qt 快照无 collapsed 字段（:1655-1678），XGui 同不落；由 restore/setSizes/拖动路径按 sizer==0 重建。

### 1.5 restoreState 语义与回归契约同步

- **语义**：删页数早退（旧 XSplitter.c:1434）；逐实际页 sizer=(k<saved?saved[k]:0)（对标 :684 value() 默认 0）→0 走 collapsed/smartMin 分支；方向/handleWidth/childrenCollapsible 无条件恢复；魔数/长度/格式校验保留返回 false。:30627 场景（快照 3 页/实际 0 页）循环 0 次→true。
- **handleWidth 哨兵往返**：saveState 直写存储值（-1→"-01"），restore 3 字符字段有符号解析，-1 原样往返、<-1 钳 0。
- **登记偏差**：快照不落 opaqueResize/opaqueResizeSet 两字段（Qt :1675/:1677 有、XGui 13B 头+5B/页格式无）；restore 后 setter 链不触碰 m_opaqueResizeSet 位→getter 走 SH_Splitter_OpaqueResize 样式回落。将来扩格式须同步 bump 头语义（仓内无跨版本持久化承诺）。
- **回归契约同步**：`xgui_regression_test.c` 仅 splitter 族 :30627 一处期望翻转——"页数不匹配按 Qt 钳位成功（qsplitter.cpp:1712-1732）"，附对标注记【撰写轮实读该文件 sp_expect grep 共 19 行，其中 :30570 为函数定义行，实际调用 18 条】。翻转断言随回归全过（SP-FAIL=0）。

---

## 二、三尾巴修复

### 2.1 死宏（apitest 快照区恢复编译）

- apitest 内误拼守卫 `XBYTEARRAY_ON`→`XByteArray_ON`（材料记 4 处 :53/:1472/:1633/:1719；撰写轮实读现存守卫 `#if XByteArray_ON` 在 :58/:1465/:1651、`#endif` :1704，行号经 F 区扩注后有漂移），F 区 8 条快照断言首次真正编译执行且全部 PASS【整改会话实跑】。
- 误拼活残留=0：撰写轮 `grep -rn 'XBYTEARRAY_ON' Src Test .zcode`（排除 overnight_report 报告散文）仅余 2 处修正说明注释（apitest :58/:1647）。
- F 区"恢复快照内尺寸均分"按 C 区显式 show 定式修补（setVisible 双页+refresh 再 save/验/复位），期望由 {315,315} 修订为 **{317,318}**（635=640−1×5 整除均分、末页吸收余量；apitest :1655-1662 注释在位【撰写轮实读】）——此为"8 条必须 PASS"的必要修正，均分语义不变，特此登记（非quietly 改期望：635 口径对应部件化后 2 页 1 条带模型，见 3.2 的 11 条基线观察）。

### 2.2 门排除（零排除集重建）

- 新建 `.zcode/wf-gates/wf_qtalign_gate.sh`（mtime 12:54:45【撰写轮实读】）：等 make 空闲→flock→build-direct 双目标 make→Xvfb :92 自启→autotest+apitest=containers 失败行归一后与两条 qtalign 基线做"新失败⊆基线"（**零排除集**）→回归 `grep -c 'SP-FAIL'`==0→过则输出 QTALIGN_GATE_PASS exit 0。
- 删除 `.zcode/wf-gates/splitter_autotest_exclude_1018.txt`（撰写轮 ls 确认不存在）；拆除 `wf_splitter_audit_gate.sh` 的 EXCLUDE 变量/5b 扣除循环/文件头排除段（重写落盘 mtime 12:55:03【撰写轮实读】）。
- **门结论表述收窄（审计修正，照登）**：重写后的审计门**从未重跑**——/tmp/ 下最后一份审计门日志 `wf_splitter_audit_gate_final.log` 停在 02:35:31，且内容带"[排除·并行旋转 lane 在制品 dwfrun-626d822d]"扣除前缀与 `AUDIT_GATE_PASS`（旧脚本产物，对应旧代码态；撰写轮实读该日志属实，其 Xvfb 为 :95、现行门统一 :92）。审计轮另做模拟核验：以 16:32 门产物的 autotest/apitest 输出对旧基线按零排除口径 extract+comm，新失败为空——即重写后审计门若重跑应 PASS，**但这是口径模拟，不是实际运行**。故本章 3.1 的门结论表述为："qtalign 门（终态代码、终版脚本）16:32 PASS；审计门重写后未重跑，其判据已被 qtalign 门终态运行覆盖（模拟比对亦空）"。

### 2.3 告警清零

- 旧 2 条告警消解：XSplitter.c:386:28（paintEvent 内 `xsp_childCount(self)` 传 XWidget\*）随 paint 重写删除；:1412:46 改 `XByteArray_data((XByteArray*)state)` 显式转型（XByteArray.h:455 constData 为非 const 别名宏，容器头不动）。
- 构建日志 `grep warning | grep -cE 'XSplitter|xgui_window_demo|xgui_demo_apitest_containers'` = 0【整改会话实跑】；撰写轮未重建构建，未重复该统计。

---

## 三、验证

### 3.1 门

终态门运行产物 `/tmp/wf_qtalign_gate_final.log`（2026-10-18 16:32:17 起）【撰写轮实读全文】：

```
BUILD_RC=0
Xvfb :92 已在运行
autotest RUN_RC(--autotest)=0
apitest=containers RUN_RC(--apitest=containers)=1
autotest 比对（基线之外新失败，零排除集）：(无)
apitest 比对（基线之外新失败，零排除集）：(无)
回归 RUN_RC(regression)=1；SP_FAIL_COUNT=0
QTALIGN_GATE_PASS
```

即：qtalign 门在终态代码、终版脚本下 **PASS**（rc=0）。配套产物 `.log.build/.autotest/.apitest/.regression` 及归一 fail/base 文件同在 /tmp/（16:32:18-16:32:26）【撰写轮实读目录】。

### 3.2 autotest / apitest / 回归基线对比

- **基线**：`apitest_containers_baseline_qtalign_1018.log`（10:43:47 采集，改前、bin 现役未重建）：总计失败=25，其中 XSplitter [FAIL] 断言恰 **11 条**【撰写轮实读】。
- **终态对比**：16:32 门产物 `.apitest.fail` 中 XSplitter [FAIL] 仍恰 11 条，且与基线 11 条**逐行相同**（两份清单撰写轮并列实读比对一致）；总计失败=25=基线。**"零新增"成立；这 11 条本身未清零**，是登记偏差的断言面（见下）。
- **11 条基线失败的构成（撰写轮实读其断言标题）**：垂直均分 {235,235}（480−2×5）、拉伸因子前置均分 {315,315}、默认均分 {315,315}（640−2×5）、getRange(0) [0,630]、refresh 回归均分、setSizes 归一化 {210,420}、setSizes 全零无操作、setSizes(NULL/0/负) 无操作、setStretchFactor(2,1) 2:1 加权、setStretchFactor 越界无操作、stretch 全 0 退化均分。事实性观察：其中均分/getRange 期望式按旧条带模型（640−2×5=630，每页后各一条带）书写；部件化后 2 页为 1 条带（640−1×5=635），F 区同类断言已按新口径修正为 {317,318} 且 PASS，而这 11 条未随本轮修改、保留为基线。是否修订这批断言期望属后续裁定，本报告如实登记（见 5.5）。
- **通过面**【整改会话实跑】：F 区 8 条快照断言（魔数头/接受自身快照/方向恢复/尺寸{317,318}/损坏/截断/字段往返等）全 PASS；新增哨兵回落与 <-1 拒绝 2 断言 PASS；handle(0)/smartMin/replaceWidget 族/F3 条带契约保持 PASS。
- **回归**：`DISPLAY=:92 ./bin/XGuiRegression_Test` → `grep -c 'SP-FAIL'`=0（18 条 sp_expect 调用全过，含 :30627 翻转断言；材料曾记"19 条契约"，经审计修正并经撰写轮 grep 复核：19 行含 :30570 定义行，实际调用 18 条）。
- **autotest**：零基线外新失败（零排除集），为 demo 导航交互（换边/收起/点击展开/记忆宽）存续的间接实证。

### 3.3 目验逐项与证据

目验会话以含 16:11/16:12 演示源修改的二进制（16:24:27 重链接，PID 311596，:92）执行【目验会话实跑，过程性细节无法事后复现；撰写轮核验了代码锚点与终态证据在位】：

| 项 | 结果 | 要点与证据 |
|---|---|---|
| 环境 | — | 五处修复代码锚点核验在位（XWindowDecoration.c:458-496 子件光标沿父链解析；xgui_window_demo.c:2229-2252 收起态 space>=40 回拖展开、:2046-2055 把手补挂 HitEdgeBand、:2022-2032 收起 sizes[pi]=0、:2034-2040 重申钳底 sizes<1→1） |
| ① 悬停光标 | 通过 | XFixes sprite 探针三段：内容区 serial=67 hot=2,1(left_ptr)→把手上 serial=68 hot=11,11（中央水平双箭头=SizeHor）→离把还原 67；装饰层子件光标沿父链解析生效，三态往返正常 |
| ④ 回拖展开阈值 | 通过 | 实测阈值=40px 精确命中：折叠后 4px 步进回拖，space=36（窗40）未展开（采样点色 244,246,248）、space=40（窗44）展开（255,255,255），与 `if(space>=40)` 一致；回拖全程带随鼠标 1:1 揭示，展开后以揭示宽度为新记忆（不弹回）；收起态把手可按可拖（HitEdgeBand 生效） |
| ⑤ 左停靠收起/展开 | 通过 | 拖宽 204→收起钮→带贴左缘 [0,7]；单击贴缘把手→恢复记忆宽 204（带 [204,211]），不再死锁 |
| ⑤ 右停靠收起钮 | 通过 | 收起后带贴右缘 [992,999]、内容页不再消失（内容区色 244 恒正常）；单击贴右缘把手（带落在 E 向改尺寸带内）仍恢复 204——HitEdgeBand 让位在 E 带同样生效 |
| ⑥ 缩放往返 | 通过 | 右停靠 208 下五步 1000→1100→1200→1000→1200→1000 全程无 [0,1] 收起签名、内容区色恒正常；1000 稳态终测带 [788,795] 记忆宽精确保持（个别中途采样滞后系 ~1fps 帧泵重申节奏，非丢失非折叠）；上轮"自发折叠+内容空白+状态劣化"未复现 |
| 连带冒烟 | 通过 | 拖动 1:1（步进证实）；换边跨停靠记忆保持（多重换边回左停靠带 [208,215]=208 精确）；切页渲染干净；弹层开合零残留；grep -icE 'segv\|crash\|assert' 日志=0；进程存活 5 分半无异常 |
| 收尾 | — | 实例已退、:92 残留 XinYueC 窗口=0；Xvfb :92 非目验会话所起按规保留 |

**目验"无异常"的限定（审计修正，照登）**：`/tmp/qtalign_ui.log:3-7` 存在一条 XError 参数校验报告（ArgIsNULL_report：XClassGetVtable(object) 是 NULL，函数 XClass_deinit_base，Src/XClass/XClass.c）。该报告**非本轮引入**：02:44 的 /tmp/audit_ui.log（改造前）同款，16:32 门产物 autotest 段亦有 1 条（apitest 无），属启动/收尾期既有杂音，应用继续运行无碍。"grep segv|crash|assert=0"按其字面成立，但日志并非全净，特此限定。

### 3.4 复核口径与行号时效声明

- **本报告撰写轮未重跑任何门脚本与测试套件**。第三章门结论来自 /tmp 产物实读与两阶段/目验会话的执行记录；审计轮曾按门脚本同式（grep -a -E 'FAIL|失败'|sort -u|sed 归一后 comm -23）对 /tmp/wf_qtalign_gate_final.log.* 独立复算并与 .zcode/wf-gates 基线比对（零新失败），qtalign_final2.png 为实看图证。
- 过程性目验细节（"进程存活 5 分半""4px 步进实测阈值=40"等）无法事后复现，仅核验其代码锚点（space>=40 等五处）与终态证据（进程已退、无残留窗口、截图状态）在位。
- Qt 源码行号（qsplitter.*/qwidget.*）沿用蓝图与审计登记值，本机无 Qt 源树未复读。
- 行号漂移实录（撰写轮实测，均系并行 lane 编辑所致）：`XWidget_setVisible` 材料 :6021→审计 :6026（撰写轮 grep 复核 :6026，diff +251/-17，mtime 14:45:34）；`handleClicked_signal` 外部引用 :3717→:3738→**现 :3781**（xgui_window_demo.c mtime 16:12:09）。结论"行号须实施/交付时重锚"三次被证实，**交付前应停止 XWidget.c、xgui_window_demo.c 并行改动后终锚**。

---

## 四、保留的 XGui 扩展（handleClicked 迁移至 handle 部件）

- `handleClicked_signal` 为 Qt 无对应的纯 XGui 扩展，**用户裁定保留**；信号标识定义仍在 XSplitter（XSplitter.h）。
- 发射实现已从分割器鼠标路径迁至把手部件回调（XSplitterHandle.c）：空闲点击≤2px→`handleClicked(m_index,0)`、双击→`handleClicked(m_index,1)`（对标 qsplitter.cpp:800-809/:821-841 的点击/双击定式，信号语义保持 XGui 扩展）。
- 外部唯一引用点在 `xgui_window_demo.c`：撰写轮 grep 实测 :3781（`(size_t)XSplitter_handleClicked_signal(NULL,0,0)` 一带）；材料/审计曾记 :3717/:3738，系演示文件被并行导航 lane 于 16:12 再编辑漂移（见 3.4）。
- **材料勘误**：材料所记"apitest:1666 handleClicked 标识断言"经查**不存在**——HEAD 版与工作树版 apitest 对 'handleClicked' 均 grep 零命中【撰写轮实读】；现 :1655-1672 一带为 F 区快照断言。该断言面不存在于 apitest，相关"不动"表述按实况更正。
- index 语义：handleClicked/splitterMoved/getRange/handle 一致保持**分隔点域 0..count-2**（每分隔点裁定；与 Qt 把手 n 槽域的差异为有意偏差，登记）。

---

## 五、已知限制与未覆盖

### 5.1 用户裁定未做

- **4 个 Q_PROPERTY 登记**（orientation/opaqueResize/handleWidth/childrenCollapsible，qsplitter.h:24-27）＝用户裁定不做。依据：控件层普遍不登记 Qt 属性（审计实测 9 控件中 8 个 `XObject_setProperty` 计数为 0），非 XSplitter 特有缺口；建议等框架级属性系统统一做。

### 5.2 许可清单外未做（登记待后续）

- show 方向 `XWidget_setVisible` 触发重排（missingPlan#5b，对标 qwidget.cpp:8344-8348+updateGeometry_helper :10520-10527）——XWidget.c 不在许可清单；当前显隐两方向均需显式 refresh（apitest F3 注释已按此实况改写为"双向如实"）。
- `EXWidget_SizeHint`/`EXWidget_MinimumSizeHint` vtable 槽（XWidget.h/XWidget.c）——sizeHint 的虚槽派发路径未验证，XSplitter 侧逐式计算已公开。
- `XOrientation` typedef 迁 XGuiConfig.h——过渡方案为 XSplitter.h 包含 XGridLayout.h 并注释登记迁移后可删。

### 5.3 登记偏差汇总（两阶段累积）

- resizeEvent 不重载＝把手全条带命中（tiny-mode mask 无对应物）；
- collapsed 不落快照（Qt 同不落）；
- opaqueResize/opaqueResizeSet 两字段不落快照，restore 后 getter 走 SH 回落；
- 不可折叠页 smartMin=0 时 setSizes(0) 落未设定池（Qt 留 0；0=unset 契约下无解）；
- XGui 折叠置位不看 smartMin>0（0=unset 契约需位区分，视觉等价 Qt）；
- getRange index≥1 公开数值已含 +hw·B_left 修正，但该数值无断言覆盖面（测试面仅 index0）；
- handleWidth init 保持显式 5（哨兵默认会丢把手宽，因默认样式 PM 口径不一）；
- CHILD_POLISHED 补 show 为防御性保留（框架无发送方）；
- rBord=3 内缩无对应物（XRubberBand 自绘）；
- index 域用分隔点 0..count-2（Qt 为 n 槽域）；
- 上轮审计差异清单"其余 13 项保留为登记差异"继续有效（见 xsplitter-qt-audit-1018.md 1.2 节）。

### 5.4 审计新发现待办（本报告照登，未在本轮修复）

1. **CE_Splitter m_horizontal 位映射反向**（XSplitterHandle.c:111-112）：详见 1.3 末条。建议翻转三元表达式并统一两处注释语义，同时更正"paintEvent 对标 qsplitter.cpp:306-324"的表述口径。
2. **公开入口护栏不防嵌套**：保存-恢复式仅 xsp_insertSlot（XSplitter.c :926-940 一带）与 xsp_setRubberBand（:688-692 一带）两处【撰写轮实读两处均为 wasBlocked 保存-恢复式】；公开入口 XSplitter_insertWidget（置 1 后无条件清 0）与 XSplitter_replaceWidget（同式）在 childEvent 处理中再调会提前解除外层护栏（Qt insertWidget_helper 用 QScopedValueRollback 保存-恢复）。属轻微健壮性差异，建议统一为保存-恢复或维持"公开入口不防嵌套"登记。
3. XSplitter.h :134-136 一带有一条孤立/重复 doc 注释（"以指定方向初始化"紧邻 init_2 的 doc）【撰写轮实读在位】，纯注释冗余，顺手清理。
4. `wf_qtalign_gate.sh` :23 注释"splitter 契约 19 条 sp_expect 全过"应为 **18 条**（grep 19 行含 :30570 定义行）【撰写轮实读双证】；判据 `grep -c 'SP-FAIL'` 不受影响，仅注释数字待改。
5. 行号终锚：XWidget.c（+251/-17，14:45:34）与 xgui_window_demo.c（16:12:09）仍在并行改动，本报告所有涉及行号为撰写轮时点读数，交付前须停并行后终锚。

### 5.5 未跑/未覆盖清单

- 重写后的 `wf_splitter_audit_gate.sh` 未实际重跑（判据由 qtalign 门终态运行覆盖，模拟比对亦空；见 2.2）。
- 11 条 qtalign 基线 XSplitter 断言失败保留未清零（见 3.2），是否修订其期望口径属后续裁定。
- 真鼠标 UI 冒烟在整改会话未做（把手事件路径与 XScrollBar 抓取定式同构、autotest 零新失败为间接证据）；后续目验会话以 xdotool+像素采样补做了面板拖拽/收起/缩放/光标四类实测（3.3），但把手 hover 光标之外的把手直拖逐 motion 复测仍无自动化覆盖。
- 旋转 lane 并行在制品期间的部分构建阻塞（XWindow_rotationMapPoint/UnmapPoint 链接未定义）经两次升级裁定以定点检查协议等待其自行收敛，本工作流未触碰该 lane 文件。

---

*报告完。撰写：对齐报告撰写员，2026-10-18。证据分级与行号时效约定见卷首声明。*
