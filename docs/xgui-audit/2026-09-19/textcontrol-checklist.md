# QWidgetTextControl 完整行为验收清单(XTextControl 对拍基线)

- 审计对象:Qt 6.8.3 源码 `qtbase/src/widgets/widgets/qwidgettextcontrol_p.h`(286 行)、`qwidgettextcontrol.cpp`(3508 行),辅以 `qwidgettextcontrol_p_p.h`、`qinputcontrol.cpp`、`qtextdocument_p.cpp`(撤销合并)、`qtextedit.cpp`(视图层自动滚动)。
- 审计方法:全量通读源码,逐分支提取行为;本文所有行号均指上述 6.8.3 源文件。
- 用途:XGui 的 `XTextControl` 实现对拍验收。凡本文标注的行为,XTextControl 应逐条复现;第 9 节用例可脚本化断言。
- 审计日期:2026-09-18(清单目录按交付要求使用 2026-09-19)。

---

## 0. 架构与职责边界(对拍前提)

| 职责 | 归属层 | 证据 |
|---|---|---|
| 选区/光标状态、键盘鼠标 IME 事件处理、撤销调用、信号发射 | QWidgetTextControl(控制层) | 全文件 |
| 拖选时**边缘自动滚动** | 视图层(QTextEdit 的 `autoScrollTimer`,100ms 启动,触发周期 `4900/delta²`,合成 MouseMove 再喂给 control)| qtextedit.cpp:1122-1146、1676-1690 |
| 滚动条位置/重绘 | 视图层消费控制层信号:`updateRequest→repaintContents`、`visibilityRequest→ensureVisible`、`documentSizeChanged→adjustScrollbars` | qtextedit.cpp:132-143 |
| 光标矩形/选区矩形几何 | 控制层 `rectForPosition`/`selectionRect`,布局由 QTextDocumentLayout 承担 | qwidgettextcontrol.cpp:1394-1439、1465-1547 |

**结论 0.1**:控制层在拖选 move 中**不调用** `ensureCursorVisible`(源码注释明确:"don't call ensureVisible for the visible cursor to avoid jumping scrollbars. the autoscrolling ensures smooth scrolling",行 1730-1733)。若 XGui 无独立视图层,XTextControl 必须自行实现边缘自动滚动,否则与 Qt 交互不一致。

**结论 0.2**:控制层所有重绘都通过 `updateRequest(QRectF)` 信号表达,自己从不绘制;`drawContents` 仅是把 `getPaintContext` 交给 `QTextDocumentLayout::draw`。

---

## 1. 默认状态与不变量(qwidgettextcontrol.cpp:95-120, 395-402)

| 成员 | 默认值 | 备注 |
|---|---|---|
| `interactionFlags` | `Qt::TextEditorInteraction`(= TextSelectableByMouse \| TextSelectableByKeyboard \| TextEditable)| Android 平台为 `TextEditable \| TextSelectableByKeyboard` |
| `dragEnabled` | true | |
| `overwriteMode` / `acceptRichText` | false / true | |
| `wordSelectionEnabled` | false | true 时普通拖选也按整词选择 |
| `openExternalLinks` / `ignoreUnusedNavigationEvents` | false / false | |
| `cursorOn` / `cursorVisible` / `cursorIsFocusIndicator` | false | |
| `hasFocus` / `isEnabled` | false / true | `isEnabled` 来自 EnabledChange 事件的 `isAccepted()`(行 1059-1061) |
| `mousePressed` / `mightStartDrag` | false | |
| `lastSelectionPosition/Anchor` | 0 | 决定 copyAvailable 边沿检测初值:初始视为"无选区" |
| `preeditCursor` / `hideCursor` | 0 / false | |

不变量:
- I1 `init()`:`doc->setUndoRedoEnabled(interactionFlags & Qt::TextEditable)` —— **只读控件整个撤销栈被禁用**(行 400)。
- I2 `setCursorWidth(-1)` 默认取样式 `PM_TextCursorWidth`(行 401、2469-2476);光标宽度实际存于布局属性 `"cursorWidth"`,缺省 1(行 1413-1419)。
- I3 `processEvent` 入口:`interactionFlags == Qt::NoTextInteraction` → `e->ignore()` 直接返回(行 989-992)。
- I4 `event()` 仅转发 `QObject::event`;定时器(闪烁/三击)经 `timerEvent` 处理(行 1165-1184)。

---

## 2. 交互状态机

### 2.1 鼠标按下(mousePressEvent,行 1555-1663)

状态变量:`mousePressed`、`mightStartDrag`、`mousePressPos`(整数坐标,press 时无条件记录)、`anchorOnMousePress`、`hadSelectionOnMousePress`、`blockWithMarkerUnderMouse`。

处理顺序(严格按源码):

1. `mousePressPos = pos`;`mightStartDrag = false`(行 1560-1564)。
2. **IME 拦截**:`sendMouseEventToInputContext` 若正在预编辑且 press 命中预编辑区 → 事件被消费,后续全部跳过(行 1566-1569;详见 2.6)。
3. 若 `LinksAccessibleByMouse`:记录 `anchorOnMousePress = anchorAt(pos)`;若当前光标是焦点指示器(`cursorIsFocusIndicator`)→ 清除该标志、重绘、`cursor.clearSelection()`(行 1571-1579)。
4. **门槛**:非左键,或非(SelectableByMouse|Editable)→ `e->ignore()` 返回(行 1580-1584)。
5. `blockWithMarkerUnderMouse = blockWithMarkerAt(pos)`;仅当**有效性翻转**(invalid↔valid)时发 `blockMarkerHovered`(行 1585-1588)。
6. `cursorIsFocusIndicator = false`;保存 `oldSelection/oldCursorPos`;`mousePressed = (interactionFlags & TextSelectableByMouse)`;`commitPreedit()`(行 1591-1597)。
7. **三击分支**:`trippleClickTimer` 激活且与 `trippleClickPoint` 曼哈顿距离 `< startDragDistance` → 选块:`StartOfBlock` → `EndOfBlock(KeepAnchor)` → `NextCharacter(KeepAnchor)`(**包含段落分隔符**);记录 `selectedBlockOnTrippleClick`;清 `anchorOnMousePress`/`blockWithMarkerUnderMouse` 并发 `blockMarkerHovered(无效块)`;停三击定时器(行 1599-1611)。**注意:此分支不做 hitTest,基于光标当前所在块。**
8. **普通分支**: `cursorPos = hitTest(Fuzzy)`;为 -1 → `ignore()` 返回(行 1613-1617)。
   - **Shift+点击**(modifiers == ShiftModifier 且 SelectableByMouse,行 1619-1630):
     - 若 `wordSelectionEnabled` 且尚无双击词选区 → 先以**当前光标**位置做 `WordUnderCursor` 播种 `selectedWordOnDoubleClick`;
     - 有三击块选区 → `extendBlockwiseSelection(cursorPos)`;否则有词选区 → `extendWordwiseSelection(cursorPos, pos.x)`;否则若 `!wordSelectionEnabled` → `setCursorPosition(cursorPos, KeepAnchor)`。
   - 无修饰键:
     - **按在选区内**(dragEnabled && 光标有选区 && 非焦点指示器 && Fuzzy 位置 ∈ [selectionStart, selectionEnd] && **hitTest(Exact) != -1**)→ 置 `mightStartDrag = true` 后**直接 return**(行 1633-1643)。此早退跳过函数尾部全部逻辑:不移动光标、不发 cursorPositionChanged、不发 updateRequest、**不更新 `hadSelectionOnMousePress`(保留陈旧值,Qt 真实行为)**。
     - 否则 `setCursorPosition(cursorPos)`(MoveAnchor;该函数在非 KeepAnchor 时清空 `selectedWordOnDoubleClick`/`selectedBlockOnTrippleClick`,行 540-548)。
9. 尾部:
   - Editable:`ensureCursorVisible()`;位置变化才发 `cursorPositionChanged`;`_q_updateCurrentCharFormatAndSelection()`(行 1649-1653)。
   - 只读:位置变化发 `cursorPositionChanged` + `microFocusChanged`;`selectionChanged()`(非强制,行 1654-1660)。
   - `repaintOldAndNewSelection(oldSelection)`;`hadSelectionOnMousePress = cursor.hasSelection()`(行 1661-1662)。

### 2.2 拖动(mouseMoveEvent,行 1665-1757)

1. 若 `LinksAccessibleByMouse`:`anchor = anchorAt(mousePos)`,**仅当与 `highlightedAnchor` 不同**时更新并发 `linkHovered(anchor)`(移出链接发空串)(行 1670-1676)。
2. 按住左键时:
   - **守卫**:`mousePressed || editable || mightStartDrag || 词选区 || 块选区` 全不成立 → 直接返回(行 1681-1686)。即:双击后拖选即使 `mousePressed` 已复位也有效。
   - `mightStartDrag`:移动距离 `> QApplication::startDragDistance()` → `startDrag()` 后返回(行 1691-1695)。
   - `newCursorPos = hitTest(Fuzzy)`。
   - **预编辑中拖动**:若 press 点与新点不同 → `commitPreedit()` 后**重新**对两点做 hitTest(提交使位置失效),光标置回 press 起点(行 1701-1712)。
   - `newCursorPos == -1` → 返回。
   - 若 `mousePressed && wordSelectionEnabled && 无词选区` → 以光标播种 `WordUnderCursor`(行 1717-1720)——**普通拖选从此变整词选择**。
   - 分派:块选区 → `extendBlockwiseSelection`;词选区 → `extendWordwiseSelection`;否则 `mousePressed && !isPreediting()` → `setCursorPosition(newCursorPos, KeepAnchor)`。
   - Editable:位置变化发 `cursorPositionChanged`;`_q_updateCurrentCharFormatAndSelection`;`QGuiApplication::inputMethod()->update(Qt::ImQueryInput)`(行 1729-1739)。只读:位置变化发 `cursorPositionChanged` + `microFocusChanged`。
   - **`selectionChanged(true)` 每次移动强制发射**(行 1747);`repaintOldAndNewSelection`。
3. 未按左键(悬停):更新 `blockWithMarkerUnderMouse`,有效性翻转时发 `blockMarkerHovered`(行 1749-1754)。
4. 末尾再次 `sendMouseEventToInputContext`(行 1756)。

**extendWordwiseSelection(newPos, mouseX)**(行 717-789):
- 新位置落在**最初双击词内部** → 恢复原始词选区并返回( setTextCursor(selectedWordOnDoubleClick))。
- 对新位置取 `StartOfWord`/`EndOfWord` 得候选词;以下情形**无变化返回**(不发任何信号):不在词首(失败)、词无效、词跨行(与原行 `textStart` 不同或空词)。
- 非 `wordSelectionEnabled` 且 mouseX 越出 `[wordStartX, wordEndX]` → **选区保持不变直接返回**(clamp 行为)。
- 锚点规则:统一先把光标设到原词**远离新方向的端点**(newPos < 原词 → 光标在 `selectionEnd`,否则 `selectionStart`),再 KeepAnchor 扩展:
  - `wordSelectionEnabled == true`:无条件吸到候选词边界(与 mouseX 无关);
  - 否则:比较 mouseX 到词首/词尾的**像素距离**,选更近的边界(#39164:向左移动也保留已选词)。
- 尾部(SelectableByMouse):`setClipboardSelection()` + `selectionChanged(true)`。

**extendBlockwiseSelection(newPos)**(行 791-819):
- 新位置在**最初三击块内部** → 恢复原始块选区。
- 向上扩展:光标置原块 `selectionEnd` → KeepAnchor 到 newPos → `StartOfBlock(KeepAnchor)`;向下扩展:光标置原块 `selectionStart` → KeepAnchor 到 newPos → `EndOfBlock(KeepAnchor)` → `NextCharacter(KeepAnchor)`(**包含换行**)。
- 尾部同上(clipboard + 强制 selectionChanged)。

### 2.3 释放(mouseReleaseEvent,行 1759-1856)

1. 保存 oldSelection;IME 拦截:预编辑区内释放 → `invokeAction(QInputMethod::Click, 偏移)`、重绘后返回(详见 2.6)。
2. `mightStartDrag && 左键`:`mousePressed=false`;`setCursorPosition(pos)`(**光标落到点击处,即"点选区内单击收拢选区"**);`cursor.clearSelection()`;`selectionChanged()`(非强制)(行 1773-1780)。
3. `mousePressed`:`mousePressed=false`;`setClipboardSelection()`;`selectionChanged(true)`(强制)(行 1781-1785)。
4. 否则若 **中键 && Editable && 剪贴板支持 Selection(X11)**:`setCursorPosition(pos)` 后把 Selection 剪贴板内容 `insertFromMimeData`(中键粘贴,行 1786-1793)。
5. `repaintOldAndNewSelection`;位置变化发 `cursorPositionChanged` + `microFocusChanged`。
6. **复选框块标记切换**:Editable && 左键 && press 时记录的标记块有效 && 当前无选区 && 释放点命中同一块 → Checked/Unchecked 互切(`cursor.setBlockFormat`)(行 1803-1821)。
7. **链接激活**(LinksAccessibleByMouse):
   - 非左键 → `ignore()`;`anchorAt(pos)` 为空 → `ignore()`;
   - 条件:`!cursor.hasSelection() || (anchor == anchorOnMousePress && hadSelectionOnMousePress)`(即:释放时无选区——普通单击或选区内单击收拢后均满足;或按前已有选区且锚点未变——覆盖拖选起于选内的取消场景;按前无选区的拖选结束时选中了链接 → 两个条件都不满足,**不激活**,防止拖选过链接误触);
   - `hitTest(Exact) >= 0` → `cursor.setPosition(anchorPos)`;`activateLinkUnderCursor(anchorOnMousePress)`(行 1823-1855)。
   - 注意:`anchorOnMousePress` 在门槛之前记录(行 1571-1573),因此右键 press 也会记录它,但右键 press 总体被 ignore。

**activateLinkUnderCursor(href)**(行 2895-2962):href 为空则取选区起点后一字符的 `anchorHref`;仍为空返回。无选区时把光标选区扩为块内连续同 href 的片段区间。有焦点 → `cursorIsFocusIndicator = true`(选区以焦点指示器样式保留显示);无焦点 → 清选区。最后:`openExternalLinks` → `QDesktopServices::openUrl`,否则发 **`linkActivated(href)`**。

### 2.4 双击/三击(mouseDoubleClickEvent,行 1858-1898)

- 仅 `左键 && SelectableByMouse` 处理;否则走 `sendMouseEventToInputContext`,仍不命中 → `ignore()`。
- 流程:`mightStartDrag=false` → `commitPreedit()` → `setCursorPosition(pos)` → 取当前行:
  - 行有效且 `line.textLength() > 0` → `cursor.select(QTextCursor::WordUnderCursor)`,`doEmit = true`;
  - 空行/无效行 → **无选区但依然播种三击状态**。
- `selectedWordOnDoubleClick = cursor`;`trippleClickPoint = pos`;`trippleClickTimer.start(QApplication::doubleClickInterval())`。
- 仅 `doEmit` 时:`selectionChanged()`(非强制)+ `setClipboardSelection()` + `cursorPositionChanged`。
- 双击事件**不设置 `mousePressed`**;后续拖动靠 `selectedWordOnDoubleClick.hasSelection()` 通过 2.2-2 的守卫。
- 三击判定在**下一次 press**(见 2.1-7);第二次 press(即三击)在双击后 `doubleClickInterval` 内且位移小才生效。

### 2.5 内部拖放(拖动选区)与外部拖入

**startDrag()**(行 505-529):`mousePressed=false`;需要 `contextWidget`;mime 来自 `createMimeDataFromSelection()`;Editable 时 actions = Copy|Move 默认 **Move**,只读仅 Copy 默认 Copy;`exec()` 返回 Move 且 `drag->target() != contextWidget`(拖到别的控件)→ `cursor.removeSelectedText()`(**删除源选区**;拖回自身则由 dropEvent 处理)。

**外部 DnD**(行 1956-2025,统一门槛:`TextEditable && canInsertFromMimeData`,否则 `e->ignore()` 并返回 false):
- DragEnter:清 `dndFeedbackCursor`,接受。
- DragMove:对旧反馈光标矩形与新位置光标矩形各发一次 `updateRequest`;`dndFeedbackCursor` 置为 hitTest(Fuzzy) 位置(**拖放指示光标**)。
- DragLeave:清反馈光标并对旧矩形发 `updateRequest`。
- Drop:清反馈 → `repaintSelection()` → `insertionCursor = cursorForPosition(pos)`;`beginEditBlock`;**`dropAction == Move && source == contextWidget` → 先 `cursor.removeSelectedText()` 删除原选区** → `cursor = insertionCursor` → `insertFromMimeData` → `endEditBlock` → `ensureCursorVisible`(一个编辑块 = 一次撤销单元)。
- 事件收尾差异:widget 路径(DragEnter/Move/Drop)调 `ev->acceptProposedAction()`;GraphicsScene 的 Drop 调 `ev->accept()`(行 1140-1144)。

### 2.6 IME 预编辑状态机(inputMethodEvent,行 2027-2153)

门槛:`interactionFlags & (TextEditable|TextSelectableByMouse)` 且 cursor 非空,否则 `ignore()`。

- `isGettingInput = commitString 非空 || preeditString != 当前 preedit || replacementLength > 0`;若 `!isGettingInput && attributes 为空` → `ignore()`。
- `beginEditBlock`;`isGettingInput` → 先 `cursor.removeSelectedText()`(**预编辑提交覆盖选区**)。
- **commit 串插入**:有 replacement 时,在 `position + replacementStart` 处 KeepAnchor `replacementLength` 后 `insertText(commitString)`(替换);纯 commit 直接插入。
- **Selection 属性**:按 `blockStart = a.start + block.position()` 设置光标选区,并 `ensureCursorVisible` + 重绘旧新选区。
- **preedit 文本**:`layout->setPreeditArea(cursor.position() - block.position(), e->preeditString())`(仅 `isGettingInput`)。**预编辑文本不属于文档**,不触发 contentsChanged/textChanged;提交插入的 commit 串才触发。
- **preedit 光标**:`preeditCursor` 默认 = preeditString 长度;`Cursor` 属性覆盖为 `a.start`,`hideCursor = (a.length == 0)`。
- **格式覆盖(IME 下划线来源)**:`TextFormat` 属性 → 当前字符格式 merge 属性格式,生成 FormatRange 按 start 排序;**空隙用当前字符格式补齐**(行 2115-2142);`layout->setFormats(overrides)`。下划线样式本身由输入法通过 TextFormat 属性携带,布局按普通文本格式绘制。
- `endEditBlock`;`cursor.d->setX()`;位置变化发 `cursorPositionChanged`;`preeditCursor` 变化发 `microFocusChanged`。**不发 textChanged/updateRequest**(重绘由布局的 update 信号转发,行 682-683)。

**鼠标与预编辑**(sendMouseEventToInputContext,行 1900-1932):预编辑中,hitTest(pos) 与 `cursor.position()` 的差 ∈ [0, preedit 长度] 时:press/move 被**消费**(press 不移动光标);release → `inputMethod->invokeAction(Click, 差值)` 并消费。press 记录的 `mousePressPos` 用于 2.2 的"预编辑中拖动即提交"。

**commitPreedit()**(行 2983-3000):非预编辑直接返回;先 `inputMethod()->commit()`;若仍在预编辑(IME 未响应)→ 手动 `setPreeditArea(-1, "")` + `clearFormats()`(包在编辑块内);`preeditCursor = 0`。调用点:mousePress、doubleClick、mouseMove 跨点拖动。

### 2.7 焦点与光标闪烁(focusEvent 行 2235-2262;setCursorVisible 行 690-702;updateCursorBlinking 行 704-715;timerEvent 行 1170-1184)

- 进入 `focusEvent` 无条件发 `updateRequest(selectionRect())`。
- FocusIn:`cursorOn = (interactionFlags & (SelectableByKeyboard|Editable))`;Editable 额外 `setCursorVisible(true)`(**闪烁启动**)。只读但可键盘选择 → 常亮不闪。
- FocusOut:`setCursorVisible(false)`;`cursorOn=false`;若 `cursorIsFocusIndicator` 且 reason 非 ActiveWindowFocusReason/PopupFocusReason 且有选区 → 清选区(**失焦收起焦点指示器选区**)。
- 闪烁:周期 = `QStyleHints::cursorFlashTime()/2`(flashTime ≥ 2 才启动);可见期间连接 `cursorFlashTimeChanged` 动态调整。tick 时 `cursorOn` 翻转;**有选区时仅当样式提示 `SH_BlinkCursorWhenTextSelected` 才继续闪烁**。
- `setCursorVisible` 仅在状态翻转时生效并重绘光标。
- `setFocus(bool, Qt::FocusReason)` = 合成 QFocusEvent 后走 `processEvent`(行 2228-2233)。

---

## 3. processEvent 路由表

### 3.1 事件分发表(processEvent,行 986-1163)

| 事件 | 动作 | 备注 |
|---|---|---|
| 无交互(NoTextInteraction) | `e->ignore()`,返回 | 最高优先级 |
| KeyPress | `keyPressEvent` | |
| MouseButtonPress / MouseMove / MouseButtonRelease / MouseButtonDblClick | 对应 mouse*Event,pos 经 transform 映射 | |
| InputMethod | `inputMethodEvent` | |
| ContextMenu | `contextMenuEvent(globalPos, 映射pos, widget)` → 标准菜单 popup(WA_DeleteOnClose,初始屏取 parent 窗口)(行 1934-1954) | |
| FocusIn / FocusOut | `focusEvent` | |
| EnabledChange | `isEnabled = e->isAccepted()` | |
| ToolTip | `showToolTip`:`cursorForPosition(pos).charFormat().toolTip()` 非空才 `QToolTip::showText`(行 2965-2972) | |
| DragEnter / DragLeave / DragMove / Drop | 见 2.5;接受时 widget 事件 `acceptProposedAction()` | |
| GraphicsScene 系列鼠标/悬停/菜单/拖放 | 同 widget 路径;HoverMove 以 NoButton 走 `mouseMoveEvent`;**GraphicsSceneDrop 用 `ev->accept()`** | 行 1096-1144 |
| EnterEditFocus / LeaveEditFocus | 仅 QT_KEYPAD_NAVIGATION 且启用时 `editFocusEvent`(桌面构建整体不编译;`setBlinkingCursorEnabled` 在 6.8.3 控制层已无定义,属遗留声明) | 行 1146-1151 |
| ShortcutOverride | Editable 且 `isCommonTextEditShortcut(ke)` → `ke->accept()`(阻止父级抢走 Ctrl+C/V/X/Z 等) | 行 1153-1159 |
| 其他 | **静默忽略(不 ignore 事件)** | default 分支 |

### 3.2 keyPressEvent 完整路由(优先级即判断顺序,行 1206-1377)

| 序 | 条件 | 动作 | 后续 |
|---|---|---|---|
| 1 | `QKeySequence::SelectAll` | `selectAll()` + `setClipboardSelection` | **直接 return**(只读也可用) |
| 2 | `QKeySequence::Copy` | `copy()` | 直接 return(只读也可用) |
| 3 | SelectableByKeyboard 且 `cursorMoveKeyEvent(e)` 返回 true | 光标移动/选区扩展(见 3.3) | goto accept |
| 4 | LinksAccessibleByKeyboard 且 (Return/Enter [+Key_Select]) 且**光标有选区**(键盘聚焦的链接) | `activateLinkUnderCursor()` | return |
| 5 | **非 Editable** | `e->ignore()` | return |
| 6 | Key_Direction_L / Key_Direction_R | mergeBlockFormat 设置布局方向 LTR/RTL | accept |
| 7 | (所有编辑操作前)`repaintSelection()` —— 跳转前擦除旧光标区(表格跨单元格跳转可见) | | |
| 8 | **Backspace 且修饰键仅 Shift/GroupSwitch**:块首且在列表且无选区 → `list->remove(block)`(取消项目符号);块首且 indent>0 → 缩进-1;否则 `deletePreviousChar` + `setX` | 智能退格 | accept |
| 9 | `QKeySequence::InsertParagraphSeparator`(Enter) | `insertParagraphSeparator()`(见下) | accept |
| 10 | `QKeySequence::InsertLineSeparator`(Shift+Enter) | `insertText(QChar::LineSeparator)` | accept |
| 11 | `QKeySequence::Undo` / `Redo` / `Cut` / `Paste` | 调公开槽;**Paste 修饰键为 Ctrl+Shift+Key_Insert 且支持选区剪贴板时从 Selection 粘贴**,其余从 Clipboard | accept |
| 12 | `QKeySequence::Delete` / `Backspace`(带其他修饰如 Ctrl+Backspace 落此处) | 局部光标 `deleteChar()` / `deletePreviousChar()` + `setX` | accept |
| 13 | `DeleteEndOfWord`(Ctrl+Del) | 无选区先 `NextWord(KeepAnchor)`,再 removeSelectedText | accept |
| 14 | `DeleteStartOfWord`(Ctrl+Backspace) | 无选区先 `PreviousWord(KeepAnchor)`,再 removeSelectedText | accept |
| 15 | `DeleteEndOfLine` | 特例:光标在块末字符位置(`position == block.position()+block.length()-2`)时 `Right(KeepAnchor)`,否则 `EndOfBlock(KeepAnchor)`;removeSelectedText | accept |
| 16 | 其他:`isAcceptableInput(e)`(见 3.5)为真 → **overwriteMode 且无选区且非块尾 → 先 `deleteChar()`**,然后 `insertText(e->text())`,`selectionChanged()`;否则 `e->ignore()` return | 普通输入 | accept / ignore |

**accept 收尾**(行 1365-1376,除 1/4/5 外所有已处理键共用):`setClipboardSelection()`;`e->accept()`;`cursorOn = true`(任意按键点亮光标);`ensureCursorVisible()`;`updateCurrentCharFormat()`。

**insertParagraphSeparator**(行 3194-3235):清 `BlockTrailingHorizontalRulerWidth`(避免复制 `<hr>`);标题块 → 清 HeadingLevel 且字符格式重置;在列表内 → 清块下边距,Checked 标记 → 新块 Unchecked;**空块**(text 为空且无 hr/代码块属性)→ 块/字符格式全部重置,**若格式确有变化则不再插入新块直接返回**(连按两次 Enter 退出列表/引用,第三次才真正换行);否则 `cursor.insertBlock(blockFmt, charFmt)`。

### 3.3 cursorMoveKeyEvent 标准键映射(行 122-287)

匹配 `QKeySequence` 标准 binding(平台相关),未命中返回 false(交给后续分支)。映射表(操作 → MoveOperation[+KeepAnchor]):

| QKeySequence | 操作 | QKeySequence | 操作 |
|---|---|---|---|
| MoveToNextChar | Right | SelectNextChar | Right+K |
| MoveToPreviousChar | Left | SelectPreviousChar | Left+K |
| MoveToNextWord | WordRight | SelectNextWord | WordRight+K |
| MoveToPreviousWord | WordLeft | SelectPreviousWord | WordLeft+K |
| MoveToStartOfLine | StartOfLine | SelectStartOfLine | StartOfLine+K |
| MoveToEndOfLine | EndOfLine | SelectEndOfLine | EndOfLine+K |
| MoveToStartOfBlock | StartOfBlock | SelectStartOfBlock | StartOfBlock+K |
| MoveToEndOfBlock | EndOfBlock | SelectEndOfBlock | EndOfBlock+K |
| MoveToPreviousLine | Up | SelectPreviousLine | Up+K(首行首列处 → Start) |
| MoveToNextLine | Down | SelectNextLine | Down+K(末行末列处 → End) |
| MoveToStartOfDocument | Start | SelectStartOfDocument | Start+K |
| MoveToEndOfDocument | End | SelectEndOfDocument | End+K |

执行细节:
- 移动期间临时 `setVisualNavigation(true)`,完成后恢复(行 256-259)。
- `ensureCursorVisible()` 无条件调用。
- **边界透传**:`ignoreNavigationEvents = ignoreUnusedNavigationEvents`;`isNavigationEvent = Up/Down/Left/Right`(桌面构建 Left/Right 也算)。移动失败(或位置未变)且 `ignoreNavigationEvents && isNavigationEvent && anchor 未变` → **返回 false**,事件不被消费(QTextBrowser 用它翻页/滚动)(行 262-280)。
- 成功:位置变化发 `cursorPositionChanged`;`microFocusChanged` 无条件;`selectionChanged(mode == KeepAnchor)`(**Shift 扩选强制发射,普通移动仅在选区状态变化时发射**);`repaintOldAndNewSelection`。

### 3.4 鼠标按钮路由汇总

| 按钮/阶段 | 行为 |
|---|---|
| 左键 press | 2.1 全流程(选择/三击/mightStartDrag) |
| 左键 move | 2.2 拖选 |
| 左键 release | 2.3(mightStartDrag 收拢 / clipboard 选区 / 链接激活 / 复选框) |
| 双击(左) | 2.4 选词;三击(左) | 2.1-7 选段 |
| 中键 release | Editable + 支持 Selection 剪贴板 → 在点击处粘贴 Selection |
| 右键 press | **始终 ignore**:非左键在门槛(行 1580-1584)即被拒,不会移动光标;但 LinksAccessibleByMouse 时 `anchorOnMousePress` 已在门槛前记录 |
| 其他按钮 | press 同样被门槛 ignore;release 仅中键有额外行为 |
| ContextMenu 事件 | → `createStandardContextMenu` + popup(见第 7 节),与 press 无关 |

### 3.5 isAcceptableInput(QInputControl,可打印字符门槛)

- `text` 为空 → false;首字符 `Other_Format`(ZWJ/ZWNJ/RLM 等格式符)→ **true**(优先于修饰键检查)。
- 修饰键为纯 Ctrl 或 Ctrl+Shift → false(AltGr=Ctrl+Alt 不拦)。
- `isPrint()` → true;`Other_PrivateUse` → true;高代理+低代理 → true;TextEdit 类型的 `\t` → true;其余 false。
- **Ctrl+数字/字母不进文档**(落到第 5 门槛 ignore)。

### 3.6 isCommonTextEditShortcut(ShortcutOverride 判定)

- 修饰键为 No/Shift/Keypad:key < Key_Escape 的所有键,或 Return/Enter/Delete/Home/End/Backspace/Left/Right/Up/Down/Tab → true。
- 其他修饰:匹配 Copy/Paste/Cut/Redo/Undo/MoveToNextWord/MoveToPreviousWord/MoveToStartOfDocument/MoveToEndOfDocument/SelectNextWord/SelectPreviousWord/SelectStartOfLine/SelectEndOfLine/SelectStartOfBlock/SelectEndOfBlock/SelectStartOfDocument/SelectEndOfDocument/SelectAll 之一 → true。

---

## 4. 信号矩阵

发射点代码位置均指 qwidgettextcontrol.cpp。

| 信号 | 触发条件(精确) | 发射点(行) | 去重/抑制规则 |
|---|---|---|---|
| `textChanged()` | 文档内容任何变化(doc `contentsChanged` 转发连接);`setContent` 结束时**固定补发一次** | 450/490-491 | setContent 加载期间临时断开转发连接再恢复,避免 setText 系列产生多次 textChanged;预编辑文本不触发 |
| `undoAvailable(bool)` | doc `undoAvailable` 原样转发 | 434 | 无 |
| `redoAvailable(bool)` | doc `redoAvailable` 原样转发 | 435 | 无 |
| `modificationChanged(bool)` | doc `modificationChanged` 原样转发(setContent 末尾对非外部文档 `setModified(false)` 会触发一轮回零) | 436-437、496 | 无 |
| `blockCountChanged(int)` | doc `blockCountChanged` 原样转发 | 438-439 | 无 |
| `currentCharFormatChanged(QTextCharFormat)` | `updateCurrentCharFormat` 中 `cursor.charFormat() != lastCharFormat` | 289-300 | 相同格式不重复发;伴随 microFocusChanged |
| `copyAvailable(bool)` | `selectionChanged()` 内**边沿触发**:`hasSelection` 状态(有无选区)与上次不同 | 594-597 | 仅翻转时发;true=出现选区,false=选区消失 |
| `selectionChanged()` | ① 强制模式:所有 `selectionChanged(true)` 调用点——键盘 Shift 扩选、鼠标拖动每次 move、release(有 mousePressed)、extend* 尾部;② 非强制:位置或锚点较上次变化**且**(选区状态翻转 或 有选区时位置/锚点变化) | 577-615 | 位置与锚点都没变时非强制不发射;**无选区时移动光标不发射**;强制模式无条件发射(可能重复) |
| `cursorPositionChanged()` | 显式发射点:cursorMoveKeyEvent(move 后位置变化)、mouse press/move/release(位置变化)、doubleClick(doEmit)、setTextCursor(pos 变化)、moveCursor(moved)、undo/redo(位置变化)、selectAll(位置变化)、focusEvent 不发;隐式:doc 编辑把控制光标推移 → `_q_emitCursorPosChanged`(isCopyOf 检查) | 276、635-642、834、846、900-921、1652/1743/1798、1888、1913、2149、2607、974 | 文档级信号只在**编辑**时发(doc finishEdit),纯移动不会双重发射 |
| `updateRequest(QRectF)` | 需要重绘的区域:布局 update 转发(整图);`_q_updateBlock`(右界置 INT_MAX);`repaintCursor`(光标矩形 ±4px);`repaintOldAndNewSelection`(选区差集或新旧选区并集);selectAll(空矩形=全量);setExtraSelections(新旧差集,FullWidthSelection 时 0..INT_MAX);focusEvent(selectionRect);setCursorWidth/setCursorIsFocusIndicator(光标);拖放反馈光标;setFocusToAnchor 系列 | 553、556-575、976、1386-1392、2492-2529、2238、2469-2476、2622-2627、1973-1998、3009-3061 | 空矩形约定为"请全量重绘" |
| `documentSizeChanged(QSizeF)` | 布局 `documentSizeChanged` 转发(换 layout 时重连) | 686-687 | 无 |
| `visibilityRequest(QRectF)` | `ensureCursorVisible`(光标矩形 X 向 ±5px);setFocusToAnchor/setFocusToNextOrPreviousAnchor(锚点选区矩形) | 3286-3292、3032、3061 | 视图层据此滚动 |
| `microFocusChanged()` | 宽泛发射:updateCurrentCharFormat(格式变化)、selectionChanged 尾部、cursorMoveKeyEvent(移动)、ensureCursorVisible、undo/redo、_q_emitCursorPosChanged、inputMethodEvent(preeditCursor 变化)、只读鼠标 move/release 位置变化、setContent | 299、612、277、3291、836/848、640、2151-2152、1657/1744/1800、499 | 高频信号,视图层用于 updateMicroFocus/输入法面板 |
| `linkActivated(QString)` | `activateLinkUnderCursor`:链接激活成功且 `!openExternalLinks`(为 true 时改由 QDesktopServices 打开,**不发信号**) | 2956-2961 | |
| `linkHovered(QString)` | 鼠标移动(含 GraphicsSceneHoverMove)时 `anchorAt` 结果与上次不同(进入链接发 href,移出发**空串**);仅 LinksAccessibleByMouse | 1670-1676 | |
| `blockMarkerHovered(QTextBlock)` | press(通过门槛后)或无按键 move 时,光标下标记块**有效性翻转**(invalid↔valid);三击时强制清空并发一次无效块 | 1585-1588、1608-1609、1750-1753 | 有效块→另一有效块不发射 |

---

## 5. 撤销/重做:粒度与合并规则

### 5.1 控制层规则

- **启用前提**:`init()` 中 `doc->setUndoRedoEnabled(interactionFlags & TextEditable)`(行 400)——只读控件撤销/重做完全禁用,`undoAvailable` 恒 false。
- `setContent` 加载新文本时临时 `setUndoRedoEnabled(false)`,结束后恢复原状态并对非外部文档 `setModified(false)`(行 442-444、492-496)——**程序化加载不进撤销栈**。
- `undo()`/`redo()`(行 828-850):`doc->undo(&cursor)`(光标随命令恢复);前后位置不同才发 `cursorPositionChanged`;无条件 `microFocusChanged` + `ensureCursorVisible`;之前 `repaintSelection()`。
- 右键菜单 Undo/Redo 项的 enabled 分别取 `doc->isUndoAvailable()/isRedoAvailable()`(行 2331-2336)。

### 5.2 文档层粒度(qtextdocument_p.cpp appendUndoItem:1032-1071 / tryMerge:106-143)

- **合并条件**(仅当栈非空且 modified):
  1. 相邻插入合并(打字):同为 `Inserted`、`pos+length == other.pos`、`strPos+length == other.strPos`、`format` 相同 → 合并为一个命令。**连续打字无限时长限制**,只要位置相邻且格式一致;**在已有文本中间插入会断开合并**(pos 不相邻)。
  2. Delete 键向右删除合并:同为 `Removed`、`pos` 相同、`strPos+length == other.strPos`、格式相同。
  3. Backspace 向左删除合并:同为 `Removed`、`other.pos+other.length == pos`、格式相同。
- **编辑块粒度**:`beginEditBlock/endEditBlock` 包裹的命令标 `block_part/block_end`,undo/redo 跳过块内命令**整体应用**(undo 处理时循环吞并同块命令,qtextdocument_p.cpp:978-985)。IME commit+preedit、drop 插入、append 等均是一个编辑块 = 一次撤销单元。
- 编辑块内光标跨位置移动会插入 `CursorMoved` 命令(行 1041-1049)。
- 新命令落在已撤销位置 → 清空 **redo 栈**(行 1036-1037);`undoCommandAdded()` 仅对非块内命令发射(行 1069-1070)。
- `modificationChanged` 语义:`modifiedState` 为某撤销点快照,回到该点 → modified=false(可来回穿越)。

---

## 6. draw 行为(drawContents / getPaintContext,行 3306-3379)

`drawContents(p, rect, widget)`:`p->save()` → `getPaintContext(widget)` → rect 有效则 `IntersectClip` → `ctx.clip = rect` → `doc->documentLayout()->draw(p, ctx)` → `p->restore()`。**所有可见元素由 QTextDocumentLayout 按 PaintContext 绘制**,控制层只负责准备 ctx。

`getPaintContext` 组装规则(顺序即组装顺序):

1. `ctx.selections = extraSelections`(**extraSelections 先于主选区入列表 → 布局先绘制**,即主选区高亮绘制在 extraSelections 之上)。
2. `ctx.palette = d->palette`;若传入 widget 且带 QSS,用 `styleSheetPalette` 覆盖。
3. **光标**:`cursorOn && isEnabled` 时:
   - `hideCursor`(IME 隐藏光标)→ `cursorPosition = -1`(不画);
   - `preeditCursor != 0` → `cursorPosition = -(preeditCursor + 2)`(布局在预编辑区内按偏移画预编辑光标);
   - 否则 `cursorPosition = cursor.position()`。
   - `dndFeedbackCursor` 非空时**覆盖** cursorPosition(拖放指示光标与编辑光标互斥,同一根)。
4. **主选区**(`cursor.hasSelection()` 时 append 一项):
   - `cursorIsFocusIndicator`(键盘导航聚焦的链接)→ format 取样式提示 `SH_TextControl_FocusIndicatorTextCharFormat`;
   - 否则:背景 `Highlight` / 前景 `HighlightedText`,色组 `hasFocus ? Active : Inactive`(**失焦选区变灰**);样式提示 `SH_RichText_FullWidthSelection` 为真时置 `FullWidthSelection` 属性(选区扩展到整行宽)。

绘制顺序与条件结论(供对拍):
- 每行内:选区/高亮背景 → 文本(含预编辑区文本与 FormatRange,即 IME 下划线)→ 光标竖线(由布局最后画)。
- 光标显示的三重条件:`cursorVisible/hasFocus 途径点亮 cursorOn` + `isEnabled` + 非 `hideCursor`;`cursorOn` 由闪烁定时器翻转,有选区时受 `SH_BlinkCursorWhenTextSelected` 节制。
- 无选区时不追加 Selection 项;`updateRequest` 空矩形应触发视图全量重绘。
- `rectForPosition`(行 1394-1439):光标矩形宽度 = 布局属性 cursorWidth(缺省 1);**overwriteMode 下宽度含下一字符 advance(块尾用空格 advance)**;预编辑存在时,预编辑插入点之后的位置整体平移 preedit 长度(行 1404-1410)。
- `selectionRect`(行 1465-1547):复杂表格选区 → 整个表格 frame 矩形;同块多行 → 各行 `rect() | naturalTextRect()` 并集再平移;跨块 → 起止位置矩形并集 + 选区内浮动 frame 矩形,左右扩到当前 frame 边界;有效矩形再 `adjust(-1,-1,1,1)` 外扩 1px。

---

## 7. 标准右键菜单(createStandardContextMenu,行 2314-2395)

- 位置非空时记录 `linkToCopy = anchorAt(pos)`;`linkToCopy` 为空且非文本选择场景(Editable|SelectableByKeyboard|SelectableByMouse 全无)→ 返回 **nullptr**(不弹菜单)。
- 项与启用条件(Edit = Editable, Sel = showTextSelectionActions):

| 菜单项 | 出现条件 | enabled | objectName |
|---|---|---|---|
| Undo | Edit | isUndoAvailable | edit-undo |
| Redo | Edit | isRedoAvailable | edit-redo |
| Cut | Edit | hasSelection | edit-cut |
| Copy | Sel | hasSelection | edit-copy |
| Copy Link Location | LinksAccessibleByKeyboard 或 Mouse | linkToCopy 非空 | link-copy |
| Paste | Edit | canPaste | edit-paste |
| Delete | Edit | hasSelection | edit-delete(_q_deleteSelected:仅 Editable 且有选区才 removeSelectedText,行 821-826) |
| Select All | Sel | !doc->isEmpty() | select-all |

- Edit 且 `useRtlExtensions` 时追加"Insert Unicode control character"子菜单(14 个控制字符,插入到 `insertPlainText`)(行 3402-3458)。
- 快捷键文本:非 `AA_DontShowShortcutsInContextMenus` 且无全局冲突时显示(ACCEL_KEY 宏)。
- `_q_copyLink`:把 `linkToCopy` 以 text/plain 写入剪贴板(行 3381-3388)。

## 8. 剪贴板与 MIME(行 2693-2751、3460-3502)

- `createMimeDataFromSelection`:返回 **QTextEditMimeData(懒物化)**,formats = text/plain、text/html、text/markdown、ODF;首次 retrieveData 才生成全部数据。
- `copy`:仅有选区时执行;`cut`:Editable 且有选区,copy + removeSelectedText。
- `canInsertFromMimeData`:acceptRichText → 有非空 text 或 html 或 x-qrichtext 或 x-qt-richtext;否则仅非空 text。
- `insertFromMimeData` 优先级:**text/markdown(若为 formats 首项)→ x-qrichtext(需 acceptRichText)→ html(需 acceptRichText)→ 纯文本**;`cursor.insertFragment`(自动替换选区);`ensureCursorVisible`;门槛 Editable。
- `paste(mode)`:剪贴板 mimeData(mode) → insertFromMimeData。
- `setClipboardSelection`:仅有选区且平台支持 Selection 才写 Selection 剪贴板。
- `canPaste`:Editable 且剪贴板 mimeData 能被 canInsertFromMimeData 接受。

---

## 9. 验收用例(共 58 条,均可脚本化断言)

约定:`ctl` 为被测控件;`ks()` 发送按键;`mouse` 系列模拟鼠标;`doc` 为其文档;信号记录通过 spy 列表断言。`SD = QApplication::startDragDistance()`。

### A. 构造与默认状态

- **TC-01** 默认交互标志:`assert ctl.textInteractionFlags() == TextEditorInteraction`;`assert ctl.isDragEnabled()`;`assert !ctl.overwriteMode()`;`assert ctl.acceptRichText()`;`assert !ctl.isWordSelectionEnabled()`;`assert !ctl.openExternalLinks()`。
- **TC-02** 只读禁撤销:设 `TextSelectableByMouse` 后 `assert !doc.isUndoRedoEnabled()`(I1)。
- **TC-03** 无交互吞事件:flags=NoTextInteraction,press/KeyPress 后 `assert event.ignored()` 且光标/文本不变。
- **TC-04** setPlainText 单次信号:载入 100 行文本,`assert textChangedSpy.count() == 1`;`assert !doc.isModified()`;`assert undoSpy.last() == false`。
- **TC-05** 光标宽度:setCursorWidth(-1) 后 `assert doc.documentLayout().property("cursorWidth") == style.pixelMetric(PM_TextCursorWidth)`;setCursorWidth(3) 后属性为 3 且收到一次 updateRequest(光标矩形 ±4px)。

### B. 鼠标拖选状态机

- **TC-06** 单击定位:左键按下文本中部,"abc def" 中点在 def → `assert cursor.position() ∈ def 范围`;`assert !cursor.hasSelection()`。
- **TC-07** 拖选锚点:press 于 p1,move 到 p2(>SD),release → `assert selectionStart..selectionEnd 覆盖 p1..p2 对应字符`;期间每个 move 事件都伴随 `selectionChanged`(`assert selSpy.count() == move次数`)。
- **TC-08** 拖选 copyAvailable 边沿:空选区状态拖选 → `assert copyAvailableSpy == [true]`;单击收拢 → `copyAvailableSpy` 追加 `false`;再在无选区状态拖选 → 再发 `true`。
- **TC-09** 拖选信号序:完整拖选 release 后顺序断言 `copyAvailable(true) 先于 selectionChanged`(release 强制 selectionChanged 且 clipboard 写入);`assert Applicationclipboard.text == 选中文本`(X11 亦写入 Selection 剪贴板)。
- **TC-10** 无选区移动光标不发 selectionChanged:只读+SelectableByKeyboard,光标移动键若干次 → `assert selSpy.isEmpty() && microFocusSpy.count() == 移动次数`。
- **TC-11** 点选区内单击收拢:先拖选 "hello",再在选区内 press+release(位移 < SD)→ `assert !cursor.hasSelection()`;`assert cursor.position() == hitTest(点击处)`;press 阶段**无** cursorPositionChanged/updateRequest(早退),release 后有一条 selectionChanged。
- **TC-12** 点选区内小位移不启动拖动:move 距离 ≤ SD → 未进入 QDrag(无 drag cursor);release 收拢。
- **TC-13** 拖动超阈值启动 QDrag:press 选区 + move > SD → `assert QDrag.exec 被调用`(mime formats 含 text/plain、text/html);可编辑时 acceptedAction 默认 Move。
- **TC-14** 拖到控件外 Move 删除源:模拟 exec 返回 MoveAction 且 target 非本控件 → `assert 原选区文本被删除`。
- **TC-15** Shift+点击扩展:光标在 p0,Shift+click p1 → `assert 选区 == p0..p1`;并 `assert selSpy.count() == 1`(非强制路径,选区状态翻转)。
- **TC-16** 右键 press 门槛:任何交互标志组合下,右键 press 均 `event.ignored()` 且光标不动(非左键门槛);但 LinksAccessibleByMouse 时 `anchorOnMousePress` 已被记录(门槛前执行)。
- **TC-17** release 无关按钮:左键拖选中按下 Shift 无影响;非左键 release 不写 Selection 剪贴板。

### C. 双击/三击

- **TC-18** 双击选词:"foo bar" 双击 bar → `assert selectedText == "bar"`;`assert selSpy==1 && cursorPositionChangedSpy==1`;Selection 剪贴板已更新。
- **TC-19** 双击空行:空块上双击 → `assert !cursor.hasSelection()` 但三击定时器已启动(后续三击仍可选段)。
- **TC-20** 双击后拖选整词吸附(wordSelectionEnabled=false):双击 "bar" 后拖过 "baz" → `assert selectedText 是整词边界对齐`(不裁半词);拖回原词内部 → `assert selectedText == "bar"`(恢复原始词)。
- **TC-21** 整词拖选 clamp:双击选词后拖出该词 X 范围(鼠标 x 超过词尾像素)→ `assert 选区保持上一次合法整词不变`,且**无新增** selectionChanged(直接 return)。
- **TC-22** wordSelectionEnabled 普通拖选整词化:开启后普通 press+move → `assert 选区从按下处所在词整词开始扩展`;Shift+click 亦按词扩展。
- **TC-23** 三击选段(含换行):双击后 SD 内再次 press → 非末段时 `assert selectedText 以 U+2029 结尾`(覆盖段分隔符);`assert selectedBlockOnTrippleClick 对应块`;三击后 anchorOnMousePress 与块标记悬停状态被清空(blockMarkerHovered 收到 invalid)。

### D. 外部拖放与反馈

- **TC-24** 只读拒拖入:DragEnter 后 `assert event.ignored()`(Editable false)。
- **TC-25** 拖放反馈光标:DragMove 到 p → `assert 收到 2 次 updateRequest`(旧光标矩形+新光标矩形),第二次为 cursorRect(hitTest(p));DragLeave → 再 1 次且 dndFeedbackCursor 清空。
- **TC-26** 内部 Move 拖放删除源:选区拖放到本控件 p(DropAction=Move)→ `assert 原选区已删除且文本插入 p`;整个过程一个撤销单元(`doc.undo()` 一次完全还原)。
- **TC-27** 外部 Drop 插入:剪贴板 mime Drop → `assert 插入于 drop 点,原选区不受影响`。
- **TC-28** Drop 后可见性:`assert visibilityRequest 在 drop 后被发射`(ensureCursorVisible)。
- **TC-29** canInsertFromMimeData:空文本 mime → false;setAcceptRichText(false) 后 html-only mime → false,text mime → true。

### E. 键盘路由

- **TC-30** 方向键表:逐键断言 Right/Left/Up/Down/Home/End/Ctrl+Left/Ctrl+Right/Ctrl+Home/Ctrl+End 对应 MoveTo* 语义(位置/行首/文档首),`assert cursorPositionChangedSpy.count() == 有效移动次数`。
- **TC-31** Shift+方向扩选:Shift+Right ×3 → `assert anchor 不动、position 前进 3`;每次按键 `assert selSpy.count()` 递增(KeepAnchor 强制发射)。
- **TC-32** 边界透传:setIgnoreUnusedNavigationEvents(true) + 无选区,在文档首按 Up → `assert 事件未被接受`(cursorMoveKeyEvent 返回 false);同条件按 Left → 同样透传(桌面构建 Left/Right 也算导航键);关闭该开关后同操作事件被接受。
- **TC-33** Ctrl+A/C 只读可用:只读控件 Ctrl+A → 全选 + `copyAvailable(true)` + updateRequest(空矩形);Ctrl+C → 剪贴板为全文。
- **TC-34** 普通输入:发 'a' → `assert textChanged`、`cursorOn == true`(光标点亮)、收到 updateRequest(光标矩形)、ensureCursorVisible 产生 visibilityRequest;发 Ctrl+2(非可打印)→ `event.ignored()`。
- **TC-35** Backspace 智能退格:块首且在列表 → `assert 该块脱离列表(缩进/列表格式移除)且字符未删`;块首且 indent=2 → indent 变 1;普通位置 → 删除前一字符;**Ctrl+Backspace**(平台序 DeleteStartOfWord)→ 删除前一词。
- **TC-36** Enter 语义:列表内 Enter → 新块 Unchecked(原 Checked)且下边距清除;标题块 Enter → 新块无 HeadingLevel 且字符格式重置;**空块上 Enter(格式有变化)→ 不插入新块直接退出列表**;空块再次 Enter → 插入新块;Shift+Enter → 插入 U+2028 行分隔符而非新块。
- **TC-37** 覆盖模式:overwriteMode=true,光标非块尾输入 'X' → `assert 下一字符被替换`(`deleteChar`+insert);有选区或块尾时选区被替换/直接插入;光标矩形宽度含下一字符 advance。
- **TC-38** Ctrl+Del/Ctrl+K:Ctrl+Del(DeleteEndOfWord,平台序)删除至词尾;DeleteEndOfLine(平台序,如 Ctrl+K)删除至行尾,光标在行末字符时恰好删除该字符(不空删)。
- **TC-39** ShortcutOverride:父级拦截场景下,Ctrl+C/Ctrl+V/Ctrl+X/Ctrl+Z/Ctrl+A/方向键 等 `isCommonTextEditShortcut` 键事件被 control accept(只读控件只 accept Copy/SelectAll 与光标键——检查 Editable 条件)。
- **TC-40** 链接键盘激活:LinksAccessibleByKeyboard 且光标选区覆盖链接 → Return/Enter → `assert linkActivated(href)` 或(接收者场景)setFocusToAnchor 后 Return;无选区按 Return 不激活且被 ignore(不可编辑时)。

### F. IME

- **TC-41** 预编辑显示:发送含 preeditString="ni" 的 InputMethodEvent → `assert doc.toPlainText() 不含 "ni"`(预编辑不入文档)、`assert !textChangedSpy`、布局 `preeditAreaText() == "ni"`、`assert cursorRect 经 rectForPosition 的预编辑偏移映射正确`。
- **TC-42** 提交:commitString="你" → `assert 文档含 "你"`、`textChangedSpy==1`、预编辑区清空;提交前若有选区 → 选区被替换(isGettingInput removeSelectedText)。
- **TC-43** 预编辑光标属性:带 Cursor 属性 (start=1,length=1) → `assert preeditCursor==1 && !hideCursor`;length=0 → `assert hideCursor` 且绘制 cursorPosition==-1(不画光标);preeditCursor 变化 → `microFocusChanged` 恰好一次。
- **TC-44** 预编辑期间鼠标:press 命中预编辑区 → 事件被消费、光标不动;release 命中预编辑区 → `inputMethod.invokeAction(Click, 偏移)` 被调用;按住左键从预编辑区拖出 → `assert 预编辑被提交且光标回到按下点`。
- **TC-45** ImQuery 汇总:inputMethodQuery 逐项断言 ImCursorRectangle==cursorRect、ImAnchorRectangle==rectForPosition(anchor)、ImSurroundingText==block.text()、ImCursorPosition 为块内偏移、ImMaximumTextLength 无效、ImTextBefore/AfterCursor 跨块拼接正确。

### G. 撤销/重做与剪贴板

- **TC-46** 打字合并:连续输入 "abc" → `assert doc.isUndoAvailable()`;undo 一次 → 文档为空(**三条合并为一个命令**);再输入 "x" → `assert redoAvailable == false`(redo 栈被清)。
- **TC-47** 中断合并:输入 "abc" 后移动光标到 "a" 后输入 "X" → undo 两次分别还原 "X" 与 "bc"(pos 不相邻断开合并)。
- **TC-48** 删除合并方向:依次 Backspace ×3 → 单次 undo 全还原;依次 Delete ×3(光标不动删除右侧)→ 单次 undo 全还原;Backspace、Delete 交替 → 各自成命令(方向不同不互并)。
- **TC-49** 编辑块原子性:一次 drop(或 IME commit+replace)→ 单次 undo 完全还原;`doc.undoCommandAdded()` 次数 == 非块内命令数。
- **TC-50** undo 光标与信号:输入多字后 undo → `assert cursor.position()` 恢复到插入点、`microFocusChanged` 已发、`visibilityRequest` 已发(ensureCursorVisible)。注:`cursorPositionChanged` 可能由 `_q_emitCursorPosChanged`(文档编辑调整光标,isCopyOf)与 `undo()` 显式发射各来一次,断言允许 1~2 次,以 Qt 实测为准。
- **TC-51** 剪贴板链:copy → clipboard.text==选区;cut → 文本删除且 clipboard 保留;paste(富文本 mime)→ 按优先级:markdown 首位 mime 走 markdown,x-qrichtext 次之,html 再次,纯文本最后;setAcceptRichText(false) 后 paste(html+text mime)→ 仅纯文本。
- **TC-52** 中键粘贴(X11 支持选区时):先选中写 Selection 剪贴板,中键点击 p → `assert Selection 内容插入 p`;平台不支持 Selection 时中键无效果。

### H. 绘制/焦点/信号细节

- **TC-53** 选区配色:hasFocus 时 drawContents 的 PaintContext 选区 format 背景为 `palette.color(Active, Highlight)`;失焦(焦点在别处)→ Inactive 组;`SH_RichText_FullWidthSelection` 开启时 format 含 FullWidthSelection 属性。
- **TC-54** 焦点指示器选区:光标键导航聚焦链接(cursorIsFocusIndicator=true)→ 选区 format 取 `SH_TextControl_FocusIndicatorTextCharFormat` 结果;鼠标 press 后指示器复位为 false。
- **TC-55** 闪烁:cursorFlashTime=1000 → 定时器周期 500ms;tick 翻转 cursorOn 且每次发 updateRequest(光标矩形±4px);**有选区时**:SH_BlinkCursorWhenTextSelected=false → cursorOn 恒 true(不闪);失焦 → 闪烁停止、cursorOn=false、updateRequest(selectionRect) 一次。
- **TC-56** 失焦收起指示器:焦点指示器状态 + 有选区,FocusOut(reason=OtherFocusReason)→ `assert !cursor.hasSelection()`;reason=PopupFocusReason → 选区保留。
- **TC-57** 链接悬停与激活:move 进入链接 → `linkHovered(href)` 一次;移出 → `linkHovered("")` 一次;期间停在同链接不发重复信号;press+release 同一链接(无拖动位移)→ `linkActivated(href)`,**即使链接文字此前已被选中**(release 时选区已收拢,满足第一条件);从选区外起点的拖选恰好结束在链接上(press 时无选区)→ 不激活;Shift+click 扩选结束在链接上 → 同样不激活(释放时有选区且 hadSelectionOnMousePress 语义不满足,以 Qt 实测为准)。
- **TC-58** 复选框块标记:Editable 下 press+release 于 Unchecked 标记块(无选区、press/release 同块)→ `assert marker == Checked` 且 `textChanged` 因块格式变化而发;拖选存在时不切换;press 与 release 不同块不切换。

---

## 10. 易错点与 Qt 真实行为备注(对拍时需刻意一致)

1. **mightStartDrag 早退跳过尾部全部信号与 `hadSelectionOnMousePress` 更新** —— 该标志在"点选区内单击"场景保留陈旧值,链接激活条件因此依赖前一次 press 的选区状态(qwidgettextcontrol.cpp:1640-1643)。
2. **selectionChanged 的强制/非强制双轨**:拖动每次 move 强制发射(高频),键盘普通移动在无选区时不发射。
3. **只读控件的 Enter/输入/删除被 ignore,但 Ctrl+A/Ctrl+C/光标键可用**;Undo 快捷键在只读下因撤销栈禁用而无效果。
4. **普通右键 press 在 Editable/SelectableByMouse 控件上会移动光标**(无专门分支拦截)。
5. **预编辑文本不属于文档**:不触发 textChanged/contentsChanged;`ensureCursorVisible`/`rectForPosition` 需做 preedit 偏移映射。
6. **整词拖选的像素 clamp**(非 wordSelectionEnabled):鼠标 X 越出候选词范围时选区冻结。
7. **三击包含段落分隔符**(`NextCharacter` 越过分隔符),选段 selectedText 以 U+2029 结尾。
8. **overwriteMode 光标宽度**含下一字符 advance,与 QTextLine::draw 的约定一致。
9. **GraphicsSceneDrop 用 `accept()` 而非 `acceptProposedAction()`** —— 两套 DnD 路径收尾不一致是源码事实。
10. **拖选 move 中控制层不调 ensureCursorVisible**,边缘滚动属视图层;XTextControl 单体实现需自行补齐(qtextedit.cpp:1676-1690 可作参数基准:进入 100ms 后按 `4900/delta²` 周期步进滚动条)。
11. `setBlinkingCursorEnabled`/`_q_setCursorAfterUndoRedo`/`ignoreAutomaticScrollbarAdjustement` 在 6.8.3 控制层为遗留声明/未用成员(仅 QT_KEYPAD_NAVIGATION 分支引用,桌面构建不编译),XTextControl 无需实现。
12. `updateRequest(空矩形)` 约定为全量重绘(selectAll、部分焦点路径发出)。
13. 双击未命中文字(空行)仍武装三击定时器;三击选段基于**双击时光标所在块**,不做 hitTest。
14. 粘贴优先级中 `text/markdown` 仅当其为 `source->formats()` **第一项**才生效(行 2721-2726)。

## 11. 源码锚点索引(行号,qwidgettextcontrol.cpp 除非注明)

| 主题 | 行 |
|---|---|
| 私有构造默认值 | 95-120(p_p.h 成员 149-208) |
| cursorMoveKeyEvent | 122-287 |
| selectionChanged 内部 | 577-615 |
| setContent | 404-503 |
| startDrag | 505-529 |
| processEvent | 986-1163 |
| timerEvent(闪烁/三击) | 1170-1184 |
| keyPressEvent | 1206-1377 |
| rectForPosition | 1394-1439 |
| selectionRect | 1465-1547 |
| mousePress/Move/Release/DoubleClick | 1555-1663 / 1665-1757 / 1759-1856 / 1858-1898 |
| sendMouseEventToInputContext | 1900-1932 |
| DnD 四事件 | 1956-2025 |
| inputMethodEvent | 2027-2153 |
| inputMethodQuery | 2155-2226 |
| focusEvent | 2235-2262 |
| 标准菜单 | 2314-2395 |
| activateLinkUnderCursor | 2895-2962 |
| setFocusToAnchor / NextPrev | 3002-3063 |
| insertParagraphSeparator | 3194-3235 |
| append | 3237-3283 |
| ensureCursorVisible | 3286-3292 |
| getPaintContext / drawContents | 3306-3379 |
| QTextEditMimeData | 3460-3502 |
| 撤销合并 | qtextdocument_p.cpp:106-143(tryMerge)、1032-1071(appendUndoItem) |
| 视图层自动滚动 | qtextedit.cpp:1122-1146、1676-1690 |
