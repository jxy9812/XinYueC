# 文本系统架构

> 归属：Src/XGui/Text（XLineControl/XTextControl 控制器）+
> Src/XGui/Widget（XLineEdit/XPlainTextEdit/XTextEdit 壳）。
> 对标：QWidgetLineControl / QWidgetTextControl 私有控制器架构。

## 1. 控制器模型

壳（XLineEdit/XPlainTextEdit）与控制器（XLineControl/XTextControl）
分离：壳负责外观/边距/滚动条/菜单组装，控制器承载 行模型/光标/选区/
撤销重做/IME/命中测试/剪贴板三通道。壳经公开 API 驱动控制器，事件
（键盘/鼠标/IME）按坐标与模式转发。

## 2. XLineControl（单行）

- 掩码引擎（inputMask 全语义）、validator 门禁（失焦 editingFinished
  带 hasAcceptableInput||fixup 判定）、maxLength、echoMode。
- IME：preedit 计入显示与光标；commit 替换选区。
- 撤销：insert/remove 命令合并（键入连续分组、退格双向合并）；
  **合并缓冲分配与拷贝分离**（历史越界读缺陷已修，勿回退 strdupN
  长度复用写法）。
- 剪贴板三通道：`XLineControl_paste(control, mode)`——CLIPBOARD 走
  系统剪贴板+共享层回退；PRIMARY 仅系统（supportsSelection 门禁）。

## 3. XTextControl（多行）

### 3.1 软换行（WidgetWidth）

- 可视行布局缓存：`{逻辑行, 起始字节, 字节长, 像素宽}` 单调数组，
  脏标记惰性全量重建（O(文档长)；增量布局为遗留项）。
- 断行：简化 UAX#14——空白/CJK/词字符/标点分类 + 行禁首尾禁则；
  WordWrap=CJK 逐字+CJK↔西文边界+词内不断；溢出先记断点再判溢出
  （贪心最满行，软断像素宽结转新行）；长词硬断兜底。
- **双向映射**：字节位置↔(可视行,列)——光标移动/鼠标命中/选区绘制/
  绘制全经此映射；软断点歧义用 `m_cursorAtRowStart` 边沿标记
  （对标 Qt 光标边沿跟踪）。
- 失效点：插入/删除、setFont（度量真变化才失效）、setTextWidth、
  换行模式 setter、preedit 挂载/清除。
- NoWrap：可视行 1:1 退化为逻辑行；绘制逐行裁剪（文本绝不越框）。

### 3.2 IME

preedit 以 `xtc_visualLineText` splice 进视觉文本参与折行；组合串
增减行数发射 documentSizeChanged；提交走 rawInsert。

### 3.3 撤销重做

命令栈 {pos, removed, inserted, group}；编辑块（group）包裹合并；
**合并缓冲分配/拷贝分离**（见 §2 警示）。

## 4. 富文本子集（XTextEdit/XTextBrowser 显式预览模式）

- 解析器（XTextDocument xtd_parseHtml）：b/i/u/s、font(color/size)、
  br、p(align)、h1-h6、ul/ol/li、a(href)、8 类实体；嵌套上限一层；
  setHtml/appendHtml 共用；toHtml 互逆。
- 渲染：xte_walkRich 逐块几何遍历（块两遍：行高/基线/块宽→对齐
  定位逐片段）；绘制/anchorAt/滚动范围共用同一几何源；伪粗体双描边。
- 链接：悬停 linkHovered+手型光标；释放同链 linkActivated；
  XTextBrowser 转发 anchorClicked/highlighted + openExternalLinks。
- 边界：不换行、超宽裁剪、justify 降级、嵌套一层、斜体无视觉合成。

## 5. XTextDocument 注意事项

- setPlainText：逐行建块，**容量增长必须配新增槽位清零**（历史越界
  缺陷，ASan 复现归属确认）。
- 连发 `<br>` 场景：容量随块推进扩展 + 收尾钳位。
- XLineControl/XTextControl 的字体比较：**先比较后释放**（toUtf8
  缓存随字体串释放，比较前置防 UAF——2026-09-21 ASan 发现）。
