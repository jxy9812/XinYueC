# XSplitter × Qt6.8 双向 API 面审计报告（2026-10-18）

> 事实来源约定：凡「实测」均为本报告撰写轮（2026-10-18 07:3x–07:5x）亲自执行的命令，命令与输出附后；凡引用审计矩阵/整改自述的条目均注明出处轮次，不重复为其背书。行号以本轮 `stat`/`sed`/`rg` 读到的工作区为准（`Src/XGui/Widget/XSplitter.c` mtime 07:21:42、`.h` 02:28、`xgui_demo_apitest_containers.c` 07:24:58；冒烟期间 `bin/XGuiWindowDemo_Test` 曾于 10:12 被并行流程重链，但 XSplitter 源码 mtime 未变，冒烟结论仍对应 07:21 版源码）。

---

## 一、双向 API 面矩阵结论（缺失 / 差异 / 冗余 三清单）

### 1.1 缺失清单（Qt 有、XSplitter 无）

首版 11 条 missing 全部保留有效（条目与出处见任务书矩阵 missing 第 4 条；pick/trans 在 `qsplitter_p.h:72-80`、handleWidth 属性文档在 `qsplitter.cpp:1603-1612` 两处行号校正随附）。三条带定性说明的条目：

1. **4 个 Q_PROPERTY 未登记（降级为观察项）**：Qt `qsplitter.h:24-27` 有 orientation/opaqueResize/handleWidth/childrenCollapsible 四个 Q_PROPERTY；`rg -n 'setProperty|XObject_property' Src/XGui/Widget/XSplitter.c` 仅命中 pressOffset 内部记账。复核实测 9 个控件的 `XObject_setProperty` 计数里仅 XSplitter 为 2、其余全 0 —— 控件层普遍不登记 Qt 属性，非 XSplitter 特有缺口。
2. **样式回退钩子未接线（定性不变）**：Qt 的 `SH_Splitter_OpaqueResize`（`qsplitter.cpp:1472-1476`）与 `PM_SplitterWidth`（`qsplitter.cpp:1614-1622`）回落，XGui 侧常量与实现俱全（`Src/XGui/Style/XStyleOption.h:494`=101 / `:323`=25，实现 `XCommonStyle.c:3458` 返回 1、`XWindowsStyle.c:135` / `XFusionStyle.c:420` 返回 dpiScaled(4)、`XCommonStyle.c:3210` 返回 0），但本轮实测 `rg -n 'XStylePM_SplitterWidth|XStyleSH_Splitter_OpaqueResize' Src/XGui/Widget/XSplitter.[ch]` **零命中（exit=1）**。`XSplitter.c:928`（固定 handleWidth 5）、`:930`（固定 opaqueResize true）属未接线，非能力缺失。
3. **QSplitterHandle::sizeHint 样式查询未接线**：Qt `qsplitter.cpp:191-199` 用 `CT_Splitter` sizeFromContents；XGui `XStyleCT_Splitter=5`（`XStyleOption.h:402`）已有，且绘制路径已在用 `XStyleCE_Splitter`（`XSplitter.c:398` 分支所在 paint 循环），但本轮实测 `rg -n 'XStyleCT_Splitter' Src/XGui/Widget/XSplitter.c` **零命中（exit=1）**，`XSplitter_handle`（现行 `XSplitter.c:1115-1139`）直接返回固定 `m_handleWidth` 条带。

枚举/静态/自由件核查（复核第 8 条）：QSplitter 无 enum、无 static 成员、`qsplitter.cpp` 无 namespace 自由函数、`operator=` 因 `qsplitter.h:89` Q_DISABLE_COPY 显式删除，均不构成可对标公开 API 缺失。可选收紧：XSplitter 用 int 1/2 表达方向（`XSplitter.h:67`、`XSplitter.c:1033` 一带），仓库已有 `XOrientation`（`Src/XGui/XLayout/XGridLayout.h:82-86`）未被采用。

### 1.2 差异清单（同名 API 行为不同）

审计矩阵 diffs 19 条（子页显隐重排归因、orientation、handleWidth、getRange 判据、回归契约固化 5 条、farMin/farMax 迟滞等 13 项）经复核第 3 轮逐条抽点属实，结论不变，此处不复述原文。需并入阅读的两点：

- **回归契约固化改变了收敛成本**：`xgui_regression_test.c` 的 splitter 契约实际含 **19 处 `sp_expect`**（本轮 `grep -c 'sp_expect(' xgui_regression_test.c` = 19），其中 `:30587`（默认 opaqueResize 真）、`:30588`（handleWidth==5）、`:30627` 一带（restoreState 页数不匹配即失败）与 Qt 语义相反/不同，按 Qt 收敛会直接打破这些断言。
- **整改后 diffs 清单需更新状态**：getRange 判据、setCollapsible 越界、sizes 出参契约、saveState 字段、replaceWidget 告警、opaqueResize 只存不用 6 项已在本期落地（见第二节），其余 13 项保留为登记差异。

### 1.3 冗余清单（XSplitter 有、调用点核查）

外部调用点计数为本轮实测（命令：`rg -n --no-ignore-vcs -g '*.c' -g '*.h' -g '!build/**' -g '!out/**' -g '!Src/XGui/Widget/XSplitter.[ch]' 'XSplitter_<名>\b' .`），与整改自述计数存在小幅出入的以本轮为准（如 `setOpaqueResize` 从整改自述的 0 变为 **2**——整改自己在 `xgui_demo_apitest_containers.c:1477/:1480` 新增了两条真实调用；`refresh` 1→4、`getRange` 2→3 等为新增断言带入）：

| API | 外部调用数（本轮实测） | 主要调用点 |
|---|---|---|
| create / create_2 | 2 / 1 | `xgui_regression_test.c:30064`、`:30579`、`:30624`（唯一「零 Test/ 覆盖、仅回归测试」的构建依赖，删则破 `XGuiRegression_Test`，CMakeLists.txt:675-676） |
| create_ex / create_ex_2 / class_init | 0 / 0 / 0 | **非冗余**：create_ex/create_ex_2 是 `XSplitter.h:90`、`:96-97` 两个宏的展开目标（本轮 `sed -n '85,105p'` 确认宏体），删则打断宏展开；class_init 由 `XClassSetVtable(self, XSplitter)`（`XSplitter.c:772` → `XClass.h:54-55`）间接调用。判定口径：宏展开目标与框架虚表钩子不因字面零调用计为冗余。 |
| setOpaqueResize | 2（本轮实测） | `xgui_demo_apitest_containers.c:1477/:1480`（整改新增）。整改前为唯一真零调用项；因规则③要求修「只存不用」，本期按③接线而非删除，删留裁定仍在用户（见第四节）。 |
| handleClicked_signal | 1 | `Test/XGuiDemo/xgui_window_demo.c:3738`（**更正**：整改自述引 :3717，本轮 rg 实测为 :3738）。Qt 无对应，属纯扩展。 |
| insertWidget / init_2 / getRange / splitterMoved_signal / opaqueResize | 1 / 1 / 3 / 2 / 4 | `xgui_window_demo.c:1994`、`apitest:1488`、`apitest:1552/:1564/:1567`、`apitest:1535`+`xgui_window_demo.c:3742` 一带、`apitest:3 处`+`xgui_regression_test.c:30587` |
| addWidget / sizes / indexOf / setSizes / orientation / setStretchFactor / count / setOrientation / setHandleWidth / replaceWidget / isCollapsible / widget / setChildrenCollapsible / childrenCollapsible / handleWidth / restoreState / saveState / handle / setCollapsible / init | 13/15/9/9/8/7/7/6/8/5/6/4/7/4/7/6/5/5/5/5 | 全部 ≥1，均建议留；saveState/restoreState 真实调用在 `apitest:1592/:1598/:1605/:1609` 一带与 `xgui_regression_test.c:30623`（`Src/XGui/Widget/XWidget.h:897`、`XWidget.c:4972` 两处为注释级引用） |

框架侧依赖：`Src/` 内零真实调用，仅 5 处注释级引用（`XGuiServer.h:68`、`XMdiArea.c:360/:461`、`XWidget.h:897`、`XWidget.c:4972`）。

---

## 二、本期整改（删除项 + 行为修正项 + 出处）

改动文件（`git diff --stat` 本轮实测）：`Src/XGui/Widget/XSplitter.c`（+750/-164 量级，750 行变更）、`XSplitter.h`（43 行）、`Test/XGuiDemo/xgui_demo_apitest_containers.c`（124 行）、门脚本 `.zcode/wf-gates/wf_splitter_audit_gate.sh`。

### 2.1 删除项：零（一个未删）

- 三条字面零调用候选（create_ex/create_ex_2/class_init）经上表口径判定非冗余，未删；
- setOpaqueResize 按第三节的规则冲突取舍走了接线而非删除，未删。

### 2.2 行为修正 6 项（Qt 语义对齐，均已在源码落位，本轮逐条读码确认）

1. **getRange 最小贡献统一 qSmartMinSize + 交叉钳位**：`XSplitter.c:1046-1059` 的 `xsp_smartMin` 取 max(minimumSize, minimumSizeHint) 主轴分量，getRange 前后页贡献统一走它（`:1151-1160` 注释引 `qsplitter.cpp:541-554`，调用在 `:1165`/`:1171`）；交叉钳位于 `:1173-1185`（对标 `qsplitter.cpp:607-611`，逐个削减前页贡献而非锁死区间）。
2. **opaqueResize 接线（非透明拖动 = rubber band + 释放落位）**：`XSplitter.c:486-492`（注释块起 486，`#define XSPLITTER_PROP_DRAG_TARGET` 在 :492）定义待落位动态属性 `xgui.splitter.dragTarget`；`m_opaqueResize` 读点在 `:458`（绘制 rubber band）、`:762`（move 中只记 target）、`:793`（release 时落位），不再是只写不读；对标 `qsplitter.cpp:255-300`。
3. **setCollapsible 越界告警+早退**：`XSplitter.c:1220-1233`（对标 `qsplitter.cpp:1040-1043`），越界不再静默扩容写入。
4. **sizes() 出参契约**：`XSplitter.c:1294-1308`——count 超页数完全不写、不足只写 count 个，其余槽位保持调用方原值（对标 `qsplitter.cpp:1560-1574`）。
5. **saveState/restoreState 补字段 + 长度校验**：`XSplitter.c:57-61` 定义 `XSPLITTER_STATE_HEADER 13`（注释起 :57，宏在 :61）；saveState 于 `:1379-1391` 写 orientation/页数/handleWidth/childrenCollapsible（对标 `qsplitter.cpp:1642-1679`，本框架无 collapsed 故不落）；restoreState 于 `:1411-1417`（注释起 :1414）补长度校验（原先仅凭 3 字节魔数按下标读满头+页尺寸，短包越界读）。
6. **replaceWidget 越界/兄弟护栏告警**：`XSplitter.c:1076-1086`（注释起 :1076，XPrintf 告警在 :1079/:1086，对标 `qsplitter.cpp:1127-1149` 的 qWarning 分支）。

未落地并已说明取舍（整改自述，本报告核对无出入）：farMin/farMax 折叠迟滞与 collapsed 字段（需改结构体+拖动模型，且 `xgui_regression_test.c` 不在可编辑范围）；restoreState「页数不匹配即失败」保留（回归 `:30627` 固化）；索引空间、stretch 作用于已设意图页、refresh 语义窄化、sizer 惰性求值、qGeomCalc 布局内核维持登记差异。

### 2.3 附带修正（冒烟发现的收起死态）

`XSplitter.c:90-101` 新增 `xsp_bandAfter`（末槽之后无条带；相邻两槽不全隐藏即保留条带），`xsp_reservedBandLen`（`:104-113`）按槽位对计数，`xsp_contentLen`（`:115-130`）用 reserved 扣减；paintEvent `:393-406` 对隐藏页保留条带绘制，并明确注记这是**有意偏离 Qt**（`qsplitter.cpp:376-380` 收起后不留把手），理由是本实现无把手部件对象、条带兼作命中区，照 Qt 隐藏会造成「收起后无法展开、按钮一并消失」的死态。

### 2.4 apitest 同步

`git diff -U0 -- Test/XGuiDemo/xgui_demo_apitest_containers.c | grep -c '^+.*XAPI_EXPECT'` 本轮实测 = **13**（整改自述写「6 条」不准确）；删除 1 条被 Qt 语义作废的断言（「页 0 不可折叠后拖动下限=其当前尺寸 315」，见门日志 fail diff）。

**重要缺陷（本报告新发现）**：新增的 2 条 saveState 断言（`xgui_demo_apitest_containers.c:1646`「restoreState 截断快照返回 false（长度校验）」、`:1659`「快照往返恢复 handleWidth/childrenCollapsible」）位于 `#if XBYTEARRAY_ON` 块（`:1620-1663`）内，而该宏**全仓无定义处**（`rg 'define XBYTEARRAY_ON'` 与 `rg 'XBYTEARRAY_ON' Src/` 除注释外零命中；XByteArray.h 用的是正确拼写 `XByteArray_ON`），因此整块 8 条断言**从未参与编译执行**——包括整改声称已验证的 saveState 魔数/往返断言。实测佐证：`grep -a -c 'XSplitter saveState\|XSplitter 快照'` 在 07:32 门日志、`/tmp/apitest_new3.log`、基线日志中均为 **0**。该拼写错误在 HEAD 版本即存在（`git show HEAD:... | grep -n XBYTEARRAY_ON` 命中 :53/:1459/:1591），但整改新增的 2 条断言被放进了这个死块，使规则③⑤ 的核心验收断言**没有任何运行覆盖**。整改后 saveState 修正的验证现状：仅由 `XGuiRegression_Test` 的 `sp_expect`（saveState 生成、页数不匹配失败）间接覆盖，长度校验与字段往返两条路径无测试。

---

## 三、验证

### 3.1 构建与整改门（本报告轮亲跑）

命令：`timeout 900 ./.zcode/wf-gates/wf_splitter_audit_gate.sh /tmp/rpt_gate.log` → 实测输出：

```
=== XSplitter 整改门 2026-10-18 07:37:11 ===
BUILD_RC=0
RUN_RC(--autotest)=0 / RUN_RC(--apitest=containers)=1
autotest 比对: (无)   apitest 比对: (无)
AUDIT_GATE_PASS
```

- 构建产物 `bin/XGuiWindowDemo_Test` 本轮门跑时未重编（`.build` 日志全为 "Built target"，无编译行；`stat` 显示该二进制 07:26:21，晚于 XSplitter.c 07:21:42，源-码时序自洽）。
- **门禁披露义务（复核第 4 条，本报告接受）**：门 PASS 依赖排除文件 `.zcode/wf-gates/splitter_autotest_exclude_1018.txt`（整行排除「窗口设置页 FAIL / 扩展页19(窗口设置) FAIL / [FAIL] 窗口设置页: 有把页定角才位（干生数）」3 行，归因并行「窗口旋转」lane，脚本注释自述「旋转 lane 落地后应删除」）。脚本注释中的自证前提「XSplitter.c/.h 自 02:26 起零变动」已被事实推翻（XSplitter.c mtime 07:21:42），「与 XSplitter 无交集」不再由时间线成立——但本轮实测这 3 行**根本未出现**于 07:32 与 07:37 两次门的 autotest 输出（`grep -Fxf exclude wf_audit_gate_final.log.autotest` exit=1），即窗口设置页族当前为 PASS，排除形同虚设但仍挂在门上。建议：删除该排除文件与脚本内对应逻辑，或改用 `.zcode/wf-gates/autotest_baseline_workspace_20261018.log`（07:18 版，其 .note 明确「该失败属他人未提交的分割条/窗口改动带进来的既有回归」）作为并列基线——门脚本目前仍指向 02:02 的 `autotest_baseline_1018.log`，基线选取口径需一并说明。
- 告警：本轮门未强制重编，未复现整改自述的「强制重编后 2 条告警」；整改者留档 `/tmp/warn4.log`（07:21 强编日志）中 XSplitter 告警为 **2 条**：`XSplitter.c:386:28`（xsp_childCount 指针类型不兼容）与 `:1412:46`（XByteArray_data 丢 const）。整改自述的「3 条、:345/:349/:1332」为旧行号且多计，以 warn4.log 为准。本报告轮未独立强制重编复现，如实声明。

### 3.2 apitest=containers 基线对比（门产物，本轮门跑）

- 基线 `.zcode/wf-gates/apitest_containers_baseline_1018.log` 聚合计数实测 `失败=26`（两处：套件行+总计行）；本轮门跑后 25。真实降幅 **26→25**（整改自述与门脚本注释写的「27→25」有误，基线文件即 26）。
- fail 行 diff 仅 1 条：`< [FAIL] XSplitter 页 0 不可折叠后拖动下限=其当前尺寸 315`（被整改删除的作废断言）。
- 新增断言执行状况：13 条新增 XAPI_EXPECT 中，本轮门日志可见 PASS 的含 opaqueResize 三条（:185-187）、sizes 不写入（:198）、smartMin/getRange 两条（:205-206）、setCollapsible 越界（:209）、空间不足不被压缩（:228）、隐藏页条带保留两条（:229-230）、末槽让出末端条带（:232）等；**2 条 saveState 断言因 2.4 所述死宏从未执行**。

### 3.3 XGuiRegression_Test（本轮亲跑）

命令：`DISPLAY=:95 timeout 600 ./bin/XGuiRegression_Test` → `RC=1`；`grep -a -c 'SP-FAIL' /tmp/rpt_reg.log` = **0**（splitter 契约 19 条 sp_expect 全过，含 saveState 生成与 restoreState 页数不匹配即失败）；`grep -a -oE 'FAIL: .*' | sort -u | wc -l` = **34** 条 distinct 失败（整改自述 34、复核意见 33，本轮我方实测 34），内容集中在 SVG / XImageReader / XCheckBox / XDial / XKeyboard 等族，无一条涉及 XSplitter。与 07:25 的 `/tmp/reg_final4.log`（33 条）相比本轮多出 1 条：`r1#4 exec 定尺=max(基线140,内容实测+CSD装饰高)`（键盘族，与 XSplitter 无关）。

### 3.4 导航分割条 UI 冒烟（本轮亲跑，针对当前二进制重跑绑定）

本轮另做一次针对现行二进制（07:26:21，晚于全部源码改动）的独立冒烟，补上「整改自述冒烟未绑定最终产物」的缺口。工具链：`xdotool` 真实鼠标驱动 + `xwd|xwdtopnm|pnmtopng|pngtopnm` 抓帧 + 自写 `/tmp/rpt_full.py` 直读像素（把手判据 = Mid 灰 (184±12) 连续列/行 ≥200/400 像素；面板侧别按灰带距窗口缘 ≤8 判收起、按带心位置判左/右）。日志 `/tmp/rpt_smokerun.log`，截帧 `/tmp/rpt_shots/`。

逐项结果（窗口 1000×700 @ Xvfb :96，窗口原点 (40,40)）：

| # | 项 | 结果 | 实测数据 |
|---|---|---|---|
| 1 | 左停靠初始态 | 通过 | band=(176,183) hw=8 panel=176 |
| 2 | 左停靠「收起」把手贴缘 | 通过 | 收起后 band=(2,7) hw=6，紧贴左缘，panel=0 collapsed=True |
| 3 | 左停靠「收起后点击把手位」展开恢复记忆宽 | 通过 | 点带 (44,340) 后 band=(176,183) panel=176，记忆宽 176 精确恢复 |
| 4 | 右停靠到位 | 通过 | band=(816,823) hw=8 panel=176 |
| 5 | 右停靠「收起」把手贴缘 | 通过 | band=(992,997) hw=6，紧贴右缘，collapsed=True |
| 6 | 右停靠「收起后点击把手位」展开恢复记忆宽 | 通过 | 点带 (1034,340) 后 band=(816,823) panel=176 精确恢复 |
| 7 | 上停靠到位 | 通过 | orient=H band=(194,201) hw=8 panel=194 |
| 8 | 上停靠收起 | **未完成** | 收起钮点击后 band 不变（(194,201)）；本轮点击坐标按「面板顶+4」推算（`xgui_window_demo.c:2133/:2137` 横版 y=4），一次点击未生效，未做多轮重试 |
| 9 | 下停靠收起/展开 | **未完成** | 上步未切换到下停靠，后续判据失效；本轮未取得下停靠数据 |
| 10 | 进程存活/崩溃扫描 | 通过 | 整轮 `alive=True`；`grep -a -icE 'segmentation|assert|abort|core dump|Fatal' /tmp/rpt_ui.log` = 0 |

上表 1-7 对应整改自述「③收起把手贴缘 + 点带展开恢复记忆宽」两缺陷在左/右停靠的修复确认——整改自述称左/右/上/下四边均 5/5 通过，但其证据 `/tmp/p1.log` 实测数字与自述对不上（见 3.5），本报告仅对上表 1-7 这 7 项给出「本轮亲测通过」结论；第 8、9 项（上/下停靠收起链路）本轮**未验证**，不采信任何一方的历史数字。

### 3.5 对整改自述冒烟数字的更正（复核意见第 1/2/6 条，本报告接受）

- 整改自述「右缘 8px×**512** 行」「恢复 **208**（把手 784..791）」「底缘 y=**666..673**」在唯一含 band 记录的日志 `/tmp/p1.log` 中零命中：p1.log 实测右停靠收起态为 `band0=(992,0,8,`**606**`)`、dock=1 展开后 `pw=`**176** `band0=(816,0,8,606)`、下停靠 `band0=(0,`**598**`,1000,8)`；`grep -a 'pw=208' /tmp/p1.log` = 0（208 只出现在 02:58 的 `/tmp/dbg14.log`，非本轮冒烟）。
- `/tmp/audit_ui.log`（02:44）`grep -a -c -E 'dock=|band0=|col=|pi='` = **0**，不含任何分隔器证据，不构成 11 条冒烟结论的支撑；`audit_ui3/4/5.log` 同样为 0。真实历史证据在 `/tmp/p1.log` + `/tmp/docksmoke2.py`。
- 上述更正不改变「收起贴缘/点带展开」两缺陷已修复的方向性结论（本轮 1-6 项独立复证），但整改自述中 512/208/666 三个数字及「46 张截图 /tmp/r5_*.png」的引用应以 p1.log 实测值（606 / 176 / 598）替代；本报告 3.4 的数字为新一次独立测量，与 p1.log 并列留档。

---

## 四、保留的 XGui 扩展（有调用点，留待用户裁定）

以下 API 为 XGui 相对 Qt 的扩展或已知保留差异，本期一律未删未改语义（除第二节已列修正），删留交由用户裁定：

1. **handleClicked_signal**（`XSplitter.h:277-281`，Qt 无对应）：空闲点击发 doubleClick=0、双击发 1，供 demo 导航面板「双击收起/单击展开」使用。唯一调用点 `xgui_window_demo.c:3738`。
2. **setOpaqueResize**（`XSplitter.h:201-204`）：整改前唯一真零外部调用项，但 Qt 有对应语义且本期已接线（第二节 2.2-②），现有 2 处 apitest 调用。删除需用户拍板。
3. **insertWidget / refresh / init_2 / handle**：均为 Qt 有对应或近似对应但签名/用途本地化的扩展，外部调用点见 1.3 表，全部 ≥1。
4. **restoreState「页数不匹配即失败」**：有意保留的 XGui 专有语义（Qt 按实际页数钳位消费，`qsplitter.cpp:1712-1732`），被 `xgui_regression_test.c:30627` 固化；改判需同时改回归契约。
5. **paintEvent 隐藏页保留把手条带**：有意偏离 Qt（`XSplitter.c:400-406` 注记），防收起死态；如需严格对齐 Qt 需先引入独立把手部件对象。

---

## 五、未实现的 Qt 缺失 API 清单（等用户点头）

本期全部未实现、仅登记（整改规则④执行结果，本报告核对无出入）：

1. `CT_Splitter` sizeFromContents 接线（`XSplitter_handle`/handleWidth 样式化）；
2. `PM_SplitterWidth` / `SH_Splitter_OpaqueResize` 样式回落接线；
3. 4 个 Q_PROPERTY 登记（观察项，见 1.1-1；建议等框架级属性系统再统一做）；
4. `QSplitter::sizeHint` / `minimumSizeHint`（`qsplitter.h:50-51`）；
5. `createHandle` / `moveSplitter` / `closestLegalPosition` / `setRubberBand`（`qsplitter.h:75/:83/:85/:84`）；
6. `childEvent` / `event` / `changeEvent` / `resizeEvent` 缺省事件链（`qsplitter.h:77/:79/:82/:80`）——子页显隐→LayoutRequest 投递对位缺口的正解需其一；
7. `QSplitterHandle` 全类 13 项（`qsplitter.h:96-125`）；
8. farMin/farMax 折叠迟滞与 collapsed 状态字段（`qsplitter.cpp:577-628` + `qsplitter_p.h:34/:39`）——改动面涉及结构体与拖动模型，且需动回归契约；
9. restoreState 按实际页数钳位消费（对齐 `qsplitter.cpp:1712-1732`）——需先解除 `xgui_regression_test.c:30627` 的语义固化；
10. 类型收紧：int 1/2 → `XOrientation`（非 Qt 缺失，属仓库一致性建议）。

---

## 附：本报告轮执行清单（命令与结果一览）

| 检查 | 命令 | 结果 |
|---|---|---|
| 整改门（含构建+两套测试+基线比对） | `timeout 900 ./.zcode/wf-gates/wf_splitter_audit_gate.sh /tmp/rpt_gate.log` | rc=0，AUDIT_GATE_PASS；apitest 失败 26→25，唯一 diff 为删掉的作废断言；autotest 无基线外新失败 |
| 回归套件 | `DISPLAY=:95 timeout 600 ./bin/XGuiRegression_Test` | rc=1；SP-FAIL=0（19 条 splitter 契约全过）；34 条既有 FAIL 全在无关族 |
| apitest 断言计数 | `git diff -U0 ... \| grep -c '^+.*XAPI_EXPECT'` | 13（非自述的 6）；其中 2 条 saveState 断言在死宏块内从未执行（`grep -c 'XSplitter saveState' ` 四份日志均 0） |
| 基线计数 | `grep '失败=' apitest_containers_baseline_1018.log` | 26（非自述的 27） |
| 样式钩子零命中 | `rg 'XStylePM_SplitterWidth\|XStyleSH_Splitter_OpaqueResize' XSplitter.[ch]` | exit=1 |
| CT_Splitter 零命中 | `rg 'XStyleCT_Splitter' XSplitter.c` | exit=1 |
| 宏展开目标 | `sed -n '85,105p' XSplitter.h` | create/create_2 为宏，展开到 create_ex/create_ex_2（:90/:96-97） |
| 告警（整改者留档核对） | `grep XSplitter /tmp/warn4.log` | 2 条（:386、:1412），非自述 3 条 |
| UI 冒烟（当前二进制） | `/tmp/rpt_run.sh`（xdotool+xwd 像素直读） | 左/右停靠收起贴缘+点带展开记忆宽 6 项通过；上/下停靠收起链路本轮未完成；进程存活、崩溃扫描 0 |

**未运行/未验证（如实声明）**：本轮未强制重编 XSplitter.c 复现告警（仅核对 07:21 留档）；未做 Qt/qsplitter 逐帧对照；未做 Xvfb 之外跨平台/DPI 验证；未验证收起态下拖动把手改尺寸；上/下停靠收起链路本轮未取得数据（见 3.4 第 8/9 行）。测试期间我启动的 `XGuiWindowDemo_Test`（:96 上）已 kill；Xvfb :95/:96 非我启动，未动。测试全程未改任何业务代码。
