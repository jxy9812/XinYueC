# XGui 三文本控件「编辑能力面 → 控制器」迁移增量图（只读审计）

> 审计日期：2026-09-19　性质：只读审计，未改动任何源码；本文件为唯一新增产物。
> 审计对象：`Src/XGui/Widget/XLineEdit.c/.h`（2687+805 行）、
> `Src/XGui/Widget/XPlainTextEdit.c/.h`（1744+546 行）、
> `Src/XGui/Widget/XLabel.c/.h`（选择交互部分，2573+453 行）。
> Qt 基准：`/home/xinyue/Qt/6.8.3/Src/qtbase/src/widgets/widgets/` 下
> `qwidgetlinecontrol_p.h/.cpp`、`qwidgettextcontrol_p.h/.cpp`、
> `qlineedit.cpp`、`qplaintextedit.cpp`、`qlabel.cpp`（下文 Qt 行号均指
> 该目录内文件）。
> 行号基准：XGui 源码行号以当前工作区为准（`Read` 工具 1 起计数）。

## 〇、结论速览

1. **XLineEdit** 是三壳中编辑逻辑最完整的（文本/光标/选区/撤销/剪贴板/
   掩码/校验/回显/补全/IME/菜单全部齐备），与 Qt `QWidgetLineControl`
   的职责面几乎一一对应——它本质上就是「把 Qt 控制器摊开写进了控件壳」。
   控制器落地时它要做的是**整体搬移 + 委托化**，而非补能力；真正的增量
   是密码回显定时器/光标闪烁/词选择/preedit 组合文本等 Qt 有而 XGui 无的项。
2. **XPlainTextEdit** 的编辑面是三壳中最薄的：选区只有单布尔
   （`m_selectionActive`，无锚点/位置对）、撤销是整文快照、无选区光标
   移动、无 Ctrl 快捷键、无拖选。它的迁移是「平铺行存储模型 → 文档+
   光标」的**模型升级**，控制器必须新增的能力远多于壳已有能力。
3. **XLabel** 的选择交互（拖选/双击选词/键盘扩展/失焦清除/命中映射）在
   Qt 中全部由 `QWidgetTextControl` 承载；XGui 目前把这些写进了壳的
   事件虚槽与静态函数。迁移方向与 Qt 一致：交互状态与命中测试迁入
   控制器，壳保留内容选择（文本/图/影片）、边距/布局、链接信号转发与
   绘制入口。
4. 三壳已有可复用的共享文本层：`Src/XGui/Text/XTextUtf8`、
   `XTextClipboard`、`XTextMenu`（由本次审计对象内注释证实，系此前从
   XLineEdit/XPlainTextEdit 抽取）。控制器应建立在这三者之上，避免
   第三次复制粘贴。

---

## 一、Qt 参照职责基线（判定依据）

### 1.1 QWidgetLineControl（行编辑控制器，`qwidgetlinecontrol_p.h`）

控制器持有并负责（行号指 `qwidgetlinecontrol_p.h`）：

| 职责 | 佐证 |
|---|---|
| 文本存储与显示文本（掩码剥离） | `m_text`/`text()`/`displayText()` :208-224 |
| 光标/选区（含 preedit 光标） | `m_cursor/m_preeditCursor/m_selstart/m_selend` :57-63, :128-150 |
| 撤销/重做命令历史（命令型，非快照） | `Command`/`m_history`/`addCommand` :440-450 |
| 修改状态 | `m_modifiedState/m_undoState`/`isModified` :62, :115-116 |
| 剪贴板 copy/paste | :155-158 |
| 回显模式 + **密码回显状态机与定时器** | `m_echoMode/m_passwordEchoEditing/m_passwordEchoTimer/m_passwordMaskDelay` :63-64, :470-480；`updatePasswordEchoEditing`（.cpp:352）、`timerEvent` 处理 `m_passwordEchoTimer`（.cpp:1543-1545） |
| 输入掩码全套 | `parseInputMask/isValidInput/maskString/clearString/stripString/findInMask` :459-465；`m_maskData` :437 |
| 校验器与 fixup | `m_validator/fixup()` :267-269, :418-424（.cpp） |
| maxLength | :258-265 |
| 补全器挂接 | `m_completer/complete(int)` :272-277 |
| IME（preedit 组合文本、提交） | `composeMode/setPreeditArea/commitPreedit/processInputMethodEvent` :305-310, :222, :340 |
| 键盘事件处理 | `processKeyEvent` :341（Return→`accepted()`/`editingFinished()` 在 .cpp:1650-1659，含 `hasAcceptableInput()||fixup()` 门禁） |
| **光标闪烁定时器** | `m_blinkTimer/setBlinkingCursorEnabled/resetCursorBlinkTimer` :60-61, :343-347 |
| 命中测试与光标矩形（经 QTextLayout） | `xToPos/cursorRect/anchorRect/cursorToX` :191-203 |
| 词选择 | `selectWordAtPos` :240 |
| 文本区绘制（正文+选区+光标） | `draw(QPainter*, const QPoint&, const QRect&, DrawFlags)` :354-361 |
| 尺寸/方向/字体/调色板 | `naturalTextWidth/ascent/setFont/setPalette/layoutDirection` :121-129, :325-353 |
| 控制器信号 | `cursorPositionChanged/selectionChanged/displayTextChanged/textChanged/textEdited/accepted/editingFinished/updateNeeded/inputRejected` :489-507 |

### 1.2 QWidgetTextControl（多行/富文本控制器，`qwidgettextcontrol_p.h`）

| 职责 | 佐证 |
|---|---|
| 文档所有权与光标 | `setDocument/textCursor/setTextCursor` :71-75 |
| 文本交互标志 | :77-78（决定鼠标/键盘可选、可编辑） |
| 字符格式 | `currentCharFormat/mergeCurrentCharFormat` :80-83 |
| 查找 | `find` :85-88 |
| 剪贴板与 MIME | `cut/copy/paste` :170-174；`createMimeDataFromSelection/canInsertFromMimeData/insertFromMimeData` :228-230 |
| 撤销/重做 | :176-177（走 QTextDocument/QUndoStack） |
| 标准右键菜单 | `createStandardContextMenu` :102 |
| 命中测试/光标/选区几何 | `hitTest/cursorForPosition/cursorRect/selectionRect/blockBoundingRect` :105-110, :159-160 |
| 锚点（链接）交互 | `anchorAt/anchorAtCursor/setFocusToAnchor/findNextPrevAnchor` :111-116, :232-234 |
| 覆盖模式/光标宽度/拖拽/词选择 | :118-153 |
| 额外选择集 | `extraSelections/setExtraSelections` :127-130 |
| 事件统一入口 | `processEvent(...)` :218-219（鼠标/键盘/IME 全走这里；QLabel 的 `sendControlEvent`、QPlainTextEdit 的各事件虚槽都汇入） |
| 绘制 | `drawContents(QPainter*, const QRectF&, QWidget*)` :222 |
| IME 查询 | `inputMethodQuery` :226 |
| 控制器信号 | `textChanged/undoAvailable/redoAvailable/currentCharFormatChanged/copyAvailable/selectionChanged/cursorPositionChanged/updateRequest/documentSizeChanged/blockCountChanged/visibilityRequest/microFocusChanged/linkActivated/linkHovered/modificationChanged` :193-211 |
| 定时器 | `timerEvent` :237（光标闪烁、三元点击、拖拽自动滚动等控制内定时） |

### 1.3 壳（QLineEdit/QPlainTextEdit/QLabel）保留的职责

| 壳保留 | 佐证 |
|---|---|
| **边框/面板/样式绘制** | QLineEdit::paintEvent 画 `PE_PanelLineEdit` + `SE_LineEditContents` 后才调 `d->control->draw(...)`（qlineedit.cpp:1981-2100）；QPlainTextEdit::paintEvent 自己铺视口背景/占位（qplaintextedit.cpp:1887 起） |
| **内置 action / 清除按钮等 side widget** | 清除按钮=QAction+QLineEditIconButton，归 QLineEditPrivate 的 leading/trailing 队列并 `positionSideWidgets()`（qlineedit.cpp:455-485, :1472, :2305-2310） |
| **sizeHint/minimumSizeHint/heightForWidth** | QLineEdit（qlineedit.cpp:690 附近）；QLabel::sizeForWidth（qlabel.cpp:554-573 经 control 取宽高） |
| **右键菜单的弹出与生命周期** | QLineEdit::contextMenuEvent → `d->createStandardContextMenu` + `popup` + DeleteOnClose（qlabel.cpp:845-861 同款）；菜单动作灰化读取控制状态 |
| **焦点策略与焦点语义** | QLineEdit::focusInEvent Tab 聚焦全选/掩码跳首个空位、focusOutEvent 失焦 `deselect()`、按 `d->edited && (hasAcceptableInput()||fixup())` 决定 `editingFinished`（qlineedit.cpp:1896-1970）；QLabel::focusOutEvent 除 ActiveWindow/Popup 原因外清选区（qlabel.cpp:879-894） |
| **鼠标事件的壳级部分** | 三元点击检测（QLineEditPrivate `tripleClickTimer` qlineedit_p.h:175-176）、拖放 dndTimer（qlineedit_p.h:208-209）、软件输入面板；命中后的光标移动才 `control->moveCursor(d->xToPos(...), mark)` |
| **滚动联动** | QPlainTextEdit::scrollContentsBy → `d->setTopLine`、`ensureCursorVisible` 的滚动条数学（qplaintextedit.cpp:2199-2205；control 只提供 `ensureCursorVisible` 语义/`cursorRect`） |
| **信号转发/再发射** | QLabel 把控制器的 `linkActivated/linkHovered` 转接为自身信号（qlabel.cpp:1541-1544, :1560-1579） |
| **回显模式对 IME 的壳级特例** | QLineEdit::inputMethodEvent 在 `PasswordEchoOnEdit && !passwordEchoEditing` 时 `clear()` 并置 editing 状态（qlineedit.cpp:1788-1796）——注意：**判定用的状态位在控制器**，壳只做触发编排 |

### 1.4 关键判定口径（用于下文三列表）

- **密码回显定时器归控制器**：`m_passwordEchoTimer` 是 QWidgetLineControl
  成员，`setEchoMode`/`timerEvent` 都在控制器内取消/触发。壳只持有
  `d->edited`（是否编辑过）这类提交门禁，并在 focusIn/Out 编排显示刷新。
- **文本区绘制（正文/选区/光标）归控制器**：`control->draw()` 与
  `control->drawContents()`；壳只画 frame/面板/占位/side widget。
- **命中测试（px→光标）归控制器**：`xToPos`/`hitTest`/`cursorForPosition`；
  壳做坐标平移（contentsOffset/margins）后转发。
- **信号发射点在控制器**（textChanged/textEdited/selectionChanged/…），
  壳负own信号的公开标识与编辑完成类（editingFinished）的焦点门禁。

---

## 二、XLineEdit：整体搬移 + 委托化

### 2.1 现状能力面摘要（XLineEdit.c）

单字节缓冲 `m_text` + 字节偏移 `m_cursor/m_anchor` 的行内模型；撤销为
整文快照栈（深 20，:246-297）；掩码/校验/回显/补全/IME/菜单/剪贴板齐备；
无 preedit 组合文本（IME 直插提交串）、无光标闪烁、无密码回显定时器、
无双击选词（双击=全选）、无拖放。

### 2.2 三列表

#### 2.2.1 迁入控制器（现状行号 → 控制器承接点）

| # | 现有逻辑 | XLineEdit.c 行号 | 控制器承接（对照 Qt） |
|---|---|---|---|
| 1 | 文本缓冲与显示缓存（回显+掩码过滤） | `m_text` 重建 :905-908；`refreshDisplay` :497-534；`appendDisplayChar` :455-494；`displayText` :1809-1815 | `m_text/updateDisplayText/displayText`（Qt :208-224, :376） |
| 2 | 光标/选区存储与变更 + 信号发射 | `moveCursor` :747-770；`setSelectionRange` :773-795；选区判定 :226-241 | `moveCursor/internalDeselect` + `cursorPositionChanged/selectionChanged`（Qt :169, :382-386, :490-491） |
| 3 | 提交核心（含撤销压栈/modified/finishedPending/textEdited） | `setContent` :882-932 | `internalSetText/finishChange`（Qt :375, :419） |
| 4 | 插入（掩码过滤+maxLength 钳位+validator 拒绝） | `insertText` :938-1017；`filterInsert` :402-447；`insert` :1829-1834 | `insert/internalInsert` + `maskString` + validator 拒绝→`inputRejected`（Qt :236, :462, :692-741） |
| 5 | 删除（backspace/del/eraseRange） | `backspace` :2161-2174；`del` :2176-2189；`eraseRange` :1020-1033 | `backspace/del/internalDelete/_q_deleteSelected`（Qt :231-232, :512） |
| 6 | 撤销/重做栈 | `undoPush/undoClear/redoPush/redoClear` :246-297；`undo` :2324-2332；`redo` :2334-2342；`isUndo/RedoAvailable` :2314-2322 | `m_history/internalUndo/internalRedo`（Qt :440-450, :388-389） |
| 7 | 剪贴板 cut/copy/paste | :2346-2390（经共享 `XTextClipboard`） | `copy/paste`（Qt :155-158） |
| 8 | 输入掩码解析/校验/满足判定 | `maskNextEntry` :305-327；`maskEntryN` :330-343；`maskCharMatches` :346-368；`maskSatisfied` :371-396；`hasAcceptableInput` :2438-2450；`setInputMask` :2421-2436 | `parseInputMask/isValidInput/hasAcceptableInput`（Qt :459-465, :282-283） |
| 9 | 校验器持有与拒绝路径 | `setValidator` :1958-1964；validator 调用 :1003-1011, :2444-2448 | `m_validator/fixup`（Qt :267-269, :418） |
| 10 | maxLength 钳位 | `setMaxLength` 截断 :1886-1912；插入钳位 :964-990 | `setMaxLength`（Qt :258-265） |
| 11 | 回显模式状态与切换语义（清选区+光标到尾） | `setEchoMode` :1869-1881 | `setEchoMode`（Qt :242-256，另含密码定时器取消——见 2.2.3 增量 #1） |
| 12 | 键盘事件→编辑动作分派 | `VXLineEdit_keyPressEvent` :1060-1161 | `processKeyEvent`（Qt :341, .cpp:1605） |
| 13 | IME 提交串插入 | `VXLineEdit_inputMethodEvent` :1204-1217 | `processInputMethodEvent/commitPreedit`（Qt :340, :222） |
| 14 | 补全器同步（内联补全写回） | `xlineedit_syncCompleter` :799-871（由 `setContent` :931 触发）；`setCompleter` :2485-2509 | `m_completer/complete(int)`（Qt :272-277） |
| 15 | 命中测试 px→光标 | `posToCursor` :708-743；`cursorPositionAt` :2087-2093 | `xToPos`（Qt :191, .cpp:366） |
| 16 | 光标矩形 | `XLineEdit_cursorRect` :2028-2061 | `cursorRect/rectForPos`（Qt :192-194） |
| 17 | 词移动（词边界语义） | `cursorWordForward/Backward` :2129-2159 + `isSpace` :173-176 | `cursorWordForward/Backward/selectWordAtPos`（Qt :185-186, :240） |
| 18 | 文本区绘制（正文/选区分段/光标竖线） | paintEvent 的文本部分 :1402-1507（`splitDisplay` :541-580、选区分段绘制 :1428-1474、光标 :1481-1506） | `draw(...,DrawText/DrawSelections/DrawCursor)`（Qt :354-361；qlineedit.cpp:2100 由壳调用） |
| 19 | 编辑完成/回车信号发射点 | Return 分支 :1138-1145；focusOut :1295-1299 | 控制器 Return→`accepted()/editingFinished()`（Qt .cpp:1650-1659）；失焦门禁留在壳（对照 2.2.2 #4） |
| 20 | 标准菜单的动作槽与灰化条件 | `xlineedit_menuOp*` :2583-2657；`allSelected` :2575-2580 | `_q_deleteSelected` 等私有槽与 `allSelected`（Qt :118, :512；灰化读控制状态） |

#### 2.2.2 保留控件侧（判定 + 理由）

| # | 保留项 | 行号 | 判定与理由 |
|---|---|---|---|
| 1 | frame/面板/样式绘制（含 Fusion 分支与焦点框） | :1360-1400 | 保留。Qt 由 QLineEdit::paintEvent 画 `PE_PanelLineEdit`（qlineedit.cpp:1981-1984）；控制器只画文本区 |
| 2 | 内置 action 槽存储、命中与绘制 | 存储 :1934-1945；命中 :651-672；绘制 :675-706；mousePress 分派 :1241-1248 | 保留。Qt 中 action/side widget 属 QLineEditPrivate（qlineedit.cpp:455-485）；控制器不认识 widget 概念 |
| 3 | 清除按钮（绘制+命中+点击清空） | 绘制 :1509-1528；命中 :1250-1259 | 保留（按钮本体）。但「清空=一次用户编辑」的提交应走控制器 `clear` 路径（Qt 的清除按钮也是 QAction→`QLineEdit::clear`→control） |
| 4 | 失焦/回车的 `editingFinished` 门禁（`m_finishedPending`+readOnly 校验门禁可后补） | :1290-1303, :1138-1145 | 保留在壳。Qt 的 `editingFinished` 失焦发射在 QLineEdit::focusOutEvent，由 `d->edited`+`hasAcceptableInput()/fixup()` 决定（qlineedit.cpp:1951-1962）；控制器提供 `hasAcceptableInput/fixup` 能力，焦点编排归壳。Return 路径 Qt 实际由控制器 `processKeyEvent` 发射——迁移时二选一并在 4.2 风险中说明 |
| 5 | 焦点策略（ClickFocus）、焦点内刷新回显重绘 | focusIn :1278-1286；`g_focusedLineEdit` :1238, :2552-2555 | 保留。`g_focusedLineEdit` 是平台层 IME 直投的壳级登记点（XLineEdit.h:794-795），控制器无 widget 身份不能登记；PasswordEchoOnEdit 的 focusIn/Out 刷新回显对应 Qt 壳侧 `updatePasswordEchoEditing` 编排（qlineedit.cpp:1896-1912） |
| 6 | 尺寸提示与同步 | `sizeHint` :1969-2009；`minimumSizeHint` :2010-2027；`updateSizeHints` :1045-1055 | 保留。布局职责属壳（Qt qlineedit.cpp:690）；控制器仅提供文本度量（`naturalTextWidth`） |
| 7 | 水平滚动偏移 `m_viewOffset` | `updateViewOffset` :585-623；绘制消费 :1426, :1496 | 判定：**语义迁控制器、像素守恒留壳**。Qt 的 `cursorToX` 在控制器（QTextLayout），壳做 `SE_LineEditContents` 内容矩形与滚动裁剪。当前实现两者揉在一起，迁移时把「光标→X」下沉，`viewOffset` 钳位逻辑留在壳的绘制/滚动联动 |
| 8 | 文本边距 `m_textMargins` | :2452-2481 | 保留。对应 Qt `setTextMargins`（壳属性，参与 contents 矩形计算） |
| 9 | alignment/frame/占位文本 | :1913-1933, :1836-1852；占位绘制 :1493-1497 | 保留（属性+绘制）。占位绘制 Qt 也在壳（QLineEdit::paintEvent 内 `placeholderText` 分支）；但空文本判定读控制器 |
| 10 | 右键菜单事件的弹出与生命周期 | `VXLineEdit_contextMenuEvent` :1183-1201 | 保留。Qt: 菜单 popup/DeleteOnClose 在壳（qlabel.cpp:845-861 同构）；菜单动作槽指控制器操作 |
| 11 | 信号标识函数与发射辅助 | :179-221, :2513-2550 | 信号标识/发射属壳公开 API（XObject 挂在控件上）；控制器经回调/函数指针通知壳发射，或迁移后壳发射点退化为「读控制器状态 + emit」 |
| 12 | 拷贝/移动/析构的资源管理 | deinit :1545-1576；copy :1579-1631；move :1634-1712 | 保留但收缩：文本/撤销栈/显示缓冲的**所有权迁控制器**后，壳这几处只剩控制器指针的释放/借用拷贝 |
| 13 | 鼠标按下中的取焦点与 `XEvent_accept` 编排 | :1236-1238, :1260-1262 | 保留。焦点与事件接受是 widget 行为；命中计算调控制器 `xToPos` 等价物 |

#### 2.2.3 控制器必须新增（Qt 有、壳无，迁移时的增量）

| # | 增量能力 | Qt 佐证 | XGui 现状 |
|---|---|---|---|
| 1 | **密码回显定时器状态机**（PasswordEchoEditing：输入后延时转掩码；`passwordMaskDelay`） | `m_passwordEchoTimer/m_passwordMaskDelay`（Qt :63-64, :470-480, .cpp:352, :1543） | 无定时器：Password 恒显 `*`，PasswordEchoOnEdit 仅按焦点切换（`appendDisplayChar` :464-466） |
| 2 | **光标闪烁**（blink 定时器 + blinkStatus） | `m_blinkTimer/setBlinkingCursorEnabled`（Qt :60-61, :343-347） | 光标焦点内常显（.h:19-20 注明「闪烁为后续扩展」） |
| 3 | **preedit 组合文本**（组合区、preedit 光标、组合中 del/点击的坐标修正） | `setPreeditArea/composeMode/commitPreedit`（Qt :305-310, :222）；双击的 preedit 修正（qlineedit.cpp:1613-1651） | IME 仅整串直插（:1204-1217）；.h:6-7 注明「中文 IME 组合为后续扩展」 |
| 4 | **双击选词 / 三元点击全选** | `selectWordAtPos`（Qt :240）；三元点击计时（qlineedit.cpp:1520-1527） | 双击=全选（:1266-1273 注明简化）；无三元点击 |
| 5 | **校验器 fixup 提交修复** | `fixup()`（Qt :283, .cpp:418-424）；`editingFinished` 门禁用它 | 仅 `hasAcceptableInput`（:2438-2450），无 fixup |
| 6 | **命令型撤销**（分命令合并连续输入/逐字符删除） | `Command/m_history/addCommand`（Qt :440-450） | 整文快照栈（:246-297），快照 20 份内存放大且无法合并 |
| 7 | **VisualMoveStyle 视觉移动** | `cursorForward` 按 `rightCursorPosition`（Qt :170-183） | 两风格行为一致，仅存储（.h:92-95, :2410-2414） |
| 8 | **RTL/布局方向**（`layoutDirection` 影响显示与移动） | Qt :325-336 | 无此概念 |
| 9 | **QInputControl 无障碍身份** | `QInputControl(LineEdit)` 继承 + `accessibleObject`（Qt :50, :84-95） | 无 |
| 10 | **剪贴板 Selection 模式（中键粘贴/主选区）** | `copy(QClipboard::Selection)`、中键粘贴（qlineedit.cpp:1666-1678） | 仅 Clipboard 模式（:2346-2390） |

### 2.3 公开 API 委托化改造表（XLineEdit.h:237-795 → 控制器）

记 `ctl = self->m_control`（迁移后字段）。无标记者=现实现整函数体迁入控制器，壳退化为 `return ctl->xxx(...)` 转发。

| 公开 API（.h 行号） | 改造 |
|---|---|
| `XLineEdit_text` :246 | `ctl->text()` |
| `XLineEdit_displayText` :257 | `ctl->displayText()`（控制器 refresh，壳不再持 `m_displayBuf`） |
| `XLineEdit_setText` :266 | `ctl->setText()`；`m_finishedPending`/信号由壳的 textChanged 转发链承接 |
| `XLineEdit_clear` :272 | `ctl->clear()` |
| `XLineEdit_insert` :282 | `ctl->insert()`（内部含掩码/长度/校验过滤） |
| `XLineEdit_placeholderText/setPlaceholderText` :284-286 | **保留壳**（占位属壳属性，Qt 同） |
| `XLineEdit_isReadOnly/setReadOnly` :291-293 | `ctl->isReadOnly()/setReadOnly()` |
| `XLineEdit_echoMode/setEchoMode` :295-304 | `ctl->echoMode()/setEchoMode()`（含密码定时器取消，Qt :243-256） |
| `XLineEdit_maxLength/setMaxLength` :306-314 | `ctl->maxLength()/setMaxLength()`（截断+textChanged 在控制器） |
| `XLineEdit_alignment/setAlignment` :316-318 | **保留壳**（绘制属性）；绘制时下发给控制器 |
| `XLineEdit_hasFrame/setFrame` :320-322 | **保留壳** |
| `XLineEdit_addAction` :384 | **保留壳**（side widget 体系） |
| `XLineEdit_cursorRect` :393 | `ctl->cursorRect()` + 壳做 contentsRect 偏移换算（现 :2028-2061 的 textStartX 部分留壳） |
| `XLineEdit_cursorPosition/setCursorPosition` :396-405 | `ctl->cursorPosition()/setCursorPosition()`（注意现实现对外是**字符索引**、内部字节偏移，换算层归壳还是控制器需定契约，见 4.4） |
| `XLineEdit_cursorPositionAt` :412 | 壳做 margin 偏移 → `ctl->xToPos()`（Qt 同构：`d->xToPos` 在壳私类做平移） |
| `XLineEdit_cursorForward/Backward/WordForward/WordBackward` :425-448 | `ctl->cursorForward()/cursorWord*()` |
| `XLineEdit_backspace/del` :456-464 | `ctl->backspace()/del()` |
| `XLineEdit_home/end` :471-478 | `ctl->home()/end()` |
| `XLineEdit_isModified/setModified` :487-494 | `ctl->isModified()/setModified()` |
| `XLineEdit_setSelection` :507 | `ctl->setSelection()` |
| `XLineEdit_hasSelectedText/selectedText/Start/End/Length` :513-538 | `ctl->hasSelectedText()/selectedText()/selectionStart()/selectionEnd()`（Length=End-Start 的字符数换算留壳或控制器，定一处） |
| `XLineEdit_deselect/selectAll` :545-552 | `ctl->deselect()/selectAll()` |
| `XLineEdit_isUndoAvailable/isRedoAvailable/undo/redo` :561-582 | `ctl->isUndoAvailable()/isRedoAvailable()/undo()/redo()` |
| `XLineEdit_cut/copy/paste` :594-610 | `ctl->cut()/copy()/paste()` |
| `XLineEdit_createStandardContextMenu` :624 | 菜单构建留壳（经 XTextMenu），灰化查询与动作槽改指 `ctl->…` |
| `XLineEdit_dragEnabled/setDragEnabled` :636-644 | **保留壳**（拖放 dndTimer 属壳，Qt qlineedit_p.h:208-209）；状态可镜像给控制器 `setDragEnabled` |
| `XLineEdit_cursorMoveStyle/setCursorMoveStyle` :650-658 | `ctl->cursorMoveStyle()/setCursorMoveStyle()`（增量 #7 落地后生效） |
| `XLineEdit_inputMask/setInputMask` :664-676 | `ctl->inputMask()/setInputMask()` |
| `XLineEdit_hasAcceptableInput` :684 | `ctl->hasAcceptableInput()`（+ 新增 `ctl->fixup()`） |
| `XLineEdit_setTextMargins/_2/textMargins` :692-706 | **保留壳** |
| `XLineEdit_setCompleter/completer` :725-731 | `ctl->setCompleter()/completer()`（补全器 setWidget 仍指向壳，Qt qlineedit.cpp:622-630 同构） |
| 七个 `*_signal` :744-792 | **保留壳**；发射点改由控制器经回调通知 |
| `XLineEdit_sizeHint/minimumSizeHint` :364-371 | **保留壳**；文本宽度向控制器查询 |
| `XLineEdit_focusedLineEdit` :795 | **保留壳**（IME 直投登记） |

---

## 三、XPlainTextEdit：模型升级型迁移（增量大于存量）

### 3.1 现状能力面摘要（XPlainTextEdit.c）

平铺 `XVector(char*)` 行数组模型（`xpe_lineAt` :40-49 等 5 个行工具
:40-100）；光标仅 `m_cursorLine/m_cursorCol`（.h:80-81），**无选区锚点**
——选区只有布尔 `m_selectionActive`（.h:89），`selectedText` 退化为
「当前行 [0,cursorCol)」（:1716-1736）；撤销为整文快照（`xpe_pushUndo`
:190-200），无 undoAvailable 信号发射体；无 Ctrl+A/C/X/V/Z 快捷键、无
Shift 扩展选区、无双击选词；剪贴板 copy 实为**全文复制**
（`xpe_selectedAllText` :1088-1091）；光标移动不发射 cursorPositionChanged
（信号函数存在但无发射点 :1170-1174）。

### 3.2 三列表

#### 3.2.1 迁入控制器（现状行号 → 控制器承接点）

| # | 现有逻辑 | XPlainTextEdit.c 行号 | 控制器承接（对照 Qt） |
|---|---|---|---|
| 1 | 行存储/行编辑原语 | `xpe_lineAt/lineCount/setLine/insertLineAt/removeLineAt` :40-100；拆行重建 `xpe_applyTextNoUndo` :718-752 | QTextDocument/块结构（控制器持文档，Qt qwidgettextcontrol_p.h:71-72） |
| 2 | 文本读写 | `setPlainText` :754-759；`toPlainText` :761-782；`appendPlainText` :784-802；`insertPlainText` :804-810；`clear` :812-816 | 控制器槽 `setPlainText/toPlainText/append/appendPlainText/insertPlainText/clear`（Qt :163-189） |
| 3 | 撤销/重做快照栈 | `xpe_pushUndo` :190-200；`xpe_canUndo/canRedo` :167-182；`undo` :1056-1072；`redo` :1074-1086 | 控制器 `undo/redo` + `undoAvailable/redoAvailable` 信号（Qt :176-177, :195-196） |
| 4 | 编辑原语（插入/分行/退格/删除） | `xpe_insertAtCursor` :225-241；`xpe_splitLineAtCursor` :243-261；`xpe_backspace` :263-294；`xpe_deleteChar` :296-320 | 控制器经文档光标完成（Qt :182-187） |
| 5 | 键盘→编辑分派 | `VX_plainTextEdit_keyPressEvent` :393-485 | `processEvent(KeyPress)`（Qt :218-219） |
| 6 | IME 提交 | `VX_plainTextEdit_inputMethodEvent` :356-368 | 控制器 IME 路径（Qt :226, :155 `isPreediting`） |
| 7 | 命中测试（含滚动换算） | `cursorForPosition` :1416-1461；mousePress :324-341 调用 | `hitTest/cursorForPosition`（Qt :105, :159；壳做 `mapToContents` 平移，qplaintextedit.cpp:1380, :2417） |
| 8 | 光标矩形 | `cursorRect` :937-967 | `cursorRect()`（Qt :106-107） |
| 9 | 查找 | `findFirstInLine/findLastInLine` :902-935；`find` :978-1027 | 控制器 `find`（Qt :85-88） |
| 10 | 剪贴板 | `copy` :1093-1103（全文）；`cut` :1105-1110；`paste` :1112-1118；`canPaste` :1262-1263 | 控制器 `cut/copy/paste/canPaste` + MIME 族（Qt :144, :170-174, :228-230） |
| 11 | 选区状态机（现单布尔） | `xpe_setSelectionActive` :155-160（唯一入口）；`selectAll` :1120-1127；`hasSelectedText` :1711-1714；`selectedText` :1716-1736 | QTextCursor 锚点/位置模型 + `selectionChanged/copyAvailable`（Qt :199, :198） |
| 12 | 块数上限钳位 | `xpe_afterChange` :202-221（前半 :206-216） | Qt 由文档 `maximumBlockCount` 承载（控制器文档层） |
| 13 | 块数/字符统计 | `blockCount` :1256-1260；`characterCount_2` :1197 | `blockCountChanged` 载荷（Qt :205） |
| 14 | 覆盖模式/光标宽度/交互标志的语义位 | `overwriteMode` :1301-1305；`cursorWidth` :1265-1269；`textInteractionFlags` :1313-1317 | 全部是控制器属性（Qt :57-61, :118-122, :77-78） |
| 15 | `moveCursor` 操作分派 | :1346-1370 | 控制器 `moveCursor(op, mode)`（Qt :142） |
| 16 | 标准菜单动作槽与灰化 | `xpe_menuOp*` :1479-1549 | 控制器 `createStandardContextMenu`（Qt :102；QPlainTextEdit 直通 qplaintextedit.cpp:2391） |

#### 3.2.2 保留控件侧（判定 + 理由）

| # | 保留项 | 行号 | 判定与理由 |
|---|---|---|---|
| 1 | 滚动区机制与内容尺寸联动 | `xpe_afterChange` 后半 `setContentSize` :217-218；`scrollContentsBy` :583-595 | 保留。QAbstractScrollArea 职责在壳（Qt `scrollContentsBy→setTopLine`，qplaintextedit.cpp:2199-2205）；控制器发 `documentSizeChanged/updateRequest`，壳换算滚动条 |
| 2 | 视口背景/边框/占位绘制 | paintEvent 背景+凹陷框 :516-535；占位 :545-550 | 保留。Qt 壳画视口与占位（qplaintextedit.cpp:1887 起, :1913）；正文的逐行绘制迁移后改为消费控制器的可见块（`control->document()` 布局 draw，qplaintextedit.cpp:1887 注释路径），但绘制调用本身留在壳 paintEvent |
| 3 | 光标绘制的调用点 | :557-561 | 保留调用、矩形取自控制器 `cursorRect`（现状已如此 :558） |
| 4 | `ensureCursorVisible` 的滚动条数学 | :1129-1139 | 保留在壳。Qt 由壳的滚动机制完成（qplaintextedit.cpp:2219 ensureCursorVisible 配合 `d->vbar`）；控制器只报 `visibilityRequest`（Qt :206） |
| 5 | 焦点策略 StrongFocus 与焦点重绘 | init :694；`VX_plainTextEdit_focusEvent` :344-352 | 保留。焦点策略是 widget 属性（Qt qplaintextedit.cpp:790 同款注释） |
| 6 | 行高常量与度量口径 | `XPE_LINE_HEIGHT` :38；cursorRect/绘制/cursorForPosition 三处共用 | 迁移后度量口径应随控制器文本布局走；在控制器提供统一行几何前，壳保留常量作为绘制口径（一处口径原则，见 4.5） |
| 7 | 换行/字体/缩放属性 | `lineWrapMode` :829-839；`wordWrapMode` :1307-1311；`zoomIn/Out` :1698-1707 | 保留壳（属性壳），效果下发：换行/字宽进控制器文档选项（Qt `setTextWidth`/QTextOption），zoom 改壳字体→changeEvent→控制器 `setDefaultFont`（qplaintextedit.cpp:2299 同构） |
| 8 | document 借用接口 | `document/setDocument` :1591-1615 | 壳保留 API 面；所有权迁控制器（Qt 文档归 control，qplaintextedit.cpp:1282-1292），壳 API 转发 |
| 9 | `updateRequest` 信号 | :1147-1168（发射点 :583-595） | 保留壳信号标识；迁移后发射源改为控制器的 `updateRequest`（Qt :203）转发 |
| 10 | 上下文菜单弹出/生命周期 | `VX_plainTextEdit_contextMenuEvent` :374-391 | 保留（同 XLineEdit 判定 #10） |
| 11 | rich 子集扩展点（appendHtml 剥标签、extraSelections 承载、loadResource 恒 NULL） | :1372-1385；:1617-1660 | 保留现状签名；语义位（extraSelections 存储）迁控制器后壳转发（Qt :127-130 在控制器） |
| 12 | `anchorAt` 恒空实现 | :969-976 | 保留壳转发；迁移后可改查控制器 `anchorAt`（Qt :111） |
| 13 | documentTitle / charFormat 位值 | :1319-1344, :1577-1588 | charFormat 语义迁控制器（Qt :80-83）；documentTitle 保留壳 |

#### 3.2.3 控制器必须新增（Qt 有、壳无——本控件的主要工作量）

| # | 增量能力 | Qt 佐证 | XGui 现状 |
|---|---|---|---|
| 1 | **选区锚点/位置对**（可跨行、方向化、拖选） | QTextCursor 模型 + `selectionChanged/copyAvailable`（Qt :198-199） | 单布尔 `m_selectionActive`（.h:89）；copy 实为全文复制（:1088-1103） |
| 2 | **鼠标拖选/双击选词/三元点击** | `processEvent` 内 `selectWordAtPos` 等词选择 + `isWordSelectionEnabled`（Qt :152-153） | mousePress 仅定位光标（:324-341）；无双击处理 |
| 3 | **键盘选区扩展**（Shift+方向/Home/End、Ctrl+A 已有但语义是单布尔置位） | 控制器 processEvent 的 move+KeepAnchor | keyPress 全部无 mark 参数（:441-483）；`selectAll` 复位光标到 (0,0)（:1120-1127）导致 `selectedText` 恒 NULL（.h:498-501 自述） |
| 4 | **快捷键编辑族**（Ctrl+C/X/V/Z/Y） | 控制器 processEvent | keyPress 无 Ctrl 分支（:393-485） |
| 5 | **真信号发射体**：undoAvailable/redoAvailable/copyAvailable/modificationChanged/blockCountChanged/cursorPositionChanged | Qt :193-211 全部真发射 | 五个 signal 函数只返回标识（:1387-1400）；cursorPositionChanged 无发射点（:1170-1174）；modificationChanged 无 modified 置位点（init :689 置 false 后无人置真） |
| 6 | **命令型撤销/undo 分组** + `isUndoRedoEnabled=false` 语义 | Qt QUndoStack 体系 | 快照栈且 `setUndoRedoEnabled(false)` 只是不压栈（:190-194），不清栈不冻结 |
| 7 | **滚动跟随光标**（centerOnScroll/centerCursor 生效逻辑）+ 垂直方向 ensure | `ensureCursorVisible`（控制器 :98）+ `visibilityRequest` | `m_centerCursor/m_centerOnScroll` 仅存储（:1271-1281）；`ensureCursorVisible` 只把行顶对齐视口顶（:1129-1139） |
| 8 | **overwrite 模式插入语义** | `overwriteMode`（Qt :118-119） | 字段仅存储（:1301-1305），插入仍为纯插入 |
| 9 | **Tab 键语义**（tabChangesFocus / 制表符插入 / tabStopDistance 生效） | qplaintextedit.cpp:2098（Tab 可编辑时插入而非切焦点） | `m_tabChangesFocus/m_tabStopDistance` 仅存储（:1289-1299）；keyPress 对 Tab（key=9）落入 default 忽略（:480-482） |
| 10 | **输入法查询**（inputMethodQuery：光标矩形/环绕文本等，供平台 IME 定位） | 控制器 `inputMethodQuery`（Qt :226）+ 壳转发（qplaintextedit.cpp:2240） | 无 inputMethodQuery；仅 inputMethodEvent 提交路径 |
| 11 | **拖放**（dragEnter/drop→insertFromMimeData、拖动自动滚动） | qplaintextedit.cpp:2169-2203 | 无 |
| 12 | **水平滚动**（长行 NoWrap 时横向滚动联动） | 控制器 `documentSizeChanged` + 壳横向条 | `xpe_afterChange` 只设高度（:217-218）；长行被裁剪不可达 |

### 3.3 公开 API 委托化改造表（XPlainTextEdit.h:117-543 → 控制器）

| 公开 API（.h 行号） | 改造 |
|---|---|
| `setPlainText` :122 | `ctl->setPlainText()` |
| `toPlainText` :127 | `ctl->toPlainText()` |
| `appendPlainText` :131 | `ctl->appendPlainText()` |
| `insertPlainText` :135 | `ctl->insertPlainText()` |
| `clear` :139 | `ctl->clear()` |
| `isReadOnly/setReadOnly` :146-150 | `ctl->…`（readOnly 是控制器的交互标志语义，Qt qplaintextedit.cpp:42-44 经 control 判定） |
| `lineWrapMode/setLineWrapMode` :154-158 | **保留壳**，换行实施参数下发控制器文档 |
| `maximumBlockCount/setMaximumBlockCount` :162-166 | 文档层参数迁控制器（`ctl->document()` 语义）；壳转发 |
| `setPlaceholderText/placeholderText` :170-174 | **保留壳**（壳绘制） |
| `isUndoRedoEnabled/setUndoRedoEnabled` :178-182 | `ctl->…` |
| `cursorLine/cursorColumn` :186-190 | `ctl->textCursor()` 解包（承载层换算留壳） |
| `cursorRect` :204 | `ctl->cursorRect()` + 壳滚动偏移换算（现 :937 已含 vsb，迁后 vsb 换算留壳） |
| `anchorAt` :216 | `ctl->anchorAt()`（当前恒空，迁移后语义就位） |
| `find` :232 | `ctl->find()` |
| `setTextCursor/textCursorLine/textCursorColumn/textCursor` :245-259, :460 | `ctl->setTextCursor()/textCursor()` |
| `undo/redo` :263, :301 | `ctl->undo()/redo()` |
| `blockCount` :267 | `ctl->blockCount()` |
| `canPaste` :268 | `ctl->canPaste()`（现状 !readOnly，迁后 = readOnly+剪贴板，Qt :144） |
| `setCursorWidth/cursorWidth` :269-270 | `ctl->…` |
| `setCenterCursor/centerCursor` :271-272 | **保留壳**（滚动联动参数），生效逻辑消费控制器 `visibilityRequest` |
| `setCenterOnScroll/centerOnScroll` :273-274 | 同上 |
| `setBackgroundVisible/backgroundVisible` :275-276 | **保留壳**（视口绘制参数） |
| `setTabChangesFocus/tabChangesFocus` :277-278 | **保留壳**（焦点策略判定在壳，qplaintextedit.cpp:2097-2100 同构）；Tab 插入语义进控制器 |
| `setTabStopDistance/tabStopDistance` :279-280 | 下发控制器文档选项；壳保留 API |
| `setOverwriteMode/overwriteMode` :281-282 | `ctl->…` |
| `setWordWrapMode/wordWrapMode` :283-284 | 下发控制器文档选项 |
| `setTextInteractionFlags/textInteractionFlags` :285-286 | `ctl->…` |
| `setDocumentTitle/_2/documentTitle` :287-289 | **保留壳**（文档元数据壳属性） |
| `moveCursor` :290 | `ctl->moveCursor()`（operation 枚举对齐 QTextCursor::MoveOperation 数值需映射） |
| `appendHtml` :291 | 暂保留壳剥标签（富文本子集）；远期 `ctl->appendHtml()` |
| 五个 bool/int 信号 :292-296 | **保留壳标识**；发射体改由控制器对应信号驱动（增量 #5） |
| `copy/cut/paste/selectAll` :305-317 | `ctl->…` |
| `ensureCursorVisible` :321 | 保留壳滚动数学（判定 #4），光标矩形取控制器 |
| `cursorForPosition` :337 | 壳做 contents 平移 → `ctl->cursorForPosition()`（Qt qplaintextedit.cpp:2417 同构） |
| `createStandardContextMenu` :354 | 菜单构建留壳（XTextMenu），动作槽/灰化改指控制器（Qt 由 control 直接创建，qplaintextedit.cpp:2391；XGui 因 C 无 QObject 层保持壳构建） |
| `currentCharFormat/setCurrentCharFormat/mergeCurrentCharFormat` :364-384 | `ctl->…` |
| `document/setDocument` :397-409 | 壳转发；文档所有权迁控制器 |
| `extraSelections/setExtraSelections` :422-436 | `ctl->…`；绘制联动留在壳 paintEvent 消费 |
| `loadResource` :449 | `ctl->loadResource()`（暂恒 NULL） |
| `zoomIn/zoomOut` :470-479 | **保留壳**（字体属壳），字体变更事件下发控制器 |
| `hasSelectedText/selectedText` :491-506 | `ctl->hasSelection()/selectedText()`（增量 #1 落地后语义修正） |
| `textChanged/cursorPositionChanged/updateRequest/selectionChanged` 信号 :513-543 | **保留壳标识**；`cursorPositionChanged` 需接控制器发射（现无发射点） |

---

## 四、XLabel：选择交互迁控制器的对齐迁移

> 范围：仅选择/链接交互链路（XLabel.c:1100-1905 中的几何映射、拖选、
> 双击选词、键盘扩展、失焦清除、链接命中与信号）；内容选择（文本/图/
> 影片）、布局与边距、尺寸提示不在迁移面内。Qt 对照：QLabel 可交互时
> 持 `QWidgetTextControl`，鼠标/键盘/焦点事件 `sendControlEvent` 直通
> （qlabel.cpp:820-916），选区 API 读写控制器的 textCursor
> （qlabel.cpp:700-777），link 信号由控制器转接（qlabel.cpp:1541-1544）。

### 4.1 三列表

#### 4.1.1 迁入控制器（现状行号 → 控制器承接点）

| # | 现有逻辑 | XLabel.c 行号 | 控制器承接（对照 Qt） |
|---|---|---|---|
| 1 | 拖选状态机（anchor/pos→start/length） | `label_selectFromAnchor` :1418-1439；mousePress 置锚 :1689-1695；mouseMove 扩展 :1746-1752；mouseRelease 收尾 :1712-1719 | QWidgetTextControl processEvent 的鼠标选区路径（Qt :218-219） |
| 2 | 双击选词（词字符分类 + 词扩展 + 锚点落词首语义） | `label_isWordUnit` :1445-1461；`label_selectWordAt` :1467-1504；双击入口 :1780-1801 | 控制器词选择（Qt `isWordSelectionEnabled` :152-153；selectWord 对应 QTextControl::selectWord，XLabel 注释 :1463 自认对标） |
| 3 | 命中映射 px→UTF-16 偏移（含布局反查/行裁剪/半宽落点） | `label_posToUtf16` :1319-1415（布局反查经 `label_computeLayout` :1022）；辅助 `label_glyphCountForRange` :1287-1298、`label_glyphIndexToUtf16` :1301-1316 | `hitTest/cursorForPosition`（Qt :105, :159） |
| 4 | 键盘扩展选择（方向/Home/End/Ctrl+A、Shift 语义、光标端推导） | `VXLabel_keyPressEvent` :1804-1870；光标端推导 `label_currentCursor` :1507-1521 | 控制器 processEvent 键盘路径 + `moveCursor(MoveOperation, KeepAnchor)`（Qt :142） |
| 5 | 程序化选区（钳位、无选择=−1 哨兵、不满足交互条件拒绝） | `XLabel_setSelection` :2487-2517 | `setTextCursor` 承载（Qt qlabel.cpp:700-711） |
| 6 | 失焦清除选区（ActiveWindow/Popup 豁免） | `VXLabel_focusOutEvent` :1881-1905 | 同一规则在 QLabel::focusOutEvent 以 cursor.clearSelection 实现（qlabel.cpp:879-894）；迁移后判定读控制器光标、清除写回控制器 |
| 7 | 选区绘制判定（逐字 selected 高亮） | `label_drawLine` 的 selected 计算 :1125-1126、反色填充 :1134-1140；selStart/selEnd 解析 :1167-1168 | 控制器提供选区区间，绘制仍由壳调用（Qt 的 `getPaintContext`（Qt :161）承载 selection 颜色，drawContents 消费）——**状态迁、绘制调用留壳**（见 4.1.2 #1） |
| 8 | 链接命中（布局反查+字节区间匹配） | `label_hitLinkAt` :1200-1282 | 控制器 `anchorAt/hitTest`（Qt :111, :159） |
| 9 | 链接交互状态（按下链接索引、悬停链接索引、hover 进入/离开发射） | `m_pressedLink/m_hoverLink` 维护 :1672-1683, :1721-1733, :1761-1776；LEAVE/HIDE 清理 `VXLabel_event` :1596-1612 | 控制器 linkActivated/linkHovered 发射（Qt :208-209；QLabel 转接 qlabel.cpp:1541-1544） |

#### 4.1.2 保留控件侧（判定 + 理由）

| # | 保留项 | 行号 | 判定与理由 |
|---|---|---|---|
| 1 | 内容绘制入口 drawContents/drawContent 与 drawTextContent | :2556-2571（drawContents）；`label_drawContent` :1565-1591；`label_drawTextContent` :1155-1195 | 保留。绘制调用属壳（Qt QLabel 的 paintEvent→drawContents 路径）；控制器只被查询选区/链接区间与颜色 |
| 2 | 文本布局（computeLayout/lineX/lineHeight/ascent/advance、wordWrap 断行） | `label_computeLayout` :1022 起；绘制消费 :1164, :1180-1193 | 保留（近期）。Qt 布局在 QTextDocument 布局层（控制器侧）；但 XLabel 的显示模型（剥标记后的 m_displayText+链接表）与选择交互解耦良好，布局可后续随富文本升级再定归属。**选择链路迁移不强制搬布局**，posToUtf16/hitLinkAt 迁移时以回调方式取壳布局行盒 |
| 3 | 文本格式解释/富文本剥标签/链接表构建 | setText 系（:665 附近的 init 复位；`label_updateLabel` :2036-2049；`label_updateMouseTracking` :2052-2061） | 保留。内容生成属壳内容选择职责；控制器只在交互时消费 displayText 与 links（Qt QLabel 也是把 control 作用在已生成的文档上，qlabel.cpp:234-256） |
| 4 | 交互标志→焦点策略映射 | `XLabel_setTextInteractionFlags` :2456-2483（StrongFocus/ClickFocus/NoFocus 推导 + 不可选时清选区） | 保留。焦点策略是 widget 属性（Qt qlabel.cpp:673-681 壳侧调 control 之外还改自身 focusPolicy） |
| 5 | 事件 accept/ignore 与取焦点编排 | mousePress :1694-1697；双击 :1795-1797 | 保留（同 XLineEdit 判定 #13） |
| 6 | linkActivated/linkHovered 信号标识与发射 | :2544-2556；`label_emitHoverLeave` :311 | 保留壳公开信号；发射源改为控制器信号转发（Qt 转接同构，qlabel.cpp:1541-1579） |
| 7 | 悬停手型光标设置/还原 | :1764-1774, :1605 | 保留。QCursor 是 widget 侧概念 |
| 8 | copy/move/deinit 的选区字段管理 | copy :1938-1943；move :1997-2002, :2014-2020；deinit :2024-2031 | 保留但收缩：选区/交互状态迁控制器后仅存控制指针 |
| 9 | sizeHint/minimumSizeHint/heightForWidth | :2397 起（sizeHint）；`label_updateLabel` :2036-2049 | 保留。布局职责属壳（Qt QLabelPrivate::sizeForWidth） |

#### 4.1.3 控制器必须新增（Qt 有、壳无）

| # | 增量能力 | Qt 佐证 | XGui 现状 |
|---|---|---|---|
| 1 | **LinksAccessibleByKeyboard 键盘链路**（Tab 焦点锚点循环、focusNextPrevChild 桥） | `setFocusToNextOrPreviousAnchor/findNextPrevAnchor`（Qt :232-234；qlabel.cpp:898-906） | 枚举位已定义未实现（XLabel.h:95「未实现的受限项」）；keyPress 无锚点分支（:1829-1851） |
| 2 | **TextEditable 路径**（标签内编辑） | 控制器可编辑 + TextEditable 标志（Qt :60, qlabel.cpp:42） | 枚举位已定义未实现（XLabel.h:96） |
| 3 | **选区上下文菜单**（含 Unicode 控制字符子菜单的 Qt 全量；XGui 至少对齐 QPlainTextEdit 的六项子集） | `createStandardContextMenu`（Qt :102；QUnicodeControlCharacterMenu :248-261） | XLabel 无 contextMenuEvent/标准菜单（对比 XLineEdit.c:1183 已有） |
| 4 | **复制选区**（TextSelectableBy* 下的 Ctrl+C/copy()） | QLabel 可选中时 context menu/copy 走控制器（Qt :170-174） | XLabel 无 copy；剪贴板链路缺失 |
| 5 | **drag 拖出选中文本**（isDragEnabled） | `setDragEnabled`（Qt :149-150） | 无 |
| 6 | **cursorIsFocusIndicator / 词选择开关等控制属性** | Qt :146-153 | 无对应 |
| 7 | **IME/preedit**（TextEditable 落地时） | `isPreediting`（Qt :155） | 无 |

### 4.2 公开 API 委托化改造表（XLabel 选择/链接面）

| 公开 API（XLabel.h 行号） | 改造 |
|---|---|
| `XLabel_setSelection` :408 | `ctl->setSelection(start,len)`（承载换算：UTF-16 码元 ↔ 控制器光标位置，定在壳适配层） |
| `XLabel_hasSelectedText` :410 | `ctl->hasSelection()` |
| `XLabel_selectedText` :416 | `ctl->selectedText()`（返回 XString 承载） |
| `XLabel_selectionStart` :418 | `ctl->textCursor().selectionStart()` 承载 |
| `XLabel_textInteractionFlags` :387 | 读壳镜像（写时已同步控制器） |
| `XLabel_setTextInteractionFlags` :393 | 壳改 focusPolicy（保留）+ `ctl->setTextInteractionFlags()` |
| `XLabel_linkActivated_signal/linkHovered_signal` :427-433 | **保留壳**；连接控制器的 link 信号转发（Qt qlabel.cpp:1541-1544 同构） |
| `XLabel_drawContents` :446 | **保留壳**；内部选区/链接颜色区间查询控制器 getPaintContext 等价物 |

---

## 五（总）、迁移风险点

### 5.1 密码回显状态机（XLineEdit，最高风险）

- 现状语义分散在两处：`appendDisplayChar` 按焦点判定 PasswordEchoOnEdit
  （:464-466，读取 `XWidget_hasFocus`——**控制器无 widget 身份**，迁移后
  该判定必须改为壳把「焦点/编辑中」状态推给控制器（Qt 的
  `updatePasswordEchoEditing` 由壳在 focusIn/Out/IME 路径调用，
  qlineedit.cpp:1896-1912, :1788-1796），控制器持状态位与定时器）。
- 切回掩码的时机由「定时器到期」与「失焦」两个源驱动（Qt .cpp:1543-1545,
  :1896-1899）。XGui 事件循环若无常驻定时器设施，需先确认
  XWidget/控制器可挂 timerEvent（QWidgetLineControl 继承 QObject 才有
  timerEvent；XGui 控制器不是 XWidget，须显式提供定时器宿主）。
- `setEchoMode` 的「取消定时器 + 复位 editing 位」（Qt :243-256）必须
  随语义一起迁移，否则 PasswordEchoOnEdit 残留 editing 态导致永久明文。
- IME 与密码交互：Qt 在 PasswordEchoOnEdit 且未 editing 时收到 IME 事件
  会 **clear() 全文**并转 editing（qlineedit.cpp:1788-1796）。XGui 现
  IME 直插不 clear（:1204-1217）；控制器化时若补齐该语义，注意与
  `m_finishedPending`/undo 栈的一致性。

### 5.2 校验器拒绝路径（XLineEdit）

- 拒绝点有三类消费方：键盘输入（:1155-1157 发 inputRejected）、粘贴
  （:2387-2389）、IME（经 `XLineEdit_insert` :1829 → 无 rejected 发射，
  与键盘路径**不一致**，迁移时统一为控制器内单点拒绝发射，避免三处
  各自 emit 的回归）。
- validator 回调签名带 `XLineEdit* self`（XLineEdit.h:120-121）：控制器
  内部调用时 `self` 仍是壳指针——需保证迁移期间壳指针生命周期覆盖
  控制器调用，或在控制器回调适配层转发（文档已约定借用语义，风险可控
  但必须在接口冻结时注明）。
- `hasAcceptableInput` 在 :2441 要求**非空文本**才可能为 true，而 Qt 对
  空文本+无掩码时 validator 可能返回 Acceptable；`editingFinished` 门禁
  若改用控制器版本，注意空串语义回归（对照 qlineedit.cpp:1957-1962 的
  `d->edited && (hasAcceptableInput() || fixup())`）。
- Intermediate 态：XGui 仅区分 Invalid 拒绝（:1003-1011），Intermediate
  与 Acceptable 都放行且 `hasAcceptableInput` 要求 Acceptable——控制器
  迁移若照搬 Qt「Intermediate 允许编辑」，回车提交门禁与失焦门禁的
  判定要同步校准，防止「能编辑但永不 editingFinished」。

### 5.3 IME 提交（XLineEdit / XPlainTextEdit）

- 两壳的 IME 入口都依赖壳级全局焦点登记（`g_focusedLineEdit`
  XLineEdit.c:71, :1238；XPlainTextEdit 无登记，靠 widget 焦点链）。
  控制器化后**该登记必须留在壳**（平台层直投目标是 widget），控制器
  只提供 `insert/commitPreedit`。迁移中若把登记搬进控制器会造成平台
  层反向依赖（P0 禁区）。
- 提交串进入路径：XLineEdit 走 `insert`（含掩码/长度/校验过滤链），
  XPlainTextEdit 走 `insertPlainText`（无过滤、readOnly 才拦截 :365）。
  控制器统一后注意 XPlainTextEdit 侧 maxLength（maximumBlockCount）
  行为差异：Qt 行数上限裁剪**最旧行**（`xpe_afterChange` :206-216 已
  对齐），逐字符上限 Qt 用 maxLength 而 XGui 无对应——勿在控制器里
  混用两套钳位。
- 组合文本（preedit）是控制器增量（2.2.3 #3）：在 preedit 落地前，
  迁移不得改变「整串直插」的现网行为，否则中文输入回归。

### 5.4 其他迁移风险

1. **信号重复发射**：XLineEdit `setContent` 是 textChanged/selectionChanged/
   cursorPositionChanged 的现有发射点（:913-924）；控制器接管后壳若再在
   委托函数里 emit 会双发。约定：发射点唯一（控制器回调→壳 emit）。
2. **字节偏移 vs 字符索引双口径**：XLineEdit 内部字节偏移、公开 API 字符
   索引（`cursorPosition` :2063-2068 注释；`selectionLength` :2288-2298）；
   XPlainTextEdit 公开列即字节偏移（.h:80-81）。控制器统一存储口径时，
   三个壳的适配层必须各自维持对外契约，扫描/测试以现有 API 文档为准。
3. **深拷贝/移动语义**：XLineEdit 的 copy/move 手工复制撤销栈与缓冲
   （:1579-1712）；控制器化为对象指针后，copy 是「深拷控制对象」还是
   「新建空控制对象」要定死（Qt 控制器不参与 QWidget copy——QWidget
   不可拷贝，XGui 允许拷贝属自家扩展，建议 copy=值语义深拷、控制内
   定时器/焦点态不拷）。
4. **XPlainTextEdit 度量口径三处同源**：cursorRect（:937）、paintEvent
   光标（:557-561）、cursorForPosition（:1416）都依赖 `XPE_LINE_HEIGHT=16`
   与左留白 2px。控制器接管几何后若行高改随字体，三处必须一次切换，
   否则点击定位与光标绘制错位（XLineEdit 已有同教训：XLineEdit.c:143-147
   注释记录了旧 8px 估算导致中文错位）。
5. **XLabel 布局回调边界**：posToUtf16/hitLinkAt 依赖壳的
   `label_computeLayout`（:1022）。若控制器迁移一步到位，需先定义
   「控制器向壳请求行盒」的回调契约，或把布局一并下沉；分两步走时
   禁止控制器内重算一份布局（两份口径必然漂移）。
6. **`XPlainTextEdit_characterCount_2`（:1197）**：游离于头文件之外的
   杂项实现，迁移清理时先确认无调用方再处置（本次审计未在头文件中
   找到声明，属潜在死代码/未声明符号风险）。

---

## 附：共享层与落地顺序建议（迁移增量的装配图）

```
Src/XGui/Text/                      （已存在，控制器直接复用）
  XTextUtf8.{c,h}        ← 码点边界（XLineEdit.c:2097-2158、XPlainTextEdit.c:274,304,446-459 在用）
  XTextClipboard.{c,h}   ← 剪贴板往返+回退（XLineEdit.c:2346-2390、XPlainTextEdit.c:1093-1118 在用）
  XTextMenu.{c,h}        ← 标准菜单构建（两壳 createStandardContextMenu 在用）

新增（建议）Src/XGui/Text/ 或 Widget/ 内部：
  XLineControl  ← 承接 2.2.1 表 #1-#20；增量 2.2.3 #1-#10
  XTextControl  ← 承接 3.2.1 表 #1-#16 与 4.1.1 表 #1-#9；增量 3.2.3 / 4.1.3
                  （XLabel 与 XPlainTextEdit 共用一个 text control，对齐 Qt「also used by QLabel」）

壳的收敛结果：
  XLineEdit.c      ≈ 事件编排 + frame/side widget/占位/sizeHint + 信号转发 + 焦点门禁
  XPlainTextEdit.c ≈ 滚动联动 + 视口绘制 + 占位 + 字体/换行属性下发 + 信号转发
  XLabel.c         ≈ 内容选择与布局 + 绘制入口 + 焦点策略 + link 信号转发
```

> 复核入口：XLineEdit 委托化面最窄（API 表逐行即可），建议先做；
> XPlainTextEdit 的增量（3.2.3 #1-#7）依赖控制器选区/撤销体系成型后
> 再收口；XLabel 依赖 XTextControl 的 link/selection 面就位后收口。
