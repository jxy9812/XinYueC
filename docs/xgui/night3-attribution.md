# night-3 归因：GPU 增量口径 ~1.36ms/帧 分解（第二波）

日期：2026-09-26 01:00–01:40（夜，纯测量 + 读码，零文件改动）
被测程序：`bin/XGuiWindowDemo_Test.exe`（2026-09-26 00:40 构建，即第一波
b1416e2a 落地后的同款），复制 `%TEMP%\xgui_bench3.exe` 运行，跑前
taskkill。命令均为 `--gpu --benchmark 5 --page N`（增量口径，800x600，
5s），环境开关见各表。原始日志：`%TEMP%\n3attr\*.log`（12+7 份，TEMP
探针用后不清理即失效，本表数值为冻结记录）。
环境：AMD Radeon (TM) Graphics / OpenGL 4.6 Compatibility Profile
（[gl-info] 实查一致），RDP/虚拟显示栈在位，与 night3-final 同机同会话。

## 1. 基准现状（XGPU_PROF=1，各 2 遍）

| 页面 | fps（两遍） | avg ms/迭代 | prof frames（beginFrame 次数） | 派发数/迭代¹ | readback 次(均值) | fillRect/s | solidQuad/s |
|---|---|---|---|---|---|---|---|
| page 0 | 732.0 / 729.8 | 1.366 / 1.370 | 7993 / 8017 | **2.18** | 292 (0.977ms) / 305 (0.899ms) | 7879 | 2256 |
| page 4 | 502.3 / 509.6 | 1.991 / 1.962 | 8207 / 8289 | **3.26** | 299 (0.944ms) / 297 (0.849ms) | 5646 | 1786 |

¹ 派发数/迭代 = prof frames ÷ 基准 frames：prof 的 `frames` 计数
beginFrame（XGpuRenderBackend.c:72,100 → xgpu_prof_frame_tick），窗口
直通下每开一次帧 = 一次 `XWidget_flushBackingStore` 派发（读码证据见 §4，
旁证：额外开帧没有伴随离屏读回——readback 恒 ≈60/s，若为离屏会话每开帧
末必 readback，应见 ~1600 次/s）。

关键反直觉事实：**每迭代 2.18–3.26 次派发，而原语数几乎与页无关**
（p0 ≈14 原语/迭代，p4 ≈14.7）——多出来的派发近乎空绘，基准循环本身每
迭代只提交 1 次 `demo_repaint`（xgui_window_demo.c:916）。

## 2. 分解实验（各 1 遍，XGPU_PROF=1）

| 实验 | 开关 | 页 | fps | avg ms | readback 次(均值) | drawImage 次(均值) | 判读 |
|---|---|---|---|---|---|---|---|
| 基准 | （默认=A 路脏区读回+60Hz 限频） | 0 | 730.9 | 1.368 | 298 (0.94ms) | 0 | 锚点 |
| 读回退全帧 | XGPU_DIRTY_READBACK=1 | 0 | 637.6 | 1.568 | 312 (2.612ms) | 0 | 全帧读回 2.6ms/次 vs bbox 0.94ms/次；摊销差 +0.18ms/迭代 ≈ fps 差 +0.20ms ✓ |
| 同上 | 同上 | 4 | 426.7 | 2.343 | 307 (2.755ms) | 0 | 同向（506→427） |
| 呈现不限频 | XGPU_PRESENT_MAX_FPS=0 | 0 | 542.1 | 1.845 | 2736 (0.380ms) | 0 | 每帧呈现反掉 fps（−26%）：readback 0.38ms/次 + 每帧 BitBlt ≈ +0.48ms/迭代；限频是止损不是瓶颈 |
| 同上 | 同上 | 4 | 398.8 | 2.507 | 2058 (0.407ms) | 0 | 同向 |
| 冲批全幅上传 | XGPU_REGION_DISABLE=1 | 0 | 193.7 | 5.164 | 133 (1.921ms) | 4524 (0.294ms) | 区域上传→全幅 drawImage：每迭代 4.6 次 × 0.294ms ≈ +1.36ms，再加带宽争用 longest 124ms，fps −73% |
| 同上 | 同上 | 4 | 103.5 | 9.658 | 123 (2.679ms) | 4494 (0.302ms) | 同向更重 |
| 组合全关 | 三开关同置退化态 | 0 | 44.0 | 22.733 | 242 (3.458ms) | 4666 (0.320ms) | 退化乘法叠加 |
| 同上 | 同上 | 4 | 44.9 | 22.274 | 270 (3.597ms) | 4301 (0.305ms) | 同上 |

补充探针（矩阵外，归因用）：

- **XGPU_PRESENT=swap**（换链通道）：p0 fps=78.1（present=344 次
  0.346ms/次）、p4 73.8（0.331ms/次）——SwapBuffers 在本远程栈被钳在
  ~70Hz 且 fillRect 计数暴涨 31×（133032/5s ≈ 340/迭代 = 整窗软件重绘
  特征，frameDegraded 路径），证实换链通道在本栈不可用（与
  XWidget.c:5906-5911 设计注释一致）。**不能当 paint-only 口径用**。
- **XGPU_PROFILE=1**（presentToWindow 内 quad/swap 分段，
  XGpuRenderDriver_gl.c:1644-1724）：默认读回+BitBlt 通道下 present=0，
  **永不打印**——该探针只在 swap 通道有输出，本夜无有效采样。
- **XGPU_QUAD_BATCH=0 / XGPU_SCISSOR_CACHE=0**（逐 quad 即时 / 关剪裁
  缓存）：p4 fps=504.1 / 500.9，对基准 506.0 **零变化** → 逐原语冲批、
  逐 scissor 变更成本 ≈ 0。⚠️ 两开关在 page 0 均异常退出（日志止于
  driver 行，无基准行，各 173 字节）——疑似即时绘制/关缓存路径在 p0
  页崩溃，移交修复道核实。
- **page 6 确认点**：fps=431.2，avg=2.319ms，prof frames=7806 → 派发
  3.62/迭代。

## 3. ms 级分解表（page 0 默认态，每基准迭代 1.368ms）

| 分桶 | ms/迭代 | 占比 | 依据 |
|---|---|---|---|
| **每派发固定成本 × 2.18 次派发** | **≈1.24–1.29** | **≈91–94%** | 线性模型：per-dispatch 0.57–0.61ms（p0 1.368/2.18=0.63；p4 1.976/3.26=0.61；p6 2.319−0.108 读回=2.21/3.62=0.61），三页拟合残差 <3% |
| 呈现链-读回（readbackRect 210x50 bbox，60Hz 摊销） | 0.078 | 5.7% | 292 次×0.977ms ÷ 3661 迭代；全帧读回态为 0.255（d_dr1），差值与 fps 差吻合 |
| 呈现链-BitBlt flush（60Hz 摊销） | ≈0.02–0.05 | 1–4% | 无后端计数器，未直接测；上界取 d_fps0 差分 0.48ms 中扣掉读回后的 BitBlt 份额 ÷12 |
| 区域上传（drawImageRegion TexSubImage 42KB+quad） | ≈0（含在固定项内，增量≈0） | — | drawImage=0、QUAD_BATCH/SCISSOR 零变化；但为生命线：REGION_DISABLE 态 +1.36ms/迭代（−73% fps） |
| quad 提交/冲批/scissor | ≈0 | — | f_qbatch0/f_sc0 p4 实测零变化 |

即：**GPU 增量口径的钱不在读回（已修）、不在上传（已区域化）、不在
原语提交（批合并已够），而在"每一次派发一次"的会话级固定成本
~0.59ms，且基准每迭代付 2.2–3.6 次**。

已知停顿点清单（读码）：

1. `glReadPixels`（xgld_readback :1744 全帧 / readbackRect :1836 bbox）
   —— 唯一硬同步点。bbox 版实测 0.85–1.02ms/次（60Hz 时管线积压 ~12
   迭代 → 深排空；不限频时 0.38ms/次）。已摊销到 6%，非主要矛盾。
2. `xgld_end_frame` 的 `glBindFramebuffer(0)`（:1631）+ 下一次
   begin 重绑 —— 每派发一对解绑/重绑 + makeCurrent 验证
   （xgld_begin_frame :1580-1587，含 Viewport/PixelStorei/scissor/blend
   复位）。GL 调用数少（探针证逐调用成本≈0），但**驱动级会话开关
   成本（FBO 完整性重验证/命令提交粒度）无法用现有探针排除**，是
   0.59ms/派发的头号嫌疑（假设，非实证，见 §5 探针建议）。
3. SwapBuffers（:1674/:1705）—— 仅 swap 通道，本栈 ~0.35ms/次且触发
   整帧软件降级，默认通道不经过。
4. 批冲刷 `xgld_flush_quads`（:819-837）：glBufferData 孤儿化 +
   一次 glDrawArrays；冲批点=纹理/混合/scissor 切换+帧界（:815-818）。

## 4. 每派发 GL 调用序列（默认通道，读码清单）

```
XWidget_flushBackingStore (XWidget.c:5716)
 ├ acquireForWindow（会话复用，无 GL）
 ├ XPainter_begin_image → beginFrameImage (XPainter.c:6999)
 │   └ xgld_begin_frame (gl.c:1580): makeCurrent, BindFramebuffer(FBO),
 │       Viewport, PixelStorei(PACK,1), Disable(SCISSOR), Blend
 │       （窗口直通不清屏不重传，FBO 持久 :1588-1612）
 ├ paintTree → 原语：fillRect/solidQuad 入 quad 批；无快速路径命令软
 │   栅到全帧暂存画布 (XPainter.c:121-224)，批末 drawImageRegion
 │   直传 BGRA TexSubImage2D (gl.c:2155-2162) + SourceOver quad
 ├ painterGpuEndFrame (XPainter.c:342) → 批冲刷 + xgld_end_frame
 │   (gl.c:1626): flush_quads{BufferData+DrawArrays}, BindFramebuffer(0)
 ├ 呈现块 (XWidget.c:5967-6044)：60Hz 限频（clock 摊销，跳帧回填脏区
 │   :5941-5965）；到帧 → readbackRect(wholeBbox)（glReadPixels 硬同步
 │   :1836）+ XBackingStore_flush 逐矩形 BitBlt (:6034)
 └ endWindowFrame（仅清指针，XGpuRenderBackend.c:420-424，无 GL）
```

## 5. 下一杠杆排序（按预期收益 × 置信度）

1. **【最大杠杆】压派发数与每派发固定成本（91–94% 池子）**
   - 5a 先定位多余派发源：基准循环每迭代只提交 1 次 demo_repaint，
     实测 2.18–3.62 次派发/迭代（p4/p6 更重），多出的派发近乎零原语。
     身份未定位（悬浮层文本仅 4Hz，不足以解释）。下一波在
     flushBackingStore 加派发计数+region 日志探针（或沿用 XGPU_P4LOG
     通道）即可拿到答案；若多余派发可合并/吞并，p0 立省 ~0.6ms/迭代
     （732→~1100 fps，+50%）。
   - 5b 再削每派发 0.59ms：头号假设是 end_frame 的 FBO 解绑/重绑 +
     makeCurrent 验证这类会话开关驱动成本。可做一个环境开关
     （end_frame 推迟解绑 FBO，readback/present 前再绑）实证；若
     0.59→0.3ms，增量口径再 +60–80%。两步都兑现 → p0 ~0.37ms/迭代
     ≈ 2700 fps，仍不及 SW 10924，但差距从 15× 收敛到 ~4×。
2. **呈现链残量（6–10%，低收益不动）**：脏区读回已是正解，60Hz 限频
   是止损（解除即掉 26%）。仅当 5a/5b 落地后读回占比被动放大时再回头
   （例如换 PBO 异步读回）。
3. **上传路径（零收益，保命项）**：区域上传不可回退——REGION_DISABLE
   实测 fps −73%；它已经只传 210x50，无可再省。
4. **勿做**：PRESENT_MAX_FPS=0 生产化（实测负收益）；swap 通道在本
   远程栈（实测降级+~70Hz 钳制）。

## 6. 诚实性声明

- 本表全部数值出自本次实跑（命令与开关见 §1–§2；原始日志在
  %TEMP%\n3attr，进程退出即失效，故正文为冻结记录）。
- 每派发固定成本 0.57–0.61ms 是**边际成本实证 + 内部构成假设**：三页
  线性拟合残差 <3% 支撑边际值；其内部 GL 驱动级成因（FBO 重绑 vs
  makeCurrent vs 提交粒度 vs XWidget 派发 CPU）未逐项实测——现有
  零构建探针已排除逐原语/逐 scissor/读回/上传四项主导可能，剩余嫌疑
  需 §5-5b 的开关或驱动级 trace 才能定谳。
- XGPU_QUAD_BATCH=0 与 XGPU_SCISSOR_CACHE=0 在 page 0 异常退出（疑似
  崩溃），对应结论仅基于 page 4 有效样本，如实标注。
- XGPU_PROFILE 分段探针在默认呈现通道无输出（present=0 不打印），本夜
  未取得 quad/swap 分段数据；建议后续把该探针扩展到 readbackRect 通道。
- BitBlt flush 无后端计数器，其 60Hz 摊销份额（0.02–0.05ms）为
  d_fps0 差分推出的区间，非直接测量。
