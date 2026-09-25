# GPU 侧场景保留设计（GPU-Retained Scene，damage-only 合成）

> 2026-09-25 设计稿（零代码）。目标：GPU 后端帧只提交脏区增量、静态内容零重提交，
> 全面超越软件渲染。对应 XGui.md §8.0g26 攻坚项①②（XGui.md:1358-1367）与
> 2026-09-24 勘察报告 P2-1 落地形态（2026-09-24-gpu-night-recon.md §八）。
> **行号基线警示**：两驱动文件在勘察期间持续漂移（recon 卷首与附录 A.5）；
> 本文 `Drive/` 行号为 2026-09-25 实读基线，动手前必须重新定位。`Src/`、
> `Test/` 行号文件稳定（XGuiWindowDemo.c / XWidget.c / XPainter.c /
> XChartView.c 当日实读）。

---

## ① 现状与目标

### 1.1 实测基线（2026-09-25，merged 二进制，800×600 图表页 --page 4 --tab 20）

命令：`copy bin\XGuiWindowDemo_Test.exe Tools\probe_demo.exe` 后运行
`Tools\probe_demo.exe --benchmark 5 --page 4 --tab 20`（可选 `--benchmark-full`、
`XGUI_RENDER_BACKEND=gpu`、`XGPU_PROF=1`），全部当日实跑：

| 口径 | FPS | avg/帧 | 说明 |
|---|---|---|---|
| SW 增量（默认，静态场景缓存+仅刷浮层小块） | **2241.0** | 0.446ms | `mode=repaint frames=11206` |
| SW 整帧（--benchmark-full） | 136.7 | 7.318ms | 全场景每帧重绘的真实成本 |
| GPU 增量（XGUI_RENDER_BACKEND=gpu） | **169.6** | 5.897ms | `frames=849` |
| GPU 整帧（--benchmark-full + gpu） | 166.7 | 5.999ms | **与增量几乎相同** |

原语计数（`XGPU_PROF=1`，5s 窗口）：`fillRect=172,767 solidQuad=163,239
present=719(0.131ms/次) readback=40(1.633ms/次)`——摊到基准帧约
**~454 原语/帧**（336k/5s ÷ ~740 帧），实测 6.767ms/帧 ÷ 454 ≈ **~15µs/原语**
（GL 函数指针→AMD ICD 固定开销，与 XGui.md:1348-1350 的 5-10µs 口径一致）。

### 1.2 根因链：GPU 模式为什么吃不到脏区红利

四层结构逐层实读：

1. **demo 级绕过**：CPU 静态场景缓存在 GPU 光栅下不生效——
   `Test/XGuiDemo/xgui_window_demo.c:762-766` GPU 分支直接
   `demo_drawStaticScene` 重画整场景（注释自认「GPU 直通：CPU memcpy
   不生效」）；SW 增量 2241 FPS 的核心（`demo_copyStaticTile`
   xgui_window_demo.c:677-712 + 脏区只刷浮层 :794-818）在 GPU 模式全丢。
2. **控件级（保留层）CPU-only**：`XWidget_setContentRetained` 的缓存是
   CPU `XImage`（`Src/XGui/Widget/XWidget.c:6190` ARGB32_Premultiplied），
   blit 走 `XPainter_drawImage`（XWidget.c:6249）。GPU 激活时该 blit 若进
   窗口 FBO 即触发驱动 drawImage 整幅纹理重传（recon 隐患 #4）；
   demo 当前未调用 setContentRetained（全文件检索零命中）。
3. **图表层静态层 CPU-only**：`XChartView` 静态层命中后
   `XPainter_drawImage(&painter, &cv->m_staticLayer, 0, 0)` 整幅贴回
   （`Src/XGui/Charts/XChartView.c:1700`）；层画布是 CPU XImage
   （XChartView.c:822-830）。GPU 下每帧整幅重传 1.83MB（800×600×4B）。
   序列/图例本就每帧现绘（XChartView.c:1719-1742，指纹**不含数据点**
   ：628-629）。
4. **驱动级无纹理身份缓存**：`Drive/windows/Graphics/XGpuRenderDriver_gl.c`
   只有 init 时创建的固定槽纹理（`m_sourceTexture` 等 4+ 张，
   XGpuRenderDriver_gl.c:1050-1053/1137-1138）；每次 drawImage 走
   CPU 逐像素重排 + `glTexImage2D` 整幅重传（:437-458，`m_pixels` 暂存
   重排 + :457 上传）。叠加：每原语全套管线状态 + 64B glBufferData
   孤儿化（recon #1/#2）、嵌套 painter 每帧 ~45 对 FBO 绑定+状态复位
   （`Src/XGui/Graphics/XPainter.c:259-261` 注释自认「框架级主开销」）。

净效果即实测的 **GPU 增量 ≈ GPU 整帧（169.6 vs 166.7，差 1.7%）**——脏区
裁剪只省了 fragment 光栅，省不掉原语提交与纹理上传；而 SW 增量/整帧 =
2241/136.7 ≈ **16.4×**。

### 1.3 目标

- **稳态帧**（无内容变化，如性能浮层刷新）：GPU 帧只提交「缓存纹理 quad ×
  脏区」增量，静态内容**零重渲染、零重上传**。帧原语数 ~454 → **个位数~两位数**。
- **失效帧**（子树内容/外观变化）：仅该子树重建进缓存纹理一次，其余仍走缓存。
- **正确性**：与直画逐位一致（复用 CPU 保留层已实证的 Porter-Duff Over
  结合律论证，XWidget.c:6141-6148/6202-6212 与 XGui.md §8.0d:354-356）。
- **可退化**：任何一步失败自动落回今天的路径（整场景重绘），行为逐位不变。

---

## ② GPU 纹理缓存设计

### 2.1 三类缓存对象与键

| 层 | 缓存对象 | 键（uint64） | 内容代（失效判据） | 失效来源 |
|---|---|---|---|---|
| 控件保留层 | 控件子树渲染输出的 GPU 纹理（透明底、只含子树输出） | `hash(widget 身份)`（指针代+创建序，防指针复用碰撞） | `m_retainedStamp` 计数递增 | 既有失效链（XWidget.c:6149-6153 invalidateForRect）+尺寸/有效性（:6228-6230） |
| 图表静态层 | XChartView 五件套（背景/标题/轴网格等）GPU 纹理 | **`m_staticFp`（FNV-1a 64 位指纹）⊕尺寸⊕格式** | `m_staticFp` 自身（指纹变=失效） | 既有 `xcv_staticFingerprint`（XChartView.c:637-729+，§10.2 契约：主题/轴 range/序列外观，**不含数据点**） |
| 应用通用 | `drawImageCached(image, key)` 的任意图像 | 调用方给的 `cacheKey` | 调用方传 `gen`（版本号） | 显式 `invalidate` 或版本递增 |

**图表层是最低垂的果实**：指纹契约已在档且稳定（§10.2；R-106 已把轴
reverse/可见性修进指纹，XChartView.c:685-707），GPU 侧直接
`key = mix(m_staticFp, w, h, format)`——**GPU 缓存不需要新的失效协议**，
「指纹变更→CPU 层重建」天然级联为「指纹变更→GPU 纹理重上传」。
层可用前提照抄：背景可见才入层（XChartView.c:1670-1672，层内容与目标
既有像素无关的 Over 结合律前提）。

### 2.2 驱动侧纹理缓存（texture cache，recon P2-1 的落地形态）

GL 驱动新增会话级哈希表（对照既有 glyph atlas 的条目数组 +
线性分配先例，XGpuRenderDriver_gl.c:146-156/463-499）：

```
entry { uint64 key; uint32 gen; GLuint tex; int w, h;
        uint64 lastUseStamp; size_t bytes; }
```

- **miss（首帧）**：走既有上传路径一次——CPU 重排预乘 RGBA + `glTexImage2D`
  （复用 `xgpu_upload_argb` 的重排代码，:437-458；格式优化 P2-5 正交可后补）。
  之后命中**零 CPU 重排、零上传**。
- **更新（gen 变化）**：优先 `glTexSubImage2D` 整幅/脏区（驱动已有 region
  增量通道与「存储尺寸跟踪」先例，:506-510 注释；Vulkan 初期无通道则回退
  整幅——照抄 `drawImageRegion` 失败回退整幅的既有约定，
  XPainter.c:178-195）。柱状图数据每帧变化属「动态部分」，**不入缓存**
  （与指纹契约口径一致：序列数据每帧现绘，XChartView.c:628-629/1719-1726）。
- **命中绘制**：绑定缓存纹理 → 既有 textured quad 快路径（一次
  program/blend 检查 + 64B BufferData + DrawArrays，:717-738）；与 P2-2
  几何合批天然叠加（同纹理 quad 可并入一次 BufferData+DrawArrays）。
- **淘汰**：LRU（`lastUseStamp`），预算超限时在**帧边界**执行
  `glDeleteTextures`（避免帧中删除在绑纹理的引用语义歧义）；预算取
  `XGPU_SCENE_CACHE_BUDGET_BYTES`（默认建议 32MB；对照 CPU 保留层
  `XGUI_RETAINED_LAYER_BUDGET_BYTES` 2MB 先例，XGuiConfig.h:554-555——
  GPU 纹理无 CPU 回读压力，可给大一个数量级）。超预算的单对象请求直接
  拒绝、调用方回退直画（不阻塞，照抄 `xwidget_retainedEnsureCache`
  语义，XWidget.c:6161-6200）。
- **会话生命周期**：缓存随会话销毁（GL 纹理不能跨上下文，本机无
  `wglShareLists`，recon §1.1）；resize 触发的销毁重建（procs 无
  `.resize`，recon #35）= 全缓存失效、首帧全量重建——正确性天然保住，
  只是 resize 帧瞬时回落。

### 2.3 失效协议（惰性重建，与 CPU 保留层同构）

- 失效不立即重渲染：内容代 `gen++` / 指纹重算即可；重建发生在该缓存
  **下次被画时**（对照 XWidget.c:6124-6128 `retainedInvalidateOne` 只置
  `m_retainedValid=false`，XChartView.c:1683-1692 未命中才 rebuild）。
- 尺寸变化、格式变化、有效性标志任一不满足 → miss（对照层命中五连判，
  XChartView.c:1677-1682）。
- 应用级批量失效（调色板广播）沿用 `XWidget_invalidateAllRetainedLayers`
  （XWidget.c:6296-6305）→ 逐缓存 gen++，下帧惰性重建。

### 2.4 正确性论证（GPU 版 Over 结合律）

CPU 保留层的既有论证（XGui.md §8.0d:354-356、XWidget.c:6202-6212）在 GPU
侧逐字成立，因为两处混合模型相同：

1. 缓存纹理**透明起画、只含子树自身输出**（GPU 侧：渲染到缓存纹理 FBO 时
   先 clear 透明，子树原语画入——等价 `XImage_fillRect(image,NULL,0u)`，
   XWidget.c:6196）。
2. 驱动 SourceOver 即预乘 `blend ONE/ONE_MINUS_SRC_ALPHA`
   （XGpuRenderDriver_gl.c:613-614），上传路径已保证预乘
   （:446-448 mul255）。
3. 故「缓存内容 over 透明」=子树输出；回贴「子树输出 over 目标既有内容」
   与直画逐位一致（Porter-Duff Over 结合律）——父级背景重绘、兄弟交叠
   不改变缓存语义，**无需相交扫描**（XWidget.c:6141-6148 同款结论）。

位一致验证可直接复用图表层现成的 A/B 旁路机制
（`XChartView_setStaticLayerBypass`，XChartView.h:96-108 / .c:560-570），
把「层开/层关逐位 diff」扩展到「GPU 缓存开/关逐位 diff」。

---

## ③ damage-only 合成流程

### 3.1 目标态帧流程

```
XWidget_repaint（脏区收集，既有 XRegion，无需新建基础设施）
  │  表面裁剪=脏区外接矩形（既有，XWidget.c:5754-5768）
  ▼
XWidget_paintTree(top, &whole)
  │  保留层控件：GPU 缓存命中？
  │    ├ 命中 → 跳过子树 paintEvent 派发，提交 1 个纹理 quad
  │    │        （drawImageCached(key,gen)，scissor 限幅在脏区）
  │    └ 未命中/失效 → 子树渲染进缓存纹理一次（GPU 原语或软栅+上传），
  │                     再提交 quad（同上）
  │  非保留层控件：照旧 paintEvent（动态内容本就每帧画）
  ▼
painterGpuEndFrame → presentToWindow（FBO blit+swap，既有，XWidget.c:5803-5808）
```

要点：

- **FBO 本身就是后备存储**：窗口直通模式缓存纹理合成进 FBO 后由既有
  present 上屏（XWidget.c:5803-5818），**不新增合成 pass、不新增 readback**。
  「回读+BitBlt 上屏」只出现在降级帧（既有 painterGpuFallback 路径，
  XPainter.c:332-344）。
- **提交量裁剪两级**：表面裁剪（外接矩形 scissor）拦 fragment；缓存纹理
  可选走 `drawImageUv` 子矩形（`XGpuRenderBackend.h:198-202` 已有 UV 通道）
  只画「脏区∩层界」部分，进一步省冗余 fragment。保守第一期只做 scissor
  （quad 提交成本恒定 ~15µs，UV 优化后补）。
- **脏区多矩形**：`whole` 本就是 XRegion（XWidget.c:5757-5766 遍历）；第一期
  维持外接矩形口径（与今天一致），二期可按矩形逐个 setClipRect 提交。
- **基准模式的脏区已经很小**：`demo_repaint` 只失效浮层几何
  （xgui_window_demo.c:803-818）——GPU 侧收益的兑现**不需要 demo 改脏区
  逻辑**，只需要 GPU 路径别把整场景重画一遍（1.2 节根因 1-4 消除即得）。

### 3.2 失效帧

子树失效（控件属性/几何/可见性，经既有 addDirtyRegion 链，XWidget.c:385-386/
450-451/744-745/2749-2750）→ 对应缓存 gen++/指纹重算 → 本帧该子树重建一次
进缓存纹理 → 其余子树照常走缓存 quad。最坏情况（全屏失效）退化为今天的
全场景重绘——**性能下界=现状**。

### 3.3 降级帧

`painterGpuFallback`（XPainter.c:332-344）readback 的是**已合成缓存内容的
FBO**，缓存纹理不参与回读——降级路径无新增正确性风险；降级帧本帧走
BitBlt（XWidget.c:5810-5816），缓存状态不受影响（gen/指纹未变，下帧继续
命中）。

---

## ④ 接口草案（分层；C 风格、与既有命名一致）

### 4.1 驱动层（`Src/XGui/Graphics/XGpuRenderDriver.h` procs 表增项；GL/Vulkan 各自实现）

```c
/* 场景纹理缓存：miss 由 draw 内部处理，返回值=是否已画出。 */
bool (*sceneCacheDraw)(XGpuRenderDriverSession*, uint64_t key, uint32_t gen,
                       int dstX, int dstY, float opacity, bool sourceOver);
/* miss/gen 不符时由后端先调：上传（整幅；region 可先 NULL=回退整幅）。 */
bool (*sceneCacheUpload)(XGpuRenderDriverSession*, uint64_t key, uint32_t gen,
                         const XImage* image);
bool (*sceneCacheUploadRegion)(XGpuRenderDriverSession*, uint64_t key,
                               uint32_t gen, const XImage* image,
                               int srcX, int srcY, int srcW, int srcH);
bool (*sceneCacheContains)(const XGpuRenderDriverSession*, uint64_t key,
                           uint32_t gen, int w, int h);
void (*sceneCacheEvict)(XGpuRenderDriverSession*, uint64_t key);
void (*sceneCacheTrim)(XGpuRenderDriverSession*, size_t budgetBytes);  /* 帧界调用 */
```

设计约束：`gen` 不符 = 视同 miss（调用方先 Upload 再 Draw）；procs 增项
必须同步填 `g_xgldOpenGLProcs` 表（recon #35 的教训：字段在表不填=恒回退）。

### 4.2 后端层（`Src/XGui/Graphics/XGpuRenderBackend.h` 公共包装 + 统计）

```c
bool XGpuRenderBackend_sceneCacheDraw(XGpuRenderBackend*, uint64_t key,
                                      uint32_t gen, const XImage* image,
                                      int x, int y, float opacity, bool sourceOver);
bool XGpuRenderBackend_sceneCacheUpload(XGpuRenderBackend*, uint64_t key,
                                        uint32_t gen, const XImage* image);
void XGpuRenderBackend_sceneCacheTrim(XGpuRenderBackend*, size_t budgetBytes);
unsigned XGpuRenderBackend_sceneCacheUploadCount(const XGpuRenderBackend*);
unsigned XGpuRenderBackend_sceneCacheHitCount(const XGpuRenderBackend*);
```

统计口径对照 glyph atlas 先例（`glyphAtlasUploadCount/HitCount`，
XGpuRenderBackend.c:528-534）；驱动未实现（Vulkan 初期/旧 procs）时包装层
返回 false——调用方回退普通 drawImage，**接口presence 不构成行为依赖**。

### 4.3 Painter 层

```c
/* 语义=identity+SourceOver 的 drawImage；GPU 激活走缓存，
   未激活/任何失败回退普通 drawImage（逐位=今天的输出）。 */
void XPainter_drawImageCached(XPainter* self, const XImage* image,
                              uint64_t cacheKey, uint32_t gen);
/* 内容已变化：可使缓存失效（也可不调，靠 gen 单调递增）。 */
void XPainter_invalidateImageCache(uint64_t cacheKey);
```

环境开关与观测：

- `XGPU_SCENE_CACHE=0` 全局关闭（默认建议：合入初期默认 0，A/B 位一致
  回归过后翻转默认 1——回退预案见⑤）。
- `XGPU_PROF` 增 `sceneCache` 计数（hit/miss/upload/uploadBytes），沿用
  5s 窗口聚合（XGpuRenderBackend.c:31-109 既有剖析框架；注意 recon #37
  教训——新原语必须同步挂计数）。

### 4.4 框架自动接线（Phase 2，改动最小化）

| 层 | 接线点 | 键 | 改动性质 |
|---|---|---|---|
| XChartView 静态层 | XChartView.c:1694-1701 命中分支：`XPainter_drawImage` → `XPainter_drawImageCached(..., mix(m_staticFp,w,h,fmt), m_staticFp)` | 指纹 | 单点替换；`g_xcvLayerBypass` 同时旁路 GPU 缓存（A/B 位一致） |
| XWidget 保留层 | XWidget.c:6243-6254 blit 块：GPU 激活时改走 drawImageCached(key=widget 身份 hash, gen=m_retainedStamp) | 身份+时间戳 | blit 目标分叉；CPU 缓存仍是真源（GPU 纹理=其镜像，上传一次/失效一次） |
| demo 静态场景 | xgui_window_demo.c:762-766：GPU 分支改 `drawImageCached(m_staticScene, demoSceneKey, demoSceneGen)`（`m_staticSceneDirty`→gen++，见 :1137/1151/1289/1409/1860 已有的失效点） | 固定 key+gen | demo 侧最小改动；`demo_updateStaticScene`（:652-674）不变 |

XChartView 层的 GPU 化有一个前置语义确认：静态层画布格式=目标表面格式
（XChartView.c:825/881-882），窗口直通下目标无 CPU XImage——GPU 缓存版
层应以 ARGB32_Premultiplied 为规范格式、回退直画路径时维持现状。

---

## ⑤ 风险与回退

| # | 风险 | 缓解 | 回退 |
|---|---|---|---|
| R1 | 指纹/键 64 位碰撞 → 陈旧内容 | 键组合尺寸+格式；FNV-1a 契约已在档（XChartView.c:620-636）；概率工程上可忽略 | `XGPU_SCENE_CACHE=0` 即刻回直画；A/B 位一致断言兜底（XChartView.h:96-108 先例） |
| R2 | 会话 resize/重建后缓存全失效 → resize 帧骤慢 | 全量重建只在首帧；P3-3（procs 填 `.resize`，recon）落地后消失 | 无需专门回退（正确性无损） |
| R3 | 显存预算失控 | 帧界 LRU trim + 单对象超预算拒绝（XWidget.c:6161-6200 不阻塞语义） | 预算调 0 = 全回退 |
| R4 | Vulkan 无 region 通道 → 更新走整幅 | UploadRegion 返回 false 回退整幅（XPainter.c:186-195 既有约定）；P1-2 落地前 Vulkan 只建议默认关 | 同 R1 开关 |
| R5 | 嵌套 painter/会话属主交叉使用缓存 | 缓存按 session 隔离（GL 纹理不可跨上下文，recon §1.1 无 share lists）；属主切换冲批语义对照 g_gpuBatchPainter（XPainter.c:130-131） | miss 回退逐帧上传，性能=今天 |
| R6 | 降级帧与缓存不一致（失效协议 bug） | 降级只读 FBO（③3.3，缓存内容已合成进 FBO）；失效单点=gen/指纹比较，逻辑面极小 | XGPU_SCENE_CACHE=0；XGuiRegression/XGuiRegressionGpu + xgui_gpu_test 三道闸（recon §八尾注） |
| R7 | 半透明内容紧致混合偏差（§8.0d 已登记的 painter 待根治项，XGui.md:366-368） | GPU 缓存不改混合模型（同为预乘 Over），不引入新偏差；既有偏差项独立跟踪 | 不适用（非本设计引入） |

**总回退原则**：每一个接线点（4.4 表三处）都是「命中加速、失败回退」的
包络式替换——开关关闭或任一步失败时，执行路径与今天逐位一致。不存在
「必须成功才能正确」的状态。

---

## ⑥ 预期收益估算（依据 1.1 实测）

稳态帧（浮层刷新，脏区≈浮层小块）成本模型，实测锚：

| 成本项 | 今天（GPU 增量 5.897ms） | 保留后 | 依据 |
|---|---|---|---|
| 原语提交 | ~454 个 × ~15µs ≈ 6.8ms 量级（计实测 avg） | **~10-30 个 quad ≈ 0.15-0.45ms**（保留层控件+浮层文本字形） | 1.1 实测原语数与单价；XGui.md:1348-1350 |
| 整幅纹理重传 | ≥2 张/帧 ≈ 3.7MB/帧（demo 静态场景+chart 静态层，各 800×600×4B≈1.83MB） | **0**（命中零上传；失效帧才传） | XChartView.c:1700 / xgui_window_demo.c:765 / recon #4 |
| 嵌套 FBO 绑定 | ~45 对/帧 | 不变（本设计不消此项；XPainter.c:259-261 记录为独立攻坚项） | XPainter.c:259-261 |
| present | 0.131ms | 不变 | XGPU_PROF 实测 |

**估算结论**（保守口径，供今晚动工前以同口径基准复核）：

- GPU 增量：169.6 → **600-1200 FPS**（帧 5.9ms → 0.8-1.7ms，约 3.5-7×）。
  剩余成本=present+帧骨架+少量 quad 提交+失效帧摊销；嵌套 FBO 项未消，
  是下一台阶。
- SW 2241 FPS 的构成（tile memcpy 0.446ms/帧）在 800×600 轻内容页是软件
  最舒适域（XGui.md:1365-1367 口径），**单页单分辨率下 SW 增量可能仍领先
  GPU 增量**；但 GPU 整帧 166.7 vs SW 整帧 136.7 已反超，而场景保留使
  GPU「增量≈常数、与全帧脱钩」——在 SW 增量优势依赖「脏区恰好很小」的
  场景（重内容页、1080p、全页滚动重绘），GPU 结构优势兑现，此为「全面
  超越」的主战场（XGui.md:1366-1367 同判）。**建议增补 1080p 图表重内容
  页专测后下最终结论**（XGui.md:1365-1367 ⑤ 项）。
- 量化验收锚（每帧消除量，均可由 XGPU_PROF 直读）：原语 454→<30（15×）；
  纹理上传 3.7MB/帧→0；sceneCacheHit/Upload 比→稳态帧 hit≈quad 数、
  upload=0。

### 验证口径（动工时逐条执行）

1. 基线复核：`Tools\probe_demo.exe --benchmark 5 --page 4 --tab 20`
   （默认 / `--benchmark-full` / `XGUI_RENDER_BACKEND=gpu` × 两者，即 1.1 表四格）。
2. 逐位一致：图表页 A/B（`XChartView_setStaticLayerBypass`）GPU 开=关
   截图 diff 全零（对照 XChartView.h:96-108 机制与 XGuiRegression 先例）。
3. 三道闸（recon §八）：xgui_gpu_test（新增断言：sceneCache miss 恰一次
   upload/命中零 upload——对照字形图集断言先例 xgui_gpu_test.c，recon §4.4）、
   XGuiRegression + XGuiRegressionGpu、demo --autotest（注意其 GPU 截图
   缺口 #39 勿作正确性证据）。
4. 收益复测：同 1，另加 `XGPU_PROF=1` 读 sceneCache 计数。

---

## ⑦ 分期落地建议

| 期 | 内容 | 涉及 | 门槛 |
|---|---|---|---|
| P-A | 驱动纹理缓存 + procs/后端/painter 接口 + `drawImageCached`（开关默认 0） | XGpuRenderDriver_gl.c、XGpuRenderDriver.h、XGpuRenderBackend.c/.h、XPainter.c | xgui_gpu_test 新断言全绿 |
| P-B | XChartView 静态层接 GPU 缓存（单点替换，4.4） | XChartView.c | A/B 位一致 + 图表页四格基准 |
| P-C | XWidget 保留层 GPU 化 + demo 静态场景 drawImageCached（4.4） | XWidget.c、xgui_window_demo.c | 保留层 ASan 探针复跑（XGui.md:361-365）+ 全量回归 |
| P-D | 开关翻转默认 1；UV 子矩形（③）；与 P2-2 合批联动复测 | 各处 | 1080p 重内容页专测（XGui.md ⑤ 项） |

依赖关系：P-A 独立可先行（= recon P2-1）；P-B 是收益最大单点（图表页
1.83MB/帧 + 整场景五件套原语全消）；P-C 后 demo 页与通用控件全面受益；
P2-2（几何合批）、#7（冗余上下文操作）、嵌套 FBO 收敛与本文正交、收益
叠乘。
