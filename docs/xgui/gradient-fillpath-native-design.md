# 渐变 fillPath 原生化设计草案（评审稿）

> 2026-09-23 设计代理产出 · 方向 B 后续大块实现前评审材料（未立项）
> 结论先行：推荐"覆盖图×LUT 双纹理单遍着色器"方案（新增 1 个微型片段程序+drawGradientAlpha 原语）；候选 b（CPU 合成走 drawImage）作天然回退层；stencil 方案排除。工作量 7~9 人日。

## 一、现状与缺口

**已有 GPU 通道**（Drive/windows/Graphics/XGpuRenderDriver_gl.c + Src/XGui/Graphics/XGpuRenderBackend.c 函数表）：GL 驱动仅两个着色器程序——solid（gl_FragColor=u_color）与 texture（texture2D(u_texture,v_texcoord)*u_modulate）；原语含 fillRect、drawImage、drawImageUv（任意 UV 子矩形）、drawAlphaBitmap（CPU 预乘 α×color→RGBA 后上传 m_sourceTexture）、drawGlyphAlpha（图集，RGBA 四通道均为覆盖度约定）、drawSolidQuad。未加载任何 stencil GL 函数。

**渐变现状**：fillRect_2 的轴对齐线性渐变已走 painterGpuFillRectGradient（XPainter.c，fillRect_2 调用）——256 级 LUT 小纹理 + drawImageUv 一次提交，门控为轴对齐/恒等或纯平移变换/Source 系合成/单矩形裁剪/XGUI_GPU_SYNC 关闭。**缺口在路径与多边形**：

- 渐变画刷多边形：painterScanFillDevice 显式跳过覆盖图分支后走软件局部提交（经 painterGpuSubmitSoftwareCommand 全帧暂存画布批量通道）——正确但每批一次全帧读回+全帧上传，是图表页 series 段 20~42ms 的主要构成（面积系列 drawPolygon、饼图洞渐变 drawEllipse，见 XChartView.c）。
- 渐变画刷 fillPath：painterFillPathContours 中 `if (device && !gradient)` 把渐变排除在"覆盖图→drawAlphaBitmap"通道外，落入逐像素扫描线 + 逆矩阵 + putPixel 直写 m_image（GPU 会话帧内容在 FBO）——既是性能缺口也是正确性隐患。
- 字形路径已有成熟"轮廓→覆盖图"通道：painterGlyphContoursAlphaCoverage（4×4 面积子采样、支持 OddEven/Winding），painterFillContoursAntialiased GPU 会话经 drawAlphaBitmap 提交——但只支持单色 ink。

## 二、候选方案对比

| 维度 | a. 覆盖图×LUT 双纹理（单遍双采样着色器） | b. CPU 合成渐变×覆盖→RGBA 走 drawImage | c. stencil 模板缓冲 |
|---|---|---|---|
| 着色器改动 | 新增 1 个片段程序：texture2D(u_lut,uv_lut)*texture2D(u_mask,v_texcoord)；顶点程序复用；约 15 行 | 零 | 零（但需新增 stencil GL 函数加载与 FBO 格式改造） |
| llvmpipe/真机性能 | 覆盖图上传 w×h 字节 + 1KB LUT + 1 次 draw；填充全在 GPU | CPU 逐像素取色 O(bbox) + 4× 上传带宽；llvmpipe 上传为主瓶颈 | 两次几何 pass，且任意多边形按填充规则上模板需新写三角化器，填充 2~3 遍 |
| 与 drawGlyphAlpha 通道复用度 | 覆盖生成完全复用 painterGlyphContoursAlphaCoverage；覆盖图 RGBA 四通道展开与图集同一约定；LUT 构建复用 fillRect 渐变路径代码 | 复用 drawImage 提交，但合成循环是新代码 | 无复用 |
| 回退安全性 | 任一门控/着色器链接失败返回 false → 既有软件局部提交原样保留；GLES 2.0 双采样器普遍支持 | 最高（无新硬件依赖） | 最差：请求的 FBO 配置可能无 stencil，llvmpipe 行为差异大，回退面大 |

结论：**推荐 a**，并把 b 作为 a 的天然回退层（即现有批量局部提交，未来可收窄为 bbox 级 b）。a 的变体"两遍合成"（先画 LUT 到暂存纹理，再以 glBlendFunc(GL_ZERO,GL_SRC_COLOR) 乘覆盖）可免新着色器，但需新增暂存 FBO/纹理、3 次 draw 与混合态切换，改动面反而大于一个微型程序，不取。c 因仓库无三角化器且 FBO 格式侵入，明确排除。

## 三、推荐方案组件清单

1. **着色器/驱动**（XGpuRenderDriver_gl.c）：新增 xgld_create_program 的第三片段程序（双采样器相乘，LUT UV 由 u_lutAxis（水平/垂直）从 v_texcoord 分量导出，天然支持日后斜向渐变）；新增 proc drawGradientAlpha：覆盖图按图集约定展开为四通道 RGBA 上传 m_sourceTexture，LUT（256×1 或 1×256 预乘 ARGB32）上传新增的 m_gradientLutTexture，TEXTURE0/TEXTURE1 分别绑定后单次 TRIANGLE_STRIP。glActiveTexture/glUniform1i 均已在加载表中，无新 GL 依赖。
2. **Backend 新原语**（XGpuRenderBackend.h/.c + XGpuRenderDriver.h 函数表）：
   bool XGpuRenderBackend_drawGradientAlpha(backend, const uint8_t* coverage, int w, int h, int stride, int x, int y, const uint32_t* lutPremul, bool verticalAxis, float opacity, bool sourceOver)；内部按惯例接入 xgpu_sync_upload/readback_if_requested。
3. **Painter 侧接入与门控**（XPainter.c）：把 fillRect 渐变路径的 LUT 构建抽为 painterGpuBuildGradientLut 复用。新入口 painterGpuFillPathGradient（覆盖图光栅化 + 调新原语），接入两处：painterScanFillDevice 渐变分支与 painterFillPathContours（改为渐变+GPU 也走覆盖图通道，顺带修复直写丢失边界）。门控条件：GPU 会话激活；画刷为渐变且初期仅 Linear；合成模式 Source/SourceOver；变换恒等或纯平移；无路径裁剪（scissor 单矩形裁剪照常）；painterGpuSyncRequested() 为否（与批量层及 fillRect 快速路径口径一致）；覆盖图尺寸护栏复用既有逻辑。subdiv 口径不变：AA 提示 4、否则 1。透明度折入 LUT 预乘项（同 fillRect 路径）。
4. **回退路径**：任一门控不满足或原语返回 false → 多边形回 painterGpuPolyCommand 局部提交、fillPath 回既有扫描线行为；纯软件构建（XGPU_ON=0）全部新代码被守卫裁剪，零影响。

## 四、工作量与验证

**工作量（合计 7~9 人日）**：驱动着色器+proc+双纹理单元 1.5~2；backend 原语与 SYNC 接线 1；painter 门控/LUT 抽取/两处接入 2；测试用例 1.5~2；基准与真机验证 1；评审与缓冲 1。

**验证方案**：
- **回归锁**（xgui_gpu_test.c，CMake 目标 XGuiGpu_Test）：① 渐变 fillPath 三角形+内轮廓洞：SYNC=1 下快速路径关闭走软件（现有逐位契约不破）；SYNC=0 下 GPU 帧断言——pad 区像素==端点停止色、洞区保持背景、边界 AA 覆盖存在。② **像素对照双口径**：软件帧 vs GPU 帧逐像素比较，掩码结构（覆盖/非覆盖）逐位一致，渐变色允许 ≤2/255 通道差（256 级 LUT 量化，与既有 fillRect 快速路径同契约）；图表页改前/改后截图按同口径容差对照。
- **性能**：XGUI_GPU_PROF 五段剖面，图表页最大化全帧重绘，目标 series 段 20~42ms 降至个位数毫秒；sweep_fps2 基线对照不回退。
- **安全网**：三后端（software/OpenGL/Vulkan）XGuiRegression 全绿；无 GL 环境自动回退路径覆盖。
