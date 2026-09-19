# QWidgetLineControl 行为验收清单(XGui XLineControl 对拍用)

- 审计对象:Qt 6.8.3 `qtbase/src/widgets/widgets/qwidgetlinecontrol_p.h`(525 行)与 `qwidgetlinecontrol.cpp`(1958 行),另参考 `qtbase/src/gui/text/qinputcontrol.cpp`(`isAcceptableInput`)。
- 审计方式:只读逐行通读;本文所有行号均指上述 Qt 6.8.3 源文件。
- 用途:XGui 的 `XLineControl` 实现逐条对拍。每条规则均可写成"输入序列 → 期望状态/信号序列"的断言。
- 术语约定:
  - `text` = 内部编辑文本 `m_text`(QString,UTF-16 编码);
  - `displayText` = 回显/掩码渲染后的显示文本(`m_textLayout.text()`);
  - `cursor` = 光标位置(UTF-16 code unit 下标);`sel=[selstart,selend)` 为半开区间;
  - 信号序列按实际发射顺序书写;`*` 表示该信号按实现细节可能不发射(正文有说明)。

---

## 1. 状态机清单

### 1.1 回显模式状态机(Normal / NoEcho / Password / PasswordEchoOnEdit)

#### 1.1.1 显示文本推导(updateDisplayText,cpp:54-103)

```
display(echo, text, timer活跃, editing):
  if echo == NoEcho:            s = ""
  else:                         s = text
  if echo == Password:
      s = passwordChar 重复 len(text) 次
      if passwordEchoTimer != 0 and 0 < cursor <= len(text):   # cpp:65
          s[cursor-1] = text[cursor-1]                          # 最后输入字符明文
          if cursor-1 > 0 且 text[cursor-1] 是低代理
             且 text[cursor-2] 是高代理:
              s[cursor-2] = text[cursor-2]                      # 代理对一并明文(cpp:69-75)
  elif echo == PasswordEchoOnEdit and not passwordEchoEditing:
      s = passwordChar 重复 len(text) 次
  for c in s:  # cpp:84-90 非打印字符替换
      if (unicode(c) < 0x20 且 c != 0x09(Tab)) 或 c==LineSeparator 或 c==ParagraphSeparator:
          c = ' '
  if s != 旧显示文本 or force:  emit displayTextChanged(s)
```

要点:
- `displayText()` 即上式结果;`text()` 与回显无关(密码模式下仍返回明文)。
- NoEcho 模式 preedit 也被丢弃(见 1.4)。
- 析构时若 `echoMode != Normal`,`m_text.fill(u'\0')` 清零密码内存(h:75-82);`setEchoMode` 非 Normal 时 `m_text.reserve(30)` 防 realloc 泄漏(h:252-253)。XGui 可选对拍内存行为,但显示行为不受影响。

#### 1.1.2 模式切换(setEchoMode,h:243-256)

| 迁移 | 动作 |
|---|---|
| 任意模式 → 任意模式 | `cancelPasswordEchoTimer()`;`passwordEchoEditing=false`;非 Normal 时 `reserve(30)`;`updateDisplayText()`(显示变化则发 `displayTextChanged`) |

#### 1.1.3 密码"最后字符明文"定时器(cpp:801-814, 1543-1547)

| 事件 | 状态迁移 |
|---|---|
| Password 模式下 `internalInsert`(键盘输入/paste/IME commit) | kill 旧定时器;`passwordMaskDelay > 0` 时 `startTimer(delay)`(delay 取平台主题 `PasswordMaskDelay`,`QT_BUILD_INTERNAL` 下可覆盖) |
| 定时器到期(timerEvent,cpp:1543) | kill;`passwordEchoTimer=0`;`updateDisplayText()`(明文位重新打码,发 `displayTextChanged`) |
| `cancelPasswordEchoTimer()`(h:474-480) | kill + 置 0。调用点:`setEchoMode`、`internalSetText`、`internalDelete`(实际删到字符时)、`removeSelectedText`、`internalUndo`、`updatePasswordEchoEditing` |
| `passwordEchoEditing()` 查询(h:313-317) | `passwordEchoTimer != 0` → true;否则返回 `m_passwordEchoEditing` |

#### 1.1.4 PasswordEchoOnEdit 编辑态子状态机

状态:`masking`(遮蔽,`passwordEchoEditing=false`,初态)/ `editing`(明文)。

| 当前态 | 事件 | 迁移 | 伴随动作 |
|---|---|---|---|
| masking | processKeyEvent:非只读、`event->text()` 非空、无 Ctrl 修饰、非 keypad 豁免键(Up/Down/Back/Select)(cpp:1668-1686) | → editing | `updatePasswordEchoEditing(true)`(顺带 cancel 定时器)+ `clear()`(旧内容全部清空!) |
| masking | processInputMethodEvent 且 `isGettingInput`(cpp:490-494) | → editing | `updatePasswordEchoEditing(true)`;`sel=[0,len]` 全选旧内容(随后被 removeSelectedText 删除) |
| editing | `updatePasswordEchoEditing(false)`(焦点迁出时由 QLineEdit 调用,control 内部仅 keypad Key_Back 分支调用)或 `setEchoMode` | → masking | cancel 定时器;`updateDisplayText()` |
| 任意 | `event->text()` 为空的按键(桌面平台的方向键/Home/End 等) | 不迁移 | 不清空、不切换。判定开关是 `event->text()` 是否为空,而非键值;源码注释声称"按 Left/Right 也会清空"(cpp:1682-1683),仅在那些方向键事件携带非空 text 的平台上成立 |

注意:PasswordEchoOnEdit 下 `copy()` 无效(`copy` 仅 Normal 生效,cpp:116-122);`isUndoAvailable`/`isRedoAvailable` 按密码模式规则禁用(见 1.2.4)。

### 1.2 撤销(undo/redo)分组规则

#### 1.2.1 命令类型与历史结构(h:440-449)

```
enum CommandType { Separator=0, Insert=1, Remove=2, Delete=3,
                   RemoveSelection=4, DeleteSelection=5, SetSelection=6 }
```

- 历史为 `std::vector<Command>`,`m_undoState` 指向"下一个 redo 位置"(= 已应用条数),`m_modifiedState` 与 `m_undoState` 比较得出 `isModified()`。

#### 1.2.2 addCommand 与 Separator 断点(cpp:779-789)

```
addCommand(cmd):
  erase history[m_undoState .. end)          # 新操作丢弃整条 redo 分支
  if m_separator and m_undoState>0 and history[m_undoState-1].type != Separator:
      push Separator(cursor, selstart, selend)   # 分组断点(undo 时仅作停点,不恢复状态)
  m_separator = false
  push cmd; m_undoState = history.size()
```

`separate()`(置 `m_separator=true`,h:452)的触发点:
| API | 断组时机 |
|---|---|
| `moveCursor(pos)` 且 `pos != cursor`(cpp:445-448) | 移动光标后,下一次编辑成为独立 undo 组 |
| `paste()`(cpp:132-140) | 插入前后各一次(粘贴自成一组,redo 时按 Separator 恢复光标/选区) |
| `_q_deleteSelected()`(cpp:309-319,菜单/快捷键删选区) | 删除前 |
| `clear()`(cpp:240-248) | 清空前 |
| `removeSelectedText()` 内部(cpp:900) | 选区删除自成一组 |
| `nextMaskBlank/prevMaskBlank` 跳跃(h:97-109) | 掩码下光标跨分隔符时置位 |

#### 1.2.3 internalUndo / internalRedo 的成组停机条件(cpp:1301-1385)

```
internalUndo(until = -1):
  while undoState>0 and undoState > until:
      cmd = history[--undoState]
      反转执行:
        Insert           → text.remove(pos,1); cursor=pos
        SetSelection     → 恢复 selstart/selend/cursor=cmd.pos
        Remove/RemoveSelection     → text.insert(pos,uc); cursor=pos+1
        Delete/DeleteSelection     → text.insert(pos,uc); cursor=pos
        Separator        → continue(仅消耗,不恢复)
      if until<0 and undoState>0:
          next = history[undoState-1]
          break if next.type != cmd.type
                   and next.type < RemoveSelection        # next ∈ {Separator,Insert,Remove,Delete}
                   and (cmd.type < RemoveSelection or next.type == Separator)

internalRedo():
  while undoState < history.size():
      cmd = history[undoState++]
      重放执行:
        Insert    → text.insert(pos,uc); cursor=pos+1
        SetSelection/Separator → 恢复 cursor/selstart/selend
        Remove/Delete/RemoveSelection/DeleteSelection
                  → text.remove(pos,1); selstart=cmd.selStart; selend=cmd.selEnd; cursor=pos
          # 注意:RemoveSelection/DeleteSelection 的 selStart/selEnd 为 (-1,-1),
          # redo 后 sel 为 (-1,-1) 中间态,对外 hasSelectedText()==false
      if undoState < size:
          next = history[undoState]
          break if next.type != cmd.type and cmd.type < RemoveSelection
                   and next.type != Separator
                   and (next.type < RemoveSelection or cmd.type == Separator)
```

解读成组规则:
1. 连续**同类型**普通命令(Insert/Remove/Delete)一次 undo 全撤。
2. 遇 `Separator`、遇**类型切换**(如 Insert 后接 Delete)即停。
3. 选区删除(RemoveSelection/DeleteSelection,由 `removeSelectedText` 产生)**与其后的低编号命令(Insert/Remove/Delete 及 SetSelection)合并撤销**——典型场景"选中后输入替换"一次 Ctrl+Z 完整还原。
4. `removeSelectedText` 的命令拆分(cpp:896-930):光标在选区内时按两段发出 `DeleteSelection`——左侧 `[selstart, cursor]` 用 `pos=i, selEnd=1` 标记(undo 恢复后 cursor 停在原 m_cursor),右侧映射 `pos = i - cursor + selstart - 1`;光标在选区外则逆序发 `RemoveSelection`。掩码模式下额外为每个空白位发 `Insert` 命令(cpp:918-921)。
5. 掩码下 `internalInsert` 每个字符发两条:`DeleteSelection`(被覆盖字符)+ `Insert`(cpp:825-828);掩码下 `internalDelete` 发两条:`RemoveSelection`(backspace)或 `DeleteSelection`(del)+ `Insert`(空白字符,cpp:871-880)。

#### 1.2.4 密码模式限制与 undo() 顶层行为(cpp:255-265, 1940-1954)

```
isUndoAvailable(): !readOnly && undoState>0
                   && (echo==Normal || history[undoState-1].type==Insert)
isRedoAvailable(): !readOnly && echo==Normal && undoState < history.size()

undo(): echo==Normal → internalUndo() + finishChange(-1, update=true, edited=true)
        echo!=Normal → cancelPasswordEchoTimer(); clear()   # 撤销=清空
redo(): internalRedo() + finishChange()(edited=true);密码模式 isRedoAvailable=false → no-op
```

- 密码模式下 `undo()` **不检查** `isUndoAvailable`,直接 `clear()`(只发 `textChanged`,不发 `textEdited`,见 1.2.5)。
- `clearUndo()`:history 清空,双状态归 0。`internalSetText` 同样清空历史并归零(h:745-746)——`setText()` 之后不可 undo。
- modified 语义:`isModified() = (modifiedState != undoState)`;`setModified(true)` 置 -1(恒为"已修改");`setModified(false)` = 当前 undoState。

#### 1.2.5 finishChange:验证、回滚与信号收敛(cpp:672-724)

```
finishChange(validateFromState=-1, update=false, edited=true):
  if textDirty:
      wasValid = validInput; validInput = true
      if validator:
          validInput = (validate(text', cursor') != Invalid)
          if validInput:
              if text != text': internalSetText(text', cursor', edited); return true  # validator 改写文本
              cursor = cursor'
          else: emit inputRejected()
      if validateFromState>=0 and wasValid and !valid:      # 本次输入被 validator 拒绝
          if transactions 非空: return false                # 本版 transactions 恒空(死代码)
          internalUndo(validateFromState)                   # 回滚到操作前
          erase history[undoState..end)                     # 丢弃被拒命令
          if modifiedState > undoState: modifiedState = -1
          validInput=true; textDirty=false                  # 不发 textChanged/textEdited
      updateDisplayText()
      if textDirty:                      # 仍未回滚:正式提交
          textDirty=false
          actual = text()                # 掩码剥离 blank 后的文本!
          if edited: emit textEdited(actual)
          emit textChanged(actual)
  if selDirty: selDirty=false; emit selectionChanged()
  if cursor == lastCursorPos: emit updateMicroFocus()
  emitCursorPositionChanged()            # cursor != lastCursorPos 时发 cursorPositionChanged(old,new)
  return true
```

各 API 的 `edited` 取值(决定 `textEdited` 是否发射):

| API | validateFromState | edited | 备注 |
|---|---|---|---|
| `insert` / `backspace` / `del` / `paste` / `removeSelection` / `_q_deleteSelected` | priorState=undoState | true | 用户编辑路径 |
| `undo()`(Normal) | -1 | true | undo 也发 `textEdited` |
| `redo()` | -1(默认) | true(默认) | 发 `textEdited` |
| `clear()` | priorState | **false** | 只发 `textChanged` |
| `setText` → `internalSetText(txt,-1,false)` | -1 | false | 只发 `textChanged` |
| IME 事件(`isGettingInput` 时) | priorState | true | commit 视作用户编辑 |

### 1.3 输入掩码解析

#### 1.3.1 parseInputMask(cpp:938-1036)

```
parseInputMask(maskFields):
  delim = maskFields.indexOf(';')
  if maskFields 为空 or delim==0:            # "" 或 ";..."
      掩码取消: maskData=null; maxLength=32767; internalSetText("",-1,false); return
  if delim==-1: inputMask=maskFields; blank=' '
  else:         inputMask=左段;   blank=(delim+1<len)? maskFields[delim+1] : ' '
  maxLength = Σ(每个字符计 1,但 '\' 不计、'\x' 转义对计 1、'!' '<' '>' '{' '}' '[' ']' 不计)
  caseMode=NoCase; 逐字符生成 maskData:
      '\' → 转义:下一字符为字面 separator
      '<' → caseMode=Lower; '>' → Upper; '!' → NoCase(重置)
      '{' '}' '[' ']' → 跳过(不产出条目)
      A a N n X x 9 0 D d # H h B b → 输入位(separator=false)
      其他字符 → 字面 separator(separator=true),受当时 caseMode 影响(仅输入位有效)
  internalSetText(m_text, -1, edited=false)   # 旧文本重新套掩码
setInputMask 后:若掩码有效,moveCursor(nextMaskBlank(0))  # 光标落第一个可输入位(h:297-302)
```

- 掩码生效期间 `m_text` **恒定长度 = maxLength**(所有编辑以替换/补 blank 维持定长);`m_maxLength` 被改写,`setMaxLength` 在掩码模式下为 no-op(h:259-265)。
- `inputMask()` getter 返回原掩码串;`blank != ' '` 时追加 `";<blank>"`(h:285-296)。

#### 1.3.2 isValidInput 逐 mask 字符判定(cpp:1044-1111)

| mask | 接受的 key | mask | 接受的 key |
|---|---|---|---|
| `A` | isLetter | `a` | isLetter 或 blank |
| `N` | isLetterOrNumber | `n` | isLetterOrNumber 或 blank |
| `X` | isPrint 且 != blank | `x` | isPrint 或 blank |
| `9` | isNumber | `0` | isNumber 或 blank |
| `D` | isNumber 且 digitValue>0(即 1-9) | `d` | 1-9 或 blank |
| `#` | isNumber 或 '+' '-' 或 blank | | |
| `B` | '0' 或 '1' | `b` | '0' '1' 或 blank |
| `H` | isNumber 或 [a-f] 或 [A-F] | `h` | 同 H 或 blank |

其余(字面 separator)不接受任何 key。

#### 1.3.3 maskString(cpp:1157-1222,把输入串套到掩码上)

```
maskString(pos, str, clear=false):
  if pos >= maxLength: return ""
  fill = clear ? clearString(0,maxLength) : m_text   # clear=true → 全 blank 底板
  i=pos; strIndex=0; s=""
  while i < maxLength and strIndex < str.size():
      if maskData[i] 是 separator:
          s += maskChar
          if str[strIndex]==maskChar: strIndex++     # 输入里的分隔符被消费(可跳过)
          i++
      else:
          if isValidInput(str[strIndex], maskChar):
              s += str[strIndex](按 caseMode Upper/Lower 转换); i++; strIndex++
          else:
              n = findInMask(i, forward=true, findSeparator=true, ch)
              if n != -1:                            # ch 是前方的 separator 字符
                  if str.size()!=1 or i==0 or (maskData[i-1] 非"同字符 separator"):
                      s += fill[i..n]; i = n+1       # 连带中间位一起跳(输入 IP 时补 ".")
                  # 否则本轮不跳,仅 strIndex++(防"输入恰为前一分隔符"误跳)
              else:
                  n = findInMask(i, forward=true, findSeparator=false, ch)  # ch 能落在哪个输入位
                  if n != -1:
                      s += fill[i..n-1]              # 中间未填位保持 blank/fill
                      s += ch(case 转换); i = n+1
                  # 找不到 → ch 丢弃
              strIndex++
  return s
```

#### 1.3.4 clearString / stripString / findInMask / next·prevMaskBlank

```
clearString(pos,len):  [pos, min(maxLength,pos+len)) 每位:separator→maskChar,否则→blank;pos>=maxLength → null
stripString(str):      separator 位 → maskChar(原样保留);非 separator 且 != blank → 保留;blank 位剔除
                       # text() 掩码模式即 stripString(m_text);"127.0__.0__.1__" → "127.0.0.1"
                       # 结果为 null(空)时 text() 返回 ""(h:208-213)
findInMask(pos,forward,findSeparator,ch):
  pos 越界(<0 或 >=maxLength) → -1
  forward: i 从 pos 到 maxLength-1;backward: 从 pos 到 0
  findSeparator=true:  找 separator 且 maskChar==ch
  findSeparator=false: 找输入位;ch 为 null → 第一个输入位;否则 isValidInput(ch, maskChar) 命中
nextMaskBlank(pos): c=findInMask(pos,true,false);(m_separator |= c!=pos);返回 c==-1 ? maxLength : c
prevMaskBlank(pos): c=findInMask(pos,false,false);(同上);返回 c==-1 ? 0 : c
```

#### 1.3.5 掩码下的编辑与光标

| 操作 | 行为 |
|---|---|
| `moveCursor(pos)`(cpp:441-470) | `pos != cursor` 时先 separate;`pos>cursor → nextMaskBlank(pos)`,否则 `prevMaskBlank(pos)`(跨 separator 跳到最近可输入位;右移失败落 maxLength,左移失败落 0) |
| `insert(s)`(cpp:817-836) | `ms = maskString(cursor, s)`;`ms` 空且 `s` 非空 → emit `inputRejected`;用 `ms` **替换** cursor 起的等长段(掩码位被覆盖);cursor += len(ms) 后 `nextMaskBlank` |
| backspace(cpp:174-196) | cursor>0:cursor 左移后 `prevMaskBlank`;`internalDelete(true)` 将该位**替换为 blank**(不缩短文本) |
| del(cpp:207-218) | 有选区删选区;无选区时按 `nextCursorPosition` 的步长逐单元 `internalDelete()`(每个单元替换为 blank) |
| `clear()` | `removeSelectedText([0,maxLength))` → 整串替换为掩码模板(全 blank + separator),**不是空串** |
| `setText(t)`(cpp:737-741) | `m_text = maskString(0, t, clear=true) + clearString(len(ms), 余长)`;`edited` 且结果与旧文本相同 → emit `inputRejected`(setText 路径 edited=false,不发) |
| `hasAcceptableInput`(cpp:1121-1147) | 见 3.2 |

### 1.4 IME preedit 生命周期

- `composeMode() = preedit 区域文本非空`(h:306)。preedit 不进入 `m_text`、不进 undo 历史,只通过 `QTextLayout::setPreeditArea(cursor, preedit)` 参与显示。

| 阶段 | 行为 |
|---|---|
| start(preedit 到来) | `processInputMethodEvent`(cpp:478-588):`isGettingInput = commitString 非空 ‖ preeditString != 旧 preedit ‖ replacementLength>0`;preedit-only 事件也满足(第二次起 preedit 内容变化),但 `m_text` 不变 → finishChange 无 text 信号。放置规则:**NoEcho → `setPreeditArea(0, "")`(丢弃);Password → preedit 逐字符打码;其他 → `setPreeditArea(m_cursor, preeditString)`**(cpp:538-553) |
| preedit 内光标 | 默认 `m_preeditCursor = preeditString.size()`(末尾);事件带 `Cursor` 属性 → `m_preeditCursor = a.start`,`m_hideCursor = !a.length`;`TextFormat` 属性 → 逐段格式(start 偏移 + m_cursor)(cpp:555-576) |
| commit | commitString 非空:走 `internalInsert`(受 maxLength/掩码/validator/undo 全约束);`cursorPositionChanged` 标记;随后 preedit 区清空(本事件 preeditString 为空)。commit 视为用户编辑:`finishChange(priorState, edited=true)` → 发 `textEdited` |
| cancel/reset | `setText()` 在 composeMode 下先 `inputMethod()->reset()`(不是 commit,h:214-221);`commitPreedit()`(cpp:147-162):调 `inputMethod()->commit()` 后若仍 composeMode,兜底 `m_preeditCursor=0; setPreeditArea(-1,""); clearFormats(); updateDisplayText(force)` |
| 光标联动 | **`moveCursor` 与 `setSelection` 开头强制 `commitPreedit()`**(h:277, 443)——键盘移动/点击选区即提交 preedit;`cursorRect`/`draw` 的光标 x = `cursor + preeditCursor`;`m_hideCursor` 时主光标不绘制(cpp:630-636) |
| IME Selection 属性 | 直接在 `m_text` 上设光标/选区(qBound + swap),`selectionChange` 标志 → 末尾 emit `selectionChanged()`(cpp:519-537) |
| 信号收敛 | 末尾:光标真变化(仅 commit 或 Selection 属性导致)→ `emitCursorPositionChanged`;否则 `preeditCursor` 变化 → emit `updateMicroFocus()`;`isGettingInput` → `finishChange(priorState)`;`selectionChange` → `selectionChanged()`(cpp:577-587) |

---

## 2. 信号矩阵(11 个信号)

| # | 信号(参数) | 触发条件与发射点 | 参数内容 |
|---|---|---|---|
| 1 | `cursorPositionChanged(int,int)` | `emitCursorPositionChanged`(cpp:1393-1407):每次 `m_cursor != m_lastCursorPos` 收敛时。路径:moveCursor/insert/backspace/del/undo/redo/internalSetText/finishChange/IME commit | `(oldLast, newCursor)`,old 为上次发射值 |
| 2 | `selectionChanged()` | (a)`moveCursor`:`mark==true`(**无论选区是否变化,恒发**,cpp:465-468)或 `selDirty`(曾清除非空选区);(b)`finishChange` 中 `selDirty`(internalDeselect 曾有选区、paste 后等);(c)`setSelection` 选区实际变化后恒发(cpp:305);(d)IME Selection 属性改变选区(cpp:587)。无参 | — |
| 3 | `displayTextChanged(QString)` | `updateDisplayText`:新显示文本 != 旧显示文本,或 `force=true`(IME 事件、commitPreedit 兜底)。回显模式切换/密码定时器到期/preedit 变化都会触发;`m_text` 不变也可能触发 | 回显后的显示文本 |
| 4 | `textChanged(QString)` | `finishChange` 提交路径:`textDirty` 未被回滚(cpp:708-714)。覆盖 insert/backspace/del/paste/clear/undo/redo/setText/IME commit/validator 改写 | `text()`(掩码剥离后),非 `m_text` 原文 |
| 5 | `textEdited(QString)` | 同上但 `edited==true`:用户编辑路径(insert/backspace/del/paste/removeSelection/_q_deleteSelected)、undo()、redo()、IME commit。**不发**:setText、clear()、掩码下 setText 无变化 | 同 `text()` |
| 6 | `resetInputContext()` | `internalSetText`(h:735)与 `_q_deleteSelected`(cpp:315)。请求 IM 上下文重建 | — |
| 7 | `updateMicroFocus()` | (a)`finishChange` 中 `cursor == lastCursorPos`(光标没动但文本变了,cpp:720-721);(b)IME 事件中仅 `preeditCursor` 变化(cpp:580-581) | — |
| 8 | `accepted()` | `processKeyEvent` 收到 Enter/Return 且 `hasAcceptableInput() \|\| fixup()` 成功(cpp:1649-1665)。**不受 readOnly 限制** | — |
| 9 | `editingFinished()` | 与 8 完全同条件,**紧随 accepted 之后**发射;同时 IM commit + (非 ImhMultiLine)IM 隐藏 | — |
| 10 | `updateNeeded(QRect)` | (a)`updateCursorBlinking`(cpp:1501-1516);(b)光标闪烁 timerEvent 每半周期(cpp:1533-1535) | 无掩码:`cursorRect()`;有掩码:`QRect()`(全区域重绘) |
| 11 | `inputRejected()` | (a)`finishChange`:validator 判 Invalid(cpp:692);(b)`internalInsert`:无掩码时 `s.size() > remaining`(含 remaining==0 即满员再插,cpp:849-850)、掩码时 `ms` 空但 `s` 非空(cpp:818-820);(c)`internalSetText`:掩码模式且 `edited` 且结果与旧文相同(cpp:740-741) | — |
| (+) | `editFocusChange(bool)` | 仅 `QT_KEYPAD_NAVIGATION` 编译期启用:keypad 导航下长按 Back 750ms 触发 `clear()` 时(cpp:1887-1904)。桌面构建不存在 | — |

补充发射顺序要点:
- `finishChange` 尾部顺序:`selectionChanged`(若 selDirty)→ `updateMicroFocus`(光标未变)→ `cursorPositionChanged`(光标变了,互斥)。
- Enter 成功序列:`accepted` → `editingFinished`(中间夹 IM commit/hide,不发本控件信号)。
- `textChanged`/`textEdited` 先于 `cursorPositionChanged`(同为 finishChange 内先后)。

---

## 3. 边界行为表

### 3.1 maxLength 截断

| 场景 | 期望行为 |
|---|---|
| 无掩码 `insert(s)`,`len(text)+len(s) > maxLength` | 插入 `s.left(maxLength - len(text))`;`s.size() > remaining` → emit `inputRejected`(remaining==0 即已满员时也发,但文本不变、不发 textChanged)(cpp:838-851) |
| 无掩码 `setText(t)` 超长 | `m_text = t.left(maxLength)`;只发 `textChanged` |
| `setMaxLength(n)` | 掩码模式下 no-op;否则立即 `setText(m_text)` 二次截断(h:259-265) |
| 截断粒度 | **UTF-16 code unit**,可能把 BMP 外字符截成半个代理对(悬挂高代理);`text()` 返回含悬挂代理的串 |
| `setCursorPosition(pos)` | `pos > size` 时**不动**(不 clamp);`pos<0` 取 0(h:280) |
| maxLength 默认 32767 | 掩码模式下被改写为掩码长度;取消掩码时恢复 32767 |

### 3.2 掩码中间态 hasAcceptableInput(cpp:1121-1147)

```
hasAcceptableInput = (无 validator 或 validate==Acceptable)
                     and (无掩码 or (len==maxLength 且逐位合法))
逐位合法:separator 位 str[i]==maskChar;输入位 isValidInput(str[i], mask)
```

| 场景 | 期望 |
|---|---|
| 输入进行中(部分 blank) | `9/A/X/D/H/B` 位为 blank → **不** acceptable;`a/n/0/d/x/h/#/b` 位为 blank → 合法(blank 允许)。故 `"000.000.000.000"` 类掩码填一半时 `hasAcceptableInput()==false`,但编辑继续、`textChanged` 照常发射 |
| validator 判 Intermediate | `validInput=true`(!=Invalid),不回滚、不 reject;判 Invalid 才走 `inputRejected`。**回滚**(吞掉输入)仅在 `validateFromState>=0` 的用户编辑路径发生;`setText`/`undo`/`redo` 路径 `validateFromState=-1`:Invalid 时只发 `inputRejected`,文本保留并发 textChanged(见 1.2.5) |
| 掩码下 `text()` | `stripString` 剥 blank 保 separator;`textChanged` 的参数即该剥离值 |
| 掩码下非法字符键入 | 被丢弃或跳位(见 1.3.3);若整串无一位可落 → `inputRejected`,文本不变 |
| Enter 时 `hasAcceptableInput()==false` | 先 `fixup()`:validator->fixup 后若 Acceptable → `internalSetText`(edited=false,清 undo 历史,发 textChanged)并同 Enter 成功路径;无 validator 或 fixup 无效 → 不发 accepted/editingFinished,`event->ignore()`(cpp:418-433, 1649-1665) |

### 3.3 选择与删除组合

| 场景 | 期望行为 |
|---|---|
| `backspace()`/`del()`/`insert()` 时有选区 | 先 `removeSelectedText()`(删除本身含 separate+SetSelection 记录),无选区才逐字符删(cpp:174-233) |
| 光标在选区内删除 | undo 命令拆两段 DeleteSelection(左段 selEnd=1 恢复光标、右段 pos 映射),undo 一次恢复"文本+光标在原 m_cursor"(cpp:903-913) |
| 光标在选区外删除 | 逆序 RemoveSelection;undo 后光标恢复到删除前的 m_cursor(经 SetSelection 命令) |
| 删除后光标 | `cursor > selstart` → `cursor -= min(cursor, selend) - selstart`(cpp:925-926) |
| `deselect()` | 清选区;若曾有选区,`finishChange` 发 `selectionChanged()`(不发 text 系信号)(h:233) |
| `selectAll()` | sel=[0,size],cursor=size;经 moveCursor(mark=true) → 恒发 `selectionChanged`(h:234) |
| `setSelection(start,len)` | 先 commitPreedit;start 越界 → qWarning+返回;len>0 光标=尾、len<0 光标=头、len==0 清选区光标=start;与现有选区完全相同且光标在对应端 → 直接 return 不发信号(cpp:275-307) |
| `copy()` | 仅 `echo==Normal` 且选区文本非空才写剪贴板(密码模式复制被静默丢弃)(cpp:116-122) |
| `cut()` | processKeyEvent:`!readOnly && 有选区` → copy()+del();无选区什么都不做(cpp:1722-1727) |
| `paste()` | 剪贴板空且无选区 → no-op;否则 separate+insert+separate(独立 undo 组)(cpp:132-140) |
| 有选区时按 →(MoveToNextChar) | 非 Windows 键盘方案(或 inline completer):光标移到选区尾并取消选区;**Windows 方案:不取消,直接 cursorForward 一格**(从 m_cursor 起算)(cpp:1748-1761);← 对称到选区头 |
| `removeSelection()`(h:145-150) | 删选区 + finishChange(edited=true) → 发 textEdited/textChanged |
| `_q_deleteSelected()` | 有选区才动作;发 `resetInputContext`,删除后 separate(cpp:309-319) |

### 3.4 UTF-16 / grapheme / home / end / 词移动

Qt 内部文本为 **UTF-16**;BMP 外字符(4 字节 UTF-8,如 emoji)为代理对(2 个 code unit)。

| 场景 | 期望行为 |
|---|---|
| backspace 遇代理对 | cursor 左移 1 后若指向低代理且前为高代理 → 连续两次 `internalDelete(true)` 一次删完整对;undo 为两条 Remove(同类相邻,一次 Ctrl+Z 成对恢复)(cpp:183-193) |
| del 遇代理对 | 步长 `n = nextCursorPosition(cursor)-cursor`(grapheme 级,代理对 n=2),循环 2 次 `internalDelete()`;undo 同类成组一次恢复(cpp:212-216) |
| insert 代理对 | 按 code unit 逐条 `Insert` 命令(2 条),undo 成组恢复 |
| 密码即时回显遇代理对 | 末字符为低代理且前为高代理 → 两个 code unit 同时明文,不出现半字符明文(cpp:69-75) |
| 键入代理对(`isAcceptableInput`) | text 首字符为高代理且带低代理 → 接受;Ctrl/Ctrl+Shift 修饰 → 拒绝;Other_Format(ZWNJ/ZWJ/RLM 等)恒接受;`isPrint` 或 PrivateUse 接受;`<0x20` 且非 Tab 不接受(qinputcontrol.cpp:21-53)。LineEdit 类型不接受 Tab |
| home(mark) | `moveCursor(0, mark)`;掩码下经 `prevMaskBlank(0)` 落在第一个可输入位 |
| end(mark) | `moveCursor(size, mark)`;掩码下 `nextMaskBlank(size)` 越界返回 maxLength==size |
| 词移动(Ctrl+←/→) | `QTextLayout` SkipWords(grapheme/词簇边界,多字节字符视为一个单元);**非 Normal 回显模式(Password 等):NextWord→end、PreviousWord→home(注意 PreviousWord 分支有 `!readOnly` 检查而 NextWord 没有,cpp:1782-1794)**;Visual 移动风格时 ←/→ 用 left/rightCursorPosition,否则按 layoutDirection 逻辑方向(cpp:1748-1781) |
| 光标步进(cursorForward) | 每步经 QTextLayout next/previousCursorPosition(逻辑)或 left/rightCursorPosition(Visual);代理对一步跨过 |
| `updateDisplayText` 非打印字符 | `<0x20`(除 Tab)、LineSeparator、ParagraphSeparator → 显示为空格;Tab 原样(能否键入取决于 isAcceptableInput,LineEditer 类型不收 Tab) |

### 3.5 其他值得对拍的行为

| 场景 | 期望 |
|---|---|
| readOnly | 禁:undo/redo/insert/backspace/del/paste/cut/字符输入/SelectAll 之外的修改;**不禁**:copy、光标移动、selectAll、Enter 的 accepted/editingFinished |
| `undo()` 在密码模式 | 无条件 `clear()`(先 cancelPasswordEchoTimer);不检查 isUndoAvailable |
| redo 后选区中间态 | RemoveSelection/DeleteSelection 重放后 `sel=(-1,-1)`(对外表现为无选区) |
| `m_transactions` | 本版本恒空;`finishChange` 回滚分支 `if (m_transactions.size()) return false` 为死分支,可忽略 |
| Enter 与 readOnly | readOnly 下仍发 accepted/editingFinished(processKeyEvent 无 readOnly 检查) |
| X11 粘贴 | X11 键盘方案下 Ctrl+Shift+Insert 粘贴 **Selection** 剪贴板(cpp:1711-1720);任何已处理按键后若平台支持 Selection,`copy(Selection)` 同步主选区(cpp:1932-1934) |
| 方向键切换布局 | Key_Direction_L/R → `setLayoutDirection` 并吞掉按键(cpp:1913-1916);layoutDirection=Auto 时按文本首字符方向渲染(h:325-329) |
| 光标闪烁 | flashTime>=2 才启动,半周期翻转 `blinkStatus`,每翻转发 `updateNeeded`;readOnly 停闪(cpp:1486-1548) |

---

## 4. 验收用例(64 条可脚本化断言)

约定伪 API(XLineControl 对等操作):
`insert(s)`、`key(ch)`=processKeyEvent 字符键、`K(name,mods)`=processKeyEvent 功能键、`backspace()`、`del()`、`undo()`、`redo()`、`clear()`、`setText(s)`、`selectAll()`、`setSel(start,len)`、`moveCursor(p,mark)`、`copy()`、`cut()`、`paste(s)`(预置剪贴板)、`ime(commit=,preedit=,cursor=)`(processInputMethodEvent)、`tickPwd()`(密码定时器到期)、`setEcho(m)`、`setMask(mask)`、`setMaxLength(n)`、`setValidator(v)`。
断言:`T`(text)、`D`(displayText)、`C`(cursor)、`S`(选区,"-"表示无)、`SIG[...]`(信号序列,按序)。初始态:空文本、cursor=0、echo=Normal、无掩码、无 validator。

### A. 回显模式

| # | 前置 | 输入序列 | 期望 |
|---|---|---|---|
| A1 | echo=Normal | `key("a")` | `T="a" D="a" SIG[textChanged("a"), textEdited("a")]` |
| A2 | echo=NoEcho | `key("a"); key("b")` | `T="ab" D=""`;全程**无 displayTextChanged**(显示串恒为 "" 与旧值相同),仅 textEdited/textChanged/cursorPositionChanged |
| A3 | echo=Password,passwordMaskDelay>0(需平台主题或 override 配置) | `key("a"); key("b")` | 第二键后 timer 活跃:`D="*b"`(**末**输入字符明文);`tickPwd()` 后 `D="**"`;`T` 恒 "ab";`copy()` 后剪贴板仍空 |
| A4 | echo=Password | `key("😀")`(代理对);`tickPwd()` | 明文窗口期内 `D` 完整回显该代理对(两个 code unit 一同明文,cpp:69-75);到期后全打码(2 个密码符) |
| A5 | echo=PasswordEchoOnEdit,`setText("old")`(C=3) | `key("x")` | 信号全序:`displayTextChanged("old")`(切换 editing 态明文)→ `textChanged("")`(clear 路径,无 textEdited)→ `cursorPositionChanged(3,0)` → `textEdited("x")` → `textChanged("x")` → `cursorPositionChanged(0,1)`;终态 `T="x" D="x"` |
| A6 | echo=PasswordEchoOnEdit,`setText("old")` | `K(Left)` 后 `key("x")` | Left(text 为空)**不清空不切换**:仅 `cursorPositionChanged(3,2)`,`T="old" D="****"`;随后 "x" 触发 A5 全流程(清空重输) |
| A7 | echo=PasswordEchoOnEdit | `ime(commit="a", preedit="")` | 同 A5:IME 输入触发 editing 态并清空旧文 |
| A8 | echo=Normal,`key("a")` 后 `setEcho(Password)` | — | `D="*"`;SIG 仅 `displayTextChanged("*")`(text 系不发,T="a") |
| A9 | echo=Password,`setText("abc")` 后 `undo()` | — | `T="" D=""` SIG 含 `textChanged("")` 不含 textEdited;redo 无效(`isRedoAvailable=false`,redo 后 T="" 不变) |
| A10 | echo=Normal,含控制字符文本 `setText("a\x01b")` | — | `T="a\x01b" D="a b"`(0x01 显示为空格),`displayTextChanged` 参数为 "a b" |

### B. 撤销/重做分组

| # | 前置 | 输入序列 | 期望 |
|---|---|---|---|
| B1 | — | `key("a");key("b");key("c");undo()` | 连续键入为一组:一次 undo 全撤,`T="" C=0`;SIG[textEdited(""), textChanged("")] |
| B2 | — | `key("ab" 逐字符)`;`moveCursor(0)`;`key("x")`;`undo()` | 光标移动断组:undo 只撤 "x" 的插入,`T="ab"`;再 undo → `T=""` |
| B3 | — | `setText("ab")` 后 `key("c"); paste("xy")`;`undo()` | 粘贴独立成组:一次 undo 撤销整个 "xy"(`T="abc"`,光标回 paste 前);**不会**连带撤 "c" |
| B4 | — | `setText("abc")`;`setSel(1,1)`(选中 "b",C=2);`key("X")` 替换;`undo()` | 选区替换一次全撤:`T="abc"`,且**选区一并恢复** `S=[1,2) C=2`(undo 链:Insert → RemoveSelection → SetSelection 因选区类命令合并规则连续回滚);SIG[textEdited("abc"), textChanged("abc")] |
| B5 | B4 后 | `redo()` | `T="aXc"`;重放后选区对外无("-"),C 恢复 |
| B6 | — | `setText("abcd")`;`moveCursor(2)`;`K(Delete)`(删 'c');`backspace()`(删 'b') | `T="ad" C=2`;`undo()`:Remove(backspace) 与 Delete(前置)类型不同不合并 → 一次 undo 只恢复 'b' 得 `T="abd"`;再 undo 恢复 'c' 得 `T="abcd"` |
| B7 | — | `setText("hi")`;`clear()`;`undo()` | `clear()` 后 `T=""` SIG[textChanged("")] 无 textEdited;undo 恢复 `T="hi" C=2 S=[0,2)`(undo 链连 SetSelection 一并恢复)且发 textEdited("hi")/textChanged("hi") |
| B8 | — | `setText("hi")` 后立即 `undo()` | setText 清空历史:undo 无效果,T="hi" 不变,isUndoAvailable=false;redo 同样无效 |
| B9 | — | `key("a");undo();redo()` | undo:`T="" C=0` SIG[textEdited(""), textChanged("")];redo:`T="a" C=1` SIG[textEdited("a"), textChanged("a"), cursorPositionChanged(0,1)] |
| B10 | validator=整数[0-100],`setText("12")` | `key("9")`(得 129,Invalid) | 回滚:输入被吞,`T="12" C=2` 不变;SIG[inputRejected],无 textChanged/Edited;历史被 erase,此后 undo 无效 |
| B11 | validator 整数,`setText("1")` | `key("2")`(12,Intermediate) | 不回滚:`T="12"` 正常提交,发 textChanged/textEdited,无 inputRejected |

### C. 输入掩码

| # | 前置 | 输入序列 | 期望 |
|---|---|---|---|
| C1 | `setMask("9999-99-99;_")`(blank='_') | — | `D="____-__-__" C=0`(光标落第一个输入位);`T="---"`(stripString **恒保留 separator**,空模板的 text() 是 "---" 而非 "");SIG[displayTextChanged] |
| C2 | C1 | `key("2");key("0")` | 第一键后 `D="2___-__-__" T="2--" C=1`;第二键后 `D="20__-__-__" T="20--" C=2`;textChanged 参数为 "2--"/"20--"(剥 blank、留 '-') |
| C3 | C2 末态(C=2) | `key("-")` | maskString separator 匹配分支:输入 '-' 落到位置 4,连中间 blank 一起补,`D` 不变(本就同形),`C: 2→5`(nextMaskBlank 跳过 '-'),`T="20--"`;再 `key("2")` → 常规落位 `D="202_-__-__" C=4` |
| C4 | C1 | `key("a")` | 'a' 无任何 '9' 位可落且非 separator → 整串被丢:ms 为空 → `SIG[inputRejected]`,D/T/C 全不变,无 textChanged |
| C5 | `setMask("AAAA")` | `key("a");key("1")` | "a" 落位 `D="a___" T="a" C=1`;"1" 在 `A` 位非法且掩码无 separator 可跳 → `SIG[inputRejected]`,文本不变 |
| C6 | `setMask(">AAA")` | `key("abc")` | 大小写转换:`D="ABC_" T="ABC" C=3` |
| C7 | `setMask("AAAA")` | `setText("xy")` | internalSetText 走 `maskString(0,txt,clear=true)`:`D="xy__" T="xy" C=4`;edited=false → 发 textChanged("xy") 不发 textEdited |
| C8 | `setMask("\\9")`(转义字面 '9') | — 后 `key("9")` | 位 0 是字面 separator:`D="9" T="9" C=1`(nextMaskBlank 无输入位 → maxLength);再击 "9":maskString 越界得空 → `SIG[inputRejected]` |
| C9 | `setMask("0")`(可收 blank) | — | `D="_"`;**不输入任何字符**:`hasAcceptableInput()==true`(blank 对 '0' 合法);Enter 可发 accepted |
| C10 | `setMask("9")` | — | `D="_"`;`hasAcceptableInput()==false`(blank 对 '9' 不合法);Enter 无 accepted/editingFinished,`event->ignore()` |
| C11 | `setMask("D")` | `key("0")` | 'D' 要求 1-9:"0" 被拒,`SIG[inputRejected]`;`key("5")` → `D="5" T="5"` |
| C12 | C1 完整键入 "20240101"(8 键) | `undo()` | **一次 undo 撤全部**:掩码下每字符产生 DeleteSelection+Insert 相邻对,合并规则使选区删除类与相邻命令连锁回滚 → `D="____-__-__" T="---"`;SIG[textEdited("---"), textChanged("---")] |
| C13 | C1 后 `clear()` | — | `D="____-__-__" T="---"`(掩码模式 clear 得模板,非空串);SIG[textChanged("---")] 无 textEdited |
| C14 | C2 末态(C=2) | `backspace()` | 位置 1 是输入位:替换为 blank,`D="2___-__-__" T="2--" C=1`(文本定长不缩短) |
| C15 | `setMask("#")` | `key("+")` | '#' 收 '+' `-/数字`:`D="+" T="+"`;`hasAcceptableInput()==true` |

### D. IME preedit

| # | 前置 | 输入序列 | 期望 |
|---|---|---|---|
| D1 | echo=Normal | `ime(preedit="ni", cursor=2)` | `D="ni" T=""`;preedit 不进 undo;`preeditAreaText()=="ni"`;SIG[displayTextChanged("ni")] |
| D2 | D1 | `ime(preedit="nih", cursor=3)` | `D="nih"`(preedit 更新);无 textChanged/textEdited(m_text 未变) |
| D3 | D2 | `ime(commit="你", preedit="")` | `T="你" D="你"`;SIG[textChanged("你"), textEdited("你"), cursorPositionChanged(0,1)];undo 一次撤整个 commit |
| D4 | D1 | `K(Left)` | 移动光标先 commitPreedit:preedit 被提交/清除(平台 IM commit 空 commit 时兜底清空),`preeditAreaText()==""` |
| D5 | echo=Password | `ime(preedit="abc", cursor=3)` | preedit 打码:`D` 中 preedit 段全为密码符,不泄露;commit 前明文不可见 |
| D6 | echo=NoEcho | `ime(preedit="abc", cursor=3)` | preedit 被丢弃:`preeditAreaText()=="" D=""` |
| D7 | `setText("ab")`,选中 [1,2) | `ime(commit="X")` | isGettingInput → 选区被删后插入:`T="aX" C=2`;SIG 含 textEdited/textChanged("aX") |
| D8 | echo=PasswordEchoOnEdit,`setText("old")` | `ime(preedit="p", cursor=1)` | 见 A7:旧文清空、进入 editing 态 |

### E. maxLength / 选择 / UTF-16 / 移动

| # | 前置 | 输入序列 | 期望 |
|---|---|---|---|
| E1 | setMaxLength(3) | `insert("abcde")` | `T="abc" C=3`;SIG[textChanged("abc"), textEdited("abc"), inputRejected] |
| E2 | setMaxLength(3),`setText("abc")` | `key("d")` | 文本不变;SIG 仅 `updateMicroFocus` + `inputRejected`(无 text 系/displayTextChanged 信号) |
| E3 | setMaxLength(3) | `setText("abcd")` | `T="abc"`(left 截断);仅 textChanged("abc") |
| E4 | setMaxLength(1) | `setText("😀")`(代理对) | `left(1)` 截出**悬挂高代理**:`len(T)==1 且 T[0].isHighSurrogate()`;插入路径 `insert("😀")` 同样得悬挂代理且另发 `inputRejected`(s.size 2 > remaining 1) |
| E5 | — | `insert("😀")`(2 code unit);`backspace()` | 一次删除整个代理对:`T="" C=0`;undo 一次恢复 `T="😀" C=1` |
| E6 | — | `insert("😀")`;`del()`(cursor=0) | 同 E5:一次删除整对(grapheme 步长 2,循环两次 internalDelete);SIG 一次 textEdited("")/textChanged("") |
| E7 | — | `insert("ab😀c")`(T 长 5,C=5);`moveCursor(4)` 后 `backspace()` | `T="abc" C=2`(backspace 识别低代理+高代理,两次 internalDelete 删整对);undo 一次恢复整对 |
| E8 | `setText("hello world")`,`moveCursor(5)` | `cursorWordForward` 键(Ctrl+→) | `C=11`(SkipWords 到词尾);再 Ctrl+← → `C=6`(world 词首);`selectWordAtPos` 双击 [0,5) 选中 "hello" |
| E9 | `setText("ab")`(C=2) | `moveCursor(0, true)` | `S=[0,2) C=0`;SIG[selectionChanged, cursorPositionChanged(2,0)](mark 路径恒发 selectionChanged) |
| E10 | E9 后 | `K(Right)` | 有选区 + 非 Windows 方案:取消选区光标到选区尾 `C=2 S=-`;SIG[selectionChanged, cursorPositionChanged(0,2)](Windows 方案则改为 cursorForward 一格:C 0→1) |
| E11 | — | `selectAll()` 后 `backspace()` | `T=""`;undo 一次恢复全文(选区删除独立组) |
| E12 | — | `setText("abc"); setSel(0,3); cut()` | 剪贴板="abc",`T=""`;SIG 含 textEdited("")/textChanged("");paste 回来恢复 |
| E13 | `setText("abc")`,setMaxLength(5) | `setSel(1,1); paste("XY")` | 替换选中 "b":`T="aXYc" C=4`(删除段后光标 2,再插入 2 字符);paste 独立 undo 组,一次 undo 回 `T="abc"` |
| E14 | `setText("abc"); setSel(1,2)`(S=[1,3) C=3) | `del()` | `T="a" C=1`;undo 一次恢复 `T="abc"` 且**选区一并恢复** `S=[1,3) C=3` |
| E15 | echo=Password,`setText("secret")`,全选 | `copy()` | 剪贴板为空(密码禁复制);echo=Normal 同操作剪贴板=="secret" |
| E16 | readOnly=true | `key("a"); backspace(); undo()` | 全部无效,T 不变;`Enter` 仍发 accepted+editingFinished(空文本无掩码即合法) |
| E17 | — | `setText("abc")`;`setCursorPosition(99)` | `C=3` 不变(pos>size 忽略);`setCursorPosition(-5)` → `C=0` |
| E18 | validator=正则 `[ab]+`(fixup 为 no-op) | `setText("xyz")` 后 `K(Return)` | setText 路径 validate 判 Invalid → 仅 `SIG[inputRejected]` + textChanged("xyz")(文本保留、不回滚);Enter 时 hasAcceptableInput==false → fixup 无效 → **不发** accepted/editingFinished,event 被 ignore |
| E19 | validator=QIntValidator(0,999) | `setText("1234")` 后 `K(Return)` | setText 时 validate("1234")=Intermediate → 保留(无 inputRejected);Enter:hasAcceptableInput==false → fixup 钳到 "999" → `internalSetText`(edited=false,清 undo 历史)发 textChanged("999"),随后 **accepted → editingFinished** 依次发射 |
| E20 | — | `setText("abc"); setSel(0,3);` `deselect()` | `S=-`;SIG[selectionChanged] 仅此(无 text 系信号) |

> 注:E4 的"悬挂代理"显示效果依赖 QTextLayout 对畸形串的容错,断言只约束 `T` 的长度与代理属性,不约束 `D`;其余用例均为源码可推导的唯一期望值。

### 4.1 信号计数抽查(可选附加断言)

| 序列 | 期望信号 |
|---|---|
| `key("a")`(空文本,Normal) | displayTextChanged("a") → textEdited("a") → textChanged("a") → cursorPositionChanged(0,1);无 selectionChanged |
| `setText("a")` | resetInputContext → displayTextChanged("a") → textChanged("a")(无 textEdited)→ cursorPositionChanged(0,1) |
| `key("a");key("a")`(连续) | 第二键:displayTextChanged("aa") → textEdited → textChanged → cursorPositionChanged(1,2) |
| `key("a")`(掩码满员/maxLength 满员) | updateMicroFocus + inputRejected(无 text 系/displayTextChanged) |
| `selectAll()`(文本经 setText 预置,光标在尾) | 仅 selectionChanged(cursor 已是 size,cursorPositionChanged 不发);若光标不在尾则再发 cursorPositionChanged(old,size) |

---

## 5. 对拍注意事项(实现陷阱汇总)

1. `textChanged` 的参数是 `text()`(**掩码剥离后**),掩码模式下与 `displayText` 不同。
2. `moveCursor(mark=true)` **无条件**发 `selectionChanged`,即使选区未变。
3. `undo()`/`redo()` 发 `textEdited`;`setText()`/`clear()` 不发。
4. PasswordEchoOnEdit 下按 Left/Right 也会清空重输(Qt 有意/已知怪癖,cpp:1682);Up/Down/Back/Select 仅在 keypad navigation 构建下豁免。
5. 密码模式 `undo()` 是 `clear()` 而非历史回滚;redo 恒禁用。
6. 掩码模式下 `m_text` 恒定长 maxLength;`clear()` 得到模板而非空串;backspace 不缩短文本只置 blank。
7. 选区删除的 undo 命令在"光标在选区内"时走特殊拆分(selEnd=1 标记),保证 undo 后光标停在原 m_cursor。
8. `setSelection` 先 `commitPreedit`,移动光标即提交 IM。
9. NoEcho 丢弃 preedit;Password 的 preedit 也要打码。
10. `finishChange` 中 validator 改写文本时经 `internalSetText`(清空 undo 历史)并直接 `return true`,跳过本次后续信号收敛(信号由 internalSetText 内部 finishChange 发出)。
11. `isAcceptableInput`:Ctrl / Ctrl+Shift 修饰的文本键拒绝(AltGr=Ctrl+Alt 放行);Other_Format 字符恒放行;LineEdit 不收 Tab。
12. 光标闪烁与 `updateNeeded(rect)`:有掩码时 rect 为空(全区域重绘),否则仅 cursorRect。
