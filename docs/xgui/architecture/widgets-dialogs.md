# 控件与对话框架构

> 归属：Src/XGui/Widget（控件族）+ Src/XGui/Window（XDialog/XWindow）。
> 对标：Qt Widgets 全家族。本文记录跨控件的公共机制；各控件细节见
> 头文件注释。

## 1. 事件与焦点公共机制

- 事件入口 `VXWidget_event`：禁用控件丢弃输入类事件（对标
  QWidget::event）；按类型分派到 23 个公开事件槽（虚表分派）。
- **XWidgetWindow 桥接**：顶层控件惰性创建内部 XWindow 子类，其
  event 转译器处理命中/坐标平移/模态拦截/触摸平板/效果接管后
  转发控件。
- 焦点：焦点链/代理（focusProxy）/原因枚举；Tab/Backtab 遍历接线
  于 keyPressEvent 无命中路径。
- 拖放： DragEnter/Move/Leave/Drop 全套（X11 XDND 协议落地于 posix）。

## 2. 文本控件族

XLineEdit（单行壳）+ XPlainTextEdit（多行壳）+ XTextEdit（富文本
预览）+ XTextBrowser（浏览+链接导航），控制器见 text-system.md。
公共语义：右键菜单构建器、中键粘贴、双击选词、验证门禁。

## 3. 按钮/容器族

- XAbstractButton：checkable/组互斥（守卫只保护组 tracked 选中项，
  反选次序对标 Qt notifyChecked）/autoExclusive。
- XGroupBox：checkable + toggled 语义 + 标题区"按压待命、释放切换"
  （对标 QGroupBox mousePress/Release）。
- XComboBox：可编辑补全（前缀过滤弹层）+ insertPolicy 七策略结算。
- XStackedWidget/TabBar/ToolBox/ScrollArea/Splitter/DockWidget/
  MainWindow：布局与状态机对齐（Dock 真实浮动/四区停靠几何/
  saveState v2）。

## 4. 条目视图族

基类统一：模型四信号联动、滚动偏移全族、键盘导航+选择模式、
**delegate 编辑闭环**（XItemDelegate：createEditor/setEditorData/
setModelData + commitData/closeEditor；editTriggers 五触发）；
role 体系（Display/Edit/CheckState/Font/Alignment/Decoration 叠加
存储）。派生视图仅需实现 visualRect 虚槽与数据访问。

## 5. 对话框

XDialog.exec 阻塞循环（应用模态登记+Escape→reject）；三套静态
便捷函数真弹窗（Input 四件套/Color 48 色块+RGB 联动/File 目录
浏览+过滤器）；无应用实例时保持无头桩语义（*ok=false）。

## 6. XGraphicsEffect 渲染生态

source→快照→效果→回贴管线（paintTree 钩子接管子树绘制；快照期
临时摘除效果防递归）；Opacity/Blur(3x3 盒式)/DropShadow 三效果；
属性 setter 即 update。边界：blurRadius 仅 API 对齐；外扩区依赖
父级重绘。

## 7. 图形效果之外的跨控件机制

- 拖放、右键菜单（DefaultContextMenu 策略+customContextMenuRequested）。
- 尺寸提示：updateSizeHints→updateGeometry→布局激活链。
- XWidgetWindow 事件转译的模态拦截（isWindowBlocked 对标）。
