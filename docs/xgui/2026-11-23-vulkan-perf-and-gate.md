# Vulkan 渲染后端：全 GPU 收口与性能超越软件光栅（2026-11-23）

> 承接远端 `4a12f049`（桌面默认 VK→GL→软件）。目标：**VK 全 GPU 渲染**（稳态零 CPU 光栅化）
> 且**整帧性能超过软件光栅**，并把这些目标钉进门禁。

## 1. 结论

| 构建 | 软件 | GL | **VK** | VK / 软件 |
|---|---|---|---|---|
| Debug（p1 门禁，1000x700 图表页整帧） | 196.5 FPS | 333.9 | **469.1** | **2.39×** |
| Release（`bin-release`，同参数） | 539.2 | 741.7 | **1247.6** | **2.31×** |
| RelWithDebInfo（`bin-relwithdebinfo`，可采样/基准） | — | — | **~1240~1260** | — |

> 以上为 2026-11-23 收尾5（实心 quad 合批缺省翻正 + 绘制侧三修）后的实测；
> 翻正前对应为 Debug VK 385~452、Release 1115~1170、RWDI ~990。

> Release 无 `/Zi` 无法采样归因；`RelWithDebInfo`（`--rwdi`）专为性能取证，
> 速度接近 Release 且带 PDB。

- p1 门禁：Debug **44 通过 / 0 失败**、Release **41 通过 / 0 失败**；
  逐像素 **15 场景** 14 个 0 硬差，draw-text 82px（与 GL 同值的
  字形 AA 量化差，阈值 82）。
- 全页 CPU 光栅预算：page 0~16 稳态 `ab=0 / route=0`（无 CPU 覆盖率光栅、无软件批画布重入）。
- 零回读：present>0 / readback≈0。

## 2. 修掉的缺陷（按发现顺序）

| # | 现象 | 根因 | 修法 |
|---|---|---|---|
| 1 | VK 逐像素 `--dump` 段错误（门禁「比对无输出」） | `COMBINED_IMAGE_SAMPLER` 写描述符 `sampler=0`（VUID-01082），Intel 驱动内空指针 | 统一走 `xvkl_update_texture_descriptor[_sampler]` helper |
| 2 | clip-path-AA 场景 VK 整块缺失（2037px） | 路径填充被**过期门控**导向软件重放（M4a 掩码烘焙已能直绘） | 仅「多矩形区域+路径裁剪」保留兜底 |
| 3 | 屏幕实际只有 ~2.6 Hz 刷新 | keep-open 下**没有任何一条腿真正 endFrame/present**，仅顶点压力 50% 被动收口 | `xvkl_present_to_window` 收口一 UI 帧；`XPainter_closeWindowFrame` 同步清画家记账 |
| 4 | K=2 收益为零 + 截图偶发整幅黑 | `beginFrame` 与 `endFrame` **各推进一次**帧槽 → 0→1→0 回环 | 槽位只在 endFrame 推进；K 缺省 2 |
| 5 | 文本页 CPU 被 FreeType 占满 | GPU 轮廓字形每帧重走「字库解码→路径展平→包围盒」 | 复用 SW 字形灰度图缓存（命中直接拿度量+矩形）；`painterOutlineGlyphBuildable` 命中即返回 |
| 6 | 恢复源缺失时清屏黑 | `lastPresentedIndex == imageIndex`（K>1+IMMEDIATE 常见） | 回退持久快照 `m_colorImage` |

## 3. 性能工程（按收益排序）

1. **字形可构建性判定零字库访问**（最大头，VK 206→441 Debug）：原实现每字形每帧
   `painterOutlineBuildPath + deinit`；现灰度图缓存有条目即判可构建，失败存 1x1 零覆盖负缓存。
2. **一 UI 帧一驱动帧**：presentToWindow 内 endFrame（提交+present+快照），K=2 让录制与上帧执行重叠。
3. **顶点发射去法**：NDC `2/w` 提到 quad 外、`/255` 改乘倒数；三个热函数去栈数组+循环全展开
   （Debug `/RTC1` 不再插 `_RTC_CheckStackVars`）。
4. **有效变换缓存**（文件级，键=函数读取的全部输入）。
5. **字形图集上传入帧内命令流**（去掉每字形一次独立 transfer 提交）。
6. **非纹理实心 quad 合批**：实现并在 `XGPU_VK_SOLID_QUAD_BATCH` 开关后。**缺省已翻正为开**
   （2026-11-23 收尾5）：同二进制 A/B 中位 **+13~17%**（修复区域上传后复测
   1245 vs 1060 = **+17.5%**；早期 1050→1231 / 1159→1310）；
   采样实证驱动 `igvk64.dll` 129→45 ms/s。旧「无收益」结论是 Debug 构建 + 「先冲批再
   追加」顺序缺陷修复前的口径，已作废。**axis 合批仍缺省关**（单独开 1033~1038、
   与实心合批同开无额外收益）。回退口：`XGPU_VK_SOLID_QUAD_BATCH=0`。
7. **具名色查找二分 + painterRound 去 CRT**（绘制侧 ~2%）：`xsss_lookupNamedColor`
   的 149 项表已排序（首次调用校验、不满足回退线性），线性 strcmp → 二分；`painterRound`
   的有限值快路不再进 `isfinite/floorf`。采样：ucrtbase 7.5%→4.1%。
8. **热路径环境闸合并（+5.2% 中位 CPU，2026-11-23）**：所有诊断/契约
   开关（`XGUI_GPU_SYNC`/`XGPU_PROF`/`XGPU_PAINTER_PROF`/`XGPU_QUAD_PROF`/
   三个合批开关）原为「每原语一次函数调用 + 函数内 static」，本帧
   ~1800 quad ⇒ ~9M 次/秒。改为**文件级 int + `static __inline` 快路 +
   out-of-line 慢路**（首读/atexit/诊断 live 语义全保留）。主线程 CPU
   0.7845→**0.726ms/帧**（中位 **−7.5%**，min 0.764→0.706 = −7.6%）。
   第二批同族闸：逐线段/逐 quad 的 `xpainter_quad_prof_on`、逐斜线的
   `painterGpuFastPathExtRequested`、`painterGpuBatchFlush` 的无待定批
   快路内联化（0.744→0.726）。门禁：Debug 44/0（VK 474.7 / SW 198.9
   =2.39×）、Release 41/0（VK 1275.8 / SW 537.2 =2.38×）。
9. **表面裁剪包含快路（−3.8%，交替 A/B 6/6 全胜）**：窗口帧
   `begin_image` 把表面裁剪继承为状态裁剪（同一矩形）⇒ 交集即自身，
   命中 4 个 int64 包含比较时跳过 `XRect_intersected`（`rect_edges`×2 +
   `rect_from_edges`，采样合计 ~2.8% 总 CPU）；不命中仍走通用实现。
   主线程 CPU 0.747→**0.7185ms/帧**。**方法学**：本机不同时段漂 3~5%，
   1%~5% 级结论必须两二进制逐轮交替 A/B（顺序测首轮曾把 −3.8% 显示成
   +4.8%）；A/B 二进制都要留在 `bin-relwithdebinfo/`（挪 `/tmp` 丢字库 ⇒
   5.7ms/帧假慢）。
10. **GL 后端两处（2026-11-23，−5.0% CPU / fps 786→830）**：
   ① 热路径开关文件级缓存（同 VK 口径：GL 每个 solid quad 连查
   quad_batch/full_batch/degen_legacy/batch_prof/premul_repair 等）⇒ −3.3%；
   ② 顶点缓冲帧内子分配（帧首孤儿化 4MB + 冲批 `glBufferSubData` 互不重叠
   区间 + `firstVertex`，替掉「每次冲批一次 glBufferData」的 101 次/帧
   ×2.4µs）⇒ 再 −3.1%；合计 −5.0% CPU（交替 A/B：1.447→1.3555ms/帧），
   三后端 Release 变为 **SW 542 / GL 830 / VK 1279**。回退
   `XGPU_GL_VBO_SUBDATA=0`。**注意与旧负结果区分**：旧「勿用 subdata 原位
   更新」指同区间写触发隐式同步；本方案是孤儿化+区间隔离。
11. **GPU AA 线段缺省翻正（2026-11-24）**：真实 HSF 客户端页面 AA 线
   多（`ab` 4k~12k/5s），旧缺省走 CPU 覆盖率 + 上传：VK 每线 ~0.4ms
   （窗口内联上传逐线结束/重开渲染通道）⇒ 数字量页整页重绘 VK 135ms /
   GL 12.9ms / SW 0.9ms。缺省改为 `XGPU_AA_LINE=1`（GPU AA，`=0` 回退）：
   真实页面 GL p3 12.9→2.1ms、VK p3 135.4→2.0ms；demo 整帧 Release
   GL 776→1058.7 fps、VK 1284；p1 门禁 Debug 44/0、Release 41/0，
   parity 82px，AA 感知与「关闭基线逐位一致」均 PASS。
12. **变换单次扫描 + 透传、同步闸前置（中性，仅作减法）**：`painterMatrixAxisShift`
   单次扫描替代两函数两遍；`drawLineDevice` 复用调用方已算变换；`xgpu_sync_readback_if_requested`
   静态开关前置。帧率变化 <1%（噪声内），不记为收益。

## 4. 负结果（不要重复踩）

- **画师侧裁剪下发去重**：backend 记 scissor 代次 + XPainter 文件级 memo。Debug clip 调用
  8.16M→3.01M 次/4.1s（-63%），但**总耗时 324.9→322.9ms 不变**；Release 反而 1245→1090 变慢。
  根因：驱动 `xvkl_set_clip_rect` 本身已有同值缓存，画师侧再去重是重复劳动。
- **（已根修）VK 区域上传零拷贝**：`XGuiRegression_Test` 在 VK 下 4 条像素断言红
  （`theme engine high-DPI paint scales` ×2、G3 `border-image` 九宫/边条 ×2；
  软件 0 红、GL 仅 3 条 `xrot` 基线红）。根因是 HEAD `4a12f049` 给
  `xvkl_copy_rows_for_upload` 加的防御守卫（stride 不足即 return）与
  `xvkl_draw_image_region` 的旧调用约定（单行拷贝传 stride=0）冲突 ⇒
  **整段零字节拷贝**，区域上传上去的是空画布（实证：画布侧 nz=1728、
  暂存侧 nzA=0；`XGPU_VK_REGION_DIRECT=0` 退回整幅 drawImage 后 0 红）。
  修法：调用点传真实行距。回归锁：parity 新场景 `draw-image-rect-scaled`
  （缩放变换强制走软件暂存画布腿；SW vs VK 0 硬差）。修后该套件 VK **0 红**。
  一并修掉 XHOV 的 acquire 永久阻塞：`XGPU_VK_ACQUIRE_TIMEOUT_MS` 缺省
  0（永久）→ **1000ms**（超时按 beginFrame 失败降级；env 显式 "0" 回旧行为）。
  **"挂住"会掩盖"红"**——该套件此前根本没跑到报这 4 条红的地方。
- **painterRound 快路强制内联（已回退，2026-11-23）**：`static __inline`
  快路 + 慢路 out-of-line，实测主线程 CPU 0.780→0.789ms/帧（噪声内）——
  /O2 已内联热调用点，符号占来自冷路径。按纪律回退。
- **合批改 4 顶点 + 共享索引表（已回退，2026-11-23）**：顶点写入/顶点
  着色 −33%（索引 `[0,1,2,2,1,3]` + `baseVertex`，三角形逐位一致），
  实测帧率与改前**完全相同**（RWDI 中位 1245 vs 1245；合批开关对照的
  +17% 仍在）⇒ 顶点写入量不是瓶颈，按纪律回退并记录。
- **（订正）实心 quad 合批「无收益」**：2026-11-23 收尾5 同二进制 A/B 证伪旧结论
  （+13~17%，见 §3-6）。当时给出「Intel 无收益」的测量同时具备两个失效条件
  （Debug 构建 + 追加/冲批顺序缺陷），故 `AXIS_QUAD_BATCH` 与 `SOLID_QUAD_BATCH`
  不能共用同一条负结论——前者仍无收益，后者已翻正。
- **1px 轴对齐线转 fillRect**（想搭默认开的实心合批）：多矩形区域裁剪下像素不等价
  （parity `clip-region-quads` 73px），加区域守卫后 parity 恢复 82px，但**帧率反而下降**
  （RWDI 990→900、Release 1170→1100）——fillRect 的分支与冲批开销大于逐线 direct quad。
  已回退；轴对齐线保持 direct quad（与上游 AMD 的 AXIS_QUAD_BATCH 缺省关结论一致）。
- Debug 剩余热点（`_RTC_CheckStackVars` 8.1%、`painterRaster_drawLineDevice` 6.7%、
  变换缓存比较链 5.7%、clip ~5%）在 Release 基本消失，不应为它们做高风险重构。

## 5. 门禁与工具

- `Tools/windows/gpu_stage_verify.sh`：
  - p1 新增断言 **「整帧 VK ≥ 软件×1.05」**（架构目标，不是抗负载护栏）。
  - **`--release`**：用 `out/build/x64-Release`、产物 `bin-release/` 跑同一套正确性门禁；
    绝对 fps 仅记录（Debug 基线不适用），架构目标照常强制。
- `Tools/windows/gpu_build_targets.bat`：
  - **vcvars 自动探测**（`VCVARS` env → `vswhere.exe` → Program Files 常见目录 → PATH），
    不再写死 VS18 路径；
  - **`--release` / `--rwdi`** 切换构建目录（参数任意位置均可，先收集参数再
    shift；**`shift` 改写 `%0`，脚本目录必须在 shift 前存 `SCRIPT_DIR`**）。
  - 缺失构建缓存时自动 `cmake -G Ninja` configure（Ninja 取自 `%VSINSTALLDIR%`）；
    **.bat 必须 CRLF**（LF 下 cmd 会把复杂 bat 的行切错，`.gitattributes`
    已加 `*.bat text eol=crlf`）。
- `xgui_gpu_parity_test` 新增 `--scene N[,M...]`：只渲染指定场景，便于单场景像素迭代。
- **模块级 + 调用方归因采样**（临时工具，已从 demo 移除）：psapi 模块表 + 1ms
  SuspendThread 采样（叶模块/RVA），外加 [rsp..48 字] 扫 exe 返回地址做 caller
  归因。**两条使用纪律**：① 叶占比要配绝对值（ms/s）看——总 CPU 下降时占比会
  「搬家」；② 栈扫 caller 可能命中陈旧栈字，只采信同时出现在叶热点的条目。
  **门禁必须独占跑**（并发会抢 6100/6200/8081 与 GPU，出现假红/假绿）。
- **低噪性能口径**：`XGUI_BENCH_CPU=1`（demo `--benchmark` 额外打印
  主线程 CPU ms/帧，`GetThreadTimes`）——墙钟 fps 本机 ±10% 波动，
  该口径 ±3%，用于判定 1%~2% 级改动；进程级口径会把驱动内部线程算进来
  （0.87 vs 主线程 0.78ms/帧），勿混用。
- **区域上传诊断**：`XGPU_VK_REGION_TRACE=1` 打印前 80 条区域上传的
  源矩形/内联腿/暂存几何 + 暂存非透明像素计数（`nzA`/`nzRGB`）——本轮
  就是靠「画布 nz=1728 vs 暂存 nzA=0」一击定位零拷贝。
- 静态层探针确定性（demo 夹具）：捕获统一跳过页面首行边界；取帧前整窗重绘 + 排空事件；
  探针起始冻结 1s 定时器（LCD/时钟/悬浮层）。修后 VK 10/10 PASS、跨后端层 dump 0 硬差。
- 诊断开关：`XGUI_VK_FRAME_TRACE=1`（begin 恢复/清屏决策与读回分支）、
  `XGPU_PAINTER_PROF` / `XGPU_QUAD_PROF` / `XGPU_VK_STAGE_PROF` / `XGPU_VK_BEGIN_PROF`。

## 6. 复现

```
:: Debug 门禁（含构建）
Tools\windows\gpu_stage_verify.sh p1
:: Release 门禁
Tools\windows\gpu_stage_verify.sh p1 --release
:: 单独基准（1000x700 图表页，每 UI 帧一次 present）
bin\XGuiWindowDemo_Test.exe --benchmark 5 --benchmark-full --page 4 --tab 20
bin-release\XGuiWindowDemo_Test.exe --benchmark 5 --benchmark-full --page 4 --tab 20
bin-relwithdebinfo\XGuiWindowDemo_Test.exe --benchmark 5 --benchmark-full --page 4 --tab 20
:: 性能取证（有 PDB，可用采样器/cdb 归因）
Tools\windows\gpu_build_targets.bat --rwdi
:: 合批回退（AMD 旧结论复现用）
set XGPU_VK_SOLID_QUAD_BATCH=0
```

> 注：`--benchmark` 的 FPS 是「CPU 录制 + present」口径；整帧一轮 = 一次全窗重绘。
> 环境变量 `XGUI_RENDER_BACKEND=software|gpu|vulkan` 选后端；未设置时 Windows 缺省 VK→GL→软件。
