# 2026-09-24 GPU 夜战勘察报告

> 2026-09-24 凌晨五路只读勘察（GL 驱动 / Vulkan 驱动 / 后端与 painter / 演示与基准 / 构建接线）+ 逐条复核汇编 · 行动材料（目标：OpenGL 与 Vulkan 双双远超软件渲染，并修复 vulkan 后端在 lavapipe 下 SIGSEGV 违反回退契约）。
> **行号基线（必读）**：GL 驱动 `Drive/windows/Graphics/XGpuRenderDriver_gl.c` 勘察期间遭并行编辑（1487→1621@01:36→1637@01:45→1611@01:51），Vulkan 驱动 `Drive/Posix/Graphics/XGpuRenderDriver_vulkan.c` 同样在改。本报告正文引用**复核锚**（01:51 状态，复核逐条重定位过）优先，差异大者标注；**汇编时（01:55/02:05）实测两文件又漂移**——GL 现为 1663 行、Vulkan 现为 2114 行（`m_atlasSet` 锚 1484→1528、GL procs 表 `drawImageRegion` 1596→1648，本次抽查 findstr 实证）。**所有行号动手前必须重新定位**。其余文件相对稳定：`XPainter.c`（14358 行，mtime 01:40）、`XGpuRenderBackend.c`（970 行，01:37）、`XPlatformGraphics_win32.c`（343 行，未变）。
> 本轮为只读勘察：除 `vulkaninfo.exe --summary` 与 vcvars64 探测外未构建、未运行任何程序；所有结论以勘察/复核时实读代码为准。

## 〇、结论速览

1. **Windows 构建当前确为 GL-only，但原因与任务书表述不同**：「Vulkan 驱动只在 Drive/Posix」仅是**文件位置**——`CMakeLists.txt:102` 裸 `file(GLOB_RECURSE "Drive/*.c")` 把 Vulkan 驱动也编入 Windows（build.ninja 有其 .obj，仅 4,142 字节=被宏裁空的 TU，对照 GL 驱动 .obj 90,704 字节）；真正原因是 `find_package(Vulkan QUIET)`（CMakeLists.txt:270-276）在本机 NOTFOUND → `XINYUE_C_HAS_VULKAN` 未定义 → Vulkan procs 返回 NULL（XGpuRenderBackend.c:180-183），回退实走 gl→software。**本机装上 SDK 该驱动即在 Windows 原样激活**。
2. **Vulkan 移植 Windows 的代码改动≈0**：win32 surface 契约层已备齐（XPlatformGraphics_win32.c:135-182），加载层是纯链接期依赖，CMake 已接好；唯一硬缺口是**本机没有任何可链接的 vulkan-1.lib**（全盘实测）。但**前置条件是先修帧路径五项 UB**（隐患 #13-#17），否则换任何平台同样崩——这正是 lavapipe SIGSEGV 的根因集（XGui.md:1238/1381 在档）。
3. **Vulkan 性能天花板是设计级的**：全同步提交（QueueWaitIdle 五处，隐患 #20）+ 每次 drawImage 全队列往返（#22）+ resize 全量重建 instance/device（#19），不消除这三条不可能「远超软件」。
4. **GL 性能三大项**：drawImage 每调用整幅纹理重传无缓存（#4）、逐原语全套管线状态无合批（#1/#34）、上传 CPU 逐像素重排（#5）。另 present 后每帧 4 次上下文操作中 2 次纯冗余（#7）。vsync 已由并行编辑显式关闭（`wglSwapIntervalEXT(0)`，gl.c:873 全树唯一命中）。
5. **painter 层复核推翻一条关键旧账**：「每批首全帧 readback」已不存在——批量提交已改脏区化 `drawImageRegion` 增量通道（XPainter.c:178-188）；但 `XGUI_GPU_SYNC` 的 `"0"` 语义在 painter 与后端两端相反（#24，高危），且带单矩形裁剪的 drawImage 系统性被拒出 GPU 快路径（#32）。
6. 回归三套件 target/ctest 已接线，bin/ 下 exe 在场（两次勘察观测不一致，见附录 A.4）。

---

## 一、GL 驱动（Drive/windows/Graphics/XGpuRenderDriver_gl.c）

### 1.1 窗口会话与上下文

- `xgld_session_create_window`（勘察锚 778-813）经 `XPlatformOpenGLContext_create` → `XPlatformGraphicsDriver_createOpenGL`（win32.c:46-79）：HWND=(HWND)XWindow_winId（win32.c:56）、GetDC **用窗口 DC 而非 pbuffer**（:58）；像素格式 `PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER`、RGBA32/深度 24/模板 8（:60-68）——深度/模板驱动全程未用（隐患 #9）；`ChoosePixelFormat/SetPixelFormat`（:69-70）、**legacy `wglCreateContext`**（:71，无 WGL_ARB_create_context、无上下文共享）。
- vsync：`wglSwapIntervalEXT(0)` 显式关闭，会话创建内一次性加载调用——gl.c:873 全树唯一命中（本次汇编 findstr 复核通过）。演示路勘察起始版本无此调用，系并行编辑新增。
- 会话追踪：每会话独立上下文无 `wglShareLists`，驱动以 `g_xgldCurrentSession` 跨会话切换（勘察锚 203-236，复核锚 xgld_make_current 210-230 每次直调 `wglMakeCurrent`，无同上下文短路）。

### 1.2 present 路径

`xgld_present_to_window`（勘察锚 934-1030）：`m_hasBlit`（glBlitFramebuffer 或 NV 变体，初始化于勘察锚 733-736）快路径 FBO→默认帧缓冲 1:1 NEAREST blit（复核锚 976-996）→ `XPlatformOpenGLContext_swapBuffers` → win32 `SwapBuffers`（win32.c:104-108）；无 blit 时回退整屏 quad 采样（复核锚 998-1016）。无 glReadPixels、无 GDI（BitBlt/SetDIBits 只在软件后备存储 win32 后备路径，与 GL present 无关）。present 四出口 doneCurrent 清追踪器 → 帧头冗余 make_current（隐患 #7）。

### 1.3 readback

`xgld_readback`（勘察锚 1032-1096）：整幅同步 `glReadPixels`（复核锚 1056-1057，PACK_ALIGNMENT=1）→ ARGB32/预乘快路径行翻转+R/B 交换直写 XImage bits（复核锚 1070-1085），慢路径逐像素 unpremultiply（复核锚 1090-1104）。**无 PBO 异步**（隐患 #6）。字形图集读回同病（复核锚 1558+，临时 FBO 挂纹理）。

### 1.4 离屏会话

`xgld_session_create_offscreen`（勘察锚 815-838）→ win32.c:273-317：CreateWindowExW 隐藏 WS_POPUP "STATIC" 窗口（:290-292，0 尺寸兜底 1x1:285-286）+GetDC+同款双缓冲 PFD+legacy 上下文——**隐藏窗口 DC 而非真 pbuffer**。缓存图离屏会话每帧 2 次全帧同步+合成第 3 次整幅上传（隐患 #27）；尺寸不符整体销毁重建（隐患 #35）。

### 1.5 关键调用点（勘察锚，供检索）

glReadPixels：gl.c:1045/1568（声明 :140，加载 :670）。SwapBuffers：win32.c:107。wglMakeCurrent（win32 原生）：win32.c:85/95/101/323/334/340；驱动层路由点约 30 处（建会话 791-833、destroy 850/866、beginFrame、endFrame、present、readback、clear、各原语）。`glGetError` 零调用点、`glFinish/glFlush` 全树零匹配、`getProcAddress` 每会话一次（隐患 #11，排除项核实通过）。

### 1.6 性能要点

逐原语管线状态（#1）、无几何合批/实例化（#34，procs 仅加载 glDrawArrays :698，无 DrawElements/Instanced）、每 quad 64B glBufferData 孤儿化（#2，subdata 修法已被实测否定 216→106 FPS，剩余空间是 persistent-mapped ring）、drawImage 整幅重传（#4）、CPU 逐像素重排四处（#5）、渐变双上传（#10）。procs 表 `g_xgldOpenGLProcs` 复核锚 1582-1603 共 22 项（汇编时 `drawImageRegion` 位于 :1648），**无 `.resize`**（隐患 #35）。

## 二、Vulkan 驱动（Drive/Posix/Graphics/XGpuRenderDriver_vulkan.c）

### 2.1 加载层

链接期依赖非 dlopen：`#include <vulkan/vulkan.h>`（:35），全部 vk* 为 extern 直调，无 vkGetInstanceProcAddr 动态取址（仅平台探测函数用 ProcAddr，也是 extern 直调）。驱动核心仅需 vulkan_core.h（文件头 :12-14/32-34 明示不含平台窗口系统头）；平台 surface 代码在 Drive 层：Posix 需 `VK_USE_PLATFORM_XLIB_KHR`+X11 头（XPlatformGraphics_posix.c:257-269），Windows 对应实现已存在（vulkan/vulkan.h+vulkan_win32.h，win32.c:131-133）。shaders 头是纯 uint32_t SPIR-V 数组（XGpuRenderDriver_vulkan_shaders.h:17-88），零改动可移植。**移植加载层工作量≈0**；若要求 vulkan-1.dll 缺失不致进程加载失败才需 LoadLibrary+函数表改造（非最小改动；现有 `GetModuleHandleW(L"vulkan-1.dll")` 探测 win32.c:184-188 晚于导入表加载，形同虚设）。

### 2.2 窗口/离屏会话与 present

- 窗口：VK_KHR_surface+VK_KHR_xlib_surface 实例扩展（posix.c:287-289）→ vkCreateXlibSurfaceKHR（:315-316）→ VK_KHR_swapchain 设备扩展（勘察锚 861-866）→ swapchain+FIFO presentMode（:656）+每交换链图像一个 framebuffer（:743-772）；beginFrame 内 vkAcquireNextImageKHR（复核锚 1257-1261），endFrame 内 vkQueueSubmit+vkQueuePresentKHR 直接上屏（:1302-1319）；**presentToWindow 是恒 true 桩**（:2002-2006）。
- 离屏：无实例扩展、单 VkImage（COLOR_ATTACHMENT|TRANSFER_SRC|TRANSFER_DST，:619-647），readback 拷回 XImage 后走软件 BitBlt（XPainter.c:300-301 帧末读回；XWidget.c:5803-5816 降级帧同路）。
- readback：窗口会话直接 false（:1433）——SYNC 模式读回半边静默 no-op（隐患 #21）；离屏 suspend（:1327-1341）→ vkCmdCopyImageToBuffer 到 HOST_VISIBLE staging（:1365-1390）→ CPU 写回 XImage（:1392-1425）→ resume（:1343-1351）。行序与 XImage 一致无需翻转（文件头 :11-12）。

### 2.3 SIGSEGV 与回退契约（复核确认，危害详见隐患表 #13-#18/#20-#23）

初始化序列判得相当全（instance/surface/物理设备/队列族/device/queue/pool/fence/semaphore/swapchain/内存类型，fail 统一 sessionDestroy 全程判空）；**真正违反回退契约的 UB 全在帧路径**：m_atlasSet 未写即绑（#13）、图集 upload/readback 帧中 reset 重录（#14）、draw_image 扩容销毁在途资源（#15）、打断五连不查返回值（#16）、acquire 信号量重用（#17）、格式失配边缘路径（#18）。XPainter 侧只有会话创建失败降级（`g_xgpuRenderProbeFailed`），**驱动内崩溃无法被其捕获**——回退契约被绕过。文档在档：XGui.md:1238/1381（本次汇编实读复核）。

### 2.4 注册表与回退

`XGpuRenderDriver_procs`（XGpuRenderBackend.c:178-187，汇编时实测宏分支位于 :166-168/:180-183）：定义宏时 Vulkan 优先返回 `XGpuRenderDriver_vulkan_procs()`（静态表恒非空），否则 NULL→`xgpu_create_ex` 立即回退 OpenGL（:552-557）；sessionCreate 失败同样改试 GL（:567-575）；再失败 NULL 保持软件（:576-580）。Posix 接线 CMakeLists.txt:164-169（`if(UNIX AND NOT APPLE)` 内 find_library），Windows :270-280（find_package(Vulkan QUIET)→Vulkan::Vulkan）。

## 三、后端与 painter（Src/XGui/Graphics/）

### 3.1 生命周期与有序回退

驱动类型解析 `xgpu_driver_type`（XGpuRenderBackend.c:535-542）：env `XGUI_RENDER_BACKEND`/`XGPU_BACKEND` 值为 "vulkan"→Vulkan，其余（gpu/opengl/1/true/on 或未设）→OpenGL；requested 判定同口径（:297-317），运行期覆盖 `addRequestedOverride`（:597-602）。回退链 `xgpu_create_ex`（:544-583）见 §2.4。会话两级：painter 级进程离屏会话（XPainter.c，尺寸不符销毁重建 #35、失败永久 latch）与窗口直通会话 `XGpuRenderBackend_acquireForWindow`（Backend.c:351-395：优先驱动原地 resize :362-368，失败 latch `g_xgpuWindowProbeFailed` :383，atexit :386-390）；1x1 探测 :604-619。整帧降级 `painterGpuFallback`（#26）置位后同帧不再激活 GPU，复位点=acquireForWindow/endWindowFrame/shutdown。

### 3.2 批量提交流水线（复核修正版）

全局状态 g_gpuBatchPainter/Backend/Canvas（持久全帧 ARGB32 暂存画布，XPainter.c:130-133）/Active。软件命令入口 `painterGpuSubmitSoftwareCommand` → `painterGpuSubmitSoftwareCommandRect`：画入持久暂存画布，**失效点 flush 走 `drawImageRegion` 脏区增量提交**（XPainter.c:178-188；整幅 drawImage 仅为驱动无 region 通道时的回退——如 Vulkan 初期，:184-187 注释点名）。复核明确驳回勘察旧账「每批首一次全帧 readback」：现行代码批量路径零 readback（全文件 readback 仅帧末 :301、降级 :318、painter SYNC 钩子 :351、legacy :533 四处）。残留成本：软件命令↔GPU 原语每次交替仍强制一次 flush（脏区大时上传量可观，如全宽图表线）。

### 3.3 非快速路径命令清单（勘察锚，行号已按复核 +144~+159 漂移核对）

- drawLine：仅 Solid 笔+非圆头+轴对齐+无路径裁剪走 solidQuad 快路径；AA 斜线逐段覆盖图（#30）；其余斜线/虚线/圆头→批量局部提交（#31）。
- fillRect：复杂变换→整帧降级（#26）；region/路径裁剪/半透明 SYNC/图案画刷/RasterOp→局部提交。
- 渐变 fillRect：仅轴对齐线性+恒等/平移，斜向拒绝落逐像素软件（#31）；每次重建 256px LUT（#33）。
- drawImage：恒等+Source/SourceOver 快路径，但**任何非空 clipRegion 即拒绝**（#32，被低估）；drawImageRect（任意变换）恒局部提交。
- drawPolygon：solid+AA/GPU→覆盖图；渐变多边形→局部提交（已有 drawGradientAlpha 通道未接，#31）；非 AA 非 GPU→整帧降级。
- fillPath/drawPath：渐变+GPU→drawGradientAlpha（painterGpuFillPathGradient）；路径裁剪激活→局部提交。
- drawText：装饰线/复杂变换/多矩形裁剪→局部提交；outline 字形 GPU 失败→整帧降级（#26）。
- 裁剪总门 `painterGpuApplyStateClip`：路径裁剪 false、多矩形 region false（单矩形放行——与 #32 的 drawImage 不对称）。

### 3.4 XGUI_GPU_SYNC 与 XGPU_PROF

- **XGUI_GPU_SYNC 语义矛盾（#24，高危）**：painter 侧 `painterGpuSyncRequested`（XPainter.c:222-223）把 `"0"` 判为关；后端三处钩子（Backend.c:214-215/233-236/255-259）只判非空——`"0"` 被判为**开**。后果：设 0 时每原语整帧 upload+双重 readback（后端钩子+painter 钩子），比双向同步更糟，且批量快速路径照常启用；与「默认关闭，仅用于回归」的文档契约（XGpuRenderBackend.h:268-273、Backend.c:194-196）直接矛盾。另 begin_image 无条件注册 syncTarget（XPainter.c:6836-6837）。
- XGPU_PROF：开关缓存/计数器/5s 窗口聚合输出（Backend.c:31-109）；计数点仅 fillRect/drawImage/solidQuad/readback/present；四原语（drawImageUv/drawGradientAlpha/drawAlphaBitmap/drawGlyphAlpha）无计数，SYNC 钩子直调驱动 readback 绕过统计（#37）——「readback≈帧数」口径在 SYNC/AA 密集帧下失真。另存在驱动内 `XGPU_PROFILE`（gl.c present 分段计时，勘察锚 911-924），变量名不同勿混。

### 3.5 帧流转与每帧同步成本（按复核修正后口径）

- 窗口健康帧（全快速路径）：present 1 次、readback 0 次、全帧上传 0 次（首帧 1 次 initialImage）；但每张缓存图 drawImage 1 次整幅纹理重传（#4）、每 AA 段 1 次覆盖图上传+前置 flush（#30）、新字形图集上传。present 终点 SwapBuffers，vsync 已关（§1.1）。
- 含软件命令的帧：每「软件↔GPU 交替」一次脏区增量 flush；legacy 路径（SYNC 口径/目标依赖命令/setup 失败）每命令 ≥2 上传+1 读回（#28）。
- 离屏会话帧：每帧全帧上传（begin）+全帧 readback（帧末，XPainter.c:300-301）——缓存图逐帧付此成本（#27）。
- 整帧降级帧：一次全帧 readback+BitBlt 上屏（#26）。
- 代码自带历史口径（供数量级参考，本次未复测）：批量化前图表页 ~140 命令≈400ms/帧（XPainter.c:125-127 附近注释）；半透明面积序列曾 1900 次/帧 local-submit；渐变多边形逐像素 32ms/帧；面积填充逐像素 19ms/帧。

## 四、演示与基准（Test/XGuiDemo/xgui_window_demo.c 等）

### 4.1 --gpu 开关与挂点

`--gpu`→`XGpuRenderBackend_addRequestedOverride(true)`（xgui_window_demo.c:2950-2957），覆盖优先于 env。三挂点：① 绘制前 `XWidget_repaint`→`acquireForWindow`（XWidget.c:5744-5748）；② `XPainter_begin_image` 绑定会话（尺寸不符的离屏缓存图不借窗口 FBO，守卫在 XPainter.c:6804-6812——**窗口帧末不回读，正确性全靠此守卫**，隐患 #39）；③ 帧末 `!frameDegraded`→presentToWindow，否则 BitBlt+setFramePresented(false)（XWidget.c:5803-5818）。降级契约：`painterGpuFallback`→FBO readback 合并回后备 XImage+本帧整体 BitBlt。直通分支仅非 PARTIAL 模式编译，Windows/Linux 桌面默认 DIRECT（XGuiConfig.h:130-138）。

### 4.2 FPS 测量口径

`--benchmark N`（repaint）/`--benchmark-resize N`（960x720↔520x360 交替）/`--benchmark-full`（强制整帧重绘）/`--maximized`（:2934-2949/781-819）。`demo_runFrameBenchmark`（:833-889）：两轮 processEvents 预热→循环 repaint+processEvents→一次输出 fps/avg/longest。present 在计时窗内（PAINT 内同步执行）。基准模式不注册帧泵（main 仅非基准分支注册 :3065-3094），无定时器无 sleep；**vsync 风险已解除**（§1.1，勘察时序内并行编辑补上 `wglSwapIntervalEXT(0)`）。软件路径经 DIB blit 无刷新率钳制。

### 4.3 --autotest 与 GPU（已知缺口）

--autotest 断言全是控件状态/事件联动，与后端无关，GPU 下应照常 PASS。**缺口**：帧 5/7 截图直接读后备 XImage（:1141-1145/1160-1163），GPU 直通帧不回读该 XImage（#39）→ `--gpu` 下两张截图空白/陈旧；只有 `--screenshot` 路径有 GPU readback 分支（:1175-1209）。`/tmp/...` 为 POSIX 风格路径，Windows 下落盘当前盘根 \tmp，未验证可写。

### 4.4 xgui_gpu_test（XGuiGpu_Test）

未设 env 时自动置 `XGUI_RENDER_BACKEND=gpu`（xgui_gpu_test.c:54-55）。断言：vulkan 防假绿（请求 vulkan 实际驱动必须为 Vulkan，回退即 FAIL，:68-85——**Vulkan 修复后此测试是守门员**）；基础像素存在性；outline 文本不得整帧降级；字形图集 miss 恰上传一次/命中零上传/重置重传/语义；AA 多边形灰度边不降级；轴线/drawRect 零降级、渐变 LUT 端点精确。`XGPU_ON=0` 短路 skipped。ctest 名 XGuiGpu（CMakeLists.txt:441-443）。

### 4.5 基准页与推荐用法

页索引 0 按钮/1 选择/2 堆叠/3 输入/4 选项卡/5 条目视图/6 对话框/7 高级控件/8 图形效果（:1295-1311）。图表页 = 第 4 页内嵌 XTabWidget 第 20 页（折线/柱状/散点/面积/样条+饼图，:2694-2814）。推荐：`--benchmark 10 --page 4 --tab 20`（加 `--benchmark-full` 取全帧成本）、条目视图 `--page 5`、按钮页 `--page 0`、效果页 `--page 8`。`--page/--tab` 先于 show/基准循环执行。

### 4.6 Windows 窗口/后备存储与回归三套件

软件路径：CreateDIBSection 自顶向下 DIB+脏矩形同步+BitBlt/SetDIBitsToDevice（XPlatformBackingStore_win32.c:145/222/258/368/449）；demo 静态场景缓存（memcpy tile）在 GPU 光栅下绕过、直接 GPU 原语重画（:761-773）。回归三套件：XGuiRegression_Test（CMakeLists.txt:341-360，承载 XGuiRegression + XGuiRegressionGpu[ENV XGUI_RENDER_BACKEND=gpu;XGUI_GPU_SYNC=1]）、XLineControl_Acceptance_Test（:365-373）、XGuiGpu_Test（:433-441）；产物在 `${repo}/bin`（:66-68，Debug 后缀 d 只落库目标）；XGuiRegression/XGuiRegressionGpu 有 WORKING_DIRECTORY=bin，XGuiGpu 无。bin/ 实测在场（见附录 A.4 观测不一致），本轮未运行 ctest。

## 五、构建接线（CMakeLists.txt 等）

### 5.1 源收集与宏裁剪

`CMakeLists.txt:88` `file(GLOB_RECURSE SRC_FILE "Src/*.c")`、`:102` `DRIVE_FILE "Drive/*.c"`，**均无 CONFIGURE_DEPENDS**；全部 Drive 下 .c 无平台过滤编入 XinYueCS/XinYueC 两库（:103-104）。build.ninja 实证：GL 驱动与 Vulkan 驱动 .obj 在两库各有条目；Vulkan TU 的 DEFINES 无 `XINYUE_C_HAS_VULKAN` → 编译为空对象（4,142B）。Vulkan 驱动守卫 `XPLATFORMINTEGRATION_ON && XGPU_ON && defined(XINYUE_C_HAS_VULKAN)`（vulkan.c:19，无平台条件）；GL 驱动守卫仅前两者（gl.c:15）。XGPU_ON/XPLATFORMINTEGRATION_ON 不在缓存不在 DEFINES，兜底默认 1（XGuiConfig.h:83-84/90-91）。

### 5.2 XINYUE_C_HAS_VULKAN 现状（本机 x64-Debug）

CMakeCache.txt：`Vulkan_INCLUDE_DIR:PATH=...NOTFOUND`（:271）、`Vulkan_LIBRARY:FILEPATH=...NOTFOUND`（:274）、glslang/glslc NOTFOUND；build.ninja findstr `D_XINYUE_C_HAS_VULKAN` 零命中（本次汇编未重复该步，勘察实跑输出 NO_VULKAN_DEFINE_IN_BUILD_NINJA）；库依赖表无 Vulkan 项。`find_package(Vulkan QUIET)` 仅在 WIN32 分支（:270-276）——NOTFOUND 即静默跳过、GL 兜底。UNIX 分支 `find_library(vulkan)` 包在 `if(UNIX AND NOT APPLE)`（:117/:164-169）内，Windows 不走。

### 5.3 新增文件与工具链

新增/删除 .c 须重触发 glob：touch CMakeLists.txt 走 Ninja regenerate，或重跑 cmake（官方口径注释 :22-28）。工具链实测：CMake 3.26.4（VS2022 Enterprise 自带）、Ninja、cl.exe=MSVC 19.37.32824（工具集 14.37.32822）、Windows Kits 10.0.22000.0；build.ninja 的 INCLUDES 只含项目内目录，标准头靠 INCLUDE 环境变量→**裸调 ninja 不行，须先 vcvars**（`vcvars64.bat` 已实测输出 Environment initialized for: 'x64'）。命令行：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" &&
  "C:\...\CMake\bin\cmake.exe" -S D:\code\CMake\Container -B D:\code\CMake\Container\out\build\x64-Debug -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_BUILD_TYPE=Debug
call vcvars64.bat && cmake.exe --build D:\code\CMake\Container\out\build\x64-Debug --target XinYueC_Dynamic XGuiRegression_Test XGuiGpu_Test XLineControl_Acceptance_Test
```

调试入口 `.vs/launch.vs.json` → bin\XGuiWindowDemo_Test.exe。无 CMakePresets.json。

### 5.4 本机 Vulkan 组件盘点（勘察实测）

- Windows Kits 10.0.22000.0 的 um/shared **无 vulkan.h**；VS VC include 无；anaconda 无。**全盘无任何可链接的 vulkan-1.lib**（Windows Kits um/x64 只有 OpenGL32.Lib/GlU32.Lib）——这是启用 Vulkan 的唯一硬缺口。
- D:\Qt 有纯头文件副本：`D:\Qt\6.2.4\Src\qtwebengine\src\3rdparty\chromium\third_party\vulkan-deps\vulkan-headers\src\include\vulkan\` 下 vulkan.h+vulkan_core.h(605,459B, 2022-03-05)+vulkan_win32.h 等齐全；另有 swiftshader/glfw/skia 头副本。
- 运行时 loader 在：C:\Windows\System32\vulkan-1.dll（1,442,584B）+ vulkan-1-999-0-0-0.dll + vulkaninfo.exe。`vulkaninfo --summary` 实跑：GPU0/GPU1 均 **AMD Radeon (TM) Graphics**（API 1.3.217，AMD 专有驱动 22.20.27.09），**无 CPU/lavapipe 设备**——本机可验 Vulkan 真硬件路径，lavapipe 复现需另搭环境（swiftshader 仅头文件在场，无成品 icd）。

---

## 六、复核确认的隐患清单

缩写：`gl.c`=Drive/windows/Graphics/XGpuRenderDriver_gl.c；`vulkan.c`=Drive/Posix/Graphics/XGpuRenderDriver_vulkan.c；`win32.c`=Drive/windows/Graphics/XPlatformGraphics_win32.c；`Painter.c`=Src/XGui/Graphics/XPainter.c；`Backend.c`=Src/XGui/Graphics/XGpuRenderBackend.c。行号为复核锚（01:51 基线），**两驱动文件汇编时已再漂移（见卷首）**。

| # | 位置（复核锚） | 结论 | 严重度 | 复核状态 |
|---|---|---|---|---|
| 1 | gl.c:600-628 | 每原语一段完整管线状态：UseProgram+BindBuffer+64B BufferData+uniform+DrawArrays（textured 另加 ActiveTexture+BindTexture），present 同样逐次全套；无批次/实例化/状态去重 | 高 | 成立 |
| 2 | gl.c:589-598 | 每绘制一次 64B glBufferData 隐式孤儿化，无 persistent-mapped ring；**glBufferSubData 原位修法已被实测否定**（GPU 读中隐式同步，216→106 FPS，:589-591 注释） | 中 | 成立（修法更正） |
| 3 | gl.c:720-732 | 顶点属性逐 draw Enable/Pointer | 低 | **驳回**（01:51 版已改初始化一次性设定 :726-732） |
| 4 | gl.c:1185/1215 | drawImage/drawImageUv 每次调用整幅 CPU 重排+整幅 texImage2D 重传；无按图像身份的纹理缓存（仅字形图集常驻）；region 增量通道（:1235-1332）未惠及常规路径——同一缓存图每帧 N 次贴图即 N 次整幅上传 | 高 | 成立 |
| 5 | gl.c:381-395/410-423/457-474/1371-1385/1483-1496 | 上传 CPU 逐像素重排四处：ARGB32 R/B 交换、非 ARGB32 预乘慢路径、alpha 覆盖图与渐变/图集 coverage 1→4 通道扩展（4× 带宽）；桌面 GL_BGRA 直传与单通道格式可消 | 中 | 成立 |
| 6 | gl.c:1056-1104/1558+ | 同步 readback 无 PBO：整幅 glReadPixels 阻塞+CPU 翻转/交换；图集读回同病 | 中 | 成立 |
| 7 | gl.c:897-899/957/971/994-995 | present 四出口 doneCurrent 清追踪器，begin_frame/present 内 ensure 后又无条件 make_current——每帧 4 次上下文操作，2 次纯冗余（无同上下文短路） | 中 | 成立 |
| 8 | gl.c:976-1016 | blit 路径每帧 3 次 FBO 绑定；无 m_hasBlit 回退全屏 quad+2 次绑定 | 低 | 成立（影响微小，非优先） |
| 9 | win32.c:66-67/302-303 | 像素格式请求 depth24+stencil8 而驱动全程未用；无 wglCreateContextAttribs/WGL_ARB_pixel_format/WGL_EXT_colorspace（legacy 兼容上下文，仅 vsync 一处扩展） | 中 | 成立 |
| 10 | gl.c:1369-1395 | 渐变每次调用双上传：bbox 掩码 CPU 扩 4 通道后整幅 TexImage2D + 256×1 LUT 无条件 TexSubImage2D（:1391 注释自认「每次同步」） | 中 | 成立 |
| 11 | gl.c:131/673 | glGetError 仅声明+加载两处零调用；glFinish/glFlush 全树零匹配；getProcAddress 每会话一次 | 低 | 成立（排除项核实通过） |
| 12 | gl.c 全文件 | 勘察期间并行编辑三版（01:36/01:45/01:51），#2/#3 结论随版本反转；汇编时又见 1663 行@01:55 | 低 | 成立 |
| 13 | vulkan.c:71/574/1528 | **m_atlasSet 只分配从未 vkUpdateDescriptorSets 写入即被 record_quad 绑定采样**——首个文字绘制即 UB，lavapipe SIGSEGV 最强候选；修法：图集创建（:1892 附近）后补写描述符（一行） | 高 | 成立 |
| 14 | vulkan.c:1896/1939-1940/1972/1990-1991 | glyph_atlas_upload/readback 无 m_recording 守卫即 vkResetCommandBuffer/Submit/WaitIdle（返回值全不查）——帧中调用（Backend.c:820→845-859 已证实）丢弃已录命令并全程 UB | 高 | 成立 |
| 15 | vulkan.c:1583-1586/1605-1610/1651/1686/1702/1735 | draw_image 打断后命令保持未提交：同帧扩容销毁 staging（:1735 未提交拷贝悬空）、换尺寸销毁 source image/view（旧 quad 描述符仍引用+已提交 quad 采新视图的内容错误）——**同帧两次不同尺寸 drawImage 即触发** | 高 | 成立 |
| 16 | vulkan.c:1694-1706 | draw_image 打断五连（End/Submit/WaitIdle/Reset/Begin）返回值全不查，任一失败即静默非法状态；pending-reset UB 仅在 Submit 成功而 WaitIdle 出错时（子机制更正） | 中 | 成立（更正） |
| 17 | vulkan.c:1249-1251/1257-1270/1287-1296/1304-1305 | begin_frame acquire 成功后帧中失败三处直接 return false，不消费 m_imageReady 即重用信号量（违反 acquire 协议）；m_imageWaitConsumed 是死代码（从未置 true）；两处 fence 等待不查（device lost 静默继续） | 中 | 成立 |
| 18 | vulkan.c:740/674-692/786-793/909-911 | renderPass 与 swapchain 格式可失配——仅经 surface formats 查询失败盲目回退 BGRA8 的边缘路径（正常路径恒匹配，:740 为 no-op；触发路径更正） | 中 | 成立（更正） |
| 19 | vulkan.c:1318/2002-2006/2028 + Backend.c:362-374 | presentToWindow 桩恒 true + vkQueuePresentKHR 结果不查（OUT_OF_DATE 也报已上屏）；`.resize=NULL`→每窗口尺寸变化全量重建 instance/device（拖拽 resize 显著劣化） | 中 | 成立 |
| 20 | vulkan.c:1085/1249-1250/1302-1305/1340/1703/1940/1991 | 全同步提交：QueueWaitIdle 五处+begin 等上帧 fence 后才 acquire，CPU-GPU 完全串行无帧流水——FPS 上限被单帧延迟钳死，「远超软件」首要改造点 | 高 | 成立 |
| 21 | Backend.c:238-239 + vulkan.c:1433 | XGUI_GPU_SYNC 读回不判能力即调，Vulkan 窗口会话 readback 恒 false 被丢弃——SYNC 读回半边静默 no-op | 低 | 成立 |
| 22 | vulkan.c:1689-1768 + gl.c:1282-1286/1366-1369 | 每次 drawImage 一次「结束渲染通道+提交+等待+重建」全队列往返；GL 同场景仅帧内 TexSubImage2D 增量上传——设计级性能缺陷 | 高 | 成立 |
| 23 | vulkan.c:922-930/1461 | 顶点缓冲写满 record_quad 静默 false 无任何告警（容量更正：32768 顶点）；单命令语义可保（软件补画）但满容不可观测 | 低 | 成立（数字更正） |
| 24 | Painter.c:222-223/6836-6837 + Backend.c:214-215/233-236/255-259 | **XGUI_GPU_SYNC 的 "0" 语义两端相反**：painter 判 0 为关、后端钩子只查非空判 0 为开——设 0 时每原语 1 上传+2 读回且批量照常启用；文档「默认关闭」实为开启 | 高 | 成立 |
| 25 | Painter.c:124/178-188/301/318/351/533 | 「每新批首一次全帧 readback（800x600≈1.87MB）」 | 低 | **驳回**（已改脏区化方案，批量路径零 readback；原量化作废） |
| 26 | Painter.c:2926-2928/312-324/10607/13666-13667 + XWidget.c:5812-5815 | 整帧降级三触发点（fillRect 复杂变换、outline 字形绘制/度量两路径）各付一次全帧 readback+endFrame 后帧末 BitBlt；m_gpuActive 随即清除，每帧至多一次 | 中 | 成立 |
| 27 | Painter.c:300-301 + gl.c:931-936 | 控件缓存图离屏会话每帧 2 次全帧同步（begin 整帧上传无首帧门+帧末整帧 readback），合成到主画布再付第 3 次整幅源上传 | 中 | 成立 |
| 28 | Painter.c:495-548/525-545 + Backend.c 8 处钩子 | legacy 局部提交每命令：全帧临时 XImage alloc+整帧 uploadFrame+整帧 readback+整帧 drawImage=≥2 上传+1 读回；SYNC 钩子 8 处叠加，drawGlyphAlpha 单函数双上传钩子 | 中 | 成立 |
| 29 | Painter.c:178-188 + gl.c:1185/1215/1596→1648 | 批 flush 全帧 drawImage——**前半驳回**（flush 优先 drawImageRegion 脏区通道，整幅仅为无 region 驱动的回退）；**后半成立**（drawImage 每调用整幅重传、无身份缓存） | 中 | 部分成立 |
| 30 | Painter.c:5602-5689/10691-10704 | AA 填充每命令 malloc+CPU 覆盖光栅+强制 flush+整块覆盖图上传+1 draw call；AA 折线未按折线合并，逐段独立覆盖图 | 中 | 成立 |
| 31 | Painter.c:2771-2786/779-782/5117-5132/6024 | 三处可原生化缺口：斜线/虚线/圆头/零尺寸点走软件局部提交；斜向渐变被拒落逐像素（矩形转 4 顶点多边形）；渐变多边形不走已有 drawGradientAlpha 通道（注释自认） | 中 | 成立 |
| 32 | Painter.c:3496-3499 vs 2942-2947/6792-6795/12988-12992 | drawImage GPU 快路径拒绝**任何非空 clipRegion**（含单矩形），而 fillRect/裁剪门放行单矩形；begin_image 表面裁剪恒以单矩形播种→带裁剪 drawImage（缓存图贴图常态）系统性落软件局部提交 | 中 | 成立（被低估） |
| 33 | Painter.c:812-837 | painterGpuFillRectGradient 每次重建 256px LUT（init+256 次 setPixel+deinit）；fillPath 路径已栈上 lutRgba[256*4] 直填未跟进 | 低 | 成立 |
| 34 | gl.c:600-628/698/1151-1171/1424-1456/1504-1528 | GL 驱动无几何合批/实例化：fillRect/solidQuad/每字形各自独立全套状态+DrawArrays；仅加载 glDrawArrays，无 DrawElements/Instanced；每 solidQuad 前还强制 flush 检查 | 中 | 成立 |
| 35 | Painter.c:257-276 + Backend.c:360-374 + gl.c procs 表 | 离屏会话尺寸不符整体销毁重建（无 resize 尝试：GL 上下文+FBO+shader+字形图集全失效重传）；窗口侧 GL procs 表未填 `.resize`（字段在 XGpuRenderDriver.h:139）→GL 恒销毁重建 | 中 | 成立 |
| 36 | Painter.c:525 | legacy 局部提交热路径 `fprintf(stderr,"[helper] local-submit…")` 未用 XGPU_SESSION_TRACE 门控（对照 :262-263 已 gating） | 低 | 成立 |
| 37 | Backend.c:238-239 vs 936-941 | XGPU_PROF 口径缺口：四原语无计数、SYNC 钩子直调驱动 readback 绕过统计——「readback 次数（批量后应≈帧数）」设计口径失真 | 低 | 成立 |
| 38 | Painter.c:132-133/579-580/590 | g_gpuBatchCanvas 全帧 ARGB32（≈1.87MB@800x600）无 atexit/退出清理（唯一释放=尺寸不符重建）；跨 painter 换属主即冲批，批量收益打折 | 低 | 成立 |
| 39 | Painter.c:300-301/6804-6812 + XWidget.c:5803-5808 | 窗口直通帧末不回读：宿主 XImage 不含本帧 GPU 产出（autotest 截图缺口根源），正确性仅靠 begin_image 尺寸守卫；与 #24 叠加放大陈旧窗口风险 | 中 | 成立 |

统计（39 条）：**成立 36（其中 4 条含复核更正：#2 修法、#16 子机制、#18 触发路径、#23 容量数字）、部分成立 1（#29 前驳后半）、驳回 2（#3/#25）**。严重度：高 8（#1/#4/#13/#14/#15/#20/#22/#24）、中 19、低 12。

---

## 七、Windows Vulkan 移植最小改动清单

**结论：代码层面近乎零改动，硬前置是 (1) 一份可链接的 vulkan-1.lib + 头文件，(2) 先修隐患 #13-#17（帧路径 UB），否则任何平台都崩。**

| 项 | 现状 | 待办 |
|---|---|---|
| (a) surface 层 | **已零改动备齐**：VK_KHR_surface+VK_KHR_win32_surface（win32.c:135-147）、vkCreateWin32SurfaceKHR 含 HWND/HINSTANCE/IsWindow 校验（:149-175）、销毁（:177-182）；声明在 Src/XGui/Platform/XPlatformGraphics.h:104。驱动侧 vulkan.c:611 调 createVulkanWindowSurface、:829 调 vulkanWindowSurfaceExtensions | 无 |
| (b) 加载层 | 链接期导入依赖（§2.1），SDK 的 vulkan-1.lib 由 CMake Vulkan::Vulkan 接上即可 | 备好 vulkan-1.lib+头文件（本机缺，见下「本机落地」）；可选加固 `/DELAYLOAD:vulkan-1.dll`（现有 GetModuleHandleW 探测 win32.c:184-188 形同虚设） |
| (c) CMake | 已接好：find_package(Vulkan QUIET)（:270-280）命中即 define+链 Vulkan::Vulkan | 仅建议更新 :267 过时注释（「Vulkan 仅由 XPlatformGraphics_win32.c 可选后端使用」——实际 define 会激活整个 vulkan 驱动） |
| (d) 文件位置 | 驱动文件留 Drive/Posix/Graphics/ 即可（跨平台核心、无平台头，GLOB 全平台编译，Windows 空 .obj 已在）；Windows surface 代码已合规在 Drive/windows/；shaders 头零改动 | 无 |
| 前置 | 帧路径 UB（#13-#17：atlasSet 描述符、图集守卫、draw_image 打断序列、信号量协议、格式边缘路径） | **必须在 Windows 真跑前修复**，否则 lavapipe 的 SIGSEGV 原样复现；xgui_gpu_test.c:68-85 的 vulkan 防假绿断言为守门 |

**本机落地路径（实测约束）**：头文件可用 Qt 副本（`D:\Qt\6.2.4\Src\qtwebengine\...\vulkan-headers\src\include\vulkan\`，vulkan_core.h 2022-03-05≈1.3.x 头，驱动用的核心 API 均覆盖——精确可用性须以实际编译为准，未验证）；运行时 loader 与 AMD 真硬件驱动已在（System32\vulkan-1.dll + 两块 AMD Radeon，vulkaninfo 实跑）；**唯独缺 vulkan-1.lib**——需装 Vulkan SDK，或从 dll 生成导入库（`dumpbin /exports`+`lib /def`，未验证），或临时改动态加载（非最小改动）。`lavapipe` 复现环境本机没有（vulkaninfo 无 CPU 设备；Qt swiftshader 目录只有头文件）——SIGSEGV 修复的验证需真硬件+VK validation layer 兜底（XGui.md:1238 口径），或另装 lavapipe ICD。

---

## 八、今晚优化候选排序（按预期收益）

排序依据：今晚双目标（GL/Vulkan 远超软件 + 修复 lavapipe SIGSEGV/回退契约）→ 先正确性后性能；性能项按「消除的每帧字节数/同步往返数 × 触达页面广度」估收益。收益列为定性预估（依据=隐患表编号与 §3.5 数量级），**今晚动工前应先跑 §4.5 基准取基线**。

| 优先 | 候选 | 位置 | 预期收益 | 依据 |
|---|---|---|---|---|
| P0-1 | Vulkan 帧路径 UB 五连修（atlasSet 补描述符一行；图集 upload/readback 独立传输命令缓冲或 m_recording 守卫；draw_image 打断改独立暂存 cb+全查返回值；acquire 失败消费信号量/跳 present；格式回退路径校验） | vulkan.c:1892/1896/1972/1694-1706/1257-1270/786-793 | 解锁 Vulkan 后端本身（lavapipe SIGSEGV 修复=今晚既定目标）；无此一切 Vulkan 性能项免谈 | #13-#18，XGui.md:1238/1381 |
| P0-2 | XGUI_GPU_SYNC `"0"` 语义统一（后端三钩子对齐 painter 判 0 为关） | Backend.c:214-215/233-236/255-259 | 一行级修复；否则 XGuiRegressionGpu（ENV XGUI_GPU_SYNC=1 幸为 "1" 不受累）口径与调试体验全错 | #24 |
| P1-1 | Vulkan 帧流水化：消除 QueueWaitIdle 五处与 begin 前 fence 等待，submit/present 异步化（fence+双信号量，≥2 帧在途） | vulkan.c:1085/1302-1305/1340/1703/1940/1991 | Vulkan 吞吐上限从「单帧延迟钳死」解放，收益上限最大（数倍级） | #20 |
| P1-2 | Vulkan drawImage 帧中打断消除：staging 上传改录进独立传输 cb/帧首 flush 队列，对齐 GL 帧内增量语义 | vulkan.c:1689-1768 | 图密页面（图表/条目视图）每图一次全队列往返→0 次往返 | #22 |
| P1-3 | Vulkan resize 接口实现（swapchain 重建，不动 instance/device），顺带 present 返回值上抛触发重建 | vulkan.c:2028/:1318 + Backend.c:362-374 | 拖拽 resize 从全量重建（百 ms 级）→swapchain 级 | #19 |
| P2-1 | GL drawImage 纹理缓存（按图像身份 key→常驻纹理+脏标记失效；可借 region 通道存储跟踪） | gl.c:1185/1215/336-438 | 图表/条目页每帧 N 次整幅重传→0；与 #27/#29 叠乘，GL 最大单项 | #4 |
| P2-2 | GL 几何合批/实例化：同 program 同纹理的 quad 聚合成一次 BufferData+DrawArrays（CPU 侧顶点数组累积，帧末/失效点提交） | gl.c:600-628/1151-1171/1424-1456/1504-1528 | 每原语 8+ 次 GL 调用→1 次；百条网格线场景 draw call 数量级下降 | #1/#34 |
| P2-3 | GL 冗余上下文操作消除：make_current 同上下文短路 + present 不清追踪器（或 begin 不重复 make） | gl.c:897-899/957/971 + 210-230 | 每帧 4 次 wglMakeCurrent→0-1 次；改动极小 | #7 |
| P2-4 | GL persistent-mapped 顶点环（持久映射+ fences 轮转；**勿用 glBufferSubData 原位**，已实测负收益 216→106 FPS） | gl.c:589-598 | 消每 quad 一次 orphaned allocation | #2 |
| P2-5 | GL 上传格式优化：ARGB32 用 GL_BGRA 直传消 R/B 交换；alpha/coverage 用单通道格式（GL_R8+swizzle+shader 微调）消三处 4× 扩展 | gl.c:381-395/457-474/1371-1385/1483-1496 | 上传 CPU 带宽降 2-4× | #5/#10 |
| P3-1 | drawImage 快路径放行单矩形 clipRegion（对齐 fillRect 门；表面裁剪/单矩形 setClipRect 下的缓存图贴图回 GPU 通道） | Painter.c:3496-3499 | 带裁剪贴图从软件局部提交回 GPU；缓存图常态场景系统性受益 | #32 |
| P3-2 | 缓存图离屏会话增量化：begin 首帧门（照抄窗口分支）+ 帧末改脏区 readback 或驻留不回读 | gl.c:931-936 + Painter.c:300-301 | 控件缓存图每帧 2 次全帧同步→接近 0 | #27/#35 |
| P3-3 | GL/离屏会话 resize 支持（procs 填 `.resize`；FBO+纹理重建不动上下文，图集保留） | gl.c procs 表 + Backend.c:362-368 | 尺寸交替基准（--benchmark-resize）与缓存图多变尺寸场景免上下文重建 | #35 |
| P3-4 | AA 折线合并与覆盖图复用（painterDrawPolyLineFloat 折线级一次覆盖图；覆盖图缓冲复用免每命令 malloc） | Painter.c:5602-5689/10691-10704 | 折线密集页（图表序列/坐标轴）AA 段成本合并 | #30 |
| P3-5 | 渐变原生化补齐：斜向线性渐变进 painterGpuFillRectGradient；渐变多边形接 drawGradientAlpha 通道 | Painter.c:779-782/5117-5132/4723-4810 | 面积系列/渐变填充从逐像素软件回 GPU（历史口径 32ms/帧级） | #31/#33 |
| P4-1 | readback PBO 异步化（离屏/降级/图集路径；批量脏区化后 readback 已大减，收益随之收窄） | gl.c:1056-1104/1558 | 延迟隐藏 | #6 |
| P4-2 | 卫生批：local-submit fprintf 门控（#36）、XGPU_PROF 补四原语计数与 SYNC 直调统计（#37）、渐变 LUT 栈上化（#33）、g_gpuBatchCanvas atexit 清理（#38）、顶点满告警与 present 结果上抛（#23/#19）、CMakeLists.txt:267 注释更新 | 各处 | 可观测性与泄漏收口，低风险顺手项 | #23/#33/#36/#37/#38 |

**建议今晚动工顺序**：P0-1 → P0-2 → 用 `--benchmark-full --page 4 --tab 20`（GPU 口径）取 GL 基线 → P2-1 → P2-2 → P2-3（验证 xgui_gpu_test 全绿+XGuiRegression 双口径）→ Vulkan 接入 Windows 链接后按 P1-1/1-2 推进。每步过三道闸：xgui_gpu_test 防假绿断言、XGuiRegression/XGuiRegressionGpu、demo --autotest（注意其 GPU 截图缺口 #39，勿以帧 5/7 截图为 GPU 正确性证据）。

---

## 附录 A：勘误与观测不一致记录

1. **任务书勘误**：「Vulkan 驱动只在 Drive/Posix」仅是文件位置，接线层面该文件已编入 Windows 构建（GLOB 无平台过滤，空 TU 实证）——Windows GL-only 的真实原因是本机缺 Vulkan SDK（§5.2）。
2. **vsync 时序**：演示路勘察称全树无 SwapInterval；GL 路/复核证实并行编辑已加入 `wglSwapIntervalEXT(0)`（gl.c:873 全树唯一命中，汇编时 findstr 复核通过）。以现状为准：**vsync 已显式关闭**，`--benchmark` 无刷新率钳制之忧。
3. **批首 readback 旧账作废**：复核驳回「每新批首一次全帧 readback」——批量提交已脏区化（XPainter.c:178-188），相关 1.87MB/次量化与「交替对 2 次全帧同步」口径不再成立（§3.2、#25/#29）。
4. **bin/ 观测不一致**：演示路勘察称 bin/ 仅 Container.exe 与 XGuiWindowDemo_Test.exe；构建接线路 `dir /b` 实测 8 项（含 XGuiRegression_Test.exe/XLineControl_Acceptance_Test.exe/XGuiGpu_Test.exe/XinYueCd.dll 等）。以构建接线路（清单更完整、含 CTestTestfile.cmake 佐证）为准，动工前以一次 `dir /b bin` 复核。
5. **行号漂移警示**：两驱动文件复核基线（01:51）后汇编时仍再漂移（GL 1611→1663@01:55、Vulkan→2114@02:05）；本次抽查实证锚点位移（vulkan.c `m_atlasSet` 1484→1528、gl.c procs 表 drawImageRegion 1596→1648），gl.c:873 wglSwapIntervalEXT 与 win32.c 343 行态未变。XGpuRenderBackend.c（01:37）与 XPainter.c（01:40）与勘察时点一致。
6. **未做事项**：本轮未构建、未运行任何测试或基准；vulkaninfo 与 vcvars64 为构建接线路实跑，其余检查均为只读检索。XGui.md:1238/1381 的 lavapipe SIGSEGV 记录为文档佐证，机制结论以本轮代码复核为准（真硬件+VK validation 复核仍未做）。
