# 页面懒分配交付报告（page-lazy-alloc-1019）

- 日期：2026-10-19　分支：`codex/xdevice-file-platform`
- 范围：XGuiDemo 14 个扩展页「首显构建 + 常驻不销毁」全量懒分配
- 结论速览：启动峰值 RSS **50.2 → 39.6 MB（−21.2%）**；机制已落地、构建零新增告警、ASan 无新增 UAF 签名；**最终门禁未通过**（18:03 GATE_FAIL：从站页新失败 + EXT_PAGE_PASS_COUNT=11<12），从站页回归归因待查（详见 §3.1、§3.5、§四）。

**证据与行号口径**（全文适用）：
- 量化数据：`/tmp/lazy_quant.log`（第一轮分析 :1051-1106，第二轮更正块 :1141-1153，**本文采用第二轮更正口径**）；汇总 `/tmp/lazy_quant/stats.txt`。
- 最终门禁：`/tmp/wf_lazy_final.log`（18:03）；目验：`/tmp/lazy2/`；ASan A/B 树：`/home/xinyue/lazyasan_base`（仓库外）。
- 行号双口径：`xgui_window_demo.c` 现状行号系本次撰写时 grep 实测（g_extBuilders=:1467、demo_ext_ensureBuilt=:1555、切页 ensureBuilt=:2570、CLI autostart=:5443/:5445、键盘钩子=:5515-5516）；标注「快照」的行号取自实验时点快照 `/tmp/lazy_quant/xgui_window_demo.c.pristine`（14:14），各页文件（modbus/remote/keyboard）行为快照行号；17:59 后 `xgui_demo_page_modbus.c` 行号已漂移（从站断言 :4949→:5007 起）。

---

## 一、量化结论（RSS 差值与策略取舍）

### 1.1 测量口径
- 指标：峰值 RSS 取 **VmHWM**（轮询 `/proc/<pid>/status`，20ms；环境无 `/usr/bin/time`），Xvfb :90，窗口 1000x700，**每配置 3 跑取均值**。
- 方法：临时 hack 环境开关 `XGUI_DEMO_LAZY_ONLY`（=none/=exti，跳过 builder、补空白占位壳保持堆叠下标）注入 `xgui_window_demo.c` 后 `make XGuiWindowDemo_Test`（build-direct, Debug）；**48 次运行矩阵 = 16 配置 × 3**（A 全量现状 / BASE 全跳 / 14 个扩展页逐页单建）。

### 1.2 RSS 差值

| 配置 | VmHWM 均值 | 说明 |
|---|---|---|
| A 全量 19 页现状（--page 18） | 51449 kB ≈ **50.2 MB** | 改动前行为 |
| BASE 全部扩展页跳过（空白占位保持堆叠下标） | 40552 kB ≈ **39.6 MB** | |
| 差值 = 扩展页常驻总量 | 10897 kB ≈ **10.64 MB** | = 全进程 RSS 的 **21.2%**（第一轮误标 20.7%，第二轮更正，/tmp/lazy_quant.log:1141） |

逐页单建增量（vs BASE，kB 均值，头部排序按增量降序）：

| 页（页码, exti） | 增量 | 备注 |
|---|---|---|
| Modbus 主站（页17, exti12） | +1800 ≈ 1.76 MB | ★最重 |
| Modbus 从站（页18, exti13） | +1436 ≈ 1.40 MB | build 即建 XModbusServer + 1s reapTimer |
| 网络设置（页13） | +1181 | |
| 远程服务器（页10） | +1049 | |
| 软件更新（页16） | +990 | |
| 悬浮窗设置（页14） | +902 | |
| 二维码（页15） | +845 | |
| effects | +809 | |
| dialogs | +782 | |
| advanced | +716 | |
| 远程客户端（页11） | +700 | |
| 窗口设置（页19） | +688 | |
| views | +486 | |
| keyboard | +456 | 另拉起应用级虚拟键盘单例；**与全距 408kB 同量级，单列存疑** |

- **SUM 单页增量 12840 kB > ALL−BASE 10897 kB** → 页间共享折减系数 **0.849**（=10897/12840，/tmp/lazy_quant.log:1142；第一轮误标 0.83，折算值主站≈1.49MB/从站≈1.19MB 本就按 0.849 计算，系系数标签笔误）。
- **运行间噪声**：全距 124kB~820kB（effects 124 最小 / advanced 820 最大），半距 ±0.06~0.41MB（log:1150-1151）。轻页增量处于噪声边缘；Modbus 双页/网络页（自身全距 128~580kB，增量 1181~1800kB）显著超噪、头部排序可信，尾部轻页排序仅作参考（log:1152-1153）。
- **A（--page 18）vs 单建从站页**：51449 → 41988 kB ≈ **50.2 → 41.0 MB**（log:1082）——当前页自身仅占 1.4MB，其余 9.2MB 是 **13 个不可见扩展页（14 可建 − 当前页）** 的常驻控件树。

### 1.3 策略取舍：推荐【全量懒分配】
14 个扩展页统一「首显构建 + 常驻不销毁」，**不做混合清单**。理由：
1. 启动 RSS 50.2 → 39.6MB（−21.2%），全部 14 页无差别适用同一机制；
2. Top-6 重页（主站1800+从站1436+网络1181+远程服务器1049+更新990+悬浮窗902）= 7358kB ≈ **7.19MB raw，×0.849 ≈ 6.10MB，已超全懒收益（10.64MB）一半**；全懒额外多省 ≈**4.54MB**（10.64−6.10）+ 免清单维护 + 销毁风险零触发（log:1145-1147；第一轮「Top-N 不足一半」论据已撤回）；
3. 「常驻不销毁」使销毁语义与现状完全一致（页面仍活到进程退出，demo 本无页级析构钩子），销毁对称性风险零触发（§2.3）；
4. 先例同向：嵌入式 kExtBuilders 全 NULL（快照 xgui_window_demo.c:3612-3617）与 XConsoleShell 懒分配（83KB→6.8KB）。

**未量化项**：各页 build 耗时（ms 级未测）；连接态增量——远程视频缓冲（ffMap，`Src/XGui/Remote/XGuiClient.c:1601`；frame，`XGuiServer.c:731`）与 Modbus 轮询数据均连接后才分配，懒构建不影响。

---

## 二、实现（惰性边界 / 红线保护 / 销毁对称）

改动文件：`Test/XGuiDemo/xgui_window_demo.c`、`Test/XGuiDemo/xgui_demo_pages.h`、`Test/XGuiDemo/xgui_demo_page_keyboard.c`、`.zcode/wf-gates/wf_lazy_gate.sh`（新增门）。

### 2.1 惰性边界
1. **注册表惰性**：`kExtBuilders` 由 `DemoWin_create` 局部提升为文件级 `g_extBuilders[15]`（现状 `xgui_window_demo.c:1467`，快照 :1448 附近；**14 个非 NULL builder + 1 图表页 NULL 占位**，快照 :3611-3632）。注册循环只建空占位壳 `XWidget_create` 堆对象并写入 `m_extPages` 槽位本身（main 尾部几何分支照旧命中 1000x700，A/B PNG 均 1000x700 实证）；新增 `m_extBuilt[15]` 幂等标记与 `demo_ext_ensureBuilt`（现状 :1555，快照 :1595 附近；槽位声明 :530-531）：insertWidget 同位换入 + removeWidget 摘壳，`VXStackedLayout_takeAt` 的 oldCurrent 补偿保证「堆叠下标 = 页索引」与当前页稳定；构建回落（裁剪）时槽位清空 = 旧「跳过注册」口径；占位壳退役走 `XObject_deleteLater` 仓库定式（无连接故无断连面）；**页常驻不销毁与现状同语义**，既有 deleteLater/断链定式零触碰。
2. **大表格收编**：`DemoWin_create` 原地组装段（快照 :4409-4445）逐式移入 `demo_ext_viewsBigTableHook`（现状 :1504，ensureBuilt 内真建 exti0 后调用 :1591），views 页 CHILD_ADDED 收编机制不变。
3. **切页契约**：`demo_switchPage` 在 setCurrentIndex 前 `ensureBuilt(index-5)`（现状 :2570，快照 :2399）；内建 autotest 调度改「先建再测」（`g_extBuilders` 甄别 + switchPage 内建 + 槽位判空回落，现状 :1620-1633）；效果页两留证守卫（帧5切页/帧7截图）前补 `ensureBuilt(3)`（现状 :1758/:1771）。注：基线 keyboard/update 两既有 FAIL 使 `demo_ext_pages_autotest` 在回页 3 后即 `XGuiApplication_quit`，帧5/7 当前在基线与改动后同样不可达（base_run1 截图行=0 实证）；本修正对可达时点（全绿运行）正确硬化且不改变现状行为。

### 2.2 红线保护
1. **CLI autostart 红线**：main 在 `DemoWin_create` 后、事件循环前按 remoteCli/remoteClientCli.enabled `ensureBuilt(5/6)`（现状 `xgui_window_demo.c:5443`/`:5445`）——实测 `--remote-server --remote-port 46777` 在 ss 中 LISTEN 0.0.0.0:46777 并打「remote-server listening port=46777」；`--remote-client` 打「remote-client connecting host=127.0.0.1 port=46777」。
2. **键盘无头钩子豁免**：新增契约 `demo_page_keyboard_headless_requested()`（`xgui_demo_page_keyboard.c` 真身 + 裁剪桩，`xgui_demo_pages.h:310` 声明），`XGUI_KB_*` 环境族任一非空时先 `ensureBuilt(4)` 再调钩子（现状 `xgui_window_demo.c:5515-5516`）——XGUI_KB_AUTOSHOW=1+DUMP=1 实测 48 行 XKB-GEO 几何全量输出且 RC=0（基线同 48 行但 RC=1，栽在其自身退出 UAF）。
3. **契约头**：`xgui_demo_pages.h:14-26` 补懒分配契约段（「首显」入口三族 / 常驻不销毁 / build 时机从窗口创建期推迟到首显期、页内 build 不得依赖创建期时序的假设禁令）。

### 2.3 销毁对称
「首显构建 + 常驻不销毁」策略下，以下五项销毁对称性风险**全部不触发**（仅在「离开销毁」激进档才成立；若未来做激进档须先解决）：
1. **页18 Modbus 从站**：build 即建无父 `XModbusServer`（mb_slaveEnsureServer，快照 `xgui_demo_page_modbus.c:3377-3390`，create_ex 无 parent 仅 static 指针持有 `s_mbSlave`，快照 :562-563）+ 1s reapTimer（快照 :4918-4930，父挂页根级联）；须显式 disconnectDevice + delete（快照 :3404 先例）。
2. **页17 Modbus 主站**：pollTimer 无父须显式删（快照 :1771，pollTimer 连接成功才建，快照 :1906-1913）；device/reply 须 disconnectDevice（快照 :1800）+ retire 回收序列。
3. **页10 远程服务器**：`XGuiServer_create` 父挂主窗顶层（快照 `xgui_demo_page_remote_server.c:1245-1246`），页销毁不级联，须显式 `XGuiServer_close`（快照 :1341 先例）否则监听残留。
4. **页11 远程客户端**：`XGuiClient_create` 父挂页内 viewHost 级联✓（快照 `xgui_demo_page_remote_client.c:874`），但销毁前须 BYE 断链（快照 :1135 先例）；视频缓冲（ffMap `XGuiClient.c:1601`）连接后才分配。
5. **页9 键盘**：build 拉起应用级虚拟键盘单例（快照 `xgui_demo_page_keyboard.c:424`，XGuiApplication 拥有），非页内存、与懒分配正交；首次进入页9 会永久 +1 份单例。

**无风险项**：抓取——demo 仅 mouseGrabber 查询钩子（现状 `xgui_window_demo.c:4984`，快照 :4812-4814），无页面持有 grab；二维码页15——build 尾 qr_generate 一次性生成 pixmap，无定时器无连接；悬浮层定时器为主窗级非页级。

---

## 三、验证（门 + 目验逐项 + RSS 数字）

### 3.1 构建与门禁（**最终门 = false**）
- **构建**：等 make 空闲后 `flock /tmp/xinYueC_lazy.lock` 执行 `cd build-direct && make -j8 XGuiWindowDemo_Test`，RC=0，新增代码零告警（仅既有图表 XSpline 告警）。
- **门禁**（`bash .zcode/wf-gates/wf_lazy_gate.sh`：等 make 空闲 → flock build-direct 构建 → Xvfb :91 自启 → `--autotest` 与 `--apitest=containers` → 失败行抽取归一 → 对 qtalign 基线「新失败⊆基线」零排除集 → EXT_PAGE_PASS_COUNT≥12 完整性守卫；文件头记录两处在途 UAF 边界与不以退出码为准的口径）：
  - **15:41 / 15:47 / 15:49（autotest RC=139，门以行为比对为准）/ 17:37 四次 PASS**；17:37 时点 autotest/apitest「基线之外新失败=(无)」、EXT_PAGE_PASS_COUNT=12、FAIL 行子集 comm-23 手工复核为空。
  - **18:03 最终复跑 GATE_FAIL**（`/tmp/wf_lazy_final.log`）：扩展页18(Modbus 从站) FAIL（:11/:28）→ **EXT_PAGE_PASS_COUNT=11，低于下限 12**（下限=14 可建页 − 键盘/软件更新两基线 FAIL，:14-15）→「GATE_FAIL: 存在基线之外新失败或懒分配完整性守卫未过」（:30）。
  - ~~「基线 3 行在改动后真实转 PASS」的旧结论按审计撤销~~：XI2 触摸两锁 + 内建总口径 3 行为 qtalign 基线既有 FAIL（10-18 采集），两轮门日志 + at.log 三处证据均 FAIL，非本轮新增回归，本轮后仍 FAIL、无明细归因。

### 3.2 ASan（build-asan 树；产物 `build-asan/bin/XGuiWindowDemo_Test`，RUNTIME_OUTPUT_DIRECTORY 指向该处，已以运行期 ASan 报告证实插桩生效）
- Xvfb :92 切页巡回（`--autotest`）：与改动前 scratch 基线同样止于 Modbus 从站页**既有在途 UAF**（XModbusDataUnit.c:186 ← mb_slaveRebuildMap ← mb_slaveApplyCounts，两树栈逐帧相同；系 2026-10-18 四区同屏在途改动）。
- `--page 5..19` 单页生命周期巡回：15 页中 **14 页无任何 ASan 错误**；pg11 报 VX_xrb_deinit(XRemoteSessionBar.c:670) ← XObject_disconnect_1 UAF——与基线在 --page 10/11/18 三页同签名报错同源（基线 3 页中招、改动后仅 1 页，**无新签名**）。
- 泄漏量级：pg5..16 恒 4374B/85alloc 纯 fontconfig 三方退出缓存（页间常数 = 框架零页级泄漏、**占位壳零泄漏实证**）；pg17/18 增量栈全部落于在途 Modbus 改动 mb_slaveRebuildGrids（页常驻设计内分配；基线因自身退出 UAF 在 LSan 前中止，**无可比数值，如实报告为不可比**）。
- **asanClean=true 限于此口径**：相对改动前基线无新增 UAF 签名、无懒分配机制所致泄漏；两处在途 UAF 属他人未提交改动且文件不可触碰（XRemoteSessionBar.c / XModbusDataUnit.c 不在本任务可改清单），门脚本头与 scratch A/B 树 `/home/xinyue/lazyasan_base`（逆向懒分配补丁 + 同 flag 构建）留证。

### 3.3 红线 / 豁免 / 截图 / 还原实测
- `ss -ltn` LISTEN 0.0.0.0:46777 + 日志 listening（--remote-server）；--remote-client connecting 同证。
- `--page 5..19` 全 15 页 PNG 均 1000x700（占位入槽生效，几何分支未改）。
- XGUI_KB_AUTOSHOW=1 XGUI_KB_DUMP=1 → 48 行 XKB-GEO、RC=0。
- **量化实验还原**：`cp /tmp/lazy_quant/xgui_window_demo.c.pristine` 回原位，md5 8f2af66b… 与实验前快照一致（git diff 925/587 为本分支既有改动非本次引入）；重建通过；复跑确认 env 已被忽略（/tmp/lazy_quant.log:1106 `RESTORED_verify unset p18: VmHWM=51716kB`，与 A 基线 51449±300kB 同口径）。

### 3.4 目验逐项（/tmp/lazy2/，passed=true）
1. **修复项1 从站页「点一次圆圈验证」**：已修复并可执行（17:31 二进制，21876784 字节）。XTEST 鼠标点击全部生效：点「远程」分类钮展开手风琴（m1_c.png）；点「Modbus 从站」switch page=18（ui.log）；从站页 DO00 圆圈红→绿（p18b_c.png）；巡回后二次进入 18 保持绿色、状态保留（p18c_c.png）；再点绿→红（p18d_c.png），双向交互完整。
   **⚠ 时效标注**：以上证据采集于 17:31 二进制；17:59 `xgui_demo_page_modbus.c` 被修改重建后**未复验**，且 18:03 门禁从站页 autotest FAIL——交互结论待最终二进制复验。
2. **修复项2 --autotest 段错误**：已修复。重跑 `DISPLAY=:91 ./bin/XGuiWindowDemo_Test --autotest` 正常退出 exit=0，日志尾「XGuiWindowDemo: 已退出」（/tmp/lazy2/at.log）；对照上轮 autotest.log/autotest2.log 均 exit=139。注：exit=0 仅代表不再段错误，断言 FAIL 仍 16 行（§3.5）。
3. **修复项3 --remote-server 实例鼠标事件**：已修复。RS3（--remote-server --remote-port 46014，pid 623000，窗口 0x400002）点击全部生效：点「远程」分类展开、switch page=18、DO00 红→绿（r3_18b_c.png）、巡回切 17→11→10 成功。mousePress 诊断日志行**仅 --remote-server 实例**不再打印（rs3.log 计数=0）；CLI 实例 /tmp/lazy2/ui.log 仍有 6 行 mousePress（实测 grep -c = 6）。
4. **修复项4 页10 焦点链死**：已修复。切到页10 后 Tab×1 焦点「远程窗口」钮、Tab×2「远程客户端」钮（t10a_c.png/t10b_c.png 焦点框移动可见），Space 成功切页 11（ui.log 'switch page=11'），可正常切离页10。
5. **巡回冒烟：通过（页集合口径）**——覆盖 页17→18→11→10→15（两次）→16→19→二次18→回0，每页截图无破版（p17b_master.png、p18a_first18.png、p11b_client.png、p10b_server.png、p15b_qrcode.png（25×25 模块）、p19c_settings.png、p0d/p0e 首页）；任务指定 `grep -ciE 'segv|assert|abort' /tmp/lazy2/ui.log` = **0**。日志实际切换顺序为「启动页0→18→17→10→11→15（两次）→16→19→二次18→回0」（/tmp/lazy2/ui.log:9-36 实测）。监听保持复验：巡回后页10 右上角「监听中 :46014」仍在（r3_10d_c.png）且 `ss -tlnp` LISTEN 0.0.0.0:46014 pid=623000。
   **日志口径边界**：所有配置（含改动前 A 基线）启动段均有既有 XError「XClass_deinit_base 参数 XClassGetVtable(object)是NULL」（/tmp/lazy_ui.log:3-7 实测 :5、/tmp/lazy_quant.log 各配置），非本轮引入、grep 口径未覆盖，留待另行排查。
6. **⑤ 收尾**：Xvfb :91（pid 618284，cmdline 核对 'Xvfb :91 -screen 0 1280x1024x24'；本次为已存在实例、复用）按 PID kill -TERM 成功退出，/tmp/.X91-lock 清除、无 :91 残留。4 个 demo 实例在收尾前已被外部清掉，无残留进程。

### 3.5 目验失败与未修项（如实收录）
1. **autotest 断言**（本轮 /tmp/lazy2/at.log：exit=0 不再段错误，FAIL 共 16 行）：
   - **XI2 触摸两锁 + 内建总口径 3 行**：qtalign 基线既有 FAIL（10-18 采集），**非本轮新增回归**；本轮后仍 FAIL、无明细归因。
   - **从站页18 断言**：以时间窗事实替换归因——17:37 门禁（当前 xgui_window_demo.c）时从站页 PASS；**17:59:52 `xgui_demo_page_modbus.c` 被修改、17:59:54 重建后，18:03 门禁从站页 FAIL**（静默 ++failures，现状 `xgui_demo_page_modbus.c:5007` 起，计数块 :5011-5024 无打印）。修改内容与作者未记录（工作区存在并行会话），**归因待查**。
   - 键盘页 5 项（autoPopup 守护×2、英文框自动切布局、默认框回落、回归锁）与软件更新页 2 项（进度条复位×2）：上轮已报未修（18:03 门禁仍见页9/页16 FAIL，/tmp/wf_lazy_final.log:20/:26）。
   - 环境项不变：remote-server listen FAILED port=0（autotest 演练）、qr_logo placeholder 缺失。
2. **最终门禁完整性守卫未过**：EXT_PAGE_PASS_COUNT=11 < 12（18:03，/tmp/wf_lazy_final.log:14-15）。
3. **RS2 静默死亡**：--remote-server 实例 RS2（--remote-port 46012，pid 622416）运行中进程消失、根窗口树无其窗口（xwininfo -root -children 仅剩第一实例 0x200002）、rs2.log 无任何异常/退出记录；同期 619376 与 RS3 623000 亦集体消失且日志无崩溃痕迹。同工作区存在并行测试会话（其历史命令含对同名进程 pgrep + kill -9 扫荡），高度疑似外部误杀而非被测代码缺陷，但无法从现有证据实锤，如实记录。
4. **验证手段变化（非失败项）**：mousePress 诊断日志行仅 --remote-server 实例随修复移除（rs3.log 计数=0），CLI 实例 ui.log 仍有 6 行；后续黑盒验证以视觉/行为变化为准，事件层定位能力部分下降。

### 3.6 RSS 数字一览（量化验证复核口径）
A 全量 = 51449kB ≈ 50.2MB；BASE = 40552kB ≈ 39.6MB；差 = 10897kB ≈ 10.64MB = 21.2%；SUM 单页 12840kB、折减系数 0.849；Top-6 = 7358kB ≈ 7.19MB raw / 6.10MB 折算；全懒额外多省 ≈4.54MB；噪声全距 124~820kB（±0.06~0.41MB）；A vs 单建从站页 50.2 → 41.0MB；还原后复跑 VmHWM 51716kB ≈ A 基线（/tmp/lazy_quant.log:1049-1153 实测）。

---

## 四、已知限制
1. **最终门禁未通过**（18:03 GATE_FAIL：从站页新失败 + EXT_PAGE_PASS_COUNT=11<12）；本报告交付时点门禁状态为 FAIL，需在 modbus.c 17:59 在途修改归因清楚后复跑定论。
2. **从站页回归归因待查**：17:59:52 `xgui_demo_page_modbus.c` 被修改（内容与作者未记录，工作区存在并行会话），17:59:54 重建后 18:03 门禁从站页 FAIL。
3. **从站页交互结论时效**：修复项1/冒烟中从站页交互证据采集于 17:31 二进制，重建后未复验，待最终二进制复验。
4. **两处在途 UAF 非本任务改动**（Modbus 从站 rebuild 计数 XModbusDataUnit.c、XRemoteSessionBar 退出析构）：文件不在本任务可改清单内、未触碰；A/B 证据树 `/home/xinyue/lazyasan_base` 留证。
5. ASan pg17/18 泄漏数值与基线**不可比**（基线因自身退出 UAF 在 LSan 前中止）。
6. 既有断言 FAIL 未修：键盘页 5 项、软件更新页 2 项、XI2 触摸两锁 + 内建总口径 3 行（基线项）；环境项 2 项（listen FAILED port=0 演练、qr_logo placeholder）。
7. 键盘页 +456kB 增量与运行间噪声同量级，单列数值存疑；尾部轻页排序仅作参考。
8. 未量化项：各页 build 耗时（ms 级未测）；连接态增量（远程视频缓冲、Modbus 轮询数据）连接后才分配、不在常驻 RSS 口径内。
9. 「离开销毁」激进档未实现；如需，须先解决 §2.3 五项销毁对称风险。
10. 启动段既有 XError「XClass_deinit_base 参数 XClassGetVtable(object)是NULL」在所有配置（含改动前 A 基线）存在，非本轮引入，门 grep 口径未覆盖，留待另行排查。
11. **行号双口径**：本文 `xgui_window_demo.c` 现状行号已按当前工作区实测复核；各页文件（modbus/remote/keyboard）为实验时点快照行号（14:14 pristine），17:59 后 modbus.c 行号已漂移（从站断言 :4949→:5007 起）。
12. RS2 及多实例静默消失疑为并行会话外部 kill -9 扫荡，未实锤，仅记录现象与依据。
13. mousePress 诊断日志仅 --remote-server 实例移除，CLI 实例仍在（6 行）；黑盒验证定位手段部分弱化。

---

**证据索引**：量化 `/tmp/lazy_quant.log`（更正块 :1141-1153）+ `/tmp/lazy_quant/measure.sh` + `/tmp/lazy_quant/stats.txt`；门禁 `.zcode/wf-gates/wf_lazy_gate.sh` + `/tmp/wf_lazy_final.log`；目验 `/tmp/lazy2/`（at.log、ui.log、rs2.log、rs3.log、p*.png、t10a/b_c.png、r3_*_c.png）；ASan `/home/xinyue/lazyasan_base`；还原基准 `/tmp/lazy_quant/xgui_window_demo.c.pristine`（md5 8f2af66b…）。
