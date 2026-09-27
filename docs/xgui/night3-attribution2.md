# night-3 归因2：GPU 增量 ~1.27–1.35ms/帧 分解与「逐原语提交成本」假设检验（第四波）

日期：2026-09-26 04:05–05:00（夜，测量 + 读码 + prof 扩列，未构建）
被测程序：`bin/XGuiWindowDemo_Test.exe`（2026-09-26 03:38 构建，K 路
keep-open + L 路四开关均在二进制内——六个开关名字节串逐一实查 FOUND），
按 M 路判崩协议复制改名副本 `%TEMP%\n3a2_<态>.exe` 运行（每态独立名），
CWD=源 bin，子进程环境先移除全部 XGPU_*/XGUI_* 键再注入本态键（净化
同 `Tools/night3_bench.ps1` Run-Bench 口径）。命令
`--gpu --benchmark 5 --page 0`（增量口径，800x600，5s）。
**9 遍全 exit=0**（无 0xCxxxxxxx、无 173 字节日志），数值可采。
TEMP 探针（exe 副本 + 脚本 + 日志）用后即删，数值冻结于本文。

同会话环境漂移警示：本会话整体比 03:38 锚点慢（SW 11005→9367 fps，
−15%；GPU 800→741，−7%）——**横向比较一律只用本会话同窗数据**，
与上波数值纵比须带此漂移系数。

## 1. prof 扩列（本波代码落地，待下一构建出数）

`Src/XGui/Graphics/XGpuRenderBackend.c` prof 段四处改动（仅此文件）：

1. 计数器结构新增 `m_fillRectUs / m_solidQuadUs / m_glyphQuadUs /
   m_glyphQuadCount`（:74-81）。
2. 三处包装层差分埋点（与既有 drawImage 同款模式）：
   fillRect → `m_driver->fillRect`（:882-887）、drawGlyphAlpha 图集主路径
   → `m_driver->glyphAtlasDraw`（:1109-1115，回退 drawAlphaBitmap 与
   图集上传不计入，防口径混叠）、drawSolidQuad →
   `m_driver->drawSolidQuad`（:1129-1134）。
3. 5s 窗口聚合行扩列：`fillRect=%u (%.4fms/次) solidQuad=%u
   (%.4fms/次) glyphQuad=%u (%.4fms/次)`，并随窗清零（:104-166）。
4. 实现说明：任务书写 "clock() 差分"，实现用文件内既有
   `xgpu_prof_now_us()`（XDateTime ns 时钟，与 readback/present 同源）——
   Windows `clock()` 粒度 ~1-15ms，无法分辨原语级 µs；同源时钟让新列与
   旧列同口径可比。语义边界（防误读）：包装层差分只含 backend 校验 +
   proc 间接 + 驱动入口；批内 `glUniform/glDrawArrays` 合并发生在帧末
   冲批（XGpuRenderDriver_gl.c:1018-1036），**不在这三列里**；图集键
   线性查找（XGpuRenderBackend.c:611-623）在包装层埋点之外。
   新列数值待下一构建产出（本波禁构建），本文不含其读数。

## 2. 五态实测（page 0 增量，5s/遍；gpu 态 2 遍）

| 态 | env（净化后注入） | fps | avg ms/迭代 | prof frames¹ | readback 次(ms/次) | fillRect/5s | solidQuad/5s |
|---|---|---|---|---|---|---|---|
| def-t1 | XGPU_PROF=1 | 738.1 | 1.355 | 64 | 310 (1.258) | 39923 | 11280 |
| def-t2 | 同上 | 744.7 | 1.343 | 48 | 306 (1.216) | 40583 | 11520 |
| k0-t1 | +XGPU_FRAME_KEEPOPEN=0 | 748.5 | 1.336 | 46 | 302 (1.275) | 40594 | 11520 |
| k0-t2 | 同上 | 674.9 | 1.482 | 50 | 295 (1.559) | 38032 | 11280 |
| loff-t1 | +FBO_PERSIST/STATE_CACHE/MAKECURRENT_ONCE/PRESENT_LEAN 全=0 | 663.4 | 1.507 | 54 | 298 (1.585) | 37976 | 11520 |
| loff-t2 | 同上 | 662.2 | 1.510 | 46 | 295 (1.625) | 38020 | 11520 |
| sw | --software | 9367.2 | 0.107 | — | — | — | — |
| qb0 | +XGPU_QUAD_BATCH=0（逐 quad 即时） | 751.9 | 1.330 | 52 | 308 (1.036) | 40367 | 11280 |

¹ prof frames = beginFrame 次数/5.1s 窗；基准迭代数 = fps×5（def-t2 为
3724）。drawImage=0、present=0 全态成立（swap 通道与暂存画布上传 p0 均
未走，与上波一致）。派发数/迭代已从上波 2.18 → **0.0129**（48/3724，
keep-open 主效应在 def 态内）。

## 3. 1.343ms/迭代 分解表（def-t2 口径；03:38 锚点 1.272 同构，桶占比相同）

| 分桶 | ms/迭代 | 占比 | 依据 |
|---|---|---|---|
| 派发框架（begin/end 会话对） | ≈0.008（上界 0.013×0.6） | <1% | 0.0129 次/迭代（prof frames 48 ÷ 3724）×上波单次派发 0.6ms 上界；keep-open 已把 2.18→0.013 |
| 原语提交（包装层+逐原语 GL） | **≤0.02（未检出）** | <1.5% | **qb0 探针**：13.9 原语/迭代（fillRect 10.9 + solidQuad 3.1，40583+11520 ÷ 3724）全改逐 quad 即时提交（use_program+bind+attrib+BufferData+uniform+DrawArrays，XGpuRenderDriver_gl.c:1341-1362，QUAD_BATCH 门 :1360），fps 751.9 vs def 均值 741.4（+1.4%）＝def 两遍自身波动量级。"每 draw call CPU 50-100µs" 若真应 +0.7-1.4ms/迭代、fps 腰斩——**实测零变化，假设对 solid-quad 群体证伪** |
| 读回摊销（readbackRect bbox，61Hz） | 0.100 | 7.4% | 306 次 × 1.216ms ÷ 3724 迭代；与上波 0.078 同量级（本会话读回单次 1.02-1.63ms 随态漂移） |
| 上屏 BitBlt（60Hz 摊销） | ≈0.02-0.05 | ~2% | 无后端计数器（present=0 证明走读回+BitBlt 通道非 swap），沿用上波差分界，未直接测 |
| **paintTree + painter GPU 编码 + 字形路径 + 每迭代框架 CPU（残差）** | **≈1.17-1.20** | **≈89%** | 1.343 − 上四行；现有全部后端计数器照不到：qb0 排除原语提交、0.013 派发排除会话对、读回已单列。SW 同页同口径 **0.107ms/迭代（含完整软栅）**证明遍历+框架+绘制本体 ≤0.107ms，GPU 路多出的 ~1.2ms 藏在 painter 侧 GPU 编码/字形路径/每迭代驱动会话成本中，**身份未定谳** |

glyph 数 p0 无计数器（旧 exe 无此列）：字形每迭代次数未知，是残差池
头号未知数。读码嫌疑（下一构建用新列即可裁决）：字形路径每字形付
**两次**图集线性查找——XPainter.c:12068 `glyphAtlasContains` +
XGpuRenderBackend.c:1036 `drawGlyphAlpha` 内再 find 一次，每次 O(图集
条目数) 线性扫（XGpuRenderBackend.c:611-623，上限 8192 条）；若 p0 每
迭代数百字形 × 千级条目，即 0.3-1ms/迭代量级，恰好填池。

## 4. 三对照判读

- **K 路（XGPU_FRAME_KEEPOPEN=0）**：t1 748.5（+0.5%）/ t2 674.9
  （−9.4%），两遍分裂、方向不稳，longest 35→65ms 抖动同现——判
  **单开关贡献在本会话噪声（±5-9%）内不可分辨**。prof frames 46-50/5s
  与 def 48-64 同级，说明 keep-open 主效应（派发 2.18→0.013/迭代）
  在 def 态已兑现，本开关的回退成本被环境噪声淹没。03:38 的
  1.366→1.272 幅度本轮未能复现（本会话 def 已漂回 1.34-1.36）。
- **L 路（四开关全关）**：663.4/662.2 两遍一致，def→loff
  **−11%（+0.165ms/迭代）——L 路已兑现 ~11%**。内部构成部分可归因：
  readback 均耗 1.216→1.585/1.625ms（STATE_CACHE=0 破坏 scissor 同值
  跳过 → 批碎片化+真实 glScissor，XGpuRenderDriver_gl.c:2411；
  MAKECURRENT_ONCE=0 恢复读回路径双 makeCurrent，:1837），摊销
  +0.036ms/迭代；其余 ~0.13ms 分布在每 begin/flush 会话固定成本。
- **SW 同页同口径**：9367.2 fps = 0.107ms/迭代；GPU:SW = 1:12.6
  （同会话；03:38 锚点 1:13.5，一致）。

## 5. 全批化预期收益推算

1. **上界来自 qb0 界**：把 fillRect/solidQuad 全部拆成逐 quad 即时
   （批化的数学反例）fps 零变化 → 「批化 vs 逐原语」在 p0 密度
   （13.9 原语/迭代，~10.4k 原语/s）下差值 ≤1.4%。全批化剩余工程
   （字形并入统一批、批容量、逐 flush 一次 uniform 等）可回收的
   **≤0.02ms/迭代（+1.5%），且原语数不 ×10 就放大不了**。
2. **推论：逐原语提交不是池子，全批化不是下一杠杆**——第四波作战
   地图的"每 draw call 50-100µs"因子被 qb0 探针证伪（至少对
   solid-quad 群体；glyph 群体在现 exe 中本就入统一批，
   XGpuRenderDriver_gl.c:1447-1468，无"逐字形提交"问题存在）。
3. 下一杠杆排序（按预期收益 × 可裁决性）：
   - ① **新 prof 列下一构建出数**：fillRectUs/solidQuadUs/glyphQuadUs
     + glyph 次数一次到位，包装层 vs 图集查找 vs 驱动入口三分。
   - ② **字形双线性查找单次化/哈希化**（XPainter.c:12068 +
     XGpuRenderBackend.c:1036 两次 O(n) 扫合一）：代码级实锤的双重
     扫描，收益随字形数线性放大；新列的 glyphQuad 次数出来后可先算
     上界再动手。注意本波禁碰 XPainter.c，落地需下一波授权。
   - ③ 若①②后残差仍 ~1ms：池子在 painter 侧逐迭代 CPU（命令编码/
     状态栈/widget 逻辑），需给 XPainter.c 埋同款开关化探针。

## 6. 诚实性声明

- **本波未构建**（禁跑构建，脚本统一）：新 prof 列无读数；全部实测
  数值出自 03:38 现有 exe 的 9 遍实跑（命令/开关/env 见 §1-§2，
  exit 全 0）。
- 环境漂移 −7%（GPU）/−15%（SW）vs 03:38 锚点：本会话横向可比，
  纵比须折算；1.272 锚点与本文 1.343 的桶结构按占比同构换算。
- k0 两遍分裂（748.5/674.9），K 路单开关贡献判"噪声内不可分辨"，
  非零也非负——如实存疑。
- BitBlt 份额无直接计数器，0.02-0.05ms 为上波差分界的沿用，非本波
  实测；present=0（swap 通道未用）为本波实查。
- glyph 每迭代次数在现 exe 不可得（无计数器），"双线性查找填池"是
  读码嫌疑（文件行号已列）而非实测结论，待新列 glyph 次数裁决。
- 执行偏差记录：任务书"clock() 差分"实现为文件内既有 ns 时钟
  `xgpu_prof_now_us()`（理由与同口径收益见 §1.4）。
