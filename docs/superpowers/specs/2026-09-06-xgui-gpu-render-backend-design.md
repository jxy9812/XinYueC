# XGui GPU 渲染后端设计（阶段 1：架构 + 最小原语）

## 背景与目标

XPainter 目前是纯软件光栅（直接写 XImage 像素）。目标：新增 **GPU 光栅化后端**（方案 B），同时满足：

- 软件渲染**保留**（默认后端，永不裁剪）；
- 软/GPU **可裁剪、可共存**（运行时选择）；
- GPU **不可用自动回退**软件渲染（创建失败/无驱动/显式禁用）；
- 嵌入式无 GPU：`XGPU_ON=0` **整体裁剪 GPU 代码**（不新增第二个开关）；
- 嵌入式有 GPU：走同一后端接口支持 GPU（GLES）。

## 已确认决策

1. 只用 `XGPU_ON` 一个编译开关（不引入 `XPAINTER_GPU_ON`）。
2. GPU 后端 OpenGL 与 Vulkan 都支持，**先实现 OpenGL**（GLES 可复用），Vulkan 留后。
3. 阶段 1 文本走 **CPU 字形 → 纹理上传**（混合）；纯 GPU 字形图集放阶段 3。

## 现状探索结论

- XPainter 已有“绘制引擎回调”字段（`m_drawLine/m_fillRect/m_drawImage/m_save/m_restore/m_drawShape/...`），`begin_image`/`begin_picture` 设置不同回调（软件光栅后端 / 指令录制后端）。
- 但**只有 drawLine 真正走回调分发**；`fillRect/drawText/drawImage` 等是硬编码软件实现，不走 `m_fillRect` 等回调（这些回调目前无调用点）。→ GPU 后端不能只“换回调”，需在绘制函数本体加后端分发。
- GL 上下文来源：`XPlatformGraphicsDriver_createOpenGL` 必须绑定窗口；`XPlatformOffscreenSurface` 提供可 makeCurrent 的离屏 GL 上下文，但**无 getProcAddress**（拿不到 GL 函数指针）→ 阶段 1 需为其补充 proc 能力（用既有 `openGLProcAddress` Driver 链）。
- 高层形状/多边形/路径回调在软件后端为 NULL，XPainter 自动分解为基础指令；GPU 后端可用同样机制（先只实现基础原语，复杂形状后续阶段替换）。

## 架构

```
XPainter（公共 API 不变）
  └─ 渲染后端选择（Raster / Gpu；XGPU_ON 编译裁剪）
       ├─ RasterBackend：现状软件光栅（XImage），默认、永不裁剪
       └─ GpuBackend（阶段 1：OpenGL）：
            · 进程级共享离屏 GL 会话（FBO + 纹理 + shader）
            · 原语：clear / fillRect（纯色+alpha）/ drawImage（纹理贴图）/
                    文本（CPU 字形→纹理上传）
            · 未实现原语：录制回放 → 软件整帧兜底（正确性保证）
            · end：GPU 帧 readback 到绑定 XImage（对上层透明）
       · 创建失败/无驱动/显式禁用 → 自动回退 RasterBackend
```

阶段 1 采用 **FBO → readback → 现有上屏**（对 XWidget/XBackingStore 完全透明，双后端可随时切换、软件结果可对比）。GPU 收益在复杂绘制/大窗口显现；**简单小窗口帧数未必更高**（readback 开销 + 显示器 vsync 限制）。直通合成上屏（去掉 readback/BitBlt）作为阶段 2 的提交点优化。

## 阶段 1 范围（本设计交付）

1. `XPlatformOffscreenSurface` 补充 `getProcAddress`（经 Driver 链）。
2. 新文件 `Src/XGui/Graphics/XGpuRenderBackend.c/.h`：
   - GL 函数指针封装（经 `getProcAddress`，不依赖平台 GL 头，嵌入式 GLES 可用）；
   - GPU 会话（FBO/纹理/顶点缓冲/两个 shader：纯色 + 纹理）；
   - 原语：clear、fillRect、drawImage（阶段 1 图形）、文本（CPU 字形→纹理）；
   - readback 到 XImage；会话销毁。
3. XPainter 集成：
   - 后端字段（Raster/Gpu）+ GPU 会话指针；
   - `begin_image` 按全局配置选择后端；`end` 时 GPU 会话 readback 并销毁；
   - `fillRect/drawImage/drawText` 加 GPU 分支；
   - 其余原语在 GPU 模式下进入“录制 + 软件重放”兜底（功能一致）；
   - GPU 不可用 → 自动回退 Raster。
4. 测试：
   - GPU 可用性探测 + 回退路径；
   - 软/GPU 渲染结果一致性（fillRect / 文本 / 图像 像素对比）；
   - `XGPU_ON=0` 变体构建回归（GPU 代码不编译）。

## 后续阶段（不在本设计内）

- 阶段 2：线段/折线/椭圆/弧/多边形（CPU 三角化 → GPU 填充，仿 Qt）；直通合成上屏（XBackingStore 提交点优化）。
- 阶段 3：路径/渐变/渲染提示/混合模式全套 + 纯 GPU 字形图集。
- 阶段 4（可选）：Vulkan 后端。

## 风险与取舍

- readback 对简单场景帧数不升反降是预期的；阶段 1 目标是“架构就位 + GPU 链路打通 + 一致性正确”，性能收益随阶段推进显现。
- 双后端渲染一致性是最大维护成本，每阶段以软件结果为准跑对比测试。
- GL 函数经 proc 手动封装，需覆盖阶段 1 原语所需的最小 GL 子集。

## 实施记录（已完成）

- 后端选择：`XGUI_RENDER_BACKEND=gpu`（或 `XGPU_BACKEND=opengl/1`）运行时启用 GPU 光栅后端；默认软件。查询 API：`XPainterRasterBackend XPainter_rasterBackend()`。
- GPU 会话：`XGpuRenderBackend`（离屏 GL 上下文 + RGBA8 FBO + GLES2 兼容 shader 管线），fillRect/drawImage/drawAlphaBitmap（CPU 字形→纹理）/clear/单矩形 scissor/预乘混合/帧末 readback。
- 回退：GPU 不可用/会话失败 → 自动软件；命令级快速路径不满足 → 整帧降级软件（已画 FBO 先合并）。
- 性能实测（x64-Debug，demo 520×360）：
  - 软件：4912.6 FPS
  - GPU：193.9 FPS（修复前 13.1 FPS）
  - 两项关键修复：readback 绕开逐像素 XImage_setPixel（187K 次/帧）；图像上传对 ARGB32 预乘源走 R/B 交换直拷（避开逐像素 XImage_pixel/mul255）。
- 验证矩阵：回归 DIRECT/FULL/PARTIAL/ASan 全绿；XGPU_ON=0 裁剪回归全绿；GPU 冒烟（xgui_gpu_test.c，CMake target XGuiGpu_Test）像素断言通过。
- 已知限制：readback 模型下简单小窗口 GPU 帧率低于纯软件（预期，见设计正文）；文本字形每帧 CPU 光栅 + 上传（阶段 3 字形图集优化）。
