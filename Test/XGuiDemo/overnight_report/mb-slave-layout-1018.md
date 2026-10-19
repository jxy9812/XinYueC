# Modbus 从站页「全宽纵向四段式」布局交付报告（1018）

- 落盘：2026-10-19（撰写员 lane）。实现与门禁：2026-10-18 夜间～10-19 凌晨；最终目验：2026-10-19 01:51–01:53。
- 分支 `codex/xdevice-file-platform`，未提交（无 commit/stash/push/checkout）。
- 改动边界：仅 `Test/XGuiDemo/xgui_demo_page_modbus.c`（从站页区段）与 `.zcode/wf-gates/wf_mbslave_layout_gate.sh`；未触碰其他源文件。
- 证据分级：**[实测]**＝本会话亲自执行/读取；**[材料]**＝实现/目验会话实跑、本会话仅复核落盘产物或未复核；凡只有材料支撑处均已标注。

---

## 一、布局规格与实现

### 1.1 布局规格（四段式 / 全宽 / 伸缩）

页根结构（页头注释树 `Test/XGuiDemo/xgui_demo_page_modbus.c:2020-2049` [实测]）：

```
scroll（XScrollArea，水平条 AlwaysOff、竖条 AsNeeded——全页唯一滚动条）
└─ layRoot(TopToBottom)
   ├─ 标题行        「Modbus 从站调试」
   ├─ layChannel    参数合并行：通道/监听端口/串口(随通道显隐)/本站地址
   ├─ layStart      启动/已停止/写事件行
   └─ dataArea（layData，恒纵向单列——四段）
      ├─ 段① DI：「输入点 DI」标题行（点数 SpinBox+按点数重建+全开+全关）+ diGridHost 换行灯格
      ├─ 段② DO：「输出点 DO」标题行（同上）+ doGridHost 换行灯格
      ├─ 段③ 「输入寄存器 IR」段标题行 + 数量 SpinBox(1..256) + irTable
      └─ 段④ 「保持寄存器 HR」段标题行 + 数量 SpinBox(1..256) + hrTable
```

- **四段式**：数据区自上而下 DI／DO／IR／HR 四段，IR、HR 各带段标题行（[10-18 三次追加]）。
- **全宽**：内容件宽恒跟随视口（水平跟随不滚），高=内容首选高，超高由整页竖滚条承担；DI/DO 灯格**换行铺排**（放不下换行往下放一行，宿主宽=列数×单元宽、高=行数×单元高随宽度重算，列数由 `syncViewport` 宽度驱动解算，`mb_slaveRebuildGrids` 注释 :2918-2932 附近 [实测]）。
- **滚动条纪律**（[10-18 二次追加]）：`diScroll/doScroll` 壳已删，DI/DO/表格任何子段不出滚动条，表满高全展开+列宽自适应铺满。
- **伸缩**：寄存器行数 1..256 可调——`MB_SLAVE_REG_MAX_ROWS=256`（:99）为上限，`MB_SLAVE_REG_ROWS=16`（:98）降格为默认行数；SpinBox 接 `valueChanged` → `mb_slaveApplyRegCounts`（:3478）：`setValueCount` 保值 + `setRowCount` 同步 + 收缩置 NULL/扩容补挂 + `echoAll` + `syncViewport`（:3490-3492 上限钳制、:3512 `mb_slaveMountRegRow` 补挂 [实测]）。
- **上下堆叠而非并排**（IR/HR 两表）：沿用前序已注释的选择理由——并排需两层嵌套且表格在盒式布局条目不贡献可分配宽 [材料]。

### 1.2 实现清单（13 条，含审计修正后的口径）

全部位于 `Test/XGuiDemo/xgui_demo_page_modbus.c` 从站页区段 [条目为材料，行号经本会话抽查核实]：

1. 常量区：`MB_SLAVE_REG_MAX_ROWS=256`（:99），`MB_SLAVE_REG_ROWS` 降格为默认行数（:94-98 注释同步）。
2. `MbSlave` 结构体新增 `layIrTitle/layHrTitle/irTitleLbl/hrTitleLbl/irCountSpin/hrCountSpin/irCount/hrCount`；`irRows/hrRows` 扩至 256（:506 `irCount` 注释「Spin 变更的副本」、:513-514 [实测]）。
3. `mb_slaveRebuildMap`（:2084）保值分支并入 **`unitIr`/`unitHr` 的 `setValueCount`（仅 IR/HR；DI/DO 不走此路径，见 1.3）**（:2111-2118 [实测]）；新建分支 IR/HR 按 `irCount`/`hrCount` 建（:2143-2158 区段 [实测]）。
4. 逐行硬编码编辑槽由 16×2 机械扩至 **256×2**：`kIrEditSlots`（:2602）/`kHrEditSlots`（:2690），装配取用 :3102-3103；槽内越界早退安全（`if (slot < 0 || slot >= MB_SLAVE_REG_MAX_ROWS) return;` :2200）。沿用本页「行号编译期写死」已验证定式，sender 不可信契约不破 [实测行号]。
5. `mb_slaveMountRegRow` 抽取（:2844，前向声明 :2074）：build 与扩容补挂共用，常驻挂载从不叠加。
6. `mb_slaveBuildRegTable`（:2887）行数参数化；IR/HR 标识移回段标题行；表头恢复**「地址 / 值(点击/回车生效)」**——`kHead` 实文 :2892-2896（审计修正：原「地址/值」补全为「值(点击/回车生效)」）[实测]。
7. 新增 `mb_slaveRePlaceRegEditors`（:3020）：列宽差量同步后页面侧按 `XTableWidget_visualItemRect`（与框架同判据）逐框重摆，越界/未挂行藏框（:3039-3046）；接入 `applyRegGeometry` 尾部（:3185-3186）；`applyRegGeometry` 改按各表实际行数定满高（autotest 侧对应断言见 :4992 注释「高=表头高+当前行数×统一行高」）。
8. 新增 `mb_slaveApplyRegCounts`（:3478）+ IR/HR 数量槽（:3528/:3535）：`setValueCount` 保值 + `setRowCount` 同步 + 收缩置 NULL/扩容补挂 + `echoAll` + `syncViewport`。
9. 新增 `mb_slaveAuditRegEditors`（:3065）：逐框断言挂载=常驻框/父=表视口/几何=visualItemRect 像素级/可见性=框架口径；孤儿 `deleteLater` 删除+重挂自愈（:3101）。
10. 装配段 IR/HR 各加段标题行（「输入寄存器 IR」/「保持寄存器 HR」+ 数量 SpinBox 1..256 接 `valueChanged`）；两表创建于 :4755/:4784。
11. autotest 同步：行数断言**改对 `irCount`/`hrCount` 生效副本**（:4935 注释 [实测]，非写死 16）；满高按实际行数；新增段标题行存在/范围/父链/纵向次序断言；审计=0 且恰 32 框；列宽差量（±120/±80 再还原）前后三轮逐框像素级审计；IR 扩 64→表内滚动条置中→审计→收缩回 16→保值与收缩残留断言。
12. 修复本轮新引入的一处指针比较告警：`mb_slaveRePlaceRegEditors` 内 `XTableWidget_cellWidget(tbl, i, 1) != (XWidget*)rows[i].reg` 的 `(XWidget*)` 强转在 **:3043**（:3040 另有一处同型强转；审计修正：行号由 :3025 改正为 :3043，本会话核实 :3043 无误）[实测]。
13. 页头与结构体注释同步八条目/契约 2/6（2026-10-18 日期）。**注意：契约 2 与结构体注释的寄存器槽口径仍为旧文，未与实现同步——见 1.5 与已知限制 1。**

### 1.3 连带根修两项（2026-10-19，审计修正补列；边界注明）

按审计修正要求，实现清单补列以下两项。**边界**：现存 overnight 报告中唯一的 Modbus 报告 `modbus-slave-rework-1018.md` 落盘于 10-18 14:04（`ls -la` 实测），早于该两修，未见其他 lane 承接，故列入本报告；若后续另有 lane 报告承接，以彼为准。

- **CLOSE-WAIT 死链巡检**：`mb_slaveDeadClientPred`（:3553）/ `mb_slaveSweepDeadClientsCb`（:3582，:3593 调用巡检）/ `reapTimer`（字段 :528，创建接线 :4806-4809，1s 间隔）+ autotest 断言 [实测行号]。
- **DI/DO 位单元首建满容量 64 且不再 resize**（16 点读回根修）：`mb_slaveRebuildMap` 新建分支位单元按 `MB_SLAVE_MAX_POINTS` 一次建足（:2123 起「[16点读回根修 2026-10-19]」注释），理由＝位类型（XBitArray_resize 路径）运行态 `setValueCount` 扩容不生效；结构体注释 :535-539 同步 [实测]；IR/HR 仍走 `setValueCount`（寄存器路径实测正常）。

### 1.4 框架根因与页面侧对策

- **根因**：框架 `xtw_cellWidgetRelayout` 仅挂在 `XTableWidget_setRowCount`（`Src/XGui/Widget/XTableWidget.c:471`→调用 :504）、`removeRow`（:590→:604）、`removeColumn`（:611→:618）、`VX_tableWidget_scrollContentsBy`（:1817→:1824）四处 [本会话逐一核实]；**列宽变化（`XTableView_setColumnWidth`）不触发重摆**，挂件摆位不跟随 [材料]。
- **对策**：框架文件无权改，页面侧在列宽同步后按 `visualItemRect` 同框架判据逐框补摆（`mb_slaveRePlaceRegEditors`，1.2 第 7 条）。

### 1.5 注释口径待改项（审计修正①，**未落地**）

本会话核实（2026-10-19），工作区中以下两处注释**仍是与实现相反的旧口径**：

- 页头契约 2（:2051-2056）仍写「改 IR/HR 各一枚共享槽按指针反查行号」；
- 结构体注释（:493-495）仍写「各 2 列 × 16 行……槽内按 sender 指针反查行号可靠」。

而实现实为 **256×2 编译期行号槽**（`kIrEditSlots`/`kHrEditSlots`，:2602/:2690，:3102-3103 取用）。审计结论：二选一——改写注释服从实现（删除上述与代码相反的表述），或反向改代码为共享槽；**注释必须服从实现**。本报告撰写员不改业务代码，此项留待代码 lane 落地（推荐改注释：256×2 槽方案已过门禁与目验）。

---

## 二、验证

### 2.1 门禁（build + autotest + apitest=containers + 基线比对 + 页 18 PASS）

门脚本 `.zcode/wf-gates/wf_mbslave_layout_gate.sh` [本会话读全文核实判定逻辑]：等 make 空闲 → `flock /tmp/xinYueC_mbslave.lock` 编译 `build-direct` 目标 `XGuiWindowDemo_Test`（失败即 GATE_FAIL）→ `DISPLAY=:92` 跑 `--autotest` 与 `--apitest=containers` → 失败行（`grep -a '\[FAIL\]' | sort -u`）对 qtalign 基线（`autotest_baseline_qtalign_1018.log` / `apitest_containers_baseline_qtalign_1018.log`）做「**新失败 ⊆ 基线失败**」判定 → 页 18 自身须含「扩展页18(Modbus 从站) PASS」（防裁剪成桩假通过）→ 全过则 `echo MBSLAYOUT_GATE_PASS && exit 0`。

材料声明门实跑两轮（run1：`bash .zcode/wf-gates/wf_mbslave_layout_gate.sh /tmp/wf_mbslave_layout_gate_run1` → `MBSLAYOUT_GATE_PASS` exit 0；修复告警后 run2 同结果）[材料]。**判定字符串为脚本 stdout、未落盘于 run 目录**；本会话对两轮落盘产物逐项复核，等价条件全部成立 [实测]：

| 检查项（本会话命令） | run1 | run2 |
|---|---|---|
| `wc -l` autotest.fails / autotest.base.fails | 9 / 9 | 9 / 9 |
| `wc -l` apitest_containers.fails / .base.fails | 25 / 25 | 25 / 25 |
| `comm -13 <(sort base.fails) <(sort fails)` 新失败 | 0 | 0 |
| `*.newfail.log` 字节数 | 0（两套均空） | 0（两套均空） |
| autotest.log 含「扩展页18(Modbus 从站) PASS」 | 是（:366/:369 另含页 17 PASS） | 是（grep -c = 1） |
| build.log 尾部 | `[100%] Built target XGuiWindowDemo_Test` | 同左（含 Linking 行） |

产物时间戳旁证：`bin/XGuiWindowDemo_Test` = 2026-10-19 01:45:59（`ls --time-style=full-iso` [实测]）。

**基线口径说明（如实）**：ask 材料提及「既有失败集=18 条存量」，而门脚本实采基线为 autotest 9 条 + apitest 25 条（本会话 `wc -l` 实测）；判定以「新失败 ⊆ 基线」为准，两轮两套均 0 新失败，未扩大任何失败集。「18 条」与现行基线数不符一事原样记录，未改基线。

**本会话未重跑门与构建**——以上为「材料实跑 + 落盘产物逐项复核」；`最终门=true` 依据即此。

### 2.2 目验逐项（2026-10-19 01:51–01:53，Xvfb **:89** 1920x1080x24，PID 449843 [材料；截图文件本会话 ls 核实在盘]）

**从站准备（--page 18，日志 /tmp/mbsl_ui5.log）**：
- u01：SpinBox 至 16/16 + 按点数重建 → 正常；
- u02：混合态 DI 仅 01/03/05 红、DO 仅 02/04 红（**本会话直读 `u02_mixed16.png` 确认**：DI 红={01,03,05}，DO 红={02,04}，IR 数量 16，表头「地址 | 值(点击/回车生效)」可见）；
- u03：HR 行 1 行内编辑 1234 生效；
- u04：点启动后 `ss -ltn | grep 15020` = `LISTEN 0.0.0.0:15020` [材料]。

**连带冒烟·主站（--page 17，日志 /tmp/mbms_ui3.log）**：
- v01：SpinBox 16/16 → v02：首连轮询 → v03：读 HR 帧显示「请求: 4  错误: 0」零错误（**本会话直读 `v03_hr_read.png` 确认**：请求 4 / 错误 0，寄存器表 0x0000=0x04D2(1234) 与从站 u03 写入一致；DO 红={02,04} 与从站 u02 一致）；
- **DI 一致性表述修正（审计修正③，本会话目验证实）**：v03 主站侧 DI 红={01,03,05,**10**}，而从站 u02 时刻 DI 红={01,03,05}——**DI10 主站多出一枚红，「DI 16 点与从站逐点一致」不成立**。两帧非同时刻（u02 01:51、v03 01:52）且无从站侧同刻配对截图，也无证据表明期间有从站侧置位 DI10 的操作；DO 与 HR 的一致性不受影响。需补「与主站读回同时刻」的从站侧截图作配对证据（见已知限制 2）。

**核心断言·三轮连接/断开循环（CLOSE-WAIT 重验，[材料]）**：
- 第 1 轮：断开后 `ss -tn | grep 15020` 在 t0 / +5s / +20s 三查均无输出（exit=1），无 FIN-WAIT-2、无 CLOSE-WAIT；
- 第 2 轮：重连 ESTAB(57533↔15020) → 断开瞬间出现短暂 CLOSE-WAIT(15020↔57533)+FIN-WAIT-2 → +4s 复查已消失（cw_exit=1）；
- 第 3 轮：重连 ESTAB(44421↔15020) → 断开 +6s 复查无任何 15020 socket（cw_exit=1、any_exit=1）。
- 结论：从站 server 在主站断开后 **0~4 秒内由应用层 close**，不再永久残留；三轮断开不累积、重连均成功（server 持续可服务）。对照基线：上轮失败描述为断开后 CLOSE-WAIT(15020↔33969) 在 6s/30s 复查均在、直至从站进程 kill 才消失。此即 1.3「CLOSE-WAIT 死链巡检」根修的目验面。

**崩溃监控**：`grep -ciE 'segmentation|assert fail|abort' /tmp/mbsl_ui5.log /tmp/mbms_ui3.log` = 0/0 [材料；本会话复跑同 0]。两实例全程存活至收尾。
**日志定性补充（审计修正⑤，本会话读两日志原文）**：两日志启动早期（app created 阶段 [0.018]/[0.019]）各出现 **1 条** `XError [XClass.c][ArgIsNULL_report][LINE:11]「XClassGetVtable(object)是NULL 函数名:XClass_deinit_base」`；此后全程正常运行（切页/绘制/交互/退出链完整），未再现、非崩溃。另两日志首行各有 1 条「XOpenIM 失败——XIM 不可用（西文不受影响）」已知无害提示。

**环境事件记录（审计修正⑤）**：原计划用 :93 已被其他会话抢先占用（对方 Xvfb PID 449249，01:47 起，1280x900；我方首启报 "Server is already active for display 93"）；改用空闲 **:89**（1920x1080x24，PID 449843）完成全部目验。收尾 kill 两实例与 :89 Xvfb 后：无残留进程——**该结论仅限本轮 :89 环境**（系统另有他会话 `--page 19` 实例在跑）；15020 不再监听、无 CLOSE-WAIT [材料]。

### 2.3 证据清单

- **截图目录 `/tmp/ppt/mbsl/`（实际路径，审计修正⑤；本会话 ls 核实在盘）**：从站 `u01_rebuild16.png`、`u02_mixed16.png`、`u03_hr1234.png`、`u04_started.png`（01:51）；主站 `v01_pts16.png`、`v02_connected.png`、`v03_hr_read.png`、`v04_after_2nd_cycle.png`、`v05_after_3rd.png`（01:52–01:53）。实现会话全宽布局截图：`/tmp/mbslave_new_layout.png`（10-19 00:21，目验：参数合并行、DI/DO 标题行+圆圈横排、IR 段标题+数量 16、满宽表+格内编辑框均正确 [材料]）。
- **门输出**：`/tmp/wf_mbslave_layout_gate_run1/`、`/tmp/wf_mbslave_layout_gate_run2/` 各 9 件（build.log、autotest.log、apitest_containers.log、`*.fails`/`*.base.fails`/`*.newfail.log`）[本会话 ls + 逐项复核]。
- **命令链 [材料]**：`Xvfb :89` + `DISPLAY=:89 ./bin/XGuiWindowDemo_Test --page 18`(/tmp/mbsl_ui5.log) 与 `--page 17`(/tmp/mbms_ui3.log)；抓帧 `xwd -id <win> | xwdtopnm | pamdepth 255 | pnmtopng`；交互 `xdotool mousemove/click/key/type`；socket 三态追踪 `ss -tn | grep 15020`、`ss -tn state close-wait | grep 15020`，时点=断开后立即/+4s/+5s/+6s/+20s。

### 2.4 本会话（报告撰写）核对动作声明

- **实跑/实读**：上述表格中全部 wc/comm/grep、门脚本全文、`xgui_demo_page_modbus.c` 全部引用行号、`XTableWidget.c:504/:604/:618/:1824` 四挂点及宿主函数、两份 UI 日志原文、u02/v03 两张截图直读、证据目录与文件清单。
- **未重跑**：门脚本本体、make 构建、`--autotest`/`--apitest`、目验交互链与 ss 时序（材料实跑，产物在盘并已复核）。
- **未做**：任何 git 写操作；除本报告落盘外无文件写入。

---

## 三、已知限制

1. **注释与实现口径不一致（未落地）**：契约 2（:2051-2056）与结构体注释（:493-495）仍述「IR/HR 各一枚共享槽按指针反查」「2 列×16 行」旧口径，与 256×2 编译期槽实现（:2602/:2690）相悖。须改注释服从实现（或反向改代码），二选一，见 1.5。本会话实测仍为旧文。
2. **DI10 配对证据缺失**：主站 v03 读回 DI 红={01,03,05,10} 而从站 u02 为 {01,03,05}，非同帧、无从站侧同刻截图，期间无置位操作证据；「DI 逐点一致」不成立。待补同刻从站配对截图并解释 DI10 差异。
3. **基线口径差**：材料所称「18 条存量失败」与门实采基线（autotest 9 + apitest 25）不符；按「新失败 ⊆ 基线」判定未扩大失败集，差异原样记录，未改基线。
4. **默认行数下无表内滚程**：满高全展开设计下 16 行时表内滚动恒为 0；滚动置中/回零审计以 IR 扩 64 制造真实滚程覆盖（实现会话 autotest 手法）——即默认 16 行形态的表内滚动行为实际无覆盖。
5. **门判定串未落盘**：`MBSLAYOUT_GATE_PASS` 为脚本 stdout；本报告以两轮产物逐项复核等价条件（2.1 表）替代。
6. **本会话未重跑门/构建/目验**：「最终门=true」的依据是材料两轮实跑声明 + 本会话产物复核一致，非本会话重跑结论。
7. **目验环境限定**：全部目验在 :89 完成（:93 被其他会话占用）；「无残留进程」仅限 :89 环境，系统另有他会话 `--page 19` 实例共存。
8. **两条已知非致命日志项**：启动早期各 1 条 `XClass_deinit_base` vtable-NULL XError（XClass.c:11）、`XOpenIM 失败`提示；均不影响会话运行（见 2.2 崩溃监控节）。
9. **IR/HR 上下堆叠为取舍结果**：并排需两层嵌套且表格在盒式布局条目不贡献可分配宽（前序注释理由）——如需并排须另解布局供给宽问题。
