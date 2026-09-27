# Night3 交互白窗/碎片 · 呈现链读码分析（GPU 直通一次 PAINT 全序列）

日期：2026-09-27。执行：呈现链读码车道（纯读码，零源码改动、零构建、零运行验证）。
范围：mouse move → XWidget_addDirtyRegion → postPaintEvent → XWidget_flushBackingStore
（XGPU_PRESENT_MAX_FPS 区域）→ paintTree → painter begin/end（keep-open 现默认关）→
批量画布 → readbackRect → XBackingStore_flush → BitBlt。
证据基线：docs/xgui/night3-interaction-sweep.md（9 页拖扫，页 0 整窗白且松开不愈、
页 4 冻结反而干净）、Tools/sweep_drag/、Tools/matrix/。
本报告全部结论来自本会话对下列文件的逐行阅读，未运行被测物。

---

## 1. 完整调用序列与每步产物

### 1.1 输入侧（mouse move → 脏区 → PAINT 入队）

| # | 步骤 | 位置 | 产物 |
|---|------|------|------|
| 1 | MOUSE_MOVE 派发 | XWidget.c:2004-2007 `XWidget_dispatchPointerEvent(top, event)` | 控件悬停/拖动状态翻转 |
| 2 | update/updateRect/updateRegion | XWidget.c:5666-5684 | `XWidget_addDirty`（969-981，栈上单矩形视图） |
| 3 | addDirtyRegion | XWidget.c:921-966 | 矩形∩contentsRect → 平移到顶层坐标 → 并入 `top->m_dirty`；置 PendingUpdate；调 postPaintEvent |
| 4 | postPaintEvent | XWidget.c:504-535 | CAS `m_paintEventPosted` 0→1（已有事件则视为成功）；`XWidget_createPaintEvent`（473-487）**复制当时 m_dirty 快照**进 PAINT 事件（XPaintEvent_createRecycled 单槽复用）；tryPostEvent 入队 |

时序要点：事件携带的是**入队时**的脏区快照；入队后新增的脏区留在 m_dirty，由 flush 内
差集（步骤 10）保留到下一轮。

### 1.2 帧侧（PAINT 派发 → flushBackingStore）

| # | 步骤 | 位置 | 产物 |
|---|------|------|------|
| 5 | PAINT 事件处理 | XWidget.c:1974-1999 | **先**清 `m_paintEventPosted`（1979，允许绘制中 update 再入队），再 `XWidget_flushBackingStore(top, &event->m_region)`（1986，借用事件区域，零拷贝） |
| 6 | flush 前置 | XWidget.c:6251-6273 | 顶层回溯；确保窗口/存储；`XBackingStore_resize`（6273，尺寸同前则 no-op） |
| 7 | whole 定稿 | XWidget.c:6281-6316 | region 非空→复制事件快照；否则 m_dirty 包围盒单矩形（P0-4 限绘）；再退 contentsRect。XGUI_FLUSH_FULLFALLBACK=1 恢复整窗退化 |
| 8 | wholeBbox 收拢 | XWidget.c:6324-6349 | 两遍求 bbox（防右/下边界静默收窄），供表面裁剪/限频整窗判定/readbackRect 三处共用 |
| 9 | 脏区差集 | XWidget.c:6356-6367 | PendingUpdate 清位；m_dirty 减去 whole（region==&m_dirty 时直接清空） |
| 10 | GPU 会话获取 | XWidget.c:6379-6396 | `XGpuRenderBackend_acquireForWindow(hwnd,w,h)`：命中则置 g_xgpuActiveSession 并**清 frameDegraded**（XGpuRenderBackend.c:514-515）；尺寸变化走驱动 resize，GL 无 resize 实现→销毁重建（473-491）；首会话帧补一次 XWidget_update（6390） |
| 11 | 表面裁剪+beginPaint | XWidget.c:6398-6405 | `g_paintTargetImage`=后备图像（Win32 零拷贝=**DIB 本体**，XPlatformBackingStore.c:959-1023）；`XPainter_setSurfaceClipRect(&wholeBbox, image)`；`XBackingStore_beginPaint`（簿记位，XPlatformBackingStore.c:1216-1266） |
| 12 | paintTree 递归 | XWidget.c:6435 → 6076-6177 | 逐控件：单矩形区域、遮罩/保留层/效果分支；每控件 `XPaintEvent_initBorrow` → paintEvent（6141-6152）；控件 paintEvent 内 `XPainter_begin_image(&painter, paintImage)` … `XPainter_end`（模式见 XLabel.c:2036/2065） |

### 1.3 每 painter 的 GPU 生命周期（keep-open 默认关，XPainter.c:731-747）

| # | 步骤 | 位置 | 产物/GL 调用 |
|---|------|------|--------------|
| 13 | begin_image GPU 分支 | XPainter.c:7512-7599 | `XGpuRenderBackend_current()` 取窗口会话；尺寸不符→保持软件（7523-7533）；防御性 `painterGpuBatchFlush`（7537）；`g_gpuFrameDepth==0`（keep-open 关 → 上一 painter end 已关帧）→ **每 painter 一次** `XGpuRenderBackend_beginFrameImage` → `xgld_begin_frame`（XGpuRenderDriver_gl.c:2231-2271：bind FBO、viewport、PACK_ALIGNMENT、scissor off、blend on；窗口 FBO **持久不清**，仅会话 m_firstFrame 上传 DIB 或 clear(0)，2249-2270） |
| 14 | 批量画布整幅清零 | XPainter.c:7583-7593 | 深度 0→1 分支：**整幅 fillRect（800x600≈1.92MB）每 painter begin 一次**（keep-open 关时每控件都走这里——正是 7551-7556 注释里被移出复用分支、却仍在 0→1 分支的那笔开销） |
| 15 | 绘制命令路由 | XPainter.c（多处） | GPU 快速路径：solid quad 入批 / 字形图集 quad / drawImage 上传 / 渐变 LUT（UV 子矩形）；无快速路径→`painterGpuSubmitSoftwareCommand`（1327）：批量透明暂存画布上软件光栅（m_image 临时切换），脏区=裁剪包围盒∩命令几何，钳位并入 `g_gpuBatchDirty`（1202-1278） |
| 16 | XPainter_end → painterGpuEndFrame | XPainter.c:796-875 | 先 `painterGpuBatchFlush`（803-804）；窗口模式**不 readback**；keep-open 关→`XGpuRenderBackend_endFrame` → `xgld_end_frame`（XGpuRenderDriver_gl.c:2285-2305：flush quads；FBO persist 默认不解绑；上下文保持 current） |
| 17 | 批量提交（冲刷点） | XPainter.c:565-608 | 清 scissor；`XGpuRenderBackend_drawImageRegion(画布, dirty)`（SourceOver；驱动 XGpuRenderDriver_gl.c:2970-3064：`glTexSubImage2D(BGRA/REV, UNPACK_ROW_LENGTH=iw)` 直传 + quad 采样，UV 顶行 v=0 与 NDC y 翻转自洽，3181/1808-1825）；失败回退整幅 drawImage（594-597） |
| 18 | 冲刷点画布清洁 | XPainter.c:381-471 | `xgpu_batch_canvas_clear_rows`：行带 [y0-8, y1+8) ×（缺省）**整宽 memset**（夜八拖动残影修复转正；XGPU_CANVAS_TRACE_CLEAR=0 回退列窗 [x0-8,x1+8)）；XGPU_CLEAR_ROWS_FULL=1 同宽诊断口径 |

### 1.4 呈现侧（帧末 readback → BitBlt）

| # | 步骤 | 位置 | 产物/GL 调用 |
|---|------|------|--------------|
| 19 | endPaint | XWidget.c:6436 | 簿记位清零 |
| 20 | 限频门（XGPU_PRESENT_MAX_FPS，缺省 60） | XWidget.c:6464-6501 | elapse<16.6ms 且非整窗帧（coversFull 按 wholeBbox 判）→ **本帧完全不 readback/不 flush**，whole 并回 m_dirty（6495-6496）；FBO 内容持久，下周期补上 |
| 21 | 脏区读回（默认通道） | XWidget.c:6531-6579 → XGpuRenderBackend.c:1160-1191 → XGpuRenderDriver_gl.c:2727-2734, 2609-2702 | `readbackRect(gpuWindow, paintImage, wholeBbox…)`：尺寸/驱动身份门（1165-1179，非 GL 表→false 回退整帧）；越界钳位；**先 flush quads**（2636）；PBO prev 槽命中（bbox⊆上帧 bbox 且 fence 就绪）→ **拷出上一帧内容**（1 帧滞后，2641-2662）；否则同步直读 `glReadPixels(x, m_height-y-height, w, h)` 紧排 + 自底向上拷出（2686-2693）；随后为下帧发异步 PBO 读（2695-2696，glY=m_height-y-height 全帧布局，2563-2596） |
| 22 | 读回失败回退 | XWidget.c:6564-6566 | 整帧 `readback`（2704-2713，不吃 PBO 滞后） |
| 23 | flush/BitBlt | XWidget.c:6569 → XPlatformBackingStore.c:715-789 → XPlatformBackingStore_win32.c:382-424 | flushRegion=whole 裁剪；native 零拷贝模式跳过 inactive 同步（756-770 的 !m_nativeBufferMode 守卫）；**逐矩形 BitBlt(SRCCOPY) 从 DIB/memDC 到窗口 DC**（410-416）；DIRECT 双缓冲翻转被 native 模式单缓冲短路（786-788） |
| 24 | 收口 | XWidget.c:6571, 6597-6598, 6609-6621 | setFramePresented（诚实口径）；`endWindowFrame`（activeSession=NULL、frameDegraded=false，XGpuRenderBackend.c:519-523）；残留 m_dirty→重投 PAINT（6617） |

整窗帧判定（coversFull）、swap 通道（XGPU_PRESENT=swap，XWidget.c:6504-6530）与
degraded 腿（6581-6596）为旁支，见 §3-S3/S5。

---

## 2. 三个指定问题的读码结论

### 2.1 keep-open 关闭：每 painter begin/end 的开销与正确性

开销（每控件 painter、每帧）：
- begin：current() 查询 + 防御冲批 + `beginFrameImage`（≈5-6 次 GL 真调，FBO persist 下
  bind 走镜像）+ **整幅画布 fillRect 1.92MB**（XPainter.c:7583-7593）。45 控件页 =
  45 次 beginFrame + 45×1.92MB memset/帧——keep-open 原始收益（摊销 begin/end）的
  成本主体仍在深度 0→1 分支，未随默认翻转消失。
- end：冲批（commit+clear_rows）+ endFrame（flush quads；不解绑 FBO、不 doneCurrent）。

正确性/时序窗：
- **无丢批窗口**：每 painter end 先冲批再 endFrame（XPainter.c:803-804）；所有读回/呈现
  /收口端点前都先 flush quads（XGpuRenderDriver_gl.c:2636/2330/2289）。
- **无丢读回窗口**：窗口直通模式 painter 帧末本就不读回（像素契约由呈现链 readbackRect
  兑现）；读回只发生在呈现链一处且在 flush（BitBlt）之前（XWidget.c:6560→6569），
  DIB 在提交前已含本帧 bbox 内容。
- keep-open 关闭反而消除了「跨 painter 保帧期间批量画布墨迹跨批存活」类缺陷
  （XPainter.c:737-741 注释的四态 A/B），§3-S1/S2 与之独立、不受此开关影响。

### 2.2 readbackRect 子矩形 Y 翻转（bbox 不满高）——逐行核对：正确

对照全帧 readback 的翻转约定逐行核：
- 同步路径（2686-2693）：`glReadPixels(x, m_height-y-height, w, h)` → 缓冲行 r =
  GL 行 m_height-y-height+r = 图像行 y+height-1-r；拷出行源 `m_pixels+(h-1)*w*4`、
  步进 -w*4 → 拷出行 r = 图像行 y+r。全帧退化为 x=0,y=0,w=W,h=H 时与旧整帧读回
  逐位同构（GL y0=m_height-H=0）。
- PBO 路径（2563-2596 发起 / 2656-2662 拷出）：写入全帧 GL 行序布局，bbox 首行
  glY=m_height-y-height；拷出行源取 GL 行 m_height-y-1（=图像顶行 y）、步进
  -m_width*4。包含判定用图像坐标（2641-2647），与 GL 行序仿射反演一一对应。
- 上游约定自洽：solid quad/图集 quad 的 NDC y=1-2y/h 使「图像顶行=FBO 高地址行」
  （1808-1825），与 readback 的翻转对称；drawImageRegion 上传不翻转（图像顶行=纹理
  v=0，3181），quad 顶边采样 v0 → 画布行 srcY+k 落到帧行 dstY+k，方向正确。

结论：**Y 翻转换算在 bbox 任意子矩形（含不满高）下与全帧约定逐位一致，无错**。
PBO 路径的真实语义风险是「1 帧滞后」而非几何错位（见 §3-S4）。

### 2.3 clear_rows ±8px 外扩与 readbackRect bbox 的覆盖关系

- 「bbox 内未清区域被当作透明提交」**在现有通道里不成立**：区域通道只上传
  `g_gpuBatchDirty`（XPainter.c:590-592），画布上 dirty 之外的像素根本不进 FBO；
  整幅回退通道（594-597）提交时画布刚被每 painter begin 整幅清过（7583-7593），
  透明像素 SourceOver 幂等（562-563）。
- 真正的暴露面是**反向**的：±8 外扩带只清**画布**，**FBO 从未收到渗出像素**——
  commit 矩形无外扩，画布带内被清掉的 AA 渗出不会进 FBO，FBO 带内保留上一帧旧墨；
  只要本帧 wholeBbox 覆盖该带（AA 边缘必然覆盖），呈现的就是 FBO 旧墨。
  夜八的行带整宽清修复治的是「画布跨批墨迹」，不治「FBO ±8 带旧墨」（§3-S2）。
- 残余的「未清被提交」窗口：同一 painter 内两次冲批之间，逃过脏区记账 >8px 的墨迹
  滞留画布，被后续冲批的 dirty 矩形覆盖并随之上传。keep-open 关闭后该窗口收窄到
  「单 painter 内、两冲批间」，且记账下界=裁剪包围盒（1204-1233）通常≥实际写入，
  触发面小（§3-S6）。

---

## 3. 疑点清单（按置信排序；S1 为白窗/碎片主嫌）

**S1【高】持久 FBO 增量 SourceOver 提交 × 非不透明重绘 = FBO 内容跨帧叠加腐坏（白窗/碎片/不自愈的统一机制）**
- 依据：窗口 FBO 持久、「脏区绘制叠加在上一帧内容上」（XGpuRenderDriver_gl.c:2249-2251）；
  批量提交 SourceOver、透明像素幂等（XPainter.c:562-563）；每帧只重绘 whole
  （XWidget.c:6281-6282）。任何 alpha<1 的重绘（AA 文字边缘、半透明 QSS、圆角、
  图形效果——页 8 的透明按钮/模糊标签即此类）逐帧叠加在 FBO 旧内容上。
- **SW/GPU 不对称根因**：软件模式的每帧不透明打底（autoFillBackground 的
  `XImage_fillRect` CPU 直写，XWidget.c:1833-1835）在 GPU 直通模式对 FBO **不可见**
  （DIB 直写不进 FBO，呈现内容又只来自 FBO 读回）——SW 每帧把区域重置为不透明底再
  叠一层，GPU 没有这个重置。拖动 0.8s × 数百帧叠加 → 半透明白/彩色层堆积成整窗
  白化与文字残丝；蓝涂抹条（不透明 solid quad）反而是存活物——与页 0 症状
  「余蓝涂抹条+文字残丝」吻合。
- 与证据的吻合：页 4 交互零重绘→零叠加→唯一干净（sweep §2/§3）；松开不再重绘→
  FBO 腐坏定格→**不自愈**（页 0/1/5/6/7/8 的 f006/f007）；FPS 面板每帧重绘→叠加
  最快→横噪/白楔。
- 诚实声明：本车道未运行验证逐层 alpha；机制为读码实证（上述四点代码事实）+ 症状
  对齐的推断。

**S2【高】批量画布 ±8 清洁带与 FBO commit 矩形不匹配：渗出带 FBO 侧保留旧墨**
- 清 [dirty±8]（画布，XPainter.c:430-447）、commit 无外扩只上传 dirty（590-592）；
  带内渗出像素被清掉即永不进 FBO，FBO 带内旧墨被 bbox 读回呈现。移动元素边缘
  软边被裁 + 旧边残留 → 横向残丝/噪带观感（页 3/5/7 噪点带）。

**S3【中高】degraded 腿整帧读回会覆盖本帧软件直写内容（路径近乎休眠但存在）**
- 帧内 `painterGpuFallback`（readback 合并+endFrame+degraded=true，XPainter.c:881-896）
  后，同帧后续 painter 纯软件直写 DIB；帧末 degraded 腿 `if (gpuWindow) readback 整帧`
  （XWidget.c:6590-6592）用 FBO（只有降级前内容）整帧覆盖 DIB → 软件部分丢失。
- 现状缓解：三处 XPAINTER_GPU_FALLBACK（4237/5835/11435）仅在 !m_gpuActive 时可达
  （GPU 分支均先行 return）→ 整帧降级近乎不可达；但 `beginFrameImage` 失败
  （7569 else-if 不成立，如上下文丢失/RDP 重连）同样产生「本帧全软件直写 DIB + 帧末
  被 readbackRect/整帧 readback 覆盖」→ 该帧丢失。另 acquireForWindow 每帧清
  degraded（XGpuRenderBackend.c:515）使「降级帧补读回」注释设想的场景（帧初即
  degraded）实际不可达，该腿在「帧内中途降级」场景是净伤害。

**S4【中】PBO 1 帧滞后读回：呈现内容可为上一帧**
- readbackRect allowPboLag=true 命中时拷出上一帧 PBO（2641-2662）。bbox 静止子区
  （FPS 面板）每帧命中 → 恒滞后一帧；叠加限频跳帧可呈现 2-3 帧旧内容。几何正确、
  非白窗源，但拖动跟手性/面板时序存疑。XGPU_PBO_READBACK=0 可关。

**S5【中】限频跳帧放大 S1 的可见突变**
- 跳帧帧完全不呈现（XWidget.c:6490-6497），FBO 却已叠加多层；下一次呈现把累积多层
  一次性读回上屏 → 腐坏以「跳变」出现，时间点与 60Hz 窗口对齐（拖后 0.5-1s 内整窗
  突变与 200+ 帧/0.8s 的叠加节奏一致）。跳帧本身最终一致性有保障（m_dirty 并回+
  重投，6617）。

**S6【中低】同一 painter 内跨冲批的画布滞留墨迹随后续 dirty 被提交**
- 仅当写入逃过脏区记账 >8px 且落在后续冲批 commit 矩形内（1202-1278 记账、430-447
  清洁带）；keep-open 关闭后窗口已收窄，触发面小。XGPU_CANVAS_BEGIN_CLEAR=1 可
  强制 begin 段整幅清零对照。

**S7【低】窗口会话销毁重建瞬态 → 透明 FBO 增量补绘**
- 尺寸变化且 GL 驱动无 resize 实现 → destroy+recreate（XGpuRenderBackend.c:486-491；
  XGpuRenderDriver_gl.c:694「无中途 resize 路径」）；新 FBO m_firstFrame 从 DIB 上传
  保留内容，initialImage 尺寸不符时 clear(0)（2252-2270）→ 透明底上只补脏区，直到
  下次全窗 flush（EXPOSE，XWidget.c:1959-1973）自愈。拖动中窗口尺寸不变，难触发。

**S8【信息】readbackRect/readback 尺寸门与 target 一致性**
- 两通道都要求 target 尺寸==会话尺寸（XGpuRenderBackend.c:1165-1168；GL 2631-2633），
  失败自动回退整帧，无静默错位；XBackingStore_resize 仅在尺寸变化时重建（零拷贝
  路径深拷贝快照回填，XPlatformBackingStore.c:961-1023），正常拖动不触。

---

## 4. 与三个问题对应的开关/对照口径（供归因实验，本车道未运行）

- S1 对照：XGUI_FLUSH_FULLFALLBACK=1（整窗退化→每帧不透明重置，应显著缓解白窗）；
  或对比页 4（零重绘基线）。
- S2 对照：XGPU_CANVAS_TRACE_CLEAR=0 vs 缺省（画布侧差异）；FBO 侧带旧墨无现成开关，
  需在 commit 侧扩带或读回后透明带复核（属后续车道）。
- S3 对照：XGPU_SESSION_RETRY=0（禁重探，放大上下文丢失窗口）+ XGUI_PRESENT_HONEST。
- S4 对照：XGPU_PBO_READBACK=0；XGPU_PBO_STATS=1 看命中/seed/stall 比。
- keep-open 专项：XGPU_FRAME_KEEPOPEN=1 恢复保帧（对照其残影放大，737-741 四态）。

## 5. 结论

一次交互拖动 PAINT 的 GPU 直通链路在**几何/时序上自洽**：readbackRect 的 Y 翻转
（问题 2）与全帧约定逐位一致；keep-open 关闭（问题 1）无丢批/丢读回时序窗，代价是
每 painter 一次 beginFrame+1.92MB 画布 memset；批量画布 ±8 外扩与 bbox 的关系
（问题 3）不存在「未清被当透明提交」，真实暴露面是 FBO 侧渗出带旧墨（S2）。
整窗白化/碎片且松开不自愈的最优先解释是 **S1：持久 FBO 增量叠加 × SW 独有的
不透明打底直写对 FBO 不可见**，其次按 S2→S3→S5 排查。
