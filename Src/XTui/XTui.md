# XTui 通用 TUI 模块

XTui 是 XinYueC 的轻量通用文本界面库，适合嵌入式裁剪。提供屏幕缓冲、ANSI
终端控制、控件虚函数基类、会话输入解析，以及两个示例控件（Box、TextEdit）。

已复用的既有类型，XTui 不重复定义：

- 几何类型（XPoint/XSize/XRect）直接复用 `XGui/XGuiTypes.h`。
- 颜色类型直接复用 `XData/XColor/XColor.h` 的 `XColor`；终端 16 色映射为
  XTui 内部调色板索引，屏幕单元格仍只保存 1 字节索引以保持轻量。
- TUI 键盘事件 `XTuiKeyEvent` 继承 `XEvent`，修饰键复用
  `XKeyboardModifier_*`。

## 目录结构

```
Src/XTui/
  XTuiConfig.h      编译期开关与固定容量
  XTuiTypes.h/.c    按键事件、颜色工具、属性（复用 XGui/XGuiTypes.h 与 XColor）
  XTuiScreen.h/.c   离屏单元格缓冲
  XTuiTerminal.h/.c ANSI 终端控制（输出走回调）
  XTuiWidget.h/.c   控件基类（虚函数：Render/KeyPress/Resize/Focus）
  XTuiBox.h/.c      带边框标题的盒式控件
  XTuiTextEdit.h/.c 单行文本编辑控件
  XTui.h/.c         TUI 会话：装配 + 差异绘制 + 输入解析
```

## 裁剪配置

`CXinYueConfig.h` 已包含 `XTui/XTuiConfig.h`，默认 `XTUI_ON=1`。
可用的开关：

| 宏 | 默认 | 作用 |
|----|------|------|
| `XTUI_ON` | 1 | 模块总开关 |
| `XTUI_SCREEN_ON` | 1 | 屏幕缓冲模块 |
| `XTUI_TERMINAL_ON` | 1 | ANSI 终端控制模块 |
| `XTUI_WIDGET_ON` | 1 | 控件基类与示例控件 |
| `XTUI_SESSION_ON` | 1 | TUI 会话与输入解析 |

容量上限（可编译期覆盖，面向嵌入式）：

| 宏 | 默认 |
|----|------|
| `XTUI_SCREEN_MAX_COLUMNS` | 256 |
| `XTUI_SCREEN_MAX_ROWS` | 64 |
| `XTUI_CELL_UTF8_MAX` | 4 |
| `XTUI_INPUT_BUFFER_SIZE` | 64 |
| `XTUI_TEXTEDIT_MAX_BYTES` | 256 |

## 使用示意

```c
#include "XTui.h"
#include "XTuiBox.h"
#include "XTuiTextEdit.h"

static bool writeOut(void* ctx, const char* data, size_t len)
{
    /* 接入 SSH / Telnet / 串口 / 本地控制台 */
    return myWrite(ctx, data, len);
}

XTui* tui = XTui_create();
XTuiTerminal* term = XTuiTerminal_create();
XTuiScreen* screen = XTuiScreen_create_ex(80, 24);
XTuiBox* box = XTuiBox_create();
XTuiTextEdit* edit = XTuiTextEdit_create();

XTuiTerminal_setWriteCallback(term, writeOut, sock);
XTui_setTerminal(tui, term);
XTui_setScreen(tui, screen);
XTui_setRootWidget(tui, (XTuiWidget*)box);
XTui_setFocusWidget(tui, (XTuiWidget*)edit);

XTuiWidget_setRect((XTuiWidget*)box, (XRect){1, 1, 78, 22});
XTuiWidget_setRect((XTuiWidget*)edit, (XRect){3, 3, 40, 1});
XTuiBox_setTitle(box, " Demo ");
XTuiTextEdit_setText(edit, "hello");

XTui_start(tui);
/* 收到远端数据时调用 */
XTui_feedInput(tui, data, len);
/* 需要重绘时调用 */
XTui_refresh(tui);
```

## 输入解析

`XTui_feedInput` 逐字节解析：

- 普通可打印字符：按 UTF-8 累积后派发 `XTuiKey_Char`
- `ESC [` / `ESC O`：方向键、Home/End、PageUp/PageDown、Insert/Delete、F1-F12
- `\r`/`\n`：Enter；`\t`：Tab；`0x7F`/`0x08`：Backspace
- `Ctrl+A`~`Ctrl+Z`：以 `XKeyboardModifier_ControlModifier` 的 `Char` 事件派发

## 轻量化说明

- 屏幕单元格只保存 1 字节调色板索引，不保存完整 `XColor`，保持内存轻量。
- 屏幕缓冲可切换动态/固定模式（`XTUI_SCREEN_DYNAMIC_BUFFER`）。
- 终端输出完全通过回调，不直接操作文件描述符。
- 差异绘制只输出发生变化的单元格，减少协议流量。
- `XTuiKeyEvent` 继承 `XEvent`，可复用事件队列/接受状态等基础设施。
