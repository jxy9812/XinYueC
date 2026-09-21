# 渲染管线架构

> 归属：Src/XGui/Graphics（XPainter/XRenderKernel/XImage 编解码）。
> 配套批次记录：history/2026-09-19-perf-rounds.md、history/2026-09-20-dualsource-rgb565.md。

## 1. 总体数据流

```
控件 update()/repaint()
  → 脏区入队（XWidget_addDirty/Region，控件矩形裁剪）
  → 事件循环取出 PAINT → XWidget_paintTree(top, region)
      ├─ 图形效果钩子（XGraphicsEffect_drawWidget，命中即整子树接管）
      ├─ 逐控件 paintEvent → XPainter 绘制指令
      └─ 落入后备存储（XPlatformBackingStore：DIRECT/FULL/PARTIAL tile）
  → present：DIRECT 拷贝 / PARTIAL 逐 tile（+攒批合并）/ FULL 整帧
```

- 后备存储格式选择器：`XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16`（默认 0
  =ARGB32；=1 切 RGB565，present 走 `xpwn_copyRect16` 直拷，非 16 位
  视觉返回 false 绝不误读）。
- PARTIAL tile 仅分配 min(w,160)×min(h,80) 逐片即绘即上屏；攒批
  （XGUI_BACKINGSTORE_TILE_BATCHING_ON，默认开）合并相邻片减少
  present 次数，16ms 帧界强制收批。

## 2. 渲染内核表（XRenderKernel）

- `XRenderKernelOps` 七原语：fillSpanOpaque / fillSpanBlend / blitSpan /
  blendSpan / glyphMaskSpan / storePrem（+ TARGETS 无关）。约定：行基址+
  像素列；颜色恒为预乘 ARGB32 规范色；未注册格式 forFormat 返回 NULL →
  调用方回退逐像素路径（零回归保险丝）。
- 内置表：RGB565（混合口径 `(a*b+127)/255` 逐字节对齐，勿用 `>>8`）。
  目验方法与结论（565 位复制展开合法性校验，0.00% 非法）见
  history/2026-09-20 §23.6。
- 扩展：NEON/Helium/DMA2D 变体经 `XRenderKernel_register` 覆盖注入，
  热点三内核（fill/copy/blit）优先（需板级验证，待办）。

## 3. XPainter 质量特性（对标 Qt 逐项）

| 特性 | 实现 | 关键点 |
|---|---|---|
| 线条 AA | 线段法线偏移构成笔宽四边形 → 既有 4x4 覆盖通道 | 轴向线/hint 关闭逐字节零回归；opacity/composition 复用状态管线 |
| 几何描边器 | 段法线偏移对接四边形 + 角平分线裁剪；Bevel/Miter(miterLimit)/Round join；三 cap | 宽笔（>1 设备像素）接管；1px 走原管线零回归 |
| Winding 填充 | XPainterPath_setFillRule + 非零环绕计数（painterBuildFillSpans） | 默认 OddEven 零回归 |
| 精确路径裁剪 | 路径光栅化为 8 位掩码 + putPixel 门控 + AA 乘法 | 矩形路径退化 clipRect 逐位一致 |
| 笔宽随变换缩放 | scale=(\|M·(1,0)\|+\|M·(0,1)\|)/2 | 宽度 0 = cosmetic 恒 1px |
| 双线性采样 | SmoothPixmapTransform hint；2x2 邻域加权 | 1:1 blit memcpy 快路径零回归 |
| 虚线 | 节距=笔宽倍数；描边器内沿轮廓连续推进 | CustomDash 空 pattern=实线（对标 Qt） |
| 动态容量 | 折线/多边形/文本行动态缓冲（无静默截断） | OOM 路径零副作用 |

合成模式 38 种（软件全实现；GPU 仅 Source/SourceOver，其余局部软件
提交）；opacity 全路径统一应用；save/restore 全状态快照（含 Picture
录制）；裁剪含 clipRect/clipRegion/clipPath/表面裁剪四级。

## 4. 已知边界

- 文本 AA 在旋转/缩放变换下退化为硬边（恒等/平移保持 AA）。
- strokePath 独立描边引擎未建（描边经折线落地）；180° 折返拐角无补片；
  miterLimit 数值 API 已有、Round 为 6/8 段近似。
- 3 个渲染 hint（Lossless/VerticalSubpixel/NonCosmeticPatterns）存储
  无消费。
- XPicture 录制流不持久化路径 fillRule；MULTIPLE 支持服务但不进
  TARGETS 广播。
