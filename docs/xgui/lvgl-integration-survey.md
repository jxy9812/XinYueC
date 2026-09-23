# XGui × LVGL 融合调研

> 2026-09-23 调研代理产出 · 决策材料（未立项，供项目所有者定夺）
> 结论先行：推荐形态 A（思想吸收、零代码依赖）+ 形态 D（板级对标基线）；不建议 B（双栈并存）与 C（借渲染层）。

## 一、现状摘要

**XGui 渲染侧已有可对接面**（均见 XGui.md 与 Src/XGui/Graphics/）：

- **软件光栅**：XPainter（指令流，38 种合成模式、AA/描边器/双线性）→ XRenderKernel 格式内核表（七原语 span 契约，文档自述"对标 Skia blitter + LVGL 组织"，已内置 RGB565/888/555/RGB32/Gray8 五张表，未注册格式回退逐像素是零回归保险丝）；扩展加速器（NEON/DMA2D）= 注册一张表，不碰 painter。
- **上屏链**：XBackingStore DIRECT/FULL/PARTIAL 三模式 + tile 攒批（160×80 分片即绘即上屏，16ms 帧界收批）——这本身就是 LVGL 式"小 buffer 局部刷新"思想的自有实现；静态保留层（§8.0d，XWidget_setContentRetained）已作为"Qt+LVGL 融合首批"落地。
- **GPU 直通**：XGpuRenderBackend + XGpuRenderDriverProcs 函数表（sessionCreate/beginFrame/presentToWindow/readback/drawGlyphAtlas 等，OpenGL 实装、Vulkan 骨架）；§8.0g23 批量提交第一步已落地（readback 35.5→6.75 次/帧）。
- **嵌入式面**：XImageFormat 全套 30+ 格式；fbdev 链已打通（XPlatformFramebuffer_posix.c → XPlatformDisplayDriverOps probe/格式协商 → 逐矩形直写+cacheSync+pan，565 直写零拷贝），但真机未验证（§8.0c4 "待板：面板色彩/撕裂目验"）。
- **规模**：全库约 23.4 万行 C，Widget 目录 81 头文件/77 实现（Qt Widgets 级：模型/视图、富文本、对话框族、Dock/MDI 全有）。§8.2 登记项为"Qt+LVGL 融合优化专项｜大｜内核表已按 LVGL 组织，续：嵌入式显存/局部刷新策略"。

**LVGL v8/v9 对接面**（公开知识）：display 的 flush_cb 区域回调 + draw buffer（可配 1/4 屏、direct_mode、full_refresh）；indev read_cb 轮询输入（v9 内置 linux fbdev+evdev 驱动）；widget 树 + 样式/动画/Flex-Grid 布局；自带软件 draw unit（lv_draw_sw）。LVGL 非线程安全，裸机主循环 `while(1)+lv_timer_handler`，与 XGui 事件循环模型不同构。

## 二、融合形态对比

| | A. 思想吸收（零依赖，延续现状） | B. 双栈并存（LVGL 独立跑，共用平台层） | C. 借用 LVGL 渲染层（lv_draw_sw 塞进 XGui 管线） |
|---|---|---|---|
| **对接点** | XGui 侧：收口"嵌入式显存/局部刷新策略"（RGB332/1bpp 内核、点阵字体整字缓存、RLE 离线资源、显存预算档位——§10.4 已排期项）。LVGL 侧：无代码 | XGui 侧：几乎不改，仅把 fbdev/evdev 垫片从 Drive 层共享给 LVGL（v9 自带 linux fbdev+evdev 驱动）。LVGL 侧：lv_conf.h 裁剪 + flush_cb 直连 fbdev | XGui 侧：在 XRenderKernelOps（span 粒度）与 lv_draw task（v9 命令流粒度）之间建桥，格式枚举/预乘语义/裁剪/dither 全要翻译。LVGL 侧：要求接管 draw buffer 生命周期与 flush 时机 |
| **工作量** | 15~20 人日（fbdev 消费链已完成，余为内核表新档位 + 板测） | 首接 5~8 人日；此后每个 LVGL 版本升级、双构建/双文档长期维护成本 | 25~40 人日起，范围不可控 |
| **收益** | 保持单栈；LVGL 实战优化经验（小 buffer、静态层、A/B buf）以 XGui 语义落地；§10.4 板级档位路线不变 | 立即获得 LVGL 全套控件/动画/主题；同板 RAM/FPS 可作 XGui 性能标尺 | 几乎为零：XGui 光栅质量特性（AA/描边/38 合成）已强于 lv_draw_sw，内核表已含同档格式 |
| **风险** | 仅板测不确定性（真机未验证，与是否融合 LVGL 无关） | **双栈冲突**：两套事件循环不可共存一线程（LVGL 轮询 vs XGui 事件驱动）、两套内存纪律（LVGL 自带池 vs 刚收官的 §8.0g6~g8 deinit 体系）、两套字体（lv_font_conv 点阵 vs fontconfig）、23 万行代码心智分裂；与"Qt 6.8.3 对标"立项目标正面冲突 | **渲染管线冲突**：lv_draw 假定自己持有 flush 时机与缓冲所有权，与 XBackingStore 攒批/GPU 直通的帧边界语义互斥；桥接层本身成性能瓶颈；LVGL 升级即断 |
| **与 XWidget 体系关系** | 纯自有栈，无关系 | 并存（应用二选一），不可能替代 XWidget 栈 | 仅借用渲染层，控件体系不动，但污染 painter 之下唯一收敛点 |

另有低强度形态 D：**板级验证期用 LVGL demo 做对标基线**（同板跑 LVGL benchmark 得 RAM/FPS 参照，XGui 逐项对标），2~3 人日，与 A 兼容。

## 三、推荐结论

**推荐形态 A（思想吸收、零代码依赖），辅以形态 D（板级对标）；明确不建议 B 与 C。**

理由：XGui 与 LVGL 的定位重叠度远超预期——渲染内核表已按 LVGL 组织、PARTIAL tile/静态保留层已把 LVGL 最核心的"小 buffer 局部刷新 + 静态内容重绘豁免"两个杀手锏自有化，§8.2 登记的"融合专项"实质剩余工作只是**嵌入式显存/局部刷新策略的板级收口**。形态 C 是负收益：借来的渲染能力弱于自有实现，却要付出管线语义翻译与版本锁定的持续代价；形态 B 只在"XGui 嵌入式路线失败、需要立即交付 LVGL 级 MCU 产品"的兜底场景下才有意义，当前不构成立项理由。

**不建议形态**：C（借渲染层）——契约粒度不匹配（span vs draw task）、收益为零、维护双渲染器；B（双栈并存）——两套事件循环/内存纪律/字体栈的长期分裂成本远大于一次性 5~8 人日，且稀释 Qt 对标主线。

## 四、后续动作清单

1. **改登记表述**（0.5 人日）：§8.2 "Qt+LVGL 融合优化专项"改为"嵌入式显存/局部刷新策略收口（LVGL 思想吸收，零依赖）"，避免"融合"误导立项预期。
2. **板级先行**（3~5 人日）：真机 RGB565 面板目验已有 fbdev 链（色彩/撕裂/刷新率），同板跑 LVGL demo 采基线数据（形态 D），形成 XGui 性能标尺。
3. **内核档位批次**（8~10 人日）：RGB332/1bpp 内核表 + 点阵字体整字缓存 + RLE/C 数组离线资源（§10.4 已排期项，即显存/存储最小化收口）。
4. **显存预算档位**（3~5 人日）：把 PARTIAL tile 尺寸开放为板级可配（1/4 屏、单 tile、全帧三档），对齐 LVGL buffer 档位实践。
5. **决策复盘点**：以上完成且板测达标后，关闭融合专项；仅当 XGui 板测不达标且交付压力大时，才把形态 B 作为兜底预案重新上会（届时先做 1 周技术验证）。
