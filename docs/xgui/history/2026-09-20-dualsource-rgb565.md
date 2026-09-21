# 主循环双源等待 + 目标格式内核表（RGB565 首批）+ 工作流方式

> 归档自 XGui.md §2026（2026-09-21 文档重构迁移，内容逐字保留）。后续更新见 XGui.md 当前版。

## 23. 主循环双源等待 + 目标格式内核表(RGB565 首批) — 2026-09-20

> 本节覆盖三个批次:①已提交的主循环双源等待+Win32 剪贴板(`1126d61a`);
> ②未提交的渲染管线普查+RGB565 内核表首批(当前工作树);③换机继续指南。

### 23.1 已提交:主循环双源统一等待(批次 `1126d61a`)

GUI 主循环从"等单源+轮询另一源(20ms 量化)"改为单阻塞点覆盖双源:

- **POSIX**(`XPlatformNativeWindow_posix.c`):`waitForEvents` 用 poll 双 fd
  —— X11 连接 fd + `XAbstractNetIoRing_global()` 的 ring 事件 fd
  (io_uring ring fd / epoll fd,经 `getEventFd` 抽象);ring 就绪调
  `XAbstractNetIoRing_processReady()`,X11 就绪泵原生事件,分源唤醒。
- **Windows**(`XPlatformNativeWindow_win32.c`):
  `MsgWaitForMultipleObjects(1, {IOCP句柄}, FALSE, msec, QS_ALLINPUT)`;
  IOCP 就绪先 `processReady` 批量 drain 再泵消息(防高频消息饿死网络)。
- `XAbstractNetIoRing_ON=0` 或 ring 未启用时退化单源,行为同既往。
- 同批修复:`XNetIoRingPosix.h` 补 `linux/time_types.h` 包含(新内核头缺
  `__kernel_timespec` 编译失败);`XNetIoRingWin32.h` 补 class_init 声明。
- 收益:网络完成事件延迟 ~20ms 量化 → 微秒级;空闲真休眠(嵌入式待机
  唤醒 50 次/秒 → 按需)。**20ms 钳制(dispatcher :705)保留未动**——它
  是时间轮心跳,不是延迟来源;若要提升普通定时器精度,把时间轮最近
  到期并入 `XDeviceTimer_nextPreciseDeadline`(未做,见 23.4 规划)。

### 23.2 未提交:目标格式内核表(RGB565 首批)——当前工作树状态

**架构**(对标 Skia blitter + LVGL 目标格式内核组织,超越两者处:格式
表在建表面时一次解析,热路径零格式分支;新格式/加速器=注册一张表,
不碰 painter):

- 新增 `Src/XGui/Graphics/XRenderKernel.h`:`XRenderKernelOps` 七个
  span 级原语(fillSpanOpaque/fillSpanBlend/blitSpan/blendSpan/
  glyphMaskSpan/storePrem)+ `XRenderKernel_forFormat(register)`。
  约定:行基址+像素列;颜色恒为预乘 ARGB32 规范色,内核自行压缩;
  **未注册格式返回 NULL → 调用方回退既有逐像素路径(零回归保险丝)**。
- 新增 `XRenderKernel.c`:槽位注册中心(惰性注册内置内核;NEON/
  Helium/DMA2D 变体未来经 register 覆盖注入)。
- 新增 `XRenderKernel_rgb565.c`:首张格式表六内核;混合口径逐字节
  对齐 `painterMul255` 的 `(a*b+127)/255`(勿用 `>>8` 近似,有 ±1 差)。

**五个接缝**(普查确认,`XPainter.c` +210 行;默认 ARGB32 路径字节级
不变——每处条件都是"dest 非 ARGB32_Premultiplied 才进新分支"):

| 接缝 | 位置 | 内容 |
|---|---|---|
| blendFillRect | XPainter.c:1602-1627 | 非 Prem 时 ops->fillSpanBlend 按行 |
| putPixel | :1775-1801 | 换最终存储为 ops->storePrem(裁剪判定零改动;detached 门保 COW) |
| blitImageRegion | :2700-2826 | 门放宽+行内核(blit/blendSpan 按 alpha 分流);行宽校验改 depth/8 |
| glyphAlphaBlend | :8598-8680 | 与直写块格式互斥的新分支,goto fallback 保底 |
| XImage_fillRect | XImage.c:5561-5592 | RGB16 行快循环(不依赖内核表,XImage 公共层内联) |

**表面格式化**(`XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16` 选择器,默认 0;
嵌入式 `-D...=1` 切 565):

- `XGuiConfig.h:152-166` 选择器定义(:713-714 裁剪级联复位);
- `XPlatformBackingStore.c:27-48` `XPBS_IMAGE_FORMAT`/`XPBS_PIXEL_BYTES`
  按选择器;:117-141 copyRect 的 *4 → *PIXEL_BYTES;:585-588 stride 校验
  按格式;:731-747 PARTIAL 下 surfaceResized 改传 tile 尺寸(修复 Win32
  白建整窗 DIB;依据既有契约注释"PARTIAL 为 tile 缓冲尺寸");
- **勿改 `requiredBufferSize`**——它按 `XImageFormat_bytesPerLine` 算,
  本身随格式正确(RGB16 4 字节行对齐),改 w*2 反而破坏奇数宽对齐。

**present 适配**:

- win32(两文件):16bpp `BI_BITFIELDS`+掩码 {0xF800,0x07E0,0x001F};
  DIB 行距 `(w*2+3)&~3`(4 字节对齐,勿用裸 w*2,否则奇数宽错位);
  `XPBS_WIN32_PIXEL_BYTES`/`XPWN_PIXEL_BYTES`/`*_DIB_ROW_STRIDE` 宏族;
  grabWindow 截图路径有意未改(GDI 自转换)。
- posix(`XPlatformNativeWindow_posix.c` +95):`xpwn_copyRect16`;
  depth-16+565 掩码直拷判定(:3920-3925);XCreateImage 显式
  bytes_per_line=w*2(:3899-3911);视觉非 16 位时返回 false(无展开
  路径,绝不误按 4 字节读)。

**PARTIAL 免全屏缓冲确认**(普查实证,此前担心的"全屏后备"不存在):
`XPlatformBackingStore.c:633-637` 只分配 `min(w,160)×min(h,80)` tile;
但 tile 是**逐片绘制完立即上屏**(`XWidget.c:5592-5599` 唯一 flushTile
调用方),无攒批——见 23.4 规划。

### 23.3 验证状态(未提交批次)

- ✅ Windows x86-Debug:581 目标全绿,`XinYueCd.dll` 链接成功
  (注意:CMake GLOB 不自动发现新文件——新增 .c 后需 `touch
  CMakeLists.txt` 重新配置;ninja 依赖缓存偶发陈旧,报"未声明标识符"
  时先 touch 源文件强制重编再排查);
- ✅ WSL gcc 11.4 全量:静态库 971 编译单元通过;测试可执行文件链接
  失败仅因 WSL 缺 libpcap-dev(`apt install libpcap-dev` 可解,非代码);
- ✅ RGB16 组合配置:三互依文件 + 内核两文件合并 gcc 语法检查通过
  (`RGB16_COMBINED_OK`);各子代理已各自跑过双配置;
- ✅ 修复主线契约文件疏漏:`XRenderKernel.c` 补 `<stddef.h>`(NULL,
  MSVC 放过 gcc 抓住);
- ❌ 运行时未测:RGB565 真机/模拟器目验、565 色彩正确性(GDI 小端
  语义为设计推断)、XGUI_ON=0 全裁剪配置下 XPainter.c 有 3 个既有
  编译错误(HEAD 同样存在,非本批引入,已记录待修);
- 📦 未提交:10 文件(+576/−40 + 3 新文件),等用户验收后提交。

### 23.4 后续规划(按优先级)

1. **RGB565 运行时目验**(板/模拟器,XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1
   构建 demo)→ 通过后提交本批;
2. **fbdev 显示驱动模板**(/dev/fb+mmap+FBIOPAN):需公共层补 5 钩子
   ——格式协商(已有选择器)、pan/swap、cache clean/invalidate(DMA
   scanout 前)、vsync/fence、按格式 stride(已有);落地后 RGB565
   present 零拷贝直写 framebuffer;
3. **SIMD 内核填充**:XRenderKernel_register 覆盖注入 NEON/Helium
   变体(fill/copy/blit 三热内核优先);
4. **PARTIAL tile 攒批**:相邻 tile 合并 flush,减少 present 次数;
5. **时间轮 deadline 并入 nextPreciseDeadline**(普通定时器精度
   20ms → 1ms,时间轮全局精度已是 `XTimeWheelGroup_create(1)`);
6. **静态内容保留层**(字节预算 LRU,静态仪表盘 3~5×);
7. 修 XGUI_ON=0 下 XPainter.c 3 个既有编译错误。

嵌入式性能预期(工程估算,以板测为准):MCU+DMA2D 场景整帧 3~6×、
RAM 省 0.5~1.5MB(PARTIAL 免全屏+565 减半);入门 A 核静态 HMI
5~10×;桌面无感(基线已 5670FPS/0.176ms)。

### 23.5 工作流方式(子代理并发模式,换机可复用)

本批采用"主线统筹 + Flash 子代理并发"的动态工作流模式,已在两个
批次中验证有效,后续沿用:

**模型分工**:主线程 GLM-5.3(统筹/契约/接线/构建验证/审查),子代理
GLM-5.3-Flash(边界清晰的实现类任务)。本机可用模型见 ListModels;
工作流脚本经 CreateWorkflow 的 `subagent_model` 字段指定:
`account:bigmodel-individual-coding-plan/GLM-5.3-Flash`。

**批次 1(普查)**:2 个只读 Explore 型子代理并发(渲染内核普查 /
表面管道普查),Promise.all 汇合,artifact.markdown 出报告;主线对
最承重行号做确定性抽查后采信。

**批次 2(实现)**:主线先亲自写契约文件(XRenderKernel.h/.c,所有
子代理的对接界面,不能并行)→ 再派 5 个实现子代理并发,按**文件
互不重叠**分组(RGB565 内核新文件 / XPainter+XImage 接缝 / 表面
公共层 / win32 两文件 / posix 一文件)→ 各自跑 gcc 语法双配置 →
主线集成(修跨平台疏漏+全量构建)。

**并发正确性三原则**(本批实证有效):
1. 契约先行:子代理开工前接口头文件必须在库里,任务书写明"先读后写";
2. 文件所有权:每个子代理独占文件集,禁止越界(5 路零冲突实证);
3. 并行顺序消解:宏定义使用点 `#ifndef 兜底`,不依赖别路先落地。

**已知坑(换机必读)**:
- Git Bash → wsl.exe 传参会把 `$var`/`$(...)` 剥离/预展开:复合命令
  写入 .sh 文件放仓库内,`wsl -- bash -c "bash /mnt/d/.../x.sh"` 执行
  (子代理们各自独立发现了这一点,解法一致);
- 命令含反斜杠路径时 printf/echo 转义易坏,批处理文件用 Write 工具
  或 heredoc 生成;
- MSVC 放过而 gcc 严格的头文件疏漏(如 NULL 未含 <stddef.h>)——
  双平台构建互补,缺一不可;
- 新增 .c 文件后 CMake GLOB 不感知:touch CMakeLists.txt 重配置;
- ninja 依赖缓存陈旧会造成幽灵"未声明标识符":touch 源文件强制重编;
- 工作流草稿存于 `.zcode/workflow-drafts/`(本机路径,不入库,换机
  后按本节描述重建即可,脚本很短);
- 大任务防中断:已完成的批次务必及时让用户验收提交(本文件 23.2 即
  处于待提交状态)。

### 23.6 本机集成与内核级验证(会话侧补充,2026-09-20 深夜)

**合并与恢复**:本会话 25 文件未提交改动(串行批次 23~26)经
`stash push → pull --ff-only(31495ae8) → stash pop` 与远端批次
融合,零冲突(三方合并:本方 AA/描边/INCR 与远端 RGB565 接缝/
双源等待落在不同区域);融合树构建 0 错误、回归零失败断言、
验收 68/68、GPU 通过。

**RGB565 内核级验证 ✓(探针 6 组全过)**:fillSpanOpaque 压缩/
fillSpanBlend 预乘混合(0x80800000 半透明红→r5=16,混合口径
正确)/blitSpan 逐像素/blendSpan alpha=0 幂等/glyphMaskSpan
255 等价直写与 0 不变/ARGB32 未注册槽位回退 NULL(零回归保险丝)。
探针初版误用非预乘色(0x80FF0000,R>A)得到的"FAIL"为探针错,
已修正——RGB565 混合语义对预乘规范色是正确的。
**显示级目验环境受限**:本机无 Xvfb/Xephyr 且 apt 需密码,
16bpp 真窗口无法创建;`sudo apt install xvfb` 后用
`Xvfb :99 -screen 0 800x600x16` 起深度 16 嵌入服务即可补
(构建 demo 时 -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1)。

**§23.4 四项实现已按 §23.5 工作流并发启动**(Flash 子模型,
文件所有权互不重叠):XGUI_ON=0 编译错误修复(XPainter.c)/
PARTIAL tile 攒批(BackingStore+XWidget)/时间轮 deadline 并入
nextPreciseDeadline(分发器+时间轮)/fbdev 驱动模板(新文件+
5 钩子契约);完成后主线集成构建+三套件验证(结果见下批记录)。

**文档重构阶段一完成 + 根目录文档更新(2026-09-21)**:
- 重构设计落地 docs/xgui-audit/2026-09-21/文档重构设计.md:
  现状盘点(3944 行活文档/日志混编、35 处性能数字无统一口径)、
  目标结构(XGui.md ≤800 行活文档 + docs/xgui/history/ 日志归档 +
  architecture/ 分册)、逐节迁移映射、验收标准;执行待三梯队
  集成完成后(阶段二迁移、阶段三分册)。
- README.md 更新:核心特性补 XGui;项目结构树补齐全目录
  (Src/XGui 十二子目录/XPlatform/XTui 等);平台说明改
  "Windows+Linux(X11) 双栈完整实现";模块表补 XGui/XPlatform/
  XTui/XDevice 并修 XEvent 死链;新增 XGui 模块详情与快速开始
  (demo/三套件/RGB565 开关)。
- XGui_Qt_Alignment_Handoff.md 加"已完成/已取代"状态横幅
  (2026-07-27 旧交接,指向 §14.125/§14.126)。

**内存泄漏基线扫查(新常设任务首轮,ASan 快照法,2026-09-20)**:方法:工作树快照至 /tmp 独立目录,`-fsanitize=address -g -O1` 全量
构建回归+验收二进制,LeakSanitizer 退出扫查 + fast_unwind 精确
归因(不干扰并发代理的工作树)。
**已修复(真缺陷 4 笔)**:
1. `xtc_recordCommand` 三个撤销合并分支 strdupN 越界读(分配长度
   被误用作拷贝长度,从旧串多读合并增量;结果碰巧被后续 Memcpy
   覆盖正确,但中间是堆越界 UB)——改为 XMalloc 分配+两段显式拷贝;
2. `XTextControl_setFont` 家族比较 UAF 时序——oldFamily 指向旧
   字体 toUtf8 缓存,deinit 在 strcmp 之前释放;比较前置;
3. 验收测试 `ac_ime` 未对栈上 XInputMethodEvent deinit(每条 IME
   事件泄漏两份 XString,ASan 复现 4344B);
4. 验收测试 `runAll` 直接 init 未置 ac_ctlAlive,首个控制器缓冲
   init-over-init 脱管(128B)。
**结论与边界**:修复后两套件 ASan 下零崩溃类错误;剩余 ~196KB
为退出时持有——主回归从不删除控件树(测试夹具语义),其中
XLineControl_setFont 的 toUtf8 缓存由未删控件字体持有,非逐调用
泄漏。**全量清零待三梯队集成后做**(集成会改泄漏画像),夹具
清理与"每操作增量泄漏"断言列入集成后任务。教训:setFont 类
"疑似泄漏"需先辨明所有权归属(调用方持有≠泄漏),本会话一次
错误释放曾引发调用方 UAF,已即改即验。

**RGB565 显示级运行时目验 ✓(§23.4 规划 1 收口,2026-09-20)**:
环境:Xvfb :99 -screen 0 800x600x16(用户已装 xvfb);构建:独立
目录 build-rgb16,-DCMAKE_C_FLAGS="-DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1"
(注意两个构建目录共用仓库 bin/ 输出,二进制互相覆盖,需备份/恢复
处理);运行:demo --autotest --screenshot 在 :99 无 X Error 干净退出。
**像素级判别验证**:逐像素校验"565 位复制展开合法性"(v<<3|v>>2 与
v<<2|v>>4;截断式 %8/%4 检查不成立——X 服务器回读是位复制展开)——
16 位显示上 187200 像素 **0.00% 非法**(全部经内核表管线:文本 AA 走
glyphMaskSpan、进度条走 fillSpan 等),24 位对照 92.98% 非法(判别力
成立)。内核探针 6 组 + 显示级目验双重通过,§23.2 批次(已提交
31495ae8)验收完成。

**三梯队集成完成(2026-09-21 凌晨,11 路 Flash 全部交付+全树集成)**:
- 第一梯队(远端 §23.4):XGUI_ON=0 三编译错误修复(语义等价经 -E
  逐行 diff 验证)/PARTIAL tile 攒批(桩验证 25 项+ASan 零泄漏)/
  时间轮 deadline 并入(20ms→1ms,并发护栏论证)/fbdev 模板
  (契约先行+默认关零 ABI 面)。首轮集成失败教训:world.run 直接
  捕获 cmake 输出 stderr 7MB 超 256KB 上限——改为输出落盘脚本
  只回传退出码摘要(.zcode/integrate.sh)。
- 第二梯队(遗留结构):XDockWidget 真实浮动+toggleViewAction+宿主
  回链防悬空;XMainWindow 四区停靠几何+saveState v2;条目视图
  delegate 编辑闭环(XItemDelegate 新建+role 体系+editTriggers+
  visualRect 虚化);MULTIPLE 协议(token 归一化比对 1872==1872
  零回归)+位图光标 X11 通道;theme×调色板联动(深色组锚定 Qt
  qt_fusionPalette 数值+显式 setPalette 守卫+顶层重绘广播)。
- 第三梯队(大件):富文本渲染子集(格式栈 HTML 解析器+预览模式+
  链接交互,冒烟 28/28);真实 UI 对话框(Input/Color/File 三套
  静态函数真弹窗);XGraphicsEffect 生态(三效果+控件渲染钩子,
  source→快照→效果→回贴管线)。
- **集成期连修两笔类宏陷阱(第三梯队效果类)**:①自定义槽首项未
  锚定 `= XCLASS_VTABLE_GET_SIZE(Parent)`,槽位从 0 起编号覆写
  EXClass_Deinit(删除旧效果即段错误);②误用 EXTEND_END 把
  END_SIZE 重置回父类槽位数(容量 10 砍掉新槽,OVERLOAD 索引
  越界)。正确形态=首槽锚定+DEFINE_END 续号(对照
  XCoreApplication.h)。修复后三套件全绿。
- 集成终验:构建 0 错误、回归 exit=0 零失败断言、验收 68/68、
  GPU 通过。全部改动未提交,等用户验收。
