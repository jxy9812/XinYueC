# 文本编辑控制器化重构计划与实施

> 归档自 XGui.md §2026（2026-09-21 文档重构迁移，内容逐字保留）。后续更新见 XGui.md 当前版。

## 16. 文本编辑控制器化重构计划（对齐 Qt 私有控制器架构） — 2026-09-18

### 16.1 背景与动机

XLineEdit（单行）与 XPlainTextEdit（多行）是两条平行继承链（前者直接继承
XWidget，后者经 XAbstractScrollArea/XFrame），与 Qt 完全一致；Qt 也没有
"文本编辑共同控件基类"。但两者在控件内部各自内联实现了同一批编辑外围能力：

- 撤销/重做栈（XLineEdit 用定长数组，XPlainTextEdit 用 XVector，两套实现）；
- 剪贴板读写（各写一份 XGuiApplication_clipboard 往返）；
- UTF-8 码点边界扫描（`xlineedit_nextBoundary` vs
  `xpe_utf8SeqLen`/`xpe_prevBoundary`）；
- IME 提交接入、标准右键菜单构建、光标绘制与命中测宽。

重复实现已发生一次真实漂移事故（2026-09-18）：XPlainTextEdit 的
backspace/delete 按单字节删除中文，把多字节字符拆成非法残序列，渲染为
空白且光标测宽错位（用户感知为"光标反方向跳动"、"删除出空白字符"）；
而 XLineEdit 的同名逻辑自始就是码点感知的（`xlineedit_nextBoundary`）。
两份实现各自演化，正是该类缺陷的温床。本轮已把 XPlainTextEdit 修复为
码点感知（`XPlainTextEdit.c` backspace/delete/左右键 + 新增回归用例），
但两份实现并存的漂移风险仍在。

### 16.2 Qt 6.8.3 参考架构（本机源码实测，D:/Qt/6.8.3/Src）

Qt 对同一问题的解法：公开控件层不做共享，编辑逻辑全部下沉到"私有文本
控制器"（非控件的 QObject）：

- `qtbase/src/widgets/widgets/qwidgetlinecontrol_p.h:50`
  `class QWidgetLineControl : public QInputControl` —— QLineEdit 专用
  （单字符串模型：maxLength/validator/回显模式/命中测试/撤销栈）。
- `qtbase/src/widgets/widgets/qwidgettextcontrol_p.h`
  `class QWidgetTextControl : public QInputControl` —— QTextEdit、
  QPlainTextEdit、QTextBrowser、QLabel（可选中文本）共用（文档模型：
  QTextDocument + QTextCursor + 选区 + 撤销栈 + IME + 命中测试）。
- `qtbase/src/widgets/widgets/qplaintextedit_p.h:46`
  `class QPlainTextEditControl : public QWidgetTextControl` —— 块感知特化。
- 共同根：`qtbase/src/gui/text/qinputcontrol_p.h:50`
  `class QInputControl : public QObject`，以 `Type{LineEdit,TextEdit}`
  区分按键可接受语义。两个控制器本身是兄弟关系，Qt 亦未强行抽取共同
  编辑基类。

控件壳因此极薄（以 QPlainTextEdit 为例，qplaintextedit.cpp）：

- `copy()/undo()/paste()` 即 `d->control->copy()/undo()/paste()`；
- `keyPressEvent/mousePressEvent/inputMethodEvent` 一律
  `d->sendControlEvent(e)`（qplaintextedit_p.h:107 →
  `control->processEvent(e, offset, viewport)`），光标定位、选区、
  撤销全部由控制器在文档坐标内完成；
- textChanged/undoAvailable/selectionChanged 等信号由控制器发射、
  控件转发；
- 绘制经 `control->draw(...)`（含 AA）。Qt 的 QRectF 路径边界
  （x+w/y+h）靠 0.5 平移 + 抗锯齿落到最外圈像素；XGui 整数光栅等价
  内缩见本轮 XFusionStyle 按钮边框修复。

### 16.3 XGui 现状对照

| 能力 | Qt 位置 | XGui 现状 |
| --- | --- | --- |
| 撤销/重做栈 | 两个私有控制器 | XLineEdit 定长数组、XPlainTextEdit XVector，两套 |
| 码点边界 | 控制器内部（QTextCursor） | xlineedit_nextBoundary / xpe_utf8SeqLen+xpe_prevBoundary 两套 |
| 剪贴板读写 | QWidgetTextControl::copy/paste | 两份 XGuiApplication_clipboard 往返 |
| IME 提交 | control->processEvent | 两份 inputMethodEvent（本轮补齐 XPlainTextEdit） |
| 标准编辑菜单 | 控件 createStandardContextMenu | 两份近乎相同的构建函数 |
| 光标绘制/测宽 | control->draw | 各自 paintEvent 内联 |

（样式引擎承接的绘制不在本计划范围；XCommonStyle/XFusionStyle 分层维持
现状。）

### 16.4 实施计划（两期）

#### 一期：文本工具层共享（低风险，先行）

1. 新增 `Src/XGui/Text/XTextUtf8`（暂定名）：
   - `XTextUtf8_seqLen(s, remain)`（吸收 xpe_utf8SeqLen）；
   - `XTextUtf8_prevBoundary(s, col)`（吸收 xpe_prevBoundary 与
     xlineedit_nextBoundary 的反向语义）；
   - `XTextUtf8_nextBoundary(s, len, col)`；
2. 剪贴板文本助手：`XTextClipboard_setText/getText`（封
   XGuiApplication_clipboard 往返与 UTF-8 转换）；
3. 标准编辑菜单构建器：`XTextMenu_createStandard(ops)`，ops 为回调表
   （undo/redo/cut/copy/paste/selectAll + 对应 enabled 查询），XLineEdit
   与 XPlainTextEdit 各传自己的槽；
4. 两个控件删除各自重复实现，改为调用共享层；行为不变，回归全绿为
   验收线（重点：UTF-8 码点编辑用例）。

#### 二期：控制器对象化（结构对齐 Qt）

1. 新增 `XLineControl`（QObject 语义，非控件）：单字符串模型、撤销栈、
   回显模式、maxLength/validator 钩子、命中测试、IME 提交、绘制数据
   （对标 QWidgetLineControl）；
2. 新增 `XTextControl`（对标 QWidgetTextControl 的平铺行简化版）：行
   数组、撤销栈、选区模型、码点游标、滚动值联动、IME/命中测试；
3. `XLineEdit`/`XPlainTextEdit` 壳化：keyPress/mousePress/IME 事件改为
   `XTextControl_processEvent(control, event)`；paintEvent 调
   `control->draw(painter, clip)`；公开 API 一行委托，签名不变；
4. 迁移顺序：先 XPlainTextEdit（本轮修复的码点/IME/菜单逻辑整体搬家），
   后 XLineEdit；分两个独立提交；
5. 验收：回归套件全绿（含 UTF-8 码点用例）、演示页交互实测
   （点击定位/中英文输入/Backspace 与 Delete 方向/方向键/右键菜单）、
   像素级截图比对（边框/光标/选区高亮）。

### 16.5 风险与约束

- 行为不变是硬约束：重构期间不得顺带改交互语义；缺陷修复单独提交；
- 光标测宽（XPainter_textWidthRange 字节偏移口径）、IME commitString、
  菜单启用态为高敏区，每步改动需截图像素比对；
- 平铺行模型暂不引入 QTextDocument（XTextDocument 桥接保持现状），
  避免把控制器对象化扩大为文档模型重写；
- 二期迁移 XLineEdit 时，密码回显（PasswordEchoOnEdit 状态机）与
  校验器拒绝路径必须逐条回归。

